// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file aes_gcm.hpp
/// @brief AES-256-GCM authenticated encryption (NIST SP 800-38D).

// ---------------------------------------------------------------------------
// aes_gcm.hpp -- Bundled, zero-dependency, header-only AES-256-GCM
//
// Implements AES-256 in Galois/Counter Mode (GCM) as specified in:
//   NIST SP 800-38D: Recommendation for Block Cipher Modes of Operation:
//   Galois/Counter Mode (GCM) and GMAC
//   https://csrc.nist.gov/publications/detail/sp/800-38d/final
//
// GCM provides both confidentiality (encryption) and authenticity (128-bit
// authentication tag). This is the mode used for Parquet footer encryption
// where tamper detection is critical.
//
// Parameters:
//   Key size:   32 bytes (256 bits, AES-256)
//   IV size:    12 bytes (96 bits, standard GCM nonce)
//   Tag size:   16 bytes (128 bits, full-length authentication tag)
//
// NIST SP 800-38D test vector (Test Case 16 -- AES-256-GCM):
//   Key:        feffe9928665731c6d6a8f9467308308
//               feffe9928665731c6d6a8f9467308308
//   IV:         cafebabefacedbaddecaf888
//   AAD:        feedfacedeadbeeffeedfacedeadbeef
//               abaddad2
//   Plaintext:  d9313225f88406e5a55909c5aff5269a
//               86a7a9531534f7da2e4c303d8a318a72
//               1c3c0c95956809532fcf0e2449a6b525
//               b16aedf5aa0de657ba637b39
//   Ciphertext: 522dc1f099567d07f47f37a32a84427d
//               643a8cdcbfe5c0c97598a2bd2555d1aa
//               8cb08e48590dbb3da7b08b1056828838
//               c5f61e6393ba7a0abcc9f662
//   Tag:        76fc6ece0f4e1768cddf8853bb2d551b
// ---------------------------------------------------------------------------

#include "signet/crypto/aes_core.hpp"
#include "signet/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace signet::forge::crypto {

// ===========================================================================
// GCM internal helpers
// ===========================================================================
namespace detail::gcm {

// ---------------------------------------------------------------------------
// GF(2^128) multiplication for GHASH
//
// The GCM spec uses the polynomial x^128 + x^7 + x^2 + x + 1 over GF(2).
// The 128-bit reduction constant (when the high bit is shifted out) is:
//   R = 0xe1000000 00000000 00000000 00000000
//
// We represent 128-bit values as two uint64_t in big-endian order:
//   v[0] = most significant 64 bits
//   v[1] = least significant 64 bits
//
// This uses the "schoolbook" bit-by-bit multiplication, which is correct
// and simple though not the fastest possible implementation.
// ---------------------------------------------------------------------------

/// 128-bit value stored as two big-endian uint64_t halves.
///
/// Used internally to represent GF(2^128) field elements in the GHASH
/// computation and to pass around 128-bit AES blocks.
struct Block128 {
    uint64_t hi; ///< Bits 127..64 (most significant half).
    uint64_t lo; ///< Bits 63..0 (least significant half).

    /// Default constructor -- zero-initializes both halves.
    Block128() : hi(0), lo(0) {}

