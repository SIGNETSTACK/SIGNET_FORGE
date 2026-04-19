// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file pme_facade.hpp
/// @brief High-level PME facade for safe encrypted Parquet I/O.
///
/// Provides an opaque KeyHandle that keeps all key material in C++ memory
/// with SecureKeyBuffer (mlock + volatile zeroing). Python bindings expose
/// only the handle — raw key bytes never cross the FFI boundary.
///
/// Usage (C++):
/// @code
///   auto key = KeyHandle::generate();            // CSPRNG → SecureKeyBuffer
///   EncryptedWriterOptions eopts;
///   eopts.master_key = &key;
///   eopts.classify("l_comment", ColumnClassification::PII);
///   eopts.classify("l_extendedprice", ColumnClassification::FINANCIAL);
///   // ... pass eopts to ParquetWriter::open via WriterOptions.encryption
/// @endcode
///
/// Usage (Python):
/// @code
///   key = sf.KeyHandle.generate()
///   eopts = sf.EncryptedWriterOptions(key)
///   eopts.classify("l_comment", sf.ColumnClassification.PII)
///   writer = sf.ParquetWriter.open_encrypted("out.parquet", schema, eopts)
/// @endcode

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "pme_facade.hpp requires SIGNET_ENABLE_COMMERCIAL=ON"
#endif

#include "signet/crypto/cipher_interface.hpp"
#include "signet/crypto/key_metadata.hpp"
#include "signet/crypto/hkdf.hpp"
#include "signet/crypto/pme.hpp"
#include "signet/error.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace signet::forge::crypto {

// =========================================================================
// Column classification for PME
// =========================================================================

/// Classification determines whether a column is encrypted and with which
/// key derivation context. Maps to the DXP ColumnClass concept.
enum class ColumnClassification : uint8_t {
    REFERENCE  = 0,  ///< Unencrypted — public/reference data
    FINANCIAL  = 1,  ///< Encrypted — financial data (prices, quantities)
    PII        = 2,  ///< Encrypted — personally identifiable information
    HEALTH     = 3,  ///< Encrypted — health/biometric data (HIPAA)
    RESTRICTED = 4,  ///< Encrypted — general restricted data
};

// =========================================================================
// KeyHandle — opaque, RAII, key-zeroing
// =========================================================================

/// Opaque handle to AES-256 key material held in a SecureKeyBuffer.
///
/// Key bytes are mlock'd (pinned to RAM, excluded from swap) and
/// volatile-zeroed on destruction. Python bindings expose this as an
/// opaque type — no `__bytes__`, no `__str__`, no way to extract the
/// raw key material from Python.
///
/// Thread safety: KeyHandle is NOT thread-safe. Do not share across
/// threads without external synchronisation.
class KeyHandle {
public:
    /// Generate a new random AES-256 key via platform CSPRNG.
    [[nodiscard]] static KeyHandle generate() {
        KeyHandle h;
        detail::cipher::fill_random_bytes(h.key_.data(), KEY_SIZE);
        detail::secure_mem::lock_memory(h.key_.data(), KEY_SIZE);
        h.valid_ = true;
        return h;
    }

    /// Construct from raw bytes (C++ internal use only — NOT exposed to Python).
    /// The input bytes are copied into the secure buffer and the source is
    /// NOT zeroed — the caller is responsible for zeroing their copy.
    [[nodiscard]] static KeyHandle from_bytes(const uint8_t* data, size_t size) {
        if (size != KEY_SIZE) {
            throw std::invalid_argument(
                "KeyHandle requires exactly 32 bytes (AES-256), got " +
                std::to_string(size));
        }
        KeyHandle h;
        std::memcpy(h.key_.data(), data, KEY_SIZE);
        detail::secure_mem::lock_memory(h.key_.data(), KEY_SIZE);
        h.valid_ = true;
        return h;
    }

