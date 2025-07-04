/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>
#include <test/bootstrapfixture.hxx>

#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>

#include <comphelper/genericpropertyset.hxx>
#include <comphelper/propertysetinfo.hxx>

#include <xmloff/maptype.hxx>
#include <xmloff/xmlimp.hxx>
#include <xmloff/xmlmetai.hxx>
#include <xmloff/xmlexp.hxx>
#include <xmloff/xmltoken.hxx>
#include <xmloff/xmlaustp.hxx>
#include <SchXMLExport.hxx>
#include <XMLChartPropertySetMapper.hxx>
#include <comphelper/processfactory.hxx>

using namespace ::xmloff::token;
using namespace ::com::sun::star;

class Test : public test::BootstrapFixture {
public:
    Test();

    virtual void setUp() override;
    virtual void tearDown() override;

    void testAutoStylePool();
    void testMetaGenerator();

    CPPUNIT_TEST_SUITE(Test);
    CPPUNIT_TEST(testAutoStylePool);
    CPPUNIT_TEST(testMetaGenerator);
    CPPUNIT_TEST_SUITE_END();
private:
    rtl::Reference<SvXMLExport> pExport;
};

Test::Test()
{
}

void Test::setUp()
{
    BootstrapFixture::setUp();

    pExport = new SchXMLExport(
        comphelper::getProcessComponentContext(), u"SchXMLExport.Compact"_ustr,
        SvXMLExportFlags::ALL);
}

void Test::tearDown()
{
    pExport.clear();
    BootstrapFixture::tearDown();
}

void Test::testAutoStylePool()
{
    rtl::Reference< SvXMLAutoStylePoolP > xPool(
        new SvXMLAutoStylePoolP( *pExport ) );
    rtl::Reference< XMLPropertySetMapper > xSetMapper(
        new XMLChartPropertySetMapper(pExport.get()) );
    rtl::Reference< XMLChartExportPropertyMapper > xExportPropMapper(
        new XMLChartExportPropertyMapper( xSetMapper, *pExport ) );

    xPool->AddFamily( XmlStyleFamily::TEXT_PARAGRAPH,
                      GetXMLToken( XML_PARAGRAPH ),
                      xExportPropMapper.get(),
                      u"Bob"_ustr );

    OUString aName = xPool->Add( XmlStyleFamily::TEXT_PARAGRAPH, u""_ustr, {} );

    // not that interesting but worth checking
    bool bHack = (getenv("LIBO_ONEWAY_STABLE_ODF_EXPORT") != nullptr);
    if (bHack)
        CPPUNIT_ASSERT_EQUAL_MESSAGE( "style / naming changed", u"Bob"_ustr, aName );
    else
        CPPUNIT_ASSERT_EQUAL_MESSAGE( "style / naming changed", u"Bob1"_ustr, aName );

    // find ourselves again:
    OUString aSameName = xPool->Find( XmlStyleFamily::TEXT_PARAGRAPH, u""_ustr, {} );
    CPPUNIT_ASSERT_EQUAL_MESSAGE( "same style not found", aName, aSameName );
}

