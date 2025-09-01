/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
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

#include <config_features.h>

#include <osl/diagnose.h>
#include <tools/debug.hxx>
#include <tools/helpers.hxx>

#include <vcl/image.hxx>
#include <vcl/metaact.hxx>
#include <vcl/skia/SkiaHelper.hxx>
#include <vcl/virdev.hxx>
#include <vcl/BitmapWriteAccess.hxx>

#include <bitmap/bmpfast.hxx>
#include <drawmode.hxx>
#include <salbmp.hxx>
#include <salgdi.hxx>

void OutputDevice::DrawBitmap( const Point& rDestPt, const Bitmap& rBitmap )
{
    assert(!is_double_buffered_window());

    const Size aSizePix( rBitmap.GetSizePixel() );
    DrawBitmap( rDestPt, PixelToLogic( aSizePix ), Point(), aSizePix, rBitmap, MetaActionType::BMP );
}

void OutputDevice::DrawBitmap( const Point& rDestPt, const Size& rDestSize, const Bitmap& rBitmap )
{
    assert(!is_double_buffered_window());

    DrawBitmap( rDestPt, rDestSize, Point(), rBitmap.GetSizePixel(), rBitmap, MetaActionType::BMPSCALE );
}

void OutputDevice::DrawBitmap( const Point& rDestPt, const Size& rDestSize,
                                   const Point& rSrcPtPixel, const Size& rSrcSizePixel,
                                   const Bitmap& rBitmap)
{
    assert(!is_double_buffered_window());

    DrawBitmap( rDestPt, rDestSize, rSrcPtPixel, rSrcSizePixel, rBitmap, MetaActionType::BMPSCALEPART );
}

void OutputDevice::DrawBitmap( const Point& rDestPt, const Size& rDestSize,
                                   const Point& rSrcPtPixel, const Size& rSrcSizePixel,
                                   const Bitmap& rBitmap, const MetaActionType nAction )
{
    assert(!is_double_buffered_window());

    if( ImplIsRecordLayout() )
        return;

    if ( RasterOp::Invert == meRasterOp )
    {
        DrawRect( tools::Rectangle( rDestPt, rDestSize ) );
        return;
    }

    if (mnDrawMode & (DrawModeFlags::BlackBitmap | DrawModeFlags::WhiteBitmap))
    {
        sal_uInt8 cCmpVal;

        if (mnDrawMode & DrawModeFlags::BlackBitmap)
            cCmpVal = 0;
        else
            cCmpVal = 255;

        Color aCol(cCmpVal, cCmpVal, cCmpVal);
        auto popIt = ScopedPush(vcl::PushFlags::LINECOLOR | vcl::PushFlags::FILLCOLOR);
        SetLineColor(aCol);
        SetFillColor(aCol);
        DrawRect(tools::Rectangle(rDestPt, rDestSize));
        return;
    }

    Bitmap aBmp(rBitmap);

    if (mnDrawMode & DrawModeFlags::GrayBitmap && !aBmp.IsEmpty())
        aBmp.Convert(BmpConversion::N8BitGreys);

    if ( mpMetaFile )
    {
        switch( nAction )
        {
            case MetaActionType::BMP:
                mpMetaFile->AddAction( new MetaBmpAction( rDestPt, aBmp ) );
            break;

            case MetaActionType::BMPSCALE:
                mpMetaFile->AddAction( new MetaBmpScaleAction( rDestPt, rDestSize, aBmp ) );
            break;

            case MetaActionType::BMPSCALEPART:
                mpMetaFile->AddAction( new MetaBmpScalePartAction(
                    rDestPt, rDestSize, rSrcPtPixel, rSrcSizePixel, aBmp ) );
            break;

            default: break;
        }
    }

    if ( !IsDeviceOutputNecessary() )
        return;

    if (!mpGraphics && !AcquireGraphics())
        return;
    assert(mpGraphics);

    if ( mbInitClipRegion )
        InitClipRegion();

    if ( mbOutputClipped )
        return;

    if (aBmp.IsEmpty())
        return;

    SalTwoRect aPosAry(rSrcPtPixel.X(), rSrcPtPixel.Y(), rSrcSizePixel.Width(), rSrcSizePixel.Height(),
                       ImplLogicXToDevicePixel(rDestPt.X()), ImplLogicYToDevicePixel(rDestPt.Y()),
                       ImplLogicWidthToDevicePixel(rDestSize.Width()),
                       ImplLogicHeightToDevicePixel(rDestSize.Height()));

    if (!aPosAry.mnSrcWidth || !aPosAry.mnSrcHeight || !aPosAry.mnDestWidth || !aPosAry.mnDestHeight)
        return;

    const BmpMirrorFlags nMirrFlags = AdjustTwoRect( aPosAry, aBmp.GetSizePixel() );

    if ( nMirrFlags != BmpMirrorFlags::NONE )
        aBmp.Mirror( nMirrFlags );

    if (!aPosAry.mnSrcWidth || !aPosAry.mnSrcHeight || !aPosAry.mnDestWidth || !aPosAry.mnDestHeight)
        return;

    if (nAction == MetaActionType::BMPSCALE && CanSubsampleBitmap())
    {
        double nScaleX = aPosAry.mnDestWidth  / static_cast<double>(aPosAry.mnSrcWidth);
        double nScaleY = aPosAry.mnDestHeight / static_cast<double>(aPosAry.mnSrcHeight);

        // If subsampling, use Bitmap::Scale() for subsampling of better quality.

        // but hidpi surfaces like the cairo one have their own scale, so don't downscale
        // past the surface scaling which can retain the extra detail
        double fScale(1.0);
        if (mpGraphics->ShouldDownscaleIconsAtSurface(fScale))
        {
            nScaleX *= fScale;
            nScaleY *= fScale;
        }

        if ( nScaleX < 1.0 || nScaleY < 1.0 )
        {
            aBmp.Scale(nScaleX, nScaleY);
            aPosAry.mnSrcWidth = aPosAry.mnDestWidth * fScale;
            aPosAry.mnSrcHeight = aPosAry.mnDestHeight * fScale;
        }
    }

    mpGraphics->DrawBitmap( aPosAry, *aBmp.ImplGetSalBitmap(), *this );
}

