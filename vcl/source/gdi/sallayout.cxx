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

#include <iostream>
#include <iomanip>

#include <sal/log.hxx>

#include <cstdio>

#include <math.h>

#include <ImplLayoutArgs.hxx>
#include <salgdi.hxx>
#include <sallayout.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/matrix/b2dhommatrixtools.hxx>

#include <i18nlangtag/lang.h>

#include <vcl/svapp.hxx>

#include <algorithm>
#include <memory>

#include <impglyphitem.hxx>

// Glyph Flags
#define GF_FONTMASK  0xF0000000
#define GF_FONTSHIFT 28


sal_UCS4 GetLocalizedChar( sal_UCS4 nChar, LanguageType eLang )
{
    // currently only conversion from ASCII digits is interesting
    if( (nChar < '0') || ('9' < nChar) )
        return nChar;

    int nOffset;
    // eLang & LANGUAGE_MASK_PRIMARY catches language independent of region.
    // CAVEAT! To some like Mongolian MS assigned the same primary language
    // although the script type is different!
    LanguageType pri = primary(eLang);
    if( pri == primary(LANGUAGE_ARABIC_SAUDI_ARABIA) )
        nOffset = 0x0660 - '0';  // arabic-indic digits
    else if ( pri.anyOf(
        primary(LANGUAGE_FARSI),
        primary(LANGUAGE_URDU_PAKISTAN),
        primary(LANGUAGE_PUNJABI), //???
        primary(LANGUAGE_SINDHI)))
        nOffset = 0x06F0 - '0';  // eastern arabic-indic digits
    else if ( pri == primary(LANGUAGE_BENGALI) )
        nOffset = 0x09E6 - '0';  // bengali
    else if ( pri == primary(LANGUAGE_HINDI) )
        nOffset = 0x0966 - '0';  // devanagari
    else if ( pri.anyOf(
        primary(LANGUAGE_AMHARIC_ETHIOPIA),
        primary(LANGUAGE_TIGRIGNA_ETHIOPIA)))
        // TODO case:
        nOffset = 0x1369 - '0';  // ethiopic
    else if ( pri == primary(LANGUAGE_GUJARATI) )
        nOffset = 0x0AE6 - '0';  // gujarati
#ifdef LANGUAGE_GURMUKHI // TODO case:
    else if ( pri == primary(LANGUAGE_GURMUKHI) )
        nOffset = 0x0A66 - '0';  // gurmukhi
#endif
    else if ( pri == primary(LANGUAGE_KANNADA) )
        nOffset = 0x0CE6 - '0';  // kannada
    else if ( pri == primary(LANGUAGE_KHMER))
        nOffset = 0x17E0 - '0';  // khmer
    else if ( pri == primary(LANGUAGE_LAO) )
        nOffset = 0x0ED0 - '0';  // lao
    else if ( pri == primary(LANGUAGE_MALAYALAM) )
        nOffset = 0x0D66 - '0';  // malayalam
    else if ( pri == primary(LANGUAGE_MONGOLIAN_MONGOLIAN_LSO))
    {
        if (eLang.anyOf(
             LANGUAGE_MONGOLIAN_MONGOLIAN_MONGOLIA,
             LANGUAGE_MONGOLIAN_MONGOLIAN_CHINA,
             LANGUAGE_MONGOLIAN_MONGOLIAN_LSO))
                nOffset = 0x1810 - '0';   // mongolian
        else
                nOffset = 0;              // mongolian cyrillic
    }
    else if ( pri == primary(LANGUAGE_BURMESE) )
        nOffset = 0x1040 - '0';  // myanmar
    else if ( pri == primary(LANGUAGE_ODIA) )
        nOffset = 0x0B66 - '0';  // odia
    else if ( pri == primary(LANGUAGE_TAMIL) )
        nOffset = 0x0BE7 - '0';  // tamil
    else if ( pri == primary(LANGUAGE_TELUGU) )
        nOffset = 0x0C66 - '0';  // telugu
    else if ( pri == primary(LANGUAGE_THAI) )
        nOffset = 0x0E50 - '0';  // thai
    else if ( pri == primary(LANGUAGE_TIBETAN) )
        nOffset = 0x0F20 - '0';  // tibetan
    else
    {
        nOffset = 0;
    }

    nChar += nOffset;
    return nChar;
}

SalLayout::SalLayout()
:   mnMinCharPos( -1 ),
    mnEndCharPos( -1 ),
    maLanguageTag( LANGUAGE_DONTKNOW ),
    mnOrientation( 0 ),
    maDrawOffset( 0, 0 ),
    mbSubpixelPositioning(false)
{}

SalLayout::~SalLayout()
{}

void SalLayout::AdjustLayout( vcl::text::ImplLayoutArgs& rArgs )
{
    mnMinCharPos  = rArgs.mnMinCharPos;
    mnEndCharPos  = rArgs.mnEndCharPos;
    mnOrientation = rArgs.mnOrientation;
    maLanguageTag = rArgs.maLanguageTag;
}

basegfx::B2DPoint SalLayout::GetDrawPosition(const basegfx::B2DPoint& rRelative) const
{
    basegfx::B2DPoint aPos{maDrawBase};
    basegfx::B2DPoint aOfs = rRelative + maDrawOffset;

    if( mnOrientation == 0_deg10 )
        aPos += aOfs;
    else
    {
        // cache trigonometric results
        static Degree10 nOldOrientation(0);
        static double fCos = 1.0, fSin = 0.0;
        if( nOldOrientation != mnOrientation )
        {
            nOldOrientation = mnOrientation;
            double fRad = toRadians(mnOrientation);
            fCos = cos( fRad );
            fSin = sin( fRad );
        }

        double fX = aOfs.getX();
        double fY = aOfs.getY();
        if (mbSubpixelPositioning)
        {
            double nX = +fCos * fX + fSin * fY;
            double nY = +fCos * fY - fSin * fX;
            aPos += basegfx::B2DPoint(nX, nY);
        }
        else
        {
            tools::Long nX = static_cast<tools::Long>( +fCos * fX + fSin * fY );
            tools::Long nY = static_cast<tools::Long>( +fCos * fY - fSin * fX );
            aPos += basegfx::B2DPoint(nX, nY);
        }
    }

    return aPos;
}

