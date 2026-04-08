"""
test_python_bindings.py — pytest suite for signet_forge Python bindings.

Run: pytest python/tests/test_python_bindings.py -v
(Requires the _bindings.so to be built and on PYTHONPATH)
"""
import sys, os, math
# Allow running from project root: add build-python to sys.path if needed
# Users should set PYTHONPATH=build-python before running.

import pytest
import numpy as np
import signet_forge as sp

# ===========================================================================
# Helpers
# ===========================================================================

def make_schema():
    return (sp.SchemaBuilder("test")
            .int64("timestamp")
            .double_("price")
            .string("symbol")
            .build())


def write_test_file(path, n=100):
    """Write n rows with 3 columns to path. Returns path as string."""
    schema = make_schema()
    opts = sp.WriterOptions()
    opts.row_group_size = 50

    writer = sp.ParquetWriter.open(str(path), schema, opts)
    ts  = np.arange(n, dtype=np.int64)
    px  = np.linspace(40000.0, 41000.0, n)
    sym = ["BTCUSDT"] * n

    writer.write_column_int64(0, ts)
    writer.write_column_double(1, px)
    writer.write_column_string(2, sym)
    writer.flush_row_group()
    writer.close()
    return str(path)


# ===========================================================================
# Section 1 — Schema / SchemaBuilder
# ===========================================================================

def test_schema_builder_basic():
    schema = make_schema()
    assert schema.num_columns == 3
    assert schema.name == "test"

def test_schema_column_names():
    schema = make_schema()
    assert schema.column(0).name == "timestamp"
    assert schema.column(1).name == "price"
    assert schema.column(2).name == "symbol"

def test_schema_find_column():
    schema = make_schema()
    assert schema.find_column("price") == 1
    assert schema.find_column("nonexistent") is None

def test_schema_builder_all_types():
    schema = (sp.SchemaBuilder("all_types")
              .bool_("flag")
              .int32("int32_col")
              .int64("int64_col")
              .float_("float_col")
              .double_("double_col")
              .string("str_col")
              .build())
    assert schema.num_columns == 6

def test_schema_builder_timestamp():
    schema = sp.SchemaBuilder("ts").int64_ts("event_time").build()
    assert schema.num_columns == 1
    assert schema.column(0).logical_type == sp.LogicalType.TIMESTAMP_NS


# ===========================================================================
# Section 2 — ParquetWriter
# ===========================================================================

def test_writer_opens_and_closes(tmp_path):
    path = tmp_path / "test.parquet"
    schema = make_schema()
    writer = sp.ParquetWriter.open(str(path), schema)
    assert writer.is_open
    writer.close()
    assert path.exists()

def test_writer_context_manager(tmp_path):
    path = tmp_path / "cm.parquet"
    schema = make_schema()
    with sp.ParquetWriter.open(str(path), schema) as writer:
        assert writer.is_open
    assert path.exists()

def test_writer_write_int64_column(tmp_path):
    path = tmp_path / "int64.parquet"
    schema = sp.SchemaBuilder("s").int64("ts").build()
    writer = sp.ParquetWriter.open(str(path), schema)
    writer.write_column_int64(0, np.arange(10, dtype=np.int64))
    writer.flush_row_group()
    writer.close()
    assert writer.rows_written == 10

def test_writer_write_double_column(tmp_path):
    path = tmp_path / "double.parquet"
    schema = sp.SchemaBuilder("s").double_("price").build()
    writer = sp.ParquetWriter.open(str(path), schema)
    prices = np.array([1.1, 2.2, 3.3], dtype=np.float64)
    writer.write_column_double(0, prices)
    writer.flush_row_group()
    writer.close()
    assert writer.rows_written == 3

def test_writer_write_string_column(tmp_path):
    path = tmp_path / "str.parquet"
    schema = sp.SchemaBuilder("s").string("sym").build()
    writer = sp.ParquetWriter.open(str(path), schema)
    writer.write_column_string(0, ["BTCUSDT", "ETHUSDT", "SOLUSDT"])
    writer.flush_row_group()
    writer.close()
    assert writer.rows_written == 3

def test_writer_write_bool_column(tmp_path):
    path = tmp_path / "bool.parquet"
    schema = sp.SchemaBuilder("s").bool_("flag").build()
    writer = sp.ParquetWriter.open(str(path), schema)
    flags = np.array([True, False, True, True, False], dtype=bool)
    writer.write_column_bool(0, flags)
    writer.flush_row_group()
    writer.close()
    assert writer.rows_written == 5

