//! Safe Rust bindings for Signet Forge — standalone C++20 Parquet library.
//!
//! This crate wraps the Signet Forge C FFI layer with idiomatic Rust types:
//! - [`SchemaBuilder`] / [`Schema`] for column definitions
//! - [`WriterOptions`] for encoding/compression configuration
//! - [`ParquetWriter`] for writing Parquet files
//! - [`ParquetReader`] for reading Parquet files
//! - [`Result`](error::Result) / [`SignetError`](error::SignetError) for error handling
//!
//! # Example
//!
//! ```no_run
//! use signet_forge::{SchemaBuilder, ParquetWriter, ParquetReader, WriterOptions};
//!
//! // Write
//! let schema = SchemaBuilder::new("data").unwrap()
//!     .add_int64("id")
//!     .add_double("value")
//!     .add_string("name")
//!     .build().unwrap();
//!
//! let mut writer = ParquetWriter::open("test.parquet", &schema, None).unwrap();
//! writer.write_column_int64(0, &[1, 2, 3]).unwrap();
//! writer.write_column_double(1, &[1.1, 2.2, 3.3]).unwrap();
//! writer.write_column_string(2, &["a", "b", "c"]).unwrap();
//! writer.flush_row_group().unwrap();
//! writer.close().unwrap();
//!
//! // Read
//! let mut reader = ParquetReader::open("test.parquet").unwrap();
//! assert_eq!(reader.num_rows(), 3);
//! let ids = reader.read_column_int64(0, 0).unwrap();
//! assert_eq!(ids, vec![1, 2, 3]);
//! ```

pub mod enums;
pub mod error;
pub mod options;
pub mod reader;
pub mod schema;
pub mod writer;

// Re-export primary types at crate root
pub use enums::{Compression, Encoding, LogicalType, PhysicalType};
pub use error::{ErrorCode, Result, SignetError};
pub use options::WriterOptions;
pub use reader::ParquetReader;
pub use schema::{Schema, SchemaBuilder};
pub use writer::ParquetWriter;

/// Return the Signet Forge library version string.
pub fn version() -> &'static str {
    use std::ffi::CStr;
    unsafe {
        CStr::from_ptr(signet_forge_sys::signet_version())
            .to_str()
            .unwrap_or("unknown")
    }
}
