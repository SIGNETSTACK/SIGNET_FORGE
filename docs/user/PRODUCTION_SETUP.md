# Production Setup Guide

This document covers everything you need to move from a development build to a hardened, production-grade deployment of Signet Forge.

---

## Recommended CMake Preset

For production servers with full compression and standard encryption:

```bash
cmake --preset server
cmake --build build
```

For production servers that also need post-quantum encryption:

```bash
cmake --preset server-pq
cmake --build build-server-pq
```

The `server` preset enables ZSTD, LZ4, and Gzip compression and real AES-256-GCM/CTR encryption. The `server-pq` preset adds Kyber-768 KEM and Dilithium-3 signatures.

---

## ZSTD Compression

ZSTD is the recommended codec for production. It is the default for Apache Spark, DuckDB, and pandas, delivers roughly 3.5:1 compression on typical columnar data, and is supported by every major Parquet tool.

### Install ZSTD

**macOS:**
```bash
brew install zstd
```

**Ubuntu / Debian:**
```bash
apt install libzstd-dev
```

**Alpine Linux:**
```bash
apk add zstd-dev
```

### Enable in CMake

```cmake
cmake -DSIGNET_ENABLE_ZSTD=ON ..
# or with a preset:
cmake --preset server  # already includes ZSTD
```

### Use in Code

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

WriterOptions opts;
opts.compression = Compression::ZSTD;
// Optional: set per-column encodings for maximum compression
opts.auto_encoding = true;  // DELTA for int64, BYTE_STREAM_SPLIT for float/double

auto writer = ParquetWriter::open("data.parquet", schema, opts);
```

---

## Column Encryption (Parquet Modular Encryption)

Signet implements the Parquet Modular Encryption (PME) specification, which encrypts individual columns independently. This lets you encrypt only the sensitive columns (PII, prices, account IDs) while leaving other columns in plaintext for fast predicate evaluation.

### Enable in CMake

```cmake
cmake -DSIGNET_ENABLE_CRYPTO=ON ..
```

### Configure Encryption

```cpp
#include "signet/forge.hpp"
#include "signet/crypto/pme.hpp"
using namespace signet::forge;
using namespace signet::forge::crypto;

// Generate or load a 32-byte (256-bit) key
// In production: load from your KMS or HSM, never hardcode
std::vector<uint8_t> footer_key = load_key_from_kms("footer-key-id");
std::vector<uint8_t> price_key  = load_key_from_kms("price-col-key-id");

EncryptionConfig enc_cfg;
enc_cfg.footer_key       = footer_key;
enc_cfg.encrypt_footer   = true;
enc_cfg.algorithm        = EncryptionAlgorithm::AES_GCM_CTR_V1;

// Encrypt specific columns with their own keys
enc_cfg.column_keys["price"]      = price_key;
enc_cfg.column_keys["account_id"] = load_key_from_kms("acct-key-id");

WriterOptions opts;
opts.encryption = enc_cfg;

auto writer = ParquetWriter::open("secure.parquet", schema, opts);
```

### Read Encrypted Files

```cpp
EncryptionConfig dec_cfg;
dec_cfg.footer_key             = load_key_from_kms("footer-key-id");
dec_cfg.column_keys["price"]   = load_key_from_kms("price-col-key-id");
dec_cfg.column_keys["account_id"] = load_key_from_kms("acct-key-id");

auto reader = ParquetReader::open("secure.parquet", dec_cfg);
```

### Key Management Best Practices

- **Never hardcode keys.** Load from an HSM, AWS KMS, GCP Cloud KMS, or HashiCorp Vault.
- **Use separate keys per column class.** Price data, PII, and audit fields should each have their own key, so a compromise of one key does not expose all data.
- **Rotate footer keys annually.** The footer key controls access to the schema and statistics; treat it as highly sensitive.
- **Store key IDs in file metadata.** Write the KMS key ID into `WriterOptions.file_metadata` so readers can look up the correct key without hardcoding paths.
- **Audit key access.** Use your KMS's access log to detect unexpected decryption attempts.

---

## Post-Quantum Encryption

For data that must remain confidential beyond the 10-15 year horizon of classical cryptography (medical records, long-term financial archives, state secrets), enable post-quantum encryption.

### Full guide

See `PQ_CRYPTO_GUIDE.md` for complete setup instructions, key size tables, and the bundled vs liboqs mode distinction.

### Quick setup

```bash
# Install liboqs (real NIST FIPS 203/204 implementation)
brew install liboqs   # macOS
apt install liboqs-dev  # Ubuntu (may require PPA)

