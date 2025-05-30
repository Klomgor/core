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

#include <string_view>

#include <strings.hrc>
#include <helpids.h>
#include <iderid.hxx>

#include "baside2.hxx"
#include <baside3.hxx>
#include <basidesh.hxx>
#include <basobj.hxx>
#include <iderdll.hxx>
#include "iderdll2.hxx"

#include <com/sun/star/script/XLibraryContainerPassword.hpp>
#include <sal/log.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/infobar.hxx>
#include <sfx2/passwd.hxx>
#include <sfx2/sfxsids.hrc>
#include <sfx2/viewfrm.hxx>
#include <svl/intitem.hxx>
#include <svl/stritem.hxx>
#include <svl/srchdefs.hxx>
#include <svl/itemset.hxx>
#include <utility>
#include <vcl/commandevent.hxx>
#include <vcl/event.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <tools/stream.hxx>
#include <o3tl/hash_combine.hxx>

namespace basctl
{

// ID used for the read-only infobar
constexpr OUString BASIC_IDE_READONLY_INFOBAR = u"readonly"_ustr;

using namespace ::com::sun::star::uno;
using namespace ::com::sun::star;

BaseWindow::BaseWindow( vcl::Window* pParent, ScriptDocument aDocument, OUString aLibName, OUString aName )
    :Window( pParent, WinBits( WB_3DLOOK ) )
    ,pShellHScrollBar( nullptr)
    ,pShellVScrollBar( nullptr)
    ,nStatus( 0)
    ,m_aDocument(std::move( aDocument ))
    ,m_aLibName(std::move( aLibName ))
    ,m_aName(std::move( aName ))
{
}

BaseWindow::~BaseWindow()
{
    disposeOnce();
}

void BaseWindow::dispose()
{
    if (pShellVScrollBar && !pShellVScrollBar->isDisposed())
        pShellVScrollBar->SetScrollHdl( Link<weld::Scrollbar&,void>() );
    if (pShellHScrollBar && !pShellHScrollBar->isDisposed())
        pShellHScrollBar->SetScrollHdl( Link<weld::Scrollbar&,void>() );
    pShellVScrollBar.reset();
    pShellHScrollBar.reset();
    vcl::Window::dispose();
}

void BaseWindow::Init()
{
    if ( pShellVScrollBar )
        pShellVScrollBar->SetScrollHdl( LINK( this, BaseWindow, VertScrollHdl ) );
    if ( pShellHScrollBar )
        pShellHScrollBar->SetScrollHdl( LINK( this, BaseWindow, HorzScrollHdl ) );

    // Show the read-only infobar if the module/dialog is read-only
    GetShell()->GetViewFrame().RemoveInfoBar(BASIC_IDE_READONLY_INFOBAR);
    if (IsReadOnly())
        ShowReadOnlyInfoBar();

    DoInit();   // virtual...
}

void BaseWindow::DoInit()
{
}

void BaseWindow::GrabScrollBars(ScrollAdaptor* pHScroll, ScrollAdaptor* pVScroll)
{
    pShellHScrollBar = pHScroll;
    pShellVScrollBar = pVScroll;
}

IMPL_LINK_NOARG(BaseWindow, VertScrollHdl, weld::Scrollbar&, void)
{
    DoScroll(pShellVScrollBar);
}

IMPL_LINK_NOARG(BaseWindow, HorzScrollHdl, weld::Scrollbar&, void)
{
    DoScroll(pShellHScrollBar);
}

void BaseWindow::ExecuteCommand (SfxRequest&)
{
}

void BaseWindow::ExecuteGlobal (SfxRequest&)
{
}

bool BaseWindow::EventNotify( NotifyEvent& rNEvt )
{
    bool bDone = false;

    if ( rNEvt.GetType() == NotifyEventType::KEYINPUT )
    {
        KeyEvent aKEvt = *rNEvt.GetKeyEvent();
        vcl::KeyCode aCode = aKEvt.GetKeyCode();
        sal_uInt16 nCode = aCode.GetCode();

        switch ( nCode )
        {
            case KEY_PAGEUP:
            case KEY_PAGEDOWN:
            {
                if ( aCode.IsMod1() )
                {
                    if (Shell* pShell = GetShell())
                        pShell->NextPage( nCode == KEY_PAGEUP );
                    bDone = true;
                }
            }
            break;
        }
    }

    return bDone || Window::EventNotify( rNEvt );
}

void BaseWindow::ShowShellScrollBars(bool bVisible)
{
    if (bVisible)
    {
        if (pShellHScrollBar)
        {
            pShellHScrollBar->Enable();
            pShellHScrollBar->Show();
        }
        if (pShellVScrollBar)
        {
            pShellVScrollBar->Enable();
            pShellVScrollBar->Show();
        }
    }
    else
    {
        if (pShellHScrollBar)
        {
            pShellHScrollBar->Disable();
            pShellHScrollBar->Hide();
        }
        if (pShellVScrollBar)
        {
            pShellVScrollBar->Disable();
            pShellVScrollBar->Hide();
        }
    }
}

void BaseWindow::DoScroll( Scrollable* )
{
}

void BaseWindow::StoreData()
{
}

bool BaseWindow::AllowUndo()
{
    return true;
}

void BaseWindow::UpdateData()
{
}

OUString BaseWindow::GetTitle()
{
    return OUString();
}

OUString BaseWindow::CreateQualifiedName()
{
    OUString aName;
    if ( !m_aLibName.isEmpty() )
    {
        LibraryLocation eLocation = m_aDocument.getLibraryLocation( m_aLibName );
        aName = m_aDocument.getTitle(eLocation) + "." + m_aLibName + "." +
                GetTitle();
    }
    return aName;
}

void BaseWindow::SetReadOnly (bool)
{
}

bool BaseWindow::IsReadOnly ()
{
    return false;
}

// Show the read-only warning messages for module and dialog windows
void BaseWindow::ShowReadOnlyInfoBar()
{
    OUString aMsg;
    if (dynamic_cast<ModulWindow*>(this))
        aMsg = IDEResId(RID_STR_MODULE_READONLY);
    else
        aMsg = IDEResId(RID_STR_DIALOG_READONLY);

    GetShell()->GetViewFrame().AppendInfoBar(BASIC_IDE_READONLY_INFOBAR, OUString(),
                                              aMsg, InfobarType::INFO, true);
}

void BaseWindow::BasicStarted()
{
}

void BaseWindow::BasicStopped()
{
}

bool BaseWindow::IsModified ()
{
    return true;
}

SfxUndoManager* BaseWindow::GetUndoManager()
{
    return nullptr;
}

SearchOptionFlags BaseWindow::GetSearchOptions()
{
    return SearchOptionFlags::NONE;
}

sal_uInt16 BaseWindow::StartSearchAndReplace (SvxSearchItem const&, bool)
{
    return 0;
}

void BaseWindow::OnNewDocument ()
{ }

void BaseWindow::InsertLibInfo () const
{
    if (ExtraData* pData = GetExtraData())
        pData->GetLibInfo().InsertInfo(m_aDocument, m_aLibName, m_aName, GetSbxType());
}

bool BaseWindow::Is (
    ScriptDocument const& rDocument,
    std::u16string_view rLibName, std::u16string_view rName,
    SbxItemType eSbxType, bool bFindSuspended
)
{
    if (bFindSuspended || !IsSuspended())
    {
        // any non-suspended window is ok
        if (rLibName.empty() || rName.empty() || eSbxType == SBX_TYPE_UNKNOWN)
            return true;
        // ok if the parameters match
        if (m_aDocument == rDocument && m_aLibName == rLibName && m_aName == rName && GetSbxType() == eSbxType)
            return true;
    }
    return false;
}

bool BaseWindow::HasActiveEditor () const
{
    return false;
}


// DockingWindow


// style bits for DockingWindow
WinBits const DockingWindow::StyleBits =
    WB_BORDER | WB_3DLOOK | WB_CLIPCHILDREN |
    WB_MOVEABLE | WB_SIZEABLE | WB_DOCKABLE;

DockingWindow::DockingWindow(vcl::Window* pParent, const OUString& rUIXMLDescription, const OUString& rID)
    : ResizableDockingWindow(pParent)
    , m_xBuilder(Application::CreateInterimBuilder(m_xBox.get(), rUIXMLDescription, true))
    , pLayout(nullptr)
    , nShowCount(0)
{
    m_xContainer = m_xBuilder->weld_container(rID);
}

DockingWindow::DockingWindow (Layout* pParent)
    : ResizableDockingWindow(pParent, StyleBits)
    , pLayout(pParent)
    , nShowCount(0)
{ }

DockingWindow::~DockingWindow()
{
    disposeOnce();
}

void DockingWindow::dispose()
{
    m_xContainer.reset();
    m_xBuilder.reset();
    pLayout.reset();
    ResizableDockingWindow::dispose();
}

// Sets the position and the size of the docking window. This property is saved
// when the window is floating. Called by Layout.
void DockingWindow::ResizeIfDocking (Point const& rPos, Size const& rSize)
{
    tools::Rectangle const rRect(rPos, rSize);
    if (rRect != aDockingRect)
    {
        // saving the position and the size
        aDockingRect = rRect;
        // resizing if actually docking
        if (!IsFloatingMode())
            SetPosSizePixel(rPos, rSize);
    }
}
void DockingWindow::ResizeIfDocking (Size const& rSize)
{
    ResizeIfDocking(aDockingRect.TopLeft(), rSize);
}

// Sets the parent Layout window.
// The physical parent is set only when the window is docking.
void DockingWindow::SetLayoutWindow (Layout* pLayout_)
{
    pLayout = pLayout_;
    if (!IsFloatingMode())
        SetParent(pLayout);

}

// Increases the "show" reference count.
// The window is shown when the reference count is positive.
void DockingWindow::Show (bool bShow) // = true
{
    if (bShow)
    {
        if (++nShowCount == 1)
            ResizableDockingWindow::Show();
    }
    else
    {
        if (--nShowCount == 0)
            ResizableDockingWindow::Hide();
    }
}

// Decreases the "show" reference count.
// The window is hidden when the reference count reaches zero.
void DockingWindow::Hide ()
{
    Show(false);
}

bool DockingWindow::Docking( const Point& rPos, tools::Rectangle& rRect )
{
    if (aDockingRect.Contains(rPos))
    {
        rRect.SetSize(aDockingRect.GetSize());
        return false; // dock
    }
    else // adjust old size
    {
        if (!aFloatingRect.IsEmpty())
            rRect.SetSize(aFloatingRect.GetSize());
        return true; // float
    }
}

void DockingWindow::EndDocking( const tools::Rectangle& rRect, bool bFloatMode )
{
    if ( bFloatMode )
        ResizableDockingWindow::EndDocking( rRect, bFloatMode );
    else
    {
        SetFloatingMode(false);
        DockThis();
    }
}

void DockingWindow::ToggleFloatingMode()
{
    if (IsFloatingMode())
    {
        if (!aFloatingRect.IsEmpty())
            SetPosSizePixel(
                GetParent()->ScreenToOutputPixel(aFloatingRect.TopLeft()),
                aFloatingRect.GetSize()
            );
    }
    DockThis();
}

bool DockingWindow::PrepareToggleFloatingMode()
{
    if (IsFloatingMode())
    {
        // memorize position and size on the desktop...
        aFloatingRect = tools::Rectangle(
            GetParent()->OutputToScreenPixel(GetPosPixel()),
            GetSizePixel()
        );
    }
    return true;
}

void DockingWindow::StartDocking()
{
    if (IsFloatingMode())
    {
        aFloatingRect = tools::Rectangle(
            GetParent()->OutputToScreenPixel(GetPosPixel()),
            GetSizePixel()
        );
    }
}

void DockingWindow::DockThis ()
{
    // resizing when floating -> docking
    if (!IsFloatingMode())
    {
        Point const aPos = aDockingRect.TopLeft();
        Size const aSize = aDockingRect.GetSize();
        if (aSize != GetSizePixel() || aPos != GetPosPixel())
            SetPosSizePixel(aPos, aSize);
    }

    if (pLayout)
    {
        if (!IsFloatingMode() && GetParent() != pLayout)
            SetParent(pLayout);
        pLayout->ArrangeWindows();
    }
}

TabBar::TabBar( vcl::Window* pParent ) :
    ::TabBar( pParent, WinBits( WB_3DLOOK | WB_SCROLL | WB_BORDER | WB_DRAG ) )
{
    EnableEditMode();

    SetHelpId( HID_BASICIDE_TABBAR );
}

void TabBar::MouseButtonDown( const MouseEvent& rMEvt )
{
    if ( rMEvt.IsLeft() && ( rMEvt.GetClicks() == 2 ) && !IsInEditMode() )
    {
        if (SfxDispatcher* pDispatcher = GetDispatcher())
            pDispatcher->Execute( SID_BASICIDE_MODULEDLG );
    }
    else
    {
        ::TabBar::MouseButtonDown( rMEvt ); // base class version
    }
}

void TabBar::Command( const CommandEvent& rCEvt )
{
    if ( ( rCEvt.GetCommand() == CommandEventId::ContextMenu ) && !IsInEditMode() )
    {
        Point aPos( rCEvt.IsMouseEvent() ? rCEvt.GetMousePosPixel() : Point(1,1) );
        if ( rCEvt.IsMouseEvent() )     // select right tab
        {
            Point aP = PixelToLogic( aPos );
            MouseEvent aMouseEvent( aP, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT );
            ::TabBar::MouseButtonDown( aMouseEvent ); // base class
        }
        if (SfxDispatcher* pDispatcher = GetDispatcher())
            pDispatcher->ExecutePopup(u"tabbar"_ustr, this, &aPos);
    }
}

TabBarAllowRenamingReturnCode TabBar::AllowRenaming()
{
    bool const bValid = IsValidSbxName(GetEditText());

    if ( !bValid )
    {
        std::unique_ptr<weld::MessageDialog> xError(Application::CreateMessageDialog(GetFrameWeld(),
                                                    VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_BADSBXNAME)));
        xError->run();
    }

