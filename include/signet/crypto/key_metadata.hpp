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
#include "signet/thrift/compact.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

// ===========================================================================
// IKmsClient -- Abstract KMS client interface (Gap P-5)
//
// Implements the two-tier key hierarchy defined by the Parquet Modular
// Encryption specification (PARQUET-1178 §3):
//
//   KEK (Key Encryption Key) -- Master key held in the KMS (AWS KMS,
//       Azure Key Vault, HashiCorp Vault, GCP Cloud KMS, etc.).
//       Never leaves the KMS boundary.
//
//   DEK (Data Encryption Key) -- Per-file or per-column AES-256 key.
//       Generated locally, wrapped (encrypted) by the KEK, and stored
//       in the Parquet file metadata as an opaque blob.
//
// On write, the caller generates a random DEK and calls wrap_key()
// to encrypt it under a KEK identified by key_id. The wrapped DEK
// is stored alongside the key_id in the file metadata.
//
// On read, the caller retrieves the wrapped DEK from file metadata
// and calls unwrap_key() with the same key_id to recover the DEK.
//
// Implementations should be thread-safe (concurrent wrap/unwrap
// from multiple FileEncryptor/FileDecryptor instances).
//
// References:
//   - Apache Parquet Modular Encryption (PARQUET-1178) §3
//   - NIST SP 800-57 Part 1 Rev. 5 §5.3 (key hierarchy)
//   - NIST SP 800-38F (AES Key Wrap)
// ===========================================================================

/// Abstract KMS client interface for DEK/KEK key wrapping.
///
/// Subclass this to integrate with a specific KMS provider.
/// The interface is intentionally minimal — only wrap and unwrap.
class IKmsClient {
public:
    virtual ~IKmsClient() = default;

    /// Wrap (encrypt) a DEK under the KEK identified by `master_key_id`.
    ///
    /// @param dek             Raw Data Encryption Key bytes (typically 32 bytes for AES-256).
    /// @param master_key_id   KMS identifier for the Key Encryption Key (KEK).
    /// @return Wrapped (encrypted) DEK bytes, or error on KMS failure.
    [[nodiscard]] virtual expected<std::vector<uint8_t>> wrap_key(
        const std::vector<uint8_t>& dek,
        const std::string& master_key_id) const = 0;

    /// Unwrap (decrypt) a wrapped DEK using the KEK identified by `master_key_id`.
    ///
    /// @param wrapped_dek     Wrapped DEK bytes (as returned by wrap_key).
    /// @param master_key_id   KMS identifier for the Key Encryption Key (KEK).
    /// @return Unwrapped (plaintext) DEK bytes, or error on KMS failure / auth failure.
    [[nodiscard]] virtual expected<std::vector<uint8_t>> unwrap_key(
        const std::vector<uint8_t>& wrapped_dek,
        const std::string& master_key_id) const = 0;
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

    // --- KMS client (Gap P-5) ------------------------------------------------

    /// Optional KMS client for DEK/KEK key wrapping (EXTERNAL key mode).
    ///
    /// When set, FileEncryptor::wrap_keys() wraps all DEKs under their
    /// respective KEKs, and FileDecryptor::unwrap_keys() recovers DEKs
    /// from the wrapped blobs stored in file metadata.
    ///
    /// Must implement IKmsClient::wrap_key() and unwrap_key().
    /// Not required for INTERNAL key mode (keys stored in plaintext).
    std::shared_ptr<IKmsClient> kms_client;

    // --- AAD (Additional Authenticated Data) ---------------------------------

    /// AAD prefix -- typically a file identifier or URI. Bound into every
    /// GCM authentication tag so ciphertext cannot be transplanted between
    /// files without detection.
    std::string aad_prefix;

    // --- AAD format (Gap P-4) ------------------------------------------------

