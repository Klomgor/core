/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <swmodeltestbase.hxx>

#include <com/sun/star/awt/FontSlant.hpp>
#include <com/sun/star/beans/XPropertyState.hpp>
#include <com/sun/star/style/VerticalAlignment.hpp>
#include <com/sun/star/style/XStyleFamiliesSupplier.hpp>
#include <com/sun/star/text/ColumnSeparatorStyle.hpp>
#include <com/sun/star/text/XDocumentIndex.hpp>
#include <com/sun/star/text/XDocumentIndexesSupplier.hpp>
#include <com/sun/star/text/XTextColumns.hpp>
#include <com/sun/star/text/XTextField.hpp>
#include <com/sun/star/text/XTextFieldsSupplier.hpp>
#include <com/sun/star/text/XTextGraphicObjectsSupplier.hpp>
#include <com/sun/star/text/XTextTable.hpp>
#include <com/sun/star/text/XTextTablesSupplier.hpp>
#include <com/sun/star/util/XRefreshable.hpp>
#include <unotools/localedatawrapper.hxx>
#include <comphelper/configuration.hxx>
#include <comphelper/sequenceashashmap.hxx>
#include <wrtsh.hxx>
#include <rootfrm.hxx>
#include <docsh.hxx>
#include <IDocumentFieldsAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentLinksAdministration.hxx>
#include <sfx2/linkmgr.hxx>
#include <officecfg/Office/Common.hxx>

namespace
{
class Test : public SwModelTestBase
{
public:
    Test()
        : SwModelTestBase(u"/sw/qa/extras/odfexport/data/"_ustr, u"writer8"_ustr)
    {
    }
};

CPPUNIT_TEST_FIXTURE(Test, tdf135942)
{
    loadAndReload("nestedTableInFooter.odt");
    // All table autostyles should be collected, including nested, and must not crash.

    CPPUNIT_ASSERT_EQUAL(1, getPages());

    xmlDocUniquePtr pXmlDoc = parseExport(u"styles.xml"_ustr);

    assertXPath(
        pXmlDoc,
        "/office:document-styles/office:automatic-styles/style:style[@style:family='table']", 2);
}

CPPUNIT_TEST_FIXTURE(Test, tdf150927)
{
    // Similar to tdf135942

    loadAndReload("table-in-frame-in-table-in-header-base.odt");
    // All table autostyles should be collected, including nested, and must not crash.

    CPPUNIT_ASSERT_EQUAL(1, getPages());

    xmlDocUniquePtr pXmlDoc = parseExport(u"styles.xml"_ustr);

    assertXPath(
        pXmlDoc,
        "/office:document-styles/office:automatic-styles/style:style[@style:family='table']", 2);
}

CPPUNIT_TEST_FIXTURE(Test, testPersonalMetaData)
{
    // 1. Remove personal info, keep user info
    auto pBatch(comphelper::ConfigurationChanges::create());
    officecfg::Office::Common::Security::Scripting::RemovePersonalInfoOnSaving::set(true, pBatch);
    officecfg::Office::Common::Security::Scripting::KeepDocUserInfoOnSaving::set(true, pBatch);
    pBatch->commit();

    loadAndReload("personalmetadata.odt");
    xmlDocUniquePtr pXmlDoc = parseExport(u"meta.xml"_ustr);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:initial-creator", 1);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:creation-date", 1);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/dc:date", 1);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/dc:creator", 1);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:printed-by", 1);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:print-date", 1);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:editing-duration", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:editing-cycles", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:template", 0);
    pXmlDoc = parseExport(u"settings.xml"_ustr);
    assertXPath(pXmlDoc,
                "/office:document-settings/office:settings/config:config-item-set[2]/"
                "config:config-item[@config:name='PrinterName']",
                0);
    assertXPath(pXmlDoc,
                "/office:document-settings/office:settings/config:config-item-set[2]/"
                "config:config-item[@config:name='PrinterSetup']",
                0);

    // 2. Remove user info too
    officecfg::Office::Common::Security::Scripting::KeepDocUserInfoOnSaving::set(false, pBatch);
    pBatch->commit();

    loadAndReload("personalmetadata.odt");
    pXmlDoc = parseExport(u"meta.xml"_ustr);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:initial-creator", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:creation-date", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/dc:date", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/dc:creator", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:printed-by", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:print-date", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:editing-duration", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:editing-cycles", 0);
    assertXPath(pXmlDoc, "/office:document-meta/office:meta/meta:template", 0);
    pXmlDoc = parseExport(u"settings.xml"_ustr);
    assertXPath(pXmlDoc,
                "/office:document-settings/office:settings/config:config-item-set[2]/"
                "config:config-item[@config:name='PrinterName']",
                0);
    assertXPath(pXmlDoc,
                "/office:document-settings/office:settings/config:config-item-set[2]/"
                "config:config-item[@config:name='PrinterSetup']",
                0);

    // Reset config change
    officecfg::Office::Common::Security::Scripting::RemovePersonalInfoOnSaving::set(false, pBatch);
    pBatch->commit();
}

CPPUNIT_TEST_FIXTURE(Test, testRemoveOnlyEditTimeMetaData)
{
    // 1. Check we have the original edit time info
    loadAndSave("personalmetadata.odt");
    xmlDocUniquePtr pXmlDoc = parseExport(u"meta.xml"_ustr);
    assertXPathContent(pXmlDoc, "/office:document-meta/office:meta/meta:editing-duration",
                       u"PT21M22S");

    // Set config RemoveEditingTimeOnSaving to true
    auto pBatch(comphelper::ConfigurationChanges::create());
    officecfg::Office::Common::Security::Scripting::RemoveEditingTimeOnSaving::set(true, pBatch);
    pBatch->commit();

    // 2. Check edit time info is 0
    loadAndSave("personalmetadata.odt");
    pXmlDoc = parseExport(u"meta.xml"_ustr);
    assertXPathContent(pXmlDoc, "/office:document-meta/office:meta/meta:editing-duration", u"P0D");

    // Reset config change
    officecfg::Office::Common::Security::Scripting::RemoveEditingTimeOnSaving::set(false, pBatch);
    pBatch->commit();
}

CPPUNIT_TEST_FIXTURE(Test, tdf151100)
{
    // Similar to tdf135942

    loadAndReload("tdf151100.docx");
    // All table autostyles should be collected, including nested, and must not crash.

    CPPUNIT_ASSERT_EQUAL(1, getPages());

    xmlDocUniquePtr pXmlDoc = parseExport(u"styles.xml"_ustr);

    assertXPath(
        pXmlDoc,
        "/office:document-styles/office:automatic-styles/style:style[@style:family='table']", 1);
}
DECLARE_ODFEXPORT_TEST(testGutterLeft, "gutter-left.odt")
{
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    uno::Reference<beans::XPropertySet> xPageStyle;
    getStyles(u"PageStyles"_ustr)->getByName(u"Standard"_ustr) >>= xPageStyle;
    sal_Int32 nGutterMargin{};
    xPageStyle->getPropertyValue(u"GutterMargin"_ustr) >>= nGutterMargin;
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 1270
    // - Actual  : 0
    // i.e. gutter margin was lost.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1270), nGutterMargin);
}

DECLARE_ODFEXPORT_TEST(testTdf52065_centerTabs, "testTdf52065_centerTabs.odt")
{
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    sal_Int32 nTabStop
        = getXPath(pXmlDoc, "//body/txt[4]/SwParaPortion/SwLineLayout/child::*[3]", "width")
              .toInt32();
    // Without the fix, the text was unseen, with a tabstop width of 64057. It should be 3057
    CPPUNIT_ASSERT(nTabStop < 4000);
    CPPUNIT_ASSERT(3000 < nTabStop);
    assertXPath(pXmlDoc, "//body/txt[4]/SwParaPortion/SwLineLayout/child::*[4]", "portion",
                u"Pečiatka zamestnávateľa");

    // tdf#149547: __XXX___invalid CharacterStyles should not be imported/exported
    CPPUNIT_ASSERT(!getStyles(u"CharacterStyles"_ustr)->hasByName(u"__XXX___invalid"_ustr));
}

DECLARE_ODFEXPORT_TEST(testTdf104254_noHeaderWrapping, "tdf104254_noHeaderWrapping.odt")
{
    CPPUNIT_ASSERT_EQUAL(1, getShapes());
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();

    sal_Int32 nParaHeight = getXPath(pXmlDoc, "//header/txt[1]/infos/bounds", "height").toInt32();
    // The wrapping on header images is supposed to be ignored (since OOo for MS compat reasons),
    // thus making the text run underneath the image. Before, height was 1104. Now it is 552.
    CPPUNIT_ASSERT_MESSAGE("Paragraph should fit on a single line", nParaHeight < 600);
}

