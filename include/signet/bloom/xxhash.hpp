// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file xxhash.hpp
/// @brief Header-only xxHash64 implementation for Signet Forge.
///
/// Implements the xxHash64 algorithm as specified at:
///   https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md
///
/// Used by the SplitBlockBloomFilter for hashing column values. All
/// arithmetic is unsigned 64-bit with natural overflow (mod 2^64).
///
/// The public API provides three entry points:
///   - hash64(const void*, size_t, seed) -- hash arbitrary bytes
///   - hash64(const std::string&, seed)  -- hash a string
///   - hash64_value(const T&, seed)      -- hash any trivially-copyable type
///
/// @see SplitBlockBloomFilter

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>

namespace signet::forge {

/// xxHash64 hashing functions for Parquet bloom filter support.
namespace xxhash {

/// Implementation details -- not part of the public API.
namespace detail {

/// @name xxHash64 prime constants
/// The five 64-bit primes used throughout the xxHash64 algorithm.
/// @{
static constexpr uint64_t PRIME1 = 0x9E3779B185EBCA87ULL; ///< Prime constant 1.
static constexpr uint64_t PRIME2 = 0xC2B2AE3D27D4EB4FULL; ///< Prime constant 2.
static constexpr uint64_t PRIME3 = 0x165667B19E3779F9ULL; ///< Prime constant 3.
static constexpr uint64_t PRIME4 = 0x85EBCA77C2B2AE63ULL; ///< Prime constant 4.
static constexpr uint64_t PRIME5 = 0x27D4EB2F165667C5ULL; ///< Prime constant 5.
/// @}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Rotate @p v left by @p r bits (circular shift).
/// @param v  64-bit value to rotate.
/// @param r  Number of bit positions to rotate (0-63).
/// @return Rotated value.
inline constexpr uint64_t rotl64(uint64_t v, int r) {
    return (v << r) | (v >> (64 - r));
}

/// Read a little-endian uint64_t from potentially unaligned memory.
/// @param p Pointer to 8 bytes of data in little-endian byte order.
/// @return The decoded 64-bit value in host byte order.
/// @note On big-endian platforms, a byte swap is applied automatically.
inline uint64_t read_u64_le(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    // On big-endian platforms this would need a byte swap.
    // x86, x86_64, and ARM (little-endian mode) are fine.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap64(v);
#elif !defined(__BYTE_ORDER__)
#  error "Cannot determine endianness — define __BYTE_ORDER__"
#endif
    return v;
}

/// Read a little-endian uint32_t from potentially unaligned memory.
/// @param p Pointer to 4 bytes of data in little-endian byte order.
/// @return The decoded 32-bit value in host byte order.
/// @note On big-endian platforms, a byte swap is applied automatically.
inline uint32_t read_u32_le(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#elif !defined(__BYTE_ORDER__)
#  error "Cannot determine endianness — define __BYTE_ORDER__"
#endif
    return v;
}

// ---------------------------------------------------------------------------
// Core primitives
// ---------------------------------------------------------------------------

/// Round function: accumulate one 8-byte lane into an accumulator.
///
/// Computes: `acc = rotl(acc + lane * PRIME2, 31) * PRIME1`
///
/// @param acc  Current accumulator value.
/// @param lane 64-bit data lane to fold in.
/// @return Updated accumulator.
inline constexpr uint64_t round(uint64_t acc, uint64_t lane) {
    acc += lane * PRIME2;
    acc  = rotl64(acc, 31);
    acc *= PRIME1;
    return acc;
}

/// Merge an accumulator value into the converged accumulator.
///
/// Computes: `acc = (acc ^ round(0, v)) * PRIME1 + PRIME4`
///
/// @param acc Converged accumulator (from the 4-lane rotation sum).
/// @param v   One of the four lane accumulators to merge.
/// @return Updated converged accumulator.
inline constexpr uint64_t merge_accumulator(uint64_t acc, uint64_t v) {
    acc ^= round(0, v);
    acc  = acc * PRIME1 + PRIME4;
    return acc;
}

/// Avalanche / finalization mix to ensure all output bits are well-distributed.
///
/// Applies three rounds of xor-shift-multiply to diffuse entropy.
///
/// @param h Raw hash accumulator before finalization.
/// @return Finalized 64-bit hash value.
inline constexpr uint64_t avalanche(uint64_t h) {
    h ^= h >> 33;
    h *= PRIME2;
    h ^= h >> 29;
    h *= PRIME3;
    h ^= h >> 32;
    return h;
}

} // namespace detail

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/// Compute xxHash64 of an arbitrary byte buffer.
///
/// This is the primary entry point. For inputs >= 32 bytes, four parallel
/// accumulators are used (Steps 1-3); for smaller inputs, a single
/// accumulator seeded with PRIME5 is used. All remaining bytes are consumed
/// in 8-byte, 4-byte, and 1-byte chunks before a final avalanche mix.
///
/// @param data   Pointer to input data (may be nullptr if @p length == 0).
/// @param length Number of bytes to hash.
/// @param seed   Hash seed (default 0). Different seeds produce independent hashes.
/// @return       64-bit hash value.
inline uint64_t hash64(const void* data, size_t length, uint64_t seed = 0) {
    using namespace detail;

    const auto* p   = static_cast<const uint8_t*>(data);
    const auto* end = p + length;

    uint64_t h;

    if (length >= 32) {
        // -----------------------------------------------------------------
        // Step 1: Initialize four accumulators
        // -----------------------------------------------------------------
        uint64_t v1 = seed + PRIME1 + PRIME2;
        uint64_t v2 = seed + PRIME2;
        uint64_t v3 = seed;
        uint64_t v4 = seed - PRIME1;

        // -----------------------------------------------------------------
        // Step 2: Process 32-byte stripes
        // -----------------------------------------------------------------
        const auto* stripe_end = end - 31;  // ensure at least 32 bytes remain
        do {
            v1 = round(v1, read_u64_le(p));      p += 8;
            v2 = round(v2, read_u64_le(p));      p += 8;
            v3 = round(v3, read_u64_le(p));      p += 8;
            v4 = round(v4, read_u64_le(p));      p += 8;
        } while (p < stripe_end);

        // -----------------------------------------------------------------
        // Step 3: Converge four accumulators into one
        // -----------------------------------------------------------------
        h = rotl64(v1,  1) +
            rotl64(v2,  7) +
            rotl64(v3, 12) +
            rotl64(v4, 18);

        h = merge_accumulator(h, v1);
        h = merge_accumulator(h, v2);
        h = merge_accumulator(h, v3);
        h = merge_accumulator(h, v4);
    } else {
        // -----------------------------------------------------------------
        // Small input (< 32 bytes): single accumulator
        // -----------------------------------------------------------------
        h = seed + PRIME5;
    }

    // -----------------------------------------------------------------
    // Step 4: Add total input length
    // -----------------------------------------------------------------
    h += static_cast<uint64_t>(length);

    // -----------------------------------------------------------------
    // Step 5: Consume remaining bytes
    // -----------------------------------------------------------------

    // Process remaining 8-byte chunks
    while (p + 8 <= end) {
        uint64_t lane = read_u64_le(p);
        h ^= round(0, lane);
        h  = rotl64(h, 27) * PRIME1 + PRIME4;
        p += 8;
    }

    // Process a remaining 4-byte chunk
    if (p + 4 <= end) {
        uint64_t lane = static_cast<uint64_t>(read_u32_le(p));
        h ^= lane * PRIME1;
        h  = rotl64(h, 23) * PRIME2 + PRIME3;
        p += 4;
    }

    // Process remaining 1-byte chunks
    while (p < end) {
        uint64_t lane = static_cast<uint64_t>(*p);
        h ^= lane * PRIME5;
        h  = rotl64(h, 11) * PRIME1;
        p += 1;
    }

    // -----------------------------------------------------------------
    // Step 6: Avalanche / finalization
    // -----------------------------------------------------------------
    return avalanche(h);
}

/// Convenience overload: hash a std::string.
/// @param s    The string to hash.
/// @param seed Hash seed (default 0).
/// @return 64-bit hash value.
inline uint64_t hash64(const std::string& s, uint64_t seed = 0) {
    return hash64(s.data(), s.size(), seed);
}

/// Convenience overload: hash a trivially-copyable typed value.
///
/// Hashes `sizeof(T)` bytes of the value's object representation. This is
/// suitable for primitive types (int32_t, int64_t, float, double, etc.).
///
/// @tparam T A trivially-copyable type (enforced via static_assert).
/// @param val  The value to hash.
/// @param seed Hash seed (default 0).
/// @return 64-bit hash value.
template <typename T>
inline uint64_t hash64_value(const T& val, uint64_t seed = 0) {
    static_assert(std::is_trivially_copyable_v<T>,
                  "hash64_value requires a trivially-copyable type");
    return hash64(&val, sizeof(T), seed);
}

} // namespace xxhash
} // namespace signet::forge
