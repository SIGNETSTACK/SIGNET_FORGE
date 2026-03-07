// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file post_quantum.hpp
/// @brief Post-quantum cryptography: Kyber-768 KEM, Dilithium-3 signatures,
///        X25519 ECDH, and HybridKem (Kyber+X25519) for Parquet encryption.

// ---------------------------------------------------------------------------
// post_quantum.hpp -- Post-quantum cryptography for SignetStack Signet Forge
//
// Provides two NIST post-quantum cryptographic primitives:
//
//   Kyber-768 KEM  -- Key Encapsulation Mechanism for establishing shared
//                     AES-256 keys between Parquet writer and reader.
//                     (NIST FIPS 203 / ML-KEM-768)
//
//   Dilithium-3    -- Digital signature scheme for Parquet footer signing
//                     and tamper detection.
//                     (NIST FIPS 204 / ML-DSA-65)
//
// Additionally provides a hybrid KEM (Kyber-768 + X25519) that combines
// post-quantum and classical key exchange for defense-in-depth.
//
// This is the first Parquet library to offer post-quantum encryption.
//
// TWO MODES OF OPERATION:
//
//   1. Bundled mode (default):
//      A simplified reference implementation using std::random_device and
//      SHA-256 for key generation, encapsulation stubs, and signature
//      simulation. This mode demonstrates the API and allows integration
//      testing WITHOUT any external dependencies.
//
//      *** WARNING: The bundled mode is NOT cryptographically secure. ***
//      *** It does NOT implement real Kyber or Dilithium algorithms.  ***
//      *** It is a functional stub for API development and testing.   ***
//
//   2. liboqs mode (SIGNET_HAS_LIBOQS defined):
//      Delegates to the Open Quantum Safe (liboqs) library for real
//      NIST-standardized Kyber-768 and Dilithium-3 implementations.
//      This is the recommended mode for production use.
//
// Header-only, zero external dependencies in bundled mode.
//
// References:
//   - NIST FIPS 203: Module-Lattice-Based Key-Encapsulation Mechanism
//   - NIST FIPS 204: Module-Lattice-Based Digital Signature Standard
//   - https://openquantumsafe.org/
//   - https://pq-crystals.org/kyber/
//   - https://pq-crystals.org/dilithium/
// ---------------------------------------------------------------------------

#include "signet/error.hpp"
#include "signet/crypto/cipher_interface.hpp"

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/crypto/post_quantum.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

#if defined(SIGNET_REQUIRE_REAL_PQ) && SIGNET_REQUIRE_REAL_PQ && !defined(SIGNET_HAS_LIBOQS)
#error "SIGNET_REQUIRE_REAL_PQ=1 forbids bundled PQ stubs. Reconfigure with -DSIGNET_ENABLE_PQ=ON and install liboqs."
#endif

#if !defined(SIGNET_HAS_LIBOQS)
#pragma message("WARNING: Signet post-quantum crypto is using BUNDLED Kyber/Dilithium STUBS (NOT post-quantum secure). HybridKem X25519 provides real classical ECDH security; only the Kyber-768 lattice portion is a structural placeholder. Build with -DSIGNET_ENABLE_PQ=ON for real post-quantum resistance.")
#endif

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

#ifdef SIGNET_HAS_LIBOQS
#include <oqs/oqs.h>
#endif

