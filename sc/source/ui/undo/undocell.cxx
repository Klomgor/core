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

#include <undocell.hxx>

#include <scitems.hxx>
#include <editeng/editobj.hxx>
#include <sfx2/app.hxx>
#include <svx/svdocapt.hxx> //
#include <comphelper/lok.hxx>
#include <osl/diagnose.h>

#include <document.hxx>
#include <patattr.hxx>
#include <docsh.hxx>
#include <tabvwsh.hxx>
#include <globstr.hrc>
#include <scresid.hxx>
#include <global.hxx>
#include <formulacell.hxx>
#include <target.hxx>
#include <undoolk.hxx>
#include <detdata.hxx>
#include <stlpool.hxx>
#include <printfun.hxx>
#include <rangenam.hxx>
#include <chgtrack.hxx>
#include <stringutil.hxx>
#include <utility>

namespace HelperNotifyChanges
{
    static void NotifyIfChangesListeners(const ScDocShell& rDocShell, const ScAddress &rPos,
        const ScUndoEnterData::ValuesType &rOldValues, const OUString& rType = u"cell-change"_ustr)
    {
        ScModelObj* pModelObj = rDocShell.GetModel();
        if (pModelObj)
        {
            ScRangeList aChangeRanges;

            for (const auto & rOldValue : rOldValues)
            {
                aChangeRanges.push_back( ScRange(rPos.Col(), rPos.Row(), rOldValue.mnTab));
            }

            if (getMustPropagateChangesModel(pModelObj))
                Notify(*pModelObj, aChangeRanges, rType);
            if (pModelObj) // possibly need to invalidate getCellArea results
            {
                Notify(*pModelObj, aChangeRanges, isDataAreaInvalidateType(rType)
                    ? u"data-area-invalidate"_ustr : u"data-area-extend"_ustr);
            }
        }
    }
}


ScUndoCursorAttr::ScUndoCursorAttr( ScDocShell& rNewDocShell,
            SCCOL nNewCol, SCROW nNewRow, SCTAB nNewTab,
            const ScPatternAttr* pOldPat, const ScPatternAttr* pNewPat,
            const ScPatternAttr* pApplyPat ) :
    ScSimpleUndo( rNewDocShell ),
    nCol( nNewCol ),
    nRow( nNewRow ),
    nTab( nNewTab ),
    aOldPattern( pOldPat ),
    aNewPattern( pNewPat ),
    aApplyPattern( pApplyPat ),
    pOldEditData( static_cast<EditTextObject*>(nullptr) ),
    pNewEditData( static_cast<EditTextObject*>(nullptr) )
{
}

ScUndoCursorAttr::~ScUndoCursorAttr()
{
}

OUString ScUndoCursorAttr::GetComment() const
{
    //! own text for automatic attribution
    return ScResId( STR_UNDO_CURSORATTR ); // "Attribute"
}

void ScUndoCursorAttr::SetEditData( std::unique_ptr<EditTextObject> pOld, std::unique_ptr<EditTextObject> pNew )
{
    pOldEditData = std::move(pOld);
    pNewEditData = std::move(pNew);
}

void ScUndoCursorAttr::DoChange( const CellAttributeHolder& rWhichPattern, const std::unique_ptr<EditTextObject>& pEditData ) const
{
    ScDocument& rDoc = rDocShell.GetDocument();
    ScAddress aPos(nCol, nRow, nTab);
    rDoc.SetPattern( nCol, nRow, nTab, rWhichPattern );

    if (rDoc.GetCellType(aPos) == CELLTYPE_EDIT && pEditData)
        rDoc.SetEditText(aPos, *pEditData, nullptr);

    ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
    if (pViewShell)
    {
        pViewShell->SetTabNo( nTab );
        pViewShell->MoveCursorAbs( nCol, nRow, SC_FOLLOW_JUMP, false, false );
        pViewShell->AdjustBlockHeight();
    }

    const SfxItemSet& rApplySet = aApplyPattern.getScPatternAttr()->GetItemSet();
    bool bPaintExt = ( rApplySet.GetItemState( ATTR_SHADOW ) != SfxItemState::DEFAULT ||
                       rApplySet.GetItemState( ATTR_CONDITIONAL ) != SfxItemState::DEFAULT );
    bool bPaintRows = ( rApplySet.GetItemState( ATTR_HOR_JUSTIFY ) != SfxItemState::DEFAULT );

    sal_uInt16 nFlags = SC_PF_TESTMERGE;
    if (bPaintExt)
        nFlags |= SC_PF_LINES;
    if (bPaintRows)
        nFlags |= SC_PF_WHOLEROWS;
    rDocShell.PostPaint( nCol,nRow,nTab, nCol,nRow,nTab, PaintPartFlags::Grid, nFlags );
}

