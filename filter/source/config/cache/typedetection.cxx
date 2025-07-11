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

#include "typedetection.hxx"
#include "constant.hxx"

#include <com/sun/star/document/XExtendedFilterDetection.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <com/sun/star/util/XURLTransformer.hpp>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/embed/StorageFormats.hpp>
#include <com/sun/star/io/XInputStream.hpp>
#include <com/sun/star/io/XSeekable.hpp>
#include <com/sun/star/packages/zip/ZipIOException.hpp>
#include <com/sun/star/task/XInteractionHandler.hpp>

#include <sfx2/brokenpackageint.hxx>
#include <o3tl/string_view.hxx>
#include <tools/wldcrd.hxx>
#include <sal/log.hxx>
#include <framework/interaction.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <tools/urlobj.hxx>
#include <comphelper/fileurl.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/scopeguard.hxx>
#include <utility>

#define DEBUG_TYPE_DETECTION 0

#if DEBUG_TYPE_DETECTION
#include <iostream>
using std::cout;
using std::endl;
#endif

using namespace com::sun::star;

namespace filter::config{

TypeDetection::TypeDetection(const css::uno::Reference< css::uno::XComponentContext >& rxContext)
   : m_xContext(rxContext)
   , m_xTerminateListener(new TerminateDetection(this))
   , m_bCancel(false)
{
    css::frame::Desktop::create(m_xContext)->addTerminateListener(m_xTerminateListener);
    BaseContainer::init(u"com.sun.star.comp.filter.config.TypeDetection"_ustr   ,
                        { u"com.sun.star.document.TypeDetection"_ustr },
                        FilterCache::E_TYPE                           );
}


TypeDetection::~TypeDetection()
{
    css::frame::Desktop::create(m_xContext)->removeTerminateListener(m_xTerminateListener);
}


OUString SAL_CALL TypeDetection::queryTypeByURL(const OUString& sURL)
{
    OUString sType;

    // SAFE ->
    std::unique_lock aLock(m_aMutex);

    css::util::URL  aURL;
    aURL.Complete = sURL;
    css::uno::Reference< css::util::XURLTransformer > xParser( css::util::URLTransformer::create(m_xContext) );
    xParser->parseStrict(aURL);

    // set std types as minimum requirement first!
    // Only in case no type was found for given URL,
    // use optional types too ...
    auto & cache = GetTheFilterCache();
    FlatDetection lFlatTypes;
    cache.detectFlatForURL(aURL, lFlatTypes);

    if (
        (lFlatTypes.empty()                                ) &&
        (!cache.isFillState(FilterCache::E_CONTAINS_TYPES))
       )
    {
        cache.load(FilterCache::E_CONTAINS_TYPES);
        cache.detectFlatForURL(aURL, lFlatTypes);
    }

    // first item is guaranteed as "preferred" one!
    if (!lFlatTypes.empty())
    {
        const FlatDetectionInfo& aMatch = *(lFlatTypes.begin());
        sType = aMatch.sType;
    }

    return sType;
    // <- SAFE
}

namespace {

/**
 * Rank format types in order of complexity.  More complex formats are
 * ranked higher so that they get tested sooner over simpler formats.
 *
 * Guidelines to determine how complex a format is (subject to change):
 *
 * 1) compressed text (XML, HTML, etc)
 * 2) binary
 * 3) non-compressed text
 *   3.1) structured text
 *     3.1.1) dialect of a structured text (e.g. docbook XML)
 *     3.1.2) generic structured text (e.g. generic XML)
 *   3.2) non-structured text
 *
 * In each category, rank them from strictly-structured to
 * loosely-structured.
 */
int getFlatTypeRank(std::u16string_view rType)
{
    // List formats from more complex to less complex.
    // TODO: Add more.
    static const char* const ranks[] = {

        // Compressed XML (ODF XML zip formats)
        "writer8_template",
        "writer8",
        "calc8_template",
        "calc8",
        "impress8_template",
        "impress8",
        "draw8_template",
        "draw8",
        "chart8",
        "math8",
        "writerglobal8_template",
        "writerglobal8",
        "writerweb8_writer_template",
        "StarBase",

        // Compressed XML (OOXML)
        "writer_OOXML_Text_Template",
        "writer_OOXML",
        "writer_MS_Word_2007_Template",
        "writer_MS_Word_2007",
        "Office Open XML Spreadsheet Template",
        "Office Open XML Spreadsheet",
        "MS Excel 2007 XML Template",
        "MS Excel 2007 XML",
        "MS PowerPoint 2007 XML Template",
        "MS PowerPoint 2007 XML AutoPlay",
        "MS PowerPoint 2007 XML",

        // Compressed XML (Uniform/Unified Office Format)
        "Unified_Office_Format_text",
        "Unified_Office_Format_spreadsheet",
        "Unified_Office_Format_presentation",

        // Compressed XML (StarOffice XML zip formats)
        "calc_StarOffice_XML_Calc",
        "calc_StarOffice_XML_Calc_Template",
        "chart_StarOffice_XML_Chart",
        "draw_StarOffice_XML_Draw",
        "draw_StarOffice_XML_Draw_Template",
        "impress_StarOffice_XML_Impress",
        "impress_StarOffice_XML_Impress_Template",
        "math_StarOffice_XML_Math",
        "writer_StarOffice_XML_Writer",
        "writer_StarOffice_XML_Writer_Template",
        "writer_globaldocument_StarOffice_XML_Writer_GlobalDocument",
        "writer_web_StarOffice_XML_Writer_Web_Template",

        // Compressed text
        "pdf_Portable_Document_Format",

        // Binary
        "writer_T602_Document",
        "writer_WordPerfect_Document",
        "writer_MS_Works_Document",
        "writer_MS_Word_97_Vorlage",
        "writer_MS_Word_97",
        "writer_MS_Word_95_Vorlage",
        "writer_MS_Word_95",
        "writer_MS_WinWord_60",
        "writer_MS_WinWord_5",
        "MS Excel 2007 Binary",
        "calc_MS_Excel_97_VorlageTemplate",
        "calc_MS_Excel_97",
        "calc_MS_Excel_95_VorlageTemplate",
        "calc_MS_Excel_95",
        "calc_MS_Excel_5095_VorlageTemplate",
        "calc_MS_Excel_5095",
        "calc_MS_Excel_40_VorlageTemplate",
        "calc_MS_Excel_40",
        "calc_Pocket_Excel_File",
        "impress_MS_PowerPoint_97_Vorlage",
        "impress_MS_PowerPoint_97_AutoPlay",
        "impress_MS_PowerPoint_97",
        "calc_Lotus",
        "calc_QPro",
        "calc_SYLK",
        "calc_DIF",
        "calc_dBase",
        "Apache Parquet",

        // Binary (raster and vector image files)
        "emf_MS_Windows_Metafile",
        "wmf_MS_Windows_Metafile",
        "met_OS2_Metafile",
        "svm_StarView_Metafile",
        "sgv_StarDraw_20",
        "tif_Tag_Image_File",
        "tga_Truevision_TARGA",
        "sgf_StarOffice_Writer_SGF",
        "ras_Sun_Rasterfile",
        "psd_Adobe_Photoshop",
        "png_Portable_Network_Graphic",
        "jpg_JPEG",
        "mov_MOV",
        "gif_Graphics_Interchange",
        "bmp_MS_Windows",
        "pcx_Zsoft_Paintbrush",
        "pct_Mac_Pict",
        "pcd_Photo_CD_Base",
        "pcd_Photo_CD_Base4",
        "pcd_Photo_CD_Base16",
        "webp_WebP",
        "impress_CGM_Computer_Graphics_Metafile", // There is binary and ascii variants ?
        "draw_WordPerfect_Graphics",
        "draw_Visio_Document",
        "draw_Publisher_Document",
        "draw_Corel_Presentation_Exchange",
        "draw_CorelDraw_Document",
        "writer_LotusWordPro_Document",
        "writer_MIZI_Hwp_97", // Hanword (Hancom Office)

        // Non-compressed XML
        "writer_ODT_FlatXML",
        "calc_ODS_FlatXML",
        "impress_ODP_FlatXML",
        "draw_ODG_FlatXML",
        "calc_ADO_rowset_XML",
        "calc_MS_Excel_2003_XML",
        "writer_MS_Word_2003_XML",
        "writer_DocBook_File",
        "XHTML_File",
        "svg_Scalable_Vector_Graphics",
        "math_MathML_XML_Math",

        // Non-compressed text
        "dxf_AutoCAD_Interchange",
        "eps_Encapsulated_PostScript",
        "pbm_Portable_Bitmap",   // There is 'raw' and 'ascii' variants.
        "ppm_Portable_Pixelmap", // There is 'raw' and 'ascii' variants.
        "pgm_Portable_Graymap",  // There is 'raw' and 'ascii' variants.
        "xpm_XPM",
        "xbm_X_Consortium",
        "writer_Rich_Text_Format",
        "writer_web_HTML_help",
        "generic_HTML",
        "generic_Markdown",

        "generic_Text", // Plain text (catch all)

        // Anything ranked lower than generic_Text will never be used during
        // type detection (since generic_Text catches all).

        // Export only
        "writer_layout_dump_xml",
        "writer_indexing_export",
        "graphic_HTML",

        // Internal use only
        "StarBaseReportChart",
        "StarBaseReport",
        "math_MathType_3x", // MathType equation embedded in Word doc.
    };

    size_t n =  std::size(ranks);

    for (size_t i = 0; i < n; ++i)
    {
        if (o3tl::equalsAscii(rType, ranks[i]))
            return n - i - 1;
    }

    // Not ranked.  Treat them equally.  Unranked formats have higher priority
    // than the ranked internal ones since they may be defined externally.
    return n;
}

/**
 * Types with matching pattern first, then extension, then custom ranks by
 * types, then types that are supported by the document service come next.
 * Lastly, sort them alphabetically.
 */
struct SortByPriority
{
    bool operator() (const FlatDetectionInfo& r1, const FlatDetectionInfo& r2) const
    {
        if (r1.bMatchByPattern != r2.bMatchByPattern)
            return r1.bMatchByPattern;

        if (r1.bMatchByExtension != r2.bMatchByExtension)
            return r1.bMatchByExtension;

        int rank1 = getFlatTypeRank(r1.sType);
        int rank2 = getFlatTypeRank(r2.sType);

        if (rank1 != rank2)
            return rank1 > rank2;

        if (r1.bPreselectedByDocumentService != r2.bPreselectedByDocumentService)
            return r1.bPreselectedByDocumentService;

        // All things being equal, sort them alphabetically.
        return r1.sType > r2.sType;
    }
};

struct SortByType
{
    bool operator() (const FlatDetectionInfo& r1, const FlatDetectionInfo& r2) const
    {
        return r1.sType > r2.sType;
    }
};

struct EqualByType
{
    bool operator() (const FlatDetectionInfo& r1, const FlatDetectionInfo& r2) const
    {
        return r1.sType == r2.sType;
    }
};

class FindByType
{
    OUString maType;
public:
    explicit FindByType(OUString aType) : maType(std::move(aType)) {}
    bool operator() (const FlatDetectionInfo& rInfo) const
    {
        return rInfo.sType == maType;
    }
};

#if DEBUG_TYPE_DETECTION
void printFlatDetectionList(const char* caption, const FlatDetection& types)
{
    cout << "-- " << caption << " (size=" << types.size() << ")" << endl;
    for (auto const& item : types)
    {
        cout << "  type='" << item.sType << "'; match by extension (" << item.bMatchByExtension
            << "); match by pattern (" << item.bMatchByPattern << "); pre-selected by doc service ("
            << item.bPreselectedByDocumentService << ")" << endl;
    }
    cout << "--" << endl;
}
#endif

}

OUString SAL_CALL TypeDetection::queryTypeByDescriptor(css::uno::Sequence< css::beans::PropertyValue >& lDescriptor,
                                                              sal_Bool                                         bAllowDeep )
{
    // make the descriptor more usable :-)
    utl::MediaDescriptor stlDescriptor(lDescriptor);
    OUString sType, sURL;

    try
    {
        // SAFE -> ----------------------------------
        std::unique_lock aLock(m_aMutex);

        // parse given URL to split it into e.g. main and jump marks ...
        sURL = stlDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_URL, OUString());

#if OSL_DEBUG_LEVEL > 0
        if (stlDescriptor.find( u"FileName"_ustr ) != stlDescriptor.end())
            OSL_FAIL("Detect using of deprecated and already unsupported MediaDescriptor property \"FileName\"!");
#endif

        css::util::URL  aURL;
        aURL.Complete = sURL;
        css::uno::Reference< css::util::XURLTransformer > xParser(css::util::URLTransformer::create(m_xContext));
        xParser->parseStrict(aURL);

        OUString aSelectedFilter = stlDescriptor.getUnpackedValueOrDefault(
            utl::MediaDescriptor::PROP_FILTERNAME, OUString());
        if (!aSelectedFilter.isEmpty())
        {
            // Caller specified the filter type.  Honor it.  Just get the default
            // type for that filter, and bail out.
            if (impl_validateAndSetFilterOnDescriptor(stlDescriptor, aSelectedFilter))
                return stlDescriptor[utl::MediaDescriptor::PROP_TYPENAME].get<OUString>();
        }

        FlatDetection lFlatTypes;
        impl_getAllFormatTypes(aLock, aURL, stlDescriptor, lFlatTypes);

        aLock.unlock();
        // <- SAFE ----------------------------------

        // Properly prioritize all candidate types.
        std::stable_sort(lFlatTypes.begin(), lFlatTypes.end(), SortByPriority());
        auto last = std::unique(lFlatTypes.begin(), lFlatTypes.end(), EqualByType());
        lFlatTypes.erase(last, lFlatTypes.end());

        OUString sLastChance;

        // verify every flat detected (or preselected!) type
        // by calling its registered deep detection service.
        // But break this loop if a type match to the given descriptor
        // by a URL pattern(!) or if deep detection isn't allowed from
        // outside (bAllowDeep=sal_False) or break the whole detection by
        // throwing an exception if creation of the might needed input
        // stream failed by e.g. an IO exception ...
        if (!lFlatTypes.empty())
            sType = impl_detectTypeFlatAndDeep(stlDescriptor, lFlatTypes, bAllowDeep, sLastChance);

        // flat detection failed
        // pure deep detection failed
        // => ask might existing InteractionHandler
        // means: ask user for its decision
        if (sType.isEmpty() && !m_bCancel)
            sType = impl_askUserForTypeAndFilterIfAllowed(stlDescriptor);


        // no real detected type - but a might valid one.
        // update descriptor and set last chance for return.
        if (sType.isEmpty() && !sLastChance.isEmpty() && !m_bCancel)
        {
            OSL_FAIL("set first flat detected type without a registered deep detection service as \"last chance\" ... nevertheless some other deep detections said \"NO\". I TRY IT!");
            sType = sLastChance;
        }
    }
    catch(const css::uno::RuntimeException&)
    {
        throw;
    }
    catch(const css::uno::Exception&)
    {
        TOOLS_WARN_EXCEPTION("filter.config", "caught exception while querying type of " << sURL);
        sType.clear();
    }

