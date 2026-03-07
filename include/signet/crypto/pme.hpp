// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file pme.hpp
/// @brief Parquet Modular Encryption (PME) orchestrator -- encrypts and decrypts
///        Parquet file components (footer, column metadata, data pages).

// ---------------------------------------------------------------------------
// pme.hpp -- Parquet Modular Encryption (PME) orchestrator
//
// This is the main interface for encrypting and decrypting Parquet file
// components according to the Parquet Modular Encryption specification.
//
// PME encrypts individual Parquet modules (footer, column metadata, data
// pages) independently, each with its own key and AAD context. This allows:
//
//   - Different columns to use different keys (column-level access control)
//   - Footer encryption or signed-plaintext footer
//   - Per-module AAD binding (prevents ciphertext transplant attacks)
//
// Wire format for encrypted modules:
//   [1 byte: IV size] [IV bytes] [ciphertext (+ GCM tag if applicable)]
//
// AAD construction (Parquet PME standard):
//   aad_prefix + '\0' + module_type_byte + '\0' + extra
//
// Module types:
//   0 = FOOTER
//   1 = COLUMN_META
//   2 = DATA_PAGE
//   3 = DICT_PAGE
//   4 = DATA_PAGE_HEADER
//   5 = COLUMN_META_HEADER
//
// Footer encryption always uses AES-GCM (authenticated).
// Column data encryption uses AES-GCM or AES-CTR depending on the
// EncryptionAlgorithm setting.
//
// References:
//   - Apache Parquet Modular Encryption (PARQUET-1178)
//   - https://github.com/apache/parquet-format/blob/master/Encryption.md
// ---------------------------------------------------------------------------

