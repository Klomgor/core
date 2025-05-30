/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <pdf/PDFEncryptorR6.hxx>
#include <pdf/EncryptionHashTransporter.hxx>
#include <pdf/pdfwriter_impl.hxx>
#include <comphelper/crypto/Crypto.hxx>
#include <comphelper/hash.hxx>
#include <comphelper/random.hxx>

using namespace css;

namespace vcl::pdf
{
namespace
{
constexpr size_t IV_SIZE = 16;
constexpr size_t BLOCK_SIZE = 16;
constexpr size_t KEY_SIZE = 32;
constexpr size_t SALT_SIZE = 8;

/** Calculates modulo 3 of the 128-bit integer, using the first 16 bytes of the vector */
sal_Int32 calculateModulo3(std::vector<sal_uInt8> const& rInput)
{
    sal_Int32 nSum = 0;
    for (size_t i = 0; i < 16; ++i)
        nSum += rInput[i];
    return nSum % 3;
}

void generateBytes(std::vector<sal_uInt8>& rBytes, size_t nSize)
{
    rBytes.resize(nSize);

    for (size_t i = 0; i < rBytes.size(); ++i)
        rBytes[i] = sal_uInt8(comphelper::rng::uniform_uint_distribution(0, 0xFF));
}

} // end anonymous

std::vector<sal_uInt8> generateKey()
{
    std::vector<sal_uInt8> aKey;
    generateBytes(aKey, KEY_SIZE);
    return aKey;
}

bool validateUserPassword(const sal_uInt8* pPass, size_t nLength, std::vector<sal_uInt8>& U)
{
    std::vector<sal_uInt8> aHash(U.begin(), U.begin() + KEY_SIZE);
    std::vector<sal_uInt8> aValidationSalt(U.begin() + KEY_SIZE, U.begin() + KEY_SIZE + SALT_SIZE);
    std::vector<sal_uInt8> aCalculatedHash
        = vcl::pdf::computeHashR6(pPass, nLength, aValidationSalt);
    return aHash == aCalculatedHash;
}

bool validateOwnerPassword(const sal_uInt8* pPass, size_t nLength, const std::vector<sal_uInt8>& U,
                           std::vector<sal_uInt8>& O)
{
    std::vector<sal_uInt8> aHash(O.begin(), O.begin() + KEY_SIZE);
    std::vector<sal_uInt8> aValidationSalt(O.begin() + KEY_SIZE, O.begin() + KEY_SIZE + SALT_SIZE);
    std::vector<sal_uInt8> aCalculatedHash
        = vcl::pdf::computeHashR6(pPass, nLength, aValidationSalt, U);
    return aHash == aCalculatedHash;
}

/** Algorithm 8 */
void generateUandUE(const sal_uInt8* pPass, size_t nLength,
                    std::vector<sal_uInt8>& rFileEncryptionKey, std::vector<sal_uInt8>& U,
                    std::vector<sal_uInt8>& UE)
{
    std::vector<sal_uInt8> aValidationSalt;
    generateBytes(aValidationSalt, SALT_SIZE);
    std::vector<sal_uInt8> aKeySalt;
    generateBytes(aKeySalt, SALT_SIZE);

    U = vcl::pdf::computeHashR6(pPass, nLength, aValidationSalt);
    U.insert(U.end(), aValidationSalt.begin(), aValidationSalt.end());
    U.insert(U.end(), aKeySalt.begin(), aKeySalt.end());

    std::vector<sal_uInt8> aKeyHash = vcl::pdf::computeHashR6(pPass, nLength, aKeySalt);
    std::vector<sal_uInt8> iv(IV_SIZE, 0); // zero IV
    UE = std::vector<sal_uInt8>(rFileEncryptionKey.size(), 0);
    comphelper::Encrypt aEncrypt(aKeyHash, iv, comphelper::CryptoType::AES_256_CBC);
    aEncrypt.update(UE, rFileEncryptionKey);
}

/** Algorithm 9 */
void generateOandOE(const sal_uInt8* pPass, size_t nLength,
                    std::vector<sal_uInt8>& rFileEncryptionKey, const std::vector<sal_uInt8>& U,
                    std::vector<sal_uInt8>& O, std::vector<sal_uInt8>& OE)
{
    std::vector<sal_uInt8> aValidationSalt;
    generateBytes(aValidationSalt, SALT_SIZE);
    std::vector<sal_uInt8> aKeySalt;
    generateBytes(aKeySalt, SALT_SIZE);

    O = vcl::pdf::computeHashR6(pPass, nLength, aValidationSalt, U);
    O.insert(O.end(), aValidationSalt.begin(), aValidationSalt.end());
    O.insert(O.end(), aKeySalt.begin(), aKeySalt.end());

    std::vector<sal_uInt8> aKeyHash = vcl::pdf::computeHashR6(pPass, nLength, aKeySalt, U);
    std::vector<sal_uInt8> iv(IV_SIZE, 0); // zero IV
    OE = std::vector<sal_uInt8>(rFileEncryptionKey.size(), 0);
    comphelper::Encrypt aEncrypt(aKeyHash, iv, comphelper::CryptoType::AES_256_CBC);
    aEncrypt.update(OE, rFileEncryptionKey);
}

/** Algorithm 8 step b) */
std::vector<sal_uInt8> decryptKey(const sal_uInt8* pPass, size_t nLength, std::vector<sal_uInt8>& U,
                                  std::vector<sal_uInt8>& UE)
{
    std::vector<sal_uInt8> aKeySalt(U.begin() + KEY_SIZE + SALT_SIZE,
                                    U.begin() + KEY_SIZE + SALT_SIZE + SALT_SIZE);

    auto aKeyHash = vcl::pdf::computeHashR6(pPass, nLength, aKeySalt);

    std::vector<sal_uInt8> aEncryptedKey(UE.begin(), UE.begin() + KEY_SIZE);
    std::vector<sal_uInt8> iv(IV_SIZE, 0);

    comphelper::Decrypt aDecryptCBC(aKeyHash, iv, comphelper::CryptoType::AES_256_CBC);
    std::vector<sal_uInt8> aFileEncryptionKey(aEncryptedKey.size());
    sal_uInt32 nDecrypted = aDecryptCBC.update(aFileEncryptionKey, aEncryptedKey);
    if (nDecrypted == 0)
        return std::vector<sal_uInt8>();
    return aFileEncryptionKey;
}

/** Algorithm 13: Validating the permissions */
std::vector<sal_uInt8> decryptPerms(std::vector<sal_uInt8>& rPermsEncrypted,
                                    std::vector<sal_uInt8>& rFileEncryptionKey)
{
    std::vector<sal_uInt8> aPermsDecrpyted(rPermsEncrypted.size());
    std::vector<sal_uInt8> iv(IV_SIZE, 0);
    comphelper::Decrypt aDecryptor(rFileEncryptionKey, iv, comphelper::CryptoType::AES_256_ECB);
    aDecryptor.update(aPermsDecrpyted, rPermsEncrypted);
    return aPermsDecrpyted;
}

/** Algorithm 10 step f) */
std::vector<sal_uInt8> encryptPerms(std::vector<sal_uInt8>& rPerms,
                                    std::vector<sal_uInt8>& rFileEncryptionKey)
{
    std::vector<sal_uInt8> aPermsEncrypted(rPerms.size());
    std::vector<sal_uInt8> iv(IV_SIZE, 0);
    comphelper::Encrypt aEncryptor(rFileEncryptionKey, iv, comphelper::CryptoType::AES_256_ECB);
    aEncryptor.update(aPermsEncrypted, rPerms);
    return aPermsEncrypted;
}

/** Algorithm 10 steps a) - e) */
std::vector<sal_uInt8> createPerms(sal_Int32 nAccessPermissions, bool bEncryptMetadata)
{
    std::vector<sal_uInt8> aPermsCreated;
    generateBytes(aPermsCreated, 16); // fill with random data - mainly for last 4 bytes
    aPermsCreated[0] = sal_uInt8(nAccessPermissions);
    aPermsCreated[1] = sal_uInt8(nAccessPermissions >> 8);
    aPermsCreated[2] = sal_uInt8(nAccessPermissions >> 16);
    aPermsCreated[3] = sal_uInt8(nAccessPermissions >> 24);
    aPermsCreated[4] = sal_uInt8(0xff);
    aPermsCreated[5] = sal_uInt8(0xff);
    aPermsCreated[6] = sal_uInt8(0xff);
    aPermsCreated[7] = sal_uInt8(0xff);
    aPermsCreated[8] = bEncryptMetadata ? 'T' : 'F'; // Encrypt metadata
    aPermsCreated[9] = 'a';
    aPermsCreated[10] = 'd';
    aPermsCreated[11] = 'b';
    return aPermsCreated;
}

/** Algorithm 2.B: Computing a hash (revision 6 and later) */
std::vector<sal_uInt8> computeHashR6(const sal_uInt8* pPassword, size_t nPasswordLength,
                                     std::vector<sal_uInt8> const& rValidationSalt,
                                     std::vector<sal_uInt8> const& rUserKey)
{
    // Round 0
    comphelper::Hash aHash(comphelper::HashType::SHA256);
    aHash.update(pPassword, nPasswordLength);
    aHash.update(rValidationSalt);
    if (!rUserKey.empty()) // if calculating owner key
        aHash.update(rUserKey);

    std::vector<sal_uInt8> K = aHash.finalize();

    std::vector<sal_uInt8> E;

    sal_Int32 nRound = 1; // round 0 is done already
    do
    {
        // Step a)
        std::vector<sal_uInt8> K1;
        for (sal_Int32 nRepetition = 0; nRepetition < 64; ++nRepetition)
        {
            K1.insert(K1.end(), pPassword, pPassword + nPasswordLength);
            K1.insert(K1.end(), K.begin(), K.end());
            if (!rUserKey.empty()) // if calculating owner key
                K1.insert(K1.end(), rUserKey.begin(), rUserKey.end());
        }

        // Step b)
        std::vector<sal_uInt8> aKey(K.begin(), K.begin() + 16);
        std::vector<sal_uInt8> aInitVector(K.begin() + 16, K.end());

        E = std::vector<sal_uInt8>(K1.size(), 0);

        comphelper::Encrypt aEncrypt(aKey, aInitVector, comphelper::CryptoType::AES_128_CBC);
        aEncrypt.update(E, K1);

        // Step c)
        sal_Int32 nModulo3Result = calculateModulo3(E);

        // Step d)
        comphelper::HashType eType;
        switch (nModulo3Result)
        {
            case 0:
                eType = comphelper::HashType::SHA256;
                break;
            case 1:
                eType = comphelper::HashType::SHA384;
                break;
            default:
                eType = comphelper::HashType::SHA512;
                break;
        }
        K = comphelper::Hash::calculateHash(E.data(), E.size(), eType);

        nRound++;
    }
    // Step e) and f)
    // We stop iteration if we do at least 64 rounds and (the last element of E <= round number - 32)
    while (nRound <= 64 || E.back() > (nRound - 32));

    // Output - first 32 bytes
    return std::vector<sal_uInt8>(K.begin(), K.begin() + 32);
}

size_t addPaddingToVector(std::vector<sal_uInt8>& rVector, size_t nBlockSize)
{
    size_t nPaddedSize = comphelper::roundUp(rVector.size(), size_t(nBlockSize));
    if (nPaddedSize > rVector.size())
    {
        sal_uInt8 nPaddedValue = sal_uInt8(nPaddedSize - rVector.size());
        rVector.resize(nPaddedSize, nPaddedValue);
    }
    return nPaddedSize;
}

class VCL_DLLPUBLIC EncryptionContext
{
private:
    std::vector<sal_uInt8> maKey;

public:
    EncryptionContext(std::vector<sal_uInt8> const& rKey)
        : maKey(rKey)
    {
    }