    /// Construct from explicit high and low halves.
    Block128(uint64_t h, uint64_t l) : hi(h), lo(l) {}
};

/// Load a 16-byte array into a Block128 (big-endian byte order).
inline Block128 load_block(const uint8_t src[16]) {
    Block128 b;
    b.hi = 0;
    b.lo = 0;
    for (int i = 0; i < 8; ++i) {
        b.hi = (b.hi << 8) | src[i];
    }
    for (int i = 8; i < 16; ++i) {
        b.lo = (b.lo << 8) | src[i];
    }
    return b;
}

/// Store a Block128 into a 16-byte array (big-endian byte order).
inline void store_block(uint8_t dst[16], const Block128& b) {
    for (int i = 7; i >= 0; --i) {
        dst[i]     = static_cast<uint8_t>(b.hi >> (8 * (7 - i)));
        dst[i + 8] = static_cast<uint8_t>(b.lo >> (8 * (7 - i)));
    }
}

/// XOR two Block128 values.
inline Block128 xor_blocks(const Block128& a, const Block128& b) {
    return {a.hi ^ b.hi, a.lo ^ b.lo};
}

/// "Doubling" in GF(2^128): multiply by x (shift right by 1 in GCM bit ordering).
///
/// If the LSB (bit 127 in GCM ordering) was set, XOR with the reducing
/// polynomial R = 0xe1 << 56.  Constant-time: no branches on the input.
inline Block128 gf128_double(const Block128& V) {
    uint64_t lsb = V.lo & 1;
    Block128 result;
    result.lo = (V.lo >> 1) | ((V.hi & 1) << 63);
    result.hi = (V.hi >> 1) ^ ((uint64_t(0xe1) << 56) & (uint64_t(0) - lsb));
    return result;
}

/// 4-bit precomputed table for constant-time GHASH multiplication.
///
/// Stores M[i] = i * H in GF(2^128) for i in 0..15, where i is treated as
/// a 4-bit GF(2) polynomial.  Computed at GCM init time from the hash
/// subkey H = AES_K(0^128).
///
/// Ref: NIST SP 800-38D §6.3, CWE-208 (constant-time requirement).
struct GHashTable {
    Block128 entries[16];

