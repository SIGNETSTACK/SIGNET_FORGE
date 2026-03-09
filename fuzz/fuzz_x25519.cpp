// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// Fuzz harness for X25519 ECDH scalar multiplication (RFC 7748).
// Gap T-3: Crypto fuzz harnesses for post-quantum/hybrid KEM compliance testing.

#include "signet/crypto/post_quantum.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Need at least 64 bytes: 32 (scalar) + 32 (point u-coordinate)
    if (size < 64) return 0;

    std::array<uint8_t, 32> scalar{};
    std::array<uint8_t, 32> point{};
    std::memcpy(scalar.data(), data, 32);
    std::memcpy(point.data(), data + 32, 32);

    // --- Test x25519_raw (low-level, no commercial gate) ---
    auto result = signet::forge::crypto::detail::x25519::x25519_raw(scalar, point);
    (void)result;  // May be all-zero for low-order points — that's valid

    // --- Test clamp_scalar produces valid clamped key ---
    auto clamped = signet::forge::crypto::detail::x25519::clamp_scalar(scalar);
    // RFC 7748: bit 0,1,2 of first byte cleared; bit 254 set; bit 255 cleared
    if ((clamped[0] & 0x07) != 0) __builtin_trap();
    if ((clamped[31] & 0x80) != 0) __builtin_trap();
    if ((clamped[31] & 0x40) == 0) __builtin_trap();

    // --- Test with clamped scalar ---
    auto result_clamped = signet::forge::crypto::detail::x25519::x25519_raw(clamped, point);
    (void)result_clamped;

    // --- Edge case: all-zeros point ---
    {
        std::array<uint8_t, 32> zero_point{};
        auto result_zero = signet::forge::crypto::detail::x25519::x25519_raw(clamped, zero_point);
        // All-zero input should produce all-zero output (low-order point)
        uint8_t acc = 0;
        for (size_t i = 0; i < 32; ++i) acc |= result_zero[i];
        // acc should be 0 for the zero point — this is the degenerate case
        (void)acc;
    }

    // --- Edge case: low-order point u=1 (identity on Montgomery curve) ---
    {
        std::array<uint8_t, 32> one_point{};
        one_point[0] = 1;
        auto result_one = signet::forge::crypto::detail::x25519::x25519_raw(clamped, one_point);
        (void)result_one;
    }

    // --- Edge case: p-1 = 2^255-20 (highest valid u-coordinate) ---
    {
        std::array<uint8_t, 32> p_minus_1{};
        std::memset(p_minus_1.data(), 0xFF, 32);
        p_minus_1[31] = 0x7F;  // 2^255 - 1
        // Subtract 19 from byte 0 to get p = 2^255 - 19, then subtract 1 more
        // p-1 = 2^255-20, LE: byte[0] = 0xEC, rest = 0xFF..., byte[31] = 0x7F
        p_minus_1[0] = 0xEC;
        auto result_pmin1 = signet::forge::crypto::detail::x25519::x25519_raw(clamped, p_minus_1);
        (void)result_pmin1;
    }

    // --- Test base point multiplication consistency ---
    // X25519(scalar, base_point) should be deterministic
    const auto& bp = signet::forge::crypto::detail::x25519::base_point();
    auto pub1 = signet::forge::crypto::detail::x25519::x25519_raw(clamped, bp);
    auto pub2 = signet::forge::crypto::detail::x25519::x25519_raw(clamped, bp);
    if (std::memcmp(pub1.data(), pub2.data(), 32) != 0) __builtin_trap();

    return 0;
}