namespace signet::forge::crypto {

/// Runtime query: returns true if post-quantum crypto is backed by real
/// liboqs implementations (Kyber-768, Dilithium-3), false if using
/// bundled stubs. Note: when false, HybridKem still provides real
/// classical security via X25519 ECDH — only the Kyber lattice
/// portion is a structural placeholder.
[[nodiscard]] inline bool is_real_pq_crypto() noexcept {
#ifdef SIGNET_HAS_LIBOQS
    return true;
#else
    return false;
#endif
}

#ifdef SIGNET_HAS_LIBOQS
#if defined(OQS_KEM_alg_kyber_768)
inline constexpr const char* kOqsKemAlgMlKem768 = OQS_KEM_alg_kyber_768;
#elif defined(OQS_KEM_alg_ml_kem_768)
inline constexpr const char* kOqsKemAlgMlKem768 = OQS_KEM_alg_ml_kem_768;
#else
#error "liboqs is missing ML-KEM-768/Kyber-768 symbols required by SIGNET_REQUIRE_REAL_PQ"
#endif

#if defined(OQS_SIG_alg_dilithium_3)
inline constexpr const char* kOqsSigAlgMlDsa65 = OQS_SIG_alg_dilithium_3;
#elif defined(OQS_SIG_alg_ml_dsa_65)
inline constexpr const char* kOqsSigAlgMlDsa65 = OQS_SIG_alg_ml_dsa_65;
#else
#error "liboqs is missing ML-DSA-65/Dilithium-3 symbols required by SIGNET_REQUIRE_REAL_PQ"
#endif
#endif

// ===========================================================================
// SHA-256 reference implementation (detail namespace)
//
// Used internally by the bundled post-quantum stubs for:
//   - Hybrid key combining: SHA-256(kyber_ss || x25519_ss)
//   - Signature simulation: SHA-256(secret_key || message)
//   - Key derivation from random seed material
//
// Implements FIPS 180-4 Secure Hash Standard.
// ===========================================================================
namespace detail::sha256 {

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

/// Hash concatenation of two byte spans: SHA-256(a || b).
/// Used for hybrid key combining and HMAC-style constructions.
inline std::array<uint8_t, 32> sha256_concat(
    const uint8_t* a, size_t a_size,
    const uint8_t* b, size_t b_size) {

    std::vector<uint8_t> combined;
    combined.reserve(a_size + b_size);
    combined.insert(combined.end(), a, a + a_size);
    combined.insert(combined.end(), b, b + b_size);
    return sha256(combined.data(), combined.size());
}

} // namespace detail::sha256

// ===========================================================================
// Random byte generation helper (used by all PQ classes)
// ===========================================================================
namespace detail::pq {

/// Fill a buffer with cryptographically random bytes.
///
/// Delegates to detail::cipher::fill_random_bytes() for platform-aware
/// CSPRNG (arc4random_buf on macOS/BSD, getrandom on Linux).
inline void random_bytes(uint8_t* buf, size_t size) {
    detail::cipher::fill_random_bytes(buf, size);
}

/// Fill a vector with random bytes.
inline std::vector<uint8_t> random_bytes(size_t size) {
    std::vector<uint8_t> buf(size);
    random_bytes(buf.data(), size);
    return buf;
}

} // namespace detail::pq

// ===========================================================================
// detail::x25519 — Constant-time X25519 (RFC 7748 Curve25519)
//
// Field: GF(2^255 - 19).
// Representation: 5-limb radix-2^51 (Clang/GCC) or 10-limb radix-2^25.5 (MSVC).
// All operations are constant-time (no data-dependent branches, no timing leaks).
//
// References:
//   - RFC 7748: Elliptic Curves for Diffie-Hellman Key Agreement
//   - D.J. Bernstein "Curve25519: new Diffie-Hellman speed records" (2006)
//   - SUPERCOP ref10 implementation
// ===========================================================================
namespace detail::x25519 {

// ---------------------------------------------------------------------------
// Field arithmetic — GF(2^255 - 19)
//
// We use two representations depending on compiler:
//   GCC/Clang: 5 x uint64_t limbs, radix 2^51. Products use unsigned __int128.
//   MSVC:      10 x int32_t limbs, radix 2^25.5. Products use int64_t.
// ---------------------------------------------------------------------------

#if defined(__GNUC__) || defined(__clang__)

// --- 5-limb 64-bit representation (Clang / GCC) ---

using u128 = unsigned __int128;

/// GF(2^255-19) field element: 5 limbs, each <= 2^51.
/// p = 2^255 - 19 = (2^51 - 1)*2^204 + ... (fully reduced form fits in 5 limbs)
using Fe = std::array<uint64_t, 5>;

/// Carry-propagate to bring all limbs into [0, 2^51).
inline Fe fe_carry(Fe a) {
    constexpr uint64_t MASK51 = (1ULL << 51) - 1;
    for (int pass = 0; pass < 2; ++pass) {
        uint64_t c;
        c = a[0] >> 51; a[0] &= MASK51; a[1] += c;
        c = a[1] >> 51; a[1] &= MASK51; a[2] += c;
        c = a[2] >> 51; a[2] &= MASK51; a[3] += c;
        c = a[3] >> 51; a[3] &= MASK51; a[4] += c;
        c = a[4] >> 51; a[4] &= MASK51; a[0] += c * 19;
    }
    return a;
}

inline Fe fe_add(Fe a, const Fe& b) {
    for (int i = 0; i < 5; ++i) a[i] += b[i];
    return fe_carry(a);
}

inline Fe fe_sub(Fe a, const Fe& b) {
    // Add 2p before subtracting to stay positive
    a[0] += 0xFFFFFFFFFFFDAULL - b[0]; // 2p[0] = 2*(2^51-19)
    a[1] += 0xFFFFFFFFFFFFEULL - b[1];
    a[2] += 0xFFFFFFFFFFFFEULL - b[2];
    a[3] += 0xFFFFFFFFFFFFEULL - b[3];
    a[4] += 0xFFFFFFFFFFFFEULL - b[4];
    return fe_carry(a);
}

inline Fe fe_mul(const Fe& a, const Fe& b) {
    // Schoolbook multiplication, reduced mod p on-the-fly.
    // 19*b[i] precomputed to avoid repeated multiplications.
    u128 t0 = (u128)a[0]*b[0] + 19*((u128)a[1]*b[4] + (u128)a[2]*b[3] + (u128)a[3]*b[2] + (u128)a[4]*b[1]);
    u128 t1 = (u128)a[0]*b[1] + (u128)a[1]*b[0] + 19*((u128)a[2]*b[4] + (u128)a[3]*b[3] + (u128)a[4]*b[2]);
    u128 t2 = (u128)a[0]*b[2] + (u128)a[1]*b[1] + (u128)a[2]*b[0] + 19*((u128)a[3]*b[4] + (u128)a[4]*b[3]);
    u128 t3 = (u128)a[0]*b[3] + (u128)a[1]*b[2] + (u128)a[2]*b[1] + (u128)a[3]*b[0] + 19*(u128)a[4]*b[4];
    u128 t4 = (u128)a[0]*b[4] + (u128)a[1]*b[3] + (u128)a[2]*b[2] + (u128)a[3]*b[1] + (u128)a[4]*b[0];

    constexpr uint64_t MASK51 = (1ULL << 51) - 1;
    Fe r;
    uint64_t c;
    r[0] = (uint64_t)t0 & MASK51; c = (uint64_t)(t0 >> 51); t1 += c;
    r[1] = (uint64_t)t1 & MASK51; c = (uint64_t)(t1 >> 51); t2 += c;
    r[2] = (uint64_t)t2 & MASK51; c = (uint64_t)(t2 >> 51); t3 += c;
    r[3] = (uint64_t)t3 & MASK51; c = (uint64_t)(t3 >> 51); t4 += c;
    r[4] = (uint64_t)t4 & MASK51; c = (uint64_t)(t4 >> 51);
    r[0] += c * 19;
    c = r[0] >> 51; r[0] &= MASK51; r[1] += c;
    return fe_carry(r);
}

inline Fe fe_sq(const Fe& a) { return fe_mul(a, a); }

/// Load 32 little-endian bytes into a field element.
inline Fe fe_from_bytes(const uint8_t* b) {
    constexpr uint64_t MASK51 = (1ULL << 51) - 1;
    auto load8 = [&](int i) -> uint64_t {
        uint64_t v = 0;
        for (int j = 0; j < 8 && i+j < 32; ++j)
            v |= (uint64_t)b[i+j] << (8*j);
        return v;
    };
    Fe f;
    f[0] =  load8( 0)        & MASK51;
    f[1] = (load8( 6) >>  3) & MASK51;
    f[2] = (load8(12) >>  6) & MASK51;
    f[3] = (load8(19) >>  1) & MASK51;
    f[4] = (load8(24) >> 12) & MASK51;
    return fe_carry(f);
}

/// Store a field element as 32 little-endian bytes.
inline void fe_to_bytes(uint8_t* out, Fe f) {
    constexpr uint64_t MASK51 = (1ULL << 51) - 1;

    // Fully normalize carries before canonical reduction.
    f = fe_carry(f);
    f = fe_carry(f);

    // Compute g = f - p by adding 19 and carrying into bit 255.
    uint64_t g0 = f[0] + 19;
    uint64_t c  = g0 >> 51; g0 &= MASK51;
    uint64_t g1 = f[1] + c; c = g1 >> 51; g1 &= MASK51;
    uint64_t g2 = f[2] + c; c = g2 >> 51; g2 &= MASK51;
    uint64_t g3 = f[3] + c; c = g3 >> 51; g3 &= MASK51;
    uint64_t g4 = f[4] + c; c = g4 >> 51; g4 &= MASK51;

    // If carry==1, f >= p and g is the reduced canonical representative.
    uint64_t mask = 0 - c;
    f[0] ^= mask & (f[0] ^ g0);
    f[1] ^= mask & (f[1] ^ g1);
    f[2] ^= mask & (f[2] ^ g2);
    f[3] ^= mask & (f[3] ^ g3);
    f[4] ^= mask & (f[4] ^ g4);

    // Pack radix-2^51 limbs into 32 little-endian bytes without overflow.
    const uint64_t w0 = f[0] | ((f[1] & ((1ULL << 13) - 1)) << 51);
    const uint64_t w1 = (f[1] >> 13) | ((f[2] & ((1ULL << 26) - 1)) << 38);
    const uint64_t w2 = (f[2] >> 26) | ((f[3] & ((1ULL << 39) - 1)) << 25);
    const uint64_t w3 = (f[3] >> 39) | (f[4] << 12);

    auto store8 = [&](uint8_t* p, uint64_t v) {
        for (int i = 0; i < 8; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
    };
    store8(out + 0,  w0);
    store8(out + 8,  w1);
    store8(out + 16, w2);
    store8(out + 24, w3);
}

/// Constant-time conditional swap of two field elements.
/// swap must be 0 or 1.
inline void fe_cswap(Fe& a, Fe& b, uint64_t swap) {
    uint64_t mask = 0 - swap; // 0 if swap==0, 0xFFFF...FF if swap==1
    for (int i = 0; i < 5; ++i) {
        uint64_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t;
        b[i] ^= t;
    }
}

/// a^(p-2) mod p = a^(2^255-21) mod p (Fermat's little theorem).
/// Implemented with fixed-exponent square-and-multiply.
inline Fe fe_inv(const Fe& z) {
    // Compute z^(p-2) where p = 2^255 - 19 using fixed-exponent
    // square-and-multiply (exponent = 2^255 - 21).
    Fe result = {1, 0, 0, 0, 0};
    constexpr uint8_t LOW8 = 0xEB; // low 8 bits of (2^255 - 21)

    auto exp_bit = [](int bit_index) -> uint64_t {
        if (bit_index >= 8) return 1;
        return static_cast<uint64_t>((LOW8 >> bit_index) & 1U);
    };

    for (int i = 254; i >= 0; --i) {
        result = fe_sq(result);
        if (exp_bit(i)) {
            result = fe_mul(result, z);
        }
    }
    return result;
}

#else // MSVC / other compilers — 10-limb int32_t representation

/// GF(2^255-19) field element: 10 limbs, radix 2^25.5 (alternating 26 and 25 bits).
using Fe = std::array<int32_t, 10>;

/// Load 32 little-endian bytes into a 10-limb field element.
inline Fe fe_from_bytes(const uint8_t* b) {
    auto load4 = [&](int i) -> int32_t {
        return (int32_t)b[i] | ((int32_t)b[i+1]<<8) | ((int32_t)b[i+2]<<16) | ((int32_t)b[i+3]<<24);
    };
    Fe h;
    h[0] =  load4( 0)         & 0x3FFFFFF;
    h[1] = (load4( 3) >> 2)   & 0x1FFFFFF;
    h[2] = (load4( 6) >> 3)   & 0x3FFFFFF;
    h[3] = (load4( 9) >> 5)   & 0x1FFFFFF;
    h[4] = (load4(12) >> 6)   & 0x3FFFFFF;
    h[5] =  load4(16)         & 0x1FFFFFF;
    h[6] = (load4(19) >> 1)   & 0x3FFFFFF;
    h[7] = (load4(22) >> 3)   & 0x1FFFFFF;
    h[8] = (load4(25) >> 4)   & 0x3FFFFFF;
    h[9] = (load4(28) >> 6)   & 0x1FFFFFF;
    return h;
}

inline void fe_carry_10(Fe& h) {
    for (int i = 0; i < 10; ++i) {
        int bits = (i % 2 == 0) ? 26 : 25;
        int32_t carry = h[i] >> bits;
        h[i] -= carry << bits;
        if (i < 9) h[i+1] += carry;
        else       h[0]   += carry * 19;
    }
}

inline Fe fe_add(Fe a, const Fe& b) {
    for (int i = 0; i < 10; ++i) a[i] += b[i];
    return a;
}

inline Fe fe_sub(Fe a, const Fe& b) {
    // Add 2p in each limb to stay non-negative
    int32_t two_p[10] = {
        2*0x3FFFFF0+2*19, 2*0x1FFFFFE, 2*0x3FFFFFE, 2*0x1FFFFFE, 2*0x3FFFFFE,
        2*0x1FFFFFE,      2*0x3FFFFFE, 2*0x1FFFFFE, 2*0x3FFFFFE, 2*0x1FFFFFE
    };
    for (int i = 0; i < 10; ++i) a[i] = a[i] + two_p[i] - b[i];
    fe_carry_10(a);
    return a;
}

inline Fe fe_mul(const Fe& f, const Fe& g) {
    // 10x10 schoolbook with cross-product combining. Each product is int64_t.
    int64_t h[10] = {};
    // 19*g[i] for i>=5 collapses the wrap-around
    int64_t g19[5];
    for (int i = 0; i < 5; ++i) g19[i] = (int64_t)g[i+5] * 19;
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            h[i+j]   += (int64_t)f[i]   * g[j];
            h[i+j+5] += (int64_t)f[i]   * g[j+5];
            h[i+j]   += (int64_t)f[i+5] * g19[j];  // wrap
        }
    }
    Fe r;
    for (int i = 0; i < 10; ++i) r[i] = (int32_t)h[i];
    fe_carry_10(r); fe_carry_10(r);
    return r;
}

inline Fe fe_sq(const Fe& a) { return fe_mul(a, a); }

inline void fe_to_bytes(uint8_t* out, Fe h) {
    fe_carry_10(h); fe_carry_10(h);
    // Conditionally subtract p
    int32_t q = (19 * h[0] + (1<<25)) >> 26;
    for (int i = 0; i < 9; ++i) {
        q = h[i] + q * (i%2==0 ? 19 : 1);
        q >>= (i%2==0 ? 26 : 25);
    }
    h[0] += 19 * q;
    fe_carry_10(h);
    // Pack
    uint32_t b[8];
    b[0] = (uint32_t)h[0] | ((uint32_t)h[1]<<26);
    b[1] = ((uint32_t)h[1]>>6) | ((uint32_t)h[2]<<19);
    b[2] = ((uint32_t)h[2]>>13) | ((uint32_t)h[3]<<13);
    b[3] = ((uint32_t)h[3]>>19) | ((uint32_t)h[4]<<6);
    b[4] = (uint32_t)h[5] | ((uint32_t)h[6]<<25);
    b[5] = ((uint32_t)h[6]>>7) | ((uint32_t)h[7]<<18);
    b[6] = ((uint32_t)h[7]>>14) | ((uint32_t)h[8]<<11);
    b[7] = ((uint32_t)h[8]>>21) | ((uint32_t)h[9]<<4);
    for (int i = 0; i < 8; ++i) {
        out[i*4+0] = (uint8_t)(b[i]);
        out[i*4+1] = (uint8_t)(b[i]>>8);
        out[i*4+2] = (uint8_t)(b[i]>>16);
        out[i*4+3] = (uint8_t)(b[i]>>24);
    }
}

inline void fe_cswap(Fe& a, Fe& b, uint64_t swap) {
    int32_t mask = (int32_t)(0 - swap);
    for (int i = 0; i < 10; ++i) {
        int32_t t = mask & (a[i] ^ b[i]);
        a[i] ^= t; b[i] ^= t;
    }
}

inline Fe fe_inv(const Fe& z) {
    // Same addition chain, but using 10-limb operations
    Fe t0 = fe_sq(z);
    Fe t1 = fe_sq(fe_sq(fe_sq(t0)));
    t1 = fe_mul(t1, z);
    t0 = fe_mul(t1, t0);
    Fe t2 = fe_sq(t0);
    t2 = fe_mul(t2, t1);
    Fe a = t2;
    for (int i=0;i<5;i++) a=fe_sq(a);
    a=fe_mul(a,t2);
    Fe b=a; for(int i=0;i<10;i++) b=fe_sq(b); b=fe_mul(b,a);
    Fe c=b; for(int i=0;i<20;i++) c=fe_sq(c); c=fe_mul(c,b);
    for(int i=0;i<10;i++) c=fe_sq(c); c=fe_mul(c,a);
    Fe d=c; for(int i=0;i<50;i++) d=fe_sq(d); d=fe_mul(d,c);
    Fe e=d; for(int i=0;i<100;i++) e=fe_sq(e); e=fe_mul(e,d);
    for(int i=0;i<50;i++) e=fe_sq(e); e=fe_mul(e,c);
    for(int i=0;i<5;i++) e=fe_sq(e);
    return fe_mul(e,t2);
}

#endif // __GNUC__ || __clang__

// ---------------------------------------------------------------------------
// X25519 Montgomery ladder (common to both representations)
// ---------------------------------------------------------------------------

/// Clamp a 32-byte scalar per RFC 7748 §5.
/// Clear bits 0,1,2 of byte 0; clear bit 7 of byte 31; set bit 6 of byte 31.
inline std::array<uint8_t, 32> clamp_scalar(std::array<uint8_t, 32> k) {
    k[0]  &= 248;
    k[31] &= 127;
    k[31] |= 64;
    return k;
}

/// X25519 scalar multiplication: result = scalar * point.
/// Returns 32-byte output (little-endian u-coordinate of result point).
/// Returns all-zero if result is the low-order point (invalid input).
inline std::array<uint8_t, 32> x25519_raw(
    const std::array<uint8_t, 32>& scalar,
    const std::array<uint8_t, 32>& point_u)
{
    // Decode input u-coordinate, masking the high bit per RFC 7748 §5
    std::array<uint8_t, 32> u_masked = point_u;
    u_masked[31] &= 0x7F;  // Ignore the top bit
    Fe u = fe_from_bytes(u_masked.data());

    // Montgomery ladder state: (x2,z2)=1 (point at infinity), (x3,z3)=u
    Fe x_1 = u;
    Fe x_2;
#if defined(__GNUC__) || defined(__clang__)
    x_2 = {1,0,0,0,0};
#else
    x_2 = {1,0,0,0,0,0,0,0,0,0};
#endif
    Fe z_2;
#if defined(__GNUC__) || defined(__clang__)
    z_2 = {0,0,0,0,0};
#else
    z_2 = {0,0,0,0,0,0,0,0,0,0};
#endif
    Fe x_3 = u;
    Fe z_3;
#if defined(__GNUC__) || defined(__clang__)
    z_3 = {1,0,0,0,0};
#else
    z_3 = {1,0,0,0,0,0,0,0,0,0};
#endif

    // a24 as field element (121665)
    Fe a24;
#if defined(__GNUC__) || defined(__clang__)
    a24 = {121665, 0, 0, 0, 0};
#else
    a24 = {121665, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#endif

    uint64_t swap = 0;

    // 255-bit scalar, iterate from bit 254 down to 0
    for (int i = 254; i >= 0; --i) {
        uint64_t k_bit = (scalar[i / 8] >> (i % 8)) & 1;
        swap ^= k_bit;
        fe_cswap(x_2, x_3, swap);
        fe_cswap(z_2, z_3, swap);
        swap = k_bit;

        // Montgomery differential addition-and-doubling
        Fe A  = fe_add(x_2, z_2);
        Fe AA = fe_sq(A);
        Fe B  = fe_sub(x_2, z_2);
        Fe BB = fe_sq(B);
        Fe E  = fe_sub(AA, BB);
        Fe C  = fe_add(x_3, z_3);
        Fe D  = fe_sub(x_3, z_3);
        Fe DA = fe_mul(D, A);
        Fe CB = fe_mul(C, B);
        Fe t1 = fe_add(DA, CB);
        Fe t2 = fe_sub(DA, CB);
        x_3 = fe_sq(t1);
        z_3 = fe_mul(fe_sq(t2), x_1);
        x_2 = fe_mul(AA, BB);
        z_2 = fe_mul(E, fe_add(AA, fe_mul(a24, E)));
    }

    // Final conditional swap
    fe_cswap(x_2, x_3, swap);
    fe_cswap(z_2, z_3, swap);

    // Recover u-coordinate: x_2 * z_2^(-1)
    Fe result = fe_mul(x_2, fe_inv(z_2));

    std::array<uint8_t, 32> out;
    fe_to_bytes(out.data(), result);
    return out;
}

/// Compute X25519(scalar, u_coord).
/// scalar is clamped per RFC 7748. Returns expected<array<uint8_t,32>>.
/// Returns error if result is the all-zero output (low-order point / invalid input).
inline expected<std::array<uint8_t, 32>> x25519(
    const std::array<uint8_t, 32>& scalar,
    const std::array<uint8_t, 32>& u_coord)
{
    auto license = commercial::require_feature("PQ x25519");
    if (!license) return license.error();

    auto result = x25519_raw(scalar, u_coord);
    // Constant-time zero check: OR all 32 bytes, then compare (RFC 7748 §6.1, CWE-208)
    uint8_t acc = 0;
    for (size_t i = 0; i < 32; ++i) acc |= result[i];
    // acc == 0 means degenerate key (all-zero output)
    if (acc == 0) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "X25519: degenerate output (all-zero) — invalid input point"};
    }
    return result;
}