    /// Precompute the 16-entry multiplication table from hash subkey H.
    void init(const Block128& H) {
        entries[0] = Block128{0, 0};
        entries[1] = H;
        entries[2] = gf128_double(H);
        entries[4] = gf128_double(entries[2]);
        entries[8] = gf128_double(entries[4]);
        // Fill remaining entries by XOR (linearity over GF(2))
        entries[3]  = xor_blocks(entries[2], entries[1]);
        entries[5]  = xor_blocks(entries[4], entries[1]);
        entries[6]  = xor_blocks(entries[4], entries[2]);
        entries[7]  = xor_blocks(entries[4], entries[3]);
        entries[9]  = xor_blocks(entries[8], entries[1]);
        entries[10] = xor_blocks(entries[8], entries[2]);
        entries[11] = xor_blocks(entries[8], entries[3]);
        entries[12] = xor_blocks(entries[8], entries[4]);
        entries[13] = xor_blocks(entries[8], entries[5]);
        entries[14] = xor_blocks(entries[8], entries[6]);
        entries[15] = xor_blocks(entries[8], entries[7]);
    }
};

/// Constant-time 16-entry Block128 table lookup.
///
/// Scans all 16 entries on every call, selecting the entry at @p index
/// via data-independent masking.  Prevents cache-timing side channels.
inline Block128 ct_table_lookup(const Block128 table[16], uint8_t index) {
    Block128 result{0, 0};
    for (uint8_t i = 0; i < 16; ++i) {
        // Branchless equality: arithmetic instead of == to avoid conditional branch (CWE-208)
        uint64_t eq = static_cast<uint64_t>(i) ^ static_cast<uint64_t>(index);
        uint64_t mask = ((eq | (~eq + 1)) >> 63) ^ 1;
        mask = static_cast<uint64_t>(0) - mask;
        result.hi |= table[i].hi & mask;
        result.lo |= table[i].lo & mask;
    }
    return result;
}

/// Constant-time 4-bit reduction table lookup.
///
/// When right-shifting a GF(2^128) element by 4 bits, the 4 bits that
/// fall off the LSB end each contribute a shifted copy of the reducing
/// polynomial R = 0xe1 << 56.  This table precomputes the XOR of those
/// contributions for every possible 4-bit value.
inline uint64_t ct_reduce4(uint8_t index) {
    static constexpr uint64_t R4[16] = {
        0x0000000000000000ULL, 0xe100000000000000ULL,
        0x7080000000000000ULL, 0x9180000000000000ULL,
        0x3840000000000000ULL, 0xd940000000000000ULL,
        0x48c0000000000000ULL, 0xa9c0000000000000ULL,
        0x1c20000000000000ULL, 0xfd20000000000000ULL,
        0x6ca0000000000000ULL, 0x8da0000000000000ULL,
        0x2460000000000000ULL, 0xc560000000000000ULL,
        0x54e0000000000000ULL, 0xb5e0000000000000ULL,
    };
    uint64_t result = 0;
    for (uint8_t i = 0; i < 16; ++i) {
        // Branchless equality: arithmetic instead of == to avoid conditional branch (CWE-208)
        uint64_t eq = static_cast<uint64_t>(i) ^ static_cast<uint64_t>(index);
        uint64_t mask = ((eq | (~eq + 1)) >> 63) ^ 1;
        mask = static_cast<uint64_t>(0) - mask;
        result |= R4[i] & mask;
    }
    return result;
}

/// Bit-reverse a 4-bit nibble value.
///
/// GCM uses a reflected bit ordering where bit 0 of a byte is the MSB
/// (coefficient of x^0). When extracting nibbles from a byte or from
/// the low 4 bits of Z.lo, the bit positions within the nibble are
/// reversed relative to the GF(2^128) polynomial basis used by the
/// precomputed tables (both the H-multiple table and the reduction table).
/// This function maps the nibble back to the correct table index.
inline uint8_t rev4(uint8_t n) {
    return static_cast<uint8_t>(
        ((n & 1) << 3) | ((n & 2) << 1) | ((n & 4) >> 1) | ((n & 8) >> 3));
}

/// Constant-time GF(2^128) multiplication using the 4-bit precomputed table.
///
/// Processes X nibble-by-nibble using reversed Horner evaluation:
/// bytes are iterated from LAST to FIRST (byte 15 → byte 0), and within
/// each byte the low nibble (higher polynomial degrees) is processed
/// before the high nibble. Both nibble values AND the 4-bit reduction
/// remainder are bit-reversed before table lookup to account for GCM's
/// reflected bit ordering (NIST SP 800-38D §6.2).
///
/// All table lookups and shifts are data-independent — no branches or
/// variable-time memory accesses on secret data.
///
/// Ref: NIST SP 800-38D §6.3, CWE-208.
inline Block128 gf128_mul_ct(const GHashTable& table, const Block128& X) {
    Block128 Z{0, 0};
    uint8_t x_bytes[16];
    store_block(x_bytes, X);

    // Process from last byte to first (reversed Horner for correct GCM ordering)
    for (int byte_idx = 15; byte_idx >= 0; --byte_idx) {
        uint8_t b = x_bytes[byte_idx];

        // Process low nibble first (higher polynomial degrees, innermost Horner)
        {
            uint8_t rem = static_cast<uint8_t>(Z.lo & 0x0F);
            Z.lo = (Z.lo >> 4) | (Z.hi << 60);
            Z.hi = (Z.hi >> 4) ^ ct_reduce4(rev4(rem));
            Block128 entry = ct_table_lookup(table.entries,
                                             rev4(static_cast<uint8_t>(b & 0x0F)));
            Z = xor_blocks(Z, entry);
        }

        // Process high nibble (lower polynomial degrees)
        {
            uint8_t rem = static_cast<uint8_t>(Z.lo & 0x0F);
            Z.lo = (Z.lo >> 4) | (Z.hi << 60);
            Z.hi = (Z.hi >> 4) ^ ct_reduce4(rev4(rem));
            Block128 entry = ct_table_lookup(table.entries,
                                             rev4(static_cast<uint8_t>((b >> 4) & 0x0F)));
            Z = xor_blocks(Z, entry);
        }
    }

    return Z;
}

/// Multiply two elements in GF(2^128) using the schoolbook algorithm.
///
/// @note This function branches on X and is NOT constant-time.  It is
///       used ONLY during table precomputation (where X is a small
///       public index, not secret data) and for the standalone ghash()
///       helper.  The AesGcm class uses gf128_mul_ct() on the hot path.
inline Block128 gf128_mul(const Block128& X, const Block128& Y) {
    Block128 Z; // Accumulator, starts at 0
    Block128 V = Y; // Working copy

    // Iterate over all 128 bits of X
    for (int i = 0; i < 128; ++i) {
        // Check if bit i of X is set (GCM bit ordering: MSB first)
        uint64_t word = (i < 64) ? X.hi : X.lo;
        int bit_pos = (i < 64) ? (63 - i) : (63 - (i - 64));
        if (word & (uint64_t(1) << bit_pos)) {
            Z = xor_blocks(Z, V);
        }

        // Check LSB of V (bit 127 in GCM ordering = bit 0 of lo)
        bool lsb = (V.lo & 1) != 0;

        // Right-shift V by 1 bit
        V.lo = (V.lo >> 1) | ((V.hi & 1) << 63);
        V.hi = V.hi >> 1;

        // If LSB was set, XOR with R = 0xe100...0
        if (lsb) {
            V.hi ^= uint64_t(0xe1) << 56;
        }
    }

    return Z;
}

/// GHASH: Compute the GHASH function over data using hash subkey H.
///
/// GHASH processes 16-byte blocks:
///   X_0 = 0
///   X_i = (X_{i-1} ^ A_i) * H   for each block A_i
///
/// The input data is padded with zeros to a multiple of 16 bytes.
[[deprecated("Use ghash_update (constant-time) instead")]]
inline Block128 ghash(const Block128& H,
                      const uint8_t* data, size_t data_size) {
    Block128 X; // X_0 = 0

    size_t full_blocks = data_size / 16;
    for (size_t i = 0; i < full_blocks; ++i) {
        Block128 block = load_block(data + i * 16);
        X = xor_blocks(X, block);
        X = gf128_mul(X, H);
    }

    // Handle partial last block (zero-padded)
    size_t remainder = data_size % 16;
    if (remainder > 0) {
        uint8_t padded[16] = {};
        std::memcpy(padded, data + full_blocks * 16, remainder);
        Block128 block = load_block(padded);
        X = xor_blocks(X, block);
        X = gf128_mul(X, H);
    }

    return X;
}

/// Increment the rightmost 32 bits of a 16-byte counter block (big-endian).
inline void inc32(uint8_t counter[16]) {
    // The counter occupies bytes 12..15 (big-endian uint32)
    for (int i = 15; i >= 12; --i) {
        if (++counter[i] != 0) break;
    }
}

/// GCTR: AES-CTR encryption with the given initial counter block.
///
/// For each 16-byte block of data:
///   1. Encrypt the counter block with AES
///   2. XOR the encrypted counter with the data block
///   3. Increment the counter
///
/// The last block may be partial (only XOR the relevant bytes).
///
/// @note This function returns void and silently returns if the counter would
///       wrap (> 2^32-2 blocks). Callers MUST enforce MAX_GCM_PLAINTEXT (< 64 GB)
///       before calling gctr() to ensure this path is never reached.
///       AesGcm::encrypt()/decrypt() enforce this limit at lines 470-473.
inline void gctr(const Aes256& cipher,
                 const uint8_t icb[16],
                 const uint8_t* input, size_t input_size,
                 uint8_t* output) {
    if (input_size == 0) return;

    const size_t num_blocks = (input_size + 15) / 16;
    // NIST SP 800-38D §5.2.1.1: 32-bit counter must not wrap (max 2^32-2 blocks).
    // Protected by MAX_GCM_PLAINTEXT guard in calling code — this is a defense-in-depth check.
    if (num_blocks > 0xFFFFFFFEULL) return;

    uint8_t counter[16];
    std::memcpy(counter, icb, 16);

    size_t full_blocks = input_size / 16;
    for (size_t i = 0; i < full_blocks; ++i) {
        uint8_t encrypted_counter[16];
        std::memcpy(encrypted_counter, counter, 16);
        cipher.encrypt_block(encrypted_counter);

        for (int j = 0; j < 16; ++j) {
            output[i * 16 + j] = input[i * 16 + j] ^ encrypted_counter[j];
        }

        inc32(counter);
    }

    // Handle partial last block
    size_t remainder = input_size % 16;
    if (remainder > 0) {
        uint8_t encrypted_counter[16];
        std::memcpy(encrypted_counter, counter, 16);
        cipher.encrypt_block(encrypted_counter);

        size_t offset = full_blocks * 16;
        for (size_t j = 0; j < remainder; ++j) {
            output[offset + j] = input[offset + j] ^ encrypted_counter[j];
        }
    }
}

} // namespace detail::gcm

