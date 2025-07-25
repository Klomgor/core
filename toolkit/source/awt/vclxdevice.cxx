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

#include <sal/config.h>

#include <com/sun/star/util/MeasureUnit.hpp>
#include <com/sun/star/lang/IllegalArgumentException.hpp>

#include <toolkit/awt/vclxdevice.hxx>
#include <toolkit/awt/vclxfont.hxx>
#include <awt/vclxbitmap.hxx>
#include <toolkit/helper/vclunohelper.hxx>

#include <vcl/svapp.hxx>
#include <vcl/outdev.hxx>
#include <vcl/virdev.hxx>
#include <vcl/bitmapex.hxx>
#include <vcl/metric.hxx>
#include <vcl/unohelp.hxx>


VCLXDevice::VCLXDevice()
{
}

VCLXDevice::~VCLXDevice()
{
    //TODO: why was this empty, and everything done in ~VCLXVirtualDevice?
    SolarMutexGuard g;
    mpOutputDevice.reset();
}

// css::awt::XDevice,
css::uno::Reference< css::awt::XGraphics > VCLXDevice::createGraphics(  )
{
    SolarMutexGuard aGuard;

    css::uno::Reference< css::awt::XGraphics > xRef;

    if ( mpOutputDevice )
        xRef = mpOutputDevice->CreateUnoGraphics();

    return xRef;
}

css::uno::Reference< css::awt::XDevice > VCLXDevice::createDevice( sal_Int32 nWidth, sal_Int32 nHeight )
{
    SolarMutexGuard aGuard;

    if ( !GetOutputDevice() )
        return nullptr;

    rtl::Reference<VCLXVirtualDevice> pVDev = new VCLXVirtualDevice;
    VclPtrInstance<VirtualDevice> pVclVDev( *GetOutputDevice() );
    pVclVDev->SetOutputSizePixel( Size( nWidth, nHeight ) );
    pVDev->SetVirtualDevice( pVclVDev );
    return pVDev;
}

css::awt::DeviceInfo VCLXDevice::getInfo()
{
    SolarMutexGuard aGuard;

    css::awt::DeviceInfo aInfo;

    if (mpOutputDevice)
        aInfo = mpOutputDevice->GetDeviceInfo();

    return aInfo;
}

css::uno::Sequence< css::awt::FontDescriptor > VCLXDevice::getFontDescriptors(  )
{
    SolarMutexGuard aGuard;

    css::uno::Sequence< css::awt::FontDescriptor> aFonts;
    if( mpOutputDevice )
    {
        int nFonts = mpOutputDevice->GetFontFaceCollectionCount();
        if ( nFonts )
        {
            aFonts = css::uno::Sequence< css::awt::FontDescriptor>( nFonts );
            css::awt::FontDescriptor* pFonts = aFonts.getArray();
            for ( int n = 0; n < nFonts; n++ )
                pFonts[n] = VCLUnoHelper::CreateFontDescriptor( mpOutputDevice->GetFontMetricFromCollection( n ) );
        }
    }
    return aFonts;
}

css::uno::Reference< css::awt::XFont > VCLXDevice::getFont( const css::awt::FontDescriptor& rDescriptor )
{
    SolarMutexGuard aGuard;

    if( !mpOutputDevice )
        return nullptr;

    rtl::Reference<VCLXFont> pMetric
        = new VCLXFont(*this, VCLUnoHelper::CreateFont(rDescriptor, mpOutputDevice->GetFont()));
    return pMetric;
}

css::uno::Reference< css::awt::XBitmap > VCLXDevice::createBitmap( sal_Int32 nX, sal_Int32 nY, sal_Int32 nWidth, sal_Int32 nHeight )
{
    SolarMutexGuard aGuard;

    if( !mpOutputDevice )
        return nullptr;

    Bitmap aBmp = mpOutputDevice->GetBitmap( Point( nX, nY ), Size( nWidth, nHeight ) );
    rtl::Reference<VCLXBitmap> pBmp = new VCLXBitmap;
    pBmp->SetBitmap( BitmapEx(aBmp) );
    return pBmp;
}

