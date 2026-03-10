# Applications Guide

This document provides complete, copy-paste ready examples for every major domain where Signet Forge is applicable. Find your use case, copy the example, and adapt it to your system.

---

## Competitive Positioning

| Capability | Arrow C++ | parquet-rs | Lance | Signet |
|------------|:---------:|:----------:|:-----:|:------:|
| Standalone (no Arrow dep) | No | Yes | Yes | Yes |
| Header-only core | No | No | No | Yes |
| Post-quantum encryption | No | No | No | Yes |
| AI decision audit trail | No | No | No | Yes |
| MiFID II / EU AI Act reports | No | No | No | Yes |
| Sub-μs streaming WAL | No | No | No | Yes |
| Native vector column type | No | No | Yes | Yes |
| Zero-copy Parquet to ONNX | No | No | No | Yes |
| Parquet-native feature store | No | No | No | Yes |
| Encrypted bloom filters | No | No | No | Yes |

---

## Financial Trading and Risk Management

### HFT Tick Data Storage

High-frequency trading systems generate millions of ticks per day per instrument. CSV wastes 3–5x more disk space than Parquet and does not support typed reads without parsing. Binary formats like FlatBuffers require custom tooling. Parquet gives you standard tooling (DuckDB, pandas, Spark), columnar compression, and predicate pushdown for date-range queries.

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

// Schema: one row per trade tick
auto schema = Schema::build("tick_data",
    Column<int64_t>("timestamp_ns"),
    Column<double>("bid"),
    Column<double>("ask"),
    Column<double>("last_price"),
    Column<double>("volume"),
    Column<std::string>("symbol"),
    Column<std::string>("exchange")
);

// Use ZSTD + DELTA encoding for timestamps + BYTE_STREAM_SPLIT for prices
WriterOptions opts;
opts.compression   = Compression::ZSTD;
opts.auto_encoding = true;  // DELTA for int64 timestamps, BSS for doubles
opts.row_group_size = 100'000;  // 100K ticks per row group

auto writer = ParquetWriter::open("ticks_20240201.parquet", schema, opts);

// Buffer ticks and write in batches of row_group_size
std::vector<int64_t>     timestamps;
std::vector<double>      bids, asks, last_prices, volumes;
std::vector<std::string> symbols, exchanges;

while (auto tick = exchange_feed.next_tick()) {
    timestamps.push_back(tick.timestamp_ns);
    bids.push_back(tick.bid);
    asks.push_back(tick.ask);
    last_prices.push_back(tick.last);
    volumes.push_back(tick.volume);
    symbols.push_back(tick.symbol);
    exchanges.push_back(tick.exchange);

    if (timestamps.size() >= 100'000) {
        (void)writer->write_column<int64_t>(0, timestamps.data(),  timestamps.size());
        (void)writer->write_column<double> (1, bids.data(),        bids.size());
        (void)writer->write_column<double> (2, asks.data(),        asks.size());
        (void)writer->write_column<double> (3, last_prices.data(), last_prices.size());
        (void)writer->write_column<double> (4, volumes.data(),     volumes.size());
        (void)writer->write_column         (5, symbols.data(),     symbols.size());
        (void)writer->write_column         (6, exchanges.data(),   exchanges.size());
        (void)writer->flush_row_group();

        timestamps.clear(); bids.clear(); asks.clear();
        last_prices.clear(); volumes.clear();
        symbols.clear(); exchanges.clear();
    }
}

(void)writer->close();
```

**Interoperability**: The resulting file is readable by DuckDB (`SELECT * FROM 'ticks_20240201.parquet' WHERE timestamp_ns BETWEEN ...`), pandas (`pd.read_parquet('ticks_20240201.parquet', columns=['timestamp_ns', 'bid', 'ask'])`), and Apache Spark.

---

### WAL for Order Flow Recording

For systems that must not lose a single order event, the WAL provides crash-safe recording at sub-microsecond latency, with asynchronous compaction to queryable Parquet.

**WAL latency: 339 ns per 32-byte event** (buffered, no fsync). This is 15–150x faster than SQLite WAL and 3000–15000x faster than Kafka. See [BENCHMARKS.md](../BENCHMARKS.md) for full WAL comparison vs RocksDB/Kafka/PostgreSQL.

```cpp
#include "signet/ai/wal.hpp"
using namespace signet::forge;

