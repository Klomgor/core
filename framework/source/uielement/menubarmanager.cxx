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

#include <uielement/menubarmanager.hxx>
#include <uielement/styletoolbarcontroller.hxx>
#include <menuconfiguration.hxx>
#include <addonmenu.hxx>
#include <framework/addonsoptions.hxx>
#include <classes/fwkresid.hxx>
#include <strings.hrc>

#include <com/sun/star/frame/XDispatch.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/lang/DisposedException.hpp>
#include <com/sun/star/uno/XComponentContext.hpp>
#include <com/sun/star/uno/XCurrentContext.hpp>
#include <com/sun/star/frame/XPopupMenuController.hpp>
#include <com/sun/star/frame/thePopupMenuControllerFactory.hpp>
#include <com/sun/star/lang/SystemDependent.hpp>
#include <com/sun/star/ui/GlobalAcceleratorConfiguration.hpp>
#include <com/sun/star/ui/ItemType.hpp>
#include <com/sun/star/ui/theModuleUIConfigurationManagerSupplier.hpp>
#include <com/sun/star/ui/XUIConfigurationManagerSupplier.hpp>
#include <com/sun/star/ui/ItemStyle.hpp>
#include <com/sun/star/frame/status/Visibility.hpp>
#include <com/sun/star/util/URLTransformer.hpp>

#include <comphelper/lok.hxx>
#include <comphelper/propertysequence.hxx>
#include <comphelper/propertyvalue.hxx>
#include <officecfg/Office/Common.hxx>
#include <svtools/javainteractionhandler.hxx>
#include <uno/current_context.hxx>
#include <unotools/cmdoptions.hxx>
#include <toolkit/awt/vclxmenu.hxx>
#include <toolkit/helper/vclunohelper.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <vcl/sysdata.hxx>
#include <vcl/menu.hxx>
#include <vcl/settings.hxx>
#include <vcl/commandinfoprovider.hxx>
#include <vcl/window.hxx>
#include <sal/log.hxx>
#include <svtools/acceleratorexecute.hxx>
#include <svtools/miscopt.hxx>
#include <uielement/menubarmerger.hxx>
#include <tools/urlobj.hxx>

using namespace ::cppu;
using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::util;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::frame;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::ui;

const sal_uInt16 ADDONMENU_MERGE_ITEMID_START = 1500;
const sal_uInt16 ITEMID_ADDONLIST             = 6678; // used to be a SID in sfx2, now just a unique id...

namespace framework
{

constexpr OUString aCmdHelpIndex = u".uno:HelpIndex"_ustr;
constexpr OUStringLiteral aCmdToolsMenu = u".uno:ToolsMenu";
constexpr OUStringLiteral aCmdHelpMenu = u".uno:HelpMenu";
constexpr OUStringLiteral aSpecialWindowCommand = u".uno:WindowList";

MenuBarManager::MenuBarManager(
    const Reference< XComponentContext >& rxContext,
    const Reference< XFrame >& rFrame,
    const Reference< XURLTransformer >& _xURLTransformer,
    const Reference< XDispatchProvider >& rDispatchProvider,
    const OUString& rModuleIdentifier,
    Menu* pMenu, bool bDelete, bool bHasMenuBar ):
      m_bRetrieveImages( false )
    , m_bAcceleratorCfg( false )
    , m_bHasMenuBar( bHasMenuBar )
    , m_xContext(rxContext)
    , m_xURLTransformer(_xURLTransformer)
    , m_sIconTheme( SvtMiscOptions::GetIconTheme() )
    , m_aAsyncSettingsTimer( "framework::MenuBarManager::Deactivate m_aAsyncSettingsTimer" )
{
    m_xPopupMenuControllerFactory = frame::thePopupMenuControllerFactory::get(m_xContext);
    FillMenuManager( pMenu, rFrame, rDispatchProvider, rModuleIdentifier, bDelete );
}

Any SAL_CALL MenuBarManager::getMenuHandle( const Sequence< sal_Int8 >& /*ProcessId*/, sal_Int16 SystemType )
{
    SolarMutexGuard aSolarGuard;

    if ( m_bDisposed )
        throw css::lang::DisposedException();

    Any a;

    if ( m_pVCLMenu )
    {
        SystemMenuData aSystemMenuData;

        m_pVCLMenu->GetSystemMenuData(aSystemMenuData);
#ifdef _WIN32
        if( SystemType == SystemDependent::SYSTEM_WIN32 )
        {
            a <<= sal_Int64(
                reinterpret_cast<sal_IntPtr>(aSystemMenuData.hMenu));
        }
#else
        (void) SystemType;
#endif
    }

    return a;
}

MenuBarManager::~MenuBarManager()
{
    // stop asynchronous settings timer
    m_xDeferredItemContainer.clear();
    m_aAsyncSettingsTimer.Stop();

    SAL_WARN_IF( OWeakObject::m_refCount != 0, "fwk.uielement", "Who wants to delete an object with refcount > 0!" );
}

// XComponent
void MenuBarManager::disposing(std::unique_lock<std::mutex>& )
{
    Reference< XComponent > xThis( this );

    SolarMutexGuard g;

    // stop asynchronous settings timer and
    // release deferred item container reference
    m_aAsyncSettingsTimer.Stop();
    m_xDeferredItemContainer.clear();
    RemoveListener();

    m_aMenuItemHandlerVector.clear();

    if ( m_bDeleteMenu )
    {
        m_pVCLMenu.disposeAndClear();
    }

    if ( m_xDocImageManager.is() )
    {
        try
        {
            m_xDocImageManager->removeConfigurationListener(
                Reference< XUIConfigurationListener >(this) );
        }
        catch ( const Exception& )
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
        catch ( const Exception& )
        {
        }
    }
    m_xDocImageManager.clear();
    m_xModuleImageManager.clear();
    m_xGlobalAcceleratorManager.clear();
    m_xModuleAcceleratorManager.clear();
    m_xDocAcceleratorManager.clear();
    m_xPopupMenuControllerFactory.clear();
    m_xContext.clear();
}

void SAL_CALL MenuBarManager::elementInserted( const css::ui::ConfigurationEvent& Event )
{
    SolarMutexGuard g;

    /* SAFE AREA ----------------------------------------------------------------------------------------------- */
    if ( m_bDisposed )
        return;

    sal_Int16 nImageType = sal_Int16();
    if (( Event.aInfo >>= nImageType ) && nImageType == 0 )
        RequestImages();
}

void SAL_CALL MenuBarManager::elementRemoved( const css::ui::ConfigurationEvent& Event )
{
    elementInserted(Event);
}

void SAL_CALL MenuBarManager::elementReplaced( const css::ui::ConfigurationEvent& Event )
{
    elementInserted(Event);
}

// XFrameActionListener
void SAL_CALL MenuBarManager::frameAction( const FrameActionEvent& Action )
{
    SolarMutexGuard g;

    if ( m_bDisposed )
        throw css::lang::DisposedException();

    if ( Action.Action != FrameAction_CONTEXT_CHANGED )
        return;

    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        // Clear dispatch reference as we will requery it later
        if ( menuItemHandler->xMenuItemDispatch.is() )
        {
            URL aTargetURL;
            aTargetURL.Complete = menuItemHandler->aMenuItemURL;
            m_xURLTransformer->parseStrict( aTargetURL );

            menuItemHandler->xMenuItemDispatch->removeStatusListener( this, aTargetURL );
        }
        menuItemHandler->xMenuItemDispatch.clear();
    }
}

