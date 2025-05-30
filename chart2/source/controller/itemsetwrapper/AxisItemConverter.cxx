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

#include <AxisItemConverter.hxx>
#include <ItemPropertyMap.hxx>
#include <CharacterPropertyItemConverter.hxx>
#include <GraphicPropertyItemConverter.hxx>
#include <chartview/ChartSfxItemIds.hxx>
#include <chartview/ExplicitScaleValues.hxx>
#include "SchWhichPairs.hxx"
#include <ChartModel.hxx>
#include <Axis.hxx>
#include <AxisHelper.hxx>
#include <CommonConverters.hxx>
#include <ChartType.hxx>
#include <ChartTypeHelper.hxx>
#include <Diagram.hxx>
#include <unonames.hxx>
#include <BaseCoordinateSystem.hxx>
#include <ChartView.hxx>
#include <memory>

#include <com/sun/star/chart/ChartAxisLabelPosition.hpp>
#include <com/sun/star/chart/ChartAxisMarkPosition.hpp>
#include <com/sun/star/chart/ChartAxisPosition.hpp>
#include <com/sun/star/chart/TimeInterval.hpp>
#include <com/sun/star/chart2/XAxis.hpp>
#include <com/sun/star/chart2/AxisOrientation.hpp>
#include <com/sun/star/chart2/AxisType.hpp>

#include <osl/diagnose.h>
#include <o3tl/any.hxx>
#include <svl/eitem.hxx>
#include <svx/chrtitem.hxx>
#include <svx/sdangitm.hxx>
#include <svl/intitem.hxx>
#include <rtl/math.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::chart2;
using ::com::sun::star::uno::Reference;
using ::com::sun::star::chart::TimeInterval;
using ::com::sun::star::chart::TimeIncrement;

