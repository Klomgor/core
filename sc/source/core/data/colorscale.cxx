/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <memory>
#include <colorscale.hxx>
#include <document.hxx>
#include <formulacell.hxx>
#include <fillinfo.hxx>
#include <bitmaps.hlst>
#include <tokenarray.hxx>
#include <refupdatecontext.hxx>
#include <refdata.hxx>
#include <editeng/fhgtitem.hxx>
#include <scitems.hxx>

#include <formula/token.hxx>
#include <vcl/bitmapex.hxx>

#include <algorithm>
#include <cassert>

ScFormulaListener::ScFormulaListener(ScFormulaCell* pCell):
    mbDirty(false),
    mrDoc(pCell->GetDocument())
{
    startListening( pCell->GetCode(), ScRange(pCell->aPos) );
}

ScFormulaListener::ScFormulaListener(ScDocument& rDoc):
    mbDirty(false),
    mrDoc(rDoc)
{
}

ScFormulaListener::ScFormulaListener(ScDocument& rDoc, const ScRangeList& rRange):
    mbDirty(false),
    mrDoc(rDoc)
{
    startListening(rRange);
}

void ScFormulaListener::startListening(const ScTokenArray* pArr, const ScRange& rRange)
{
    if (!pArr || mrDoc.IsClipOrUndo())
        return;

    for ( auto t: pArr->References() )
    {
        switch (t->GetType())
        {
            case formula::svSingleRef:
            {
                ScAddress aCell = t->GetSingleRef()->toAbs(mrDoc, rRange.aStart);
                ScAddress aCell2 = t->GetSingleRef()->toAbs(mrDoc, rRange.aEnd);
                ScRange aRange(aCell, aCell2);
                if (aRange.IsValid())
                    mrDoc.StartListeningArea(aRange, false, this);
            }
            break;
            case formula::svDoubleRef:
            {
                const ScSingleRefData& rRef1 = *t->GetSingleRef();
                const ScSingleRefData& rRef2 = *t->GetSingleRef2();
                ScAddress aCell1 = rRef1.toAbs(mrDoc, rRange.aStart);
                ScAddress aCell2 = rRef2.toAbs(mrDoc, rRange.aStart);
                ScAddress aCell3 = rRef1.toAbs(mrDoc, rRange.aEnd);
                ScAddress aCell4 = rRef2.toAbs(mrDoc, rRange.aEnd);
                ScRange aRange1(aCell1, aCell3);
                ScRange aRange2(aCell2, aCell4);
                aRange1.ExtendTo(aRange2);
                if (aRange1.IsValid())
                {
                    if (t->GetOpCode() == ocColRowNameAuto)
                    {   // automagically
                        if ( rRef1.IsColRel() )
                        {   // ColName
                            aRange1.aEnd.SetRow(mrDoc.MaxRow());
                        }
                        else
                        {   // RowName
                            aRange1.aEnd.SetCol(mrDoc.MaxCol());
                        }
                    }
                    mrDoc.StartListeningArea(aRange1, false, this);
                }
            }
            break;
            default:
                ;   // nothing
        }
    }
}

void ScFormulaListener::startListening(const ScRangeList& rRange)
{
    if (mrDoc.IsClipOrUndo())
        return;

    size_t nLength = rRange.size();
    for (size_t i = 0; i < nLength; ++i)
    {
        const ScRange& aRange = rRange[i];
        mrDoc.StartListeningArea(aRange, false, this);
    }
}

void ScFormulaListener::addTokenArray(const ScTokenArray* pArray, const ScRange& rRange)
{
    startListening(pArray, rRange);
}

void ScFormulaListener::setCallback(const std::function<void()>& aCallback)
{
    maCallbackFunction = aCallback;
}

void ScFormulaListener::stopListening()
{
    if (mrDoc.IsClipOrUndo())
        return;

    EndListeningAll();
}

ScFormulaListener::~ScFormulaListener()
{
    stopListening();
}

void ScFormulaListener::Notify(const SfxHint& rHint)
{
    mbDirty = true;

    if (rHint.GetId() == SfxHintId::Dying)
        return;

    if (maCallbackFunction)
        maCallbackFunction();
}

bool ScFormulaListener::NeedsRepaint() const
{
    bool bRet = mbDirty;
    mbDirty = false;
    return bRet;
}

ScColorScaleEntry::ScColorScaleEntry():
    mnVal(0),
    mpFormat(nullptr),
    meType(COLORSCALE_VALUE),
    meMode(ScConditionMode::EqGreater)
{
}

ScColorScaleEntry::ScColorScaleEntry(double nVal, const Color& rCol, ScColorScaleEntryType eType,
                                        ScConditionMode eMode):
    mnVal(nVal),
    mpFormat(nullptr),
    maColor(rCol),
    meType(eType),
    meMode(eMode)
{
}

ScColorScaleEntry::ScColorScaleEntry(const ScColorScaleEntry& rEntry):
    mnVal(rEntry.mnVal),
    mpFormat(rEntry.mpFormat),
    maColor(rEntry.maColor),
    meType(rEntry.meType),
    meMode(rEntry.meMode)
{
    setListener();
    if(rEntry.mpCell)
    {
        mpCell.reset(new ScFormulaCell(*rEntry.mpCell, rEntry.mpCell->GetDocument(), rEntry.mpCell->aPos, ScCloneFlags::NoMakeAbsExternal));
        mpCell->StartListeningTo(mpCell->GetDocument());
        mpListener.reset(new ScFormulaListener(mpCell.get()));
    }
}

ScColorScaleEntry::ScColorScaleEntry(ScDocument& rDoc, const ScColorScaleEntry& rEntry):
    mnVal(rEntry.mnVal),
    mpFormat(rEntry.mpFormat),
    maColor(rEntry.maColor),
    meType(rEntry.meType),
    meMode(rEntry.meMode)
{
    setListener();
    if(rEntry.mpCell)
    {
        mpCell.reset(new ScFormulaCell(*rEntry.mpCell, rDoc, rEntry.mpCell->aPos, ScCloneFlags::NoMakeAbsExternal));
        mpCell->StartListeningTo( rDoc );
        mpListener.reset(new ScFormulaListener(mpCell.get()));
        if (mpFormat)
            mpListener->setCallback([&]() { mpFormat->DoRepaint();});
    }
}

ScColorScaleEntry::~ScColorScaleEntry() COVERITY_NOEXCEPT_FALSE
{
    if(mpCell)
        mpCell->EndListeningTo(mpCell->GetDocument());
}

