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

#include <sfx2/request.hxx>

#include <sfx2/viewfrm.hxx>
#include <unotools/moduleoptions.hxx>

#include <hlmailtp.hxx>

#include <comphelper/lok.hxx>

using namespace ::com::sun::star;

/*************************************************************************
|*
|* Constructor / Destructor
|*
|************************************************************************/
SvxHyperlinkMailTp::SvxHyperlinkMailTp(weld::Container* pParent, SvxHpLinkDlg* pDlg, const SfxItemSet* pItemSet)
    : SvxHyperlinkTabPageBase(pParent, pDlg, u"cui/ui/hyperlinkmailpage.ui"_ustr, u"HyperlinkMailPage"_ustr, pItemSet)
    , m_xCbbReceiver(new SvxHyperURLBox(xBuilder->weld_combo_box(u"receiver"_ustr)))
    , m_xBtAdrBook(xBuilder->weld_button(u"addressbook"_ustr))
    , m_xEdSubject(xBuilder->weld_entry(u"subject"_ustr))
{
    m_xCbbReceiver->SetSmartProtocol(INetProtocol::Mailto);

    InitStdControls();

    m_xCbbReceiver->show();

    SetExchangeSupport ();

    // set handlers
    m_xBtAdrBook->connect_clicked( LINK ( this, SvxHyperlinkMailTp, ClickAdrBookHdl_Impl ) );
    m_xCbbReceiver->connect_changed( LINK ( this, SvxHyperlinkMailTp, ModifiedReceiverHdl_Impl) );

    if (!SvtModuleOptions().IsDataBaseInstalled() || comphelper::LibreOfficeKit::isActive())
        m_xBtAdrBook->hide();
}

SvxHyperlinkMailTp::~SvxHyperlinkMailTp()
{
}

/*************************************************************************
|*
|* Fill the all dialog-controls except controls in groupbox "more..."
|*
|************************************************************************/

void SvxHyperlinkMailTp::FillDlgFields(const OUString& rStrURL)
{
    OUString aStrScheme = GetSchemeFromURL(rStrURL);

    // set URL-field and additional controls
    OUString aStrURLc (rStrURL);
    // set additional controls for EMail:
    if ( aStrScheme.startsWith( INET_MAILTO_SCHEME ) )
    {
        // Find mail-subject
        OUString aStrSubject, aStrTmp( aStrURLc );

        sal_Int32 nPos = aStrTmp.toAsciiLowerCase().indexOf( "subject" );

        if ( nPos != -1 )
            nPos = aStrTmp.indexOf( '=', nPos );

        if ( nPos != -1 )
            aStrSubject = aStrURLc.copy( nPos+1 );

        nPos = aStrURLc.indexOf( '?' );

        if ( nPos != -1 )
            aStrURLc = aStrURLc.copy( 0, nPos );

        m_xEdSubject->set_text( aStrSubject );
    }
    else
    {
        m_xEdSubject->set_text(u""_ustr);
    }

    m_xCbbReceiver->set_entry_text(aStrURLc);

    SetScheme( aStrScheme );
}

/*************************************************************************
|*
|* retrieve and prepare data from dialog-fields
|*
|************************************************************************/
void SvxHyperlinkMailTp::GetCurrentItemData ( OUString& rStrURL, OUString& aStrName,
                                             OUString& aStrIntName, SvxLinkInsertMode& eMode )
{
    rStrURL = CreateAbsoluteURL();
    GetDataFromCommonFields( aStrName, aStrIntName, eMode );
}

OUString SvxHyperlinkMailTp::CreateAbsoluteURL() const
{
    OUString aStrURL = m_xCbbReceiver->get_active_text();
    INetURLObject aURL(aStrURL, INetProtocol::Mailto);

    // subject for EMail-url
    if( aURL.GetProtocol() == INetProtocol::Mailto )
    {
        if (!m_xEdSubject->get_text().isEmpty())
        {
            OUString aQuery = "subject=" + m_xEdSubject->get_text();
            aURL.SetParam(aQuery);
        }
    }

    if ( aURL.GetProtocol() != INetProtocol::NotValid )
        return aURL.GetMainURL( INetURLObject::DecodeMechanism::WithCharset );
    else //#105788# always create a URL even if it is not valid
        return aStrURL;
}

/*************************************************************************
|*
|* static method to create Tabpage
|*
|************************************************************************/

std::unique_ptr<IconChoicePage> SvxHyperlinkMailTp::Create(weld::Container* pWindow, SvxHpLinkDlg* pDlg, const SfxItemSet* pItemSet)
{
    return std::make_unique<SvxHyperlinkMailTp>(pWindow, pDlg, pItemSet);
}

/*************************************************************************
|*
|* Set initial focus
|*
|************************************************************************/
void SvxHyperlinkMailTp::SetInitFocus()
{
    m_xCbbReceiver->grab_focus();
}

/*************************************************************************
|************************************************************************/
void SvxHyperlinkMailTp::SetScheme(std::u16string_view rScheme)
{
    //update target:
    RemoveImproperProtocol(rScheme);
    m_xCbbReceiver->SetSmartProtocol( INetProtocol::Mailto );

    //show/hide  special fields for MAIL:
    m_xBtAdrBook->set_sensitive(true);
    m_xEdSubject->set_sensitive(true);
}

/*************************************************************************
|*
|* Remove protocol if it does not fit to the current button selection
|*
|************************************************************************/
void SvxHyperlinkMailTp::RemoveImproperProtocol(std::u16string_view aProperScheme)
{
    OUString aStrURL(m_xCbbReceiver->get_active_text());
    if ( !aStrURL.isEmpty() )
    {
        OUString aStrScheme = GetSchemeFromURL( aStrURL );
        if ( !aStrScheme.isEmpty() && aStrScheme != aProperScheme )
        {
            aStrURL = aStrURL.copy( aStrScheme.getLength() );
            m_xCbbReceiver->set_entry_text(aStrURL);
        }
    }
}

/*************************************************************************
|*
|* Contents of editfield "receiver" modified
|*
|************************************************************************/
IMPL_LINK_NOARG(SvxHyperlinkMailTp, ModifiedReceiverHdl_Impl, weld::ComboBox&, void)
{
    OUString aScheme = GetSchemeFromURL( m_xCbbReceiver->get_active_text() );
    if(!aScheme.isEmpty())
        SetScheme( aScheme );
}

/*************************************************************************
|*
|* Click on imagebutton : addressbook
|*
|************************************************************************/
IMPL_STATIC_LINK_NOARG(SvxHyperlinkMailTp, ClickAdrBookHdl_Impl, weld::Button&, void)
{
    if (SfxViewFrame* pViewFrame = SfxViewFrame::Current())
    {
        SfxItemPool &rPool = pViewFrame->GetPool();
        SfxRequest aReq(SID_VIEW_DATA_SOURCE_BROWSER, SfxCallMode::SLOT, rPool);
        pViewFrame->ExecuteSlot( aReq, true );
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