// ===========================================================================
// AesGcm -- AES-256-GCM authenticated encryption (NIST SP 800-38D)
// ===========================================================================

/// AES-256 in Galois/Counter Mode (GCM) as specified in NIST SP 800-38D.
///
/// Provides both confidentiality (encryption) and authenticity (128-bit
/// authentication tag). This is the mode used for Parquet footer encryption
/// where tamper detection is critical.
///
/// @note The IV/nonce MUST be unique per message under the same key. Reusing
///       an IV completely breaks GCM's authenticity guarantees.
/// @see AesCtr for unauthenticated column data encryption
class AesGcm {
public:
    static constexpr size_t KEY_SIZE = 32;    ///< AES-256 key size in bytes.
    static constexpr size_t IV_SIZE  = 12;    ///< Standard GCM nonce size in bytes (96 bits).
    static constexpr size_t TAG_SIZE = 16;    ///< Authentication tag size in bytes (128 bits).

    /// Initialize with a 32-byte key. Computes the hash subkey H = AES_K(0^128)
    /// and precomputes the 4-bit GHASH multiplication table for constant-time
    /// operation (NIST SP 800-38D §6.3, CWE-208).
    explicit AesGcm(const uint8_t key[KEY_SIZE])
        : cipher_(key) {
        // Compute hash subkey: H = AES_K(0^128)
        uint8_t zero_block[16] = {};
        cipher_.encrypt_block(zero_block);
        auto H = detail::gcm::load_block(zero_block);
        H_table_.init(H);
    }

