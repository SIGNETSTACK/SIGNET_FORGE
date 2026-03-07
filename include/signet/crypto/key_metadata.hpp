// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file key_metadata.hpp
/// @brief Key material, encryption configuration, and TLV serialization for
///        Parquet Modular Encryption (PME).

// ---------------------------------------------------------------------------
// key_metadata.hpp -- Key material and encryption metadata for Parquet
//                     Modular Encryption (PME)
//
// Defines the structures needed to configure, store, and retrieve encryption
// key material for Parquet files. Supports two key modes:
//
//   INTERNAL -- Key material is stored directly in file metadata. Suitable
//               for testing, development, and self-contained encrypted files.
//
//   EXTERNAL -- Only a KMS key identifier is stored; the actual key material
//               is retrieved from an external Key Management Service at
//               read time.
//
// Serialization uses a simple binary TLV (tag-length-value) format:
//   [4-byte LE tag] [4-byte LE length] [data bytes]
// for each field. This is intentionally simple and deterministic -- no
// alignment padding, no variable-length integers.
//
// Tags used:
//   0x0001  key_mode       (4 bytes, int32_t LE)
//   0x0002  key_material   (variable length blob)
//   0x0003  key_id         (variable length UTF-8 string)
//   0x0010  algorithm      (4 bytes, int32_t LE)
//   0x0011  footer_encrypted (1 byte, 0/1)
//   0x0012  aad_prefix     (variable length UTF-8 string)
// ---------------------------------------------------------------------------

#include "signet/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace signet::forge::crypto {

// ===========================================================================
// Enumerations
// ===========================================================================

/// How the encryption key is stored or referenced.
enum class KeyMode : int32_t {
    INTERNAL = 0,  ///< Key material stored directly in file metadata (testing/dev).
    EXTERNAL = 1   ///< Key referenced by KMS key ID; actual key resolved from KMS at runtime.
};

/// Encryption algorithm identifier.
///
/// These correspond to the Parquet specification's encryption algorithms:
///   AES_GCM_V1     -- AES-256-GCM for both footer and column data.
///                     Provides authenticated encryption everywhere.
///   AES_GCM_CTR_V1 -- AES-256-GCM for footer (authenticated), AES-256-CTR
///                     for column data (faster, integrity from page checksums).
///                     This is the Parquet standard default.
enum class EncryptionAlgorithm : int32_t {
    AES_GCM_V1     = 0,  ///< AES-256-GCM for both footer and column data.
    AES_GCM_CTR_V1 = 1   ///< AES-256-GCM for footer, AES-256-CTR for column data (Parquet default).
};

// ===========================================================================
// ColumnKeySpec -- Per-column encryption key specification
// ===========================================================================

/// Specifies the encryption key for a single Parquet column.
///
/// For INTERNAL mode, `key` holds the raw 32-byte AES-256 key.
/// For EXTERNAL mode, `key_id` holds the KMS identifier used to retrieve
/// the key at runtime.
struct ColumnKeySpec {
    std::string          column_name;  ///< Parquet column path (e.g. "a.b.c").
    std::vector<uint8_t> key;          ///< 32-byte AES-256 key (INTERNAL mode).
    std::string          key_id;       ///< KMS key identifier (EXTERNAL mode).
};

// ===========================================================================
// EncryptionConfig -- Complete encryption configuration for a Parquet file
// ===========================================================================

/// Top-level configuration structure that drives FileEncryptor / FileDecryptor.
///
/// To encrypt a Parquet file, populate this structure and pass it to
/// FileEncryptor. To decrypt, populate it with the same keys/KMS references
/// and pass it to FileDecryptor.
struct EncryptionConfig {
    /// Encryption algorithm (GCM everywhere, or GCM-footer + CTR-columns).
    EncryptionAlgorithm algorithm = EncryptionAlgorithm::AES_GCM_CTR_V1;

    // --- Footer encryption ---------------------------------------------------

    /// 32-byte AES-256 key for encrypting the Parquet footer (FileMetaData).
    std::vector<uint8_t> footer_key;

    /// KMS key identifier for the footer key (EXTERNAL mode).
    std::string footer_key_id;

    /// If true, the footer is encrypted. If false, the footer is stored in
    /// plaintext with an HMAC signature for integrity (signed plaintext footer).
    bool encrypt_footer = true;

    // --- Column encryption ---------------------------------------------------