void ScColorScaleEntry::SetFormula( const OUString& rFormula, ScDocument& rDoc, const ScAddress& rAddr, formula::FormulaGrammar::Grammar eGrammar )
{
    mpCell.reset(new ScFormulaCell( rDoc, rAddr, rFormula, eGrammar ));
    mpCell->StartListeningTo( rDoc );
    mpListener.reset(new ScFormulaListener(mpCell.get()));
    if (mpFormat)
        mpListener->setCallback([&]() { mpFormat->DoRepaint();});
}

const ScTokenArray* ScColorScaleEntry::GetFormula() const
{
    if(mpCell)
    {
        return mpCell->GetCode();
    }

    return nullptr;
}

OUString ScColorScaleEntry::GetFormula( formula::FormulaGrammar::Grammar eGrammar ) const
{
    if(mpCell)
    {
        return mpCell->GetFormula(eGrammar);
    }

    return OUString();
}

double ScColorScaleEntry::GetValue() const
{
    if(mpCell)
    {
        mpCell->Interpret();
        if(mpCell->IsValue())
            return mpCell->GetValue();

        return std::numeric_limits<double>::max();
    }

    return mnVal;
}

ScConditionMode ScColorScaleEntry::GetMode() const
{
    return meMode;
}

void ScColorScaleEntry::SetMode(ScConditionMode eMode)
{
    meMode = eMode;
}

void ScColorScaleEntry::SetValue(double nValue)
{
    mnVal = nValue;
    mpCell.reset();
    setListener();
}

void ScColorScaleEntry::UpdateReference( const sc::RefUpdateContext& rCxt )
{
    if (!mpCell)
    {
        setListener();
        return;
    }

    mpCell->UpdateReference(rCxt);
    mpListener.reset(new ScFormulaListener(mpCell.get()));
    SetRepaintCallback(mpFormat);
}

void ScColorScaleEntry::UpdateInsertTab( const sc::RefUpdateInsertTabContext& rCxt )
{
    if (!mpCell)
    {
        setListener();
        return;
    }

    mpCell->UpdateInsertTab(rCxt);
    mpListener.reset(new ScFormulaListener(mpCell.get()));
    SetRepaintCallback(mpFormat);
}

void ScColorScaleEntry::UpdateDeleteTab( const sc::RefUpdateDeleteTabContext& rCxt )
{
    if (!mpCell)
    {
        setListener();
        return;
    }

    mpCell->UpdateDeleteTab(rCxt);
    mpListener.reset(new ScFormulaListener(mpCell.get()));
    SetRepaintCallback(mpFormat);
}

void ScColorScaleEntry::UpdateMoveTab( const sc::RefUpdateMoveTabContext& rCxt )
{
    if (!mpCell)
    {
        setListener();
        return;
    }

    SCTAB nTabNo = rCxt.getNewTab(mpCell->aPos.Tab());
    mpCell->UpdateMoveTab(rCxt, nTabNo);
    mpListener.reset(new ScFormulaListener(mpCell.get()));
    SetRepaintCallback(mpFormat);
}

void ScColorScaleEntry::SetColor(const Color& rColor)
{
    maColor = rColor;
}

void ScColorScaleEntry::SetRepaintCallback(ScConditionalFormat* pFormat)
{
    mpFormat = pFormat;
    setListener();
    if (mpFormat && mpListener)
        mpListener->setCallback([&]() { mpFormat->DoRepaint();});
}

void ScColorScaleEntry::SetType( ScColorScaleEntryType eType )
{
    meType = eType;
    if(eType != COLORSCALE_FORMULA)
    {
        mpCell.reset();
        mpListener.reset();
    }

    setListener();
}

void ScColorScaleEntry::setListener()
{
    if (!mpFormat)
        return;

    if (meType == COLORSCALE_PERCENT || meType == COLORSCALE_PERCENTILE
            || meType == COLORSCALE_MIN || meType == COLORSCALE_MAX
            || meType == COLORSCALE_AUTO)
    {
        mpListener.reset(new ScFormulaListener(mpFormat->GetDocument(), mpFormat->GetRange()));
        mpListener->setCallback([&]() { mpFormat->DoRepaint();});
    }
}

void ScColorScaleEntry::SetRepaintCallback(const std::function<void()>& func)
{
    mpListener->setCallback(func);
}

ScColorFormat::ScColorFormat(ScDocument& rDoc)
    : ScFormatEntry(rDoc)
    , mpParent(nullptr)
{
}

ScColorFormat::~ScColorFormat()
{
}

void ScColorFormat::SetParent( ScConditionalFormat* pParent )
{
    mpParent = pParent;
}

ScColorScaleFormat::ScColorScaleFormat(ScDocument& rDoc):
    ScColorFormat(rDoc)
{
}

ScColorScaleFormat::ScColorScaleFormat(ScDocument& rDoc, const ScColorScaleFormat& rFormat):
    ScColorFormat(rDoc)
{
    for(const auto& rxEntry : rFormat)
    {
        maColorScales.emplace_back(new ScColorScaleEntry(rDoc, *rxEntry));
    }

    auto aCache = rFormat.GetCache();
    SetCache(aCache);
}

ScColorFormat* ScColorScaleFormat::Clone(ScDocument& rDoc) const
{
    return new ScColorScaleFormat(rDoc, *this);
}

ScColorScaleFormat::~ScColorScaleFormat()
{
}

void ScColorScaleFormat::SetParent(ScConditionalFormat* pFormat)
{
    for (auto itr = begin(), itrEnd = end(); itr != itrEnd; ++itr)
    {
        (*itr)->SetRepaintCallback(pFormat);
    }
    ScColorFormat::SetParent(pFormat);
}

void ScColorScaleFormat::AddEntry( ScColorScaleEntry* pEntry )
{
    maColorScales.push_back(std::unique_ptr<ScColorScaleEntry, o3tl::default_delete<ScColorScaleEntry>>(pEntry));
    maColorScales.back()->SetRepaintCallback(mpParent);
}

bool ScColorScaleFormat::IsEqual(const ScFormatEntry& rOther, bool /*bIgnoreSrcPos*/) const
{
    if (GetType() != rOther.GetType())
        return false;

    const ScColorScaleFormat& r = static_cast<const ScColorScaleFormat&>(rOther);

    for (size_t i = 0; i < maColorScales.size(); ++i)
    {
        if (!maColorScales[i]->GetColor().IsRGBEqual(r.maColorScales[i]->GetColor().GetRGBColor())
            || maColorScales[i]->GetType() != r.maColorScales[i]->GetType()
            || maColorScales[i]->GetValue() != r.maColorScales[i]->GetValue())
            return false;
    }

    return true;
}

