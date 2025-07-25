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

#include <com/sun/star/text/ReferenceFieldPart.hpp>
#include <com/sun/star/text/ReferenceFieldSource.hpp>
#include <o3tl/unreachable.hxx>
#include <unotools/localedatawrapper.hxx>
#include <unotools/charclass.hxx>
#include <doc.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentMarkAccess.hxx>
#include <pam.hxx>
#include <cntfrm.hxx>
#include <pagefrm.hxx>
#include <rootfrm.hxx>
#include <modeltoviewhelper.hxx>
#include <fmtfld.hxx>
#include <txtfld.hxx>
#include <txtftn.hxx>
#include <fmtrfmrk.hxx>
#include <txtrfmrk.hxx>
#include <fmtftn.hxx>
#include <ndtxt.hxx>
#include <chpfld.hxx>
#include <reffld.hxx>
#include <expfld.hxx>
#include <txtfrm.hxx>
#include <notxtfrm.hxx>
#include <flyfrm.hxx>
#include <pagedesc.hxx>
#include <IMark.hxx>
#include <crossrefbookmark.hxx>
#include <ftnidx.hxx>
#include <utility>
#include <viewsh.hxx>
#include <unofldmid.h>
#include <SwStyleNameMapper.hxx>
#include <shellres.hxx>
#include <poolfmt.hxx>
#include <strings.hrc>
#include <numrule.hxx>
#include <SwNodeNum.hxx>
#include <calbck.hxx>
#include <names.hxx>

#include <cstddef>
#include <memory>
#include <vector>
#include <set>
#include <string_view>
#include <map>
#include <algorithm>
#include <deque>

using namespace ::com::sun::star;
using namespace ::com::sun::star::text;

static std::pair<OUString, bool> MakeRefNumStr(SwRootFrame const* pLayout,
      const SwTextNode& rTextNodeOfField,
      const SwTextNode& rTextNodeOfReferencedItem,
      ReferencesSubtype nSubType,
      RefFieldFormat nRefNumFormat,
      sal_uInt16 nFlags);

static void lcl_GetLayTree( const SwFrame* pFrame, std::vector<const SwFrame*>& rArr )
{
    while( pFrame )
    {
        if( pFrame->IsBodyFrame() ) // unspectacular
            pFrame = pFrame->GetUpper();
        else
        {
            rArr.push_back( pFrame );

            // this is the last page
            if( pFrame->IsPageFrame() )
                break;

            if( pFrame->IsFlyFrame() )
                pFrame = static_cast<const SwFlyFrame*>(pFrame)->GetAnchorFrame();
            else
                pFrame = pFrame->GetUpper();
        }
    }
}

bool IsFrameBehind( const SwTextNode& rMyNd, sal_Int32 nMySttPos,
                    const SwTextNode& rBehindNd, sal_Int32 nSttPos )
{
    const SwTextFrame * pMyFrame = static_cast<SwTextFrame*>(rMyNd.getLayoutFrame(
        rMyNd.GetDoc().getIDocumentLayoutAccess().GetCurrentLayout(), nullptr, nullptr));
    const SwTextFrame * pFrame = static_cast<SwTextFrame*>(rBehindNd.getLayoutFrame(
        rBehindNd.GetDoc().getIDocumentLayoutAccess().GetCurrentLayout(), nullptr, nullptr));

    if( !pFrame || !pMyFrame)
        return false;

    TextFrameIndex const nMySttPosIndex(pMyFrame->MapModelToView(&rMyNd, nMySttPos));
    TextFrameIndex const nSttPosIndex(pFrame->MapModelToView(&rBehindNd, nSttPos));
    while (pFrame && !pFrame->IsInside(nSttPosIndex))
        pFrame = pFrame->GetFollow();
    while (pMyFrame && !pMyFrame->IsInside(nMySttPosIndex))
        pMyFrame = pMyFrame->GetFollow();

    if( !pFrame || !pMyFrame || pFrame == pMyFrame )
        return false;

    std::vector<const SwFrame*> aRefArr, aArr;
    ::lcl_GetLayTree( pFrame, aRefArr );
    ::lcl_GetLayTree( pMyFrame, aArr );

    size_t nRefCnt = aRefArr.size() - 1, nCnt = aArr.size() - 1;
    bool bVert = false;
    bool bR2L = false;

    // Loop as long as a frame does not equal?
    while( nRefCnt && nCnt && aRefArr[ nRefCnt ] == aArr[ nCnt ] )
    {
        const SwFrame* pTmpFrame = aArr[ nCnt ];
        bVert = pTmpFrame->IsVertical();
        bR2L = pTmpFrame->IsRightToLeft();
        --nCnt;
        --nRefCnt;
    }

    // If a counter overflows?
    if( aRefArr[ nRefCnt ] == aArr[ nCnt ] )
    {
        if( nCnt )
            --nCnt;
        else
            --nRefCnt;
    }

    const SwFrame* pRefFrame = aRefArr[ nRefCnt ];
    const SwFrame* pFieldFrame = aArr[ nCnt ];

    // different frames, check their Y-/X-position
    bool bRefIsLower = false;
    if( ( SwFrameType::Column | SwFrameType::Cell ) & pFieldFrame->GetType() ||
        ( SwFrameType::Column | SwFrameType::Cell ) & pRefFrame->GetType() )
    {
        if( pFieldFrame->GetType() == pRefFrame->GetType() )
        {
            // here, the X-pos is more important
            if( bVert )
            {
                if( bR2L )
                    bRefIsLower = pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() ||
                            ( pRefFrame->getFrameArea().Top() == pFieldFrame->getFrameArea().Top() &&
                              pRefFrame->getFrameArea().Left() < pFieldFrame->getFrameArea().Left() );
                else
                    bRefIsLower = pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() ||
                            ( pRefFrame->getFrameArea().Top() == pFieldFrame->getFrameArea().Top() &&
                              pRefFrame->getFrameArea().Left() > pFieldFrame->getFrameArea().Left() );
            }
            else if( bR2L )
                bRefIsLower = pRefFrame->getFrameArea().Left() > pFieldFrame->getFrameArea().Left() ||
                            ( pRefFrame->getFrameArea().Left() == pFieldFrame->getFrameArea().Left() &&
                              pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() );
            else
                bRefIsLower = pRefFrame->getFrameArea().Left() < pFieldFrame->getFrameArea().Left() ||
                            ( pRefFrame->getFrameArea().Left() == pFieldFrame->getFrameArea().Left() &&
                              pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() );
            pRefFrame = nullptr;
        }
        else if( ( SwFrameType::Column | SwFrameType::Cell ) & pFieldFrame->GetType() )
            pFieldFrame = aArr[ nCnt - 1 ];
        else
            pRefFrame = aRefArr[ nRefCnt - 1 ];
    }

    if( pRefFrame ) // misuse as flag
    {
        if( bVert )
        {
            if( bR2L )
                bRefIsLower = pRefFrame->getFrameArea().Left() < pFieldFrame->getFrameArea().Left() ||
                            ( pRefFrame->getFrameArea().Left() == pFieldFrame->getFrameArea().Left() &&
                                pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() );
            else
                bRefIsLower = pRefFrame->getFrameArea().Left() > pFieldFrame->getFrameArea().Left() ||
                            ( pRefFrame->getFrameArea().Left() == pFieldFrame->getFrameArea().Left() &&
                                pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() );
        }
        else if( bR2L )
            bRefIsLower = pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() ||
                        ( pRefFrame->getFrameArea().Top() == pFieldFrame->getFrameArea().Top() &&
                            pRefFrame->getFrameArea().Left() > pFieldFrame->getFrameArea().Left() );
        else
            bRefIsLower = pRefFrame->getFrameArea().Top() < pFieldFrame->getFrameArea().Top() ||
                        ( pRefFrame->getFrameArea().Top() == pFieldFrame->getFrameArea().Top() &&
                            pRefFrame->getFrameArea().Left() < pFieldFrame->getFrameArea().Left() );
    }
    return bRefIsLower;
}