    // -----------------------------------------------------------------------
    // encrypt -- Authenticated encryption with additional data (AEAD)
    //
    // Inputs:
    //   plaintext / plaintext_size: data to encrypt
    //   iv: 12-byte nonce (MUST be unique per message under the same key)
    //   aad / aad_size: additional authenticated data (authenticated but not
    //                   encrypted; may be nullptr if aad_size == 0)
    //
    // Output:
    //   ciphertext (same size as plaintext) with 16-byte auth tag appended.
    //   Total output size = plaintext_size + TAG_SIZE.
    //
    // Algorithm (NIST SP 800-38D Section 7.1):
    //   1. J0 = IV || 0x00000001  (initial counter block)
    //   2. ICB = inc32(J0)        (first counter for encryption)
    //   3. C = GCTR_K(ICB, P)     (encrypt plaintext)
    //   4. S = GHASH_H(A || pad || C || pad || [len(A)]_64 || [len(C)]_64)
    //   5. T = MSB_t(GCTR_K(J0, S))  (authentication tag)
    // -----------------------------------------------------------------------
    /// Set the expected IV size. Default is 12 bytes (96 bits, standard).
    /// Optionally supports 16 bytes; 16-byte IVs use GHASH-based J0 derivation
    /// per NIST SP 800-38D §5.2.1.2.
    /// @param size  Must be 12 or 16.
    /// @throws std::invalid_argument if size is neither 12 nor 16.
    void set_iv_size(size_t size) {
        if (size != 12 && size != 16) {
            throw std::invalid_argument("AES-GCM: IV size must be 12 or 16 bytes");
        }
        iv_size_ = size;
    }

    /// Get the current IV size (12 or 16 bytes).
    [[nodiscard]] size_t iv_size() const { return iv_size_; }

    /// Maximum plaintext size for a single GCM invocation (NIST SP 800-38D §5.2.1.1).
    /// 32-bit counter can address at most (2^32 - 2) blocks of 16 bytes each
    /// (counter value 1 is reserved for J0).
    static constexpr uint64_t MAX_GCM_PLAINTEXT =
        (static_cast<uint64_t>(UINT32_MAX) - 1) * 16; // ~64 GB

