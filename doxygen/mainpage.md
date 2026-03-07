# Signet Forge {#mainpage}

**C++20 Parquet library with AI-native extensions — zero mandatory dependencies.**

[GitHub Repository](https://github.com/SIGNETSTACK/SIGNET_FORGE) | [Browser Parquet Demo](demo/index.html)

---

## Features

- **Standalone C++20 Parquet** — no Arrow dependency, header-mostly, pure stdlib core
- **All standard encodings** — PLAIN, RLE, DELTA_BINARY_PACKED, BYTE_STREAM_SPLIT, RLE_DICTIONARY
- **Compression** — bundled Snappy; optional ZSTD, LZ4, Gzip
- **Parquet Modular Encryption** — AES-256-GCM/CTR column-level encryption (PME)
- **Post-quantum cryptography** — Kyber-768 KEM + Dilithium-3 signatures + X25519 hybrid
- **AI-native vector columns** — `FLOAT32_VECTOR(dim)` with INT8/INT4 quantization
- **Zero-copy tensor bridge** — Parquet → ONNX OrtValue / Arrow / NumPy / DLPack
- **Streaming WAL** — sub-microsecond append with mmap ring buffer
- **Feature store** — point-in-time joins, versioned feature vectors, Parquet-backed
- **MPMC event bus** — lock-free Vyukov ring + columnar batches
- **Cryptographic audit chain** — SHA-256 hash chain across row groups
- **Compliance reporters** — MiFID II RTS 24, EU AI Act Art. 12/13/19

---

## Modules

| Module | Namespace | Headers |
|--------|-----------|---------|
| **Core** | `signet::forge` | schema.hpp, types.hpp, error.hpp, writer.hpp, reader.hpp |
| **Encodings** | `signet::forge` | encoding/rle.hpp, encoding/delta.hpp, encoding/dictionary.hpp, encoding/byte_stream_split.hpp |
| **Compression** | `signet::forge` | compression/codec.hpp, compression/snappy.hpp, compression/zstd.hpp, compression/lz4.hpp, compression/gzip.hpp |
| **Crypto** | `signet::forge::crypto` | crypto/aes_core.hpp, crypto/aes_gcm.hpp, crypto/aes_ctr.hpp, crypto/pme.hpp, crypto/key_metadata.hpp, crypto/post_quantum.hpp |
| **Bloom Filters** | `signet::forge` | bloom/split_block.hpp, bloom/xxhash.hpp |
| **Indexes** | `signet::forge` | column_index.hpp, mmap_reader.hpp |
| **AI Vectors** | `signet::forge` | ai/vector_type.hpp, ai/quantized_vector.hpp, ai/tensor_bridge.hpp |
| **Streaming** | `signet::forge` | ai/wal.hpp, ai/wal_mapped_segment.hpp, ai/streaming_sink.hpp |
| **Feature Store** | `signet::forge` | ai/feature_writer.hpp, ai/feature_reader.hpp |
| **Event Bus** | `signet::forge` | ai/mpmc_ring.hpp, ai/column_batch.hpp, ai/event_bus.hpp |
| **Audit & Compliance** | `signet::forge` | ai/audit_chain.hpp, ai/decision_log.hpp, ai/inference_log.hpp, ai/compliance/* |
| **Interop** | `signet::forge` | interop/arrow_bridge.hpp, interop/onnx_bridge.hpp, interop/numpy_bridge.hpp |

---

## Quick Start

```cpp
#include <signet/forge.hpp>

using namespace signet::forge;

// Define schema
auto schema = Schema::build("example",
    Column<int64_t>{"id"},
    Column<double>{"price"},
    Column<std::string>{"symbol"});

// Write
auto writer = ParquetWriter::open("data.parquet", schema).value();
int64_t ids[] = {1, 2, 3};
double prices[] = {100.5, 200.75, 300.0};
std::string symbols[] = {"AAPL", "GOOGL", "MSFT"};
(void)writer.write_column(0, ids, 3);
(void)writer.write_column(1, prices, 3);
(void)writer.write_column(2, symbols, 3);
(void)writer.close();

// Read
auto reader = ParquetReader::open("data.parquet").value();
auto col = reader.read_column<double>(0, 1).value(); // row group 0, column 1
```

---

## Build

```cmake
include(FetchContent)
FetchContent_Declare(signet_forge
    GIT_REPOSITORY https://github.com/SIGNETSTACK/SIGNET_FORGE.git
    GIT_TAG        main)
FetchContent_MakeAvailable(signet_forge)
target_link_libraries(your_target PRIVATE signet::forge)
```

---

## License

- **Core library** (Phases 1–9): Apache License 2.0
- **AI Audit & Compliance tier**: Business Source License 1.1