#include "signet/crypto/aes_gcm.hpp"
#include "signet/crypto/aes_ctr.hpp"
#include "signet/crypto/cipher_interface.hpp"
#include "signet/crypto/key_metadata.hpp"
#include "signet/error.hpp"

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/crypto/pme.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace signet::forge::crypto {

// ===========================================================================
// PME module type constants
// ===========================================================================

/// @cond INTERNAL
namespace detail::pme {

static constexpr uint8_t MODULE_FOOTER             = 0;  ///< PME module type: footer.
static constexpr uint8_t MODULE_COLUMN_META        = 1;  ///< PME module type: column metadata.
static constexpr uint8_t MODULE_DATA_PAGE          = 2;  ///< PME module type: data page.
static constexpr uint8_t MODULE_DICT_PAGE          = 3;  ///< PME module type: dictionary page.
static constexpr uint8_t MODULE_DATA_PAGE_HEADER   = 4;  ///< PME module type: data page header.
static constexpr uint8_t MODULE_COLUMN_META_HEADER = 5;  ///< PME module type: column metadata header.

} // namespace detail::pme
/// @endcond

// ===========================================================================
// FileEncryptor -- Encrypts Parquet file components
// ===========================================================================

/// Encrypts Parquet modules (footer, column metadata, data pages) using the
/// keys and algorithm specified in an EncryptionConfig.
///
/// Usage:
///   EncryptionConfig cfg;
///   cfg.footer_key = ...;       // 32-byte key
///   cfg.algorithm = EncryptionAlgorithm::AES_GCM_CTR_V1;
///   cfg.aad_prefix = "file://my_table/part-00000.parquet";
///
///   FileEncryptor enc(cfg);
///   auto ct_footer = enc.encrypt_footer(footer_bytes, footer_size);
///   auto ct_page   = enc.encrypt_column_page(page_bytes, page_size,
///                                            "price", 0, 0);
class FileEncryptor {
public:
    /// Construct an encryptor from an encryption configuration.
    /// @param config  Configuration specifying keys, algorithm, and AAD prefix.
    explicit FileEncryptor(const EncryptionConfig& config)
        : config_(config) {}

    // -----------------------------------------------------------------------
    // encrypt_footer -- Encrypt the serialized FileMetaData
    //
    // Always uses AES-GCM regardless of the algorithm setting, because the
    // footer requires authenticated encryption (tamper detection).
    //
    // AAD = aad_prefix + '\0' + MODULE_FOOTER + '\0'
    //
    // Output format:
    //   [1 byte: IV_SIZE(12)] [12 bytes: IV] [ciphertext + 16-byte GCM tag]
    // -----------------------------------------------------------------------
    /// Encrypt the serialized FileMetaData (footer) with AES-GCM.
    ///
    /// Always uses authenticated encryption regardless of the algorithm setting.
    /// @param footer_data  Pointer to the serialized footer bytes.
    /// @param size         Footer size in bytes.
    /// @return Encrypted footer: [iv_size(1)] [iv(12)] [ciphertext + GCM tag].
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_footer(
        const uint8_t* footer_data, size_t size) const {

        auto license = commercial::require_feature("PME encrypt_footer");
        if (!license) return license.error();

        auto cipher = CipherFactory::create_footer_cipher(
            config_.algorithm, config_.footer_key);
        if (config_.footer_key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: footer key must be 32 bytes"};
        }

        // Build AAD
        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_FOOTER);

        // Encrypt with ICipher (GCM — footer is always authenticated)
        return cipher->encrypt(footer_data, size, aad);
    }

    // -----------------------------------------------------------------------
    // encrypt_column_page -- Encrypt a column data page
    //
    // Uses AES-GCM when algorithm is AES_GCM_V1, AES-CTR when AES_GCM_CTR_V1.
    //
    // AAD = aad_prefix + '\0' + MODULE_DATA_PAGE + '\0'
    //       + column_name + ':' + row_group_ordinal + ':' + page_ordinal
    //
    // The AAD binds the ciphertext to its location in the file, preventing
    // page reordering or transplant attacks.
    // -----------------------------------------------------------------------
    /// Encrypt a column data page.
    ///
    /// Uses AES-GCM (AES_GCM_V1) or AES-CTR (AES_GCM_CTR_V1) depending on
    /// the algorithm setting. AAD binds the ciphertext to its file location.
    ///
    /// @param page_data          Pointer to the page data bytes.
    /// @param size               Page data size in bytes.
    /// @param column_name        Column path for key resolution and AAD.
    /// @param row_group_ordinal  Row group index (for AAD binding).
    /// @param page_ordinal       Page index within the row group (for AAD binding).
    /// @return Encrypted page, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_column_page(
        const uint8_t* page_data, size_t size,
        const std::string& column_name,
        int32_t row_group_ordinal,
        int32_t page_ordinal) const {

        auto license = commercial::require_feature("PME encrypt_column_page");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            // No key for this column -- pass through unencrypted
            return std::vector<uint8_t>(page_data, page_data + size);
        }

        auto cipher = CipherFactory::create_column_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: column key must be 32 bytes for column '"
                         + column_name + "'"};
        }

        // Build extra AAD context: column_name:rg_ordinal:page_ordinal
        std::string extra = column_name + ":"
                          + std::to_string(row_group_ordinal) + ":"
                          + std::to_string(page_ordinal);

        if (config_.algorithm == EncryptionAlgorithm::AES_GCM_V1) {
            // AES-GCM for column data — include AAD
            std::string aad = build_aad(config_.aad_prefix,
                                        detail::pme::MODULE_DATA_PAGE, extra);
            return cipher->encrypt(page_data, size, aad);
        } else {
            // AES-CTR for column data (AES_GCM_CTR_V1) — no AAD
            return cipher->encrypt(page_data, size);
        }
    }

    // -----------------------------------------------------------------------
    // encrypt_column_metadata -- Encrypt serialized ColumnMetaData
    //
    // Always uses AES-GCM (column metadata requires authentication).
    //
    // AAD = aad_prefix + '\0' + MODULE_COLUMN_META + '\0' + column_name
    // -----------------------------------------------------------------------
    /// Encrypt serialized ColumnMetaData with AES-GCM (always authenticated).
    ///
    /// @param metadata     Pointer to the serialized column metadata bytes.
    /// @param size         Metadata size in bytes.
    /// @param column_name  Column path for key resolution and AAD.
    /// @return Encrypted metadata, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_column_metadata(
        const uint8_t* metadata, size_t size,
        const std::string& column_name) const {

        auto license = commercial::require_feature("PME encrypt_column_metadata");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            // No key -- pass through
            return std::vector<uint8_t>(metadata, metadata + size);
        }

        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: column key must be 32 bytes for column '"
                         + column_name + "'"};
        }

        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_COLUMN_META, column_name);
        return cipher->encrypt(metadata, size, aad);
    }

    /// Get FileEncryptionProperties for embedding in FileMetaData.
    /// @return Properties struct with algorithm, footer-encrypted flag, and AAD prefix.
    [[nodiscard]] FileEncryptionProperties file_properties() const {
        return FileEncryptionProperties{
            config_.algorithm,
            config_.encrypt_footer,
            config_.aad_prefix
        };
    }

    /// Get key metadata for a column (stored in ColumnChunk.column_crypto_metadata).
    /// @param column_name  Column path to look up.
    /// @return Metadata with key_mode, key_material (INTERNAL), or key_id (EXTERNAL).
    [[nodiscard]] EncryptionKeyMetadata column_key_metadata(
        const std::string& column_name) const {

        EncryptionKeyMetadata meta;
        meta.key_mode = config_.key_mode;

        // Look for a specific column key
        for (const auto& ck : config_.column_keys) {
            if (ck.column_name == column_name) {
                if (config_.key_mode == KeyMode::INTERNAL) {
                    meta.key_material = ck.key;
                }
                meta.key_id = ck.key_id;
                return meta;
            }
        }

        // Fall back to default column key
        if (config_.key_mode == KeyMode::INTERNAL) {
            meta.key_material = config_.default_column_key;
        }
        meta.key_id = config_.default_column_key_id;
        return meta;
    }

    /// Check if a column has an encryption key (specific or default).
    /// @param column_name  Column path to check.
    /// @return True if the column will be encrypted.
    [[nodiscard]] bool is_column_encrypted(const std::string& column_name) const {
        return !get_column_key(column_name).empty();
    }

    /// Access the underlying EncryptionConfig.
    /// @return Const reference to the configuration.
    [[nodiscard]] const EncryptionConfig& config() const { return config_; }

