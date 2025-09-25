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

#include <dpcontrol.hxx>

#include <vcl/outdev.hxx>
#include <vcl/settings.hxx>
#include <comphelper/lok.hxx>
#include <scitems.hxx>
#include <document.hxx>
#include <docpool.hxx>
#include <patattr.hxx>
#include <svtools/colorcfg.hxx>

ScDPFieldButton::ScDPFieldButton(OutputDevice* pOutDev, const StyleSettings& rStyle, const Fraction& rZoomY, ScDocument& rDoc):
    maZoomY(rZoomY),
    mrDoc(rDoc),
    mpOutDev(pOutDev),
    mrStyle(rStyle),
    mnToggleIndent(0),
    mbBaseButton(true),
    mbPopupButton(false),
    mbPopupButtonMulti(false),
    mbToggleButton(false),
    mbToggleCollapse(false),
    mbHasHiddenMember(false),
    mbPopupPressed(false),
    mbPopupLeft(false)
{
}

ScDPFieldButton::~ScDPFieldButton()
{
}

void ScDPFieldButton::setText(const OUString& rText)
{
    maText = rText;
}

void ScDPFieldButton::setBoundingBox(const Point& rPos, const Size& rSize, bool bLayoutRTL)
{
    maPos = rPos;
    maSize = rSize;
    if (bLayoutRTL)
    {
        // rPos is the logical-left position, adjust maPos to visual-left (inside the cell border)
        maPos.AdjustX( -(maSize.Width() - 1) );
    }
}

void ScDPFieldButton::setDrawBaseButton(bool b)
{
    mbBaseButton = b;
}

void ScDPFieldButton::setDrawPopupButton(bool b)
{
    mbPopupButton = b;
}

void ScDPFieldButton::setDrawPopupButtonMulti(bool b)
{
    mbPopupButtonMulti = b;
}

void ScDPFieldButton::setDrawToggleButton(bool b, bool bCollapse, sal_Int32 nIndent)
{
    mbToggleButton = b;
    mbToggleCollapse = bCollapse;
    mnToggleIndent = nIndent;
}

void ScDPFieldButton::setHasHiddenMember(bool b)
{
    mbHasHiddenMember = b;
}

void ScDPFieldButton::setPopupPressed(bool b)
{
    mbPopupPressed = b;
}

void ScDPFieldButton::setPopupLeft(bool b)
{
    mbPopupLeft = b;
}

void ScDPFieldButton::draw()
{
    bool bOldMapEnabled = mpOutDev->IsMapModeEnabled();

    if (mpOutDev->GetMapMode().GetMapUnit() != MapUnit::MapPixel)
        mpOutDev->EnableMapMode(false);

    if (mbBaseButton)
    {
        // Background
        tools::Rectangle aRect(maPos, maSize);
        mpOutDev->SetLineColor(mrStyle.GetFaceColor());
        mpOutDev->SetFillColor(mrStyle.GetFaceColor());
        mpOutDev->DrawRect(aRect);

        // Border lines
        mpOutDev->SetLineColor(mrStyle.GetLightColor());
        mpOutDev->DrawLine(maPos, Point(maPos.X(), maPos.Y()+maSize.Height()-1));
        mpOutDev->DrawLine(maPos, Point(maPos.X()+maSize.Width()-1, maPos.Y()));

        mpOutDev->SetLineColor(mrStyle.GetShadowColor());
        mpOutDev->DrawLine(Point(maPos.X(), maPos.Y()+maSize.Height()-1),
                           Point(maPos.X()+maSize.Width()-1, maPos.Y()+maSize.Height()-1));
        mpOutDev->DrawLine(Point(maPos.X()+maSize.Width()-1, maPos.Y()),
                           Point(maPos.X()+maSize.Width()-1, maPos.Y()+maSize.Height()-1));

        // Field name.
        // Get the font and size the same way as in scenario selection (lcl_DrawOneFrame in gridwin4.cxx)
        vcl::Font aTextFont( mrStyle.GetAppFont() );
        //  use ScPatternAttr::GetFont only for font size
        vcl::Font aAttrFont;
        mrDoc.getCellAttributeHelper().getDefaultCellAttribute().fillFontOnly(aAttrFont, mpOutDev, &maZoomY);
        aTextFont.SetFontSize(aAttrFont.GetFontSize());
        mpOutDev->SetFont(aTextFont);
        mpOutDev->SetTextColor(mrStyle.GetButtonTextColor());

        Point aTextPos = maPos;
        tools::Long nTHeight = mpOutDev->GetTextHeight();
        aTextPos.setX(maPos.getX() + 2); // 2 = Margin
        aTextPos.setY(maPos.getY() + (maSize.Height()-nTHeight)/2);

        auto popIt = mpOutDev->ScopedPush(vcl::PushFlags::CLIPREGION);
        mpOutDev->IntersectClipRegion(aRect);
        mpOutDev->DrawText(aTextPos, maText);
    }

    if (mbPopupButton || mbPopupButtonMulti)
        drawPopupButton();

    if (mbToggleButton)
        drawToggleButton();

    mpOutDev->EnableMapMode(bOldMapEnabled);
}

