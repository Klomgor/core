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

#include <cassert>

#include <uielement/toolbarmanager.hxx>
#include <uielement/popuptoolbarcontroller.hxx>

#include <framework/generictoolbarcontroller.hxx>
#include <officecfg/Office/Common.hxx>
#include <uielement/styletoolbarcontroller.hxx>
#include <properties.h>
#include <framework/sfxhelperfunctions.hxx>
#include <classes/fwkresid.hxx>
#include <classes/resource.hxx>
#include <strings.hrc>
#include <uielement/toolbarmerger.hxx>

#include <com/sun/star/ui/ItemType.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/awt/XDockableWindow.hpp>
#include <com/sun/star/frame/XLayoutManager.hpp>
#include <com/sun/star/ui/DockingArea.hpp>
#include <com/sun/star/graphic/XGraphic.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/frame/ModuleManager.hpp>
#include <com/sun/star/frame/theToolbarControllerFactory.hpp>
#include <com/sun/star/ui/ItemStyle.hpp>
#include <com/sun/star/ui/XUIElementSettings.hpp>
#include <com/sun/star/ui/XUIConfigurationPersistence.hpp>
#include <com/sun/star/ui/theModuleUIConfigurationManagerSupplier.hpp>
#include <com/sun/star/ui/XUIConfigurationManagerSupplier.hpp>
#include <com/sun/star/ui/ImageType.hpp>
#include <com/sun/star/ui/UIElementType.hpp>
#include <com/sun/star/lang/DisposedException.hpp>
#include <com/sun/star/util/URLTransformer.hpp>

#include <svtools/toolboxcontroller.hxx>
#include <unotools/cmdoptions.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <unotools/mediadescriptor.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/propertysequence.hxx>
#include <svtools/miscopt.hxx>
#include <svtools/imgdef.hxx>
#include <utility>
#include <vcl/event.hxx>
#include <vcl/graph.hxx>
#include <vcl/svapp.hxx>
#include <vcl/menu.hxx>
#include <vcl/syswin.hxx>
#include <vcl/taskpanelist.hxx>
#include <vcl/toolbox.hxx>
#include <vcl/settings.hxx>
#include <vcl/commandinfoprovider.hxx>
#include <vcl/weldutils.hxx>
#include <tools/debug.hxx>

//  namespaces

using namespace ::com::sun::star::awt;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::frame;
using namespace ::com::sun::star::graphic;
using namespace ::com::sun::star::util;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::ui;
using namespace ::com::sun::star;

namespace framework
{

const char ITEM_DESCRIPTOR_COMMANDURL[] = "CommandURL";
const char ITEM_DESCRIPTOR_VISIBLE[]    = "IsVisible";

const sal_uInt16 STARTID_CUSTOMIZE_POPUPMENU = 1000;

static css::uno::Reference< css::frame::XLayoutManager > getLayoutManagerFromFrame(
    css::uno::Reference< css::frame::XFrame > const & rFrame )
{
    css::uno::Reference< css::frame::XLayoutManager > xLayoutManager;

    Reference< XPropertySet > xPropSet( rFrame, UNO_QUERY );
    if ( xPropSet.is() )
    {
        try
        {
            xPropSet->getPropertyValue(u"LayoutManager"_ustr) >>= xLayoutManager;
        }
        catch (const RuntimeException&)
        {
            throw;
        }
        catch (const Exception&)
        {
        }
    }

    return xLayoutManager;
}
namespace
{

sal_Int16 getCurrentImageType()
{
    sal_Int16 nImageType = css::ui::ImageType::SIZE_DEFAULT;
    sal_Int16 nCurrentSymbolSize = SvtMiscOptions::GetCurrentSymbolsSize();
    if (nCurrentSymbolSize == SFX_SYMBOLS_SIZE_LARGE)
        nImageType |= css::ui::ImageType::SIZE_LARGE;
    else if (nCurrentSymbolSize == SFX_SYMBOLS_SIZE_32)
        nImageType |= css::ui::ImageType::SIZE_32;
    return nImageType;
}

class VclToolBarManager : public ToolBarManagerImpl
{
    DECL_LINK(Click, ToolBox*, void);

public:
    VclToolBarManager(VclPtr<ToolBox> pToolbar)
    : m_pToolBar(std::move(pToolbar))
    , m_bAddedToTaskPaneList(true)
    , m_pManager(nullptr)
    {}

    ~VclToolBarManager()
    {
        OSL_ASSERT( !m_bAddedToTaskPaneList );
    }

    virtual void Init() override
    {
        vcl::Window* pWindow = m_pToolBar;
        while ( pWindow && !pWindow->IsSystemWindow() )
            pWindow = pWindow->GetParent();

        if ( pWindow )
            static_cast<SystemWindow *>(pWindow)->GetTaskPaneList()->AddWindow( m_pToolBar );
    }

    virtual void Destroy() override
    {
        OSL_ASSERT( m_pToolBar != nullptr );
        SolarMutexGuard g;
        if ( m_bAddedToTaskPaneList )
        {
            vcl::Window* pWindow = m_pToolBar;
            while ( pWindow && !pWindow->IsSystemWindow() )
                pWindow = pWindow->GetParent();

            if ( pWindow )
                static_cast<SystemWindow *>(pWindow)->GetTaskPaneList()->RemoveWindow( m_pToolBar );
            m_bAddedToTaskPaneList = false;
        }

        // Delete the additional add-ons data
        for ( ToolBox::ImplToolItems::size_type i = 0; i < m_pToolBar->GetItemCount(); i++ )
        {
            ToolBoxItemId nItemId = m_pToolBar->GetItemId( i );
            if ( nItemId > ToolBoxItemId(0) )
                delete static_cast< AddonsParams* >( m_pToolBar->GetItemData( nItemId ));
        }

        // #i93173# note we can still be in one of the toolbar's handlers
        m_pToolBar->SetSelectHdl( Link<ToolBox *, void>() );
        m_pToolBar->SetActivateHdl( Link<ToolBox *, void>() );
        m_pToolBar->SetDeactivateHdl( Link<ToolBox *, void>() );
        m_pToolBar->SetClickHdl( Link<ToolBox *, void>() );
        m_pToolBar->SetDropdownClickHdl( Link<ToolBox *, void>() );
        m_pToolBar->SetDoubleClickHdl( Link<ToolBox *, void>() );
        m_pToolBar->SetStateChangedHdl( Link<StateChangedType const *, void>() );
        m_pToolBar->SetDataChangedHdl( Link<DataChangedEvent const *, void>() );

        m_pToolBar.disposeAndClear();
    }

    virtual css::uno::Reference<css::awt::XWindow> GetInterface() override
    {
        return VCLUnoHelper::GetInterface(m_pToolBar);
    }

    virtual void ConnectCallbacks(ToolBarManager* pManager) override
    {
        m_pManager = pManager;
        m_pToolBar->SetSelectHdl( LINK( pManager, ToolBarManager, Select) );
        m_pToolBar->SetClickHdl( LINK( this, VclToolBarManager, Click ) );
        m_pToolBar->SetDropdownClickHdl( LINK( pManager, ToolBarManager, DropdownClick ) );
        m_pToolBar->SetDoubleClickHdl( LINK( pManager, ToolBarManager, DoubleClick ) );
        m_pToolBar->SetStateChangedHdl( LINK( pManager, ToolBarManager, StateChanged ) );
        m_pToolBar->SetDataChangedHdl( LINK( pManager, ToolBarManager, DataChanged ) );

        m_pToolBar->SetMenuButtonHdl( LINK( pManager, ToolBarManager, MenuButton ) );
        m_pToolBar->SetMenuExecuteHdl( LINK( pManager, ToolBarManager, MenuPreExecute ) );
        m_pToolBar->GetMenu()->SetSelectHdl( LINK( pManager, ToolBarManager, MenuSelect ) );
    }

    virtual void InsertItem(ToolBoxItemId nId,
                            const OUString& rCommandURL,
                            const OUString& rTooltip,
                            const OUString& rLabel,
                            ToolBoxItemBits nItemBits) override
    {
        m_pToolBar->InsertItem( nId, rLabel, rCommandURL, nItemBits );
        m_pToolBar->SetQuickHelpText(nId, rTooltip);
        m_pToolBar->EnableItem( nId );
        m_pToolBar->SetItemState( nId, TRISTATE_FALSE );
    }

    virtual void InsertSeparator() override
    {
        m_pToolBar->InsertSeparator();
    }

    virtual void InsertSpace() override
    {
        m_pToolBar->InsertSpace();
    }

    virtual void InsertBreak() override
    {
        m_pToolBar->InsertBreak();
    }

    virtual ToolBoxItemId GetItemId(sal_uInt16 nPos) override
    {
        return m_pToolBar->GetItemId(nPos);
    }

    virtual ToolBoxItemId GetCurItemId() override
    {
        return m_pToolBar->GetCurItemId();
    }

    virtual OUString GetItemCommand(ToolBoxItemId nId) override
    {
        return m_pToolBar->GetItemCommand(nId);
    }

    virtual sal_uInt16 GetItemCount() override
    {
        return m_pToolBar->GetItemCount();
    }

    virtual void SetItemCheckable(ToolBoxItemId nId) override
    {
        m_pToolBar->SetItemBits( nId, m_pToolBar->GetItemBits( nId ) | ToolBoxItemBits::CHECKABLE );
    }

    virtual void HideItem(ToolBoxItemId nId, const OUString& /*rCommandURL*/) override
    {
        m_pToolBar->HideItem( nId );
    }

    virtual bool IsItemVisible(ToolBoxItemId nId, const OUString& /*rCommandURL*/) override
    {
        return m_pToolBar->IsItemVisible(nId);
    }

    virtual void Clear() override
    {
        m_pToolBar->Clear();
    }

    virtual void SetName(const OUString& rName) override
    {
        m_pToolBar->SetText( rName );
    }

    virtual void SetHelpId(const OUString& rHelpId) override
    {
        m_pToolBar->SetHelpId( rHelpId );
    }

    virtual bool WillUsePopupMode() override
    {
        return m_pToolBar->WillUsePopupMode();
    }

    virtual bool IsReallyVisible() override
    {
        return m_pToolBar->IsReallyVisible();
    }

    virtual void SetIconSize(ToolBoxButtonSize eSize) override
    {
        m_pToolBar->SetToolboxButtonSize(eSize);
    }

    virtual vcl::ImageType GetImageSize() override
    {
        return m_pToolBar->GetImageSize();
    }

    virtual void SetMenuType(ToolBoxMenuType eType) override
    {
        m_pToolBar->SetMenuType( eType );
    }

