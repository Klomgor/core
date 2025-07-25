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

#include <memory>
#include <com/sun/star/embed/XEmbedPersist.hpp>
#include <com/sun/star/embed/Aspects.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <sal/log.hxx>
#include <vcl/graphicfilter.hxx>
#include <vcl/gdimtf.hxx>
#include <svl/itemiter.hxx>
#include <tools/UnitConversion.hxx>

#include <svtools/embedhlp.hxx>

#include <hintids.hxx>
#include <editeng/boxitem.hxx>
#include <editeng/shaditem.hxx>
#include <filter/msfilter/msoleexp.hxx>
#include <editeng/fhgtitem.hxx>
#include <svx/svdoole2.hxx>

#include <unotools/ucbstreamhelper.hxx>
#include <fmtanchr.hxx>
#include <ndgrf.hxx>
#include <frmfmt.hxx>
#include <grfatr.hxx>
#include <ndole.hxx>
#include <fmtfsize.hxx>
#include <fmtornt.hxx>

#include "sprmids.hxx"

#include <doc.hxx>
#include "writerhelper.hxx"
#include "writerwordglue.hxx"
#include "ww8struc.hxx"
#include "wrtww8.hxx"
#include "ww8par.hxx"
#include "escher.hxx"
//Added for i120568
#include "ww8attributeoutput.hxx"
#include <fmturl.hxx>

#include <IDocumentDrawModelAccess.hxx>
#include <drawdoc.hxx>
#include <o3tl/string_view.hxx>
#include <officecfg/Office/Common.hxx>

using namespace ::com::sun::star;

// TODO:
// 5. convert the MapModes that Widows can't handle

// OutGrf () is called for every GrfNode in the document. A PicLocFc-Sprm
// will be inserted, which contains a magic number instead of an address.
// The GrfNode-Ptr is saved in Graf-Class (used later for output of
// the graphic and patching of the PicLocFc attributes)

void WW8Export::OutputGrfNode( const SwGrfNode& /*rNode*/ )
{
    SAL_INFO("sw", "WW8Export::OutputGrfNode( const SwGrfNode& )" );
    OSL_ENSURE( m_pParentFrame, "frame not set!" );
    if ( m_pParentFrame )
    {
        OutGrf( *m_pParentFrame );
        m_pFib->m_fHasPic = true;
    }
}

bool WW8Export::TestOleNeedsGraphic(const SwAttrSet& rSet, rtl::Reference<SotStorage> const& xOleStg,
                                    const rtl::Reference<SotStorage>& xObjStg,
                                    OUString const& rStorageName, SwOLENode& rOLENd)
{
    bool bGraphicNeeded = false;
    SfxItemIter aIter( rSet );
    for (auto pItem = aIter.GetCurItem(); !bGraphicNeeded && pItem; pItem = aIter.NextItem())
    {
        switch (pItem->Which())
        {
            /*
            For an inline object these properties are irrelevant because they
            will be the same as the defaults that msword applies in their
            absence, so if that is all that there is for these inline objects
            then if there turns out to be enough information in the object
            itself to regenerate the correct size and preview of the object
            then we will not need to provide an additional graphics preview in
            the data stream, which can save a lot of disk space.
            */
            case RES_FRM_SIZE:
            case RES_CNTNT:
            case RES_VERT_ORIENT:
            case RES_ANCHOR:
                break;
            default:
                bGraphicNeeded = true;
        }
    }

    /*
    Now we must see if the object contains a preview itself which is equal to
    the preview that we are currently using. If the graphics are equal then we
    don't need to store another preview
    */
    GDIMetaFile aWMF;
    tools::Long nX=0,nY=0;
    if (!bGraphicNeeded && SwWW8ImplReader::ImportOleWMF(xOleStg,aWMF,nX,nY))
    {
        // bGraphicNeeded set to true is right / fixes #i51670#.
        bGraphicNeeded = true;
        tools::Rectangle aRect( Point(), Size( nX, nY ) );
        Graphic aGraph(aWMF);

        ErrCode nErr = ERRCODE_NONE;
        sal_Int64 nAspect = rOLENd.GetAspect();
        rtl::Reference<SdrOle2Obj> pRet = SvxMSDffManager::CreateSdrOLEFromStorage(
            m_rDoc.getIDocumentDrawModelAccess().GetOrCreateDrawModel(),
            rStorageName,
            xObjStg,
            m_rDoc.GetDocStorage(),
            aGraph,
            aRect,
            tools::Rectangle(),
            nullptr,
            nErr,
            0,
            nAspect,
            m_pWriter->GetBaseURL());

        if (pRet)
        {
            uno::Reference< embed::XEmbeddedObject > xObj = rOLENd.GetOLEObj().GetOleRef();
            if ( xObj.is() )
            {
                std::unique_ptr<SvStream> pGraphicStream;
                comphelper::EmbeddedObjectContainer aCnt( m_rDoc.GetDocStorage() );
                try
                {
                    uno::Reference< embed::XEmbedPersist > xPersist(xObj, uno::UNO_QUERY );
                    if (xPersist)
                    {
                        // it makes no sense to search the object in the container by reference since the object was created
                        // outside of the container and was not inserted there, only the name makes sense
                        pGraphicStream =
                                ::utl::UcbStreamHelper::CreateStream( aCnt.GetGraphicStream( xPersist->getEntryName() ) );
                    }
                }
                catch( const uno::Exception& )
                {}

                OSL_ENSURE( pGraphicStream && !pGraphicStream->GetError(), "No graphic stream available!" );
                if ( pGraphicStream && !pGraphicStream->GetError() )
                {
                    Graphic aGr1;
                    GraphicFilter& rGF = GraphicFilter::GetGraphicFilter();
                    if( rGF.ImportGraphic( aGr1, u"", *pGraphicStream ) == ERRCODE_NONE )
                    {
                        Graphic aGr2;
                        pGraphicStream =
                                ::utl::UcbStreamHelper::CreateStream( aCnt.GetGraphicStream( pRet->GetObjRef() ) );
                        if( pGraphicStream && rGF.ImportGraphic( aGr2, u"", *pGraphicStream ) == ERRCODE_NONE )
                        {
                            if ( aGr1 == aGr2 )
                                bGraphicNeeded = false;
                        }
                    }
                }
            }
        }
    }
    else
        bGraphicNeeded = true;
    return bGraphicNeeded;
}

