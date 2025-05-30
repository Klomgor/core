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

#include <config_features.h>
#include <config_fuzzers.h>
#include <config_wasm_strip.h>

#include <comphelper/propertysequence.hxx>
#include <comphelper/servicehelper.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/event.hxx>
#include <sfx2/objitem.hxx>
#include <svx/dataaccessdescriptor.hxx>
#include <svtools/restartdialog.hxx>
#include <svl/eitem.hxx>
#include <svl/whiter.hxx>
#include <svl/stritem.hxx>
#include <svl/voiditem.hxx>
#include <sfx2/lokhelper.hxx>
#include <sfx2/request.hxx>
#include <sfx2/fcontnr.hxx>
#include <svl/ctloptions.hxx>
#include <svtools/colorcfg.hxx>
#include <svtools/accessibilityoptions.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <unotools/useroptions.hxx>
#include <com/sun/star/document/UpdateDocMode.hpp>
#include <sfx2/docfile.hxx>
#include <sfx2/objface.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <o3tl/string_view.hxx>
#include <PostItMgr.hxx>

#include <view.hxx>
#include <pview.hxx>
#include <srcview.hxx>
#include <wrtsh.hxx>
#include <docsh.hxx>
#include <cmdid.h>
#include <initui.hxx>
#include <uitool.hxx>
#include <swmodule.hxx>
#include <wview.hxx>
#include <usrpref.hxx>
#include <gloslst.hxx>
#include <glosdoc.hxx>
#include <doc.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <prtopt.hxx>
#include <modcfg.hxx>
#include <fontcfg.hxx>
#include <barcfg.hxx>
#include <navicfg.hxx>
#include <uinums.hxx>
#include <dbconfig.hxx>
#include <mmconfigitem.hxx>
#include <strings.hrc>
#include <unotxdoc.hxx>
#include <com/sun/star/container/XChild.hpp>
#include <com/sun/star/sdbc/XConnection.hpp>
#include <com/sun/star/sdb/TextConnectionSettings.hpp>
#include <com/sun/star/sdbc/XDataSource.hpp>
#include <com/sun/star/task/OfficeRestartManager.hpp>
#include <org/freedesktop/PackageKit/SyncDbusSessionHelper.hpp>
#include <swabstdlg.hxx>
#include <comphelper/dispatchcommand.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/lok.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>

#include <salhelper/simplereferenceobject.hxx>
#include <rtl/ref.hxx>

#include <officecfg/Office/Common.hxx>

using namespace ::com::sun::star;

// Slotmaps for the application's methods

// here are the SlotID's being included
// see Idl-file
#define ShellClass_SwModule
#include <sfx2/msg.hxx>
#include <swslots.hxx>

SFX_IMPL_INTERFACE(SwModule, SfxModule)

void SwModule::InitInterface_Impl()
{
    GetStaticInterface()->RegisterStatusBar(StatusBarId::WriterStatusBar);

    GetStaticInterface()->RegisterObjectBar(SFX_OBJECTBAR_APPLICATION,
                                            SfxVisibilityFlags::Standard | SfxVisibilityFlags::Client | SfxVisibilityFlags::Viewer,
                                            ToolbarId::Module_Toolbox);
}

