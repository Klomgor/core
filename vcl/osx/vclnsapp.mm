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
#include <config_features.h>

#include <vector>

#include <stdlib.h>

#include <sal/main.h>
#include <vcl/commandevent.hxx>
#include <vcl/ImageTree.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>

#include <osx/saldata.hxx>
#include <osx/salframe.h>
#include <osx/salframeview.h>
#include <osx/salinst.h>
#include <osx/salnsmenu.h>
#include <osx/vclnsapp.h>
#include <quartz/utils.h>

#include <premac.h>
#include <objc/objc-runtime.h>
#import "Carbon/Carbon.h"
#import "apple_remote/RemoteControl.h"
#include <postmac.h>


@implementation CocoaThreadEnabler
-(void)enableCocoaThreads:(id)param
{
    // do nothing, this is just to start an NSThread and therefore put
    // Cocoa into multithread mode
    (void)param;
}
@end

// If you wonder how this VCL_NSApplication stuff works, one thing you
// might have missed is that the NSPrincipalClass property in
// desktop/macosx/Info.plist has the value VCL_NSApplication.

@implementation VCL_NSApplication

-(void)applicationDidFinishLaunching:(NSNotification*)pNotification
{
    (void)pNotification;

    NSEvent* pEvent = [NSEvent otherEventWithType: NSEventTypeApplicationDefined
                               location: NSZeroPoint
                               modifierFlags: 0
                               timestamp: [[NSProcessInfo processInfo] systemUptime]
                               windowNumber: 0
                               context: nil
                               subtype: AquaSalInstance::AppExecuteSVMain
                               data1: 0
                               data2: 0 ];
    assert( pEvent );
    [NSApp postEvent: pEvent atStart: NO];

    // Disable native tabbed windows. Before native tabbed windows can be
    // enabled the following known issues need to be fixed:
    // - Live resizing a non-full screen window with multiple tabs open
    //   causes the window height to increase far offscreen
    // - The status bar is pushed off the bottom of the screen in a
    //   non-full screen window with multiple tabs open
    // - After closing all tabs in a non-full screen window with multiple
    //   tabs, the last window leaves an empty space below the status bar
    //   equal in height to the hidden tab bar
    [NSWindow setAllowsAutomaticWindowTabbing: NO];

    // listen to dark mode change
    [NSApp addObserver:self forKeyPath:@"effectiveAppearance" options: 0 context: nil];
}

