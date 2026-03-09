// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji

/// @file rle.hpp
/// @brief RLE/Bit-Packing Hybrid encoding and decoding (Parquet spec).
///
/// Implements the RLE/Bit-Packing Hybrid encoding used throughout Parquet for
/// definition levels, repetition levels, boolean columns, and dictionary indices
/// (RLE_DICTIONARY encoding). Provides both streaming (RleEncoder/RleDecoder)
/// and static convenience APIs.
///
/// @see https://parquet.apache.org/documentation/latest/ (Hybrid RLE section)

#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace signet::forge {

// ===========================================================================
// RLE/Bit-Packing Hybrid Encoding (Parquet spec)
// ===========================================================================
//
// This implements the RLE/Bit-Packing Hybrid encoding used throughout Parquet:
//   - Definition levels and repetition levels
//   - Boolean columns (when RLE-encoded)
//   - Dictionary indices (RLE_DICTIONARY encoding)
//
// Wire format (each run begins with a varint header):
//
//   header & 1 == 0  =>  RLE run
//       run_length = header >> 1
//       followed by the repeated value in ceil(bit_width/8) bytes, LE
//
//   header & 1 == 1  =>  Bit-packed group
//       group_count = header >> 1
//       each group holds 8 values, packed at bit_width bits per value, LSB first
//       total bytes = group_count * bit_width
//
// For def/rep levels the encoded payload is prefixed with a 4-byte LE length.
// For RLE_DICTIONARY, the bit_width is stored as a single preceding byte.
//

// ---------------------------------------------------------------------------
// Varint helpers (unsigned LEB128)
// ---------------------------------------------------------------------------

/// Encode an unsigned varint (LEB128) into a byte buffer.
///
/// Appends the variable-length encoding of @p value to @p buf. Each output byte
/// uses 7 data bits and 1 continuation bit (MSB), following the unsigned LEB128
/// convention used by the Parquet wire format.
///
/// @param buf   Output byte buffer to append to.
/// @param value The unsigned integer to encode.
/// @return      Number of bytes written (1--10).
/// @see decode_varint
inline size_t encode_varint(std::vector<uint8_t>& buf, uint64_t value) {
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
/// On success, @p pos is advanced past the consumed bytes. Returns 0 with @p pos
/// unchanged if the buffer is exhausted before a terminating byte is found.
/// Includes overflow protection: decoding stops if the shift exceeds 63 bits.
///
/// @param data  Pointer to the encoded byte stream.
/// @param pos   Current read position (updated on return).
/// @param size  Total size of the byte stream.
/// @return      The decoded unsigned integer, or 0 on failure.
/// @see encode_varint
inline uint64_t decode_varint(const uint8_t* data, size_t& pos, size_t size) {
    uint64_t result = 0;
    int shift = 0;
    size_t start_pos = pos;
    while (pos < size) {
        uint8_t byte = data[pos++];
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return result;
        }
        shift += 7;
        if (shift >= 64) { pos = start_pos; return 0; } // CWE-190: Integer Overflow — restore position on overflow
    }
    return result;
}

// ---------------------------------------------------------------------------
// Bit-packing helpers
// ---------------------------------------------------------------------------

/// Pack exactly 8 values at the given bit width into a byte buffer.
///
/// Each value occupies @p bit_width bits, packed LSB-first in little-endian
/// byte order. Appends exactly @p bit_width bytes to @p out (since
/// 8 values * bit_width bits = bit_width bytes). If @p bit_width is 0,
/// no bytes are emitted (all values are implicitly zero).
///
/// @param out        Output byte buffer to append packed bytes to.
/// @param values     Pointer to exactly 8 unsigned values to pack.
/// @param bit_width  Bits per value (0--64).
/// @see bit_unpack_8
inline void bit_pack_8(std::vector<uint8_t>& out, const uint64_t* values, int bit_width) {
    if (bit_width == 0) return; // all zeros, no bytes needed

    // Total bits = 8 * bit_width; bytes = ceil(8 * bit_width / 8) = bit_width
    size_t start = out.size();
    out.resize(start + (8 * static_cast<size_t>(bit_width) + 7) / 8, 0);
    uint8_t* dst = out.data() + start;

    int bit_offset = 0; // absolute bit position in dst
    for (int i = 0; i < 8; ++i) {
        uint64_t val = values[i];
        // Write bit_width bits of val starting at bit_offset
        int bits_remaining = bit_width;
        int cur_bit = bit_offset;
        while (bits_remaining > 0) {
            int byte_idx = cur_bit / 8;
            int bit_idx  = cur_bit % 8;
            int bits_to_write = std::min(bits_remaining, 8 - bit_idx);
            uint8_t mask = static_cast<uint8_t>((val & ((1u << bits_to_write) - 1)) << bit_idx);
            dst[byte_idx] |= mask;
            val >>= bits_to_write;
            cur_bit += bits_to_write;
            bits_remaining -= bits_to_write;
        }
        bit_offset += bit_width;
    }
}

