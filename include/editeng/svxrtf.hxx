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

#ifndef INCLUDED_EDITENG_SVXRTF_HXX
#define INCLUDED_EDITENG_SVXRTF_HXX

#include <svl/itemset.hxx>
#include <svtools/parrtf.hxx>
#include <rtl/ustring.hxx>
#include <tools/color.hxx>
#include <vcl/font.hxx>

#include <editeng/editengdllapi.h>

#include <optional>
#include <vector>
#include <map>
#include <memory>

class SvxRTFItemStackType;
using SvxRTFItemStackList = std::vector<std::unique_ptr<SvxRTFItemStackType>>;

// Mapper-Classes for the various requirements on Document positions
//        Swg - NodePosition is a SwContentIndex, which is used internally
// EditEngine - ULONG to list of paragraphs

class EditEngine;
class ContentNode;
class EditNodeIdx
{
public:
    EditNodeIdx(EditEngine* pEE, ContentNode* pNd);
    sal_Int32   GetIdx() const;
    ContentNode* GetNode() { return mpNode; }
private:
    EditEngine*   mpEditEngine;
    ContentNode*  mpNode;
};

class EditSelection;
class EditPosition
{
private:
    EditEngine*     mpEditEngine;
    EditSelection*  mpCurSel;

public:
    EditPosition(EditEngine* pIEE, EditSelection* pSel);

    sal_Int32   GetNodeIdx() const;
    sal_Int32   GetCntIdx() const;

    // clone NodeIndex
    EditNodeIdx MakeNodeIdx() const;
};

typedef std::map<short, vcl::Font> SvxRTFFontTbl;

// own helper classes for the RTF Parser
struct SvxRTFStyleType
{
    SfxItemSet aAttrSet;        // the attributes of Style (+ derivated!)
    OUString sName;
    sal_uInt16 nBasedOn;
    sal_uInt8 nOutlineNo;

    SvxRTFStyleType(SfxItemPool& rPool, const WhichRangesContainer& pWhichRange);
};

typedef std::map<sal_uInt16, SvxRTFStyleType> SvxRTFStyleTbl;

class EDITENG_DLLPUBLIC SvxRTFParser : public SvRTFParser
{
    std::vector<Color>    maColorTable;
    SvxRTFFontTbl         m_FontTable;
    SvxRTFStyleTbl        m_StyleTable;
    std::deque< std::unique_ptr<SvxRTFItemStackType> >  aAttrStack;
    SvxRTFItemStackList   m_AttrSetList;

    struct PlainOrPardMap
    {
        std::map<sal_uInt16, sal_uInt16> data;
        template<class T>
        TypedWhichId<T> operator[](TypedWhichId<T> in) const { return TypedWhichId<T>(data.at(in)); }
        template<class T>
        void set(TypedWhichId<T> in, TypedWhichId<T> out) { data[in] = out; }
    };
    PlainOrPardMap aPlainMap;
    PlainOrPardMap aPardMap;
    WhichRangesContainer aWhichMap;

    std::optional<EditPosition> mxInsertPosition;
    SfxItemPool* pAttrPool;
    std::optional<Color>  mxDefaultColor;
    std::optional<vcl::Font>   pDfltFont;
    std::unique_ptr<SfxItemSet> pRTFDefaults;

    int     nDfltFont;

    bool    bNewDoc : 1;            // sal_False - Reading in an existing
    bool    bNewGroup : 1;          // sal_True - there was an opening parenthesis
    bool    bIsSetDfltTab : 1;      // sal_True - DefTab was loaded
    bool    bChkStyleAttr : 1;      // sal_True - StyleSheets are evaluated
    bool    bCalcValue : 1;         // sal_True - Twip values adapt to App
    bool    bIsLeftToRightDef : 1;  // sal_True - in LeftToRight char run def.
                                        // sal_False - in RightToLeft char run def.
    bool    bIsInReadStyleTab : 1;  // sal_True - in ReadStyleTable

    SvxRTFParser(SvxRTFParser const&) = delete;
    void operator=(SvxRTFParser const&) = delete;

    void ClearColorTbl();
    void ClearAttrStack();

    SvxRTFItemStackType* GetAttrSet_();  // Create new ItemStackType:s
    void ClearStyleAttr_( SvxRTFItemStackType& rStkType );

    // Sets all the attributes that are different from the current
    void SetAttrSet( SvxRTFItemStackType &rSet );
    void SetDefault( int nToken, int nValue );

    // Execute pard / plain
    void RTFPardPlain( bool bPard, SfxItemSet** ppSet );

    void BuildWhichTable();

    enum RTF_CharTypeDef
    {
        NOTDEF_CHARTYPE,
        LOW_CHARTYPE,
        HIGH_CHARTYPE,
        DOUBLEBYTE_CHARTYPE
    };

        // set latin/asian/complex character attributes
    void SetScriptAttr(
        RTF_CharTypeDef eType, SfxItemSet& rSet, SfxPoolItem& rItem );

protected:
    virtual void InsertPara() = 0;

    static void DelCharAtEnd( OUStringBuffer& rStr, const sal_Unicode cDel );

    // is called for each token that is recognized in CallParser
    virtual void NextToken( int nToken ) override;

    void ReadStyleTable();
    void ReadColorTable();
    void ReadFontTable();
    void ReadAttr( int nToken, SfxItemSet* pSet );
    void ReadTabAttr( int nToken, SfxItemSet& rSet );