    return bValid ? TABBAR_RENAMING_YES : TABBAR_RENAMING_NO;
}


void TabBar::EndRenaming()
{
    if ( !IsEditModeCanceled() )
    {
        SfxUInt16Item aID( SID_BASICIDE_ARG_TABID, GetEditPageId() );
        SfxStringItem aNewName( SID_BASICIDE_ARG_MODULENAME, GetEditText() );
        if (SfxDispatcher* pDispatcher = GetDispatcher())
            pDispatcher->ExecuteList( SID_BASICIDE_NAMECHANGEDONTAB,
                      SfxCallMode::SYNCHRON, { &aID, &aNewName });
    }
}


namespace
{

// helper class for sorting TabBar
struct TabBarSortHelper
{
    sal_uInt16      nPageId;
    OUString        aPageText;

    bool operator < (TabBarSortHelper const& rComp) const
    {
        return aPageText.compareToIgnoreAsciiCase(rComp.aPageText) < 0;
    }
};

} // namespace

void TabBar::Sort()
{
    Shell* pShell = GetShell();
    if (!pShell)
        return;

    Shell::WindowTable& aWindowTable = pShell->GetWindowTable();
    TabBarSortHelper aTabBarSortHelper;
    std::vector<TabBarSortHelper> aModuleList;
    std::vector<TabBarSortHelper> aDialogList;
    sal_uInt16 nPageCount = GetPageCount();
    sal_uInt16 i;

    // create module and dialog lists for sorting
    for ( i = 0; i < nPageCount; i++)
    {
        sal_uInt16 nId = GetPageId( i );
        aTabBarSortHelper.nPageId = nId;
        aTabBarSortHelper.aPageText = GetPageText( nId );
        BaseWindow* pWin = aWindowTable[ nId ].get();

        if (dynamic_cast<ModulWindow*>(pWin))
        {
            aModuleList.push_back( aTabBarSortHelper );
        }
        else if (dynamic_cast<DialogWindow*>(pWin))
        {
            aDialogList.push_back( aTabBarSortHelper );
        }
    }

    // sort module and dialog lists by page text
    std::sort( aModuleList.begin() , aModuleList.end() );
    std::sort( aDialogList.begin() , aDialogList.end() );


    sal_uInt16 nModules = sal::static_int_cast<sal_uInt16>( aModuleList.size() );
    sal_uInt16 nDialogs = sal::static_int_cast<sal_uInt16>( aDialogList.size() );

    // move module pages to new positions
    for (i = 0; i < nModules; i++)
    {
        MovePage( aModuleList[i].nPageId , i );
    }

    // move dialog pages to new positions
    for (i = 0; i < nDialogs; i++)
    {
        MovePage( aDialogList[i].nPageId , nModules + i );
    }
}