// XStatusListener
void SAL_CALL MenuBarManager::statusChanged( const FeatureStateEvent& Event )
{
    OUString aFeatureURL = Event.FeatureURL.Complete;

    SolarMutexGuard aSolarGuard;
    {
        if ( m_bDisposed )
            return;

        // We have to check all menu entries as there can be identical entries in a popup menu.
        for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
        {
            if ( menuItemHandler->aParsedItemURL == aFeatureURL )
            {
                bool            bCheckmark( false );
                bool            bMenuItemEnabled( m_pVCLMenu->IsItemEnabled( menuItemHandler->nItemId ));
                bool            bEnabledItem( Event.IsEnabled );
                OUString       aItemText;
                status::Visibility  aVisibilityStatus;

                #ifdef UNIX
                //enable some slots hardly, because UNIX clipboard does not notify all changes
                // Can be removed if follow up task will be fixed directly within applications.
                // Note: PasteSpecial is handled specifically by calc
                // Calc also disables Paste under some circumstances, do not override.
                /* TODO: is this workaround even needed anymore? Was introduced
                 * in 2009 with commit 426ab2c0e8f6e3fe2b766f74f6b8da873d860260
                 * as some "metropatch" and the other places it touched seem to
                 * be gone. */
                if (!comphelper::LibreOfficeKit::isActive() &&
                    ((menuItemHandler->aMenuItemURL == ".uno:Paste" &&
                            m_aModuleIdentifier != "com.sun.star.sheet.SpreadsheetDocument")
                        || menuItemHandler->aMenuItemURL == ".uno:PasteClipboard"))    // special for draw/impress
                    bEnabledItem = true;
                #endif

                // Enable/disable item
                if ( bEnabledItem != bMenuItemEnabled )
                {
                    m_pVCLMenu->EnableItem( menuItemHandler->nItemId, bEnabledItem );

                    // Remove "checked" mark for disabled menu items.
                    // Initially disabled but checkable menu items do not receive
                    // checked/unchecked state, so can appear inconsistently after
                    // enabling/disabling. Since we can not pass checked state for disabled
                    // items, we will just reset checked state for them, anyway correct state
                    // will be transferred from controller once item enabled.
                    if ( !bEnabledItem && m_pVCLMenu->IsItemChecked( menuItemHandler->nItemId ) )
                        m_pVCLMenu->CheckItem( menuItemHandler->nItemId, false );
                }

                if ( Event.State >>= bCheckmark )
                {
                    // Checkmark or RadioButton
                    m_pVCLMenu->CheckItem( menuItemHandler->nItemId, bCheckmark );
                    // If not already designated RadioButton set as CheckMark
                    MenuItemBits nBits = m_pVCLMenu->GetItemBits( menuItemHandler->nItemId );
                    if (!(nBits & MenuItemBits::RADIOCHECK))
                        m_pVCLMenu->SetItemBits( menuItemHandler->nItemId, nBits | MenuItemBits::CHECKABLE );

                    if ( menuItemHandler->bMadeInvisible )
                        m_pVCLMenu->ShowItem( menuItemHandler->nItemId );
                }
                else if ( Event.State >>= aItemText )
                {
                    INetURLObject aURL( aFeatureURL );
                    OUString aEnumPart = aURL.GetURLPath().getToken( 1, '.' );
                    if ( !aEnumPart.isEmpty() && aURL.GetProtocol() == INetProtocol::Uno )
                    {
                        // Checkmark or RadioButton
                        m_pVCLMenu->CheckItem( menuItemHandler->nItemId, aItemText == aEnumPart );
                        // If not already designated RadioButton set as CheckMark
                        MenuItemBits nBits = m_pVCLMenu->GetItemBits( menuItemHandler->nItemId );
                        if (!(nBits & MenuItemBits::RADIOCHECK))
                            m_pVCLMenu->SetItemBits( menuItemHandler->nItemId, nBits | MenuItemBits::CHECKABLE );
                    }
                    else
                    {
                        // Replacement for place holders
                        if ( aItemText.startsWith("($1)") )
                        {
                            aItemText = FwkResId(STR_UPDATEDOC) + " " + aItemText.subView( 4 );
                        }
                        else if ( aItemText.startsWith("($2)") )
                        {
                            aItemText = FwkResId(STR_CLOSEDOC_ANDRETURN) + aItemText.subView( 4 );
                        }
                        else if ( aItemText.startsWith("($3)") )
                        {
                            aItemText = FwkResId(STR_SAVECOPYDOC) + aItemText.subView( 4 );
                        }

                        m_pVCLMenu->SetItemText( menuItemHandler->nItemId, aItemText );
                    }

                    if ( menuItemHandler->bMadeInvisible )
                        m_pVCLMenu->ShowItem( menuItemHandler->nItemId );
                }
                else if ( Event.State >>= aVisibilityStatus )
                {
                    // Visibility
                    m_pVCLMenu->ShowItem( menuItemHandler->nItemId, aVisibilityStatus.bVisible );
                    menuItemHandler->bMadeInvisible = !aVisibilityStatus.bVisible;
                }
                else if ( menuItemHandler->bMadeInvisible )
                    m_pVCLMenu->ShowItem( menuItemHandler->nItemId );
            }

            if ( Event.Requery )
            {
                // Release dispatch object - will be required on the next activate!
                menuItemHandler->xMenuItemDispatch.clear();
            }
        }
    }
}

// Helper to retrieve own structure from item ID
MenuBarManager::MenuItemHandler* MenuBarManager::GetMenuItemHandler( sal_uInt16 nItemId )
{
    SolarMutexGuard g;

    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        if ( menuItemHandler->nItemId == nItemId )
            return menuItemHandler.get();
    }

    return nullptr;
}

// Helper to set request images flag
void MenuBarManager::RequestImages()
{

    m_bRetrieveImages = true;
    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        if ( menuItemHandler->xSubMenuManager.is() )
            menuItemHandler->xSubMenuManager->RequestImages();
    }
}

// Helper to reset objects to prepare shutdown
void MenuBarManager::RemoveListener()
{
    SolarMutexGuard g;

    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        if ( menuItemHandler->xMenuItemDispatch.is() )
        {
            URL aTargetURL;
            aTargetURL.Complete = menuItemHandler->aMenuItemURL;
            m_xURLTransformer->parseStrict( aTargetURL );

            menuItemHandler->xMenuItemDispatch->removeStatusListener(
                static_cast< XStatusListener* >( this ), aTargetURL );
        }

        menuItemHandler->xMenuItemDispatch.clear();

        if ( menuItemHandler->xPopupMenu.is() )
        {
            {
                // Remove popup menu from menu structure
                m_pVCLMenu->SetPopupMenu( menuItemHandler->nItemId, nullptr );
            }

            Reference< css::lang::XEventListener > xEventListener( menuItemHandler->xPopupMenuController, UNO_QUERY );
            if ( xEventListener.is() )
            {
                EventObject aEventObject;
                aEventObject.Source = static_cast<OWeakObject *>(this);
                xEventListener->disposing( aEventObject );
            }

            // We now provide a popup menu controller to external code.
            // Therefore the life-time must be explicitly handled via
            // dispose!!
            try
            {
                Reference< XComponent > xComponent( menuItemHandler->xPopupMenuController, UNO_QUERY );
                if ( xComponent.is() )
                    xComponent->dispose();
            }
            catch ( const RuntimeException& )
            {
                throw;
            }
            catch ( const Exception& )
            {
            }

            // Release references to controller and popup menu
            menuItemHandler->xPopupMenuController.clear();
            menuItemHandler->xPopupMenu.clear();
        }

        if ( menuItemHandler->xSubMenuManager )
            menuItemHandler->xSubMenuManager->dispose();
    }

    try
    {
        if ( m_xFrame.is() )
            m_xFrame->removeFrameActionListener( Reference< XFrameActionListener >(this) );
    }
    catch ( const Exception& )
    {
    }

    m_xFrame = nullptr;
}