    /// NIST SP 800-38D §5.2.1.1: AAD length limit is 2^64-1 bits.
    /// Practical limit: 2^61-1 bytes (to avoid overflow when converting to bits).
    static constexpr uint64_t MAX_AAD_BYTES = (UINT64_MAX / 8);

    // Gap C-6: Compile-time assertion that TAG_SIZE is the full 128 bits.
    // NIST SP 800-38D §5.2.1.2 specifies allowed tag lengths {128,120,112,104,96,64,32}.
    // Truncated tags weaken authentication strength. Signet enforces full 128-bit tags
    // only — no truncation API is exposed. This static_assert guards against accidental
    // changes to TAG_SIZE.
    static_assert(TAG_SIZE == 16,
                  "GCM tag truncation prohibited (NIST SP 800-38D §5.2.1.2, CWE-328)");

    /// Authenticated encryption with additional data (AEAD).
    ///
    /// @param plaintext       Pointer to data to encrypt.
    /// @param plaintext_size  Number of bytes to encrypt.
    /// @param iv              12-byte nonce (MUST be unique per message under the same key).
    /// @param aad             Additional authenticated data (authenticated but not encrypted;
    ///                        may be nullptr if aad_size == 0).
    /// @param aad_size        Length of AAD in bytes.
    /// @return Ciphertext with 16-byte auth tag appended (total = plaintext_size + TAG_SIZE),
    ///         or an error if the plaintext exceeds the NIST maximum.
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt(
        const uint8_t* plaintext, size_t plaintext_size,
        const uint8_t iv[IV_SIZE],
        const uint8_t* aad = nullptr, size_t aad_size = 0) const {

        using namespace detail::gcm;

        if (static_cast<uint64_t>(plaintext_size) > MAX_GCM_PLAINTEXT) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-GCM: plaintext exceeds NIST SP 800-38D maximum"};
        }
        // Gap C-12: AAD length limit per NIST SP 800-38D §5.2.1.1
        if (static_cast<uint64_t>(aad_size) > MAX_AAD_BYTES) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-GCM: AAD exceeds NIST SP 800-38D §5.2.1.1 maximum (2^64-1 bits)"};
        }

        // Step 1: Derive J0 from IV (supports both 12-byte and 16-byte IVs)
        uint8_t J0[16] = {};
        derive_j0(iv, J0);

        // Step 2: Form ICB = inc32(J0) -- first counter for GCTR
        uint8_t ICB[16];
        std::memcpy(ICB, J0, 16);
        inc32(ICB); // Now counter = 2

        // Step 3: Encrypt plaintext with GCTR
        std::vector<uint8_t> output(plaintext_size + TAG_SIZE);
        gctr(cipher_, ICB, plaintext, plaintext_size, output.data());

        // Step 4: Compute GHASH over AAD and ciphertext
        //   S = GHASH_H(A || 0* || C || 0* || [len(A)]_64 || [len(C)]_64)
        //
        // We compute this incrementally:
        //   X = GHASH(H, AAD_padded)
        //   X = continue GHASH with ciphertext_padded
        //   X = continue GHASH with length block
        Block128 X; // X_0 = 0

        // Process AAD blocks
        if (aad != nullptr && aad_size > 0) {
            X = ghash_update(X, aad, aad_size);
        }

        // Process ciphertext blocks
        if (plaintext_size > 0) {
            X = ghash_update(X, output.data(), plaintext_size);
        }

        // Process length block: [len(A) in bits]_64 || [len(C) in bits]_64
        uint8_t len_block[16] = {};
        uint64_t aad_bits = static_cast<uint64_t>(aad_size) * 8;
        uint64_t ct_bits  = static_cast<uint64_t>(plaintext_size) * 8;
        // Store as big-endian uint64
        for (int i = 0; i < 8; ++i) {
            len_block[7 - i]  = static_cast<uint8_t>(aad_bits >> (8 * i));
            len_block[15 - i] = static_cast<uint8_t>(ct_bits >> (8 * i));
        }
        Block128 len_b = load_block(len_block);
        X = xor_blocks(X, len_b);
        X = gf128_mul_ct(H_table_, X);

        // Step 5: Compute authentication tag T = GCTR_K(J0, S)
        uint8_t S[16];
        store_block(S, X);
        uint8_t tag[16];
        gctr(cipher_, J0, S, 16, tag);

        // Append tag to output
        std::memcpy(output.data() + plaintext_size, tag, TAG_SIZE);

        return output;
    }

    // -----------------------------------------------------------------------
    // decrypt -- Authenticated decryption and verification
    //
    // Inputs:
    //   ciphertext_with_tag / total_size: ciphertext + 16-byte appended tag.
    //     total_size must be >= TAG_SIZE.
    //   iv: 12-byte nonce (same as used for encryption)
    //   aad / aad_size: additional authenticated data
    //
    // Output:
    //   On success: plaintext (total_size - TAG_SIZE bytes)
    //   On failure: ENCRYPTION_ERROR if authentication tag does not match
    //
    // Algorithm (NIST SP 800-38D Section 7.2):
    //   1. Separate C and T from the input
    //   2. J0 = IV || 0x00000001
    //   3. ICB = inc32(J0)
    //   4. P = GCTR_K(ICB, C)           (decrypt ciphertext)
    //   5. S = GHASH_H(A || pad || C || pad || len_block)
    //   6. T' = MSB_t(GCTR_K(J0, S))   (recompute tag)
    //   7. If T != T': return error     (authentication failed)
    // -----------------------------------------------------------------------
    /// Authenticated decryption and verification (NIST SP 800-38D Section 7.2).
    ///
    /// @param ciphertext_with_tag  Pointer to ciphertext + 16-byte appended tag.
    /// @param total_size           Total input size (ciphertext + TAG_SIZE); must be >= TAG_SIZE.
    /// @param iv                   12-byte nonce (same as used for encryption).
    /// @param aad                  Additional authenticated data (same as encryption).
    /// @param aad_size             Length of AAD in bytes.
    /// @return Plaintext (total_size - TAG_SIZE bytes) on success, or
    ///         ENCRYPTION_ERROR if the authentication tag does not match.
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt(
        const uint8_t* ciphertext_with_tag, size_t total_size,
        const uint8_t iv[IV_SIZE],
        const uint8_t* aad = nullptr, size_t aad_size = 0) const {

        using namespace detail::gcm;

        // Validate minimum size
        if (total_size < TAG_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-GCM: input too short for authentication tag"};
        }

        if (static_cast<uint64_t>(total_size - TAG_SIZE) > MAX_GCM_PLAINTEXT) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-GCM: ciphertext exceeds NIST SP 800-38D maximum"};
        }
        // Gap C-12: AAD length limit per NIST SP 800-38D §5.2.1.1
        if (static_cast<uint64_t>(aad_size) > MAX_AAD_BYTES) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-GCM: AAD exceeds NIST SP 800-38D §5.2.1.1 maximum"};
        }

        size_t ciphertext_size = total_size - TAG_SIZE;
        const uint8_t* ciphertext = ciphertext_with_tag;
        const uint8_t* received_tag = ciphertext_with_tag + ciphertext_size;

        // Step 1: Derive J0 from IV (supports both 12-byte and 16-byte IVs)
        uint8_t J0[16] = {};
        derive_j0(iv, J0);

        // Step 2: Recompute GHASH over AAD and ciphertext (BEFORE decryption)
        Block128 X;

        // Process AAD blocks
        if (aad != nullptr && aad_size > 0) {
            X = ghash_update(X, aad, aad_size);
        }

        // Process ciphertext blocks
        if (ciphertext_size > 0) {
            X = ghash_update(X, ciphertext, ciphertext_size);
        }

        // Process length block
        uint8_t len_block[16] = {};
        uint64_t aad_bits = static_cast<uint64_t>(aad_size) * 8;
        uint64_t ct_bits  = static_cast<uint64_t>(ciphertext_size) * 8;
        for (int i = 0; i < 8; ++i) {
            len_block[7 - i]  = static_cast<uint8_t>(aad_bits >> (8 * i));
            len_block[15 - i] = static_cast<uint8_t>(ct_bits >> (8 * i));
        }
        Block128 len_b = load_block(len_block);
        X = xor_blocks(X, len_b);
        X = gf128_mul_ct(H_table_, X);

        // Step 3: Compute expected tag T' = GCTR_K(J0, S)
        uint8_t S[16];
        store_block(S, X);
        uint8_t expected_tag[16];
        gctr(cipher_, J0, S, 16, expected_tag);

        // Step 4: Constant-time tag comparison (prevents timing attacks)
        uint8_t diff = 0;
        for (int i = 0; i < static_cast<int>(TAG_SIZE); ++i) {
            diff |= received_tag[i] ^ expected_tag[i];
        }
        if (diff != 0) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-GCM: authentication tag mismatch"};
        }

        // Step 5: Decrypt ciphertext
        uint8_t ICB[16];
        std::memcpy(ICB, J0, 16);
        inc32(ICB);

        std::vector<uint8_t> plaintext(ciphertext_size);
        gctr(cipher_, ICB, ciphertext, ciphertext_size, plaintext.data());

        return plaintext;
    }

