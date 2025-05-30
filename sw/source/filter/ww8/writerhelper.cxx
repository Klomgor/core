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
#include <sal/log.hxx>

#include <com/sun/star/util/CloseVetoException.hpp>

#include <doc.hxx>
#include "writerhelper.hxx"
#include <msfilter.hxx>
#include <com/sun/star/container/XChild.hpp>

#include <algorithm>
#include <svl/itemiter.hxx>
#include <svx/svdobj.hxx>
#include <svx/svdoole2.hxx>
#include <tools/UnitConversion.hxx>
#include <editeng/formatbreakitem.hxx>
#include <osl/diagnose.h>
#include <ndtxt.hxx>
#include <ndnotxt.hxx>
#include <fmtcntnt.hxx>
#include <swtable.hxx>
#include <frmfmt.hxx>
#include <flypos.hxx>
#include <fmtanchr.hxx>
#include <fmtfsize.hxx>
#include <SwStyleNameMapper.hxx>
#include <docary.hxx>
#include <charfmt.hxx>
#include <fchrfmt.hxx>
#include <redline.hxx>
#include "types.hxx"
#include <svtools/embedhlp.hxx>
#include <numrule.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <IDocumentMarkAccess.hxx>
#include <IMark.hxx>
#include <grfatr.hxx>
#include <poolfmt.hxx>

using namespace com::sun::star;

namespace
{
    // #i98791# - adjust sorting
    // Utility to sort SwTextFormatColl's by their assigned outline style list level
    class outlinecmp
    {
    public:
        bool operator()(const SwTextFormatColl *pA, const SwTextFormatColl *pB) const
        {
            // #i98791#
            bool bResult( false );
            const bool bIsAAssignedToOutlineStyle( pA->IsAssignedToListLevelOfOutlineStyle() );
            const bool bIsBAssignedToOutlineStyle( pB->IsAssignedToListLevelOfOutlineStyle() );
            if ( bIsAAssignedToOutlineStyle != bIsBAssignedToOutlineStyle )
            {
                bResult = bIsBAssignedToOutlineStyle;
            }
            else if ( !bIsAAssignedToOutlineStyle )
            {
                // pA and pB are equal regarding the sorting criteria.
                // Thus return value does not matter.
                bResult = false;
            }
            else
            {
                bResult = pA->GetAssignedOutlineStyleLevel() < pB->GetAssignedOutlineStyleLevel();
            }

            return bResult;
       }
    };

    bool IsValidSlotWhich(sal_uInt16 nSlotId, sal_uInt16 nWhichId)
    {
        return (nSlotId != 0 && nWhichId != 0 && nSlotId != nWhichId);
    }

    /*
     Utility to convert a SwPosFlyFrames into a simple vector of ww8::Frames

     The crucial thing is that a ww8::Frame always has an anchor which
     points to some content in the document. This is a requirement of exporting
     to Word
    */
    ww8::Frames SwPosFlyFramesToFrames(const SwPosFlyFrames &rFlys)
    {
        ww8::Frames aRet;

        for(const auto& rFly : rFlys)
        {
            const SwFrameFormat &rEntry = rFly.GetFormat();
            const SwFormat* pParent = rEntry.DerivedFrom();
            const SwFormatAnchor& rAnchor = rEntry.GetAnchor();
            // keep only Inline Heading frames from the frames anchored as characters
            bool bAsChar = rAnchor.GetAnchorId() ==
                    static_cast<RndStdIds>(css::text::TextContentAnchorType_AS_CHARACTER);
            if ( bAsChar &&
                    !(pParent && pParent->GetPoolFormatId() == RES_POOLFRM_INLINE_HEADING) )
            {
                continue;
            }
            if (const SwNode* pAnchor = rAnchor.GetAnchorNode())
            {
                // the anchor position will be invalidated by SetRedlineFlags
                // so set a dummy position and fix it in UpdateFramePositions
                SwPosition const dummy(pAnchor->GetNodes());
                aRet.emplace_back(rEntry, dummy);
            }
            else
            {
                SwPosition aPos(rFly.GetNode());
                aRet.emplace_back(rEntry, aPos);
            }
        }
        return aRet;
    }

