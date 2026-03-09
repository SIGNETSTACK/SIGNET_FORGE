// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace signet::forge {

/// @file types.hpp
/// @brief Parquet format enumerations, type traits, and statistics structs.

/// Parquet physical (storage) types as defined in parquet.thrift.
///
/// Every column in a Parquet file stores values in one of these physical
/// representations.  The mapping from C++ types is provided by parquet_type_of.
/// @see LogicalType, parquet_type_of, ColumnDescriptor
enum class PhysicalType : int32_t {
    BOOLEAN              = 0,  ///< 1-bit boolean, bit-packed in pages.
    INT32                = 1,  ///< 32-bit signed integer (little-endian).
    INT64                = 2,  ///< 64-bit signed integer (little-endian).
    INT96                = 3,  ///< 96-bit value (deprecated — legacy Impala timestamps).
    FLOAT                = 4,  ///< IEEE 754 single-precision float.
    DOUBLE               = 5,  ///< IEEE 754 double-precision float.
    BYTE_ARRAY           = 6,  ///< Variable-length byte sequence (strings, binary).
    FIXED_LEN_BYTE_ARRAY = 7   ///< Fixed-length byte array (UUID, vectors, decimals).
};

/// Parquet logical types (from parquet.thrift LogicalType union).
///
/// Logical types add semantic meaning on top of a PhysicalType.  For example,
/// a STRING column is stored as BYTE_ARRAY but interpreted as UTF-8 text.
/// @see PhysicalType, ColumnDescriptor
// Windows <mmsystem.h> defines TIME_MS as a macro — undefine to avoid collision.
#ifdef TIME_MS
#undef TIME_MS
#endif

enum class LogicalType : int32_t {
    NONE       = 0,   ///< No logical annotation — raw physical type.
    STRING     = 1,   ///< UTF-8 string (stored as BYTE_ARRAY).
    ENUM       = 2,   ///< Enum string (stored as BYTE_ARRAY).
    UUID       = 3,   ///< RFC 4122 UUID (stored as FIXED_LEN_BYTE_ARRAY(16)).
    DATE       = 4,   ///< Calendar date — INT32, days since 1970-01-01.
    TIME_MS    = 5,   ///< Time of day — INT32, milliseconds since midnight.
    TIME_US    = 6,   ///< Time of day — INT64, microseconds since midnight.
    TIME_NS    = 7,   ///< Time of day — INT64, nanoseconds since midnight.
    TIMESTAMP_MS = 8, ///< Timestamp — INT64, milliseconds since Unix epoch.
    TIMESTAMP_US = 9, ///< Timestamp — INT64, microseconds since Unix epoch.
    TIMESTAMP_NS = 10,///< Timestamp — INT64, nanoseconds since Unix epoch.
    DECIMAL    = 11,  ///< Fixed-point decimal (INT32/INT64/FIXED_LEN_BYTE_ARRAY).
    JSON       = 12,  ///< JSON document (stored as BYTE_ARRAY).
    BSON       = 13,  ///< BSON document (stored as BYTE_ARRAY).
    FLOAT16    = 14,  ///< IEEE 754 half-precision float (FIXED_LEN_BYTE_ARRAY(2)).
    /// ML embedding vector — FIXED_LEN_BYTE_ARRAY(dim*4).
    /// Signet AI-native extension; stored as standard Parquet types with logical annotation only.
    FLOAT32_VECTOR = 100,
};

