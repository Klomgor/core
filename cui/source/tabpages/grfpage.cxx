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

#include <memory>
#include <svl/eitem.hxx>
#include <svl/stritem.hxx>
#include <dialmgr.hxx>
#include <svx/dlgutil.hxx>
#include <editeng/sizeitem.hxx>
#include <editeng/brushitem.hxx>
#include <grfpage.hxx>
#include <svx/grfcrop.hxx>
#include <rtl/ustring.hxx>
#include <tools/debug.hxx>
#include <tools/fract.hxx>
#include <svx/svxids.hrc>
#include <strings.hrc>
#include <vcl/fieldvalues.hxx>
#include <vcl/outdev.hxx>
#include <vcl/settings.hxx>
#include <vcl/svapp.hxx>
#include <svtools/unitconv.hxx>
#include <svtools/optionsdrawinglayer.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <o3tl/unit_conversion.hxx>
#include <officecfg/Office/Common.hxx>

constexpr auto CM_1_TO_TWIP = o3tl::convert(1, o3tl::Length::cm, o3tl::Length::twip); // 567


static int lcl_GetValue(const weld::MetricSpinButton& rMetric, FieldUnit eUnit)
{
    return rMetric.denormalize(rMetric.get_value(eUnit));
}

/*--------------------------------------------------------------------
    description: crop graphic
 --------------------------------------------------------------------*/

SvxGrfCropPage::SvxGrfCropPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet &rSet)
    : SfxTabPage(pPage, pController, u"cui/ui/croppage.ui"_ustr, u"CropPage"_ustr, &rSet)
    , m_nOldWidth(0)
    , m_nOldHeight(0)
    , m_bSetOrigSize(false)
    , m_aPreferredDPI(0)
    , m_xCropFrame(m_xBuilder->weld_widget(u"cropframe"_ustr))
    , m_xZoomConstRB(m_xBuilder->weld_radio_button(u"keepscale"_ustr))
    , m_xSizeConstRB(m_xBuilder->weld_radio_button(u"keepsize"_ustr))
    , m_xLeftMF(m_xBuilder->weld_metric_spin_button(u"left"_ustr, FieldUnit::CM))
    , m_xRightMF(m_xBuilder->weld_metric_spin_button(u"right"_ustr, FieldUnit::CM))
    , m_xTopMF(m_xBuilder->weld_metric_spin_button(u"top"_ustr, FieldUnit::CM))
    , m_xBottomMF(m_xBuilder->weld_metric_spin_button(u"bottom"_ustr, FieldUnit::CM))
    , m_xScaleFrame(m_xBuilder->weld_widget(u"scaleframe"_ustr))
    , m_xWidthZoomMF(m_xBuilder->weld_metric_spin_button(u"widthzoom"_ustr, FieldUnit::PERCENT))
    , m_xHeightZoomMF(m_xBuilder->weld_metric_spin_button(u"heightzoom"_ustr, FieldUnit::PERCENT))
    , m_xSizeFrame(m_xBuilder->weld_widget(u"sizeframe"_ustr))
    , m_xWidthMF(m_xBuilder->weld_metric_spin_button(u"width"_ustr, FieldUnit::CM))
    , m_xHeightMF(m_xBuilder->weld_metric_spin_button(u"height"_ustr, FieldUnit::CM))
    , m_xOrigSizeGrid(m_xBuilder->weld_widget(u"origsizegrid"_ustr))
    , m_xOrigSizeFT(m_xBuilder->weld_label(u"origsizeft"_ustr))
    , m_xOrigSizePB(m_xBuilder->weld_button(u"origsize"_ustr))
    , m_xUncropPB(m_xBuilder->weld_button(u"uncrop"_ustr))
    , m_xExampleWN(new weld::CustomWeld(*m_xBuilder, u"preview"_ustr, m_aExampleWN))
{
    SetExchangeSupport();

    // set the correct metric
    const FieldUnit eMetric = GetModuleFieldUnit( rSet );

    SetFieldUnit( *m_xWidthMF, eMetric );
    SetFieldUnit( *m_xHeightMF, eMetric );
    SetFieldUnit( *m_xLeftMF, eMetric );
    SetFieldUnit( *m_xRightMF, eMetric );
    SetFieldUnit( *m_xTopMF , eMetric );
    SetFieldUnit( *m_xBottomMF, eMetric );

    Link<weld::MetricSpinButton&,void> aLk = LINK(this, SvxGrfCropPage, SizeHdl);
    m_xWidthMF->connect_value_changed( aLk );
    m_xHeightMF->connect_value_changed( aLk );

    aLk = LINK(this, SvxGrfCropPage, ZoomHdl);
    m_xWidthZoomMF->connect_value_changed( aLk );
    m_xHeightZoomMF->connect_value_changed( aLk );

    aLk = LINK(this, SvxGrfCropPage, CropModifyHdl);
    m_xLeftMF->connect_value_changed( aLk );
    m_xRightMF->connect_value_changed( aLk );
    m_xTopMF->connect_value_changed( aLk );
    m_xBottomMF->connect_value_changed( aLk );

    m_xOrigSizePB->connect_clicked(LINK(this, SvxGrfCropPage, OrigSizeHdl));
    m_xUncropPB->connect_clicked(LINK(this, SvxGrfCropPage, UncropHdl));
}

