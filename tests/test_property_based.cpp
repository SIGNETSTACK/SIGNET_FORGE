// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// test_property_based.cpp — Property-based / generative testing (Gap T-9)
//
// Validates encode/decode identity properties using randomized inputs:
//   forall input: decode(encode(input)) == input
//
// Uses Catch2 GENERATE + random seeds for reproducible generative testing.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "signet/forge.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace {

/// Deterministic PRNG seeded from a Catch2 GENERATE value.
std::mt19937_64 make_rng(uint64_t seed) { return std::mt19937_64{seed}; }

/// Generate n random uint32_t values in [0, max_val].
std::vector<uint32_t> random_u32(std::mt19937_64& rng, size_t n, uint32_t max_val) {
    std::uniform_int_distribution<uint32_t> dist(0, max_val);
    std::vector<uint32_t> out(n);
    for (auto& v : out) v = dist(rng);
    return out;
}

/// Generate n random int64_t values in [lo, hi].
std::vector<int64_t> random_i64(std::mt19937_64& rng, size_t n,
                                 int64_t lo = -1'000'000, int64_t hi = 1'000'000) {
    std::uniform_int_distribution<int64_t> dist(lo, hi);
    std::vector<int64_t> out(n);
    for (auto& v : out) v = dist(rng);
    return out;
}

/// Generate n random doubles in [lo, hi].
std::vector<double> random_f64(std::mt19937_64& rng, size_t n,
                                double lo = -1e6, double hi = 1e6) {
    std::uniform_real_distribution<double> dist(lo, hi);
    std::vector<double> out(n);
    for (auto& v : out) v = dist(rng);
    return out;
}

/// Minimum bit width needed to represent max_val.
int bit_width_for(uint32_t max_val) {
    if (max_val == 0) return 1;
    int bits = 0;
    uint32_t v = max_val;
    while (v > 0) { v >>= 1; ++bits; }
    return bits;
}

} // anonymous namespace

// ===========================================================================
// Property: RLE encode/decode is identity
// ===========================================================================
TEST_CASE("Property: RLE encode/decode identity", "[property][encoding][rle]") {
    auto seed = GENERATE(42u, 137u, 2718u, 31415u, 99999u);
    auto rng = make_rng(seed);

    auto n = GENERATE(1u, 10u, 100u, 1000u);
    auto max_val = GENERATE(1u, 3u, 15u, 255u);

    CAPTURE(seed, n, max_val);

    auto values = random_u32(rng, n, max_val);
    int bw = bit_width_for(max_val);

    auto encoded = RleEncoder::encode(values.data(), values.size(), bw);
    REQUIRE(!encoded.empty());

    auto decoded = RleDecoder::decode(encoded.data(), encoded.size(), bw, values.size());
    REQUIRE(decoded.size() == values.size());
    REQUIRE(decoded == values);
}

// ===========================================================================
// Property: Delta encode/decode is identity (int64)
// ===========================================================================
TEST_CASE("Property: Delta encode/decode identity (int64)", "[property][encoding][delta]") {
    auto seed = GENERATE(42u, 137u, 2718u, 31415u, 99999u);
    auto rng = make_rng(seed);
    auto n = GENERATE(1u, 10u, 100u, 500u);

    CAPTURE(seed, n);

    auto values = random_i64(rng, n);

    auto encoded = delta::encode_int64(values.data(), values.size());
    REQUIRE(!encoded.empty());

    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());
    REQUIRE(decoded.size() == values.size());
    REQUIRE(decoded == values);
}

// ===========================================================================
// Property: BYTE_STREAM_SPLIT encode/decode is identity (double)
// ===========================================================================
TEST_CASE("Property: BSS encode/decode identity (double)", "[property][encoding][bss]") {
    auto seed = GENERATE(42u, 137u, 2718u, 31415u, 99999u);
    auto rng = make_rng(seed);
    auto n = GENERATE(1u, 10u, 100u, 500u);

    CAPTURE(seed, n);

    auto values = random_f64(rng, n);

    auto encoded = byte_stream_split::encode_double(values.data(), values.size());
    REQUIRE(!encoded.empty());

    auto decoded = byte_stream_split::decode_double(encoded.data(), encoded.size(), values.size());
    REQUIRE(decoded.size() == values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(std::memcmp(&decoded[i], &values[i], sizeof(double)) == 0);
    }
}

