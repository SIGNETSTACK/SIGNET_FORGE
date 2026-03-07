// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

// ---------------------------------------------------------------------------
// ParquetReader — Parquet file reader
//
// Opens a Parquet file, verifies PAR1 magic bytes, deserializes the Thrift
// footer to extract FileMetaData, builds a Schema, and provides typed access
// to column data via ColumnReader.
//
// Supports:
//   - Typed column reads (read_column<T>)
//   - String conversion reads (read_column_as_strings)
//   - Full row-group and file reads
//   - Column projection by name
//   - Per-column statistics access
//   - Multiple encodings: PLAIN, RLE_DICTIONARY, DELTA_BINARY_PACKED,
//     BYTE_STREAM_SPLIT, RLE (booleans)
//   - Decompression via CodecRegistry (Snappy, ZSTD, LZ4, etc.)
// ---------------------------------------------------------------------------

#include "signet/types.hpp"
#include "signet/error.hpp"
#include "signet/schema.hpp"
#include "signet/column_reader.hpp"
#include "signet/memory.hpp"
#include "signet/thrift/compact.hpp"
#include "signet/thrift/types.hpp"
#include "signet/encoding/rle.hpp"
#include "signet/encoding/dictionary.hpp"
#include "signet/encoding/delta.hpp"
#include "signet/encoding/byte_stream_split.hpp"
#include "signet/compression/codec.hpp"
#include "signet/compression/snappy.hpp"
#include "signet/compression/zstd.hpp"
#include "signet/compression/lz4.hpp"
#include "signet/compression/gzip.hpp"
#include "signet/bloom/split_block.hpp"
#include "signet/column_index.hpp"

