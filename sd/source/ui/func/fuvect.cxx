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

#include <fuvect.hxx>
#include <svx/svdograf.hxx>

#include <View.hxx>
#include <Window.hxx>
#include <strings.hrc>
#include <sdresid.hxx>
#include <sdabstdlg.hxx>

namespace sd
{


FuVectorize::FuVectorize (
    ViewShell& rViewSh,
    ::sd::Window* pWin,
    ::sd::View* pView,
    SdDrawDocument& rDoc,
    SfxRequest& rReq)
    : FuPoor (rViewSh, pWin, pView, rDoc, rReq)
{
}

rtl::Reference<FuPoor> FuVectorize::Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq )
{
    rtl::Reference<FuPoor> xFunc( new FuVectorize( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    return xFunc;
}

void FuVectorize::DoExecute( SfxRequest& )
{
    const SdrMarkList& rMarkList = mpView->GetMarkedObjectList();

    if( rMarkList.GetMarkCount() != 1 )
        return;

    SdrObject* pObj = rMarkList.GetMark( 0 )->GetMarkedSdrObj();

    auto pSdrGrafObj = dynamic_cast< const SdrGrafObj *>( pObj );
    if( !pSdrGrafObj )
        return;

    SdAbstractDialogFactory* pFact = SdAbstractDialogFactory::Create();
    ScopedVclPtr<AbstractSdVectorizeDlg> pDlg(
            pFact->CreateSdVectorizeDlg(mpWindow ? mpWindow->GetFrameWeld() : nullptr,
                                        pSdrGrafObj->GetGraphic().GetBitmapEx().GetBitmap(), mpDocSh ) );
    if( pDlg->Execute() != RET_OK )
        return;

    const GDIMetaFile&  rMtf = pDlg->GetGDIMetaFile();
    SdrPageView*        pPageView = mpView->GetSdrPageView();

    if( pPageView && rMtf.GetActionSize() )
    {
        rtl::Reference<SdrGrafObj> pVectObj = SdrObject::Clone(static_cast<SdrGrafObj&>(*pObj), pObj->getSdrModelFromSdrObject());
        OUString aStr = rMarkList.GetMarkDescription() +
            " " + SdResId( STR_UNDO_VECTORIZE );
        mpView->BegUndo( aStr );
        pVectObj->SetGraphic( rMtf );
        mpView->ReplaceObjectAtView( pObj, *pPageView, pVectObj.get() );
        mpView->EndUndo();
    }
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