DECLARE_ODFEXPORT_TEST(testTdf131025_noZerosInTable, "tdf131025_noZerosInTable.odt")
{
    uno::Reference<text::XTextTablesSupplier> xSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTables = xSupplier->getTextTables();
    uno::Reference<text::XTextTable> xTable(xTables->getByName(u"Table1"_ustr), uno::UNO_QUERY);

    uno::Reference<text::XTextRange> xCell(xTable->getCellByName(u"C3"_ustr), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"5 gp"_ustr, xCell->getString());
}

DECLARE_ODFEXPORT_TEST(testTdf153090, "Custom-Style-TOC.docx")
{
    uno::Reference<text::XDocumentIndexesSupplier> xIndexSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xIndexes(xIndexSupplier->getDocumentIndexes());
    uno::Reference<text::XDocumentIndex> xTOC(xIndexes->getByIndex(0), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"_CustomImageCaption"_ustr,
                         getProperty<OUString>(xTOC, u"CreateFromParagraphStyle"_ustr));
    // tdf#153659 this was imported as "table of figures" instead of "Figure Index 1"
    // thus custom settings were not retained after ToF update
    CPPUNIT_ASSERT_EQUAL(u"Figure Index 1"_ustr,
                         getProperty<OUString>(getParagraph(1), u"ParaStyleName"_ustr));

    xTOC->update();
    OUString const tocContent(xTOC->getAnchor()->getString());
    CPPUNIT_ASSERT(tocContent.indexOf("1. Abb. Ein Haus") != -1);
    CPPUNIT_ASSERT(tocContent.indexOf("2. Abb.Ein Schiff!") != -1);
    CPPUNIT_ASSERT(tocContent.indexOf(u"1. ábra Small house with Hungarian description category")
                   != -1);
}

DECLARE_ODFEXPORT_TEST(testTdf143793_noBodyWrapping, "tdf143793_noBodyWrapping.odt")
{
    CPPUNIT_ASSERT_EQUAL(2, getShapes());
    // Preserve old document wrapping. Compat "Use OOo 1.1 text wrapping around objects"
    // Originally, the body text did not wrap around spill-over header images
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Fits on one page", 1, getPages());

    xmlDocUniquePtr pXmlDoc = parseLayoutDump();

    sal_Int32 nParaHeight
        = getXPath(pXmlDoc, "//page[1]/header/txt[1]/infos/bounds", "height").toInt32();
    // The header text should wrap around the header image in OOo 1.1 and prior,
    // thus taking up two lines instead of one. One line is 276. It should be 552.
    CPPUNIT_ASSERT_MESSAGE("Header text should fill two lines", nParaHeight > 400);
}

CPPUNIT_TEST_FIXTURE(Test, testTdf137199)
{
    loadAndReload("tdf137199.docx");
    CPPUNIT_ASSERT_EQUAL(u">1<"_ustr,
                         getProperty<OUString>(getParagraph(1), u"ListLabelString"_ustr));

    CPPUNIT_ASSERT_EQUAL(u"1)"_ustr,
                         getProperty<OUString>(getParagraph(2), u"ListLabelString"_ustr));

    CPPUNIT_ASSERT_EQUAL(u"HELLO1WORLD!"_ustr,
                         getProperty<OUString>(getParagraph(3), u"ListLabelString"_ustr));

    CPPUNIT_ASSERT_EQUAL(u"HELLO2WORLD!"_ustr,
                         getProperty<OUString>(getParagraph(4), u"ListLabelString"_ustr));
}

DECLARE_ODFEXPORT_TEST(testTdf143605, "tdf143605.odt")
{
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    // With numbering type "none" there should be just prefix & suffix
    CPPUNIT_ASSERT_EQUAL(u"."_ustr,
                         getProperty<OUString>(getParagraph(1), u"ListLabelString"_ustr));
}

CPPUNIT_TEST_FIXTURE(Test, testTdf165115)
{
    // Test saving a template file with password protection
    createSwDoc();
    saveAndReload("writer8_template", "test");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf57317_autoListName)
{
    createSwDoc("tdf57317_autoListName.odt");
    // The list style (from styles.xml) overrides a duplicate named auto-style
    //uno::Any aNumStyle = getStyles("NumberingStyles")->getByName("L1");
    //CPPUNIT_ASSERT(aNumStyle.hasValue());
    uno::Reference<beans::XPropertySet> xPara(getParagraph(1), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u">1<"_ustr, getProperty<OUString>(xPara, u"ListLabelString"_ustr));
    CPPUNIT_ASSERT_EQUAL(u"L1"_ustr, getProperty<OUString>(xPara, u"NumberingStyleName"_ustr));

    dispatchCommand(mxComponent, u".uno:SelectAll"_ustr, {});
    dispatchCommand(mxComponent, u".uno:DefaultBullet"_ustr, {});

    // This was failing with a duplicate auto numbering style name of L1 instead of a unique name,
    // thus it was showing the same info as before the bullet modification.
    saveAndReload(u"writer8"_ustr);
    xPara.set(getParagraph(1), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u""_ustr, getProperty<OUString>(xPara, u"ListLabelString"_ustr));

    uno::Reference<container::XIndexAccess> xLevels(xPara->getPropertyValue(u"NumberingRules"_ustr),
                                                    uno::UNO_QUERY);
    uno::Sequence<beans::PropertyValue> aProps;
    xLevels->getByIndex(0) >>= aProps;
    for (beans::PropertyValue const& rProp : aProps)
    {
        if (rProp.Name == "BulletChar")
            return;
    }
    CPPUNIT_FAIL("no BulletChar property");
}

CPPUNIT_TEST_FIXTURE(Test, testListFormatDocx)
{
    loadAndReload("listformat.docx");
    // Ensure in resulting ODT we also have not just prefix/suffix, but custom delimiters
    CPPUNIT_ASSERT_EQUAL(u">1<"_ustr,
                         getProperty<OUString>(getParagraph(1), u"ListLabelString"_ustr));
    CPPUNIT_ASSERT_EQUAL(u">>1/1<<"_ustr,
                         getProperty<OUString>(getParagraph(2), u"ListLabelString"_ustr));
    CPPUNIT_ASSERT_EQUAL(u">>1/1/1<<"_ustr,
                         getProperty<OUString>(getParagraph(3), u"ListLabelString"_ustr));
    CPPUNIT_ASSERT_EQUAL(u">>1/1/2<<"_ustr,
                         getProperty<OUString>(getParagraph(4), u"ListLabelString"_ustr));

    // Check also that in numbering styles we have num-list-format defined
    xmlDocUniquePtr pXmlDoc = parseExport(u"styles.xml"_ustr);
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='1']",
                "num-list-format", u">%1%<");
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='2']",
                "num-list-format", u">>%1%/%2%<<");
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='3']",
                "num-list-format", u">>%1%/%2%/%3%<<");

    // But for compatibility there are still prefix/suffix
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='1']",
                "num-prefix", u">");
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='1']",
                "num-suffix", u"<");
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='2']",
                "num-prefix", u">>");
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='2']",
                "num-suffix", u"<<");
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='3']",
                "num-prefix", u">>");
    assertXPath(pXmlDoc,
                "/office:document-styles/office:styles/text:list-style[@style:name='WWNum1']/"
                "text:list-level-style-number[@text:level='3']",
                "num-suffix", u"<<");
}

CPPUNIT_TEST_FIXTURE(Test, testShapeWithHyperlink)
{
    loadAndSave("shape-with-hyperlink.odt");
    CPPUNIT_ASSERT_EQUAL(1, getShapes());
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    // Check how conversion from prefix/suffix to list format did work
    assertXPath(pXmlDoc, "/office:document-content/office:body/office:text/text:p/draw:a", "href",
                u"http://shape.com/");
}

