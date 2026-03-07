# AI-Native Features Guide

Signet Forge's AI tier transforms a Parquet library into a complete infrastructure layer for production AI systems. This document covers every AI-native component: vector columns, the tensor bridge, the streaming WAL, the ML feature store, decision audit trails, compliance reporting, and the MPMC event bus.

---

## AI Vector Columns

**Header:** `include/signet/ai/vector_type.hpp`, `include/signet/ai/quantized_vector.hpp`

### Why Native Vector Columns?

Embedding models (OpenAI `text-embedding-3-large`, Sentence-BERT, CLIP, etc.) produce dense float32 vectors with dimensions ranging from 128 to 3072. Storing these in Parquet as a BYTE_ARRAY blob works but loses type information and requires custom deserialization. Signet's `VectorColumnWriter/Reader` stores them as properly-typed `FLOAT32_VECTOR(dim)` columns, which enables:

- Typed reads back to `std::vector<float>` without custom parsing
- SIMD-accelerated reads on AVX2/NEON hardware
- Compatibility with the zero-copy tensor bridge (see below)

### Writing Float32 Vectors

```cpp
#include "signet/ai/vector_type.hpp"
using namespace signet::forge;

// 768-dimensional BERT embeddings, 1000 documents
const size_t DIM = 768;
const size_t N   = 1000;

auto schema = Schema::build("embeddings",
    Column<int64_t>("doc_id"),
    Column<std::string>("embedding")  // vector column uses BYTE_ARRAY physical type
);

auto writer = ParquetWriter::open("embeddings.parquet", schema);

VectorColumnWriter vw(DIM);

// Populate your embeddings (shape: [N, DIM])
std::vector<float> embeddings(N * DIM);
// ... run your embedding model to fill embeddings ...

(void)vw.write(embeddings.data(), N);
(void)vw.flush(*writer, 1);  // column index 1

(void)writer->flush_row_group();
(void)writer->close();
```

### Reading Float32 Vectors

```cpp
#include "signet/ai/vector_type.hpp"

auto reader = ParquetReader::open("embeddings.parquet");
VectorColumnReader vr;
auto vectors = vr.read(*reader, 1);  // column index 1
// vectors->size() == N (number of vectors)
// vectors->at(0).size() == DIM (dimension of each vector)

// Access individual embedding:
const std::vector<float>& embedding_0 = vectors->at(0);
```

### INT8 / INT4 Quantized Vectors

For similarity search at scale, 4x–8x storage reduction via quantization is often acceptable (0.5–2% recall loss at typical similarity thresholds):

```cpp
#include "signet/ai/quantized_vector.hpp"

// Write INT8 quantized vectors
QuantizedVectorWriter qw(DIM, QuantizationMode::INT8);
(void)qw.write(embeddings.data(), N);  // float32 input, automatically quantized
(void)qw.flush(writer, col_index);

// Read back: automatically dequantized to float32
QuantizedVectorReader qr;
auto vectors = qr.read(reader, col_index);
// vectors are float32 after dequantization
```

---

## Tensor Bridge

**Header:** `include/signet/ai/tensor_bridge.hpp`, `include/signet/interop/`

The tensor bridge provides a zero-copy path from Parquet columns to:
- **ONNX Runtime** (`OrtValue`) for running ONNX models
- **Arrow** (C Data Interface `ArrowArray`) for Arrow-based analytics
- **NumPy / PyTorch** (DLPack protocol) via the Python bindings

### Zero-Copy ONNX Inference

```cpp
#include "signet/ai/tensor_bridge.hpp"
#include "signet/interop/onnx_bridge.hpp"
using namespace signet::forge;

// Create an owned tensor (allocates memory once)
TensorShape shape{1024};  // single brace — NOT double brace
OwnedTensor<float> tensor(shape, TensorDataType::FLOAT32);

// Get a mutable view and fill from Parquet reader
TensorView<float> view = tensor.view();

auto reader = ParquetReader::open("features.parquet");
auto prices = reader->read_column<double>(0, 0);  // row group 0, col 0

for (size_t i = 0; i < prices->size(); ++i) {
    view.data()[i] = static_cast<float>((*prices)[i]);
}

// Create OrtValue for ONNX Runtime (zero-copy: no data is copied)
// See include/signet/interop/onnx_bridge.hpp for OrtValue creation helpers
```

### DLPack / NumPy Bridge

```cpp
#include "signet/interop/numpy_bridge.hpp"

// Zero-copy export to DLPack for Python/NumPy/PyTorch consumption
// The TensorView exposes a DLPack-compatible memory descriptor
TensorView<float> view = tensor.view();
auto dlpack = to_dlpack(view);  // zero-copy: same memory
```

---

## Streaming WAL

**Header:** `include/signet/ai/wal.hpp`

### What the WAL Is For

The Write-Ahead Log solves the durability problem in streaming AI pipelines:

- Raw events arrive faster than Parquet can be flushed to disk
- Power failure or crash must not lose buffered events
- Events must be recoverable in sequence order

Signet provides two WAL implementations. The standard `WalWriter` appends records at **~339 ns per 32-byte entry** (buffered fwrite, no fsync). The high-performance `WalMmapWriter` uses memory-mapped ring segments to achieve **~38 ns per append** (mmap, no sync) — see [High-Performance Mode: WalMmapWriter](#high-performance-mode-walmmapwriter) below. Both produce identical on-disk formats readable by `WalReader`. When enough data accumulates, a background process compacts WAL segments into queryable Parquet files.

### WAL Latency at a Glance

> Run benchmark cases with: `./build-benchmarks/signet_benchmarks "[wal][bench]" --benchmark-samples 200`

| Mode | Payload | Mean latency | Benchmark tag (Case #) | Notes |
|------|---------|-------------|------------------------|-------|
| `WalWriter` | 32 B | **339 ns** | `"append 32B"` (Case 1) | Buffered fwrite, no kernel syscall |
| `WalWriter` | 256 B | ~450 ns | `"append 256B"` (Case 2) | Buffered |
| `WalWriter` | 1 KB | ~900 ns | — | Buffered |
| `WalWriter` batch flush (1000 × 32 B) | 32 KB | ~2.1 μs | `"1000 appends"` (Case 3) | fflush of full batch |
| `WalWriter` append + fflush | 32 B | ~600 ns | `"append + flush(no-fsync)"` (Case 4) | fflush only |
| `WalManager` (mutex + roll check) | 32 B | ~400–450 ns | `"manager append 32B"` (Case 5) | +60–110 ns over WalWriter: mutex + segment-roll check |
| `WalMmapWriter` | 32 B | **~38 ns** | `"mmap append 32B"` (Case 7) | mmap ring, no sync, single-writer; 9× vs WalWriter |
| `WalMmapWriter` | 256 B | **~42 ns** | `"mmap append 256B"` (Case 8) | Only payload-proportional cost: memcpy + CRC32 |
| `WalMmapWriter` with rotation | 32 B | **~38 ns amortized** | `"mmap append 32B"` (Case 7) | Pre-allocated STANDBY segment; rotation cost ~5 ns amortized |
| Three-way side-by-side | 32 B | see above | Case 11 (fwrite vs mmap), Case 12 (all three) | Run `[wal][mmap][bench]` for adjacent Catch2 output |

For full WAL vs RocksDB/Kafka/PostgreSQL comparison, see the [Performance Summary](#performance-summary) below.

### Basic WAL Usage

```cpp
#include "signet/ai/wal.hpp"
using namespace signet::forge;

// Open or create a WAL file
auto wal = WalWriter::open("/data/events/live.wal");
if (!wal.has_value()) return;

// Append events — returns the assigned sequence number
auto event = serialize_my_event(tick_data);  // your serialization
auto seq = wal->append(event);
// seq->value() is the sequence number (0, 1, 2, ...)

// Periodic flush (fflush only, no kernel fsync — ~600ns)
(void)wal->flush(false);

// Durable flush before shutdown (triggers F_FULLFSYNC on macOS, fsync elsewhere)
(void)wal->flush(true);
(void)wal->close();
```

### Crash Recovery

```cpp
// After a crash, open the WAL in read mode to recover events
auto reader = WalReader::open("/data/events/live.wal");
auto entries = reader->read_all();
// entries is expected<vector<WalEntry>>
// The reader stops at the first truncated or CRC-failed record

for (auto& entry : *entries) {
    // entry.seq: sequence number (monotonically increasing)
    // entry.timestamp_ns: when the record was written
    // entry.payload: your serialized event bytes
    process_recovered_event(entry);
}
```

The WAL reader uses CRC-32 verification on every record. Truncated records (incomplete at the end of the file, common after a crash) are silently skipped without returning an error. Records with bad CRC (rare, indicates media corruption) also stop the scan.

### Rolling Segments with WalManager

For long-running services, use `WalManager` to automatically roll to new segment files when size or record count thresholds are exceeded:

```cpp
WalManagerOptions mgr_opts;
mgr_opts.max_segment_bytes = 64 * 1024 * 1024;  // 64 MB per segment
mgr_opts.max_records       = 500'000;

auto mgr = WalManager::open("/data/wal", mgr_opts);

// Append — automatically rolls when thresholds are hit
auto seq = mgr->append(event_bytes);

// List all segments (for compaction job to process)
auto segments = mgr->segment_paths();

// After a segment has been compacted to Parquet, remove it
(void)mgr->remove_segment(segments[0]);
```

---

### High-Performance Mode: WalMmapWriter

**Header:** `include/signet/ai/wal_mapped_segment.hpp`

#### When to Use It

Use `WalMmapWriter` when:
- Your system is co-located at an exchange or on a low-latency server (HFT colocation)
- Your latency requirement for WAL append is `<50 ns`
- You have a single writer thread per WAL (the mmap path has no internal lock)
- Fixed disk budget is acceptable (`ring_segments × segment_size` — default: 4 × 64 MB = 256 MB)
- Crash safety is sufficient (records survive process crash via `MS_ASYNC`); for power-loss safety, set `sync_on_flush=true`

For MiFID II compliance with the mmap mode: set `sync_on_flush=true` and call `flush()` at session end or before generating reports to ensure all records are durably written.

**Do not use `WalMmapWriter`** for multi-threaded concurrent appends — it has a single-writer contract. Use `WalWriter` (which has an internal mutex) if multiple threads must write to the same WAL.

#### Complete Code Example

```cpp
#include "signet/ai/wal_mapped_segment.hpp"
using namespace signet::forge;

// -----------------------------------------------------------------------
// Configure the mmap ring
// -----------------------------------------------------------------------
WalMmapOptions opts;
opts.dir            = "/fast/nvme/wal";        // NVMe for best results
opts.name_prefix    = "ticks";                 // files: ticks_0000.wal, ticks_0001.wal, ...
opts.ring_segments  = 4;                       // 4 slots in flight: 1 ACTIVE + 1 DRAINING + 2 STANDBY
opts.segment_size   = 64 * 1024 * 1024;        // 64 MB each; MUST be multiple of 65536
opts.sync_on_append = false;                   // HFT: skip per-append msync (~38 ns mode)
opts.sync_on_flush  = false;                   // set true for MiFID II power-loss guarantee

// -----------------------------------------------------------------------
// Open the writer (starts background pre-allocation thread)
// -----------------------------------------------------------------------
auto writer = WalMmapWriter::open(opts);
if (!writer.has_value()) {
    handle_error(writer.error());
    return;
}

// -----------------------------------------------------------------------
// Hot path — ~38 ns per append (32 B payload, x86_64 -O2, no sync)
// -----------------------------------------------------------------------
for (;;) {
    auto tick = recv_market_data();
    auto seq = (*writer)->append(tick.data(), tick.size());
    if (!seq.has_value()) {
        if (seq.error().code == ErrorCode::WAL_RING_FULL) {
            // All 4 ring slots busy: background thread is too slow
            // Increase ring_segments or segment_size to reduce rotation frequency
            handle_backpressure();
        } else {
            handle_error(seq.error());
        }
    }
    // seq->value() is the monotonically increasing sequence number
}

// -----------------------------------------------------------------------
// Graceful shutdown
// -----------------------------------------------------------------------
// Collect segment paths BEFORE close() — paths are unavailable after close
auto paths = (*writer)->segment_paths();

(void)(*writer)->flush();  // ensure MS_ASYNC flush submitted
(*writer)->close();        // joins background thread, unmaps all segments

// Hand paths to WalReader for compaction or recovery
for (const auto& path : paths) {
    auto rdr = WalReader::open(path);
    auto entries = rdr->read_all();
    // ... compact to Parquet ...
}
```

#### Segment Rotation

Segment rotation is automatic and transparent to the caller. When the active segment's write pointer reaches `segment_size`:

1. The background thread has already pre-allocated and mmap-ed the next segment (STANDBY state).
2. The rotation is a single atomic CAS: STANDBY → ACTIVE. The old ACTIVE transitions to DRAINING.
3. `append()` continues immediately on the new segment. Rotation cost is ~5 ns amortized.

The ring design means there are always `ring_segments - 1` segments in STANDBY or DRAINING at any time. With `ring_segments=4`: one ACTIVE, one DRAINING (being flushed), two STANDBY (pre-allocated). Files written by `WalMmapWriter` use the same binary format as `WalWriter` — `WalReader` reads them without modification.

#### Backpressure: WAL_RING_FULL

`WAL_RING_FULL` is returned by `append()` when all ring slots are simultaneously ACTIVE or DRAINING — meaning the background thread cannot drain DRAINING segments fast enough for the writer to claim new STANDBY slots.

In practice this does not occur at typical HFT rates with `ring_segments=4` and 64 MB segments. It would require the background thread (which runs `msync` + `munmap`) to lag more than 256 MB behind the writer. If you do see it:
- Increase `ring_segments` (up to 16) to give the background thread more headroom
- Increase `segment_size` to reduce rotation frequency
- Check that your NVMe is not saturated

#### Durability Note

`WalMmapWriter` with `sync_on_append=false` (the default) uses `MS_ASYNC` — the OS page cache accepts writes immediately and schedules kernel flush asynchronously. This means:

- **Process crash**: All records written before the crash are recoverable (kernel retains page cache across process death).
- **OS crash / power loss**: Records in dirty page cache that have not been flushed to disk may be lost.

For power-loss safety: set `sync_on_flush=true` and call `flush()` periodically. Each `flush()` call triggers `msync(MS_SYNC)` (POSIX) or `FlushViewOfFile + FlushFileBuffers` (Windows), which blocks until pages are on durable storage. Cost: ~150–200 ns per flush call on NVMe. For MiFID II compliance, call `flush()` at end-of-session or before generating audit reports.

#### Platform Note: Windows Fully Supported

`WalMmapWriter` supports Windows via `CreateFile` / `CreateFileMapping` / `MapViewOfFile`. All 11 Windows-specific issues that arise with high-frequency mmap rings are mitigated inside `MappedSegment`:

- File sharing mode set correctly (`FILE_SHARE_READ|WRITE`)
- 64 KB allocation granularity enforced (`segment_size % 65536 == 0`)
- Pre-allocation cost hidden in background prefault loop
- File handle closed before any reuse (`UnmapViewOfFile → CloseHandle(hMap) → CloseHandle(hFile)`)
- Optional `FlushFileBuffers` for power-loss safety (`sync_on_flush=true`)
- Large page support opt-in via `use_large_pages=true` (requires `SE_LOCK_MEMORY_NAME` privilege)

No application code changes are needed for Windows — the same `WalMmapOptions` struct and `WalMmapWriter::open()` call work on all platforms.

#### Performance Comparison: WalWriter vs WalManager vs WalMmapWriter

| Property | `WalWriter` (fwrite) | `WalManager` (fwrite + orchestration) | `WalMmapWriter` (mmap) |
|----------|---------------------|---------------------------------------|----------------------|
| Append latency (32 B, no sync) | ~339 ns · `"append 32B"` Case 1 | ~400–450 ns · `"manager append 32B"` Case 5 | ~38 ns · `"mmap append 32B"` Case 7 |
| Append latency (256 B, no sync) | ~450 ns · Case 2 | ~510–560 ns (extrapolated) | ~42 ns · Case 8 |
| Thread safety | Move-only, single-writer | `std::mutex` — multi-writer safe | Single-writer (no lock on hot path) |
| Per-append overhead vs raw WalWriter | Baseline | +60–110 ns (mutex + roll check + counter) | −9× (no C-lib, no mutex, direct store) |
| Durability per append | stdio buffer | stdio buffer | MS_ASYNC (crash-safe) |
| Power-loss durability | `flush(true)` → ~200 ns | `flush(true)` → ~200 ns | `sync_on_flush=true` → ~200 ns |
| Disk usage | Grows with data | Auto-rolls: max_segment_bytes enforced | Fixed: `ring_segments × segment_size` |
| Segment rotation | Manual | Automatic (size + record count triggers) | Self-managed ring, automatic |
| File format | `WAL_FILE_MAGIC` + CRC-checked records | Identical | Identical — same `WalReader` |
| Benchmark cases | Cases 1–4, 11, 12 | Case 5, 12 | Cases 7–12 (`[wal][mmap][bench]`) |
| Windows support | Yes | Yes (all 11 issues handled) |
| Best for | General purpose, multi-writer | HFT colocation, `<50 ns` |

---

### Real-Time AI Pipeline Pattern

A typical streaming ML pipeline using the WAL:

```
[Exchange feed] → WalWriter (~339ns/event)
                    ↓ (background thread)
              [WAL compactor] → Parquet files
                    ↓
              [Feature extractor] → FeatureWriter
                    ↓
              [ML model inference]
                    ↓
              [DecisionLogWriter] → audit trail
```

---

## ML Feature Store

**Header:** `include/signet/ai/feature_writer.hpp`, `include/signet/ai/feature_reader.hpp`

### Why Parquet as a Feature Store?

Traditional feature stores use Redis, DynamoDB, or custom serving layers. These add operational complexity and cost. Signet's feature store stores features directly in Parquet:

- **Point-in-time correctness** (`as_of(entity, timestamp)`) — critical for training data to avoid look-ahead bias
- **~12 μs per lookup** — binary search on an in-memory index after file open
- **No Redis required** — features live in the same files as your training data
- **Temporal history** — full audit trail of feature values over time

### Feature Store Latency at a Glance

| Operation | Latency | Dataset | Notes |
|-----------|---------|---------|-------|
| `as_of()` single entity | **12 μs** | 10K feature vectors | Binary search over row group stats |
| `as_of_batch()` 100 entities | < 1 ms | Same dataset | One file scan, 100 lookups |
| `write_batch()` 10K × 16 features | ~8 ms | UNCOMPRESSED | Full Parquet flush |

For full Feature Store vs Redis/Feast/Tecton comparison, see the [Performance Summary](#performance-summary) below.

### Writing Features

```cpp
#include "signet/ai/feature_writer.hpp"
using namespace signet::forge;

FeatureWriterOptions wo;
wo.output_path = "features/momentum_v2/";

auto fw = FeatureWriter::create(wo);

// Append feature vectors
FeatureVector fv;
fv.entity_id    = "BTCUSDT";
fv.timestamp_ns = now_ns();
fv.version      = 1;
fv.values       = {0.95f, 0.03f, 1.2f, 0.7f};  // [momentum, volatility, spread, imbalance]

(void)fw->append(fv);

// For 3+ features, build FeatureVector explicitly — do NOT use the
// make_fv helper with 3 positional float args, as the 4th arg is version
(void)fw->flush();
(void)fw->close();
```

### Point-in-Time Lookup (Training Use)

```cpp
#include "signet/ai/feature_reader.hpp"

FeatureReaderOptions ro;
ro.input_path = "features/momentum_v2/";

auto fr = FeatureReader::open(ro);

// Get the most recent features for an entity
auto latest = fr->get("BTCUSDT");
// latest->values == [0.95, 0.03, 1.2, 0.7]

// Point-in-time lookup: get features as of a specific moment
// This is the as_of semantics: returns the last known features at or before the timestamp
int64_t training_timestamp = parse_timestamp("2024-01-15T09:30:00Z");
auto features_at_t = fr->as_of("BTCUSDT", training_timestamp);
// Returns the feature vector that was current at training_timestamp
// No future leakage — this is safe for training data construction
```

### Batch Lookup

```cpp
// Look up features for 100 entities at a single timestamp (for batch inference)
std::vector<std::string> entity_ids = load_active_symbols();
int64_t inference_time = now_ns();

auto batch = fr->as_of_batch(entity_ids, inference_time);
// batch is expected<vector<FeatureVector>>
// batch->size() == entity_ids.size()
```

### Historical Feature Access

```cpp
// Get the full history of feature updates for an entity
auto history = fr->history("BTCUSDT");
// history is expected<vector<FeatureVector>>
// sorted by timestamp_ns ascending

for (const auto& fv : *history) {
    // fv.timestamp_ns, fv.values, fv.version
}
```

### Training vs Serving Semantics

| Operation | Use case | Method |
|-----------|----------|--------|
| `as_of(entity, training_time)` | Build training labels (no look-ahead bias) | `FeatureReader::as_of()` |
| `get(entity)` | Online serving (latest features) | `FeatureReader::get()` |
| `as_of_batch(entities, t)` | Batch inference at a point in time | `FeatureReader::as_of_batch()` |
| `history(entity)` | Backtesting, feature drift analysis | `FeatureReader::history()` |

---

## AI Decision Audit Trail

**Header:** `include/signet/ai/decision_log.hpp`

**License:** BSL 1.1

Every AI system that makes consequential decisions should be able to answer: "What did the model decide, when, with what confidence, and was the record tampered with?" Signet's decision log answers all of these.

### SHA-256 Hash Chain

Each `DecisionRecord` is hashed with SHA-256. The hash of record N includes the hash of record N-1, forming a chain. Any modification to any record — including adding, removing, or editing records — breaks the chain and is detectable by `verify_chain()`.

### Writing Decision Records

```cpp
#include "signet/ai/decision_log.hpp"
using namespace signet::forge;

// output_dir: must not contain ".." path segments (security validation)
// chain_id: must match [a-zA-Z0-9_-]+ (validated on construction)
auto log = DecisionLogWriter::create("/logs/decisions", "trading-chain-001");

DecisionRecord rec;
rec.strategy_id  = 7;           // int32_t — NOT std::string
rec.timestamp_ns = now_ns();    // now_ns() from signet::forge namespace
rec.action       = "BUY";
rec.confidence   = 0.87;
rec.model_version = "alpha-v2.3";
rec.symbol       = "BTCUSDT";
rec.price        = 45123.50;
rec.quantity     = 0.1;
rec.venue        = "BINANCE";
rec.order_id     = "ORD-20240201-001";

(void)log->log(rec);
(void)log->close();
```

### Reading and Verifying

```cpp
auto reader = DecisionLogReader::open("/logs/decisions/trading-chain-001.parquet");

// Verify the hash chain — detects any tampering
auto ok = reader->verify_chain();
if (!ok.has_value() || !*ok) {
    alert("AUDIT CHAIN BROKEN — possible tampering detected");
}

auto records = reader->read_all();
```

### Tamper Detection Example

```cpp
// A regulator can verify integrity without trusting the operator:
auto valid = reader->verify_chain();
// valid == true: every record's hash matches, chain is intact
// valid == false: ErrorCode::HASH_CHAIN_BROKEN — record has been modified
```

---

## Inference Logging

**Header:** `include/signet/ai/inference_log.hpp`

**License:** BSL 1.1

Inference logs record every model invocation: which model, which version, what the inputs were (by hash), what the outputs were (by hash), and how long it took. This enables **ML reproducibility** — given a logged inference, you can rerun it with the same model and the same inputs and verify you get the same output hash.

```cpp
#include "signet/ai/inference_log.hpp"
using namespace signet::forge;

auto ilog = InferenceLogWriter::create("/logs/inference", "model-chain-001");

InferenceRecord irec;
irec.timestamp_ns  = now_ns();
irec.model_id      = "momentum-predictor";
irec.model_version = "v2.3";
irec.input_hash    = sha256_hex(input_features_bytes);  // hash of inputs
irec.output_hash   = sha256_hex(output_predictions_bytes);
irec.latency_us    = inference_duration_us;
irec.batch_size    = 32;

(void)ilog->log(irec);
```

---

## MPMC Event Bus

**Header:** `include/signet/ai/event_bus.hpp`

### Three-Tier Architecture

The event bus routes `SharedColumnBatch` (shared_ptr to a columnar batch) through three tiers of increasing latency and durability:

| Tier | Latency | Use case |
|------|---------|----------|
| Tier 1: dedicated SPSC channel | sub-μs | One exchange adapter → one specific strategy |
| Tier 2: shared MPMC ring | ~10 ns | All exchanges → ML inference pool (load-balanced) |
| Tier 3: WAL/StreamingSink | ms | Compliance logging, Parquet compaction |

### Creating the Event Bus

```cpp
#include "signet/ai/event_bus.hpp"
using namespace signet::forge;

// EventBusOptions is at NAMESPACE scope, NOT nested in EventBus
EventBusOptions bus_opts;
bus_opts.tier2_capacity = 4096;   // must be power-of-2
bus_opts.tier1_capacity = 256;
bus_opts.enable_tier3   = false;

// EventBus is NOT movable (std::mutex member) — always use unique_ptr
auto bus = std::make_unique<EventBus>(bus_opts);
```

### Tier 1: Dedicated Channels

```cpp
// Producer side (Binance adapter thread):
auto ch = bus->make_channel("binance->risk", 256);
auto batch = make_shared_batch("market_data", my_prices, my_quantities);
ch->push(batch);

// Consumer side (risk gate thread):
SharedColumnBatch received;
if (ch->pop(received)) {
    process_market_data(*received);
}
```

### Tier 2: Shared MPMC Pool

```cpp
// Any producer:
auto batch = std::make_shared<ColumnBatch>();
batch->topic = "market_data";
batch->timestamp_ns = now_ns();
// ... fill batch->columns ...
bool accepted = bus->publish(batch);
// accepted == false if the Tier-2 ring is full (check and handle)

// Any worker thread (ML inference pool):
SharedColumnBatch work_item;
while (bus->pop(work_item)) {
    run_model_inference(*work_item);
}
```

### Tier 3: WAL Attachment

```cpp
#include "signet/ai/streaming_sink.hpp"

StreamingSink::Options sink_opts;
sink_opts.output_path   = "/data/compliance_wal/";
sink_opts.ring_capacity = 4096;

auto sink = StreamingSink::create(sink_opts);

// Attach: every publish() call now also submits to the WAL sink
bus_opts.enable_tier3 = true;
bus->attach_sink(&*sink);
```

### Performance

```
Single-threaded push+pop (Tier 2): ~10.4 ns per operation (96 M ops/s)
Concurrent 4P × 4C (Tier 2):       ~70 ns per operation  (57 M ops/s)
```

---

## MiFID II Compliance Reports (BSL 1.1)

**Header:** `include/signet/ai/compliance/mifid2_reporter.hpp`

MiFID II (Markets in Financial Instruments Directive II) Article 25 and RTS 24 require investment firms and algorithmic trading firms to maintain detailed records of every order and algorithmic trading decision. These records must be:

- Retained for five years
- Available to regulators on request within 72 hours
- Tamper-evident

Signet generates RTS 24 Annex I-compliant reports directly from its hash-chained decision logs.

### When You Need This

- Your firm is subject to MiFID II (EU-regulated trading)
- Your algorithmic trading system makes autonomous order decisions
- You need to demonstrate best execution under Article 27

### Generating a MiFID II Report

```cpp
#include "signet/ai/compliance/mifid2_reporter.hpp"
using namespace signet::forge;

ReportOptions opts;
opts.format            = ReportFormat::JSON;       // or NDJSON or CSV
opts.output_path       = "mifid2_report_2024Q1.json";
opts.decision_log_path = "/logs/decisions/";
opts.firm_id           = "FIRM-LEI-00123456789";  // Legal Entity Identifier
opts.verify_chain      = true;                    // Reject broken chains

auto report = MiFID2Reporter::generate(opts);

if (!report.has_value()) {
    // report.error().code == HASH_CHAIN_BROKEN if chain was tampered with
    handle_error(report.error());
} else {
    // report->chain_verified == true: audit chain intact
    // report->record_count: number of decisions in the report
    // report->content: the JSON/NDJSON/CSV report content
    write_to_file(opts.output_path, report->content);
}
```

The generated JSON includes all fields required by Commission Delegated Regulation (EU) 2017/576 (RTS 24), including timestamp, order ID, strategy identifier, instrument, price, quantity, venue, and execution decision metadata.

---

## EU AI Act Compliance Reports (BSL 1.1)

**Header:** `include/signet/ai/compliance/eu_ai_act_reporter.hpp`

The EU AI Act (Regulation (EU) 2024/1689) imposes record-keeping obligations on providers of high-risk AI systems (Article 12), transparency requirements (Article 13), and conformity assessment obligations (Article 19). Compliance obligations begin August 2026.

### What Is Covered

- **Article 12**: Logging of AI system inputs, outputs, and decisions throughout the lifetime of the system
- **Article 13**: Transparency information enabling users to understand the AI system's operation
- **Article 19**: Conformity assessment — demonstrating the system meets safety and accuracy requirements

### Generating EU AI Act Reports

```cpp
#include "signet/ai/compliance/eu_ai_act_reporter.hpp"
using namespace signet::forge;

// Article 12 + 13 combined report
ReportOptions opts;
opts.format            = ReportFormat::NDJSON;  // one JSON object per line
opts.output_path       = "eu_ai_act_report.ndjson";
opts.decision_log_path = "/logs/decisions/";

auto report = EUAIActReporter::generate(opts);

// Article 19 conformity assessment (includes chain verification status)
auto conformity = EUAIActReporter::generate_article19(opts);
// conformity->content includes "conformity_status": "CONFORMANT"
// when both decision and inference chains are verified intact
```

---

## Complete ML Pipeline Example

This example shows all AI-native components working together in a production trading system:

```cpp
#include "signet/ai/wal.hpp"
#include "signet/ai/feature_writer.hpp"
#include "signet/ai/feature_reader.hpp"
#include "signet/ai/decision_log.hpp"
#include "signet/ai/inference_log.hpp"
#include "signet/ai/event_bus.hpp"
#include "signet/ai/compliance/mifid2_reporter.hpp"
using namespace signet::forge;

// -----------------------------------------------------------------------
// 1. Ingest raw market data via WAL (sub-microsecond latency)
// -----------------------------------------------------------------------
auto wal = WalWriter::open("/data/wal/ticks.wal");
auto seq = wal->append(serialize_tick(tick_data));

// -----------------------------------------------------------------------
// 2. Extract features and write to feature store
// -----------------------------------------------------------------------
FeatureWriterOptions fwo;
fwo.output_path = "/data/features/alpha_v1/";
auto fw = FeatureWriter::create(fwo);

FeatureVector fv;
fv.entity_id    = "BTCUSDT";
fv.timestamp_ns = now_ns();
fv.values       = compute_features(tick_data);
(void)fw->append(fv);

// -----------------------------------------------------------------------
// 3. Look up features for model inference (point-in-time correct)
// -----------------------------------------------------------------------
FeatureReaderOptions fro;
fro.input_path = "/data/features/alpha_v1/";
auto fr = FeatureReader::open(fro);

auto features = fr->get("BTCUSDT");

// -----------------------------------------------------------------------
// 4. Run model inference via MPMC event bus
// -----------------------------------------------------------------------
EventBusOptions bus_opts;
auto bus = std::make_unique<EventBus>(bus_opts);

auto batch = std::make_shared<ColumnBatch>();
// ... populate batch with features ...
bus->publish(batch);

// In a worker thread:
SharedColumnBatch work;
bus->pop(work);
auto prediction = run_onnx_model(*work);

// -----------------------------------------------------------------------
// 5. Log the inference (for reproducibility)
// -----------------------------------------------------------------------
auto ilog = InferenceLogWriter::create("/logs/inference", "alpha-v1");
InferenceRecord irec;
irec.model_id      = "alpha_predictor";
irec.model_version = "v2.3";
irec.input_hash    = sha256_hex(features_bytes);
irec.output_hash   = sha256_hex(prediction_bytes);
irec.latency_us    = measured_latency_us;
(void)ilog->log(irec);

// -----------------------------------------------------------------------
// 6. Log the trading decision (for audit and compliance)
// -----------------------------------------------------------------------
auto dlog = DecisionLogWriter::create("/logs/decisions", "trading-chain-001");
DecisionRecord rec;
rec.strategy_id  = 7;
rec.timestamp_ns = now_ns();
rec.action       = "BUY";
rec.confidence   = prediction.confidence;
rec.symbol       = "BTCUSDT";
rec.price        = current_price;
rec.quantity     = 0.1;
rec.venue        = "BINANCE";
(void)dlog->log(rec);

// -----------------------------------------------------------------------
// 7. Generate MiFID II report (end of day / on regulator request)
// -----------------------------------------------------------------------
ReportOptions report_opts;
report_opts.format            = ReportFormat::JSON;
report_opts.output_path       = "mifid2_eod.json";
report_opts.decision_log_path = "/logs/decisions/";
report_opts.firm_id           = "FIRM-LEI-001";

auto report = MiFID2Reporter::generate(report_opts);
// report->chain_verified == true: no tampering detected
// report->content: the full RTS 24 compliant JSON report
```

---

## Performance Summary

All numbers measured on x86_64 macOS, Apple Clang 17, `-O2`. For full methodology and benchmark source files, see [docs/BENCHMARKS.md](../BENCHMARKS.md).

Note on hardware variation: numbers are from x86_64 macOS. ARM (Apple M1/M2) is typically 10–20% faster on Montgomery ladder operations (X25519) due to native 64-bit multiply. MPMC throughput is similar across platforms because the bottleneck is memory barrier cost rather than arithmetic.

### WAL vs RocksDB / Kafka / PostgreSQL

| System | Per-write latency | Benchmark tag | Notes |
|--------|------------------|---------------|-------|
| **Signet WalMmapWriter** | **~38 ns** | `"mmap append 32B"` (Case 7) | mmap ring, no sync, x86_64 -O2; Case 12 side-by-side |
| **Signet WalWriter** | **339 ns** | `"append 32B"` (Case 1) | Buffered fwrite, fflush only |
| **Signet WalManager** | **~400–450 ns** | `"manager append 32B"` (Case 5) | WalWriter + mutex + segment-roll check; auto-rolls, thread-safe |
| RocksDB WAL (sync=false) | 200–800 ns | — | Similar buffered model |
| LevelDB log_writer | 100–500 ns | — | Similar; single-writer design |
| Chronicle Queue (Java, off-heap) | 25–50 ns | — | Memory-mapped, off-heap; JVM required |
| SQLite WAL (non-sync) | 5–50 μs | — | Page-level WAL, higher per-record overhead |
| Kafka (local, single producer, acks=0) | 1–5 ms | — | Network + ISR synchronization overhead |
| PostgreSQL WAL (fsync=on) | 50–500 μs | Full durability, group commit |

`WalWriter` is in the same performance tier as RocksDB and LevelDB. `WalMmapWriter` at ~38 ns projected closes the gap with Chronicle Queue (25–50 ns) while staying in C++ with no JVM requirement. Kafka is 3–4 orders of magnitude slower due to network stack overhead even in local mode. Unlike all these systems, both Signet WAL modes compact directly to queryable Parquet with zero schema translation. `sync_on_flush=true` adds ~150–200 ns to either mode.

**Enterprise bulk throughput (real tick data)**: WalMmapWriter processes 1M records in 403 ms (0.40 μs/record) vs WalWriter's 1.14 s (1.14 μs/record) — a **2.8x throughput advantage** at scale. Encrypted WAL shows < 1% overhead vs plain. See `Benchmarking_Protocols/results/COMPARISON.md` for full results.

### Feature Store vs Feast / Tecton / Vertex AI

| System | p50 Latency | Deployment | Notes |
|--------|-------------|-----------|-------|
| **Signet Feature Store** | **12 μs** | Local (co-located) | Parquet-native, no network, O(log n) |
| Redis GET (local network) | 50–100 μs | Local network | TCP RTT + RESP deserialization |
| Redis GET (Unix socket, same machine) | 20–40 μs | Same machine | Eliminates TCP overhead |
| Feast (Redis backend, gRPC) | 1–5 ms | Network + gRPC | Typical managed deployment |
| Tecton online store | 5–20 ms | Managed SaaS | Full network round-trip |
| Vertex AI Feature Store | 50–100 ms | Google Cloud | Cloud network latency |
| Hopsworks Feature Store | 10–50 ms | Managed / on-prem | RonDB backend |

Signet at 12 μs is **4–8x faster than Redis GET on a local network** because there is no network stack, no RESP protocol overhead, and Parquet row group statistics enable O(log n) binary search instead of a hash table lookup that still requires a network round-trip.

### MPMC Queue vs LMAX Disruptor / Boost / TBB

| System | Single-thread | Multi-thread (4P×4C) | Notes |
|--------|--------------|---------------------|-------|
| **Signet MpmcRing** | **10.4 ns** (96 M ops/s) | ~70 ns/op (57 M ops/s) | Vyukov sequence-based |
| LMAX Disruptor (Java) | 25–50 ns | ~40 ns | Off-heap, memory barriers, JVM overhead |
| folly MPMC Queue (Meta, C++) | 20–40 ns | ~60 ns | Meta's production implementation |
| Boost.LockFree SPSC queue | 15–30 ns | N/A (SPSC only) | GCC builtin CAS, single-producer only |
| Intel TBB concurrent_queue | 50–100 ns | ~80 ns | General-purpose, dynamically sized |
| `std::queue` + `std::mutex` | ~200 ns | ~500 ns (contended) | Baseline with heavy lock contention |
| moodycamel ConcurrentQueue | 15–30 ns | ~50 ns | Token-based, dynamically allocated |

Signet MpmcRing at 10.4 ns is competitive with the best C++ lock-free queues. The key differentiator is that the ring natively carries `ColumnBatch` with zero-copy `TensorView` access — eliminating buffer ownership complexity that other queues require callers to manage externally.
