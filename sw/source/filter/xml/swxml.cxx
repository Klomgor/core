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

#include <com/sun/star/embed/XStorage.hpp>
#include <com/sun/star/embed/ElementModes.hpp>
#include <comphelper/processfactory.hxx>
#include <com/sun/star/xml/sax/InputSource.hpp>
#include <com/sun/star/xml/sax/Parser.hpp>
#include <com/sun/star/xml/sax/SAXParseException.hpp>
#include <com/sun/star/text/XTextRange.hpp>
#include <com/sun/star/container/XChild.hpp>
#include <com/sun/star/document/NamedPropertyValues.hpp>
#include <com/sun/star/beans/XPropertySetInfo.hpp>
#include <com/sun/star/beans/NamedValue.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/packages/zip/ZipIOException.hpp>
#include <com/sun/star/packages/WrongPasswordException.hpp>
#include <com/sun/star/ucb/InteractiveAugmentedIOException.hpp>
#include <com/sun/star/task/XStatusIndicator.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <o3tl/any.hxx>
#include <vcl/errinf.hxx>
#include <sfx2/docfile.hxx>
#include <svtools/sfxecode.hxx>
#include <svl/stritem.hxx>
#include <svx/dialmgr.hxx>
#include <svx/strings.hrc>
#include <svx/xmlgrhlp.hxx>
#include <svx/xmleohlp.hxx>
#include <comphelper/fileformat.h>
#include <comphelper/genericpropertyset.hxx>
#include <comphelper/propertysetinfo.hxx>
#include <sal/log.hxx>
#include <sfx2/frame.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <swerror.h>
#include <fltini.hxx>
#include <drawdoc.hxx>
#include <doc.hxx>
#include <docfunc.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentMarkAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <DocumentRedlineManager.hxx>
#include <docary.hxx>
#include <docsh.hxx>
#include <unotextrange.hxx>
#include <SwXMLSectionList.hxx>

#include <SwStyleNameMapper.hxx>
#include <poolfmt.hxx>
#include <numrule.hxx>
#include <paratr.hxx>
#include <fmtrowsplt.hxx>

#include <svx/svdpage.hxx>
#include <svx/svditer.hxx>
#include <svx/svdoole2.hxx>
#include <svx/svdograf.hxx>
#include <sfx2/docfilt.hxx>
#include <sfx2/sfxsids.hrc>
#include <istyleaccess.hxx>
#include <names.hxx>

#include <sfx2/DocumentMetadataAccess.hxx>
#include <comphelper/diagnose_ex.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::text;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::document;
using namespace ::com::sun::star::lang;

static void lcl_EnsureValidPam( SwPaM& rPam )
{
    if( rPam.GetPointContentNode() != nullptr )
    {
        // set proper point content
        if( rPam.GetPointContentNode() != rPam.GetPoint()->GetContentNode() )
        {
            rPam.GetPoint()->nContent.Assign( rPam.GetPointContentNode(), 0 );
        }
        // else: point was already valid

        // if mark is invalid, we delete it
        if( ( rPam.GetMarkContentNode() == nullptr ) ||
            ( rPam.GetMarkContentNode() != rPam.GetMark()->GetContentNode() ) )
        {
            rPam.DeleteMark();
        }
    }
    else
    {
        // point is not valid, so move it into the first content
        rPam.DeleteMark();
        rPam.GetPoint()->Assign(
            *rPam.GetDoc().GetNodes().GetEndOfContent().StartOfSectionNode() );
        rPam.GetPoint()->Adjust(SwNodeOffset(+1));
        rPam.Move( fnMoveForward, GoInContent ); // go into content
    }
}

XMLReader::XMLReader()
{
}

SwReaderType XMLReader::GetReaderType()
{
    return SwReaderType::Storage;
}

