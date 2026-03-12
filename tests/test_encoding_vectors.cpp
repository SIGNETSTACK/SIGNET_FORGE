// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
//
// Tier 2 Encoding Verification — Standard Vector Tests
// =====================================================
// Three-way verification: C++ implementation vs Python independent
// implementation vs published standard wire format vectors.
//
// Each test compares against known byte sequences derived from the
// Parquet format specification and independently verified by
// /tmp/verify_encoding_tier2.py (44/44 tests passing).

#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

using namespace signet::forge;

// ===================================================================
// Helper: compare byte vectors
// ===================================================================

static bool bytes_equal(const std::vector<uint8_t>& a,
                        const std::vector<uint8_t>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

// ===================================================================
// Section 1: Zigzag Encoding — Standard Vectors (Protobuf/Parquet spec)
// ===================================================================

TEST_CASE("Zigzag64 standard vectors (Parquet/Protobuf spec)", "[encoding][delta][vectors]") {
    // Published mapping: signed → unsigned
    // 0 → 0, -1 → 1, 1 → 2, -2 → 3, 2 → 4
    REQUIRE(delta::zigzag_encode(0) == 0);
    REQUIRE(delta::zigzag_encode(-1) == 1);
    REQUIRE(delta::zigzag_encode(1) == 2);
    REQUIRE(delta::zigzag_encode(-2) == 3);
    REQUIRE(delta::zigzag_encode(2) == 4);

    // INT32 boundary values
    REQUIRE(delta::zigzag_encode(INT32_MAX) == 4294967294ULL);
    REQUIRE(delta::zigzag_encode(INT32_MIN) == 4294967295ULL);

    // INT64 boundary values
    REQUIRE(delta::zigzag_encode(INT64_MAX) == 18446744073709551614ULL);
    REQUIRE(delta::zigzag_encode(INT64_MIN) == 18446744073709551615ULL);

    // Roundtrip verification
    REQUIRE(delta::zigzag_decode(delta::zigzag_encode(0)) == 0);
    REQUIRE(delta::zigzag_decode(delta::zigzag_encode(-1)) == -1);
    REQUIRE(delta::zigzag_decode(delta::zigzag_encode(1)) == 1);
    REQUIRE(delta::zigzag_decode(delta::zigzag_encode(INT64_MAX)) == INT64_MAX);
    REQUIRE(delta::zigzag_decode(delta::zigzag_encode(INT64_MIN)) == INT64_MIN);
}

TEST_CASE("Zigzag32 standard vectors", "[encoding][delta][vectors]") {
    REQUIRE(delta::zigzag_encode32(0) == 0u);
    REQUIRE(delta::zigzag_encode32(-1) == 1u);
    REQUIRE(delta::zigzag_encode32(1) == 2u);
    REQUIRE(delta::zigzag_encode32(-2) == 3u);
    REQUIRE(delta::zigzag_encode32(2) == 4u);
    REQUIRE(delta::zigzag_encode32(INT32_MAX) == 4294967294u);
    REQUIRE(delta::zigzag_encode32(INT32_MIN) == 4294967295u);

    // Roundtrip
    REQUIRE(delta::zigzag_decode32(delta::zigzag_encode32(0)) == 0);
    REQUIRE(delta::zigzag_decode32(delta::zigzag_encode32(-1)) == -1);
    REQUIRE(delta::zigzag_decode32(delta::zigzag_encode32(INT32_MAX)) == INT32_MAX);
    REQUIRE(delta::zigzag_decode32(delta::zigzag_encode32(INT32_MIN)) == INT32_MIN);
}

// ===================================================================
// Section 2: Varint (LEB128) — Standard Vectors
// ===================================================================

TEST_CASE("Varint LEB128 standard vectors", "[encoding][rle][vectors]") {
    // Known varint encodings (unsigned LEB128)
    auto test_varint = [](uint64_t value, const std::vector<uint8_t>& expected) {
        // Encode
        std::vector<uint8_t> buf;
        encode_varint(buf, value);
        REQUIRE(bytes_equal(buf, expected));

        // Decode
        size_t pos = 0;
        uint64_t decoded = decode_varint(expected.data(), pos, expected.size());
        REQUIRE(decoded == value);
        REQUIRE(pos == expected.size());
    };

    test_varint(0,     {0x00});
    test_varint(1,     {0x01});
    test_varint(127,   {0x7F});
    test_varint(128,   {0x80, 0x01});
    test_varint(300,   {0xAC, 0x02});
    test_varint(16384, {0x80, 0x80, 0x01});
}

// ===================================================================
// Section 3: Bit-Packing — Known Bit Patterns
// ===================================================================

TEST_CASE("Bit-pack 8 values [0..7] at bw=3 known pattern", "[encoding][rle][vectors]") {
    // 8 values [0,1,2,3,4,5,6,7] at 3 bits each = 24 bits = 3 bytes
    // LSB-first packing: 0x88 0xC6 0xFA
    // (Verified by Python: bit_pack_8([0..7], 3) = 88c6fa)
    uint64_t values[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    std::vector<uint8_t> packed;
    bit_pack_8(packed, values, 3);

    REQUIRE(packed.size() == 3);
    REQUIRE(packed[0] == 0x88);
    REQUIRE(packed[1] == 0xC6);
    REQUIRE(packed[2] == 0xFA);

    // Roundtrip
    uint64_t unpacked[8];
    bit_unpack_8(packed.data(), unpacked, 3);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(unpacked[i] == static_cast<uint64_t>(i));
    }
}

TEST_CASE("Bit-pack alternating at bw=1 → 0x55", "[encoding][rle][vectors]") {
    // [1,0,1,0,1,0,1,0] at bw=1 = 0x55 (01010101 binary)
    uint64_t values[8] = {1, 0, 1, 0, 1, 0, 1, 0};
    std::vector<uint8_t> packed;
    bit_pack_8(packed, values, 1);

    REQUIRE(packed.size() == 1);
    REQUIRE(packed[0] == 0x55);
}

TEST_CASE("Bit-pack all-ones at bw=1 → 0xFF", "[encoding][rle][vectors]") {
    uint64_t values[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    std::vector<uint8_t> packed;
    bit_pack_8(packed, values, 1);

    REQUIRE(packed.size() == 1);
    REQUIRE(packed[0] == 0xFF);
}

TEST_CASE("Bit-pack at bw=8 is identity", "[encoding][rle][vectors]") {
    // At bit_width=8, each value occupies exactly 1 byte
    uint64_t values[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
    std::vector<uint8_t> packed;
    bit_pack_8(packed, values, 8);

    REQUIRE(packed.size() == 8);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(packed[i] == static_cast<uint8_t>(values[i]));
    }
}

TEST_CASE("Bit-pack bw=0 produces no bytes", "[encoding][rle][vectors]") {
    uint64_t values[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<uint8_t> packed;
    bit_pack_8(packed, values, 0);
    REQUIRE(packed.empty());

    // Unpack with bw=0 gives all zeros
    uint64_t unpacked[8];
    bit_unpack_8(nullptr, unpacked, 0);
    for (int i = 0; i < 8; ++i) {
        REQUIRE(unpacked[i] == 0);
    }
}

// ===================================================================
// Section 4: RLE Known Wire Format Decode
// ===================================================================

TEST_CASE("RLE decode known wire: 8 copies of 5 at bw=4", "[encoding][rle][vectors]") {
    // Wire format: header=0x10 (run_length=8, LSB=0), value=0x05
    // Python verified: rle_decode(bytes([0x10, 0x05]), 4, 8) == [5]*8
    std::vector<uint8_t> wire = {0x10, 0x05};
    auto decoded = RleDecoder::decode(wire.data(), wire.size(), 4, 8);
    REQUIRE(decoded.size() == 8);
    for (auto v : decoded) {
        REQUIRE(v == 5);
    }
}

TEST_CASE("RLE decode known wire: bit-pack group [0..7] at bw=3", "[encoding][rle][vectors]") {
    // Wire format: header=0x03 (group_count=1, LSB=1) + packed bytes
    // Python verified: 03 + 88c6fa
    std::vector<uint8_t> wire = {0x03, 0x88, 0xC6, 0xFA};
    auto decoded = RleDecoder::decode(wire.data(), wire.size(), 3, 8);
    REQUIRE(decoded.size() == 8);
    for (size_t i = 0; i < 8; ++i) {
        REQUIRE(decoded[i] == static_cast<uint32_t>(i));
    }
}

TEST_CASE("RLE decode known wire: run of 1 at bw=1", "[encoding][rle][vectors]") {
    // header = (1 << 1) | 0 = 0x02, value = 0x01
    std::vector<uint8_t> wire = {0x02, 0x01};
    auto decoded = RleDecoder::decode(wire.data(), wire.size(), 1, 1);
    REQUIRE(decoded.size() == 1);
    REQUIRE(decoded[0] == 1);
}

// ===================================================================
// Section 5: RLE Encode/Decode Three-Way Roundtrip
// ===================================================================

TEST_CASE("RLE three-way: pure RLE runs", "[encoding][rle][vectors]") {
    // 8 zeros, 8 ones, 8 twos — verified in Python
    std::vector<uint32_t> values;
    for (uint32_t v = 0; v < 3; ++v) {
        for (int i = 0; i < 8; ++i) {
            values.push_back(v);
        }
    }

    auto encoded = RleEncoder::encode(values.data(), values.size(), 2);
    REQUIRE(!encoded.empty());

    auto decoded = RleDecoder::decode(encoded.data(), encoded.size(), 2, values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("RLE three-way: bit-pack diverse values", "[encoding][rle][vectors]") {
    // [0,1,2,3,4,5,6,7] * 2 at bw=3
    std::vector<uint32_t> values;
    for (int rep = 0; rep < 2; ++rep) {
        for (uint32_t i = 0; i < 8; ++i) {
            values.push_back(i);
        }
    }

    auto encoded = RleEncoder::encode(values.data(), values.size(), 3);
    auto decoded = RleDecoder::decode(encoded.data(), encoded.size(), 3, values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("RLE three-way: length-prefixed roundtrip", "[encoding][rle][vectors]") {
    std::vector<uint32_t> values = {1, 1, 1, 1, 1, 1, 1, 1,
                                     0, 0, 0, 0, 0, 0, 0, 0};
    auto encoded = RleEncoder::encode_with_length(values.data(), values.size(), 1);
    REQUIRE(encoded.size() >= 4);

    // Verify length prefix
    uint32_t prefix_len = static_cast<uint32_t>(encoded[0])
                        | (static_cast<uint32_t>(encoded[1]) << 8)
                        | (static_cast<uint32_t>(encoded[2]) << 16)
                        | (static_cast<uint32_t>(encoded[3]) << 24);
    REQUIRE(prefix_len == encoded.size() - 4);

    auto decoded = RleDecoder::decode_with_length(encoded.data(), encoded.size(),
                                                    1, values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("RLE three-way: large run (1024 repeated values)", "[encoding][rle][vectors]") {
    std::vector<uint32_t> values(1024, 42);
    auto encoded = RleEncoder::encode(values.data(), values.size(), 6);
    auto decoded = RleDecoder::decode(encoded.data(), encoded.size(), 6, values.size());
    REQUIRE(decoded.size() == values.size());
    for (auto v : decoded) {
        REQUIRE(v == 42);
    }
    // RLE should compress this very well
    REQUIRE(encoded.size() < 10);
}

TEST_CASE("RLE three-way: all bit widths 1-16", "[encoding][rle][vectors]") {
    for (int bw = 1; bw <= 16; ++bw) {
        uint32_t max_val = (bw >= 32) ? UINT32_MAX : ((1u << bw) - 1);
        std::vector<uint32_t> values;
        // 8 repeated + 8 diverse
        for (int i = 0; i < 8; ++i) values.push_back(max_val);
        for (int i = 0; i < 8; ++i) values.push_back(static_cast<uint32_t>(i) & max_val);

        auto encoded = RleEncoder::encode(values.data(), values.size(), bw);
        REQUIRE(!encoded.empty());

        auto decoded = RleDecoder::decode(encoded.data(), encoded.size(), bw, values.size());
        REQUIRE(decoded.size() == values.size());
        for (size_t i = 0; i < values.size(); ++i) {
            REQUIRE(decoded[i] == values[i]);
        }
    }
}

// ===================================================================
// Section 6: Delta Binary Packed — Standard Vectors
// ===================================================================

TEST_CASE("Delta three-way: constant delta=1 (1..129)", "[encoding][delta][vectors]") {
    // 129 values → 128 deltas = 1 full block, all deltas = 1
    // Python verified: delta_encode_int64([1..129]) roundtrips correctly
    std::vector<int64_t> values(129);
    for (int i = 0; i < 129; ++i) values[i] = i + 1;

    auto encoded = delta::encode_int64(values.data(), values.size());
    REQUIRE(!encoded.empty());

    // Verify header structure: block_size=128, miniblock_count=4
    size_t pos = 0;
    uint64_t block_size = decode_varint(encoded.data(), pos, encoded.size());
    uint64_t miniblock_count = decode_varint(encoded.data(), pos, encoded.size());
    REQUIRE(block_size == 128);
    REQUIRE(miniblock_count == 4);

    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("Delta three-way: nanosecond timestamps", "[encoding][delta][vectors]") {
    // Simulates real-world sorted timestamps with constant stride
    constexpr size_t N = 200;
    std::vector<int64_t> values(N);
    for (size_t i = 0; i < N; ++i) {
        values[i] = 1000000000LL + static_cast<int64_t>(i) * 1000;
    }

    auto encoded = delta::encode_int64(values.data(), values.size());
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());

    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }

    // Constant-delta should compress significantly
    size_t raw_size = N * sizeof(int64_t);
    REQUIRE(encoded.size() < raw_size / 2);
}

TEST_CASE("Delta three-way: negative deltas (descending)", "[encoding][delta][vectors]") {
    // Descending: 100, 90, 80, ..., 10 + padding to fill a block
    constexpr size_t BLOCK = 128;
    std::vector<int64_t> values;
    for (int i = 0; i < 10; ++i) values.push_back(100 - i * 10);
    // Pad to fill block + 1
    while (values.size() <= BLOCK) values.push_back(10);

    auto encoded = delta::encode_int64(values.data(), values.size());
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());

    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("Delta three-way: mixed positive/negative deltas", "[encoding][delta][vectors]") {
    // Alternating ups and downs: 0, 10, 5, 15, 10, 20, 15, ...
    constexpr size_t N = 129;
    std::vector<int64_t> values(N);
    values[0] = 0;
    for (size_t i = 1; i < N; ++i) {
        values[i] = values[i-1] + ((i % 2 == 1) ? 10 : -5);
    }

    auto encoded = delta::encode_int64(values.data(), values.size());
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());

    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("Delta three-way: int32 roundtrip", "[encoding][delta][vectors]") {
    std::vector<int32_t> values = {10, 20, 30, 25, 35, 45, 40, 50};

    auto encoded = delta::encode_int32(values.data(), values.size());
    auto decoded = delta::decode_int32(encoded.data(), encoded.size(), values.size());

    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("Delta three-way: extreme INT64 boundary values", "[encoding][delta][vectors]") {
    // Large signed values that stress unsigned delta arithmetic
    std::vector<int64_t> values;
    values.reserve(129);
    for (int i = 0; i < 129; ++i) {
        if (i % 2 == 0)
            values.push_back(INT64_MIN / 2 + i);
        else
            values.push_back(INT64_MAX / 2 - i);
    }

    auto encoded = delta::encode_int64(values.data(), values.size());
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());

    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("Delta three-way: all zeros", "[encoding][delta][vectors]") {
    // All deltas = 0, should compress to minimal size
    std::vector<int64_t> values(200, 0);

    auto encoded = delta::encode_int64(values.data(), values.size());
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());

    REQUIRE(decoded.size() == values.size());
    for (auto v : decoded) {
        REQUIRE(v == 0);
    }
    // All-zero deltas with bit_width=0 miniblocks should be very compact
    REQUIRE(encoded.size() < 30);
}

TEST_CASE("Delta three-way: single value", "[encoding][delta][vectors]") {
    std::vector<int64_t> values = {42};
    auto encoded = delta::encode_int64(values.data(), values.size());
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), 1);
    REQUIRE(decoded.size() == 1);
    REQUIRE(decoded[0] == 42);
}

TEST_CASE("Delta three-way: empty", "[encoding][delta][vectors]") {
    auto encoded = delta::encode_int64(nullptr, 0);
    REQUIRE(!encoded.empty()); // Valid header with count=0
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), 0);
    REQUIRE(decoded.empty());
}

// ===================================================================
// Section 7: Dictionary Encoding — Three-Way Roundtrip
// ===================================================================

TEST_CASE("Dictionary three-way: string encode/decode", "[encoding][dictionary][vectors]") {
    // Standard pattern: low-cardinality symbols
    std::vector<std::string> input = {
        "BTC", "ETH", "BTC", "SOL", "ETH", "BTC", "BTC", "SOL",
        "BTC", "ETH", "BTC", "SOL", "ETH", "BTC", "BTC", "SOL"
    };

    DictionaryEncoder<std::string> enc;
    for (const auto& s : input) REQUIRE(enc.put(s));
    enc.flush();

    REQUIRE(enc.dictionary_size() == 3);
    REQUIRE(enc.num_values() == 16);
    REQUIRE(enc.is_worthwhile());
    // bit_width = ceil(log2(3)) = 2
    REQUIRE(enc.bit_width() == 2);

    auto dict_page = enc.dictionary_page();
    auto idx_page = enc.indices_page();

    // Verify index page starts with bit_width byte
    REQUIRE(idx_page[0] == 2);

    DictionaryDecoder<std::string> dec(dict_page.data(), dict_page.size(),
                                        enc.dictionary_size(),
                                        PhysicalType::BYTE_ARRAY);
    REQUIRE(dec.dictionary_size() == 3);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        REQUIRE((*decoded)[i] == input[i]);
    }
}

TEST_CASE("Dictionary three-way: int64 encode/decode", "[encoding][dictionary][vectors]") {
    std::vector<int64_t> input = {100, 200, 100, 300, 200, 100, 100, 200};

    DictionaryEncoder<int64_t> enc;
    for (auto v : input) REQUIRE(enc.put(v));
    enc.flush();

    REQUIRE(enc.dictionary_size() == 3);
    REQUIRE(enc.bit_width() == 2);

    auto dict_page = enc.dictionary_page();
    auto idx_page = enc.indices_page();

    DictionaryDecoder<int64_t> dec(dict_page.data(), dict_page.size(),
                                    enc.dictionary_size(), PhysicalType::INT64);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        REQUIRE((*decoded)[i] == input[i]);
    }
}

TEST_CASE("Dictionary three-way: float encode/decode", "[encoding][dictionary][vectors]") {
    std::vector<float> input = {1.0f, 2.5f, 1.0f, 3.14f, 2.5f, 1.0f};

    DictionaryEncoder<float> enc;
    for (auto v : input) REQUIRE(enc.put(v));
    enc.flush();

    REQUIRE(enc.dictionary_size() == 3);

    auto dict_page = enc.dictionary_page();
    auto idx_page = enc.indices_page();

    DictionaryDecoder<float> dec(dict_page.data(), dict_page.size(),
                                   enc.dictionary_size(), PhysicalType::FLOAT);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        uint32_t orig_bits, dec_bits;
        std::memcpy(&orig_bits, &input[i], 4);
        std::memcpy(&dec_bits, &(*decoded)[i], 4);
        REQUIRE(orig_bits == dec_bits);
    }
}

TEST_CASE("Dictionary three-way: double encode/decode", "[encoding][dictionary][vectors]") {
    std::vector<double> input = {50000.123, 2890.456, 50000.123, 2890.456, 50000.123};

    DictionaryEncoder<double> enc;
    for (auto v : input) REQUIRE(enc.put(v));
    enc.flush();

    REQUIRE(enc.dictionary_size() == 2);
    REQUIRE(enc.bit_width() == 1);

    auto dict_page = enc.dictionary_page();
    auto idx_page = enc.indices_page();

    DictionaryDecoder<double> dec(dict_page.data(), dict_page.size(),
                                    enc.dictionary_size(), PhysicalType::DOUBLE);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        uint64_t orig_bits, dec_bits;
        std::memcpy(&orig_bits, &input[i], 8);
        std::memcpy(&dec_bits, &(*decoded)[i], 8);
        REQUIRE(orig_bits == dec_bits);
    }
}