void SAL_CALL MenuBarManager::disposing( const EventObject& Source )
{
    MenuItemHandler* pMenuItemDisposing = nullptr;

    SolarMutexGuard g;

    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        if ( menuItemHandler->xMenuItemDispatch.is() &&
             menuItemHandler->xMenuItemDispatch == Source.Source )
        {
            // disposing called from menu item dispatcher, remove listener
            pMenuItemDisposing = menuItemHandler.get();
            break;
        }
    }

    if ( pMenuItemDisposing )
    {
        // Release references to the dispatch object
        URL aTargetURL;
        aTargetURL.Complete = pMenuItemDisposing->aMenuItemURL;

        m_xURLTransformer->parseStrict( aTargetURL );

        pMenuItemDisposing->xMenuItemDispatch->removeStatusListener(
            static_cast< XStatusListener* >( this ), aTargetURL );
        pMenuItemDisposing->xMenuItemDispatch.clear();
        if ( pMenuItemDisposing->xPopupMenu.is() )
        {
            Reference< css::lang::XEventListener > xEventListener( pMenuItemDisposing->xPopupMenuController, UNO_QUERY );
            if ( xEventListener.is() )
                xEventListener->disposing( Source );

            {
                // Remove popup menu from menu structure as we release our reference to
                // the controller.
                m_pVCLMenu->SetPopupMenu( pMenuItemDisposing->nItemId, nullptr );
            }

            pMenuItemDisposing->xPopupMenuController.clear();
            pMenuItemDisposing->xPopupMenu.clear();
        }
        return;
    }
    else if ( Source.Source == m_xFrame )
    {
        // Our frame gets disposed. We have to remove all our listeners
        RemoveListener();
    }
    else if ( Source.Source == Reference< XInterface >( m_xDocImageManager, UNO_QUERY ))
        m_xDocImageManager.clear();
    else if ( Source.Source == Reference< XInterface >( m_xModuleImageManager, UNO_QUERY ))
        m_xModuleImageManager.clear();
}

static void lcl_CheckForChildren(Menu* pMenu, sal_uInt16 nItemId)
{
    if (PopupMenu* pThisPopup = pMenu->GetPopupMenu( nItemId ))
        pMenu->EnableItem( nItemId, pThisPopup->GetItemCount() != 0 && pThisPopup->HasValidEntries(true));
}

// vcl handler

namespace {

class QuietInteractionContext:
    public cppu::WeakImplHelper< css::uno::XCurrentContext >
{
public:
    explicit QuietInteractionContext(
        css::uno::Reference< css::uno::XCurrentContext > context):
        context_(std::move(context)) {}
    QuietInteractionContext(const QuietInteractionContext&) = delete;
    QuietInteractionContext& operator=(const QuietInteractionContext&) = delete;

private:
    virtual ~QuietInteractionContext() override {}

    virtual css::uno::Any SAL_CALL getValueByName(
        OUString const & Name) override
    {
        return Name != JAVA_INTERACTION_HANDLER_NAME && context_.is()
            ? context_->getValueByName(Name)
            : css::uno::Any();
    }

    css::uno::Reference< css::uno::XCurrentContext >
        context_;
};

}

IMPL_LINK( MenuBarManager, Activate, Menu *, pMenu, bool )
{
    if ( pMenu != m_pVCLMenu )
        return true;

    css::uno::ContextLayer layer(
        new QuietInteractionContext(
            css::uno::getCurrentContext()));

    // set/unset hiding disabled menu entries
    bool bDontHide           = officecfg::Office::Common::View::Menu::DontHideDisabledEntry::get();
    const StyleSettings& rSettings = Application::GetSettings().GetStyleSettings();
    bool bShowMenuImages     = rSettings.GetUseImagesInMenus();

    SolarMutexGuard g;

    MenuFlags nFlag = pMenu->GetMenuFlags();
    if ( bDontHide )
        nFlag &= ~MenuFlags::HideDisabledEntries;
    else
        nFlag |= MenuFlags::HideDisabledEntries;
    pMenu->SetMenuFlags( nFlag );

    if ( m_bActive )
        return false;

    m_bActive = true;

    // Check if some modes have changed so we have to update our menu images
    OUString sIconTheme = SvtMiscOptions::GetIconTheme();

    if ( m_bRetrieveImages ||
         bShowMenuImages != m_bShowMenuImages ||
         sIconTheme != m_sIconTheme )
    {
        m_bShowMenuImages   = bShowMenuImages;
        m_bRetrieveImages   = false;
        m_sIconTheme     = sIconTheme;
        FillMenuImages( m_xFrame, pMenu, bShowMenuImages );
    }

    // Try to map commands to labels
    for ( sal_uInt16 nPos = 0; nPos < pMenu->GetItemCount(); nPos++ )
    {
        sal_uInt16 nItemId = pMenu->GetItemId( nPos );
        if (( pMenu->GetItemType( nPos ) != MenuItemType::SEPARATOR ) &&
            ( pMenu->GetItemText( nItemId ).isEmpty() ))
        {
            OUString aCommand = pMenu->GetItemCommand( nItemId );
            if ( !aCommand.isEmpty() ) {
                pMenu->SetItemText( nItemId, RetrieveLabelFromCommand( aCommand ));
            }
        }
    }

    // Try to set accelerator keys
    SetAcceleratorKeys(pMenu);

    URL aTargetURL;

    // Use provided dispatch provider => fallback to frame as dispatch provider
    Reference< XDispatchProvider > xDispatchProvider;
    if ( m_xDispatchProvider.is() )
        xDispatchProvider = m_xDispatchProvider;
    else
        xDispatchProvider.set( m_xFrame, UNO_QUERY );

    if ( !xDispatchProvider.is() )
        return true;

    SvtCommandOptions aCmdOptions;
    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        if (menuItemHandler)
        {
            if ( !menuItemHandler->xMenuItemDispatch.is() &&
                 !menuItemHandler->xSubMenuManager.is()      )
            {
                Reference< XDispatch > xMenuItemDispatch;

                aTargetURL.Complete = menuItemHandler->aMenuItemURL;

                m_xURLTransformer->parseStrict( aTargetURL );

                if (SvtCommandOptions().HasEntriesDisabled())
                {
                    if ( aCmdOptions.LookupDisabled( aTargetURL.Path ))
                        pMenu->HideItem( menuItemHandler->nItemId );
                }

                if ( aTargetURL.Complete.startsWith( ".uno:StyleApply?" ) )
                    xMenuItemDispatch = new StyleDispatcher( m_xFrame, m_xURLTransformer, aTargetURL );
                else
                {
                    try
                    {
                        xMenuItemDispatch = xDispatchProvider->queryDispatch( aTargetURL, menuItemHandler->aTargetFrame, 0 );
                    }
                    catch (uno::Exception const&)
                    {
                        TOOLS_WARN_EXCEPTION("fwk.uielement", "MenuBarManager::Activate(): exception from queryDispatch()");
                    }
                }

                bool bPopupMenu( false );
                if ( !menuItemHandler->xPopupMenuController.is() &&
                     m_xPopupMenuControllerFactory->hasController( menuItemHandler->aMenuItemURL, m_aModuleIdentifier ) )
                {
                    if( xMenuItemDispatch.is() || menuItemHandler->aMenuItemURL != ".uno:RecentFileList" )
                        bPopupMenu = CreatePopupMenuController(menuItemHandler.get(), m_xDispatchProvider, m_aModuleIdentifier);

                    if (bPopupMenu && menuItemHandler->xPopupMenuController.is())
                    {
                        if (PopupMenu* pThisPopup = pMenu->GetPopupMenu(menuItemHandler->nItemId))
                        {
                            pThisPopup->Activate();
                            pThisPopup->Deactivate();
                        }
                    }
                }
                else if ( menuItemHandler->xPopupMenuController.is() )
                {
                    // Force update of popup menu
                    menuItemHandler->xPopupMenuController->updatePopupMenu();
                    bPopupMenu = true;
                    if (PopupMenu*  pThisPopup = pMenu->GetPopupMenu( menuItemHandler->nItemId ))
                    {
                        pThisPopup->Activate();
                        pThisPopup->Deactivate();
                    }
                }
                lcl_CheckForChildren(pMenu, menuItemHandler->nItemId);

                if ( xMenuItemDispatch.is() )
                {
                    menuItemHandler->xMenuItemDispatch = xMenuItemDispatch;
                    menuItemHandler->aParsedItemURL    = aTargetURL.Complete;

                    if ( !bPopupMenu )
                    {
                        xMenuItemDispatch->addStatusListener( static_cast< XStatusListener* >( this ), aTargetURL );
                        // For the menubar, we have to keep status listening to support Ubuntu's HUD.
                        if ( !m_bHasMenuBar )
                            xMenuItemDispatch->removeStatusListener( static_cast< XStatusListener* >( this ), aTargetURL );
                    }
                }
                else if ( !bPopupMenu )
                    pMenu->EnableItem( menuItemHandler->nItemId, false );
            }
            else if ( menuItemHandler->xPopupMenuController.is() )
            {
                // Force update of popup menu
                menuItemHandler->xPopupMenuController->updatePopupMenu();
                if (PopupMenu* pThisPopup = pMenu->GetPopupMenu(menuItemHandler->nItemId))
                {
                    pThisPopup->Activate();
                    pThisPopup->Deactivate();
                }
                lcl_CheckForChildren(pMenu, menuItemHandler->nItemId);
            }
            else if ( menuItemHandler->xMenuItemDispatch.is() )
            {
                // We need an update to reflect the current state
                try
                {
                    aTargetURL.Complete = menuItemHandler->aMenuItemURL;
                    m_xURLTransformer->parseStrict( aTargetURL );

                    menuItemHandler->xMenuItemDispatch->addStatusListener(
                                                            static_cast< XStatusListener* >( this ), aTargetURL );
                    menuItemHandler->xMenuItemDispatch->removeStatusListener(
                                                            static_cast< XStatusListener* >( this ), aTargetURL );
                }
                catch ( const Exception& )
                {
                }
            }
            else if (menuItemHandler->xSubMenuManager.is())
            {
                MenuBarManager* pMenuBarManager = menuItemHandler->xSubMenuManager.get();
                if (pMenuBarManager)
                {
                    pMenuBarManager->Activate(pMenuBarManager->GetMenuBar());
                    pMenuBarManager->Deactivate(pMenuBarManager->GetMenuBar());
                }
                lcl_CheckForChildren(pMenu, menuItemHandler->nItemId);
            }
        }
    }

    return true;
}