namespace
{

/// read a component (file + filter version)
ErrCodeMsg ReadThroughComponent(
    uno::Reference<io::XInputStream> const & xInputStream,
    uno::Reference<XComponent> const & xModelComponent,
    const OUString& rStreamName,
    uno::Reference<uno::XComponentContext> const & rxContext,
    const char* pFilterName,
    const Sequence<Any>& rFilterArguments,
    const OUString& rName,
    bool bMustBeSuccessful,
    bool bEncrypted )
{
    OSL_ENSURE(xInputStream.is(), "input stream missing");
    OSL_ENSURE(xModelComponent.is(), "document missing");
    OSL_ENSURE(rxContext.is(), "factory missing");
    OSL_ENSURE(nullptr != pFilterName,"I need a service name for the component!");

    // prepare ParserInputSource
    xml::sax::InputSource aParserInput;
    aParserInput.sSystemId = rName;
    aParserInput.aInputStream = xInputStream;

    // get filter
    const OUString aFilterName(OUString::createFromAscii(pFilterName));
    uno::Reference< XInterface > xFilter =
        rxContext->getServiceManager()->createInstanceWithArgumentsAndContext(aFilterName, rFilterArguments, rxContext);
    SAL_WARN_IF(!xFilter.is(), "sw.filter", "Can't instantiate filter component: " << aFilterName);
    if( !xFilter.is() )
        return ERR_SWG_READ_ERROR;
    // the underlying SvXMLImport implements XFastParser, XImporter, XFastDocumentHandler
    uno::Reference< xml::sax::XFastParser > xFastParser(xFilter, UNO_QUERY);
    uno::Reference< xml::sax::XDocumentHandler > xDocumentHandler;
    if (!xFastParser)
        xDocumentHandler.set(xFilter, UNO_QUERY);
    if (!xDocumentHandler && !xFastParser)
    {
        SAL_WARN("sd", "service does not implement XFastParser or XDocumentHandler");
        assert(false);
        return ERR_SWG_READ_ERROR;
    }

    // connect model and filter
    uno::Reference < XImporter > xImporter( xFilter, UNO_QUERY );
    xImporter->setTargetDocument( xModelComponent );

    // finally, parse the stream
    try
    {
        if (xFastParser)
            xFastParser->parseStream( aParserInput );
        else
        {
            uno::Reference< xml::sax::XParser > xParser = xml::sax::Parser::create(rxContext);
            // connect parser and filter
            xParser->setDocumentHandler( xDocumentHandler );
            xParser->parseStream( aParserInput );
        }
    }
    catch( xml::sax::SAXParseException& r)
    {
        css::uno::Any ex( cppu::getCaughtException() );
        // sax parser sends wrapped exceptions,
        // try to find the original one
        xml::sax::SAXException aSaxEx = *static_cast<xml::sax::SAXException*>(&r);
        bool bTryChild = true;

        while( bTryChild )
        {
            xml::sax::SAXException aTmp;
            if ( aSaxEx.WrappedException >>= aTmp )
                aSaxEx = std::move(aTmp);
            else
                bTryChild = false;
        }

        packages::zip::ZipIOException aBrokenPackage;
        if ( aSaxEx.WrappedException >>= aBrokenPackage )
            return ERRCODE_IO_BROKENPACKAGE;

        if( bEncrypted )
            return ERRCODE_SFX_WRONGPASSWORD;

        SAL_WARN( "sw", "SAX parse exception caught while importing: " << exceptionToString(ex) );

        const OUString sErr( OUString::number( r.LineNumber )
            + ","
            + OUString::number( r.ColumnNumber ) );

        if( !rStreamName.isEmpty() )
        {
            return ErrCodeMsg(
                            (bMustBeSuccessful ? ERR_FORMAT_FILE_ROWCOL
                                                    : WARN_FORMAT_FILE_ROWCOL),
                            rStreamName, sErr,
                            DialogMask::ButtonsOk | DialogMask::MessageError );
        }
        else
        {
            OSL_ENSURE( bMustBeSuccessful, "Warnings are not supported" );
            return ErrCodeMsg( ERR_FORMAT_ROWCOL, sErr,
                             DialogMask::ButtonsOk | DialogMask::MessageError );
        }
    }
    catch(const xml::sax::SAXException& r)
    {
        css::uno::Any ex( cppu::getCaughtException() );
        packages::zip::ZipIOException aBrokenPackage;
        if ( r.WrappedException >>= aBrokenPackage )
            return ERRCODE_IO_BROKENPACKAGE;

        if( bEncrypted )
            return ERRCODE_SFX_WRONGPASSWORD;

        SAL_WARN( "sw", "uno exception caught while importing: " << exceptionToString(ex) );
        return ERR_SWG_READ_ERROR;
    }
    catch(const packages::zip::ZipIOException&)
    {
        TOOLS_WARN_EXCEPTION( "sw", "uno exception caught while importing" );
        return ERRCODE_IO_BROKENPACKAGE;
    }
    catch(const io::IOException&)
    {
        TOOLS_WARN_EXCEPTION( "sw", "uno exception caught while importing" );
        return ERR_SWG_READ_ERROR;
    }
    catch(const uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION( "sw", "uno exception caught while importing" );
        return ERR_SWG_READ_ERROR;
    }

    // success!
    return ERRCODE_NONE;
}

// read a component (storage version)
ErrCodeMsg ReadThroughComponent(
    uno::Reference<embed::XStorage> const & xStorage,
    uno::Reference<XComponent> const & xModelComponent,
    const char* pStreamName,
    uno::Reference<uno::XComponentContext> const & rxContext,
    const char* pFilterName,
    const Sequence<Any>& rFilterArguments,
    const OUString& rName,
    bool bMustBeSuccessful)
{
    OSL_ENSURE(xStorage.is(), "Need storage!");
    OSL_ENSURE(nullptr != pStreamName, "Please, please, give me a name!");

    // open stream (and set parser input)
    OUString sStreamName = OUString::createFromAscii(pStreamName);
    bool bContainsStream = false;
    try
    {
        bContainsStream = xStorage->isStreamElement(sStreamName);
    }
    catch( container::NoSuchElementException& )
    {
    }

    if (!bContainsStream )
    {
        // stream name not found! return immediately with OK signal
        return ERRCODE_NONE;
    }

    // set Base URL
    uno::Reference< beans::XPropertySet > xInfoSet;
    if( rFilterArguments.hasElements() )
        rFilterArguments.getConstArray()[0] >>= xInfoSet;
    OSL_ENSURE( xInfoSet.is(), "missing property set" );
    if( xInfoSet.is() )
    {
        xInfoSet->setPropertyValue( u"StreamName"_ustr, Any( sStreamName ) );
    }

    try
    {
        // get input stream
        uno::Reference <io::XStream> xStream = xStorage->openStreamElement( sStreamName, embed::ElementModes::READ );
        uno::Reference <beans::XPropertySet > xProps( xStream, uno::UNO_QUERY );

        Any aAny = xProps->getPropertyValue(u"Encrypted"_ustr);

        std::optional<const bool> b = o3tl::tryAccess<bool>(aAny);
        bool bEncrypted = b.has_value() && *b;

        uno::Reference <io::XInputStream> xInputStream = xStream->getInputStream();

        // read from the stream
        return ReadThroughComponent(
            xInputStream, xModelComponent, sStreamName, rxContext,
            pFilterName, rFilterArguments,
            rName, bMustBeSuccessful, bEncrypted );
    }
    catch ( packages::WrongPasswordException& )
    {
        return ERRCODE_SFX_WRONGPASSWORD;
    }
    catch( packages::zip::ZipIOException& )
    {
        return ERRCODE_IO_BROKENPACKAGE;
    }
    catch ( uno::Exception& )
    {
        OSL_FAIL( "Error on import" );
        // TODO/LATER: error handling
    }

    return ERR_SWG_READ_ERROR;
}

}