/// The X25519 base point u=9, encoded as 32 LE bytes.
inline const std::array<uint8_t, 32>& base_point() {
    static const std::array<uint8_t, 32> BP = {
        9,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
    };
    return BP;
}

/// Generate a new X25519 keypair.
/// secret_key: 32 random clamped bytes.
/// public_key: X25519(secret_key, base_point).
inline expected<std::pair<std::array<uint8_t,32>, std::array<uint8_t,32>>>
generate_keypair() {
    auto license = commercial::require_feature("PQ x25519 keypair");
    if (!license) return license.error();

    std::array<uint8_t,32> sk;
    pq::random_bytes(sk.data(), 32);
    sk = clamp_scalar(sk);
    auto pk_result = x25519(sk, base_point());
    if (!pk_result) return pk_result.error();
    return std::make_pair(sk, *pk_result);
}

} // namespace detail::x25519

// ===========================================================================
// KyberKem -- Kyber-768 Key Encapsulation Mechanism
//
// Used to establish shared AES-256 keys between Parquet writer and reader.
//
// Kyber-768 is the NIST ML-KEM-768 standard (FIPS 203), providing
// approximately 192-bit post-quantum security.
//
// Parameter set (Kyber-768 / ML-KEM-768):
//   Public key:     1184 bytes
//   Secret key:     2400 bytes
//   Ciphertext:     1088 bytes
//   Shared secret:  32 bytes (256 bits, suitable for AES-256)
// ===========================================================================

