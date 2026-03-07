// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file mmap_reader.hpp
/// @brief Memory-mapped I/O reader for Parquet files.
///
/// Provides two classes:
/// - MmapReader -- Low-level memory-mapped file handle (mmap/munmap).
/// - MmapParquetReader -- Full Parquet reader backed by MmapReader.
///
/// The mmap reader avoids the full-file copy that ParquetReader::open() does
/// (std::vector allocation + read), instead mapping the file directly into
/// the process address space. For PLAIN-encoded columns this enables true
/// zero-copy reads: ColumnReader points directly into mmap'd memory.
///
/// Platform support:
/// - macOS / Linux: mmap(2) + madvise(MADV_SEQUENTIAL)
/// - Windows: not yet implemented (static_assert at compile time)

#include "signet/types.hpp"
#include "signet/error.hpp"
#include "signet/schema.hpp"
#include "signet/column_reader.hpp"
#include "signet/thrift/compact.hpp"
#include "signet/thrift/types.hpp"
#include "signet/encoding/rle.hpp"
#include "signet/encoding/dictionary.hpp"
#include "signet/encoding/delta.hpp"
#include "signet/encoding/byte_stream_split.hpp"
#include "signet/compression/codec.hpp"
#include "signet/compression/snappy.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#error "MmapReader: Windows support not yet implemented. Use ParquetReader instead."
#endif

namespace signet::forge {

/// Low-level memory-mapped file handle.
///
/// RAII wrapper around POSIX mmap(2). Opens a file in read-only mode,
/// maps it entirely into memory, and unmaps on destruction (or explicit
/// close()). Non-copyable, movable.
///
/// @note No bounds checking is performed by data_at() -- the caller must
///       ensure offsets are within [0, size()).
class MmapReader {
public:
    /// Open a file and memory-map it read-only.
    ///
    /// On success, returns an active MmapReader whose data() points to
    /// the mapped file contents. Applies MADV_SEQUENTIAL if available.
    ///
    /// @param path  Filesystem path to the file to map.
    /// @return An MmapReader on success, or an Error on failure.
    [[nodiscard]] static expected<MmapReader> open(
            const std::filesystem::path& path) {

        // Open the file descriptor
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0) {
            return Error{ErrorCode::IO_ERROR,
                         "cannot open file: " + path.string()};
        }

        // Determine file size via fstat
        struct stat st;
        if (::fstat(fd, &st) != 0) {
            ::close(fd);
            return Error{ErrorCode::IO_ERROR,
                         "cannot stat file: " + path.string()};
        }

        size_t file_size = static_cast<size_t>(st.st_size);
        if (file_size == 0) {
            ::close(fd);
            return Error{ErrorCode::INVALID_FILE,
                         "file is empty: " + path.string()};
        }

        // Memory-map the file (read-only, private copy-on-write)
        void* mapped = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapped == MAP_FAILED) {
            ::close(fd);
            return Error{ErrorCode::IO_ERROR,
                         "mmap failed: " + path.string()};
        }

        // Advise the kernel that we will read sequentially (improves readahead)
#ifdef MADV_SEQUENTIAL
        ::madvise(mapped, file_size, MADV_SEQUENTIAL);
#endif