def test_writer_multiple_row_groups(tmp_path):
    path = tmp_path / "multi_rg.parquet"
    schema = sp.SchemaBuilder("s").int64("ts").build()
    opts = sp.WriterOptions()
    opts.row_group_size = 50
    writer = sp.ParquetWriter.open(str(path), schema, opts)
    for rg in range(3):
        writer.write_column_int64(0, np.arange(rg * 50, (rg + 1) * 50, dtype=np.int64))
        writer.flush_row_group()
    writer.close()
    assert writer.rows_written == 150
    assert writer.row_groups_written == 3


# ===========================================================================
# Section 3 — ParquetReader
# ===========================================================================

def test_reader_opens(tmp_path):
    path = write_test_file(tmp_path / "r.parquet")
    reader = sp.ParquetReader.open(path)
    assert reader.num_rows > 0

def test_reader_schema(tmp_path):
    path = write_test_file(tmp_path / "r.parquet")
    reader = sp.ParquetReader.open(path)
    assert reader.schema.num_columns == 3

def test_reader_read_column_int64(tmp_path):
    path = write_test_file(tmp_path / "r.parquet", n=50)
    reader = sp.ParquetReader.open(path)
    arr = reader.read_column_int64(0, 0)
    assert isinstance(arr, np.ndarray)
    assert arr.dtype == np.int64
    assert len(arr) == 50
    assert arr[0] == 0
    assert arr[49] == 49

def test_reader_read_column_double(tmp_path):
    path = write_test_file(tmp_path / "r.parquet", n=50)
    reader = sp.ParquetReader.open(path)
    arr = reader.read_column_double(0, 1)
    assert isinstance(arr, np.ndarray)
    assert arr.dtype == np.float64
    assert len(arr) == 50
    assert arr[0] == pytest.approx(40000.0, abs=1.0)

def test_reader_read_column_string(tmp_path):
    path = write_test_file(tmp_path / "r.parquet", n=10)
    reader = sp.ParquetReader.open(path)
    syms = reader.read_column_string(0, 2)
    assert isinstance(syms, list)
    assert all(s == "BTCUSDT" for s in syms)

def test_roundtrip_int64(tmp_path):
    path = str(tmp_path / "rt.parquet")
    data = np.array([10, 20, 30, 40, 50], dtype=np.int64)
    schema = sp.SchemaBuilder("rt").int64("v").build()
    writer = sp.ParquetWriter.open(path, schema)
    writer.write_column_int64(0, data)
    writer.flush_row_group()
    writer.close()

    reader = sp.ParquetReader.open(path)
    result = reader.read_column_int64(0, 0)
    np.testing.assert_array_equal(result, data)

def test_roundtrip_double(tmp_path):
    path = str(tmp_path / "rt.parquet")
    data = np.array([3.14, 2.71, 1.41], dtype=np.float64)
    schema = sp.SchemaBuilder("rt").double_("v").build()
    writer = sp.ParquetWriter.open(path, schema)
    writer.write_column_double(0, data)
    writer.flush_row_group()
    writer.close()

    reader = sp.ParquetReader.open(path)
    result = reader.read_column_double(0, 0)
    np.testing.assert_array_almost_equal(result, data)

def test_reader_context_manager(tmp_path):
    path = write_test_file(tmp_path / "r.parquet")
    with sp.ParquetReader.open(path) as reader:
        assert reader.num_rows == 100


# ===========================================================================
# Section 4 — Feature Store
# ===========================================================================

def make_fw_opts(output_dir, feature_names):
    group = sp.FeatureGroupDef()
    group.name = "test_group"
    group.feature_names = feature_names
    group.schema_version = 1

    opts = sp.FeatureWriterOptions()
    opts.output_dir = str(output_dir)
    opts.group = group
    opts.file_prefix = "feat"
    return opts


def test_feature_vector_fields():
    fv = sp.FeatureVector()
    fv.entity_id = "entity_0"
    fv.timestamp_ns = 1_700_000_000_000_000_000
    fv.version = 1
    fv.values = [1.0, 2.0, 3.0]
    assert fv.entity_id == "entity_0"
    assert len(fv.values) == 3


def test_feature_writer_creates_file(tmp_path):
    opts = make_fw_opts(tmp_path, ["f0", "f1"])
    fw = sp.FeatureWriter.create(opts)
    fv = sp.FeatureVector()
    fv.entity_id = "e0"
    fv.timestamp_ns = 1_000_000
    fv.version = 1
    fv.values = [1.0, 2.0]
    fw.write(fv)
    fw.close()
    assert fw.rows_written == 1
    assert len(fw.output_files) > 0