void ScUndoCursorAttr::Undo()
{
    BeginUndo();
    DoChange(aOldPattern, pOldEditData);
    EndUndo();
}

void ScUndoCursorAttr::Redo()
{
    BeginRedo();
    DoChange(aNewPattern, pNewEditData);
    EndRedo();
}

void ScUndoCursorAttr::Repeat(SfxRepeatTarget& rTarget)
{
    if (auto pViewTarget = dynamic_cast<ScTabViewTarget*>( &rTarget))
        pViewTarget->GetViewShell().ApplySelectionPattern( *aApplyPattern.getScPatternAttr() );
}

bool ScUndoCursorAttr::CanRepeat(SfxRepeatTarget& rTarget) const
{
    return dynamic_cast<const ScTabViewTarget*>( &rTarget) !=  nullptr;
}

ScUndoEnterData::Value::Value() : mnTab(-1), mbHasFormat(false), mnFormat(0) {}

ScUndoEnterData::ScUndoEnterData(
    ScDocShell& rNewDocShell, const ScAddress& rPos, ValuesType& rOldValues,
    OUString aNewStr, std::unique_ptr<EditTextObject> pObj ) :
    ScSimpleUndo( rNewDocShell ),
    maNewString(std::move(aNewStr)),
    mpNewEditData(std::move(pObj)),
    mnEndChangeAction(0),
    maPos(rPos)
{
    maOldValues.swap(rOldValues);

    SetChangeTrack();
}

OUString ScUndoEnterData::GetComment() const
{
    return ScResId( STR_UNDO_ENTERDATA ); // "Input"
}

void ScUndoEnterData::DoChange() const
{
    // only when needed (old or new Edit cell, or Attribute)?
    bool bHeightChanged = false;
    for (const auto & i : maOldValues)
    {
        if (rDocShell.AdjustRowHeight(maPos.Row(), maPos.Row(), i.mnTab))
            bHeightChanged = true;
    }

    ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
    if (pViewShell)
    {
        if (comphelper::LibreOfficeKit::isActive() && bHeightChanged)
        {
            ScTabViewShell::notifyAllViewsHeaderInvalidation(pViewShell, ROW_HEADER, maPos.Tab());
            ScTabViewShell::notifyAllViewsSheetGeomInvalidation(
                pViewShell, false /* bColumns */, true /* bRows */, true /* bSizes*/,
                false /* bHidden */, false /* bFiltered */, false /* bGroups */, maPos.Tab());
        }
        pViewShell->SetTabNo(maPos.Tab());
        pViewShell->MoveCursorAbs(maPos.Col(), maPos.Row(), SC_FOLLOW_JUMP, false, false);
    }

    rDocShell.PostDataChanged();
}

void ScUndoEnterData::SetChangeTrack()
{
    ScChangeTrack* pChangeTrack = rDocShell.GetDocument().GetChangeTrack();
    if ( pChangeTrack )
    {
        mnEndChangeAction = pChangeTrack->GetActionMax() + 1;
        ScAddress aPos(maPos);
        for (const Value & rOldValue : maOldValues)
        {
            aPos.SetTab(rOldValue.mnTab);
            sal_uLong nFormat = 0;
            if (rOldValue.mbHasFormat)
                nFormat = rOldValue.mnFormat;
            pChangeTrack->AppendContent(aPos, rOldValue.maCell, nFormat);
        }
        if ( mnEndChangeAction > pChangeTrack->GetActionMax() )
            mnEndChangeAction = 0;       // nothing is appended
    }
    else
        mnEndChangeAction = 0;
}

