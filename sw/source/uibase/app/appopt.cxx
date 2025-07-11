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
#include <cmdid.h>

#include <com/sun/star/i18n/ScriptType.hpp>

#include <sal/log.hxx>
#include <hintids.hxx>
#include <svl/eitem.hxx>
#include <sfx2/app.hxx>
#include <sfx2/printer.hxx>
#include <sfx2/htmlmode.hxx>
#include <sfx2/bindings.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/tstpitem.hxx>
#include <svx/optgrid.hxx>
#include <svx/dialogs.hrc>
#include <tools/UnitConversion.hxx>
#include <i18nlangtag/mslangid.hxx>
#include <i18nlangtag/languagetag.hxx>
#include <fontcfg.hxx>
#include <swmodule.hxx>
#include <view.hxx>
#include <doc.hxx>
#include <wrtsh.hxx>
#include <IDocumentDeviceAccess.hxx>
#include <IDocumentSettingAccess.hxx>
#include <uitool.hxx>
#include <wview.hxx>
#include <cfgitems.hxx>
#include <prtopt.hxx>
#include <pview.hxx>
#include <usrpref.hxx>
#include <uiitems.hxx>
#include <editeng/langitem.hxx>
#include <unotools/lingucfg.hxx>
#include <globals.hrc>
#include <swabstdlg.hxx>
#include <swwrtshitem.hxx>

#include <sfx2/dispatch.hxx>

using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;

