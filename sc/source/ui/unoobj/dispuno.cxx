/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <sfx2/viewfrm.hxx>
#include <svx/dataaccessdescriptor.hxx>
#include <svl/hint.hxx>
#include <vcl/svapp.hxx>

#include <com/sun/star/frame/XDispatchProviderInterception.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <com/sun/star/sdb/CommandType.hpp>

#include <dispuno.hxx>
#include <tabvwsh.hxx>
#include <dbdocfun.hxx>
#include <dbdata.hxx>

using namespace com::sun::star;

const char cURLInsertColumns[] = ".uno:DataSourceBrowser/InsertColumns"; //data into text
constexpr OUString cURLDocDataSource = u".uno:DataSourceBrowser/DocumentDataSource"_ustr;

static uno::Reference<view::XSelectionSupplier> lcl_GetSelectionSupplier( const SfxViewShell* pViewShell )
{
    if ( pViewShell )
    {
        SfxViewFrame& rViewFrame = pViewShell->GetViewFrame();
        return uno::Reference<view::XSelectionSupplier>( rViewFrame.GetFrame().GetController(), uno::UNO_QUERY );
    }
    return uno::Reference<view::XSelectionSupplier>();
}

ScDispatchProviderInterceptor::ScDispatchProviderInterceptor(ScTabViewShell* pViewSh) :
    pViewShell( pViewSh )
{
    if ( !pViewShell )
        return;

    m_xIntercepted.set(uno::Reference<frame::XDispatchProviderInterception>(pViewShell->GetViewFrame().GetFrame().GetFrameInterface(), uno::UNO_QUERY));
    if (m_xIntercepted.is())
    {
        osl_atomic_increment( &m_refCount );

        m_xIntercepted->registerDispatchProviderInterceptor(
                    static_cast<frame::XDispatchProviderInterceptor*>(this));
        // this should make us the top-level dispatch-provider for the component, via a call to our
        // setDispatchProvider we should have got a fallback for requests we (i.e. our master) cannot fulfill
        uno::Reference<lang::XComponent> xInterceptedComponent(m_xIntercepted, uno::UNO_QUERY);
        if (xInterceptedComponent.is())
            xInterceptedComponent->addEventListener(static_cast<lang::XEventListener*>(this));

        osl_atomic_decrement( &m_refCount );
    }

    StartListening(*pViewShell);
}

ScDispatchProviderInterceptor::~ScDispatchProviderInterceptor()
{
    if (pViewShell)
        EndListening(*pViewShell);
}

void ScDispatchProviderInterceptor::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::Dying )
        pViewShell = nullptr;
}

// XDispatchProvider

uno::Reference<frame::XDispatch> SAL_CALL ScDispatchProviderInterceptor::queryDispatch(
                        const util::URL& aURL, const OUString& aTargetFrameName,
                        sal_Int32 nSearchFlags )
{
    SolarMutexGuard aGuard;

    uno::Reference<frame::XDispatch> xResult;
    // create some dispatch ...
    if ( pViewShell && (
        aURL.Complete == cURLInsertColumns ||
        aURL.Complete == cURLDocDataSource ) )
    {
        if (!m_xMyDispatch.is())
            m_xMyDispatch = new ScDispatch( pViewShell );
        xResult = m_xMyDispatch;
    }

    // ask our slave provider
    if (!xResult.is() && m_xSlaveDispatcher.is())
        xResult = m_xSlaveDispatcher->queryDispatch(aURL, aTargetFrameName, nSearchFlags);

    return xResult;
}

uno::Sequence< uno::Reference<frame::XDispatch> > SAL_CALL
                        ScDispatchProviderInterceptor::queryDispatches(
                        const uno::Sequence<frame::DispatchDescriptor>& aDescripts )
{
    SolarMutexGuard aGuard;

    uno::Sequence< uno::Reference< frame::XDispatch> > aReturn(aDescripts.getLength());
    std::transform(aDescripts.begin(), aDescripts.end(), aReturn.getArray(),
        [this](const frame::DispatchDescriptor& rDescr) -> uno::Reference<frame::XDispatch> {
            return queryDispatch(rDescr.FeatureURL, rDescr.FrameName, rDescr.SearchFlags); });
    return aReturn;
}

