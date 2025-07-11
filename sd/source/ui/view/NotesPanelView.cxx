/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <NotesPanelViewShell.hxx>
#include <NotesPanelView.hxx>
#include <OutlineView.hxx>
#include <ViewShellBase.hxx>
#include <editeng/editeng.hxx>
#include <editeng/outliner.hxx>
#include <sdresid.hxx>
#include <editeng/editund2.hxx>
#include <sdpage.hxx>
#include <DrawViewShell.hxx>
#include <DrawDocShell.hxx>
#include <ToolBarManager.hxx>
#include <Window.hxx>
#include <drawdoc.hxx>
#include <sdmod.hxx>
#include <officecfg/Office/Common.hxx>
#include <EventMultiplexer.hxx>
#include <app.hrc>
#include <strings.hrc>

namespace sd
{
NotesPanelView::NotesPanelView(DrawDocShell& rDocSh, vcl::Window* pWindow,
                               NotesPanelViewShell& rNotesPanelViewShell)
    : ::sd::SimpleOutlinerView(*rDocSh.GetDoc(), pWindow->GetOutDev(), &rNotesPanelViewShell)
    , mrNotesPanelViewShell(rNotesPanelViewShell)
    , maOutliner(mrDoc, OutlinerMode::TextObject)
    , maOutlinerView(maOutliner, pWindow)
    , aModifyIdle("NotesEditWindow ModifyIdle")
{
    aModifyIdle.SetInvokeHandler(LINK(this, NotesPanelView, ModifyTimerHdl));
    aModifyIdle.SetPriority(TaskPriority::LOWEST);

    maOutliner.Init(OutlinerMode::OutlineView);
    maOutliner.SetRefDevice(SdModule::get()->GetVirtualRefDevice());
    maOutliner.SetPaperSize(mrNotesPanelViewShell.GetActiveWindow()->GetViewSize());

    maOutlinerView.SetOutputArea(
        ::tools::Rectangle{ Point(0, 0), mrNotesPanelViewShell.GetActiveWindow()->GetViewSize() });
    maOutliner.InsertView(&maOutlinerView, EE_APPEND);

    onUpdateStyleSettings();

    // fill Outliner with contents
    FillOutliner();

    mrNotesPanelViewShell.GetViewShellBase().GetEventMultiplexer()->AddEventListener(
        LINK(this, NotesPanelView, EventMultiplexerListener));

    // TODO: UNDO
    // sd::UndoManager* pDocUndoMgr = dynamic_cast<sd::UndoManager*>(mpDocSh->GetUndoManager());
    // if (pDocUndoMgr != nullptr)
    //     pDocUndoMgr->SetLinkedUndoManager(&maOutliner.GetUndoManager());
}

NotesPanelView::~NotesPanelView()
{
    mrNotesPanelViewShell.GetViewShellBase().GetEventMultiplexer()->RemoveEventListener(
        LINK(this, NotesPanelView, EventMultiplexerListener));

    ResetLinks();
    // DisconnectFromApplication();
    // mpProgress.reset();
}

void NotesPanelView::FillOutliner()
{
    maOutliner.GetUndoManager().Clear();
    maOutliner.EnableUndo(false);
    ResetLinks();
    maOutliner.Clear();

    SdrTextObj* pNotesTextObj = getNotesTextObj();
    if (!pNotesTextObj)
        return;

    getNotesFromDoc();
    SetLinks();
    maOutliner.EnableUndo(true);
}

SdrTextObj* NotesPanelView::getNotesTextObj()
{
    SdPage* pNotesPage = mrNotesPanelViewShell.getCurrentPage();
    if (!pNotesPage)
        return nullptr;

    SdrObject* pNotesObj = pNotesPage->GetPresObj(PresObjKind::Notes);
    if (!pNotesObj)
        return nullptr;

    return dynamic_cast<SdrTextObj*>(pNotesObj);
}

void NotesPanelView::SetLinks()
{
    maOutliner.SetStatusEventHdl(LINK(this, NotesPanelView, StatusEventHdl));
}

void NotesPanelView::ResetLinks() { maOutliner.SetStatusEventHdl(Link<EditStatus&, void>()); }

void NotesPanelView::getNotesFromDoc()
{
    SdrTextObj* pNotesTextObj = getNotesTextObj();
    if (!pNotesTextObj)
        return;

    // Ignore notifications that will rebound from updating the text
    maOutliner.SetModifyHdl(Link<LinkParamNone*, void>());

    if (OutlinerParaObject* pPara = pNotesTextObj->GetOutlinerParaObject())
        maOutliner.SetText(*pPara);

    maOutliner.SetModifyHdl(LINK(this, NotesPanelView, EditModifiedHdl));
}

void NotesPanelView::setNotesToDoc()
{
    SdrTextObj* pNotesTextObj = getNotesTextObj();
    if (!pNotesTextObj)
        return;

    std::optional<OutlinerParaObject> pNewText = maOutliner.CreateParaObject();
    pNotesTextObj->SetOutlinerParaObject(std::move(pNewText));
    if (pNotesTextObj->IsEmptyPresObj())
        pNotesTextObj->SetEmptyPresObj(false);
}

void NotesPanelView::Paint(const ::tools::Rectangle& rRect, ::sd::Window const* /*pWin*/)
{
    maOutlinerView.DrawText_ToEditView(rRect);
}

OutlinerView* NotesPanelView::GetOutlinerView() { return &maOutlinerView; }

void NotesPanelView::onUpdateStyleSettings()
{
    svtools::ColorConfig aColorConfig;
    const Color aDocColor(aColorConfig.GetColorValue(svtools::DOCCOLOR).nColor);

    maOutlinerView.SetBackgroundColor(aDocColor);
    if (vcl::Window* pWindow = maOutlinerView.GetWindow())
        pWindow->SetBackground(Wallpaper(aDocColor));

    maOutliner.SetBackgroundColor(aDocColor);
}

void NotesPanelView::onResize()
{
    ::sd::Window* pWin = mrNotesPanelViewShell.GetActiveWindow();
    if (!pWin)
        return;

    OutlinerView* pOutlinerView = GetOutlinerView();
    if (!pOutlinerView)
        return;

    Size aOutputSize = pWin->PixelToLogic(pWin->GetOutputSizePixel());

    pOutlinerView->SetOutputArea({ Point(0, 0), aOutputSize });
    maOutliner.SetPaperSize(aOutputSize);
    pOutlinerView->ShowCursor();

    const ::tools::Long nMaxVisAreaStart = maOutliner.GetTextHeight() - aOutputSize.Height();

    ::tools::Rectangle aVisArea(pOutlinerView->GetVisArea());

    if (aVisArea.Top() > nMaxVisAreaStart)
    {
        aVisArea.SetTop(std::max<::tools::Long>(nMaxVisAreaStart, 0));
        aVisArea.SetSize(aOutputSize);
        pOutlinerView->SetVisArea(aVisArea);
        pOutlinerView->ShowCursor();
    }

    if (!aVisArea.IsEmpty()) // not when opening
    {
        mrNotesPanelViewShell.InitWindows(Point(0, 0), aVisArea.GetSize(), aVisArea.TopLeft(),
                                          true);
        mrNotesPanelViewShell.UpdateScrollBars();
    }
}

void NotesPanelView::onGrabFocus()
{
    if (mbInFocus)
        return;
    mbInFocus = true;

    SdrTextObj* pNotesTextObj = getNotesTextObj();
    if (pNotesTextObj && pNotesTextObj->IsEmptyPresObj())
    {
        // clear the "Click to add Notes" text on entering the window.
        maOutliner.SetToEmptyText();
    }
}

void NotesPanelView::onLoseFocus()
{
    if (!mbInFocus)
        return;
    mbInFocus = false;

    aModifyIdle.Stop();
    SdrTextObj* pNotesTextObj = getNotesTextObj();
    if (pNotesTextObj)
    {
        if (!maOutliner.GetEditEngine().HasText())
        {
            // if the notes are empty restore the placeholder text and state.
            SdPage* pPage = dynamic_cast<SdPage*>(pNotesTextObj->getSdrPageFromSdrObject());
            if (pPage)
                pPage->RestoreDefaultText(pNotesTextObj);
        }
        else
            setNotesToDoc();
    }
}

/**
 * Handler for StatusEvents
 */
IMPL_LINK_NOARG(NotesPanelView, StatusEventHdl, EditStatus&, void) { onResize(); }

IMPL_LINK_NOARG(NotesPanelView, EditModifiedHdl, LinkParamNone*, void)
{
    // EditEngine calls ModifyHdl many times in succession for some edits.
    // (e.g. when deleting multiple lines)
    // Debounce the rapid ModifyHdl calls using a timer.
    aModifyIdle.Start();
    return;
}

IMPL_LINK_NOARG(NotesPanelView, ModifyTimerHdl, Timer*, void)
{
    setNotesToDoc();
    aModifyIdle.Stop();
}

IMPL_LINK(NotesPanelView, EventMultiplexerListener, tools::EventMultiplexerEvent&, rEvent, void)
{
    switch (rEvent.meEventId)
    {
        case EventMultiplexerEventId::CurrentPageChanged:
        case EventMultiplexerEventId::MainViewRemoved:
        case EventMultiplexerEventId::MainViewAdded:
            FillOutliner();
            onResize();
            break;
        default:
            break;
    }
}

OutlinerView* NotesPanelView::GetViewByWindow(vcl::Window const* /*pWin*/) const
{
    return const_cast<NotesPanelView*>(this)->GetOutlinerView();
}

/**
 * Set attributes of the selected text
 */
bool NotesPanelView::SetAttributes(const SfxItemSet& rSet, bool /*bSlide*/, bool /*bReplaceAll*/,
                                   bool /*bMaster*/)
{
    bool bOk = false;

    OutlinerView* pOlView = GetOutlinerView();

    if (pOlView)
    {
        pOlView->SetAttribs(rSet);
        bOk = true;
    }

    mrNotesPanelViewShell.Invalidate(SID_PREVIEW_STATE);

    return bOk;
}

/**
 * Get attributes of the selected text
 */
void NotesPanelView::GetAttributes(SfxItemSet& rTargetSet, bool) const
{
    rTargetSet.Put(const_cast<OutlinerView&>(maOutlinerView).GetAttribs(), false);
}

SvtScriptType NotesPanelView::GetScriptType() const
{
    SvtScriptType nScriptType = ::sd::View::GetScriptType();

    std::optional<OutlinerParaObject> pTempOPObj = maOutliner.CreateParaObject();
    if (pTempOPObj)
    {
        nScriptType = pTempOPObj->GetTextObject().GetScriptType();
    }

    return nScriptType;
}

sal_Int8 NotesPanelView::AcceptDrop(const AcceptDropEvent&, DropTargetHelper&, SdrLayerID)
{
    return DND_ACTION_NONE;
}

sal_Int8 NotesPanelView::ExecuteDrop(const ExecuteDropEvent&, ::sd::Window*, sal_uInt16, SdrLayerID)
{
    return DND_ACTION_NONE;
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
