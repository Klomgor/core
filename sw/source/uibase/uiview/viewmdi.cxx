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

#include <sfx2/dispatch.hxx>
#include <sfx2/viewfrm.hxx>
#include <svx/ruler.hxx>
#include <editeng/lrspitem.hxx>
#include <o3tl/safeint.hxx>
#include <svl/srchitem.hxx>
#include <svl/stritem.hxx>
#include <sfx2/request.hxx>
#include <swmodule.hxx>
#include <view.hxx>
#include <wrtsh.hxx>
#include <viewopt.hxx>
#include <frmatr.hxx>
#include <edtwin.hxx>
#include <pagedesc.hxx>
#include <IMark.hxx>
#include <fldbas.hxx>
#include <workctrl.hxx>
#include <usrpref.hxx>
#include <scroll.hxx>
#include <wview.hxx>

#include <cmdid.h>

#include <PostItMgr.hxx>
#include <AnnotationWin.hxx>

#include <svx/srchdlg.hxx>
#include <svx/svdview.hxx>

#include <vcl/uitest/logger.hxx>
#include <vcl/uitest/eventdescription.hxx>

sal_uInt16  SwView::s_nMoveType = NID_PGE;
sal_Int32 SwView::s_nActMark = 0;

namespace {

void collectUIInformation(const OUString& aFactor)
{
    EventDescription aDescription;
    aDescription.aID = "writer_edit";
    aDescription.aParameters = {{"ZOOM", aFactor}};
    aDescription.aAction = "SET";
    aDescription.aKeyWord = "SwEditWinUIObject";
    aDescription.aParent = "MainWindow";
    UITestLogger::getInstance().logEvent(aDescription);
}

}

void SwView::SetZoom( SvxZoomType eZoomType, short nFactor, bool bViewOnly )
{
    bool const bCursorIsVisible(m_pWrtShell->IsCursorVisible());
    SetZoom_( GetEditWin().GetOutputSizePixel(), eZoomType, nFactor, bViewOnly );
    // fdo#40465 force the cursor to stay in view whilst zooming
    if (bCursorIsVisible)
        m_pWrtShell->ShowCursor();

    Invalidate(SID_ZOOM_IN);
    Invalidate(SID_ZOOM_OUT);

    collectUIInformation(OUString::number(nFactor));
}

