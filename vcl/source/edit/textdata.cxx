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
#include <sal/log.hxx>
#include <osl/diagnose.h>

#include <cstddef>

#include <utility>
#include <vcl/textdata.hxx>

#include "TextLine.hxx"
#include "textdat2.hxx"

TextSelection::TextSelection()
{
}

TextSelection::TextSelection( const TextPaM& rPaM ) :
    maStartPaM( rPaM ), maEndPaM( rPaM )
{
}

TextSelection::TextSelection( const TextPaM& rStart, const TextPaM& rEnd ) :
    maStartPaM( rStart ), maEndPaM( rEnd )
{
}

void TextSelection::Justify()
{
    if ( maEndPaM < maStartPaM )
    {
        TextPaM aTemp( maStartPaM );
        maStartPaM = maEndPaM;
        maEndPaM = aTemp;
    }
}

TETextPortionList::TETextPortionList()
{
}

TETextPortionList::~TETextPortionList()
{
    Reset();
}

TETextPortion& TETextPortionList::operator[]( std::size_t nPos )
{
    return maPortions[ nPos ];
}

std::vector<TETextPortion>::iterator TETextPortionList::begin()
{
    return maPortions.begin();
}

std::vector<TETextPortion>::const_iterator TETextPortionList::begin() const
{
    return maPortions.begin();
}

std::vector<TETextPortion>::iterator TETextPortionList::end()
{
    return maPortions.end();
}

std::vector<TETextPortion>::const_iterator TETextPortionList::end() const
{
    return maPortions.end();
}

bool TETextPortionList::empty() const
{
    return maPortions.empty();
}

std::size_t TETextPortionList::size() const
{
    return maPortions.size();
}

std::vector<TETextPortion>::iterator TETextPortionList::erase( const std::vector<TETextPortion>::iterator& aIter )
{
    return maPortions.erase( aIter );
}

std::vector<TETextPortion>::iterator TETextPortionList::insert( const std::vector<TETextPortion>::iterator& aIter,
                                                                 const TETextPortion& rTP )
{
    return maPortions.insert( aIter, rTP );
}

void TETextPortionList::push_back( const TETextPortion& rTP )
{
    maPortions.push_back( rTP );
}

void TETextPortionList::Reset()
{
    maPortions.clear();
}

void TETextPortionList::DeleteFromPortion( std::size_t nDelFrom )
{
    SAL_WARN_IF( ( nDelFrom >= maPortions.size() ) && ( (nDelFrom != 0) || (!maPortions.empty()) ), "vcl", "DeleteFromPortion: Out of range" );
    maPortions.erase( maPortions.begin() + nDelFrom, maPortions.end() );
}

std::size_t TETextPortionList::FindPortion( sal_Int32 nCharPos, sal_Int32& nPortionStart, bool bPreferStartingPortion )
{
    // find left portion at nCharPos at portion border
    sal_Int32 nTmpPos = 0;
    for ( std::size_t nPortion = 0; nPortion < maPortions.size(); nPortion++ )
    {
        TETextPortion& rPortion = maPortions[ nPortion ];
        nTmpPos += rPortion.GetLen();
        if ( nTmpPos >= nCharPos )
        {
            // take this one if we don't prefer the starting portion, or if it's the last one
            if ( ( nTmpPos != nCharPos ) || !bPreferStartingPortion || ( nPortion == maPortions.size() - 1 ) )
            {
                nPortionStart = nTmpPos - rPortion.GetLen();
                return nPortion;
            }
        }
    }
    OSL_FAIL( "FindPortion: Not found!" );
    return ( maPortions.size() - 1 );
}

TEParaPortion::TEParaPortion( TextNode* pN )
    : mpNode {pN}
    , mnInvalidPosStart {0}
    , mnInvalidDiff {0}
    , mbInvalid {true}
    , mbSimple {false}
{
}

TEParaPortion::~TEParaPortion()
{
}

void TEParaPortion::MarkInvalid( sal_Int32 nStart, sal_Int32 nDiff )
{
    if ( !mbInvalid )
    {
        mnInvalidPosStart = ( nDiff >= 0 ) ? nStart : ( nStart + nDiff );
        mnInvalidDiff = nDiff;
    }
    else
    {
        // simple consecutive typing
        if ( ( nDiff > 0 ) && ( mnInvalidDiff > 0 ) &&
             ( ( mnInvalidPosStart+mnInvalidDiff ) == nStart ) )
        {
            mnInvalidDiff = mnInvalidDiff + nDiff;
        }
        // simple consecutive deleting
        else if ( ( nDiff < 0 ) && ( mnInvalidDiff < 0 ) && ( mnInvalidPosStart == nStart ) )
        {
            mnInvalidPosStart = mnInvalidPosStart + nDiff;
            mnInvalidDiff = mnInvalidDiff + nDiff;
        }
        else
        {
            SAL_WARN_IF( ( nDiff < 0 ) && ( (nStart+nDiff) < 0 ), "vcl", "MarkInvalid: Diff out of Range" );
            mnInvalidPosStart = std::min( mnInvalidPosStart, nDiff < 0 ? nStart+nDiff : nDiff );
            mnInvalidDiff = 0;
            mbSimple = false;
        }
    }

    maWritingDirectionInfos.clear();

    mbInvalid = true;
}

