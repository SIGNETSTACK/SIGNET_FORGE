// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

// ---------------------------------------------------------------------------
// writer.hpp — Streaming Parquet file writer
//
// Header-only. Writes valid Parquet files with configurable encoding and
// compression. Supports PLAIN, DELTA_BINARY_PACKED, BYTE_STREAM_SPLIT,
// RLE_DICTIONARY, and RLE encodings, plus SNAPPY/ZSTD/LZ4 compression.
// Supports row-based (string vector) and column-based (typed batch) APIs.
// Includes a standalone CSV-to-Parquet converter.
// ---------------------------------------------------------------------------

#include "signet/types.hpp"
#include "signet/error.hpp"
#include "signet/schema.hpp"
#include "signet/statistics.hpp"
#include "signet/column_writer.hpp"
#include "signet/thrift/compact.hpp"
#include "signet/thrift/types.hpp"
#include "signet/encoding/rle.hpp"
#include "signet/encoding/dictionary.hpp"
#include "signet/encoding/delta.hpp"
#include "signet/encoding/byte_stream_split.hpp"
#include "signet/compression/codec.hpp"
#include "signet/compression/snappy.hpp"
#include "signet/bloom/split_block.hpp"
#include "signet/column_index.hpp"

#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
#include "signet/crypto/pme.hpp"
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace signet::forge {

namespace detail::writer {

/// CRC-32 (polynomial 0xEDB88320) for page integrity (Parquet PageHeader.crc).
inline uint32_t page_crc32(const uint8_t* data, size_t length) noexcept {
    static constexpr auto make_table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    };
    static constexpr auto table = make_table();
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

} // namespace detail::writer

/// Configuration options for ParquetWriter.
///
/// Controls row group sizing, encoding, compression, bloom filters, page
/// indexes, file-level metadata, and (optionally) Parquet Modular Encryption.
/// An instance of this struct is passed to ParquetWriter::open(). All fields
/// have sensible defaults so a default-constructed WriterOptions produces
/// uncompressed, PLAIN-encoded Parquet files.
///
/// @see ParquetWriter::open
struct WriterOptions {
    /// Target number of rows per row group. When the row-based API
    /// accumulates this many pending rows, flush_row_group() is called
    /// automatically. Default: 65 536.
    int64_t row_group_size = 64 * 1024;

    /// Value written into the Parquet footer's "created_by" field.
    std::string created_by = SIGNET_CREATED_BY;

    /// Custom key-value metadata pairs embedded in the Parquet footer.
    std::vector<thrift::KeyValue> file_metadata;

    // -- Encoding & compression options --------------------------------------

    /// Default encoding applied to every column that does not have a
    /// per-column override in @ref column_encodings. Default: PLAIN.
    Encoding    default_encoding  = Encoding::PLAIN;

    /// Compression codec applied to every data and dictionary page.
    /// Default: UNCOMPRESSED.
    Compression compression       = Compression::UNCOMPRESSED;

    /// Per-column encoding overrides keyed by column name. Entries here
    /// take priority over @ref default_encoding and @ref auto_encoding.
    std::unordered_map<std::string, Encoding> column_encodings;

    /// When true, the writer automatically selects the best encoding for
    /// each column based on its physical type (e.g. DELTA_BINARY_PACKED
    /// for INT32/INT64, BYTE_STREAM_SPLIT for FLOAT/DOUBLE, RLE for
    /// BOOLEAN). Per-column overrides still take priority.
    bool auto_encoding    = false;

    /// When true, the writer samples page data and selects the most
    /// effective compression codec automatically.
    bool auto_compression = false;

    // -- Page Index (optional) ------------------------------------------------

    /// When true, a ColumnIndex and OffsetIndex are written for each column
    /// chunk, enabling predicate pushdown during reads.
    bool enable_page_index = false;

    // -- Bloom filters (optional) ---------------------------------------------

    /// When true, a Split Block Bloom Filter is written for each column
    /// (or for the subset named in @ref bloom_filter_columns).
    bool enable_bloom_filter = false;

    /// Target false-positive rate for bloom filters. Default: 1 %.
    double bloom_filter_fpr  = 0.01;

    /// Column names for which bloom filters should be generated. An empty
    /// set means all columns get a bloom filter when @ref enable_bloom_filter
    /// is true.
    std::unordered_set<std::string> bloom_filter_columns;

    // -- Encryption (BSL commercial tier only) --------------------------------
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
    /// Optional Parquet Modular Encryption (PME) configuration. When set,
    /// column data pages and (optionally) the footer are encrypted with
    /// AES-256-GCM/CTR. Requires the commercial build tier.
    std::optional<crypto::EncryptionConfig> encryption;
#endif
};

// ---------------------------------------------------------------------------
// ParquetWriter — streaming writer that writes one row group at a time
// ---------------------------------------------------------------------------

/// Streaming Parquet file writer with row-based and column-based APIs.
///
/// ParquetWriter is the primary write-path class in Signet Forge. It produces
/// spec-compliant Apache Parquet files with configurable encoding (PLAIN,
/// DELTA_BINARY_PACKED, BYTE_STREAM_SPLIT, RLE_DICTIONARY, RLE), compression
/// (Snappy, ZSTD, LZ4, Gzip), optional bloom filters, page indexes, and
/// Parquet Modular Encryption (commercial tier).
///
/// **Lifecycle:**
/// @code
///   auto w = ParquetWriter::open(path, schema, options);
///   w->write_column(0, data, n);  // or w->write_row({...})
///   w->flush_row_group();
///   auto stats = w->close();
/// @endcode
///
/// The class is move-only (non-copyable). If the user forgets to call close(),
/// the destructor performs a best-effort close.
///
/// @note Thread safety: ParquetWriter is **not** thread-safe. All calls must
///       be serialized by the caller.
///
/// @see WriterOptions, Schema, WriteStats, ParquetReader
class ParquetWriter {
public:
    /// Alias for WriterOptions, usable as `ParquetWriter::Options`.
    using Options = WriterOptions;

    // -- Factory: open a file for writing ------------------------------------

    /// Open a new Parquet file for writing.
    ///
    /// Creates (or truncates) the file at @p path, writes the 4-byte PAR1
    /// magic header, and initializes internal column writers, bloom filters,
    /// and page-index builders according to @p options. Parent directories
    /// are created automatically if they do not exist.
    ///
    /// @param path     Filesystem path for the output Parquet file.
    /// @param schema   Column schema describing names, physical types, and
    ///                 logical types.
    /// @param options  Writer configuration (encoding, compression, bloom
    ///                 filters, encryption, etc.). Defaults to plain,
    ///                 uncompressed output.
    /// @return An open ParquetWriter on success, or an Error (IO_ERROR) on
    ///         failure.
    /// @see close, WriterOptions
    [[nodiscard]] static expected<ParquetWriter> open(
            const std::filesystem::path& path,
            const Schema& schema,
            const Options& options = Options{}) {

        ParquetWriter writer;
        writer.schema_  = schema;
        writer.options_ = options;
        writer.path_    = path;

        // Create parent directories if needed
        if (path.has_parent_path()) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            // Ignore error — the open() below will catch it
        }

        writer.file_.open(path, std::ios::binary | std::ios::trunc);
        if (!writer.file_.is_open()) {
            return Error{ErrorCode::IO_ERROR,
                         "Failed to open file for writing: " + path.string()};
        }

        // Write PAR1 magic (4 bytes)
        writer.write_raw_le32(PARQUET_MAGIC);
        writer.file_offset_ = 4;

        // Initialize column writers — one per schema column
        writer.init_column_writers();

        // Initialize encryption if configured (commercial tier)
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        if (options.encryption) {
            writer.encryptor_ = std::make_unique<crypto::FileEncryptor>(*options.encryption);
        }
#endif

        // Initialize bloom filters if enabled
        if (options.enable_bloom_filter) {
            writer.bloom_filters_.resize(schema.num_columns());
        }

        // Initialize page index builders if enabled
        if (options.enable_page_index) {
            writer.col_index_builders_.resize(schema.num_columns());
        }

        writer.open_ = true;
        return writer;
    }

    // -- Row-based API -------------------------------------------------------

    /// Write a single row as a vector of string values.
    ///
    /// Each string is parsed and converted to its column's physical type when
    /// the row group is flushed (either automatically when @ref
    /// WriterOptions::row_group_size is reached, or explicitly via
    /// flush_row_group()). The number of values must exactly match the
    /// schema's column count.
    ///
    /// @param values  One string per column, in schema order.
    /// @return `expected<void>` -- error if the writer is closed or if
    ///         @p values.size() does not match the schema.
    /// @see write_column, flush_row_group
    [[nodiscard]] expected<void> write_row(const std::vector<std::string>& values) {
        if (!open_) {
            return Error{ErrorCode::IO_ERROR, "Writer is not open"};
        }
        if (values.size() != schema_.num_columns()) {
            return Error{ErrorCode::SCHEMA_MISMATCH,
                         "Row has " + std::to_string(values.size()) +
                         " values, schema has " + std::to_string(schema_.num_columns()) +
                         " columns"};
        }

        pending_rows_.push_back(values);

        // Auto-flush when we reach the target row group size
        if (static_cast<int64_t>(pending_rows_.size()) >= options_.row_group_size) {
            return flush_row_group();
        }

        return expected<void>{};
    }

    // -- Schema query --------------------------------------------------------

    /// Returns the number of columns in the writer's schema.
    /// @return Column count (always >= 1 for a validly-opened writer).
    [[nodiscard]] size_t num_columns() const noexcept { return schema_.num_columns(); }

    // -- Column-based API (typed batch) --------------------------------------

    /// Write a batch of typed values to a single column.
    ///
    /// The caller writes each column independently and then calls
    /// flush_row_group(). All columns within a row group must receive the
    /// same number of values; a mismatch is detected at flush time.
    ///
    /// Supported template types map to Parquet physical types:
    /// - `bool`        -> BOOLEAN
    /// - `int32_t`     -> INT32
    /// - `int64_t`     -> INT64
    /// - `float`       -> FLOAT
    /// - `double`      -> DOUBLE
    /// - `std::string` -> BYTE_ARRAY (use the string overload instead)
    ///
    /// @tparam T       C++ type matching the column's physical type.
    /// @param col_index  Zero-based column index in the schema.
    /// @param values     Pointer to a contiguous array of @p count values.
    /// @param count      Number of values to write.
    /// @return `expected<void>` -- error if the writer is closed or
    ///         @p col_index is out of range.
    /// @see write_column(size_t, const std::string*, size_t), flush_row_group
    template <typename T>
    [[nodiscard]] expected<void> write_column(size_t col_index,
                                              const T* values, size_t count) {
        if (!open_) {
            return Error{ErrorCode::IO_ERROR, "Writer is not open"};
        }
        if (col_index >= schema_.num_columns()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "Column index " + std::to_string(col_index) +
                         " out of range (schema has " +
                         std::to_string(schema_.num_columns()) + " columns)"};
        }

        col_writers_[col_index].write_batch(values, count);
        col_row_counts_[col_index] = col_writers_[col_index].num_values();

        // Insert values into bloom filter if enabled for this column
        if (!bloom_filters_.empty()) {
            bloom_insert_typed(col_index, values, count);
        }

        return expected<void>{};
    }

    /// Write a batch of string values to a BYTE_ARRAY column.
    ///
    /// This overload handles variable-length binary / UTF-8 data. Each string
    /// is stored with a 4-byte little-endian length prefix in the PLAIN
    /// encoding buffer, matching the Parquet BYTE_ARRAY wire format.
    ///
    /// @param col_index  Zero-based column index in the schema.
    /// @param values     Pointer to a contiguous array of @p count strings.
    /// @param count      Number of string values to write.
    /// @return `expected<void>` -- error if the writer is closed or
    ///         @p col_index is out of range.
    /// @see write_column(size_t, const T*, size_t)
    [[nodiscard]] expected<void> write_column(size_t col_index,
                                              const std::string* values,
                                              size_t count) {
        if (!open_) {
            return Error{ErrorCode::IO_ERROR, "Writer is not open"};
        }
        if (col_index >= schema_.num_columns()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "Column index " + std::to_string(col_index) +
                         " out of range"};
        }

        col_writers_[col_index].write_batch(values, count);
        col_row_counts_[col_index] = col_writers_[col_index].num_values();

        // Insert values into bloom filter if enabled for this column
        if (!bloom_filters_.empty()) {
            bloom_insert_typed(col_index, values, count);
        }

        return expected<void>{};
    }

    // -- Flush / Close -------------------------------------------------------

    /// Flush the current row group to disk.
    ///
    /// Encodes any pending string rows (row-based API), verifies that all
    /// columns have the same value count, writes column chunks with the
    /// selected encoding and compression, emits bloom filters and page
    /// indexes if enabled, and records the row group metadata for the footer.
    ///
    /// This method is called automatically by write_row() when the pending
    /// row count reaches WriterOptions::row_group_size, and by close() to
    /// drain any remaining data. It may also be called explicitly to control
    /// row group boundaries.
    ///
    /// @return `expected<void>` -- error on I/O failure, schema mismatch
    ///         (column value counts differ), or compression/encryption error.
    /// @note  Calling flush_row_group() when no data is pending is a no-op.
    /// @see close, write_row, write_column
    [[nodiscard]] expected<void> flush_row_group() {
        if (!open_) {
            return Error{ErrorCode::IO_ERROR, "Writer is not open"};
        }

        // Ensure Snappy codec is registered (idempotent, done once)
        ensure_snappy_registered();

        // If we have pending string rows, encode them into the column writers
        if (!pending_rows_.empty()) {
            auto result = encode_pending_rows();
            if (!result) return result;
        }

        // Check that we have data to flush
        bool has_data = false;
        for (size_t c = 0; c < col_writers_.size(); ++c) {
            if (col_writers_[c].num_values() > 0) {
                has_data = true;
                break;
            }
        }
        if (!has_data) {
            return expected<void>{};  // Nothing to flush
        }

        // Verify all columns have the same number of values
        int64_t rg_num_rows = col_writers_[0].num_values();
        for (size_t c = 1; c < col_writers_.size(); ++c) {
            if (col_writers_[c].num_values() != rg_num_rows) {
                return Error{ErrorCode::SCHEMA_MISMATCH,
                             "Column " + std::to_string(c) + " has " +
                             std::to_string(col_writers_[c].num_values()) +
                             " values, expected " + std::to_string(rg_num_rows)};
            }
        }

        // Build the row group metadata
        thrift::RowGroup rg;
        rg.num_rows = rg_num_rows;
        rg.total_byte_size = 0;
        rg.columns.resize(col_writers_.size());

        // Write each column chunk with encoding and compression
        for (size_t c = 0; c < col_writers_.size(); ++c) {
            const auto& cw = col_writers_[c];
            const auto& col_desc = schema_.column(c);

            // ---- Step 1: Choose encoding for this column -------------------
            Encoding col_encoding = choose_encoding(c, col_desc, cw);

            // ---- Step 2: Determine compression codec -----------------------
            Compression col_codec = options_.compression;
            if (options_.auto_compression) {
                // Use the PLAIN data as a sample for auto-selection
                col_codec = auto_select_compression(
                    cw.data().data(), cw.data().size());
            }

            // ---- Step 3: Track total uncompressed and compressed bytes -----
            int64_t total_uncompressed = 0;
            int64_t total_compressed   = 0;
            std::unordered_set<Encoding> used_encodings;

            // Record the column chunk start offset
            int64_t column_offset = file_offset_;
            int64_t dict_page_offset = -1;  // -1 means no dict page
            int64_t data_page_offset = -1;

            // ---- Step 4: Handle dictionary encoding specially --------------
            if (col_encoding == Encoding::PLAIN_DICTIONARY ||
                col_encoding == Encoding::RLE_DICTIONARY) {

                // Re-encode as dictionary: extract raw values from PLAIN data
                // and build a dictionary.
                auto dict_result = write_dictionary_column(
                    c, col_desc, cw, col_codec);
                if (!dict_result) return dict_result.error();

                const auto& dict_info = *dict_result;
                total_uncompressed = dict_info.total_uncompressed;
                total_compressed   = dict_info.total_compressed;
                used_encodings     = dict_info.used_encodings;
                dict_page_offset   = dict_info.dict_page_offset;
                data_page_offset   = dict_info.data_page_offset;

                // Feed page index builder (dictionary path)
                if (!col_index_builders_.empty()) {
                    auto& builder = col_index_builders_[c];
                    builder.start_page();
                    builder.set_first_row_index(0);
                    builder.set_page_location(dict_info.data_page_offset,
                        static_cast<int32_t>(dict_info.total_compressed));

                    const auto& cw_stats_pi = cw.statistics();
                    if (cw_stats_pi.has_min_max()) {
                        const auto& min_b = cw_stats_pi.min_bytes();
                        const auto& max_b = cw_stats_pi.max_bytes();
                        builder.set_min(std::string(min_b.begin(), min_b.end()));
                        builder.set_max(std::string(max_b.begin(), max_b.end()));
                    }
                    builder.set_null_page(cw_stats_pi.null_count() == cw.num_values());
                    builder.set_null_count(cw_stats_pi.null_count());
                }

            } else {
                // ---- Step 5: Non-dictionary encoding path ------------------
                // Encode (or reuse PLAIN data) based on selected encoding
                auto encoded = encode_column_data(cw, col_encoding, col_desc.physical_type);

                if (encoded.size() > static_cast<size_t>(INT32_MAX)) {
                    return Error{ErrorCode::INTERNAL_ERROR,
                                 "encoded page size exceeds int32 limit (2 GiB)"};
                }
                int32_t uncompressed_size = static_cast<int32_t>(encoded.size());

                // Compress the page data
                const uint8_t* page_data = encoded.data();
                size_t page_data_size = encoded.size();
                std::vector<uint8_t> compressed_buf;
                int32_t compressed_size = uncompressed_size;

                if (col_codec != Compression::UNCOMPRESSED) {
                    auto comp_result = compress(col_codec, page_data, page_data_size);
                    if (!comp_result) return comp_result.error();
                    compressed_buf = std::move(*comp_result);
                    compressed_size = static_cast<int32_t>(compressed_buf.size());
                    page_data = compressed_buf.data();
                    page_data_size = compressed_buf.size();
                }

                // Encrypt the page data if PME is configured for this column
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
                std::vector<uint8_t> encrypted_buf;
                if (encryptor_ && encryptor_->is_column_encrypted(col_desc.name)) {
                    auto enc_result = encryptor_->encrypt_column_page(
                        page_data, page_data_size, col_desc.name,
                        static_cast<int32_t>(row_groups_.size()), 0);
                    if (!enc_result) return enc_result.error();
                    encrypted_buf = std::move(*enc_result);
                    compressed_size = static_cast<int32_t>(encrypted_buf.size());
                    page_data = encrypted_buf.data();
                    page_data_size = encrypted_buf.size();
                    // uncompressed_page_size stays the same (pre-encryption size
                    // for the reader to know how much to decompress after decrypt)
                }
#endif

                // Build the DataPage PageHeader
                thrift::PageHeader ph;
                ph.type = PageType::DATA_PAGE;
                ph.uncompressed_page_size = uncompressed_size;
                ph.compressed_page_size   = compressed_size;
                // CRC-32 over final page data (post-compression, post-encryption)
                ph.crc = static_cast<int32_t>(detail::writer::page_crc32(
                    page_data, page_data_size));

                thrift::DataPageHeader dph;
                dph.num_values                = static_cast<int32_t>(cw.num_values());
                dph.encoding                  = col_encoding;
                dph.definition_level_encoding = Encoding::RLE;
                dph.repetition_level_encoding = Encoding::RLE;
                ph.data_page_header = dph;

                // Serialize and write the page header
                thrift::CompactEncoder header_enc;
                ph.serialize(header_enc);
                const auto& header_bytes = header_enc.data();

                data_page_offset = file_offset_;

                write_raw(header_bytes.data(), header_bytes.size());
                write_raw(page_data, page_data_size);

                total_uncompressed = static_cast<int64_t>(header_bytes.size()) +
                                     static_cast<int64_t>(uncompressed_size);
                total_compressed   = static_cast<int64_t>(header_bytes.size()) +
                                     static_cast<int64_t>(compressed_size);

                used_encodings.insert(col_encoding);

                // Feed page index builder (non-dictionary path)
                if (!col_index_builders_.empty()) {
                    auto& builder = col_index_builders_[c];
                    builder.start_page();
                    builder.set_first_row_index(0);
                    builder.set_page_location(data_page_offset,
                        compressed_size + static_cast<int32_t>(header_bytes.size()));

                    const auto& cw_stats_pi = cw.statistics();
                    if (cw_stats_pi.has_min_max()) {
                        const auto& min_b = cw_stats_pi.min_bytes();
                        const auto& max_b = cw_stats_pi.max_bytes();
                        builder.set_min(std::string(min_b.begin(), min_b.end()));
                        builder.set_max(std::string(max_b.begin(), max_b.end()));
                    }
                    builder.set_null_page(cw_stats_pi.null_count() == cw.num_values());
                    builder.set_null_count(cw_stats_pi.null_count());
                }
            }

            // ---- Step 6: Build ColumnChunk metadata ------------------------
            thrift::ColumnChunk& cc = rg.columns[c];
            cc.file_offset = column_offset;

            thrift::ColumnMetaData cmd;
            cmd.type           = col_desc.physical_type;
            cmd.path_in_schema = {col_desc.name};
            cmd.codec          = col_codec;
            cmd.num_values     = cw.num_values();
            cmd.total_uncompressed_size = total_uncompressed;
            cmd.total_compressed_size   = total_compressed;
            cmd.data_page_offset        = data_page_offset;

            // Set encodings list
            cmd.encodings.assign(used_encodings.begin(), used_encodings.end());

            // Set dictionary page offset if applicable
            if (dict_page_offset >= 0) {
                cmd.dictionary_page_offset = dict_page_offset;
            }

            // Populate statistics from ColumnWriter
            const auto& cw_stats = cw.statistics();
            if (cw_stats.has_min_max()) {
                thrift::Statistics stats;
                stats.null_count = cw_stats.null_count();

                // Convert min/max bytes to binary strings for Thrift
                const auto& min_b = cw_stats.min_bytes();
                const auto& max_b = cw_stats.max_bytes();
                stats.min_value = std::string(min_b.begin(), min_b.end());
                stats.max_value = std::string(max_b.begin(), max_b.end());
                // Also set legacy min/max for backward compatibility
                stats.min = stats.min_value;
                stats.max = stats.max_value;

                if (cw_stats.distinct_count().has_value()) {
                    stats.distinct_count = *cw_stats.distinct_count();
                }

                cmd.statistics = stats;
            }

            cc.meta_data = cmd;

            // ---- Step 7: Write bloom filter for this column (if enabled) ---
            if (!bloom_filters_.empty() && bloom_filters_[c]) {
                int64_t bf_offset = file_offset_;
                const auto& bf_data = bloom_filters_[c]->data();
                uint32_t bf_size = static_cast<uint32_t>(bf_data.size());

                // Write: 4-byte LE header (total bloom filter size) + raw bytes
                write_raw_le32(bf_size);
                write_raw(bf_data.data(), bf_data.size());

                cc.bloom_filter_offset = bf_offset;
                cc.bloom_filter_length = static_cast<int32_t>(4 + bf_data.size());

                rg.total_byte_size += static_cast<int64_t>(4 + bf_data.size());
            }

            // ---- Step 8: Write ColumnIndex + OffsetIndex (if enabled) ----
            if (!col_index_builders_.empty()) {
                auto& builder = col_index_builders_[c];

                // Serialize and write ColumnIndex
                auto col_idx = builder.build_column_index();
                thrift::CompactEncoder ci_enc;
                col_idx.serialize(ci_enc);
                int64_t ci_offset = file_offset_;
                write_raw(ci_enc.data().data(), ci_enc.data().size());
                cc.column_index_offset = ci_offset;
                cc.column_index_length = static_cast<int32_t>(ci_enc.data().size());

                // Serialize and write OffsetIndex
                auto off_idx = builder.build_offset_index();
                thrift::CompactEncoder oi_enc;
                off_idx.serialize(oi_enc);
                int64_t oi_offset = file_offset_;
                write_raw(oi_enc.data().data(), oi_enc.data().size());
                cc.offset_index_offset = oi_offset;
                cc.offset_index_length = static_cast<int32_t>(oi_enc.data().size());

                rg.total_byte_size += static_cast<int64_t>(ci_enc.data().size() + oi_enc.data().size());
            }

            rg.total_byte_size += total_compressed;
        }

        // Record the row group
        row_groups_.push_back(std::move(rg));
        total_rows_ += rg_num_rows;

        // Reset column writers for the next row group
        for (auto& cw : col_writers_) {
            cw.reset();
        }
        for (auto& count : col_row_counts_) {
            count = 0;
        }

        // Reset bloom filters for the next row group
        for (auto& bf : bloom_filters_) {
            if (bf) bf->reset();
        }

        // Reset page index builders for the next row group
        for (auto& builder : col_index_builders_) {
            builder.reset();
        }

        return expected<void>{};
    }

    /// Close the file and finalize the Parquet footer.
    ///
    /// Flushes any remaining row data via flush_row_group(), serializes the
    /// Thrift FileMetaData (schema, row group metadata, statistics, custom
    /// key-value pairs), writes the footer length as a 4-byte LE integer,
    /// and appends the closing PAR1 magic (or PARE for encrypted footers).
    ///
    /// After close() returns, the file on disk is a complete, spec-valid
    /// Parquet file. Calling close() on an already-closed writer is safe
    /// and returns an empty WriteStats.
    ///
    /// @return WriteStats summarizing file size, row/row-group counts,
    ///         per-column compression ratios, and encoding details.
    /// @note  The writer **must** be closed (explicitly or via the destructor)
    ///        to produce a valid Parquet file. Omitting close() results in a
    ///        truncated, unreadable file.
    /// @see flush_row_group, WriteStats
    [[nodiscard]] expected<WriteStats> close() {
        if (!open_) {
            return WriteStats{};  // Already closed — return empty stats
        }

        // Validate footer completeness before close (Parquet spec)
        if (schema_.num_columns() == 0) {
            file_.close();
            open_ = false;
            return Error{ErrorCode::INTERNAL_ERROR, "ParquetWriter: cannot close with empty schema"};
        }

        // Flush any remaining data
        auto flush_result = flush_row_group();
        if (!flush_result) {
            file_.close();
            open_ = false;
            return flush_result.error();
        }

        // Build FileMetaData
        thrift::FileMetaData fmd;
        fmd.version    = PARQUET_VERSION;
        fmd.num_rows   = total_rows_;
        fmd.row_groups = row_groups_;
        fmd.created_by = options_.created_by;

        // Build schema elements: root + one per column
        // Root element: group node with num_children = num_columns
        thrift::SchemaElement root;
        root.name         = schema_.name();
        root.num_children = static_cast<int32_t>(schema_.num_columns());
        // Root has no type (it's a group node)
        fmd.schema.push_back(root);

        // One element per leaf column
        for (size_t c = 0; c < schema_.num_columns(); ++c) {
            const auto& col_desc = schema_.column(c);

            thrift::SchemaElement elem;
            elem.type            = col_desc.physical_type;
            elem.name            = col_desc.name;
            elem.repetition_type = col_desc.repetition;

            // Set type_length for FIXED_LEN_BYTE_ARRAY
            if (col_desc.physical_type == PhysicalType::FIXED_LEN_BYTE_ARRAY &&
                col_desc.type_length > 0) {
                elem.type_length = col_desc.type_length;
            }

            // Set converted type for common logical types
            if (col_desc.logical_type == LogicalType::STRING) {
                elem.converted_type = ConvertedType::UTF8;
            } else if (col_desc.logical_type == LogicalType::DATE) {
                elem.converted_type = ConvertedType::DATE;
            } else if (col_desc.logical_type == LogicalType::TIMESTAMP_MS) {
                elem.converted_type = ConvertedType::TIMESTAMP_MILLIS;
            } else if (col_desc.logical_type == LogicalType::TIMESTAMP_US) {
                elem.converted_type = ConvertedType::TIMESTAMP_MICROS;
            } else if (col_desc.logical_type == LogicalType::JSON) {
                elem.converted_type = ConvertedType::JSON;
            } else if (col_desc.logical_type == LogicalType::ENUM) {
                elem.converted_type = ConvertedType::ENUM;
            } else if (col_desc.logical_type == LogicalType::DECIMAL) {
                elem.converted_type = ConvertedType::DECIMAL;
                if (col_desc.precision > 0) elem.precision = col_desc.precision;
                if (col_desc.scale >= 0)    elem.scale     = col_desc.scale;
            }

            fmd.schema.push_back(elem);
        }

        // Set custom key-value metadata
        if (!options_.file_metadata.empty()) {
            fmd.key_value_metadata = options_.file_metadata;
        }

        // If encryption is configured, embed FileEncryptionProperties in
        // file metadata so the reader knows this file uses PME.
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        if (encryptor_) {
            auto file_props = encryptor_->file_properties();
            auto props_bytes = file_props.serialize();
            std::string props_str(props_bytes.begin(), props_bytes.end());

            thrift::KeyValue enc_kv;
            enc_kv.key   = "signet.encryption.properties";
            enc_kv.value = std::move(props_str);

            if (!fmd.key_value_metadata.has_value()) {
                fmd.key_value_metadata = std::vector<thrift::KeyValue>{};
            }
            fmd.key_value_metadata->push_back(std::move(enc_kv));
        }
#endif

        // Serialize FileMetaData to Thrift compact protocol
        thrift::CompactEncoder enc;
        fmd.serialize(enc);
        const auto& footer_bytes = enc.data();

        // Record footer start position
        int64_t footer_start = file_offset_;
        (void)footer_start;  // Not needed, but useful for debugging

        // Footer encryption: if encryptor is set and footer encryption is
        // enabled, encrypt the serialized FileMetaData and write "PARE" magic.
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        if (encryptor_ && encryptor_->config().encrypt_footer) {
            auto enc_footer = encryptor_->encrypt_footer(
                footer_bytes.data(), footer_bytes.size());
            if (!enc_footer) {
                file_.close();
                open_ = false;
                return enc_footer.error();
            }
            const auto& encrypted_footer = *enc_footer;

            // Write encrypted footer
            write_raw(encrypted_footer.data(), encrypted_footer.size());

            // Write footer length (encrypted footer size)
            uint32_t footer_len = static_cast<uint32_t>(encrypted_footer.size());
            write_raw_le32(footer_len);

            // Write "PARE" magic to indicate encrypted footer
            write_raw_le32(PARQUET_MAGIC_ENCRYPTED);
        } else {
#endif
            // Write footer in plaintext (standard path)
            write_raw(footer_bytes.data(), footer_bytes.size());

            // Write footer length (4 bytes LE)
            uint32_t footer_len = static_cast<uint32_t>(footer_bytes.size());
            write_raw_le32(footer_len);

            // Write closing PAR1 magic (4 bytes)
            write_raw_le32(PARQUET_MAGIC);
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        }
#endif

        file_.flush();
        file_.close();
        open_ = false;

        // Build WriteStats from accumulated row group metadata
        return build_write_stats();
    }

    /// Destructor. Performs a best-effort close() if the file is still open.
    ///
    /// Any errors during the implicit close are silently discarded. Prefer
    /// calling close() explicitly so that errors and WriteStats can be
    /// inspected.
    ~ParquetWriter() {
        if (open_) {
            // Best-effort close; ignore errors in destructor
            (void)close();
        }
    }

    // -- Non-copyable, movable -----------------------------------------------

    /// Deleted copy constructor. ParquetWriter is move-only.
    ParquetWriter(const ParquetWriter&) = delete;
    /// Deleted copy-assignment operator. ParquetWriter is move-only.
    ParquetWriter& operator=(const ParquetWriter&) = delete;

    /// Move constructor. Transfers ownership of the open file and all
    /// internal state from @p other. After the move, @p other is in a
    /// closed, empty state.
    ParquetWriter(ParquetWriter&& other) noexcept
        : schema_(std::move(other.schema_))
        , options_(std::move(other.options_))
        , path_(std::move(other.path_))
        , file_(std::move(other.file_))
        , file_offset_(other.file_offset_)
        , col_writers_(std::move(other.col_writers_))
        , col_row_counts_(std::move(other.col_row_counts_))
        , pending_rows_(std::move(other.pending_rows_))
        , row_groups_(std::move(other.row_groups_))
        , total_rows_(other.total_rows_)
        , open_(other.open_)
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        , encryptor_(std::move(other.encryptor_))
#endif
        , bloom_filters_(std::move(other.bloom_filters_))
        , col_index_builders_(std::move(other.col_index_builders_))
    {
        other.open_ = false;
        other.file_offset_ = 0;
        other.total_rows_ = 0;
    }

    /// Move-assignment operator. Closes the current file (if open) before
    /// transferring ownership from @p other.
    ParquetWriter& operator=(ParquetWriter&& other) noexcept {
        if (this != &other) {
            // Close current file if open
            if (open_) {
                (void)close();
            }

            schema_         = std::move(other.schema_);
            options_        = std::move(other.options_);
            path_           = std::move(other.path_);
            file_           = std::move(other.file_);
            file_offset_    = other.file_offset_;
            col_writers_    = std::move(other.col_writers_);
            col_row_counts_ = std::move(other.col_row_counts_);
            pending_rows_   = std::move(other.pending_rows_);
            row_groups_     = std::move(other.row_groups_);
            total_rows_     = other.total_rows_;
            open_           = other.open_;
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
            encryptor_      = std::move(other.encryptor_);
#endif
            bloom_filters_  = std::move(other.bloom_filters_);
            col_index_builders_ = std::move(other.col_index_builders_);

            other.open_ = false;
            other.file_offset_ = 0;
            other.total_rows_ = 0;
        }
        return *this;
    }

    // -- Status queries ------------------------------------------------------

    /// Returns the total number of rows written so far.
    ///
    /// Includes both rows already flushed to completed row groups and rows
    /// buffered in memory awaiting the next flush_row_group() call.
    ///
    /// @return Total row count (flushed + pending).
    [[nodiscard]] int64_t rows_written() const {
        return total_rows_ + static_cast<int64_t>(pending_rows_.size());
    }

    /// Returns the number of row groups that have been flushed to disk.
    /// @return Count of completed row groups (does not include any
    ///         in-progress row group that has not yet been flushed).
    [[nodiscard]] int64_t row_groups_written() const {
        return static_cast<int64_t>(row_groups_.size());
    }

    /// Returns whether the writer is open and accepting data.
    /// @return `true` if the writer is open, `false` after close() or move.
    [[nodiscard]] bool is_open() const { return open_; }

    // ========================================================================
    // Standalone CSV-to-Parquet converter
    // ========================================================================

    /// Convert a CSV file to a Parquet file.
    ///
    /// Reads the entire CSV into memory, auto-detects column types by
    /// scanning every value in each column (priority: INT64 > DOUBLE >
    /// BOOLEAN > STRING), builds a Schema, writes all rows through a
    /// ParquetWriter, and closes the output file.
    ///
    /// The first line of the CSV is treated as the header (column names).
    /// Quoted fields with embedded commas and escaped double-quotes (`""`)
    /// are supported.
    ///
    /// @param csv_input       Path to the input CSV file.
    /// @param parquet_output  Path for the output Parquet file (created or
    ///                        truncated).
    /// @param options         Writer options forwarded to ParquetWriter::open().
    /// @return `expected<void>` -- error on I/O failure, empty CSV, or any
    ///         write/close error.
    /// @note  The entire CSV is loaded into memory; very large files may
    ///        require a streaming approach instead.
    /// @see ParquetWriter::open
    [[nodiscard]] static expected<void> csv_to_parquet(
            const std::filesystem::path& csv_input,
            const std::filesystem::path& parquet_output,
            const Options& options = Options{}) {

        // Open CSV
        std::ifstream csv(csv_input);
        if (!csv.is_open()) {
            return Error{ErrorCode::IO_ERROR,
                         "Failed to open CSV file: " + csv_input.string()};
        }

        // Read header line
        std::string header_line;
        if (!std::getline(csv, header_line)) {
            return Error{ErrorCode::INVALID_FILE, "CSV file is empty"};
        }

        auto col_names = split_csv_line(header_line);
        if (col_names.empty()) {
            return Error{ErrorCode::INVALID_FILE, "CSV header has no columns"};
        }

        size_t num_cols = col_names.size();

        // Read all data rows
        std::vector<std::vector<std::string>> rows;
        std::string line;
        while (std::getline(csv, line)) {
            if (line.empty()) continue;
            auto fields = split_csv_line(line);
            // Pad or truncate to match header width
            fields.resize(num_cols);
            rows.push_back(std::move(fields));
        }
        csv.close();

        if (rows.empty()) {
            return Error{ErrorCode::INVALID_FILE, "CSV file has no data rows"};
        }

        // Auto-detect column types by scanning all values
        // For each column, try in order: INT64 → DOUBLE → BOOLEAN → STRING
        std::vector<PhysicalType> detected_types(num_cols, PhysicalType::INT64);
        std::vector<LogicalType>  detected_logical(num_cols, LogicalType::NONE);

        for (size_t c = 0; c < num_cols; ++c) {
            bool all_int64  = true;
            bool all_double = true;
            bool all_bool   = true;

            for (const auto& row : rows) {
                const std::string& val = row[c];
                if (val.empty()) continue;  // Skip empty values for type detection

                // Try INT64
                if (all_int64) {
                    int64_t parsed;
                    auto [ptr, ec] = std::from_chars(val.data(),
                                                      val.data() + val.size(),
                                                      parsed);
                    if (ec != std::errc{} || ptr != val.data() + val.size()) {
                        all_int64 = false;
                    }
                }

                // Try DOUBLE
                if (all_double) {
                    char* end = nullptr;
                    (void)std::strtod(val.c_str(), &end);
                    if (end == val.c_str() || end != val.c_str() + val.size()) {
                        all_double = false;
                    }
                }

                // Try BOOLEAN
                if (all_bool) {
                    if (val != "true" && val != "false" &&
                        val != "TRUE" && val != "FALSE" &&
                        val != "True" && val != "False" &&
                        val != "1" && val != "0") {
                        all_bool = false;
                    }
                }
            }

            // Priority: INT64 > DOUBLE > BOOLEAN > STRING
            if (all_int64) {
                detected_types[c]   = PhysicalType::INT64;
                detected_logical[c] = LogicalType::NONE;
            } else if (all_double) {
                detected_types[c]   = PhysicalType::DOUBLE;
                detected_logical[c] = LogicalType::NONE;
            } else if (all_bool) {
                detected_types[c]   = PhysicalType::BOOLEAN;
                detected_logical[c] = LogicalType::NONE;
            } else {
                detected_types[c]   = PhysicalType::BYTE_ARRAY;
                detected_logical[c] = LogicalType::STRING;
            }
        }

        // Build schema from detected types
        std::vector<ColumnDescriptor> col_descs;
        col_descs.reserve(num_cols);
        for (size_t c = 0; c < num_cols; ++c) {
            ColumnDescriptor cd;
            cd.name          = col_names[c];
            cd.physical_type = detected_types[c];
            cd.logical_type  = detected_logical[c];
            col_descs.push_back(std::move(cd));
        }

        Schema schema("csv_data", std::move(col_descs));

        // Open Parquet writer
        auto writer_result = ParquetWriter::open(parquet_output, schema, options);
        if (!writer_result) {
            return writer_result.error();
        }
        auto& writer = *writer_result;

        // Write all rows
        for (const auto& row : rows) {
            auto result = writer.write_row(row);
            if (!result) {
                return result;
            }
        }

        // Close
        auto close_result = writer.close();
        if (!close_result) return close_result.error();
        return expected<void>{};
    }