/// Kyber-768 Key Encapsulation Mechanism (NIST FIPS 203 / ML-KEM-768).
///
/// Used to establish shared AES-256 keys between Parquet writer and reader.
/// Provides approximately 192-bit post-quantum security.
///
/// Two modes of operation:
///   - **Bundled mode** (default): SHA-256-based stubs for API testing.
///     NOT cryptographically secure.
///   - **liboqs mode** (`SIGNET_HAS_LIBOQS`): Real NIST-standardized Kyber-768.
///
/// @see HybridKem for combined Kyber-768 + X25519 key exchange
/// @see DilithiumSign for post-quantum digital signatures
class KyberKem {
public:
#if defined(SIGNET_HAS_LIBOQS) && defined(OQS_KEM_ml_kem_768_length_public_key)
    static constexpr size_t PUBLIC_KEY_SIZE    = OQS_KEM_ml_kem_768_length_public_key;
    static constexpr size_t SECRET_KEY_SIZE    = OQS_KEM_ml_kem_768_length_secret_key;
    static constexpr size_t CIPHERTEXT_SIZE    = OQS_KEM_ml_kem_768_length_ciphertext;
    static constexpr size_t SHARED_SECRET_SIZE = OQS_KEM_ml_kem_768_length_shared_secret;
#elif defined(SIGNET_HAS_LIBOQS) && defined(OQS_KEM_kyber_768_length_public_key)
    static constexpr size_t PUBLIC_KEY_SIZE    = OQS_KEM_kyber_768_length_public_key;
    static constexpr size_t SECRET_KEY_SIZE    = OQS_KEM_kyber_768_length_secret_key;
    static constexpr size_t CIPHERTEXT_SIZE    = OQS_KEM_kyber_768_length_ciphertext;
    static constexpr size_t SHARED_SECRET_SIZE = OQS_KEM_kyber_768_length_shared_secret;
#else
    static constexpr size_t PUBLIC_KEY_SIZE    = 1184;  ///< Kyber-768 public key size (stub default).
    static constexpr size_t SECRET_KEY_SIZE    = 2400;  ///< Kyber-768 secret key size (stub default).
    static constexpr size_t CIPHERTEXT_SIZE    = 1088;  ///< Kyber-768 ciphertext size (stub default).
    static constexpr size_t SHARED_SECRET_SIZE = 32;    ///< Shared secret size (256 bits, for AES-256).
#endif

    /// Kyber-768 keypair: public key for encapsulation, secret key for decapsulation.
    struct KeyPair {
        std::vector<uint8_t> public_key;   ///< PUBLIC_KEY_SIZE bytes.
        std::vector<uint8_t> secret_key;   ///< SECRET_KEY_SIZE bytes.
    };

    /// Result of Kyber-768 encapsulation: ciphertext to send + shared secret to keep.
    struct EncapsulationResult {
        std::vector<uint8_t> ciphertext;       ///< CIPHERTEXT_SIZE bytes (sent to recipient).
        std::vector<uint8_t> shared_secret;    ///< 32 bytes (used as AES-256 key).
    };

    // -----------------------------------------------------------------------
    // generate_keypair -- Generate a Kyber-768 keypair
    //
    // Bundled mode:
    //   Generates random bytes for public/secret keys. The keys are
    //   structurally valid (correct sizes) but NOT real Kyber lattice keys.
    //
    // liboqs mode:
    //   Delegates to OQS_KEM_kyber_768_keypair() for real key generation.
    // -----------------------------------------------------------------------
    /// Generate a Kyber-768 keypair.
    /// @return KeyPair with public_key (1184 bytes) and secret_key (2400 bytes),
    ///         or an error on failure.
    [[nodiscard]] static expected<KeyPair> generate_keypair() {
        auto license = commercial::require_feature("Kyber generate_keypair");
        if (!license) return license.error();

#ifdef SIGNET_HAS_LIBOQS
        // --- liboqs mode: real Kyber-768 key generation ---
        OQS_KEM* kem = OQS_KEM_new(kOqsKemAlgMlKem768);
        if (!kem) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: failed to initialize OQS Kyber-768"};
        }

        KeyPair kp;
        kp.public_key.resize(kem->length_public_key);
        kp.secret_key.resize(kem->length_secret_key);

        OQS_STATUS rc = OQS_KEM_keypair(kem,
                                         kp.public_key.data(),
                                         kp.secret_key.data());
        OQS_KEM_free(kem);

        if (rc != OQS_SUCCESS) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: OQS keypair generation failed"};
        }

        return kp;
#else
        // --- REFERENCE IMPLEMENTATION — use liboqs for production ---
        //
        // Generates random bytes of the correct sizes. These are NOT
        // real Kyber lattice keys and provide NO post-quantum security.
        // The encapsulate/decapsulate functions below use SHA-256-based
        // deterministic derivation so that the round-trip is functional.
        KeyPair kp;
        kp.public_key = detail::pq::random_bytes(PUBLIC_KEY_SIZE);
        kp.secret_key = detail::pq::random_bytes(SECRET_KEY_SIZE);

        // Embed a copy of the public key hash at the end of the secret key
        // so decapsulation can derive the same shared secret.
        // secret_key layout: [random_seed(2368)] [pk_hash(32)]
        auto pk_hash = detail::sha256::sha256(kp.public_key);
        std::memcpy(kp.secret_key.data() + SECRET_KEY_SIZE - 32,
                    pk_hash.data(), 32);

        return kp;
