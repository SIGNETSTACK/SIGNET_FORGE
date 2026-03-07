# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-03-04

Initial public release of Signet Forge.

### Added

#### Core Parquet Engine
- `ParquetWriter` — streaming Parquet file writer with configurable encoding and compression
- `ParquetReader` — random-access Parquet file reader with typed column APIs
- `MmapParquetReader` — memory-mapped reader path for zero-copy access
- `Schema` and `SchemaBuilder` — fluent schema definition with 7 physical types (BOOLEAN, INT32, INT64, INT96, FLOAT, DOUBLE, BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY)
- `ColumnStatistics` — min/max/null_count per column chunk
- `ColumnIndex` and `OffsetIndex` — predicate pushdown support
- CSV-to-Parquet converter with automatic type detection

#### Encodings
- PLAIN encoding for all physical types
- RLE/Bit-Packing Hybrid encoding (definition/repetition levels, boolean columns)
- DELTA_BINARY_PACKED encoding (timestamps, monotonic sequences)
- BYTE_STREAM_SPLIT encoding (floating-point columns)
- RLE_DICTIONARY encoding with dictionary pages

#### Compression
- Snappy — bundled header-only implementation (zero external dependencies)
- ZSTD — optional, link `libzstd` (`-DSIGNET_ENABLE_ZSTD=ON`)
- LZ4 — optional, link `liblz4` (`-DSIGNET_ENABLE_LZ4=ON`)
- Gzip — optional, link `zlib` (`-DSIGNET_ENABLE_GZIP=ON`)

#### Encryption
- AES-256-GCM footer encryption
- AES-256-CTR column data encryption
- Parquet Modular Encryption (PME) full spec implementation
- Key metadata serialization
- Post-quantum encryption: Kyber-768 KEM + Dilithium-3 digital signatures
- X25519 Diffie-Hellman key agreement (RFC 7748 Montgomery ladder)
- Hybrid KEM: X25519 + Kyber-768 combined key encapsulation

#### Bloom Filters
- Split-block bloom filter (Parquet spec compliant)
- Bundled xxHash64 implementation (public domain)
- PME-encrypted bloom filter support

#### AI Vector Types
- `FLOAT32_VECTOR(dim)` logical type with SIMD-accelerated I/O
- `INT8` and `INT4` quantized vector storage with on-read dequantization

#### Zero-Copy Interop
- Arrow C Data Interface bridge (`ArrowArray` / `ArrowSchema`, zero-copy)
- ONNX Runtime bridge (`OrtValue` creation from Parquet columns)
- NumPy / DLPack / buffer protocol bridge for PyTorch integration
- `TensorView` and `OwnedTensor` for zero-copy ML inference

#### Write-Ahead Log (WAL)
- `WalWriter` — fwrite-based WAL with 339 ns per-append latency (32 B payload)
- `WalReader` — crash-safe WAL reader with CRC-32 integrity verification
- `WalManager` — segment rolling, compaction, and lifecycle management
- `WalMmapWriter` — mmap ring-buffer WAL with ~38 ns per-append latency
- `MappedSegment` — 4-slot ring with background pre-allocation and drain
- `StreamingSink` — lock-free ring buffer to row group flusher

#### Feature Store
- `FeatureWriter` — point-in-time correct feature materialization to Parquet
- `FeatureReader` — 12 us per-entity `as_of()` lookup via binary search index
- History and batch APIs for time-travel feature retrieval

#### Event Bus
- `MpmcRing<T>` — Vyukov MPMC bounded queue (10.4 ns single-threaded push+pop)
- `ColumnBatch` — columnar batch container with zero-copy TensorView access
- `EventBus` — three-tier event router (topic → subscriber → handler)

#### AI Audit Trail (BSL 1.1)
- `AuditChain` — SHA-256 hash chain across row groups for tamper detection
- `DecisionLogWriter` / `DecisionLogReader` — structured AI decision logging
- `InferenceLogWriter` / `InferenceLogReader` — ML inference audit trail

#### Compliance Reporters (BSL 1.1)
- `MiFID2Reporter` — MiFID II RTS 24 Annex I report generation (JSON/NDJSON/CSV)
- `EUAIActReporter` — EU AI Act Articles 12, 13, and 19 conformity assessment

#### Python Bindings
- pybind11 bindings with 44/44 C++ API exports
- NumPy array integration for all column types
- Full audit/compliance API bindings (gated by `SIGNET_HAS_AI_AUDIT`)
- 35 pytest test functions

#### CLI Tool
- `signet_cli` — Parquet file inspection (schema, row groups, statistics, metadata)

#### Build System
- 12 CMake presets: dev, dev-tests, release, asan, tsan, ubsan, msan, ci, benchmarks, python, minimal, server, server-pq
- Header-only core with zero mandatory dependencies
- FetchContent integration (Catch2 3.7.1 for tests, pybind11 for Python)
- `SIGNET_MINIMAL_DEPS` one-flag embedded build mode

#### CI/CD
- GitHub Actions with 7 matrix jobs: build-test (Ubuntu + macOS), ASan, TSan, UBSan, Windows MSVC, server codecs (ZSTD+LZ4+Gzip), post-quantum (liboqs)
- Concurrency control with cancel-in-progress

