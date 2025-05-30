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

#include <comphelper/lok.hxx>
#include <comphelper/string.hxx>
#include <sfx2/linkmgr.hxx>
#include <sfx2/sfxsids.hrc>
#include <com/sun/star/document/UpdateDocMode.hpp>
#include <officecfg/Office/Common.hxx>
#include <osl/file.hxx>
#include <sfx2/objsh.hxx>
#include <svl/urihelper.hxx>
#include <sot/formats.hxx>
#include <tools/urlobj.hxx>
#include <sot/exchange.hxx>
#include <tools/debug.hxx>
#include <vcl/filter/SvmReader.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <vcl/gdimtf.hxx>
#include <sfx2/lnkbase.hxx>
#include <sfx2/app.hxx>
#include <vcl/graph.hxx>
#include <svl/stritem.hxx>
#include <svl/eitem.hxx>
#include <svl/intitem.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <vcl/dibtools.hxx>
#include <unotools/charclass.hxx>
#include <unotools/securityoptions.hxx>
#include <vcl/GraphicLoader.hxx>
#include <vcl/TypeSerializer.hxx>

#include "fileobj.hxx"
#include "impldde.hxx"
#include <sfx2/strings.hrc>
#include <sfx2/sfxresid.hxx>

#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/util/XCloseable.hpp>

using ::com::sun::star::uno::UNO_QUERY;
using ::com::sun::star::uno::Reference;
using ::com::sun::star::lang::XComponent;
using ::com::sun::star::util::XCloseable;

namespace sfx2
{

namespace {

class SvxInternalLink : public sfx2::SvLinkSource
{
public:
    SvxInternalLink() {}