bool SalLayout::GetOutline(basegfx::B2DPolyPolygonVector& rVector) const
{
    bool bAllOk = true;
    bool bOneOk = false;

    basegfx::B2DPolyPolygon aGlyphOutline;

    basegfx::B2DPoint aPos;
    const GlyphItem* pGlyph;
    int nStart = 0;
    const LogicalFontInstance* pGlyphFont;
    while (GetNextGlyph(&pGlyph, aPos, nStart, &pGlyphFont))
    {
        // get outline of individual glyph, ignoring "empty" glyphs
        bool bSuccess = pGlyph->GetGlyphOutline(pGlyphFont, aGlyphOutline);
        bAllOk &= bSuccess;
        bOneOk |= bSuccess;
        // only add non-empty outlines
        if( bSuccess && (aGlyphOutline.count() > 0) )
        {
            if( aPos.getX() || aPos.getY() )
            {
                aGlyphOutline.transform(basegfx::utils::createTranslateB2DHomMatrix(aPos.getX(), aPos.getY()));
            }

            // insert outline at correct position
            rVector.push_back( aGlyphOutline );
        }
    }

    return (bAllOk && bOneOk);
}

// No need to expand to the next pixel, when the character only covers its tiny fraction
static double trimInsignificant(double n)
{
    return std::abs(n) >= 0x1p53 ? n : std::round(n * 1e5) / 1e5;
}

bool SalLayout::GetBoundRect(basegfx::B2DRectangle& rRect) const
{
    bool bRet = false;
    rRect.reset();
    basegfx::B2DRectangle aRectangle;

    basegfx::B2DPoint aPos;
    const GlyphItem* pGlyph;
    int nStart = 0;
    const LogicalFontInstance* pGlyphFont;
    while (GetNextGlyph(&pGlyph, aPos, nStart, &pGlyphFont))
    {
        // get bounding rectangle of individual glyph
        if (pGlyph->GetGlyphBoundRect(pGlyphFont, aRectangle))
        {
            if (!aRectangle.isEmpty())
            {
                // translate rectangle to correct position
                aRectangle.translate(aPos);
                // merge rectangle
                rRect.expand(aRectangle);
            }
            bRet = true;
        }
    }

    return bRet;
}

tools::Rectangle SalLayout::BoundRect2Rectangle(const basegfx::B2DRectangle& rRect)
{
    if (rRect.isEmpty())
        return {};

    double l = rtl::math::approxFloor(trimInsignificant(rRect.getMinX())),
           t = rtl::math::approxFloor(trimInsignificant(rRect.getMinY())),
           r = rtl::math::approxCeil(trimInsignificant(rRect.getMaxX())),
           b = rtl::math::approxCeil(trimInsignificant(rRect.getMaxY()));
    assert(std::isfinite(l) && std::isfinite(t) && std::isfinite(r) && std::isfinite(b));
    return tools::Rectangle(l, t, r, b);
}

SalLayoutGlyphs SalLayout::GetGlyphs() const
{
    return SalLayoutGlyphs(); // invalid
}

double GenericSalLayout::FillDXArray( std::vector<double>* pCharWidths, const OUString& rStr ) const
{
    if (pCharWidths)
        GetCharWidths(*pCharWidths, rStr);

    return GetTextWidth();
}

double GenericSalLayout::FillPartialDXArray(std::vector<double>* pCharWidths, const OUString& rStr,
                                            sal_Int32 skipStart, sal_Int32 amt) const
{
    if (pCharWidths)
    {
        GetCharWidths(*pCharWidths, rStr);

        // Strip excess characters from the array
        if (skipStart < static_cast<sal_Int32>(pCharWidths->size()))
        {
            std::copy(pCharWidths->begin() + skipStart, pCharWidths->end(), pCharWidths->begin());
        }

        pCharWidths->resize(amt, 0.0);
    }

    return GetPartialTextWidth(skipStart, amt);
}

// the text width is the maximum logical extent of all glyphs
double GenericSalLayout::GetTextWidth() const
{
    if (!m_GlyphItems.IsValid())
        return 0;

    double nWidth = 0;
    for (auto const& aGlyphItem : m_GlyphItems)
        nWidth += aGlyphItem.newWidth();

    return nWidth;
}

double GenericSalLayout::GetPartialTextWidth(sal_Int32 skipStart, sal_Int32 amt) const
{
    if (!m_GlyphItems.IsValid())
    {
        return 0;
    }

    auto skipEnd = skipStart + amt;
    double nWidth = 0.0;
    for (auto const& aGlyphItem : m_GlyphItems)
    {
        auto pos = aGlyphItem.charPos();
        if (pos >= skipStart && pos < skipEnd)
        {
            nWidth += aGlyphItem.newWidth();
        }
    }

    return nWidth;
}

