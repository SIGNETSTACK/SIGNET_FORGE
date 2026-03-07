// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// bench_feature_store.cpp — Feature store latency benchmarks
// Tests point-in-time correctness + retrieval speed
//
// Key claims:
//   - Point-in-time single-entity retrieval  < 50 µs
//   - as_of_batch() for 100 entities         < 1 ms
//   - write_batch() throughput measured for 10K vectors × 16 features

#include "signet/ai/feature_writer.hpp"
#include "signet/ai/feature_reader.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string bench_tmp(const char* name) {
    auto p = fs::temp_directory_path() / "signet_bench_feature" / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

static int64_t bench_now_ns() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<nanoseconds>(
            system_clock::now().time_since_epoch()).count());
}

// Build a FeatureVector with `nfeat` feature values for a given entity/ts.
static FeatureVector make_fv_n(const std::string& entity,
                                int64_t            timestamp_ns,
                                size_t             nfeat,
                                double             seed = 1.0) {
    FeatureVector fv;
    fv.entity_id    = entity;
    fv.timestamp_ns = timestamp_ns;
    fv.version      = 1;
    fv.values.resize(nfeat);
    for (size_t i = 0; i < nfeat; ++i)
        fv.values[i] = seed + static_cast<double>(i);
    return fv;
}

// Build FeatureWriterOptions for the given dir with nfeat features.
static FeatureWriterOptions make_writer_opts(const std::string& dir,
                                              size_t             nfeat) {
    FeatureWriterOptions opts;
    opts.output_dir    = dir;
    opts.group.name    = "bench_group";
    opts.row_group_size = 10000;
    opts.max_file_rows  = 1000000;
    opts.file_prefix    = "features";
    opts.group.feature_names.resize(nfeat);
    for (size_t i = 0; i < nfeat; ++i)
        opts.group.feature_names[i] = "f" + std::to_string(i);
    return opts;
}

// ---------------------------------------------------------------------------
// Shared setup: write 10K rows for 100 entities (16 features each).
// Returns a FeatureReader already opened over the written files.
// Call once per TEST_CASE (not inside BENCHMARK) to measure only retrieval.
// ---------------------------------------------------------------------------

struct FeatureDataset {
    std::vector<std::string> files;
    int64_t                  base_ns    = 0;    // timestamp of row 0
    int64_t                  mid_ns     = 0;    // timestamp near row 5000
    int64_t                  end_ns     = 0;    // timestamp of last row

    static FeatureDataset build(const std::string& dir,
                                 size_t n_rows  = 10000,
                                 size_t n_feats = 16,
                                 size_t n_ent   = 100) {
        FeatureDataset ds;
        ds.base_ns = 1'000'000'000LL;  // 1 second epoch offset

        auto opts = make_writer_opts(dir, n_feats);
        auto fw_result = FeatureWriter::create(opts);
        if (!fw_result.has_value()) return ds;
        auto& fw = *fw_result;

        for (size_t i = 0; i < n_rows; ++i) {
            std::string eid = "entity_" + std::to_string(i % n_ent);
            // Space timestamps 1 ms apart so as_of queries have resolution.
            int64_t ts = ds.base_ns + static_cast<int64_t>(i) * 1'000'000LL;
            (void)fw.write(make_fv_n(eid, ts, n_feats, static_cast<double>(i)));
        }
        (void)fw.close();

        ds.files   = fw.output_files();
        ds.mid_ns  = ds.base_ns + static_cast<int64_t>(n_rows / 2) * 1'000'000LL;
        ds.end_ns  = ds.base_ns + static_cast<int64_t>(n_rows - 1) * 1'000'000LL;
        return ds;
    }
};

// ===========================================================================
// Benchmark 1 — write_batch() throughput: 10K vectors × 16 features
// ===========================================================================

TEST_CASE("Feature Store write throughput — 10K vectors, 16 features",
          "[bench][feature_store]") {
    constexpr size_t kRows  = 10000;
    constexpr size_t kFeats = 16;
    constexpr size_t kEnts  = 100;

    // Pre-build the batch of vectors so allocation cost is excluded.
    std::vector<FeatureVector> batch;
    batch.reserve(kRows);
    int64_t base = bench_now_ns();
    for (size_t i = 0; i < kRows; ++i) {
        std::string eid = "entity_" + std::to_string(i % kEnts);
        batch.push_back(make_fv_n(eid,
                                   base + static_cast<int64_t>(i) * 1'000'000LL,
                                   kFeats,
                                   static_cast<double>(i)));
    }

    int64_t rows_written = 0;

    BENCHMARK("write_batch 10K") {
        // Each iteration uses a fresh temp dir so the writer starts clean.
        auto dir        = bench_tmp("fw_throughput");
        auto opts       = make_writer_opts(dir, kFeats);
        auto fw_result  = FeatureWriter::create(opts);
        if (!fw_result.has_value()) return rows_written;
        auto& fw = *fw_result;

        (void)fw.write_batch(batch);
        (void)fw.close();

        rows_written = fw.rows_written();
        fs::remove_all(dir);
        return rows_written;
    };
}