/// Legacy Parquet converted types for backward compatibility with older readers.
///
/// Prefer LogicalType for new code.  ConvertedType is written to the Thrift
/// footer only when a corresponding LogicalType mapping exists (e.g. STRING → UTF8).
/// @see LogicalType
enum class ConvertedType : int32_t {
    NONE             = -1, ///< No converted type annotation.
    UTF8             = 0,  ///< UTF-8 encoded string.
    MAP              = 1,  ///< Map (nested group).
    MAP_KEY_VALUE    = 2,  ///< Map key-value pair.
    LIST             = 3,  ///< List (nested group).
    ENUM             = 4,  ///< Enum string.
    DECIMAL          = 5,  ///< Fixed-point decimal.
    DATE             = 6,  ///< Date (days since epoch).
    TIME_MILLIS      = 7,  ///< Time in milliseconds.
    TIME_MICROS      = 8,  ///< Time in microseconds.
    TIMESTAMP_MILLIS = 9,  ///< Timestamp in milliseconds.
    TIMESTAMP_MICROS = 10, ///< Timestamp in microseconds.
    UINT_8           = 11, ///< Unsigned 8-bit integer.
    UINT_16          = 12, ///< Unsigned 16-bit integer.
    UINT_32          = 13, ///< Unsigned 32-bit integer.
    UINT_64          = 14, ///< Unsigned 64-bit integer.
    INT_8            = 15, ///< Signed 8-bit integer.
    INT_16           = 16, ///< Signed 16-bit integer.
    INT_32           = 17, ///< Signed 32-bit integer.
    INT_64           = 18, ///< Signed 64-bit integer.
    JSON             = 19, ///< JSON document.
    BSON             = 20, ///< BSON document.
    INTERVAL         = 21  ///< Time interval.
};

/// Parquet page encoding types.
///
/// Each data page stores values using one of these encodings.  The writer
/// selects encoding per-column (or auto-selects based on data characteristics).
/// @see WriterOptions::default_encoding, WriterOptions::auto_encoding
enum class Encoding : int32_t {
    PLAIN                  = 0, ///< Values stored back-to-back in their native binary layout.
    PLAIN_DICTIONARY       = 2, ///< Legacy dictionary encoding (Parquet 1.0).
    RLE                    = 3, ///< Run-length / bit-packed hybrid (used for booleans and def/rep levels).
    BIT_PACKED             = 4, ///< Deprecated — superseded by RLE.
    DELTA_BINARY_PACKED    = 5, ///< Delta encoding for INT32/INT64 (compact for sorted/sequential data).
    DELTA_LENGTH_BYTE_ARRAY = 6, ///< Delta-encoded lengths + concatenated byte arrays.
    DELTA_BYTE_ARRAY       = 7, ///< Incremental/prefix encoding for byte arrays.
    RLE_DICTIONARY         = 8, ///< Modern dictionary encoding (Parquet 2.0) — dict page + RLE indices.
    BYTE_STREAM_SPLIT      = 9  ///< Byte-stream split for FLOAT/DOUBLE (transposes byte lanes for better compression).
};

/// Parquet compression codecs.
///
/// Snappy is bundled (header-only); ZSTD, LZ4, and Gzip require linking
/// external libraries enabled via CMake options.
/// @see WriterOptions::compression, WriterOptions::auto_compression
enum class Compression : int32_t {
    UNCOMPRESSED = 0, ///< No compression.
    SNAPPY       = 1, ///< Snappy compression (bundled, header-only).
    GZIP         = 2, ///< Gzip/deflate compression (requires SIGNET_ENABLE_GZIP).
    LZO          = 3, ///< LZO compression (not currently supported).
    BROTLI       = 4, ///< Brotli compression (not currently supported).
    LZ4          = 5, ///< LZ4 block compression (requires SIGNET_ENABLE_LZ4).
    ZSTD         = 6, ///< Zstandard compression (requires SIGNET_ENABLE_ZSTD).
    LZ4_RAW      = 7  ///< LZ4 raw (unframed) block compression.
};

/// Parquet page types within a column chunk.
enum class PageType : int32_t {
    DATA_PAGE       = 0, ///< Data page (Parquet 1.0 format).
    INDEX_PAGE      = 1, ///< Index page (reserved, not used by Signet).
    DICTIONARY_PAGE = 2, ///< Dictionary page — contains the value dictionary for RLE_DICTIONARY columns.
    DATA_PAGE_V2    = 3  ///< Data page v2 (Parquet 2.0 format with separate rep/def level sections).
};