    // adapt media descriptor, so it contains the right values
    // for type/filter name/document service/ etcpp.
    impl_checkResultsAndAddBestFilter(stlDescriptor, sType); // Attention: sType is used as IN/OUT param here and will might be changed inside this method !!!
    impl_validateAndSetTypeOnDescriptor(stlDescriptor, sType);

    stlDescriptor >> lDescriptor;
    return sType;
}


void TypeDetection::impl_checkResultsAndAddBestFilter(utl::MediaDescriptor& rDescriptor,
                                                      OUString&               sType      )
{
    // a)
    // Don't overwrite a might preselected filter!
    OUString sFilter = rDescriptor.getUnpackedValueOrDefault(
                                utl::MediaDescriptor::PROP_FILTERNAME,
                                OUString());
    if (!sFilter.isEmpty())
        return;

    auto & cache = GetTheFilterCache();

    // b)
    // check a preselected document service too.
    // Then we have to search a suitable filter within this module.
    OUString sDocumentService = rDescriptor.getUnpackedValueOrDefault(
                                            utl::MediaDescriptor::PROP_DOCUMENTSERVICE,
                                            OUString());
    if (!sDocumentService.isEmpty())
    {
        try
        {
            OUString sRealType = sType;

            // SAFE ->
            std::unique_lock aLock(m_aMutex);

            // Attention: For executing next lines of code, We must be sure that
            // all filters already loaded :-(
            // That can disturb our "load on demand feature". But we have no other chance!
            cache.load(FilterCache::E_CONTAINS_FILTERS);

            css::beans::NamedValue lIProps[] {
                { PROPNAME_DOCUMENTSERVICE, uno::Any(sDocumentService) },
                { PROPNAME_TYPE, uno::Any(sRealType) } };
            std::vector<OUString> lFilters = cache.getMatchingItemsByProps(FilterCache::E_FILTER, lIProps);

            aLock.unlock();
            // <- SAFE

            for (auto const& filter : lFilters)
            {
                // SAFE ->
                aLock.lock();
                try
                {
                    CacheItem aFilter = cache.getItem(FilterCache::E_FILTER, filter);
                    sal_Int32 nFlags  = 0;
                    aFilter[PROPNAME_FLAGS] >>= nFlags;

                    if (static_cast<SfxFilterFlags>(nFlags) & SfxFilterFlags::IMPORT)
                        sFilter = filter;
                    if (static_cast<SfxFilterFlags>(nFlags) & SfxFilterFlags::PREFERED)
                        break;
                }
                catch(const css::uno::Exception&) {}
                aLock.unlock();
                // <- SAFE
            }

            if (!sFilter.isEmpty())
            {
                rDescriptor[utl::MediaDescriptor::PROP_TYPENAME  ] <<= sRealType;
                rDescriptor[utl::MediaDescriptor::PROP_FILTERNAME] <<= sFilter;
                sType = sRealType;
                return;
            }
        }
        catch(const css::uno::Exception&)
            {}
    }

    // c)
    // We can use the preferred filter for the specified type.
    // Such preferred filter points:
    // - to the default filter of the preferred application
    // - or to any other filter if no preferred filter was set.
    // Note: It's an optimization only!
    // It's not guaranteed, that such preferred filter exists.
    sFilter.clear();
    try
    {
        CacheItem aType = cache.getItem(FilterCache::E_TYPE, sType);
        aType[PROPNAME_PREFERREDFILTER] >>= sFilter;
        cache.getItem(FilterCache::E_FILTER, sFilter);

        // no exception => found valid type and filter => set it on the given descriptor
        rDescriptor[utl::MediaDescriptor::PROP_TYPENAME  ] <<= sType  ;
        rDescriptor[utl::MediaDescriptor::PROP_FILTERNAME] <<= sFilter;
        return;
    }
    catch(const css::uno::Exception&)
        {}

    // d)
    // Search for any import(!) filter, which is registered for this type.
    sFilter.clear();
    try
    {
        // Attention: For executing next lines of code, We must be sure that
        // all filters already loaded :-(
        // That can disturb our "load on demand feature". But we have no other chance!
        cache.load(FilterCache::E_CONTAINS_FILTERS);

        css::beans::NamedValue lIProps[] {
            { PROPNAME_TYPE, uno::Any(sType) } };
        std::vector<OUString> lFilters = cache.getMatchingItemsByProps(FilterCache::E_FILTER, lIProps);

        for (auto const& filter : lFilters)
        {
            sFilter = filter;

            try
            {
                CacheItem aFilter = cache.getItem(FilterCache::E_FILTER, sFilter);
                sal_Int32 nFlags  = 0;
                aFilter[PROPNAME_FLAGS] >>= nFlags;

                if (static_cast<SfxFilterFlags>(nFlags) & SfxFilterFlags::IMPORT)
                    break;
            }
            catch(const css::uno::Exception&)
                { continue; }

            sFilter.clear();
        }

        if (!sFilter.isEmpty())
        {
            rDescriptor[utl::MediaDescriptor::PROP_TYPENAME  ] <<= sType  ;
            rDescriptor[utl::MediaDescriptor::PROP_FILTERNAME] <<= sFilter;
            return;
        }
    }
    catch(const css::uno::Exception&)
        {}
}


