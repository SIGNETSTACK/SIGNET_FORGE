// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file aes_ctr.hpp
/// @brief AES-256-CTR stream cipher implementation (NIST SP 800-38A).

// ---------------------------------------------------------------------------
// aes_ctr.hpp -- Bundled, zero-dependency, header-only AES-256-CTR
//
// Implements AES-256 in Counter Mode (CTR) as specified in:
//   NIST SP 800-38A: Recommendation for Block Cipher Modes of Operation
//   https://csrc.nist.gov/publications/detail/sp/800-38a/final
//
// CTR mode converts AES into a stream cipher: for each 16-byte block, the
// counter block is encrypted with AES, and the result is XORed with the data.
// Since XOR is its own inverse, encryption and decryption are identical.
//
// This mode is used for Parquet column data encryption where authentication
// is not needed (column integrity is verified by page checksums).
//
// Parameters:
//   Key size:  32 bytes (256 bits, AES-256)
//   IV size:   16 bytes (full 128-bit IV/counter initial value)
//   Counter:   Last 4 bytes of the 16-byte block, incremented big-endian
//
// NIST SP 800-38A test vector (F.5.5 -- CTR-AES256.Encrypt):
//   Key:       603deb1015ca71be2b73aef0857d7781
//              1f352c073b6108d72d9810a30914dff4
//   IV/CTR:    f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff
//   Plaintext: 6bc1bee22e409f96e93d7e117393172a
//              ae2d8a571e03ac9c9eb76fac45af8e51
//              30c81c46a35ce411e5fbc1191a0a52ef
//              f69f2445df4f9b17ad2b417be66c3710
//   Ciphertext:601ec313775789a5b7a7f504bbf3d228
//              f443e3ca4d62b59aca84e990cacaf5c5
//              2b0930daa23de94ce87017ba2d84988d
//              dfc9c58db67aada613c2dd08457941a6
// ---------------------------------------------------------------------------

#include "signet/crypto/aes_core.hpp"
#include "signet/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace signet::forge::crypto {

// ===========================================================================
// AesCtr -- AES-256-CTR stream cipher (NIST SP 800-38A)
// ===========================================================================

/// AES-256 in Counter Mode (CTR) as specified in NIST SP 800-38A.
///
/// CTR mode converts AES into a stream cipher by encrypting sequential counter
/// blocks and XORing the resulting keystream with the data. Because XOR is its
/// own inverse, encryption and decryption are the same operation.
///
/// This mode is used for Parquet column data encryption where authentication
/// is not needed (column integrity is verified by page checksums).
///
/// @note Counter increment uses the last 4 bytes of the 16-byte IV as a
///       big-endian uint32, matching the Parquet PME specification.
/// @see AesGcm for authenticated encryption (footer encryption)
/// @see NIST SP 800-38A Section 6.5 (CTR mode)
class AesCtr {
public:
    static constexpr size_t KEY_SIZE = 32;    ///< AES-256 key size in bytes.
    static constexpr size_t IV_SIZE  = 16;    ///< Full 128-bit IV/counter size in bytes.

    /// Initialize with a 32-byte key.
    explicit AesCtr(const uint8_t key[KEY_SIZE])
        : cipher_(key) {}

    AesCtr(const AesCtr&) = delete;
    AesCtr& operator=(const AesCtr&) = delete;

    // -----------------------------------------------------------------------
    // process -- Encrypt or decrypt data using AES-CTR
    //
    // CTR mode is symmetric: encrypt(plaintext) == decrypt(ciphertext)
    // because the keystream is generated independently and simply XORed.
    //
    // Algorithm:
    //   For block i (0-indexed):
    //     1. counter_block = IV with last 4 bytes = (initial_ctr + i) in BE
    //     2. keystream_block = AES_K(counter_block)
    //     3. output_block = input_block ^ keystream_block
    //
    // The last block may be partial -- only the needed keystream bytes
    // are used.
    //
    // Counter increment: The last 4 bytes of the 16-byte IV are treated
    // as a big-endian uint32 and incremented by 1 for each block. The
    // first 12 bytes (nonce portion) remain constant.
    // -----------------------------------------------------------------------
    /// Encrypt or decrypt data using AES-CTR.
    ///
    /// CTR mode is symmetric: encrypt(plaintext) == decrypt(ciphertext),
    /// because the keystream is generated independently and simply XORed.
    ///
    /// @param data   Pointer to input bytes (plaintext or ciphertext).
    /// @param size   Number of bytes to process.
    /// @param iv     16-byte initialization vector / initial counter value.
    /// @return Processed output bytes (same length as input).
    [[nodiscard]] expected<std::vector<uint8_t>> process(
        const uint8_t* data, size_t size,
        const uint8_t iv[IV_SIZE]) const {

        std::vector<uint8_t> output(size);
        if (size == 0) return output;

        // Counter overflow guard: extract initial 32-bit counter from IV's
        // last 4 bytes (big-endian) and compute actual remaining blocks.
        uint32_t initial_counter =
            (static_cast<uint32_t>(iv[12]) << 24) |
            (static_cast<uint32_t>(iv[13]) << 16) |
            (static_cast<uint32_t>(iv[14]) <<  8) |
            (static_cast<uint32_t>(iv[15]));
        uint64_t max_blocks = 0xFFFFFFFFULL - static_cast<uint64_t>(initial_counter) + 1;
        uint64_t max_bytes  = max_blocks * 16;
        if (static_cast<uint64_t>(size) > max_bytes)
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-CTR: data size exceeds 32-bit counter space for this IV"};

        // Initialize counter block from IV
        uint8_t counter[16];
        std::memcpy(counter, iv, 16);

        size_t offset = 0;

        while (offset < size) {
            // Encrypt the counter block to produce keystream
            uint8_t keystream[16];
            std::memcpy(keystream, counter, 16);
            cipher_.encrypt_block(keystream);

            // XOR keystream with data (handle partial last block)
            size_t block_len = size - offset;
            if (block_len > 16) block_len = 16;

            for (size_t j = 0; j < block_len; ++j) {
                output[offset + j] = data[offset + j] ^ keystream[j];
            }

            offset += block_len;

            // Increment counter (last 4 bytes, big-endian)
            inc_counter(counter);
        }

        return output;
    }

    /// Convenience alias for process() -- encrypt data with AES-CTR.
    /// @param data  Pointer to plaintext bytes.
    /// @param size  Number of bytes to encrypt.
    /// @param iv    16-byte initialization vector.
    /// @return Ciphertext bytes (same length as input).
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt(
        const uint8_t* data, size_t size,
        const uint8_t iv[IV_SIZE]) const {
        return process(data, size, iv);
    }

    /// Convenience alias for process() -- decrypt data with AES-CTR.
    /// @param data  Pointer to ciphertext bytes.
    /// @param size  Number of bytes to decrypt.
    /// @param iv    16-byte initialization vector (must match encryption IV).
    /// @return Plaintext bytes (same length as input).
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt(
        const uint8_t* data, size_t size,
        const uint8_t iv[IV_SIZE]) const {
        return process(data, size, iv);
    }

private:
    Aes256 cipher_;  ///< Underlying AES-256 block cipher instance.

    /// Increment the last 4 bytes of the counter block (big-endian uint32).
    static void inc_counter(uint8_t counter[16]) {
        for (int i = 15; i >= 12; --i) {
            if (++counter[i] != 0) break;
        }
    }
};

} // namespace signet::forge::crypto
