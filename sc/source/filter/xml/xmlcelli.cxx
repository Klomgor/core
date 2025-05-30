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

#include <limits>
#include <memory>
#include "xmlcelli.hxx"
#include "xmlimprt.hxx"
#include "xmlannoi.hxx"
#include <global.hxx>
#include <cellvalue.hxx>
#include <document.hxx>
#include <docuno.hxx>
#include <docsh.hxx>
#include <postit.hxx>
#include <sheetdata.hxx>
#include <cellform.hxx>
#include <validat.hxx>
#include <patattr.hxx>
#include <scitems.hxx>
#include <docpool.hxx>

#include "XMLTableShapeImportHelper.hxx"
#include "XMLStylesImportHelper.hxx"
#include "celltextparacontext.hxx"
#include "XMLCellRangeSourceContext.hxx"

#include <arealink.hxx>
#include <sfx2/linkmgr.hxx>
#include <scerrors.hxx>
#include <editutil.hxx>
#include <formulacell.hxx>
#include "editattributemap.hxx"
#include <tokenarray.hxx>
#include <scmatrix.hxx>
#include <stringutil.hxx>
#include <documentimport.hxx>
#include <externalrefmgr.hxx>

#include <xmloff/maptype.hxx>
#include <xmloff/xmltoken.hxx>
#include <xmloff/xmlprmap.hxx>
#include <xmloff/xmluconv.hxx>
#include <xmloff/families.hxx>
#include <xmloff/xmlnamespace.hxx>
#include <xmloff/prstylei.hxx>
#include <xmloff/xmlimppr.hxx>
#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <svx/svdocapt.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/outlobj.hxx>
#include <editeng/wghtitem.hxx>
#include <editeng/colritem.hxx>
#include <editeng/fhgtitem.hxx>
#include <editeng/postitem.hxx>
#include <editeng/flditem.hxx>
#include <editeng/fontitem.hxx>
#include <editeng/udlnitem.hxx>
#include <editeng/wrlmitem.hxx>
#include <editeng/crossedoutitem.hxx>
#include <editeng/charreliefitem.hxx>
#include <editeng/charscaleitem.hxx>
#include <editeng/contouritem.hxx>
#include <editeng/shdditem.hxx>
#include <editeng/kernitem.hxx>
#include <editeng/autokernitem.hxx>
#include <editeng/escapementitem.hxx>
#include <editeng/emphasismarkitem.hxx>
#include <editeng/langitem.hxx>
#include <svl/sharedstringpool.hxx>
#include <sax/tools/converter.hxx>
#include <sax/fastattribs.hxx>

#include <com/sun/star/util/NumberFormat.hpp>

#include <com/sun/star/sheet/ValidationType.hpp>
#include <com/sun/star/sheet/ValidationAlertStyle.hpp>

#include <rtl/ustrbuf.hxx>
#include <osl/diagnose.h>
#include <sal/log.hxx>
#include <i18nlangtag/lang.h>

#include <comphelper/servicehelper.hxx>
#include <comphelper/lok.hxx>

using namespace com::sun::star;
using namespace xmloff::token;

ScXMLTableRowCellContext::ParaFormat::ParaFormat(const ScEditEngineDefaulter& rEditEngine) :
    maItemSet(rEditEngine.GetEmptyItemSet()) {}

ScXMLTableRowCellContext::Field::Field(std::unique_ptr<SvxFieldData> pData) : mpData(std::move(pData)) {}

ScXMLTableRowCellContext::Field::~Field()
{
}

ScXMLTableRowCellContext::ScXMLTableRowCellContext( ScXMLImport& rImport,
                                      const rtl::Reference<sax_fastparser::FastAttributeList>& rAttrList,
                                      const bool bTempIsCovered,
                                      const sal_Int32 nTempRepeatedRows ) :
    ScXMLImportContext( rImport ),
    mpEditEngine(GetScImport().GetEditEngine()),
    mnCurParagraph(0),
    fValue(std::numeric_limits<double>::quiet_NaN()),
    nMergedRows(1),
    nMatrixRows(0),
    nRepeatedRows(nTempRepeatedRows),
    nMergedCols(1),
    nMatrixCols(0),
    nColsRepeated(1),
    rXMLImport(rImport),
    eGrammar( formula::FormulaGrammar::GRAM_STORAGE_DEFAULT),
    nCellType(util::NumberFormat::TEXT),
    bIsMerged(false),
    bIsMatrix(false),
    bIsCovered(bTempIsCovered),
    bIsEmpty(true),
    mbNewValueType(false),
    mbErrorValue(false),
    bSolarMutexLocked(false),
    bFormulaTextResult(false),
    mbPossibleErrorCell(false),
    mbCheckWithCompilerForError(false),
    mbEditEngineHasText(false),
    mbHasFormatRuns(false),
    mbHasStyle(false),
    mbPossibleEmptyDisplay(false)
{
    rXMLImport.GetTables().AddColumn(bTempIsCovered);

    std::optional<OUString> xStyleName;
    std::optional<OUString> xCurrencySymbol;
    if ( rAttrList.is() )
    {
        for (auto &it : *rAttrList)
        {
            switch ( it.getToken() )
            {
                case XML_ELEMENT( TABLE, XML_STYLE_NAME ):
                    xStyleName = it.toString();
                    mbHasStyle = true;
                break;
                case XML_ELEMENT( TABLE, XML_CONTENT_VALIDATION_NAME ):
                    OSL_ENSURE(!maContentValidationName, "here should be only one Validation Name");
                    if (!it.isEmpty())
                        maContentValidationName = it.toString();
                break;
                case XML_ELEMENT( TABLE, XML_NUMBER_ROWS_SPANNED ):
                    bIsMerged = true;
                    nMergedRows = static_cast<SCROW>(it.toInt32());
                break;
                case XML_ELEMENT( TABLE, XML_NUMBER_COLUMNS_SPANNED ):
                    bIsMerged = true;
                    nMergedCols = static_cast<SCCOL>(it.toInt32());
                break;
                case XML_ELEMENT( TABLE, XML_NUMBER_MATRIX_COLUMNS_SPANNED ):
                    bIsMatrix = true;
                    nMatrixCols = static_cast<SCCOL>(it.toInt32());
                break;
                case XML_ELEMENT( TABLE, XML_NUMBER_MATRIX_ROWS_SPANNED ):
                    bIsMatrix = true;
                    nMatrixRows = static_cast<SCROW>(it.toInt32());
                break;
                case XML_ELEMENT( TABLE, XML_NUMBER_COLUMNS_REPEATED ):
                {
                    if (ScDocument* pDoc = rImport.GetDocument())
                    {
                        nColsRepeated = static_cast<SCCOL>(
                            std::min<sal_Int32>( pDoc->GetSheetLimits().GetMaxColCount(),
                            std::max( it.toInt32(), static_cast<sal_Int32>(1) ) ));
                    }
                }
                break;
                case XML_ELEMENT( OFFICE, XML_VALUE_TYPE ):
                    nCellType = ScXMLImport::GetCellType(it.toCString(), it.getLength());
                    bIsEmpty = false;
                break;
                case XML_ELEMENT( CALC_EXT, XML_VALUE_TYPE ):
                    if(it.isString( "error" ) )
                        mbErrorValue = true;
                    else
                        nCellType = ScXMLImport::GetCellType(it.toCString(), it.getLength());
                    bIsEmpty = false;
                    mbNewValueType = true;
                break;
                case XML_ELEMENT( OFFICE, XML_VALUE ):
                {
                    if (!it.isEmpty())
                    {
                        fValue = it.toDouble();
                        bIsEmpty = false;

                        //if office:value="0", let's get the text:p in case this is
                        //a special case in HasSpecialCaseFormulaText(). If it
                        //turns out not to be a special case, we'll use the 0 value.
                        if(fValue == 0.0)
                            bFormulaTextResult = true;
                    }
                }
                break;
                case XML_ELEMENT( OFFICE, XML_DATE_VALUE ):
                {
                    if (!it.isEmpty() && rXMLImport.SetNullDateOnUnitConverter())
                    {
                        rXMLImport.GetMM100UnitConverter().convertDateTime(fValue, it.toView());
                        bIsEmpty = false;
                    }
                }
                break;
                case XML_ELEMENT( OFFICE, XML_TIME_VALUE ):
                {
                    if (!it.isEmpty())
                    {
                        ::sax::Converter::convertDuration(fValue, it.toView());
                        bIsEmpty = false;
                    }
                }
                break;
                case XML_ELEMENT( OFFICE, XML_STRING_VALUE ):
                {
                    if (!it.isEmpty())
                    {
                        OSL_ENSURE(!maStringValue, "here should be only one string value");
                        maStringValue = it.toString();
                        bIsEmpty = false;
                    }
                }
                break;
                case XML_ELEMENT( OFFICE , XML_BOOLEAN_VALUE ):
                {
                    if (!it.isEmpty())
                    {
                        if ( IsXMLToken( it, XML_TRUE ) )
                            fValue = 1.0;
                        else if ( IsXMLToken( it, XML_FALSE ) )
                            fValue = 0.0;
                        else
                            fValue = it.toDouble();
                        bIsEmpty = false;
                    }
                }
                break;
                case XML_ELEMENT( TABLE, XML_FORMULA ):
                {
                    if (!it.isEmpty())
                    {
                        OSL_ENSURE(!maFormula, "here should be only one formula");
                        OUString aFormula, aFormulaNmsp;
                        rXMLImport.ExtractFormulaNamespaceGrammar( aFormula, aFormulaNmsp, eGrammar, it.toString() );
                        maFormula = FormulaWithNamespace(aFormula, aFormulaNmsp);
                    }
                }
                break;
                case XML_ELEMENT( OFFICE, XML_CURRENCY ):
                    xCurrencySymbol = it.toString();
                break;
                default:
                    ;
            }
        }
    }

    if (maFormula)
    {
        if (nCellType == util::NumberFormat::TEXT)
            bFormulaTextResult = true;
        if(nCellType == util::NumberFormat::DATETIME)
            nCellType = util::NumberFormat::UNDEFINED;
        //if bIsEmpty is true at this point, then there is no office value.
        //we must get the text:p (even if it is empty) in case this a special
        //case in HasSpecialCaseFormulaText().
        if(bIsEmpty)
            bFormulaTextResult = true;
    }
    rXMLImport.GetStylesImportHelper()->SetAttributes(std::move(xStyleName), std::move(xCurrencySymbol), nCellType);
}

