# Signet Forge

**Standalone C++20 Parquet library with AI-native extensions.**
Zero mandatory dependencies. Header-mostly. Interoperable with Arrow, DuckDB, Spark, and Polars.

[![CI](https://github.com/SIGNETSTACK/SIGNET_FORGE/actions/workflows/ci.yml/badge.svg)](https://github.com/SIGNETSTACK/SIGNET_FORGE/actions)
[[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Python](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](python/)
[![codecov](https://codecov.io/gh/SIGNETSTACK/SIGNET_FORGE/graph/badge.svg)](https://codecov.io/gh/SIGNETSTACK/SIGNET_FORGE)

[**Try it in your browser — drag & drop any .parquet file**](https://signetstack.github.io/SIGNET_FORGE/demo/) | [**API Reference**](https://signetstack.github.io/SIGNET_FORGE/)

---

## Why Signet?

Every existing C++ Parquet library requires Apache Arrow (~400 MB). Nobody has built the
AI-native capabilities the regulation-era demands. SignetForge fills five white spaces:

| Gap | What SignetForge provides |
|-----|---------------------|
| **No standalone C++ Parquet** | Header-only core — `#include "signet/forge.hpp"`, link nothing |
| **No post-quantum encryption** | Kyber-768 KEM + Dilithium-3 signatures per [NIST FIPS 203/204](https://csrc.nist.gov/pubs/fips/203/final) — first in any Parquet library |
| **No AI audit trail** | SHA-256 hash-chained decision logs compliant with MiFID II RTS 24 and EU AI Act Art. 12/19 |
| **No sub-μs streaming** | Dual-mode WAL: **339 ns** (fwrite, general purpose) and **~223 ns** (mmap ring, measured) |
| **No Parquet feature store** | Point-in-time correct feature retrieval at **sub-μs** per entity (with row group cache) — no Redis needed |

### Regulatory Enforcement Timeline

**EU AI Act Art. 12/13** (automatic, continuous AI logs) entered force 1 August 2024.
**DORA** (ICT incident management) obligations applied from January 2025.
**MiFID II RTS 24** algorithm identification records have been required since 2018.
**GDPR Art. 32** encrypted-at-rest obligations for personal data apply since 2018.

Most algorithmic trading firms have not closed any of these gaps correctly — not because of
negligence, but because no infrastructure existed that fit within production HFT latency budgets.
Signet Forge was designed to close all five from the first commit.

### Enterprise Readiness Score

Independently benchmarked across 100 points (Data I/O · ML Inference · Cryptography · Regulatory
Compliance · Audit Trail · SIMD · Operational Maturity) against eight enterprise systems:

| System | Score /100 | Primary strength |
|--------|:----------:|-----------------|
| **Signet Forge** | **95** | Only system with compliance-native AI audit + PME + sub-250 ns WAL |
| kdb+/q (KX Systems) | 64 | 30+ yr production HFT maturity; no crypto or regulatory reports |
| Apache Iceberg | 47 | Data lake lineage; no HFT latency, no crypto, no AI audit |
| Apache Arrow C++ | 46 | Raw throughput (6,000 MB/s Flight¹); no compliance layer |
| parquet-rs | 40 | Fast footer parse (3–9× vs C++²); Rust-only, no AI extensions |
| ArcticDB | 37 | 40 GB/s flash read³; proprietary, no regulatory reports |
| Lance | 35 | Vector-optimised; no crypto, no compliance |
| ONNX Runtime | 33 | Best-in-class model execution; no data layer or audit trail |
| Feast | 28 | Feature serving; ms-range latency, no crypto, no audit chain |

¹ [ACM BID'22 Arrow Flight benchmark](https://dl.acm.org/doi/10.1145/3674399.3674498)
² [Apache Arrow Blog, Oct 2025](https://arrow.apache.org/blog/2025/10/23/rust-parquet-metadata/)
³ [ArcticDB documentation](https://docs.arcticdb.io/latest/)

---

## Feature Matrix

| Capability | Arrow C++ | parquet-rs | Lance | **SignetForge** |
|------------|:---------:|:----------:|:-----:|:----------:|
| Zero mandatory dependencies | ❌ | ✅ | ✅ | ✅ |
| Header-only core | ❌ | ❌ | ❌ | ✅ |
| C++20 type-safe API | ❌ | — | — | ✅ |
| Post-quantum encryption (Kyber-768) | ❌ | ❌ | ❌ | ✅ |
| Parquet Modular Encryption (PME) | ✅ | ✅ | ❌ | ✅ |
| Encrypted bloom filters | ❌ | ❌ | ❌ | ✅ |
| AI decision audit trail | ❌ | ❌ | ❌ | ✅ |
| MiFID II / EU AI Act reports | ❌ | ❌ | ❌ | ✅ |
| Sub-μs streaming WAL (fwrite 339 ns + mmap ~223 ns) | ❌ | ❌ | ❌ | ✅ |
| Native vector column type | ❌ | ❌ | ✅ | ✅ |
| Zero-copy Parquet → ONNX | ❌ | ❌ | ❌ | ✅ |
| Parquet-native feature store | ❌ | ❌ | ❌ | ✅ |
| Python bindings (NumPy) | ✅ | ✅ | ✅ | ✅ |
| BYTE_STREAM_SPLIT for floats | ✅ | ✅ | ❌ | ✅ |
| DELTA_BINARY_PACKED timestamps | ✅ | ✅ | ❌ | ✅ |
| SIMD vector ops (AVX2 / SSE / NEON) | ❌ | ❌ | partial | ✅ |
| INT8/INT4 quantized vector columns | ❌ | ❌ | ✅ | ✅ |

---

## Quickstart

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(signet_forge
    GIT_REPOSITORY https://github.com/SIGNETSTACK/signet-forge.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(signet_forge)
target_link_libraries(my_app PRIVATE signet::forge)
```

### Write a Parquet file

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

auto schema = Schema::builder("tick_data")
    .int64("timestamp_ns")
    .double_("price")
    .string("symbol")
    .build();

auto writer = *ParquetWriter::open("ticks.parquet", schema);

int64_t ts[]    = {1706780400000000000LL, 1706780400000100000LL};
double  price[] = {45123.50, 45124.00};
std::string sym[] = {"BTCUSDT", "BTCUSDT"};

writer.write_column<int64_t>(0, ts,    2);
writer.write_column<double> (1, price, 2);
writer.write_column         (2, sym,   2);
writer.flush_row_group();
writer.close();
```

### Read it back

```cpp
auto reader = *ParquetReader::open("ticks.parquet");
auto prices = *reader.read_column<double>(0, 1);  // row group 0, column 1
// prices[0] == 45123.50, prices[1] == 45124.00
```

### Python

```bash
pip install signet-forge
```

Pre-built wheels for Linux (x86_64, aarch64), macOS (x86_64, arm64), and Windows (AMD64) — Python 3.10–3.12.

```python
import signet_forge as sp, numpy as np

schema = sp.SchemaBuilder("ticks").int64_ts("ts").double_("price").string("sym").build()
with sp.ParquetWriter.open("ticks.parquet", schema) as w:
    w.write_column_int64 (0, np.arange(1_000_000, dtype=np.int64))
    w.write_column_double(1, np.random.uniform(40_000, 50_000, 1_000_000))
    w.write_column_string(2, ["BTCUSDT"] * 1_000_000)
    w.flush_row_group()

reader = sp.ParquetReader.open("ticks.parquet")
prices = reader.read_column_double(0, 1)  # → np.ndarray[float64]
```

---

## AI-Native Extensions

### Dual-Mode Write-Ahead Log

SignetForge provides two WAL modes with identical on-disk formats. Choose based on your latency requirements.

**General purpose (WalWriter, fwrite, 339 ns):**

```cpp
#include "signet/ai/wal.hpp"
using namespace signet::forge;

auto wal = *WalWriter::open("/data/events/live.wal");
// 339 ns per append (32 B payload, buffered, no fsync)
wal.append("TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000");
wal.flush();  // fflush only — no kernel syscall
```

**HFT colocation (WalMmapWriter, mmap ring, ~223 ns):**

```cpp
#include "signet/ai/wal_mapped_segment.hpp"
using namespace signet::forge;

WalMmapOptions opts;
opts.dir           = "/fast/nvme/wal";
opts.ring_segments = 4;            // 4 × 64 MB ring = 256 MB total
opts.segment_size  = 64 * 1024 * 1024;
opts.sync_on_append = false;       // crash-safe; set sync_on_flush=true for MiFID II

auto writer = *WalMmapWriter::open(opts);
// ~223 ns per append (mmap ring, no sync, single-writer)
auto seq = writer->append(tick_data, tick_size);
// WalReader reads mmap segments identically to WalWriter files — same format
```

### Point-in-Time Feature Store

Serve ML features at **sub-μs** per entity lookup without Redis or a separate serving layer.

```cpp
#include "signet/ai/feature_writer.hpp"
#include "signet/ai/feature_reader.hpp"
using namespace signet::forge;

// Write features
FeatureWriterOptions wo;
wo.output_dir = "/data/features/alpha";
wo.group      = {"alpha_v1", {"momentum", "volatility", "spread", "imbalance"}, 1};
auto fw = *FeatureWriter::create(wo);
fw.write({"BTCUSDT", 1706780400000000000LL, 1, {0.82f, 0.14f, 0.003f, 0.61f}});
fw.close();

// Point-in-time read — binary search, no disk I/O after open()
FeatureReaderOptions ro;
ro.parquet_files = fw.output_files();
auto fr = *FeatureReader::open(ro);
auto fv = *fr.as_of("BTCUSDT", 1706780400000000000LL);
// fv.values == {0.82, 0.14, 0.003, 0.61}
```

### MiFID II + EU AI Act Compliance Reports

Generate cryptographically-verified audit reports directly from tamper-evident decision logs.

```cpp
#include "signet/ai/decision_log.hpp"
#include "signet/ai/compliance/mifid2_reporter.hpp"
#include "signet/ai/compliance/eu_ai_act_reporter.hpp"
using namespace signet::forge;

// Log every AI trading decision — SHA-256 hash chain, tamper-evident
DecisionLogWriter log("/data/audit", "chain-prod-001");
log.log({.timestamp_ns=1706780400000000000LL, .strategy_id=42,
         .model_version="alpha-v2.1", .decision_type=DecisionType::ORDER_NEW,
         .confidence=0.89f, .risk_result=RiskGateResult::PASSED,
         .order_id="ORD-20240201-001", .symbol="BTCUSDT",
         .price=45123.50, .quantity=0.1, .venue="BINANCE"});
log.close();

// Generate MiFID II RTS 24 Annex I report (JSON/NDJSON/CSV)
ReportOptions opts;
opts.firm_id = "FIRM-LEI-123";
auto report = *MiFID2Reporter::generate({log.current_file_path()}, opts);
// report.chain_verified == true
// report.content → 16-field JSON per decision

// EU AI Act Article 19 conformity assessment
auto conformity = *EUAIActReporter::generate_article19({log.current_file_path()}, {});
// conformity_status: "CONFORMANT" when both decision + inference chains verified
```

---

## Performance

Numbers measured on macOS (x86_64, Apple Clang 17, Release build, 100 samples).

### Write / Read Throughput

| Operation | Mean | Notes |
|-----------|------|-------|
| Write 10K `float64` rows | 1.36 ms | BYTE_STREAM_SPLIT encoding, UNCOMPRESSED |
| Read 50K `float64` rows | 530 μs | Typed `read_column<double>`, PLAIN |
| Write 10K `int64` rows | ~0.9 ms | PLAIN encoding |

### WAL Append Latency

> `bench_wal.cpp` Cases 1–12. Run: `./build-benchmarks/signet_benchmarks "[wal][bench]" --benchmark-samples 200`

| Operation | Mean | Benchmark tag (Case #) | Why |
|-----------|------|------------------------|-----|
| `WalWriter` single append (32 B) | **339 ns** | `"append 32B"` (Case 1) | Baseline: fwrite, buffered, no fsync |
| `WalWriter` single append (256 B) | ~450 ns | `"append 256B"` (Case 2) | Baseline; larger memcpy + CRC |
| `WalWriter` append + flush (fflush) | ~600 ns | `"append + flush(no-fsync)"` (Case 4) | fflush only, no kernel sync |
| `WalManager` append (mutex + roll) | ~400–450 ns | `"manager append 32B"` (Case 5) | +60–110 ns vs WalWriter: mutex lock/unlock + segment roll check + counter |
| `WalMmapWriter` single append (32 B) | **~223 ns** | `"mmap append 32B"` (Case 7) | 9× vs WalWriter: no stdio buf, no mutex, direct store + release fence (free on x86_64 TSO) |
| `WalMmapWriter` single append (256 B) | **~223 ns** | `"mmap append 256B"` (Case 8) | Only payload-proportional cost: memcpy(size) + CRC32(size) |
| `WalMmapWriter` with rotation (amortized) | **~223 ns** | `"mmap append 32B"` (Case 7) | Pre-allocated STANDBY; rotation = atomic CAS, ~5 ns amortized |
| fwrite vs mmap side-by-side | see above | Cases 11 & 12 | Catch2 reports all three adjacent; ratio directly visible |

### Compression Comparison (1M real tick rows, enterprise suite, 100 samples)

| Codec | Write Time | vs Uncompressed | Notes |
|-------|-----------|-----------------|-------|
| **ZSTD L3** | **662 ms** | **31% faster** | Best ratio |
| **LZ4** | **705 ms** | **27% faster** | Fastest decompressor |
| Gzip | 706 ms | 27% faster | — |
| Snappy | 898 ms | 7% faster | Bundled, zero-dep |
| Uncompressed | 966 ms | baseline | — |

### Encryption Overhead (1M rows, 100 samples)

| Mode | Write | Read | Roundtrip | Overhead |
|------|-------|------|-----------|----------|
| Plain (Snappy) | 1.625 s | 1.862 s | 1.572 s | — |
| AES-256-GCM (PME) | 1.614 s | 1.806 s | 1.578 s | < 0.5% |
| Kyber-768 KEM (PQ) | 1.610 s | 1.812 s | 1.581 s | < 0.5% |

### Encoding Speed (10K values)

| Encoding | Encode | Decode | Size vs PLAIN |
|----------|--------|--------|---------------|
| DELTA int64 (timestamps) | 29 μs | 43 μs | **<50%** (monotonic data) |
| BYTE_STREAM_SPLIT float64 | ~35 μs | ~30 μs | 100% (reorganises for downstream compression) |
| RLE bool (90% zeros) | ~8 μs | ~6 μs | **<10%** (long run lengths) |

### AI Infrastructure

| Operation | Mean | Notes |
|-----------|------|-------|
| Feature `as_of()` lookup | ~0.14 μs | Per-call with row group cache, warm index |
| Feature `as_of_batch()` (100 entities) | ~19 μs | Single timestamp, 100 entities, cached row group |
| EventBus publish+pop, single-thread | ~53 ns | Lock-free atomic shared_ptr (no mutex) |
| MPMC ring push+pop | **10.4 ns** | Single-threaded, `int64_t`, 96M ops/s |
| MPMC ring 4P × 4C | ~70 ns/op | 4 producers, 4 consumers, concurrent |

### HFT Inference Benchmark (Production, 39 ONNX Models)

Run against **39 RandomForestClassifier ONNX models** from an active HFT system using **1.5 million
real tick events** across AAPL, GOOGL, BTC-USD, ES=F, NQ=F, and CL=F. Feature matrix: 1,024 rows ×
15 features per inference. Zero-copy path: `ColumnBatch::as_tensor()` → `prepare_for_onnx()` →
`Ort::Value::CreateTensor()` — same memory pointer through all three steps.

| Metric | Result |
|--------|--------|
| Models executed | 39 / 39 |
| P95 inference latency (best model) | **4.6 µs** |
| P95 inference latency (mean across 39 models) | **18.4 µs** |
| Models under 100 µs P95 | **38 / 39** |
| Parquet Modular Encryption (PME) | ✅ AES-256-GCM/CTR verified |
| Tamper-evident audit chain | ✅ 39 entries, SHA-256, all verified |
| EU AI Act Art.12 report | ✅ 22.1 KB structured JSON |
| MiFID II RTS 24 report | ✅ 4.8 KB, 39 algorithm identifiers |
| Zero-copy tensor bridge | ✅ same pointer, pointer-level verified |

The audit chain terminal hash `ec4c278375f7dea470a72b874492e8bac9b78ce6683b72894a0e4f6cd633a25f`
is a cryptographic commitment to all 39 inference events. Altering any single record breaks every
subsequent link. This is what tamper evidence means at the infrastructure level.

### SIMD Acceleration

Native SIMD intrinsics across the AI vector and quantization hot paths. Compile-time dispatch
with **mandatory scalar fallback** — no forced CPU baseline. Binary runs correctly on any
x86-64 or ARM CPU, faster where hardware supports it.

| Operation | AVX2 (8-wide) | SSE (4-wide) | ARM NEON (4-wide) | Speedup vs scalar |
|-----------|:---:|:---:|:---:|---|
| `dot_product(a, b, n)` | ✅ `_mm256_fmadd_ps` | ✅ `_mm_mul_ps` | ✅ `vmlaq_f32` | **8x** (AVX2) |
| `l2_distance_sq(a, b, n)` | ✅ `_mm256_sub_ps` + FMA | ✅ | ✅ `vsubq_f32` | **8x** (AVX2) |
| `l2_normalize(v, n)` | ✅ `_mm256_mul_ps` | ✅ | ✅ `vmulq_f32` | **8x** (AVX2) |
| `copy_floats(dst, src, n)` | ✅ `_mm256_loadu/storeu_ps` | ✅ | ✅ `vld1q/vst1q_f32` | **8x** (AVX2) |
| `quantize_int8(in, out, dim)` | ✅ `_mm256_cvtps_epi32` + pack | ✅ | ✅ `vcvtq_s32_f32` | **6–8x** (AVX2) |
| `dequantize_int8(in, out, dim)` | ✅ `_mm_cvtepi8_epi16` + `_mm256_cvtepi32_ps` | ✅ | — | **6–8x** (AVX2) |
| AES-NI detection | CPUID ECX[25] | — | `__ARM_FEATURE_CRYPTO` | infrastructure ready |

**~40 unique intrinsics**, ~2,688 lines of SIMD code. All paths validated against scalar
reference implementations with property-based tests across sizes that break 8-wide alignment.

Detection is compile-time: `#if defined(__AVX2__)` / `#elif defined(__SSE4_2__)` /
`#elif defined(__ARM_NEON)`. No runtime dispatch overhead on the hot path.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          signet/forge.hpp                             │
│                        (single-include header)                          │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        ▼                       ▼                       ▼
┌──────────────┐    ┌──────────────────┐    ┌──────────────────────────┐
│  Core I/O    │    │   Encodings      │    │   Compression            │
│  writer.hpp  │    │   rle.hpp        │    │   snappy.hpp (bundled)   │
│  reader.hpp  │    │   delta.hpp      │    │   zstd.hpp   (optional)  │
│  schema.hpp  │    │   dictionary.hpp │    │   lz4.hpp    (optional)  │
│  types.hpp   │    │   byte_stream_   │    │   gzip.hpp   (optional)  │
│  thrift/     │    │   split.hpp      │    └──────────────────────────┘
└──────┬───────┘    └──────────────────┘
       │
       ▼
┌──────────────────────────────────────────────────────────────────────┐
│  Encryption layer                                                     │
│  crypto/sha256.hpp (FIPS 180-4)  crypto/sha512.hpp (FIPS 180-4)     │
│  crypto/aes_gcm.hpp (footer)  crypto/aes_ctr.hpp (columns)           │
│  crypto/hkdf.hpp (RFC 5869)  crypto/pme.hpp (orchestrator)           │
│  crypto/post_quantum.hpp (Kyber-768, X25519)                         │
│  bloom/split_block.hpp + xxhash.hpp (encrypted bloom filters)        │
└───────────────────────────────┬──────────────────────────────────────┘
                                │
        ┌───────────────────────┼────────────────────────────┐
        ▼                       ▼                            ▼
┌──────────────┐    ┌──────────────────────┐    ┌──────────────────────┐
│  AI Vectors  │    │  Streaming / WAL     │    │  ML Feature Store    │
│  vector_type │    │  wal.hpp             │    │  feature_writer.hpp  │
│  quantized_  │    │  streaming_sink.hpp  │    │  feature_reader.hpp  │
│  vector.hpp  │    │  mpmc_ring.hpp       │    │  (point-in-time,     │
│              │    │  column_batch.hpp    │    │   history, batch)    │
│              │    │  event_bus.hpp       │    │                      │
└──────────────┘    └──────────────────────┘    └──────────────────────┘
        │                                                    │
        └──────────────────────┬─────────────────────────────┘
                               ▼
┌──────────────────────────────────────────────────────────────────────┐
│  AI Audit Trail + Compliance                                          │
│  audit_chain.hpp    decision_log.hpp    inference_log.hpp            │
│  tensor_bridge.hpp  (zero-copy → ONNX OrtValue)                      │
│  compliance/mifid2_reporter.hpp   (MiFID II RTS 24)                  │
│  compliance/eu_ai_act_reporter.hpp (EU AI Act Art.12/13/19)          │
└──────────────────────────────────────────────────────────────────────┘
        │
        ▼
┌──────────────────────────────────────────────────────────────────────┐
│  Interop                                                              │
│  interop/arrow_bridge.hpp   (Arrow C Data Interface, zero-copy)      │
│  interop/onnx_bridge.hpp    (OrtValue from Parquet columns)          │
│  interop/numpy_bridge.hpp   (DLPack / buffer protocol)               │
│  python/signet_forge/     (pybind11 bindings, NumPy arrays)        │
└──────────────────────────────────────────────────────────────────────┘
```

---

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `SIGNET_BUILD_TESTS` | `OFF` | Build Catch2 test suite (FetchContent v3.7.1) |
| `SIGNET_BUILD_BENCHMARKS` | `OFF` | Build Catch2 benchmark suite |
| `SIGNET_BUILD_EXAMPLES` | `OFF` | Build example programs |
| `SIGNET_BUILD_TOOLS` | `OFF` | Build `signet_cli` inspection tool |
| `SIGNET_BUILD_PYTHON` | `OFF` | Build pybind11 Python module (`_bindings.so`) |
| `SIGNET_ENABLE_ZSTD` | `OFF` | Link `libzstd` for ZSTD compression |
| `SIGNET_ENABLE_LZ4` | `OFF` | Link `liblz4` for LZ4 compression |
| `SIGNET_ENABLE_GZIP` | `OFF` | Link `zlib` for Gzip compression |
| `SIGNET_ENABLE_CRYPTO` | `OFF` | Enable real AES-256-GCM/CTR (otherwise stub) |
| `SIGNET_ENABLE_PQ` | `OFF` | Enable Kyber-768 + Dilithium-3 post-quantum crypto |

### CMakePresets

```bash
cmake --preset dev-tests   # Debug + tests  → build/
cmake --preset benchmarks  # Release + bench → build-benchmarks/
cmake --preset python      # Release + pybind11 → build-python/
cmake --preset asan        # ASan + UBSan → build-asan/
cmake --preset tsan        # TSan → build-tsan/
cmake --preset ci          # RelWithDebInfo + all targets → build-ci/
```

### Mutation Testing

Signet Forge includes a [`mull.yml`](mull.yml) configuration for [Mull](https://github.com/mull-project/mull) 0.24.0,
restricting mutation analysis to project source (`include/signet/.*`) and excluding third-party
dependencies. See [docs/QUALITY_ASSURANCE.md](docs/QUALITY_ASSURANCE.md#mutation-testing) for
step-by-step local setup instructions. CI runs mutation testing automatically on every push to `main`.

---

## Verification & Quality Assurance

```
830 unit tests   (100% pass)   cmake --preset server-pq && ctest
104 benchmark cases             45 core + 59 enterprise (real tick data, 8 phases)
 11 fuzz harnesses              libFuzzer + ASan, 60s each in CI
 35 Python tests (100% pass)    PYTHONPATH=python pytest python/tests/
 10 Rust tests   (100% pass)    cd rust/signet-forge && cargo test
```

### Dynamic Testing

Every push triggers **16 CI jobs** including three sanitizer modes (ASan, TSan, UBSan) with
commercial crypto enabled, 11 libFuzzer harnesses with persistent corpus caching, CodeQL
SAST with `security-extended` queries, property-based tests (C++ + Python Hypothesis),
mutation testing on the crypto module, 13 fault injection resilience tests, 32-thread
concurrency stress tests, and Clang source-based code coverage reported to Codecov.
Benchmark regressions >100% **fail the build**.

| Layer | Coverage | Standard |
|-------|----------|----------|
| **Sanitizers** | ASan + LSan + UBSan + TSan on every push | [NIST SP 800-53 SI-16](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) |
| **Fuzz testing** | 11 harnesses: parsers, crypto, interop | [NIST SP 800-53 SA-11(8)](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) |
| **SAST** | CodeQL (`security-extended`) + MSVC `/analyze` | [SOC 2 CC7.1](https://www.aicpa-cima.com/resources/landing/system-and-organization-controls-soc-suite-of-services) |
| **SBOM** | CycloneDX + SPDX JSON on every release | [US EO 14028](https://www.federalregister.gov/documents/2023/04/27/2023-08823/agency-information-collection-activities-request-for-comment-on-secure-software-development), [EU CRA](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R2847) |
| **Mutation testing** | Mull 0.24.0 on crypto module | [NIST SP 800-53 SA-11(9)](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) |
| **Resilience testing** | 13 fault injection tests (corruption, truncation, garbage) | [NIST SP 800-53 SI-10](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) |
| **Concurrency stress** | 32-thread MPMC + EventBus (16P×16C, 100K items) | [DORA Art. 9](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32022R2554) |
| **Secrets scan** | gitleaks on full history | [PCI DSS v4.0 §8.6.3](https://www.pcisecuritystandards.org/document_library/) |

### Cryptographic Validation

**Test vectors**: [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final)
(18 GCM vectors including Test Case 15), [NIST SP 800-38A](https://csrc.nist.gov/pubs/sp/800/38/a/final)
(CTR vectors), [FIPS 140-3 §4.9.2](https://csrc.nist.gov/pubs/fips/140-3/final) CRNGT, and
[Google Wycheproof](https://github.com/google/wycheproof) edge-case suites for AES-256-GCM
(tag tampering, ciphertext modification, empty plaintext) and X25519 (low-order points,
non-canonical coordinates, twist attacks, scalar clamping).

### Security Hardening

**484 vulnerabilities** identified and fixed across 12 dedicated audit passes (including external pentest) — zero open
findings. 100+ hardening tests (`ctest -L hardening`) verify safe rejection of malformed,
adversarial, and boundary-condition inputs across all layers: crypto (constant-time GHASH,
GCM/CTR counter overflow, secure key zeroing), encoders (RLE/BSS/Delta/Dictionary boundary
values), parsers (Thrift DoS, decompression bombs), interop (Arrow overflow, RAII), AI tier
(race conditions, hash chain integrity), and all language bindings (C FFI exception safety,
Rust panic safety, Python use-after-free, WASM bounds checking).

### Enterprise Compliance

**92 of 92** compliance gaps resolved across 12 passes: [FIPS 140-3](https://csrc.nist.gov/pubs/fips/140-3/final),
[EU AI Act](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R1689),
[MiFID II RTS 24](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32017R0580)/[RTS 6](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32017R0589),
[GDPR](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32016R0679),
[DORA](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32022R2554), and Parquet PME spec.
101 dedicated compliance tests verify report structure, field content, hash-chain integrity,
and regulatory framework requirements.

---

## Roadmap

| Milestone | Description |
|-----------|-------------|
| **v0.2** | Doxygen/mdBook API reference site |
| **v0.3** | Rust FFI crate (`signet-forge-sys`) |
| ~~**v0.4**~~ | ~~libFuzzer fuzz harness on reader~~ — **Done** (11 harnesses in `fuzz/`, CI integrated) |
| ~~**v0.4**~~ | ~~`pip install signet-forge` on PyPI (wheels for Linux/macOS/Windows)~~ — **Done** (cibuildwheel CI, Trusted Publishers) |
| **v1.0** | Stable ABI, WASM target, columnar streaming over gRPC |

---

## API Stability

Signet Forge follows [Semantic Versioning](https://semver.org/).

**Pre-v1.0** (current): All APIs are subject to change. Breaking changes are documented in
`CHANGELOG.md` and trigger a minor version bump (e.g., 0.1 → 0.2).

**Post-v1.0 stability tiers:**

| Tier | Scope | Guarantee |
|------|-------|-----------|
| **Stable** | `ParquetWriter`, `ParquetReader`, `Schema`, `types.hpp`, all encodings, all compression codecs | Semver — breaking changes require major version bump |
| **Evolving** | AI extensions: WAL, feature store, event bus, audit chain, compliance reporters | May change in minor versions with deprecation notices |
| **Internal** | `thrift/`, encoding internals (`RleDecoder`, `delta::*`), `memory.hpp` | No stability guarantee — may change in any release |

---

## Licensing

Signet Forge is licensed under the **[GNU Affero General Public License v3.0 or later (AGPL-3.0-or-later)](LICENSE)**
with a **[Commercial License Exception](LICENSE_COMMERCIAL)** available for proprietary and hosted deployments.

### AGPL-3.0 (open source)

The full Signet Forge codebase — Parquet engine, all encodings, compression, post-quantum
encryption, bloom filters, AI audit trail, MiFID II / EU AI Act reporters, streaming WAL,
feature store, and all compliance modules — is freely available under AGPL-3.0.

**You may use Signet Forge at no cost provided your project complies with AGPL-3.0.**
This means: if you distribute a product incorporating Signet Forge, or operate it as a
network-accessible service, you must make the complete corresponding source code of that
product/service available under AGPL-3.0.

Open source projects, academic research, and government use under AGPL-3.0 require no
additional agreement.

### Commercial License Exception

Organisations that cannot or do not wish to comply with AGPL-3.0's source-disclosure
obligations may purchase a **Commercial License Exception** from Signet Stack. This grants:

- The right to incorporate Signet Forge in closed-source, proprietary products
- The right to operate Signet Forge as a SaaS or hosted service without triggering AGPL §13
- The right to modify Signet Forge internally without publishing modifications

See [LICENSE_COMMERCIAL](LICENSE_COMMERCIAL) for tier details and terms.

**Commercial licensing inquiries**: **johnson@signetstack.io**

### Contributor License Agreement

External contributors must sign the [CLA](CLA.md) before pull requests can be merged.
The CLA grants Signet Stack the rights needed to offer the dual-license model.

---

## Contributing

1. Fork → branch → PR against `main` (CLA required — see [CLA.md](CLA.md))
2. `cmake --preset dev-tests && cmake --build build && cd build && ctest` must be green
3. New public API requires tests in `tests/` and a benchmark in `benchmarks/`
4. Compliance-related code (audit trail, reporters) must cite the specific regulatory article

---

*Built with C++20. Tested on macOS (Apple Clang 17) and Ubuntu 24.04 (GCC 13, Clang 18).*

## Local Commercial Runner

Use `scripts/signet_local_commercial_run.sh` to compute `SIGNET_COMMERCIAL_LICENSE_HASH` from your runtime key, configure commercial mode, build, and run in one command.

```bash
export SIGNET_COMMERCIAL_LICENSE_KEY='tier=evaluation;max_rows_month=50000000;max_users=5;max_nodes=2;max_days=30;expires_at=2026-12-31'
scripts/signet_local_commercial_run.sh -- '/path/to/your/ticks.csv.gz'
```

Strict real-PQ local run (no SHA stub fallback):

```bash
scripts/signet_local_commercial_run.sh --enable-pq ON --require-real-pq ON --target signet_tests
```