bool TypeDetection::impl_getPreselectionForType(
    std::unique_lock<std::mutex>& /*rGuard*/,
    const OUString& sPreSelType, const util::URL& aParsedURL, FlatDetection& rFlatTypes, bool bDocService)
{
    // Can be used to suppress execution of some parts of this method
    // if it's already clear that detected type is valid or not.
    // It's necessary to use shared code at the end, which update
    // all return parameters consistency!
    bool bBreakDetection = false;

    // Further we must know if it matches by pattern
    // Every flat detected type by pattern won't be detected deep!
    bool bMatchByPattern = false;

    // And we must know if a preselection must be preferred, because
    // it matches by its extension too.
    bool bMatchByExtension = false;

    // validate type
    OUString sType(sPreSelType);
    CacheItem       aType;
    try
    {
        aType = GetTheFilterCache().getItem(FilterCache::E_TYPE, sType);
    }
    catch(const css::container::NoSuchElementException&)
    {
        sType.clear();
        bBreakDetection = true;
    }

    if (!bBreakDetection)
    {
        // We can't check a preselected type for a given stream!
        // So we must believe, that it can work ...
        if ( aParsedURL.Complete == "private:stream" )
            bBreakDetection = true;
    }

    if (!bBreakDetection)
    {
        // extract extension from URL .. to check it case-insensitive !
        INetURLObject   aParser    (aParsedURL.Main);
        OUString sExtension = aParser.getExtension(INetURLObject::LAST_SEGMENT       ,
                                                          true                          ,
                                                          INetURLObject::DecodeMechanism::WithCharset);
        sExtension = sExtension.toAsciiLowerCase();

        // otherwise we must know, if it matches to the given URL really.
        // especially if it matches by its extension or pattern registration.
        const css::uno::Sequence<OUString> lExtensions = aType[PROPNAME_EXTENSIONS].get<css::uno::Sequence<OUString> >();
        const css::uno::Sequence<OUString> lURLPattern = aType[PROPNAME_URLPATTERN].get<css::uno::Sequence<OUString> >();

        for (auto const& extension : lExtensions)
        {
            OUString sCheckExtension(extension.toAsciiLowerCase());
            if (sCheckExtension == sExtension)
            {
                bBreakDetection        = true;
                bMatchByExtension      = true;
                break;
            }
        }

        if (!bBreakDetection)
        {
            for (auto const& elem : lURLPattern)
            {
                WildCard aCheck(elem);
                if (aCheck.Matches(aParsedURL.Main))
                {
                    bMatchByPattern = true;
                    break;
                }
            }
        }
    }

    // if it's a valid type - set it on all return values!
    if (!sType.isEmpty())
    {
        FlatDetection::iterator it = std::find_if(rFlatTypes.begin(), rFlatTypes.end(), FindByType(sType));
        if (it != rFlatTypes.end())
        {
            if (bMatchByExtension)
                it->bMatchByExtension = true;
            if (bMatchByPattern)
                it->bMatchByPattern = true;
            if (bDocService)
                it->bPreselectedByDocumentService = true;
        }

        return true;
    }

    // not valid!
    return false;
}