// tdf#115319 create alternative reference formats, if the user asked for it
// (ReferenceFieldLanguage attribute of the reference field is not empty), and
// language of the text and ReferenceFieldLanguage are the same.
// Right now only HUNGARIAN seems to need this (as in the related issue,
// the reversed caption order in autocaption, solved by #i61007#)
static void lcl_formatReferenceLanguage( OUString& rRefText,
                                         bool bClosingParenthesis, LanguageType eLang,
                                         std::u16string_view rReferenceLanguage)
{
    if (eLang != LANGUAGE_HUNGARIAN || (rReferenceLanguage != u"hu" && rReferenceLanguage != u"Hu"))
        return;

    // Add Hungarian definitive article (a/az) before references,
    // similar to \aref, \apageref etc. of LaTeX Babel package.
    //
    // for example:
    //
    //     "az 1. oldalon" ("on page 1"), but
    //     "a 2. oldalon" ("on page 2")
    //     "a fentebbi", "az alábbi" (above/below)
    //     "a Lorem", "az Ipsum"
    //
    // Support following numberings of EU publications:
    //
    // 1., 1a., a), (1), (1a), iii., III., IA.
    //
    // (http://publications.europa.eu/code/hu/hu-120700.htm,
    // http://publications.europa.eu/code/hu/hu-4100600.htm)

    CharClass aCharClass(( LanguageTag(eLang) ));
    sal_Int32 nLen = rRefText.getLength();
    sal_Int32 i;
    // substring of rRefText starting with letter or number
    OUString sNumbering;
    // is article "az"?
    bool bArticleAz = false;
    // is numbering a number?
    bool bNum = false;

    // search first member of the numbering (numbers or letters)
    for (i=0; i<nLen && (sNumbering.isEmpty() ||
                ((bNum && aCharClass.isDigit(rRefText, i)) ||
                (!bNum && aCharClass.isLetter(rRefText, i)))); ++i)
    {
      // start of numbering within the field text
      if (sNumbering.isEmpty() && aCharClass.isLetterNumeric(rRefText, i)) {
          sNumbering = rRefText.copy(i);
          bNum = aCharClass.isDigit(rRefText, i);
      }
    }

    // length of numbering
    nLen = i - (rRefText.getLength() - sNumbering.getLength());

    if (bNum)
    {
        // az 1, 1000, 1000000, 1000000000...
        // az 5, 50, 500...
        if ((sNumbering.startsWith("1") && (nLen == 1 || nLen == 4 || nLen == 7 || nLen == 10)) ||
            sNumbering.startsWith("5"))
                bArticleAz = true;
    }
    else if (nLen == 1 && sNumbering[0] < 128)
    {
        // ASCII 1-letter numbering
        // az a), e), f) ... x)
        // az i., v. (but, a x.)
        static const std::u16string_view sLettersStartingWithVowels = u"aefilmnorsuxyAEFILMNORSUXY";
        if (sLettersStartingWithVowels.find(sNumbering[0]) != std::u16string_view::npos)
        {
            // x),  X) are letters, but x. and X. etc. are Roman numbers
            if (bClosingParenthesis ||
                (sNumbering[0] != 'x' && sNumbering[0] != 'X'))
                    bArticleAz = true;
        } else if ((sNumbering[0] == 'v' || sNumbering[0] == 'V') && !bClosingParenthesis)
            // v), V) are letters, but v. and V. are Roman numbers
            bArticleAz = true;
    }
    else
    {
        static const sal_Unicode sVowelsWithDiacritic[] = {
            0x00E1, 0x00C1, 0x00E9, 0x00C9, 0x00ED, 0x00CD,
            0x00F3, 0x00D3, 0x00F6, 0x00D6, 0x0151, 0x0150,
            0x00FA, 0x00DA, 0x00FC, 0x00DC, 0x0171, 0x0170, 0 };
        static const OUString sVowels = OUString::Concat(u"aAeEiIoOuU") + sVowelsWithDiacritic;

        // handle more than 1-letter long Roman numbers and
        // their possible combinations with letters:
        // az IA, a IIB, a IIIC., az Ia, a IIb., a iiic), az LVIII. szonett
        bool bRomanNumber = false;
        if (nLen > 1 && (nLen + 1 >= sNumbering.getLength() || sNumbering[nLen] == '.'))
        {
            sal_Unicode last = sNumbering[nLen - 1];
            OUString sNumberingTrim;
            if ((last >= 'A' && last < 'I') || (last >= 'a' && last < 'i'))
                sNumberingTrim = sNumbering.copy(0, nLen - 1);
            else
                sNumberingTrim = sNumbering.copy(0, nLen);
            bRomanNumber =
                sNumberingTrim.replaceAll("i", "").replaceAll("v", "").replaceAll("x", "").replaceAll("l", "").replaceAll("c", "").isEmpty() ||
                sNumberingTrim.replaceAll("I", "").replaceAll("V", "").replaceAll("X", "").replaceAll("L", "").replaceAll("C", "").isEmpty();
        }

        if (
             // Roman number and a letter optionally
             ( bRomanNumber && (
                  (sNumbering[0] == 'i' && sNumbering[1] != 'i' && sNumbering[1] != 'v' && sNumbering[1] != 'x') ||
                  (sNumbering[0] == 'I' && sNumbering[1] != 'I' && sNumbering[1] != 'V' && sNumbering[1] != 'X') ||
                  (sNumbering[0] == 'v' && sNumbering[1] != 'i') ||
                  (sNumbering[0] == 'V' && sNumbering[1] != 'I') ||
                  (sNumbering[0] == 'l' && sNumbering[1] != 'x') ||
                  (sNumbering[0] == 'L' && sNumbering[1] != 'X')) ) ||
             // a word starting with vowel (not Roman number)
             ( !bRomanNumber && sVowels.indexOf(sNumbering[0]) != -1))
        {
            bArticleAz = true;
        }
    }
    // not a title text starting already with a definitive article
    if ( sNumbering.startsWith("A ") || sNumbering.startsWith("Az ") ||
         sNumbering.startsWith("a ") || sNumbering.startsWith("az ") )
        return;

    // lowercase, if rReferenceLanguage == "hu", not "Hu"
    OUString sArticle;

    if ( rReferenceLanguage == u"hu" )
        sArticle = "a";
    else
        sArticle = "A";

    if (bArticleAz)
        sArticle += "z";

    rRefText = sArticle + " " + rRefText;
}

/// get references
SwGetRefField::SwGetRefField( SwGetRefFieldType* pFieldType,
                              SwMarkName aSetRef, OUString aSetReferenceLanguage, ReferencesSubtype nSubTyp,
                              sal_uInt16 nSequenceNo, sal_uInt16 nFlags, RefFieldFormat nFormat )
    : SwField(pFieldType),
      m_sSetRefName(std::move(aSetRef)),
      m_sSetReferenceLanguage(std::move(aSetReferenceLanguage)),
      m_nSubType(nSubTyp),
      m_nSeqNo(nSequenceNo),
      m_nFlags(nFlags),
      m_nFormat(nFormat)
{
}

SwGetRefField::~SwGetRefField()
{
}

OUString SwGetRefField::GetDescription() const
{
    return SwResId(STR_REFERENCE);
}

ReferencesSubtype SwGetRefField::GetSubType() const
{
    return m_nSubType;
}

void SwGetRefField::SetSubType( ReferencesSubtype n )
{
    m_nSubType = n;
}

// #i81002#
bool SwGetRefField::IsRefToHeadingCrossRefBookmark() const
{
    return GetSubType() == ReferencesSubtype::Bookmark &&
        ::sw::mark::CrossRefHeadingBookmark::IsLegalName(m_sSetRefName);
}

bool SwGetRefField::IsRefToNumItemCrossRefBookmark() const
{
    return GetSubType() == ReferencesSubtype::Bookmark &&
        ::sw::mark::CrossRefNumItemBookmark::IsLegalName(m_sSetRefName);
}

const SwTextNode* SwGetRefField::GetReferencedTextNode(const SwTextNode* pTextNode, SwFrame* pFrame) const
{
    SwGetRefFieldType *pTyp = dynamic_cast<SwGetRefFieldType*>(GetTyp());
    if (!pTyp)
        return nullptr;
    sal_Int32 nDummy = -1;
    return SwGetRefFieldType::FindAnchor( &pTyp->GetDoc(), m_sSetRefName, m_nSubType, m_nSeqNo, m_nFlags, &nDummy,
                                          nullptr, nullptr, pTextNode, pFrame );
}

// strikethrough for tooltips using Unicode combining character
static OUString lcl_formatStringByCombiningCharacter(std::u16string_view sText, const sal_Unicode cChar)
{
    OUStringBuffer sRet(sText.size() * 2);
    for (size_t i = 0; i < sText.size(); ++i)
    {
        sRet.append(OUStringChar(sText[i]) + OUStringChar(cChar));
    }
    return sRet.makeStringAndClear();
}

// #i85090#
OUString SwGetRefField::GetExpandedTextOfReferencedTextNode(
        SwRootFrame const& rLayout) const
{
    const SwTextNode* pReferencedTextNode( GetReferencedTextNode(/*pTextNode*/nullptr, /*pFrame*/nullptr) );
    if ( !pReferencedTextNode )
        return OUString();

    // show the referenced text without the deletions, but if the whole text was
    // deleted, show the original text for the sake of the comfortable reviewing,
    // but with Unicode strikethrough in the tooltip
    OUString sRet = sw::GetExpandTextMerged(&rLayout, *pReferencedTextNode, true, false, ExpandMode::HideDeletions);
    if ( sRet.isEmpty() )
    {
       static const sal_Unicode cStrikethrough = u'\x0336';
       sRet = sw::GetExpandTextMerged(&rLayout, *pReferencedTextNode, true, false, ExpandMode(0));
       sRet = lcl_formatStringByCombiningCharacter( sRet, cStrikethrough );
    }

    return sRet;
}

void SwGetRefField::SetExpand( const OUString& rStr )
{
    m_sText = rStr;
    m_sTextRLHidden = rStr;
}

OUString SwGetRefField::ExpandImpl(SwRootFrame const*const pLayout) const
{
    return pLayout && pLayout->IsHideRedlines() ? m_sTextRLHidden : m_sText;
}

OUString SwGetRefField::GetFieldName() const
{
    const OUString aName = GetTyp()->GetName().toString();
    if ( !aName.isEmpty() || !m_sSetRefName.isEmpty() )
    {
        return aName + " " + m_sSetRefName.toString();
    }
    return ExpandImpl(nullptr);
}


static void FilterText(OUString & rText, LanguageType const eLang,
        std::u16string_view rSetReferenceLanguage)
{
    // remove all special characters (replace them with blanks)
    if (rText.isEmpty())
        return;

    rText = rText.replaceAll(u"\u00ad", "");
    OUStringBuffer aBuf(rText);
    const sal_Int32 l = aBuf.getLength();
    for (sal_Int32 i = 0; i < l; ++i)
    {
        if (aBuf[i] < ' ')
        {
            aBuf[i] = ' ';
        }
        else if (aBuf[i] == 0x2011)
        {
            aBuf[i] = '-';
        }
    }
    rText = aBuf.makeStringAndClear();
    if (!rSetReferenceLanguage.empty())
    {
        lcl_formatReferenceLanguage(rText, false, eLang, rSetReferenceLanguage);
    }
}

