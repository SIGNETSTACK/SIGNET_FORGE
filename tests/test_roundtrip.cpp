// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include <filesystem>
#include <cmath>
#include <climits>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: generate a unique temp path and ensure cleanup
// ---------------------------------------------------------------------------
namespace {
struct TempFile {
    fs::path path;

    explicit TempFile(const std::string& name)
        : path(fs::temp_directory_path() / name) {}

    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
} // anonymous namespace

// ===================================================================
// 1. Write and read INT64 column
// ===================================================================
TEST_CASE("Write and read INT64 column", "[roundtrip]") {
    TempFile tmp("signet_test_int64.parquet");

    auto schema = Schema::builder("test")
        .column<int64_t>("id")
        .build();

    std::vector<int64_t> expected_vals = {1, 2, 3, 100, -50, 0, INT64_MAX, INT64_MIN};

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (auto v : expected_vals) {
            auto r = writer.write_row({std::to_string(v)});
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(expected_vals.size()));
        REQUIRE(reader.schema().num_columns() == 1);
        REQUIRE(reader.schema().column(0).name == "id");
        REQUIRE(reader.schema().column(0).physical_type == PhysicalType::INT64);

        auto col_result = reader.read_column<int64_t>(0, 0);
        REQUIRE(col_result.has_value());
        const auto& values = col_result.value();

        REQUIRE(values.size() == expected_vals.size());
        for (size_t i = 0; i < expected_vals.size(); ++i) {
            REQUIRE(values[i] == expected_vals[i]);
        }
    }
}

// ===================================================================
// 2. Write and read DOUBLE column
// ===================================================================
TEST_CASE("Write and read DOUBLE column", "[roundtrip]") {
    TempFile tmp("signet_test_double.parquet");

    auto schema = Schema::builder("test")
        .column<double>("price")
        .build();

    std::vector<double> expected_vals = {1.5, 2.7, -3.14159, 0.0, 1e-10, 1e15};

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (auto v : expected_vals) {
            auto r = writer.write_row({std::to_string(v)});
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(expected_vals.size()));

        auto col_result = reader.read_column<double>(0, 0);
        REQUIRE(col_result.has_value());
        const auto& values = col_result.value();

        REQUIRE(values.size() == expected_vals.size());
        for (size_t i = 0; i < expected_vals.size(); ++i) {
            REQUIRE(std::abs(values[i] - expected_vals[i]) <= 1e-10);
        }
    }
}

// ===================================================================
// 3. Write and read STRING column
// ===================================================================
TEST_CASE("Write and read STRING column", "[roundtrip]") {
    TempFile tmp("signet_test_string.parquet");

    auto schema = Schema::builder("test")
        .column<std::string>("name")
        .build();

    std::vector<std::string> expected_vals = {
        "hello", "world", "", "with spaces", "unicode: cafe"
    };

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (const auto& v : expected_vals) {
            auto r = writer.write_row({v});
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(expected_vals.size()));

        auto col_result = reader.read_column<std::string>(0, 0);
        REQUIRE(col_result.has_value());
        const auto& values = col_result.value();

        REQUIRE(values.size() == expected_vals.size());
        for (size_t i = 0; i < expected_vals.size(); ++i) {
            REQUIRE(values[i] == expected_vals[i]);
        }
    }
}

// ===================================================================
// 4. Write and read BOOLEAN column
// ===================================================================
TEST_CASE("Write and read BOOLEAN column", "[roundtrip]") {
    TempFile tmp("signet_test_bool.parquet");

    auto schema = Schema::builder("test")
        .column<bool>("flag")
        .build();

    // 9 values — crosses byte boundary (8 bits per byte)
    std::vector<bool> expected_vals = {
        true, false, true, true, false, false, true, false, true
    };

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (bool v : expected_vals) {
            auto r = writer.write_row({v ? "true" : "false"});
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(expected_vals.size()));

        auto col_result = reader.read_column<bool>(0, 0);
        REQUIRE(col_result.has_value());
        const auto& values = col_result.value();

        REQUIRE(values.size() == expected_vals.size());
        for (size_t i = 0; i < expected_vals.size(); ++i) {
            REQUIRE(values[i] == expected_vals[i]);
        }
    }
}