# Build Signet with post-quantum support
cmake --preset server-pq
cmake --build build-server-pq
```

```cpp
#include "signet/crypto/post_quantum.hpp"
using namespace signet::forge::crypto;

// Step 1: Generate keypairs (do this once; store securely in HSM)
auto kem_kp  = KyberKem::generate_keypair();
auto sign_kp = DilithiumSign::generate_keypair();

// Step 2: Configure
PostQuantumConfig pq_cfg;
pq_cfg.enabled             = true;
pq_cfg.hybrid_mode         = true;  // Kyber-768 + X25519 hybrid
pq_cfg.recipient_public_key = kem_kp->public_key;
pq_cfg.signing_secret_key   = sign_kp->secret_key;
pq_cfg.signing_public_key   = sign_kp->public_key;
```

---

## Bundled vs liboqs Mode

Signet ships two implementations of the post-quantum primitives:

| Mode | When used | Security level |
|------|-----------|----------------|
| **Bundled** | `SIGNET_ENABLE_PQ=ON` without liboqs | Functional stub — NOT cryptographically secure. For development and testing only. |
| **liboqs** | liboqs found on the system | Real NIST FIPS 203 (ML-KEM-768) and FIPS 204 (ML-DSA-65). Suitable for production. |

The X25519 classical component provides **real 128-bit security in both modes** — the Montgomery ladder from RFC 7748 is always the real implementation. Only the Kyber/Dilithium component is stubbed in bundled mode.

**Important**: Files written in bundled mode cannot be decrypted in liboqs mode and vice versa. The two modes use different wire formats. Upgrade your build before writing production data.

Use bundled mode to develop and test your application without installing liboqs. Switch to liboqs for the actual production deployment by installing the library and rebuilding.

---

## Microcontroller and Minimal Docker Images

For embedded targets or minimal Docker images with strict binary-size budgets:

```bash
cmake --preset minimal
cmake --build build-minimal
```

Or with explicit flags:

```cmake
cmake -DSIGNET_ENABLE_ZSTD=OFF \
      -DSIGNET_ENABLE_LZ4=OFF  \
      -DSIGNET_ENABLE_GZIP=OFF \
      -DSIGNET_ENABLE_CRYPTO=OFF \
      -DSIGNET_BUILD_AI_AUDIT=OFF \
      -DSIGNET_MINIMAL_DEPS=ON \
      ..
```

In minimal mode, only Snappy compression (bundled, no external dependency) is available. The core reader/writer and all encodings function normally.

---

## CI Integration

Use the `ci` preset in your GitHub Actions or GitLab CI pipeline:

```yaml
# .github/workflows/build.yml
- name: Configure
  run: cmake --preset ci

- name: Build
  run: cmake --build build-ci

- name: Test
  run: ctest --preset ci --output-on-failure
