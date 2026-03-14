// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
//
// signet_forge.h — C FFI for Signet Forge Parquet library
//
// Flat extern "C" API with opaque handles. Designed for consumption by
// Rust (signet-forge-sys), Python ctypes, and any other FFI-capable language.

#ifndef SIGNET_FORGE_FFI_H
#define SIGNET_FORGE_FFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

/// Return the library version string (e.g. "0.1.0"). The pointer is
/// statically allocated and must NOT be freed.
const char* signet_version(void);

// ---------------------------------------------------------------------------
// Error type
// ---------------------------------------------------------------------------

/// Error codes mirroring signet::forge::ErrorCode.
enum signet_error_code_t {
    SIGNET_OK                       = 0,
    SIGNET_IO_ERROR                 = 1,
    SIGNET_INVALID_FILE             = 2,
    SIGNET_CORRUPT_FOOTER           = 3,
    SIGNET_CORRUPT_PAGE             = 4,
    SIGNET_UNSUPPORTED_ENCODING     = 5,
    SIGNET_UNSUPPORTED_COMPRESSION  = 6,
    SIGNET_UNSUPPORTED_TYPE         = 7,
    SIGNET_SCHEMA_MISMATCH          = 8,
    SIGNET_OUT_OF_RANGE             = 9,
    SIGNET_THRIFT_DECODE_ERROR      = 10,
    SIGNET_ENCRYPTION_ERROR         = 11,
    SIGNET_HASH_CHAIN_BROKEN        = 12,
    SIGNET_LICENSE_ERROR             = 13,
    SIGNET_LICENSE_LIMIT_EXCEEDED   = 14,
    SIGNET_INTERNAL_ERROR           = 15
};

/// Error returned by value from C API functions. code == SIGNET_OK means
/// success. When code != SIGNET_OK, `message` points to a heap-allocated
/// null-terminated string that the caller must free with signet_error_free().
typedef struct {
    int32_t code;
    const char* message;  // NULL when code == SIGNET_OK
} signet_error_t;

/// Free the message string inside an error. Safe to call on SIGNET_OK errors.
void signet_error_free(signet_error_t* err);

// ---------------------------------------------------------------------------
// String types for FFI
// ---------------------------------------------------------------------------

/// Owned string returned from C API. Caller must free with signet_string_free().
typedef struct {
    const char* ptr;  // Heap-allocated, null-terminated
    size_t len;       // Length excluding null terminator
} signet_string_t;

/// Free a signet_string_t. Safe to call on zeroed structs.
void signet_string_free(signet_string_t* s);

// ---------------------------------------------------------------------------
// Enums (mirror C++ enums as int32_t)
// ---------------------------------------------------------------------------

/// Parquet physical types.
enum signet_physical_type_t {
    SIGNET_BOOLEAN              = 0,
    SIGNET_INT32                = 1,
    SIGNET_INT64                = 2,
    SIGNET_INT96                = 3,
    SIGNET_FLOAT                = 4,
    SIGNET_DOUBLE               = 5,
    SIGNET_BYTE_ARRAY           = 6,
    SIGNET_FIXED_LEN_BYTE_ARRAY = 7
};

/// Parquet logical types.
enum signet_logical_type_t {
    SIGNET_LOGICAL_NONE         = 0,
    SIGNET_LOGICAL_STRING       = 1,
    SIGNET_LOGICAL_ENUM         = 2,
    SIGNET_LOGICAL_UUID         = 3,
    SIGNET_LOGICAL_DATE         = 4,
    SIGNET_LOGICAL_TIME_MS      = 5,
    SIGNET_LOGICAL_TIME_US      = 6,
    SIGNET_LOGICAL_TIME_NS      = 7,
    SIGNET_LOGICAL_TIMESTAMP_MS = 8,
    SIGNET_LOGICAL_TIMESTAMP_US = 9,
    SIGNET_LOGICAL_TIMESTAMP_NS = 10,
    SIGNET_LOGICAL_DECIMAL      = 11,
    SIGNET_LOGICAL_JSON         = 12,
    SIGNET_LOGICAL_BSON         = 13,
    SIGNET_LOGICAL_FLOAT16      = 14,
    SIGNET_LOGICAL_FLOAT32_VECTOR = 100
};