// other states
void SwModule::StateOther(SfxItemSet &rSet)
{
    SfxWhichIter aIter(rSet);
    sal_uInt16 nWhich = aIter.FirstWhich();

    SwView* pActView = ::GetActiveView();
    bool bWebView = dynamic_cast<SwWebView*>( pActView ) !=  nullptr;

    while(nWhich)
    {
        switch(nWhich)
        {
            case FN_BUSINESS_CARD:
            case FN_LABEL:
            case FN_ENVELOP:
            {
                bool bDisable = false;
                SfxViewShell* pCurrView = SfxViewShell::Current();
                if( !pCurrView || dynamic_cast< const SwView *>( pCurrView ) ==  nullptr )
                    bDisable = true;
                SwDocShell *pDocSh = static_cast<SwDocShell*>( SfxObjectShell::Current());
                if ( bDisable ||
                    (pDocSh &&    (pDocSh->IsReadOnly() ||
                                  pDocSh->GetCreateMode() == SfxObjectCreateMode::EMBEDDED)) )
                    rSet.DisableItem( nWhich );

            }
            break;
            case FN_XFORMS_INIT:
                // slot is always active!
                break;
            case FN_EDIT_FORMULA:
                {
                    SwWrtShell* pSh = nullptr;
                    SelectionType nSelection = SelectionType::NONE;
                    if( pActView )
                        pSh = &pActView->GetWrtShell();
                    if( pSh )
                        nSelection = pSh->GetSelectionType();

                    if( (pSh && pSh->HasSelection()) ||
                        !(nSelection & (SelectionType::Text | SelectionType::Table)))
                        rSet.DisableItem(nWhich);
                }
            break;
            case SID_ATTR_METRIC:
                rSet.Put( SfxUInt16Item( SID_ATTR_METRIC, static_cast< sal_uInt16 >(::GetDfltMetric(bWebView))));
            break;
            case FN_SET_MODOPT_TBLNUMFMT:
                rSet.Put( SfxBoolItem( nWhich, m_pModuleConfig->
                                            IsInsTableFormatNum( bWebView )));
            break;
            case FN_MAILMERGE_WIZARD:
            {
                SfxObjectShell* pObjectShell = GetObjectShell();
                if (pObjectShell && pObjectShell->isExportLocked())
                    rSet.DisableItem(nWhich);
                break;
            }
            case FN_MAILMERGE_FIRST_ENTRY:
            case FN_MAILMERGE_PREV_ENTRY:
            case FN_MAILMERGE_NEXT_ENTRY:
            case FN_MAILMERGE_LAST_ENTRY:
            {
                std::shared_ptr<SwMailMergeConfigItem> xConfigItem;
                if (SwView* pView = GetActiveView())
                    xConfigItem = pView->GetMailMergeConfigItem();
                if (!xConfigItem)
                    rSet.DisableItem(nWhich);
                else if (xConfigItem->GetConnection().is()
                         && !xConfigItem->GetConnection()->isClosed())
                {
                    bool bFirst, bLast;
                    bool bValid = xConfigItem->IsResultSetFirstLast(bFirst, bLast);

                    if (!bValid ||
                        (bFirst && (nWhich == FN_MAILMERGE_FIRST_ENTRY || nWhich == FN_MAILMERGE_PREV_ENTRY)) ||
                        (bLast && (nWhich == FN_MAILMERGE_LAST_ENTRY || nWhich == FN_MAILMERGE_NEXT_ENTRY)))
                    {
                        rSet.DisableItem(nWhich);
                    }
                }
            }
            break;
            case FN_MAILMERGE_CURRENT_ENTRY:
            case FN_MAILMERGE_EXCLUDE_ENTRY:
            {
                // just trigger calling statusChanged() of MMExcludeEntryController
                // resp. MMCurrentEntryController
                rSet.InvalidateItem(nWhich);
            }
            break;
            case FN_MAILMERGE_CREATE_DOCUMENTS:
            case FN_MAILMERGE_SAVE_DOCUMENTS:
            case FN_MAILMERGE_PRINT_DOCUMENTS:
            case FN_MAILMERGE_EMAIL_DOCUMENTS:
            {
                std::shared_ptr<SwMailMergeConfigItem> xConfigItem;
                if (SwView* pView = GetActiveView())
                    xConfigItem = pView->EnsureMailMergeConfigItem();

                // #i51949# hide e-Mail option if e-Mail is not supported
                // #i63267# printing might be disabled
                // Without attempting to open the database, (in case it is remote or passworded),
                // hide everything after determining there are no valid results. tdf#121606
                if (!xConfigItem ||
                    xConfigItem->GetCurrentDBData().sDataSource.isEmpty() ||
                    xConfigItem->GetCurrentDBData().sCommand.isEmpty() ||
                    (xConfigItem->GetConnection().is() && !xConfigItem->GetConnection()->isClosed() && !xConfigItem->GetResultSet().is()) ||
                    (nWhich == FN_MAILMERGE_PRINT_DOCUMENTS && Application::GetSettings().GetMiscSettings().GetDisablePrinting()) ||
                    (nWhich == FN_MAILMERGE_EMAIL_DOCUMENTS && !xConfigItem->IsMailAvailable()))
                {
                    rSet.DisableItem(nWhich);
                }
            }
            break;
            default:
                OSL_FAIL("::StateOther: default");
        }
        nWhich = aIter.NextWhich();
    }
}

// start field dialog
static void NewXForms( SfxRequest& rReq ); // implementation: below

std::shared_ptr<SwMailMergeConfigItem> SwView::EnsureMailMergeConfigItem(const SfxItemSet* pArgs)
{
    // create if it does not exist yet
    std::shared_ptr<SwMailMergeConfigItem> xMMConfig = GetMailMergeConfigItem();
    if (!xMMConfig)
    {
        xMMConfig = std::make_shared<SwMailMergeConfigItem>();
        xMMConfig->SetSourceView(this);

        //set the first used database as default source on the config item
        const SfxUnoAnyItem* pItem = nullptr;
        if (pArgs && (pItem = pArgs->GetItemIfSet(
               FN_PARAM_DATABASE_PROPERTIES, false)))
        {
            //mailmerge has been called from the database beamer
            uno::Sequence< beans::PropertyValue> aDBValues;
            if (pItem->GetValue() >>= aDBValues)
            {
                SwDBData aDBData;
                svx::ODataAccessDescriptor aDescriptor(aDBValues);
                aDescriptor[svx::DataAccessDescriptorProperty::DataSource]   >>= aDBData.sDataSource;
                aDescriptor[svx::DataAccessDescriptorProperty::Command]      >>= aDBData.sCommand;
                aDescriptor[svx::DataAccessDescriptorProperty::CommandType]  >>= aDBData.nCommandType;

                uno::Reference< sdbc::XConnection>          xConnection;
                uno::Reference< sdbc::XDataSource>          xSource;
                uno::Reference< sdbcx::XColumnsSupplier>    xColumnsSupplier;
                if (aDescriptor.has(svx::DataAccessDescriptorProperty::Connection))
                    aDescriptor[svx::DataAccessDescriptorProperty::Connection] >>= xConnection;
                uno::Reference<container::XChild> xChild(xConnection, uno::UNO_QUERY);
                if (xChild.is())
                    xSource.set(xChild->getParent(), uno::UNO_QUERY);
                xMMConfig->SetCurrentConnection(
                    xSource, SharedConnection(xConnection, SharedConnection::NoTakeOwnership),
                    xColumnsSupplier, aDBData);
            }
        }
        else
        {
            std::vector<OUString> aDBNameList;
            std::vector<OUString> aAllDBNames;
            GetWrtShell().GetAllUsedDB(aDBNameList, &aAllDBNames);
            if (!aDBNameList.empty())
            {
                const OUString& sDBName(aDBNameList[0]);
                SwDBData aDBData;
                sal_Int32 nIdx{ 0 };
                aDBData.sDataSource = sDBName.getToken(0, DB_DELIM, nIdx);
                aDBData.sCommand = sDBName.getToken(0, DB_DELIM, nIdx);
                aDBData.nCommandType = o3tl::toInt32(o3tl::getToken(sDBName, 0, DB_DELIM, nIdx));
                //set the currently used database for the wizard
                xMMConfig->SetCurrentDBData(aDBData);
            }
        }

        SetMailMergeConfigItem(xMMConfig);
    }
    return xMMConfig;
}