void ScUndoEnterData::Undo()
{
    BeginUndo();

    ScDocument& rDoc = rDocShell.GetDocument();
    for (const Value & rVal : maOldValues)
    {
        ScCellValue aNewCell;
        aNewCell.assign(rVal.maCell, rDoc, ScCloneFlags::StartListening);
        ScAddress aPos = maPos;
        aPos.SetTab(rVal.mnTab);
        aNewCell.release(rDoc, aPos);

        if (rVal.mbHasFormat)
            rDoc.ApplyAttr(maPos.Col(), maPos.Row(), rVal.mnTab,
                            SfxUInt32Item(ATTR_VALUE_FORMAT, rVal.mnFormat));
        else
        {
            ScPatternAttr* pPattern(new ScPatternAttr(*rDoc.GetPattern(maPos.Col(), maPos.Row(), rVal.mnTab)));
            pPattern->GetItemSet().ClearItem( ATTR_VALUE_FORMAT );
            rDoc.SetPattern(maPos.Col(), maPos.Row(), rVal.mnTab, CellAttributeHolder(pPattern, true));
        }
        rDocShell.PostPaintCell(maPos.Col(), maPos.Row(), rVal.mnTab);
    }

    ScChangeTrack* pChangeTrack = rDoc.GetChangeTrack();
    size_t nCount = maOldValues.size();
    if ( pChangeTrack && mnEndChangeAction >= sal::static_int_cast<sal_uLong>(nCount) )
        pChangeTrack->Undo( mnEndChangeAction - nCount + 1, mnEndChangeAction );

    DoChange();
    EndUndo();

    HelperNotifyChanges::NotifyIfChangesListeners(rDocShell, maPos, maOldValues, u"undo"_ustr);
}

void ScUndoEnterData::Redo()
{
    BeginRedo();

    ScDocument& rDoc = rDocShell.GetDocument();
    for (const Value & rOldValue : maOldValues)
    {
        SCTAB nTab = rOldValue.mnTab;
        if (mpNewEditData)
        {
            ScAddress aPos = maPos;
            aPos.SetTab(nTab);
            // edit text will be cloned.
            rDoc.SetEditText(aPos, *mpNewEditData, nullptr);
        }
        else
            rDoc.SetString(maPos.Col(), maPos.Row(), nTab, maNewString);

        rDocShell.PostPaintCell(maPos.Col(), maPos.Row(), nTab);
    }

    SetChangeTrack();

    DoChange();
    EndRedo();

    HelperNotifyChanges::NotifyIfChangesListeners(rDocShell, maPos, maOldValues, u"redo"_ustr);
}

void ScUndoEnterData::Repeat(SfxRepeatTarget& rTarget)
{
    if (auto pViewTarget = dynamic_cast<ScTabViewTarget*>( &rTarget))
    {
        OUString aTemp = maNewString;
        pViewTarget->GetViewShell().EnterDataAtCursor( aTemp );
    }
}

bool ScUndoEnterData::CanRepeat(SfxRepeatTarget& rTarget) const
{
    return dynamic_cast<const ScTabViewTarget*>( &rTarget) !=  nullptr;
}

ScUndoEnterValue::ScUndoEnterValue(
    ScDocShell& rNewDocShell, const ScAddress& rNewPos,
    ScCellValue aUndoCell, double nVal ) :
    ScSimpleUndo( rNewDocShell ),
    aPos        ( rNewPos ),
    maOldCell(std::move(aUndoCell)),
    nValue      ( nVal )
{
    SetChangeTrack();
}

ScUndoEnterValue::~ScUndoEnterValue()
{
}

OUString ScUndoEnterValue::GetComment() const
{
    return ScResId( STR_UNDO_ENTERDATA ); // "Input"
}

void ScUndoEnterValue::SetChangeTrack()
{
    ScDocument& rDoc = rDocShell.GetDocument();
    ScChangeTrack* pChangeTrack = rDoc.GetChangeTrack();
    if ( pChangeTrack )
    {
        nEndChangeAction = pChangeTrack->GetActionMax() + 1;
        pChangeTrack->AppendContent(aPos, maOldCell);
        if ( nEndChangeAction > pChangeTrack->GetActionMax() )
            nEndChangeAction = 0;       // nothing is appended
    }
    else
        nEndChangeAction = 0;
}

void ScUndoEnterValue::Undo()
{
    BeginUndo();

    ScDocument& rDoc = rDocShell.GetDocument();
    ScCellValue aNewCell;
    aNewCell.assign(maOldCell, rDoc, ScCloneFlags::StartListening);
    aNewCell.release(rDoc, aPos);

    rDocShell.PostPaintCell( aPos );

    ScChangeTrack* pChangeTrack = rDoc.GetChangeTrack();
    if ( pChangeTrack )
        pChangeTrack->Undo( nEndChangeAction, nEndChangeAction );

    EndUndo();
}

void ScUndoEnterValue::Redo()
{
    BeginRedo();

    ScDocument& rDoc = rDocShell.GetDocument();
    rDoc.SetValue( aPos.Col(), aPos.Row(), aPos.Tab(), nValue );
    rDocShell.PostPaintCell( aPos );

    SetChangeTrack();

    EndRedo();
}

void ScUndoEnterValue::Repeat(SfxRepeatTarget& /* rTarget */)
{
    // makes no sense
}