    virtual void MergeToolbar(ToolBoxItemId & rItemId, sal_uInt16 nFirstItem,
                              const OUString& rModuleIdentifier,
                              CommandToInfoMap& rCommandMap,
                              MergeToolbarInstruction& rInstruction) override
    {
        ReferenceToolbarPathInfo aRefPoint = ToolBarMerger::FindReferencePoint( m_pToolBar, nFirstItem, rInstruction.aMergePoint );

        // convert the sequence< sequence< propertyvalue > > structure to
        // something we can better handle. A vector with item data
        AddonToolbarItemContainer aItems;
        ToolBarMerger::ConvertSeqSeqToVector( rInstruction.aMergeToolbarItems, aItems );

        if ( aRefPoint.bResult )
        {
            ToolBarMerger::ProcessMergeOperation( m_pToolBar,
                                                    aRefPoint.nPos,
                                                    rItemId,
                                                    rCommandMap,
                                                    rModuleIdentifier,
                                                    rInstruction.aMergeCommand,
                                                    rInstruction.aMergeCommandParameter,
                                                    aItems );
        }
        else
        {
            ToolBarMerger::ProcessMergeFallback( m_pToolBar,
                                                    rItemId,
                                                    rCommandMap,
                                                    rModuleIdentifier,
                                                    rInstruction.aMergeCommand,
                                                    rInstruction.aMergeFallback,
                                                    aItems );
        }
    }

    virtual void SetItemImage(ToolBoxItemId nId,
                              const OUString& /*rCommandURL*/,
                              const Image& rImage) override
    {
        m_pToolBar->SetItemImage(nId, rImage);
    }

    virtual void UpdateSize() override
    {
        ::Size aSize = m_pToolBar->CalcWindowSizePixel();
        m_pToolBar->SetOutputSizePixel( aSize );
    }

    virtual void SetItemWindow(ToolBoxItemId nItemId, vcl::Window* pNewWindow) override
    {
        m_pToolBar->SetItemWindow( nItemId, pNewWindow );
    }

private:
    VclPtr<ToolBox> m_pToolBar;
    bool m_bAddedToTaskPaneList;
    ToolBarManager* m_pManager;
};

IMPL_LINK_NOARG(VclToolBarManager, Click, ToolBox*, void)
{
    m_pManager->OnClick();
}

class WeldToolBarManager : public ToolBarManagerImpl
{
    DECL_LINK(Click, const OUString&, void);
    DECL_LINK(ToggleMenuHdl, const OUString&, void);

public:
    WeldToolBarManager(weld::Toolbar* pToolbar,
                       weld::Builder* pBuilder)
    : m_pWeldedToolBar(pToolbar)
    , m_pBuilder(pBuilder)
    , m_pManager(nullptr)
    , m_nCurrentId(0)
    {}

    virtual void Init() override {}

    virtual void Destroy() override {}

    virtual css::uno::Reference<css::awt::XWindow> GetInterface() override
    {
        return new weld::TransportAsXWindow(m_pWeldedToolBar, m_pBuilder);
    }

    virtual void ConnectCallbacks(ToolBarManager* pManager) override
    {
        m_pManager = pManager;
        m_pWeldedToolBar->connect_clicked(LINK(this, WeldToolBarManager, Click));
        m_pWeldedToolBar->connect_menu_toggled(LINK(this, WeldToolBarManager, ToggleMenuHdl));
    }

    virtual void InsertItem(ToolBoxItemId nId,
                            const OUString& rCommandURL,
                            const OUString& rTooltip,
                            const OUString& rLabel,
                            ToolBoxItemBits /*nItemBits*/) override
    {
        m_aCommandToId[rCommandURL] = nId;
        m_aIdToCommand[nId] = rCommandURL;
        m_aCommandOrder.push_back(rCommandURL);

        m_pWeldedToolBar->insert_item(m_aCommandOrder.size(), rCommandURL);
        m_pWeldedToolBar->set_item_tooltip_text(rCommandURL, rTooltip);
        m_pWeldedToolBar->set_item_label(rCommandURL, rLabel);
        m_pWeldedToolBar->set_item_sensitive(rCommandURL, true);
        m_pWeldedToolBar->set_item_active(rCommandURL, false);
    }

    virtual void InsertSeparator() override
    {
        m_pWeldedToolBar->append_separator(u""_ustr);
    }

    virtual void InsertSpace() override {}

    virtual void InsertBreak() override {}

    virtual ToolBoxItemId GetItemId(sal_uInt16 nPos) override
    {
        return m_aCommandToId[m_aCommandOrder[nPos]];
    }

    virtual ToolBoxItemId GetCurItemId() override
    {
        return m_nCurrentId;
    }

    virtual OUString GetItemCommand(ToolBoxItemId nId) override
    {
        return m_aIdToCommand[nId];
    }

    virtual sal_uInt16 GetItemCount() override
    {
        return m_aCommandOrder.size();
    }

    virtual void SetItemCheckable(ToolBoxItemId /*nId*/) override {}

    virtual void HideItem(ToolBoxItemId /*nId*/, const OUString& rCommandURL) override
    {
        m_pWeldedToolBar->set_item_visible(rCommandURL, false);
    }

    virtual bool IsItemVisible(ToolBoxItemId /*nId*/, const OUString& rCommandURL) override
    {
        return m_pWeldedToolBar->get_item_visible(rCommandURL);
    }

    virtual void Clear() override {}

    virtual void SetName(const OUString& /*rName*/) override {}

    virtual void SetHelpId(const OUString& /*rHelpId*/) override {}

    virtual bool WillUsePopupMode() override { return true; }

    virtual bool IsReallyVisible() override { return true; }

    virtual void SetIconSize(ToolBoxButtonSize /*eSize*/) override {}

    virtual vcl::ImageType GetImageSize() override
    {
        return vcl::ImageType::Size32;
    }

    virtual void SetMenuType(ToolBoxMenuType /*eType*/) override {}

    virtual void MergeToolbar(ToolBoxItemId & /*rItemId*/,
                              sal_uInt16 /*nFirstItem*/,
                              const OUString& /*rModuleIdentifier*/,
                              CommandToInfoMap& /*rCommandMap*/,
                              MergeToolbarInstruction& /*rInstruction*/) override {}

    virtual void SetItemImage(ToolBoxItemId /*nId*/,
                              const OUString& rCommandURL,
                              const Image& rImage) override
    {
        m_pWeldedToolBar->set_item_image(rCommandURL, Graphic(rImage).GetXGraphic());
    }

    virtual void UpdateSize() override {}

