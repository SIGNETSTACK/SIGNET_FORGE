use crate::enums::{LogicalType, PhysicalType};
use crate::error::{check_error, Result};
use signet_forge_sys as ffi;
use std::ffi::{CStr, CString};
use std::ptr;

/// Immutable Parquet schema (wraps the C opaque handle).
///
/// Schemas are created via [`SchemaBuilder`] and consumed by
/// [`ParquetWriter::open`](crate::ParquetWriter::open).
pub struct Schema {
    pub(crate) ptr: *mut ffi::signet_schema_t,
    /// When true, Drop will free the pointer. When false, the pointer is
    /// borrowed from a reader and must not be freed.
    pub(crate) owned: bool,
}

// SAFETY: Schema is Send because the C handle is not shared.
unsafe impl Send for Schema {}

impl Schema {
    /// Number of columns in this schema.
    pub fn num_columns(&self) -> usize {
        unsafe { ffi::signet_schema_num_columns(self.ptr) }
    }

    /// Get the name of a column by index.
    pub fn column_name(&self, index: usize) -> Option<String> {
        if index >= self.num_columns() {
            return None;
        }
        unsafe {
            let mut s = ffi::signet_schema_column_name(self.ptr, index);
            if s.ptr.is_null() {
                return None;
            }
            let name = CStr::from_ptr(s.ptr).to_string_lossy().into_owned();
            ffi::signet_string_free(&mut s);
            Some(name)
        }
    }

    /// Get the physical type of a column by index.
    pub fn column_physical_type(&self, index: usize) -> Option<PhysicalType> {
        if index >= self.num_columns() {
            return None;
        }
        let raw = unsafe { ffi::signet_schema_column_physical_type(self.ptr, index) };
        PhysicalType::from_raw(raw)
    }

    /// Get the logical type of a column by index.
    pub fn column_logical_type(&self, index: usize) -> Option<LogicalType> {
        if index >= self.num_columns() {
            return None;
        }
        let raw = unsafe { ffi::signet_schema_column_logical_type(self.ptr, index) };
        LogicalType::from_raw(raw)
    }
}

impl Drop for Schema {
    fn drop(&mut self) {
        if self.owned && !self.ptr.is_null() {
            unsafe { ffi::signet_schema_free(self.ptr) };
        }
    }
}

/// Fluent builder for constructing a [`Schema`].
///
/// # Example
///
/// ```no_run
/// use signet_forge::SchemaBuilder;
///
/// let schema = SchemaBuilder::new("trades").unwrap()
///     .add_int64("timestamp")
///     .add_double("price")
///     .add_string("symbol")
///     .build()
///     .unwrap();
/// ```
pub struct SchemaBuilder {
    ptr: *mut ffi::signet_schema_builder_t,
}

// SAFETY: SchemaBuilder is Send because the C handle is not shared.
unsafe impl Send for SchemaBuilder {}

impl SchemaBuilder {
    /// Create a new builder with the given root schema name.
    ///
    /// Returns an error if the name contains null bytes or if allocation fails.
    pub fn new(name: &str) -> Result<Self> {
        let c_name = CString::new(name).map_err(|_| crate::error::SignetError {
            code: crate::error::ErrorCode::InternalError,
            message: "schema name contains null byte".into(),
        })?;
        let ptr = unsafe { ffi::signet_schema_builder_new(c_name.as_ptr()) };
        if ptr.is_null() {
            return Err(crate::error::SignetError {
                code: crate::error::ErrorCode::InternalError,
                message: "allocation failed for SchemaBuilder".into(),
            });
        }
        Ok(Self { ptr })
    }

    /// Add a BOOLEAN column.
    pub fn add_bool(self, name: &str) -> Self {
        let c = CString::new(name).expect("column name must not contain null bytes");
        let err = unsafe { ffi::signet_schema_builder_add_bool(self.ptr, c.as_ptr()) };
        let _ = check_error(err);
        self
    }

    /// Add an INT32 column.
    pub fn add_int32(self, name: &str) -> Self {
        let c = CString::new(name).expect("column name must not contain null bytes");
        let err = unsafe { ffi::signet_schema_builder_add_int32(self.ptr, c.as_ptr()) };
        let _ = check_error(err);
        self
    }

    /// Add an INT64 column.
    pub fn add_int64(self, name: &str) -> Self {
        let c = CString::new(name).expect("column name must not contain null bytes");
        let err = unsafe { ffi::signet_schema_builder_add_int64(self.ptr, c.as_ptr()) };
        let _ = check_error(err);
        self
    }

    /// Add a FLOAT column.
    pub fn add_float(self, name: &str) -> Self {
        let c = CString::new(name).expect("column name must not contain null bytes");
        let err = unsafe { ffi::signet_schema_builder_add_float(self.ptr, c.as_ptr()) };
        let _ = check_error(err);
        self
    }

    /// Add a DOUBLE column.
    pub fn add_double(self, name: &str) -> Self {
        let c = CString::new(name).expect("column name must not contain null bytes");
        let err = unsafe { ffi::signet_schema_builder_add_double(self.ptr, c.as_ptr()) };
        let _ = check_error(err);
        self
    }

    /// Add a STRING column (BYTE_ARRAY physical type).
    pub fn add_string(self, name: &str) -> Self {
        let c = CString::new(name).expect("column name must not contain null bytes");
        let err = unsafe { ffi::signet_schema_builder_add_string(self.ptr, c.as_ptr()) };
        let _ = check_error(err);
        self
    }

    /// Build the schema, consuming the builder.
    ///
    /// On success, the builder is freed by the C API. On failure, Drop
    /// will free the builder via `signet_schema_builder_free`.
    pub fn build(self) -> Result<Schema> {
        let mut schema_ptr: *mut ffi::signet_schema_t = ptr::null_mut();
        let err = unsafe { ffi::signet_schema_builder_build(self.ptr, &mut schema_ptr) };
        if err.code == 0 {
            // Success — builder was freed by C API, prevent double-free
            std::mem::forget(self);
            Ok(Schema {
                ptr: schema_ptr,
                owned: true,
            })
        } else {
            // Failure — builder NOT freed by C API, let Drop handle it
            check_error(err)?;
            unreachable!()
        }
    }
}

impl Drop for SchemaBuilder {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { ffi::signet_schema_builder_free(self.ptr) };
        }
    }
}