    virtual bool Connect( sfx2::SvBaseLink* ) override;
};

}

LinkManager::LinkManager(SfxObjectShell* p)
    : pPersist( p )
{
}

LinkManager::~LinkManager()
{
    for(tools::SvRef<SvBaseLink> & rTmp : aLinkTbl)
    {
        if( rTmp.is() )
        {
            rTmp->Disconnect();
            rTmp->SetLinkManager( nullptr );
        }
    }
}

void LinkManager::InsertCachedComp(const Reference<XComponent>& xComp)
{
    maCachedComps.push_back(xComp);
}

void LinkManager::CloseCachedComps()
{
    for (const auto& rxCachedComp : maCachedComps)
    {
        Reference<XCloseable> xCloseable(rxCachedComp, UNO_QUERY);
        if (!xCloseable.is())
            continue;

        xCloseable->close(true);
    }
    maCachedComps.clear();
}

void LinkManager::Remove( SvBaseLink const *pLink )
{
    // No duplicate links inserted
    bool bFound = false;
    for( size_t n = 0; n < aLinkTbl.size(); )
    {
        tools::SvRef<SvBaseLink>& rTmp = aLinkTbl[ n ];
        if( pLink == rTmp.get() )
        {
            rTmp->Disconnect();
            rTmp->SetLinkManager( nullptr );
            rTmp.clear();
            bFound = true;
        }

        // Remove empty ones if they exist
        if( !rTmp.is() )
        {
            aLinkTbl.erase( aLinkTbl.begin() + n );
            if( bFound )
                return ;
        }
        else
            ++n;
    }
}

void LinkManager::Remove( size_t nPos, size_t nCnt )
{
    if( !nCnt || nPos >= aLinkTbl.size() )
        return;

    if (sal::static_int_cast<size_t>(nPos + nCnt) > aLinkTbl.size())
        nCnt = aLinkTbl.size() - nPos;

    for( size_t n = nPos; n < nPos + nCnt; ++n)
    {
        tools::SvRef<SvBaseLink>& rTmp = aLinkTbl[ n ];
        if( rTmp.is() )
        {
            rTmp->Disconnect();
            rTmp->SetLinkManager( nullptr );
        }
    }
    aLinkTbl.erase( aLinkTbl.begin() + nPos, aLinkTbl.begin() + nPos + nCnt );
}

bool LinkManager::Insert( SvBaseLink* pLink )
{
    for( size_t n = 0; n < aLinkTbl.size(); ++n )
    {
        tools::SvRef<SvBaseLink>& rTmp = aLinkTbl[ n ];
        if( !rTmp.is() )
        {
            aLinkTbl.erase( aLinkTbl.begin() + n-- );
        }
        else if( pLink == rTmp.get() )
            return false; // No duplicate links inserted
    }

    pLink->SetLinkManager( this );
    aLinkTbl.emplace_back(pLink );
    return true;
}

bool LinkManager::InsertLink( SvBaseLink * pLink,
                                SvBaseLinkObjectType nObjType,
                                SfxLinkUpdateMode nUpdateMode,
                                const OUString* pName )
{
    // This First
    pLink->SetObjType( nObjType );
    if( pName )
        pLink->SetName( *pName );
    pLink->SetUpdateMode( nUpdateMode );
    return Insert( pLink );
}

void LinkManager::InsertDDELink( SvBaseLink * pLink,
                                    const OUString& rServer,
                                    std::u16string_view rTopic,
                                    std::u16string_view rItem )
{
    if( !isClientType( pLink->GetObjType() ) )
        return;

    OUString sCmd;
    ::sfx2::MakeLnkName( sCmd, &rServer, rTopic, rItem );

    pLink->SetObjType( SvBaseLinkObjectType::ClientDde );
    pLink->SetName( sCmd );
    Insert( pLink );
}

void LinkManager::InsertDDELink( SvBaseLink * pLink )
{
    DBG_ASSERT( isClientType(pLink->GetObjType()), "no OBJECT_CLIENT_SO" );
    if( !isClientType( pLink->GetObjType() ) )
        return;

    if( pLink->GetObjType() == SvBaseLinkObjectType::ClientSo )
        pLink->SetObjType( SvBaseLinkObjectType::ClientDde );

    Insert( pLink );
}

// Obtain the string for the dialog
bool LinkManager::GetDisplayNames( const SvBaseLink * pLink,
                                        OUString* pType,
                                        OUString* pFile,
                                        OUString* pLinkStr,
                                        OUString* pFilter )
{
    bool bRet = false;
    const OUString& sLNm( pLink->GetLinkSourceName() );
    if( !sLNm.isEmpty() )
    {
        switch( pLink->GetObjType() )
        {
            case SvBaseLinkObjectType::ClientFile:
            case SvBaseLinkObjectType::ClientGraphic:
            case SvBaseLinkObjectType::ClientOle:
                {
                    sal_Int32 nPos = 0;
                    OUString sFile( sLNm.getToken( 0, ::sfx2::cTokenSeparator, nPos ) );
                    OUString sRange( sLNm.getToken( 0, ::sfx2::cTokenSeparator, nPos ) );

                    if( pFile )
                        *pFile = sFile;
                    if( pLinkStr )
                        *pLinkStr = sRange;
                    if( pFilter )
                        *pFilter = nPos == -1 ? OUString() : sLNm.copy(nPos);

                    if( pType )
                    {
                        SvBaseLinkObjectType nObjType = pLink->GetObjType();
                        *pType = SfxResId(
                                    ( SvBaseLinkObjectType::ClientFile == nObjType || SvBaseLinkObjectType::ClientOle == nObjType )
                                            ? RID_SVXSTR_FILELINK
                                            : RID_SVXSTR_GRAPHICLINK);
                    }
                    bRet = true;
                }
                break;
            case SvBaseLinkObjectType::ClientDde:
                {
                    sal_Int32 nTmp = 0;
                    OUString sServer( sLNm.getToken( 0, cTokenSeparator, nTmp ) );
                    OUString sTopic( sLNm.getToken( 0, cTokenSeparator, nTmp ) );
                    OUString sLinkStr( sLNm.getToken(0, cTokenSeparator, nTmp) );

                    if( pType )
                        *pType = sServer;
                    if( pFile )
                        *pFile = sTopic;
                    if( pLinkStr )
                        *pLinkStr = sLinkStr;
                    bRet = true;
                }
                break;
            default:
                break;
        }
    }

    return bRet;
}

static void disallowAllLinksUpdate(SvBaseLink* pShellProvider)
{
    if (SfxObjectShell* pShell = pShellProvider->GetLinkManager()->GetPersist())
        pShell->getEmbeddedObjectContainer().setUserAllowsLinkUpdate(false);
}

void LinkManager::UpdateAllLinks(
    bool bAskUpdate,
    bool bUpdateGrfLinks,
    weld::Window* pParentWin,
    OUString const & referer )
{
    // when active content is disabled don't bother updating all links
    // also (when bAskUpdate == true) don't show the pop up.
    if(officecfg::Office::Common::Security::Scripting::DisableActiveContent::get()
       || SvtSecurityOptions::isUntrustedReferer(referer))
        return;

    // First make a copy of the array in order to update links
    // links in ... no contact between them!
    std::vector<SvBaseLink*> aTmpArr;
    for( size_t n = 0; n < aLinkTbl.size(); ++n )
    {
        tools::SvRef<SvBaseLink>& rLink = aLinkTbl[ n ];
        if( !rLink.is() )
        {
            Remove( n-- );
            continue;
        }
        aTmpArr.push_back( rLink.get() );
    }

    for(SvBaseLink* pLink : aTmpArr)
    {
        // search first in the array after the entry
        bool bFound = false;
        for(const tools::SvRef<SvBaseLink> & i : aLinkTbl)
            if( pLink == i.get() )
            {
                bFound = true;
                break;
            }

        if( !bFound )
            continue;  // was not available!

        // Graphic-Links not to update yet
        if( !pLink->IsVisible() ||
            ( !bUpdateGrfLinks && SvBaseLinkObjectType::ClientGraphic == pLink->GetObjType() ))
            continue;

        if( bAskUpdate )
        {
            if (comphelper::LibreOfficeKit::isActive())
            {
                // only one document in jail, no update possible
                disallowAllLinksUpdate(pLink);
                return;
            }

            OUString aMsg = SfxResId(STR_QUERY_UPDATE_LINKS);
            INetURLObject aURL(pPersist->getDocumentBaseURL());
            aMsg = aMsg.replaceFirst("%{filename}", aURL.GetLastName());

            std::unique_ptr<weld::MessageDialog> xQueryBox(Application::CreateMessageDialog(pParentWin,
                                                           VclMessageType::Question, VclButtonsType::YesNo, aMsg));
            xQueryBox->set_default_response(RET_YES);

            int nRet = xQueryBox->run();
            if( RET_YES != nRet )
            {
                disallowAllLinksUpdate(pLink);
                return ;        // nothing should be updated
            }
            bAskUpdate = false;  // once is enough
        }

        pLink->Update();
    }
    CloseCachedComps();
}

SvLinkSourceRef LinkManager::CreateObj( SvBaseLink const * pLink )
{
    switch( pLink->GetObjType() )
    {
        case SvBaseLinkObjectType::ClientFile:
        case SvBaseLinkObjectType::ClientGraphic:
        case SvBaseLinkObjectType::ClientOle:
            return new SvFileObject;
        case SvBaseLinkObjectType::Internal:
            if(officecfg::Office::Common::Security::Scripting::DisableActiveContent::get())
                return SvLinkSourceRef();
            return new SvxInternalLink;
        case SvBaseLinkObjectType::ClientDde:
            if (officecfg::Office::Common::Security::Scripting::DisableActiveContent::get())
                return SvLinkSourceRef();
            return new SvDDEObject;
        default:
            return SvLinkSourceRef();
       }
}

bool LinkManager::InsertServer( SvLinkSource* pObj )
{
    // no duplicate inserts
    if( !pObj )
        return false;

    return aServerTbl.insert( pObj ).second;
}

void LinkManager::RemoveServer( SvLinkSource* pObj )
{
    aServerTbl.erase( pObj );
}

void MakeLnkName( OUString& rName, const OUString* pType, std::u16string_view rFile,
                    std::u16string_view rLink, const OUString* pFilter )
{
    if( pType )
    {
        rName = comphelper::string::strip(*pType, ' ')
            + OUStringChar(cTokenSeparator);
    }
    else
        rName.clear();

    rName += rFile;

    rName = comphelper::string::strip(rName, ' ')
        + OUStringChar(cTokenSeparator);
    rName = comphelper::string::strip(rName, ' ') + rLink;
    if( pFilter )
    {
        rName += OUStringChar(cTokenSeparator) + *pFilter;
        rName = comphelper::string::strip(rName, ' ');
    }
}

void LinkManager::ReconnectDdeLink(SfxObjectShell& rServer)
{
    SfxMedium* pMed = rServer.GetMedium();
    if (!pMed)
        return;

    const ::sfx2::SvBaseLinks& rLinks = GetLinks();
    size_t n = rLinks.size();

    for (size_t i = 0; i < n; ++i)
    {
        ::sfx2::SvBaseLink* p = rLinks[i].get();
        OUString aType, aFile, aLink, aFilter;
        if (!GetDisplayNames(p, &aType, &aFile, &aLink, &aFilter))
            continue;

        if (aType != "soffice")
            // DDE connections between OOo apps are always named 'soffice'.
            continue;

        OUString aTmp;
        OUString aURL = aFile;
        if (osl::FileBase::getFileURLFromSystemPath(aFile, aTmp)
            == osl::FileBase::E_None)
            aURL = aTmp;

        if (!aURL.equalsIgnoreAsciiCase(pMed->GetName()))
            // This DDE link is not associated with this server shell...  Skip it.
            continue;

        if (aLink.isEmpty())
            continue;

        LinkServerShell(aLink, rServer, *p);
    }
}

void LinkManager::LinkServerShell(const OUString& rPath, SfxObjectShell& rServer, ::sfx2::SvBaseLink& rLink)
{
    ::sfx2::SvLinkSource* pSrvSrc = rServer.DdeCreateLinkSource(rPath);
    if (pSrvSrc)
    {
        css::datatransfer::DataFlavor aFl;
        SotExchange::GetFormatDataFlavor(rLink.GetContentType(), aFl);
        rLink.SetObj(pSrvSrc);
        pSrvSrc->AddDataAdvise(
            &rLink, aFl.MimeType,
            SfxLinkUpdateMode::ONCALL == rLink.GetUpdateMode() ? ADVISEMODE_ONLYONCE : 0);
    }
}

void LinkManager::InsertFileLink(
    sfx2::SvBaseLink& rLink, SvBaseLinkObjectType nFileType, std::u16string_view rFileNm,
    const OUString* pFilterNm, const OUString* pRange)
{
    if (!isClientType(rLink.GetObjType()))
        return;

    OUStringBuffer aBuf(64);
    aBuf.append(rFileNm + OUStringChar(sfx2::cTokenSeparator));

    if (pRange)
        aBuf.append(*pRange);

    if (pFilterNm)
    {
        aBuf.append(OUStringChar(sfx2::cTokenSeparator) + *pFilterNm);
    }

    OUString aCmd = aBuf.makeStringAndClear();
    InsertLink(&rLink, nFileType, SfxLinkUpdateMode::ONCALL, &aCmd);
}

// A transfer is aborted, so cancel all download media
// (for now this is only of interest for the file links!)
void LinkManager::CancelTransfers()
{

    const sfx2::SvBaseLinks& rLnks = GetLinks();
    for( size_t n = rLnks.size(); n; )
    {
        const sfx2::SvBaseLink& rLnk = *rLnks[--n];
        if (isClientFileType(rLnk.GetObjType()))
        {
            if (SvFileObject* pFileObj = static_cast<SvFileObject*>(rLnk.GetObj()))
                pFileObj->CancelTransfers();
        }
    }
}

// For the purpose of sending Status information from the file object to
// the base link, there exist a dedicated ClipBoardId. The SvData-object
// gets the appropriate information as a string
// For now this is required for file object in conjunction with JavaScript
// - needs information about Load/Abort/Error
SotClipboardFormatId LinkManager::RegisterStatusInfoId()
{
    static SotClipboardFormatId nFormat = SotClipboardFormatId::NONE;

    if( nFormat == SotClipboardFormatId::NONE )
    {
        nFormat = SotExchange::RegisterFormatName(
                    u"StatusInfo from SvxInternalLink"_ustr);
    }
    return nFormat;
}

bool LinkManager::GetGraphicFromAny(std::u16string_view rMimeType,
                                    const css::uno::Any & rValue,
                                    Graphic& rGraphic,
                                    weld::Window* pParentWin)
{
    bool bRet = false;

    if (!rValue.hasValue())
        return bRet;

    if (rValue.has<OUString>())
    {
        OUString sReferer;
        SfxObjectShell* sh = GetPersist();
        if (sh && sh->HasName())
            sReferer = sh->GetMedium()->GetName();

        OUString sURL = rValue.get<OUString>();
        if (!SvtSecurityOptions::isUntrustedReferer(sReferer) &&
            !INetURLObject(sURL).IsExoticProtocol())
        {
            rGraphic = vcl::graphic::loadFromURL(sURL, pParentWin);
        }
        if (rGraphic.IsNone())
            rGraphic.SetDefaultType();
        rGraphic.setOriginURL(sURL);
        return true;
    }
    else if (rValue.has<css::uno::Sequence<sal_Int8>>())
    {
        auto aSeq = rValue.get<css::uno::Sequence<sal_Int8>>();

        SvMemoryStream aMemStm( const_cast<sal_Int8 *>(aSeq.getConstArray()), aSeq.getLength(),
                                StreamMode::READ );
        aMemStm.Seek( 0 );

        switch( SotExchange::GetFormatIdFromMimeType( rMimeType ) )
        {
        case SotClipboardFormatId::SVXB:
            {
                TypeSerializer aSerializer(aMemStm);
                aSerializer.readGraphic(rGraphic);
                bRet = true;
            }
            break;
        case SotClipboardFormatId::GDIMETAFILE:
            {
                GDIMetaFile aMtf;
                SvmReader aReader( aMemStm );
                aReader.Read( aMtf );
                rGraphic = aMtf;
                bRet = true;
            }
            break;
        case SotClipboardFormatId::BITMAP:
            {
                Bitmap aBmp;
                ReadDIB(aBmp, aMemStm, true);
                rGraphic = BitmapEx(aBmp);
                bRet = true;
            }
            break;
        default: break;
        }
    }
    return bRet;
}

static OUString lcl_DDE_RelToAbs( const OUString& rTopic, std::u16string_view rBaseURL )
{
    OUString sRet;
    INetURLObject aURL( rTopic );
    if( INetProtocol::NotValid == aURL.GetProtocol() )
        osl::FileBase::getFileURLFromSystemPath(rTopic, sRet);
    if( sRet.isEmpty() )
        sRet = URIHelper::SmartRel2Abs( INetURLObject(rBaseURL), rTopic, URIHelper::GetMaybeFileHdl() );
    return sRet;
}

bool SvxInternalLink::Connect( sfx2::SvBaseLink* pLink )
{
    SfxObjectShell* pFndShell = nullptr;
    sal_uInt16 nUpdateMode = css::document::UpdateDocMode::NO_UPDATE;
    OUString sTopic, sItem, sReferer;
    LinkManager* pLinkMgr = pLink->GetLinkManager();
    if (pLinkMgr && sfx2::LinkManager::GetDisplayNames(pLink, nullptr, &sTopic, &sItem) && !sTopic.isEmpty())
    {
        // first only loop over the DocumentShells the shells and find those
        // with the name:
        CharClass aCC( LanguageTag( LANGUAGE_SYSTEM) );

        bool bFirst = true;
        SfxObjectShell* pShell = pLinkMgr->GetPersist();
        if( pShell && pShell->GetMedium() )
        {
            sReferer = pShell->GetMedium()->GetBaseURL();
            const SfxUInt16Item* pItem = pShell->GetMedium()->GetItemSet().GetItem(SID_UPDATEDOCMODE, false);
            if ( pItem )
                nUpdateMode = pItem->GetValue();
        }

        OUString sNmURL(aCC.lowercase(lcl_DDE_RelToAbs(sTopic, sReferer)));

        if ( !pShell )
        {
            bFirst = false;
            pShell = SfxObjectShell::GetFirst( nullptr, false );
        }

        OUString sTmp;
        while( pShell )
        {
            if( sTmp.isEmpty() )
            {
                sTmp = pShell->GetTitle( SFX_TITLE_FULLNAME );
                sTmp = lcl_DDE_RelToAbs(sTmp, sReferer );
            }


            sTmp = aCC.lowercase( sTmp );
            if( sTmp == sNmURL )  // we want these
            {
                pFndShell = pShell;
                break;
            }

            if( bFirst )
            {
                bFirst = false;
                pShell = SfxObjectShell::GetFirst( nullptr, false );
            }
            else
                pShell = SfxObjectShell::GetNext( *pShell, nullptr, false );

            sTmp.clear();
        }
    }

    // empty topics are not allowed - which document is it
    if( sTopic.isEmpty() )
        return false;

    if (pFndShell)
    {
        sfx2::SvLinkSource* pNewSrc = pFndShell->DdeCreateLinkSource( sItem );
        if( pNewSrc )
        {
            css::datatransfer::DataFlavor aFl;
            SotExchange::GetFormatDataFlavor( pLink->GetContentType(), aFl );

            pLink->SetObj( pNewSrc );
            pNewSrc->AddDataAdvise( pLink, aFl.MimeType,
                                SfxLinkUpdateMode::ONCALL == pLink->GetUpdateMode()
                                    ? ADVISEMODE_ONLYONCE
                                    : 0 );
            return true;
        }
    }
    else
    {
        // then try to download the file:
        INetURLObject aURL( sTopic );
        INetProtocol eOld = aURL.GetProtocol();
        sTopic = lcl_DDE_RelToAbs( sTopic, sReferer );
        aURL.SetURL( sTopic );
        if( INetProtocol::NotValid != eOld ||
            INetProtocol::Http != aURL.GetProtocol() )
        {
            SfxStringItem aName( SID_FILE_NAME, sTopic );
            SfxBoolItem aMinimized(SID_MINIMIZED, true);
            SfxBoolItem aHidden(SID_HIDDEN, true);
            SfxStringItem aTarget( SID_TARGETNAME, u"_blank"_ustr );
            SfxStringItem aReferer( SID_REFERER, sReferer );
            SfxUInt16Item aUpdate( SID_UPDATEDOCMODE, nUpdateMode );
            SfxBoolItem aReadOnly(SID_DOC_READONLY, false);

            // Disable automatic re-connection to avoid this link instance
            // being destroyed at re-connection.
            SfxBoolItem aDdeConnect(SID_DDE_RECONNECT_ONLOAD, false);

            // #i14200# (DDE-link crashes wordprocessor)
            SfxAllItemSet aArgs( SfxGetpApp()->GetPool() );
            aArgs.Put(aReferer);
            aArgs.Put(aTarget);
            aArgs.Put(aHidden);
            aArgs.Put(aMinimized);
            aArgs.Put(aName);
            aArgs.Put(aUpdate);
            aArgs.Put(aReadOnly);
            aArgs.Put(aDdeConnect);
            Reference<XComponent> xComp = SfxObjectShell::CreateAndLoadComponent(aArgs);
            pFndShell = SfxObjectShell::GetShellFromComponent(xComp);
            if (xComp.is() && pFndShell && pLinkMgr)
            {
                pLinkMgr->InsertCachedComp(xComp);
                sfx2::LinkManager::LinkServerShell(sItem, *pFndShell, *pLink);
                return true;
            }
        }
    }

    return false;
}

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