void CutLines( OUString& rStr, sal_Int32 nStartLine, sal_Int32 nLines )
{
    sal_Int32 nStartPos = 0;
    sal_Int32 nLine = 0;
    while ( nLine < nStartLine )
    {
        nStartPos = searchEOL( rStr, nStartPos );
        if( nStartPos == -1 )
            break;
        nStartPos++;    // not the \n.
        nLine++;
    }

    SAL_WARN_IF( nStartPos == -1, "basctl.basicide", "CutLines: Start line not found!" );

    if ( nStartPos == -1 )
        return;

    sal_Int32 nEndPos = nStartPos;

    for ( sal_Int32 i = 0; i < nLines; i++ )
        nEndPos = searchEOL( rStr, nEndPos+1 );

    if ( nEndPos == -1 ) // might happen at the last line
        nEndPos = rStr.getLength();
    else
        nEndPos++;

    rStr = OUString::Concat(rStr.subView( 0, nStartPos )) + rStr.subView( nEndPos );

    // erase trailing empty lines
    {
        sal_Int32 n = nStartPos;
        sal_Int32 nLen = rStr.getLength();
        while ( ( n < nLen ) && ( rStr[ n ] == LINE_SEP ||
                                  rStr[ n ] == LINE_SEP_CR ) )
        {
            n++;
        }

        if ( n > nStartPos )
        {
            rStr = OUString::Concat(rStr.subView( 0, nStartPos )) + rStr.subView( n );
        }
    }
}

