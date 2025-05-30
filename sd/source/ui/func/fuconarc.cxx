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

#include <fuconarc.hxx>
#include <svx/svdpagv.hxx>
#include <svx/svdocirc.hxx>
#include <sfx2/request.hxx>
#include <svl/intitem.hxx>
#include <sfx2/dispatch.hxx>
#include <svx/svdobj.hxx>
#include <sfx2/viewfrm.hxx>
#include <osl/diagnose.h>

#include <svx/svxids.hrc>

#include <Window.hxx>
#include <drawdoc.hxx>

#include <View.hxx>
#include <ViewShell.hxx>
#include <ViewShellBase.hxx>
#include <ToolBarManager.hxx>

#include <svx/sxciaitm.hxx>
#include <svx/xfillit0.hxx>

using namespace com::sun::star;

namespace sd {


FuConstructArc::FuConstructArc (
    ViewShell&  rViewSh,
    ::sd::Window*       pWin,
    ::sd::View*         pView,
    SdDrawDocument& rDoc,
    SfxRequest&     rReq )
    : FuConstruct( rViewSh, pWin, pView, rDoc, rReq )
{
}

rtl::Reference<FuPoor> FuConstructArc::Create( ViewShell& rViewSh, ::sd::Window* pWin, ::sd::View* pView, SdDrawDocument& rDoc, SfxRequest& rReq, bool bPermanent  )
{
    FuConstructArc* pFunc;
    rtl::Reference<FuPoor> xFunc( pFunc = new FuConstructArc( rViewSh, pWin, pView, rDoc, rReq ) );
    xFunc->DoExecute(rReq);
    pFunc->SetPermanent(bPermanent);
    return xFunc;
}

void FuConstructArc::DoExecute( SfxRequest& rReq )
{
    FuConstruct::DoExecute( rReq );

    mrViewShell.GetViewShellBase().GetToolBarManager()->SetToolBar(
        ToolBarManager::ToolBarGroup::Function,
        ToolBarManager::msDrawingObjectToolBar);

    const SfxItemSet *pArgs = rReq.GetArgs ();

    if (!pArgs)
        return;

    const SfxUInt32Item* pCenterX = rReq.GetArg<SfxUInt32Item>(ID_VAL_CENTER_X);
    const SfxUInt32Item* pCenterY = rReq.GetArg<SfxUInt32Item>(ID_VAL_CENTER_Y);
    const SfxUInt32Item* pAxisX = rReq.GetArg<SfxUInt32Item>(ID_VAL_AXIS_X);
    const SfxUInt32Item* pAxisY = rReq.GetArg<SfxUInt32Item>(ID_VAL_AXIS_Y);
    const SfxUInt32Item* pPhiStart = rReq.GetArg<SfxUInt32Item>(ID_VAL_ANGLESTART);
    const SfxUInt32Item* pPhiEnd = rReq.GetArg<SfxUInt32Item>(ID_VAL_ANGLEEND);

    ::tools::Rectangle   aNewRectangle (pCenterX->GetValue () - pAxisX->GetValue () / 2,
                               pCenterY->GetValue () - pAxisY->GetValue () / 2,
                               pCenterX->GetValue () + pAxisX->GetValue () / 2,
                               pCenterY->GetValue () + pAxisY->GetValue () / 2);

    Activate();  // sets aObjKind
    rtl::Reference<SdrCircObj> pNewCircle =
        new SdrCircObj(
            mpView->getSdrModelFromSdrView(),
            ToSdrCircKind(mpView->GetCurrentObjIdentifier()),
            aNewRectangle,
            Degree100(pPhiStart->GetValue() * 10),
            Degree100(pPhiEnd->GetValue() * 10));
    SdrPageView *pPV = mpView->GetSdrPageView();

    mpView->InsertObjectAtView(pNewCircle.get(), *pPV, SdrInsertFlags::SETDEFLAYER);
}

bool FuConstructArc::MouseButtonDown( const MouseEvent& rMEvt )
{
    bool bReturn = FuConstruct::MouseButtonDown( rMEvt );

    if ( rMEvt.IsLeft() && !mpView->IsAction() )
    {
        Point aPnt( mpWindow->PixelToLogic( rMEvt.GetPosPixel() ) );
        mpWindow->CaptureMouse();
        sal_uInt16 nDrgLog = sal_uInt16 ( mpWindow->PixelToLogic(Size(mpView->GetDragThresholdPixels(),0)).Width() );
        mpView->BegCreateObj(aPnt, nullptr, nDrgLog);

        SdrObject* pObj = mpView->GetCreateObj();

        if (pObj)
        {
            SfxItemSet aAttr(mrDoc.GetPool());
            SetStyleSheet(aAttr, pObj);

            pObj->SetMergedItemSet(aAttr);
        }

        bReturn = true;
    }
    return bReturn;
}

bool FuConstructArc::MouseButtonUp( const MouseEvent& rMEvt )
{
    if (rMEvt.IsLeft() && IsIgnoreUnexpectedMouseButtonUp())
        return false;

    bool bReturn = false;
    bool bCreated = false;

    if ( mpView->IsCreateObj() && rMEvt.IsLeft() )
    {
        const size_t nCount = mpView->GetSdrPageView()->GetObjList()->GetObjCount();

        if (mpView->EndCreateObj(SdrCreateCmd::NextPoint) )
        {
            if (nCount != mpView->GetSdrPageView()->GetObjList()->GetObjCount())
            {
                bCreated = true;
            }
        }

        bReturn = true;
    }

    bReturn = FuConstruct::MouseButtonUp (rMEvt) || bReturn;

    if (!bPermanent && bCreated)
        mrViewShell.GetViewFrame()->GetDispatcher()->Execute(SID_OBJECT_SELECT, SfxCallMode::ASYNCHRON);

    return bReturn;
}

void FuConstructArc::Activate()
{
    SdrObjKind aObjKind;

    switch( nSlotId )
    {
        case SID_DRAW_ARC      :
        case SID_DRAW_CIRCLEARC:
        {
            aObjKind = SdrObjKind::CircleArc;
        }
        break;

        case SID_DRAW_PIE             :
        case SID_DRAW_PIE_NOFILL      :
        case SID_DRAW_CIRCLEPIE       :
        case SID_DRAW_CIRCLEPIE_NOFILL:
        {
            aObjKind = SdrObjKind::CircleSection;
        }
        break;

        case SID_DRAW_ELLIPSECUT       :
        case SID_DRAW_ELLIPSECUT_NOFILL:
        case SID_DRAW_CIRCLECUT        :
        case SID_DRAW_CIRCLECUT_NOFILL :
        {
            aObjKind = SdrObjKind::CircleCut;
        }
        break;

        default:
        {
            aObjKind = SdrObjKind::CircleArc;
        }
        break;
    }

    mpView->SetCurrentObj(aObjKind);

    FuConstruct::Activate();
}

rtl::Reference<SdrObject> FuConstructArc::CreateDefaultObject(const sal_uInt16 nID, const ::tools::Rectangle& rRectangle)
{

    rtl::Reference<SdrObject> pObj(SdrObjFactory::MakeNewObject(
        mpView->getSdrModelFromSdrView(),
        mpView->GetCurrentObjInventor(),
        mpView->GetCurrentObjIdentifier()));

    if(pObj)
    {
        if( dynamic_cast< const SdrCircObj *>( pObj.get() ) !=  nullptr)
        {
            ::tools::Rectangle aRect(rRectangle);

            if(SID_DRAW_ARC == nID ||
                SID_DRAW_CIRCLEARC == nID ||
                SID_DRAW_CIRCLEPIE == nID ||
                SID_DRAW_CIRCLEPIE_NOFILL == nID ||
                SID_DRAW_CIRCLECUT == nID ||
                SID_DRAW_CIRCLECUT_NOFILL == nID)
            {
                // force quadratic
                ImpForceQuadratic(aRect);
            }

            pObj->SetLogicRect(aRect);

            SfxItemSet aAttr(mrDoc.GetPool());
            aAttr.Put(makeSdrCircStartAngleItem(9000_deg100));
            aAttr.Put(makeSdrCircEndAngleItem(0_deg100));

            if(SID_DRAW_PIE_NOFILL == nID ||
                SID_DRAW_CIRCLEPIE_NOFILL == nID ||
                SID_DRAW_ELLIPSECUT_NOFILL == nID ||
                SID_DRAW_CIRCLECUT_NOFILL == nID)
            {
                aAttr.Put(XFillStyleItem(drawing::FillStyle_NONE));
            }

            pObj->SetMergedItemSet(aAttr);
        }
        else
        {
            OSL_FAIL("Object is NO circle object");
        }
    }

    return pObj;
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
