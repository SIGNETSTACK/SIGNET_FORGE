// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji

/// @file delta.hpp
/// @brief DELTA_BINARY_PACKED encoding and decoding (Parquet encoding type 5).
///
/// Delta-encodes int32/int64 values for high compression on sorted or monotonic
/// sequences (timestamps, sequence IDs, etc.). Achieves 90%+ compression on
/// sorted time-series data. All functions reside in the @c signet::forge::delta
/// namespace.
///
/// @see https://parquet.apache.org/documentation/latest/ (DELTA_BINARY_PACKED)

#pragma once

// ---------------------------------------------------------------------------
// delta.hpp -- DELTA_BINARY_PACKED encoding (Parquet encoding=5)
//
// Delta-encodes int32/int64 values for high compression on sorted or
// monotonic sequences (timestamps, sequence IDs, etc.). Can achieve 90%+
// compression on sorted time-series data.
//
// Wire format:
//
// 1. Header (all unsigned varints, first_value is zigzag-encoded):
//    - block_size:        values per block (must be multiple of 128)
//    - miniblock_count:   miniblocks per block
//    - total_value_count: total number of values encoded
//    - first_value:       zigzag-encoded first value
//
// 2. Per block:
//    - min_delta:   zigzag-encoded minimum delta in this block
//    - bit_widths:  one byte per miniblock (bit width for that miniblock)
//    - miniblocks:  each miniblock contains (block_size / miniblock_count)
//                   values, bit-packed at the specified width. Stored values
//                   are (delta - min_delta), always non-negative.
//
// Zigzag encoding maps signed integers to unsigned:
//    encode: (n << 1) ^ (n >> 63)  for int64
//    decode: (v >> 1) ^ -(v & 1)
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace signet::forge {
namespace delta {

// ---------------------------------------------------------------------------
// Constants -- tuned for the Parquet default configuration
// ---------------------------------------------------------------------------

/// Default number of delta values per block (must be a multiple of 128).
inline constexpr size_t DEFAULT_BLOCK_SIZE      = 128;

/// Default number of miniblocks within each block.
inline constexpr size_t DEFAULT_MINIBLOCK_COUNT = 4;

/// Number of delta values per miniblock (DEFAULT_BLOCK_SIZE / DEFAULT_MINIBLOCK_COUNT).
inline constexpr size_t VALUES_PER_MINIBLOCK    = DEFAULT_BLOCK_SIZE / DEFAULT_MINIBLOCK_COUNT;

// ---------------------------------------------------------------------------
// Zigzag encoding/decoding
// ---------------------------------------------------------------------------

/// Zigzag-encode a signed 64-bit integer to an unsigned representation.
///
/// Maps signed integers to unsigned using the formula `(n << 1) ^ (n >> 63)`,
/// so that small-magnitude values (positive or negative) map to small unsigned
/// values. This is critical for varint efficiency.
///
/// @param n  The signed 64-bit integer to encode.
/// @return   The zigzag-encoded unsigned 64-bit value.
/// @see zigzag_decode
[[nodiscard]] inline uint64_t zigzag_encode(int64_t n) {
    // Cast to unsigned before left shift to avoid signed overflow UB (CWE-190)
    return (static_cast<uint64_t>(n) << 1) ^ static_cast<uint64_t>(n >> 63);
}

/// Zigzag-encode a signed 32-bit integer to an unsigned representation.
///
/// 32-bit variant of zigzag_encode(). Uses the formula `(n << 1) ^ (n >> 31)`.
///
/// @param n  The signed 32-bit integer to encode.
/// @return   The zigzag-encoded unsigned 32-bit value.
/// @see zigzag_decode32
[[nodiscard]] inline uint32_t zigzag_encode32(int32_t n) {
    return (static_cast<uint32_t>(n) << 1) ^ static_cast<uint32_t>(n >> 31);
}

/// Zigzag-decode an unsigned 64-bit integer back to its signed representation.
///
/// Reverses zigzag_encode(): `(v >> 1) ^ (~(v & 1) + 1)`.
///
/// @param v  The zigzag-encoded unsigned 64-bit value.
/// @return   The original signed 64-bit integer.
/// @see zigzag_encode
[[nodiscard]] inline int64_t zigzag_decode(uint64_t v) {
    return static_cast<int64_t>((v >> 1) ^ (~(v & 1) + 1));
}

/// Zigzag-decode an unsigned 32-bit integer back to its signed representation.
///
/// 32-bit variant of zigzag_decode(). Uses `(v >> 1) ^ (~(v & 1) + 1)`.
///
/// @param v  The zigzag-encoded unsigned 32-bit value.
/// @return   The original signed 32-bit integer.
/// @see zigzag_encode32
[[nodiscard]] inline int32_t zigzag_decode32(uint32_t v) {
    return static_cast<int32_t>((v >> 1) ^ (~(v & 1) + 1));
}

// ---------------------------------------------------------------------------
// Unsigned varint encoding/decoding (LEB128, same as Thrift compact protocol)
// ---------------------------------------------------------------------------

/// Encode an unsigned varint (LEB128) into a byte buffer.
///
/// Appends the variable-length encoding of @p value to @p buf, using the same
/// unsigned LEB128 format as the Thrift compact protocol. Each byte uses 7 data
/// bits and 1 continuation bit (MSB).
///
/// @param buf    Output byte buffer to append to.
/// @param value  The unsigned integer to encode.
/// @return       Number of bytes written (1--10).
/// @see decode_uvarint
inline size_t encode_uvarint(std::vector<uint8_t>& buf, uint64_t value) {
    size_t start = buf.size();
    while (value >= 0x80) {
        buf.push_back(static_cast<uint8_t>(value & 0x7F) | 0x80);
        value >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(value));
    return buf.size() - start;
}

/// Decode an unsigned varint (LEB128) from a byte buffer.
///
/// Reads a variable-length encoded unsigned integer starting at @p data[pos].
/// Advances @p pos past the consumed bytes. Returns 0 if the buffer is
/// exhausted or the shift exceeds 63 bits (overflow protection).
///
/// @param data  Pointer to the encoded byte stream.
/// @param pos   Current read position (updated on return).
/// @param size  Total size of the byte stream.
/// @return      The decoded unsigned integer, or 0 on failure.
/// @see encode_uvarint
[[nodiscard]] inline uint64_t decode_uvarint(const uint8_t* data, size_t& pos, size_t size) {
    const size_t start_pos = pos;
    uint64_t result = 0;
    int shift = 0;
    while (pos < size) {
        uint8_t byte = data[pos++];
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return result;
        }
        shift += 7;
        if (shift >= 64) { pos = start_pos; break; } // overflow protection
    }
    pos = start_pos;
    return result;
}

// ---------------------------------------------------------------------------
// Bit width computation
// ---------------------------------------------------------------------------

/// Compute the minimum number of bits required to represent an unsigned value.
///
/// Equivalent to `ceil(log2(value + 1))`. Returns 0 for @p value == 0.
///
/// @param value  The unsigned integer whose bit width to compute.
/// @return       Minimum bits needed (0--64).
[[nodiscard]] inline int bit_width_for(uint64_t value) {
    if (value == 0) return 0;
    int width = 0;
    while (value > 0) {
        value >>= 1;
        ++width;
    }
    return width;
}

// ---------------------------------------------------------------------------
// Bit-packing helpers for miniblocks
// ---------------------------------------------------------------------------

/// Bit-pack an arbitrary number of values at a given bit width.
///
/// Appends `ceil(count * bit_width / 8)` bytes to @p out, packing each value
/// LSB-first. Typically used to pack one miniblock of VALUES_PER_MINIBLOCK (32)
/// delta-adjusted values. If @p bit_width is 0, no bytes are emitted.
///
/// @param out        Output byte buffer to append packed data to.
/// @param values     Pointer to @p count unsigned values to pack.
/// @param count      Number of values (typically 32 for a miniblock).
/// @param bit_width  Bits per value (0--64).
/// @see bit_unpack_values
inline void bit_pack_values(std::vector<uint8_t>& out,
                            const uint64_t* values, size_t count,
                            int bit_width) {
    if (bit_width == 0) return; // all values are zero, no bytes needed

    // Total bytes = ceil(count * bit_width / 8)
    size_t total_bits = count * static_cast<size_t>(bit_width);
    size_t total_bytes = (total_bits + 7) / 8;

    size_t start = out.size();
    out.resize(start + total_bytes, 0);
    uint8_t* dst = out.data() + start;

    size_t bit_offset = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t val = values[i];
        int bits_remaining = bit_width;
        size_t cur_bit = bit_offset;
        while (bits_remaining > 0) {
            size_t byte_idx = cur_bit / 8;
            int bit_idx = static_cast<int>(cur_bit % 8);
            int bits_to_write = (std::min)(bits_remaining, 8 - bit_idx);
            uint8_t mask = static_cast<uint8_t>(
                (val & ((uint64_t{1} << bits_to_write) - 1)) << bit_idx);
            dst[byte_idx] |= mask;
            val >>= bits_to_write;
            cur_bit += static_cast<size_t>(bits_to_write);
            bits_remaining -= bits_to_write;
        }
        bit_offset += static_cast<size_t>(bit_width);
    }
}