```

The `ci` preset builds in `RelWithDebInfo` mode with all targets enabled (tests, benchmarks, examples, tools). It is equivalent to running `cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSIGNET_BUILD_TESTS=ON -DSIGNET_BUILD_BENCHMARKS=ON`.

---

## Performance Tuning

### Row Group Size

The default row group size is 65,536 rows. For analytics workloads that typically filter on time ranges, a larger row group (128K–1M rows) reduces per-file metadata overhead. For streaming ingestion with frequent checkpoints, a smaller row group (8K–16K rows) reduces recovery time.

```cpp
WriterOptions opts;
opts.row_group_size = 128 * 1024;  // 128K rows per row group
```

### Encoding Selection

| Data type | Recommended encoding | Why |
|-----------|---------------------|-----|
| Monotonically increasing timestamps (int64) | `DELTA_BINARY_PACKED` | Stores deltas; typically <50% of PLAIN size |
| Float/double columns | `BYTE_STREAM_SPLIT` | Separates exponent/mantissa bytes; improves downstream compression ratio |
| Low-cardinality strings (e.g., exchange names, symbols) | `RLE_DICTIONARY` | One copy of each unique string, integer indices |
| Boolean flags | `RLE` | Run-length encodes runs of true/false |

Enable automatic encoding selection:

```cpp
WriterOptions opts;
opts.auto_encoding = true;  // Selects per-column based on physical type
```

Or specify per column:

```cpp
WriterOptions opts;
opts.column_encodings["timestamp_ns"] = Encoding::DELTA_BINARY_PACKED;
opts.column_encodings["price"]        = Encoding::BYTE_STREAM_SPLIT;
opts.column_encodings["symbol"]       = Encoding::RLE_DICTIONARY;
```

### Compression Level

ZSTD supports compression levels 1–22. The default is level 3, which is the best balance of speed and ratio. For cold storage archives (written once, read rarely), use level 9-15:

```cpp
// Custom ZSTD level requires building the compression layer with the level
// set in the codec options — see include/signet/compression/zstd.hpp
```

For real-time ingestion paths where latency matters, ZSTD level 1 or LZ4 gives near-Snappy speed with better ratio:

```cpp
WriterOptions opts;
opts.compression = Compression::LZ4;  // requires SIGNET_ENABLE_LZ4=ON
```

---

## Monitoring

### WAL Health Check

In streaming pipelines using the WAL, check segment accumulation periodically:

```cpp
#include "signet/ai/wal.hpp"
using namespace signet::forge;

auto mgr = WalManager::open("/data/wal", {});
auto segments = mgr->segment_paths();

// Alert if more than 10 uncompacted segments exist
if (segments.size() > 10) {
    alert("WAL compaction falling behind: " + std::to_string(segments.size()) + " segments");
}

