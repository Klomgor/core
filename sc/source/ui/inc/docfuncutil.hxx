/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include "undobase.hxx"

#include <memory>

class ScMarkData;
class ScRange;
enum class InsertDeleteFlags : sal_Int32;

namespace sc {

class DocFuncUtil
{
public:

    static bool hasProtectedTab( const ScDocument& rDoc, const ScMarkData& rMark );

    static ScDocumentUniquePtr createDeleteContentsUndoDoc(
        ScDocument& rDoc, const ScMarkData& rMark, const ScRange& rRange,
        InsertDeleteFlags nFlags, bool bOnlyMarked );

    static void addDeleteContentsUndo(
        SfxUndoManager* pUndoMgr, ScDocShell& rDocSh, const ScMarkData& rMark,
        const ScRange& rRange, ScDocumentUniquePtr&& pUndoDoc, InsertDeleteFlags nFlags,
        const std::shared_ptr<ScSimpleUndo::DataSpansType>& pSpans,
        bool bMulti, bool bDrawUndo );

    static std::shared_ptr<ScSimpleUndo::DataSpansType> getNonEmptyCellSpans(
        const ScDocument& rDoc, const ScMarkData& rMark, const ScRange& rRange );
};

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