void WW8Export::OutputOLENode( const SwOLENode& rOLENode )
{
    SAL_INFO("sw", "WW8Export::OutputOLENode( const SwOLENode& rOLENode )" );
    sal_uInt8 *pSpecOLE;
    sal_uInt8 *pDataAdr;
    short nSize;
    sal_uInt8 aSpecOLE_WW8[] = {
            0x03, 0x6a, 0, 0, 0, 0, // sprmCPicLocation
            0x0a, 0x08, 1,          // sprmCFOLE2
            0x56, 0x08, 1           // sprmCFObj
        };

    pSpecOLE = aSpecOLE_WW8;
    nSize = sizeof( aSpecOLE_WW8 );
    pDataAdr = pSpecOLE + 2; //WW6 sprm is 1 but has 1 byte len as well.

    rtl::Reference<SotStorage> xObjStg = GetWriter().GetStorage().OpenSotStorage(SL::aObjectPool);

    if( !xObjStg.is()  )
        return;

    uno::Reference < embed::XEmbeddedObject > xObj(const_cast<SwOLENode&>(rOLENode).GetOLEObj().GetOleRef());
    if( !xObj.is() )
        return;

    const embed::XEmbeddedObject *pObj = xObj.get();
    //Don't want to use pointer ids, as is traditional, because we need
    //to put this into a 32bit value, and on 64bit the bottom bits
    //might collide and two unrelated ole objects end up considered the
    //same.  Don't want to simply start at 0 which is a special value
    sal_Int32 nPictureId = SAL_MAX_INT32 - m_aOleMap.size();
    WW8OleMap::value_type entry = std::make_pair(pObj, nPictureId);
    std::pair<WW8OleMap::iterator, bool> aRes = m_aOleMap.insert(entry);
    bool bIsNotDuplicate = aRes.second; //.second is false when element already existed
    nPictureId = aRes.first->second;
    Set_UInt32(pDataAdr, nPictureId);
    OUString sStorageName = "_" + OUString::number( nPictureId );
    rtl::Reference<SotStorage> xOleStg = xObjStg->OpenSotStorage(sStorageName);
    if( !xOleStg.is() )
        return;

    /*
    If this object storage has been written already don't
    waste time rewriting it
    */
    if (bIsNotDuplicate)
    {
        sal_Int64 nAspect = rOLENode.GetAspect();
        svt::EmbeddedObjectRef aObjRef( xObj, nAspect );

        if ( !m_oOLEExp )
        {
            sal_uInt32 nSvxMSDffOLEConvFlags = 0;
            if (officecfg::Office::Common::Filter::Microsoft::Export::MathToMathType::get())
                nSvxMSDffOLEConvFlags |= OLE_STARMATH_2_MATHTYPE;
            if (officecfg::Office::Common::Filter::Microsoft::Export::WriterToWinWord::get())
                nSvxMSDffOLEConvFlags |= OLE_STARWRITER_2_WINWORD;
            if (officecfg::Office::Common::Filter::Microsoft::Export::CalcToExcel::get())
                nSvxMSDffOLEConvFlags |= OLE_STARCALC_2_EXCEL;
            if (officecfg::Office::Common::Filter::Microsoft::Export::ImpressToPowerPoint::get())
                nSvxMSDffOLEConvFlags |= OLE_STARIMPRESS_2_POWERPOINT;

            m_oOLEExp.emplace( nSvxMSDffOLEConvFlags );
        }

        m_oOLEExp->ExportOLEObject( aObjRef, *xOleStg );
        if ( nAspect == embed::Aspects::MSOLE_ICON )
        {
            OUString aObjInfo( u"\3ObjInfo"_ustr );
            if ( !xOleStg->IsStream( aObjInfo ) )
            {
                const sal_uInt8 pObjInfoData[] = { 0x40, 0x00, 0x03, 0x00 };
                rtl::Reference<SotStorageStream> rObjInfoStream = xOleStg->OpenSotStream(aObjInfo);
                if ( rObjInfoStream.is() && !rObjInfoStream->GetError() )
                {
                    rObjInfoStream->WriteBytes(pObjInfoData, sizeof(pObjInfoData));
                    xOleStg->Commit();
                }
            }
        }
    }

    // write as embedded field - the other things will be done
    // in the escher export
    OUString sServer = FieldString(ww::eEMBED) + xOleStg->GetUserName() + " ";

    OutputField(nullptr, ww::eEMBED, sServer, FieldFlags::Start |
        FieldFlags::CmdStart | FieldFlags::CmdEnd);

    m_pChpPlc->AppendFkpEntry( Strm().Tell(),
            nSize, pSpecOLE );

    bool bEndCR = true;
    /*
    In the word filter we only need a preview image for
    floating images, and then only (the usual case) if the
    object doesn't contain enough information to reconstruct
    what we need.

    We don't need a graphic for inline objects, so we don't
    even need the overhead of a graphic in that case.
    */
    bool bGraphicNeeded = false;

    if (m_pParentFrame)
    {
        bGraphicNeeded = true;

        if (m_pParentFrame->IsInline())
        {
            const SwAttrSet& rSet =
                m_pParentFrame->GetFrameFormat().GetAttrSet();
            bEndCR = false;
            bGraphicNeeded = TestOleNeedsGraphic(rSet,
                xOleStg, xObjStg, sStorageName, const_cast<SwOLENode&>(rOLENode));
        }
    }

    if (!bGraphicNeeded)
        WriteChar(0x1);
    else
    {
        /*
        ##897##
        We need to insert the graphic representation of
        this object for the inline case, otherwise word
        has no place to find the dimensions of the ole
        object, and will not be able to draw it
        */
        OutGrf(*m_pParentFrame);
    }

    OutputField(nullptr, ww::eEMBED, OUString(),
        FieldFlags::End | FieldFlags::Close);

    if (bEndCR) //No newline in inline case
        WriteCR();
}