IMPL_LINK( MenuBarManager, Deactivate, Menu *, pMenu, bool )
{
    if ( pMenu == m_pVCLMenu )
    {
        m_bActive = false;
        if ( pMenu->IsMenuBar() && m_xDeferredItemContainer.is() )
        {
            // Start timer to handle settings asynchronous
            // Changing the menu inside this handler leads to
            // a crash under X!
            m_aAsyncSettingsTimer.SetInvokeHandler(LINK(this, MenuBarManager, AsyncSettingsHdl));
            m_aAsyncSettingsTimer.SetTimeout(10);
            m_aAsyncSettingsTimer.Start();
        }
    }

    return true;
}

IMPL_LINK_NOARG( MenuBarManager, AsyncSettingsHdl, Timer*, void)
{
    SolarMutexGuard g;
    Reference< XInterface > xSelfHold(
        static_cast< ::cppu::OWeakObject* >( this ), UNO_QUERY_THROW );

    m_aAsyncSettingsTimer.Stop();
    if ( !m_bActive && m_xDeferredItemContainer.is() )
    {
        SetItemContainer( m_xDeferredItemContainer );
        m_xDeferredItemContainer.clear();
    }
}

namespace
{
struct MenuExecData
{
    URL aTargetURL;
    std::vector<beans::PropertyValue> aArgs;
    Reference<XDispatch> xDispatch;
};

void AsyncMenuExecute(void* /*instance*/, void* data)
{
    std::unique_ptr<MenuExecData> pData(static_cast<MenuExecData*>(data));
    {
        SolarMutexReleaser aReleaser;
        pData->xDispatch->dispatch(pData->aTargetURL,
                                   comphelper::containerToSequence(pData->aArgs));
    }
}
}

IMPL_LINK( MenuBarManager, Select, Menu *, pMenu, bool )
{
    auto pData = std::make_unique<MenuExecData>();

    {
        SolarMutexGuard g;

        sal_uInt16 nCurItemId = pMenu->GetCurItemId();
        sal_uInt16 nCurPos    = pMenu->GetItemPos( nCurItemId );
        if ( pMenu == m_pVCLMenu &&
             pMenu->GetItemType( nCurPos ) != MenuItemType::SEPARATOR )
        {
            MenuItemHandler* pMenuItemHandler = GetMenuItemHandler( nCurItemId );
            if ( pMenuItemHandler && pMenuItemHandler->xMenuItemDispatch.is() )
            {
                pData->aTargetURL.Complete = pMenuItemHandler->aMenuItemURL;
                m_xURLTransformer->parseStrict( pData->aTargetURL );

                if ( pMenu->GetUserValue( nCurItemId ) )
                {
                    // addon menu item selected
                    pData->aArgs.push_back(
                        comphelper::makePropertyValue(u"Referer"_ustr, u"private:user"_ustr));
                }

                // pass along if SHIFT/CTRL/ALT/CMD keys are pressed down
                const VclPtr<vcl::Window> pWindow
                    = VCLUnoHelper::GetWindow(m_xFrame->getContainerWindow());
                const sal_Int16 nKeys
                    = pWindow ? pWindow->GetPointerState().mnState & KEY_MODIFIERS_MASK : 0;
                if (nKeys)
                    pData->aArgs.push_back(comphelper::makePropertyValue(u"KeyModifier"_ustr, nKeys));

                pData->xDispatch = pMenuItemHandler->xMenuItemDispatch;
            }
        }
    }

    if (pData->xDispatch.is())
    {
        Application::PostUserEvent(LINK_NONMEMBER(nullptr, AsyncMenuExecute), pData.release());
    }

    if ( !m_bHasMenuBar )
        // Standalone (non-native) popup menu doesn't fire deactivate event
        // in this case, so we have to reset the active flag here.
        m_bActive = false;

    return true;
}

bool MenuBarManager::MustBeHidden( PopupMenu* pPopupMenu, const Reference< XURLTransformer >& rTransformer )
{
    if ( !pPopupMenu )
        return true;

    URL               aTargetURL;
    SvtCommandOptions aCmdOptions;

    sal_uInt16 nCount = pPopupMenu->GetItemCount();
    sal_uInt16 nHideCount( 0 );

    for ( sal_uInt16 i = 0; i < nCount; i++ )
    {
        sal_uInt16 nId = pPopupMenu->GetItemId( i );
        if ( nId > 0 )
        {
            PopupMenu* pSubPopupMenu = pPopupMenu->GetPopupMenu( nId );
            if ( pSubPopupMenu )
            {
                if ( MustBeHidden( pSubPopupMenu, rTransformer ))
                {
                    pPopupMenu->HideItem( nId );
                    ++nHideCount;
                }
            }
            else
            {
                aTargetURL.Complete = pPopupMenu->GetItemCommand( nId );
                rTransformer->parseStrict( aTargetURL );

                if ( aCmdOptions.LookupDisabled( aTargetURL.Path ))
                    ++nHideCount;
            }
        }
        else
            ++nHideCount;
    }

    return ( nCount == nHideCount );
}