// ===========================================================================
// Benchmark 2 — get() latency: single entity, 1000 calls
// ===========================================================================

TEST_CASE("Feature Store get() latency — 100 entities, 10K rows",
          "[bench][feature_store]") {
    // Setup once outside the BENCHMARK loop.
    auto dir = bench_tmp("fr_get_latency");
    auto ds  = FeatureDataset::build(dir, 10000, 16, 100);

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    int64_t count = 0;

    BENCHMARK("get latest") {
        count = 0;
        for (int i = 0; i < 1000; ++i) {
            auto r = fr.get("entity_42");
            if (r.has_value() && r->has_value()) ++count;
        }
        return count;
    };

    fs::remove_all(dir);
}

// ===========================================================================
// Benchmark 3 — as_of() latency: point-in-time lookup, 1000 calls
// ===========================================================================

TEST_CASE("Feature Store as_of() latency — point-in-time correct lookup",
          "[bench][feature_store]") {
    auto dir = bench_tmp("fr_asof_latency");
    auto ds  = FeatureDataset::build(dir, 10000, 16, 100);

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    // Pick a timestamp midway through the dataset (row ~5000).
    const int64_t mid_ts = ds.mid_ns;
    int64_t count = 0;

    BENCHMARK("as_of mid-range") {
        count = 0;
        for (int i = 0; i < 1000; ++i) {
            auto r = fr.as_of("entity_42", mid_ts);
            if (r.has_value() && r->has_value()) ++count;
        }
        return count;
    };

    fs::remove_all(dir);
}

// ===========================================================================
// Benchmark 4 — as_of_batch() latency: 100 entities at once
// ===========================================================================

TEST_CASE("Feature Store as_of_batch() — 100 entities at once",
          "[bench][feature_store]") {
    auto dir = bench_tmp("fr_asoflbatch_latency");
    auto ds  = FeatureDataset::build(dir, 10000, 16, 100);

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    // Build the vector of 100 entity IDs once (not inside the timed loop).
    std::vector<std::string> ids;
    ids.reserve(100);
    for (int i = 0; i < 100; ++i)
        ids.push_back("entity_" + std::to_string(i));

    const int64_t query_ts = ds.mid_ns;
    size_t result_size     = 0;

    BENCHMARK("as_of_batch 100 entities") {
        auto batch_result = fr.as_of_batch(ids, query_ts);
        if (batch_result.has_value())
            result_size = batch_result->size();
        return result_size;
    };

    fs::remove_all(dir);
}

// ===========================================================================
// Benchmark 5 — history() range query covering ~100 records
// ===========================================================================

TEST_CASE("Feature Store history() range query",
          "[bench][feature_store]") {
    auto dir = bench_tmp("fr_history_latency");
    auto ds  = FeatureDataset::build(dir, 10000, 16, 100);

    FeatureReaderOptions ropts;
    ropts.parquet_files = ds.files;
    auto fr_result = FeatureReader::open(ropts);
    REQUIRE(fr_result.has_value());
    auto& fr = *fr_result;

    // entity_0 appears at rows 0, 100, 200, ..., 9900 (rows that satisfy i%100==0).
    // Timestamps for entity_0: base + i*1_000_000 ns for i in {0,100,200,...,9900}.
    // A range covering i in [0, 100*100 - 1] = [base, base+9999*1ms] captures
    // exactly 100 observations for entity_0 (i=0,100,...,9900).
    // Use a tighter window: rows i=0..9900 step 100 → 100 rows.
    // start = base + 0,  end = base + 9900*1_000_000 ns
    const int64_t start_ns = ds.base_ns;
    const int64_t end_ns   = ds.base_ns + 9900LL * 1'000'000LL;
    size_t result_size     = 0;

    BENCHMARK("history 100 records") {
        auto h = fr.history("entity_0", start_ns, end_ns);
        if (h.has_value())
            result_size = h->size();
        return result_size;
    };

    fs::remove_all(dir);
}