// #i44177#
static void lcl_AdjustOutlineStylesForOOo(SwDoc& _rDoc)
{
    // array containing the names of the default outline styles ('Heading 1',
    // 'Heading 2', ..., 'Heading 10')
    ProgName aDefOutlStyleNames[ MAXLEVEL ];
    {
        for ( sal_uInt8 i = 0; i < MAXLEVEL; ++i )
        {
            aDefOutlStyleNames[i] =
                SwStyleNameMapper::GetProgName( RES_POOLCOLL_HEADLINE1 + i,
                                                UIName() );
        }
    }

    // array indicating, which outline level already has a style assigned.
    bool aOutlineLevelAssigned[ MAXLEVEL ];
    // array of the default outline styles, which are created for the document.
    SwTextFormatColl* aCreatedDefaultOutlineStyles[ MAXLEVEL ];

    {
        for ( sal_uInt8 i = 0; i < MAXLEVEL; ++i )
        {
            aOutlineLevelAssigned[ i ] = false;
            aCreatedDefaultOutlineStyles[ i ] = nullptr;
        }
    }

    // determine, which outline level has already a style assigned and
    // which of the default outline styles is created.
    const SwTextFormatColls& rColls = *(_rDoc.GetTextFormatColls());
    for ( size_t n = 1; n < rColls.size(); ++n )
    {
        SwTextFormatColl* pColl = rColls[ n ];
        if ( pColl->IsAssignedToListLevelOfOutlineStyle() )
        {
            aOutlineLevelAssigned[ pColl->GetAssignedOutlineStyleLevel() ] = true;
        }

        for ( sal_uInt8 i = 0; i < MAXLEVEL; ++i )
        {
            // FIXME: comparing ProgName to UIName ?
            if ( aCreatedDefaultOutlineStyles[ i ] == nullptr &&
                 pColl->GetName().toString() == aDefOutlStyleNames[i].toString() )
            {
                aCreatedDefaultOutlineStyles[ i ] = pColl;
                break;
            }
        }
    }

    // assign already created default outline style to outline level, which
    // doesn't have a style assigned to it.
    const SwNumRule* pOutlineRule = _rDoc.GetOutlineNumRule();
    for ( sal_uInt8 i = 0; i < MAXLEVEL; ++i )
    {
        // #i73361#
        // Do not change assignment of already created default outline style
        // to a certain outline level.
        if ( !aOutlineLevelAssigned[ i ] &&
             aCreatedDefaultOutlineStyles[ i ] != nullptr &&
             ! aCreatedDefaultOutlineStyles[ i ]->IsAssignedToListLevelOfOutlineStyle() )
        {
            // apply outline level at created default outline style
            aCreatedDefaultOutlineStyles[ i ]->AssignToListLevelOfOutlineStyle(i);

            // apply outline numbering rule, if none is set.
            const SwNumRuleItem& rItem =
                aCreatedDefaultOutlineStyles[ i ]->GetFormatAttr( RES_PARATR_NUMRULE, false );
            if ( rItem.GetValue().isEmpty() )
            {
                SwNumRuleItem aItem( pOutlineRule->GetName() );
                aCreatedDefaultOutlineStyles[ i ]->SetFormatAttr( aItem );
            }
        }
    }
}