double ScColorScaleFormat::GetMinValue() const
{
    ScColorScaleEntries::const_iterator itr = maColorScales.begin();

    if((*itr)->GetType() == COLORSCALE_VALUE || (*itr)->GetType() == COLORSCALE_FORMULA)
        return (*itr)->GetValue();
    else
    {
        return getMinValue();
    }
}

double ScColorScaleFormat::GetMaxValue() const
{
    ScColorScaleEntries::const_reverse_iterator itr = maColorScales.rbegin();

    if((*itr)->GetType() == COLORSCALE_VALUE || (*itr)->GetType() == COLORSCALE_FORMULA)
        return (*itr)->GetValue();
    else
    {
        return getMaxValue();
    }
}

void ScColorScaleFormat::calcMinMax(double& rMin, double& rMax) const
{
    rMin = GetMinValue();
    rMax = GetMaxValue();
}

const ScRangeList& ScColorFormat::GetRange() const
{
    return mpParent->GetRange();
}

std::vector<double> ScColorFormat::GetCache() const
{
    if (!mpParent)
        return {};

    std::vector<double>* pRes = mpParent->GetCache();
    if (pRes)
        return *pRes;

    return {};
}

void ScColorFormat::SetCache(const std::vector<double>& aValues) const
{
    if (!mpParent)
        return;

    mpParent->SetCache(aValues);
}

std::vector<double>& ScColorFormat::getValues() const
{
    assert(mpParent);

    std::vector<double>* pCache = mpParent->GetCache();
    if (!pCache || pCache->empty())
    {
        if (!pCache)
        {
            SetCache({});
            pCache = mpParent->GetCache();
            assert(pCache);
        }

        std::vector<double>& rValues = *pCache;

        size_t n = GetRange().size();
        const ScRangeList& aRanges = GetRange();
        for(size_t i = 0; i < n; ++i)
        {
            const ScRange & rRange = aRanges[i];
            SCTAB nTab = rRange.aStart.Tab();

            SCCOL nColStart = rRange.aStart.Col();
            SCROW nRowStart = rRange.aStart.Row();
            SCCOL nColEnd = rRange.aEnd.Col();
            SCROW nRowEnd = rRange.aEnd.Row();

            if(nRowEnd == mrDoc.MaxRow())
            {
                bool bShrunk = false;
                mrDoc.ShrinkToUsedDataArea(bShrunk, nTab, nColStart, nRowStart,
                        nColEnd, nRowEnd, false);
            }
            for(SCCOL nCol = nColStart; nCol <= nColEnd; ++nCol)
            {
                for(SCROW nRow = nRowStart; nRow <= nRowEnd; ++nRow)
                {
                    ScAddress aAddr(nCol, nRow, nTab);
                    ScRefCellValue rCell(mrDoc, aAddr);
                    if(rCell.hasNumeric())
                    {
                        double aVal = rCell.getValue();
                        rValues.push_back(aVal);
                    }
                }
            }
        }

        std::sort(rValues.begin(), rValues.end());
    }

    return *pCache;
}

double ScColorFormat::getMinValue() const
{
    std::vector<double>& rValues = getValues();
    if(rValues.empty())
        return 0;
    return rValues[0];
}

double ScColorFormat::getMaxValue() const
{
    std::vector<double>& rValues = getValues();
    if(rValues.empty())
        return 0;
    return rValues[rValues.size()-1];
}

void ScColorFormat::startRendering()
{
}

void ScColorFormat::endRendering()
{
}

void ScColorFormat::updateValues()
{
    getMinValue();
    getMaxValue();
}

namespace {

sal_uInt8 GetColorValue( double nVal, double nVal1, sal_uInt8 nColVal1, double nVal2, sal_uInt8 nColVal2 )
{
    if (nVal <= nVal1)
        return nColVal1;

    if (nVal >= nVal2)
        return nColVal2;

    sal_uInt8 nColVal = static_cast<int>((nVal - nVal1)/(nVal2-nVal1)*(nColVal2-nColVal1))+nColVal1;
    return nColVal;
}

Color CalcColor( double nVal, double nVal1, const Color& rCol1, double nVal2, const Color& rCol2)
{
    sal_uInt8 nColRed = GetColorValue(nVal, nVal1, rCol1.GetRed(), nVal2, rCol2.GetRed());
    sal_uInt8 nColBlue = GetColorValue(nVal, nVal1, rCol1.GetBlue(), nVal2, rCol2.GetBlue());
    sal_uInt8 nColGreen = GetColorValue(nVal, nVal1, rCol1.GetGreen(), nVal2, rCol2.GetGreen());

    return Color(nColRed, nColGreen, nColBlue);
}

/**
 * @param rVector sorted vector of the array
 * @param fPercentile percentile
 */
double GetPercentile( const std::vector<double>& rArray, double fPercentile )
{
    assert(!rArray.empty());
    SAL_WARN_IF(fPercentile < 0, "sc", "negative percentile");
    if (fPercentile < 0)
        return rArray.front();
    assert(fPercentile <= 1);
    size_t nSize = rArray.size();
    double fFloor = ::rtl::math::approxFloor(fPercentile * (nSize-1));
    size_t nIndex = static_cast<size_t>(fFloor);
    double fDiff = fPercentile * (nSize-1) - fFloor;
    std::vector<double>::const_iterator iter = rArray.begin() + nIndex;
    if (fDiff == 0.0)
        return *iter;
    else
    {
        double fVal = *iter;
        iter = rArray.begin() + nIndex+1;
        return fVal + fDiff * (*iter - fVal);
    }
}

}

double ScColorScaleFormat::CalcValue(double nMin, double nMax, const ScColorScaleEntries::const_iterator& itr) const
{
    switch((*itr)->GetType())
    {
        case COLORSCALE_PERCENT:
            return nMin + (nMax-nMin)*((*itr)->GetValue()/100);
        case COLORSCALE_MIN:
            return nMin;
        case COLORSCALE_MAX:
            return nMax;
        case COLORSCALE_PERCENTILE:
        {
            std::vector<double>& rValues = getValues();
            if(rValues.size() == 1)
                return rValues[0];
            else
            {
                double fPercentile = (*itr)->GetValue()/100.0;
                return GetPercentile(rValues, fPercentile);
            }
        }

        default:
        break;
    }

    return (*itr)->GetValue();
}