        MmapReader reader;
        reader.mapped_ = mapped;
        reader.size_   = file_size;
        reader.fd_     = fd;
        return reader;
    }

    // -- Access the mapped memory --------------------------------------------

    /// Pointer to the start of the mapped file.
    [[nodiscard]] const uint8_t* data() const {
        return static_cast<const uint8_t*>(mapped_);
    }

    /// Total file size in bytes.
    [[nodiscard]] size_t size() const { return size_; }

    /// Pointer to mapped memory at a given offset.
    /// No bounds checking -- caller must ensure offset < size().
    [[nodiscard]] const uint8_t* data_at(size_t offset) const {
        return data() + offset;
    }

    // -- Close / unmap -------------------------------------------------------

    /// Unmap the file and close the file descriptor.
    ///
    /// Safe to call multiple times; subsequent calls are no-ops.
    void close() {
        if (mapped_ != nullptr && mapped_ != MAP_FAILED) {
            ::munmap(mapped_, size_);
            mapped_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
        size_ = 0;
    }

    /// Destructor -- unmaps file and closes fd.
    ~MmapReader() {
        close();
    }

    // -- Non-copyable, movable -----------------------------------------------

    MmapReader(const MmapReader&) = delete;            ///< Non-copyable.
    MmapReader& operator=(const MmapReader&) = delete; ///< Non-copyable.

    /// Move constructor -- transfers ownership of the mapping.
    MmapReader(MmapReader&& other) noexcept
        : mapped_(other.mapped_)
        , size_(other.size_)
        , fd_(other.fd_)
    {
        other.mapped_ = nullptr;
        other.size_   = 0;
        other.fd_     = -1;
    }

    /// Move assignment -- transfers ownership of the mapping.
    MmapReader& operator=(MmapReader&& other) noexcept {
        if (this != &other) {
            close();
            mapped_       = other.mapped_;
            size_         = other.size_;
            fd_           = other.fd_;
            other.mapped_ = nullptr;
            other.size_   = 0;
            other.fd_     = -1;
        }
        return *this;
    }

    /// Returns true if the mapping is currently active.
    [[nodiscard]] bool is_open() const {
        return mapped_ != nullptr && mapped_ != MAP_FAILED;
    }

    /// Default constructor — creates a closed/unmapped reader.
    MmapReader() = default;

private:
    void*  mapped_ = nullptr;
    size_t size_   = 0;
    int    fd_     = -1;
};