    virtual void SetItemWindow(ToolBoxItemId /*nItemId*/, vcl::Window* /*pNewWindow*/) override {}

private:
    weld::Toolbar* m_pWeldedToolBar;
    weld::Builder* m_pBuilder;
    ToolBarManager* m_pManager;
    ToolBoxItemId m_nCurrentId;
    std::map<const OUString, ToolBoxItemId> m_aCommandToId;
    std::map<ToolBoxItemId, OUString> m_aIdToCommand;
    std::vector<OUString> m_aCommandOrder;
};

IMPL_LINK(WeldToolBarManager, Click, const OUString&, rCommand, void)
{
    m_nCurrentId = m_aCommandToId[rCommand];
    m_pManager->OnClick(true);
}

IMPL_LINK(WeldToolBarManager, ToggleMenuHdl, const OUString&, rCommand, void)
{
    m_nCurrentId = m_aCommandToId[rCommand];
    m_pManager->OnDropdownClick(false);
}

} // end anonymous namespace

//  XInterface, XTypeProvider, XServiceInfo

ToolBarManager::ToolBarManager( const Reference< XComponentContext >& rxContext,
                                const Reference< XFrame >& rFrame,
                                OUString aResourceName,
                                ToolBox* pToolBar ) :
    m_bDisposed( false ),
    m_bFrameActionRegistered( false ),
    m_bUpdateControllers( false ),
    m_eSymbolSize(SvtMiscOptions::GetCurrentSymbolsSize()),
    m_nContextMinPos(0),
    m_pImpl( new VclToolBarManager( pToolBar ) ),
    m_pToolBar( pToolBar ),
    m_pWeldedToolBar( nullptr ),
    m_aResourceName(std::move( aResourceName )),
    m_xFrame( rFrame ),
    m_xContext( rxContext ),
    m_aAsyncUpdateControllersTimer( "framework::ToolBarManager m_aAsyncUpdateControllersTimer" ),
    m_sIconTheme( SvtMiscOptions::GetIconTheme() )
{
    Init();
}

ToolBarManager::ToolBarManager( const Reference< XComponentContext >& rxContext,
                                const Reference< XFrame >& rFrame,
                                OUString aResourceName,
                                weld::Toolbar* pToolBar,
                                weld::Builder* pBuilder ) :
    m_bDisposed( false ),
    m_bFrameActionRegistered( false ),
    m_bUpdateControllers( false ),
    m_eSymbolSize( SvtMiscOptions::GetCurrentSymbolsSize() ),
    m_nContextMinPos(0),
    m_pImpl( new WeldToolBarManager( pToolBar, pBuilder ) ),
    m_pWeldedToolBar( pToolBar ),
    m_aResourceName(std::move( aResourceName )),
    m_xFrame( rFrame ),
    m_xContext( rxContext ),
    m_aAsyncUpdateControllersTimer( "framework::ToolBarManager m_aAsyncUpdateControllersTimer" ),
    m_sIconTheme( SvtMiscOptions::GetIconTheme() )
{
    Init();
}

void ToolBarManager::Init()
{
    OSL_ASSERT( m_xContext.is() );

    m_pImpl->Init();

    m_xToolbarControllerFactory = frame::theToolbarControllerFactory::get( m_xContext );
    m_xURLTransformer = URLTransformer::create( m_xContext );

    m_pImpl->ConnectCallbacks(this);

    if (m_eSymbolSize == SFX_SYMBOLS_SIZE_LARGE)
        m_pImpl->SetIconSize(ToolBoxButtonSize::Large);
    else if (m_eSymbolSize == SFX_SYMBOLS_SIZE_32)
        m_pImpl->SetIconSize(ToolBoxButtonSize::Size32);
    else
        m_pImpl->SetIconSize(ToolBoxButtonSize::Small);

    // enables a menu for clipped items and customization
    SvtCommandOptions aCmdOptions;
    ToolBoxMenuType nMenuType = ToolBoxMenuType::ClippedItems;
    if ( !aCmdOptions.LookupDisabled( u"CreateDialog"_ustr))
         nMenuType |= ToolBoxMenuType::Customize;

    m_pImpl->SetMenuType( nMenuType );

    // set name for testtool, the useful part is after the last '/'
    sal_Int32 idx = m_aResourceName.lastIndexOf('/');
    idx++; // will become 0 if '/' not found: use full string
    std::u16string_view aToolbarName = m_aResourceName.subView( idx );
    OUString aHelpIdAsString = ".HelpId:" + OUString::Concat(aToolbarName);
    m_pImpl->SetHelpId( aHelpIdAsString );

    m_aAsyncUpdateControllersTimer.SetTimeout( 50 );
    m_aAsyncUpdateControllersTimer.SetInvokeHandler( LINK( this, ToolBarManager, AsyncUpdateControllersHdl ) );

    SvtMiscOptions().AddListenerLink( LINK( this, ToolBarManager, MiscOptionsChanged ) );
}

ToolBarManager::~ToolBarManager()
{
    assert(!m_aAsyncUpdateControllersTimer.IsActive());
    assert(!m_pToolBar); // must be disposed by ToolbarLayoutManager
}

void ToolBarManager::Destroy()
{
    m_pImpl->Destroy();

    SvtMiscOptions().RemoveListenerLink( LINK( this, ToolBarManager, MiscOptionsChanged ) );
}

ToolBox* ToolBarManager::GetToolBar() const
{
    SolarMutexGuard g;
    return m_pToolBar;
}

void ToolBarManager::CheckAndUpdateImages()
{
    SolarMutexGuard g;
    bool bRefreshImages = false;

    sal_Int16 eNewSymbolSize = SvtMiscOptions::GetCurrentSymbolsSize();

    if (m_eSymbolSize != eNewSymbolSize )
    {
        bRefreshImages = true;
        m_eSymbolSize = eNewSymbolSize;
    }

    const OUString sCurrentIconTheme = SvtMiscOptions::GetIconTheme();
    if ( m_sIconTheme != sCurrentIconTheme )
    {
        bRefreshImages = true;
        m_sIconTheme = sCurrentIconTheme;
    }

    // Refresh images if requested
    if ( bRefreshImages )
        RefreshImages();
}

void ToolBarManager::RefreshImages()
{
    SolarMutexGuard g;

    if (m_eSymbolSize == SFX_SYMBOLS_SIZE_LARGE)
        m_pImpl->SetIconSize(ToolBoxButtonSize::Large);
    else if (m_eSymbolSize == SFX_SYMBOLS_SIZE_32)
        m_pImpl->SetIconSize(ToolBoxButtonSize::Size32);
    else
        m_pImpl->SetIconSize(ToolBoxButtonSize::Small);

    for ( auto const& it : m_aControllerMap )
    {
        Reference< XSubToolbarController > xController( it.second, UNO_QUERY );
        if ( xController.is() && xController->opensSubToolbar() )
        {
            // The button should show the last function that was selected from the
            // dropdown. The controller should know better than us what it was.
            xController->updateImage();
        }
        else
        {
            OUString aCommandURL = m_pImpl->GetItemCommand( it.first );
            vcl::ImageType eImageType = m_pImpl->GetImageSize();
            Image aImage = vcl::CommandInfoProvider::GetImageForCommand(aCommandURL, m_xFrame, eImageType);
            // Try also to query for add-on images before giving up and use an
            // empty image.
            bool bBigImages = eImageType != vcl::ImageType::Size16;
            if ( !aImage )
                aImage = Image(framework::AddonsOptions().GetImageFromURL(aCommandURL, bBigImages));
            m_pImpl->SetItemImage( it.first, aCommandURL, aImage );
        }
    }

    m_pImpl->UpdateSize();
}

void ToolBarManager::UpdateControllers()
{

    if( officecfg::Office::Common::Misc::DisableUICustomization::get() )
    {
        Any a;
        Reference< XLayoutManager > xLayoutManager;
        Reference< XPropertySet > xFramePropSet( m_xFrame, UNO_QUERY );
        if ( xFramePropSet.is() )
            a = xFramePropSet->getPropertyValue(u"LayoutManager"_ustr);
        a >>= xLayoutManager;
        Reference< XDockableWindow > xDockable( m_pImpl->GetInterface(), UNO_QUERY );
        if ( xLayoutManager.is() && xDockable.is() )
        {
            css::awt::Point aPoint;
            aPoint.X = aPoint.Y = SAL_MAX_INT32;
            xLayoutManager->dockWindow( m_aResourceName, DockingArea_DOCKINGAREA_DEFAULT, aPoint );
            xLayoutManager->lockWindow( m_aResourceName );
        }
    }

    if ( !m_bUpdateControllers )
    {
        m_bUpdateControllers = true;
        for (auto const& controller : m_aControllerMap)
        {
            try
            {
                Reference< XUpdatable > xUpdatable( controller.second, UNO_QUERY );
                if ( xUpdatable.is() )
                    xUpdatable->update();
            }
            catch (const Exception&)
            {
            }
        }
    }
    m_bUpdateControllers = false;
}

//for update toolbar controller via Support Visible
void ToolBarManager::UpdateController( const css::uno::Reference< css::frame::XToolbarController >& xController)
{

    if ( !m_bUpdateControllers )
    {
        m_bUpdateControllers = true;
        try
        {   if(xController.is())
            {
                Reference< XUpdatable > xUpdatable( xController, UNO_QUERY );
                if ( xUpdatable.is() )
                    xUpdatable->update();
            }
         }
         catch (const Exception&)
         {
         }

    }
    m_bUpdateControllers = false;
}

void ToolBarManager::frameAction( const FrameActionEvent& Action )
{
    SolarMutexGuard g;
    if ( Action.Action == FrameAction_CONTEXT_CHANGED && !m_bDisposed )
    {
        if (m_aImageController)
            m_aImageController->update();
        m_aAsyncUpdateControllersTimer.Start();
    }
}

void SAL_CALL ToolBarManager::disposing( const EventObject& Source )
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    RemoveControllers();

    if ( m_xDocImageManager.is() )
    {
        try
        {
            m_xDocImageManager->removeConfigurationListener(
                Reference< XUIConfigurationListener >(this) );
        }
        catch (const Exception&)
        {
        }
    }

    if ( m_xModuleImageManager.is() )
    {
        try
        {
            m_xModuleImageManager->removeConfigurationListener(
                Reference< XUIConfigurationListener >(this) );
        }
        catch (const Exception&)
        {
        }
    }

    m_xDocImageManager.clear();
    m_xModuleImageManager.clear();

    if ( Source.Source == Reference< XInterface >( m_xFrame, UNO_QUERY ))
        m_xFrame.clear();

    m_xContext.clear();
}

// XComponent
void SAL_CALL ToolBarManager::dispose()
{
    Reference< XComponent > xThis(this);

    {
        EventObject aEvent( xThis );
        std::unique_lock aGuard(m_mutex);
        m_aListenerContainer.disposeAndClear( aGuard, aEvent );
    }
    {
        SolarMutexGuard g;

        if (m_bDisposed)
        {
            return;
        }

        RemoveControllers();

        if ( m_xDocImageManager.is() )
        {
            try
            {
                m_xDocImageManager->removeConfigurationListener(
                    Reference< XUIConfigurationListener >(this) );
            }
            catch (const Exception&)
            {
            }
        }
        m_xDocImageManager.clear();
        if ( m_xModuleImageManager.is() )
        {
            try
            {
                m_xModuleImageManager->removeConfigurationListener(
                    Reference< XUIConfigurationListener >(this) );
            }
            catch (const Exception&)
            {
            }
        }
        m_xModuleImageManager.clear();

        if ( m_aOverflowManager.is() )
        {
            m_aOverflowManager->dispose();
            m_aOverflowManager.clear();
        }

        // We have to destroy our toolbar instance now.
        Destroy();
        m_pToolBar.reset();

        if ( m_bFrameActionRegistered && m_xFrame.is() )
        {
            try
            {
                m_xFrame->removeFrameActionListener( Reference< XFrameActionListener >(this) );
            }
            catch (const Exception&)
            {
            }
        }

        m_xFrame.clear();
        m_xContext.clear();

        // stop timer to prevent timer events after dispose
        // do it last because other calls could restart timer in StateChanged()
        m_aAsyncUpdateControllersTimer.Stop();

        m_bDisposed = true;
    }
}

void SAL_CALL ToolBarManager::addEventListener( const Reference< XEventListener >& xListener )
{
    SolarMutexGuard g;

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    if ( m_bDisposed )
        throw DisposedException();

    std::unique_lock aGuard(m_mutex);
    m_aListenerContainer.addInterface( aGuard, xListener );
}

void SAL_CALL ToolBarManager::removeEventListener( const Reference< XEventListener >& xListener )
{
    std::unique_lock aGuard(m_mutex);
    m_aListenerContainer.removeInterface( aGuard, xListener );
}

// XUIConfigurationListener
void SAL_CALL ToolBarManager::elementInserted( const css::ui::ConfigurationEvent& Event )
{
    impl_elementChanged(false,Event);
}

void SAL_CALL ToolBarManager::elementRemoved( const css::ui::ConfigurationEvent& Event )
{
    impl_elementChanged(true,Event);
}
void ToolBarManager::impl_elementChanged(bool const isRemove,
        const css::ui::ConfigurationEvent& Event)
{
    SolarMutexGuard g;

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    if ( m_bDisposed )
        return;

    Reference< XNameAccess > xNameAccess;
    sal_Int16                nImageType = sal_Int16();
    sal_Int16                nCurrentImageType = getCurrentImageType();

    if (!(( Event.aInfo >>= nImageType ) &&
        ( nImageType == nCurrentImageType ) &&
        ( Event.Element >>= xNameAccess )))
        return;

    sal_Int16 nImageInfo( 1 );
    Reference< XInterface > xIfacDocImgMgr( m_xDocImageManager, UNO_QUERY );
    if ( xIfacDocImgMgr == Event.Source )
        nImageInfo = 0;

    const Sequence< OUString > aSeq = xNameAccess->getElementNames();
    for ( OUString const & commandName : aSeq )
    {
        CommandToInfoMap::iterator pIter = m_aCommandMap.find( commandName );
        if ( pIter != m_aCommandMap.end() && ( pIter->second.nImageInfo >= nImageInfo ))
        {
            if (isRemove)
            {
                Image aImage;
                if (( pIter->second.nImageInfo == 0 ) && ( pIter->second.nImageInfo == nImageInfo ))
                {
                    // Special case: An image from the document image manager has been removed.
                    // It is possible that we have an image at our module image manager. Before
                    // we can remove our image we have to ask our module image manager.
                    Sequence< OUString > aCmdURLSeq{ pIter->first };
                    Sequence< Reference< XGraphic > > aGraphicSeq;
                    aGraphicSeq = m_xModuleImageManager->getImages( nImageType, aCmdURLSeq );
                    aImage = Image( aGraphicSeq[0] );
                }

                setToolBarImage(aImage,pIter);
            } // if (isRemove)
            else
            {
                Reference< XGraphic > xGraphic;
                if ( xNameAccess->getByName( commandName ) >>= xGraphic )
                {
                    Image aImage( xGraphic );
                    setToolBarImage(aImage,pIter);
                }
                pIter->second.nImageInfo = nImageInfo;
            }
        }
    }
}
void ToolBarManager::setToolBarImage(const Image& rImage,
        const CommandToInfoMap::const_iterator& rIter)
{
    const ::std::vector<ToolBoxItemId>& rIDs = rIter->second.aIds;
    m_pImpl->SetItemImage( rIter->second.nId, rIter->first, rImage );
    for (auto const& it : rIDs)
    {
        m_pImpl->SetItemImage(it, rIter->first, rImage);
    }
}

