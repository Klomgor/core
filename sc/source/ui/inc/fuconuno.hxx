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

#include "fuconstr.hxx"

#include <scdllapi.h> // SC_DLLPUBLIC is needed for unittest

enum class SdrInventor : sal_uInt32;

/** Draw Control */
class SAL_DLLPUBLIC_RTTI FuConstUnoControl final : public FuConstruct
{
    SdrInventor nInventor;
    SdrObjKind nIdentifier;

public:
    FuConstUnoControl(ScTabViewShell& rViewSh, vcl::Window* pWin, ScDrawView* pView,
                       SdrModel& rDoc, const SfxRequest& rReq);

    virtual ~FuConstUnoControl() override;
                                       // Mouse- & Key-Events
    SC_DLLPUBLIC virtual bool MouseButtonUp(const MouseEvent& rMEvt) override;
    SC_DLLPUBLIC virtual bool MouseButtonDown(const MouseEvent& rMEvt) override;

    SC_DLLPUBLIC virtual void Activate() override;
    SC_DLLPUBLIC virtual void Deactivate() override;

    // Create default drawing objects via keyboard
    virtual rtl::Reference<SdrObject> CreateDefaultObject(const sal_uInt16 nID, const tools::Rectangle& rRectangle) override;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