void GenericSalLayout::Justify(double nNewWidth)
{
    double nOldWidth = GetTextWidth();
    if( !nOldWidth || nNewWidth==nOldWidth )
        return;

    if (!m_GlyphItems.IsValid())
    {
        return;
    }
    // find rightmost glyph, it won't get stretched
    std::vector<GlyphItem>::iterator pGlyphIterRight = m_GlyphItems.begin();
    pGlyphIterRight += m_GlyphItems.size() - 1;
    std::vector<GlyphItem>::iterator pGlyphIter;
    // count stretchable glyphs
    int nStretchable = 0;
    double nMaxGlyphWidth = 0.0;
    for(pGlyphIter = m_GlyphItems.begin(); pGlyphIter != pGlyphIterRight; ++pGlyphIter)
    {
        if( !pGlyphIter->IsInCluster() )
            ++nStretchable;
        if (nMaxGlyphWidth < pGlyphIter->origWidth())
            nMaxGlyphWidth = pGlyphIter->origWidth();
    }

    // move rightmost glyph to requested position
    auto nRightGlyphOffset = nOldWidth - pGlyphIterRight->linearPos().getX();
    nOldWidth -= nRightGlyphOffset;

    if( nOldWidth <= 0.0 )
        return;
    if( nNewWidth < nMaxGlyphWidth)
        nNewWidth = nMaxGlyphWidth;
    nNewWidth -= nRightGlyphOffset;
    pGlyphIterRight->setLinearPosX( nNewWidth );

    // justify glyph widths and positions
    double nDiffWidth = nNewWidth - nOldWidth;
    if( nDiffWidth >= 0.0 ) // expanded case
    {
        // expand width by distributing space between glyphs evenly
        double nDeltaSum = 0.0;
        for( pGlyphIter = m_GlyphItems.begin(); pGlyphIter != pGlyphIterRight; ++pGlyphIter )
        {
            // move glyph to justified position
            pGlyphIter->adjustLinearPosX(nDeltaSum);

            // do not stretch non-stretchable glyphs
            if( pGlyphIter->IsInCluster() || (nStretchable <= 0) )
                continue;

            // distribute extra space equally to stretchable glyphs
            double nDeltaWidth = nDiffWidth / nStretchable--;
            nDiffWidth     -= nDeltaWidth;
            pGlyphIter->addNewWidth(nDeltaWidth);
            nDeltaSum      += nDeltaWidth;
        }
    }
    else // condensed case
    {
        // squeeze width by moving glyphs proportionally
        double fSqueeze = nNewWidth / nOldWidth;
        if(m_GlyphItems.size() > 1)
        {
            for( pGlyphIter = m_GlyphItems.begin(); ++pGlyphIter != pGlyphIterRight;)
            {
                double nX = pGlyphIter->linearPos().getX();
                nX = nX * fSqueeze;
                pGlyphIter->setLinearPosX( nX );
            }
        }
        // adjust glyph widths to new positions
        for( pGlyphIter = m_GlyphItems.begin(); pGlyphIter != pGlyphIterRight; ++pGlyphIter )
            pGlyphIter->setNewWidth( pGlyphIter[1].linearPos().getX() - pGlyphIter[0].linearPos().getX());
    }
}

// returns asian kerning values in quarter of character width units
// to enable automatic halfwidth substitution for fullwidth punctuation
// return value is negative for l, positive for r, zero for neutral
// TODO: handle vertical layout as proposed in commit 43bf2ad49c2b3989bbbe893e4fee2e032a3920f5?
static int lcl_CalcAsianKerning(sal_UCS4 c, bool bLeft)
{
    // http://www.asahi-net.or.jp/~sd5a-ucd/freetexts/jis/x4051/1995/appendix.html
    static const signed char nTable[0x30] =
    {
         0, -2, -2,  0,   0,  0,  0,  0,  +2, -2, +2, -2,  +2, -2, +2, -2,
        +2, -2,  0,  0,  +2, -2, +2, -2,   0,  0,  0,  0,   0, +2, -2, -2,
         0,  0,  0,  0,   0,  0,  0,  0,   0,  0, -2, -2,  +2, +2, -2, -2
    };

    int nResult = 0;
    if( (c >= 0x3000) && (c < 0x3030) )
        nResult = nTable[ c - 0x3000 ];
    else switch( c )
    {
        case 0x30FB:
            nResult = bLeft ? -1 : +1;      // 25% left/right/top/bottom
            break;
        case 0x2019: case 0x201D:
        case 0xFF01: case 0xFF09: case 0xFF0C:
        case 0xFF1A: case 0xFF1B:
            nResult = -2;
            break;
        case 0x2018: case 0x201C:
        case 0xFF08:
            nResult = +2;
            break;
        default:
            break;
    }

    return nResult;
}

static bool lcl_CanApplyAsianKerning(sal_Unicode cp)
{
    return (0x3000 == (cp & 0xFF00)) || (0xFF00 == (cp & 0xFF00)) || (0x2010 == (cp & 0xFFF0));
}

void GenericSalLayout::ApplyAsianKerning(std::u16string_view rStr)
{
    const int nLength = rStr.size();
    double nOffset = 0;

    for (std::vector<GlyphItem>::iterator pGlyphIter = m_GlyphItems.begin(),
                                          pGlyphIterEnd = m_GlyphItems.end();
         pGlyphIter != pGlyphIterEnd; ++pGlyphIter)
    {
        const int n = pGlyphIter->charPos();
        if (n < nLength - 1)
        {
            // ignore code ranges that are not affected by asian punctuation compression
            const sal_Unicode cCurrent = rStr[n];
            if (!lcl_CanApplyAsianKerning(cCurrent))
                continue;
            const sal_Unicode cNext = rStr[n + 1];
            if (!lcl_CanApplyAsianKerning(cNext))
                continue;

            // calculate compression values
            const int nKernCurrent = +lcl_CalcAsianKerning(cCurrent, true);
            if (nKernCurrent == 0)
                continue;
            const int nKernNext = -lcl_CalcAsianKerning(cNext, false);
            if (nKernNext == 0)
                continue;

            // apply punctuation compression to logical glyph widths
            double nDelta = (nKernCurrent < nKernNext) ? nKernCurrent : nKernNext;
            if (nDelta < 0)
            {
                nDelta = (nDelta * pGlyphIter->origWidth() + 2) / 4;
                if( pGlyphIter+1 == pGlyphIterEnd )
                    pGlyphIter->addNewWidth( nDelta );
                nOffset += nDelta;
            }
        }

        // adjust the glyph positions to the new glyph widths
        if( pGlyphIter+1 != pGlyphIterEnd )
            pGlyphIter->adjustLinearPosX(nOffset);
    }
}