// #i81002# - parameter <pFieldTextAttr> added
void SwGetRefField::UpdateField( const SwTextField* pFieldTextAttr, SwFrame* pFrame )
{
    SwDoc& rDoc = static_cast<SwGetRefFieldType*>(GetTyp())->GetDoc();

    for (SwRootFrame const* const pLay : rDoc.GetAllLayouts())
    {
        if (pLay->IsHideRedlines())
        {
            UpdateField(pFieldTextAttr, pFrame, pLay, m_sTextRLHidden);
        }
        else
        {
            UpdateField(pFieldTextAttr, pFrame, pLay, m_sText);
        }
    }
}

void SwGetRefField::UpdateField(const SwTextField* pFieldTextAttr, SwFrame* pFrameContainingField,
                                const SwRootFrame* const pLayout, OUString& rText)
{
    SwDoc& rDoc = static_cast<SwGetRefFieldType*>(GetTyp())->GetDoc();

    rText.clear();

    // finding the reference target (the number)
    sal_Int32 nNumStart = -1;
    sal_Int32 nNumEnd = -1;
    SwTextNode* pTextNd = SwGetRefFieldType::FindAnchor(
        &rDoc, m_sSetRefName, m_nSubType, m_nSeqNo, m_nFlags, &nNumStart, &nNumEnd,
        pLayout, pFieldTextAttr ? pFieldTextAttr->GetpTextNode() : nullptr, pFrameContainingField
    );
    // not found?
    if ( !pTextNd )
    {
        rText = SwViewShell::GetShellRes()->aGetRefField_RefItemNotFound;

        return;
    }

    // where is the category name (e.g. "Illustration")?
    const OUString aText = pTextNd->GetText();
    const sal_Int32 nCatStart = aText.indexOf(m_sSetRefName.toString());
    const bool bHasCat = nCatStart>=0;
    const sal_Int32 nCatEnd = bHasCat ? nCatStart + m_sSetRefName.toString().getLength() : -1;

    // length of the referenced text
    const sal_Int32 nLen = aText.getLength();

    // which format?
    switch( GetFormat() )
    {
    case RefFieldFormat::Content:
    case RefFieldFormat::CategoryAndNumber:
    case RefFieldFormat::CaptionText:
    case RefFieldFormat::Numbering:
        {
            // needed part of Text
            sal_Int32 nStart;
            sal_Int32 nEnd;

            switch( m_nSubType )
            {
            case ReferencesSubtype::SequenceField:

                switch( GetFormat() )
                {
                // "Category and Number"
                case RefFieldFormat::CategoryAndNumber:
                    if (bHasCat) {
                        nStart = std::min(nNumStart, nCatStart);
                        nEnd = std::max(nNumEnd, nCatEnd);
                    } else {
                        nStart = nNumStart;
                        nEnd = nNumEnd;
                    }
                    break;

                // "Caption Text"
                case RefFieldFormat::CaptionText: {
                    // next alphanumeric character after category+number
                    if (const SwTextAttr* const pTextAttr =
                        pTextNd->GetTextAttrForCharAt(nNumStart, RES_TXTATR_FIELD)
                    ) {
                        // start searching from nFrom
                        const sal_Int32 nFrom = bHasCat
                            ? std::max(nNumStart + 1, nCatEnd)
                            : nNumStart + 1;
                        nStart = SwGetExpField::GetReferenceTextPos( pTextAttr->GetFormatField(), rDoc, nFrom );
                    } else {
                        nStart = bHasCat ? std::max(nNumEnd, nCatEnd) : nNumEnd;
                    }
                    nEnd = nLen;
                    break;
                }

                // "Numbering"
                case RefFieldFormat::Numbering:
                    nStart = nNumStart;
                    nEnd = std::min(nStart + 1, nLen);
                    break;

                // "Reference" (whole Text)
                case RefFieldFormat::Content:
                    nStart = 0;
                    nEnd = nLen;
                    break;

                default:
                    O3TL_UNREACHABLE;
                }
                break;

            case ReferencesSubtype::Bookmark:
                nStart = nNumStart;
                // text is spread across multiple nodes - get whole text or only until end of node?
                nEnd = nNumEnd<0 ? nLen : nNumEnd;
                break;

            case ReferencesSubtype::Outline:
            case ReferencesSubtype::SetRefAttr:
                nStart = nNumStart;
                nEnd = nNumEnd;
                break;

            case ReferencesSubtype::Footnote:
            case ReferencesSubtype::Endnote:
                // get number or numString
                for( size_t i = 0; i < rDoc.GetFootnoteIdxs().size(); ++i )
                {
                    SwTextFootnote* const pFootnoteIdx = rDoc.GetFootnoteIdxs()[i];
                    if( m_nSeqNo == pFootnoteIdx->GetSeqRefNo() )
                    {
                        rText = pFootnoteIdx->GetFootnote().GetViewNumStr(rDoc, pLayout);
                        if (!m_sSetReferenceLanguage.isEmpty())
                        {
                            lcl_formatReferenceLanguage(rText, false, GetLanguage(), m_sSetReferenceLanguage);
                        }
                        break;
                    }
                }
                return;

            case ReferencesSubtype::Style:
                nStart = 0;
                nEnd = nLen;
                break;

            default:
                O3TL_UNREACHABLE;
            }

            if( nStart != nEnd ) // a section?
            {
                if (pLayout->IsHideRedlines())
                {
                    if (m_nSubType == ReferencesSubtype::Outline
                        || (m_nSubType == ReferencesSubtype::SequenceField && RefFieldFormat::Content == GetFormat()))
                    {
                        rText = sw::GetExpandTextMerged(pLayout, *pTextNd, false, false,
                                                        ExpandMode(0));
                    }
                    else
                    {
                        rText = pTextNd->GetExpandText(pLayout, nStart, nEnd - nStart, false, false,
                                                       false, ExpandMode::HideDeletions);
                    }
                }
                else
                {
                    rText = pTextNd->GetExpandText(pLayout, nStart, nEnd - nStart, false, false,
                                                   false, ExpandMode::HideDeletions);
                    // show the referenced text without the deletions, but if the whole text was
                    // deleted, show the original text for the sake of the comfortable reviewing
                    // (with strikethrough in tooltip, see GetExpandedTextOfReferencedTextNode())
                    if (rText.isEmpty())
                        rText = pTextNd->GetExpandText(pLayout, nStart, nEnd - nStart, false, false,
                                                       false, ExpandMode(0));
                }
                FilterText(rText, GetLanguage(), m_sSetReferenceLanguage);
            }
        }
        break;

    case RefFieldFormat::Page:
    case RefFieldFormat::AsPageStyle:
        {
            SwTextFrame const* pFrame = static_cast<SwTextFrame*>(pTextNd->getLayoutFrame(pLayout, nullptr, nullptr));
            SwTextFrame const*const pSave = pFrame;
            if (pFrame)
            {
                TextFrameIndex const nNumStartIndex(pFrame->MapModelToView(pTextNd, nNumStart));
                while (pFrame && !pFrame->IsInside(nNumStartIndex))
                    pFrame = pFrame->GetFollow();
            }

            if( pFrame || nullptr != ( pFrame = pSave ))
            {
                sal_uInt16 nPageNo = pFrame->GetVirtPageNum();
                const SwPageFrame *pPage;
                if( RefFieldFormat::AsPageStyle == GetFormat() &&
                    nullptr != ( pPage = pFrame->FindPageFrame() ) &&
                    pPage->GetPageDesc() )
                {
                    rText = pPage->GetPageDesc()->GetNumType().GetNumStr(nPageNo);
                }
                else
                {
                    rText = OUString::number(nPageNo);
                }

                if (!m_sSetReferenceLanguage.isEmpty())
                    lcl_formatReferenceLanguage(rText, false, GetLanguage(), m_sSetReferenceLanguage);
            }
        }
        break;

    case RefFieldFormat::Chapter:
    {
        // a bit tricky: search any frame
        SwFrame const* const pFrame = pTextNd->getLayoutFrame(pLayout);
        if (pFrame)
        {
            SwChapterFieldType aFieldTyp;
            SwChapterField aField(&aFieldTyp, SwChapterFormat::Number);
            aField.SetLevel(MAXLEVEL - 1);
            aField.ChangeExpansion(*pFrame, pTextNd, true);

            rText = aField.GetNumber(pLayout);

            if (!m_sSetReferenceLanguage.isEmpty())
                lcl_formatReferenceLanguage(rText, false, GetLanguage(), m_sSetReferenceLanguage);
        }
        }
        break;

    case RefFieldFormat::UpDown:
        {
            // #i81002#
            // simplified: use parameter <pFieldTextAttr>
            if( !pFieldTextAttr || !pFieldTextAttr->GetpTextNode() )
                break;

            LocaleDataWrapper aLocaleData(( LanguageTag( GetLanguage() ) ));

            // first a "short" test - in case both are in the same node
            if( pFieldTextAttr->GetpTextNode() == pTextNd )
            {
                rText = nNumStart < pFieldTextAttr->GetStart()
                            ? aLocaleData.getAboveWord()
                            : aLocaleData.getBelowWord();
                break;
            }

            rText = ::IsFrameBehind( *pFieldTextAttr->GetpTextNode(), pFieldTextAttr->GetStart(),
                                    *pTextNd, nNumStart )
                        ? aLocaleData.getAboveWord()
                        : aLocaleData.getBelowWord();

            if (!m_sSetReferenceLanguage.isEmpty())
                    lcl_formatReferenceLanguage(rText, false, GetLanguage(), m_sSetReferenceLanguage);
        }
        break;
    // #i81002#
    case RefFieldFormat::Number:
    case RefFieldFormat::NumberNoContext:
    case RefFieldFormat::NumberFullContext:
        {
            if ( pFieldTextAttr && pFieldTextAttr->GetpTextNode() )
            {
                auto result =
                    MakeRefNumStr(pLayout, pFieldTextAttr->GetTextNode(), *pTextNd, m_nSubType, GetFormat(), GetFlags());
                rText = result.first;
                // for differentiation of Roman numbers and letters in Hungarian article handling
                bool bClosingParenthesis = result.second;
                if (!m_sSetReferenceLanguage.isEmpty())
                {
                    lcl_formatReferenceLanguage(rText, bClosingParenthesis, GetLanguage(), m_sSetReferenceLanguage);
                }
            }
        }

        break;

    default:
        OSL_FAIL("<SwGetRefField::UpdateField(..)> - unknown format type");
    }
}


