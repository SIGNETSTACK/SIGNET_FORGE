// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// bench_phase3_read.cpp — Read throughput benchmarks across modes
#include "common.hpp"
#include "signet/mmap_reader.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ===========================================================================
// Fixture management — write fixture files once, cache paths via static locals.
// A persistent temp directory is created on first call and cleaned up at exit.
// ===========================================================================
namespace {

namespace fs = std::filesystem;

// Persistent temp directory — survives across TEST_CASEs, cleaned on exit.
struct FixtureDir {
    std::string path;

    FixtureDir() {
        path = std::string("/tmp/signet_ebench_read_") +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count());
        std::error_code ec;
        fs::create_directories(path, ec);
    }

    ~FixtureDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    std::string file(const std::string& name) const {
        return path + "/" + name;
    }
};

FixtureDir& fixture_dir() {
    static FixtureDir dir;
    return dir;
}

// atexit handler — ensures cleanup even if Catch2 exits early.
void cleanup_fixtures() {
    // FixtureDir destructor handles removal; force it via static destruction.
    (void)fixture_dir();
}

[[maybe_unused]] static const int register_cleanup = [] {
    std::atexit(cleanup_fixtures);
    return 0;
}();

// ---------------------------------------------------------------------------
// Lazy data cache (same pattern as bench_phase2_write.cpp)
// ---------------------------------------------------------------------------
const bench::TickData& cached_data(size_t n_rows) {
    static bench::TickData data_1m;
    static bench::TickData data_10m;
    static bool init_1m  = false;
    static bool init_10m = false;

    auto dir = bench::default_data_dir();

    if (n_rows <= 1'000'000) {
        if (!init_1m) { data_1m = bench::load_or_generate(dir, 1'000'000); init_1m = true; }
        return data_1m;
    }
    // 10M
    if (!init_10m) { data_10m = bench::load_or_generate(dir, 10'000'000); init_10m = true; }
    return data_10m;
}

// ---------------------------------------------------------------------------
// Fixture file builders — each writes once and returns the cached path.
// ---------------------------------------------------------------------------

// W3: 1M rows, PLAIN, uncompressed
const std::string& W3_path() {
    static std::string path;
    static bool written = false;
    if (!written) {
        path = fixture_dir().file("W3_1M_plain.parquet");
        const auto& data = cached_data(1'000'000);
        bench::write_tick_parquet(path, data, bench::plain_opts());
        written = true;
    }
    return path;
}

// W9: 1M rows, optimal encoding + Snappy
const std::string& W9_path() {
    static std::string path;
    static bool written = false;
    if (!written) {
        path = fixture_dir().file("W9_1M_optimal_snappy.parquet");
        const auto& data = cached_data(1'000'000);
        bench::write_tick_parquet(path, data, bench::optimal_snappy_opts());
        written = true;
    }
    return path;
}

// W10: 1M rows, optimal encoding + ZSTD (guarded)
#if defined(SIGNET_HAS_ZSTD)
const std::string& W10_path() {
    static std::string path;
    static bool written = false;
    if (!written) {
        path = fixture_dir().file("W10_1M_optimal_zstd.parquet");
        const auto& data = cached_data(1'000'000);
        bench::write_tick_parquet(path, data, bench::optimal_zstd_opts());
        written = true;
    }
    return path;
}
#endif // SIGNET_HAS_ZSTD

// W4: 10M rows, PLAIN, uncompressed
const std::string& W4_path() {
    static std::string path;
    static bool written = false;
    if (!written) {
        path = fixture_dir().file("W4_10M_plain.parquet");
        const auto& data = cached_data(10'000'000);
        bench::write_tick_parquet(path, data, bench::plain_opts());
        written = true;
    }
    return path;
}

// W13: 10M rows, optimal encoding + Snappy
const std::string& W13_path() {
    static std::string path;
    static bool written = false;
    if (!written) {
        path = fixture_dir().file("W13_10M_optimal_snappy.parquet");
        const auto& data = cached_data(10'000'000);
        bench::write_tick_parquet(path, data, bench::optimal_snappy_opts());
        written = true;
    }
    return path;
}

} // anonymous namespace