#if HAVE_FEATURE_DBCONNECTIVITY && !ENABLE_FUZZERS

namespace
{

SwView* lcl_LoadDoc(SwView* pView, const OUString& rURL)
{
    SwView* pNewView = nullptr;
    if(!rURL.isEmpty())
    {
        SfxStringItem aURL(SID_FILE_NAME, rURL);
        SfxStringItem aTargetFrameName( SID_TARGETNAME, u"_blank"_ustr );
        SfxBoolItem aHidden( SID_HIDDEN, true );
        SfxStringItem aReferer(SID_REFERER, pView->GetDocShell()->GetTitle());
        const SfxPoolItemHolder aResult(
            pView->GetViewFrame().GetDispatcher()->ExecuteList(SID_OPENDOC,
            SfxCallMode::SYNCHRON,
            { &aURL, &aHidden, &aReferer, &aTargetFrameName }));
        const SfxObjectItem* pItem(static_cast<const SfxObjectItem*>(aResult.getItem()));
        SfxShell* pShell = pItem ? pItem->GetShell() : nullptr;

        if(pShell)
        {
            SfxViewShell* pViewShell = pShell->GetViewShell();
            if(pViewShell)
            {
                pNewView = dynamic_cast<SwView*>(pViewShell);
                if (pNewView)
                {
                    pNewView->GetViewFrame().GetFrame().Appear();
                }
                else
                {
                    pViewShell->GetViewFrame().DoClose();
                }
            }
        }
    }
    else
    {
        SfxStringItem aFactory(SID_NEWDOCDIRECT, SwDocShell::Factory().GetFilterContainer()->GetName());
        const SfxPoolItemHolder aResult(pView->GetViewFrame().GetDispatcher()->ExecuteList(
            SID_NEWDOCDIRECT,
            SfxCallMode::SYNCHRON, { &aFactory }));
        const SfxFrameItem* pItem(static_cast<const SfxFrameItem*>(aResult.getItem()));
        SfxFrame* pFrame = pItem ? pItem->GetFrame() : nullptr;
        SfxViewFrame* pViewFrame = pFrame ? pFrame->GetCurrentViewFrame() : nullptr;
        pNewView = pViewFrame ? dynamic_cast<SwView*>( pViewFrame->GetViewShell() ) : nullptr;
    }

    return pNewView;
}

class SwMailMergeWizardExecutor : public salhelper::SimpleReferenceObject
{
    SwView*                  m_pView;       // never owner
    SwView*                  m_pView2Close; // never owner
    VclPtr<AbstractMailMergeWizard> m_pWizard;     // always owner
    VclPtr<AbstractMailMergeWizard> m_pWizardToDestroyInCallback;

    void EndDialogHdl(sal_Int32 nResponse);
    DECL_LINK( DestroyDialogHdl, void*, void );
    DECL_LINK( DestroyWizardHdl, void*, void );
    DECL_LINK( CancelHdl, void*, void );
    DECL_LINK( CloseFrameHdl, void*, void );

    void ExecutionFinished();
    void ExecuteWizard();

public:
    SwMailMergeWizardExecutor();
    virtual ~SwMailMergeWizardExecutor() override;