#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
#include "signet/crypto/pme.hpp"
#endif

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace signet::forge {

/// Maximum allowed uncompressed page size in bytes (256 MB).
///
/// Any page whose declared uncompressed size exceeds this limit is rejected
/// with `ErrorCode::CORRUPT_PAGE`. This guards against malicious or corrupt
/// files that would otherwise trigger unbounded memory allocation.
static constexpr size_t PARQUET_MAX_PAGE_SIZE = 256ULL * 1024ULL * 1024ULL;  // 256 MB hard cap per page

/// Maximum number of values allowed in a single data page (100 million).
///
/// Pages declaring more values than this are rejected with
/// `ErrorCode::CORRUPT_PAGE` to prevent out-of-memory conditions during
/// decoding.
static constexpr int64_t MAX_VALUES_PER_PAGE = 100'000'000;  // 100M values per page — OOM guard

/// Parquet file reader with typed column access and full encoding support.
///
/// Opens a Parquet file, verifies PAR1 magic bytes, deserializes the Thrift
/// footer to extract FileMetaData, builds a Schema, and provides typed access
/// to column data via ColumnReader.
///
/// Supported encodings: PLAIN, RLE_DICTIONARY, DELTA_BINARY_PACKED,
/// BYTE_STREAM_SPLIT, and RLE (booleans). Decompression is handled
/// transparently via the CodecRegistry (Snappy, ZSTD, LZ4, Gzip).
///
/// When built with `SIGNET_ENABLE_COMMERCIAL`, encrypted Parquet files
/// (PME with PARE footer magic) are supported via a FileDecryptor.
///
/// @note Move-only. Default-constructed readers are not valid; use the
///       static `open()` factory method.
/// @see ParquetWriter for the corresponding writer.
/// @see Schema, ColumnReader, FileStats
class ParquetReader {
public:
    // ===================================================================
    // Open a Parquet file
    // ===================================================================

    /// Open and parse a Parquet file, returning a ready-to-query reader.
    ///
    /// Reads the entire file into memory, validates PAR1/PARE magic bytes,
    /// deserializes the Thrift footer into FileMetaData, and constructs the
    /// column Schema. Common compression codecs (Snappy, and optionally
    /// ZSTD/LZ4/Gzip) are registered automatically on first call.
    ///
    /// @param path  Filesystem path to the `.parquet` file.
    /// @note Commercial builds accept an optional `encryption` parameter for
    ///       decrypting PME-encrypted files (PARE footer magic).
    /// @return The constructed `ParquetReader` on success, or an `Error` with
    ///         codes such as `IO_ERROR`, `INVALID_FILE`, `CORRUPT_FOOTER`,
    ///         `ENCRYPTION_ERROR`, or `LICENSE_ERROR`.
    /// @note The entire file is loaded into memory. For very large files,
    ///       consider using the memory-mapped reader path instead.
    /// @see close()
    static expected<ParquetReader> open(
        const std::filesystem::path& path
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        , const std::optional<crypto::EncryptionConfig>& encryption = std::nullopt
#endif
        ) {

        // Ensure built-in codecs are available for page decompression.
        ensure_default_codecs_registered();

        // --- Read entire file into memory ---
        std::error_code ec;
        auto file_size = std::filesystem::file_size(path, ec);
        if (ec) {
            return Error{ErrorCode::IO_ERROR,
                         "cannot determine file size: " + path.string() +
                         " (" + ec.message() + ")"};
        }

        if (file_size < 12) {
            // Minimum: 4 (magic) + 4 (footer len) + 4 (magic) = 12
            return Error{ErrorCode::INVALID_FILE,
                         "file too small to be a valid Parquet file: " +
                         path.string()};
        }

        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            return Error{ErrorCode::IO_ERROR,
                         "cannot open file: " + path.string()};
        }

        std::vector<uint8_t> file_data(static_cast<size_t>(file_size));
        ifs.read(reinterpret_cast<char*>(file_data.data()),
                 static_cast<std::streamsize>(file_size));
        if (!ifs) {
            return Error{ErrorCode::IO_ERROR,
                         "failed to read file: " + path.string()};
        }
        ifs.close();

        // --- Verify PAR1 magic at start ---
        const size_t sz = file_data.size();

        uint32_t magic_start;
        std::memcpy(&magic_start, file_data.data(), 4);
        if (magic_start != PARQUET_MAGIC) {
            return Error{ErrorCode::INVALID_FILE,
                         "missing PAR1 magic at start of file"};
        }

        // --- Check trailing magic: PAR1 (plaintext) or PARE (encrypted footer) ---
        uint32_t magic_end;
        std::memcpy(&magic_end, file_data.data() + sz - 4, 4);

        bool encrypted_footer = (magic_end == PARQUET_MAGIC_ENCRYPTED);

        if (magic_end != PARQUET_MAGIC && magic_end != PARQUET_MAGIC_ENCRYPTED) {
            return Error{ErrorCode::INVALID_FILE,
                         "missing PAR1/PARE magic at end of file"};
        }

        // --- Read footer length (4-byte LE uint32 at [size-8, size-4]) ---
        uint32_t footer_len;
        std::memcpy(&footer_len, file_data.data() + sz - 8, 4);

        if (footer_len == 0 || static_cast<size_t>(footer_len) > sz - 12) {
            return Error{ErrorCode::CORRUPT_FOOTER,
                         "invalid footer length: " + std::to_string(footer_len)};
        }

        // --- Prepare decryptor if needed ---
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        std::unique_ptr<crypto::FileDecryptor> decryptor;
        if (encrypted_footer || encryption) {
            if (!encryption) {
                return Error{ErrorCode::ENCRYPTION_ERROR,
                             "file has encrypted footer (PARE magic) but no "
                             "encryption config was provided"};
            }
            decryptor = std::make_unique<crypto::FileDecryptor>(*encryption);
        }
#else
        if (encrypted_footer) {
            return Error{ErrorCode::LICENSE_ERROR,
                         "encrypted footer (PARE) requires commercial build and license"};
        }
#endif

        // --- Deserialize FileMetaData from footer ---
        size_t footer_offset = sz - 8 - footer_len;
        const uint8_t* footer_ptr = file_data.data() + footer_offset;
        size_t footer_size = footer_len;

        // If footer is encrypted, decrypt it first
        std::vector<uint8_t> decrypted_footer_buf;
        if (encrypted_footer) {
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
            auto dec_result = decryptor->decrypt_footer(footer_ptr, footer_size);
            if (!dec_result) return dec_result.error();
            decrypted_footer_buf = std::move(*dec_result);
            footer_ptr  = decrypted_footer_buf.data();
            footer_size = decrypted_footer_buf.size();
#else
            return Error{ErrorCode::LICENSE_ERROR,
                         "encrypted footer (PARE) requires commercial build and license"};
#endif
        }

        thrift::CompactDecoder dec(footer_ptr, footer_size);

        thrift::FileMetaData metadata;
        metadata.deserialize(dec);

        if (!dec.good()) {
            return Error{ErrorCode::CORRUPT_FOOTER,
                         "thrift deserialization of FileMetaData failed"};
        }

        // --- Build Schema from FileMetaData.schema ---
        // The first element is the root (group) node — skip it.
        // Subsequent leaf elements become columns.
        std::string schema_name;
        std::vector<ColumnDescriptor> columns;

        if (!metadata.schema.empty()) {
            schema_name = metadata.schema[0].name;

            for (size_t i = 1; i < metadata.schema.size(); ++i) {
                const auto& elem = metadata.schema[i];

                // Skip group nodes (those with num_children set)
                if (elem.num_children.has_value()) {
                    continue;
                }

                ColumnDescriptor cd;
                cd.name = elem.name;
                cd.physical_type = elem.type.value_or(PhysicalType::BYTE_ARRAY);
                cd.repetition = elem.repetition_type.value_or(Repetition::REQUIRED);

                if (elem.type_length.has_value()) {
                    cd.type_length = *elem.type_length;
                }
                if (elem.precision.has_value()) {
                    cd.precision = *elem.precision;
                }
                if (elem.scale.has_value()) {
                    cd.scale = *elem.scale;
                }

                // Map ConvertedType to LogicalType for common cases
                if (elem.converted_type.has_value()) {
                    cd.logical_type = converted_type_to_logical(*elem.converted_type);
                }

                columns.push_back(std::move(cd));
            }
        }

        // --- Assemble the reader ---
        ParquetReader reader;
        reader.file_data_  = std::move(file_data);
        reader.metadata_   = std::move(metadata);
        reader.schema_     = Schema(std::move(schema_name), std::move(columns));
        reader.created_by_ = reader.metadata_.created_by.value_or("");
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        reader.decryptor_  = std::move(decryptor);
#endif

        return reader;
    }

    // ===================================================================
    // File metadata accessors
    // ===================================================================

    /// Return the file's column schema.
    /// @return Const reference to the Schema parsed from the Thrift footer.
    [[nodiscard]] const Schema& schema() const { return schema_; }

    /// Return the total number of rows across all row groups.
    [[nodiscard]] int64_t num_rows() const { return metadata_.num_rows; }

    /// Return the number of row groups in the file.
    [[nodiscard]] int64_t num_row_groups() const {
        return static_cast<int64_t>(metadata_.row_groups.size());
    }

    /// Return the `created_by` string from the file footer metadata.
    ///
    /// This typically identifies the library and version that wrote the file
    /// (e.g. `"signet-forge 0.1.0"`). Returns an empty string if the field
    /// was not set by the writer.
    [[nodiscard]] const std::string& created_by() const { return created_by_; }

    /// Return the file-level key-value metadata pairs.
    ///
    /// These are arbitrary string key-value pairs stored in the Parquet footer
    /// (e.g. Arrow schema, pandas metadata). Returns an empty vector if the
    /// writer did not set any key-value metadata.
    [[nodiscard]] const std::vector<thrift::KeyValue>& key_value_metadata() const {
        static const std::vector<thrift::KeyValue> empty;
        return metadata_.key_value_metadata.has_value()
                   ? *metadata_.key_value_metadata
                   : empty;
    }

    // ===================================================================
    // Row group info
    // ===================================================================

    /// Summary metadata for a single row group.
    struct RowGroupInfo {
        int64_t num_rows;         ///< Number of rows in this row group.
        int64_t total_byte_size;  ///< Total serialized size in bytes (compressed).
        int64_t row_group_index;  ///< Zero-based index of this row group in the file.
    };

    /// Return summary metadata for a specific row group.
    ///
    /// @param index  Zero-based row group index. Must be less than
    ///               `num_row_groups()`.
    /// @return A `RowGroupInfo` struct with row count, byte size, and index.
    /// @note No bounds checking is performed; passing an out-of-range index
    ///       is undefined behavior. Use `num_row_groups()` to validate first.
    RowGroupInfo row_group(size_t index) const {
        const auto& rg = metadata_.row_groups[index];
        return {rg.num_rows,
                rg.total_byte_size,
                static_cast<int64_t>(index)};
    }

    // ===================================================================
    // File statistics (aggregate metadata for the entire file)
    // ===================================================================

    /// Compute aggregate statistics for the entire file.
    ///
    /// Iterates over all row groups and columns to produce a `FileStats`
    /// summary including: file size, total rows, row group count, column
    /// count, per-column compressed/uncompressed sizes, null counts,
    /// compression ratio, bytes-per-row, and bloom filter/page index
    /// presence flags.
    ///
    /// @return A `FileStats` struct populated from the file's metadata.
    /// @see FileStats
    [[nodiscard]] FileStats file_stats() const {
        FileStats fs;
        fs.file_size_bytes = static_cast<int64_t>(file_data_.size());
        fs.total_rows      = metadata_.num_rows;
        fs.num_row_groups  = static_cast<int64_t>(metadata_.row_groups.size());
        fs.num_columns     = static_cast<int64_t>(schema_.num_columns());
        fs.created_by      = created_by_;

        // Aggregate per-column stats across all row groups
        fs.columns.resize(schema_.num_columns());
        for (size_t c = 0; c < schema_.num_columns(); ++c) {
            auto& col = fs.columns[c];
            col.column_name  = schema_.column(c).name;
            col.physical_type = schema_.column(c).physical_type;
            col.logical_type  = schema_.column(c).logical_type;
        }

        int64_t total_uncompressed = 0;
        int64_t total_compressed   = 0;

        for (const auto& rg : metadata_.row_groups) {
            for (size_t c = 0; c < rg.columns.size() && c < schema_.num_columns(); ++c) {
                const auto& cc = rg.columns[c];
                if (!cc.meta_data.has_value()) continue;
                const auto& cmd = *cc.meta_data;
                auto& col = fs.columns[c];

                col.uncompressed_bytes += cmd.total_uncompressed_size;
                col.compressed_bytes   += cmd.total_compressed_size;
                col.num_values         += cmd.num_values;
                col.compression         = cmd.codec;

                if (cmd.statistics.has_value() && cmd.statistics->null_count.has_value()) {
                    col.null_count += *cmd.statistics->null_count;
                }

                // Check bloom filter presence
                if (cc.bloom_filter_offset.has_value() && *cc.bloom_filter_offset >= 0) {
                    col.has_bloom_filter = true;
                }
                // Check page index presence
                if (cc.column_index_offset.has_value() && *cc.column_index_offset >= 0) {
                    col.has_page_index = true;
                }

                total_uncompressed += cmd.total_uncompressed_size;
                total_compressed   += cmd.total_compressed_size;
            }
        }

        if (total_compressed > 0) {
            fs.compression_ratio = static_cast<double>(total_uncompressed)
                                 / static_cast<double>(total_compressed);
        }
        if (fs.total_rows > 0) {
            fs.bytes_per_row = static_cast<double>(fs.file_size_bytes)
                             / static_cast<double>(fs.total_rows);
        }

        return fs;
    }

    /// Ensure common compression codecs are registered in the global CodecRegistry.
    ///
    /// Called automatically by `open()`. Registers Snappy (always), and
    /// optionally ZSTD, LZ4, and Gzip when their respective `SIGNET_HAS_*`
    /// macros are defined. Safe to call multiple times; only the first call
    /// performs registration.
    static void ensure_default_codecs_registered() {
        static bool registered = false;
        if (registered) return;
        register_snappy_codec();
#ifdef SIGNET_HAS_ZSTD
        register_zstd_codec();
#endif
#ifdef SIGNET_HAS_LZ4
        register_lz4_codec();
#endif
#ifdef SIGNET_HAS_GZIP
        register_gzip_codec();
#endif
        registered = true;
    }

    // ===================================================================
    // Read a single column from a row group as a typed vector
    // ===================================================================

    /// Read a single column from a row group as a typed vector.
    ///
    /// Automatically selects the appropriate decoding path based on the
    /// column's encoding metadata:
    /// - **RLE_DICTIONARY** -- dictionary decode (string, int32, int64, float, double)
    /// - **DELTA_BINARY_PACKED** -- delta decode (int32, int64)
    /// - **BYTE_STREAM_SPLIT** -- BSS decode (float, double)
    /// - **RLE** -- run-length decode (bool only)
    /// - **PLAIN** -- raw value decode via ColumnReader (all types)
    ///
    /// Decompression and (in commercial builds) decryption are applied
    /// transparently before decoding.
    ///
    /// @tparam T  The C++ value type. Must match the column's physical type:
    ///            `bool`, `int32_t`, `int64_t`, `float`, `double`, or
    ///            `std::string`.
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the schema.
    /// @return A vector of decoded values on success, or an `Error` with
    ///         codes such as `OUT_OF_RANGE`, `CORRUPT_PAGE`,
    ///         `UNSUPPORTED_ENCODING`, or `UNSUPPORTED_COMPRESSION`.
    /// @see read_column_as_strings(), read_row_group(), read_all()
    template <typename T>
    expected<std::vector<T>> read_column(size_t row_group_index,
                                          size_t column_index) {
        // --- Validate indices ---
        if (row_group_index >= metadata_.row_groups.size()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "row group index out of range"};
        }
        if (column_index >= schema_.num_columns()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "column index out of range"};
        }

        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "column index out of range"};
        }

        const auto& chunk = rg.columns[column_index];
        if (!chunk.meta_data.has_value()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "column chunk has no metadata"};
        }
        const auto& col_meta = *chunk.meta_data;

        // --- Detect encoding strategy from column metadata ---
        bool has_dict = false;
        Encoding data_encoding = Encoding::PLAIN;

        for (auto enc : col_meta.encodings) {
            if (enc == Encoding::PLAIN_DICTIONARY || enc == Encoding::RLE_DICTIONARY) {
                has_dict = true;
            }
            if (enc == Encoding::DELTA_BINARY_PACKED) {
                data_encoding = Encoding::DELTA_BINARY_PACKED;
            }
            if (enc == Encoding::BYTE_STREAM_SPLIT) {
                data_encoding = Encoding::BYTE_STREAM_SPLIT;
            }
            if (enc == Encoding::RLE && col_meta.type == PhysicalType::BOOLEAN) {
                data_encoding = Encoding::RLE;
            }
        }

        // Resolve column name for encryption context
        const std::string& col_name = col_meta.path_in_schema.empty()
            ? schema_.column(column_index).name
            : col_meta.path_in_schema[0];

        // --- Dictionary encoding path ---
        // Dictionary encoding supports: string, int32, int64, float, double
        if (has_dict) {
            if constexpr (std::is_same_v<T, std::string> ||
                          std::is_same_v<T, int32_t> ||
                          std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, float> ||
                          std::is_same_v<T, double>) {
                return read_column_dict<T>(col_meta, col_name,
                    static_cast<int32_t>(row_group_index));
            } else {
                return Error{ErrorCode::UNSUPPORTED_ENCODING,
                             "dictionary encoding not supported for this type"};
            }
        }

        // --- DELTA_BINARY_PACKED path (INT32/INT64 only) ---
        if (data_encoding == Encoding::DELTA_BINARY_PACKED) {
            if constexpr (std::is_same_v<T, int32_t> ||
                          std::is_same_v<T, int64_t>) {
                return read_column_delta<T>(col_meta, col_name,
                    static_cast<int32_t>(row_group_index));
            } else {
                return Error{ErrorCode::UNSUPPORTED_ENCODING,
                             "DELTA_BINARY_PACKED only supports INT32/INT64"};
            }
        }

        // --- BYTE_STREAM_SPLIT path (FLOAT/DOUBLE only) ---
        if (data_encoding == Encoding::BYTE_STREAM_SPLIT) {
            if constexpr (std::is_same_v<T, float> ||
                          std::is_same_v<T, double>) {
                return read_column_bss<T>(col_meta, col_name,
                    static_cast<int32_t>(row_group_index));
            } else {
                return Error{ErrorCode::UNSUPPORTED_ENCODING,
                             "BYTE_STREAM_SPLIT only supports FLOAT/DOUBLE"};
            }
        }

        // --- RLE path (boolean only) ---
        if (data_encoding == Encoding::RLE &&
            col_meta.type == PhysicalType::BOOLEAN) {
            if constexpr (std::is_same_v<T, bool>) {
                return read_column_rle_bool<T>(col_meta, col_name,
                    static_cast<int32_t>(row_group_index));
            } else {
                return Error{ErrorCode::UNSUPPORTED_ENCODING,
                             "RLE boolean encoding requires bool type"};
            }
        }

        // --- Default: PLAIN encoding via ColumnReader ---
        auto reader_result = make_column_reader(row_group_index, column_index);
        if (!reader_result) return reader_result.error();

        auto& [col_reader, num_values] = reader_result.value();
        size_t count = static_cast<size_t>(num_values);

        if constexpr (std::is_same_v<T, bool>) {
            // std::vector<bool> has no .data() — read one by one
            std::vector<bool> values;
            values.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                auto val = col_reader.template read<bool>();
                if (!val) return val.error();
                values.push_back(*val);
            }
            return values;
        } else {
            std::vector<T> values(count);
            auto batch_result = col_reader.template read_batch<T>(
                values.data(), count);
            if (!batch_result) return batch_result.error();
            return values;
        }
    }

    // ===================================================================
    // Read column as strings (converts any type to string representation)
    // ===================================================================

    /// Read a column and convert every value to its string representation.
    ///
    /// Dispatches to `read_column<T>()` based on the column's physical type,
    /// then converts each value using `std::to_string()` (numeric types),
    /// `"true"`/`"false"` (booleans), identity (BYTE_ARRAY/string), or
    /// hex-encoding (FIXED_LEN_BYTE_ARRAY).
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the schema.
    /// @return A vector of string-converted values on success, or an `Error`.
    /// @see read_column(), read_row_group()
    expected<std::vector<std::string>> read_column_as_strings(
            size_t row_group_index, size_t column_index) {
        if (row_group_index >= metadata_.row_groups.size()) {
            return Error{ErrorCode::OUT_OF_RANGE, "row group index out of range"};
        }
        if (column_index >= schema_.num_columns()) {
            return Error{ErrorCode::OUT_OF_RANGE, "column index out of range"};
        }

        PhysicalType pt = schema_.column(column_index).physical_type;

        switch (pt) {
        case PhysicalType::BOOLEAN: {
            auto res = read_column<bool>(row_group_index, column_index);
            if (!res) return res.error();
            return to_string_vec(res.value());
        }
        case PhysicalType::INT32: {
            auto res = read_column<int32_t>(row_group_index, column_index);
            if (!res) return res.error();
            return to_string_vec(res.value());
        }
        case PhysicalType::INT64: {
            auto res = read_column<int64_t>(row_group_index, column_index);
            if (!res) return res.error();
            return to_string_vec(res.value());
        }
        case PhysicalType::FLOAT: {
            auto res = read_column<float>(row_group_index, column_index);
            if (!res) return res.error();
            return to_string_vec(res.value());
        }
        case PhysicalType::DOUBLE: {
            auto res = read_column<double>(row_group_index, column_index);
            if (!res) return res.error();
            return to_string_vec(res.value());
        }
        case PhysicalType::BYTE_ARRAY: {
            // Strings are already strings — read directly
            return read_column<std::string>(row_group_index, column_index);
        }
        case PhysicalType::FIXED_LEN_BYTE_ARRAY: {
            // Read as raw bytes, hex-encode each value
            auto reader_result = make_column_reader(row_group_index, column_index);
            if (!reader_result) return reader_result.error();
            auto& [col_reader, num_values] = reader_result.value();

            std::vector<std::string> result;
            result.reserve(static_cast<size_t>(num_values));
            for (int64_t i = 0; i < num_values; ++i) {
                auto bytes_result = col_reader.read_bytes();
                if (!bytes_result) return bytes_result.error();
                result.push_back(hex_encode(bytes_result.value()));
            }
            return result;
        }
        default:
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "unsupported physical type for string conversion"};
        }
    }

    // ===================================================================
    // Read all columns from a row group as vectors of strings
    // ===================================================================

    /// Read all columns from a single row group as string vectors.
    ///
    /// Calls `read_column_as_strings()` for every column in the schema,
    /// producing one `vector<string>` per column. The outer vector is
    /// indexed by column ordinal.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @return A column-major vector of string vectors on success, or an
    ///         `Error` if any column read fails.
    /// @see read_column_as_strings(), read_all()
    expected<std::vector<std::vector<std::string>>> read_row_group(
            size_t row_group_index) {
        size_t num_cols = schema_.num_columns();
        std::vector<std::vector<std::string>> columns(num_cols);

        for (size_t c = 0; c < num_cols; ++c) {
            auto res = read_column_as_strings(row_group_index, c);
            if (!res) return res.error();
            columns[c] = std::move(res.value());
        }

        return columns;
    }

    // ===================================================================
    // Read entire file as vector of rows (each row = vector of strings)
    // ===================================================================

    /// Read the entire file as a row-major vector of string vectors.
    ///
    /// Iterates over all row groups via `read_row_group()` and transposes
    /// the column-major data into rows, where each inner vector has one
    /// string element per column.
    ///
    /// @return A row-major `vector<vector<string>>` on success, or an
    ///         `Error` if any row group or column read fails.
    /// @note For large files this allocates all data as strings in memory.
    ///       Prefer `read_column<T>()` or column projection for selective
    ///       access.
    /// @see read_row_group(), read_columns()
    expected<std::vector<std::vector<std::string>>> read_all() {
        size_t num_cols = schema_.num_columns();
        std::vector<std::vector<std::string>> rows;
        rows.reserve(static_cast<size_t>(metadata_.num_rows));

        for (size_t rg = 0; rg < metadata_.row_groups.size(); ++rg) {
            auto cols_result = read_row_group(rg);
            if (!cols_result) return cols_result.error();

            const auto& col_data = cols_result.value();
            if (col_data.empty()) continue;

            size_t rg_rows = col_data[0].size();
            for (size_t r = 0; r < rg_rows; ++r) {
                std::vector<std::string> row(num_cols);
                for (size_t c = 0; c < num_cols; ++c) {
                    if (r < col_data[c].size()) {
                        row[c] = col_data[c][r];
                    }
                }
                rows.push_back(std::move(row));
            }
        }

        return rows;
    }

    // ===================================================================
    // Column projection -- read only specific columns by name
    // ===================================================================

    /// Read a subset of columns (by name) across all row groups.
    ///
    /// Resolves each column name to its schema index via
    /// `Schema::find_column()`, then reads that column from every row group,
    /// concatenating the results into a single vector per projected column.
    /// The outer vector is ordered to match @p column_names.
    ///
    /// @param column_names  Column names to project. Each must exist in the
    ///                      schema or `SCHEMA_MISMATCH` is returned.
    /// @return A column-major `vector<vector<string>>` with one entry per
    ///         requested column, spanning all row groups.
    /// @see read_column_as_strings(), read_all()
    expected<std::vector<std::vector<std::string>>> read_columns(
            const std::vector<std::string>& column_names) {
        // Resolve column indices
        std::vector<size_t> indices;
        indices.reserve(column_names.size());

        for (const auto& name : column_names) {
            auto idx = schema_.find_column(name);
            if (!idx.has_value()) {
                return Error{ErrorCode::SCHEMA_MISMATCH,
                             "column not found: " + name};
            }
            indices.push_back(*idx);
        }

        // Read across all row groups
        size_t proj_cols = indices.size();
        std::vector<std::vector<std::string>> result(proj_cols);

        for (size_t rg = 0; rg < metadata_.row_groups.size(); ++rg) {
            for (size_t p = 0; p < proj_cols; ++p) {
                auto col_result = read_column_as_strings(rg, indices[p]);
                if (!col_result) return col_result.error();

                auto& col_vec = col_result.value();
                result[p].insert(result[p].end(),
                                 std::make_move_iterator(col_vec.begin()),
                                 std::make_move_iterator(col_vec.end()));
            }
        }

        return result;
    }

    // ===================================================================
    // Statistics for a column in a row group
    // ===================================================================

    /// Access Parquet column statistics for a specific column chunk.
    ///
    /// Returns a pointer to the `thrift::Statistics` stored in the column
    /// chunk's metadata (min, max, null_count, distinct_count, etc.).
    /// Returns `nullptr` if the row group index or column index is out of
    /// range, or if the column chunk has no statistics.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the row group.
    /// @return Pointer to statistics, or `nullptr` if unavailable.
    /// @note The returned pointer is valid for the lifetime of this reader.
    const thrift::Statistics* column_statistics(size_t row_group_index,
                                                 size_t column_index) const {
        if (row_group_index >= metadata_.row_groups.size()) return nullptr;
        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size()) return nullptr;
        const auto& chunk = rg.columns[column_index];
        if (!chunk.meta_data.has_value()) return nullptr;
        if (!chunk.meta_data->statistics.has_value()) return nullptr;
        return &(*chunk.meta_data->statistics);
    }

    // ===================================================================
    // Bloom filter access
    // ===================================================================

    /// Read the Split Block Bloom Filter for a column chunk, if present.
    ///
    /// Locates the bloom filter in the file using the column chunk's
    /// `bloom_filter_offset`, reads the 4-byte LE size header, validates
    /// alignment to `kBytesPerBlock`, and deserializes the filter data.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the row group.
    /// @return A `SplitBlockBloomFilter` on success, or an `Error` with
    ///         `INVALID_FILE` (no filter), `OUT_OF_RANGE`, or `CORRUPT_PAGE`.
    /// @see bloom_might_contain(), SplitBlockBloomFilter
    [[nodiscard]] expected<SplitBlockBloomFilter> read_bloom_filter(
            size_t row_group_index, size_t column_index) const {
        if (row_group_index >= metadata_.row_groups.size()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "row group index out of range"};
        }

        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "column index out of range"};
        }

        const auto& chunk = rg.columns[column_index];
        if (!chunk.bloom_filter_offset.has_value()) {
            return Error{ErrorCode::INVALID_FILE,
                         "no bloom filter for this column chunk"};
        }

        int64_t bf_offset = *chunk.bloom_filter_offset;
        if (bf_offset < 0 || static_cast<size_t>(bf_offset) + 4 > file_data_.size()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "bloom filter offset out of file bounds"};
        }

        // Read 4-byte LE size header
        uint32_t bf_size = 0;
        std::memcpy(&bf_size, file_data_.data() + bf_offset, 4);

        size_t data_start = static_cast<size_t>(bf_offset) + 4;
        if (data_start + bf_size > file_data_.size()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "bloom filter data extends past end of file"};
        }

        if (bf_size == 0 || (bf_size % SplitBlockBloomFilter::kBytesPerBlock) != 0) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "invalid bloom filter size: " + std::to_string(bf_size)};
        }

        return SplitBlockBloomFilter::from_data(
            file_data_.data() + data_start, bf_size);
    }

    /// Check whether a value might exist in a column using its bloom filter.
    ///
    /// If no bloom filter is present for the column chunk, returns `true`
    /// (conservative: the value cannot be ruled out). A return value of
    /// `false` guarantees the value is absent; `true` may be a false
    /// positive.
    ///
    /// @tparam T  The value type (must be hashable by xxHash64).
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the row group.
    /// @param value            The value to probe.
    /// @return `false` if the bloom filter definitively excludes the value;
    ///         `true` otherwise (including when no filter is available).
    /// @see read_bloom_filter()
    template <typename T>
    [[nodiscard]] bool bloom_might_contain(
            size_t row_group_index, size_t column_index,
            const T& value) const {
        auto bf_result = read_bloom_filter(row_group_index, column_index);
        if (!bf_result) {
            return true;  // No bloom filter — cannot rule out the value
        }
        return bf_result->might_contain_value(value);
    }

    // ===================================================================
    // Page Index access (ColumnIndex + OffsetIndex)
    // ===================================================================

    /// Read the ColumnIndex (min/max per page) for a column chunk.
    ///
    /// Deserializes the Thrift-encoded ColumnIndex structure from the file
    /// at the offset recorded in the column chunk metadata. The ColumnIndex
    /// enables predicate pushdown by providing per-page min/max boundaries
    /// and null page flags.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the row group.
    /// @return A `ColumnIndex` on success, or an `Error` with `OUT_OF_RANGE`,
    ///         `INVALID_FILE` (no index), or `CORRUPT_PAGE`.
    /// @see read_offset_index(), has_page_index()
    [[nodiscard]] expected<ColumnIndex> read_column_index(
            size_t row_group_index, size_t column_index) const {
        if (row_group_index >= metadata_.row_groups.size())
            return Error{ErrorCode::OUT_OF_RANGE, "row group index out of range"};
        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size())
            return Error{ErrorCode::OUT_OF_RANGE, "column index out of range"};

        const auto& chunk = rg.columns[column_index];
        if (!chunk.column_index_offset.has_value() || !chunk.column_index_length.has_value())
            return Error{ErrorCode::INVALID_FILE, "no column index for this column chunk"};

        int64_t ci_offset = *chunk.column_index_offset;
        int32_t ci_length = *chunk.column_index_length;
        if (ci_offset < 0 || ci_length < 0)
            return Error{ErrorCode::CORRUPT_PAGE, "column index offset/length negative"};
        auto uoff = static_cast<size_t>(ci_offset);
        auto ulen = static_cast<size_t>(ci_length);
        if (uoff > file_data_.size() || ulen > file_data_.size() - uoff)
            return Error{ErrorCode::CORRUPT_PAGE, "column index offset/length out of bounds"};

        thrift::CompactDecoder dec(file_data_.data() + ci_offset,
                                    static_cast<size_t>(ci_length));
        ColumnIndex ci;
        ci.deserialize(dec);
        if (!dec.good())
            return Error{ErrorCode::CORRUPT_PAGE, "column index deserialization failed"};
        return ci;
    }

    /// Read the OffsetIndex (page locations) for a column chunk.
    ///
    /// Deserializes the Thrift-encoded OffsetIndex structure, which maps
    /// each data page to its file offset, compressed size, and first row
    /// index. Used together with ColumnIndex for page-level predicate
    /// pushdown and selective I/O.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the row group.
    /// @return An `OffsetIndex` on success, or an `Error` with `OUT_OF_RANGE`,
    ///         `INVALID_FILE` (no index), or `CORRUPT_PAGE`.
    /// @see read_column_index(), has_page_index()
    [[nodiscard]] expected<OffsetIndex> read_offset_index(
            size_t row_group_index, size_t column_index) const {
        if (row_group_index >= metadata_.row_groups.size())
            return Error{ErrorCode::OUT_OF_RANGE, "row group index out of range"};
        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size())
            return Error{ErrorCode::OUT_OF_RANGE, "column index out of range"};

        const auto& chunk = rg.columns[column_index];
        if (!chunk.offset_index_offset.has_value() || !chunk.offset_index_length.has_value())
            return Error{ErrorCode::INVALID_FILE, "no offset index for this column chunk"};

        int64_t oi_offset = *chunk.offset_index_offset;
        int32_t oi_length = *chunk.offset_index_length;
        if (oi_offset < 0 || oi_length < 0)
            return Error{ErrorCode::CORRUPT_PAGE, "offset index offset/length negative"};
        auto uoff2 = static_cast<size_t>(oi_offset);
        auto ulen2 = static_cast<size_t>(oi_length);
        if (uoff2 > file_data_.size() || ulen2 > file_data_.size() - uoff2)
            return Error{ErrorCode::CORRUPT_PAGE, "offset index offset/length out of bounds"};

        thrift::CompactDecoder dec(file_data_.data() + oi_offset,
                                    static_cast<size_t>(oi_length));
        OffsetIndex oi;
        oi.deserialize(dec);
        if (!dec.good())
            return Error{ErrorCode::CORRUPT_PAGE, "offset index deserialization failed"};
        return oi;
    }

    /// Check whether a column chunk has both ColumnIndex and OffsetIndex data.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index within the row group.
    /// @return `true` if both column_index_offset and offset_index_offset are
    ///         present in the column chunk metadata; `false` otherwise
    ///         (including for out-of-range indices).
    /// @see read_column_index(), read_offset_index()
    [[nodiscard]] bool has_page_index(size_t row_group_index, size_t column_index) const {
        if (row_group_index >= metadata_.row_groups.size()) return false;
        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size()) return false;
        const auto& chunk = rg.columns[column_index];
        return chunk.column_index_offset.has_value() && chunk.offset_index_offset.has_value();
    }

    // ===================================================================
    // Special members
    // ===================================================================

    /// Destructor. Releases the in-memory file buffer and all decode state.
    ~ParquetReader() = default;
    /// Move constructor.
    ParquetReader(ParquetReader&&) noexcept = default;
    /// Move assignment operator.
    ParquetReader& operator=(ParquetReader&&) noexcept = default;