/// Encoding types.
enum signet_encoding_t {
    SIGNET_ENCODING_PLAIN                = 0,
    SIGNET_ENCODING_PLAIN_DICTIONARY     = 2,
    SIGNET_ENCODING_RLE                  = 3,
    SIGNET_ENCODING_BIT_PACKED           = 4,
    SIGNET_ENCODING_DELTA_BINARY_PACKED  = 5,
    SIGNET_ENCODING_DELTA_LENGTH_BYTE_ARRAY = 6,
    SIGNET_ENCODING_DELTA_BYTE_ARRAY     = 7,
    SIGNET_ENCODING_RLE_DICTIONARY       = 8,
    SIGNET_ENCODING_BYTE_STREAM_SPLIT    = 9
};

/// Compression codecs.
enum signet_compression_t {
    SIGNET_COMPRESSION_UNCOMPRESSED = 0,
    SIGNET_COMPRESSION_SNAPPY       = 1,
    SIGNET_COMPRESSION_GZIP         = 2,
    SIGNET_COMPRESSION_LZO          = 3,
    SIGNET_COMPRESSION_BROTLI       = 4,
    SIGNET_COMPRESSION_LZ4          = 5,
    SIGNET_COMPRESSION_ZSTD         = 6,
    SIGNET_COMPRESSION_LZ4_RAW      = 7
};

// ---------------------------------------------------------------------------
// Opaque handle types
// ---------------------------------------------------------------------------

typedef struct signet_schema_builder_s signet_schema_builder_t;
typedef struct signet_schema_s         signet_schema_t;
typedef struct signet_writer_options_s signet_writer_options_t;
typedef struct signet_writer_s         signet_writer_t;
typedef struct signet_reader_s         signet_reader_t;

// ---------------------------------------------------------------------------
// SchemaBuilder
// ---------------------------------------------------------------------------

/// Create a new schema builder with the given root name.
signet_schema_builder_t* signet_schema_builder_new(const char* name);

/// Add a BOOLEAN column.
signet_error_t signet_schema_builder_add_bool(
    signet_schema_builder_t* builder, const char* col_name);

/// Add an INT32 column.
signet_error_t signet_schema_builder_add_int32(
    signet_schema_builder_t* builder, const char* col_name);

/// Add an INT64 column.
signet_error_t signet_schema_builder_add_int64(
    signet_schema_builder_t* builder, const char* col_name);

/// Add a FLOAT column.
signet_error_t signet_schema_builder_add_float(
    signet_schema_builder_t* builder, const char* col_name);

/// Add a DOUBLE column.
signet_error_t signet_schema_builder_add_double(
    signet_schema_builder_t* builder, const char* col_name);

/// Add a STRING (BYTE_ARRAY) column.
signet_error_t signet_schema_builder_add_string(
    signet_schema_builder_t* builder, const char* col_name);

/// Build the schema, consuming the builder. On success, *out_schema is set
/// and the builder is freed. On failure, the builder is NOT freed — use
/// signet_schema_builder_free() to release it manually.
signet_error_t signet_schema_builder_build(
    signet_schema_builder_t* builder,
    signet_schema_t** out_schema);

/// Free a schema builder without building. Use this to release a builder
/// that was not consumed by signet_schema_builder_build() (e.g. on error).
void signet_schema_builder_free(signet_schema_builder_t* builder);

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

/// Free a schema.
void signet_schema_free(signet_schema_t* schema);

/// Return the number of columns.
size_t signet_schema_num_columns(const signet_schema_t* schema);

/// Return the name of column at `index`. The returned signet_string_t is
/// a copy that the caller must free with signet_string_free().
signet_string_t signet_schema_column_name(
    const signet_schema_t* schema, size_t index);

/// Return the physical type of column at `index`.
int32_t signet_schema_column_physical_type(
    const signet_schema_t* schema, size_t index);

/// Return the logical type of column at `index` (signet_logical_type_t).
/// Returns -1 if schema is NULL or index is out of range.
int32_t signet_schema_column_logical_type(
    const signet_schema_t* schema, size_t index);

// ---------------------------------------------------------------------------
// WriterOptions
// ---------------------------------------------------------------------------

/// Create default writer options.
signet_writer_options_t* signet_writer_options_new(void);

/// Free writer options.
void signet_writer_options_free(signet_writer_options_t* opts);

/// Set the target row group size (number of rows).
void signet_writer_options_set_row_group_size(
    signet_writer_options_t* opts, int64_t size);

/// Set the default encoding.
void signet_writer_options_set_encoding(
    signet_writer_options_t* opts, int32_t encoding);

/// Set the compression codec.
void signet_writer_options_set_compression(
    signet_writer_options_t* opts, int32_t compression);