void SwView::SetZoom_( const Size &rEditSize, SvxZoomType eZoomType,
                        short nFactor, bool bViewOnly )
{
    bool bUnLockView = !m_pWrtShell->IsViewLocked();
    m_pWrtShell->LockView( true );
    m_pWrtShell->LockPaint(LockPaintReason::SetZoom);

    { // start of SwActContext scope
    SwActContext aActContext(m_pWrtShell.get());

    tools::Long nFac = nFactor;

    const bool bWeb = dynamic_cast< const SwWebView *>( this ) !=  nullptr;
    SwModule* mod = SwModule::get();
    SwMasterUsrPref *pUsrPref = const_cast<SwMasterUsrPref*>(mod->GetUsrPref(bWeb));

    const SwPageDesc &rDesc = m_pWrtShell->GetPageDesc( m_pWrtShell->GetCurPageDesc() );
    const SvxLRSpaceItem &rLRSpace = rDesc.GetMaster().GetLRSpace();
    const SwViewOption *pOpt = m_pWrtShell->GetViewOptions();
    tools::Long lLeftMargin = 0;

    if( eZoomType != SvxZoomType::PERCENT )
    {
        const bool bAutomaticViewLayout = 0 == pOpt->GetViewLayoutColumns();

        const SwRect aPageRect( m_pWrtShell->GetAnyCurRect( CurRectType::PageCalc ) );
        const SwRect aRootRect( m_pWrtShell->GetAnyCurRect( CurRectType::PagesArea ) );
        Size aPageSize( aPageRect.SSize() );
        Size aRootSize( aRootRect.SSize() );

        //mod #i6193# added sidebar width
        SwPostItMgr* pPostItMgr = GetPostItMgr();
        if (pPostItMgr->HasNotes() && pPostItMgr->ShowNotes())
            aPageSize.AdjustWidth(pPostItMgr->GetSidebarWidth() + pPostItMgr->GetSidebarBorderWidth() );

        const MapMode aTmpMap( MapUnit::MapTwip );
        const Size aWindowSize( GetEditWin().PixelToLogic( rEditSize, aTmpMap ) );

        if( SvxZoomType::OPTIMAL == eZoomType )
        {
            // unclear if this is useful for OPTIMAL, or completely useless?
            if( UseOnPage::Mirror == rDesc.GetUseOn() )    // mirrored pages
            {
                const SvxLRSpaceItem &rLeftLRSpace = rDesc.GetLeft().GetLRSpace();
                aPageSize.AdjustWidth(
                    std::abs(rLeftLRSpace.ResolveLeft({}) - rLRSpace.ResolveLeft({})));
            }

            if (!pPostItMgr->HasNotes() || !pPostItMgr->ShowNotes())
                aPageSize.AdjustWidth(
                    -(rLRSpace.ResolveLeft({}) + rLRSpace.ResolveRight({}) + nLeftOfst * 2));
            lLeftMargin = rLRSpace.ResolveLeft({}) + DOCUMENTBORDER + nLeftOfst;
            nFac = aWindowSize.Width() * 100 / aPageSize.Width();
        }
        else if(SvxZoomType::WHOLEPAGE == eZoomType || SvxZoomType::PAGEWIDTH == eZoomType )
        {
            const tools::Long nOf = DOCUMENTBORDER * 2;
            tools::Long nTmpWidth = bAutomaticViewLayout ? aPageSize.Width() : aRootSize.Width();
            nTmpWidth += nOf;
            aPageSize.AdjustHeight(nOf );
            nFac = aWindowSize.Width() * 100 / nTmpWidth;

            if ( SvxZoomType::WHOLEPAGE == eZoomType )
            {
                tools::Long nVisPercent = aWindowSize.Height() * 100 / aPageSize.Height();
                nFac = std::min( nFac, nVisPercent );
            }
        }
        else
        {
            const tools::Long nTmpWidth = bAutomaticViewLayout ? aPageSize.Width() : aRootSize.Width();
            nFac = aWindowSize.Width() * 100 / nTmpWidth;
        }
    }

    nFac = std::max( tools::Long( MINZOOM ), nFac );
    const sal_uInt16 nZoomFac = o3tl::narrowing<sal_uInt16>(nFac);

    SwViewOption aOpt( *pOpt );
    if ( !GetViewFrame().GetFrame().IsInPlace() )
    {
        //Update MasterUsrPrefs and after that update the ViewOptions of the current View.
        if ( !bViewOnly &&
                (nZoomFac != pUsrPref->GetZoom() ||
                eZoomType != pUsrPref->GetZoomType()) )
        {
            pUsrPref->SetZoom(nZoomFac);
            pUsrPref->SetZoomType(eZoomType);
            mod->ApplyUsrPref(*pUsrPref, nullptr);
            pUsrPref->SetModified();
        }
        if ( pOpt->GetZoom() != nZoomFac )
        {
            aOpt.SetZoom(nZoomFac);
            aOpt.SetReadonly(pOpt->IsReadonly());
            m_pWrtShell->ApplyViewOptions( aOpt );
        }
        if ( eZoomType != SvxZoomType::PERCENT )
        {
            Point aPos;

            if ( eZoomType == SvxZoomType::WHOLEPAGE )
                aPos.setY( m_pWrtShell->GetAnyCurRect(CurRectType::Page).Top() - DOCUMENTBORDER );
            else
            {
                // Make sure that the cursor is in the visible range, so that
                // the scrolling will be performed only once.
                aPos.setX( lLeftMargin );
                const SwRect &rCharRect = m_pWrtShell->GetCharRect();
                if ( rCharRect.Top() > GetVisArea().Bottom() ||
                    rCharRect.Bottom() < aPos.Y() )
                    aPos.setY( rCharRect.Top() - rCharRect.Height() );
                else
                    aPos.setY( GetVisArea().Top() );
            }
            SetVisArea( aPos );
        }
        // Compromise solution - Under certain circumstances SetZoom is called
        // in CalcVisAreas again and thus be set wrong values.
        const_cast<SwViewOption*>(m_pWrtShell->GetViewOptions())->SetZoomType( eZoomType );
        CalcVisArea( rEditSize );   // for the recalculation of the viewable area
    }
    else if ( nZoomFac != pOpt->GetZoom() )
    {
        aOpt.SetZoom( nZoomFac );
        m_pWrtShell->ApplyViewOptions( aOpt );
    }

    const Fraction aFrac( nFac, 100 );
    m_pVRuler->SetZoom( aFrac );
    m_pVRuler->ForceUpdate();
    m_pHRuler->SetZoom( aFrac );
    m_pHRuler->ForceUpdate();
    const_cast<SwViewOption*>(m_pWrtShell->GetViewOptions())->SetZoomType( eZoomType );
    } // end of SwActContext scope

    m_pWrtShell->UnlockPaint();
    if( bUnLockView )
        m_pWrtShell->LockView( false );
}