/// Unpack exactly 8 values at the given bit width from a byte buffer.
///
/// Reverses the packing performed by bit_pack_8(). Reads @p bit_width bytes
/// from @p src and unpacks 8 values of @p bit_width bits each, stored LSB-first.
/// If @p bit_width is 0, all output values are set to zero.
///
/// @param src        Pointer to at least @p bit_width bytes of packed data.
/// @param values     Output array of exactly 8 unsigned values.
/// @param bit_width  Bits per value (0--64).
/// @see bit_pack_8
inline void bit_unpack_8(const uint8_t* src, uint64_t* values, int bit_width) {
    if (bit_width == 0) {
        for (int i = 0; i < 8; ++i) values[i] = 0;
        return;
    }

    uint64_t mask = (bit_width == 64) ? ~uint64_t(0)
                                      : (uint64_t(1) << bit_width) - 1;

    int bit_offset = 0;
    for (int i = 0; i < 8; ++i) {
        uint64_t val = 0;
        int bits_remaining = bit_width;
        int cur_bit = bit_offset;
        int val_bit = 0;
        while (bits_remaining > 0) {
            int byte_idx = cur_bit / 8;
            int bit_idx  = cur_bit % 8;
            int bits_avail = 8 - bit_idx;
            int bits_to_read = std::min(bits_remaining, bits_avail);
            uint64_t chunk = (src[byte_idx] >> bit_idx) & ((1u << bits_to_read) - 1);
            val |= chunk << val_bit;
            cur_bit += bits_to_read;
            val_bit += bits_to_read;
            bits_remaining -= bits_to_read;
        }
        values[i] = val & mask;
        bit_offset += bit_width;
    }
}

// ===========================================================================
// RleEncoder
// ===========================================================================
//
// Encodes a stream of unsigned integer values using the Parquet RLE/Bit-Pack
// Hybrid scheme. The encoder buffers values and decides per-group whether to
// emit an RLE run (for repeated values) or a bit-packed group of 8.
//
// Usage:
//     RleEncoder enc(bit_width);
//     for (auto v : values) enc.put(v);
//     enc.flush();
//     // enc.data() contains the encoded bytes (no length prefix)
//
/// Streaming encoder for the Parquet RLE/Bit-Packing Hybrid scheme.
///
/// Accepts a stream of unsigned integer values via put() and decides per-group
/// whether to emit an RLE run (for repeated values) or a bit-packed group of 8.
/// Call flush() after all values are written, then retrieve the encoded bytes
/// from data().
///
/// @note The encoded output does NOT include a length prefix. Use
///       encode_with_length() for def/rep level encoding that requires one.
///
/// @see RleDecoder
/// @see https://parquet.apache.org/documentation/latest/
class RleEncoder {
public:
    /// Construct an encoder for values of the given bit width.
    ///
    /// @param bit_width  Bits per value (0--64). Values outside this range are
    ///                   clamped to 0 (no encoding).
    explicit RleEncoder(int bit_width)
        : bit_width_(bit_width < 0 ? 0 : (bit_width > 64 ? 0 : bit_width))
        , byte_width_(bit_width_ > 0 ? static_cast<int>((bit_width_ + 7) / 8) : 0) {}

