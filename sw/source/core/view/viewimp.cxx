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

#include <config_wasm_strip.h>

#include <rootfrm.hxx>
#include <pagefrm.hxx>
#include <viewimp.hxx>
#include <viewopt.hxx>
#include <flyfrm.hxx>
#include <layact.hxx>
#include <dview.hxx>
#include <svx/svdpage.hxx>
#include <accmap.hxx>

#include <officecfg/Office/Common.hxx>
#include <pagepreviewlayout.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <drawdoc.hxx>
#include <prevwpage.hxx>
#include <sfx2/viewsh.hxx>
#include <svx/sdr/contact/viewobjectcontact.hxx>
#include <svx/sdr/contact/viewcontact.hxx>

void SwViewShellImp::Init( const SwViewOption *pNewOpt )
{
    OSL_ENSURE( m_pDrawView, "SwViewShellImp::Init without DrawView" );
    //Create PageView if it doesn't exist
    SwRootFrame *pRoot = m_rShell.GetLayout();
    if ( !m_pSdrPageView )
    {
        IDocumentDrawModelAccess& rIDDMA = m_rShell.getIDocumentDrawModelAccess();
        if ( !pRoot->GetDrawPage() )
            pRoot->SetDrawPage( rIDDMA.GetDrawModel()->GetPage( 0 ) );

        if ( pRoot->GetDrawPage()->GetSize() != pRoot->getFrameArea().SSize() )
            pRoot->GetDrawPage()->SetSize( pRoot->getFrameArea().SSize() );

        m_pSdrPageView = m_pDrawView->ShowSdrPage( pRoot->GetDrawPage());
        // Notify drawing page view about invisible layers
        rIDDMA.NotifyInvisibleLayers( *m_pSdrPageView );
    }
    m_pDrawView->SetDragStripes( pNewOpt->IsCrossHair() );
    m_pDrawView->SetGridSnap( pNewOpt->IsSnap() );
    m_pDrawView->SetGridVisible( pNewOpt->IsGridVisible() );
    const Size &rSz = pNewOpt->GetSnapSize();
    m_pDrawView->SetGridCoarse( rSz );
    const Size aFSize
            ( rSz.Width() ? rSz.Width() /std::max(short(1),pNewOpt->GetDivisionX()):0,
              rSz.Height()? rSz.Height()/std::max(short(1),pNewOpt->GetDivisionY()):0);
    m_pDrawView->SetGridFine( aFSize );
    Fraction aSnGrWdtX(rSz.Width(), pNewOpt->GetDivisionX() + 1);
    Fraction aSnGrWdtY(rSz.Height(), pNewOpt->GetDivisionY() + 1);
    m_pDrawView->SetSnapGridWidth( aSnGrWdtX, aSnGrWdtY );

    if ( pRoot->getFrameArea().HasArea() )
        m_pDrawView->SetWorkArea( pRoot->getFrameArea().SVRect() );

    if ( GetShell().IsPreview() )
        m_pDrawView->SetAnimationEnabled( false );

    m_pDrawView->SetUseIncompatiblePathCreateInterface( false );

    // set handle size to 9 pixels, always
    m_pDrawView->SetMarkHdlSizePixel(9);
}

/// CTor for the core internals
SwViewShellImp::SwViewShellImp( SwViewShell &rParent ) :
    m_rShell( rParent ),
    m_pSdrPageView( nullptr ),
    m_pFirstVisiblePage( nullptr ),
    m_pLayAction( nullptr ),
    m_pIdleAct( nullptr ),
    m_bFirstPageInvalid( true ),
    m_bResetHdlHiddenPaint( false ),
    m_bSmoothUpdate( false ),
    m_bStopSmooth( false ),
    m_nRestoreActions( 0 )
{
}

SwViewShellImp::~SwViewShellImp()
{
#if !ENABLE_WASM_STRIP_ACCESSIBILITY
    m_pAccessibleMap.reset();
#endif

    m_pPagePreviewLayout.reset();

    // Make sure HideSdrPage is also executed after ShowSdrPage.
    if( m_pDrawView )
         m_pDrawView->HideSdrPage();

    m_pDrawView.reset();

    DeletePaintRegion();

    OSL_ENSURE( !m_pLayAction, "Have action for the rest of your life." );
    OSL_ENSURE( !m_pIdleAct,"Be idle for the rest of your life." );
}