namespace chart::wrapper {

namespace {

ItemPropertyMapType & lcl_GetAxisPropertyMap()
{
    static ItemPropertyMapType aAxisPropertyMap{
        {SCHATTR_AXIS_SHOWDESCR,     {"DisplayLabels",    0}},
        {SCHATTR_AXIS_TICKS,         {"MajorTickmarks",   0}},
        {SCHATTR_AXIS_HELPTICKS,     {"MinorTickmarks",   0}},
        {SCHATTR_AXIS_LABEL_ORDER,   {"ArrangeOrder",     0}},
        {SCHATTR_TEXT_STACKED,       {"StackCharacters",  0}},
        {SCHATTR_AXIS_LABEL_BREAK,   {"TextBreak",        0}},
        {SCHATTR_AXIS_LABEL_OVERLAP, {"TextOverlap",      0}}};
    return aAxisPropertyMap;
};

} // anonymous namespace

AxisItemConverter::AxisItemConverter(
    const Reference< beans::XPropertySet > & rPropertySet,
    SfxItemPool& rItemPool,
    SdrModel& rDrawModel,
    const rtl::Reference<::chart::ChartModel> & xChartDoc,
    ::chart::ExplicitScaleData const * pScale /* = NULL */,
    ::chart::ExplicitIncrementData const * pIncrement /* = NULL */,
    const std::optional<awt::Size>& pRefSize ) :
        ItemConverter( rPropertySet, rItemPool ),
        m_xChartDoc( xChartDoc )
{
    if( pScale )
        m_pExplicitScale.reset( new ::chart::ExplicitScaleData( *pScale ) );
    if( pIncrement )
        m_pExplicitIncrement.reset( new ::chart::ExplicitIncrementData( *pIncrement ) );

    m_aConverters.emplace_back( new GraphicPropertyItemConverter(
                                 rPropertySet, rItemPool, rDrawModel,
                                 xChartDoc,
                                 GraphicObjectType::LineProperties ));
    m_aConverters.emplace_back(
        new CharacterPropertyItemConverter(rPropertySet, rItemPool, pRefSize, u"ReferencePageSize"_ustr));

    m_xAxis = dynamic_cast<::chart::Axis*>(rPropertySet.get());
    assert(m_xAxis);
}

AxisItemConverter::~AxisItemConverter()
{
}

void AxisItemConverter::FillItemSet( SfxItemSet & rOutItemSet ) const
{
    for( const auto& pConv : m_aConverters )
        pConv->FillItemSet( rOutItemSet );

    // own items
    ItemConverter::FillItemSet( rOutItemSet );
}

bool AxisItemConverter::ApplyItemSet( const SfxItemSet & rItemSet )
{
    bool bResult = false;

    for( const auto& pConv : m_aConverters )
        bResult = pConv->ApplyItemSet( rItemSet ) || bResult;

    // own items
    return ItemConverter::ApplyItemSet( rItemSet ) || bResult;
}

const WhichRangesContainer& AxisItemConverter::GetWhichPairs() const
{
    // must span all used items!
    return nAxisWhichPairs;
}

bool AxisItemConverter::GetItemProperty( tWhichIdType nWhichId, tPropertyNameWithMemberId & rOutProperty ) const
{
    ItemPropertyMapType & rMap( lcl_GetAxisPropertyMap());
    ItemPropertyMapType::const_iterator aIt( rMap.find( nWhichId ));

    if( aIt == rMap.end())
        return false;

    rOutProperty =(*aIt).second;

    return true;
}

static bool lcl_hasTimeIntervalValue( const uno::Any& rAny )
{
    bool bRet = false;
    TimeInterval aValue;
    if( rAny >>= aValue )
        bRet = true;
    return bRet;
}

void AxisItemConverter::FillSpecialItem( sal_uInt16 nWhichId, SfxItemSet & rOutItemSet ) const
{
    if( !m_xAxis.is() )
        return;

    const chart2::ScaleData      aScale( m_xAxis->getScaleData() );
    const chart2::IncrementData& rIncrement( aScale.IncrementData );
    const uno::Sequence< chart2::SubIncrement >& rSubIncrements( aScale.IncrementData.SubIncrements );
    const TimeIncrement& rTimeIncrement( aScale.TimeIncrement );
    bool bDateAxis = (aScale.AxisType == chart2::AxisType::DATE);
    if( m_pExplicitScale )
        bDateAxis = (m_pExplicitScale->AxisType == chart2::AxisType::DATE);

    switch( nWhichId )
    {
        case SCHATTR_AXIS_AUTO_MAX:
                rOutItemSet.Put( SfxBoolItem( nWhichId, !hasDoubleValue(aScale.Maximum) ) );
            break;

        case SCHATTR_AXIS_MAX:
            {
                double fMax = 10.0;
                if( aScale.Maximum >>= fMax )
                    rOutItemSet.Put( SvxDoubleItem( fMax, SCHATTR_AXIS_MAX ) );
                else
                {
                    if( m_pExplicitScale )
                        fMax = m_pExplicitScale->Maximum;
                    rOutItemSet.Put( SvxDoubleItem( fMax, SCHATTR_AXIS_MAX ) );
                }
            }
            break;

        case SCHATTR_AXIS_AUTO_MIN:
                rOutItemSet.Put( SfxBoolItem( nWhichId, !hasDoubleValue(aScale.Minimum) ) );
            break;

        case SCHATTR_AXIS_MIN:
            {
                double fMin = 0.0;
                if( aScale.Minimum >>= fMin )
                    rOutItemSet.Put( SvxDoubleItem( fMin, SCHATTR_AXIS_MIN ) );
                else if( m_pExplicitScale )
                    rOutItemSet.Put( SvxDoubleItem( m_pExplicitScale->Minimum, SCHATTR_AXIS_MIN ));
            }
            break;

        case SCHATTR_AXIS_LOGARITHM:
            {
                bool bValue = AxisHelper::isLogarithmic( aScale.Scaling );
                rOutItemSet.Put( SfxBoolItem( nWhichId, bValue ));
            }
            break;

        case SCHATTR_AXIS_REVERSE:
                rOutItemSet.Put( SfxBoolItem( nWhichId, (aScale.Orientation == AxisOrientation_REVERSE) ));
            break;

        // Increment
        case SCHATTR_AXIS_AUTO_STEP_MAIN:
            if( bDateAxis )
                rOutItemSet.Put( SfxBoolItem( nWhichId, !lcl_hasTimeIntervalValue(rTimeIncrement.MajorTimeInterval) ) );
            else
                rOutItemSet.Put( SfxBoolItem( nWhichId, !hasDoubleValue(rIncrement.Distance) ) );
            break;

        case SCHATTR_AXIS_MAIN_TIME_UNIT:
            {
                TimeInterval aTimeInterval;
                if( rTimeIncrement.MajorTimeInterval >>= aTimeInterval )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, aTimeInterval.TimeUnit ) );
                else if( m_pExplicitIncrement )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, m_pExplicitIncrement->MajorTimeInterval.TimeUnit ) );
            }
            break;

        case SCHATTR_AXIS_STEP_MAIN:
            if( bDateAxis )
            {
                TimeInterval aTimeInterval;
                if( rTimeIncrement.MajorTimeInterval >>= aTimeInterval )
                    rOutItemSet.Put( SvxDoubleItem(aTimeInterval.Number, SCHATTR_AXIS_STEP_MAIN ));
                else if( m_pExplicitIncrement )
                    rOutItemSet.Put( SvxDoubleItem( m_pExplicitIncrement->MajorTimeInterval.Number, SCHATTR_AXIS_STEP_MAIN ));
            }
            else
            {
                double fDistance = 1.0;
                if( rIncrement.Distance >>= fDistance )
                    rOutItemSet.Put( SvxDoubleItem(fDistance, SCHATTR_AXIS_STEP_MAIN ));
                else if( m_pExplicitIncrement )
                    rOutItemSet.Put( SvxDoubleItem( m_pExplicitIncrement->Distance, SCHATTR_AXIS_STEP_MAIN ));
            }
            break;

        // SubIncrement
        case SCHATTR_AXIS_AUTO_STEP_HELP:
            if( bDateAxis )
                rOutItemSet.Put( SfxBoolItem( nWhichId, !lcl_hasTimeIntervalValue(rTimeIncrement.MinorTimeInterval) ) );
            else
                rOutItemSet.Put( SfxBoolItem( nWhichId,
                    ! ( rSubIncrements.hasElements() && rSubIncrements[0].IntervalCount.hasValue() )));
            break;

        case SCHATTR_AXIS_HELP_TIME_UNIT:
            {
                TimeInterval aTimeInterval;
                if( rTimeIncrement.MinorTimeInterval >>= aTimeInterval )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, aTimeInterval.TimeUnit ) );
                else if( m_pExplicitIncrement )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, m_pExplicitIncrement->MinorTimeInterval.TimeUnit ) );
            }
            break;

        case SCHATTR_AXIS_STEP_HELP:
            if( bDateAxis )
            {
                TimeInterval aTimeInterval;
                if( rTimeIncrement.MinorTimeInterval >>= aTimeInterval )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, aTimeInterval.Number ));
                else if( m_pExplicitIncrement )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, m_pExplicitIncrement->MinorTimeInterval.Number ));
            }
            else
            {
                if( rSubIncrements.hasElements() && rSubIncrements[0].IntervalCount.hasValue())
                {
                    rOutItemSet.Put( SfxInt32Item( nWhichId,
                            *o3tl::doAccess<sal_Int32>(
                                rSubIncrements[0].IntervalCount) ));
                }
                else
                {
                    if( m_pExplicitIncrement && !m_pExplicitIncrement->SubIncrements.empty() )
                    {
                        rOutItemSet.Put( SfxInt32Item( nWhichId,
                                m_pExplicitIncrement->SubIncrements[0].IntervalCount ));
                    }
                }
            }
            break;

        case SCHATTR_AXIS_AUTO_TIME_RESOLUTION:
            {
                rOutItemSet.Put( SfxBoolItem( nWhichId,
                        !rTimeIncrement.TimeResolution.hasValue() ));
            }
            break;
        case SCHATTR_AXIS_TIME_RESOLUTION:
            {
                sal_Int32 nTimeResolution=0;
                if( rTimeIncrement.TimeResolution >>= nTimeResolution )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, nTimeResolution ) );
                else if( m_pExplicitScale )
                    rOutItemSet.Put( SfxInt32Item( nWhichId, m_pExplicitScale->TimeResolution ) );
            }
            break;

        case SCHATTR_AXIS_AUTO_ORIGIN:
        {
            rOutItemSet.Put( SfxBoolItem( nWhichId, ( !hasDoubleValue(aScale.Origin) )));
        }
        break;

        case SCHATTR_AXIS_ORIGIN:
        {
            double fOrigin = 0.0;
            if( !(aScale.Origin >>= fOrigin) )
            {
                if( m_pExplicitScale )
                    fOrigin = m_pExplicitScale->Origin;
            }
            rOutItemSet.Put( SvxDoubleItem( fOrigin, SCHATTR_AXIS_ORIGIN ));
        }
        break;

        case SCHATTR_AXIS_POSITION:
        {
            css::chart::ChartAxisPosition eAxisPos( css::chart::ChartAxisPosition_ZERO );
            GetPropertySet()->getPropertyValue( u"CrossoverPosition"_ustr ) >>= eAxisPos;
            rOutItemSet.Put( SfxInt32Item( nWhichId, static_cast<sal_Int32>(eAxisPos) ) );
        }
        break;

        case SCHATTR_AXIS_POSITION_VALUE:
        {
            double fValue = 0.0;
            if( GetPropertySet()->getPropertyValue( u"CrossoverValue"_ustr ) >>= fValue )
                rOutItemSet.Put( SvxDoubleItem( fValue, SCHATTR_AXIS_POSITION_VALUE ) );
        }
        break;

        case SCHATTR_AXIS_CROSSING_MAIN_AXIS_NUMBERFORMAT:
        {
            //read only item
            //necessary tp display the crossing value with an appropriate format

            rtl::Reference< BaseCoordinateSystem > xCooSys( AxisHelper::getCoordinateSystemOfAxis(
                m_xAxis, m_xChartDoc->getFirstChartDiagram() ) );

            rtl::Reference< Axis > xCrossingMainAxis = AxisHelper::getCrossingMainAxis( m_xAxis, xCooSys );

            sal_Int32 nFormatKey = ChartView::getExplicitNumberFormatKeyForAxis(
                xCrossingMainAxis, xCooSys, m_xChartDoc);

            rOutItemSet.Put( SfxUInt32Item( nWhichId, nFormatKey ));
        }
        break;

        case SCHATTR_AXIS_SHIFTED_CATEGORY_POSITION:
            rOutItemSet.Put(SfxBoolItem(nWhichId, aScale.ShiftedCategoryPosition));
        break;

        case SCHATTR_AXIS_LABEL_POSITION:
        {
            css::chart::ChartAxisLabelPosition ePos( css::chart::ChartAxisLabelPosition_NEAR_AXIS );
            GetPropertySet()->getPropertyValue( u"LabelPosition"_ustr ) >>= ePos;
            rOutItemSet.Put( SfxInt32Item( nWhichId, static_cast<sal_Int32>(ePos) ) );
        }
        break;

        case SCHATTR_AXIS_MARK_POSITION:
        {
            css::chart::ChartAxisMarkPosition ePos( css::chart::ChartAxisMarkPosition_AT_LABELS_AND_AXIS );
            GetPropertySet()->getPropertyValue( u"MarkPosition"_ustr ) >>= ePos;
            rOutItemSet.Put( SfxInt32Item( nWhichId, static_cast<sal_Int32>(ePos) ) );
        }
        break;

        case SCHATTR_TEXT_DEGREES:
        {
            // convert double to int (times 100)
            double fVal = 0;

            if( GetPropertySet()->getPropertyValue( u"TextRotation"_ustr ) >>= fVal )
            {
                rOutItemSet.Put( SdrAngleItem( SCHATTR_TEXT_DEGREES, Degree100(static_cast< sal_Int32 >(
                                                   ::rtl::math::round( fVal * 100.0 )) ) ));
            }
        }
        break;

        case SID_ATTR_NUMBERFORMAT_VALUE:
        {
            if( m_pExplicitScale )
            {
                rtl::Reference< BaseCoordinateSystem > xCooSys(
                        AxisHelper::getCoordinateSystemOfAxis(
                              m_xAxis, m_xChartDoc->getFirstChartDiagram() ) );

                sal_Int32 nFormatKey = ChartView::getExplicitNumberFormatKeyForAxis(
                    m_xAxis, xCooSys, m_xChartDoc);

                rOutItemSet.Put( SfxUInt32Item( nWhichId, nFormatKey ));
            }
        }
        break;

        case SID_ATTR_NUMBERFORMAT_SOURCE:
        {
            bool bLinkToSource = true;
            GetPropertySet()->getPropertyValue(CHART_UNONAME_LINK_TO_SRC_NUMFMT) >>= bLinkToSource;
            rOutItemSet.Put(SfxBoolItem(nWhichId, bLinkToSource));
        }
        break;

        case SCHATTR_AXISTYPE:
            rOutItemSet.Put( SfxInt32Item( nWhichId, aScale.AxisType ));
        break;

        case SCHATTR_AXIS_AUTO_DATEAXIS:
            rOutItemSet.Put( SfxBoolItem( nWhichId, aScale.AutoDateAxis ));
        break;

        case SCHATTR_AXIS_ALLOW_DATEAXIS:
        {
            rtl::Reference< BaseCoordinateSystem > xCooSys(
                AxisHelper::getCoordinateSystemOfAxis( m_xAxis, m_xChartDoc->getFirstChartDiagram() ) );
            sal_Int32 nDimensionIndex=0; sal_Int32 nAxisIndex=0;
            AxisHelper::getIndicesForAxis(m_xAxis, xCooSys, nDimensionIndex, nAxisIndex );
            auto xChartType = AxisHelper::getChartTypeByIndex(xCooSys, 0);
            bool bChartTypeAllowsDateAxis = xChartType.is() ? xChartType->isSupportingDateAxis(nDimensionIndex) : true;
            rOutItemSet.Put( SfxBoolItem( nWhichId, bChartTypeAllowsDateAxis ));
        }
        break;
    }
}