void WW8Export::OutputLinkedOLE( const OUString& rOleId )
{
    uno::Reference< embed::XStorage > xDocStg = m_rDoc.GetDocStorage();
    uno::Reference< embed::XStorage > xOleStg = xDocStg->openStorageElement( u"OLELinks"_ustr, embed::ElementModes::READ );
    rtl::Reference<SotStorage> xObjSrc = SotStorage::OpenOLEStorage( xOleStg, rOleId, StreamMode::READ );

    rtl::Reference<SotStorage> xObjStg = GetWriter().GetStorage().OpenSotStorage(SL::aObjectPool);

    if( !(xObjStg.is() && xObjSrc.is()) )
        return;

    rtl::Reference<SotStorage> xOleDst = xObjStg->OpenSotStorage(rOleId);
    if ( xOleDst.is() )
        xObjSrc->CopyTo( xOleDst.get() );

    if ( xOleDst->GetError( ) )
        return;

    xOleDst->Commit();

    // Output the cPicLocation attribute
    std::unique_ptr<ww::bytes> pBuf( new ww::bytes );
    SwWW8Writer::InsUInt16( *pBuf, NS_sprm::CPicLocation::val );
    SwWW8Writer::InsUInt32( *pBuf, o3tl::toInt32(rOleId.subView( 1 )) );

    SwWW8Writer::InsUInt16( *pBuf, NS_sprm::CFOle2::val );
    pBuf->push_back( 1 );

    SwWW8Writer::InsUInt16( *pBuf, NS_sprm::CFSpec::val );
    pBuf->push_back( 1 );

    SwWW8Writer::InsUInt16( *pBuf, NS_sprm::CFObj::val );
    pBuf->push_back( 1 );

    m_pChpPlc->AppendFkpEntry( Strm().Tell(), pBuf->size(), pBuf->data() );
}

