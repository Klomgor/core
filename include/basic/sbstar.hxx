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

#pragma once

#include <basic/sbx.hxx>
#include <basic/sbxobj.hxx>
#include <basic/sbmod.hxx>
#include <rtl/ustring.hxx>
#include <tools/link.hxx>
#include <comphelper/errcode.hxx>

#include <basic/sbdef.hxx>
#include <basic/basicdllapi.h>

namespace com::sun::star::frame { class XModel; }
namespace com::sun::star::script { struct ModuleInfo; }

class SbMethod;

class BASIC_DLLPUBLIC StarBASIC final : public SbxObject
{
    friend class SbiScanner;
    friend class SbiExpression; // Access to RTL
    friend class SbiInstance;   // runtime instance
    friend class SbiRuntime;    // currently running procedure
    friend class DocBasicItem;

    SbModules       pModules;               // List of all modules
    SbxObjectRef    pRtl;               // Runtime Library
    SbxArrayRef     xUnoListeners;          // Listener handled by CreateUnoListener

   // Handler-Support:
    Link<StarBASIC*,bool>            aErrorHdl;              // Error handler
    Link<StarBASIC*,BasicDebugFlags> aBreakHdl;              // Breakpoint handler
    bool            bNoRtl;                 // if true: do not search RTL
    bool            bBreak;                 // if true: Break, otherwise Step
    bool            bDocBasic;
    bool            bVBAEnabled;
    bool            bQuit;

    SbxObjectRef pVBAGlobals;

    BASIC_DLLPRIVATE void implClearDependingVarsOnDelete( StarBASIC* pDeletedBasic );
    bool                                CError( ErrCode, const OUString&, sal_Int32, sal_Int32, sal_Int32 );
    BASIC_DLLPRIVATE bool               RTError( ErrCode, const OUString& rMsg, sal_Int32, sal_Int32, sal_Int32 );
    BASIC_DLLPRIVATE BasicDebugFlags    BreakPoint( sal_Int32 nLine, sal_Int32 nCol1, sal_Int32 nCol2 );
    BASIC_DLLPRIVATE BasicDebugFlags    StepPoint( sal_Int32 nLine, sal_Int32 nCol1, sal_Int32 nCol2 );
    virtual bool LoadData( SvStream&, sal_uInt16 ) override;
    virtual std::pair<bool, sal_uInt32> StoreData( SvStream& ) const override;
    bool             ErrorHdl();
    BasicDebugFlags  BreakHdl();
    virtual ~StarBASIC() override;

public:

    SBX_DECL_PERSIST_NODATA(SBXID_BASIC,1);

    StarBASIC( StarBASIC* pParent = nullptr, bool bIsDocBasic = false );

    // #51727 SetModified overridden so that the Modified-State is
        // not delivered to Parent.
    virtual void SetModified( bool ) override;

    virtual void    Insert( SbxVariable* ) override;
    using SbxObject::Remove;
    virtual void    Remove( SbxVariable* ) override;
    virtual void    Clear() override;

    // Compiler-Interface
    SbModule*       MakeModule( const OUString& rName, const OUString& rSrc );
    SbModule*       MakeModule( const OUString& rName, const css::script::ModuleInfo& mInfo, const OUString& rSrc );
    static void     Stop();
    static void     Error( ErrCode, const OUString& rMsg = {} );
    static void     FatalError( ErrCode );
    static void     FatalError( ErrCode, const OUString& rMsg );
    static bool     IsRunning();
    static ErrCode  GetErrBasic();
    // #66536 make additional message accessible by RTL function Error
    static const OUString & GetErrorMsg();
    static sal_Int32 GetErl();

    virtual SbxVariable* Find( const OUString&, SbxClassType ) override;
    virtual bool Call( const OUString&, SbxArray* = nullptr ) override;

    SbModules&      GetModules() { return pModules; }
    SbxObject*      GetRtl()     { return pRtl.get();     }
    SbModule*       FindModule( std::u16string_view );
    // Run init code of all modules (including the inserted Doc-Basics)
    void            InitAllModules( StarBASIC const * pBasicNotToInit = nullptr );
    void            DeInitAllModules();
    void            ClearAllModuleVars();

    // Calls for error and break handler
    static sal_uInt16 GetLine();
    static sal_uInt16 GetCol1();
    static sal_uInt16 GetCol2();
    static void     SetErrorData( const ErrCodeMsg& nCode, sal_uInt16 nLine,
                                  sal_uInt16 nCol1, sal_uInt16 nCol2 );

    // Specific to error handler
    static void     MakeErrorText( ErrCode, std::u16string_view aMsg );
    static const    OUString& GetErrorText();
    static ErrCodeMsg const & GetErrorCode();
    static sal_uInt16 GetVBErrorCode( ErrCode nError );
    static ErrCode  GetSfxFromVBError( sal_uInt16 nError );
    bool            IsBreak() const             { return bBreak; }

    static Link<StarBASIC*,bool> const & GetGlobalErrorHdl();
    static void     SetGlobalErrorHdl( const Link<StarBASIC*,bool>& rNewHdl );

    static void     SetGlobalBreakHdl( const Link<StarBASIC*,BasicDebugFlags>& rNewHdl );

    SbxArrayRef const & getUnoListeners();

    static SbxBase* FindSBXInCurrentScope( const OUString& rName );
    static SbMethod* GetActiveMethod( sal_uInt16 nLevel = 0 );
    static SbModule* GetActiveModule();
    void SetVBAEnabled( bool bEnabled );
    bool isVBAEnabled() const;

    const SbxObjectRef& getRTL() const { return pRtl; }
    bool IsDocBasic() const { return bDocBasic; }
    SbxVariable* VBAFind( const OUString& rName, SbxClassType t );
    bool GetUNOConstant( const OUString& rName, css::uno::Any& aOut );
    void QuitAndExitApplication();
    bool IsQuitApplication() const { return bQuit; };

    SbxObject* getVBAGlobals( );

    static css::uno::Reference< css::frame::XModel >
        GetModelFromBasic( SbxObject* pBasic );

    static void DetachAllDocBasicItems();
};

typedef tools::SvRef<StarBASIC> StarBASICRef;

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