void SAL_CALL ToolBarManager::elementReplaced( const css::ui::ConfigurationEvent& Event )
{
    impl_elementChanged(false,Event);
}

void ToolBarManager::RemoveControllers()
{
    DBG_TESTSOLARMUTEX();
    assert(!m_bDisposed);

    m_aSubToolBarControllerMap.clear();

    if (m_aImageController)
        m_aImageController->dispose();
    m_aImageController.clear();

    // i90033
    // Remove item window pointers from the toolbar. They were
    // destroyed by the dispose() at the XComponent. This is needed
    // as VCL code later tries to access the item window data in certain
    // dtors where the item window is already invalid!
    for ( ToolBox::ImplToolItems::size_type i = 0; i < m_pImpl->GetItemCount(); i++ )
    {
        ToolBoxItemId nItemId = m_pImpl->GetItemId( i );
        if ( nItemId > ToolBoxItemId(0) )
        {
            Reference< XComponent > xComponent( m_aControllerMap[ nItemId ], UNO_QUERY );
            if ( xComponent.is() )
            {
                try
                {
                    xComponent->dispose();
                }
                catch (const Exception&)
                {
                }
            }
            m_pImpl->SetItemWindow(nItemId, nullptr);
        }
    }
    m_aControllerMap.clear();
}

void ToolBarManager::CreateControllers()
{
    Reference< XWindow > xToolbarWindow = m_pImpl->GetInterface();

    css::util::URL      aURL;
    bool                bHasDisabledEntries = SvtCommandOptions().HasEntriesDisabled();
    SvtCommandOptions   aCmdOptions;

    for ( ToolBox::ImplToolItems::size_type i = 0; i < m_pImpl->GetItemCount(); i++ )
    {
        ToolBoxItemId nId = m_pImpl->GetItemId( i );
        if ( nId == ToolBoxItemId(0) )
            continue;

        bool                     bInit( true );
        bool                     bCreate( true );
        Reference< XStatusListener > xController;

        OUString aCommandURL( m_pImpl->GetItemCommand( nId ) );
        // Command can be just an alias to another command.
        auto aProperties = vcl::CommandInfoProvider::GetCommandProperties(aCommandURL, m_aModuleIdentifier);
        OUString aRealCommandURL( vcl::CommandInfoProvider::GetRealCommandForCommand(aProperties) );
        if ( !aRealCommandURL.isEmpty() )
            aCommandURL = aRealCommandURL;

        if ( bHasDisabledEntries )
        {
            aURL.Complete = aCommandURL;
            m_xURLTransformer->parseStrict( aURL );
            if ( aCmdOptions.LookupDisabled( aURL.Path ))
            {
                m_aControllerMap[ nId ] = xController;
                m_pImpl->HideItem( nId, aCommandURL );
                continue;
            }
        }

        if ( m_xToolbarControllerFactory.is() &&
             m_xToolbarControllerFactory->hasController( aCommandURL, m_aModuleIdentifier ))
        {
            Reference<XMultiServiceFactory> xMSF(m_xContext->getServiceManager(), UNO_QUERY_THROW);
            Sequence< Any > aArgs( comphelper::InitAnyPropertySequence( {
                { "ModuleIdentifier", Any(m_aModuleIdentifier) },
                { "Frame", Any(m_xFrame) },
                { "ServiceManager", Any(xMSF) },
                { "ParentWindow", Any(xToolbarWindow) },
                { "Identifier", Any(sal_uInt16(nId)) },
            } ));
            xController.set( m_xToolbarControllerFactory->createInstanceWithArgumentsAndContext( aCommandURL, aArgs, m_xContext ),
                             UNO_QUERY );
            bInit = false; // Initialization is done through the factory service
        }

        if (( aCommandURL == ".uno:OpenUrl" ) && ( !m_pImpl->IsItemVisible(nId, aCommandURL)))
            bCreate = false;

        if ( !xController.is() && bCreate )
        {
            if ( m_pToolBar )
                xController = CreateToolBoxController( m_xFrame, m_pToolBar, nId, aCommandURL );
            if ( !xController )
            {
                if ( aCommandURL.startsWith( ".uno:StyleApply?" ) )
                {
                    xController.set( new StyleToolbarController( m_xContext, m_xFrame, aCommandURL ));
                    m_pImpl->SetItemCheckable( nId );
                }
                else if ( aCommandURL.startsWith( "private:resource/" ) )
                {
                    xController.set( new framework::GenericPopupToolbarController(m_xContext, {}) );
                }
                else if ( m_pToolBar && m_pToolBar->GetItemData( nId ) != nullptr )
                {
                    // retrieve additional parameters
                    OUString aControlType = static_cast< AddonsParams* >( m_pToolBar->GetItemData( nId ))->aControlType;
                    sal_uInt16 nWidth = static_cast< AddonsParams* >( m_pToolBar->GetItemData( nId ))->nWidth;

                    xController.set(
                        ToolBarMerger::CreateController( m_xContext,
                                                         m_xFrame,
                                                         m_pToolBar,
                                                         aCommandURL,
                                                         nId,
                                                         nWidth,
                                                         aControlType ).get(), UNO_QUERY );

                }
                else
                {
                    if ( m_pToolBar )
                        xController.set( new GenericToolbarController( m_xContext, m_xFrame, m_pToolBar, nId, aCommandURL ));
                    else
                        xController.set( new GenericToolbarController( m_xContext, m_xFrame, *m_pWeldedToolBar, aCommandURL ));
                }
            }
        }

        // Accessibility support: Set toggle button role for specific commands
        const sal_Int32 nProps = vcl::CommandInfoProvider::GetPropertiesForCommand(aCommandURL, m_aModuleIdentifier);
        if (nProps & UICOMMANDDESCRIPTION_PROPERTIES_TOGGLEBUTTON)
            m_pImpl->SetItemCheckable(nId);

        // Associate ID and controller to be able to retrieve
        // the controller from the ID later.
        m_aControllerMap[ nId ] = xController;

        // Fill sub-toolbars into our hash-map
        Reference< XSubToolbarController > xSubToolBar( xController, UNO_QUERY );
        if ( xSubToolBar.is() && xSubToolBar->opensSubToolbar() )
        {
            OUString aSubToolBarName = xSubToolBar->getSubToolbarName();
            if ( !aSubToolBarName.isEmpty() )
            {
                SubToolBarToSubToolBarControllerMap::iterator pIter =
                    m_aSubToolBarControllerMap.find( aSubToolBarName );
                if ( pIter == m_aSubToolBarControllerMap.end() )
                {
                    SubToolBarControllerVector aSubToolBarVector;
                    aSubToolBarVector.push_back( xSubToolBar );
                    m_aSubToolBarControllerMap.emplace(
                            aSubToolBarName, aSubToolBarVector );
                }
                else
                    pIter->second.push_back( xSubToolBar );
            }
        }

        Reference< XInitialization > xInit( xController, UNO_QUERY );
        if ( xInit.is() )
        {
            if ( bInit )
            {
                Reference<XMultiServiceFactory> xMSF(m_xContext->getServiceManager(), UNO_QUERY_THROW);
                Sequence< Any > aArgs( comphelper::InitAnyPropertySequence( {
                    { "Frame", Any(m_xFrame) },
                    { "CommandURL", Any(aCommandURL) },
                    { "ServiceManager", Any(xMSF) },
                    { "ParentWindow", Any(xToolbarWindow) },
                    { "ModuleIdentifier", Any(m_aModuleIdentifier) },
                    { "Identifier", Any(sal_uInt16(nId)) },
                } ));

                xInit->initialize( aArgs );
            }

            // Request an item window from the toolbar controller and set it at the VCL toolbar
            Reference< XToolbarController > xTbxController( xController, UNO_QUERY );
            if ( xTbxController.is() && xToolbarWindow.is() )
            {
                Reference< XWindow > xWindow = xTbxController->createItemWindow( xToolbarWindow );
                if ( xWindow.is() )
                {
                    VclPtr<vcl::Window> pItemWin = VCLUnoHelper::GetWindow( xWindow );
                    if ( pItemWin )
                    {
                        WindowType eType = pItemWin->GetType();
                        if ( m_pToolBar && (eType == WindowType::LISTBOX || eType == WindowType::MULTILISTBOX || eType == WindowType::COMBOBOX) )
                            pItemWin->SetAccessibleName( m_pToolBar->GetItemText( nId ) );
                        m_pImpl->SetItemWindow( nId, pItemWin );
                    }
                }
            }
        }

        //for update Controller via support visible state
        Reference< XPropertySet > xPropSet( xController, UNO_QUERY );
        if ( xPropSet.is() )
        {
            try
            {
                bool bSupportVisible = true;
                Any a( xPropSet->getPropertyValue(u"SupportsVisible"_ustr) );
                a >>= bSupportVisible;
                if (bSupportVisible)
                {
                    Reference< XToolbarController > xTbxController( xController, UNO_QUERY );
                    UpdateController(xTbxController);
                }
            }
            catch (const RuntimeException&)
            {
                throw;
            }
            catch (const Exception&)
            {
            }
        }
    }

    AddFrameActionListener();
}

void ToolBarManager::AddFrameActionListener()
{
    if ( !m_bFrameActionRegistered && m_xFrame.is() )
    {
        m_bFrameActionRegistered = true;
        m_xFrame->addFrameActionListener( Reference< XFrameActionListener >(this) );
    }
}