void SwView::SetViewLayout( sal_uInt16 nColumns, bool bBookMode, bool bViewOnly )
{
    SwActContext aActContext(m_pWrtShell.get());

    if ( !GetViewFrame().GetFrame().IsInPlace() && !bViewOnly )
    {
        const bool bWeb = dynamic_cast< const SwWebView *>( this ) !=  nullptr;
        SwModule* mod = SwModule::get();
        SwMasterUsrPref *pUsrPref = const_cast<SwMasterUsrPref*>(mod->GetUsrPref(bWeb));

        // Update MasterUsrPrefs and after that update the ViewOptions of the current View.
        if ( nColumns  != pUsrPref->GetViewLayoutColumns() ||
             bBookMode != pUsrPref->IsViewLayoutBookMode() )
        {
            pUsrPref->SetViewLayoutColumns(nColumns);
            pUsrPref->SetViewLayoutBookMode(bBookMode);
            mod->ApplyUsrPref(*pUsrPref, nullptr);
            pUsrPref->SetModified();
        }
    }

    const SwViewOption *pOpt = m_pWrtShell->GetViewOptions();

    if ( nColumns  != pOpt->GetViewLayoutColumns() ||
         bBookMode != pOpt->IsViewLayoutBookMode() )
    {
        SwViewOption aOpt( *pOpt );
        aOpt.SetViewLayoutColumns( nColumns );
        aOpt.SetViewLayoutBookMode( bBookMode );
        m_pWrtShell->ApplyViewOptions( aOpt );
    }

    m_pVRuler->ForceUpdate();
    m_pHRuler->ForceUpdate();

    SfxBindings& rBnd = GetViewFrame().GetBindings();
    rBnd.Invalidate( SID_ATTR_VIEWLAYOUT );
    rBnd.Invalidate( SID_ATTR_ZOOMSLIDER);
}

// Scrollbar - Handler

IMPL_LINK( SwView, WindowChildEventListener, VclWindowEvent&, rEvent, void )
{
    OSL_ENSURE( rEvent.GetWindow(), "Window???" );
    vcl::Window* pChildWin = static_cast< vcl::Window* >( rEvent.GetData() );

    switch ( rEvent.GetId() )
    {
        case VclEventId::WindowHide:
            if( pChildWin == m_pHScrollbar )
                ShowHScrollbar( false );
            else if( pChildWin == m_pVScrollbar )
                ShowVScrollbar( false );
            break;
        case VclEventId::WindowShow:
            if( pChildWin == m_pHScrollbar )
                ShowHScrollbar( true );
            else if( pChildWin == m_pVScrollbar )
                ShowVScrollbar( true );
            break;
        default: break;
    }
}

void SwView::CreateScrollbar( bool bHori )
{
    vcl::Window *pMDI = &GetViewFrame().GetWindow();
    VclPtr<SwScrollbar>& ppScrollbar = bHori ? m_pHScrollbar : m_pVScrollbar;

    assert(!ppScrollbar); //check beforehand!

    ppScrollbar = VclPtr<SwScrollbar>::Create( pMDI, bHori );
    UpdateScrollbars();
    if(bHori)
        ppScrollbar->SetScrollHdl( LINK( this, SwView, HoriScrollHdl ));
    else
        ppScrollbar->SetScrollHdl( LINK( this, SwView, VertScrollHdl ));

    if(GetWindow())
        InvalidateBorder();

    if (!m_bShowAtResize)
        ppScrollbar->ExtendedShow();
}

