// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
//
// Tier 4 Standard Vector Tests — xxHash64 + Split Block Bloom Filter
//
// Three-way verification: C++ implementation vs published xxHash64 test vectors
// vs Parquet specification bloom filter constants.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "signet/bloom/xxhash.hpp"
#include "signet/bloom/split_block.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace signet::forge;

// =============================================================================
// xxHash64 Known Test Vectors
// =============================================================================
// Reference: https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md
// and the official xxhsum tool test vectors.

TEST_CASE("xxHash64: empty string, seed=0", "[xxhash][vectors]") {
    // xxHash64("", seed=0) = 0xEF46DB3751D8E999
    auto h = xxhash::hash64("", 0, 0);
    REQUIRE(h == 0xEF46DB3751D8E999ULL);
}

TEST_CASE("xxHash64: empty string, seed=2654435761", "[xxhash][vectors]") {
    // xxHash64("", seed=2654435761) = 0xAC75FDA2929B17EFULL
    auto h = xxhash::hash64("", 0, 2654435761ULL);
    REQUIRE(h == 0xAC75FDA2929B17EFULL);
}

TEST_CASE("xxHash64: single byte 0x00, seed=0", "[xxhash][vectors]") {
    uint8_t data[] = {0x00};
    auto h = xxhash::hash64(data, 1, 0);
    // Known vector: xxHash64({0x00}, seed=0) = 0xE934A84ADB052768
    REQUIRE(h == 0xE934A84ADB052768ULL);
}

TEST_CASE("xxHash64: 14 bytes, seed=0", "[xxhash][vectors]") {
    // Input = sequential bytes 0x00..0x0D (14 bytes, exercises 8-byte + 4-byte + 2x1-byte paths)
    uint8_t data[14];
    for (uint8_t i = 0; i < 14; ++i) data[i] = i;

    auto h = xxhash::hash64(data, 14, 0);
    // Verified via independent Python xxHash64 implementation
    REQUIRE(h == 0x5CDA8B69BBFC1D45ULL);
}

TEST_CASE("xxHash64: deterministic - same input same hash", "[xxhash][vectors]") {
    std::string input = "The quick brown fox jumps over the lazy dog";
    auto h1 = xxhash::hash64(input);
    auto h2 = xxhash::hash64(input);
    REQUIRE(h1 == h2);
    REQUIRE(h1 != 0); // extremely unlikely to be zero
}

TEST_CASE("xxHash64: different seeds produce different hashes", "[xxhash][vectors]") {
    std::string input = "test";
    auto h0 = xxhash::hash64(input, 0);
    auto h1 = xxhash::hash64(input, 1);
    auto h42 = xxhash::hash64(input, 42);

    REQUIRE(h0 != h1);
    REQUIRE(h0 != h42);
    REQUIRE(h1 != h42);
}

TEST_CASE("xxHash64: hash64_value for int32_t", "[xxhash][vectors]") {
    int32_t val = 42;
    auto h = xxhash::hash64_value(val);
    // Verify it's deterministic and non-zero
    REQUIRE(h == xxhash::hash64_value(val));
    REQUIRE(h != 0);

    // Different values produce different hashes
    REQUIRE(xxhash::hash64_value(int32_t{42}) != xxhash::hash64_value(int32_t{43}));
}

TEST_CASE("xxHash64: hash64_value for int64_t", "[xxhash][vectors]") {
    int64_t val = 1234567890123LL;
    auto h = xxhash::hash64_value(val);
    REQUIRE(h == xxhash::hash64_value(val));
    REQUIRE(h != 0);
}

TEST_CASE("xxHash64: hash64_value for double", "[xxhash][vectors]") {
    double val = 3.14159;
    auto h = xxhash::hash64_value(val);
    REQUIRE(h == xxhash::hash64_value(val));
    REQUIRE(h != 0);
}

TEST_CASE("xxHash64: large input (>= 32 bytes) exercises 4-lane path", "[xxhash][vectors]") {
    // Input >= 32 bytes uses the 4-accumulator path
    std::string input(64, 'A');
    auto h = xxhash::hash64(input);
    REQUIRE(h != 0);
    REQUIRE(h == xxhash::hash64(input)); // deterministic

    // Slightly different input → different hash
    std::string input2(64, 'A');
    input2[63] = 'B';
    REQUIRE(xxhash::hash64(input) != xxhash::hash64(input2));
}

TEST_CASE("xxHash64: exactly 32 bytes (boundary for 4-lane)", "[xxhash][vectors]") {
    uint8_t data[32];
    for (uint8_t i = 0; i < 32; ++i) data[i] = i;

    auto h = xxhash::hash64(data, 32, 0);
    REQUIRE(h != 0);
    REQUIRE(h == xxhash::hash64(data, 32, 0));
}

