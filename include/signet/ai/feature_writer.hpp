// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
/// @file feature_writer.hpp
/// @brief Versioned ML feature store writer for appending typed feature vectors to Parquet.
//
// Writes typed, versioned feature vectors keyed by entity_id and timestamp_ns.
// Schema: entity_id (STRING) | timestamp_ns (INT64) | version (INT32) | feat_0..N (DOUBLE)
// Files are append-only; rolling is controlled by max_file_rows.

#pragma once

#include "signet/error.hpp"
#include "signet/types.hpp"
#include "signet/schema.hpp"
#include "signet/writer.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace signet::forge {

// ============================================================================
// FeatureGroupDef — schema definition for one feature group
// (defined at namespace scope to avoid Apple Clang default-member-init bug)
// ============================================================================

/// Schema definition for a single feature group.
///
/// A feature group is a named collection of feature columns that share the
/// same entity_id and timestamp_ns key space.
///
/// @see FeatureWriter, FeatureWriterOptions
struct FeatureGroupDef {
    std::string              name;            ///< Group name (e.g. "price_features", "vol_features").
    std::vector<std::string> feature_names;   ///< Ordered feature column names.
    int32_t                  schema_version = 1; ///< Bumped when features are added or removed.
};

// ============================================================================
// FeatureVector — one versioned observation for one entity
// ============================================================================

/// A single versioned observation for one entity.
///
/// Each FeatureVector is a row in the feature store: it captures the feature
/// values of a given entity at a specific nanosecond timestamp with a version
/// number for late-arriving corrections.
///
/// @see FeatureWriter::write, FeatureReader::as_of
struct FeatureVector {
    std::string         entity_id;           ///< Entity key (e.g. "BTCUSDT").
    int64_t             timestamp_ns = 0;    ///< Observation timestamp in nanoseconds.
    int32_t             version      = 1;    ///< Version number (for late-arriving corrections).
    std::vector<double> values;              ///< One value per FeatureGroupDef::feature_names.
    std::string              computation_dag;   ///< DAG description of feature computation (EU AI Act Art.12)
    std::vector<int64_t>     source_row_ids;    ///< Source row IDs used in computation
};

// ============================================================================
// FeatureWriterOptions
// (defined outside FeatureWriter to avoid Apple Clang default-member-init bug)
// ============================================================================

/// Configuration options for FeatureWriter::create().
///
/// Defined at namespace scope (not nested in FeatureWriter) to work around an
/// Apple Clang restriction on default member initializers in nested aggregates.
///
/// @see FeatureWriter
struct FeatureWriterOptions {
    std::string     output_dir;                  ///< Directory for output Parquet files (created if missing).
    FeatureGroupDef group;                       ///< Feature group schema definition.
    size_t          row_group_size = 10'000;     ///< Rows per Parquet row group.
    size_t          max_file_rows  = 1'000'000;  ///< Rows per output file before rolling to a new file.
    std::string     file_prefix    = "features"; ///< Filename prefix for generated Parquet files.
};

// ============================================================================
// FeatureWriter — append-only writer for a single feature group
// ============================================================================

/// Append-only writer for a single feature group.
///
/// Writes typed, versioned feature vectors keyed by entity_id and timestamp_ns
/// into Parquet files. Schema layout per file:
///   col 0 = entity_id (STRING), col 1 = timestamp_ns (INT64),
///   col 2 = version (INT32), col 3..N = feature columns (DOUBLE).
///
/// Files are append-only and automatically rolled when `max_file_rows` is
/// reached. The writer flushes pending rows to a row group when the buffer
/// reaches `row_group_size`.
///
/// @note Movable but not copyable. The destructor calls close() if the writer
///       is still open.
///
/// @see FeatureWriterOptions, FeatureGroupDef, FeatureVector, FeatureReader
class FeatureWriter {
public:
    /// Alias for the options struct.
    using Options = FeatureWriterOptions;

    // -------------------------------------------------------------------------
    // Factory
    // -------------------------------------------------------------------------

