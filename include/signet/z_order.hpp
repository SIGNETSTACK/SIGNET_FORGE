// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file z_order.hpp
/// @brief Z-order curve (Morton code) encoding/decoding for multi-dimensional indexing.
///
/// Provides three layers of functionality:
///   1. **Value normalization** -- type-specific mappings that preserve sort order
///      when the result is interpreted as an unsigned integer.
///   2. **Morton code bit-interleaving** -- 2D fast path (uint64_t key) and
///      N-dimensional generalized path (byte-array key) for 2-8 columns.
///   3. **ZOrderSorter** -- computes a permutation vector that reorders rows
///      by their Z-order (Morton) key, improving spatial locality for
///      multi-column range queries and enabling better data skipping through
///      column/page min-max statistics.
///
/// @code
///   std::vector<ZOrderColumn> cols = {
///       { PhysicalType::INT32, int_data.data(), int_data.size() },
///       { PhysicalType::FLOAT, float_data.data(), float_data.size() }
///   };
///   auto perm = ZOrderSorter::sort(num_rows, cols);
///   // Apply perm to reorder rows before writing to Parquet.
/// @endcode
///
/// @see SplitBlockBloomFilter for another data-skipping technique.

#include "signet/types.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// Z-order curve (Morton code) utilities for spatial sort keys.
namespace signet::forge::z_order {

// ===========================================================================
// Value normalization -- preserve sort order as unsigned integers
// ===========================================================================

/// Normalize a signed 32-bit integer to an unsigned 32-bit integer that
/// preserves the original sort order (flip the sign bit).
/// @param v Signed input value.
/// @return Order-preserving unsigned representation.
inline uint32_t normalize_int32(int32_t v) {
    return static_cast<uint32_t>(v) ^ 0x80000000u;
}

/// Normalize a signed 64-bit integer to uint64_t (flip sign bit).
/// @param v Signed input value.
/// @return Order-preserving unsigned representation.
inline uint64_t normalize_int64(int64_t v) {
    return static_cast<uint64_t>(v) ^ 0x8000000000000000ULL;
}

/// Normalize a 32-bit float to uint32_t preserving total order.
///
/// Uses the IEEE 754 sign-magnitude trick: negative floats have all bits
/// flipped; non-negative floats have only the sign bit flipped. The result
/// sorts identically to the original float values when compared as unsigned.
///
/// @param v Float input value.
/// @return Order-preserving unsigned 32-bit representation.
inline uint32_t normalize_float(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    uint32_t mask = -static_cast<int32_t>(bits >> 31) | 0x80000000u;
    return bits ^ mask;
}

/// Normalize a 64-bit double to uint64_t preserving total order.
///
/// Same IEEE 754 sign-magnitude trick as normalize_float(), but for doubles.
///
/// @param v Double input value.
/// @return Order-preserving unsigned 64-bit representation.
inline uint64_t normalize_double(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    uint64_t mask = -static_cast<int64_t>(bits >> 63) | 0x8000000000000000ULL;
    return bits ^ mask;
}

/// Normalize a string to uint32_t by taking the first 4 bytes in big-endian order.
///
/// Strings shorter than 4 bytes are zero-padded on the right, so "AB" becomes
/// `0x41420000`. This provides a coarse sort-preserving key suitable for
/// Morton interleaving (lexicographic order is preserved for the first 4 chars).
///
/// @param v String input (only the first 4 bytes are used).
/// @return 32-bit sort key derived from the string prefix.
inline uint32_t normalize_string(std::string_view v) {
    uint32_t result = 0;
    for (size_t i = 0; i < 4 && i < v.size(); ++i) {
        result |= static_cast<uint32_t>(static_cast<uint8_t>(v[i])) << (24 - 8 * i);
    }
    return result;
}

/// Truncate a uint64_t to uint32_t by extracting the upper 32 bits.
///
/// Used to reduce 64-bit normalized values (int64, double) to 32-bit Morton
/// inputs. The upper bits carry the most significant information for sorting.
///
/// @param v 64-bit normalized value.
/// @return Upper 32 bits of @p v.
inline uint32_t truncate_to_32(uint64_t v) {
    return static_cast<uint32_t>(v >> 32);
}

// ===========================================================================
// Morton code — bit interleaving
// ===========================================================================

/// Interleave bits of two uint32_t values into a single uint64_t Morton code (2D).
///
/// This is the fast path for 2-column Z-ordering. Uses the parallel
/// bit-deposit "magic bits" technique to spread each 32-bit input across
/// alternating bit positions in the 64-bit result.
///
/// @param x First column's normalized uint32_t value (occupies even bit positions).
/// @param y Second column's normalized uint32_t value (occupies odd bit positions).
/// @return 64-bit Morton code with bits interleaved as: y31 x31 y30 x30 ... y0 x0.
inline uint64_t morton_2d(uint32_t x, uint32_t y) {
    auto spread = [](uint32_t v) -> uint64_t {
        uint64_t r = v;
        r = (r | (r << 16)) & 0x0000FFFF0000FFFFULL;
        r = (r | (r <<  8)) & 0x00FF00FF00FF00FFULL;
        r = (r | (r <<  4)) & 0x0F0F0F0F0F0F0F0FULL;
        r = (r | (r <<  2)) & 0x3333333333333333ULL;
        r = (r | (r <<  1)) & 0x5555555555555555ULL;
        return r;
    };
    return spread(x) | (spread(y) << 1);
}

/// Deinterleave a 2D Morton code back into its two uint32_t components.
///
/// Inverse of morton_2d(): extracts even-positioned bits into @p x and
/// odd-positioned bits into @p y.
///
/// @param code 64-bit Morton code produced by morton_2d().
/// @param[out] x First column value (even bit positions).
/// @param[out] y Second column value (odd bit positions).
inline void deinterleave_2d(uint64_t code, uint32_t& x, uint32_t& y) {
    auto compact = [](uint64_t v) -> uint32_t {
        v &= 0x5555555555555555ULL;
        v = (v | (v >> 1))  & 0x3333333333333333ULL;
        v = (v | (v >> 2))  & 0x0F0F0F0F0F0F0F0FULL;
        v = (v | (v >> 4))  & 0x00FF00FF00FF00FFULL;
        v = (v | (v >> 8))  & 0x0000FFFF0000FFFFULL;
        v = (v | (v >> 16)) & 0x00000000FFFFFFFFULL;
        return static_cast<uint32_t>(v);
    };
    x = compact(code);
    y = compact(code >> 1);
}

/// Generalized N-column Morton code via round-robin bit interleaving (MSB-first).
///
/// Supports 2-8 columns, each providing a uint32_t normalized value. Bits are
/// interleaved in round-robin order starting from the most significant bit of
/// each column, producing a byte-array sort key suitable for lexicographic
/// comparison.
///
/// The output length is `ceil(N * bits_per_col / 8)` bytes.
///
/// @param normalized Vector of N normalized uint32_t values (one per column).
/// @param bits_per_col Number of bits to interleave per column (default 32).
/// @return Byte array sort key with MSB first. Empty if @p normalized is empty.
/// @note For exactly 2 columns, prefer morton_2d() which returns a uint64_t
///       and avoids heap allocation.
inline std::vector<uint8_t> morton_nd(const std::vector<uint32_t>& normalized,
                                       size_t bits_per_col = 32) {
    size_t n = normalized.size();
    if (n == 0) return {};

    size_t total_bits = n * bits_per_col;
    size_t total_bytes = (total_bits + 7) / 8;
    std::vector<uint8_t> result(total_bytes, 0);

    // Round-robin interleave: bit position `b` of column `c` maps to
    // output bit position `b * n + c`, with MSB first.
    size_t out_bit = 0;
    for (size_t b = 0; b < bits_per_col; ++b) {
        size_t src_bit = bits_per_col - 1 - b;  // MSB first
        for (size_t c = 0; c < n; ++c) {
            if (normalized[c] & (1u << src_bit)) {
                size_t byte_idx = out_bit / 8;
                size_t bit_in_byte = 7 - (out_bit % 8);
                result[byte_idx] |= static_cast<uint8_t>(1u << bit_in_byte);
            }
            ++out_bit;
        }
    }

    return result;
}

// ===========================================================================
// ZOrderColumn -- describes a column's data for Z-order sorting
// ===========================================================================

/// Descriptor for a single column of raw typed data used by ZOrderSorter.
///
/// The caller must ensure that @p data points to a contiguous array of the
/// appropriate C++ type for the given PhysicalType, with at least @p count
/// elements. Supported types: INT32, INT64, FLOAT, DOUBLE, BYTE_ARRAY
/// (interpreted as `const std::string*`). Unsupported types sort as zero.
struct ZOrderColumn {
    PhysicalType type;   ///< Parquet physical type of the column data.
    const void* data;    ///< Pointer to a contiguous typed array (not owned).
    size_t count;        ///< Number of elements in the array (must equal num_rows).
};

// ===========================================================================
// ZOrderSorter -- sorts row indices by Morton key
// ===========================================================================

/// Computes a permutation vector that reorders rows by Z-order (Morton) key.
///
/// Two code paths are used internally:
///   - **2 columns**: morton_2d() produces a uint64_t key; sorting is a
///     single std::sort on 64-bit integers (fast, no heap allocation per row).
///   - **3-8 columns**: morton_nd() produces a variable-length byte-array key;
///     sorting uses lexicographic comparison on byte vectors.
///
/// Apply the returned permutation to your row data before writing to Parquet
/// to improve spatial locality and enable better min/max data skipping.
struct ZOrderSorter {
    /// Sort row indices [0..num_rows) by Z-order key computed from @p columns.
    ///
    /// @param num_rows Number of rows to sort.
    /// @param columns  Descriptors for each column to include in the Z-order key.
    ///                 All columns must have `count == num_rows`.
    /// @return Permutation vector of row indices sorted by ascending Morton key.
    ///         Empty if @p num_rows is 0 or @p columns is empty.
    [[nodiscard]] static std::vector<size_t> sort(
            size_t num_rows,
            const std::vector<ZOrderColumn>& columns) {

        if (num_rows == 0 || columns.empty()) return {};

        size_t n_cols = columns.size();

        // Validate that each column's count matches num_rows (CWE-787: OOB write)
        for (size_t c = 0; c < n_cols; ++c) {
            if (columns[c].count != num_rows) {
                throw std::out_of_range(
                    "ZOrderSorter::sort: column " + std::to_string(c) +
                    " count (" + std::to_string(columns[c].count) +
                    ") != num_rows (" + std::to_string(num_rows) + ")");
            }
        }

        // Normalize all values to uint32_t
        std::vector<std::vector<uint32_t>> normalized(n_cols);
        for (size_t c = 0; c < n_cols; ++c) {
            normalized[c].resize(num_rows);
            normalize_column(columns[c], normalized[c]);
        }

        // Fast path: 2 columns — use morton_2d with uint64_t sort keys
        if (n_cols == 2) {
            std::vector<uint64_t> keys(num_rows);
            for (size_t r = 0; r < num_rows; ++r) {
                keys[r] = morton_2d(normalized[0][r], normalized[1][r]);
            }

            std::vector<size_t> perm(num_rows);
            std::iota(perm.begin(), perm.end(), 0);
            std::sort(perm.begin(), perm.end(),
                      [&keys](size_t a, size_t b) { return keys[a] < keys[b]; });
            return perm;
        }

        // General path: N columns — use morton_nd byte-array sort keys
        std::vector<std::vector<uint8_t>> keys(num_rows);
        std::vector<uint32_t> row_vals(n_cols);
        for (size_t r = 0; r < num_rows; ++r) {
            for (size_t c = 0; c < n_cols; ++c) {
                row_vals[c] = normalized[c][r];
            }
            keys[r] = morton_nd(row_vals);
        }

        std::vector<size_t> perm(num_rows);
        std::iota(perm.begin(), perm.end(), 0);
        std::sort(perm.begin(), perm.end(),
                  [&keys](size_t a, size_t b) { return keys[a] < keys[b]; });
        return perm;
    }

private:
    /// Normalize a column's raw values to uint32_t based on its PhysicalType.
    ///
    /// Each physical type uses a type-specific normalization function that maps
    /// the original value space to uint32_t while preserving sort order.
    /// INT64 and DOUBLE are first normalized to uint64_t, then truncated to
    /// the upper 32 bits via truncate_to_32().
    ///
    /// @param col Column descriptor with type, data pointer, and count.
    /// @param[out] out Pre-sized vector to receive the normalized values.
    static void normalize_column(const ZOrderColumn& col,
                                  std::vector<uint32_t>& out) {
        switch (col.type) {
            case PhysicalType::INT32: {
                const auto* vals = static_cast<const int32_t*>(col.data);
                for (size_t i = 0; i < col.count; ++i) {
                    out[i] = normalize_int32(vals[i]);
                }
                break;
            }
            case PhysicalType::INT64: {
                const auto* vals = static_cast<const int64_t*>(col.data);
                for (size_t i = 0; i < col.count; ++i) {
                    out[i] = truncate_to_32(normalize_int64(vals[i]));
                }
                break;
            }
            case PhysicalType::FLOAT: {
                const auto* vals = static_cast<const float*>(col.data);
                for (size_t i = 0; i < col.count; ++i) {
                    out[i] = normalize_float(vals[i]);
                }
                break;
            }
            case PhysicalType::DOUBLE: {
                const auto* vals = static_cast<const double*>(col.data);
                for (size_t i = 0; i < col.count; ++i) {
                    out[i] = truncate_to_32(normalize_double(vals[i]));
                }
                break;
            }
            case PhysicalType::BYTE_ARRAY: {
                // Interpret data as array of std::string pointers
                const auto* vals = static_cast<const std::string*>(col.data);
                for (size_t i = 0; i < col.count; ++i) {
                    out[i] = normalize_string(vals[i]);
                }
                break;
            }
            default: {
                // For unsupported types, use zero (all rows sort equally)
                for (size_t i = 0; i < col.count; ++i) {
                    out[i] = 0;
                }
                break;
            }
        }
    }
};

} // namespace signet::forge::z_order