void GenericSalLayout::GetCaretPositions(std::vector<double>& rCaretPositions,
                                         const OUString& rStr) const
{
    const int nCaretPositions = (mnEndCharPos - mnMinCharPos) * 2;

    rCaretPositions.clear();
    rCaretPositions.resize(nCaretPositions, -1);

    if (m_GlyphItems.empty())
        return;

    std::vector<double> aCharWidths;
    GetCharWidths(aCharWidths, rStr);

    // calculate caret positions using glyph array
    for (auto const& aGlyphItem : m_GlyphItems)
    {
        auto nCurrX = aGlyphItem.linearPos().getX() - aGlyphItem.xOffset();
        auto nCharStart = aGlyphItem.charPos();
        auto nCharEnd = nCharStart + aGlyphItem.charCount() - 1;
        if (!aGlyphItem.IsRTLGlyph())
        {
            // unchanged positions for LTR case
            for (int i = nCharStart; i <= nCharEnd; i++)
            {
                int n = i - mnMinCharPos;
                int nCurrIdx = 2 * n;

                auto nLeft = nCurrX;
                nCurrX += aCharWidths[n];
                auto nRight = nCurrX;

                rCaretPositions[nCurrIdx] = nLeft;
                rCaretPositions[nCurrIdx + 1] = nRight;
            }
        }
        else
        {
            // reverse positions for RTL case
            for (int i = nCharEnd; i >= nCharStart; i--)
            {
                int n = i - mnMinCharPos;
                int nCurrIdx = 2 * n;

                auto nRight = nCurrX;
                nCurrX += aCharWidths[n];
                auto nLeft = nCurrX;

                rCaretPositions[nCurrIdx] = nLeft;
                rCaretPositions[nCurrIdx + 1] = nRight;
            }
        }
    }
}

sal_Int32 GenericSalLayout::GetTextBreak(double nMaxWidth, double nCharExtra, int nFactor) const
{
    std::vector<double> aCharWidths;
    GetCharWidths(aCharWidths, {});

    double nWidth = 0;
    for( int i = mnMinCharPos; i < mnEndCharPos; ++i )
    {
        double nDelta =  aCharWidths[ i - mnMinCharPos ] * nFactor;

        if (nDelta != 0)
        {
            nWidth += nDelta;
            if( nWidth > nMaxWidth )
                return i;

            nWidth += nCharExtra;
        }
    }

    return -1;
}

bool GenericSalLayout::GetNextGlyph(const GlyphItem** pGlyph,
                                    basegfx::B2DPoint& rPos, int& nStart,
                                    const LogicalFontInstance** ppGlyphFont) const
{
    std::vector<GlyphItem>::const_iterator pGlyphIter = m_GlyphItems.begin();
    std::vector<GlyphItem>::const_iterator pGlyphIterEnd = m_GlyphItems.end();
    pGlyphIter += nStart;

    // find next glyph in substring
    for(; pGlyphIter != pGlyphIterEnd; ++nStart, ++pGlyphIter )
    {
        int n = pGlyphIter->charPos();
        if( (mnMinCharPos <= n) && (n < mnEndCharPos) )
            break;
    }

    // return zero if no more glyph found
    if( nStart >= static_cast<int>(m_GlyphItems.size()) )
        return false;

    if( pGlyphIter == pGlyphIterEnd )
        return false;

    // update return data with glyph info
    *pGlyph = &(*pGlyphIter);
    ++nStart;
    if (ppGlyphFont)
        *ppGlyphFont = m_GlyphItems.GetFont().get();

    // calculate absolute position in pixel units
    basegfx::B2DPoint aRelativePos = pGlyphIter->linearPos();

    rPos = GetDrawPosition( aRelativePos );

    return true;
}

void GenericSalLayout::MoveGlyph(int nStart, double nNewXPos)
{
    if( nStart >= static_cast<int>(m_GlyphItems.size()) )
        return;

    std::vector<GlyphItem>::iterator pGlyphIter = m_GlyphItems.begin();
    pGlyphIter += nStart;

    // the nNewXPos argument determines the new cell position
    // as RTL-glyphs are right justified in their cell
    // the cell position needs to be adjusted to the glyph position
    if( pGlyphIter->IsRTLGlyph() )
        nNewXPos += pGlyphIter->newWidth() - pGlyphIter->origWidth();
    // calculate the x-offset to the old position
    double nXDelta = nNewXPos - pGlyphIter->linearPos().getX() + pGlyphIter->xOffset();
    // adjust all following glyph positions if needed
    if( nXDelta != 0 )
    {
        for( std::vector<GlyphItem>::iterator pGlyphIterEnd = m_GlyphItems.end(); pGlyphIter != pGlyphIterEnd; ++pGlyphIter )
        {
            pGlyphIter->adjustLinearPosX(nXDelta);
        }
    }
}

void GenericSalLayout::DropGlyph( int nStart )
{
    if( nStart >= static_cast<int>(m_GlyphItems.size()))
        return;

    std::vector<GlyphItem>::iterator pGlyphIter = m_GlyphItems.begin();
    pGlyphIter += nStart;
    pGlyphIter->dropGlyph();
}

void GenericSalLayout::Simplify( bool bIsBase )
{
    // remove dropped glyphs inplace
    size_t j = 0;
    for(size_t i = 0; i < m_GlyphItems.size(); i++ )
    {
        if (bIsBase && m_GlyphItems[i].IsDropped())
            continue;
        if (!bIsBase && m_GlyphItems[i].glyphId() == 0)
            continue;

        if( i != j )
        {
            m_GlyphItems[j] = m_GlyphItems[i];
        }
        j += 1;
    }
    m_GlyphItems.erase(m_GlyphItems.begin() + j, m_GlyphItems.end());
}