    /// Per-column key specifications. Columns listed here get their own key.
    std::vector<ColumnKeySpec> column_keys;

    /// Default column key (32 bytes). Used for any column not explicitly
    /// listed in `column_keys`. If empty and a column has no specific key,
    /// that column's data is stored unencrypted.
    std::vector<uint8_t> default_column_key;

    /// KMS key identifier for the default column key (EXTERNAL mode).
    std::string default_column_key_id;

    // --- Key mode ------------------------------------------------------------

    /// INTERNAL: keys stored in file metadata. EXTERNAL: KMS references only.
    KeyMode key_mode = KeyMode::INTERNAL;

    // --- AAD (Additional Authenticated Data) ---------------------------------

    /// AAD prefix -- typically a file identifier or URI. Bound into every
    /// GCM authentication tag so ciphertext cannot be transplanted between
    /// files without detection.
    std::string aad_prefix;
};

// ===========================================================================
// Serialization helpers (TLV: tag-length-value, all little-endian)
// ===========================================================================
namespace detail::meta {

/// Write a 4-byte little-endian uint32 to dst.
inline void write_le32(uint8_t* dst, uint32_t val) {
    dst[0] = static_cast<uint8_t>(val);
    dst[1] = static_cast<uint8_t>(val >> 8);
    dst[2] = static_cast<uint8_t>(val >> 16);
    dst[3] = static_cast<uint8_t>(val >> 24);
}

/// Read a 4-byte little-endian uint32 from src.
inline uint32_t read_le32(const uint8_t* src) {
    return static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) << 8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}

/// Append a TLV field: [4-byte LE tag] [4-byte LE length] [data].
inline void append_tlv(std::vector<uint8_t>& buf,
                       uint32_t tag,
                       const uint8_t* data, uint32_t len) {
    size_t pos = buf.size();
    buf.resize(pos + 8 + len);
    write_le32(buf.data() + pos, tag);
    write_le32(buf.data() + pos + 4, len);
    if (len > 0) {
        std::memcpy(buf.data() + pos + 8, data, len);
    }
}

/// Append a TLV field containing a single int32_t (little-endian).
inline void append_tlv_i32(std::vector<uint8_t>& buf,
                           uint32_t tag, int32_t val) {
    uint8_t tmp[4];
    write_le32(tmp, static_cast<uint32_t>(val));
    append_tlv(buf, tag, tmp, 4);
}

/// Append a TLV field containing a single byte.
inline void append_tlv_u8(std::vector<uint8_t>& buf,
                          uint32_t tag, uint8_t val) {
    append_tlv(buf, tag, &val, 1);
}

/// Append a TLV field containing a string.
inline void append_tlv_str(std::vector<uint8_t>& buf,
                           uint32_t tag, const std::string& s) {
    append_tlv(buf, tag,
               reinterpret_cast<const uint8_t*>(s.data()),
               static_cast<uint32_t>(s.size()));
}

/// Append a TLV field containing a blob.
inline void append_tlv_blob(std::vector<uint8_t>& buf,
                            uint32_t tag,
                            const std::vector<uint8_t>& blob) {
    append_tlv(buf, tag, blob.data(), static_cast<uint32_t>(blob.size()));
}

/// Parsed TLV (tag-length-value) field from serialized metadata.
struct TlvField {
    uint32_t       tag;     ///< 4-byte tag identifying the field type.
    const uint8_t* data;    ///< Pointer to the field data within the source buffer.
    uint32_t       length;  ///< Length of the field data in bytes.
};

/// Maximum TLV field length (64 MB cap to prevent memory exhaustion from malformed data).
static constexpr uint32_t MAX_TLV_LENGTH = 64u * 1024u * 1024u;

/// Maximum total metadata size (1 MB cap to prevent memory exhaustion from crafted payloads, CWE-770).
static constexpr size_t MAX_METADATA_SIZE = 1024 * 1024;

/// Parse the next TLV field from a buffer.
///
/// On success, advances @p offset past the field and populates @p field.
/// @param buf       Pointer to the serialized buffer.
/// @param buf_size  Total buffer size in bytes.
/// @param offset    Current read offset (updated on success).
/// @param field     Output field (populated on success).
/// @return True on success, false if truncated or oversized.
inline bool read_tlv(const uint8_t* buf, size_t buf_size,
                     size_t& offset, TlvField& field) {
    if (offset + 8 > buf_size) return false;
    field.tag    = read_le32(buf + offset);
    field.length = read_le32(buf + offset + 4);
    // Guard: reject oversized TLV fields
    if (field.length > MAX_TLV_LENGTH) return false;
    // Overflow-safe bounds check: use subtraction instead of addition
    size_t remaining = buf_size - (offset + 8);
    if (field.length > remaining) return false;
    field.data = buf + offset + 8;
    offset += 8 + field.length;
    return true;
}

