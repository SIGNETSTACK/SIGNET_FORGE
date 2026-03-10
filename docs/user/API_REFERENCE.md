# API Reference

All public APIs live in the `signet::forge` namespace. Single include: `#include "signet/forge.hpp"`.

---

## Table of Contents

1. [Error Handling](#error-handling)
2. [Core — Schema](#core--schema)
3. [Core — ParquetWriter](#core--parquetwriter)
4. [Core — ParquetReader](#core--parquetreader)
5. [Encodings](#encodings)
6. [Compression](#compression)
7. [Encryption — PME](#encryption--pme)
8. [Post-Quantum Cryptography](#post-quantum-cryptography)
9. [Bloom Filters](#bloom-filters)
10. [AI Vector Columns](#ai-vector-columns)
11. [Tensor Bridge](#tensor-bridge)
12. [Streaming WAL](#streaming-wal)
13. [Feature Store](#feature-store)
14. [AI Audit Chain](#ai-audit-chain)
15. [Decision Log (BSL 1.1)](#decision-log-bsl-11)
16. [Inference Log (BSL 1.1)](#inference-log-bsl-11)
17. [StreamingSink](#streamingsink)
18. [MPMC Event Bus](#mpmc-event-bus)
19. [Compliance Reporters (BSL 1.1)](#compliance-reporters-bsl-11)

---

## Error Handling

**Header:** `include/signet/error.hpp`

### `ErrorCode` enum

```cpp
enum class ErrorCode {
    OK = 0,
    IO_ERROR,
    INVALID_FILE,
    CORRUPT_FOOTER,
    CORRUPT_PAGE,
    UNSUPPORTED_ENCODING,
    UNSUPPORTED_COMPRESSION,
    UNSUPPORTED_TYPE,
    SCHEMA_MISMATCH,
    OUT_OF_RANGE,
    THRIFT_DECODE_ERROR,
    ENCRYPTION_ERROR,
    HASH_CHAIN_BROKEN,
    INTERNAL_ERROR
};
```

### `Error` struct

```cpp
struct Error {
    ErrorCode   code;
    std::string message;

    bool ok() const;  // true if code == OK
};
```

### `expected<T>` template

All factory functions and write operations return `expected<T>`. It holds either a value of type `T` or an `Error`.

```cpp
// Check success
auto result = ParquetWriter::open("file.parquet", schema);
if (!result.has_value()) {
    std::cerr << result.error().message << "\n";
}

// Dereference after checking
auto& writer = *result;        // or result.value()
auto* writer_ptr = result.operator->(); // for member access

// expected<void> — for write operations
auto r = writer.write_column<double>(0, data.data(), data.size());
if (!r) { /* r.error().code, r.error().message */ }
```

**Key rule:** Never dereference an `expected` without checking `has_value()` first. Doing so triggers `assert()` and aborts in debug builds.

---

## Core — Schema

**Header:** `include/signet/schema.hpp`

### `Column<T>` template

```cpp
template <typename T>
struct Column {
    std::string name;
    LogicalType logical_type = LogicalType::NONE;

    Column(std::string name);
    Column(std::string name, LogicalType lt);
};
```

Type mapping: `bool` → `BOOLEAN`, `int32_t` → `INT32`, `int64_t` → `INT64`, `float` → `FLOAT`, `double` → `DOUBLE`, `std::string` → `BYTE_ARRAY/UTF8`.

### `Schema::build()`

```cpp
template <typename... Cols>
static Schema build(std::string name, Cols&&... cols);
```

Creates a schema from typed `Column<T>` descriptors.

```cpp
auto schema = Schema::build("tick_data",
    Column<int64_t>("timestamp_ns"),
    Column<double>("price"),
    Column<std::string>("symbol")
);
```

### `Schema::builder()`

```cpp
static SchemaBuilder builder(std::string name);
```

Returns a fluent `SchemaBuilder` for step-by-step column addition.

```cpp
auto schema = Schema::builder("trades")
    .column<int64_t>("timestamp_ns")
    .column<double>("price")
    .column<std::string>("symbol", LogicalType::STRING)
    .optional_column<double>("fee")  // nullable
    .build();
```

### `Schema` accessors

```cpp
const std::string& name() const;
size_t num_columns() const;
const ColumnDescriptor& column(size_t index) const;
const std::vector<ColumnDescriptor>& columns() const;
std::optional<size_t> find_column(const std::string& name) const;
bool operator==(const Schema& other) const;
```

### `ColumnDescriptor` struct

```cpp
struct ColumnDescriptor {
    std::string  name;
    PhysicalType physical_type  = PhysicalType::BYTE_ARRAY;
    LogicalType  logical_type   = LogicalType::NONE;
    Repetition   repetition     = Repetition::REQUIRED;
    int32_t      type_length    = -1;   // For FIXED_LEN_BYTE_ARRAY
    int32_t      precision      = -1;   // For DECIMAL
    int32_t      scale          = -1;   // For DECIMAL
};
```

---

## Core — ParquetWriter

**Header:** `include/signet/writer.hpp`

### `WriterOptions` struct

```cpp
struct WriterOptions {
    int64_t     row_group_size     = 65536;
    std::string created_by         = "signet-forge/...";
    std::vector<thrift::KeyValue> file_metadata;  // Custom key-value pairs

    Encoding    default_encoding   = Encoding::PLAIN;
    Compression compression        = Compression::UNCOMPRESSED;
    std::unordered_map<std::string, Encoding> column_encodings;  // Per-column override
    bool        auto_encoding      = false;
    bool        auto_compression   = false;

    bool        enable_bloom_filter = false;
    double      bloom_filter_fpr    = 0.01;
    std::unordered_set<std::string> bloom_filter_columns;  // Empty = all columns

    std::optional<crypto::EncryptionConfig> encryption;
};
```

### `ParquetWriter::open()`

```cpp
[[nodiscard]] static expected<ParquetWriter> open(
    const std::filesystem::path& path,
    const Schema& schema,
    const WriterOptions& options = WriterOptions{});
```

Opens a new Parquet file for writing. Writes the PAR1 magic immediately. Creates parent directories if they do not exist.

**Errors:** `IO_ERROR` if the file cannot be created.

### `ParquetWriter::write_column()`

```cpp
template <typename T>
[[nodiscard]] expected<void> write_column(size_t col_index,
                                          const T* values,
                                          size_t count);

// String overload:
[[nodiscard]] expected<void> write_column(size_t col_index,
                                          const std::string* values,
                                          size_t count);
```

Appends typed values to the in-memory buffer for a column. Does not write to disk until `flush_row_group()` is called.

**Errors:** `IO_ERROR` if writer is closed; `OUT_OF_RANGE` if col_index exceeds schema column count.

### `ParquetWriter::write_row()`

```cpp
[[nodiscard]] expected<void> write_row(const std::vector<std::string>& values);
```

Row-based API. Values are parsed to the column's physical type at flush time. Automatically calls `flush_row_group()` when `row_group_size` is reached.

**Errors:** `SCHEMA_MISMATCH` if values.size() != schema column count.

### `ParquetWriter::flush_row_group()`

```cpp
[[nodiscard]] expected<void> flush_row_group();
```

Encodes, optionally compresses, optionally encrypts, and writes the current row group to disk. Resets internal column buffers.

**Errors:** `SCHEMA_MISMATCH` if column row counts differ; `IO_ERROR` on write failure.

### `ParquetWriter::close()`

```cpp
[[nodiscard]] expected<void> close();
```

Flushes any pending row group, writes the Thrift FileMetaData footer, footer length, and closing PAR1 magic. The file is not valid until `close()` completes. The destructor calls `close()` automatically as a safety net.

### `ParquetWriter::csv_to_parquet()`

```cpp
[[nodiscard]] static expected<void> csv_to_parquet(
    const std::filesystem::path& csv_input,
    const std::filesystem::path& parquet_output,
    const WriterOptions& options = WriterOptions{});
```

Converts a CSV file to Parquet. Auto-detects column types by scanning all rows (INT64 > DOUBLE > BOOLEAN > STRING priority order). The first row is treated as the header.

### Status accessors

```cpp
int64_t rows_written() const;        // total rows (flushed + pending)
int64_t row_groups_written() const;  // flushed row groups
bool    is_open() const;
```

---

## Core — ParquetReader

**Header:** `include/signet/reader.hpp`

### `ParquetReader::open()`

```cpp
[[nodiscard]] static expected<ParquetReader> open(
    const std::filesystem::path& path,
    const std::optional<crypto::EncryptionConfig>& encryption = std::nullopt);
```

Reads the entire file into memory, validates PAR1 magic, deserializes the Thrift footer, and builds the schema. Pass `encryption` to read PME-encrypted files.

**Errors:** `IO_ERROR` if file cannot be opened; `INVALID_FILE` if magic is missing; `CORRUPT_FOOTER` if footer deserialization fails; `ENCRYPTION_ERROR` if the file has an encrypted footer but no config was provided.

### `ParquetReader::read_column()`

```cpp
template <typename T>
expected<std::vector<T>> read_column(size_t row_group_index,
                                      size_t column_index);
```

Reads a typed column from a specific row group. Automatically handles PLAIN, RLE_DICTIONARY, DELTA_BINARY_PACKED, BYTE_STREAM_SPLIT, and RLE (booleans) encodings, plus decompression.

Supported type mappings: `bool` for BOOLEAN columns, `int32_t` for INT32, `int64_t` for INT64, `float` for FLOAT, `double` for DOUBLE, `std::string` for BYTE_ARRAY.

**Errors:** `OUT_OF_RANGE` if indices are out of bounds; `CORRUPT_PAGE` if page data is malformed; `UNSUPPORTED_ENCODING` for unrecognised encoding combinations.

### `ParquetReader::read_column_as_strings()`

```cpp
expected<std::vector<std::string>> read_column_as_strings(
    size_t row_group_index, size_t column_index);
```

Reads any column and converts values to string representation. FIXED_LEN_BYTE_ARRAY columns are hex-encoded.

### `ParquetReader::read_row_group()`

```cpp
expected<std::vector<std::vector<std::string>>> read_row_group(
    size_t row_group_index);
```

Returns all columns in a row group as `vector<vector<string>>`. Index `[col][row]`.

### `ParquetReader::read_all()`

```cpp
expected<std::vector<std::vector<std::string>>> read_all();
```

Reads the entire file as a row-major vector: `[row][col]`.

### `ParquetReader::read_columns()`

```cpp
expected<std::vector<std::vector<std::string>>> read_columns(
    const std::vector<std::string>& column_names);
```

Column projection — reads only the named columns across all row groups.

### `ParquetReader::read_bloom_filter()`

```cpp
expected<SplitBlockBloomFilter> read_bloom_filter(
    size_t row_group_index, size_t column_index) const;

template <typename T>
bool bloom_might_contain(
    size_t row_group_index, size_t column_index,
    const T& value) const;
```

Returns `true` if the value might be present (false positive possible). Returns `true` if no bloom filter exists (conservative).

### Metadata accessors

```cpp
const Schema& schema() const;
int64_t num_rows() const;
int64_t num_row_groups() const;
const std::string& created_by() const;
const std::vector<thrift::KeyValue>& key_value_metadata() const;
RowGroupInfo row_group(size_t index) const;
const thrift::Statistics* column_statistics(size_t rg, size_t col) const;
```

---

## Encodings

**Header:** `include/signet/encoding/`

### `RleEncoder` / `RleDecoder`

```cpp
// Encode — input is uint32_t array (even for boolean columns)
static std::vector<uint8_t> encode(const uint32_t* values, size_t count, int bit_width);
static std::vector<uint8_t> encode_with_length(const uint32_t* values, size_t count, int bit_width);

// Decode
static std::vector<uint32_t> decode(const uint8_t* data, size_t size, int bit_width, size_t count);
static std::vector<uint32_t> decode_with_length(const uint8_t* data, size_t size, int bit_width, size_t count);
```

`bit_width` must be in range [1, 32]. Bit_width 0 and values above 32 return empty results.

### `delta::encode_int64` / `decode_int64`

```cpp
// namespace signet::forge::delta
std::vector<uint8_t> encode_int64(const int64_t* values, size_t count);
std::vector<int64_t> decode_int64(const uint8_t* data, size_t buf_size, size_t count);

// INT32 variants:
std::vector<uint8_t> encode_int32(const int32_t* values, size_t count);
std::vector<int32_t> decode_int32(const uint8_t* data, size_t buf_size, size_t count);
```

DELTA_BINARY_PACKED encoding is ideal for monotonically increasing timestamps. Typical compression: less than 50% of PLAIN size on sorted int64 data.

### `DictionaryEncoder<T>` / `DictionaryDecoder<T>`

```cpp
template <typename T>
class DictionaryEncoder {
    void put(const T& value);
    void flush();
    std::vector<uint8_t> dictionary_page() const;
    std::vector<uint8_t> indices_page() const;
    size_t dictionary_size() const;
};

template <typename T>
class DictionaryDecoder {
    DictionaryDecoder(const uint8_t* dict_data, size_t dict_size,
                      size_t num_dict_entries, PhysicalType type);
    expected<std::vector<T>> decode(const uint8_t* indices_data,
                                    size_t indices_size, size_t count);
};
```

### `byte_stream_split::encode_double` / `decode_double`

```cpp
// namespace signet::forge::byte_stream_split
std::vector<uint8_t> encode_double(const double* values, size_t count);
std::vector<double>  decode_double(const uint8_t* data, size_t size, size_t count);

std::vector<uint8_t> encode_float(const float* values, size_t count);
std::vector<float>   decode_float(const uint8_t* data, size_t size, size_t count);
```

Byte-stream-split reorders floating-point bytes so that the exponent bytes from all values are contiguous, improving compression ratio for downstream codecs.

---

## Compression

**Header:** `include/signet/compression/codec.hpp`

### `ICompressor` / `IDecompressor` interfaces

```cpp
class ICompressor {
    virtual expected<std::vector<uint8_t>> compress(
        const uint8_t* data, size_t size) const = 0;
};

class IDecompressor {
    virtual expected<std::vector<uint8_t>> decompress(
        const uint8_t* data, size_t compressed_size,
        size_t uncompressed_size) const = 0;
};
```

### `Compression` enum

```cpp
enum class Compression {
    UNCOMPRESSED,
    SNAPPY,
    GZIP,
    LZ4,
    ZSTD
};
```

### Codec availability

| Codec | Build requirement | `WriterOptions.compression` |
|-------|------------------|---------------------------|
| `UNCOMPRESSED` | Always | `Compression::UNCOMPRESSED` |
| `SNAPPY` | Bundled (no external dep) | `Compression::SNAPPY` |
| `ZSTD` | `SIGNET_ENABLE_ZSTD=ON` | `Compression::ZSTD` |
| `LZ4` | `SIGNET_ENABLE_LZ4=ON` | `Compression::LZ4` |
| `GZIP` | `SIGNET_ENABLE_GZIP=ON` | `Compression::GZIP` |

Using an unavailable codec returns `Error{ErrorCode::UNSUPPORTED_COMPRESSION, ...}`.

---

## Encryption — PME

**Header:** `include/signet/crypto/pme.hpp`

### `EncryptionConfig` struct

```cpp
struct EncryptionConfig {
    std::vector<uint8_t> footer_key;       // 32-byte AES-256 key for footer
    bool encrypt_footer = false;

    std::vector<uint8_t> default_column_key;  // fallback for all columns
    std::unordered_map<std::string, std::vector<uint8_t>> column_keys;

    EncryptionAlgorithm algorithm = EncryptionAlgorithm::AES_GCM_CTR_V1;
};
```

### `EncryptionAlgorithm` enum

```cpp
enum class EncryptionAlgorithm {
    AES_GCM_V1,      // AES-256-GCM for both footer and columns
    AES_GCM_CTR_V1   // AES-256-GCM for footer, AES-256-CTR for columns (recommended)
};
```

### `FileEncryptor` / `FileDecryptor`

```cpp
class FileEncryptor {
    explicit FileEncryptor(const EncryptionConfig& config);
    bool is_column_encrypted(const std::string& column_name) const;
    expected<std::vector<uint8_t>> encrypt_footer(const uint8_t* data, size_t size);
    expected<std::vector<uint8_t>> encrypt_column_page(
        const uint8_t* data, size_t size,
        const std::string& column_name, int32_t row_group_index, int32_t page_ordinal);
};

class FileDecryptor {
    explicit FileDecryptor(const EncryptionConfig& config);
    expected<std::vector<uint8_t>> decrypt_footer(const uint8_t* data, size_t size);
    expected<std::vector<uint8_t>> decrypt_column_page(
        const uint8_t* data, size_t size,
        const std::string& column_name, int32_t row_group_index, int32_t page_ordinal);
};
```

---

## Post-Quantum Cryptography

**Header:** `include/signet/crypto/post_quantum.hpp`

Requires `SIGNET_ENABLE_PQ=ON`. See `PQ_CRYPTO_GUIDE.md` for full documentation.

### `KyberKem`

```cpp
struct KemKeypair {
    std::vector<uint8_t> public_key;   // 1184 bytes (Kyber-768)
    std::vector<uint8_t> secret_key;   // 2400 bytes
};

struct KemCiphertext {
    std::vector<uint8_t> ciphertext;   // 1088 bytes
    std::vector<uint8_t> shared_secret;
};

class KyberKem {
    static expected<KemKeypair>   generate_keypair();
    static expected<KemCiphertext> encapsulate(const std::vector<uint8_t>& public_key);
    static expected<std::vector<uint8_t>> decapsulate(
        const std::vector<uint8_t>& ciphertext,
        const std::vector<uint8_t>& secret_key);
};
```

### `DilithiumSign`

```cpp
struct SignKeypair {
    std::vector<uint8_t> public_key;   // 1952 bytes (Dilithium-3)
    std::vector<uint8_t> secret_key;   // 4000 bytes
};

class DilithiumSign {
    static expected<SignKeypair> generate_keypair();
    static expected<std::vector<uint8_t>> sign(
        const std::vector<uint8_t>& message,
        const std::vector<uint8_t>& secret_key);
    static expected<bool> verify(
        const std::vector<uint8_t>& message,
        const std::vector<uint8_t>& signature,
        const std::vector<uint8_t>& public_key);
};
```

Signature size: maximum 3293 bytes (Dilithium-3).

### `PostQuantumConfig`

```cpp
struct PostQuantumConfig {
    bool enabled            = false;
    bool hybrid_mode        = true;  // Kyber-768 + X25519 hybrid (recommended)
    std::vector<uint8_t> recipient_public_key;
    std::vector<uint8_t> signing_secret_key;
    std::vector<uint8_t> signing_public_key;
};
```

---

## Bloom Filters

**Header:** `include/signet/bloom/split_block.hpp`

### `SplitBlockBloomFilter`

```cpp
class SplitBlockBloomFilter {
    // Construct for writing: ndv_estimate = expected distinct values
    SplitBlockBloomFilter(size_t ndv_estimate, double fpr = 0.01);

    // Insert a value
    template <typename T>
    void insert_value(const T& value);

    // Query: returns true if value might be present (false positives possible)
    template <typename T>
    bool might_contain_value(const T& value) const;

    // Serialization
    const std::vector<uint8_t>& data() const;
    static SplitBlockBloomFilter from_data(const uint8_t* data, size_t size);
    void reset();

    static constexpr size_t kBytesPerBlock = 32;
};
```

Use via `WriterOptions.enable_bloom_filter = true` for automatic bloom filter management.

---

## AI Vector Columns

**Header:** `include/signet/ai/vector_type.hpp`, `include/signet/ai/quantized_vector.hpp`

### `VectorColumnWriter` / `VectorColumnReader`

```cpp
class VectorColumnWriter {
    VectorColumnWriter(size_t dimension);
    expected<void> write(const float* values, size_t num_vectors);
    expected<void> flush(ParquetWriter& writer, size_t column_index);
};

class VectorColumnReader {
    expected<std::vector<std::vector<float>>> read(
        ParquetReader& reader, size_t column_index);
};
```

### `QuantizedVectorWriter` / `QuantizedVectorReader`

Store INT8 or INT4 quantized vectors to reduce storage by 4x–8x:

```cpp
enum class QuantizationMode { INT8, INT4 };

class QuantizedVectorWriter {
    QuantizedVectorWriter(size_t dimension, QuantizationMode mode = QuantizationMode::INT8);
    expected<void> write(const float* values, size_t num_vectors);
    expected<void> flush(ParquetWriter& writer, size_t column_index);
};

class QuantizedVectorReader {
    // Reads and dequantizes to float on the fly
    expected<std::vector<std::vector<float>>> read(
        ParquetReader& reader, size_t column_index);
};
```

**Usage example:**
```cpp
// Write 1000 128-dimensional float32 vectors
VectorColumnWriter vw(128);
std::vector<float> embeddings(128 * 1000);
// ... populate embeddings ...
(void)vw.write(embeddings.data(), 1000);
(void)vw.flush(writer, col_index);
```

---

## Tensor Bridge

**Header:** `include/signet/ai/tensor_bridge.hpp`

### `TensorShape`

```cpp
struct TensorShape {
    size_t n;  // Single-dimension shape: TensorShape{1024} — NOT TensorShape{{1024}}
};
```

### `TensorDataType` enum

```cpp
enum class TensorDataType {
    FLOAT32,
    FLOAT64,
    INT32,
    INT64,
    INT8,
    UINT8
};
```

### `OwnedTensor<T>`

```cpp
template <typename T>
class OwnedTensor {
    OwnedTensor(TensorShape shape, TensorDataType dtype);

    TensorView<T> view();
    const T* data() const;
    size_t   size() const;
    TensorShape shape() const;
};
```

### `TensorView<T>`

```cpp
template <typename T>
class TensorView {
    TensorView(T* data, TensorShape shape, TensorDataType dtype);

    T* data();
    const T* data() const;
    size_t size() const;
    TensorShape shape() const;
};
```

Zero-copy views enable passing tensor data directly to ONNX Runtime as `OrtValue`, to Arrow via the C Data Interface, and to NumPy via DLPack. See `include/signet/interop/` headers for the bridge adapters.

---

## Streaming WAL

**Header:** `include/signet/ai/wal.hpp`

### `WalEntry` struct

```cpp
struct WalEntry {
    int64_t              seq;           // Sequence number (0-based, monotonic)
    int64_t              timestamp_ns;  // Unix nanoseconds (CLOCK_REALTIME)
    std::vector<uint8_t> payload;       // Raw bytes
};
using WalRecord = WalEntry;  // alias
```

### `WalWriterOptions`

```cpp
struct WalWriterOptions {
    bool   sync_on_append = false;   // fsync after every record (slow but durable)
    bool   sync_on_flush  = true;    // fsync on explicit flush()
    size_t buffer_size    = 65536;   // stdio buffer size in bytes
    int64_t start_seq     = 0;       // first sequence number for new files
};
```

### `WalWriter`

```cpp
class WalWriter {
    // Factory
    static expected<WalWriter> open(const std::string& path, WalWriterOptions opts = {});

    // Append — multiple overloads
    expected<int64_t> append(const uint8_t* data, size_t size);
    expected<int64_t> append(const std::vector<uint8_t>& data);
    expected<int64_t> append(std::string_view sv);

    // Flush stdio buffer; do_fsync forces kernel sync
    expected<void> flush(bool do_fsync = false);

    // Close: flush + fsync + close file
    expected<void> close();

    // Status
    bool        is_open() const;
    int64_t     next_seq() const;
    const std::string& path() const;
    int64_t     bytes_written() const;
};
```

`WalWriter` is move-only (not copyable). Benchmark: ~339 ns per 32-byte append (buffered, no fsync).

### `WalReader`

```cpp
class WalReader {
    static expected<WalReader> open(const std::string& path);

    // Read next entry; returns nullopt at EOF or on first corrupted record
    expected<std::optional<WalEntry>> next();

    // Read all valid entries
    expected<std::vector<WalEntry>> read_all();

    int64_t last_seq() const;
    int64_t count() const;
    void    close();
};
```

### `WalManagerOptions`

```cpp
struct WalManagerOptions {
    size_t max_segment_bytes = 64 * 1024 * 1024;  // 64 MB
    size_t max_records       = 1'000'000;
    bool   sync_on_append    = false;
    bool   sync_on_roll      = true;
    std::string file_prefix  = "wal";
    std::string file_ext     = ".wal";
};
```

### `WalManager`

```cpp
class WalManager {
    static expected<WalManager> open(const std::string& dir, WalManagerOptions opts = {});

    expected<int64_t> append(const uint8_t* data, size_t size);
    expected<int64_t> append(std::string_view sv);

    expected<std::vector<WalEntry>> read_all();

    std::vector<std::string> segment_paths() const;
    expected<void> remove_segment(const std::string& path);

    int64_t total_records() const;
    expected<void> close();
};
```

Automatically rolls to a new segment file when `max_segment_bytes` or `max_records` is exceeded.

---

## Feature Store

**Header:** `include/signet/ai/feature_writer.hpp`, `include/signet/ai/feature_reader.hpp`

### `FeatureVector` struct

```cpp
struct FeatureVector {
    std::string          entity_id;
    int64_t              timestamp_ns;
    std::vector<float>   values;
    int32_t              version = 1;
};
```

### `FeatureWriterOptions`

```cpp
struct FeatureWriterOptions {
    std::string output_path;   // Directory for output Parquet files
    // Group metadata, schema name, feature names
};
```

### `FeatureWriter`

```cpp
class FeatureWriter {
    static expected<FeatureWriter> create(const FeatureWriterOptions& opts);

    expected<void> append(const FeatureVector& fv);
    expected<void> flush();
    expected<void> close();
};
```

`FeatureWriter` is move-only.

### `FeatureReaderOptions`

```cpp
struct FeatureReaderOptions {
    std::string input_path;   // Directory containing feature Parquet files
};
```

### `FeatureReader`

```cpp
class FeatureReader {
    static expected<FeatureReader> open(const FeatureReaderOptions& opts);

    // Latest feature vector for entity
    expected<FeatureVector> get(const std::string& entity_id);

    // Point-in-time lookup: latest vector at or before timestamp_ns
    expected<FeatureVector> as_of(const std::string& entity_id, int64_t timestamp_ns);

    // Full history for entity, sorted by timestamp ascending
    expected<std::vector<FeatureVector>> history(const std::string& entity_id);

    // Batch point-in-time lookup
    expected<std::vector<FeatureVector>> as_of_batch(
        const std::vector<std::string>& entity_ids, int64_t timestamp_ns);
};
```

Benchmark: ~1.4 μs per `as_of()` call (with row group cache).

---

## AI Audit Chain

**Header:** `include/signet/ai/audit_chain.hpp`

**License:** BSL 1.1 (gated by `SIGNET_BUILD_AI_AUDIT`)

### `now_ns()`

```cpp
int64_t now_ns() noexcept;  // Unix nanoseconds via CLOCK_REALTIME
```

Defined in `signet::forge` namespace. If your code has `using namespace signet::forge`, do not define your own `now_ns()` — the names will collide. Rename your local helper to `test_now_ns()`.

### `AuditEntry` struct

```cpp
struct AuditEntry {
    int64_t     timestamp_ns;
    std::string data;
    std::string prev_hash;   // SHA-256 of previous entry
    std::string hash;        // SHA-256 of this entry
};
```

### `AuditChain`

```cpp
class AuditChain {
    static expected<AuditChain> create(const std::string& path);
    static expected<AuditChain> open(const std::string& path);

    expected<void> append(const std::string& data);
    expected<bool> verify() const;  // Returns true if hash chain is intact
    expected<std::vector<AuditEntry>> read_all() const;
};
```

---

## Decision Log (BSL 1.1)

**Header:** `include/signet/ai/decision_log.hpp`

**License:** BSL 1.1

### `DecisionRecord` struct

```cpp
struct DecisionRecord {
    int32_t     strategy_id;    // int32_t, NOT std::string
    int64_t     timestamp_ns;
    std::string action;         // e.g., "BUY", "SELL", "HOLD"
    double      confidence;
    std::string model_version;
    std::string order_id;
    std::string symbol;
    double      price;
    double      quantity;
    std::string venue;
};
```

### `DecisionLogWriter`

```cpp
class DecisionLogWriter {
    // output_dir: no ".." path segments
    // chain_id:   must match [a-zA-Z0-9_-]+ (validated)
    static expected<DecisionLogWriter> create(
        const std::string& output_dir,
        const std::string& chain_id);

    expected<void> log(const DecisionRecord& rec);
    expected<void> close();

    std::string current_file_path() const;
};
```

### `DecisionLogReader`

```cpp
class DecisionLogReader {
    static expected<DecisionLogReader> open(const std::string& path);
    expected<std::vector<DecisionRecord>> read_all() const;
    expected<bool> verify_chain() const;
};
```

---

## Inference Log (BSL 1.1)

**Header:** `include/signet/ai/inference_log.hpp`

**License:** BSL 1.1

### `InferenceRecord` struct

```cpp
struct InferenceRecord {
    int64_t     timestamp_ns;
    std::string model_id;
    std::string model_version;
    std::string input_hash;    // SHA-256 of input features
    std::string output_hash;   // SHA-256 of model output
    double      latency_us;
    int32_t     batch_size;
};
```

### `InferenceLogWriter`

```cpp
class InferenceLogWriter {
    static expected<InferenceLogWriter> create(
        const std::string& output_dir,
        const std::string& chain_id);

    expected<void> log(const InferenceRecord& rec);
    expected<void> close();
};
```

---

## StreamingSink

**Header:** `include/signet/ai/streaming_sink.hpp`

### `StreamingSink::Options` (nested struct)

```cpp
class StreamingSink {
public:
    struct Options {               // Nested — use StreamingSink::Options, NOT StreamingSinkOptions
        size_t ring_capacity = 4096;
        std::string output_path;
    };

    static expected<StreamingSink> create(Options opts);
    expected<void> submit(/* record */);
    void stop();
};
```

---

## MPMC Event Bus

**Header:** `include/signet/ai/event_bus.hpp`

### `EventBusOptions` (namespace scope)

```cpp
// Defined at signet::forge namespace scope, NOT as EventBus::Options
struct EventBusOptions {
    size_t tier2_capacity = 4096;   // Tier-2 MPMC ring capacity (must be power-of-2)
    size_t tier1_capacity = 256;    // Default capacity for make_channel()
    bool   enable_tier3   = false;  // Route to attached StreamingSink
};
```

### `EventBus`

```cpp
class EventBus {
    // EventBus is NOT movable or copyable (std::mutex member)
    // Always allocate on heap: auto bus = std::make_unique<EventBus>(opts);

    explicit EventBus(EventBusOptions opts = {});

    // Tier 1: dedicated named channels
    std::shared_ptr<MpmcRing<SharedColumnBatch>> make_channel(
        const std::string& name, size_t capacity = 0);
    std::shared_ptr<MpmcRing<SharedColumnBatch>> channel(
        const std::string& name) const;

    // Tier 2: shared MPMC pool
    bool publish(SharedColumnBatch batch);  // returns false if ring full
    bool pop(SharedColumnBatch& out);       // returns false if ring empty

    // Tier 3: WAL sink attachment
    void attach_sink(StreamingSink* sink);
    void detach_sink();

    // Stats
    struct Stats { uint64_t published, dropped, tier3_drops; };
    Stats stats() const noexcept;
    void  reset_stats() noexcept;

    // Introspection
    size_t tier2_size() const noexcept;
    size_t tier2_capacity() const noexcept;
    size_t num_channels() const;
};
```

Benchmark: ~10.4 ns per push+pop (single-threaded, int64_t payload).

### `MpmcRing<T>`

```cpp
template <typename T>
class MpmcRing {
    explicit MpmcRing(size_t capacity);  // capacity must be power-of-2

    bool push(T value);   // returns false if full
    bool pop(T& out);     // returns false if empty

    size_t size() const;
    size_t capacity() const;
};
```

### `ColumnBatch` / `SharedColumnBatch`

```cpp
using SharedColumnBatch = std::shared_ptr<ColumnBatch>;

struct ColumnBatch {
    std::string topic;
    int64_t     timestamp_ns;
    std::vector<std::vector<uint8_t>> columns;  // raw column data

    TensorView<float> column_as_tensor(size_t col_index) const;
    StreamRecord to_stream_record() const;
};
```

---

## Compliance Reporters (BSL 1.1)

**Header:** `include/signet/ai/compliance/`

**License:** BSL 1.1. Requires `SIGNET_BUILD_AI_AUDIT=ON` (default).

### `ReportFormat` enum

```cpp
enum class ReportFormat {
    JSON,
    NDJSON,  // newline-delimited JSON, one record per line
    CSV
};
```

### `ComplianceStandard` enum

```cpp
enum class ComplianceStandard {
    MIFID2_RTS24,
    EU_AI_ACT_ARTICLE_12,
    EU_AI_ACT_ARTICLE_13,
    EU_AI_ACT_ARTICLE_19
};
```

### `ReportOptions` struct

```cpp
struct ReportOptions {
    ReportFormat       format              = ReportFormat::JSON;
    std::string        output_path;
    std::string        decision_log_path;
    std::string        firm_id;            // MiFID II: Legal Entity Identifier (LEI)
    bool               verify_chain       = true;
};
```

### `ComplianceReport` struct

```cpp
struct ComplianceReport {
    bool        chain_verified;   // true if audit hash chain is intact
    std::string content;          // serialized report (JSON/NDJSON/CSV)
    std::string standard;         // e.g., "MiFID II RTS 24 Annex I"
    int64_t     record_count;
};
```

### `MiFID2Reporter`

```cpp
class MiFID2Reporter {
    static expected<ComplianceReport> generate(const ReportOptions& opts);
};
```

Produces RTS 24-compliant output with all required fields per Annex I of Commission Delegated Regulation (EU) 2017/576.

### `EUAIActReporter`

```cpp
class EUAIActReporter {
    static expected<ComplianceReport> generate(const ReportOptions& opts);
    static expected<ComplianceReport> generate_article19(const ReportOptions& opts);
};
```

Covers Articles 12 (record-keeping), 13 (transparency), and 19 (conformity assessment) of Regulation (EU) 2024/1689.

**Usage example:**
```cpp
#include "signet/ai/compliance/mifid2_reporter.hpp"
using namespace signet::forge;

ReportOptions opts;
opts.format            = ReportFormat::JSON;
opts.output_path       = "mifid2_report.json";
opts.decision_log_path = "/logs/decisions/";
opts.firm_id           = "FIRM-LEI-00123456789";

auto report = MiFID2Reporter::generate(opts);
if (report.has_value()) {
    // report->chain_verified == true means the audit chain is tamper-free
    // report->content contains the JSON output
}
```