// static
ToolBoxItemBits ToolBarManager::ConvertStyleToToolboxItemBits( sal_Int32 nStyle )
{
    ToolBoxItemBits nItemBits( ToolBoxItemBits::NONE );
    if ( nStyle & css::ui::ItemStyle::RADIO_CHECK )
        nItemBits |= ToolBoxItemBits::RADIOCHECK;
    if ( nStyle & css::ui::ItemStyle::ALIGN_LEFT )
        nItemBits |= ToolBoxItemBits::LEFT;
    if ( nStyle & css::ui::ItemStyle::AUTO_SIZE )
        nItemBits |= ToolBoxItemBits::AUTOSIZE;
    if ( nStyle & css::ui::ItemStyle::DROP_DOWN )
        nItemBits |= ToolBoxItemBits::DROPDOWN;
    if ( nStyle & css::ui::ItemStyle::REPEAT )
        nItemBits |= ToolBoxItemBits::REPEAT;
    if ( nStyle & css::ui::ItemStyle::DROPDOWN_ONLY )
        nItemBits |= ToolBoxItemBits::DROPDOWNONLY;
    if ( nStyle & css::ui::ItemStyle::TEXT )
        nItemBits |= ToolBoxItemBits::TEXT_ONLY;
    if ( nStyle & css::ui::ItemStyle::ICON )
        nItemBits |= ToolBoxItemBits::ICON_ONLY;

    return nItemBits;
}

void ToolBarManager::InitImageManager()
{
    Reference< XModuleManager2 > xModuleManager = ModuleManager::create( m_xContext );
    if ( !m_xDocImageManager.is() )
    {
        Reference< XModel > xModel( GetModelFromFrame() );
        if ( xModel.is() )
        {
            Reference< XUIConfigurationManagerSupplier > xSupplier( xModel, UNO_QUERY );
            if ( xSupplier.is() )
            {
                Reference< XUIConfigurationManager > xDocUICfgMgr = xSupplier->getUIConfigurationManager();
                m_xDocImageManager.set( xDocUICfgMgr->getImageManager(), UNO_QUERY );
                m_xDocImageManager->addConfigurationListener(
                                        Reference< XUIConfigurationListener >(this) );
            }
        }
    }

    try
    {
        m_aModuleIdentifier = xModuleManager->identify( Reference< XInterface >( m_xFrame, UNO_QUERY ) );
    }
    catch (const Exception&)
    {
    }

    if ( !m_xModuleImageManager.is() )
    {
        Reference< XModuleUIConfigurationManagerSupplier > xModuleCfgMgrSupplier =
            theModuleUIConfigurationManagerSupplier::get( m_xContext );
        Reference< XUIConfigurationManager > xUICfgMgr = xModuleCfgMgrSupplier->getUIConfigurationManager( m_aModuleIdentifier );
        m_xModuleImageManager.set( xUICfgMgr->getImageManager(), UNO_QUERY );
        m_xModuleImageManager->addConfigurationListener( Reference< XUIConfigurationListener >(this) );
    }
}

void ToolBarManager::FillToolbar( const Reference< XIndexAccess >& rItemContainer,
                                  const Reference< XIndexAccess >& rContextData,
                                  const OUString& rContextToolbarName )
{
    OString aTbxName = OUStringToOString( m_aResourceName, RTL_TEXTENCODING_ASCII_US );
    SAL_INFO( "fwk.uielement", "framework (cd100003) ::ToolBarManager::FillToolbar " << aTbxName );

    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    InitImageManager();

    RemoveControllers();

    // reset and fill command map
    m_pImpl->Clear();
    m_aControllerMap.clear();
    m_aCommandMap.clear();

    ToolBoxItemId nId(1), nAddonId(1000);
    FillToolbarFromContainer( rItemContainer, m_aResourceName, nId, nAddonId );
    m_aContextResourceName = rContextToolbarName;
    if ( rContextData.is() )
    {
        m_pImpl->InsertSeparator();
        FillToolbarFromContainer( rContextData, m_aContextResourceName, nId, nAddonId );
    }

    // Request images for all toolbar items. Must be done before CreateControllers as
    // some controllers need access to the image.
    RequestImages();

    // Create controllers after we set the images. There are controllers which needs
    // an image at the toolbar at creation time!
    CreateControllers();

    // Notify controllers that they are now correctly initialized and can start listening
    // toolbars that will open in popup mode will be updated immediately to avoid flickering
    if( m_pImpl->WillUsePopupMode() )
        UpdateControllers();
    else if ( m_pImpl->IsReallyVisible() )
    {
        m_aAsyncUpdateControllersTimer.Start();
    }

    // Try to retrieve UIName from the container property set and set it as the title
    // if it is not empty.
    Reference< XPropertySet > xPropSet( rItemContainer, UNO_QUERY );
    if ( !xPropSet.is() )
        return;

    try
    {
        OUString aUIName;
        xPropSet->getPropertyValue(u"UIName"_ustr) >>= aUIName;
        if ( !aUIName.isEmpty() )
            m_pImpl->SetName( aUIName );
    }
    catch (const Exception&)
    {
    }
}

void ToolBarManager::FillToolbarFromContainer( const Reference< XIndexAccess >& rItemContainer,
                                               const OUString& rResourceName, ToolBoxItemId& nId, ToolBoxItemId& nAddonId )
{
    m_nContextMinPos = m_pImpl->GetItemCount();
    CommandInfo aCmdInfo;
    for ( sal_Int32 n = 0; n < rItemContainer->getCount(); n++ )
    {
        Sequence< PropertyValue >   aProps;
        OUString                    aCommandURL;
        OUString                    aLabel;
        OUString                    aTooltip;
        sal_uInt16                  nType( css::ui::ItemType::DEFAULT );
        sal_uInt32                  nStyle( 0 );

        try
        {
            if ( rItemContainer->getByIndex( n ) >>= aProps )
            {
                bool bIsVisible( true );
                for (PropertyValue const& prop : aProps)
                {
                    if ( prop.Name == ITEM_DESCRIPTOR_COMMANDURL )
                        prop.Value >>= aCommandURL;
                    else if ( prop.Name == "Label" )
                        prop.Value >>= aLabel;
                    else if ( prop.Name == "Tooltip" )
                        prop.Value >>= aTooltip;
                    else if ( prop.Name == "Type" )
                        prop.Value >>= nType;
                    else if ( prop.Name == ITEM_DESCRIPTOR_VISIBLE )
                        prop.Value >>= bIsVisible;
                    else if ( prop.Name == "Style" )
                        prop.Value >>= nStyle;
                }

                if (!aCommandURL.isEmpty() && vcl::CommandInfoProvider::IsExperimental(aCommandURL, m_aModuleIdentifier) &&
                    !officecfg::Office::Common::Misc::ExperimentalMode::get())
                {
                    continue;
                }

                if (( nType == css::ui::ItemType::DEFAULT ) && !aCommandURL.isEmpty() )
                {
                    auto aProperties = vcl::CommandInfoProvider::GetCommandProperties(aCommandURL, m_aModuleIdentifier);
                    if (!aProperties.hasElements()) // E.g., user-provided macro command?
                        aProperties = std::move(aProps); // Use existing info, including user-provided Label

                    ToolBoxItemBits nItemBits = ConvertStyleToToolboxItemBits( nStyle );

                    if ( aTooltip.isEmpty() )
                        aTooltip = vcl::CommandInfoProvider::GetTooltipForCommand(aCommandURL, aProperties, m_xFrame);

                    if ( aLabel.isEmpty() )
                        aLabel = vcl::CommandInfoProvider::GetLabelForCommand(aProperties);

                    m_pImpl->InsertItem(nId, aCommandURL, aTooltip, aLabel, nItemBits);

                    // Fill command map. It stores all our commands and from what
                    // image manager we got our image. So we can decide if we have to use an
                    // image from a notification message.
                    auto pIter = m_aCommandMap.emplace( aCommandURL, aCmdInfo );
                    if ( pIter.second )
                    {
                        aCmdInfo.nId = nId;
                        pIter.first->second.nId = nId;
                    }
                    else
                    {
                        pIter.first->second.aIds.push_back( nId );
                    }

                    if ( !bIsVisible )
                        m_pImpl->HideItem( nId, aCommandURL );

                    ++nId;
                }
                else if ( nType == css::ui::ItemType::SEPARATOR_LINE )
                {
                    m_pImpl->InsertSeparator();
                }
                else if ( nType == css::ui::ItemType::SEPARATOR_SPACE )
                {
                    m_pImpl->InsertSpace();
                }
                else if ( nType == css::ui::ItemType::SEPARATOR_LINEBREAK )
                {
                    m_pImpl->InsertBreak();
                }
            }
        }
        catch (const css::lang::IndexOutOfBoundsException&)
        {
            break;
        }
    }

    // Support add-on toolbar merging here. Working directly on the toolbar object is much
    // simpler and faster.
    MergeToolbarInstructionContainer aMergeInstructionContainer;

    // Retrieve the toolbar name from the resource name
    OUString aToolbarName( rResourceName );
    sal_Int32 nIndex = aToolbarName.lastIndexOf( '/' );
    if (( nIndex > 0 ) && ( nIndex < aToolbarName.getLength() ))
        aToolbarName = aToolbarName.copy( nIndex+1 );

    AddonsOptions().GetMergeToolbarInstructions( aToolbarName, aMergeInstructionContainer );

    if ( !aMergeInstructionContainer.empty() )
    {
        const sal_uInt32 nCount = aMergeInstructionContainer.size();
        for ( sal_uInt32 i=0; i < nCount; i++ )
        {
            MergeToolbarInstruction& rInstruction = aMergeInstructionContainer[i];
            if ( ToolBarMerger::IsCorrectContext( rInstruction.aMergeContext, m_aModuleIdentifier ))
            {
                m_pImpl->MergeToolbar(nAddonId, m_nContextMinPos, m_aModuleIdentifier, m_aCommandMap, rInstruction);
            }
        }
    }
}

