// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
//
// Tier 3 Standard Vector Tests — Snappy Compression + Thrift Compact Protocol
//
// Three-way verification: C++ implementation vs known wire format vs Python
// independent implementation (/tmp/verify_tier3.py, 60/60 passing).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "signet/compression/snappy.hpp"
#include "signet/thrift/compact.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace signet::forge;
using namespace signet::forge::thrift;

// =============================================================================
// Snappy Tests
// =============================================================================

TEST_CASE("Snappy: empty input roundtrip", "[snappy][vectors]") {
    SnappyCodec codec;
    std::vector<uint8_t> empty;

    auto compressed = codec.compress(empty.data(), 0);
    REQUIRE(compressed.has_value());

    // Preamble should be varint(0) = 0x00
    REQUIRE(compressed->size() >= 1);
    REQUIRE((*compressed)[0] == 0x00);

    auto decompressed = codec.decompress(compressed->data(), compressed->size(), 0);
    REQUIRE(decompressed.has_value());
    REQUIRE(decompressed->empty());
}

TEST_CASE("Snappy: single byte roundtrip", "[snappy][vectors]") {
    SnappyCodec codec;
    std::vector<uint8_t> input = {0x42};

    auto compressed = codec.compress(input.data(), input.size());
    REQUIRE(compressed.has_value());

    // Preamble should be varint(1) = 0x01
    REQUIRE(compressed->size() >= 1);
    REQUIRE((*compressed)[0] == 0x01);

    auto decompressed = codec.decompress(compressed->data(), compressed->size(), 1);
    REQUIRE(decompressed.has_value());
    REQUIRE(*decompressed == input);
}

TEST_CASE("Snappy: small literal roundtrip", "[snappy][vectors]") {
    SnappyCodec codec;
    std::string msg = "Hello, Snappy!";
    std::vector<uint8_t> input(msg.begin(), msg.end());

    auto compressed = codec.compress(input.data(), input.size());
    REQUIRE(compressed.has_value());

    // Preamble: varint(14) = 0x0E
    REQUIRE((*compressed)[0] == 0x0E);

    auto decompressed = codec.decompress(
        compressed->data(), compressed->size(), input.size());
    REQUIRE(decompressed.has_value());
    REQUIRE(*decompressed == input);
}

TEST_CASE("Snappy: varint preamble encoding - known values", "[snappy][vectors]") {
    SnappyCodec codec;

    // 127 bytes: varint = 0x7F (single byte)
    {
        std::vector<uint8_t> input(127, 'A');
        auto comp = codec.compress(input.data(), input.size());
        REQUIRE(comp.has_value());
        REQUIRE((*comp)[0] == 0x7F);
    }

    // 128 bytes: varint = 0x80 0x01 (two bytes)
    {
        std::vector<uint8_t> input(128, 'B');
        auto comp = codec.compress(input.data(), input.size());
        REQUIRE(comp.has_value());
        REQUIRE((*comp)[0] == 0x80);
        REQUIRE((*comp)[1] == 0x01);
    }

    // 300 bytes: varint = 300 = 0xAC 0x02
    {
        std::vector<uint8_t> input(300, 'C');
        auto comp = codec.compress(input.data(), input.size());
        REQUIRE(comp.has_value());
        REQUIRE((*comp)[0] == 0xAC);
        REQUIRE((*comp)[1] == 0x02);
    }
}

TEST_CASE("Snappy: wire format - literal tag byte", "[snappy][vectors]") {
    SnappyCodec codec;

    // For a 3-byte input (too short for hash matching), the compressor emits:
    // [varint preamble] [literal tag] [3 bytes data]
    // literal tag for length 3: (3-1) << 2 | 0 = 0x08
    std::vector<uint8_t> input = {0xAA, 0xBB, 0xCC};
    auto comp = codec.compress(input.data(), input.size());
    REQUIRE(comp.has_value());

    // Preamble: varint(3) = 0x03
    REQUIRE((*comp)[0] == 0x03);
    // Tag byte: literal, len=3 → (2 << 2) | 0 = 0x08
    REQUIRE((*comp)[1] == 0x08);
    // Literal data
    REQUIRE((*comp)[2] == 0xAA);
    REQUIRE((*comp)[3] == 0xBB);
    REQUIRE((*comp)[4] == 0xCC);
}