void TEParaPortion::MarkSelectionInvalid( sal_Int32 nStart )
{
    if ( !mbInvalid )
    {
        mnInvalidPosStart = nStart;
    }
    else
    {
        mnInvalidPosStart = std::min( mnInvalidPosStart, nStart );
    }

    maWritingDirectionInfos.clear();

    mnInvalidDiff = 0;
    mbInvalid = true;
    mbSimple = false;
}

std::vector<TextLine>::size_type TEParaPortion::GetLineNumber( sal_Int32 nChar, bool bInclEnd )
{
    for ( std::vector<TextLine>::size_type nLine = 0; nLine < maLines.size(); nLine++ )
    {
        TextLine& rLine = maLines[ nLine ];
        if ( ( bInclEnd && ( rLine.GetEnd() >= nChar ) ) ||
             ( rLine.GetEnd() > nChar ) )
        {
            return nLine;
        }
    }

    // Then it should be at the end of the last line
    OSL_ENSURE(nChar == maLines.back().GetEnd(), "wrong Index");
    OSL_ENSURE(!bInclEnd, "Line not found: FindLine");
    return ( maLines.size() - 1 );
}

void TEParaPortion::CorrectValuesBehindLastFormattedLine( sal_uInt16 nLastFormattedLine )
{
    sal_uInt16 nLines = maLines.size();
    SAL_WARN_IF( !nLines, "vcl", "CorrectPortionNumbersFromLine: Empty portion?" );
    if ( nLastFormattedLine >= ( nLines - 1 ) )
        return;

    const TextLine& rLastFormatted = maLines[ nLastFormattedLine ];
    const TextLine& rUnformatted = maLines[ nLastFormattedLine+1 ];
    std::ptrdiff_t nPortionDiff = rUnformatted.GetStartPortion() - rLastFormatted.GetEndPortion();
    sal_Int32 nTextDiff = rUnformatted.GetStart() - rLastFormatted.GetEnd();
    nTextDiff++;    // LastFormatted.GetEnd() was inclusive => subtracted one too much!

    // The first unformatted one has to start exactly one portion past the last
    // formatted one.
    // If a portion got split in the changed row, nLastEnd could be > nNextStart!
    std::ptrdiff_t nPDiff = -( nPortionDiff-1 );
    const sal_Int32 nTDiff = -( nTextDiff-1 );
    if ( !(nPDiff || nTDiff) )
        return;

    for ( sal_uInt16 nL = nLastFormattedLine+1; nL < nLines; nL++ )
    {
        TextLine& rLine = maLines[ nL ];

        rLine.SetStartPortion(rLine.GetStartPortion() + nPDiff);
        rLine.SetEndPortion(rLine.GetEndPortion() + nPDiff);

        rLine.SetStart(rLine.GetStart() + nTDiff);
        rLine.SetEnd(rLine.GetEnd() + nTDiff);

        rLine.SetValid();
    }
}

TEParaPortions::~TEParaPortions()
{
}

TextHint::TextHint( SfxHintId Id ) : SfxHint( Id ), mnValue(0)
{
}

TextHint::TextHint( SfxHintId Id, sal_Int32 nValue ) : SfxHint( Id ), mnValue(nValue)
{
}

TEIMEInfos::TEIMEInfos( const TextPaM& rPos, OUString _aOldTextAfterStartPos )
    : aOldTextAfterStartPos(std::move(_aOldTextAfterStartPos))
    , aPos(rPos)
    , nLen(0)
    , bWasCursorOverwrite(false)
{
}

TEIMEInfos::~TEIMEInfos()
{
}

void TEIMEInfos::CopyAttribs(const ExtTextInputAttr* pA, sal_Int32 nL)
{
    nLen = nL;
    pAttribs.reset( new ExtTextInputAttr[ nL ] );
    memcpy( pAttribs.get(), pA, nL*sizeof(ExtTextInputAttr) );
}

void TEIMEInfos::DestroyAttribs()
{
    pAttribs.reset();
    nLen = 0;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
