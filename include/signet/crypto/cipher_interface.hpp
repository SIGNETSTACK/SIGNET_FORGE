// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file cipher_interface.hpp
/// @brief Abstract cipher interface, GCM/CTR adapters, CipherFactory, and platform CSPRNG.

// ---------------------------------------------------------------------------
// cipher_interface.hpp -- Abstract cipher interface + CipherFactory
//
// Provides crypto-agility for Parquet Modular Encryption by abstracting
// the cipher behind an ICipher interface. Concrete adapters wrap AesGcm
// and AesCtr. CipherFactory selects the correct cipher for each PME role
// (footer, column data, metadata) based on the EncryptionAlgorithm enum.
//
// Wire format (unified across all ciphers):
//   [1 byte: iv_size] [iv bytes] [ciphertext (+tag for GCM)]
//
// The interface is Apache 2.0 (core tier) — it's not gated by BSL.
// ---------------------------------------------------------------------------

#include "signet/crypto/aes_gcm.hpp"
#include "signet/crypto/aes_ctr.hpp"
#include "signet/crypto/key_metadata.hpp"
#include "signet/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Platform-specific CSPRNG headers
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#  include <stdlib.h>  // arc4random_buf
#elif defined(__linux__)
#  include <sys/random.h>  // getrandom
#else
#  include <random>  // fallback: std::random_device
#endif

namespace signet::forge::crypto {

// ===========================================================================
// ICipher -- Abstract cipher interface
// ===========================================================================

/// Abstract cipher interface — unified API for authenticated (GCM) and
/// unauthenticated (CTR) encryption. Implementations are move-only
/// (hold key material).
class ICipher {
public:
    virtual ~ICipher() = default;

    /// Encrypt data. For authenticated ciphers, aad is bound into the tag.
    /// For unauthenticated ciphers, aad is ignored.
    /// Returns: [iv_size(1)] [iv] [ciphertext] [tag if authenticated]
    [[nodiscard]] virtual expected<std::vector<uint8_t>> encrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const = 0;

    /// Decrypt data produced by encrypt().
    [[nodiscard]] virtual expected<std::vector<uint8_t>> decrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const = 0;

    /// Whether this cipher provides authentication (GCM=true, CTR=false).
    [[nodiscard]] virtual bool is_authenticated() const noexcept = 0;

    /// Key size in bytes (32 for AES-256).
    [[nodiscard]] virtual size_t key_size() const noexcept = 0;

    /// Human-readable algorithm name.
    [[nodiscard]] virtual std::string_view algorithm_name() const noexcept = 0;

    // Non-copyable, non-movable (interface type)
    ICipher() = default;
    ICipher(const ICipher&) = delete;
    ICipher& operator=(const ICipher&) = delete;
    ICipher(ICipher&&) = default;
    ICipher& operator=(ICipher&&) = default;
};

// ===========================================================================
// Internal: IV generation (shared by both adapters)
// ===========================================================================

namespace detail::cipher {

/// Fill a buffer with cryptographically random bytes using the best
/// available OS-level CSPRNG. Falls back to std::random_device on
/// platforms without a direct syscall.
inline void fill_random_bytes(uint8_t* buf, size_t size) {
    if (size == 0) return;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(buf, size);
#elif defined(__linux__)
    // getrandom() with flags=0 blocks until urandom is seeded
    size_t written = 0;
    while (written < size) {
        ssize_t ret = getrandom(buf + written, size - written, 0);
        if (ret < 0) break; // should not happen; fall through if it does
        written += static_cast<size_t>(ret);
    }
#else
    // Fallback: std::random_device with full-word extraction
    std::random_device rd;
    constexpr size_t WORD = sizeof(std::random_device::result_type);
    size_t i = 0;
    for (; i + WORD <= size; i += WORD) {
        auto val = rd();
        std::memcpy(buf + i, &val, WORD);
    }
    if (i < size) {
        auto val = rd();
        std::memcpy(buf + i, &val, size - i);
    }
#endif
}

/// Generate a random initialization vector of the specified size.
/// @param iv_size  Number of random bytes to generate (12 for GCM, 16 for CTR).
/// @return Vector of cryptographically random bytes.
inline std::vector<uint8_t> generate_iv(size_t iv_size) {
    std::vector<uint8_t> iv(iv_size);
    fill_random_bytes(iv.data(), iv_size);
    return iv;
}

/// Prepend an IV header to ciphertext: [1 byte: iv.size()] [iv bytes] [ciphertext].
/// @param iv          Initialization vector bytes.
/// @param ciphertext  Encrypted data (may include auth tag for GCM).
/// @return Combined output with IV header prepended.
inline std::vector<uint8_t> prepend_iv(const std::vector<uint8_t>& iv,
                                        const std::vector<uint8_t>& ciphertext) {
    std::vector<uint8_t> out;
    out.reserve(1 + iv.size() + ciphertext.size());
    out.push_back(static_cast<uint8_t>(iv.size()));
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    return out;
}

/// Result of parsing an IV header from encrypted data.
struct IvParsed {
    const uint8_t* iv;          ///< Pointer to the IV bytes within the input buffer.
    const uint8_t* ciphertext;  ///< Pointer to the ciphertext after the IV.
    size_t ct_size;             ///< Ciphertext length (may include GCM auth tag).
};

/// Parse the IV header from encrypted data: [1 byte: iv_size] [iv] [ciphertext].
/// @param data  Pointer to the encrypted data buffer.
/// @param size  Total size of the encrypted data.
/// @return Parsed IV and ciphertext pointers, or an error if malformed.
inline expected<IvParsed> parse_iv_header(const uint8_t* data, size_t size) {
    if (size < 1) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "cipher: encrypted data too short (no IV size byte)"};
    }
    uint8_t iv_size = data[0];
    if (iv_size == 0 || iv_size > 16) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "cipher: invalid IV size " + std::to_string(iv_size)};
    }
    size_t header_len = 1 + static_cast<size_t>(iv_size);
    if (size < header_len) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "cipher: encrypted data too short for IV"};
    }
    return IvParsed{data + 1, data + header_len, size - header_len};
}

} // namespace detail::cipher