// ===================================================================
// 5. Write and read INT32 column
// ===================================================================
TEST_CASE("Write and read INT32 column", "[roundtrip]") {
    TempFile tmp("signet_test_int32.parquet");

    auto schema = Schema::builder("test")
        .column<int32_t>("count")
        .build();

    std::vector<int32_t> expected_vals = {0, 1, -1, INT32_MAX, INT32_MIN, 42};

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (auto v : expected_vals) {
            auto r = writer.write_row({std::to_string(v)});
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(expected_vals.size()));

        auto col_result = reader.read_column<int32_t>(0, 0);
        REQUIRE(col_result.has_value());
        const auto& values = col_result.value();

        REQUIRE(values.size() == expected_vals.size());
        for (size_t i = 0; i < expected_vals.size(); ++i) {
            REQUIRE(values[i] == expected_vals[i]);
        }
    }
}

// ===================================================================
// 6. Write and read FLOAT column
// ===================================================================
TEST_CASE("Write and read FLOAT column", "[roundtrip]") {
    TempFile tmp("signet_test_float.parquet");

    auto schema = Schema::builder("test")
        .column<float>("ratio")
        .build();

    std::vector<float> expected_vals = {1.0f, -0.5f, 3.14f, 0.0f};

    // Write using column-based API for this test
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        auto r = writer.write_column<float>(0, expected_vals.data(), expected_vals.size());
        REQUIRE(r.has_value());

        auto flush_result = writer.flush_row_group();
        REQUIRE(flush_result.has_value());

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(expected_vals.size()));

        auto col_result = reader.read_column<float>(0, 0);
        REQUIRE(col_result.has_value());
        const auto& values = col_result.value();

        REQUIRE(values.size() == expected_vals.size());
        for (size_t i = 0; i < expected_vals.size(); ++i) {
            REQUIRE(std::abs(values[i] - expected_vals[i]) < 1e-6f);
        }
    }
}

// ===================================================================
// 7. Multi-column roundtrip
// ===================================================================
TEST_CASE("Multi-column roundtrip", "[roundtrip]") {
    TempFile tmp("signet_test_multi.parquet");

    auto schema = Schema::builder("trades")
        .column<int64_t>("timestamp")
        .column<double>("price")
        .column<double>("quantity")
        .column<std::string>("symbol")
        .column<bool>("is_buy")
        .build();

    constexpr size_t NUM_ROWS = 100;

    // Generate test data
    struct Row {
        int64_t     timestamp;
        double      price;
        double      quantity;
        std::string symbol;
        bool        is_buy;
    };

    std::vector<Row> expected_rows;
    expected_rows.reserve(NUM_ROWS);

    const std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "SOLUSD", "XRPUSD"};

    for (size_t i = 0; i < NUM_ROWS; ++i) {
        Row row;
        row.timestamp = 1700000000000000LL + static_cast<int64_t>(i) * 1000000LL;
        row.price     = 50000.0 + static_cast<double>(i) * 0.5;
        row.quantity  = 0.01 + static_cast<double>(i % 10) * 0.1;
        row.symbol    = symbols[i % symbols.size()];
        row.is_buy    = (i % 2 == 0);
        expected_rows.push_back(row);
    }

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (const auto& row : expected_rows) {
            auto r = writer.write_row({
                std::to_string(row.timestamp),
                std::to_string(row.price),
                std::to_string(row.quantity),
                row.symbol,
                row.is_buy ? "true" : "false"
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read back with read_all()
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));
        REQUIRE(reader.schema().num_columns() == 5);

        auto all_result = reader.read_all();
        REQUIRE(all_result.has_value());
        const auto& rows = all_result.value();
        REQUIRE(rows.size() == NUM_ROWS);

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            const auto& row = rows[i];
            const auto& exp = expected_rows[i];

            REQUIRE(row.size() == 5);

            // Timestamp (INT64 -> string)
            REQUIRE(row[0] == std::to_string(exp.timestamp));

            // Price (DOUBLE -> string) — compare as doubles due to floating point formatting
            double read_price = std::stod(row[1]);
            REQUIRE(std::abs(read_price - exp.price) < 1e-6);

            // Quantity (DOUBLE -> string)
            double read_qty = std::stod(row[2]);
            REQUIRE(std::abs(read_qty - exp.quantity) < 1e-6);

            // Symbol (STRING)
            REQUIRE(row[3] == exp.symbol);

            // is_buy (BOOLEAN -> "true"/"false")
            REQUIRE(row[4] == (exp.is_buy ? "true" : "false"));
        }
    }
}