#endif
    }

    // -----------------------------------------------------------------------
    // encapsulate -- Generate a shared secret from a public key
    //
    // The sender calls this with the recipient's public key. It produces:
    //   - ciphertext: sent to the recipient (1088 bytes)
    //   - shared_secret: used locally as the AES-256 key (32 bytes)
    //
    // Bundled mode:
    //   Generates a random seed, derives shared_secret = SHA-256(seed || pk),
    //   and produces ciphertext = seed XOR SHA-256(pk). This is NOT real
    //   Kyber encapsulation but demonstrates the API contract.
    //
    // liboqs mode:
    //   Delegates to OQS_KEM_kyber_768_encaps().
    // -----------------------------------------------------------------------
    /// Generate a shared secret from a recipient's public key (encapsulation).
    ///
    /// The sender calls this with the recipient's public key.
    /// @param public_key  Pointer to the recipient's Kyber-768 public key.
    /// @param pk_size     Must equal PUBLIC_KEY_SIZE (1184 bytes).
    /// @return Ciphertext (to send) + shared secret (to use as AES-256 key).
    [[nodiscard]] static expected<EncapsulationResult> encapsulate(
        const uint8_t* public_key, size_t pk_size) {

        auto license = commercial::require_feature("Kyber encapsulate");
        if (!license) return license.error();

        if (pk_size != PUBLIC_KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: public key must be "
                         + std::to_string(PUBLIC_KEY_SIZE) + " bytes, got "
                         + std::to_string(pk_size)};
        }

#ifdef SIGNET_HAS_LIBOQS
        // --- liboqs mode: real Kyber-768 encapsulation ---
        OQS_KEM* kem = OQS_KEM_new(kOqsKemAlgMlKem768);
        if (!kem) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: failed to initialize OQS Kyber-768"};
        }

        EncapsulationResult result;
        result.ciphertext.resize(kem->length_ciphertext);
        result.shared_secret.resize(kem->length_shared_secret);

        OQS_STATUS rc = OQS_KEM_encaps(kem,
                                         result.ciphertext.data(),
                                         result.shared_secret.data(),
                                         public_key);
        OQS_KEM_free(kem);

        if (rc != OQS_SUCCESS) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: OQS encapsulation failed"};
        }

        return result;
#else
        // --- REFERENCE IMPLEMENTATION — use liboqs for production ---
        //
        // 1. Generate random 32-byte seed
        // 2. shared_secret = SHA-256(seed || public_key_hash)
        // 3. ciphertext = [seed(32)] [pk_hash_xor_pad(1056)] -- padded to 1088
        //
        // The ciphertext contains the seed in cleartext (XORed with pk-derived
        // mask for minimal obfuscation). This is NOT cryptographically secure.

        EncapsulationResult result;

        // Generate random seed
        std::array<uint8_t, 32> seed;
        detail::pq::random_bytes(seed.data(), 32);

        // Derive public key hash
        auto pk_hash = detail::sha256::sha256(public_key, pk_size);

        // Derive shared secret: SHA-256(seed || pk_hash)
        result.shared_secret.resize(SHARED_SECRET_SIZE);
        auto ss = detail::sha256::sha256_concat(
            seed.data(), seed.size(), pk_hash.data(), pk_hash.size());
        std::memcpy(result.shared_secret.data(), ss.data(), SHARED_SECRET_SIZE);

        // Build ciphertext (1088 bytes):
        // [seed XOR pk_hash (32 bytes)] [padding derived from pk_hash (1056 bytes)]
        result.ciphertext.resize(CIPHERTEXT_SIZE, 0);

        // XOR seed with pk_hash for minimal obfuscation
        for (size_t i = 0; i < 32; ++i) {
            result.ciphertext[i] = seed[i] ^ pk_hash[i];
        }

        // Fill remaining ciphertext with deterministic padding from pk_hash
        // so that the decapsulator can identify and strip it
        for (size_t i = 32; i < CIPHERTEXT_SIZE; ++i) {
            result.ciphertext[i] = pk_hash[i % 32] ^ static_cast<uint8_t>(i);
        }

        return result;
#endif
    }

    // -----------------------------------------------------------------------
    // decapsulate -- Recover the shared secret from ciphertext + secret key
    //
    // The recipient calls this with the received ciphertext and their
    // secret key. It produces the same 32-byte shared secret that the
    // sender derived during encapsulation.
    //
    // Bundled mode:
    //   Extracts the seed from ciphertext by XORing with pk_hash (derived
    //   from the pk_hash embedded in the secret key), then derives
    //   shared_secret = SHA-256(seed || pk_hash).
    //
    // liboqs mode:
    //   Delegates to OQS_KEM_kyber_768_decaps().
    // -----------------------------------------------------------------------
    /// Recover the shared secret from ciphertext + secret key (decapsulation).
    ///
    /// The recipient calls this with the received ciphertext and their secret key.
    /// @param ciphertext   Pointer to the ciphertext from encapsulate().
    /// @param ct_size      Must equal CIPHERTEXT_SIZE (1088 bytes).
    /// @param secret_key   Pointer to the recipient's Kyber-768 secret key.
    /// @param sk_size      Must equal SECRET_KEY_SIZE (2400 bytes).
    /// @return 32-byte shared secret (identical to the sender's).
    [[nodiscard]] static expected<std::vector<uint8_t>> decapsulate(
        const uint8_t* ciphertext, size_t ct_size,
        const uint8_t* secret_key, size_t sk_size) {

        auto license = commercial::require_feature("Kyber decapsulate");
        if (!license) return license.error();

        if (ct_size != CIPHERTEXT_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: ciphertext must be "
                         + std::to_string(CIPHERTEXT_SIZE) + " bytes, got "
                         + std::to_string(ct_size)};
        }
        if (sk_size != SECRET_KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: secret key must be "
                         + std::to_string(SECRET_KEY_SIZE) + " bytes, got "
                         + std::to_string(sk_size)};
        }

#ifdef SIGNET_HAS_LIBOQS
        // --- liboqs mode: real Kyber-768 decapsulation ---
        OQS_KEM* kem = OQS_KEM_new(kOqsKemAlgMlKem768);
        if (!kem) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: failed to initialize OQS Kyber-768"};
        }

        std::vector<uint8_t> shared_secret(kem->length_shared_secret);

        OQS_STATUS rc = OQS_KEM_decaps(kem,
                                         shared_secret.data(),
                                         ciphertext,
                                         secret_key);
        OQS_KEM_free(kem);

        if (rc != OQS_SUCCESS) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "KyberKem: OQS decapsulation failed"};
        }

        return shared_secret;
#else
        // --- REFERENCE IMPLEMENTATION — use liboqs for production ---
        //
        // 1. Extract pk_hash from secret_key (last 32 bytes)
        // 2. Recover seed = ciphertext[0..31] XOR pk_hash
        // 3. shared_secret = SHA-256(seed || pk_hash)

        // Extract pk_hash from the tail of secret_key
        std::array<uint8_t, 32> pk_hash;
        std::memcpy(pk_hash.data(), secret_key + SECRET_KEY_SIZE - 32, 32);

        // Recover seed by XORing the first 32 bytes of ciphertext with pk_hash
        std::array<uint8_t, 32> seed;
        for (size_t i = 0; i < 32; ++i) {
            seed[i] = ciphertext[i] ^ pk_hash[i];
        }

        // Derive shared secret: SHA-256(seed || pk_hash)
        auto ss = detail::sha256::sha256_concat(
            seed.data(), seed.size(), pk_hash.data(), pk_hash.size());

        std::vector<uint8_t> shared_secret(SHARED_SECRET_SIZE);
        std::memcpy(shared_secret.data(), ss.data(), SHARED_SECRET_SIZE);

        return shared_secret;
#endif
    }
};

// ===========================================================================
// DilithiumSign -- Dilithium-3 Digital Signatures
//
// Used to sign Parquet file footers for tamper detection.
//
// Dilithium-3 is the NIST ML-DSA-65 standard (FIPS 204), providing
// approximately 192-bit post-quantum security for digital signatures.
//
// Parameter set (Dilithium-3 / ML-DSA-65):
//   Public key:       liboqs-defined (1952 in current profiles)
//   Secret key:       liboqs-defined (4032 for ML-DSA-65, 4000 in older Dilithium-3 builds)
//   Signature (max):  liboqs-defined (3309 for ML-DSA-65, 3293 in older Dilithium-3 builds)
// ===========================================================================

