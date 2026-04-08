// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// Benchmarking_Protocols/bench_phase6_features.cpp
// Enterprise benchmark suite — Phase 6: Feature Store (5 cases)
//
// Measures feature store write throughput and read latency across the full
// FeatureWriter/FeatureReader API surface: bulk write, get(), as_of(),
// as_of_batch(), and history() range queries.
//
// Config: 6 entities (BTCUSDT..ADAUSDT), 7 features per vector (bid_price,
// ask_price, bid_qty, ask_qty, spread_bps, mid_price, vwap), timestamps
// spaced 1 ms apart, 100K vectors total.
//
// Build: cmake --preset benchmarks && cmake --build build-benchmarks
// Run:   ./build-benchmarks/signet_ebench "[bench-enterprise][feature-store]"

#include "common.hpp"
#include "signet/ai/feature_writer.hpp"
#include "signet/ai/feature_reader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ===========================================================================
// Constants
// ===========================================================================

namespace {

constexpr size_t kNumVectors  = 100'000;
constexpr size_t kNumEntities = 6;
constexpr size_t kNumFeatures = 7;

const std::array<std::string, kNumEntities> kSymbols = {
    "BTCUSDT", "ETHUSDT", "SOLUSDT", "BNBUSDT", "XRPUSDT", "ADAUSDT"
};

const std::array<std::string, kNumFeatures> kFeatureNames = {
    "bid_price", "ask_price", "bid_qty", "ask_qty",
    "spread_bps", "mid_price", "vwap"
};

// Base timestamp: 1 second epoch offset (in nanoseconds)
constexpr int64_t kBaseNs = 1'000'000'000LL;

// Spacing between consecutive vectors: 1 ms = 1,000,000 ns
constexpr int64_t kStepNs = 1'000'000LL;

// ===========================================================================
// Helpers
// ===========================================================================

/// Build a single FeatureVector with realistic tick-derived values.
FeatureVector make_feature_vector(const std::string& entity,
                                  int64_t            timestamp_ns,
                                  double             seed) {
    FeatureVector fv;
    fv.entity_id    = entity;
    fv.timestamp_ns = timestamp_ns;
    fv.version      = 1;

    double base_price = 45000.0 + seed * 0.01;
    double spread     = 0.5 + std::fmod(seed, 100.0) * 0.01;
    double mid        = base_price + spread / 2.0;

    fv.values = {
        base_price,              // bid_price
        base_price + spread,     // ask_price
        0.001 + seed * 0.0001,   // bid_qty
        0.002 + seed * 0.00005,  // ask_qty
        spread * 100.0 / base_price,  // spread_bps
        mid,                     // mid_price
        mid + 0.05               // vwap (derived)
    };
    return fv;
}

/// Build a batch of 100K FeatureVectors across 6 entities.
std::vector<FeatureVector> build_vectors() {
    std::vector<FeatureVector> vecs;
    vecs.reserve(kNumVectors);
    for (size_t i = 0; i < kNumVectors; ++i) {
        const auto& entity = kSymbols[i % kNumEntities];
        int64_t ts = kBaseNs + static_cast<int64_t>(i) * kStepNs;
        vecs.push_back(make_feature_vector(entity, ts, static_cast<double>(i)));
    }
    return vecs;
}

/// Build FeatureWriterOptions for a given directory.
FeatureWriterOptions make_writer_opts(const std::string& dir) {
    FeatureWriterOptions opts;
    opts.output_dir     = dir;
    opts.group.name     = "tick_features";
    opts.group.feature_names.assign(kFeatureNames.begin(), kFeatureNames.end());
    opts.group.schema_version = 1;
    opts.row_group_size = 10'000;
    opts.max_file_rows  = 1'000'000;
    opts.file_prefix    = "features";
    return opts;
}

// ===========================================================================
// FeatureDataset — shared setup: writes 100K vectors, opens a reader
// ===========================================================================

struct FeatureDataset {
    std::vector<std::string> files;
    int64_t base_ns = 0;
    int64_t mid_ns  = 0;
    int64_t end_ns  = 0;

    static FeatureDataset build(const std::string& dir) {
        FeatureDataset ds;
        ds.base_ns = kBaseNs;

        auto vecs = build_vectors();

        auto opts = make_writer_opts(dir);
        auto fw_result = FeatureWriter::create(opts);
        if (!fw_result.has_value()) return ds;
        auto& fw = *fw_result;

        (void)fw.write_batch(vecs);
        (void)fw.close();

        ds.files  = fw.output_files();
        ds.mid_ns = kBaseNs + static_cast<int64_t>(kNumVectors / 2) * kStepNs;
        ds.end_ns = kBaseNs + static_cast<int64_t>(kNumVectors - 1) * kStepNs;
        return ds;
    }
};

} // anonymous namespace

