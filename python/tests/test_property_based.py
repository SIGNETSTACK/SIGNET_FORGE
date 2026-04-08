"""
test_property_based.py — Hypothesis property-based tests for signet_forge Python bindings.

Run: pytest python/tests/test_property_based.py -v
Requires: pip install hypothesis

These tests use randomized data generators with automatic shrinking to find
edge cases in write/read roundtrip paths that hand-picked vectors miss.
"""
import pytest
import numpy as np

try:
    from hypothesis import given, settings, assume
    from hypothesis import strategies as st
    from hypothesis.extra.numpy import arrays
    HAS_HYPOTHESIS = True
except ImportError:
    HAS_HYPOTHESIS = False

import signet_forge as sp


pytestmark = pytest.mark.skipif(
    not HAS_HYPOTHESIS,
    reason="hypothesis not installed (pip install hypothesis)"
)


# ===========================================================================
# Helpers
# ===========================================================================

def roundtrip_int64(tmp_path, data):
    """Write int64 array, read back, verify identity."""
    path = str(tmp_path / "prop_int64.parquet")
    schema = sp.SchemaBuilder("prop").int64("v").build()
    writer = sp.ParquetWriter.open(path, schema)
    writer.write_column_int64(0, data)
    writer.flush_row_group()
    writer.close()

    reader = sp.ParquetReader.open(path)
    result = reader.read_column_int64(0, 0)
    return result


def roundtrip_double(tmp_path, data):
    """Write float64 array, read back, verify identity."""
    path = str(tmp_path / "prop_double.parquet")
    schema = sp.SchemaBuilder("prop").double_("v").build()
    writer = sp.ParquetWriter.open(path, schema)
    writer.write_column_double(0, data)
    writer.flush_row_group()
    writer.close()

    reader = sp.ParquetReader.open(path)
    result = reader.read_column_double(0, 0)
    return result


def roundtrip_string(tmp_path, data):
    """Write string list, read back, verify identity."""
    path = str(tmp_path / "prop_string.parquet")
    schema = sp.SchemaBuilder("prop").string("v").build()
    writer = sp.ParquetWriter.open(path, schema)
    writer.write_column_string(0, data)
    writer.flush_row_group()
    writer.close()

    reader = sp.ParquetReader.open(path)
    result = reader.read_column_string(0, 0)
    return result


# ===========================================================================
# Property-based tests
# ===========================================================================

@given(data=arrays(dtype=np.int64, shape=st.integers(min_value=1, max_value=5000)))
@settings(max_examples=50, deadline=10000)
def test_int64_roundtrip_identity(data, tmp_path):
    """Property: decode(encode(x)) == x for all int64 arrays."""
    result = roundtrip_int64(tmp_path, data)
    np.testing.assert_array_equal(result, data)


@given(data=arrays(
    dtype=np.float64,
    shape=st.integers(min_value=1, max_value=5000),
    elements=st.floats(
        min_value=-1e15, max_value=1e15,
        allow_nan=False, allow_infinity=False
    )
))
@settings(max_examples=50, deadline=10000)
def test_double_roundtrip_identity(data, tmp_path):
    """Property: decode(encode(x)) == x for all finite float64 arrays."""
    result = roundtrip_double(tmp_path, data)
    np.testing.assert_array_almost_equal(result, data)


@given(data=st.lists(
    st.text(
        alphabet=st.characters(
            whitelist_categories=('L', 'N', 'P', 'S', 'Z'),
            blacklist_characters='\x00'
        ),
        min_size=0, max_size=200
    ),
    min_size=1, max_size=2000
))
@settings(max_examples=50, deadline=10000)
def test_string_roundtrip_identity(data, tmp_path):
    """Property: decode(encode(x)) == x for all string arrays."""
    result = roundtrip_string(tmp_path, data)
    assert result == data


@given(
    n=st.integers(min_value=1, max_value=3000),
    seed=st.integers(min_value=0, max_value=2**32 - 1)
)
@settings(max_examples=30, deadline=10000)
def test_int64_length_preserved(n, seed, tmp_path):
    """Property: len(decode(encode(x))) == len(x) for all lengths."""
    rng = np.random.RandomState(seed)
    data = rng.randint(-2**62, 2**62, size=n, dtype=np.int64)
    result = roundtrip_int64(tmp_path, data)
    assert len(result) == n


@given(data=arrays(
    dtype=np.int64,
    shape=st.integers(min_value=1, max_value=1000),
    elements=st.just(np.int64(0))
))
@settings(max_examples=20, deadline=10000)
def test_int64_all_zeros(data, tmp_path):
    """Property: all-zero arrays roundtrip correctly (RLE edge case)."""
    result = roundtrip_int64(tmp_path, data)
    np.testing.assert_array_equal(result, data)


@given(data=arrays(
    dtype=np.int64,
    shape=st.integers(min_value=1, max_value=1000),
    elements=st.just(np.int64(42))
))
@settings(max_examples=20, deadline=10000)
def test_int64_constant_value(data, tmp_path):
    """Property: constant-value arrays roundtrip correctly (dictionary edge case)."""
    result = roundtrip_int64(tmp_path, data)
    np.testing.assert_array_equal(result, data)


@given(n=st.integers(min_value=1, max_value=500))
@settings(max_examples=20, deadline=10000)
def test_string_empty_strings(n, tmp_path):
    """Property: arrays of empty strings roundtrip correctly."""
    data = [""] * n
    result = roundtrip_string(tmp_path, data)
    assert result == data


@given(data=st.lists(
    st.text(min_size=1, max_size=50),
    min_size=2, max_size=500
))
@settings(max_examples=30, deadline=10000)
def test_multicolumn_roundtrip(data, tmp_path):
    """Property: multi-column write/read preserves all data."""
    path = str(tmp_path / "prop_multi.parquet")
    n = len(data)
    ts = np.arange(n, dtype=np.int64)
    prices = np.linspace(100.0, 200.0, n)

    schema = (sp.SchemaBuilder("prop")
              .int64("ts")
              .double_("price")
              .string("sym")
              .build())
    writer = sp.ParquetWriter.open(path, schema)
    writer.write_column_int64(0, ts)
    writer.write_column_double(1, prices)
    writer.write_column_string(2, data)
    writer.flush_row_group()
    writer.close()

    reader = sp.ParquetReader.open(path)
    assert reader.num_rows == n
    result_ts = reader.read_column_int64(0, 0)
    result_px = reader.read_column_double(0, 1)
    result_sym = reader.read_column_string(0, 2)
    np.testing.assert_array_equal(result_ts, ts)
    np.testing.assert_array_almost_equal(result_px, prices)
    assert result_sym == data