private:
    /// Default constructor (private); use open() factory method.
    ParquetReader() = default;

    std::vector<uint8_t>      file_data_;   ///< Raw file bytes loaded into memory.
    thrift::FileMetaData      metadata_;    ///< Deserialized Thrift footer metadata.
    Schema                    schema_;      ///< Column schema built from footer.
    std::string               created_by_;  ///< Writer identification string.
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
    std::unique_ptr<crypto::FileDecryptor> decryptor_;  ///< PME decryptor (nullptr if none).
#endif

    /// Holds decompressed page data so ColumnReader pointers remain valid.
    std::vector<std::vector<uint8_t>> decompressed_buffers_;

    /// Internal: ColumnReader plus the value count extracted from the page header.
    struct ColumnReaderWithCount {
        ColumnReader reader;      ///< Positioned column reader.
        int64_t      num_values;  ///< Number of values in the page.
    };

    /// Internal: construct a ColumnReader for a given row group + column.
    expected<ColumnReaderWithCount> make_column_reader(
            size_t row_group_index, size_t column_index) {
        if (row_group_index >= metadata_.row_groups.size()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "row group index out of range"};
        }

        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size()) {
            return Error{ErrorCode::OUT_OF_RANGE,
                         "column index out of range"};
        }

        const auto& chunk = rg.columns[column_index];
        if (!chunk.meta_data.has_value()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "column chunk has no metadata"};
        }

        const auto& col_meta = *chunk.meta_data;

        // Locate the data page in the file buffer.
        // For dictionary-encoded columns, the dictionary page comes first
        // at dictionary_page_offset, and data_page_offset points to the
        // data page (which contains RLE-encoded indices).
        int64_t offset = col_meta.data_page_offset;
        if (offset < 0 || static_cast<size_t>(offset) >= file_data_.size()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "data_page_offset out of file bounds"};
        }

        size_t remaining = file_data_.size() - static_cast<size_t>(offset);
        const uint8_t* page_start = file_data_.data() + offset;

        // Deserialize the PageHeader to find where the data begins.
        // The serialized size of the PageHeader is variable (Thrift compact
        // encoding). We use decoder.position() to determine how many bytes
        // the PageHeader consumed.
        thrift::CompactDecoder page_dec(page_start, remaining);
        thrift::PageHeader page_header;
        page_header.deserialize(page_dec);

        if (!page_dec.good()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "failed to deserialize PageHeader"};
        }

        size_t header_size = page_dec.position();
        if (page_header.compressed_page_size < 0) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "negative compressed_page_size"};
        }
        size_t page_data_size = static_cast<size_t>(
            page_header.compressed_page_size);
        const uint8_t* page_data = page_start + header_size;

        if (header_size + page_data_size > remaining) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "page data extends past end of file"};
        }

        // --- Decrypt page data if PME is configured for this column ---
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        if (decryptor_) {
            const std::string& col_name = col_meta.path_in_schema.empty()
                ? schema_.column(column_index).name
                : col_meta.path_in_schema[0];

            if (!decryptor_->config().default_column_key.empty() ||
                !decryptor_->config().column_keys.empty()) {
                auto dec_result = decryptor_->decrypt_column_page(
                    page_data, page_data_size, col_name,
                    static_cast<int32_t>(row_group_index), 0);
                if (!dec_result) return dec_result.error();
                decompressed_buffers_.push_back(std::move(*dec_result));
                page_data = decompressed_buffers_.back().data();
                page_data_size = decompressed_buffers_.back().size();
            }
        }