    //Utility to test if a frame is anchored at a given node
    class anchoredto
    {
    private:
        const SwNode& mrNode;
    public:
        explicit anchoredto(const SwNode& rNode) : mrNode(rNode) {}
        bool operator()(const ww8::Frame &rFrame) const
        {
            return (mrNode == rFrame.GetPosition().GetNode());
        }
    };
}

namespace ww8
{
    //For i120928,size conversion before exporting graphic of bullet
    Frame::Frame(const Graphic &rGrf, SwPosition aPos)
        : mpFlyFrame(nullptr)
        , maPos(std::move(aPos))
        , meWriterType(eBulletGrf)
        , mpStartFrameContent(nullptr)
        , mbIsInline(true)
        , mbForBullet(true)
        , maGrf(rGrf)
    {
        const MapMode aMap100mm( MapUnit::Map100thMM );
        Size    aSize( rGrf.GetPrefSize() );
        if ( MapUnit::MapPixel == rGrf.GetPrefMapMode().GetMapUnit() )
        {
            aSize = Application::GetDefaultDevice()->PixelToLogic(aSize, aMap100mm );
        }
        else
        {
            aSize = OutputDevice::LogicToLogic( aSize,rGrf.GetPrefMapMode(), aMap100mm );
        }
        maSize = aSize;
        maLayoutSize = maSize;
    }

    Frame::Frame(const SwFrameFormat &rFormat, SwPosition aPos)
        : mpFlyFrame(&rFormat)
        , maPos(std::move(aPos))
        , meWriterType(eTextBox)
        , mpStartFrameContent(nullptr)
        // #i43447# - move to initialization list
        , mbIsInline( (rFormat.GetAnchor().GetAnchorId() == RndStdIds::FLY_AS_CHAR) )
        // #i120928# - handle graphic of bullet within existing implementation
        , mbForBullet(false)
    {
        switch (rFormat.Which())
        {
            case RES_FLYFRMFMT:
                if (const SwNodeIndex* pIdx = rFormat.GetContent().GetContentIdx())
                {
                    SwNodeIndex aIdx(*pIdx, 1);
                    const SwNode &rNd = aIdx.GetNode();
                    // #i43447# - determine layout size
                    {
                        SwRect aLayRect( rFormat.FindLayoutRect() );
                        tools::Rectangle aRect( aLayRect.SVRect() );
                        // The Object is not rendered (e.g. something in unused
                        // header/footer) - thus, get the values from the format.
                        if ( aLayRect.IsEmpty() )
                        {
                            aRect.SetSize( rFormat.GetFrameSize().GetSize() );
                        }
                        maLayoutSize = aRect.GetSize();
                    }
                    switch (rNd.GetNodeType())
                    {
                        case SwNodeType::Grf:
                            meWriterType = eGraphic;
                            maSize = rNd.GetNoTextNode()->GetTwipSize();
                            break;
                        case SwNodeType::Ole:
                            meWriterType = eOle;
                            maSize = rNd.GetNoTextNode()->GetTwipSize();
                            break;
                        default:
                            meWriterType = eTextBox;
                            // #i43447# - Size equals layout size for text boxes
                            maSize = maLayoutSize;
                            break;
                    }
                    mpStartFrameContent = &rNd;
                }
                else
                {
                    OSL_ENSURE(false, "Impossible");
                    meWriterType = eTextBox;
                }
                break;
            default:
                if (const SdrObject* pObj = rFormat.FindRealSdrObject())
                {
                    if (pObj->GetObjInventor() == SdrInventor::FmForm)
                        meWriterType = eFormControl;
                    else
                        meWriterType = eDrawing;
                    maSize = pObj->GetSnapRect().GetSize();
                    maLayoutSize = maSize;
                }
                else
                {
                    OSL_ENSURE(false, "Impossible");
                    meWriterType = eDrawing;
                }
                break;
        }
    }


    void Frame::ForceTreatAsInline()
    {
        mbIsInline = true;
    }
}

namespace sw
{
    namespace hack
    {

        sal_uInt16 TransformWhichBetweenPools(const SfxItemPool &rDestPool,
            const SfxItemPool &rSrcPool, sal_uInt16 nWhich)
        {
            sal_uInt16 nSlotId = rSrcPool.GetSlotId(nWhich);
            if (IsValidSlotWhich(nSlotId, nWhich))
                nWhich = rDestPool.GetWhichIDFromSlotID(nSlotId);
            else
                nWhich = 0;
            return nWhich;
        }

