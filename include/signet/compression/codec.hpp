// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file codec.hpp
/// @brief Compression codec interface and registry for Signet Forge.
///
/// Provides:
///   - CompressionCodec:  Abstract base class for all compression codecs
///   - CodecRegistry:     Thread-safe singleton registry of codec implementations
///   - compress() / decompress():  Convenience functions using the registry
///   - auto_select_compression():  Heuristic codec selection based on availability
///
/// Concrete codec implementations (Snappy, ZSTD, LZ4, GZIP) register themselves
/// via CodecRegistry::instance().register_codec() at startup. UNCOMPRESSED is
/// handled inline without a registered codec.
///
/// @see snappy.hpp, zstd.hpp, lz4.hpp, gzip.hpp

#include "signet/types.hpp"
#include "signet/error.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace signet::forge {

// ===========================================================================
// CompressionCodec — abstract interface for compression/decompression
// ===========================================================================

/// Abstract base class for all compression/decompression codecs.
///
/// Each concrete codec (Snappy, ZSTD, LZ4, GZIP) derives from this class
/// and registers itself with the CodecRegistry at startup. The codec is
/// stateless and all methods are const-qualified, making instances safe to
/// share across threads.
///
/// @see CodecRegistry, compress(), decompress()
class CompressionCodec {
public:
    /// Virtual destructor for safe polymorphic deletion.
    virtual ~CompressionCodec() = default;

    /// Compress raw data into codec-specific format.
    /// @param data Pointer to the uncompressed input bytes.
    /// @param size Number of bytes to compress.
    /// @return The compressed byte buffer on success, or an Error on failure.
    [[nodiscard]] virtual expected<std::vector<uint8_t>> compress(
        const uint8_t* data, size_t size) const = 0;

    /// Decompress codec-specific data back to raw bytes.
    /// @param data Pointer to the compressed input bytes.
    /// @param size Number of compressed bytes.
    /// @param uncompressed_size Expected output size (from the Parquet page header).
    /// @return The decompressed byte buffer on success, or an Error on failure.
    /// @note The caller must supply the correct @p uncompressed_size; a mismatch
    ///       between actual and expected sizes is treated as corruption.
    [[nodiscard]] virtual expected<std::vector<uint8_t>> decompress(
        const uint8_t* data, size_t size, size_t uncompressed_size) const = 0;

    /// Return the Parquet Compression enum value identifying this codec.
    /// @return The Compression enumerator (e.g. Compression::ZSTD).
    [[nodiscard]] virtual Compression codec_type() const = 0;

    /// Return a human-readable codec name (e.g. "ZSTD", "snappy", "lz4_raw").
    /// @return Null-terminated string with the codec name. The pointer is valid
    ///         for the lifetime of the codec instance.
    [[nodiscard]] virtual const char* name() const = 0;
};

// ===========================================================================
// CodecRegistry — singleton holding all registered compression codecs
// ===========================================================================

/// Thread-safe singleton registry of compression codec implementations.
///
/// Codecs can be registered from static initializers or at runtime from any
/// thread. All look-up and mutation operations are guarded by an internal mutex.
/// UNCOMPRESSED is handled inline by the convenience functions and is never
/// stored in the registry itself.
///
/// @note Non-copyable and non-movable (singleton pattern).
/// @see CompressionCodec, compress(), decompress(), auto_select_compression()
class CodecRegistry {
public:
    /// Access the process-wide singleton instance.
    /// @return Reference to the CodecRegistry singleton (Meyers singleton).
    static CodecRegistry& instance() {
        static CodecRegistry registry;
        return registry;
    }

    /// Register a codec, transferring ownership to the registry.
    ///
    /// If a codec for the same Compression type is already registered, the
    /// previous one is replaced and destroyed.
    /// @param codec Owning pointer to the codec implementation. Null pointers
    ///              are silently ignored.
    void register_codec(std::unique_ptr<CompressionCodec> codec) {
        if (!codec) return;
        std::lock_guard<std::mutex> lock(mutex_);
        Compression type = codec->codec_type();
        codecs_[type] = std::move(codec);
    }

    /// Look up a registered codec by its Compression type.
    /// @param type The Compression enumerator to look up.
    /// @return Non-owning pointer to the codec, or @c nullptr if not registered.
    /// @note UNCOMPRESSED is not stored in the registry; use the free-standing
    ///       compress()/decompress() functions which handle it inline.
    [[nodiscard]] const CompressionCodec* get(Compression type) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = codecs_.find(type);
        if (it != codecs_.end()) {
            return it->second.get();
        }
        return nullptr;
    }

    /// Check whether a codec is available for the given compression type.
    /// @param type The Compression enumerator to query.
    /// @return @c true if the codec is registered, or if @p type is UNCOMPRESSED
    ///         (which is always available).
    [[nodiscard]] bool has(Compression type) const {
        if (type == Compression::UNCOMPRESSED) return true;
        std::lock_guard<std::mutex> lock(mutex_);
        return codecs_.find(type) != codecs_.end();
    }

    /// List all available compression types, including UNCOMPRESSED.
    /// @return A vector containing Compression::UNCOMPRESSED followed by
    ///         every registered codec type, in unspecified order.
    [[nodiscard]] std::vector<Compression> available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Compression> result;
        result.reserve(codecs_.size() + 1);
        result.push_back(Compression::UNCOMPRESSED);
        for (const auto& [type, _] : codecs_) {
            result.push_back(type);
        }
        return result;
    }

    /// @name Deleted copy/move operations (singleton)
    /// @{
    CodecRegistry(const CodecRegistry&) = delete;
    CodecRegistry& operator=(const CodecRegistry&) = delete;
    CodecRegistry(CodecRegistry&&) = delete;
    CodecRegistry& operator=(CodecRegistry&&) = delete;
    /// @}