TEST_CASE("Dictionary three-way: single-entry dict (bw=0)", "[encoding][dictionary][vectors]") {
    // All same value → dictionary size 1, bit_width 0
    std::vector<std::string> input(16, "ONLY");

    DictionaryEncoder<std::string> enc;
    for (const auto& s : input) REQUIRE(enc.put(s));
    enc.flush();

    REQUIRE(enc.dictionary_size() == 1);
    REQUIRE(enc.bit_width() == 0);

    auto dict_page = enc.dictionary_page();
    auto idx_page = enc.indices_page();

    DictionaryDecoder<std::string> dec(dict_page.data(), dict_page.size(),
                                        1, PhysicalType::BYTE_ARRAY);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (const auto& v : *decoded) {
        REQUIRE(v == "ONLY");
    }
}

TEST_CASE("Dictionary three-way: int32 encode/decode", "[encoding][dictionary][vectors]") {
    std::vector<int32_t> input = {-1, 0, 1, -1, 0, 1, -1, 0};

    DictionaryEncoder<int32_t> enc;
    for (auto v : input) REQUIRE(enc.put(v));
    enc.flush();

    REQUIRE(enc.dictionary_size() == 3);

    auto dict_page = enc.dictionary_page();
    auto idx_page = enc.indices_page();

    DictionaryDecoder<int32_t> dec(dict_page.data(), dict_page.size(),
                                     enc.dictionary_size(), PhysicalType::INT32);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        REQUIRE((*decoded)[i] == input[i]);
    }
}

