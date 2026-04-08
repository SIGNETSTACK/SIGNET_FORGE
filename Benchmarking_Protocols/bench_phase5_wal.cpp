// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// bench_phase5_wal.cpp — Enterprise WAL benchmark suite (7 cases)
//
// WAL1: 100K WalWriter (fwrite)          — buffered stdio path
// WAL2: 100K WalMmapWriter               — mmap ring path
// WAL3: 100K WalManager                  — manager + mutex + roll-check
// WAL4: 1M   WalWriter                   — sustained fwrite throughput
// WAL5: 1M   WalMmapWriter               — sustained mmap throughput
// WAL6: 1M   WAL write + read_all        — full roundtrip pipeline
// WAL7: 100K WalWriter (encrypted)       — PME-gated commercial tier
//
// Per-record latency = total_time / N.  All sync disabled for max throughput.

#include "common.hpp"
#include "signet/ai/wal.hpp"
#include "signet/ai/wal_mapped_segment.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Tick-shaped payload used by all WAL benchmarks.
// 51 bytes — representative of a single tick record in an HFT pipeline.
// ---------------------------------------------------------------------------
static constexpr std::string_view tick_payload{
    "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
};

// ===========================================================================
// WAL1 — 100K records via WalWriter (fwrite path)
// ===========================================================================
// Measures sustained throughput of the buffered stdio writer over 100K
// tick-sized appends.  sync_on_append=false, sync_on_flush=false.
// Per-record cost ~ total_time / 100'000.

TEST_CASE("WAL1: 100K WalWriter throughput", "[bench-enterprise][wal]") {
    bench::TempDir dir("ebench_wal1_");

    WalWriterOptions opts;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    BENCHMARK("WAL1: 100K WalWriter") {
        auto result = WalWriter::open(dir.file("wal1.wal"), opts);
        REQUIRE(result.has_value());
        WalWriter& writer = result.value();

        for (int i = 0; i < 100'000; ++i) {
            (void)writer.append(tick_payload);
        }

        auto seq = writer.next_seq();
        (void)writer.close();
        return seq;
    };
}

// ===========================================================================
// WAL2 — 100K records via WalMmapWriter (mmap ring path)
// ===========================================================================
// Measures sustained throughput of the memory-mapped ring writer over 100K
// tick-sized appends.  No sync, 64 MB segments, 4-slot ring.
// Measured ~223 ns/record (core bench, 100 samples).

TEST_CASE("WAL2: 100K WalMmapWriter throughput", "[bench-enterprise][wal]") {
    bench::TempDir dir("ebench_wal2_");

    BENCHMARK("WAL2: 100K WalMmapWriter") {
        WalMmapOptions opts;
        opts.dir            = dir.file("mmap2");
        opts.name_prefix    = "wal2";
        opts.ring_segments  = 4;
        opts.segment_size   = 64 * 1024 * 1024;  // 64 MB — no rotation at 100K
        opts.sync_on_append = false;
        opts.sync_on_flush  = false;

        auto result = WalMmapWriter::open(opts);
        REQUIRE(result.has_value());
        WalMmapWriter& writer = result.value();

        for (int i = 0; i < 100'000; ++i) {
            (void)writer.append(tick_payload);
        }

        auto seq = writer.next_seq();
        (void)writer.close();
        return seq;
    };
}

// ===========================================================================
// WAL3 — 100K records via WalManager (mutex + segment-roll check)
// ===========================================================================
// Measures the overhead of the WalManager orchestration layer: per-append
// mutex lock/unlock, segment-roll check, and record counter update.
// Segment limits set high enough to avoid rolling during the benchmark.

TEST_CASE("WAL3: 100K WalManager throughput", "[bench-enterprise][wal]") {
    bench::TempDir dir("ebench_wal3_");

    BENCHMARK("WAL3: 100K WalManager") {
        WalManagerOptions opts;
        opts.sync_on_append    = false;
        opts.sync_on_roll      = false;
        opts.max_segment_bytes = 128 * 1024 * 1024;  // 128 MB — no roll
        opts.max_records       = 2'000'000;           // well above 100K
        opts.reset_on_open     = true;
        opts.lifecycle_mode    = WalLifecycleMode::Benchmark;

        auto result = WalManager::open(dir.file("mgr3"), opts);
        REQUIRE(result.has_value());
        WalManager& manager = result.value();

        for (int i = 0; i < 100'000; ++i) {
            (void)manager.append(tick_payload);
        }

        auto total = manager.total_records();
        (void)manager.close();
        return total;
    };
}

// ===========================================================================
// WAL4 — 1M records via WalWriter (fwrite path)
// ===========================================================================
// Sustained 1M-record throughput test for the buffered stdio writer.
// Total data: ~1M * (28 overhead + 51 payload) = ~75 MB.
// Validates that fwrite performance does not degrade at scale.

