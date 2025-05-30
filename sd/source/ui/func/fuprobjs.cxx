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

#include <fuprobjs.hxx>

#include <svl/stritem.hxx>
#include <svl/style.hxx>
#include <editeng/outliner.hxx>
#include <svl/hint.hxx>
#include <tools/debug.hxx>

#include <app.hrc>

#include <strings.hxx>

#include <drawdoc.hxx>
#include <sfx2/sfxdlg.hxx>
#include <DrawDocShell.hxx>
#include <OutlineView.hxx>
#include <OutlineViewShell.hxx>
#include <ViewShell.hxx>
#include <Window.hxx>
#include <glob.hxx>
#include <prlayout.hxx>
#include <unchss.hxx>
#include <sdabstdlg.hxx>
#include <memory>

namespace sd {


FuPresentationObjects::FuPresentationObjects (
    ViewShell& rViewSh,
    ::sd::Window* pWin,
    ::sd::View* pView,
    SdDrawDocument& rDoc,
    SfxRequest& rReq)
     : FuPoor(rViewSh, pWin, pView, rDoc, rReq)
{
}

rtl::Reference<FuPoor> FuPresentationObjects::Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq )
{
    rtl::Reference<FuPoor> xFunc( new FuPresentationObjects( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    return xFunc;
}

void FuPresentationObjects::DoExecute( SfxRequest& )
{
    OutlineViewShell* pOutlineViewShell = dynamic_cast< OutlineViewShell* >( &mrViewShell );
    DBG_ASSERT( pOutlineViewShell, "sd::FuPresentationObjects::DoExecute(), does not work without an OutlineViewShell!");
    if( !pOutlineViewShell )
        return;

    /* does the selections end in a unique presentation layout?
       if not, it is not allowed to edit the templates */
    SfxItemSetFixed<SID_STATUS_LAYOUT, SID_STATUS_LAYOUT> aSet(mrDoc.GetItemPool() );
    pOutlineViewShell->GetStatusBarState( aSet );
    OUString aLayoutName = aSet.Get(SID_STATUS_LAYOUT).GetValue();
    DBG_ASSERT(!aLayoutName.isEmpty(), "Layout not defined");

    bool    bUnique = false;
    sal_Int16   nDepth, nTmp;
    OutlineView* pOlView = static_cast<OutlineView*>(pOutlineViewShell->GetView());
    OutlinerView* pOutlinerView = pOlView->GetViewByWindow( static_cast<Window*>(mpWindow) );
    ::Outliner& rOutl = pOutlinerView->GetOutliner();

    std::vector<Paragraph*> aSelList;
    pOutlinerView->CreateSelectionList(aSelList);

    Paragraph* pPara = aSelList.empty() ? nullptr : aSelList.front();

    nDepth = rOutl.GetDepth(rOutl.GetAbsPos( pPara ) );
    bool bPage = ::Outliner::HasParaFlag( pPara, ParaFlag::ISPAGE );

    for( const auto& rpPara : aSelList )
    {
        nTmp = rOutl.GetDepth( rOutl.GetAbsPos( rpPara ) );

        if( nDepth != nTmp )
        {
            bUnique = false;
            break;
        }

        if( ::Outliner::HasParaFlag( rpPara, ParaFlag::ISPAGE ) != bPage )
        {
            bUnique = false;
            break;
        }
        bUnique = true;
    }

    if( !bUnique )
        return;

    OUString aStyleName = aLayoutName + SD_LT_SEPARATOR;
    PresentationObjects ePO;

    if( bPage )
    {
        ePO = PresentationObjects::Title;
        aStyleName += STR_LAYOUT_TITLE;
    }
    else
    {
        ePO = static_cast<PresentationObjects>( static_cast<int>(PresentationObjects::Outline_1) + nDepth - 1 );
        aStyleName += STR_LAYOUT_OUTLINE + " " + OUString::number(nDepth);
    }

    SfxStyleSheetBasePool* pStyleSheetPool = mpDocSh->GetStyleSheetPool();
    SfxStyleSheetBase* pStyleSheet = pStyleSheetPool->Find( aStyleName, SfxStyleFamily::Page );
    DBG_ASSERT(pStyleSheet, "StyleSheet missing");

    if( !pStyleSheet )
        return;

    SfxStyleSheetBase& rStyleSheet = *pStyleSheet;

    SdAbstractDialogFactory* pFact = SdAbstractDialogFactory::Create();
    ScopedVclPtr<SfxAbstractTabDialog> pDlg(pFact->CreateSdPresLayoutTemplateDlg(mpDocSh, mrViewShell.GetFrameWeld(),
                                                        false, rStyleSheet, ePO, pStyleSheetPool));
    if( pDlg->Execute() == RET_OK )
    {
        const SfxItemSet* pOutSet = pDlg->GetOutputItemSet();
        // Undo-Action
        mpDocSh->GetUndoManager()->AddUndoAction(
            std::make_unique<StyleSheetUndoAction>(mrDoc, static_cast<SfxStyleSheet&>(rStyleSheet), pOutSet));

        rStyleSheet.GetItemSet().Put( *pOutSet );
        static_cast<SfxStyleSheet&>( rStyleSheet ).Broadcast( SfxHint( SfxHintId::DataChanged ) );
    }
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