    void ExecuteMailMergeWizard( const SfxItemSet * pArgs );
};

SwMailMergeWizardExecutor::SwMailMergeWizardExecutor()
    : m_pView( nullptr ),
      m_pView2Close( nullptr ),
      m_pWizard( nullptr )
{
}

SwMailMergeWizardExecutor::~SwMailMergeWizardExecutor()
{
    OSL_ENSURE( m_pWizard == nullptr, "SwMailMergeWizardExecutor: m_pWizard must be Null!" );
}

bool lcl_hasAllComponentsAvailable()
{
    try
    {
        return css::sdb::TextConnectionSettings::create(comphelper::getProcessComponentContext()).is();
    }
    catch (const css::uno::Exception &)
    {
        TOOLS_INFO_EXCEPTION(
            "sw.core", "assuming Base to be missing; caught ");
        return false;
    }
}

void SwMailMergeWizardExecutor::ExecuteMailMergeWizard( const SfxItemSet * pArgs )
{
    if(!lcl_hasAllComponentsAvailable())
    {
        if (officecfg::Office::Common::PackageKit::EnableBaseInstallation::get())
        {
            try
            {
                using namespace org::freedesktop::PackageKit;
                using namespace svtools;
                css::uno::Reference< XSyncDbusSessionHelper > xSyncDbusSessionHelper(SyncDbusSessionHelper::create(comphelper::getProcessComponentContext()));
                const css::uno::Sequence< OUString > vPackages{ u"libreoffice-base"_ustr };
                xSyncDbusSessionHelper->InstallPackageNames(vPackages, OUString());
                SolarMutexGuard aGuard;
                (void)executeRestartDialog(comphelper::getProcessComponentContext(), nullptr, RESTART_REASON_MAILMERGE_INSTALL);
            }
            catch (const css::uno::Exception &)
            {
                TOOLS_INFO_EXCEPTION(
                    "sw.core",
                    "trying to install LibreOffice Base, caught");
                auto xRestartManager
                    = css::task::OfficeRestartManager::get(comphelper::getProcessComponentContext());
                if (!xRestartManager->isRestartRequested(false))
                {
                    // Base is absent, and could not initiate its install - ask user to do that manually
                    // Only show the dialog if restart is not initiated yet
                    std::unique_ptr<weld::MessageDialog> xWarnBox(Application::CreateMessageDialog(
                        nullptr, VclMessageType::Info, VclButtonsType::Ok,
                        SwResId(STR_NO_BASE_FOR_MERGE)));
                    xWarnBox->run();
                }
            }
        } else {
            auto xRestartManager
                = css::task::OfficeRestartManager::get(comphelper::getProcessComponentContext());
            if (!xRestartManager->isRestartRequested(false))
            {
                // Base is absent, and could not initiate its install - ask user to do that manually
                // Only show the dialog if restart is not initiated yet
                std::unique_ptr<weld::MessageDialog> xWarnBox(Application::CreateMessageDialog(
                    nullptr, VclMessageType::Info, VclButtonsType::Ok,
                    SwResId(STR_NO_BASE_FOR_MERGE)));
                xWarnBox->run();
            }
        }
        return;
    }
    if ( m_pView )
    {
        OSL_FAIL("SwMailMergeWizardExecutor::ExecuteMailMergeWizard: Already executing the wizard!" );
        return;
    }

    m_pView = ::GetActiveView();
    if (!m_pView)
        return;

    // keep self alive until done.
    acquire();

    // create if it does not exist yet
    std::shared_ptr<SwMailMergeConfigItem> xMMConfig = m_pView->EnsureMailMergeConfigItem(pArgs);

    SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
    m_pWizard = pFact->CreateMailMergeWizard(*m_pView, xMMConfig);

    ExecuteWizard();
}

void SwMailMergeWizardExecutor::ExecutionFinished()
{
    std::shared_ptr<SwMailMergeConfigItem> xMMConfig = m_pView->GetMailMergeConfigItem();
    if (xMMConfig)
        xMMConfig->Commit();

    SwDoc* pDoc = m_pView->GetDocShell()->GetDoc();
    if (pDoc)
    {
        SwDBManager* pDbManager = pDoc->GetDBManager();
        if (pDbManager)
            pDbManager->CommitLastRegistrations();

        // Show the toolbar
        m_pView->ShowUIElement(u"private:resource/toolbar/mailmerge"_ustr);

        // Update Mail Merge controls
        const sal_uInt16 slotIds[] = { FN_MAILMERGE_FIRST_ENTRY,
                                       FN_MAILMERGE_PREV_ENTRY,
                                       FN_MAILMERGE_NEXT_ENTRY,
                                       FN_MAILMERGE_LAST_ENTRY,
                                       FN_MAILMERGE_CURRENT_ENTRY,
                                       FN_MAILMERGE_EXCLUDE_ENTRY,
                                       FN_MAILMERGE_CREATE_DOCUMENTS,
                                       FN_MAILMERGE_SAVE_DOCUMENTS,
                                       FN_MAILMERGE_PRINT_DOCUMENTS,
                                       FN_MAILMERGE_EMAIL_DOCUMENTS,
                                       0 };
        m_pView->GetViewFrame().GetBindings().Invalidate(slotIds);
    }

    // release/destroy asynchronously
    Application::PostUserEvent( LINK( this, SwMailMergeWizardExecutor, DestroyDialogHdl ) );
}

void SwMailMergeWizardExecutor::ExecuteWizard()
{
    m_pWizard->StartExecuteAsync([this](sal_Int32 nResult){
        EndDialogHdl(nResult);
    });
}

void SwMailMergeWizardExecutor::EndDialogHdl(sal_Int32 nRet)
{
    sal_uInt16 nRestartPage = m_pWizard->GetRestartPage();

    switch ( nRet )
    {
    case RET_LOAD_DOC:
        {
            SwView* pNewView = lcl_LoadDoc(m_pView, m_pWizard->GetReloadDocument());

            // Destroy wizard asynchronously, since we are deep inside the wizard and dialog
            // machinery code here
            m_pWizardToDestroyInCallback = m_pWizard;
            Application::PostUserEvent(
                LINK( this, SwMailMergeWizardExecutor, DestroyWizardHdl ), nullptr );

            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            std::shared_ptr<SwMailMergeConfigItem> xMMConfig = m_pView->GetMailMergeConfigItem();
            if (pNewView)
            {
                pNewView->SetMailMergeConfigItem(xMMConfig);
                m_pView = pNewView;
                xMMConfig->DocumentReloaded();
                //new source view!
                xMMConfig->SetSourceView( m_pView );
                m_pWizard = pFact->CreateMailMergeWizard(*m_pView, xMMConfig);
                m_pWizard->ShowPage( nRestartPage );
            }
            else
            {
                m_pWizard = pFact->CreateMailMergeWizard(*m_pView, xMMConfig);
            }

            // execute the wizard again
            ExecuteWizard();
            break;
        }
    case RET_TARGET_CREATED:
        {
            std::shared_ptr<SwMailMergeConfigItem> xMMConfig = m_pView->GetMailMergeConfigItem();
            SwView* pTargetView = xMMConfig->GetTargetView();
            OSL_ENSURE(pTargetView, "No target view has been created");
            if(pTargetView)
            {
                // destroy wizard asynchronously
                m_pWizardToDestroyInCallback = m_pWizard;
                Application::PostUserEvent(
                    LINK( this, SwMailMergeWizardExecutor, DestroyWizardHdl ), nullptr );

                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                m_pWizard = pFact->CreateMailMergeWizard(*pTargetView, xMMConfig);
                m_pWizard->ShowPage( nRestartPage );

                // execute the wizard again
                ExecuteWizard();
            }
            else
            {
                // should not happen - just in case no target view has been created
                ExecutionFinished();
            }
            break;
        }
    case RET_REMOVE_TARGET:
        {
            std::shared_ptr<SwMailMergeConfigItem> xMMConfig = m_pView->GetMailMergeConfigItem();
            SwView* pTargetView = xMMConfig->GetTargetView();
            SwView* pSourceView = xMMConfig->GetSourceView();
            OSL_ENSURE(pTargetView && pSourceView, "source or target view not available" );
            if(pTargetView && pSourceView)
            {
                m_pView2Close = pTargetView;
                pTargetView->GetViewFrame().GetTopViewFrame()->GetWindow().Hide();
                pSourceView->GetViewFrame().GetFrame().AppearWithUpdate();
                // the current view has be set when the target is destroyed
                m_pView = pSourceView;
                xMMConfig->SetTargetView(nullptr);

                // destroy wizard asynchronously
                m_pWizardToDestroyInCallback = m_pWizard;
                Application::PostUserEvent(
                    LINK( this, SwMailMergeWizardExecutor, CloseFrameHdl ), m_pWizard );

                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                m_pWizard = pFact->CreateMailMergeWizard(*pSourceView, xMMConfig);
                m_pWizard->ShowPage( nRestartPage );

                // execute the wizard again
                ExecuteWizard();
            }
            else
            {
                // should not happen - just in case no target view has been created
                ExecutionFinished();
            }
            break;
        }
    case RET_CANCEL:
        {
            // close frame and destroy wizard asynchronously
            Application::PostUserEvent(
                LINK( this, SwMailMergeWizardExecutor, CancelHdl ), m_pWizard );
            break;
        }
    default: // finish
        {
            std::shared_ptr<SwMailMergeConfigItem> xMMConfig = m_pView->GetMailMergeConfigItem();
            SwView* pSourceView = xMMConfig ? xMMConfig->GetSourceView() : nullptr;
            if(pSourceView)
            {
                xMMConfig->GetSourceView()->GetViewFrame().GetFrame().Appear();
            }
            ExecutionFinished();
            break;
        }

    } // switch
}

IMPL_LINK_NOARG(SwMailMergeWizardExecutor, DestroyDialogHdl, void*, void)
{
    m_pWizard.disposeAndClear();

    release();
}

IMPL_LINK_NOARG(SwMailMergeWizardExecutor, DestroyWizardHdl, void*, void)
{
    m_pWizardToDestroyInCallback.disposeAndClear();
}

IMPL_LINK_NOARG(SwMailMergeWizardExecutor, CancelHdl, void*, void)
{
    std::shared_ptr<SwMailMergeConfigItem> xMMConfig = m_pView->GetMailMergeConfigItem();
    if (xMMConfig)
    {
        if (xMMConfig->GetTargetView())
        {
            xMMConfig->GetTargetView()->GetViewFrame().DoClose();
            xMMConfig->SetTargetView(nullptr);
        }
        if (xMMConfig->GetSourceView())
        {
            auto& rViewFrame(xMMConfig->GetSourceView()->GetViewFrame());
            rViewFrame.GetFrame().AppearWithUpdate();
        }
        xMMConfig->Commit();
    }

    // Revoke created connections
    SwDoc* pDoc = m_pView->GetDocShell()->GetDoc();
    SwDBManager* pDbManager = pDoc->GetDBManager();
    if (pDbManager)
        pDbManager->RevokeLastRegistrations();

    m_pWizard.disposeAndClear();
    release();
}

IMPL_LINK_NOARG(SwMailMergeWizardExecutor, CloseFrameHdl, void*, void)
{
    if ( m_pView2Close )
    {
        m_pView2Close->GetViewFrame().DoClose();
        m_pView2Close = nullptr;
    }
    m_pWizardToDestroyInCallback.disposeAndClear();
}
} // namespace