TEST_CASE("WAL4: 1M WalWriter throughput", "[bench-enterprise][wal]") {
    bench::TempDir dir("ebench_wal4_");

    WalWriterOptions opts;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    BENCHMARK("WAL4: 1M WalWriter") {
        auto result = WalWriter::open(dir.file("wal4.wal"), opts);
        REQUIRE(result.has_value());
        WalWriter& writer = result.value();

        for (int i = 0; i < 1'000'000; ++i) {
            (void)writer.append(tick_payload);
        }

        auto seq = writer.next_seq();
        (void)writer.close();
        return seq;
    };
}

// ===========================================================================
// WAL5 — 1M records via WalMmapWriter (mmap ring path)
// ===========================================================================
// Sustained 1M-record throughput test for the mmap ring writer.
// Total data: ~1M * (28 + 51) = ~75 MB.  With 64 MB segments and 4-slot
// ring, this exercises at least one segment rotation.

TEST_CASE("WAL5: 1M WalMmapWriter throughput", "[bench-enterprise][wal]") {
    bench::TempDir dir("ebench_wal5_");

    BENCHMARK("WAL5: 1M WalMmapWriter") {
        WalMmapOptions opts;
        opts.dir            = dir.file("mmap5");
        opts.name_prefix    = "wal5";
        opts.ring_segments  = 4;
        opts.segment_size   = 64 * 1024 * 1024;  // 64 MB — will rotate ~1-2 times
        opts.sync_on_append = false;
        opts.sync_on_flush  = false;

        auto result = WalMmapWriter::open(opts);
        REQUIRE(result.has_value());
        WalMmapWriter& writer = result.value();

        for (int i = 0; i < 1'000'000; ++i) {
            (void)writer.append(tick_payload);
        }

        auto seq = writer.next_seq();
        (void)writer.close();
        return seq;
    };
}

// ===========================================================================
// WAL6 — 1M WAL write + read_all roundtrip
// ===========================================================================
// Measures the full WAL pipeline: write 1M tick-shaped records via WalWriter,
// then read them all back via WalReader::read_all().  This is the dominant
// cost path during crash recovery and WAL compaction.

TEST_CASE("WAL6: 1M WAL write + read_all roundtrip", "[bench-enterprise][wal]") {
    bench::TempDir dir("ebench_wal6_");

    WalWriterOptions wopts;
    wopts.sync_on_append = false;
    wopts.sync_on_flush  = false;

    BENCHMARK("WAL6: 1M WAL write + read_all") {
        const std::string wal_path = dir.file("wal6.wal");

        // --- Write phase: 1M records ---
        {
            auto result = WalWriter::open(wal_path, wopts);
            REQUIRE(result.has_value());
            WalWriter& writer = result.value();

            for (int i = 0; i < 1'000'000; ++i) {
                (void)writer.append(tick_payload);
            }

            (void)writer.close();
        }

        // --- Read phase: read_all ---
        auto reader_result = WalReader::open(wal_path);
        REQUIRE(reader_result.has_value());
        WalReader& reader = reader_result.value();

        auto entries = reader.read_all();
        REQUIRE(entries.has_value());

        // Return entry count to prevent elision.
        return entries->size();
    };
}

// ===========================================================================
// WAL7 — 100K WalWriter with note encryption (commercial tier)
// ===========================================================================
// Encrypted WAL records would use Parquet Modular Encryption (PME) to
// encrypt the payload before appending to the WAL.  This benchmark is
// gated behind SIGNET_ENABLE_COMMERCIAL to validate the build path.
//
// When the commercial tier is not enabled, this test case is compiled out
// entirely.  The benchmark itself runs the same WalWriter path; the
// encryption overhead would be measured by the delta vs WAL1.

#if SIGNET_ENABLE_COMMERCIAL

TEST_CASE("WAL7: 100K WalWriter (encrypted)", "[bench-enterprise][wal]") {
    bench::TempDir dir("ebench_wal7_");

    WalWriterOptions opts;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    // In the commercial tier, the payload would be encrypted via PME
    // before appending.  For now, this benchmark validates the build
    // integration and measures baseline WalWriter cost under the
    // commercial build configuration.

    BENCHMARK("WAL7: 100K WalWriter (encrypted)") {
        auto result = WalWriter::open(dir.file("wal7_enc.wal"), opts);
        REQUIRE(result.has_value());
        WalWriter& writer = result.value();

        for (int i = 0; i < 100'000; ++i) {
            // TODO(commercial): encrypt tick_payload via PME before append
            (void)writer.append(tick_payload);
        }

        auto seq = writer.next_seq();
        (void)writer.close();
        return seq;
    };
}

#endif // SIGNET_ENABLE_COMMERCIAL