TEST_CASE("Snappy: repetitive data compresses with copies", "[snappy][vectors]") {
    SnappyCodec codec;

    // Highly repetitive data should produce copy elements
    std::vector<uint8_t> input(1024, 0x55);
    auto comp = codec.compress(input.data(), input.size());
    REQUIRE(comp.has_value());
    // Compressed size should be much smaller than 1024
    REQUIRE(comp->size() < input.size() / 2);

    auto decomp = codec.decompress(comp->data(), comp->size(), input.size());
    REQUIRE(decomp.has_value());
    REQUIRE(*decomp == input);
}

TEST_CASE("Snappy: large random-ish data roundtrip", "[snappy][vectors]") {
    SnappyCodec codec;

    // Generate pseudo-random data (deterministic)
    std::vector<uint8_t> input(4096);
    uint32_t state = 0xDEADBEEF;
    for (size_t i = 0; i < input.size(); ++i) {
        state = state * 1664525U + 1013904223U;
        input[i] = static_cast<uint8_t>(state >> 24);
    }

    auto comp = codec.compress(input.data(), input.size());
    REQUIRE(comp.has_value());

    auto decomp = codec.decompress(comp->data(), comp->size(), input.size());
    REQUIRE(decomp.has_value());
    REQUIRE(*decomp == input);
}

TEST_CASE("Snappy: decompress rejects mismatched length", "[snappy][vectors]") {
    SnappyCodec codec;
    std::vector<uint8_t> input = {1, 2, 3, 4, 5};

    auto comp = codec.compress(input.data(), input.size());
    REQUIRE(comp.has_value());

    // Decompress with wrong expected size
    auto bad = codec.decompress(comp->data(), comp->size(), 999);
    REQUIRE(!bad.has_value());
}

TEST_CASE("Snappy: decompress rejects truncated data", "[snappy][vectors]") {
    SnappyCodec codec;
    std::vector<uint8_t> input(64, 'X');

    auto comp = codec.compress(input.data(), input.size());
    REQUIRE(comp.has_value());

    // Truncate compressed data
    auto truncated = std::vector<uint8_t>(comp->begin(), comp->begin() + 3);
    auto bad = codec.decompress(truncated.data(), truncated.size(), 64);
    REQUIRE(!bad.has_value());
}

TEST_CASE("Snappy: decompress rejects empty stream for non-zero output", "[snappy][vectors]") {
    SnappyCodec codec;
    auto bad = codec.decompress(nullptr, 0, 100);
    REQUIRE(!bad.has_value());
}

TEST_CASE("Snappy: mixed literal and copy data roundtrip", "[snappy][vectors]") {
    SnappyCodec codec;

    // Pattern: repeated 16-byte block followed by unique data
    std::vector<uint8_t> input;
    std::vector<uint8_t> block = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    for (int i = 0; i < 10; ++i) {
        input.insert(input.end(), block.begin(), block.end());
    }
    // Add some unique bytes
    for (uint8_t i = 0; i < 50; ++i) {
        input.push_back(i ^ 0xAA);
    }
    // More repetitions
    for (int i = 0; i < 5; ++i) {
        input.insert(input.end(), block.begin(), block.end());
    }

    auto comp = codec.compress(input.data(), input.size());
    REQUIRE(comp.has_value());

    auto decomp = codec.decompress(comp->data(), comp->size(), input.size());
    REQUIRE(decomp.has_value());
    REQUIRE(*decomp == input);
}

// =============================================================================
// Thrift Compact Protocol Tests
// =============================================================================

TEST_CASE("Thrift: zigzag i32 - known vectors", "[thrift][vectors]") {
    // Zigzag encoding standard: 0→0, -1→1, 1→2, -2→3, 2→4, ...
    // We verify via encode/decode roundtrip and wire format inspection.
    CompactEncoder enc;
    CompactDecoder dec(nullptr, 0);

    // Encode field 1 as I32 with value 0
    enc.write_field(1, compact_type::I32);
    enc.write_i32(0);

    // Field header: delta=1, type=I32(5) → (1<<4)|5 = 0x15
    REQUIRE(enc.data()[0] == 0x15);
    // Zigzag(0) = 0, varint(0) = 0x00
    REQUIRE(enc.data()[1] == 0x00);
}