void Test::testMetaGenerator()
{
    comphelper::PropertyMapEntry const aInfoMap[] = {
        { u"BuildId"_ustr, 0, ::cppu::UnoType<OUString>::get(), beans::PropertyAttribute::MAYBEVOID, 0 },
    };
    uno::Reference<beans::XPropertySet> const xInfoSet(
        comphelper::GenericPropertySet_CreateInstance(
            new comphelper::PropertySetInfo(aInfoMap)));

    static struct {
        char const*const generator;
        char const*const buildId;
        sal_uInt16 const result;
    } const tests [] = {
        // foreign
        { "AbiWord/2.8.6 (unix, gtk)", "", SvXMLImport::ProductVersionUnknown },
        { "Aspose.Words for Java 13.10.0.0", "", SvXMLImport::ProductVersionUnknown },
        { "CIB jsmerge 1.0.0", "", SvXMLImport::ProductVersionUnknown },
        { "Calligra/2.4.3", "", SvXMLImport::ProductVersionUnknown },
        { "CocoaODFWriter/1339", "", SvXMLImport::ProductVersionUnknown },
        { "KOffice/1.4.1", "", SvXMLImport::ProductVersionUnknown },
        { "KPresenter 1.3", "", SvXMLImport::ProductVersionUnknown },
        { "KSpread 1.3.2", "", SvXMLImport::ProductVersionUnknown },
        { "Lotus Symphony/1.2.0_20081023.1730/Win32", "", SvXMLImport::ProductVersionUnknown },
        { "Microsoft Excel Online", "", SvXMLImport::ProductVersionUnknown },
        { "MicrosoftOffice/12.0 MicrosoftExcel/CalculationVersion-4518", "", SvXMLImport::ProductVersionUnknown },
        { "MicrosoftOffice/15.0 MicrosoftWord", "", SvXMLImport::ProductVersionUnknown },
        { "ODF Converter v1.0.0", "", SvXMLImport::ProductVersionUnknown },
        { "ODF::lpOD 1.121", "", SvXMLImport::ProductVersionUnknown },
        { "ODFDOM/0.6.1$Build-1", "", SvXMLImport::ProductVersionUnknown },
        { "ODFPY/0.9.6", "", SvXMLImport::ProductVersionUnknown },
        { "OpenXML/ODF Translator Command Line Tool 3.0 2.0.0", "", SvXMLImport::ProductVersionUnknown },
        { "Org-7.8.03/Emacs-24.0.93.1", "", SvXMLImport::ProductVersionUnknown },
        { "TeX4ht from eqns_long.tex, options: xhtml,ooffice,refcaption", "", SvXMLImport::ProductVersionUnknown },
        { "TextMaker", "", SvXMLImport::ProductVersionUnknown },
        { "docbook2odf generator (http://open.comsultia.com/docbook2odf/)", "", SvXMLImport::ProductVersionUnknown },
        { "fig2sxd", "", SvXMLImport::ProductVersionUnknown },
        { "gnumeric/1.10.9", "", SvXMLImport::ProductVersionUnknown },
        { "libodfgen/0.1.6", "", SvXMLImport::ProductVersionUnknown },

        // OOo 1.x
        { "StarSuite 6.0 (Linux)", "645$8687", SvXMLImport::OOo_1x },
        { "StarOffice 6.1 (Win32)", "645$8687", SvXMLImport::OOo_1x },
        { "OpenOffice.org 1.1.2RC3.DE (Win32)", "645$8687", SvXMLImport::OOo_1x },
        { "OpenOffice.org 1.1.5 (Win32)", "645$8687", SvXMLImport::OOo_1x },
        { "StarOffice 7 (Win32)", "645$8687", SvXMLImport::OOo_1x },

        // OOo 2.x
        { "Sun_ODF_Plugin_for_Microsoft_Office/1.1$Win32 OpenOffice.org_project/680m5$Build-9221", "680$9221", SvXMLImport::OOo_2x },
        { "StarSuite/8$Win32 OpenOffice.org_project/680m6$Build-9095", "680$9095", SvXMLImport::OOo_2x },
        { "StarOffice/8$Win32 OpenOffice.org_project/680m93$Build-8897", "680$8897", SvXMLImport::OOo_2x },
        { "OpenOffice.org/2.0$Linux OpenOffice.org_project/680m3$Build-8968", "680$8968", SvXMLImport::OOo_2x },
        { "OpenOffice.org/2.1$Win32 OpenOffice.org_project/680m6$Build-9095", "680$9095", SvXMLImport::OOo_2x },
        { "OpenOffice.org/2.4$Win32 OpenOffice.org_project/680m248$Build-9274", "680$9274", SvXMLImport::OOo_2x },

        // OOo 3.x
        { "OpenOffice.org/3.0$Solaris_Sparc OpenOffice.org_project/300m9$Build-9358", "300$9358", SvXMLImport::OOo_30x },
        { "StarSuite/9$Unix OpenOffice.org_project/300m9$Build-9358", "300$9358", SvXMLImport::OOo_30x },
        { "StarOffice/9$Win32 OpenOffice.org_project/300m14$Build-9376", "300$9376", SvXMLImport::OOo_30x },
        { "OpenOffice.org/3.1$Solaris_x86 OpenOffice.org_project/310m11$Build-9399", "310$9399", SvXMLImport::OOo_31x },
        { "IBM_Lotus_Symphony/2.0$Win32 OpenOffice.org_project/310m11$Build-9399", "310$9399", SvXMLImport::OOo_31x },
        { "BrOffice.org/3.1$Linux OpenOffice.org_project/310m11$Build-9399", "310$9399", SvXMLImport::OOo_31x },
        { "StarOffice/9$Solaris_Sparc OpenOffice.org_project/310m19$Build-9420", "310$9420", SvXMLImport::OOo_31x },
        { "OpenOffice.org/3.2$Linux OpenOffice.org_project/320m12$Build-9483", "320$9483", SvXMLImport::OOo_32x },
        { "StarOffice/9$Win32 OpenOffice.org_project/320m12$Build-9483", "320$9483", SvXMLImport::OOo_32x },
        { "OpenOffice.org/3.3$Linux OpenOffice.org_project/330m20$Build-9567", "330$9567", SvXMLImport::OOo_33x },
        { "Oracle_Open_Office/3.3$Win32 OpenOffice.org_project/330m7$Build-9552", "330$9552", SvXMLImport::OOo_33x },
        { "OpenOffice.org/3.4$Unix OpenOffice.org_project/340m1$Build-9590", "340$9590", SvXMLImport::OOo_34x },

        // AOO versions
        { "OpenOffice/4.0.0$Win32 OpenOffice.org_project/400m3$Build-9702", "400$9702", SvXMLImport::AOO_40x },
        { "OpenOffice/4.0.1$Linux OpenOffice.org_project/401m4$Build-9713", "401$9713", SvXMLImport::AOO_40x },
        { "OpenOffice/4.1.1$FreeBSD/amd64 OpenOffice.org_project/411m6$Build-9775", "411$9775", SvXMLImport::AOO_4x },
        { "OpenOffice/4.1.2$OS/2 OpenOffice.org_project/412m3$Build-9782-bww", "412$9782-bww", SvXMLImport::AOO_4x },
        { "OpenOffice/4.1.4$Unix OpenOffice.org_project/414m2$Build-9785", "414$9785", SvXMLImport::AOO_4x },

        // LO versions
        { "LibreOffice/3.3$Linux LibreOffice_project/330m17$Build-3", "330$3;3.3", SvXMLImport::LO_3x },
        { "BrOffice/3.3$Win32 LibreOffice_project/330m19$Build-8", "330$8;3.3", SvXMLImport::LO_3x },
        { "LibreOffice/3.4$Linux LibreOffice_project/340m1$Build-1206", "340$1206;3.4", SvXMLImport::LO_3x },
        { "LibreOffice/3.5$Linux_X86_64 LibreOffice_project/3fa2330-e49ffd2-90d118b-705e248-051e21c", ";3.5", SvXMLImport::LO_3x },
        { "LibreOffice/3.6$Windows_x86 LibreOffice_project/a9a0717-273e462-768e6e3-978247f-65e65f", ";3.6", SvXMLImport::LO_3x },
        { "LibreOffice/4.0.2.2$Windows_x86 LibreOffice_project/4c82dcdd6efcd48b1d8bba66bfe1989deee49c3", ";4.0.2.2", SvXMLImport::LO_41x },
        { "LibreOffice/4.1.2.3$MacOSX_x86 LibreOffice_project/40b2d7fde7e8d2d7bc5a449dc65df4d08a7dd38", ";4.1.2.3", SvXMLImport::LO_41x },
        { "LibreOffice/4.2.8.2$Windows_x86 LibreOffice_project/48d50dbfc06349262c9d50868e5c1f630a573ebd", ";4.2.8.2", SvXMLImport::LO_42x },
        { "LibreOffice_from_Collabora_4.2-8/4.2.10.8$Linux_x86 LibreOffice_project/84584cc237b2eb93f7684d8fcd063bb37e87b5fb", ";4.2.10.8", SvXMLImport::LO_42x },
        { "LibreOffice/4.3.3.2$Linux_x86 LibreOffice_project/9bb7eadab57b6755b1265afa86e04bf45fbfc644", ";4.3.3.2", SvXMLImport::LO_43x },
        { "LibreOffice_from_Collabora_4.4-10/4.4.10.9$Linux_x86 LibreOffice_project/5600b19b88a01bbb669b0900100760758dff8c26", ";4.4.10.9", SvXMLImport::LO_44x },
        { "LibreOffice/4.3.3.2$Linux_X86_64 LibreOffice_project/430m0$Build-2", "430$2;4.3.3.2", SvXMLImport::LO_43x },
        { "LibreOffice/4.4.3.2$Linux_x86 LibreOffice_project/88805f81e9fe61362df02b9941de8e38a9b5fd16", ";4.4.3.2", SvXMLImport::LO_44x },
        { "LibreOffice/5.0.1.1$Linux_x86 LibreOffice_project/00m0$Build-1", "00$1;5.0.1.1", SvXMLImport::LO_5x },
        { "LibreOffice/5.0.3.2$Windows_X86_64 LibreOffice_project/e5f16313668ac592c1bfb310f4390624e3dbfb75", ";5.0.3.2", SvXMLImport::LO_5x },
        { "Collabora_Office/5.0.10.19$Linux_X86_64 LibreOffice_project/95060d44300d8866fa81c16fc8fe2afe22d63777", ";5.0.10.19", SvXMLImport::LO_5x },
        { "LibreOffice/5.1.6.2.0$Linux_X86_64 LibreOffice_project/10$Build-2", ";5.1.6.2.0", SvXMLImport::LO_5x },
        { "Collabora_Office/5.1.10.17$Linux_X86_64 LibreOffice_project/a104cbe76eefca3cf23973da68893d2225fd718b", ";5.1.10.17", SvXMLImport::LO_5x },
        { "LibreOffice/5.2.1.2$Windows_X86_64 LibreOffice_project/31dd62db80d4e60af04904455ec9c9219178d620", ";5.2.1.2", SvXMLImport::LO_5x },
        { "LibreOffice_Vanilla/5.2.3.5$MacOSX_X86_64 LibreOffice_project/83adc9c35c74e0badc710d981405858b1179a327", ";5.2.3.5", SvXMLImport::LO_5x },
        { "LibreOffice/5.3.4.2$Windows_X86_64 LibreOffice_project/f82d347ccc0be322489bf7da61d7e4ad13fe2ff3", ";5.3.4.2", SvXMLImport::LO_5x },
        { "Collabora_Office/5.3.10.27$Linux_X86_64 LibreOffice_project/7a5a5378661e338a44666c08773cc796b8d1c84a", ";5.3.10.27", SvXMLImport::LO_5x },
        { "LibreOfficeDev/5.4.7.0.0$Linux_X86_64 LibreOffice_project/ba7461fc88c08e75e315f786020a2946e56166c9", ";5.4.7.0.0", SvXMLImport::LO_5x },
        { "LibreOfficeDev/6.0.3.0.0$Linux_X86_64 LibreOffice_project/34442b85bfb0c451738b4db023345a7484463321", ";6.0.3.0.0", SvXMLImport::LO_6x },
        { "LibreOffice_powered_by_CIBDev/6.3.9.0.0$Linux_X86_64 LibreOffice_project/c87f331d2900eab70ac3021cbe530926efa6499f", ";6.3.9.0.0", SvXMLImport::LO_63x },
        { "LibreOffice_powered_by_CIBDev/6.4.0.0.0$Linux_X86_64 LibreOffice_project/e29e100174c133d27e953934311d68602c4515b7", ";6.4.0.0.0", SvXMLImport::LO_63x },
        { "LibreOfficeDev/7.0.6.0.0$Linux_X86_64 LibreOffice_project/dfc40e2292c6e19e285c10ed8c8044d9454107d0", ";7.0.6.0.0", SvXMLImport::LO_7x },
        { "CIB_OfficeDev/6.4.0.19$Linux_X86_64 LibreOffice_project/2e04f804b5f82770435f250873f07b3384d95504", ";6.4.0.19", SvXMLImport::LO_63x },
        { "LibreOfficeDev/24.2.0.0.alpha0$Linux_X86_64 LibreOffice_project/b81e7b6f3c71fb3ade1cb665444ac730dac0a9a9", ";24.2.0.0.", SvXMLImport::LO_242 },
        { "LibreOfficeDev/24.8.6.2$Linux_X86_64 LibreOffice_project/dc6216c3d2b4ec3bcdba950f1e6ee1d013adb2d6", ";24.8.6.2", SvXMLImport::LO_248 },
        { "LibreOfficeDev/25.8.0.0.alpha0$Linux_X86_64 LibreOffice_project/308705574842e0c36f7385f73fc47da9a4542367", ";25.8.0.0.", SvXMLImport::LO_New }
    };

    for (auto const[pGenerator, pBuildId, nResult] : tests)
    {
        // the DocumentInfo instance is cached so need fresh SvXMLImport
        rtl::Reference<SvXMLImport> const pImport(new SvXMLImport(
            comphelper::getProcessComponentContext(), u"testdummy"_ustr,
            SvXMLImportFlags::ALL));

        pImport->initialize(uno::Sequence<uno::Any>{ uno::Any(xInfoSet) });

        SvXMLMetaDocumentContext::setBuildId(OUString::createFromAscii(pGenerator), xInfoSet);
        if (pBuildId[0] != '\0')
        {
            CPPUNIT_ASSERT_EQUAL_MESSAGE(pGenerator,
                                 OUString::createFromAscii(pBuildId),
                                 xInfoSet->getPropertyValue(u"BuildId"_ustr).get<OUString>());
        }
        else
        {
            CPPUNIT_ASSERT_MESSAGE(pGenerator, !xInfoSet->getPropertyValue(u"BuildId"_ustr).hasValue());
        }
        CPPUNIT_ASSERT_EQUAL_MESSAGE(pGenerator, nResult, pImport->getGeneratorVersion());
    }
}

CPPUNIT_TEST_SUITE_REGISTRATION(Test);

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