DECLARE_ODFEXPORT_TEST(testShapesHyperlink, "shapes-hyperlink.odt")
{
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    CPPUNIT_ASSERT_EQUAL(5, getShapes());
    uno::Reference<beans::XPropertySet> const xPropSet1(getShape(1), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"http://libreoffice.org/"_ustr,
                         getProperty<OUString>(xPropSet1, u"Hyperlink"_ustr));

    uno::Reference<beans::XPropertySet> const xPropSet2(getShape(2), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"http://libreoffice2.org/"_ustr,
                         getProperty<OUString>(xPropSet2, u"Hyperlink"_ustr));

    uno::Reference<beans::XPropertySet> const xPropSet3(getShape(3), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"http://libreoffice3.org/"_ustr,
                         getProperty<OUString>(xPropSet3, u"Hyperlink"_ustr));

    uno::Reference<beans::XPropertySet> const xPropSet4(getShape(4), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"http://libreoffice4.org/"_ustr,
                         getProperty<OUString>(xPropSet4, u"Hyperlink"_ustr));

    uno::Reference<beans::XPropertySet> const xPropSet5(getShape(5), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"http://libreoffice5.org/"_ustr,
                         getProperty<OUString>(xPropSet5, u"Hyperlink"_ustr));
}

CPPUNIT_TEST_FIXTURE(Test, testListFormatOdt)
{
    auto verify = [this]() {
        CPPUNIT_ASSERT_EQUAL(1, getPages());
        // Ensure in resulting ODT we also have not just prefix/suffix, but custom delimiters
        CPPUNIT_ASSERT_EQUAL(u">1<"_ustr,
                             getProperty<OUString>(getParagraph(1), u"ListLabelString"_ustr));
        CPPUNIT_ASSERT_EQUAL(u">>1.1<<"_ustr,
                             getProperty<OUString>(getParagraph(2), u"ListLabelString"_ustr));
        CPPUNIT_ASSERT_EQUAL(u">>1.1.1<<"_ustr,
                             getProperty<OUString>(getParagraph(3), u"ListLabelString"_ustr));
        CPPUNIT_ASSERT_EQUAL(u">>1.1.2<<"_ustr,
                             getProperty<OUString>(getParagraph(4), u"ListLabelString"_ustr));
    };

    createSwDoc("listformat.odt");
    verify();
    saveAndReload(mpFilter);
    verify();

    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    // Check how conversion from prefix/suffix to list format did work
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='1']",
        "num-list-format", u">%1%<");
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='2']",
        "num-list-format", u">>%1%.%2%<<");
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='3']",
        "num-list-format", u">>%1%.%2%.%3%<<");

    // But for compatibility there are still prefix/suffix as they were before
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='1']",
        "num-prefix", u">");
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='1']",
        "num-suffix", u"<");
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='2']",
        "num-prefix", u">>");
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='2']",
        "num-suffix", u"<<");
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='3']",
        "num-prefix", u">>");
    assertXPath(
        pXmlDoc,
        "/office:document-content/office:automatic-styles/text:list-style[@style:name='L1']/"
        "text:list-level-style-number[@text:level='3']",
        "num-suffix", u"<<");
}

CPPUNIT_TEST_FIXTURE(Test, testStyleLink)
{
    // Given a document with a para and a char style that links each other, when loading that
    // document:
    createSwDoc("style-link.fodt");

    // Then make sure the char style links the para one:
    uno::Any aCharStyle
        = getStyles(u"CharacterStyles"_ustr)->getByName(u"List Paragraph Char"_ustr);
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: List Paragraph
    // - Actual  :
    // i.e. the linked style was lost on import.
    CPPUNIT_ASSERT_EQUAL(u"List Paragraph"_ustr,
                         getProperty<OUString>(aCharStyle, u"LinkStyle"_ustr));
    uno::Any aParaStyle = getStyles(u"ParagraphStyles"_ustr)->getByName(u"List Paragraph"_ustr);
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: List Paragraph Char
    // - Actual  :
    // i.e. the linked style was lost on import.
    CPPUNIT_ASSERT_EQUAL(u"List Paragraph Char"_ustr,
                         getProperty<OUString>(aParaStyle, u"LinkStyle"_ustr));
}

CPPUNIT_TEST_FIXTURE(Test, tdf120972)
{
    loadAndReload("table_number_format_3.docx");

    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    OUString cDecimal(SvtSysLocale().GetLocaleData().getNumDecimalSep()[0]);
    assertXPath(
        pXmlDoc,
        "//style:style[@style:name='P1']/style:paragraph-properties/style:tab-stops/style:tab-stop",
        "char", cDecimal);
    assertXPath(
        pXmlDoc,
        "//style:style[@style:name='P2']/style:paragraph-properties/style:tab-stops/style:tab-stop",
        "char", cDecimal);
}

DECLARE_ODFEXPORT_TEST(testTdf114287, "tdf114287.odt")
{
    uno::Reference<container::XIndexAccess> const xLevels1(
        getProperty<uno::Reference<container::XIndexAccess>>(getParagraph(2),
                                                             u"NumberingRules"_ustr));
    uno::Reference<container::XNamed> const xNum1(xLevels1, uno::UNO_QUERY);
    ::comphelper::SequenceAsHashMap props1(xLevels1->getByIndex(0));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-700), props1[u"FirstLineIndent"_ustr].get<sal_Int32>());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1330), props1[u"IndentAt"_ustr].get<sal_Int32>());

    // 1: automatic style applies list-style-name and margin-left
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-1000),
                         getProperty<sal_Int32>(getParagraph(2), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(5001),
                         getProperty<sal_Int32>(getParagraph(2), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(2), u"ParaRightMargin"_ustr));

    // list is continued
    uno::Reference<container::XNamed> const xNum2(
        getProperty<uno::Reference<container::XNamed>>(getParagraph(9), u"NumberingRules"_ustr));
    CPPUNIT_ASSERT_EQUAL(xNum1->getName(), xNum2->getName());

    // 2: style applies list-style-name and margin-left, list applies list-style-name
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-1000),
                         getProperty<sal_Int32>(getParagraph(9), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(5001),
                         getProperty<sal_Int32>(getParagraph(9), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(9), u"ParaRightMargin"_ustr));

    // list is continued
    uno::Reference<container::XNamed> const xNum3(
        getProperty<uno::Reference<container::XNamed>>(getParagraph(16), u"NumberingRules"_ustr));
    CPPUNIT_ASSERT_EQUAL(xNum1->getName(), xNum3->getName());

    // 3: style applies margin-left, automatic style applies list-style-name
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-1000),
                         getProperty<sal_Int32>(getParagraph(16), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(5001),
                         getProperty<sal_Int32>(getParagraph(16), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(16), u"ParaRightMargin"_ustr));

    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPath(pXmlDoc, "/root/page[1]/body/txt[2]/infos/prtBounds", "left", u"2268");
    assertXPath(pXmlDoc, "/root/page[1]/body/txt[2]/infos/prtBounds", "right", u"11339");
    // the list style name of the list is the same as the list style name of the
    // paragraph, but in any case the margins of the paragraph take precedence
    assertXPath(pXmlDoc, "/root/page[1]/body/txt[9]/infos/prtBounds", "left", u"2268");
    assertXPath(pXmlDoc, "/root/page[1]/body/txt[9]/infos/prtBounds", "right", u"11339");
    assertXPath(pXmlDoc, "/root/page[1]/body/txt[16]/infos/prtBounds", "left", u"357");
    assertXPath(pXmlDoc, "/root/page[1]/body/txt[16]/infos/prtBounds", "right", u"11339");
}

DECLARE_ODFEXPORT_TEST(testSectionColumnSeparator, "section-columns-separator.fodt")
{
    // tdf#150235: due to wrong types used in column export, 'style:height' and 'style:style'
    // attributes were exported incorrectly for 'style:column-sep' element
    auto xSection
        = getProperty<uno::Reference<uno::XInterface>>(getParagraph(1), u"TextSection"_ustr);
    auto xColumns = getProperty<uno::Reference<text::XTextColumns>>(xSection, u"TextColumns"_ustr);
    CPPUNIT_ASSERT(xColumns);
    CPPUNIT_ASSERT_EQUAL(sal_Int16(2), xColumns->getColumnCount());

    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 50
    // - Actual  : 100
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(50),
                         getProperty<sal_Int32>(xColumns, u"SeparatorLineRelativeHeight"_ustr));
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 2
    // - Actual  : 0
    CPPUNIT_ASSERT_EQUAL(css::text::ColumnSeparatorStyle::DOTTED,
                         getProperty<sal_Int16>(xColumns, u"SeparatorLineStyle"_ustr));

    // Check the rest of the properties, too
    CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(xColumns, u"IsAutomatic"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(600),
                         getProperty<sal_Int32>(xColumns, u"AutomaticDistance"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(9),
                         getProperty<sal_Int32>(xColumns, u"SeparatorLineWidth"_ustr));
    CPPUNIT_ASSERT_EQUAL(Color(0x99, 0xAA, 0xBB),
                         getProperty<Color>(xColumns, u"SeparatorLineColor"_ustr));
    CPPUNIT_ASSERT_EQUAL(css::style::VerticalAlignment_BOTTOM,
                         getProperty<css::style::VerticalAlignment>(
                             xColumns, u"SeparatorLineVerticalAlignment"_ustr));
    CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(xColumns, u"SeparatorLineIsOn"_ustr));
}