    /// Add a single value to the encoding stream.
    ///
    /// Values are buffered internally and flushed as RLE runs or bit-packed
    /// groups. If bit_width is 0, this is a no-op (all values are implicitly
    /// zero).
    ///
    /// @param value  The unsigned integer value to encode (must fit in bit_width bits).
    void put(uint64_t value) {
        // bit_width=0: all values are implicitly zero, nothing to encode.
        // Matches static encode() which returns empty for bit_width=0.
        if (bit_width_ == 0) return;

        // Accumulate values into the pending buffer
        if (rle_count_ == 0) {
            // Start a new potential RLE run
            rle_value_ = value;
            rle_count_ = 1;
        } else if (value == rle_value_ && bp_count_ == 0) {
            // Extend current RLE run
            ++rle_count_;
        } else {
            // Value differs from current run, or we're already in bit-pack mode
            if (bp_count_ == 0 && rle_count_ >= 8) {
                // We have a worthwhile RLE run, flush it
                flush_rle_run();
                // Start new run with this value
                rle_value_ = value;
                rle_count_ = 1;
            } else {
                // Move RLE-buffered values into bit-pack buffer
                while (rle_count_ > 0) {
                    bp_buffer_[bp_count_++] = rle_value_;
                    --rle_count_;
                    if (bp_count_ == 8) {
                        flush_bp_group();
                    }
                }
                // Add the new value to bit-pack buffer
                bp_buffer_[bp_count_++] = value;
                if (bp_count_ == 8) {
                    flush_bp_group();
                }
            }
        }
    }

    /// Flush any pending values to the output buffer.
    ///
    /// Must be called after all put() calls to finalize the encoding. Any
    /// partial bit-packed group (fewer than 8 values) is zero-padded to 8
    /// before emission.
    void flush() {
        // First: if we have pending bit-packed values plus RLE values,
        // drain everything
        if (bp_count_ > 0 && rle_count_ > 0) {
            // Move remaining RLE values into bit-pack
            while (rle_count_ > 0) {
                bp_buffer_[bp_count_++] = rle_value_;
                --rle_count_;
                if (bp_count_ == 8) {
                    flush_bp_group();
                }
            }
        }

        // Flush any pending RLE run
        if (rle_count_ > 0) {
            flush_rle_run();
        }

        // Flush any remaining bit-pack values (partial group, pad with zeros)
        if (bp_count_ > 0) {
            // Pad to 8 with zeros
            while (bp_count_ < 8) {
                bp_buffer_[bp_count_++] = 0;
            }
            flush_bp_group();
        }

        // Flush accumulated bit-pack groups
        flush_bp_groups();
    }

    /// Returns a reference to the encoded byte buffer (without length prefix).
    ///
    /// @return Const reference to the internal encoded output.
    /// @note   Call flush() before accessing this to ensure all data is emitted.
    [[nodiscard]] const std::vector<uint8_t>& data() const { return buffer_; }

    /// Returns the size of the encoded data in bytes.
    ///
    /// @return Number of bytes in the encoded output.
    [[nodiscard]] size_t encoded_size() const { return buffer_.size(); }

    /// Reset the encoder to its initial state, preserving the bit width.
    ///
    /// Clears all internal buffers and accumulators so the encoder can be
    /// reused for a new encoding session.
    void reset() {
        buffer_.clear();
        rle_count_ = 0;
        rle_value_ = 0;
        bp_count_ = 0;
        bp_groups_.clear();
        bp_group_count_ = 0;
    }

    // -----------------------------------------------------------------------
    // Static convenience methods
    // -----------------------------------------------------------------------

    /// Encode an array of uint32 values using the RLE/Bit-Pack Hybrid scheme.
    ///
    /// Convenience static method that constructs an encoder, feeds all values,
    /// flushes, and returns the resulting byte buffer without a length prefix.
    ///
    /// @param values     Pointer to the input values.
    /// @param count      Number of values to encode.
    /// @param bit_width  Bits per value (0--64). Returns empty for invalid widths.
    /// @return           Encoded byte buffer (empty on error or bit_width == 0).
    /// @see encode_with_length
    static std::vector<uint8_t> encode(const uint32_t* values, size_t count, int bit_width) {
        if (bit_width == 0) {
            // Parquet levels may legally use bit_width=0 when all values are 0.
            // We encode this as an empty payload (no runs needed).
            for (size_t i = 0; i < count; ++i) {
                if (values[i] != 0) return {};
            }
            return {};
        }
        if (bit_width < 1 || bit_width > 64) return {};
        RleEncoder enc(bit_width);
        for (size_t i = 0; i < count; ++i) {
            enc.put(static_cast<uint64_t>(values[i]));
        }
        enc.flush();
        return enc.buffer_;
    }