MultiSalLayout::MultiSalLayout( std::unique_ptr<SalLayout> pBaseLayout )
:   mnLevel( 1 )
,   mbIncomplete( false )
{
    assert(dynamic_cast<GenericSalLayout*>(pBaseLayout.get()));

    mpLayouts[ 0 ].reset(static_cast<GenericSalLayout*>(pBaseLayout.release()));
}

std::unique_ptr<SalLayout> MultiSalLayout::ReleaseBaseLayout()
{
    return std::move(mpLayouts[0]);
}

void MultiSalLayout::SetIncomplete(bool bIncomplete)
{
    mbIncomplete = bIncomplete;
    maFallbackRuns[mnLevel-1] = ImplLayoutRuns();
}

MultiSalLayout::~MultiSalLayout()
{
}

void MultiSalLayout::AddFallback( std::unique_ptr<SalLayout> pFallback,
    ImplLayoutRuns const & rFallbackRuns)
{
    assert(dynamic_cast<GenericSalLayout*>(pFallback.get()));
    if( mnLevel >= MAX_FALLBACK )
        return;

    mpLayouts[ mnLevel ].reset(static_cast<GenericSalLayout*>(pFallback.release()));
    maFallbackRuns[ mnLevel-1 ] = rFallbackRuns;
    ++mnLevel;
}

bool MultiSalLayout::LayoutText( vcl::text::ImplLayoutArgs& rArgs, const SalLayoutGlyphsImpl* )
{
    if( mnLevel <= 1 )
        return false;
    if (!mbIncomplete)
        maFallbackRuns[ mnLevel-1 ] = rArgs.maRuns;
    return true;
}

void MultiSalLayout::AdjustLayout( vcl::text::ImplLayoutArgs& rArgs )
{
    SalLayout::AdjustLayout( rArgs );
    vcl::text::ImplLayoutArgs aMultiArgs = rArgs;
    std::vector<double> aJustificationArray;

    if (!rArgs.mstJustification.empty() && rArgs.mnLayoutWidth)
    {
        // for stretched text in a MultiSalLayout the target width needs to be
        // distributed by individually adjusting its virtual character widths
        double nTargetWidth = aMultiArgs.mnLayoutWidth;
        aMultiArgs.mnLayoutWidth = 0;

        // we need to get the original unmodified layouts ready
        for( int n = 0; n < mnLevel; ++n )
            mpLayouts[n]->SalLayout::AdjustLayout( aMultiArgs );
        // then we can measure the unmodified metrics
        int nCharCount = rArgs.mnEndCharPos - rArgs.mnMinCharPos;
        FillDXArray( &aJustificationArray, {} );
        // #i17359# multilayout is not simplified yet, so calculating the
        // unjustified width needs handholding; also count the number of
        // stretchable virtual char widths
        double nOrigWidth = 0;
        int nStretchable = 0;
        for( int i = 0; i < nCharCount; ++i )
        {
            // convert array from widths to sum of widths
            nOrigWidth += aJustificationArray[i];
            if( aJustificationArray[i] > 0 )
                ++nStretchable;
        }

        // now we are able to distribute the extra width over the virtual char widths
        if( nOrigWidth && (nTargetWidth != nOrigWidth) )
        {
            double nDiffWidth = nTargetWidth - nOrigWidth;
            double nWidthSum = 0;
            for( int i = 0; i < nCharCount; ++i )
            {
                double nJustWidth = aJustificationArray[i];
                if( (nJustWidth > 0) && (nStretchable > 0) )
                {
                    double nDeltaWidth = nDiffWidth / nStretchable;
                    nJustWidth += nDeltaWidth;
                    nDiffWidth -= nDeltaWidth;
                    --nStretchable;
                }
                nWidthSum += nJustWidth;
                aJustificationArray[i] = nWidthSum;
            }
            if( nWidthSum != nTargetWidth )
                aJustificationArray[ nCharCount-1 ] = nTargetWidth;

            // change the DXArray temporarily (just for the justification)
            JustificationData stJustData{ rArgs.mnMinCharPos, nCharCount };
            for (sal_Int32 i = 0; i < nCharCount; ++i)
            {
                stJustData.SetTotalAdvance(rArgs.mnMinCharPos + i, aJustificationArray[i]);
            }

            aMultiArgs.SetJustificationData(std::move(stJustData));
        }
    }

    ImplAdjustMultiLayout(rArgs, aMultiArgs, aMultiArgs.mstJustification);
}