/// Read an int32 from a TLV field's data (must be exactly 4 bytes).
inline bool tlv_to_i32(const TlvField& field, int32_t& out) {
    if (field.length != 4) return false;
    out = static_cast<int32_t>(read_le32(field.data));
    return true;
}

/// Read a uint8 from a TLV field's data (must be exactly 1 byte).
inline bool tlv_to_u8(const TlvField& field, uint8_t& out) {
    if (field.length != 1) return false;
    out = field.data[0];
    return true;
}

/// Read a string from a TLV field's data.
inline std::string tlv_to_str(const TlvField& field) {
    return std::string(reinterpret_cast<const char*>(field.data), field.length);
}

/// Read a blob from a TLV field's data.
inline std::vector<uint8_t> tlv_to_blob(const TlvField& field) {
    return std::vector<uint8_t>(field.data, field.data + field.length);
}

/// @name TLV tag constants for key metadata serialization.
/// @{
static constexpr uint32_t TAG_KEY_MODE         = 0x0001;  ///< Tag: key mode (4 bytes, int32_t LE).
static constexpr uint32_t TAG_KEY_MATERIAL     = 0x0002;  ///< Tag: raw key material (variable blob).
static constexpr uint32_t TAG_KEY_ID           = 0x0003;  ///< Tag: KMS key identifier (variable UTF-8).
static constexpr uint32_t TAG_ALGORITHM        = 0x0010;  ///< Tag: encryption algorithm (4 bytes, int32_t LE).
static constexpr uint32_t TAG_FOOTER_ENCRYPTED = 0x0011;  ///< Tag: footer-encrypted flag (1 byte, 0/1).
static constexpr uint32_t TAG_AAD_PREFIX       = 0x0012;  ///< Tag: AAD prefix string (variable UTF-8).
/// @}

} // namespace detail::meta

// ===========================================================================
// EncryptionKeyMetadata -- Key material stored in file/column metadata
// ===========================================================================

/// Per-key metadata stored alongside encrypted Parquet components.
///
/// For INTERNAL mode: `key_material` contains the raw AES key bytes.
/// For EXTERNAL mode: `key_id` contains the KMS reference string.
///
/// This is serialized into the Parquet ColumnChunk's `column_crypto_metadata`
/// field so that the decryptor can identify which key to use.
struct EncryptionKeyMetadata {
    KeyMode              key_mode     = KeyMode::INTERNAL;  ///< INTERNAL or EXTERNAL key mode.
    std::vector<uint8_t> key_material;  ///< Raw AES key bytes (INTERNAL mode only).
    std::string          key_id;        ///< KMS key reference (EXTERNAL mode).

    /// Serialize to bytes using TLV format.
    /// @return Serialized byte vector containing all populated fields.
    [[nodiscard]] std::vector<uint8_t> serialize() const {
        using namespace detail::meta;

        std::vector<uint8_t> buf;
        buf.reserve(64);

        // Key mode (always present)
        append_tlv_i32(buf, TAG_KEY_MODE, static_cast<int32_t>(key_mode));

        // Key material (INTERNAL) or key ID (EXTERNAL)
        if (key_mode == KeyMode::INTERNAL && !key_material.empty()) {
            append_tlv_blob(buf, TAG_KEY_MATERIAL, key_material);
        }
        if (!key_id.empty()) {
            append_tlv_str(buf, TAG_KEY_ID, key_id);
        }

        return buf;
    }