void TypeDetection::impl_getPreselectionForDocumentService(
    std::unique_lock<std::mutex>& rGuard,
    const OUString& sPreSelDocumentService, const util::URL& aParsedURL, FlatDetection& rFlatTypes)
{
    // get all filters, which match to this doc service
    std::vector<OUString> lFilters;
    try
    {
        // Attention: For executing next lines of code, We must be sure that
        // all filters already loaded :-(
        // That can disturb our "load on demand feature". But we have no other chance!
        auto & cache = GetTheFilterCache();
        cache.load(FilterCache::E_CONTAINS_FILTERS);

        css::beans::NamedValue lIProps[] {
            { PROPNAME_DOCUMENTSERVICE, css::uno::Any(sPreSelDocumentService) } };
        lFilters = cache.getMatchingItemsByProps(FilterCache::E_FILTER, lIProps);
    }
    catch (const css::container::NoSuchElementException&)
    {
        lFilters.clear();
    }

    // step over all filters, and check if its registered type
    // match the given URL.
    // But use temp. list of "preselected types" instead of incoming rFlatTypes list!
    // The reason behind: we must filter the obtained results. And copying stl entries
    // is an easier job than removing them .-)
    for (auto const& filter : lFilters)
    {
        OUString aType = impl_getTypeFromFilter(rGuard, filter);
        if (aType.isEmpty())
            continue;

        impl_getPreselectionForType(rGuard, aType, aParsedURL, rFlatTypes, true);
    }
}

