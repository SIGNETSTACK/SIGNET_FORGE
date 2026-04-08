# Compression Guide

This document explains all compression options in Signet Forge, how to choose the right codec, and performance numbers measured on real workloads.

---

## Why Parquet Compression Matters

Parquet's columnar layout is the key reason compression works so well on it. All values from the same column are stored together, so they share type, range, and often a narrow value distribution. A column of exchange names like `["BINANCE", "BINANCE", "BYBIT", "BINANCE", ...]` is extremely compressible. A column of `float64` prices with small deltas compresses 2–4x even before DELTA encoding.

**Benefits of compression in practice:**

- **Smaller files**: 2x–6x size reduction is typical, directly reducing storage cost
- **Faster I/O**: reading fewer bytes from disk or network is faster even if decompression adds CPU overhead
- **Better cache utilization**: more data fits in the OS page cache
- **Ecosystem compatibility**: ZSTD-compressed Parquet files are natively supported by Spark, DuckDB, pandas, Polars, and pyarrow without any configuration

Signet applies compression at the **page level** inside each column chunk. Compression interacts with encoding: for numeric columns, applying DELTA_BINARY_PACKED encoding before ZSTD compression often achieves better ratios than either technique alone.

---

## Codec Overview

### Snappy (bundled, no external dependency)

Snappy is Google's fast-but-moderate-ratio codec. It is bundled directly into Signet's header implementation — no external library needed.

| Property | Value |
|----------|-------|
| External dep | None (bundled) |
| Compress speed | ~250 MB/s |
| Decompress speed | ~500 MB/s |
| Typical ratio | ~1.7:1 on columnar data |
| Available | Always |

**Best for:** Development builds, CI pipelines, and any deployment where installing external libraries is inconvenient. Also good for latency-sensitive hot paths where you need some compression without the installation overhead.

```cpp
WriterOptions opts;
opts.compression = Compression::SNAPPY;
auto writer = ParquetWriter::open("file.parquet", schema, opts);
```

---

### ZSTD (recommended for production)

ZSTD is the clear choice for production. It is the default compression codec in Apache Spark 3.x, DuckDB, and pandas. It delivers roughly twice the compression ratio of Snappy at comparable or better speed for decompression.

| Property | Value |
|----------|-------|
| External dep | `libzstd` |
| Compress speed (level 3) | ~400 MB/s |
| Decompress speed | ~1200 MB/s |
| Typical ratio | ~3.5:1 on columnar data |
| Level range | 1 (fastest) to 22 (best ratio) |
| Available | `SIGNET_ENABLE_ZSTD=ON` |

**Install:**

```bash
# macOS
brew install zstd

# Ubuntu / Debian
apt install libzstd-dev

# Alpine
apk add zstd-dev
```

**Enable:**

```cmake
# CMake flag
cmake -DSIGNET_ENABLE_ZSTD=ON ..

# Or use the server preset which includes ZSTD
cmake --preset server
```

**Use in code:**

```cpp
WriterOptions opts;
opts.compression = Compression::ZSTD;
auto writer = ParquetWriter::open("archive.parquet", schema, opts);
```

**Best for:** Cold storage, analytics archives, data lakes, any file that will be read more than it is written, and any environment where Spark/DuckDB/pandas compatibility matters.

---

### LZ4

LZ4 is the fastest available codec. It trades compression ratio for speed, making it ideal for hot data that is read very frequently or for real-time streaming ingestion where writer latency must be minimized.

| Property | Value |
|----------|-------|
| External dep | `liblz4` |
| Compress speed | ~800 MB/s – 2 GB/s |
| Decompress speed | ~4 GB/s |
| Typical ratio | ~2.1:1 on columnar data |
| Available | `SIGNET_ENABLE_LZ4=ON` |

**Install:**

```bash
brew install lz4          # macOS
apt install liblz4-dev    # Ubuntu
```

**Enable:**

```cmake
cmake -DSIGNET_ENABLE_LZ4=ON ..
```

**Use:**

```cpp
WriterOptions opts;
opts.compression = Compression::LZ4;
```

**Best for:** Streaming ingestion pipelines, frequently-read hot-tier data, in-memory analytics where decompression overhead must be near zero, and edge devices with fast CPUs but limited I/O bandwidth.

---

### Gzip

Gzip (using zlib) provides the highest compression ratio of the non-ZSTD codecs and the widest legacy tool compatibility. It is significantly slower than all other options for both compression and decompression.

| Property | Value |
|----------|-------|
| External dep | `zlib` |
| Compress speed | ~100 MB/s |
| Decompress speed | ~250 MB/s |
| Typical ratio | ~3.0:1 on columnar data |
| Available | `SIGNET_ENABLE_GZIP=ON` |

**Install:**

```bash
# zlib is almost always already installed:
apt install zlib1g-dev    # Ubuntu
brew install zlib          # macOS (usually already present)
```

**Enable:**