DECLARE_ODFEXPORT_TEST(testTdf78510, "WordTest_edit.odt")
{
    uno::Reference<container::XIndexAccess> const xLevels1(
        getProperty<uno::Reference<container::XIndexAccess>>(getParagraph(1),
                                                             u"NumberingRules"_ustr));
    ::comphelper::SequenceAsHashMap props1(xLevels1->getByIndex(0));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-1000), props1[u"FirstLineIndent"_ustr].get<sal_Int32>());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2000), props1[u"IndentAt"_ustr].get<sal_Int32>());

    // 1: inherited from paragraph style and overridden by list
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(1), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(1), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(1), u"ParaRightMargin"_ustr));
    // 2: as 1 + paragraph sets firstline
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1000),
                         getProperty<sal_Int32>(getParagraph(2), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(2), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(2), u"ParaRightMargin"_ustr));
    // 3: as 1 + paragraph sets textleft
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(3), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3000),
                         getProperty<sal_Int32>(getParagraph(3), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(3), u"ParaRightMargin"_ustr));
    // 4: as 1 + paragraph sets firstline, textleft
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-2000),
                         getProperty<sal_Int32>(getParagraph(4), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3000),
                         getProperty<sal_Int32>(getParagraph(4), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(4), u"ParaRightMargin"_ustr));
    // 5: as 1 + paragraph sets firstline
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-2000),
                         getProperty<sal_Int32>(getParagraph(5), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(5), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(5), u"ParaRightMargin"_ustr));
    // 6: as 1
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(6), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(6), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(6), u"ParaRightMargin"_ustr));

    uno::Reference<container::XIndexAccess> const xLevels8(
        getProperty<uno::Reference<container::XIndexAccess>>(getParagraph(8),
                                                             u"NumberingRules"_ustr));
    ::comphelper::SequenceAsHashMap props8(xLevels8->getByIndex(0));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1000), props8[u"FirstLineIndent"_ustr].get<sal_Int32>());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1000), props8[u"IndentAt"_ustr].get<sal_Int32>());

    // 8: inherited from paragraph style and overridden by list
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(8), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(8), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(8), u"ParaRightMargin"_ustr));
    // 9: as 8 + paragraph sets firstline
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2000),
                         getProperty<sal_Int32>(getParagraph(9), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(9), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(9), u"ParaRightMargin"_ustr));
    // 10: as 8 + paragraph sets textleft
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(10), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3000),
                         getProperty<sal_Int32>(getParagraph(10), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(10), u"ParaRightMargin"_ustr));
    // 11: as 8 + paragraph sets firstline, textleft
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-2000),
                         getProperty<sal_Int32>(getParagraph(11), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3000),
                         getProperty<sal_Int32>(getParagraph(11), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(11), u"ParaRightMargin"_ustr));
    // 12: as 8 + paragraph sets firstline
    CPPUNIT_ASSERT_EQUAL(sal_Int32(-2000),
                         getProperty<sal_Int32>(getParagraph(12), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(12), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(12), u"ParaRightMargin"_ustr));
    // 13: as 8
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(13), u"ParaFirstLineIndent"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1270),
                         getProperty<sal_Int32>(getParagraph(13), u"ParaLeftMargin"_ustr));
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0),
                         getProperty<sal_Int32>(getParagraph(13), u"ParaRightMargin"_ustr));

    // unfortunately it appears that the portions don't have a position
    // so it's not possible to check the first-line-offset that's applied
    // (the first-line-indent is computed on the fly in SwTextMargin when
    // painting)
    {
        xmlDocUniquePtr pXmlDoc = parseLayoutDump();
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[1]/infos/prtBounds", "left", u"567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[1]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[2]/infos/prtBounds", "left", u"1134");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[2]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[3]/infos/prtBounds", "left", u"1134");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[3]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[4]/infos/prtBounds", "left", u"567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[4]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[5]/infos/prtBounds", "left", u"0");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[5]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[6]/infos/prtBounds", "left", u"567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[6]/infos/prtBounds", "right", u"9359");

        assertXPath(pXmlDoc, "/root/page[1]/body/txt[8]/infos/prtBounds", "left", u"567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[8]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[9]/infos/prtBounds", "left", u"567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[9]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[10]/infos/prtBounds", "left", u"1701");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[10]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[11]/infos/prtBounds", "left", u"567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[11]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[12]/infos/prtBounds", "left", u"-567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[12]/infos/prtBounds", "right", u"9359");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[13]/infos/prtBounds", "left", u"567");
        assertXPath(pXmlDoc, "/root/page[1]/body/txt[13]/infos/prtBounds", "right", u"9359");
    }

        // now check the positions where text is actually painted -
        // wonder how fragile this is...
        // FIXME some platform difference, 1st one is 2306 on Linux, 3087 on WNT ?
        // some Mac has 3110
#if !defined(_WIN32) && !defined(MACOSX)
    {
        std::shared_ptr<GDIMetaFile> pMetaFile = getSwDocShell()->GetPreviewMetaFile();
        MetafileXmlDump aDumper;
        xmlDocUniquePtr pXmlDoc = dumpAndParse(aDumper, *pMetaFile);

        // 1: inherited from paragraph style and overridden by list
        // bullet char is extra

        assertXPath(pXmlDoc, "//textarray[1]", "x", u"2306");
        // text is after a tab from list - haven't checked if that is correct?
        assertXPath(pXmlDoc, "//textarray[2]", "x", u"2873");
        // second line
        assertXPath(pXmlDoc, "//textarray[3]", "x", u"2873");
        // 2: as 1 + paragraph sets firstline
        assertXPath(pXmlDoc, "//textarray[4]", "x", u"3440");
        assertXPath(pXmlDoc, "//textarray[5]", "x", u"3593");
        assertXPath(pXmlDoc, "//textarray[6]", "x", u"2873");
        // 3: as 1 + paragraph sets textleft
        assertXPath(pXmlDoc, "//textarray[7]", "x", u"2873");
        assertXPath(pXmlDoc, "//textarray[8]", "x", u"3440");
        assertXPath(pXmlDoc, "//textarray[9]", "x", u"3440");
        // 4: as 1 + paragraph sets firstline, textleft
        assertXPath(pXmlDoc, "//textarray[10]", "x", u"2306");
        assertXPath(pXmlDoc, "//textarray[11]", "x", u"3440");
        assertXPath(pXmlDoc, "//textarray[12]", "x", u"3440");
        // 5: as 1 + paragraph sets firstline
        assertXPath(pXmlDoc, "//textarray[13]", "x", u"1739");
        assertXPath(pXmlDoc, "//textarray[14]", "x", u"2873");
        assertXPath(pXmlDoc, "//textarray[15]", "x", u"2873");
        // 6: as 1
        assertXPath(pXmlDoc, "//textarray[16]", "x", u"2306");
        assertXPath(pXmlDoc, "//textarray[17]", "x", u"2873");

        // 8: inherited from paragraph style and overridden by list
        assertXPath(pXmlDoc, "//textarray[18]", "x", u"2873");
        assertXPath(pXmlDoc, "//textarray[19]", "x", u"3746");
        assertXPath(pXmlDoc, "//textarray[20]", "x", u"2306");
        // 9: as 8 + paragraph sets firstline
        assertXPath(pXmlDoc, "//textarray[21]", "x", u"3440");
        assertXPath(pXmlDoc, "//textarray[22]", "x", u"3746");
        assertXPath(pXmlDoc, "//textarray[23]", "x", u"2306");
        // 10: as 8 + paragraph sets textleft
        assertXPath(pXmlDoc, "//textarray[24]", "x", u"4007");
        assertXPath(pXmlDoc, "//textarray[25]", "x", u"4880");
        assertXPath(pXmlDoc, "//textarray[26]", "x", u"3440");
        // 11: as 8 + paragraph sets firstline, textleft
        assertXPath(pXmlDoc, "//textarray[27]", "x", u"2306");
        assertXPath(pXmlDoc, "//textarray[28]", "x", u"3440");
        assertXPath(pXmlDoc, "//textarray[29]", "x", u"3440");
        // 12: as 8 + paragraph sets firstline
        assertXPath(pXmlDoc, "//textarray[30]", "x", u"1172");
        assertXPath(pXmlDoc, "//textarray[31]", "x", u"1739");
        assertXPath(pXmlDoc, "//textarray[32]", "x", u"2306");
        // 13: as 8
        assertXPath(pXmlDoc, "//textarray[33]", "x", u"2873");
        assertXPath(pXmlDoc, "//textarray[34]", "x", u"3746");
    }