/// Enable or disable auto-encoding selection.
void signet_writer_options_set_auto_encoding(
    signet_writer_options_t* opts, uint8_t enabled);

// ---------------------------------------------------------------------------
// ParquetWriter
// ---------------------------------------------------------------------------

/// Open a new Parquet file for writing.
signet_error_t signet_writer_open(
    const char* path,
    const signet_schema_t* schema,
    const signet_writer_options_t* options,  // May be NULL for defaults
    signet_writer_t** out_writer);

/// Write a column of bool values.
signet_error_t signet_writer_write_column_bool(
    signet_writer_t* writer, size_t col_index,
    const uint8_t* values, size_t count);

/// Write a column of int32 values.
signet_error_t signet_writer_write_column_int32(
    signet_writer_t* writer, size_t col_index,
    const int32_t* values, size_t count);

/// Write a column of int64 values.
signet_error_t signet_writer_write_column_int64(
    signet_writer_t* writer, size_t col_index,
    const int64_t* values, size_t count);

/// Write a column of float values.
signet_error_t signet_writer_write_column_float(
    signet_writer_t* writer, size_t col_index,
    const float* values, size_t count);

/// Write a column of double values.
signet_error_t signet_writer_write_column_double(
    signet_writer_t* writer, size_t col_index,
    const double* values, size_t count);

/// Write a column of string values. Each string is a null-terminated
/// C string. The array `values` has `count` elements.
signet_error_t signet_writer_write_column_string(
    signet_writer_t* writer, size_t col_index,
    const char* const* values, size_t count);

/// Write a single row as string values (one per column, null-terminated).
signet_error_t signet_writer_write_row(
    signet_writer_t* writer,
    const char* const* values, size_t num_values);

/// Flush the current row group to disk.
signet_error_t signet_writer_flush_row_group(signet_writer_t* writer);

/// Close the writer and finalize the Parquet file.
signet_error_t signet_writer_close(signet_writer_t* writer);

/// Return the number of rows written so far.
int64_t signet_writer_rows_written(const signet_writer_t* writer);

/// Return 1 if the writer is open, 0 otherwise.
uint8_t signet_writer_is_open(const signet_writer_t* writer);

/// Free the writer. If still open, performs a best-effort close first.
void signet_writer_free(signet_writer_t* writer);

// ---------------------------------------------------------------------------
// ParquetReader
// ---------------------------------------------------------------------------

/// Open a Parquet file for reading.
signet_error_t signet_reader_open(
    const char* path,
    signet_reader_t** out_reader);

/// Free the reader.
void signet_reader_free(signet_reader_t* reader);

/// Return the total number of rows.
int64_t signet_reader_num_rows(const signet_reader_t* reader);

/// Return the number of row groups.
int64_t signet_reader_num_row_groups(const signet_reader_t* reader);

/// Return the schema (caller does NOT own it — valid while reader is alive).
const signet_schema_t* signet_reader_schema(const signet_reader_t* reader);

/// Return the "created_by" string. Caller must free with signet_string_free().
signet_string_t signet_reader_created_by(const signet_reader_t* reader);

/// Read a bool column. On success, *out_values is heap-allocated (new[]),
/// *out_count is set. Caller must free with signet_free_bool_array().
signet_error_t signet_reader_read_column_bool(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    uint8_t** out_values, size_t* out_count);

/// Read an int32 column.
signet_error_t signet_reader_read_column_int32(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    int32_t** out_values, size_t* out_count);

/// Read an int64 column.
signet_error_t signet_reader_read_column_int64(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    int64_t** out_values, size_t* out_count);

/// Read a float column.
signet_error_t signet_reader_read_column_float(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    float** out_values, size_t* out_count);

/// Read a double column.
signet_error_t signet_reader_read_column_double(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    double** out_values, size_t* out_count);

/// Read a string column. On success, *out_values is a heap-allocated array of
/// signet_string_t, *out_count is set. Free with signet_free_string_array().
signet_error_t signet_reader_read_column_string(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    signet_string_t** out_values, size_t* out_count);

// ---------------------------------------------------------------------------
// Array free functions
// ---------------------------------------------------------------------------

void signet_free_bool_array(uint8_t* arr);
void signet_free_int32_array(int32_t* arr);
void signet_free_int64_array(int64_t* arr);
void signet_free_float_array(float* arr);
void signet_free_double_array(double* arr);
void signet_free_string_array(signet_string_t* arr, size_t count);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // SIGNET_FORGE_FFI_H