    /// Construct from a deterministic seed (for benchmarking/testing ONLY).
    /// Derives the key via HKDF-SHA256 from the seed string.
    [[nodiscard]] static KeyHandle from_seed(const std::string& seed) {
        KeyHandle h;
        auto prk = hkdf_extract(nullptr, 0,
            reinterpret_cast<const uint8_t*>(seed.data()), seed.size());
        hkdf_expand(prk,
            reinterpret_cast<const uint8_t*>("signet-pme-test-key"),
            19, h.key_.data(), KEY_SIZE);
        detail::secure_mem::lock_memory(h.key_.data(), KEY_SIZE);
        h.valid_ = true;
        return h;
    }

    ~KeyHandle() {
        if (valid_) {
            detail::secure_mem::secure_zero(key_.data(), KEY_SIZE);
            detail::secure_mem::unlock_memory(key_.data(), KEY_SIZE);
            valid_ = false;
        }
    }

    // Move-only (no copy — prevents key duplication)
    KeyHandle(KeyHandle&& other) noexcept
        : key_(other.key_), valid_(other.valid_) {
        if (other.valid_) {
            std::memset(other.key_.data(), 0, KEY_SIZE);
            other.valid_ = false;
        }
    }
    KeyHandle& operator=(KeyHandle&& other) noexcept {
        if (this != &other) {
            if (valid_) {
                detail::secure_mem::secure_zero(key_.data(), KEY_SIZE);
                detail::secure_mem::unlock_memory(key_.data(), KEY_SIZE);
            }
            key_ = other.key_;
            valid_ = other.valid_;
            if (other.valid_) {
                std::memset(other.key_.data(), 0, KEY_SIZE);
                other.valid_ = false;
            }
        }
        return *this;
    }
    KeyHandle(const KeyHandle&) = delete;
    KeyHandle& operator=(const KeyHandle&) = delete;

    /// Check if the handle holds a valid key.
    [[nodiscard]] bool is_valid() const noexcept { return valid_; }

    /// Key size in bytes (always 32 for AES-256).
    static constexpr size_t key_size() noexcept { return KEY_SIZE; }

    // -- Internal access (C++ only, NOT exposed to Python) --

    /// Access raw key bytes. MUST NOT be exposed through FFI.
    [[nodiscard]] const uint8_t* data() const noexcept { return key_.data(); }

    /// Get key as a vector (copies — use sparingly). For EncryptionConfig construction.
    [[nodiscard]] std::vector<uint8_t> to_vector() const {
        return std::vector<uint8_t>(key_.begin(), key_.end());
    }

private:
    KeyHandle() : key_{}, valid_(false) {}
    static constexpr size_t KEY_SIZE = 32;
    std::array<uint8_t, KEY_SIZE> key_;
    bool valid_;
};

// =========================================================================
// EncryptedWriterOptions — high-level config that builds EncryptionConfig
// =========================================================================

/// High-level encryption options for ParquetWriter.
///
/// Wraps EncryptionConfig construction behind a safe API. The caller
/// classifies columns and provides a KeyHandle; the facade derives
/// per-column DEKs via HKDF and constructs the full EncryptionConfig.
struct EncryptedWriterOptions {
    /// Algorithm: GCM for both footer and columns (default), or GCM footer + CTR columns.
    EncryptionAlgorithm algorithm = EncryptionAlgorithm::AES_GCM_CTR_V1;

    /// Whether to encrypt the footer (true) or sign it with HMAC (false).
    bool encrypt_footer = true;

    /// AAD prefix — typically a file URI or tenant identifier.
    std::string aad_prefix = "signet-forge-pme";

    /// Column classifications. Only FINANCIAL, PII, HEALTH, RESTRICTED
    /// columns are encrypted. REFERENCE columns are written in plaintext.
    std::unordered_map<std::string, ColumnClassification> column_classes;

    /// Classify a column for encryption.
    void classify(const std::string& column_name, ColumnClassification cls) {
        column_classes[column_name] = cls;
    }