private:
    EncryptionConfig config_;  ///< Encryption configuration (keys, algorithm, AAD).

    /// Resolve the AES key for a given column.
    ///
    /// Priority: (1) column-specific key, (2) default column key, (3) empty (unencrypted).
    [[nodiscard]] const std::vector<uint8_t>& get_column_key(
        const std::string& column_name) const {

        for (const auto& ck : config_.column_keys) {
            if (ck.column_name == column_name) {
                return ck.key;
            }
        }
        return config_.default_column_key;
    }

    // -----------------------------------------------------------------------
    // generate_iv -- Generate a random initialization vector
    //
    // Delegates to detail::cipher::fill_random_bytes() for platform-aware
    // CSPRNG (arc4random_buf on macOS/BSD, getrandom on Linux).
    // GCM: 12 bytes (96 bits). CTR: 16 bytes (128 bits).
    // -----------------------------------------------------------------------
    [[nodiscard]] static std::vector<uint8_t> generate_iv(size_t iv_size) {
        return detail::cipher::generate_iv(iv_size);
    }

    // -----------------------------------------------------------------------
    // build_aad -- Construct Parquet PME AAD string
    //
    // Format: prefix + '\0' + module_type_byte + '\0' + extra
    //
    // The null bytes are separators ensuring unambiguous parsing. The module
    // type byte identifies what kind of Parquet component is being
    // authenticated. The extra field carries component-specific context
    // (column name, row group ordinal, etc.).
    // -----------------------------------------------------------------------
    [[nodiscard]] static std::string build_aad(const std::string& prefix,
                                               uint8_t module_type,
                                               const std::string& extra = "") {
        std::string aad;
        aad.reserve(prefix.size() + 3 + extra.size());
        aad.append(prefix);
        aad.push_back('\0');
        aad.push_back(static_cast<char>(module_type));
        aad.push_back('\0');
        aad.append(extra);
        return aad;
    }

    // -----------------------------------------------------------------------
    // prepend_iv -- Wrap ciphertext with IV header
    //
    // Output: [1 byte: iv.size()] [iv bytes] [ciphertext bytes]
    // -----------------------------------------------------------------------
    [[nodiscard]] static std::vector<uint8_t> prepend_iv(
        const std::vector<uint8_t>& iv,
        const std::vector<uint8_t>& ciphertext) {

        std::vector<uint8_t> out;
        out.reserve(1 + iv.size() + ciphertext.size());
        out.push_back(static_cast<uint8_t>(iv.size()));
        out.insert(out.end(), iv.begin(), iv.end());
        out.insert(out.end(), ciphertext.begin(), ciphertext.end());
        return out;
    }

    // -----------------------------------------------------------------------
    // encrypt_gcm -- AES-GCM encryption with AAD construction (via ICipher)
    // -----------------------------------------------------------------------
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_gcm(
        const std::vector<uint8_t>& key,
        uint8_t module_type,
        const std::string& extra,
        const uint8_t* data, size_t size) const {

        std::string aad = build_aad(config_.aad_prefix, module_type, extra);
        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        return cipher->encrypt(data, size, aad);
    }

    // -----------------------------------------------------------------------
    // encrypt_ctr -- AES-CTR encryption (no authentication, via ICipher)
    //
    // For AES_GCM_CTR_V1 column data. CTR mode has no AAD -- integrity is
    // verified by Parquet page checksums.
    // -----------------------------------------------------------------------
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_ctr(
        const std::vector<uint8_t>& key,
        const uint8_t* data, size_t size) const {

        auto cipher = CipherFactory::create_column_cipher(
            EncryptionAlgorithm::AES_GCM_CTR_V1, key);
        return cipher->encrypt(data, size);
    }
};