void ToolBarManager::FillAddonToolbar( const Sequence< Sequence< PropertyValue > >& rAddonToolbar )
{
    if (!m_pToolBar)
        return;

    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    InitImageManager();

    RemoveControllers();

    // reset and fill command map
    m_pToolBar->Clear();
    m_aControllerMap.clear();
    m_aCommandMap.clear();

    ToolBoxItemId nId( 1 );
    CommandInfo aCmdInfo;
    for ( const Sequence< PropertyValue >& rSeq : rAddonToolbar )
    {
        OUString   aURL;
        OUString   aTitle;
        OUString   aContext;
        OUString   aTarget;
        OUString   aControlType;
        sal_uInt16 nWidth( 0 );

        ToolBarMerger::ConvertSequenceToValues( rSeq, aURL, aTitle, aTarget, aContext, aControlType, nWidth );

        if ( ToolBarMerger::IsCorrectContext( aContext, m_aModuleIdentifier ) )
        {
            if ( aURL == "private:separator" )
            {
                ToolBox::ImplToolItems::size_type nCount = m_pToolBar->GetItemCount();
                if ( nCount > 0 && m_pToolBar->GetItemType( nCount-1 ) != ToolBoxItemType::SEPARATOR )
                    m_pToolBar->InsertSeparator();
            }
            else
            {
                m_pToolBar->InsertItem( nId, aTitle, aURL );

                OUString aShortcut(vcl::CommandInfoProvider::GetCommandShortcut(aURL, m_xFrame));
                if (!aShortcut.isEmpty())
                    m_pToolBar->SetQuickHelpText(nId, aTitle + " (" + aShortcut + ")");

                // Create AddonsParams to hold additional information we will need in the future
                AddonsParams* pRuntimeItemData = new AddonsParams;
                pRuntimeItemData->aControlType = aControlType;
                pRuntimeItemData->nWidth = nWidth;
                m_pToolBar->SetItemData( nId, pRuntimeItemData );

                // Fill command map. It stores all our commands and from what
                // image manager we got our image. So we can decide if we have to use an
                // image from a notification message.
                auto pIter = m_aCommandMap.emplace( aURL, aCmdInfo );
                if ( pIter.second )
                {
                    aCmdInfo.nId = nId;
                    pIter.first->second.nId = nId;
                }
                else
                {
                    pIter.first->second.aIds.push_back( nId );
                }
                ++nId;
            }
        }
    }

    // Don't setup images yet, AddonsToolbarWrapper::populateImages does that.
    // (But some controllers might need an image at the toolbar at creation time!)
    CreateControllers();

    // Notify controllers that they are now correctly initialized and can start listening.
    UpdateControllers();
}

void ToolBarManager::FillOverflowToolbar( ToolBox const * pParent )
{
    if (!m_pToolBar)
        return;

    CommandInfo aCmdInfo;
    bool bInsertSeparator = false;
    for ( ToolBox::ImplToolItems::size_type i = 0; i < pParent->GetItemCount(); ++i )
    {
        ToolBoxItemId nId = pParent->GetItemId( i );
        if ( pParent->IsItemClipped( nId ) )
        {
            if ( bInsertSeparator )
            {
                m_pToolBar->InsertSeparator();
                bInsertSeparator = false;
            }

            const OUString aCommandURL( pParent->GetItemCommand( nId ) );
            m_pToolBar->InsertItem( nId, pParent->GetItemText( nId ), aCommandURL );
            m_pToolBar->SetQuickHelpText( nId, pParent->GetQuickHelpText( nId ) );

            // Handle possible add-on controls.
            AddonsParams* pAddonParams = static_cast< AddonsParams* >( pParent->GetItemData( nId ) );
            if ( pAddonParams )
                m_pToolBar->SetItemData( nId, new AddonsParams( *pAddonParams ) );

            // Fill command map. It stores all our commands and from what
            // image manager we got our image. So we can decide if we have to use an
            // image from a notification message.
            auto pIter = m_aCommandMap.emplace( aCommandURL, aCmdInfo );
            if ( pIter.second )
            {
                aCmdInfo.nId = nId;
                pIter.first->second.nId = nId;
            }
            else
            {
                pIter.first->second.aIds.push_back( nId );
            }
        }
        else
        {
            ToolBoxItemType eType = pParent->GetItemType( i );
            if ( m_pToolBar->GetItemCount() &&
                ( eType == ToolBoxItemType::SEPARATOR || eType == ToolBoxItemType::BREAK ) )
                bInsertSeparator = true;
        }
    }

    InitImageManager();

    // Request images for all toolbar items. Must be done before CreateControllers as
    // some controllers need access to the image.
    RequestImages();

    // Create controllers after we set the images. There are controllers which needs
    // an image at the toolbar at creation time!
    CreateControllers();

    // Notify controllers that they are now correctly initialized and can start listening
    // toolbars that will open in popup mode will be updated immediately to avoid flickering
    UpdateControllers();
}

void ToolBarManager::RequestImages()
{

    // Request images from image manager
    Sequence< OUString > aCmdURLSeq( comphelper::mapKeysToSequence(m_aCommandMap) );
    Sequence< Reference< XGraphic > > aDocGraphicSeq;
    Sequence< Reference< XGraphic > > aModGraphicSeq;

    sal_Int16 nImageType = getCurrentImageType();

    if ( m_xDocImageManager.is() )
        aDocGraphicSeq = m_xDocImageManager->getImages(nImageType, aCmdURLSeq);
    aModGraphicSeq = m_xModuleImageManager->getImages(nImageType, aCmdURLSeq);

    sal_uInt32 i = 0;
    CommandToInfoMap::iterator pIter = m_aCommandMap.begin();
    CommandToInfoMap::iterator pEnd = m_aCommandMap.end();
    while ( pIter != pEnd )
    {
        Image aImage;
        if ( aDocGraphicSeq.hasElements() )
            aImage = Image( aDocGraphicSeq[i] );
        if ( !aImage )
        {
            aImage = Image( aModGraphicSeq[i] );
            // Try also to query for add-on images before giving up and use an
            // empty image.
            if ( !aImage )
                aImage = Image(framework::AddonsOptions().GetImageFromURL(aCmdURLSeq[i], SvtMiscOptions::AreCurrentSymbolsLarge()));

            pIter->second.nImageInfo = 1; // mark image as module based
        }
        else
        {
            pIter->second.nImageInfo = 0; // mark image as document based
        }
        setToolBarImage(aImage,pIter);
        ++pIter;
        ++i;
    }

    assert(!m_aImageController); // an existing one isn't disposed here
    m_aImageController = new ImageOrientationController(m_xContext, m_xFrame, m_pImpl->GetInterface(), m_aModuleIdentifier);
    m_aImageController->update();
}

void ToolBarManager::notifyRegisteredControllers( const OUString& aUIElementName, const OUString& aCommand )
{
    SolarMutexClearableGuard aGuard;
    if ( m_aSubToolBarControllerMap.empty() )
        return;

    SubToolBarToSubToolBarControllerMap::const_iterator pIter =
        m_aSubToolBarControllerMap.find( aUIElementName );

    if ( pIter == m_aSubToolBarControllerMap.end() )
        return;

    const SubToolBarControllerVector& rSubToolBarVector = pIter->second;
    if ( rSubToolBarVector.empty() )
        return;

    SubToolBarControllerVector aNotifyVector = rSubToolBarVector;
    aGuard.clear();

    const sal_uInt32 nCount = aNotifyVector.size();
    for ( sal_uInt32 i=0; i < nCount; i++ )
    {
        try
        {
            const Reference< XSubToolbarController >& xController = aNotifyVector[i];
            if ( xController.is() )
                xController->functionSelected( aCommand );
        }
        catch (const RuntimeException&)
        {
            throw;
        }
        catch (const Exception&)
        {
        }
    }
}

void ToolBarManager::HandleClick(ClickAction eClickAction)
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    ToolBoxItemId nId( m_pImpl->GetCurItemId() );
    ToolBarControllerMap::const_iterator pIter = m_aControllerMap.find( nId );
    if ( pIter == m_aControllerMap.end() )
        return;

    Reference< XToolbarController > xController( pIter->second, UNO_QUERY );

    if ( xController.is() )
    {
        switch (eClickAction)
        {
            case ClickAction::Click:
                xController->click();
                break;

            case ClickAction::DblClick:
                xController->doubleClick();
                break;

            case ClickAction::Execute:
                xController->execute(0);
                break;
        }
    }
}

void ToolBarManager::OnClick(bool bUseExecute)
{
    if (bUseExecute)
        HandleClick(ClickAction::Execute);
    else
        HandleClick(ClickAction::Click);
}

IMPL_LINK_NOARG(ToolBarManager, DropdownClick, ToolBox *, void)
{
    OnDropdownClick(true);
}

void ToolBarManager::OnDropdownClick(bool bCreatePopupWindow)
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    ToolBoxItemId nId( m_pImpl->GetCurItemId() );
    ToolBarControllerMap::const_iterator pIter = m_aControllerMap.find( nId );
    if ( pIter == m_aControllerMap.end() )
        return;

    Reference< XToolbarController > xController( pIter->second, UNO_QUERY );

    if ( xController.is() )
    {
        if (bCreatePopupWindow)
        {
            Reference< XWindow > xWin = xController->createPopupWindow();
            if ( xWin.is() )
                xWin->setFocus();
        }
        else
        {
            xController->click();
        }
    }
}

IMPL_LINK_NOARG(ToolBarManager, DoubleClick, ToolBox *, void)
{
    HandleClick(ClickAction::DblClick);
}

Reference< XModel > ToolBarManager::GetModelFromFrame() const
{
    Reference< XController > xController = m_xFrame->getController();
    Reference< XModel > xModel;
    if ( xController.is() )
        xModel = xController->getModel();

    return xModel;
}

bool ToolBarManager::IsPluginMode() const
{
    bool bPluginMode( false );

    if ( m_xFrame.is() )
    {
        Reference< XModel > xModel = GetModelFromFrame();
        if ( xModel.is() )
        {
            Sequence< PropertyValue > aSeq = xModel->getArgs();
            utl::MediaDescriptor aMediaDescriptor( aSeq );
            bPluginMode = aMediaDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_VIEWONLY, false );
        }
    }

    return bPluginMode;
}

