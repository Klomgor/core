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


#include <pdfiprocessor.hxx>
#include <xmlemitter.hxx>
#include <pdfihelper.hxx>
#include <imagecontainer.hxx>
#include <genericelements.hxx>
#include "style.hxx"
#include <treevisiting.hxx>

#include <sal/log.hxx>

#include <com/sun/star/geometry/AffineMatrix2D.hpp>

#include <comphelper/sequence.hxx>
#include <basegfx/polygon/b2dpolygonclipper.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <basegfx/polygon/b2dpolypolygoncutter.hxx>
#include <basegfx/utils/canvastools.hxx>
#include <basegfx/vector/b2enums.hxx>
#include <basegfx/matrix/b2dhommatrix.hxx>
#include <i18nutil/unicode.hxx>
#include <o3tl/string_view.hxx>

using namespace com::sun::star;

#include <drawinglayer/primitive2d/PolyPolygonStrokePrimitive2D.hxx>
#include <drawinglayer/processor2d/linegeometryextractor2d.hxx>

#include <iostream>

namespace pdfi
{

 PDFIProcessor::PDFIProcessor( const uno::Reference< task::XStatusIndicator >& xStat ,
            css::uno::Reference< css::uno::XComponentContext > const & xContext) :

    m_xContext(xContext),
    prevCharWidth(0),
    m_pDocument( ElementFactory::createDocumentElement() ),
    m_pCurPage(nullptr),
    m_pCurElement(nullptr),
    m_nNextFontId( 1 ),
    m_nNextGCId( 1 ),
    m_nPages(0),
    m_nNextZOrder( 1 ),
    m_xStatusIndicator( xStat )
{
    FontAttributes aDefFont;
    aDefFont.familyName = "Helvetica";
    aDefFont.fontWeight = u"normal"_ustr;
    aDefFont.isItalic   = false;
    aDefFont.size       = 10*PDFI_OUTDEV_RESOLUTION/72;
    m_aIdToFont.insert({0, aDefFont});
    m_aFontToId.insert({aDefFont, 0});

    GraphicsContext aDefGC;
    m_aGCStack.push_back( aDefGC );
    m_aGCToId.insert({aDefGC, 0});
    m_aIdToGC.insert({0, aDefGC});
}

void PDFIProcessor::setPageNum( sal_Int32 nPages )
{
    m_nPages = nPages;
}


void PDFIProcessor::pushState()
{
    GraphicsContextStack::value_type const a(m_aGCStack.back());
    m_aGCStack.push_back(a);
}

void PDFIProcessor::popState()
{
    m_aGCStack.pop_back();
}

void PDFIProcessor::setFlatness( double value )
{
    getCurrentContext().Flatness = value;
}

void PDFIProcessor::setTransformation( const geometry::AffineMatrix2D& rMatrix )
{
    basegfx::unotools::homMatrixFromAffineMatrix(
        getCurrentContext().Transformation,
        rMatrix );
}

void PDFIProcessor::setLineDash( const uno::Sequence<double>& dashes,
                                 double                       /*start*/ )
{
    // TODO(F2): factor in start offset
    GraphicsContext& rContext( getCurrentContext() );
    comphelper::sequenceToContainer(rContext.DashArray,dashes);
}

void PDFIProcessor::setLineJoin(basegfx::B2DLineJoin nJoin)
{
    getCurrentContext().LineJoin = nJoin;
}

void PDFIProcessor::setLineCap(sal_Int8 nCap)
{
    getCurrentContext().LineCap = nCap;
}

void PDFIProcessor::setMiterLimit(double)
{
    SAL_WARN("sdext.pdfimport", "PDFIProcessor::setMiterLimit(): not supported by ODF");
}

void PDFIProcessor::setLineWidth(double nWidth)
{
    getCurrentContext().LineWidth = nWidth;
}

void PDFIProcessor::setFillColor( const rendering::ARGBColor& rColor )
{
    getCurrentContext().FillColor = rColor;
}

void PDFIProcessor::setStrokeColor( const rendering::ARGBColor& rColor )
{
    getCurrentContext().LineColor = rColor;
}

void PDFIProcessor::setFont( const FontAttributes& i_rFont )
{
    FontAttributes aChangedFont( i_rFont );
    GraphicsContext& rGC=getCurrentContext();
    // for text render modes, please see PDF reference manual
    if (rGC.TextRenderMode == 1)
    {
        aChangedFont.isOutline = true;
    }
    else if (rGC.TextRenderMode == 2)
    {
        // tdf#81484: faux bold is represented as "stroke+fill" (while using the same color for both stroke and fill) in pdf.
        // Convert to bold instead if the stroke color is the same as the fill color,
        // otherwise it should be outline.
        if (getCurrentContext().LineColor == getCurrentContext().FillColor)
            aChangedFont.fontWeight = u"bold"_ustr;
        else
            aChangedFont.isOutline = true;
    }
    FontToIdMap::const_iterator it = m_aFontToId.find( aChangedFont );
    if( it != m_aFontToId.end() )
        rGC.FontId = it->second;
    else
    {
        m_aFontToId[ aChangedFont ] = m_nNextFontId;
        m_aIdToFont[ m_nNextFontId ] = std::move(aChangedFont);
        rGC.FontId = m_nNextFontId;
        m_nNextFontId++;
    }
}

void PDFIProcessor::setTextRenderMode( sal_Int32 i_nMode )
{
    GraphicsContext& rGC=getCurrentContext();
    rGC.TextRenderMode = i_nMode;
    IdToFontMap::iterator it = m_aIdToFont.find( rGC.FontId );
    if( it != m_aIdToFont.end() )
        setFont( it->second );
}

sal_Int32 PDFIProcessor::getFontId( const FontAttributes& rAttr ) const
{
    const sal_Int32 nCurFont = getCurrentContext().FontId;
    const_cast<PDFIProcessor*>(this)->setFont( rAttr );
    const sal_Int32 nFont = getCurrentContext().FontId;
    const_cast<PDFIProcessor*>(this)->getCurrentContext().FontId = nCurFont;

    return nFont;
}

// line diagnose block - start
void PDFIProcessor::processGlyphLine()
{
    if (m_GlyphsList.empty())
        return;

    double spaceDetectBoundary = 0.0;

    // Try to find space glyph and its width
    for (CharGlyph & i : m_GlyphsList)
    {
        OUString& glyph = i.getGlyph();

        sal_Unicode ch = '\0';
        if (!glyph.isEmpty())
            ch = glyph[0];

        if ((ch == 0x20) || (ch == 0xa0))
        {
            double spaceWidth = i.getWidth();
            spaceDetectBoundary = spaceWidth * 0.5;
            break;
        }
    }

    // If space glyph is not found, use average glyph width instead
    if (spaceDetectBoundary == 0.0)
    {
        double avgGlyphWidth = 0.0;
        for (const CharGlyph & i : m_GlyphsList)
            avgGlyphWidth += i.getWidth();
        avgGlyphWidth /= m_GlyphsList.size();
        spaceDetectBoundary = avgGlyphWidth * 0.2;
    }

    FrameElement* frame = ElementFactory::createFrameElement(
        m_GlyphsList[0].getCurElement(),
        getGCId(m_GlyphsList[0].getGC()));
    frame->ZOrder = m_nNextZOrder++;
    frame->IsForText = true;
    frame->FontSize = getFont(m_GlyphsList[0].getGC().FontId).size;
    ParagraphElement* para = ElementFactory::createParagraphElement(frame);

    for (size_t i = 0; i < m_GlyphsList.size(); i++)
    {
        bool prependSpace = false;
        TextElement* text = ElementFactory::createTextElement(
            para,
            getGCId(m_GlyphsList[i].getGC()),
            m_GlyphsList[i].getGC().FontId);
        if (i == 0)
        {
            text->x = m_GlyphsList[0].getGC().Transformation.get(0, 2);
            text->y = m_GlyphsList[0].getGC().Transformation.get(1, 2);
            text->w = 0;
            text->h = 0;
            para->updateGeometryWith(text);
            frame->updateGeometryWith(para);
        }
        else
        {
            double spaceSize = m_GlyphsList[i].getPrevSpaceWidth();
            prependSpace = spaceSize > spaceDetectBoundary;
        }
        if (prependSpace)
            text->Text.append(" ");
        text->Text.append(m_GlyphsList[i].getGlyph());
    }

    m_GlyphsList.clear();
}

void PDFIProcessor::drawGlyphs( const OUString&             rGlyphs,
                                const geometry::RealRectangle2D& rRect,
                                const geometry::Matrix2D&        rFontMatrix,
                                double fontSize)
{
    double ascent = getFont(getCurrentContext().FontId).ascent;

    basegfx::B2DHomMatrix fontMatrix(
        rFontMatrix.m00, rFontMatrix.m01, 0.0,
        rFontMatrix.m10, rFontMatrix.m11, 0.0);
    fontMatrix.scale(fontSize, fontSize);

    basegfx::B2DHomMatrix totalTextMatrix1(fontMatrix);
    basegfx::B2DHomMatrix totalTextMatrix2(fontMatrix);
    totalTextMatrix1.translate(rRect.X1, rRect.Y1);
    totalTextMatrix2.translate(rRect.X2, rRect.Y2);

    basegfx::B2DHomMatrix corrMatrix;
    corrMatrix.scale(1.0, -1.0);
    corrMatrix.translate(0.0, ascent);
    totalTextMatrix1 = totalTextMatrix1 * corrMatrix;
    totalTextMatrix2 = totalTextMatrix2 * corrMatrix;

    totalTextMatrix1 *= getCurrentContext().Transformation;
    totalTextMatrix2 *= getCurrentContext().Transformation;

    basegfx::B2DHomMatrix invMatrix(totalTextMatrix1);
    basegfx::B2DHomMatrix invPrevMatrix(prevTextMatrix);
    invMatrix.invert();
    invPrevMatrix.invert();
    basegfx::B2DHomMatrix offsetMatrix1(totalTextMatrix1);
    basegfx::B2DHomMatrix offsetMatrix2(totalTextMatrix2);
    offsetMatrix1 *= invPrevMatrix;
    offsetMatrix2 *= invMatrix;

    double charWidth = offsetMatrix2.get(0, 2);
    double prevSpaceWidth = offsetMatrix1.get(0, 2) - prevCharWidth;

    if ((totalTextMatrix1.get(0, 0) != prevTextMatrix.get(0, 0)) ||
        (totalTextMatrix1.get(0, 1) != prevTextMatrix.get(0, 1)) ||
        (totalTextMatrix1.get(1, 0) != prevTextMatrix.get(1, 0)) ||
        (totalTextMatrix1.get(1, 1) != prevTextMatrix.get(1, 1)) ||
        (offsetMatrix1.get(0, 2) < 0.0) ||
        (prevSpaceWidth > prevCharWidth * 1.3) ||
        (!basegfx::fTools::equalZero(offsetMatrix1.get(1, 2), 0.0001)))
    {
        processGlyphLine();
    }

    CharGlyph aGlyph(m_pCurElement, getCurrentContext(), charWidth, prevSpaceWidth, rGlyphs);
    aGlyph.getGC().Transformation = totalTextMatrix1;
    m_GlyphsList.push_back(aGlyph);

    prevCharWidth = charWidth;
    prevTextMatrix = totalTextMatrix1;
}

void PDFIProcessor::endText()
{
    TextElement* pText = m_pCurElement->dynCastAsTextElement();
    if( pText )
        m_pCurElement = pText->Parent;
}

void PDFIProcessor::setupImage(ImageId nImage)
{
    const GraphicsContext& rGC(getCurrentContext());

    basegfx::B2DTuple aScale, aTranslation;
    double fRotate, fShearX;
    rGC.Transformation.decompose(aScale, aTranslation, fRotate, fShearX);

    const sal_Int32 nGCId = getGCId(rGC);
    FrameElement* pFrame = ElementFactory::createFrameElement( m_pCurElement, nGCId );
    ImageElement* pImageElement = ElementFactory::createImageElement( pFrame, nGCId, nImage );
    pFrame->x = pImageElement->x = aTranslation.getX();
    pFrame->y = pImageElement->y = aTranslation.getY();
    pFrame->w = pImageElement->w = aScale.getX();
    pFrame->h = pImageElement->h = aScale.getY();
    pFrame->ZOrder = m_nNextZOrder++;

    // Poppler wrapper takes into account that vertical axes of PDF and ODF are opposite,
    // and it flips matrix vertically (see poppler's GfxState::GfxState()).
    // But image internal vertical axis is independent of PDF vertical axis direction,
    // so arriving matrix is extra-flipped relative to image.
    // We force vertical flip here to compensate that.
    pFrame->MirrorVertical = true;
}

void PDFIProcessor::drawMask(const uno::Sequence<beans::PropertyValue>& xBitmap,
                             bool                                       /*bInvert*/ )
{
    // TODO(F3): Handle mask and inversion
    setupImage( m_aImages.addImage(xBitmap) );
}

void PDFIProcessor::drawImage(const uno::Sequence<beans::PropertyValue>& xBitmap )
{
    setupImage( m_aImages.addImage(xBitmap) );
}

void PDFIProcessor::drawColorMaskedImage(const uno::Sequence<beans::PropertyValue>& xBitmap,
                                         const uno::Sequence<uno::Any>&             /*xMaskColors*/ )
{
    // TODO(F3): Handle mask colors
    setupImage( m_aImages.addImage(xBitmap) );
}

void PDFIProcessor::drawMaskedImage(const uno::Sequence<beans::PropertyValue>& xBitmap,
                                    const uno::Sequence<beans::PropertyValue>& /*xMask*/,
                                    bool                                       /*bInvertMask*/)
{
    // TODO(F3): Handle mask and inversion
    setupImage( m_aImages.addImage(xBitmap) );
}

void PDFIProcessor::drawAlphaMaskedImage(const uno::Sequence<beans::PropertyValue>& xBitmap,
                                         const uno::Sequence<beans::PropertyValue>& /*xMask*/)
{
    // TODO(F3): Handle mask

    setupImage( m_aImages.addImage(xBitmap) );

}

void PDFIProcessor::tilingPatternFill(int nX0, int nY0, int nX1, int nY1,
                                      double nxStep, double nyStep,
                                      int /* nPaintType */,
                                      css::geometry::AffineMatrix2D& rMat,
                                      const css::uno::Sequence<css::beans::PropertyValue>& xTile)
{
    const GraphicsContext& rGC(getCurrentContext());
    auto nTile = m_aImages.addImage(xTile);

    basegfx::B2DTuple aScale, aTranslation;
    double fRotate, fShearX;
    rGC.Transformation.decompose(aScale, aTranslation, fRotate, fShearX);

    // Build a poly covering the whole fill area
    double np0x = nX0 * nxStep;
    double np0y = nY0 * nyStep;
    double np1x = nX1 * nxStep;
    double np1y = nY1 * nyStep;

    // Transform with the rMat passed in
    double tmpx, tmpy;
    tmpx = np0x * rMat.m00 + np0y * rMat.m01 + rMat.m02;
    tmpy = np0x * rMat.m10 + np0y * rMat.m11 + rMat.m12;
    np0x = tmpx;
    np0y = tmpy;
    tmpx = np1x * rMat.m00 + np1y * rMat.m01 + rMat.m02;
    tmpy = np1x * rMat.m10 + np1y * rMat.m11 + rMat.m12;
    np1x = tmpx;
    np1y = tmpy;

    auto aB2DPoly = basegfx::B2DPolyPolygon(basegfx::utils::createPolygonFromRect(basegfx::B2DRange(np0x, np0y, np1x, np1y)));
    aB2DPoly.transform(getCurrentContext().Transformation);

    // Clip against current clip path, if any
    basegfx::B2DPolyPolygon aCurClip = getCurrentContext().Clip;
    if( aCurClip.count() ) {
        aB2DPoly = basegfx::utils::clipPolyPolygonOnPolyPolygon( aB2DPoly, aCurClip,
                       true, /* bInside, keep parts inside the clip */
                       false /* bStroke, filled not stroked */ );
    }
    // TODO: That clipping might shift the fill pattern offsets

    double transformedxStep = nxStep * rMat.m00 + nyStep * rMat.m01;
    double transformedyStep = nxStep * rMat.m10 + nyStep * rMat.m11;

    auto pPolyElement = ElementFactory::createPolyPolyElement(
        m_pCurElement,
        getGCId(getCurrentContext()),
        aB2DPoly,
        PATH_EOFILL, // Hmm how do I know if this should be EO or not?
        nTile,
        transformedxStep * aScale.getX(),
        transformedyStep * -aScale.getY());
    pPolyElement->updateGeometry();
    pPolyElement->ZOrder = m_nNextZOrder++;
}

void PDFIProcessor::strokePath( const uno::Reference< rendering::XPolyPolygon2D >& rPath )
{
    basegfx::B2DPolyPolygon aPoly=basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(rPath);
    basegfx::B2DPolyPolygon aCurClip = getCurrentContext().Clip;
    aPoly.transform(getCurrentContext().Transformation);

    if( aCurClip.count() ) {
        aPoly = basegfx::utils::clipPolyPolygonOnPolyPolygon( aPoly, aCurClip,
                    true, /* bInside, keep parts inside the clip */
                    !aPoly.isClosed() /* bStroke */ );
    }

    PolyPolyElement* pPoly = ElementFactory::createPolyPolyElement(
        m_pCurElement,
        getGCId(getCurrentContext()),
        aPoly,
        PATH_STROKE,
        -1, 0, 0 );
    pPoly->updateGeometry();
    pPoly->ZOrder = m_nNextZOrder++;
}

void PDFIProcessor::fillPath( const uno::Reference< rendering::XPolyPolygon2D >& rPath )
{
    basegfx::B2DPolyPolygon aPoly=basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(rPath);
    aPoly = basegfx::utils::createNonzeroConform(aPoly);

    aPoly.transform(getCurrentContext().Transformation);

    basegfx::B2DPolyPolygon aCurClip = getCurrentContext().Clip;
    if( aCurClip.count() ) {
        aPoly = basegfx::utils::clipPolyPolygonOnPolyPolygon( aPoly, aCurClip,
                    true, /* bInside, keep parts inside the clip */
                    false /* bStroke, filled not stroked */ );
    }

    PolyPolyElement* pPoly = ElementFactory::createPolyPolyElement(
        m_pCurElement,
        getGCId(getCurrentContext()),
        aPoly,
        PATH_FILL,
        -1, 0, 0 );
    pPoly->updateGeometry();
    pPoly->ZOrder = m_nNextZOrder++;
}

void PDFIProcessor::eoFillPath( const uno::Reference< rendering::XPolyPolygon2D >& rPath )
{
    basegfx::B2DPolyPolygon aPoly=basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(rPath);
    aPoly.transform(getCurrentContext().Transformation);

    basegfx::B2DPolyPolygon aCurClip = getCurrentContext().Clip;
    if( aCurClip.count() ) {
        aPoly = basegfx::utils::clipPolyPolygonOnPolyPolygon( aPoly, aCurClip,
                    true, /* bInside, keep parts inside the clip */
                    false /* bStroke, filled not stroked */ );
    }

    PolyPolyElement* pPoly = ElementFactory::createPolyPolyElement(
        m_pCurElement,
        getGCId(getCurrentContext()),
        aPoly,
        PATH_EOFILL,
        -1, 0, 0 );
    pPoly->updateGeometry();
    pPoly->ZOrder = m_nNextZOrder++;
}

void PDFIProcessor::intersectClip(const uno::Reference< rendering::XPolyPolygon2D >& rPath)
{
    // TODO(F3): interpret fill mode
    basegfx::B2DPolyPolygon aNewClip = basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(rPath);
    aNewClip.transform(getCurrentContext().Transformation);
    basegfx::B2DPolyPolygon aCurClip = getCurrentContext().Clip;

    if( aCurClip.count() )  // #i92985# adapted API from (..., false, false) to (..., true, false)
        aNewClip = basegfx::utils::clipPolyPolygonOnPolyPolygon( aCurClip, aNewClip, true, false );

    getCurrentContext().Clip = std::move(aNewClip);
}

void PDFIProcessor::intersectEoClip(const uno::Reference< rendering::XPolyPolygon2D >& rPath)
{
    // TODO(F3): interpret fill mode
    basegfx::B2DPolyPolygon aNewClip = basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(rPath);
    aNewClip.transform(getCurrentContext().Transformation);
    basegfx::B2DPolyPolygon aCurClip = getCurrentContext().Clip;

    if( aCurClip.count() )  // #i92985# adapted API from (..., false, false) to (..., true, false)
        aNewClip = basegfx::utils::clipPolyPolygonOnPolyPolygon( aCurClip, aNewClip, true, false );

    getCurrentContext().Clip = std::move(aNewClip);
}

void PDFIProcessor::intersectClipToStroke(const uno::Reference< rendering::XPolyPolygon2D >& rPath)
{
    // TODO(F3): interpret fill mode
    basegfx::B2DPolyPolygon aNewClip = basegfx::unotools::b2DPolyPolygonFromXPolyPolygon2D(rPath);
    const GraphicsContext& rGC(getCurrentContext());
    aNewClip.transform(rGC.Transformation);
    basegfx::B2DPolyPolygon aCurClip = rGC.Clip;
    double nScale = GetAverageTransformationScale(rGC.Transformation);


    // We need to get a path that corresponds to a 'stroked path' - i.e. with whatever line
    // thickness etc is set.  PolyPolygonStrokePrimitive2D::create2DDecomposition does most
    // of the work.
    const basegfx::BColor aBlack(0.0, 0.0, 0.0);
    drawinglayer::attribute::LineAttribute aLineAttribute(aBlack, rGC.LineWidth * nScale,
                                                          rGC.LineJoin);
    rtl::Reference<drawinglayer::primitive2d::PolyPolygonStrokePrimitive2D> aStrokePrimitive(
        new drawinglayer::primitive2d::PolyPolygonStrokePrimitive2D(aNewClip, aLineAttribute));
    drawinglayer::primitive2d::Primitive2DContainer aPrimitiveContainer;
    const drawinglayer::geometry::ViewInformation2D aViewInformation2D;
    aStrokePrimitive->get2DDecomposition(aPrimitiveContainer, aViewInformation2D);

    // Based on extractLineContourFromPrimitive2DSequence and ImpConvertToContourObj
    drawinglayer::processor2d::LineGeometryExtractor2D aExtractor(aViewInformation2D);
    aExtractor.process(aPrimitiveContainer);

    basegfx::B2DPolygonVector aHairlines = aExtractor.getExtractedHairlines();
    auto aFills = aExtractor.getExtractedLineFills();

    basegfx::B2DPolyPolygon aTmpClip = basegfx::utils::mergeToSinglePolyPolygon(std::move(aFills));

    for (const basegfx::B2DPolygon & rExtractedHairline : aHairlines)
        aTmpClip.append(rExtractedHairline);

    aNewClip = std::move(aTmpClip);

    if( aCurClip.count() )  // #i92985# adapted API from (..., false, false) to (..., true, false)
        aNewClip = basegfx::utils::clipPolyPolygonOnPolyPolygon( aCurClip, aNewClip, true, false );

    getCurrentContext().Clip = std::move(aNewClip);
}

void PDFIProcessor::hyperLink( const geometry::RealRectangle2D& rBounds,
                               const OUString&           rURI )
{
    if( !rURI.isEmpty() )
    {
        HyperlinkElement* pLink = ElementFactory::createHyperlinkElement(
            &m_pCurPage->Hyperlinks,
            rURI );
        pLink->x = rBounds.X1;
        pLink->y = rBounds.Y1;
        pLink->w = rBounds.X2-rBounds.X1;
        pLink->h = rBounds.Y2-rBounds.Y1;
    }
}

const FontAttributes& PDFIProcessor::getFont( sal_Int32 nFontId ) const
{
    IdToFontMap::const_iterator it = m_aIdToFont.find( nFontId );
    if( it == m_aIdToFont.end() )
        it = m_aIdToFont.find( 0 );
    assert(it != m_aIdToFont.end());
    return it->second;
}

sal_Int32 PDFIProcessor::getGCId( const GraphicsContext& rGC )
{
    sal_Int32 nGCId = 0;
    auto it = m_aGCToId.find( rGC );
    if( it != m_aGCToId.end() )
        nGCId = it->second;
    else
    {
        m_aGCToId.insert({rGC, m_nNextGCId});
        m_aIdToGC.insert({m_nNextGCId, rGC});
        nGCId = m_nNextGCId;
        m_nNextGCId++;
    }

    return nGCId;
}

const GraphicsContext& PDFIProcessor::getGraphicsContext( sal_Int32 nGCId ) const
{
    auto it = m_aIdToGC.find( nGCId );
    if( it == m_aIdToGC.end() )
        it = m_aIdToGC.find( 0 );
    assert(it != m_aIdToGC.end());
    return it->second;
}

void PDFIProcessor::endPage()
{
    processGlyphLine(); // draw last line
    if( m_xStatusIndicator.is()
        && m_pCurPage
        && m_pCurPage->PageNumber == m_nPages
    )
        m_xStatusIndicator->end();
}

void PDFIProcessor::startPage( const geometry::RealSize2D& rSize )
{
    // initial clip is to page bounds
    getCurrentContext().Clip = basegfx::B2DPolyPolygon(
        basegfx::utils::createPolygonFromRect(
            basegfx::B2DRange( 0, 0, rSize.Width, rSize.Height )));

    sal_Int32 nNextPageNr = m_pCurPage ? m_pCurPage->PageNumber+1 : 1;
    if( m_xStatusIndicator.is() )
    {
        if( nNextPageNr == 1 )
            startIndicator( u" "_ustr );
        m_xStatusIndicator->setValue( nNextPageNr );
    }
    m_pCurPage = ElementFactory::createPageElement(m_pDocument.get(), nNextPageNr);
    m_pCurElement = m_pCurPage;
    m_pCurPage->w = rSize.Width;
    m_pCurPage->h = rSize.Height;
    m_nNextZOrder = 1;


}

void PDFIProcessor::emit( XmlEmitter&               rEmitter,
                          const TreeVisitorFactory& rVisitorFactory )
{
#if OSL_DEBUG_LEVEL > 0
    m_pDocument->emitStructure( 0 );
#endif

    ElementTreeVisitorSharedPtr optimizingVisitor(
        rVisitorFactory.createOptimizingVisitor(*this));
    // FIXME: localization
    startIndicator( u" "_ustr );
    m_pDocument->visitedBy( *optimizingVisitor, std::list<std::unique_ptr<Element>>::const_iterator());

#if OSL_DEBUG_LEVEL > 0
    m_pDocument->emitStructure( 0 );
#endif

    // get styles
    StyleContainer aStyles;
    ElementTreeVisitorSharedPtr finalizingVisitor(
        rVisitorFactory.createStyleCollectingVisitor(aStyles,*this));
    // FIXME: localization

    m_pDocument->visitedBy( *finalizingVisitor, std::list<std::unique_ptr<Element>>::const_iterator() );

    EmitContext aContext( rEmitter, aStyles, m_aImages, *this, m_xStatusIndicator, m_xContext );
    ElementTreeVisitorSharedPtr aEmittingVisitor(
        rVisitorFactory.createEmittingVisitor(aContext));

    PropertyMap aProps;
    // document prolog
    #define OASIS_STR "urn:oasis:names:tc:opendocument:xmlns:"
    aProps[ u"xmlns:office"_ustr ]      = OASIS_STR "office:1.0" ;
    aProps[ u"xmlns:style"_ustr ]       = OASIS_STR "style:1.0" ;
    aProps[ u"xmlns:text"_ustr ]        = OASIS_STR "text:1.0" ;
    aProps[ u"xmlns:svg"_ustr ]         = OASIS_STR "svg-compatible:1.0" ;
    aProps[ u"xmlns:table"_ustr ]       = OASIS_STR "table:1.0" ;
    aProps[ u"xmlns:draw"_ustr ]        = OASIS_STR "drawing:1.0" ;
    aProps[ u"xmlns:fo"_ustr ]          = OASIS_STR "xsl-fo-compatible:1.0" ;
    aProps[ u"xmlns:xlink"_ustr]        = "http://www.w3.org/1999/xlink";
    aProps[ u"xmlns:dc"_ustr]           = "http://purl.org/dc/elements/1.1/";
    aProps[ u"xmlns:number"_ustr]       = OASIS_STR "datastyle:1.0" ;
    aProps[ u"xmlns:presentation"_ustr] = OASIS_STR "presentation:1.0" ;
    aProps[ u"xmlns:math"_ustr]         = "http://www.w3.org/1998/Math/MathML";
    aProps[ u"xmlns:form"_ustr]         = OASIS_STR "form:1.0" ;
    aProps[ u"xmlns:script"_ustr]       = OASIS_STR "script:1.0" ;
    aProps[ u"xmlns:dom"_ustr]          = "http://www.w3.org/2001/xml-events";
    aProps[ u"xmlns:xforms"_ustr]       = "http://www.w3.org/2002/xforms";
    aProps[ u"xmlns:xsd"_ustr]          = "http://www.w3.org/2001/XMLSchema";
    aProps[ u"xmlns:xsi"_ustr]          = "http://www.w3.org/2001/XMLSchema-instance";
    aProps[ u"office:version"_ustr ]    = "1.0";

    aContext.rEmitter.beginTag( "office:document", aProps );

    // emit style list
    aStyles.emit( aContext, *aEmittingVisitor );

    m_pDocument->visitedBy( *aEmittingVisitor, std::list<std::unique_ptr<Element>>::const_iterator() );
    aContext.rEmitter.endTag( "office:document" );
    endIndicator();
}

void PDFIProcessor::startIndicator( const OUString& rText  )
{
    sal_Int32 nElements = m_nPages;
    if( !m_xStatusIndicator.is() )
        return;

    sal_Int32 nLength = rText.getLength();
    OUStringBuffer aStr( nLength*2 );
    const sal_Unicode* pText = rText.getStr();
    for( int i = 0; i < nLength; i++ )
    {
        if( nLength-i > 1&&
            pText[i]   == '%' &&
            pText[i+1] == 'd'
        )
        {
            aStr.append( nElements );
            i++;
        }
        else
            aStr.append( pText[i] );
    }
    m_xStatusIndicator->start( aStr.makeStringAndClear(), nElements );
}

void PDFIProcessor::endIndicator()
{
    if( m_xStatusIndicator.is() )
        m_xStatusIndicator->end();
}

static bool lr_tb_sort( std::unique_ptr<Element> const & pLeft, std::unique_ptr<Element> const & pRight )
{
    // Ensure irreflexivity (which could be compromised if h or w is negative):
    if (pLeft == pRight)
        return false;

    // first: top-bottom sorting

    // Note: allow for 10% overlap on text lines since text lines are usually
    // of the same order as font height whereas the real paint area
    // of text is usually smaller
    double fudge_factor_left = 0.0, fudge_factor_right = 0.0;
    if( pLeft->dynCastAsTextElement() )
        fudge_factor_left = 0.1;
    if( pRight->dynCastAsTextElement() )
        fudge_factor_right = 0.1;

    // Allow negative height
    double lower_boundary_left  = pLeft->y  + std::max(pLeft->h, 0.0)  - fabs(pLeft->h)  * fudge_factor_left;
    double lower_boundary_right = pRight->y + std::max(pRight->h, 0.0) - fabs(pRight->h) * fudge_factor_right;
    double upper_boundary_left  = pLeft->y  + std::min(pLeft->h, 0.0);
    double upper_boundary_right = pRight->y + std::min(pRight->h, 0.0);
    // if left's lower boundary is above right's upper boundary
    // then left is smaller
    if( lower_boundary_left < upper_boundary_right )
        return true;
    // if right's lower boundary is above left's upper boundary
    // then left is definitely not smaller
    if( lower_boundary_right < upper_boundary_left )
        return false;

    // Allow negative width
    double left_boundary_left   = pLeft->y  + std::min(pLeft->w, 0.0);
    double left_boundary_right  = pRight->y + std::min(pRight->w, 0.0);
    double right_boundary_left  = pLeft->y  + std::max(pLeft->w, 0.0);
    double right_boundary_right = pRight->y + std::max(pRight->w, 0.0);
    // by now we have established that left and right are inside
    // a "line", that is they have vertical overlap
    // second: left-right sorting
    // if left's right boundary is left to right's left boundary
    // then left is smaller
    if( right_boundary_left < left_boundary_right )
        return true;
    // if right's right boundary is left to left's left boundary
    // then left is definitely not smaller
    if( right_boundary_right < left_boundary_left )
        return false;

    // here we have established vertical and horizontal overlap
    // so sort left first, top second
    if( pLeft->x < pRight->x )
        return true;
    if( pRight->x < pLeft->x )
        return false;
    if( pLeft->y < pRight->y )
        return true;

    return false;
}

void PDFIProcessor::sortElements(Element* pEle)
{
    if( pEle->Children.empty() )
        return;

    // sort method from std::list is equivalent to stable_sort
    // See S Meyers, Effective STL
    pEle->Children.sort(lr_tb_sort);
}

/* Produce mirrored-image for each code point which has the Bidi_Mirrored property, within a string.
   This need to be done in forward order.
*/
OUString PDFIProcessor::SubstituteBidiMirrored(std::u16string_view rString)
{
    const sal_Int32 nLen = rString.size();
    OUStringBuffer aMirror(nLen);

    for (sal_Int32 i = 0; i < nLen;) {
        const sal_uInt32 nCodePoint = o3tl::iterateCodePoints(rString, &i);
        aMirror.appendUtf32(unicode::GetMirroredChar(nCodePoint));
    }
    return aMirror.makeStringAndClear();
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
