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
#pragma once

#include "ItemConverter.hxx"
#include "GraphicPropertyItemConverter.hxx"
#include <com/sun/star/awt/Size.hpp>
#include <com/sun/star/uno/Sequence.h>

#include <tools/color.hxx>
#include <rtl/ref.hxx>
#include <optional>
#include <vector>

namespace com::sun::star::chart2 { class XDataSeries; }
namespace com::sun::star::frame { class XModel; }
namespace com::sun::star::uno { class XComponentContext; }
namespace chart { class ChartModel; }
namespace chart { class DataSeries; }
class SdrModel;

namespace chart::wrapper {

class DataPointItemConverter final : public ItemConverter
{
public:
    DataPointItemConverter(
        const rtl::Reference<::chart::ChartModel>& xChartModel,
        const css::uno::Reference<css::uno::XComponentContext>& xContext,
        const css::uno::Reference<css::beans::XPropertySet>& rPropertySet,
        const rtl::Reference<::chart::DataSeries>& xSeries,
        SfxItemPool& rItemPool,
        SdrModel& rDrawModel,
        GraphicObjectType eMapTo,
        const std::optional<css::awt::Size>& pRefSize = std::nullopt,
        bool bDataSeries = false,
        bool bUseSpecialFillColor = false,
        sal_Int32 nSpecialFillColor = 0,
        bool bOverwriteLabelsForAttributedDataPointsAlso = false,
        sal_Int32 nNumberFormat = 0,
        sal_Int32 nPercentNumberFormat = 0,
        sal_Int32 nPointIndex = -1 );

    virtual ~DataPointItemConverter() override;

    virtual void FillItemSet( SfxItemSet & rOutItemSet ) const override;
    virtual bool ApplyItemSet( const SfxItemSet & rItemSet ) override;

protected:
    virtual const WhichRangesContainer& GetWhichPairs() const override;
    virtual bool GetItemProperty( tWhichIdType nWhichId, tPropertyNameWithMemberId & rOutProperty ) const override;

    virtual void FillSpecialItem( sal_uInt16 nWhichId, SfxItemSet & rOutItemSet ) const override;
    virtual bool ApplySpecialItem( sal_uInt16 nWhichId, const SfxItemSet & rItemSet ) override;

private:
    std::vector< std::unique_ptr<ItemConverter> > m_aConverters;
    bool                                m_bDataSeries;
    bool                                m_bOverwriteLabelsForAttributedDataPointsAlso;
    bool                                m_bUseSpecialFillColor;
    Color                               m_nSpecialFillColor;
    sal_Int32                           m_nNumberFormat;
    sal_Int32                           m_nPercentNumberFormat;
    css::uno::Sequence<sal_Int32>       m_aAvailableLabelPlacements;
    bool                                m_bForbidPercentValue;
    bool                                m_bHideLegendEntry;
    sal_Int32                           m_nPointIndex;
    rtl::Reference<::chart::DataSeries> m_xSeries;
};

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
