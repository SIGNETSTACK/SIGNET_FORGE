// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/z_order.hpp"
#include "signet/forge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

using namespace signet::forge;
namespace z = signet::forge::z_order;
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
// 1. normalize_int32 preserves order
// ===================================================================
TEST_CASE("normalize_int32 preserves order", "[z_order][normalize]") {
    // INT32_MIN < -1 < 0 < 1 < INT32_MAX
    auto a = z::normalize_int32(std::numeric_limits<int32_t>::min());
    auto b = z::normalize_int32(-1);
    auto c = z::normalize_int32(0);
    auto d = z::normalize_int32(1);
    auto e = z::normalize_int32(std::numeric_limits<int32_t>::max());

    CHECK(a < b);
    CHECK(b < c);
    CHECK(c < d);
    CHECK(d < e);
}

// ===================================================================
// 2. normalize_float preserves order
// ===================================================================
TEST_CASE("normalize_float preserves order", "[z_order][normalize]") {
    auto neg_inf = z::normalize_float(-std::numeric_limits<float>::infinity());
    auto neg_one = z::normalize_float(-1.0f);
    auto neg_zero = z::normalize_float(-0.0f);
    auto pos_zero = z::normalize_float(0.0f);
    auto pos_one = z::normalize_float(1.0f);
    auto pos_inf = z::normalize_float(std::numeric_limits<float>::infinity());

    CHECK(neg_inf < neg_one);
    CHECK(neg_one < neg_zero);
    // -0.0 and +0.0 may be equal or differ by 1 in IEEE encoding; both are fine
    CHECK(neg_zero <= pos_zero);
    CHECK(pos_zero < pos_one);
    CHECK(pos_one < pos_inf);
}

// ===================================================================
// 3. normalize_double preserves order
// ===================================================================
TEST_CASE("normalize_double preserves order", "[z_order][normalize]") {
    auto neg_inf = z::normalize_double(-std::numeric_limits<double>::infinity());
    auto neg_one = z::normalize_double(-1.0);
    auto pos_zero = z::normalize_double(0.0);
    auto pos_one = z::normalize_double(1.0);
    auto pos_inf = z::normalize_double(std::numeric_limits<double>::infinity());

    CHECK(neg_inf < neg_one);
    CHECK(neg_one < pos_zero);
    CHECK(pos_zero < pos_one);
    CHECK(pos_one < pos_inf);
}

// ===================================================================
// 4. normalize_string big-endian padding
// ===================================================================
TEST_CASE("normalize_string big-endian padding", "[z_order][normalize]") {
    // "A" = 0x41, padded to 0x41000000
    CHECK(z::normalize_string("A") == 0x41000000u);

    // "AB" = 0x41420000
    CHECK(z::normalize_string("AB") == 0x41420000u);

    // "ABCD" = 0x41424344
    CHECK(z::normalize_string("ABCD") == 0x41424344u);

    // Empty string = 0
    CHECK(z::normalize_string("") == 0u);

    // Longer than 4 chars: only first 4 used
    CHECK(z::normalize_string("ABCDE") == 0x41424344u);

    // Order preserved: "A" < "B"
    CHECK(z::normalize_string("A") < z::normalize_string("B"));
}

// ===================================================================
// 5. morton_2d known values
// ===================================================================
TEST_CASE("morton_2d known values", "[z_order][morton]") {
    // (0,0) -> 0
    CHECK(z::morton_2d(0, 0) == 0);

    // (1,0) -> 1 (x bit 0 goes to position 0)
    CHECK(z::morton_2d(1, 0) == 1);

    // (0,1) -> 2 (y bit 0 goes to position 1)
    CHECK(z::morton_2d(0, 1) == 2);

    // (1,1) -> 3 (both low bits set)
    CHECK(z::morton_2d(1, 1) == 3);

    // (2,0) -> 4 (x bit 1 goes to position 2)
    CHECK(z::morton_2d(2, 0) == 4);

    // (0,2) -> 8 (y bit 1 goes to position 3)
    CHECK(z::morton_2d(0, 2) == 8);

    // (3,5) = interleave(011, 101) = 100111 = 39
    CHECK(z::morton_2d(3, 5) == 39);
}

// ===================================================================
// 6. morton_2d roundtrip with deinterleave
// ===================================================================
TEST_CASE("morton_2d roundtrip with deinterleave", "[z_order][morton]") {
    std::vector<std::pair<uint32_t, uint32_t>> test_cases = {
        {0, 0}, {1, 0}, {0, 1}, {1, 1},
        {255, 127}, {1000, 2000}, {0xFFFF, 0xFFFF},
        {0x12345678, 0x9ABCDEF0}
    };

    for (const auto& [x, y] : test_cases) {
        uint64_t code = z::morton_2d(x, y);
        uint32_t rx, ry;
        z::deinterleave_2d(code, rx, ry);
        CHECK(rx == x);
        CHECK(ry == y);
    }
}

// ===================================================================
// 7. morton_nd 3-column bit interleave
// ===================================================================
TEST_CASE("morton_nd 3-column bit interleave", "[z_order][morton]") {
    // 3 columns, each with value 1 (bit 0 set):
    // Output should have the 3 lowest output bits set (MSB order)
    auto key1 = z::morton_nd({1, 1, 1}, 1);
    // 1 bit per col, 3 cols = 3 bits = 1 byte: 0b11100000 = 0xE0
    REQUIRE(key1.size() == 1);
    CHECK(key1[0] == 0xE0);

    // With 2 bits per column: col0=2, col1=1, col2=3
    // col0 bits: 1,0  col1 bits: 0,1  col2 bits: 1,1
    // Interleaved (MSB first): col0[1]=1, col1[1]=0, col2[1]=1, col0[0]=0, col1[0]=1, col2[0]=1
    // = 101011 in 6 bits -> pad to 1 byte = 10101100 = 0xAC
    auto key2 = z::morton_nd({2, 1, 3}, 2);
    REQUIRE(key2.size() == 1);
    CHECK(key2[0] == 0xAC);
}

