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

#pragma once

#include <sal/types.h>

#include "global.hxx"
#include "rangelst.hxx"
#include <memory>

class ScDocument;
class ScTableProtectionImpl;

enum ScPasswordHash
{
    PASSHASH_SHA1 = 0,
    PASSHASH_SHA1_UTF8, // tdf#115483 this is UTF8, previous one is wrong UTF16
    PASSHASH_SHA256,
    PASSHASH_XL,
    PASSHASH_UNSPECIFIED
};

/// OOXML password definitions: algorithmName, hashValue, saltValue, spinCount
struct ScOoxPasswordHash
{
    OUString    maAlgorithmName;    /// "SHA-512", ...
    OUString    maHashValue;        /// base64 encoded hash value
    OUString    maSaltValue;        /// base64 encoded salt value
    sal_uInt32  mnSpinCount;        /// spin count, iteration runs

    ScOoxPasswordHash() : mnSpinCount(0) {}
    bool hasPassword() const { return !maHashValue.isEmpty(); }
    void clear()
    {
        // Keep algorithm and spin count.
        maHashValue.clear();
        maSaltValue.clear();
    }
    bool verifyPassword( std::u16string_view aPassText ) const;
};

namespace ScPassHashHelper
{
    /** Check for the compatibility of all password hashes.  If there is at
     * least one hash that needs to be regenerated, it returns true.  If all
     * hash values are compatible with the specified hash type, then it
     * returns false. */
    bool needsPassHashRegen(const ScDocument& rDoc, ScPasswordHash eHash1, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED);

    const OUString & getHashURI(ScPasswordHash eHash);

    ScPasswordHash getHashTypeFromURI(std::u16string_view rURI);
}

class SAL_NO_VTABLE ScPassHashProtectable
{
public:
    virtual ~ScPassHashProtectable() = 0;

    virtual bool isProtected() const = 0;
    virtual bool isProtectedWithPass() const = 0;
    virtual void setProtected(bool bProtected) = 0;

    virtual bool isPasswordEmpty() const = 0;
    virtual bool hasPasswordHash(ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) const = 0;
    virtual void setPassword(const OUString& aPassText) = 0;
    virtual css::uno::Sequence<sal_Int8> getPasswordHash(
        ScPasswordHash eHash, ScPasswordHash eHas2 = PASSHASH_UNSPECIFIED) const = 0;
    virtual const ScOoxPasswordHash& getPasswordHash() const = 0;
    virtual void setPasswordHash(
        const css::uno::Sequence<sal_Int8>& aPassword,
        ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) = 0;
    virtual void setPasswordHash( const OUString& rAlgorithmName, const OUString& rHashValue,
            const OUString& rSaltValue, sal_uInt32 nSpinCount ) = 0;
    virtual bool verifyPassword(std::u16string_view aPassText) const = 0;
};

class SAL_DLLPUBLIC_RTTI ScDocProtection final : public ScPassHashProtectable
{
public:
    enum Option
    {
        STRUCTURE = 0,
        WINDOWS,
        NONE        ///< last item - used to resize the vector
    };

    SC_DLLPUBLIC explicit ScDocProtection();
    explicit ScDocProtection(const ScDocProtection& r);
    SC_DLLPUBLIC virtual ~ScDocProtection() override;

    SC_DLLPUBLIC virtual bool isProtected() const override;
    virtual bool isProtectedWithPass() const override;
    SC_DLLPUBLIC virtual void setProtected(bool bProtected) override;

    virtual bool isPasswordEmpty() const override;
    virtual bool hasPasswordHash(ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) const override;
    virtual void setPassword(const OUString& aPassText) override;
    SC_DLLPUBLIC virtual css::uno::Sequence<sal_Int8> getPasswordHash(
        ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) const override;
    virtual const ScOoxPasswordHash& getPasswordHash() const override;
    SC_DLLPUBLIC virtual void setPasswordHash(
        const css::uno::Sequence<sal_Int8>& aPassword,
        ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) override;
    virtual void setPasswordHash( const OUString& rAlgorithmName, const OUString& rHashValue,
            const OUString& rSaltValue, sal_uInt32 nSpinCount ) override;
    SC_DLLPUBLIC virtual bool verifyPassword(std::u16string_view aPassText) const override;

