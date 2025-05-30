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
#ifndef INCLUDED_EDITENG_HYPHENZONEITEM_HXX
#define INCLUDED_EDITENG_HYPHENZONEITEM_HXX

#include <svl/poolitem.hxx>
#include <editeng/editengdllapi.h>

// class SvxHyphenZoneItem -----------------------------------------------

/*  [Description]

    This item describes a hyphenation attribute  (automatic?, number of
    characters at the end of the line and start).
*/

class EDITENG_DLLPUBLIC SvxHyphenZoneItem final : public SfxPoolItem
{
    bool      bHyphen  : 1;
    bool      bKeep : 1;        // avoid hyphenation across page etc., see ParagraphHyphenationKeep
    bool      bNoCapsHyphenation : 1;
    bool      bNoLastWordHyphenation : 1;
    sal_uInt8 nMinLead;
    sal_uInt8 nMinTrail;
    sal_uInt8 nMaxHyphens;      // max. consecutive lines with hyphenation
    sal_uInt8 nMinWordLength;   // hyphenate only words with at least nMinWordLength characters
    sal_uInt16 nTextHyphenZone; // don't force hyphenation at line end, allow this extra white space
    sal_uInt16 nTextHyphenZoneAlways; // don't force hyphenation at paragraph end, allow this extra white space
    sal_uInt16 nTextHyphenZoneColumn; // don't force hyphenation at column end, allow this extra white space
    sal_uInt16 nTextHyphenZonePage;   // don't force hyphenation at page end, allow this extra white space
    sal_uInt16 nTextHyphenZoneSpread; // don't force hyphenation at spread end, allow this extra white space
    sal_uInt8 nKeepType;        // avoid hyphenation across page etc., see ParagraphHyphenationKeep
    bool      bKeepLine : 1;    // if bKeep, shift the hyphenated word (true), or the full line
    sal_uInt8 nCompoundMinLead; // min. characters between compound word boundary and hyphenation

public:
    static SfxPoolItem* CreateDefault();

    DECLARE_ITEM_TYPE_FUNCTION(SvxHyphenZoneItem)
    SvxHyphenZoneItem( const bool bHyph /*= false*/,
                       const sal_uInt16 nId  );

    // "pure virtual Methods" from SfxPoolItem
    virtual bool            operator==( const SfxPoolItem& ) const override;
    virtual bool            QueryValue( css::uno::Any& rVal, sal_uInt8 nMemberId = 0 ) const override;
    virtual bool            PutValue( const css::uno::Any& rVal, sal_uInt8 nMemberId ) override;

    virtual bool GetPresentation( SfxItemPresentation ePres,
                                  MapUnit eCoreMetric,
                                  MapUnit ePresMetric,
                                  OUString &rText, const IntlWrapper& ) const override;

    virtual SvxHyphenZoneItem* Clone( SfxItemPool *pPool = nullptr ) const override;

    void SetHyphen( const bool bNew ) { bHyphen = bNew; }
    bool IsHyphen() const { return bHyphen; }

    void SetKeep( const bool bNew ) { bKeep = bNew; }
    bool IsKeep() const { return bKeep; }

    void SetNoCapsHyphenation( const bool bNew ) { bNoCapsHyphenation = bNew; }
    bool IsNoCapsHyphenation() const { return bNoCapsHyphenation; }
    void SetNoLastWordHyphenation( const bool bNew ) { bNoLastWordHyphenation = bNew; }
    bool IsNoLastWordHyphenation() const { return bNoLastWordHyphenation; }

    sal_uInt8 &GetMinLead() { return nMinLead; }
    sal_uInt8 GetMinLead() const { return nMinLead; }

    sal_uInt8 &GetMinTrail() { return nMinTrail; }
    sal_uInt8 GetMinTrail() const { return nMinTrail; }

    sal_uInt8 &GetCompoundMinLead() { return nCompoundMinLead; }
    sal_uInt8 GetCompoundMinLead() const { return nCompoundMinLead; }

    sal_uInt8 &GetMaxHyphens() { return nMaxHyphens; }
    sal_uInt8 GetMaxHyphens() const { return nMaxHyphens; }

    sal_uInt8 &GetMinWordLength() { return nMinWordLength; }
    sal_uInt8 GetMinWordLength() const { return nMinWordLength; }

    sal_uInt16 &GetTextHyphenZone() { return nTextHyphenZone; }
    sal_uInt16 GetTextHyphenZone() const { return nTextHyphenZone; }

    sal_uInt16 &GetTextHyphenZoneAlways() { return nTextHyphenZoneAlways; }
    sal_uInt16 GetTextHyphenZoneAlways() const { return nTextHyphenZoneAlways; }

    sal_uInt16 &GetTextHyphenZoneColumn() { return nTextHyphenZoneColumn; }
    sal_uInt16 GetTextHyphenZoneColumn() const { return nTextHyphenZoneColumn; }

    sal_uInt16 &GetTextHyphenZonePage() { return nTextHyphenZonePage; }
    sal_uInt16 GetTextHyphenZonePage() const { return nTextHyphenZonePage; }

    sal_uInt16 &GetTextHyphenZoneSpread() { return nTextHyphenZoneSpread; }
    sal_uInt16 GetTextHyphenZoneSpread() const { return nTextHyphenZoneSpread; }

    sal_uInt8 &GetKeepType() { return nKeepType; }
    sal_uInt8 GetKeepType() const { return nKeepType; }

    void SetKeepLine( const bool bNew ) { bKeepLine = bNew; }
    bool IsKeepLine() const { return bKeepLine; }
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