-(void)sendEvent:(NSEvent*)pEvent
{
    NSEventType eType = [pEvent type];
    if( eType == NSEventTypeApplicationDefined )
    {
        AquaSalInstance::handleAppDefinedEvent( pEvent );
    }
    else if( eType == NSEventTypeKeyDown && ([pEvent modifierFlags] & NSEventModifierFlagCommand) != 0 )
    {
        NSWindow* pKeyWin = [NSApp keyWindow];
        if( pKeyWin && [pKeyWin isKindOfClass: [SalFrameWindow class]] )
        {
            // Commit uncommitted text before dispatching key shortcuts. In
            // certain cases such as pressing Command-Option-C in a Writer
            // document while there is uncommitted text will call
            // AquaSalFrame::EndExtTextInput() which will dispatch a
            // SalEvent::EndExtTextInput event. Writer's handler for that event
            // will delete the uncommitted text and then insert the committed
            // text but LibreOffice will crash when deleting the uncommitted
            // text because deletion of the text also removes and deletes the
            // newly inserted comment.
            [static_cast<SalFrameWindow*>(pKeyWin) endExtTextInput];

            AquaSalFrame* pFrame = [static_cast<SalFrameWindow*>(pKeyWin) getSalFrame];

            // Related tdf#162010: match against -[NSEvent characters]
            // When using some non-Western European keyboard layouts, the
            // event's "characters ignoring modifiers" will be set to the
            // original Unicode character instead of the resolved key
            // equivalent character so match against the -[NSEvent characters]
            // instead.
            NSEventModifierFlags nModMask = ([pEvent modifierFlags] & (NSEventModifierFlagShift|NSEventModifierFlagControl|NSEventModifierFlagOption|NSEventModifierFlagCommand));

            // Note: when pressing Command-Option keys, some non-Western
            // keyboards will set the "characters ignoring modifiers"
            // property  to the key shortcut character instead of setting
            // the "characters property. So check for both cases.
            NSString *pCharacters = [pEvent characters];
            NSString *pCharactersIgnoringModifiers = [pEvent charactersIgnoringModifiers];

            /*
             * #i98949# - Cmd-M miniaturize window, Cmd-Option-M miniaturize all windows
             */
            if( [pCharacters isEqualToString: @"m"] || [pCharactersIgnoringModifiers isEqualToString: @"m"] )
            {
                if ( nModMask == NSEventModifierFlagCommand && ([pFrame->getNSWindow() styleMask] & NSWindowStyleMaskMiniaturizable) )
                {
                    [pFrame->getNSWindow() performMiniaturize: nil];
                    return;
                }
                else if ( nModMask == ( NSEventModifierFlagCommand | NSEventModifierFlagOption ) )
                {
                    [NSApp miniaturizeAll: nil];
                    return;
                }
            }
            // tdf#162190 handle Command-w
            // On macOS, Command-w should attempt to close the key window.
            // TODO: Command-Option-w should attempt to close all windows.
            else if( [pCharacters isEqualToString: @"w"] || [pCharactersIgnoringModifiers isEqualToString: @"w"] )
            {
                if ( nModMask == NSEventModifierFlagCommand && ([pFrame->getNSWindow() styleMask] & NSWindowStyleMaskClosable ) )
                {
                    [pFrame->getNSWindow() performClose: nil];
                    return;
                }
            }

            // get information whether the event was handled; keyDown returns nothing
            GetSalData()->maKeyEventAnswer[ pEvent ] = false;
            bool bHandled = false;

            // dispatch to view directly to avoid the key event being consumed by the menubar
            // popup windows do not get the focus, so they don't get these either
            // simplest would be dispatch this to the key window always if it is without parent
            // however e.g. in document we want the menu shortcut if e.g. the stylist has focus
            if( pFrame->mpParent && !(pFrame->mnStyle & SalFrameStyleFlags::FLOAT) )
            {
                [[pKeyWin contentView] keyDown: pEvent];
                bHandled = GetSalData()->maKeyEventAnswer[ pEvent ];
            }

            // see whether the main menu consumes this event
            // if not, we want to dispatch it ourselves. Unless we do this "trick"
            // the main menu just beeps for an unknown or disabled key equivalent
            // and swallows the event wholesale
            NSMenu* pMainMenu = [NSApp mainMenu];
            if( ! bHandled &&
                (pMainMenu == nullptr || ! [NSMenu menuBarVisible] || ! [pMainMenu performKeyEquivalent: pEvent]) )
            {
                [[pKeyWin contentView] keyDown: pEvent];
                bHandled = GetSalData()->maKeyEventAnswer[ pEvent ];
            }
            else
            {
                bHandled = true;  // event handled already or main menu just handled it
            }
            GetSalData()->maKeyEventAnswer.erase( pEvent );

            if( bHandled )
                return;
        }
        else if( pKeyWin )
        {
            // #i94601# a window not of vcl's making has the focus.
            // Since our menus do not invoke the usual commands
            // try to play nice with native windows like the file dialog
            // and emulate them
            // precondition: this ONLY works because CMD-V (paste), CMD-C (copy) and CMD-X (cut) are
            // NOT localized, that is the same in all locales. Should this be
            // different in any locale, this hack will fail.
            if( [SalNSMenu dispatchSpecialKeyEquivalents:pEvent] )
                return;
        }
    }
    [super sendEvent: pEvent];
}

