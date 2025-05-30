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

#include <fuprlout.hxx>
#include <sfx2/dispatch.hxx>

#include <sfx2/viewfrm.hxx>
#include <sfx2/request.hxx>
#include <svl/stritem.hxx>

#include <sdattr.hrc>
#include <drawdoc.hxx>
#include <sdpage.hxx>
#include <pres.hxx>
#include <DrawViewShell.hxx>
#include <View.hxx>
#include <glob.hxx>
#include <app.hrc>
#include <DrawDocShell.hxx>
#include <SlideSorterViewShell.hxx>
#include <Window.hxx>
#include <drawview.hxx>
#include <sdabstdlg.hxx>
#include <memory>

namespace sd
{


#define DOCUMENT_TOKEN '#'

FuPresentationLayout::FuPresentationLayout (
    ViewShell& rViewSh,
    ::sd::Window* pWin,
    ::sd::View* pView,
    SdDrawDocument& rDoc,
    SfxRequest& rReq)
    : FuPoor(rViewSh, pWin, pView, rDoc, rReq)
{
}

rtl::Reference<FuPoor> FuPresentationLayout::Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq )
{
    rtl::Reference<FuPoor> xFunc( new FuPresentationLayout( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    return xFunc;
}

void FuPresentationLayout::DoExecute( SfxRequest& rReq )
{
    // prevent selected objects or objects which are under editing from disappearing
    mpView->SdrEndTextEdit();

    if(mpView->GetSdrPageView())
    {
        mpView->UnmarkAll();
    }

    bool bError = false;

    /* if we are on a master page, the changes apply for all pages and notes-
       pages who are using the relevant layout */
    bool bOnMaster = false;
    if (DrawViewShell *pShell = dynamic_cast<DrawViewShell*>(&mrViewShell))
    {
        EditMode eEditMode = pShell->GetEditMode();
        if (eEditMode == EditMode::MasterPage)
            bOnMaster = true;
    }

    std::vector<SdPage*> aUnselect;
    if (!bOnMaster)
    {
        //We later rely on IsSelected, so transfer the selection here
        //into the document
        slidesorter::SlideSorterViewShell* pSlideSorterViewShell
            = slidesorter::SlideSorterViewShell::GetSlideSorter(mrViewShell.GetViewShellBase());
        if (pSlideSorterViewShell)
        {
            std::shared_ptr<slidesorter::SlideSorterViewShell::PageSelection> xSelection(
                pSlideSorterViewShell->GetPageSelection());
            if (xSelection)
            {
                for (SdPage *pPage : *xSelection)
                {
                    if (pPage->IsSelected() || pPage->GetPageKind() != PageKind::Standard)
                        continue;
                    mrDoc.SetSelected(pPage, true);
                    aUnselect.push_back(pPage);
                }
            }
        }
    }

    std::vector<SdPage*> aSelectedPages;
    std::vector<sal_uInt16> aSelectedPageNums;
    // determine the active pages
    for (sal_uInt16 nPage = 0; nPage < mrDoc.GetSdPageCount(PageKind::Standard); nPage++)
    {
        SdPage* pPage = mrDoc.GetSdPage(nPage, PageKind::Standard);
        if (pPage->IsSelected())
        {
            aSelectedPages.push_back(pPage);
            aSelectedPageNums.push_back(nPage);
        }
    }

    bool bMasterPage = bOnMaster;
    bool bCheckMasters = false;

    // call dialog
    bool   bLoad = false;           // appear the new master pages?
    OUString aFile;

    SfxItemSetFixed<ATTR_PRESLAYOUT_START, ATTR_PRESLAYOUT_END> aSet(mrDoc.GetPool() );

    aSet.Put( SfxBoolItem( ATTR_PRESLAYOUT_LOAD, bLoad));
    aSet.Put( SfxBoolItem( ATTR_PRESLAYOUT_MASTER_PAGE, bMasterPage ) );
    aSet.Put( SfxBoolItem( ATTR_PRESLAYOUT_CHECK_MASTERS, bCheckMasters ) );

    if (!aSelectedPages.empty())
    {
        OUString aOldLayoutName(aSelectedPages.back()->GetLayoutName());
        sal_Int32 nPos = aOldLayoutName.indexOf(SD_LT_SEPARATOR);
        if (nPos != -1)
            aOldLayoutName = aOldLayoutName.copy(0, nPos);
        aSet.Put(SfxStringItem(ATTR_PRESLAYOUT_NAME, aOldLayoutName));
    }

    const SfxItemSet *pArgs = rReq.GetArgs ();

    if (pArgs)
    {
        if (pArgs->GetItemState(ATTR_PRESLAYOUT_LOAD) == SfxItemState::SET)
            bLoad = static_cast<const SfxBoolItem&>(pArgs->Get(ATTR_PRESLAYOUT_LOAD)).GetValue();
        if( pArgs->GetItemState( ATTR_PRESLAYOUT_MASTER_PAGE ) == SfxItemState::SET )
            bMasterPage = pArgs->Get( ATTR_PRESLAYOUT_MASTER_PAGE ).GetValue();
        if( pArgs->GetItemState( ATTR_PRESLAYOUT_CHECK_MASTERS ) == SfxItemState::SET )
            bCheckMasters = static_cast<const SfxBoolItem&>( pArgs->Get( ATTR_PRESLAYOUT_CHECK_MASTERS ) ).GetValue();
        if (pArgs->GetItemState(ATTR_PRESLAYOUT_NAME) == SfxItemState::SET)
            aFile = pArgs->Get(ATTR_PRESLAYOUT_NAME).GetValue();
    }
    else
    {
        SdAbstractDialogFactory* pFact = SdAbstractDialogFactory::Create();
        ScopedVclPtr<AbstractSdPresLayoutDlg> pDlg(pFact->CreateSdPresLayoutDlg(mpWindow ? mpWindow->GetFrameWeld() : nullptr, mpDocSh, aSet));

        sal_uInt16 nResult = pDlg->Execute();

        switch (nResult)
        {
            case RET_OK:
            {
                pDlg->GetAttr(aSet);
                if (aSet.GetItemState(ATTR_PRESLAYOUT_LOAD) == SfxItemState::SET)
                    bLoad = static_cast<const SfxBoolItem&>(aSet.Get(ATTR_PRESLAYOUT_LOAD)).GetValue();
                if( aSet.GetItemState( ATTR_PRESLAYOUT_MASTER_PAGE ) == SfxItemState::SET )
                    bMasterPage = aSet.Get( ATTR_PRESLAYOUT_MASTER_PAGE ).GetValue();
                if( aSet.GetItemState( ATTR_PRESLAYOUT_CHECK_MASTERS ) == SfxItemState::SET )
                    bCheckMasters = static_cast<const SfxBoolItem&>(aSet.Get( ATTR_PRESLAYOUT_CHECK_MASTERS ) ).GetValue();
                if (aSet.GetItemState(ATTR_PRESLAYOUT_NAME) == SfxItemState::SET)
                    aFile = aSet.Get(ATTR_PRESLAYOUT_NAME).GetValue();
            }
            break;

            default:
                bError = true;
        }
    }

    if (bError)
        return;

    mpDocSh->SetWaitCursor( true );

    /* Here, we only exchange masterpages, therefore the current page
       remains the current page. To prevent calling PageOrderChangedHint
       during insertion and extraction of the masterpages, we block. */
    /* That isn't quite right. If the masterpageview is active and you are
       removing a masterpage, it's possible that you are removing the
       current masterpage. So you have to call ResetActualPage ! */
    if( dynamic_cast< const DrawViewShell *>( &mrViewShell ) !=  nullptr && !bCheckMasters )
        static_cast<DrawView*>(mpView)->BlockPageOrderChangedHint(true);

    if (bLoad)
    {
        sal_Int32 nIdx{ 0 };
        OUString aFileName = aFile.getToken(0, DOCUMENT_TOKEN, nIdx);
        SdDrawDocument* pTempDoc = mrDoc.OpenBookmarkDoc( aFileName );

        // #69581: If I chose the standard-template I got no filename and so I get no
        //         SdDrawDocument-Pointer. But the method SetMasterPage is able to handle
        //         a NULL-pointer as a Standard-template ( look at SdDrawDocument::SetMasterPage )
        OUString aLayoutName;
        if( pTempDoc )
            aLayoutName = aFile.getToken(0, DOCUMENT_TOKEN, nIdx);
        for (auto nSelectedPage : aSelectedPageNums)
            mrDoc.SetMasterPage(nSelectedPage, aLayoutName, pTempDoc, bMasterPage, bCheckMasters);
        mrDoc.CloseBookmarkDoc();
    }
    else
    {
        // use master page with the layout name aFile from current Doc
        for (auto nSelectedPage : aSelectedPageNums)
            mrDoc.SetMasterPage(nSelectedPage, aFile, &mrDoc, bMasterPage, bCheckMasters);
    }

    // remove blocking
    if( dynamic_cast< const DrawViewShell *>( &mrViewShell ) !=  nullptr && !bCheckMasters )
        static_cast<DrawView*>(mpView)->BlockPageOrderChangedHint(false);

    // if the master page was visible, show it again
    if (!aSelectedPages.empty())
    {
        if (bOnMaster)
        {
            if( auto pDrawViewShell = dynamic_cast<DrawViewShell *>( &mrViewShell ))
            {
                ::sd::View* pView = pDrawViewShell->GetView();
                for (auto pSelectedPage : aSelectedPages)
                {
                    sal_uInt16 nPgNum = pSelectedPage->TRG_GetMasterPage().GetPageNum();

                    if (static_cast<DrawViewShell*>(&mrViewShell)->GetPageKind() == PageKind::Notes)
                        nPgNum++;

                    pView->HideSdrPage();
                    pView->ShowSdrPage(pView->GetModel().GetMasterPage(nPgNum));
                }
            }

            // force update of TabBar
            mrViewShell.GetViewFrame()->GetDispatcher()->Execute(SID_MASTERPAGE, SfxCallMode::ASYNCHRON | SfxCallMode::RECORD);
        }
        else
        {
            for (auto pSelectedPage : aSelectedPages)
                pSelectedPage->SetAutoLayout(pSelectedPage->GetAutoLayout());
        }
    }

    //Undo transfer to document selection
    for (auto pPage : aUnselect)
        mrDoc.SetSelected(pPage, false);


    // fake a mode change to repaint the page tab bar
    if( auto pDrawViewSh = dynamic_cast<DrawViewShell *>( &mrViewShell ) )
    {
        EditMode eMode = pDrawViewSh->GetEditMode();
        bool bLayer = pDrawViewSh->IsLayerModeActive();
        pDrawViewSh->ChangeEditMode( eMode, !bLayer );
        pDrawViewSh->ChangeEditMode( eMode, bLayer );
    }

    mpDocSh->SetWaitCursor( false );
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
