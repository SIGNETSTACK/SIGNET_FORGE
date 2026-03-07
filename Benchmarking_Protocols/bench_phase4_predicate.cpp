// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// bench_phase4_predicate.cpp — Predicate pushdown and bloom filter benchmarks
//
// Simulates predicate pushdown by reading column data per row group and
// filtering in-memory. Measures the cost of scan+filter for common HFT query
// patterns: single-column equality, multi-column conjunction, timestamp range,
// bloom filter probing, and large-scale (10M) symbol scan.
//
// P1: symbol="BTCUSDT"               — single-column equality on 1M rows
// P2: symbol="BTCUSDT" AND exchange="BINANCE" — multi-column conjunction on 1M rows
// P3: 1-hour time window             — timestamp range scan on 1M rows
// P4: bloom filter probe (symbol)    — per-row-group bloom probe on 1M rows
// P5: symbol="BTCUSDT"               — single-column equality on 10M rows
//
// Build: cmake --preset benchmarks && cmake --build build-benchmarks
// Run:   ./build-benchmarks/signet_ebench "[bench-enterprise][predicate]"

#include "common.hpp"
#include "signet/bloom/split_block.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ===========================================================================
// Lazy data cache — avoids regenerating the same dataset across test cases
// that share a row count. Thread-safe via Catch2 single-threaded runner.
// ===========================================================================
namespace {

const bench::TickData& cached_data_1m() {
    static bench::TickData data;
    static bool init = false;
    if (!init) {
        data = bench::load_or_generate(bench::default_data_dir(), 1'000'000);
        init = true;
    }
    return data;
}

const bench::TickData& cached_data_10m() {
    static bench::TickData data;
    static bool init = false;
    if (!init) {
        data = bench::load_or_generate(bench::default_data_dir(), 10'000'000);
        init = true;
    }
    return data;
}

// Row group size for predicate benchmarks: 128K rows (matches common.hpp default)
constexpr size_t kRowGroupSize = 128 * 1024;

} // anonymous namespace

// ===========================================================================
// P1: filter symbol="BTCUSDT" on 1M rows
// ===========================================================================
// Reads the symbol column (col 1) from each row group and counts matches.
// Simulates predicate pushdown by scanning per-row-group and filtering
// in-memory. With 6 symbols cycling, ~1/6 of rows match (≈166K).