/// Parquet field repetition types (nullability / cardinality).
/// @see ColumnDescriptor::repetition
// Windows <sal.h> defines OPTIONAL as a SAL annotation macro — undefine.
#ifdef OPTIONAL
#undef OPTIONAL
#endif
enum class Repetition : int32_t {
    REQUIRED = 0, ///< Exactly one value per row (non-nullable).
    OPTIONAL = 1, ///< Zero or one value per row (nullable).
    REPEATED = 2  ///< Zero or more values per row (list).
};

/// Descriptor for a single column in a Parquet schema.
///
/// Combines the physical storage type, optional logical annotation, repetition
/// level, and type-specific parameters (type_length for FIXED_LEN_BYTE_ARRAY,
/// precision/scale for DECIMAL).
/// @see Schema, Column, SchemaBuilder
struct ColumnDescriptor {
    std::string   name;           ///< Column name (unique within a schema).
    PhysicalType  physical_type;  ///< On-disk storage type.
    LogicalType   logical_type   = LogicalType::NONE;     ///< Semantic annotation (STRING, TIMESTAMP_NS, etc.).
    Repetition    repetition     = Repetition::REQUIRED;  ///< Nullability / cardinality.
    int32_t       type_length    = -1;  ///< Byte length for FIXED_LEN_BYTE_ARRAY columns (-1 = N/A).
    int32_t       precision      = -1;  ///< Decimal precision (-1 = N/A).
    int32_t       scale          = -1;  ///< Decimal scale (-1 = N/A).
};

/// Maps a C++ type to its corresponding Parquet PhysicalType at compile time.
///
/// Specializations exist for: `bool`, `int32_t`, `int64_t`, `float`, `double`,
/// `std::string`.  Access the value via `parquet_type_of<T>::value` or the
/// convenience variable template `parquet_type_of_v<T>`.
/// @tparam T The C++ type to map.
/// @see native_type_of, parquet_type_of_v
template <typename T> struct parquet_type_of;

template <> struct parquet_type_of<bool>        { static constexpr PhysicalType value = PhysicalType::BOOLEAN; };
template <> struct parquet_type_of<int32_t>     { static constexpr PhysicalType value = PhysicalType::INT32; };
template <> struct parquet_type_of<int64_t>     { static constexpr PhysicalType value = PhysicalType::INT64; };
template <> struct parquet_type_of<float>        { static constexpr PhysicalType value = PhysicalType::FLOAT; };
template <> struct parquet_type_of<double>       { static constexpr PhysicalType value = PhysicalType::DOUBLE; };
template <> struct parquet_type_of<std::string>  { static constexpr PhysicalType value = PhysicalType::BYTE_ARRAY; };

/// Convenience variable template: `parquet_type_of_v<double>` == `PhysicalType::DOUBLE`.
template <typename T>
inline constexpr PhysicalType parquet_type_of_v = parquet_type_of<T>::value;

/// Maps a Parquet PhysicalType back to its corresponding C++ native type.
///
/// Use `native_type_of_t<PhysicalType::DOUBLE>` to obtain `double`.
/// @tparam PT The PhysicalType enumerator.
/// @see parquet_type_of, native_type_of_t
template <PhysicalType PT> struct native_type_of;

template <> struct native_type_of<PhysicalType::BOOLEAN>    { using type = bool; };
template <> struct native_type_of<PhysicalType::INT32>      { using type = int32_t; };
template <> struct native_type_of<PhysicalType::INT64>      { using type = int64_t; };
template <> struct native_type_of<PhysicalType::FLOAT>      { using type = float; };
template <> struct native_type_of<PhysicalType::DOUBLE>     { using type = double; };
template <> struct native_type_of<PhysicalType::BYTE_ARRAY> { using type = std::string; };

/// Convenience alias: `native_type_of_t<PhysicalType::INT64>` == `int64_t`.
template <PhysicalType PT>
using native_type_of_t = typename native_type_of<PT>::type;