void MultiSalLayout::ImplAdjustMultiLayout(vcl::text::ImplLayoutArgs& rArgs,
                                           vcl::text::ImplLayoutArgs& rMultiArgs,
                                           const JustificationData& rstJustification)
{
    // Compute rtl flags, since in some scripts glyphs/char order can be
    // reversed for a few character sequences e.g. Myanmar
    std::vector<bool> vRtl(rArgs.mnEndCharPos - rArgs.mnMinCharPos, false);
    rArgs.ResetPos();
    bool bRtl;
    int nRunStart, nRunEnd;
    while (rArgs.GetNextRun(&nRunStart, &nRunEnd, &bRtl))
    {
        if (bRtl) std::fill(vRtl.begin() + (nRunStart - rArgs.mnMinCharPos),
                            vRtl.begin() + (nRunEnd - rArgs.mnMinCharPos), true);
    }
    rArgs.ResetPos();

    // prepare "merge sort"
    int nStartOld[ MAX_FALLBACK ];
    int nStartNew[ MAX_FALLBACK ];
    const GlyphItem* pGlyphs[MAX_FALLBACK];
    bool bValid[MAX_FALLBACK] = { false };

    basegfx::B2DPoint aPos;
    int nLevel = 0, n;
    for( n = 0; n < mnLevel; ++n )
    {
        // now adjust the individual components
        if( n > 0 )
        {
            rMultiArgs.maRuns = maFallbackRuns[ n-1 ];
            rMultiArgs.mnFlags |= SalLayoutFlags::ForFallback;
        }
        mpLayouts[n]->AdjustLayout( rMultiArgs );

        // remove unused parts of component
        if( n > 0 )
        {
            if (mbIncomplete && (n == mnLevel-1))
                mpLayouts[n]->Simplify( true );
            else
                mpLayouts[n]->Simplify( false );
        }

        // prepare merging components
        nStartNew[ nLevel ] = nStartOld[ nLevel ] = 0;
        bValid[nLevel] = mpLayouts[n]->GetNextGlyph(&pGlyphs[nLevel], aPos, nStartNew[nLevel]);

        if( (n > 0) && !bValid[ nLevel ] )
        {
            // an empty fallback layout can be released
            mpLayouts[n].reset();
        }
        else
        {
            // reshuffle used fallbacks if needed
            if( nLevel != n )
            {
                mpLayouts[ nLevel ]         = std::move(mpLayouts[ n ]);
                maFallbackRuns[ nLevel ]    = maFallbackRuns[ n ];
            }
            ++nLevel;
        }
    }
    mnLevel = nLevel;

    // prepare merge the fallback levels
    double nXPos = 0;
    for( n = 0; n < nLevel; ++n )
        maFallbackRuns[n].ResetPos();

    int nFirstValid = -1;
    for( n = 0; n < nLevel; ++n )
    {
        if(bValid[n])
        {
            nFirstValid = n;
            break;
        }
    }
    assert(nFirstValid >= 0);

    // get the next codepoint index that needs fallback
    int nActiveCharPos = pGlyphs[nFirstValid]->charPos();
    int nActiveCharIndex = nActiveCharPos - mnMinCharPos;
    // get the end index of the active run
    int nLastRunEndChar = (nActiveCharIndex >= 0 && vRtl[nActiveCharIndex]) ?
        rArgs.mnEndCharPos : rArgs.mnMinCharPos - 1;
    int nRunVisibleEndChar = pGlyphs[nFirstValid]->charPos();
    // merge the fallback levels
    while( bValid[nFirstValid] && (nLevel > 0))
    {
        // find best fallback level
        for( n = 0; n < nLevel; ++n )
            if( bValid[n] && !maFallbackRuns[n].PosIsInAnyRun( nActiveCharPos ) )
                // fallback level n wins when it requested no further fallback
                break;
        int nFBLevel = n;

        if( n < nLevel )
        {
            // use base(n==0) or fallback(n>=1) level
            mpLayouts[n]->MoveGlyph( nStartOld[n], nXPos );
        }
        else
        {
            n = 0;  // keep NotDef in base level
        }

        if( n > 0 )
        {
            // drop the NotDef glyphs in the base layout run if a fallback run exists
            //
            // tdf#163761: The whole algorithm in this outer loop works by advancing through
            // all of the glyphs and runs in lock-step. The current glyph in the base layout
            // must not outpace the fallback runs. The following loop does this by breaking
            // at the end of the current fallback run (which comes from the previous level).
            while ((maFallbackRuns[n - 1].PosIsInRun(pGlyphs[nFirstValid]->charPos()))
                   && (!maFallbackRuns[n].PosIsInAnyRun(pGlyphs[nFirstValid]->charPos())))
            {
                mpLayouts[0]->DropGlyph( nStartOld[0] );
                nStartOld[0] = nStartNew[0];
                bValid[nFirstValid] = mpLayouts[0]->GetNextGlyph(&pGlyphs[nFirstValid], aPos, nStartNew[0]);

                if( !bValid[nFirstValid] )
                   break;
            }
        }

        // skip to end of layout run and calculate its advance width
        double nRunAdvance = 0;
        bool bKeepNotDef = (nFBLevel >= nLevel);
        for(;;)
        {
            // check for reordered glyphs
            // tdf#154104: Moved this up in the loop body to handle the case of single-glyph
            // runs that start on a reordered glyph.
            if (!rstJustification.empty())
            {
                if (vRtl[nActiveCharPos - mnMinCharPos])
                {
                    if (rstJustification.GetTotalAdvance(nRunVisibleEndChar)
                        >= rstJustification.GetTotalAdvance(pGlyphs[n]->charPos()))
                    {
                        nRunVisibleEndChar = pGlyphs[n]->charPos();
                    }
                }
                else if (rstJustification.GetTotalAdvance(nRunVisibleEndChar)
                         <= rstJustification.GetTotalAdvance(pGlyphs[n]->charPos()))
                {
                    nRunVisibleEndChar = pGlyphs[n]->charPos();
                }
            }

            nRunAdvance += pGlyphs[n]->newWidth();

            // proceed to next glyph
            nStartOld[n] = nStartNew[n];
            int nOrigCharPos = pGlyphs[n]->charPos();
            bValid[n] = mpLayouts[n]->GetNextGlyph(&pGlyphs[n], aPos, nStartNew[n]);
            // break after last glyph of active layout
            if( !bValid[n] )
            {
                // performance optimization (when a fallback layout is no longer needed)
                if( n >= nLevel-1 )
                    --nLevel;
                break;
            }

            //If the next character is one which belongs to the next level, then we
            //are finished here for now, and we'll pick up after the next level has
            //been processed
            if ((n+1 < nLevel) && (pGlyphs[n]->charPos() != nOrigCharPos))
            {
                if (nOrigCharPos < pGlyphs[n]->charPos())
                {
                    if (pGlyphs[n+1]->charPos() > nOrigCharPos && (pGlyphs[n+1]->charPos() < pGlyphs[n]->charPos()))
                        break;
                }
                else if (nOrigCharPos > pGlyphs[n]->charPos())
                {
                    if (pGlyphs[n+1]->charPos() > pGlyphs[n]->charPos() && (pGlyphs[n+1]->charPos() < nOrigCharPos))
                        break;
                }
            }

            // break at end of layout run
            if( n > 0 )
            {
                // skip until end of fallback run
                if (!maFallbackRuns[n-1].PosIsInRun(pGlyphs[n]->charPos()))
                    break;
            }
            else
            {
                // break when a fallback is needed and available
                bool bNeedFallback = maFallbackRuns[0].PosIsInRun(pGlyphs[nFirstValid]->charPos());
                if( bNeedFallback )
                    if (!maFallbackRuns[nLevel-1].PosIsInRun(pGlyphs[nFirstValid]->charPos()))
                        break;
                // break when change from resolved to unresolved base layout run
                if( bKeepNotDef && !bNeedFallback )
                    { maFallbackRuns[0].NextRun(); break; }
                bKeepNotDef = bNeedFallback;
            }
        }

        // if a justification array is available
        // => use it directly to calculate the corresponding run width
        if (!rstJustification.empty())
        {
            // the run advance is the width from the first char
            // in the run to the first char in the next run
            nRunAdvance = 0;
            nActiveCharIndex = nActiveCharPos - mnMinCharPos;
            if (nActiveCharIndex >= 0 && vRtl[nActiveCharIndex])
            {
                nRunAdvance -= rstJustification.GetTotalAdvance(nRunVisibleEndChar - 1);
                nRunAdvance += rstJustification.GetTotalAdvance(nLastRunEndChar - 1);
            }
            else
            {
                nRunAdvance += rstJustification.GetTotalAdvance(nRunVisibleEndChar);
                nRunAdvance -= rstJustification.GetTotalAdvance(nLastRunEndChar);
            }
            nLastRunEndChar = nRunVisibleEndChar;
            nRunVisibleEndChar = pGlyphs[nFirstValid]->charPos();
        }

        // calculate new x position
        nXPos += nRunAdvance;

        // prepare for next fallback run
        nActiveCharPos = pGlyphs[nFirstValid]->charPos();
        // it essential that the runs don't get ahead of themselves and in the
        // if( bKeepNotDef && !bNeedFallback ) statement above, the next run may
        // have already been reached on the base level
        for( int i = nFBLevel; --i >= 0;)
        {
            if (maFallbackRuns[i].GetRun(&nRunStart, &nRunEnd, &bRtl))
            {
                // tdf#165510: Need to use the direction of the current character,
                // not the direction of the fallback run.
                nActiveCharIndex = nActiveCharPos - mnMinCharPos;
                if (nActiveCharIndex >= 0)
                {
                    bRtl = vRtl[nActiveCharIndex];
                }

                if (bRtl)
                {
                    if (nRunStart > nActiveCharPos)
                        maFallbackRuns[i].NextRun();
                }
                else
                {
                    if (nRunEnd <= nActiveCharPos)
                        maFallbackRuns[i].NextRun();
                }
            }
        }
    }

    mpLayouts[0]->Simplify( true );
}

