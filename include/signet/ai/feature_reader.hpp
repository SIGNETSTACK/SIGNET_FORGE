// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
/// @file feature_reader.hpp
/// @brief Versioned ML feature store reader with point-in-time queries.
//
// Point-in-time correct reads, time-travel history, batch lookups, and column
// projection over Parquet files written by FeatureWriter.
//
// Index is built at open() time by scanning entity_id + timestamp_ns columns
// across all files.  All subsequent queries are O(log N) with no disk I/O
// (file data is held in memory by ParquetReader).

#pragma once

#include "signet/error.hpp"
#include "signet/types.hpp"
#include "signet/reader.hpp"
#include "signet/ai/feature_writer.hpp"  // FeatureGroupDef, FeatureVector

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace signet::forge {

// ============================================================================
// FeatureReaderOptions
// (defined outside FeatureReader to avoid Apple Clang default-member-init bug)
// ============================================================================

/// Configuration options for FeatureReader::open().
///
/// Defined at namespace scope (not nested in FeatureReader) to work around an
/// Apple Clang restriction on default member initializers in nested aggregates.
///
/// @see FeatureReader
struct FeatureReaderOptions {
    std::vector<std::string> parquet_files;  ///< Paths to feature group Parquet files.
    std::string              feature_group;  ///< Expected group name (optional validation).
};

// ============================================================================
// FeatureReader — point-in-time correct ML feature store reader
// ============================================================================

/// Point-in-time correct ML feature store reader over Parquet files.
///
/// Opens one or more Parquet files written by FeatureWriter, builds an
/// in-memory index at open() time, then serves O(log N) queries with no
/// additional disk I/O.
///
/// Usage:
/// @code
///   auto r = FeatureReader::open({.parquet_files = fw.output_files()});
///   auto fv = r->as_of("BTCUSDT", now_ns);     // latest version <= now_ns
///   auto hist = r->history("BTCUSDT", t0, t1);  // all versions in [t0, t1]
/// @endcode
///
/// @note Movable but not copyable. Internally holds mutable ParquetReader
///       instances because read_column() updates a decompression cache.
///
/// @see FeatureReaderOptions, FeatureWriter, FeatureVector
class FeatureReader {
public:
    /// Alias for the options struct.
    using Options = FeatureReaderOptions;

    // -------------------------------------------------------------------------
    // Factory — opens all files and builds in-memory index
    // -------------------------------------------------------------------------

    /// Open all Parquet files and build the in-memory entity/timestamp index.
    ///
    /// @param opts  Reader options specifying which files to open.
    /// @return A fully-indexed FeatureReader, or an Error on failure.
    [[nodiscard]] static expected<FeatureReader> open(Options opts) {
        if (opts.parquet_files.empty())
            return Error{ErrorCode::IO_ERROR,
                         "FeatureReader: parquet_files must not be empty"};

        FeatureReader reader;
        reader.opts_ = std::move(opts);

        auto r = reader.build_index();
        if (!r) return r.error();

        return reader;
    }

    // -------------------------------------------------------------------------
    // Special members — movable, non-copyable
    // -------------------------------------------------------------------------

    /// Default-construct an empty reader (use open() factory instead).
    FeatureReader() = default;
    FeatureReader(FeatureReader&&) = default;
    FeatureReader& operator=(FeatureReader&&) = default;
    FeatureReader(const FeatureReader&) = delete;
    FeatureReader& operator=(const FeatureReader&) = delete;

    // =========================================================================
    // Query API
    // =========================================================================

    // -------------------------------------------------------------------------
    // as_of — latest version of entity at or before timestamp_ns
    // -------------------------------------------------------------------------

    /// Retrieve the latest version of an entity at or before the given timestamp.
    ///
    /// Uses binary search over the sorted index. O(log N) per entity.
    ///
    /// @param entity_id     The entity key to look up (e.g. "BTCUSDT").
    /// @param timestamp_ns  Upper bound timestamp (inclusive) in nanoseconds.
    /// @param project       Optional subset of feature names to return; empty means all.
    /// @return The matching FeatureVector, or nullopt if the entity is unknown
    ///         or all its entries have timestamps after the query time.
    [[nodiscard]] expected<std::optional<FeatureVector>> as_of(
            const std::string& entity_id,
            int64_t            timestamp_ns,
            const std::vector<std::string>& project = {}) const {

        auto it = index_.find(entity_id);
        if (it == index_.end())
            return std::optional<FeatureVector>{std::nullopt};

        const auto& locs = it->second;

        // Binary search: find the last entry with timestamp_ns <= query time.
        auto pos = std::upper_bound(
            locs.begin(), locs.end(), timestamp_ns,
            [](int64_t ts, const RowLocation& loc) {
                return ts < loc.timestamp_ns;
            });

        if (pos == locs.begin())
            return std::optional<FeatureVector>{std::nullopt};  // all > timestamp

        --pos;  // largest timestamp_ns <= query

        auto fv = fetch_row(*pos, project);
        if (!fv) return fv.error();
        return std::optional<FeatureVector>{std::move(*fv)};
    }

    // -------------------------------------------------------------------------
    // get — latest version of entity (no time constraint)
    // -------------------------------------------------------------------------

    /// Retrieve the latest version of an entity regardless of timestamp.
    ///
    /// Equivalent to `as_of(entity_id, INT64_MAX, project)`.
    ///
    /// @param entity_id  The entity key to look up.
    /// @param project    Optional subset of feature names; empty means all.
    /// @return The latest FeatureVector, or nullopt if the entity is unknown.
    [[nodiscard]] expected<std::optional<FeatureVector>> get(
            const std::string& entity_id,
            const std::vector<std::string>& project = {}) const {
        return as_of(entity_id,
                     (std::numeric_limits<int64_t>::max)(),
                     project);
    }

    // -------------------------------------------------------------------------
    // history — all versions of entity in the inclusive range [start_ns, end_ns]
    // -------------------------------------------------------------------------

    /// Retrieve all versions of an entity in the inclusive timestamp range.
    ///
    /// @param entity_id  The entity key to look up.
    /// @param start_ns   Lower bound timestamp (inclusive) in nanoseconds.
    /// @param end_ns     Upper bound timestamp (inclusive) in nanoseconds.
    /// @param project    Optional subset of feature names; empty means all.
    /// @return A vector of FeatureVectors sorted by ascending timestamp,
    ///         or an empty vector if the entity is unknown.
    [[nodiscard]] expected<std::vector<FeatureVector>> history(
            const std::string& entity_id,
            int64_t start_ns,
            int64_t end_ns,
            const std::vector<std::string>& project = {}) const {

        auto it = index_.find(entity_id);
        if (it == index_.end())
            return std::vector<FeatureVector>{};

        const auto& locs = it->second;
        std::vector<FeatureVector> result;

        for (const auto& loc : locs) {
            if (loc.timestamp_ns < start_ns) continue;
            if (loc.timestamp_ns > end_ns)   break;

            auto fv = fetch_row(loc, project);
            if (!fv) return fv.error();
            result.push_back(std::move(*fv));
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // as_of_batch — as_of for N entities at the same timestamp
    // -------------------------------------------------------------------------

    /// Batch as_of query: retrieve the latest version of multiple entities at one timestamp.
    ///
    /// Entities not found are silently omitted from the result.
    ///
    /// @param entity_ids    Vector of entity keys to look up.
    /// @param timestamp_ns  Upper bound timestamp (inclusive) in nanoseconds.
    /// @param project       Optional subset of feature names; empty means all.
    /// @return A vector of FeatureVectors for all found entities.
    [[nodiscard]] expected<std::vector<FeatureVector>> as_of_batch(
            const std::vector<std::string>& entity_ids,
            int64_t timestamp_ns,
            const std::vector<std::string>& project = {}) const {

        std::vector<FeatureVector> result;
        result.reserve(entity_ids.size());

        for (const auto& eid : entity_ids) {
            auto r = as_of(eid, timestamp_ns, project);
            if (!r) return r.error();
            if (r->has_value())
                result.push_back(std::move(r->value()));
        }

        return result;
    }

    // =========================================================================
    // Schema / metadata accessors
    // =========================================================================

    /// Return the ordered feature column names discovered from the first readable file.
    [[nodiscard]] const std::vector<std::string>& feature_names() const {
        return feature_names_;
    }
    /// Number of feature columns in the schema.
    [[nodiscard]] size_t num_features()  const { return feature_names_.size(); }
    /// Number of distinct entities in the index.
    [[nodiscard]] size_t num_entities()  const { return index_.size(); }
    /// Total number of rows indexed across all files and row groups.
    [[nodiscard]] size_t total_rows()    const { return total_rows_; }
    /// L22: Number of files that failed to open during build_index().
    /// Exposed for error observability — callers can detect partial index
    /// builds caused by missing/corrupt files and alert or retry accordingly.
    [[nodiscard]] size_t failed_file_count() const { return failed_file_count_; }

private:
    // =========================================================================
    // Internal: row location in the index
    // =========================================================================

    /// Location of a single row within the file/row-group index.
    struct RowLocation {
        int64_t timestamp_ns = 0;
        size_t  file_idx     = 0;   ///< index into opts_.parquet_files / readers_
        size_t  row_group    = 0;
        size_t  row_offset   = 0;   ///< row index within the row group
    };

    // entity_id → entries sorted by timestamp_ns (ascending)
    using EntityIndex = std::unordered_map<std::string, std::vector<RowLocation>>;

    /// Scan all Parquet files, populate index_ and readers_.
    [[nodiscard]] expected<void> build_index() {
        const size_t num_files = opts_.parquet_files.size();
        readers_.reserve(num_files);

        for (size_t fi = 0; fi < num_files; ++fi) {
            const auto& path = opts_.parquet_files[fi];

            // Check existence before opening (ParquetReader::open gives a
            // poor error message for missing files).
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) {
                readers_.push_back(nullptr);
                ++failed_file_count_;
                continue;
            }

            auto rdr_result = ParquetReader::open(path);
            if (!rdr_result) {
                readers_.push_back(nullptr);
                ++failed_file_count_;
                continue;
            }

            auto& rdr = *rdr_result;
            const auto& schema = rdr.schema();

            // Discover feature names from the first readable file.
            // Schema layout: col0=entity_id, col1=timestamp_ns, col2=version,
            //                col3..N = feature columns (DOUBLE).
            if (feature_names_.empty() && schema.num_columns() > 3) {
                for (size_t ci = 3; ci < schema.num_columns(); ++ci)
                    feature_names_.push_back(schema.column(ci).name);
            }

            const size_t num_rg = static_cast<size_t>(rdr.num_row_groups());

            for (size_t rg = 0; rg < num_rg; ++rg) {
                // Read only entity_id (col 0) and timestamp_ns (col 1)
                // for the index — feature values are fetched on demand.
                auto eid_result = rdr.read_column<std::string>(rg, 0);
                if (!eid_result) continue;

                auto ts_result = rdr.read_column<int64_t>(rg, 1);
                if (!ts_result) continue;

                const auto& eids = *eid_result;
                const auto& tss  = *ts_result;
                const size_t nrows = std::min(eids.size(), tss.size());

                for (size_t row = 0; row < nrows; ++row) {
                    RowLocation loc;
                    loc.timestamp_ns = tss[row];
                    loc.file_idx     = fi;
                    loc.row_group    = rg;
                    loc.row_offset   = row;
                    index_[eids[row]].push_back(loc);
                }

                total_rows_ += nrows;
            }

            // Move reader into storage so fetch_row can use it later.
            readers_.push_back(
                std::make_unique<ParquetReader>(std::move(*rdr_result)));
        }

        // Sort each entity's entries by timestamp_ns for binary search.
        for (auto& [eid, locs] : index_) {
            std::sort(locs.begin(), locs.end(),
                [](const RowLocation& a, const RowLocation& b) {
                    return a.timestamp_ns < b.timestamp_ns;
                });
        }

        return expected<void>{};
    }

    /// Read actual feature values for a specific row location.
    [[nodiscard]] expected<FeatureVector> fetch_row(
            const RowLocation& loc,
            const std::vector<std::string>& project) const {

        if (loc.file_idx >= readers_.size() || !readers_[loc.file_idx])
            return Error{ErrorCode::IO_ERROR,
                         "FeatureReader: reader for file index " +
                         std::to_string(loc.file_idx) + " is not available"};

        // ParquetReader::read_column() is logically const but modifies an
        // internal decompression cache — hence readers_ is mutable.
        auto& rdr           = *readers_[loc.file_idx];
        const auto& schema  = rdr.schema();

        // --- Determine which feature columns to fetch ---
        std::vector<size_t>      feat_col_indices;
        std::vector<std::string> feat_names_out;

        if (project.empty()) {
            // All features in schema order
            for (size_t ci = 3; ci < schema.num_columns(); ++ci) {
                feat_col_indices.push_back(ci);
                feat_names_out.push_back(schema.column(ci).name);
            }
        } else {
            // Projected subset — skip features not present in this file
            // (handles schema evolution: older files may lack newer features)
            for (const auto& fname : project) {
                auto idx = schema.find_column(fname);
                if (idx.has_value()) {
                    feat_col_indices.push_back(*idx);
                    feat_names_out.push_back(fname);
                }
            }
        }

        // --- Read the fixed columns from this row group ---
        auto eid_result = rdr.read_column<std::string>(loc.row_group, 0);
        if (!eid_result) return eid_result.error();

        auto ts_result = rdr.read_column<int64_t>(loc.row_group, 1);
        if (!ts_result) return ts_result.error();

        auto ver_result = rdr.read_column<int32_t>(loc.row_group, 2);
        if (!ver_result) return ver_result.error();

        const auto& eids = *eid_result;
        const auto& tss  = *ts_result;
        const auto& vers = *ver_result;

        if (loc.row_offset >= tss.size())
            return Error{ErrorCode::OUT_OF_RANGE,
                         "FeatureReader: row_offset " +
                         std::to_string(loc.row_offset) +
                         " out of range for row group with " +
                         std::to_string(tss.size()) + " rows"};

        FeatureVector fv;
        fv.entity_id    = (loc.row_offset < eids.size())
                              ? eids[loc.row_offset] : "";
        fv.timestamp_ns = tss[loc.row_offset];
        fv.version      = (loc.row_offset < vers.size())
                              ? vers[loc.row_offset] : 1;

        // --- Read feature columns ---
        fv.values.reserve(feat_col_indices.size());
        for (size_t ci : feat_col_indices) {
            auto feat_result = rdr.read_column<double>(loc.row_group, ci);
            if (!feat_result) return feat_result.error();
            const auto& col = *feat_result;
            fv.values.push_back(
                (loc.row_offset < col.size())
                    ? col[loc.row_offset]
                    : std::numeric_limits<double>::quiet_NaN());
        }

        return fv;
    }

    // =========================================================================
    // State
    // =========================================================================

    Options                  opts_;
    EntityIndex              index_;
    std::vector<std::string> feature_names_;
    size_t                   total_rows_{0};
    size_t                   failed_file_count_{0};

    // One entry per file in opts_.parquet_files (nullptr for unreadable files).
    // Mutable because ParquetReader::read_column() updates an internal
    // decompression cache even though it is logically a read operation.
    mutable std::vector<std::unique_ptr<ParquetReader>> readers_;
};

} // namespace signet::forge