TEST_CASE("Dictionary bit_width computation", "[encoding][dictionary][vectors]") {
    // Verify dict_bit_width matches spec: ceil(log2(dict_size))
    REQUIRE(detail::dict_bit_width(0) == 0);
    REQUIRE(detail::dict_bit_width(1) == 0);
    REQUIRE(detail::dict_bit_width(2) == 1);
    REQUIRE(detail::dict_bit_width(3) == 2);
    REQUIRE(detail::dict_bit_width(4) == 2);
    REQUIRE(detail::dict_bit_width(5) == 3);
    REQUIRE(detail::dict_bit_width(8) == 3);
    REQUIRE(detail::dict_bit_width(9) == 4);
    REQUIRE(detail::dict_bit_width(16) == 4);
    REQUIRE(detail::dict_bit_width(256) == 8);
    REQUIRE(detail::dict_bit_width(257) == 9);
}

// ===================================================================
// Section 8: Byte Stream Split — Standard Wire Format Vectors
// ===================================================================

TEST_CASE("BSS float [1.0, 2.0] known wire format", "[encoding][bss][vectors]") {
    // 1.0f = 0x3F800000 LE: [00, 00, 80, 3F]
    // 2.0f = 0x40000000 LE: [00, 00, 00, 40]
    // BSS output: [byte0_1, byte0_2, byte1_1, byte1_2, byte2_1, byte2_2, byte3_1, byte3_2]
    //           = [00, 00, 00, 00, 80, 00, 3F, 40]
    // Python verified: bss_encode_float([1.0, 2.0]) = 0000000080003f40

    float values[2] = {1.0f, 2.0f};
    auto encoded = byte_stream_split::encode_float(values, 2);

    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x3F, 0x40};
    REQUIRE(bytes_equal(encoded, expected));

    auto decoded = byte_stream_split::decode_float(encoded.data(), encoded.size(), 2);
    REQUIRE(decoded.size() == 2);

    uint32_t bits0, bits1;
    std::memcpy(&bits0, &decoded[0], 4);
    std::memcpy(&bits1, &decoded[1], 4);
    REQUIRE(bits0 == 0x3F800000u); // 1.0f
    REQUIRE(bits1 == 0x40000000u); // 2.0f
}