std::optional<Color> ScColorScaleFormat::GetColor( const ScAddress& rAddr ) const
{
    ScRefCellValue rCell(mrDoc, rAddr);
    if(!rCell.hasNumeric())
        return std::optional<Color>();

    // now we have for sure a value
    double nVal = rCell.getValue();

    if (maColorScales.size() < 2)
        return std::optional<Color>();

    double nMin = std::numeric_limits<double>::max();
    double nMax = std::numeric_limits<double>::min();
    calcMinMax(nMin, nMax);

    // this check is for safety
    if(nMin > nMax)
        return std::optional<Color>();

    ScColorScaleEntries::const_iterator itr = begin();
    double nValMin = CalcValue(nMin, nMax, itr);
    Color rColMin = (*itr)->GetColor();
    ++itr;
    double nValMax = CalcValue(nMin, nMax, itr);
    Color rColMax = (*itr)->GetColor();

    // tdf#155321 for the last percentile value, use always the end of the color scale,
    // i.e. not the first possible color in the case of repeating values
    bool bEqual = COLORSCALE_PERCENTILE == (*itr)->GetType() && nVal == nMax && nVal == nValMax;

    ++itr;
    while(itr != end() && (nVal > nValMax || bEqual))
    {
        rColMin = rColMax;
        nValMin = !bEqual ? nValMax : nValMax - 1;
        rColMax = (*itr)->GetColor();
        nValMax = CalcValue(nMin, nMax, itr);
        ++itr;
    }

    Color aColor = CalcColor(nVal, nValMin, rColMin, nValMax, rColMax);

    return aColor;
}

void ScColorScaleFormat::UpdateReference( sc::RefUpdateContext& rCxt )
{
    for(ScColorScaleEntries::iterator itr = begin(); itr != end(); ++itr)
        (*itr)->UpdateReference(rCxt);
}

void ScColorScaleFormat::UpdateInsertTab( sc::RefUpdateInsertTabContext& rCxt )
{
    for (ScColorScaleEntries::iterator it = begin(); it != end(); ++it)
        (*it)->UpdateInsertTab(rCxt);
}

void ScColorScaleFormat::UpdateDeleteTab( sc::RefUpdateDeleteTabContext& rCxt )
{
    for (ScColorScaleEntries::iterator it = begin(); it != end(); ++it)
        (*it)->UpdateDeleteTab(rCxt);
}

void ScColorScaleFormat::UpdateMoveTab( sc::RefUpdateMoveTabContext& rCxt )
{
    for (ScColorScaleEntries::iterator it = begin(); it != end(); ++it)
        (*it)->UpdateMoveTab(rCxt);
}

ScFormatEntry::Type ScColorScaleFormat::GetType() const
{
    return Type::Colorscale;
}

ScColorScaleEntries::iterator ScColorScaleFormat::begin()
{
    return maColorScales.begin();
}

ScColorScaleEntries::const_iterator ScColorScaleFormat::begin() const
{
    return maColorScales.begin();
}

ScColorScaleEntries::iterator ScColorScaleFormat::end()
{
    return maColorScales.end();
}

ScColorScaleEntries::const_iterator ScColorScaleFormat::end() const
{
    return maColorScales.end();
}

ScColorScaleEntry* ScColorScaleFormat::GetEntry(size_t nPos)
{
    if (maColorScales.size() <= nPos)
        return nullptr;

    return maColorScales[nPos].get();
}

const ScColorScaleEntry* ScColorScaleFormat::GetEntry(size_t nPos) const
{
    if (maColorScales.size() <= nPos)
        return nullptr;

    return maColorScales[nPos].get();
}

size_t ScColorScaleFormat::size() const
{
    return maColorScales.size();
}

void ScColorScaleFormat::EnsureSize()
{
    if (maColorScales.size() < 2)
    {
        // TODO: create 2 valid entries
    }
}

ScDataBarFormat::ScDataBarFormat(ScDocument& rDoc):
    ScColorFormat(rDoc),
    mpFormatData(new ScDataBarFormatData())
{
}

ScDataBarFormat::ScDataBarFormat(ScDocument& rDoc, const ScDataBarFormat& rFormat):
    ScColorFormat(rDoc),
    mpFormatData(new ScDataBarFormatData(*rFormat.mpFormatData))
{
}

void ScDataBarFormat::SetDataBarData( ScDataBarFormatData* pData )
{
    mpFormatData.reset(pData);
    if (mpParent)
    {
        mpFormatData->mpUpperLimit->SetRepaintCallback(mpParent);
        mpFormatData->mpLowerLimit->SetRepaintCallback(mpParent);
    }
}

ScDataBarFormatData* ScDataBarFormat::GetDataBarData()
{
    return mpFormatData.get();
}

const ScDataBarFormatData* ScDataBarFormat::GetDataBarData() const
{
    return mpFormatData.get();
}

ScColorFormat* ScDataBarFormat::Clone(ScDocument& rDoc) const
{
    return new ScDataBarFormat(rDoc, *this);
}

void ScDataBarFormat::SetParent(ScConditionalFormat* pFormat)
{
    if (mpFormatData)
    {
        mpFormatData->mpUpperLimit->SetRepaintCallback(pFormat);
        mpFormatData->mpLowerLimit->SetRepaintCallback(pFormat);
    }
    ScColorFormat::SetParent(pFormat);
}

ScFormatEntry::Type ScDataBarFormat::GetType() const
{
    return Type::Databar;
}

bool ScDataBarFormat::IsEqual(const ScFormatEntry& rOther, bool /*bIgnoreSrcPos*/) const
{
    if (GetType() != rOther.GetType())
        return false;

    const ScDataBarFormat& r = static_cast<const ScDataBarFormat&>(rOther);

    bool bEq = (mpFormatData->maAxisColor.IsRGBEqual(r.mpFormatData->maAxisColor)
                && mpFormatData->maPositiveColor.IsRGBEqual(r.mpFormatData->maPositiveColor)
                && mpFormatData->mxNegativeColor == r.mpFormatData->mxNegativeColor
                && mpFormatData->meAxisPosition == r.mpFormatData->meAxisPosition
                && mpFormatData->mbGradient == r.mpFormatData->mbGradient
                && mpFormatData->mbOnlyBar == r.mpFormatData->mbOnlyBar);

    if (mpFormatData->mpUpperLimit->GetType() == r.mpFormatData->mpUpperLimit->GetType()
        && bEq)
    {
        bEq = (mpFormatData->mpUpperLimit->GetColor().IsRGBEqual(
                   r.mpFormatData->mpUpperLimit->GetColor())
               && mpFormatData->mpUpperLimit->GetValue()
                      == r.mpFormatData->mpUpperLimit->GetValue());
    }

    if (mpFormatData->mpLowerLimit->GetType() == r.mpFormatData->mpLowerLimit->GetType()
        && bEq)
    {
        bEq = (mpFormatData->mpLowerLimit->GetColor().IsRGBEqual(
                   r.mpFormatData->mpLowerLimit->GetColor())
               && mpFormatData->mpLowerLimit->GetValue()
                      == r.mpFormatData->mpLowerLimit->GetValue());
    }

    return bEq;
}