    /// AAD construction format.
    ///
    /// LEGACY: aad_prefix + '\0' + module_type_byte + '\0' + extra
    ///   (Signet v1 format — string concatenation with null separators)
    ///
    /// SPEC_BINARY: aad_file_unique || module_type (1 byte) ||
    ///              row_group_ordinal (2 bytes LE) ||
    ///              column_ordinal (2 bytes LE) ||
    ///              page_ordinal (2 bytes LE)
    ///   (Parquet PME spec format — fixed-width binary ordinals,
    ///    compatible with parquet-mr and pyarrow)
    ///
    /// Default is LEGACY for backward compatibility with existing encrypted
    /// files. Set to SPEC_BINARY for cross-implementation interoperability.
    enum class AadFormat : int32_t {
        LEGACY      = 0,  ///< Signet v1: null-separated string AAD
        SPEC_BINARY = 1,  ///< Parquet PME spec: fixed-width binary AAD
    };
    AadFormat aad_format = AadFormat::LEGACY;
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

/// Maximum TLV field length (64 MB cap to prevent memory exhaustion from malformed data).
inline constexpr uint32_t MAX_TLV_LENGTH = 64u * 1024u * 1024u;

/// Maximum total metadata size (1 MB cap to prevent memory exhaustion from crafted payloads, CWE-770).
inline constexpr size_t MAX_METADATA_SIZE = 1024 * 1024;

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
    if (s.size() > MAX_TLV_LENGTH) {
        throw std::overflow_error("TLV value exceeds maximum length");
    }
    append_tlv(buf, tag,
               reinterpret_cast<const uint8_t*>(s.data()),
               static_cast<uint32_t>(s.size()));
}

/// Append a TLV field containing a blob.
inline void append_tlv_blob(std::vector<uint8_t>& buf,
                            uint32_t tag,
                            const std::vector<uint8_t>& blob) {
    if (blob.size() > MAX_TLV_LENGTH) {
        throw std::overflow_error("TLV value exceeds maximum length");
    }
    append_tlv(buf, tag, blob.data(), static_cast<uint32_t>(blob.size()));
}

/// Parsed TLV (tag-length-value) field from serialized metadata.
struct TlvField {
    uint32_t       tag;     ///< 4-byte tag identifying the field type.
    const uint8_t* data;    ///< Pointer to the field data within the source buffer.
    uint32_t       length;  ///< Length of the field data in bytes.
};

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
inline constexpr uint32_t TAG_KEY_MODE         = 0x0001;  ///< Tag: key mode (4 bytes, int32_t LE).
inline constexpr uint32_t TAG_KEY_MATERIAL     = 0x0002;  ///< Tag: raw key material (variable blob).
inline constexpr uint32_t TAG_KEY_ID           = 0x0003;  ///< Tag: KMS key identifier (variable UTF-8).
inline constexpr uint32_t TAG_ALGORITHM        = 0x0010;  ///< Tag: encryption algorithm (4 bytes, int32_t LE).
inline constexpr uint32_t TAG_FOOTER_ENCRYPTED = 0x0011;  ///< Tag: footer-encrypted flag (1 byte, 0/1).
inline constexpr uint32_t TAG_AAD_PREFIX       = 0x0012;  ///< Tag: AAD prefix string (variable UTF-8).
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
#ifndef SIGNET_SUPPRESS_INTERNAL_KEY_WARNING
            static bool warned = false;
            if (!warned) {
                fprintf(stderr, "[SIGNET WARNING] KeyMode::INTERNAL stores encryption key in file metadata — NOT for production use\n");
                warned = true;
            }
