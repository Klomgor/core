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

#include <DrawViewShell.hxx>
#include <com/sun/star/form/FormButtonType.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <comphelper/string.hxx>
#include <svx/svxids.hrc>
#include <svx/sdmetitm.hxx>
#include <svx/hlnkitem.hxx>
#include <editeng/cmapitem.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/flditem.hxx>
#include <editeng/udlnitem.hxx>
#include <sfx2/viewfrm.hxx>
#include <svl/whiter.hxx>
#include <svl/eitem.hxx>
#include <svl/intitem.hxx>
#include <svl/itempool.hxx>
#include <sfx2/tplpitem.hxx>
#include <sfx2/bindings.hxx>
#include <svx/xdef.hxx>
#include <svx/svdoutl.hxx>
#include <svx/svdouno.hxx>
#include <svx/svdpagv.hxx>
#include <svx/fmshell.hxx>
#include <svl/cjkoptions.hxx>
#include <app.hrc>

#include <sdmod.hxx>
#include <sdpage.hxx>
#include <stlsheet.hxx>
#include <drawview.hxx>
#include <drawdoc.hxx>
#include <Window.hxx>
#include <ViewShellBase.hxx>
#include <FormShellManager.hxx>
#include <anminfo.hxx>

#include <editeng/lspcitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/escapementitem.hxx>
#include <editeng/numitem.hxx>
#include <editeng/adjustitem.hxx>
#include <editeng/urlfieldhelper.hxx>
#include <svx/nbdtmgfact.hxx>
#include <svx/nbdtmg.hxx>
#include <memory>

using namespace svx::sidebar;
using namespace ::com::sun::star;