#endif
}

CPPUNIT_TEST_FIXTURE(Test, testParagraphMarkerMarkupRoundtrip)
{
    loadAndReload("ParagraphMarkerMarkup.fodt");
    // Test that the markup stays at save-and-reload
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    OUString autostyle = getXPath(pXmlDoc, "//office:body/office:text/text:p", "marker-style-name");
    OString style_text_properties
        = "/office:document-content/office:automatic-styles/style:style[@style:name='"
          + autostyle.toUtf8() + "']/style:text-properties";
    assertXPath(pXmlDoc, style_text_properties, "font-size", u"9pt");
    assertXPath(pXmlDoc, style_text_properties, "color", u"#ff0000");
}

CPPUNIT_TEST_FIXTURE(Test, testCommentStyles)
{
    createSwDoc();

    auto xFactory(mxComponent.queryThrow<lang::XMultiServiceFactory>());
    auto xComment(xFactory->createInstance(u"com.sun.star.text.textfield.Annotation"_ustr)
                      .queryThrow<text::XTextContent>());
    auto xCommentText(getProperty<uno::Reference<text::XTextRange>>(xComment, u"TextRange"_ustr));
    xCommentText->setString(u"Hello World"_ustr);
    xCommentText.queryThrow<beans::XPropertySet>()->setPropertyValue(u"ParaStyleName"_ustr,
                                                                     uno::Any(u"Heading"_ustr));

    xComment->attach(getParagraph(1)->getEnd());

    saveAndReload(u"writer8"_ustr);

    auto xFields(
        mxComponent.queryThrow<text::XTextFieldsSupplier>()->getTextFields()->createEnumeration());
    xComment.set(xFields->nextElement().queryThrow<text::XTextContent>());
    CPPUNIT_ASSERT(xComment.queryThrow<lang::XServiceInfo>()->supportsService(
        u"com.sun.star.text.textfield.Annotation"_ustr));

    xCommentText.set(getProperty<uno::Reference<text::XTextRange>>(xComment, u"TextRange"_ustr));
    CPPUNIT_ASSERT_EQUAL(u"Heading"_ustr,
                         getProperty<OUString>(xCommentText, u"ParaStyleName"_ustr));

    auto xStyleFamilies(
        mxComponent.queryThrow<style::XStyleFamiliesSupplier>()->getStyleFamilies());
    auto xParaStyles(xStyleFamilies->getByName(u"ParagraphStyles"_ustr));
    auto xStyle(xParaStyles.queryThrow<container::XNameAccess>()->getByName(u"Heading"_ustr));
    CPPUNIT_ASSERT_EQUAL(getProperty<float>(xStyle, u"CharHeight"_ustr),
                         getProperty<float>(xCommentText, u"CharHeight"_ustr));
    CPPUNIT_ASSERT_EQUAL(
        beans::PropertyState_DEFAULT_VALUE,
        xCommentText.queryThrow<beans::XPropertyState>()->getPropertyState(u"CharHeight"_ustr));
}

CPPUNIT_TEST_FIXTURE(Test, testTdf150408_IsLegal)
{
    loadAndReload("IsLegal.fodt");

    // Second level's numbering should use Arabic numbers for first level reference
    auto xPara = getParagraph(1);
    CPPUNIT_ASSERT_EQUAL(u"CH I"_ustr, getProperty<OUString>(xPara, u"ListLabelString"_ustr));
    xPara = getParagraph(2);
    CPPUNIT_ASSERT_EQUAL(u"Sect 1.01"_ustr, getProperty<OUString>(xPara, u"ListLabelString"_ustr));
    xPara = getParagraph(3);
    CPPUNIT_ASSERT_EQUAL(u"CH II"_ustr, getProperty<OUString>(xPara, u"ListLabelString"_ustr));
    xPara = getParagraph(4);
    CPPUNIT_ASSERT_EQUAL(u"Sect 2.01"_ustr, getProperty<OUString>(xPara, u"ListLabelString"_ustr));

    // Test that the markup stays at save-and-reload
    xmlDocUniquePtr pXmlDoc = parseExport(u"styles.xml"_ustr);
    assertXPath(
        pXmlDoc,
        "/office:document-styles/office:styles/text:outline-style/text:outline-level-style[2]",
        "is-legal", u"true");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf159382)
{
    // Testing NoGapAfterNoteNumber compat option

    createSwDoc("footnote_spacing_hanging_para.docx");
    // 1. Make sure that DOCX import sets NoGapAfterNoteNumber option, and creates
    // correct layout
    {
        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(true),
                             xSettings->getPropertyValue(u"NoGapAfterNoteNumber"_ustr));

        xmlDocUniquePtr pXmlDoc = parseLayoutDump();
        sal_Int32 width
            = getXPath(pXmlDoc,
                       "/root/page/ftncont/ftn/txt/SwParaPortion/SwLineLayout/SwFieldPortion",
                       "width")
                  .toInt32();
        CPPUNIT_ASSERT(width);
        CPPUNIT_ASSERT_LESS(sal_Int32(100), width); // It was 720, i.e. 0.5 inch
    }

    saveAndReload(mpFilter);
    // 2. Make sure that exported document has NoGapAfterNoteNumber option set,
    // and has correct layout
    {
        xmlDocUniquePtr pXmlDoc = parseExport(u"settings.xml"_ustr);
        assertXPathContent(pXmlDoc, "//config:config-item[@config:name='NoGapAfterNoteNumber']",
                           u"true");

        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(true),
                             xSettings->getPropertyValue(u"NoGapAfterNoteNumber"_ustr));

        pXmlDoc = parseLayoutDump();
        sal_Int32 width
            = getXPath(pXmlDoc,
                       "/root/page/ftncont/ftn/txt/SwParaPortion/SwLineLayout/SwFieldPortion",
                       "width")
                  .toInt32();
        CPPUNIT_ASSERT(width);
        CPPUNIT_ASSERT_LESS(sal_Int32(100), width);
    }

    createSwDoc("footnote_spacing_hanging_para.doc");
    // 3. Make sure that DOC import sets NoGapAfterNoteNumber option, and creates
    // correct layout
    {
        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(true),
                             xSettings->getPropertyValue(u"NoGapAfterNoteNumber"_ustr));

        xmlDocUniquePtr pXmlDoc = parseLayoutDump();
        sal_Int32 width
            = getXPath(pXmlDoc,
                       "/root/page/ftncont/ftn/txt/SwParaPortion/SwLineLayout/SwFieldPortion",
                       "width")
                  .toInt32();
        CPPUNIT_ASSERT(width);
        CPPUNIT_ASSERT_LESS(sal_Int32(100), width);
    }

    createSwDoc("footnote_spacing_hanging_para.rtf");
    // 4. Make sure that RTF import sets NoGapAfterNoteNumber option, and creates
    // correct layout
    {
        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(true),
                             xSettings->getPropertyValue(u"NoGapAfterNoteNumber"_ustr));

        xmlDocUniquePtr pXmlDoc = parseLayoutDump();
        sal_Int32 width
            = getXPath(pXmlDoc,
                       "/root/page/ftncont/ftn/txt/SwParaPortion/SwLineLayout/SwFieldPortion",
                       "width")
                  .toInt32();
        CPPUNIT_ASSERT(width);
        CPPUNIT_ASSERT_LESS(sal_Int32(100), width);
    }

    createSwDoc();
    // 5. Make sure that a new Writer document has this setting set to false
    {
        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(false),
                             xSettings->getPropertyValue(u"NoGapAfterNoteNumber"_ustr));
    }
}

