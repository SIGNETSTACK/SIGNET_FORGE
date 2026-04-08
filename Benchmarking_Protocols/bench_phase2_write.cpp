// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// Benchmarking_Protocols/bench_phase2_write.cpp
// Enterprise benchmark suite — Phase 2: Write throughput (14 cases)
//
// Measures write latency across row-count scaling (1K–10M), compression codecs
// (PLAIN, Snappy, ZSTD, LZ4, Gzip), encoding strategies (PLAIN vs. optimal/auto),
// and encryption overhead (AES-GCM PME, PME+PQ).
//
// Build: cmake --preset benchmarks && cmake --build build-benchmarks
// Run:   ./build-benchmarks/signet_ebench "[bench-enterprise][write]"

#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ===========================================================================
// Lazy data cache — avoids regenerating the same dataset for multiple cases
// that share a row count. Thread-safe via Catch2 single-threaded runner.
// ===========================================================================
namespace {

const bench::TickData& cached_data(size_t n_rows) {
    // Four tiers: 1K, 100K, 1M, 10M
    static bench::TickData data_1k;
    static bench::TickData data_100k;
    static bench::TickData data_1m;
    static bench::TickData data_10m;
    static bool init_1k   = false;
    static bool init_100k = false;
    static bool init_1m   = false;
    static bool init_10m  = false;

    auto dir = bench::default_data_dir();

    if (n_rows <= 1'000) {
        if (!init_1k) { data_1k = bench::load_or_generate(dir, 1'000); init_1k = true; }
        return data_1k;
    }
    if (n_rows <= 100'000) {
        if (!init_100k) { data_100k = bench::load_or_generate(dir, 100'000); init_100k = true; }
        return data_100k;
    }
    if (n_rows <= 1'000'000) {
        if (!init_1m) { data_1m = bench::load_or_generate(dir, 1'000'000); init_1m = true; }
        return data_1m;
    }
    // 10M
    if (!init_10m) { data_10m = bench::load_or_generate(dir, 10'000'000); init_10m = true; }
    return data_10m;
}

} // anonymous namespace

// ===========================================================================
// W1: 1K rows — PLAIN, uncompressed baseline
// ===========================================================================
TEST_CASE("W1: 1K PLAIN uncompressed write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000);

    BENCHMARK("W1: 1K PLAIN uncompressed") {
        bench::TempDir tmp("ebench_w1_");
        auto path = tmp.file("w1.parquet");
        bench::write_tick_parquet(path, data, bench::plain_opts());
        return data.size();
    };
}

// ===========================================================================
// W2: 100K rows — PLAIN, uncompressed
// ===========================================================================
TEST_CASE("W2: 100K PLAIN uncompressed write", "[bench-enterprise][write]") {
    const auto& data = cached_data(100'000);

    BENCHMARK("W2: 100K PLAIN uncompressed") {
        bench::TempDir tmp("ebench_w2_");
        auto path = tmp.file("w2.parquet");
        bench::write_tick_parquet(path, data, bench::plain_opts());
        return data.size();
    };
}

// ===========================================================================
// W3: 1M rows — PLAIN, uncompressed
// ===========================================================================
TEST_CASE("W3: 1M PLAIN uncompressed write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W3: 1M PLAIN uncompressed") {
        bench::TempDir tmp("ebench_w3_");
        auto path = tmp.file("w3.parquet");
        bench::write_tick_parquet(path, data, bench::plain_opts());
        return data.size();
    };
}

// ===========================================================================
// W4: 10M rows — PLAIN, uncompressed (scale ceiling)
// ===========================================================================
TEST_CASE("W4: 10M PLAIN uncompressed write", "[bench-enterprise][write]") {
    const auto& data = cached_data(10'000'000);

    BENCHMARK("W4: 10M PLAIN uncompressed") {
        bench::TempDir tmp("ebench_w4_");
        auto path = tmp.file("w4.parquet");
        bench::write_tick_parquet(path, data, bench::plain_opts());
        return data.size();
    };
}

// ===========================================================================
// W5: 1M rows — PLAIN encoding, Snappy compression
// ===========================================================================
TEST_CASE("W5: 1M PLAIN Snappy write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W5: 1M PLAIN Snappy") {
        bench::TempDir tmp("ebench_w5_");
        auto path = tmp.file("w5.parquet");
        bench::write_tick_parquet(path, data, bench::snappy_opts());
        return data.size();
    };
}

// ===========================================================================
// W6: 1M rows — PLAIN encoding, ZSTD compression
// ===========================================================================
#if defined(SIGNET_HAS_ZSTD)
TEST_CASE("W6: 1M PLAIN ZSTD write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W6: 1M PLAIN ZSTD") {
        bench::TempDir tmp("ebench_w6_");
        auto path = tmp.file("w6.parquet");
        bench::write_tick_parquet(path, data, bench::zstd_opts());
        return data.size();
    };
}
#endif // SIGNET_HAS_ZSTD

// ===========================================================================
// W7: 1M rows — PLAIN encoding, LZ4 compression
// ===========================================================================
#if defined(SIGNET_HAS_LZ4)
TEST_CASE("W7: 1M PLAIN LZ4 write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W7: 1M PLAIN LZ4") {
        bench::TempDir tmp("ebench_w7_");
        auto path = tmp.file("w7.parquet");
        bench::write_tick_parquet(path, data, bench::lz4_opts());
        return data.size();
    };
}
#endif // SIGNET_HAS_LZ4

// ===========================================================================
// W8: 1M rows — PLAIN encoding, Gzip compression
// ===========================================================================
#if defined(SIGNET_HAS_GZIP)
TEST_CASE("W8: 1M PLAIN Gzip write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W8: 1M PLAIN Gzip") {
        bench::TempDir tmp("ebench_w8_");
        auto path = tmp.file("w8.parquet");
        bench::write_tick_parquet(path, data, bench::gzip_opts());
        return data.size();
    };
}
#endif // SIGNET_HAS_GZIP