OUString MenuBarManager::RetrieveLabelFromCommand(const OUString& rCmdURL)
{
    auto aProperties = vcl::CommandInfoProvider::GetCommandProperties(rCmdURL, m_aModuleIdentifier);
    if ( !m_bHasMenuBar )
    {
        // This is a context menu, prefer "PopupLabel" over "Label".
        return vcl::CommandInfoProvider::GetPopupLabelForCommand(aProperties);
    }
    return vcl::CommandInfoProvider::GetMenuLabelForCommand(aProperties);
}

bool MenuBarManager::CreatePopupMenuController( MenuItemHandler* pMenuItemHandler,
                                                const css::uno::Reference< css::frame::XDispatchProvider >& rDispatchProvider,
                                                const OUString& rModuleIdentifier )
{
    OUString aItemCommand( pMenuItemHandler->aMenuItemURL );

    // Try instantiate a popup menu controller. It is stored in the menu item handler.
    if ( !m_xPopupMenuControllerFactory.is() )
        return false;

    auto aSeq( comphelper::InitAnyPropertySequence( {
        { "DispatchProvider", Any(rDispatchProvider) },
        { "ModuleIdentifier", Any(rModuleIdentifier) },
        { "Frame", Any(m_xFrame) },
        { "InToolbar", Any(!m_bHasMenuBar) }
    } ) );

    Reference< XPopupMenuController > xPopupMenuController(
                                            m_xPopupMenuControllerFactory->createInstanceWithArgumentsAndContext(
                                                aItemCommand,
                                                aSeq,
                                                m_xContext ),
                                            UNO_QUERY );

    if ( xPopupMenuController.is() )
    {
        // Provide our awt popup menu to the popup menu controller
        pMenuItemHandler->xPopupMenuController = xPopupMenuController;
        xPopupMenuController->setPopupMenu( pMenuItemHandler->xPopupMenu );
        return true;
    }

    return false;
}

void MenuBarManager::FillMenuManager( Menu* pMenu, const Reference< XFrame >& rFrame,
                                      const Reference< XDispatchProvider >& rDispatchProvider,
                                      const OUString& rModuleIdentifier, bool bDelete )
{
    m_xFrame            = rFrame;
    m_bActive           = false;
    m_bDeleteMenu       = bDelete;
    m_pVCLMenu          = pMenu;
    m_xDispatchProvider = rDispatchProvider;

    const StyleSettings& rSettings = Application::GetSettings().GetStyleSettings();
    m_bShowMenuImages   = rSettings.GetUseImagesInMenus();
    m_bRetrieveImages   = false;

    // Set module identifier when provided from outside
    if (!rModuleIdentifier.isEmpty())
        m_aModuleIdentifier = rModuleIdentifier;
    else
        m_aModuleIdentifier = vcl::CommandInfoProvider::GetModuleIdentifier(m_xFrame);

    // Add root as ui configuration listener
    RetrieveImageManagers();

    if ( pMenu->IsMenuBar() && rFrame.is() )
    {
        // First merge all addon popup menus into our structure
        sal_uInt16 nPos = 0;
        for ( nPos = 0; nPos < pMenu->GetItemCount(); nPos++ )
        {
            sal_uInt16          nItemId  = pMenu->GetItemId( nPos );
            OUString aCommand = pMenu->GetItemCommand( nItemId );
            if ( aCommand == aSpecialWindowCommand || aCommand == aCmdHelpMenu )
            {
                // Retrieve addon popup menus and add them to our menu bar
                framework::AddonMenuManager::MergeAddonPopupMenus( rFrame, nPos, static_cast<MenuBar *>(pMenu) );
                break;
            }
        }

        // Merge the Add-Ons help menu items into the Office help menu
        framework::AddonMenuManager::MergeAddonHelpMenu( rFrame, static_cast<MenuBar *>(pMenu) );
    }

    const bool bAccessibilityEnabled = MiscSettings::GetEnableATToolSupport();
    sal_uInt16 nItemCount = pMenu->GetItemCount();
    OUString aItemCommand;
    m_aMenuItemHandlerVector.reserve(nItemCount);
    for ( sal_uInt16 i = 0; i < nItemCount; i++ )
    {
        sal_uInt16 nItemId = FillItemCommand(aItemCommand,pMenu, i );

        if (( pMenu->IsMenuBar() || bAccessibilityEnabled ) &&
            ( pMenu->GetItemText( nItemId ).isEmpty() ))
        {
            if ( !aItemCommand.isEmpty() )
                pMenu->SetItemText( nItemId, RetrieveLabelFromCommand( aItemCommand ));
        }

        // Command can be just an alias to another command.
        auto aProperties = vcl::CommandInfoProvider::GetCommandProperties(aItemCommand, m_aModuleIdentifier);
        OUString aRealCommand = vcl::CommandInfoProvider::GetRealCommandForCommand(aProperties);
        if ( !aRealCommand.isEmpty() )
            aItemCommand = aRealCommand;

        Reference< XDispatch > xDispatch;
        VclPtr<PopupMenu> pPopup = pMenu->GetPopupMenu( nItemId );
        // overwrite the show icons on menu option?
        MenuItemBits nBits = pMenu->GetItemBits( nItemId ) & ( MenuItemBits::ICON | MenuItemBits::TEXT );
        bool bItemShowMenuImages = ( m_bShowMenuImages && nBits != MenuItemBits::TEXT ) || nBits & MenuItemBits::ICON;

        if ( pPopup )
        {
            // Retrieve module identifier from Help Command entry
            OUString aModuleIdentifier( rModuleIdentifier );
            if (!pMenu->GetHelpCommand(nItemId).isEmpty())
            {
                aModuleIdentifier = pMenu->GetHelpCommand( nItemId );
                pMenu->SetHelpCommand( nItemId, u""_ustr );
            }

            // Retrieve possible attributes struct
            Reference< XDispatchProvider > xPopupMenuDispatchProvider( rDispatchProvider );
            MenuAttributes* pAttributes = static_cast<MenuAttributes *>(pMenu->GetUserValue( nItemId ));
            if ( pAttributes )
                xPopupMenuDispatchProvider = pAttributes->xDispatchProvider;

            if ( m_xPopupMenuControllerFactory.is() &&
                 m_xPopupMenuControllerFactory->hasController( aItemCommand, aModuleIdentifier )
                  )
            {
                // Check if we have to create a popup menu for a uno based popup menu controller.
                // We have to set an empty popup menu into our menu structure so the controller also
                // works with inplace OLE.
                MenuItemHandler* pItemHandler = new MenuItemHandler( nItemId, nullptr, xDispatch );
                pItemHandler->xPopupMenu = new VCLXPopupMenu(pPopup);
                pItemHandler->aMenuItemURL = aItemCommand;
                m_aMenuItemHandlerVector.push_back( std::unique_ptr<MenuItemHandler>(pItemHandler) );

                if ( bAccessibilityEnabled || pMenu->IsMenuBar())
                {
                    if ( CreatePopupMenuController( pItemHandler, xPopupMenuDispatchProvider, aModuleIdentifier ))
                        pItemHandler->xPopupMenuController->updatePopupMenu();
                }
                lcl_CheckForChildren(pMenu, nItemId);
            }
            else
            {
                // Check if this is the tools menu. Add menu item if needed
                if ( aItemCommand == aCmdToolsMenu && AddonMenuManager::HasAddonMenuElements() )
                {
                    // Create addon popup menu if there exist elements and this is the tools popup menu
                    VclPtr<PopupMenu> pSubMenu = AddonMenuManager::CreateAddonMenu(rFrame);
                    if ( pSubMenu && ( pSubMenu->GetItemCount() > 0 ))
                    {
                        if ( pPopup->GetItemType( pPopup->GetItemCount() - 1 ) != MenuItemType::SEPARATOR )
                            pPopup->InsertSeparator();

                        pPopup->InsertItem( ITEMID_ADDONLIST, OUString() );
                        pPopup->SetPopupMenu( ITEMID_ADDONLIST, pSubMenu );
                        pPopup->SetItemCommand( ITEMID_ADDONLIST, u".uno:Addons"_ustr );
                    }
                    else
                        pSubMenu.disposeAndClear();
                }

                rtl::Reference<MenuBarManager> pSubMenuManager = new MenuBarManager( m_xContext, rFrame, m_xURLTransformer,
                    xPopupMenuDispatchProvider, aModuleIdentifier, pPopup, false, m_bHasMenuBar );

                AddMenu(pSubMenuManager.get(), aItemCommand, nItemId);
            }
        }
        else if ( pMenu->GetItemType( i ) != MenuItemType::SEPARATOR )
        {
            if ( bItemShowMenuImages )
                m_bRetrieveImages = true;

            std::unique_ptr<MenuItemHandler> pItemHandler(new MenuItemHandler( nItemId, nullptr, xDispatch ));
            // Retrieve possible attributes struct
            MenuAttributes* pAttributes = static_cast<MenuAttributes *>(pMenu->GetUserValue( nItemId ));
            if ( pAttributes )
                pItemHandler->aTargetFrame = pAttributes->aTargetFrame;
            pItemHandler->aMenuItemURL = aItemCommand;

            if ( m_xPopupMenuControllerFactory.is() &&
                 m_xPopupMenuControllerFactory->hasController( aItemCommand, m_aModuleIdentifier ) )
            {
                // Check if we have to create a popup menu for a uno based popup menu controller.
                // We have to set an empty popup menu into our menu structure so the controller also
                // works with inplace OLE.
                pItemHandler->xPopupMenu = new VCLXPopupMenu;
                PopupMenu* pPopupMenu = static_cast<PopupMenu*>(pItemHandler->xPopupMenu->GetMenu());
                pMenu->SetPopupMenu( pItemHandler->nItemId, pPopupMenu );

                if ( bAccessibilityEnabled && CreatePopupMenuController( pItemHandler.get(), m_xDispatchProvider, m_aModuleIdentifier ) )
                {
                    pItemHandler->xPopupMenuController->updatePopupMenu();
                }

                lcl_CheckForChildren(pMenu, pItemHandler->nItemId);
            }

            m_aMenuItemHandlerVector.push_back( std::move(pItemHandler) );
        }
    }

    if ( m_bHasMenuBar && bAccessibilityEnabled )
        SetAcceleratorKeys(pMenu);

    SetHdl();
}

