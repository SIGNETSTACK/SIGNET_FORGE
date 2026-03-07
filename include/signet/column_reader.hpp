// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file column_reader.hpp
/// @brief PLAIN-encoded Parquet column decoder.
///
/// Decodes values from a raw data page buffer using the PLAIN encoding.
/// All reads are bounds-checked and return @c expected<T> on failure.
/// Supports single-value and batch reads for every Parquet physical type.
///
/// PLAIN encoding rules (all little-endian on disk):
/// | Type                   | Layout                                    |
/// |------------------------|-------------------------------------------|
/// | BOOLEAN                | bit-packed, LSB first                     |
/// | INT32                  | 4 bytes LE                                |
/// | INT64                  | 8 bytes LE                                |
/// | FLOAT                  | 4 bytes LE (IEEE 754)                     |
/// | DOUBLE                 | 8 bytes LE (IEEE 754)                     |
/// | BYTE_ARRAY             | 4-byte LE length prefix + N bytes         |
/// | FIXED_LEN_BYTE_ARRAY   | @c type_length bytes (no length prefix)   |

#include "signet/types.hpp"
#include "signet/error.hpp"
#include "signet/memory.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace signet::forge {

/// PLAIN-encoded Parquet column decoder.
///
/// Wraps a raw data page buffer and decodes values one at a time or in
/// batches. The reader maintains a cursor position and a count of values
/// read, returning an error on type mismatch, buffer overrun, or exhaustion.
///
/// @note ColumnReader does **not** own the data buffer. The caller must
///       ensure the buffer remains valid for the reader's lifetime.
///
/// @see ColumnWriter (the encoding counterpart)
/// @see MmapParquetReader (constructs ColumnReaders over mmap'd pages)
class ColumnReader {
public:
    /// Construct a reader over raw PLAIN-encoded page data.
    ///
    /// @param type        The physical type of the column.
    /// @param data        Pointer to the start of the page data buffer.
    /// @param size        Size of the page data in bytes.
    /// @param num_values  Number of values encoded in this page.
    /// @param type_length For FIXED_LEN_BYTE_ARRAY columns, the fixed
    ///                    byte length per value (ignored for other types).
    ColumnReader(PhysicalType type,
                 const uint8_t* data,
                 size_t size,
                 int64_t num_values,
                 int32_t type_length = -1)
        : type_(type)
        , data_(data)
        , size_(size)
        , pos_(0)
        , num_values_(num_values)
        , values_read_(0)
        , type_length_(type_length)
        , bool_bit_offset_(0) {}

    // ===================================================================
    // Single-value reads
    // ===================================================================

    /// Read a single BOOLEAN value (bit-packed, LSB first).
    /// @return The decoded boolean, or an error on type mismatch / exhaustion.
    expected<bool> read_bool() {
        if (type_ != PhysicalType::BOOLEAN) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_bool() called on non-BOOLEAN column"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }

        size_t byte_index = bool_bit_offset_ / 8;
        size_t bit_index  = bool_bit_offset_ % 8;

        if (byte_index >= size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "boolean read past end of page data"};
        }