/// Unpack an arbitrary number of values at a given bit width from packed data.
///
/// Reverses bit_pack_values(). Reads `ceil(count * bit_width / 8)` bytes from
/// @p src and unpacks @p count values. If @p bit_width is 0, all output values
/// are set to zero.
///
/// @param src        Pointer to packed byte data (at least ceil(count*bit_width/8) bytes).
/// @param values     Output array for @p count unpacked unsigned values.
/// @param count      Number of values to unpack.
/// @param bit_width  Bits per value (0--64).
/// @see bit_pack_values
inline void bit_unpack_values(const uint8_t* src,
                              uint64_t* values, size_t count,
                              int bit_width) {
    if (bit_width == 0) {
        for (size_t i = 0; i < count; ++i) values[i] = 0;
        return;
    }

    uint64_t mask = (bit_width >= 64) ? ~uint64_t{0}
                                      : (uint64_t{1} << bit_width) - 1;

    size_t bit_offset = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t val = 0;
        int bits_remaining = bit_width;
        size_t cur_bit = bit_offset;
        int val_bit = 0;
        while (bits_remaining > 0) {
            size_t byte_idx = cur_bit / 8;
            int bit_idx = static_cast<int>(cur_bit % 8);
            int bits_avail = 8 - bit_idx;
            int bits_to_read = (std::min)(bits_remaining, bits_avail);
            uint64_t chunk = (src[byte_idx] >> bit_idx)
                             & ((uint64_t{1} << bits_to_read) - 1);
            val |= chunk << val_bit;
            cur_bit += static_cast<size_t>(bits_to_read);
            val_bit += bits_to_read;
            bits_remaining -= bits_to_read;
        }
        values[i] = val & mask;
        bit_offset += static_cast<size_t>(bit_width);
    }
}

