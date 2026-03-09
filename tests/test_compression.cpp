// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
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
// Snappy Tests
// ===================================================================

TEST_CASE("Snappy compress/decompress roundtrip", "[compression][snappy]") {
    // Create 1000 bytes of mixed content
    std::vector<uint8_t> data(1000);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>((i * 7 + 13) % 256);
    }

    register_snappy_codec();

    auto compressed = compress(Compression::SNAPPY, data.data(), data.size());
    REQUIRE(compressed.has_value());
    REQUIRE(!compressed->empty());

    auto decompressed = decompress(Compression::SNAPPY,
                                    compressed->data(), compressed->size(),
                                    data.size());
    REQUIRE(decompressed.has_value());
    REQUIRE(decompressed->size() == data.size());
    REQUIRE(*decompressed == data);
}

TEST_CASE("Snappy compresses repetitive data", "[compression][snappy]") {
    // Create highly repetitive data: "BTCUSD" repeated 500 times
    std::string pattern = "BTCUSD";
    std::vector<uint8_t> data;
    data.reserve(pattern.size() * 500);
    for (int i = 0; i < 500; ++i) {
        data.insert(data.end(), pattern.begin(), pattern.end());
    }
    size_t original_size = data.size(); // 3000 bytes

    register_snappy_codec();

    auto compressed = compress(Compression::SNAPPY, data.data(), data.size());
    REQUIRE(compressed.has_value());
    REQUIRE(compressed->size() < original_size);

    // Verify roundtrip
    auto decompressed = decompress(Compression::SNAPPY,
                                    compressed->data(), compressed->size(),
                                    data.size());
    REQUIRE(decompressed.has_value());
    REQUIRE(*decompressed == data);
}

TEST_CASE("Snappy handles empty data", "[compression][snappy]") {
    register_snappy_codec();

    std::vector<uint8_t> empty_data;

    auto compressed = compress(Compression::SNAPPY,
                                empty_data.data(), empty_data.size());
    REQUIRE(compressed.has_value());

    auto decompressed = decompress(Compression::SNAPPY,
                                    compressed->data(), compressed->size(),
                                    0);
    REQUIRE(decompressed.has_value());
    REQUIRE(decompressed->empty());
}

TEST_CASE("Snappy direct codec", "[compression][snappy]") {
    SnappyCodec codec;

    std::string test_str = "The quick brown fox jumps over the lazy dog!";
    const auto* data = reinterpret_cast<const uint8_t*>(test_str.data());
    size_t size = test_str.size();

    auto compressed = codec.compress(data, size);
    REQUIRE(compressed.has_value());
    REQUIRE(!compressed->empty());

    auto decompressed = codec.decompress(compressed->data(), compressed->size(),
                                          size);
    REQUIRE(decompressed.has_value());
    REQUIRE(decompressed->size() == size);

    std::string result(reinterpret_cast<const char*>(decompressed->data()),
                       decompressed->size());
    REQUIRE(result == test_str);
}

// ===================================================================
// Codec Registry Tests
// ===================================================================

TEST_CASE("Codec registry has Snappy", "[compression][registry]") {
    register_snappy_codec();
#ifdef SIGNET_HAS_ZSTD
    register_zstd_codec();
#endif
#ifdef SIGNET_HAS_LZ4
    register_lz4_codec();
#endif
#ifdef SIGNET_HAS_GZIP
    register_gzip_codec();
#endif

    REQUIRE(CodecRegistry::instance().has(Compression::SNAPPY) == true);
    REQUIRE(CodecRegistry::instance().has(Compression::UNCOMPRESSED) == true);
#ifdef SIGNET_HAS_ZSTD
    REQUIRE(CodecRegistry::instance().has(Compression::ZSTD) == true);
#else
    REQUIRE(CodecRegistry::instance().has(Compression::ZSTD) == false);
#endif
#ifdef SIGNET_HAS_LZ4
    REQUIRE(CodecRegistry::instance().has(Compression::LZ4_RAW) == true);
#endif
#ifdef SIGNET_HAS_GZIP
    REQUIRE(CodecRegistry::instance().has(Compression::GZIP) == true);
#endif
}

TEST_CASE("Uncompressed passthrough", "[compression]") {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03, 0x04, 0x05,
                                  0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

    auto result = compress(Compression::UNCOMPRESSED, data.data(), data.size());
    REQUIRE(result.has_value());
    REQUIRE(result->size() == data.size());
    REQUIRE(*result == data);

    // Decompress passthrough
    auto dec_result = decompress(Compression::UNCOMPRESSED,
                                  data.data(), data.size(), data.size());
    REQUIRE(dec_result.has_value());
    REQUIRE(*dec_result == data);
}

// ===================================================================
// Full Pipeline Tests (encoding + compression)
// ===================================================================

