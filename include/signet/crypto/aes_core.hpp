// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file aes_core.hpp
/// @brief AES-256 block cipher implementation (FIPS-197).

// ---------------------------------------------------------------------------
// aes_core.hpp -- Bundled, zero-dependency, header-only AES-256 block cipher
//
// Implements the AES-256 block cipher as specified in FIPS-197:
//   https://csrc.nist.gov/publications/detail/fips/197/final
//
// This is a clean-room, table-based implementation providing correct AES-256
// encrypt and decrypt for a single 128-bit block. Higher-level modes (GCM,
// CTR) are built on top in separate headers.
//
// AES-256 parameters:
//   Key size:    32 bytes (256 bits)
//   Block size:  16 bytes (128 bits)
//   Rounds:      14
//   Round keys:  15 (initial + 14 rounds) = 240 bytes total
//
// FIPS-197 test vector (Appendix C.3 -- AES-256):
//   Key:       000102030405060708090a0b0c0d0e0f
//              101112131415161718191a1b1c1d1e1f
//   Plaintext: 00112233445566778899aabbccddeeff
//   Ciphertext:8ea2b7ca516745bfeafc49904b496089
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace signet::forge::crypto {

// ===========================================================================
// AES S-box and inverse S-box (FIPS-197 Section 5.1.1)
// ===========================================================================
namespace detail::aes {

/// Forward S-box: SubBytes substitution table (FIPS-197 Section 5.1.1).
static constexpr uint8_t SBOX[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
    0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
    0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
    0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
    0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
    0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
    0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
    0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
    0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
    0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
    0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
    0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

/// Inverse S-box: InvSubBytes substitution table (FIPS-197 Section 5.3.2).
static constexpr uint8_t INV_SBOX[256] = {
    0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
    0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
    0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
    0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
    0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
    0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
    0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
    0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
    0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
    0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
    0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
    0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
    0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
    0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
    0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
    0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
    0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
    0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
    0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
    0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
    0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
    0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
    0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
    0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
    0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
    0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
    0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
    0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
    0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
    0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
    0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

/// Round constants for key expansion (FIPS-197 Section 5.2).
///
/// Rcon[i] = x^(i-1) in GF(2^8); only the first byte is non-zero.
/// AES-256 uses up to Rcon[7] (7 key expansion rounds).
static constexpr uint8_t RCON[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

// ===========================================================================
// GF(2^8) arithmetic for MixColumns (FIPS-197 Section 4.2)
//
// The irreducible polynomial is: x^8 + x^4 + x^3 + x + 1  (0x11b)
// ===========================================================================

/// Multiply by x (i.e., by 2) in GF(2^8). This is the "xtime" operation.
/// If the high bit is set, XOR with 0x1b after shifting (reduction mod 0x11b).
inline constexpr uint8_t xtime(uint8_t a) {
    return static_cast<uint8_t>((a << 1) ^ ((a & 0x80) ? 0x1b : 0x00));
}

/// Multiply two elements in GF(2^8) using the Russian peasant algorithm.
/// This is used for MixColumns and InvMixColumns.
inline constexpr uint8_t gf_mul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    for (int i = 0; i < 8; ++i) {
        if (b & 1) {
            result ^= a;
        }
        uint8_t hi_bit = a & 0x80;
        a <<= 1;
        if (hi_bit) {
            a ^= 0x1b; // Reduce modulo x^8 + x^4 + x^3 + x + 1
        }
        b >>= 1;
    }
    return result;
}

// ===========================================================================
// SubWord / RotWord helpers for key expansion
// ===========================================================================

/// Apply S-box to each byte of a 4-byte word.
inline void sub_word(uint8_t word[4]) {
    word[0] = SBOX[word[0]];
    word[1] = SBOX[word[1]];
    word[2] = SBOX[word[2]];
    word[3] = SBOX[word[3]];
}

/// Rotate a 4-byte word left by one byte: [a,b,c,d] -> [b,c,d,a].
inline void rot_word(uint8_t word[4]) {
    uint8_t tmp = word[0];
    word[0] = word[1];
    word[1] = word[2];
    word[2] = word[3];
    word[3] = tmp;
}

/// Securely zero memory that held key material (CWE-244, NIST SP 800-38D §8.3).
///
/// Uses volatile write + compiler barrier to prevent dead-store elimination.
/// This is the approach used by libsodium and BoringSSL — portable across
/// all compilers and platforms without relying on non-standard APIs.
inline void secure_zero(void* ptr, size_t len) {
    if (len == 0) return;
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    for (size_t i = 0; i < len; ++i) p[i] = 0;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" ::: "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#endif
}

} // namespace detail::aes

// ===========================================================================
// Aes256 -- AES-256 block cipher (FIPS-197)
// ===========================================================================

/// AES-256 block cipher (FIPS-197).
///
/// Clean-room, table-based implementation providing correct AES-256 encrypt
/// and decrypt for a single 128-bit block. Higher-level modes (GCM, CTR) are
/// built on top in separate headers.
///
/// @note The destructor securely zeroes all round key material using volatile
///       writes to prevent optimiser elimination.
/// @see AesGcm, AesCtr for higher-level modes
class Aes256 {
public:
    static constexpr size_t KEY_SIZE   = 32;    ///< Key size in bytes (256 bits).
    static constexpr size_t BLOCK_SIZE = 16;    ///< Block size in bytes (128 bits).
    static constexpr int    NUM_ROUNDS = 14;    ///< Number of AES-256 rounds.

    /// Initialize with a 32-byte key. Performs key expansion immediately.
    explicit Aes256(const uint8_t key[KEY_SIZE]) {
        key_expansion(key);
    }

    /// Destructor: securely zero round keys to prevent key material leakage.
    ~Aes256() {
        detail::aes::secure_zero(round_keys_, sizeof(round_keys_));
    }

    /// Encrypt a single 16-byte block in-place (FIPS-197 Section 5.1).
    ///
    /// Applies AddRoundKey(0), then 13 rounds of SubBytes/ShiftRows/MixColumns/
    /// AddRoundKey, followed by a final round without MixColumns.
    ///
    /// @param block 16-byte plaintext block; overwritten with ciphertext.
    void encrypt_block(uint8_t block[BLOCK_SIZE]) const {
        // State is organized as a 4x4 column-major matrix:
        //   state[row][col] = block[row + 4*col]
        // We operate directly on the flat block array using index arithmetic.

        // Round 0: AddRoundKey
        add_round_key(block, 0);

        // Rounds 1..13: SubBytes, ShiftRows, MixColumns, AddRoundKey
        for (int round = 1; round < NUM_ROUNDS; ++round) {
            sub_bytes(block);
            shift_rows(block);
            mix_columns(block);
            add_round_key(block, round);
        }

        // Final round (14): SubBytes, ShiftRows, AddRoundKey (no MixColumns)
        sub_bytes(block);
        shift_rows(block);
        add_round_key(block, NUM_ROUNDS);
    }

    /// Decrypt a single 16-byte block in-place (FIPS-197 Section 5.3).
    ///
    /// Uses the "direct" inverse cipher: AddRoundKey(14), then 13 rounds of
    /// InvShiftRows/InvSubBytes/AddRoundKey/InvMixColumns, followed by a
    /// final round without InvMixColumns.
    ///
    /// @param block 16-byte ciphertext block; overwritten with plaintext.
    void decrypt_block(uint8_t block[BLOCK_SIZE]) const {
        // Round 14: AddRoundKey
        add_round_key(block, NUM_ROUNDS);

        // Rounds 13..1: InvShiftRows, InvSubBytes, AddRoundKey, InvMixColumns
        for (int round = NUM_ROUNDS - 1; round >= 1; --round) {
            inv_shift_rows(block);
            inv_sub_bytes(block);
            add_round_key(block, round);
            inv_mix_columns(block);
        }

        // Final round (0): InvShiftRows, InvSubBytes, AddRoundKey
        inv_shift_rows(block);
        inv_sub_bytes(block);
        add_round_key(block, 0);
    }

private:
    // 15 round keys * 16 bytes each = 240 bytes
    uint8_t round_keys_[BLOCK_SIZE * (NUM_ROUNDS + 1)]; // 240 bytes

    // -----------------------------------------------------------------------
    // Key expansion (FIPS-197 Section 5.2)
    //
    // AES-256 (Nk=8, Nr=14):
    //   - The key schedule produces 60 32-bit words (w[0..59]).
    //   - w[0..7] = the original key.
    //   - For i >= 8:
    //       if i mod 8 == 0: w[i] = w[i-8] ^ SubWord(RotWord(w[i-1])) ^ Rcon[i/8-1]
    //       if i mod 8 == 4: w[i] = w[i-8] ^ SubWord(w[i-1])
    //       otherwise:       w[i] = w[i-8] ^ w[i-1]
    // -----------------------------------------------------------------------
    void key_expansion(const uint8_t key[KEY_SIZE]) {
        // Nk = 8 words (32 bytes), Nb = 4 words (16 bytes), Nr = 14 rounds
        // Total words = Nb * (Nr + 1) = 4 * 15 = 60
        static constexpr int NK         = 8;  // Key length in 32-bit words
        static constexpr int TOTAL_WORDS = 4 * (NUM_ROUNDS + 1); // 60

        // Copy the original key into the first 8 words
        std::memcpy(round_keys_, key, KEY_SIZE);

        // Expand the remaining words
        uint8_t temp[4];
        for (int i = NK; i < TOTAL_WORDS; ++i) {
            // Copy w[i-1] into temp
            std::memcpy(temp, &round_keys_[(i - 1) * 4], 4);

            if (i % NK == 0) {
                // RotWord + SubWord + Rcon
                detail::aes::rot_word(temp);
                detail::aes::sub_word(temp);
                temp[0] ^= detail::aes::RCON[i / NK - 1];
            } else if (i % NK == 4) {
                // AES-256 extra step: SubWord only (no RotWord, no Rcon)
                detail::aes::sub_word(temp);
            }

            // w[i] = w[i-Nk] ^ temp
            round_keys_[i * 4 + 0] = round_keys_[(i - NK) * 4 + 0] ^ temp[0];
            round_keys_[i * 4 + 1] = round_keys_[(i - NK) * 4 + 1] ^ temp[1];
            round_keys_[i * 4 + 2] = round_keys_[(i - NK) * 4 + 2] ^ temp[2];
            round_keys_[i * 4 + 3] = round_keys_[(i - NK) * 4 + 3] ^ temp[3];
        }
        detail::aes::secure_zero(temp, sizeof(temp));
    }

    // -----------------------------------------------------------------------
    // AddRoundKey -- XOR state with round key (FIPS-197 Section 5.1.4)
    // -----------------------------------------------------------------------
    void add_round_key(uint8_t block[BLOCK_SIZE], int round) const {
        const uint8_t* rk = &round_keys_[round * BLOCK_SIZE];
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            block[i] ^= rk[i];
        }
    }

    // -----------------------------------------------------------------------
    // SubBytes -- Apply S-box to every byte (FIPS-197 Section 5.1.1)
    // -----------------------------------------------------------------------
    static void sub_bytes(uint8_t block[BLOCK_SIZE]) {
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            block[i] = detail::aes::SBOX[block[i]];
        }
    }

    // -----------------------------------------------------------------------
    // InvSubBytes -- Apply inverse S-box (FIPS-197 Section 5.3.2)
    // -----------------------------------------------------------------------
    static void inv_sub_bytes(uint8_t block[BLOCK_SIZE]) {
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            block[i] = detail::aes::INV_SBOX[block[i]];
        }
    }

    // -----------------------------------------------------------------------
    // ShiftRows -- Cyclically shift rows left (FIPS-197 Section 5.1.2)
    //
    // The state is column-major:
    //   block[0]  block[4]  block[8]  block[12]    (row 0: no shift)
    //   block[1]  block[5]  block[9]  block[13]    (row 1: shift left 1)
    //   block[2]  block[6]  block[10] block[14]    (row 2: shift left 2)
    //   block[3]  block[7]  block[11] block[15]    (row 3: shift left 3)
    // -----------------------------------------------------------------------
    static void shift_rows(uint8_t block[BLOCK_SIZE]) {
        uint8_t tmp;

        // Row 1: shift left by 1
        tmp       = block[1];
        block[1]  = block[5];
        block[5]  = block[9];
        block[9]  = block[13];
        block[13] = tmp;

        // Row 2: shift left by 2
        tmp       = block[2];
        block[2]  = block[10];
        block[10] = tmp;
        tmp       = block[6];
        block[6]  = block[14];
        block[14] = tmp;

        // Row 3: shift left by 3 (= shift right by 1)
        tmp       = block[15];
        block[15] = block[11];
        block[11] = block[7];
        block[7]  = block[3];
        block[3]  = tmp;
    }

    // -----------------------------------------------------------------------
    // InvShiftRows -- Cyclically shift rows right (FIPS-197 Section 5.3.1)
    // -----------------------------------------------------------------------
    static void inv_shift_rows(uint8_t block[BLOCK_SIZE]) {
        uint8_t tmp;

        // Row 1: shift right by 1
        tmp       = block[13];
        block[13] = block[9];
        block[9]  = block[5];
        block[5]  = block[1];
        block[1]  = tmp;

        // Row 2: shift right by 2
        tmp       = block[2];
        block[2]  = block[10];
        block[10] = tmp;
        tmp       = block[6];
        block[6]  = block[14];
        block[14] = tmp;

        // Row 3: shift right by 3 (= shift left by 1)
        tmp       = block[3];
        block[3]  = block[7];
        block[7]  = block[11];
        block[11] = block[15];
        block[15] = tmp;
    }

    // -----------------------------------------------------------------------
    // MixColumns -- Mix each column using GF(2^8) (FIPS-197 Section 5.1.3)
    //
    // Each column [s0, s1, s2, s3] is multiplied by the fixed matrix:
    //   [2 3 1 1]   [s0]
    //   [1 2 3 1] * [s1]
    //   [1 1 2 3]   [s2]
    //   [3 1 1 2]   [s3]
    // -----------------------------------------------------------------------
    static void mix_columns(uint8_t block[BLOCK_SIZE]) {
        for (int col = 0; col < 4; ++col) {
            int i = col * 4; // Column starts at block[i]

            uint8_t s0 = block[i + 0];
            uint8_t s1 = block[i + 1];
            uint8_t s2 = block[i + 2];
            uint8_t s3 = block[i + 3];

            block[i + 0] = detail::aes::gf_mul(2, s0) ^ detail::aes::gf_mul(3, s1)
                         ^ s2 ^ s3;
            block[i + 1] = s0 ^ detail::aes::gf_mul(2, s1) ^ detail::aes::gf_mul(3, s2)
                         ^ s3;
            block[i + 2] = s0 ^ s1 ^ detail::aes::gf_mul(2, s2)
                         ^ detail::aes::gf_mul(3, s3);
            block[i + 3] = detail::aes::gf_mul(3, s0) ^ s1 ^ s2
                         ^ detail::aes::gf_mul(2, s3);
        }
    }

    // -----------------------------------------------------------------------
    // InvMixColumns -- Inverse of MixColumns (FIPS-197 Section 5.3.3)
    //
    // Each column is multiplied by the inverse matrix:
    //   [14 11 13  9]   [s0]
    //   [ 9 14 11 13] * [s1]
    //   [13  9 14 11]   [s2]
    //   [11 13  9 14]   [s3]
    // -----------------------------------------------------------------------
    static void inv_mix_columns(uint8_t block[BLOCK_SIZE]) {
        for (int col = 0; col < 4; ++col) {
            int i = col * 4;

            uint8_t s0 = block[i + 0];
            uint8_t s1 = block[i + 1];
            uint8_t s2 = block[i + 2];
            uint8_t s3 = block[i + 3];

            block[i + 0] = detail::aes::gf_mul(14, s0) ^ detail::aes::gf_mul(11, s1)
                         ^ detail::aes::gf_mul(13, s2) ^ detail::aes::gf_mul( 9, s3);
            block[i + 1] = detail::aes::gf_mul( 9, s0) ^ detail::aes::gf_mul(14, s1)
                         ^ detail::aes::gf_mul(11, s2) ^ detail::aes::gf_mul(13, s3);
            block[i + 2] = detail::aes::gf_mul(13, s0) ^ detail::aes::gf_mul( 9, s1)
                         ^ detail::aes::gf_mul(14, s2) ^ detail::aes::gf_mul(11, s3);
            block[i + 3] = detail::aes::gf_mul(11, s0) ^ detail::aes::gf_mul(13, s1)
                         ^ detail::aes::gf_mul( 9, s2) ^ detail::aes::gf_mul(14, s3);
        }
    }
};

} // namespace signet::forge::crypto