void ScDataBarFormat::UpdateReference( sc::RefUpdateContext& rCxt )
{
    mpFormatData->mpUpperLimit->UpdateReference(rCxt);
    mpFormatData->mpLowerLimit->UpdateReference(rCxt);
}

void ScDataBarFormat::UpdateInsertTab( sc::RefUpdateInsertTabContext& rCxt )
{
    mpFormatData->mpUpperLimit->UpdateInsertTab(rCxt);
    mpFormatData->mpLowerLimit->UpdateInsertTab(rCxt);
}

void ScDataBarFormat::UpdateDeleteTab( sc::RefUpdateDeleteTabContext& rCxt )
{
    mpFormatData->mpUpperLimit->UpdateDeleteTab(rCxt);
    mpFormatData->mpLowerLimit->UpdateDeleteTab(rCxt);
}

void ScDataBarFormat::UpdateMoveTab( sc::RefUpdateMoveTabContext& rCxt )
{
    mpFormatData->mpUpperLimit->UpdateMoveTab(rCxt);
    mpFormatData->mpLowerLimit->UpdateMoveTab(rCxt);
}

double ScDataBarFormat::getMin(double nMin, double nMax) const
{
    switch(mpFormatData->mpLowerLimit->GetType())
    {
        case COLORSCALE_MIN:
            return nMin;

        case COLORSCALE_AUTO:
            return std::min<double>(0, nMin);

        case COLORSCALE_PERCENT:
            return nMin + (nMax-nMin)/100*mpFormatData->mpLowerLimit->GetValue();

        case COLORSCALE_PERCENTILE:
        {
            double fPercentile = mpFormatData->mpLowerLimit->GetValue()/100.0;
            std::vector<double>& rValues = getValues();
            return GetPercentile(rValues, fPercentile);
        }

        default:
        break;
    }

    return mpFormatData->mpLowerLimit->GetValue();
}

double ScDataBarFormat::getMax(double nMin, double nMax) const
{
    switch(mpFormatData->mpUpperLimit->GetType())
    {
        case COLORSCALE_MAX:
            return nMax;
        case COLORSCALE_AUTO:
            return std::max<double>(0, nMax);
        case COLORSCALE_PERCENT:
            return nMin + (nMax-nMin)/100*mpFormatData->mpUpperLimit->GetValue();
        case COLORSCALE_PERCENTILE:
        {
            double fPercentile = mpFormatData->mpUpperLimit->GetValue()/100.0;
            std::vector<double>& rValues = getValues();
            return GetPercentile(rValues, fPercentile);
        }

        default:
            break;
    }

    return mpFormatData->mpUpperLimit->GetValue();
}

std::unique_ptr<ScDataBarInfo> ScDataBarFormat::GetDataBarInfo(const ScAddress& rAddr) const
{
    ScRefCellValue rCell(mrDoc, rAddr);
    if(!rCell.hasNumeric())
        return nullptr;

    // now we have for sure a value

    double nValMin = getMinValue();
    double nValMax = getMaxValue();
    double nMin = getMin(nValMin, nValMax);
    double nMax = getMax(nValMin, nValMax);
    double nMinLength = mpFormatData->mnMinLength;
    double nMaxLength = mpFormatData->mnMaxLength;

    double nValue = rCell.getValue();

    std::unique_ptr<ScDataBarInfo> pInfo(new ScDataBarInfo);
    if(mpFormatData->meAxisPosition == databar::NONE)
    {
        if(nValue <= nMin)
        {
            pInfo->mnLength = nMinLength;
        }
        else if(nValue >= nMax)
        {
            pInfo->mnLength = nMaxLength;
        }
        else
        {
            double nDiff = nMax - nMin;
            pInfo->mnLength = nMinLength + (nValue - nMin)/nDiff * (nMaxLength-nMinLength);
        }
        pInfo->mnZero = 0;
    }
    else if (mpFormatData->meAxisPosition == databar::AUTOMATIC)
    {
        // if auto is used we may need to adjust it
        // for the length calculation
        if (mpFormatData->mpLowerLimit->GetType() == COLORSCALE_AUTO && nMin > 0)
            nMin = 0;
        if (mpFormatData->mpUpperLimit->GetType() == COLORSCALE_MAX && nMax < 0)
            nMax = 0;

        //calculate the zero position first
        if(nMin < 0)
        {
            if(nMax < 0)
                pInfo->mnZero = 100;
            else
            {
                pInfo->mnZero = -100*nMin/(nMax-nMin);
            }
        }
        else
            pInfo->mnZero = 0;

        double nMinNonNegative = std::max(0.0, nMin);
        double nMaxNonPositive = std::min(0.0, nMax);
        //calculate the length
        if(nValue < 0 && nMin < 0)
        {
            if (nValue < nMin)
                pInfo->mnLength = -100;
            else
                pInfo->mnLength = -100 * (nValue-nMaxNonPositive)/(nMin-nMaxNonPositive);
        }
        else
        {
            if ( nValue > nMax )
                pInfo->mnLength = 100;
            else if (nValue <= nMin)
                pInfo->mnLength = 0;
            else
                pInfo->mnLength = 100 * (nValue-nMinNonNegative)/(nMax-nMinNonNegative);
        }
    }
    else if( mpFormatData->meAxisPosition == databar::MIDDLE)
    {
        pInfo->mnZero = 50;
        double nAbsMax = std::max(std::abs(nMin), std::abs(nMax));
        if (nValue < 0 && nMin < 0)
        {
            if (nValue < nMin)
                pInfo->mnLength = nMaxLength * (nMin/nAbsMax);
            else
                pInfo->mnLength = nMaxLength * (nValue/nAbsMax);
        }
        else
        {
            if (nValue > nMax)
                pInfo->mnLength = nMaxLength * (nMax/nAbsMax);
            else
                pInfo->mnLength = nMaxLength * (std::max(nValue, nMin)/nAbsMax);
        }
    }
    else
        assert(false);

    // set color
    if(mpFormatData->mbNeg && nValue < 0)
    {
        if(mpFormatData->mxNegativeColor)
        {
            pInfo->maColor = *mpFormatData->mxNegativeColor;
        }
        else
        {
            // default negative color is red
            pInfo->maColor = COL_LIGHTRED;
        }

    }
    else
        pInfo->maColor = mpFormatData->maPositiveColor;

    pInfo->mbGradient = mpFormatData->mbGradient;
    pInfo->mbShowValue = !mpFormatData->mbOnlyBar;
    pInfo->maAxisColor = mpFormatData->maAxisColor;

    return pInfo;
}