// #i81002#
static std::pair<OUString, bool> MakeRefNumStr(
        SwRootFrame const*const pLayout,
        const SwTextNode& i_rTextNodeOfField,
        const SwTextNode& i_rTextNodeOfReferencedItem,
        const ReferencesSubtype nSubType,
        const RefFieldFormat nRefNumFormat,
        const sal_uInt16 nFlags)
{
    bool bHideNonNumerical = (nSubType == ReferencesSubtype::Style) && ((nFlags & REFFLDFLAG_STYLE_HIDE_NON_NUMERICAL) == REFFLDFLAG_STYLE_HIDE_NON_NUMERICAL);
    SwTextNode const& rTextNodeOfField(pLayout
            ?   *sw::GetParaPropsNode(*pLayout, i_rTextNodeOfField)
            :   i_rTextNodeOfField);
    SwTextNode const& rTextNodeOfReferencedItem(pLayout
            ?   *sw::GetParaPropsNode(*pLayout, i_rTextNodeOfReferencedItem)
            :   i_rTextNodeOfReferencedItem);
    if ( rTextNodeOfReferencedItem.HasNumber(pLayout) &&
         rTextNodeOfReferencedItem.IsCountedInList() )
    {
        OSL_ENSURE( rTextNodeOfReferencedItem.GetNum(pLayout),
                "<SwGetRefField::MakeRefNumStr(..)> - referenced paragraph has number, but no <SwNodeNum> instance!" );

        // Determine, up to which level the superior list labels have to be
        // included - default is to include all superior list labels.
        int nRestrictInclToThisLevel( 0 );
        // Determine for format REF_NUMBER the level, up to which the superior
        // list labels have to be restricted, if the text node of the reference
        // field and the text node of the referenced item are in the same
        // document context.
        if ( nRefNumFormat == RefFieldFormat::Number &&
             rTextNodeOfField.FindFlyStartNode()
                            == rTextNodeOfReferencedItem.FindFlyStartNode() &&
             rTextNodeOfField.FindFootnoteStartNode()
                            == rTextNodeOfReferencedItem.FindFootnoteStartNode() &&
             rTextNodeOfField.FindHeaderStartNode()
                            == rTextNodeOfReferencedItem.FindHeaderStartNode() &&
             rTextNodeOfField.FindFooterStartNode()
                            == rTextNodeOfReferencedItem.FindFooterStartNode() )
        {
            const SwNodeNum* pNodeNumForTextNodeOfField( nullptr );
            if ( rTextNodeOfField.HasNumber(pLayout) &&
                 rTextNodeOfField.GetNumRule() == rTextNodeOfReferencedItem.GetNumRule() )
            {
                pNodeNumForTextNodeOfField = rTextNodeOfField.GetNum(pLayout);
            }
            else
            {
                pNodeNumForTextNodeOfField =
                    rTextNodeOfReferencedItem.GetNum(pLayout)->GetPrecedingNodeNumOf(rTextNodeOfField);
            }
            if ( pNodeNumForTextNodeOfField )
            {
                const SwNumberTree::tNumberVector rFieldNumVec =
                    pNodeNumForTextNodeOfField->GetNumberVector();
                const SwNumberTree::tNumberVector rRefItemNumVec =
                    rTextNodeOfReferencedItem.GetNum()->GetNumberVector();
                std::size_t nLevel( 0 );
                while ( nLevel < rFieldNumVec.size() && nLevel < rRefItemNumVec.size() )
                {
                    if ( rRefItemNumVec[nLevel] == rFieldNumVec[nLevel] )
                    {
                        nRestrictInclToThisLevel = nLevel + 1;
                    }
                    else
                    {
                        break;
                    }
                    ++nLevel;
                }
            }
        }

        // Determine, if superior list labels have to be included
        const bool bInclSuperiorNumLabels(
            ( nRestrictInclToThisLevel < rTextNodeOfReferencedItem.GetActualListLevel() &&
              ( nRefNumFormat == RefFieldFormat::Number || nRefNumFormat == RefFieldFormat::NumberFullContext ) ) );

        OSL_ENSURE( rTextNodeOfReferencedItem.GetNumRule(),
                "<SwGetRefField::MakeRefNumStr(..)> - referenced numbered paragraph has no numbering rule set!" );
        return std::make_pair(
                rTextNodeOfReferencedItem.GetNumRule()->MakeRefNumString(
                    *(rTextNodeOfReferencedItem.GetNum(pLayout)),
                    bInclSuperiorNumLabels,
                    nRestrictInclToThisLevel,
                    bHideNonNumerical ),
                rTextNodeOfReferencedItem.GetNumRule()->MakeNumString(
                    *(rTextNodeOfReferencedItem.GetNum(pLayout)),
                    true).endsWith(")") );
    }

    return std::make_pair(OUString(), false);
}

std::unique_ptr<SwField> SwGetRefField::Copy() const
{
    std::unique_ptr<SwGetRefField> pField( new SwGetRefField( static_cast<SwGetRefFieldType*>(GetTyp()),
                                                m_sSetRefName, m_sSetReferenceLanguage, m_nSubType,
                                                m_nSeqNo, m_nFlags, GetFormat() ) );
    pField->m_sText = m_sText;
    pField->m_sTextRLHidden = m_sTextRLHidden;
    return std::unique_ptr<SwField>(pField.release());
}

/// get reference name
OUString SwGetRefField::GetPar1() const
{
    return m_sSetRefName.toString();
}

/// set reference name
void SwGetRefField::SetPar1( const OUString& rName )
{
    m_sSetRefName = SwMarkName(rName);
}

OUString SwGetRefField::GetPar2() const
{
    return ExpandImpl(nullptr);
}

bool SwGetRefField::QueryValue( uno::Any& rAny, sal_uInt16 nWhichId ) const
{
    switch( nWhichId )
    {
    case FIELD_PROP_USHORT1:
        {
            sal_Int16 nPart = 0;
            switch(GetFormat())
            {
            case RefFieldFormat::Page       : nPart = ReferenceFieldPart::PAGE                ; break;
            case RefFieldFormat::Chapter    : nPart = ReferenceFieldPart::CHAPTER             ; break;
            case RefFieldFormat::Content    : nPart = ReferenceFieldPart::TEXT                ; break;
            case RefFieldFormat::UpDown     : nPart = ReferenceFieldPart::UP_DOWN             ; break;
            case RefFieldFormat::AsPageStyle: nPart = ReferenceFieldPart::PAGE_DESC           ; break;
            case RefFieldFormat::CategoryAndNumber : nPart = ReferenceFieldPart::CATEGORY_AND_NUMBER ; break;
            case RefFieldFormat::CaptionText: nPart = ReferenceFieldPart::ONLY_CAPTION        ; break;
            case RefFieldFormat::Numbering  : nPart = ReferenceFieldPart::ONLY_SEQUENCE_NUMBER; break;
            // #i81002#
            case RefFieldFormat::Number:              nPart = ReferenceFieldPart::NUMBER;              break;
            case RefFieldFormat::NumberNoContext:   nPart = ReferenceFieldPart::NUMBER_NO_CONTEXT;   break;
            case RefFieldFormat::NumberFullContext: nPart = ReferenceFieldPart::NUMBER_FULL_CONTEXT; break;
            }
            rAny <<= nPart;
        }
        break;
    case FIELD_PROP_USHORT2:
        {
            sal_Int16 nSource = 0;
            switch(m_nSubType)
            {
            case  ReferencesSubtype::SetRefAttr : nSource = ReferenceFieldSource::REFERENCE_MARK; break;
            case  ReferencesSubtype::SequenceField: nSource = ReferenceFieldSource::SEQUENCE_FIELD; break;
            case  ReferencesSubtype::Bookmark   : nSource = ReferenceFieldSource::BOOKMARK; break;
            case  ReferencesSubtype::Outline    : OSL_FAIL("not implemented"); break;
            case  ReferencesSubtype::Footnote   : nSource = ReferenceFieldSource::FOOTNOTE; break;
            case  ReferencesSubtype::Endnote    : nSource = ReferenceFieldSource::ENDNOTE; break;
            case  ReferencesSubtype::Style      : nSource = ReferenceFieldSource::STYLE; break;
            }
            rAny <<= nSource;
        }
        break;
    case FIELD_PROP_USHORT3:
        rAny <<= m_nFlags;
        break;
    case FIELD_PROP_PAR1:
    {
        OUString sTmp(GetPar1());
        if(ReferencesSubtype::SequenceField == m_nSubType)
        {
            sal_uInt16 nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( UIName(sTmp), SwGetPoolIdFromName::TxtColl );
            switch( nPoolId )
            {
                case RES_POOLCOLL_LABEL_ABB:
                case RES_POOLCOLL_LABEL_TABLE:
                case RES_POOLCOLL_LABEL_FRAME:
                case RES_POOLCOLL_LABEL_DRAWING:
                case RES_POOLCOLL_LABEL_FIGURE:
                {
                    ProgName sTmp2(sTmp);
                    SwStyleNameMapper::FillProgName(nPoolId, sTmp2) ;
                    sTmp = sTmp2.toString();
                }
                break;
            }
        }
        else if (ReferencesSubtype::Style == m_nSubType)
        {
            ProgName name;
            SwStyleNameMapper::FillProgName(UIName{sTmp}, name, SwGetPoolIdFromName::TxtColl);
            if (name == sTmp)
            {
                SwStyleNameMapper::FillProgName(UIName{sTmp}, name, SwGetPoolIdFromName::ChrFmt);
            }
            sTmp = name.toString();

        }
        rAny <<= sTmp;
    }
    break;
    case FIELD_PROP_PAR3:
        rAny <<= ExpandImpl(nullptr);
        break;
    case FIELD_PROP_PAR4:
        rAny <<= m_sSetReferenceLanguage;
        break;
    case FIELD_PROP_SHORT1:
        rAny <<= static_cast<sal_Int16>(m_nSeqNo);
        break;
    default:
        assert(false);
    }
    return true;
}

