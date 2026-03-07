//! Raw FFI bindings to the Signet Forge C API.
//!
//! This crate provides unsafe `extern "C"` declarations matching
//! `ffi/signet_forge.h`. It is not intended for direct use — prefer
//! the safe `signet-forge` wrapper crate.

#![allow(non_camel_case_types)]

use std::os::raw::c_char;

// ---------------------------------------------------------------------------
// Error codes
// ---------------------------------------------------------------------------

pub const SIGNET_OK: i32 = 0;
pub const SIGNET_IO_ERROR: i32 = 1;
pub const SIGNET_INVALID_FILE: i32 = 2;
pub const SIGNET_CORRUPT_FOOTER: i32 = 3;
pub const SIGNET_CORRUPT_PAGE: i32 = 4;
pub const SIGNET_UNSUPPORTED_ENCODING: i32 = 5;
pub const SIGNET_UNSUPPORTED_COMPRESSION: i32 = 6;
pub const SIGNET_UNSUPPORTED_TYPE: i32 = 7;
pub const SIGNET_SCHEMA_MISMATCH: i32 = 8;
pub const SIGNET_OUT_OF_RANGE: i32 = 9;
pub const SIGNET_THRIFT_DECODE_ERROR: i32 = 10;
pub const SIGNET_ENCRYPTION_ERROR: i32 = 11;
pub const SIGNET_HASH_CHAIN_BROKEN: i32 = 12;
pub const SIGNET_LICENSE_ERROR: i32 = 13;
pub const SIGNET_LICENSE_LIMIT_EXCEEDED: i32 = 14;
pub const SIGNET_INTERNAL_ERROR: i32 = 15;

// ---------------------------------------------------------------------------
// Physical types
// ---------------------------------------------------------------------------

pub const SIGNET_BOOLEAN: i32 = 0;
pub const SIGNET_INT32: i32 = 1;
pub const SIGNET_INT64: i32 = 2;
pub const SIGNET_INT96: i32 = 3;
pub const SIGNET_FLOAT: i32 = 4;
pub const SIGNET_DOUBLE: i32 = 5;
pub const SIGNET_BYTE_ARRAY: i32 = 6;
pub const SIGNET_FIXED_LEN_BYTE_ARRAY: i32 = 7;

// ---------------------------------------------------------------------------
// Logical types
// ---------------------------------------------------------------------------

pub const SIGNET_LOGICAL_NONE: i32 = 0;
pub const SIGNET_LOGICAL_STRING: i32 = 1;
pub const SIGNET_LOGICAL_ENUM: i32 = 2;
pub const SIGNET_LOGICAL_UUID: i32 = 3;
pub const SIGNET_LOGICAL_DATE: i32 = 4;
pub const SIGNET_LOGICAL_TIME_MS: i32 = 5;
pub const SIGNET_LOGICAL_TIME_US: i32 = 6;
pub const SIGNET_LOGICAL_TIME_NS: i32 = 7;
pub const SIGNET_LOGICAL_TIMESTAMP_MS: i32 = 8;
pub const SIGNET_LOGICAL_TIMESTAMP_US: i32 = 9;
pub const SIGNET_LOGICAL_TIMESTAMP_NS: i32 = 10;
pub const SIGNET_LOGICAL_DECIMAL: i32 = 11;
pub const SIGNET_LOGICAL_JSON: i32 = 12;
pub const SIGNET_LOGICAL_BSON: i32 = 13;
pub const SIGNET_LOGICAL_FLOAT16: i32 = 14;
pub const SIGNET_LOGICAL_FLOAT32_VECTOR: i32 = 100;

// ---------------------------------------------------------------------------
// Encoding types
// ---------------------------------------------------------------------------

pub const SIGNET_ENCODING_PLAIN: i32 = 0;
pub const SIGNET_ENCODING_PLAIN_DICTIONARY: i32 = 2;
pub const SIGNET_ENCODING_RLE: i32 = 3;
pub const SIGNET_ENCODING_BIT_PACKED: i32 = 4;
pub const SIGNET_ENCODING_DELTA_BINARY_PACKED: i32 = 5;
pub const SIGNET_ENCODING_DELTA_LENGTH_BYTE_ARRAY: i32 = 6;
pub const SIGNET_ENCODING_DELTA_BYTE_ARRAY: i32 = 7;
pub const SIGNET_ENCODING_RLE_DICTIONARY: i32 = 8;
pub const SIGNET_ENCODING_BYTE_STREAM_SPLIT: i32 = 9;