    /// Encode with a 4-byte little-endian length prefix.
    ///
    /// Produces the same output as encode(), but prepends a 4-byte LE uint32
    /// length prefix containing the payload size. This format is required by
    /// Parquet for definition and repetition level encoding.
    ///
    /// @param values     Pointer to the input values.
    /// @param count      Number of values to encode.
    /// @param bit_width  Bits per value (0--64).
    /// @return           Length-prefixed encoded byte buffer.
    /// @see encode, RleDecoder::decode_with_length
    static std::vector<uint8_t> encode_with_length(const uint32_t* values, size_t count,
                                                    int bit_width) {
        auto payload = encode(values, count, bit_width);
        if (payload.size() > UINT32_MAX) return {}; // CWE-190: Integer Overflow — payload too large for uint32 length prefix
        std::vector<uint8_t> result;
        result.reserve(4 + payload.size());
        // 4-byte LE length prefix
        uint32_t len = static_cast<uint32_t>(payload.size());
        result.push_back(static_cast<uint8_t>(len));
        result.push_back(static_cast<uint8_t>(len >> 8));
        result.push_back(static_cast<uint8_t>(len >> 16));
        result.push_back(static_cast<uint8_t>(len >> 24));
        result.insert(result.end(), payload.begin(), payload.end());
        return result;
    }

private:
    int bit_width_;                ///< Bits per encoded value.
    int byte_width_;               ///< ceil(bit_width / 8) — bytes per RLE literal.

    std::vector<uint8_t> buffer_;  ///< Final encoded output.

    uint64_t rle_value_ = 0;      ///< Current RLE run value.
    size_t   rle_count_ = 0;      ///< Number of repeated values in the current RLE run.

    uint64_t bp_buffer_[8] = {};   ///< Bit-pack accumulator (up to 8 values).
    int      bp_count_ = 0;        ///< Number of values in bp_buffer_.

    std::vector<uint8_t> bp_groups_; ///< Accumulated bit-pack group bytes.
    int bp_group_count_ = 0;        ///< Number of bit-pack groups accumulated.

    /// Write the value in byte_width_ little-endian bytes to the buffer.
    void write_rle_value(uint64_t value) {
        for (int i = 0; i < byte_width_; ++i) {
            buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
            value >>= 8;
        }
    }

    /// Flush a complete RLE run of rle_count_ copies of rle_value_.
    void flush_rle_run() {
        // First flush any pending bit-pack groups
        flush_bp_groups();

        // CWE-190: Integer Overflow / CWE-682: Incorrect Calculation — cap before left shift
        if (rle_count_ > (SIZE_MAX >> 1)) rle_count_ = SIZE_MAX >> 1;
        // header = (run_length << 1) | 0
        encode_varint(buffer_, rle_count_ << 1);
        write_rle_value(rle_value_);
        rle_count_ = 0;
    }

    /// Flush a single bit-packed group of 8 values (accumulates into bp_groups_).
    void flush_bp_group() {
        bit_pack_8(bp_groups_, bp_buffer_, bit_width_);
        ++bp_group_count_;
        bp_count_ = 0;
        std::memset(bp_buffer_, 0, sizeof(bp_buffer_));
    }

    /// Flush all accumulated bit-pack groups into buffer_ with a single header.
    void flush_bp_groups() {
        if (bp_group_count_ == 0) return;

        // header = (group_count << 1) | 1
        encode_varint(buffer_, (static_cast<uint64_t>(bp_group_count_) << 1) | 1);
        buffer_.insert(buffer_.end(), bp_groups_.begin(), bp_groups_.end());

        bp_groups_.clear();
        bp_group_count_ = 0;
    }
};

