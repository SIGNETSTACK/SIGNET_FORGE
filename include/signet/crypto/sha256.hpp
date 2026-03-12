// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file sha256.hpp
/// @brief SHA-256 hash function (NIST FIPS 180-4).
///
/// Public-standard cryptographic hash — no proprietary IP.
/// Extracted for clean tier separation: Tier 1 (Apache 2.0) consumers
/// (hkdf.hpp, audit_chain.hpp, row_lineage.hpp) can use SHA-256 without
/// depending on any commercial or proprietary header.
///
/// Reference: NIST FIPS 180-4 — Secure Hash Standard (SHS)

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace signet::forge::crypto {
namespace detail::sha256 {

// ===========================================================================
// SHA-256 (FIPS 180-4)
// ===========================================================================

/// SHA-256 initial hash values (FIPS 180-4 Section 5.3.3)
/// These are the first 32 bits of the fractional parts of the square roots
/// of the first 8 prime numbers (2, 3, 5, 7, 11, 13, 17, 19).
static constexpr uint32_t H0[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

/// SHA-256 round constants (FIPS 180-4 Section 4.2.2)
/// First 32 bits of the fractional parts of the cube roots of the first
/// 64 prime numbers (2..311).
static constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/// Right-rotate a 32-bit word by n bits.
inline constexpr uint32_t rotr(uint32_t x, int n) {
    return (x >> n) | (x << (32 - n));
}

/// SHA-256 logical functions (FIPS 180-4 Section 4.1.2)
inline constexpr uint32_t Ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline constexpr uint32_t Maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline constexpr uint32_t Sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline constexpr uint32_t Sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline constexpr uint32_t sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline constexpr uint32_t sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

/// Load a big-endian uint32 from 4 bytes.
inline uint32_t load_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24)
         | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) <<  8)
         | (static_cast<uint32_t>(p[3]));
}

/// Store a uint32 as 4 big-endian bytes.
inline void store_be32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >>  8);
    p[3] = static_cast<uint8_t>(v);
}

/// Store a uint64 as 8 big-endian bytes.
inline void store_be64(uint8_t* p, uint64_t v) {
    p[0] = static_cast<uint8_t>(v >> 56);
    p[1] = static_cast<uint8_t>(v >> 48);
    p[2] = static_cast<uint8_t>(v >> 40);
    p[3] = static_cast<uint8_t>(v >> 32);
    p[4] = static_cast<uint8_t>(v >> 24);
    p[5] = static_cast<uint8_t>(v >> 16);
    p[6] = static_cast<uint8_t>(v >>  8);
    p[7] = static_cast<uint8_t>(v);
}

/// Process a single 64-byte (512-bit) message block.
///
/// FIPS 180-4 Section 6.2.2: SHA-256 hash computation.
/// Updates the 8-word hash state h[0..7] with one message block.
inline void compress(uint32_t h[8], const uint8_t block[64]) {
    // Step 1: Prepare the message schedule W[0..63]
    uint32_t W[64];

    // W[0..15] = the sixteen 32-bit words from the message block
    for (int t = 0; t < 16; ++t) {
        W[t] = load_be32(block + t * 4);
    }

    // W[16..63] = derived from earlier words
    for (int t = 16; t < 64; ++t) {
        W[t] = sigma1(W[t - 2]) + W[t - 7] + sigma0(W[t - 15]) + W[t - 16];
    }

    // Step 2: Initialize working variables
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    // Step 3: 64 rounds of compression
    for (int t = 0; t < 64; ++t) {
        uint32_t T1 = hh + Sigma1(e) + Ch(e, f, g) + K[t] + W[t];
        uint32_t T2 = Sigma0(a) + Maj(a, b, c);
        hh = g;
        g  = f;
        f  = e;
        e  = d + T1;
        d  = c;
        c  = b;
        b  = a;
        a  = T1 + T2;
    }

    // Step 4: Update hash state
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

/// Compute SHA-256 hash of arbitrary-length input.
///
/// Returns a 32-byte (256-bit) digest.
///
/// Implements the full FIPS 180-4 algorithm:
///   1. Pad message to 512-bit boundary (with 1-bit, zeros, 64-bit length)
///   2. Process each 512-bit block through the compression function
///   3. Output the final hash state as big-endian bytes
inline std::array<uint8_t, 32> sha256(const uint8_t* data, size_t size) {
    // Initialize hash state
    uint32_t h[8];
    std::memcpy(h, H0, sizeof(h));

    // Process complete 64-byte blocks
    size_t full_blocks = size / 64;
    for (size_t i = 0; i < full_blocks; ++i) {
        compress(h, data + i * 64);
    }

    // Pad the final block(s)
    // Padding format: message || 1-bit || 0*-bits || 64-bit big-endian length
    size_t remainder = size % 64;
    uint8_t final_block[128] = {}; // At most 2 blocks needed

    // Copy remaining bytes
    if (remainder > 0)
        std::memcpy(final_block, data + full_blocks * 64, remainder);

    // Append the 1-bit (0x80 byte)
    final_block[remainder] = 0x80;

    // Determine how many final blocks we need
    size_t final_blocks;
    if (remainder < 56) {
        // Length fits in the same block (bytes 56..63)
        final_blocks = 1;
        store_be64(final_block + 56, static_cast<uint64_t>(size) * 8);
    } else {
        // Need a second block for the length
        final_blocks = 2;
        store_be64(final_block + 120, static_cast<uint64_t>(size) * 8);
    }

    // Process final block(s)
    for (size_t i = 0; i < final_blocks; ++i) {
        compress(h, final_block + i * 64);
    }

    // Output as big-endian bytes
    std::array<uint8_t, 32> digest;
    for (int i = 0; i < 8; ++i) {
        store_be32(digest.data() + i * 4, h[i]);
    }

    return digest;
}

/// Convenience overload: hash a vector of bytes.
inline std::array<uint8_t, 32> sha256(const std::vector<uint8_t>& data) {
    return sha256(data.data(), data.size());
}

/// Hash concatenation of two byte spans with domain separation:
/// SHA-256(label || a || b).
/// Used for hybrid key combining and HMAC-style constructions.
///
/// Domain separation label prevents cross-protocol attacks per
/// NIST SP 800-227 (Final, Sep 2025) §4.2 and Barker & Roginsky "Transitioning
/// the Use of Cryptographic Algorithms" guidance.
inline std::array<uint8_t, 32> sha256_concat(
    const uint8_t* a, size_t a_size,
    const uint8_t* b, size_t b_size) {

    static constexpr uint8_t LABEL[] = "signet-forge-hybrid-kem-v1";
    std::vector<uint8_t> combined;
    combined.reserve(sizeof(LABEL) - 1 + a_size + b_size);
    combined.insert(combined.end(), LABEL, LABEL + sizeof(LABEL) - 1);
    combined.insert(combined.end(), a, a + a_size);
    combined.insert(combined.end(), b, b + b_size);
    return sha256(combined.data(), combined.size());
}

} // namespace detail::sha256
} // namespace signet::forge::crypto