// The WAL hot path (~339 ns per event for 32-byte payloads)
WalManagerOptions mgr_opts;
mgr_opts.max_segment_bytes = 64 * 1024 * 1024;  // roll at 64 MB
mgr_opts.sync_on_append    = false;               // batch for throughput
mgr_opts.sync_on_roll      = true;                // fsync on segment close

auto wal = WalManager::open("/data/wal/orders/", mgr_opts);

// On each order event (called from the hot path):
auto oe_bytes = order_event.serialize();
auto seq = wal->append(oe_bytes);

// Background compaction thread:
// 1. Get sealed segments
auto segments = wal->segment_paths();
for (const auto& seg : segments) {
    if (seg == segments.back()) continue;  // skip active segment
    compact_segment_to_parquet(seg);       // your compaction logic
    (void)wal->remove_segment(seg);        // clean up after compaction
}
```

---

### MiFID II Best Execution Reporting

```cpp
#include "signet/ai/decision_log.hpp"
#include "signet/ai/compliance/mifid2_reporter.hpp"
using namespace signet::forge;

// Log each AI trading decision during the trading session
auto log = DecisionLogWriter::create("/logs/decisions", "hft-chain-prod");

DecisionRecord rec;
rec.strategy_id   = 12;           // int32_t strategy identifier
rec.timestamp_ns  = now_ns();
rec.action        = "ORDER_NEW";
rec.confidence    = 0.91;
rec.model_version = "momentum-v3.1";
rec.order_id      = generate_order_id();
rec.symbol        = "EURUSD";
rec.price         = 1.0823;
rec.quantity      = 1'000'000.0;  // 1M EUR notional
rec.venue         = "EURONEXT";
(void)log->log(rec);

// End of day: generate MiFID II RTS 24 report
ReportOptions report_opts;
report_opts.format            = ReportFormat::JSON;
report_opts.output_path       = "rts24_report_20240201.json";
report_opts.decision_log_path = "/logs/decisions/";
report_opts.firm_id           = "FIRM-LEI-5299009C0X6XGWJRM735";  // your LEI
report_opts.verify_chain      = true;

auto report = MiFID2Reporter::generate(report_opts);
if (report.has_value() && report->chain_verified) {
    submit_to_regulator(report->content);
}
```

---

### Portfolio Risk — Float32 Vector Columns for Factor Matrices

Risk factor matrices (e.g., Barra-style equity risk models) are naturally expressed as vectors of factor exposures per asset:

```cpp
#include "signet/ai/vector_type.hpp"
using namespace signet::forge;

// 150 risk factors per asset, 5000 assets
const size_t NUM_FACTORS = 150;
const size_t NUM_ASSETS  = 5000;

auto schema = Schema::build("risk_factors",
    Column<std::string>("asset_id"),
    Column<int64_t>("date"),
    Column<std::string>("factor_exposures")  // FLOAT32_VECTOR(150)
);

auto writer = ParquetWriter::open("risk_20240201.parquet", schema);

VectorColumnWriter vw(NUM_FACTORS);

std::vector<std::string> asset_ids(NUM_ASSETS);
std::vector<int64_t>     dates(NUM_ASSETS, 20240201LL);
std::vector<float>       all_exposures(NUM_ASSETS * NUM_FACTORS);

// ... populate from your risk model ...

(void)writer->write_column         (0, asset_ids.data(),     NUM_ASSETS);
(void)writer->write_column<int64_t>(1, dates.data(),         NUM_ASSETS);
(void)vw.write(all_exposures.data(), NUM_ASSETS);
(void)vw.flush(*writer, 2);
(void)writer->flush_row_group();
(void)writer->close();
```

---

### Cross-Asset Correlation Matrices

```cpp
// Store a correlation matrix as a single float32 vector column
// Flatten the upper triangle: N*(N-1)/2 elements for N assets
const size_t N = 200;  // 200 assets
const size_t UPPER_TRIANGLE = N * (N - 1) / 2;