        bool val = (data_[byte_index] >> bit_index) & 1;
        ++bool_bit_offset_;
        ++values_read_;
        return val;
    }

    /// Read a single INT32 value (4 bytes little-endian).
    /// @return The decoded int32_t, or an error on type mismatch / exhaustion.
    expected<int32_t> read_int32() {
        if (type_ != PhysicalType::INT32) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_int32() called on non-INT32 column"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }
        if (pos_ + 4 > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "INT32 read past end of page data"};
        }

        int32_t val;
        std::memcpy(&val, data_ + pos_, 4);
        pos_ += 4;
        ++values_read_;
        return val;
    }

    /// Read a single INT64 value (8 bytes little-endian).
    /// @return The decoded int64_t, or an error on type mismatch / exhaustion.
    expected<int64_t> read_int64() {
        if (type_ != PhysicalType::INT64) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_int64() called on non-INT64 column"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }
        if (pos_ + 8 > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "INT64 read past end of page data"};
        }

        int64_t val;
        std::memcpy(&val, data_ + pos_, 8);
        pos_ += 8;
        ++values_read_;
        return val;
    }

    /// Read a single FLOAT value (4 bytes little-endian, IEEE 754).
    /// @return The decoded float, or an error on type mismatch / exhaustion.
    expected<float> read_float() {
        if (type_ != PhysicalType::FLOAT) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_float() called on non-FLOAT column"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }
        if (pos_ + 4 > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "FLOAT read past end of page data"};
        }

        float val;
        std::memcpy(&val, data_ + pos_, 4);
        pos_ += 4;
        ++values_read_;
        return val;
    }

    /// Read a single DOUBLE value (8 bytes little-endian, IEEE 754).
    /// @return The decoded double, or an error on type mismatch / exhaustion.
    expected<double> read_double() {
        if (type_ != PhysicalType::DOUBLE) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_double() called on non-DOUBLE column"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }
        if (pos_ + 8 > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "DOUBLE read past end of page data"};
        }

        double val;
        std::memcpy(&val, data_ + pos_, 8);
        pos_ += 8;
        ++values_read_;
        return val;
    }

    /// Read a single BYTE_ARRAY value as a std::string.
    ///
    /// PLAIN encoding: 4-byte LE length prefix followed by raw bytes.
    ///
    /// @return The decoded string, or an error on type mismatch / exhaustion.
    expected<std::string> read_string() {
        if (type_ != PhysicalType::BYTE_ARRAY) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_string() called on non-BYTE_ARRAY column"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }
        if (pos_ + 4 > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "BYTE_ARRAY length prefix read past end of page data"};
        }

        uint32_t len;
        std::memcpy(&len, data_ + pos_, 4);

        // Bounds check before advancing pos_: use subtraction to avoid
        // pos_+len wraparound on crafted files (CWE-125, CWE-190).
        if (static_cast<size_t>(len) > size_ - pos_ - 4) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "BYTE_ARRAY data read past end of page data"};
        }
        pos_ += 4;

        std::string val(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        ++values_read_;
        return val;
    }

    /// Read a single BYTE_ARRAY value as a non-owning std::string_view.
    ///
    /// The returned view points directly into the page data buffer, so
    /// it is only valid as long as the underlying buffer is alive.
    ///
    /// @return A string_view into the page data, or an error.
    expected<std::string_view> read_string_view() {
        if (type_ != PhysicalType::BYTE_ARRAY) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_string_view() called on non-BYTE_ARRAY column"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }
        if (pos_ + 4 > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "BYTE_ARRAY length prefix read past end of page data"};
        }

        uint32_t len;
        std::memcpy(&len, data_ + pos_, 4);

        // Bounds check before advancing pos_: use subtraction to avoid
        // pos_+len wraparound on crafted files (CWE-125, CWE-190).
        if (static_cast<size_t>(len) > size_ - pos_ - 4) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "BYTE_ARRAY data read past end of page data"};
        }
        pos_ += 4;

        std::string_view val(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        ++values_read_;
        return val;
    }

    /// Read a single BYTE_ARRAY or FIXED_LEN_BYTE_ARRAY value as raw bytes.
    ///
    /// For BYTE_ARRAY, reads a 4-byte LE length prefix then the payload.
    /// For FIXED_LEN_BYTE_ARRAY, reads exactly @c type_length bytes.
    ///
    /// @return A vector of the raw bytes, or an error.
    expected<std::vector<uint8_t>> read_bytes() {
        if (type_ != PhysicalType::BYTE_ARRAY &&
            type_ != PhysicalType::FIXED_LEN_BYTE_ARRAY) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_bytes() requires BYTE_ARRAY or FIXED_LEN_BYTE_ARRAY"};
        }
        if (values_read_ >= num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE, "no more values to read"};
        }

        if (type_ == PhysicalType::FIXED_LEN_BYTE_ARRAY) {
            if (type_length_ <= 0) {
                return Error{ErrorCode::SCHEMA_MISMATCH,
                             "FIXED_LEN_BYTE_ARRAY requires positive type_length"};
            }
            size_t len = static_cast<size_t>(type_length_);
            if (pos_ + len > size_) {
                return Error{ErrorCode::CORRUPT_PAGE,
                             "FIXED_LEN_BYTE_ARRAY read past end of page data"};
            }
            std::vector<uint8_t> val(data_ + pos_, data_ + pos_ + len);
            pos_ += len;
            ++values_read_;
            return val;
        }

        // BYTE_ARRAY: 4-byte LE length prefix
        if (pos_ + 4 > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "BYTE_ARRAY length prefix read past end of page data"};
        }
        uint32_t len;
        std::memcpy(&len, data_ + pos_, 4);

        // Bounds check before advancing pos_: use subtraction to avoid
        // pos_+len wraparound on crafted files (CWE-125, CWE-190).
        if (static_cast<size_t>(len) > size_ - pos_ - 4) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "BYTE_ARRAY data read past end of page data"};
        }
        pos_ += 4;
        std::vector<uint8_t> val(data_ + pos_, data_ + pos_ + len);
        pos_ += len;
        ++values_read_;
        return val;
    }

    // ===================================================================
    // Batch reads -- read @p count values into a caller-provided buffer
    // ===================================================================

    /// Read a batch of BOOLEAN values into @p out.
    /// @param out    Pre-allocated buffer of at least @p count elements.
    /// @param count  Number of values to read.
    /// @return Void on success, or an error.
    expected<void> read_batch_bool(bool* out, size_t count) {
        if (type_ != PhysicalType::BOOLEAN) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_batch_bool() called on non-BOOLEAN column"};
        }
        if (values_read_ + static_cast<int64_t>(count) > num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "batch read exceeds available values"};
        }

        for (size_t i = 0; i < count; ++i) {
            size_t byte_index = bool_bit_offset_ / 8;
            size_t bit_index  = bool_bit_offset_ % 8;

            if (byte_index >= size_) {
                return Error{ErrorCode::CORRUPT_PAGE,
                             "boolean batch read past end of page data"};
            }

            out[i] = (data_[byte_index] >> bit_index) & 1;
            ++bool_bit_offset_;
            ++values_read_;
        }
        return expected<void>{};
    }

    /// Read a batch of INT32 values via bulk memcpy.
    /// @param out    Pre-allocated buffer of at least @p count elements.
    /// @param count  Number of values to read.
    /// @return Void on success, or an error.
    expected<void> read_batch_int32(int32_t* out, size_t count) {
        if (type_ != PhysicalType::INT32) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_batch_int32() called on non-INT32 column"};
        }
        if (count > SIZE_MAX / 4) {
            return Error{ErrorCode::OUT_OF_RANGE, "count too large, would overflow"};
        }
        size_t total_bytes = count * 4;
        if (values_read_ + static_cast<int64_t>(count) > num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "batch read exceeds available values"};
        }
        if (pos_ + total_bytes > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "INT32 batch read past end of page data"};
        }

        std::memcpy(out, data_ + pos_, total_bytes);
        pos_ += total_bytes;
        values_read_ += static_cast<int64_t>(count);
        return expected<void>{};
    }

    /// Read a batch of INT64 values via bulk memcpy.
    /// @param out    Pre-allocated buffer of at least @p count elements.
    /// @param count  Number of values to read.
    /// @return Void on success, or an error.
    expected<void> read_batch_int64(int64_t* out, size_t count) {
        if (type_ != PhysicalType::INT64) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_batch_int64() called on non-INT64 column"};
        }
        if (count > SIZE_MAX / 8) {
            return Error{ErrorCode::OUT_OF_RANGE, "count too large, would overflow"};
        }
        size_t total_bytes = count * 8;
        if (values_read_ + static_cast<int64_t>(count) > num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "batch read exceeds available values"};
        }
        if (pos_ + total_bytes > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "INT64 batch read past end of page data"};
        }

        std::memcpy(out, data_ + pos_, total_bytes);
        pos_ += total_bytes;
        values_read_ += static_cast<int64_t>(count);
        return expected<void>{};
    }

    /// Read a batch of FLOAT values via bulk memcpy.
    /// @param out    Pre-allocated buffer of at least @p count elements.
    /// @param count  Number of values to read.
    /// @return Void on success, or an error.
    expected<void> read_batch_float(float* out, size_t count) {
        if (type_ != PhysicalType::FLOAT) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_batch_float() called on non-FLOAT column"};
        }
        if (count > SIZE_MAX / 4) {
            return Error{ErrorCode::OUT_OF_RANGE, "count too large, would overflow"};
        }
        size_t total_bytes = count * 4;
        if (values_read_ + static_cast<int64_t>(count) > num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "batch read exceeds available values"};
        }
        if (pos_ + total_bytes > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "FLOAT batch read past end of page data"};
        }

        std::memcpy(out, data_ + pos_, total_bytes);
        pos_ += total_bytes;
        values_read_ += static_cast<int64_t>(count);
        return expected<void>{};
    }

    /// Read a batch of DOUBLE values via bulk memcpy.
    /// @param out    Pre-allocated buffer of at least @p count elements.
    /// @param count  Number of values to read.
    /// @return Void on success, or an error.
    expected<void> read_batch_double(double* out, size_t count) {
        if (type_ != PhysicalType::DOUBLE) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_batch_double() called on non-DOUBLE column"};
        }
        if (count > SIZE_MAX / 8) {
            return Error{ErrorCode::OUT_OF_RANGE, "count too large, would overflow"};
        }
        size_t total_bytes = count * 8;
        if (values_read_ + static_cast<int64_t>(count) > num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "batch read exceeds available values"};
        }
        if (pos_ + total_bytes > size_) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "DOUBLE batch read past end of page data"};
        }

        std::memcpy(out, data_ + pos_, total_bytes);
        pos_ += total_bytes;
        values_read_ += static_cast<int64_t>(count);
        return expected<void>{};
    }

    /// Read a batch of BYTE_ARRAY values as strings.
    /// @param out    Pre-allocated buffer of at least @p count std::string elements.
    /// @param count  Number of values to read.
    /// @return Void on success, or an error.
    expected<void> read_batch_string(std::string* out, size_t count) {
        if (type_ != PhysicalType::BYTE_ARRAY) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "read_batch_string() called on non-BYTE_ARRAY column"};
        }
        if (values_read_ + static_cast<int64_t>(count) > num_values_) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "batch read exceeds available values"};
        }

        for (size_t i = 0; i < count; ++i) {
            if (pos_ + 4 > size_) {
                return Error{ErrorCode::CORRUPT_PAGE,
                             "BYTE_ARRAY length prefix read past end of page data"};
            }
            uint32_t len;
            std::memcpy(&len, data_ + pos_, 4);
            pos_ += 4;

            if (pos_ + len > size_) {
                return Error{ErrorCode::CORRUPT_PAGE,
                             "BYTE_ARRAY data read past end of page data"};
            }
            out[i].assign(reinterpret_cast<const char*>(data_ + pos_), len);
            pos_ += len;
            ++values_read_;
        }
        return expected<void>{};
    }

    // ===================================================================
    // Template dispatch -- read<T>() and read_batch<T>()
    // ===================================================================

    /// Read a single value of type @c T, dispatching to the correct typed reader.
    ///
    /// Supported types: @c bool, @c int32_t, @c int64_t, @c float, @c double,
    /// @c std::string, @c std::string_view, @c std::vector<uint8_t>.
    ///
    /// @tparam T  The value type to decode.
    /// @return The decoded value, or an error.
    template <typename T>
    expected<T> read() {
        if constexpr (std::is_same_v<T, bool>) {
            return read_bool();
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return read_int32();
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return read_int64();
        } else if constexpr (std::is_same_v<T, float>) {
            return read_float();
        } else if constexpr (std::is_same_v<T, double>) {
            return read_double();
        } else if constexpr (std::is_same_v<T, std::string>) {
            return read_string();
        } else if constexpr (std::is_same_v<T, std::string_view>) {
            return read_string_view();
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            return read_bytes();
        } else {
            static_assert(!std::is_same_v<T, T>,
                          "ColumnReader::read<T>: unsupported type");
        }
    }

    /// Read a batch of @p count values of type @c T into @p out.
    ///
    /// Dispatches to the correct typed batch reader. Supported types:
    /// @c bool, @c int32_t, @c int64_t, @c float, @c double, @c std::string.
    ///
    /// @tparam T      The value type to decode.
    /// @param  out    Pre-allocated buffer of at least @p count elements.
    /// @param  count  Number of values to read.
    /// @return Void on success, or an error.
    template <typename T>
    expected<void> read_batch(T* out, size_t count) {
        if constexpr (std::is_same_v<T, bool>) {
            return read_batch_bool(out, count);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return read_batch_int32(out, count);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return read_batch_int64(out, count);
        } else if constexpr (std::is_same_v<T, float>) {
            return read_batch_float(out, count);
        } else if constexpr (std::is_same_v<T, double>) {
            return read_batch_double(out, count);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return read_batch_string(out, count);
        } else {
            static_assert(!std::is_same_v<T, T>,
                          "ColumnReader::read_batch<T>: unsupported type");
        }
    }

    // ===================================================================
    // Status queries
    // ===================================================================

    /// Number of values not yet read from this page.
    [[nodiscard]] int64_t values_remaining() const {
        return num_values_ - values_read_;
    }

    /// Whether there is at least one more value to read.
    [[nodiscard]] bool has_next() const {
        return values_read_ < num_values_;
    }

    /// The Parquet physical type of this column.
    [[nodiscard]] PhysicalType type() const {
        return type_;
    }

    /// Current byte offset within the page data buffer.
    [[nodiscard]] size_t position() const {
        return pos_;
    }

private:
    PhysicalType    type_;
    const uint8_t*  data_;
    size_t          size_;
    size_t          pos_;
    int64_t         num_values_;
    int64_t         values_read_;
    int32_t         type_length_;

    // For BOOLEAN: bit offset within the data buffer (since booleans are
    // bit-packed rather than byte-aligned)
    size_t          bool_bit_offset_;
};

} // namespace signet::forge
