// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include "signet/bloom/xxhash.hpp"
#include "signet/bloom/split_block.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: RAII temp file that cleans up on destruction
// ---------------------------------------------------------------------------
namespace {
struct TempFile {
    fs::path path;

    explicit TempFile(const std::string& name)
        : path(fs::temp_directory_path() / name) {}

    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
} // anonymous namespace

// ===================================================================
// xxHash64 Tests
// ===================================================================

TEST_CASE("xxHash64 empty input", "[bloom][xxhash]") {
    uint64_t h = xxhash::hash64("", 0, 0);

    // Empty input with seed=0 should produce a known non-zero value
    REQUIRE(h != 0);

    // Deterministic: same input produces the same hash
    uint64_t h2 = xxhash::hash64("", 0, 0);
    REQUIRE(h == h2);
}

TEST_CASE("xxHash64 deterministic", "[bloom][xxhash]") {
    // Hash "hello" multiple times — must be identical
    uint64_t h1 = xxhash::hash64(std::string("hello"));
    uint64_t h2 = xxhash::hash64(std::string("hello"));
    REQUIRE(h1 == h2);

    // Hash "hello" vs "Hello" — must differ (case sensitive)
    uint64_t h3 = xxhash::hash64(std::string("Hello"));
    REQUIRE(h1 != h3);
}

TEST_CASE("xxHash64 typed values", "[bloom][xxhash]") {
    // int64_t(42) vs int64_t(43) — different values produce different hashes
    uint64_t h42 = xxhash::hash64_value(int64_t{42});
    uint64_t h43 = xxhash::hash64_value(int64_t{43});
    REQUIRE(h42 != h43);

    // double(3.14) — should produce a non-zero hash
    uint64_t hpi = xxhash::hash64_value(double{3.14});
    REQUIRE(hpi != 0);
}

// ===================================================================
// Split Block Bloom Filter Tests
// ===================================================================

TEST_CASE("Bloom filter insert and check", "[bloom][filter]") {
    SplitBlockBloomFilter bf(100, 0.01);

    bf.insert_value(std::string("BTCUSD"));
    bf.insert_value(std::string("ETHUSD"));
    bf.insert_value(std::string("SOLUSD"));

    // Inserted values must be found (no false negatives)
    REQUIRE(bf.might_contain_value(std::string("BTCUSD")) == true);
    REQUIRE(bf.might_contain_value(std::string("ETHUSD")) == true);

    // Non-inserted value should (with overwhelming probability) not be found
    // This can fail with probability ~0.01 — acceptable for a test
    REQUIRE(bf.might_contain_value(std::string("XRPUSD")) == false);
}

TEST_CASE("Bloom filter numeric values", "[bloom][filter]") {
    SplitBlockBloomFilter bf(100, 0.01);

    bf.insert_value(int64_t{100});
    bf.insert_value(int64_t{200});
    bf.insert_value(int64_t{300});

    REQUIRE(bf.might_contain_value(int64_t{100}) == true);
    REQUIRE(bf.might_contain_value(int64_t{200}) == true);

    // 999 was not inserted — should not be found (with high probability)
    REQUIRE(bf.might_contain_value(int64_t{999}) == false);
}

TEST_CASE("Bloom filter FPR estimation", "[bloom][filter]") {
    constexpr size_t NDV = 1000;
    constexpr double TARGET_FPR = 0.05;

    SplitBlockBloomFilter bf(NDV, TARGET_FPR);

    // Insert 1000 unique strings
    for (size_t i = 0; i < NDV; ++i) {
        bf.insert_value("item_" + std::to_string(i));
    }

    // Test 10000 non-inserted strings and count false positives
    constexpr size_t TEST_COUNT = 10000;
    size_t false_positives = 0;

    for (size_t i = 0; i < TEST_COUNT; ++i) {
        if (bf.might_contain_value("test_" + std::to_string(i))) {
            ++false_positives;
        }
    }

    double actual_fpr = static_cast<double>(false_positives) /
                        static_cast<double>(TEST_COUNT);

    // Actual FPR should be within 2x of target (< 0.10)
    REQUIRE(actual_fpr < TARGET_FPR * 2.0);
}

TEST_CASE("Bloom filter serialization roundtrip", "[bloom][filter]") {
    SplitBlockBloomFilter bf(100, 0.01);

    bf.insert_value(std::string("BTCUSD"));
    bf.insert_value(std::string("ETHUSD"));
    bf.insert_value(int64_t{42});

    // Serialize
    const auto& raw = bf.data();
    REQUIRE(!raw.empty());
    REQUIRE((raw.size() % SplitBlockBloomFilter::kBytesPerBlock) == 0);

    // Reconstruct from serialized bytes
    auto bf2 = SplitBlockBloomFilter::from_data(raw.data(), raw.size());

    // Verify same might_contain results
    REQUIRE(bf2.might_contain_value(std::string("BTCUSD")) == true);
    REQUIRE(bf2.might_contain_value(std::string("ETHUSD")) == true);
    REQUIRE(bf2.might_contain_value(int64_t{42}) == true);
    REQUIRE(bf2.might_contain_value(std::string("XRPUSD")) == false);
}

TEST_CASE("Bloom filter merge", "[bloom][filter]") {
    // Create two same-sized filters
    auto bf1 = SplitBlockBloomFilter::with_size(256);
    auto bf2 = SplitBlockBloomFilter::with_size(256);

    bf1.insert_value(std::string("A"));
    bf2.insert_value(std::string("B"));

    // Before merge: bf1 has A but not B
    REQUIRE(bf1.might_contain_value(std::string("A")) == true);
    // B may or may not be a false positive before merge — just check after

    // Merge bf2 into bf1
    bf1.merge(bf2);

    // After merge: bf1 should have both A and B
    REQUIRE(bf1.might_contain_value(std::string("A")) == true);
    REQUIRE(bf1.might_contain_value(std::string("B")) == true);
}

// ===================================================================
// Bloom Filter Integration Tests (Writer/Reader)
// ===================================================================

TEST_CASE("Write with bloom filter, read and check", "[bloom][pipeline]") {
    TempFile tmp("signet_test_bloom_rw.parquet");

    auto schema = Schema::builder("trades")
        .column<int64_t>("id")
        .column<std::string>("symbol")
        .build();

    ParquetWriter::Options opts;
    opts.enable_bloom_filter = true;
    opts.bloom_filter_fpr = 0.01;

    constexpr size_t NUM_ROWS = 100;

    const std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "SOLUSD"};

