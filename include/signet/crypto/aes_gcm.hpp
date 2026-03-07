// SPDX-License-Identifier: Apache-2.0
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

/// Multiply two elements in GF(2^128) using the GCM reducing polynomial.
///
/// Algorithm (schoolbook, bit-serial):
///   Z = 0
///   V = Y
///   for i = 0 to 127:
///     if bit i of X is set:
///       Z = Z ^ V
///     if LSB of V is 0:
///       V = V >> 1
///     else:
///       V = (V >> 1) ^ R
///
/// Bit ordering: In GCM, bit 0 of a byte is the MSB (leftmost). So bit i
/// of the 128-bit block X corresponds to:
///   byte index = i / 8
///   bit within byte = 7 - (i % 8)  (MSB-first)
///
/// Equivalently, in our Block128 representation:
///   bit 0   = MSB of hi (bit 63 of hi)
///   bit 63  = LSB of hi (bit 0 of hi)
///   bit 64  = MSB of lo (bit 63 of lo)
///   bit 127 = LSB of lo (bit 0 of lo)
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
inline void gctr(const Aes256& cipher,
                 const uint8_t icb[16],
                 const uint8_t* input, size_t input_size,
                 uint8_t* output) {
    if (input_size == 0) return;

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

    /// Initialize with a 32-byte key. Computes the hash subkey H = AES_K(0^128).
    explicit AesGcm(const uint8_t key[KEY_SIZE])
        : cipher_(key) {
        // Compute hash subkey: H = AES_K(0^128)
        uint8_t zero_block[16] = {};
        cipher_.encrypt_block(zero_block);
        H_ = detail::gcm::load_block(zero_block);
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
    /// Maximum plaintext size for a single GCM invocation (NIST SP 800-38D limit).
    static constexpr uint64_t MAX_GCM_PLAINTEXT = (1ULL << 36) - 32; // ~64 GB

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

        // Step 1: Form J0 = IV (12 bytes) || 0x00000001
        uint8_t J0[16] = {};
        std::memcpy(J0, iv, IV_SIZE);
        J0[12] = 0x00;
        J0[13] = 0x00;
        J0[14] = 0x00;
        J0[15] = 0x01;

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
        X = gf128_mul(X, H_);

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

        size_t ciphertext_size = total_size - TAG_SIZE;
        const uint8_t* ciphertext = ciphertext_with_tag;
        const uint8_t* received_tag = ciphertext_with_tag + ciphertext_size;

        // Step 1: Form J0
        uint8_t J0[16] = {};
        std::memcpy(J0, iv, IV_SIZE);
        J0[12] = 0x00;
        J0[13] = 0x00;
        J0[14] = 0x00;
        J0[15] = 0x01;

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
        X = gf128_mul(X, H_);

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
    Aes256 cipher_;               ///< Underlying AES-256 block cipher.
    detail::gcm::Block128 H_;     ///< GCM hash subkey: H = AES_K(0^128).

    /// Incrementally update GHASH state X with data (zero-padded to 16 bytes).
    detail::gcm::Block128 ghash_update(
        detail::gcm::Block128 X,
        const uint8_t* data, size_t data_size) const {

        using namespace detail::gcm;

        size_t full_blocks = data_size / 16;
        for (size_t i = 0; i < full_blocks; ++i) {
            Block128 block = load_block(data + i * 16);
            X = xor_blocks(X, block);
            X = gf128_mul(X, H_);
        }

        // Handle partial last block (zero-padded)
        size_t remainder = data_size % 16;
        if (remainder > 0) {
            uint8_t padded[16] = {};
            std::memcpy(padded, data + full_blocks * 16, remainder);
            Block128 block = load_block(padded);
            X = xor_blocks(X, block);
            X = gf128_mul(X, H_);
        }

        return X;
    }
};

} // namespace signet::forge::crypto