bool SwGetRefField::PutValue( const uno::Any& rAny, sal_uInt16 nWhichId )
{
    switch( nWhichId )
    {
    case FIELD_PROP_USHORT1:
        {
            sal_Int16 nPart = 0;
            rAny >>= nPart;
            switch(nPart)
            {
            case ReferenceFieldPart::PAGE:                  m_nFormat = RefFieldFormat::Page; break;
            case ReferenceFieldPart::CHAPTER:               m_nFormat = RefFieldFormat::Chapter; break;
            case ReferenceFieldPart::TEXT:                  m_nFormat = RefFieldFormat::Content; break;
            case ReferenceFieldPart::UP_DOWN:               m_nFormat = RefFieldFormat::UpDown; break;
            case ReferenceFieldPart::PAGE_DESC:             m_nFormat = RefFieldFormat::AsPageStyle; break;
            case ReferenceFieldPart::CATEGORY_AND_NUMBER:   m_nFormat = RefFieldFormat::CategoryAndNumber; break;
            case ReferenceFieldPart::ONLY_CAPTION:          m_nFormat = RefFieldFormat::CaptionText; break;
            case ReferenceFieldPart::ONLY_SEQUENCE_NUMBER : m_nFormat = RefFieldFormat::Numbering; break;
            // #i81002#
            case ReferenceFieldPart::NUMBER:              m_nFormat = RefFieldFormat::Number;              break;
            case ReferenceFieldPart::NUMBER_NO_CONTEXT:   m_nFormat = RefFieldFormat::NumberNoContext;   break;
            case ReferenceFieldPart::NUMBER_FULL_CONTEXT: m_nFormat = RefFieldFormat::NumberFullContext; break;
            default: return false;
            }
        }
        break;
    case FIELD_PROP_USHORT2:
        {
            sal_Int16 nSource = 0;
            rAny >>= nSource;
            switch(nSource)
            {
            case ReferenceFieldSource::REFERENCE_MARK : m_nSubType = ReferencesSubtype::SetRefAttr ; break;
            case ReferenceFieldSource::SEQUENCE_FIELD :
            {
                if(ReferencesSubtype::SequenceField == m_nSubType)
                    break;
                m_nSubType = ReferencesSubtype::SequenceField;
                ConvertProgrammaticToUIName();
            }
            break;
            case ReferenceFieldSource::BOOKMARK       : m_nSubType = ReferencesSubtype::Bookmark   ; break;
            case ReferenceFieldSource::FOOTNOTE       : m_nSubType = ReferencesSubtype::Footnote   ; break;
            case ReferenceFieldSource::ENDNOTE        : m_nSubType = ReferencesSubtype::Endnote    ; break;
            case ReferenceFieldSource::STYLE          :
                if (ReferencesSubtype::Style != m_nSubType)
                {
                    m_nSubType = ReferencesSubtype::Style;
                    ConvertProgrammaticToUIName();
                }
                break;
            }
        }
        break;
    case FIELD_PROP_PAR1:
    {
        OUString sTmpStr;
        rAny >>= sTmpStr;
        SetPar1(sTmpStr);
        ConvertProgrammaticToUIName();
    }
    break;
    case FIELD_PROP_PAR3:
        {
            OUString sTmpStr;
            rAny >>= sTmpStr;
            SetExpand( sTmpStr );
        }
        break;
    case FIELD_PROP_PAR4:
        rAny >>= m_sSetReferenceLanguage;
        break;
    case FIELD_PROP_USHORT3:
        {
            sal_uInt16 nSetFlags = 0;
            rAny >>= nSetFlags;
            m_nFlags = nSetFlags;
        }
        break;
    case FIELD_PROP_SHORT1:
        {
            sal_Int16 nSetSeq = 0;
            rAny >>= nSetSeq;
            if(nSetSeq >= 0)
                m_nSeqNo = nSetSeq;
        }
        break;
    default:
        assert(false);
    }
    return true;
}

void SwGetRefField::ConvertProgrammaticToUIName()
{
    if (GetTyp() && ReferencesSubtype::Style == m_nSubType)
    {
        // this is super ugly, but there isn't a sensible way to check in xmloff
        SwDoc & rDoc{static_cast<SwGetRefFieldType*>(GetTyp())->GetDoc()};
        if (rDoc.IsInXMLImport242())
        {
            SAL_INFO("sw.xml", "Potentially accepting erroneously produced UIName for style-ref field");
            return;
        }
        ProgName const par1{GetPar1()};
        UIName name;
        SwStyleNameMapper::FillUIName(par1, name, SwGetPoolIdFromName::TxtColl);
        if (name.toString() == par1.toString())
        {
            SwStyleNameMapper::FillUIName(par1, name, SwGetPoolIdFromName::ChrFmt);
        }
        if (name.toString() != par1.toString())
        {
            SetPar1(name.toString());
        }
        return;
    }

    if(!(GetTyp() && ReferencesSubtype::SequenceField == m_nSubType))
        return;

    SwDoc& rDoc = static_cast<SwGetRefFieldType*>(GetTyp())->GetDoc();
    const OUString rPar1 = GetPar1();
    // don't convert when the name points to an existing field type
    if (rDoc.getIDocumentFieldsAccess().GetFieldType(SwFieldIds::SetExp, rPar1, false))
        return;

    sal_uInt16 nPoolId = SwStyleNameMapper::GetPoolIdFromProgName( ProgName(rPar1), SwGetPoolIdFromName::TxtColl );
    TranslateId pResId;
    switch( nPoolId )
    {
        case RES_POOLCOLL_LABEL_ABB:
            pResId = STR_POOLCOLL_LABEL_ABB;
        break;
        case RES_POOLCOLL_LABEL_TABLE:
            pResId = STR_POOLCOLL_LABEL_TABLE;
        break;
        case RES_POOLCOLL_LABEL_FRAME:
            pResId = STR_POOLCOLL_LABEL_FRAME;
        break;
        case RES_POOLCOLL_LABEL_DRAWING:
            pResId = STR_POOLCOLL_LABEL_DRAWING;
        break;
        case RES_POOLCOLL_LABEL_FIGURE:
            pResId = STR_POOLCOLL_LABEL_FIGURE;
        break;
    }
    if (pResId)
        SetPar1(SwResId(pResId));
}

SwGetRefFieldType::SwGetRefFieldType( SwDoc& rDc )
    : SwFieldType( SwFieldIds::GetRef ), m_rDoc( rDc )
{}

std::unique_ptr<SwFieldType> SwGetRefFieldType::Copy() const
{
    return std::make_unique<SwGetRefFieldType>( m_rDoc );
}

void SwGetRefFieldType::UpdateGetReferences()
{
    std::vector<SwFormatField*> vFields;
    GatherFields(vFields, false);
    for(auto pFormatField: vFields)
    {
        // update only the GetRef fields
        //JP 3.4.2001: Task 71231 - we need the correct language
        SwGetRefField* pGRef = static_cast<SwGetRefField*>(pFormatField->GetField());
        const SwTextField* pTField;
        if(!pGRef->GetLanguage() &&
            nullptr != (pTField = pFormatField->GetTextField()) &&
            pTField->GetpTextNode())
        {
            pGRef->SetLanguage(pTField->GetpTextNode()->GetLang(pTField->GetStart()));
        }

        // #i81002#
        pGRef->UpdateField(pFormatField->GetTextField(), nullptr);
    }
    CallSwClientNotify(sw::LegacyModifyHint(nullptr, nullptr));
}

