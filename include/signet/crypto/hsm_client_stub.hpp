// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/crypto/hsm_client_stub.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

// ---------------------------------------------------------------------------
// hsm_client_stub.hpp -- HSM Integration Testing Stub
//
// Gap T-8: HSM integration testing stubs for PME key wrapping validation.
//
// Provides a test-only IKmsClient implementation that simulates HSM-backed
// key wrapping without requiring actual HSM hardware. Uses NIST SP 800-38F
// AES Key Wrap (RFC 3394) semantics with a software-only KEK.
//
// This stub enables:
//   - End-to-end PME key wrapping tests without HSM infrastructure
//   - Validation of IKmsClient contract compliance
//   - Regression testing for DEK/KEK lifecycle
//   - CI pipeline crypto integration tests
//
// NOT for production use — keys are stored in process memory.
//
// References:
//   - NIST SP 800-38F: AES Key Wrap Specification
//   - RFC 3394: AES Key Wrap Algorithm
//   - PARQUET-1178 §3: Modular Encryption Key Management
//
// Header-only. Part of the signet::forge crypto module.
// ---------------------------------------------------------------------------

#include "signet/crypto/aes_core.hpp"
#include "signet/crypto/key_metadata.hpp"
#include "signet/error.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace signet::forge::crypto {

// ---------------------------------------------------------------------------
// AES Key Wrap (NIST SP 800-38F / RFC 3394) — software implementation
// ---------------------------------------------------------------------------
namespace detail::aes_key_wrap {

/// Default Initial Value per RFC 3394 §2.2.3.1
static constexpr std::array<uint8_t, 8> DEFAULT_IV = {
    0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6
};

/// AES Key Wrap — wraps plaintext key material under a KEK.
///
/// @param kek        256-bit Key Encryption Key
/// @param plaintext  Key material to wrap (must be multiple of 8 bytes, >= 16)
/// @return Wrapped key (plaintext.size() + 8 bytes), or error
[[nodiscard]] inline expected<std::vector<uint8_t>> wrap(
    const std::array<uint8_t, 32>& kek,
    const std::vector<uint8_t>& plaintext)
{
    if (plaintext.size() < 16 || (plaintext.size() % 8) != 0) {
        return Error{ErrorCode::INVALID_ARGUMENT,
            "AES Key Wrap: plaintext must be >= 16 bytes and a multiple of 8"};
    }

    const size_t n = plaintext.size() / 8;  // Number of 64-bit blocks
    Aes256 cipher(kek.data());

    // Initialize: A = IV, R[1..n] = plaintext blocks
    std::array<uint8_t, 8> A{};
    std::memcpy(A.data(), DEFAULT_IV.data(), 8);

    std::vector<uint8_t> R(plaintext.begin(), plaintext.end());

    // Wrap rounds: 6 * n iterations
    for (size_t j = 0; j < 6; ++j) {
        for (size_t i = 0; i < n; ++i) {
            // B = AES(K, A || R[i])
            std::array<uint8_t, 16> block{};
            std::memcpy(block.data(), A.data(), 8);
            std::memcpy(block.data() + 8, R.data() + i * 8, 8);

            cipher.encrypt_block(block.data());
            const auto& encrypted = block;

            // A = MSB(64, B) ^ t where t = n*j + i + 1
            uint64_t t = n * j + i + 1;
            std::memcpy(A.data(), encrypted.data(), 8);
            // XOR t into A (big-endian)
            for (int k = 7; k >= 0 && t > 0; --k) {
                A[static_cast<size_t>(k)] ^= static_cast<uint8_t>(t & 0xFF);
                t >>= 8;
            }

            // R[i] = LSB(64, B)
            std::memcpy(R.data() + i * 8, encrypted.data() + 8, 8);
        }
    }

    // Output: A || R[1..n]
    std::vector<uint8_t> result(8 + R.size());
    std::memcpy(result.data(), A.data(), 8);
    std::memcpy(result.data() + 8, R.data(), R.size());
    return result;
}

/// AES Key Unwrap — recovers plaintext key material from wrapped form.
///
/// @param kek          256-bit Key Encryption Key
/// @param ciphertext   Wrapped key (must be multiple of 8 bytes, >= 24)
/// @return Unwrapped key material, or error if integrity check fails
[[nodiscard]] inline expected<std::vector<uint8_t>> unwrap(
    const std::array<uint8_t, 32>& kek,
    const std::vector<uint8_t>& ciphertext)
{
    if (ciphertext.size() < 24 || (ciphertext.size() % 8) != 0) {
        return Error{ErrorCode::INVALID_ARGUMENT,
            "AES Key Unwrap: ciphertext must be >= 24 bytes and a multiple of 8"};
    }

    const size_t n = (ciphertext.size() / 8) - 1;
    Aes256 cipher(kek.data());

    // Initialize: A = C[0], R[1..n] = C[1..n]
    std::array<uint8_t, 8> A{};
    std::memcpy(A.data(), ciphertext.data(), 8);

    std::vector<uint8_t> R(ciphertext.begin() + 8, ciphertext.end());

    // Unwrap rounds: 6 * n iterations in reverse
    for (int j = 5; j >= 0; --j) {
        for (int ii = static_cast<int>(n) - 1; ii >= 0; --ii) {
            size_t i = static_cast<size_t>(ii);

            // A ^ t
            uint64_t t = n * static_cast<size_t>(j) + i + 1;
            std::array<uint8_t, 8> A_xor = A;
            for (int k = 7; k >= 0 && t > 0; --k) {
                A_xor[static_cast<size_t>(k)] ^= static_cast<uint8_t>(t & 0xFF);
                t >>= 8;
            }

            // B = AES^-1(K, (A^t) || R[i])
            std::array<uint8_t, 16> block{};
            std::memcpy(block.data(), A_xor.data(), 8);
            std::memcpy(block.data() + 8, R.data() + i * 8, 8);

            cipher.decrypt_block(block.data());

            // A = MSB(64, B)
            std::memcpy(A.data(), block.data(), 8);
            // R[i] = LSB(64, B)
            std::memcpy(R.data() + i * 8, block.data() + 8, 8);
        }
    }

    // Integrity check: A must equal the default IV
    if (std::memcmp(A.data(), DEFAULT_IV.data(), 8) != 0) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
            "AES Key Unwrap: integrity check failed — wrong KEK or corrupted data"};
    }

    return R;
}

} // namespace detail::aes_key_wrap

