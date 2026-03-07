// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file zstd.hpp
/// @brief ZSTD compression codec for Signet Forge (wraps libzstd).
///
/// Thin wrapper around libzstd providing Zstandard compression and
/// decompression conforming to the CompressionCodec interface. Conditionally
/// compiled when @c SIGNET_HAS_ZSTD is defined (set by CMake when the zstd
/// dependency is found via @c SIGNET_ENABLE_ZSTD=ON).
///
/// @par Usage
/// @code
///   signet::forge::register_zstd_codec();   // once at startup
///   // Then use compress()/decompress() via the CodecRegistry automatically.
/// @endcode
///
/// @see CompressionCodec, CodecRegistry, register_zstd_codec()

#ifdef SIGNET_HAS_ZSTD

#include "signet/compression/codec.hpp"

#include <zstd.h>

#include <cstdint>
#include <string>
#include <vector>

namespace signet::forge {

/// ZSTD (Zstandard) compression codec backed by libzstd.
///
/// ZSTD typically offers the best compression ratio among Parquet-supported
/// codecs, making it the top preference for auto_select_compression(). The
/// compression level can be tuned at construction time.
///
/// @note This class is only available when @c SIGNET_HAS_ZSTD is defined.
/// @see CompressionCodec, register_zstd_codec(), auto_select_compression()
class ZstdCodec : public CompressionCodec {
public:
    /// Construct a ZSTD codec with the given compression level.
    /// @param compression_level ZSTD compression level. Default is 3.
    ///        Valid range: @c ZSTD_minCLevel() (typically -131072) to
    ///        @c ZSTD_maxCLevel() (typically 22). Higher values yield better
    ///        compression at the cost of speed.
    explicit ZstdCodec(int compression_level = 3)
        : level_(compression_level) {}

    /// Compress data using the ZSTD algorithm.
    ///
    /// @param data Pointer to the uncompressed input bytes.
    /// @param size Number of bytes to compress.
    /// @return The ZSTD-compressed byte buffer on success, or an Error if
    ///         libzstd reports a failure.
    [[nodiscard]] expected<std::vector<uint8_t>> compress(
        const uint8_t* data, size_t size) const override {

        size_t bound = ZSTD_compressBound(size);
        std::vector<uint8_t> out(bound);

        size_t compressed_size = ZSTD_compress(
            out.data(), out.size(),
            data, size,
            level_);

        if (ZSTD_isError(compressed_size)) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         std::string("ZSTD compress failed: ") +
                         ZSTD_getErrorName(compressed_size)};
        }

        out.resize(compressed_size);
        return out;
    }

    /// Decompress ZSTD-compressed data back to raw bytes.
    ///
    /// @param data              Pointer to the ZSTD-compressed input bytes.
    /// @param size              Number of compressed bytes.
    /// @param uncompressed_size Expected decompressed size (from the Parquet
    ///                          page header). A mismatch with the actual
    ///                          decompressed size is reported as corruption.
    /// @return The decompressed byte buffer, or an Error on failure.
    [[nodiscard]] expected<std::vector<uint8_t>> decompress(
        const uint8_t* data, size_t size,
        size_t uncompressed_size) const override {

        std::vector<uint8_t> out(uncompressed_size);

        size_t decompressed_size = ZSTD_decompress(
            out.data(), out.size(),
            data, size);

        if (ZSTD_isError(decompressed_size)) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         std::string("ZSTD decompress failed: ") +
                         ZSTD_getErrorName(decompressed_size)};
        }

        if (decompressed_size != uncompressed_size) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "ZSTD: decompressed " +
                         std::to_string(decompressed_size) +
                         " bytes but expected " +
                         std::to_string(uncompressed_size)};
        }

        return out;
    }

    /// @name Metadata
    /// @{

    /// Return Compression::ZSTD.
    [[nodiscard]] Compression codec_type() const override {
        return Compression::ZSTD;
    }

    /// Return the codec name "zstd".
    [[nodiscard]] const char* name() const override {
        return "zstd";
    }

    /// @}

private:
    /// ZSTD compression level used by compress().
    int level_;
};

// ===========================================================================
// Auto-registration helper
// ===========================================================================

/// Register the ZSTD codec with the global CodecRegistry.
///
/// Call this once at startup to make Compression::ZSTD available through
/// compress() and decompress().
///
/// @param level ZSTD compression level (default 3). Passed to the ZstdCodec
///              constructor.
/// @see ZstdCodec, CodecRegistry::register_codec()
inline void register_zstd_codec(int level = 3) {
    CodecRegistry::instance().register_codec(std::make_unique<ZstdCodec>(level));
}

} // namespace signet::forge

#endif // SIGNET_HAS_ZSTD