void SwGetRefFieldType::UpdateStyleReferences()
{
    std::vector<SwFormatField*> vFields;
    GatherFields(vFields, false);
    bool bModified = false;
    for(auto pFormatField: vFields)
    {
        // update only the GetRef fields which are also STYLEREF fields
        SwGetRefField* pGRef = static_cast<SwGetRefField*>(pFormatField->GetField());

        if (pGRef->GetSubType() != ReferencesSubtype::Style) continue;

        const SwTextField* pTField;
        if(!pGRef->GetLanguage() &&
            nullptr != (pTField = pFormatField->GetTextField()) &&
            pTField->GetpTextNode())
        {
            pGRef->SetLanguage(pTField->GetpTextNode()->GetLang(pTField->GetStart()));
        }

        pGRef->UpdateField(pFormatField->GetTextField(), nullptr);
        bModified = true;
    }
    if (bModified)
        CallSwClientNotify(sw::LegacyModifyHint(nullptr, nullptr));
}

void SwGetRefFieldType::SwClientNotify(const SwModify&, const SfxHint& rHint)
{
    if (rHint.GetId() == SfxHintId::SwFormatChange || rHint.GetId() == SfxHintId::SwObjectDying)
    {
        // forward to text fields, they "expand" the text
        CallSwClientNotify(rHint);
        return;
    }
    if (rHint.GetId() == SfxHintId::SwAttrSetChange)
    {
        auto pChangeHint = static_cast<const sw::AttrSetChangeHint*>(&rHint);
        if(!pChangeHint->m_pNew && !pChangeHint->m_pOld)
            // update to all GetReference fields
            // hopefully, this codepath is soon dead code, and
            // UpdateGetReferences gets only called directly
            UpdateGetReferences();
        else
            // forward to text fields, they "expand" the text
            CallSwClientNotify(rHint);
        return;
    }
    if (rHint.GetId() == SfxHintId::SwUpdateAttr)
    {
        auto pChangeHint = static_cast<const sw::UpdateAttrHint*>(&rHint);
        if(!pChangeHint->m_pNew && !pChangeHint->m_pOld)
            // update to all GetReference fields
            // hopefully, this codepath is soon dead code, and
            // UpdateGetReferences gets only called directly
            UpdateGetReferences();
        else
            // forward to text fields, they "expand" the text
            CallSwClientNotify(rHint);
        return;
    }
    if (rHint.GetId() != SfxHintId::SwLegacyModify)
        return;
    auto pLegacy = static_cast<const sw::LegacyModifyHint*>(&rHint);
    if(!pLegacy->m_pNew && !pLegacy->m_pOld)
        // update to all GetReference fields
        // hopefully, this codepath is soon dead code, and
        // UpdateGetReferences gets only called directly
        UpdateGetReferences();
    else
        // forward to text fields, they "expand" the text
        CallSwClientNotify(rHint);
}

namespace sw {

bool IsMarkHintHidden(SwRootFrame const& rLayout,
        SwTextNode const& rNode, SwTextAttrEnd const& rHint)
{
    if (!rLayout.HasMergedParas())
    {
        return false;
    }
    SwTextFrame const*const pFrame(static_cast<SwTextFrame const*>(
        rNode.getLayoutFrame(&rLayout)));
    if (!pFrame)
    {
        return true;
    }
    sal_Int32 const*const pEnd(rHint.GetEnd());
    if (pEnd)
    {
        return pFrame->MapModelToView(&rNode, rHint.GetStart())
            == pFrame->MapModelToView(&rNode, *pEnd);
    }
    else
    {
        assert(rHint.HasDummyChar());
        return pFrame->MapModelToView(&rNode, rHint.GetStart())
            == pFrame->MapModelToView(&rNode, rHint.GetStart() + 1);
    }
}

} // namespace sw

namespace
{
    enum StyleRefElementType
    {
        Default,
        Reference, /* e.g. footnotes, endnotes */
        Marginal, /* headers, footers */
    };

    SwTextNode* SearchForStyleAnchor(const SwTextNode* pSelf, SwNode* pCurrent,
                                    std::u16string_view rStyleName,
                                    sal_Int32 *const pStart, sal_Int32 *const pEnd,
                                    bool bCaseSensitive = true)
    {
        if (pCurrent == pSelf)
            return nullptr;

        SwTextNode* pTextNode = pCurrent->GetTextNode();
        if (!pTextNode)
            return nullptr;

        UIName const & rFormatName = pTextNode->GetFormatColl()->GetName();
        if (bCaseSensitive
            ? rFormatName == rStyleName
            : rFormatName.toString().equalsIgnoreAsciiCase(rStyleName))
        {
            *pStart = 0;
            if (pEnd)
            {
                *pEnd = pTextNode->GetText().getLength();
            }
            return pTextNode;
        }

        if (auto const pHints = pTextNode->GetpSwpHints())
        {
            for (size_t i = 0, nCnt = pHints->Count(); i < nCnt; ++i)
            {
                auto const*const pHint(pHints->Get(i));
                if (pHint->Which() == RES_TXTATR_CHARFMT)
                {
                    if (bCaseSensitive
                        ? pHint->GetCharFormat().GetCharFormat()->HasName(rStyleName)
                        : pHint->GetCharFormat().GetCharFormat()->GetName().toString().equalsIgnoreAsciiCase(rStyleName))
                    {
                        *pStart = pHint->GetStart();
                        if (pEnd)
                        {
                            *pEnd = *pHint->End();
                        }
                        return pTextNode;
                    }
                }
            }
        }

        return nullptr;
    }
    /// Picks the first text node with a matching style from the specified node range
    SwTextNode* SearchForStyleAnchor(const SwTextNode* pSelf, const SwNodes& rNodes, SwNodeOffset nNodeStart, SwNodeOffset nNodeEnd, bool bSearchBackward,
                                    std::u16string_view rStyleName,
                                    sal_Int32 *const pStart, sal_Int32 *const pEnd,
                                    bool bCaseSensitive = true)
    {
        if (!bSearchBackward)
        {
            for (SwNodeOffset nCurrent = nNodeStart; nCurrent <= nNodeEnd; ++nCurrent)
            {
                SwNode* pCurrent = rNodes[nCurrent];
                SwTextNode* pFound = SearchForStyleAnchor(pSelf, pCurrent, rStyleName, pStart, pEnd, bCaseSensitive);
                if (pFound)
                    return pFound;
            }
        }
        else
        {
            for (SwNodeOffset nCurrent = nNodeEnd; nCurrent >= nNodeStart; --nCurrent)
            {
                SwNode* pCurrent = rNodes[nCurrent];
                SwTextNode* pFound = SearchForStyleAnchor(pSelf, pCurrent, rStyleName, pStart, pEnd, bCaseSensitive);
                if (pFound)
                    return pFound;
            }
        }
        return nullptr;
    }
}

SwTextNode* SwGetRefFieldType::FindAnchor(SwDoc* pDoc, const SwMarkName& rRefMark,
                                          ReferencesSubtype nSubType, sal_uInt16 nSeqNo, sal_uInt16 nFlags,
                                          sal_Int32* pStart, sal_Int32* pEnd, SwRootFrame const* const pLayout,
                                          const SwTextNode* pSelf, SwFrame* pContentFrame)
{
    assert( pStart && "Why did no one check the StartPos?" );

    IDocumentRedlineAccess & rIDRA(pDoc->getIDocumentRedlineAccess());
    SwTextNode* pTextNd = nullptr;
    switch( nSubType )
    {
    case ReferencesSubtype::SetRefAttr:
        {
            const SwFormatRefMark *pRef = pDoc->GetRefMark( rRefMark );
            SwTextRefMark const*const pRefMark(pRef ? pRef->GetTextRefMark() : nullptr);
            if (pRefMark && (!pLayout || !sw::IsMarkHintHidden(*pLayout,
                                           pRefMark->GetTextNode(), *pRefMark)))
            {
                pTextNd = const_cast<SwTextNode*>(&pRef->GetTextRefMark()->GetTextNode());
                *pStart = pRef->GetTextRefMark()->GetStart();
                if( pEnd )
                    *pEnd = pRef->GetTextRefMark()->GetAnyEnd();
            }
        }
        break;

    case ReferencesSubtype::SequenceField:
        {
            SwFieldType* pFieldType = pDoc->getIDocumentFieldsAccess().GetFieldType( SwFieldIds::SetExp, rRefMark, false );
            if( pFieldType && pFieldType->HasWriterListeners() &&
                SwGetSetExpType::Sequence & static_cast<SwSetExpFieldType*>(pFieldType)->GetType() )
            {
                std::vector<SwFormatField*> vFields;
                pFieldType->GatherFields(vFields, false);
                for(auto pFormatField: vFields)
                {
                    SwTextField *const pTextField(pFormatField->GetTextField());
                    if (pTextField && nSeqNo ==
                        static_cast<SwSetExpField*>(pFormatField->GetField())->GetSeqNumber()
                        && (!pLayout || !pLayout->IsHideRedlines()
                            || !sw::IsFieldDeletedInModel(rIDRA, *pTextField)))
                    {
                        pTextNd = pTextField->GetpTextNode();
                        *pStart = pTextField->GetStart();
                        if( pEnd )
                            *pEnd = (*pStart) + 1;
                        break;
                    }
                }
            }
        }
        break;

    case ReferencesSubtype::Bookmark:
        {
            auto ppMark = pDoc->getIDocumentMarkAccess()->findMark(rRefMark);
            if (ppMark != pDoc->getIDocumentMarkAccess()->getAllMarksEnd()
                && (!pLayout || !pLayout->IsHideRedlines()
                    || !sw::IsMarkHidden(*pLayout, **ppMark)))
            {
                const ::sw::mark::MarkBase* pBkmk = *ppMark;
                const SwPosition* pPos = &pBkmk->GetMarkStart();

                pTextNd = pPos->GetNode().GetTextNode();
                *pStart = pPos->GetContentIndex();
                if(pEnd)
                {
                    if(!pBkmk->IsExpanded())
                    {
                        *pEnd = *pStart;
                        // #i81002#
                        if(dynamic_cast< ::sw::mark::CrossRefBookmark const *>(pBkmk))
                        {
                            assert(pTextNd &&
                                    "<SwGetRefFieldType::FindAnchor(..)> - node marked by cross-reference bookmark isn't a text node --> crash");
                            *pEnd = pTextNd->Len();
                        }
                    }
                    else if(pBkmk->GetOtherMarkPos().GetNode() == pBkmk->GetMarkPos().GetNode())
                        *pEnd = pBkmk->GetMarkEnd().GetContentIndex();
                    else
                        *pEnd = -1;
                }
            }
        }
        break;

    case ReferencesSubtype::Outline:
        break;

    case ReferencesSubtype::Footnote:
    case ReferencesSubtype::Endnote:
        {
            for( auto pFootnoteIdx : pDoc->GetFootnoteIdxs() )
                if( nSeqNo == pFootnoteIdx->GetSeqRefNo() )
                {
                    if (pLayout && pLayout->IsHideRedlines()
                        && sw::IsFootnoteDeleted(rIDRA, *pFootnoteIdx))
                    {
                        return nullptr;
                    }
                    // otherwise: the position at the start of the footnote
                    // will be mapped to something visible at least...
                    const SwNodeIndex* pIdx = pFootnoteIdx->GetStartNode();
                    if( pIdx )
                    {
                        SwNodeIndex aIdx( *pIdx, 1 );
                        pTextNd = aIdx.GetNode().GetTextNode();
                        if( nullptr == pTextNd )
                            pTextNd = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));
                    }
                    *pStart = 0;
                    if( pEnd )
                        *pEnd = 0;
                    break;
                }
        }
        break;
        case ReferencesSubtype::Style:
            pTextNd = FindAnchorRefStyle(pDoc, rRefMark, nFlags,
                                          pStart, pEnd, pLayout, pSelf, pContentFrame);
            break;
    }

    return pTextNd;
}