-(void)sendSuperEvent:(NSEvent*)pEvent
{
    [super sendEvent: pEvent];
}

-(NSMenu*)applicationDockMenu:(NSApplication *)sender
{
    (void)sender;
    return AquaSalInstance::GetDynamicDockMenu();
}

-(BOOL)application: (NSApplication*)app openFile: (NSString*)pFile
{
    (void)app;
    std::vector<OUString> aFile { GetOUString( pFile ) };
    if( ! AquaSalInstance::isOnCommandLine( aFile[0] ) )
    {
        const ApplicationEvent* pAppEvent = new ApplicationEvent(ApplicationEvent::Type::Open, std::move(aFile));
        AquaSalInstance::aAppEventList.push_back( pAppEvent );
        AquaSalInstance *pInst = GetSalData()->mpInstance;
        if( pInst )
            pInst->TriggerUserEventProcessing();
    }
    return YES;
}

-(void)application: (NSApplication*) app openFiles: (NSArray*)files
{
    (void)app;
    std::vector<OUString> aFileList;

    NSEnumerator* it = [files objectEnumerator];
    NSString* pFile = nil;

    while( (pFile = [it nextObject]) != nil )
    {
        const OUString aFile( GetOUString( pFile ) );
        if( ! AquaSalInstance::isOnCommandLine( aFile ) )
        {
            aFileList.push_back( aFile );
        }
    }

    if( !aFileList.empty() )
    {
        // we have no back channel here, we have to assume success, in which case
        // replyToOpenOrPrint does not need to be called according to documentation
        // [app replyToOpenOrPrint: NSApplicationDelegateReplySuccess];
        const ApplicationEvent* pAppEvent = new ApplicationEvent(ApplicationEvent::Type::Open, std::move(aFileList));
        AquaSalInstance::aAppEventList.push_back( pAppEvent );
        AquaSalInstance *pInst = GetSalData()->mpInstance;
        if( pInst )
            pInst->TriggerUserEventProcessing();
    }
}

-(BOOL)application: (NSApplication*)app printFile: (NSString*)pFile
{
    (void)app;
    std::vector<OUString> aFile { GetOUString(pFile) };
    const ApplicationEvent* pAppEvent = new ApplicationEvent(ApplicationEvent::Type::Print, std::move(aFile));
    AquaSalInstance::aAppEventList.push_back( pAppEvent );
    AquaSalInstance *pInst = GetSalData()->mpInstance;
    if( pInst )
        pInst->TriggerUserEventProcessing();
    return YES;
}
-(NSApplicationPrintReply)application: (NSApplication *) app printFiles:(NSArray *)files withSettings: (NSDictionary *)printSettings showPrintPanels:(BOOL)bShowPrintPanels
{
    (void)app;
    (void)printSettings;
    (void)bShowPrintPanels;
    // currently ignores print settings a bShowPrintPanels
    std::vector<OUString> aFileList;

    NSEnumerator* it = [files objectEnumerator];
    NSString* pFile = nil;

    while( (pFile = [it nextObject]) != nil )
    {
        aFileList.push_back( GetOUString( pFile ) );
    }
    const ApplicationEvent* pAppEvent = new ApplicationEvent(ApplicationEvent::Type::Print, std::move(aFileList));
    AquaSalInstance::aAppEventList.push_back( pAppEvent );
    AquaSalInstance *pInst = GetSalData()->mpInstance;
    if( pInst )
        pInst->TriggerUserEventProcessing();
    // we have no back channel here, we have to assume success
    // correct handling would be NSPrintingReplyLater and then send [app replyToOpenOrPrint]
    return NSPrintingSuccess;
}

-(void)applicationWillTerminate: (NSNotification *) aNotification
{
    (void)aNotification;
    sal_detail_deinitialize();
    _Exit(0);
}