namespace sd {

/**
 * Set state of controller SfxSlots
 */
void DrawViewShell::GetCtrlState(SfxItemSet &rSet)
{
    if (rSet.GetItemState(SID_RELOAD) != SfxItemState::UNKNOWN)
    {
        // let "last version" of SFx en/disable
        GetViewFrame()->GetSlotState (SID_RELOAD, nullptr, &rSet);
    }

    if (SfxItemState::DEFAULT == rSet.GetItemState(SID_HYPERLINK_GETLINK))
    {
        SvxHyperlinkItem aHLinkItem;

        OutlinerView* pOLV = mpDrawView->GetTextEditOutlinerView();

        if (pOLV)
        {
            const SvxFieldItem* pFieldItem = pOLV->GetFieldAtSelection();
            const SvxFieldData* pField = pFieldItem ? pFieldItem->GetField() : nullptr;
            if( auto pUrlField = dynamic_cast< const SvxURLField *>( pField ) )
            {
                aHLinkItem.SetName(pUrlField->GetRepresentation());
                aHLinkItem.SetURL(pUrlField->GetURL());
                aHLinkItem.SetTargetFrame(pUrlField->GetTargetFrame());
                aHLinkItem.SetIntName(pUrlField->GetName());
            }
            else
            {
                // use selected text as name for urls
                OUString sReturn = pOLV->GetSelected();
                if (sReturn.getLength() > 255)
                    sReturn = sReturn.copy(0, 255);
                aHLinkItem.SetName(comphelper::string::stripEnd(sReturn, ' '));
            }
        }
        else
        {
            const SdrMarkList& rMarkList = mpDrawView->GetMarkedObjectList();
            if (rMarkList.GetMarkCount() > 0)
            {
                bool bFound = false;

                SdrObject* pMarkedObj = rMarkList.GetMark(0)->GetMarkedSdrObj();
                if( pMarkedObj && (SdrInventor::FmForm == pMarkedObj->GetObjInventor()) )
                {
                    SdrUnoObj* pUnoCtrl = dynamic_cast< SdrUnoObj* >( pMarkedObj );

                    if(pUnoCtrl) try
                    {
                        uno::Reference< awt::XControlModel > xControlModel( pUnoCtrl->GetUnoControlModel(), uno::UNO_SET_THROW );
                        uno::Reference< beans::XPropertySet > xPropSet( xControlModel, uno::UNO_QUERY_THROW );
                        uno::Reference< beans::XPropertySetInfo > xPropInfo( xPropSet->getPropertySetInfo(), uno::UNO_SET_THROW );

                        form::FormButtonType eButtonType = form::FormButtonType_URL;
                        static constexpr OUString sButtonType( u"ButtonType"_ustr );
                        if(xPropInfo->hasPropertyByName( sButtonType ) && (xPropSet->getPropertyValue( sButtonType ) >>= eButtonType ) )
                        {
                            OUString aString;

                            // Label
                            static constexpr OUString sLabel( u"Label"_ustr );
                            if(xPropInfo->hasPropertyByName(sLabel))
                            {
                                if( xPropSet->getPropertyValue(sLabel) >>= aString )
                                    aHLinkItem.SetName(aString);
                            }

                            // URL
                            static constexpr OUString sTargetURL( u"TargetURL"_ustr );
                            if(xPropInfo->hasPropertyByName(sTargetURL))
                            {
                                if( xPropSet->getPropertyValue(sTargetURL) >>= aString )
                                    aHLinkItem.SetURL(aString);
                            }

                            // Target
                            static constexpr OUString sTargetFrame( u"TargetFrame"_ustr );
                            if(xPropInfo->hasPropertyByName(sTargetFrame) )
                            {
                                if( xPropSet->getPropertyValue(sTargetFrame) >>= aString )
                                    aHLinkItem.SetTargetFrame(aString);
                            }

                            aHLinkItem.SetInsertMode(HLINK_BUTTON);
                            bFound = true;
                        }
                    }
                    catch( uno::Exception& )
                    {
                    }
                }

                // try interaction link
                if( !bFound && pMarkedObj )
                {
                    SdAnimationInfo* pInfo = SdDrawDocument::GetShapeUserData(*pMarkedObj);
                    if( pInfo && (pInfo->meClickAction == presentation::ClickAction_DOCUMENT) )
                        aHLinkItem.SetURL( pInfo->GetBookmark());
                    aHLinkItem.SetInsertMode(HLINK_BUTTON);
                }
            }
        }

        rSet.Put(aHLinkItem);
    }
    rSet.Put( SfxBoolItem( SID_READONLY_MODE, mbReadOnly ) );

    // output quality
    if( SfxItemState::DEFAULT == rSet.GetItemState( SID_OUTPUT_QUALITY_COLOR ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_OUTPUT_QUALITY_GRAYSCALE ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_OUTPUT_QUALITY_BLACKWHITE ) ||
        SfxItemState::DEFAULT == rSet.GetItemState( SID_OUTPUT_QUALITY_CONTRAST ) )
    {
        const sal_uLong nMode = static_cast<sal_Int32>(GetActiveWindow()->GetOutDev()->GetDrawMode());
        rSet.Put( SfxBoolItem( SID_OUTPUT_QUALITY_COLOR, sal_uLong(OUTPUT_DRAWMODE_COLOR) == nMode ) );
        rSet.Put( SfxBoolItem( SID_OUTPUT_QUALITY_GRAYSCALE, static_cast<sal_uLong>(OUTPUT_DRAWMODE_GRAYSCALE) == nMode ) );
        rSet.Put( SfxBoolItem( SID_OUTPUT_QUALITY_BLACKWHITE, static_cast<sal_uLong>(OUTPUT_DRAWMODE_BLACKWHITE) == nMode ) );
        rSet.Put( SfxBoolItem( SID_OUTPUT_QUALITY_CONTRAST, static_cast<sal_uLong>(OUTPUT_DRAWMODE_CONTRAST) == nMode ) );
    }

    if ( SfxItemState::DEFAULT == rSet.GetItemState(SID_MAIL_SCROLLBODY_PAGEDOWN) )
    {
        rSet.Put( SfxBoolItem( SID_MAIL_SCROLLBODY_PAGEDOWN, true ) );
    }

    if ( SfxItemState::DEFAULT == rSet.GetItemState(SID_ATTR_YEAR2000) )
    {
        FmFormShell* pFormShell = GetViewShellBase().GetFormShellManager()->GetFormShell();
        if (pFormShell != nullptr)
        {
            sal_uInt16 nState = 0;
            if (pFormShell->GetY2KState(nState))
                rSet.Put( SfxUInt16Item( SID_ATTR_YEAR2000, nState ) );
            else
                rSet.DisableItem( SID_ATTR_YEAR2000 );
        }
    }

    if ( !GetView()->GetTextEditOutliner() )
    {
        if( !SvtCJKOptions::IsChangeCaseMapEnabled() )
        {
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HALFWIDTH, false );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_FULLWIDTH, false );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HIRAGANA, false );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_KATAKANA, false );
        }
        else
        {
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HALFWIDTH, true );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_FULLWIDTH, true );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HIRAGANA, true );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_KATAKANA, true );
        }

        rSet.DisableItem( SID_TRANSLITERATE_SENTENCE_CASE );
        rSet.DisableItem( SID_TRANSLITERATE_TITLE_CASE );
        rSet.DisableItem( SID_TRANSLITERATE_TOGGLE_CASE );
        rSet.DisableItem( SID_TRANSLITERATE_UPPER );
        rSet.DisableItem( SID_TRANSLITERATE_LOWER );
        rSet.DisableItem( SID_TRANSLITERATE_HALFWIDTH );
        rSet.DisableItem( SID_TRANSLITERATE_FULLWIDTH );
        rSet.DisableItem( SID_TRANSLITERATE_HIRAGANA );
        rSet.DisableItem( SID_TRANSLITERATE_KATAKANA );
    }
    else
    {
        if( !SvtCJKOptions::IsChangeCaseMapEnabled() )
        {
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HALFWIDTH, false );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_FULLWIDTH, false );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HIRAGANA, false );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_KATAKANA, false );
            rSet.DisableItem( SID_TRANSLITERATE_HALFWIDTH );
            rSet.DisableItem( SID_TRANSLITERATE_FULLWIDTH );
            rSet.DisableItem( SID_TRANSLITERATE_HIRAGANA );
            rSet.DisableItem( SID_TRANSLITERATE_KATAKANA );
        }
        else
        {
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HALFWIDTH, true );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_FULLWIDTH, true );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_HIRAGANA, true );
            GetViewFrame()->GetBindings().SetVisibleState( SID_TRANSLITERATE_KATAKANA, true );
        }
    }
}