void MultiSalLayout::DrawText( SalGraphics& rGraphics ) const
{
    for( int i = mnLevel; --i >= 0; )
    {
        SalLayout& rLayout = *mpLayouts[ i ];
        rLayout.DrawBase() += maDrawBase;
        rLayout.DrawOffset() += maDrawOffset;
        rLayout.DrawText( rGraphics );
        rLayout.DrawOffset() -= maDrawOffset;
        rLayout.DrawBase() -= maDrawBase;
    }
    // NOTE: now the baselevel font is active again
}

sal_Int32 MultiSalLayout::GetTextBreak(double nMaxWidth, double nCharExtra, int nFactor) const
{
    if( mnLevel <= 0 )
        return -1;
    if( mnLevel == 1 )
        return mpLayouts[0]->GetTextBreak( nMaxWidth, nCharExtra, nFactor );

    int nCharCount = mnEndCharPos - mnMinCharPos;
    std::vector<double> aCharWidths;
    std::vector<double> aFallbackCharWidths;
    mpLayouts[0]->FillDXArray( &aCharWidths, {} );

    for( int n = 1; n < mnLevel; ++n )
    {
        SalLayout& rLayout = *mpLayouts[ n ];
        rLayout.FillDXArray( &aFallbackCharWidths, {} );
        for( int i = 0; i < nCharCount; ++i )
            if( aCharWidths[ i ] == 0 )
                aCharWidths[i] = aFallbackCharWidths[i];
    }

    double nWidth = 0;
    for( int i = 0; i < nCharCount; ++i )
    {
        nWidth += aCharWidths[ i ] * nFactor;
        if( nWidth > nMaxWidth )
            return (i + mnMinCharPos);
        nWidth += nCharExtra;
    }

    return -1;
}

double MultiSalLayout::GetTextWidth() const
{
    // Measure text width. There might be holes in each SalLayout due to
    // missing chars, so we use GetNextGlyph() to get the glyphs across all
    // layouts.
    int nStart = 0;
    basegfx::B2DPoint aPos;
    const GlyphItem* pGlyphItem;

    double nWidth = 0;
    while (GetNextGlyph(&pGlyphItem, aPos, nStart))
        nWidth += pGlyphItem->newWidth();

    return nWidth;
}

double MultiSalLayout::GetPartialTextWidth(sal_Int32 skipStart, sal_Int32 amt) const
{
    // Measure text width. There might be holes in each SalLayout due to
    // missing chars, so we use GetNextGlyph() to get the glyphs across all
    // layouts.
    int nStart = 0;
    basegfx::B2DPoint aPos;
    const GlyphItem* pGlyphItem;

    auto skipEnd = skipStart + amt;
    double nWidth = 0;
    while (GetNextGlyph(&pGlyphItem, aPos, nStart))
    {
        auto cpos = pGlyphItem->charPos();
        if (cpos >= skipStart && cpos < skipEnd)
        {
            nWidth += pGlyphItem->newWidth();
        }
    }

    return nWidth;
}

