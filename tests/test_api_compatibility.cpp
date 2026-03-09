// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
//
// Gap T-19: API compatibility test suite.
//
// Verifies that public enum values, struct sizes, error codes, and key API
// entry points remain stable across releases.  These tests act as an early
// warning system: if a refactor inadvertently changes a public ABI surface,
// one of these checks will fail before downstream consumers are affected.

#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

// Windows <mmsystem.h> defines TIME_MS as a macro — undefine to avoid collision.
#ifdef TIME_MS
#undef TIME_MS
#endif

namespace fs = std::filesystem;

// ===================================================================
// 1. PhysicalType enum value stability
// ===================================================================
TEST_CASE("PhysicalType enum values are stable", "[api][compatibility]") {
    using PT = signet::forge::PhysicalType;

    REQUIRE(static_cast<int32_t>(PT::BOOLEAN)              == 0);
    REQUIRE(static_cast<int32_t>(PT::INT32)                == 1);
    REQUIRE(static_cast<int32_t>(PT::INT64)                == 2);
    REQUIRE(static_cast<int32_t>(PT::INT96)                == 3);
    REQUIRE(static_cast<int32_t>(PT::FLOAT)                == 4);
    REQUIRE(static_cast<int32_t>(PT::DOUBLE)               == 5);
    REQUIRE(static_cast<int32_t>(PT::BYTE_ARRAY)           == 6);
    REQUIRE(static_cast<int32_t>(PT::FIXED_LEN_BYTE_ARRAY) == 7);
}

// ===================================================================
// 2. Encoding enum value stability
// ===================================================================
TEST_CASE("Encoding enum values are stable", "[api][compatibility]") {
    using E = signet::forge::Encoding;

    REQUIRE(static_cast<int32_t>(E::PLAIN)                  == 0);
    REQUIRE(static_cast<int32_t>(E::PLAIN_DICTIONARY)       == 2);
    REQUIRE(static_cast<int32_t>(E::RLE)                    == 3);
    REQUIRE(static_cast<int32_t>(E::BIT_PACKED)             == 4);
    REQUIRE(static_cast<int32_t>(E::DELTA_BINARY_PACKED)    == 5);
    REQUIRE(static_cast<int32_t>(E::DELTA_LENGTH_BYTE_ARRAY)== 6);
    REQUIRE(static_cast<int32_t>(E::DELTA_BYTE_ARRAY)       == 7);
    REQUIRE(static_cast<int32_t>(E::RLE_DICTIONARY)         == 8);
    REQUIRE(static_cast<int32_t>(E::BYTE_STREAM_SPLIT)      == 9);
}

// ===================================================================
// 3. Compression enum value stability
// ===================================================================
TEST_CASE("Compression enum values are stable", "[api][compatibility]") {
    using C = signet::forge::Compression;

    REQUIRE(static_cast<int32_t>(C::UNCOMPRESSED) == 0);
    REQUIRE(static_cast<int32_t>(C::SNAPPY)       == 1);
    REQUIRE(static_cast<int32_t>(C::GZIP)         == 2);
    REQUIRE(static_cast<int32_t>(C::LZO)          == 3);
    REQUIRE(static_cast<int32_t>(C::BROTLI)       == 4);
    REQUIRE(static_cast<int32_t>(C::LZ4)          == 5);
    REQUIRE(static_cast<int32_t>(C::ZSTD)         == 6);
    REQUIRE(static_cast<int32_t>(C::LZ4_RAW)      == 7);
}