CPPUNIT_TEST_FIXTURE(Test, testTdf159438)
{
    // Given a text with bookmarks, where an end of one bookmark is the position of another,
    // and the start of a third
    loadAndReload("bookmark_order.fodt");
    auto xPara = getParagraph(1);

    // Check that the order of runs is correct (bookmarks don't overlap)

    {
        auto run = getRun(xPara, 1);
        CPPUNIT_ASSERT_EQUAL(u"Bookmark"_ustr, getProperty<OUString>(run, u"TextPortionType"_ustr));
        CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(run, u"IsStart"_ustr));
        CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(run, u"IsCollapsed"_ustr));
        auto named = getProperty<uno::Reference<container::XNamed>>(run, u"Bookmark"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"bookmark1"_ustr, named->getName());
    }

    {
        auto run = getRun(xPara, 2);
        CPPUNIT_ASSERT_EQUAL(u"Text"_ustr, getProperty<OUString>(run, u"TextPortionType"_ustr));
        CPPUNIT_ASSERT_EQUAL(u"foo"_ustr, run->getString());
    }

    {
        auto run = getRun(xPara, 3);
        CPPUNIT_ASSERT_EQUAL(u"Bookmark"_ustr, getProperty<OUString>(run, u"TextPortionType"_ustr));
        CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(run, u"IsStart"_ustr));
        CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(run, u"IsCollapsed"_ustr));
        auto named = getProperty<uno::Reference<container::XNamed>>(run, u"Bookmark"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"bookmark1"_ustr, named->getName());
    }

    {
        auto run = getRun(xPara, 4);
        CPPUNIT_ASSERT_EQUAL(u"Bookmark"_ustr, getProperty<OUString>(run, u"TextPortionType"_ustr));
        CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(run, u"IsStart"_ustr));
        CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(run, u"IsCollapsed"_ustr));
        auto named = getProperty<uno::Reference<container::XNamed>>(run, u"Bookmark"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"bookmark2"_ustr, named->getName());
    }

    {
        auto run = getRun(xPara, 5);
        CPPUNIT_ASSERT_EQUAL(u"Bookmark"_ustr, getProperty<OUString>(run, u"TextPortionType"_ustr));
        CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(run, u"IsStart"_ustr));
        CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(run, u"IsCollapsed"_ustr));
        auto named = getProperty<uno::Reference<container::XNamed>>(run, u"Bookmark"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"bookmark3"_ustr, named->getName());
    }

    {
        auto run = getRun(xPara, 6);
        CPPUNIT_ASSERT_EQUAL(u"Text"_ustr, getProperty<OUString>(run, u"TextPortionType"_ustr));
        CPPUNIT_ASSERT_EQUAL(u"bar"_ustr, run->getString());
    }

    {
        auto run = getRun(xPara, 7);
        CPPUNIT_ASSERT_EQUAL(u"Bookmark"_ustr, getProperty<OUString>(run, u"TextPortionType"_ustr));
        CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(run, u"IsStart"_ustr));
        CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(run, u"IsCollapsed"_ustr));
        auto named = getProperty<uno::Reference<container::XNamed>>(run, u"Bookmark"_ustr);
        CPPUNIT_ASSERT_EQUAL(u"bookmark3"_ustr, named->getName());
    }

    // Test that the markup stays at save-and-reload
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);

    assertXPathNodeName(pXmlDoc, "//office:body/office:text/text:p/*[1]", "bookmark-start");
    assertXPath(pXmlDoc, "//office:body/office:text/text:p/*[1]", "name", u"bookmark1");

    // Without the fix in place, this would fail with
    // - Expected: bookmark-end
    // - Actual  : bookmark-start
    // - In XPath '//office:body/office:text/text:p/*[2]' name of node is incorrect
    assertXPathNodeName(pXmlDoc, "//office:body/office:text/text:p/*[2]", "bookmark-end");
    assertXPath(pXmlDoc, "//office:body/office:text/text:p/*[2]", "name", u"bookmark1");

    assertXPathNodeName(pXmlDoc, "//office:body/office:text/text:p/*[3]", "bookmark");
    assertXPath(pXmlDoc, "//office:body/office:text/text:p/*[3]", "name", u"bookmark2");

    assertXPathNodeName(pXmlDoc, "//office:body/office:text/text:p/*[4]", "bookmark-start");
    assertXPath(pXmlDoc, "//office:body/office:text/text:p/*[4]", "name", u"bookmark3");

    assertXPathNodeName(pXmlDoc, "//office:body/office:text/text:p/*[5]", "bookmark-end");
    assertXPath(pXmlDoc, "//office:body/office:text/text:p/*[5]", "name", u"bookmark3");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf160700)
{
    // Given a document with an empty numbered paragraph, and a cross-reference to it
    loadAndReload("tdf160700.odt");

    // Refresh fields and ensure cross-reference to numbered para is okay
    auto xTextFieldsSupplier(mxComponent.queryThrow<text::XTextFieldsSupplier>());
    auto xFieldsAccess(xTextFieldsSupplier->getTextFields());

    xFieldsAccess.queryThrow<util::XRefreshable>()->refresh();

    auto xFields(xFieldsAccess->createEnumeration());
    CPPUNIT_ASSERT(xFields->hasMoreElements());
    auto xTextField(xFields->nextElement().queryThrow<text::XTextField>());
    // Save must not create markup with text:bookmark-end element before text:bookmark-start
    // Without the fix, this would fail with
    // - Expected: 1
    // - Actual  : Error: Reference source not found
    // i.e., the bookmark wasn't imported, and the field had no proper source
    CPPUNIT_ASSERT_EQUAL(u"1"_ustr, xTextField->getPresentation(false));

    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    // Check that we export the bookmark in the empty paragraph as a single text:bookmark
    // element. Another valid markup is text:bookmark-start followed by text:bookmark-end
    // (in that order). The problem was, that text:bookmark-end was before text:bookmark-start.
    assertXPathChildren(pXmlDoc, "//office:text/text:list/text:list-item/text:p", 1);
    assertXPath(pXmlDoc, "//office:text/text:list/text:list-item/text:p/text:bookmark");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf160253_ordinary_numbering)
{
    // Given a document with a list, and an out-of-the-list paragraph in the middle, having an
    // endnote, which has a paragraph in another list.
    // Before the fix, this already failed with
    //   Error: "list2916587379" is referenced by an IDREF, but not defined.
    loadAndReload("tdf160253_ordinary_numbering.fodt");

    // Make sure that the fourth paragraph has correct number - it was "1." before the fix
    CPPUNIT_ASSERT_EQUAL(u"3."_ustr,
                         getProperty<OUString>(getParagraph(4), u"ListLabelString"_ustr));

    // Make sure that we emit an identifier for the first list, and refer to it in the continuation
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    // This failed before the fix, because 'xml:id' attribute wasn't emitted
    OUString firstListId = getXPath(pXmlDoc, "//office:body/office:text/text:list[1]", "id");
    CPPUNIT_ASSERT(!firstListId.isEmpty());
    assertXPath(pXmlDoc, "//office:body/office:text/text:list[2]", "continue-list", firstListId);
}

CPPUNIT_TEST_FIXTURE(Test, testTdf160253_outline_numbering)
{
    // Given a document with an outline (chapter) numbering, and a paragraph in the middle, having
    // an endnote, which has a paragraph in a list.
    // Before the fix, this already failed with
    //   Error: "list2916587379" is referenced by an IDREF, but not defined.
    loadAndReload("tdf160253_outline_numbering.fodt");

    // Make sure that the third paragraph has correct number - it was "1" before the fix
    CPPUNIT_ASSERT_EQUAL(u"2"_ustr,
                         getProperty<OUString>(getParagraph(3), u"ListLabelString"_ustr));

    // The difference with the ordinary numbering is that for outline numbering, the list element
    // isn't really necessary. It is a TODO to fix the output, and not export the list.
    // xmlDocUniquePtr pXmlDoc = parseExport("content.xml");
    // assertXPath(pXmlDoc, "//office:body/office:text/text:list", 0);
}

CPPUNIT_TEST_FIXTURE(Test, testTableInFrameAnchoredToPage)
{
    // Given a table in a frame anchored to a page:
    // it must not assert on export because of missing format for an exported table
    loadAndReload("table_in_frame_to_page.fodt");

    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    auto AutoStyleUsedIn = [this, &pXmlDoc](const OString& path, const char* attr) -> OString {
        const OUString styleName = getXPath(pXmlDoc, path, attr);
        return "//office:automatic-styles/style:style[@style:name='" + styleName.toUtf8() + "']";
    };
    static constexpr OString xPathTextBox
        = "//office:body/office:text/draw:frame/draw:text-box"_ostr;

    // Check also, that autostyles defined inside that frame are stored correctly. If not, then
    // these paragraphs would refer to styles in <office::styles>, not in <office:automatic-styles>,
    // without the 'italic' and 'bold' attributes.
    OString P = AutoStyleUsedIn(xPathTextBox + "/text:p", "style-name");
    assertXPath(pXmlDoc, P + "/style:text-properties", "font-weight", u"bold");

    P = AutoStyleUsedIn(xPathTextBox + "/table:table/table:table-row[1]/table:table-cell[1]/text:p",
                        "style-name");
    assertXPath(pXmlDoc, P + "/style:text-properties", "font-style", u"italic");
}