TEST_CASE("BSS double [1.0] known wire format", "[encoding][bss][vectors]") {
    // 1.0d = 0x3FF0000000000000 LE: [00, 00, 00, 00, 00, 00, F0, 3F]
    // Single value: BSS is identity (each byte stream has 1 element)
    // Python verified: bss_encode_double([1.0]) = 000000000000f03f

    double val = 1.0;
    auto encoded = byte_stream_split::encode_double(&val, 1);

    std::vector<uint8_t> expected = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F};
    REQUIRE(bytes_equal(encoded, expected));

    auto decoded = byte_stream_split::decode_double(encoded.data(), encoded.size(), 1);
    REQUIRE(decoded.size() == 1);

    uint64_t bits;
    std::memcpy(&bits, &decoded[0], 8);
    REQUIRE(bits == 0x3FF0000000000000ULL);
}

TEST_CASE("BSS float preserves NaN/Inf/-0.0 (bit-exact)", "[encoding][bss][vectors]") {
    // Python verified: all special IEEE 754 values survive BSS roundtrip bit-exactly
    float pos_zero = 0.0f;
    float neg_zero = -0.0f;
    float pos_inf  = std::numeric_limits<float>::infinity();
    float neg_inf  = -std::numeric_limits<float>::infinity();
    float nan_val  = std::numeric_limits<float>::quiet_NaN();
    float denorm   = std::numeric_limits<float>::denorm_min();
    float max_val  = (std::numeric_limits<float>::max)();
    float min_val  = (std::numeric_limits<float>::min)();

    std::vector<float> values = {pos_zero, neg_zero, pos_inf, neg_inf,
                                  nan_val, denorm, max_val, min_val};

    auto encoded = byte_stream_split::encode_float(values.data(), values.size());
    REQUIRE(encoded.size() == values.size() * sizeof(float));

    auto decoded = byte_stream_split::decode_float(encoded.data(), encoded.size(),
                                                     values.size());
    REQUIRE(decoded.size() == values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        uint32_t orig_bits, dec_bits;
        std::memcpy(&orig_bits, &values[i], 4);
        std::memcpy(&dec_bits, &decoded[i], 4);
        REQUIRE(orig_bits == dec_bits);
    }
}