#endif
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
                    if (mode_val < 0 || mode_val > 1) {
                        return Error{ErrorCode::INVALID_ARGUMENT,
                                     "EncryptionKeyMetadata: invalid KeyMode value"};
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
                    if (algo_val < 0 || algo_val > 1) {
                        return Error{ErrorCode::INVALID_ARGUMENT,
                                     "FileEncryptionProperties: invalid EncryptionAlgorithm value"};
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

// ===========================================================================
// Thrift-based serialization (Gap P-6)
//
// The Parquet PME spec uses Thrift Compact Protocol for key metadata,
// specifically the ColumnCryptoMetaData struct. Adding Thrift serialization
// alongside the existing TLV format enables cross-implementation interop
// with parquet-mr (Java), pyarrow, and other Parquet implementations.
//
// Parquet Thrift schema (from parquet.thrift):
//
//   struct AesGcmV1 {
//     1: optional binary aad_prefix
//     2: optional binary aad_file_unique
//     3: optional bool supply_aad_prefix
//   }
//   struct AesGcmCtrV1 {
//     1: optional binary aad_prefix
//     2: optional binary aad_file_unique
//     3: optional bool supply_aad_prefix
//   }
//   union EncryptionAlgorithm {
//     1: AesGcmV1 AES_GCM_V1
//     2: AesGcmCtrV1 AES_GCM_CTR_V1
//   }
//   struct ColumnCryptoMetaData {
//     1: required EncryptionAlgorithm ENCRYPTION_WITH_FOOTER_KEY
//     2: optional binary key_metadata
//   }
//   struct FileCryptoMetaData {
//     1: required EncryptionAlgorithm encryption_algorithm
//     2: optional binary key_metadata
//   }
//
// Reference: https://github.com/apache/parquet-format/blob/master/src/main/thrift/parquet.thrift
// ===========================================================================

namespace detail::thrift_crypto {

/// Serialize EncryptionKeyMetadata to Thrift Compact Protocol.
///
/// Thrift struct layout (ColumnCryptoMetaData-compatible):
///   field 1 (STRUCT): EncryptionAlgorithm union
///     - If AES_GCM_V1: field 1 of the union, containing AesGcmV1 struct
///     - If AES_GCM_CTR_V1: field 2 of the union, containing AesGcmCtrV1 struct
///   field 2 (BINARY): key_metadata (opaque blob containing key material or key_id)
///   field 3 (I32): key_mode (Signet extension — ignored by other implementations)
inline std::vector<uint8_t> serialize_key_metadata(
        const EncryptionKeyMetadata& meta,
        EncryptionAlgorithm algo = EncryptionAlgorithm::AES_GCM_CTR_V1,
        const std::string& aad_prefix = "") {

    thrift::CompactEncoder enc;

    // Field 1: EncryptionAlgorithm (union, encoded as struct)
    enc.write_field(1, thrift::compact_type::STRUCT);
    enc.begin_struct();
    {
        // Union: field 1 = AES_GCM_V1, field 2 = AES_GCM_CTR_V1
        int16_t union_field = (algo == EncryptionAlgorithm::AES_GCM_V1) ? 1 : 2;
        enc.write_field(union_field, thrift::compact_type::STRUCT);
        enc.begin_struct();
        {
            // AesGcmV1 / AesGcmCtrV1 inner struct
            if (!aad_prefix.empty()) {
                enc.write_field(1, thrift::compact_type::BINARY);
                enc.write_binary(
                    reinterpret_cast<const uint8_t*>(aad_prefix.data()),
                    aad_prefix.size());
            }
            enc.write_stop();
        }
        enc.end_struct();
        enc.write_stop();
    }
    enc.end_struct();

    // Field 2: key_metadata (opaque binary — contains serialized key info)
    // We embed a mini TLV inside the binary blob for key_mode + key_material/key_id
    // This is what parquet-mr and pyarrow do — the key_metadata field is opaque
    {
        std::vector<uint8_t> key_blob;
        // Encode key_mode as first byte
        key_blob.push_back(static_cast<uint8_t>(meta.key_mode));
        if (meta.key_mode == KeyMode::INTERNAL && !meta.key_material.empty()) {
            key_blob.push_back(0x01);  // marker: key material follows
            // 2-byte LE length + data
            uint16_t klen = static_cast<uint16_t>(meta.key_material.size());
            key_blob.push_back(static_cast<uint8_t>(klen & 0xFF));
            key_blob.push_back(static_cast<uint8_t>((klen >> 8) & 0xFF));
            key_blob.insert(key_blob.end(),
                            meta.key_material.begin(), meta.key_material.end());
        }
        if (!meta.key_id.empty()) {
            key_blob.push_back(0x02);  // marker: key_id follows
            uint16_t idlen = static_cast<uint16_t>(meta.key_id.size());
            key_blob.push_back(static_cast<uint8_t>(idlen & 0xFF));
            key_blob.push_back(static_cast<uint8_t>((idlen >> 8) & 0xFF));
            key_blob.insert(key_blob.end(), meta.key_id.begin(), meta.key_id.end());
        }

        enc.write_field(2, thrift::compact_type::BINARY);
        enc.write_binary(key_blob.data(), key_blob.size());
    }

    enc.write_stop();
    return enc.data();
}

/// Deserialize EncryptionKeyMetadata from Thrift Compact Protocol.
inline expected<EncryptionKeyMetadata> deserialize_key_metadata(
        const uint8_t* data, size_t size) {

    if (size > detail::meta::MAX_METADATA_SIZE) {
        return Error{ErrorCode::INVALID_ARGUMENT,
                     "Thrift key metadata exceeds 1 MB limit"};
    }

    thrift::CompactDecoder dec(data, size);
    EncryptionKeyMetadata meta;
    bool found_key_metadata = false;

    while (true) {
        auto fh = dec.read_field_header();
        if (fh.is_stop()) break;

        switch (fh.field_id) {
            case 1: {
                // EncryptionAlgorithm union — skip it (we don't need it for key metadata)
                dec.skip_field(fh.thrift_type);
                break;
            }
            case 2: {
                // key_metadata binary blob
                auto blob = dec.read_binary();
                if (blob.empty()) break;
                found_key_metadata = true;

                size_t off = 0;
                if (off < blob.size()) {
                    meta.key_mode = static_cast<KeyMode>(blob[off++]);
                }
                while (off < blob.size()) {
                    uint8_t marker = blob[off++];
                    if (marker == 0x01 && off + 2 <= blob.size()) {
                        // key material
                        uint16_t klen = static_cast<uint16_t>(blob[off])
                                      | (static_cast<uint16_t>(blob[off + 1]) << 8);
                        off += 2;
                        if (off + klen <= blob.size()) {
                            meta.key_material.assign(blob.begin() + static_cast<ptrdiff_t>(off),
                                                     blob.begin() + static_cast<ptrdiff_t>(off + klen));
                            off += klen;
                        }
                    } else if (marker == 0x02 && off + 2 <= blob.size()) {
                        // key_id
                        uint16_t idlen = static_cast<uint16_t>(blob[off])
                                       | (static_cast<uint16_t>(blob[off + 1]) << 8);
                        off += 2;
                        if (off + idlen <= blob.size()) {
                            meta.key_id = std::string(
                                reinterpret_cast<const char*>(blob.data() + off), idlen);
                            off += idlen;
                        }
                    } else {
                        break;  // unknown marker, stop
                    }
                }
                break;
            }
            default:
                dec.skip_field(fh.thrift_type);
                break;
        }
    }

    if (!found_key_metadata) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "Thrift ColumnCryptoMetaData: missing key_metadata field"};
    }

    return meta;
}

/// Serialize FileEncryptionProperties to Thrift Compact Protocol.
///
/// Thrift struct layout (FileCryptoMetaData-compatible):
///   field 1 (STRUCT): EncryptionAlgorithm union
///   field 2 (BINARY): key_metadata (optional, for footer key reference)
inline std::vector<uint8_t> serialize_file_properties(
        const FileEncryptionProperties& props) {

    thrift::CompactEncoder enc;

    // Field 1: EncryptionAlgorithm union
    enc.write_field(1, thrift::compact_type::STRUCT);
    enc.begin_struct();
    {
        int16_t union_field = (props.algorithm == EncryptionAlgorithm::AES_GCM_V1) ? 1 : 2;
        enc.write_field(union_field, thrift::compact_type::STRUCT);
        enc.begin_struct();
        {
            if (!props.aad_prefix.empty()) {
                enc.write_field(1, thrift::compact_type::BINARY);
                enc.write_binary(
                    reinterpret_cast<const uint8_t*>(props.aad_prefix.data()),
                    props.aad_prefix.size());
            }
            // field 3: supply_aad_prefix — true if aad_prefix is set
            if (!props.aad_prefix.empty()) {
                enc.write_field_bool(3, true);
            }
            enc.write_stop();
        }
        enc.end_struct();
        enc.write_stop();
    }
    enc.end_struct();

    // Field 3 (Signet extension): footer_encrypted flag
    enc.write_field_bool(3, props.footer_encrypted);

    enc.write_stop();
    return enc.data();
}

/// Deserialize FileEncryptionProperties from Thrift Compact Protocol.
inline expected<FileEncryptionProperties> deserialize_file_properties(
        const uint8_t* data, size_t size) {

    if (size > detail::meta::MAX_METADATA_SIZE) {
        return Error{ErrorCode::INVALID_ARGUMENT,
                     "Thrift file properties exceeds 1 MB limit"};
    }

    thrift::CompactDecoder dec(data, size);
    FileEncryptionProperties props;
    bool found_algo = false;

    while (true) {
        auto fh = dec.read_field_header();
        if (fh.is_stop()) break;

        switch (fh.field_id) {
            case 1: {
                // EncryptionAlgorithm union — serialized as nested struct
                dec.begin_struct();
                auto inner_fh = dec.read_field_header();
                if (!inner_fh.is_stop()) {
                    if (inner_fh.field_id == 1) {
                        props.algorithm = EncryptionAlgorithm::AES_GCM_V1;
                    } else {
                        props.algorithm = EncryptionAlgorithm::AES_GCM_CTR_V1;
                    }
                    found_algo = true;

                    // Read inner AesGcmV1/AesGcmCtrV1 struct
                    dec.begin_struct();
                    while (true) {
                        auto aes_fh = dec.read_field_header();
                        if (aes_fh.is_stop()) break;
                        if (aes_fh.field_id == 1 &&
                            aes_fh.thrift_type == thrift::compact_type::BINARY) {
                            auto prefix_bin = dec.read_binary();
                            props.aad_prefix = std::string(
                                reinterpret_cast<const char*>(prefix_bin.data()),
                                prefix_bin.size());
                        } else {
                            dec.skip_field(aes_fh.thrift_type);
                        }
                    }
                    dec.end_struct();

                    // Read remaining union fields until stop
                    while (true) {
                        auto ufh = dec.read_field_header();
                        if (ufh.is_stop()) break;
                        dec.skip_field(ufh.thrift_type);
                    }
                }
                dec.end_struct();
                break;
            }
            case 3: {
                // footer_encrypted bool (Signet extension)
                // Bool fields have value encoded in type nibble
                props.footer_encrypted =
                    (fh.thrift_type == thrift::compact_type::BOOL_TRUE);
                break;
            }
            default:
                dec.skip_field(fh.thrift_type);
                break;
        }
    }

    if (!found_algo) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "Thrift FileCryptoMetaData: missing encryption_algorithm"};
    }

    return props;
}

} // namespace detail::thrift_crypto

