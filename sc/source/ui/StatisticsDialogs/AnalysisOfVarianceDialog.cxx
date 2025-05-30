/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#include <memory>
#include <string_view>

#include <rangelst.hxx>
#include <reffact.hxx>
#include <TableFillingAndNavigationTools.hxx>
#include <AnalysisOfVarianceDialog.hxx>
#include <scresid.hxx>
#include <strings.hrc>

namespace
{

struct StatisticCalculation {
    TranslateId aLabelId;
    const char* aFormula;
    const char* aResultRangeName;
};

StatisticCalculation const lclBasicStatistics[] =
{
    { STR_ANOVA_LABEL_GROUPS, nullptr,             nullptr       },
    { STRID_CALC_COUNT,       "=COUNT(%RANGE%)",   "COUNT_RANGE" },
    { STRID_CALC_SUM,         "=SUM(%RANGE%)",     "SUM_RANGE"   },
    { STRID_CALC_MEAN,        "=AVERAGE(%RANGE%)", "MEAN_RANGE"  },
    { STRID_CALC_VARIANCE,    "=VAR(%RANGE%)",     "VAR_RANGE"   },
    { {},                     nullptr,             nullptr       }
};

const TranslateId lclAnovaLabels[] =
{
    STR_ANOVA_LABEL_SOURCE_OF_VARIATION,
    STR_ANOVA_LABEL_SS,
    STR_ANOVA_LABEL_DF,
    STR_ANOVA_LABEL_MS,
    STR_ANOVA_LABEL_F,
    STR_ANOVA_LABEL_P_VALUE,
    STR_ANOVA_LABEL_F_CRITICAL,
    {}
};

constexpr OUString strWildcardRange = u"%RANGE%"_ustr;

OUString lclCreateMultiParameterFormula(
            ScRangeList&        aRangeList, const OUString& aFormulaTemplate,
            std::u16string_view aWildcard,  const ScDocument& rDocument,
            const ScAddress::Details& aAddressDetails)
{
    OUStringBuffer aResult;
    for (size_t i = 0; i < aRangeList.size(); i++)
    {
        OUString aRangeString(aRangeList[i].Format(rDocument, ScRefFlags::RANGE_ABS_3D, aAddressDetails));
        OUString aFormulaString = aFormulaTemplate.replaceAll(aWildcard, aRangeString);
        aResult.append(aFormulaString);
        if(i != aRangeList.size() - 1) // Not Last
            aResult.append(";");
    }
    return aResult.makeStringAndClear();
}

void lclMakeSubRangesList(ScRangeList& rRangeList, const ScRange& rInputRange, ScStatisticsInputOutputDialog::GroupedBy aGroupedBy)
{
    std::unique_ptr<DataRangeIterator> pIterator;
    if (aGroupedBy == ScStatisticsInputOutputDialog::BY_COLUMN)
        pIterator.reset(new DataRangeByColumnIterator(rInputRange));
    else
        pIterator.reset(new DataRangeByRowIterator(rInputRange));

    for( ; pIterator->hasNext(); pIterator->next() )
    {
        ScRange aRange = pIterator->get();
        rRangeList.push_back(aRange);
    }
}

}

ScAnalysisOfVarianceDialog::ScAnalysisOfVarianceDialog(
                    SfxBindings* pSfxBindings, SfxChildWindow* pChildWindow,
                    weld::Window* pParent, ScViewData& rViewData )
    : ScStatisticsInputOutputDialog(
            pSfxBindings, pChildWindow, pParent, rViewData,
            u"modules/scalc/ui/analysisofvariancedialog.ui"_ustr,
            u"AnalysisOfVarianceDialog"_ustr)
    , meFactor(SINGLE_FACTOR)
    , mxAlphaField(m_xBuilder->weld_spin_button(u"alpha-spin"_ustr))
    , mxSingleFactorRadio(m_xBuilder->weld_radio_button(u"radio-single-factor"_ustr))
    , mxTwoFactorRadio(m_xBuilder->weld_radio_button(u"radio-two-factor"_ustr))
    , mxRowsPerSampleField(m_xBuilder->weld_spin_button(u"rows-per-sample-spin"_ustr))
{
    mxSingleFactorRadio->connect_toggled( LINK( this, ScAnalysisOfVarianceDialog, FactorChanged ) );
    mxTwoFactorRadio->connect_toggled( LINK( this, ScAnalysisOfVarianceDialog, FactorChanged ) );

    mxSingleFactorRadio->set_active(true);
    mxTwoFactorRadio->set_active(false);

    FactorChanged();
}