    /// Create a new FeatureWriter for the given options.
    ///
    /// Creates the output directory if it does not exist. Validates that
    /// the output_dir does not contain path traversal components.
    ///
    /// @param opts  Writer configuration (output directory, group schema, etc.).
    /// @return A ready-to-write FeatureWriter, or an Error on validation/IO failure.
    [[nodiscard]] static expected<FeatureWriter> create(Options opts) {
        if (opts.output_dir.empty())
            return Error{ErrorCode::IO_ERROR,
                         "FeatureWriter: output_dir must not be empty"};
        // Validate output_dir against path traversal:
        // 1. Check raw path for '..' components (catches ../../etc, /tmp/a/../../../etc)
        // 2. Also check canonical path in case symlinks resolve to traversal
        // CWE-59: Improper Link Resolution Before File Access —
        // weakly_canonical() resolves symlinks for existing path prefixes but
        // cannot fully validate symlinks for not-yet-created directories.
        // Deployers should ensure the output directory is not a symlink to
        // an untrusted location.
        {
            std::filesystem::path raw(opts.output_dir);
            for (const auto& part : raw) {
                if (part == "..") {
                    return Error{ErrorCode::IO_ERROR,
                        "FeatureWriter: output_dir contains '..' path traversal"};
                }
            }
            std::filesystem::path canon = std::filesystem::weakly_canonical(raw);
            for (const auto& part : canon) {
                if (part == "..") {
                    return Error{ErrorCode::IO_ERROR,
                        "FeatureWriter: output_dir resolves to path with '..' traversal"};
                }
            }
        }
        if (opts.group.name.empty())
            return Error{ErrorCode::IO_ERROR,
                         "FeatureWriter: group.name must not be empty"};
        if (opts.group.feature_names.empty())
            return Error{ErrorCode::IO_ERROR,
                         "FeatureWriter: group.feature_names must not be empty"};

        std::error_code ec;
        std::filesystem::create_directories(opts.output_dir, ec);
        if (ec)
            return Error{ErrorCode::IO_ERROR,
                         "FeatureWriter: cannot create output_dir '" +
                         opts.output_dir + "': " + ec.message()};

        FeatureWriter fw;
        fw.opts_      = std::move(opts);
        fw.feat_bufs_.resize(fw.opts_.group.feature_names.size());
        return fw;
    }

    // -------------------------------------------------------------------------
    // Special members — movable, non-copyable
    // -------------------------------------------------------------------------

    /// Default-construct an empty writer (use create() factory instead).
    FeatureWriter() = default;
    FeatureWriter(FeatureWriter&&) noexcept = default;
    FeatureWriter& operator=(FeatureWriter&&) noexcept = default;
    FeatureWriter(const FeatureWriter&) = delete;
    FeatureWriter& operator=(const FeatureWriter&) = delete;

    ~FeatureWriter() {
        if (current_writer_) (void)close();
    }

    // -------------------------------------------------------------------------
    // Write a single feature vector
    // -------------------------------------------------------------------------

    /// Write a single feature vector to the store.
    ///
    /// Buffers internally; flushes a row group when the buffer reaches
    /// `row_group_size`.
    ///
    /// @param fv  The feature vector to write. Its `values.size()` must match
    ///            the number of feature names in the group definition.
    /// @return Success, or SCHEMA_MISMATCH if the value count is wrong.
    [[nodiscard]] expected<void> write(const FeatureVector& fv) {
        const size_t nfeat = opts_.group.feature_names.size();
        if (fv.values.size() != nfeat)
            return Error{ErrorCode::SCHEMA_MISMATCH,
                         "FeatureWriter: values.size() " +
                         std::to_string(fv.values.size()) +
                         " != feature_names.size() " +
                         std::to_string(nfeat)};

        entity_buf_.push_back(fv.entity_id);
        ts_buf_.push_back(fv.timestamp_ns);
        ver_buf_.push_back(fv.version);
        for (size_t i = 0; i < nfeat; ++i)
            feat_bufs_[i].push_back(fv.values[i]);

        ++pending_;
        ++total_rows_;

        if (pending_ >= opts_.row_group_size)
            return flush();

        return expected<void>{};
    }

    // -------------------------------------------------------------------------
    // Write a batch of feature vectors
    // -------------------------------------------------------------------------