void ScDataBarFormat::EnsureSize()
{
    if (!mpFormatData->mpLowerLimit)
    {
        // TODO: implement
    }
    if (!mpFormatData->mpUpperLimit)
    {
        // TODO: implement
    }
}

static bool Compare(double nVal1, double nVal2, const ScIconSetFormat::const_iterator& itr)
{
    switch ((*itr)->GetMode())
    {
        case ScConditionMode::Equal:
            return nVal1 == nVal2;
        case ScConditionMode::Less:
            return nVal1 < nVal2;
        case ScConditionMode::Greater:
            return nVal1 > nVal2;
        case ScConditionMode::EqLess:
            return nVal1 <= nVal2;
        case ScConditionMode::EqGreater:
            return nVal1 >= nVal2;
        case ScConditionMode::NotEqual:
            return nVal1 != nVal2;
        default:
            break;
    }
    return false;
}

ScIconSetFormatData::ScIconSetFormatData(ScIconSetFormatData const& rOther)
    : eIconSetType(rOther.eIconSetType)
    , mbShowValue(rOther.mbShowValue)
    , mbReverse(rOther.mbReverse)
    , mbCustom(rOther.mbCustom)
    , maCustomVector(rOther.maCustomVector)
{
    m_Entries.reserve(rOther.m_Entries.size());
    for (auto const& it : rOther.m_Entries)
    {
        m_Entries.emplace_back(new ScColorScaleEntry(*it));
    }
}

ScIconSetFormat::ScIconSetFormat(ScDocument& rDoc):
    ScColorFormat(rDoc),
    mpFormatData(new ScIconSetFormatData)
{
}

ScIconSetFormat::ScIconSetFormat(ScDocument& rDoc, const ScIconSetFormat& rFormat):
    ScColorFormat(rDoc),
    mpFormatData(new ScIconSetFormatData(*rFormat.mpFormatData))
{
}

ScColorFormat* ScIconSetFormat::Clone( ScDocument& rDoc ) const
{
    return new ScIconSetFormat(rDoc, *this);
}

void ScIconSetFormat::SetParent(ScConditionalFormat* pFormat)
{
    for(iterator itr = begin(); itr != end(); ++itr)
    {
        (*itr)->SetRepaintCallback(pFormat);
    }
    ScColorFormat::SetParent(pFormat);
}

void ScIconSetFormat::SetIconSetData( ScIconSetFormatData* pFormatData )
{
    mpFormatData.reset( pFormatData );
    SetParent(mpParent);
}

ScIconSetFormatData* ScIconSetFormat::GetIconSetData()
{
    return mpFormatData.get();
}

const ScIconSetFormatData* ScIconSetFormat::GetIconSetData() const
{
    return mpFormatData.get();
}

std::unique_ptr<ScIconSetInfo> ScIconSetFormat::GetIconSetInfo(const ScAddress& rAddr) const
{
    ScRefCellValue rCell(mrDoc, rAddr);
    if(!rCell.hasNumeric())
        return nullptr;

    // now we have for sure a value
    double nVal = rCell.getValue();

    if (mpFormatData->m_Entries.size() < 3)
        return nullptr;

    double nMin = GetMinValue();
    double nMax = GetMaxValue();

    sal_Int32 nIndex = -1;
    const_iterator itr = begin();
    ScConditionMode eMode = ScConditionMode::EqGreater;
    double nValRef = 0;
    int i = 0;
    while(itr != end())
    {
        nValRef = CalcValue(nMin, nMax, itr);
        if (Compare(nVal, nValRef, itr))
        {
            nIndex = i;
            eMode = (*itr)->GetMode();
        }
        itr++;
        i++;
    }
    if (nIndex == -1)
        return nullptr;

    std::unique_ptr<ScIconSetInfo> pInfo(new ScIconSetInfo);

    const SfxPoolItem& rPoolItem = mrDoc.GetPattern(rAddr)->GetItem(ATTR_FONT_HEIGHT);
    tools::Long aFontHeight = static_cast<const SvxFontHeightItem&>(rPoolItem).GetHeight();
    pInfo->mnHeight = aFontHeight;

    if(mpFormatData->mbReverse)
    {
        sal_Int32 nMaxIndex = mpFormatData->m_Entries.size() - 1;
        nIndex = nMaxIndex - nIndex;
    }

    if (mpFormatData->mbCustom && sal_Int32(mpFormatData->maCustomVector.size()) > nIndex)
    {
        ScIconSetType eCustomType = mpFormatData->maCustomVector[nIndex].first;
        sal_Int32 nCustomIndex = mpFormatData->maCustomVector[nIndex].second;
        if (nCustomIndex == -1)
        {
            return nullptr;
        }

        pInfo->eIconSetType = eCustomType;
        pInfo->nIconIndex = nCustomIndex;
    }
    else
    {
        pInfo->nIconIndex = nIndex;
        pInfo->eIconSetType = mpFormatData->eIconSetType;
    }

    pInfo->mbShowValue = mpFormatData->mbShowValue;
    pInfo->eConditionMode = eMode;
    return pInfo;
}

ScFormatEntry::Type ScIconSetFormat::GetType() const
{
    return Type::Iconset;
}

void ScIconSetFormat::UpdateReference( sc::RefUpdateContext& rCxt )
{
    for(iterator itr = begin(); itr != end(); ++itr)
    {
        (*itr)->UpdateReference(rCxt);
    }
}

void ScIconSetFormat::UpdateInsertTab( sc::RefUpdateInsertTabContext& rCxt )
{
    for(iterator itr = begin(); itr != end(); ++itr)
    {
        (*itr)->UpdateInsertTab(rCxt);
    }
}

void ScIconSetFormat::UpdateDeleteTab( sc::RefUpdateDeleteTabContext& rCxt )
{
    for(iterator itr = begin(); itr != end(); ++itr)
    {
        (*itr)->UpdateDeleteTab(rCxt);
    }
}

void ScIconSetFormat::UpdateMoveTab( sc::RefUpdateMoveTabContext& rCxt )
{
    for(iterator itr = begin(); itr != end(); ++itr)
    {
        (*itr)->UpdateMoveTab(rCxt);
    }
}

ScIconSetFormat::iterator ScIconSetFormat::begin()
{
    return mpFormatData->m_Entries.begin();
}

ScIconSetFormat::const_iterator ScIconSetFormat::begin() const
{
    return mpFormatData->m_Entries.begin();
}

ScIconSetFormat::iterator ScIconSetFormat::end()
{
    return mpFormatData->m_Entries.end();
}

ScIconSetFormat::const_iterator ScIconSetFormat::end() const
{
    return mpFormatData->m_Entries.end();
}

