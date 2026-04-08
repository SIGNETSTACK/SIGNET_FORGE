use crate::error::{check_error, Result, SignetError, ErrorCode};
use crate::schema::SchemaRef;
use signet_forge_sys as ffi;
use std::ffi::{CStr, CString};
use std::path::Path;
use std::ptr;

/// Parquet file reader with typed column access.
///
/// Opens a Parquet file, validates the format, and provides typed reads
/// for all 6 core types (bool, i32, i64, f32, f64, String).
///
/// # Example
///
/// ```no_run
/// use signet_forge::ParquetReader;
///
/// let mut reader = ParquetReader::open("data.parquet").unwrap();
/// let ids = reader.read_column_int64(0, 0).unwrap();
/// let prices = reader.read_column_double(0, 1).unwrap();
/// ```
pub struct ParquetReader {
    ptr: *mut ffi::signet_reader_t,
}

// SAFETY: ParquetReader is Send (single-owner, not thread-safe).
unsafe impl Send for ParquetReader {}

impl ParquetReader {
    /// Open a Parquet file for reading.
    pub fn open(path: impl AsRef<Path>) -> Result<Self> {
        let path_str = path.as_ref().to_str().ok_or_else(|| SignetError {
            code: ErrorCode::IoError,
            message: "path is not valid UTF-8".into(),
        })?;
        let c_path = CString::new(path_str).map_err(|_| SignetError {
            code: ErrorCode::IoError,
            message: "path contains null byte".into(),
        })?;

        let mut reader_ptr: *mut ffi::signet_reader_t = ptr::null_mut();
        let err = unsafe { ffi::signet_reader_open(c_path.as_ptr(), &mut reader_ptr) };
        check_error(err)?;
        Ok(Self { ptr: reader_ptr })
    }

    /// Total number of rows across all row groups.
    pub fn num_rows(&self) -> i64 {
        unsafe { ffi::signet_reader_num_rows(self.ptr) }
    }

    /// Number of row groups in the file.
    pub fn num_row_groups(&self) -> i64 {
        unsafe { ffi::signet_reader_num_row_groups(self.ptr) }
    }

    /// Get the file's schema.
    ///
    /// Returns a [`SchemaRef`] whose lifetime is tied to `&self`.  The compiler
    /// prevents the returned value from outliving this reader, eliminating the
    /// use-after-free that an unconstrained `Schema` copy would permit.
    pub fn schema(&self) -> SchemaRef<'_> {
        let ptr = unsafe { ffi::signet_reader_schema(self.ptr) };
        SchemaRef {
            ptr: ptr as *mut _,
            _borrow: std::marker::PhantomData,
        }
    }

    /// Get the "created_by" string from the file footer.
    pub fn created_by(&self) -> String {
        unsafe {
            let mut s = ffi::signet_reader_created_by(self.ptr);
            if s.ptr.is_null() {
                return String::new();
            }
            let result = CStr::from_ptr(s.ptr).to_string_lossy().into_owned();
            ffi::signet_string_free(&mut s);
            result
        }
    }

    /// Read a bool column from a specific row group.
    pub fn read_column_bool(
        &mut self,
        row_group: usize,
        col_index: usize,
    ) -> Result<Vec<bool>> {
        let mut values_ptr: *mut u8 = ptr::null_mut();
        let mut count: usize = 0;
        let err = unsafe {
            ffi::signet_reader_read_column_bool(
                self.ptr,
                row_group,
                col_index,
                &mut values_ptr,
                &mut count,
            )
        };
        check_error(err)?;
        let result: Vec<bool> = unsafe {
            std::slice::from_raw_parts(values_ptr, count)
                .iter()
                .map(|&v| v != 0)
                .collect()
        };
        unsafe { ffi::signet_free_bool_array(values_ptr) };
        Ok(result)
    }

    /// Read an int32 column from a specific row group.
    pub fn read_column_int32(
        &mut self,
        row_group: usize,
        col_index: usize,
    ) -> Result<Vec<i32>> {
        let mut values_ptr: *mut i32 = ptr::null_mut();
        let mut count: usize = 0;
        let err = unsafe {
            ffi::signet_reader_read_column_int32(
                self.ptr,
                row_group,
                col_index,
                &mut values_ptr,
                &mut count,
            )
        };
        check_error(err)?;
        let result = unsafe { std::slice::from_raw_parts(values_ptr, count).to_vec() };
        unsafe { ffi::signet_free_int32_array(values_ptr) };
        Ok(result)
    }

    /// Read an int64 column from a specific row group.
    pub fn read_column_int64(
        &mut self,
        row_group: usize,
        col_index: usize,
    ) -> Result<Vec<i64>> {
        let mut values_ptr: *mut i64 = ptr::null_mut();
        let mut count: usize = 0;
        let err = unsafe {
            ffi::signet_reader_read_column_int64(
                self.ptr,
                row_group,
                col_index,
                &mut values_ptr,
                &mut count,
            )
        };
        check_error(err)?;
        let result = unsafe { std::slice::from_raw_parts(values_ptr, count).to_vec() };
        unsafe { ffi::signet_free_int64_array(values_ptr) };
        Ok(result)
    }

    /// Read a float column from a specific row group.
    pub fn read_column_float(
        &mut self,
        row_group: usize,
        col_index: usize,
    ) -> Result<Vec<f32>> {
        let mut values_ptr: *mut f32 = ptr::null_mut();
        let mut count: usize = 0;
        let err = unsafe {
            ffi::signet_reader_read_column_float(
                self.ptr,
                row_group,
                col_index,
                &mut values_ptr,
                &mut count,
            )
        };
        check_error(err)?;
        let result = unsafe { std::slice::from_raw_parts(values_ptr, count).to_vec() };
        unsafe { ffi::signet_free_float_array(values_ptr) };
        Ok(result)
    }

    /// Read a double column from a specific row group.
    pub fn read_column_double(
        &mut self,
        row_group: usize,
        col_index: usize,
    ) -> Result<Vec<f64>> {
        let mut values_ptr: *mut f64 = ptr::null_mut();
        let mut count: usize = 0;
        let err = unsafe {
            ffi::signet_reader_read_column_double(
                self.ptr,
                row_group,
                col_index,
                &mut values_ptr,
                &mut count,
            )
        };
        check_error(err)?;
        let result = unsafe { std::slice::from_raw_parts(values_ptr, count).to_vec() };
        unsafe { ffi::signet_free_double_array(values_ptr) };
        Ok(result)
    }

    /// Read a string column from a specific row group.
    pub fn read_column_string(
        &mut self,
        row_group: usize,
        col_index: usize,
    ) -> Result<Vec<String>> {
        let mut values_ptr: *mut ffi::signet_string_t = ptr::null_mut();
        let mut count: usize = 0;
        let err = unsafe {
            ffi::signet_reader_read_column_string(
                self.ptr,
                row_group,
                col_index,
                &mut values_ptr,
                &mut count,
            )
        };
        check_error(err)?;

        let result: Vec<String> = unsafe {
            let slice = std::slice::from_raw_parts(values_ptr, count);
            slice
                .iter()
                .map(|s| {
                    if s.ptr.is_null() {
                        String::new()
                    } else {
                        CStr::from_ptr(s.ptr).to_string_lossy().into_owned()
                    }
                })
                .collect()
        };
        unsafe { ffi::signet_free_string_array(values_ptr, count) };
        Ok(result)
    }
}

impl Drop for ParquetReader {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { ffi::signet_reader_free(self.ptr) };
        }
    }
}
