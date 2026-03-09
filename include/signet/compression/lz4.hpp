// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file lz4.hpp
/// @brief LZ4 raw-block compression codec for Signet Forge (wraps liblz4).
///
/// Thin wrapper around liblz4 providing LZ4 raw block compression and
/// decompression conforming to the CompressionCodec interface. Conditionally
/// compiled when @c SIGNET_HAS_LZ4 is defined (set by CMake when the lz4
/// dependency is found via @c SIGNET_ENABLE_LZ4=ON).
///
/// @par Parquet LZ4 variants
/// The Parquet spec defines two LZ4 enum values:
///   - @c Compression::LZ4     (value 5) -- legacy, Hadoop-compatible, sometimes framed
///   - @c Compression::LZ4_RAW (value 7) -- raw block format (preferred for modern Parquet)
///
/// This codec implements **LZ4_RAW** (raw block format), which is the standard
/// for modern Parquet files.
///
/// @par Usage
/// @code
///   signet::forge::register_lz4_codec();   // once at startup
///   // Then use compress()/decompress() via the CodecRegistry automatically.
/// @endcode
///
/// @see CompressionCodec, CodecRegistry, register_lz4_codec()

#ifdef SIGNET_HAS_LZ4

#include "signet/compression/codec.hpp"

#include <lz4.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace signet::forge {

/// LZ4 raw block compression codec backed by liblz4.
///
/// Uses @c LZ4_compress_default() for compression and @c LZ4_decompress_safe()
/// for decompression. The raw block format does not include framing or checksums;
/// integrity is ensured by the Parquet page CRC.
///
/// @note This class is only available when @c SIGNET_HAS_LZ4 is defined.
/// @see CompressionCodec, register_lz4_codec()
class Lz4RawCodec : public CompressionCodec {
public:
    /// Compress data using LZ4 raw block compression.
    ///
    /// @param data Pointer to the uncompressed input bytes.
    /// @param size Number of bytes to compress. Must fit in an @c int
    ///             (liblz4 uses int-sized lengths).
    /// @return The LZ4-compressed byte buffer on success, or an Error if the
    ///         input is too large for liblz4 or compression fails.
    [[nodiscard]] expected<std::vector<uint8_t>> compress(
        const uint8_t* data, size_t size) const override {

        if (size == 0) {
            return std::vector<uint8_t>{};
        }

        // CWE-190: Integer Overflow (liblz4 uses int for sizes)
        if (size > static_cast<size_t>((std::numeric_limits<int>::max)())) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "LZ4: input exceeds int32 limit"};
        }

        // LZ4_compressBound returns the maximum compressed size.
        int max_compressed = LZ4_compressBound(static_cast<int>(size));
        if (max_compressed <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "LZ4: input too large for LZ4_compressBound"};
        }

        std::vector<uint8_t> out(static_cast<size_t>(max_compressed));

        int compressed_size = LZ4_compress_default(
            reinterpret_cast<const char*>(data),
            reinterpret_cast<char*>(out.data()),
            static_cast<int>(size),
            max_compressed);

        if (compressed_size <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "LZ4 compress failed (returned " +
                         std::to_string(compressed_size) + ")"};
        }

        out.resize(static_cast<size_t>(compressed_size));
        return out;
    }

    /// Decompress LZ4 raw block data back to the original bytes.
    ///
    /// @param data              Pointer to the LZ4-compressed input bytes.
    /// @param size              Number of compressed bytes.
    /// @param uncompressed_size Expected decompressed size (from the Parquet
    ///                          page header). A mismatch with the actual
    ///                          decompressed output is reported as corruption.
    /// @return The decompressed byte buffer, or an Error on failure.
    [[nodiscard]] expected<std::vector<uint8_t>> decompress(
        const uint8_t* data, size_t size,
        size_t uncompressed_size) const override {

        if (uncompressed_size == 0) {
            return std::vector<uint8_t>{};
        }

        // CWE-190: Integer Overflow (liblz4 uses int for sizes)
        if (size > static_cast<size_t>((std::numeric_limits<int>::max)())) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "LZ4: compressed input exceeds int32 limit"};
        }
        // CWE-190: Integer Overflow (liblz4 uses int for sizes)
        if (uncompressed_size > static_cast<size_t>((std::numeric_limits<int>::max)())) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "LZ4: uncompressed size exceeds int32 limit"};
        }

        std::vector<uint8_t> out(uncompressed_size);

        int decompressed_size = LZ4_decompress_safe(
            reinterpret_cast<const char*>(data),
            reinterpret_cast<char*>(out.data()),
            static_cast<int>(size),
            static_cast<int>(uncompressed_size));

        if (decompressed_size < 0) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "LZ4 decompress failed (returned " +
                         std::to_string(decompressed_size) + ")"};
        }

        if (static_cast<size_t>(decompressed_size) != uncompressed_size) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "LZ4: decompressed " +
                         std::to_string(decompressed_size) +
                         " bytes but expected " +
                         std::to_string(uncompressed_size)};
        }

        return out;
    }

    /// @name Metadata
    /// @{

    /// Return Compression::LZ4_RAW.
    [[nodiscard]] Compression codec_type() const override {
        return Compression::LZ4_RAW;
    }

    /// Return the codec name "lz4_raw".
    [[nodiscard]] const char* name() const override {
        return "lz4_raw";
    }

    /// @}
};

// ===========================================================================
// Auto-registration helper
// ===========================================================================

/// Register the LZ4 raw block codec with the global CodecRegistry.
///
/// Call this once at startup to make Compression::LZ4_RAW available through
/// compress() and decompress().
///
/// @see Lz4RawCodec, CodecRegistry::register_codec()
inline void register_lz4_codec() {
    CodecRegistry::instance().register_codec(std::make_unique<Lz4RawCodec>());
}

} // namespace signet::forge

#endif // SIGNET_HAS_LZ4