OUString TypeDetection::impl_getTypeFromFilter(std::unique_lock<std::mutex>& /*rGuard*/, const OUString& rFilterName)
{
    CacheItem aFilter;
    try
    {
        aFilter = GetTheFilterCache().getItem(FilterCache::E_FILTER, rFilterName);
    }
    catch (const container::NoSuchElementException&)
    {
        return OUString();
    }

    OUString aType;
    aFilter[PROPNAME_TYPE] >>= aType;
    return aType;
}

void TypeDetection::impl_getAllFormatTypes(
    std::unique_lock<std::mutex>& rGuard,
    const util::URL& aParsedURL, utl::MediaDescriptor const & rDescriptor, FlatDetection& rFlatTypes)
{
    rFlatTypes.clear();

    // Get all filters that we have.
    std::vector<OUString> aFilterNames;
    try
    {
        auto & cache = GetTheFilterCache();
        cache.load(FilterCache::E_CONTAINS_FILTERS);
        aFilterNames = cache.getItemNames(FilterCache::E_FILTER);
    }
    catch (const container::NoSuchElementException&)
    {
        return;
    }

    // Retrieve the default type for each of these filters, and store them.
    for (auto const& filterName : aFilterNames)
    {
        OUString aType = impl_getTypeFromFilter(rGuard, filterName);

        if (aType.isEmpty())
            continue;

        FlatDetectionInfo aInfo; // all flags set to false by default.
        aInfo.sType = aType;
        rFlatTypes.push_back(aInfo);
    }

    {
        // Get all types that match the URL alone.
        FlatDetection aFlatByURL;
        GetTheFilterCache().detectFlatForURL(aParsedURL, aFlatByURL);
        for (auto const& elem : aFlatByURL)
        {
            FlatDetection::iterator itPos = std::find_if(rFlatTypes.begin(), rFlatTypes.end(), FindByType(elem.sType));
            if (itPos == rFlatTypes.end())
                // Not in the list yet.
                rFlatTypes.push_back(elem);
            else
            {
                // Already in the list. Update the flags.
                FlatDetectionInfo& rInfo = *itPos;
                const FlatDetectionInfo& rThisInfo = elem;
                if (rThisInfo.bMatchByExtension)
                    rInfo.bMatchByExtension = true;
                if (rThisInfo.bMatchByPattern)
                    rInfo.bMatchByPattern = true;
                if (rThisInfo.bPreselectedByDocumentService)
                    rInfo.bPreselectedByDocumentService = true;
            }
        }
    }

    // Remove duplicates.
    std::stable_sort(rFlatTypes.begin(), rFlatTypes.end(), SortByType());
    auto last = std::unique(rFlatTypes.begin(), rFlatTypes.end(), EqualByType());
    rFlatTypes.erase(last, rFlatTypes.end());

    // Mark pre-selected type (if any) to have it prioritized.
    OUString sSelectedType = rDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_TYPENAME, OUString());
    if (!sSelectedType.isEmpty())
        impl_getPreselectionForType(rGuard, sSelectedType, aParsedURL, rFlatTypes, false);

    // Mark all types preferred by the current document service, to have it prioritized.
    OUString sSelectedDoc = rDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_DOCUMENTSERVICE, OUString());
    if (!sSelectedDoc.isEmpty())
        impl_getPreselectionForDocumentService(rGuard, sSelectedDoc, aParsedURL, rFlatTypes);
}