SvxGrfCropPage::~SvxGrfCropPage()
{
    m_xExampleWN.reset();
}

std::unique_ptr<SfxTabPage> SvxGrfCropPage::Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet *rSet)
{
    return std::make_unique<SvxGrfCropPage>(pPage, pController, *rSet);
}

void SvxGrfCropPage::Reset( const SfxItemSet *rSet )
{
    const SfxPoolItem* pItem;
    const SfxItemPool& rPool = *rSet->GetPool();

    if(SfxItemState::SET == rSet->GetItemState( rPool.GetWhichIDFromSlotID(
                                    SID_ATTR_GRAF_KEEP_ZOOM ), true, &pItem ))
    {
        if( static_cast<const SfxBoolItem*>(pItem)->GetValue() )
            m_xZoomConstRB->set_active(true);
        else
            m_xSizeConstRB->set_active(true);
        m_xZoomConstRB->save_state();
    }

    sal_uInt16 nW = rPool.GetWhichIDFromSlotID( SID_ATTR_GRAF_CROP );
    if( SfxItemState::SET == rSet->GetItemState( nW, true, &pItem))
    {
        FieldUnit eUnit = MapToFieldUnit( rSet->GetPool()->GetMetric( nW ));

        const SvxGrfCrop* pCrop = static_cast<const SvxGrfCrop*>(pItem);

        m_aExampleWN.SetLeft(pCrop->GetLeft());
        m_aExampleWN.SetRight(pCrop->GetRight());
        m_aExampleWN.SetTop(pCrop->GetTop());
        m_aExampleWN.SetBottom(pCrop->GetBottom());

        m_xLeftMF->set_value( m_xLeftMF->normalize( pCrop->GetLeft()), eUnit );
        m_xRightMF->set_value( m_xRightMF->normalize( pCrop->GetRight()), eUnit );
        m_xTopMF->set_value( m_xTopMF->normalize( pCrop->GetTop()), eUnit );
        m_xBottomMF->set_value( m_xBottomMF->normalize( pCrop->GetBottom()), eUnit );
    }
    else
    {
        m_xLeftMF->set_value(0, FieldUnit::NONE);
        m_xRightMF->set_value(0, FieldUnit::NONE);
        m_xTopMF->set_value(0, FieldUnit::NONE);
        m_xBottomMF->set_value(0, FieldUnit::NONE);
    }

    m_xLeftMF->save_value();
    m_xRightMF->save_value();
    m_xTopMF->save_value();
    m_xBottomMF->save_value();

    nW = rPool.GetWhichIDFromSlotID( SID_ATTR_PAGE_SIZE );
    if ( SfxItemState::SET == rSet->GetItemState( nW, false, &pItem ) )
    {
        // orientation and size from the PageItem
        FieldUnit eUnit = MapToFieldUnit( rSet->GetPool()->GetMetric( nW ));

        m_aPageSize = static_cast<const SvxSizeItem*>(pItem)->GetSize();

        auto nMin = m_xWidthMF->normalize( 23 );
        auto nMax = m_xHeightMF->normalize(m_aPageSize.Height());
        m_xHeightMF->set_range(nMin, nMax, eUnit);
        nMax = m_xWidthMF->normalize(m_aPageSize.Width());
        m_xWidthMF->set_range(nMin, nMax, eUnit);
    }
    else
    {
        m_aPageSize = OutputDevice::LogicToLogic(
                        Size( CM_1_TO_TWIP,  CM_1_TO_TWIP ),
                        MapMode( MapUnit::MapTwip ),
                        MapMode( rSet->GetPool()->GetMetric( nW ) ) );
    }

    bool bFound = false;
    if( const SvxBrushItem* pGraphicItem = rSet->GetItemIfSet( SID_ATTR_GRAF_GRAPHIC, false ) )
    {
        OUString referer;
        SfxStringItem const * it = rSet->GetItem(SID_REFERER);
        if (it != nullptr) {
            referer = it->GetValue();
        }
        const Graphic* pGrf = pGraphicItem->GetGraphic(referer);
        if( pGrf )
        {
            m_aOrigSize = GetGrfOrigSize( *pGrf );
            if (pGrf->GetType() == GraphicType::Bitmap && m_aOrigSize.Width() && m_aOrigSize.Height())
            {
                m_aOrigPixelSize = pGrf->GetSizePixel();
            }

            if( m_aOrigSize.Width() && m_aOrigSize.Height() )
            {
                CalcMinMaxBorder();
                m_aExampleWN.SetGraphic( *pGrf );
                m_aExampleWN.SetFrameSize( m_aOrigSize );

                bFound = true;
                if( !pGraphicItem->GetGraphicLink().isEmpty() )
                    m_aGraphicName = pGraphicItem->GetGraphicLink();
            }
        }
    }

    GraphicHasChanged( bFound );
    ActivatePage( *rSet );
}

