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

#ifndef INCLUDED_COMPHELPER_DOCPASSWORDHELPER_HXX
#define INCLUDED_COMPHELPER_DOCPASSWORDHELPER_HXX

#include <comphelper/comphelperdllapi.h>
#include <string_view>
#include <vector>
#include <comphelper/docpasswordrequest.hxx>
#include <comphelper/hash.hxx>

namespace com::sun::star::task { class XInteractionHandler; }
namespace com::sun::star::beans { struct PropertyValue; }
namespace com::sun::star::beans { struct NamedValue; }

namespace comphelper {

enum class DocPasswordVerifierResult
{
    OK,
    WrongPassword,
    Abort
};


/** Base class for a password verifier used by the DocPasswordHelper class
    below.

    Users have to implement the virtual functions and pass an instance of the
    verifier to one of the password request functions.
 */
class COMPHELPER_DLLPUBLIC IDocPasswordVerifier
{
public:
    virtual             ~IDocPasswordVerifier();

    /** Will be called every time a password needs to be verified.

        @param rPassword
            The password to be verified

        @param o_rEncryptionData
            Output parameter, that is filled with the EncryptionData generated
            from the password. The data is filled only if the validation was
            successful.

        @return  The result of the verification.
            - DocPasswordVerifierResult_OK, if and only if the passed password
              is valid and can be used to process the related document.
            - DocPasswordVerifierResult_WRONG_PASSWORD, if the password is
              wrong. The user may be asked again for a new password.
            - DocPasswordVerifierResult_ABORT, if an unrecoverable error
              occurred while password verification. The password request loop
              will be aborted.
     */
    virtual DocPasswordVerifierResult verifyPassword( const OUString& rPassword, css::uno::Sequence< css::beans::NamedValue >& o_rEncryptionData ) = 0;

    /** Will be called every time an encryption data needs to be verified.

        @param rEncryptionData
            The data will be validated

        @return  The result of the verification.
            - DocPasswordVerifierResult_OK, if and only if the passed encryption data
              is valid and can be used to process the related document.
            - DocPasswordVerifierResult_WRONG_PASSWORD, if the encryption data is
              wrong.
            - DocPasswordVerifierResult_ABORT, if an unrecoverable error
              occurred while data verification. The password request loop
              will be aborted.
     */
    virtual DocPasswordVerifierResult verifyEncryptionData( const css::uno::Sequence< css::beans::NamedValue >& o_rEncryptionData ) = 0;

};


/** Helper that asks for a document password and checks its validity.
 */
class COMPHELPER_DLLPUBLIC DocPasswordHelper
{
public:


    /** This helper function generates the information related
        to "Password to modify" provided by user. The result
        sequence contains the hash and the algorithm-related
        info.

        @param aString
            The string for which the info should be generated

        @return
            The sequence containing the hash and the algorithm-related info
      */

    static css::uno::Sequence< css::beans::PropertyValue >
        GenerateNewModifyPasswordInfo( std::u16string_view aPassword );

    /** This helper function converts a grab-bagged password, e.g. the
        trackChanges password which has no complete inner equivalent to
        the inner format. The result sequence contains the hash and the
        algorithm-related info to use e.g. in IsModifyPasswordCorrect().

        @param aInfo
            The sequence containing the hash and the algorithm-related info
            according to the OOXML origin, used by grab-bagging.

        @return
            The sequence containing the hash and the algorithm-related info
            in the inner format.
      */

    static css::uno::Sequence< css::beans::PropertyValue > ConvertPasswordInfo(
        const css::uno::Sequence< css::beans::PropertyValue >& aInfo );

    static css::uno::Sequence<css::beans::PropertyValue>
    GenerateNewModifyPasswordInfoOOXML(std::u16string_view aPassword);

    /** This helper function allows to check whether
        the "Password to modify" provided by user is the correct one.

        @param aString
            The string containing the provided password

        @param aInfo
            The sequence containing the hash and the algorithm-info

        @return
            <TRUE/> if the password is correct one
            <FALSE/> otherwise
      */

    static bool IsModifyPasswordCorrect(
                std::u16string_view aPassword,
                const css::uno::Sequence< css::beans::PropertyValue >& aInfo );


    /** This helper function generates the hash code based on the algorithm
        specified by MS for "Password to modify" feature of Word.

        @param aString
            The string for which the hash should be calculated

        @return
            The hash represented by sal_uInt32
      */

    static sal_uInt32 GetWordHashAsUINT32( std::u16string_view aString );


    /** This helper function generates the hash code based on the algorithm
        specified by MS for "Password to modify" and passwords related to
        table protection of Excel.

        @param aString
            The string for which the hash should be calculated

        @param nEnc
            The encoding that should be used to generate the 8-bit string
            before the hash is generated

        @return
            The hash represented by sal_uInt16
      */