css::uno::Reference< css::awt::XDisplayBitmap > VCLXDevice::createDisplayBitmap( const css::uno::Reference< css::awt::XBitmap >& rxBitmap )
{
    SolarMutexGuard aGuard;

    BitmapEx aBmp = VCLUnoHelper::GetBitmap( rxBitmap );
    rtl::Reference<VCLXBitmap> pBmp = new VCLXBitmap;
    pBmp->SetBitmap( aBmp );
    return pBmp;
}

VCLXVirtualDevice::~VCLXVirtualDevice()
{
    SolarMutexGuard aGuard;

    mpOutputDevice.disposeAndClear();
}

// Interface implementation of css::awt::XUnitConversion

css::awt::Point SAL_CALL VCLXDevice::convertPointToLogic( const css::awt::Point& aPoint, ::sal_Int16 TargetUnit )
{
    SolarMutexGuard aGuard;
    if (TargetUnit == css::util::MeasureUnit::PERCENT )
    {
        // percentage not allowed here
        throw css::lang::IllegalArgumentException();
    }

    css::awt::Point aAWTPoint(0,0);
    // X,Y

    if( mpOutputDevice )
    {
        MapMode aMode(VCLUnoHelper::ConvertToMapModeUnit(TargetUnit));
        ::Point aVCLPoint = vcl::unohelper::ConvertToVCLPoint(aPoint);
        ::Point aDevPoint = mpOutputDevice->PixelToLogic(aVCLPoint, aMode );
        aAWTPoint = vcl::unohelper::ConvertToAWTPoint(aDevPoint);
    }

    return aAWTPoint;
}


css::awt::Point SAL_CALL VCLXDevice::convertPointToPixel( const css::awt::Point& aPoint, ::sal_Int16 SourceUnit )
{
    SolarMutexGuard aGuard;
    if (SourceUnit == css::util::MeasureUnit::PERCENT ||
        SourceUnit == css::util::MeasureUnit::PIXEL )
    {
        // pixel or percentage not allowed here
        throw css::lang::IllegalArgumentException();
    }

    css::awt::Point aAWTPoint(0,0);

    if( mpOutputDevice )
    {
        MapMode aMode(VCLUnoHelper::ConvertToMapModeUnit(SourceUnit));
        ::Point aVCLPoint = vcl::unohelper::ConvertToVCLPoint(aPoint);
        ::Point aDevPoint = mpOutputDevice->LogicToPixel(aVCLPoint, aMode );
        aAWTPoint = vcl::unohelper::ConvertToAWTPoint(aDevPoint);
    }

    return aAWTPoint;
}

css::awt::Size SAL_CALL VCLXDevice::convertSizeToLogic( const css::awt::Size& aSize, ::sal_Int16 TargetUnit )
{
    SolarMutexGuard aGuard;
    if (TargetUnit == css::util::MeasureUnit::PERCENT)
    {
        // percentage not allowed here
        throw css::lang::IllegalArgumentException();
    }

    css::awt::Size aAWTSize(0,0);
    // Width, Height


    if( mpOutputDevice )
    {
        MapMode aMode(VCLUnoHelper::ConvertToMapModeUnit(TargetUnit));
        ::Size aVCLSize = vcl::unohelper::ConvertToVCLSize(aSize);
        ::Size aDevSz = mpOutputDevice->PixelToLogic(aVCLSize, aMode );
        aAWTSize = vcl::unohelper::ConvertToAWTSize(aDevSz);
    }

    return aAWTSize;
}

css::awt::Size SAL_CALL VCLXDevice::convertSizeToPixel( const css::awt::Size& aSize, ::sal_Int16 SourceUnit )
{
    SolarMutexGuard aGuard;
    if (SourceUnit == css::util::MeasureUnit::PERCENT ||
        SourceUnit == css::util::MeasureUnit::PIXEL)
    {
        // pixel or percentage not allowed here
        throw css::lang::IllegalArgumentException();
    }

    css::awt::Size aAWTSize(0,0);
    // Width, Height
    if( mpOutputDevice )
    {
        MapMode aMode(VCLUnoHelper::ConvertToMapModeUnit(SourceUnit));
        ::Size aVCLSize = vcl::unohelper::ConvertToVCLSize(aSize);
        ::Size aDevSz = mpOutputDevice->LogicToPixel(aVCLSize, aMode );
        aAWTSize = vcl::unohelper::ConvertToAWTSize(aDevSz);
    }

    return aAWTSize;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
