#ifndef HYDRA_CRYPTO_H
#define HYDRA_CRYPTO_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// AES-256-GCM constants
constexpr size_t AEAD_KEY_LEN   = 32;  // 256 bits
constexpr size_t AEAD_NONCE_LEN = 12;  // 96 bits
constexpr size_t AEAD_TAG_LEN   = 16;  // 128 bits

// Encrypt plaintext with AES-256-GCM.
// ciphertextOut receives ciphertext || 16-byte auth tag.
// Returns true on success.
bool aeadEncrypt(const uint8_t* key, size_t keyLen,
                 const uint8_t* nonce, size_t nonceLen,
                 const uint8_t* aad, size_t aadLen,
                 const uint8_t* plaintext, size_t ptLen,
                 std::vector<uint8_t>& ciphertextOut);

// Decrypt ciphertext || 16-byte auth tag with AES-256-GCM.
// plaintextOut receives decrypted plaintext.
// Returns false if auth tag verification fails (tampered data).
bool aeadDecrypt(const uint8_t* key, size_t keyLen,
                 const uint8_t* nonce, size_t nonceLen,
                 const uint8_t* aad, size_t aadLen,
                 const uint8_t* ciphertext, size_t ctLen,
                 std::vector<uint8_t>& plaintextOut);

// Generate n cryptographically secure random bytes.
void randomBytes(uint8_t* buf, size_t n);

// Compute key_id: hex of first 8 bytes of SHA-256(key).
// Used to detect stale keys after password change.
std::string computeKeyId(const std::vector<uint8_t>& key);

// Build AAD blob for scroll-back encryption:
// account_id (4 bytes LE) || sessionId (variable) || seq (8 bytes LE)
std::vector<uint8_t> buildScrollbackAAD(uint32_t accountId,
                                        const std::string& sessionId,
                                        uint64_t seq);

#endif // HYDRA_CRYPTO_H