SwTextNode* SwGetRefFieldType::FindAnchorRefStyle(SwDoc* pDoc, const SwMarkName& rRefMark,
                                          sal_uInt16 nFlags,
                                          sal_Int32* pStart, sal_Int32* pEnd, SwRootFrame const* const pLayout,
                                          const SwTextNode* pSelf, SwFrame* pContentFrame)
{
    if (!pSelf)
        return nullptr;

    SwTextNode* pTextNd = nullptr;

    StyleRefElementType elementType = StyleRefElementType::Default;
    const SwTextNode* pReference = nullptr;
    IDocumentRedlineAccess & rIDRA(pDoc->getIDocumentRedlineAccess());

    /* Check if we're a footnote/endnote */
    for (SwTextFootnote* pFootnoteIdx : pDoc->GetFootnoteIdxs())
    {
        if (pLayout && pLayout->IsHideRedlines()
            && sw::IsFootnoteDeleted(rIDRA, *pFootnoteIdx))
        {
            continue;
        }
        const SwNodeIndex* pIdx = pFootnoteIdx->GetStartNode();
        if (pIdx)
        {
            SwNodeIndex aIdx(*pIdx, 1);
            SwTextNode* pFootnoteNode = aIdx.GetNode().GetTextNode();
            if (nullptr == pFootnoteNode)
                pFootnoteNode = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));

            if (*pSelf == *pFootnoteNode)
            {
                elementType = StyleRefElementType::Reference;
                pReference = &pFootnoteIdx->GetTextNode();
            }
        }
    }

    if (pDoc->IsInHeaderFooter(*pSelf))
    {
        elementType = StyleRefElementType::Marginal;
    }

    if (pReference == nullptr)
    {
        pReference = pSelf;
    }

    // undocumented Word feature: 1 = "Heading 1" etc.
    const OUString& sRefMarkStr = rRefMark.toString();
    OUString const styleName(
        (sRefMarkStr.getLength() == 1 && '1' <= sRefMarkStr[0] && sRefMarkStr[0] <= '9')
        ? SwStyleNameMapper::GetProgName(RES_POOLCOLL_HEADLINE1 + sRefMarkStr[0] - '1', UIName(sRefMarkStr)).toString()
        : sRefMarkStr);

    switch (elementType)
    {
        case Marginal:
            pTextNd = FindAnchorRefStyleMarginal(pDoc, nFlags,
                                          pStart, pEnd, pSelf, pContentFrame, pReference, styleName);
            break;
        case Reference:
        case Default:
            pTextNd = FindAnchorRefStyleOther(pDoc,
                                          pStart, pEnd, pSelf, pReference, styleName);
            break;
        default:
            OSL_FAIL("<SwGetRefFieldType::FindAnchorRefStyle(..)> - unknown getref element type");
    }
    return pTextNd;
}

SwTextNode* SwGetRefFieldType::FindAnchorRefStyleMarginal(SwDoc* pDoc,
                                          sal_uInt16 nFlags,
                                          sal_Int32* pStart, sal_Int32* pEnd,
                                          const SwTextNode* pSelf, SwFrame* pContentFrame,
                                          const SwTextNode* pReference, std::u16string_view styleName)
{
    // For marginals, styleref tries to act on the current page first
    // 1. Get the page we're on, search it from top to bottom

    SwTextNode* pTextNd = nullptr;

    bool bFlagFromBottom = (nFlags & REFFLDFLAG_STYLE_FROM_BOTTOM) == REFFLDFLAG_STYLE_FROM_BOTTOM;

    Point aPt;
    std::pair<Point, bool> const tmp(aPt, false);

    if (!pContentFrame) SAL_WARN("xmloff.text", "<SwGetRefFieldType::FindAnchorRefStyleMarginal(..)>: Missing content frame for marginal styleref");
    const SwPageFrame* pPageFrame = nullptr;

    if (pContentFrame)
        pPageFrame = pContentFrame->FindPageFrame();

    const SwNode* pPageStart(nullptr);
    const SwNode* pPageEnd(nullptr);

    if (pPageFrame)
    {
        const SwContentFrame* pPageStartFrame = pPageFrame->FindFirstBodyContent();
        const SwContentFrame* pPageEndFrame = pPageFrame->FindLastBodyContent();

        if (pPageStartFrame)
        {
            if (pPageStartFrame->IsTextFrame())
            {
                pPageStart = static_cast<const SwTextFrame*>(pPageStartFrame)
                                ->GetTextNodeFirst();
            }
            else
            {
                pPageStart
                    = static_cast<const SwNoTextFrame*>(pPageStartFrame)->GetNode();
            }
        }

        if (pPageEndFrame)
        {
            if (pPageEndFrame->IsTextFrame())
            {
                pPageEnd = static_cast<const SwTextFrame*>(pPageEndFrame)
                            ->GetTextNodeFirst();
            }
            else
            {
                pPageEnd = static_cast<const SwNoTextFrame*>(pPageEndFrame)->GetNode();
            }
        }
    }

    if (!pPageStart || !pPageEnd)
    {
        pPageStart = pReference;
        pPageEnd = pReference;
    }

    SwNodeOffset nPageStart = pPageStart->GetIndex();
    SwNodeOffset nPageEnd = pPageEnd->GetIndex();
    const SwNodes& nodes = pDoc->GetNodes();

    pTextNd = SearchForStyleAnchor(pSelf, nodes, nPageStart, nPageEnd, bFlagFromBottom, styleName, pStart, pEnd);
    if (pTextNd)
        return pTextNd;

    // 2. Search up from the top of the page
    pTextNd = SearchForStyleAnchor(pSelf, nodes, SwNodeOffset(0), nPageStart - 1, /*bBackwards*/true, styleName, pStart, pEnd);
    if (pTextNd)
        return pTextNd;

    // 3. Search down from the bottom of the page
    pTextNd = SearchForStyleAnchor(pSelf, nodes, nPageEnd + 1, nodes.Count() - 1, /*bBackwards*/false, styleName, pStart, pEnd);
    if (pTextNd)
        return pTextNd;

    // Word has case insensitive styles. LO has case sensitive styles. If we didn't find
    // it yet, maybe we could with a case insensitive search. Let's do that

    pTextNd = SearchForStyleAnchor(pSelf, nodes, nPageStart, nPageEnd, bFlagFromBottom, styleName, pStart, pEnd,
                                   false /* bCaseSensitive */);
    if (pTextNd)
        return pTextNd;

    pTextNd = SearchForStyleAnchor(pSelf, nodes, SwNodeOffset(0), nPageStart - 1, /*bBackwards*/true, styleName, pStart, pEnd,
                                   false /* bCaseSensitive */);
    if (pTextNd)
        return pTextNd;

    pTextNd = SearchForStyleAnchor(pSelf, nodes, nPageEnd + 1, nodes.Count() - 1, /*bBackwards*/false, styleName, pStart, pEnd,
                                   false /* bCaseSensitive */);
    return pTextNd;
}