std::optional<SfxItemSet> SwModule::CreateItemSet( sal_uInt16 nId )
{
    bool bTextDialog = (nId == SID_SW_EDITOPTIONS);

    // the options for the Web- and Textdialog are put together here
    SwViewOption aViewOpt = *GetUsrPref(!bTextDialog);
    SwMasterUsrPref* pPref = bTextDialog ? m_pUsrPref.get() : m_pWebUsrPref.get();
    // no MakeUsrPref, because only options from textdoks can be used here
    SwView* pAppView = GetView();
    if(pAppView && &pAppView->GetViewFrame() != SfxViewFrame::Current())
        pAppView = nullptr;
    if(pAppView)
    {
        bool bWebView = dynamic_cast<SwWebView*>( pAppView ) !=  nullptr;
        // if Text then no WebView and vice versa
        if (bWebView != bTextDialog)
        {
            aViewOpt = *pAppView->GetWrtShell().GetViewOptions();
        }
        else
            pAppView = nullptr; // with View, there's nothing to win here
    }

    // Options/Edit
    SfxItemSetFixed<
            RES_BACKGROUND, RES_BACKGROUND,
            XATTR_FILL_FIRST, XATTR_FILL_LAST,
            SID_PRINTPREVIEW, SID_PRINTPREVIEW,
            SID_ATTR_GRID_OPTIONS, SID_ATTR_GRID_OPTIONS,
            SID_HTML_MODE, SID_HTML_MODE,
            SID_ATTR_CHAR_CJK_LANGUAGE, SID_ATTR_CHAR_CJK_LANGUAGE,
            SID_ATTR_CHAR_CTL_LANGUAGE, SID_ATTR_CHAR_CTL_LANGUAGE,
            SID_ATTR_LANGUAGE, SID_ATTR_METRIC,
            SID_ATTR_DEFTABSTOP, SID_ATTR_DEFTABSTOP,
            SID_ATTR_APPLYCHARUNIT, SID_ATTR_APPLYCHARUNIT,
            FN_HSCROLL_METRIC, FN_VSCROLL_METRIC,
            FN_PARAM_ADDPRINTER, FN_PARAM_ADDPRINTER,
            FN_PARAM_DOCDISP, FN_PARAM_ELEM,
            FN_PARAM_PRINTER, FN_PARAM_STDFONTS,
            FN_PARAM_WRTSHELL, FN_PARAM_WRTSHELL,
            FN_PARAM_SHADOWCURSOR, FN_PARAM_SHADOWCURSOR,
            FN_PARAM_CRSR_IN_PROTECTED, FN_PARAM_CRSR_IN_PROTECTED,
            FN_PARAM_FMT_AIDS_AUTOCOMPL, FN_PARAM_FMT_AIDS_AUTOCOMPL>
        aRet(GetPool());

    aRet.Put( SwDocDisplayItem( aViewOpt ) );
    SwElemItem aElemItem( aViewOpt );
    if( bTextDialog )
    {
        aRet.Put( SwShadowCursorItem( aViewOpt ));
        aRet.Put( SfxBoolItem(FN_PARAM_CRSR_IN_PROTECTED, aViewOpt.IsCursorInProtectedArea()));
        aRet.Put(SwFmtAidsAutoComplItem(aViewOpt));
        aElemItem.SetDefaultZoom(pPref->IsDefaultZoom());
        aElemItem.SetDefaultZoomType(pPref->GetDefaultZoomType());
        aElemItem.SetDefaultZoomValue(pPref->GetDefaultZoomValue());
    }
    aRet.Put( aElemItem );

    if( pAppView )
    {
        SwWrtShell& rWrtShell = pAppView->GetWrtShell();

        SfxPrinter* pPrt = rWrtShell.getIDocumentDeviceAccess().getPrinter( false );
        if( pPrt )
            aRet.Put(SwPtrItem(FN_PARAM_PRINTER, pPrt));
        aRet.Put(SwPtrItem(FN_PARAM_WRTSHELL, &rWrtShell));

        aRet.Put(rWrtShell.GetDefault(RES_CHRATR_LANGUAGE).CloneSetWhich(SID_ATTR_LANGUAGE));
        aRet.Put(rWrtShell.GetDefault(RES_CHRATR_CJK_LANGUAGE).CloneSetWhich(SID_ATTR_CHAR_CJK_LANGUAGE));
        aRet.Put(rWrtShell.GetDefault(RES_CHRATR_CTL_LANGUAGE).CloneSetWhich(SID_ATTR_CHAR_CTL_LANGUAGE));
    }
    else
    {
        SvtLinguConfig aLinguCfg;
        css::lang::Locale aLocale;
        LanguageType nLang;

        using namespace ::com::sun::star::i18n::ScriptType;

        Any aLang = aLinguCfg.GetProperty(u"DefaultLocale");
        aLang >>= aLocale;
        nLang = MsLangId::resolveSystemLanguageByScriptType(LanguageTag::convertToLanguageType( aLocale, false), LATIN);
        aRet.Put(SvxLanguageItem(nLang, SID_ATTR_LANGUAGE));

        aLang = aLinguCfg.GetProperty(u"DefaultLocale_CJK");
        aLang >>= aLocale;
        nLang = MsLangId::resolveSystemLanguageByScriptType(LanguageTag::convertToLanguageType( aLocale, false), ASIAN);
        aRet.Put(SvxLanguageItem(nLang, SID_ATTR_CHAR_CJK_LANGUAGE));

        aLang = aLinguCfg.GetProperty(u"DefaultLocale_CTL");
        aLang >>= aLocale;
        nLang = MsLangId::resolveSystemLanguageByScriptType(LanguageTag::convertToLanguageType( aLocale, false), COMPLEX);
        aRet.Put(SvxLanguageItem(nLang, SID_ATTR_CHAR_CTL_LANGUAGE));
    }
    if(bTextDialog)
        aRet.Put(SwPtrItem(FN_PARAM_STDFONTS, GetStdFontConfig()));
    if( dynamic_cast<SwPagePreview*>( SfxViewShell::Current())!=nullptr )
    {
        SfxBoolItem aBool(SfxBoolItem(SID_PRINTPREVIEW, true));
        aRet.Put(aBool);
    }

    FieldUnit eUnit = pPref->GetHScrollMetric();
    if(pAppView)
        pAppView->GetHRulerMetric(eUnit);
    aRet.Put(SfxUInt16Item( FN_HSCROLL_METRIC, static_cast< sal_uInt16 >(eUnit)));

    eUnit = pPref->GetVScrollMetric();
    if(pAppView)
        pAppView->GetVRulerMetric(eUnit);
    aRet.Put(SfxUInt16Item( FN_VSCROLL_METRIC, static_cast< sal_uInt16 >(eUnit) ));
    aRet.Put(SfxUInt16Item( SID_ATTR_METRIC, static_cast< sal_uInt16 >(pPref->GetMetric()) ));
    aRet.Put(SfxBoolItem(SID_ATTR_APPLYCHARUNIT, pPref->IsApplyCharUnit()));
    if(bTextDialog)
    {
        if(pAppView)
        {
            const SvxTabStopItem& rDefTabs =
                    pAppView->GetWrtShell().GetDefault(RES_PARATR_TABSTOP);
            aRet.Put( SfxUInt16Item( SID_ATTR_DEFTABSTOP, o3tl::narrowing<sal_uInt16>(::GetTabDist(rDefTabs))));
        }
        else
            aRet.Put(SfxUInt16Item( SID_ATTR_DEFTABSTOP, o3tl::toTwips(pPref->GetDefTabInMm100(), o3tl::Length::mm100)));
    }

    // Options for GridTabPage
    SvxGridItem aGridItem( SID_ATTR_GRID_OPTIONS);

    aGridItem.SetUseGridSnap( aViewOpt.IsSnap());
    aGridItem.SetSynchronize( aViewOpt.IsSynchronize());
    aGridItem.SetGridVisible( aViewOpt.IsGridVisible());

    const Size& rSnapSize = aViewOpt.GetSnapSize();
    aGridItem.SetFieldDrawX( o3tl::narrowing<sal_uInt16>(rSnapSize.Width() ));
    aGridItem.SetFieldDrawY( o3tl::narrowing<sal_uInt16>(rSnapSize.Height()));

    aGridItem.SetFieldDivisionX( aViewOpt.GetDivisionX());
    aGridItem.SetFieldDivisionY( aViewOpt.GetDivisionY());

    aRet.Put(aGridItem);

    // Options for PrintTabPage
    const SwPrintData* pOpt = GetPrtOptions(!bTextDialog);
    SwAddPrinterItem aAddPrinterItem(*pOpt );
    aRet.Put(aAddPrinterItem);

    // Options for Web
    if(!bTextDialog)
    {
        aRet.Put(SvxBrushItem(aViewOpt.GetRetoucheColor(), RES_BACKGROUND));
        aRet.Put(SfxUInt16Item(SID_HTML_MODE, HTMLMODE_ON));
    }

    return aRet;
}