    SC_DLLPUBLIC bool isOptionEnabled(Option eOption) const;
    SC_DLLPUBLIC void setOption(Option eOption, bool bEnabled);

private:
    std::unique_ptr<ScTableProtectionImpl> mpImpl;
};

/** Container for the Excel EnhancedProtection feature.
 */
struct ScEnhancedProtection
{
    ScRangeListRef              maRangeList;
    sal_uInt32                  mnAreserved;
    sal_uInt32                  mnPasswordVerifier;
    OUString                    maTitle;
    ::std::vector< sal_uInt8 >  maSecurityDescriptor;       // imported as raw BIFF data
    OUString                    maSecurityDescriptorXML;    // imported from OOXML
    ScOoxPasswordHash           maPasswordHash;

    ScEnhancedProtection() : mnAreserved(0), mnPasswordVerifier(0) {}

    bool hasSecurityDescriptor() const
    {
        return !maSecurityDescriptor.empty() || !maSecurityDescriptorXML.isEmpty();
    }

    bool hasPassword() const
    {
        return mnPasswordVerifier != 0 || maPasswordHash.hasPassword();
    }
};

/** sheet protection state container
 *
 * This class stores sheet's protection state: 1) whether the protection
 * is on, 2) password and/or password hash, and 3) any associated
 * protection options.  This class is also used as a protection state
 * container for the undo/redo stack, in which case the password, hash and
 * the options need to be preserved even when the protection flag is
 * off. */
class SC_DLLPUBLIC ScTableProtection final : public ScPassHashProtectable
{
public:
    enum Option
    {
        AUTOFILTER = 0,
        DELETE_COLUMNS,
        DELETE_ROWS,
        FORMAT_CELLS,
        FORMAT_COLUMNS,
        FORMAT_ROWS,
        INSERT_COLUMNS,
        INSERT_HYPERLINKS,
        INSERT_ROWS,
        OBJECTS,
        PIVOT_TABLES,
        SCENARIOS,
        SELECT_LOCKED_CELLS,
        SELECT_UNLOCKED_CELLS,
        SORT,
        NONE        ///< last item - used to resize the vector
    };

    explicit ScTableProtection();
    explicit ScTableProtection(const ScTableProtection& r);
    virtual ~ScTableProtection() override;

    virtual bool isProtected() const override;
    virtual bool isProtectedWithPass() const override;
    virtual void setProtected(bool bProtected) override;

    virtual bool isPasswordEmpty() const override;
    virtual bool hasPasswordHash(ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) const override;
    virtual void setPassword(const OUString& aPassText) override;
    virtual css::uno::Sequence<sal_Int8> getPasswordHash(
        ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) const override;
    virtual const ScOoxPasswordHash& getPasswordHash() const override;
    virtual void setPasswordHash(
        const css::uno::Sequence<sal_Int8>& aPassword,
        ScPasswordHash eHash, ScPasswordHash eHash2 = PASSHASH_UNSPECIFIED) override;
    virtual void setPasswordHash( const OUString& rAlgorithmName, const OUString& rHashValue,
            const OUString& rSaltValue, sal_uInt32 nSpinCount ) override;
    virtual bool verifyPassword(std::u16string_view aPassText) const override;

    bool isOptionEnabled(Option eOption) const;
    void setOption(Option eOption, bool bEnabled);

    void setEnhancedProtection( ::std::vector< ScEnhancedProtection > && rProt );
    const ::std::vector< ScEnhancedProtection > & getEnhancedProtection() const;
    bool updateReference( UpdateRefMode, const ScDocument&, const ScRange& rWhere, SCCOL nDx, SCROW nDy, SCTAB nDz );
    bool isBlockEditable( const ScRange& rRange ) const;
    bool isSelectionEditable( const ScRangeList& rRangeList ) const;

private:
    std::unique_ptr<ScTableProtectionImpl> mpImpl;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