TEST_CASE("Thrift: zigzag i32 - negative values", "[thrift][vectors]") {
    CompactEncoder enc;

    enc.write_field(1, compact_type::I32);
    enc.write_i32(-1);  // zigzag(-1) = 1, varint(1) = 0x01

    REQUIRE(enc.data()[0] == 0x15);  // field header
    REQUIRE(enc.data()[1] == 0x01);  // zigzag(-1) = 1
}

TEST_CASE("Thrift: zigzag i32 - positive values", "[thrift][vectors]") {
    CompactEncoder enc;

    enc.write_field(1, compact_type::I32);
    enc.write_i32(1);  // zigzag(1) = 2, varint(2) = 0x02

    REQUIRE(enc.data()[0] == 0x15);
    REQUIRE(enc.data()[1] == 0x02);  // zigzag(1) = 2
}

TEST_CASE("Thrift: zigzag i32 - large values", "[thrift][vectors]") {
    CompactEncoder enc;

    enc.write_field(1, compact_type::I32);
    enc.write_i32(100);  // zigzag(100) = 200, varint(200) = 0xC8 0x01

    REQUIRE(enc.data()[0] == 0x15);
    REQUIRE(enc.data()[1] == 0xC8);
    REQUIRE(enc.data()[2] == 0x01);
}

TEST_CASE("Thrift: zigzag i64 roundtrip - boundary values", "[thrift][vectors]") {
    // Test INT64_MIN, INT64_MAX, 0, -1, 1
    struct TestCase { int64_t val; };
    TestCase cases[] = {
        {0}, {1}, {-1}, {127}, {-128},
        {INT32_MAX}, {INT32_MIN},
        {INT64_MAX}, {INT64_MIN},
        {1000000}, {-1000000}
    };

    for (const auto& tc : cases) {
        CompactEncoder enc;
        enc.write_field(1, compact_type::I64);
        enc.write_i64(tc.val);
        enc.write_stop();

        CompactDecoder dec(enc.data().data(), enc.data().size());
        auto fh = dec.read_field_header();
        REQUIRE(fh.field_id == 1);
        REQUIRE(fh.thrift_type == compact_type::I64);

        auto decoded = dec.read_i64();
        REQUIRE(decoded == tc.val);
        REQUIRE(dec.good());
    }
}

TEST_CASE("Thrift: field header delta encoding - wire format", "[thrift][vectors]") {
    CompactEncoder enc;

    // Field 1 (I32): delta=1 → (1<<4)|5 = 0x15
    enc.write_field(1, compact_type::I32);
    enc.write_i32(42);
    REQUIRE(enc.data()[0] == 0x15);

    // Field 2 (I64): delta=1 → (1<<4)|6 = 0x16
    enc.write_field(2, compact_type::I64);
    enc.write_i64(100);

    // Find the second field header byte
    // After field 1: 0x15 + varint(zigzag(42)) = 0x15 0x54
    // zigzag(42)=84, varint(84)=0x54
    size_t pos = 0;
    pos++; // skip field 1 header
    // Skip the i32 varint
    while (pos < enc.data().size() && (enc.data()[pos] & 0x80)) pos++;
    pos++; // past the final varint byte

    REQUIRE(enc.data()[pos] == 0x16);  // field 2 header: delta=1, type=I64
}

TEST_CASE("Thrift: field header non-delta encoding", "[thrift][vectors]") {
    CompactEncoder enc;

    // Field 1 (I32)
    enc.write_field(1, compact_type::I32);
    enc.write_i32(0);

    // Field 20 (I32): delta=19 > 15, must use non-delta form
    enc.write_field(20, compact_type::I32);
    enc.write_i32(0);

    // Non-delta header: type byte (0x05) followed by zigzag(20) varint
    // Find position after field 1 (0x15, 0x00 = 2 bytes)
    REQUIRE(enc.data()[2] == compact_type::I32);  // type byte only, no delta
    // zigzag_encode_i32(20) = 40, varint(40) = 0x28
    REQUIRE(enc.data()[3] == 0x28);
}

