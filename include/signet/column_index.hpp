// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file column_index.hpp
/// @brief ColumnIndex, OffsetIndex, and ColumnIndexBuilder for predicate pushdown.
///
/// Per-column-chunk structures for predicate pushdown and random page access.
/// Written after the row groups but before the footer in the Parquet file.
///
/// ColumnIndex stores per-page min/max statistics, enabling readers to skip
/// pages that cannot contain matching data. OffsetIndex stores page locations
/// for efficient random access into column chunks.
///
/// Thrift field IDs follow the canonical parquet.thrift specification:
/// - ColumnIndex:  1=null_pages, 2=min_values, 3=max_values, 4=boundary_order, 5=null_counts
/// - OffsetIndex:  1=page_locations (list of PageLocation)
/// - PageLocation: 1=offset, 2=compressed_page_size, 3=first_row_index

#include "signet/thrift/compact.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace signet::forge {

/// File offset and size descriptor for a single data page.
///
/// Used by OffsetIndex to record the location of each page within the
/// Parquet file, enabling random access into column chunks without
/// sequential scanning.
struct PageLocation {
    int64_t offset              = 0;  ///< Absolute file offset of the page header.
    int32_t compressed_page_size = 0; ///< Size of the page in compressed bytes.
    int64_t first_row_index     = 0;  ///< First row in this page (relative to row group).

    /// Serialize this PageLocation to a Thrift compact encoder.
    /// @param enc  The encoder to write to.
    void serialize(thrift::CompactEncoder& enc) const {
        enc.begin_struct();

        // field 1: offset (i64)
        enc.write_field(1, thrift::compact_type::I64);
        enc.write_i64(offset);

        // field 2: compressed_page_size (i32)
        enc.write_field(2, thrift::compact_type::I32);
        enc.write_i32(compressed_page_size);

        // field 3: first_row_index (i64)
        enc.write_field(3, thrift::compact_type::I64);
        enc.write_i64(first_row_index);

        enc.write_stop();
        enc.end_struct();
    }