private:
    Aes256 cipher_;                    ///< Underlying AES-256 block cipher.
    detail::gcm::GHashTable H_table_;  ///< Precomputed 4-bit GHASH table from H = AES_K(0^128).
    size_t iv_size_{IV_SIZE};          ///< Current IV size: 12 (default) or 16 bytes.

    /// Derive J0 from an IV of arbitrary (configured) size.
    /// - 12-byte IV: J0 = IV || 0x00000001 (NIST SP 800-38D §5.2.1.1)
    /// - 16-byte IV: J0 = GHASH_H(IV || pad || [len(IV)]_64) (§5.2.1.2)
    void derive_j0(const uint8_t* iv, uint8_t j0[16]) const {
        using namespace detail::gcm;
        if (iv_size_ == 12) {
            std::memcpy(j0, iv, 12);
            j0[12] = 0x00; j0[13] = 0x00; j0[14] = 0x00; j0[15] = 0x01;
        } else {
            // GHASH-based derivation for non-96-bit IV (§5.2.1.2)
            Block128 X{};
            // Process IV as GHASH input (single 16-byte block)
            Block128 iv_block = load_block(iv);
            X = xor_blocks(X, iv_block);
            X = gf128_mul_ct(H_table_, X);
            // Length block: [0]_64 || [len(IV) in bits]_64
            uint8_t len_block[16] = {};
            uint64_t iv_bits = static_cast<uint64_t>(iv_size_) * 8;
            for (int i = 0; i < 8; ++i) {
                len_block[15 - i] = static_cast<uint8_t>(iv_bits >> (8 * i));
            }
            Block128 lb = load_block(len_block);
            X = xor_blocks(X, lb);
            X = gf128_mul_ct(H_table_, X);
            store_block(j0, X);
        }
    }

    /// Incrementally update GHASH state X with data (zero-padded to 16 bytes).
    /// Uses the constant-time 4-bit table multiplication (CWE-208).
    detail::gcm::Block128 ghash_update(
        detail::gcm::Block128 X,
        const uint8_t* data, size_t data_size) const {

        using namespace detail::gcm;

        size_t full_blocks = data_size / 16;
        for (size_t i = 0; i < full_blocks; ++i) {
            Block128 block = load_block(data + i * 16);
            X = xor_blocks(X, block);
            X = gf128_mul_ct(H_table_, X);
        }

        // Handle partial last block (zero-padded)
        size_t remainder = data_size % 16;
        if (remainder > 0) {
            uint8_t padded[16] = {};
            std::memcpy(padded, data + full_blocks * 16, remainder);
            Block128 block = load_block(padded);
            X = xor_blocks(X, block);
            X = gf128_mul_ct(H_table_, X);
        }

        return X;
    }
};

} // namespace signet::forge::crypto