    /// Deserialize from bytes. Returns an error if the data is malformed.
    /// @param data  Pointer to TLV-serialized bytes.
    /// @param size  Number of bytes to parse.
    /// @return Deserialized metadata, or ENCRYPTION_ERROR if truncated or missing key_mode.
    [[nodiscard]] static expected<EncryptionKeyMetadata> deserialize(
        const uint8_t* data, size_t size) {

        using namespace detail::meta;

        if (size > MAX_METADATA_SIZE) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "key metadata exceeds 1 MB limit (CWE-770)"};
        }

        EncryptionKeyMetadata meta;
        size_t offset = 0;
        bool found_mode = false;

        while (offset < size) {
            TlvField field;
            if (!read_tlv(data, size, offset, field)) {
                return Error{ErrorCode::ENCRYPTION_ERROR,
                             "EncryptionKeyMetadata: truncated TLV field"};
            }

            switch (field.tag) {
                case TAG_KEY_MODE: {
                    int32_t mode_val;
                    if (!tlv_to_i32(field, mode_val)) {
                        return Error{ErrorCode::ENCRYPTION_ERROR,
                                     "EncryptionKeyMetadata: invalid key_mode field"};
                    }
                    meta.key_mode = static_cast<KeyMode>(mode_val);
                    found_mode = true;
                    break;
                }
                case TAG_KEY_MATERIAL:
                    meta.key_material = tlv_to_blob(field);
                    break;
                case TAG_KEY_ID:
                    meta.key_id = tlv_to_str(field);
                    break;
                default:
                    // Unknown tags are silently skipped for forward compatibility
                    break;
            }
        }

        if (!found_mode) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "EncryptionKeyMetadata: missing key_mode field"};
        }

        return meta;
    }
};

// ===========================================================================
// FileEncryptionProperties -- File-level encryption metadata
// ===========================================================================

/// Stored in the Parquet FileMetaData.encryption_algorithm field.
///
/// This tells the reader which algorithm was used, whether the footer
/// itself is encrypted, and the AAD prefix bound into authentication tags.
struct FileEncryptionProperties {
    EncryptionAlgorithm algorithm        = EncryptionAlgorithm::AES_GCM_CTR_V1;  ///< Encryption algorithm.
    bool                footer_encrypted = true;    ///< Whether the footer itself is encrypted.
    std::string         aad_prefix;                 ///< AAD prefix bound into GCM auth tags.

    /// Serialize to bytes using TLV format.
    /// @return Serialized byte vector.
    [[nodiscard]] std::vector<uint8_t> serialize() const {
        using namespace detail::meta;

        std::vector<uint8_t> buf;
        buf.reserve(64);

        append_tlv_i32(buf, TAG_ALGORITHM, static_cast<int32_t>(algorithm));
        append_tlv_u8(buf, TAG_FOOTER_ENCRYPTED, footer_encrypted ? 1 : 0);
        if (!aad_prefix.empty()) {
            append_tlv_str(buf, TAG_AAD_PREFIX, aad_prefix);
        }

        return buf;
    }

    /// Deserialize from bytes.
    /// @param data  Pointer to TLV-serialized bytes.
    /// @param size  Number of bytes to parse.
    /// @return Deserialized properties, or ENCRYPTION_ERROR if truncated or missing algorithm.
    [[nodiscard]] static expected<FileEncryptionProperties> deserialize(
        const uint8_t* data, size_t size) {

        using namespace detail::meta;

        if (size > MAX_METADATA_SIZE) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "key metadata exceeds 1 MB limit (CWE-770)"};
        }

        FileEncryptionProperties props;
        size_t offset = 0;
        bool found_algo = false;

        while (offset < size) {
            TlvField field;
            if (!read_tlv(data, size, offset, field)) {
                return Error{ErrorCode::ENCRYPTION_ERROR,
                             "FileEncryptionProperties: truncated TLV field"};
            }

            switch (field.tag) {
                case TAG_ALGORITHM: {
                    int32_t algo_val;
                    if (!tlv_to_i32(field, algo_val)) {
                        return Error{ErrorCode::ENCRYPTION_ERROR,
                                     "FileEncryptionProperties: invalid algorithm field"};
                    }
                    props.algorithm = static_cast<EncryptionAlgorithm>(algo_val);
                    found_algo = true;
                    break;
                }
                case TAG_FOOTER_ENCRYPTED: {
                    uint8_t val;
                    if (!tlv_to_u8(field, val)) {
                        return Error{ErrorCode::ENCRYPTION_ERROR,
                                     "FileEncryptionProperties: invalid footer_encrypted field"};
                    }
                    props.footer_encrypted = (val != 0);
                    break;
                }
                case TAG_AAD_PREFIX:
                    props.aad_prefix = tlv_to_str(field);
                    break;
                default:
                    // Unknown tags silently skipped for forward compatibility
                    break;
            }
        }

        if (!found_algo) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "FileEncryptionProperties: missing algorithm field"};
        }

        return props;
    }
};

} // namespace signet::forge::crypto