/// Parquet format version written to the file footer.
inline constexpr int32_t PARQUET_VERSION = 2;
/// Default "created_by" string embedded in every Parquet footer.
inline constexpr const char* SIGNET_CREATED_BY = "SignetStack signet-forge version 0.1.0";
/// "PAR1" magic bytes (little-endian uint32) — marks a standard Parquet file.
inline constexpr uint32_t PARQUET_MAGIC           = 0x31524150;
/// "PARE" magic bytes (little-endian uint32) — marks a Parquet file with an encrypted footer.
inline constexpr uint32_t PARQUET_MAGIC_ENCRYPTED = 0x45524150;

/// Per-column statistics produced by ParquetWriter::close().
/// @see WriteStats
struct ColumnWriteStats {
    std::string column_name;                              ///< Column name from the schema.
    PhysicalType physical_type   = PhysicalType::BYTE_ARRAY; ///< Storage type used on disk.
    Encoding    encoding         = Encoding::PLAIN;       ///< Encoding applied to data pages.
    Compression compression      = Compression::UNCOMPRESSED; ///< Compression codec applied.
    int64_t     uncompressed_bytes = 0;                   ///< Total uncompressed data size (bytes).
    int64_t     compressed_bytes   = 0;                   ///< Total compressed data size (bytes).
    int64_t     num_values       = 0;                     ///< Number of values written.
    int64_t     null_count       = 0;                     ///< Number of null values.
};

/// File-level write statistics returned by ParquetWriter::close().
///
/// Aggregates sizes, row counts, and compression ratios across all row groups
/// and columns.  Per-column breakdowns are available in the @c columns vector.
/// @see ParquetWriter::close(), ColumnWriteStats
struct WriteStats {
    int64_t file_size_bytes          = 0;   ///< Total on-disk file size (bytes).
    int64_t total_rows               = 0;   ///< Total rows written across all row groups.
    int64_t total_row_groups         = 0;   ///< Number of row groups in the file.
    int64_t total_uncompressed_bytes = 0;   ///< Sum of uncompressed page sizes.
    int64_t total_compressed_bytes   = 0;   ///< Sum of compressed page sizes.
    double  compression_ratio        = 1.0; ///< Ratio of uncompressed / compressed (>= 1.0).
    double  bytes_per_row            = 0.0; ///< Average file bytes per row.

    std::vector<ColumnWriteStats> columns;  ///< Per-column statistics.
};

/// Per-column statistics from ParquetReader::file_stats().
/// @see FileStats
struct ColumnFileStats {
    std::string   column_name;                               ///< Column name.
    PhysicalType  physical_type    = PhysicalType::BYTE_ARRAY; ///< Storage type.
    LogicalType   logical_type     = LogicalType::NONE;      ///< Logical annotation.
    Compression   compression      = Compression::UNCOMPRESSED; ///< Compression codec.
    int64_t       uncompressed_bytes = 0;                    ///< Total uncompressed size.
    int64_t       compressed_bytes   = 0;                    ///< Total compressed size.
    int64_t       num_values       = 0;                      ///< Total value count.
    int64_t       null_count       = 0;                      ///< Total null count.
    bool          has_bloom_filter = false;                   ///< Whether a bloom filter is present.
    bool          has_page_index   = false;                   ///< Whether column/offset index is present.
};

/// Aggregate file-level statistics returned by ParquetReader::file_stats().
///
/// Summarises row counts, byte sizes, compression ratios, and per-column
/// metadata across all row groups in the file.
/// @see ParquetReader::file_stats(), ColumnFileStats
struct FileStats {
    int64_t     file_size_bytes    = 0;   ///< Total file size on disk (bytes).
    int64_t     total_rows         = 0;   ///< Total rows in the file.
    int64_t     num_row_groups     = 0;   ///< Number of row groups.
    int64_t     num_columns        = 0;   ///< Number of columns.
    std::string created_by;               ///< "created_by" string from the footer.
    double      compression_ratio  = 1.0; ///< Overall uncompressed / compressed ratio.
    double      bytes_per_row      = 0.0; ///< Average file bytes per row.

    std::vector<ColumnFileStats> columns; ///< Per-column statistics.
};

} // namespace signet::forge