/// Full Parquet reader backed by memory-mapped I/O.
///
/// Opens a Parquet file via MmapReader, validates the PAR1 magic bytes,
/// deserializes the Thrift FileMetaData footer, and reconstructs the Schema.
/// Column data is read directly from mmap'd memory, enabling zero-copy
/// access for PLAIN-encoded columns and automatic decompression for
/// compressed pages.
///
/// Supports PLAIN, RLE_DICTIONARY, DELTA_BINARY_PACKED, BYTE_STREAM_SPLIT,
/// and RLE boolean encodings. Encrypted footers (PARE magic) are rejected
/// -- use ParquetReader with an EncryptionConfig for those files.
///
/// Non-copyable, movable.
///
/// @see MmapReader     (low-level mmap handle)
/// @see ParquetReader  (non-mmap reader alternative)
/// @see ColumnReader   (per-page value decoder)
class MmapParquetReader {
public:
    /// Open a Parquet file with memory-mapped I/O.
    ///
    /// Validates the file structure (PAR1 magic, footer length), deserializes
    /// the FileMetaData, and reconstructs the column schema.
    ///
    /// @param path  Filesystem path to the Parquet file.
    /// @return An MmapParquetReader on success, or an Error.
    [[nodiscard]] static expected<MmapParquetReader> open(
            const std::filesystem::path& path) {

        // Memory-map the file
        auto mmap_result = MmapReader::open(path);
        if (!mmap_result) return mmap_result.error();

        auto mmap = std::move(*mmap_result);

        const uint8_t* file_data = mmap.data();
        const size_t   sz        = mmap.size();

        // --- Validate minimum size: 4 (magic) + 4 (footer len) + 4 (magic) = 12 ---
        if (sz < 12) {
            return Error{ErrorCode::INVALID_FILE,
                         "file too small to be a valid Parquet file: " +
                         path.string()};
        }

        // --- Verify PAR1 magic at start ---
        uint32_t magic_start;
        std::memcpy(&magic_start, file_data, 4);
        if (magic_start != PARQUET_MAGIC) {
            return Error{ErrorCode::INVALID_FILE,
                         "missing PAR1 magic at start of file"};
        }

        // --- Verify PAR1 or PARE magic at end ---
        uint32_t magic_end;
        std::memcpy(&magic_end, file_data + sz - 4, 4);

        if (magic_end != PARQUET_MAGIC && magic_end != PARQUET_MAGIC_ENCRYPTED) {
            return Error{ErrorCode::INVALID_FILE,
                         "missing PAR1/PARE magic at end of file"};
        }

        if (magic_end == PARQUET_MAGIC_ENCRYPTED) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "MmapParquetReader does not support encrypted footers; "
                         "use ParquetReader with an EncryptionConfig instead"};
        }

        // --- Read footer length (4-byte LE uint32 at [size-8, size-4]) ---
        uint32_t footer_len;
        std::memcpy(&footer_len, file_data + sz - 8, 4);

        if (footer_len == 0 || static_cast<size_t>(footer_len) > sz - 12) {
            return Error{ErrorCode::CORRUPT_FOOTER,
                         "invalid footer length: " + std::to_string(footer_len)};
        }

        // --- Deserialize FileMetaData from footer (directly from mmap) ---
        size_t footer_offset = sz - 8 - footer_len;
        const uint8_t* footer_ptr = file_data + footer_offset;

        thrift::CompactDecoder dec(footer_ptr, footer_len);
        thrift::FileMetaData metadata;
        metadata.deserialize(dec);

        if (!dec.good()) {
            return Error{ErrorCode::CORRUPT_FOOTER,
                         "thrift deserialization of FileMetaData failed"};
        }

        // --- Build Schema from FileMetaData.schema ---
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
                cd.name          = elem.name;
                cd.physical_type = elem.type.value_or(PhysicalType::BYTE_ARRAY);
                cd.repetition    = elem.repetition_type.value_or(Repetition::REQUIRED);

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
        MmapParquetReader reader;
        reader.mmap_       = std::move(mmap);
        reader.metadata_   = std::move(metadata);
        reader.schema_     = Schema(std::move(schema_name), std::move(columns));
        reader.created_by_ = reader.metadata_.created_by.value_or("");

        return reader;
    }

    // -- File metadata accessors ---------------------------------------------

    /// The file's column schema.
    [[nodiscard]] const Schema& schema() const { return schema_; }

    /// Total number of rows across all row groups.
    [[nodiscard]] int64_t num_rows() const { return metadata_.num_rows; }

    /// Number of row groups in the file.
    [[nodiscard]] int64_t num_row_groups() const {
        return static_cast<int64_t>(metadata_.row_groups.size());
    }

    /// The "created by" string from the file footer (may be empty).
    [[nodiscard]] const std::string& created_by() const { return created_by_; }

    /// User-defined key-value metadata from the file footer.
    /// @return A reference to the metadata vector, or an empty vector if none.
    [[nodiscard]] const std::vector<thrift::KeyValue>& key_value_metadata() const {
        static const std::vector<thrift::KeyValue> empty;
        return metadata_.key_value_metadata.has_value()
                   ? *metadata_.key_value_metadata
                   : empty;
    }

    // -- Row group info ------------------------------------------------------

    /// Summary information for a single row group.
    struct RowGroupInfo {
        int64_t num_rows;        ///< Number of rows in this row group.
        int64_t total_byte_size; ///< Total uncompressed byte size of the row group.
        int64_t row_group_index; ///< Zero-based index of this row group.
    };

    /// Retrieve summary information for a specific row group.
    /// @param index  Zero-based row group index.
    /// @return A RowGroupInfo struct.
    [[nodiscard]] RowGroupInfo row_group(size_t index) const {
        const auto& rg = metadata_.row_groups[index];
        return {rg.num_rows,
                rg.total_byte_size,
                static_cast<int64_t>(index)};
    }

    // -- Statistics for a column in a row group ------------------------------

    /// Retrieve the Thrift Statistics for a column chunk.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index.
    /// @return Pointer to the Statistics struct, or nullptr if unavailable.
    [[nodiscard]] const thrift::Statistics* column_statistics(
            size_t row_group_index, size_t column_index) const {
        if (row_group_index >= metadata_.row_groups.size()) return nullptr;
        const auto& rg = metadata_.row_groups[row_group_index];
        if (column_index >= rg.columns.size()) return nullptr;
        const auto& chunk = rg.columns[column_index];
        if (!chunk.meta_data.has_value()) return nullptr;
        if (!chunk.meta_data->statistics.has_value()) return nullptr;
        return &(*chunk.meta_data->statistics);
    }

    // -- Typed column reads --------------------------------------------------

    /// Read an entire column from a row group as a typed vector.
    ///
    /// Automatically detects the encoding strategy (PLAIN, dictionary,
    /// DELTA_BINARY_PACKED, BYTE_STREAM_SPLIT, or RLE boolean) and
    /// dispatches to the appropriate decoder.
    ///
    /// @tparam T               The C++ type to decode into (bool, int32_t,
    ///                         int64_t, float, double, std::string).
    /// @param  row_group_index Zero-based row group index.
    /// @param  column_index    Zero-based column index.
    /// @return A vector of decoded values, or an Error.
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

        // --- Detect encoding strategy ---
        bool has_dict = false;
        Encoding data_encoding = Encoding::PLAIN;

        for (auto enc : col_meta.encodings) {
            if (enc == Encoding::PLAIN_DICTIONARY ||
                enc == Encoding::RLE_DICTIONARY) {
                has_dict = true;
            }
            if (enc == Encoding::DELTA_BINARY_PACKED) {
                data_encoding = Encoding::DELTA_BINARY_PACKED;
            }
            if (enc == Encoding::BYTE_STREAM_SPLIT) {
                data_encoding = Encoding::BYTE_STREAM_SPLIT;
            }
            if (enc == Encoding::RLE &&
                col_meta.type == PhysicalType::BOOLEAN) {
                data_encoding = Encoding::RLE;
            }
        }

        // --- Dictionary encoding path ---
        if (has_dict) {
            if constexpr (std::is_same_v<T, std::string> ||
                          std::is_same_v<T, int32_t> ||
                          std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, float> ||
                          std::is_same_v<T, double>) {
                return read_column_dict<T>(col_meta,
                    static_cast<int32_t>(row_group_index));
            } else {
                return Error{ErrorCode::UNSUPPORTED_ENCODING,
                             "dictionary encoding not supported for this type"};
            }
        }

        // --- DELTA_BINARY_PACKED path ---
        if (data_encoding == Encoding::DELTA_BINARY_PACKED) {
            if constexpr (std::is_same_v<T, int32_t> ||
                          std::is_same_v<T, int64_t>) {
                return read_column_delta<T>(col_meta);
            } else {
                return Error{ErrorCode::UNSUPPORTED_ENCODING,
                             "DELTA_BINARY_PACKED only supports INT32/INT64"};
            }
        }

        // --- BYTE_STREAM_SPLIT path ---
        if (data_encoding == Encoding::BYTE_STREAM_SPLIT) {
            if constexpr (std::is_same_v<T, float> ||
                          std::is_same_v<T, double>) {
                return read_column_bss<T>(col_meta);
            } else {
                return Error{ErrorCode::UNSUPPORTED_ENCODING,
                             "BYTE_STREAM_SPLIT only supports FLOAT/DOUBLE"};
            }
        }

        // --- RLE boolean path ---
        if (data_encoding == Encoding::RLE &&
            col_meta.type == PhysicalType::BOOLEAN) {
            if constexpr (std::is_same_v<T, bool>) {
                return read_column_rle_bool(col_meta);
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

    // -- String reads --------------------------------------------------------

    /// Read a column and convert all values to their string representations.
    ///
    /// Booleans become "true"/"false", numerics use std::to_string(),
    /// BYTE_ARRAY values are returned as-is, and FIXED_LEN_BYTE_ARRAY
    /// values are hex-encoded.
    ///
    /// @param row_group_index  Zero-based row group index.
    /// @param column_index     Zero-based column index.
    /// @return A vector of string-converted values, or an Error.
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
            return read_column<std::string>(row_group_index, column_index);
        }
        case PhysicalType::FIXED_LEN_BYTE_ARRAY: {
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

    // -- Read all rows -------------------------------------------------------

    /// Read all rows from all row groups as a vector of string rows.
    ///
    /// Each inner vector represents one row, with columns converted to
    /// strings via read_column_as_strings(). Useful for inspection and
    /// debugging, but not recommended for high-performance paths.
    ///
    /// @return A 2D vector [row][column] of string values, or an Error.
    expected<std::vector<std::vector<std::string>>> read_all() {
        size_t num_cols = schema_.num_columns();
        std::vector<std::vector<std::string>> rows;
        rows.reserve(static_cast<size_t>(metadata_.num_rows));

        for (size_t rg = 0; rg < metadata_.row_groups.size(); ++rg) {
            // Read all columns for this row group
            size_t rg_num_cols = num_cols;
            std::vector<std::vector<std::string>> col_data(rg_num_cols);
            for (size_t c = 0; c < rg_num_cols; ++c) {
                auto res = read_column_as_strings(rg, c);
                if (!res) return res.error();
                col_data[c] = std::move(res.value());
            }

            if (col_data.empty() || col_data[0].empty()) continue;

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

    // -- Access the underlying mmap ------------------------------------------

    /// Direct access to the memory-mapped file data.
    [[nodiscard]] const MmapReader& mmap() const { return mmap_; }

    // -- Special members -----------------------------------------------------

    ~MmapParquetReader() = default;                                        ///< Default destructor.
    MmapParquetReader(MmapParquetReader&&) noexcept = default;             ///< Move-constructible.
    MmapParquetReader& operator=(MmapParquetReader&&) noexcept = default;  ///< Move-assignable.

private:
    /// Private default constructor -- use the static open() factory.
    MmapParquetReader() = default;

    MmapReader               mmap_;
    thrift::FileMetaData     metadata_;
    Schema                   schema_;
    std::string              created_by_;

    // Holds decompressed page data so ColumnReader pointers remain valid
    std::vector<std::vector<uint8_t>> decompressed_buffers_;

    /// Internal: ColumnReader bundled with its value count.
    struct ColumnReaderWithCount {
        ColumnReader reader;
        int64_t      num_values;
    };

    /// Internal: result of reading and decompressing a page.
    struct PageReadResult {
        const uint8_t*     data;
        size_t             size;
        thrift::PageHeader header;
    };

    /// Read and optionally decompress a page at the given file offset.
    expected<PageReadResult> read_page_at(int64_t offset, Compression codec) {
        if (offset < 0 || static_cast<size_t>(offset) >= mmap_.size()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "page offset out of file bounds"};
        }

        size_t remaining = mmap_.size() - static_cast<size_t>(offset);
        const uint8_t* page_start = mmap_.data_at(static_cast<size_t>(offset));

        thrift::CompactDecoder page_dec(page_start, remaining);
        thrift::PageHeader ph;
        ph.deserialize(page_dec);

        if (!page_dec.good()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "failed to deserialize PageHeader"};
        }

        size_t hdr_size = page_dec.position();
        size_t compressed_size = static_cast<size_t>(ph.compressed_page_size);
        const uint8_t* pdata = page_start + hdr_size;

        if (hdr_size + compressed_size > remaining) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "page data extends past end of file"};
        }

        size_t pdata_size = compressed_size;

        // Decompress if needed
        if (codec != Compression::UNCOMPRESSED) {
            size_t uncompressed_size = static_cast<size_t>(
                ph.uncompressed_page_size);
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

    /// Construct a PLAIN-encoded ColumnReader for a given column chunk.
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
        int64_t offset = col_meta.data_page_offset;

        if (offset < 0 || static_cast<size_t>(offset) >= mmap_.size()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "data_page_offset out of file bounds"};
        }

        // Read from mmap directly -- no file I/O
        size_t remaining = mmap_.size() - static_cast<size_t>(offset);
        const uint8_t* page_start = mmap_.data_at(static_cast<size_t>(offset));

        // Deserialize the PageHeader
        thrift::CompactDecoder page_dec(page_start, remaining);
        thrift::PageHeader page_header;
        page_header.deserialize(page_dec);

        if (!page_dec.good()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "failed to deserialize PageHeader"};
        }

        size_t header_size = page_dec.position();
        size_t page_data_size = static_cast<size_t>(
            page_header.compressed_page_size);
        const uint8_t* page_data = page_start + header_size;

        if (header_size + page_data_size > remaining) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "page data extends past end of file"};
        }

        // Decompress if needed
        if (col_meta.codec != Compression::UNCOMPRESSED) {
            size_t uncompressed_size = static_cast<size_t>(
                page_header.uncompressed_page_size);
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

        // Determine num_values
        int64_t num_values = 0;
        if (page_header.type == PageType::DATA_PAGE &&
            page_header.data_page_header.has_value()) {
            num_values = page_header.data_page_header->num_values;
        } else if (page_header.type == PageType::DATA_PAGE_V2 &&
                   page_header.data_page_header_v2.has_value()) {
            num_values = page_header.data_page_header_v2->num_values;
        } else {
            num_values = col_meta.num_values;
        }

        // Determine physical type and type_length
        PhysicalType pt = col_meta.type;
        int32_t type_length = -1;
        if (column_index < schema_.num_columns()) {
            type_length = schema_.column(column_index).type_length;
        }

        ColumnReader col_reader(pt, page_data, page_data_size,
                                num_values, type_length);

        return ColumnReaderWithCount{std::move(col_reader), num_values};
    }

    /// Read a dictionary-encoded column.
    template <typename T>
    expected<std::vector<T>> read_column_dict(
            const thrift::ColumnMetaData& col_meta,
            int32_t rg_index = 0) {
        (void)rg_index;

        int64_t dict_offset = col_meta.dictionary_page_offset.value_or(
            col_meta.data_page_offset);

        // Read the dictionary page
        auto dict_page_result = read_page_at(dict_offset, col_meta.codec);
        if (!dict_page_result) return dict_page_result.error();

        auto& dict_pr = dict_page_result.value();
        if (dict_pr.header.type != PageType::DICTIONARY_PAGE ||
            !dict_pr.header.dictionary_page_header.has_value()) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "expected DICTIONARY_PAGE at dictionary offset"};
        }

        size_t num_dict_entries = static_cast<size_t>(
            dict_pr.header.dictionary_page_header->num_values);

        // Find the data page offset
        int64_t data_offset = col_meta.data_page_offset;
        if (data_offset == dict_offset) {
            // Dictionary and data pages are sequential -- skip past the
            // dictionary page to reach the data page.
            size_t dict_raw_start = static_cast<size_t>(dict_offset);
            const uint8_t* dict_start = mmap_.data_at(dict_raw_start);
            size_t dict_remaining = mmap_.size() - dict_raw_start;

            thrift::CompactDecoder hdr_dec(dict_start, dict_remaining);
            thrift::PageHeader tmp_hdr;
            tmp_hdr.deserialize(hdr_dec);
            size_t dict_hdr_size = hdr_dec.position();
            size_t dict_compressed_size = static_cast<size_t>(
                tmp_hdr.compressed_page_size);

            data_offset = dict_offset
                        + static_cast<int64_t>(dict_hdr_size)
                        + static_cast<int64_t>(dict_compressed_size);
        }

        // Read the data page (RLE-encoded indices)
        auto data_page_result = read_page_at(data_offset, col_meta.codec);
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

        DictionaryDecoder<T> decoder(dict_pr.data, dict_pr.size,
                                     num_dict_entries, col_meta.type);

        return decoder.decode(data_pr.data, data_pr.size,
                              static_cast<size_t>(num_values));
    }

    /// Read a DELTA_BINARY_PACKED-encoded column (INT32/INT64 only).
    template <typename T>
    expected<std::vector<T>> read_column_delta(
            const thrift::ColumnMetaData& col_meta) {
        auto page_result = read_page_at(col_meta.data_page_offset,
                                        col_meta.codec);
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

    /// Read a BYTE_STREAM_SPLIT-encoded column (FLOAT/DOUBLE only).
    template <typename T>
    expected<std::vector<T>> read_column_bss(
            const thrift::ColumnMetaData& col_meta) {
        auto page_result = read_page_at(col_meta.data_page_offset,
                                        col_meta.codec);
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

    /// Read an RLE-encoded boolean column.
    expected<std::vector<bool>> read_column_rle_bool(
            const thrift::ColumnMetaData& col_meta) {
        auto page_result = read_page_at(col_meta.data_page_offset,
                                        col_meta.codec);
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

        // RLE boolean: 4-byte LE length prefix + RLE payload, bit_width=1
        auto indices = RleDecoder::decode_with_length(
            pr.data, pr.size, /*bit_width=*/1, count);

        std::vector<bool> result;
        result.reserve(count);
        for (size_t i = 0; i < count && i < indices.size(); ++i) {
            result.push_back(indices[i] != 0);
        }
        return result;
    }

    // -------------------------------------------------------------------
    // String conversion helpers
    // -------------------------------------------------------------------

    /// Convert a bool vector to string ("true"/"false").
    static std::vector<std::string> to_string_vec(const std::vector<bool>& vals) {
        std::vector<std::string> result;
        result.reserve(vals.size());
        for (bool v : vals) {
            result.push_back(v ? "true" : "false");
        }
        return result;
    }

    /// Convert a numeric vector to strings via std::to_string().
    template <typename T>
    static std::vector<std::string> to_string_vec(const std::vector<T>& vals) {
        std::vector<std::string> result;
        result.reserve(vals.size());
        for (const auto& v : vals) {
            result.push_back(std::to_string(v));
        }
        return result;
    }

    /// Hex-encode a byte vector (lowercase, no prefix).
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

    /// Map legacy ConvertedType to modern LogicalType.
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