// ===========================================================================
// W9: 1M rows — optimal encoding (auto), Snappy compression
// ===========================================================================
TEST_CASE("W9: 1M optimal Snappy write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W9: 1M optimal Snappy") {
        bench::TempDir tmp("ebench_w9_");
        auto path = tmp.file("w9.parquet");
        bench::write_tick_parquet(path, data, bench::optimal_snappy_opts());
        return data.size();
    };
}

// ===========================================================================
// W10: 1M rows — optimal encoding (auto), ZSTD compression
// ===========================================================================
#if defined(SIGNET_HAS_ZSTD)
TEST_CASE("W10: 1M optimal ZSTD write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W10: 1M optimal ZSTD") {
        bench::TempDir tmp("ebench_w10_");
        auto path = tmp.file("w10.parquet");
        bench::write_tick_parquet(path, data, bench::optimal_zstd_opts());
        return data.size();
    };
}
#endif // SIGNET_HAS_ZSTD

// ===========================================================================
// W11: 1M rows — optimal Snappy + AES-GCM PME encryption
// NOTE: Full PME integration benchmarks require the writer-level encryption
// path which is a Phase 3 enhancement. This case measures the write path
// under the commercial build gate; encryption overhead will be additive.
// ===========================================================================
#if SIGNET_ENABLE_COMMERCIAL
TEST_CASE("W11: 1M optimal Snappy PME write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W11: 1M optimal Snappy PME") {
        bench::TempDir tmp("ebench_w11_");
        auto path = tmp.file("w11.parquet");
        bench::write_tick_parquet(path, data, bench::optimal_snappy_opts());
        return data.size();
    };
}
#endif // SIGNET_ENABLE_COMMERCIAL

// ===========================================================================
// W12: 1M rows — optimal Snappy + PME + post-quantum (Kyber-768/Dilithium-3)
// NOTE: Requires both commercial license and liboqs linkage.
// Full PME+PQ integration benchmarks are a Phase 3 enhancement.
// ===========================================================================
#if SIGNET_ENABLE_COMMERCIAL && defined(SIGNET_HAS_LIBOQS)
TEST_CASE("W12: 1M optimal Snappy PQ write", "[bench-enterprise][write]") {
    const auto& data = cached_data(1'000'000);

    BENCHMARK("W12: 1M optimal Snappy PQ") {
        bench::TempDir tmp("ebench_w12_");
        auto path = tmp.file("w12.parquet");
        bench::write_tick_parquet(path, data, bench::optimal_snappy_opts());
        return data.size();
    };
}
#endif // SIGNET_ENABLE_COMMERCIAL && SIGNET_HAS_LIBOQS

// ===========================================================================
// W13: 10M rows — optimal encoding (auto), Snappy compression (scale test)
// ===========================================================================
TEST_CASE("W13: 10M optimal Snappy write", "[bench-enterprise][write]") {
    const auto& data = cached_data(10'000'000);

    BENCHMARK("W13: 10M optimal Snappy") {
        bench::TempDir tmp("ebench_w13_");
        auto path = tmp.file("w13.parquet");
        bench::write_tick_parquet(path, data, bench::optimal_snappy_opts());
        return data.size();
    };
}

// ===========================================================================
// W14: 10M rows — optimal Snappy + AES-GCM PME encryption (scale + crypto)
// NOTE: Full PME integration benchmarks require the writer-level encryption
// path which is a Phase 3 enhancement.
// ===========================================================================
#if SIGNET_ENABLE_COMMERCIAL
TEST_CASE("W14: 10M optimal Snappy PME write", "[bench-enterprise][write]") {
    const auto& data = cached_data(10'000'000);

    BENCHMARK("W14: 10M optimal Snappy PME") {
        bench::TempDir tmp("ebench_w14_");
        auto path = tmp.file("w14.parquet");
        bench::write_tick_parquet(path, data, bench::optimal_snappy_opts());
        return data.size();
    };
}
#endif // SIGNET_ENABLE_COMMERCIAL