/// Dilithium-3 digital signature scheme (NIST FIPS 204 / ML-DSA-65).
///
/// Used to sign Parquet file footers for tamper detection. Provides
/// approximately 192-bit post-quantum security for digital signatures.
///
/// Two modes of operation:
///   - **Bundled mode** (default): SHA-256-based HMAC stubs. NOT cryptographically
///     secure -- any party with the key binding can forge signatures.
///   - **liboqs mode** (`SIGNET_HAS_LIBOQS`): Real NIST-standardized Dilithium-3.
///
/// @see KyberKem for post-quantum key encapsulation
class DilithiumSign {
public:
#if defined(SIGNET_HAS_LIBOQS) && defined(OQS_SIG_ml_dsa_65_length_public_key)
    static constexpr size_t PUBLIC_KEY_SIZE    = OQS_SIG_ml_dsa_65_length_public_key;
    static constexpr size_t SECRET_KEY_SIZE    = OQS_SIG_ml_dsa_65_length_secret_key;
    static constexpr size_t SIGNATURE_MAX_SIZE = OQS_SIG_ml_dsa_65_length_signature;
#elif defined(SIGNET_HAS_LIBOQS) && defined(OQS_SIG_dilithium_3_length_public_key)
    static constexpr size_t PUBLIC_KEY_SIZE    = OQS_SIG_dilithium_3_length_public_key;
    static constexpr size_t SECRET_KEY_SIZE    = OQS_SIG_dilithium_3_length_secret_key;
    static constexpr size_t SIGNATURE_MAX_SIZE = OQS_SIG_dilithium_3_length_signature;
#else
    static constexpr size_t PUBLIC_KEY_SIZE    = 1952;   ///< Dilithium-3 public key size (stub default).
    static constexpr size_t SECRET_KEY_SIZE    = 4000;   ///< Dilithium-3 secret key size (stub default).
    static constexpr size_t SIGNATURE_MAX_SIZE = 3293;   ///< Maximum Dilithium-3 signature size (stub default).
#endif

    /// Dilithium-3 signing keypair: public key for verification, secret key for signing.
    struct SignKeyPair {
        std::vector<uint8_t> public_key;   ///< PUBLIC_KEY_SIZE bytes.
        std::vector<uint8_t> secret_key;   ///< SECRET_KEY_SIZE bytes.
    };

    // -----------------------------------------------------------------------
    // generate_keypair -- Generate a Dilithium-3 signing keypair
    //
    // Bundled mode:
    //   Generates random bytes for the keys and embeds a hash binding
    //   between public and secret keys for the signature stub to use.
    //
    // liboqs mode:
    //   Delegates to OQS_SIG_dilithium_3_keypair().
    // -----------------------------------------------------------------------
    /// Generate a Dilithium-3 signing keypair.
    /// @return SignKeyPair with public_key and secret_key, or an error on failure.
    [[nodiscard]] static expected<SignKeyPair> generate_keypair() {
        auto license = commercial::require_feature("Dilithium generate_keypair");
        if (!license) return license.error();

#ifdef SIGNET_HAS_LIBOQS
        // --- liboqs mode: real Dilithium-3 key generation ---
        OQS_SIG* sig = OQS_SIG_new(kOqsSigAlgMlDsa65);
        if (!sig) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: failed to initialize OQS Dilithium-3"};
        }

        SignKeyPair kp;
        kp.public_key.resize(sig->length_public_key);
        kp.secret_key.resize(sig->length_secret_key);

        OQS_STATUS rc = OQS_SIG_keypair(sig,
                                          kp.public_key.data(),
                                          kp.secret_key.data());
        OQS_SIG_free(sig);

        if (rc != OQS_SUCCESS) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: OQS keypair generation failed"};
        }

        return kp;
#else
        // --- REFERENCE IMPLEMENTATION — use liboqs for production ---
        //
        // Generates random keys with an embedded binding:
        //   secret_key layout: [random(3936)] [SHA-256(pk_seed)(32)] [pk_seed(32)]
        //   public_key layout: [pk_seed(32)] [random(1920)]
        //
        // The pk_seed is shared between both keys so that sign() can produce
        // a deterministic "signature" that verify() can check.

        SignKeyPair kp;
        kp.public_key = detail::pq::random_bytes(PUBLIC_KEY_SIZE);
        kp.secret_key = detail::pq::random_bytes(SECRET_KEY_SIZE);

        // Embed the first 32 bytes of public key (pk_seed) at the end of
        // secret key, and its hash before it, for the stub signature scheme.
        auto pk_seed_hash = detail::sha256::sha256(
            kp.public_key.data(), 32);

        // sk[3968..3999] = pk_seed (first 32 bytes of pk)
        std::memcpy(kp.secret_key.data() + SECRET_KEY_SIZE - 32,
                    kp.public_key.data(), 32);

        // sk[3936..3967] = SHA-256(pk_seed)
        std::memcpy(kp.secret_key.data() + SECRET_KEY_SIZE - 64,
                    pk_seed_hash.data(), 32);

        return kp;
#endif
    }

    // -----------------------------------------------------------------------
    // sign -- Sign a message with the secret key
    //
    // Produces a signature of up to SIGNATURE_MAX_SIZE bytes.
    //
    // Bundled mode:
    //   Produces a "signature" = SHA-256(sk_binding || message) padded to
    //   a fixed size. This is NOT a real Dilithium lattice signature and
    //   provides NO post-quantum security. Any party with the secret key
    //   binding can forge signatures.
    //
    // liboqs mode:
    //   Delegates to OQS_SIG_dilithium_3_sign().
    // -----------------------------------------------------------------------
    /// Sign a message with the secret key.
    ///
    /// @param message     Pointer to the message bytes to sign.
    /// @param msg_size    Message length in bytes.
    /// @param secret_key  Pointer to the Dilithium-3 secret key.
    /// @param sk_size     Must equal SECRET_KEY_SIZE.
    /// @return Signature (up to SIGNATURE_MAX_SIZE bytes), or an error on failure.
    [[nodiscard]] static expected<std::vector<uint8_t>> sign(
        const uint8_t* message, size_t msg_size,
        const uint8_t* secret_key, size_t sk_size) {

        auto license = commercial::require_feature("Dilithium sign");
        if (!license) return license.error();

        if (sk_size != SECRET_KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: secret key must be "
                         + std::to_string(SECRET_KEY_SIZE) + " bytes, got "
                         + std::to_string(sk_size)};
        }

#ifdef SIGNET_HAS_LIBOQS
        // --- liboqs mode: real Dilithium-3 signing ---
        OQS_SIG* sig = OQS_SIG_new(kOqsSigAlgMlDsa65);
        if (!sig) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: failed to initialize OQS Dilithium-3"};
        }

        std::vector<uint8_t> signature(sig->length_signature);
        size_t sig_len = 0;

        OQS_STATUS rc = OQS_SIG_sign(sig,
                                       signature.data(), &sig_len,
                                       message, msg_size,
                                       secret_key);
        OQS_SIG_free(sig);

        if (rc != OQS_SUCCESS) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: OQS signing failed"};
        }

        signature.resize(sig_len);
        return signature;
#else
        // --- REFERENCE IMPLEMENTATION — use liboqs for production ---
        //
        // "Signature" = SHA-256(pk_seed_hash || message), zero-padded to
        // SIGNATURE_MAX_SIZE bytes.
        //
        // pk_seed_hash is extracted from sk[3936..3967].
        // This is a MAC, NOT a digital signature. It proves knowledge of
        // the secret key but does NOT provide the non-repudiation or
        // unforgeability guarantees of a real Dilithium signature.

        // Extract the pk_seed_hash from the secret key
        std::array<uint8_t, 32> pk_seed_hash;
        std::memcpy(pk_seed_hash.data(),
                    secret_key + SECRET_KEY_SIZE - 64, 32);

        // Compute "signature" = SHA-256(pk_seed_hash || message)
        auto sig_hash = detail::sha256::sha256_concat(
            pk_seed_hash.data(), pk_seed_hash.size(),
            message, msg_size);

        // Pad to SIGNATURE_MAX_SIZE
        // Layout: [SHA-256 hash (32 bytes)] [deterministic padding (3261 bytes)]
        std::vector<uint8_t> signature(SIGNATURE_MAX_SIZE, 0);
        std::memcpy(signature.data(), sig_hash.data(), 32);

        // Fill padding deterministically from the hash so verify() can
        // check it (the padding is derived, not random)
        for (size_t i = 32; i < SIGNATURE_MAX_SIZE; ++i) {
            signature[i] = sig_hash[i % 32] ^ static_cast<uint8_t>(i & 0xFF);
        }

        return signature;