#endif // HAVE_FEATURE_DBCONNECTIVITY

void SwModule::ExecOther(SfxRequest& rReq)
{
    const SfxItemSet *pArgs = rReq.GetArgs();
    const SfxPoolItem* pItem = nullptr;

    sal_uInt16 nWhich = rReq.GetSlot();
    switch (nWhich)
    {
        case FN_ENVELOP:
            InsertEnv( rReq );
            break;

        case FN_BUSINESS_CARD:
        case FN_LABEL:
            InsertLab(rReq, nWhich == FN_LABEL);
            break;

        case FN_XFORMS_INIT:
            NewXForms( rReq );
            break;

        case SID_ATTR_METRIC:
        if(pArgs && SfxItemState::SET == pArgs->GetItemState(nWhich, false, &pItem))
        {
            FieldUnit eUnit = static_cast<FieldUnit>(static_cast<const SfxUInt16Item*>(pItem)->GetValue());
            switch( eUnit )
            {
                case FieldUnit::MM:
                case FieldUnit::CM:
                case FieldUnit::INCH:
                case FieldUnit::PICA:
                case FieldUnit::POINT:
                {
                    SwView* pActView = ::GetActiveView();
                    bool bWebView = dynamic_cast<SwWebView*>( pActView ) !=  nullptr;
                    ::SetDfltMetric(eUnit, bWebView);
                }
                break;
                default:;//prevent warning
            }
        }
        break;

        case FN_SET_MODOPT_TBLNUMFMT:
            {
                bool bWebView = dynamic_cast<SwWebView*>( ::GetActiveView() )!=  nullptr ,
                     bSet;

                if( pArgs && SfxItemState::SET == pArgs->GetItemState(
                        nWhich, false, &pItem ))
                    bSet = static_cast<const SfxBoolItem*>(pItem)->GetValue();
                else
                    bSet = !m_pModuleConfig->IsInsTableFormatNum( bWebView );

                m_pModuleConfig->SetInsTableFormatNum( bWebView, bSet );
            }
            break;
#if HAVE_FEATURE_DBCONNECTIVITY && !ENABLE_FUZZERS
        case FN_MAILMERGE_WIZARD:
        {
            // show the mailmerge wizard
            rtl::Reference< SwMailMergeWizardExecutor > xEx( new SwMailMergeWizardExecutor );
            xEx->ExecuteMailMergeWizard( pArgs );
        }
        break;
        case FN_MAILMERGE_FIRST_ENTRY:
        case FN_MAILMERGE_PREV_ENTRY:
        case FN_MAILMERGE_NEXT_ENTRY:
        case FN_MAILMERGE_LAST_ENTRY:
        case FN_MAILMERGE_CURRENT_ENTRY:
        {
            SwView* pView = ::GetActiveView();
            if (!pView)
                return;

            const std::shared_ptr<SwMailMergeConfigItem>& xConfigItem = pView->GetMailMergeConfigItem();
            if (!xConfigItem)
                return;

            const bool bHadConnection
                = xConfigItem->GetConnection().is() && !xConfigItem->GetConnection()->isClosed();

            sal_Int32 nPos = xConfigItem->GetResultSetPosition();
            switch (nWhich)
            {
                case FN_MAILMERGE_FIRST_ENTRY: xConfigItem->MoveResultSet(1); break;
                case FN_MAILMERGE_PREV_ENTRY:  xConfigItem->MoveResultSet(nPos - 1); break;
                case FN_MAILMERGE_NEXT_ENTRY:  xConfigItem->MoveResultSet(nPos + 1); break;
                case FN_MAILMERGE_LAST_ENTRY:  xConfigItem->MoveResultSet(-1); break;
                case FN_MAILMERGE_CURRENT_ENTRY: /* don't move the result set, just update the document */ break;
                default: break;
            }

            // now the record has to be merged into the source document
            // TODO can we re-use PerformMailMerge() here somehow?
            const SwDBData& rDBData = xConfigItem->GetCurrentDBData();
            uno::Sequence<uno::Any> vSelection({ uno::Any(xConfigItem->GetResultSetPosition()) });
            svx::ODataAccessDescriptor aDescriptor(::comphelper::InitPropertySequence({
                        {"Selection",        uno::Any(vSelection)},
                        {"DataSourceName",   uno::Any(rDBData.sDataSource)},
                        {"Command",          uno::Any(rDBData.sCommand)},
                        {"CommandType",      uno::Any(rDBData.nCommandType)},
                        {"ActiveConnection", uno::Any(xConfigItem->GetConnection().getTyped())},
                        {"Filter",           uno::Any(xConfigItem->GetFilter())},
                        {"Cursor",           uno::Any(xConfigItem->GetResultSet())}
                        }));

            SwWrtShell& rSh = pView->GetWrtShell();
            SwMergeDescriptor aMergeDesc(DBMGR_MERGE, rSh, aDescriptor);
            rSh.GetDBManager()->Merge(aMergeDesc);

            // update enabled / disabled status of the buttons in the toolbar
            SfxBindings& rBindings = rSh.GetView().GetViewFrame().GetBindings();
            rBindings.Invalidate(FN_MAILMERGE_FIRST_ENTRY);
            rBindings.Invalidate(FN_MAILMERGE_PREV_ENTRY);
            rBindings.Invalidate(FN_MAILMERGE_NEXT_ENTRY);
            rBindings.Invalidate(FN_MAILMERGE_LAST_ENTRY);
            rBindings.Invalidate(FN_MAILMERGE_CURRENT_ENTRY);
            rBindings.Invalidate(FN_MAILMERGE_EXCLUDE_ENTRY);
            if (!bHadConnection && xConfigItem->GetConnection().is()
                && !xConfigItem->GetConnection()->isClosed())
            {
                // The connection has been activated. Update controls that were disabled
                rBindings.Invalidate(FN_MAILMERGE_CREATE_DOCUMENTS);
                rBindings.Invalidate(FN_MAILMERGE_SAVE_DOCUMENTS);
                rBindings.Invalidate(FN_MAILMERGE_PRINT_DOCUMENTS);
                rBindings.Invalidate(FN_MAILMERGE_EMAIL_DOCUMENTS);
            }
            rBindings.Update();
        }
        break;
        case FN_MAILMERGE_CREATE_DOCUMENTS:
        case FN_MAILMERGE_SAVE_DOCUMENTS:
        case FN_MAILMERGE_PRINT_DOCUMENTS:
        case FN_MAILMERGE_EMAIL_DOCUMENTS:
        {
            SwView* pView = ::GetActiveView();
            if (!pView)
                return;

            std::shared_ptr<SwMailMergeConfigItem> xConfigItem = pView->GetMailMergeConfigItem();
            assert(xConfigItem);
            if (!xConfigItem->GetResultSet().is())
            {
                // The connection has been attempted, but failed or no results found,
                // so invalidate the toolbar buttons in case they need to be disabled.
                SfxBindings& rBindings
                    = pView->GetWrtShell().GetView().GetViewFrame().GetBindings();
                rBindings.Invalidate(FN_MAILMERGE_CREATE_DOCUMENTS);
                rBindings.Invalidate(FN_MAILMERGE_SAVE_DOCUMENTS);
                rBindings.Invalidate(FN_MAILMERGE_PRINT_DOCUMENTS);
                rBindings.Invalidate(FN_MAILMERGE_EMAIL_DOCUMENTS);
                rBindings.Invalidate(FN_MAILMERGE_FIRST_ENTRY);
                rBindings.Invalidate(FN_MAILMERGE_PREV_ENTRY);
                rBindings.Invalidate(FN_MAILMERGE_NEXT_ENTRY);
                rBindings.Invalidate(FN_MAILMERGE_LAST_ENTRY);
                rBindings.Update();
                return;
            }

            if (nWhich == FN_MAILMERGE_CREATE_DOCUMENTS)
            {
                xConfigItem = SwDBManager::PerformMailMerge(pView);

                if (xConfigItem && xConfigItem->GetTargetView())
                    xConfigItem->GetTargetView()->GetViewFrame().GetFrame().Appear();
            }
            else
            {
                xConfigItem->SetTargetView(nullptr);
                SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
                if (nWhich == FN_MAILMERGE_SAVE_DOCUMENTS)
                    pFact->ExecuteMMResultSaveDialog(rReq.GetFrameWeld());
                else if (nWhich == FN_MAILMERGE_PRINT_DOCUMENTS)
                    pFact->ExecuteMMResultPrintDialog(rReq.GetFrameWeld());
                else if (nWhich == FN_MAILMERGE_EMAIL_DOCUMENTS)
                    pFact->ExecuteMMResultEmailDialog(rReq.GetFrameWeld());
            }
        }
        break;
#endif
    }
}

