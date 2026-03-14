# Signet_Forge — Test & Benchmark Reference

A comprehensive, exhaustive guide to every test and benchmark in the Signet_Forge project.
Covers what each test/benchmark **is**, what it **represents**, **why** it is written the way it
is, and **how** it is invoked. Read this alongside the source files.

---

## Table of Contents

1. [Python Binding Tests (`python/tests/test_python_bindings.py`)](#python-binding-tests)
   - [Section 1 — Schema / SchemaBuilder (5 tests)](#section-1--schema--schemabuilder-5-tests)
   - [Section 2 — ParquetWriter (6 tests)](#section-2--parquetwriter-6-tests)
   - [Section 3 — ParquetReader (8 tests)](#section-3--parquetreader-8-tests)
   - [Section 4 — Feature Store (5 tests)](#section-4--feature-store-5-tests)
   - [Section 5 — Decision + Inference Logs (2 tests)](#section-5--decision--inference-logs-2-tests)
   - [Section 6 — Compliance Reports (5 tests)](#section-6--compliance-reports-5-tests)
2. [C++ Benchmark Suite (`benchmarks/`)](#c-benchmark-suite-benchmarks)
   - [bench_write.cpp — Write Throughput (5 cases)](#bench_writecpp--write-throughput-5-cases)
   - [bench_read.cpp — Read Throughput (5 cases)](#bench_readcpp--read-throughput-5-cases)
   - [bench_wal.cpp — WAL Latency (6 cases)](#bench_walcpp--wal-latency-6-cases)
   - [bench_encodings.cpp — Encoding Speed + Size (5 cases)](#bench_encodingscpp--encoding-speed--size-5-cases)
   - [bench_feature_store.cpp — Feature Store Latency (5 cases)](#bench_feature_storecpp--feature-store-latency-5-cases)
   - [bench_event_bus.cpp — MPMC Throughput (5 cases)](#bench_event_buscpp--mpmc-throughput-5-cases)
3. [How to Run Everything](#how-to-run-everything)
4. [Interpreting Output](#interpreting-output)

---

## Python Binding Tests

**File**: `python/tests/test_python_bindings.py`  
**Runner**: pytest  
**Total tests**: 31  
**Dependencies**: `signet_forge` (built with `cmake --preset python`), `numpy`, `pytest`

### How the test suite is structured

The suite is organised into six sections corresponding to the six layers of the Python API surface.
Each section is isolated: a failure in Section 1 (schema) does not cascade into Section 6
(compliance reports) because each test builds its own fixtures. The shared helpers `make_schema()`
and `write_test_file()` exist to prevent schema definition drift — a single change point keeps all
reader tests consistent with all writer tests.

Two pytest conventions are used throughout:

- **`tmp_path` fixture**: a pytest built-in that provides a `pathlib.Path` pointing to a unique
  per-test temporary directory that is automatically removed after the test. This ensures no test
  leaves stale files that could interfere with a later run.
- **`pytest.approx`**: approximate equality for floating-point assertions. Used where IEEE 754
  rounding at conversion boundaries can produce values that are correct but not bit-identical.

---

### Section 1 — Schema / SchemaBuilder (5 tests)

Schema construction is the entry point for all I/O. Every write and read operation requires a
valid schema; a bug here silently corrupts all downstream operations. These tests isolate schema
logic from I/O to catch schema bugs independently.

---

#### `test_schema_builder_basic()`

**What it is**  
Builds a 3-column mixed-type schema (`timestamp: int64`, `price: double`, `symbol: string`) using
the fluent builder API and asserts that the resulting schema reports the correct column count and
name.

**What it represents**  
The entry-point contract for the Python API. Every single downstream test depends on
`SchemaBuilder` working. This test verifies the two most primitive properties: that `build()`
produces a schema object and that the object knows how many columns it contains.

**Why it is written this way**  
The `make_schema()` helper is defined at module scope and shared across all sections. This means
a single change point controls the schema used in all writer, reader, and round-trip tests.
Centralisation prevents silent schema drift (e.g. a reader test expecting 3 columns while the
writer test writes 4).

**How it is used**  
```python
schema = make_schema()           # calls SchemaBuilder().int64().double_().string().build()
assert schema.num_columns == 3   # count check
assert schema.name == "test"     # name preservation check
```

---

#### `test_schema_column_names()`

**What it is**  
Verifies that each column's name is preserved exactly through the builder by accessing each column
descriptor by positional index.

**What it represents**  
Column-name-to-index mapping is the navigation API for projections. At the binary Parquet level,
columns are addressed by index; the name→index mapping lives in the footer metadata. If names are
truncated or mangled during schema construction, `read_columns(["price"])` silently reads the wrong
column.

**Why it is written this way**  
Positional access (`column(0)`, `column(1)`, `column(2)`) rather than name lookup is deliberate:
it tests that ordering is preserved. The Parquet spec addresses columns by position. If the builder
reorders columns, the file will be structurally valid but logically wrong — e.g., `price` data
stored under `timestamp`'s position index.

**How it is used**  
```python
assert schema.column(0).name == "timestamp"
assert schema.column(1).name == "price"
assert schema.column(2).name == "symbol"
```

---

#### `test_schema_find_column()`

**What it is**  
Tests reverse-lookup: given a column name, return its index. Also tests that a nonexistent column
name returns `None` (not a C++ `-1` integer leaking through).

**What it represents**  
`find_column()` is the gateway for name-based projection. Python users write
`reader.read_columns(["price"])`, not `reader.read_column(1)`. The `None`-for-missing behaviour
enables safe conditional projection code without `try/except`.

**Why it is written this way**  
Two assertions: found case (`== 1` for "price") and missing case (`is None`). The `is None`
check explicitly guards against the C++ binding returning `-1` as a Python int, which would be
truthy and would silently propagate through callers that write `if find_column("x"):`.

**How it is used**  
```python
assert schema.find_column("price") == 1
assert schema.find_column("nonexistent") is None
```

---

#### `test_schema_builder_all_types()`

**What it is**  
Exercises all six physical-type factory methods in a single chain: `bool_()`, `int32()`, `int64()`,
`float_()`, `double_()`, `string()`. Checks that the resulting schema has 6 columns.

**What it represents**  
Schema completeness coverage. Without this test, a silently unbound method (e.g. `float_()` is
missing from `_bindings.cpp`) would only be discovered when a float column write fails in
production. Count-based testing (`num_columns == 6`) is a smoke test: if any method is missing or
raises, the count fails immediately.

**Why it is written this way**  
Trailing underscores on `bool_` and `float_` are not stylistic — `bool` and `float` are reserved
Python keywords. The C++ binding must expose these under Python-safe names. If a future pybind11
upgrade breaks the `float_` binding name, this test catches it.

**How it is used**  
```python
schema = (sp.SchemaBuilder("all_types")
          .bool_("flag").int32("int32_col").int64("int64_col")
          .float_("float_col").double_("double_col").string("str_col")
          .build())
assert schema.num_columns == 6
```

---

#### `test_schema_builder_timestamp()`

**What it is**  
Tests the `int64_ts()` semantic shorthand — an int64 column tagged with `TIMESTAMP_NS` logical type
— and asserts the logical type annotation is preserved.

**What it represents**  
Timestamps are the most common column type in financial and ML datasets. The `TIMESTAMP_NS`
logical type instructs Parquet readers (Arrow, DuckDB, Spark, Polars) to interpret the int64 as
nanosecond-epoch timestamps, enabling automatic type inference and time-series operations. A
regression here — where `int64_ts()` silently creates a bare int64 instead — would cause downstream
consumers to treat timestamps as plain integers, breaking all time-range queries.

**Why it is written this way**  
The two-part assertion checks count AND semantic annotation:

```python
assert schema.num_columns == 1
assert schema.column(0).logical_type == sp.LogicalType.TIMESTAMP_NS
```

The second assertion is the critical one: it verifies that the logical type annotation (not just
the physical type) round-trips through the builder. This is the only test in the schema section
that inspects the logical type.

---

### Section 2 — ParquetWriter (6 tests)

These tests verify the writer lifecycle (open → write → flush → close), all three column types
(int64, double, string), multi-row-group writing, and Python resource management patterns.

---

#### `test_writer_opens_and_closes(tmp_path)`

**What it is**  
Opens a writer, checks `is_open == True`, calls `close()`, then checks that the file physically
exists on the filesystem.

**What it represents**  
The writer lifecycle contract. `is_open` is a state probe for caller code that needs to check
whether a writer is safe to write to. Physical file existence after close is the minimum durability
guarantee — it proves the C++ `close()` called `fflush()` and `fclose()` and did not merely set a
Python flag.

**Why it is written this way**  
`assert path.exists()` checks the filesystem, not the Python object. This is the difference
between testing that Python *thinks* the file was written vs. testing that it *actually was*. Using
`tmp_path` ensures the test does not pollute the working directory and does not interfere with
other test runs even in parallel mode.

---

#### `test_writer_context_manager(tmp_path)`

**What it is**  
Tests the `__enter__` / `__exit__` protocol: `with sp.ParquetWriter.open(...) as writer:` must
close and finalize the file when the `with` block exits.

**What it represents**  
Python resource management. In production, file handles and row groups must be properly finalised
both on normal exit and on exception. This test covers the normal exit path; an exception exit
path would require `pytest.raises`. The `__exit__` implementation must call `close()` which flushes
the final row group, writes the Parquet footer, and releases the file descriptor.

**Why it is written this way**  
Only `assert writer.is_open` is inside the `with` block — no data is written. This isolates the
context-manager mechanism from data-writing correctness. `assert path.exists()` after the block
confirms the file was finalised on `__exit__`, not just that a file object was created by `open()`.

---

#### `test_writer_write_int64_column(tmp_path)`

**What it is**  
End-to-end write test for int64: opens a writer, calls `write_column_int64(0, array)` with a
10-element NumPy array, flushes and closes, checks `rows_written == 10`.

**What it represents**  
The fundamental NumPy→Parquet write path for integer time series (timestamps, sequence numbers,
trade IDs). `np.arange` produces a contiguous C-style int64 array — the optimal input for the
pybind11 buffer protocol, which accesses the array's raw memory pointer without copying.

**Why it is written this way**  
`dtype=np.int64` is explicit. `np.arange(10)` defaults to `int64` on Linux/macOS but `int32` on
Windows (where Python's native int is 32-bit). The explicit dtype makes the test platform-agnostic
and prevents false failures on CI Windows runners.

**How it is used**  
```python
writer.write_column_int64(0, np.arange(10, dtype=np.int64))
writer.flush_row_group()
writer.close()
assert writer.rows_written == 10
```

---

#### `test_writer_write_double_column(tmp_path)`

**What it is**  
Write test for float64: writes 3 double values, verifies row count.

**What it represents**  
Price data path — the most common column type in financial applications. Every price, quantity,
PnL, and model output is a double column. The test uses non-sequential values (`[1.1, 2.2, 3.3]`)
to confirm that arbitrary double values are accepted, not just integer-valued doubles.

---

#### `test_writer_write_string_column(tmp_path)`

**What it is**  
Write test for variable-length strings: writes 3 ticker symbols, verifies row count.

**What it represents**  
Symbol/instrument identifier columns. String encoding is architecturally different from numeric:
each value has variable length, requiring a length-prefix encoding. The Python binding accepts a
`list` (not `np.ndarray`) because strings do not have a NumPy buffer-protocol representation.

**Why it is written this way**  
Python list → C++ `std::vector<std::string>` conversion is tested explicitly because it is the
most common binding failure mode (type mismatch between py::list and expected sequence types).

---

#### `test_writer_multiple_row_groups(tmp_path)`

**What it is**  
Writes 3 row groups of 50 rows each using `WriterOptions.row_group_size = 50`, verifying both
`rows_written == 150` and `row_groups_written == 3`.

**What it represents**  
Streaming write pattern — the core operational mode for continuous data capture (market data,
model inference logs, event streams). Parquet's row group boundary is the predicate-pushdown
granularity: Parquet readers use the column statistics in each row group's metadata to skip entire
row groups during filtered reads. Verifying the row group counter is critical for future query
optimisation.

**Why it is written this way**  
The explicit loop `for rg in range(3)` mirrors real application code: write a batch, flush a row
group, repeat. `WriterOptions.row_group_size` is set to 50 so the writer does not auto-flush
mid-iteration (the option limits, it does not auto-trigger; `flush_row_group()` is called
explicitly in the loop).

---

### Section 3 — ParquetReader (8 tests)

The reader tests cover: schema reconstruction from footer, typed column reads for all three types,
column projection by name, full round-trips for int64 and double, and the context manager pattern.

---

#### `test_reader_opens(tmp_path)`

**What it is**  
Opens a reader on a writer-produced file and checks `num_rows > 0`.

**What it represents**  
Reader initialisation contract — that Parquet footer parsing succeeds on a file written by the same
library version. Footer parsing involves Thrift compact protocol decoding; this test is the
first-line canary for Thrift decode regressions.

---

#### `test_reader_schema(tmp_path)`

**What it is**  
Verifies that the schema written by the writer is correctly reconstructed by the reader.

**What it represents**  
Schema round-trip — the most fundamental interoperability test. If schema reconstruction fails,
all column reads produce incorrect data types or silently fail. `reader.schema.num_columns == 3`
is the minimal assertion; individual column names are tested in Section 1.

---

#### `test_reader_read_column_int64(tmp_path)`

**What it is**  
Reads back an int64 column and verifies length (50), dtype (`np.int64`), and two specific values
(`arr[0] == 0`, `arr[49] == 49`).

**What it represents**  
Numeric column round-trip identity. The boundary value checks (`arr[0]`, `arr[49]`) are
specifically chosen: `arr[0]` guards against a one-element offset; `arr[49]` guards against
truncation (a file that was correctly written but read too short would fail here). Reading by
`(row_group=0, col_idx=0)` explicitly tests the row group addressing.

**Why it is written this way**  
```python
arr = reader.read_column_int64(0, 0)   # row_group=0, column_index=0
assert isinstance(arr, np.ndarray)      # type: buffer-protocol result
assert arr.dtype == np.int64            # dtype: no silent widening
assert len(arr) == 50                   # length: no silent truncation
assert arr[0] == 0 and arr[49] == 49    # identity: boundaries checked
```

The `isinstance(arr, np.ndarray)` check guards against the binding returning a list or tuple
(which would indicate a buffer-protocol failure where the C++ code fell back to element-by-element
conversion).

---

#### `test_reader_read_column_double(tmp_path)`

**What it is**  
Reads back a double column and spot-checks the first value with tolerance.

**What it represents**  
Price column round-trip fidelity. `pytest.approx(40000.0, abs=1.0)` uses a wide tolerance (1 USD
for a BTC price) because `np.linspace(40000.0, 41000.0, 50)` may produce endpoint values with
sub-ulp rounding at the specific value. The tolerance is deliberately wide to avoid
platform-dependent floating-point failures while still catching a completely wrong value.

---

#### `test_reader_read_column_string(tmp_path)`

**What it is**  
Reads back a string column and verifies every value equals "BTCUSDT".

**What it represents**  
Symbol column round-trip. Strings involve heap allocation on both write (encoding) and read
(decoding). The `all(s == "BTCUSDT" ...)` check verifies every element, not a sample — with 10
rows this is inexpensive but provides complete coverage.

---

#### `test_roundtrip_int64(tmp_path)`

**What it is**  
Full write-then-read round-trip for int64, using `np.testing.assert_array_equal` for byte-level
identity.

**What it represents**  
The strongest int64 correctness guarantee. `assert_array_equal` raises with a descriptive element
diff if any value differs, making root-cause identification faster than a bare `==` comparison that
produces only `AssertionError: False`.

---

#### `test_roundtrip_double(tmp_path)`

**What it is**  
Full write-then-read round-trip for float64, using `np.testing.assert_array_almost_equal`.

**What it represents**  
IEEE 754 round-trip fidelity. `assert_array_almost_equal` allows 6 decimal places of tolerance —
appropriate for PLAIN encoding where the bytes are written and read without transformation, but
where the PLAIN encoding→decode path may introduce a minimum representation epsilon on some
platforms.

**Why it is written this way**  
The data is `[3.14, 2.71, 1.41]` — mathematical constants chosen because they have interesting
IEEE 754 representations (non-terminating binary fractions) that stress the encoding path more
than round numbers like `[1.0, 2.0, 3.0]`.

---

#### `test_reader_context_manager(tmp_path)`

**What it is**  
Tests `with sp.ParquetReader.open(...) as reader:`.

**What it represents**  
Reader file-handle lifecycle management. Unclosed readers hold an open file descriptor and,
on some OS, a memory map. In long-running processes or tests with large fixture sets, file
descriptor exhaustion causes obscure failures. The `__exit__` path must release the file.

---

### Section 4 — Feature Store (5 tests)

The Feature Store tests verify the ML feature write/read pipeline. These tests are the Python
exposure of the sub-50µs feature serving claim made in the benchmark suite.

---

#### `test_feature_vector_fields()`

**What it is**  
Unit test for the `FeatureVector` data class: sets `entity_id`, `timestamp_ns`, `version`, and
`values` fields, reads them back.

**What it represents**  
The Python representation of one ML feature record. All fields must be assignable and readable
from Python. If a field binding is missing or the wrong type (e.g., `values` is read-only), every
feature write test fails with an `AttributeError` that obscures the real bug.

**Why it is written this way**  
No I/O — pure data-class test. This is the cheapest possible test to run and provides the earliest
signal for binding regressions. `fv.values = [1.0, 2.0, 3.0]` tests Python list → C++
`std::vector<double>` assignment; `len(fv.values) == 3` confirms the vector was set and is
readable as a sequence.

---

#### `test_feature_writer_creates_file(tmp_path)`

**What it is**  
Writes 1 feature vector with 2 features and verifies `rows_written == 1` and
`len(output_files) > 0`.

**What it represents**  
Minimum viable feature write — the simplest end-to-end path: `FeatureWriter.create()` → `write()`
→ `close()`. Two independent invariants are checked: the row count (data integrity) and the output
file list (disk persistence). A writer that creates a zero-byte file or fails silently would fail
on exactly one of these invariants.

---

#### `test_feature_reader_get(tmp_path)`

**What it is**  
Writes 5 feature vectors for entity "e0" with increasing timestamps (0, 1ms, 2ms, 3ms, 4ms),
then calls `reader.get("e0")` and verifies the returned entity ID.

**What it represents**  
Online feature serving — the primary ML inference pattern: "give me the latest features for entity
X." This is the hot path for real-time model inference. `get()` should return the vector with the
highest timestamp for the requested entity.

**Why it is written this way**  
`result.entity_id == "e0"` confirms the right entity was returned, not the nearest neighbour from
a different entity key. In a production feature store with thousands of entities, an off-by-one in
hash bucketing could return the wrong entity's features — a serious silent bug.

---

#### `test_feature_reader_as_of(tmp_path)`

**What it is**  
Writes 10 feature vectors at 1ms intervals, queries `as_of("e0", 5 * 1_000_000)` (5ms), and
asserts the returned timestamp does not exceed the query time.

**What it represents**  
**Point-in-time correctness** — the defining property of a feature store and the most critical
correctness requirement for any system used in backtesting or ML training. "What were the features
for entity X as they existed at time T?" Without this, training pipelines introduce look-ahead
bias: a model trained on features that were not available at the time of the label would be
optimistic in backtest and fail in production.

**Why it is written this way**  
`result.timestamp_ns <= 5 * 1_000_000` is an inequality, not equality. The correct `as_of`
implementation returns the latest vector at or before the query time — it may be exactly at 5ms
or at any earlier timestamp if the 5ms vector does not exist. The inequality expresses the
point-in-time contract precisely.

---

#### `test_feature_reader_missing_entity(tmp_path)`

**What it is**  
Writes features for "e0", queries for "nonexistent", expects `None`.

**What it represents**  
Graceful miss handling. In production feature stores, new entities appear continuously (newly
listed stocks, new users, new instruments). Their first appearance in a request must return `None`
(no features yet), not an exception that crashes the inference pipeline.

**Why it is written this way**  
`result is None` (identity check) rather than `not result`. A zero-feature vector would be falsy
in Python but is not `None`. The `is None` check distinguishes "entity not found" (None) from
"entity found but has no features" (empty FeatureVector), which have different downstream
semantics.

---

### Section 5 — Decision + Inference Logs (2 tests)

These tests verify the AI compliance audit trail — the MiFID II and EU AI Act logging pipeline.
Every field on each record type is populated to test all binding paths.

---

#### `test_decision_log_writer(tmp_path)`

**What it is**  
Creates a `DecisionLogWriter`, populates a `DecisionRecord` with all fields, calls `writer.log(rec)`,
closes the writer, and asserts `current_file_path()` is non-empty.

**What it represents**  
The AI trading decision audit trail. Each `DecisionRecord` captures the complete state of one
trading decision: when it happened, which strategy and model produced it, what the model saw
(input features), what it decided (model output, confidence, risk gate result), and what was
executed (order ID, symbol, price, quantity, venue). This is the record format required by
MiFID II RTS 24 and EU AI Act Art.12.

**Why it is written this way**  
All fields are set explicitly rather than relying on defaults. This is intentional: it forces the
binding for every field to be exercised. The most common binding regression is a new field added in
C++ without a corresponding Python binding — exercising every field in a test catches this.

Key field validation:
- `strategy_id = 42` (int32, not string — important: the C++ field is `int32_t`)
- `decision_type = sp.DecisionType.ORDER_NEW` (enum from correct name — not `HOLD` or `BUY`)
- `risk_result = sp.RiskGateResult.PASSED` (enum — not `OVERRIDE`)

---

#### `test_inference_log_writer(tmp_path)`

**What it is**  
Creates an `InferenceLogWriter`, populates an `InferenceRecord`, logs it, and verifies a file path.

**What it represents**  
LLM/ML inference audit trail. Captures: model identity (`model_id`, `model_version`), inference
type (`CLASSIFICATION`), input (`input_embedding`, `input_hash`), output (`output_hash`,
`output_score`), operational metadata (`latency_ns`, `batch_size`). EU AI Act Art.19 requires
exactly these fields for technical AI system documentation.

**Why it is written this way**  
`input_embedding = [0.1, 0.2]` is a length-2 vector (real production embeddings are 768–4096
dimensions). Using a short vector keeps the test fast while still exercising the list→vector
binding path.

---

### Section 6 — Compliance Reports (5 tests)

These tests verify the regulatory output pipeline: from raw log files to formatted compliance
reports in JSON, NDJSON, and CSV formats.

---

#### `test_mifid2_reporter_json(tmp_path)`

**What it is**  
Generates a MiFID II RTS 24 JSON report from 10 decision log records and checks record count,
compliance standard enum, and content substrings.

**What it represents**  
MiFID II (Markets in Financial Instruments Directive II) Regulatory Technical Standard 24 requires
investment firms to maintain records of all algorithmic trading decisions. This report is the
machine-readable format for regulatory submission. `total_records == 10` confirms all input
records were processed; `"MiFID_II_RTS_24" in report.content` confirms the report is correctly
annotated with the applicable standard identifier.

**Why it is written this way**  
Substring checks rather than full JSON parsing: `"MiFID_II_RTS_24" in report.content` and
`"TEST_FIRM" in report.content`. The report content format may evolve; pinning the exact JSON
structure would cause false failures on format updates that are backward-compatible. The two
substrings tested — the standard identifier and the firm ID — are the invariants that must always
be present regardless of format version.

---

#### `test_mifid2_reporter_csv(tmp_path)`

**What it is**  
Same as above but in CSV format: `opts.format = sp.ReportFormat.CSV`.

**What it represents**  
Regulatory submissions frequently require CSV for import into compliance management platforms
(Bloomberg AIMS, Axiom, etc.). The CSV format uses the standard column layout (`timestamp_utc`,
`order_id`, `instrument`, ...) defined by MiFID II RTS 24. `"timestamp_utc" in report.content`
confirms the CSV header row is present — the most likely regression point after any format change.

---

#### `test_eu_ai_act_article12(tmp_path)`

**What it is**  
Generates an EU AI Act Article 12 report from 10 inference log records.

**What it represents**  
EU AI Act Article 12 requires providers of high-risk AI systems to maintain automatically generated
logs with a level of detail enabling ex-post verification of AI system outputs. Article 12 is
specifically targeted at General-Purpose AI (GPAI) providers, with obligations effective August
2026. This report format provides the technical documentation required for Art.12 compliance.

---

#### `test_eu_ai_act_article19(tmp_path)`

**What it is**  
Generates an EU AI Act Article 19 report from both decision logs and inference logs.

**What it represents**  
Article 19 requires deployers of high-risk AI systems to keep logs for the lifetime of the system
(minimum 10 years for financial services). This report uniquely combines trading decision records
(business-level activity) with model inference records (technical AI activity) into a single
conformity assessment document. This is the joint decision+inference audit format that no other
Parquet library supports natively.

**Why it is written this way**  
Two separate path lists — `[dec_path]` and `[inf_path]` — reflecting the cross-cutting nature of
Art.19 compliance that spans multiple data sources. `"conformity_status" in report.content`
confirms the Art.19-specific conformity assessment section is present.

---

#### `test_mifid2_csv_header()`

**What it is**  
Tests the static `MiFID2Reporter.csv_header()` method — returns just the CSV column header string
without requiring any data files.

**What it represents**  
Template-first compliance design: operators need to know the column layout before any data is
available, to configure ETL pipelines and compliance management system integrations. The static
method enables this without instantiating a reporter.

**Why it is written this way**  
No `tmp_path` fixture — stateless class method. Three specific column names checked:
`timestamp_utc`, `order_id`, `instrument`. These are the three columns that MiFID II RTS 24
Annex I explicitly names as required fields in every algorithmic trading record.

---

---

## C++ Benchmark Suite (`benchmarks/`)

**Build preset**: `cmake --preset benchmarks` (build dir: `build-benchmarks/`)  
**Binary**: `build-benchmarks/signet_benchmarks`  
**Runner**: Catch2 BENCHMARK macro  
**Total cases**: 31 (across 6 files)

> **Note**: Total C++ unit test count is **423/423** as of the Mar 2026 security hardening pass #5 + static audit follow-up (was 306 → 317 → 334 → 390 → 394 → 423). See [Security Hardening Tests](#security-hardening-tests-added-feb-2026) below.
**Tags**: `[bench]` (top-level), plus specific tags per file

### How Catch2 BENCHMARK works

`BENCHMARK("name") { ... return value; }` is a lambda that Catch2 runs in a multi-sample loop.
On each iteration:

1. The lambda is called in a timed loop. Catch2 selects the number of iterations to achieve
   statistical stability (default: ~200ms of wall time per benchmark).
2. A **warm-up phase** discards the first few iterations (cold-start artefacts).
3. The **return value** of the lambda is critical: it prevents the C++ compiler's optimiser from
   treating the entire lambda body as dead code (since the side effects are on heap objects and
   the return prevents elision of the computation).
4. Results are reported as mean, standard deviation, and confidence interval.

**Fixture placement**: Data setup (building vectors, writing files) is placed **outside** the
`BENCHMARK(...)` lambda. This ensures only the target operation (write, read, encode, append) is
measured, not the fixture construction. The pattern:

```cpp
// Setup (measured once, not timed):
std::vector<double> data = build_data();

// Only this is timed:
BENCHMARK("my operation") {
    auto result = do_operation(data);
    return result;  // prevent elision
};
```

---

### bench_write.cpp — Write Throughput (5 cases)

**File**: `benchmarks/bench_write.cpp`  
**Tag**: `[bench][write]`  
**Purpose**: Measures how many rows/second the `ParquetWriter` can write for different column
types, schemas, and row group configurations.

---

#### `TEST_CASE("Write throughput — int64 column 10K rows")`

**What it is**  
Opens a new `ParquetWriter`, writes a 10,000-element int64 column (`ts`), flushes the row group,
closes the writer, and removes the file — all inside the BENCHMARK lambda.

**What it represents**  
The write throughput baseline for integer data. This is the core operation for timestamp column
recording in HFT tick data capture. A single int64 column of 10K rows corresponds to approximately
1 second of tick data at 10 KHz sampling rate.

**Why it is written this way**  
The file is created and removed inside the BENCHMARK to measure the complete write cycle (open +
encode + write + close). `Compression::UNCOMPRESSED` is used to isolate encoding overhead from
compression overhead — the benchmark measures Parquet serialisation, not compressor throughput.
`std::iota` fills the vector outside the lambda so vector construction is not measured.

**Key design choice**: The path uses `fs::temp_directory_path()` with a fixed name. On macOS this
is `/tmp/` (tmpfs, memory-backed), which minimises I/O latency variance. This means the benchmark
measures Parquet serialisation overhead more than disk speed — intentional for a library benchmark.

---

#### `TEST_CASE("Write throughput — double column 10K rows")`

**What it is**  
Same as above but for 10,000 double values with `BYTE_STREAM_SPLIT` encoding.

**What it represents**  
Price column write throughput — the most performance-critical path for financial data capture.
`BYTE_STREAM_SPLIT` (BSS) is specified via `WriterOptions.default_encoding` to benchmark the BSS
encoding path, which rearranges byte planes to improve subsequent compression ratios for clustered
floating-point values.

**Why it is written this way**  
BSS does not reduce file size on its own (output is the same number of bytes as input). The
benchmark therefore measures pure encoding throughput in MB/s, not compression savings. This is
the correct comparison: BSS is a pre-processing step before an external compressor, not a
compressor itself.

---

#### `TEST_CASE("Write throughput — mixed schema 5 columns 10K rows")`

**What it is**  
Writes 5 columns (int64 `ts`, double `price`, double `qty`, string `symbol`, string `side`) of
10,000 rows each.

**What it represents**  
Realistic HFT trade record write throughput. The schema (`ts`, `price`, `qty`, `symbol`, `side`)
mirrors the canonical tick record format used in market microstructure research. Writing 5 columns
sequentially exercises the column dispatch mechanism and reveals whether multi-column write has
super-linear overhead.

**Why it is written this way**  
All 5 columns are written with a single `flush_row_group()` — the one-row-group pattern. The
string columns use realistic ticker symbols from a pool (`BTCUSDT`, `ETHUSDT`, etc.) rather than
synthetic `"str_0"`, `"str_1"` values, because real symbol distribution (short, high-repetition
strings) is a significantly different encoding workload from synthetic data.

---

#### `TEST_CASE("Write throughput — 100K rows 10 row groups")`

**What it is**  
Writes 100,000 rows across 10 row groups of 10,000 rows each, using `row_group_size = 10000`.

**What it represents**  
Multi-row-group write overhead. Each `flush_row_group()` writes a Thrift-encoded RowGroup metadata
block and updates internal statistics. This test measures whether the row group overhead grows
linearly with row group count or introduces non-linear overhead (e.g., O(N²) footer re-encoding).

**Why it is written this way**  
The loop `for (int64_t rg = 0; rg < NUM_RGS; ++rg)` uses pointer arithmetic (`ts.data() + offset`)
to pass slices of the pre-allocated arrays — no per-row-group heap allocation. This isolates the
Parquet machinery cost from allocator cost.

---

#### `TEST_CASE("Write throughput — string column 10K rows")`

**What it is**  
Writes 10,000 string values from a 5-symbol pool using `RLE_DICTIONARY` encoding.

**What it represents**  
Dictionary-encoded string column throughput — the encoding of choice for high-cardinality string
columns with moderate repetition (e.g., instrument symbols: 5 distinct values repeated thousands
of times). Dictionary encoding stores the unique values once plus an RLE index, achieving very high
compression for symbol columns while maintaining fast random access.

**Why it is written this way**  
`Encoding::RLE_DICTIONARY` is explicitly set via `WriterOptions.default_encoding`. The pool of 5
symbols cycles via `i % symbols_pool.size()` to produce realistic repetition patterns. The
benchmark measures both dictionary construction and RLE index encoding throughput.

---

### bench_read.cpp — Read Throughput (5 cases)

**File**: `benchmarks/bench_read.cpp`  
**Tag**: `[bench][read]`  
**Purpose**: Measures how fast the `ParquetReader` can read columns of different types, with full
read, projection, and footer-only patterns.

**Critical design principle**: All fixture files are written **outside** the `BENCHMARK` lambda,
using static helper functions `write_fixture_50k()` and `write_fixture_50k_strings()`. The
benchmark measures only the read path, not write+read combined. Files are written to `tmp_` paths
and removed after the benchmark.

---

#### `TEST_CASE("Read throughput — typed column read<double> (50K rows)")`

**What it is**  
Opens a reader on a 50K-row file and calls `read_column<double>(0, 1)` inside the BENCHMARK.

**What it represents**  
Single-column typed read throughput — the hot path for ML feature extraction. `read_column<double>`
returns a typed `std::vector<double>` without going through string conversion, making it the
fastest read API for numeric columns.

**Why it is written this way**  
The reader is opened inside the BENCHMARK lambda (not outside) to include the file-open and
footer-parse cost in each measurement. This is intentional: in real applications, a model inference
pipeline opens the feature file, reads the required columns, and closes it in a single operation.
Measuring only the column decode (with a pre-opened reader) would understate the real latency.

---

#### `TEST_CASE("Read throughput — read_all string conversion (50K rows)")`

**What it is**  
Calls `read_all()` which reads all columns and converts all values to `std::vector<std::string>`.

**What it represents**  
CSV-export / general-purpose read throughput. `read_all()` exercises the complete decode + format
conversion pipeline. For numeric columns, this includes double→string conversion (expensive). This
benchmark establishes the upper bound on read cost when no type information is available to the
caller.

**Why it is written this way**  
The result is a `vector<vector<string>>`, and the total element count is summed and returned:
`for (auto& col : *result) total += col.size();`. This prevents the compiler from eliding the
`read_all()` call (since the return value is used in a computation that feeds the benchmark
return).

---

#### `TEST_CASE("Read throughput — column projection by name (50K rows)")`

**What it is**  
Reads only columns named "price" and "qty" from a 3-column file (skipping "ts").

**What it represents**  
Column projection performance — the primary read optimisation for wide schemas. Parquet's columnar
layout means a reader can seek directly to the column chunk for "price" and skip "ts" entirely.
This test measures whether the projection implementation achieves this skip-seek efficiency or
deserialises skipped columns anyway.

**Why it is written this way**  
`read_columns({"price", "qty"})` — name-based projection, not index-based. This exercises the
schema's `find_column()` lookup for each requested name before seeking to the column chunks.

---

#### `TEST_CASE("Read throughput — typed int64 column read (50K rows)")`

**What it is**  
Reads the `ts` (int64) column, mirroring the double column benchmark for direct comparison.

**What it represents**  
Integer vs double decode throughput comparison. PLAIN-encoded int64 is a raw 8-byte-per-value
read with no transformation. Comparing this benchmark against the double read shows whether the
read path has type-specific bottlenecks.

---

#### `TEST_CASE("Read latency — open + read footer only")`

**What it is**  
Opens the reader and calls `num_rows()` only — no column data is read. Uses the string-column
fixture (5-column schema) for a non-trivial footer.

**What it represents**  
Footer parse latency — the irreducible overhead for any Parquet read operation. Every read
must parse the footer before accessing columns. The footer contains the complete Thrift-encoded
`FileMetaData` including schema, row group metadata, column statistics, and offset indices.
This benchmark isolates the Thrift decode cost from the column data read cost.

**Why it is written this way**  
Uses `write_fixture_50k_strings()` which produces a 5-column schema (vs the 3-column numeric
fixture). A more complex schema produces a larger Thrift-encoded footer, giving a more realistic
measurement. `num_rows()` is the minimal read operation that requires footer parsing.

---

### bench_wal.cpp — WAL Latency (6 cases)

**File**: `benchmarks/bench_wal.cpp`  
**Tag**: `[wal][bench]`  
**Purpose**: Measures the Write-Ahead Log append latency. The WAL is the critical path for HFT
event capture. The key claim is **sub-1µs per append without fsync**.

**Design**: `TempDir` is an RAII struct that creates a `/tmp/signet_bench_wal_<timestamp>/`
directory and removes it on destruction. Using a RAII guard prevents directory leak if the test
panics.

---

#### `TEST_CASE("WAL single-record append latency (32B payload)")`

**What it is**  
Appends a 52-byte (`string_view` of "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000")
payload to a pre-opened `WalWriter` in a tight BENCHMARK loop.

**What it represents**  
The fundamental HFT tick logging latency. At 10 KHz tick rate, each append must complete in under
100µs to avoid backlog. Sub-1µs means the WAL is 100× below the tick rate budget. The 32B
payload simulates a compact binary tick record.

**Why it is written this way**  
`sync_on_append = false` and `sync_on_flush = false` — no fsync, userspace buffer only. This
measures pure in-process I/O overhead: `fwrite()` to a buffered C FILE stream. This is the
practical production setting for HFT: durability comes from WAL-to-Parquet compaction checkpoints,
not per-record fsync.

The writer is opened **outside** the BENCHMARK and reused across iterations. This measures steady-
state append throughput (the steady-state hot path), not including file-open overhead. `next_seq()`
is returned to prevent the compiler from eliding `append()`.

---

#### `TEST_CASE("WAL single-record append latency (256B payload)")`

**What it is**  
Same as above but with a 256-byte JSON-shaped order event payload.

**What it represents**  
Payload size scaling. Comparing 32B vs 256B append latency reveals whether latency is dominated by
fixed overhead (function call, mutex check, sequence counter increment) or by data movement
(memcpy into the write buffer). A 4× larger payload with the same latency indicates fixed-overhead
dominance — good for HFT where per-record overhead, not throughput, limits performance.

---

#### `TEST_CASE("WAL batch 1000 appends throughput")`

**What it is**  
Performs 1000 appends inside a single BENCHMARK iteration. Dividing the measured time by 1000
gives the amortised per-record cost.

**What it represents**  
Batch append throughput for event log streams. When a strategy produces 1000 events in a burst
(e.g., mass-cancel during a flash crash), the batch cost determines the blackout window. Amortised
cost < 100ns/record means a 1000-event burst takes < 100µs.

**Why it is written this way**  
The loop `for (int i = 0; i < 1000; ++i)` uses `(void)writer.append(payload)` to discard the
`expected<void>` return. The `return writer.next_seq()` after the loop ensures the entire
1000-append batch is not elided.

---

#### `TEST_CASE("WAL append + flush (no fsync)")`

**What it is**  
Every append is followed by `writer.flush(false)` (fflush only, no fsync).

**What it represents**  
Durability-sensitive append path. `fflush()` moves data from the C stdio buffer to the kernel
page cache. This ensures the data is visible to other processes (e.g., a compaction thread reading
the WAL) without the full disk-write cost of `fsync()`. This is the recommended setting for
medium-durability applications.

**Why it is written this way**  
`flush(false)` — the boolean argument selects fflush-only vs fsync. Measuring append+fflush vs
bare append (TEST_CASE 1) quantifies the cost of kernel-visible durability.

---

#### `TEST_CASE("WAL WalManager auto-segment append")`

**What it is**  
Uses `WalManager::append()` instead of `WalWriter::append()`.

**What it represents**  
Manager layer overhead. `WalManager` wraps `WalWriter` with: a mutex lock (for thread safety),
a segment-roll check (max bytes + max records), and a record counter increment. This benchmark
reveals the overhead of the management layer versus raw WAL write — the difference is the cost
of thread safety.

**Why it is written this way**  
`max_segment_bytes = 64MB` and `max_records = 1M` — both limits are set high enough that no
segment roll occurs during the benchmark. This isolates the per-append mutex + counter overhead
from the (infrequent) segment-roll I/O.

---

#### `TEST_CASE("WAL recovery — read_all from 10K record WAL")`

**What it is**  
Writes 10,000 records **before** the BENCHMARK, then benchmarks `WalReader::open() + read_all()`.

**What it represents**  
Cold-recovery latency — the time to replay uncommitted WAL entries on process restart. This is the
dominant cost during crash recovery in production. For an HFT system that crashes during a burst,
recovery time determines how quickly trading can resume.

**Why it is written this way**  
The 10K records are written outside the BENCHMARK (write cost not measured). Inside the BENCHMARK,
a **new** `WalReader` is opened per iteration (not reused), measuring the complete cold-read path
including the `fopen()` syscall. The payload varies per record (`snprintf` generates unique
content) to prevent the OS from serving the file from cache in a degenerate mode.

---

### bench_encodings.cpp — Encoding Speed + Size (5 cases)

**File**: `benchmarks/bench_encodings.cpp`  
**Tag**: `[encoding][delta|bss|rle][bench]`  
**Purpose**: Measures encoding and decoding throughput for the three non-trivial encodings. Also
validates size claims (DELTA compresses; BSS preserves size).

---

#### `TEST_CASE("Encoding: DELTA int64 timestamps — encode 10K")`

**What it is**  
Encodes and decodes 10,000 nanosecond timestamps spaced exactly 100ns apart.

**What it represents**  
Timestamp column compression for HFT tick data. Timestamps from a tick stream are nearly perfectly
monotonic with small fixed deltas (~100ns). DELTA encoding reduces such a sequence to a base value
plus a run of identical deltas, achieving extreme compression before a subsequent ZSTD/LZ4 pass.
For 100ns-spaced int64 timestamps, the DELTA output is typically < 5% of the PLAIN size.

**Why it is written this way**  
Two separate `BENCHMARK` macros — "delta encode" and "delta decode" — within the same TEST_CASE.
This is a Catch2 feature: multiple BENCHMARKs per TEST_CASE are reported separately, enabling
encode/decode throughput comparison side by side.

The pre-encoded buffer is computed once before the BENCHMARK so the decode benchmark measures pure
decode throughput, not encode+decode.

---

#### `TEST_CASE("Encoding: BYTE_STREAM_SPLIT float64 prices — encode 10K")`

**What it is**  
Encodes and decodes 10,000 doubles representing prices in [40000, 50000].

**What it represents**  
Price column BSS encode/decode throughput. BSS reorganises the byte planes of IEEE 754 doubles
so that byte-plane 7 (the sign+exponent byte, nearly constant for clustered prices) forms a
compressible run, and byte-plane 0 (the least-significant byte, nearly random) is grouped
separately. The subsequent LZ4/ZSTD pass achieves 30–50% better compression on BSS-preprocessed
data vs PLAIN.

**Why it is written this way**  
The 1-cent increment `prices[i] = 40000.0 + (i % 10000 * 1.0)` produces prices across a
$10,000 range, cycling. This is slightly less compressible than a narrow-range price series but
more realistic than a constant or monotonic sequence.

---

#### `TEST_CASE("Encoding: RLE boolean flags — encode 10K bits")`

**What it is**  
Encodes and decodes 10,000 bit-width-1 values with 90% zeros and 10% ones.

**What it represents**  
Bid/ask side flag column. In limit-order book data, the side column (0 = ask, 1 = bid) often
has long runs of one side (e.g., 90% ask quotes during passive market making). RLE encoding of
boolean columns with long runs achieves very high compression — a run of N identical bits
becomes a 1-byte count + 1-byte value, regardless of N.

**Why it is written this way**  
`bit_width = 1` in `RleEncoder::encode(flags.data(), N, 1)` specifies single-bit packing. The
RLE threshold is set to maximise long runs: `flags[i] = (i % 10 == 0) ? 1u : 0u` produces
runs of 9 zeros followed by a single one — the pattern that maximises RLE efficiency for a 10%
true rate.

---

#### `TEST_CASE("Encoding: DELTA vs PLAIN size comparison — 10K timestamps")`

**What it is**  
Verifies `delta_encoded.size() < plain_size / 2` (i.e., DELTA is more than 2× smaller than PLAIN)
and measures both encode throughput and a `memcpy` baseline.

**What it represents**  
The core size-claim validation for DELTA encoding. The assertion `CHECK(compressed_size < plain_size / 2)`
is a **correctness assertion embedded in a benchmark** — a deliberate design choice that turns
a benchmark case into both a performance measurement and a correctness guard. If a future
algorithm change causes DELTA to stop compressing effectively, the `CHECK` fails.

**Why it is written this way**  
The "plain copy baseline" benchmark measures the throughput of a raw `memcpy` of the same number
of bytes. Comparing DELTA encode throughput vs memcpy throughput reveals the overhead of the
DELTA algorithm itself: if DELTA is 2× slower than memcpy, the compression overhead is known.

Also includes a **full round-trip correctness check** (`for (size_t i = 0; i < N; ++i) REQUIRE(decoded[i] == timestamps[i])`)
— a rare pattern (correctness inside a benchmark file) that is intentional: the size guarantee
and correctness guarantee are inseparable properties of the DELTA encoding.

---

#### `TEST_CASE("Encoding: BYTE_STREAM_SPLIT vs PLAIN size — 10K prices")`

**What it is**  
Verifies `bss_encoded.size() == plain_size` (BSS is size-preserving) and measures encode/decode
throughput.

**What it represents**  
BSS is not a compressor — it is a byte-plane reorganiser. Its output size is always exactly
`N * sizeof(double)` bytes, regardless of input. This property is validated with
`CHECK(bss_encoded.size() == plain_size)`. The purpose of BSS is to improve downstream
compressibility, not to reduce size directly — a distinction that is critical for users who
might expect BSS to reduce file size without a following compression step.

**Why it is written this way**  
Both encode and decode benchmarks are in this TEST_CASE alongside the correctness checks. This
colocation ensures that the round-trip fidelity guarantee and the throughput claim are jointly
verified in a single build target. The pattern `CHECK` (soft assertion, continues on failure) vs
`REQUIRE` (hard assertion, aborts on failure) is used deliberately: size mismatch is a warning
(`CHECK`) but data corruption is fatal (`REQUIRE`).

---

### bench_feature_store.cpp — Feature Store Latency (5 cases)

**File**: `benchmarks/bench_feature_store.cpp`  
**Tag**: `[bench][feature_store]`  
**Purpose**: Measures feature write throughput, `get()` / `as_of()` / `as_of_batch()` /
`history()` latency. Key claims: single-entity retrieval < 50µs; batch-100 retrieval < 1ms.

**`FeatureDataset` struct**: A shared setup helper that writes 10,000 feature vectors for 100
entities (16 features each, 1ms timestamp spacing) to a temp dir and returns the resulting
`FeatureReader`. The `build()` method is called once per TEST_CASE, outside the BENCHMARK.

---

#### `TEST_CASE("Feature Store write throughput — 10K vectors, 16 features")`

**What it is**  
Creates a fresh `FeatureWriter`, calls `write_batch(batch)` with 10,000 pre-built feature vectors,
closes the writer, measures wall time for the full write.

**What it represents**  
Feature store ingest throughput for training data pipelines. At 10K vectors × 16 features, each
write iteration produces a 1.28MB feature set (10K × 16 × 8 bytes). This simulates one mini-batch
of training data being persisted before a model update.

**Why it is written this way**  
The batch of `FeatureVector` objects is built **outside** the BENCHMARK to exclude Python-side
object construction from the measurement. Inside the BENCHMARK, a fresh temp dir is created per
iteration so the writer always starts empty — measuring consistent write-to-new-file throughput
rather than write-append-to-existing throughput.

---

#### `TEST_CASE("Feature Store get() latency — 100 entities, 10K rows")`

**What it is**  
Calls `fr.get("entity_42")` 1000 times in a tight loop inside the BENCHMARK.

**What it represents**  
Online serving latency for ML inference. "entity_42" is one of 100 entities in the dataset.
`get()` returns the latest feature vector — a linear scan through the stored vectors for the
requested entity. For a well-indexed feature store, this should be O(log N) or better.

**Why it is written this way**  
1000 calls per BENCHMARK iteration, returning the count of successful lookups. Dividing the
BENCHMARK-reported mean by 1000 gives per-call latency. The entity is fixed ("entity_42") to
measure hot-path latency rather than average-over-all-entities latency, which would include cache-
miss variation.

---

#### `TEST_CASE("Feature Store as_of() latency — point-in-time correct lookup")`

**What it is**  
Calls `fr.as_of("entity_42", mid_ts)` 1000 times, where `mid_ts` is the timestamp at the
midpoint of the dataset.

**What it represents**  
Point-in-time feature serving for backtesting. `as_of()` requires a binary search through all
timestamps for the given entity to find the latest record at or before `mid_ts`. This is O(log N)
for a sorted index vs O(N) for a linear scan. The benchmark distinguishes between these two
internal implementations.

---

#### `TEST_CASE("Feature Store as_of_batch() — 100 entities at once")`

**What it is**  
Calls `fr.as_of_batch(ids, query_ts)` with 100 entity IDs in a single call.

**What it represents**  
Batch feature serving for online ML inference where a model scores many entities simultaneously
(e.g., all 100 instruments in a portfolio at every bar). Batch APIs amortise per-call overhead —
one index scan covering all 100 entities is cheaper than 100 independent `as_of()` calls.

---

#### `TEST_CASE("Feature Store history() range query")`

**What it is**  
Calls `fr.history("entity_0", start_ns, end_ns)` where the range covers exactly 100 records
(entity_0 appears at rows 0, 100, 200, ..., 9900 in the 10K-row dataset).

**What it represents**  
Historical feature retrieval for walk-forward cross-validation. Model validation requires
replaying training data in temporal order; `history()` provides the time-range scan needed to
extract a validation window for a specific entity.

---

### bench_event_bus.cpp — MPMC Throughput (5 cases)

**File**: `benchmarks/bench_event_bus.cpp`  
**Tag**: `[bench][mpmc_ring][column_batch][event_bus]`  
**Purpose**: Measures the throughput of the lock-free messaging layer. Key claims: MpmcRing<int64_t>
push+pop < 1µs single-threaded; 4P×4C concurrent throughput measured end-to-end.

---

#### `TEST_CASE("MpmcRing<int64_t> push+pop latency (single-threaded)")`

**What it is**  
Creates a `MpmcRing<int64_t>` of capacity 1024 outside the BENCHMARK, then inside the BENCHMARK
calls `ring.push(42LL)` followed by `ring.pop(v)`.

**What it represents**  
The atomic CAS (compare-and-swap) cycle cost for the Vyukov MPMC bounded queue. A single push+pop
round-trip involves two CAS operations on sequence counters (one at head, one at tail), two
acquire/release loads, and a cache-line write + read. The measured latency is primarily the L1
cache hit cost for the ring slot, since the ring is pre-warmed by the setup.

**Why it is written this way**  
The ring persists outside the BENCHMARK — it is created once and reused across all iterations.
This measures steady-state throughput (the ring is always in a partially-filled state) rather
than the cold-start path (empty ring, cold cache). The ring must be non-empty for `pop()` to
succeed; `push(42LL)` before `pop(v)` within the same BENCHMARK iteration guarantees this.

---

#### `TEST_CASE("MpmcRing<SharedColumnBatch> 4P×4C concurrent throughput")`

**What it is**  
Spawns 4 producer threads and 4 consumer threads inside the BENCHMARK lambda. Each producer
pushes 1000 items; consumers pop until all 4000 items are consumed.

**What it represents**  
Multi-threaded event bus throughput — the real-world use case where 4 exchange feed handlers push
market data batches and 4 strategy workers consume them. This measures aggregate throughput under
contention: 8 threads competing for slots in a ring buffer.

**Why it is written this way**  
A fresh ring (`MpmcRing<SharedColumnBatch> ring(4096)`) and fresh `std::atomic` counters are
created inside the BENCHMARK on each iteration. This ensures inter-run interference does not
inflate or deflate measurements. The `proto` batch is created outside the BENCHMARK and
copy-pushed — simulating real producer behaviour where the same data is broadcast to multiple
consumers via `shared_ptr`.

---

#### `TEST_CASE("ColumnBatch push_row + column_view (no copy)")`

**What it is**  
Creates a `ColumnBatch` with 8 columns, pushes 1000 rows, then calls `column_view(0)` and
`column_view(7)`.

**What it represents**  
Zero-copy column access latency. `column_view()` returns a `TensorView` pointing directly into the
ColumnBatch's internal buffer — no heap allocation, no memcpy. The view cost should be O(1)
regardless of batch size. This benchmark validates the zero-copy claim: if `column_view` allocates
memory, the latency would scale with batch size.

**Why it is written this way**  
A fresh `ColumnBatch` is created per iteration to include allocation cost in the measurement.
The `view_size` (sum of both views' element counts) is returned to prevent the compiler from
eliding the `column_view()` calls.

---

#### `TEST_CASE("ColumnBatch as_tensor() — 1024 rows × 8 columns")`

**What it is**  
Calls `batch.as_tensor()` on a pre-filled 1024-row × 8-column batch.

**What it represents**  
ONNX/PyTorch inference handoff latency. `as_tensor()` converts the columnar ColumnBatch layout
(column-major: all values of column 0, then column 1, ...) to a row-major 2D tensor layout
(row 0: [c0, c1, ..., c7], row 1: ...) suitable for ML model inference. This involves a transpose
and a type conversion (double → float32 by default). The benchmark measures the data layout
transformation cost.

---

#### `TEST_CASE("EventBus publish + pop single-threaded throughput")`

**What it is**  
Creates a fresh `EventBus` per iteration, publishes 1000 batches and pops 1000 times in a loop.

**What it represents**  
EventBus routing overhead. The EventBus adds a tier-routing layer above MpmcRing: Tier 1 (fast
path, same core), Tier 2 (inter-core ring), Tier 3 (inter-node if multi-machine). This benchmark
measures the Tier 2 path (MPMC ring). Comparing with the raw MpmcRing benchmark (TEST_CASE 1 in
this file) reveals the EventBus layer overhead per message.

**Why it is written this way**  
A new `EventBus` is created per BENCHMARK iteration (inside the lambda). This is different from
the MpmcRing benchmark which reuses the ring. The difference is intentional: EventBus construction
is more expensive (tier initialisation, stats allocation) and should be included in the measurement
to reveal whether EventBus can be created at startup (once) or must be a persistent object.
`bus.stats().published` is returned to prevent elision.

---

## How to Run Everything

### C++ unit tests (dev build)
```bash
cd /path/to/signet-forge
cmake --preset dev-tests
cmake --build build --target signet_tests
cd build && ctest --output-on-failure
```

### C++ benchmarks (release build)
```bash
cd /path/to/signet-forge
cmake --preset benchmarks
cmake --build build-benchmarks --target signet_benchmarks

# Run all benchmarks with 100 samples:
./build-benchmarks/signet_benchmarks "[bench]" --benchmark-samples 100

# Run a specific category:
./build-benchmarks/signet_benchmarks "[bench][write]" --benchmark-samples 200
./build-benchmarks/signet_benchmarks "[bench][read]"  --benchmark-samples 200
./build-benchmarks/signet_benchmarks "[wal][bench]"   --benchmark-samples 200
./build-benchmarks/signet_benchmarks "[encoding][bench]" --benchmark-samples 200
./build-benchmarks/signet_benchmarks "[bench][feature_store]" --benchmark-samples 100
./build-benchmarks/signet_benchmarks "[bench][mpmc_ring]" --benchmark-samples 200
```

### Python tests
```bash
cd /path/to/signet-forge

# Build the Python module first:
cmake --preset python
cmake --build build-python --target _bindings

# Copy .so to package directory:
cp build-python/_bindings.*.so python/signet_forge/

# Run tests:
PYTHONPATH=python python3 -m pytest python/tests/test_python_bindings.py -v
```

### Use the bash runner script for categories:
```bash
cd /path/to/signet-forge
./scripts/run_benchmarks.sh --category wal
./scripts/run_benchmarks.sh --category all --samples 200
```

---

## Interpreting Output

Catch2 BENCHMARK output format:
```
benchmark name         samples  iterations  estimated  mean        std dev
write int64 10K            100          11    2.6501 ms   1.35863 ms  37.0 µs
```

| Column | Meaning |
|--------|---------|
| `samples` | Number of outer timing samples (from `--benchmark-samples`) |
| `iterations` | Inner loop iterations per sample (Catch2 auto-selected for stability) |
| `estimated` | Total elapsed wall time for all samples |
| `mean` | The number to compare against claims (per-iteration mean) |
| `std dev` | Standard deviation — a high std dev relative to mean indicates thermal/OS noise |

**Rule of thumb**: mean should be the primary comparison metric. std dev > 20% of mean suggests the
machine is thermally throttling or has background load; re-run with `nice -20` or after a warm-up.

**For WAL benchmarks**: the mean should be < 1µs for 32B append (the sub-µs claim). If it exceeds
2µs on a warm machine, this indicates the C++ `FILE*` buffering is not engaged or the tmpfs is
unexpectedly slow.

**For feature store benchmarks**: as_of() mean ÷ 1000 (calls per iteration) gives per-call
latency. With 10K rows and 100 entities, expect 5–20µs per call on a warm cache.

---

## Security Hardening Tests (added Feb 2026)

A hardening pass added 11 new negative/boundary tests covering 6 confirmed vulnerabilities:

| Test file | Tests added | What they verify |
|-----------|-------------|-----------------|
| `tests/test_encoding.cpp` | 6 | BSS decode OOB (float + double), RLE bit_width=0, bit_width=65 (encode + decode) |
| `tests/test_interop.cpp` | 3 | Thrift nesting depth > 128 → error flag; ParquetReader bad magic → error; ParquetReader truncated file → error |
| `python/tests/test_python_bindings.py` | 3 | write_column OOB → IndexError; DecisionLogWriter path traversal blocked; chain_id invalid chars blocked |

All 6 vulnerabilities confirmed fixed prior to first public commit. Total test count after pass #1: **317/317**.

---

## Thrift Correctness Phase Tests — `tests/test_thrift_types.cpp` (47 tests, added Mar 2026)

**Total count after phase: 826/826.**

This test file covers the complete Thrift Correctness Phase: protocol primitives, field-type validation, required-field bitmask enforcement, all new LogicalType and struct types, encryption types, bloom filter header, ordering types, and structured error propagation. It is the CI gate for the parquet-format 2.9.0 Thrift schema layer.

### § Protocol Correctness (3 tests, `[thrift][protocol]`)

| Test | What it verifies |
|------|-----------------|
| `zigzag_encode_i32: no signed-shift UB` | Round-trips 0, -1, 1, INT32_MIN, INT32_MAX through write_i32/read_i32; verifies the fixed unsigned-space ZigZag (CWE-190 fix) |
| `zigzag_encode_i64: round-trip boundary values` | INT64_MIN and INT64_MAX round-trip exactly |
| `write_i8/read_i8: raw byte round-trip` | All boundary values (-128, -1, 0, 1, 127) encode as exactly 1 byte and decode correctly |

**Why written this way**: ZigZag correctness at boundary values is the canonical regression test for the signed-shift UB fix. INT32_MIN and INT64_MIN produce the maximum ZigZag value; encoding these confirms the fix produces the right bit pattern rather than UB.

### § Field-Type Validation (5 tests, `[thrift][ftype]`)

| Test | Type error injected | CWE |
|------|--------------------|----|
| `KeyValue: wrong wire type for key field` | Field 1 encoded as I32 instead of BINARY | CWE-843 |
| `Statistics: wrong wire type for null_count` | Field 3 encoded as I32 instead of I64 | CWE-843 |
| `DataPageHeader: wrong wire type for num_values` | Field 1 encoded as BINARY instead of I32 | CWE-843 |
| `IntType: wrong wire type for bit_width` | Field 1 encoded as I32 instead of I8 | CWE-843 |
| `DecimalType: wrong wire type for scale` | Field 1 encoded as I64 instead of I32 | CWE-843 |

**Why written this way**: Each test hand-encodes a struct with the correct field ID but the wrong wire type. This is the exact attack vector for CWE-843 (type confusion) — a malformed file where a BINARY blob is interpreted as an integer. The tests confirm that all deserializers return `expected<void>` error (not silently read garbage) on wire-type mismatch.

### § Required-Field Bitmask (5 tests, `[thrift][required]`)

| Test | Required field(s) absent | Expected result |
|------|--------------------------|----------------|
| `KeyValue: missing required key field` | Field 1 (key) | Error |
| `KeyValue: all required fields present` | — | Success, values preserved |
| `DataPageHeader: missing required fields` | Fields 2-4 absent | Error |
| `DataPageHeader: all required fields present` | — | Success, round-trip |
| `ColumnMetaData: missing required fields` | Fields 2-7,9 absent | Error |

**Why written this way**: Required-field absence produces silently defaulted objects without the bitmask, which would write semantically corrupt Parquet files (e.g., ColumnMetaData with encoding=0 even if the actual encoding is RLE). These tests confirm the `uint32_t seen` bitmask detects missing required fields.

### § TimeUnit Round-Trip (3 tests, `[thrift][logical_type]`)

All three TimeUnit variants (MILLIS, MICROS, NANOS) round-trip through serialize/deserialize, verifying the empty-struct union encoding (each variant is a zero-byte nested struct at its field ID).

### § LogicalTypeUnion Round-Trip (8 tests, `[thrift][logical_type]`)

| Test | Variant | Payload verified |
|------|---------|-----------------|
| STRING | Kind::STRING | No payload; kind preserved |
| DECIMAL | Kind::DECIMAL | scale=4, precision=18 |
| TIMESTAMP (MICROS, UTC) | Kind::TIMESTAMP | is_adjusted_to_utc=true, unit.kind=MICROS |
| TIMESTAMP (MILLIS, non-UTC) | Kind::TIMESTAMP | is_adjusted_to_utc=false, unit.kind=MILLIS |
| INT(32, signed) | Kind::INT | bit_width=32, is_signed=true |
| INT(8, unsigned) | Kind::INT | bit_width=8, is_signed=false |
| UUID | Kind::UUID | No payload; kind preserved |
| STRING type validation | Wrong wire type (BINARY) | Returns error |

**Why written this way**: Each of the 5 Sub-phase A types is exercised with representative financial/AI values (Decimal precision=18 covers 18-digit financial amounts; Timestamp MICROS UTC covers market data; INT(32,signed) covers order IDs; INT(8,unsigned) covers flags). The type-validation test confirms that the union's STRUCT requirement is enforced.

### § SchemaElement with logicalType (2 tests, `[thrift][schema]`)

Round-trips `SchemaElement` with `field_id` (field 9) and `logical_type` (field 10) for STRING and DECIMAL variants. Verifies that the two new fields survive serialize/deserialize alongside the existing fields 1-8.

### § DataPageHeader with statistics (1 test, `[thrift][page_header]`)

Round-trips `DataPageHeader` with a `Statistics` struct in field 5 (null_count=3, min_value="aaa", max_value="zzz"). Verifies nested struct propagation and ftype validation on the statistics field.

### § DictionaryPageHeader with is_sorted (3 tests, `[thrift][page_header]`)

Round-trips with is_sorted=true, is_sorted=false, and without is_sorted (verifying absent optional round-trips correctly).

### § Ordering Types (3 tests, `[thrift][ordering]`)

| Test | Type | Values verified |
|------|------|----------------|
| `ColumnOrder: TYPE_ORDER` | `ColumnOrder` | kind=TYPE_ORDER preserved |
| `SortingColumn: descending, nulls_first=false` | `SortingColumn` | column_idx=3, descending=true, nulls_first=false |
| `SortingColumn: ascending, nulls_first=true` | `SortingColumn` | column_idx=0, descending=false, nulls_first=true |

### § Bloom Filter Header Types (4 tests, `[thrift][bloom]`)

Each of `BloomFilterAlgorithm` (BLOCK), `BloomFilterHash` (XXHASH), `BloomFilterCompression` (UNCOMPRESSED) is individually round-tripped to verify the empty-struct union encoding. `BloomFilterHeader` is round-tripped with num_bytes=1024 and all three sub-structs set.

### § Encryption Types (6 tests, `[thrift][encryption]`)

| Test | Type | Key assertion |
|------|------|--------------|
| `AesGcmV1: with all fields` | `AesGcmV1` | aad_prefix=[1,2,3], aad_file_unique=true, supply_aad_prefix=false |
| `AesGcmV1: without optional fields` | `AesGcmV1` | All optionals absent after round-trip |
| `EncryptionAlgorithm: AES_GCM_V1` | `EncryptionAlgorithm` | Kind=AES_GCM_V1, payload preserved |
| `EncryptionAlgorithm: AES_GCM_CTR_V1` | `EncryptionAlgorithm` | Kind=AES_GCM_CTR_V1, supply_aad_prefix=false |
| `FileCryptoMetaData: with key_metadata` | `FileCryptoMetaData` | algorithm kind + key_metadata=[0xAA,0xBB,0xCC] |
| `ColumnCryptoMetaData: COLUMN_KEY` | `ColumnCryptoMetaData` | path_in_schema=["a","b","c"], key_metadata=[0x11,0x22] |

### § Error Propagation (2 tests, `[thrift][error_propagation]`)

| Test | Scenario | Assertion |
|------|----------|-----------|
| `expected<void>: nested Statistics failure` | DataPageHeader with Statistics having I32 for null_count (expects I64) | Outer `DataPageHeader::deserialize()` returns error — not just inner |
| `expected<void>: error code is THRIFT_DECODE_ERROR` | Missing required key field in KeyValue | `r.error().code == ErrorCode::THRIFT_DECODE_ERROR` |

**Why written this way**: The first test confirms that the error propagation chain `DataPageHeader::deserialize → Statistics::deserialize → field type mismatch` returns an error to the outermost caller rather than silently continuing. The second confirms the error code is specifically `THRIFT_DECODE_ERROR`, not a generic code, so callers can distinguish Thrift parse failures from I/O or corruption errors.

### § RowGroup with sorting_columns (1 test, `[thrift][rowgroup]`)

Round-trips a `RowGroup` with two `SortingColumn` entries (column 0 ascending nulls-first, column 1 descending nulls-last). Verifies that LIST-encoded structs in field 4 survive the full serialize/deserialize cycle with all three fields of each `SortingColumn` preserved.

### § FileMetaData with column_orders (1 test, `[thrift][filemetadata]`)

Round-trips a `FileMetaData` with two `ColumnOrder` entries in field 7. Verifies that the `optional<vector<ColumnOrder>>` field is correctly emitted and recovered.

### How to Run

```bash
# All Thrift Correctness Phase tests:
cd build-server-pq && ./signet_tests "[thrift]"

# Specific tag groups:
./signet_tests "[thrift][protocol]"
./signet_tests "[thrift][ftype]"
./signet_tests "[thrift][required]"
./signet_tests "[thrift][logical_type]"
./signet_tests "[thrift][encryption]"
./signet_tests "[thrift][error_propagation]"

# CTest filter:
ctest -R test_thrift_types --output-on-failure
```

> Hardening pass #2 added 6 more guards + 6 tests (334 total). Hardening pass #3 added 23 more guards + 22 tests (390 total). Hardening pass #4 added 29 more guards + 4 tests (394 total). Hardening pass #5 added 53 more guards + 29 tests (423 total). Static audit follow-up completed 11 additional fixes (no new tests — existing 423 cover all paths). See CHANGELOG.md for full details.