static bool lcl_isDateAxis( const SfxItemSet & rItemSet )
{
    sal_Int32 nAxisType = rItemSet.Get( SCHATTR_AXISTYPE ).GetValue();//css::chart2::AxisType
    return (nAxisType == chart2::AxisType::DATE);
}

static bool lcl_isAutoMajor( const SfxItemSet & rItemSet )
{
    bool bRet = rItemSet.Get( SCHATTR_AXIS_AUTO_STEP_MAIN ).GetValue();
    return bRet;
}

static bool lcl_isAutoMinor( const SfxItemSet & rItemSet )
{
    bool bRet = rItemSet.Get( SCHATTR_AXIS_AUTO_STEP_HELP ).GetValue();
    return bRet;
}

bool AxisItemConverter::ApplySpecialItem( sal_uInt16 nWhichId, const SfxItemSet & rItemSet )
{
    if( !m_xAxis.is() )
        return false;

    chart2::ScaleData     aScale( m_xAxis->getScaleData() );

    bool bSetScale    = false;
    bool bChangedOtherwise = false;

    uno::Any aValue;

    switch( nWhichId )
    {
        case SCHATTR_AXIS_AUTO_MAX:
            if( static_cast< const SfxBoolItem & >(rItemSet.Get( nWhichId )).GetValue() )
            {
                aScale.Maximum.clear();
                bSetScale = true;
            }
            // else SCHATTR_AXIS_MAX must have some value
            break;

        case SCHATTR_AXIS_MAX:
            // only if auto if false
            if( ! (rItemSet.Get( SCHATTR_AXIS_AUTO_MAX ).GetValue() ))
            {
                rItemSet.Get( nWhichId ).QueryValue( aValue );

                if( aScale.Maximum != aValue )
                {
                    aScale.Maximum = aValue;
                    bSetScale = true;
                }
            }
            break;

        case SCHATTR_AXIS_AUTO_MIN:
            if( static_cast< const SfxBoolItem & >(rItemSet.Get( nWhichId )).GetValue() )
            {
                aScale.Minimum.clear();
                bSetScale = true;
            }
            // else SCHATTR_AXIS_MIN must have some value
            break;

        case SCHATTR_AXIS_MIN:
            // only if auto if false
            if( ! (rItemSet.Get( SCHATTR_AXIS_AUTO_MIN ).GetValue() ))
            {
                rItemSet.Get( nWhichId ).QueryValue( aValue );

                if( aScale.Minimum != aValue )
                {
                    aScale.Minimum = aValue;
                    bSetScale = true;
                }
            }
            break;

        case SCHATTR_AXIS_LOGARITHM:
        {
            bool bWasLogarithm = AxisHelper::isLogarithmic( aScale.Scaling );

            if( static_cast< const SfxBoolItem & >(rItemSet.Get( nWhichId )).GetValue() )
            {
                // logarithm is true
                if( ! bWasLogarithm )
                {
                    aScale.Scaling = AxisHelper::createLogarithmicScaling( 10.0 );
                    bSetScale = true;
                }
            }
            else
            {
                // logarithm is false => linear scaling
                if( bWasLogarithm )
                {
                    aScale.Scaling = AxisHelper::createLinearScaling();
                    bSetScale = true;
                }
            }
        }
        break;

        case SCHATTR_AXIS_REVERSE:
        {
            bool bWasReverse = ( aScale.Orientation == AxisOrientation_REVERSE );
            bool bNewReverse = static_cast< const SfxBoolItem & >(
                     rItemSet.Get( nWhichId )).GetValue();
            if( bWasReverse != bNewReverse )
            {
                aScale.Orientation = bNewReverse ? AxisOrientation_REVERSE : AxisOrientation_MATHEMATICAL;
                bSetScale = true;
            }
        }
        break;

        // Increment
        case SCHATTR_AXIS_AUTO_STEP_MAIN:
            if( lcl_isAutoMajor(rItemSet) )
            {
                aScale.IncrementData.Distance.clear();
                aScale.TimeIncrement.MajorTimeInterval.clear();
                bSetScale = true;
            }
            // else SCHATTR_AXIS_STEP_MAIN must have some value
            break;

        case SCHATTR_AXIS_MAIN_TIME_UNIT:
            if( !lcl_isAutoMajor(rItemSet) )
            {
                if( rItemSet.Get( nWhichId ).QueryValue( aValue ) )
                {
                    TimeInterval aTimeInterval;
                    aScale.TimeIncrement.MajorTimeInterval >>= aTimeInterval;
                    aValue >>= aTimeInterval.TimeUnit;
                    aScale.TimeIncrement.MajorTimeInterval <<= aTimeInterval;
                    bSetScale = true;
                }
            }
            break;

        case SCHATTR_AXIS_STEP_MAIN:
            // only if auto if false
            if( !lcl_isAutoMajor(rItemSet) )
            {
                rItemSet.Get( nWhichId ).QueryValue( aValue );
                if( lcl_isDateAxis(rItemSet) )
                {
                    double fValue = 1.0;
                    if( aValue >>= fValue )
                    {
                        TimeInterval aTimeInterval;
                        aScale.TimeIncrement.MajorTimeInterval >>= aTimeInterval;
                        aTimeInterval.Number = static_cast<sal_Int32>(fValue);
                        aScale.TimeIncrement.MajorTimeInterval <<= aTimeInterval;
                        bSetScale = true;
                    }
                }
                else if( aScale.IncrementData.Distance != aValue )
                {
                    aScale.IncrementData.Distance = aValue;
                    bSetScale = true;
                }
            }
            break;

        // SubIncrement
        case SCHATTR_AXIS_AUTO_STEP_HELP:
            if( lcl_isAutoMinor(rItemSet) )
            {
                if( aScale.IncrementData.SubIncrements.hasElements() &&
                    aScale.IncrementData.SubIncrements[0].IntervalCount.hasValue() )
                {
                        aScale.IncrementData.SubIncrements.getArray()[0].IntervalCount.clear();
                        bSetScale = true;
                }
                if( aScale.TimeIncrement.MinorTimeInterval.hasValue() )
                {
                    aScale.TimeIncrement.MinorTimeInterval.clear();
                    bSetScale = true;
                }
            }
            // else SCHATTR_AXIS_STEP_MAIN must have some value
            break;

        case SCHATTR_AXIS_HELP_TIME_UNIT:
            if( !lcl_isAutoMinor(rItemSet) )
            {
                if( rItemSet.Get( nWhichId ).QueryValue( aValue ) )
                {
                    TimeInterval aTimeInterval;
                    aScale.TimeIncrement.MinorTimeInterval >>= aTimeInterval;
                    aValue >>= aTimeInterval.TimeUnit;
                    aScale.TimeIncrement.MinorTimeInterval <<= aTimeInterval;
                    bSetScale = true;
                }
            }
            break;

        case SCHATTR_AXIS_STEP_HELP:
            // only if auto is false
            if( !lcl_isAutoMinor(rItemSet) )
            {
                rItemSet.Get( nWhichId ).QueryValue( aValue );
                if( lcl_isDateAxis(rItemSet) )
                {
                    TimeInterval aTimeInterval;
                    aScale.TimeIncrement.MinorTimeInterval >>= aTimeInterval;
                    aValue >>= aTimeInterval.Number;
                    aScale.TimeIncrement.MinorTimeInterval <<= aTimeInterval;
                    bSetScale = true;
                }
                else if( aScale.IncrementData.SubIncrements.hasElements() )
                {
                    if( ! aScale.IncrementData.SubIncrements[0].IntervalCount.hasValue() ||
                        aScale.IncrementData.SubIncrements[0].IntervalCount != aValue )
                    {
                        OSL_ASSERT( aValue.getValueTypeClass() == uno::TypeClass_LONG );
                        aScale.IncrementData.SubIncrements.getArray()[0].IntervalCount = aValue;
                        bSetScale = true;
                    }
                }
            }
            break;

        case SCHATTR_AXIS_AUTO_TIME_RESOLUTION:
            if( static_cast< const SfxBoolItem & >( rItemSet.Get( nWhichId )).GetValue() )
            {
                aScale.TimeIncrement.TimeResolution.clear();
                bSetScale = true;
            }
            break;
        case SCHATTR_AXIS_TIME_RESOLUTION:
            // only if auto is false
            if( ! ( rItemSet.Get( SCHATTR_AXIS_AUTO_TIME_RESOLUTION ).GetValue() ))
            {
                rItemSet.Get( nWhichId ).QueryValue( aValue );

                if( aScale.TimeIncrement.TimeResolution != aValue )
                {
                    aScale.TimeIncrement.TimeResolution = aValue;
                    bSetScale = true;
                }
            }
            break;

        case SCHATTR_AXIS_AUTO_ORIGIN:
        {
            if( static_cast< const SfxBoolItem & >(rItemSet.Get( nWhichId )).GetValue() )
            {
                aScale.Origin.clear();
                bSetScale = true;
            }
        }
        break;

        case SCHATTR_AXIS_ORIGIN:
        {
            // only if auto is false
            if( ! (rItemSet.Get( SCHATTR_AXIS_AUTO_ORIGIN ).GetValue() ))
            {
                rItemSet.Get( nWhichId ).QueryValue( aValue );

                if( aScale.Origin != aValue )
                {
                    aScale.Origin = aValue;
                    bSetScale = true;

                    if( !AxisHelper::isAxisPositioningEnabled() )
                    {
                        //keep old and new settings for axis positioning in sync somehow
                        rtl::Reference< BaseCoordinateSystem > xCooSys( AxisHelper::getCoordinateSystemOfAxis(
                            m_xAxis, m_xChartDoc->getFirstChartDiagram() ) );

                        sal_Int32 nDimensionIndex=0;
                        sal_Int32 nAxisIndex=0;
                        if( AxisHelper::getIndicesForAxis( m_xAxis, xCooSys, nDimensionIndex, nAxisIndex ) && nAxisIndex==0 )
                        {
                            rtl::Reference< Axis > xCrossingMainAxis = AxisHelper::getCrossingMainAxis( m_xAxis, xCooSys );
                            if( xCrossingMainAxis.is() )
                            {
                                double fValue = 0.0;
                                if( aValue >>= fValue )
                                {
                                    xCrossingMainAxis->setPropertyValue( u"CrossoverPosition"_ustr , uno::Any( css::chart::ChartAxisPosition_VALUE ));
                                    xCrossingMainAxis->setPropertyValue( u"CrossoverValue"_ustr , uno::Any( fValue ));
                                }
                                else
                                    xCrossingMainAxis->setPropertyValue( u"CrossoverPosition"_ustr , uno::Any( css::chart::ChartAxisPosition_START ));
                            }
                        }
                    }
                }
            }
        }
        break;

        case SCHATTR_AXIS_POSITION:
        {
            css::chart::ChartAxisPosition eAxisPos =
                static_cast<css::chart::ChartAxisPosition>(static_cast< const SfxInt32Item & >( rItemSet.Get( nWhichId )).GetValue());

            css::chart::ChartAxisPosition eOldAxisPos( css::chart::ChartAxisPosition_ZERO );
            bool bPropExisted = ( GetPropertySet()->getPropertyValue( u"CrossoverPosition"_ustr ) >>= eOldAxisPos );

            if( !bPropExisted || ( eOldAxisPos != eAxisPos ))
            {
                GetPropertySet()->setPropertyValue( u"CrossoverPosition"_ustr , uno::Any( eAxisPos ));
                bChangedOtherwise = true;

                //move the parallel axes to the other side if necessary
                if( eAxisPos==css::chart::ChartAxisPosition_START || eAxisPos==css::chart::ChartAxisPosition_END )
                {
                    rtl::Reference<Diagram> xDiagram = m_xChartDoc->getFirstChartDiagram();
                    rtl::Reference< Axis > xParallelAxis = AxisHelper::getParallelAxis( m_xAxis, xDiagram );
                    if( xParallelAxis.is() )
                    {
                        css::chart::ChartAxisPosition eOtherPos;
                        if( xParallelAxis->getPropertyValue( u"CrossoverPosition"_ustr ) >>= eOtherPos )
                        {
                            if( eOtherPos == eAxisPos )
                            {
                                css::chart::ChartAxisPosition eOppositePos =
                                    (eAxisPos==css::chart::ChartAxisPosition_START)
                                    ? css::chart::ChartAxisPosition_END
                                    : css::chart::ChartAxisPosition_START;
                                xParallelAxis->setPropertyValue( u"CrossoverPosition"_ustr , uno::Any( eOppositePos ));
                            }
                        }
                    }
                }
            }
        }
        break;

        case SCHATTR_AXIS_POSITION_VALUE:
        {
            double fValue = static_cast< const SvxDoubleItem & >( rItemSet.Get( nWhichId )).GetValue();

            double fOldValue = 0.0;
            bool bPropExisted = ( GetPropertySet()->getPropertyValue( u"CrossoverValue"_ustr ) >>= fOldValue );

            if( !bPropExisted || ( fOldValue != fValue ))
            {
                GetPropertySet()->setPropertyValue( u"CrossoverValue"_ustr , uno::Any( fValue ));
                bChangedOtherwise = true;

                //keep old and new settings for axis positioning in sync somehow
                {
                    rtl::Reference< BaseCoordinateSystem > xCooSys( AxisHelper::getCoordinateSystemOfAxis(
                        m_xAxis, m_xChartDoc->getFirstChartDiagram() ) );

                    sal_Int32 nDimensionIndex=0;
                    sal_Int32 nAxisIndex=0;
                    if( AxisHelper::getIndicesForAxis( m_xAxis, xCooSys, nDimensionIndex, nAxisIndex ) && nAxisIndex==0 )
                    {
                        rtl::Reference< Axis > xCrossingMainAxis( AxisHelper::getCrossingMainAxis( m_xAxis, xCooSys ) );
                        if( xCrossingMainAxis.is() )
                        {
                            ScaleData aCrossingScale( xCrossingMainAxis->getScaleData() );
                            aCrossingScale.Origin <<= fValue;
                            xCrossingMainAxis->setScaleData(aCrossingScale);
                        }
                    }
                }
            }
        }
        break;

        case SCHATTR_AXIS_SHIFTED_CATEGORY_POSITION:
        {
            bool bNewValue = static_cast<const SfxBoolItem &> (rItemSet.Get(nWhichId)).GetValue();
            bool bOldValue = aScale.ShiftedCategoryPosition;
            if (bOldValue != bNewValue)
            {
                aScale.ShiftedCategoryPosition = bNewValue;
                bSetScale = true;
            }
        }
        break;

        case SCHATTR_AXIS_LABEL_POSITION:
        {
            css::chart::ChartAxisLabelPosition ePos =
                static_cast<css::chart::ChartAxisLabelPosition>(static_cast< const SfxInt32Item & >( rItemSet.Get( nWhichId )).GetValue());

            css::chart::ChartAxisLabelPosition eOldPos( css::chart::ChartAxisLabelPosition_NEAR_AXIS );
            bool bPropExisted = ( GetPropertySet()->getPropertyValue( u"LabelPosition"_ustr ) >>= eOldPos );

            if( !bPropExisted || ( eOldPos != ePos ))
            {
                GetPropertySet()->setPropertyValue( u"LabelPosition"_ustr , uno::Any( ePos ));
                bChangedOtherwise = true;

                //move the parallel axes to the other side if necessary
                if( ePos==css::chart::ChartAxisLabelPosition_OUTSIDE_START || ePos==css::chart::ChartAxisLabelPosition_OUTSIDE_END )
                {
                    rtl::Reference<Diagram> xDiagram = m_xChartDoc->getFirstChartDiagram();
                    rtl::Reference< Axis > xParallelAxis = AxisHelper::getParallelAxis( m_xAxis, xDiagram );
                    if( xParallelAxis.is() )
                    {
                        css::chart::ChartAxisLabelPosition eOtherPos;
                        if( xParallelAxis->getPropertyValue( u"LabelPosition"_ustr ) >>= eOtherPos )
                        {
                            if( eOtherPos == ePos )
                            {
                                css::chart::ChartAxisLabelPosition eOppositePos =
                                    (ePos==css::chart::ChartAxisLabelPosition_OUTSIDE_START)
                                    ? css::chart::ChartAxisLabelPosition_OUTSIDE_END
                                    : css::chart::ChartAxisLabelPosition_OUTSIDE_START;
                                xParallelAxis->setPropertyValue( u"LabelPosition"_ustr , uno::Any( eOppositePos ));
                            }
                        }
                    }
                }
            }
        }
        break;

        case SCHATTR_AXIS_MARK_POSITION:
        {
            css::chart::ChartAxisMarkPosition ePos =
                static_cast<css::chart::ChartAxisMarkPosition>(static_cast< const SfxInt32Item & >( rItemSet.Get( nWhichId )).GetValue());

            css::chart::ChartAxisMarkPosition eOldPos( css::chart::ChartAxisMarkPosition_AT_LABELS_AND_AXIS );
            bool bPropExisted = ( GetPropertySet()->getPropertyValue( u"MarkPosition"_ustr ) >>= eOldPos );

            if( !bPropExisted || ( eOldPos != ePos ))
            {
                GetPropertySet()->setPropertyValue( u"MarkPosition"_ustr , uno::Any( ePos ));
                bChangedOtherwise = true;
            }
        }
        break;

        case SCHATTR_TEXT_DEGREES:
        {
            double fVal = toDegrees(rItemSet.Get(SCHATTR_TEXT_DEGREES).GetValue());
            double fOldVal = 0.0;
            bool bPropExisted =
                ( GetPropertySet()->getPropertyValue( u"TextRotation"_ustr ) >>= fOldVal );

            if( ! bPropExisted || fOldVal != fVal )
            {
                GetPropertySet()->setPropertyValue( u"TextRotation"_ustr , uno::Any( fVal ));
                bChangedOtherwise = true;
            }
        }
        break;

        case SID_ATTR_NUMBERFORMAT_VALUE:
        {
            if( m_pExplicitScale )
            {
                bool bUseSourceFormat =
                        rItemSet.Get( SID_ATTR_NUMBERFORMAT_SOURCE ).GetValue();

                if( ! bUseSourceFormat )
                {
                    sal_Int32 nFmt = static_cast< sal_Int32 >(
                        static_cast< const SfxUInt32Item & >(
                            rItemSet.Get( nWhichId )).GetValue());

                    aValue <<= nFmt;
                    if (GetPropertySet()->getPropertyValue(CHART_UNONAME_NUMFMT) != aValue)
                    {
                        GetPropertySet()->setPropertyValue(CHART_UNONAME_NUMFMT , aValue);
                        bChangedOtherwise = true;
                    }
                }
            }
        }
        break;

        case SID_ATTR_NUMBERFORMAT_SOURCE:
        {
            bool bUseSourceFormat =
                static_cast< const SfxBoolItem & >(
                    rItemSet.Get( nWhichId )).GetValue();
            GetPropertySet()->setPropertyValue(CHART_UNONAME_LINK_TO_SRC_NUMFMT, uno::Any(bUseSourceFormat));

            bool bNumberFormatIsSet = GetPropertySet()->getPropertyValue(CHART_UNONAME_NUMFMT).hasValue();

            bChangedOtherwise = (bUseSourceFormat == bNumberFormatIsSet);
            if( bChangedOtherwise )
            {
                if( ! bUseSourceFormat )
                {
                    SfxItemState aState = rItemSet.GetItemState( SID_ATTR_NUMBERFORMAT_VALUE );
                    if( aState == SfxItemState::SET )
                    {
                        sal_Int32 nFormatKey = static_cast< sal_Int32 >(
                            rItemSet.Get( SID_ATTR_NUMBERFORMAT_VALUE ).GetValue());
                        aValue <<= nFormatKey;
                    }
                    else
                    {
                        rtl::Reference< BaseCoordinateSystem > xCooSys =
                            AxisHelper::getCoordinateSystemOfAxis(
                                m_xAxis, m_xChartDoc->getFirstChartDiagram() );

                        sal_Int32 nFormatKey = ChartView::getExplicitNumberFormatKeyForAxis(
                            m_xAxis, xCooSys, m_xChartDoc);

                        aValue <<= nFormatKey;
                    }
                }
                // else set a void Any
                GetPropertySet()->setPropertyValue(CHART_UNONAME_NUMFMT , aValue);
            }
        }
        break;

        case SCHATTR_AXISTYPE:
        {
            sal_Int32 nNewAxisType = static_cast< const SfxInt32Item & >( rItemSet.Get( nWhichId )).GetValue();//css::chart2::AxisType
            aScale.AxisType = nNewAxisType;
            bSetScale = true;
        }
        break;

        case SCHATTR_AXIS_AUTO_DATEAXIS:
        {
            bool bNewValue = static_cast< const SfxBoolItem & >( rItemSet.Get( nWhichId )).GetValue();
            bool bOldValue = aScale.AutoDateAxis;
            if( bOldValue != bNewValue )
            {
                aScale.AutoDateAxis = bNewValue;
                bSetScale = true;
            }
        }
        break;
    }

    if( bSetScale )
        m_xAxis->setScaleData( aScale );

    return (bSetScale || bChangedOtherwise);
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