    /// Write a contiguous array of feature vectors.
    /// @param fvs    Pointer to the first FeatureVector.
    /// @param count  Number of vectors to write.
    /// @return Success, or an error from an individual write.
    [[nodiscard]] expected<void> write_batch(const FeatureVector* fvs, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            auto r = write(fvs[i]);
            if (!r) return r;
        }
        return expected<void>{};
    }

    /// Write a vector of feature vectors.
    /// @param fvs  The feature vectors to write.
    /// @return Success, or an error from an individual write.
    [[nodiscard]] expected<void> write_batch(const std::vector<FeatureVector>& fvs) {
        return write_batch(fvs.data(), fvs.size());
    }

    // -------------------------------------------------------------------------
    // Flush pending rows to the current Parquet file (explicit call)
    // -------------------------------------------------------------------------

    /// Flush all buffered rows to the current Parquet file as a new row group.
    ///
    /// Opens a new file if none is currently open. Automatically rolls to a
    /// new file when `max_file_rows` is exceeded.
    ///
    /// @return Success, or an IO error from the underlying ParquetWriter.
    [[nodiscard]] expected<void> flush() {
        if (pending_ == 0) return expected<void>{};

        if (!current_writer_) {
            auto r = open_new_file();
            if (!r) return r;
        }

        const size_t nfeat = opts_.group.feature_names.size();

        // Do NOT clear buffers on write failure — preserve data for retry (CWE-459)
        { auto r = current_writer_->write_column(0, entity_buf_.data(), entity_buf_.size()); if (!r) return r; }
        { auto r = current_writer_->write_column<int64_t>(1, ts_buf_.data(), ts_buf_.size()); if (!r) return r; }
        { auto r = current_writer_->write_column<int32_t>(2, ver_buf_.data(), ver_buf_.size()); if (!r) return r; }
        for (size_t i = 0; i < nfeat; ++i) {
            auto r = current_writer_->write_column<double>(3 + i, feat_bufs_[i].data(), feat_bufs_[i].size());
            if (!r) return r;
        }
        { auto r = current_writer_->flush_row_group(); if (!r) return r; }

        current_file_rows_ += pending_;
        pending_ = 0;
        entity_buf_.clear();
        ts_buf_.clear();
        ver_buf_.clear();
        for (auto& fb : feat_bufs_) fb.clear();

        // Roll to new file if this one is full
        if (current_file_rows_ >= opts_.max_file_rows) {
            auto r = current_writer_->close();
            current_writer_.reset();
            current_file_rows_ = 0;
            if (!r) {
                // CWE-459: Incomplete Cleanup — remove the partial/corrupt
                // file on roll close failure to prevent downstream readers
                // from ingesting an incomplete Parquet file.
                std::error_code remove_ec;
                std::filesystem::remove(current_file_path_, remove_ec);
                return r.error();
            }
            // Register file only after confirmed successful close
            register_file(current_file_path_);
        }

        return expected<void>{};
    }

    // -------------------------------------------------------------------------
    // Close — flush remaining data, write Parquet footer, finalize file
    // -------------------------------------------------------------------------

    /// Close the writer: flush remaining data, write Parquet footer, and finalize the file.
    /// @return Success, or an IO error from the underlying ParquetWriter.
    [[nodiscard]] expected<void> close() {
        if (pending_ > 0) {
            auto r = flush();
            if (!r) return r;
        }
        if (current_writer_) {
            auto cr = current_writer_->close();
            current_writer_.reset();
            if (!cr) { current_file_rows_ = 0; return cr.error(); }
            // Register file only after confirmed successful close
            if (current_file_rows_ > 0) register_file(current_file_path_);
            current_file_rows_ = 0;
        }
        return expected<void>{};
    }

    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------

    /// Return the feature group definition used by this writer.
    [[nodiscard]] const FeatureGroupDef&   group_def()    const { return opts_.group; }
    /// Total number of rows written (including flushed and pending).
    [[nodiscard]] int64_t                  rows_written() const { return total_rows_; }
    /// Return a copy of all finalized output file paths (excludes the active file).
    [[nodiscard]] std::vector<std::string> output_files() const { return output_files_; }
    /// Check whether the writer currently has an open Parquet file.
    [[nodiscard]] bool                     is_open()      const { return static_cast<bool>(current_writer_); }

private:
    /// Open a new Parquet output file with the feature group schema.
    [[nodiscard]] expected<void> open_new_file() {
        // Build schema: entity_id | timestamp_ns | version | feat_0 .. feat_N
        auto sb = Schema::builder(opts_.group.name)
            .column<std::string>("entity_id")
            .column<int64_t>("timestamp_ns", LogicalType::TIMESTAMP_NS)
            .column<int32_t>("version");

        for (const auto& fname : opts_.group.feature_names)
            sb.column<double>(fname);

        Schema schema = sb.build();

        // Generate unique filename based on file_index + nanosecond timestamp
        using namespace std::chrono;
        const int64_t ts_ns = static_cast<int64_t>(
            duration_cast<nanoseconds>(
                system_clock::now().time_since_epoch()).count());
        char buf[80];
        std::snprintf(buf, sizeof(buf), "%08zu_%lld",
                      file_index_++, static_cast<long long>(ts_ns));

        current_file_path_ = opts_.output_dir + "/" + opts_.file_prefix
                           + "_" + opts_.group.name + "_" + buf + ".parquet";

        WriterOptions wo;
        wo.row_group_size = static_cast<int64_t>(opts_.row_group_size);

        auto wr = ParquetWriter::open(current_file_path_, schema, wo);
        if (!wr) return wr.error();

        current_writer_    = std::make_unique<ParquetWriter>(std::move(*wr));
        current_file_rows_ = 0;
        return expected<void>{};
    }

    /// Register a completed output file path (dedup'd).
    void register_file(const std::string& path) {
        for (const auto& p : output_files_) { if (p == path) return; }
        output_files_.push_back(path);
    }

    // -------------------------------------------------------------------------
    // State
    // -------------------------------------------------------------------------

    Options                        opts_;
    std::unique_ptr<ParquetWriter> current_writer_;
    std::string                    current_file_path_;
    size_t                         current_file_rows_{0};
    size_t                         file_index_{0};
    int64_t                        total_rows_{0};
    size_t                         pending_{0};

    // Per-row-group buffers (cleared after each flush)
    std::vector<std::string>          entity_buf_;
    std::vector<int64_t>              ts_buf_;
    std::vector<int32_t>              ver_buf_;
    std::vector<std::vector<double>>  feat_bufs_;  ///< feat_bufs_[i] = values for feature i

    std::vector<std::string>          output_files_;
};

} // namespace signet::forge
