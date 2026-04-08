use crate::error::{check_error, Result, SignetError, ErrorCode};
use crate::options::WriterOptions;
use crate::schema::Schema;
use signet_forge_sys as ffi;
use std::ffi::CString;
use std::path::Path;
use std::ptr;

/// Streaming Parquet file writer.
///
/// Writes spec-compliant Parquet files with configurable encoding and
/// compression. Supports typed column-based writes for all 6 core types.
///
/// # Lifecycle
///
/// ```no_run
/// use signet_forge::{SchemaBuilder, ParquetWriter, WriterOptions};
///
/// let schema = SchemaBuilder::new("data").unwrap()
///     .add_int64("id")
///     .add_double("value")
///     .build().unwrap();
///
/// let mut w = ParquetWriter::open("out.parquet", &schema, None).unwrap();
/// w.write_column_int64(0, &[1, 2, 3]).unwrap();
/// w.write_column_double(1, &[1.1, 2.2, 3.3]).unwrap();
/// w.flush_row_group().unwrap();
/// w.close().unwrap();
/// ```
///
/// If `close()` is not called, the destructor will attempt a best-effort close.
pub struct ParquetWriter {
    ptr: *mut ffi::signet_writer_t,
}

// SAFETY: ParquetWriter is Send (single-owner, not thread-safe).
unsafe impl Send for ParquetWriter {}

impl ParquetWriter {
    /// Open a new Parquet file for writing.
    pub fn open(
        path: impl AsRef<Path>,
        schema: &Schema,
        options: Option<WriterOptions>,
    ) -> Result<Self> {
        let path_str = path.as_ref().to_str().ok_or_else(|| SignetError {
            code: ErrorCode::IoError,
            message: "path is not valid UTF-8".into(),
        })?;
        let c_path = CString::new(path_str).map_err(|_| SignetError {
            code: ErrorCode::IoError,
            message: "path contains null byte".into(),
        })?;

        let opts_ptr = options
            .as_ref()
            .map(|o| o.ptr as *const _)
            .unwrap_or(ptr::null());

        let mut writer_ptr: *mut ffi::signet_writer_t = ptr::null_mut();
        let err = unsafe {
            ffi::signet_writer_open(c_path.as_ptr(), schema.ptr, opts_ptr, &mut writer_ptr)
        };
        check_error(err)?;
        Ok(Self { ptr: writer_ptr })
    }

    /// Write a column of bool values.
    pub fn write_column_bool(&mut self, col_index: usize, values: &[bool]) -> Result<()> {
        let u8_values: Vec<u8> = values.iter().map(|&b| if b { 1 } else { 0 }).collect();
        let err = unsafe {
            ffi::signet_writer_write_column_bool(
                self.ptr,
                col_index,
                u8_values.as_ptr(),
                u8_values.len(),
            )
        };
        check_error(err)
    }

    /// Write a column of int32 values.
    pub fn write_column_int32(&mut self, col_index: usize, values: &[i32]) -> Result<()> {
        let err = unsafe {
            ffi::signet_writer_write_column_int32(
                self.ptr,
                col_index,
                values.as_ptr(),
                values.len(),
            )
        };
        check_error(err)
    }

    /// Write a column of int64 values.
    pub fn write_column_int64(&mut self, col_index: usize, values: &[i64]) -> Result<()> {
        let err = unsafe {
            ffi::signet_writer_write_column_int64(
                self.ptr,
                col_index,
                values.as_ptr(),
                values.len(),
            )
        };
        check_error(err)
    }

    /// Write a column of float values.
    pub fn write_column_float(&mut self, col_index: usize, values: &[f32]) -> Result<()> {
        let err = unsafe {
            ffi::signet_writer_write_column_float(
                self.ptr,
                col_index,
                values.as_ptr(),
                values.len(),
            )
        };
        check_error(err)
    }

    /// Write a column of double values.
    pub fn write_column_double(&mut self, col_index: usize, values: &[f64]) -> Result<()> {
        let err = unsafe {
            ffi::signet_writer_write_column_double(
                self.ptr,
                col_index,
                values.as_ptr(),
                values.len(),
            )
        };
        check_error(err)
    }

    /// Write a column of string values.
    pub fn write_column_string(&mut self, col_index: usize, values: &[&str]) -> Result<()> {
        let c_strings: Vec<CString> = values
            .iter()
            .map(|s| CString::new(*s).map_err(|_| SignetError {
                code: ErrorCode::InternalError,
                message: "string value contains null byte".into(),
            }))
            .collect::<std::result::Result<Vec<_>, _>>()?;
        let ptrs: Vec<*const std::os::raw::c_char> =
            c_strings.iter().map(|cs| cs.as_ptr()).collect();

        let err = unsafe {
            ffi::signet_writer_write_column_string(
                self.ptr,
                col_index,
                ptrs.as_ptr(),
                ptrs.len(),
            )
        };
        check_error(err)
    }

    /// Flush the current row group to disk.
    pub fn flush_row_group(&mut self) -> Result<()> {
        let err = unsafe { ffi::signet_writer_flush_row_group(self.ptr) };
        check_error(err)
    }

    /// Close the writer, finalizing the Parquet file.
    ///
    /// This consumes the writer. Prefer calling this explicitly over
    /// relying on `Drop`, which cannot report errors.
    pub fn close(self) -> Result<()> {
        let ptr = self.ptr;
        // Prevent Drop from double-freeing
        std::mem::forget(self);
        let err = unsafe { ffi::signet_writer_close(ptr) };
        let result = check_error(err);
        unsafe { ffi::signet_writer_free(ptr) };
        result
    }

    /// Number of rows written so far.
    pub fn rows_written(&self) -> i64 {
        unsafe { ffi::signet_writer_rows_written(self.ptr) }
    }

    /// Whether the writer is still open.
    pub fn is_open(&self) -> bool {
        unsafe { ffi::signet_writer_is_open(self.ptr) != 0 }
    }
}

impl Drop for ParquetWriter {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { ffi::signet_writer_free(self.ptr) };
        }
    }
}