// ===========================================================================
// MetadataFormat — selects TLV (legacy) or Thrift (spec-compliant) wire format
// ===========================================================================

/// Wire format for key metadata serialization.
///
/// TLV: Signet v1 custom format (backward-compatible, existing files).
/// THRIFT: Parquet spec Thrift Compact Protocol (cross-implementation interop).
enum class MetadataFormat : int32_t {
    TLV    = 0,  ///< Signet v1 custom TLV format
    THRIFT = 1,  ///< Parquet Thrift Compact Protocol (spec-compliant)
};

// ===========================================================================
// Algorithm deprecation framework (Gap C-4)
//
// NIST SP 800-131A Rev. 2 — Transitioning the Use of Cryptographic
// Algorithms and Key Lengths.
//
// Tracks algorithm lifecycle status to support deprecation planning.
//
// Reference: NIST SP 800-131A Rev. 2 (March 2019)
// ===========================================================================

/// Algorithm lifecycle status per NIST SP 800-131A.
enum class AlgorithmStatus : int32_t {
    ACCEPTABLE   = 0,  ///< Approved for use.
    DEPRECATED   = 1,  ///< Still allowed but scheduled for removal.
    DISALLOWED   = 2,  ///< Must not be used.
    LEGACY       = 3,  ///< Only for processing existing data (no new encryption).
};

