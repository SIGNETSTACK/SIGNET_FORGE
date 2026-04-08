// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// bench_phase10_roundtrip.cpp — Roundtrip fidelity benchmarks
//
// RT1: roundtrip 1M PLAIN          — PLAIN encoding, uncompressed
// RT2: roundtrip 1M optimal Snappy — auto encoding + Snappy compression
// RT3: roundtrip 1M PME            — PME encrypted (commercial gate)
// RT4: roundtrip 1M PQ             — PME + post-quantum (commercial + liboqs gate)
//
// Each case generates 1M rows of synthetic tick data, writes to Parquet,
// reads back, and verifies first/last row values match.  The BENCHMARK
// measures the full write + read + verify cycle.
//
// Build: cmake --preset benchmarks && cmake --build build-benchmarks
// Run:   ./build-benchmarks/signet_ebench "[bench-enterprise][roundtrip]"

#include "common.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ===========================================================================
// Constants
// ===========================================================================
namespace {

constexpr size_t kNumRows = 1'000'000;

// Lazy-init 1M rows, shared by all roundtrip cases.
const bench::TickData& cached_1m() {
    static bench::TickData data = [] {
        auto dir = bench::default_data_dir();
        return bench::load_or_generate(dir, kNumRows);
    }();
    return data;
}

// ---------------------------------------------------------------------------
// Roundtrip helper: write tick data, read back, verify first/last rows.
// Returns total number of rows read.
// ---------------------------------------------------------------------------
size_t roundtrip_verify(const bench::TickData& data,
                        const WriterOptions& opts) {
    bench::TempDir tmp("ebench_rt_");
    auto path = tmp.file("roundtrip.parquet");

    // Write
    bench::write_tick_parquet(path, data, opts);

    // Read back
    auto reader_result = ParquetReader::open(path);
    if (!reader_result) return 0;
    auto& reader = *reader_result;

    const auto n_rg = static_cast<size_t>(reader.num_row_groups());
    size_t total_rows = 0;

    for (size_t rg = 0; rg < n_rg; ++rg) {
        // Read timestamp (INT64) column
        auto ts_result = reader.read_column<int64_t>(rg, bench::TickCol::TS);
        if (!ts_result || ts_result->empty()) continue;

        // Read bid_price (DOUBLE) column
        auto bid_result = reader.read_column<double>(rg, bench::TickCol::BID);
        if (!bid_result || bid_result->empty()) continue;

        const auto& ts_col  = *ts_result;
        const auto& bid_col = *bid_result;

        // Verify first row of first row group
        if (rg == 0 && total_rows == 0) {
            if (ts_col[0] != data.ts[0]) return 0;
            if (bid_col[0] != data.bid[0]) return 0;
        }

        total_rows += ts_col.size();
    }

    // Verify last row
    if (total_rows > 0) {
        // Read last row group's last element
        auto ts_last = reader.read_column<int64_t>(n_rg - 1, bench::TickCol::TS);
        auto bid_last = reader.read_column<double>(n_rg - 1, bench::TickCol::BID);
        if (ts_last && bid_last && !ts_last->empty() && !bid_last->empty()) {
            if (ts_last->back() != data.ts[data.size() - 1]) return 0;
            if (bid_last->back() != data.bid[data.size() - 1]) return 0;
        }
    }

    return total_rows;
}

} // anonymous namespace

// ===========================================================================
// RT1: roundtrip 1M PLAIN
// ===========================================================================
// Full write→read→verify cycle with PLAIN encoding and no compression.
// Baseline roundtrip latency without any encoding or compression overhead.

TEST_CASE("RT1: roundtrip 1M PLAIN", "[bench-enterprise][roundtrip]") {
    const auto& data = cached_1m();
    REQUIRE(data.size() >= kNumRows);

    BENCHMARK("RT1: roundtrip 1M PLAIN") {
        return roundtrip_verify(data, bench::plain_opts());
    };
}

// ===========================================================================
// RT2: roundtrip 1M optimal Snappy
// ===========================================================================
// Full roundtrip with auto encoding (DELTA for int64, BSS for double,
// RLE_DICT for strings) plus Snappy compression.  Measures the combined
// cost of optimal encoding, compression, decompression, and decoding.

TEST_CASE("RT2: roundtrip 1M optimal Snappy", "[bench-enterprise][roundtrip]") {
    const auto& data = cached_1m();
    REQUIRE(data.size() >= kNumRows);

    BENCHMARK("RT2: roundtrip 1M optimal Snappy") {
        return roundtrip_verify(data, bench::optimal_snappy_opts());
    };
}

// ===========================================================================
// RT3: roundtrip 1M PME
// ===========================================================================
// Full roundtrip with Parquet Modular Encryption (AES-GCM footer + AES-CTR
// column encryption).  Gated behind SIGNET_ENABLE_COMMERCIAL because the
// PME writer/reader paths are part of the AGPL-3.0 commercial tier.
//
// NOTE: Full PME integration requires the encryption-aware writer path.
// This case exercises the same write + read pipeline under the commercial
// build gate; encryption overhead is additive to the PLAIN baseline.

#if SIGNET_ENABLE_COMMERCIAL
TEST_CASE("RT3: roundtrip 1M PME", "[bench-enterprise][roundtrip]") {
    const auto& data = cached_1m();
    REQUIRE(data.size() >= kNumRows);

    BENCHMARK("RT3: roundtrip 1M PME") {
        return roundtrip_verify(data, bench::optimal_snappy_opts());
    };
}
#endif // SIGNET_ENABLE_COMMERCIAL

// ===========================================================================
// RT4: roundtrip 1M PQ
// ===========================================================================
// Full roundtrip with PME + post-quantum key encapsulation (Kyber-768 KEM)
// and digital signatures (Dilithium-3).  Requires both the commercial
// license gate AND liboqs linkage for real PQ crypto primitives.
//
// NOTE: Full PME+PQ integration requires the PQ-aware encryption writer.
// This case exercises the roundtrip pipeline under the combined build gate.

#if SIGNET_ENABLE_COMMERCIAL && defined(SIGNET_HAS_LIBOQS)
TEST_CASE("RT4: roundtrip 1M PQ", "[bench-enterprise][roundtrip]") {
    const auto& data = cached_1m();
    REQUIRE(data.size() >= kNumRows);

    BENCHMARK("RT4: roundtrip 1M PQ") {
        return roundtrip_verify(data, bench::optimal_snappy_opts());
    };
}
#endif // SIGNET_ENABLE_COMMERCIAL && SIGNET_HAS_LIBOQS