Bitmap OutputDevice::GetBitmap( const Point& rSrcPt, const Size& rSize ) const
{
    if ( !mpGraphics && !AcquireGraphics() )
        return Bitmap();

    assert(mpGraphics);

    tools::Long    nX = ImplLogicXToDevicePixel( rSrcPt.X() );
    tools::Long    nY = ImplLogicYToDevicePixel( rSrcPt.Y() );
    tools::Long    nWidth = ImplLogicWidthToDevicePixel( rSize.Width() );
    tools::Long    nHeight = ImplLogicHeightToDevicePixel( rSize.Height() );
    if ( nWidth <= 0 || nHeight <= 0 || nX > (mnOutWidth + mnOutOffX) || nY > (mnOutHeight + mnOutOffY))
        return Bitmap();

    Bitmap  aBmp;
    tools::Rectangle   aRect( Point( nX, nY ), Size( nWidth, nHeight ) );
    bool bClipped = false;

    // X-Coordinate outside of draw area?
    if ( nX < mnOutOffX )
    {
        nWidth -= ( mnOutOffX - nX );
        nX = mnOutOffX;
        bClipped = true;
    }

    // Y-Coordinate outside of draw area?
    if ( nY < mnOutOffY )
    {
        nHeight -= ( mnOutOffY - nY );
        nY = mnOutOffY;
        bClipped = true;
    }

    // Width outside of draw area?
    if ( (nWidth + nX) > (mnOutWidth + mnOutOffX) )
    {
        nWidth  = mnOutOffX + mnOutWidth - nX;
        bClipped = true;
    }

    // Height outside of draw area?
    if ( (nHeight + nY) > (mnOutHeight + mnOutOffY) )
    {
        nHeight = mnOutOffY + mnOutHeight - nY;
        bClipped = true;
    }

    if ( bClipped )
    {
        // If the visible part has been clipped, we have to create a
        // Bitmap with the correct size in which we copy the clipped
        // Bitmap to the correct position.
        ScopedVclPtrInstance< VirtualDevice > aVDev(  *this  );

        if ( aVDev->SetOutputSizePixel( aRect.GetSize() ) )
        {
            if ( aVDev->mpGraphics || aVDev->AcquireGraphics() )
            {
                if ( (nWidth > 0) && (nHeight > 0) )
                {
                    SalTwoRect aPosAry(nX, nY, nWidth, nHeight,
                                      (aRect.Left() < mnOutOffX) ? (mnOutOffX - aRect.Left()) : 0L,
                                      (aRect.Top() < mnOutOffY) ? (mnOutOffY - aRect.Top()) : 0L,
                                      nWidth, nHeight);
                    aVDev->mpGraphics->CopyBits(aPosAry, *mpGraphics, *this, *this);
                }
                else
                {
                    OSL_ENSURE(false, "CopyBits with zero or negative width or height");
                }

                aBmp = aVDev->GetBitmap( Point(), aVDev->GetOutputSizePixel() );
            }
            else
                bClipped = false;
        }
        else
            bClipped = false;
    }

    if ( !bClipped )
    {
        std::shared_ptr<SalBitmap> pSalBmp;
        // if we are a virtual device, we might need to remove the unused alpha channel
        bool bWithoutAlpha = false;
        if (OUTDEV_VIRDEV == GetOutDevType())
            bWithoutAlpha = static_cast<const VirtualDevice*>(this)->IsWithoutAlpha();
        pSalBmp = mpGraphics->GetBitmap( nX, nY, nWidth, nHeight, *this, bWithoutAlpha );

        if( pSalBmp )
        {
            aBmp.ImplSetSalBitmap(pSalBmp);
        }
    }

    return aBmp;
}

