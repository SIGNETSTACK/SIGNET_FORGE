// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// Fuzz harness for HKDF-Extract/Expand (RFC 5869) using HMAC-SHA256.
// Gap T-3: Crypto fuzz harnesses for NIST SP 800-56C compliance testing.

#include "signet/crypto/hkdf.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Minimum: 1 (salt_len) + 1 (ikm_len) + 1 (info_len) + 1 (out_len) + 1 (at least 1 byte of material)
    if (size < 5) return 0;

    // Use first 4 bytes as lengths for salt, IKM, info, and output
    uint8_t salt_len_raw = data[0];
    uint8_t ikm_len_raw  = data[1];
    uint8_t info_len_raw = data[2];
    uint8_t out_len_raw  = data[3];
    const uint8_t* pool  = data + 4;
    size_t pool_size     = size - 4;

    // Clamp lengths to available data
    size_t salt_len = (salt_len_raw < pool_size) ? salt_len_raw : pool_size;
    size_t remaining = pool_size - salt_len;

    size_t ikm_len = (ikm_len_raw < remaining) ? ikm_len_raw : remaining;
    remaining -= ikm_len;

    size_t info_len = (info_len_raw < remaining) ? info_len_raw : remaining;

    const uint8_t* salt = pool;
    const uint8_t* ikm  = pool + salt_len;
    const uint8_t* info = pool + salt_len + ikm_len;

    // Clamp output length to valid HKDF range: 1..255*32 = 8160
    // Use modulo to keep it reasonable for fuzzing (cap at 512 to avoid slow runs)
    size_t out_len = (out_len_raw == 0) ? 32 : (out_len_raw % 512) + 1;

    // --- Test HKDF-Extract ---
    auto prk = signet::forge::crypto::hkdf_extract(salt, salt_len, ikm, ikm_len);
    // PRK must always be 32 bytes (SHA-256 output)
    if (prk.size() != 32) __builtin_trap();

    // --- Test HKDF-Expand ---
    std::vector<uint8_t> okm(out_len, 0);
    bool ok = signet::forge::crypto::hkdf_expand(prk, info, info_len, okm.data(), okm.size());
    if (!ok) __builtin_trap();  // Should always succeed for out_len <= 8160

    // --- Test HKDF one-shot (Extract-then-Expand) ---
    std::vector<uint8_t> okm2(out_len, 0);
    bool ok2 = signet::forge::crypto::hkdf(
        salt, salt_len, ikm, ikm_len, info, info_len, okm2.data(), okm2.size());
    if (!ok2) __builtin_trap();

    // One-shot must produce identical output to separate Extract+Expand
    if (std::memcmp(okm.data(), okm2.data(), out_len) != 0) __builtin_trap();

    // --- Test with empty salt (triggers zero-salt path in hkdf_extract) ---
    auto prk_no_salt = signet::forge::crypto::hkdf_extract(nullptr, 0, ikm, ikm_len);
    if (prk_no_salt.size() != 32) __builtin_trap();

    // --- Test HKDF-Expand rejects oversized output ---
    std::vector<uint8_t> huge(256 * 32, 0);  // 8192 > max 8160
    bool should_fail = signet::forge::crypto::hkdf_expand(prk, info, info_len, huge.data(), huge.size());
    if (should_fail) __builtin_trap();  // Must return false for output > 255*32

    return 0;
}