double ScIconSetFormat::GetMinValue() const
{
    const_iterator itr = begin();

    if ((*itr)->GetType() == COLORSCALE_VALUE || (*itr)->GetType() == COLORSCALE_FORMULA)
        return (*itr)->GetValue();
    else
    {
        return getMinValue();
    }
}

double ScIconSetFormat::GetMaxValue() const
{
    auto const itr = mpFormatData->m_Entries.rbegin();

    if ((*itr)->GetType() == COLORSCALE_VALUE || (*itr)->GetType() == COLORSCALE_FORMULA)
        return (*itr)->GetValue();
    else
    {
        return getMaxValue();
    }
}

double ScIconSetFormat::CalcValue(double nMin, double nMax, const ScIconSetFormat::const_iterator& itr) const
{
    switch ((*itr)->GetType())
    {
        case COLORSCALE_PERCENT:
            return nMin + (nMax-nMin)*((*itr)->GetValue()/100);
        case COLORSCALE_MIN:
            return nMin;
        case COLORSCALE_MAX:
            return nMax;
        case COLORSCALE_PERCENTILE:
        {
            std::vector<double>& rValues = getValues();
            if(rValues.size() == 1)
                return rValues[0];
            else
            {
                double fPercentile = (*itr)->GetValue()/100.0;
                return GetPercentile(rValues, fPercentile);
            }
        }

        default:
        break;
    }

    return (*itr)->GetValue();
}

constexpr ScIconSetMap ScIconSetFormat::g_IconSetMap[] = {
    { u"3Arrows"_ustr, IconSet_3Arrows, 3 },
    { u"3ArrowsGray"_ustr, IconSet_3ArrowsGray, 3 },
    { u"3Flags"_ustr, IconSet_3Flags, 3 },
    { u"3TrafficLights1"_ustr, IconSet_3TrafficLights1, 3 },
    { u"3TrafficLights2"_ustr, IconSet_3TrafficLights2, 3 },
    { u"3Signs"_ustr, IconSet_3Signs, 3 },
    { u"3Symbols"_ustr, IconSet_3Symbols, 3 },
    { u"3Symbols2"_ustr, IconSet_3Symbols2, 3 },
    { u"3Smilies"_ustr, IconSet_3Smilies, 3 },
    { u"3ColorSmilies"_ustr, IconSet_3ColorSmilies, 3 },
    { u"3Stars"_ustr, IconSet_3Stars, 3 },
    { u"3Triangles"_ustr, IconSet_3Triangles, 3 },
    { u"4Arrows"_ustr, IconSet_4Arrows, 4 },
    { u"4ArrowsGray"_ustr, IconSet_4ArrowsGray, 4 },
    { u"4RedToBlack"_ustr, IconSet_4RedToBlack, 4 },
    { u"4Rating"_ustr, IconSet_4Rating, 4 },
    { u"4TrafficLights"_ustr, IconSet_4TrafficLights, 4 },
    { u"5Arrows"_ustr, IconSet_5Arrows, 5 },
    { u"5ArrowsGray"_ustr, IconSet_5ArrowsGray, 5 },
    { u"5Rating"_ustr, IconSet_5Ratings, 5 },
    { u"5Quarters"_ustr, IconSet_5Quarters, 5 },
    { u"5Boxes"_ustr, IconSet_5Boxes, 5 },
    { u""_ustr, IconSet_3Arrows, 0 }
};

size_t ScIconSetFormat::size() const
{
    return mpFormatData->m_Entries.size();
}