void OutputDevice::DrawDeviceAlphaBitmap( const Bitmap& rBmp,
                                    const Point& rDestPt, const Size& rDestSize,
                                    const Point& rSrcPtPixel, const Size& rSrcSizePixel )
{
    assert(!is_double_buffered_window());

    Point     aOutPt(LogicToPixel(rDestPt));
    Size      aOutSz(LogicToPixel(rDestSize));
    tools::Rectangle aDstRect(Point(), GetOutputSizePixel());

    const bool bHMirr = aOutSz.Width() < 0;
    const bool bVMirr = aOutSz.Height() < 0;

    ClipToPaintRegion(aDstRect);

    BmpMirrorFlags mirrorFlags = BmpMirrorFlags::NONE;
    if (bHMirr)
    {
        aOutSz.setWidth( -aOutSz.Width() );
        aOutPt.AdjustX( -(aOutSz.Width() - 1) );
        mirrorFlags |= BmpMirrorFlags::Horizontal;
    }

    if (bVMirr)
    {
        aOutSz.setHeight( -aOutSz.Height() );
        aOutPt.AdjustY( -(aOutSz.Height() - 1) );
        mirrorFlags |= BmpMirrorFlags::Vertical;
    }

    if (aDstRect.Intersection(tools::Rectangle(aOutPt, aOutSz)).IsEmpty())
        return;

    {
        Point aRelPt = aOutPt + Point(mnOutOffX, mnOutOffY);
        SalTwoRect aTR(
            rSrcPtPixel.X(), rSrcPtPixel.Y(),
            rSrcSizePixel.Width(), rSrcSizePixel.Height(),
            aRelPt.X(), aRelPt.Y(),
            aOutSz.Width(), aOutSz.Height());

        Bitmap bitmap(rBmp);
        if(bHMirr || bVMirr)
        {
            bitmap.Mirror(mirrorFlags);
        }
        SalBitmap* pSalSrcBmp = bitmap.ImplGetSalBitmap().get();

        if (mpGraphics->DrawAlphaBitmap(aTR, *pSalSrcBmp, *this))
            return;

        // we need to make sure Skia never reaches this slow code path
        // (but do not fail in no-op cases)
        assert(!SkiaHelper::isVCLSkiaEnabled() || !SkiaHelper::isAlphaMaskBlendingEnabled()
            || tools::Rectangle(Point(), rBmp.GetSizePixel())
                .Intersection(tools::Rectangle(rSrcPtPixel, rSrcSizePixel)).IsEmpty());
    }

    tools::Rectangle aBmpRect(Point(), rBmp.GetSizePixel());
    if (aBmpRect.Intersection(tools::Rectangle(rSrcPtPixel, rSrcSizePixel)).IsEmpty())
        return;

    Point     auxOutPt(LogicToPixel(rDestPt));
    Size      auxOutSz(LogicToPixel(rDestSize));

    // HACK: The function is broken with alpha vdev and mirroring, mirror here.
    Bitmap bitmap(rBmp);
    if(bHMirr || bVMirr)
    {
        bitmap.Mirror(mirrorFlags);
        auxOutPt = aOutPt;
        auxOutSz = aOutSz;
    }
    DrawDeviceAlphaBitmapSlowPath(bitmap, aDstRect, aBmpRect, auxOutSz, auxOutPt);
}