    /// Deserialize this PageLocation from a Thrift compact decoder.
    /// @param dec  The decoder to read from.
    void deserialize(thrift::CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == thrift::compact_type::STOP) break;
            switch (fid) {
                case 1: offset               = dec.read_i64(); break;
                case 2: compressed_page_size  = dec.read_i32(); break;
                case 3: first_row_index      = dec.read_i64(); break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Page locations for random access within a column chunk.
///
/// Contains a list of PageLocation entries, one per data page in the
/// column chunk. Written to the Parquet file after row groups to enable
/// readers to seek directly to any page.
///
/// @see ColumnIndex (companion structure for predicate pushdown)
struct OffsetIndex {
    std::vector<PageLocation> page_locations; ///< One entry per data page.

    /// Serialize this OffsetIndex to a Thrift compact encoder.
    /// @param enc  The encoder to write to.
    void serialize(thrift::CompactEncoder& enc) const {
        enc.begin_struct();

        // field 1: page_locations (list<struct>)
        enc.write_field(1, thrift::compact_type::LIST);
        enc.write_list_header(thrift::compact_type::STRUCT,
                              static_cast<int32_t>(page_locations.size()));
        for (const auto& loc : page_locations) {
            loc.serialize(enc);
        }

        enc.write_stop();
        enc.end_struct();
    }

    /// Deserialize this OffsetIndex from a Thrift compact decoder.
    /// @param dec  The decoder to read from.
    void deserialize(thrift::CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == thrift::compact_type::STOP) break;
            switch (fid) {
                case 1: {
                    auto [elem_type, count] = dec.read_list_header();
                    // CWE-400: Uncontrolled Resource Consumption — 10M cap on list counts
                    if (count < 0 || static_cast<size_t>(count) > 10'000'000) {
                        return; // list count exceeds 10M cap or is negative
                    }
                    page_locations.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        page_locations[static_cast<size_t>(i)].deserialize(dec);
                    }
                    break;
                }
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Per-page min/max statistics for predicate pushdown.
///
/// Stores binary-encoded min/max values for each data page in a column
/// chunk, along with null-page flags, boundary ordering, and optional
/// null counts. Readers use filter_pages() to eliminate pages whose value
/// ranges do not overlap the query predicate.
///
/// @see OffsetIndex          (companion for page offsets)
/// @see ColumnIndexBuilder   (builder pattern for constructing during writes)
struct ColumnIndex {
    std::vector<bool>        null_pages;  ///< True if the corresponding page is all nulls.
    std::vector<std::string> min_values;  ///< Binary-encoded minimum value per page.
    std::vector<std::string> max_values;  ///< Binary-encoded maximum value per page.

    /// Ordering of min values across pages, used to short-circuit filtering.
    enum class BoundaryOrder : int32_t {
        UNORDERED  = 0, ///< Min values have no particular order.
        ASCENDING  = 1, ///< Min values are non-decreasing across pages.
        DESCENDING = 2  ///< Min values are non-increasing across pages.
    };
    BoundaryOrder boundary_order = BoundaryOrder::UNORDERED; ///< Boundary order of min values.

    std::vector<int64_t> null_counts; ///< Null count per page (optional).

    /// Serialize this ColumnIndex to a Thrift compact encoder.
    /// @param enc  The encoder to write to.
    void serialize(thrift::CompactEncoder& enc) const {
        enc.begin_struct();

        // field 1: null_pages (list<bool>)
        enc.write_field(1, thrift::compact_type::LIST);
        enc.write_list_header(thrift::compact_type::BOOL_TRUE,
                              static_cast<int32_t>(null_pages.size()));
        for (bool np : null_pages) {
            enc.write_bool(np);
        }

        // field 2: min_values (list<binary>)
        enc.write_field(2, thrift::compact_type::LIST);
        enc.write_list_header(thrift::compact_type::BINARY,
                              static_cast<int32_t>(min_values.size()));
        for (const auto& mv : min_values) {
            enc.write_string(mv);
        }

        // field 3: max_values (list<binary>)
        enc.write_field(3, thrift::compact_type::LIST);
        enc.write_list_header(thrift::compact_type::BINARY,
                              static_cast<int32_t>(max_values.size()));
        for (const auto& mv : max_values) {
            enc.write_string(mv);
        }

        // field 4: boundary_order (i32)
        enc.write_field(4, thrift::compact_type::I32);
        enc.write_i32(static_cast<int32_t>(boundary_order));

        // field 5: null_counts (list<i64>, optional -- written only if non-empty)
        if (!null_counts.empty()) {
            enc.write_field(5, thrift::compact_type::LIST);
            enc.write_list_header(thrift::compact_type::I64,
                                  static_cast<int32_t>(null_counts.size()));
            for (int64_t nc : null_counts) {
                enc.write_i64(nc);
            }
        }

        enc.write_stop();
        enc.end_struct();
    }

    /// Deserialize this ColumnIndex from a Thrift compact decoder.
    /// @param dec  The decoder to read from.
    void deserialize(thrift::CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == thrift::compact_type::STOP) break;
            switch (fid) {
                case 1: {
                    // null_pages: list<bool>
                    auto [elem_type, count] = dec.read_list_header();
                    // CWE-400: Uncontrolled Resource Consumption — 10M cap on list counts
                    if (count < 0 || static_cast<size_t>(count) > 10'000'000) {
                        return; // list count exceeds 10M cap or is negative
                    }
                    null_pages.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        null_pages[static_cast<size_t>(i)] = dec.read_bool();
                    }
                    break;
                }
                case 2: {
                    // min_values: list<binary>
                    auto [elem_type, count] = dec.read_list_header();
                    // CWE-400: Uncontrolled Resource Consumption — 10M cap on list counts
                    if (count < 0 || static_cast<size_t>(count) > 10'000'000) {
                        return; // list count exceeds 10M cap or is negative
                    }
                    min_values.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        min_values[static_cast<size_t>(i)] = dec.read_string();
                    }
                    break;
                }
                case 3: {
                    // max_values: list<binary>
                    auto [elem_type, count] = dec.read_list_header();
                    // CWE-400: Uncontrolled Resource Consumption — 10M cap on list counts
                    if (count < 0 || static_cast<size_t>(count) > 10'000'000) {
                        return; // list count exceeds 10M cap or is negative
                    }
                    max_values.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        max_values[static_cast<size_t>(i)] = dec.read_string();
                    }
                    break;
                }
                case 4:
                    boundary_order = static_cast<BoundaryOrder>(dec.read_i32());
                    break;
                case 5: {
                    // null_counts: list<i64>
                    auto [elem_type, count] = dec.read_list_header();
                    // CWE-400: Uncontrolled Resource Consumption — 10M cap on list counts
                    if (count < 0 || static_cast<size_t>(count) > 10'000'000) {
                        return; // list count exceeds 10M cap or is negative
                    }
                    null_counts.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        null_counts[static_cast<size_t>(i)] = dec.read_i64();
                    }
                    break;
                }
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }

    /// Filter pages by a value range for predicate pushdown.
    ///
    /// Given a range [@p min_val, @p max_val] (binary-encoded, same encoding
    /// as min_values/max_values), returns page indices that might contain
    /// matching data. A page is excluded only if its max is strictly less
    /// than @p min_val or its min is strictly greater than @p max_val.
    /// All-null pages are always excluded.
    ///
    /// @note Binary comparison uses lexicographic byte ordering, which is
    ///       correct for unsigned integer types and strings. For signed
    ///       types, the caller should ensure values use a comparison-safe
    ///       binary encoding.
    ///
    /// @param min_val  Lower bound of the query range (binary-encoded).
    /// @param max_val  Upper bound of the query range (binary-encoded).
    /// @return A vector of page indices (0-based) that may contain matches.
    [[nodiscard]] std::vector<size_t> filter_pages(
            const std::string& min_val,
            const std::string& max_val) const {

        std::vector<size_t> matching;
        size_t num_pages = min_values.size();

        for (size_t i = 0; i < num_pages; ++i) {
            // Skip all-null pages -- they cannot contain any matching data
            if (i < null_pages.size() && null_pages[i]) {
                continue;
            }

            // Skip if page max < query min (entire page below the range)
            if (i < max_values.size() && max_values[i] < min_val) {
                continue;
            }

            // Skip if page min > query max (entire page above the range)
            if (i < min_values.size() && min_values[i] > max_val) {
                continue;
            }

            matching.push_back(i);
        }

        return matching;
    }
};