def test_feature_reader_get(tmp_path):
    opts = make_fw_opts(tmp_path, ["f0", "f1"])
    fw = sp.FeatureWriter.create(opts)
    for i in range(5):
        fv = sp.FeatureVector()
        fv.entity_id = "e0"
        fv.timestamp_ns = i * 1_000_000
        fv.version = 1
        fv.values = [float(i), float(i * 2)]
        fw.write(fv)
    fw.close()

    ro = sp.FeatureReaderOptions()
    ro.parquet_files = fw.output_files
    reader = sp.FeatureReader.open(ro)
    result = reader.get("e0")
    assert result is not None
    assert result.entity_id == "e0"


def test_feature_reader_as_of(tmp_path):
    opts = make_fw_opts(tmp_path, ["f0"])
    fw = sp.FeatureWriter.create(opts)
    for i in range(10):
        fv = sp.FeatureVector()
        fv.entity_id = "e0"
        fv.timestamp_ns = i * 1_000_000
        fv.version = 1
        fv.values = [float(i)]
        fw.write(fv)
    fw.close()

    ro = sp.FeatureReaderOptions()
    ro.parquet_files = fw.output_files
    reader = sp.FeatureReader.open(ro)
    result = reader.as_of("e0", 5 * 1_000_000)
    assert result is not None
    assert result.timestamp_ns <= 5 * 1_000_000


def test_feature_reader_missing_entity(tmp_path):
    opts = make_fw_opts(tmp_path, ["f0"])
    fw = sp.FeatureWriter.create(opts)
    fv = sp.FeatureVector()
    fv.entity_id = "e0"
    fv.timestamp_ns = 1_000_000
    fv.version = 1
    fv.values = [1.0]
    fw.write(fv)
    fw.close()

    ro = sp.FeatureReaderOptions()
    ro.parquet_files = fw.output_files
    reader = sp.FeatureReader.open(ro)
    result = reader.get("nonexistent")
    assert result is None


# ===========================================================================
# Section 5 — Decision + Inference Logs
# ===========================================================================

def test_decision_log_writer(tmp_path):
    writer = sp.DecisionLogWriter(str(tmp_path), "chain-001")
    rec = sp.DecisionRecord()
    rec.timestamp_ns = 1_700_000_000_000_000_000
    rec.strategy_id = 42
    rec.model_version = "v1.0"
    rec.decision_type = sp.DecisionType.ORDER_NEW
    rec.input_features = [0.1, 0.2, 0.3]
    rec.model_output = 0.85
    rec.confidence = 0.9
    rec.risk_result = sp.RiskGateResult.PASSED
    rec.order_id = "ORD-001"
    rec.symbol = "BTCUSDT"
    rec.price = 45000.0
    rec.quantity = 0.1
    rec.venue = "BINANCE"
    writer.log(rec)
    writer.close()
    assert len(writer.current_file_path()) > 0


def test_inference_log_writer(tmp_path):
    writer = sp.InferenceLogWriter(str(tmp_path), "chain-inf-001")
    rec = sp.InferenceRecord()
    rec.timestamp_ns = 1_700_000_000_000_000_000
    rec.model_id = "alpha-model"
    rec.model_version = "v2.0"
    rec.inference_type = sp.InferenceType.CLASSIFICATION
    rec.input_embedding = [0.1, 0.2]
    rec.input_hash = "abc123"
    rec.output_hash = "def456"
    rec.output_score = 0.75
    rec.latency_ns = 5000
    rec.batch_size = 1
    writer.log(rec)
    writer.close()
    assert len(writer.current_file_path()) > 0


# ===========================================================================
# Section 6 — Compliance Reports
# ===========================================================================

def write_decision_logs(tmp_path, n=5):
    writer = sp.DecisionLogWriter(str(tmp_path), "chain-test")
    base_ts = 1_700_000_000_000_000_000
    for i in range(n):
        rec = sp.DecisionRecord()
        rec.timestamp_ns = base_ts + i * 1_000_000
        rec.strategy_id = 1
        rec.model_version = "v1.0"
        rec.decision_type = sp.DecisionType.ORDER_NEW
        rec.input_features = [0.1 * i]
        rec.model_output = 0.5
        rec.confidence = 0.8
        rec.risk_result = sp.RiskGateResult.PASSED
        rec.order_id = f"ORD-{i:04d}"
        rec.symbol = "BTCUSDT"
        rec.price = 45000.0
        rec.quantity = 0.1
        rec.venue = "BINANCE"
        writer.log(rec)
    writer.close()
    return writer.current_file_path()


