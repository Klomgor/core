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

#include <vcl/settings.hxx>
#include <vcl/outdev.hxx>
#include <vcl/graph.hxx>
#include <vcl/graphicfilter.hxx>
#include <vcl/image.hxx>
#include <sal/types.h>
#include <image.h>

#include <bitmap/BitmapColorizeFilter.hxx>

using namespace css;

Image::Image()
{
}

Image::Image(const Bitmap& rBitmap)
{
    ImplInit(rBitmap);
}

Image::Image(uno::Reference<graphic::XGraphic> const & rxGraphic)
{
    if (rxGraphic.is())
    {
        const Graphic aGraphic(rxGraphic);

        OUString aPath;
        if (aGraphic.getOriginURL().startsWith("private:graphicrepository/", &aPath))
            mpImplData = std::make_shared<ImplImage>(aPath);
        else if (aGraphic.GetType() == GraphicType::GdiMetafile)
            mpImplData = std::make_shared<ImplImage>(aGraphic.GetGDIMetaFile());
        else
            ImplInit(aGraphic.GetBitmap());
    }
}

Image::Image(const OUString & rFileUrl)
{
    OUString sImageName;
    if (rFileUrl.startsWith("private:graphicrepository/", &sImageName))
        mpImplData = std::make_shared<ImplImage>(sImageName);
    else
    {
        Graphic aGraphic;
        if (ERRCODE_NONE == GraphicFilter::LoadGraphic(rFileUrl, u"" IMP_PNG ""_ustr, aGraphic))
            ImplInit(aGraphic.GetBitmap());
    }
}

Image::Image(StockImage, const OUString & rFileUrl)
    : mpImplData(std::make_shared<ImplImage>(rFileUrl))
{
}

void Image::ImplInit(const Bitmap& rBitmap)
{
    if (!rBitmap.IsEmpty())
        mpImplData = std::make_shared<ImplImage>(rBitmap);
}

const OUString & Image::GetStock() const
{
    if (mpImplData)
        return mpImplData->getStock();
    return EMPTY_OUSTRING;
}

Size Image::GetSizePixel() const
{
    if (mpImplData)
        return mpImplData->getSizePixel();
    else
        return Size();
}

Bitmap Image::GetBitmap() const
{
    if (mpImplData)
        return mpImplData->getBitmap();
    else
        return Bitmap();
}

void Image::SetOptional(bool bValue)
{
    if (mpImplData)
    {
        mpImplData->SetOptional(bValue);
    }
}

bool Image::operator==(const Image& rImage) const
{
    bool bRet = false;

    if (rImage.mpImplData == mpImplData)
        bRet = true;
    else if (!rImage.mpImplData || !mpImplData)
        bRet = false;
    else
        bRet = rImage.mpImplData->isEqual(*mpImplData);

    return bRet;
}

void Image::Draw(OutputDevice* pOutDev, const Point& rPos, DrawImageFlags nStyle, const Size* pSize) const
{
    if (!mpImplData || (!pOutDev->IsDeviceOutputNecessary() && pOutDev->GetConnectMetaFile() == nullptr))
        return;

    Size aOutSize = pSize ? *pSize : pOutDev->PixelToLogic(mpImplData->getSizePixel());

    Bitmap aRenderBmp = mpImplData->getBitmapForHiDPI(bool(nStyle & DrawImageFlags::Disable), pOutDev->GetGraphics());

    if (!(nStyle & DrawImageFlags::Disable) &&
        (nStyle & (DrawImageFlags::ColorTransform | DrawImageFlags::Highlight |
                   DrawImageFlags::Deactive | DrawImageFlags::SemiTransparent)))
    {
        if (nStyle & (DrawImageFlags::Highlight | DrawImageFlags::Deactive))
        {
            const StyleSettings& rSettings = pOutDev->GetSettings().GetStyleSettings();
            Color aColor;
            if (nStyle & DrawImageFlags::Highlight)
                aColor = rSettings.GetHighlightColor();
            else
                aColor = rSettings.GetDeactiveColor();

            BitmapFilter::Filter(aRenderBmp, BitmapColorizeFilter(aColor));
        }

        if (nStyle & DrawImageFlags::SemiTransparent)
        {
            BitmapEx aTempBitmapEx(aRenderBmp);
            if (aTempBitmapEx.IsAlpha())
            {
                Bitmap aAlphaBmp(aTempBitmapEx.GetAlphaMask().GetBitmap());
                aAlphaBmp.Adjust(50);
                aTempBitmapEx = BitmapEx(aTempBitmapEx.GetBitmap(), AlphaMask(aAlphaBmp));
            }
            else
            {
                sal_uInt8 cErase = 128;
                aTempBitmapEx = BitmapEx(aTempBitmapEx.GetBitmap(), AlphaMask(aTempBitmapEx.GetSizePixel(), &cErase));
            }
            aRenderBmp = Bitmap(aTempBitmapEx);
        }
    }

    pOutDev->DrawBitmapEx(rPos, aOutSize, aRenderBmp);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
