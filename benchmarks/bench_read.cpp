// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// benchmarks/bench_read.cpp
// Read-throughput benchmarks for signet::forge
// Build: link against signet_forge + Catch2::Catch2WithMain
//
// Each TEST_CASE writes a fixture file once (outside the BENCHMARK lambda),
// benchmarks the read path, then removes the file.

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "signet/forge.hpp"

#include <cstdint>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper — write a 50 K row file with schema: ts(int64), price(double), qty(double)
// Returns the path that was written.
// ---------------------------------------------------------------------------
static fs::path write_fixture_50k(const std::string& stem) {
    constexpr int64_t ROWS = 50'000;

    std::vector<int64_t> ts(ROWS);
    std::vector<double>  price(ROWS);
    std::vector<double>  qty(ROWS);

    std::iota(ts.begin(), ts.end(), int64_t{1'700'000'000'000LL});
    for (int64_t i = 0; i < ROWS; ++i) {
        auto idx   = static_cast<std::size_t>(i);
        price[idx] = 30'000.0 + static_cast<double>(i) * 0.01;
        qty[idx]   = 0.001 * static_cast<double>((i % 200) + 1);
    }

    auto schema = Schema::build("bench_read_fixture",
        Column<int64_t>("ts"),
        Column<double>("price"),
        Column<double>("qty"));

    auto path = fs::temp_directory_path() / (stem + ".parquet");

    WriterOptions opts{};
    opts.compression = Compression::UNCOMPRESSED;

    auto writer = ParquetWriter::open(path.string(), schema, opts);
    (*writer).template write_column<int64_t>(0, ts.data(),    static_cast<int64_t>(ts.size()));
    (*writer).template write_column<double> (1, price.data(), static_cast<int64_t>(price.size()));
    (*writer).template write_column<double> (2, qty.data(),   static_cast<int64_t>(qty.size()));
    (*writer).flush_row_group();
    (*writer).close();

    return path;
}

// ---------------------------------------------------------------------------
// Helper — write a 50 K row file that also includes string columns
// Schema: ts(int64), price(double), qty(double), symbol(string), side(string)
// ---------------------------------------------------------------------------
static fs::path write_fixture_50k_strings(const std::string& stem) {
    constexpr int64_t ROWS = 50'000;

    const std::vector<std::string> sym_pool{ "BTCUSDT", "ETHUSDT", "SOLUSDT", "BNBUSDT" };
    const std::vector<std::string> side_pool{ "BUY", "SELL" };

    std::vector<int64_t>    ts(ROWS);
    std::vector<double>     price(ROWS);
    std::vector<double>     qty(ROWS);
    std::vector<std::string> symbol(ROWS);
    std::vector<std::string> side(ROWS);

    std::iota(ts.begin(), ts.end(), int64_t{1'700'000'000'000LL});
    for (int64_t i = 0; i < ROWS; ++i) {
        auto idx    = static_cast<std::size_t>(i);
        price[idx]  = 30'000.0 + static_cast<double>(i) * 0.01;
        qty[idx]    = 0.001 * static_cast<double>((i % 200) + 1);
        symbol[idx] = sym_pool[idx % sym_pool.size()];
        side[idx]   = side_pool[idx % side_pool.size()];
    }

    auto schema = Schema::build("bench_read_fixture_str",
        Column<int64_t>("ts"),
        Column<double>("price"),
        Column<double>("qty"),
        Column<std::string>("symbol"),
        Column<std::string>("side"));

    auto path = fs::temp_directory_path() / (stem + ".parquet");

    WriterOptions opts{};
    opts.compression = Compression::UNCOMPRESSED;

    auto writer = ParquetWriter::open(path.string(), schema, opts);
    (*writer).template write_column<int64_t>    (0, ts.data(),     static_cast<int64_t>(ts.size()));
    (*writer).template write_column<double>     (1, price.data(),  static_cast<int64_t>(price.size()));
    (*writer).template write_column<double>     (2, qty.data(),    static_cast<int64_t>(qty.size()));
    (*writer).template write_column<std::string>(3, symbol.data(), static_cast<int64_t>(symbol.size()));
    (*writer).template write_column<std::string>(4, side.data(),   static_cast<int64_t>(side.size()));
    (*writer).flush_row_group();
    (*writer).close();

    return path;
}

// ---------------------------------------------------------------------------
// TEST CASE 1 — typed column read<double>, 50 K rows
// ---------------------------------------------------------------------------
TEST_CASE("Read throughput — typed column read<double> (50K rows)", "[bench][read]") {
    auto path = write_fixture_50k("signet_bench_r_double_50k");

    BENCHMARK("read_column<double> price") {
        auto r   = ParquetReader::open(path.string());
        auto col = (*r).template read_column<double>(0, 1);  // row_group=0, col_idx=1
        return col->size();
    };

    fs::remove(path);
}

// ---------------------------------------------------------------------------
// TEST CASE 2 — read_all (string conversion), 50 K rows
// ---------------------------------------------------------------------------
TEST_CASE("Read throughput — read_all string conversion (50K rows)", "[bench][read]") {
    auto path = write_fixture_50k("signet_bench_r_all_50k");

    BENCHMARK("read_all") {
        auto r      = ParquetReader::open(path.string());
        auto result = (*r).read_all();
        // result is vector<vector<string>>; return total cell count to prevent elision
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };

    fs::remove(path);
}

// ---------------------------------------------------------------------------
// TEST CASE 3 — column projection by name, 50 K rows
// Reads only "price" and "qty" by name (skips "ts")
// ---------------------------------------------------------------------------
TEST_CASE("Read throughput — column projection by name (50K rows)", "[bench][read]") {
    auto path = write_fixture_50k("signet_bench_r_proj_50k");

    BENCHMARK("read_columns price+qty") {
        auto r      = ParquetReader::open(path.string());
        auto result = (*r).read_columns({"price", "qty"});
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };

    fs::remove(path);
}

// ---------------------------------------------------------------------------
// TEST CASE 4 — typed int64 column read, 50 K rows
// ---------------------------------------------------------------------------
TEST_CASE("Read throughput — typed int64 column read (50K rows)", "[bench][read]") {
    auto path = write_fixture_50k("signet_bench_r_int64_50k");

    BENCHMARK("read_column<int64_t> ts") {
        auto r   = ParquetReader::open(path.string());
        auto col = (*r).template read_column<int64_t>(0, 0);  // row_group=0, col_idx=0
        return col->size();
    };

    fs::remove(path);
}

// ---------------------------------------------------------------------------
// TEST CASE 5 — open + read footer only (latency benchmark)
// Just opens the reader (which parses the Parquet footer/metadata) and
// queries num_rows() — no column data is read.
// ---------------------------------------------------------------------------
TEST_CASE("Read latency — open + read footer only", "[bench][read]") {
    // Use the string-column fixture so the schema is non-trivial
    auto path = write_fixture_50k_strings("signet_bench_r_footer_50k");

    BENCHMARK("open + num_rows") {
        auto r    = ParquetReader::open(path.string());
        auto rows = (*r).num_rows();
        return rows;
    };

    fs::remove(path);
}