ScXMLTableRowCellContext::~ScXMLTableRowCellContext()
{
}

void ScXMLTableRowCellContext::LockSolarMutex()
{
    if (!bSolarMutexLocked)
    {
        GetScImport().LockSolarMutex();
        bSolarMutexLocked = true;
    }
}

namespace {

bool cellExists( const ScDocument& rDoc, const ScAddress& rCellPos )
{
    return( rCellPos.Col() >= 0 && rCellPos.Row() >= 0 &&
            rCellPos.Col() <= rDoc.MaxCol() && rCellPos.Row() <= rDoc.MaxRow() );
}

}

void ScXMLTableRowCellContext::PushParagraphSpan(std::u16string_view rSpan, const OUString& rStyleName)
{
    sal_Int32 nBegin = maParagraph.getLength();
    sal_Int32 nEnd = nBegin + rSpan.size();
    maParagraph.append(rSpan);

    PushFormat(nBegin, nEnd, rStyleName);
}

void ScXMLTableRowCellContext::PushParagraphField(std::unique_ptr<SvxFieldData> pData, const OUString& rStyleName)
{
    mbHasFormatRuns = true;
    maFields.push_back(std::make_unique<Field>(std::move(pData)));
    Field& rField = *maFields.back();

    sal_Int32 nPos = maParagraph.getLength();
    maParagraph.append('\1'); // Placeholder text for inserted field item.
    rField.maSelection.start.nPara = rField.maSelection.end.nPara = mnCurParagraph;
    rField.maSelection.start.nIndex = nPos;
    rField.maSelection.end.nIndex = nPos+1;

    PushFormat(nPos, nPos+1, rStyleName);
}

