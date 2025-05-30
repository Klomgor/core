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

#include <fusldlg.hxx>
#include <svl/eitem.hxx>
#include <svl/itemset.hxx>
#include <svl/stritem.hxx>
#include <svl/intitem.hxx>

#include <drawdoc.hxx>
#include <sdpage.hxx>
#include <sdresid.hxx>
#include <strings.hrc>
#include <sdattr.hrc>
#include <sdmod.hxx>
#include <Window.hxx>
#include <optsitem.hxx>
#include <sdabstdlg.hxx>
#include <slideshow.hxx>
#include <ViewShell.hxx>

using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::presentation;

namespace sd {

#define ITEMVALUE(ItemSet,Id,Cast) static_cast<const Cast&>((ItemSet).Get(Id)).GetValue()


FuSlideShowDlg::FuSlideShowDlg (
    ViewShell& rViewSh,
    ::sd::Window* pWin,
    ::sd::View* pView,
    SdDrawDocument& rDoc,
    SfxRequest& rReq)
    : FuPoor( rViewSh, pWin, pView, rDoc, rReq )
{
}

rtl::Reference<FuPoor> FuSlideShowDlg::Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq )
{
    rtl::Reference<FuPoor> xFunc( new FuSlideShowDlg( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    return xFunc;
}

void FuSlideShowDlg::DoExecute( SfxRequest& )
{
    PresentationSettings& rPresentationSettings = mrDoc.getPresentationSettings();

    SfxItemSetFixed<ATTR_PRESENT_START, ATTR_PRESENT_END> aDlgSet( mrDoc.GetPool() );
    std::vector<OUString> aPageNameList(mrDoc.GetSdPageCount( PageKind::Standard ));
    const OUString& rPresPage = rPresentationSettings.maPresPage;
    OUString        aFirstPage;
    SdPage*         pPage = nullptr;
    ::tools::Long            nPage;

    for( nPage = mrDoc.GetSdPageCount( PageKind::Standard ) - 1; nPage >= 0; nPage-- )
    {
        pPage = mrDoc.GetSdPage( static_cast<sal_uInt16>(nPage), PageKind::Standard );
        OUString aStr( pPage->GetName() );

        if ( aStr.isEmpty() )
        {
            aStr = SdResId( STR_PAGE ) + OUString::number( nPage + 1 );
        }

        aPageNameList[ nPage ] = aStr;

        // is this our (existing) first page?
        if ( rPresPage == aStr )
            aFirstPage = rPresPage;
        else if ( pPage->IsSelected() && aFirstPage.isEmpty() )
            aFirstPage = aStr;
    }
    SdCustomShowList* pCustomShowList = mrDoc.GetCustomShowList(); // No Create

    if( aFirstPage.isEmpty() && pPage )
        aFirstPage = pPage->GetName();

    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_ALL, rPresentationSettings.mbAll ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_CUSTOMSHOW, rPresentationSettings.mbCustomShow ) );
    aDlgSet.Put( SfxStringItem( ATTR_PRESENT_DIANAME, aFirstPage ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_ENDLESS, rPresentationSettings.mbEndless ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_MANUEL, rPresentationSettings.mbManual ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_MOUSE, rPresentationSettings.mbMouseVisible ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_PEN, rPresentationSettings.mbMouseAsPen ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_ANIMATION_ALLOWED, rPresentationSettings.mbAnimationAllowed ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_CHANGE_PAGE, !rPresentationSettings.mbLockedPages ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_ALWAYS_ON_TOP, rPresentationSettings.mbAlwaysOnTop ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_FULLSCREEN, rPresentationSettings.mbFullScreen ) );
    aDlgSet.Put( SfxUInt32Item( ATTR_PRESENT_PAUSE_TIMEOUT, rPresentationSettings.mnPauseTimeout ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_SHOW_PAUSELOGO, rPresentationSettings.mbShowPauseLogo ) );
    aDlgSet.Put( SfxBoolItem( ATTR_PRESENT_INTERACTIVE, rPresentationSettings.mbInteractive ) );

    SdOptions* pOptions = SdModule::get()->GetSdOptions(DocumentType::Impress);
    aDlgSet.Put( SfxInt32Item( ATTR_PRESENT_DISPLAY, pOptions->GetDisplay() ) );

    SdAbstractDialogFactory* pFact = SdAbstractDialogFactory::Create();
    ScopedVclPtr<AbstractSdStartPresDlg> pDlg( pFact->CreateSdStartPresentationDlg(mpWindow ? mpWindow->GetFrameWeld() : nullptr, aDlgSet, aPageNameList, pCustomShowList) );
    if( pDlg->Execute() != RET_OK )
        return;

    ::tools::Long    nValue32;
    bool    bValue;
    bool    bValuesChanged = false;

    pDlg->GetAttr( aDlgSet );

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_ALL, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbAll )
    {
        bValuesChanged = true;
        rPresentationSettings.mbAll = bValue;
        // remove any previous existing slide
        rPresentationSettings.maPresPage.clear();
    }

    if (!rPresentationSettings.mbAll)
    {
        OUString aPage = aDlgSet.Get(ATTR_PRESENT_DIANAME).GetValue();
        if( aPage != rPresentationSettings.maPresPage )
        {
            bValuesChanged = true;
            rPresentationSettings.maPresPage = aPage;
        }
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_CUSTOMSHOW, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbCustomShow )
    {
        bValuesChanged = true;
        rPresentationSettings.mbCustomShow = bValue;
        rPresentationSettings.mbStartCustomShow = false;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_ENDLESS, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbEndless )
    {
        bValuesChanged = true;
        rPresentationSettings.mbEndless = bValue;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_MANUEL, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbManual )
    {
        bValuesChanged = true;
        rPresentationSettings.mbManual = bValue;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_MOUSE, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbMouseVisible )
    {
        bValuesChanged = true;
        rPresentationSettings.mbMouseVisible = bValue;
    }

    bValue = !ITEMVALUE( aDlgSet, ATTR_PRESENT_CHANGE_PAGE, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbLockedPages )
    {
        bValuesChanged = true;
        rPresentationSettings.mbLockedPages = bValue;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_ANIMATION_ALLOWED, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbAnimationAllowed )
    {
        bValuesChanged = true;
        rPresentationSettings.mbAnimationAllowed = bValue;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_ALWAYS_ON_TOP, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbAlwaysOnTop )
    {
        bValuesChanged = true;
        rPresentationSettings.mbAlwaysOnTop = bValue;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_FULLSCREEN, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbFullScreen )
    {
        bValuesChanged = true;
        rPresentationSettings.mbFullScreen = bValue;
    }

    nValue32 = aDlgSet.Get(ATTR_PRESENT_PAUSE_TIMEOUT).GetValue();
    if( nValue32 != rPresentationSettings.mnPauseTimeout )
    {
        bValuesChanged = true;
        rPresentationSettings.mnPauseTimeout = nValue32;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_SHOW_PAUSELOGO, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbShowPauseLogo )
    {
        bValuesChanged = true;
        rPresentationSettings.mbShowPauseLogo = bValue;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_INTERACTIVE, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbInteractive )
    {
        bValuesChanged = true;
        rPresentationSettings.mbInteractive = bValue;
    }

    bValue = ITEMVALUE( aDlgSet, ATTR_PRESENT_PEN, SfxBoolItem );
    if ( bValue != rPresentationSettings.mbMouseAsPen )
    {
        bValuesChanged = true;
        rPresentationSettings.mbMouseAsPen = bValue;

        // live slideshow? pass pen state on immediately
        Reference< XSlideShowController > xSlideShowController(
            SlideShow::GetSlideShowController(mrViewShell.GetViewShellBase() ) );
        if( xSlideShowController.is() )
            if(rPresentationSettings.mbInteractive)
                xSlideShowController->setUsePen( bValue );
    }

    pOptions->SetDisplay( aDlgSet.Get(ATTR_PRESENT_DISPLAY).GetValue() );

    // is something has changed, we set the modified flag
    if ( bValuesChanged )
        mrDoc.SetChanged();
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