TEST_CASE("Thrift: bool embedded in field header", "[thrift][vectors]") {
    CompactEncoder enc;

    // Bool true: field 1, type BOOL_TRUE(1)
    enc.write_field_bool(1, true);
    // Delta=1, type=1 → (1<<4)|1 = 0x11
    REQUIRE(enc.data()[0] == 0x11);

    // Bool false: field 2, type BOOL_FALSE(2)
    enc.write_field_bool(2, false);
    // Delta=1, type=2 → (1<<4)|2 = 0x12
    REQUIRE(enc.data()[1] == 0x12);

    // Decode and verify
    CompactDecoder dec(enc.data().data(), enc.data().size());

    auto fh1 = dec.read_field_header();
    REQUIRE(fh1.field_id == 1);
    REQUIRE(fh1.thrift_type == compact_type::BOOL_TRUE);
    REQUIRE(dec.read_bool() == true);

    auto fh2 = dec.read_field_header();
    REQUIRE(fh2.field_id == 2);
    REQUIRE(fh2.thrift_type == compact_type::BOOL_FALSE);
    REQUIRE(dec.read_bool() == false);

    REQUIRE(dec.good());
}

TEST_CASE("Thrift: string field roundtrip", "[thrift][vectors]") {
    CompactEncoder enc;
    enc.write_field(1, compact_type::BINARY);
    enc.write_string("Hello, Thrift!");
    enc.write_stop();

    CompactDecoder dec(enc.data().data(), enc.data().size());
    auto fh = dec.read_field_header();
    REQUIRE(fh.field_id == 1);
    REQUIRE(fh.thrift_type == compact_type::BINARY);

    auto str = dec.read_string();
    REQUIRE(str == "Hello, Thrift!");

    auto stop = dec.read_field_header();
    REQUIRE(stop.is_stop());
    REQUIRE(dec.good());
}

TEST_CASE("Thrift: double field wire format", "[thrift][vectors]") {
    CompactEncoder enc;
    enc.write_field(1, compact_type::DOUBLE);
    enc.write_double(3.14);
    enc.write_stop();

    CompactDecoder dec(enc.data().data(), enc.data().size());
    auto fh = dec.read_field_header();
    REQUIRE(fh.field_id == 1);
    REQUIRE(fh.thrift_type == compact_type::DOUBLE);

    auto val = dec.read_double();
    REQUIRE_THAT(val, Catch::Matchers::WithinRel(3.14, 1e-10));

    auto stop = dec.read_field_header();
    REQUIRE(stop.is_stop());
    REQUIRE(dec.good());
}

TEST_CASE("Thrift: list header short form (size <= 14)", "[thrift][vectors]") {
    CompactEncoder enc;
    enc.write_field(1, compact_type::LIST);
    enc.write_list_header(compact_type::I32, 5);

    // Short form: (5 << 4) | 5 = 0x55
    // After field header byte (0x19 = delta=1, type=LIST=9)
    REQUIRE(enc.data()[0] == 0x19);
    REQUIRE(enc.data()[1] == 0x55);

    // Write 5 i32 values
    for (int i = 0; i < 5; ++i) {
        enc.write_i32(i * 10);
    }
    enc.write_stop();

    // Decode
    CompactDecoder dec(enc.data().data(), enc.data().size());
    auto fh = dec.read_field_header();
    REQUIRE(fh.thrift_type == compact_type::LIST);

    auto lh = dec.read_list_header();
    REQUIRE(lh.elem_type == compact_type::I32);
    REQUIRE(lh.size == 5);

    for (int i = 0; i < 5; ++i) {
        REQUIRE(dec.read_i32() == i * 10);
    }
    REQUIRE(dec.good());
}