// ---------------------------------------------------------------------------
// Compression types
// ---------------------------------------------------------------------------

pub const SIGNET_COMPRESSION_UNCOMPRESSED: i32 = 0;
pub const SIGNET_COMPRESSION_SNAPPY: i32 = 1;
pub const SIGNET_COMPRESSION_GZIP: i32 = 2;
pub const SIGNET_COMPRESSION_LZO: i32 = 3;
pub const SIGNET_COMPRESSION_BROTLI: i32 = 4;
pub const SIGNET_COMPRESSION_LZ4: i32 = 5;
pub const SIGNET_COMPRESSION_ZSTD: i32 = 6;
pub const SIGNET_COMPRESSION_LZ4_RAW: i32 = 7;

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

/// Error returned by value from C API functions.
#[repr(C)]
#[derive(Debug)]
pub struct signet_error_t {
    pub code: i32,
    pub message: *const c_char,
}

/// Owned string returned from C API.
#[repr(C)]
#[derive(Debug)]
pub struct signet_string_t {
    pub ptr: *const c_char,
    pub len: usize,
}

// ---------------------------------------------------------------------------
// Opaque handle types
// ---------------------------------------------------------------------------

#[repr(C)]
pub struct signet_schema_builder_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct signet_schema_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct signet_writer_options_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct signet_writer_t {
    _opaque: [u8; 0],
}

#[repr(C)]
pub struct signet_reader_t {
    _opaque: [u8; 0],
}

// ---------------------------------------------------------------------------
// Extern declarations
// ---------------------------------------------------------------------------