// Catch notifications

// Catch hint for DocInfo
void SwModule::Notify( SfxBroadcaster& /*rBC*/, const SfxHint& rHint )
{
    if (rHint.GetId() == SfxHintId::ThisIsAnSfxEventHint)
    {
        const SfxEventHint& rEvHint = static_cast<const SfxEventHint&>(rHint);
        rtl::Reference<SwDocShell> pDocSh = dynamic_cast<SwDocShell*>(rEvHint.GetObjShell().get());
        if( pDocSh )
        {
            SwWrtShell* pWrtSh = pDocSh->GetWrtShell();
            switch (rEvHint.GetEventId())
            {
            case SfxEventHintId::LoadFinished:
                // if it is a new document created from a template,
                // update fixed fields
                if (pDocSh->GetMedium())
                {
                    const SfxBoolItem* pTemplateItem = pDocSh->GetMedium()->GetItemSet().GetItem(SID_TEMPLATE, false);
                    if (pTemplateItem && pTemplateItem->GetValue())
                    {
                        // assume that not calling via SwEditShell::SetFixFields
                        // is allowed, because the shell hasn't been created yet
                        assert(!pWrtSh || pWrtSh->GetView().GetViewFrame().GetFrame().IsClosing_Impl());
                        pDocSh->GetDoc()->getIDocumentFieldsAccess().SetFixFields(nullptr);
                    }
                }
                break;
            case SfxEventHintId::CreateDoc:
                // Update all FIX-Date/Time fields
                if( pWrtSh )
                {
                    const SfxUInt16Item* pUpdateDocItem = pDocSh->GetMedium()->GetItemSet().GetItem(SID_UPDATEDOCMODE, false);
                    bool bUpdateFields = true;
                    if( pUpdateDocItem &&  pUpdateDocItem->GetValue() == document::UpdateDocMode::NO_UPDATE)
                        bUpdateFields = false;
                    if(bUpdateFields)
                    {
                        comphelper::dispatchCommand(u".uno:UpdateInputFields"_ustr, {});

                        // Are database fields contained?
                        // Get all used databases for the first time
                        SwDoc *pDoc = pDocSh->GetDoc();
                        std::vector<OUString> aDBNameList;
                        pDoc->GetAllUsedDB( aDBNameList );
                        if(!aDBNameList.empty())
                        {   // Open database beamer
                            ShowDBObj(pWrtSh->GetView(), pDoc->GetDBData());
                        }
                    }
                }
                break;
            default: break;
            }
        }
    }
    else
    {
        if (rHint.GetId() == SfxHintId::Deinitializing)
        {
            m_pWebUsrPref.reset();
            m_pUsrPref.reset();
            m_pModuleConfig.reset();
            m_pPrintOptions.reset();
            m_pWebPrintOptions.reset();
            m_pChapterNumRules.reset();
            m_pStdFontConfig.reset();
            m_pNavigationConfig.reset();
            m_pToolbarConfig.reset();
            m_pWebToolbarConfig.reset();
            m_pDBConfig.reset();
            if( m_pColorConfig )
            {
                m_pColorConfig->RemoveListener(this);
                m_pColorConfig.reset();
            }
            if( m_pCTLOptions )
            {
                m_pCTLOptions->RemoveListener(this);
                m_pCTLOptions.reset();
            }
            if( m_pUserOptions )
            {
                m_pUserOptions->RemoveListener(this);
                m_pUserOptions.reset();
            }
        }
    }
}