namespace
{

struct TradScaleContext
{
    std::unique_ptr<sal_Int32[]> mpMapX;
    std::unique_ptr<sal_Int32[]> mpMapY;

    TradScaleContext(tools::Rectangle const & aDstRect, tools::Rectangle const & aBitmapRect,
                 Size const & aOutSize, tools::Long nOffX, tools::Long nOffY)

        : mpMapX(new sal_Int32[aDstRect.GetWidth()])
        , mpMapY(new sal_Int32[aDstRect.GetHeight()])
    {
        const tools::Long nSrcWidth = aBitmapRect.GetWidth();
        const tools::Long nSrcHeight = aBitmapRect.GetHeight();

        const bool bHMirr = aOutSize.Width() < 0;
        const bool bVMirr = aOutSize.Height() < 0;

        generateSimpleMap(
            nSrcWidth, aDstRect.GetWidth(), aBitmapRect.Left(),
            aOutSize.Width(), nOffX, bHMirr, mpMapX.get());

        generateSimpleMap(
            nSrcHeight, aDstRect.GetHeight(), aBitmapRect.Top(),
            aOutSize.Height(), nOffY, bVMirr, mpMapY.get());
    }

private:

    static void generateSimpleMap(tools::Long nSrcDimension, tools::Long nDstDimension, tools::Long nDstLocation,
                                  tools::Long nOutDimension, tools::Long nOffset, bool bMirror, sal_Int32* pMap)
    {
        tools::Long nMirrorOffset = 0;

        if (bMirror)
            nMirrorOffset = (nDstLocation << 1) + nSrcDimension - 1;

        for (tools::Long i = 0; i < nDstDimension; ++i, ++nOffset)
        {
            pMap[i] = nDstLocation + nOffset * nSrcDimension / nOutDimension;
            if (bMirror)
                pMap[i] = nMirrorOffset - pMap[i];
        }
    }
};


// Co = Cs + Cd*(1-As) premultiplied alpha -or-
// Co = (AsCs + AdCd*(1-As)) / Ao
sal_uInt8 lcl_CalcColor( const sal_uInt8 nSourceColor, const sal_uInt8 nSourceAlpha,
                            const sal_uInt8 nDstAlpha, const sal_uInt8 nResAlpha, const sal_uInt8 nDestColor )
{
    int c = nResAlpha ? ( static_cast<int>(nSourceAlpha)*nSourceColor + static_cast<int>(nDstAlpha)*nDestColor -
                          static_cast<int>(nDstAlpha)*nDestColor*nSourceAlpha/255 ) / static_cast<int>(nResAlpha) : 0;
    return sal_uInt8( c );
}

// blend one color with another color with an alpha value

BitmapColor lcl_AlphaBlendColors(const BitmapColor& rCol1, const BitmapColor& rCol2)
{
    const sal_uInt8 nAlpha = rCol1.GetAlpha();
    BitmapColor aCol(rCol2);

    // Perform porter-duff compositing 'over' operation

    // Co = Cs + Cd*(1-As)
    // Ad = As + Ad*(1-As)
    const sal_uInt8 nResAlpha = static_cast<int>(nAlpha) + static_cast<int>(rCol2.GetAlpha())
              - static_cast<int>(rCol2.GetAlpha()) * nAlpha / 255;

    aCol.SetAlpha(nResAlpha);
    aCol.SetRed(lcl_CalcColor(rCol1.GetRed(), nAlpha, rCol2.GetAlpha(), nResAlpha, aCol.GetRed()));
    aCol.SetBlue(lcl_CalcColor(rCol1.GetBlue(), nAlpha, rCol2.GetAlpha(), nResAlpha, aCol.GetBlue()));
    aCol.SetGreen(lcl_CalcColor(rCol1.GetGreen(), nAlpha, rCol2.GetAlpha(), nResAlpha, aCol.GetGreen()));

    return aCol;
}

Bitmap lcl_BlendBitmapWithAlpha(
            Bitmap&             aBmp,
            const Bitmap&       rBitmap,
            const sal_Int32     nDstHeight,
            const sal_Int32     nDstWidth,
            const sal_Int32*    pMapX,
            const sal_Int32*    pMapY )

{
    Bitmap      res;

    BitmapScopedReadAccess pSrcBmp(rBitmap);

    {
        BitmapScopedWriteAccess pDstBmp(aBmp);
        if (pDstBmp && pSrcBmp)
        {
            for( int nY = 0; nY < nDstHeight; nY++ )
            {
                const tools::Long  nMapY = pMapY[ nY ];
                Scanline pDstScanline = pDstBmp->GetScanline(nY);

                for( int nX = 0; nX < nDstWidth; nX++ )
                {
                    pDstBmp->SetPixelOnData(pDstScanline, nX,
                                            lcl_AlphaBlendColors(pSrcBmp->GetColor(nMapY, pMapX[nX]),
                                                                 pDstBmp->GetColor(nY, nX)));
                }
            }
        }

        pDstBmp.reset();
        res = aBmp;
    }

    return res;
}

} // end anonymous namespace