CPPUNIT_TEST_FIXTURE(Test, testDeletedTableAutostylesExport)
{
    // Given a document with deleted table:
    // it must not assert on export because of missing format for an exported table
    loadAndReload("deleted_table.fodt");
}

DECLARE_ODFEXPORT_TEST(testTdf160877, "tdf160877.odt")
{
    CPPUNIT_ASSERT_EQUAL(1, getPages());

    uno::Reference<text::XText> xHeaderTextPage1 = getProperty<uno::Reference<text::XText>>(
        getStyles(u"PageStyles"_ustr)->getByName(u"Standard"_ustr), u"HeaderTextFirst"_ustr);
    CPPUNIT_ASSERT_EQUAL(u"Classification: General Business"_ustr, xHeaderTextPage1->getString());

    // Without the fix in place, this test would have failed with
    // - Expected: (Sign GB)Test
    // - Actual  : Test
    CPPUNIT_ASSERT_EQUAL(u"(Sign GB)Test"_ustr, getParagraph(1)->getString());
}

CPPUNIT_TEST_FIXTURE(Test, testMidnightRedlineDatetime)
{
    // Given a document with a tracked change with a midnight datetime:
    // make sure that it succeeds export and import validation. Before the fix, this failed:
    // - Error: "2001-01-01" does not satisfy the "dateTime" type
    // because "2001-01-01T00:00:00" became "2001-01-01" on roundtrip.
    loadAndReload("midnight_redline.fodt");

    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    assertXPathContent(pXmlDoc,
                       "//office:body/office:text/text:tracked-changes/text:changed-region/"
                       "text:deletion/office:change-info/dc:date",
                       u"2001-01-01T00:00:00");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf122452)
{
    // FIXME:  Error: element "text:insertion" was found where no element may occur
    skipValidation();
    loadAndReload("tdf122452.doc");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();

    // Without the fix in place this fails with:
    // Expected: 1
    // Actual:   0
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Redlines should be Hidden", true,
                                 pWrtShell->GetLayout()->IsHideRedlines());
}

CPPUNIT_TEST_FIXTURE(Test, testTdf159027)
{
    loadAndReload("tdf159027.odt");
    SwDoc* pDoc = getSwDoc();
    pDoc->getIDocumentFieldsAccess().UpdateFields(true);

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xTables(xTablesSupplier->getTextTables(),
                                                    uno::UNO_QUERY);
    uno::Reference<text::XTextTable> xTextTable(xTables->getByIndex(0), uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xCellD9(xTextTable->getCellByName(u"D9"_ustr), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"70"_ustr, xCellD9->getString());
    uno::Reference<text::XTextRange> xCellE9(xTextTable->getCellByName(u"E9"_ustr), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"6"_ustr, xCellE9->getString());
}

CPPUNIT_TEST_FIXTURE(Test, testTdf121119)
{
    createSwGlobalDoc("tdf121119.odm");
    SwDoc* pDoc = getSwDoc();
    CPPUNIT_ASSERT_EQUAL(
        size_t(2), pDoc->getIDocumentLinksAdministration().GetLinkManager().GetLinks().size());
    pDoc->getIDocumentLinksAdministration().GetLinkManager().UpdateAllLinks(false, false, nullptr,
                                                                            u""_ustr);

    uno::Reference<text::XTextGraphicObjectsSupplier> xTextGraphicObjectsSupplier(mxComponent,
                                                                                  uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xIndexAccess(
        xTextGraphicObjectsSupplier->getGraphicObjects(), uno::UNO_QUERY);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xIndexAccess->getCount());

    saveAndReload(u"writerglobal8_writer"_ustr);
    pDoc = getSwDoc();

    CPPUNIT_ASSERT_EQUAL(
        size_t(2), pDoc->getIDocumentLinksAdministration().GetLinkManager().GetLinks().size());
    pDoc->getIDocumentLinksAdministration().GetLinkManager().UpdateAllLinks(false, false, nullptr,
                                                                            u""_ustr);

    uno::Reference<text::XTextGraphicObjectsSupplier> xTextGraphicObjectsSupplier2(mxComponent,
                                                                                   uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xIndexAccess2(
        xTextGraphicObjectsSupplier2->getGraphicObjects(), uno::UNO_QUERY);

    // This was 8 (duplicated images anchored at page)
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xIndexAccess2->getCount());
}

CPPUNIT_TEST_FIXTURE(Test, testTdf121119_runtime_update)
{
    createSwGlobalDoc("tdf121119.odm");
    SwDoc* pDoc = getSwDoc();
    CPPUNIT_ASSERT_EQUAL(
        size_t(2), pDoc->getIDocumentLinksAdministration().GetLinkManager().GetLinks().size());
    pDoc->getIDocumentLinksAdministration().GetLinkManager().UpdateAllLinks(false, false, nullptr,
                                                                            u""_ustr);
    // double update of the links
    pDoc->getIDocumentLinksAdministration().GetLinkManager().UpdateAllLinks(false, false, nullptr,
                                                                            u""_ustr);

    uno::Reference<text::XTextGraphicObjectsSupplier> xTextGraphicObjectsSupplier(mxComponent,
                                                                                  uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xIndexAccess(
        xTextGraphicObjectsSupplier->getGraphicObjects(), uno::UNO_QUERY);

    // This was 8 (duplicated images anchored at page)
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xIndexAccess->getCount());
}

CPPUNIT_TEST_FIXTURE(Test, testTdf163703)
{
    // Given a document with italics autostyle in a comment
    loadAndReload("italics-in-comment.fodt");

    auto xFields(
        mxComponent.queryThrow<text::XTextFieldsSupplier>()->getTextFields()->createEnumeration());
    auto xComment(xFields->nextElement().queryThrow<text::XTextContent>());
    CPPUNIT_ASSERT(xComment.queryThrow<lang::XServiceInfo>()->supportsService(
        u"com.sun.star.text.textfield.Annotation"_ustr));

    auto xCommentText(getProperty<uno::Reference<css::text::XText>>(xComment, u"TextRange"_ustr));
    CPPUNIT_ASSERT(xCommentText);
    CPPUNIT_ASSERT_EQUAL(1, getParagraphs(xCommentText));
    auto xCommentPara(getParagraphOrTable(1, xCommentText).queryThrow<css::text::XTextRange>());
    CPPUNIT_ASSERT_EQUAL(u"lorem"_ustr, xCommentPara->getString());

    // Without the fix, this would fail with
    // - Expected: lo
    // - Actual  : lorem
    // - run does not contain expected content
    // because direct formatting was dropped on export, and the comment was exported in one chunk
    auto x1stRun = getRun(xCommentPara, 1, "lo");
    CPPUNIT_ASSERT_EQUAL(css::awt::FontSlant_NONE,
                         getProperty<css::awt::FontSlant>(x1stRun, u"CharPosture"_ustr));

    auto x2ndRun = getRun(xCommentPara, 2, "r");
    CPPUNIT_ASSERT_EQUAL(css::awt::FontSlant_ITALIC,
                         getProperty<css::awt::FontSlant>(x2ndRun, u"CharPosture"_ustr));

    auto x3rdRun = getRun(xCommentPara, 3, "em");
    CPPUNIT_ASSERT_EQUAL(css::awt::FontSlant_NONE,
                         getProperty<css::awt::FontSlant>(x3rdRun, u"CharPosture"_ustr));

    xmlDocUniquePtr pXml = parseExport(u"content.xml"_ustr);
    assertXPathContent(pXml, "//office:text/text:p/office:annotation/text:p", u"lorem");
    // Without the fix, this would fail with
    // - Expected: 1
    // - Actual  : 0
    assertXPathChildren(pXml, "//office:text/text:p/office:annotation/text:p", 1);
    assertXPathContent(pXml, "//office:text/text:p/office:annotation/text:p/text:span", u"r");
    auto autostylename
        = getXPath(pXml, "//office:text/text:p/office:annotation/text:p/text:span", "style-name");
    OString autoStyleXPath = "//office:automatic-styles/style:style[@style:name='"
                             + autostylename.toUtf8() + "']/style:text-properties";
    assertXPath(pXml, autoStyleXPath, "font-style", u"italic");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf36709)
{
    // Verifies that loext:text-indent correctly round-trips
    loadAndReload("tdf36709.fodt");
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);

    // Style P1 should have been rewritten as fo:text-indent
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P1']/style:paragraph-properties[@fo:text-indent]", 1);
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P1']/style:paragraph-properties[@loext:text-indent]",
                0);
    assertXPath(pXmlDoc, "//style:style[@style:name='P1']/style:paragraph-properties",
                "text-indent", u"3in");

    // Style P2 should have round-tripped as loext:text-indent
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P2']/style:paragraph-properties[@fo:text-indent]", 0);
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P2']/style:paragraph-properties[@loext:text-indent]",
                1);
    assertXPath(pXmlDoc, "//style:style[@style:name='P2']/style:paragraph-properties",
                "text-indent", u"6em");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf163913)
{
    // Verifies that loext:left-margin and loext:right-margin correctly round-trip
    loadAndReload("tdf163913.fodt");
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);

    // Style P1 should have been rewritten as fo:margin-left
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P1']/style:paragraph-properties[@fo:margin-left]", 1);
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P1']/style:paragraph-properties[@loext:margin-left]",
                0);
    assertXPath(pXmlDoc, "//style:style[@style:name='P1']/style:paragraph-properties",
                "margin-left", u"3in");

    // Style P2 should have round-tripped as loext:margin-left
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P2']/style:paragraph-properties[@fo:margin-left]", 0);
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P2']/style:paragraph-properties[@loext:margin-left]",
                1);
    assertXPath(pXmlDoc, "//style:style[@style:name='P2']/style:paragraph-properties",
                "margin-left", u"6em");

    // Style P3 should have been rewritten as fo:margin-right
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P3']/style:paragraph-properties[@fo:margin-right]", 1);
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P3']/style:paragraph-properties[@loext:margin-right]",
                0);
    assertXPath(pXmlDoc, "//style:style[@style:name='P3']/style:paragraph-properties",
                "margin-right", u"3in");

    // Style P4 should have round-tripped as loext:margin-right
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P4']/style:paragraph-properties[@fo:margin-right]", 0);
    assertXPath(pXmlDoc,
                "//style:style[@style:name='P4']/style:paragraph-properties[@loext:margin-right]",
                1);
    assertXPath(pXmlDoc, "//style:style[@style:name='P4']/style:paragraph-properties",
                "margin-right", u"6em");
}