private:
    /// Default constructor (private). Use open() to create instances.
    ParquetWriter() = default;

    // -- Internal state -------------------------------------------------------

    Schema                                 schema_;
    Options                                options_;
    std::filesystem::path                  path_;
    std::ofstream                          file_;
    int64_t                                file_offset_  = 0;
    std::vector<ColumnWriter>              col_writers_;
    std::vector<int64_t>                   col_row_counts_;
    std::vector<std::vector<std::string>>  pending_rows_;
    std::vector<thrift::RowGroup>          row_groups_;
    int64_t                                total_rows_   = 0;
    bool                                   open_         = false;
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
    std::unique_ptr<crypto::FileEncryptor> encryptor_;     // PME encryption (nullptr if none)
#endif
    std::vector<std::unique_ptr<SplitBlockBloomFilter>> bloom_filters_; // Per-column bloom filters
    std::vector<ColumnIndexBuilder> col_index_builders_;  // Per-column page index builders

    // -- Initialization -------------------------------------------------------

    void init_column_writers() {
        col_writers_.clear();
        col_writers_.reserve(schema_.num_columns());
        col_row_counts_.resize(schema_.num_columns(), 0);
        for (size_t c = 0; c < schema_.num_columns(); ++c) {
            col_writers_.emplace_back(schema_.column(c).physical_type);
        }
    }

    // -- Build WriteStats from accumulated row group metadata -----------------

    WriteStats build_write_stats() const {
        WriteStats stats;
        stats.file_size_bytes  = file_offset_;
        stats.total_rows       = total_rows_;
        stats.total_row_groups = static_cast<int64_t>(row_groups_.size());

        // Aggregate per-column stats across all row groups
        size_t num_cols = schema_.num_columns();
        stats.columns.resize(num_cols);

        for (size_t c = 0; c < num_cols; ++c) {
            auto& col_stats       = stats.columns[c];
            col_stats.column_name = schema_.column(c).name;
            col_stats.physical_type = schema_.column(c).physical_type;
        }

        for (const auto& rg : row_groups_) {
            for (size_t c = 0; c < rg.columns.size() && c < num_cols; ++c) {
                if (!rg.columns[c].meta_data.has_value()) continue;
                const auto& cmd = *rg.columns[c].meta_data;
                auto& col_stats = stats.columns[c];

                col_stats.uncompressed_bytes += cmd.total_uncompressed_size;
                col_stats.compressed_bytes   += cmd.total_compressed_size;
                col_stats.num_values         += cmd.num_values;
                col_stats.compression         = cmd.codec;

                // Use the first encoding in the list as the primary encoding
                if (!cmd.encodings.empty()) {
                    col_stats.encoding = cmd.encodings[0];
                }

                // Accumulate null count from statistics if available
                if (cmd.statistics.has_value() && cmd.statistics->null_count.has_value()) {
                    col_stats.null_count += *cmd.statistics->null_count;
                }

                stats.total_uncompressed_bytes += cmd.total_uncompressed_size;
                stats.total_compressed_bytes   += cmd.total_compressed_size;
            }
        }

        // Compute derived ratios
        if (stats.total_compressed_bytes > 0) {
            stats.compression_ratio = static_cast<double>(stats.total_uncompressed_bytes)
                                    / static_cast<double>(stats.total_compressed_bytes);
        }
        if (stats.total_rows > 0) {
            stats.bytes_per_row = static_cast<double>(stats.file_size_bytes)
                                / static_cast<double>(stats.total_rows);
        }

        return stats;
    }

    // -- Raw I/O helpers ------------------------------------------------------

    void write_raw(const uint8_t* data, size_t len) {
        file_.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(len));
        file_offset_ += static_cast<int64_t>(len);
    }

    void write_raw_le32(uint32_t val) {
        uint8_t bytes[4];
        bytes[0] = static_cast<uint8_t>((val      ) & 0xFF);
        bytes[1] = static_cast<uint8_t>((val >>  8) & 0xFF);
        bytes[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
        bytes[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
        write_raw(bytes, 4);
    }

    // -- Encode pending rows into column writers ------------------------------

    [[nodiscard]] expected<void> encode_pending_rows() {
        for (size_t c = 0; c < schema_.num_columns(); ++c) {
            const auto& col_desc = schema_.column(c);
            auto& cw = col_writers_[c];

            // Lazily initialize bloom filter for this column on first data
            bool bf_active = bloom_ensure_filter(c);

            for (const auto& row : pending_rows_) {
                const std::string& val = row[c];

                switch (col_desc.physical_type) {
                case PhysicalType::BOOLEAN: {
                    bool b = (val == "true" || val == "TRUE" || val == "True" ||
                              val == "1");
                    cw.write_bool(b);
                    // Booleans: insert as int32 (0 or 1) to match Parquet convention
                    if (bf_active) bloom_filters_[c]->insert_value(static_cast<int32_t>(b));
                    break;
                }
                case PhysicalType::INT32: {
                    int32_t parsed = 0;
                    auto [ptr, ec] = std::from_chars(val.data(),
                                                      val.data() + val.size(),
                                                      parsed);
                    if (ec != std::errc{}) {
                        // Fallback: try parsing as double and truncating
                        char* end = nullptr;
                        double d = std::strtod(val.c_str(), &end);
                        parsed = static_cast<int32_t>(d);
                    }
                    cw.write_int32(parsed);
                    if (bf_active) bloom_filters_[c]->insert_value(parsed);
                    break;
                }
                case PhysicalType::INT64: {
                    int64_t parsed = 0;
                    auto [ptr, ec] = std::from_chars(val.data(),
                                                      val.data() + val.size(),
                                                      parsed);
                    if (ec != std::errc{}) {
                        // Fallback: try parsing as double and truncating
                        char* end = nullptr;
                        double d = std::strtod(val.c_str(), &end);
                        parsed = static_cast<int64_t>(d);
                    }
                    cw.write_int64(parsed);
                    if (bf_active) bloom_filters_[c]->insert_value(parsed);
                    break;
                }
                case PhysicalType::FLOAT: {
                    char* end = nullptr;
                    float f = std::strtof(val.c_str(), &end);
                    cw.write_float(f);
                    if (bf_active) bloom_filters_[c]->insert_value(f);
                    break;
                }
                case PhysicalType::DOUBLE: {
                    char* end = nullptr;
                    double d = std::strtod(val.c_str(), &end);
                    cw.write_double(d);
                    if (bf_active) bloom_filters_[c]->insert_value(d);
                    break;
                }
                case PhysicalType::BYTE_ARRAY: {
                    cw.write_byte_array(val);
                    if (bf_active) bloom_filters_[c]->insert_value(val);
                    break;
                }
                case PhysicalType::FIXED_LEN_BYTE_ARRAY: {
                    cw.write_fixed_len_byte_array(
                        reinterpret_cast<const uint8_t*>(val.data()), val.size());
                    if (bf_active) bloom_filters_[c]->insert_value(val);
                    break;
                }
                default: {
                    // INT96 — write as raw bytes
                    cw.write_byte_array(val);
                    if (bf_active) bloom_filters_[c]->insert_value(val);
                    break;
                }
                }
            }
        }

        pending_rows_.clear();
        return expected<void>{};
    }

    // -- Snappy auto-registration ---------------------------------------------

    static void ensure_snappy_registered() {
        static bool registered = false;
        if (!registered) {
            register_snappy_codec();
            registered = true;
        }
    }

    // -- Encoding selection ---------------------------------------------------

    /// Choose the encoding for a column based on options and data characteristics.
    [[nodiscard]] Encoding choose_encoding(
            size_t col_index,
            const ColumnDescriptor& col_desc,
            const ColumnWriter& cw) const {

        // 1. Per-column override takes priority
        auto it = options_.column_encodings.find(col_desc.name);
        if (it != options_.column_encodings.end()) {
            return it->second;
        }

        // 2. Auto-encoding: pick optimal encoding based on type and data
        if (options_.auto_encoding) {
            switch (col_desc.physical_type) {
            case PhysicalType::INT32:
            case PhysicalType::INT64:
                return Encoding::DELTA_BINARY_PACKED;

            case PhysicalType::FLOAT:
            case PhysicalType::DOUBLE:
                return Encoding::BYTE_STREAM_SPLIT;

            case PhysicalType::BYTE_ARRAY: {
                // Check distinct ratio via statistics
                const auto& stats = cw.statistics();
                if (stats.distinct_count().has_value() && cw.num_values() > 0) {
                    double ratio = static_cast<double>(*stats.distinct_count()) /
                                   static_cast<double>(cw.num_values());
                    if (ratio < 0.40) {
                        return Encoding::RLE_DICTIONARY;
                    }
                }
                // Fallback: estimate from data size vs value count
                // PLAIN BYTE_ARRAY has 4-byte length prefix per value, so
                // if many values share the same content, dict encoding wins.
                // Without stats, stay with PLAIN as a safe default.
                return Encoding::PLAIN;
            }

            case PhysicalType::BOOLEAN:
                return Encoding::RLE;

            default:
                return Encoding::PLAIN;
            }
        }

        // 3. Use the global default encoding
        (void)col_index;
        return options_.default_encoding;
    }

    // -- Column data encoding (non-dictionary) --------------------------------

    /// Encode a column's data using the specified encoding.
    /// For PLAIN, returns the ColumnWriter's raw buffer directly.
    /// For other encodings, re-encodes from the PLAIN buffer.
    [[nodiscard]] static std::vector<uint8_t> encode_column_data(
            const ColumnWriter& cw,
            Encoding encoding,
            PhysicalType type) {

        const auto& plain_data = cw.data();
        int64_t num_vals = cw.num_values();

        switch (encoding) {

        case Encoding::DELTA_BINARY_PACKED: {
            if (type == PhysicalType::INT32) {
                // Extract int32 values from PLAIN buffer (4 bytes LE each)
                size_t count = static_cast<size_t>(num_vals);
                std::vector<int32_t> values(count);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(&values[i], plain_data.data() + i * 4, 4);
                }
                return delta::encode_int32(values.data(), count);
            }
            if (type == PhysicalType::INT64) {
                size_t count = static_cast<size_t>(num_vals);
                std::vector<int64_t> values(count);
                for (size_t i = 0; i < count; ++i) {
                    std::memcpy(&values[i], plain_data.data() + i * 8, 8);
                }
                return delta::encode_int64(values.data(), count);
            }
            // Fallback: unsupported type for DELTA_BINARY_PACKED → return PLAIN
            return std::vector<uint8_t>(plain_data.begin(), plain_data.end());
        }

        case Encoding::BYTE_STREAM_SPLIT: {
            if (type == PhysicalType::FLOAT) {
                size_t count = static_cast<size_t>(num_vals);
                const auto* float_ptr = reinterpret_cast<const float*>(plain_data.data());
                return byte_stream_split::encode_float(float_ptr, count);
            }
            if (type == PhysicalType::DOUBLE) {
                size_t count = static_cast<size_t>(num_vals);
                const auto* double_ptr = reinterpret_cast<const double*>(plain_data.data());
                return byte_stream_split::encode_double(double_ptr, count);
            }
            // Fallback: unsupported type → return PLAIN
            return std::vector<uint8_t>(plain_data.begin(), plain_data.end());
        }

        case Encoding::RLE: {
            // For BOOLEAN columns: RLE-encode the bit-packed booleans
            if (type == PhysicalType::BOOLEAN) {
                size_t count = static_cast<size_t>(num_vals);
                // Extract boolean values from the PLAIN bit-packed buffer
                std::vector<uint32_t> bool_vals(count);
                for (size_t i = 0; i < count; ++i) {
                    size_t byte_idx = i / 8;
                    size_t bit_idx  = i % 8;
                    bool_vals[i] = (plain_data[byte_idx] >> bit_idx) & 1;
                }
                // RLE-encode with bit_width=1, with 4-byte length prefix
                return RleEncoder::encode_with_length(bool_vals.data(), count, 1);
            }
            // Fallback: return PLAIN
            return std::vector<uint8_t>(plain_data.begin(), plain_data.end());
        }

        case Encoding::PLAIN:
        default:
            // Return a copy of the PLAIN data
            return std::vector<uint8_t>(plain_data.begin(), plain_data.end());
        }
    }

    // -- Dictionary encoding helper -------------------------------------------

    /// Result of writing a dictionary-encoded column (dict page + data page).
    struct DictColumnResult {
        int64_t total_uncompressed;
        int64_t total_compressed;
        std::unordered_set<Encoding> used_encodings;
        int64_t dict_page_offset;
        int64_t data_page_offset;
    };

    /// Extract BYTE_ARRAY strings from the PLAIN-encoded ColumnWriter buffer.
    [[nodiscard]] static std::vector<std::string> extract_byte_array_strings(
            const ColumnWriter& cw) {
        const auto& buf = cw.data();
        size_t count = static_cast<size_t>(cw.num_values());
        std::vector<std::string> result;
        result.reserve(count);
        size_t pos = 0;
        for (size_t i = 0; i < count; ++i) {
            uint32_t len = 0;
            std::memcpy(&len, buf.data() + pos, 4);
            pos += 4;
            result.emplace_back(
                reinterpret_cast<const char*>(buf.data() + pos), len);
            pos += len;
        }
        return result;
    }

    /// Extract INT32 values from PLAIN-encoded buffer.
    [[nodiscard]] static std::vector<int32_t> extract_int32_values(
            const ColumnWriter& cw) {
        size_t count = static_cast<size_t>(cw.num_values());
        std::vector<int32_t> result(count);
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(&result[i], cw.data().data() + i * 4, 4);
        }
        return result;
    }

    /// Extract INT64 values from PLAIN-encoded buffer.
    [[nodiscard]] static std::vector<int64_t> extract_int64_values(
            const ColumnWriter& cw) {
        size_t count = static_cast<size_t>(cw.num_values());
        std::vector<int64_t> result(count);
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(&result[i], cw.data().data() + i * 8, 8);
        }
        return result;
    }

    /// Extract FLOAT values from PLAIN-encoded buffer.
    [[nodiscard]] static std::vector<float> extract_float_values(
            const ColumnWriter& cw) {
        size_t count = static_cast<size_t>(cw.num_values());
        std::vector<float> result(count);
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(&result[i], cw.data().data() + i * 4, 4);
        }
        return result;
    }

    /// Extract DOUBLE values from PLAIN-encoded buffer.
    [[nodiscard]] static std::vector<double> extract_double_values(
            const ColumnWriter& cw) {
        size_t count = static_cast<size_t>(cw.num_values());
        std::vector<double> result(count);
        for (size_t i = 0; i < count; ++i) {
            std::memcpy(&result[i], cw.data().data() + i * 8, 8);
        }
        return result;
    }

    /// Write a dictionary-encoded column: emits the dictionary page then data page.
    /// Handles compression of each page independently.
    [[nodiscard]] expected<DictColumnResult> write_dictionary_column(
            size_t /*col_index*/,
            const ColumnDescriptor& col_desc,
            const ColumnWriter& cw,
            Compression col_codec) {

        DictColumnResult info;
        info.total_uncompressed = 0;
        info.total_compressed   = 0;
        info.used_encodings.insert(Encoding::PLAIN);          // dictionary page
        info.used_encodings.insert(Encoding::RLE_DICTIONARY); // data page

        // Build the dictionary based on physical type
        std::vector<uint8_t> dict_page_data;
        std::vector<uint8_t> indices_page_data;
        int32_t num_dict_entries = 0;

        switch (col_desc.physical_type) {
        case PhysicalType::BYTE_ARRAY: {
            auto strings = extract_byte_array_strings(cw);
            DictionaryEncoder<std::string> enc;
            for (const auto& s : strings) enc.put(s);
            enc.flush();
            dict_page_data = enc.dictionary_page();
            indices_page_data = enc.indices_page();
            num_dict_entries = static_cast<int32_t>(enc.dictionary_size());
            break;
        }
        case PhysicalType::INT32: {
            auto vals = extract_int32_values(cw);
            DictionaryEncoder<int32_t> enc;
            for (auto v : vals) enc.put(v);
            enc.flush();
            dict_page_data = enc.dictionary_page();
            indices_page_data = enc.indices_page();
            num_dict_entries = static_cast<int32_t>(enc.dictionary_size());
            break;
        }
        case PhysicalType::INT64: {
            auto vals = extract_int64_values(cw);
            DictionaryEncoder<int64_t> enc;
            for (auto v : vals) enc.put(v);
            enc.flush();
            dict_page_data = enc.dictionary_page();
            indices_page_data = enc.indices_page();
            num_dict_entries = static_cast<int32_t>(enc.dictionary_size());
            break;
        }
        case PhysicalType::FLOAT: {
            auto vals = extract_float_values(cw);
            DictionaryEncoder<float> enc;
            for (auto v : vals) enc.put(v);
            enc.flush();
            dict_page_data = enc.dictionary_page();
            indices_page_data = enc.indices_page();
            num_dict_entries = static_cast<int32_t>(enc.dictionary_size());
            break;
        }
        case PhysicalType::DOUBLE: {
            auto vals = extract_double_values(cw);
            DictionaryEncoder<double> enc;
            for (auto v : vals) enc.put(v);
            enc.flush();
            dict_page_data = enc.dictionary_page();
            indices_page_data = enc.indices_page();
            num_dict_entries = static_cast<int32_t>(enc.dictionary_size());
            break;
        }
        default:
            // Unsupported type for dictionary encoding — fall back to PLAIN
            // This shouldn't normally happen.
            return Error{ErrorCode::UNSUPPORTED_ENCODING,
                         "Dictionary encoding not supported for this type"};
        }

        // ---- Write dictionary page -----------------------------------------
        int32_t dict_uncompressed_size = static_cast<int32_t>(dict_page_data.size());
        const uint8_t* dict_write_data = dict_page_data.data();
        size_t dict_write_size = dict_page_data.size();
        std::vector<uint8_t> dict_compressed_buf;
        int32_t dict_compressed_size = dict_uncompressed_size;

        if (col_codec != Compression::UNCOMPRESSED) {
            auto comp = compress(col_codec, dict_page_data.data(), dict_page_data.size());
            if (!comp) return comp.error();
            dict_compressed_buf = std::move(*comp);
            dict_compressed_size = static_cast<int32_t>(dict_compressed_buf.size());
            dict_write_data = dict_compressed_buf.data();
            dict_write_size = dict_compressed_buf.size();
        }

        // Encrypt dictionary page if PME is configured for this column
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        std::vector<uint8_t> dict_encrypted_buf;
        if (encryptor_ && encryptor_->is_column_encrypted(col_desc.name)) {
            auto enc_result = encryptor_->encrypt_column_page(
                dict_write_data, dict_write_size, col_desc.name,
                static_cast<int32_t>(row_groups_.size()), -1 /*dict page*/);
            if (!enc_result) return enc_result.error();
            dict_encrypted_buf = std::move(*enc_result);
            dict_compressed_size = static_cast<int32_t>(dict_encrypted_buf.size());
            dict_write_data = dict_encrypted_buf.data();
            dict_write_size = dict_encrypted_buf.size();
        }
#endif

        thrift::PageHeader dict_ph;
        dict_ph.type = PageType::DICTIONARY_PAGE;
        dict_ph.uncompressed_page_size = dict_uncompressed_size;
        dict_ph.compressed_page_size   = dict_compressed_size;

        thrift::DictionaryPageHeader dph;
        dph.num_values = num_dict_entries;
        dph.encoding   = Encoding::PLAIN_DICTIONARY;
        dict_ph.dictionary_page_header = dph;

        thrift::CompactEncoder dict_header_enc;
        dict_ph.serialize(dict_header_enc);
        const auto& dict_header_bytes = dict_header_enc.data();

        info.dict_page_offset = file_offset_;

        write_raw(dict_header_bytes.data(), dict_header_bytes.size());
        write_raw(dict_write_data, dict_write_size);

        info.total_uncompressed += static_cast<int64_t>(dict_header_bytes.size()) +
                                   static_cast<int64_t>(dict_uncompressed_size);
        info.total_compressed   += static_cast<int64_t>(dict_header_bytes.size()) +
                                   static_cast<int64_t>(dict_compressed_size);

        // ---- Write data page (RLE_DICTIONARY indices) ----------------------
        int32_t idx_uncompressed_size = static_cast<int32_t>(indices_page_data.size());
        const uint8_t* idx_write_data = indices_page_data.data();
        size_t idx_write_size = indices_page_data.size();
        std::vector<uint8_t> idx_compressed_buf;
        int32_t idx_compressed_size = idx_uncompressed_size;

        if (col_codec != Compression::UNCOMPRESSED) {
            auto comp = compress(col_codec, indices_page_data.data(), indices_page_data.size());
            if (!comp) return comp.error();
            idx_compressed_buf = std::move(*comp);
            idx_compressed_size = static_cast<int32_t>(idx_compressed_buf.size());
            idx_write_data = idx_compressed_buf.data();
            idx_write_size = idx_compressed_buf.size();
        }

        // Encrypt indices page if PME is configured for this column
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        std::vector<uint8_t> idx_encrypted_buf;
        if (encryptor_ && encryptor_->is_column_encrypted(col_desc.name)) {
            auto enc_result = encryptor_->encrypt_column_page(
                idx_write_data, idx_write_size, col_desc.name,
                static_cast<int32_t>(row_groups_.size()), 0);
            if (!enc_result) return enc_result.error();
            idx_encrypted_buf = std::move(*enc_result);
            idx_compressed_size = static_cast<int32_t>(idx_encrypted_buf.size());
            idx_write_data = idx_encrypted_buf.data();
            idx_write_size = idx_encrypted_buf.size();
        }
#endif

        thrift::PageHeader idx_ph;
        idx_ph.type = PageType::DATA_PAGE;
        idx_ph.uncompressed_page_size = idx_uncompressed_size;
        idx_ph.compressed_page_size   = idx_compressed_size;

        thrift::DataPageHeader idx_dph;
        idx_dph.num_values                = static_cast<int32_t>(cw.num_values());
        idx_dph.encoding                  = Encoding::RLE_DICTIONARY;
        idx_dph.definition_level_encoding = Encoding::RLE;
        idx_dph.repetition_level_encoding = Encoding::RLE;
        idx_ph.data_page_header = idx_dph;

        thrift::CompactEncoder idx_header_enc;
        idx_ph.serialize(idx_header_enc);
        const auto& idx_header_bytes = idx_header_enc.data();

        info.data_page_offset = file_offset_;

        write_raw(idx_header_bytes.data(), idx_header_bytes.size());
        write_raw(idx_write_data, idx_write_size);

        info.total_uncompressed += static_cast<int64_t>(idx_header_bytes.size()) +
                                   static_cast<int64_t>(idx_uncompressed_size);
        info.total_compressed   += static_cast<int64_t>(idx_header_bytes.size()) +
                                   static_cast<int64_t>(idx_compressed_size);

        return info;
    }

    // -- Bloom filter helpers -------------------------------------------------

    /// Check if bloom filters are enabled and should be active for a given column.
    /// Lazily initializes the bloom filter on first call with data.
    /// Returns true if a bloom filter is active for this column.
    bool bloom_ensure_filter(size_t col_index) {
        if (bloom_filters_.empty()) return false;
        if (bloom_filters_[col_index]) return true;  // Already initialized

        // Check if this column should have a bloom filter
        const auto& col_name = schema_.column(col_index).name;
        if (!options_.bloom_filter_columns.empty() &&
            options_.bloom_filter_columns.find(col_name) ==
                options_.bloom_filter_columns.end()) {
            return false;
        }

        // Initialize with row_group_size as the expected NDV estimate
        size_t ndv_estimate = static_cast<size_t>(
            std::max(int64_t{1}, options_.row_group_size));
        bloom_filters_[col_index] = std::make_unique<SplitBlockBloomFilter>(
            ndv_estimate, options_.bloom_filter_fpr);
        return true;
    }

    /// Insert a batch of typed values into the bloom filter for a column.
    template <typename T>
    void bloom_insert_typed(size_t col_index, const T* values, size_t count) {
        if (!bloom_ensure_filter(col_index)) return;
        auto& bf = bloom_filters_[col_index];
        for (size_t i = 0; i < count; ++i) {
            bf->insert_value(values[i]);
        }
    }

    // -- CSV parsing helper ---------------------------------------------------

    /// Split a CSV line into fields. Handles double-quoted fields with
    /// embedded commas and escaped quotes ("").
    [[nodiscard]] static std::vector<std::string> split_csv_line(const std::string& line) {
        std::vector<std::string> fields;
        std::string field;
        bool in_quotes = false;
        size_t i = 0;

        while (i < line.size()) {
            char ch = line[i];

            if (in_quotes) {
                if (ch == '"') {
                    // Check for escaped quote ("")
                    if (i + 1 < line.size() && line[i + 1] == '"') {
                        field += '"';
                        i += 2;
                    } else {
                        // End of quoted field
                        in_quotes = false;
                        ++i;
                    }
                } else {
                    field += ch;
                    ++i;
                }
            } else {
                if (ch == '"') {
                    in_quotes = true;
                    ++i;
                } else if (ch == ',') {
                    // Trim whitespace from unquoted fields
                    trim_string(field);
                    fields.push_back(std::move(field));
                    field.clear();
                    ++i;
                } else if (ch == '\r') {
                    // Skip carriage return
                    ++i;
                } else {
                    field += ch;
                    ++i;
                }
            }
        }

        // Add the last field
        trim_string(field);
        fields.push_back(std::move(field));

        return fields;
    }

    /// Trim leading and trailing whitespace from a string in-place.
    static void trim_string(std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) {
            s.clear();
            return;
        }
        size_t end = s.find_last_not_of(" \t\r\n");
        s = s.substr(start, end - start + 1);
    }
};

} // namespace signet::forge
