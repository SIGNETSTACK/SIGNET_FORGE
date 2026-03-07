// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji

/// @file dictionary.hpp
/// @brief Dictionary encoding and decoding for Parquet (PLAIN_DICTIONARY / RLE_DICTIONARY).
///
/// Implements PLAIN_DICTIONARY (encoding type 2) for dictionary pages and
/// RLE_DICTIONARY (encoding type 8) for data pages. Critical for low-cardinality
/// columns (symbols, sides, exchanges) where 10--50x compression is typical.
/// The encoder (DictionaryEncoder) and decoder (DictionaryDecoder) are both
/// templated on the value type.
///
/// @see rle.hpp for the RLE/Bit-Packing Hybrid used to encode dictionary indices

#pragma once

// ---------------------------------------------------------------------------
// dictionary.hpp -- Dictionary encoding for Parquet
//
// Implements PLAIN_DICTIONARY (encoding=2) for dictionary pages and
// RLE_DICTIONARY (encoding=8) for data pages. Critical for low-cardinality
// string columns (symbols, sides, exchanges) where 10-50x compression is
// typical.
//
// Wire format:
//   Dictionary page: PLAIN-encoded unique values
//     - BYTE_ARRAY: 4-byte LE length + bytes per entry
//     - INT32/FLOAT: 4-byte LE per entry
//     - INT64/DOUBLE: 8-byte LE per entry
//
//   Data page: 1-byte bit_width + RLE/Bit-Pack hybrid encoded indices
//     - bit_width = 0 for dict_size==1, else ceil(log2(dict_size))
//     - Indices encoded using RLE/Bit-Packing Hybrid (see rle.hpp)
// ---------------------------------------------------------------------------