#endif

        // --- Decompress page data if needed ---
        if (col_meta.codec != Compression::UNCOMPRESSED) {
            size_t uncompressed_size = static_cast<size_t>(
                page_header.uncompressed_page_size);
            if (uncompressed_size == 0 || uncompressed_size > PARQUET_MAX_PAGE_SIZE) {
                return Error{ErrorCode::CORRUPT_PAGE,
                             "ParquetReader: uncompressed_page_size exceeds 256 MB hard cap"};
            }
            auto decompressed = decompress(col_meta.codec,
                                           page_data, page_data_size,
                                           uncompressed_size);
            if (!decompressed) {
                return Error{ErrorCode::UNSUPPORTED_COMPRESSION,
                             "decompression failed: " +
                             decompressed.error().message};
            }
            decompressed_buffers_.push_back(std::move(decompressed.value()));
            page_data = decompressed_buffers_.back().data();
            page_data_size = decompressed_buffers_.back().size();
        }

        // Determine num_values from the PageHeader
        int64_t num_values = 0;
        if (page_header.type == PageType::DATA_PAGE &&
            page_header.data_page_header.has_value()) {
            num_values = page_header.data_page_header->num_values;
        } else if (page_header.type == PageType::DATA_PAGE_V2 &&
                   page_header.data_page_header_v2.has_value()) {
            num_values = page_header.data_page_header_v2->num_values;
        } else {
            // Fall back to column metadata num_values
            num_values = col_meta.num_values;
        }

        if (num_values < 0 || num_values > MAX_VALUES_PER_PAGE) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "num_values out of valid range"};
        }

        // Determine the physical type and type_length from the schema
        PhysicalType pt = col_meta.type;
        int32_t type_length = -1;
        if (column_index < schema_.num_columns()) {
            type_length = schema_.column(column_index).type_length;
        }

        ColumnReader col_reader(pt, page_data, page_data_size,
                                num_values, type_length);

        return ColumnReaderWithCount{std::move(col_reader), num_values};
    }

    /// Internal: result of reading and optionally decompressing a page.
    struct PageReadResult {
        const uint8_t* data;        ///< Pointer to (decompressed) page payload.
        size_t         size;        ///< Size of page payload in bytes.
        thrift::PageHeader header;  ///< Deserialized page header.
    };

    /// Internal: read and decompress/decrypt a page at a given file offset.
    expected<PageReadResult> read_page_at(int64_t offset, Compression codec,
            const std::string& col_name = "",
            int32_t rg_index = 0, int32_t page_ordinal = 0) {
        if (offset < 0 || static_cast<size_t>(offset) >= file_data_.size()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "page offset out of file bounds"};
        }

        size_t remaining = file_data_.size() - static_cast<size_t>(offset);
        const uint8_t* page_start = file_data_.data() + offset;

        thrift::CompactDecoder page_dec(page_start, remaining);
        thrift::PageHeader ph;
        ph.deserialize(page_dec);

        if (!page_dec.good()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "failed to deserialize PageHeader"};
        }

        size_t hdr_size = page_dec.position();
        if (ph.compressed_page_size < 0) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "negative compressed_page_size"};
        }
        size_t compressed_size = static_cast<size_t>(ph.compressed_page_size);
        const uint8_t* pdata = page_start + hdr_size;

        if (hdr_size + compressed_size > remaining) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "page data extends past end of file"};
        }

        size_t pdata_size = compressed_size;

        // --- Decrypt page data if PME is configured ---
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
        if (decryptor_ && !col_name.empty()) {
            if (!decryptor_->config().default_column_key.empty() ||
                !decryptor_->config().column_keys.empty()) {
                auto dec_result = decryptor_->decrypt_column_page(
                    pdata, pdata_size, col_name, rg_index, page_ordinal);
                if (!dec_result) return dec_result.error();
                decompressed_buffers_.push_back(std::move(*dec_result));
                pdata = decompressed_buffers_.back().data();
                pdata_size = decompressed_buffers_.back().size();
            }
        }