// ===========================================================================
// Encoder — DELTA_BINARY_PACKED for int64
// ===========================================================================

/// Encode int64 values using the DELTA_BINARY_PACKED algorithm.
///
/// Computes successive deltas, partitions them into blocks of DEFAULT_BLOCK_SIZE,
/// each subdivided into DEFAULT_MINIBLOCK_COUNT miniblocks, and bit-packs the
/// adjusted deltas (delta - min_delta) per miniblock. Achieves excellent
/// compression on sorted or near-monotonic sequences (timestamps, IDs).
///
/// @param values  Pointer to the input int64 values.
/// @param count   Number of values to encode.
/// @return        Encoded byte buffer containing the DELTA_BINARY_PACKED payload.
/// @note          For count == 0, returns a valid header with total_count = 0.
/// @see decode_int64, encode_int32
[[nodiscard]] inline std::vector<uint8_t> encode_int64(const int64_t* values,
                                                        size_t count) {
    std::vector<uint8_t> out;

    if (count == 0) {
        // Header: block_size, miniblock_count, total_count=0, first_value=0
        encode_uvarint(out, DEFAULT_BLOCK_SIZE);
        encode_uvarint(out, DEFAULT_MINIBLOCK_COUNT);
        encode_uvarint(out, 0);
        encode_uvarint(out, zigzag_encode(0));
        return out;
    }

    // Reserve a reasonable estimate (header + ~1 byte per delta on average)
    out.reserve(32 + count);

    // Write header
    encode_uvarint(out, DEFAULT_BLOCK_SIZE);
    encode_uvarint(out, DEFAULT_MINIBLOCK_COUNT);
    encode_uvarint(out, count);
    encode_uvarint(out, zigzag_encode(values[0]));

    if (count == 1) {
        return out;
    }

    // Compute all deltas
    size_t num_deltas = count - 1;
    std::vector<int64_t> deltas(num_deltas);
    for (size_t i = 0; i < num_deltas; ++i) {
        // CWE-190: Integer Overflow — unsigned subtraction avoids signed overflow UB; C++ [expr.shift] §7.6.7
        deltas[i] = static_cast<int64_t>(static_cast<uint64_t>(values[i + 1]) - static_cast<uint64_t>(values[i]));
    }

    // Process deltas in blocks of DEFAULT_BLOCK_SIZE
    size_t delta_idx = 0;
    while (delta_idx < num_deltas) {
        // Determine how many deltas are in this block
        size_t block_remaining = (std::min)(DEFAULT_BLOCK_SIZE, num_deltas - delta_idx);

        // Pad the block to DEFAULT_BLOCK_SIZE with zeros for the last block
        std::vector<int64_t> block_deltas(DEFAULT_BLOCK_SIZE, 0);
        std::copy(deltas.begin() + static_cast<ptrdiff_t>(delta_idx),
                  deltas.begin() + static_cast<ptrdiff_t>(delta_idx + block_remaining),
                  block_deltas.begin());

        // Find min_delta across the actual (non-padded) values in this block
        int64_t min_delta = block_deltas[0];
        for (size_t i = 1; i < block_remaining; ++i) {
            if (block_deltas[i] < min_delta) {
                min_delta = block_deltas[i];
            }
        }

        // Write min_delta (zigzag-encoded)
        encode_uvarint(out, zigzag_encode(min_delta));

        // Compute (delta - min_delta) for all values in the block.
        // For padding positions beyond block_remaining, use (0 - min_delta)
        // if min_delta <= 0 (non-negative result), else just 0.
        std::vector<uint64_t> adjusted(DEFAULT_BLOCK_SIZE);
        for (size_t i = 0; i < block_remaining; ++i) {
            // Use unsigned arithmetic to avoid signed overflow UB
            adjusted[i] = static_cast<uint64_t>(block_deltas[i]) - static_cast<uint64_t>(min_delta);
        }
        // Pad positions: store 0 so padding doesn't inflate bit_width
        for (size_t i = block_remaining; i < DEFAULT_BLOCK_SIZE; ++i) {
            adjusted[i] = 0;
        }

        // Compute bit widths per miniblock
        uint8_t bit_widths[DEFAULT_MINIBLOCK_COUNT];
        for (size_t mb = 0; mb < DEFAULT_MINIBLOCK_COUNT; ++mb) {
            size_t mb_start = mb * VALUES_PER_MINIBLOCK;
            uint64_t max_val = 0;
            for (size_t j = 0; j < VALUES_PER_MINIBLOCK; ++j) {
                if (adjusted[mb_start + j] > max_val) {
                    max_val = adjusted[mb_start + j];
                }
            }
            bit_widths[mb] = static_cast<uint8_t>(bit_width_for(max_val));
        }

        // Write bit widths (one byte per miniblock)
        for (size_t mb = 0; mb < DEFAULT_MINIBLOCK_COUNT; ++mb) {
            out.push_back(bit_widths[mb]);
        }

        // Write miniblock data (bit-packed)
        for (size_t mb = 0; mb < DEFAULT_MINIBLOCK_COUNT; ++mb) {
            size_t mb_start = mb * VALUES_PER_MINIBLOCK;
            bit_pack_values(out, adjusted.data() + mb_start,
                            VALUES_PER_MINIBLOCK, bit_widths[mb]);
        }

        delta_idx += block_remaining;
    }

    return out;
}

