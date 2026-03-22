#include "crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstring>

bool aeadEncrypt(const uint8_t* key, size_t keyLen,
                 const uint8_t* nonce, size_t nonceLen,
                 const uint8_t* aad, size_t aadLen,
                 const uint8_t* plaintext, size_t ptLen,
                 std::vector<uint8_t>& ciphertextOut) {
    if (keyLen != AEAD_KEY_LEN || nonceLen != AEAD_NONCE_LEN) return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool ok = false;
    int len = 0;

    // ciphertextOut = ciphertext || tag
    ciphertextOut.resize(ptLen + AEAD_TAG_LEN);

    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                               nullptr, nullptr) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                                static_cast<int>(nonceLen), nullptr) != 1) break;
        if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1) break;

        // Feed AAD
        if (aadLen > 0) {
            if (EVP_EncryptUpdate(ctx, nullptr, &len, aad,
                                  static_cast<int>(aadLen)) != 1) break;
        }

        // Encrypt plaintext
        if (EVP_EncryptUpdate(ctx, ciphertextOut.data(), &len, plaintext,
                              static_cast<int>(ptLen)) != 1) break;
        int ctLen = len;

        if (EVP_EncryptFinal_ex(ctx, ciphertextOut.data() + ctLen,
                                &len) != 1) break;
        ctLen += len;

        // Extract tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                                AEAD_TAG_LEN,
                                ciphertextOut.data() + ctLen) != 1) break;

        ciphertextOut.resize(ctLen + AEAD_TAG_LEN);
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) ciphertextOut.clear();
    return ok;
}

bool aeadDecrypt(const uint8_t* key, size_t keyLen,
                 const uint8_t* nonce, size_t nonceLen,
                 const uint8_t* aad, size_t aadLen,
                 const uint8_t* ciphertext, size_t ctLen,
                 std::vector<uint8_t>& plaintextOut) {
    if (keyLen != AEAD_KEY_LEN || nonceLen != AEAD_NONCE_LEN) return false;
    if (ctLen < AEAD_TAG_LEN) return false;

    size_t dataLen = ctLen - AEAD_TAG_LEN;
    const uint8_t* tag = ciphertext + dataLen;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return false;

    bool ok = false;
    int len = 0;

    plaintextOut.resize(dataLen);

    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                               nullptr, nullptr) != 1) break;
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                                static_cast<int>(nonceLen), nullptr) != 1) break;
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, key, nonce) != 1) break;

        // Feed AAD
        if (aadLen > 0) {
            if (EVP_DecryptUpdate(ctx, nullptr, &len, aad,
                                  static_cast<int>(aadLen)) != 1) break;
        }

        // Decrypt ciphertext (without tag)
        if (EVP_DecryptUpdate(ctx, plaintextOut.data(), &len, ciphertext,
                              static_cast<int>(dataLen)) != 1) break;
        int ptLen = len;

        // Set expected tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                                AEAD_TAG_LEN,
                                const_cast<uint8_t*>(tag)) != 1) break;

        // Verify tag
        if (EVP_DecryptFinal_ex(ctx, plaintextOut.data() + ptLen,
                                &len) != 1) break;
        ptLen += len;

        plaintextOut.resize(static_cast<size_t>(ptLen));
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) plaintextOut.clear();
    return ok;
}

void randomBytes(uint8_t* buf, size_t n) {
    RAND_bytes(buf, static_cast<int>(n));
}

std::string computeKeyId(const std::vector<uint8_t>& key) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(key.data(), key.size(), hash);

    // Hex-encode first 8 bytes
    static const char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(16);
    for (int i = 0; i < 8; i++) {
        id.push_back(hex[hash[i] >> 4]);
        id.push_back(hex[hash[i] & 0x0F]);
    }
    return id;
}

std::vector<uint8_t> buildScrollbackAAD(uint32_t accountId,
                                        const std::string& sessionId,
                                        uint64_t seq) {
    std::vector<uint8_t> aad;
    aad.reserve(4 + sessionId.size() + 8);

    // account_id: 4 bytes little-endian
    aad.push_back(static_cast<uint8_t>(accountId));
    aad.push_back(static_cast<uint8_t>(accountId >> 8));
    aad.push_back(static_cast<uint8_t>(accountId >> 16));
    aad.push_back(static_cast<uint8_t>(accountId >> 24));

    // session_id: variable length string bytes
    aad.insert(aad.end(), sessionId.begin(), sessionId.end());

    // seq: 8 bytes little-endian
    for (int i = 0; i < 8; i++) {
        aad.push_back(static_cast<uint8_t>(seq >> (i * 8)));
    }

    return aad;
}