#include "signet/encoding/rle.hpp"
#include "signet/types.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/// @brief Internal implementation details for dictionary encoding.
namespace detail {

/// Compute the minimum bit width needed to represent dictionary indices.
///
/// Returns 0 for @p dict_size <= 1 (single-entry dictionaries need 0 bits),
/// otherwise returns `ceil(log2(dict_size))`, which is the number of bits
/// needed to represent index values in the range [0, dict_size - 1].
///
/// @param dict_size  Number of entries in the dictionary.
/// @return           Bit width (0 for dict_size <= 1).
inline int dict_bit_width(size_t dict_size) {
    if (dict_size <= 1) return 0;
    // ceil(log2(n)) = number of bits needed to represent 0..n-1
    int bits = 0;
    size_t n = dict_size - 1; // max index value
    while (n > 0) {
        ++bits;
        n >>= 1;
    }
    return bits;
}

// -- PLAIN encoding helpers for dictionary page entries ----------------------

/// Append a string value in PLAIN BYTE_ARRAY format (4-byte LE length prefix + raw bytes).
///
/// @param buf  Output byte buffer.
/// @param val  The string value to encode.
inline void plain_encode_value(std::vector<uint8_t>& buf, const std::string& val) {
    uint32_t len = static_cast<uint32_t>(val.size());
    buf.push_back(static_cast<uint8_t>((len      ) & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    buf.insert(buf.end(),
               reinterpret_cast<const uint8_t*>(val.data()),
               reinterpret_cast<const uint8_t*>(val.data()) + val.size());
}

/// Append an int32_t in PLAIN format (4-byte little-endian).
///
/// @param buf  Output byte buffer.
/// @param val  The int32 value to encode.
inline void plain_encode_value(std::vector<uint8_t>& buf, int32_t val) {
    uint32_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    buf.push_back(static_cast<uint8_t>((bits      ) & 0xFF));
    buf.push_back(static_cast<uint8_t>((bits >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((bits >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((bits >> 24) & 0xFF));
}

/// Append an int64_t in PLAIN format (8-byte little-endian).
///
/// @param buf  Output byte buffer.
/// @param val  The int64 value to encode.
inline void plain_encode_value(std::vector<uint8_t>& buf, int64_t val) {
    uint64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>(bits & 0xFF));
        bits >>= 8;
    }
}

/// Append a float in PLAIN format (4-byte little-endian, IEEE 754).
///
/// @param buf  Output byte buffer.
/// @param val  The float value to encode.
inline void plain_encode_value(std::vector<uint8_t>& buf, float val) {
    uint32_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    buf.push_back(static_cast<uint8_t>((bits      ) & 0xFF));
    buf.push_back(static_cast<uint8_t>((bits >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((bits >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((bits >> 24) & 0xFF));
}

/// Append a double in PLAIN format (8-byte little-endian, IEEE 754).
///
/// @param buf  Output byte buffer.
/// @param val  The double value to encode.
inline void plain_encode_value(std::vector<uint8_t>& buf, double val) {
    uint64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<uint8_t>(bits & 0xFF));
        bits >>= 8;
    }
}

// -- PLAIN decoding helpers for dictionary page entries ----------------------

/// Decode a string from PLAIN BYTE_ARRAY format at @p data[pos].
///
/// Reads a 4-byte LE length prefix followed by raw bytes. Advances @p pos
/// past the consumed bytes. Returns an empty string if the buffer is too small.
///
/// @param data  Pointer to the encoded byte stream.
/// @param pos   Current read position (updated on return).
/// @param size  Total size of the byte stream.
/// @return      The decoded string value.
inline std::string plain_decode_value(const uint8_t* data, size_t& pos,
                                       size_t size, std::string* /*tag*/) {
    if (pos + 4 > size) return {};
    uint32_t len;
    std::memcpy(&len, data + pos, 4);
    pos += 4;
    if (pos + len > size) return {};
    std::string val(reinterpret_cast<const char*>(data + pos), len);
    pos += len;
    return val;
}

/// Decode an int32_t from PLAIN format at @p data[pos]. Advances @p pos by 4.
///
/// @param data  Pointer to the encoded byte stream.
/// @param pos   Current read position (updated on return).
/// @param size  Total size of the byte stream.
/// @return      The decoded int32 value, or 0 if insufficient data.
inline int32_t plain_decode_value(const uint8_t* data, size_t& pos,
                                   size_t size, int32_t* /*tag*/) {
    if (pos + 4 > size) return 0;
    int32_t val;
    std::memcpy(&val, data + pos, 4);
    pos += 4;
    return val;
}

/// Decode an int64_t from PLAIN format at @p data[pos]. Advances @p pos by 8.
///
/// @param data  Pointer to the encoded byte stream.
/// @param pos   Current read position (updated on return).
/// @param size  Total size of the byte stream.
/// @return      The decoded int64 value, or 0 if insufficient data.
inline int64_t plain_decode_value(const uint8_t* data, size_t& pos,
                                   size_t size, int64_t* /*tag*/) {
    if (pos + 8 > size) return 0;
    int64_t val;
    std::memcpy(&val, data + pos, 8);
    pos += 8;
    return val;
}

/// Decode a float from PLAIN format at @p data[pos]. Advances @p pos by 4.
///
/// @param data  Pointer to the encoded byte stream.
/// @param pos   Current read position (updated on return).
/// @param size  Total size of the byte stream.
/// @return      The decoded float value, or 0.0f if insufficient data.
inline float plain_decode_value(const uint8_t* data, size_t& pos,
                                 size_t size, float* /*tag*/) {
    if (pos + 4 > size) return 0.0f;
    float val;
    std::memcpy(&val, data + pos, 4);
    pos += 4;
    return val;
}

/// Decode a double from PLAIN format at @p data[pos]. Advances @p pos by 8.
///
/// @param data  Pointer to the encoded byte stream.
/// @param pos   Current read position (updated on return).
/// @param size  Total size of the byte stream.
/// @return      The decoded double value, or 0.0 if insufficient data.
inline double plain_decode_value(const uint8_t* data, size_t& pos,
                                  size_t size, double* /*tag*/) {
    if (pos + 8 > size) return 0.0;
    double val;
    std::memcpy(&val, data + pos, 8);
    pos += 8;
    return val;
}

} // namespace detail

// ===========================================================================
// DictionaryEncoder
// ===========================================================================
//
// Builds a dictionary of unique values and encodes data as indices into
// that dictionary. The dictionary page is PLAIN-encoded, and the indices
// page uses RLE_DICTIONARY (bit_width byte + RLE/Bit-Pack hybrid).
//
// Usage:
//     DictionaryEncoder<std::string> enc;
//     for (auto& s : strings) enc.put(s);
//     enc.flush();
//     auto dict_page = enc.dictionary_page();   // PLAIN-encoded unique values
//     auto idx_page  = enc.indices_page();       // bit_width + RLE-encoded indices
//
/// Dictionary encoder for Parquet PLAIN_DICTIONARY / RLE_DICTIONARY encoding.
///
/// Builds a dictionary of unique values, assigning each a sequential integer
/// index. The dictionary page is PLAIN-encoded (one entry per unique value),
/// and the data page is an RLE/Bit-Packing Hybrid stream of dictionary indices
/// prefixed by a 1-byte bit_width. Use dictionary_page() and indices_page()
/// after flush() to retrieve the encoded outputs.
///
/// @tparam T  The value type (std::string, int32_t, int64_t, float, or double).
///
/// @see DictionaryDecoder, RleEncoder
template <typename T>
class DictionaryEncoder {
public:
    /// Default-construct an empty dictionary encoder.
    DictionaryEncoder() = default;

    /// Add a value to the encoding stream.
    ///
    /// If @p value has not been seen before, it is assigned a fresh sequential
    /// dictionary index. The corresponding index is appended to the internal
    /// indices buffer regardless.
    ///
    /// @param value  The value to encode.
    void put(const T& value) {
        auto it = dict_map_.find(value);
        uint32_t index;
        if (it == dict_map_.end()) {
            index = static_cast<uint32_t>(dict_values_.size());
            dict_map_.emplace(value, index);
            dict_values_.push_back(value);
        } else {
            index = it->second;
        }
        indices_.push_back(index);
    }

    /// Finalize the encoding. Must be called after all put() calls.
    ///
    /// @note This is a no-op for DictionaryEncoder (indices are stored
    ///       incrementally). It exists for API symmetry with other encoders.
    void flush() {
        // Nothing to accumulate -- indices are stored incrementally.
        // This exists for API symmetry with other encoders.
    }

    /// Get the dictionary page as PLAIN-encoded unique values.
    ///
    /// Returns the raw bytes of the dictionary page, suitable for writing as a
    /// Parquet DICTIONARY_PAGE. Each entry is encoded per its type (BYTE_ARRAY
    /// for strings, fixed-width LE for numeric types).
    ///
    /// @return  PLAIN-encoded dictionary page bytes.
    /// @see indices_page
    [[nodiscard]] std::vector<uint8_t> dictionary_page() const {
        std::vector<uint8_t> buf;
        for (const auto& val : dict_values_) {
            detail::plain_encode_value(buf, val);
        }
        return buf;
    }

    /// Get the data page as RLE_DICTIONARY-encoded indices.
    ///
    /// Returns a byte buffer starting with a 1-byte bit_width followed by the
    /// RLE/Bit-Packing Hybrid encoded dictionary indices. This is the format
    /// expected by Parquet for RLE_DICTIONARY (encoding type 8) data pages.
    ///
    /// @return  Encoded indices page (1-byte bit_width prefix + RLE payload).
    /// @see dictionary_page, DictionaryDecoder::decode
    [[nodiscard]] std::vector<uint8_t> indices_page() const {
        int bw = bit_width();

        // RLE-encode the indices
        auto rle_payload = RleEncoder::encode(indices_.data(),
                                               indices_.size(), bw);

        // Prepend the bit_width byte
        std::vector<uint8_t> result;
        result.reserve(1 + rle_payload.size());
        result.push_back(static_cast<uint8_t>(bw));
        result.insert(result.end(), rle_payload.begin(), rle_payload.end());
        return result;
    }

    /// Number of unique values in the dictionary.
    ///
    /// @return  Dictionary cardinality.
    [[nodiscard]] size_t dictionary_size() const { return dict_values_.size(); }

    /// Total number of values encoded (including duplicates).
    ///
    /// @return  Total row count fed to put().
    [[nodiscard]] size_t num_values() const { return indices_.size(); }

    /// Bits per dictionary index (ceil(log2(dictionary_size))).
    ///
    /// @return  Bit width for index encoding (0 for single-entry dictionaries).
    [[nodiscard]] int bit_width() const {
        return detail::dict_bit_width(dict_values_.size());
    }

    /// Reset the encoder, clearing the dictionary, indices, and all internal state.
    ///
    /// After reset, the encoder can be reused for a new encoding session.
    void reset() {
        dict_map_.clear();
        dict_values_.clear();
        indices_.clear();
    }

    /// Heuristic check: is dictionary encoding worthwhile for this data?
    ///
    /// Returns @c true when fewer than 40% of values are unique (i.e., there
    /// is meaningful repetition). High-cardinality columns (many unique values
    /// relative to total rows) should fall back to PLAIN encoding.
    ///
    /// @return @c true if dictionary encoding provides good compression.
    [[nodiscard]] bool is_worthwhile() const {
        if (indices_.empty()) return false;
        return dict_values_.size() < indices_.size() * 4 / 10;
    }

private:
    std::unordered_map<T, uint32_t> dict_map_;   ///< Value-to-index lookup.
    std::vector<T>                  dict_values_; ///< Index-to-value mapping (insertion order).
    std::vector<uint32_t>           indices_;     ///< Per-row dictionary index.
};

// ===========================================================================
// DictionaryDecoder
// ===========================================================================
//
// Decodes a dictionary page (PLAIN-encoded values) and an indices page
// (bit_width byte + RLE-encoded indices) back to the original typed values.
//
// Usage:
//     DictionaryDecoder<std::string> dec(dict_data, dict_size, num_entries, type);
//     auto values = dec.decode(idx_data, idx_size, num_values);
//
/// Dictionary decoder for Parquet PLAIN_DICTIONARY / RLE_DICTIONARY encoding.
///
/// Reconstructs original typed values from a PLAIN-encoded dictionary page and
/// an RLE_DICTIONARY-encoded indices page. The constructor parses the dictionary
/// page; decode() then maps indices back to values.
///
/// @tparam T  The value type (std::string, int32_t, int64_t, float, or double).
///
/// @see DictionaryEncoder, RleDecoder
template <typename T>
class DictionaryDecoder {
public:
    /// Construct a decoder by parsing the raw PLAIN-encoded dictionary page.
    ///
    /// Decodes @p num_dict_entries values from @p dict_data using the
    /// appropriate PLAIN format for type @p T. The decoded values are stored
    /// internally for index-based lookup during decode().
    ///
    /// @param dict_data       Pointer to PLAIN-encoded dictionary page bytes.
    /// @param dict_size       Size of the dictionary page in bytes.
    /// @param num_dict_entries Number of entries in the dictionary.
    /// @param type            The Parquet physical type (retained for metadata;
    ///                        actual decoding dispatches on template type T).
    DictionaryDecoder(const uint8_t* dict_data, size_t dict_size,
                      size_t num_dict_entries, PhysicalType type)
        : type_(type)
    {
        // Decode dictionary entries from PLAIN-encoded data
        dict_values_.reserve(num_dict_entries);
        size_t pos = 0;
        for (size_t i = 0; i < num_dict_entries; ++i) {
            T* tag = nullptr;
            dict_values_.push_back(
                detail::plain_decode_value(dict_data, pos, dict_size, tag));
        }
    }

    /// Decode an RLE_DICTIONARY indices page into original typed values.
    ///
    /// Reads the 1-byte bit_width prefix, decodes the RLE/Bit-Packing Hybrid
    /// index stream, and maps each index back to its dictionary value. Returns
    /// an empty vector if any index is out of bounds.
    ///
    /// @param indices_data  Pointer to the indices page (1-byte bit_width + RLE payload).
    /// @param indices_size  Size of the indices page in bytes.
    /// @param num_values    Number of values to decode.
    /// @return              Decoded values, or empty on out-of-bounds index.
    /// @see DictionaryEncoder::indices_page
    [[nodiscard]] std::vector<T> decode(const uint8_t* indices_data,
                                         size_t indices_size,
                                         size_t num_values) const {
        if (indices_size == 0 || num_values == 0) return {};

        // First byte is the bit_width
        int bw = static_cast<int>(indices_data[0]);

        // Remaining bytes are the RLE-encoded indices
        auto indices = RleDecoder::decode(indices_data + 1,
                                           indices_size - 1,
                                           bw, num_values);

        // Map indices back to dictionary values
        std::vector<T> result;
        result.reserve(indices.size());
        for (uint32_t idx : indices) {
            if (static_cast<size_t>(idx) >= dict_values_.size()) return {};
            result.push_back(dict_values_[idx]);
        }
        return result;
    }

    /// Number of entries in the dictionary.
    ///
    /// @return  Dictionary cardinality (as parsed from the dictionary page).
    [[nodiscard]] size_t dictionary_size() const { return dict_values_.size(); }

private:
    PhysicalType    type_;         ///< The Parquet physical type for this column.
    std::vector<T>  dict_values_;  ///< Index-to-value mapping (parsed from dictionary page).
};

} // namespace signet::forge