void SwModule::ApplyItemSet( sal_uInt16 nId, const SfxItemSet& rSet )
{
    bool bTextDialog = nId == SID_SW_EDITOPTIONS;
    SwView* pAppView = GetView();
    if(pAppView && &pAppView->GetViewFrame() != SfxViewFrame::Current())
        pAppView = nullptr;
    if(pAppView)
    {
        // the text dialog mustn't apply data to the web view and vice versa
        bool bWebView = dynamic_cast<SwWebView*>( pAppView ) !=  nullptr;
        if(bWebView == bTextDialog)
            pAppView = nullptr;
    }

    SwViewOption aViewOpt = *GetUsrPref(!bTextDialog);
    SwMasterUsrPref* pPref = bTextDialog ? m_pUsrPref.get() : m_pWebUsrPref.get();

    SfxBindings *pBindings = pAppView ? &pAppView->GetViewFrame().GetBindings()
                                 : nullptr;

    // Interpret the page Documentview
    if( const SwDocDisplayItem* pDocDispItem = rSet.GetItemIfSet( FN_PARAM_DOCDISP, false ))
    {
        if(!aViewOpt.IsViewMetaChars())
        {
            if(     (!aViewOpt.IsTab( true ) &&  pDocDispItem->m_bTab) ||
                    (!aViewOpt.IsBlank( true ) && pDocDispItem->m_bSpace) ||
                    (!aViewOpt.IsShowBookmarks(true) && pDocDispItem->m_bBookmarks) ||
                    (!aViewOpt.IsParagraph( true ) && pDocDispItem->m_bParagraphEnd) ||
                    (!aViewOpt.IsLineBreak( true ) && pDocDispItem->m_bManualBreak) )
            {
                aViewOpt.SetViewMetaChars(true);
                if(pBindings)
                    pBindings->Invalidate(FN_VIEW_META_CHARS);
            }

        }
        pDocDispItem->FillViewOptions( aViewOpt );
        if(pBindings)
        {
            pBindings->Invalidate(FN_VIEW_GRAPHIC);
            pBindings->Invalidate(FN_VIEW_HIDDEN_PARA);
        }
    }

    // Elements - interpret Item
    bool bReFoldOutlineFolding = false;
    if( const SwElemItem* pElemItem = rSet.GetItemIfSet( FN_PARAM_ELEM, false ) )
    {
        pElemItem->FillViewOptions( aViewOpt );
        if (bTextDialog)
        {
            pPref->SetDefaultZoom(pElemItem->IsDefaultZoom());
            pPref->SetDefaultZoomType(pElemItem->GetDefaultZoomType());
            pPref->SetDefaultZoomValue(pElemItem->GetDefaultZoomValue());
        }

        // Outline-folding options
        if (SwWrtShell* pWrtShell = GetActiveWrtShell())
        {
            bool bIsOutlineFoldingOn = pWrtShell->GetViewOptions()->IsShowOutlineContentVisibilityButton();
            bool bTreatSubsChanged = aViewOpt.IsTreatSubOutlineLevelsAsContent()
                    != pWrtShell->GetViewOptions()->IsTreatSubOutlineLevelsAsContent();
            if (bIsOutlineFoldingOn &&
                    (!aViewOpt.IsShowOutlineContentVisibilityButton() || bTreatSubsChanged))
            {
                // Outline-folding options have change which require to show all content.
                // Either outline-folding is being switched off or outline-folding is currently on
                // and the treat subs option has changed.
                pWrtShell->GetView().GetViewFrame().GetDispatcher()->Execute(FN_SHOW_OUTLINECONTENTVISIBILITYBUTTON);
                if (bTreatSubsChanged)
                    bReFoldOutlineFolding = true; // folding method changed, set flag to refold below
            }
            else
            {
                // Refold needs to be done when outline-folding is being turned on or off
                bReFoldOutlineFolding =
                        pWrtShell->GetViewOptions()->IsShowOutlineContentVisibilityButton() !=
                        aViewOpt.IsShowOutlineContentVisibilityButton();
            }
        }
    }

    if( const SfxUInt16Item* pMetricItem = rSet.GetItemIfSet(SID_ATTR_METRIC, false ) )
    {
        SfxApplication::SetOptions(rSet);
        PutItem(*pMetricItem);
        ::SetDfltMetric(static_cast<FieldUnit>(pMetricItem->GetValue()), !bTextDialog);
    }
    if( const SfxBoolItem* pCharItem = rSet.GetItemIfSet(SID_ATTR_APPLYCHARUNIT,
                                                    false ) )
    {
        SfxApplication::SetOptions(rSet);
        ::SetApplyCharUnit(pCharItem->GetValue(), !bTextDialog);
    }

    if( const SfxUInt16Item* pMetricItem = rSet.GetItemIfSet(FN_HSCROLL_METRIC, false ) )
    {
        FieldUnit eUnit = static_cast<FieldUnit>(pMetricItem->GetValue());
        pPref->SetHScrollMetric(eUnit);
        if(pAppView)
            pAppView->ChangeTabMetric(eUnit);
    }

    if( const SfxUInt16Item* pMetricItem = rSet.GetItemIfSet(FN_VSCROLL_METRIC, false ) )
    {
        FieldUnit eUnit = static_cast<FieldUnit>(pMetricItem->GetValue());
        pPref->SetVScrollMetric(eUnit);
        if(pAppView)
            pAppView->ChangeVRulerMetric(eUnit);
    }

    if( const SfxUInt16Item* pItem = rSet.GetItemIfSet(SID_ATTR_DEFTABSTOP, false ) )
    {
        sal_uInt16 nTabDist = pItem->GetValue();
        pPref->SetDefTabInMm100(convertTwipToMm100(nTabDist));
        if(pAppView)
        {
            SvxTabStopItem aDefTabs( 0, 0, SvxTabAdjust::Default, RES_PARATR_TABSTOP );
            MakeDefTabs( nTabDist, aDefTabs );
            pAppView->GetWrtShell().SetDefault( aDefTabs );
        }
    }

    // Background only in WebDialog
    if(SfxItemState::SET == rSet.GetItemState(RES_BACKGROUND))
    {
        const SvxBrushItem& rBrushItem = rSet.Get(RES_BACKGROUND);
        aViewOpt.SetRetoucheColor( rBrushItem.GetColor() );
    }

    // Interpret page Grid Settings
    if( const SvxGridItem* pGridItem = rSet.GetItemIfSet( SID_ATTR_GRID_OPTIONS, false ))
    {
        aViewOpt.SetSnap( pGridItem->GetUseGridSnap() );
        aViewOpt.SetSynchronize(pGridItem->GetSynchronize());
        if( aViewOpt.IsGridVisible() != pGridItem->GetGridVisible() )
            aViewOpt.SetGridVisible( pGridItem->GetGridVisible());
        Size aSize( pGridItem->GetFieldDrawX(), pGridItem->GetFieldDrawY()  );
        if( aViewOpt.GetSnapSize() != aSize )
            aViewOpt.SetSnapSize( aSize );
        short nDiv = static_cast<short>(pGridItem->GetFieldDivisionX()) ;
        if( aViewOpt.GetDivisionX() != nDiv  )
            aViewOpt.SetDivisionX( nDiv );
        nDiv = static_cast<short>(pGridItem->GetFieldDivisionY());
        if( aViewOpt.GetDivisionY() != nDiv  )
            aViewOpt.SetDivisionY( nDiv  );

        if(pBindings)
        {
            pBindings->Invalidate(SID_GRID_VISIBLE);
            pBindings->Invalidate(SID_GRID_USE);
        }
    }

    // Interpret Writer Printer Options
    if( const SwAddPrinterItem* pAddPrinterAttr = rSet.GetItemIfSet( FN_PARAM_ADDPRINTER, false ) )
    {
        SwPrintOptions* pOpt = GetPrtOptions(!bTextDialog);
        if (pOpt)
        {
            *pOpt = *pAddPrinterAttr;
            SwPagePreview* pPagePreview = dynamic_cast<SwPagePreview*>( SfxViewShell::Current());
            if( pPagePreview !=nullptr )
            {
                pPagePreview->GetViewShell()->getIDocumentDeviceAccess().setPrintData(*pOpt);
                pPagePreview->PrintSettingsChanged();
            }
        }
    }

    if( const SwShadowCursorItem* pItem = rSet.GetItemIfSet( FN_PARAM_SHADOWCURSOR, false ))
    {
        pItem->FillViewOptions( aViewOpt );
        if(pBindings)
            pBindings->Invalidate(FN_SHADOWCURSOR);
    }

    if( pAppView )
    {
        SwWrtShell &rWrtSh = pAppView->GetWrtShell();
        const bool bAlignFormulas = rWrtSh.GetDoc()->getIDocumentSettingAccess().get( DocumentSettingId::MATH_BASELINE_ALIGNMENT );
        pPref->SetAlignMathObjectsToBaseline( bAlignFormulas );

        // don't align formulas in documents that are currently loading
        if (bAlignFormulas && !rWrtSh.GetDoc()->IsInReading())
            rWrtSh.AlignAllFormulasToBaseline();
    }

    if( const SfxBoolItem* pItem = rSet.GetItemIfSet( FN_PARAM_CRSR_IN_PROTECTED, false ))
    {
        aViewOpt.SetCursorInProtectedArea(pItem->GetValue());
    }

    if (const SwFmtAidsAutoComplItem* pItem = rSet.GetItemIfSet(FN_PARAM_FMT_AIDS_AUTOCOMPL, false))
    {
        aViewOpt.SetEncloseWithCharactersOn(pItem->IsEncloseWithCharactersOn());
    }

    // set elements for the current view and shell
    ApplyUsrPref( aViewOpt, pAppView, bTextDialog? SvViewOpt::DestText : SvViewOpt::DestWeb);

    // must be done after ApplyUsrPref
    if (SfxItemState::SET != rSet.GetItemState(FN_PARAM_ELEM, false))
        return;

    if (bReFoldOutlineFolding)
    {
        if (SwWrtShell* pWrtShell = GetActiveWrtShell())
        {
            pWrtShell->GetView().GetViewFrame().GetDispatcher()->Execute(FN_SHOW_OUTLINECONTENTVISIBILITYBUTTON);
            pWrtShell->GetView().GetViewFrame().GetDispatcher()->Execute(FN_SHOW_OUTLINECONTENTVISIBILITYBUTTON);
        }
    }
}