    inline SfxItemSet& GetAttrSet();
    // no text yet inserted? (SttPos from the top stack entry!)
    bool IsAttrSttPos();
    void AttrGroupEnd();        // edit the current, delete from stack
    void SetAllAttrOfStk();     // end all Attr. and set it into doc


    virtual void InsertText() = 0;
    virtual void MovePos( bool bForward = true ) = 0;
    virtual void SetEndPrevPara( std::optional<EditNodeIdx>& rpNodePos,
                                 sal_Int32& rCntPos )=0;
    virtual void SetAttrInDoc( SvxRTFItemStackType &rSet );
    // for Tokens, which are not evaluated in ReadAttr
    virtual void UnknownAttrToken( int nToken );

    // if no-one would like to have any twips
    virtual void CalcValue();

    SvxRTFParser( SfxItemPool& rAttrPool, SvStream& rIn );
    virtual ~SvxRTFParser() override;

    void SetNewDoc( bool bFlag )        { bNewDoc = bFlag; }
    bool IsChkStyleAttr() const         { return bChkStyleAttr; }
    void SetChkStyleAttr( bool bFlag )  { bChkStyleAttr = bFlag; }
    bool IsCalcValue() const            { return bCalcValue; }
    void SetCalcValue( bool bFlag )     { bCalcValue = bFlag; }

    // Query/Set the current insert position
    void SetInsPos( const EditPosition& rNew );
    SvxRTFStyleTbl& GetStyleTbl()               { return m_StyleTable; }

public:

    virtual SvParserState CallParser() override;

    inline const Color& GetColor( size_t nId ) const;
    const vcl::Font& GetFont( sal_uInt16 nId );      // Changes the default Font

    virtual bool IsEndPara( EditNodeIdx* pNd, sal_Int32 nCnt ) const = 0;

    // to set a different attribute pool. May only be done prior to CallParser!
    // The maps are not generated anew!
    void SetAttrPool( SfxItemPool* pNewPool )   { pAttrPool = pNewPool; }
    // to set different WhichIds for a different pool.
    template<class T>
    void SetPardMap(TypedWhichId<T> wid, TypedWhichId<T> widTrue) { aPardMap.set(wid, widTrue); }
    // to be able to assign them from the outside as for example table cells
    void ReadBorderAttr( int nToken, SfxItemSet& rSet, bool bTableDef=false );
    void ReadBackgroundAttr( int nToken, SfxItemSet& rSet, bool bTableDef=false  );

    // for asynchronous read from the SvStream
    virtual void Continue( int nToken ) override;

    // get RTF default ItemSets. Must be used by pard/plain tokens or in
    // reset of Style-Items
    const SfxItemSet& GetRTFDefaults();
};

// The stack for the attributes:
// this class may only be used by SvxRTFParser!
class SvxRTFItemStackType
{
    friend class SvxRTFParser;

    SfxItemSet   aAttrSet;
    std::optional<EditNodeIdx>  mxStartNodeIdx;
    std::optional<EditNodeIdx>  mxEndNodeIdx;
    sal_Int32    nSttCnt, nEndCnt;
    SvxRTFItemStackList maChildList;
    sal_uInt16   nStyleNo;

    SvxRTFItemStackType(SvxRTFItemStackType const&) = delete;
    void operator=(SvxRTFItemStackType const&) = delete;

    SvxRTFItemStackType( SfxItemPool&, const WhichRangesContainer& pWhichRange,
                            const EditPosition& );

    static std::unique_ptr<SvxRTFItemStackType> createSvxRTFItemStackType(
        SfxItemPool&, const WhichRangesContainer& pWhichRange, const EditPosition&);

    void Add(std::unique_ptr<SvxRTFItemStackType>);
    void Compress( const SvxRTFParser& );
    void DropChildList();

public:
    SvxRTFItemStackType(const SvxRTFItemStackType&, const EditPosition&,
                        bool bCopyAttr);
    ~SvxRTFItemStackType();
    //cmc, I'm very suspicious about SetStartPos, it doesn't change
    //its children's starting position, and the implementation looks
    //bad, consider this deprecated.
    void SetStartPos( const EditPosition& rPos );

    const EditNodeIdx& GetSttNode() const { return *mxStartNodeIdx; }
    const EditNodeIdx& GetEndNode() const { return *mxEndNodeIdx; }

    sal_Int32 GetSttCnt() const { return nSttCnt; }
    sal_Int32 GetEndCnt() const { return nEndCnt; }

          SfxItemSet& GetAttrSet()          { return aAttrSet; }
    const SfxItemSet& GetAttrSet() const    { return aAttrSet; }

    sal_uInt16 StyleNo() const  { return nStyleNo; }

    void SetRTFDefaults( const SfxItemSet& rDefaults );
};


// ----------- Inline Implementations --------------

inline const Color& SvxRTFParser::GetColor( size_t nId ) const
{
    if( nId < maColorTable.size() )
        return maColorTable[ nId ];
    return *mxDefaultColor;
}

inline SfxItemSet& SvxRTFParser::GetAttrSet()
{
    SvxRTFItemStackType* pTmp;
    if( bNewGroup || aAttrStack.empty() )
        pTmp = GetAttrSet_();
    else
    {
        pTmp = aAttrStack.back().get();
        if ( pTmp == nullptr )
            pTmp = GetAttrSet_();
    }
    return pTmp->aAttrSet;
}


#endif // INCLUDED_EDITENG_SVXRTF_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