void MenuBarManager::impl_RetrieveShortcutsFromConfiguration(
    const Reference< XAcceleratorConfiguration >& rAccelCfg,
    const Sequence< OUString >& rCommands,
    std::vector< std::unique_ptr<MenuItemHandler> >& aMenuShortCuts )
{
    if ( !rAccelCfg.is() )
        return;

    try
    {
        css::awt::KeyEvent aKeyEvent;
        Sequence< Any > aSeqKeyCode = rAccelCfg->getPreferredKeyEventsForCommandList( rCommands );
        for ( sal_Int32 i = 0; i < aSeqKeyCode.getLength(); i++ )
        {
            if ( aSeqKeyCode[i] >>= aKeyEvent )
                aMenuShortCuts[i]->aKeyCode = svt::AcceleratorExecute::st_AWTKey2VCLKey( aKeyEvent );
        }
    }
    catch ( const IllegalArgumentException& )
    {
    }
}

void MenuBarManager::RetrieveShortcuts( std::vector< std::unique_ptr<MenuItemHandler> >& aMenuShortCuts )
{
    Reference< XAcceleratorConfiguration > xDocAccelCfg( m_xDocAcceleratorManager );
    Reference< XAcceleratorConfiguration > xModuleAccelCfg( m_xModuleAcceleratorManager );
    Reference< XAcceleratorConfiguration > xGlobalAccelCfg( m_xGlobalAcceleratorManager );

    if ( !m_bAcceleratorCfg )
    {
        // Retrieve references on demand
        m_bAcceleratorCfg = true;
        if ( !xDocAccelCfg.is() )
        {
            Reference< XController > xController = m_xFrame->getController();
            Reference< XModel > xModel;
            if ( xController.is() )
            {
                xModel = xController->getModel();
                if ( xModel.is() )
                {
                    Reference< XUIConfigurationManagerSupplier > xSupplier( xModel, UNO_QUERY );
                    if ( xSupplier.is() )
                    {
                        Reference< XUIConfigurationManager > xDocUICfgMgr = xSupplier->getUIConfigurationManager();
                        if ( xDocUICfgMgr.is() )
                        {
                            xDocAccelCfg = xDocUICfgMgr->getShortCutManager();
                            m_xDocAcceleratorManager = xDocAccelCfg;
                        }
                    }
                }
            }
        }

        if ( !xModuleAccelCfg.is() )
        {
            Reference< XModuleUIConfigurationManagerSupplier > xModuleCfgMgrSupplier =
                theModuleUIConfigurationManagerSupplier::get( m_xContext );
            try
            {
                Reference< XUIConfigurationManager > xUICfgMgr = xModuleCfgMgrSupplier->getUIConfigurationManager( m_aModuleIdentifier );
                if ( xUICfgMgr.is() )
                {
                    xModuleAccelCfg = xUICfgMgr->getShortCutManager();
                    m_xModuleAcceleratorManager = xModuleAccelCfg;
                }
            }
            catch ( const RuntimeException& )
            {
                throw;
            }
            catch ( const Exception& )
            {
            }
        }

        if ( !xGlobalAccelCfg.is() ) try
        {
            xGlobalAccelCfg = GlobalAcceleratorConfiguration::create( m_xContext );
            m_xGlobalAcceleratorManager = xGlobalAccelCfg;
        }
        catch ( const css::uno::DeploymentException& )
        {
            SAL_WARN("fwk.uielement", "GlobalAcceleratorConfiguration"
                    " not available. This should happen only on mobile platforms.");
        }
    }

    vcl::KeyCode aEmptyKeyCode;
    Sequence< OUString > aSeq( aMenuShortCuts.size() );
    auto aSeqRange = asNonConstRange(aSeq);
    const sal_uInt32 nCount = aMenuShortCuts.size();
    for ( sal_uInt32 i = 0; i < nCount; ++i )
    {
        aSeqRange[i] = aMenuShortCuts[i]->aMenuItemURL;
        aMenuShortCuts[i]->aKeyCode = aEmptyKeyCode;
    }

    if ( m_xGlobalAcceleratorManager.is() )
        impl_RetrieveShortcutsFromConfiguration( xGlobalAccelCfg, aSeq, aMenuShortCuts );
    if ( m_xModuleAcceleratorManager.is() )
        impl_RetrieveShortcutsFromConfiguration( xModuleAccelCfg, aSeq, aMenuShortCuts );
    if ( m_xDocAcceleratorManager.is() )
        impl_RetrieveShortcutsFromConfiguration( xDocAccelCfg, aSeq, aMenuShortCuts );
}