// ===========================================================================
// RleDecoder
// ===========================================================================
//
// Decodes a byte stream encoded with the Parquet RLE/Bit-Pack Hybrid scheme.
//
// Usage:
//     RleDecoder dec(data, size, bit_width);
//     uint64_t val;
//     while (dec.get(&val)) { /* process val */ }
//
/// Streaming decoder for the Parquet RLE/Bit-Packing Hybrid scheme.
///
/// Consumes a byte stream encoded by RleEncoder and yields individual unsigned
/// integer values one at a time via get(), or in bulk via get_batch(). The
/// decoder handles both RLE runs and bit-packed groups transparently.
///
/// @note The input data must NOT include a length prefix; use decode_with_length()
///       for length-prefixed buffers (def/rep levels).
///
/// @see RleEncoder
class RleDecoder {
public:
    /// Construct a decoder over a raw encoded byte buffer.
    ///
    /// @param data       Pointer to the RLE/Bit-Pack encoded bytes.
    /// @param size       Size of the encoded data in bytes.
    /// @param bit_width  Bits per value (0--64). Values outside this range are
    ///                   clamped to 0.
    RleDecoder(const uint8_t* data, size_t size, int bit_width)
        : data_(data)
        , size_(size)
        , pos_(0)
        , bit_width_(bit_width < 0 ? 0 : (bit_width > 64 ? 0 : bit_width))
        , byte_width_(bit_width_ > 0 ? static_cast<int>((bit_width_ + 7) / 8) : 0)
        , rle_remaining_(0)
        , rle_value_(0)
        , bp_remaining_(0)
        , bp_index_(0) {}

    /// Read the next decoded value.
    ///
    /// Reads from buffered RLE runs or bit-packed groups first, then parses the
    /// next header from the byte stream when buffers are exhausted. Includes
    /// guards against corrupt varints and oversized bit-packed allocations
    /// (capped at 8M values per group).
    ///
    /// @param[out] value  Pointer to receive the decoded value.
    /// @return            @c true if a value was read, @c false if the stream is exhausted.
    bool get(uint64_t* value) {
        // If we have buffered RLE values, return one
        if (rle_remaining_ > 0) {
            *value = rle_value_;
            --rle_remaining_;
            return true;
        }

        // If we have buffered bit-packed values, return one
        if (bp_remaining_ > 0) {
            *value = bp_buffer_[bp_index_++];
            --bp_remaining_;
            return true;
        }

        // Need to read the next header
        if (pos_ >= size_) return false;

        uint64_t header = decode_varint(data_, pos_, size_);

        if ((header & 1) == 0) {
            // RLE run
            uint64_t run_length = header >> 1;
            if (run_length == 0) return false;

            // Read the value in byte_width_ LE bytes
            uint64_t val = 0;
            int bytes_read = 0;
            for (int i = 0; i < byte_width_ && pos_ < size_; ++i) {
                val |= static_cast<uint64_t>(data_[pos_++]) << (8 * i);
                ++bytes_read;
            }
            if (bytes_read < byte_width_) return false; // CWE-125: Out-of-bounds Read — truncated RLE value

            rle_value_ = val;
            rle_remaining_ = run_length - 1; // return one now
            *value = val;
            return true;
        } else {
            // Bit-packed groups
            uint64_t group_count = header >> 1;
            if (group_count == 0) return false;

            // Cap group_count to available data to prevent OOM from corrupt varints
            size_t avail_bytes = (pos_ < size_) ? (size_ - pos_) : 0;
            if (bit_width_ > 0) {
                size_t max_groups = avail_bytes / static_cast<size_t>(bit_width_) + 1;
                if (group_count > max_groups) group_count = max_groups;
            }
            if (group_count == 0) return false;

            size_t total_values = group_count * 8;
            size_t total_bytes = group_count * static_cast<size_t>(bit_width_);
            // Byte-based cap: prevent huge allocations from corrupt varints (CWE-770)
            static constexpr size_t MAX_BP_BYTES = 256 * 1024 * 1024; // 256 MB
            if (total_bytes > MAX_BP_BYTES) return false;

            if (pos_ + total_bytes > size_) {
                // Truncated data: only decode what we can
                total_bytes = size_ - pos_;
            }

            // Decode all groups into bp_decoded_
            bp_decoded_.resize(total_values);
            size_t src_offset = 0;
            size_t val_offset = 0;
            for (uint64_t g = 0; g < group_count; ++g) {
                if (src_offset + static_cast<size_t>(bit_width_) > total_bytes) break;
                bit_unpack_8(data_ + pos_ + src_offset,
                             bp_decoded_.data() + val_offset,
                             bit_width_);
                src_offset += static_cast<size_t>(bit_width_);
                val_offset += 8;
            }

            pos_ += total_bytes;

            // If no groups were decoded (all truncated), stop
            if (val_offset == 0) return false;

            // Set up buffered read state
            bp_buffer_ = bp_decoded_.data();
            bp_index_ = 1;
            bp_remaining_ = val_offset - 1;
            *value = bp_decoded_[0];
            return true;
        }
    }