bool SvxGrfCropPage::FillItemSet(SfxItemSet *rSet)
{
    const SfxItemPool& rPool = *rSet->GetPool();
    bool bModified = false;
    if( m_xWidthMF->get_value_changed_from_saved() ||
        m_xHeightMF->get_value_changed_from_saved() )
    {
        constexpr TypedWhichId<SvxSizeItem> nW = SID_ATTR_GRAF_FRMSIZE;
        FieldUnit eUnit = MapToFieldUnit( rSet->GetPool()->GetMetric( nW ));

        std::shared_ptr<SvxSizeItem> aSz(std::make_shared<SvxSizeItem>(nW));

        // size could already have been set from another page
        const SfxItemSet* pExSet = GetDialogExampleSet();
        const SvxSizeItem* pSizeItem = nullptr;
        if( pExSet && (pSizeItem = pExSet->GetItemIfSet( nW, false )) )
        {
            aSz.reset(pSizeItem->Clone());
        }
        else
        {
            aSz.reset(GetItemSet().Get(nW).Clone());
        }

        Size aTmpSz( aSz->GetSize() );
        if( m_xWidthMF->get_value_changed_from_saved() )
            aTmpSz.setWidth( lcl_GetValue( *m_xWidthMF, eUnit ) );
        if( m_xHeightMF->get_value_changed_from_saved() )
            aTmpSz.setHeight( lcl_GetValue( *m_xHeightMF, eUnit ) );
        aSz->SetSize( aTmpSz );
        m_xWidthMF->save_value();
        m_xHeightMF->save_value();

        bModified |= nullptr != rSet->Put( *aSz );

        if (m_bSetOrigSize)
        {
            bModified |= nullptr != rSet->Put( SvxSizeItem( rPool.GetWhichIDFromSlotID(
                        SID_ATTR_GRAF_FRMSIZE_PERCENT ), Size( 0, 0 )) );
        }
    }
    if( m_xLeftMF->get_value_changed_from_saved() || m_xRightMF->get_value_changed_from_saved() ||
        m_xTopMF->get_value_changed_from_saved()  || m_xBottomMF->get_value_changed_from_saved() )
    {
        sal_uInt16 nW = rPool.GetWhichIDFromSlotID( SID_ATTR_GRAF_CROP );
        FieldUnit eUnit = MapToFieldUnit( rSet->GetPool()->GetMetric( nW ));
        std::unique_ptr<SvxGrfCrop> pNew(static_cast<SvxGrfCrop*>(rSet->Get( nW ).Clone()));

        pNew->SetLeft( lcl_GetValue( *m_xLeftMF, eUnit ) );
        pNew->SetRight( lcl_GetValue( *m_xRightMF, eUnit ) );
        pNew->SetTop( lcl_GetValue( *m_xTopMF, eUnit ) );
        pNew->SetBottom( lcl_GetValue( *m_xBottomMF, eUnit ) );
        bModified |= nullptr != rSet->Put( std::move(pNew) );
    }

    if( m_xZoomConstRB->get_state_changed_from_saved() )
    {
        bModified |= nullptr != rSet->Put( SfxBoolItem( rPool.GetWhichIDFromSlotID(
                    SID_ATTR_GRAF_KEEP_ZOOM), m_xZoomConstRB->get_active() ) );
    }

    return bModified;
}

void SvxGrfCropPage::ActivatePage(const SfxItemSet& rSet)
{
#ifdef DBG_UTIL
    SfxItemPool* pPool = GetItemSet().GetPool();
    DBG_ASSERT( pPool, "Where is the pool?" );
#endif

    auto& aProperties = getAdditionalProperties();
    auto aIterator = aProperties.find(u"PreferredDPI"_ustr);
    if (aIterator != aProperties.end())
        m_aPreferredDPI = aIterator->second.get<sal_Int32>();

    m_bSetOrigSize = false;

    // Size
    Size aSize;
    if( const SvxSizeItem* pFrmSizeItem = rSet.GetItemIfSet( SID_ATTR_GRAF_FRMSIZE, false ) )
        aSize = pFrmSizeItem->GetSize();

    m_nOldWidth = aSize.Width();
    m_nOldHeight = aSize.Height();

    auto nWidth = m_xWidthMF->normalize(m_nOldWidth);
    auto nHeight = m_xHeightMF->normalize(m_nOldHeight);

    if (nWidth != m_xWidthMF->get_value(FieldUnit::TWIP))
        m_xWidthMF->set_value(nWidth, FieldUnit::TWIP);
    m_xWidthMF->save_value();

    if (nHeight != m_xHeightMF->get_value(FieldUnit::TWIP))
        m_xHeightMF->set_value(nHeight, FieldUnit::TWIP);
    m_xHeightMF->save_value();

    if( const SvxBrushItem* pBrushItem = rSet.GetItemIfSet( SID_ATTR_GRAF_GRAPHIC, false ) )
    {
        if( !pBrushItem->GetGraphicLink().isEmpty() &&
            m_aGraphicName != pBrushItem->GetGraphicLink() )
            m_aGraphicName = pBrushItem->GetGraphicLink();

        OUString referer;
        SfxStringItem const * it = rSet.GetItem(SID_REFERER);
        if (it != nullptr) {
            referer = it->GetValue();
        }
        const Graphic* pGrf = pBrushItem->GetGraphic(referer);
        if( pGrf )
        {
            m_aExampleWN.SetGraphic( *pGrf );
            m_aOrigSize = GetGrfOrigSize( *pGrf );
            if (pGrf->GetType() == GraphicType::Bitmap && m_aOrigSize.Width() > 1 && m_aOrigSize.Height() > 1) {
                m_aOrigPixelSize = pGrf->GetSizePixel();
            }
            m_aExampleWN.SetFrameSize(m_aOrigSize);
            GraphicHasChanged( m_aOrigSize.Width() && m_aOrigSize.Height() );
            CalcMinMaxBorder();
        }
        else
            GraphicHasChanged( false );
    }

    CalcZoom();
}

