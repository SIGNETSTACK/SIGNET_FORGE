use signet_forge_sys as ffi;

/// Parquet physical (storage) types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum PhysicalType {
    Boolean = ffi::SIGNET_BOOLEAN,
    Int32 = ffi::SIGNET_INT32,
    Int64 = ffi::SIGNET_INT64,
    Int96 = ffi::SIGNET_INT96,
    Float = ffi::SIGNET_FLOAT,
    Double = ffi::SIGNET_DOUBLE,
    ByteArray = ffi::SIGNET_BYTE_ARRAY,
    FixedLenByteArray = ffi::SIGNET_FIXED_LEN_BYTE_ARRAY,
}

impl PhysicalType {
    pub fn from_raw(v: i32) -> Option<Self> {
        match v {
            ffi::SIGNET_BOOLEAN => Some(Self::Boolean),
            ffi::SIGNET_INT32 => Some(Self::Int32),
            ffi::SIGNET_INT64 => Some(Self::Int64),
            ffi::SIGNET_INT96 => Some(Self::Int96),
            ffi::SIGNET_FLOAT => Some(Self::Float),
            ffi::SIGNET_DOUBLE => Some(Self::Double),
            ffi::SIGNET_BYTE_ARRAY => Some(Self::ByteArray),
            ffi::SIGNET_FIXED_LEN_BYTE_ARRAY => Some(Self::FixedLenByteArray),
            _ => None,
        }
    }
}

/// Parquet logical types (semantic annotations on top of physical types).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum LogicalType {
    None = ffi::SIGNET_LOGICAL_NONE,
    String = ffi::SIGNET_LOGICAL_STRING,
    Enum = ffi::SIGNET_LOGICAL_ENUM,
    Uuid = ffi::SIGNET_LOGICAL_UUID,
    Date = ffi::SIGNET_LOGICAL_DATE,
    TimeMs = ffi::SIGNET_LOGICAL_TIME_MS,
    TimeUs = ffi::SIGNET_LOGICAL_TIME_US,
    TimeNs = ffi::SIGNET_LOGICAL_TIME_NS,
    TimestampMs = ffi::SIGNET_LOGICAL_TIMESTAMP_MS,
    TimestampUs = ffi::SIGNET_LOGICAL_TIMESTAMP_US,
    TimestampNs = ffi::SIGNET_LOGICAL_TIMESTAMP_NS,
    Decimal = ffi::SIGNET_LOGICAL_DECIMAL,
    Json = ffi::SIGNET_LOGICAL_JSON,
    Bson = ffi::SIGNET_LOGICAL_BSON,
    Float16 = ffi::SIGNET_LOGICAL_FLOAT16,
    Float32Vector = ffi::SIGNET_LOGICAL_FLOAT32_VECTOR,
}

impl LogicalType {
    pub fn from_raw(v: i32) -> Option<Self> {
        match v {
            ffi::SIGNET_LOGICAL_NONE => Some(Self::None),
            ffi::SIGNET_LOGICAL_STRING => Some(Self::String),
            ffi::SIGNET_LOGICAL_ENUM => Some(Self::Enum),
            ffi::SIGNET_LOGICAL_UUID => Some(Self::Uuid),
            ffi::SIGNET_LOGICAL_DATE => Some(Self::Date),
            ffi::SIGNET_LOGICAL_TIME_MS => Some(Self::TimeMs),
            ffi::SIGNET_LOGICAL_TIME_US => Some(Self::TimeUs),
            ffi::SIGNET_LOGICAL_TIME_NS => Some(Self::TimeNs),
            ffi::SIGNET_LOGICAL_TIMESTAMP_MS => Some(Self::TimestampMs),
            ffi::SIGNET_LOGICAL_TIMESTAMP_US => Some(Self::TimestampUs),
            ffi::SIGNET_LOGICAL_TIMESTAMP_NS => Some(Self::TimestampNs),
            ffi::SIGNET_LOGICAL_DECIMAL => Some(Self::Decimal),
            ffi::SIGNET_LOGICAL_JSON => Some(Self::Json),
            ffi::SIGNET_LOGICAL_BSON => Some(Self::Bson),
            ffi::SIGNET_LOGICAL_FLOAT16 => Some(Self::Float16),
            ffi::SIGNET_LOGICAL_FLOAT32_VECTOR => Some(Self::Float32Vector),
            _ => None,
        }
    }
}

/// Parquet encoding types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Encoding {
    Plain = ffi::SIGNET_ENCODING_PLAIN,
    PlainDictionary = ffi::SIGNET_ENCODING_PLAIN_DICTIONARY,
    Rle = ffi::SIGNET_ENCODING_RLE,
    BitPacked = ffi::SIGNET_ENCODING_BIT_PACKED,
    DeltaBinaryPacked = ffi::SIGNET_ENCODING_DELTA_BINARY_PACKED,
    DeltaLengthByteArray = ffi::SIGNET_ENCODING_DELTA_LENGTH_BYTE_ARRAY,
    DeltaByteArray = ffi::SIGNET_ENCODING_DELTA_BYTE_ARRAY,
    RleDictionary = ffi::SIGNET_ENCODING_RLE_DICTIONARY,
    ByteStreamSplit = ffi::SIGNET_ENCODING_BYTE_STREAM_SPLIT,
}

/// Parquet compression codecs.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum Compression {
    Uncompressed = ffi::SIGNET_COMPRESSION_UNCOMPRESSED,
    Snappy = ffi::SIGNET_COMPRESSION_SNAPPY,
    Gzip = ffi::SIGNET_COMPRESSION_GZIP,
    Lzo = ffi::SIGNET_COMPRESSION_LZO,
    Brotli = ffi::SIGNET_COMPRESSION_BROTLI,
    Lz4 = ffi::SIGNET_COMPRESSION_LZ4,
    Zstd = ffi::SIGNET_COMPRESSION_ZSTD,
    Lz4Raw = ffi::SIGNET_COMPRESSION_LZ4_RAW,
}