IMPL_LINK( SwView, MoveNavigationHdl, void*, p, void )
{
    bool* pbNext = static_cast<bool*>(p);
    if ( !pbNext )
        return;
    const bool bNext = *pbNext;
    SwWrtShell& rSh = GetWrtShell();
    if ( NID_SRCH_REP != s_nMoveType)
    {
        if ( rSh.GetDrawView()->IsTextEdit() )
            rSh.EndTextEdit();
        if ( IsDrawMode() )
            LeaveDrawCreate();
    }
    if ( NID_POSTIT != s_nMoveType && m_pPostItMgr )
    {
        sw::annotation::SwAnnotationWin* pActiveSidebarWin = m_pPostItMgr->GetActiveSidebarWin();
        if (pActiveSidebarWin)
            pActiveSidebarWin->SwitchToFieldPos();
    }
    if (NID_RECENCY != s_nMoveType && NID_PGE != s_nMoveType && NID_SRCH_REP != s_nMoveType)
        rSh.addCurrentPosition();
    switch( s_nMoveType )
    {
        case NID_PGE:
        {
            tools::Long nYPos;
            SwVisiblePageNumbers aVisiblePageNumbers;
            rSh.GetFirstLastVisPageNumbers(aVisiblePageNumbers, rSh.GetView());
            if ((bNext && aVisiblePageNumbers.nLastPhy + 1 > rSh.GetPageCnt()) ||
                (!bNext && aVisiblePageNumbers.nFirstPhy == 1))
            {
                nYPos = rSh.GetPagePos(bNext ? 1 : rSh.GetPageCnt()).Y();
                SvxSearchDialogWrapper::SetSearchLabel(bNext ? SearchLabel::EndWrapped :
                                                               SearchLabel::StartWrapped);
            }
            else
            {
                auto nPage = bNext ? aVisiblePageNumbers.nLastPhy + 1 :
                                     aVisiblePageNumbers.nFirstPhy - 1;
                nYPos = rSh.GetPagePos(nPage).Y();
                SvxSearchDialogWrapper::SetSearchLabel(SearchLabel::Empty);
            }
            const Point aPt(GetVisArea().Left(), nYPos);
            Point aAlPt(AlignToPixel(aPt));
            // If there is a difference, has been truncated --> then add one pixel,
            // so that no residue of the previous page is visible.
            if(aPt.Y() != aAlPt.Y())
                aAlPt.AdjustY(3 * GetEditWin().PixelToLogic(Size(0, 1)).Height());
            SetVisArea(aAlPt);
        }
        break;
        case NID_TBL :
            rSh.EnterStdMode();
            if(bNext)
                rSh.MoveTable(GotoNextTable, fnTableStart);
            else
                rSh.MoveTable(GotoPrevTable, fnTableStart);
        break;
        case NID_FRM :
        case NID_GRF:
        case NID_OLE:
        {
            GotoObjFlags eType = GotoObjFlags::FlyFrame;
            if(s_nMoveType == NID_GRF)
                eType = GotoObjFlags::FlyGrf;
            else if(s_nMoveType == NID_OLE)
                eType = GotoObjFlags::FlyOLE;
            bool bSuccess = bNext ?
                    rSh.GotoNextFly(eType) :
                        rSh.GotoPrevFly(eType);
            if(bSuccess)
            {
                rSh.HideCursor();
                rSh.EnterSelFrameMode();
            }
        }
        break;
        case NID_CTRL:
            if (!rSh.GetView().IsDesignMode())
                rSh.GetView().GetFormShell()->SetDesignMode(true);
            [[fallthrough]];
        case NID_DRW:
        {
            bool bSuccess = rSh.GotoObj(bNext,
                    s_nMoveType == NID_DRW ?
                        GotoObjFlags::DrawSimple :
                        GotoObjFlags::DrawControl);
            if(bSuccess)
            {
                rSh.HideCursor();
                rSh.EnterSelFrameMode();
            }
        }
        break;
        case NID_REG :
            rSh.EnterStdMode();
            if(bNext)
                rSh.MoveRegion(GotoNextRegion, fnRegionStart);
            else
                rSh.MoveRegion(GotoPrevRegion, fnRegionStart);

        break;
        case NID_BKM :
            rSh.EnterStdMode();
            GetViewFrame().GetDispatcher()->Execute(bNext ?
                                        FN_NEXT_BOOKMARK :
                                            FN_PREV_BOOKMARK);
            break;
        case NID_FIELD:
        {
            rSh.EnterStdMode();
            rSh.StartAction();
            SearchLabel eSearchLabel = SearchLabel::Empty;
            if (!rSh.MoveFieldType(nullptr, bNext, SwFieldIds::Unknown))
            {
                // no field found in the move direction
                // wrap and try again
                SwShellCursor* pCursor = rSh.GetCursor_();
                SwCursorSaveState aSaveState(*pCursor);
                rSh.SttEndDoc(bNext);
                // document might have a field at the start of the document
                SwField* pField = rSh.GetCurField();
                if ((bNext && pField && pField->GetTypeId() != SwFieldTypesEnum::Postit) ||
                    rSh.MoveFieldType(nullptr, bNext, SwFieldIds::Unknown))
                {
                    eSearchLabel = bNext ? SearchLabel::EndWrapped : SearchLabel::StartWrapped;
                }
                else
                {
                    // no visible fields found
                    pCursor->RestoreSavePos();
                    eSearchLabel = SearchLabel::NavElementNotFound;
                }
            }
            SvxSearchDialogWrapper::SetSearchLabel(eSearchLabel);
            rSh.EndAction();
        }
        break;
        case NID_FIELD_BYTYPE:
        {
            // see: SwFieldMgr::GoNextPrev
            SwField* pCurField = rSh.GetCurField(true);
            if (!pCurField)
                break;
            rSh.EnterStdMode();
            SwFieldType* pTyp = nullptr;
            const SwFieldTypesEnum nTypeId = pCurField->GetTypeId();
            if (SwFieldTypesEnum::SetInput == nTypeId || SwFieldTypesEnum::UserInput == nTypeId)
                pTyp = rSh.GetFieldType(0, SwFieldIds::Input);
            else
                pTyp = pCurField->GetTyp();
            if (pTyp)
            {
                if (pTyp->Which() == SwFieldIds::Database)
                    rSh.MoveFieldType(nullptr, bNext, SwFieldIds::Database);
                else
                    rSh.MoveFieldType(pTyp, bNext);
            }
        }
        break;
        case NID_OUTL:
            rSh.EnterStdMode();
            bNext ? rSh.GotoNextOutline() : rSh.GotoPrevOutline();
        break;
        case NID_SEL :
            rSh.GoNextPrevCursorSetSearchLabel(bNext);
        break;
        case NID_FTN:
        {
            bool bFrameTypeFootnote(rSh.GetFrameType(nullptr, false) & FrameTypeFlags::FOOTNOTE);

            if (bFrameTypeFootnote)
            {
                rSh.LockView(true);
                rSh.GotoFootnoteAnchor();
            }

            rSh.EnterStdMode();
            bNext ?
                rSh.GotoNextFootnoteAnchor() :
                    rSh.GotoPrevFootnoteAnchor();

            if (bFrameTypeFootnote)
            {
                rSh.LockView(false);
                rSh.GotoFootnoteText();
            }
        }
        break;
        case NID_MARK:
        {
            // unselect
            rSh.MoveCursor();
            rSh.EnterStdMode();

            // collect and sort navigator reminder names
            IDocumentMarkAccess* const pMarkAccess = rSh.getIDocumentMarkAccess();
            std::vector< SwMarkName > vNavMarkNames;
            for(auto ppMark = pMarkAccess->getAllMarksBegin();
                ppMark != pMarkAccess->getAllMarksEnd();
                ++ppMark)
            {
                if( IDocumentMarkAccess::GetType(**ppMark) == IDocumentMarkAccess::MarkType::NAVIGATOR_REMINDER )
                    vNavMarkNames.push_back((*ppMark)->GetName());
            }
            std::sort(vNavMarkNames.begin(), vNavMarkNames.end());

            // move
            if(!vNavMarkNames.empty())
            {
                SvxSearchDialogWrapper::SetSearchLabel( SearchLabel::Empty );

                if(bNext)
                {
                    s_nActMark++;
                    if (s_nActMark >= MAX_MARKS || s_nActMark >= static_cast<sal_Int32>(vNavMarkNames.size()))
                    {
                        s_nActMark = 0;
                        SvxSearchDialogWrapper::SetSearchLabel( SearchLabel::ReminderEndWrapped );
                    }
                }
                else
                {
                    s_nActMark--;
                    if (s_nActMark < 0 || o3tl::make_unsigned(s_nActMark) >= vNavMarkNames.size())
                    {
                        s_nActMark = vNavMarkNames.size()-1;
                        SvxSearchDialogWrapper::SetSearchLabel( SearchLabel::ReminderStartWrapped );
                    }
                }
                rSh.GotoMark(vNavMarkNames[s_nActMark]);
            }
            else
                SvxSearchDialogWrapper::SetSearchLabel( SearchLabel::NavElementNotFound );
        }
        break;

        case NID_POSTIT:
        {
            if (m_pPostItMgr->HasNotes())
            {
                rSh.EnterStdMode();
                m_pPostItMgr->AssureStdModeAtShell();
                m_pPostItMgr->SetActiveSidebarWin(nullptr);
                GetEditWin().GrabFocus();
                SwShellCursor* pCursor = rSh.GetCursor_();
                SwCursorSaveState aSaveState(*pCursor);
                SwFieldType* pFieldType = rSh.GetFieldType(0, SwFieldIds::Postit);
                bool bWrapped = false;
                bool bFound = false;
                rSh.StartAction();
                while (!bFound)
                {
                    if (!rSh.MoveFieldType(pFieldType, bNext))
                    {
                        if (bWrapped)
                            break;
                        bWrapped = true;
                        rSh.SttEndDoc(bNext);
                        continue;
                    }
                    do
                    {
                        auto pAnnotationWin = m_pPostItMgr->GetAnnotationWin(
                                    rSh.GetPostItFieldAtCursor());
                        if (pAnnotationWin && pAnnotationWin->IsVisible())
                            bFound = true;
                    } while (!bFound && rSh.MoveFieldType(pFieldType, bNext));
                }
                if (!bFound)
                {
                    pCursor->RestoreSavePos();
                    SvxSearchDialogWrapper::SetSearchLabel(SearchLabel::NavElementNotFound);
                }
                rSh.EndAction();
                if (bFound)
                {
                    GetViewFrame().GetDispatcher()->Execute(FN_POSTIT);
                    if (bWrapped)
                        SvxSearchDialogWrapper::SetSearchLabel(bNext ? SearchLabel::EndWrapped :
                                                                       SearchLabel::StartWrapped);
                    else
                        SvxSearchDialogWrapper::SetSearchLabel(SearchLabel::Empty);
                }
            }
            else
                SvxSearchDialogWrapper::SetSearchLabel(SearchLabel::NavElementNotFound);
        }
        break;

        case NID_SRCH_REP:
        if(s_pSrchItem)
        {
            bool bBackward = s_pSrchItem->GetBackward();
            if (rSh.HasSelection() && bNext != rSh.IsCursorPtAtEnd())
                rSh.SwapPam();
            s_pSrchItem->SetBackward(!bNext);
            SfxRequest aReq(FN_REPEAT_SEARCH, SfxCallMode::SLOT, GetPool());
            ExecSearch(aReq);
            s_pSrchItem->SetBackward(bBackward);
        }
        break;
        case NID_INDEX_ENTRY:
            rSh.GotoNxtPrvTOXMark(bNext);
        break;

        case NID_TABLE_FORMULA:
            rSh.GotoNxtPrvTableFormula( bNext );
            break;

        case NID_TABLE_FORMULA_ERROR:
            rSh.GotoNxtPrvTableFormula( bNext, true );
            break;

        case NID_RECENCY :
            rSh.EnterStdMode();
            bNext ? rSh.GetNavigationMgr().goForward() : rSh.GetNavigationMgr().goBack();
            break;
    }
    if (NID_POSTIT != s_nMoveType)
        m_pEditWin->GrabFocus();
    delete pbNext;
}