DeactivateRC SvxGrfCropPage::DeactivatePage(SfxItemSet *_pSet)
{
    if ( _pSet )
        FillItemSet( _pSet );
    return DeactivateRC::LeavePage;
}

/*--------------------------------------------------------------------
    description: scale changed, adjust size
 --------------------------------------------------------------------*/

IMPL_LINK( SvxGrfCropPage, ZoomHdl, weld::MetricSpinButton&, rField, void )
{
    SfxItemPool* pPool = GetItemSet().GetPool();
    assert(pPool && "Where is the pool?");
    FieldUnit eUnit = MapToFieldUnit( pPool->GetMetric( pPool->GetWhichIDFromSlotID(
                                                    SID_ATTR_GRAF_CROP ) ) );

    if (&rField == m_xWidthZoomMF.get())
    {
        tools::Long nLRBorders = lcl_GetValue(*m_xLeftMF, eUnit)
                         +lcl_GetValue(*m_xRightMF, eUnit);
        m_xWidthMF->set_value( m_xWidthMF->normalize(
            ((m_aOrigSize.Width() - nLRBorders) * rField.get_value(FieldUnit::NONE))/100),
            eUnit);
    }
    else
    {
        tools::Long nULBorders = lcl_GetValue(*m_xTopMF, eUnit)
                         +lcl_GetValue(*m_xBottomMF, eUnit);
        m_xHeightMF->set_value( m_xHeightMF->normalize(
            ((m_aOrigSize.Height() - nULBorders ) * rField.get_value(FieldUnit::NONE))/100) ,
            eUnit );
    }
}

/*--------------------------------------------------------------------
    description: change size, adjust scale
 --------------------------------------------------------------------*/

IMPL_LINK( SvxGrfCropPage, SizeHdl, weld::MetricSpinButton&, rField, void )
{
    SfxItemPool* pPool = GetItemSet().GetPool();
    assert(pPool && "Where is the pool?");
    FieldUnit eUnit = MapToFieldUnit( pPool->GetMetric( pPool->GetWhichIDFromSlotID(
                                                    SID_ATTR_GRAF_CROP ) ) );

    Size aSize( lcl_GetValue(*m_xWidthMF, eUnit),
                lcl_GetValue(*m_xHeightMF, eUnit) );

    if(&rField == m_xWidthMF.get())
    {
        tools::Long nWidth = m_aOrigSize.Width() -
                ( lcl_GetValue(*m_xLeftMF, eUnit) +
                  lcl_GetValue(*m_xRightMF, eUnit) );
        if(!nWidth)
            nWidth++;
        sal_uInt16 nZoom = static_cast<sal_uInt16>( aSize.Width() * 100 / nWidth);
        m_xWidthZoomMF->set_value(nZoom, FieldUnit::NONE);
    }
    else
    {
        tools::Long nHeight = m_aOrigSize.Height() -
                ( lcl_GetValue(*m_xTopMF, eUnit) +
                  lcl_GetValue(*m_xBottomMF, eUnit));
        if(!nHeight)
            nHeight++;
        sal_uInt16 nZoom = static_cast<sal_uInt16>( aSize.Height() * 100 / nHeight);
        m_xHeightZoomMF->set_value(nZoom, FieldUnit::NONE);
    }
}

/*--------------------------------------------------------------------
    description: evaluate border
 --------------------------------------------------------------------*/

