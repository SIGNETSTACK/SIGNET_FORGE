// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// bench_encodings.cpp — Encoding throughput benchmarks
// Financial data patterns: monotonic timestamps (DELTA), float prices (BSS), boolean flags (RLE)

#include "signet/encoding/rle.hpp"
#include "signet/encoding/delta.hpp"
#include "signet/encoding/byte_stream_split.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace signet::forge;

// ===========================================================================
// TEST_CASE 1 — DELTA int64 timestamps: encode/decode 10K values
// ===========================================================================
// Financial timestamps from a tick stream are nearly perfectly monotonic and
// have small fixed deltas (~100 ns apart).  DELTA encoding reduces such a
// sequence to a handful of unique bit widths + a constant base, achieving
// deep compression before a subsequent ZSTD/LZ4 pass.
//
// Data: 10K nanosecond timestamps starting at 2024-02-01 00:00:00 UTC,
//       spaced exactly 100 ns apart.

TEST_CASE("Encoding: DELTA int64 timestamps — encode 10K", "[encoding][delta][bench]") {
    constexpr size_t N = 10'000;
    constexpr int64_t base_ts  = 1706780400'000'000'000LL; // 2024-02-01 00:00:00 UTC in ns
    constexpr int64_t delta_ns = 100LL;                     // 100 ns per tick

    // Build the timestamp dataset once outside any BENCHMARK.
    std::vector<int64_t> timestamps(N);
    for (size_t i = 0; i < N; ++i) {
        timestamps[i] = base_ts + static_cast<int64_t>(i) * delta_ns;
    }

    // Pre-encode so the decode benchmark has valid input without re-encoding
    // inside the measured loop.
    std::vector<uint8_t> encoded = delta::encode_int64(timestamps.data(), N);

    BENCHMARK("enc_delta_enc_ts_10k") {
        // Return the encoded buffer to prevent elision.
        return delta::encode_int64(timestamps.data(), N);
    };

    BENCHMARK("enc_delta_dec_ts_10k") {
        // Return the decoded vector to prevent elision.
        return delta::decode_int64(encoded.data(), encoded.size(), N);
    };
}

// ===========================================================================
// TEST_CASE 2 — BYTE_STREAM_SPLIT float64 prices: encode/decode 10K values
// ===========================================================================
// BSS interleaves the byte streams of doubles so that each byte-plane is
// individually compressible.  For financial prices clustered in a narrow
// range, the high bytes are almost constant, giving the subsequent LZ4/ZSTD
// pass an easy target.
//
// Data: 10K prices in [40000.00, 50000.00] with 1-cent increments cycling.

TEST_CASE("Encoding: BYTE_STREAM_SPLIT float64 prices — encode 10K", "[encoding][bss][bench]") {
    constexpr size_t N = 10'000;

    // Build price dataset: cycle through a realistic tick-by-tick range.
    std::vector<double> prices(N);
    for (size_t i = 0; i < N; ++i) {
        // Simulate a slow drift from 40000 toward 50000 with sub-cent noise.
        prices[i] = 40000.0 + (static_cast<double>(i % 10000) * 1.0);
    }

    // Pre-encode so the decode benchmark has valid input.
    std::vector<uint8_t> encoded = byte_stream_split::encode_double(prices.data(), N);

    BENCHMARK("enc_bss_enc_px_10k") {
        return byte_stream_split::encode_double(prices.data(), N);
    };

    BENCHMARK("enc_bss_dec_px_10k") {
        return byte_stream_split::decode_double(encoded.data(), encoded.size(), N);
    };
}

// ===========================================================================
// TEST_CASE 3 — RLE boolean flags: encode/decode 10K bit values
// ===========================================================================
// Bid/ask side flags (0 = ask, 1 = bid) are Boolean columns stored at
// bit_width=1.  Market microstructure data is heavily skewed toward one side
// in quote data (e.g. 90 % ask quotes in a passive market-making book), so
// long RLE runs are common and compression is high.
//
// Data: 10K uint32_t values at 0 or 1, with 90 % zeros (ask-side dominant).

TEST_CASE("Encoding: RLE boolean flags — encode 10K bits", "[encoding][rle][bench]") {
    constexpr size_t N = 10'000;

    // 90% zeros, 10% ones — produces long RLE runs in a realistic side-flag column.
    std::vector<uint32_t> flags(N);
    for (size_t i = 0; i < N; ++i) {
        // Every 10th record is a bid (1); the rest are asks (0).
        flags[i] = (i % 10 == 0) ? 1u : 0u;
    }

    // Pre-encode so the decode benchmark has valid input.
    std::vector<uint8_t> encoded = RleEncoder::encode(flags.data(), N, 1);

    BENCHMARK("enc_rle_enc_bw1_10k") {
        return RleEncoder::encode(flags.data(), N, 1);
    };

    BENCHMARK("enc_rle_dec_bw1_10k") {
        return RleDecoder::decode(encoded.data(), encoded.size(), 1, N);
    };
}