void ScDPFieldButton::getPopupBoundingBox(Point& rPos, Size& rSize) const
{
    float fScaleFactor = mpOutDev->GetDPIScaleFactor();

    tools::Long nMaxSize = 18 * fScaleFactor; // Button max size in either dimension

    tools::Long nW = std::min(maSize.getWidth() / 2, nMaxSize);
    tools::Long nH = std::min(maSize.getHeight(),    nMaxSize);

    double fZoom = static_cast<double>(maZoomY) > 1.0 ? static_cast<double>(maZoomY) : 1.0;
    if (fZoom > 1.0)
    {
        nW = fZoom * (nW - 1);
        nH = fZoom * (nH - 1);
    }

    // #i114944# AutoFilter button is left-aligned in RTL.
    // DataPilot button is always right-aligned for now, so text output isn't affected.
    if (mbPopupLeft)
        rPos.setX(maPos.getX());
    else
        rPos.setX(maPos.getX() + maSize.getWidth() - nW);

    rPos.setY(maPos.getY() + maSize.getHeight() - nH);
    rSize.setWidth(nW);
    rSize.setHeight(nH);
}

void ScDPFieldButton::getToggleBoundingBox(Point& rPos, Size& rSize) const
{
    const float fScaleFactor = mpOutDev->GetDPIScaleFactor();

    tools::Long nMaxSize = 13 * fScaleFactor; // Button max size in either dimension
    tools::Long nMargin = 3 * fScaleFactor;

    tools::Long nIndent = fScaleFactor * o3tl::convert(mnToggleIndent, o3tl::Length::twip, o3tl::Length::px);
    tools::Long nW = std::min(maSize.getWidth() / 2, nMaxSize);
    tools::Long nH = std::min(maSize.getHeight(),    nMaxSize);
    nIndent = std::min(nIndent, maSize.getWidth());

    double fZoom = static_cast<double>(maZoomY) > 1.0 ? static_cast<double>(maZoomY) : 1.0;
    if (fZoom > 1.0)
    {
        nW = fZoom * (nW - 1);
        nH = fZoom * (nH - 1);
        nIndent = fZoom * (nIndent -1);
        nMargin = fZoom * (nMargin - 1);
    }

    // FIXME: RTL case ?
    rPos.setX(maPos.getX() + nIndent - nW + nMargin);
    rPos.setY(maPos.getY() + maSize.getHeight() / 2 - nH / 2 + nMargin);
    rSize.setWidth(nW - nMargin - 1);
    rSize.setHeight(nH - nMargin - 1);
}

void ScDPFieldButton::drawPopupButton()
{
    Point aPos;
    Size aSize;
    getPopupBoundingBox(aPos, aSize);

    Color aDocColor = svtools::ColorConfig().GetColorValue(svtools::DOCCOLOR).nColor;
    Color aAccentColor = svtools::ColorConfig().GetColorValue(svtools::ACCENTCOLOR).nColor;

    Color aBackgroundColor = mbPopupPressed      ? mrStyle.GetFaceColor()
                             : mbHasHiddenMember ? aAccentColor
                                                 : aDocColor;

    mpOutDev->SetFillColor(aBackgroundColor);
    mpOutDev->SetLineColor();
    mpOutDev->DrawRect(tools::Rectangle(aPos, aSize));

    const double nVDist = aSize.Height() / 4.0;
    const double nHDist = aSize.Width() / 10.0;
    const double fZoom = static_cast<double>(maZoomY);

    aBackgroundColor = mbPopupPressed      ? mrStyle.GetButtonTextColor()
                       : mbHasHiddenMember ? aDocColor
                                           : aAccentColor;
    mpOutDev->SetFillColor(aBackgroundColor);
    for (int i = 0; i < 3; i++)
    {
        tools::Rectangle aRect;
        aRect.SetTop(aPos.Y() + nVDist * (i + 1));
        aRect.SetBottom(aPos.Y() + nVDist * (i + 1) + fZoom);
        aRect.SetLeft(aPos.X() + nHDist + i * 3 * fZoom);
        aRect.SetRight(aPos.X() + aSize.Width() - nHDist - i * 3 * fZoom);
        mpOutDev->DrawRect(aRect);
    }
}

void ScDPFieldButton::drawToggleButton()
{
    Point aPos;
    Size aSize;
    getToggleBoundingBox(aPos, aSize);

    // Background & outer black border
    mpOutDev->SetLineColor(COL_BLACK);
    mpOutDev->SetFillColor();
    mpOutDev->DrawRect(tools::Rectangle(aPos, aSize));

    Point aCenter(aPos.X() + aSize.getWidth() / 2, aPos.Y() + aSize.getHeight() / 2);

    mpOutDev->DrawLine(
        Point(aPos.X() + 2, aCenter.Y()),
        Point(aPos.X() + aSize.getWidth() - 2, aCenter.Y()));

    if (!mbToggleCollapse)
    {
        mpOutDev->DrawLine(
            Point(aCenter.X(), aPos.Y() + 2),
            Point(aCenter.X(), aPos.Y() + aSize.getHeight() - 2));
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
