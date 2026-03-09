# Signet Forge — Product Overview

**Standalone C++20 Parquet library with AI-native extensions.**
Zero mandatory dependencies. Header-only core. 5 language bindings. Post-quantum ready.

---

## What It Is

Signet Forge is the first standalone C++ Parquet read/write library that doesn't require
Apache Arrow. It's header-only, compiles with any C++20 compiler, and extends the Parquet
format with capabilities no other library provides — post-quantum encryption, regulatory
compliance audit trails, sub-microsecond streaming, and a native ML feature store.

```cpp
#include "signet/forge.hpp"  // That's it. No Arrow. No linking. No dependencies.
```

---

## What Makes It Different

| Capability | Arrow C++ | parquet-rs | Lance | **Signet Forge** |
|------------|:---------:|:----------:|:-----:|:----------------:|
| Zero mandatory dependencies | | Yes | Yes | **Yes** |
| Header-only core | | | | **Yes** |
| Post-quantum encryption (Kyber-768 + Dilithium-3) | | | | **Yes** |
| Parquet Modular Encryption (AES-256-GCM/CTR) | Yes | Yes | | **Yes** |
| AI decision audit trail (SHA-256 hash chain) | | | | **Yes** |
| MiFID II / EU AI Act compliance reports | | | | **Yes** |
| Sub-microsecond WAL (339 ns fwrite, 38 ns mmap) | | | | **Yes** |
| Parquet-native ML feature store (12 us lookup) | | | | **Yes** |
| Zero-copy Parquet to ONNX Runtime | | | | **Yes** |
| Native vector column (FLOAT32 + INT8/INT4 quantized) | | | Yes | **Yes** |
| Encrypted bloom filters | | | | **Yes** |
| MPMC event bus (9.6 ns push+pop) | | | | **Yes** |

---

## Performance

Measured on macOS x86_64, Apple Clang 17, Release build, 100 samples.

| Operation | Latency | Context |
|-----------|---------|---------|
| WAL append (mmap ring, 32B) | **38 ns** | Zero-copy, single-writer, HFT colocation |
| WAL append (fwrite, 32B) | **339 ns** | Buffered, multi-writer safe |
| MPMC ring push+pop | **9.6 ns** | Single-threaded, 104M ops/s |
| Feature store lookup | **12 us** | Point-in-time correct, binary search |
| Write 10K double rows | **1.25 ms** | BYTE_STREAM_SPLIT encoding |
| Read 50K double rows | **456 us** | Typed column read |
| Encryption overhead (PME) | **< 0.5%** | AES-256-GCM, measured on 1M rows |
| Post-quantum overhead (Kyber) | **< 0.5%** | Hybrid KEM, measured on 1M rows |

### Compression (1M real tick rows)

| Codec | Write Time | vs Uncompressed | Dependency |
|-------|-----------|-----------------|------------|
| LZ4 | 695 ms | 19% faster | Optional (`libzstd`) |
| Gzip | 698 ms | 18% faster | Optional (`zlib`) |
| ZSTD | 710 ms | 17% faster | Optional (`liblz4`) |
| Snappy | 931 ms | 9% slower | **Bundled** (zero-dep) |

---

## Language Support

| Language | Mechanism | Install |
|----------|-----------|---------|
| **C++** | Header-only (native) | `#include "signet/forge.hpp"` |
| **Python** | pybind11 + NumPy | `pip install signet-forge` |
| **Rust** | FFI crate | `signet-forge = "0.1"` in Cargo.toml |
| **JavaScript** | WASM (Emscripten embind) | Load `signet_wasm.js` in browser |
| **C** | Stable C ABI | Link `libsignet_forge_c` |

All bindings share the same battle-tested C++ core. Security hardening and feature coverage
propagate automatically through every language layer.

---

## Security

**151 vulnerabilities fixed** across 5 dedicated hardening passes plus a static audit follow-up,
covering every layer of the stack: core C++ engine, cryptography, Thrift parser, all 5 encodings,
all 4 interop bridges, AI tier, compliance reporters, and every language binding (C FFI, Rust,
Python, WASM).

| Category | Fixes | Examples |
|----------|-------|---------|
| Integer overflow | 12 | Arena allocator, encoding counters, offset arithmetic |
| Buffer safety | 8 | Decoder bounds, column index, shift overflow |
| DoS prevention | 7 | Thrift bombs, page size bombs, decompression bombs |
| Crypto hardening | 6 | Platform CSPRNG, key zeroing, counter overflow guards |
| FFI boundary | 8 | C++ exceptions through extern "C", Rust panics through FFI |
| Path traversal | 3 | FeatureWriter, DecisionLogWriter, StreamingSink |
| Input validation | 8 | Magic bytes, empty records, negative counts |

**Test coverage**: 423 C++ tests + 35 Python tests + 10 Rust tests + 6 fuzz harnesses +
59 enterprise benchmarks. Sanitizer CI: ASan, TSan, UBSan. Coverage reporting via Codecov.

---

## Licensing