void SwModule::ConfigurationChanged(utl::ConfigurationBroadcaster* pBrdCst, ConfigurationHints eHints)
{
    if( pBrdCst == m_pUserOptions.get() )
    {
        m_bAuthorInitialised = false;
    }
    else if ( pBrdCst == m_pColorConfig.get() )
    {
        //invalidate only the current view in tiled rendering mode, or all views otherwise
        const bool bKit = comphelper::LibreOfficeKit::isActive();
        SfxViewShell* pViewShell = bKit ? SfxViewShell::Current() : SfxViewShell::GetFirst();
        while(pViewShell)
        {
            if(pViewShell->GetWindow())
            {
                auto pSwView = dynamic_cast<SwView *>(pViewShell);
                if (pSwView)
                {
                    SwViewOption aNewOptions = *pSwView->GetWrtShell().GetViewOptions();
                    aNewOptions.SetThemeName(svtools::ColorConfig::GetCurrentSchemeName());
                    SwViewColors aViewColors(*m_pColorConfig);
                    aNewOptions.SetColorConfig(aViewColors);
                    const bool bChanged(aNewOptions != *pSwView->GetWrtShell().GetViewOptions());
                    if (bChanged)
                        pSwView->GetWrtShell().ApplyViewOptions(aNewOptions);
                    else if (bKit)
                    {
                        SwXTextDocument* pModel = comphelper::getFromUnoTunnel<SwXTextDocument>(pViewShell->GetCurrentDocument());
                        SfxLokHelper::notifyViewRenderState(pViewShell, pModel);
                    }

                    if (bKit)
                    {
                        pViewShell->libreOfficeKitViewCallback(LOK_CALLBACK_APPLICATION_BACKGROUND_COLOR,
                            aViewColors.m_aAppBackgroundColor.AsRGBHexString().toUtf8());
                        pViewShell->libreOfficeKitViewCallback(LOK_CALLBACK_DOCUMENT_BACKGROUND_COLOR,
                            aViewColors.m_aDocColor.AsRGBHexString().toUtf8());
                    }

                    if (!bKit)
                    {
                        SwPostItMgr* pPostItManager = pSwView->GetPostItMgr();
                        if (pPostItManager)
                            pPostItManager->UpdateColors();
                    }

                    // if nothing changed, and the hint was OnlyCurrentDocumentColorScheme we can skip invalidate
                    const bool bSkipInvalidate = !bChanged && bKit && eHints == ConfigurationHints::OnlyCurrentDocumentColorScheme;
                    if (!bSkipInvalidate)
                        pViewShell->GetWindow()->Invalidate();
                }
                else if (dynamic_cast< const SwPagePreview *>( pViewShell ) != nullptr ||
                         dynamic_cast< const SwSrcView *>( pViewShell ) !=  nullptr)
                {
                    pViewShell->GetWindow()->Invalidate();
                }
            }
            if (bKit)
                break;
            pViewShell = SfxViewShell::GetNext( *pViewShell );
        }
    }
    else if( pBrdCst == m_pCTLOptions.get() )
    {
        m_eCTLTextNumerals = SvtCTLOptions::GetCTLTextNumerals();
        const SfxObjectShell* pObjSh = SfxObjectShell::GetFirst();
        while( pObjSh )
        {
            if( auto pDocShell = dynamic_cast<const SwDocShell*>(pObjSh) )
            {
                SwDoc* pDoc = const_cast<SwDocShell*>(pDocShell)->GetDoc();
                SwViewShell* pVSh = pDoc->getIDocumentLayoutAccess().GetCurrentViewShell();
                if ( pVSh )
                    pVSh->ChgNumberDigits();
            }
            pObjSh = SfxObjectShell::GetNext(*pObjSh);
        }
    }
}

