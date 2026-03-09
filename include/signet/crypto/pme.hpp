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
#include "signet/crypto/hkdf.hpp"
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
#include <unordered_map>
#include <vector>

namespace signet::forge::crypto {

// ===========================================================================
// PME module type constants
// ===========================================================================

/// Required AES-256 key size for all PME operations (NIST SP 800-131A).
constexpr size_t PME_REQUIRED_KEY_SIZE = 32;

/// AES-128 key size — detected for interop diagnostics only (Gap P-7).
constexpr size_t PME_AES128_KEY_SIZE   = 16;

/// @cond INTERNAL
namespace detail::pme {

static constexpr uint8_t MODULE_FOOTER             = 0;  ///< PME module type: footer.
static constexpr uint8_t MODULE_COLUMN_META        = 1;  ///< PME module type: column metadata.
static constexpr uint8_t MODULE_DATA_PAGE          = 2;  ///< PME module type: data page.
static constexpr uint8_t MODULE_DICT_PAGE          = 3;  ///< PME module type: dictionary page.
static constexpr uint8_t MODULE_DATA_PAGE_HEADER   = 4;  ///< PME module type: data page header.
static constexpr uint8_t MODULE_COLUMN_META_HEADER = 5;  ///< PME module type: column metadata header.

/// Signet v1 AAD format: prefix + '\0' + module_type + '\0' + extra
inline std::string build_aad_legacy(const std::string& prefix,
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

/// Parquet PME spec binary AAD (Gap P-4):
/// aad_file_unique || module_type (1B) || rg_ordinal (2B LE) ||
/// col_ordinal (2B LE) || page_ordinal (2B LE)
inline std::string build_aad_spec(const std::string& prefix,
                                   uint8_t module_type,
                                   const std::string& extra = "") {
    std::string aad;
    aad.append(prefix);
    aad.push_back(static_cast<char>(module_type));

    // Parse extra string: "column_name:col_ordinal:rg_ordinal:page_ordinal" (4 parts)
    // Backward compat: "column_name:rg_ordinal:page_ordinal" (3 parts, col_ord=0)
    uint16_t rg_ord = 0, col_ord = 0, pg_ord = 0;
    if (!extra.empty()) {
        auto colon1 = extra.find(':');
        if (colon1 != std::string::npos) {
            auto colon2 = extra.find(':', colon1 + 1);
            if (colon2 != std::string::npos) {
                auto colon3 = extra.find(':', colon2 + 1);
                if (colon3 != std::string::npos) {
                    // 4-part format: name:col_ord:rg_ord:pg_ord
                    try { col_ord = static_cast<uint16_t>(std::stoi(extra.substr(colon1 + 1, colon2 - colon1 - 1))); } catch (...) {}
                    try { rg_ord  = static_cast<uint16_t>(std::stoi(extra.substr(colon2 + 1, colon3 - colon2 - 1))); } catch (...) {}
                    try { pg_ord  = static_cast<uint16_t>(std::stoi(extra.substr(colon3 + 1))); } catch (...) {}
                } else {
                    // 3-part format (backward compat): name:rg_ord:pg_ord, col_ord=0
                    try { rg_ord  = static_cast<uint16_t>(std::stoi(extra.substr(colon1 + 1, colon2 - colon1 - 1))); } catch (...) {}
                    try { pg_ord  = static_cast<uint16_t>(std::stoi(extra.substr(colon2 + 1))); } catch (...) {}
                }
            }
        }
    }

    auto append_le16 = [&](uint16_t v) {
        aad.push_back(static_cast<char>(v & 0xFF));
        aad.push_back(static_cast<char>((v >> 8) & 0xFF));
    };

    if (module_type >= MODULE_COLUMN_META) {
        append_le16(rg_ord);
        append_le16(col_ord);
        append_le16(pg_ord);
    }

    return aad;
}

/// Dispatch to legacy or spec AAD based on config format flag.
inline std::string build_aad(const EncryptionConfig& config,
                              const std::string& prefix,
                              uint8_t module_type,
                              const std::string& extra = "") {
    if (config.aad_format == EncryptionConfig::AadFormat::SPEC_BINARY) {
        return build_aad_spec(prefix, module_type, extra);
    }
    return build_aad_legacy(prefix, module_type, extra);
}

/// Build a descriptive error message for invalid PME key sizes (Gap P-7).
///
/// When a PME-encrypted Parquet file uses AES-128 (16-byte keys), return a
/// specific interop message instead of a generic error. This prevents silent
/// corruption and guides users toward the required AES-256 key size.
inline std::string pme_key_size_error(size_t actual_size,
                                       const std::string& context) {
    if (actual_size == PME_AES128_KEY_SIZE) {
        return "PME: AES-128 (16-byte) keys detected for " + context
             + ". Signet Forge requires AES-256 (32-byte) keys per "
               "NIST SP 800-131A. See Gap P-7 for AES-128 interop roadmap.";
    }
    return "PME: invalid key size (" + std::to_string(actual_size)
         + " bytes) for " + context
         + ". Expected " + std::to_string(PME_REQUIRED_KEY_SIZE)
         + " bytes (AES-256).";
}

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
        : config_(config) {
        // Gap P-8: Build O(1) column key lookup cache from O(n) vector
        for (const auto& ck : config_.column_keys) {
            key_cache_[ck.column_name] = &ck.key;
        }
    }

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
                         detail::pme::pme_key_size_error(
                             config_.footer_key.size(), "footer")};
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
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
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
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_COLUMN_META, column_name);
        return cipher->encrypt(metadata, size, aad);
    }

    // -----------------------------------------------------------------------
    // encrypt_dict_page -- Encrypt a dictionary page (Gap P-1)
    //
    // PME spec requires dictionary pages to be encrypted with the column key
    // using the same algorithm as data pages. Module type = MODULE_DICT_PAGE (3).
    // Without this, dictionary-encoded columns leak all distinct values.
    //
    // AAD = aad_prefix + '\0' + MODULE_DICT_PAGE + '\0'
    //       + column_name + ':' + row_group_ordinal + ':0'
    //
    // Reference: Apache Parquet Encryption specification (PARQUET-1178)
    //   https://github.com/apache/parquet-format/blob/master/Encryption.md
    // -----------------------------------------------------------------------
    /// Encrypt a dictionary page with the column's encryption key.
    ///
    /// Dictionary pages contain all distinct values for dictionary-encoded columns.
    /// If left unencrypted, they leak the value domain even when data pages are encrypted.
    ///
    /// @param page_data          Pointer to the dictionary page data bytes.
    /// @param size               Page data size in bytes.
    /// @param column_name        Column path for key resolution and AAD.
    /// @param row_group_ordinal  Row group index (for AAD binding).
    /// @return Encrypted dictionary page, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_dict_page(
        const uint8_t* page_data, size_t size,
        const std::string& column_name,
        int32_t row_group_ordinal) const {

        auto license = commercial::require_feature("PME encrypt_dict_page");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            return std::vector<uint8_t>(page_data, page_data + size);
        }

        auto cipher = CipherFactory::create_column_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string extra = column_name + ":"
                          + std::to_string(row_group_ordinal) + ":0";

        if (config_.algorithm == EncryptionAlgorithm::AES_GCM_V1) {
            std::string aad = build_aad(config_.aad_prefix,
                                        detail::pme::MODULE_DICT_PAGE, extra);
            return cipher->encrypt(page_data, size, aad);
        } else {
            return cipher->encrypt(page_data, size);
        }
    }

    // -----------------------------------------------------------------------
    // encrypt_data_page_header -- Encrypt a data page header (Gap P-2)
    //
    // In AES_GCM_CTR_V1 mode, page headers contain min/max statistics that
    // leak plaintext information about encrypted columns. The PME spec
    // requires page headers to be GCM-encrypted even when page data uses CTR.
    // Module type = MODULE_DATA_PAGE_HEADER (4).
    //
    // Reference: Apache Parquet Encryption specification (PARQUET-1178)
    //   https://github.com/apache/parquet-format/blob/master/Encryption.md
    // -----------------------------------------------------------------------
    /// Encrypt a data page header (always AES-GCM authenticated).
    ///
    /// Page headers contain min/max statistics. In AES_GCM_CTR_V1 mode,
    /// page data uses CTR but headers must use GCM to prevent statistics leakage.
    ///
    /// @param header_data        Pointer to the serialized page header bytes.
    /// @param size               Header size in bytes.
    /// @param column_name        Column path for key resolution and AAD.
    /// @param row_group_ordinal  Row group index (for AAD binding).
    /// @param page_ordinal       Page index within the row group (for AAD binding).
    /// @return Encrypted page header, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_data_page_header(
        const uint8_t* header_data, size_t size,
        const std::string& column_name,
        int32_t row_group_ordinal,
        int32_t page_ordinal) const {

        auto license = commercial::require_feature("PME encrypt_data_page_header");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            return std::vector<uint8_t>(header_data, header_data + size);
        }

        // Page headers always use GCM (authenticated) regardless of algorithm setting
        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string extra = column_name + ":"
                          + std::to_string(row_group_ordinal) + ":"
                          + std::to_string(page_ordinal);
        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_DATA_PAGE_HEADER, extra);
        return cipher->encrypt(header_data, size, aad);
    }

    // -----------------------------------------------------------------------
    // encrypt_column_meta_header -- Encrypt a column metadata header (Gap P-2)
    //
    // Module type = MODULE_COLUMN_META_HEADER (5). Always GCM-authenticated.
    // -----------------------------------------------------------------------
    /// Encrypt a column metadata header (always AES-GCM authenticated).
    ///
    /// @param header_data   Pointer to the serialized column metadata header.
    /// @param size          Header size in bytes.
    /// @param column_name   Column path for key resolution and AAD.
    /// @return Encrypted header, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> encrypt_column_meta_header(
        const uint8_t* header_data, size_t size,
        const std::string& column_name) const {

        auto license = commercial::require_feature("PME encrypt_column_meta_header");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            return std::vector<uint8_t>(header_data, header_data + size);
        }

        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_COLUMN_META_HEADER,
                                    column_name);
        return cipher->encrypt(header_data, size, aad);
    }

    // -----------------------------------------------------------------------
    // sign_footer -- Sign plaintext footer with HMAC-SHA256 (Gap P-3)
    //
    // In "signed plaintext footer" mode, the footer is NOT encrypted but
    // is signed with HMAC-SHA256 for tamper detection. This allows metadata
    // inspection tools to read column names, statistics, and schema without
    // decryption keys, while still detecting modifications.
    //
    // The signing key is derived from the footer key using HKDF:
    //   signing_key = HKDF-Expand(HKDF-Extract(aad_prefix, footer_key),
    //                             "signet-pme-footer-sign-v1", 32)
    //
    // Output format: [footer_data] [32-byte HMAC-SHA256 signature]
    //
    // Reference: Apache Parquet Encryption (PARQUET-1178) §4.2
    // -----------------------------------------------------------------------
    /// Sign the plaintext footer with HMAC-SHA256 (signed plaintext footer mode).
    ///
    /// The footer remains readable but any modification will invalidate the signature.
    /// @param footer_data  Pointer to the serialized footer bytes.
    /// @param size         Footer size in bytes.
    /// @return Footer data with 32-byte HMAC signature appended.
    [[nodiscard]] expected<std::vector<uint8_t>> sign_footer(
        const uint8_t* footer_data, size_t size) const {

        auto license = commercial::require_feature("PME sign_footer");
        if (!license) return license.error();

        if (config_.footer_key.empty() || config_.footer_key.size() != PME_REQUIRED_KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             config_.footer_key.size(), "footer signing")};
        }

        // Derive signing key via HKDF
        auto signing_key = derive_footer_signing_key();

        // Compute HMAC-SHA256(signing_key, aad || footer_data)
        std::string aad = build_aad(config_.aad_prefix, detail::pme::MODULE_FOOTER);
        std::vector<uint8_t> msg;
        msg.reserve(aad.size() + size);
        msg.insert(msg.end(), aad.begin(), aad.end());
        msg.insert(msg.end(), footer_data, footer_data + size);

        auto hmac = detail::hkdf::hmac_sha256(
            signing_key.data(), signing_key.size(),
            msg.data(), msg.size());

        // Output: footer_data || hmac
        std::vector<uint8_t> out;
        out.reserve(size + 32);
        out.insert(out.end(), footer_data, footer_data + size);
        out.insert(out.end(), hmac.begin(), hmac.end());
        return out;
    }

    // -----------------------------------------------------------------------
    // wrap_keys -- Wrap all DEKs under their KEKs via KMS (Gap P-5)
    //
    // For EXTERNAL key mode with a KMS client configured: wraps the footer
    // key and all column keys under their respective KEK identifiers.
    //
    // Returns a map of key_id → wrapped_dek for storage in file metadata.
    //
    // Reference: Parquet PME spec (PARQUET-1178) §3, NIST SP 800-38F
    // -----------------------------------------------------------------------
    /// Wrap all DEKs under their KEKs using the configured KMS client.
    ///
    /// @return Map of key_id → wrapped DEK bytes, or error if KMS unavailable.
    [[nodiscard]] expected<std::vector<std::pair<std::string, std::vector<uint8_t>>>>
    wrap_keys() const {

        auto license = commercial::require_feature("PME wrap_keys");
        if (!license) return license.error();

        if (!config_.kms_client) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: KMS client not configured for key wrapping"};
        }

        std::vector<std::pair<std::string, std::vector<uint8_t>>> result;

        // Wrap footer key
        if (!config_.footer_key.empty() && !config_.footer_key_id.empty()) {
            auto wrapped = config_.kms_client->wrap_key(
                config_.footer_key, config_.footer_key_id);
            if (!wrapped) return wrapped.error();
            result.emplace_back(config_.footer_key_id, std::move(wrapped.value()));
        }

        // Wrap per-column keys
        for (const auto& ck : config_.column_keys) {
            if (!ck.key.empty() && !ck.key_id.empty()) {
                auto wrapped = config_.kms_client->wrap_key(ck.key, ck.key_id);
                if (!wrapped) return wrapped.error();
                result.emplace_back(ck.key_id, std::move(wrapped.value()));
            }
        }

        // Wrap default column key
        if (!config_.default_column_key.empty() &&
            !config_.default_column_key_id.empty()) {
            auto wrapped = config_.kms_client->wrap_key(
                config_.default_column_key, config_.default_column_key_id);
            if (!wrapped) return wrapped.error();
            result.emplace_back(config_.default_column_key_id,
                                std::move(wrapped.value()));
        }

        return result;
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
    /// Gap P-8: O(1) column key lookup cache (column_name → key pointer).
    std::unordered_map<std::string, const std::vector<uint8_t>*> key_cache_;

    /// Resolve the AES key for a given column.
    ///
    /// Priority: (1) column-specific key (O(1) cache), (2) default column key, (3) empty.
    [[nodiscard]] const std::vector<uint8_t>& get_column_key(
        const std::string& column_name) const {

        auto it = key_cache_.find(column_name);
        if (it != key_cache_.end()) {
            return *it->second;
        }
        return config_.default_column_key;
    }

    // -----------------------------------------------------------------------
    // derive_footer_signing_key -- HKDF-derived key for signed plaintext footer
    // -----------------------------------------------------------------------
    [[nodiscard]] std::array<uint8_t, 32> derive_footer_signing_key() const {
        static constexpr uint8_t INFO[] = "signet-pme-footer-sign-v1";
        auto prk = hkdf_extract(
            reinterpret_cast<const uint8_t*>(config_.aad_prefix.data()),
            config_.aad_prefix.size(),
            config_.footer_key.data(),
            config_.footer_key.size());
        std::array<uint8_t, 32> key{};
        (void)hkdf_expand(prk, INFO, sizeof(INFO) - 1, key.data(), key.size());
        return key;
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
    // build_aad -- Construct Parquet PME AAD (dispatches on config format)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::string build_aad(const std::string& prefix,
                                        uint8_t module_type,
                                        const std::string& extra = "") const {
        return detail::pme::build_aad(config_, prefix, module_type, extra);
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

        // Validate module_type range [0..5] per Parquet PME spec (CWE-20)
        if (module_type > 5) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "PME module_type out of range [0..5] (CWE-20)"};
        }
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
        : config_(config) {
        // Gap P-8: Build O(1) column key lookup cache from O(n) vector
        for (const auto& ck : config_.column_keys) {
            key_cache_[ck.column_name] = &ck.key;
        }
    }

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
                         detail::pme::pme_key_size_error(
                             config_.footer_key.size(), "footer")};
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
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
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
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_COLUMN_META, column_name);
        return cipher->decrypt(encrypted_metadata, size, aad);
    }

    // -----------------------------------------------------------------------
    // decrypt_dict_page -- Decrypt a dictionary page (Gap P-1)
    //
    // Counterpart to FileEncryptor::encrypt_dict_page().
    // Uses MODULE_DICT_PAGE (3) for AAD construction.
    // -----------------------------------------------------------------------
    /// Decrypt a dictionary page.
    ///
    /// @param encrypted_page     Pointer to encrypted dictionary page bytes.
    /// @param size               Total encrypted size.
    /// @param column_name        Column path for key resolution and AAD.
    /// @param row_group_ordinal  Row group index (for AAD reconstruction).
    /// @return Decrypted dictionary page, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_dict_page(
        const uint8_t* encrypted_page, size_t size,
        const std::string& column_name,
        int32_t row_group_ordinal) const {

        auto license = commercial::require_feature("PME decrypt_dict_page");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            return std::vector<uint8_t>(encrypted_page, encrypted_page + size);
        }

        auto cipher = CipherFactory::create_column_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string extra = column_name + ":"
                          + std::to_string(row_group_ordinal) + ":0";

        if (config_.algorithm == EncryptionAlgorithm::AES_GCM_V1) {
            std::string aad = build_aad(config_.aad_prefix,
                                        detail::pme::MODULE_DICT_PAGE, extra);
            return cipher->decrypt(encrypted_page, size, aad);
        } else {
            return cipher->decrypt(encrypted_page, size);
        }
    }

    // -----------------------------------------------------------------------
    // decrypt_data_page_header -- Decrypt a data page header (Gap P-2)
    //
    // Counterpart to FileEncryptor::encrypt_data_page_header().
    // Uses MODULE_DATA_PAGE_HEADER (4). Always GCM.
    // -----------------------------------------------------------------------
    /// Decrypt a data page header (always AES-GCM authenticated).
    ///
    /// @param encrypted_header   Pointer to encrypted page header bytes.
    /// @param size               Total encrypted size.
    /// @param column_name        Column path for key resolution and AAD.
    /// @param row_group_ordinal  Row group index (for AAD reconstruction).
    /// @param page_ordinal       Page index (for AAD reconstruction).
    /// @return Decrypted page header, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_data_page_header(
        const uint8_t* encrypted_header, size_t size,
        const std::string& column_name,
        int32_t row_group_ordinal,
        int32_t page_ordinal) const {

        auto license = commercial::require_feature("PME decrypt_data_page_header");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            return std::vector<uint8_t>(encrypted_header, encrypted_header + size);
        }

        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string extra = column_name + ":"
                          + std::to_string(row_group_ordinal) + ":"
                          + std::to_string(page_ordinal);
        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_DATA_PAGE_HEADER, extra);
        return cipher->decrypt(encrypted_header, size, aad);
    }

    // -----------------------------------------------------------------------
    // decrypt_column_meta_header -- Decrypt a column metadata header (Gap P-2)
    //
    // Counterpart to FileEncryptor::encrypt_column_meta_header().
    // Uses MODULE_COLUMN_META_HEADER (5). Always GCM.
    // -----------------------------------------------------------------------
    /// Decrypt a column metadata header (always AES-GCM authenticated).
    ///
    /// @param encrypted_header  Pointer to encrypted column metadata header.
    /// @param size              Total encrypted size.
    /// @param column_name       Column path for key resolution and AAD.
    /// @return Decrypted header, or passthrough if column has no key.
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_column_meta_header(
        const uint8_t* encrypted_header, size_t size,
        const std::string& column_name) const {

        auto license = commercial::require_feature("PME decrypt_column_meta_header");
        if (!license) return license.error();

        const auto& key = get_column_key(column_name);
        if (key.empty()) {
            return std::vector<uint8_t>(encrypted_header,
                                        encrypted_header + size);
        }

        auto cipher = CipherFactory::create_metadata_cipher(config_.algorithm, key);
        if (key.size() != cipher->key_size()) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             key.size(), "column '" + column_name + "'")};
        }

        std::string aad = build_aad(config_.aad_prefix,
                                    detail::pme::MODULE_COLUMN_META_HEADER,
                                    column_name);
        return cipher->decrypt(encrypted_header, size, aad);
    }

    // -----------------------------------------------------------------------
    // unwrap_keys -- Unwrap DEKs from wrapped blobs via KMS (Gap P-5)
    //
    // For EXTERNAL key mode: takes a list of (key_id, wrapped_dek) pairs
    // read from file metadata and calls the KMS client to unwrap each DEK.
    // The unwrapped keys are populated into the config for subsequent
    // decrypt operations.
    //
    // Reference: Parquet PME spec (PARQUET-1178) §3, NIST SP 800-38F
    // -----------------------------------------------------------------------
    /// Unwrap DEKs from wrapped blobs using the configured KMS client.
    ///
    /// Call this before decrypt_footer / decrypt_column_page when using
    /// EXTERNAL key mode. Populates the internal config with unwrapped keys.
    ///
    /// @param wrapped_keys  List of (key_id, wrapped_dek) pairs from file metadata.
    /// @return void on success, or error if KMS unwrap fails.
    [[nodiscard]] expected<void> unwrap_keys(
        const std::vector<std::pair<std::string, std::vector<uint8_t>>>& wrapped_keys) {

        auto license = commercial::require_feature("PME unwrap_keys");
        if (!license) return license.error();

        if (!config_.kms_client) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: KMS client not configured for key unwrapping"};
        }

        for (const auto& [key_id, wrapped_dek] : wrapped_keys) {
            auto unwrapped = config_.kms_client->unwrap_key(wrapped_dek, key_id);
            if (!unwrapped) return unwrapped.error();

            // Match key_id to the appropriate config slot
            if (key_id == config_.footer_key_id) {
                config_.footer_key = std::move(unwrapped.value());
            } else if (key_id == config_.default_column_key_id) {
                config_.default_column_key = std::move(unwrapped.value());
            } else {
                // Check per-column keys
                bool found = false;
                for (auto& ck : config_.column_keys) {
                    if (ck.key_id == key_id) {
                        ck.key = std::move(unwrapped.value());
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return Error{ErrorCode::ENCRYPTION_ERROR,
                                 "PME: no config slot for KMS key_id '" + key_id + "'"};
                }
            }
        }

        // Rebuild O(1) cache after key population
        key_cache_.clear();
        for (const auto& ck : config_.column_keys) {
            key_cache_[ck.column_name] = &ck.key;
        }

        return {};
    }

    // -----------------------------------------------------------------------
    // verify_footer_signature -- Verify signed plaintext footer (Gap P-3)
    //
    // Counterpart to FileEncryptor::sign_footer(). Splits the signed footer
    // into [footer_data] and [32-byte HMAC], recomputes the HMAC, and
    // performs constant-time comparison to detect tampering.
    //
    // Reference: Apache Parquet Encryption (PARQUET-1178) §4.2
    // -----------------------------------------------------------------------
    /// Verify a signed plaintext footer and return the original footer data.
    ///
    /// @param signed_footer  Pointer to footer bytes with appended 32-byte HMAC.
    /// @param size           Total size including the 32-byte signature.
    /// @return Original footer data (without signature), or ENCRYPTION_ERROR on mismatch.
    [[nodiscard]] expected<std::vector<uint8_t>> verify_footer_signature(
        const uint8_t* signed_footer, size_t size) const {

        auto license = commercial::require_feature("PME verify_footer_signature");
        if (!license) return license.error();

        if (size < 32) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: signed footer too short (need at least 32 bytes for HMAC)"};
        }

        if (config_.footer_key.empty() || config_.footer_key.size() != PME_REQUIRED_KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         detail::pme::pme_key_size_error(
                             config_.footer_key.size(), "footer signature verification")};
        }

        size_t footer_size = size - 32;
        const uint8_t* footer_data = signed_footer;
        const uint8_t* expected_hmac = signed_footer + footer_size;

        // Derive the same signing key as FileEncryptor
        auto signing_key = derive_footer_signing_key();

        // Recompute HMAC-SHA256(signing_key, aad || footer_data)
        std::string aad = build_aad(config_.aad_prefix, detail::pme::MODULE_FOOTER);
        std::vector<uint8_t> msg;
        msg.reserve(aad.size() + footer_size);
        msg.insert(msg.end(), aad.begin(), aad.end());
        msg.insert(msg.end(), footer_data, footer_data + footer_size);

        auto computed_hmac = detail::hkdf::hmac_sha256(
            signing_key.data(), signing_key.size(),
            msg.data(), msg.size());

        // Constant-time comparison to prevent timing side-channel
        uint8_t diff = 0;
        for (size_t i = 0; i < 32; ++i) {
            diff |= computed_hmac[i] ^ expected_hmac[i];
        }

        if (diff != 0) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "PME: footer signature verification failed — data may be tampered"};
        }

        return std::vector<uint8_t>(footer_data, footer_data + footer_size);
    }

    /// Access the underlying EncryptionConfig.
    /// @return Const reference to the configuration.
    [[nodiscard]] const EncryptionConfig& config() const { return config_; }