bool SwViewShellImp::AddPaintRect( const SwRect &rRect )
{
    // In case of tiled rendering the visual area is the last painted tile -> not interesting.
    if ( rRect.Overlaps( m_rShell.VisArea() ) || comphelper::LibreOfficeKit::isActive() )
    {
        if ( !m_oPaintRegion )
        {
            // In case of normal rendering, this makes sure only visible rectangles are painted.
            // Otherwise get the rectangle of the full document, so all paint rectangles are invalidated.
            const SwRect& rArea = comphelper::LibreOfficeKit::isActive() ? m_rShell.GetLayout()->getFrameArea() : m_rShell.VisArea();
            m_oPaintRegion.emplace();
            m_oPaintRegion->ChangeOrigin(rArea);
        }
        if(!m_oPaintRegion->empty())
        {
            // This function often gets called with rectangles that line up vertically.
            // Try to extend the last one downwards to include the new one (use Union()
            // in case the new one is actually already contained in the last one).
            SwRect& last = m_oPaintRegion->back();
            if(last.Left() == rRect.Left() && last.Width() == rRect.Width()
                && last.Bottom() + 1 >= rRect.Top() && last.Bottom() <= rRect.Bottom())
            {
                last.Union(rRect);
                // And these rectangles lined up vertically often come up in groups
                // that line up horizontally. Try to extend the previous rectangle
                // to the right to include the last one.
                if(m_oPaintRegion->size() > 1)
                {
                    SwRect& last2 = (*m_oPaintRegion)[m_oPaintRegion->size() - 2];
                    if(last2.Top() == last.Top() && last2.Height() == last.Height()
                        && last2.Right() + 1 >= last.Left() && last2.Right() <= last2.Right())
                    {
                        last2.Union(last);
                        m_oPaintRegion->pop_back();
                        return true;
                    }
                }
                return true;
            }
        }
        (*m_oPaintRegion) += rRect;
        return true;
    }
    return false;
}

void SwViewShellImp::AddPendingLOKInvalidation( const SwRect& rRect )
{
    std::vector<SwRect>& l = m_pendingLOKInvalidations;
    if(l.empty() && m_rShell.GetSfxViewShell()) // Announce that these invalidations will need flushing.
        m_rShell.GetSfxViewShell()->libreOfficeKitViewAddPendingInvalidateTiles();
    // These are often repeated, so check first for duplicates.
    if( std::find( l.begin(), l.end(), rRect ) == l.end())
        l.push_back( rRect );
}

std::vector<SwRect> SwViewShellImp::TakePendingLOKInvalidations()
{
    std::vector<SwRect> ret;
    std::swap(ret, m_pendingLOKInvalidations);
    return ret;
}

void SwViewShellImp::CheckWaitCursor()
{
    if ( m_pLayAction )
        m_pLayAction->CheckWaitCursor();
}

bool SwViewShellImp::IsCalcLayoutProgress() const
{
    return m_pLayAction && m_pLayAction->IsCalcLayout();
}

bool SwViewShellImp::IsUpdateExpFields()
{
    if ( m_pLayAction && m_pLayAction->IsCalcLayout() )
    {
        m_pLayAction->SetUpdateExpFields();
        return true;
    }
    return false;
}

void SwViewShellImp::SetFirstVisPage(OutputDevice const * pRenderContext)
{
    if ( m_rShell.mbDocSizeChgd && m_rShell.VisArea().Top() > m_rShell.GetLayout()->getFrameArea().Height() )
    {
        //We are in an action and because of erase actions the VisArea is
        //after the first visible page.
        //To avoid excessive formatting, hand back the last page.
        m_pFirstVisiblePage = static_cast<SwPageFrame*>(m_rShell.GetLayout()->Lower());
        while ( m_pFirstVisiblePage && m_pFirstVisiblePage->GetNext() )
            m_pFirstVisiblePage = static_cast<SwPageFrame*>(m_pFirstVisiblePage->GetNext());
    }
    else
    {
        const SwViewOption* pSwViewOption = GetShell().GetViewOptions();
        const bool bBookMode = pSwViewOption->IsViewLayoutBookMode();

        SwPageFrame *pPage = static_cast<SwPageFrame*>(m_rShell.GetLayout()->Lower());
        SwRect aPageRect = pPage ? pPage->GetBoundRect(pRenderContext) : SwRect();
        while ( pPage && !aPageRect.Overlaps( m_rShell.VisArea() ) )
        {
            pPage = static_cast<SwPageFrame*>(pPage->GetNext());
            if ( pPage )
            {
                aPageRect = pPage->GetBoundRect(pRenderContext);
                if ( bBookMode && pPage->IsEmptyPage() )
                {
                    const SwPageFrame& rFormatPage = pPage->GetFormatPage();
                    aPageRect.SSize( rFormatPage.GetBoundRect(pRenderContext).SSize() );
                }
            }
        }
        m_pFirstVisiblePage = pPage ? pPage : static_cast<SwPageFrame*>(m_rShell.GetLayout()->Lower());
    }
    m_bFirstPageInvalid = false;
}

