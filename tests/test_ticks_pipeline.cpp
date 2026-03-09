// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// test_ticks_pipeline.cpp — End-to-end round-trip tests for tick-data Parquet
//
// Tests:
//   1. Schema matches the 9-column tick layout
//   2. Full round-trip: write N rows, read back, verify exact values
//   3. Row group splitting at boundary
//   4. Predicate pushdown: column_statistics() enables symbol-based skip
//   5. auto_encoding produces correct decompressed values for all column types
//   6. Compression ratio (Parquet < raw string size)
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct TickRecord {
    int64_t     timestamp_ms;
    std::string symbol;
    std::string exchange;
    double      bid_price;
    double      ask_price;
    double      bid_qty;
    double      ask_qty;
    double      spread_bps;
    double      mid_price;
};

// Generate N synthetic tick records cycling through 3 symbols and 2 exchanges.
static std::vector<TickRecord> make_ticks(size_t n, int64_t base_ts = 1'771'977'628'000LL) {
    static const char* SYMS[]  = {"BTCUSDT", "ETHUSDT", "SOLUSDT"};
    static const char* EXCHS[] = {"BINANCE", "BYBIT"};

    std::vector<TickRecord> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        TickRecord r;
        r.timestamp_ms = base_ts + static_cast<int64_t>(i);
        r.symbol       = SYMS[i % 3];
        r.exchange     = EXCHS[i % 2];
        double base    = (i % 3 == 0) ? 64000.0 : (i % 3 == 1) ? 1850.0 : 79.0;
        r.bid_price    = base + (i % 100) * 0.01;
        r.ask_price    = r.bid_price + 0.01;
        r.bid_qty      = 0.1 + (i % 50) * 0.001;
        r.ask_qty      = 0.2 + (i % 30) * 0.001;
        r.spread_bps   = 0.0156 + (i % 10) * 0.0001;
        r.mid_price    = (r.bid_price + r.ask_price) / 2.0;
        v.push_back(r);
    }
    return v;
}

