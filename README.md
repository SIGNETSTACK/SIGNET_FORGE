# Signet Forge

**Standalone C++20 Parquet library with AI-native extensions.**
Zero mandatory dependencies. Header-mostly. Interoperable with Arrow, DuckDB, Spark, and Polars.

[![CI](https://github.com/SIGNETSTACK/signet-forge/actions/workflows/ci.yml/badge.svg)](https://github.com/SIGNETSTACK/signet-forge/actions)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Python](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](python/)
[![codecov](https://codecov.io/gh/SIGNETSTACK/signet-forge/graph/badge.svg)](https://codecov.io/gh/SIGNETSTACK/signet-forge)

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
| **No sub-μs streaming** | Dual-mode WAL: **339 ns** (fwrite, general purpose) and **~38 ns** (mmap ring, HFT colocation) |
| **No Parquet feature store** | Point-in-time correct feature retrieval at **12 μs** per entity — no Redis needed |

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
| Sub-μs streaming WAL (fwrite 339 ns + mmap ~38 ns) | ❌ | ❌ | ❌ | ✅ |
| Native vector column type | ❌ | ❌ | ✅ | ✅ |
| Zero-copy Parquet → ONNX | ❌ | ❌ | ❌ | ✅ |
| Parquet-native feature store | ❌ | ❌ | ❌ | ✅ |
| Python bindings (NumPy) | ✅ | ✅ | ✅ | ✅ |
| BYTE_STREAM_SPLIT for floats | ✅ | ✅ | ❌ | ✅ |
| DELTA_BINARY_PACKED timestamps | ✅ | ✅ | ❌ | ✅ |

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

**HFT colocation (WalMmapWriter, mmap ring, ~38 ns):**

```cpp
#include "signet/ai/wal_mapped_segment.hpp"
using namespace signet::forge;

WalMmapOptions opts;
opts.dir           = "/fast/nvme/wal";
opts.ring_segments = 4;            // 4 × 64 MB ring = 256 MB total
opts.segment_size  = 64 * 1024 * 1024;
opts.sync_on_append = false;       // crash-safe; set sync_on_flush=true for MiFID II

auto writer = *WalMmapWriter::open(opts);
// ~38 ns per append (mmap ring, no sync, single-writer)
auto seq = writer->append(tick_data, tick_size);
// WalReader reads mmap segments identically to WalWriter files — same format
```

### Point-in-Time Feature Store

Serve ML features at **12 μs** per entity lookup without Redis or a separate serving layer.

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

Numbers measured on macOS (x86_64, Apple Clang 17, Release build, 50–100 samples).

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
| `WalMmapWriter` single append (32 B) | **~38 ns** | `"mmap append 32B"` (Case 7) | 9× vs WalWriter: no stdio buf, no mutex, direct store + release fence (free on x86_64 TSO) |
| `WalMmapWriter` single append (256 B) | **~42 ns** | `"mmap append 256B"` (Case 8) | Only payload-proportional cost: memcpy(size) + CRC32(size) |
| `WalMmapWriter` with rotation (amortized) | **~38 ns** | `"mmap append 32B"` (Case 7) | Pre-allocated STANDBY; rotation = atomic CAS, ~5 ns amortized |
| fwrite vs mmap side-by-side | see above | Cases 11 & 12 | Catch2 reports all three adjacent; ratio directly visible |

### Compression Comparison (1M real tick rows, enterprise suite)

| Codec | Write Time | vs Uncompressed | Notes |
|-------|-----------|-----------------|-------|
| **LZ4** | **695 ms** | **19% faster** | Fastest codec |
| Gzip | 698 ms | 18% faster | — |
| ZSTD L3 | 710 ms | 17% faster | Best ratio |
| Uncompressed | 857 ms | baseline | — |
| Snappy | 931 ms | 9% slower | Bundled, zero-dep |

### Encryption Overhead (1M rows, measured)

| Mode | Write | Read | Roundtrip | Overhead |
|------|-------|------|-----------|----------|
| Plain (Snappy) | 1.459 s | 1.757 s | 1.717 s | — |
| AES-256-GCM (PME) | 1.457 s | 1.748 s | 1.735 s | < 0.5% |
| Kyber-768 KEM (PQ) | 1.459 s | 1.740 s | 1.725 s | < 0.5% |

### Encoding Speed (10K values)

| Encoding | Encode | Decode | Size vs PLAIN |
|----------|--------|--------|---------------|
| DELTA int64 (timestamps) | 29 μs | 43 μs | **<50%** (monotonic data) |
| BYTE_STREAM_SPLIT float64 | ~35 μs | ~30 μs | 100% (reorganises for downstream compression) |
| RLE bool (90% zeros) | ~8 μs | ~6 μs | **<10%** (long run lengths) |

### AI Infrastructure

| Operation | Mean | Notes |
|-----------|------|-------|
| Feature `as_of()` lookup | ~12 μs | Point-in-time, binary search, in-memory index |
| Feature `as_of_batch()` (100 entities) | ~1.4 ms | Single timestamp, 100 entities |
| MPMC ring push+pop | **10.4 ns** | Single-threaded, `int64_t`, 96M ops/s |
| MPMC ring 4P × 4C | ~70 ns/op | 4 producers, 4 consumers, concurrent |

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
│  crypto/aes_gcm.hpp (footer)  crypto/aes_ctr.hpp (columns)           │
│  crypto/pme.hpp (orchestrator)  crypto/post_quantum.hpp (Kyber-768)  │
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

---

## Test Coverage

```
566 unit tests   (100% pass)   cmake --preset server-pq && ctest
 37 benchmark cases             cmake --preset benchmarks
 59 enterprise benchmarks       Benchmarking_Protocols/ (real tick data, 9 phases)
 35 Python tests (100% pass)    PYTHONPATH=python pytest python/tests/
```

Tests span: roundtrip identity, all encodings, all compression codecs, PME encryption,
post-quantum (Kyber-768, Dilithium-3, X25519 [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748)),
bloom filters, Arrow/DuckDB interop, vector types, tensor bridge, audit chain integrity,
WAL crash recovery + mmap ring path, feature store time-travel, MPMC event bus,
MiFID II and EU AI Act report generation, z-order curve encoding, page index predicate pushdown.

**Cryptographic test vectors**: [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38d/final)
(18 GCM vectors including Test Case 15), [NIST SP 800-38A](https://csrc.nist.gov/pubs/sp/800/38a/final)
(CTR vectors), and [Google Wycheproof](https://github.com/google/wycheproof) edge-case suites
for both AES-256-GCM (tag tampering, ciphertext modification, empty plaintext) and X25519
(low-order points, non-canonical coordinates, twist attacks, scalar clamping).

**Security hardening tests** (`ctest -L hardening`): six hardening passes plus static audit
follow-up covering 242 confirmed vulnerabilities — constant-time GHASH, GCM/CTR counter overflow,
CSPRNG hardening (platform dispatch + hard-fail), secure key zeroing (volatile+barrier), move-only
ciphers, typed statistics merge, page CRC-32, encoding boundary values (RLE/BSS/Delta/Dictionary),
Thrift parser DoS (nesting depth, field count, string bomb, negative list count, MAP size),
interop guards (Arrow offset/length caps, RAII memory management), AI tier hardening (Float16
shift UB, unaligned cast fixes, feature flush ordering, Z-Order bounds, INT4 sign extension,
verify_chain early return), compliance (cross-chain verification, error reporting, price precision,
timestamp granularity, training metadata), WAL fsync checks + empty record rejection, mmap parity
(negative page size, num_values cap, decompression pre-validation), reader row_group bounds,
C FFI exception safety, Rust FFI panic safety, WASM bounds checking and key zeroing, Python
graceful degradation, and getrandom EINTR retry.

**Enterprise compliance** (73 of 92 gaps resolved across 9 passes): FIPS 140-3, [EU AI Act](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R1689),
[MiFID II RTS 24](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32017R0580),
[GDPR](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32016R0679),
[DORA](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32022R2554),
and Parquet PME spec.

---

## Roadmap

| Milestone | Description |
|-----------|-------------|
| **v0.2** | Doxygen/mdBook API reference site |
| **v0.3** | Rust FFI crate (`signet-forge-sys`) |
| ~~**v0.4**~~ | ~~libFuzzer fuzz harness on reader~~ — **Done** (6 harnesses in `fuzz/`, CI integrated) |
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

Signet Forge uses a **dual-license model**:

| Module | License | Files |
|--------|---------|-------|
| Core Parquet engine | [Apache 2.0](LICENSE) | `include/signet/` (all except AI audit tier) |
| Encodings, compression, crypto, bloom, thrift, interop | [Apache 2.0](LICENSE) | Same |
| AI vector types, feature store, streaming WAL | [Apache 2.0](LICENSE) | `include/signet/ai/` (wal, feature_writer, feature_reader, vector_type, etc.) |
| **AI Audit & Compliance tier** | [BSL 1.1](LICENSE_COMMERCIAL) | `include/signet/ai/audit_chain.hpp`, `decision_log.hpp`, `inference_log.hpp`, `ai/compliance/` |

### Apache 2.0 (core + AI infrastructure)

The core library — Parquet read/write, all encodings, compression, post-quantum encryption, bloom filters, vector types, feature store, and streaming WAL — is Apache 2.0. Use it freely in any project, including commercial products.

### BSL 1.1 (AI Audit & Compliance tier)

The AI audit and compliance module (`audit_chain`, `decision_log`, `inference_log`, and the MiFID II / EU AI Act reporters) is licensed under the [Business Source License 1.1](LICENSE_COMMERCIAL).

**Free testing/evaluation grant**: Internal non-production use is permitted up to the thresholds in `LICENSE_COMMERCIAL` (default policy: 30 days, 50,000,000 rows/month, 3 users, 1 node).

**Commercial licensing required** for production use beyond those thresholds, hosted third-party service use, or distributed SDK/library use.

**Runtime enforcement**: Commercial builds validate `SIGNET_COMMERCIAL_LICENSE_KEY` and enforce eval thresholds when the key contains `tier=eval` claims (for example: `tier=eval;max_rows_month=50000000;max_users=3;max_nodes=1;max_days=30`).

**Change Date**: January 1, 2030 — after which the BSL 1.1 tier also converts to Apache 2.0.

**Disable the BSL tier**: Build with `-DSIGNET_BUILD_AI_AUDIT=OFF` to exclude it entirely from your build.

### Commercial Licensing

For commercial licensing inquiries: **johnson@signetstack.io**

---

## Contributing

1. Fork → branch → PR against `main`
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