void SwView::CreateTab()
{
    m_pHRuler->SetActive(GetFrame() && IsActive());

    m_pHRuler->Show();
    InvalidateBorder();
}

void SwView::KillTab()
{
    m_pHRuler->Hide();
    InvalidateBorder();
}

void SwView::ChangeTabMetric( FieldUnit eUnit )
{
    if(m_pHRuler->GetUnit() != eUnit )
    {
        m_pHRuler->SetUnit( eUnit );
        m_pHRuler->Invalidate();
    }
}

void SwView::ChangeVRulerMetric( FieldUnit eUnit )
{
    if(m_pVRuler->GetUnit() != eUnit)
    {
        m_pVRuler->SetUnit( eUnit );
        m_pVRuler->Invalidate();
    }
}

void SwView::GetVRulerMetric(FieldUnit& eToFill) const
{
    eToFill = m_pVRuler->GetUnit();
}

void SwView::GetHRulerMetric(FieldUnit& eToFill) const
{
    eToFill = m_pHRuler->GetUnit();
}

void SwView::CreateVRuler()
{
    m_pHRuler->SetBorderPos( m_pVRuler->GetSizePixel().Width()-1 );

    m_pVRuler->SetActive(GetFrame() && IsActive());
    m_pVRuler->Show();
    InvalidateBorder();
}