-(NSApplicationTerminateReply)applicationShouldTerminate: (NSApplication *) app
{
    (void)app;

    // Related: tdf#126638 disable all menu items when displaying modal windows
    // Although -[SalNSMenuItem validateMenuItem:] disables almost all menu
    // items when a modal window is displayed, the standard Quit menu item
    // does not get disabled so disable it here.
    if ([NSApp modalWindow])
        return NSTerminateCancel;

    NSApplicationTerminateReply aReply = NSTerminateNow;
    {
        SolarMutexGuard aGuard;

        AquaSalInstance *pInst = GetSalData()->mpInstance;
        SalFrame *pAnyFrame = pInst->anyFrame();
        if( pAnyFrame )
        {
            // the following QueryExit will likely present a message box, activate application
            [NSApp activateIgnoringOtherApps: YES];
            aReply = pAnyFrame->CallCallback( SalEvent::Shutdown, nullptr ) ? NSTerminateCancel : NSTerminateNow;
        }

        if( aReply == NSTerminateNow )
        {
            ApplicationEvent aEv(ApplicationEvent::Type::PrivateDoShutdown);
            GetpApp()->AppEvent( aEv );
            ImageTree::get().shutdown();
            // DeInitVCL should be called in ImplSVMain - unless someone exits first which
            // can occur in Desktop::doShutdown for example
        }
    }

    return aReply;
}

-(void)observeValueForKeyPath: (NSString*) keyPath ofObject:(id)object
                               change: (NSDictionary<NSKeyValueChangeKey, id>*)change
                               context: (void*)context
{
    [super observeValueForKeyPath:keyPath ofObject:object change:change context:context];
    if ([keyPath isEqualToString:@"effectiveAppearance"])
        [self systemColorsChanged: nil];
}

-(void)systemColorsChanged: (NSNotification*) pNotification
{
    (void)pNotification;
    SolarMutexGuard aGuard;

    // Related: tdf#156855 delay SalEvent::SettingsChanged event
    // -[SalFrameView viewDidChangeEffectiveAppearance] needs to delay
    // so be safe and do the same here.
    GetSalData()->mpInstance->delayedSettingsChanged( true );
}

-(void)screenParametersChanged: (NSNotification*) pNotification
{
    (void)pNotification;
    SolarMutexGuard aGuard;

    for( auto pSalFrame : GetSalData()->mpInstance->getFrames() )
    {
        AquaSalFrame *pFrame = static_cast<AquaSalFrame*>( pSalFrame );
        pFrame->screenParametersChanged();
    }
}

-(void)scrollbarVariantChanged: (NSNotification*) pNotification
{
    (void)pNotification;
    GetSalData()->mpInstance->delayedSettingsChanged( true );
}

-(void)scrollbarSettingsChanged: (NSNotification*) pNotification
{
    (void)pNotification;
    GetSalData()->mpInstance->delayedSettingsChanged( false );
}

-(void)addFallbackMenuItem: (NSMenuItem*)pNewItem
{
    AquaSalMenu::addFallbackMenuItem( pNewItem );
}

-(void)removeFallbackMenuItem: (NSMenuItem*)pItem
{
    AquaSalMenu::removeFallbackMenuItem( pItem );
}

-(void)addDockMenuItem: (NSMenuItem*)pNewItem
{
    NSMenu* pDock = AquaSalInstance::GetDynamicDockMenu();
    [pDock insertItem: pNewItem atIndex: [pDock numberOfItems]];
}

// for Apple Remote implementation

