# Quickstart — Signet Forge

Get a working Parquet writer and reader in under five minutes.

---

## Requirements

- C++20 compiler: Apple Clang 15+, GCC 13+, or MSVC 2022
- CMake 3.21 or newer
- Internet access for the initial FetchContent download

No other dependencies are required. The core library is header-only, pure C++20 stdlib.

---

## Step 1 — Add to Your CMake Project

Add the following to your `CMakeLists.txt`:

```cmake
include(FetchContent)
FetchContent_Declare(signet_forge
    GIT_REPOSITORY https://github.com/SIGNETSTACK/signet-forge.git
    GIT_TAG main
)
FetchContent_MakeAvailable(signet_forge)
target_link_libraries(my_target PRIVATE signet::forge)
```

That is the entire integration. CMake downloads and configures the library automatically on the first configure run. Subsequent builds use the cached copy.

---

## Step 2 — Write a Parquet File

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

int main() {
    // Define the schema
    auto schema = Schema::build("my_table",
        Column<int64_t>("timestamp"),
        Column<double>("price"),
        Column<std::string>("symbol")
    );

    // Open the writer — returns expected<ParquetWriter>
    auto writer = ParquetWriter::open("output.parquet", schema);
    if (!writer.has_value()) {
        // writer.error().message contains the reason
        return 1;
    }

    // Prepare column data
    std::vector<int64_t>     timestamps = {1700000000000LL, 1700000001000LL};
    std::vector<double>      prices     = {42.50, 42.51};
    std::vector<std::string> symbols    = {"AAPL", "AAPL"};

    // Write each column
    (void)writer->write_column<int64_t>(0, timestamps.data(), timestamps.size());
    (void)writer->write_column<double> (1, prices.data(),     prices.size());
    (void)writer->write_column         (2, symbols.data(),    symbols.size());

    // Flush the row group and close
    (void)writer->flush_row_group();
    (void)writer->close();

    return 0;
}
```

---

## Step 3 — Read the File Back

```cpp
#include "signet/forge.hpp"
using namespace signet::forge;

int main() {
    auto reader = ParquetReader::open("output.parquet");
    if (!reader.has_value()) return 1;

    // Read column 1 (price) from row group 0
    auto prices = reader->read_column<double>(0, 1);
    if (!prices.has_value()) return 1;

    // prices->size() == 2
    // prices->at(0) == 42.50
    // prices->at(1) == 42.51

    return 0;
}
```

---

## Python Quickstart

If you build the Python bindings (see `PRODUCTION_SETUP.md`), you can use Signet from Python with NumPy arrays:

```python
import signet_forge as sp
import numpy as np

# Write
writer = sp.ParquetWriter("output.parquet",
    sp.Schema([sp.Column("price", sp.Type.DOUBLE),
               sp.Column("qty",   sp.Type.INT64)]))
writer.write_column_double(0, np.array([42.50, 42.51]))
writer.write_column_int64 (1, np.array([100, 200], dtype=np.int64))
writer.flush_row_group()
writer.close()

# Read
reader = sp.ParquetReader("output.parquet")
prices = reader.read_column_double(0)  # returns np.ndarray[float64]
print(prices)  # [42.5  42.51]
```

---

## What Just Happened?

When you ran the example above, Signet produced a fully spec-compliant Apache Parquet file. Here is what is inside `output.parquet`:

```
PAR1 magic (4 bytes)
--- Row Group 0 ---
  Column chunk 0: int64  "timestamp"  — PLAIN encoding, UNCOMPRESSED
  Column chunk 1: double "price"      — PLAIN encoding, UNCOMPRESSED
  Column chunk 2: string "symbol"     — PLAIN encoding, UNCOMPRESSED
--- FileMetaData footer (Thrift compact) ---
  version: 2
  num_rows: 2
  schema: [root(my_table), timestamp(INT64), price(DOUBLE), symbol(BYTE_ARRAY/UTF8)]
Footer length (4 bytes LE)
PAR1 magic (4 bytes)
```

The file is readable by Apache Spark, DuckDB, pandas (`pd.read_parquet`), Polars, and any other Parquet-compatible tool. This interoperability is guaranteed by the Parquet specification compliance built into Signet.

---

## Common Pitfalls

### Forgetting `flush_row_group()`

All column data accumulates in memory until `flush_row_group()` is called. If you call `close()` without calling `flush_row_group()`, Signet will automatically flush any pending data before closing, but it is good practice to call it explicitly after each logical batch.

```cpp
// Correct pattern:
writer->write_column<double>(0, data.data(), data.size());
(void)writer->flush_row_group();  // write to disk
(void)writer->close();
```

### Not Checking `has_value()`

Every factory function and write operation returns an `expected<T>`. Dereferencing an `expected` that holds an error causes an assertion failure. Always check:

```cpp
auto writer = ParquetWriter::open("file.parquet", schema);
if (!writer.has_value()) {
    std::cerr << "Error: " << writer.error().message << "\n";
    return 1;
}
// Safe to use writer-> here
```

### Column Count Mismatch

If you write different numbers of values to different columns, `flush_row_group()` returns an error with code `SCHEMA_MISMATCH`. All columns must have the same row count before flushing.

```cpp
// Wrong — column 0 has 3 values, column 1 has 2:
writer->write_column<int64_t>(0, ts.data(), 3);
writer->write_column<double>(1, price.data(), 2);
auto result = writer->flush_row_group();
// result.has_value() == false, result.error().code == ErrorCode::SCHEMA_MISMATCH
```

### Using `(void)` on Nodiscard Returns

All write operations return `expected<void>` and are marked `[[nodiscard]]`. In production code, check them. In benchmarks or quick scripts, cast to void to suppress the warning:

```cpp
(void)writer->write_column<double>(0, data.data(), data.size());
```

---

## Next Steps

| Goal | Document |
|------|----------|
| Production deployment with ZSTD, encryption | `PRODUCTION_SETUP.md` |
| Full API reference with all signatures | `API_REFERENCE.md` |
| Post-quantum encryption setup | `PQ_CRYPTO_GUIDE.md` |
| Compression codec selection | `COMPRESSION_GUIDE.md` |
| AI-native features: WAL, feature store, audit | `AI_FEATURES.md` |
| Domain-specific applications and examples | `APPLICATIONS.md` |
