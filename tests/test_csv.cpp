// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include <filesystem>
#include <fstream>
#include <cmath>
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

// ---------------------------------------------------------------------------
// Helper: write a string to a file
// ---------------------------------------------------------------------------
static void write_text_file(const fs::path& path, const std::string& content) {
    std::ofstream ofs(path, std::ios::trunc);
    REQUIRE(ofs.is_open());
    ofs << content;
    ofs.close();
}

// ===================================================================
// 1. Basic CSV conversion
// ===================================================================
TEST_CASE("Basic CSV conversion", "[csv]") {
    TempFile csv_file("signet_csv_basic.csv");
    TempFile pq_file("signet_csv_basic.parquet");

    // Write CSV
    write_text_file(csv_file.path,
        "id,name,price\n"
        "1,Alice,100.5\n"
        "2,Bob,200.75\n"
        "3,Charlie,300.0\n"
        "4,Diana,400.25\n"
        "5,Eve,500.99\n"
    );

    // Convert
    auto convert_result = ParquetWriter::csv_to_parquet(csv_file.path, pq_file.path);
    REQUIRE(convert_result.has_value());

    // Read and verify
    auto reader_result = ParquetReader::open(pq_file.path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    REQUIRE(reader.num_rows() == 5);
    REQUIRE(reader.schema().num_columns() == 3);

    // Verify detected types
    REQUIRE(reader.schema().column(0).name == "id");
    REQUIRE(reader.schema().column(0).physical_type == PhysicalType::INT64);

    REQUIRE(reader.schema().column(1).name == "name");
    REQUIRE(reader.schema().column(1).physical_type == PhysicalType::BYTE_ARRAY);

    REQUIRE(reader.schema().column(2).name == "price");
    REQUIRE(reader.schema().column(2).physical_type == PhysicalType::DOUBLE);

    // Read all rows and verify values
    auto all_result = reader.read_all();
    REQUIRE(all_result.has_value());
    const auto& rows = all_result.value();

    REQUIRE(rows.size() == 5);

    // Row 0: id=1, name=Alice, price=100.5
    REQUIRE(rows[0][0] == "1");
    REQUIRE(rows[0][1] == "Alice");
    double price0 = std::stod(rows[0][2]);
    REQUIRE(std::abs(price0 - 100.5) < 1e-6);

    // Row 4: id=5, name=Eve, price=500.99
    REQUIRE(rows[4][0] == "5");
    REQUIRE(rows[4][1] == "Eve");
    double price4 = std::stod(rows[4][2]);
    REQUIRE(std::abs(price4 - 500.99) < 1e-6);
}

// ===================================================================
// 2. Auto type detection
// ===================================================================
TEST_CASE("Auto type detection", "[csv]") {
    TempFile csv_file("signet_csv_types.csv");
    TempFile pq_file("signet_csv_types.parquet");

    // Column layout:
    //   col_int    -> pure integers   -> should detect INT64
    //   col_double -> pure doubles    -> should detect DOUBLE
    //   col_mixed  -> mixed types     -> should fall back to STRING
    //   col_bool   -> boolean values  -> should detect BOOLEAN
    write_text_file(csv_file.path,
        "col_int,col_double,col_mixed,col_bool\n"
        "1,1.5,hello,true\n"
        "2,2.7,42,false\n"
        "3,3.14,world,TRUE\n"
        "-10,0.0,123abc,False\n"
        "999,100.001,test,0\n"
    );

    // Convert
    auto convert_result = ParquetWriter::csv_to_parquet(csv_file.path, pq_file.path);
    REQUIRE(convert_result.has_value());

    // Read and verify detected types
    auto reader_result = ParquetReader::open(pq_file.path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    REQUIRE(reader.schema().num_columns() == 4);

    // col_int: all values are valid integers -> INT64
    REQUIRE(reader.schema().column(0).name == "col_int");
    REQUIRE(reader.schema().column(0).physical_type == PhysicalType::INT64);

    // col_double: all values are valid doubles (but not all integers) -> DOUBLE
    REQUIRE(reader.schema().column(1).name == "col_double");
    REQUIRE(reader.schema().column(1).physical_type == PhysicalType::DOUBLE);

    // col_mixed: some values are not numeric or boolean -> STRING
    REQUIRE(reader.schema().column(2).name == "col_mixed");
    REQUIRE(reader.schema().column(2).physical_type == PhysicalType::BYTE_ARRAY);

    // col_bool: all values are valid booleans -> BOOLEAN
    REQUIRE(reader.schema().column(3).name == "col_bool");
    REQUIRE(reader.schema().column(3).physical_type == PhysicalType::BOOLEAN);
}

// ===================================================================
// 3. Quoted CSV fields
// ===================================================================
TEST_CASE("Quoted CSV fields", "[csv]") {
    TempFile csv_file("signet_csv_quoted.csv");
    TempFile pq_file("signet_csv_quoted.parquet");

    // CSV with:
    //  - quoted fields containing commas
    //  - embedded quotes (doubled as "")
    write_text_file(csv_file.path,
        "id,description\n"
        "1,\"hello, world\"\n"
        "2,\"he said \"\"hi\"\"\"\n"
        "3,\"just a normal string\"\n"
        "4,\"commas, everywhere, here\"\n"
    );

    // Convert
    auto convert_result = ParquetWriter::csv_to_parquet(csv_file.path, pq_file.path);
    REQUIRE(convert_result.has_value());

    // Read and verify
    auto reader_result = ParquetReader::open(pq_file.path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    REQUIRE(reader.num_rows() == 4);
    REQUIRE(reader.schema().num_columns() == 2);

    auto all_result = reader.read_all();
    REQUIRE(all_result.has_value());
    const auto& rows = all_result.value();

    // Row 0: field with comma
    REQUIRE(rows[0][0] == "1");
    REQUIRE(rows[0][1] == "hello, world");

    // Row 1: field with embedded quotes
    REQUIRE(rows[1][0] == "2");
    REQUIRE(rows[1][1] == "he said \"hi\"");

    // Row 2: normal quoted string
    REQUIRE(rows[2][0] == "3");
    REQUIRE(rows[2][1] == "just a normal string");

    // Row 3: multiple commas inside quotes
    REQUIRE(rows[3][0] == "4");
    REQUIRE(rows[3][1] == "commas, everywhere, here");
}

// ===================================================================
// 4. Empty CSV (header only, no data rows)
// ===================================================================
TEST_CASE("Empty CSV", "[csv]") {
    TempFile csv_file("signet_csv_empty.csv");
    TempFile pq_file("signet_csv_empty.parquet");

    // CSV with header only, no data rows
    write_text_file(csv_file.path, "id,name,price\n");

    // The csv_to_parquet implementation returns an error for empty CSV
    // (no data rows), so verify the error is reported correctly.
    auto convert_result = ParquetWriter::csv_to_parquet(csv_file.path, pq_file.path);
    REQUIRE_FALSE(convert_result.has_value());
    REQUIRE(convert_result.error().code == ErrorCode::INVALID_FILE);
}

// ===================================================================
// 5. Large CSV
// ===================================================================
TEST_CASE("Large CSV", "[csv]") {
    TempFile csv_file("signet_csv_large.csv");
    TempFile pq_file("signet_csv_large.parquet");

    constexpr size_t NUM_ROWS = 1000;

    // Generate a 1000-row CSV with 5 columns:
    //   row_id (int), price (double), quantity (double), symbol (string), side (string)
    {
        std::ofstream ofs(csv_file.path, std::ios::trunc);
        REQUIRE(ofs.is_open());

        ofs << "row_id,price,quantity,symbol,side\n";

        const std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "SOLUSD", "XRPUSD"};
        const std::vector<std::string> sides   = {"buy", "sell"};

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            ofs << i << ","
                << (50000.0 + static_cast<double>(i) * 0.123) << ","
                << (0.01 + static_cast<double>(i % 100) * 0.001) << ","
                << symbols[i % symbols.size()] << ","
                << sides[i % sides.size()] << "\n";
        }

        ofs.close();
    }

    // Convert
    auto convert_result = ParquetWriter::csv_to_parquet(csv_file.path, pq_file.path);
    REQUIRE(convert_result.has_value());

    // Read and verify
    auto reader_result = ParquetReader::open(pq_file.path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));
    REQUIRE(reader.schema().num_columns() == 5);

    // Verify detected types
    REQUIRE(reader.schema().column(0).physical_type == PhysicalType::INT64);   // row_id
    REQUIRE(reader.schema().column(1).physical_type == PhysicalType::DOUBLE);  // price
    REQUIRE(reader.schema().column(2).physical_type == PhysicalType::DOUBLE);  // quantity
    REQUIRE(reader.schema().column(3).physical_type == PhysicalType::BYTE_ARRAY); // symbol
    REQUIRE(reader.schema().column(4).physical_type == PhysicalType::BYTE_ARRAY); // side

    // Read all data
    auto all_result = reader.read_all();
    REQUIRE(all_result.has_value());
    const auto& rows = all_result.value();
    REQUIRE(rows.size() == NUM_ROWS);

    // Spot-check: first row
    REQUIRE(rows[0][0] == "0");
    double price0 = std::stod(rows[0][1]);
    REQUIRE(std::abs(price0 - 50000.0) < 1e-6);
    REQUIRE(rows[0][3] == "BTCUSD");
    REQUIRE(rows[0][4] == "buy");

    // Spot-check: row 500
    REQUIRE(rows[500][0] == "500");
    double price500 = std::stod(rows[500][1]);
    double expected_price500 = 50000.0 + 500.0 * 0.123;
    REQUIRE(std::abs(price500 - expected_price500) < 0.05);
    REQUIRE(rows[500][3] == "BTCUSD");  // 500 % 4 == 0
    REQUIRE(rows[500][4] == "buy");     // 500 % 2 == 0

    // Spot-check: last row (999)
    REQUIRE(rows[999][0] == "999");
    double price999 = std::stod(rows[999][1]);
    double expected_price999 = 50000.0 + 999.0 * 0.123;
    REQUIRE(std::abs(price999 - expected_price999) < 0.05);
    REQUIRE(rows[999][3] == "XRPUSD"); // 999 % 4 == 3
    REQUIRE(rows[999][4] == "sell");    // 999 % 2 == 1
}