/// Algorithm deprecation entry.
struct AlgorithmPolicy {
    std::string       algorithm_name;       ///< E.g. "AES-256-GCM", "AES-128-CTR", "3DES".
    AlgorithmStatus   status = AlgorithmStatus::ACCEPTABLE;
    int32_t           min_key_bits = 0;     ///< Minimum key length in bits.
    std::string       transition_guidance;  ///< Migration guidance.
    int64_t           sunset_ns = 0;        ///< Planned deprecation timestamp (0 = no sunset).
};

// ===========================================================================
// INTERNAL key mode production gate (Gap C-15)
//
// FIPS 140-3 §7.7 — INTERNAL key mode stores plaintext keys in file
// metadata, which is unsuitable for production. This compile-time
// gate prevents accidental use in production builds.
//
// Reference: FIPS 140-3 §7.7 — Key/CSP zeroization
// ===========================================================================

/// Check if INTERNAL key mode is allowed in the current build.
///
/// In production builds (SIGNET_REQUIRE_COMMERCIAL_LICENSE=ON),
/// INTERNAL key mode should be rejected. This function provides
/// a runtime check that can be called before file encryption.
///
/// @param mode  The key mode to validate.
/// @return void if allowed, error if INTERNAL mode in production.
[[nodiscard]] inline expected<void> validate_key_mode_for_production(KeyMode mode) {
#if defined(SIGNET_REQUIRE_COMMERCIAL_LICENSE) && SIGNET_REQUIRE_COMMERCIAL_LICENSE
    if (mode == KeyMode::INTERNAL) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "INTERNAL key mode stores plaintext keys in file metadata — "
                     "not allowed in production builds (FIPS 140-3 §7.7). "
                     "Use EXTERNAL key mode with a KMS client."};
    }