// ===========================================================================
// FileDecryptor -- Decrypts Parquet file components
// ===========================================================================

/// Decrypts Parquet modules using the keys from an EncryptionConfig.
///
/// The config must contain the same keys that were used for encryption.
/// For EXTERNAL mode, the caller is responsible for resolving KMS key IDs
/// to actual key bytes before constructing the config.
///
/// Usage:
///   FileDecryptor dec(cfg);
///   auto footer = dec.decrypt_footer(encrypted_footer, footer_size);
///   auto page   = dec.decrypt_column_page(encrypted_page, page_size,
///                                         "price", 0, 0);
class FileDecryptor {
public:
    /// Construct a decryptor from an encryption configuration.
    /// @param config  Configuration with the same keys used during encryption.
    explicit FileDecryptor(const EncryptionConfig& config)
        : config_(config) {}

    // -----------------------------------------------------------------------
    // decrypt_footer -- Decrypt the encrypted FileMetaData
    //
    // Reads the IV from the header, then decrypts with AES-GCM.
    //
    // Input format:
    //   [1 byte: IV size] [IV bytes] [ciphertext + 16-byte GCM tag]
    // -----------------------------------------------------------------------
    /// Decrypt the encrypted FileMetaData (footer).
    ///
    /// Reads the IV from the header, then decrypts with AES-GCM.
    /// @param encrypted_footer  Pointer to encrypted footer bytes.
    /// @param size              Total size including IV header and GCM tag.
    /// @return Decrypted footer bytes, or ENCRYPTION_ERROR on tag mismatch.
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_footer(
        const uint8_t* encrypted_footer, size_t size) const {

        auto license = commercial::require_feature("PME decrypt_footer");
        if (!license) return license.error();

        auto cipher = CipherFactory::create_footer_cipher(
            config_.algorithm, config_.footer_key);
        if (config_.footer_key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: footer key must be 32 bytes"};
        }

        // Build AAD (must match what was used during encryption)
        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_FOOTER);

        // Decrypt with ICipher (GCM — footer is always authenticated)
        return cipher->decrypt(encrypted_footer, size, aad);
    }

    // -----------------------------------------------------------------------
    // decrypt_column_page -- Decrypt a column data page
    //
    // Uses AES-GCM or AES-CTR depending on algorithm setting.
    // -----------------------------------------------------------------------
    /// Decrypt a column data page (AES-GCM or AES-CTR depending on algorithm).
    ///
    /// @param encrypted_page     Pointer to encrypted page bytes.
    /// @param size               Total encrypted size.
    /// @param column_name        Column path for key resolution and AAD.
    /// @param row_group_ordinal  Row group index (for AAD reconstruction).
    /// @param page_ordinal       Page index (for AAD reconstruction).
    /// @return Decrypted page bytes, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_column_page(
        const uint8_t* encrypted_page, size_t size,
        const std::string& column_name,
        int32_t row_group_ordinal,
        int32_t page_ordinal) const {

        auto license = commercial::require_feature("PME decrypt_column_page");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            // Not encrypted -- return as-is
            return std::vector<uint8_t>(encrypted_page, encrypted_page + size);
        }
        auto cipher = CipherFactory::create_column_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: column key must be 32 bytes for column '"
                         + column_name + "'"};
        }

        // Build extra AAD context (must match encryption)
        std::string extra = column_name + ":"
                          + std::to_string(row_group_ordinal) + ":"
                          + std::to_string(page_ordinal);

        if (config_.algorithm == EncryptionAlgorithm::AES_GCM_V1) {
            std::string aad = build_aad(config_.aad_prefix,
                                        detail::pme::MODULE_DATA_PAGE, extra);
            return cipher->decrypt(encrypted_page, size, aad);
        } else {
            return cipher->decrypt(encrypted_page, size);
        }
    }

    // -----------------------------------------------------------------------
    // decrypt_column_metadata -- Decrypt serialized ColumnMetaData
    //
    // Always uses AES-GCM (column metadata is always authenticated).
    // -----------------------------------------------------------------------
    /// Decrypt serialized ColumnMetaData (always AES-GCM authenticated).
    ///
    /// @param encrypted_metadata  Pointer to encrypted metadata bytes.
    /// @param size                Total encrypted size.
    /// @param column_name         Column path for key resolution and AAD.
    /// @return Decrypted metadata bytes, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_column_metadata(
        const uint8_t* encrypted_metadata, size_t size,
        const std::string& column_name) const {

        auto license = commercial::require_feature("PME decrypt_column_metadata");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            return std::vector<uint8_t>(encrypted_metadata,
                                        encrypted_metadata + size);
        }

        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: column key must be 32 bytes for column '"
                         + column_name + "'"};
        }

        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_COLUMN_META, column_name);
        return cipher->decrypt(encrypted_metadata, size, aad);
    }

    /// Access the underlying EncryptionConfig.
    /// @return Const reference to the configuration.
    [[nodiscard]] const EncryptionConfig& config() const { return config_; }