double MultiSalLayout::FillDXArray( std::vector<double>* pCharWidths, const OUString& rStr ) const
{
    if (pCharWidths)
    {
        // prepare merging of fallback levels
        std::vector<double> aTempWidths;
        const int nCharCount = mnEndCharPos - mnMinCharPos;
        pCharWidths->clear();
        pCharWidths->resize(nCharCount, 0);

        for (int n = mnLevel; --n >= 0;)
        {
            // query every fallback level
            mpLayouts[n]->FillDXArray(&aTempWidths, rStr);

            // calculate virtual char widths using most probable fallback layout
            for (int i = 0; i < nCharCount; ++i)
            {
                // #i17359# restriction:
                // one char cannot be resolved from different fallbacks
                if ((*pCharWidths)[i] != 0)
                    continue;
                double nCharWidth = aTempWidths[i];
                if (!nCharWidth)
                    continue;
                (*pCharWidths)[i] = nCharWidth;
            }
        }
    }

    return GetTextWidth();
}

double MultiSalLayout::FillPartialDXArray(std::vector<double>* pCharWidths, const OUString& rStr,
                                          sal_Int32 skipStart, sal_Int32 amt) const
{
    if (pCharWidths)
    {
        FillDXArray(pCharWidths, rStr);

        // Strip excess characters from the array
        if (skipStart < static_cast<sal_Int32>(pCharWidths->size()))
        {
            std::copy(pCharWidths->begin() + skipStart, pCharWidths->end(), pCharWidths->begin());
        }

        pCharWidths->resize(amt);
    }

    return GetPartialTextWidth(skipStart, amt);
}

void MultiSalLayout::GetCaretPositions(std::vector<double>& rCaretPositions,
                                       const OUString& rStr) const
{
    // prepare merging of fallback levels
    std::vector<double> aTempPos;
    const int nCaretPositions = (mnEndCharPos - mnMinCharPos) * 2;
    rCaretPositions.clear();
    rCaretPositions.resize(nCaretPositions, -1);

    for (int n = mnLevel; --n >= 0;)
    {
        // query every fallback level
        mpLayouts[n]->GetCaretPositions(aTempPos, rStr);

        // calculate virtual char widths using most probable fallback layout
        for (int i = 0; i < nCaretPositions; ++i)
        {
            // one char cannot be resolved from different fallbacks
            if (rCaretPositions[i] != -1)
                continue;
            if (aTempPos[i] >= 0)
                rCaretPositions[i] = aTempPos[i];
        }
    }
}

bool MultiSalLayout::GetNextGlyph(const GlyphItem** pGlyph,
                                  basegfx::B2DPoint& rPos, int& nStart,
                                  const LogicalFontInstance** ppGlyphFont) const
{
    // NOTE: nStart is tagged with current font index
    int nLevel = static_cast<unsigned>(nStart) >> GF_FONTSHIFT;
    nStart &= ~GF_FONTMASK;
    for(; nLevel < mnLevel; ++nLevel, nStart=0 )
    {
        GenericSalLayout& rLayout = *mpLayouts[ nLevel ];
        if (rLayout.GetNextGlyph(pGlyph, rPos, nStart, ppGlyphFont))
        {
            int nFontTag = nLevel << GF_FONTSHIFT;
            nStart |= nFontTag;
            rPos += maDrawBase + maDrawOffset;
            return true;
        }
    }

    return false;
}

bool MultiSalLayout::GetOutline(basegfx::B2DPolyPolygonVector& rPPV) const
{
    bool bRet = false;

    for( int i = mnLevel; --i >= 0; )
    {
        SalLayout& rLayout = *mpLayouts[ i ];
        rLayout.DrawBase() = maDrawBase;
        rLayout.DrawOffset() += maDrawOffset;
        bRet |= rLayout.GetOutline(rPPV);
        rLayout.DrawOffset() -= maDrawOffset;
    }

    return bRet;
}

bool MultiSalLayout::HasFontKashidaPositions() const
{
    // tdf#163215: VCL cannot suggest valid kashida positions for certain fonts (e.g. AAT).
    // In order to strictly validate kashida positions, all fallback fonts must allow it.
    for (int n = 0; n < mnLevel; ++n)
    {
        if (!mpLayouts[n]->HasFontKashidaPositions())
        {
            return false;
        }
    }

    return true;
}

bool MultiSalLayout::IsKashidaPosValid(int nCharPos, int nNextCharPos) const
{
    // Check the base layout
    bool bValid = mpLayouts[0]->IsKashidaPosValid(nCharPos, nNextCharPos);

    // If base layout returned false, it might be because the character was not
    // supported there, so we check fallback layouts.
    if (!bValid)
    {
        for (int i = 1; i < mnLevel; ++i)
        {
            // - 1 because there is no fallback run for the base layout, IIUC.
            if (maFallbackRuns[i - 1].PosIsInAnyRun(nCharPos) &&
                maFallbackRuns[i - 1].PosIsInAnyRun(nNextCharPos))
            {
                bValid = mpLayouts[i]->IsKashidaPosValid(nCharPos, nNextCharPos);
                break;
            }
        }
    }

    return bValid;
}

SalLayoutGlyphs MultiSalLayout::GetGlyphs() const
{
    SalLayoutGlyphs glyphs;
    for( int n = 0; n < mnLevel; ++n )
        glyphs.AppendImpl(mpLayouts[n]->GlyphsImpl().clone());
    return glyphs;
}

void MultiSalLayout::drawSalLayout(void* pSurface, const basegfx::BColor& rTextColor, bool bAntiAliased) const
{
    for( int i = mnLevel; --i >= 0; )
    {
        Application::GetDefaultDevice()->GetGraphics()->DrawSalLayout(*mpLayouts[ i ], pSurface, rTextColor, bAntiAliased);
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