bool ScUndoEnterValue::CanRepeat(SfxRepeatTarget& /* rTarget */) const
{
    return false;
}

ScUndoSetCell::ScUndoSetCell( ScDocShell& rDocSh, const ScAddress& rPos, ScCellValue aOldVal, ScCellValue aNewVal ) :
    ScSimpleUndo(rDocSh), maPos(rPos), maOldValue(std::move(aOldVal)), maNewValue(std::move(aNewVal)), mnEndChangeAction(0)
{
    SetChangeTrack();
}

ScUndoSetCell::~ScUndoSetCell() {}

void ScUndoSetCell::Undo()
{
    BeginUndo();
    SetValue(maOldValue);
    MoveCursorToCell();
    rDocShell.PostPaintCell(maPos);

    ScDocument& rDoc = rDocShell.GetDocument();
    ScChangeTrack* pChangeTrack = rDoc.GetChangeTrack();
    if (pChangeTrack)
        pChangeTrack->Undo(mnEndChangeAction, mnEndChangeAction);

    EndUndo();
}

void ScUndoSetCell::Redo()
{
    BeginRedo();
    SetValue(maNewValue);
    MoveCursorToCell();
    rDocShell.PostPaintCell(maPos);
    SetChangeTrack();
    EndRedo();
}

void ScUndoSetCell::Repeat( SfxRepeatTarget& /*rTarget*/ )
{
    // Makes no sense.
}

bool ScUndoSetCell::CanRepeat( SfxRepeatTarget& /*rTarget*/ ) const
{
    return false;
}

OUString ScUndoSetCell::GetComment() const
{
    return ScResId(STR_UNDO_ENTERDATA); // "Input"
}

void ScUndoSetCell::SetChangeTrack()
{
    ScDocument& rDoc = rDocShell.GetDocument();
    ScChangeTrack* pChangeTrack = rDoc.GetChangeTrack();
    if (pChangeTrack)
    {
        mnEndChangeAction = pChangeTrack->GetActionMax() + 1;

        pChangeTrack->AppendContent(maPos, maOldValue);

        if (mnEndChangeAction > pChangeTrack->GetActionMax())
            mnEndChangeAction = 0;       // Nothing is appended
    }
    else
        mnEndChangeAction = 0;
}

void ScUndoSetCell::SetValue( const ScCellValue& rVal )
{
    ScDocument& rDoc = rDocShell.GetDocument();

    switch (rVal.getType())
    {
        case CELLTYPE_NONE:
            // empty cell
            rDoc.SetEmptyCell(maPos);
        break;
        case CELLTYPE_VALUE:
            rDoc.SetValue(maPos, rVal.getDouble());
        break;
        case CELLTYPE_STRING:
        {
            ScSetStringParam aParam;
            aParam.setTextInput();
            // Undo only cell content, without setting any number format.
            aParam.meSetTextNumFormat = ScSetStringParam::Keep;
            rDoc.SetString(maPos, rVal.getSharedString()->getString(), &aParam);
        }
        break;
        case CELLTYPE_EDIT:
            rDoc.SetEditText(maPos, rVal.getEditText()->Clone());
        break;
        case CELLTYPE_FORMULA:
            rDoc.SetFormulaCell(maPos, rVal.getFormula()->Clone());
        break;
        default:
            ;
    }
}

void ScUndoSetCell::MoveCursorToCell()
{
    ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
    if ( pViewShell )
    {
        pViewShell->SetTabNo( maPos.Tab() );
        pViewShell->MoveCursorAbs( maPos.Col(), maPos.Row(), SC_FOLLOW_JUMP, false, false );
    }
}

ScUndoPageBreak::ScUndoPageBreak( ScDocShell& rNewDocShell,
            SCCOL nNewCol, SCROW nNewRow, SCTAB nNewTab,
            bool bNewColumn, bool bNewInsert ) :
    ScSimpleUndo( rNewDocShell ),
    nCol( nNewCol ),
    nRow( nNewRow ),
    nTab( nNewTab ),
    bColumn( bNewColumn ),
    bInsert( bNewInsert )
{
}

ScUndoPageBreak::~ScUndoPageBreak()
{
}

OUString ScUndoPageBreak::GetComment() const
{
    //"Column break" | "Row break"  "insert" | "delete"
    return bColumn ?
        ( bInsert ?
            ScResId( STR_UNDO_INSCOLBREAK ) :
            ScResId( STR_UNDO_DELCOLBREAK )
        ) :
        ( bInsert ?
            ScResId( STR_UNDO_INSROWBREAK ) :
            ScResId( STR_UNDO_DELROWBREAK )
        );
}