private:
    /// Default constructor (private; use instance() to access).
    CodecRegistry() = default;

    /// Mutex guarding all accesses to codecs_.
    mutable std::mutex mutex_;
    /// Map from Compression enum to owning codec pointer.
    std::unordered_map<Compression, std::unique_ptr<CompressionCodec>> codecs_;
};

// ===========================================================================
// Convenience functions — compress / decompress via the registry
// ===========================================================================

/// Compress data using the specified codec via the global CodecRegistry.
///
/// For Compression::UNCOMPRESSED, returns a verbatim copy of the input without
/// consulting the registry.
///
/// @param codec The compression type to use.
/// @param data  Pointer to the raw input bytes.
/// @param size  Number of bytes to compress.
/// @return The compressed byte buffer, or an Error if the codec is not
///         registered or compression fails.
/// @see decompress(), auto_select_compression()
[[nodiscard]] inline expected<std::vector<uint8_t>> compress(
    Compression codec, const uint8_t* data, size_t size) {

    // UNCOMPRESSED: return a copy of the data
    if (codec == Compression::UNCOMPRESSED) {
        return std::vector<uint8_t>(data, data + size);
    }

    const CompressionCodec* impl = CodecRegistry::instance().get(codec);
    if (!impl) {
        return Error{ErrorCode::UNSUPPORTED_COMPRESSION,
                     "compression codec not registered"};
    }

    return impl->compress(data, size);
}

/// Decompress data using the specified codec via the global CodecRegistry.
///
/// For Compression::UNCOMPRESSED, returns a verbatim copy of the input without
/// consulting the registry.
///
/// @param codec             The compression type that was used to compress the data.
/// @param data              Pointer to the compressed input bytes.
/// @param size              Number of compressed bytes.
/// @param uncompressed_size Expected size of the decompressed output (from the
///                          Parquet page header).
/// @return The decompressed byte buffer, or an Error if the codec is not
///         registered or decompression fails.
/// @see compress(), auto_select_compression()
[[nodiscard]] inline expected<std::vector<uint8_t>> decompress(
    Compression codec, const uint8_t* data, size_t size,
    size_t uncompressed_size) {

    // UNCOMPRESSED: return a copy of the data
    if (codec == Compression::UNCOMPRESSED) {
        return std::vector<uint8_t>(data, data + size);
    }

    // Absolute decompressed size cap (256 MB)
    // CWE-409: Improper Handling of Highly Compressed Data (Decompression Bomb)
    static constexpr size_t MAX_DECOMPRESS_SIZE = 256ULL * 1024 * 1024;
    if (uncompressed_size > MAX_DECOMPRESS_SIZE) {
        return Error{ErrorCode::CORRUPT_PAGE,
                     "Decompressed size exceeds 256 MB limit"};
    }
    // Reject zero-length compressed data claiming non-zero output
    // CWE-20: Improper Input Validation
    if (size == 0 && uncompressed_size > 0) {
        return Error{ErrorCode::CORRUPT_PAGE,
                     "Zero-length compressed data with non-zero uncompressed size"};
    }
    // Decompression bomb guard: ratio > 1024:1 is suspicious
    // CWE-409: Improper Handling of Highly Compressed Data (Decompression Bomb)
    static constexpr size_t MAX_DECOMPRESSION_RATIO = 1024;
    if (size > 0 && uncompressed_size / size > MAX_DECOMPRESSION_RATIO) {
        return Error{ErrorCode::CORRUPT_PAGE,
                     "Decompression ratio exceeds limit"};
    }

    const CompressionCodec* impl = CodecRegistry::instance().get(codec);
    if (!impl) {
        return Error{ErrorCode::UNSUPPORTED_COMPRESSION,
                     "compression codec not registered"};
    }

    return impl->decompress(data, size, uncompressed_size);
}

// ===========================================================================
// auto_select_compression — heuristic codec selection
// ===========================================================================

/// Automatically select the best available compression codec.
///
/// Priority order: ZSTD > Snappy > LZ4_RAW > LZ4 > UNCOMPRESSED.
///
/// - ZSTD gives the best compression ratio for most Parquet workloads.
/// - Snappy is the fastest but has moderate compression.
/// - LZ4 is a good middle ground between speed and ratio.
///
/// @param sample_data Reserved for future data-aware heuristics (e.g. entropy
///                    estimation). Currently unused.
/// @param sample_size Reserved for future use. Currently unused.
/// @return The Compression enumerator for the highest-priority codec that is
///         registered, or Compression::UNCOMPRESSED if none are available.
/// @see CodecRegistry::has(), compress()
[[nodiscard]] inline Compression auto_select_compression(
    [[maybe_unused]] const uint8_t* sample_data,
    [[maybe_unused]] size_t sample_size) {

    const auto& registry = CodecRegistry::instance();

    // Prefer ZSTD for best compression ratio
    if (registry.has(Compression::ZSTD)) {
        return Compression::ZSTD;
    }

    // Snappy: fast compression, moderate ratio
    if (registry.has(Compression::SNAPPY)) {
        return Compression::SNAPPY;
    }

    // LZ4 (raw): fast, decent ratio
    if (registry.has(Compression::LZ4_RAW)) {
        return Compression::LZ4_RAW;
    }

    // LZ4 (framed, legacy enum value)
    if (registry.has(Compression::LZ4)) {
        return Compression::LZ4;
    }

    // No compression codecs available
    return Compression::UNCOMPRESSED;
}

} // namespace signet::forge