sal_uInt32 CalcLineCount( SvStream& rStream )
{
    sal_uInt32 nLFs = 0;
    sal_uInt32 nCRs = 0;
    char c;

    rStream.Seek( 0 );
    rStream.ReadChar( c );
    while ( !rStream.eof() )
    {
        if ( c == '\n' )
            nLFs++;
        else if ( c == '\r' )
            nCRs++;
        rStream.ReadChar( c );
    }

    rStream.Seek( 0 );
    if ( nLFs > nCRs )
        return nLFs;
    return nCRs;
}


// LibInfo


LibInfo::LibInfo ()
{ }

LibInfo::~LibInfo ()
{ }

void LibInfo::InsertInfo (
    ScriptDocument const& rDocument,
    OUString const& rLibName,
    OUString const& rCurrentName,
    SbxItemType eCurrentType
)
{
    Key aKey(rDocument, rLibName);
    m_aMap.erase(aKey);
    m_aMap.emplace(aKey, Item(rCurrentName, eCurrentType));
}

void LibInfo::RemoveInfoFor (ScriptDocument const& rDocument)
{
    Map::iterator it = std::find_if(m_aMap.begin(), m_aMap.end(),
        [&rDocument](Map::reference rEntry) { return rEntry.first.GetDocument() == rDocument; });
    if (it != m_aMap.end())
        m_aMap.erase(it);
}