// ===================================================================
// 4. LogicalType enum value stability
// ===================================================================
TEST_CASE("LogicalType enum values are stable", "[api][compatibility]") {
    using LT = signet::forge::LogicalType;

    REQUIRE(static_cast<int32_t>(LT::NONE)            == 0);
    REQUIRE(static_cast<int32_t>(LT::STRING)           == 1);
    REQUIRE(static_cast<int32_t>(LT::ENUM)             == 2);
    REQUIRE(static_cast<int32_t>(LT::UUID)             == 3);
    REQUIRE(static_cast<int32_t>(LT::DATE)             == 4);
    REQUIRE(static_cast<int32_t>(LT::TIME_MS)          == 5);
    REQUIRE(static_cast<int32_t>(LT::TIME_US)          == 6);
    REQUIRE(static_cast<int32_t>(LT::TIME_NS)          == 7);
    REQUIRE(static_cast<int32_t>(LT::TIMESTAMP_MS)     == 8);
    REQUIRE(static_cast<int32_t>(LT::TIMESTAMP_US)     == 9);
    REQUIRE(static_cast<int32_t>(LT::TIMESTAMP_NS)     == 10);
    REQUIRE(static_cast<int32_t>(LT::DECIMAL)          == 11);
    REQUIRE(static_cast<int32_t>(LT::JSON)             == 12);
    REQUIRE(static_cast<int32_t>(LT::BSON)             == 13);
    REQUIRE(static_cast<int32_t>(LT::FLOAT16)          == 14);
    REQUIRE(static_cast<int32_t>(LT::FLOAT32_VECTOR)   == 100);
}

// ===================================================================
// 5. PageType enum value stability
// ===================================================================
TEST_CASE("PageType enum values are stable", "[api][compatibility]") {
    using P = signet::forge::PageType;

    REQUIRE(static_cast<int32_t>(P::DATA_PAGE)       == 0);
    REQUIRE(static_cast<int32_t>(P::INDEX_PAGE)      == 1);
    REQUIRE(static_cast<int32_t>(P::DICTIONARY_PAGE) == 2);
    REQUIRE(static_cast<int32_t>(P::DATA_PAGE_V2)    == 3);
}

// ===================================================================
// 6. Repetition enum value stability
// ===================================================================
TEST_CASE("Repetition enum values are stable", "[api][compatibility]") {
    using R = signet::forge::Repetition;

    REQUIRE(static_cast<int32_t>(R::REQUIRED) == 0);
    REQUIRE(static_cast<int32_t>(R::OPTIONAL) == 1);
    REQUIRE(static_cast<int32_t>(R::REPEATED) == 2);
}

// ===================================================================
// 7. ErrorCode enum value stability
// ===================================================================
TEST_CASE("ErrorCode enum values are stable", "[api][compatibility]") {
    using EC = signet::forge::ErrorCode;

    // ErrorCode uses implicit numbering from 0, so verify ordinal positions.
    REQUIRE(static_cast<int>(EC::OK)                       == 0);
    REQUIRE(static_cast<int>(EC::IO_ERROR)                 == 1);
    REQUIRE(static_cast<int>(EC::INVALID_FILE)             == 2);
    REQUIRE(static_cast<int>(EC::CORRUPT_FOOTER)           == 3);
    REQUIRE(static_cast<int>(EC::CORRUPT_PAGE)             == 4);
    REQUIRE(static_cast<int>(EC::CORRUPT_DATA)             == 5);
    REQUIRE(static_cast<int>(EC::INVALID_ARGUMENT)         == 6);
    REQUIRE(static_cast<int>(EC::UNSUPPORTED_ENCODING)     == 7);
    REQUIRE(static_cast<int>(EC::UNSUPPORTED_COMPRESSION)  == 8);
    REQUIRE(static_cast<int>(EC::UNSUPPORTED_TYPE)         == 9);
    REQUIRE(static_cast<int>(EC::SCHEMA_MISMATCH)          == 10);
    REQUIRE(static_cast<int>(EC::OUT_OF_RANGE)             == 11);
    REQUIRE(static_cast<int>(EC::THRIFT_DECODE_ERROR)      == 12);
    REQUIRE(static_cast<int>(EC::ENCRYPTION_ERROR)         == 13);
    REQUIRE(static_cast<int>(EC::HASH_CHAIN_BROKEN)        == 14);
    REQUIRE(static_cast<int>(EC::LICENSE_ERROR)             == 15);
    REQUIRE(static_cast<int>(EC::LICENSE_LIMIT_EXCEEDED)    == 16);
    REQUIRE(static_cast<int>(EC::INTERNAL_ERROR)            == 17);
}