void ScUndoPageBreak::DoChange( bool bInsertP ) const
{
    ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();

    if (pViewShell)
    {
        pViewShell->SetTabNo( nTab );
        pViewShell->MoveCursorAbs( nCol, nRow, SC_FOLLOW_JUMP, false, false );

        if (bInsertP)
            pViewShell->InsertPageBreak(bColumn, false);
        else
            pViewShell->DeletePageBreak(bColumn, false);

        rDocShell.GetDocument().InvalidatePageBreaks(nTab);
    }
}

void ScUndoPageBreak::Undo()
{
    BeginUndo();
    DoChange(!bInsert);
    EndUndo();
}

void ScUndoPageBreak::Redo()
{
    BeginRedo();
    DoChange(bInsert);
    EndRedo();
}

void ScUndoPageBreak::Repeat(SfxRepeatTarget& rTarget)
{
    if (auto pViewTarget = dynamic_cast<ScTabViewTarget*>( &rTarget))
    {
        ScTabViewShell& rViewShell = pViewTarget->GetViewShell();

        if (bInsert)
            rViewShell.InsertPageBreak(bColumn);
        else
            rViewShell.DeletePageBreak(bColumn);
    }
}

bool ScUndoPageBreak::CanRepeat(SfxRepeatTarget& rTarget) const
{
    return dynamic_cast<const ScTabViewTarget*>( &rTarget) !=  nullptr;
}

ScUndoPrintZoom::ScUndoPrintZoom( ScDocShell& rNewDocShell,
            SCTAB nT, sal_uInt16 nOS, sal_uInt16 nOP, sal_uInt16 nNS, sal_uInt16 nNP ) :
    ScSimpleUndo( rNewDocShell ),
    nTab( nT ),
    nOldScale( nOS ),
    nOldPages( nOP ),
    nNewScale( nNS ),
    nNewPages( nNP )
{
}

ScUndoPrintZoom::~ScUndoPrintZoom()
{
}

OUString ScUndoPrintZoom::GetComment() const
{
    return ScResId( STR_UNDO_PRINTSCALE );
}

void ScUndoPrintZoom::DoChange( bool bUndo )
{
    sal_uInt16 nScale = bUndo ? nOldScale : nNewScale;
    sal_uInt16 nPages = bUndo ? nOldPages : nNewPages;

    ScDocument& rDoc = rDocShell.GetDocument();
    OUString aStyleName = rDoc.GetPageStyle( nTab );
    ScStyleSheetPool* pStylePool = rDoc.GetStyleSheetPool();
    SfxStyleSheetBase* pStyleSheet = pStylePool->Find( aStyleName, SfxStyleFamily::Page );
    OSL_ENSURE( pStyleSheet, "PageStyle not found" );
    if ( pStyleSheet )
    {
        SfxItemSet& rSet = pStyleSheet->GetItemSet();
        rSet.Put( SfxUInt16Item( ATTR_PAGE_SCALE, nScale ) );
        rSet.Put( SfxUInt16Item( ATTR_PAGE_SCALETOPAGES, nPages ) );

        ScPrintFunc aPrintFunc( rDocShell, rDocShell.GetPrinter(), nTab );
        aPrintFunc.UpdatePages();
    }
}

void ScUndoPrintZoom::Undo()
{
    BeginUndo();
    DoChange(true);
    EndUndo();
}

void ScUndoPrintZoom::Redo()
{
    BeginRedo();
    DoChange(false);
    EndRedo();
}

void ScUndoPrintZoom::Repeat(SfxRepeatTarget& rTarget)
{
    if (auto pViewTarget = dynamic_cast<ScTabViewTarget*>( &rTarget))
    {
        ScTabViewShell& rViewShell = pViewTarget->GetViewShell();
        ScViewData& rViewData = rViewShell.GetViewData();
        rViewData.GetDocShell().SetPrintZoom( rViewData.GetTabNo(), nNewScale, nNewPages );
    }
}

bool ScUndoPrintZoom::CanRepeat(SfxRepeatTarget& rTarget) const
{
    return dynamic_cast<const ScTabViewTarget*>( &rTarget) !=  nullptr;
}

ScUndoThesaurus::ScUndoThesaurus(
    ScDocShell& rNewDocShell, SCCOL nNewCol, SCROW nNewRow, SCTAB nNewTab,
    ScCellValue aOldText, ScCellValue aNewText ) :
    ScSimpleUndo( rNewDocShell ),
    nCol( nNewCol ),
    nRow( nNewRow ),
    nTab( nNewTab ),
    maOldText(std::move(aOldText)),
    maNewText(std::move(aNewText))
{
    SetChangeTrack(maOldText);
}