void SwViewShellImp::MakeDrawView()
{
    IDocumentDrawModelAccess& rIDDMA = GetShell().getIDocumentDrawModelAccess();

    // the else here is not an error, MakeDrawModel_() calls this method again
    // after the DrawModel is created to create DrawViews for all shells...
    if( !rIDDMA.GetDrawModel() )
    {
        rIDDMA.MakeDrawModel_();
    }
    else
    {
        if ( !m_pDrawView )
        {
            // #i72809#
            // Discussed with FME, he also thinks that the getPrinter is old and not correct. When i got
            // him right, it anyways returns GetOut() when it's a printer, but NULL when not. He suggested
            // to use GetOut() and check the existing cases.
            // Check worked well. Took a look at viewing, printing, PDF export and print preview with a test
            // document which has an empty 2nd page (right page, see bug)
            auto pWin = GetShell().GetWin();
            OutputDevice* pOutDevForDrawView = pWin ? pWin->GetOutDev() : nullptr;

            if(!pOutDevForDrawView)
            {
                pOutDevForDrawView = GetShell().GetOut();
            }

            m_pDrawView.reset( new SwDrawView(
                *this,
                rIDDMA.GetOrCreateDrawModel(),
                pOutDevForDrawView) );
        }

        GetDrawView()->SetActiveLayer(u"Heaven"_ustr);
        const SwViewOption* pSwViewOption = GetShell().GetViewOptions();
        Init(pSwViewOption);

        // #i68597# If document is read-only, we will not profit from overlay,
        // so switch it off.
        if (m_pDrawView->IsBufferedOverlayAllowed())
        {
            if(pSwViewOption->IsReadonly())
            {
                m_pDrawView->SetBufferedOverlayAllowed(false);
            }
        }
    }
}

Color SwViewShellImp::GetRetoucheColor() const
{
    Color aRet( COL_TRANSPARENT );
    const SwViewShell &rSh = GetShell();
    if (rSh.GetWin() || rSh.isOutputToWindow())
    {
        if ( rSh.GetViewOptions()->getBrowseMode() &&
             COL_TRANSPARENT != rSh.GetViewOptions()->GetRetoucheColor() )
            aRet = rSh.GetViewOptions()->GetRetoucheColor();
        else if(rSh.GetViewOptions()->IsPagePreview()  &&
                    !officecfg::Office::Common::Accessibility::IsForPagePreviews::get())
            aRet = COL_WHITE;
        else
            aRet = rSh.GetViewOptions()->GetDocColor();
    }
    return aRet;
}

SwPageFrame *SwViewShellImp::GetFirstVisPage(OutputDevice const * pRenderContext)
{
    if ( m_bFirstPageInvalid )
        SetFirstVisPage(pRenderContext);
    return m_pFirstVisiblePage;
}

const SwPageFrame *SwViewShellImp::GetFirstVisPage(OutputDevice const * pRenderContext) const
{
    if ( m_bFirstPageInvalid )
        const_cast<SwViewShellImp*>(this)->SetFirstVisPage(pRenderContext);
    return m_pFirstVisiblePage;
}