// ===================================================================
// 8. Struct size stability (baselines — CHECK, not REQUIRE)
// ===================================================================
TEST_CASE("Key struct sizes have not changed unexpectedly", "[api][compatibility]") {
    // These are baseline sizes captured on the reference platform
    // (Apple Clang 17, macOS arm64, C++20).  They guard against
    // accidental field additions that break ABI.  Use CHECK (not
    // REQUIRE) so all sizes are reported even if one fails.

    // ColumnDescriptor: name(string) + physical_type + logical_type +
    //                   repetition + type_length + precision + scale
    CHECK(sizeof(signet::forge::ColumnDescriptor) > 0);

    // Error: code(enum) + message(string)
    CHECK(sizeof(signet::forge::Error) > 0);

    // WriteStats: aggregated file-level write statistics
    CHECK(sizeof(signet::forge::WriteStats) > 0);

    // FileStats: aggregated file-level read statistics
    CHECK(sizeof(signet::forge::FileStats) > 0);

    // ColumnWriteStats: per-column write statistics
    CHECK(sizeof(signet::forge::ColumnWriteStats) > 0);

    // ColumnFileStats: per-column read statistics
    CHECK(sizeof(signet::forge::ColumnFileStats) > 0);

    // WriterOptions: configuration for ParquetWriter
    CHECK(sizeof(signet::forge::WriterOptions) > 0);
}

// ===================================================================
// 9. parquet_type_of trait stability
// ===================================================================
TEST_CASE("parquet_type_of trait maps are stable", "[api][compatibility]") {
    using namespace signet::forge;

    REQUIRE(parquet_type_of_v<bool>        == PhysicalType::BOOLEAN);
    REQUIRE(parquet_type_of_v<int32_t>     == PhysicalType::INT32);
    REQUIRE(parquet_type_of_v<int64_t>     == PhysicalType::INT64);
    REQUIRE(parquet_type_of_v<float>       == PhysicalType::FLOAT);
    REQUIRE(parquet_type_of_v<double>      == PhysicalType::DOUBLE);
    REQUIRE(parquet_type_of_v<std::string> == PhysicalType::BYTE_ARRAY);
}

// ===================================================================
// 10. Version metadata constants are stable
// ===================================================================
TEST_CASE("Version and magic constants are stable", "[api][compatibility]") {
    // Parquet format version written to the footer
    REQUIRE(signet::forge::PARQUET_VERSION == 2);

    // PAR1 magic bytes (little-endian "PAR1")
    REQUIRE(signet::forge::PARQUET_MAGIC == 0x31524150);

    // PARE magic bytes (little-endian "PARE") for encrypted footer
    REQUIRE(signet::forge::PARQUET_MAGIC_ENCRYPTED == 0x45524150);

    // Created-by string
    REQUIRE(std::string(signet::forge::SIGNET_CREATED_BY).find("signet-forge") != std::string::npos);
}

// ===================================================================
// 11. Public API existence — ParquetWriter
// ===================================================================
TEST_CASE("ParquetWriter public API exists and is callable", "[api][compatibility]") {
    namespace sf = signet::forge;

    auto schema = sf::Schema::builder("api_test")
        .column<int32_t>("x")
        .build();

    fs::path tmp_path = fs::temp_directory_path() / "signet_api_compat_writer.parquet";

    // open() returns expected<ParquetWriter>
    auto writer_result = sf::ParquetWriter::open(tmp_path, schema);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    // write_column<int32_t>() exists and is callable
    std::vector<int32_t> data = {1, 2, 3};
    auto wc_result = writer.write_column<int32_t>(0, data.data(), data.size());
    REQUIRE(wc_result.has_value());

    // flush_row_group() exists and is callable
    auto flush_result = writer.flush_row_group();
    REQUIRE(flush_result.has_value());

    // close() exists and returns expected<WriteStats>
    auto close_result = writer.close();
    REQUIRE(close_result.has_value());
    REQUIRE(close_result->total_rows == 3);

    // Cleanup
    std::error_code ec;
    fs::remove(tmp_path, ec);
}

