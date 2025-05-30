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

#include <sfx2/module.hxx>
#include <sfx2/app.hxx>
#include <vcl/vclptr.hxx>
#include <unotools/options.hxx>
#include <optional>

namespace svtools { class ColorConfig; }

class SfxObjectFactory;
class SmSymbolManager;
class SmMathConfig;

/*************************************************************************
|*
|* This subclass of <SfxModule> (which is a subclass of <SfxShell>) is
|* linked to the DLL. One instance of this class exists while the DLL is
|* loaded.
|*
|* SdModule is like to be compared with the <SfxApplication>-subclass.
|*
|* Remember: Don`t export this class! It uses DLL-internal symbols.
|*
\************************************************************************/

class VirtualDevice;


OUString SmResId(TranslateId aId);

namespace SmLocalizedSymbolData
{
    OUString GetUiSymbolName( std::u16string_view rExportName );
    OUString GetExportSymbolName( std::u16string_view rUiName );

    OUString GetUiSymbolSetName( std::u16string_view rExportName );
    OUString GetExportSymbolSetName( std::u16string_view rUiName );
};

class SmModule final : public SfxModule, public utl::ConfigurationListener
{
    std::unique_ptr<svtools::ColorConfig> mpColorConfig;
    std::unique_ptr<SmMathConfig> mpConfig;
    std::optional<SvtSysLocale> moSysLocale;
    VclPtr<VirtualDevice>    mpVirtualDev;

public:
    SFX_DECL_INTERFACE(SFX_INTERFACE_SMA_START + SfxInterfaceId(0))

private:
    /// SfxInterface initializer.
    static void InitInterface_Impl();

public:
    explicit SmModule(SfxObjectFactory* pObjFact);
    virtual ~SmModule() override;

    virtual void ConfigurationChanged( utl::ConfigurationBroadcaster*, ConfigurationHints ) override;

    svtools::ColorConfig &  GetColorConfig();

    SmMathConfig *          GetConfig();
    SmSymbolManager &       GetSymbolManager();

    static void GetState(SfxItemSet&);

    const SvtSysLocale& GetSysLocale();

    VirtualDevice &     GetDefaultVirtualDev();

    //virtual methods for options dialog
    virtual std::optional<SfxItemSet> CreateItemSet( sal_uInt16 nId ) override;
    virtual void         ApplyItemSet( sal_uInt16 nId, const SfxItemSet& rSet ) override;
    virtual std::unique_ptr<SfxTabPage> CreateTabPage( sal_uInt16 nId, weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& rSet ) override;

    static auto get() { return static_cast<SmModule*>(SfxApplication::GetModule(SfxToolsModule::Math)); }
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
