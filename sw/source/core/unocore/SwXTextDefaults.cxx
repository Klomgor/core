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

#include <com/sun/star/beans/PropertyAttribute.hpp>

#include <vcl/svapp.hxx>
#include <osl/diagnose.h>
#include <svl/itemprop.hxx>

#include <SwXTextDefaults.hxx>
#include <SwStyleNameMapper.hxx>
#include <fchrfmt.hxx>
#include <charfmt.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <docstyle.hxx>
#include <doc.hxx>
#include <docsh.hxx>
#include <unomap.hxx>
#include <unomid.h>
#include <paratr.hxx>
#include <unocrsrhelper.hxx>
#include <hintids.hxx>
#include <fmtpdsc.hxx>
#include <names.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::lang;

SwXTextDefaults::SwXTextDefaults ( SwDoc * pNewDoc ) :
    m_pPropSet( aSwMapProvider.GetPropertySet( PROPERTY_MAP_TEXT_DEFAULT ) ),
    m_pDoc   ( pNewDoc )
{
}

SwXTextDefaults::~SwXTextDefaults ()
{
}

uno::Reference< XPropertySetInfo > SAL_CALL SwXTextDefaults::getPropertySetInfo(  )
{
    static uno::Reference < XPropertySetInfo > xRef = m_pPropSet->getPropertySetInfo();
    return xRef;
}

void SAL_CALL SwXTextDefaults::setPropertyValue( const OUString& rPropertyName, const Any& aValue )
{
    SolarMutexGuard aGuard;
    if (!m_pDoc)
        throw RuntimeException();
    const SfxItemPropertyMapEntry *pMap = m_pPropSet->getPropertyMap().getByName( rPropertyName );
    if (!pMap)
        throw UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );
    if ( pMap->nFlags & PropertyAttribute::READONLY)
        throw PropertyVetoException ( "Property is read-only: " + rPropertyName, getXWeak() );

    const SfxPoolItem& rItem = m_pDoc->GetDefault(pMap->nWID);
    if (RES_PAGEDESC == pMap->nWID && MID_PAGEDESC_PAGEDESCNAME == pMap->nMemberId)
    {
        SfxItemSetFixed<RES_PAGEDESC, RES_PAGEDESC> aSet( m_pDoc->GetAttrPool() );
        aSet.Put(rItem);
        SwUnoCursorHelper::SetPageDesc( aValue, *m_pDoc, aSet );
        m_pDoc->SetDefault(aSet.Get(RES_PAGEDESC));
    }
    else if ((RES_PARATR_DROP == pMap->nWID && MID_DROPCAP_CHAR_STYLE_NAME == pMap->nMemberId) ||
             (RES_TXTATR_CHARFMT == pMap->nWID))
    {
        OUString uStyle;
        if(!(aValue >>= uStyle))
            throw lang::IllegalArgumentException();

        UIName sStyle;
        SwStyleNameMapper::FillUIName(ProgName(uStyle), sStyle, SwGetPoolIdFromName::ChrFmt );
        SwDocStyleSheet* pStyle =
            static_cast<SwDocStyleSheet*>(m_pDoc->GetDocShell()->GetStyleSheetPool()->Find(sStyle.toString(), SfxStyleFamily::Char));
        std::unique_ptr<SwFormatDrop> pDrop;
        std::unique_ptr<SwFormatCharFormat> pCharFormat;
        if(!pStyle)
            throw lang::IllegalArgumentException();

        rtl::Reference< SwDocStyleSheet > xStyle( new SwDocStyleSheet( *pStyle ) );
        if (xStyle->GetCharFormat() == m_pDoc->GetDfltCharFormat())
            return; // don't SetCharFormat with formats from mpDfltCharFormat

        if (RES_PARATR_DROP == pMap->nWID)
        {
            pDrop.reset(static_cast<SwFormatDrop*>(rItem.Clone()));   // because rItem is const...
            pDrop->SetCharFormat(xStyle->GetCharFormat());
            m_pDoc->SetDefault(*pDrop);
        }
        else // RES_TXTATR_CHARFMT == pMap->nWID
        {
            pCharFormat.reset(static_cast<SwFormatCharFormat*>(rItem.Clone()));   // because rItem is const...
            pCharFormat->SetCharFormat(xStyle->GetCharFormat());
            m_pDoc->SetDefault(*pCharFormat);
        }
    }
    else
    {
        std::unique_ptr<SfxPoolItem> pNewItem(rItem.Clone());
        pNewItem->PutValue( aValue, pMap->nMemberId);
        m_pDoc->SetDefault(*pNewItem);
    }
}

