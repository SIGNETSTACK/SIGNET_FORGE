// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file statistics.hpp
/// @brief Per-column-chunk statistics tracker and little-endian byte helpers.
///
/// Provides ColumnStatistics for tracking min/max/null_count/num_values during
/// column writing, plus @c to_le_bytes() / @c from_le_bytes() utilities for
/// converting arithmetic types to their little-endian byte representation.

#include "signet/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace signet::forge {

/// Convert an arithmetic value to its little-endian byte representation.
///
/// On little-endian platforms (x86, ARM) this is a straight memcpy.
/// On big-endian platforms the bytes are reversed.
///
/// @tparam T  An arithmetic type (int32_t, double, etc.).
/// @param  value  The value to convert.
/// @return A vector of @c sizeof(T) bytes in little-endian order.
template <typename T>
[[nodiscard]] inline std::vector<uint8_t> to_le_bytes(T value) {
    static_assert(std::is_arithmetic_v<T>, "to_le_bytes requires an arithmetic type");

    std::vector<uint8_t> bytes(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));

    // If this platform is big-endian, reverse the bytes.
    // On little-endian (x86, ARM), this is a no-op at compile time.
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    std::reverse(bytes.begin(), bytes.end());
#endif

    return bytes;
}

/// Overload for std::string -- returns raw bytes (no endian conversion needed).
///
/// @param value  The string whose bytes are copied verbatim.
/// @return A vector containing the string's byte content.
[[nodiscard]] inline std::vector<uint8_t> to_le_bytes(const std::string& value) {
    return {value.begin(), value.end()};
}

/// Reconstruct an arithmetic value from its little-endian byte representation.
///
/// If @p bytes contains fewer than @c sizeof(T) bytes the result is
/// value-initialized (zero).
///
/// @tparam T      An arithmetic type.
/// @param  bytes  At least @c sizeof(T) bytes in little-endian order.
/// @return The reconstructed value.
template <typename T>
[[nodiscard]] inline T from_le_bytes(const std::vector<uint8_t>& bytes) {
    static_assert(std::is_arithmetic_v<T>, "from_le_bytes requires an arithmetic type");

    T value{};
    if (bytes.size() >= sizeof(T)) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        std::vector<uint8_t> tmp(bytes.begin(), bytes.begin() + sizeof(T));
        std::reverse(tmp.begin(), tmp.end());
        std::memcpy(&value, tmp.data(), sizeof(T));
#else
        std::memcpy(&value, bytes.data(), sizeof(T));
#endif
    }
    return value;
}

/// Per-column-chunk statistics tracker.
///
/// Tracks min/max values (stored as little-endian byte vectors), null count,
/// value count, and an optional distinct count. Values of any supported
/// Parquet physical type can be fed to update(); min/max are maintained via
/// typed comparison (not raw byte comparison for numeric types).
///
/// ColumnStatistics instances can be merged to combine page-level statistics
/// into chunk-level statistics.
///
/// @see ColumnWriter (primary producer of statistics)
/// @see ColumnIndex  (consumes min/max per page for predicate pushdown)
class ColumnStatistics {
public:
    /// Default constructor -- initializes all counters to zero.
    ColumnStatistics() { reset(); }

    // -- Core update methods ---------------------------------------------------