// Alert on total record count
if (mgr->total_records() > 5'000'000) {
    alert("WAL record count exceeds threshold");
}
```

### Feature Store Compaction

The feature store writes Parquet files that accumulate over time. Compact them periodically by reading all features and rewriting to a single file per entity group. Point-in-time correctness is preserved across compactions because timestamps are stored with each feature vector.

### Key Rotation Monitoring

Track when encryption keys were created (store the creation date in file key-value metadata) and alert when any key approaches its rotation deadline:

```cpp
WriterOptions opts;
opts.file_metadata.push_back({"key.rotation.deadline", "2027-01-01"});
opts.file_metadata.push_back({"key.id", kms_key_id});
```

---

## Input Validation and Hardened Attack Surfaces

Signet enforces hard limits on all untrusted binary inputs. Three security hardening passes identified and guarded 35 attack surfaces across the parser, writer, crypto, interop, AI tier, and compliance layers. All limits are enforced at decode/open time — the library returns an error rather than allocating or looping without bound.

### Hard Limits Reference Table

| Subsystem | Input | Limit | Guard location | Behaviour on violation |
|-----------|-------|-------|---------------|------------------------|
| Parquet reader | `uncompressed_page_size` per page | **256 MB** (`PARQUET_MAX_PAGE_SIZE`) | `reader.hpp` (2 locations in page decode path) | Returns `CORRUPT_PAGE` error |
| Thrift decoder | Struct nesting depth | **128 levels** (`MAX_NESTING_DEPTH`) | `thrift/compact.hpp` `begin_struct()` | Sets `error_=true`; all reads become no-ops |
| Thrift decoder | Fields per struct | **65 536** (`MAX_FIELD_COUNT`) | `thrift/compact.hpp` `read_field_header()` | Sets `error_=true`; returns STOP |
| Thrift decoder | String / bytes field size | **64 MB** (`MAX_STRING_BYTES`) | `thrift/compact.hpp` `read_string()` + `read_binary()` | Sets `error_=true`; returns empty |
| WAL reader | Record payload size | **64 MB** (`WAL_MAX_RECORD_SIZE`) | `ai/wal.hpp` `WalReader::next()` | Stops gracefully; returns all valid records before the oversized entry |
| RLE encoding | `bit_width` parameter | **0–64** (clamped) | `encoding/rle.hpp` encode + decode | Returns empty result; negative clamped to 0, >64 clamped to 0 |
| BSS encoding | `count × 8` vs `size` | Buffer bounds | `encoding/byte_stream_split.hpp` decode | Returns empty result |
| Python bindings | Column index in `write_column_*` | Schema column count | `python/signet_forge/_bindings.cpp` | Raises `IndexError` |
| `DecisionLogWriter` | `output_dir` path | No `..` segments | `ai/decision_log.hpp` constructor | Returns `IO_ERROR` |
| `DecisionLogWriter` | `chain_id` value | `[a-zA-Z0-9_-]+` only | `ai/decision_log.hpp` constructor | Returns `IO_ERROR` |
| `FeatureWriter` | `output_dir` path | No `..` segments | `ai/feature_writer.hpp` `create()` | Returns `IO_ERROR` |
| Arrow C Data Interface | `ArrowArray::offset` | ≥ 0 and `offset + length ≤ INT64_MAX` | `interop/arrow_bridge.hpp` `import_array()` | Returns `INVALID_ARGUMENT` |
| Arrow C Data Interface | `offset × elem_size` | Overflow check (`SIZE_MAX`) | `interop/arrow_bridge.hpp` | Returns `INVALID_ARGUMENT` |
| Thrift decoder | Collection (list/set/map) size | **1 000 000** (`MAX_COLLECTION_SIZE`) | `thrift/compact.hpp` `skip_field()` | Sets `error_=true` |
| Thrift decoder | List count sign | ≤ `INT32_MAX` | `thrift/compact.hpp` `read_list_header()` | Sets `error_=true`; returns `{0, 0}` |
| Dictionary decoder | Index vs dictionary size | Runtime bounds check | `encoding/dictionary.hpp` | Returns empty (was assert in release) |
| Delta decoder | Accumulation overflow | `__builtin_add_overflow` | `encoding/delta.hpp` | Returns partial result |
| BSS encoding | `count > size / WIDTH` | Division-based overflow guard | `encoding/byte_stream_split.hpp` | Returns empty result |
| AES-GCM | Plaintext size | **~64 GB** (`MAX_GCM_PLAINTEXT`) | `crypto/aes_gcm.hpp` | Returns error (NIST SP 800-38D) |
| Key metadata TLV | Field length | **64 MB** (`MAX_TLV_LENGTH`) | `crypto/key_metadata.hpp` | Returns false |
| MiFID2 / EU AI Act reporters | User string fields | **4 096 chars** (`MAX_FIELD_LENGTH`) | `compliance/mifid2_reporter.hpp`, `eu_ai_act_reporter.hpp` | Truncated with `...[TRUNCATED]` |
| WAL writer | Record payload | > 0 bytes required | `ai/wal.hpp` `WalWriter::append()` | Returns `IO_ERROR` |
| `FeatureWriter` | `output_dir` path | No `..` in raw or canonical path | `ai/feature_writer.hpp` `create()` | Returns `IO_ERROR` |
| `TensorView` | Index bounds | Checked at runtime | `ai/tensor_bridge.hpp` `at()` | Throws `std::out_of_range` |
| Compliance reporters | `time_t` width | `static_assert(sizeof >= 8)` | `mifid2_reporter.hpp`, `eu_ai_act_reporter.hpp` | Compile-time failure on 32-bit time_t |

### Decompression Safety

The bundled Snappy decompressor cross-checks the varint-encoded uncompressed length from the compressed stream against the `uncompressed_page_size` in the Parquet page header. A mismatch returns a `CORRUPT_PAGE` error immediately — no allocation is attempted. The 256 MB `PARQUET_MAX_PAGE_SIZE` cap applies independently as a second defence.

For ZSTD, LZ4, and Gzip (optional external libraries), the cap also applies — the `uncompressed_page_size` guard fires before any codec-specific decompression.

### WAL Recovery Safety

`WalReader` recovers from malformed WAL files by stopping at the first record that fails any of these checks (in order):
1. Bad record magic (`WAL_RECORD_MAGIC` mismatch)
2. Payload size > `WAL_MAX_RECORD_SIZE` (64 MB)
3. Insufficient bytes in file for the claimed payload size
4. CRC32 mismatch on the full record

All records before the first corrupt entry are returned successfully. This design means a truncated or partially-written WAL (e.g. from an unclean shutdown) returns all committed data without error.

### Path Traversal Prevention

`DecisionLogWriter` and `FeatureWriter` both validate their output directory paths at construction time. The validation iterates the `std::filesystem::path` component-by-component and rejects any `..` component, preventing escape from a configured base directory.

```cpp
// Confirmed safe — absolute path with no ".." components
auto w = DecisionLogWriter::create("/data/audit/chain1", "chain-2026-01");