// ===========================================================================
// AesGcmCipher -- AES-256-GCM adapter
// ===========================================================================

/// AES-256-GCM adapter -- wraps the low-level AesGcm class behind ICipher.
///
/// Provides authenticated encryption with AAD support. Generates a random
/// 12-byte IV per encrypt() call and prepends it to the output.
///
/// @note The destructor securely zeroes key material using volatile writes.
/// @see AesCtrCipher for the unauthenticated counterpart
class AesGcmCipher final : public ICipher {
public:
    /// Construct from a key vector (must be 32 bytes for AES-256).
    explicit AesGcmCipher(const std::vector<uint8_t>& key)
        : key_(key) {}

    /// Construct from a raw key pointer and length.
    explicit AesGcmCipher(const uint8_t* key, size_t key_len)
        : key_(key, key + key_len) {}

    [[nodiscard]] expected<std::vector<uint8_t>> encrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const override {

        if (key_.size() != AesGcm::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesGcmCipher: key must be 32 bytes"};
        }

        auto iv = detail::cipher::generate_iv(AesGcm::IV_SIZE);
        AesGcm gcm(key_.data());

        auto result = aad.empty()
            ? gcm.encrypt(data, size, iv.data())
            : gcm.encrypt(data, size, iv.data(),
                           reinterpret_cast<const uint8_t*>(aad.data()),
                           aad.size());

        if (!result) return result.error();
        return detail::cipher::prepend_iv(iv, *result);
    }

    [[nodiscard]] expected<std::vector<uint8_t>> decrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const override {

        if (key_.size() != AesGcm::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesGcmCipher: key must be 32 bytes"};
        }

        auto iv_result = detail::cipher::parse_iv_header(data, size);
        if (!iv_result) return iv_result.error();
        const auto& [iv, ciphertext, ct_size] = *iv_result;

        AesGcm gcm(key_.data());
        if (!aad.empty()) {
            return gcm.decrypt(ciphertext, ct_size, iv,
                               reinterpret_cast<const uint8_t*>(aad.data()),
                               aad.size());
        } else {
            return gcm.decrypt(ciphertext, ct_size, iv);
        }
    }

    /// Destructor: securely zeroes key material.
    ~AesGcmCipher() override {
        volatile uint8_t* p = key_.data();
        for (size_t i = 0; i < key_.size(); ++i) p[i] = 0;
    }

    [[nodiscard]] bool is_authenticated() const noexcept override { return true; }
    [[nodiscard]] size_t key_size() const noexcept override { return AesGcm::KEY_SIZE; }
    [[nodiscard]] std::string_view algorithm_name() const noexcept override {
        return "AES-256-GCM";
    }

private:
    std::vector<uint8_t> key_;
};