ScUndoThesaurus::~ScUndoThesaurus() {}

OUString ScUndoThesaurus::GetComment() const
{
    return ScResId( STR_UNDO_THESAURUS );    // "Thesaurus"
}

void ScUndoThesaurus::SetChangeTrack( const ScCellValue& rOldCell )
{
    ScChangeTrack* pChangeTrack = rDocShell.GetDocument().GetChangeTrack();
    if ( pChangeTrack )
    {
        nEndChangeAction = pChangeTrack->GetActionMax() + 1;
        pChangeTrack->AppendContent(ScAddress(nCol, nRow, nTab), rOldCell);
        if ( nEndChangeAction > pChangeTrack->GetActionMax() )
            nEndChangeAction = 0;       // nothing is appended
    }
    else
        nEndChangeAction = 0;
}

void ScUndoThesaurus::DoChange( bool bUndo, const ScCellValue& rText )
{
    ScDocument& rDoc = rDocShell.GetDocument();

    ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
    if (pViewShell)
    {
        pViewShell->SetTabNo( nTab );
        pViewShell->MoveCursorAbs( nCol, nRow, SC_FOLLOW_JUMP, false, false );
    }

    ScAddress aPos(nCol, nRow, nTab);
    rText.commit(rDoc, aPos);
    if (!bUndo)
        SetChangeTrack(maOldText);

    rDocShell.PostPaintCell( nCol, nRow, nTab );
}

void ScUndoThesaurus::Undo()
{
    BeginUndo();
    DoChange(true, maOldText);
    ScChangeTrack* pChangeTrack = rDocShell.GetDocument().GetChangeTrack();
    if ( pChangeTrack )
        pChangeTrack->Undo( nEndChangeAction, nEndChangeAction );
    EndUndo();
}

void ScUndoThesaurus::Redo()
{
    BeginRedo();
    DoChange(false, maNewText);
    EndRedo();
}

void ScUndoThesaurus::Repeat(SfxRepeatTarget& rTarget)
{
    if (auto pViewTarget = dynamic_cast<ScTabViewTarget*>( &rTarget))
        pViewTarget->GetViewShell().DoThesaurus();
}

bool ScUndoThesaurus::CanRepeat(SfxRepeatTarget& rTarget) const
{
    return dynamic_cast<const ScTabViewTarget*>( &rTarget) !=  nullptr;
}


ScUndoReplaceNote::ScUndoReplaceNote( ScDocShell& rDocSh, const ScAddress& rPos,
        const ScNoteData& rNoteData, bool bInsert, std::unique_ptr<SdrUndoAction> pDrawUndo ) :
    ScSimpleUndo( rDocSh ),
    maPos( rPos ),
    mpDrawUndo( std::move(pDrawUndo) )
{
    OSL_ENSURE( rNoteData.mxCaption, "ScUndoReplaceNote::ScUndoReplaceNote - missing note caption" );
    if (bInsert)
    {
        maNewData = rNoteData;
    }
    else
    {
        maOldData = rNoteData;
    }
}

ScUndoReplaceNote::ScUndoReplaceNote( ScDocShell& rDocSh, const ScAddress& rPos,
        ScNoteData aOldData, ScNoteData aNewData, std::unique_ptr<SdrUndoAction> pDrawUndo ) :
    ScSimpleUndo( rDocSh ),
    maPos( rPos ),
    maOldData(std::move( aOldData )),
    maNewData(std::move( aNewData )),
    mpDrawUndo( std::move(pDrawUndo) )
{
    OSL_ENSURE( maOldData.mxCaption || maNewData.mxCaption, "ScUndoReplaceNote::ScUndoReplaceNote - missing note captions" );
    OSL_ENSURE( !maOldData.mxInitData && !maNewData.mxInitData, "ScUndoReplaceNote::ScUndoReplaceNote - unexpected uninitialized note" );
}

ScUndoReplaceNote::~ScUndoReplaceNote()
{
    mpDrawUndo.reset();
}

void ScUndoReplaceNote::Undo()
{
    BeginUndo();
    DoSdrUndoAction( mpDrawUndo.get(), &rDocShell.GetDocument() );
    /*  Undo insert -> remove new note.
        Undo remove -> insert old note.
        Undo replace -> remove new note, insert old note. */
    DoRemoveNote( maNewData );
    DoInsertNote( maOldData );
    rDocShell.PostPaintCell( maPos );
    EndUndo();
}