// ===========================================================================
// FS1: Write 100K vectors
// ===========================================================================

TEST_CASE("FS1: Feature Store write 100K vectors",
          "[bench-enterprise][feature-store]") {
    // Pre-build the batch outside the timed region.
    auto vecs = build_vectors();

    int64_t rows_written = 0;

    BENCHMARK("FS1: write 100K vectors") {
        bench::TempDir tmp("ebench_fs1_");
        auto opts       = make_writer_opts(tmp.path);
        auto fw_result  = FeatureWriter::create(opts);
        if (!fw_result.has_value()) return rows_written;
        auto& fw = *fw_result;

        (void)fw.write_batch(vecs);
        (void)fw.close();

        rows_written = static_cast<int64_t>(fw.rows_written());
        return rows_written;
    };
}

// ===========================================================================
// FS2: get() latest per entity (1000 calls)
// ===========================================================================

TEST_CASE("FS2: Feature Store get() latest 1000x",
          "[bench-enterprise][feature-store]") {
    bench::TempDir tmp("ebench_fs2_");
    auto ds = FeatureDataset::build(tmp.path);
    REQUIRE(!ds.files.empty());

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    int64_t count = 0;

    BENCHMARK("FS2: get latest 1000x") {
        count = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto& entity = kSymbols[static_cast<size_t>(i) % kNumEntities];
            auto r = fr.get(entity);
            if (r.has_value() && r->has_value()) ++count;
        }
        return count;
    };
}

// ===========================================================================
// FS3: as_of() point-in-time (1000 calls)
// ===========================================================================

TEST_CASE("FS3: Feature Store as_of() point-in-time 1000x",
          "[bench-enterprise][feature-store]") {
    bench::TempDir tmp("ebench_fs3_");
    auto ds = FeatureDataset::build(tmp.path);
    REQUIRE(!ds.files.empty());

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    // Query timestamp midway through the dataset.
    const int64_t query_ts = ds.mid_ns;
    int64_t count = 0;

    BENCHMARK("FS3: as_of 1000x") {
        count = 0;
        for (int i = 0; i < 1000; ++i) {
            const auto& entity = kSymbols[static_cast<size_t>(i) % kNumEntities];
            auto r = fr.as_of(entity, query_ts);
            if (r.has_value() && r->has_value()) ++count;
        }
        return count;
    };
}

// ===========================================================================
// FS4: as_of_batch() all 6 entities
// ===========================================================================

TEST_CASE("FS4: Feature Store as_of_batch() 6 entities",
          "[bench-enterprise][feature-store]") {
    bench::TempDir tmp("ebench_fs4_");
    auto ds = FeatureDataset::build(tmp.path);
    REQUIRE(!ds.files.empty());

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    // Build the vector of 6 entity IDs once (not inside the timed loop).
    std::vector<std::string> ids(kSymbols.begin(), kSymbols.end());

    const int64_t query_ts = ds.mid_ns;
    size_t result_size = 0;

    BENCHMARK("FS4: as_of_batch 6 entities") {
        auto batch_result = fr.as_of_batch(ids, query_ts);
        if (batch_result.has_value())
            result_size = batch_result->size();
        return result_size;
    };
}

// ===========================================================================
// FS5: history() 1000-record range
// ===========================================================================

TEST_CASE("FS5: Feature Store history() 1000 records",
          "[bench-enterprise][feature-store]") {
    bench::TempDir tmp("ebench_fs5_");
    auto ds = FeatureDataset::build(tmp.path);
    REQUIRE(!ds.files.empty());

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    // BTCUSDT appears at rows 0, 6, 12, 18, ... (every 6th row, since 6 entities
    // are interleaved). To capture ~1000 observations for BTCUSDT we need the
    // range covering 1000 * 6 = 6000 consecutive rows.
    //
    // Row i for BTCUSDT: i = 0, 6, 12, ..., 5994  (1000 rows)
    // Timestamps:  base + 0*step, base + 6*step, ..., base + 5994*step
    // Window: [base, base + 5994 * step]
    const int64_t start_ns = ds.base_ns;
    const int64_t end_ns   = ds.base_ns + 5994LL * kStepNs;
    size_t result_size = 0;

    BENCHMARK("FS5: history 1000 records") {
        auto h = fr.history("BTCUSDT", start_ns, end_ns);
        if (h.has_value())
            result_size = h->size();
        return result_size;
    };
}