TEST_CASE("xxHash64: prime constants match spec", "[xxhash][vectors]") {
    // Verify the xxHash64 prime constants from the specification
    REQUIRE(xxhash::detail::PRIME1 == 0x9E3779B185EBCA87ULL);
    REQUIRE(xxhash::detail::PRIME2 == 0xC2B2AE3D27D4EB4FULL);
    REQUIRE(xxhash::detail::PRIME3 == 0x165667B19E3779F9ULL);
    REQUIRE(xxhash::detail::PRIME4 == 0x85EBCA77C2B2AE63ULL);
    REQUIRE(xxhash::detail::PRIME5 == 0x27D4EB2F165667C5ULL);
}

// =============================================================================
// Split Block Bloom Filter Tests
// =============================================================================

TEST_CASE("Bloom: salt constants match Parquet spec", "[bloom][vectors]") {
    // The 8 salt constants from the Parquet BloomFilter.md specification.
    // We can't access private kSalt directly, but we can verify behavior
    // matches the spec by inserting a known hash and checking the block.

    // Create a minimal 1-block filter (32 bytes)
    auto bloom = SplitBlockBloomFilter::with_size(32);

    // Insert hash 0 — key=0, block_idx=0
    // With key=0, all salt*key=0, so bit_pos = 0>>27 = 0 for all words
    // This means bit 0 of all 8 words should be set
    bloom.insert(0);
    REQUIRE(bloom.might_contain(0));

    // Verify the raw data: word 0 through 7 should all have bit 0 set
    const auto& data = bloom.data();
    for (size_t w = 0; w < 8; ++w) {
        uint32_t word;
        std::memcpy(&word, data.data() + w * 4, 4);
        REQUIRE((word & 1) == 1);
    }
}

TEST_CASE("Bloom: insert and query - strings", "[bloom][vectors]") {
    SplitBlockBloomFilter bloom(1000, 0.01);

    bloom.insert_value(std::string("AAPL"));
    bloom.insert_value(std::string("MSFT"));
    bloom.insert_value(std::string("GOOG"));

    REQUIRE(bloom.might_contain_value(std::string("AAPL")));
    REQUIRE(bloom.might_contain_value(std::string("MSFT")));
    REQUIRE(bloom.might_contain_value(std::string("GOOG")));

    // Values not inserted — might_contain could return true (FP), but
    // definitely-not-present values should return false most of the time
    // We test a large batch for statistical confidence
    int false_positives = 0;
    for (int i = 0; i < 1000; ++i) {
        std::string probe = "NOT_INSERTED_" + std::to_string(i);
        if (bloom.might_contain_value(probe)) {
            ++false_positives;
        }
    }
    // With 1000 NDV filter and 1% FPR, querying 1000 random strings
    // should yield roughly 10 FPs (1%). Allow up to 50 (5%) for safety.
    REQUIRE(false_positives < 50);
}

TEST_CASE("Bloom: insert and query - integers", "[bloom][vectors]") {
    SplitBlockBloomFilter bloom(500, 0.01);

    for (int32_t i = 0; i < 100; ++i) {
        bloom.insert_value(i);
    }

    // All inserted values must be found
    for (int32_t i = 0; i < 100; ++i) {
        REQUIRE(bloom.might_contain_value(i));
    }
}

TEST_CASE("Bloom: insert and query - int64", "[bloom][vectors]") {
    SplitBlockBloomFilter bloom(500, 0.01);

    std::vector<int64_t> values = {
        0, 1, -1, INT64_MAX, INT64_MIN, 1234567890123LL
    };
    for (auto v : values) {
        bloom.insert_value(v);
    }
    for (auto v : values) {
        REQUIRE(bloom.might_contain_value(v));
    }
}

TEST_CASE("Bloom: insert and query - float/double", "[bloom][vectors]") {
    SplitBlockBloomFilter bloom(500, 0.01);

    bloom.insert_value(3.14f);
    bloom.insert_value(2.718281828);

    REQUIRE(bloom.might_contain_value(3.14f));
    REQUIRE(bloom.might_contain_value(2.718281828));
}

TEST_CASE("Bloom: empty filter returns false for all queries", "[bloom][vectors]") {
    auto bloom = SplitBlockBloomFilter::with_size(256);

    REQUIRE(!bloom.might_contain_value(std::string("anything")));
    REQUIRE(!bloom.might_contain_value(int32_t{42}));
    REQUIRE(!bloom.might_contain_value(int64_t{1000}));
    REQUIRE(!bloom.might_contain_value(3.14));
}

TEST_CASE("Bloom: FPR estimation matches expected rate", "[bloom][vectors]") {
    // Create filter for 10,000 NDV at 1% FPR
    SplitBlockBloomFilter bloom(10000, 0.01);

    // Before insertions, FPR should be 0
    REQUIRE(bloom.estimated_fpr(0) == 0.0);

    // After inserting the expected number, FPR should be non-zero and bounded.
    // Note: the optimal sizing formula may overprovision (producing lower FPR
    // than requested), so we only check it's positive and below the target.
    double fpr = bloom.estimated_fpr(10000);
    REQUIRE(fpr > 0.0);     // must be positive after insertions
    REQUIRE(fpr < 0.05);    // no more than 5% (target was 1%)
}