const SwPageFrame* SwViewShellImp::GetLastVisPage(const OutputDevice* pRenderContext) const
{
    const SwViewOption* pSwViewOption = m_rShell.GetViewOptions();
    const bool bBookMode = pSwViewOption->IsViewLayoutBookMode();
    const SwPageFrame* pPage = GetFirstVisPage(pRenderContext);
    const SwPageFrame* pLastVisPage = pPage;
    SwRect aPageRect = pPage->GetBoundRect(pRenderContext);
    while (pPage && (pPage->IsEmptyPage() || aPageRect.Overlaps(m_rShell.VisArea())))
    {
        pLastVisPage = pPage;
        pPage = static_cast<const SwPageFrame*>(pPage->GetNext());
        if (pPage)
        {
            aPageRect = pPage->GetBoundRect(pRenderContext);
            if (bBookMode && pPage->IsEmptyPage())
            {
                const SwPageFrame& rFormatPage = pPage->GetFormatPage();
                aPageRect.SSize(rFormatPage.GetBoundRect(pRenderContext).SSize());
            }
        }
    }
    return pLastVisPage;
}

// create page preview layout
void SwViewShellImp::InitPagePreviewLayout()
{
    OSL_ENSURE( m_rShell.GetLayout(), "no layout - page preview layout can not be created.");
    if ( m_rShell.GetLayout() )
        m_pPagePreviewLayout.reset( new SwPagePreviewLayout(m_rShell, *(m_rShell.GetLayout()) ) );
}

#if !ENABLE_WASM_STRIP_ACCESSIBILITY
void SwViewShellImp::UpdateAccessible()
{
    // We require a layout and an XModel to be accessible.
    IDocumentLayoutAccess& rIDLA = GetShell().getIDocumentLayoutAccess();
    vcl::Window *pWin = GetShell().GetWin();
    OSL_ENSURE( GetShell().GetLayout(), "no layout, no access" );
    OSL_ENSURE( pWin, "no window, no access" );

    if( IsAccessible() && rIDLA.GetCurrentViewShell() && pWin )
    {
        try
        {
            GetAccessibleMap().GetDocumentView();
        }
        catch (uno::Exception const&)
        {
            TOOLS_WARN_EXCEPTION("sw.a11y", "");
            assert(!"SwViewShellImp::UpdateAccessible: unhandled exception");
        }
    }
}

void SwViewShellImp::DisposeAccessible(const SwFrame *pFrame,
                                       const SdrObject *pObj,
                                       bool bRecursive,
                                       bool bCanSkipInvisible)
{
    OSL_ENSURE( !pFrame || pFrame->IsAccessibleFrame(), "frame is not accessible" );
    for(SwViewShell& rTmp : GetShell().GetRingContainer())
    {
        if( rTmp.Imp()->IsAccessible() )
            rTmp.Imp()->GetAccessibleMap().A11yDispose( pFrame, pObj, nullptr, bRecursive, bCanSkipInvisible );
    }
}

void SwViewShellImp::MoveAccessible( const SwFrame *pFrame, const SdrObject *pObj,
                                const SwRect& rOldFrame )
{
    OSL_ENSURE( !pFrame || pFrame->IsAccessibleFrame(), "frame is not accessible" );
    for(SwViewShell& rTmp : GetShell().GetRingContainer())
    {
        if( rTmp.Imp()->IsAccessible() )
            rTmp.Imp()->GetAccessibleMap().InvalidatePosOrSize( pFrame, pObj, nullptr,
                                                                 rOldFrame );
    }
}

void SwViewShellImp::InvalidateAccessibleFrameContent( const SwFrame *pFrame )
{
    OSL_ENSURE( pFrame->IsAccessibleFrame(), "frame is not accessible" );
    for(SwViewShell& rTmp : GetShell().GetRingContainer())
    {
        if( rTmp.Imp()->IsAccessible() )
            rTmp.Imp()->GetAccessibleMap().InvalidateContent( pFrame );
    }
}

void SwViewShellImp::InvalidateAccessibleCursorPosition( const SwFrame *pFrame )
{
    if( IsAccessible() )
        GetAccessibleMap().InvalidateCursorPosition( pFrame );
}

void SwViewShellImp::InvalidateAccessibleEditableState( bool bAllShells,
                                                      const SwFrame *pFrame )
{
    if( bAllShells )
    {
        for(SwViewShell& rTmp : GetShell().GetRingContainer())
        {
            if( rTmp.Imp()->IsAccessible() )
                rTmp.Imp()->GetAccessibleMap().InvalidateEditableStates( pFrame );
        }
    }
    else if( IsAccessible() )
    {
        GetAccessibleMap().InvalidateEditableStates( pFrame );
    }
}