void ToolBarManager::AddCustomizeMenuItems(ToolBox const * pToolBar)
{
    if (!m_pToolBar)
        return;

    // No config menu entries if command ".uno:ConfigureDialog" is not enabled
    Reference< XDispatch > xDisp;
    css::util::URL aURL;
    if ( m_xFrame.is() )
    {
        Reference< XDispatchProvider > xProv( m_xFrame, UNO_QUERY );
        aURL.Complete = ".uno:ConfigureDialog";
        m_xURLTransformer->parseStrict( aURL );
        if ( xProv.is() )
            xDisp = xProv->queryDispatch( aURL, OUString(), 0 );

        if ( !xDisp.is() || IsPluginMode() )
            return;
    }

    // popup menu for quick customization
    bool bHideDisabledEntries = !officecfg::Office::Common::View::Menu::DontHideDisabledEntry::get();

    ::PopupMenu *pMenu = pToolBar->GetMenu();

    // copy all menu items 'Visible buttons, Customize toolbar, Dock toolbar,
    // Dock all Toolbars) from the loaded resource into the toolbar menu
    sal_uInt16 nGroupLen = pMenu->GetItemCount();
    if (nGroupLen)
        pMenu->InsertSeparator();

    VclPtr<PopupMenu> xVisibleItemsPopupMenu;

    if (!m_aResourceName.startsWith("private:resource/toolbar/addon_"))
    {
        pMenu->InsertItem(MENUITEM_TOOLBAR_VISIBLEBUTTON, FwkResId(STR_TOOLBAR_VISIBLE_BUTTONS));
        xVisibleItemsPopupMenu = VclPtr<PopupMenu>::Create();
        pMenu->SetPopupMenu(MENUITEM_TOOLBAR_VISIBLEBUTTON, xVisibleItemsPopupMenu);

        if (m_pToolBar->IsCustomize())
        {
            pMenu->InsertItem(MENUITEM_TOOLBAR_CUSTOMIZETOOLBAR, FwkResId(STR_TOOLBAR_CUSTOMIZE_TOOLBAR));
            pMenu->SetItemCommand(MENUITEM_TOOLBAR_CUSTOMIZETOOLBAR, u".uno:ConfigureToolboxVisible"_ustr);
        }
        pMenu->InsertSeparator();
    }

    if (pToolBar->IsFloatingMode())
    {
        pMenu->InsertItem(MENUITEM_TOOLBAR_DOCKTOOLBAR, FwkResId(STR_TOOLBAR_DOCK_TOOLBAR));
        pMenu->SetAccelKey(MENUITEM_TOOLBAR_DOCKTOOLBAR, vcl::KeyCode(KEY_F10, true, true, false, false));
    }
    else
    {
        pMenu->InsertItem(MENUITEM_TOOLBAR_UNDOCKTOOLBAR, FwkResId(STR_TOOLBAR_UNDOCK_TOOLBAR));
        pMenu->SetAccelKey(MENUITEM_TOOLBAR_UNDOCKTOOLBAR, vcl::KeyCode(KEY_F10, true, true, false, false));
    }

    pMenu->InsertItem(MENUITEM_TOOLBAR_DOCKALLTOOLBAR, FwkResId(STR_TOOLBAR_DOCK_ALL_TOOLBARS));
    pMenu->InsertSeparator();
    pMenu->InsertItem(MENUITEM_TOOLBAR_LOCKTOOLBARPOSITION, FwkResId(STR_TOOLBAR_LOCK_TOOLBAR), MenuItemBits::CHECKABLE);
    pMenu->InsertItem(MENUITEM_TOOLBAR_CLOSE, FwkResId(STR_TOOLBAR_CLOSE_TOOLBAR));

    if (m_pToolBar->IsCustomize())
    {
        bool    bIsFloating( false );

        DockingManager* pDockMgr = vcl::Window::GetDockingManager();
        if ( pDockMgr )
            bIsFloating = pDockMgr->IsFloating( m_pToolBar );

        if ( !bIsFloating )
        {
            pMenu->EnableItem(MENUITEM_TOOLBAR_DOCKALLTOOLBAR, false);
            Reference< XDockableWindow > xDockable( VCLUnoHelper::GetInterface( m_pToolBar ), UNO_QUERY );
            if( xDockable.is() )
                pMenu->CheckItem(MENUITEM_TOOLBAR_LOCKTOOLBARPOSITION, xDockable->isLocked());
        }
        else
            pMenu->EnableItem(MENUITEM_TOOLBAR_LOCKTOOLBARPOSITION, false);

        if (officecfg::Office::Common::Misc::DisableUICustomization::get())
        {
            pMenu->EnableItem(MENUITEM_TOOLBAR_VISIBLEBUTTON, false);
            pMenu->EnableItem(MENUITEM_TOOLBAR_CUSTOMIZETOOLBAR, false);
            pMenu->EnableItem(MENUITEM_TOOLBAR_LOCKTOOLBARPOSITION, false);
        }

        // Disable menu item CLOSE if the toolbar has no closer
        if( !(pToolBar->GetFloatStyle() & WB_CLOSEABLE) )
            pMenu->EnableItem(MENUITEM_TOOLBAR_CLOSE, false);

        // Temporary stores a Command --> Url map to update contextual menu with the
        // correct icons. The popup icons are by default the same as those in the
        // toolbar. They are not correct for contextual popup menu.
        std::map< OUString, Image > commandToImage;

        if (xVisibleItemsPopupMenu)
        {
            // Go through all toolbar items and add them to the context menu
            for ( ToolBox::ImplToolItems::size_type nPos = 0; nPos < m_pToolBar->GetItemCount(); ++nPos )
            {
                if ( m_pToolBar->GetItemType(nPos) == ToolBoxItemType::BUTTON )
                {
                    ToolBoxItemId nId = m_pToolBar->GetItemId(nPos);
                    OUString aCommandURL = m_pToolBar->GetItemCommand( nId );
                    xVisibleItemsPopupMenu->InsertItem( STARTID_CUSTOMIZE_POPUPMENU+nPos, m_pToolBar->GetItemText( nId ), MenuItemBits::CHECKABLE );
                    xVisibleItemsPopupMenu->CheckItem( STARTID_CUSTOMIZE_POPUPMENU+nPos, m_pToolBar->IsItemVisible( nId ) );
                    xVisibleItemsPopupMenu->SetItemCommand( STARTID_CUSTOMIZE_POPUPMENU+nPos, aCommandURL );
                    Image aImage(vcl::CommandInfoProvider::GetImageForCommand(aCommandURL, m_xFrame));
                    commandToImage[aCommandURL] = aImage;
                    xVisibleItemsPopupMenu->SetItemImage( STARTID_CUSTOMIZE_POPUPMENU+nPos, aImage );
                    vcl::KeyCode aKeyCodeShortCut = vcl::CommandInfoProvider::GetCommandKeyCodeShortcut( aCommandURL, m_xFrame );
                    xVisibleItemsPopupMenu->SetAccelKey( STARTID_CUSTOMIZE_POPUPMENU+nPos, aKeyCodeShortCut );
                }
                else
                {
                    xVisibleItemsPopupMenu->InsertSeparator();
                }
            }
        }

        // Now we go through all the contextual menu to update the icons
        // and accelerator key shortcuts
        std::map< OUString, Image >::iterator it;
        for ( sal_uInt16 nPos = 0; nPos < pMenu->GetItemCount(); ++nPos )
        {
            sal_uInt16 nId = pMenu->GetItemId( nPos );
            OUString cmdUrl = pMenu->GetItemCommand( nId );
            it = commandToImage.find( cmdUrl );
            if (it != commandToImage.end()) {
                pMenu->SetItemImage( nId, it->second );
            }
            vcl::KeyCode aKeyCodeShortCut = vcl::CommandInfoProvider::GetCommandKeyCodeShortcut( cmdUrl, m_xFrame );
            if ( aKeyCodeShortCut.GetFullCode() != 0 )
                pMenu->SetAccelKey( nId, aKeyCodeShortCut );
        }
    }

    // Set the title of the menu
    pMenu->SetText( pToolBar->GetText() );

    if ( bHideDisabledEntries )
        pMenu->RemoveDisabledEntries();
}

void ToolBarManager::ToggleButton( const OUString& rResourceName, std::u16string_view rCommand )
{
    Reference< XLayoutManager > xLayoutManager = getLayoutManagerFromFrame( m_xFrame );
    if ( !xLayoutManager.is() )
        return;

    Reference< XUIElementSettings > xUIElementSettings( xLayoutManager->getElement( rResourceName ), UNO_QUERY );
    if ( !xUIElementSettings.is() )
        return;

    Reference< XIndexContainer > xItemContainer( xUIElementSettings->getSettings( true ), UNO_QUERY );
    sal_Int32 nCount = xItemContainer->getCount();
    for ( sal_Int32 i = 0; i < nCount; i++ )
    {
        Sequence< PropertyValue > aProp;
        sal_Int32 nVisibleIndex( -1 );
        OUString aCommandURL;
        bool bVisible( false );

        if ( xItemContainer->getByIndex( i ) >>= aProp )
        {
            for ( sal_Int32 j = 0; j < aProp.getLength(); j++ )
            {
                if ( aProp[j].Name == ITEM_DESCRIPTOR_COMMANDURL )
                {
                    aProp[j].Value >>= aCommandURL;
                }
                else if ( aProp[j].Name == ITEM_DESCRIPTOR_VISIBLE )
                {
                    aProp[j].Value >>= bVisible;
                    nVisibleIndex = j;
                }
            }

            if (( aCommandURL == rCommand ) && ( nVisibleIndex >= 0 ))
            {
                // We have found the requested item, toggle the visible flag
                // and write back the configuration settings to the toolbar
                aProp.getArray()[nVisibleIndex].Value <<= !bVisible;
                try
                {
                    xItemContainer->replaceByIndex( i, Any( aProp ));
                    xUIElementSettings->setSettings( xItemContainer );
                    Reference< XPropertySet > xPropSet( xUIElementSettings, UNO_QUERY );
                    if ( xPropSet.is() )
                    {
                        Reference< XUIConfigurationPersistence > xUICfgMgr;
                        if (( xPropSet->getPropertyValue(u"ConfigurationSource"_ustr) >>= xUICfgMgr ) && ( xUICfgMgr.is() ))
                            xUICfgMgr->store();
                    }
                }
                catch (const Exception&)
                {
                }
                break;
            }
        }
    }
}

IMPL_LINK( ToolBarManager, MenuButton, ToolBox*, pToolBar, void )
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    assert( !m_aOverflowManager.is() );

    VclPtrInstance<ToolBox> pOverflowToolBar( pToolBar, WB_BORDER | WB_SCROLL );
    pOverflowToolBar->SetLineSpacing(true);
    m_aOverflowManager.set( new ToolBarManager( m_xContext, m_xFrame, OUString(), pOverflowToolBar ) );
    m_aOverflowManager->FillOverflowToolbar( pToolBar );

    ::Size aActSize( pOverflowToolBar->GetSizePixel() );
    ::Size aSize( pOverflowToolBar->CalcWindowSizePixel() );
    aSize.setWidth( aActSize.Width() );
    pOverflowToolBar->SetOutputSizePixel( aSize );

    aSize = pOverflowToolBar->CalcPopupWindowSizePixel();
    pOverflowToolBar->SetSizePixel( aSize );

    pOverflowToolBar->EnableDocking();
    pOverflowToolBar->AddEventListener( LINK( this, ToolBarManager, OverflowEventListener ) );
    vcl::Window::GetDockingManager()->StartPopupMode( pToolBar, pOverflowToolBar, FloatWinPopupFlags::AllMouseButtonClose );

    // send HOME key to subtoolbar in order to select first item if keyboard activated
    if(pToolBar->IsKeyEvent() )
    {
        ::KeyEvent aEvent( 0, vcl::KeyCode( KEY_HOME ) );
        pOverflowToolBar->KeyInput(aEvent);
    }
}