    /** Algorithm 1.A: Encryption of data using the AES algorithms */
    void encrypt(const void* pInput, sal_uInt64 nInputSize, std::vector<sal_uInt8>& rOutput,
                 std::vector<sal_uInt8>& rIV)
    {
        comphelper::Encrypt aEncrypt(maKey, rIV, comphelper::CryptoType::AES_256_CBC);
        const sal_uInt8* pInputBytes = static_cast<const sal_uInt8*>(pInput);
        std::vector<sal_uInt8> aInput(pInputBytes, pInputBytes + nInputSize);
        size_t nPaddedSize = addPaddingToVector(aInput, BLOCK_SIZE);
        std::vector<sal_uInt8> aOutput(nPaddedSize);
        aEncrypt.update(aOutput, aInput);
        rOutput.resize(nPaddedSize + IV_SIZE);
        std::copy(rIV.begin(), rIV.end(), rOutput.begin());
        std::copy(aOutput.begin(), aOutput.end(), rOutput.begin() + IV_SIZE);
    }
};

PDFEncryptorR6::PDFEncryptorR6() = default;
PDFEncryptorR6::~PDFEncryptorR6() = default;

std::vector<sal_uInt8> PDFEncryptorR6::getEncryptedAccessPermissions(std::vector<sal_uInt8>& rKey)
{
    std::vector<sal_uInt8> aPerms = createPerms(m_nAccessPermissions, isMetadataEncrypted());
    return encryptPerms(aPerms, rKey);
}

void PDFEncryptorR6::initEncryption(EncryptionHashTransporter& rEncryptionHashTransporter,
                                    const OUString& rOwnerPassword, const OUString& rUserPassword)
{
    if (rUserPassword.isEmpty())
        return;

    std::vector<sal_uInt8> aEncryptionKey = vcl::pdf::generateKey();
    rEncryptionHashTransporter.setEncryptionKey(aEncryptionKey);

    std::vector<sal_uInt8> U;
    std::vector<sal_uInt8> UE;

    OString pUserPasswordUtf8 = OUStringToOString(rUserPassword, RTL_TEXTENCODING_UTF8);
    vcl::pdf::generateUandUE(reinterpret_cast<const sal_uInt8*>(pUserPasswordUtf8.getStr()),
                             pUserPasswordUtf8.getLength(), aEncryptionKey, U, UE);

    rEncryptionHashTransporter.setU(U);
    rEncryptionHashTransporter.setUE(UE);

    OUString aOwnerPasswordToUse = rOwnerPassword.isEmpty() ? rUserPassword : rOwnerPassword;

    std::vector<sal_uInt8> O;
    std::vector<sal_uInt8> OE;

    OString pOwnerPasswordUtf8 = OUStringToOString(aOwnerPasswordToUse, RTL_TEXTENCODING_UTF8);
    vcl::pdf::generateOandOE(reinterpret_cast<const sal_uInt8*>(pOwnerPasswordUtf8.getStr()),
                             pOwnerPasswordUtf8.getLength(), aEncryptionKey, U, O, OE);

    rEncryptionHashTransporter.setO(O);
    rEncryptionHashTransporter.setOE(OE);
}

bool PDFEncryptorR6::prepareEncryption(
    const uno::Reference<beans::XMaterialHolder>& xEncryptionMaterialHolder,
    vcl::PDFEncryptionProperties& rProperties)
{
    auto* pTransporter
        = EncryptionHashTransporter::getEncHashTransporter(xEncryptionMaterialHolder);

    if (!pTransporter)
    {
        rProperties.clear();
        return false;
    }

    rProperties.UValue = pTransporter->getU();
    rProperties.UE = pTransporter->getUE();
    rProperties.OValue = pTransporter->getO();
    rProperties.OE = pTransporter->getOE();
    rProperties.EncryptionKey = pTransporter->getEncryptionKey();

    return true;
}

void PDFEncryptorR6::setupKeysAndCheck(vcl::PDFEncryptionProperties& rProperties)
{
    m_nAccessPermissions = rProperties.getAccessPermissions();
}

sal_uInt64 PDFEncryptorR6::calculateSizeIncludingHeader(sal_uInt64 nSize)
{
    return IV_SIZE + comphelper::roundUp<sal_uInt64>(nSize, BLOCK_SIZE);
}

void PDFEncryptorR6::setupEncryption(std::vector<sal_uInt8>& rEncryptionKey, sal_Int32 /*nObject*/)
{
    m_pEncryptionContext = std::make_unique<EncryptionContext>(rEncryptionKey);
}

void PDFEncryptorR6::encrypt(const void* pInput, sal_uInt64 nInputSize,
                             std::vector<sal_uInt8>& rOutput, sal_uInt64 /*nOutputSize*/)
{
    std::vector<sal_uInt8> aIV;
    generateBytes(aIV, IV_SIZE);
    m_pEncryptionContext->encrypt(pInput, nInputSize, rOutput, aIV);
}

void PDFEncryptorR6::encryptWithIV(const void* pInput, sal_uInt64 nInputSize,
                                   std::vector<sal_uInt8>& rOutput, std::vector<sal_uInt8>& rIV)
{
    m_pEncryptionContext->encrypt(pInput, nInputSize, rOutput, rIV);
}

} // end vcl::pdf

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
