use crate::enums::{Compression, Encoding};
use signet_forge_sys as ffi;

/// Configuration options for [`ParquetWriter`](crate::ParquetWriter).
///
/// # Example
///
/// ```no_run
/// use signet_forge::{WriterOptions, Compression, Encoding};
///
/// let opts = WriterOptions::new()
///     .row_group_size(100_000)
///     .compression(Compression::Snappy)
///     .encoding(Encoding::RleDictionary);
/// ```
pub struct WriterOptions {
    pub(crate) ptr: *mut ffi::signet_writer_options_t,
}

// SAFETY: WriterOptions is Send because the C handle is not shared.
unsafe impl Send for WriterOptions {}

impl WriterOptions {
    /// Create default writer options.
    ///
    /// # Panics
    ///
    /// Panics if the C allocator returns null (out of memory).
    pub fn new() -> Self {
        let ptr = unsafe { ffi::signet_writer_options_new() };
        assert!(!ptr.is_null(), "WriterOptions allocation failed (out of memory)");
        Self { ptr }
    }

    /// Set the target number of rows per row group.
    pub fn row_group_size(self, size: i64) -> Self {
        unsafe { ffi::signet_writer_options_set_row_group_size(self.ptr, size) };
        self
    }

    /// Set the default encoding for all columns.
    pub fn encoding(self, enc: Encoding) -> Self {
        unsafe { ffi::signet_writer_options_set_encoding(self.ptr, enc as i32) };
        self
    }

    /// Set the compression codec.
    pub fn compression(self, comp: Compression) -> Self {
        unsafe { ffi::signet_writer_options_set_compression(self.ptr, comp as i32) };
        self
    }

    /// Enable or disable auto-encoding selection.
    pub fn auto_encoding(self, enabled: bool) -> Self {
        unsafe {
            ffi::signet_writer_options_set_auto_encoding(self.ptr, if enabled { 1 } else { 0 })
        };
        self
    }
}

impl Default for WriterOptions {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for WriterOptions {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { ffi::signet_writer_options_free(self.ptr) };
        }
    }
}