    static sal_uInt16 GetXLHashAsUINT16(
                std::u16string_view aString,
                rtl_TextEncoding nEnc = RTL_TEXTENCODING_UTF8 );


    /** This helper function generates the hash code based on the algorithm
        specified by MS for "Password to modify" and passwords related to
        table protection.

        @param aString
            The string for which the hash should be calculated

        @return
            The hash represented by sequence of bytes in BigEndian form
      */

    static css::uno::Sequence< sal_Int8 > GetXLHashAsSequence(
                std::u16string_view aString );


    /** Convenience function to calculate a salted hash with iterations as
        specified in https://msdn.microsoft.com/en-us/library/dd920692 for the
        OOXML sheetProtection and fileSharing elements, or
        https://msdn.microsoft.com/en-us/library/dd924776 and
        https://msdn.microsoft.com/en-us/library/dd925430 for Standard and
        Agile Encryption.

        @param  rPassword
                UTF-16 encoded string without leading BOM character

        @param  rSaltValue
                Base64 encoded salt that will be decoded and prepended to password
                data.

        @param  nSpinCount
                If >0 the number of repeated iterations.

        @param  eIterCount
                If Hash::IterCount::APPEND, append iteration count as per
                https://msdn.microsoft.com/en-us/library/dd920692
                If Hash::IterCount::PREPEND, prepend iteration count as per
                https://msdn.microsoft.com/en-us/library/dd924776 and
                https://msdn.microsoft.com/en-us/library/dd925430
                If Hash::IterCount::NONE, do not add the iteration count to hash
                iterations.

        @param  rAlgorithmName
                One of "SHA-512", "SHA-256", ... as listed for AlgorithmName in
                https://msdn.microsoft.com/en-us/library/dd920692
                or "SHA512", "SHA256", ... as listed for HashAlgorithm in
                https://msdn.microsoft.com/en-us/library/dd925810
                that have a valid match in comphelper::HashType. If not, an
                empty sequence is returned. Not all algorithm names are
                supported.

        @return the raw hash value as sal_Int8 sequence.
     */
    static css::uno::Sequence<sal_Int8> GetOoxHashAsSequence(
            std::u16string_view rPassword,
            std::u16string_view rSaltValue,
            sal_uInt32 nSpinCount,
            comphelper::Hash::IterCount eIterCount,
             std::u16string_view rAlgorithmName);


    /** Convenience function to calculate a salted hash with iterations as
        specified in https://msdn.microsoft.com/en-us/library/dd920692 for the
        OOXML sheetProtection and fileSharing elements, or
        https://msdn.microsoft.com/en-us/library/dd924776 and
        https://msdn.microsoft.com/en-us/library/dd925430 for Standard and
        Agile Encryption.

        @param  rPassword
                UTF-16 encoded string without leading BOM character

        @param  rSaltValue
                Base64 encoded salt that will be decoded and prepended to password
                data.

        @param  nSpinCount
                If >0 the number of repeated iterations.

        @param  eIterCount
                If Hash::IterCount::APPEND, append iteration count as per
                https://msdn.microsoft.com/en-us/library/dd920692
                If Hash::IterCount::PREPEND, prepend iteration count as per
                https://msdn.microsoft.com/en-us/library/dd924776 and
                https://msdn.microsoft.com/en-us/library/dd925430
                If Hash::IterCount::NONE, do not add the iteration count to hash
                iterations.

        @param  rAlgorithmName
                One of "SHA-512", "SHA-256", ... as listed for AlgorithmName in
                https://msdn.microsoft.com/en-us/library/dd920692
                or "SHA512", "SHA256", ... as listed for HashAlgorithm in
                https://msdn.microsoft.com/en-us/library/dd925810
                that have a valid match in comphelper::HashType. If not, an
                empty sequence is returned. Not all algorithm names are
                supported.

        @return the base64 encoded string of the hash value, that can be
                compared against a stored base64 encoded hash value.
     */
    static OUString GetOoxHashAsBase64(
            std::u16string_view rPassword,
            std::u16string_view rSaltValue,
            sal_uInt32 nSpinCount,
            comphelper::Hash::IterCount eIterCount,
            std::u16string_view rAlgorithmName);