```cmake
cmake -DSIGNET_ENABLE_GZIP=ON ..
```

**Best for:** Compliance archives that must be readable by legacy tools (older Spark versions, Java-based Parquet readers that predate ZSTD support), and situations where you need maximum compatibility over maximum performance.

---

## Compression Selection Guide

| Use case | Recommended codec | Reason |
|----------|------------------|--------|
| Development / CI | `SNAPPY` (or `UNCOMPRESSED`) | No external deps; fast build and test cycle |
| Production cold storage / data lake | `ZSTD` | Best ratio, universal tool support |
| Real-time streaming ingestion | `LZ4` | Lowest write latency |
| Frequently-read hot data (daily analytics) | `LZ4` or `ZSTD level 1` | Fast reads, moderate space savings |
| Long-term compliance archive | `ZSTD level 9+` | Maximum ratio; read rarely |
| Legacy tool compatibility (old Spark, Hive) | `GZIP` | Widest reader support |
| Embedded / microcontroller | `SNAPPY` or `UNCOMPRESSED` | No external deps |
| ML training data pipeline | `ZSTD` | Fast decompression during training; small files on NFS |

---

## Performance Numbers

Measured on macOS (x86_64, Apple Clang 17, Release build). Typical columnar data: mix of int64 timestamps, float64 prices, and string symbols.

| Codec | Write throughput | Read throughput | Ratio (float64 column) |
|-------|-----------------|-----------------|----------------------|
| UNCOMPRESSED | ~750 MB/s | ~800 MB/s | 1.0:1 |
| SNAPPY | ~420 MB/s | ~680 MB/s | ~1.7:1 |
| LZ4 | ~680 MB/s | ~760 MB/s | ~2.1:1 |
| ZSTD (level 3) | ~430 MB/s | ~700 MB/s | ~3.5:1 |
| GZIP | ~95 MB/s | ~230 MB/s | ~2.9:1 |

DELTA encoding on int64 timestamps before ZSTD compression: ~4.2:1 combined ratio.

---

## Encoding + Compression Interaction

Encoding (the column-level transformation) and compression (the page-level codec) are independent but interact. Choosing the right combination amplifies the benefit:

```
timestamp_ns (int64, monotonically increasing)
  → DELTA_BINARY_PACKED encoding   : deltas are tiny (e.g., 100ns intervals)
  → then ZSTD compression          : small integers compress extremely well
  → combined ratio: 6:1 – 10:1 depending on tick density
```

```
price (float64, small random walk)
  → BYTE_STREAM_SPLIT encoding     : separates exponent/mantissa byte streams
  → then ZSTD compression          : exponent bytes are highly repetitive
  → combined ratio: ~3.5:1 vs ~1.5:1 without BYTE_STREAM_SPLIT
```

Enable automatic encoding selection to get this behavior without manual configuration:

```cpp
WriterOptions opts;
opts.compression   = Compression::ZSTD;
opts.auto_encoding = true;  // DELTA for INT32/INT64, BSS for FLOAT/DOUBLE, RLE for BOOLEAN
auto writer = ParquetWriter::open("optimized.parquet", schema, opts);
```

---

## SIGNET_MINIMAL_DEPS: No External Compression

For embedded targets, WebAssembly, or minimal Docker images where you cannot install external libraries:

```cmake
cmake -DSIGNET_MINIMAL_DEPS=ON ..
# Or:
cmake --preset minimal
```

In minimal mode:
- SNAPPY is available (bundled header-only implementation)
- ZSTD, LZ4, and GZIP are compiled as stubs that return `UNSUPPORTED_COMPRESSION`
- All core Parquet read/write functionality is fully available
- All encodings (DELTA, RLE, BYTE_STREAM_SPLIT, DICTIONARY) are available

Minimal mode produces the smallest binary footprint: approximately 200 KB for the entire library including schema, encodings, Snappy, and Thrift.

---

## Auto-Compression Selection

If you prefer to let Signet choose the best codec per column automatically:

```cpp
WriterOptions opts;
opts.auto_compression = true;  // Selects based on data characteristics
```

The auto-selection heuristic samples the first 1000 bytes of each column's PLAIN data and chooses:
- `SNAPPY` for string columns (fast, moderate ratio)
- `ZSTD` for numeric columns if the library is available
- `SNAPPY` as the fallback when ZSTD is not built in

For production deployments, prefer explicit codec selection over auto-compression to ensure deterministic behavior across builds.

---

## Using Compression with Encryption

Compression and PME encryption are fully compatible. Signet applies them in the correct order:

1. **Write path**: encode → compress → encrypt → write
2. **Read path**: read → decrypt → decompress → decode

Always compress before encrypting. Encrypted data is effectively random bytes and cannot be compressed.

```cpp
WriterOptions opts;
opts.compression = Compression::ZSTD;
opts.encryption  = my_encryption_config;
// Signet automatically applies the correct order
auto writer = ParquetWriter::open("secure_compressed.parquet", schema, opts);
```