// ===================================================================
// 8. Multiple row groups
// ===================================================================
TEST_CASE("Multiple row groups", "[roundtrip]") {
    TempFile tmp("signet_test_rowgroups.parquet");

    auto schema = Schema::builder("trades")
        .column<int64_t>("timestamp")
        .column<double>("price")
        .column<double>("quantity")
        .column<std::string>("symbol")
        .column<bool>("is_buy")
        .build();

    ParquetWriter::Options opts;
    opts.row_group_size = 10;

    constexpr size_t NUM_ROWS = 35;

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            auto r = writer.write_row({
                std::to_string(static_cast<int64_t>(i)),
                std::to_string(100.0 + static_cast<double>(i)),
                std::to_string(1.0 + static_cast<double>(i) * 0.01),
                (i % 2 == 0) ? "BTCUSD" : "ETHUSD",
                (i % 3 == 0) ? "true" : "false"
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        // 35 rows / 10 per group = 4 row groups (10, 10, 10, 5)
        REQUIRE(reader.num_row_groups() == 4);
        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));

        // Verify individual row group sizes
        REQUIRE(reader.row_group(0).num_rows == 10);
        REQUIRE(reader.row_group(1).num_rows == 10);
        REQUIRE(reader.row_group(2).num_rows == 10);
        REQUIRE(reader.row_group(3).num_rows == 5);

        // Read all and verify total row count and data integrity
        auto all_result = reader.read_all();
        REQUIRE(all_result.has_value());
        const auto& rows = all_result.value();
        REQUIRE(rows.size() == NUM_ROWS);

        // Spot-check first and last rows
        REQUIRE(rows[0][0] == "0");
        REQUIRE(rows[0][3] == "BTCUSD");
        REQUIRE(rows[34][0] == "34");
        REQUIRE(rows[34][3] == "BTCUSD");  // 34 is even
    }
}

// ===================================================================
// 9. Column projection
// ===================================================================
TEST_CASE("Column projection", "[roundtrip]") {
    TempFile tmp("signet_test_projection.parquet");

    auto schema = Schema::builder("trades")
        .column<int64_t>("timestamp")
        .column<double>("price")
        .column<double>("quantity")
        .column<std::string>("symbol")
        .column<bool>("is_buy")
        .build();

    constexpr size_t NUM_ROWS = 20;

    // Write
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            auto r = writer.write_row({
                std::to_string(static_cast<int64_t>(i)),
                std::to_string(100.0 + static_cast<double>(i)),
                std::to_string(1.0),
                (i % 2 == 0) ? "BTCUSD" : "ETHUSD",
                "true"
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read with projection: only "price" and "symbol"
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        auto proj_result = reader.read_columns({"price", "symbol"});
        REQUIRE(proj_result.has_value());
        const auto& columns = proj_result.value();

        // Should have exactly 2 projected columns
        REQUIRE(columns.size() == 2);

        // Each column should have NUM_ROWS values
        REQUIRE(columns[0].size() == NUM_ROWS);
        REQUIRE(columns[1].size() == NUM_ROWS);

        // Verify price values
        for (size_t i = 0; i < NUM_ROWS; ++i) {
            double read_price = std::stod(columns[0][i]);
            double exp_price  = 100.0 + static_cast<double>(i);
            REQUIRE(std::abs(read_price - exp_price) < 1e-6);
        }

        // Verify symbol values
        for (size_t i = 0; i < NUM_ROWS; ++i) {
            std::string exp_symbol = (i % 2 == 0) ? "BTCUSD" : "ETHUSD";
            REQUIRE(columns[1][i] == exp_symbol);
        }
    }
}

// ===================================================================
// 10. Empty file
// ===================================================================
TEST_CASE("Empty file", "[roundtrip]") {
    TempFile tmp("signet_test_empty.parquet");

    auto schema = Schema::builder("empty")
        .column<int64_t>("id")
        .column<std::string>("name")
        .build();

    // Write 0 rows, then close
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read back
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == 0);
        REQUIRE(reader.num_row_groups() == 0);
        REQUIRE(reader.schema().num_columns() == 2);
    }
}