Any SAL_CALL SwXTextDefaults::getPropertyValue( const OUString& rPropertyName )
{
    SolarMutexGuard aGuard;
    if (!m_pDoc)
        throw RuntimeException();
    const SfxItemPropertyMapEntry *pMap = m_pPropSet->getPropertyMap().getByName( rPropertyName );
    if (!pMap)
        throw UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );
    Any aRet;
    const SfxPoolItem& rItem = m_pDoc->GetDefault(pMap->nWID);
    rItem.QueryValue( aRet, pMap->nMemberId );
    return aRet;
}

void SAL_CALL SwXTextDefaults::addPropertyChangeListener( const OUString& /*rPropertyName*/, const uno::Reference< XPropertyChangeListener >& /*xListener*/ )
{
    OSL_FAIL ( "not implemented" );
}

void SAL_CALL SwXTextDefaults::removePropertyChangeListener( const OUString& /*rPropertyName*/, const uno::Reference< XPropertyChangeListener >& /*xListener*/ )
{
    OSL_FAIL ( "not implemented" );
}

void SAL_CALL SwXTextDefaults::addVetoableChangeListener( const OUString& /*rPropertyName*/, const uno::Reference< XVetoableChangeListener >& /*xListener*/ )
{
    OSL_FAIL ( "not implemented" );
}

void SAL_CALL SwXTextDefaults::removeVetoableChangeListener( const OUString& /*rPropertyName*/, const uno::Reference< XVetoableChangeListener >& /*xListener*/ )
{
    OSL_FAIL ( "not implemented" );
}

// XPropertyState
PropertyState SAL_CALL SwXTextDefaults::getPropertyState( const OUString& rPropertyName )
{
    SolarMutexGuard aGuard;
    PropertyState eRet = PropertyState_DIRECT_VALUE;
    if (!m_pDoc)
        throw RuntimeException();
    const SfxItemPropertyMapEntry *pMap = m_pPropSet->getPropertyMap().getByName( rPropertyName );
    if (!pMap)
        throw UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );

    const SfxPoolItem& rItem = m_pDoc->GetDefault(pMap->nWID);
    if (IsStaticDefaultItem ( &rItem ) )
        eRet = PropertyState_DEFAULT_VALUE;
    return eRet;
}

Sequence< PropertyState > SAL_CALL SwXTextDefaults::getPropertyStates( const Sequence< OUString >& rPropertyNames )
{
    const sal_Int32 nCount = rPropertyNames.getLength();
    Sequence < PropertyState > aRet ( nCount );

    std::transform(rPropertyNames.begin(), rPropertyNames.end(), aRet.getArray(),
        [this](const OUString& rName) -> PropertyState { return getPropertyState(rName); });

    return aRet;
}

void SAL_CALL SwXTextDefaults::setPropertyToDefault( const OUString& rPropertyName )
{
    if (!m_pDoc)
        throw RuntimeException();
    const SfxItemPropertyMapEntry *pMap = m_pPropSet->getPropertyMap().getByName( rPropertyName );
    if (!pMap)
        throw UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );
    if ( pMap->nFlags & PropertyAttribute::READONLY)
        throw RuntimeException( "setPropertyToDefault: property is read-only: " + rPropertyName, getXWeak() );
    SfxItemPool& rSet (m_pDoc->GetAttrPool());
    rSet.ResetUserDefaultItem ( pMap->nWID );
}

Any SAL_CALL SwXTextDefaults::getPropertyDefault( const OUString& rPropertyName )
{
    if (!m_pDoc)
        throw RuntimeException();
    const SfxItemPropertyMapEntry *pMap = m_pPropSet->getPropertyMap().getByName( rPropertyName );
    if (!pMap)
        throw UnknownPropertyException( "Unknown property: " + rPropertyName, getXWeak() );
    Any aRet;
    SfxItemPool& rSet (m_pDoc->GetAttrPool());
    SfxPoolItem const*const pItem = rSet.GetUserDefaultItem(pMap->nWID);
    if (pItem)
    {
        pItem->QueryValue( aRet, pMap->nMemberId );
    }
    return aRet;
}

OUString SAL_CALL SwXTextDefaults::getImplementationName(  )
{
    return u"SwXTextDefaults"_ustr;
}

sal_Bool SAL_CALL SwXTextDefaults::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence< OUString > SAL_CALL SwXTextDefaults::getSupportedServiceNames(  )
{
    return { u"com.sun.star.text.Defaults"_ustr,
             u"com.sun.star.style.CharacterProperties"_ustr,
             u"com.sun.star.style.CharacterPropertiesAsian"_ustr,
             u"com.sun.star.style.CharacterPropertiesComplex"_ustr,
             u"com.sun.star.style.ParagraphProperties"_ustr,
             u"com.sun.star.style.ParagraphPropertiesAsian"_ustr,
             u"com.sun.star.style.ParagraphPropertiesComplex"_ustr };
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