void SwView::KillVRuler()
{
    m_pVRuler->Hide();
    m_pHRuler->SetBorderPos();
    InvalidateBorder();
}

IMPL_LINK( SwView, ExecRulerClick, Ruler *, pRuler, void )
{
    OUString sDefPage;
    TypedWhichId<SfxStringItem> nDefDlg = SID_PARA_DLG;
    switch( pRuler->GetClickType() )
    {
        case RulerType::DontKnow:
        case RulerType::Outside:
            sDefPage="borders";
            break;
        case RulerType::Indent:
            sDefPage="indents";
            break;
        case RulerType::Margin1:
        case RulerType::Margin2:
            nDefDlg= FN_FORMAT_PAGE_DLG;
            sDefPage = "page";
            break;
        default:
            sDefPage = "tabs";

    }

    SfxStringItem aDefPage(nDefDlg, sDefPage);
    GetViewFrame().GetDispatcher()->ExecuteList(nDefDlg,
                                SfxCallMode::SYNCHRON|SfxCallMode::RECORD,
                                { &aDefPage });
}

sal_uInt16 SwView::GetMoveType()
{
    return s_nMoveType;
}

void SwView::SetMoveType(sal_uInt16 nSet)
{
    s_nMoveType = nSet;
}

void SwView::SetActMark(sal_Int32 nSet)
{
    s_nActMark = nSet;
}

void SwView::ShowHScrollbar(bool bShow)
{
    assert(m_pHScrollbar && "Scrollbar invalid");
    m_pHScrollbar->ExtendedShow(bShow);
}

bool SwView::IsHScrollbarVisible()const
{
    assert(m_pHScrollbar && "Scrollbar invalid");
    return m_pHScrollbar->IsScrollbarVisible(false) || m_pHScrollbar->IsAuto();
}

void SwView::ShowVScrollbar(bool bShow)
{
    assert(m_pVScrollbar && "Scrollbar invalid");
    m_pVScrollbar->ExtendedShow(bShow);
}

bool SwView::IsVScrollbarVisible()const
{
    assert(m_pVScrollbar && "Scrollbar invalid");
    return m_pVScrollbar->IsScrollbarVisible(false);
}

void SwView::EnableHScrollbar(bool bEnable)
{
    if (m_bHScrollbarEnabled != bEnable)
    {
        m_bHScrollbarEnabled = bEnable;
        InvalidateBorder();
    }
}

void SwView::EnableVScrollbar(bool bEnable)
{
    if (m_bVScrollbarEnabled != bEnable)
    {
        m_bVScrollbarEnabled = bEnable;
        InvalidateBorder();
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