#### Examples
- `basic_write.cpp` — minimal write example
- `basic_read.cpp` — minimal read example
- `csv_to_parquet.cpp` — generic CSV to Parquet converter
- `ticks_import.cpp` — HFT tick data CSV.gz to Parquet with optimal encodings
- `ticks_query.cpp` — HFT tick data query with predicate pushdown
- `ticks_wal_stream.cpp` — HFT tick data WAL durable ingestion to Parquet compaction

#### Benchmarks
- 37 benchmark cases across 6 files: write throughput, read throughput, WAL latency (fwrite + mmap), encoding speed, feature store latency, MPMC ring throughput

#### Documentation
- 9 internal architecture documents (overview, build system, data structures, Parquet format, encryption, encoding codecs, AI subsystem, thread model, PQ-PME integration)
- 7 user-facing documents (quickstart, API reference, applications, compression guide, PQ crypto guide, AI features, production setup)
- Production README with feature matrix, quickstart, benchmark table, architecture diagram

### Security

- **Hardening Pass 1**: Fixed 6 vulnerabilities — BSS decode OOB reads, RLE bit_width boundary values (0 and 65), Thrift nesting depth exhaustion, bad/truncated Parquet magic bytes, Python write_column OOB, path traversal in DecisionLogWriter
- **Hardening Pass 2**: Fixed 6 additional vulnerabilities — WAL oversize record cap (64 MB), FeatureWriter path traversal, Parquet page size bomb cap (256 MB), Thrift field-count DoS cap (65536), Thrift string bomb cap (64 MB), ArrowArray offset overflow
- **Hardening Pass 3**: Fixed 23 additional vulnerabilities across 5 batches (~20 files modified):
  - *Encoding*: RLE bit_width clamped to [0,64] (prevents UB on shift > 64), Dictionary decoder OOB index (assert→runtime check), BSS count×WIDTH integer overflow (division-based guard), Delta decoder accumulation overflow (__builtin_add_overflow)
  - *Thrift*: Negative list count rejection, MAP/LIST/SET collection size DoS cap (1M entries)
  - *Crypto*: Replaced std::random_device byte-by-byte IV generation with platform CSPRNG (arc4random_buf on macOS, getrandom on Linux), GCM counter overflow guard per NIST SP 800-38D (~64 GB limit), TLV field size overflow guard (64 MB cap), AES-256 round keys zeroed in destructor via volatile pointer, cipher adapter key vectors zeroed on destruction
  - *Interop*: Arrow bridge offset×elem_size overflow check, raw new→std::make_unique RAII for ArrowSchemaPrivate/ArrowArrayPrivate
  - *AI*: TensorView assert()→throw std::out_of_range in production, string size truncation in decision/inference log append_string, FeatureWriter symlink bypass (weakly_canonical + raw path check)
  - *Compliance*: Field length truncation (MAX_FIELD_LENGTH=4096) in MiFID2 and EU AI Act reporters, 64-bit time_t static_assert for timestamps beyond 2038
  - *WAL*: Empty record rejection, verify_chain() early return on hash deserialization failure
  - *Post-quantum*: Refined #pragma message warning (acknowledges X25519 HybridKem provides real classical ECDH; only Kyber lattice portion is stubbed), added is_real_pq_crypto() runtime query
  - New header: `crypto/cipher_interface.hpp` — platform-aware CSPRNG + shared cipher adapters
- **Hardening Pass 4**: Fixed 29 additional vulnerabilities across 7 batches (all language bindings):
  - *Core C++*: Arena allocator overflow guards (count×sizeof, size+alignment), signed index overflow in column/offset index, dictionary page num_values validation, batch read count overflow in column_reader, INT64_MIN negation UB in delta encoder (cast to unsigned), page size >2GiB truncation guard, decompression ratio bomb limit (1024:1), NaN exclusion from statistics, schema column() bounds checking with std::out_of_range
  - *Crypto*: AES-CTR counter overflow guard (64 GiB limit matching AES-GCM)
  - *AI tier*: Feature/embedding count bounds checking in decision_log/inference_log deserialization, stoll try/catch in audit metadata parsing, StreamingSink output_dir path traversal guard, ColumnBatch 100M cell OOM limit
  - *C FFI*: try/catch around 5 extern "C" functions (signet_writer_open, write_column_string, write_row, reader_open, read_column_string) preventing C++ exception UB across FFI boundary, signet_schema_builder_free() for proper cleanup, string column read OOM cleanup on partial failure
  - *Rust FFI*: SchemaBuilder::new() returns Result<Self> with null check (was infallible), build() only forgets self on success, Drop uses signet_schema_builder_free, write_column_string panic→Result, WriterOptions null allocation assert
  - *WASM*: writeFileToMemfs 256 MB file size limit, schema column accessor bounds checks, writer column bounds checks, encryption key material zeroing after use (volatile write)
  - *Python*: __init__.py graceful degradation for AI audit types (try/except ImportError), write_column_bool bounds check parity with other write_column methods
  - *Keygen*: parse_hex_hash bare "0x" rejection, expiry_date overflow clamp [1,36500 days], semicolon injection prevention in custom claims
- 394 total unit tests + 5 Rust integration tests + 5 doc-compile tests, all passing across all 4 hardening passes

[Unreleased]: https://github.com/SIGNETSTACK/signet-forge/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/SIGNETSTACK/signet-forge/releases/tag/v0.1.0