auto schema = Schema::build("correlation_matrices",
    Column<int64_t>("date"),
    Column<std::string>("correlation_upper_tri")  // FLOAT32_VECTOR(UPPER_TRIANGLE)
);

auto writer = ParquetWriter::open("correlations.parquet", schema);
VectorColumnWriter vw(UPPER_TRIANGLE);

std::vector<float> corr_data(UPPER_TRIANGLE);
// ... compute correlations ...

(void)vw.write(corr_data.data(), 1);  // one matrix per row group
(void)vw.flush(*writer, 1);
(void)writer->flush_row_group();
(void)writer->close();
```

---

## AI/ML Research

### Training Data Pipeline: CSV to Parquet

Converting CSV training data to Parquet reduces storage by 2–5x and eliminates parsing overhead during training:

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

WriterOptions opts;
opts.compression   = Compression::ZSTD;
opts.auto_encoding = true;
opts.row_group_size = 65536;

// Single function call — auto-detects column types
auto result = ParquetWriter::csv_to_parquet(
    "training_data.csv",
    "training_data.parquet",
    opts
);

if (!result.has_value()) {
    std::cerr << "Conversion failed: " << result.error().message << "\n";
}
```

For Python users:

```python
import signet_forge as sp
import numpy as np
import pandas as pd

# Convert a pandas DataFrame to Parquet via Signet
df = pd.read_csv("training_data.csv")

writer = sp.ParquetWriter("training_data.parquet",
    sp.Schema([sp.Column(col, sp.Type.DOUBLE) for col in df.columns]))

for col_idx, col_name in enumerate(df.columns):
    writer.write_column_double(col_idx, df[col_name].to_numpy())

writer.flush_row_group()
writer.close()
```

---

### Feature Store for ML Experiments

The `as_of()` semantics of Signet's feature store prevent look-ahead bias in training data — a common source of ML models that perform well in backtesting but fail in production.

**Feature store latency: ~1.4 μs per `as_of()` lookup (with row group cache)** (10K vectors, local, no network). This is 35–70× faster than Redis GET over a local network (50–100 μs) because Signet is co-located and uses binary search over Parquet row group statistics. See [BENCHMARKS.md](../BENCHMARKS.md) for full comparison vs Feast/Tecton/Vertex AI.

```cpp
#include "signet/ai/feature_writer.hpp"
#include "signet/ai/feature_reader.hpp"
using namespace signet::forge;

// When building training labels for an event at time T,
// you must use features known at or before T, not after.

FeatureReaderOptions fro;
fro.input_path = "/data/features/alpha_v2/";
auto fr = FeatureReader::open(fro);

// Build training dataset (each row: features at event_time → label)
std::vector<TrainingSample> samples;

for (const auto& event : training_events) {
    // as_of() guarantees no future leakage
    auto features = fr->as_of(event.entity_id, event.timestamp_ns);
    if (features.has_value()) {
        TrainingSample sample;
        sample.features = features->values;
        sample.label    = compute_label(event);
        samples.push_back(sample);
    }
}
```

---

### Embedding Storage: OpenAI / Sentence-BERT

```cpp
#include "signet/ai/vector_type.hpp"
using namespace signet::forge;

// OpenAI text-embedding-3-large: 3072 dimensions
const size_t DIM = 3072;

auto schema = Schema::build("document_embeddings",
    Column<std::string>("doc_id"),
    Column<std::string>("text_snippet"),
    Column<int64_t>("created_at"),
    Column<std::string>("embedding")  // FLOAT32_VECTOR(3072)
);

WriterOptions opts;
opts.compression = Compression::ZSTD;

auto writer = ParquetWriter::open("embeddings.parquet", schema, opts);
VectorColumnWriter vw(DIM);

// Write a batch of 10,000 embeddings
const size_t BATCH = 10000;
std::vector<float> batch_embeddings(BATCH * DIM);

// ... call embedding API to fill batch_embeddings ...

(void)writer->write_column         (0, doc_ids.data(),   BATCH);
(void)writer->write_column         (1, texts.data(),     BATCH);
(void)writer->write_column<int64_t>(2, timestamps.data(), BATCH);
(void)vw.write(batch_embeddings.data(), BATCH);
(void)vw.flush(*writer, 3);
(void)writer->flush_row_group();
(void)writer->close();
```