// ===================================================================
// 8. ZOrderSorter 2-column int32 sort correctness
// ===================================================================
TEST_CASE("ZOrderSorter 2-column int32 sort correctness", "[z_order][sort]") {
    // 4 points in 2D: (3,1), (1,3), (2,2), (0,0)
    // Z-order should cluster nearby points together
    std::vector<int32_t> col_x = {3, 1, 2, 0};
    std::vector<int32_t> col_y = {1, 3, 2, 0};

    std::vector<z::ZOrderColumn> cols = {
        { PhysicalType::INT32, col_x.data(), col_x.size() },
        { PhysicalType::INT32, col_y.data(), col_y.size() }
    };

    auto perm = z::ZOrderSorter::sort(4, cols);
    REQUIRE(perm.size() == 4);

    // The permutation should be valid (contains each index exactly once)
    auto sorted_perm = perm;
    std::sort(sorted_perm.begin(), sorted_perm.end());
    CHECK(sorted_perm == std::vector<size_t>{0, 1, 2, 3});

    // Row 3 (0,0) should come first (smallest Z-value)
    CHECK(perm[0] == 3);
}

// ===================================================================
// 9. ZOrderSorter 3-column mixed types
// ===================================================================
TEST_CASE("ZOrderSorter 3-column mixed types", "[z_order][sort]") {
    std::vector<int32_t> ints = {100, -50, 0, 200, -100};
    std::vector<float> floats = {1.5f, -3.0f, 0.0f, 2.5f, -1.0f};
    std::vector<std::string> strs = {"banana", "apple", "cherry", "date", "elderberry"};

    std::vector<z::ZOrderColumn> cols = {
        { PhysicalType::INT32, ints.data(), ints.size() },
        { PhysicalType::FLOAT, floats.data(), floats.size() },
        { PhysicalType::BYTE_ARRAY, strs.data(), strs.size() }
    };

    auto perm = z::ZOrderSorter::sort(5, cols);
    REQUIRE(perm.size() == 5);

    // Verify permutation is valid
    auto sorted_perm = perm;
    std::sort(sorted_perm.begin(), sorted_perm.end());
    CHECK(sorted_perm == std::vector<size_t>{0, 1, 2, 3, 4});

    // The sort should be deterministic — run twice and get same result
    auto perm2 = z::ZOrderSorter::sort(5, cols);
    CHECK(perm == perm2);
}

// ===================================================================
// 10. Write Z-ordered Parquet + read back, verify spatial locality
// ===================================================================
TEST_CASE("Write Z-ordered Parquet and read back", "[z_order][pipeline]") {
    TempFile tmp("signet_test_z_order.parquet");

    // Generate 100 2D points
    constexpr size_t N = 100;
    std::vector<int32_t> x_vals(N), y_vals(N);
    for (size_t i = 0; i < N; ++i) {
        // Scatter points across a 2D space
        x_vals[i] = static_cast<int32_t>((i * 37) % 100);
        y_vals[i] = static_cast<int32_t>((i * 53) % 100);
    }

    // Compute Z-order permutation
    std::vector<z::ZOrderColumn> cols = {
        { PhysicalType::INT32, x_vals.data(), x_vals.size() },
        { PhysicalType::INT32, y_vals.data(), y_vals.size() }
    };
    auto perm = z::ZOrderSorter::sort(N, cols);
    REQUIRE(perm.size() == N);

    // Apply permutation to reorder data
    std::vector<int32_t> sorted_x(N), sorted_y(N);
    for (size_t i = 0; i < N; ++i) {
        sorted_x[i] = x_vals[perm[i]];
        sorted_y[i] = y_vals[perm[i]];
    }

    // Write Z-ordered data to Parquet
    auto schema = Schema::builder("z_ordered")
        .column<int32_t>("x")
        .column<int32_t>("y")
        .build();

    {
        auto writer_result = ParquetWriter::open(tmp.path, schema);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (size_t i = 0; i < N; ++i) {
            auto r = writer.write_row({
                std::to_string(sorted_x[i]),
                std::to_string(sorted_y[i])
            });
            REQUIRE(r.has_value());
        }

        auto close = writer.close();
        REQUIRE(close.has_value());
    }

    // Read back and verify
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(N));

        auto x_read = reader.read_column<int32_t>(0, 0);
        REQUIRE(x_read.has_value());
        auto y_read = reader.read_column<int32_t>(0, 1);
        REQUIRE(y_read.has_value());

        REQUIRE(x_read->size() == N);
        REQUIRE(y_read->size() == N);

        // Verify data matches what we wrote
        for (size_t i = 0; i < N; ++i) {
            CHECK((*x_read)[i] == sorted_x[i]);
            CHECK((*y_read)[i] == sorted_y[i]);
        }

        // Verify Z-ordering: the read-back data should have the same
        // Z-order as what we computed (first row should have smallest Z-key)
        std::vector<z::ZOrderColumn> read_cols = {
            { PhysicalType::INT32, x_read->data(), x_read->size() },
            { PhysicalType::INT32, y_read->data(), y_read->size() }
        };
        auto read_perm = z::ZOrderSorter::sort(N, read_cols);
        // The data is already Z-ordered, so perm should be identity
        for (size_t i = 0; i < N; ++i) {
            CHECK(read_perm[i] == i);
        }
    }
}