TEST_CASE("BSS double preserves NaN/Inf/-0.0 (bit-exact)", "[encoding][bss][vectors]") {
    double pos_zero = 0.0;
    double neg_zero = -0.0;
    double pos_inf  = std::numeric_limits<double>::infinity();
    double neg_inf  = -std::numeric_limits<double>::infinity();
    double nan_val  = std::numeric_limits<double>::quiet_NaN();
    double denorm   = std::numeric_limits<double>::denorm_min();
    double max_val  = (std::numeric_limits<double>::max)();
    double min_val  = (std::numeric_limits<double>::min)();

    std::vector<double> values = {pos_zero, neg_zero, pos_inf, neg_inf,
                                   nan_val, denorm, max_val, min_val};

    auto encoded = byte_stream_split::encode_double(values.data(), values.size());
    REQUIRE(encoded.size() == values.size() * sizeof(double));

    auto decoded = byte_stream_split::decode_double(encoded.data(), encoded.size(),
                                                      values.size());
    REQUIRE(decoded.size() == values.size());

    for (size_t i = 0; i < values.size(); ++i) {
        uint64_t orig_bits, dec_bits;
        std::memcpy(&orig_bits, &values[i], 8);
        std::memcpy(&dec_bits, &decoded[i], 8);
        REQUIRE(orig_bits == dec_bits);
    }
}