static bool isBrokenZIP(const css::uno::Reference<css::io::XInputStream>& xStream,
                        const css::uno::Reference<css::uno::XComponentContext>& xContext)
{
    try
    {
        // Only consider seekable streams starting with "PK", to avoid false detections
        css::uno::Reference<css::io::XSeekable> xSeek(xStream, css::uno::UNO_QUERY_THROW);
        comphelper::ScopeGuard restorePos(
            [xSeek, nPos = xSeek->getPosition()]
            {
                try
                {
                    xSeek->seek(nPos);
                }
                catch (const css::uno::Exception&)
                {
                }
            });
        css::uno::Sequence<sal_Int8> magic(2);
        xStream->readBytes(magic, 2);
        if (magic.getLength() < 2 || magic[0] != 'P' || magic[1] != 'K')
            return false;
    }
    catch (const css::uno::Exception&)
    {
        return false;
    }

    std::vector<css::uno::Any> aArguments{
        css::uno::Any(xStream),
        css::uno::Any(css::beans::NamedValue(u"AllowRemoveOnInsert"_ustr, css::uno::Any(false))),
        css::uno::Any(css::beans::NamedValue(u"StorageFormat"_ustr,
                                             css::uno::Any(css::embed::StorageFormats::ZIP))),
    };
    try
    {
        // If this is a broken ZIP package, or not a ZIP, this would throw ZipIOException
        xContext->getServiceManager()->createInstanceWithArgumentsAndContext(
            u"com.sun.star.packages.comp.ZipPackage"_ustr, comphelper::containerToSequence(aArguments),
            xContext);
    }
    catch (const css::packages::zip::ZipIOException&)
    {
        // Now test if repair will succeed
        aArguments.emplace_back(css::beans::NamedValue(u"RepairPackage"_ustr, css::uno::Any(true)));
        try
        {
            // If this is a broken ZIP package that can be repaired, this would succeed,
            // and the result will be not empty
            if (css::uno::Reference<css::beans::XPropertySet> xPackage{
                    xContext->getServiceManager()->createInstanceWithArgumentsAndContext(
                        u"com.sun.star.packages.comp.ZipPackage"_ustr,
                        comphelper::containerToSequence(aArguments), xContext),
                    css::uno::UNO_QUERY })
                if (bool bHasElements; xPackage->getPropertyValue(u"HasElements"_ustr) >>= bHasElements)
                    return bHasElements;
        }
        catch (const css::uno::Exception&)
        {
        }
    }
    catch (const css::uno::Exception&)
    {
    }
    // The package is either not broken, or is not a repairable ZIP
    return false;
}


OUString TypeDetection::impl_detectTypeFlatAndDeep(      utl::MediaDescriptor& rDescriptor   ,
                                                          const FlatDetection&                 lFlatTypes    ,
                                                                bool                       bAllowDeep    ,
                                                                OUString&               rLastChance   )
{
    // reset it everytimes, so the outside code can distinguish between
    // a set and a not set value.
    rLastChance.clear();

    // tdf#96401: First of all, check if this is a broken ZIP package. Not doing this here would
    // make some filters silently not recognize their content in broken packages, and some filters
    // show a warning and mistakenly claim own content based on user choice.
    if (bAllowDeep && !rDescriptor.getUnpackedValueOrDefault(u"RepairPackage"_ustr, false)
        && rDescriptor.getUnpackedValueOrDefault(u"RepairAllowed"_ustr, true)
        && rDescriptor.contains(utl::MediaDescriptor::PROP_INTERACTIONHANDLER))
    {
        try
        {
            // tdf#161573: do not interact with the user about possible unrelated failures (e.g.,
            // missing file). If needed, that will happen later, in the main detection phase.
            auto aInteraction(rDescriptor[utl::MediaDescriptor::PROP_INTERACTIONHANDLER]);
            rDescriptor.erase(utl::MediaDescriptor::PROP_INTERACTIONHANDLER);
            comphelper::ScopeGuard interactionHelperGuard([&rDescriptor, &aInteraction]
                { rDescriptor[utl::MediaDescriptor::PROP_INTERACTIONHANDLER] = aInteraction; });
            impl_openStream(rDescriptor);
            if (auto xStream = rDescriptor.getUnpackedValueOrDefault(
                    utl::MediaDescriptor::PROP_INPUTSTREAM,
                    css::uno::Reference<css::io::XInputStream>()))
            {
                css::uno::Reference<css::uno::XComponentContext> xContext;

                // SAFE ->
                {
                    std::unique_lock aLock(m_aMutex);
                    xContext = m_xContext;
                }
                // <- SAFE

                if (isBrokenZIP(xStream, xContext))
                {
                    if (css::uno::Reference<css::task::XInteractionHandler> xInteraction{
                            aInteraction,
                            css::uno::UNO_QUERY })
                    {
                        INetURLObject aURL(rDescriptor.getUnpackedValueOrDefault(
                            utl::MediaDescriptor::PROP_URL, OUString()));
                        OUString aDocumentTitle
                            = aURL.getName(INetURLObject::LAST_SEGMENT, true,
                                           INetURLObject::DecodeMechanism::WithCharset);

                        // Ask the user whether they wants to try to repair
                        RequestPackageReparation aRequest(aDocumentTitle);
                        xInteraction->handle(aRequest.GetRequest());

                        if (aRequest.isApproved())
                        {
                            // lok: we want to overwrite file in jail, so don't use template flag
                            const bool bIsLOK = comphelper::LibreOfficeKit::isActive();
                            rDescriptor[utl::MediaDescriptor::PROP_DOCUMENTTITLE] <<= aDocumentTitle;
                            rDescriptor[utl::MediaDescriptor::PROP_ASTEMPLATE] <<= !bIsLOK;
                            rDescriptor[u"RepairPackage"_ustr] <<= true;
                        }
                        else
                            rDescriptor[u"RepairAllowed"_ustr] <<= false; // Do not ask again
                    }
                }
            }
        }
        catch (const css::uno::Exception&)
        {
            // No problem
        }
    }

    // step over all possible types for this URL.
    // solutions:
    // a) no types                                => no detection
    // b) deep detection not allowed              => return first valid type of list (because it's the preferred or the first valid one)
    //    or(!) match by URLPattern               => in such case a deep detection will be suppressed!
    // c) type has no detect service              => safe the first occurred type without a detect service
    //                                               as "last chance"(!). It will be used outside of this method
    //                                               if no further type could be detected.
    //                                               It must be the first one, because it can be a preferred type.
    //                                               Our types list was sorted by such criteria!
    // d) detect service return a valid result    => return its decision
    // e) detect service return an invalid result
    //    or any needed information could not be
    //    obtained from the cache                 => ignore it, and continue with search

    for (auto const& flatTypeInfo : lFlatTypes)
    {
        if (m_bCancel)
            break;
        OUString sFlatType = flatTypeInfo.sType;

        if (!impl_validateAndSetTypeOnDescriptor(rDescriptor, sFlatType))
            continue;

        // b)
        if (
            (!bAllowDeep                  ) ||
            (flatTypeInfo.bMatchByPattern)
           )
        {
            return sFlatType;
        }

        try
        {
            // SAFE -> ----------------------------------
            std::unique_lock aLock(m_aMutex);
            CacheItem aType = GetTheFilterCache().getItem(FilterCache::E_TYPE, sFlatType);
            aLock.unlock();

            OUString sDetectService;
            aType[PROPNAME_DETECTSERVICE] >>= sDetectService;

            // c)
            if (sDetectService.isEmpty())
            {
                // flat detected types without any registered deep detection service and not
                // preselected by the user can be used as LAST CHANCE in case no other type could
                // be detected. Of course only the first type without deep detector can be used.
                // Further ones has to be ignored.
                if (rLastChance.isEmpty())
                    rLastChance = sFlatType;

                continue;
            }

            OUString sDeepType = impl_askDetectService(sDetectService, rDescriptor);

            // d)
            if (!sDeepType.isEmpty())
                return sDeepType;
        }
        catch(const css::container::NoSuchElementException&)
            {}
        // e)
    }

    return OUString();
    // <- SAFE ----------------------------------
}