#endif

        if (codec != Compression::UNCOMPRESSED) {
            size_t uncompressed_size = static_cast<size_t>(
                ph.uncompressed_page_size);
            if (uncompressed_size == 0 || uncompressed_size > PARQUET_MAX_PAGE_SIZE) {
                return Error{ErrorCode::CORRUPT_PAGE,
                             "ParquetReader: uncompressed_page_size exceeds 256 MB hard cap"};
            }
            auto dec_result = decompress(codec, pdata, pdata_size,
                                         uncompressed_size);
            if (!dec_result) {
                return Error{ErrorCode::UNSUPPORTED_COMPRESSION,
                             "decompression failed: " +
                             dec_result.error().message};
            }
            decompressed_buffers_.push_back(std::move(dec_result.value()));
            pdata = decompressed_buffers_.back().data();
            pdata_size = decompressed_buffers_.back().size();
        }

        return PageReadResult{pdata, pdata_size, std::move(ph)};
    }

    /// Internal: dictionary-encoded column read path.
    template <typename T>
    expected<std::vector<T>> read_column_dict(
            const thrift::ColumnMetaData& col_meta,
            const std::string& col_name = "",
            int32_t rg_index = 0) {
        // Step 1: determine where the dictionary page lives
        int64_t dict_offset = col_meta.dictionary_page_offset.value_or(
            col_meta.data_page_offset);

        // Step 2: read the dictionary page
        auto dict_page_result = read_page_at(dict_offset, col_meta.codec,
                                              col_name, rg_index, -1);
        if (!dict_page_result) return dict_page_result.error();

        auto& dict_pr = dict_page_result.value();
        if (dict_pr.header.type != PageType::DICTIONARY_PAGE ||
            !dict_pr.header.dictionary_page_header.has_value()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "expected DICTIONARY_PAGE at dictionary offset"};
        }

        int32_t raw_dict_count = dict_pr.header.dictionary_page_header->num_values;
        if (raw_dict_count < 0 || raw_dict_count > 10'000'000) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "dictionary page num_values out of valid range"};
        }
        size_t num_dict_entries = static_cast<size_t>(raw_dict_count);

        // Step 3: find the data page offset
        // If dictionary_page_offset was set and != data_page_offset, the
        // data page is at data_page_offset.  Otherwise the data page
        // immediately follows the dictionary page in the file.
        int64_t data_offset = col_meta.data_page_offset;
        if (data_offset == dict_offset) {
            // Dictionary page and data page are sequential; skip past the
            // dictionary page in the file to get to the data page.
            // We need the raw page size (compressed) + header.
            size_t dict_raw_start = static_cast<size_t>(dict_offset);
            const uint8_t* dict_start = file_data_.data() + dict_raw_start;
            size_t dict_remaining = file_data_.size() - dict_raw_start;

            thrift::CompactDecoder hdr_dec(dict_start, dict_remaining);
            thrift::PageHeader tmp_hdr;
            tmp_hdr.deserialize(hdr_dec);
            size_t dict_hdr_size = hdr_dec.position();
            if (tmp_hdr.compressed_page_size < 0) {
                return Error{ErrorCode::CORRUPT_PAGE,
                             "negative compressed_page_size in dictionary page"};
            }
            size_t dict_compressed_size = static_cast<size_t>(
                tmp_hdr.compressed_page_size);

            data_offset = dict_offset
                        + static_cast<int64_t>(dict_hdr_size)
                        + static_cast<int64_t>(dict_compressed_size);
        }

        // Step 4: read the data page (contains RLE-encoded indices)
        auto data_page_result = read_page_at(data_offset, col_meta.codec,
                                              col_name, rg_index, 0);
        if (!data_page_result) return data_page_result.error();

        auto& data_pr = data_page_result.value();

        int64_t num_values = 0;
        if (data_pr.header.type == PageType::DATA_PAGE &&
            data_pr.header.data_page_header.has_value()) {
            num_values = data_pr.header.data_page_header->num_values;
        } else if (data_pr.header.type == PageType::DATA_PAGE_V2 &&
                   data_pr.header.data_page_header_v2.has_value()) {
            num_values = data_pr.header.data_page_header_v2->num_values;
        } else {
            num_values = col_meta.num_values;
        }

        if (num_values < 0 || num_values > MAX_VALUES_PER_PAGE) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "num_values out of valid range"};
        }

        // Step 5: decode using DictionaryDecoder
        DictionaryDecoder<T> decoder(dict_pr.data, dict_pr.size,
                                     num_dict_entries, col_meta.type);

        return decoder.decode(data_pr.data, data_pr.size,
                              static_cast<size_t>(num_values));
    }

    /// Internal: DELTA_BINARY_PACKED column read path (INT32/INT64 only).
    template <typename T>
    expected<std::vector<T>> read_column_delta(
            const thrift::ColumnMetaData& col_meta,
            const std::string& col_name = "",
            int32_t rg_index = 0) {
        auto page_result = read_page_at(col_meta.data_page_offset,
                                        col_meta.codec,
                                        col_name, rg_index, 0);
        if (!page_result) return page_result.error();

        auto& pr = page_result.value();

        int64_t num_values = 0;
        if (pr.header.type == PageType::DATA_PAGE &&
            pr.header.data_page_header.has_value()) {
            num_values = pr.header.data_page_header->num_values;
        } else if (pr.header.type == PageType::DATA_PAGE_V2 &&
                   pr.header.data_page_header_v2.has_value()) {
            num_values = pr.header.data_page_header_v2->num_values;
        } else {
            num_values = col_meta.num_values;
        }

        size_t count = static_cast<size_t>(num_values);

        if constexpr (std::is_same_v<T, int32_t>) {
            return delta::decode_int32(pr.data, pr.size, count);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return delta::decode_int64(pr.data, pr.size, count);
        } else {
            return Error{ErrorCode::UNSUPPORTED_ENCODING,
                         "DELTA_BINARY_PACKED only supports INT32/INT64"};
        }
    }

    /// Internal: BYTE_STREAM_SPLIT column read path (FLOAT/DOUBLE only).
    template <typename T>
    expected<std::vector<T>> read_column_bss(
            const thrift::ColumnMetaData& col_meta,
            const std::string& col_name = "",
            int32_t rg_index = 0) {
        auto page_result = read_page_at(col_meta.data_page_offset,
                                        col_meta.codec,
                                        col_name, rg_index, 0);
        if (!page_result) return page_result.error();

        auto& pr = page_result.value();

        int64_t num_values = 0;
        if (pr.header.type == PageType::DATA_PAGE &&
            pr.header.data_page_header.has_value()) {
            num_values = pr.header.data_page_header->num_values;
        } else if (pr.header.type == PageType::DATA_PAGE_V2 &&
                   pr.header.data_page_header_v2.has_value()) {
            num_values = pr.header.data_page_header_v2->num_values;
        } else {
            num_values = col_meta.num_values;
        }

        size_t count = static_cast<size_t>(num_values);

        if constexpr (std::is_same_v<T, float>) {
            return byte_stream_split::decode_float(pr.data, pr.size, count);
        } else if constexpr (std::is_same_v<T, double>) {
            return byte_stream_split::decode_double(pr.data, pr.size, count);
        } else {
            return Error{ErrorCode::UNSUPPORTED_ENCODING,
                         "BYTE_STREAM_SPLIT only supports FLOAT/DOUBLE"};
        }
    }

    /// Internal: RLE-encoded boolean column read path.
    template <typename T>
    expected<std::vector<T>> read_column_rle_bool(
            const thrift::ColumnMetaData& col_meta,
            const std::string& col_name = "",
            int32_t rg_index = 0) {
        auto page_result = read_page_at(col_meta.data_page_offset,
                                        col_meta.codec,
                                        col_name, rg_index, 0);
        if (!page_result) return page_result.error();

        auto& pr = page_result.value();

        int64_t num_values = 0;
        if (pr.header.type == PageType::DATA_PAGE &&
            pr.header.data_page_header.has_value()) {
            num_values = pr.header.data_page_header->num_values;
        } else if (pr.header.type == PageType::DATA_PAGE_V2 &&
                   pr.header.data_page_header_v2.has_value()) {
            num_values = pr.header.data_page_header_v2->num_values;
        } else {
            num_values = col_meta.num_values;
        }

        size_t count = static_cast<size_t>(num_values);

        if constexpr (std::is_same_v<T, bool>) {
            // RLE boolean: 4-byte LE length prefix + RLE payload, bit_width=1
            auto indices = RleDecoder::decode_with_length(
                pr.data, pr.size, /*bit_width=*/1, count);

            std::vector<bool> result;
            result.reserve(count);
            for (size_t i = 0; i < count && i < indices.size(); ++i) {
                result.push_back(indices[i] != 0);
            }
            return result;
        } else {
            return Error{ErrorCode::UNSUPPORTED_ENCODING,
                         "RLE encoding for booleans requires bool type"};
        }
    }

    /// Convert a bool vector to string vector ("true"/"false").
    static std::vector<std::string> to_string_vec(const std::vector<bool>& vals) {
        std::vector<std::string> result;
        result.reserve(vals.size());
        for (bool v : vals) {
            result.push_back(v ? "true" : "false");
        }
        return result;
    }

    /// Convert a numeric vector to string vector via std::to_string.
    template <typename T>
    static std::vector<std::string> to_string_vec(const std::vector<T>& vals) {
        std::vector<std::string> result;
        result.reserve(vals.size());
        for (const auto& v : vals) {
            result.push_back(std::to_string(v));
        }
        return result;
    }

    /// Hex-encode a byte vector to a lowercase hex string.
    static std::string hex_encode(const std::vector<uint8_t>& bytes) {
        static constexpr char hex_chars[] = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);
        for (uint8_t b : bytes) {
            result.push_back(hex_chars[(b >> 4) & 0x0F]);
            result.push_back(hex_chars[b & 0x0F]);
        }
        return result;
    }

    /// Map a legacy ConvertedType enum to the corresponding LogicalType.
    static LogicalType converted_type_to_logical(ConvertedType ct) {
        switch (ct) {
        case ConvertedType::UTF8:             return LogicalType::STRING;
        case ConvertedType::ENUM:             return LogicalType::ENUM;
        case ConvertedType::DATE:             return LogicalType::DATE;
        case ConvertedType::TIME_MILLIS:      return LogicalType::TIME_MS;
        case ConvertedType::TIME_MICROS:      return LogicalType::TIME_US;
        case ConvertedType::TIMESTAMP_MILLIS: return LogicalType::TIMESTAMP_MS;
        case ConvertedType::TIMESTAMP_MICROS: return LogicalType::TIMESTAMP_US;
        case ConvertedType::DECIMAL:          return LogicalType::DECIMAL;
        case ConvertedType::JSON:             return LogicalType::JSON;
        case ConvertedType::BSON:             return LogicalType::BSON;
        default:                              return LogicalType::NONE;
        }
    }
};

} // namespace signet::forge