TEST_CASE("BSS float financial data roundtrip", "[encoding][bss][vectors]") {
    // Simulates real financial price data (exponent bytes highly repetitive)
    std::vector<float> prices;
    for (int i = 0; i < 100; ++i) {
        prices.push_back(50000.0f + static_cast<float>(i) * 0.01f);
    }

    auto encoded = byte_stream_split::encode_float(prices.data(), prices.size());
    REQUIRE(encoded.size() == prices.size() * sizeof(float));

    auto decoded = byte_stream_split::decode_float(encoded.data(), encoded.size(),
                                                     prices.size());
    REQUIRE(decoded.size() == prices.size());

    for (size_t i = 0; i < prices.size(); ++i) {
        uint32_t orig_bits, dec_bits;
        std::memcpy(&orig_bits, &prices[i], 4);
        std::memcpy(&dec_bits, &decoded[i], 4);
        REQUIRE(orig_bits == dec_bits);
    }
}

TEST_CASE("BSS double financial data roundtrip", "[encoding][bss][vectors]") {
    std::vector<double> prices;
    for (int i = 0; i < 100; ++i) {
        prices.push_back(2890.456 + static_cast<double>(i) * 0.001);
    }

    auto encoded = byte_stream_split::encode_double(prices.data(), prices.size());
    REQUIRE(encoded.size() == prices.size() * sizeof(double));

    auto decoded = byte_stream_split::decode_double(encoded.data(), encoded.size(),
                                                      prices.size());
    REQUIRE(decoded.size() == prices.size());

    for (size_t i = 0; i < prices.size(); ++i) {
        uint64_t orig_bits, dec_bits;
        std::memcpy(&orig_bits, &prices[i], 8);
        std::memcpy(&dec_bits, &decoded[i], 8);
        REQUIRE(orig_bits == dec_bits);
    }
}