TEST_CASE("Thrift: list header long form (size > 14)", "[thrift][vectors]") {
    CompactEncoder enc;
    enc.write_field(1, compact_type::LIST);
    enc.write_list_header(compact_type::I64, 20);

    // Long form: (0xF0 | 6) = 0xF6, then varint(20) = 0x14
    REQUIRE(enc.data()[0] == 0x19);  // field header
    REQUIRE(enc.data()[1] == 0xF6);  // long form: 0xF0 | I64(6)
    REQUIRE(enc.data()[2] == 0x14);  // varint(20)

    // Write 20 i64 values
    for (int i = 0; i < 20; ++i) {
        enc.write_i64(static_cast<int64_t>(i));
    }
    enc.write_stop();

    // Decode
    CompactDecoder dec(enc.data().data(), enc.data().size());
    auto fh = dec.read_field_header();
    REQUIRE(fh.thrift_type == compact_type::LIST);

    auto lh = dec.read_list_header();
    REQUIRE(lh.elem_type == compact_type::I64);
    REQUIRE(lh.size == 20);

    for (int i = 0; i < 20; ++i) {
        REQUIRE(dec.read_i64() == static_cast<int64_t>(i));
    }
    REQUIRE(dec.good());
}

TEST_CASE("Thrift: nested struct roundtrip", "[thrift][vectors]") {
    // Encode: outer struct { field 1: i32=42, field 2: inner struct { field 1: string="nested" } }
    CompactEncoder enc;

    enc.write_field(1, compact_type::I32);
    enc.write_i32(42);

    enc.write_field(2, compact_type::STRUCT);
    enc.begin_struct();
    enc.write_field(1, compact_type::BINARY);
    enc.write_string("nested");
    enc.write_stop();  // end inner struct
    enc.end_struct();

    enc.write_stop();  // end outer struct

    // Decode
    CompactDecoder dec(enc.data().data(), enc.data().size());

    auto fh1 = dec.read_field_header();
    REQUIRE(fh1.field_id == 1);
    REQUIRE(fh1.thrift_type == compact_type::I32);
    REQUIRE(dec.read_i32() == 42);

    auto fh2 = dec.read_field_header();
    REQUIRE(fh2.field_id == 2);
    REQUIRE(fh2.thrift_type == compact_type::STRUCT);

    dec.begin_struct();
    auto inner_fh = dec.read_field_header();
    REQUIRE(inner_fh.field_id == 1);
    REQUIRE(inner_fh.thrift_type == compact_type::BINARY);
    REQUIRE(dec.read_string() == "nested");

    auto inner_stop = dec.read_field_header();
    REQUIRE(inner_stop.is_stop());
    dec.end_struct();

    auto outer_stop = dec.read_field_header();
    REQUIRE(outer_stop.is_stop());
    REQUIRE(dec.good());
}

TEST_CASE("Thrift: full struct roundtrip - all types", "[thrift][vectors]") {
    CompactEncoder enc;

    // field 1: bool true
    enc.write_field_bool(1, true);
    // field 2: i32 = -999
    enc.write_field(2, compact_type::I32);
    enc.write_i32(-999);
    // field 3: i64 = 1234567890123
    enc.write_field(3, compact_type::I64);
    enc.write_i64(1234567890123LL);
    // field 4: double = 2.718281828
    enc.write_field(4, compact_type::DOUBLE);
    enc.write_double(2.718281828);
    // field 5: string = "test_value"
    enc.write_field(5, compact_type::BINARY);
    enc.write_string("test_value");
    // field 6: bool false
    enc.write_field_bool(6, false);

    enc.write_stop();

    // Decode and verify each field
    CompactDecoder dec(enc.data().data(), enc.data().size());

    auto f1 = dec.read_field_header();
    REQUIRE(f1.field_id == 1);
    REQUIRE(dec.read_bool() == true);

    auto f2 = dec.read_field_header();
    REQUIRE(f2.field_id == 2);
    REQUIRE(dec.read_i32() == -999);

    auto f3 = dec.read_field_header();
    REQUIRE(f3.field_id == 3);
    REQUIRE(dec.read_i64() == 1234567890123LL);

    auto f4 = dec.read_field_header();
    REQUIRE(f4.field_id == 4);
    REQUIRE_THAT(dec.read_double(), Catch::Matchers::WithinRel(2.718281828, 1e-9));

    auto f5 = dec.read_field_header();
    REQUIRE(f5.field_id == 5);
    REQUIRE(dec.read_string() == "test_value");

    auto f6 = dec.read_field_header();
    REQUIRE(f6.field_id == 6);
    REQUIRE(dec.read_bool() == false);

    auto stop = dec.read_field_header();
    REQUIRE(stop.is_stop());
    REQUIRE(dec.good());
}