TEST_CASE("P1: filter symbol 1M", "[bench-enterprise][predicate]") {
    const auto& data = cached_data_1m();

    // Write the fixture file once, outside the timed region.
    bench::TempDir tmp("ebench_p1_");
    auto path = tmp.file("pred_1m.parquet");
    REQUIRE(bench::write_tick_parquet(path, data, bench::optimal_snappy_opts(), kRowGroupSize));

    int64_t matched = 0;

    BENCHMARK("P1: filter symbol 1M") {
        auto reader_result = ParquetReader::open(path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        matched = 0;
        for (int64_t rg = 0; rg < reader.num_row_groups(); ++rg) {
            auto symbols = reader.read_column<std::string>(
                static_cast<size_t>(rg), bench::TickCol::SYMBOL);
            REQUIRE(symbols.has_value());
            for (const auto& s : *symbols) {
                if (s == "BTCUSDT") ++matched;
            }
        }
        return matched;
    };
}

// ===========================================================================
// P2: filter symbol="BTCUSDT" AND exchange="BINANCE" on 1M rows
// ===========================================================================
// Reads both symbol (col 1) and exchange (col 2) per row group. Counts
// rows where both predicates are satisfied. With 6 symbols and 3 exchanges
// cycling independently, the joint match rate is 1/18 of rows (≈55K).

TEST_CASE("P2: filter symbol+exchange 1M", "[bench-enterprise][predicate]") {
    const auto& data = cached_data_1m();

    bench::TempDir tmp("ebench_p2_");
    auto path = tmp.file("pred_1m.parquet");
    REQUIRE(bench::write_tick_parquet(path, data, bench::optimal_snappy_opts(), kRowGroupSize));

    int64_t matched = 0;

    BENCHMARK("P2: filter symbol+exchange 1M") {
        auto reader_result = ParquetReader::open(path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        matched = 0;
        for (int64_t rg = 0; rg < reader.num_row_groups(); ++rg) {
            auto rg_idx = static_cast<size_t>(rg);
            auto symbols = reader.read_column<std::string>(rg_idx, bench::TickCol::SYMBOL);
            auto exchanges = reader.read_column<std::string>(rg_idx, bench::TickCol::EXCH);
            REQUIRE(symbols.has_value());
            REQUIRE(exchanges.has_value());

            size_t n = symbols->size();
            for (size_t i = 0; i < n; ++i) {
                if ((*symbols)[i] == "BTCUSDT" && (*exchanges)[i] == "BINANCE") {
                    ++matched;
                }
            }
        }
        return matched;
    };
}

// ===========================================================================
// P3: time range filter (1-hour window) on 1M rows
// ===========================================================================
// Reads the timestamp column (col 0, INT64 milliseconds) per row group and
// counts rows falling within a 1-hour window (3,600,000 ms). The window
// starts at min_ts + 100,000 ms to ensure it sits within the data range.
//
// With synthetic data at 1ms spacing, a 3.6M ms window captures at most
// 3.6M rows — but capped by the 1M row count.

TEST_CASE("P3: time range filter 1M", "[bench-enterprise][predicate]") {
    const auto& data = cached_data_1m();

    bench::TempDir tmp("ebench_p3_");
    auto path = tmp.file("pred_1m.parquet");
    REQUIRE(bench::write_tick_parquet(path, data, bench::optimal_snappy_opts(), kRowGroupSize));

    // Compute time window: [min_ts + 100K, min_ts + 100K + 3.6M)
    // Base ts from generate_ticks: 1706780400000 (ms)
    // With 1M rows at 1ms spacing, max ts = base + 999999
    constexpr int64_t kWindowOffsetMs = 100'000;
    constexpr int64_t kWindowSizeMs   = 3'600'000; // 1 hour in milliseconds
    const int64_t window_start = data.ts.front() + kWindowOffsetMs;
    const int64_t window_end   = window_start + kWindowSizeMs;

    int64_t matched = 0;

    BENCHMARK("P3: time range filter 1M") {
        auto reader_result = ParquetReader::open(path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        matched = 0;
        for (int64_t rg = 0; rg < reader.num_row_groups(); ++rg) {
            auto timestamps = reader.read_column<int64_t>(
                static_cast<size_t>(rg), bench::TickCol::TS);
            REQUIRE(timestamps.has_value());
            for (const auto ts : *timestamps) {
                if (ts >= window_start && ts < window_end) ++matched;
            }
        }
        return matched;
    };
}

// ===========================================================================
// P4: bloom filter probe (symbol column) on 1M rows
// ===========================================================================
// Uses ParquetReader::bloom_might_contain() to probe the per-row-group bloom
// filter on the symbol column before reading the full column data. This is
// the true predicate pushdown path: if the bloom filter says "definitely not
// present", the row group is skipped entirely.
//
// For BTCUSDT (present in every row group of synthetic data), the bloom
// filter should return true for every row group — no pruning. The benchmark
// measures the cost of N bloom probes + full column reads.
//
// We also probe for a symbol that does NOT exist ("ZZZUSDT") to measure
// the bloom filter's ability to eliminate row groups.

TEST_CASE("P4: bloom probe 1M", "[bench-enterprise][predicate]") {
    const auto& data = cached_data_1m();

    bench::TempDir tmp("ebench_p4_");
    auto path = tmp.file("pred_1m.parquet");
    REQUIRE(bench::write_tick_parquet(path, data, bench::optimal_snappy_opts(), kRowGroupSize));

    int64_t matched = 0;

    BENCHMARK("P4: bloom probe 1M") {
        auto reader_result = ParquetReader::open(path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        matched = 0;
        int64_t skipped_rgs = 0;

        for (int64_t rg = 0; rg < reader.num_row_groups(); ++rg) {
            auto rg_idx = static_cast<size_t>(rg);

            // --- Bloom filter probe: skip row group if BTCUSDT definitely absent ---
            if (!reader.bloom_might_contain<std::string>(
                    rg_idx, bench::TickCol::SYMBOL, std::string("BTCUSDT"))) {
                ++skipped_rgs;
                continue;
            }

            // --- Bloom says "maybe": read the full column and filter ---
            auto symbols = reader.read_column<std::string>(rg_idx, bench::TickCol::SYMBOL);
            REQUIRE(symbols.has_value());
            for (const auto& s : *symbols) {
                if (s == "BTCUSDT") ++matched;
            }
        }

        // Also probe a non-existent symbol to exercise bloom filter rejection
        for (int64_t rg = 0; rg < reader.num_row_groups(); ++rg) {
            if (!reader.bloom_might_contain<std::string>(
                    static_cast<size_t>(rg), bench::TickCol::SYMBOL,
                    std::string("ZZZUSDT"))) {
                ++skipped_rgs;
            }
        }

        return matched + skipped_rgs;
    };
}

// ===========================================================================
// P5: filter symbol="BTCUSDT" on 10M rows (scale validation)
// ===========================================================================
// Same as P1 but at 10x scale. Validates that predicate scan cost scales
// linearly with row count. Expected ~1.67M matches (10M / 6).

TEST_CASE("P5: filter symbol 10M", "[bench-enterprise][predicate]") {
    const auto& data = cached_data_10m();

    bench::TempDir tmp("ebench_p5_");
    auto path = tmp.file("pred_10m.parquet");
    REQUIRE(bench::write_tick_parquet(path, data, bench::optimal_snappy_opts(), kRowGroupSize));

    int64_t matched = 0;

    BENCHMARK("P5: filter symbol 10M") {
        auto reader_result = ParquetReader::open(path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        matched = 0;
        for (int64_t rg = 0; rg < reader.num_row_groups(); ++rg) {
            auto symbols = reader.read_column<std::string>(
                static_cast<size_t>(rg), bench::TickCol::SYMBOL);
            REQUIRE(symbols.has_value());
            for (const auto& s : *symbols) {
                if (s == "BTCUSDT") ++matched;
            }
        }
        return matched;
    };
}