IMPL_LINK( SvxGrfCropPage, CropModifyHdl, weld::MetricSpinButton&, rField, void )
{
    SfxItemPool* pPool = GetItemSet().GetPool();
    assert(pPool && "Where is the pool?");
    FieldUnit eUnit = MapToFieldUnit( pPool->GetMetric( pPool->GetWhichIDFromSlotID(
                                                    SID_ATTR_GRAF_CROP ) ) );

    bool bZoom = m_xZoomConstRB->get_active();
    if (&rField == m_xLeftMF.get() || &rField == m_xRightMF.get())
    {
        tools::Long nLeft = lcl_GetValue( *m_xLeftMF, eUnit );
        tools::Long nRight = lcl_GetValue( *m_xRightMF, eUnit );
        tools::Long nWidthZoom = static_cast<tools::Long>(m_xWidthZoomMF->get_value(FieldUnit::NONE));
        if (bZoom && nWidthZoom != 0 && ( ( ( m_aOrigSize.Width() - (nLeft + nRight )) * nWidthZoom )
                            / 100 >= m_aPageSize.Width() ) )
        {
            if (&rField == m_xLeftMF.get())
            {
                nLeft = m_aOrigSize.Width() -
                            ( m_aPageSize.Width() * 100 / nWidthZoom + nRight );
                m_xLeftMF->set_value( m_xLeftMF->normalize( nLeft ), eUnit );
            }
            else
            {
                nRight = m_aOrigSize.Width() -
                            ( m_aPageSize.Width() * 100 / nWidthZoom + nLeft );
                m_xRightMF->set_value( m_xRightMF->normalize( nRight ), eUnit );
            }
        }
        if (AllSettings::GetLayoutRTL())
        {
            m_aExampleWN.SetLeft(nRight);
            m_aExampleWN.SetRight(nLeft);
        }
        else
        {
            m_aExampleWN.SetLeft(nLeft);
            m_aExampleWN.SetRight(nRight);
        }
        if(bZoom)
        {
            // scale stays, recompute width
            ZoomHdl(*m_xWidthZoomMF);
        }
    }
    else
    {
        tools::Long nTop = lcl_GetValue( *m_xTopMF, eUnit );
        tools::Long nBottom = lcl_GetValue( *m_xBottomMF, eUnit );
        tools::Long nHeightZoom = static_cast<tools::Long>(m_xHeightZoomMF->get_value(FieldUnit::NONE));
        if(bZoom && ( ( ( m_aOrigSize.Height() - (nTop + nBottom )) * nHeightZoom)
                                            / 100 >= m_aPageSize.Height()))
        {
            assert(nHeightZoom && "div-by-zero");
            if(&rField == m_xTopMF.get())
            {
                nTop = m_aOrigSize.Height() -
                            ( m_aPageSize.Height() * 100 / nHeightZoom + nBottom);
                m_xTopMF->set_value( m_xWidthMF->normalize( nTop ), eUnit );
            }
            else
            {
                nBottom = m_aOrigSize.Height() -
                            ( m_aPageSize.Height() * 100 / nHeightZoom + nTop);
                m_xBottomMF->set_value( m_xWidthMF->normalize( nBottom ), eUnit );
            }
        }
        m_aExampleWN.SetTop( nTop );
        m_aExampleWN.SetBottom( nBottom );
        if(bZoom)
        {
            // scale stays, recompute height
            ZoomHdl(*m_xHeightZoomMF);
        }
    }
    m_aExampleWN.Invalidate();
    // size and border changed -> recompute scale
    if(!bZoom)
        CalcZoom();
    CalcMinMaxBorder();
}
/*--------------------------------------------------------------------
    description: set original size
 --------------------------------------------------------------------*/

IMPL_LINK_NOARG(SvxGrfCropPage, OrigSizeHdl, weld::Button&, void)
{
    SfxItemPool* pPool = GetItemSet().GetPool();
    assert(pPool && "Where is the pool?");
    FieldUnit eUnit = MapToFieldUnit( pPool->GetMetric( pPool->GetWhichIDFromSlotID(
                                                    SID_ATTR_GRAF_CROP ) ) );

    tools::Long nWidth = m_aOrigSize.Width() -
        lcl_GetValue( *m_xLeftMF, eUnit ) -
        lcl_GetValue( *m_xRightMF, eUnit );
    m_xWidthMF->set_value( m_xWidthMF->normalize( nWidth ), eUnit );
    tools::Long nHeight = m_aOrigSize.Height() -
        lcl_GetValue( *m_xTopMF, eUnit ) -
        lcl_GetValue( *m_xBottomMF, eUnit );
    m_xHeightMF->set_value( m_xHeightMF->normalize( nHeight ), eUnit );
    m_xWidthZoomMF->set_value(100, FieldUnit::NONE);
    m_xHeightZoomMF->set_value(100, FieldUnit::NONE);
    m_bSetOrigSize = true;
}

/*--------------------------------------------------------------------
    description: reset crop
 --------------------------------------------------------------------*/