/// Encode int32 values using the DELTA_BINARY_PACKED algorithm.
///
/// Convenience overload that widens int32 values to int64 and delegates to
/// encode_int64(). The encoded wire format is identical.
///
/// @param values  Pointer to the input int32 values.
/// @param count   Number of values to encode.
/// @return        Encoded byte buffer containing the DELTA_BINARY_PACKED payload.
/// @see decode_int32, encode_int64
[[nodiscard]] inline std::vector<uint8_t> encode_int32(const int32_t* values,
                                                        size_t count) {
    // Widen int32 to int64 and use the same encoding
    std::vector<int64_t> wide(count);
    for (size_t i = 0; i < count; ++i) {
        wide[i] = static_cast<int64_t>(values[i]);
    }
    return encode_int64(wide.data(), count);
}

// ===========================================================================
// Decoder — DELTA_BINARY_PACKED for int64
// ===========================================================================

/// Decode DELTA_BINARY_PACKED data back to int64 values.
///
/// Parses the block header (block_size, miniblock_count, total_count,
/// first_value), then iterates through blocks and miniblocks, unpacking
/// bit-packed adjusted deltas and reconstructing original values. Includes
/// overflow protection: returns a partial result on integer overflow or
/// corrupt bit widths.
///
/// @param data        Pointer to the encoded DELTA_BINARY_PACKED payload.
/// @param size        Size of the encoded data in bytes.
/// @param num_values  Number of values to decode (from the Parquet page header).
/// @return            Decoded int64 values (may be fewer than @p num_values on
///                    truncated or corrupt input).
/// @note              The @c total_value_count in the header is ignored; the
///                    caller-supplied @p num_values is authoritative.
/// @see encode_int64, decode_int32
[[nodiscard]] inline std::vector<int64_t> decode_int64(const uint8_t* data,
                                                        size_t size,
                                                        size_t num_values) {
    std::vector<int64_t> result;
    if (num_values == 0 || size == 0) return result;
    if (num_values > 256 * 1024 * 1024) return result; // 256M value cap
    result.reserve(num_values);

    size_t pos = 0;

    // Read header
    uint64_t block_size      = decode_uvarint(data, pos, size);
    uint64_t miniblock_count = decode_uvarint(data, pos, size);
    uint64_t total_count     = decode_uvarint(data, pos, size);
    uint64_t first_value_zz  = decode_uvarint(data, pos, size);

    (void)total_count; // We use num_values from the caller

    // Validate block structure
    if (miniblock_count == 0 || block_size == 0) return result;
    // Parquet spec: block_size must be a multiple of 128; cap to prevent
    // absurd allocations from corrupted data (65536 values per block max).
    static constexpr uint64_t MAX_DELTA_BLOCK_SIZE = 65536;
    if (block_size > MAX_DELTA_BLOCK_SIZE) return result;
    static constexpr uint64_t MAX_MINIBLOCK_COUNT = 256;
    if (miniblock_count > MAX_MINIBLOCK_COUNT) return result;
    if (block_size % miniblock_count != 0) return result;
    size_t values_per_miniblock = static_cast<size_t>(block_size / miniblock_count);
    if (values_per_miniblock == 0) return result;

    // First value
    int64_t prev = zigzag_decode(first_value_zz);
    result.push_back(prev);

    if (num_values == 1) return result;

    // Decode blocks until we have enough values
    size_t values_remaining = num_values - 1; // first value already emitted

    while (values_remaining > 0 && pos < size) {
        // Read min_delta (zigzag-encoded)
        uint64_t min_delta_zz = decode_uvarint(data, pos, size);
        int64_t min_delta = zigzag_decode(min_delta_zz);

        // Read bit widths (one per miniblock)
        std::vector<uint8_t> bit_widths(static_cast<size_t>(miniblock_count));
        for (size_t mb = 0; mb < static_cast<size_t>(miniblock_count); ++mb) {
            if (pos < size) {
                bit_widths[mb] = data[pos++];
            } else {
                bit_widths[mb] = 0;
            }
        }

        // Decode each miniblock (buffer allocated once, reused across miniblocks)
        std::vector<uint64_t> unpacked(values_per_miniblock);
        for (size_t mb = 0; mb < static_cast<size_t>(miniblock_count); ++mb) {
            if (values_remaining == 0) break;

            int bw = bit_widths[mb];
            if (bw > 64) return result; // corrupt bit width — return partial

            if (bw == 0) {
                // All adjusted values are 0 => all deltas are min_delta
                for (size_t j = 0; j < values_per_miniblock; ++j) {
                    unpacked[j] = 0;
                }
            } else {
                // Calculate bytes needed for this miniblock
                size_t miniblock_bytes = (values_per_miniblock * static_cast<size_t>(bw) + 7) / 8;

                if (pos + miniblock_bytes > size) {
                    // Truncated data: decode what we can, pad rest with zeros
                    size_t avail = size - pos;
                    // Create a zero-padded copy
                    std::vector<uint8_t> padded(miniblock_bytes, 0);
                    std::memcpy(padded.data(), data + pos, avail);
                    bit_unpack_values(padded.data(), unpacked.data(),
                                      values_per_miniblock, bw);
                    pos = size;
                } else {
                    bit_unpack_values(data + pos, unpacked.data(),
                                      values_per_miniblock, bw);
                    pos += miniblock_bytes;
                }
            }

            // Convert adjusted values back to actual values
            size_t to_emit = (std::min)(values_per_miniblock, values_remaining);
            for (size_t j = 0; j < to_emit; ++j) {
                // Reconstruct delta using unsigned arithmetic to avoid overflow UB
                int64_t delta = static_cast<int64_t>(
                    unpacked[j] + static_cast<uint64_t>(min_delta));
#if defined(__GNUC__) || defined(__clang__)
                int64_t new_val;
                if (__builtin_add_overflow(prev, delta, &new_val)) {
                    return result; // overflow — return partial result
                }
                prev = new_val;
#else
                // Manual overflow check for MSVC: detect sign-change
                if ((delta > 0 && prev > (std::numeric_limits<int64_t>::max)() - delta) ||
                    (delta < 0 && prev < (std::numeric_limits<int64_t>::min)() - delta)) {
                    return result; // overflow — return partial result
                }
                prev += delta;
#endif
                result.push_back(prev);
            }
            values_remaining -= to_emit;
        }
    }

    return result;
}

/// Decode DELTA_BINARY_PACKED data back to int32 values.
///
/// Convenience overload that decodes via decode_int64() and narrows the
/// results to int32. Values exceeding the int32 range are truncated.
///
/// @param data        Pointer to the encoded DELTA_BINARY_PACKED payload.
/// @param size        Size of the encoded data in bytes.
/// @param num_values  Number of values to decode (from the Parquet page header).
/// @return            Decoded int32 values.
/// @see encode_int32, decode_int64
[[nodiscard]] inline std::vector<int32_t> decode_int32(const uint8_t* data,
                                                        size_t size,
                                                        size_t num_values) {
    auto wide = decode_int64(data, size, num_values);
    std::vector<int32_t> result(wide.size());
    for (size_t i = 0; i < wide.size(); ++i) {
        // CWE-681: Incorrect Conversion between Numeric Types — range check before int64→int32 narrowing
        if (wide[i] < (std::numeric_limits<int32_t>::min)() || wide[i] > (std::numeric_limits<int32_t>::max)()) return {};
        result[i] = static_cast<int32_t>(wide[i]);
    }
    return result;
}

} // namespace delta
} // namespace signet::forge