#endif
    }

    // -----------------------------------------------------------------------
    // verify -- Verify a signature against a message and public key
    //
    // Returns true if the signature is valid, false otherwise.
    //
    // Bundled mode:
    //   Recomputes the expected "signature" from the public key's pk_seed
    //   and message, then compares in constant time.
    //
    // liboqs mode:
    //   Delegates to OQS_SIG_dilithium_3_verify().
    // -----------------------------------------------------------------------
    /// Verify a signature against a message and public key.
    ///
    /// @param message     Pointer to the original message bytes.
    /// @param msg_size    Message length in bytes.
    /// @param signature   Pointer to the signature bytes.
    /// @param sig_size    Signature length (must be <= SIGNATURE_MAX_SIZE).
    /// @param public_key  Pointer to the Dilithium-3 public key.
    /// @param pk_size     Must equal PUBLIC_KEY_SIZE.
    /// @return True if the signature is valid, false otherwise.
    [[nodiscard]] static expected<bool> verify(
        const uint8_t* message, size_t msg_size,
        const uint8_t* signature, size_t sig_size,
        const uint8_t* public_key, size_t pk_size) {

        auto license = commercial::require_feature("Dilithium verify");
        if (!license) return license.error();

        if (pk_size != PUBLIC_KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: public key must be "
                         + std::to_string(PUBLIC_KEY_SIZE) + " bytes, got "
                         + std::to_string(pk_size)};
        }
        if (sig_size == 0 || sig_size > SIGNATURE_MAX_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: invalid signature size "
                         + std::to_string(sig_size)};
        }

#ifdef SIGNET_HAS_LIBOQS
        // --- liboqs mode: real Dilithium-3 verification ---
        OQS_SIG* sig_ctx = OQS_SIG_new(kOqsSigAlgMlDsa65);
        if (!sig_ctx) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "DilithiumSign: failed to initialize OQS Dilithium-3"};
        }

        OQS_STATUS rc = OQS_SIG_verify(sig_ctx,
                                         message, msg_size,
                                         signature, sig_size,
                                         public_key);
        OQS_SIG_free(sig_ctx);

        return (rc == OQS_SUCCESS);
#else
        // --- REFERENCE IMPLEMENTATION — use liboqs for production ---
        //
        // Recompute the expected "signature" from pk_seed and message,
        // then compare the first 32 bytes (the SHA-256 core) in constant time.

        // Extract pk_seed from the first 32 bytes of the public key
        auto pk_seed_hash = detail::sha256::sha256(public_key, 32);

        // Recompute expected signature hash
        auto expected_hash = detail::sha256::sha256_concat(
            pk_seed_hash.data(), pk_seed_hash.size(),
            message, msg_size);

        // Constant-time comparison of the first 32 bytes
        // (the SHA-256 hash is the cryptographic core of our stub signature)
        if (sig_size < 32) {
            return false;
        }

        uint8_t diff = 0;
        for (size_t i = 0; i < 32; ++i) {
            diff |= signature[i] ^ expected_hash[i];
        }

        return (diff == 0);
#endif
    }
};

// ===========================================================================
// HybridKem -- Kyber-768 + X25519 Hybrid Key Encapsulation
//
// Combines post-quantum (Kyber-768) and classical (X25519) key exchange
// into a single hybrid KEM. The shared secret is derived as:
//
//   shared_secret = SHA-256(kyber_shared_secret || x25519_shared_secret)
//
// This provides defense-in-depth: even if Kyber is broken (unlikely given
// NIST standardization), X25519 still provides classical security. And
// if X25519 is broken by a quantum computer, Kyber still provides
// post-quantum security.
//
// This follows the "KEM combiner" approach recommended by NIST and the
// IETF Composite KEM draft.
//
// X25519 implementation:
//   Uses the constant-time Montgomery ladder in detail::x25519 (RFC 7748).
//   DH commutativity: X25519(eph_sk, recip_pk) == X25519(recip_sk, eph_pk).
//   This is correct in both bundled mode and liboqs mode.
// ===========================================================================

/// Hybrid Key Encapsulation combining Kyber-768 (post-quantum) and X25519 (classical).
///
/// The shared secret is derived as SHA-256(kyber_ss || x25519_ss), providing
/// defense-in-depth: even if one algorithm is broken, the other still provides
/// security. Follows the NIST/IETF Composite KEM approach.
///
/// X25519 uses the real constant-time Montgomery ladder in `detail::x25519`
/// (RFC 7748) in both bundled and liboqs modes.
///
/// @see KyberKem for standalone Kyber-768
/// @see DilithiumSign for post-quantum signatures
class HybridKem {
public:
    static constexpr size_t X25519_PUBLIC_KEY_SIZE    = 32;  ///< X25519 public key size in bytes.
    static constexpr size_t X25519_SECRET_KEY_SIZE    = 32;  ///< X25519 secret key size in bytes.
    static constexpr size_t HYBRID_SHARED_SECRET_SIZE = 32;  ///< Combined shared secret size (SHA-256 output).

    /// Hybrid keypair: Kyber-768 + X25519 components.
    struct HybridKeyPair {
        std::vector<uint8_t> kyber_public_key;   ///< Kyber-768 public key (1184 bytes).
        std::vector<uint8_t> kyber_secret_key;   ///< Kyber-768 secret key (2400 bytes).
        std::vector<uint8_t> x25519_public_key;  ///< X25519 public key (32 bytes).
        std::vector<uint8_t> x25519_secret_key;  ///< X25519 clamped secret scalar (32 bytes).
    };

    /// Result of hybrid encapsulation.
    struct HybridEncapsResult {
        std::vector<uint8_t> kyber_ciphertext;   ///< Kyber ciphertext (1088 bytes, sent to recipient).
        std::vector<uint8_t> x25519_public_key;  ///< Ephemeral X25519 public key (32 bytes, sent to recipient).
        std::vector<uint8_t> shared_secret;      ///< 32 bytes = SHA-256(kyber_ss || x25519_ss).
    };

    // -----------------------------------------------------------------------
    // generate_keypair
    // Generates Kyber-768 + real X25519 (RFC 7748) keypair.
    // -----------------------------------------------------------------------
    /// Generate a hybrid Kyber-768 + X25519 (RFC 7748) keypair.
    /// @return HybridKeyPair with all four key components, or an error on failure.
    [[nodiscard]] static expected<HybridKeyPair> generate_keypair() {
        auto license = commercial::require_feature("HybridKem generate_keypair");
        if (!license) return license.error();

        // Kyber-768 component (stub or real via liboqs)
        auto kyber_result = KyberKem::generate_keypair();
        if (!kyber_result) return kyber_result.error();

        HybridKeyPair hkp;
        hkp.kyber_public_key = std::move(kyber_result->public_key);
        hkp.kyber_secret_key = std::move(kyber_result->secret_key);

        // X25519 component: real RFC 7748 Curve25519 keypair
        auto x25519_kp = detail::x25519::generate_keypair();
        if (!x25519_kp) return x25519_kp.error();

        hkp.x25519_secret_key.assign(x25519_kp->first.begin(),  x25519_kp->first.end());
        hkp.x25519_public_key.assign(x25519_kp->second.begin(), x25519_kp->second.end());

        return hkp;
    }