void ScXMLTableRowCellContext::PushFormat(sal_Int32 nBegin, sal_Int32 nEnd, const OUString& rStyleName)
{
    if (rStyleName.isEmpty())
        return;

    // Get the style information from xmloff.
    rtl::Reference<XMLPropertySetMapper> xMapper = GetImport().GetTextImport()->GetTextImportPropertySetMapper()->getPropertySetMapper();
    if (!xMapper.is())
        // We can't do anything without the mapper.
        return;

    sal_Int32 nEntryCount = xMapper->GetEntryCount();

    SvXMLStylesContext* pAutoStyles = GetImport().GetAutoStyles();
    if (!pAutoStyles)
        return;

    // Style name for text span corresponds with the name of an automatic style.
    const XMLPropStyleContext* pStyle = dynamic_cast<const XMLPropStyleContext*>(
        pAutoStyles->FindStyleChildContext(XmlStyleFamily::TEXT_TEXT, rStyleName));

    if (!pStyle)
        // No style by that name found.
        return;

    const std::vector<XMLPropertyState>& rProps = pStyle->GetProperties();
    if (rProps.empty())
        return;

    const ScXMLEditAttributeMap& rEditAttrMap = GetScImport().GetEditAttributeMap();

    mbHasFormatRuns = true;
    maFormats.push_back(std::make_unique<ParaFormat>(*mpEditEngine));
    ParaFormat& rFmt = *maFormats.back();
    rFmt.maSelection.start.nPara = rFmt.maSelection.end.nPara = mnCurParagraph;
    rFmt.maSelection.start.nIndex = nBegin;
    rFmt.maSelection.end.nIndex = nEnd;

    // Store the used text styles for export.
    ScSheetSaveData* pSheetData = rXMLImport.GetScModel()->GetSheetSaveData();
    ScAddress aCellPos = rXMLImport.GetTables().GetCurrentCellPos();
    pSheetData->AddTextStyle(rStyleName, aCellPos, rFmt.maSelection);

    std::unique_ptr<SfxPoolItem> pPoolItem;
    sal_uInt16 nLastItemID = EE_CHAR_END + 1;

    for (const auto& rProp : rProps)
    {
        if (rProp.mnIndex == -1 || rProp.mnIndex >= nEntryCount)
            continue;

        const OUString& rName = xMapper->GetEntryAPIName(rProp.mnIndex);
        const ScXMLEditAttributeMap::Entry* pEntry = rEditAttrMap.getEntryByAPIName(rName);
        if (!pEntry)
            continue;

        if (nLastItemID != pEntry->mnItemID && pPoolItem)
        {
            // Flush the last item when the item ID changes.
            rFmt.maItemSet.Put(std::move(pPoolItem));
        }

        switch (pEntry->mnItemID)
        {
            case EE_CHAR_FONTINFO:
            case EE_CHAR_FONTINFO_CJK:
            case EE_CHAR_FONTINFO_CTL:
            {
                // Font properties need to be consolidated into a single item.
                if (!pPoolItem)
                    pPoolItem.reset(new SvxFontItem(pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_WEIGHT:
            case EE_CHAR_WEIGHT_CJK:
            case EE_CHAR_WEIGHT_CTL:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxWeightItem(WEIGHT_NORMAL, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_FONTHEIGHT:
            case EE_CHAR_FONTHEIGHT_CJK:
            case EE_CHAR_FONTHEIGHT_CTL:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxFontHeightItem(240, 100, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_ITALIC:
            case EE_CHAR_ITALIC_CJK:
            case EE_CHAR_ITALIC_CTL:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxPostureItem(ITALIC_NONE, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_UNDERLINE:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxUnderlineItem(LINESTYLE_NONE, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_OVERLINE:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxOverlineItem(LINESTYLE_NONE, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_COLOR:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxColorItem(pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_WLM:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxWordLineModeItem(false, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_STRIKEOUT:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxCrossedOutItem(STRIKEOUT_NONE, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_RELIEF:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxCharReliefItem(FontRelief::NONE, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_OUTLINE:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxContourItem(false, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_SHADOW:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxShadowedItem(false, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_KERNING:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxKerningItem(0, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_PAIRKERNING:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxAutoKernItem(false, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_FONTWIDTH:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxCharScaleWidthItem(100, TypedWhichId<SvxCharScaleWidthItem>(pEntry->mnItemID)));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_ESCAPEMENT:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxEscapementItem(pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_EMPHASISMARK:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxEmphasisMarkItem(FontEmphasisMark::NONE, TypedWhichId<SvxEmphasisMarkItem>(pEntry->mnItemID)));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            case EE_CHAR_LANGUAGE:
            case EE_CHAR_LANGUAGE_CJK:
            case EE_CHAR_LANGUAGE_CTL:
            {
                if (!pPoolItem)
                    pPoolItem.reset(new SvxLanguageItem(LANGUAGE_DONTKNOW, pEntry->mnItemID));

                pPoolItem->PutValue(rProp.maValue, pEntry->mnFlag);
            }
            break;
            default:
                ;
        }

        nLastItemID = pEntry->mnItemID;
    }

    if (pPoolItem)
        rFmt.maItemSet.Put(std::move(pPoolItem));
}

OUString ScXMLTableRowCellContext::GetFirstParagraph() const
{
    if (!maFirstParagraph)
        return mpEditEngine->GetText(0);

    return *maFirstParagraph;
}

void ScXMLTableRowCellContext::PushParagraphFieldDate(const OUString& rStyleName)
{
    PushParagraphField(std::make_unique<SvxDateField>(), rStyleName);
}

void ScXMLTableRowCellContext::PushParagraphFieldSheetName(const OUString& rStyleName)
{
    SCTAB nTab = GetScImport().GetTables().GetCurrentCellPos().Tab();
    PushParagraphField(std::make_unique<SvxTableField>(nTab), rStyleName);
}

void ScXMLTableRowCellContext::PushParagraphFieldDocTitle(const OUString& rStyleName)
{
    PushParagraphField(std::make_unique<SvxFileField>(), rStyleName);
}

void ScXMLTableRowCellContext::PushParagraphFieldURL(
    const OUString& rURL, const OUString& rRep, const OUString& rStyleName, const OUString& rTargetFrame)
{
    OUString aAbsURL = GetScImport().GetAbsoluteReference(rURL);
    std::unique_ptr<SvxURLField> pURLField(new SvxURLField(aAbsURL, rRep, SvxURLFormat::Repr));
    pURLField->SetTargetFrame(rTargetFrame);
    PushParagraphField(std::move(pURLField), rStyleName);
}

void ScXMLTableRowCellContext::PushParagraphEnd()
{
    // EditEngine always has at least one paragraph even when its content is empty.

    if (mbEditEngineHasText)
    {
        if (maFirstParagraph)
        {
            // Flush the cached first paragraph first.
            mpEditEngine->Clear();
            mpEditEngine->SetTextCurrentDefaults(*maFirstParagraph);
            maFirstParagraph.reset();
        }
        mpEditEngine->InsertParagraph(mpEditEngine->GetParagraphCount(), maParagraph.makeStringAndClear());
    }
    else if (mbHasFormatRuns || ScStringUtil::isMultiline(maParagraph))
    {
        mpEditEngine->Clear();
        mpEditEngine->SetTextCurrentDefaults(maParagraph.makeStringAndClear());
        mbEditEngineHasText = true;
    }
    else if (mnCurParagraph == 0)
    {
        maFirstParagraph = maParagraph.makeStringAndClear();
        mbEditEngineHasText = true;
    }

    ++mnCurParagraph;
}

uno::Reference< xml::sax::XFastContextHandler > SAL_CALL ScXMLTableRowCellContext::createFastChildContext(
    sal_Int32 nElement, const uno::Reference< xml::sax::XFastAttributeList >& xAttrList )
{
    SvXMLImportContext *pContext = nullptr;
    sax_fastparser::FastAttributeList *pAttribList =
        &sax_fastparser::castToFastAttributeList( xAttrList );

    bool bTextP(false);
    switch (nElement)
    {
        case XML_ELEMENT( TEXT, XML_P ):
        {
            bIsEmpty = false;
            bTextP = true;

            pContext = new ScXMLCellTextParaContext(rXMLImport, *this);
        }
        break;
        case XML_ELEMENT( TABLE, XML_SUB_TABLE ):
        {
            SAL_WARN("sc", "ScXMLTableRowCellContext::createFastChildContext: subtables are not supported");
        }
        break;
        case XML_ELEMENT( TABLE, XML_DETECTIVE ):
        {
            bIsEmpty = false;
            if (!pDetectiveObjVec)
                pDetectiveObjVec.reset( new ScMyImpDetectiveObjVec );
            pContext = new ScXMLDetectiveContext(
                rXMLImport, pDetectiveObjVec.get() );
        }
        break;
        case XML_ELEMENT( TABLE, XML_CELL_RANGE_SOURCE ):
        {
            bIsEmpty = false;
            if (!pCellRangeSource)
                pCellRangeSource.reset(new ScMyImpCellRangeSource());
            pContext = new ScXMLCellRangeSourceContext(
                rXMLImport, pAttribList, pCellRangeSource.get() );
        }
        break;
        case XML_ELEMENT(OFFICE, XML_ANNOTATION):
        {
            bIsEmpty = false;
            OSL_ENSURE(
                !mxAnnotationData,
                "ScXMLTableRowCellContext::CreateChildContext - multiple annotations in one cell");
            mxAnnotationData.reset( new ScXMLAnnotationData );
            pContext = new ScXMLAnnotationContext( rXMLImport, nElement,
                                                    xAttrList, *mxAnnotationData);
        }
        break;
    }

    if (!pContext && !bTextP)
    {
        ScAddress aCellPos = rXMLImport.GetTables().GetCurrentCellPos();
        uno::Reference<drawing::XShapes> xShapes (rXMLImport.GetTables().GetCurrentXShapes());
        if (xShapes.is())
        {
            if (ScDocument* pDoc = rXMLImport.GetDocument())
            {
                if (aCellPos.Col() > pDoc->MaxCol())
                    aCellPos.SetCol(pDoc->MaxCol());
                if (aCellPos.Row() > pDoc->MaxRow())
                    aCellPos.SetRow(pDoc->MaxRow());
                XMLTableShapeImportHelper* pTableShapeImport =
                        static_cast< XMLTableShapeImportHelper* >( rXMLImport.GetShapeImport().get() );
                pTableShapeImport->SetOnTable(false);
                pTableShapeImport->SetCell(aCellPos);
                pContext = XMLShapeImportHelper::CreateGroupChildContext(
                    rXMLImport, nElement, xAttrList, xShapes);
                if (pContext)
                {
                    bIsEmpty = false;
                    rXMLImport.ProgressBarIncrement();
                }
            }
        }
    }

    return pContext;
}

void ScXMLTableRowCellContext::DoMerge( const ScAddress& rScAddress, const SCCOL nCols, const SCROW nRows )
{
    SCCOL mergeToCol = rScAddress.Col() + nCols;
    SCROW mergeToRow = rScAddress.Row() + nRows;
    if (ScDocument* pDoc = rXMLImport.GetDocument())
    {
        bool bInBounds = rScAddress.Col() <= pDoc->MaxCol() && rScAddress.Row() <= pDoc->MaxRow() &&
                           mergeToCol <= pDoc->MaxCol() && mergeToRow <= pDoc->MaxRow();
        if( bInBounds )
        {
            pDoc->DoMerge( rScAddress.Col(), rScAddress.Row(),
                           mergeToCol, mergeToRow, rScAddress.Tab() );
        }
    }
}

namespace {

ScValidationMode validationTypeToMode( const sheet::ValidationType eVType )
{
    ScValidationMode eMode;
    switch( eVType )
    {
        case sheet::ValidationType_WHOLE:               eMode = SC_VALID_WHOLE;     break;
        case sheet::ValidationType_DECIMAL:             eMode = SC_VALID_DECIMAL;   break;
        case sheet::ValidationType_DATE:                eMode = SC_VALID_DATE;      break;
        case sheet::ValidationType_TIME:                eMode = SC_VALID_TIME;      break;
        case sheet::ValidationType_TEXT_LEN:            eMode = SC_VALID_TEXTLEN;   break;
        case sheet::ValidationType_LIST:                eMode = SC_VALID_LIST;      break;
        case sheet::ValidationType_CUSTOM:              eMode = SC_VALID_CUSTOM;    break;
        default:                                        eMode = SC_VALID_ANY;       break;
    }
    return eMode;
}

ScValidErrorStyle validAlertToValidError( const sheet::ValidationAlertStyle eVAlertStyle )
{
    ScValidErrorStyle eVErrStyle;
    switch( eVAlertStyle )
    {
        case sheet::ValidationAlertStyle_STOP:          eVErrStyle = SC_VALERR_STOP;      break;
        case sheet::ValidationAlertStyle_WARNING:       eVErrStyle = SC_VALERR_WARNING;   break;
        case sheet::ValidationAlertStyle_MACRO:         eVErrStyle = SC_VALERR_MACRO;     break;
        default:                                        eVErrStyle = SC_VALERR_INFO;      break;
        //should INFO be the default?  seems to be the most unobtrusive choice.
    }
    return eVErrStyle;
}

}

void ScXMLTableRowCellContext::SetContentValidation( const ScRange& rScRange )
{
    if (!maContentValidationName)
        return;

    ScDocument* pDoc = rXMLImport.GetDocument();
    if (!pDoc)
        return;

    ScMyImportValidation aValidation;
    aValidation.eGrammar1 = aValidation.eGrammar2 = pDoc->GetStorageGrammar();
    if( !rXMLImport.GetValidation(*maContentValidationName, aValidation) )
        return;

    ScValidationData aScValidationData(
        validationTypeToMode(aValidation.aValidationType),
        ScConditionEntry::GetModeFromApi(aValidation.aOperator),
        aValidation.sFormula1, aValidation.sFormula2, *pDoc, ScAddress(),
        aValidation.sFormulaNmsp1, aValidation.sFormulaNmsp2,
        aValidation.eGrammar1, aValidation.eGrammar2
    );

    aScValidationData.SetIgnoreBlank( aValidation.bIgnoreBlanks );
    aScValidationData.SetCaseSensitive( aValidation.bCaseSensitive );
    aScValidationData.SetListType( aValidation.nShowList );

    // set strings for error / input even if disabled (and disable afterwards)
    aScValidationData.SetInput( aValidation.sInputTitle, aValidation.sInputMessage );
    if( !aValidation.bShowInputMessage )
        aScValidationData.ResetInput();
    aScValidationData.SetError( aValidation.sErrorTitle, aValidation.sErrorMessage, validAlertToValidError(aValidation.aAlertStyle) );
    if( !aValidation.bShowErrorMessage )
        aScValidationData.ResetError();

    if( !aValidation.sBaseCellAddress.isEmpty() )
        aScValidationData.SetSrcString( aValidation.sBaseCellAddress );

    sal_uInt32 nIndex = pDoc->AddValidationEntry( aScValidationData );

    ScPatternAttr aPattern(pDoc->getCellAttributeHelper());
    aPattern.GetItemSet().Put( SfxUInt32Item( ATTR_VALIDDATA, nIndex ) );
    if( rScRange.aStart == rScRange.aEnd )  //for a single cell
    {
        pDoc->ApplyPattern( rScRange.aStart.Col(), rScRange.aStart.Row(),
                            rScRange.aStart.Tab(), aPattern );
    }
    else  //for repeating cells
    {
        pDoc->ApplyPatternAreaTab( rScRange.aStart.Col(), rScRange.aStart.Row(),
                               rScRange.aEnd.Col(), rScRange.aEnd.Row(),
                               rScRange.aStart.Tab(), aPattern );
    }

    // is the below still needed?
    // For now, any sheet with validity is blocked from stream-copying.
    // Later, the validation names could be stored along with the style names.
    ScSheetSaveData* pSheetData = GetScImport().GetScModel()->GetSheetSaveData();
    pSheetData->BlockSheet( GetScImport().GetTables().GetCurrentSheet() );
}

void ScXMLTableRowCellContext::SetContentValidation( const ScAddress& rCellPos )
{
    SetContentValidation( ScRange(rCellPos, rCellPos) );
}

void ScXMLTableRowCellContext::SetAnnotation(const ScAddress& rPos)
{
    ScDocument* pDoc = rXMLImport.GetDocument();
    if (!pDoc || !mxAnnotationData)
        return;

    LockSolarMutex();

    ScPostIt* pNote = nullptr;

    uno::Reference< drawing::XShapes > xShapes = rXMLImport.GetTables().GetCurrentXShapes();
    sal_Int32 nOldShapeCount = xShapes.is() ? xShapes->getCount() : 0;

    OSL_ENSURE( !mxAnnotationData->mxShape.is() || mxAnnotationData->mxShapes.is(),
        "ScXMLTableRowCellContext::SetAnnotation - shape without drawing page" );
    if( mxAnnotationData->mxShape.is() && mxAnnotationData->mxShapes.is() )
    {
        OSL_ENSURE( mxAnnotationData->mxShapes.get() == xShapes.get(), "ScXMLTableRowCellContext::SetAnnotation - different drawing pages" );

        /*  Don't attempt to get the style from the SdrObject,
            as it might be a default assigned one. */
        const SvXMLStylesContext* pStylesCtxt = rXMLImport.GetShapeImport()->GetAutoStylesContext();
        const SvXMLStyleContext* pStyle = pStylesCtxt ? pStylesCtxt->FindStyleChildContext(
            XmlStyleFamily::SD_GRAPHICS_ID, mxAnnotationData->maStyleName) : nullptr;
        OUString aStyleName = pStyle ? pStyle->GetParentName() : mxAnnotationData->maStyleName;
        assert(!pStylesCtxt || !pStylesCtxt->FindStyleChildContext(
            XmlStyleFamily::SD_GRAPHICS_ID, aStyleName));
        aStyleName = rXMLImport.GetStyleDisplayName(XmlStyleFamily::SD_GRAPHICS_ID, aStyleName);

        SdrObject* pObject = SdrObject::getSdrObjectFromXShape(mxAnnotationData->mxShape);
        OSL_ENSURE( pObject, "ScXMLTableRowCellContext::SetAnnotation - cannot get SdrObject from shape" );

        /*  Try to reuse the drawing object already created (but only if the
            note is visible, and the object is a caption object). */
        if( mxAnnotationData->mbShown && mxAnnotationData->mbUseShapePos && !comphelper::LibreOfficeKit::isActive())
        {
            if( SdrCaptionObj* pCaption = dynamic_cast< SdrCaptionObj* >( pObject ) )
            {
                OSL_ENSURE( !pCaption->GetLogicRect().IsEmpty(), "ScXMLTableRowCellContext::SetAnnotation - invalid caption rectangle" );
                // create the cell note with the caption object
                pNote = ScNoteUtil::CreateNoteFromCaption( *pDoc, rPos, pCaption, !aStyleName.isEmpty() );
                // forget pointer to object (do not create note again below)
                pObject = nullptr;
            }
        }

        // drawing object has not been used to create a note -> use shape data
        if( pObject )
        {
            // rescue settings from drawing object before the shape is removed
            SfxItemSet aItemSet( pObject->GetMergedItemSet() );
            std::optional<OutlinerParaObject> pOutlinerObj;
            if (auto p = pObject->GetOutlinerParaObject())
                pOutlinerObj = *p;
            tools::Rectangle aCaptionRect;
            if( mxAnnotationData->mbUseShapePos )
                aCaptionRect = pObject->GetLogicRect();
            // remove the shape from the drawing page, this invalidates pObject
            mxAnnotationData->mxShapes->remove( mxAnnotationData->mxShape );
            pObject = nullptr;
            // update current number of existing objects
            if( xShapes.is() )
                nOldShapeCount = xShapes->getCount();

            // an outliner object is required (empty note captions not allowed)
            if (pOutlinerObj)
            {
                // create cell note with all data from drawing object
                if(!comphelper::LibreOfficeKit::isActive())
                {
                    pNote = ScNoteUtil::CreateNoteFromObjectData( *pDoc, rPos,
                        std::move(aItemSet), aStyleName, *pOutlinerObj,
                        aCaptionRect, mxAnnotationData->mbShown );
                }
                else
                {
                    pNote = ScNoteUtil::CreateNoteFromObjectData( *pDoc, rPos,
                        std::move(aItemSet), aStyleName, *pOutlinerObj,
                        aCaptionRect, false );
                }

            }
        }
    }
    else if( !mxAnnotationData->maSimpleText.isEmpty() )
    {
        // create note from simple text
        pNote = ScNoteUtil::CreateNoteFromString( *pDoc, rPos,
            mxAnnotationData->maSimpleText, mxAnnotationData->mbShown, false );
    }

    // set author and date
    if( pNote )
    {
        double fDate;
        if (rXMLImport.GetMM100UnitConverter().convertDateTime(fDate, mxAnnotationData->maCreateDate))
        {
            OUString aDate;
            if (comphelper::LibreOfficeKit::isActive())
            {
                //online handles the date format itself in browser
                aDate = mxAnnotationData->maCreateDate;
            }
            else
            {
                SvNumberFormatter* pNumForm = pDoc->GetFormatTable();

                // Date string is in format ISO 8601 inside <dc:date>
                // i.e: 2024-08-14 or 2024-08-14T23:55:06 or 20240814T235506
                // Time always has prefix 'T'
                sal_uInt32 nfIndex = pNumForm->GetFormatIndex(
                    mxAnnotationData->maCreateDate.indexOf('T') > -1 ? NF_DATETIME_SYS_DDMMYYYY_HHMMSS
                                                                    : NF_DATE_SYS_DDMMYYYY,
                    LANGUAGE_SYSTEM);
                const Color* pColor = nullptr;
                pNumForm->GetOutputString( fDate, nfIndex, aDate, &pColor );
            }
            pNote->SetDate( aDate );
        }
        pNote->SetAuthor( mxAnnotationData->maAuthor );
    }

    // register a shape that has been newly created in the ScNoteUtil functions
    if( xShapes.is() && (nOldShapeCount < xShapes->getCount()) )
    {
        uno::Reference< drawing::XShape > xShape;
        rXMLImport.GetShapeImport()->shapeWithZIndexAdded( xShape, xShapes->getCount() );
    }

    // store the style names for stream copying
    ScSheetSaveData* pSheetData = rXMLImport.GetScModel()->GetSheetSaveData();
    pSheetData->HandleNoteStyles( mxAnnotationData->maStyleName, mxAnnotationData->maTextStyle, rPos );

    for (const auto& rContentStyle : mxAnnotationData->maContentStyles)
    {
        pSheetData->AddNoteContentStyle( rContentStyle.mnFamily, rContentStyle.maName, rPos, rContentStyle.maSelection );
    }
}

// core implementation
void ScXMLTableRowCellContext::SetDetectiveObj( const ScAddress& rPosition )
{
    ScDocument* pDoc = rXMLImport.GetDocument();
    if( !pDoc || !cellExists(*pDoc, rPosition) || !pDetectiveObjVec || pDetectiveObjVec->empty() )
        return;

    LockSolarMutex();
    ScDetectiveFunc aDetFunc( *pDoc, rPosition.Tab() );
    uno::Reference<container::XIndexAccess> xShapesIndex = rXMLImport.GetTables().GetCurrentXShapes(); // make draw page
    for(const auto& rDetectiveObj : *pDetectiveObjVec)
    {
        aDetFunc.InsertObject( rDetectiveObj.eObjType, rPosition, rDetectiveObj.aSourceRange, rDetectiveObj.bHasError );
        if (xShapesIndex.is())
        {
            sal_Int32 nShapes = xShapesIndex->getCount();
            uno::Reference < drawing::XShape > xShape;
            rXMLImport.GetShapeImport()->shapeWithZIndexAdded(xShape, nShapes);
        }
    }
}

// core implementation
void ScXMLTableRowCellContext::SetCellRangeSource( const ScAddress& rPosition )
{
    ScDocument* pDoc = rXMLImport.GetDocument();
    if( !pDoc || !cellExists(*pDoc, rPosition) || !pCellRangeSource || pCellRangeSource->sSourceStr.isEmpty() ||
        pCellRangeSource->sFilterName.isEmpty() || pCellRangeSource->sURL.isEmpty() )
        return;

    LockSolarMutex();
    ScRange aDestRange( rPosition.Col(), rPosition.Row(), rPosition.Tab(),
        rPosition.Col() + static_cast<SCCOL>(pCellRangeSource->nColumns - 1),
        rPosition.Row() + static_cast<SCROW>(pCellRangeSource->nRows - 1), rPosition.Tab() );
    OUString sFilterName( pCellRangeSource->sFilterName );
    OUString sSourceStr( pCellRangeSource->sSourceStr );
    ScAreaLink* pLink = new ScAreaLink( *pDoc->GetDocumentShell(), pCellRangeSource->sURL,
        sFilterName, pCellRangeSource->sFilterOptions, sSourceStr, aDestRange, pCellRangeSource->nRefresh );
    sfx2::LinkManager* pLinkManager = pDoc->GetLinkManager();
    pLinkManager->InsertFileLink( *pLink, sfx2::SvBaseLinkObjectType::ClientFile, pCellRangeSource->sURL, &sFilterName, &sSourceStr );
}

void ScXMLTableRowCellContext::SetFormulaCell(ScFormulaCell* pFCell) const
{
    if(!pFCell)
        return;

    bool bMayForceNumberformat = true;

    if(mbErrorValue)
    {
        // don't do anything here
        // we need to recalc anyway
    }
    else if( bFormulaTextResult && maStringValue )
    {
        if( !IsPossibleErrorString() )
        {
            ScDocument* pDoc = rXMLImport.GetDocument();
            pFCell->SetHybridString(pDoc->GetSharedStringPool().intern(*maStringValue));
            pFCell->ResetDirty();
            // A General format doesn't force any other format for a string
            // result, don't attempt to recalculate this later.
            bMayForceNumberformat = false;
        }
    }
    else if (std::isfinite(fValue))
    {
        pFCell->SetHybridDouble(fValue);
        if (mbPossibleEmptyDisplay && fValue == 0.0)
        {
            // Needs to be recalculated to propagate, otherwise would be
            // propagated as empty string. So don't ResetDirty().
            pFCell->SetHybridEmptyDisplayedAsString();
        }
        else
            pFCell->ResetDirty();
    }

    if (bMayForceNumberformat)
        // Re-calculate to get number format only when style is not set.
        pFCell->SetNeedNumberFormat(!mbHasStyle);
}

void ScXMLTableRowCellContext::PutTextCell( const ScAddress& rCurrentPos,
        const SCCOL nCurrentCol, const ::std::optional< OUString >& pOUText )
{
    ScDocument* pDoc = rXMLImport.GetDocument();
    bool bDoIncrement = true;
    //matrix reference cells that contain text formula results;
    //cell was already put in document, just need to set text here.
    if( pDoc && rXMLImport.GetTables().IsPartOfMatrix(rCurrentPos) )
    {
        ScRefCellValue aCell(*pDoc, rCurrentPos);
        bDoIncrement = aCell.getType() == CELLTYPE_FORMULA;
        if ( bDoIncrement )
        {
            ScFormulaCell* pFCell = aCell.getFormula();
            OUString aCellString;
            if (maStringValue)
                aCellString = *maStringValue;
            else if (mbEditEngineHasText)
                aCellString = GetFirstParagraph();
            else if ( nCurrentCol > 0 && pOUText && !pOUText->isEmpty() )
                aCellString = *pOUText;
            else
                bDoIncrement = false;

            if(mbErrorValue)
                bDoIncrement = false;

            if(!aCellString.isEmpty())
            {
                if (bDoIncrement && !IsPossibleErrorString() && pFCell)
                {
                    pFCell->SetHybridString(pDoc->GetSharedStringPool().intern(aCellString));
                    pFCell->ResetDirty();
                }
                else
                {
                    ScAddress aTopLeftMatrixCell;
                    if (pFCell && pFCell->GetMatrixOrigin(*pDoc, aTopLeftMatrixCell))
                    {
                        ScFormulaCell* pMatrixCell = pDoc->GetFormulaCell(aTopLeftMatrixCell);
                        if (pMatrixCell)
                            pMatrixCell->SetDirty();
                    }
                    else
                        SAL_WARN("sc", "matrix cell without matrix");
                }
            }
        }
    }
    else //regular text cells
    {
        ScDocumentImport& rDoc = rXMLImport.GetDoc();
        if (maStringValue)
        {
            rDoc.setStringCell(rCurrentPos, *maStringValue);
        }
        else if (mbEditEngineHasText)
        {
            if (maFirstParagraph)
            {
                // This is a normal text without format runs.
                rDoc.setStringCell(rCurrentPos, *maFirstParagraph);
            }
            else
            {
                // This text either has format runs, has field(s), or consists of multiple lines.
                for (const auto& rxFormat : maFormats)
                    mpEditEngine->QuickSetAttribs(rxFormat->maItemSet, rxFormat->maSelection);

                for (const auto& rxField : maFields)
                    mpEditEngine->QuickInsertField(SvxFieldItem(*rxField->mpData, EE_FEATURE_FIELD), rxField->maSelection);

                // This edit engine uses the SfxItemPool instance returned
                // from pDoc->GetEditPool() to create the text object, which
                // is a prerequisite for using this constructor of ScEditCell.
                rDoc.setEditCell(rCurrentPos, mpEditEngine->CreateTextObject());
            }
        }
        else if ( nCurrentCol > 0 && pOUText && !pOUText->isEmpty() )
        {
            rDoc.setStringCell(rCurrentPos, *pOUText);
        }
        else
            bDoIncrement = false;
    }

    // #i56027# This is about setting simple text, not edit cells,
    // so ProgressBarIncrement must be called with bEditCell = FALSE.
    // Formatted text that is put into the cell by the child context
    // is handled in AddCellsToTable() (bIsEmpty is true then).
    if (bDoIncrement)
        rXMLImport.ProgressBarIncrement();
}

void ScXMLTableRowCellContext::PutValueCell( const ScAddress& rCurrentPos )
{
    //matrix reference cells that contain value formula results;
    //cell was already put in document, just need to set value here.
    if( rXMLImport.GetTables().IsPartOfMatrix(rCurrentPos) )
    {
        ScRefCellValue aCell(*rXMLImport.GetDocument(), rCurrentPos);
        if (aCell.getType() == CELLTYPE_FORMULA)
        {
            ScFormulaCell* pFCell = aCell.getFormula();
            SetFormulaCell(pFCell);
            if (pFCell)
                pFCell->SetNeedNumberFormat( true );
        }
    }
    else  //regular value cell
    {
        // fdo#62250 absent values are not NaN, set to 0.0
        // PutValueCell() is called only for a known cell value type,
        // bIsEmpty==false in all these cases, no sense to check it here.
        if (!std::isfinite( fValue))
            fValue = 0.0;

        // #i62435# Initialize the value cell's script type if the default
        // style's number format is latin-only. If the cell uses a different
        // format, the script type will be reset when the style is applied.

        rXMLImport.GetDoc().setNumericCell(rCurrentPos, fValue);
    }
    rXMLImport.ProgressBarIncrement();
}

namespace {

bool isEmptyOrNote( const ScDocument* pDoc, const ScAddress& rCurrentPos )
{
    CellType eType = pDoc->GetCellType(rCurrentPos);
    return (eType == CELLTYPE_NONE);
}

}

void ScXMLTableRowCellContext::AddTextAndValueCell( const ScAddress& rCellPos,
        const ::std::optional< OUString >& pOUText, ScAddress& rCurrentPos )
{
    ScDocument* pDoc = rXMLImport.GetDocument();
    ScMyTables& rTables = rXMLImport.GetTables();
    bool bWasEmpty = bIsEmpty;
    for (SCCOL i = 0; i < nColsRepeated; ++i)
    {
        rCurrentPos.SetCol( rCellPos.Col() + i );

        // it makes no sense to import data after the last supported column
        // fdo#58539 & gnome#627150
        if(rCurrentPos.Col() > pDoc->MaxCol())
        {
            rXMLImport.SetRangeOverflowType(SCWARN_IMPORT_COLUMN_OVERFLOW);
            break;
        }

        if (i > 0)
            rTables.AddColumn(false);
        if (!bIsEmpty)
        {
            for (SCROW j = 0; j < nRepeatedRows; ++j)
            {
                rCurrentPos.SetRow( rCellPos.Row() + j );

                // it makes no sense to import data after last supported row
                // fdo#58539 & gnome#627150
                if(rCurrentPos.Row() > pDoc->MaxRow())
                {
                    rXMLImport.SetRangeOverflowType(SCWARN_IMPORT_ROW_OVERFLOW);
                    break;
                }

                if( (rCurrentPos.Col() == 0) && (j > 0) )
                {
                    rTables.AddRow();
                    rTables.AddColumn(false);
                }
                if( cellExists(*pDoc, rCurrentPos) )
                {
                    if(  !bIsCovered || isEmptyOrNote(pDoc, rCurrentPos)  )
                    {
                        switch (nCellType)
                        {
                            case util::NumberFormat::TEXT:
                            {
                                PutTextCell( rCurrentPos, i, pOUText );
                            }
                            break;
                            case util::NumberFormat::NUMBER:
                            case util::NumberFormat::PERCENT:
                            case util::NumberFormat::CURRENCY:
                            case util::NumberFormat::TIME:
                            case util::NumberFormat::DATETIME:
                            case util::NumberFormat::LOGICAL:
                            {
                                PutValueCell( rCurrentPos );
                            }
                            break;
                            default:
                            {
                                OSL_FAIL("no cell type given");
                            }
                            break;
                        }
                    }

                    SetAnnotation( rCurrentPos );
                    SetDetectiveObj( rCurrentPos );
                    SetCellRangeSource( rCurrentPos );
                }
                else
                {
                    if (!bWasEmpty || mxAnnotationData)
                    {
                        if (rCurrentPos.Row() > pDoc->MaxRow())
                            rXMLImport.SetRangeOverflowType(SCWARN_IMPORT_ROW_OVERFLOW);
                        else
                            rXMLImport.SetRangeOverflowType(SCWARN_IMPORT_COLUMN_OVERFLOW);
                    }
                }
            }
        }
        else
        {
            if ((i == 0) && (rCellPos.Col() == 0))
            {
                for (sal_Int32 j = 1; j < nRepeatedRows; ++j)
                {
                    rTables.AddRow();
                    rTables.AddColumn(false);
                }
            }
            // if nothing else useful can happen in the loop, just exit early
            if (i != 0 && bIsEmpty && rCurrentPos.Row() != 0)
            {
                rCurrentPos.SetCol( rCellPos.Col() + nColsRepeated - 1 );
                rTables.AddColumns(nColsRepeated - i - 1);
                break;
            }
        }
    }
}

bool ScXMLTableRowCellContext::CellsAreRepeated() const
{
    return ( (nColsRepeated > 1) || (nRepeatedRows > 1) );
}

namespace {

// from ScCellObj::GetOutputString_Imp().  all of it may not be necessary.
OUString getOutputString( ScDocument* pDoc, const ScAddress& aCellPos )
{
    if (!pDoc)
        return OUString();

    ScRefCellValue aCell(*pDoc, aCellPos);
    switch (aCell.getType())
    {
        case CELLTYPE_NONE:
            return OUString();
        case CELLTYPE_EDIT:
        {
            //  GetString on EditCell replaces linebreaks with spaces;
            //  however here we need line breaks
            const EditTextObject* pData = aCell.getEditText();
            EditEngine& rEngine = pDoc->GetEditEngine();
            rEngine.SetText(*pData);
            return rEngine.GetText();
            //  also don't format EditCells per NumberFormatter
        }
        break;
        default:
        {
            //  like in GetString for document (column)
            const Color* pColor;
            sal_uInt32 nNumFmt = pDoc->GetNumberFormat(ScRange(aCellPos));
            return ScCellFormat::GetString(aCell, nNumFmt, &pColor, nullptr, *pDoc);
        }
    }
}

}

void ScXMLTableRowCellContext::AddNonFormulaCell( const ScAddress& rCellPos )
{
    ::std::optional< OUString > pOUText;

    ScDocument* pDoc = rXMLImport.GetDocument();
    if( nCellType == util::NumberFormat::TEXT )
    {
        if( !bIsEmpty && !maStringValue && !mbEditEngineHasText && cellExists(*pDoc, rCellPos) && CellsAreRepeated() )
            pOUText = getOutputString(pDoc, rCellPos);

        if (!mbEditEngineHasText && !pOUText && !maStringValue)
            bIsEmpty = true;
    }

    ScAddress aCurrentPos( rCellPos );
    if( mxAnnotationData || pDetectiveObjVec || pCellRangeSource ) // has special content
        bIsEmpty = false;

    AddTextAndValueCell( rCellPos, pOUText, aCurrentPos );

    if( CellsAreRepeated() )
    {
        SCCOL nStartCol( std::min(rCellPos.Col(), pDoc->MaxCol()) );
        SCROW nStartRow( std::min(rCellPos.Row(), pDoc->MaxRow()) );
        SCCOL nEndCol( std::min<SCCOL>(rCellPos.Col() + nColsRepeated - 1, pDoc->MaxCol()) );
        SCROW nEndRow( std::min(rCellPos.Row() + nRepeatedRows - 1, pDoc->MaxRow()) );
        ScRange aScRange( nStartCol, nStartRow, rCellPos.Tab(), nEndCol, nEndRow, rCellPos.Tab() );
        SetContentValidation( aScRange );
        rXMLImport.GetStylesImportHelper()->AddRange( aScRange );
    }
    else if( cellExists(*pDoc, rCellPos) )
    {
        rXMLImport.GetStylesImportHelper()->AddCell(rCellPos);
        SetContentValidation( rCellPos );
    }
}

void ScXMLTableRowCellContext::PutFormulaCell( const ScAddress& rCellPos )
{
    ScDocument* pDoc = rXMLImport.GetDocument();
    if (!pDoc)
        return;

    ScDocumentImport& rDocImport = rXMLImport.GetDoc();

    const OUString & aText = maFormula->first;

    ScExternalRefManager::ApiGuard aExtRefGuard(*pDoc);

    if ( aText.isEmpty() )
        return;

    // temporary formula string as string tokens
    std::unique_ptr<ScTokenArray> pCode(new ScTokenArray(*pDoc));

    // Check the special case of a single error constant without leading
    // '=' and create an error formula cell without tokens.
    FormulaError nError = GetScImport().GetFormulaErrorConstant(aText);
    if (nError != FormulaError::NONE)
    {
        pCode->SetCodeError(nError);
    }
    else
    {
        // 5.2 and earlier wrote broken "Err:xxx" as formula to designate
        // an error formula cell.
        if (aText.startsWithIgnoreAsciiCase("Err:") && aText.getLength() <= 9 &&
                ((nError =
                  GetScImport().GetFormulaErrorConstant( OUString::Concat("#ERR") + aText.subView(4) + "!")) != FormulaError::NONE))
        {
            pCode->SetCodeError(nError);
        }
        else
        {
            OUString aFormulaNmsp = maFormula->second;
            if( eGrammar != formula::FormulaGrammar::GRAM_EXTERNAL )
                aFormulaNmsp.clear();
            pCode->AssignXMLString( aText, aFormulaNmsp );
            rDocImport.getDoc().IncXMLImportedFormulaCount( aText.getLength() );
        }
    }

    ScFormulaCell* pNewCell = new ScFormulaCell(*pDoc, rCellPos, std::move(pCode), eGrammar, ScMatrixMode::NONE);
    SetFormulaCell(pNewCell);
    rDocImport.setFormulaCell(rCellPos, pNewCell);
}

void ScXMLTableRowCellContext::AddFormulaCell( const ScAddress& rCellPos )
{
    ScDocument* pDoc = rXMLImport.GetDocument();
    if( cellExists(*pDoc, rCellPos) )
    {
        SetContentValidation( rCellPos );
        SAL_WARN_IF((nColsRepeated != 1) || (nRepeatedRows != 1), "sc", "repeated cells with formula not possible now");
        rXMLImport.GetStylesImportHelper()->AddCell(rCellPos);

        //add matrix
        if(bIsMatrix)
        {
            if (nMatrixCols > 0 && nMatrixRows > 0)
            {
                //matrix cells are put in the document, but we must set the
                //value/text of each matrix cell later
                rXMLImport.GetTables().AddMatrixRange(
                        rCellPos.Col(), rCellPos.Row(),
                        std::min<SCCOL>(rCellPos.Col() + nMatrixCols - 1, pDoc->MaxCol()),
                        std::min<SCROW>(rCellPos.Row() + nMatrixRows - 1, pDoc->MaxRow()),
                        maFormula->first, maFormula->second, eGrammar);

                // Set the value/text of the top-left matrix position in its
                // cached result.  For import, we only need to set the correct
                // matrix geometry and the value type of the top-left element.
                ScFormulaCell* pFCell = pDoc->GetFormulaCell(rCellPos);
                if (pFCell)
                {
                    ScMatrixRef pMat(new ScMatrix(nMatrixCols, nMatrixRows));
                    if (bFormulaTextResult && maStringValue)
                    {
                        if (!IsPossibleErrorString())
                        {
                            pFCell->SetResultMatrix(
                                    nMatrixCols, nMatrixRows, pMat, new formula::FormulaStringToken(
                                        pDoc->GetSharedStringPool().intern( *maStringValue)));
                            pFCell->ResetDirty();
                        }
                    }
                    else if (std::isfinite(fValue))
                    {
                        pFCell->SetResultMatrix(
                            nMatrixCols, nMatrixRows, pMat, new formula::FormulaDoubleToken(fValue));
                        pFCell->ResetDirty();
                    }
                }
            }
        }
        else
            PutFormulaCell( rCellPos );

        SetAnnotation( rCellPos );
        SetDetectiveObj( rCellPos );
        SetCellRangeSource( rCellPos );
        rXMLImport.ProgressBarIncrement();
    }
    else
    {
        if (rCellPos.Row() > pDoc->MaxRow())
            rXMLImport.SetRangeOverflowType(SCWARN_IMPORT_ROW_OVERFLOW);
        else
            rXMLImport.SetRangeOverflowType(SCWARN_IMPORT_COLUMN_OVERFLOW);
    }
}

//There are cases where a formula cell is exported with an office:value of 0 or
//no office:value at all, but the formula cell will have a text:p value which
//contains the intended formula result.
//These cases include when a formula result:
// - is blank
// - has a constant error value beginning with "#" (such as "#VALUE!" or "#N/A")
// - has an "Err:[###]" (where "[###]" is an error number)
// Libreoffice 4.1+ with ODF1.2 extended write however calcext:value-type="error" in that case
void ScXMLTableRowCellContext::HasSpecialCaseFormulaText()
{
    if (!mbEditEngineHasText)
        return;

    const OUString aStr = GetFirstParagraph();

    if (mbNewValueType)
    {
        if (aStr.isEmpty())
            mbPossibleEmptyDisplay = true;
        return;
    }

    if (aStr.isEmpty())
    {
        mbPossibleErrorCell = true;
        mbPossibleEmptyDisplay = true;
    }
    else if (aStr.startsWith("Err:"))
        mbPossibleErrorCell = true;
    else if (aStr.startsWith("#"))
        mbCheckWithCompilerForError = true;
}

bool ScXMLTableRowCellContext::IsPossibleErrorString() const
{
    if(mbNewValueType)
        return mbErrorValue;

    return mbPossibleErrorCell || (mbCheckWithCompilerForError && maStringValue &&
            GetScImport().GetFormulaErrorConstant(*maStringValue) != FormulaError::NONE);
}

void SAL_CALL ScXMLTableRowCellContext::endFastElement(sal_Int32 /*nElement*/)
{
    HasSpecialCaseFormulaText();
    if( bFormulaTextResult && (mbPossibleErrorCell || mbCheckWithCompilerForError) )
    {
        maStringValue = GetFirstParagraph();
    }

    ScAddress aCellPos = rXMLImport.GetTables().GetCurrentCellPos();
    if( aCellPos.Col() > 0 && nRepeatedRows > 1 )
        aCellPos.SetRow( aCellPos.Row() - (nRepeatedRows - 1) );
    if( bIsMerged )
        DoMerge( aCellPos, nMergedCols - 1, nMergedRows - 1 );

    if (maFormula)
        AddFormulaCell(aCellPos);
    else
        AddNonFormulaCell(aCellPos);

    //if LockSolarMutex got used, we presumably need to ensure an UnlockSolarMutex
    if (bSolarMutexLocked)
    {
        GetScImport().UnlockSolarMutex();
        bSolarMutexLocked = false;
    }

    bIsMerged = false;
    nMergedCols = 1;
    nMergedRows = 1;
    nColsRepeated = 1;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