extern "C" {
    // Version
    pub fn signet_version() -> *const c_char;

    // Error + String
    pub fn signet_error_free(err: *mut signet_error_t);
    pub fn signet_string_free(s: *mut signet_string_t);

    // SchemaBuilder
    pub fn signet_schema_builder_new(name: *const c_char) -> *mut signet_schema_builder_t;
    pub fn signet_schema_builder_add_bool(
        builder: *mut signet_schema_builder_t,
        col_name: *const c_char,
    ) -> signet_error_t;
    pub fn signet_schema_builder_add_int32(
        builder: *mut signet_schema_builder_t,
        col_name: *const c_char,
    ) -> signet_error_t;
    pub fn signet_schema_builder_add_int64(
        builder: *mut signet_schema_builder_t,
        col_name: *const c_char,
    ) -> signet_error_t;
    pub fn signet_schema_builder_add_float(
        builder: *mut signet_schema_builder_t,
        col_name: *const c_char,
    ) -> signet_error_t;
    pub fn signet_schema_builder_add_double(
        builder: *mut signet_schema_builder_t,
        col_name: *const c_char,
    ) -> signet_error_t;
    pub fn signet_schema_builder_add_string(
        builder: *mut signet_schema_builder_t,
        col_name: *const c_char,
    ) -> signet_error_t;
    pub fn signet_schema_builder_build(
        builder: *mut signet_schema_builder_t,
        out_schema: *mut *mut signet_schema_t,
    ) -> signet_error_t;
    pub fn signet_schema_builder_free(builder: *mut signet_schema_builder_t);

    // Schema
    pub fn signet_schema_free(schema: *mut signet_schema_t);
    pub fn signet_schema_num_columns(schema: *const signet_schema_t) -> usize;
    pub fn signet_schema_column_name(
        schema: *const signet_schema_t,
        index: usize,
    ) -> signet_string_t;
    pub fn signet_schema_column_physical_type(
        schema: *const signet_schema_t,
        index: usize,
    ) -> i32;

    // WriterOptions
    pub fn signet_writer_options_new() -> *mut signet_writer_options_t;
    pub fn signet_writer_options_free(opts: *mut signet_writer_options_t);
    pub fn signet_writer_options_set_row_group_size(
        opts: *mut signet_writer_options_t,
        size: i64,
    );
    pub fn signet_writer_options_set_encoding(opts: *mut signet_writer_options_t, encoding: i32);
    pub fn signet_writer_options_set_compression(
        opts: *mut signet_writer_options_t,
        compression: i32,
    );
    pub fn signet_writer_options_set_auto_encoding(
        opts: *mut signet_writer_options_t,
        enabled: u8,
    );

    // ParquetWriter
    pub fn signet_writer_open(
        path: *const c_char,
        schema: *const signet_schema_t,
        options: *const signet_writer_options_t,
        out_writer: *mut *mut signet_writer_t,
    ) -> signet_error_t;
    pub fn signet_writer_write_column_bool(
        writer: *mut signet_writer_t,
        col_index: usize,
        values: *const u8,
        count: usize,
    ) -> signet_error_t;
    pub fn signet_writer_write_column_int32(
        writer: *mut signet_writer_t,
        col_index: usize,
        values: *const i32,
        count: usize,
    ) -> signet_error_t;
    pub fn signet_writer_write_column_int64(
        writer: *mut signet_writer_t,
        col_index: usize,
        values: *const i64,
        count: usize,
    ) -> signet_error_t;
    pub fn signet_writer_write_column_float(
        writer: *mut signet_writer_t,
        col_index: usize,
        values: *const f32,
        count: usize,
    ) -> signet_error_t;
    pub fn signet_writer_write_column_double(
        writer: *mut signet_writer_t,
        col_index: usize,
        values: *const f64,
        count: usize,
    ) -> signet_error_t;
    pub fn signet_writer_write_column_string(
        writer: *mut signet_writer_t,
        col_index: usize,
        values: *const *const c_char,
        count: usize,
    ) -> signet_error_t;
    pub fn signet_writer_write_row(
        writer: *mut signet_writer_t,
        values: *const *const c_char,
        num_values: usize,
    ) -> signet_error_t;
    pub fn signet_writer_flush_row_group(writer: *mut signet_writer_t) -> signet_error_t;
    pub fn signet_writer_close(writer: *mut signet_writer_t) -> signet_error_t;
    pub fn signet_writer_rows_written(writer: *const signet_writer_t) -> i64;
    pub fn signet_writer_is_open(writer: *const signet_writer_t) -> u8;
    pub fn signet_writer_free(writer: *mut signet_writer_t);

    // ParquetReader
    pub fn signet_reader_open(
        path: *const c_char,
        out_reader: *mut *mut signet_reader_t,
    ) -> signet_error_t;
    pub fn signet_reader_free(reader: *mut signet_reader_t);
    pub fn signet_reader_num_rows(reader: *const signet_reader_t) -> i64;
    pub fn signet_reader_num_row_groups(reader: *const signet_reader_t) -> i64;
    pub fn signet_reader_schema(reader: *const signet_reader_t) -> *const signet_schema_t;
    pub fn signet_reader_created_by(reader: *const signet_reader_t) -> signet_string_t;

    pub fn signet_reader_read_column_bool(
        reader: *mut signet_reader_t,
        row_group: usize,
        col_index: usize,
        out_values: *mut *mut u8,
        out_count: *mut usize,
    ) -> signet_error_t;
    pub fn signet_reader_read_column_int32(
        reader: *mut signet_reader_t,
        row_group: usize,
        col_index: usize,
        out_values: *mut *mut i32,
        out_count: *mut usize,
    ) -> signet_error_t;
    pub fn signet_reader_read_column_int64(
        reader: *mut signet_reader_t,
        row_group: usize,
        col_index: usize,
        out_values: *mut *mut i64,
        out_count: *mut usize,
    ) -> signet_error_t;
    pub fn signet_reader_read_column_float(
        reader: *mut signet_reader_t,
        row_group: usize,
        col_index: usize,
        out_values: *mut *mut f32,
        out_count: *mut usize,
    ) -> signet_error_t;
    pub fn signet_reader_read_column_double(
        reader: *mut signet_reader_t,
        row_group: usize,
        col_index: usize,
        out_values: *mut *mut f64,
        out_count: *mut usize,
    ) -> signet_error_t;
    pub fn signet_reader_read_column_string(
        reader: *mut signet_reader_t,
        row_group: usize,
        col_index: usize,
        out_values: *mut *mut signet_string_t,
        out_count: *mut usize,
    ) -> signet_error_t;

    // Array free
    pub fn signet_free_bool_array(arr: *mut u8);
    pub fn signet_free_int32_array(arr: *mut i32);
    pub fn signet_free_int64_array(arr: *mut i64);
    pub fn signet_free_float_array(arr: *mut f32);
    pub fn signet_free_double_array(arr: *mut f64);
    pub fn signet_free_string_array(arr: *mut signet_string_t, count: usize);
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::CStr;

    #[test]
    fn test_version() {
        unsafe {
            let v = signet_version();
            let s = CStr::from_ptr(v).to_str().unwrap();
            assert_eq!(s, "0.1.0");
        }
    }
}
