// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file hkdf.hpp
/// @brief HKDF key derivation (RFC 5869) using HMAC-SHA256.

// ---------------------------------------------------------------------------
// hkdf.hpp -- HMAC-based Key Derivation Function (RFC 5869)
//
// Gap C-7/C-8: Proper cryptographic key derivation replaces raw SHA-256
// for key material derivation. Required by:
//   - NIST SP 800-108 Rev.1: Key Derivation Using Pseudorandom Functions
//   - NIST SP 800-56C Rev.2: Key-Derivation Methods in Key-Establishment
//   - Parquet PME spec: KEK→DEK derivation for key wrapping
//
// Components:
//   - HMAC-SHA256: RFC 2104 keyed hash
//   - HKDF-Extract: RFC 5869 §2.2 — PRK = HMAC-Hash(salt, IKM)
//   - HKDF-Expand: RFC 5869 §2.3 — OKM = T(1) || T(2) || ...
//
// Reference: https://www.rfc-editor.org/rfc/rfc5869
// ---------------------------------------------------------------------------

#include "signet/crypto/sha256.hpp"  // for detail::sha256

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace signet::forge::crypto {

namespace detail::hkdf {

static constexpr size_t SHA256_BLOCK_SIZE = 64;
static constexpr size_t SHA256_HASH_SIZE = 32;

/// HMAC-SHA256 (RFC 2104): keyed hash for HKDF.
///
/// HMAC(K, text) = H((K ^ opad) || H((K ^ ipad) || text))
inline std::array<uint8_t, 32> hmac_sha256(
    const uint8_t* key, size_t key_size,
    const uint8_t* data, size_t data_size) {

    std::array<uint8_t, 32> key_hash{};
    const uint8_t* actual_key = key;
    size_t actual_key_size = key_size;

    if (key_size > SHA256_BLOCK_SIZE) {
        key_hash = detail::sha256::sha256(key, key_size);
        actual_key = key_hash.data();
        actual_key_size = SHA256_HASH_SIZE;
    }

    uint8_t k_pad[SHA256_BLOCK_SIZE] = {};
    std::memcpy(k_pad, actual_key, actual_key_size);

    // Inner: H((K ^ ipad) || data)
    uint8_t inner_buf[SHA256_BLOCK_SIZE];
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        inner_buf[i] = k_pad[i] ^ 0x36;
    }

    std::vector<uint8_t> inner_msg;
    inner_msg.reserve(SHA256_BLOCK_SIZE + data_size);
    inner_msg.insert(inner_msg.end(), inner_buf, inner_buf + SHA256_BLOCK_SIZE);
    inner_msg.insert(inner_msg.end(), data, data + data_size);
    auto inner_hash = detail::sha256::sha256(inner_msg.data(), inner_msg.size());

    // Outer: H((K ^ opad) || inner_hash)
    uint8_t outer_buf[SHA256_BLOCK_SIZE];
    for (size_t i = 0; i < SHA256_BLOCK_SIZE; ++i) {
        outer_buf[i] = k_pad[i] ^ 0x5c;
    }

    std::vector<uint8_t> outer_msg;
    outer_msg.reserve(SHA256_BLOCK_SIZE + SHA256_HASH_SIZE);
    outer_msg.insert(outer_msg.end(), outer_buf, outer_buf + SHA256_BLOCK_SIZE);
    outer_msg.insert(outer_msg.end(), inner_hash.begin(), inner_hash.end());
    return detail::sha256::sha256(outer_msg.data(), outer_msg.size());
}

} // namespace detail::hkdf

/// HKDF-Extract (RFC 5869 §2.2): Extract a pseudorandom key from input keying material.
///
/// PRK = HMAC-Hash(salt, IKM)
///
/// @param salt      Optional salt (if empty, uses zero-filled key of HashLen bytes).
/// @param salt_size Salt length.
/// @param ikm       Input keying material.
/// @param ikm_size  IKM length.
/// @return 32-byte pseudorandom key (PRK).
[[nodiscard]] inline std::array<uint8_t, 32> hkdf_extract(
    const uint8_t* salt, size_t salt_size,
    const uint8_t* ikm, size_t ikm_size) {

    if (salt == nullptr || salt_size == 0) {
        uint8_t zero_salt[32] = {};
        return detail::hkdf::hmac_sha256(zero_salt, 32, ikm, ikm_size);
    }
    return detail::hkdf::hmac_sha256(salt, salt_size, ikm, ikm_size);
}

/// HKDF-Expand (RFC 5869 §2.3): Expand PRK to output keying material.
///
/// @param prk         Pseudorandom key from HKDF-Extract (32 bytes).
/// @param info        Context and application-specific info (may be empty).
/// @param info_size   Info length.
/// @param output      Buffer for output keying material.
/// @param output_size Desired OKM length (max 255 * 32 = 8160 bytes).
/// @return true on success, false if output_size exceeds maximum.
[[nodiscard]] inline bool hkdf_expand(
    const std::array<uint8_t, 32>& prk,
    const uint8_t* info, size_t info_size,
    uint8_t* output, size_t output_size) {

    static constexpr size_t HASH_LEN = 32;
    if (output_size > 255 * HASH_LEN) return false;

    size_t n = (output_size + HASH_LEN - 1) / HASH_LEN;
    std::array<uint8_t, 32> t_prev{};
    size_t t_prev_size = 0;
    size_t offset = 0;

    for (size_t i = 1; i <= n; ++i) {
        std::vector<uint8_t> msg;
        msg.reserve(t_prev_size + info_size + 1);
        msg.insert(msg.end(), t_prev.data(), t_prev.data() + t_prev_size);
        if (info != nullptr && info_size > 0) {
            msg.insert(msg.end(), info, info + info_size);
        }
        msg.push_back(static_cast<uint8_t>(i));

        auto t_i = detail::hkdf::hmac_sha256(prk.data(), HASH_LEN, msg.data(), msg.size());

        size_t copy_len = (std::min)(HASH_LEN, output_size - offset);
        std::memcpy(output + offset, t_i.data(), copy_len);
        offset += copy_len;

        t_prev = t_i;
        t_prev_size = HASH_LEN;
    }

    return true;
}

/// HKDF one-shot (RFC 5869): Extract-then-Expand in one call.
[[nodiscard]] inline bool hkdf(
    const uint8_t* salt, size_t salt_size,
    const uint8_t* ikm, size_t ikm_size,
    const uint8_t* info, size_t info_size,
    uint8_t* output, size_t output_size) {

    auto prk = hkdf_extract(salt, salt_size, ikm, ikm_size);
    return hkdf_expand(prk, info, info_size, output, output_size);
}

} // namespace signet::forge::crypto