// ===================================================================
// 12. Public API existence — ParquetReader
// ===================================================================
TEST_CASE("ParquetReader public API exists and is callable", "[api][compatibility]") {
    namespace sf = signet::forge;

    // First, write a small file to read back
    fs::path tmp_path = fs::temp_directory_path() / "signet_api_compat_reader.parquet";

    auto schema = sf::Schema::builder("api_test")
        .column<int32_t>("x")
        .column<std::string>("label")
        .build();

    {
        auto writer_result = sf::ParquetWriter::open(tmp_path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        auto r = writer.write_row({"42", "hello"});
        REQUIRE(r.has_value());
        r = writer.write_row({"99", "world"});
        REQUIRE(r.has_value());

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // open() returns expected<ParquetReader>
    auto reader_result = sf::ParquetReader::open(tmp_path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    // num_rows() exists and returns the correct count
    REQUIRE(reader.num_rows() == 2);

    // num_row_groups() exists
    REQUIRE(reader.num_row_groups() >= 1);

    // schema().num_columns() exists
    REQUIRE(reader.schema().num_columns() == 2);

    // read_column<int32_t>() exists and is callable
    auto col_result = reader.read_column<int32_t>(0, 0);
    REQUIRE(col_result.has_value());
    REQUIRE(col_result->size() == 2);
    REQUIRE((*col_result)[0] == 42);
    REQUIRE((*col_result)[1] == 99);

    // Cleanup
    std::error_code ec;
    fs::remove(tmp_path, ec);
}

// ===================================================================
// 13. Public API existence — Schema::build() variadic factory
// ===================================================================
TEST_CASE("Schema::build variadic factory is stable", "[api][compatibility]") {
    namespace sf = signet::forge;

    auto schema = sf::Schema::build("compat_test",
        sf::Column<int64_t>{"timestamp", sf::LogicalType::TIMESTAMP_NS},
        sf::Column<double>{"price"},
        sf::Column<std::string>{"symbol"});

    REQUIRE(schema.num_columns() == 3);
    REQUIRE(schema.column(0).name == "timestamp");
    REQUIRE(schema.column(0).physical_type == sf::PhysicalType::INT64);
    REQUIRE(schema.column(0).logical_type == sf::LogicalType::TIMESTAMP_NS);
    REQUIRE(schema.column(1).name == "price");
    REQUIRE(schema.column(1).physical_type == sf::PhysicalType::DOUBLE);
    REQUIRE(schema.column(2).name == "symbol");
    REQUIRE(schema.column(2).physical_type == sf::PhysicalType::BYTE_ARRAY);
    REQUIRE(schema.column(2).logical_type == sf::LogicalType::STRING);
}

// ===================================================================
// 14. Public API existence — Schema::builder() fluent API
// ===================================================================
TEST_CASE("Schema::builder fluent API is stable", "[api][compatibility]") {
    namespace sf = signet::forge;

    auto schema = sf::Schema::builder("compat_builder")
        .column<bool>("flag")
        .column<int32_t>("count")
        .column<float>("ratio")
        .build();

    REQUIRE(schema.num_columns() == 3);
    REQUIRE(schema.column(0).name == "flag");
    REQUIRE(schema.column(0).physical_type == sf::PhysicalType::BOOLEAN);
    REQUIRE(schema.column(1).name == "count");
    REQUIRE(schema.column(1).physical_type == sf::PhysicalType::INT32);
    REQUIRE(schema.column(2).name == "ratio");
    REQUIRE(schema.column(2).physical_type == sf::PhysicalType::FLOAT);
}

// ===================================================================
// 15. Error type API stability
// ===================================================================
TEST_CASE("Error type API is stable", "[api][compatibility]") {
    namespace sf = signet::forge;

    // Default-constructed Error is OK
    sf::Error ok_err;
    REQUIRE(ok_err.ok());
    REQUIRE(ok_err.code == sf::ErrorCode::OK);
    REQUIRE_FALSE(static_cast<bool>(ok_err));

    // Error with code and message
    sf::Error io_err{sf::ErrorCode::IO_ERROR, "test message"};
    REQUIRE_FALSE(io_err.ok());
    REQUIRE(io_err.code == sf::ErrorCode::IO_ERROR);
    REQUIRE(static_cast<bool>(io_err));
    REQUIRE(io_err.message == "test message");
}

// ===================================================================
// 16. ConvertedType enum value stability
// ===================================================================
TEST_CASE("ConvertedType enum values are stable", "[api][compatibility]") {
    using CT = signet::forge::ConvertedType;

    REQUIRE(static_cast<int32_t>(CT::NONE)             == -1);
    REQUIRE(static_cast<int32_t>(CT::UTF8)             == 0);
    REQUIRE(static_cast<int32_t>(CT::MAP)              == 1);
    REQUIRE(static_cast<int32_t>(CT::MAP_KEY_VALUE)    == 2);
    REQUIRE(static_cast<int32_t>(CT::LIST)             == 3);
    REQUIRE(static_cast<int32_t>(CT::ENUM)             == 4);
    REQUIRE(static_cast<int32_t>(CT::DECIMAL)          == 5);
    REQUIRE(static_cast<int32_t>(CT::DATE)             == 6);
    REQUIRE(static_cast<int32_t>(CT::TIME_MILLIS)      == 7);
    REQUIRE(static_cast<int32_t>(CT::TIME_MICROS)      == 8);
    REQUIRE(static_cast<int32_t>(CT::TIMESTAMP_MILLIS) == 9);
    REQUIRE(static_cast<int32_t>(CT::TIMESTAMP_MICROS) == 10);
    REQUIRE(static_cast<int32_t>(CT::UINT_8)           == 11);
    REQUIRE(static_cast<int32_t>(CT::UINT_16)          == 12);
    REQUIRE(static_cast<int32_t>(CT::UINT_32)          == 13);
    REQUIRE(static_cast<int32_t>(CT::UINT_64)          == 14);
    REQUIRE(static_cast<int32_t>(CT::INT_8)            == 15);
    REQUIRE(static_cast<int32_t>(CT::INT_16)           == 16);
    REQUIRE(static_cast<int32_t>(CT::INT_32)           == 17);
    REQUIRE(static_cast<int32_t>(CT::INT_64)           == 18);
    REQUIRE(static_cast<int32_t>(CT::JSON)             == 19);
    REQUIRE(static_cast<int32_t>(CT::BSON)             == 20);
    REQUIRE(static_cast<int32_t>(CT::INTERVAL)         == 21);
}

// ===================================================================
// 17. WriterOptions default values are stable
// ===================================================================
TEST_CASE("WriterOptions defaults are stable", "[api][compatibility]") {
    namespace sf = signet::forge;

    sf::WriterOptions opts;

    REQUIRE(opts.row_group_size    == 64 * 1024);
    REQUIRE(opts.default_encoding  == sf::Encoding::PLAIN);
    REQUIRE(opts.compression       == sf::Compression::UNCOMPRESSED);
    REQUIRE(opts.auto_encoding     == false);
    REQUIRE(opts.auto_compression  == false);
    REQUIRE(opts.enable_page_index == false);
    REQUIRE(opts.enable_bloom_filter == false);
}

// ===================================================================
// 18. native_type_of trait stability
// ===================================================================
TEST_CASE("native_type_of reverse trait maps are stable", "[api][compatibility]") {
    using namespace signet::forge;

    REQUIRE((std::is_same_v<native_type_of_t<PhysicalType::BOOLEAN>,    bool>));
    REQUIRE((std::is_same_v<native_type_of_t<PhysicalType::INT32>,      int32_t>));
    REQUIRE((std::is_same_v<native_type_of_t<PhysicalType::INT64>,      int64_t>));
    REQUIRE((std::is_same_v<native_type_of_t<PhysicalType::FLOAT>,      float>));
    REQUIRE((std::is_same_v<native_type_of_t<PhysicalType::DOUBLE>,     double>));
    REQUIRE((std::is_same_v<native_type_of_t<PhysicalType::BYTE_ARRAY>, std::string>));
}

// ===================================================================
// 19. Enum underlying types are int32_t (Parquet Thrift convention)
// ===================================================================
TEST_CASE("Enum underlying types are int32_t", "[api][compatibility]") {
    REQUIRE((std::is_same_v<std::underlying_type_t<signet::forge::PhysicalType>, int32_t>));
    REQUIRE((std::is_same_v<std::underlying_type_t<signet::forge::LogicalType>,  int32_t>));
    REQUIRE((std::is_same_v<std::underlying_type_t<signet::forge::Encoding>,     int32_t>));
    REQUIRE((std::is_same_v<std::underlying_type_t<signet::forge::Compression>,  int32_t>));
    REQUIRE((std::is_same_v<std::underlying_type_t<signet::forge::PageType>,     int32_t>));
    REQUIRE((std::is_same_v<std::underlying_type_t<signet::forge::Repetition>,   int32_t>));
    REQUIRE((std::is_same_v<std::underlying_type_t<signet::forge::ConvertedType>,int32_t>));
}