        sal_uInt16 GetSetWhichFromSwDocWhich(const SfxItemSet &rSet,
            const SwDoc &rDoc, sal_uInt16 nWhich)
        {
            if (RES_WHICHHINT_END < rSet.GetRanges()[0].first)
            {
                nWhich = TransformWhichBetweenPools(*rSet.GetPool(),
                    rDoc.GetAttrPool(), nWhich);
            }
            return nWhich;
        }

        DrawingOLEAdaptor::DrawingOLEAdaptor(SdrOle2Obj &rObj,
            SfxObjectShell &rPers)
            : mxIPRef(rObj.GetObjRef()), mrPers(rPers),
            mpGraphic( rObj.GetGraphic() )
        {
            rObj.AbandonObject();
        }

        bool DrawingOLEAdaptor::TransferToDoc( OUString &rName )
        {
            OSL_ENSURE(mxIPRef.is(), "Transferring invalid object to doc");
            if (!mxIPRef.is())
                return false;

            uno::Reference < container::XChild > xChild( mxIPRef, uno::UNO_QUERY );
            if ( xChild.is() )
                xChild->setParent( mrPers.GetModel() );

            bool bSuccess = mrPers.GetEmbeddedObjectContainer().InsertEmbeddedObject( mxIPRef, rName );
            if (bSuccess)
            {
                if ( mpGraphic )
                    ::svt::EmbeddedObjectRef::SetGraphicToContainer( *mpGraphic,
                                                                    mrPers.GetEmbeddedObjectContainer(),
                                                                    rName,
                                                                    OUString() );

                mxIPRef = nullptr;
            }

            return bSuccess;
        }

        DrawingOLEAdaptor::~DrawingOLEAdaptor()
        {
            if (!mxIPRef.is())
                return;

            OSL_ENSURE( !mrPers.GetEmbeddedObjectContainer().HasEmbeddedObject( mxIPRef ), "Object in adaptor is inserted?!" );
            try
            {
                mxIPRef->close(true);
            }
            catch ( const css::util::CloseVetoException& )
            {
            }

            mxIPRef = nullptr;
        }
    }

    namespace util
    {
        SwTwips MakeSafePositioningValue(SwTwips nIn)
        {
            if (nIn > SHRT_MAX)
                nIn = SHRT_MAX;
            else if (nIn < SHRT_MIN)
                nIn = SHRT_MIN;
            return nIn;
        }

        void SetLayer::SendObjectToHell(SdrObject &rObject) const
        {
            SetObjectLayer(rObject, eHell);
        }

        void SetLayer::SendObjectToHeaven(SdrObject &rObject) const
        {
            SetObjectLayer(rObject, eHeaven);
        }

        void SetLayer::SetObjectLayer(SdrObject &rObject, Layer eLayer) const
        {
            if (SdrInventor::FmForm == rObject.GetObjInventor())
                rObject.SetLayer(mnFormLayer);
            else
            {
                switch (eLayer)
                {
                    case eHeaven:
                        rObject.SetLayer(mnHeavenLayer);
                        break;
                    case eHell:
                        rObject.SetLayer(mnHellLayer);
                        break;
                }
            }
        }

        //SetLayer boilerplate begin

        // #i38889# - by default put objects into the invisible layers.
        SetLayer::SetLayer(const SwDoc &rDoc)
            : mnHeavenLayer(rDoc.getIDocumentDrawModelAccess().GetInvisibleHeavenId()),
              mnHellLayer(rDoc.getIDocumentDrawModelAccess().GetInvisibleHellId()),
              mnFormLayer(rDoc.getIDocumentDrawModelAccess().GetInvisibleControlsId())
        {
        }
        //SetLayer boilerplate end