| Tier | License | What's Included |
|------|---------|-----------------|
| **Core** (Apache 2.0) | Free for any use | Parquet read/write, all encodings, compression, encryption (including post-quantum), bloom filters, WAL, feature store, event bus, vector types, tensor bridge, Arrow/ONNX/NumPy interop |
| **AI Audit** (BSL 1.1) | Free evaluation; commercial license for production | Audit chain, decision/inference logs, MiFID II reporter, EU AI Act reporter |

**Evaluation grant**: 30 days, 50M rows/month, 3 users, 1 node — no license key needed for
development and testing.

**Change Date**: January 1, 2030 — BSL 1.1 tier converts to Apache 2.0.

Build with `-DSIGNET_BUILD_AI_AUDIT=OFF` to exclude the BSL tier entirely.

---

## Distribution

| Channel | Command |
|---------|---------|
| CMake FetchContent | `FetchContent_Declare(signet_forge GIT_REPOSITORY ...)` |
| vcpkg | `vcpkg install signet-forge` |
| Conan | `conan install signet-forge/0.1.0` |
| conda | `conda install -c conda-forge signet-forge` |
| Homebrew | `brew install signet-forge` |
| PyPI | `pip install signet-forge` |
| WASM | `<script src="signet_wasm.js">` |

---

## Use Cases

### Financial Data Infrastructure
- HFT tick data storage with optimal encodings (DELTA timestamps, BSS prices)
- WAL durable ingestion at sub-microsecond latency
- MiFID II RTS 24 regulatory reporting from tamper-evident audit logs

### AI/ML Pipelines
- Zero-copy Parquet-to-ONNX inference path (no Arrow intermediate)
- Point-in-time feature store serving at 12 us (replaces Redis)
- Native FLOAT32_VECTOR columns with SIMD I/O for embedding storage
- INT8/INT4 quantized vector storage with automatic dequantization

### Compliance & Audit
- SHA-256 hash-chained decision logs for AI trading decisions
- EU AI Act Articles 12, 13, 19 conformity assessment reports
- MiFID II RTS 24 Annex I report generation (JSON/NDJSON/CSV)
- Tamper detection via cryptographic chain verification

### Embedded & Edge
- Header-only core compiles with zero dependencies (`SIGNET_MINIMAL_DEPS=ON`)
- WASM build runs Parquet I/O entirely in the browser — no server upload
- Suitable for IoT devices, microcontrollers, and resource-constrained environments

### Post-Quantum Ready
- Kyber-768 key encapsulation + Dilithium-3 digital signatures
- X25519 Diffie-Hellman key agreement (RFC 7748)
- Hybrid KEM: classical + post-quantum combined encryption
- Future-proof against quantum computing threats to data-at-rest

---

## Quick Start

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

// Define schema
auto schema = Schema::builder("trades")
    .int64("timestamp_ns")
    .double_("price")
    .string("symbol")
    .build();

// Write
auto writer = *ParquetWriter::open("trades.parquet", schema);
writer.write_column<int64_t>(0, timestamps, n);
writer.write_column<double>(1, prices, n);
writer.write_column(2, symbols, n);
writer.flush_row_group();
writer.close();

// Read
auto reader = *ParquetReader::open("trades.parquet");
auto prices = *reader.read_column<double>(0, 1);
```

---

## CI/CD

10 GitHub Actions jobs on every push:

| Job | What It Tests |
|-----|--------------|
| build-test | Linux + macOS matrix |
| asan | Address Sanitizer + UBSan |
| tsan | Thread Sanitizer |
| ubsan | Undefined Behavior Sanitizer |
| windows | MSVC 2022 |
| server-codecs | ZSTD + LZ4 + Gzip |
| post-quantum | Kyber-768 + Dilithium-3 (liboqs) |
| fuzz | libFuzzer, 60s per harness |
| coverage | Clang coverage to Codecov |
| benchmarks | Regression detection (20% alert threshold) |

---

## Architecture

```
signet/forge.hpp (single-include)
    |
    +-- Core I/O (writer, reader, schema, types, thrift)
    +-- Encodings (RLE, DELTA, Dictionary, BYTE_STREAM_SPLIT)
    +-- Compression (Snappy bundled; ZSTD, LZ4, Gzip optional)
    +-- Encryption (AES-GCM/CTR, PME, Kyber-768, Dilithium-3, X25519)
    +-- Bloom Filters (split-block + xxHash64, PME-encrypted)
    +-- AI Vectors (FLOAT32_VECTOR, INT8/INT4 quantized)
    +-- Streaming (WAL fwrite + mmap, MPMC ring, EventBus, StreamingSink)
    +-- Feature Store (FeatureWriter, FeatureReader, point-in-time)
    +-- Audit Trail (SHA-256 chain, decision log, inference log) [BSL 1.1]
    +-- Compliance (MiFID II RTS 24, EU AI Act Art. 12/13/19) [BSL 1.1]
    +-- Interop (Arrow C Data, ONNX Runtime, NumPy/DLPack)
```

---

*Signet Forge v0.1.0 | Apache 2.0 + BSL 1.1 | C++20 | github.com/SIGNETSTACK/signet-forge*

*Contact: johnson@signetstack.io*