    // Write 100 rows with bloom filter enabled
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            auto r = writer.write_row({
                std::to_string(static_cast<int64_t>(i)),
                symbols[i % symbols.size()]
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read and check bloom filter for the "symbol" column (index 1)
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));
        REQUIRE(reader.num_row_groups() >= 1);

        // Read bloom filter for column 1 ("symbol") in row group 0
        auto bf_result = reader.read_bloom_filter(0, 1);
        REQUIRE(bf_result.has_value());
        auto& bf = bf_result.value();

        // Inserted values must be found
        REQUIRE(bf.might_contain_value(std::string("BTCUSD")) == true);
        REQUIRE(bf.might_contain_value(std::string("ETHUSD")) == true);
        REQUIRE(bf.might_contain_value(std::string("SOLUSD")) == true);

        // Non-inserted value should not be found (with high probability)
        REQUIRE(bf.might_contain_value(std::string("DOGEUSD")) == false);
    }
}

TEST_CASE("Bloom predicate pushdown", "[bloom][pipeline]") {
    TempFile tmp("signet_test_bloom_predicate.parquet");

    auto schema = Schema::builder("trades")
        .column<int64_t>("id")
        .column<std::string>("symbol")
        .build();

    ParquetWriter::Options opts;
    opts.row_group_size = 100;
    opts.enable_bloom_filter = true;
    opts.bloom_filter_fpr = 0.01;

    // Write 200 rows across 2 row groups (100 each)
    // Row group 0: symbols = BTCUSD, ETHUSD, SOLUSD (alternating)
    // Row group 1: symbols = AVAXUSD, DOTUSD, LINKUSD (alternating)
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        const std::vector<std::string> symbols_rg0 = {"BTCUSD", "ETHUSD", "SOLUSD"};
        const std::vector<std::string> symbols_rg1 = {"AVAXUSD", "DOTUSD", "LINKUSD"};

        for (size_t i = 0; i < 100; ++i) {
            auto r = writer.write_row({
                std::to_string(static_cast<int64_t>(i)),
                symbols_rg0[i % symbols_rg0.size()]
            });
            REQUIRE(r.has_value());
        }

        for (size_t i = 0; i < 100; ++i) {
            auto r = writer.write_row({
                std::to_string(static_cast<int64_t>(100 + i)),
                symbols_rg1[i % symbols_rg1.size()]
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read and check bloom filters for predicate pushdown
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == 200);
        REQUIRE(reader.num_row_groups() == 2);

        // "BTCUSD" should be in row group 0 but NOT in row group 1
        REQUIRE(reader.bloom_might_contain(0, 1, std::string("BTCUSD")) == true);
        REQUIRE(reader.bloom_might_contain(1, 1, std::string("BTCUSD")) == false);

        // "AVAXUSD" should be in row group 1 but NOT in row group 0
        REQUIRE(reader.bloom_might_contain(0, 1, std::string("AVAXUSD")) == false);
        REQUIRE(reader.bloom_might_contain(1, 1, std::string("AVAXUSD")) == true);
    }
}

TEST_CASE("Bloom filter column selection", "[bloom][pipeline]") {
    TempFile tmp("signet_test_bloom_colselect.parquet");

    auto schema = Schema::builder("trades")
        .column<int64_t>("id")
        .column<std::string>("symbol")
        .build();

    ParquetWriter::Options opts;
    opts.enable_bloom_filter = true;
    opts.bloom_filter_columns = {"symbol"};  // Only bloom filter for "symbol"

    // Write data
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (size_t i = 0; i < 50; ++i) {
            auto r = writer.write_row({
                std::to_string(static_cast<int64_t>(i)),
                (i % 2 == 0) ? "BTCUSD" : "ETHUSD"
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Verify: bloom filter exists for "symbol" (col 1) but NOT for "id" (col 0)
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        // "id" column (index 0) should NOT have a bloom filter
        auto bf_id = reader.read_bloom_filter(0, 0);
        REQUIRE_FALSE(bf_id.has_value());

        // "symbol" column (index 1) should have a bloom filter
        auto bf_sym = reader.read_bloom_filter(0, 1);
        REQUIRE(bf_sym.has_value());
        REQUIRE(bf_sym->might_contain_value(std::string("BTCUSD")) == true);
    }
}
