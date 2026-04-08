# Signet Enterprise Benchmark Suite

59-case benchmark suite covering all implemented modes of the Signet Forge library,
with proper statistical methodology via Catch2 BENCHMARK.

## Quick Start

```bash
# 1. (Optional) Prepare real tick data subsets
bash Benchmarking_Protocols/scripts/prepare_data.sh /path/to/ticks.csv.gz

# 2. Build
cmake --preset benchmarks
cmake --build build-benchmarks --target signet_enterprise_bench

# 3. Run (10 samples per benchmark)
./build-benchmarks/signet_enterprise_bench "[bench-enterprise]" \
    --benchmark-samples 10

# Or use the orchestrator script:
bash Benchmarking_Protocols/scripts/run_full_suite.sh 10
```

## Data Sources

Benchmarks automatically generate synthetic tick data if real CSV subsets are not
available. For production-grade numbers, prepare real data first:

```bash
bash Benchmarking_Protocols/scripts/prepare_data.sh ~/Downloads/ticks.csv.gz
```

This creates 4 CSV files in `data/`: `ticks_1k.csv`, `ticks_100k.csv`,
`ticks_1m.csv`, `ticks_10m.csv`.

Set `SIGNET_BENCH_DATA_DIR` to override the data directory path.

## Benchmark Phases (59 Cases)

| Phase | File | Cases | Tags | Description |
|-------|------|-------|------|-------------|
| 2 | `bench_phase2_write.cpp` | 14 | `[write]` | Write throughput: encoding × compression × encryption × scale |
| 3 | `bench_phase3_read.cpp` | 10 | `[read]` | Read throughput: reader mode × source × projection |
| 4 | `bench_phase4_predicate.cpp` | 5 | `[predicate]` | Predicate pushdown: filter type × scale × bloom probing |
| 5 | `bench_phase5_wal.cpp` | 7 | `[wal]` | WAL pipeline: writer mode × scale × roundtrip |
| 6 | `bench_phase6_features.cpp` | 5 | `[feature-store]` | Feature store: write/get/as_of/as_of_batch/history |
| 7 | `bench_phase7_ai.cpp` | 5 | `[ai]` | AI-native: decision log / inference log / chain verify / tensor / EventBus |
| 8 | `bench_phase8_compliance.cpp` | 6 | `[compliance]` | Compliance reports: MiFID II × 3 formats + EU AI Act Art.12/13/19 |
| 9 | `bench_phase9_interop.cpp` | 3 | `[interop]` | Interop bridges: Arrow export / tensor wrap / batch tensor |
| 10 | `bench_phase10_roundtrip.cpp` | 4 | `[roundtrip]` | Roundtrip fidelity: write → read → verify across modes |

## Running Individual Phases

```bash
# Run only write benchmarks
./build-benchmarks/signet_enterprise_bench "[write]" --benchmark-samples 10

# Run only WAL benchmarks
./build-benchmarks/signet_enterprise_bench "[wal]" --benchmark-samples 10

# Run a single named case
./build-benchmarks/signet_enterprise_bench "W9: 1M optimal Snappy write" --benchmark-samples 20
```

## Methodology

- **Warmup**: Catch2 benchmark warmup (100ms default)
- **Samples**: Configurable (10 minimum recommended, 100+ for publication)
- **Statistics**: Catch2 computes mean, low mean, high mean, std dev automatically
- **Isolation**: Each benchmark uses RAII temp directories (`bench::TempDir`)
- **Data caching**: Tick data loaded once via lazy static locals, shared across cases
- **Elision prevention**: All BENCHMARK lambdas return a value to prevent dead-code elimination

## Build Configurations

| Configuration | Cases Active | Command |
|---------------|-------------|---------|
| Default (benchmarks preset) | 43 core cases | `cmake --preset benchmarks` |
| + ZSTD/LZ4/Gzip | +3 compression cases (W6/W7/W8, R6) | Add `-DSIGNET_ENABLE_ZSTD=ON -DSIGNET_ENABLE_LZ4=ON -DSIGNET_ENABLE_GZIP=ON` |
| + Commercial | +14 cases (W11/W14, R7, WAL7, AI1-3, CR1-6, RT3) | Add `-DSIGNET_ENABLE_COMMERCIAL=ON` |
| + Post-Quantum | +2 cases (W12, R8, RT4) | Add `-DSIGNET_ENABLE_PQ=ON` (requires liboqs) |
| Full suite | All 59 cases | All flags enabled |