// ===========================================================================
// Property: Parquet write/read roundtrip is identity (int64 column)
// ===========================================================================
TEST_CASE("Property: Parquet write/read roundtrip (int64)", "[property][roundtrip]") {
    auto seed = GENERATE(42u, 2718u, 99999u);
    auto rng = make_rng(seed);
    auto n = GENERATE(10u, 100u, 500u);

    CAPTURE(seed, n);

    auto values = random_i64(rng, n);

    const std::string path = "/tmp/signet_prop_roundtrip_i64_" +
                             std::to_string(seed) + "_" + std::to_string(n) + ".parquet";

    auto schema = Schema::build("prop_test", Column<int64_t>("vals"));

    // Write
    {
        auto wr = ParquetWriter::open(path, schema);
        REQUIRE(wr.has_value());
        auto r1 = wr->write_column(0, values.data(), values.size());
        REQUIRE(r1.has_value());
        auto r2 = wr->flush_row_group();
        REQUIRE(r2.has_value());
        auto r3 = wr->close();
        REQUIRE(r3.has_value());
    }

    // Read and verify identity
    {
        auto rdr = ParquetReader::open(path);
        REQUIRE(rdr.has_value());
        auto col = rdr->read_column<int64_t>(0, 0);
        REQUIRE(col.has_value());
        REQUIRE(col->size() == values.size());
        REQUIRE(*col == values);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ===========================================================================
// Property: Parquet write/read roundtrip is identity (double column)
// ===========================================================================
TEST_CASE("Property: Parquet write/read roundtrip (double)", "[property][roundtrip]") {
    auto seed = GENERATE(42u, 2718u, 99999u);
    auto rng = make_rng(seed);
    auto n = GENERATE(10u, 100u, 500u);

    CAPTURE(seed, n);

    auto values = random_f64(rng, n);

    const std::string path = "/tmp/signet_prop_roundtrip_f64_" +
                             std::to_string(seed) + "_" + std::to_string(n) + ".parquet";

    auto schema = Schema::build("prop_test", Column<double>("vals"));

    // Write
    {
        auto wr = ParquetWriter::open(path, schema);
        REQUIRE(wr.has_value());
        auto r1 = wr->write_column(0, values.data(), values.size());
        REQUIRE(r1.has_value());
        auto r2 = wr->flush_row_group();
        REQUIRE(r2.has_value());
        auto r3 = wr->close();
        REQUIRE(r3.has_value());
    }

    // Read and verify identity
    {
        auto rdr = ParquetReader::open(path);
        REQUIRE(rdr.has_value());
        auto col = rdr->read_column<double>(0, 0);
        REQUIRE(col.has_value());
        REQUIRE(col->size() == values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            REQUIRE(std::memcmp(&(*col)[i], &values[i], sizeof(double)) == 0);
        }
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ===========================================================================
// Property: Parquet write/read roundtrip is identity (string column)
// ===========================================================================
TEST_CASE("Property: Parquet write/read roundtrip (strings)", "[property][roundtrip]") {
    auto seed = GENERATE(42u, 2718u);
    auto rng = make_rng(seed);

    constexpr const char* WORDS[] = {
        "foo", "bar", "baz", "qux", "quux", "corge", "grault", "garply"
    };
    auto n = GENERATE(10u, 100u);
    CAPTURE(seed, n);

    std::uniform_int_distribution<size_t> word_dist(0, 7);
    std::vector<std::string> values(n);
    for (auto& v : values) v = WORDS[word_dist(rng)];

    const std::string path = "/tmp/signet_prop_roundtrip_str_" +
                             std::to_string(seed) + "_" + std::to_string(n) + ".parquet";

    auto schema = Schema::build("prop_test", Column<std::string>("vals"));

    {
        auto wr = ParquetWriter::open(path, schema);
        REQUIRE(wr.has_value());
        auto r1 = wr->write_column(0, values.data(), values.size());
        REQUIRE(r1.has_value());
        auto r2 = wr->flush_row_group();
        REQUIRE(r2.has_value());
        auto r3 = wr->close();
        REQUIRE(r3.has_value());
    }

    {
        auto rdr = ParquetReader::open(path);
        REQUIRE(rdr.has_value());
        auto col = rdr->read_column<std::string>(0, 0);
        REQUIRE(col.has_value());
        REQUIRE(col->size() == values.size());
        REQUIRE(*col == values);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ===========================================================================
// Property: Parquet write/read roundtrip is identity (int32 column)
// ===========================================================================
TEST_CASE("Property: Parquet write/read roundtrip (int32)", "[property][roundtrip]") {
    auto seed = GENERATE(42u, 2718u, 99999u);
    auto rng = make_rng(seed);
    auto n = GENERATE(10u, 100u, 500u);

    CAPTURE(seed, n);

    std::uniform_int_distribution<int32_t> dist(-100'000, 100'000);
    std::vector<int32_t> values(n);
    for (auto& v : values) v = dist(rng);

    const std::string path = "/tmp/signet_prop_roundtrip_i32_" +
                             std::to_string(seed) + "_" + std::to_string(n) + ".parquet";

    auto schema = Schema::build("prop_test", Column<int32_t>("vals"));

    {
        auto wr = ParquetWriter::open(path, schema);
        REQUIRE(wr.has_value());
        auto r1 = wr->write_column(0, values.data(), values.size());
        REQUIRE(r1.has_value());
        auto r2 = wr->flush_row_group();
        REQUIRE(r2.has_value());
        auto r3 = wr->close();
        REQUIRE(r3.has_value());
    }

    {
        auto rdr = ParquetReader::open(path);
        REQUIRE(rdr.has_value());
        auto col = rdr->read_column<int32_t>(0, 0);
        REQUIRE(col.has_value());
        REQUIRE(col->size() == values.size());
        REQUIRE(*col == values);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