#else
    (void)mode;
#endif
    return {};
}

// ===========================================================================
// Key rotation API (Gap T-7)
//
// PCI-DSS, HIPAA, SOX, and DORA Art. 9(2) require cryptographic key
// rotation with documented procedures. This API provides the mechanism
// to rotate keys and re-encrypt files.
//
// Reference: NIST SP 800-57 Part 1 Rev. 5 §5.3.5 (key transition)
//            PCI-DSS v4.0 Req. 3.6.4 (cryptographic key rotation)
// ===========================================================================

/// Key rotation request describing old → new key transition.
struct KeyRotationRequest {
    std::string              key_id;            ///< Key being rotated.
    std::vector<uint8_t>     old_key;           ///< Current (old) key.
    std::vector<uint8_t>     new_key;           ///< Replacement key.
    std::string              reason;            ///< Rotation reason: "scheduled", "compromised", "policy".
    int64_t                  requested_ns = 0;  ///< Rotation request timestamp.
};

/// Key rotation result.
struct KeyRotationResult {
    bool          success = false;          ///< Whether the rotation completed.
    std::string   key_id;                   ///< Rotated key ID.
    int64_t       completed_ns = 0;         ///< Completion timestamp.
    int64_t       files_re_encrypted = 0;   ///< Number of files re-encrypted.
    std::string   error_message;            ///< Error message (if not successful).
};