// Rejected — ".." component in path → IO_ERROR
auto w2 = DecisionLogWriter::create("../../etc/cron.d", "chain-evil");
```

### Running All Security Tests

```bash
# Run all hardening tests (12 negative tests across 2 passes)
ctest --test-dir build -L hardening --output-on-failure

# Run WAL-specific hardening test
ctest --test-dir build -R "WalReader stops gracefully" --output-on-failure

# Run feature store path traversal test
ctest --test-dir build -R "FeatureWriter rejects" --output-on-failure

# Run Thrift and reader bomb tests
ctest --test-dir build -R "ThriftCompact|ParquetReader rejects|ArrowArray" --output-on-failure
```

---

## SIMD / AVX2 Optimization

Signet Forge uses **compile-time SIMD dispatch** for AI vector operations (quantization, dot product, L2 distance). The dispatch hierarchy is:

```
AVX2 (8-wide float) → SSE4.2 (4-wide float) → ARM NEON (4-wide float) → Scalar fallback
```

### Default behavior

By default, **no explicit SIMD flags** are set in any CMake preset. Apple Clang 17 on x86_64 defines `__SSE2__` through `__SSE4_2__` but **not** `__AVX2__`. This means:

- **All presets** compile with SSE4.2 (4-wide SIMD) for vector operations
- **AVX2 code paths are not compiled in** unless you add `-march=native`
- The scalar fallback handles any remaining elements after SIMD loops

### Enabling AVX2 for production

If your target CPUs support AVX2 (Intel Haswell 2013+, AMD Excavator 2015+, all modern server CPUs):

```bash
# Server preset with AVX2 enabled
cmake --preset server \
  -DCMAKE_CXX_FLAGS="-march=native -O3"
cmake --build build-server

# Verify AVX2 is compiled in
objdump -d build-server/signet_tests 2>/dev/null | grep -c "vfmadd"
# Non-zero output confirms AVX2 FMA instructions
```

### Performance impact

| SIMD level | Width | Operations affected | Relative speed |
|-----------|-------|-------------------|----------------|
| Scalar | 1 float | All vector ops | 1x (baseline) |
| SSE4.2 (default) | 4 floats | dot_product, l2_distance, quantize | ~3.5x |
| **AVX2** | 8 floats | dot_product, l2_distance, quantize, FMA | ~7x |

The affected operations are in `include/signet/ai/vector_type.hpp` (dot product, L2 distance, normalization) and `include/signet/ai/quantized_vector.hpp` (INT8/INT4 quantization and dequantization).

### Cross-platform portability

| Platform | Default SIMD | Notes |
|----------|-------------|-------|
| x86_64 macOS (Apple Clang) | SSE4.2 | Add `-march=native` for AVX2 |
| x86_64 Linux (GCC/Clang) | SSE2 | Add `-march=native` for AVX2/AVX-512 |
| ARM64 macOS (Apple Silicon) | NEON | 4-wide, comparable to SSE4 |
| ARM64 Linux (Graviton, etc.) | NEON | 4-wide, no AVX equivalent |

No runtime dispatch (CPUID) is used — all paths are resolved at compile time via `#if defined(__AVX2__)` preprocessor guards. This ensures zero overhead from dispatch logic.