namespace {

constexpr OUString a3TrafficLights1[] = {
    BMP_ICON_SET_CIRCLES1_RED, BMP_ICON_SET_CIRCLES1_YELLOW, BMP_ICON_SET_CIRCLES1_GREEN
};

constexpr OUString a3TrafficLights2[] = {
    BMP_ICON_SET_TRAFFICLIGHTS_RED, BMP_ICON_SET_TRAFFICLIGHTS_YELLOW, BMP_ICON_SET_TRAFFICLIGHTS_GREEN
};

constexpr OUString a3Arrows[] = {
    BMP_ICON_SET_COLORARROWS_DOWN, BMP_ICON_SET_COLORARROWS_SAME, BMP_ICON_SET_COLORARROWS_UP
};

constexpr OUString a3ArrowsGray[] = {
    BMP_ICON_SET_GRAYARROWS_DOWN, BMP_ICON_SET_GRAYARROWS_SAME, BMP_ICON_SET_GRAYARROWS_UP
};

constexpr OUString a3Flags[] = {
    BMP_ICON_SET_FLAGS_RED, BMP_ICON_SET_FLAGS_YELLOW, BMP_ICON_SET_FLAGS_GREEN
};

constexpr OUString a3Smilies[] = {
    BMP_ICON_SET_NEGATIVE_YELLOW_SMILIE, BMP_ICON_SET_NEUTRAL_YELLOW_SMILIE, BMP_ICON_SET_POSITIVE_YELLOW_SMILIE
};

constexpr OUString a3ColorSmilies[] = {
    BMP_ICON_SET_NEGATIVE_RED_SMILIE, BMP_ICON_SET_NEUTRAL_YELLOW_SMILIE, BMP_ICON_SET_POSITIVE_GREEN_SMILIE
};

constexpr OUString a3Stars[] = {
    BMP_ICON_SET_STARS_EMPTY, BMP_ICON_SET_STARS_HALF, BMP_ICON_SET_STARS_FULL
};

constexpr OUString a3Triangles[] = {
    BMP_ICON_SET_TRIANGLES_DOWN, BMP_ICON_SET_TRIANGLES_SAME, BMP_ICON_SET_TRIANGLES_UP
};

constexpr OUString a4Arrows[] = {
    BMP_ICON_SET_COLORARROWS_DOWN, BMP_ICON_SET_COLORARROWS_SLIGHTLY_DOWN, BMP_ICON_SET_COLORARROWS_SLIGHTLY_UP, BMP_ICON_SET_COLORARROWS_UP
};

constexpr OUString a4ArrowsGray[] = {
    BMP_ICON_SET_GRAYARROWS_DOWN, BMP_ICON_SET_GRAYARROWS_SLIGHTLY_DOWN, BMP_ICON_SET_GRAYARROWS_SLIGHTLY_UP, BMP_ICON_SET_GRAYARROWS_UP
};

constexpr OUString a5Arrows[] = {
    BMP_ICON_SET_COLORARROWS_DOWN, BMP_ICON_SET_COLORARROWS_SLIGHTLY_DOWN,
    BMP_ICON_SET_COLORARROWS_SAME, BMP_ICON_SET_COLORARROWS_SLIGHTLY_UP, BMP_ICON_SET_COLORARROWS_UP
};

constexpr OUString a5ArrowsGray[] = {
    BMP_ICON_SET_GRAYARROWS_DOWN, BMP_ICON_SET_GRAYARROWS_SLIGHTLY_DOWN,
    BMP_ICON_SET_GRAYARROWS_SAME, BMP_ICON_SET_GRAYARROWS_SLIGHTLY_UP, BMP_ICON_SET_GRAYARROWS_UP
};

constexpr OUString a4TrafficLights[] = {
    BMP_ICON_SET_CIRCLES1_GRAY, BMP_ICON_SET_CIRCLES1_RED,
    BMP_ICON_SET_CIRCLES1_YELLOW, BMP_ICON_SET_CIRCLES1_GREEN
};

constexpr OUString a5Quarters[] = {
    BMP_ICON_SET_PIES_EMPTY, BMP_ICON_SET_PIES_ONE_QUARTER, BMP_ICON_SET_PIES_HALF,
    BMP_ICON_SET_PIES_THREE_QUARTER, BMP_ICON_SET_PIES_FULL,
};

constexpr OUString a5Boxes[] = {
    BMP_ICON_SET_SQUARES_EMPTY, BMP_ICON_SET_SQUARES_ONE_QUARTER,
    BMP_ICON_SET_SQUARES_HALF, BMP_ICON_SET_SQUARES_THREE_QUARTER,
    BMP_ICON_SET_SQUARES_FULL
};

constexpr OUString a3Symbols1[] = {
    BMP_ICON_SET_SYMBOLS1_CROSS, BMP_ICON_SET_SYMBOLS1_EXCLAMATION_MARK, BMP_ICON_SET_SYMBOLS1_CHECK
};

constexpr OUString a3Signs[] = {
    BMP_ICON_SET_SHAPES_DIAMOND, BMP_ICON_SET_SHAPES_TRIANGLE, BMP_ICON_SET_SHAPES_CIRCLE
};

constexpr OUString a4RedToBlack[] = {
    BMP_ICON_SET_CIRCLES2_DARK_GRAY, BMP_ICON_SET_CIRCLES2_LIGHT_GRAY,
    BMP_ICON_SET_CIRCLES2_LIGHT_RED, BMP_ICON_SET_CIRCLES2_DARK_RED
};

constexpr OUString a4Ratings[] = {
    BMP_ICON_SET_BARS_ONE_QUARTER, BMP_ICON_SET_BARS_HALF,
    BMP_ICON_SET_BARS_THREE_QUARTER, BMP_ICON_SET_BARS_FULL
};

constexpr OUString a5Ratings[] = {
    BMP_ICON_SET_BARS_EMPTY, BMP_ICON_SET_BARS_ONE_QUARTER, BMP_ICON_SET_BARS_HALF,
    BMP_ICON_SET_BARS_THREE_QUARTER, BMP_ICON_SET_BARS_FULL
};

struct ScIconSetBitmapMap {
    ScIconSetType eType;
    const OUString* pBitmaps;
};

const ScIconSetBitmapMap aBitmapMap[] = {
    { IconSet_3Arrows, a3Arrows },
    { IconSet_3ArrowsGray, a3ArrowsGray },
    { IconSet_3Flags, a3Flags },
    { IconSet_3Signs, a3Signs },
    { IconSet_3Symbols, a3Symbols1 },
    { IconSet_3Symbols2, a3Symbols1 },
    { IconSet_3TrafficLights1, a3TrafficLights1 },
    { IconSet_3TrafficLights2, a3TrafficLights2 },
    { IconSet_3Smilies, a3Smilies },
    { IconSet_3ColorSmilies, a3ColorSmilies },
    { IconSet_3Triangles, a3Triangles },
    { IconSet_3Stars, a3Stars },
    { IconSet_4Arrows, a4Arrows },
    { IconSet_4ArrowsGray, a4ArrowsGray },
    { IconSet_4Rating, a4Ratings },
    { IconSet_4RedToBlack, a4RedToBlack },
    { IconSet_4TrafficLights, a4TrafficLights },
    { IconSet_5Arrows, a5Arrows },
    { IconSet_5ArrowsGray, a5ArrowsGray },
    { IconSet_5Quarters, a5Quarters },
    { IconSet_5Ratings, a5Ratings },
    { IconSet_5Boxes, a5Boxes }
};

const ScIconSetMap* findIconSetType(ScIconSetType eType)
{
    const ScIconSetMap* pMap = ScIconSetFormat::g_IconSetMap;
    for (; !pMap->aName.isEmpty(); ++pMap)
    {
        if (pMap->eType == eType)
            return pMap;
    }

    return nullptr;
}

}

OUString ScIconSetFormat::getIconSetName( ScIconSetType eType )
{
    const ScIconSetMap* pMap = findIconSetType(eType);
    if (pMap)
        return pMap->aName;

    return u""_ustr;
}

sal_Int32 ScIconSetFormat::getIconSetElements( ScIconSetType eType )
{
    const ScIconSetMap* pMap = findIconSetType(eType);
    if (pMap)
        return pMap->nElements;

    return 0;
}

OUString ScIconSetFormat::getIconName(ScIconSetType const eType, sal_Int32 const nIndex)
{
    OUString sBitmap;

    for(const ScIconSetBitmapMap & i : aBitmapMap)
    {
        if(i.eType == eType)
        {
            sBitmap = *(i.pBitmaps + nIndex);
            break;
        }
    }

    assert(!sBitmap.isEmpty());

    return sBitmap;
}

BitmapEx& ScIconSetFormat::getBitmap(sc::IconSetBitmapMap & rIconSetBitmapMap,
        ScIconSetType const eType, sal_Int32 const nIndex)
{
    OUString sBitmap(ScIconSetFormat::getIconName(eType, nIndex));

    std::map<OUString, BitmapEx>::iterator itr = rIconSetBitmapMap.find(sBitmap);
    if (itr != rIconSetBitmapMap.end())
        return itr->second;

    BitmapEx aBitmap(sBitmap);
    std::pair<OUString, BitmapEx> aPair(sBitmap, aBitmap);
    std::pair<std::map<OUString, BitmapEx>::iterator, bool> itrNew = rIconSetBitmapMap.insert(aPair);
    assert(itrNew.second);

    return itrNew.first->second;
}

void ScIconSetFormat::EnsureSize()
{
    ScIconSetType eType = mpFormatData->eIconSetType;
    for (const ScIconSetMap & i : g_IconSetMap)
    {
        if (i.eType == eType)
        {
            // size_t nElements = aIconSetMap[i].nElements;
            // TODO: implement
            break;
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