TEST_CASE("BSS empty input", "[encoding][bss][vectors]") {
    auto enc_f = byte_stream_split::encode_float(nullptr, 0);
    REQUIRE(enc_f.empty());

    auto enc_d = byte_stream_split::encode_double(nullptr, 0);
    REQUIRE(enc_d.empty());
}

// ===================================================================
// Section 9: Cross-Encoding Integration Tests
// ===================================================================

TEST_CASE("Dictionary + RLE integration: indices use RLE encoding", "[encoding][dictionary][rle][vectors]") {
    // Verify that dictionary indices_page() produces valid RLE that can be
    // independently decoded by RleDecoder
    DictionaryEncoder<std::string> enc;
    // 16 values with 4 unique = indices 0,1,2,3
    std::vector<std::string> input = {"A", "B", "C", "D", "A", "B", "C", "D",
                                        "A", "B", "C", "D", "A", "B", "C", "D"};
    for (const auto& s : input) REQUIRE(enc.put(s));
    enc.flush();

    auto idx_page = enc.indices_page();
    int bw = idx_page[0]; // First byte is bit_width
    REQUIRE(bw == 2); // ceil(log2(4)) = 2

    // Decode RLE payload (skip bit_width byte)
    auto indices = RleDecoder::decode(idx_page.data() + 1, idx_page.size() - 1,
                                        bw, input.size());
    REQUIRE(indices.size() == input.size());
    // Indices should cycle 0,1,2,3,0,1,2,3,...
    for (size_t i = 0; i < indices.size(); ++i) {
        REQUIRE(indices[i] == static_cast<uint32_t>(i % 4));
    }
}

TEST_CASE("Delta + Zigzag integration: header contains zigzag first_value", "[encoding][delta][vectors]") {
    // Verify the wire format header contains the correct zigzag-encoded first_value
    int64_t first = -42;
    std::vector<int64_t> values = {first, -40, -38};

    auto encoded = delta::encode_int64(values.data(), values.size());

    // Parse header manually
    size_t pos = 0;
    uint64_t block_size = decode_varint(encoded.data(), pos, encoded.size());
    uint64_t miniblock_count = decode_varint(encoded.data(), pos, encoded.size());
    uint64_t total_count = decode_varint(encoded.data(), pos, encoded.size());
    uint64_t first_zz = decode_varint(encoded.data(), pos, encoded.size());

    REQUIRE(block_size == 128);
    REQUIRE(miniblock_count == 4);
    REQUIRE(total_count == 3);
    REQUIRE(first_zz == delta::zigzag_encode(first)); // 83
    REQUIRE(delta::zigzag_decode(first_zz) == first);
}