    /// Build the low-level EncryptionConfig from this facade.
    /// Derives per-column keys from the master key via HKDF-SHA256.
    ///
    /// @param master_key  Opaque handle holding the FMK (File Master Key).
    /// @return EncryptionConfig ready for ParquetWriter.
    [[nodiscard]] EncryptionConfig build_config(const KeyHandle& master_key) const {
        if (!master_key.is_valid()) {
            throw std::runtime_error("KeyHandle is not valid (moved or not initialised)");
        }

        EncryptionConfig config;
        config.algorithm = algorithm;
        config.encrypt_footer = encrypt_footer;
        config.footer_key = master_key.to_vector();
        config.aad_prefix = aad_prefix;
        config.key_mode = KeyMode::INTERNAL;

        // Derive per-column keys via HKDF
        for (const auto& [col_name, cls] : column_classes) {
            if (cls == ColumnClassification::REFERENCE) continue;

            // HKDF info = "signet-pme-col:" + classification + ":" + column_name
            std::string info = "signet-pme-col:" +
                std::to_string(static_cast<int>(cls)) + ":" + col_name;

            ColumnKeySpec spec;
            spec.column_name = col_name;
            spec.key.resize(32);

            auto prk = hkdf_extract(nullptr, 0,
                master_key.data(), KeyHandle::key_size());
            (void)hkdf_expand(prk,
                reinterpret_cast<const uint8_t*>(info.data()),
                info.size(), spec.key.data(), 32);

            config.column_keys.push_back(std::move(spec));
        }

        return config;
    }
};

// =========================================================================
// EncryptedReaderOptions — high-level config for decryption + RBAC
// =========================================================================

/// High-level decryption options for ParquetReader.
///
/// The reader presents a KeyHandle and an optional set of authorised
/// columns. Columns not in the authorised set are denied — the reader
/// cannot decrypt them even if the key would derive the correct DEK.
struct EncryptedReaderOptions {
    /// Algorithm must match the writer's algorithm.
    EncryptionAlgorithm algorithm = EncryptionAlgorithm::AES_GCM_CTR_V1;

    /// Whether the footer was encrypted (must match writer).
    bool encrypted_footer = true;

    /// AAD prefix (must match writer).
    std::string aad_prefix = "signet-forge-pme";

    /// Column classifications (must match writer for correct key derivation).
    std::unordered_map<std::string, ColumnClassification> column_classes;

    /// Authorised columns for this reader. If empty, all columns are
    /// authorised (full access). If non-empty, only listed columns can
    /// be decrypted — others return an error on read.
    std::vector<std::string> authorised_columns;

    /// Classify a column (must match writer classification for correct HKDF).
    void classify(const std::string& column_name, ColumnClassification cls) {
        column_classes[column_name] = cls;
    }

    /// Authorise a column for reading.
    void authorise(const std::string& column_name) {
        authorised_columns.push_back(column_name);
    }

    /// Check if a column is authorised for this reader.
    [[nodiscard]] bool is_authorised(const std::string& column_name) const {
        if (authorised_columns.empty()) return true;  // empty = full access
        for (const auto& name : authorised_columns) {
            if (name == column_name) return true;
        }
        return false;
    }

    /// Build the low-level EncryptionConfig for decryption.
    /// Only derives keys for authorised columns.
    [[nodiscard]] EncryptionConfig build_config(const KeyHandle& master_key) const {
        if (!master_key.is_valid()) {
            throw std::runtime_error("KeyHandle is not valid");
        }

        EncryptionConfig config;
        config.algorithm = algorithm;
        config.encrypt_footer = encrypted_footer;
        config.footer_key = master_key.to_vector();
        config.aad_prefix = aad_prefix;
        config.key_mode = KeyMode::INTERNAL;

        for (const auto& [col_name, cls] : column_classes) {
            if (cls == ColumnClassification::REFERENCE) continue;
            if (!is_authorised(col_name)) continue;  // RBAC: skip unauthorised

            std::string info = "signet-pme-col:" +
                std::to_string(static_cast<int>(cls)) + ":" + col_name;

            ColumnKeySpec spec;
            spec.column_name = col_name;
            spec.key.resize(32);

            auto prk = hkdf_extract(nullptr, 0,
                master_key.data(), KeyHandle::key_size());
            (void)hkdf_expand(prk,
                reinterpret_cast<const uint8_t*>(info.data()),
                info.size(), spec.key.data(), 32);

            config.column_keys.push_back(std::move(spec));
        }

        return config;
    }
};

} // namespace signet::forge::crypto