SwDBConfig* SwModule::GetDBConfig()
{
    if(!m_pDBConfig)
        m_pDBConfig.reset(new SwDBConfig);
    return m_pDBConfig.get();
}

svtools::ColorConfig& SwModule::GetColorConfig()
{
    if(!m_pColorConfig)
    {
        m_pColorConfig.reset(new svtools::ColorConfig);
        SwViewOption::SetInitialColorConfig(*m_pColorConfig);
        m_pColorConfig->AddListener(this);
    }
    return *m_pColorConfig;
}

SvtUserOptions& SwModule::GetUserOptions()
{
    if(!m_pUserOptions)
    {
        m_pUserOptions.reset(new SvtUserOptions);
        m_pUserOptions->AddListener(this);
    }
    return *m_pUserOptions;
}

const SwMasterUsrPref *SwModule::GetUsrPref(bool bWeb) const
{
    SwModule* pNonConstModule = const_cast<SwModule*>(this);
    if(bWeb && !m_pWebUsrPref)
    {
        // The SpellChecker is needed in SwMasterUsrPref's Load, but it must not
        // be created there #58256#
        pNonConstModule->m_pWebUsrPref.reset(new SwMasterUsrPref(true));
    }
    else if(!bWeb && !m_pUsrPref)
    {
        pNonConstModule->m_pUsrPref.reset(new SwMasterUsrPref(false));
    }
    return  bWeb ? m_pWebUsrPref.get() : m_pUsrPref.get();
}

void NewXForms( SfxRequest& rReq )
{
    // copied & excerpted from SwModule::InsertLab(..)

    // create new document
    SwDocShellRef xDocSh( new SwDocShell( SfxObjectCreateMode::STANDARD) );
    xDocSh->DoInitNew();

    // initialize XForms
    xDocSh->GetDoc()->initXForms(true);

    // load document into frame
    SfxViewFrame::DisplayNewDocument( *xDocSh, rReq );

    // set return value
    rReq.SetReturnValue( SfxVoidItem( rReq.GetSlot() ) );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