        void GetPoolItems(const SfxItemSet &rSet, ww8::PoolItems &rItems, bool bExportParentItemSet )
        {
            if( bExportParentItemSet )
            {
                for (SfxItemIter aIter(rSet); !aIter.IsAtEnd(); aIter.NextItem())
                {
                    const SfxPoolItem* pItem(nullptr);
                    if(SfxItemState::SET == aIter.GetItemState(true, &pItem))
                        rItems[aIter.GetCurWhich()] = pItem;
                }
            }
            else if( rSet.Count())
            {
                for (SfxItemIter aIter(rSet); !aIter.IsAtEnd(); aIter.NextItem())
                    rItems[aIter.GetCurWhich()] = aIter.GetCurItem();
            }
//            DeduplicateItems(rItems);
        }

        const SfxPoolItem *SearchPoolItems(const ww8::PoolItems &rItems,
            sal_uInt16 eType)
        {
            auto aIter = rItems.find(eType);
            if (aIter != rItems.end())
                return aIter->second;
            return nullptr;
        }

        void ClearOverridesFromSet(const SwFormatCharFormat &rFormat, SfxItemSet &rSet)
        {
            if (const SwCharFormat* pCharFormat = rFormat.GetCharFormat())
            {
                if (pCharFormat->GetAttrSet().Count())
                {
                    SfxItemIter aIter(pCharFormat->GetAttrSet());
                    const SfxPoolItem *pItem = aIter.GetCurItem();
                    do
                        rSet.ClearItem(pItem->Which());
                    while ((pItem = aIter.NextItem()));
                }
            }
        }

        ww8::ParaStyles GetParaStyles(const SwDoc &rDoc)
        {
            ww8::ParaStyles aStyles;
            typedef ww8::ParaStyles::size_type mysizet;

            const SwTextFormatColls *pColls = rDoc.GetTextFormatColls();
            mysizet nCount = pColls ? pColls->size() : 0;
            aStyles.reserve(nCount);
            for (mysizet nI = 0; nI < nCount; ++nI)
                aStyles.push_back((*pColls)[ static_cast< sal_uInt16 >(nI) ]);
            return aStyles;
        }

        SwTextFormatColl* GetParaStyle(SwDoc &rDoc, const UIName& rName)
        {
            // Search first in the Doc-Styles
            SwTextFormatColl* pColl = rDoc.FindTextFormatCollByName(rName);
            if (!pColl)
            {
                // Collection not found, try in Pool ?
                sal_uInt16 n = SwStyleNameMapper::GetPoolIdFromUIName(rName,
                    SwGetPoolIdFromName::TxtColl);
                if (n != SAL_MAX_UINT16)       // found or standard
                    pColl = rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool(n, false);
            }
            return pColl;
        }

        SwCharFormat* GetCharStyle(SwDoc &rDoc, const UIName& rName)
        {
            SwCharFormat *pFormat = rDoc.FindCharFormatByName(rName);
            if (!pFormat)
            {
                // Collection not found, try in Pool ?
                sal_uInt16 n = SwStyleNameMapper::GetPoolIdFromUIName(rName,
                    SwGetPoolIdFromName::ChrFmt);
                if (n != SAL_MAX_UINT16)       // found or standard
                    pFormat = rDoc.getIDocumentStylePoolAccess().GetCharFormatFromPool(n);
            }
            return pFormat;
        }

        // #i98791# - adjust sorting algorithm
        void SortByAssignedOutlineStyleListLevel(ww8::ParaStyles &rStyles)
        {
            std::sort(rStyles.begin(), rStyles.end(), outlinecmp());
        }

        /*
           Utility to extract FlyFormats from a document, potentially from a
           selection.
           */
        ww8::Frames GetFrames(const SwDoc &rDoc, SwPaM const *pPaM /*, bool bAll*/)
        {
            SwPosFlyFrames aFlys(rDoc.GetAllFlyFormats(pPaM, /*bDrawAlso=*/true, /*bAsCharAlso=*/true));
            ww8::Frames aRet(SwPosFlyFramesToFrames(aFlys));
            return aRet;
        }

        void UpdateFramePositions(ww8::Frames & rFrames)
        {
            for (ww8::Frame & rFrame : rFrames)
            {
                SwFormatAnchor const& rAnchor = rFrame.GetFrameFormat().GetAnchor();
                if (SwPosition const*const pAnchor = rAnchor.GetContentAnchor())
                {
                    rFrame.SetPosition(*pAnchor);
                }
                else
                {   // these don't need to be corrected, they're not in redlines
                    assert(RndStdIds::FLY_AT_PAGE == rAnchor.GetAnchorId());
                }
            }
        }