void WW8Export::OutGrf(const ww8::Frame &rFrame)
{
    //Added for i120568,the hyperlink info within a graphic whose anchor type is "As character"
    //will be exported to ensure the fidelity
    const SwFormatURL& rURL = rFrame.GetFrameFormat().GetAttrSet().GetURL();
    bool bURLStarted = false;
    if( !rURL.GetURL().isEmpty() && rFrame.GetWriterType() == ww8::Frame::eGraphic)
    {
        bURLStarted = true;
        m_pAttrOutput->StartURL( rURL.GetURL(), rURL.GetTargetFrameName() );
    }

    // Store the graphic settings in GrfNode so they may be written-out later
    m_pGrf->Insert(rFrame);

    m_pChpPlc->AppendFkpEntry( Strm().Tell(), m_pO->size(), m_pO->data() );
    m_pO->clear();

    // #i29408#
    // linked, as-character anchored graphics have to be exported as fields.
    const SwGrfNode* pGrfNd = rFrame.IsInline() && rFrame.GetContent()
                              ? rFrame.GetContent()->GetGrfNode() : nullptr;
    if ( pGrfNd && pGrfNd->IsLinkedFile() )
    {
        OUString sStr;
        pGrfNd->GetFileFilterNms(&sStr, nullptr);
        sStr = FieldString(ww::eINCLUDEPICTURE) + " \"" + sStr + "\" \\d";

        OutputField( nullptr, ww::eINCLUDEPICTURE, sStr,
                   FieldFlags::Start | FieldFlags::CmdStart | FieldFlags::CmdEnd );
    }

    WriteChar( char(1) );   // paste graphic symbols in the main text

    sal_uInt8 aArr[ 18 ];
    sal_uInt8* pArr = aArr;

    const SwFrameFormat &rFlyFormat = rFrame.GetFrameFormat();
    const RndStdIds eAn = rFlyFormat.GetAttrSet().GetAnchor(false).GetAnchorId();
    if (eAn == RndStdIds::FLY_AS_CHAR)
    {
        sal_Int16 eVert = rFlyFormat.GetVertOrient().GetVertOrient();
        if ((eVert == text::VertOrientation::CHAR_CENTER) || (eVert == text::VertOrientation::LINE_CENTER))
        {
            bool bVert = false;
            //The default for word in vertical text mode is to center,
            //otherwise a sub/super script hack is employed
            if (auto pTextNd = dynamic_cast< const SwContentNode *>( m_pOutFormatNode ) )
            {
                SwPosition aPos(*pTextNd);
                bVert = m_rDoc.IsInVerticalText(aPos);
            }
            if (!bVert)
            {
                SwTwips nHeight = rFlyFormat.GetFrameSize().GetHeight();
                nHeight/=20; //nHeight was in twips, want it in half points, but
                             //then half of total height.
                tools::Long nFontHeight = GetItem(RES_CHRATR_FONTSIZE).GetHeight();
                nHeight-=nFontHeight/20;

                Set_UInt16( pArr, NS_sprm::CHpsPos::val );
                Set_UInt16( pArr, - static_cast<sal_Int16>(nHeight));
            }
        }
    }

    // sprmCFSpec
    Set_UInt16( pArr, 0x855 );
    Set_UInt8( pArr, 1 );

    // sprmCPicLocation
    Set_UInt16( pArr, NS_sprm::CPicLocation::val );
    Set_UInt32( pArr, GRF_MAGIC_321 );

    // vary Magic, so that different graphic attributes will not be merged
    static sal_uInt8 nAttrMagicIdx = 0;
    --pArr;
    Set_UInt8( pArr, nAttrMagicIdx++ );
    m_pChpPlc->AppendFkpEntry( Strm().Tell(), static_cast< short >(pArr - aArr), aArr );

    // #i75464#
    // Check, if graphic isn't exported as-character anchored.
    // Otherwise, an additional paragraph is exported for a graphic, which is
    // forced to be treated as inline, because it's anchored inside another frame.
    if ( !rFrame.IsInline() &&
         ( (eAn == RndStdIds::FLY_AT_PARA) ||
           (eAn == RndStdIds::FLY_AT_PAGE) ) )
    {
        WriteChar( char(0x0d) ); // close the surrounding frame with CR

        static sal_uInt8 nSty[2] = { 0, 0 };
        m_pO->insert( m_pO->end(), nSty, nSty+2 );     // Style #0
        bool bOldGrf = m_bOutGrf;
        m_bOutGrf = true;

        OutputFormat( rFrame.GetFrameFormat(), false, false, true ); // Fly-Attrs

        m_bOutGrf = bOldGrf;
        m_pPapPlc->AppendFkpEntry( Strm().Tell(), m_pO->size(), m_pO->data() );
        m_pO->clear();
    }
    // #i29408#
    // linked, as-character anchored graphics have to be exported as fields.
    else if ( pGrfNd && pGrfNd->IsLinkedFile() )
    {
        OutputField( nullptr, ww::eINCLUDEPICTURE, OUString(), FieldFlags::Close );
    }
    //Added for i120568,the hyperlink info within a graphic whose anchor type is
    //"As character" will be exported to ensure the fidelity
    if( bURLStarted )
        m_pAttrOutput->EndURL(false);
}