    /** Convenience function to calculate a salted hash with iterations as
        specified in https://msdn.microsoft.com/en-us/library/dd920692 for the
        OOXML sheetProtection and fileSharing elements, or
        https://msdn.microsoft.com/en-us/library/dd924776 and
        https://msdn.microsoft.com/en-us/library/dd925430 for Standard and
        Agile Encryption.

        @param  rPassword
                UTF-16 encoded string without leading BOM character

        @param  rSaltValue
                A raw salt that will be prepended to password data.

        @param  nSpinCount
                If >0 the number of repeated iterations.

        @param  eIterCount
                If Hash::IterCount::APPEND, append iteration count as per
                https://msdn.microsoft.com/en-us/library/dd920692
                If Hash::IterCount::PREPEND, prepend iteration count as per
                https://msdn.microsoft.com/en-us/library/dd924776 and
                https://msdn.microsoft.com/en-us/library/dd925430
                If Hash::IterCount::NONE, do not add the iteration count to hash
                iterations.

        @param  rAlgorithmName
                One of "SHA-512", "SHA-256", ... as listed for AlgorithmName in
                https://msdn.microsoft.com/en-us/library/dd920692
                or "SHA512", "SHA256", ... as listed for HashAlgorithm in
                https://msdn.microsoft.com/en-us/library/dd925810
                that have a valid match in comphelper::HashType. If not, an
                empty sequence is returned. Not all algorithm names are
                supported.

        @return the raw the hash value.
     */
    static std::vector<unsigned char> GetOoxHashAsVector(
            std::u16string_view rPassword,
            const std::vector<unsigned char>& rSaltValue,
            sal_uInt32 nSpinCount,
            comphelper::Hash::IterCount eIterCount,
            std::u16string_view rAlgorithmName);


    /** This helper function generates a random sequence of bytes of
        requested length.
      */

    static css::uno::Sequence< sal_Int8 > GenerateRandomByteSequence(
                sal_Int32 nLength );


    /** This helper function generates a byte sequence representing the
        key digest value used by MSCodec_Std97 codec.
      */

    static css::uno::Sequence< sal_Int8 > GenerateStd97Key(
                std::u16string_view aPassword,
                const css::uno::Sequence< sal_Int8 >& aDocId );


    /** This helper function generates a byte sequence representing the
        key digest value used by MSCodec_Std97 codec.
      */

    static css::uno::Sequence< sal_Int8 > GenerateStd97Key(
                const sal_uInt16 pPassData[16],
                const css::uno::Sequence< sal_Int8 >& aDocId );

    /** This helper function generates a byte sequence representing the
        key digest value used by MSCodec_Std97 codec.
      */

    static css::uno::Sequence< sal_Int8 > GenerateStd97Key(
                const sal_uInt16 pPassData[16],
                const sal_uInt8 pDocId[16] );


    /** This helper function tries to request and verify a password to load a
        protected document.

        First, the list of default passwords will be tried if provided. This is
        needed by import filters for external file formats that have to check a
        predefined password in some cases without asking the user for a
        password. Every password is checked using the passed password verifier.

        If not successful, the passed password of a medium is tried, that has
        been set e.g. by an API call to load a document. If existing, the
        password is checked using the passed password verifier.

        If still not successful, the passed interaction handler is used to
        request a password from the user. This will be repeated until the
        passed password verifier validates the entered password, or if the user
        chooses to cancel password input.

        @param rVerifier
            The password verifier used to check every processed password.

        @param rMediaPassword
            If not empty, will be passed to the password validator before
            requesting a password from the user. This password usually should
            be queried from a media descriptor.

        @param rxInteractHandler
            The interaction handler that will be used to request a password
            from the user, e.g. by showing a password input dialog.

        @param rDocumentUrl
            The URL of the related document that will be shown in the password
            input dialog.

        @param eRequestType
            The password request type that will be passed to the
            DocPasswordRequest object created internally. See
            docpasswordrequest.hxx for more details.

        @param pDefaultPasswords
            If not null, contains default passwords that will be tried before a
            password will be requested from the media descriptor or the user.

        @param pbIsDefaultPassword
            (output parameter) If not null, the type of the found password will
            be returned. True means the password has been found in the passed
            list of default passwords. False means the password has been taken
            from the rMediaPassword parameter or has been entered by the user.

        @return
            If not empty, contains the password that has been validated by the
            passed password verifier. If empty, no valid password has been
            found, or the user has chosen to cancel password input.
     */
    static css::uno::Sequence< css::beans::NamedValue > requestAndVerifyDocPassword(
                            IDocPasswordVerifier& rVerifier,
                            const css::uno::Sequence< css::beans::NamedValue >& rMediaEncData,
                            const OUString& rMediaPassword,
                            const css::uno::Reference< css::task::XInteractionHandler >& rxInteractHandler,
                            const OUString& rDocumentUrl,
                            DocPasswordRequestType eRequestType,
                            const ::std::vector< OUString >* pDefaultPasswords = nullptr,
                            bool* pbIsDefaultPassword = nullptr );

    static css::uno::Sequence< css::beans::NamedValue > decryptGpgSession(
        const css::uno::Sequence< css::uno::Sequence< css::beans::NamedValue > >& rGpgProperties);

private:
                        ~DocPasswordHelper();
};


} // namespace comphelper

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