        ww8::Frames GetFramesInNode(const ww8::Frames &rFrames, const SwNode &rNode)
        {
            ww8::Frames aRet;
            std::copy_if(rFrames.begin(), rFrames.end(),
                std::back_inserter(aRet), anchoredto(rNode));
            return aRet;
        }

        const SwNumFormat* GetNumFormatFromSwNumRuleLevel(const SwNumRule &rRule,
            int nLevel)
        {
            if (nLevel < 0 || nLevel >= MAXLEVEL)
            {
                OSL_FAIL("Invalid level");
                return nullptr;
            }
            return &(rRule.Get( static_cast< sal_uInt16 >(nLevel) ));
        }

        const SwNumFormat* GetNumFormatFromTextNode(const SwTextNode &rTextNode)
        {
            const SwNumRule *pRule = nullptr;
            if (
                rTextNode.IsNumbered() && rTextNode.IsCountedInList() &&
                nullptr != (pRule = rTextNode.GetNumRule())
                )
            {
                return GetNumFormatFromSwNumRuleLevel(*pRule,
                    rTextNode.GetActualListLevel());
            }

            if (
                rTextNode.IsNumbered() && rTextNode.IsCountedInList() &&
                nullptr != (pRule = rTextNode.GetDoc().GetOutlineNumRule())
                )
            {
                return GetNumFormatFromSwNumRuleLevel(*pRule,
                    rTextNode.GetActualListLevel());
            }

            return nullptr;
        }

        const SwNumRule* GetNumRuleFromTextNode(const SwTextNode &rTextNode)
        {
            return GetNormalNumRuleFromTextNode(rTextNode);
        }

        const SwNumRule* GetNormalNumRuleFromTextNode(const SwTextNode &rTextNode)
        {
            const SwNumRule *pRule = nullptr;

            if (
                rTextNode.IsNumbered() && rTextNode.IsCountedInList() &&
                nullptr != (pRule = rTextNode.GetNumRule())
               )
            {
                return pRule;
            }
            return nullptr;
        }

        SwNoTextNode *GetNoTextNodeFromSwFrameFormat(const SwFrameFormat &rFormat)
        {
            const SwNodeIndex *pIndex = rFormat.GetContent().GetContentIdx();
            OSL_ENSURE(pIndex, "No NodeIndex in SwFrameFormat ?, suspicious");
            if (!pIndex)
                return nullptr;
            SwNodeIndex aIdx(*pIndex, 1);
            return aIdx.GetNode().GetNoTextNode();
        }

        bool HasPageBreak(const SwNode &rNd)
        {
            const SvxFormatBreakItem *pBreak = nullptr;
            if (rNd.IsTableNode() && rNd.GetTableNode())
            {
                const SwTable& rTable = rNd.GetTableNode()->GetTable();
                const SwFrameFormat* pApply = rTable.GetFrameFormat();
                OSL_ENSURE(pApply, "impossible");
                if (pApply)
                    pBreak = &pApply->GetFormatAttr(RES_BREAK);
            }
            else if (const SwContentNode *pNd = rNd.GetContentNode())
                pBreak = &pNd->GetAttr(RES_BREAK);

            return pBreak && pBreak->GetBreak() == SvxBreak::PageBefore;
        }

        tools::Polygon PolygonFromPolyPolygon(const tools::PolyPolygon &rPolyPoly)
        {
            if(1 == rPolyPoly.Count())
            {
                return rPolyPoly[0];
            }
            else
            {
                // This method will now just concatenate the polygons contained
                // in the given PolyPolygon. Anything else which might be thought of
                // for reducing to a single polygon will just need more power and
                // cannot create more correct results.
                sal_uInt32 nPointCount(0);

                for( auto const& rPoly : rPolyPoly )
                {
                    nPointCount += static_cast<sal_uInt32>(rPoly.GetSize());
                }

                if(nPointCount > 0x0000ffff)
                {
                    OSL_FAIL("PolygonFromPolyPolygon: too many points for a single polygon (!)");
                    nPointCount = 0x0000ffff;
                }

                tools::Polygon aRetval(o3tl::narrowing<sal_uInt16>(nPointCount));
                sal_uInt32 nAppendIndex(0);

                for( auto const& rCandidate : rPolyPoly )
                {
                    for(sal_uInt16 b(0); nAppendIndex <= nPointCount && b < rCandidate.GetSize(); b++)
                    {
                        aRetval[o3tl::narrowing<sal_uInt16>(nAppendIndex++)] = rCandidate[b];
                    }
                }

                return aRetval;
            }
        }

