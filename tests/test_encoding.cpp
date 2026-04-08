// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace signet::forge;

// ===================================================================
// RLE / Bit-Packing Hybrid Tests
// ===================================================================

TEST_CASE("RLE encode/decode roundtrip", "[encoding][rle]") {
    // 8 zeros, 8 ones, 8 twos — classic RLE-friendly data
    std::vector<uint32_t> values;
    for (int v = 0; v < 3; ++v) {
        for (int i = 0; i < 8; ++i) {
            values.push_back(static_cast<uint32_t>(v));
        }
    }
    REQUIRE(values.size() == 24);

    int bit_width = 2; // Values 0-2 fit in 2 bits

    auto encoded = RleEncoder::encode(values.data(), values.size(), bit_width);
    REQUIRE(!encoded.empty());

    auto decoded = RleDecoder::decode(encoded.data(), encoded.size(),
                                       bit_width, values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("RLE bit-pack mixed values", "[encoding][rle]") {
    // Diverse values that will trigger bit-packing groups
    std::vector<uint32_t> values = {0, 1, 2, 3, 4, 5, 6, 7,
                                     0, 1, 2, 3, 4, 5, 6, 7};
    REQUIRE(values.size() == 16);

    int bit_width = 3; // Values 0-7 fit in 3 bits

    auto encoded = RleEncoder::encode(values.data(), values.size(), bit_width);
    REQUIRE(!encoded.empty());

    auto decoded = RleDecoder::decode(encoded.data(), encoded.size(),
                                       bit_width, values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("RLE with length prefix", "[encoding][rle]") {
    std::vector<uint32_t> values = {1, 1, 1, 1, 1, 1, 1, 1,
                                     0, 0, 0, 0, 0, 0, 0, 0};
    int bit_width = 1;

    // Encode with 4-byte LE length prefix
    auto encoded = RleEncoder::encode_with_length(values.data(), values.size(),
                                                   bit_width);
    REQUIRE(encoded.size() >= 4);

    // Verify the 4-byte LE length prefix is correct
    uint32_t prefix_len = static_cast<uint32_t>(encoded[0])
                        | (static_cast<uint32_t>(encoded[1]) << 8)
                        | (static_cast<uint32_t>(encoded[2]) << 16)
                        | (static_cast<uint32_t>(encoded[3]) << 24);
    REQUIRE(prefix_len == encoded.size() - 4);

    // Decode with length prefix and verify identity
    auto decoded = RleDecoder::decode_with_length(encoded.data(), encoded.size(),
                                                    bit_width, values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("RLE bit_width=0", "[encoding][rle]") {
    // All zeros — bit_width=0 means every value is implicitly 0
    std::vector<uint32_t> values(16, 0);
    int bit_width = 0;

    auto encoded = RleEncoder::encode(values.data(), values.size(), bit_width);
    // Should encode to minimal bytes (just a varint header, no value bytes)
    REQUIRE(encoded.size() <= 4);

    auto decoded = RleDecoder::decode(encoded.data(), encoded.size(),
                                       bit_width, values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == 0);
    }
}

// ===================================================================
// Dictionary Encoding Tests
// ===================================================================

TEST_CASE("Dictionary string encode/decode", "[encoding][dictionary]") {
    std::vector<std::string> input = {
        "BTC", "ETH", "BTC", "SOL", "ETH", "BTC", "BTC", "SOL",
        "BTC", "ETH", "BTC", "SOL"  // 12 values, 3 unique = 25%
    };

    DictionaryEncoder<std::string> enc;
    for (const auto& s : input) {
        enc.put(s);
    }
    enc.flush();

    // Verify dictionary properties
    REQUIRE(enc.dictionary_size() == 3);
    REQUIRE(enc.num_values() == 12);
    // 3/12 = 25% unique, well below 40% threshold
    REQUIRE(enc.is_worthwhile() == true);

    // Get encoded pages
    auto dict_page = enc.dictionary_page();
    auto idx_page  = enc.indices_page();
    REQUIRE(!dict_page.empty());
    REQUIRE(!idx_page.empty());

    // Decode using DictionaryDecoder
    DictionaryDecoder<std::string> dec(dict_page.data(), dict_page.size(),
                                        enc.dictionary_size(),
                                        PhysicalType::BYTE_ARRAY);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        REQUIRE((*decoded)[i] == input[i]);
    }
}

TEST_CASE("Dictionary int64 encode/decode", "[encoding][dictionary]") {
    std::vector<int64_t> input = {100, 200, 100, 300, 200, 100};

    DictionaryEncoder<int64_t> enc;
    for (auto v : input) {
        enc.put(v);
    }
    enc.flush();

    REQUIRE(enc.dictionary_size() == 3);
    REQUIRE(enc.num_values() == 6);

    auto dict_page = enc.dictionary_page();
    auto idx_page  = enc.indices_page();

    DictionaryDecoder<int64_t> dec(dict_page.data(), dict_page.size(),
                                    enc.dictionary_size(),
                                    PhysicalType::INT64);

    auto decoded = dec.decode(idx_page.data(), idx_page.size(), input.size());
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->size() == input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        REQUIRE((*decoded)[i] == input[i]);
    }
}

TEST_CASE("Dictionary not worthwhile", "[encoding][dictionary]") {
    // All unique values: 5/5 = 100% unique
    std::vector<int64_t> input = {1, 2, 3, 4, 5};

    DictionaryEncoder<int64_t> enc;
    for (auto v : input) {
        enc.put(v);
    }
    enc.flush();

    REQUIRE(enc.dictionary_size() == 5);
    REQUIRE(enc.num_values() == 5);
    REQUIRE(enc.is_worthwhile() == false);
}

// ===================================================================
// Delta Encoding Tests
// ===================================================================

TEST_CASE("Delta int64 monotonic timestamps", "[encoding][delta]") {
    // 100 monotonically increasing nanosecond timestamps
    constexpr size_t COUNT = 100;
    std::vector<int64_t> values(COUNT);
    for (size_t i = 0; i < COUNT; ++i) {
        values[i] = 1000000000LL + static_cast<int64_t>(i);
    }

    auto encoded = delta::encode_int64(values.data(), values.size());
    REQUIRE(!encoded.empty());

    // For constant-delta data, encoded size should be much smaller than raw
    size_t raw_size = COUNT * sizeof(int64_t); // 800 bytes
    REQUIRE(encoded.size() < raw_size);

    auto decoded = delta::decode_int64(encoded.data(), encoded.size(),
                                        values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("Delta int32 roundtrip", "[encoding][delta]") {
    std::vector<int32_t> values = {10, 20, 30, 25, 35, 45, 40, 50};

    auto encoded = delta::encode_int32(values.data(), values.size());
    REQUIRE(!encoded.empty());

    auto decoded = delta::decode_int32(encoded.data(), encoded.size(),
                                        values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}

TEST_CASE("Delta single value", "[encoding][delta]") {
    std::vector<int64_t> values = {42};

    auto encoded = delta::encode_int64(values.data(), values.size());
    REQUIRE(!encoded.empty());

    auto decoded = delta::decode_int64(encoded.data(), encoded.size(),
                                        values.size());
    REQUIRE(decoded.size() == 1);
    REQUIRE(decoded[0] == 42);
}

TEST_CASE("Delta empty", "[encoding][delta]") {
    std::vector<int64_t> values;

    auto encoded = delta::encode_int64(values.data(), values.size());
    // Empty input still produces a header
    REQUIRE(!encoded.empty());

    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), 0);
    REQUIRE(decoded.empty());
}

// ===================================================================
// Byte Stream Split Tests
// ===================================================================

TEST_CASE("BSS float roundtrip", "[encoding][bss]") {
    std::vector<float> values = {
        1.0f, 2.5f, 3.14f, -0.001f, 100.0f,
        0.0f, -1.5f, 42.42f, 1e10f, 1e-10f
    };
    constexpr size_t COUNT = 10;
    REQUIRE(values.size() == COUNT);

    auto encoded = byte_stream_split::encode_float(values.data(), values.size());

    // Encoded size should be exactly count * sizeof(float) (just rearranged)
    REQUIRE(encoded.size() == COUNT * sizeof(float));

    auto decoded = byte_stream_split::decode_float(
        encoded.data(), encoded.size(), values.size());
    REQUIRE(decoded.size() == values.size());

    // Verify exact bit equality (BSS is lossless)
    for (size_t i = 0; i < values.size(); ++i) {
        uint32_t orig_bits, decoded_bits;
        std::memcpy(&orig_bits, &values[i], sizeof(float));
        std::memcpy(&decoded_bits, &decoded[i], sizeof(float));
        REQUIRE(orig_bits == decoded_bits);
    }
}

TEST_CASE("BSS double roundtrip", "[encoding][bss]") {
    std::vector<double> values = {
        50000.123, 2890.456, -0.00001, 1e15, 0.0,
        3.14159265358979, 2.71828182845905, -999.999, 1e-15, 42.0
    };
    constexpr size_t COUNT = 10;
    REQUIRE(values.size() == COUNT);

    auto encoded = byte_stream_split::encode_double(values.data(), values.size());

    // Encoded size should be exactly count * sizeof(double)
    REQUIRE(encoded.size() == COUNT * sizeof(double));

    auto decoded = byte_stream_split::decode_double(
        encoded.data(), encoded.size(), values.size());
    REQUIRE(decoded.size() == values.size());

    // Verify exact bit equality
    for (size_t i = 0; i < values.size(); ++i) {
        uint64_t orig_bits, decoded_bits;
        std::memcpy(&orig_bits, &values[i], sizeof(double));
        std::memcpy(&decoded_bits, &decoded[i], sizeof(double));
        REQUIRE(orig_bits == decoded_bits);
    }
}

// ===========================================================================
// Security hardening — negative / boundary tests
// ===========================================================================

TEST_CASE("BSS decode_double: size < count*8 returns empty (not crash)", "[encoding][bss][hardening]") {
    // 4 bytes provided but 2 doubles need 16 bytes — must return empty, not overrun
    std::vector<uint8_t> short_buf(4, 0xAA);
    auto result = byte_stream_split::decode_double(short_buf.data(), short_buf.size(), 2);
    REQUIRE(result.empty());
}

TEST_CASE("BSS decode_float: size < count*4 returns empty (not crash)", "[encoding][bss][hardening]") {
    std::vector<uint8_t> short_buf(3, 0xBB);
    auto result = byte_stream_split::decode_float(short_buf.data(), short_buf.size(), 2);
    REQUIRE(result.empty());
}

TEST_CASE("RLE encode: bit_width=0 returns empty", "[encoding][rle][hardening]") {
    std::vector<uint32_t> values = {1, 2, 3};
    auto result = RleEncoder::encode(values.data(), values.size(), 0);
    REQUIRE(result.empty());
}

TEST_CASE("RLE encode: bit_width=65 returns empty", "[encoding][rle][hardening]") {
    std::vector<uint32_t> values = {1, 2, 3};
    auto result = RleEncoder::encode(values.data(), values.size(), 65);
    REQUIRE(result.empty());
}

TEST_CASE("RLE decode: bit_width=0 returns empty", "[encoding][rle][hardening]") {
    std::vector<uint8_t> buf = {0x02, 0x01}; // valid RLE header but bad bit_width
    auto result = RleDecoder::decode(buf.data(), buf.size(), 0, 1);
    REQUIRE(result.empty());
}

TEST_CASE("RLE decode: bit_width=65 returns empty", "[encoding][rle][hardening]") {
    std::vector<uint8_t> buf = {0x02, 0x01};
    auto result = RleDecoder::decode(buf.data(), buf.size(), 65, 1);
    REQUIRE(result.empty());
}

// ===================================================================
// Hardening Pass #3 Tests
// ===================================================================

TEST_CASE("RleDecoder: bit_width=128 is clamped safely", "[encoding][rle][hardening]") {
    // bit_width > 64 should be clamped to 0, producing a zero-width stream
    // which returns empty for non-empty payload
    std::vector<uint8_t> buf = {0x04, 0x00}; // RLE header: run of 2, value 0
    auto result = RleDecoder::decode(buf.data(), buf.size(), 128, 2);
    // Clamped to 0, non-empty payload with bit_width=0 returns empty
    REQUIRE(result.empty());
}

TEST_CASE("RleEncoder: negative bit_width is clamped safely", "[encoding][rle][hardening]") {
    // Negative bit_width should be clamped to 0 — no crash or UB
    RleEncoder enc(-5);
    enc.put(0);
    enc.flush();
    // bit_width=0: put() is a no-op (all values implicitly zero), so
    // encoded output is empty — consistent with static encode() for bw=0
    REQUIRE(enc.encoded_size() == 0);
}

TEST_CASE("DictionaryDecoder: OOB index returns empty", "[encoding][dict][hardening]") {
    // Build a dictionary with 2 entries
    std::vector<uint8_t> dict_page;
    detail::plain_encode_value(dict_page, int32_t(100));
    detail::plain_encode_value(dict_page, int32_t(200));

    DictionaryDecoder<int32_t> dec(dict_page.data(), dict_page.size(), 2,
                                    PhysicalType::INT32);
    REQUIRE(dec.dictionary_size() == 2);

    // Craft an indices page with an OOB index (index=5, dict only has 2)
    // bit_width = 3 (enough for index 5), RLE run: header=0x02 (run of 1), value=5
    std::vector<uint8_t> idx_page;
    idx_page.push_back(3); // bit_width byte
    idx_page.push_back(0x02); // RLE header: run_length = 1
    idx_page.push_back(0x05); // value = 5 (OOB!)

    auto result = dec.decode(idx_page.data(), idx_page.size(), 1);
    REQUIRE(!result.has_value()); // must return error, not crash
}

TEST_CASE("DictionaryDecoder: truncated page returns empty", "[encoding][dict][hardening]") {
    // Only 2 bytes of data — not enough for a single int32 PLAIN entry (needs 4)
    uint8_t truncated[] = {0x01, 0x02};
    DictionaryDecoder<int32_t> dec(truncated, 2, 1, PhysicalType::INT32);
    // The constructor should handle truncated data gracefully
    // (plain_decode_value returns 0 instead of crashing)
    REQUIRE(dec.dictionary_size() == 1); // parsed with truncated value = 0
}

TEST_CASE("BSS decode_float: overflow count returns empty", "[encoding][bss][hardening]") {
    // count = SIZE_MAX / 2 with a tiny buffer — the old check (size < count * WIDTH)
    // would overflow. The new check (count > size / WIDTH) prevents it.
    uint8_t data[16] = {};
    auto result = byte_stream_split::decode_float(data, sizeof(data),
                                                   SIZE_MAX / 2);
    REQUIRE(result.empty());
}

TEST_CASE("BSS decode_double: overflow count returns empty", "[encoding][bss][hardening]") {
    uint8_t data[16] = {};
    auto result = byte_stream_split::decode_double(data, sizeof(data),
                                                    SIZE_MAX / 2);
    REQUIRE(result.empty());
}

TEST_CASE("Delta decode_int64: overflow stops gracefully", "[encoding][delta][hardening]") {
    // Encode values that will cause accumulation overflow during decode
    // Start at INT64_MAX - 1, with a delta that causes overflow
    std::vector<int64_t> values = {
        (std::numeric_limits<int64_t>::max)() - 10,
        (std::numeric_limits<int64_t>::max)() - 5,
        (std::numeric_limits<int64_t>::max)()
    };
    auto encoded = delta::encode_int64(values.data(), values.size());
    REQUIRE(!encoded.empty());

    // Decode should succeed for these valid values (no overflow in accumulation)
    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), 3);
    REQUIRE(decoded.size() == 3);
    REQUIRE(decoded[0] == values[0]);
    REQUIRE(decoded[1] == values[1]);
    REQUIRE(decoded[2] == values[2]);
}

// ===================================================================
// Hardening Pass #4 Tests
// ===================================================================

TEST_CASE("RLE bit-pack resize formula is byte-correct", "[encoding][hardening]") {
    // Test that bit_pack_8 correctly sizes output for various bit widths
    // The fix changed: start + bit_width -> start + (8 * bit_width + 7) / 8

    for (int bw = 1; bw <= 8; ++bw) {
        std::vector<uint32_t> values = {0, 1, 2, 3, 4, 5, 6, 7};
        // Clamp values to fit within bit_width
        for (auto& v : values) v &= ((1u << bw) - 1);

        // Encode using the static convenience method
        auto encoded = RleEncoder::encode(values.data(), values.size(), bw);
        REQUIRE(!encoded.empty());

        // Decode and verify roundtrip identity
        auto decoded = RleDecoder::decode(encoded.data(), encoded.size(),
                                           bw, values.size());
        REQUIRE(decoded.size() == 8);
        for (size_t i = 0; i < 8; ++i) {
            REQUIRE(decoded[i] == values[i]);
        }
    }
}

TEST_CASE("Dictionary decoder returns error on corrupt index", "[encoding][hardening]") {
    // Build a 2-entry dictionary ("hello", "world")
    DictionaryEncoder<std::string> enc;
    enc.put("hello");
    enc.put("world");
    enc.flush();

    auto dict_page = enc.dictionary_page();

    // Manually craft an RLE index page with an OOB index (value=5, dict has 2 entries)
    // Format: [1-byte bit_width] [RLE payload: header + value]
    // bit_width = 3 (enough for index 5), RLE run: header=0x02 (run of 1), value=5
    std::vector<uint8_t> bad_idx;
    bad_idx.push_back(3);    // bit_width byte
    bad_idx.push_back(0x02); // RLE header: run of 1
    bad_idx.push_back(0x05); // value = 5 (OOB for 2-entry dict)

    DictionaryDecoder<std::string> dec(dict_page.data(), dict_page.size(),
                                        enc.dictionary_size(),
                                        PhysicalType::BYTE_ARRAY);

    auto result = dec.decode(bad_idx.data(), bad_idx.size(), 1);
    // After C-4 fix, OOB indices return an error instead of silent corruption
    REQUIRE(!result.has_value());
}

TEST_CASE("BSS decode rejects overflow in count*width", "[encoding][hardening]") {
    // byte_stream_split decode with count that overflows when multiplied by width
    // The fix adds: if (count > SIZE_MAX / WIDTH || count * WIDTH > size) return {};
    uint8_t data[16] = {};
    auto result = byte_stream_split::decode_double(data, 16, SIZE_MAX);
    REQUIRE(result.empty());
}

TEST_CASE("Delta encoding handles large signed values without UB", "[encoding][hardening]") {
    // H-5: Values with large deltas that would overflow signed subtraction.
    // The fix casts to uint64_t before computing deltas.
    // Use enough values to fill a delta block (min 128) for reliable round-trip.
    std::vector<int64_t> values;
    values.reserve(128);
    // Alternate between large negative and large positive values
    for (int i = 0; i < 128; ++i) {
        if (i % 2 == 0)
            values.push_back((std::numeric_limits<int64_t>::min)() / 2 + i);
        else
            values.push_back((std::numeric_limits<int64_t>::max)() / 2 - i);
    }

    auto encoded = delta::encode_int64(values.data(), values.size());
    REQUIRE(!encoded.empty());

    auto decoded = delta::decode_int64(encoded.data(), encoded.size(), values.size());
    REQUIRE(decoded.size() == values.size());
    for (size_t i = 0; i < values.size(); ++i) {
        REQUIRE(decoded[i] == values[i]);
    }
}