IMPL_LINK( ToolBarManager, OverflowEventListener, VclWindowEvent&, rWindowEvent, void )
{
    if ( rWindowEvent.GetId() != VclEventId::WindowEndPopupMode )
        return;

    if ( m_aOverflowManager.is() )
    {
        m_aOverflowManager->dispose();
        m_aOverflowManager.clear();
    }
}

IMPL_LINK( ToolBarManager, MenuPreExecute, ToolBox*, pToolBar, void )
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    AddCustomizeMenuItems( pToolBar );
}

IMPL_LINK( ToolBarManager, MenuSelect, Menu*, pMenu, bool )
{
    // We have to hold a reference to ourself as it is possible that we will be disposed and
    // our refcount could be zero (destruction) otherwise.
    Reference< XInterface > xKeepAlive( static_cast< OWeakObject* >( this ), UNO_QUERY );

    {
        // The guard must be in its own context as the we can get destroyed when our
        // own xInterface reference get destroyed!
        SolarMutexGuard g;

        if ( m_bDisposed )
            return true;

        switch ( pMenu->GetCurItemId() )
        {
            case MENUITEM_TOOLBAR_CUSTOMIZETOOLBAR:
            {
                Reference< XDispatch > xDisp;
                css::util::URL aURL;
                if ( m_xFrame.is() )
                {
                    Reference< XDispatchProvider > xProv( m_xFrame, UNO_QUERY );
                    aURL.Complete = ".uno:ConfigureDialog";
                    m_xURLTransformer->parseStrict( aURL );
                    if ( xProv.is() )
                        xDisp = xProv->queryDispatch( aURL, OUString(), 0 );
                }

                if ( xDisp.is() )
                {
                    Sequence< PropertyValue > aPropSeq{ comphelper::makePropertyValue(
                        u"ResourceURL"_ustr, m_aResourceName) };

                    xDisp->dispatch( aURL, aPropSeq );
                }
                break;
            }

            case MENUITEM_TOOLBAR_UNDOCKTOOLBAR:
            {
                ExecuteInfo* pExecuteInfo = new ExecuteInfo;

                pExecuteInfo->aToolbarResName = m_aResourceName;
                pExecuteInfo->nCmd            = EXEC_CMD_UNDOCKTOOLBAR;
                pExecuteInfo->xLayoutManager  = getLayoutManagerFromFrame( m_xFrame );

                Application::PostUserEvent( LINK(nullptr, ToolBarManager, ExecuteHdl_Impl), pExecuteInfo );
                break;
            }

            case MENUITEM_TOOLBAR_DOCKTOOLBAR:
            {
                ExecuteInfo* pExecuteInfo = new ExecuteInfo;

                pExecuteInfo->aToolbarResName = m_aResourceName;
                pExecuteInfo->nCmd            = EXEC_CMD_DOCKTOOLBAR;
                pExecuteInfo->xLayoutManager  = getLayoutManagerFromFrame( m_xFrame );

                Application::PostUserEvent( LINK(nullptr, ToolBarManager, ExecuteHdl_Impl), pExecuteInfo );
                break;
            }

            case MENUITEM_TOOLBAR_DOCKALLTOOLBAR:
            {
                ExecuteInfo* pExecuteInfo = new ExecuteInfo;

                pExecuteInfo->aToolbarResName = m_aResourceName;
                pExecuteInfo->nCmd            = EXEC_CMD_DOCKALLTOOLBARS;
                pExecuteInfo->xLayoutManager  = getLayoutManagerFromFrame( m_xFrame );

                Application::PostUserEvent( LINK(nullptr, ToolBarManager, ExecuteHdl_Impl), pExecuteInfo );
                break;
            }

            case MENUITEM_TOOLBAR_LOCKTOOLBARPOSITION:
            {
                Reference< XLayoutManager > xLayoutManager = getLayoutManagerFromFrame( m_xFrame );
                if ( xLayoutManager.is() )
                {
                    Reference< XDockableWindow > xDockable( VCLUnoHelper::GetInterface( m_pToolBar ), UNO_QUERY );

                    if( xDockable->isLocked() )
                        xLayoutManager->unlockWindow( m_aResourceName );
                    else
                        xLayoutManager->lockWindow( m_aResourceName );
                }
                break;
            }

            case MENUITEM_TOOLBAR_CLOSE:
            {
                ExecuteInfo* pExecuteInfo = new ExecuteInfo;

                pExecuteInfo->aToolbarResName = m_aResourceName;
                pExecuteInfo->nCmd            = EXEC_CMD_CLOSETOOLBAR;
                pExecuteInfo->xLayoutManager  = getLayoutManagerFromFrame( m_xFrame );
                pExecuteInfo->xWindow         = VCLUnoHelper::GetInterface( m_pToolBar );

                Application::PostUserEvent( LINK(nullptr, ToolBarManager, ExecuteHdl_Impl), pExecuteInfo );
                break;
            }

            default:
            {
                sal_uInt16 nId = pMenu->GetCurItemId();
                if(( nId > 0 ) && ( nId < TOOLBOX_MENUITEM_START ))
                // Items in the "enable/disable" sub-menu
                {
                    // toggle toolbar button visibility
                    OUString aCommand = pMenu->GetItemCommand( nId );
                    if (m_aContextResourceName.isEmpty() ||
                        nId - STARTID_CUSTOMIZE_POPUPMENU < m_nContextMinPos)
                        ToggleButton(m_aResourceName, aCommand);
                    else
                        ToggleButton(m_aContextResourceName, aCommand);
                }
                break;
            }
        }
    }

    return true;
}

IMPL_LINK_NOARG(ToolBarManager, Select, ToolBox *, void)
{
    if ( m_bDisposed )
        return;

    sal_Int16   nKeyModifier( static_cast<sal_Int16>(m_pToolBar->GetModifier()) );
    ToolBoxItemId nId( m_pToolBar->GetCurItemId() );

    ToolBarControllerMap::const_iterator pIter = m_aControllerMap.find( nId );
    if ( pIter != m_aControllerMap.end() )
    {
        Reference< XToolbarController > xController( pIter->second, UNO_QUERY );

        if ( xController.is() )
            xController->execute( nKeyModifier );
    }
}

IMPL_LINK( ToolBarManager, StateChanged, StateChangedType const *, pStateChangedType, void )
{
    if ( m_bDisposed )
        return;

    if ( *pStateChangedType == StateChangedType::ControlBackground )
    {
        CheckAndUpdateImages();
    }
    else if ( *pStateChangedType == StateChangedType::Visible )
    {
        if ( m_pToolBar->IsReallyVisible() )
        {
            m_aAsyncUpdateControllersTimer.Start();
        }
    }
    else if ( *pStateChangedType == StateChangedType::InitShow )
    {
        m_aAsyncUpdateControllersTimer.Start();
    }
}

IMPL_LINK( ToolBarManager, DataChanged, DataChangedEvent const *, pDataChangedEvent, void )
{
    if ((( pDataChangedEvent->GetType() == DataChangedEventType::SETTINGS )   ||
        (  pDataChangedEvent->GetType() == DataChangedEventType::DISPLAY  ))  &&
        ( pDataChangedEvent->GetFlags() & AllSettingsFlags::STYLE        ))
    {
        CheckAndUpdateImages();
    }

    for ( ToolBox::ImplToolItems::size_type nPos = 0; nPos < m_pToolBar->GetItemCount(); ++nPos )
    {
        const ToolBoxItemId nId = m_pToolBar->GetItemId(nPos);
        vcl::Window* pWindow = m_pToolBar->GetItemWindow( nId );
        if ( pWindow )
        {
            const DataChangedEvent& rDCEvt( *pDataChangedEvent );
            pWindow->DataChanged( rDCEvt );
        }
    }

    if ( !m_pToolBar->IsFloatingMode() &&
         m_pToolBar->IsVisible() )
    {
        // Resize toolbar, layout manager is resize listener and will calc
        // the layout automatically.
        ::Size aSize( m_pToolBar->CalcWindowSizePixel() );
        m_pToolBar->SetOutputSizePixel( aSize );
    }
}

IMPL_LINK_NOARG(ToolBarManager, MiscOptionsChanged, LinkParamNone*, void)
{
    CheckAndUpdateImages();
}

IMPL_LINK_NOARG(ToolBarManager, AsyncUpdateControllersHdl, Timer *, void)
{
    // The guard must be in its own context as the we can get destroyed when our
    // own xInterface reference get destroyed!
    Reference< XComponent > xThis(this);

    SolarMutexGuard g;

    if ( m_bDisposed )
        return;

    // Request to update our controllers
    m_aAsyncUpdateControllersTimer.Stop();
    UpdateControllers();
}

IMPL_STATIC_LINK( ToolBarManager, ExecuteHdl_Impl, void*, p, void )
{
    ExecuteInfo* pExecuteInfo = static_cast<ExecuteInfo*>(p);
    try
    {
        // Asynchronous execution as this can lead to our own destruction!
        if (( pExecuteInfo->nCmd == EXEC_CMD_CLOSETOOLBAR ) &&
            ( pExecuteInfo->xLayoutManager.is() ) &&
            ( pExecuteInfo->xWindow.is() ))
        {
            // Use docking window close to close the toolbar. The toolbar layout manager is
            // listener and will react correctly according to the context sensitive
            // flag of our toolbar.
            VclPtr<vcl::Window> pWin = VCLUnoHelper::GetWindow( pExecuteInfo->xWindow );
            DockingWindow* pDockWin = dynamic_cast< DockingWindow* >( pWin.get() );
            if ( pDockWin )
                pDockWin->Close();
        }
        else if (( pExecuteInfo->nCmd == EXEC_CMD_UNDOCKTOOLBAR ) &&
                 ( pExecuteInfo->xLayoutManager.is() ))
        {
            pExecuteInfo->xLayoutManager->floatWindow( pExecuteInfo->aToolbarResName );
        }
        else if (( pExecuteInfo->nCmd == EXEC_CMD_DOCKTOOLBAR ) &&
                 ( pExecuteInfo->xLayoutManager.is() ))
        {
            css::awt::Point aPoint;
            aPoint.X = aPoint.Y = SAL_MAX_INT32;
            pExecuteInfo->xLayoutManager->dockWindow( pExecuteInfo->aToolbarResName,
                                                      DockingArea_DOCKINGAREA_DEFAULT,
                                                      aPoint );
        }
        else if (( pExecuteInfo->nCmd == EXEC_CMD_DOCKALLTOOLBARS ) &&
                 ( pExecuteInfo->xLayoutManager.is() ))
        {
            pExecuteInfo->xLayoutManager->dockAllWindows( UIElementType::TOOLBAR );
        }
    }
    catch (const Exception&)
    {
    }

    delete pExecuteInfo;
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