LibInfo::Item const* LibInfo::GetInfo (
    ScriptDocument const& rDocument, OUString const& rLibName
)
{
    Map::iterator it = m_aMap.find(Key(rDocument, rLibName));
    return it != m_aMap.end() ? &it->second : nullptr;
}

LibInfo::Key::Key (ScriptDocument aDocument, OUString aLibName) :
    m_aDocument(std::move(aDocument)), m_aLibName(std::move(aLibName))
{ }

bool LibInfo::Key::operator == (Key const& rKey) const
{
    return m_aDocument == rKey.m_aDocument && m_aLibName == rKey.m_aLibName;
}

size_t LibInfo::Key::Hash::operator () (Key const& rKey) const
{
    std::size_t seed = 0;
    o3tl::hash_combine(seed, rKey.m_aDocument.hashCode());
    o3tl::hash_combine(seed, rKey.m_aLibName.hashCode());
    return seed;
}

LibInfo::Item::Item (
    OUString aCurrentName,
    SbxItemType eCurrentType
) :
    m_aCurrentName(std::move(aCurrentName)),
    m_eCurrentType(eCurrentType)
{ }

static bool QueryDel(std::u16string_view rName, const OUString &rStr, weld::Widget* pParent)
{
    OUString aName = OUString::Concat("\'") + rName + "\'";
    OUString aQuery = rStr.replaceAll("XX", aName);
    std::unique_ptr<weld::MessageDialog> xQueryBox(Application::CreateMessageDialog(pParent,
                                                   VclMessageType::Question, VclButtonsType::YesNo, aQuery));
    return (xQueryBox->run() == RET_YES);
}