void SwWW8WrGrf::Insert(const ww8::Frame &rFly)
{
    const Size aSize( rFly.GetLayoutSize() );
    const sal_uInt16 nWidth = static_cast< sal_uInt16 >(aSize.Width());
    const sal_uInt16 nHeight = static_cast< sal_uInt16 >(aSize.Height());
    maDetails.emplace_back(rFly, nWidth, nHeight);
}

void SwWW8WrGrf::WritePICFHeader(SvStream& rStrm, const ww8::Frame &rFly,
    sal_uInt16 mm, sal_uInt16 nWidth, sal_uInt16 nHeight, const SwAttrSet* pAttrSet)
{
    sal_Int16 nXSizeAdd = 0, nYSizeAdd = 0;
    sal_Int16 nCropL = 0, nCropR = 0, nCropT = 0, nCropB = 0;

            // write Crop-Attribute content in Header ( if available )
    const SwCropGrf* pCropItem;
    if (pAttrSet && (pCropItem
        = pAttrSet->GetItemIfSet(RES_GRFATR_CROPGRF, false)))
    {
        nCropL = static_cast<sal_Int16>(pCropItem->GetLeft());
        nCropR = static_cast<sal_Int16>(pCropItem->GetRight());
        nCropT = static_cast<sal_Int16>(pCropItem->GetTop());
        nCropB = static_cast<sal_Int16>(pCropItem->GetBottom());
        nXSizeAdd = nXSizeAdd - static_cast<sal_Int16>( pCropItem->GetLeft() + pCropItem->GetRight() );
        nYSizeAdd = nYSizeAdd - static_cast<sal_Int16>( pCropItem->GetTop() + pCropItem->GetBottom() );
    }

    Size aGrTwipSz(rFly.GetSize());
    sal_uInt16 nHdrLen = 0x44;

    sal_uInt8 aArr[ 0x44 ] = { 0 };

    sal_uInt8* pArr = aArr + 0x2E;  // Do borders first

    const SwAttrSet& rAttrSet = rFly.GetFrameFormat().GetAttrSet();
    if (const SvxBoxItem* pBox = rAttrSet.GetItemIfSet(RES_BOX, false))
    {
        bool bShadow = false;               // Shadow ?
        if (const SvxShadowItem* pSI = rAttrSet.GetItem<SvxShadowItem>(RES_SHADOW))
        {
            bShadow = (pSI->GetLocation() != SvxShadowLocation::NONE) &&
                (pSI->GetWidth() != 0);
        }

        static const SvxBoxItemLine aLnArr[4] = { SvxBoxItemLine::TOP, SvxBoxItemLine::LEFT,
                            SvxBoxItemLine::BOTTOM, SvxBoxItemLine::RIGHT };
        for(const SvxBoxItemLine & i : aLnArr)
        {
            const ::editeng::SvxBorderLine* pLn = pBox->GetLine( i );
            WW8_BRC aBrc;
            if (pLn)
            {
                WW8_BRCVer9 aBrc90 = WW8Export::TranslateBorderLine( *pLn,
                    pBox->GetDistance( i ), bShadow );
                sal_uInt8 ico = msfilter::util::TransColToIco(msfilter::util::BGRToRGB(
                    aBrc90.cv()));
                aBrc = WW8_BRC(aBrc90.dptLineWidth(), aBrc90.brcType(), ico,
                    aBrc90.dptSpace(), aBrc90.fShadow(), aBrc90.fFrame());
            }

            // use importer logic to determine how large the exported
            // border will really be in word and adjust accordingly
            short nSpacing;
            short nThick = aBrc.DetermineBorderProperties(&nSpacing);
            switch (i)
            {
                case SvxBoxItemLine::TOP:
                case SvxBoxItemLine::BOTTOM:
                    nHeight -= bShadow ? nThick*2 : nThick;
                    nHeight = nHeight - nSpacing;
                    break;
                case SvxBoxItemLine::LEFT:
                case SvxBoxItemLine::RIGHT:
                default:
                    nWidth -= bShadow ? nThick*2 : nThick;
                    nWidth = nWidth - nSpacing;
                    break;
            }
            memcpy( pArr, &aBrc.aBits1, 2);
            pArr+=2;

            memcpy( pArr, &aBrc.aBits2, 2);
            pArr+=2;
        }
    }

    pArr = aArr + 4;                                // skip lcb
    Set_UInt16( pArr, nHdrLen );                    // set cbHeader

    Set_UInt16( pArr, mm );                         // set mm

    /*
    Just in case our original size is too big to fit inside a ushort we can
    substitute the final size and lose on retaining the scaling factor but
    still keep the correct display size anyway.
    */
    const bool bIsSubstitutedSize = (aGrTwipSz.Width() > SHRT_MAX) || (aGrTwipSz.Height() > SHRT_MAX) ||
                                    aGrTwipSz.IsEmpty();
    if ( bIsSubstitutedSize )
    {
        aGrTwipSz.setWidth( nWidth );
        aGrTwipSz.setHeight( nHeight );
    }
    using namespace sw::types;
    // set xExt & yExt
    Set_UInt16(pArr, msword_cast<sal_uInt16>(convertTwipToMm100(aGrTwipSz.Width())));
    Set_UInt16(pArr, msword_cast<sal_uInt16>(convertTwipToMm100(aGrTwipSz.Height())));
    pArr += 16;
    // skip hMF & rcWinMF
    // set dxaGoal & dyaGoal
    Set_UInt16(pArr, msword_cast<sal_uInt16>(aGrTwipSz.Width()));
    Set_UInt16(pArr, msword_cast<sal_uInt16>(aGrTwipSz.Height()));

    if ( aGrTwipSz.Width() + nXSizeAdd )             // set mx
    {
        if ( !bIsSubstitutedSize )
        {
            const double fVal = nWidth * 1000.0 / (aGrTwipSz.Width() + nXSizeAdd );
            Set_UInt16( pArr, o3tl::narrowing<sal_uInt16>(::rtl::math::round(fVal)) );
        }
        else
        {
            Set_UInt16( pArr, 1000 );
        }
    }
    else
    {
        pArr += 2;
    }

    if ( aGrTwipSz.Height() + nYSizeAdd )            // set my
    {
        if ( !bIsSubstitutedSize )
        {
            const double fVal = nHeight * 1000.0 / (aGrTwipSz.Height() + nYSizeAdd);
            Set_UInt16( pArr, o3tl::narrowing<sal_uInt16>(::rtl::math::round(fVal)) );
        }
        else
        {
            Set_UInt16( pArr, 1000 );
        }
    }
    else
    {
        pArr += 2;
    }

    if ( !bIsSubstitutedSize )
    {
        Set_UInt16( pArr, nCropL );                     // set dxaCropLeft
        Set_UInt16( pArr, nCropT );                     // set dyaCropTop
        Set_UInt16( pArr, nCropR );                     // set dxaCropRight
        Set_UInt16( pArr, nCropB );                     // set dyaCropBottom
    }

    rStrm.WriteBytes(aArr, nHdrLen);
}