        tools::Polygon CorrectWordWrapPolygonForExport(const tools::PolyPolygon& rPolyPoly, const SwNoTextNode* pNd, bool bCorrectCrop)
        {
            tools::Polygon aPoly(PolygonFromPolyPolygon(rPolyPoly));
            const Size aOrigSize = pNd->GetGraphic().GetPrefSize();

            const SwAttrSet* pAttrSet = pNd->GetpSwAttrSet();
            if (bCorrectCrop && pAttrSet)
            {
                if (pAttrSet->HasItem(RES_GRFATR_CROPGRF))
                {
                    // Word's wrap polygon deals with a canvas which has the size of the already
                    // cropped graphic, do the opposite of correctCrop() in writerfilter/.
                    const SwCropGrf& rCrop = pAttrSet->GetCropGrf();
                    sal_Int32 nCropLeft = convertTwipToMm100(rCrop.GetLeft());
                    sal_Int32 nCropRight = convertTwipToMm100(rCrop.GetRight());
                    sal_Int32 nCropTop = convertTwipToMm100(rCrop.GetTop());
                    sal_Int32 nCropBottom = convertTwipToMm100(rCrop.GetBottom());
                    aPoly.Move(-nCropLeft, -nCropTop);

                    Fraction aScaleX(aOrigSize.getWidth(), aOrigSize.getWidth() - nCropLeft - nCropRight);
                    Fraction aScaleY(aOrigSize.getHeight(), aOrigSize.getHeight() - nCropTop - nCropBottom);
                    aPoly.Scale(double(aScaleX), double(aScaleY));
                }
            }

            Fraction aMapPolyX(ww::nWrap100Percent, aOrigSize.Width());
            Fraction aMapPolyY(ww::nWrap100Percent, aOrigSize.Height());
            aPoly.Scale(double(aMapPolyX), double(aMapPolyY));

            /*
             a) stretch right bound by 15twips
             b) shrink bottom bound to where it would have been in word
             c) Move it to the left by 15twips

             See the import for details
            */
            const Size aSize = pNd->GetTwipSize();
            Fraction aMoveHack(ww::nWrap100Percent, aSize.Width());
            aMoveHack *= Fraction(15, 1);
            tools::Long nMove(aMoveHack);

            Fraction aHackX(ww::nWrap100Percent + nMove,
                    ww::nWrap100Percent);
            Fraction aHackY(ww::nWrap100Percent - nMove,
                    ww::nWrap100Percent);
            aPoly.Scale(double(aHackX), double(aHackY));

            aPoly.Move(-nMove, 0);
            return aPoly;
        }

        void RedlineStack::open(const SwPosition& rPos, const SfxPoolItem& rAttr)
        {
            OSL_ENSURE(rAttr.Which() == RES_FLTR_REDLINE, "not a redline");
            maStack.emplace_back(new SwFltStackEntry(rPos, std::unique_ptr<SfxPoolItem>(rAttr.Clone())));
        }

        namespace {

        class SameOpenRedlineType
        {
        private:
            RedlineType meType;
        public:
            explicit SameOpenRedlineType(RedlineType eType) : meType(eType) {}
            bool operator()(const std::unique_ptr<SwFltStackEntry> & pEntry) const
            {
                const SwFltRedline *pTest = static_cast<const SwFltRedline *>
                    (pEntry->m_pAttr.get());
                return (pEntry->m_bOpen && (pTest->m_eType == meType));
            }
        };

        }