SwTextNode* SwGetRefFieldType::FindAnchorRefStyleOther(SwDoc* pDoc,
                                          sal_Int32* pStart, sal_Int32* pEnd,
                                          const SwTextNode* pSelf,
                                          const SwTextNode* pReference, std::u16string_view styleName)
{
    // Normally, styleref does searches around the field position
    // For references, styleref acts from the position of the reference not the field
    // Happily, the previous code saves either one into pReference, so the following is generic for both

    const SwNodes& nodes = pDoc->GetNodes();

    if (&nodes != &pReference->GetNodes())
        return nullptr;

    // It is possible to end up here, with a pReference pointer which points to a node which has already been
    // removed from the nodes array, which means that calling GetIndex() returns an incorrect index.
    SwNodeOffset nReference;
    if (!pReference->IsDisconnected())
        nReference = pReference->GetIndex();
    else
        nReference = nodes.Count() - 1;

    SwTextNode* pTextNd = nullptr;

    // 1. Search up until we hit the top of the document

    pTextNd = SearchForStyleAnchor(pSelf, nodes, SwNodeOffset(0), nReference, /*bBackwards*/true, styleName, pStart, pEnd);
    if (pTextNd)
        return pTextNd;

    // 2. Search down until we hit the bottom of the document

    pTextNd = SearchForStyleAnchor(pSelf, nodes, nReference + 1, nodes.Count() - 1, /*bBackwards*/false, styleName, pStart, pEnd);
    if (pTextNd)
        return pTextNd;

    // Again, we need to remember that Word styles are not case sensitive

    pTextNd = SearchForStyleAnchor(pSelf, nodes, SwNodeOffset(0), nReference, /*bBackwards*/true, styleName, pStart, pEnd,
                                   false /* bCaseSensitive */);
    if (pTextNd)
        return pTextNd;

    pTextNd = SearchForStyleAnchor(pSelf, nodes, nReference + 1, nodes.Count() - 1, /*bBackwards*/false, styleName, pStart, pEnd,
                                   false /* bCaseSensitive */);
    return pTextNd;
}

namespace {

struct RefIdsMap
{
private:
    SwMarkName aName;
    std::set<sal_uInt16> aIds;
    std::set<sal_uInt16> aDstIds;
    std::map<sal_uInt16, sal_uInt16> sequencedIds; /// ID numbers sorted by sequence number.
    bool bInit;

    void       Init(SwDoc& rDoc, SwDoc& rDestDoc, bool bField );
    static void GetNoteIdsFromDoc( SwDoc& rDoc, std::set<sal_uInt16> &rIds );
    void       GetFieldIdsFromDoc( SwDoc& rDoc, std::set<sal_uInt16> &rIds );
    void       AddId( sal_uInt16 id, sal_uInt16 seqNum );
    static sal_uInt16 GetFirstUnusedId( std::set<sal_uInt16> &rIds );

public:
    explicit RefIdsMap( SwMarkName _aName ) : aName(std::move( _aName )), bInit( false ) {}

    void Check( SwDoc& rDoc, SwDoc& rDestDoc, SwGetRefField& rField, bool bField );

    const SwMarkName& GetName() const { return aName; }
};

}

/// Get a sorted list of the field IDs from a document.
/// @param[in]     rDoc The document to search.
/// @param[in,out] rIds The list of IDs found in the document.
void RefIdsMap::GetFieldIdsFromDoc( SwDoc& rDoc, std::set<sal_uInt16> &rIds)
{
    SwFieldType *const pType = rDoc.getIDocumentFieldsAccess().GetFieldType(SwFieldIds::SetExp, aName, false);
    if (!pType)
        return;
    std::vector<SwFormatField*> vFields;
    pType->GatherFields(vFields);
    for(const auto pF: vFields)
        rIds.insert(static_cast<SwSetExpField const*>(pF->GetField())->GetSeqNumber());
}

/// Get a sorted list of the footnote/endnote IDs from a document.
/// @param[in]     rDoc The document to search.
/// @param[in,out] rIds The list of IDs found in the document.
void RefIdsMap::GetNoteIdsFromDoc( SwDoc& rDoc, std::set<sal_uInt16> &rIds)
{
    for( auto n = rDoc.GetFootnoteIdxs().size(); n; )
        rIds.insert( rDoc.GetFootnoteIdxs()[ --n ]->GetSeqRefNo() );
}

/// Initialise the aIds and aDestIds collections from the source documents.
/// @param[in] rDoc     The source document.
/// @param[in] rDestDoc The destination document.
/// @param[in] bField   True if we're interested in all fields, false for footnotes.
void RefIdsMap::Init( SwDoc& rDoc, SwDoc& rDestDoc, bool bField )
{
    if( bInit )
        return;

    if( bField )
    {
        GetFieldIdsFromDoc( rDestDoc, aIds );
        GetFieldIdsFromDoc( rDoc, aDstIds );

        // Map all the new src fields to the next available unused id
        for (const auto& rId : aDstIds)
            AddId( GetFirstUnusedId(aIds), rId );

        // Change the Sequence number of all SetExp fields in the source document
        SwFieldType* pType = rDoc.getIDocumentFieldsAccess().GetFieldType( SwFieldIds::SetExp, aName, false );
        if(pType)
        {
            std::vector<SwFormatField*> vFields;
            pType->GatherFields(vFields, false);
            for(auto pF: vFields)
            {
                if(!pF->GetTextField())
                    continue;
                SwSetExpField *const pSetExp(static_cast<SwSetExpField *>(pF->GetField()));
                sal_uInt16 const n = pSetExp->GetSeqNumber();
                pSetExp->SetSeqNumber(sequencedIds[n]);
            }
        }
    }
    else
    {
        GetNoteIdsFromDoc( rDestDoc, aIds );
        GetNoteIdsFromDoc( rDoc, aDstIds );

        for (const auto& rId : aDstIds)
            AddId( GetFirstUnusedId(aIds), rId );

        // Change the footnotes/endnotes in the source doc to the new ID
        for ( const auto pFootnoteIdx : rDoc.GetFootnoteIdxs() )
        {
            sal_uInt16 const n = pFootnoteIdx->GetSeqRefNo();
            pFootnoteIdx->SetSeqNo(sequencedIds[n]);
        }
    }
    bInit = true;
}

/// Get the lowest number unused in the passed set.
/// @param[in] rIds The set of used ID numbers.
/// @returns The lowest number unused by the passed set
sal_uInt16 RefIdsMap::GetFirstUnusedId( std::set<sal_uInt16> &rIds )
{
    sal_uInt16 num(0);

    for( const auto& rId : rIds )
    {
        if( num != rId )
        {
            return num;
        }
        ++num;
    }
    return num;
}

/// Add a new ID and sequence number to the "occupied" collection.
/// @param[in] id     The ID number.
/// @param[in] seqNum The sequence number.
void RefIdsMap::AddId( sal_uInt16 id, sal_uInt16 seqNum )
{
    aIds.insert( id );
    sequencedIds[ seqNum ] = id;
}

void RefIdsMap::Check( SwDoc& rDoc, SwDoc& rDestDoc, SwGetRefField& rField,
                        bool bField )
{
    Init( rDoc, rDestDoc, bField);

    sal_uInt16 const nSeqNo = rField.GetSeqNo();

    // check if it needs to be remapped
    // if sequencedIds doesn't contain the number, it means there is no
    // SetExp field / footnote in the source document: do not modify
    // the number, which works well for copy from/paste to same document
    // (and if it is not the same document, there's no "correct" result anyway)
    if (sequencedIds.count(nSeqNo))
    {
        rField.SetSeqNo( sequencedIds[nSeqNo] );
    }
}

/// 1. if _both_ SetExp + GetExp / Footnote + GetExp field are copied,
///    ensure that both get a new unused matching number
/// 2. if only SetExp / Footnote is copied, it gets a new unused number
/// 3. if only GetExp field is copied, for the case of copy from / paste to
///    same document it's desirable to keep the same number;
///    for other cases of copy/paste or master documents it's not obvious
///    what is most desirable since it's going to be wrong anyway
void SwGetRefFieldType::MergeWithOtherDoc( SwDoc& rDestDoc )
{
    if (&rDestDoc == &m_rDoc)
        return;

    if (rDestDoc.IsClipBoard())
    {
        // when copying _to_ clipboard, expectation is that no fields exist
        // so no re-mapping is required to avoid collisions
        assert(!rDestDoc.getIDocumentFieldsAccess().GetSysFieldType(SwFieldIds::GetRef)->HasWriterListeners());
        return; // don't modify the fields in the source doc
    }

    // then there are RefFields in the DescDox - so all RefFields in the SourceDoc
    // need to be converted to have unique IDs for both documents
    RefIdsMap aFntMap { SwMarkName() };
    std::vector<std::unique_ptr<RefIdsMap>> aFieldMap;

    std::vector<SwFormatField*> vFields;
    GatherFields(vFields);
    for(auto pField: vFields)
    {
        SwGetRefField& rRefField = *static_cast<SwGetRefField*>(pField->GetField());
        switch( rRefField.GetSubType() )
        {
        case ReferencesSubtype::SequenceField:
            {
                RefIdsMap* pMap = nullptr;
                for( auto n = aFieldMap.size(); n; )
                {
                    if (aFieldMap[ --n ]->GetName() == rRefField.GetSetRefName())
                    {
                        pMap = aFieldMap[ n ].get();
                        break;
                    }
                }
                if( !pMap )
                {
                    pMap = new RefIdsMap( rRefField.GetSetRefName() );
                    aFieldMap.push_back(std::unique_ptr<RefIdsMap>(pMap));
                }

                pMap->Check(m_rDoc, rDestDoc, rRefField, true);
            }
            break;

        case ReferencesSubtype::Footnote:
        case ReferencesSubtype::Endnote:
            aFntMap.Check(m_rDoc, rDestDoc, rRefField, false);
            break;
        default: break;
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
