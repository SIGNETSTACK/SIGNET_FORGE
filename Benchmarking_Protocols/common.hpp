// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// Benchmarking_Protocols/common.hpp — Shared infrastructure for enterprise benchmarks
//
// Provides: TempDir, tick schema, CSV loader, WriterOptions helpers, file-size util.
// All 9 bench_phase*.cpp files include this header.

#pragma once

#include "signet/forge.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace bench {

namespace fs = std::filesystem;
using namespace signet::forge;

// ===========================================================================
// TempDir — RAII directory that removes itself on destruction
// ===========================================================================
struct TempDir {
    std::string path;

    explicit TempDir(const char* prefix = "signet_ebench_") {
        path = std::string("/tmp/") + prefix +
               std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count());
        std::error_code ec;
        fs::create_directories(path, ec);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }

    std::string file(const std::string& name) const {
        return path + "/" + name;
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// ===========================================================================
// File size helper
// ===========================================================================
inline size_t file_size_bytes(const std::string& path) {
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    return ec ? 0 : static_cast<size_t>(sz);
}

// ===========================================================================
// Tick data schema (9 columns — matches ticks.csv)
// ===========================================================================
// timestamp_ms(INT64), symbol(STRING), exchange(STRING),
// bid_price(DOUBLE), ask_price(DOUBLE), bid_qty(DOUBLE),
// ask_qty(DOUBLE), spread_bps(DOUBLE), mid_price(DOUBLE)

static constexpr size_t NUM_TICK_COLS = 9;

enum TickCol : size_t {
    TS      = 0,
    SYMBOL  = 1,
    EXCH    = 2,
    BID     = 3,
    ASK     = 4,
    BQTY    = 5,
    AQTY    = 6,
    SPREAD  = 7,
    MID     = 8,
};

inline Schema tick_schema() {
    return Schema::builder("market_ticks")
        .column<int64_t>("timestamp_ms")
        .column<std::string>("symbol")
        .column<std::string>("exchange")
        .column<double>("bid_price")
        .column<double>("ask_price")
        .column<double>("bid_qty")
        .column<double>("ask_qty")
        .column<double>("spread_bps")
        .column<double>("mid_price")
        .build();
}

// ===========================================================================
// TickData — parallel column vectors for tick data
// ===========================================================================
struct TickData {
    std::vector<int64_t>     ts;
    std::vector<std::string> symbol;
    std::vector<std::string> exchange;
    std::vector<double>      bid;
    std::vector<double>      ask;
    std::vector<double>      bid_qty;
    std::vector<double>      ask_qty;
    std::vector<double>      spread;
    std::vector<double>      mid;

    size_t size() const { return ts.size(); }

    void reserve(size_t n) {
        ts.reserve(n);
        symbol.reserve(n);
        exchange.reserve(n);
        bid.reserve(n);
        ask.reserve(n);
        bid_qty.reserve(n);
        ask_qty.reserve(n);
        spread.reserve(n);
        mid.reserve(n);
    }

    void clear() {
        ts.clear(); symbol.clear(); exchange.clear();
        bid.clear(); ask.clear(); bid_qty.clear();
        ask_qty.clear(); spread.clear(); mid.clear();
    }
};

// ===========================================================================
// CSV loader — loads tick data from a plain CSV file (no gzip)
// ===========================================================================
inline TickData load_ticks(const std::string& csv_path, size_t max_rows = 0) {
    TickData data;

    FILE* fp = fopen(csv_path.c_str(), "r");
    if (!fp) {
        std::cerr << "[bench] Cannot open tick CSV: " << csv_path << "\n";
        return data;
    }

    if (max_rows > 0) data.reserve(max_rows);

    char linebuf[1024];
    bool header_done = false;

    while (fgets(linebuf, sizeof(linebuf), fp)) {
        size_t len = strlen(linebuf);
        if (!header_done) {
            header_done = true;
            continue;  // skip header
        }
        if (len <= 1) continue;

        // Parse 9 comma-separated fields
        std::array<std::string_view, NUM_TICK_COLS> fields;
        size_t f = 0;
        const char* start = linebuf;
        for (size_t i = 0; i <= len; ++i) {
            if (linebuf[i] == ',' || linebuf[i] == '\0' ||
                linebuf[i] == '\n' || linebuf[i] == '\r') {
                if (f >= NUM_TICK_COLS) break;
                fields[f++] = std::string_view(start, linebuf + i - start);
                start = linebuf + i + 1;
                if (f == NUM_TICK_COLS) break;
            }
        }
        if (f != NUM_TICK_COLS) continue;

        try {
            data.ts.push_back(std::stoll(std::string(fields[TS])));
            data.symbol.emplace_back(fields[SYMBOL]);
            data.exchange.emplace_back(fields[EXCH]);
            data.bid.push_back(std::stod(std::string(fields[BID])));
            data.ask.push_back(std::stod(std::string(fields[ASK])));
            data.bid_qty.push_back(std::stod(std::string(fields[BQTY])));
            data.ask_qty.push_back(std::stod(std::string(fields[AQTY])));
            data.spread.push_back(std::stod(std::string(fields[SPREAD])));
            data.mid.push_back(std::stod(std::string(fields[MID])));
        } catch (...) {
            continue;
        }

        if (max_rows > 0 && data.size() >= max_rows) break;
    }

    fclose(fp);
    return data;
}

// ===========================================================================
// Synthetic tick data generator (for when CSV data is not available)
// ===========================================================================
inline TickData generate_ticks(size_t n_rows) {
    TickData data;
    data.reserve(n_rows);

    const std::array<std::string, 6> symbols = {
        "BTCUSDT", "ETHUSDT", "SOLUSDT", "BNBUSDT", "XRPUSDT", "ADAUSDT"
    };
    const std::array<std::string, 3> exchanges = {
        "BINANCE", "OKX", "BYBIT"
    };

    int64_t base_ts = 1706780400000LL;  // 2024-02-01 00:00:00 UTC in ms

    for (size_t i = 0; i < n_rows; ++i) {
        data.ts.push_back(base_ts + static_cast<int64_t>(i));
        data.symbol.push_back(symbols[i % symbols.size()]);
        data.exchange.push_back(exchanges[i % exchanges.size()]);

        double base_price = 45000.0 + static_cast<double>(i % 10000) * 0.01;
        double spread = 0.5 + static_cast<double>(i % 100) * 0.01;
        data.bid.push_back(base_price);
        data.ask.push_back(base_price + spread);
        data.bid_qty.push_back(0.001 + static_cast<double>(i % 1000) * 0.001);
        data.ask_qty.push_back(0.002 + static_cast<double>(i % 500) * 0.001);
        data.spread.push_back(spread * 100.0 / base_price);  // spread in bps
        data.mid.push_back(base_price + spread / 2.0);
    }

    return data;
}

// ===========================================================================
// Try to load real data, fall back to synthetic
// ===========================================================================
inline TickData load_or_generate(const std::string& data_dir, size_t n_rows) {
    // Try standard filenames
    std::string suffix;
    if (n_rows <= 1000)         suffix = "1k";
    else if (n_rows <= 100000)  suffix = "100k";
    else if (n_rows <= 1000000) suffix = "1m";
    else                        suffix = "10m";

    std::string csv_path = data_dir + "/ticks_" + suffix + ".csv";
    if (fs::exists(csv_path)) {
        auto data = load_ticks(csv_path, n_rows);
        if (data.size() > 0) return data;
    }

    // Fall back to synthetic
    return generate_ticks(n_rows);
}

// ===========================================================================
// Write tick data to a Parquet file
// ===========================================================================
inline bool write_tick_parquet(const std::string& path,
                               const TickData& data,
                               WriterOptions opts,
                               size_t rg_size = 128 * 1024) {
    auto schema = tick_schema();
    opts.row_group_size = static_cast<int64_t>(rg_size);

    auto writer_result = ParquetWriter::open(path, schema, opts);
    if (!writer_result) return false;
    auto& writer = *writer_result;

    size_t n = data.size();
    size_t offset = 0;
    while (offset < n) {
        size_t chunk = (std::min)(rg_size, n - offset);
        auto i = static_cast<int64_t>(chunk);

        (void)writer.write_column(TickCol::TS,     data.ts.data()       + offset, i);
        (void)writer.write_column(TickCol::SYMBOL, data.symbol.data()   + offset, i);
        (void)writer.write_column(TickCol::EXCH,   data.exchange.data() + offset, i);
        (void)writer.write_column(TickCol::BID,    data.bid.data()      + offset, i);
        (void)writer.write_column(TickCol::ASK,    data.ask.data()      + offset, i);
        (void)writer.write_column(TickCol::BQTY,   data.bid_qty.data()  + offset, i);
        (void)writer.write_column(TickCol::AQTY,   data.ask_qty.data()  + offset, i);
        (void)writer.write_column(TickCol::SPREAD, data.spread.data()   + offset, i);
        (void)writer.write_column(TickCol::MID,    data.mid.data()      + offset, i);
        (void)writer.flush_row_group();
        offset += chunk;
    }

    (void)writer.close();
    return true;
}

// ===========================================================================
// WriterOptions presets
// ===========================================================================

inline WriterOptions plain_opts() {
    WriterOptions opts;
    opts.compression      = Compression::UNCOMPRESSED;
    opts.default_encoding = Encoding::PLAIN;
    return opts;
}

inline WriterOptions snappy_opts() {
    WriterOptions opts;
    opts.compression      = Compression::SNAPPY;
    opts.default_encoding = Encoding::PLAIN;
    return opts;
}

inline WriterOptions zstd_opts() {
    WriterOptions opts;
    opts.compression      = Compression::ZSTD;
    opts.default_encoding = Encoding::PLAIN;
    return opts;
}

inline WriterOptions lz4_opts() {
    WriterOptions opts;
    opts.compression      = Compression::LZ4_RAW;
    opts.default_encoding = Encoding::PLAIN;
    return opts;
}

inline WriterOptions gzip_opts() {
    WriterOptions opts;
    opts.compression      = Compression::GZIP;
    opts.default_encoding = Encoding::PLAIN;
    return opts;
}

// Optimal encoding: auto_encoding picks DELTA for int64, BSS for double,
// RLE_DICT for strings
inline WriterOptions optimal_snappy_opts() {
    WriterOptions opts;
    opts.compression    = Compression::SNAPPY;
    opts.auto_encoding  = true;
    opts.enable_bloom_filter = true;
    opts.enable_page_index   = true;
    return opts;
}

inline WriterOptions optimal_zstd_opts() {
    WriterOptions opts;
    opts.compression    = Compression::ZSTD;
    opts.auto_encoding  = true;
    opts.enable_bloom_filter = true;
    opts.enable_page_index   = true;
    return opts;
}

// Data directory default path (relative to Benchmarking_Protocols/)
inline std::string default_data_dir() {
    // Try the environment variable first
    if (const char* env = std::getenv("SIGNET_BENCH_DATA_DIR"))
        return env;
    // Fall back to the co-located data/ directory (set by CMake)
#ifdef SIGNET_BENCH_DATA_DIR
    return std::string(SIGNET_BENCH_DATA_DIR);
#else
    return std::string("data");
#endif
}

} // namespace bench