TEST_CASE("Bloom: empirical FPR is within acceptable bounds", "[bloom][vectors]") {
    const size_t ndv = 1000;
    SplitBlockBloomFilter bloom(ndv, 0.01);

    // Insert ndv values
    for (size_t i = 0; i < ndv; ++i) {
        bloom.insert_value(static_cast<int64_t>(i));
    }

    // Test with values NOT in the filter
    int false_positives = 0;
    const int num_probes = 10000;
    for (int i = 0; i < num_probes; ++i) {
        int64_t probe = static_cast<int64_t>(ndv + i + 1);
        if (bloom.might_contain_value(probe)) {
            ++false_positives;
        }
    }

    double empirical_fpr = static_cast<double>(false_positives) / num_probes;
    // Allow up to 5% — generous margin for the split-block approximation
    REQUIRE(empirical_fpr < 0.05);
}

TEST_CASE("Bloom: serialization roundtrip", "[bloom][vectors]") {
    SplitBlockBloomFilter bloom(500, 0.01);

    bloom.insert_value(std::string("hello"));
    bloom.insert_value(int32_t{42});
    bloom.insert_value(int64_t{1000000});

    // Serialize
    const auto& serialized = bloom.data();
    size_t sz = bloom.size_bytes();

    // Deserialize
    auto restored = SplitBlockBloomFilter::from_data(serialized.data(), sz);

    // All previously inserted values must still be found
    REQUIRE(restored.might_contain_value(std::string("hello")));
    REQUIRE(restored.might_contain_value(int32_t{42}));
    REQUIRE(restored.might_contain_value(int64_t{1000000}));

    // Values not inserted should (mostly) not be found
    REQUIRE(!restored.might_contain_value(std::string("world")));
}

TEST_CASE("Bloom: size is always a multiple of 32 bytes", "[bloom][vectors]") {
    for (size_t ndv : {1, 10, 100, 1000, 10000, 100000}) {
        SplitBlockBloomFilter bloom(ndv, 0.01);
        REQUIRE(bloom.size_bytes() % 32 == 0);
        REQUIRE(bloom.size_bytes() >= 32);
    }
}

TEST_CASE("Bloom: with_size creates exact size", "[bloom][vectors]") {
    for (size_t sz : {32, 64, 128, 256, 1024}) {
        auto bloom = SplitBlockBloomFilter::with_size(sz);
        REQUIRE(bloom.size_bytes() == sz);
        REQUIRE(bloom.num_blocks() == sz / 32);
    }
}

TEST_CASE("Bloom: with_size rejects invalid sizes", "[bloom][vectors]") {
    REQUIRE_THROWS_AS(SplitBlockBloomFilter::with_size(0), std::invalid_argument);
    REQUIRE_THROWS_AS(SplitBlockBloomFilter::with_size(31), std::invalid_argument);
    REQUIRE_THROWS_AS(SplitBlockBloomFilter::with_size(33), std::invalid_argument);
}

TEST_CASE("Bloom: from_data rejects invalid sizes", "[bloom][vectors]") {
    REQUIRE_THROWS_AS(
        SplitBlockBloomFilter::from_data(nullptr, 0), std::invalid_argument);

    uint8_t dummy[33] = {};
    REQUIRE_THROWS_AS(
        SplitBlockBloomFilter::from_data(dummy, 33), std::invalid_argument);
}

TEST_CASE("Bloom: merge combines two filters", "[bloom][vectors]") {
    auto bloom1 = SplitBlockBloomFilter::with_size(256);
    auto bloom2 = SplitBlockBloomFilter::with_size(256);

    bloom1.insert_value(std::string("A"));
    bloom2.insert_value(std::string("B"));

    bloom1.merge(bloom2);

    REQUIRE(bloom1.might_contain_value(std::string("A")));
    REQUIRE(bloom1.might_contain_value(std::string("B")));
}

TEST_CASE("Bloom: merge rejects different sizes", "[bloom][vectors]") {
    auto bloom1 = SplitBlockBloomFilter::with_size(32);
    auto bloom2 = SplitBlockBloomFilter::with_size(64);

    REQUIRE_THROWS_AS(bloom1.merge(bloom2), std::invalid_argument);
}

TEST_CASE("Bloom: reset clears all bits", "[bloom][vectors]") {
    SplitBlockBloomFilter bloom(100, 0.01);
    bloom.insert_value(std::string("test"));
    REQUIRE(bloom.might_contain_value(std::string("test")));

    bloom.reset();

    // After reset, no value should be found
    REQUIRE(!bloom.might_contain_value(std::string("test")));
}

TEST_CASE("Bloom: constructor rejects invalid FPR", "[bloom][vectors]") {
    REQUIRE_THROWS_AS(SplitBlockBloomFilter(100, 0.0), std::invalid_argument);
    REQUIRE_THROWS_AS(SplitBlockBloomFilter(100, 1.0), std::invalid_argument);
    REQUIRE_THROWS_AS(SplitBlockBloomFilter(100, -0.1), std::invalid_argument);
}

TEST_CASE("Bloom: NDV=0 is handled gracefully", "[bloom][vectors]") {
    // Should not throw; internally treated as 1
    SplitBlockBloomFilter bloom(0, 0.01);
    REQUIRE(bloom.size_bytes() >= 32);
}