void TypeDetection::impl_seekStreamToZero(utl::MediaDescriptor const & rDescriptor)
{
    // try to seek to 0 ...
    // But because XSeekable is an optional interface ... try it only .-)
    css::uno::Reference< css::io::XInputStream > xStream = rDescriptor.getUnpackedValueOrDefault(
                                                            utl::MediaDescriptor::PROP_INPUTSTREAM,
                                                            css::uno::Reference< css::io::XInputStream >());
    css::uno::Reference< css::io::XSeekable > xSeek(xStream, css::uno::UNO_QUERY);
    if (!xSeek.is())
        return;

    try
    {
        xSeek->seek(0);
    }
    catch(const css::uno::RuntimeException&)
    {
        throw;
    }
    catch(const css::uno::Exception&)
    {
    }
}

OUString TypeDetection::impl_askDetectService(const OUString&               sDetectService,
                                                           utl::MediaDescriptor& rDescriptor   )
{
    // Open the stream and add it to the media descriptor if this method is called for the first time.
    // All following requests to this method will detect, that there already exists a stream .-)
    // Attention: This method throws an exception if the stream could not be opened.
    // It's important to break any further detection in such case.
    // Catch it on the highest detection level only !!!
    impl_openStream(rDescriptor);

    // seek to 0 is an optional feature to be more robust against
    // "simple implemented detect services" .-)
    impl_seekStreamToZero(rDescriptor);

    css::uno::Reference< css::document::XExtendedFilterDetection > xDetector;
    css::uno::Reference< css::uno::XComponentContext >         xContext;

    // SAFE ->
    {
        std::unique_lock aLock(m_aMutex);
        xContext = m_xContext;
    }
    // <- SAFE

    try
    {
        // Attention! If e.g. an office module was not installed sometimes we
        // find a registered detect service, which is referred inside the
        // configuration ... but not really installed. On the other side we use
        // third party components here, which can make trouble anyway.  So we
        // should handle errors during creation of such services more
        // gracefully .-)
        xDetector.set(
                xContext->getServiceManager()->createInstanceWithContext(sDetectService, xContext),
                css::uno::UNO_QUERY_THROW);
    }
    catch (...)
    {
    }

    if ( ! xDetector.is())
        return OUString();

    OUString sDeepType;
    try
    {
        // start deep detection
        // Don't forget to convert stl descriptor to its uno representation.

        /* Attention!
                You have to use an explicit instance of this uno sequence...
                Because it's used as an in out parameter. And in case of a temp. used object
                we will run into memory corruptions!
        */
        css::uno::Sequence< css::beans::PropertyValue > lDescriptor;
        rDescriptor >> lDescriptor;
        sDeepType = xDetector->detect(lDescriptor);
        rDescriptor << lDescriptor;
    }
    catch (...)
    {
        // We should ignore errors here.
        // Thrown exceptions mostly will end in crash recovery...
        // But might be we find another deep detection service which can detect the same
        // document without a problem .-)
        sDeepType.clear();
    }

    // seek to 0 is an optional feature to be more robust against
    // "simple implemented detect services" .-)
    impl_seekStreamToZero(rDescriptor);

    // analyze the results
    // a) detect service returns "" => return "" too and remove TYPE/FILTER prop from descriptor
    // b) returned type is unknown  => return "" too and remove TYPE/FILTER prop from descriptor
    // c) returned type is valid    => check TYPE/FILTER props inside descriptor and return the type

    // this special helper checks for a valid type
    // and set right values on the descriptor!
    bool bValidType = impl_validateAndSetTypeOnDescriptor(rDescriptor, sDeepType);
    if (bValidType)
        return sDeepType;

    return OUString();
}