void ScUndoReplaceNote::Redo()
{
    BeginRedo();
    RedoSdrUndoAction( mpDrawUndo.get() );
    /*  Redo insert -> insert new note.
        Redo remove -> remove old note.
        Redo replace -> remove old note, insert new note. */
    DoRemoveNote( maOldData );
    DoInsertNote( maNewData );
    rDocShell.PostPaintCell( maPos );
    EndRedo();
}

void ScUndoReplaceNote::Repeat( SfxRepeatTarget& /*rTarget*/ )
{
}

bool ScUndoReplaceNote::CanRepeat( SfxRepeatTarget& /*rTarget*/ ) const
{
    return false;
}

OUString ScUndoReplaceNote::GetComment() const
{
    return ScResId( maNewData.mxCaption ?
        (maOldData.mxCaption ? STR_UNDO_EDITNOTE : STR_UNDO_INSERTNOTE) : STR_UNDO_DELETENOTE );
}

void ScUndoReplaceNote::DoInsertNote( const ScNoteData& rNoteData )
{
    if( rNoteData.mxCaption )
    {
        ScDocument& rDoc = rDocShell.GetDocument();
        OSL_ENSURE( !rDoc.GetNote(maPos), "ScUndoReplaceNote::DoInsertNote - unexpected cell note" );
        ScPostIt* pNote = new ScPostIt( rDoc, maPos, rNoteData, false );
        rDoc.SetNote( maPos, std::unique_ptr<ScPostIt>(pNote) );
        ScDocShell::LOKCommentNotify(LOKCommentNotificationType::Add, rDoc, maPos, pNote);
    }
}

void ScUndoReplaceNote::DoRemoveNote( const ScNoteData& rNoteData )
{
    if( !rNoteData.mxCaption )
        return;

    ScDocument& rDoc = rDocShell.GetDocument();
    OSL_ENSURE( rDoc.GetNote(maPos), "ScUndoReplaceNote::DoRemoveNote - missing cell note" );
    if( std::unique_ptr<ScPostIt> pNote = rDoc.ReleaseNote( maPos ) )
    {
        /*  Forget pointer to caption object to suppress removing the
            caption object from the drawing layer while deleting pNote
            (removing the caption is done by a drawing undo action). */
        pNote->ForgetCaption();
        ScDocShell::LOKCommentNotify(LOKCommentNotificationType::Remove, rDoc, maPos, pNote.get());
    }
}

ScUndoShowHideNote::ScUndoShowHideNote( ScDocShell& rDocSh, const ScAddress& rPos, bool bShow ) :
    ScSimpleUndo( rDocSh ),
    maPos( rPos ),
    mbShown( bShow )
{
}

ScUndoShowHideNote::~ScUndoShowHideNote()
{
}

void ScUndoShowHideNote::Undo()
{
    BeginUndo();
    if( ScPostIt* pNote = rDocShell.GetDocument().GetNote(maPos) )
        pNote->ShowCaption( maPos, !mbShown );
    EndUndo();
}

void ScUndoShowHideNote::Redo()
{
    BeginRedo();
    if( ScPostIt* pNote = rDocShell.GetDocument().GetNote(maPos) )
        pNote->ShowCaption( maPos, mbShown );
    EndRedo();
}

void ScUndoShowHideNote::Repeat( SfxRepeatTarget& /*rTarget*/ )
{
}

bool ScUndoShowHideNote::CanRepeat( SfxRepeatTarget& /*rTarget*/ ) const
{
    return false;
}

OUString ScUndoShowHideNote::GetComment() const
{
    return ScResId( mbShown ? STR_UNDO_SHOWNOTE : STR_UNDO_HIDENOTE );
}

ScUndoDetective::ScUndoDetective( ScDocShell& rNewDocShell,
                                    std::unique_ptr<SdrUndoAction> pDraw, const ScDetOpData* pOperation,
                                    std::unique_ptr<ScDetOpList> pUndoList ) :
    ScSimpleUndo( rNewDocShell ),
    pOldList    ( std::move(pUndoList) ),
    nAction     ( 0 ),
    pDrawUndo   ( std::move(pDraw) )
{
    bIsDelete = ( pOperation == nullptr );
    if (!bIsDelete)
    {
        nAction = static_cast<sal_uInt16>(pOperation->GetOperation());
        aPos = pOperation->GetPos();
    }
}

ScUndoDetective::~ScUndoDetective()
{
    pDrawUndo.reset();
    pOldList.reset();
}