void OutputDevice::DrawDeviceAlphaBitmapSlowPath(const Bitmap& rBitmap,
    const tools::Rectangle& rDstRect, const tools::Rectangle& rBmpRect, Size const& rOutSize, Point const& rOutPoint)
{
    assert(!is_double_buffered_window());

    // The scaling in this code path produces really ugly results - it
    // does the most trivial scaling with no smoothing.
    GDIMetaFile* pOldMetaFile = mpMetaFile;
    const bool   bOldMap = mbMap;

    mpMetaFile = nullptr; // fdo#55044 reset before GetBitmap!
    mbMap = false;

    Bitmap aDeviceBmp(GetBitmap(rDstRect.TopLeft(), rDstRect.GetSize()));

    // #109044# The generated bitmap need not necessarily be
    // of aDstRect dimensions, it's internally clipped to
    // window bounds. Thus, we correct the dest size here,
    // since we later use it (in nDstWidth/Height) for pixel
    // access)
    // #i38887# reading from screen may sometimes fail

    tools::Rectangle aDstRect(rDstRect);

    if (aDeviceBmp.ImplGetSalBitmap())
        aDstRect.SetSize(aDeviceBmp.GetSizePixel());

    // calculate offset in original bitmap
    // in RTL case this is a little more complicated since the contents of the
    // bitmap is not mirrored (it never is), however the paint region and bmp region
    // are in mirrored coordinates, so the intersection of (aOutPt,aOutSz) with these
    // is content wise somewhere else and needs to take mirroring into account
    const tools::Long nOffX = IsRTLEnabled()
                            ? rOutSize.Width() - aDstRect.GetWidth() - (aDstRect.Left() - rOutPoint.X())
                            : aDstRect.Left() - rOutPoint.X();

    const tools::Long nOffY = aDstRect.Top() - rOutPoint.Y();

    const TradScaleContext aTradContext(aDstRect, rBmpRect, rOutSize, nOffX, nOffY);

    // #i38887# reading from screen may sometimes fail
    if (aDeviceBmp.ImplGetSalBitmap())
    {
        Bitmap aNewBitmap;

        aNewBitmap = lcl_BlendBitmapWithAlpha(
                        aDeviceBmp, rBitmap, aDstRect.GetHeight(), aDstRect.GetWidth(),
                        aTradContext.mpMapX.get(), aTradContext.mpMapY.get() );

        DrawBitmap(aDstRect.TopLeft(), aNewBitmap);
    }

    mbMap = bOldMap;
    mpMetaFile = pOldMetaFile;
}

bool OutputDevice::HasFastDrawTransformedBitmap() const
{
    if( ImplIsRecordLayout() )
        return false;

    if (!mpGraphics && !AcquireGraphics())
        return false;
    assert(mpGraphics);

    return mpGraphics->HasFastDrawTransformedBitmap();
}

void OutputDevice::DrawImage( const Point& rPos, const Image& rImage, DrawImageFlags nStyle )
{
    assert(!is_double_buffered_window());

    DrawImage( rPos, Size(), rImage, nStyle );
}

void OutputDevice::DrawImage( const Point& rPos, const Size& rSize,
                              const Image& rImage, DrawImageFlags nStyle )
{
    assert(!is_double_buffered_window());

    bool bIsSizeValid = !rSize.IsEmpty();

    if (!ImplIsRecordLayout())
    {
        if (bIsSizeValid)
            rImage.Draw(this, rPos, nStyle, &rSize);
        else
            rImage.Draw(this, rPos, nStyle);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