void SwWW8WrGrf::WriteGrfFromGrfNode(SvStream& rStrm, const SwGrfNode &rGrfNd,
    const ww8::Frame &rFly, sal_uInt16 nWidth, sal_uInt16 nHeight)
{
    if (rGrfNd.IsLinkedFile())     // Linked File
    {
        OUString aFileN;
        rGrfNd.GetFileFilterNms( &aFileN, nullptr );

        sal_uInt16 const mm = 94;                    // 94 = BMP, GIF

        WritePICFHeader(rStrm, rFly, mm, nWidth, nHeight,
            rGrfNd.GetpSwAttrSet());
        rStrm.WriteUChar( aFileN.getLength() );    // write Pascal-String
        SwWW8Writer::WriteString8(rStrm, aFileN, false,
            RTL_TEXTENCODING_MS_1252);
    }
    else                                // Embedded File or DDE or something like that
    {
        WritePICFHeader(rStrm, rFly, 0x64, nWidth, nHeight,
            rGrfNd.GetpSwAttrSet());
        SwBasicEscherEx aInlineEscher(&rStrm, m_rWrt);
        aInlineEscher.WriteGrfFlyFrame(rFly.GetFrameFormat(), 0x401);
        aInlineEscher.WritePictures();
    }
}
//For i120928,export graphic info of bullet
void SwWW8WrGrf::WritePICBulletFHeader(SvStream& rStrm, const Graphic &rGrf,
            sal_uInt16 mm, sal_uInt16 nWidth, sal_uInt16 nHeight)
{
    sal_Int16 nXSizeAdd = 0, nYSizeAdd = 0;

    Size aGrTwipSz(rGrf.GetPrefSize());
    sal_uInt16 nHdrLen = 0x44;

    sal_uInt8 aArr[ 0x44 ] = { 0 };

    sal_uInt8* pArr = aArr + 0x2E;  //Do borders first

    static const SvxBoxItemLine aLnArr[4] = { SvxBoxItemLine::TOP, SvxBoxItemLine::LEFT,
        SvxBoxItemLine::BOTTOM, SvxBoxItemLine::RIGHT };
    for(const SvxBoxItemLine & i : aLnArr)
    {
        WW8_BRC aBrc;

        short nSpacing;
        short nThick = aBrc.DetermineBorderProperties(&nSpacing);
        switch (i)
        {
            case SvxBoxItemLine::TOP:
            case SvxBoxItemLine::BOTTOM:
            nHeight -= nThick;
            nHeight = nHeight - nSpacing;
            break;
            case SvxBoxItemLine::LEFT:
            case SvxBoxItemLine::RIGHT:
            default:
            nWidth -= nThick;
            nWidth = nWidth - nSpacing;
            break;
        }
        memcpy( pArr, &aBrc.aBits1, 2);
        pArr+=2;

        memcpy(pArr, &aBrc.aBits2, 2);
        pArr+=2;
    }

    pArr = aArr + 4;                                //skip lcb
    Set_UInt16( pArr, nHdrLen );                    // set cbHeader

    Set_UInt16( pArr, mm );                         // set mm

    if ( (convertTwipToMm100(aGrTwipSz.Width()) > USHRT_MAX ) || ( convertTwipToMm100(aGrTwipSz.Height()) > USHRT_MAX )
        || aGrTwipSz.IsEmpty() )
    {
        aGrTwipSz.setWidth( nWidth );
        aGrTwipSz.setHeight( nHeight );
    }
    using namespace sw::types;
    // set xExt & yExt
    Set_UInt16(pArr, msword_cast<sal_uInt16>(convertTwipToMm100(aGrTwipSz.Width())));
    Set_UInt16(pArr, msword_cast<sal_uInt16>(convertTwipToMm100(aGrTwipSz.Height())));
    pArr += 16;
    // skip hMF & rcWinMF
    // set dxaGoal & dyaGoal
    Set_UInt16(pArr, msword_cast<sal_uInt16>(aGrTwipSz.Width()));
    Set_UInt16(pArr, msword_cast<sal_uInt16>(aGrTwipSz.Height()));

    if( aGrTwipSz.Width() + nXSizeAdd )             // set mx
    {
        double fVal = nWidth * 1000.0 / (aGrTwipSz.Width() + nXSizeAdd);
        Set_UInt16( pArr, o3tl::narrowing<sal_uInt16>(::rtl::math::round(fVal)) );
    }
    else
        pArr += 2;

    if( aGrTwipSz.Height() + nYSizeAdd )            // set my
    {
        double fVal = nHeight * 1000.0 / (aGrTwipSz.Height() + nYSizeAdd);
        Set_UInt16( pArr, o3tl::narrowing<sal_uInt16>(::rtl::math::round(fVal)) );
    }
    else
        pArr += 2;

    Set_UInt16( pArr, 0 );                     // set dxaCropLeft
    Set_UInt16( pArr, 0 );                     // set dyaCropTop
    Set_UInt16( pArr, 0 );                     // set dxaCropRight
    Set_UInt16( pArr, 0 );                     // set dyaCropBottom

    rStrm.WriteBytes(aArr, nHdrLen);
}