// ===========================================================================
// TEST_CASE 4 — DELTA vs PLAIN size comparison: 10K timestamps
// ===========================================================================
// Verifies the core compression claim for DELTA encoding on monotonic data:
// the encoded output must be less than half the size of a raw (PLAIN) copy.
// For 100-ns-spaced int64 timestamps, deltas are all identical (=100), so
// DELTA should compress extremely well — typically below 5 % of PLAIN size.
//
// Also benchmarks both paths so relative throughput is visible in the report.

TEST_CASE("Encoding: DELTA vs PLAIN size comparison — 10K timestamps", "[encoding][delta][bench]") {
    constexpr size_t N = 10'000;
    const size_t     plain_size = N * sizeof(int64_t); // 80,000 bytes

    constexpr int64_t base_ts  = 1706780400'000'000'000LL;
    constexpr int64_t delta_ns = 100LL;

    std::vector<int64_t> timestamps(N);
    for (size_t i = 0; i < N; ++i) {
        timestamps[i] = base_ts + static_cast<int64_t>(i) * delta_ns;
    }

    // Encode once to check the size and to use as the baseline for the
    // decode benchmark below.
    const std::vector<uint8_t> delta_encoded =
        delta::encode_int64(timestamps.data(), N);
    const size_t compressed_size = delta_encoded.size();

    // Core correctness assertion: DELTA must be at least 2x smaller than PLAIN
    // for perfectly monotonic data.
    CHECK(compressed_size < plain_size / 2);

    // Also verify round-trip fidelity at this N.
    {
        auto decoded = delta::decode_int64(delta_encoded.data(),
                                           delta_encoded.size(), N);
        REQUIRE(decoded.size() == N);
        for (size_t i = 0; i < N; ++i) {
            REQUIRE(decoded[i] == timestamps[i]);
        }
    }

    BENCHMARK("enc_delta_ts_10k_plain") {
        return delta::encode_int64(timestamps.data(), N);
    };

    // Plain baseline: a raw memcpy into a same-sized output buffer — this
    // represents the PLAIN encoding cost (data movement only, no transformation).
    BENCHMARK("enc_plain_copy_ts_10k") {
        std::vector<uint8_t> buf(plain_size);
        std::memcpy(buf.data(), timestamps.data(), plain_size);
        // Return buffer to prevent elision.
        return buf;
    };
}

// ===========================================================================
// TEST_CASE 5 — BYTE_STREAM_SPLIT vs PLAIN size: 10K prices
// ===========================================================================
// BSS does NOT compress: it is a lossless byte-plane reorganisation that
// produces output of exactly the same size as the input (N * sizeof(double)
// bytes).  Its purpose is to improve the compressibility of the data for a
// subsequent general-purpose compressor such as LZ4 or ZSTD, not to reduce
// byte count on its own.
//
// This test validates that invariant (BSS output size == PLAIN size) and
// measures encode/decode throughput side by side.

TEST_CASE("Encoding: BYTE_STREAM_SPLIT vs PLAIN size — 10K prices", "[encoding][bss][bench]") {
    constexpr size_t N = 10'000;
    const size_t     plain_size = N * sizeof(double); // 80,000 bytes

    std::vector<double> prices(N);
    for (size_t i = 0; i < N; ++i) {
        prices[i] = 40000.0 + (static_cast<double>(i % 10000) * 1.0);
    }

    const std::vector<uint8_t> bss_encoded =
        byte_stream_split::encode_double(prices.data(), N);

    // BSS is a size-preserving encoding: output must equal N * 8 bytes exactly.
    CHECK(bss_encoded.size() == plain_size);

    // Round-trip fidelity check.
    {
        auto decoded = byte_stream_split::decode_double(
            bss_encoded.data(), bss_encoded.size(), N);
        REQUIRE(decoded.size() == N);
        for (size_t i = 0; i < N; ++i) {
            REQUIRE(decoded[i] == prices[i]);
        }
    }

    BENCHMARK("enc_bss_enc_px_10k_sz") {
        return byte_stream_split::encode_double(prices.data(), N);
    };

    BENCHMARK("enc_bss_dec_px_10k_sz") {
        return byte_stream_split::decode_double(
            bss_encoded.data(), bss_encoded.size(), N);
    };
}
