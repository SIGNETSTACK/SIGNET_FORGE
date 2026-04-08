// SPDX-License-Identifier: AGPL-3.0-or-later
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
#include <tuple>
#include <mutex>
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
    FeatureReader(FeatureReader&& o) noexcept
        : opts_(std::move(o.opts_)),
          index_(std::move(o.index_)),
          feature_names_(std::move(o.feature_names_)),
          total_rows_(o.total_rows_),
          failed_file_count_(o.failed_file_count_),
          readers_(std::move(o.readers_)) {
        // Lock source cache mutex to prevent data race with concurrent ensure_cached()
        std::lock_guard<std::mutex> lk(o.rg_cache_mutex_);
        rg_cache_ = std::move(o.rg_cache_);
    }
    FeatureReader& operator=(FeatureReader&& o) noexcept {
        if (this != &o) {
            opts_             = std::move(o.opts_);
            index_            = std::move(o.index_);
            feature_names_    = std::move(o.feature_names_);
            total_rows_       = o.total_rows_;
            failed_file_count_= o.failed_file_count_;
            readers_          = std::move(o.readers_);
            std::scoped_lock lk(rg_cache_mutex_, o.rg_cache_mutex_);
            rg_cache_         = std::move(o.rg_cache_);
        }
        return *this;
    }
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
        int32_t version      = 1;   ///< version for composite sort key
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
                if (!eid_result) {
                    return Error{ErrorCode::CORRUPT_DATA,
                                 "FeatureReader: failed to read entity_id from '" + path +
                                     "' row group " + std::to_string(rg) +
                                     ": " + eid_result.error().message};
                }

                auto ts_result = rdr.read_column<int64_t>(rg, 1);
                if (!ts_result) {
                    return Error{ErrorCode::CORRUPT_DATA,
                                 "FeatureReader: failed to read timestamp_ns from '" + path +
                                     "' row group " + std::to_string(rg) +
                                     ": " + ts_result.error().message};
                }

                auto ver_result = rdr.read_column<int32_t>(rg, 2);
                if (!ver_result) {
                    return Error{ErrorCode::CORRUPT_DATA,
                                 "FeatureReader: failed to read version from '" + path +
                                     "' row group " + std::to_string(rg) +
                                     ": " + ver_result.error().message};
                }

                const auto& eids = *eid_result;
                const auto& tss  = *ts_result;
                const auto& vers = *ver_result;
                if (eids.size() != tss.size() || eids.size() != vers.size()) {
                    return Error{
                        ErrorCode::CORRUPT_DATA,
                        "FeatureReader: fixed column length mismatch in '" + path +
                            "' row group " + std::to_string(rg) + " (entity_id=" +
                            std::to_string(eids.size()) + ", timestamp_ns=" +
                            std::to_string(tss.size()) + ", version=" +
                            std::to_string(vers.size()) + ")"};
                }
                const size_t nrows = eids.size();

                for (size_t row = 0; row < nrows; ++row) {
                    RowLocation loc;
                    loc.timestamp_ns = tss[row];
                    loc.version      = vers[row];
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

        // Sort each entity's entries by (timestamp_ns, version) composite key.
        for (auto& [eid, locs] : index_) {
            std::sort(locs.begin(), locs.end(),
                [](const RowLocation& a, const RowLocation& b) {
                    if (a.timestamp_ns != b.timestamp_ns)
                        return a.timestamp_ns < b.timestamp_ns;
                    if (a.version != b.version)
                        return a.version < b.version;
                    if (a.file_idx != b.file_idx)
                        return a.file_idx < b.file_idx;
                    if (a.row_group != b.row_group)
                        return a.row_group < b.row_group;
                    return a.row_offset < b.row_offset;
                });
        }

        return expected<void>{};
    }

    // =========================================================================
    // Row group cache — avoids re-decoding entire columns per point query
    // =========================================================================

    /// Cached decoded columns for a single row group.
    struct RowGroupCache {
        size_t file_idx  = SIZE_MAX;
        size_t row_group = SIZE_MAX;

        std::vector<std::string> eids;
        std::vector<int64_t>     tss;
        std::vector<int32_t>     vers;
        std::vector<std::vector<double>> feat_cols;  ///< one vector per feature column
    };

    /// Populate or reuse the row group cache for the given location.
    [[nodiscard]] expected<const RowGroupCache*> ensure_cached(
            const RowLocation& loc) const {
        std::lock_guard<std::mutex> cache_lk(rg_cache_mutex_);

        // Cache hit — same (file_idx, row_group) as last query
        if (rg_cache_.file_idx == loc.file_idx &&
            rg_cache_.row_group == loc.row_group) {
            return &rg_cache_;
        }

        if (loc.file_idx >= readers_.size() || !readers_[loc.file_idx])
            return Error{ErrorCode::IO_ERROR,
                         "FeatureReader: reader for file index " +
                         std::to_string(loc.file_idx) + " is not available"};

        auto& rdr          = *readers_[loc.file_idx];
        const auto& schema = rdr.schema();

        // Decode the three fixed columns
        auto eid_result = rdr.read_column<std::string>(loc.row_group, 0);
        if (!eid_result) return eid_result.error();

        auto ts_result = rdr.read_column<int64_t>(loc.row_group, 1);
        if (!ts_result) return ts_result.error();

        auto ver_result = rdr.read_column<int32_t>(loc.row_group, 2);
        if (!ver_result) return ver_result.error();

        // Decode all feature columns (col 3..N)
        std::vector<std::vector<double>> feat_cols;
        for (size_t ci = 3; ci < schema.num_columns(); ++ci) {
            auto feat_result = rdr.read_column<double>(loc.row_group, ci);
            if (!feat_result) return feat_result.error();
            feat_cols.push_back(std::move(*feat_result));
        }

        const size_t row_count = eid_result->size();
        if (ts_result->size() != row_count || ver_result->size() != row_count) {
            return Error{
                ErrorCode::CORRUPT_DATA,
                "FeatureReader: fixed column length mismatch for file index " +
                    std::to_string(loc.file_idx) + ", row group " +
                    std::to_string(loc.row_group)};
        }
        for (size_t ci = 0; ci < feat_cols.size(); ++ci) {
            if (feat_cols[ci].size() != row_count) {
                return Error{
                    ErrorCode::CORRUPT_DATA,
                    "FeatureReader: feature column length mismatch for file index " +
                        std::to_string(loc.file_idx) + ", row group " +
                        std::to_string(loc.row_group) + ", column " +
                        std::to_string(ci + 3)};
            }
        }

        // Store in cache
        rg_cache_.file_idx  = loc.file_idx;
        rg_cache_.row_group = loc.row_group;
        rg_cache_.eids      = std::move(*eid_result);
        rg_cache_.tss       = std::move(*ts_result);
        rg_cache_.vers      = std::move(*ver_result);
        rg_cache_.feat_cols = std::move(feat_cols);

        return &rg_cache_;
    }

    /// Read actual feature values for a specific row location.
    [[nodiscard]] expected<FeatureVector> fetch_row(
            const RowLocation& loc,
            const std::vector<std::string>& project) const {

        auto cache_result = ensure_cached(loc);
        if (!cache_result) return cache_result.error();
        const auto* cache = *cache_result;

        if (loc.row_offset >= cache->tss.size())
            return Error{ErrorCode::OUT_OF_RANGE,
                         "FeatureReader: row_offset " +
                         std::to_string(loc.row_offset) +
                         " out of range for row group with " +
                         std::to_string(cache->tss.size()) + " rows"};

        FeatureVector fv;
        if (loc.row_offset >= cache->eids.size() ||
            loc.row_offset >= cache->vers.size()) {
            return Error{ErrorCode::CORRUPT_DATA,
                         "FeatureReader: cached row layout mismatch for file index " +
                             std::to_string(loc.file_idx) + ", row group " +
                             std::to_string(loc.row_group)};
        }
        fv.entity_id    = cache->eids[loc.row_offset];
        fv.timestamp_ns = cache->tss[loc.row_offset];
        fv.version      = cache->vers[loc.row_offset];

        // --- Determine which feature columns to extract ---
        if (project.empty()) {
            // All features — direct index into cached columns
            fv.values.reserve(cache->feat_cols.size());
            for (const auto& col : cache->feat_cols) {
                if (loc.row_offset >= col.size()) {
                    return Error{ErrorCode::CORRUPT_DATA,
                                 "FeatureReader: feature column shorter than cached row count"};
                }
                fv.values.push_back(col[loc.row_offset]);
            }
        } else {
            // Projected subset — look up column indices by name
            if (loc.file_idx < readers_.size() && readers_[loc.file_idx]) {
                const auto& schema = readers_[loc.file_idx]->schema();
                fv.values.reserve(project.size());
                for (const auto& fname : project) {
                    auto idx = schema.find_column(fname);
                    if (!idx.has_value() || *idx < 3) {
                        return Error{ErrorCode::SCHEMA_MISMATCH,
                                     "FeatureReader: requested feature column '" +
                                         fname + "' is not present"};
                    }

                    size_t feat_idx = *idx - 3;
                    if (feat_idx >= cache->feat_cols.size()) {
                        return Error{ErrorCode::CORRUPT_DATA,
                                     "FeatureReader: cached projected feature column index out of range"};
                    }

                    const auto& col = cache->feat_cols[feat_idx];
                    if (loc.row_offset >= col.size()) {
                        return Error{ErrorCode::CORRUPT_DATA,
                                     "FeatureReader: projected feature column shorter than cached row count"};
                    }
                    fv.values.push_back(col[loc.row_offset]);
                }
            }
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

    // Single-entry row group cache — consecutive point queries hitting the
    // same (file_idx, row_group) reuse decoded columns instead of re-decoding.
    mutable std::mutex    rg_cache_mutex_;
    mutable RowGroupCache rg_cache_;
};

} // namespace signet::forge
