// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file column_writer.hpp
/// @brief PLAIN encoding writer for all Parquet physical types.
///
/// Header-only. Encodes column data using the PLAIN encoding format,
/// tracking ColumnStatistics (min/max/null_count) as values are written.
/// Supports all 7 Parquet physical types: BOOLEAN, INT32, INT64, FLOAT,
/// DOUBLE, BYTE_ARRAY, and FIXED_LEN_BYTE_ARRAY.

#include "signet/types.hpp"
#include "signet/statistics.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// Little-endian append helpers
// ---------------------------------------------------------------------------

/// Append a uint32_t in little-endian byte order to a byte buffer.
/// @param buf  The destination byte buffer.
/// @param val  The 32-bit value to append (4 bytes).
inline void append_le32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back(static_cast<uint8_t>((val      ) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

/// Append a uint64_t in little-endian byte order to a byte buffer.
/// @param buf  The destination byte buffer.
/// @param val  The 64-bit value to append (8 bytes).
inline void append_le64(std::vector<uint8_t>& buf, uint64_t val) {
    buf.push_back(static_cast<uint8_t>((val      ) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 32) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 40) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 48) & 0xFF));
    buf.push_back(static_cast<uint8_t>((val >> 56) & 0xFF));
}

/// PLAIN encoding writer for a single Parquet column.
///
/// Encodes values into an internal byte buffer using the PLAIN encoding
/// and simultaneously maintains ColumnStatistics. After all values have
/// been written, call data() to retrieve the encoded buffer and
/// statistics() for the accumulated column-chunk statistics.
///
/// @note The writer does not enforce type safety at the column level --
///       it is the caller's responsibility to use the correct write method
///       for the configured PhysicalType.
///
/// @see ColumnReader   (the decoding counterpart)
/// @see ColumnStatistics
class ColumnWriter {
public:
    /// Construct a writer for the given Parquet physical type.
    /// @param type         The physical type this writer will encode.
    /// @param type_length  For FIXED_LEN_BYTE_ARRAY columns, the fixed byte
    ///                     length per value (from schema). Ignored for other types.
    explicit ColumnWriter(PhysicalType type, int32_t type_length = -1)
        : type_(type), type_length_(type_length), num_values_(0) {}

    // -- Typed write methods --------------------------------------------------

    /// Write a single boolean value.
    ///
    /// PLAIN encoding: bit-packed, LSB first. Bit i of byte (i/8) is set if
    /// value[i] is true. N values produce ceil(N/8) bytes.
    ///
    /// @param val  The boolean to encode.
    void write_bool(bool val) {
        size_t bit_index = static_cast<size_t>(num_values_);
        size_t byte_index = bit_index / 8;
        size_t bit_offset = bit_index % 8;

        // Extend the buffer if we need a new byte
        if (byte_index >= buf_.size()) {
            buf_.push_back(0);
        }

        if (val) {
            buf_[byte_index] |= static_cast<uint8_t>(1u << bit_offset);
        }

        stats_.update(val);
        ++num_values_;
    }

    /// Write a single INT32 value (4 bytes little-endian).
    /// @param val  The 32-bit integer to encode.
    void write_int32(int32_t val) {
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        append_le32(buf_, bits);

        stats_.update(val);
        ++num_values_;
    }

    /// Write a single INT64 value (8 bytes little-endian).
    /// @param val  The 64-bit integer to encode.
    void write_int64(int64_t val) {
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        append_le64(buf_, bits);

        stats_.update(val);
        ++num_values_;
    }

    /// Write a single FLOAT value (4 bytes little-endian, IEEE 754).
    /// @param val  The float to encode.
    void write_float(float val) {
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        append_le32(buf_, bits);

        stats_.update(val);
        ++num_values_;
    }

    /// Write a single DOUBLE value (8 bytes little-endian, IEEE 754).
    /// @param val  The double to encode.
    void write_double(double val) {
        uint64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        append_le64(buf_, bits);

        stats_.update(val);
        ++num_values_;
    }

    /// Write a single BYTE_ARRAY value from a std::string.
    ///
    /// PLAIN encoding: 4-byte LE length prefix + raw bytes.
    ///
    /// @param val  The string whose bytes are encoded.
    void write_byte_array(const std::string& val) {
        write_byte_array(reinterpret_cast<const uint8_t*>(val.data()), val.size());
    }