IMPL_LINK_NOARG(SvxGrfCropPage, UncropHdl, weld::Button&, void)
{
    SfxItemPool* pPool = GetItemSet().GetPool();
    DBG_ASSERT( pPool, "Where is the pool?" );

    m_xLeftMF->set_value(0, FieldUnit::NONE);
    m_xRightMF->set_value(0, FieldUnit::NONE);
    ZoomHdl(*m_xWidthZoomMF);
    m_xTopMF->set_value(0, FieldUnit::NONE);
    m_xBottomMF->set_value(0, FieldUnit::NONE);
    ZoomHdl(*m_xHeightZoomMF);

    m_aExampleWN.SetLeft(0);
    m_aExampleWN.SetRight(0);
    m_aExampleWN.SetTop(0);
    m_aExampleWN.SetBottom(0);

    m_aExampleWN.Invalidate();
    CalcMinMaxBorder();

}


/*--------------------------------------------------------------------
    description: compute scale
 --------------------------------------------------------------------*/

void SvxGrfCropPage::CalcZoom()
{
    SfxItemPool* pPool = GetItemSet().GetPool();
    assert(pPool && "Where is the pool?");
    FieldUnit eUnit = MapToFieldUnit( pPool->GetMetric( pPool->GetWhichIDFromSlotID(
                                                    SID_ATTR_GRAF_CROP ) ) );

    tools::Long nWidth = lcl_GetValue( *m_xWidthMF, eUnit );
    tools::Long nHeight = lcl_GetValue( *m_xHeightMF, eUnit );
    tools::Long nLRBorders = lcl_GetValue( *m_xLeftMF, eUnit ) +
                      lcl_GetValue( *m_xRightMF, eUnit );
    tools::Long nULBorders = lcl_GetValue( *m_xTopMF, eUnit ) +
                      lcl_GetValue( *m_xBottomMF, eUnit );
    sal_uInt16 nZoom = 0;
    tools::Long nDen;
    if( (nDen = m_aOrigSize.Width() - nLRBorders) > 0)
        nZoom = static_cast<sal_uInt16>((( nWidth  * 1000 / nDen )+5)/10);
    m_xWidthZoomMF->set_value(nZoom, FieldUnit::NONE);
    if( (nDen = m_aOrigSize.Height() - nULBorders) > 0)
        nZoom = static_cast<sal_uInt16>((( nHeight * 1000 / nDen )+5)/10);
    else
        nZoom = 0;
    m_xHeightZoomMF->set_value(nZoom, FieldUnit::NONE);
}

/*--------------------------------------------------------------------
    description: set minimum/maximum values for the margins
 --------------------------------------------------------------------*/

void SvxGrfCropPage::CalcMinMaxBorder()
{
    SfxItemPool* pPool = GetItemSet().GetPool();
    assert(pPool && "Where is the pool?");
    FieldUnit eUnit = MapToFieldUnit( pPool->GetMetric( pPool->GetWhichIDFromSlotID(
                                                    SID_ATTR_GRAF_CROP ) ) );
    tools::Long nR = lcl_GetValue(*m_xRightMF, eUnit );
    tools::Long nMinWidth = (m_aOrigSize.Width() * 10) /11;
    tools::Long nMin = nMinWidth - (nR >= 0 ? nR : 0);
    m_xLeftMF->set_max( m_xLeftMF->normalize(nMin), eUnit );

    tools::Long nL = lcl_GetValue(*m_xLeftMF, eUnit );
    nMin = nMinWidth - (nL >= 0 ? nL : 0);
    m_xRightMF->set_max( m_xRightMF->normalize(nMin), eUnit );

    tools::Long nUp  = lcl_GetValue( *m_xTopMF, eUnit );
    tools::Long nMinHeight = (m_aOrigSize.Height() * 10) /11;
    nMin = nMinHeight - (nUp >= 0 ? nUp : 0);
    m_xBottomMF->set_max( m_xBottomMF->normalize(nMin), eUnit );

    tools::Long nLow = lcl_GetValue(*m_xBottomMF, eUnit );
    nMin = nMinHeight - (nLow >= 0 ? nLow : 0);
    m_xTopMF->set_max( m_xTopMF->normalize(nMin), eUnit );
}
/*--------------------------------------------------------------------
    description:   set spinsize to 1/20 of the original size,
                   fill FixedText with the original size
 --------------------------------------------------------------------*/

