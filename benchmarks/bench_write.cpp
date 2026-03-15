// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// benchmarks/bench_write.cpp
// Write-throughput benchmarks for signet::forge
// Build: link against signet_forge + Catch2::Catch2WithMain

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include "signet/forge.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// TEST CASE 1 — int64 column, 10 K rows
// ---------------------------------------------------------------------------
TEST_CASE("Write throughput — int64 column 10K rows", "[bench][write]") {
    constexpr int64_t N = 10'000;

    std::vector<int64_t> ts(N);
    std::iota(ts.begin(), ts.end(), int64_t{0});

    auto schema = Schema::build("bench_int64",
        Column<int64_t>("ts"));

    BENCHMARK("write") {
        auto path = fs::temp_directory_path() /
                    ("signet_bench_w_" + std::to_string(N) + "_int64.parquet");

        WriterOptions opts{};
        opts.compression = Compression::UNCOMPRESSED;

        auto writer = ParquetWriter::open(path.string(), schema, opts);
        (*writer).template write_column<int64_t>(0, ts.data(), static_cast<int64_t>(ts.size()));
        (*writer).flush_row_group();
        (*writer).close();

        fs::remove(path);
        return N;
    };
}

// ---------------------------------------------------------------------------
// TEST CASE 2 — double column, 10 K rows
// ---------------------------------------------------------------------------
TEST_CASE("Write throughput — double column 10K rows", "[bench][write]") {
    constexpr int64_t N = 10'000;

    std::vector<double> price(N);
    for (int64_t i = 0; i < N; ++i)
        price[static_cast<std::size_t>(i)] = 100.0 + static_cast<double>(i) * 0.01;

    auto schema = Schema::build("bench_double",
        Column<double>("price"));

    BENCHMARK("write") {
        auto path = fs::temp_directory_path() /
                    ("signet_bench_w_" + std::to_string(N) + "_double.parquet");

        WriterOptions opts{};
        opts.compression     = Compression::UNCOMPRESSED;
        opts.default_encoding = Encoding::BYTE_STREAM_SPLIT;

        auto writer = ParquetWriter::open(path.string(), schema, opts);
        (*writer).template write_column<double>(0, price.data(), static_cast<int64_t>(price.size()));
        (*writer).flush_row_group();
        (*writer).close();

        fs::remove(path);
        return N;
    };
}

// ---------------------------------------------------------------------------
// TEST CASE 3 — mixed schema, 5 columns, 10 K rows
// ts(int64), price(double), qty(double), symbol(string), side(string)
// ---------------------------------------------------------------------------
TEST_CASE("Write throughput — mixed schema 5 columns 10K rows", "[bench][write]") {
    constexpr int64_t N = 10'000;

    std::vector<int64_t>    ts(N);
    std::vector<double>     price(N);
    std::vector<double>     qty(N);
    std::vector<std::string> symbol(N);
    std::vector<std::string> side(N);

    std::iota(ts.begin(), ts.end(), int64_t{1'700'000'000'000LL});
    for (int64_t i = 0; i < N; ++i) {
        auto idx = static_cast<std::size_t>(i);
        price[idx]  = 30'000.0 + static_cast<double>(i) * 0.1;
        qty[idx]    = 0.001 * static_cast<double>((i % 100) + 1);
        symbol[idx] = "BTCUSDT";
        side[idx]   = (i % 2 == 0) ? "BUY" : "SELL";
    }

    auto schema = Schema::build("bench_mixed",
        Column<int64_t>("ts"),
        Column<double>("price"),
        Column<double>("qty"),
        Column<std::string>("symbol"),
        Column<std::string>("side"));

    BENCHMARK("write") {
        auto path = fs::temp_directory_path() /
                    ("signet_bench_w_" + std::to_string(N) + "_mixed.parquet");

        WriterOptions opts{};
        opts.compression = Compression::UNCOMPRESSED;

        auto writer = ParquetWriter::open(path.string(), schema, opts);
        (*writer).template write_column<int64_t>   (0, ts.data(),               static_cast<int64_t>(ts.size()));
        (*writer).template write_column<double>    (1, price.data(),             static_cast<int64_t>(price.size()));
        (*writer).template write_column<double>    (2, qty.data(),               static_cast<int64_t>(qty.size()));
        (*writer).template write_column<std::string>(3, symbol.data(),           static_cast<int64_t>(symbol.size()));
        (*writer).template write_column<std::string>(4, side.data(),             static_cast<int64_t>(side.size()));
        (*writer).flush_row_group();
        (*writer).close();

        fs::remove(path);
        return N;
    };
}

// ---------------------------------------------------------------------------
// TEST CASE 4 — 100 K rows, 10 row groups (row_group_size = 10 K)
// ---------------------------------------------------------------------------
TEST_CASE("Write throughput — 100K rows 10 row groups", "[bench][write]") {
    constexpr int64_t TOTAL     = 100'000;
    constexpr int64_t RG_SIZE   = 10'000;
    constexpr int64_t NUM_RGS   = TOTAL / RG_SIZE;

    std::vector<int64_t> ts(TOTAL);
    std::vector<double>  price(TOTAL);
    std::iota(ts.begin(), ts.end(), int64_t{0});
    for (int64_t i = 0; i < TOTAL; ++i)
        price[static_cast<std::size_t>(i)] = 1.0 + static_cast<double>(i) * 0.0001;

    auto schema = Schema::build("bench_100k",
        Column<int64_t>("ts"),
        Column<double>("price"));

    BENCHMARK("write") {
        auto path = fs::temp_directory_path() /
                    ("signet_bench_w_" + std::to_string(TOTAL) + "_rg.parquet");

        WriterOptions opts{};
        opts.row_group_size  = RG_SIZE;
        opts.compression     = Compression::UNCOMPRESSED;

        auto writer = ParquetWriter::open(path.string(), schema, opts);

        for (int64_t rg = 0; rg < NUM_RGS; ++rg) {
            int64_t offset = rg * RG_SIZE;
            (*writer).template write_column<int64_t>(0, ts.data()    + offset, RG_SIZE);
            (*writer).template write_column<double> (1, price.data() + offset, RG_SIZE);
            (*writer).flush_row_group();
        }
        (*writer).close();

        fs::remove(path);
        return TOTAL;
    };
}

// ---------------------------------------------------------------------------
// TEST CASE 5 — string column, 10 K rows
// ---------------------------------------------------------------------------
TEST_CASE("Write throughput — string column 10K rows", "[bench][write]") {
    constexpr int64_t N = 10'000;

    // Build a realistic mix of ticker strings
    const std::vector<std::string> symbols_pool{
        "BTCUSDT", "ETHUSDT", "SOLUSDT", "BNBUSDT", "XRPUSDT"
    };
    std::vector<std::string> symbols(N);
    for (int64_t i = 0; i < N; ++i)
        symbols[static_cast<std::size_t>(i)] =
            symbols_pool[static_cast<std::size_t>(i) % symbols_pool.size()];

    auto schema = Schema::build("bench_string",
        Column<std::string>("symbol"));

    BENCHMARK("write") {
        auto path = fs::temp_directory_path() /
                    ("signet_bench_w_" + std::to_string(N) + "_string.parquet");

        WriterOptions opts{};
        opts.compression     = Compression::UNCOMPRESSED;
        opts.default_encoding = Encoding::RLE_DICTIONARY;

        auto writer = ParquetWriter::open(path.string(), schema, opts);
        (*writer).template write_column<std::string>(0, symbols.data(), static_cast<int64_t>(symbols.size()));
        (*writer).flush_row_group();
        (*writer).close();

        fs::remove(path);
        return N;
    };
}