OUString TypeDetection::impl_askUserForTypeAndFilterIfAllowed(utl::MediaDescriptor& rDescriptor)
{
    css::uno::Reference< css::task::XInteractionHandler > xInteraction =
        rDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_INTERACTIONHANDLER,
        css::uno::Reference< css::task::XInteractionHandler >());

    if (!xInteraction.is())
        return OUString();

    OUString sURL =
        rDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_URL,
        OUString());

    css::uno::Reference< css::io::XInputStream > xStream =
        rDescriptor.getUnpackedValueOrDefault(utl::MediaDescriptor::PROP_INPUTSTREAM,
        css::uno::Reference< css::io::XInputStream >());

    // Don't disturb the user for "non existing files - means empty URLs" or
    // if we were forced to detect a stream.
    // Reason behind: we must be sure to ask user for "unknown contents" only...
    // and not for "missing files". Especially if detection is done by a stream only
    // we can't check if the stream points to an "existing content"!
    if (
        (sURL.isEmpty()                                     ) || // "non existing file" ?
        (!xStream.is()                                         ) || // non existing file !
        (sURL.equalsIgnoreAsciiCase("private:stream"))    // not a good idea .-)
       )
        return OUString();

    try
    {
        // create a new request to ask user for its decision about the usable filter
        ::framework::RequestFilterSelect aRequest(sURL);
        xInteraction->handle(aRequest.GetRequest());

        // "Cancel" pressed? => return with error
        if (aRequest.isAbort())
            return OUString();

        // "OK" pressed => verify the selected filter, get its corresponding
        // type and return it. (BTW: We must update the media descriptor here ...)
        // The user selected explicitly a filter ... but normally we are interested on
        // a type here only. But we must be sure, that the selected filter is used
        // too and no ambiguous filter registration disturb us .-)

        OUString sFilter = aRequest.getFilter();
        if (!impl_validateAndSetFilterOnDescriptor(rDescriptor, sFilter))
            return OUString();
        OUString sType;
        rDescriptor[utl::MediaDescriptor::PROP_TYPENAME] >>= sType;
        return sType;
    }
    catch(const css::uno::Exception&)
        {}

    return OUString();
}


void TypeDetection::impl_openStream(utl::MediaDescriptor& rDescriptor)
{
    bool bSuccess = false;
    OUString sURL = rDescriptor.getUnpackedValueOrDefault( utl::MediaDescriptor::PROP_URL, OUString() );
    bool bRequestedReadOnly = rDescriptor.getUnpackedValueOrDefault( utl::MediaDescriptor::PROP_READONLY, false );
    if ( comphelper::isFileUrl( sURL ) )
    {
        // OOo uses own file locking mechanics in case of local file
        bSuccess = rDescriptor.addInputStreamOwnLock();
    }
    else
        bSuccess = rDescriptor.addInputStream();

    if ( !bSuccess )
        throw css::uno::Exception(
            "Could not open stream for <" + sURL + ">",
            getXWeak());

    if ( !bRequestedReadOnly )
    {
        // The MediaDescriptor implementation adds ReadOnly argument if the file can not be opened for writing
        // this argument should be either removed or an additional argument should be added so that application
        // can separate the case when the user explicitly requests readonly document.
        // The current solution is to remove it here.
        rDescriptor.erase( utl::MediaDescriptor::PROP_READONLY );
    }
}


void TypeDetection::impl_removeTypeFilterFromDescriptor(utl::MediaDescriptor& rDescriptor)
{
    utl::MediaDescriptor::iterator pItType   = rDescriptor.find(utl::MediaDescriptor::PROP_TYPENAME  );
    utl::MediaDescriptor::iterator pItFilter = rDescriptor.find(utl::MediaDescriptor::PROP_FILTERNAME);
    if (pItType != rDescriptor.end())
        rDescriptor.erase(pItType);
    if (pItFilter != rDescriptor.end())
        rDescriptor.erase(pItFilter);
}


bool TypeDetection::impl_validateAndSetTypeOnDescriptor(      utl::MediaDescriptor& rDescriptor,
                                                            const OUString&               sType      )
{
    if (GetTheFilterCache().hasItem(FilterCache::E_TYPE, sType))
    {
        rDescriptor[utl::MediaDescriptor::PROP_TYPENAME] <<= sType;
        return true;
    }

    // remove all related information from the descriptor
    impl_removeTypeFilterFromDescriptor(rDescriptor);
    return false;
}


bool TypeDetection::impl_validateAndSetFilterOnDescriptor( utl::MediaDescriptor& rDescriptor,
                                                           const OUString&               sFilter    )
{
    try
    {
        auto & cache = GetTheFilterCache();
        CacheItem aFilter = cache.getItem(FilterCache::E_FILTER, sFilter);
        OUString sType;
        aFilter[PROPNAME_TYPE] >>= sType;

        // found valid type and filter => set it on the given descriptor
        rDescriptor[utl::MediaDescriptor::PROP_TYPENAME  ] <<= sType  ;
        rDescriptor[utl::MediaDescriptor::PROP_FILTERNAME] <<= sFilter;
        return true;
    }
    catch(const css::container::NoSuchElementException&){}

    // remove all related information from the descriptor
    impl_removeTypeFilterFromDescriptor(rDescriptor);
    return false;
}

} // namespace filter

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface*
filter_TypeDetection_get_implementation(
    css::uno::XComponentContext* context, css::uno::Sequence<css::uno::Any> const&)
{
    return cppu::acquire(new filter::config::TypeDetection(context));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