#if !HAVE_FEATURE_MACOSX_SANDBOX
- (void)applicationWillBecomeActive:(NSNotification *)pNotification
{
    (void)pNotification;
    SalData* pSalData = GetSalData();
    AppleRemoteMainController* pAppleRemoteCtrl = pSalData->mpAppleRemoteMainController;
    if( pAppleRemoteCtrl && pAppleRemoteCtrl->remoteControl)
    {
        // [remoteControl startListening: self];
        // does crash because the right thing to do is
        // [pAppleRemoteCtrl->remoteControl startListening: self];
        // but the instance variable 'remoteControl' is declared protected
        // workaround : declare remoteControl instance variable as public in RemoteMainController.m

        [pAppleRemoteCtrl->remoteControl startListening: self];
#if OSL_DEBUG_LEVEL >= 2
        NSLog(@"Apple Remote will become active - Using remote controls");
#endif
    }
    for( std::list< AquaSalFrame* >::const_iterator it = pSalData->maPresentationFrames.begin();
         it != pSalData->maPresentationFrames.end(); ++it )
    {
        NSWindow* pNSWindow = (*it)->getNSWindow();
        [pNSWindow setLevel: NSPopUpMenuWindowLevel];
        if( [pNSWindow isVisible] )
            [pNSWindow orderFront: NSApp];
    }
}

- (void)applicationWillResignActive:(NSNotification *)pNotification
{
    (void)pNotification;
    SalData* pSalData = GetSalData();
    AppleRemoteMainController* pAppleRemoteCtrl = pSalData->mpAppleRemoteMainController;
    if( pAppleRemoteCtrl && pAppleRemoteCtrl->remoteControl)
    {
        // [remoteControl stopListening: self];
        // does crash because the right thing to do is
        // [pAppleRemoteCtrl->remoteControl stopListening: self];
        // but the instance variable 'remoteControl' is declared protected
        // workaround : declare remoteControl instance variable as public in RemoteMainController.m

        [pAppleRemoteCtrl->remoteControl stopListening: self];
#if OSL_DEBUG_LEVEL >= 2
        NSLog(@"Apple Remote will resign active - Releasing remote controls");
#endif
    }
    for( std::list< AquaSalFrame* >::const_iterator it = pSalData->maPresentationFrames.begin();
         it != pSalData->maPresentationFrames.end(); ++it )
    {
        [(*it)->getNSWindow() setLevel: NSNormalWindowLevel];
    }
}
#endif

- (BOOL)applicationShouldHandleReopen: (NSApplication*)pApp hasVisibleWindows: (BOOL) bWinVisible
{
    (void)pApp;
    (void)bWinVisible;
    NSObject* pHdl = GetSalData()->mpDockIconClickHandler;
    if( pHdl && [pHdl respondsToSelector: @selector(dockIconClicked:)] )
    {
        [pHdl performSelector:@selector(dockIconClicked:) withObject: self];
    }
    return YES;
}

- (BOOL)applicationSupportsSecureRestorableState: (NSApplication *)pApp
{
    return YES;
}

-(void)setDockIconClickHandler: (NSObject*)pHandler
{
    GetSalData()->mpDockIconClickHandler = pHandler;
}

-(NSImage*)createNSImage: (NSValue*)pImageValue
{
    if (pImageValue)
    {
        Image *pImage = static_cast<Image*>([pImageValue pointerValue]);
        if (pImage)
            return CreateNSImage(*pImage);
    }

    return nil;
}

-(void)addWindowsItem: (NSWindow *)pWindow title: (NSString *)pString filename: (BOOL)bFilename
{
    // Related: tdf#165448 stop macOS from creating its own file list in the
    // windows menu
    (void)pWindow;
    (void)pString;
    (void)bFilename;
}

-(void)changeWindowsItem: (NSWindow *)pWindow title: (NSString *)pString filename: (BOOL)bFilename
{
    // Related: tdf#165448 stop macOS from creating its own file list in the
    // windows menu
    (void)pWindow;
    (void)pString;
    (void)bFilename;
}

-(void)removeWindowsItem: (NSWindow *)pWindow
{
    // Related: tdf#165448 stop macOS from creating its own file list in the
    // windows menu
    (void)pWindow;
}

-(void)updateWindowsItem: (NSWindow *)pWindow
{
    // Related: tdf#165448 stop macOS from creating its own file list in the
    // windows menu
    (void)pWindow;
}

@end

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