bool QueryDelMacro( std::u16string_view rName, weld::Widget* pParent )
{
    return QueryDel( rName, IDEResId( RID_STR_QUERYDELMACRO ), pParent );
}

bool QueryReplaceMacro( std::u16string_view rName, weld::Widget* pParent )
{
    return QueryDel( rName, IDEResId( RID_STR_QUERYREPLACEMACRO ), pParent );
}

bool QueryDelDialog( std::u16string_view rName, weld::Widget* pParent )
{
    EnsureIde();
    return QueryDel( rName, IDEResId( RID_STR_QUERYDELDIALOG ), pParent );
}

bool QueryDelLib( std::u16string_view rName, bool bRef, weld::Widget* pParent )
{
    EnsureIde();
    return QueryDel( rName, IDEResId( bRef ? RID_STR_QUERYDELLIBREF : RID_STR_QUERYDELLIB ), pParent );
}

bool QueryDelModule( std::u16string_view rName, weld::Widget* pParent )
{
    EnsureIde();
    return QueryDel( rName, IDEResId( RID_STR_QUERYDELMODULE ), pParent );
}

bool QueryPassword(weld::Widget* pDialogParent, const Reference< script::XLibraryContainer >& xLibContainer, const OUString& rLibName, OUString& rPassword, bool bRepeat, bool bNewTitle)
{
    EnsureIde();

    bool bOK = false;
    sal_uInt16 nRet = 0;

    do
    {
        // password dialog
        SfxPasswordDialog aDlg(pDialogParent);
        aDlg.SetMinLen(1);

        // set new title
        if ( bNewTitle )
        {
            OUString aTitle(IDEResId(RID_STR_ENTERPASSWORD));
            aTitle = aTitle.replaceAll("XX", rLibName);
            aDlg.set_title(aTitle);
        }

        // execute dialog
        nRet = aDlg.run();

        // verify password
        if ( nRet == RET_OK )
        {
            if ( xLibContainer.is() && xLibContainer->hasByName( rLibName ) )
            {
                Reference< script::XLibraryContainerPassword > xPasswd( xLibContainer, UNO_QUERY );
                if ( xPasswd.is() && xPasswd->isLibraryPasswordProtected( rLibName ) && !xPasswd->isLibraryPasswordVerified( rLibName ) )
                {
                    rPassword = aDlg.GetPassword();
                    bOK = xPasswd->verifyLibraryPassword( rLibName, rPassword );

                    if ( !bOK )
                    {
                        std::unique_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(pDialogParent,
                                                                       VclMessageType::Warning, VclButtonsType::Ok, IDEResId(RID_STR_WRONGPASSWORD)));
                        xErrorBox->run();
                    }
                }
            }
        }
    }
    while ( bRepeat && !bOK && nRet == RET_OK );

    return bOK;
}


} // namespace basctl

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