// ===========================================================================
// CryptoShredder — GDPR right-to-erasure via per-subject key destruction
// (Gap G-1)
//
// Implements GDPR Art. 17 "right to be forgotten" via cryptographic erasure:
// each data subject's records are encrypted with a unique per-subject DEK.
// To "erase" a subject's data, the subject's DEK is destroyed, rendering
// all their encrypted records permanently unreadable without needing to
// locate and delete every copy of the data.
//
// This approach is recognized by EDPB (European Data Protection Board)
// Guidelines 8/2020 as a valid erasure method when "deletion of personal
// data is not feasible" (e.g., in immutable data stores, backups, or
// distributed systems).
//
// Usage:
//   CryptoShredder shredder;
//   auto result = shredder.register_subject("user-42", subject_dek);
//   // ... write encrypted data using subject_dek ...
//   shredder.shred("user-42");  // Destroys the DEK → data is unreadable
//
// References:
//   - GDPR Art. 17 — Right to erasure
//   - EDPB Guidelines 8/2020 — Technical measures for erasure
//   - NIST SP 800-88 Rev. 1 §2.4 — Cryptographic Erase (CE)
// ===========================================================================

/// Per-subject key store supporting cryptographic erasure.
///
/// Thread-safety: NOT thread-safe — callers must synchronize externally.
class CryptoShredder {
public:
    /// Register a data subject's DEK.
    /// @param subject_id  Unique identifier for the data subject.
    /// @param dek         The subject's Data Encryption Key (32 bytes).
    /// @return void on success, error if subject_id already registered.
    [[nodiscard]] expected<void> register_subject(
        const std::string& subject_id,
        const std::vector<uint8_t>& dek) {

        if (subject_id.empty()) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "CryptoShredder: subject_id must not be empty"};
        }
        if (keys_.count(subject_id) > 0) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "CryptoShredder: subject '" + subject_id + "' already registered"};
        }
        keys_[subject_id] = dek;
        return {};
    }

    /// Retrieve a subject's DEK for encryption/decryption.
    /// @param subject_id  The data subject identifier.
    /// @return Pointer to the DEK, or error if not found or shredded.
    [[nodiscard]] expected<const std::vector<uint8_t>*> get_key(
        const std::string& subject_id) const {

        auto it = keys_.find(subject_id);
        if (it == keys_.end()) {
            // Check if this subject was previously shredded
            if (shredded_.count(subject_id) > 0) {
                return Error{ErrorCode::ENCRYPTION_ERROR,
                             "CryptoShredder: subject '" + subject_id +
                             "' has been cryptographically erased (GDPR Art. 17)"};
            }
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "CryptoShredder: subject '" + subject_id + "' not found"};
        }
        return &it->second;
    }

    /// Cryptographically shred a subject's data by destroying their DEK.
    ///
    /// After this call, all data encrypted with this subject's DEK becomes
    /// permanently unreadable (NIST SP 800-88 Rev. 1 §2.4 Cryptographic Erase).
    ///
    /// @param subject_id  The data subject to erase.
    /// @return void on success, error if subject not found.
    [[nodiscard]] expected<void> shred(const std::string& subject_id) {
        auto it = keys_.find(subject_id);
        if (it == keys_.end()) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "CryptoShredder: subject '" + subject_id + "' not found"};
        }

        // Securely zero the key material before erasing
        volatile unsigned char* p =
            reinterpret_cast<volatile unsigned char*>(it->second.data());
        for (size_t i = 0; i < it->second.size(); ++i) p[i] = 0;

        keys_.erase(it);
        shredded_.insert(subject_id);
        return {};
    }

    /// Check if a subject has been cryptographically erased.
    [[nodiscard]] bool is_shredded(const std::string& subject_id) const {
        return shredded_.count(subject_id) > 0;
    }

    /// Number of active (non-shredded) subjects.
    [[nodiscard]] size_t active_count() const { return keys_.size(); }

    /// Number of shredded subjects.
    [[nodiscard]] size_t shredded_count() const { return shredded_.size(); }

private:
    std::unordered_map<std::string, std::vector<uint8_t>> keys_;
    std::unordered_set<std::string> shredded_;
};

} // namespace signet::forge::crypto