def write_inference_logs(tmp_path, n=5):
    writer = sp.InferenceLogWriter(str(tmp_path), "chain-inf-test")
    base_ts = 1_700_000_000_000_000_000
    for i in range(n):
        rec = sp.InferenceRecord()
        rec.timestamp_ns = base_ts + i * 500_000
        rec.model_id = "alpha-model"
        rec.model_version = "v2.0"
        rec.inference_type = sp.InferenceType.CLASSIFICATION
        rec.input_embedding = [0.1]
        rec.input_hash = f"hash-{i}"
        rec.output_hash = f"out-{i}"
        rec.output_score = 0.7
        rec.latency_ns = 1000 + i * 50
        rec.batch_size = 1
        writer.log(rec)
    writer.close()
    return writer.current_file_path()


def test_mifid2_reporter_json(tmp_path):
    dec_dir = tmp_path / "dec"
    dec_dir.mkdir()
    log_path = write_decision_logs(dec_dir, n=10)

    opts = sp.ReportOptions()
    opts.firm_id = "TEST_FIRM"
    report = sp.MiFID2Reporter.generate([log_path], opts)
    assert report.total_records == 10
    assert report.standard == sp.ComplianceStandard.MIFID2_RTS24
    assert "MiFID_II_RTS_24" in report.content
    assert "TEST_FIRM" in report.content


def test_mifid2_reporter_csv(tmp_path):
    dec_dir = tmp_path / "dec"
    dec_dir.mkdir()
    log_path = write_decision_logs(dec_dir, n=5)

    opts = sp.ReportOptions()
    opts.format = sp.ReportFormat.CSV
    report = sp.MiFID2Reporter.generate([log_path], opts)
    assert report.format == sp.ReportFormat.CSV
    assert "timestamp_utc" in report.content  # header row present


def test_eu_ai_act_article12(tmp_path):
    inf_dir = tmp_path / "inf"
    inf_dir.mkdir()
    log_path = write_inference_logs(inf_dir, n=10)

    opts = sp.ReportOptions()
    opts.system_id = "trading-ai"
    report = sp.EUAIActReporter.generate_article12([log_path], opts)
    assert report.total_records == 10
    assert report.standard == sp.ComplianceStandard.EU_AI_ACT_ART12
    assert "EU_AI_ACT_ARTICLE_12" in report.content


def test_eu_ai_act_article19(tmp_path):
    dec_dir = tmp_path / "dec19"
    inf_dir = tmp_path / "inf19"
    dec_dir.mkdir()
    inf_dir.mkdir()
    dec_path = write_decision_logs(dec_dir, n=5)
    inf_path = write_inference_logs(inf_dir, n=5)

    report = sp.EUAIActReporter.generate_article19([dec_path], [inf_path])
    assert report.standard == sp.ComplianceStandard.EU_AI_ACT_ART19
    assert "conformity_status" in report.content


def test_mifid2_csv_header():
    header = sp.MiFID2Reporter.csv_header()
    assert "timestamp_utc" in header
    assert "order_id" in header
    assert "instrument" in header


# ===========================================================================
# Section 7 — Security Hardening Tests
# ===========================================================================

def test_write_column_oob_raises_index_error(tmp_path):
    """write_column with col >= num_columns must raise IndexError, not crash."""
    schema = (sp.SchemaBuilder("oob_test")
              .int64("ts")
              .double_("price")
              .build())
    writer = sp.ParquetWriter.open(str(tmp_path / "oob.parquet"), schema)
    with pytest.raises(IndexError):
        writer.write_column_int64(999, np.arange(5, dtype=np.int64))
    writer.close()


def test_decision_log_path_traversal_blocked(tmp_path):
    """output_dir with '..' segments must raise RuntimeError."""
    if not hasattr(sp, 'DecisionLogWriter'):
        pytest.skip("AI audit module not built")
    with pytest.raises((RuntimeError, ValueError)):
        sp.DecisionLogWriter(str(tmp_path / "safe" / ".." / "escape"), "chain1")


def test_decision_log_chain_id_invalid_chars_blocked(tmp_path):
    """chain_id with path-traversal chars must raise RuntimeError."""
    if not hasattr(sp, 'DecisionLogWriter'):
        pytest.skip("AI audit module not built")
    safe_dir = str(tmp_path)
    with pytest.raises((RuntimeError, ValueError)):
        sp.DecisionLogWriter(safe_dir, "../badchain")