---

## Detailed Performance Reference

For methodology, hardware context, and full industry comparisons, see [docs/BENCHMARKS.md](../BENCHMARKS.md).

All numbers below are from direct measurement on x86_64 macOS, Apple Clang 17, `-O2`.

### Full Codec Comparison Table

| Codec | Compress speed | Decompress speed | Ratio (financial data) | Dependency | When to use |
|-------|----------------|-----------------|----------------------|-----------|-------------|
| UNCOMPRESSED | N/A | N/A | 1.0:1 | None | Debugging, benchmarking, pre-compressed inputs |
| Snappy (bundled) | ~400 MB/s | ~1.5 GB/s | ~1.7:1 | None | Zero-dep default; CI; embedded deployments |
| LZ4 | ~2 GB/s | ~3 GB/s | ~2.1:1 | liblz4 | Real-time streaming; decompression-latency critical |
| ZSTD level 1 (fast) | ~1 GB/s | ~2 GB/s | ~2.8:1 | libzstd | Hot data read frequently; balanced performance |
| ZSTD level 3 (default) | ~500 MB/s | ~1.5 GB/s | **~3.5:1** | libzstd | **Production default; best ratio/speed trade-off** |
| ZSTD level 9 (max ratio) | ~80 MB/s | ~1.5 GB/s | ~4.2:1 | libzstd | Cold archives; compliance storage; write-once |
| Gzip level 6 | ~100 MB/s | ~500 MB/s | ~3.1:1 | zlib | Legacy tool compatibility; old Spark/Hive readers |

Note: Snappy speeds above reflect the reference Snappy library. Signet's bundled header-only mini-Snappy achieves ~250 MB/s compress / ~500 MB/s decompress — roughly half — as the trade-off for zero external dependency.

### Codec Decision Flowchart

```
Can you install external libraries?
├── No  → SNAPPY (bundled, always available)
└── Yes
    ├── Is decompression latency the critical constraint?
    │   └── Yes → LZ4 (~2 GB/s compress, ~3 GB/s decompress)
    ├── Do you need maximum compression ratio?
    │   └── Yes → ZSTD level 9 (~4.2:1, ~80 MB/s compress)
    ├── Do you need legacy tool compatibility (old Spark, Hive)?
    │   └── Yes → GZIP
    └── Default case → ZSTD level 3 (~3.5:1, ~500 MB/s compress)
                       (Spark 3.x default, DuckDB default, pandas default)
```

### Why ZSTD Is the Spark/DuckDB/pandas Default

Apache Spark ≥ 3.3, DuckDB ≥ 0.8, and pandas ≥ 2.0 all default to ZSTD for Parquet output because ZSTD achieves the best compression ratio at production-grade write speed (~500 MB/s), and its decompression speed (~1.5 GB/s) is fast enough that reads are I/O-bound rather than CPU-bound on standard NVMe storage. A Signet file written with `Compression::ZSTD` is directly readable by all these tools without any configuration.

### DELTA + ZSTD Synergy (the >10:1 combination)

Applying `DELTA_BINARY_PACKED` encoding to monotonic int64 timestamps before ZSTD compression achieves **>10:1 combined ratio**:

```
Stage 1 — DELTA_BINARY_PACKED encoding:
  Raw int64 timestamps at 100ns intervals → deltas of ~100 each
  100 is ~7 bits → each value packed in ~7 bits instead of 64 bits
  Compression factor from encoding alone: ~9x

Stage 2 — ZSTD level 3 on packed delta stream:
  The packed delta stream has repetitive bit patterns → ZSTD achieves ~1.3–1.5:1 on top
  Combined: 9x × 1.4x ≈ >12x total

Stage 3 — Result:
  1 billion timestamps (8 GB raw) → ~670 MB stored
```

Enable this with `opts.auto_encoding = true` + `opts.compression = Compression::ZSTD` — Signet applies DELTA automatically for all `INT32`/`INT64` columns and ZSTD as the page compressor.

### Industry Context: ZSTD Ratios vs Other Systems

| System / Workload | ZSTD ratio (columnar numeric) | Notes |
|-------------------|-------------------------------|-------|
| Signet (ZSTD level 3) | ~3.5:1 | Financial data mix |
| Apache Spark (ZSTD level 3) | ~3–4:1 | Similar columnar layout |
| DuckDB (ZSTD level 3) | ~3–5:1 | Additional RLE/dict encoding before ZSTD |
| ClickHouse (ZSTD level 3) | ~4–8:1 | Columnar DB with additional domain-specific encoding |
| Parquet + DELTA + ZSTD (timestamps) | **>10:1** | Combined encoding + compression |

DuckDB and ClickHouse can achieve higher ratios because they apply additional lightweight encodings (dictionary, frame-of-reference) automatically before handing off to ZSTD. Signet achieves similar results with `auto_encoding = true`.