static void lcl_ConvertSdrOle2ObjsToSdrGrafObjs(SwDoc& _rDoc)
{
    if ( !(_rDoc.getIDocumentDrawModelAccess().GetDrawModel() &&
         _rDoc.getIDocumentDrawModelAccess().GetDrawModel()->GetPage( 0 )) )
        return;

    const SdrPage& rSdrPage( *(_rDoc.getIDocumentDrawModelAccess().GetDrawModel()->GetPage( 0 )) );

    // iterate recursive with group objects over all shapes on the draw page
    SdrObjListIter aIter( &rSdrPage );
    while( aIter.IsMore() )
    {
        SdrOle2Obj* pOle2Obj = dynamic_cast< SdrOle2Obj* >( aIter.Next() );
        if( pOle2Obj )
        {
            // found an ole2 shape
            SdrObjList* pObjList = pOle2Obj->getParentSdrObjListFromSdrObject();

            // get its graphic
            Graphic aGraphic;
            pOle2Obj->Connect();
            const Graphic* pGraphic = pOle2Obj->GetGraphic();
            if( pGraphic )
                aGraphic = *pGraphic;
            pOle2Obj->Disconnect();

            // create new graphic shape with the ole graphic and shape size
            rtl::Reference<SdrGrafObj> pGraphicObj = new SdrGrafObj(
                pOle2Obj->getSdrModelFromSdrObject(),
                aGraphic,
                pOle2Obj->GetCurrentBoundRect());

            // apply layer of ole2 shape at graphic shape
            pGraphicObj->SetLayer( pOle2Obj->GetLayer() );

            // replace ole2 shape with the new graphic object and delete the ol2 shape
            pObjList->ReplaceObject( pGraphicObj.get(), pOle2Obj->GetOrdNum() );
        }
    }
}