// ===========================================================================
// AesCtrCipher -- AES-256-CTR adapter
// ===========================================================================

/// AES-256-CTR adapter -- wraps the low-level AesCtr class behind ICipher.
///
/// Unauthenticated encryption (the AAD parameter is ignored). Generates a
/// random 16-byte IV per encrypt() call and prepends it to the output.
///
/// @note The destructor securely zeroes key material using volatile writes.
/// @see AesGcmCipher for the authenticated counterpart
class AesCtrCipher final : public ICipher {
public:
    /// Construct from a key vector (must be 32 bytes for AES-256).
    explicit AesCtrCipher(const std::vector<uint8_t>& key)
        : key_(key) {}

    /// Construct from a raw key pointer and length.
    explicit AesCtrCipher(const uint8_t* key, size_t key_len)
        : key_(key, key + key_len) {}

    [[nodiscard]] expected<std::vector<uint8_t>> encrypt(
        const uint8_t* data, size_t size,
        const std::string& /*aad*/ = "") const override {

        if (key_.size() != AesCtr::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesCtrCipher: key must be 32 bytes"};
        }

        auto iv = detail::cipher::generate_iv(AesCtr::IV_SIZE);
        AesCtr ctr(key_.data());
        std::vector<uint8_t> ciphertext = ctr.encrypt(data, size, iv.data());
        return detail::cipher::prepend_iv(iv, ciphertext);
    }

    [[nodiscard]] expected<std::vector<uint8_t>> decrypt(
        const uint8_t* data, size_t size,
        const std::string& /*aad*/ = "") const override {

        if (key_.size() != AesCtr::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesCtrCipher: key must be 32 bytes"};
        }

        auto iv_result = detail::cipher::parse_iv_header(data, size);
        if (!iv_result) return iv_result.error();
        const auto& [iv, ciphertext, ct_size] = *iv_result;

        AesCtr ctr(key_.data());
        std::vector<uint8_t> plaintext = ctr.decrypt(ciphertext, ct_size, iv);
        return plaintext;
    }

    /// Destructor: securely zeroes key material.
    ~AesCtrCipher() override {
        volatile uint8_t* p = key_.data();
        for (size_t i = 0; i < key_.size(); ++i) p[i] = 0;
    }

    /// @return Always false (CTR mode has no authentication).
    [[nodiscard]] bool is_authenticated() const noexcept override { return false; }
    /// @return 32 (AES-256 key size in bytes).
    [[nodiscard]] size_t key_size() const noexcept override { return AesCtr::KEY_SIZE; }
    /// @return "AES-256-CTR".
    [[nodiscard]] std::string_view algorithm_name() const noexcept override {
        return "AES-256-CTR";
    }

private:
    std::vector<uint8_t> key_;
};

// ===========================================================================
// CipherFactory -- static factory for creating cipher instances
// ===========================================================================

/// Factory for creating cipher instances from algorithm enum + raw key.
/// NOT a singleton — all methods are static.
struct CipherFactory {
    /// Create a footer cipher (always authenticated = GCM).
    [[nodiscard]] static std::unique_ptr<ICipher> create_footer_cipher(
            EncryptionAlgorithm /*algo*/, const std::vector<uint8_t>& key) {
        // Footer always uses GCM regardless of algorithm
        return std::make_unique<AesGcmCipher>(key);
    }

    /// Create a column data cipher (GCM or CTR based on algorithm).
    [[nodiscard]] static std::unique_ptr<ICipher> create_column_cipher(
            EncryptionAlgorithm algo, const std::vector<uint8_t>& key) {
        if (algo == EncryptionAlgorithm::AES_GCM_V1) {
            return std::make_unique<AesGcmCipher>(key);
        }
        // AES_GCM_CTR_V1: column data uses CTR
        return std::make_unique<AesCtrCipher>(key);
    }

    /// Create a metadata cipher (always authenticated = GCM).
    [[nodiscard]] static std::unique_ptr<ICipher> create_metadata_cipher(
            EncryptionAlgorithm /*algo*/, const std::vector<uint8_t>& key) {
        // Metadata always uses GCM regardless of algorithm
        return std::make_unique<AesGcmCipher>(key);
    }
};

} // namespace signet::forge::crypto
