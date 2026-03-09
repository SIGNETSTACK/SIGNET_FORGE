// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file gzip.hpp
/// @brief GZIP compression codec for Signet Forge (wraps zlib).
///
/// Thin wrapper around zlib (deflate/inflate with gzip framing) conforming to
/// the CompressionCodec interface. Conditionally compiled when
/// @c SIGNET_HAS_GZIP is defined (set by CMake when zlib is found via
/// @c SIGNET_ENABLE_GZIP=ON).
///
/// The Parquet spec's GZIP codec uses standard gzip format (RFC 1952), which
/// is zlib's deflate with @c windowBits = 15 + 16 to enable the gzip header
/// and trailer.
///
/// @par Usage
/// @code
///   signet::forge::register_gzip_codec();   // once at startup
///   // Then use compress()/decompress() via the CodecRegistry automatically.
/// @endcode
///
/// @see CompressionCodec, CodecRegistry, register_gzip_codec()

#ifdef SIGNET_HAS_GZIP

#include "signet/compression/codec.hpp"

#include <zlib.h>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace signet::forge {

/// GZIP compression codec backed by zlib (deflate with gzip framing).
///
/// Uses @c deflateInit2() / @c inflateInit2() with @c windowBits = 15 + 16 to
/// produce and consume standard gzip streams (RFC 1952). The compression level
/// can be tuned at construction time.
///
/// @note This class is only available when @c SIGNET_HAS_GZIP is defined.
/// @see CompressionCodec, register_gzip_codec()
class GzipCodec : public CompressionCodec {
public:
    /// Construct a GZIP codec with the given zlib compression level.
    /// @param compression_level zlib compression level. Default is
    ///        @c Z_DEFAULT_COMPRESSION (-1), which lets zlib choose (typically
    ///        level 6). Valid range: 0 (store, no compression) to 9 (maximum
    ///        compression, slowest).
    explicit GzipCodec(int compression_level = Z_DEFAULT_COMPRESSION)
        : level_(compression_level) {}

    /// Compress data using gzip (deflate with gzip framing).
    ///
    /// Initializes a zlib deflate stream with @c windowBits = 15 + 16 and
    /// calls @c deflate(Z_FINISH) in a single pass. The output buffer is
    /// pre-allocated using @c deflateBound() for an accurate upper bound.
    ///
    /// @param data Pointer to the uncompressed input bytes.
    /// @param size Number of bytes to compress.
    /// @return The gzip-compressed byte buffer on success, or an Error if
    ///         zlib initialization or deflation fails.
    [[nodiscard]] expected<std::vector<uint8_t>> compress(
        const uint8_t* data, size_t size) const override {

        // Pessimistic upper bound: deflate worst case is ~0.1% expansion
        // plus gzip header/trailer (~18 bytes). deflateBound gives an
        // accurate upper bound once the stream is initialized, but we
        // need a reasonable initial allocation before that.
        // CWE-190: Integer Overflow (zlib uses uInt for sizes)
        if (size > std::numeric_limits<uInt>::max()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "GZIP: input exceeds uInt limit"};
        }

        z_stream stream{};
        stream.next_in  = const_cast<Bytef*>(data);
        stream.avail_in = static_cast<uInt>(size);