void MenuBarManager::SetAcceleratorKeys(Menu* pMenu)
{
    const StyleSettings& rSettings = Application::GetSettings().GetStyleSettings();
    bool bShowShortcuts = m_bHasMenuBar || rSettings.GetContextMenuShortcuts();
    if ( bShowShortcuts )
        RetrieveShortcuts( m_aMenuItemHandlerVector );

    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        if ( !bShowShortcuts )
        {
            pMenu->SetAccelKey( menuItemHandler->nItemId, vcl::KeyCode() );
        }
        else if ( menuItemHandler->aMenuItemURL == aCmdHelpIndex )
        {
            // Set key code, workaround for hard-coded shortcut F1 mapped to .uno:HelpIndex
            // Only non-popup menu items can have a short-cut
            vcl::KeyCode aKeyCode( KEY_F1 );
            pMenu->SetAccelKey( menuItemHandler->nItemId, aKeyCode );
        }
        else if ( pMenu->GetPopupMenu( menuItemHandler->nItemId ) == nullptr )
            pMenu->SetAccelKey( menuItemHandler->nItemId, menuItemHandler->aKeyCode );
    }
}

void MenuBarManager::RetrieveImageManagers()
{
    if ( !m_xDocImageManager.is() )
    {
        Reference< XController > xController = m_xFrame->getController();
        Reference< XModel > xModel;
        if ( xController.is() )
        {
            xModel = xController->getModel();
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

void MenuBarManager::FillMenuWithConfiguration(
    sal_uInt16&                         nId,
    Menu*                               pMenu,
    const OUString&              rModuleIdentifier,
    const Reference< XIndexAccess >&    rItemContainer,
    const Reference< XURLTransformer >& rTransformer )
{
    Reference< XDispatchProvider > xEmptyDispatchProvider;
    MenuBarManager::FillMenu( nId, pMenu, rModuleIdentifier, rItemContainer, xEmptyDispatchProvider );

    // Merge add-on menu entries into the menu bar
    MenuBarManager::MergeAddonMenus( pMenu,
                                     AddonsOptions().GetMergeMenuInstructions(),
                                     rModuleIdentifier );

    bool bHasDisabledEntries = SvtCommandOptions().HasEntriesDisabled();
    if ( !bHasDisabledEntries )
        return;

    sal_uInt16 nCount = pMenu->GetItemCount();
    for ( sal_uInt16 i = 0; i < nCount; i++ )
    {
        sal_uInt16 nID = pMenu->GetItemId( i );
        if ( nID > 0 )
        {
            PopupMenu* pPopupMenu = pMenu->GetPopupMenu( nID );
            if ( pPopupMenu )
            {
                if ( MustBeHidden( pPopupMenu, rTransformer ))
                    pMenu->HideItem( nId );
            }
        }
    }
}

void MenuBarManager::FillMenu(
    sal_uInt16& nId,
    Menu* pMenu,
    const OUString& rModuleIdentifier,
    const Reference< XIndexAccess >& rItemContainer,
    const Reference< XDispatchProvider >& rDispatchProvider )
{
    // Fill menu bar with container contents
    for ( sal_Int32 n = 0; n < rItemContainer->getCount(); n++ )
    {
        Sequence< PropertyValue > aProps;
        OUString aCommandURL;
        OUString aLabel;
        OUString aModuleIdentifier( rModuleIdentifier );
        sal_uInt16 nType = 0;
        Reference< XIndexAccess > xIndexContainer;
        Reference< XDispatchProvider > xDispatchProvider( rDispatchProvider );
        sal_Int16 nStyle = 0;
        try
        {
            if ( rItemContainer->getByIndex( n ) >>= aProps )
            {
                bool bShow = true;
                bool bEnabled = true;

                for (beans::PropertyValue const& rProp : aProps)
                {
                    OUString aPropName = rProp.Name;
                    if ( aPropName == "CommandURL" )
                        rProp.Value >>= aCommandURL;
                    else if ( aPropName == "ItemDescriptorContainer" )
                        rProp.Value >>= xIndexContainer;
                    else if ( aPropName == "Label" )
                        rProp.Value >>= aLabel;
                    else if ( aPropName == "Type" )
                        rProp.Value >>= nType;
                    else if ( aPropName == "ModuleIdentifier" )
                        rProp.Value >>= aModuleIdentifier;
                    else if ( aPropName == "DispatchProvider" )
                        rProp.Value >>= xDispatchProvider;
                    else if ( aPropName == "Style" )
                        rProp.Value >>= nStyle;
                    else if ( aPropName == "IsVisible" )
                        rProp.Value >>= bShow;
                    else if ( aPropName == "Enabled" )
                        rProp.Value >>= bEnabled;
                }

                if (!aCommandURL.isEmpty() && vcl::CommandInfoProvider::IsExperimental(aCommandURL, rModuleIdentifier) &&
                    !officecfg::Office::Common::Misc::ExperimentalMode::get())
                {
                    continue;
                }
                if (aCommandURL == ".uno:SafeMode"
                    && !officecfg::Office::Common::Misc::OfferSafeMode::get())
                {
                    continue;
                }

                if ( nType == css::ui::ItemType::DEFAULT )
                {
                    pMenu->InsertItem( nId, aLabel );
                    pMenu->SetItemCommand( nId, aCommandURL );

                    if ( nStyle )
                    {
                        MenuItemBits nBits = pMenu->GetItemBits( nId );
                        if ( nStyle & css::ui::ItemStyle::ICON )
                           nBits |= MenuItemBits::ICON;
                        if ( nStyle & css::ui::ItemStyle::TEXT )
                           nBits |= MenuItemBits::TEXT;
                        if ( nStyle & css::ui::ItemStyle::RADIO_CHECK )
                           nBits |= MenuItemBits::RADIOCHECK;
                        pMenu->SetItemBits( nId, nBits );
                    }

                    if ( !bShow )
                        pMenu->HideItem( nId );

                    if ( !bEnabled)
                        pMenu->EnableItem( nId, false );

                    if ( xIndexContainer.is() )
                    {
                        VclPtr<PopupMenu> pNewPopupMenu = VclPtr<PopupMenu>::Create();
                        pMenu->SetPopupMenu( nId, pNewPopupMenu );
                        // Use the command URL as the Help ID for the sub menu
                        pNewPopupMenu->SetHelpId(aCommandURL);

                        if ( xDispatchProvider.is() )
                        {
                            // Use attributes struct to transport special dispatch provider
                            void* nAttributePtr = MenuAttributes::CreateAttribute(xDispatchProvider);
                            pMenu->SetUserValue(nId, nAttributePtr, MenuAttributes::ReleaseAttribute);
                        }

                        // Use help command to transport module identifier
                        if ( !aModuleIdentifier.isEmpty() )
                            pMenu->SetHelpCommand( nId, aModuleIdentifier );

                        ++nId;
                        FillMenu( nId, pNewPopupMenu, aModuleIdentifier, xIndexContainer, xDispatchProvider );
                    }
                    else
                        ++nId;
                }
                else
                {
                    pMenu->InsertSeparator();
                    ++nId;
                }
            }
        }
        catch ( const IndexOutOfBoundsException& )
        {
            break;
        }
    }
}

void MenuBarManager::MergeAddonMenus(
    Menu* pMenuBar,
    const MergeMenuInstructionContainer& aMergeInstructionContainer,
    const OUString& rModuleIdentifier )
{
    // set start value for the item ID for the new addon menu items
    sal_uInt16 nItemId = ADDONMENU_MERGE_ITEMID_START;

    const sal_uInt32 nCount = aMergeInstructionContainer.size();
    for ( sal_uInt32 i = 0; i < nCount; i++ )
    {
        const MergeMenuInstruction& rMergeInstruction = aMergeInstructionContainer[i];

        if ( MenuBarMerger::IsCorrectContext( rMergeInstruction.aMergeContext, rModuleIdentifier ))
        {
            ::std::vector< OUString > aMergePath;

            // retrieve the merge path from the merge point string
            MenuBarMerger::RetrieveReferencePath( rMergeInstruction.aMergePoint, aMergePath );

            // convert the sequence/sequence property value to a more convenient vector<>
            AddonMenuContainer aMergeMenuItems;
            MenuBarMerger::GetSubMenu( rMergeInstruction.aMergeMenu, aMergeMenuItems );

            // try to find the reference point for our merge operation
            Menu* pMenu = pMenuBar;
            ReferencePathInfo aResult = MenuBarMerger::FindReferencePath( aMergePath, pMenu );

            if ( aResult.eResult == RP_OK )
            {
                // normal merge operation
                MenuBarMerger::ProcessMergeOperation( aResult.pPopupMenu,
                                                      aResult.nPos,
                                                      nItemId,
                                                      rMergeInstruction.aMergeCommand,
                                                      rMergeInstruction.aMergeCommandParameter,
                                                      rModuleIdentifier,
                                                      aMergeMenuItems );
            }
            else
            {
                // fallback
                MenuBarMerger::ProcessFallbackOperation( aResult,
                                                         nItemId,
                                                         rMergeInstruction.aMergeCommand,
                                                         rMergeInstruction.aMergeFallback,
                                                         aMergePath,
                                                         rModuleIdentifier,
                                                         aMergeMenuItems );
            }
        }
    }
}

void MenuBarManager::SetItemContainer( const Reference< XIndexAccess >& rItemContainer )
{
    SolarMutexGuard aSolarMutexGuard;

    Reference< XFrame > xFrame = m_xFrame;

    // Clear MenuBarManager structures
    {
        // Check active state as we cannot change our VCL menu during activation by the user
        if ( m_bActive )
        {
            m_xDeferredItemContainer = rItemContainer;
            return;
        }

        RemoveListener();
        m_aMenuItemHandlerVector.clear();
        m_pVCLMenu->Clear();

        sal_uInt16          nId = 1;

        // Fill menu bar with container contents
        FillMenuWithConfiguration( nId, m_pVCLMenu, m_aModuleIdentifier, rItemContainer, m_xURLTransformer );

        // Refill menu manager again
        Reference< XDispatchProvider > xDispatchProvider;
        FillMenuManager( m_pVCLMenu, xFrame, xDispatchProvider, m_aModuleIdentifier, false );

        // add itself as frame action listener
        m_xFrame->addFrameActionListener( Reference< XFrameActionListener >(this) );
    }
}

void MenuBarManager::GetPopupController( PopupControllerCache& rPopupController )
{

    SolarMutexGuard aSolarMutexGuard;

    for (auto const& menuItemHandler : m_aMenuItemHandlerVector)
    {
        if ( menuItemHandler->xPopupMenuController.is() )
        {
            Reference< XDispatchProvider > xDispatchProvider( menuItemHandler->xPopupMenuController, UNO_QUERY );

            PopupControllerEntry aPopupControllerEntry;
            aPopupControllerEntry.m_xDispatchProvider = xDispatchProvider;

            // Just use the main part of the URL for popup menu controllers
            sal_Int32     nSchemePart( 0 );
            OUString aMenuURL( menuItemHandler->aMenuItemURL );

            nSchemePart = aMenuURL.indexOf( ':' );
            if (( nSchemePart > 0 ) &&
                ( aMenuURL.getLength() > ( nSchemePart+1 )))
            {
                OUString aMainURL( u"vnd.sun.star.popup:"_ustr );
                sal_Int32 nQueryPart  = aMenuURL.indexOf( '?', nSchemePart );
                if ( nQueryPart > 0 )
                    aMainURL += aMenuURL.subView( nSchemePart, nQueryPart-nSchemePart );
                else if ( nQueryPart == -1 )
                    aMainURL += aMenuURL.subView( nSchemePart+1 );

                rPopupController.emplace( aMainURL, aPopupControllerEntry );
            }
        }
        if ( menuItemHandler->xSubMenuManager )
        {
            menuItemHandler->xSubMenuManager->GetPopupController( rPopupController );
        }
    }
}

void MenuBarManager::AddMenu(MenuBarManager* pSubMenuManager,const OUString& _sItemCommand,sal_uInt16 _nItemId)
{
    Reference< XStatusListener > xSubMenuManager( pSubMenuManager );
    m_xFrame->addFrameActionListener( Reference< XFrameActionListener >( xSubMenuManager, UNO_QUERY ));

    std::unique_ptr<MenuItemHandler> pMenuItemHandler(new MenuItemHandler(
                                                _nItemId,
                                                pSubMenuManager,
                                                Reference<XDispatch>() ));
    pMenuItemHandler->aMenuItemURL = _sItemCommand;
    m_aMenuItemHandlerVector.push_back( std::move(pMenuItemHandler) );
}

// static
sal_uInt16 MenuBarManager::FillItemCommand(OUString& _rItemCommand, Menu* _pMenu,sal_uInt16 _nIndex)
{
    sal_uInt16 nItemId = _pMenu->GetItemId( _nIndex );

    _rItemCommand = _pMenu->GetItemCommand( nItemId );
    if ( _rItemCommand.isEmpty() )
    {
        _rItemCommand = "slot:" + OUString::number( nItemId );
        _pMenu->SetItemCommand( nItemId, _rItemCommand );
    }
    return nItemId;
}

void MenuBarManager::SetHdl()
{
    m_pVCLMenu->SetActivateHdl( LINK( this, MenuBarManager, Activate ));
    m_pVCLMenu->SetDeactivateHdl( LINK( this, MenuBarManager, Deactivate ));
    m_pVCLMenu->SetSelectHdl( LINK( this, MenuBarManager, Select ));

    if ( !m_xURLTransformer.is() && m_xContext.is() )
        m_xURLTransformer.set( URLTransformer::create( m_xContext) );
}

void MenuBarManager::FillMenuImages(Reference< XFrame > const & _xFrame, Menu* _pMenu,bool bShowMenuImages)
{
    AddonsOptions aAddonOptions;

    for ( sal_uInt16 nPos = 0; nPos < _pMenu->GetItemCount(); nPos++ )
    {
        sal_uInt16 nId = _pMenu->GetItemId( nPos );
        if ( _pMenu->GetItemType( nPos ) != MenuItemType::SEPARATOR )
        {
            // overwrite the show icons on menu option?
            MenuItemBits nBits = _pMenu->GetItemBits( nId ) & ( MenuItemBits::ICON | MenuItemBits::TEXT );
            bool bTmpShowMenuImages = ( bShowMenuImages && nBits != MenuItemBits::TEXT ) || nBits & MenuItemBits::ICON;

            if ( bTmpShowMenuImages )
            {
                OUString aMenuItemCommand = _pMenu->GetItemCommand( nId );
                Image aImage = vcl::CommandInfoProvider::GetImageForCommand(aMenuItemCommand, _xFrame);
                if ( !aImage )
                    aImage = Image(aAddonOptions.GetImageFromURL(aMenuItemCommand, false));

                _pMenu->SetItemImage( nId, aImage );
            }
            else
                _pMenu->SetItemImage( nId, Image() );
        }
    }
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