        bool RedlineStack::close(const SwPosition& rPos, RedlineType eType)
        {
            //Search from end for same type
            auto aResult = std::find_if(maStack.rbegin(), maStack.rend(),
                SameOpenRedlineType(eType));
            if (aResult != maStack.rend())
            {
                SwTextNode *const pNode(rPos.GetNode().GetTextNode());
                sal_Int32 const nIndex(rPos.GetContentIndex());
                // HACK to prevent overlap of field-mark and redline,
                // which would destroy field-mark invariants when the redline
                // is hidden: move the redline end one to the left
                if (pNode && nIndex > 0
                    && pNode->GetText()[nIndex - 1] == CH_TXT_ATR_FIELDEND)
                {
                    SwPosition const end(*rPos.GetNode().GetTextNode(),
                                         nIndex - 1);
                    sw::mark::Fieldmark *const pFieldMark(
                        rPos.GetDoc().getIDocumentMarkAccess()->getFieldmarkAt(end));
                    SAL_WARN_IF(!pFieldMark, "sw.ww8", "expected a field mark");
                    if (pFieldMark && pFieldMark->GetMarkPos().GetNodeIndex() == (*aResult)->m_aMkPos.m_nNode.GetIndex()+1
                        && pFieldMark->GetMarkPos().GetContentIndex() < (*aResult)->m_aMkPos.m_nContent)
                    {
                        (*aResult)->SetEndPos(end);
                        return true;
                    }
                }
                (*aResult)->SetEndPos(rPos);
                return true;
            }
            return false;
        }

        void RedlineStack::closeall(const SwPosition& rPos)
        {
            std::for_each(maStack.begin(), maStack.end(), SetEndIfOpen(rPos));
        }

        void MoveAttrFieldmarkInserted(SwFltPosition& rMkPos, SwFltPosition& rPtPos, const SwPosition& rPos)
        {
            sal_Int32 const nInserted = 2; // CH_TXT_ATR_FIELDSTART, CH_TXT_ATR_FIELDSEP
            SwNodeOffset nPosNd = rPos.GetNodeIndex();
            sal_Int32 nPosCt = rPos.GetContentIndex() - nInserted;

            bool const isPoint(rMkPos == rPtPos);
            if ((rMkPos.m_nNode.GetIndex()+1 == nPosNd) &&
                (nPosCt <= rMkPos.m_nContent))
            {
                rMkPos.m_nContent += nInserted;
                SAL_WARN_IF(rMkPos.m_nContent > rPos.GetNodes()[nPosNd]->GetContentNode()->Len(),
                        "sw.ww8", "redline ends after end of line");
                if (isPoint) // sigh ... important special case...
                {
                    rPtPos.m_nContent += nInserted;
                    return;
                }
            }
            // for the end position, leave it alone if it's *on* the dummy
            // char position, that should remain *before*
            if ((rPtPos.m_nNode.GetIndex()+1 == nPosNd) &&
                (nPosCt < rPtPos.m_nContent))
            {
                rPtPos.m_nContent += nInserted;
                SAL_WARN_IF(rPtPos.m_nContent > rPos.GetNodes()[nPosNd]->GetContentNode()->Len(),
                        "sw.ww8", "range ends after end of line");
            }
        }

        void RedlineStack::MoveAttrsFieldmarkInserted(const SwPosition& rPos)
        {
            for (size_t i = 0, nCnt = maStack.size(); i < nCnt; ++i)
            {
                SwFltStackEntry& rEntry = *maStack[i];
                MoveAttrFieldmarkInserted(rEntry.m_aMkPos, rEntry.m_aPtPos, rPos);
            }
        }

        void SetInDocAndDelete::operator()(std::unique_ptr<SwFltStackEntry>& pEntry)
        {
            SwPaM aRegion(pEntry->m_aMkPos.m_nNode);
            if (pEntry->MakeRegion(aRegion,
                    SwFltStackEntry::RegionMode::CheckNodes|SwFltStackEntry::RegionMode::CheckFieldmark) &&
                (*aRegion.GetPoint() != *aRegion.GetMark())
            )
            {
                mrDoc.getIDocumentRedlineAccess().SetRedlineFlags(RedlineFlags::On | RedlineFlags::ShowInsert |
                                         RedlineFlags::ShowDelete);
                const SwFltRedline *pFltRedline = static_cast<const SwFltRedline*>
                    (pEntry->m_pAttr.get());

                SwRedlineData aData(pFltRedline->m_eType, pFltRedline->m_nAutorNo,
                        pFltRedline->m_aStamp, 0, OUString(), nullptr);

                SwRangeRedline *const pNewRedline(new SwRangeRedline(aData, aRegion));
                // the point node may be deleted in AppendRedline, so park
                // the PaM somewhere safe
                aRegion.DeleteMark();
                aRegion.GetPoint()->Assign(*mrDoc.GetNodes()[SwNodeOffset(0)]);
                mrDoc.getIDocumentRedlineAccess().AppendRedline(pNewRedline, true);
                mrDoc.getIDocumentRedlineAccess().SetRedlineFlags(RedlineFlags::NONE | RedlineFlags::ShowInsert |
                     RedlineFlags::ShowDelete );
            }
            pEntry.reset();
        }