void DrawViewShell::GetAttrState( SfxItemSet& rSet )
{
    SfxWhichIter    aIter( rSet );
    sal_uInt16          nWhich = aIter.FirstWhich();

    bool    bAttr = false;
    SfxAllItemSet aAllSet( *rSet.GetPool() );

    const SdrMarkList& rMarkList = mpDrawView->GetMarkedObjectList();
    while ( nWhich )
    {
        sal_uInt16 nSlotId = SfxItemPool::IsWhich(nWhich)
            ? GetPool().GetSlotId(nWhich)
            : nWhich;
        switch ( nSlotId )
        {
            case SID_ATTR_PARA_ADJUST_LEFT:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );

                SvxAdjust eAdj = aAttrs.Get( EE_PARA_JUST ).GetAdjust();
                if ( eAdj == SvxAdjust::Left)
                {
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_LEFT, true ) );
                }

                bAttr = true;

                Invalidate(nSlotId);
            }
            break;
            case SID_ATTR_PARA_ADJUST_CENTER:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );

                SvxAdjust eAdj = aAttrs.Get( EE_PARA_JUST ).GetAdjust();
                if ( eAdj == SvxAdjust::Center)
                {
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_CENTER, true ) );
                }

                bAttr = true;

                Invalidate(nSlotId);
            }
            break;
            case SID_ATTR_PARA_ADJUST_RIGHT:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );

                SvxAdjust eAdj = aAttrs.Get( EE_PARA_JUST ).GetAdjust();
                if ( eAdj == SvxAdjust::Right)
                {
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_RIGHT, true ) );
                }

                bAttr = true;

                Invalidate(nSlotId);
            }
            break;
            case SID_ATTR_PARA_ADJUST_BLOCK:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );

                SvxAdjust eAdj = aAttrs.Get( EE_PARA_JUST ).GetAdjust();
                if ( eAdj == SvxAdjust::Block)
                {
                    rSet.Put( SfxBoolItem( SID_ATTR_PARA_ADJUST_BLOCK, true ) );
                }

                bAttr = true;

                Invalidate(nSlotId);
            }
            break;
            case SID_ATTR_PARA_LRSPACE:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );
                SvxLRSpaceItem aLRSpace = aAttrs.Get( EE_PARA_LRSPACE );
                aLRSpace.SetWhich(SID_ATTR_PARA_LRSPACE);
                rSet.Put(aLRSpace);
                bAttr = true;
                Invalidate(SID_ATTR_PARA_LRSPACE);
            }
            break;
            case SID_ATTR_PARA_LINESPACE:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );
                SvxLineSpacingItem aLineLR = aAttrs.Get( EE_PARA_SBL );
                rSet.Put(aLineLR);
                bAttr = true;
                Invalidate(SID_ATTR_PARA_LINESPACE);
            }
            break;
            case SID_ATTR_PARA_ULSPACE:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );
                SvxULSpaceItem aULSP = aAttrs.Get( EE_PARA_ULSPACE );
                aULSP.SetWhich(SID_ATTR_PARA_ULSPACE);
                rSet.Put(aULSP);
                bAttr = true;
                Invalidate(SID_ATTR_PARA_ULSPACE);
            }
            break;
            case SID_ULINE_VAL_NONE:
            case SID_ULINE_VAL_SINGLE:
            case SID_ULINE_VAL_DOUBLE:
            case SID_ULINE_VAL_DOTTED:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );
                if( aAttrs.GetItemState( EE_CHAR_UNDERLINE ) >= SfxItemState::DEFAULT )
                {
                    FontLineStyle eLineStyle = aAttrs.Get(EE_CHAR_UNDERLINE).GetLineStyle();

                    switch (nSlotId)
                    {
                        case SID_ULINE_VAL_NONE:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_NONE));
                            break;
                        case SID_ULINE_VAL_SINGLE:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_SINGLE));
                            break;
                        case SID_ULINE_VAL_DOUBLE:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_DOUBLE));
                            break;
                        case SID_ULINE_VAL_DOTTED:
                            rSet.Put(SfxBoolItem(nSlotId, eLineStyle == LINESTYLE_DOTTED));
                            break;
                    }
                }

                bAttr = true;

                Invalidate(nSlotId);
            }
            break;
            case SID_ATTR_FILL_STYLE:
            case SID_ATTR_FILL_COLOR:
            case SID_ATTR_FILL_GRADIENT:
            case SID_ATTR_FILL_HATCH:
            case SID_ATTR_FILL_BITMAP:
            case SID_ATTR_FILL_SHADOW:
            case SID_ATTR_SHADOW_COLOR:
            case SID_ATTR_SHADOW_TRANSPARENCE:
            case SID_ATTR_SHADOW_BLUR:
            case SID_ATTR_SHADOW_XDISTANCE:
            case SID_ATTR_SHADOW_YDISTANCE:
            case SID_ATTR_FILL_USE_SLIDE_BACKGROUND:
            case SID_ATTR_FILL_TRANSPARENCE:
            case SID_ATTR_FILL_FLOATTRANSPARENCE:
            case SID_ATTR_LINE_STYLE:
            case SID_ATTR_LINE_DASH:
            case SID_ATTR_LINE_WIDTH:
            case SID_ATTR_LINE_COLOR:
            case SID_ATTR_LINE_TRANSPARENCE:
            case SID_ATTR_LINE_JOINT:
            case SID_ATTR_LINE_CAP:
            case SID_ATTR_TEXT_FITTOSIZE:
            case SID_ATTR_CHAR_FONT:
            case SID_ATTR_CHAR_FONTHEIGHT:
            case SID_ATTR_CHAR_SHADOWED:
            case SID_ATTR_CHAR_POSTURE:
            case SID_ATTR_CHAR_OVERLINE:
            case SID_ATTR_CHAR_UNDERLINE:
            case SID_ATTR_CHAR_STRIKEOUT:
            case SID_ATTR_CHAR_CONTOUR:
            case SID_ATTR_CHAR_WEIGHT:
            case SID_ATTR_CHAR_COLOR:
            case SID_ATTR_CHAR_KERNING:
            case SID_ATTR_CHAR_CASEMAP:
            case SID_ATTR_GLOW_COLOR:
            case SID_ATTR_GLOW_RADIUS:
            case SID_ATTR_GLOW_TRANSPARENCY:
            case SID_ATTR_GLOW_TEXT_COLOR:
            case SID_ATTR_GLOW_TEXT_RADIUS:
            case SID_ATTR_GLOW_TEXT_TRANSPARENCY:
            case SID_ATTR_SOFTEDGE_RADIUS:
            case SID_SET_SUB_SCRIPT:
            case SID_SET_SUPER_SCRIPT:
            case SID_SET_SMALL_CAPS:
            {
                bAttr = true;
            }
            break;

            case SID_ATTR_TEXTCOLUMNS_NUMBER:
            case SID_ATTR_TEXTCOLUMNS_SPACING:
            {
                SfxItemSet aAttrs(GetDoc()->GetPool());
                mpDrawView->GetAttributes(aAttrs);
                const sal_uInt16 nActWhich = nSlotId == SID_ATTR_TEXTCOLUMNS_NUMBER
                                                    ? SDRATTR_TEXTCOLUMNS_NUMBER
                                                    : SDRATTR_TEXTCOLUMNS_SPACING;
                rSet.Put(aAttrs.Get(nActWhich).CloneSetWhich(nSlotId));
            }
            break;

            case SID_HYPHENATION:
            {
                SfxItemSet aAttrs( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aAttrs );
                if( aAttrs.GetItemState( EE_PARA_HYPHENATE ) >= SfxItemState::DEFAULT )
                {
                    bool bValue = aAttrs.Get( EE_PARA_HYPHENATE ).GetValue();
                    rSet.Put( SfxBoolItem( SID_HYPHENATION, bValue ) );
                }
            }
            break;

            case SID_STYLE_FAMILY2:
            case SID_STYLE_FAMILY3:
            case SID_STYLE_FAMILY5:
            case SID_STYLE_APPLY: // StyleControl
            {
                SfxStyleSheet* pStyleSheet = mpDrawView->GetStyleSheet();
                if( pStyleSheet )
                {
                    if( nSlotId != SID_STYLE_APPLY && rMarkList.GetMarkCount() == 0 )
                    {
                        SfxTemplateItem aTmpItem( nWhich, OUString() );
                        aAllSet.Put( aTmpItem );
                    }
                    else
                    {
                        if (pStyleSheet->GetFamily() == SfxStyleFamily::Page)
                            pStyleSheet = static_cast<SdStyleSheet*>(pStyleSheet)->GetPseudoStyleSheet();

                        if( pStyleSheet )
                        {
                            SfxStyleFamily eFamily = pStyleSheet->GetFamily();

                            if ((eFamily == SfxStyleFamily::Para &&     nSlotId == SID_STYLE_FAMILY2)       ||
                                (eFamily == SfxStyleFamily::Frame     && nSlotId == SID_STYLE_FAMILY3)       ||
                                (eFamily == SfxStyleFamily::Pseudo &&   nSlotId == SID_STYLE_FAMILY5))
                            {
                                SfxTemplateItem aTmpItem ( nWhich, pStyleSheet->GetName() );
                                aAllSet.Put( aTmpItem );
                            }
                            else
                            {
                                SfxTemplateItem aTmpItem(nWhich, OUString());
                                aAllSet.Put( aTmpItem );
                            }
                        }
                    }
                }
                else
                {   SfxTemplateItem aItem( nWhich, OUString() );
                    aAllSet.Put( aItem );
                }
            }
            break;

            case SID_SET_DEFAULT:
            {
                if( !rMarkList.GetMarkCount() ||
                    ( !mpDrawView->IsTextEdit() && !mpDrawView->GetStyleSheet() )
                  )
                    rSet.DisableItem( nWhich );
            }
            break;

            case SID_REMOVE_HYPERLINK:
            {
                if (!URLFieldHelper::IsCursorAtURLField(mpDrawView->GetTextEditOutlinerView(),
                                                        /*AlsoCheckBeforeCursor=*/true))
                    rSet.DisableItem(nWhich);
            }
            break;

            case SID_INSERT_HYPERLINK:
            {
                // Disable when no text selected or when cursor is at URL field
                if (!HasSelection(true)
                    || URLFieldHelper::IsCursorAtURLField(mpDrawView->GetTextEditOutlinerView(),
                                                          /*AlsoCheckBeforeCursor=*/true))
                {
                    rSet.DisableItem(nWhich);
                }
            }
            break;

            case SID_STYLE_WATERCAN:
            {
                std::unique_ptr<SfxUInt16Item> pFamilyItem;
                GetViewFrame()->GetBindings().QueryState(SID_STYLE_FAMILY, pFamilyItem);
                if (pFamilyItem && static_cast<SfxStyleFamily>(pFamilyItem->GetValue()) == SfxStyleFamily::Pseudo)
                    rSet.Put(SfxBoolItem(nWhich,false));
                else
                {
                    SfxBoolItem aItem(nWhich, SdModule::get()->GetWaterCan());
                    aAllSet.Put( aItem );
                }
            }
            break;

            case SID_STYLE_NEW:
            {
                std::unique_ptr<SfxUInt16Item> pFamilyItem;
                GetViewFrame()->GetBindings().QueryState(SID_STYLE_FAMILY, pFamilyItem);
                if (pFamilyItem && static_cast<SfxStyleFamily>(pFamilyItem->GetValue()) == SfxStyleFamily::Pseudo)
                {
                    rSet.DisableItem(nWhich);
                }
            }
            break;

            case SID_STYLE_DRAGHIERARCHIE:
            {
                std::unique_ptr<SfxUInt16Item> pFamilyItem;
                GetViewFrame()->GetBindings().QueryState(SID_STYLE_FAMILY, pFamilyItem);
                if (pFamilyItem && static_cast<SfxStyleFamily>(pFamilyItem->GetValue()) == SfxStyleFamily::Pseudo)
                    rSet.DisableItem(nWhich);
            }
            break;

            case SID_STYLE_NEW_BY_EXAMPLE:
            {
                // It is not possible to create PseudoStyleSheets 'by Example';
                // normal style sheets need a selected object for that

                std::unique_ptr<SfxUInt16Item> pFamilyItem;
                GetViewFrame()->GetBindings().QueryState(SID_STYLE_FAMILY, pFamilyItem);
                if (pFamilyItem)
                {
                    if (static_cast<SfxStyleFamily>(pFamilyItem->GetValue()) == SfxStyleFamily::Pseudo)
                    {
                        rSet.DisableItem(nWhich);
                    }
                    else if (static_cast<SfxStyleFamily>(pFamilyItem->GetValue()) == SfxStyleFamily::Para)
                    {
                        if (rMarkList.GetMarkCount() == 0)
                        {
                            rSet.DisableItem(nWhich);
                        }
                    }
                }
                // if there is no (yet) a style designer, we have to go back into the
                // view state; an actual set family can not be considered
                else
                {
                    if (rMarkList.GetMarkCount() == 0)
                    {
                        rSet.DisableItem(nWhich);
                    }
                }
            }
            break;

            case SID_STYLE_UPDATE_BY_EXAMPLE:
            {
                if (rMarkList.GetMarkCount() == 0)
                {
                    rSet.DisableItem(nWhich);
                }
            }
            break;
            case FN_BUL_NUM_RULE_INDEX:
            case FN_NUM_NUM_RULE_INDEX:
            {
                SfxItemSet aEditAttr( GetDoc()->GetPool() );
                mpDrawView->GetAttributes( aEditAttr );

                SfxItemSetFixed<EE_ITEMS_START, EE_ITEMS_END> aNewAttr( GetPool() );
                aNewAttr.Put( aEditAttr, false );

                std::unique_ptr<SvxNumRule> pNumRule;
                const SfxPoolItem* pTmpItem=nullptr;
                TypedWhichId<SvxNumBulletItem> nNumItemId = SID_ATTR_NUMBERING_RULE;
                sal_uInt16 nActNumLvl = mpDrawView->GetSelectionLevel();
                pTmpItem=GetNumBulletItem(aNewAttr, nNumItemId);

                if (pTmpItem)
                    pNumRule.reset(new SvxNumRule(static_cast<const SvxNumBulletItem*>(pTmpItem)->GetNumRule()));

                if ( pNumRule )
                {
                    sal_uInt16 nMask = 1;
                    sal_uInt16 nCount = 0;
                    sal_uInt16 nCurLevel = sal_uInt16(0xFFFF);
                    for(sal_uInt16 i = 0; i < pNumRule->GetLevelCount(); i++)
                    {
                        if(nActNumLvl & nMask)
                        {
                            nCount++;
                            nCurLevel = i;
                        }
                        nMask <<= 1;
                    }
                    if ( nCount == 1 )
                    {
                        const SvxNumberFormat* pNumFmt = pNumRule->Get(nCurLevel);
                        if ( pNumFmt )
                        {
                            bool bBullets = false;
                            switch(pNumFmt->GetNumberingType())
                            {
                                case SVX_NUM_CHAR_SPECIAL:
                                case SVX_NUM_BITMAP:
                                    bBullets = true;
                                    break;

                                default:
                                    bBullets = false;
                            }

                            rSet.Put(SfxUInt16Item(FN_BUL_NUM_RULE_INDEX,sal_uInt16(0xFFFF)));
                            rSet.Put(SfxUInt16Item(FN_NUM_NUM_RULE_INDEX,sal_uInt16(0xFFFF)));
                            if ( bBullets )
                            {
                                NBOTypeMgrBase* pBullets = NBOutlineTypeMgrFact::CreateInstance(NBOType::Bullets);
                                if ( pBullets )
                                {
                                    sal_uInt16 nBulIndex = pBullets->GetNBOIndexForNumRule(*pNumRule,nActNumLvl);
                                    rSet.Put(SfxUInt16Item(FN_BUL_NUM_RULE_INDEX,nBulIndex));
                                }
                            }else
                            {
                                NBOTypeMgrBase* pNumbering = NBOutlineTypeMgrFact::CreateInstance(NBOType::Numbering);
                                if ( pNumbering )
                                {
                                    sal_uInt16 nBulIndex = pNumbering->GetNBOIndexForNumRule(*pNumRule,nActNumLvl);
                                    rSet.Put(SfxUInt16Item(FN_NUM_NUM_RULE_INDEX,nBulIndex));
                                }
                            }
                        }
                    }
                }
            }
            break;
            case FN_NUM_BULLET_ON:
            case FN_NUM_NUMBERING_ON:
            {
                bool bEnable = false;
                const size_t nMarkCount = rMarkList.GetMarkCount();
                for (size_t nIndex = 0; nIndex < nMarkCount; ++nIndex)
                {
                    SdrTextObj* pTextObj = DynCastSdrTextObj(rMarkList.GetMark(nIndex)->GetMarkedSdrObj());
                    if (pTextObj && pTextObj->GetObjInventor() == SdrInventor::Default)
                    {
                        if (pTextObj->GetObjIdentifier() != SdrObjKind::OLE2)
                        {
                            bEnable = true;
                            break;
                        }
                    }
                }
                if (bEnable)
                {
                    bool bIsBullet = false;
                    bool bIsNumbering = false;
                    OutlinerView* pOlView = mpDrawView->GetTextEditOutlinerView();
                    if (pOlView)
                    {
                        pOlView->IsBulletOrNumbering(bIsBullet, bIsNumbering);
                    }
                    rSet.Put(SfxBoolItem(FN_NUM_BULLET_ON, bIsBullet));
                    rSet.Put(SfxBoolItem(FN_NUM_NUMBERING_ON, bIsNumbering));
                }
                else
                {
                    rSet.DisableItem(FN_NUM_BULLET_ON);
                    rSet.DisableItem(FN_NUM_NUMBERING_ON);
                }
            }
            break;
        }
        nWhich = aIter.NextWhich();
    }

    std::optional<SfxItemSet> pSet;

    if( bAttr )
    {
        pSet.emplace( GetDoc()->GetPool() );
        mpDrawView->GetAttributes( *pSet );
        rSet.Put( *pSet, false );
    }

    rSet.Put( aAllSet, false );

    // there were changes at area and/or line attributes
    if( !(bAttr && pSet) )
        return;

    // if the view owns selected objects, corresponding items have to be
    // changed from SfxItemState::DEFAULT (_ON) to SfxItemState::DISABLED
    if( rMarkList.GetMarkCount() != 0 )
    {
        SfxWhichIter aNewIter( *pSet );
        nWhich = aNewIter.FirstWhich();
        while( nWhich )
        {
            if (nWhich >= XATTR_LINE_FIRST && nWhich <= XATTR_LINE_LAST
                && SfxItemState::DEFAULT == aNewIter.GetItemState() )
            {
                rSet.ClearItem( nWhich );
                rSet.DisableItem( nWhich );
            }
            nWhich = aNewIter.NextWhich();
        }
    }

    SfxItemState eState = pSet->GetItemState( EE_PARA_LRSPACE );
    if ( eState == SfxItemState::INVALID )
    {
        rSet.InvalidateItem(EE_PARA_LRSPACE);
        rSet.InvalidateItem(SID_ATTR_PARA_LRSPACE);
    }
    eState = pSet->GetItemState( EE_PARA_SBL );
    if ( eState == SfxItemState::INVALID )
    {
        rSet.InvalidateItem(EE_PARA_SBL);
        rSet.InvalidateItem(SID_ATTR_PARA_LINESPACE);
    }
    eState = pSet->GetItemState( EE_PARA_ULSPACE );
    if ( eState == SfxItemState::INVALID )
    {
        rSet.InvalidateItem(EE_PARA_ULSPACE);
        rSet.InvalidateItem(SID_ATTR_PARA_ULSPACE);
    }

    SvxEscapement eEsc = pSet->Get(EE_CHAR_ESCAPEMENT).GetEscapement();
    rSet.Put(SfxBoolItem(SID_SET_SUPER_SCRIPT, eEsc == SvxEscapement::Superscript));
    rSet.Put(SfxBoolItem(SID_SET_SUB_SCRIPT, eEsc == SvxEscapement::Subscript));

    SvxCaseMap eCaseMap = pSet->Get(EE_CHAR_CASEMAP).GetCaseMap();
    rSet.Put(SfxBoolItem(SID_SET_SMALL_CAPS, eCaseMap == SvxCaseMap::SmallCaps));

    eState = pSet->GetItemState( EE_CHAR_KERNING );
    if ( eState == SfxItemState::INVALID )
    {
        rSet.InvalidateItem(EE_CHAR_KERNING);
        rSet.InvalidateItem(SID_ATTR_CHAR_KERNING);
    }
}