TEST_CASE("Thrift: stop byte is 0x00", "[thrift][vectors]") {
    CompactEncoder enc;
    enc.write_stop();
    REQUIRE(enc.data().size() == 1);
    REQUIRE(enc.data()[0] == 0x00);
}

TEST_CASE("Thrift: varint encoding - known wire format", "[thrift][vectors]") {
    // Verify varint encoding via i32 field values
    // zigzag(n) then varint-encoded

    CompactEncoder enc;

    // Value 0: zigzag=0, varint=0x00
    enc.write_field(1, compact_type::I32);
    enc.write_i32(0);

    // Value 300: zigzag=600, varint(600) = 0xD8 0x04
    enc.write_field(2, compact_type::I32);
    enc.write_i32(300);

    // Verify field 1: header=0x15, data=0x00
    REQUIRE(enc.data()[0] == 0x15);
    REQUIRE(enc.data()[1] == 0x00);

    // Verify field 2: header=0x15 (delta=1)
    REQUIRE(enc.data()[2] == 0x15);
    // zigzag(300) = 600 = 0x258
    // varint(600): 600 & 0x7F = 0x58, 600>>7 = 4 → 0xD8 0x04
    REQUIRE(enc.data()[3] == 0xD8);
    REQUIRE(enc.data()[4] == 0x04);
}

TEST_CASE("Thrift: decoder detects truncated buffer", "[thrift][vectors]") {
    // 1 byte buffer — try to read field header then i32
    uint8_t buf[] = {0x15}; // field header byte only, no i32 data

    CompactDecoder dec(buf, 1);
    auto fh = dec.read_field_header();
    REQUIRE(fh.field_id == 1);
    REQUIRE(fh.thrift_type == compact_type::I32);

    // Try to read i32 — should fail (no data after header)
    auto val = dec.read_i32();
    (void)val;
    REQUIRE(!dec.good());
}

TEST_CASE("Thrift: empty binary field", "[thrift][vectors]") {
    CompactEncoder enc;
    enc.write_field(1, compact_type::BINARY);
    enc.write_string("");
    enc.write_stop();

    CompactDecoder dec(enc.data().data(), enc.data().size());
    auto fh = dec.read_field_header();
    REQUIRE(fh.thrift_type == compact_type::BINARY);
    REQUIRE(dec.read_string().empty());
    REQUIRE(dec.good());
}

TEST_CASE("Thrift: skip_field skips unknown types", "[thrift][vectors]") {
    CompactEncoder enc;

    // Write a struct with 3 fields, then verify skipping field 2
    enc.write_field(1, compact_type::I32);
    enc.write_i32(111);
    enc.write_field(2, compact_type::BINARY);
    enc.write_string("skip me");
    enc.write_field(3, compact_type::I32);
    enc.write_i32(333);
    enc.write_stop();

    CompactDecoder dec(enc.data().data(), enc.data().size());

    auto f1 = dec.read_field_header();
    REQUIRE(f1.field_id == 1);
    REQUIRE(dec.read_i32() == 111);

    // Skip field 2
    auto f2 = dec.read_field_header();
    REQUIRE(f2.field_id == 2);
    dec.skip_field(f2.thrift_type);

    // Read field 3
    auto f3 = dec.read_field_header();
    REQUIRE(f3.field_id == 3);
    REQUIRE(dec.read_i32() == 333);

    REQUIRE(dec.good());
}

TEST_CASE("Thrift: float field roundtrip", "[thrift][vectors]") {
    CompactEncoder enc;
    // Note: float is not standard compact protocol, but our implementation supports it
    enc.write_float(1.5f);

    CompactDecoder dec(enc.data().data(), enc.data().size());
    auto val = dec.read_float();
    REQUIRE(val == 1.5f);
    REQUIRE(dec.good());
}
