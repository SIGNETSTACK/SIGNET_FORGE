// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji

/// @file byte_stream_split.hpp
/// @brief BYTE_STREAM_SPLIT encoding and decoding (Parquet encoding type 9).
///
/// Splits IEEE 754 float/double values by byte position to group similar
/// exponent and mantissa bits together. This dramatically improves compression
/// ratios with ZSTD/Snappy/LZ4 for financial data (prices, rates, quantities)
/// where successive values share exponent bytes. All functions reside in the
/// @c signet::forge::byte_stream_split namespace.
///
/// @see https://parquet.apache.org/documentation/latest/ (BYTE_STREAM_SPLIT)

#pragma once

// ---------------------------------------------------------------------------
// byte_stream_split.hpp -- BYTE_STREAM_SPLIT encoding (Parquet encoding=9)
//
// Splits IEEE 754 float/double values by byte position to group similar
// exponent and mantissa bits together. This dramatically improves
// compression ratios with ZSTD/Snappy for financial data (prices, rates,
// quantities) where successive values share exponent bytes.
//
// Layout for N float values (4 bytes each):
//   [byte0 of val0][byte0 of val1]...[byte0 of valN-1]   (N bytes)
//   [byte1 of val0][byte1 of val1]...[byte1 of valN-1]   (N bytes)
//   [byte2 of val0][byte2 of val1]...[byte2 of valN-1]   (N bytes)
//   [byte3 of val0][byte3 of val1]...[byte3 of valN-1]   (N bytes)
//   Total: 4*N bytes (same as input, just rearranged)
//
// Layout for N double values (8 bytes each):
//   Same pattern but 8 byte streams instead of 4.
//   Total: 8*N bytes.
//
// Decoding reverses the process: de-interleave back to native byte order.
// ---------------------------------------------------------------------------

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace signet::forge {

static_assert(std::endian::native == std::endian::little,
              "Byte Stream Split encoding requires little-endian platform");

/// @brief BYTE_STREAM_SPLIT encoding functions for float and double types.
namespace byte_stream_split {

// ===========================================================================
// Encode
// ===========================================================================

/// Encode float values using the BYTE_STREAM_SPLIT algorithm.
///
/// Splits each 4-byte IEEE 754 float by byte position, producing 4 contiguous
/// byte streams: all byte-0s, all byte-1s, all byte-2s, all byte-3s. The
/// output size is always `4 * count` bytes (identical to the input size, but
/// rearranged for better compressibility).
///
/// @param values  Pointer to @p count float values.
/// @param count   Number of float values to encode.
/// @return        Encoded byte buffer of size `4 * count`.
/// @see decode_float
[[nodiscard]] inline std::vector<uint8_t> encode_float(const float* values,
                                                        size_t count) {
    constexpr size_t WIDTH = sizeof(float); // 4
    if (count > SIZE_MAX / WIDTH) return {}; // CWE-190: Integer Overflow — prevent count * WIDTH wraparound
    std::vector<uint8_t> out(count * WIDTH);

    if (count == 0) return out;

    // Reinterpret the float array as raw bytes
    const auto* src = reinterpret_cast<const uint8_t*>(values);

    // For each byte position b in [0,4), copy byte b of every value
    // into the output at offset b*count
    for (size_t b = 0; b < WIDTH; ++b) {
        uint8_t* dst = out.data() + b * count;
        for (size_t i = 0; i < count; ++i) {
            dst[i] = src[i * WIDTH + b];
        }
    }

    return out;
}

/// Encode double values using the BYTE_STREAM_SPLIT algorithm.
///
/// Splits each 8-byte IEEE 754 double by byte position, producing 8 contiguous
/// byte streams. The output size is always `8 * count` bytes. Particularly
/// effective for financial time-series data where exponent bytes are highly
/// repetitive across successive values.
///
/// @param values  Pointer to @p count double values.
/// @param count   Number of double values to encode.
/// @return        Encoded byte buffer of size `8 * count`.
/// @see decode_double
[[nodiscard]] inline std::vector<uint8_t> encode_double(const double* values,
                                                         size_t count) {
    constexpr size_t WIDTH = sizeof(double); // 8
    if (count > SIZE_MAX / WIDTH) return {}; // CWE-190: Integer Overflow — prevent count * WIDTH wraparound
    std::vector<uint8_t> out(count * WIDTH);

    if (count == 0) return out;

    const auto* src = reinterpret_cast<const uint8_t*>(values);

    for (size_t b = 0; b < WIDTH; ++b) {
        uint8_t* dst = out.data() + b * count;
        for (size_t i = 0; i < count; ++i) {
            dst[i] = src[i * WIDTH + b];
        }
    }

    return out;
}

// ===========================================================================
// Decode
// ===========================================================================

/// Decode float values from BYTE_STREAM_SPLIT encoding.
///
/// Reverses encode_float(): reassembles original IEEE 754 floats from 4
/// byte-position streams. Returns an empty vector if @p size is insufficient
/// to hold @p count float values.
///
/// @param data   Pointer to the encoded data: [all byte0s][byte1s][byte2s][byte3s].
/// @param size   Size of the encoded data in bytes (must be >= @p count * 4).
/// @param count  Number of float values to decode.
/// @return       Decoded float values, or empty on insufficient data.
/// @see encode_float
[[nodiscard]] inline std::vector<float> decode_float(const uint8_t* data,
                                                      size_t size,
                                                      size_t count) {
    constexpr size_t WIDTH = sizeof(float); // 4
    if (count > SIZE_MAX / WIDTH || count * WIDTH > size) return {};

    std::vector<float> out(count);

    if (count == 0) return out;

    auto* dst = reinterpret_cast<uint8_t*>(out.data());

    // Reverse the split: for each byte position b, read from data[b*count+i]
    // and write to dst[i*WIDTH+b]
    for (size_t b = 0; b < WIDTH; ++b) {
        const uint8_t* src = data + b * count;
        for (size_t i = 0; i < count; ++i) {
            dst[i * WIDTH + b] = src[i];
        }
    }

    return out;
}

/// Decode double values from BYTE_STREAM_SPLIT encoding.
///
/// Reverses encode_double(): reassembles original IEEE 754 doubles from 8
/// byte-position streams. Returns an empty vector if @p size is insufficient
/// to hold @p count double values.
///
/// @param data   Pointer to the encoded data: [all byte0s]...[all byte7s].
/// @param size   Size of the encoded data in bytes (must be >= @p count * 8).
/// @param count  Number of double values to decode.
/// @return       Decoded double values, or empty on insufficient data.
/// @see encode_double
[[nodiscard]] inline std::vector<double> decode_double(const uint8_t* data,
                                                        size_t size,
                                                        size_t count) {
    constexpr size_t WIDTH = sizeof(double); // 8
    if (count > SIZE_MAX / WIDTH || count * WIDTH > size) return {};

    std::vector<double> out(count);

    if (count == 0) return out;

    auto* dst = reinterpret_cast<uint8_t*>(out.data());

    for (size_t b = 0; b < WIDTH; ++b) {
        const uint8_t* src = data + b * count;
        for (size_t i = 0; i < count; ++i) {
            dst[i * WIDTH + b] = src[i];
        }
    }

    return out;
}

} // namespace byte_stream_split
} // namespace signet::forge