## Tick Data Schema

9-column financial market tick data:

| Column | Type | Encoding (optimal) | Description |
|--------|------|-------------------|-------------|
| `timestamp_ms` | INT64 | DELTA_BINARY_PACKED | Millisecond epoch timestamp |
| `symbol` | STRING | RLE_DICTIONARY | Instrument (6 symbols) |
| `exchange` | STRING | RLE_DICTIONARY | Exchange (3 venues) |
| `bid_price` | DOUBLE | BYTE_STREAM_SPLIT | Best bid price |
| `ask_price` | DOUBLE | BYTE_STREAM_SPLIT | Best ask price |
| `bid_qty` | DOUBLE | BYTE_STREAM_SPLIT | Bid quantity |
| `ask_qty` | DOUBLE | BYTE_STREAM_SPLIT | Ask quantity |
| `spread_bps` | DOUBLE | BYTE_STREAM_SPLIT | Spread in basis points |
| `mid_price` | DOUBLE | BYTE_STREAM_SPLIT | Mid price |

## Directory Structure

```
Benchmarking_Protocols/
├── CMakeLists.txt                 # Build target: signet_enterprise_bench
├── README.md                      # This file
├── common.hpp                     # Shared: TempDir, tick schema, CSV loader, option helpers
├── bench_phase2_write.cpp         # 14 cases: encoding × compression × encryption × scale
├── bench_phase3_read.cpp          # 10 cases: reader mode × source × projection
├── bench_phase4_predicate.cpp     #  5 cases: filter type × scale × bloom
├── bench_phase5_wal.cpp           #  7 cases: WAL mode × scale × encryption
├── bench_phase6_features.cpp      #  5 cases: feature store operations
├── bench_phase7_ai.cpp            #  5 cases: decision/inference/chain/tensor/eventbus
├── bench_phase8_compliance.cpp    #  6 cases: MiFID II + EU AI Act × format
├── bench_phase9_interop.cpp       #  3 cases: Arrow/tensor bridges
├── bench_phase10_roundtrip.cpp    #  4 cases: fidelity across modes
├── scripts/
│   ├── prepare_data.sh            # Extract tick CSV subsets from ticks.csv.gz
│   └── run_full_suite.sh          # Orchestrator: purge cache, run all, save results
├── results/                       # Output dir (gitignored)
│   └── .gitkeep
└── data/                          # Prepared tick subsets (gitignored)
    └── .gitkeep
```

## Latest Results (2026-03-05)

Full comparison report: [`results/COMPARISON.md`](results/COMPARISON.md)

Three runs completed on x86_64 macOS Darwin 25, Apple Clang 17, `-O2`:

| Run | Cases | Key Result |
|-----|-------|------------|
| Base (no commercial/PQ) | 37/59 | W3: 857 ms (1M PLAIN), WAL1: 168 ms (100K) |
| Commercial + compression | 56/59 | LZ4 **19% faster** than uncompressed, PME overhead < 0.2% |
| Full (PQ enabled) | 59/59 | PQ overhead < 0.1%, all 59 cases green |

Key findings:
- **LZ4 > Gzip > ZSTD > uncompressed > Snappy** for financial tick write throughput
- **PME and PQ encryption add < 0.5% overhead** at any scale (1K–10M rows)
- **WalMmapWriter is 2.8x faster** than WalWriter at 1M-record bulk throughput
- **column_view() is sub-nanosecond** (0.47 ns) — true zero-copy

## Hardware Profile

Record the following when publishing results:

- CPU model and core count
- RAM size and speed
- Storage type (NVMe SSD, SATA SSD, HDD)
- OS and kernel version
- Compiler and version
- CMake build type (Release, RelWithDebInfo)