        // windowBits = 15 + 16 enables gzip format (not raw deflate).
        int ret = deflateInit2(&stream,
                               level_,
                               Z_DEFLATED,
                               15 + 16,   // gzip framing
                               8,         // default memLevel
                               Z_DEFAULT_STRATEGY);
        if (ret != Z_OK) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "GZIP: deflateInit2 failed (zlib error " +
                         std::to_string(ret) + ")"};
        }

        // Use deflateBound for an accurate output size estimate.
        uLong bound = deflateBound(&stream, static_cast<uLong>(size));
        std::vector<uint8_t> out(bound);

        stream.next_out  = out.data();
        stream.avail_out = static_cast<uInt>(out.size());

        ret = deflate(&stream, Z_FINISH);
        if (ret != Z_STREAM_END) {
            deflateEnd(&stream);
            return Error{ErrorCode::INTERNAL_ERROR,
                         "GZIP: deflate failed (zlib error " +
                         std::to_string(ret) + ")"};
        }

        size_t compressed_size = stream.total_out;
        deflateEnd(&stream);

        out.resize(compressed_size);
        return out;
    }

    /// Decompress gzip-compressed data back to the original bytes.
    ///
    /// Initializes a zlib inflate stream with @c windowBits = 15 + 16 and
    /// calls @c inflate(Z_FINISH) in a single pass. The output buffer is
    /// pre-allocated to @p uncompressed_size.
    ///
    /// @param data              Pointer to the gzip-compressed input bytes.
    /// @param size              Number of compressed bytes.
    /// @param uncompressed_size Expected decompressed size (from the Parquet
    ///                          page header). A mismatch with the actual
    ///                          inflated size is reported as corruption.
    /// @return The decompressed byte buffer, or an Error on failure.
    [[nodiscard]] expected<std::vector<uint8_t>> decompress(
        const uint8_t* data, size_t size,
        size_t uncompressed_size) const override {

        if (uncompressed_size == 0) {
            return std::vector<uint8_t>{};
        }

        // CWE-190: Integer Overflow (zlib uses uInt for sizes)
        if (size > std::numeric_limits<uInt>::max()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "GZIP: compressed input exceeds uInt limit"};
        }
        // CWE-190: Integer Overflow (zlib uses uInt for sizes)
        if (uncompressed_size > std::numeric_limits<uInt>::max()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "GZIP: uncompressed size exceeds uInt limit"};
        }

        std::vector<uint8_t> out(uncompressed_size);

        z_stream stream{};
        stream.next_in   = const_cast<Bytef*>(data);
        stream.avail_in  = static_cast<uInt>(size);
        stream.next_out  = out.data();
        stream.avail_out = static_cast<uInt>(out.size());

        // windowBits = 15 + 16 to accept gzip format.
        int ret = inflateInit2(&stream, 15 + 16);
        if (ret != Z_OK) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "GZIP: inflateInit2 failed (zlib error " +
                         std::to_string(ret) + ")"};
        }

        ret = inflate(&stream, Z_FINISH);
        if (ret != Z_STREAM_END) {
            inflateEnd(&stream);
            return Error{ErrorCode::CORRUPT_PAGE,
                         "GZIP: inflate failed (zlib error " +
                         std::to_string(ret) +
                         (stream.msg ? std::string(", ") + stream.msg
                                     : std::string()) + ")"};
        }

        size_t decompressed_size = stream.total_out;
        inflateEnd(&stream);

        if (decompressed_size != uncompressed_size) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "GZIP: decompressed " +
                         std::to_string(decompressed_size) +
                         " bytes but expected " +
                         std::to_string(uncompressed_size)};
        }

        return out;
    }

    /// @name Metadata
    /// @{

    /// Return Compression::GZIP.
    [[nodiscard]] Compression codec_type() const override {
        return Compression::GZIP;
    }

    /// Return the codec name "gzip".
    [[nodiscard]] const char* name() const override {
        return "gzip";
    }

    /// @}

private:
    /// zlib compression level used by compress().
    int level_;
};

// ===========================================================================
// Auto-registration helper
// ===========================================================================

/// Register the GZIP codec with the global CodecRegistry.
///
/// Call this once at startup to make Compression::GZIP available through
/// compress() and decompress().
///
/// @param level zlib compression level (default @c Z_DEFAULT_COMPRESSION).
///              Passed to the GzipCodec constructor.
/// @see GzipCodec, CodecRegistry::register_codec()
inline void register_gzip_codec(int level = Z_DEFAULT_COMPRESSION) {
    CodecRegistry::instance().register_codec(std::make_unique<GzipCodec>(level));
}

} // namespace signet::forge

#endif // SIGNET_HAS_GZIP