ScAnalysisOfVarianceDialog::~ScAnalysisOfVarianceDialog()
{
}

void ScAnalysisOfVarianceDialog::Close()
{
    DoClose( ScAnalysisOfVarianceDialogWrapper::GetChildWindowId() );
}

TranslateId ScAnalysisOfVarianceDialog::GetUndoNameId()
{
    return STR_ANALYSIS_OF_VARIANCE_UNDO_NAME;
}

IMPL_LINK_NOARG( ScAnalysisOfVarianceDialog, FactorChanged, weld::Toggleable&, void )
{
    FactorChanged();
}

void ScAnalysisOfVarianceDialog::FactorChanged()
{
    if (mxSingleFactorRadio->get_active())
    {
        mxGroupByRowsRadio->set_sensitive(true);
        mxGroupByColumnsRadio->set_sensitive(true);
        mxRowsPerSampleField->set_sensitive(false);
        meFactor = SINGLE_FACTOR;
    }
    else if (mxTwoFactorRadio->get_active())
    {
        mxGroupByRowsRadio->set_sensitive(false);
        mxGroupByColumnsRadio->set_sensitive(false);
        mxRowsPerSampleField->set_sensitive(false); // Rows per sample not yet implemented
        meFactor = TWO_FACTOR;
    }
}

void ScAnalysisOfVarianceDialog::RowColumn(ScRangeList& rRangeList, AddressWalkerWriter& aOutput, FormulaTemplate& aTemplate,
                                           const OUString& sFormula, GroupedBy aGroupedBy, ScRange* pResultRange)
{
    if (pResultRange != nullptr)
        pResultRange->aStart = aOutput.current();
    if (!sFormula.isEmpty())
    {
        for (size_t i = 0; i < rRangeList.size(); i++)
        {
            ScRange const & rRange = rRangeList[i];
            aTemplate.setTemplate(sFormula);
            aTemplate.applyRange(strWildcardRange, rRange);
            aOutput.writeFormula(aTemplate.getTemplate());
            if (pResultRange != nullptr)
                pResultRange->aEnd = aOutput.current();
            aOutput.nextRow();
        }
    }
    else
    {
        TranslateId pLabelId = (aGroupedBy == BY_COLUMN) ? STR_COLUMN_LABEL_TEMPLATE : STR_ROW_LABEL_TEMPLATE;
        OUString aLabelTemplate(ScResId(pLabelId));

        for (size_t i = 0; i < rRangeList.size(); i++)
        {
            aTemplate.setTemplate(aLabelTemplate);
            aTemplate.applyNumber(u"%NUMBER%", i + 1);
            aOutput.writeString(aTemplate.getTemplate());
            if (pResultRange != nullptr)
                pResultRange->aEnd = aOutput.current();
            aOutput.nextRow();
        }
    }
}