    /// Write a single BYTE_ARRAY value from raw bytes.
    ///
    /// PLAIN encoding: 4-byte LE length prefix + raw bytes.
    ///
    /// @param data  Pointer to the raw byte payload.
    /// @param len   Number of bytes in the payload.
    void write_byte_array(const uint8_t* data, size_t len) {
        // CWE-190: Integer Overflow — Parquet BYTE_ARRAY uses 4-byte LE length prefix;
        // payloads > 4 GiB would truncate the length field, corrupting the output.
        if (len > static_cast<size_t>(UINT32_MAX)) {
            // H14: BYTE_ARRAY length prefix is a 4-byte LE uint32; reject > 4 GiB payloads.
            // Throw instead of silently dropping the value, which would corrupt the output.
            throw std::length_error("BYTE_ARRAY value exceeds 4 GiB limit");
        }
        append_le32(buf_, static_cast<uint32_t>(len));
        buf_.insert(buf_.end(), data, data + len);

        // Update statistics with the string representation
        std::string str_val(reinterpret_cast<const char*>(data), len);
        stats_.update(str_val);
        ++num_values_;
    }

    /// Write a single FIXED_LEN_BYTE_ARRAY value from raw bytes.
    ///
    /// PLAIN encoding: raw bytes only (no length prefix -- length is in the schema).
    ///
    /// @pre @p len must exactly match the schema's @c type_length. Passing a
    ///      different length produces a corrupt column chunk (no runtime check
    ///      is performed here for performance -- the caller must validate).
    ///
    /// @param data  Pointer to the raw byte payload.
    /// @param len   Number of bytes (must match the schema's @c type_length).
    void write_fixed_len_byte_array(const uint8_t* data, size_t len) {
        // CWE-130: Improper Handling of Length Parameter Inconsistency —
        // Validate that len matches the schema's type_length to prevent
        // silent data corruption in the column chunk.
        if (type_length_ > 0 && len != static_cast<size_t>(type_length_)) {
            throw std::length_error(
                "FIXED_LEN_BYTE_ARRAY value length " + std::to_string(len) +
                " != schema type_length " + std::to_string(type_length_));
        }
        buf_.insert(buf_.end(), data, data + len);

        // Update statistics with the string representation
        std::string str_val(reinterpret_cast<const char*>(data), len);
        stats_.update(str_val);
        ++num_values_;
    }

    // -- Template convenience dispatchers ------------------------------------

    /// Write a single value, dispatching to the correct typed write method.
    ///
    /// @tparam T  Supported: @c bool, @c int32_t, @c int64_t, @c float,
    ///            @c double, @c std::string.
    /// @param val  The value to encode.
    template <typename T>
    void write(const T& val) {
        if constexpr (std::is_same_v<T, bool>) {
            write_bool(val);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            write_int32(val);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            write_int64(val);
        } else if constexpr (std::is_same_v<T, float>) {
            write_float(val);
        } else if constexpr (std::is_same_v<T, double>) {
            write_double(val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            write_byte_array(val);
        } else {
            static_assert(!std::is_same_v<T, T>,
                          "ColumnWriter::write: unsupported type");
        }
    }

    // -- Batch write methods -------------------------------------------------

    /// Write a batch of typed values.
    ///
    /// @tparam T       The value type (same constraints as write<T>()).
    /// @param  values  Pointer to a contiguous array of @p count values.
    /// @param  count   Number of values to write.
    template <typename T>
    void write_batch(const T* values, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            write<T>(values[i]);
        }
    }

    /// Write a batch of string values (BYTE_ARRAY).
    /// @param values  Pointer to a contiguous array of @p count strings.
    /// @param count   Number of strings to write.
    void write_batch(const std::string* values, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            write_byte_array(values[i]);
        }
    }

    // -- Access encoded data -------------------------------------------------

    /// Returns a const reference to the encoded byte buffer.
    [[nodiscard]] const std::vector<uint8_t>& data() const { return buf_; }

    /// Returns the total encoded data size in bytes.
    [[nodiscard]] size_t encoded_size() const { return buf_.size(); }

    /// Returns the number of values written so far.
    [[nodiscard]] int64_t num_values() const { return num_values_; }

    // -- Statistics -----------------------------------------------------------

    /// Returns a const reference to the column statistics.
    [[nodiscard]] const ColumnStatistics& statistics() const { return stats_; }

    // -- Reset ----------------------------------------------------------------

    /// Reset the writer for the next column chunk. Clears all data and statistics.
    void reset() {
        buf_.clear();
        stats_.reset();
        num_values_ = 0;
    }

    /// Returns the physical type this writer encodes.
    [[nodiscard]] PhysicalType type() const { return type_; }

private:
    PhysicalType           type_;
    int32_t                type_length_;  ///< FIXED_LEN_BYTE_ARRAY byte length (-1 = N/A).
    std::vector<uint8_t>   buf_;
    ColumnStatistics       stats_;
    int64_t                num_values_;
};

} // namespace signet::forge