// XDispatchProviderInterceptor

uno::Reference<frame::XDispatchProvider> SAL_CALL
                        ScDispatchProviderInterceptor::getSlaveDispatchProvider()
{
    SolarMutexGuard aGuard;
    return m_xSlaveDispatcher;
}

void SAL_CALL ScDispatchProviderInterceptor::setSlaveDispatchProvider(
                        const uno::Reference<frame::XDispatchProvider>& xNewDispatchProvider )
{
    SolarMutexGuard aGuard;
    m_xSlaveDispatcher.set(xNewDispatchProvider);
}

uno::Reference<frame::XDispatchProvider> SAL_CALL
                        ScDispatchProviderInterceptor::getMasterDispatchProvider()
{
    SolarMutexGuard aGuard;
    return m_xMasterDispatcher;
}

void SAL_CALL ScDispatchProviderInterceptor::setMasterDispatchProvider(
                        const uno::Reference<frame::XDispatchProvider>& xNewSupplier )
{
    SolarMutexGuard aGuard;
    m_xMasterDispatcher.set(xNewSupplier);
}

// XEventListener

void SAL_CALL ScDispatchProviderInterceptor::disposing( const lang::EventObject& /* Source */ )
{
    SolarMutexGuard aGuard;

    if (m_xIntercepted.is())
    {
        m_xIntercepted->releaseDispatchProviderInterceptor(
                static_cast<frame::XDispatchProviderInterceptor*>(this));
        uno::Reference<lang::XComponent> xInterceptedComponent(m_xIntercepted, uno::UNO_QUERY);
        if (xInterceptedComponent.is())
            xInterceptedComponent->removeEventListener(static_cast<lang::XEventListener*>(this));

        m_xMyDispatch = nullptr;
    }
    m_xIntercepted = nullptr;
}

ScDispatch::ScDispatch(ScTabViewShell* pViewSh) :
    pViewShell( pViewSh ),
    bListeningToView( false )
{
    if (pViewShell)
        StartListening(*pViewShell);
}

ScDispatch::~ScDispatch()
{
    if (pViewShell)
        EndListening(*pViewShell);

    if (bListeningToView && pViewShell)
    {
        uno::Reference<view::XSelectionSupplier> xSupplier(lcl_GetSelectionSupplier( pViewShell ));
        if ( xSupplier.is() )
            xSupplier->removeSelectionChangeListener(this);
    }
}

void ScDispatch::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::Dying )
        pViewShell = nullptr;
}

// XDispatch

void SAL_CALL ScDispatch::dispatch( const util::URL& aURL,
                                const uno::Sequence<beans::PropertyValue>& aArgs )
{
    SolarMutexGuard aGuard;

    bool bDone = false;
    if ( pViewShell && aURL.Complete == cURLInsertColumns )
    {
        ScViewData& rViewData = pViewShell->GetViewData();
        ScAddress aPos( rViewData.GetCurX(), rViewData.GetCurY(), rViewData.GetTabNo() );

        ScDBDocFunc aFunc( rViewData.GetDocShell() );
        aFunc.DoImportUno( aPos, aArgs );
        bDone = true;
    }
    // cURLDocDataSource is never dispatched

    if (!bDone)
        throw uno::RuntimeException();
}

static void lcl_FillDataSource( frame::FeatureStateEvent& rEvent, const ScImportParam& rParam )
{
    rEvent.IsEnabled = rParam.bImport;

    svx::ODataAccessDescriptor aDescriptor;
    if ( rParam.bImport )
    {
        sal_Int32 nType = rParam.bSql ? sdb::CommandType::COMMAND :
                    ( (rParam.nType == ScDbQuery) ? sdb::CommandType::QUERY :
                                                    sdb::CommandType::TABLE );

        aDescriptor.setDataSource(rParam.aDBName);
        aDescriptor[svx::DataAccessDescriptorProperty::Command]     <<= rParam.aStatement;
        aDescriptor[svx::DataAccessDescriptorProperty::CommandType] <<= nType;
    }
    else
    {
        //  descriptor has to be complete anyway

        aDescriptor[svx::DataAccessDescriptorProperty::DataSource]  <<= OUString();
        aDescriptor[svx::DataAccessDescriptorProperty::Command]     <<= OUString();
        aDescriptor[svx::DataAccessDescriptorProperty::CommandType] <<= sal_Int32(sdb::CommandType::TABLE);
    }
    rEvent.State <<= aDescriptor.createPropertyValueSequence();
}