    /// Update statistics with a non-null typed value.
    ///
    /// Dispatches to the appropriate internal updater based on @c T.
    /// Supported types: @c bool, @c float, @c double, @c std::string,
    /// and all other arithmetic types (int32_t, int64_t, etc.).
    ///
    /// @tparam T  The value type (must be arithmetic or std::string).
    /// @param value  The non-null value to incorporate.
    template <typename T>
    void update(const T& value) {
        if constexpr (std::is_same_v<T, bool>) {
            update_numeric(static_cast<uint8_t>(value ? 1 : 0));
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            // NaN values do not count toward num_values_ (Parquet spec S2.4)
            if (std::isnan(value)) return;
            update_float(value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            update_string(value);
        } else if constexpr (std::is_arithmetic_v<T>) {
            update_numeric(value);
        } else {
            static_assert(!std::is_same_v<T, T>,
                          "ColumnStatistics::update: unsupported type");
        }
        ++num_values_;
    }

    /// Record a null value (increments null count only, no min/max update).
    void update_null() {
        ++null_count_;
    }

    /// Reset all statistics to initial state.
    void reset() {
        null_count_     = 0;
        num_values_     = 0;
        distinct_count_ = std::nullopt;
        min_value_.clear();
        max_value_.clear();
        has_min_max_    = false;
    }

    // -- Accessors -------------------------------------------------------------

    /// Number of null values recorded.
    [[nodiscard]] int64_t null_count()  const { return null_count_; }
    /// Number of non-null values recorded.
    [[nodiscard]] int64_t num_values()  const { return num_values_; }
    /// Optional distinct-value count (invalidated on merge).
    [[nodiscard]] std::optional<int64_t> distinct_count() const { return distinct_count_; }
    /// Whether at least one non-null value has been recorded (min/max valid).
    [[nodiscard]] bool    has_min_max() const { return has_min_max_; }

    /// Raw little-endian bytes of the minimum value.
    [[nodiscard]] const std::vector<uint8_t>& min_bytes() const { return min_value_; }
    /// Raw little-endian bytes of the maximum value.
    [[nodiscard]] const std::vector<uint8_t>& max_bytes() const { return max_value_; }

    /// Reconstruct the typed minimum value from stored bytes.
    /// @tparam T  The original value type used during update().
    /// @note Caller is responsible for matching T to the PhysicalType tracked by type_.
    ///       Mismatched types produce undefined byte-reinterpretation (CWE-843).
    template <typename T>
    [[nodiscard]] T min_as() const {
        if constexpr (std::is_same_v<T, bool>) {
            return min_value_.empty() ? false : (min_value_[0] != 0);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::string(min_value_.begin(), min_value_.end());
        } else {
            return from_le_bytes<T>(min_value_);
        }
    }

    /// Reconstruct the typed maximum value from stored bytes.
    /// @tparam T  The original value type used during update().
    /// @note Caller is responsible for matching T to the PhysicalType tracked by type_.
    ///       Mismatched types produce undefined byte-reinterpretation (CWE-843).
    template <typename T>
    [[nodiscard]] T max_as() const {
        if constexpr (std::is_same_v<T, bool>) {
            return max_value_.empty() ? false : (max_value_[0] != 0);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return std::string(max_value_.begin(), max_value_.end());
        } else {
            return from_le_bytes<T>(max_value_);
        }
    }

    // -- Mutators for optional fields ------------------------------------------

    /// Set the distinct-value count (e.g. from a dictionary encoder).
    /// @param count  The number of distinct values.
    void set_distinct_count(int64_t count) { distinct_count_ = count; }

    /// Set the physical type for type-aware min/max comparison during merge.
    /// @param t  The Parquet physical type of the column.
    void set_type(PhysicalType t) { type_ = t; }

    /// Get the physical type associated with these statistics.
    [[nodiscard]] PhysicalType type() const { return type_; }

    // -- Merge two statistics (useful for combining page stats into chunk stats) -

    /// Merge another ColumnStatistics into this one.
    ///
    /// Null counts and value counts are summed. Min/max are updated via
    /// byte-level comparison (the caller must ensure both instances track
    /// the same column/type). Distinct count is invalidated on merge because
    /// the union cannot be computed without the full distinct set.
    ///
    /// @param other  The statistics to merge into this instance.
    void merge(const ColumnStatistics& other) {
        null_count_ += other.null_count_;
        num_values_ += other.num_values_;

        if (other.has_min_max_) {
            if (!has_min_max_) {
                min_value_  = other.min_value_;
                max_value_  = other.max_value_;
                has_min_max_ = true;
            } else {
                // Use typed comparison for numeric types (CWE-697: incorrect comparison)
                merge_min_max(other.min_value_, other.max_value_);
            }
        }

        // distinct_count cannot be merged without a full distinct set
        if (distinct_count_.has_value() || other.distinct_count_.has_value()) {
            distinct_count_ = std::nullopt; // invalidate on merge
        }
    }

private:
    int64_t                null_count_     = 0;
    int64_t                num_values_     = 0;
    std::optional<int64_t> distinct_count_;
    std::vector<uint8_t>   min_value_;
    std::vector<uint8_t>   max_value_;
    bool                   has_min_max_    = false;
    PhysicalType           type_           = PhysicalType::INT32;

    // -- Typed merge helpers (used by merge()) -----------------------------------

    /// Merge min/max using physical-type-aware comparison.
    void merge_min_max(const std::vector<uint8_t>& other_min,
                       const std::vector<uint8_t>& other_max) {
        switch (type_) {
            case PhysicalType::INT32:
                typed_merge<int32_t>(other_min, other_max); break;
            case PhysicalType::INT64:
                typed_merge<int64_t>(other_min, other_max); break;
            case PhysicalType::FLOAT:
                typed_merge<float>(other_min, other_max); break;
            case PhysicalType::DOUBLE:
                typed_merge<double>(other_min, other_max); break;
            default:
                // BOOLEAN, BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY: lexicographic is correct
                if (other_min < min_value_) min_value_ = other_min;
                if (other_max > max_value_) max_value_ = other_max;
                break;
        }
    }

    /// Typed comparison for merge: reconstitute native values from LE bytes.
    template <typename T>
    void typed_merge(const std::vector<uint8_t>& other_min_bytes,
                     const std::vector<uint8_t>& other_max_bytes) {
        T cur_min = from_le_bytes<T>(min_value_);
        T cur_max = from_le_bytes<T>(max_value_);
        T o_min   = from_le_bytes<T>(other_min_bytes);
        T o_max   = from_le_bytes<T>(other_max_bytes);
        if (o_min < cur_min) min_value_ = other_min_bytes;
        if (o_max > cur_max) max_value_ = other_max_bytes;
    }

    // -- Internal update helpers -----------------------------------------------

    /// Update for integer types (int32_t, int64_t, uint8_t for bool).
    template <typename T>
    void update_numeric(T value) {
        auto bytes = to_le_bytes(value);

        if (!has_min_max_) {
            min_value_   = bytes;
            max_value_   = bytes;
            has_min_max_ = true;
            return;
        }

        // Compare as native typed values for correctness (signed vs unsigned)
        T current_min = from_le_bytes<T>(min_value_);
        T current_max = from_le_bytes<T>(max_value_);

        if (value < current_min) {
            min_value_ = bytes;
        }
        if (value > current_max) {
            max_value_ = bytes;
        }
    }

    /// Update for float/double with NaN handling.
    template <typename T>
    void update_float(T value) {
        // Skip NaN values entirely — they do not participate in min/max
        if (std::isnan(value)) {
            return;
        }

        auto bytes = to_le_bytes(value);

        if (!has_min_max_) {
            min_value_   = bytes;
            max_value_   = bytes;
            has_min_max_ = true;
            return;
        }

        T current_min = from_le_bytes<T>(min_value_);
        T current_max = from_le_bytes<T>(max_value_);

        if (value < current_min) {
            min_value_ = bytes;
        }
        if (value > current_max) {
            max_value_ = bytes;
        }
    }

    /// Update for std::string — lexicographic comparison, raw byte storage.
    void update_string(const std::string& value) {
        auto bytes = to_le_bytes(value);

        if (!has_min_max_) {
            min_value_   = bytes;
            max_value_   = bytes;
            has_min_max_ = true;
            return;
        }

        // Lexicographic comparison on raw bytes (equivalent to std::string comparison)
        if (bytes < min_value_) {
            min_value_ = bytes;
        }
        if (bytes > max_value_) {
            max_value_ = bytes;
        }
    }
};

} // namespace signet::forge