        bool CompareRedlines::operator()(const std::unique_ptr<SwFltStackEntry> & pOneE,
            const std::unique_ptr<SwFltStackEntry> & pTwoE) const
        {
            const SwFltRedline *pOne= static_cast<const SwFltRedline*>
                (pOneE->m_pAttr.get());
            const SwFltRedline *pTwo= static_cast<const SwFltRedline*>
                (pTwoE->m_pAttr.get());

            //Return the earlier time, if two have the same time, prioritize
            //inserts over deletes
            if (pOne->m_aStamp == pTwo->m_aStamp)
                return (pOne->m_eType == RedlineType::Insert && pTwo->m_eType != RedlineType::Insert);
            else
                return (pOne->m_aStamp < pTwo->m_aStamp);
        }

        void RedlineStack::ImplDestroy()
        {
            std::stable_sort(maStack.begin(), maStack.end(), CompareRedlines());
            std::for_each(maStack.begin(), maStack.end(), SetInDocAndDelete(mrDoc));
        }

        RedlineStack::~RedlineStack()
        {
            suppress_fun_call_w_exception(ImplDestroy());
        }

        sal_uInt16 WrtRedlineAuthor::AddName( const OUString& rNm )
        {
            sal_uInt16 nRet;
            auto aIter = std::find(maAuthors.begin(), maAuthors.end(), rNm);
            if (aIter != maAuthors.end())
                nRet = static_cast< sal_uInt16 >(aIter - maAuthors.begin());
            else
            {
                nRet = static_cast< sal_uInt16 >(maAuthors.size());
                maAuthors.push_back(rNm);
            }
            return nRet;
        }
    }

    namespace util
    {
        InsertedTableListener::InsertedTableListener(SwTableNode& rNode)
            : m_pTableNode(&rNode)
        {
            StartListening(rNode.GetNotifier());
        }

        SwTableNode* InsertedTableListener::GetTableNode()
            { return m_pTableNode; }

        void  InsertedTableListener::Notify(const SfxHint& rHint)
        {
            if(rHint.GetId() == SfxHintId::Dying)
                m_pTableNode = nullptr;
        }

        InsertedTablesManager::InsertedTablesManager(const SwDoc &rDoc)
            : mbHasRoot(rDoc.getIDocumentLayoutAccess().GetCurrentLayout())
        { }

        void InsertedTablesManager::DelAndMakeTableFrames()
        {
            if (!mbHasRoot)
                return;
            for (auto& aTable : maTables)
            {
                // If already a layout exists, then the BoxFrames must recreated at this table
                SwTableNode *pTable = aTable.first->GetTableNode();
                OSL_ENSURE(pTable, "Why no expected table");
                if (pTable)
                {
                    SwFrameFormat * pFrameFormat = pTable->GetTable().GetFrameFormat();

                    if (pFrameFormat != nullptr)
                    {
                        SwPosition *pIndex = aTable.second;
                        pTable->DelFrames();
                        pTable->MakeOwnFrames(pIndex);
                    }
                }
            }
        }

        void InsertedTablesManager::InsertTable(SwTableNode& rTableNode, SwPaM& rPaM)
        {
            if (!mbHasRoot)
                return;
            //Associate this tablenode with this after position, replace an old
            //node association if necessary
            maTables.emplace(
                    std::unique_ptr<InsertedTableListener>(new InsertedTableListener(rTableNode)),
                    rPaM.GetPoint());
        }
    }

}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