TEST_CASE("Write and read with Snappy compression", "[compression][roundtrip]") {
    TempFile tmp("signet_test_snappy_pipeline.parquet");

    register_snappy_codec();

    auto schema = Schema::builder("trades")
        .column<int64_t>("timestamp")
        .column<double>("price")
        .column<std::string>("symbol")
        .build();

    ParquetWriter::Options opts;
    opts.compression = Compression::SNAPPY;

    constexpr size_t NUM_ROWS = 100;

    // Generate test data
    struct Row {
        int64_t     timestamp;
        double      price;
        std::string symbol;
    };

    std::vector<Row> expected_rows;
    expected_rows.reserve(NUM_ROWS);

    const std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "SOLUSD"};

    for (size_t i = 0; i < NUM_ROWS; ++i) {
        Row row;
        row.timestamp = 1700000000000LL + static_cast<int64_t>(i) * 1000LL;
        row.price     = 50000.0 + static_cast<double>(i) * 0.5;
        row.symbol    = symbols[i % symbols.size()];
        expected_rows.push_back(row);
    }

    // Write with Snappy compression
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (const auto& row : expected_rows) {
            auto r = writer.write_row({
                std::to_string(row.timestamp),
                std::to_string(row.price),
                row.symbol
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read back and verify
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));
        REQUIRE(reader.schema().num_columns() == 3);

        auto all_result = reader.read_all();
        REQUIRE(all_result.has_value());
        const auto& rows = all_result.value();
        REQUIRE(rows.size() == NUM_ROWS);

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            const auto& row = rows[i];
            const auto& exp = expected_rows[i];

            // Timestamp
            REQUIRE(row[0] == std::to_string(exp.timestamp));

            // Price
            double read_price = std::stod(row[1]);
            REQUIRE(std::abs(read_price - exp.price) < 1e-6);

            // Symbol
            REQUIRE(row[2] == exp.symbol);
        }
    }
}

TEST_CASE("Write with auto encoding + Snappy", "[compression][roundtrip]") {
    TempFile tmp("signet_test_auto_enc_snappy.parquet");

    register_snappy_codec();

    // Schema: timestamp (INT64), price (DOUBLE), symbol (STRING with low cardinality)
    auto schema = Schema::builder("tick_data")
        .column<int64_t>("timestamp", LogicalType::TIMESTAMP_NS)
        .column<double>("price")
        .column<std::string>("symbol")
        .build();

    ParquetWriter::Options opts;
    opts.auto_encoding = true;
    opts.compression   = Compression::SNAPPY;

    constexpr size_t NUM_ROWS = 200;

    // Generate test data
    struct Row {
        int64_t     timestamp;
        double      price;
        std::string symbol;
    };

    std::vector<Row> expected_rows;
    expected_rows.reserve(NUM_ROWS);

    // Low cardinality symbols to trigger dictionary encoding
    const std::vector<std::string> symbols = {"BTC", "ETH", "SOL"};

    for (size_t i = 0; i < NUM_ROWS; ++i) {
        Row row;
        // Monotonic timestamps -> good for DELTA encoding
        row.timestamp = 1700000000000000000LL + static_cast<int64_t>(i) * 1000000LL;
        // Financial prices -> good for BYTE_STREAM_SPLIT
        row.price     = 50000.123 + static_cast<double>(i) * 0.01;
        // Low cardinality -> good for DICTIONARY encoding
        row.symbol    = symbols[i % symbols.size()];
        expected_rows.push_back(row);
    }

    // Write with auto encoding + Snappy compression
    // This exercises: DELTA for timestamps, BSS for prices, DICT for symbols
    {
        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (const auto& row : expected_rows) {
            auto r = writer.write_row({
                std::to_string(row.timestamp),
                std::to_string(row.price),
                row.symbol
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read back and verify all values match
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));
        REQUIRE(reader.schema().num_columns() == 3);

        // Read timestamp column as typed int64
        auto ts_result = reader.read_column<int64_t>(0, 0);
        REQUIRE(ts_result.has_value());
        const auto& timestamps = ts_result.value();
        REQUIRE(timestamps.size() == NUM_ROWS);
        for (size_t i = 0; i < NUM_ROWS; ++i) {
            REQUIRE(timestamps[i] == expected_rows[i].timestamp);
        }

        // Read price column as typed double
        auto price_result = reader.read_column<double>(0, 1);
        REQUIRE(price_result.has_value());
        const auto& prices = price_result.value();
        REQUIRE(prices.size() == NUM_ROWS);
        for (size_t i = 0; i < NUM_ROWS; ++i) {
            REQUIRE(std::abs(prices[i] - expected_rows[i].price) < 1e-6);
        }

        // Read symbol column as typed string
        auto sym_result = reader.read_column<std::string>(0, 2);
        REQUIRE(sym_result.has_value());
        const auto& symbols_read = sym_result.value();
        REQUIRE(symbols_read.size() == NUM_ROWS);
        for (size_t i = 0; i < NUM_ROWS; ++i) {
            REQUIRE(symbols_read[i] == expected_rows[i].symbol);
        }
    }
}