ErrCodeMsg XMLReader::Read( SwDoc &rDoc, const OUString& rBaseURL, SwPaM &rPaM, const OUString & rName )
{
    // needed for relative URLs, but in clipboard copy/paste there may be none
    // and also there is the SwXMLTextBlocks special case
    SAL_INFO_IF(rBaseURL.isEmpty(), "sw.filter", "sw::XMLReader: no base URL");

    // Get service factory
    const uno::Reference< uno::XComponentContext >& xContext =
            comphelper::getProcessComponentContext();

    uno::Reference<document::XGraphicStorageHandler> xGraphicStorageHandler;
    rtl::Reference<SvXMLGraphicHelper> xGraphicHelper;
    uno::Reference< document::XEmbeddedObjectResolver > xObjectResolver;
    rtl::Reference<SvXMLEmbeddedObjectHelper> xObjectHelper;

    // get the input stream (storage or stream)
    uno::Reference<embed::XStorage> xStorage;
    if( m_pMedium )
        xStorage = m_pMedium->GetStorage();
    else
        xStorage = m_xStorage;

    if( !xStorage.is() )
        return ERR_SWG_READ_ERROR;

    xGraphicHelper = SvXMLGraphicHelper::Create( xStorage,
                                                 SvXMLGraphicHelperMode::Read );
    xGraphicStorageHandler = xGraphicHelper.get();
    SfxObjectShell *pPersist = rDoc.GetPersist();
    if( pPersist )
    {
        xObjectHelper = SvXMLEmbeddedObjectHelper::Create(
                                        xStorage, *pPersist,
                                        SvXMLEmbeddedObjectHelperMode::Read );
        xObjectResolver = xObjectHelper.get();
    }

    // Get the docshell, the model, and finally the model's component
    SwDocShell *pDocSh = rDoc.GetDocShell();
    OSL_ENSURE( pDocSh, "XMLReader::Read: got no doc shell" );
    if( !pDocSh )
        return ERR_SWG_READ_ERROR;
    uno::Reference< lang::XComponent > xModelComp = pDocSh->GetModel();
    OSL_ENSURE( xModelComp.is(),
            "XMLReader::Read: got no model" );
    if( !xModelComp.is() )
        return ERR_SWG_READ_ERROR;

    // create and prepare the XPropertySet that gets passed through
    // the components, and the XStatusIndicator that shows progress to
    // the user.

    // create XPropertySet with three properties for status indicator
    static comphelper::PropertyMapEntry const aInfoMap[] =
    {
        { u"ProgressRange"_ustr, 0,
              ::cppu::UnoType<sal_Int32>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0},
        { u"ProgressMax"_ustr, 0,
              ::cppu::UnoType<sal_Int32>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0},
        { u"ProgressCurrent"_ustr, 0,
              ::cppu::UnoType<sal_Int32>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0},
        { u"NumberStyles"_ustr, 0,
              cppu::UnoType<container::XNameContainer>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0},
        { u"RecordChanges"_ustr, 0,
              cppu::UnoType<bool>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"ShowChanges"_ustr, 0,
              cppu::UnoType<bool>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"RedlineProtectionKey"_ustr, 0,
              cppu::UnoType<Sequence<sal_Int8>>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"PrivateData"_ustr, 0,
              cppu::UnoType<XInterface>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"BaseURI"_ustr, 0,
              ::cppu::UnoType<OUString>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"StreamRelPath"_ustr, 0,
              ::cppu::UnoType<OUString>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"StreamName"_ustr, 0,
              ::cppu::UnoType<OUString>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        // properties for insert modes
        { u"StyleInsertModeFamilies"_ustr, 0,
              cppu::UnoType<Sequence<OUString>>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"StyleInsertModeOverwrite"_ustr, 0,
              cppu::UnoType<bool>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"TextInsertModeRange"_ustr, 0,
              cppu::UnoType<text::XTextRange>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0},
        { u"AutoTextMode"_ustr, 0,
              cppu::UnoType<bool>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"OrganizerMode"_ustr, 0,
              cppu::UnoType<bool>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },

        // #i28749# - Add property, which indicates, if the
        // shape position attributes are given in horizontal left-to-right layout.
        // This is the case for the OpenOffice.org file format.
        { u"ShapePositionInHoriL2R"_ustr, 0,
              cppu::UnoType<bool>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },

        { u"BuildId"_ustr, 0,
              ::cppu::UnoType<OUString>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },

        // Add property, which indicates, if a text document in OpenOffice.org
        // file format is read.
        // Note: Text documents read via the binary filter are also finally
        //       read using the OpenOffice.org file format. Thus, e.g. for text
        //       documents in StarOffice 5.2 binary file format this property
        //       will be true.
        { u"TextDocInOOoFileFormat"_ustr, 0,
              cppu::UnoType<bool>::get(),
              beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"SourceStorage"_ustr, 0, cppu::UnoType<embed::XStorage>::get(),
          css::beans::PropertyAttribute::MAYBEVOID, 0 },
        { u"IsInPaste"_ustr, 0, cppu::UnoType<bool>::get(),
          beans::PropertyAttribute::MAYBEVOID, 0 },
    };
    uno::Reference< beans::XPropertySet > xInfoSet(
                comphelper::GenericPropertySet_CreateInstance(
                            new comphelper::PropertySetInfo( aInfoMap ) ) );

    // get BuildId from parent container if available
    uno::Reference< container::XChild > xChild( xModelComp, uno::UNO_QUERY );
    if( xChild.is() )
    {
        uno::Reference< beans::XPropertySet > xParentSet( xChild->getParent(), uno::UNO_QUERY );
        if( xParentSet.is() )
        {
            uno::Reference< beans::XPropertySetInfo > xPropSetInfo( xParentSet->getPropertySetInfo() );
            static constexpr OUString sPropName(u"BuildId"_ustr );
            if( xPropSetInfo.is() && xPropSetInfo->hasPropertyByName(sPropName) )
            {
                xInfoSet->setPropertyValue( sPropName, xParentSet->getPropertyValue(sPropName) );
            }
        }
    }

    // try to get an XStatusIndicator from the Medium
    uno::Reference<task::XStatusIndicator> xStatusIndicator;

    if (pDocSh->GetMedium())
    {
        const SfxUnoAnyItem* pItem =
            pDocSh->GetMedium()->GetItemSet().GetItem(SID_PROGRESS_STATUSBAR_CONTROL);
        if (pItem)
        {
            pItem->GetValue() >>= xStatusIndicator;
        }
    }

    // set progress range and start status indicator
    sal_Int32 nProgressRange(1000000);
    if (xStatusIndicator.is())
    {
        xStatusIndicator->start(SvxResId(RID_SVXSTR_DOC_LOAD), nProgressRange);
    }
    uno::Any aProgRange;
    aProgRange <<= nProgressRange;
    xInfoSet->setPropertyValue(u"ProgressRange"_ustr, aProgRange);

    Reference< container::XNameAccess > xLateInitSettings( document::NamedPropertyValues::create(xContext), UNO_QUERY_THROW );
    beans::NamedValue aLateInitSettings( u"LateInitSettings"_ustr, Any( xLateInitSettings ) );

    xInfoSet->setPropertyValue( u"SourceStorage"_ustr, Any( xStorage ) );

    xInfoSet->setPropertyValue(u"IsInPaste"_ustr, Any(IsInPaste()));

    // prepare filter arguments, WARNING: the order is important!
    Sequence<Any> aFilterArgs{  Any(xInfoSet),
                                Any(xStatusIndicator),
                                Any(xGraphicStorageHandler),
                                Any(xObjectResolver),
                                Any(aLateInitSettings) };

    Sequence<Any> aEmptyArgs{   Any(xInfoSet),
                                Any(xStatusIndicator) };

    // prepare for special modes
    if( m_aOption.IsFormatsOnly() )
    {
        sal_Int32 nCount =
            (m_aOption.IsFrameFormats() ? 1 : 0) +
            (m_aOption.IsPageDescs() ? 1 : 0) +
            (m_aOption.IsTextFormats() ? 2 : 0) +
            (m_aOption.IsNumRules() ? 1 : 0);

        Sequence< OUString> aFamiliesSeq( nCount );
        OUString *pSeq = aFamiliesSeq.getArray();
        if( m_aOption.IsFrameFormats() )
            // SfxStyleFamily::Frame;
            *pSeq++ = "FrameStyles";
        if( m_aOption.IsPageDescs() )
            // SfxStyleFamily::Page;
            *pSeq++ = "PageStyles";
        if( m_aOption.IsTextFormats() )
        {
            // (SfxStyleFamily::Char|SfxStyleFamily::Para);
            *pSeq++ = "CharacterStyles";
            *pSeq++ = "ParagraphStyles";
        }
        if( m_aOption.IsNumRules() )
            // SfxStyleFamily::Pseudo;
            *pSeq++ = "NumberingStyles";

        xInfoSet->setPropertyValue( u"StyleInsertModeFamilies"_ustr,
                                    Any(aFamiliesSeq) );

        xInfoSet->setPropertyValue( u"StyleInsertModeOverwrite"_ustr, Any(!m_aOption.IsMerge()) );
    }
    else if( m_bInsertMode )
    {
        const rtl::Reference<SwXTextRange> xInsertTextRange =
            SwXTextRange::CreateXTextRange(rDoc, *rPaM.GetPoint(), nullptr);
        xInfoSet->setPropertyValue( u"TextInsertModeRange"_ustr,
                                    Any(uno::Reference<text::XTextRange>(xInsertTextRange)) );
    }
    else
    {
        rPaM.GetBound().nContent.Assign(nullptr, 0);
        rPaM.GetBound(false).nContent.Assign(nullptr, 0);
    }

    if( IsBlockMode() )
    {
        xInfoSet->setPropertyValue( u"AutoTextMode"_ustr, Any(true) );
    }
    if( IsOrganizerMode() )
    {
        xInfoSet->setPropertyValue( u"OrganizerMode"_ustr, Any(true) );
    }

    // Set base URI
    // there is ambiguity which medium should be used here
    // for now the own medium has a preference
    SfxMedium* pMedDescrMedium = m_pMedium ? m_pMedium : pDocSh->GetMedium();
    OSL_ENSURE( pMedDescrMedium, "There is no medium to get MediaDescriptor from!" );

    xInfoSet->setPropertyValue( u"BaseURI"_ustr, Any( rBaseURL ) );

    // TODO/LATER: separate links from usual embedded objects
    OUString StreamPath;
    if( SfxObjectCreateMode::EMBEDDED == rDoc.GetDocShell()->GetCreateMode() )
    {
        if (pMedDescrMedium)
        {
            const SfxStringItem* pDocHierarchItem =
                pMedDescrMedium->GetItemSet().GetItem(SID_DOC_HIERARCHICALNAME);
            if ( pDocHierarchItem )
                StreamPath = pDocHierarchItem->GetValue();
        }
        else
        {
            StreamPath = "dummyObjectName";
        }

        if( !StreamPath.isEmpty() )
        {
            xInfoSet->setPropertyValue( u"StreamRelPath"_ustr, Any( StreamPath ) );
        }
    }

    rtl::Reference<SwDoc> aHoldRef(&rDoc); // prevent deletion
    ErrCodeMsg nRet = ERRCODE_NONE;

    // save redline mode into import info property set
    static constexpr OUString sShowChanges(u"ShowChanges"_ustr);
    static constexpr OUString sRecordChanges(u"RecordChanges"_ustr);
    static constexpr OUString sRedlineProtectionKey(u"RedlineProtectionKey"_ustr);
    xInfoSet->setPropertyValue( sShowChanges,
        Any(IDocumentRedlineAccess::IsShowChanges(rDoc.getIDocumentRedlineAccess().GetRedlineFlags())) );
    xInfoSet->setPropertyValue( sRecordChanges,
        Any(IDocumentRedlineAccess::IsRedlineOn(rDoc.getIDocumentRedlineAccess().GetRedlineFlags())) );
    xInfoSet->setPropertyValue( sRedlineProtectionKey,
        Any(rDoc.getIDocumentRedlineAccess().GetRedlinePassword()) );

    // force redline mode to "none"
    rDoc.getIDocumentRedlineAccess().SetRedlineFlags_intern( RedlineFlags::NONE );

    const bool bOASIS = ( SotStorage::GetVersion( xStorage ) > SOFFICE_FILEFORMAT_60 );
    // #i28749# - set property <ShapePositionInHoriL2R>
    {
        const bool bShapePositionInHoriL2R = !bOASIS;
        xInfoSet->setPropertyValue(
                u"ShapePositionInHoriL2R"_ustr,
                Any( bShapePositionInHoriL2R ) );
    }
    {
        const bool bTextDocInOOoFileFormat = !bOASIS;
        xInfoSet->setPropertyValue(
                u"TextDocInOOoFileFormat"_ustr,
                Any( bTextDocInOOoFileFormat ) );
    }

    ErrCode nWarnRDF = ERRCODE_NONE;
    if ( !(IsOrganizerMode() || IsBlockMode() || m_aOption.IsFormatsOnly() ||
           m_bInsertMode) )
    {
        // RDF metadata - must be read before styles/content
        // N.B.: embedded documents have their own manifest.rdf!
        try
        {
            const uno::Reference<rdf::XDocumentMetadataAccess> xDMA(xModelComp,
                uno::UNO_QUERY_THROW);
            const uno::Reference<frame::XModel> xModel(xModelComp,
                uno::UNO_QUERY_THROW);
            const uno::Reference<rdf::XURI> xBaseURI( ::sfx2::createBaseURI(
                xContext, xModel, rBaseURL, StreamPath) );
            const uno::Reference<task::XInteractionHandler> xHandler(
                pDocSh->GetMedium()->GetInteractionHandler() );
            xDMA->loadMetadataFromStorage(xStorage, xBaseURI, xHandler);
        }
        catch (const lang::WrappedTargetException & e)
        {
            ucb::InteractiveAugmentedIOException iaioe;
            if (e.TargetException >>= iaioe)
            {
                // import error that was not ignored by InteractionHandler!
                nWarnRDF = ERR_SWG_READ_ERROR;
            }
            else
            {
                nWarnRDF = WARN_SWG_FEATURES_LOST; // uhh... something wrong?
            }
        }
        catch (uno::Exception &)
        {
            nWarnRDF = WARN_SWG_FEATURES_LOST; // uhh... something went wrong?
        }
    }

    // read storage streams

    // #i103539#: always read meta.xml for generator
    ErrCodeMsg const nWarn = ReadThroughComponent(
        xStorage, xModelComp, "meta.xml", xContext,
        (bOASIS ? "com.sun.star.comp.Writer.XMLOasisMetaImporter"
                : "com.sun.star.comp.Writer.XMLMetaImporter"),
        aEmptyArgs, rName, false );

    ErrCodeMsg nWarn2 = ERRCODE_NONE;
    if( !(IsOrganizerMode() || IsBlockMode() || m_aOption.IsFormatsOnly() ||
          m_bInsertMode) )
    {
        nWarn2 = ReadThroughComponent(
            xStorage, xModelComp, "settings.xml", xContext,
            (bOASIS ? "com.sun.star.comp.Writer.XMLOasisSettingsImporter"
                    : "com.sun.star.comp.Writer.XMLSettingsImporter"),
            aFilterArgs, rName, false );
    }

    nRet = ReadThroughComponent(
        xStorage, xModelComp, "styles.xml", xContext,
        (bOASIS ? "com.sun.star.comp.Writer.XMLOasisStylesImporter"
                : "com.sun.star.comp.Writer.XMLStylesImporter"),
        aFilterArgs, rName, true );

    if( !nRet && !(IsOrganizerMode() || m_aOption.IsFormatsOnly()) )
        nRet = ReadThroughComponent(
           xStorage, xModelComp, "content.xml", xContext,
            (bOASIS ? "com.sun.star.comp.Writer.XMLOasisContentImporter"
                    : "com.sun.star.comp.Writer.XMLContentImporter"),
           aFilterArgs, rName, true );

    if( !IsOrganizerMode() && !IsBlockMode() && !m_bInsertMode &&
          !m_aOption.IsFormatsOnly() &&
            // sw_redlinehide: disable layout cache for now
          *o3tl::doAccess<bool>(xInfoSet->getPropertyValue(sShowChanges)) &&
            // sw_fieldmarkhide: also disable if there is a fieldmark
          rDoc.getIDocumentMarkAccess()->getFieldmarksCount() == 0)
    {
        try
        {
            uno::Reference < io::XStream > xStm = xStorage->openStreamElement( u"layout-cache"_ustr, embed::ElementModes::READ );
            std::unique_ptr<SvStream> pStrm2 = utl::UcbStreamHelper::CreateStream( xStm );
            if( !pStrm2->GetError() )
                rDoc.ReadLayoutCache( *pStrm2 );
        }
        catch (const uno::Exception&)
        {
        }
    }

    // Notify math objects
    if( m_bInsertMode )
        rDoc.PrtOLENotify( false );
    else if ( rDoc.IsOLEPrtNotifyPending() )
        rDoc.PrtOLENotify( true );

    if (!nRet)
    {
        if (nWarn)
            nRet = nWarn;
        else if (nWarn2)
            nRet = std::move(nWarn2);
        else
            nRet = nWarnRDF;
    }

    ::svx::DropUnusedNamedItems(xModelComp);

    m_aOption.ResetAllFormatsOnly();

    // redline password
    Any aAny = xInfoSet->getPropertyValue( sRedlineProtectionKey );
    Sequence<sal_Int8> aKey;
    aAny >>= aKey;
    rDoc.getIDocumentRedlineAccess().SetRedlinePassword( aKey );

    // restore redline mode from import info property set
    RedlineFlags nRedlineFlags = RedlineFlags::ShowInsert;
    aAny = xInfoSet->getPropertyValue( sShowChanges );
    if ( *o3tl::doAccess<bool>(aAny) )
        nRedlineFlags |= RedlineFlags::ShowDelete;
    aAny = xInfoSet->getPropertyValue( sRecordChanges );
    if ( *o3tl::doAccess<bool>(aAny) || aKey.hasElements() )
        nRedlineFlags |= RedlineFlags::On;

    // ... restore redline mode
    // (First set bogus mode to make sure the mode in getIDocumentRedlineAccess().SetRedlineFlags()
    //  is different from its previous mode.)
    rDoc.getIDocumentRedlineAccess().SetRedlineFlags_intern( ~nRedlineFlags );
    // must set flags to show delete so that CompressRedlines works
    rDoc.getIDocumentRedlineAccess().SetRedlineFlags(nRedlineFlags|RedlineFlags::ShowDelete);
    // tdf#83260 ensure that the first call of CompressRedlines after loading
    // the document is a no-op by calling it now
    rDoc.getIDocumentRedlineAccess().CompressRedlines();
    // can't set it on the layout or view shell because it doesn't exist yet
    rDoc.GetDocumentRedlineManager().SetHideRedlines(!(nRedlineFlags & RedlineFlags::ShowDelete));

    lcl_EnsureValidPam( rPaM ); // move Pam into valid content

    if( xGraphicHelper )
        xGraphicHelper->dispose();
    xGraphicHelper.clear();
    xGraphicStorageHandler = nullptr;
    if( xObjectHelper )
        xObjectHelper->dispose();
    xObjectHelper.clear();
    xObjectResolver = nullptr;
    aHoldRef.clear();

    if ( !bOASIS )
    {
        // #i44177# - assure that for documents in OpenOffice.org
        // file format the relation between outline numbering rule and styles is
        // filled-up accordingly.
        // Note: The OpenOffice.org file format, which has no content that applies
        //       a certain style, which is related to the outline numbering rule,
        //       has lost the information, that this certain style is related to
        //       the outline numbering rule.
        // #i70748# - only for templates
        if ( m_pMedium && m_pMedium->GetFilter() &&
             m_pMedium->GetFilter()->IsOwnTemplateFormat() )
        {
            lcl_AdjustOutlineStylesForOOo( rDoc );
        }
        // Fix #i58251#: Unfortunately is the static default different to SO7 behaviour,
        // so we have to set a dynamic default after importing SO7
        rDoc.SetDefault(SwFormatRowSplit(false));
    }

    rDoc.PropagateOutlineRule();

    // #i62875#
    if ( rDoc.getIDocumentSettingAccess().get(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE) && !docfunc::ExistsDrawObjs( rDoc ) )
    {
        rDoc.getIDocumentSettingAccess().set(DocumentSettingId::DO_NOT_CAPTURE_DRAW_OBJS_ON_PAGE, false);
    }

    // Convert all instances of <SdrOle2Obj> into <SdrGrafObj>, because the
    // Writer doesn't support such objects.
    lcl_ConvertSdrOle2ObjsToSdrGrafObjs( rDoc );

    // set BuildId on XModel for later OLE object loading
    if( xInfoSet.is() )
    {
        uno::Reference< beans::XPropertySet > xModelSet( xModelComp, uno::UNO_QUERY );
        if( xModelSet.is() )
        {
            uno::Reference< beans::XPropertySetInfo > xModelSetInfo( xModelSet->getPropertySetInfo() );
            static constexpr OUString sName(u"BuildId"_ustr );
            if( xModelSetInfo.is() && xModelSetInfo->hasPropertyByName(sName) )
            {
                xModelSet->setPropertyValue( sName, xInfoSet->getPropertyValue(sName) );
            }
        }
    }

    // tdf#115815 restore annotation ranges stored in temporary bookmarks
    rDoc.getIDocumentMarkAccess()->restoreAnnotationMarks();

    if (xStatusIndicator.is())
    {
        xStatusIndicator->end();
    }

    rDoc.GetIStyleAccess().clearCaches(); // Clear Automatic-Style-Caches(shared_pointer!)
    return nRet;
}

    // read the sections of the document, which is equal to the medium.
    // returns the count of it
size_t XMLReader::GetSectionList( SfxMedium& rMedium,
                                  std::vector<OUString>& rStrings) const
{
    const uno::Reference< uno::XComponentContext >& xContext =
            comphelper::getProcessComponentContext();
    uno::Reference < embed::XStorage > xStg2;
    if( ( xStg2 = rMedium.GetStorage() ).is() )
    {
        try
        {
            xml::sax::InputSource aParserInput;
            static constexpr OUString sDocName( u"content.xml"_ustr );
            aParserInput.sSystemId = sDocName;

            uno::Reference < io::XStream > xStm = xStg2->openStreamElement( sDocName, embed::ElementModes::READ );
            aParserInput.aInputStream = xStm->getInputStream();

            // get filter
            rtl::Reference< SwXMLSectionList > xImport = new SwXMLSectionList( xContext, rStrings );

            // parse
            xImport->parseStream( aParserInput );
        }
        catch( xml::sax::SAXParseException&  )
        {
            TOOLS_WARN_EXCEPTION("sw", "");
            // re throw ?
        }
        catch( xml::sax::SAXException&  )
        {
            TOOLS_WARN_EXCEPTION("sw", "");
            // re throw ?
        }
        catch( io::IOException& )
        {
            TOOLS_WARN_EXCEPTION("sw", "");
            // re throw ?
        }
        catch( packages::WrongPasswordException& )
        {
            // re throw ?
        }
    }
    return rStrings.size();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
