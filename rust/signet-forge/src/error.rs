use signet_forge_sys as ffi;
use std::ffi::CStr;
use std::fmt;

/// Error codes from Signet Forge.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum ErrorCode {
    IoError = ffi::SIGNET_IO_ERROR,
    InvalidFile = ffi::SIGNET_INVALID_FILE,
    CorruptFooter = ffi::SIGNET_CORRUPT_FOOTER,
    CorruptPage = ffi::SIGNET_CORRUPT_PAGE,
    UnsupportedEncoding = ffi::SIGNET_UNSUPPORTED_ENCODING,
    UnsupportedCompression = ffi::SIGNET_UNSUPPORTED_COMPRESSION,
    UnsupportedType = ffi::SIGNET_UNSUPPORTED_TYPE,
    SchemaMismatch = ffi::SIGNET_SCHEMA_MISMATCH,
    OutOfRange = ffi::SIGNET_OUT_OF_RANGE,
    ThriftDecodeError = ffi::SIGNET_THRIFT_DECODE_ERROR,
    EncryptionError = ffi::SIGNET_ENCRYPTION_ERROR,
    HashChainBroken = ffi::SIGNET_HASH_CHAIN_BROKEN,
    LicenseError = ffi::SIGNET_LICENSE_ERROR,
    LicenseLimitExceeded = ffi::SIGNET_LICENSE_LIMIT_EXCEEDED,
    InternalError = ffi::SIGNET_INTERNAL_ERROR,
}

impl ErrorCode {
    pub(crate) fn from_raw(code: i32) -> Self {
        match code {
            ffi::SIGNET_IO_ERROR => Self::IoError,
            ffi::SIGNET_INVALID_FILE => Self::InvalidFile,
            ffi::SIGNET_CORRUPT_FOOTER => Self::CorruptFooter,
            ffi::SIGNET_CORRUPT_PAGE => Self::CorruptPage,
            ffi::SIGNET_UNSUPPORTED_ENCODING => Self::UnsupportedEncoding,
            ffi::SIGNET_UNSUPPORTED_COMPRESSION => Self::UnsupportedCompression,
            ffi::SIGNET_UNSUPPORTED_TYPE => Self::UnsupportedType,
            ffi::SIGNET_SCHEMA_MISMATCH => Self::SchemaMismatch,
            ffi::SIGNET_OUT_OF_RANGE => Self::OutOfRange,
            ffi::SIGNET_THRIFT_DECODE_ERROR => Self::ThriftDecodeError,
            ffi::SIGNET_ENCRYPTION_ERROR => Self::EncryptionError,
            ffi::SIGNET_HASH_CHAIN_BROKEN => Self::HashChainBroken,
            ffi::SIGNET_LICENSE_ERROR => Self::LicenseError,
            ffi::SIGNET_LICENSE_LIMIT_EXCEEDED => Self::LicenseLimitExceeded,
            _ => Self::InternalError,
        }
    }
}

impl fmt::Display for ErrorCode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::IoError => write!(f, "IO error"),
            Self::InvalidFile => write!(f, "invalid file"),
            Self::CorruptFooter => write!(f, "corrupt footer"),
            Self::CorruptPage => write!(f, "corrupt page"),
            Self::UnsupportedEncoding => write!(f, "unsupported encoding"),
            Self::UnsupportedCompression => write!(f, "unsupported compression"),
            Self::UnsupportedType => write!(f, "unsupported type"),
            Self::SchemaMismatch => write!(f, "schema mismatch"),
            Self::OutOfRange => write!(f, "out of range"),
            Self::ThriftDecodeError => write!(f, "thrift decode error"),
            Self::EncryptionError => write!(f, "encryption error"),
            Self::HashChainBroken => write!(f, "hash chain broken"),
            Self::LicenseError => write!(f, "license error"),
            Self::LicenseLimitExceeded => write!(f, "license limit exceeded"),
            Self::InternalError => write!(f, "internal error"),
        }
    }
}

/// Error type for Signet Forge operations.
#[derive(Debug, Clone)]
pub struct SignetError {
    pub code: ErrorCode,
    pub message: String,
}

impl fmt::Display for SignetError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "signet error ({}): {}", self.code, self.message)
    }
}

impl std::error::Error for SignetError {}

/// Convenience type alias.
pub type Result<T> = std::result::Result<T, SignetError>;

/// Convert a raw FFI error into a Rust Result, freeing the C error message.
pub(crate) fn check_error(mut err: ffi::signet_error_t) -> Result<()> {
    if err.code == ffi::SIGNET_OK {
        return Ok(());
    }
    let message = if !err.message.is_null() {
        let s = unsafe { CStr::from_ptr(err.message) }
            .to_string_lossy()
            .into_owned();
        unsafe { ffi::signet_error_free(&mut err) };
        s
    } else {
        String::new()
    };
    Err(SignetError {
        code: ErrorCode::from_raw(err.code),
        message,
    })
}