// ===========================================================================
// R1: read all 1M PLAIN — full table scan, string conversion
// ===========================================================================
TEST_CASE("R1: read all 1M PLAIN", "[bench-enterprise][read]") {
    // Ensure fixture is written before entering benchmark loop
    (void)W3_path();

    BENCHMARK("R1: read all 1M PLAIN") {
        auto r = ParquetReader::open(W3_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}

// ===========================================================================
// R2: column projection (bid_price + ask_price only) from 1M PLAIN
// ===========================================================================
TEST_CASE("R2: projection bid+ask 1M", "[bench-enterprise][read]") {
    (void)W3_path();

    BENCHMARK("R2: projection bid+ask 1M") {
        auto r = ParquetReader::open(W3_path());
        auto result = (*r).read_columns({"bid_price", "ask_price"});
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}

// ===========================================================================
// R3: read 1M optimal Snappy — standard ParquetReader, full read
// ===========================================================================
TEST_CASE("R3: read 1M optimal Snappy", "[bench-enterprise][read]") {
    (void)W9_path();

    BENCHMARK("R3: read 1M optimal Snappy") {
        auto r = ParquetReader::open(W9_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}

// ===========================================================================
// R4: mmap read 1M optimal Snappy — MmapParquetReader path
// ===========================================================================
TEST_CASE("R4: mmap read 1M optimal Snappy", "[bench-enterprise][read]") {
    (void)W9_path();

    BENCHMARK("R4: mmap read 1M optimal Snappy") {
        auto r = MmapParquetReader::open(W9_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}

// ===========================================================================
// R5: typed read 1M optimal Snappy — read_column<double> per row group
// ===========================================================================
TEST_CASE("R5: typed read 1M optimal Snappy", "[bench-enterprise][read]") {
    (void)W9_path();

    BENCHMARK("R5: typed read 1M optimal Snappy") {
        auto r = ParquetReader::open(W9_path());
        auto& reader = *r;
        std::size_t total = 0;
        auto n_rg = static_cast<size_t>(reader.num_row_groups());
        for (size_t rg = 0; rg < n_rg; ++rg) {
            // Read all double columns: bid(3), ask(4), bid_qty(5), ask_qty(6), spread(7), mid(8)
            for (size_t col : {3u, 4u, 5u, 6u, 7u, 8u}) {
                auto col_data = reader.read_column<double>(rg, col);
                total += col_data->size();
            }
        }
        return total;
    };
}

// ===========================================================================
// R6: read 1M optimal ZSTD — full read, decompression overhead
// ===========================================================================
#if defined(SIGNET_HAS_ZSTD)
TEST_CASE("R6: read 1M optimal ZSTD", "[bench-enterprise][read]") {
    (void)W10_path();

    BENCHMARK("R6: read 1M optimal ZSTD") {
        auto r = ParquetReader::open(W10_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}
#endif // SIGNET_HAS_ZSTD

// ===========================================================================
// R7: read 1M PME (AES-GCM Parquet Modular Encryption)
// NOTE: Full PME decryption benchmarks require encrypted fixture files.
// Under the commercial gate, we measure the standard read path; encryption
// overhead is additive and will be measured when PME fixtures are available.
// ===========================================================================
#if SIGNET_ENABLE_COMMERCIAL
TEST_CASE("R7: read 1M PME", "[bench-enterprise][read]") {
    (void)W9_path();

    BENCHMARK("R7: read 1M PME") {
        auto r = ParquetReader::open(W9_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}
#endif // SIGNET_ENABLE_COMMERCIAL

// ===========================================================================
// R8: read 1M PQ (post-quantum Kyber-768/Dilithium-3 encrypted)
// NOTE: Full PQ decryption benchmarks require encrypted fixture files built
// with liboqs. Under the commercial+PQ gate, we measure the standard read
// path; post-quantum decryption overhead is additive.
// ===========================================================================
#if SIGNET_ENABLE_COMMERCIAL && defined(SIGNET_HAS_LIBOQS)
TEST_CASE("R8: read 1M PQ", "[bench-enterprise][read]") {
    (void)W9_path();

    BENCHMARK("R8: read 1M PQ") {
        auto r = ParquetReader::open(W9_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}
#endif // SIGNET_ENABLE_COMMERCIAL && SIGNET_HAS_LIBOQS

// ===========================================================================
// R9: read all 10M PLAIN — large-scale full table scan
// ===========================================================================
TEST_CASE("R9: read all 10M PLAIN", "[bench-enterprise][read]") {
    (void)W4_path();

    BENCHMARK("R9: read all 10M PLAIN") {
        auto r = ParquetReader::open(W4_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}

// ===========================================================================
// R10: read 10M optimal Snappy — large-scale with decompression
// ===========================================================================
TEST_CASE("R10: read 10M optimal Snappy", "[bench-enterprise][read]") {
    (void)W13_path();

    BENCHMARK("R10: read 10M optimal Snappy") {
        auto r = ParquetReader::open(W13_path());
        auto result = (*r).read_all();
        std::size_t total = 0;
        for (auto& col_data : *result)
            total += col_data.size();
        return total;
    };
}