private:
    EncryptionConfig config_;  ///< Decryption configuration (keys, algorithm, AAD).

    /// Resolve the AES key for a given column (same logic as FileEncryptor).
    [[nodiscard]] const std::vector<uint8_t>& get_column_key(
        const std::string& column_name) const {

        for (const auto& ck : config_.column_keys) {
            if (ck.column_name == column_name) {
                return ck.key;
            }
        }
        return config_.default_column_key;
    }

    // -----------------------------------------------------------------------
    // IvParsed -- Result of parsing the IV header from encrypted data
    // -----------------------------------------------------------------------
    struct IvParsed {
        const uint8_t* iv;          // Pointer into the input buffer
        const uint8_t* ciphertext;  // Pointer past the IV
        size_t         ct_size;     // Ciphertext length (including GCM tag)
    };

    // -----------------------------------------------------------------------
    // parse_iv_header -- Extract IV from the encrypted module header
    //
    // Input format: [1 byte: iv_size] [iv_size bytes: IV] [ciphertext...]
    // -----------------------------------------------------------------------
    [[nodiscard]] static expected<IvParsed> parse_iv_header(
        const uint8_t* data, size_t size) {

        if (size < 1) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: encrypted data too short (no IV size byte)"};
        }

        uint8_t iv_size = data[0];
        if (iv_size == 0 || iv_size > 16) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: invalid IV size " + std::to_string(iv_size)};
        }

        size_t header_len = 1 + static_cast<size_t>(iv_size);
        if (size < header_len) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: encrypted data too short for IV"};
        }

        return IvParsed{
            data + 1,             // iv
            data + header_len,    // ciphertext
            size - header_len     // ct_size
        };
    }

    // -----------------------------------------------------------------------
    // build_aad -- Same construction as FileEncryptor
    // -----------------------------------------------------------------------
    [[nodiscard]] static std::string build_aad(const std::string& prefix,
                                               uint8_t module_type,
                                               const std::string& extra = "") {
        std::string aad;
        aad.reserve(prefix.size() + 3 + extra.size());
        aad.append(prefix);
        aad.push_back('\0');
        aad.push_back(static_cast<char>(module_type));
        aad.push_back('\0');
        aad.append(extra);
        return aad;
    }

    // -----------------------------------------------------------------------
    // decrypt_gcm -- AES-GCM decryption with AAD (via ICipher)
    // -----------------------------------------------------------------------
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_gcm(
        const std::vector<uint8_t>& key,
        uint8_t module_type,
        const std::string& extra,
        const uint8_t* data, size_t size) const {

        std::string aad = build_aad(config_.aad_prefix, module_type, extra);
        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        return cipher->decrypt(data, size, aad);
    }

    // -----------------------------------------------------------------------
    // decrypt_ctr -- AES-CTR decryption (via ICipher)
    // -----------------------------------------------------------------------
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_ctr(
        const std::vector<uint8_t>& key,
        const uint8_t* data, size_t size) const {

        auto cipher = CipherFactory::create_column_cipher(
            EncryptionAlgorithm::AES_GCM_CTR_V1, key);
        return cipher->decrypt(data, size);
    }
};

} // namespace signet::forge::crypto