**Python:**

```python
import signet_forge as sp
import numpy as np

writer = sp.ParquetWriter("embeddings.parquet",
    sp.Schema([
        sp.Column("doc_id",    sp.Type.STRING),
        sp.Column("embedding", sp.Type.FLOAT32_VECTOR)
    ]))

# embeddings is a (N, 3072) float32 numpy array
writer.write_column_string(0, doc_ids)
writer.write_column_vector(1, embeddings)  # accepts 2D array
writer.flush_row_group()
writer.close()
```

---

### Model Audit Trail for Reproducibility

```cpp
#include "signet/ai/inference_log.hpp"
using namespace signet::forge;

// Log every inference so you can reproduce any prediction
auto ilog = InferenceLogWriter::create("/logs/model_audit", "experiment-42");

// Run inference
auto inputs_bytes  = serialize_features(features);
auto outputs_bytes = serialize_predictions(predictions);

InferenceRecord rec;
rec.timestamp_ns  = now_ns();
rec.model_id      = "gradient_boost_v3";
rec.model_version = "commit_a7f32d1";
rec.input_hash    = sha256_hex(inputs_bytes);
rec.output_hash   = sha256_hex(outputs_bytes);
rec.latency_us    = inference_duration_us;
rec.batch_size    = features.size();

(void)ilog->log(rec);

// Later: reproduce any prediction
// Load the exact model version, recreate the inputs using the hash,
// and verify that running the model produces the same output hash.
```

---

### Active Learning Pipeline with Event Bus

**MPMC ring throughput: 10.4 ns single-threaded (96 M ops/s), ~70 ns at 4P×4C concurrency (57 M ops/s aggregate)**. This is comparable to folly MPMC Queue (Meta) and within 2x of LMAX Disruptor (Java). See [BENCHMARKS.md](../BENCHMARKS.md) for full comparison vs Disruptor/Boost/TBB.

```cpp
#include "signet/ai/event_bus.hpp"
#include "signet/ai/wal.hpp"
using namespace signet::forge;

// Pipeline: new data arrives → WAL → labeling queue → annotation team
// After annotation: labeled data → feature store → retrain

EventBusOptions opts;
auto bus = std::make_unique<EventBus>(opts);

// Unlabeled data channel: ingestion → labeling queue
auto labeling_ch = bus->make_channel("unlabeled->labelers", 1024);

// Labeled data channel: annotators → training queue
auto training_ch = bus->make_channel("labeled->trainer", 1024);

// Ingestion thread: push unlabeled samples
while (auto sample = data_stream.next()) {
    auto batch = make_batch(sample);
    labeling_ch->push(batch);
}

// Labeling thread: annotate and forward
SharedColumnBatch to_label;
while (labeling_ch->pop(to_label)) {
    auto labeled = annotation_ui.annotate(*to_label);
    auto labeled_batch = make_batch(labeled);
    training_ch->push(labeled_batch);
}
```

---

## Scientific Computing

### Simulation Output Storage

Molecular dynamics simulations produce massive time-series of particle positions, velocities, and energies. Parquet provides typed columnar storage with fast time-range queries:

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

// Columns: step, time, particle_id, x, y, z, vx, vy, vz, energy
auto schema = Schema::build("md_trajectory",
    Column<int64_t>("step"),
    Column<double>("time_ps"),
    Column<int32_t>("particle_id"),
    Column<double>("x"), Column<double>("y"), Column<double>("z"),
    Column<double>("vx"), Column<double>("vy"), Column<double>("vz"),
    Column<double>("energy_kJ_mol")
);

WriterOptions opts;
opts.compression   = Compression::ZSTD;
opts.auto_encoding = true;  // DELTA for step/particle_id, BSS for floats
opts.row_group_size = 100'000;

auto writer = ParquetWriter::open("trajectory.parquet", schema, opts);