std::unique_ptr<SfxTabPage> SwModule::CreateTabPage( sal_uInt16 nId, weld::Container* pPage, weld::DialogController* pController, const SfxItemSet& rSet )
{
    std::unique_ptr<SfxTabPage> xRet;
    SfxAllItemSet aSet(*(rSet.GetPool()));
    switch( nId )
    {
        case RID_SW_TP_CONTENT_OPT:
        case RID_SW_TP_HTML_CONTENT_OPT:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ::CreateTabPage fnCreatePage = pFact->GetTabPageCreatorFunc( nId );
            xRet = (*fnCreatePage)( pPage, pController, &rSet );
            break;
        }
        case RID_SW_TP_HTML_OPTGRID_PAGE:
        case RID_SVXPAGE_GRID:
            xRet = SvxGridTabPage::Create(pPage, pController, rSet);
        break;

        case RID_SW_TP_STD_FONT:
        case RID_SW_TP_STD_FONT_CJK:
        case RID_SW_TP_STD_FONT_CTL:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ::CreateTabPage fnCreatePage = pFact->GetTabPageCreatorFunc( nId );
            xRet = (*fnCreatePage)( pPage, pController, &rSet );
            if(RID_SW_TP_STD_FONT != nId)
            {
                aSet.Put (SfxUInt16Item(SID_FONTMODE_TYPE, RID_SW_TP_STD_FONT_CJK == nId ? FONT_GROUP_CJK : FONT_GROUP_CTL));
                xRet->PageCreated(aSet);
            }
        }
        break;
        case RID_SW_TP_HTML_OPTPRINT_PAGE:
        case RID_SW_TP_OPTPRINT_PAGE:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ::CreateTabPage fnCreatePage = pFact->GetTabPageCreatorFunc( nId );
            xRet = (*fnCreatePage)( pPage, pController, &rSet );
            aSet.Put (SfxBoolItem(SID_FAX_LIST, true));
            xRet->PageCreated(aSet);
        }
        break;
        case RID_SW_TP_HTML_OPTTABLE_PAGE:
        case RID_SW_TP_OPTTABLE_PAGE:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ::CreateTabPage fnCreatePage = pFact->GetTabPageCreatorFunc( nId );
            xRet = (*fnCreatePage)( pPage, pController, &rSet );
            SwView* pCurrView = GetView();
            if(pCurrView)
            {
                // if text then not WebView and vice versa
                bool bWebView = dynamic_cast<SwWebView*>( pCurrView ) !=  nullptr;
                if( (bWebView &&  RID_SW_TP_HTML_OPTTABLE_PAGE == nId) ||
                    (!bWebView &&  RID_SW_TP_HTML_OPTTABLE_PAGE != nId) )
                {
                    aSet.Put (SwWrtShellItem(pCurrView->GetWrtShellPtr()));
                    xRet->PageCreated(aSet);
                }
            }
        }
        break;
        case RID_SW_TP_OPTSHDWCRSR:
        case RID_SW_TP_HTML_OPTSHDWCRSR:
        case RID_SW_TP_REDLINE_OPT:
        case RID_SW_TP_COMPARISON_OPT:
        case RID_SW_TP_OPTLOAD_PAGE:
        case RID_SW_TP_OPTCOMPATIBILITY_PAGE:
        case RID_SW_TP_MAILCONFIG:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ::CreateTabPage fnCreatePage = pFact->GetTabPageCreatorFunc( nId );
            xRet = (*fnCreatePage)( pPage, pController, &rSet );
            if (nId == RID_SW_TP_OPTSHDWCRSR || nId == RID_SW_TP_HTML_OPTSHDWCRSR)
            {
                SwView* pCurrView = GetView();
                if(pCurrView)
                {
                    aSet.Put( SwWrtShellItem( pCurrView->GetWrtShellPtr() ) );
                    xRet->PageCreated(aSet);
                }
            }
        }
        break;
        case  RID_SW_TP_OPTTEST_PAGE:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ::CreateTabPage fnCreatePage = pFact->GetTabPageCreatorFunc( nId );
            xRet = (*fnCreatePage)( pPage, pController, &rSet );
            break;
        }
        case RID_SW_TP_OPTCAPTION_PAGE:
        {
            SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
            ::CreateTabPage fnCreatePage = pFact->GetTabPageCreatorFunc( RID_SW_TP_OPTCAPTION_PAGE );
            xRet = (*fnCreatePage)( pPage, pController, &rSet );
        }
        break;
    }

    if(!xRet)
        SAL_WARN( "sw", "SwModule::CreateTabPage(): Unknown tabpage id " << nId );
    return xRet;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