    /// Read a batch of decoded values.
    ///
    /// Reads exactly @p count values into @p out. Returns @c false immediately
    /// if the stream is exhausted before @p count values are read (partial
    /// results may be written to @p out).
    ///
    /// @param[out] out    Output array with space for at least @p count values.
    /// @param      count  Number of values to read.
    /// @return            @c true if all @p count values were read successfully.
    bool get_batch(uint64_t* out, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (!get(&out[i])) return false;
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Static convenience methods
    // -----------------------------------------------------------------------

    /// Decode values from an RLE-encoded buffer without a length prefix.
    ///
    /// Convenience static method that constructs a decoder, reads up to
    /// @p num_values values, and returns them as uint32. For bit_width == 0
    /// and an empty payload, returns a vector of @p num_values zeros. Returns
    /// empty on invalid bit_width or non-empty payload with bit_width == 0.
    ///
    /// @param data        Pointer to the encoded byte data.
    /// @param size        Size of the encoded data in bytes.
    /// @param bit_width   Bits per value (0--64).
    /// @param num_values  Maximum number of values to decode.
    /// @return            Decoded values (may be shorter than @p num_values if
    ///                    the stream is exhausted).
    /// @see decode_with_length, RleEncoder::encode
    static std::vector<uint32_t> decode(const uint8_t* data, size_t size,
                                         int bit_width, size_t num_values) {
        if (bit_width == 0) {
            // Canonical empty payload for a zero-width stream. Reject any
            // non-empty payload as malformed to avoid ambiguous decoding.
            if (size != 0) return {};
            return std::vector<uint32_t>(num_values, 0u);
        }
        if (bit_width < 1 || bit_width > 64) return {};
        RleDecoder dec(data, size, bit_width);
        std::vector<uint32_t> result;
        result.reserve(num_values);
        uint64_t val;
        for (size_t i = 0; i < num_values; ++i) {
            if (!dec.get(&val)) break;
            result.push_back(static_cast<uint32_t>(val));
        }
        return result;
    }

    /// Decode from a buffer that starts with a 4-byte LE length prefix.
    ///
    /// Reads a 4-byte little-endian uint32 payload length, then delegates to
    /// decode() for the payload bytes. This is the format used by Parquet for
    /// definition and repetition level encoding.
    ///
    /// @param data        Pointer to the length-prefixed encoded data.
    /// @param size        Total size of the buffer in bytes (must be >= 4).
    /// @param bit_width   Bits per value (0--64).
    /// @param num_values  Maximum number of values to decode.
    /// @return            Decoded values (empty if size < 4 or decode fails).
    /// @see decode, RleEncoder::encode_with_length
    static std::vector<uint32_t> decode_with_length(const uint8_t* data, size_t size,
                                                     int bit_width, size_t num_values) {
        if (size < 4) return {};

        uint32_t payload_len = static_cast<uint32_t>(data[0])
                             | (static_cast<uint32_t>(data[1]) << 8)
                             | (static_cast<uint32_t>(data[2]) << 16)
                             | (static_cast<uint32_t>(data[3]) << 24);

        size_t available = std::min(static_cast<size_t>(payload_len), size - 4);
        return decode(data + 4, available, bit_width, num_values);
    }

private:
    const uint8_t* data_;          ///< Pointer to the encoded byte stream.
    size_t size_;                  ///< Total size of the encoded data.
    size_t pos_;                   ///< Current read position in the byte stream.
    int bit_width_;                ///< Bits per encoded value.
    int byte_width_;               ///< ceil(bit_width / 8) — bytes per RLE literal.

    size_t   rle_remaining_;       ///< Remaining values in the current RLE run.
    uint64_t rle_value_;           ///< Repeated value for the current RLE run.

    size_t   bp_remaining_;        ///< Remaining values in the current bit-packed group.
    size_t   bp_index_;            ///< Next index to read from bp_buffer_.
    uint64_t* bp_buffer_ = nullptr; ///< Pointer into bp_decoded_ for current group.
    std::vector<uint64_t> bp_decoded_; ///< Storage for unpacked bit-packed values.
};

} // namespace signet::forge