void SwWW8WrGrf::WriteGrfForBullet(SvStream& rStrm, const Graphic &rGrf, sal_uInt16 nWidth, sal_uInt16 nHeight)
{
    WritePICBulletFHeader(rStrm,rGrf, 0x64,nWidth,nHeight);
    SwBasicEscherEx aInlineEscher(&rStrm, m_rWrt);
    aInlineEscher.WriteGrfBullet(rGrf);
    aInlineEscher.WritePictures();
}

void SwWW8WrGrf::WriteGraphicNode(SvStream& rStrm, const GraphicDetails &rItem)
{
    sal_uInt16 nWidth = rItem.mnWid;
    sal_uInt16 nHeight = rItem.mnHei;
    sal_uInt64 nPos = rStrm.Tell();         // store start of graphic

    const ww8::Frame &rFly = rItem.maFly;
    switch (rFly.GetWriterType())
    {
        case ww8::Frame::eGraphic:
        {
            const SwNode *pNode = rItem.maFly.GetContent();
            const SwGrfNode *pNd = pNode ? pNode->GetGrfNode() : nullptr;
            OSL_ENSURE(pNd, "Impossible");
            if (pNd)
                WriteGrfFromGrfNode(rStrm, *pNd, rItem.maFly, nWidth, nHeight);
        }
        break;
        //For i120928,add branch to export graphic of bullet
        case ww8::Frame::eBulletGrf:
        {
            if (rItem.maFly.HasGraphic())
            {
                const Graphic& rGrf = rItem.maFly.GetGraphic();
                WriteGrfForBullet(rStrm, rGrf, nWidth, nHeight);
            }
        }
        break;

        case ww8::Frame::eOle:
        {
            const SwNode *pNode = rItem.maFly.GetContent();
            const SwOLENode *pNd = pNode ? pNode->GetOLENode() : nullptr;
            OSL_ENSURE(pNd, "Impossible");
            if (pNd)
            {
#ifdef OLE_PREVIEW_AS_EMF
                //Convert this ole2 preview in ww8+ to an EMF for better unicode
                //support (note that at this moment this breaks StarSymbol
                //using graphics because I need to embed starsymbol in exported
                //documents.
                WritePICFHeader(rStrm, rFly, 0x64, nWidth, nHeight,
                    pNd->GetpSwAttrSet());
                SwBasicEscherEx aInlineEscher(&rStrm, m_rWrt);
                aInlineEscher.WriteOLEFlyFrame(rFly.GetFrameFormat(), 0x401);
                aInlineEscher.WritePictures();
#else
                // cast away const
                SwOLENode *pOleNd = const_cast<SwOLENode*>(pNd);
                SwOLEObj& rSObj= pOleNd->GetOLEObj();

                // TODO/LATER: do we need to load object?
                Graphic* pGr = SdrOle2Obj::GetGraphicFromObject( pOleNd->GetDoc()->GetDocStorage(), rObj );

                //TODO/LATER: do we really want to use GDIMetafile?!
                GDIMetaFile aMtf;
                if ( pGr )
                    aMtf = pGr->GetGDIMetaFile();

                Size aS(aMtf.GetPrefSize());
                aMtf.WindStart();
                aMtf.Play(Application::GetDefaultDevice(), Point(0, 0),
                    Size(2880, 2880));

                WritePICFHeader(rStrm, rFly, 8, nWidth, nHeight,
                    pNd->GetpSwAttrSet());
                WriteWindowMetafileBits(rStrm, aMtf);
                delete pGr;
#endif
            }
        }
        break;
        case ww8::Frame::eDrawing:
        case ww8::Frame::eTextBox:
        case ww8::Frame::eFormControl:
            /*
            #i3958# We only export an empty dummy picture frame here, this is
            what word does the escher export should contain an anchored to
            character element which is drawn over this dummy and the whole
            shebang surrounded with a SHAPE field. This isn't *my* hack :-),
            it's what word does.
            */
            {
                WritePICFHeader(rStrm, rFly, 0x64, nWidth, nHeight);
                SwBasicEscherEx aInlineEscher(&rStrm, m_rWrt);
                aInlineEscher.WriteEmptyFlyFrame(rFly.GetFrameFormat(), 0x401);
            }
            break;
        default:
            OSL_ENSURE(false, "Some inline export not implemented");
            break;
    }

    sal_uInt64 nPos2 = rStrm.Tell();                    // store the end
    rStrm.Seek( nPos );
    rStrm.WriteUInt32(nPos2 - nPos); // patch graphic length in the header
    rStrm.Seek( nPos2 );                            // restore Pos
}

// SwWW8WrGrf::Write() is called after the text.
// It writes out all the graphics and remembers the file locations of the graphics,
// so when writing the attributes of the items it can be patched into PicLocFc-SPRMs.
// The search in the attributes for the Magic sal_uLong and patching
// happens when writing the attributes. Class SwWW8WrGrf provides with
// GetFPos() sequentially the positions
void SwWW8WrGrf::Write()
{
    SvStream& rStrm = *m_rWrt.m_pDataStrm;
    auto aEnd = maDetails.end();
    for (auto aIter = maDetails.begin(); aIter != aEnd; ++aIter)
    {
        sal_uInt64 nPos = rStrm.Tell();                 // align to 4 Bytes
        if( nPos & 0x3 )
            SwWW8Writer::FillCount( rStrm, 4 - ( nPos & 0x3 ) );

        auto aIter2 = std::find(maDetails.begin(), aIter, *aIter);
        if (aIter2 != aIter)
        {
            aIter->mnPos = aIter2->mnPos;
        }
        else
        {
            aIter->mnPos = rStrm.Tell();
            WriteGraphicNode(rStrm, *aIter);
        }
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