// Write simulation output in row groups of 100K frames
for (const auto& frame : simulation.frames()) {
    // ... accumulate into column vectors ...
    if (accumulated_count >= 100'000) {
        // ... write_column calls ...
        (void)writer->flush_row_group();
        // ... clear vectors ...
    }
}
(void)writer->close();
```

---

### Climate Data

```cpp
// Climate data: DELTA encoding for timestamps, ZSTD for measurements
auto schema = Schema::build("weather_station",
    Column<int64_t>("timestamp_unix"),   // DELTA encoding: small deltas
    Column<double>("temperature_c"),
    Column<double>("pressure_hPa"),
    Column<double>("humidity_pct"),
    Column<double>("wind_speed_ms"),
    Column<std::string>("station_id")
);

WriterOptions opts;
opts.compression = Compression::ZSTD;
opts.column_encodings["timestamp_unix"] = Encoding::DELTA_BINARY_PACKED;
opts.column_encodings["station_id"]     = Encoding::RLE_DICTIONARY;

auto writer = ParquetWriter::open("weather_2024.parquet", schema, opts);
```

---

### Genomics: Sequence Data

```cpp
// Store DNA sequences and alignment metadata
auto schema = Schema::build("alignments",
    Column<std::string>("read_id"),
    Column<std::string>("sequence"),          // variable-length BYTE_ARRAY
    Column<std::string>("quality_scores"),    // BYTE_ARRAY (phred scores)
    Column<std::string>("reference_name"),
    Column<int64_t>("position"),
    Column<int32_t>("mapping_quality"),
    Column<bool>("reverse_strand")
);

WriterOptions opts;
opts.compression = Compression::ZSTD;
opts.column_encodings["reference_name"]   = Encoding::RLE_DICTIONARY;
opts.column_encodings["reverse_strand"]   = Encoding::RLE;

auto writer = ParquetWriter::open("alignments.parquet", schema, opts);
```

---

### Telescope / Astronomy: Spectral Data with Vector Columns

```cpp
#include "signet/ai/vector_type.hpp"
using namespace signet::forge;

// Each observation has a spectrum vector (e.g., 4096 wavelength bins)
const size_t SPECTRUM_DIM = 4096;

auto schema = Schema::build("spectral_survey",
    Column<int64_t>("observation_id"),
    Column<double>("ra_deg"),    // right ascension
    Column<double>("dec_deg"),   // declination
    Column<double>("redshift"),
    Column<std::string>("source_class"),    // RLE_DICTIONARY: STAR, GALAXY, QSO
    Column<std::string>("spectrum")         // FLOAT32_VECTOR(4096)
);

WriterOptions opts;
opts.compression = Compression::ZSTD;
opts.enable_bloom_filter = true;
opts.bloom_filter_columns = {"source_class"};  // fast source type filtering

auto writer = ParquetWriter::open("sdss_spectra.parquet", schema, opts);
VectorColumnWriter vw(SPECTRUM_DIM);

// ... write observation metadata columns and spectrum vectors ...
```

**Bloom filter usage for fast source lookup:**

```cpp
auto reader = ParquetReader::open("sdss_spectra.parquet");

// Check if "QSO" might exist in row group 3 before loading column data
bool might_have_qso = reader->bloom_might_contain(3, 4, std::string("QSO"));
if (might_have_qso) {
    auto classes = reader->read_column<std::string>(3, 4);  // rg 3, col 4
    // ... filter for QSO entries ...
}
```

---

## Data Engineering

### ETL: CSV to Parquet Conversion

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

WriterOptions opts;
opts.compression    = Compression::ZSTD;
opts.auto_encoding  = true;
opts.row_group_size = 65536;

// Automatic type detection: INT64 > DOUBLE > BOOLEAN > STRING
auto result = ParquetWriter::csv_to_parquet(
    "/data/raw/sales_2024.csv",
    "/data/processed/sales_2024.parquet",
    opts
);

if (!result.has_value()) {
    log_error("ETL failed: " + result.error().message);
    return;
}
```

---

### Data Lake Storage with Partitioning

Write one Parquet file per partition (e.g., date × exchange) for efficient predicate pushdown:

```cpp
#include "signet/forge.hpp"
#include <filesystem>
using namespace signet::forge;

void write_partition(const std::string& date, const std::string& exchange,
                     const std::vector<Tick>& ticks) {
    // Partition path: date=20240201/exchange=BINANCE/data.parquet
    auto path = std::filesystem::path("data_lake")
              / ("date=" + date)
              / ("exchange=" + exchange)
              / "data.parquet";

    std::filesystem::create_directories(path.parent_path());

    WriterOptions opts;
    opts.compression = Compression::ZSTD;

    // Embed partition values as file metadata
    opts.file_metadata.push_back({"partition.date",     date});
    opts.file_metadata.push_back({"partition.exchange", exchange});

    auto writer = ParquetWriter::open(path, tick_schema, opts);
    // ... write ticks ...
    (void)writer->flush_row_group();
    (void)writer->close();
}
```

---

### DuckDB Interoperability

Files written by Signet are readable by DuckDB, Spark, pandas, and pyarrow without any configuration. The files use standard Parquet format with standard encodings:

```sql
-- Query a Signet-written Parquet file with DuckDB:
SELECT
    date_trunc('hour', to_timestamp(timestamp_ns / 1e9)) AS hour,
    avg(bid) AS avg_bid,
    avg(ask) AS avg_ask,
    count(*) AS tick_count
FROM read_parquet('ticks_20240201.parquet')
WHERE symbol = 'BTCUSDT'
GROUP BY 1
ORDER BY 1;
```

```python
# Read with pandas:
import pandas as pd
df = pd.read_parquet('ticks_20240201.parquet', columns=['timestamp_ns', 'bid', 'ask'])

# Read with pyarrow:
import pyarrow.parquet as pq
table = pq.read_table('ticks_20240201.parquet', filters=[('symbol', '=', 'BTCUSDT')])
```

---

### Schema Evolution

Parquet supports adding columns to a schema. Signet's reader handles files written with an older schema by treating missing columns as absent in the row groups that predate the schema change:

```cpp
// Version 1 schema: 3 columns
auto schema_v1 = Schema::build("events",
    Column<int64_t>("timestamp_ns"),
    Column<std::string>("event_type"),
    Column<double>("value")
);

// Version 2 schema: add "source" column
auto schema_v2 = Schema::build("events",
    Column<int64_t>("timestamp_ns"),
    Column<std::string>("event_type"),
    Column<double>("value"),
    Column<std::string>("source")  // new column
);

// Old files (written with schema_v1) are still readable:
// read_column<std::string>(0, 3) returns OUT_OF_RANGE for old row groups
// New files (schema_v2) include the source column
```

---

## Healthcare and Regulated Industries

### EU AI Act Compliance for Medical AI

Medical AI systems (diagnostic imaging, clinical decision support, drug discovery) are classified as high-risk under EU AI Act Annex III. Article 12 requires logging of AI system inputs and outputs. August 2026 is when the Act's obligations begin applying to new high-risk systems.

```cpp
#include "signet/ai/decision_log.hpp"
#include "signet/ai/inference_log.hpp"
#include "signet/ai/compliance/eu_ai_act_reporter.hpp"
using namespace signet::forge;

// Log each clinical AI decision
auto dlog = DecisionLogWriter::create("/audit/clinical_decisions", "cds-chain-001");

DecisionRecord rec;
rec.strategy_id   = 3;           // 3 = "Radiology AI v2" in your system
rec.timestamp_ns  = now_ns();
rec.action        = "FLAGGED_FOR_REVIEW";
rec.confidence    = 0.93;
rec.model_version = "radiology-chest-xray-v2.1";
(void)dlog->log(rec);

// Generate Article 19 conformity assessment
ReportOptions opts;
opts.format            = ReportFormat::JSON;
opts.output_path       = "eu_ai_act_conformity_2024Q1.json";
opts.decision_log_path = "/audit/clinical_decisions/";

auto conformity = EUAIActReporter::generate_article19(opts);
// conformity->content includes: conformity_status, chain_verified,
// decision_count, model_versions, timestamp range
```

---

### GDPR Column-Level Encryption for PII

Encrypt only the PII columns while leaving non-sensitive columns readable for analytics:

```cpp
#include "signet/forge.hpp"
#include "signet/crypto/pme.hpp"
using namespace signet::forge;
using namespace signet::forge::crypto;

// Schema with PII and non-PII mixed
auto schema = Schema::build("patient_records",
    Column<int64_t>("record_id"),        // non-PII: leave unencrypted
    Column<int64_t>("admission_date"),   // non-PII
    Column<std::string>("diagnosis_code"), // non-PII
    Column<std::string>("patient_name"),   // PII: encrypt
    Column<std::string>("date_of_birth"),  // PII: encrypt
    Column<std::string>("address")         // PII: encrypt
);

EncryptionConfig enc;
enc.footer_key = load_key("record-footer-key");
enc.column_keys["patient_name"]  = load_key("pii-key");
enc.column_keys["date_of_birth"] = load_key("pii-key");
enc.column_keys["address"]       = load_key("pii-key");
// Columns not in column_keys are written in plaintext

WriterOptions opts;
opts.encryption = enc;
auto writer = ParquetWriter::open("patient_records.parquet", schema, opts);
```

Analysts with no access to the `pii-key` can still read and aggregate `record_id`, `admission_date`, and `diagnosis_code`. Only authorized systems with the `pii-key` can read the PII columns.

---

### Post-Quantum Encryption for Long-Term Medical Records

Medical records must be retained for 30+ years in many jurisdictions. Classical RSA and ECDH encryption cannot be assumed to be secure over that horizon. Use post-quantum encryption for records created today that must remain confidential in 2050:

```cpp
#include "signet/crypto/post_quantum.hpp"
using namespace signet::forge::crypto;

// Generate keypairs once (store in HSM)
auto kem_kp  = KyberKem::generate_keypair();
auto sign_kp = DilithiumSign::generate_keypair();

// Store in HSM with rotation date
hsm.store("hospital-kyber-pub",         kem_kp->public_key,  rotation_date_2027);
hsm.store("hospital-kyber-sec",         kem_kp->secret_key,  rotation_date_2027);
hsm.store("hospital-dilithium-pub",     sign_kp->public_key, rotation_date_2027);
hsm.store("hospital-dilithium-sec",     sign_kp->secret_key, rotation_date_2027);

// Write patient record with post-quantum protection
PostQuantumConfig pq;
pq.enabled              = true;
pq.hybrid_mode          = true;  // Kyber-768 + X25519
pq.recipient_public_key = hsm.load("hospital-kyber-pub");
pq.signing_secret_key   = hsm.load("hospital-dilithium-sec");
pq.signing_public_key   = hsm.load("hospital-dilithium-pub");

// Configure PME + PQ together
WriterOptions opts;
opts.compression = Compression::ZSTD;
// ... opts.encryption with PQ config ...

auto writer = ParquetWriter::open("patient_record_2024.parquet", schema, opts);
```

---

## IoT and Edge

### SIGNET_MINIMAL_DEPS for Microcontrollers

For Arduino Mega, Raspberry Pi Pico, STM32, and similar MCUs with limited flash and no dynamic linking:

```cmake
cmake -DSIGNET_MINIMAL_DEPS=ON \
      -DSIGNET_BUILD_TESTS=OFF \
      -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake \
      ..
```

Available in minimal mode:
- Core Parquet read/write (all physical types)
- All encodings (PLAIN, DELTA, RLE, DICTIONARY, BYTE_STREAM_SPLIT)
- Snappy compression (bundled, header-only)
- Schema, error handling

Not available in minimal mode: ZSTD, LZ4, Gzip, AES encryption, post-quantum, AI extensions.

---

### WAL for Reliable Edge Logging

Edge devices with intermittent connectivity need to log events locally and upload when connectivity is restored:

```cpp
#include "signet/ai/wal.hpp"
using namespace signet::forge;

WalWriterOptions wal_opts;
wal_opts.sync_on_append = true;   // fsync on every record (embedded: use EEPROM writes)
wal_opts.buffer_size    = 4096;   // smaller buffer for constrained memory

auto wal = WalWriter::open("/sd/events.wal", wal_opts);

// On sensor reading:
SensorReading reading = sensors.read();
auto payload = reading.serialize();
auto seq = wal->append(payload);

// When connectivity is restored:
auto reader = WalReader::open("/sd/events.wal");
auto entries = reader->read_all();
if (entries.has_value()) {
    upload_to_server(*entries);
    // After successful upload: truncate or delete and recreate the WAL
}
```