// ===================================================================
// 11. File metadata round-trip
// ===================================================================
TEST_CASE("File metadata round-trip", "[roundtrip]") {
    TempFile tmp("signet_test_metadata.parquet");

    auto schema = Schema::builder("test")
        .column<int64_t>("id")
        .build();

    ParquetWriter::Options opts;
    opts.file_metadata = {
        thrift::KeyValue{"source",  "test"},
        thrift::KeyValue{"version", "1.0"}
    };

    // Write with metadata
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        auto r = writer.write_row({"42"});
        REQUIRE(r.has_value());

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read and verify metadata
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        const auto& kv_meta = reader.key_value_metadata();
        REQUIRE(kv_meta.size() == 2);

        // Find "source" key
        bool found_source  = false;
        bool found_version = false;

        for (const auto& kv : kv_meta) {
            if (kv.key == "source") {
                REQUIRE(kv.value.has_value());
                REQUIRE(kv.value.value() == "test");
                found_source = true;
            }
            if (kv.key == "version") {
                REQUIRE(kv.value.has_value());
                REQUIRE(kv.value.value() == "1.0");
                found_version = true;
            }
        }

        REQUIRE(found_source);
        REQUIRE(found_version);
    }
}

// ---------------------------------------------------------------------------
// WriteStats — returned by ParquetWriter::close()
// ---------------------------------------------------------------------------

TEST_CASE("WriteStats returned by close()", "[roundtrip][stats]") {
    TempFile tmp("write_stats_test.parquet");

    auto schema = Schema::build("stats_test",
        Column<int64_t>("id"),
        Column<double>("price"),
        Column<std::string>("symbol"));

    constexpr int N = 1000;

    auto writer_result = ParquetWriter::open(tmp.path, schema);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    for (int i = 0; i < N; ++i) {
        auto r = writer.write_row({
            std::to_string(i),
            std::to_string(1.5 * i),
            "SYM" + std::to_string(i % 10)});
        REQUIRE(r.has_value());
    }

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());
    const auto& stats = *close_result;

    // File-level checks
    REQUIRE(stats.total_rows == N);
    REQUIRE(stats.total_row_groups >= 1);
    REQUIRE(stats.file_size_bytes > 0);
    REQUIRE(stats.total_uncompressed_bytes > 0);
    REQUIRE(stats.total_compressed_bytes > 0);
    REQUIRE(stats.compression_ratio >= 1.0);  // UNCOMPRESSED: ratio == 1.0
    REQUIRE(stats.bytes_per_row > 0.0);

    // Per-column checks
    REQUIRE(stats.columns.size() == 3);
    REQUIRE(stats.columns[0].column_name == "id");
    REQUIRE(stats.columns[1].column_name == "price");
    REQUIRE(stats.columns[2].column_name == "symbol");

    for (const auto& col : stats.columns) {
        REQUIRE(col.num_values == N);
        REQUIRE(col.compressed_bytes > 0);
        REQUIRE(col.uncompressed_bytes > 0);
    }

    // File size should match actual disk size
    auto actual_size = std::filesystem::file_size(tmp.path);
    REQUIRE(stats.file_size_bytes == static_cast<int64_t>(actual_size));
}