void SvxGrfCropPage::GraphicHasChanged( bool bFound )
{
    if( bFound )
    {
        SfxItemPool* pPool = GetItemSet().GetPool();
        assert(pPool && "Where is the pool?");
        FieldUnit eUnit = MapToFieldUnit( pPool->GetMetric( pPool->GetWhichIDFromSlotID(
                                                    SID_ATTR_GRAF_CROP ) ));

        sal_Int64 nSpin = m_xLeftMF->normalize(m_aOrigSize.Width()) / 20;
        nSpin = vcl::ConvertValue( nSpin, m_aOrigSize.Width(), 0,
                                               eUnit, m_xLeftMF->get_unit());

        // if the margin is too big, it is set to 1/3 on both pages
        tools::Long nR = lcl_GetValue( *m_xRightMF, eUnit );
        tools::Long nL = lcl_GetValue( *m_xLeftMF, eUnit );
        if((nL + nR) < - m_aOrigSize.Width())
        {
            tools::Long nVal = m_aOrigSize.Width() / -3;
            m_xRightMF->set_value( m_xRightMF->normalize( nVal ), eUnit );
            m_xLeftMF->set_value( m_xLeftMF->normalize( nVal ), eUnit );
            m_aExampleWN.SetLeft(nVal);
            m_aExampleWN.SetRight(nVal);
        }
        tools::Long nUp  = lcl_GetValue(*m_xTopMF, eUnit );
        tools::Long nLow = lcl_GetValue(*m_xBottomMF, eUnit );
        if((nUp + nLow) < - m_aOrigSize.Height())
        {
            tools::Long nVal = m_aOrigSize.Height() / -3;
            m_xTopMF->set_value( m_xTopMF->normalize( nVal ), eUnit );
            m_xBottomMF->set_value( m_xBottomMF->normalize( nVal ), eUnit );
            m_aExampleWN.SetTop(nVal);
            m_aExampleWN.SetBottom(nVal);
        }

        m_xLeftMF->set_increments(nSpin, nSpin * 10, FieldUnit::NONE);
        m_xRightMF->set_increments(nSpin, nSpin * 10, FieldUnit::NONE);
        nSpin = m_xTopMF->normalize(m_aOrigSize.Height()) / 20;
        nSpin = vcl::ConvertValue( nSpin, m_aOrigSize.Width(), 0,
                                               eUnit, m_xLeftMF->get_unit() );
        m_xTopMF->set_increments(nSpin, nSpin * 10, FieldUnit::NONE);
        m_xBottomMF->set_increments(nSpin, nSpin * 10, FieldUnit::NONE);

        // display original size
        const FieldUnit eMetric = GetModuleFieldUnit( GetItemSet() );

        OUString sTemp;
        {
            std::unique_ptr<weld::Builder> xBuilder(Application::CreateBuilder(GetFrameWeld(), u"cui/ui/spinbox.ui"_ustr));
            std::unique_ptr<weld::Dialog> xTopLevel(xBuilder->weld_dialog(u"SpinDialog"_ustr));
            std::unique_ptr<weld::MetricSpinButton> xFld(xBuilder->weld_metric_spin_button(u"spin"_ustr, FieldUnit::CM));
            SetFieldUnit( *xFld, eMetric );
            xFld->set_digits(m_xWidthMF->get_digits());
            xFld->set_max(INT_MAX - 1, FieldUnit::NONE);

            xFld->set_value(xFld->normalize(m_aOrigSize.Width()), eUnit);
            sTemp = xFld->get_text();
            xFld->set_value(xFld->normalize(m_aOrigSize.Height()), eUnit);
            // multiplication sign (U+00D7)
            sTemp += u"\u00D7" + xFld->get_text();
        }

        if ( m_aOrigPixelSize.Width() && m_aOrigPixelSize.Height() ) {
             sal_Int32 ax = 0.5 + m_aOrigPixelSize.Width() /
                 o3tl::convert<double>(m_aOrigSize.Width(), o3tl::Length::twip,
                                                          o3tl::Length::in);
             sal_Int32 ay = 0.5 + m_aOrigPixelSize.Height() /
                 o3tl::convert<double>(m_aOrigSize.Height(), o3tl::Length::twip,
                                                           o3tl::Length::in);
             OUString sPPI = OUString::number(ax);
             if (abs(ax - ay) > 1) {
                sPPI += u"\u00D7" + OUString::number(ay);
             }
             sTemp += " " + CuiResId(RID_CUISTR_PPI).replaceAll("%1", sPPI);
        }
        sTemp += "\n" + OUString::number(m_aOrigPixelSize.Width()) + u"\u00D7" + OUString::number(m_aOrigPixelSize.Height()) + " px";
        m_xOrigSizeFT->set_label(sTemp);
    }

    m_xCropFrame->set_sensitive(bFound);
    m_xScaleFrame->set_sensitive(bFound);
    m_xSizeFrame->set_sensitive(bFound);
    m_xOrigSizeGrid->set_sensitive(bFound);
    m_xZoomConstRB->set_sensitive(bFound);
}

Size SvxGrfCropPage::GetGrfOrigSize(const Graphic& rGrf)
{
    Size aSize;

    if (m_aPreferredDPI > 0)
    {
        Size aPixelSize = rGrf.GetSizePixel();
        double fWidth = aPixelSize.Width() / double(m_aPreferredDPI);
        double fHeight = aPixelSize.Height() / double(m_aPreferredDPI);
        fWidth = o3tl::convert(fWidth, o3tl::Length::in, o3tl::Length::twip);
        fHeight = o3tl::convert(fHeight, o3tl::Length::in, o3tl::Length::twip);
        aSize = Size(fWidth, fHeight);
    }
    else
    {
        const MapMode aMapTwip( MapUnit::MapTwip );
        aSize = rGrf.GetPrefSize();
        if( MapUnit::MapPixel == rGrf.GetPrefMapMode().GetMapUnit() )
            aSize = Application::GetDefaultDevice()->PixelToLogic(aSize, aMapTwip);
        else
            aSize = OutputDevice::LogicToLogic( aSize,
                                            rGrf.GetPrefMapMode(), aMapTwip );
    }
    return aSize;
}