/// Builder that accumulates per-page statistics during column writing.
///
/// Usage:
/// @code
///   ColumnIndexBuilder builder;
///   for (each page being written) {
///       builder.start_page();
///       builder.set_min(...);
///       builder.set_max(...);
///       builder.set_null_page(false);
///       builder.set_null_count(0);
///       builder.set_first_row_index(row_offset);
///       builder.set_page_location(file_offset, compressed_size);
///   }
///   ColumnIndex ci = builder.build_column_index();
///   OffsetIndex oi = builder.build_offset_index();
/// @endcode
///
/// @see ColumnIndex   (output of build_column_index())
/// @see OffsetIndex   (output of build_offset_index())
class ColumnIndexBuilder {
public:
    /// Start a new page. Must be called before set_min/set_max etc.
    void start_page() {
        pages_.emplace_back();
    }

    /// Record the minimum value for the current page (binary-encoded).
    /// @param min_val  The binary-encoded minimum value.
    void set_min(const std::string& min_val) {
        if (!pages_.empty()) {
            pages_.back().min_value = min_val;
        }
    }

    /// Record the maximum value for the current page (binary-encoded).
    /// @param max_val  The binary-encoded maximum value.
    void set_max(const std::string& max_val) {
        if (!pages_.empty()) {
            pages_.back().max_value = max_val;
        }
    }

    /// Mark the current page as all-nulls (or not).
    /// @param is_null  True if the page contains only null values.
    void set_null_page(bool is_null) {
        if (!pages_.empty()) {
            pages_.back().null_page = is_null;
        }
    }

    /// Record the null count for the current page.
    /// @param count  Number of null values in the current page.
    void set_null_count(int64_t count) {
        if (!pages_.empty()) {
            pages_.back().null_count = count;
        }
    }