void ScAnalysisOfVarianceDialog::AnovaSingleFactor(AddressWalkerWriter& output, FormulaTemplate& aTemplate)
{
    output.writeBoldString(ScResId(STR_ANOVA_SINGLE_FACTOR_LABEL));
    output.newLine();

    double aAlphaValue = mxAlphaField->get_value() / 100.0;
    output.writeString(ScResId(STR_LABEL_ALPHA));
    output.nextColumn();
    output.writeValue(aAlphaValue);
    aTemplate.autoReplaceAddress(u"%ALPHA%"_ustr, output.current());
    output.newLine();
    output.newLine();

    // Write labels
    for(sal_Int32 i = 0; lclBasicStatistics[i].aLabelId; i++)
    {
        output.writeString(ScResId(lclBasicStatistics[i].aLabelId));
        output.nextColumn();
    }
    output.newLine();

    // Collect aRangeList
    ScRangeList aRangeList;
    lclMakeSubRangesList(aRangeList, mInputRange, mGroupedBy);

    output.push();

    // Write values
    for(sal_Int32 i = 0; lclBasicStatistics[i].aLabelId; i++)
    {
        output.resetRow();
        ScRange aResultRange;
        OUString sFormula = OUString::createFromAscii(lclBasicStatistics[i].aFormula);
        RowColumn(aRangeList, output, aTemplate, sFormula, mGroupedBy, &aResultRange);
        output.nextColumn();
        if (lclBasicStatistics[i].aResultRangeName != nullptr)
        {
            OUString sResultRangeName = OUString::createFromAscii(lclBasicStatistics[i].aResultRangeName);
            aTemplate.autoReplaceRange("%" + sResultRangeName + "%", aResultRange);
        }
    }

    output.nextRow(); // Blank row

    // Write ANOVA labels
    output.resetColumn();
    for(sal_Int32 i = 0; lclAnovaLabels[i]; i++)
    {
        output.writeString(ScResId(lclAnovaLabels[i]));
        output.nextColumn();
    }
    output.nextRow();

    aTemplate.autoReplaceRange(u"%FIRST_COLUMN%"_ustr, aRangeList[0]);

    // Between Groups
    {
        // Label
        output.resetColumn();
        output.writeString(ScResId(STR_ANOVA_LABEL_BETWEEN_GROUPS));
        output.nextColumn();

        // Sum of Squares
        aTemplate.setTemplate("=SUMPRODUCT(%SUM_RANGE%;%MEAN_RANGE%)-SUM(%SUM_RANGE%)^2/SUM(%COUNT_RANGE%)");
        aTemplate.autoReplaceAddress(u"%BETWEEN_SS%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // Degree of freedom
        aTemplate.setTemplate("=COUNT(%SUM_RANGE%)-1");
        aTemplate.autoReplaceAddress(u"%BETWEEN_DF%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // MS
        aTemplate.setTemplate("=%BETWEEN_SS% / %BETWEEN_DF%");
        aTemplate.autoReplaceAddress(u"%BETWEEN_MS%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // F
        aTemplate.setTemplate("=%BETWEEN_MS% / %WITHIN_MS%");
        aTemplate.applyAddress(u"%WITHIN_MS%",  output.current(-1, 1));
        aTemplate.autoReplaceAddress(u"%F_VAL%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // P-value
        aTemplate.setTemplate("=FDIST(%F_VAL%; %BETWEEN_DF%; %WITHIN_DF%");
        aTemplate.applyAddress(u"%WITHIN_DF%",   output.current(-3, 1));
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // F critical
        aTemplate.setTemplate("=FINV(%ALPHA%; %BETWEEN_DF%; %WITHIN_DF%");
        aTemplate.applyAddress(u"%WITHIN_DF%",  output.current(-4, 1));
        output.writeFormula(aTemplate.getTemplate());
    }
    output.nextRow();

    // Within Groups
    {
        // Label
        output.resetColumn();
        output.writeString(ScResId(STR_ANOVA_LABEL_WITHIN_GROUPS));
        output.nextColumn();

        // Sum of Squares
        OUString aSSPart = lclCreateMultiParameterFormula(aRangeList, u"DEVSQ(%RANGE%)"_ustr, strWildcardRange, mDocument, mAddressDetails);
        aTemplate.setTemplate("=SUM(%RANGE%)");
        aTemplate.applyString(strWildcardRange, aSSPart);
        aTemplate.autoReplaceAddress(u"%WITHIN_SS%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // Degree of freedom
        aTemplate.setTemplate("=SUM(%COUNT_RANGE%)-COUNT(%COUNT_RANGE%)");
        aTemplate.autoReplaceAddress(u"%WITHIN_DF%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // MS
        aTemplate.setTemplate("=%WITHIN_SS% / %WITHIN_DF%");
        output.writeFormula(aTemplate.getTemplate());
    }
    output.nextRow();

    // Total
    {
        // Label
        output.resetColumn();
        output.writeString(ScResId(STR_ANOVA_LABEL_TOTAL));
        output.nextColumn();

        // Sum of Squares
        aTemplate.setTemplate("=DEVSQ(%RANGE_LIST%)");
        aTemplate.applyRangeList(u"%RANGE_LIST%", aRangeList, ';');
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // Degree of freedom
        aTemplate.setTemplate("=SUM(%COUNT_RANGE%) - 1");
        output.writeFormula(aTemplate.getTemplate());
    }
    output.nextRow();
}

void ScAnalysisOfVarianceDialog::AnovaTwoFactor(AddressWalkerWriter& output, FormulaTemplate& aTemplate)
{
    output.writeBoldString(ScResId(STR_ANOVA_TWO_FACTOR_LABEL));
    output.newLine();

    double aAlphaValue = mxAlphaField->get_value() / 100.0;
    output.writeString("Alpha");
    output.nextColumn();
    output.writeValue(aAlphaValue);
    aTemplate.autoReplaceAddress(u"%ALPHA%"_ustr, output.current());
    output.newLine();
    output.newLine();

    // Write labels
    for(sal_Int32 i = 0; lclBasicStatistics[i].aLabelId; i++)
    {
        output.writeString(ScResId(lclBasicStatistics[i].aLabelId));
        output.nextColumn();
    }
    output.newLine();

    ScRangeList aColumnRangeList;
    ScRangeList aRowRangeList;

    lclMakeSubRangesList(aColumnRangeList, mInputRange, BY_COLUMN);
    lclMakeSubRangesList(aRowRangeList, mInputRange, BY_ROW);

    // Write ColumnX values
    output.push();
    for(sal_Int32 i = 0; lclBasicStatistics[i].aLabelId; i++)
    {
        output.resetRow();
        ScRange aResultRange;
        OUString sFormula = OUString::createFromAscii(lclBasicStatistics[i].aFormula);
        RowColumn(aColumnRangeList, output, aTemplate, sFormula, BY_COLUMN, &aResultRange);
        if (lclBasicStatistics[i].aResultRangeName != nullptr)
        {
            OUString sResultRangeName = OUString::createFromAscii(lclBasicStatistics[i].aResultRangeName);
            aTemplate.autoReplaceRange("%" + sResultRangeName + "_COLUMN%", aResultRange);
        }
        output.nextColumn();
    }
    output.newLine();

    // Write RowX values
    output.push();
    for(sal_Int32 i = 0; lclBasicStatistics[i].aLabelId; i++)
    {
        output.resetRow();
        ScRange aResultRange;
        OUString sFormula = OUString::createFromAscii(lclBasicStatistics[i].aFormula);
        RowColumn(aRowRangeList, output, aTemplate, sFormula, BY_ROW, &aResultRange);

        if (lclBasicStatistics[i].aResultRangeName != nullptr)
        {
            OUString sResultRangeName = OUString::createFromAscii(lclBasicStatistics[i].aResultRangeName);
            aTemplate.autoReplaceRange("%" + sResultRangeName + "_ROW%", aResultRange);
        }
        output.nextColumn();
    }
    output.newLine();

    // Write ANOVA labels
    for(sal_Int32 i = 0; lclAnovaLabels[i]; i++)
    {
        output.writeString(ScResId(lclAnovaLabels[i]));
        output.nextColumn();
    }
    output.nextRow();

    // Setup auto-replace strings
    aTemplate.autoReplaceRange(strWildcardRange, mInputRange);
    aTemplate.autoReplaceRange(u"%FIRST_COLUMN%"_ustr, aColumnRangeList[0]);
    aTemplate.autoReplaceRange(u"%FIRST_ROW%"_ustr,    aRowRangeList[0]);

    // Rows
    {
        // Label
        output.resetColumn();
        output.writeString("Rows");
        output.nextColumn();

        // Sum of Squares
        aTemplate.setTemplate("=SUMPRODUCT(%SUM_RANGE_ROW%;%MEAN_RANGE_ROW%) - SUM(%RANGE%)^2 / COUNT(%RANGE%)");
        aTemplate.autoReplaceAddress(u"%ROW_SS%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // Degree of freedom
        aTemplate.setTemplate("=MAX(%COUNT_RANGE_COLUMN%) - 1");
        aTemplate.autoReplaceAddress(u"%ROW_DF%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // MS
        aTemplate.setTemplate("=%ROW_SS% / %ROW_DF%");
        aTemplate.autoReplaceAddress(u"%MS_ROW%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // F
        aTemplate.setTemplate("=%MS_ROW% / %MS_ERROR%");
        aTemplate.applyAddress(u"%MS_ERROR%", output.current(-1, 2));
        aTemplate.autoReplaceAddress(u"%F_ROW%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // P-value
        aTemplate.setTemplate("=FDIST(%F_ROW%; %ROW_DF%; %ERROR_DF%");
        aTemplate.applyAddress(u"%ERROR_DF%",   output.current(-3, 2));
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // F critical
        aTemplate.setTemplate("=FINV(%ALPHA%; %ROW_DF%; %ERROR_DF%");
        aTemplate.applyAddress(u"%ERROR_DF%",  output.current(-4, 2));
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();
    }
    output.nextRow();

    // Columns
    {
        // Label
        output.resetColumn();
        output.writeString("Columns");
        output.nextColumn();

        // Sum of Squares
        aTemplate.setTemplate("=SUMPRODUCT(%SUM_RANGE_COLUMN%;%MEAN_RANGE_COLUMN%) - SUM(%RANGE%)^2 / COUNT(%RANGE%)");
        aTemplate.autoReplaceAddress(u"%COLUMN_SS%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // Degree of freedom
        aTemplate.setTemplate("=MAX(%COUNT_RANGE_ROW%) - 1");
        aTemplate.autoReplaceAddress(u"%COLUMN_DF%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // MS
        aTemplate.setTemplate("=%COLUMN_SS% / %COLUMN_DF%");
        aTemplate.autoReplaceAddress(u"%MS_COLUMN%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // F
        aTemplate.setTemplate("=%MS_COLUMN% / %MS_ERROR%");
        aTemplate.applyAddress(u"%MS_ERROR%", output.current(-1, 1));
        aTemplate.autoReplaceAddress(u"%F_COLUMN%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

         // P-value
        aTemplate.setTemplate("=FDIST(%F_COLUMN%; %COLUMN_DF%; %ERROR_DF%");
        aTemplate.applyAddress(u"%ERROR_DF%",   output.current(-3, 1));
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // F critical
        aTemplate.setTemplate("=FINV(%ALPHA%; %COLUMN_DF%; %ERROR_DF%");
        aTemplate.applyAddress(u"%ERROR_DF%",  output.current(-4, 1));
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();
    }
    output.nextRow();

    // Error
    {
        // Label
        output.resetColumn();
        output.writeString("Error");
        output.nextColumn();

        // Sum of Squares
        aTemplate.setTemplate("=SUMSQ(%RANGE%)+SUM(%RANGE%)^2/COUNT(%RANGE%) - (SUMPRODUCT(%SUM_RANGE_ROW%;%MEAN_RANGE_ROW%) + SUMPRODUCT(%SUM_RANGE_COLUMN%;%MEAN_RANGE_COLUMN%))");
        aTemplate.autoReplaceAddress(u"%ERROR_SS%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // Degree of freedom
        aTemplate.setTemplate("=%TOTAL_DF% - %ROW_DF% - %COLUMN_DF%");
        aTemplate.applyAddress(u"%TOTAL_DF%", output.current(0,1));
        aTemplate.autoReplaceAddress(u"%ERROR_DF%"_ustr, output.current());
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // MS
        aTemplate.setTemplate("=%ERROR_SS% / %ERROR_DF%");
        output.writeFormula(aTemplate.getTemplate());
    }
    output.nextRow();

     // Total
    {
        // Label
        output.resetColumn();
        output.writeString("Total");
        output.nextColumn();

        // Sum of Squares
        aTemplate.setTemplate("=SUM(%ROW_SS%;%COLUMN_SS%;%ERROR_SS%)");
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();

        // Degree of freedom
        aTemplate.setTemplate("=COUNT(%RANGE%)-1");
        output.writeFormula(aTemplate.getTemplate());
        output.nextColumn();
    }
}

ScRange ScAnalysisOfVarianceDialog::ApplyOutput(ScDocShell& rDocShell)
{
    AddressWalkerWriter output(mOutputAddress, rDocShell, mDocument,
        formula::FormulaGrammar::mergeToGrammar(formula::FormulaGrammar::GRAM_ENGLISH, mAddressDetails.eConv));
    FormulaTemplate aTemplate(&mDocument);

    if (meFactor == SINGLE_FACTOR)
    {
        AnovaSingleFactor(output, aTemplate);
    }
    else if (meFactor == TWO_FACTOR)
    {
        AnovaTwoFactor(output, aTemplate);
    }

    return ScRange(output.mMinimumAddress, output.mMaximumAddress);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