OUString ScUndoDetective::GetComment() const
{
    TranslateId pId = STR_UNDO_DETDELALL;
    if ( !bIsDelete )
        switch ( static_cast<ScDetOpType>(nAction) )
        {
            case SCDETOP_ADDSUCC:   pId = STR_UNDO_DETADDSUCC;  break;
            case SCDETOP_DELSUCC:   pId = STR_UNDO_DETDELSUCC;  break;
            case SCDETOP_ADDPRED:   pId = STR_UNDO_DETADDPRED;  break;
            case SCDETOP_DELPRED:   pId = STR_UNDO_DETDELPRED;  break;
            case SCDETOP_ADDERROR:  pId = STR_UNDO_DETADDERROR; break;
        }

    return ScResId(pId);
}

void ScUndoDetective::Undo()
{
    BeginUndo();

    ScDocument& rDoc = rDocShell.GetDocument();
    DoSdrUndoAction(pDrawUndo.get(), &rDoc);

    if (bIsDelete)
    {
        if ( pOldList )
            rDoc.SetDetOpList( std::unique_ptr<ScDetOpList>(new ScDetOpList(*pOldList)) );
    }
    else
    {
        // Remove entry from list

        ScDetOpList* pList = rDoc.GetDetOpList();
        if (pList && pList->Count())
        {
            ScDetOpDataVector& rVec = pList->GetDataVector();
            ScDetOpDataVector::iterator it = rVec.begin() + rVec.size() - 1;
            if ( it->GetOperation() == static_cast<ScDetOpType>(nAction) && it->GetPos() == aPos )
                rVec.erase( it);
            else
            {
                OSL_FAIL("Detective entry could not be found in list");
            }
        }
    }

    ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
    if (pViewShell)
        pViewShell->RecalcPPT();    //! use broadcast instead?

    EndUndo();
}

void ScUndoDetective::Redo()
{
    BeginRedo();

    RedoSdrUndoAction(pDrawUndo.get());

    ScDocument& rDoc = rDocShell.GetDocument();

    if (bIsDelete)
        rDoc.ClearDetectiveOperations();
    else
        rDoc.AddDetectiveOperation( ScDetOpData( aPos, static_cast<ScDetOpType>(nAction) ) );

    ScTabViewShell* pViewShell = ScTabViewShell::GetActiveViewShell();
    if (pViewShell)
        pViewShell->RecalcPPT();    //! use broadcast instead?

    EndRedo();
}

void ScUndoDetective::Repeat(SfxRepeatTarget& /* rTarget */)
{
    // makes no sense
}

bool ScUndoDetective::CanRepeat(SfxRepeatTarget& /* rTarget */) const
{
    return false;
}

ScUndoRangeNames::ScUndoRangeNames( ScDocShell& rNewDocShell,
                                    std::unique_ptr<ScRangeName> pOld, std::unique_ptr<ScRangeName> pNew, SCTAB nTab ) :
    ScSimpleUndo( rNewDocShell ),
    pOldRanges  ( std::move(pOld) ),
    pNewRanges  ( std::move(pNew) ),
    mnTab       ( nTab )
{
}

ScUndoRangeNames::~ScUndoRangeNames()
{
    pOldRanges.reset();
    pNewRanges.reset();
}

OUString ScUndoRangeNames::GetComment() const
{
    return ScResId( STR_UNDO_RANGENAMES );
}

void ScUndoRangeNames::DoChange( bool bUndo )
{
    ScDocument& rDoc = rDocShell.GetDocument();
    rDoc.PreprocessRangeNameUpdate();

    if ( bUndo )
    {
        auto p = std::make_unique<ScRangeName>(*pOldRanges);
        if (mnTab >= 0)
            rDoc.SetRangeName( mnTab, std::move(p) );
        else
            rDoc.SetRangeName( std::move(p) );
    }
    else
    {
        auto p = std::make_unique<ScRangeName>(*pNewRanges);
        if (mnTab >= 0)
            rDoc.SetRangeName( mnTab, std::move(p) );
        else
            rDoc.SetRangeName( std::move(p) );
    }

    rDoc.CompileHybridFormula();

    SfxGetpApp()->Broadcast( SfxHint( SfxHintId::ScAreasChanged ) );
}

void ScUndoRangeNames::Undo()
{
    BeginUndo();
    DoChange( true );
    EndUndo();
}

void ScUndoRangeNames::Redo()
{
    BeginRedo();
    DoChange( false );
    EndRedo();
}

void ScUndoRangeNames::Repeat(SfxRepeatTarget& /* rTarget */)
{
    // makes no sense
}

bool ScUndoRangeNames::CanRepeat(SfxRepeatTarget& /* rTarget */) const
{
    return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