OUString DrawViewShell::GetSelectionText(bool bCompleteWords)
{
    OUString aStrSelection;
    ::Outliner* pOl = mpDrawView->GetTextEditOutliner();
    OutlinerView* pOlView = mpDrawView->GetTextEditOutlinerView();

    if (pOl && pOlView)
    {
        if (bCompleteWords)
        {
            ESelection aSel = pOlView->GetSelection();
            OUString aStrCurrentDelimiters = pOl->GetWordDelimiters();

            pOl->SetWordDelimiters(u" .,;\"'"_ustr);
            aStrSelection = pOl->GetWord(aSel.end);
            pOl->SetWordDelimiters( aStrCurrentDelimiters );
        }
        else
        {
            aStrSelection = pOlView->GetSelected();
        }
    }

    return aStrSelection;
}

bool DrawViewShell::HasSelection(bool bText) const
{
    bool bReturn = false;

    const SdrMarkList& rMarkList = mpDrawView->GetMarkedObjectList();
    if (bText)
    {
        OutlinerView* pOlView = mpDrawView->GetTextEditOutlinerView();

        if (pOlView && !pOlView->GetSelected().isEmpty())
        {
            bReturn = true;
        }
    }
    else if (rMarkList.GetMarkCount() != 0)
    {
        bReturn = true;
    }

    return bReturn;
}

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