---

### Snappy Compression for Resource-Constrained Devices

Snappy is the right choice for edge deployments: it is bundled (no external library), fast to compress (~250 MB/s), and fast to decompress (~500 MB/s), making it suitable for devices with limited CPU cycles:

```cpp
WriterOptions opts;
opts.compression    = Compression::SNAPPY;  // bundled, no external dep
opts.row_group_size = 1024;                 // small row groups for streaming upload
```

---

## Security and Cryptography

### Encrypted Parquet as Encrypted Filesystem Alternative

Column-level encryption can replace encrypted filesystems (LUKS, VeraCrypt) for data at rest:

- **Advantages over encrypted filesystems**: encryption is per-file, not per-block device; keys can be different per dataset; files remain interoperable with standard Parquet tools when decrypted
- **Disadvantages**: key management is your responsibility; metadata (schema, column names) is partially visible unless footer encryption is enabled

```cpp
EncryptionConfig enc;
enc.footer_key     = load_256bit_key_from_kms("dataset-key");
enc.encrypt_footer = true;  // hide schema from unauthorized readers
enc.algorithm      = EncryptionAlgorithm::AES_GCM_CTR_V1;
// Optionally add column keys for per-column granularity
```

---

### Audit Chain for Tamper-Evident Logs

Any system that produces logs that must be tamper-evident (financial systems, medical devices, industrial control systems, security logs) can use Signet's audit chain:

```cpp
#include "signet/ai/audit_chain.hpp"
using namespace signet::forge;

auto chain = AuditChain::create("/audit/system_events.chain");

// On each auditable event:
chain->append(serialize_event(event));

// Periodic integrity check (e.g., hourly by a separate process):
auto ok = chain->verify();
if (!ok.has_value() || !*ok) {
    trigger_security_alert("AUDIT CHAIN BROKEN");
}
```

---

### MiFID II Best Execution Proof with Dilithium Signatures

Signet's decision log uses SHA-256 hash chaining. Combined with Dilithium-3 signatures on each row group, you can produce cryptographic proof of best execution that is non-repudiable:

```cpp
// Each row group in the decision log file has a Dilithium-3 signature
// in the file footer key-value metadata.
// A regulator (or counterparty) can verify:
//   1. The hash chain is intact (no records added, deleted, or modified)
//   2. The Dilithium signature is valid (data was produced by your system's key)
//   3. Your Dilithium public key was registered with the regulatory authority

auto reader = DecisionLogReader::open("decisions_20240201.parquet");
auto chain_ok  = reader->verify_chain();
// chain_ok == true: tamper-free

// Extract the Dilithium public key fingerprint
auto metadata = reader->file_key_value_metadata();
auto pubkey_fp = find_metadata(metadata, "dilithium.public_key.fingerprint");
// Verify against your registered public key in the regulatory registry
```

---

## Performance Summary

All numbers measured on macOS x86_64, Apple Clang 17, Release build:

| Operation | Performance | Notes |
|-----------|-------------|-------|
| Write throughput (PLAIN, double, 100K rows) | ~450 MB/s | No compression |
| Read throughput (PLAIN) | ~800 MB/s | Typed `read_column<double>` |
| WAL append (32-byte payload) | ~339 ns | Buffered, no fsync |
| WAL append + flush | ~600 ns | fflush only, no kernel sync |
| Feature store `as_of()` | ~1.4 μs | Binary search, in-memory index |
| Feature store batch (100 entities) | ~21 μs | Single timestamp |
| MPMC event bus (single-threaded) | ~10.4 ns | 96M ops/s |
| MPMC event bus (4P × 4C) | ~70 ns/op | 57M ops/s |
| DELTA encoding (int64, 10K values) | 29 μs encode, 43 μs decode | <50% of PLAIN size |
| BYTE_STREAM_SPLIT (float64, 10K) | ~35 μs encode, ~30 μs decode | Better downstream compression |
| DELTA on sorted int64 | 4.2:1 compression ratio | Combined with ZSTD |