void SwViewShellImp::InvalidateAccessibleRelationSet(const SwFlyFrame& rMaster,
                                                     const SwFlyFrame& rFollow)
{
    for(SwViewShell& rTmp : GetShell().GetRingContainer())
    {
        if( rTmp.Imp()->IsAccessible() )
            rTmp.Imp()->GetAccessibleMap().InvalidateRelationSet(rMaster, rFollow);
    }
}

/// invalidate CONTENT_FLOWS_FROM/_TO relation for paragraphs
void SwViewShellImp::InvalidateAccessibleParaFlowRelation_( const SwTextFrame* _pFromTextFrame,
                                                       const SwTextFrame* _pToTextFrame )
{
    if ( !_pFromTextFrame && !_pToTextFrame )
    {
        // No text frame provided. Thus, nothing to do.
        return;
    }

    for(SwViewShell& rTmp : GetShell().GetRingContainer())
    {
        if ( rTmp.Imp()->IsAccessible() )
        {
            if ( _pFromTextFrame )
            {
                rTmp.Imp()->GetAccessibleMap().
                            InvalidateParaFlowRelation( *_pFromTextFrame, true );
            }
            if ( _pToTextFrame )
            {
                rTmp.Imp()->GetAccessibleMap().
                            InvalidateParaFlowRelation( *_pToTextFrame, false );
            }
        }
    }
}

/// invalidate text selection for paragraphs
void SwViewShellImp::InvalidateAccessibleParaTextSelection_()
{
    for(SwViewShell& rTmp : GetShell().GetRingContainer())
    {
        if ( rTmp.Imp()->IsAccessible() )
        {
            rTmp.Imp()->GetAccessibleMap().InvalidateTextSelectionOfAllParas();
        }
    }
}

/// invalidate attributes for paragraphs
void SwViewShellImp::InvalidateAccessibleParaAttrs_( const SwTextFrame& rTextFrame )
{
    for(SwViewShell& rTmp : GetShell().GetRingContainer())
    {
        if ( rTmp.Imp()->IsAccessible() )
        {
            rTmp.Imp()->GetAccessibleMap().InvalidateAttr( rTextFrame );
        }
    }
}

void SwViewShellImp::UpdateAccessiblePreview( const std::vector<std::unique_ptr<PreviewPage>>& _rPreviewPages,
                                         const Fraction&  _rScale,
                                         const SwPageFrame* _pSelectedPageFrame,
                                         const Size&      _rPreviewWinSize )
{
    if( IsAccessible() )
        GetAccessibleMap().UpdatePreview( _rPreviewPages, _rScale,
                                          _pSelectedPageFrame, _rPreviewWinSize );
}

void SwViewShellImp::InvalidateAccessiblePreviewSelection( sal_uInt16 nSelPage )
{
    if( IsAccessible() )
        GetAccessibleMap().InvalidatePreviewSelection( nSelPage );
}

SwAccessibleMap *SwViewShellImp::CreateAccessibleMap()
{
    assert(!m_pAccessibleMap);
    m_pAccessibleMap = std::make_shared<SwAccessibleMap>(GetShell());
    return m_pAccessibleMap.get();
}

void SwViewShellImp::FireAccessibleEvents()
{
    if( IsAccessible() )
        GetAccessibleMap().FireEvents();
}
#endif // ENABLE_WASM_STRIP_ACCESSIBILITY

void SwViewObjectContactRedirector::createRedirectedPrimitive2DSequence(
                        const sdr::contact::ViewObjectContact& rOriginal,
                        const sdr::contact::DisplayInfo& rDisplayInfo,
                        drawinglayer::primitive2d::Primitive2DDecompositionVisitor& rVisitor)
{
    bool bPaint( true );

    SdrObject* pObj = rOriginal.GetViewContact().TryToGetSdrObject();
    if ( pObj )
    {
        bPaint = SwFlyFrame::IsPaint(pObj, mrViewShell);
    }

    if ( !bPaint )
    {
        return;
    }

    sdr::contact::ViewObjectContactRedirector::createRedirectedPrimitive2DSequence(
                                            rOriginal, rDisplayInfo, rVisitor );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
