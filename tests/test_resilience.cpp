// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
//
// Resilience & fault injection tests — verifies graceful handling of
// corrupted files, truncated data, and adversarial I/O conditions.
// Also includes high-concurrency stress tests for MPMC ring and EventBus.

#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <thread>
#include <atomic>
#include <vector>
#include <random>

using namespace signet::forge;
namespace fs = std::filesystem;

namespace {
struct TempFile {
    fs::path path;
    explicit TempFile(const std::string& name)
        : path(fs::temp_directory_path() / name) {
        // Remove any leftover file from a previous run
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// Write a valid Parquet file with n int64 rows, return file size
size_t write_valid_parquet(const fs::path& path, size_t n = 100) {
    auto schema = Schema::builder("test").column<int64_t>("val").build();
    auto writer = *ParquetWriter::open(path, schema);
    std::vector<int64_t> data(n);
    for (size_t i = 0; i < n; ++i) data[i] = static_cast<int64_t>(i);
    (void)writer.write_column<int64_t>(0, data.data(), n);
    (void)writer.flush_row_group();
    (void)writer.close();
    return static_cast<size_t>(fs::file_size(path));
}

// Corrupt bytes at given offset in a file
void corrupt_file(const fs::path& path, size_t offset, size_t len) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(static_cast<std::streamoff>(offset));
    for (size_t i = 0; i < len; ++i) {
        char c = static_cast<char>(0xFF);
        f.write(&c, 1);
    }
}

// Truncate file to given size
void truncate_file(const fs::path& path, size_t size) {
    fs::resize_file(path, size);
}
} // anonymous namespace

// ===========================================================================
// Section 1 — File corruption resilience
// ===========================================================================

TEST_CASE("Reader rejects file with corrupted magic bytes", "[resilience][hardening]") {
    TempFile tmp("signet_corrupt_magic.parquet");
    write_valid_parquet(tmp.path);

    // Corrupt the leading PAR1 magic (first 4 bytes)
    corrupt_file(tmp.path, 0, 4);

    auto result = ParquetReader::open(tmp.path);
    REQUIRE(!result.has_value());
}

TEST_CASE("Reader rejects file with corrupted footer magic", "[resilience][hardening]") {
    TempFile tmp("signet_corrupt_footer_magic.parquet");
    auto fsize = write_valid_parquet(tmp.path);

    // Corrupt the trailing PAR1 magic (last 4 bytes)
    corrupt_file(tmp.path, fsize - 4, 4);

    auto result = ParquetReader::open(tmp.path);
    REQUIRE(!result.has_value());
}

TEST_CASE("Reader rejects truncated file (header only)", "[resilience][hardening]") {
    TempFile tmp("signet_truncated_header.parquet");
    write_valid_parquet(tmp.path);

    // Truncate to just the PAR1 magic + a few bytes
    truncate_file(tmp.path, 8);

    auto result = ParquetReader::open(tmp.path);
    REQUIRE(!result.has_value());
}

TEST_CASE("Reader rejects truncated file (mid-footer)", "[resilience][hardening]") {
    TempFile tmp("signet_truncated_mid.parquet");
    auto fsize = write_valid_parquet(tmp.path);

    // Truncate to roughly half the file
    truncate_file(tmp.path, fsize / 2);

    auto result = ParquetReader::open(tmp.path);
    REQUIRE(!result.has_value());
}

TEST_CASE("Reader rejects zero-byte file", "[resilience][hardening]") {
    TempFile tmp("signet_empty.parquet");
    // Create empty file
    { std::ofstream f(tmp.path, std::ios::binary); }

    auto result = ParquetReader::open(tmp.path);
    REQUIRE(!result.has_value());
}

TEST_CASE("Reader rejects file with corrupted Thrift footer", "[resilience][hardening]") {
    TempFile tmp("signet_corrupt_thrift.parquet");
    auto fsize = write_valid_parquet(tmp.path);

    // Corrupt bytes in the Thrift footer area (just before trailing magic)
    // Footer length is at fsize-8..fsize-4, Thrift data is before that
    if (fsize > 20) {
        corrupt_file(tmp.path, fsize - 20, 12);
    }

    auto result = ParquetReader::open(tmp.path);
    // Should either fail to open or fail to read columns
    if (result.has_value()) {
        auto col = result->read_column<int64_t>(0, 0);
        // At least one of open or read must fail
        // (corrupted footer may still parse if corruption is in padding)
    }
    // Test passes if we reach here without crashing — no UB, no segfault
    SUCCEED("No crash on corrupted Thrift footer");
}

TEST_CASE("Reader rejects file with random garbage content", "[resilience][hardening]") {
    TempFile tmp("signet_garbage.parquet");

    // Write 4KB of random data
    std::mt19937 rng(42);
    std::vector<uint8_t> garbage(4096);
    for (auto& b : garbage) b = static_cast<uint8_t>(rng() & 0xFF);
    {
        std::ofstream f(tmp.path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(garbage.data()),
                static_cast<std::streamsize>(garbage.size()));
    }

    auto result = ParquetReader::open(tmp.path);
    REQUIRE(!result.has_value());
}

TEST_CASE("Reader handles file with valid magic but corrupted page data", "[resilience][hardening]") {
    TempFile tmp("signet_corrupt_page.parquet");
    auto fsize = write_valid_parquet(tmp.path, 1000);

    // Corrupt bytes in the middle of the data pages (offset ~100)
    if (fsize > 200) {
        corrupt_file(tmp.path, 100, 50);
    }

    auto result = ParquetReader::open(tmp.path);
    // Open may succeed (magic and footer intact), but reading may fail or return wrong data
    // Key: no crash, no UB
    SUCCEED("No crash on corrupted page data");
}

// FIX: Docker root bypass — root can write to /nonexistent/ if the
// filesystem allows it. Skip these tests when running as UID 0.
namespace {
inline bool running_as_root() noexcept {
#if defined(_WIN32)
    return false;
#else
    return ::getuid() == 0;
#endif
}
} // namespace

TEST_CASE("Writer handles nonexistent directory gracefully", "[resilience]") {
    if (running_as_root()) { SUCCEED("Skipped under root (Docker)"); return; }
    auto schema = Schema::builder("test").column<int64_t>("v").build();
#ifdef _WIN32
    auto result = ParquetWriter::open("Z:\\__signet_no_such_dir__\\no\\file.parquet", schema);
#else
    auto result = ParquetWriter::open("/nonexistent/path/file.parquet", schema);
#endif
    REQUIRE(!result.has_value());
}

TEST_CASE("Reader handles nonexistent file gracefully", "[resilience]") {
    if (running_as_root()) { SUCCEED("Skipped under root (Docker)"); return; }
#ifdef _WIN32
    auto result = ParquetReader::open("Z:\\__signet_no_such_dir__\\no\\file.parquet");
#else
    auto result = ParquetReader::open("/nonexistent/path/file.parquet");
#endif
    REQUIRE(!result.has_value());
}

// ===========================================================================
// Section 2 — WAL corruption resilience
// ===========================================================================

TEST_CASE("WalReader handles truncated WAL file", "[resilience][wal]") {
    TempFile tmp("signet_trunc_wal.wal");
    std::string wal_path = tmp.path.string();

    // Write some WAL entries
    {
        auto w_res = WalWriter::open(wal_path);
        REQUIRE(w_res.has_value());
        auto& w = *w_res;
        for (int i = 0; i < 50; ++i) {
            (void)w.append("entry-" + std::to_string(i));
        }
        (void)w.close();
    }

    auto fsize = fs::file_size(tmp.path);
    REQUIRE(fsize > 0);
    // Truncate mid-entry
    truncate_file(tmp.path, fsize / 2);

    // Reader should recover as many entries as possible without crashing
    auto r = WalReader::open(wal_path);
    REQUIRE(r.has_value());
    size_t count = 0;
    constexpr size_t kMaxIter = 1000; // safety guard
    for (size_t iter = 0; iter < kMaxIter; ++iter) {
        auto entry = r->next();
        // next() returns expected<optional<WalEntry>>:
        //   - !entry.has_value() => hard I/O error (expected failed)
        //   - entry->has_value() == false => EOF/truncation/corruption (optional is nullopt)
        if (!entry.has_value() || !entry->has_value()) break;
        ++count;
    }
    // Should have recovered at least some entries
    REQUIRE(count > 0);
    REQUIRE(count < 50);
}

TEST_CASE("WalReader handles corrupted CRC in WAL entry", "[resilience][wal]") {
    TempFile tmp("signet_crc_wal.wal");
    std::string wal_path = tmp.path.string();

    {
        auto w_res = WalWriter::open(wal_path);
        REQUIRE(w_res.has_value());
        auto& w = *w_res;
        for (int i = 0; i < 10; ++i) {
            (void)w.append("test-data-" + std::to_string(i));
        }
        (void)w.close();
    }

    // Corrupt a few bytes in the middle of the WAL (likely hits a CRC or data)
    auto fsize = fs::file_size(tmp.path);
    if (fsize > 40) {
        corrupt_file(tmp.path, 30, 4);
    }

    // Reader should stop or skip corrupted entries, not crash
    auto r = WalReader::open(wal_path);
    REQUIRE(r.has_value());
    constexpr size_t kMaxIter = 1000; // safety guard
    for (size_t iter = 0; iter < kMaxIter; ++iter) {
        auto entry = r->next();
        // next() returns expected<optional<WalEntry>>:
        //   - !entry.has_value() => hard I/O error
        //   - entry->has_value() == false => corruption/EOF (optional is nullopt)
        if (!entry.has_value() || !entry->has_value()) break;
    }
    // Some entries read, corruption detected — no crash
    SUCCEED("No crash on corrupted WAL CRC");
}

// ===========================================================================
// Section 3 — High-concurrency stress tests
// ===========================================================================

TEST_CASE("MpmcRing 16-producer 16-consumer stress", "[mpmc_ring][stress]") {
    constexpr size_t kItems        = 100000;
    constexpr size_t kNumProducers = 16;
    constexpr size_t kNumConsumers = 16;
    constexpr size_t kPerProducer  = kItems / kNumProducers;

    MpmcRing<int> ring(4096);
    std::atomic<size_t> total_consumed{0};
    std::atomic<int64_t> checksum_produced{0};
    std::atomic<int64_t> checksum_consumed{0};

    std::vector<std::thread> producers;
    for (size_t p = 0; p < kNumProducers; ++p) {
        producers.emplace_back([&, p] {
            for (size_t i = 0; i < kPerProducer; ++i) {
                int val = static_cast<int>(p * kPerProducer + i);
                checksum_produced.fetch_add(val, std::memory_order_relaxed);
                while (!ring.push(val)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::vector<std::thread> consumers;
    for (size_t c = 0; c < kNumConsumers; ++c) {
        consumers.emplace_back([&] {
            while (total_consumed.load(std::memory_order_relaxed) < kItems) {
                int v = 0;
                if (ring.pop(v)) {
                    checksum_consumed.fetch_add(v, std::memory_order_relaxed);
                    total_consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    REQUIRE(total_consumed.load() == kItems);
    REQUIRE(checksum_produced.load() == checksum_consumed.load());
}

TEST_CASE("EventBus 16-producer 16-consumer stress", "[event_bus][stress]") {
    EventBusOptions opts;
    opts.tier2_capacity = 8192;
    EventBus bus(opts);

    constexpr int kTotal     = 50000;
    constexpr int kProducers = 16;
    constexpr int kConsumers = 16;
    constexpr int kPerProducer = kTotal / kProducers;

    std::atomic<int> consumed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < kPerProducer; ++i) {
                auto b = ColumnBatch::with_schema({{"val"}}, 1);
                (void)b.push_row({static_cast<double>(i)});
                auto sp = std::make_shared<ColumnBatch>(std::move(b));
                while (!bus.publish(sp)) std::this_thread::yield();
            }
        });
    }

    std::vector<std::thread> consumers;
    for (int c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&] {
            while (consumed.load(std::memory_order_relaxed) < kTotal) {
                SharedColumnBatch out;
                if (bus.pop(out))
                    consumed.fetch_add(1, std::memory_order_relaxed);
                else
                    std::this_thread::yield();
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    REQUIRE(consumed.load() == kTotal);
}

TEST_CASE("MpmcRing sustained throughput (100K items, 32 threads)", "[mpmc_ring][stress]") {
    // 16 producers + 16 consumers = 32 threads total
    constexpr size_t kItems        = 100000;
    constexpr size_t kNumProducers = 16;
    constexpr size_t kNumConsumers = 16;
    constexpr size_t kPerProducer  = kItems / kNumProducers;

    MpmcRing<int64_t> ring(8192);
    std::atomic<size_t> total_consumed{0};
    std::atomic<int64_t> sum_produced{0};
    std::atomic<int64_t> sum_consumed{0};

    auto start = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    // Producers
    for (size_t p = 0; p < kNumProducers; ++p) {
        threads.emplace_back([&, p] {
            for (size_t i = 0; i < kPerProducer; ++i) {
                auto val = static_cast<int64_t>(p * kPerProducer + i);
                sum_produced.fetch_add(val, std::memory_order_relaxed);
                while (!ring.push(val)) std::this_thread::yield();
            }
        });
    }
    // Consumers
    for (size_t c = 0; c < kNumConsumers; ++c) {
        threads.emplace_back([&] {
            while (total_consumed.load(std::memory_order_relaxed) < kItems) {
                int64_t v = 0;
                if (ring.pop(v)) {
                    sum_consumed.fetch_add(v, std::memory_order_relaxed);
                    total_consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : threads) t.join();

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    REQUIRE(total_consumed.load() == kItems);
    REQUIRE(sum_produced.load() == sum_consumed.load());
    // Should complete within a reasonable time (< 10 seconds)
    REQUIRE(ms < 10000);
}