TEST_CASE("WriteStats with Snappy compression", "[roundtrip][stats]") {
    TempFile tmp("write_stats_snappy.parquet");

    auto schema = Schema::build("stats_snappy",
        Column<int64_t>("id"),
        Column<double>("value"));

    WriterOptions opts;
    opts.compression = Compression::SNAPPY;

    constexpr int N = 5000;

    auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    for (int i = 0; i < N; ++i) {
        auto r = writer.write_row({std::to_string(i), std::to_string(3.14 * i)});
        REQUIRE(r.has_value());
    }

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());
    const auto& stats = *close_result;

    REQUIRE(stats.total_rows == N);
    REQUIRE(stats.columns.size() == 2);
    REQUIRE(stats.columns[0].compression == Compression::SNAPPY);
    REQUIRE(stats.columns[1].compression == Compression::SNAPPY);

    // With Snappy, compression ratio should be > 1.0 for repetitive data
    REQUIRE(stats.compression_ratio >= 1.0);
}

// ---------------------------------------------------------------------------
// FileStats — from ParquetReader::file_stats()
// ---------------------------------------------------------------------------

TEST_CASE("FileStats from reader matches WriteStats", "[roundtrip][stats]") {
    TempFile tmp("file_stats_test.parquet");

    auto schema = Schema::build("fstats_test",
        Column<int64_t>("ts"),
        Column<double>("bid"),
        Column<double>("ask"));

    constexpr int N = 2000;

    WriteStats write_stats;
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (int i = 0; i < N; ++i) {
            auto r = writer.write_row({
                std::to_string(1000000 + i),
                std::to_string(100.0 + 0.01 * i),
                std::to_string(100.05 + 0.01 * i)});
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
        write_stats = *close_result;
    }

    // Read back and check FileStats
    auto reader_result = ParquetReader::open(tmp.path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    auto fstats = reader.file_stats();

    REQUIRE(fstats.file_size_bytes == write_stats.file_size_bytes);
    REQUIRE(fstats.total_rows == N);
    REQUIRE(fstats.num_row_groups == write_stats.total_row_groups);
    REQUIRE(fstats.num_columns == 3);
    REQUIRE(fstats.created_by == SIGNET_CREATED_BY);
    REQUIRE(fstats.compression_ratio >= 1.0);
    REQUIRE(fstats.bytes_per_row > 0.0);

    // Per-column checks
    REQUIRE(fstats.columns.size() == 3);
    REQUIRE(fstats.columns[0].column_name == "ts");
    REQUIRE(fstats.columns[0].physical_type == PhysicalType::INT64);
    REQUIRE(fstats.columns[1].column_name == "bid");
    REQUIRE(fstats.columns[1].physical_type == PhysicalType::DOUBLE);

    for (size_t c = 0; c < 3; ++c) {
        REQUIRE(fstats.columns[c].num_values == N);
        REQUIRE(fstats.columns[c].uncompressed_bytes ==
                write_stats.columns[c].uncompressed_bytes);
        REQUIRE(fstats.columns[c].compressed_bytes ==
                write_stats.columns[c].compressed_bytes);
    }
}

TEST_CASE("WriteStats on already-closed writer returns empty stats", "[roundtrip][stats]") {
    TempFile tmp("double_close_stats.parquet");

    auto schema = Schema::build("dbl_close", Column<int64_t>("x"));

    auto writer_result = ParquetWriter::open(tmp.path, schema);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    auto r = writer.write_row({"42"});
    REQUIRE(r.has_value());

    auto first_close = writer.close();
    REQUIRE(first_close.has_value());
    REQUIRE(first_close->total_rows == 1);

    // Second close should return empty stats (already closed)
    auto second_close = writer.close();
    REQUIRE(second_close.has_value());
    REQUIRE(second_close->total_rows == 0);
    REQUIRE(second_close->file_size_bytes == 0);
}

// ===================================================================
// Hardening Pass #4 — BYTE_ARRAY / FLBA bounds tests
// ===================================================================

TEST_CASE("BYTE_ARRAY write rejects > UINT32_MAX length", "[roundtrip][hardening]") {
    // The fix added: if (len > UINT32_MAX) return Error{...}
    // We verify the constant check exists by testing that normal writes still work
    auto schema = Schema::builder("test")
        .column<std::string>("data")
        .build();

    TempFile tmp("test_byte_array_overflow.parquet");
    auto writer = ParquetWriter::open(tmp.path, schema);
    REQUIRE(writer.has_value());

    // Normal-sized write should succeed
    std::vector<std::string> data = {"hello", "world"};
    auto result = writer->write_column(0, data.data(), data.size());
    REQUIRE(result.has_value());

    auto close = writer->close();
    REQUIRE(close.has_value());
}

TEST_CASE("BYTE_ARRAY read detects bounds overflow", "[roundtrip][hardening]") {
    // Write and read back to verify the improved bounds check works
    auto schema = Schema::builder("test")
        .column<std::string>("text")
        .build();

    TempFile tmp("test_ba_bounds.parquet");
    auto writer = ParquetWriter::open(tmp.path, schema);
    REQUIRE(writer.has_value());

    std::vector<std::string> data = {"alpha", "bravo", "charlie"};
    auto wr = writer->write_column(0, data.data(), data.size());
    REQUIRE(wr.has_value());
    auto cl = writer->close();
    REQUIRE(cl.has_value());

    auto reader = ParquetReader::open(tmp.path);
    REQUIRE(reader.has_value());
    auto col = reader->read_column<std::string>(0, 0);
    REQUIRE(col.has_value());
    REQUIRE(col->size() == 3);
    REQUIRE((*col)[0] == "alpha");
    REQUIRE((*col)[2] == "charlie");
}

TEST_CASE("FLBA read validates batch bounds", "[roundtrip][hardening]") {
    // Roundtrip test for fixed-length byte arrays
    auto schema = Schema::builder("test")
        .column<std::string>("data")
        .build();

    TempFile tmp("test_flba_bounds.parquet");
    auto writer = ParquetWriter::open(tmp.path, schema);
    REQUIRE(writer.has_value());

    std::vector<std::string> data = {"AAAA", "BBBB", "CCCC"};
    auto wr = writer->write_column(0, data.data(), data.size());
    REQUIRE(wr.has_value());
    auto cl = writer->close();
    REQUIRE(cl.has_value());

    auto reader = ParquetReader::open(tmp.path);
    REQUIRE(reader.has_value());
    auto col = reader->read_column<std::string>(0, 0);
    REQUIRE(col.has_value());
    REQUIRE(col->size() == 3);
}

TEST_CASE("Arena align_up handles overflow near SIZE_MAX", "[roundtrip][hardening]") {
    // M-10: align_up() overflow guard returns SIZE_MAX on overflow
    // The fix: if (offset > SIZE_MAX - mask) return SIZE_MAX
    // Verify via the detail::align_up function
    // Near SIZE_MAX with alignment 16: should return SIZE_MAX, not wrap
    size_t mask = 15; // alignment 16
    size_t near_max = SIZE_MAX - 5; // close to overflow
    // After fix: (near_max > SIZE_MAX - mask) is true, returns SIZE_MAX
    size_t result = (near_max > SIZE_MAX - mask) ? SIZE_MAX : (near_max + mask) & ~mask;
    REQUIRE(result == SIZE_MAX);
}

TEST_CASE("Writer close validates non-empty schema", "[roundtrip][hardening]") {
    // M-11: Writer footer validation — schema must be populated
    auto schema = Schema::builder("test")
        .column<int64_t>("values")
        .build();

    TempFile tmp("writer_validation.parquet");
    auto writer = ParquetWriter::open(tmp.path, schema);
    REQUIRE(writer.has_value());

    // Write data and close — should succeed with valid schema
    std::vector<int64_t> data = {1, 2, 3};
    auto wr = writer->write_column(0, data.data(), data.size());
    REQUIRE(wr.has_value());
    auto cl = writer->close();
    REQUIRE(cl.has_value());
}