CPPUNIT_TEST_FIXTURE(Test, testMsWordUlTrailSpace)
{
    // Testing MsWordUlTrailSpace compat option

    // Given a document with both MsWordCompTrailingBlanks and MsWordUlTrailSpace set
    createSwDoc("UnderlineTrailingSpace.fodt");
    // 1. Make sure that the import sets MsWordUlTrailSpace option, and creates correct layout
    {
        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(true),
                             xSettings->getPropertyValue(u"MsWordUlTrailSpace"_ustr));

        xmlDocUniquePtr pXmlDoc = parseLayoutDump();
        OUString val;
        // Line 1: one SwHolePortion, showing underline
        val = getXPath(pXmlDoc, "//body/txt[1]/SwParaPortion/SwLineLayout/SwHolePortion", "length");
        CPPUNIT_ASSERT_GREATEREQUAL(sal_Int32(69), val.toInt32()); // In truth, it should be 70
        val = getXPath(pXmlDoc, "//body/txt[1]/SwParaPortion/SwLineLayout/SwHolePortion",
                       "show-underline");
        CPPUNIT_ASSERT_EQUAL(u"true"_ustr, val);
        // TODO: Line 2
        // Line 3: two SwHolePortion, one for shown underline, one for the rest
        val = getXPath(pXmlDoc, "//body/txt[3]/SwParaPortion/SwLineLayout/SwHolePortion[1]",
                       "length");
        CPPUNIT_ASSERT_GREATEREQUAL(sal_Int32(140), val.toInt32());
        val = getXPath(pXmlDoc, "//body/txt[3]/SwParaPortion/SwLineLayout/SwHolePortion[1]",
                       "show-underline");
        CPPUNIT_ASSERT_EQUAL(u"true"_ustr, val);
        val = getXPath(pXmlDoc, "//body/txt[3]/SwParaPortion/SwLineLayout/SwHolePortion[2]",
                       "length");
        CPPUNIT_ASSERT_GREATEREQUAL(sal_Int32(850), val.toInt32());
        val = getXPath(pXmlDoc, "//body/txt[3]/SwParaPortion/SwLineLayout/SwHolePortion[2]",
                       "show-underline");
        CPPUNIT_ASSERT_EQUAL(u"false"_ustr, val);
    }

    saveAndReload(mpFilter);
    // 2. Make sure that exported document has MsWordUlTrailSpace option set
    {
        xmlDocUniquePtr pXmlDoc = parseExport(u"settings.xml"_ustr);
        assertXPathContent(pXmlDoc, "//config:config-item[@config:name='MsWordUlTrailSpace']",
                           u"true");

        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(true),
                             xSettings->getPropertyValue(u"MsWordUlTrailSpace"_ustr));
    }

    // 3. Disable the option, and check the layout
    {
        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        xSettings->setPropertyValue(u"MsWordUlTrailSpace"_ustr, uno::Any(false));
        CPPUNIT_ASSERT_EQUAL(uno::Any(false),
                             xSettings->getPropertyValue(u"MsWordUlTrailSpace"_ustr));

        getSwDoc()->getIDocumentLayoutAccess().GetCurrentViewShell()->Reformat();
        xmlDocUniquePtr pXmlDoc = parseLayoutDump();
        OUString val;
        // Line 1: one SwHolePortion, not showing underline
        val = getXPath(pXmlDoc, "//body/txt[1]/SwParaPortion/SwLineLayout/SwHolePortion", "length");
        CPPUNIT_ASSERT_GREATEREQUAL(sal_Int32(69), val.toInt32()); // In truth, it should be 70
        val = getXPath(pXmlDoc, "//body/txt[1]/SwParaPortion/SwLineLayout/SwHolePortion",
                       "show-underline");
        CPPUNIT_ASSERT_EQUAL(u"false"_ustr, val);
        // TODO: Line 2
        // Line 3: one SwHolePortion, not showing underline
        val = getXPath(pXmlDoc, "//body/txt[3]/SwParaPortion/SwLineLayout/SwHolePortion", "length");
        CPPUNIT_ASSERT_GREATEREQUAL(sal_Int32(999), val.toInt32());
        val = getXPath(pXmlDoc, "//body/txt[3]/SwParaPortion/SwLineLayout/SwHolePortion",
                       "show-underline");
        CPPUNIT_ASSERT_EQUAL(u"false"_ustr, val);
    }

    saveAndReload(mpFilter);
    // 4. Make sure that exported document has MsWordUlTrailSpace option not set
    {
        xmlDocUniquePtr pXmlDoc = parseExport(u"settings.xml"_ustr);
        assertXPathContent(pXmlDoc, "//config:config-item[@config:name='MsWordUlTrailSpace']",
                           u"false");

        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(false),
                             xSettings->getPropertyValue(u"MsWordUlTrailSpace"_ustr));
    }

    createSwDoc();
    // 5. Make sure that a new Writer document has this setting set to false
    {
        uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
        uno::Reference<beans::XPropertySet> xSettings(
            xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);
        CPPUNIT_ASSERT_EQUAL(uno::Any(false),
                             xSettings->getPropertyValue(u"MsWordUlTrailSpace"_ustr));
    }
}

CPPUNIT_TEST_FIXTURE(Test, testTdf71583)
{
    // Verifies that loext:text-indent correctly round-trips
    loadAndReload("tdf71583.odt");
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
    assertXPathNodeName(pXmlDoc, "//office:body/office:text/text:p/*[1]", "page-count-range");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf166011)
{
    // Verifies that style:script-type correctly round-trips
    loadAndReload("tdf166011.fodt");
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);

    assertXPath(pXmlDoc, "//style:style[@style:name='P1']/style:text-properties", "script-type",
                u"complex");
    assertXPath(pXmlDoc, "//style:style[@style:name='T1']/style:text-properties", "script-type",
                u"latin");
}

CPPUNIT_TEST_FIXTURE(Test, testTdf166011ee)
{
    // Verifies that style:script-type correctly round-trips with Edit Engine
    loadAndReload("tdf166011ee.fodt");
    CPPUNIT_ASSERT_EQUAL(1, getPages());
    xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);

    assertXPath(pXmlDoc, "//style:style[@style:name='T1']/style:text-properties", "script-type",
                u"complex");
    assertXPath(pXmlDoc, "//style:style[@style:name='T2']/style:text-properties", "script-type",
                u"latin");
}

} // end of anonymous namespace
CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