/*****************************************************************/

SvxCropExample::SvxCropExample()
    : m_aTopLeft(0, 0)
    , m_aBottomRight(0, 0)
{
}

void SvxCropExample::SetDrawingArea(weld::DrawingArea* pDrawingArea)
{
    CustomWidgetController::SetDrawingArea(pDrawingArea);
    OutputDevice& rDevice = pDrawingArea->get_ref_device();
    Size aSize(rDevice.LogicToPixel(Size(78, 78), MapMode(MapUnit::MapAppFont)));
    pDrawingArea->set_size_request(aSize.Width(), aSize.Height());

    m_aMapMode = rDevice.GetMapMode();
    m_aFrameSize = OutputDevice::LogicToLogic(
                            Size(CM_1_TO_TWIP / 2, CM_1_TO_TWIP / 2),
                            MapMode(MapUnit::MapTwip), m_aMapMode);
}

void SvxCropExample::Paint(vcl::RenderContext& rRenderContext, const ::tools::Rectangle&)
{
    rRenderContext.Push(vcl::PushFlags::MAPMODE);
    rRenderContext.SetMapMode(m_aMapMode);

    // Win BG
    const Size aWinSize(rRenderContext.PixelToLogic(GetOutputSizePixel()));
    rRenderContext.SetLineColor();
    rRenderContext.SetFillColor(rRenderContext.GetSettings().GetStyleSettings().GetWindowColor());
    rRenderContext.DrawRect(::tools::Rectangle(Point(), aWinSize));

    // use AA, the Graphic may be a metafile/svg and would then look ugly
    rRenderContext.SetAntialiasing(AntialiasingFlags::Enable);

    // draw Graphic
    ::tools::Rectangle aRect(
        Point((aWinSize.Width() - m_aFrameSize.Width())/2, (aWinSize.Height() - m_aFrameSize.Height())/2),
        m_aFrameSize);
    m_aGrf.Draw(rRenderContext, aRect.TopLeft(), aRect.GetSize());

    // Remove one more case that uses XOR paint (RasterOp::Invert).
    // Get colors and logic DashLength from settings, use equal to
    // PolygonMarkerPrimitive2D, may be changed to that primitive later.
    // Use this to guarantee good visibility - that was the purpose of
    // the former used XOR paint.
    const Color aColA(SvtOptionsDrawinglayer::GetStripeColorA().getBColor());
    const Color aColB(SvtOptionsDrawinglayer::GetStripeColorB().getBColor());
    const double fStripeLength(officecfg::Office::Common::Drawinglayer::StripeLength::get());
    const basegfx::B2DVector aDashVector(rRenderContext.GetInverseViewTransformation() * basegfx::B2DVector(fStripeLength, 0.0));
    const double fLogicDashLength(aDashVector.getX());

    // apply current crop settings
    aRect.AdjustLeft(m_aTopLeft.Y());
    aRect.AdjustTop(m_aTopLeft.X());
    aRect.AdjustRight(-m_aBottomRight.Y());
    aRect.AdjustBottom(-m_aBottomRight.X());

    // apply dash with direct paint callbacks
    basegfx::utils::applyLineDashing(
        basegfx::utils::createPolygonFromRect(
            basegfx::B2DRange(aRect.Left(), aRect.Top(), aRect.Right(), aRect.Bottom())),
        std::vector< double >(2, fLogicDashLength),
        [&aColA,&rRenderContext](const basegfx::B2DPolygon& rSnippet)
        {
            rRenderContext.SetLineColor(aColA);
            rRenderContext.DrawPolyLine(rSnippet);
        },
        [&aColB,&rRenderContext](const basegfx::B2DPolygon& rSnippet)
        {
            rRenderContext.SetLineColor(aColB);
            rRenderContext.DrawPolyLine(rSnippet);
        },
        2.0 * fLogicDashLength);

    rRenderContext.Pop();
}

void SvxCropExample::Resize()
{
    SetFrameSize(m_aFrameSize);
}

void SvxCropExample::SetFrameSize( const Size& rSz )
{
    m_aFrameSize = rSz;
    if (!m_aFrameSize.Width())
        m_aFrameSize.setWidth( 1 );
    if (!m_aFrameSize.Height())
        m_aFrameSize.setHeight( 1 );
    Size aWinSize( GetOutputSizePixel() );
    Fraction aXScale( aWinSize.Width() * 4, m_aFrameSize.Width() * 5 );
    Fraction aYScale( aWinSize.Height() * 4, m_aFrameSize.Height() * 5 );

    if( aYScale < aXScale )
        aXScale = aYScale;

    m_aMapMode.SetScaleX(aXScale);
    m_aMapMode.SetScaleY(aXScale);

    Invalidate();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