// ---------------------------------------------------------------------------
// HsmClientStub — IKmsClient implementation for testing
// ---------------------------------------------------------------------------

/// Test stub implementing IKmsClient using software AES Key Wrap.
///
/// Simulates an HSM-backed KMS for integration testing.
/// Keys are stored in process memory — NOT for production use.
///
/// Usage:
/// @code
///   auto hsm = std::make_shared<HsmClientStub>();
///   hsm->register_kek("master-key-1", kek_bytes);
///   config.kms_client = hsm;
///   auto wrapped = encryptor.wrap_keys();
/// @endcode
class HsmClientStub : public IKmsClient {
public:
    HsmClientStub() = default;

    /// Register a KEK by ID. The key must be exactly 32 bytes (AES-256).
    [[nodiscard]] expected<void> register_kek(
        const std::string& key_id,
        const std::vector<uint8_t>& kek)
    {
        if (kek.size() != 32) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "HSM stub: KEK must be exactly 32 bytes (AES-256)"};
        }
        std::array<uint8_t, 32> key{};
        std::memcpy(key.data(), kek.data(), 32);
        keks_[key_id] = key;
        return {};
    }

    /// Register a KEK from a raw 32-byte array.
    void register_kek(const std::string& key_id,
                      const std::array<uint8_t, 32>& kek) {
        keks_[key_id] = kek;
    }

    /// Check if a KEK is registered.
    [[nodiscard]] bool has_kek(const std::string& key_id) const {
        return keks_.find(key_id) != keks_.end();
    }

    /// Number of registered KEKs.
    [[nodiscard]] size_t kek_count() const { return keks_.size(); }

    // --- IKmsClient interface ---

    [[nodiscard]] expected<std::vector<uint8_t>> wrap_key(
        const std::vector<uint8_t>& dek,
        const std::string& master_key_id) const override
    {
        auto it = keks_.find(master_key_id);
        if (it == keks_.end()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                "HSM stub: KEK not found: " + master_key_id};
        }

        // Reject DEKs that are not valid for AES Key Wrap (H-5).
        // Silent padding would cause unwrap to return extra bytes.
        if (dek.size() < 16 || (dek.size() % 8) != 0) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "HSM stub: DEK must be >= 16 bytes and a multiple of 8"};
        }

        return detail::aes_key_wrap::wrap(it->second, dek);
    }

    [[nodiscard]] expected<std::vector<uint8_t>> unwrap_key(
        const std::vector<uint8_t>& wrapped_dek,
        const std::string& master_key_id) const override
    {
        auto it = keks_.find(master_key_id);
        if (it == keks_.end()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                "HSM stub: KEK not found: " + master_key_id};
        }

        return detail::aes_key_wrap::unwrap(it->second, wrapped_dek);
    }

private:
    std::unordered_map<std::string, std::array<uint8_t, 32>> keks_;
};

} // namespace signet::forge::crypto
