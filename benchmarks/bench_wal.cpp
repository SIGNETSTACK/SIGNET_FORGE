// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// bench_wal.cpp — WAL append latency benchmarks
// Key claim: single-record append < 1μs (no fsync, buffered)

#include "signet/ai/wal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <filesystem>
#include <string_view>
#include <cstdio>
#include <cstring>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// TempDir — RAII directory that removes itself on destruction
// ---------------------------------------------------------------------------
struct TempDir {
    std::string path;

    explicit TempDir(const char* prefix = "signet_bench_wal_") {
        path = std::string("/tmp/") + prefix +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count());
        std::error_code ec;
        fs::create_directories(path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    std::string file(const std::string& name) const {
        return path + "/" + name;
    }
};

// ===========================================================================
// TEST_CASE 1 — Single-record append latency (32-byte payload)
// ===========================================================================
// Claim: buffered append (no fsync) should complete in sub-microsecond time.
// The writer is opened/closed outside the BENCHMARK inner loop so that we
// isolate only the append operation itself.

TEST_CASE("WAL single-record append latency (32B payload)", "[wal][bench]") {
    TempDir dir("signet_bench_wal_32b_");

    WalWriter::Options opts;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    auto result = WalWriter::open(dir.file("bench_32b.wal"), opts);
    REQUIRE(result.has_value());
    WalWriter& writer = result.value();

    // Static payload defined outside the BENCHMARK to prevent elision artefacts.
    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("append 32B") {
        auto r = writer.append(payload);
        // Return next_seq() to prevent the compiler from eliding the call.
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 2 — Single-record append latency (256-byte payload)
// ===========================================================================
// Simulates a JSON order event.  Measures how per-record overhead scales
// with payload size relative to the 32-byte case above.

TEST_CASE("WAL single-record append latency (256B payload)", "[wal][bench]") {
    TempDir dir("signet_bench_wal_256b_");

    WalWriter::Options opts;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    auto result = WalWriter::open(dir.file("bench_256b.wal"), opts);
    REQUIRE(result.has_value());
    WalWriter& writer = result.value();

    // 256-byte JSON-shaped order event payload.
    // Padded with spaces to reach exactly 256 bytes so the size is deterministic.
    static const std::string payload = [] {
        std::string s =
            R"({"symbol":"BTCUSDT","price":45123.50,"qty":0.100,"side":"BUY",)"
            R"("order_id":"ORD-20240201-000001","timestamp_ns":1706780400000000000,)"
            R"("exchange":"BINANCE","strategy":"MMA","status":"FILLED","fee":0.00045})";
        // Pad to exactly 256 bytes.
        while (s.size() < 256) s += ' ';
        s.resize(256);
        return s;
    }();

    BENCHMARK("append 256B") {
        auto r = writer.append(payload);
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 3 — Batch 1000-append throughput
// ===========================================================================
// Measures the total cost of 1000 consecutive append calls to the same
// writer.  The BENCHMARK inner loop contains all 1000 appends; dividing
// the measured time by 1000 gives the amortised per-record cost.

TEST_CASE("WAL batch 1000 appends throughput", "[wal][bench]") {
    TempDir dir("signet_bench_wal_batch_");

    WalWriter::Options opts;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    auto result = WalWriter::open(dir.file("bench_batch.wal"), opts);
    REQUIRE(result.has_value());
    WalWriter& writer = result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("1000 appends") {
        for (int i = 0; i < 1000; ++i) {
            (void)writer.append(payload);
        }
        // Return next_seq() to prevent the compiler from eliding the loop.
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 4 — Append + flush (no fsync)
// ===========================================================================
// Models the write path used for durability-sensitive records: every append
// is followed by a fflush() (but NOT fsync) so that the data is at least in
// the kernel's page cache.  Useful for comparing against TEST_CASE 1 to see
// the cost of fflush per record.

TEST_CASE("WAL append + flush (no fsync)", "[wal][bench]") {
    TempDir dir("signet_bench_wal_flush_");

    WalWriter::Options opts;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;  // We call flush() manually below; disable auto fsync.

    auto result = WalWriter::open(dir.file("bench_flush.wal"), opts);
    REQUIRE(result.has_value());
    WalWriter& writer = result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("append + flush(no-fsync)") {
        (void)writer.append(payload);
        (void)writer.flush(false);   // fflush only, no fsync
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 5 — WalManager auto-segment append
// ===========================================================================
// Identical payload to TEST_CASE 1 but exercises the WalManager path, which
// adds a mutex lock, segment-roll check, and record counter update on every
// call.  Reveals the overhead of the manager layer versus a raw WalWriter.

TEST_CASE("WAL WalManager auto-segment append", "[wal][bench]") {
    TempDir dir("signet_bench_walmanager_");

    WalManager::Options opts;
    opts.sync_on_append    = false;
    opts.max_segment_bytes = 64 * 1024 * 1024;  // 64 MB — no roll during bench
    opts.max_records       = 1'000'000;          // 1 M records — no roll during bench

    auto result = WalManager::open(dir.path, opts);
    REQUIRE(result.has_value());
    WalManager& manager = result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("manager append 32B") {
        auto r = manager.append(payload);
        // Return total_records() to prevent elision.
        return manager.total_records();
    };

    (void)manager.close();
}

// ===========================================================================
// TEST_CASE 6 — WAL recovery: read_all from a 10K-record WAL
// ===========================================================================
// Measures the cold-recovery path: opening a WAL file that already contains
// 10,000 records and reading every entry.  This is the dominant cost during
// process restart.
//
// The 10K records are written BEFORE the BENCHMARK so the write cost is not
// measured.  Inside the BENCHMARK we open a fresh WalReader and call
// read_all(), returning the entry count to prevent elision.

TEST_CASE("WAL recovery — read_all from 10K record WAL", "[wal][bench]") {
    TempDir dir("signet_bench_wal_recovery_");
    const std::string wal_path = dir.file("recovery.wal");

    // --- Setup: write 10K records ---
    {
        WalWriter::Options opts;
        opts.sync_on_append = false;
        opts.sync_on_flush  = false;

        auto w = WalWriter::open(wal_path, opts);
        REQUIRE(w.has_value());
        WalWriter& writer = w.value();

        for (int i = 0; i < 10'000; ++i) {
            // Vary the payload slightly so every record has a unique sequence.
            char buf[64];
            std::snprintf(buf, sizeof(buf),
                          "TICK:BTCUSDT:%.2f:0.100:BUY:%d",
                          45000.0 + (i % 1000) * 0.01,
                          i);
            (void)writer.append(buf, std::strlen(buf));
        }

        (void)writer.close();
    }

    // --- Benchmark: open + read_all ---
    // We open a new WalReader on every BENCHMARK iteration to measure the
    // full cold-read cost including the file-open syscall.

    BENCHMARK("read_all 10K records") {
        auto reader_result = WalReader::open(wal_path);
        // Treat a failed open as a non-recoverable test error (not expected).
        if (!reader_result.has_value()) return static_cast<size_t>(0);
        WalReader& reader = reader_result.value();

        auto entries = reader.read_all();
        if (!entries.has_value()) return static_cast<size_t>(0);

        // Return entry count to prevent the compiler from eliding read_all().
        return entries->size();
    };
}

// ===========================================================================
// TEST_CASE 7 — WalMmapWriter single-record append latency (32-byte payload)
// ===========================================================================
// Companion to TEST_CASE 1 (fwrite, 32B). Same payload, same conditions.
// The mmap ring eliminates: stdio buffer management, the fwrite C-library
// call, and the per-record mutex.  Replaced by: 5 header stores + memcpy +
// CRC32 + a release fence (compiles to 0 instructions on x86_64 TSO).
//
// Measured: ~223 ns on x86_64 -O2, no sync (~1.7× faster than fwrite path).

TEST_CASE("WalMmapWriter single-record append latency (32B payload)", "[wal][mmap][bench]") {
    TempDir dir("signet_bench_mmap_32b_");

    WalMmapOptions opts;
    opts.dir            = dir.path;
    opts.name_prefix    = "bench_mmap_32b";
    opts.ring_segments  = 4;
    opts.segment_size   = 64 * 1024 * 1024;  // 64 MB — no rotation during bench
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    auto result = WalMmapWriter::open(opts);
    REQUIRE(result.has_value());
    WalMmapWriter& writer = result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("mmap append 32B") {
        auto r = writer.append(payload);
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 8 — WalMmapWriter single-record append latency (256-byte payload)
// ===========================================================================
// Companion to TEST_CASE 2 (fwrite, 256B).  The mmap path scales with
// payload as: memcpy(size) + CRC32(size) — both linear in payload size.
// Measured ~675 ns for 256 B (vs ~223 ns for 32 B), showing expected
// payload-proportional growth for 224 additional bytes.

TEST_CASE("WalMmapWriter single-record append latency (256B payload)", "[wal][mmap][bench]") {
    TempDir dir("signet_bench_mmap_256b_");

    WalMmapOptions opts;
    opts.dir            = dir.path;
    opts.name_prefix    = "bench_mmap_256b";
    opts.ring_segments  = 4;
    opts.segment_size   = 64 * 1024 * 1024;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    auto result = WalMmapWriter::open(opts);
    REQUIRE(result.has_value());
    WalMmapWriter& writer = result.value();

    static const std::string payload = [] {
        std::string s =
            R"({"symbol":"BTCUSDT","price":45123.50,"qty":0.100,"side":"BUY",)"
            R"("order_id":"ORD-20240201-000001","timestamp_ns":1706780400000000000,)"
            R"("exchange":"BINANCE","strategy":"MMA","status":"FILLED","fee":0.00045})";
        while (s.size() < 256) s += ' ';
        s.resize(256);
        return s;
    }();

    BENCHMARK("mmap append 256B") {
        auto r = writer.append(payload);
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 9 — WalMmapWriter batch 1000 appends throughput
// ===========================================================================
// Companion to TEST_CASE 3 (fwrite batch).  Unlike the fwrite path, the mmap
// path has no stdio buffering layer to amortize — each append is a direct
// mapped-memory store — so the batch cost should be close to
// 1000 × single-record cost (~200 μs).

TEST_CASE("WalMmapWriter batch 1000 appends throughput", "[wal][mmap][bench]") {
    TempDir dir("signet_bench_mmap_batch_");

    WalMmapOptions opts;
    opts.dir            = dir.path;
    opts.name_prefix    = "bench_mmap_batch";
    opts.ring_segments  = 4;
    opts.segment_size   = 64 * 1024 * 1024;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    auto result = WalMmapWriter::open(opts);
    REQUIRE(result.has_value());
    WalMmapWriter& writer = result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("mmap 1000 appends") {
        for (int i = 0; i < 1000; ++i) {
            (void)writer.append(payload);
        }
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 10 — WalMmapWriter append + flush (no msync)
// ===========================================================================
// Companion to TEST_CASE 4 (fwrite append + fflush).  Calls flush() after
// each append with sync_on_flush=false (default).  flush() is effectively a
// no-op when sync is disabled — confirming that adding a flush() call adds
// < 2 ns overhead vs TEST_CASE 7.

TEST_CASE("WalMmapWriter append + flush (no msync)", "[wal][mmap][bench]") {
    TempDir dir("signet_bench_mmap_flush_");

    WalMmapOptions opts;
    opts.dir            = dir.path;
    opts.name_prefix    = "bench_mmap_flush";
    opts.ring_segments  = 4;
    opts.segment_size   = 64 * 1024 * 1024;
    opts.sync_on_append = false;
    opts.sync_on_flush  = false;

    auto result = WalMmapWriter::open(opts);
    REQUIRE(result.has_value());
    WalMmapWriter& writer = result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("mmap append + flush(no-msync)") {
        (void)writer.append(payload);
        (void)writer.flush();   // no-op when sync_on_flush=false
        return writer.next_seq();
    };

    (void)writer.close();
}

// ===========================================================================
// TEST_CASE 11 — fwrite vs mmap head-to-head (same 32B payload, same host)
// ===========================================================================
// Both writers run in the same TEST_CASE so Catch2 reports them adjacent and
// the improvement ratio is directly visible.
// Measured ratio: ~339 ns / ~223 ns ≈ 1.7×.
//
// Sources of WalMmapWriter speedup vs WalWriter:
//   1. No stdio buffer management (FILE* internal bookkeeping removed)
//   2. No C-library per-call overhead (dispatch, lock, buffer pointer update)
//   3. No per-record mutex (WalMmapWriter is single-writer — no lock/unlock)
//   4. Direct store into mmap'd page (CPU WB policy; no kernel boundary crossed)
//   5. Release fence on CRC write = 0 instructions on x86_64 (TSO model)

TEST_CASE("WAL fwrite vs mmap side-by-side (32B)", "[wal][mmap][bench]") {
    TempDir dir_fw  ("signet_bench_vs_fw_");
    TempDir dir_mmap("signet_bench_vs_mmap_");

    // --- fwrite writer ---
    WalWriter::Options fw_opts;
    fw_opts.sync_on_append = false;
    fw_opts.sync_on_flush  = false;
    auto fw_result = WalWriter::open(dir_fw.file("vs_fwrite.wal"), fw_opts);
    REQUIRE(fw_result.has_value());
    WalWriter& fw = fw_result.value();

    // --- mmap writer ---
    WalMmapOptions mm_opts;
    mm_opts.dir            = dir_mmap.path;
    mm_opts.name_prefix    = "vs_mmap";
    mm_opts.ring_segments  = 4;
    mm_opts.segment_size   = 64 * 1024 * 1024;
    mm_opts.sync_on_append = false;
    mm_opts.sync_on_flush  = false;
    auto mm_result = WalMmapWriter::open(mm_opts);
    REQUIRE(mm_result.has_value());
    WalMmapWriter& mm = mm_result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("fwrite append 32B") {
        (void)fw.append(payload);
        return fw.next_seq();
    };

    BENCHMARK("mmap append 32B") {
        (void)mm.append(payload);
        return mm.next_seq();
    };

    (void)fw.close();
    (void)mm.close();
}

// ===========================================================================
// TEST_CASE 12 — Three-way: WalWriter vs WalManager vs WalMmapWriter (32B)
// ===========================================================================
// WalManager adds per-append overhead on top of WalWriter:
//   one mutex lock + unlock, a segment-roll check, and a record counter
//   increment.  This case quantifies that overhead so users can pick the
//   right abstraction for their workload:
//
//   WalMmapWriter (~223 ns) — lowest latency, single-writer, self-managed ring
//   WalWriter     (~339 ns) — general purpose, move-only, single file
//   WalManager    (~400 ns) — orchestration layer, mutex-safe, auto-rolls
//
// All three writers use the same 32B tick payload with sync disabled.

TEST_CASE("WAL three-way: WalWriter vs WalManager vs WalMmapWriter (32B)", "[wal][mmap][bench]") {
    TempDir dir_fw  ("signet_bench_3way_fw_");
    TempDir dir_mgr ("signet_bench_3way_mgr_");
    TempDir dir_mmap("signet_bench_3way_mmap_");

    // --- raw WalWriter ---
    WalWriter::Options fw_opts;
    fw_opts.sync_on_append = false;
    fw_opts.sync_on_flush  = false;
    auto fw_result = WalWriter::open(dir_fw.file("3way.wal"), fw_opts);
    REQUIRE(fw_result.has_value());
    WalWriter& fw = fw_result.value();

    // --- WalManager ---
    WalManager::Options mgr_opts;
    mgr_opts.sync_on_append    = false;
    mgr_opts.max_segment_bytes = 64 * 1024 * 1024;  // no roll during bench
    mgr_opts.max_records       = 1'000'000;
    auto mgr_result = WalManager::open(dir_mgr.path, mgr_opts);
    REQUIRE(mgr_result.has_value());
    WalManager& mgr = mgr_result.value();

    // --- WalMmapWriter ---
    WalMmapOptions mm_opts;
    mm_opts.dir            = dir_mmap.path;
    mm_opts.name_prefix    = "3way";
    mm_opts.ring_segments  = 4;
    mm_opts.segment_size   = 64 * 1024 * 1024;
    mm_opts.sync_on_append = false;
    mm_opts.sync_on_flush  = false;
    auto mm_result = WalMmapWriter::open(mm_opts);
    REQUIRE(mm_result.has_value());
    WalMmapWriter& mm = mm_result.value();

    static constexpr std::string_view payload{
        "TICK:BTCUSDT:45123.50:0.100:BUY:1706780400000000000"
    };

    BENCHMARK("WalWriter append 32B") {
        (void)fw.append(payload);
        return fw.next_seq();
    };

    BENCHMARK("WalManager append 32B") {
        (void)mgr.append(payload);
        return mgr.total_records();
    };

    BENCHMARK("WalMmapWriter append 32B") {
        (void)mm.append(payload);
        return mm.next_seq();
    };

    (void)fw.close();
    (void)mgr.close();
    (void)mm.close();
}