void SAL_CALL ScDispatch::addStatusListener(
    const uno::Reference<frame::XStatusListener>& xListener,
    const util::URL& aURL)
{
    SolarMutexGuard aGuard;

    if (!pViewShell)
        throw uno::RuntimeException();

    //  initial state
    frame::FeatureStateEvent aEvent;
    aEvent.IsEnabled = true;
    aEvent.Source = getXWeak();
    aEvent.FeatureURL = aURL;

    if ( aURL.Complete == cURLDocDataSource )
    {
        aDataSourceListeners.emplace_back( xListener );

        if (!bListeningToView)
        {
            uno::Reference<view::XSelectionSupplier> xSupplier(lcl_GetSelectionSupplier( pViewShell ));
            if ( xSupplier.is() )
                xSupplier->addSelectionChangeListener(this);
            bListeningToView = true;
        }

        ScDBData* pDBData = pViewShell->GetDBData(false,SC_DB_OLD);
        if ( pDBData )
            pDBData->GetImportParam( aLastImport );
        lcl_FillDataSource( aEvent, aLastImport );          // modifies State, IsEnabled
    }
    //! else add to listener for "enabled" changes?

    xListener->statusChanged( aEvent );
}

void SAL_CALL ScDispatch::removeStatusListener(
                                const uno::Reference<frame::XStatusListener>& xListener,
                                const util::URL& aURL )
{
    SolarMutexGuard aGuard;

    if ( aURL.Complete != cURLDocDataSource )
        return;

    sal_uInt16 nCount = aDataSourceListeners.size();
    for ( sal_uInt16 n=nCount; n--; )
    {
        uno::Reference<frame::XStatusListener>& rObj = aDataSourceListeners[n];
        if ( rObj == xListener )
        {
            aDataSourceListeners.erase( aDataSourceListeners.begin() + n );
            break;
        }
    }

    if ( aDataSourceListeners.empty() && pViewShell )
    {
        uno::Reference<view::XSelectionSupplier> xSupplier(lcl_GetSelectionSupplier( pViewShell ));
        if ( xSupplier.is() )
            xSupplier->removeSelectionChangeListener(this);
        bListeningToView = false;
    }
}

// XSelectionChangeListener

void SAL_CALL ScDispatch::selectionChanged( const css::lang::EventObject& /* aEvent */ )
{
    //  currently only called for URL cURLDocDataSource

    if ( !pViewShell )
        return;

    ScImportParam aNewImport;
    ScDBData* pDBData = pViewShell->GetDBData(false,SC_DB_OLD);
    if ( pDBData )
        pDBData->GetImportParam( aNewImport );

    //  notify listeners only if data source has changed
    if ( !(aNewImport.bImport    != aLastImport.bImport ||
         aNewImport.aDBName    != aLastImport.aDBName ||
         aNewImport.aStatement != aLastImport.aStatement ||
         aNewImport.bSql       != aLastImport.bSql ||
         aNewImport.nType      != aLastImport.nType) )
        return;

    frame::FeatureStateEvent aEvent;
    aEvent.Source = getXWeak();
    aEvent.FeatureURL.Complete = cURLDocDataSource;

    lcl_FillDataSource( aEvent, aNewImport );       // modifies State, IsEnabled

    for (uno::Reference<frame::XStatusListener> & xDataSourceListener : aDataSourceListeners)
        xDataSourceListener->statusChanged( aEvent );

    aLastImport = aNewImport;
}

// XEventListener

void SAL_CALL ScDispatch::disposing( const css::lang::EventObject& rSource )
{
    uno::Reference<view::XSelectionSupplier> xSupplier(rSource.Source, uno::UNO_QUERY);
    xSupplier->removeSelectionChangeListener(this);
    bListeningToView = false;

    lang::EventObject aEvent;
    aEvent.Source = getXWeak();
    for (uno::Reference<frame::XStatusListener> & xDataSourceListener : aDataSourceListeners)
        xDataSourceListener->disposing( aEvent );

    pViewShell = nullptr;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