    // -----------------------------------------------------------------------
    // encapsulate
    //
    // Kyber-768 encapsulation (stub or liboqs) + real X25519 DH:
    //   1. kyber_ss from KyberKem::encapsulate(recipient.kyber_pk)
    //   2. Generate ephemeral X25519 keypair (eph_sk, eph_pk)
    //   3. x25519_ss = X25519(eph_sk, recipient.x25519_pk)  -- real DH
    //   4. shared_secret = SHA-256(kyber_ss || x25519_ss)
    //   5. Send: kyber_ciphertext + eph_pk
    //
    // The recipient runs decapsulate() with their full keypair to
    // recover the same shared_secret because X25519 is commutative:
    //   X25519(eph_sk, recip_pk) == X25519(recip_sk, eph_pk)
    // -----------------------------------------------------------------------
    /// Hybrid encapsulation: Kyber-768 + X25519 DH key agreement.
    ///
    /// @param recipient_pk  Recipient's hybrid keypair (only public keys used).
    /// @return Kyber ciphertext + ephemeral X25519 pk + combined shared secret.
    [[nodiscard]] static expected<HybridEncapsResult> encapsulate(
        const HybridKeyPair& recipient_pk) {

        auto license = commercial::require_feature("HybridKem encapsulate");
        if (!license) return license.error();

        // Step 1: Kyber-768 encapsulation
        auto kyber_result = KyberKem::encapsulate(
            recipient_pk.kyber_public_key.data(),
            recipient_pk.kyber_public_key.size());
        if (!kyber_result) return kyber_result.error();

        // Step 2: Generate ephemeral X25519 keypair
        auto eph_kp = detail::x25519::generate_keypair();
        if (!eph_kp) return eph_kp.error();
        const auto& eph_sk = eph_kp->first;
        const auto& eph_pk = eph_kp->second;

        // Step 3: X25519 shared secret = X25519(eph_sk, recipient.x25519_pk)
        if (recipient_pk.x25519_public_key.size() != 32) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "HybridKem: recipient X25519 public key must be 32 bytes"};
        }
        std::array<uint8_t, 32> recip_x25519_pk;
        std::memcpy(recip_x25519_pk.data(),
                    recipient_pk.x25519_public_key.data(), 32);

        auto x25519_ss_result = detail::x25519::x25519(eph_sk, recip_x25519_pk);
        if (!x25519_ss_result) return x25519_ss_result.error();

        // Step 4: Combined shared secret = SHA-256(kyber_ss || x25519_ss)
        auto combined = detail::sha256::sha256_concat(
            kyber_result->shared_secret.data(),
            kyber_result->shared_secret.size(),
            x25519_ss_result->data(),
            x25519_ss_result->size());

        HybridEncapsResult result;
        result.kyber_ciphertext = std::move(kyber_result->ciphertext);
        result.x25519_public_key.assign(eph_pk.begin(), eph_pk.end());
        result.shared_secret.assign(combined.begin(), combined.end());

        return result;
    }

    // -----------------------------------------------------------------------
    // decapsulate
    //
    // Recovers the same shared_secret as encapsulate() because X25519 DH
    // is commutative: X25519(eph_sk, recip_pk) == X25519(recip_sk, eph_pk).
    //
    //   1. kyber_ss = KyberKem::decapsulate(kyber_ciphertext, recip.kyber_sk)
    //   2. x25519_ss = X25519(recip.x25519_sk, eph_pk)  -- commutative with encaps
    //   3. shared_secret = SHA-256(kyber_ss || x25519_ss)  -- identical to encaps
    //
    // Works in both bundled mode (Kyber stub + real X25519) and
    // liboqs mode (real Kyber + real X25519).
    // -----------------------------------------------------------------------
    /// Hybrid decapsulation: recovers the same shared secret as encapsulate().
    ///
    /// X25519 DH is commutative: X25519(eph_sk, recip_pk) == X25519(recip_sk, eph_pk).
    /// @param encaps        Encapsulation result (ciphertext + ephemeral pk).
    /// @param recipient_sk  Recipient's full hybrid keypair (secret keys used).
    /// @return 32-byte combined shared secret, identical to encapsulate()'s output.
    [[nodiscard]] static expected<std::vector<uint8_t>> decapsulate(
        const HybridEncapsResult& encaps,
        const HybridKeyPair& recipient_sk) {

        auto license = commercial::require_feature("HybridKem decapsulate");
        if (!license) return license.error();

        // Step 1: Kyber-768 decapsulation
        auto kyber_ss = KyberKem::decapsulate(
            encaps.kyber_ciphertext.data(),
            encaps.kyber_ciphertext.size(),
            recipient_sk.kyber_secret_key.data(),
            recipient_sk.kyber_secret_key.size());
        if (!kyber_ss) return kyber_ss.error();

        // Step 2: X25519 key agreement (commutative with encapsulate)
        if (recipient_sk.x25519_secret_key.size() != 32 ||
            encaps.x25519_public_key.size() != 32) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "HybridKem: X25519 key sizes must be 32 bytes"};
        }
        std::array<uint8_t, 32> recip_sk_arr, eph_pk_arr;
        std::memcpy(recip_sk_arr.data(), recipient_sk.x25519_secret_key.data(), 32);
        std::memcpy(eph_pk_arr.data(),   encaps.x25519_public_key.data(),       32);

        auto x25519_ss_result = detail::x25519::x25519(recip_sk_arr, eph_pk_arr);
        if (!x25519_ss_result) return x25519_ss_result.error();

        // Step 3: Combined shared secret — identical to encapsulate()
        auto combined = detail::sha256::sha256_concat(
            kyber_ss->data(), kyber_ss->size(),
            x25519_ss_result->data(), x25519_ss_result->size());

        return std::vector<uint8_t>(combined.begin(), combined.end());
    }
};

// ===========================================================================
// PostQuantumConfig -- Configuration for post-quantum encryption in PME
//
// This structure integrates with the existing EncryptionConfig to add
// post-quantum key encapsulation and footer signing.
//
// Usage:
//   PostQuantumConfig pq_cfg;
//   pq_cfg.enabled = true;
//   pq_cfg.hybrid_mode = true;  // Kyber + X25519
//
//   // Generate keypairs (typically done once, stored securely)
//   auto kem_kp = KyberKem::generate_keypair();
//   pq_cfg.recipient_public_key = kem_kp->public_key;
//   pq_cfg.recipient_secret_key = kem_kp->secret_key;
//
//   auto sig_kp = DilithiumSign::generate_keypair();
//   pq_cfg.signing_public_key = sig_kp->public_key;
//   pq_cfg.signing_secret_key = sig_kp->secret_key;
//
// When writing:
//   1. encapsulate() with recipient_public_key to get the AES-256 key
//   2. Use that key as the footer_key / column_key in EncryptionConfig
//   3. sign() the serialized footer with signing_secret_key
//   4. Store the KEM ciphertext and signature in file metadata
//
// When reading:
//   1. decapsulate() with recipient_secret_key to recover the AES-256 key
//   2. Use that key to decrypt footer / columns via PME
//   3. verify() the footer signature with signing_public_key
// ===========================================================================

/// Configuration for post-quantum encryption in Parquet Modular Encryption.
///
/// Integrates with EncryptionConfig to add post-quantum key encapsulation
/// (Kyber-768 or HybridKem) and footer signing (Dilithium-3).
///
/// @see KyberKem, HybridKem, DilithiumSign
struct PostQuantumConfig {
    /// Master enable for post-quantum features. When false, standard PME
    /// is used without any PQ key exchange or signing.
    bool enabled = false;

    /// When true, use hybrid KEM (Kyber-768 + X25519) for key exchange.
    /// When false, use Kyber-768 KEM alone.
    /// Hybrid mode is recommended: it provides security even if one
    /// algorithm is broken.
    bool hybrid_mode = true;

    // --- Kyber KEM keypair for file encryption key exchange ---

    /// Recipient's Kyber-768 public key (KyberKem::PUBLIC_KEY_SIZE bytes).
    /// The writer encapsulates against this to derive the AES-256 file key.
    std::vector<uint8_t> recipient_public_key;

    /// Recipient's Kyber-768 secret key (KyberKem::SECRET_KEY_SIZE bytes).
    /// The reader uses this to decapsulate and recover the AES-256 file key.
    /// This is sensitive material -- protect it accordingly.
    std::vector<uint8_t> recipient_secret_key;

    // --- Dilithium signing keypair for footer signing ---

    /// Dilithium-3/ML-DSA-65 public key (DilithiumSign::PUBLIC_KEY_SIZE bytes).
    /// Embedded in file metadata so readers can verify the footer signature.
    std::vector<uint8_t> signing_public_key;

    /// Dilithium-3/ML-DSA-65 secret key (DilithiumSign::SECRET_KEY_SIZE bytes).
    /// Used by the writer to sign the serialized footer.
    /// This is sensitive material -- protect it accordingly.
    std::vector<uint8_t> signing_secret_key;
};

} // namespace signet::forge::crypto
