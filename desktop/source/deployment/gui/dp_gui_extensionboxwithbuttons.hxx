/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
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

#include "dp_gui_extlistbox.hxx"

#include <tools/gen.hxx>
#include <vcl/event.hxx>

namespace dp_gui
{
class ExtMgrDialog;

class ExtensionBoxWithButtons : public ExtensionBox
{
    bool m_bInterfaceLocked;

    ExtMgrDialog* m_pParent;

    void SetButtonStatus(const TEntry_Impl& rEntry);
    OUString ShowPopupMenu(const Point& rPos, const tools::Long nPos);

public:
    explicit ExtensionBoxWithButtons(ExtMgrDialog* pParentDialog,
                                     std::unique_ptr<weld::ScrolledWindow> xScroll,
                                     TheExtensionManager& rManager);

    virtual bool MouseButtonDown(const MouseEvent& rMEvt) override;
    virtual bool Command(const CommandEvent& rCEvt) override;

    virtual void RecalcAll() override;
    virtual void selectEntry(const tools::Long nPos) override;

    void enableButtons(bool bEnable);
};

} // namespace dp_gui

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