    /// Record the first row index for the current page (relative to row group).
    /// @param row_index  Zero-based row index of the first row in this page.
    void set_first_row_index(int64_t row_index) {
        if (!pages_.empty()) {
            pages_.back().first_row_index = row_index;
        }
    }

    /// Record the page location (file offset and compressed size) for the current page.
    /// @param offset           Absolute file offset of the page.
    /// @param compressed_size  Page size in compressed bytes.
    void set_page_location(int64_t offset, int32_t compressed_size) {
        if (!pages_.empty()) {
            pages_.back().offset = offset;
            pages_.back().compressed_size = compressed_size;
        }
    }

    /// Finalize and return the ColumnIndex from accumulated page info.
    ///
    /// Automatically detects boundary order from the min_values sequence.
    ///
    /// @return A fully populated ColumnIndex ready for serialization.
    [[nodiscard]] ColumnIndex build_column_index() const {
        ColumnIndex ci;
        ci.null_pages.reserve(pages_.size());
        ci.min_values.reserve(pages_.size());
        ci.max_values.reserve(pages_.size());
        ci.null_counts.reserve(pages_.size());

        bool has_any_null_counts = false;
        for (const auto& p : pages_) {
            ci.null_pages.push_back(p.null_page);
            ci.min_values.push_back(p.min_value);
            ci.max_values.push_back(p.max_value);
            ci.null_counts.push_back(p.null_count);
            if (p.null_count > 0) {
                has_any_null_counts = true;
            }
        }

        // Determine boundary order by scanning min_values
        ci.boundary_order = detect_boundary_order(ci.min_values);

        // Only include null_counts if at least one page has nulls,
        // or if the caller explicitly set null counts. The Parquet spec
        // makes null_counts optional, so we always include them for
        // completeness (readers expect them for null-page detection).
        (void)has_any_null_counts;

        return ci;
    }

    /// Finalize and return the OffsetIndex from accumulated page info.
    /// @return A fully populated OffsetIndex ready for serialization.
    [[nodiscard]] OffsetIndex build_offset_index() const {
        OffsetIndex oi;
        oi.page_locations.reserve(pages_.size());

        for (const auto& p : pages_) {
            PageLocation loc;
            loc.offset               = p.offset;
            loc.compressed_page_size = p.compressed_size;
            loc.first_row_index      = p.first_row_index;
            oi.page_locations.push_back(loc);
        }

        return oi;
    }

    /// Reset the builder, discarding all accumulated page info.
    void reset() {
        pages_.clear();
    }

    /// Number of pages accumulated so far.
    [[nodiscard]] size_t num_pages() const { return pages_.size(); }

private:
    /// Accumulated per-page metadata during building.
    struct PageInfo {
        std::string min_value;
        std::string max_value;
        bool        null_page       = false;
        int64_t     null_count      = 0;
        int64_t     first_row_index = 0;
        int64_t     offset          = 0;
        int32_t     compressed_size = 0;
    };

    std::vector<PageInfo> pages_;

    /// Detect boundary order from the min_values sequence.
    ///
    // TODO: type-aware comparison (signed vs unsigned vs byte) per Parquet spec
    // Currently uses lexicographic byte comparison which is correct for
    // BYTE_ARRAY and big-endian encoded numeric types (CWE-843).
    [[nodiscard]] static ColumnIndex::BoundaryOrder detect_boundary_order(
            const std::vector<std::string>& values) {

        if (values.size() <= 1) {
            return ColumnIndex::BoundaryOrder::ASCENDING;
        }

        bool ascending  = true;
        bool descending = true;

        for (size_t i = 1; i < values.size(); ++i) {
            if (values[i] < values[i - 1]) {
                ascending = false;
            }
            if (values[i] > values[i - 1]) {
                descending = false;
            }
            if (!ascending && !descending) {
                break;
            }
        }

        if (ascending) return ColumnIndex::BoundaryOrder::ASCENDING;
        if (descending) return ColumnIndex::BoundaryOrder::DESCENDING;
        return ColumnIndex::BoundaryOrder::UNORDERED;
    }
};

} // namespace signet::forge