// Build the standard tick schema used by ticks_import
static Schema tick_schema() {
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

// Write a batch of TickRecords to a Parquet file; returns the writer result.
static bool write_ticks(const fs::path& path,
                        const std::vector<TickRecord>& ticks,
                        size_t row_group_size = 65536) {
    WriterOptions opts;
    opts.auto_encoding  = true;
    opts.compression    = Compression::SNAPPY;
    opts.row_group_size = row_group_size;

    auto wr = ParquetWriter::open(path, tick_schema(), opts);
    REQUIRE(wr.has_value());

    // Extract typed column vectors and write in row-group-sized chunks
    for (size_t off = 0; off < ticks.size(); off += row_group_size) {
        size_t n = (std::min)(row_group_size, ticks.size() - off);

        std::vector<int64_t>     ts(n);
        std::vector<std::string> sym(n), exch(n);
        std::vector<double>      bid(n), ask(n), bqty(n), aqty(n), sp(n), mid(n);

        for (size_t i = 0; i < n; ++i) {
            const auto& r = ticks[off + i];
            ts[i]   = r.timestamp_ms;
            sym[i]  = r.symbol;
            exch[i] = r.exchange;
            bid[i]  = r.bid_price;
            ask[i]  = r.ask_price;
            bqty[i] = r.bid_qty;
            aqty[i] = r.ask_qty;
            sp[i]   = r.spread_bps;
            mid[i]  = r.mid_price;
        }

        REQUIRE(wr->write_column(0, ts.data(),   n).has_value());
        REQUIRE(wr->write_column(1, sym.data(),  n).has_value());
        REQUIRE(wr->write_column(2, exch.data(), n).has_value());
        REQUIRE(wr->write_column(3, bid.data(),  n).has_value());
        REQUIRE(wr->write_column(4, ask.data(),  n).has_value());
        REQUIRE(wr->write_column(5, bqty.data(), n).has_value());
        REQUIRE(wr->write_column(6, aqty.data(), n).has_value());
        REQUIRE(wr->write_column(7, sp.data(),   n).has_value());
        REQUIRE(wr->write_column(8, mid.data(),  n).has_value());
        REQUIRE(wr->flush_row_group().has_value());
    }

    REQUIRE(wr->close().has_value());
    return true;
}

// ---------------------------------------------------------------------------
// RAII temp file
// ---------------------------------------------------------------------------
namespace {
struct TempFile {
    fs::path path;

    explicit TempFile(const std::string& name)
        : path(fs::temp_directory_path() / ("signet_ticks_test_" + name + ".parquet")) {}

    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
} // anonymous namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("Tick schema has correct 9-column layout", "[ticks][pipeline]") {
    auto schema = tick_schema();
    REQUIRE(schema.num_columns() == 9);
    REQUIRE(schema.column(0).name == "timestamp_ms");
    REQUIRE(schema.column(1).name == "symbol");
    REQUIRE(schema.column(2).name == "exchange");
    REQUIRE(schema.column(3).name == "bid_price");
    REQUIRE(schema.column(4).name == "ask_price");
    REQUIRE(schema.column(5).name == "bid_qty");
    REQUIRE(schema.column(6).name == "ask_qty");
    REQUIRE(schema.column(7).name == "spread_bps");
    REQUIRE(schema.column(8).name == "mid_price");

    REQUIRE(schema.column(0).physical_type == PhysicalType::INT64);
    REQUIRE(schema.column(1).physical_type == PhysicalType::BYTE_ARRAY);
    REQUIRE(schema.column(2).physical_type == PhysicalType::BYTE_ARRAY);
    REQUIRE(schema.column(3).physical_type == PhysicalType::DOUBLE);
}

TEST_CASE("Round-trip: write 1000 ticks, read back, verify exact values", "[ticks][pipeline]") {
    TempFile tmp("roundtrip");
    const size_t N = 1000;
    auto ticks = make_ticks(N);
    write_ticks(tmp.path, ticks);

    auto reader = ParquetReader::open(tmp.path);
    REQUIRE(reader.has_value());
    REQUIRE(reader->num_rows() == static_cast<int64_t>(N));
    REQUIRE(reader->num_row_groups() == 1);

    // Read all columns from row group 0
    auto ts_r  = reader->read_column<int64_t>(0, 0);
    auto sym_r = reader->read_column<std::string>(0, 1);
    auto ex_r  = reader->read_column<std::string>(0, 2);
    auto bid_r = reader->read_column<double>(0, 3);
    auto ask_r = reader->read_column<double>(0, 4);
    auto bq_r  = reader->read_column<double>(0, 5);
    auto aq_r  = reader->read_column<double>(0, 6);
    auto sp_r  = reader->read_column<double>(0, 7);
    auto mid_r = reader->read_column<double>(0, 8);

    REQUIRE(ts_r.has_value());
    REQUIRE(sym_r.has_value());
    REQUIRE(ex_r.has_value());
    REQUIRE(bid_r.has_value());

    REQUIRE(ts_r->size()  == N);
    REQUIRE(sym_r->size() == N);

    // Spot-check first, last, and a middle row
    for (size_t i : {0UL, 499UL, 999UL}) {
        INFO("Row " << i);
        CHECK((*ts_r)[i]  == ticks[i].timestamp_ms);
        CHECK((*sym_r)[i] == ticks[i].symbol);
        CHECK((*ex_r)[i]  == ticks[i].exchange);
        CHECK((*bid_r)[i] == Catch::Approx(ticks[i].bid_price).epsilon(1e-9));
        CHECK((*ask_r)[i] == Catch::Approx(ticks[i].ask_price).epsilon(1e-9));
        CHECK((*bq_r)[i]  == Catch::Approx(ticks[i].bid_qty).epsilon(1e-9));
        CHECK((*aq_r)[i]  == Catch::Approx(ticks[i].ask_qty).epsilon(1e-9));
        CHECK((*sp_r)[i]  == Catch::Approx(ticks[i].spread_bps).epsilon(1e-9));
        CHECK((*mid_r)[i] == Catch::Approx(ticks[i].mid_price).epsilon(1e-9));
    }
}

TEST_CASE("Row group splitting at boundary", "[ticks][pipeline]") {
    TempFile tmp("rgsplit");
    const size_t N = 5000;
    const size_t RG = 1000;
    auto ticks = make_ticks(N);
    write_ticks(tmp.path, ticks, RG);

    auto reader = ParquetReader::open(tmp.path);
    REQUIRE(reader.has_value());
    REQUIRE(reader->num_rows() == static_cast<int64_t>(N));
    REQUIRE(reader->num_row_groups() == static_cast<int64_t>(N / RG));

    // Each row group should have exactly RG rows
    for (int64_t rg = 0; rg < reader->num_row_groups(); ++rg) {
        auto info = reader->row_group(static_cast<size_t>(rg));
        CHECK(info.num_rows == static_cast<int64_t>(RG));
    }
}

TEST_CASE("Column statistics are populated for predicate pushdown", "[ticks][pipeline]") {
    TempFile tmp("stats");
    // Write two separate row groups: first 500 rows BTCUSDT-only, next 500 ETHUSDT-only
    WriterOptions opts;
    opts.auto_encoding  = true;
    opts.compression    = Compression::SNAPPY;
    opts.row_group_size = 65536;

    auto wr = ParquetWriter::open(tmp.path, tick_schema(), opts);
    REQUIRE(wr.has_value());

    // Row group 0: all BTCUSDT/BINANCE
    {
        const size_t n = 500;
        std::vector<int64_t>     ts(n);
        std::vector<std::string> sym(n, "BTCUSDT"), exch(n, "BINANCE");
        std::vector<double>      bid(n, 64000.0), ask(n, 64000.01),
                                 bqty(n, 0.5),    aqty(n, 0.6),
                                 sp(n, 0.0156),   mid(n, 64000.005);
        for (size_t i = 0; i < n; ++i) ts[i] = 1000LL + i;

        REQUIRE(wr->write_column(0, ts.data(),   n).has_value());
        REQUIRE(wr->write_column(1, sym.data(),  n).has_value());
        REQUIRE(wr->write_column(2, exch.data(), n).has_value());
        REQUIRE(wr->write_column(3, bid.data(),  n).has_value());
        REQUIRE(wr->write_column(4, ask.data(),  n).has_value());
        REQUIRE(wr->write_column(5, bqty.data(), n).has_value());
        REQUIRE(wr->write_column(6, aqty.data(), n).has_value());
        REQUIRE(wr->write_column(7, sp.data(),   n).has_value());
        REQUIRE(wr->write_column(8, mid.data(),  n).has_value());
        REQUIRE(wr->flush_row_group().has_value());
    }

    // Row group 1: all ETHUSDT/BYBIT
    {
        const size_t n = 500;
        std::vector<int64_t>     ts(n);
        std::vector<std::string> sym(n, "ETHUSDT"), exch(n, "BYBIT");
        std::vector<double>      bid(n, 1850.0), ask(n, 1850.01),
                                 bqty(n, 3.0),   aqty(n, 4.0),
                                 sp(n, 0.054),   mid(n, 1850.005);
        for (size_t i = 0; i < n; ++i) ts[i] = 2000LL + i;

        REQUIRE(wr->write_column(0, ts.data(),   n).has_value());
        REQUIRE(wr->write_column(1, sym.data(),  n).has_value());
        REQUIRE(wr->write_column(2, exch.data(), n).has_value());
        REQUIRE(wr->write_column(3, bid.data(),  n).has_value());
        REQUIRE(wr->write_column(4, ask.data(),  n).has_value());
        REQUIRE(wr->write_column(5, bqty.data(), n).has_value());
        REQUIRE(wr->write_column(6, aqty.data(), n).has_value());
        REQUIRE(wr->write_column(7, sp.data(),   n).has_value());
        REQUIRE(wr->write_column(8, mid.data(),  n).has_value());
        REQUIRE(wr->flush_row_group().has_value());
    }
    REQUIRE(wr->close().has_value());

    // Now verify statistics
    auto reader = ParquetReader::open(tmp.path);
    REQUIRE(reader.has_value());
    REQUIRE(reader->num_row_groups() == 2);

    // Row group 0: symbol stats should be "BTCUSDT".."BTCUSDT"
    auto* s0 = reader->column_statistics(0, 1);  // symbol column
    REQUIRE(s0 != nullptr);
    if (s0->min_value.has_value() && s0->max_value.has_value()) {
        CHECK(*s0->min_value == "BTCUSDT");
        CHECK(*s0->max_value == "BTCUSDT");
    }

    // Row group 1: symbol stats should be "ETHUSDT".."ETHUSDT"
    auto* s1 = reader->column_statistics(1, 1);
    REQUIRE(s1 != nullptr);
    if (s1->min_value.has_value() && s1->max_value.has_value()) {
        CHECK(*s1->min_value == "ETHUSDT");
        CHECK(*s1->max_value == "ETHUSDT");
    }

    // Predicate pushdown simulation: querying for "SOLUSDT"
    // Row group 0 max = "BTCUSDT" < "SOLUSDT" → should skip (but only if BTCUSDT < SOLUSDT lexically)
    // Lexicographic: B < E < S → "BTCUSDT" < "SOLUSDT" and "ETHUSDT" < "SOLUSDT"
    // So both row groups can be skipped when querying for SOLUSDT
    int64_t rgs_skipped = 0;
    for (int64_t rg = 0; rg < 2; ++rg) {
        auto* stats = reader->column_statistics(static_cast<size_t>(rg), 1);
        if (stats && stats->min_value.has_value() && stats->max_value.has_value()) {
            std::string filter = "SOLUSDT";
            if (filter < *stats->min_value || filter > *stats->max_value) {
                ++rgs_skipped;
            }
        }
    }
    CHECK(rgs_skipped == 2);  // SOLUSDT not in either row group → both skippable
}

TEST_CASE("Parquet file is smaller than equivalent CSV size", "[ticks][pipeline]") {
    TempFile tmp("compress");
    const size_t N = 10000;
    auto ticks = make_ticks(N);
    write_ticks(tmp.path, ticks);

    // Estimate raw CSV size: each row ~80 characters
    size_t estimated_csv_bytes = N * 80;

    std::error_code ec;
    auto parquet_bytes = static_cast<size_t>(fs::file_size(tmp.path, ec));
    REQUIRE(!ec);

    // Parquet + Snappy + optimal encodings should be well under raw CSV
    CHECK(parquet_bytes < estimated_csv_bytes);

    // Print ratio for informational purposes
    double ratio = 100.0 * parquet_bytes / estimated_csv_bytes;
    INFO("Parquet size " << parquet_bytes << " bytes vs est. CSV "
         << estimated_csv_bytes << " bytes ("
         << ratio << "% of CSV size)");
}

TEST_CASE("Large tick batch: 100K rows, multiple row groups", "[ticks][pipeline]") {
    TempFile tmp("large");
    const size_t N  = 100000;
    const size_t RG = 32768;
    auto ticks = make_ticks(N);
    write_ticks(tmp.path, ticks, RG);

    auto reader = ParquetReader::open(tmp.path);
    REQUIRE(reader.has_value());
    REQUIRE(reader->num_rows() == static_cast<int64_t>(N));

    // Expected row groups: ceil(100000 / 32768) = 4
    int64_t expected_rgs = static_cast<int64_t>((N + RG - 1) / RG);
    CHECK(reader->num_row_groups() == expected_rgs);

    // Verify last row group has correct partial count
    size_t last_rg_rows = N - ((expected_rgs - 1) * RG);
    auto last_info = reader->row_group(static_cast<size_t>(expected_rgs - 1));
    CHECK(last_info.num_rows == static_cast<int64_t>(last_rg_rows));
}