private:
    EncryptionConfig config_;  ///< Decryption configuration (keys, algorithm, AAD).
    /// Gap P-8: O(1) column key lookup cache (column_name → key pointer).
    std::unordered_map<std::string, const std::vector<uint8_t>*> key_cache_;

    /// Resolve the AES key for a given column (O(1) cache lookup).
    [[nodiscard]] const std::vector<uint8_t>& get_column_key(
        const std::string& column_name) const {

        auto it = key_cache_.find(column_name);
        if (it != key_cache_.end()) {
            return *it->second;
        }
        return config_.default_column_key;
    }

    // -----------------------------------------------------------------------
    // derive_footer_signing_key -- HKDF-derived key for signed plaintext footer
    // -----------------------------------------------------------------------
    [[nodiscard]] std::array<uint8_t, 32> derive_footer_signing_key() const {
        static constexpr uint8_t INFO[] = "signet-pme-footer-sign-v1";
        auto prk = hkdf_extract(
            reinterpret_cast<const uint8_t*>(config_.aad_prefix.data()),
            config_.aad_prefix.size(),
            config_.footer_key.data(),
            config_.footer_key.size());
        std::array<uint8_t, 32> key{};
        (void)hkdf_expand(prk, INFO, sizeof(INFO) - 1, key.data(), key.size());
        return key;
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
    // build_aad -- Same construction as FileEncryptor (dispatches on format)
    // -----------------------------------------------------------------------
    [[nodiscard]] std::string build_aad(const std::string& prefix,
                                        uint8_t module_type,
                                        const std::string& extra = "") const {
        return detail::pme::build_aad(config_, prefix, module_type, extra);
    }

    // -----------------------------------------------------------------------
    // decrypt_gcm -- AES-GCM decryption with AAD (via ICipher)
    // -----------------------------------------------------------------------
    [[nodiscard]] expected<std::vector<uint8_t>> decrypt_gcm(
        const std::vector<uint8_t>& key,
        uint8_t module_type,
        const std::string& extra,
        const uint8_t* data, size_t size) const {

        // Validate module_type range [0..5] per Parquet PME spec (CWE-20)
        if (module_type > 5) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "PME module_type out of range [0..5] (CWE-20)"};
        }
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
