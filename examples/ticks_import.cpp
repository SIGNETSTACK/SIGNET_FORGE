// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// ticks_import.cpp — Convert a gzip-compressed tick-data CSV to Parquet
//
// Input schema (9 columns):
//   timestamp_ms  int64   millisecond epoch timestamp
//   symbol        string  low-cardinality (6 symbols)  → RLE_DICTIONARY
//   exchange      string  low-cardinality (3 exchanges) → RLE_DICTIONARY
//   bid_price     double  → BYTE_STREAM_SPLIT
//   ask_price     double  → BYTE_STREAM_SPLIT
//   bid_qty       double  → BYTE_STREAM_SPLIT
//   ask_qty       double  → BYTE_STREAM_SPLIT
//   spread_bps    double  → BYTE_STREAM_SPLIT
//   mid_price     double  → BYTE_STREAM_SPLIT
//
// Usage:
//   gunzip -c ticks.csv.gz | ./ticks_import output.parquet
//   ./ticks_import ticks.csv.gz output.parquet        # auto-decompresses .gz
//   ./ticks_import ticks.csv    output.parquet        # plain CSV
//
// Build (standalone):
//   g++ -std=c++20 -O2 -I../include ticks_import.cpp -o ticks_import
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"

#include <array>
#include <chrono>
#include <cstdio>

// MSVC names popen/pclose with a leading underscore
#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr size_t ROW_GROUP_ROWS  = 128 * 1024;   // 128 K rows per group
static constexpr size_t LINE_BUF        = 1024;          // max CSV line length
static constexpr size_t NUM_COLS        = 9;

// Column indices (must match schema order below)
enum ColIdx : size_t {
    COL_TS      = 0,
    COL_SYMBOL  = 1,
    COL_EXCH    = 2,
    COL_BID     = 3,
    COL_ASK     = 4,
    COL_BQTY    = 5,
    COL_AQTY    = 6,
    COL_SPREAD  = 7,
    COL_MID     = 8,
};

// ---------------------------------------------------------------------------
// parse_csv_line — split a comma-delimited line into exactly N fields.
// Returns false if the field count doesn't match.
// ---------------------------------------------------------------------------
static bool parse_csv_line(const char* line, size_t len,
                            std::array<std::string_view, NUM_COLS>& fields) {
    size_t f = 0;
    const char* start = line;
    for (size_t i = 0; i <= len; ++i) {
        if (line[i] == ',' || line[i] == '\0' || line[i] == '\n' || line[i] == '\r') {
            if (f >= NUM_COLS) return false;
            fields[f++] = std::string_view(start, line + i - start);
            start = line + i + 1;
            if (f == NUM_COLS) break;
        }
    }
    return f == NUM_COLS;
}

// ---------------------------------------------------------------------------
// flush_batch — write one row group to the Parquet file
// ---------------------------------------------------------------------------
static bool flush_batch(ParquetWriter& writer,
                         std::vector<int64_t>&     ts,
                         std::vector<std::string>& sym,
                         std::vector<std::string>& exch,
                         std::vector<double>&      bid,
                         std::vector<double>&      ask,
                         std::vector<double>&      bqty,
                         std::vector<double>&      aqty,
                         std::vector<double>&      spread,
                         std::vector<double>&      mid) {
    size_t n = ts.size();
    if (n == 0) return true;

    auto check = [](expected<void> r, const char* col) -> bool {
        if (!r) {
            std::cerr << "Write error on column '" << col << "': "
                      << r.error().message << "\n";
            return false;
        }
        return true;
    };

    if (!check(writer.write_column(COL_TS,     ts.data(),      n), "timestamp_ms")) return false;
    if (!check(writer.write_column(COL_SYMBOL, sym.data(),     n), "symbol"))       return false;
    if (!check(writer.write_column(COL_EXCH,   exch.data(),    n), "exchange"))     return false;
    if (!check(writer.write_column(COL_BID,    bid.data(),     n), "bid_price"))    return false;
    if (!check(writer.write_column(COL_ASK,    ask.data(),     n), "ask_price"))    return false;
    if (!check(writer.write_column(COL_BQTY,   bqty.data(),    n), "bid_qty"))      return false;
    if (!check(writer.write_column(COL_AQTY,   aqty.data(),    n), "ask_qty"))      return false;
    if (!check(writer.write_column(COL_SPREAD, spread.data(),  n), "spread_bps"))   return false;
    if (!check(writer.write_column(COL_MID,    mid.data(),     n), "mid_price"))    return false;

    auto flush = writer.flush_row_group();
    if (!flush) {
        std::cerr << "flush_row_group failed: " << flush.error().message << "\n";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {

    // ---- Argument parsing --------------------------------------------------
    // Modes:
    //   (a) stdin mode:  ticks_import output.parquet            (argv[1] = output)
    //   (b) file mode:   ticks_import input.csv[.gz] output.parquet

    std::string output_path;
    std::string input_path;
    bool read_stdin = false;

    if (argc == 2) {
        // stdin → output.parquet
        output_path = argv[1];
        read_stdin  = true;
    } else if (argc == 3) {
        input_path  = argv[1];
        output_path = argv[2];
    } else {
        std::cerr << "Usage:\n";
        std::cerr << "  " << argv[0] << " output.parquet                  # reads from stdin\n";
        std::cerr << "  " << argv[0] << " input.csv[.gz] output.parquet   # reads from file\n";
        std::cerr << "\nExample:\n";
        std::cerr << "  gunzip -c ticks.csv.gz | " << argv[0] << " ticks.parquet\n";
        std::cerr << "  " << argv[0] << " ticks.csv.gz ticks.parquet\n";
        return 1;
    }

    // ---- Open input --------------------------------------------------------
    FILE* fp = nullptr;
    bool  use_popen = false;

    if (read_stdin) {
        fp = stdin;
    } else {
        if (!std::filesystem::exists(input_path)) {
            std::cerr << "Error: input file not found: " << input_path << "\n";
            return 1;
        }
        // Auto-detect gzip by extension
        bool is_gz = (input_path.size() >= 3 &&
                      input_path.substr(input_path.size() - 3) == ".gz");
        if (is_gz) {
            // CWE-78: Validate path contains no shell metacharacters before popen()
            for (char c : input_path) {
                if (c == '\'' || c == '"' || c == '\\' || c == '`' ||
                    c == '$'  || c == '|' || c == ';'  || c == '&' ||
                    c == '('  || c == ')' || c == '<'  || c == '>' ||
                    c == '\n' || c == '\r' || c == '\0') {
                    std::cerr << "Error: input path contains unsafe characters\n";
                    return 1;
                }
            }
            std::string cmd = "gunzip -c '" + input_path + "'";
            fp = popen(cmd.c_str(), "r");
            use_popen = true;
            if (!fp) {
                std::cerr << "Error: failed to open gzip pipe for: " << input_path << "\n";
                return 1;
            }
        } else {
            fp = fopen(input_path.c_str(), "r");
            if (!fp) {
                std::cerr << "Error: cannot open file: " << input_path << "\n";
                return 1;
            }
        }
    }

    // ---- Schema (optimised for tick data) ----------------------------------
    auto schema = Schema::builder("market_ticks")
        .column<int64_t>("timestamp_ms")   // DELTA_BINARY_PACKED via auto_encoding
        .column<std::string>("symbol")     // RLE_DICTIONARY via auto_encoding
        .column<std::string>("exchange")   // RLE_DICTIONARY via auto_encoding
        .column<double>("bid_price")       // BYTE_STREAM_SPLIT via auto_encoding
        .column<double>("ask_price")
        .column<double>("bid_qty")
        .column<double>("ask_qty")
        .column<double>("spread_bps")
        .column<double>("mid_price")
        .build();

    WriterOptions opts;
    opts.auto_encoding   = true;           // DELTA for int64, BSS for double, DICT for strings
    opts.compression     = Compression::SNAPPY;
    opts.row_group_size  = ROW_GROUP_ROWS;

    auto writer_result = ParquetWriter::open(output_path, schema, opts);
    if (!writer_result) {
        std::cerr << "Failed to open output: " << writer_result.error().message << "\n";
        if (use_popen) pclose(fp);
        else if (fp && fp != stdin) fclose(fp);
        return 1;
    }
    auto& writer = *writer_result;

    // ---- Column buffers ----------------------------------------------------
    std::vector<int64_t>     buf_ts;      buf_ts.reserve(ROW_GROUP_ROWS);
    std::vector<std::string> buf_sym;     buf_sym.reserve(ROW_GROUP_ROWS);
    std::vector<std::string> buf_exch;    buf_exch.reserve(ROW_GROUP_ROWS);
    std::vector<double>      buf_bid;     buf_bid.reserve(ROW_GROUP_ROWS);
    std::vector<double>      buf_ask;     buf_ask.reserve(ROW_GROUP_ROWS);
    std::vector<double>      buf_bqty;    buf_bqty.reserve(ROW_GROUP_ROWS);
    std::vector<double>      buf_aqty;    buf_aqty.reserve(ROW_GROUP_ROWS);
    std::vector<double>      buf_spread;  buf_spread.reserve(ROW_GROUP_ROWS);
    std::vector<double>      buf_mid;     buf_mid.reserve(ROW_GROUP_ROWS);

    // ---- Parse loop --------------------------------------------------------
    auto t_start = std::chrono::steady_clock::now();

    char linebuf[LINE_BUF];
    int64_t row_count    = 0;
    int64_t skip_count   = 0;
    int64_t rg_count     = 0;
    bool    header_done  = false;

    std::array<std::string_view, NUM_COLS> fields;

    while (fgets(linebuf, sizeof(linebuf), fp)) {
        size_t len = strlen(linebuf);

        // Skip header row
        if (!header_done) {
            header_done = true;
            // Verify first field is "timestamp_ms" (sanity check)
            if (strncmp(linebuf, "timestamp_ms", 12) != 0) {
                std::cerr << "Warning: unexpected header: " << linebuf;
            }
            continue;
        }

        // Skip blank lines
        if (len <= 1) continue;

        // Parse fields
        if (!parse_csv_line(linebuf, len, fields)) {
            ++skip_count;
            continue;
        }

        // Convert fields
        try {
            buf_ts.push_back(std::stoll(std::string(fields[COL_TS])));
            buf_sym.emplace_back(fields[COL_SYMBOL]);
            buf_exch.emplace_back(fields[COL_EXCH]);
            buf_bid.push_back(std::stod(std::string(fields[COL_BID])));
            buf_ask.push_back(std::stod(std::string(fields[COL_ASK])));
            buf_bqty.push_back(std::stod(std::string(fields[COL_BQTY])));
            buf_aqty.push_back(std::stod(std::string(fields[COL_AQTY])));
            buf_spread.push_back(std::stod(std::string(fields[COL_SPREAD])));
            buf_mid.push_back(std::stod(std::string(fields[COL_MID])));
            ++row_count;
        } catch (const std::exception& e) {
            if (skip_count < 10) {
                std::cerr << "Warning: skipping row " << (row_count + skip_count + 1)
                          << ": " << e.what() << "\n";
            } else if (skip_count == 10) {
                std::cerr << "Warning: suppressing further row skip messages\n";
            }
            ++skip_count;
            continue;
        }

        // Flush row group when buffer is full
        if (buf_ts.size() >= ROW_GROUP_ROWS) {
            if (!flush_batch(writer, buf_ts, buf_sym, buf_exch,
                             buf_bid, buf_ask, buf_bqty, buf_aqty,
                             buf_spread, buf_mid)) {
                if (use_popen) pclose(fp);
                else if (fp && fp != stdin) fclose(fp);
                return 1;
            }
            ++rg_count;

            // Progress report every row group
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - t_start).count();
            double rate    = (elapsed > 0) ? row_count / elapsed / 1e6 : 0;
            std::cerr << "\r  " << row_count / 1000000 << "M rows ("
                      << rg_count << " row groups, "
                      << std::fixed << std::setprecision(2) << rate << " M/s) ...";
            std::cerr.flush();

            buf_ts.clear();    buf_sym.clear();   buf_exch.clear();
            buf_bid.clear();   buf_ask.clear();   buf_bqty.clear();
            buf_aqty.clear();  buf_spread.clear(); buf_mid.clear();
        }
    }

    // Close input
    if (use_popen) pclose(fp);
    else if (fp && fp != stdin) fclose(fp);

    // Flush the last partial row group
    if (!buf_ts.empty()) {
        if (!flush_batch(writer, buf_ts, buf_sym, buf_exch,
                         buf_bid, buf_ask, buf_bqty, buf_aqty,
                         buf_spread, buf_mid)) {
            return 1;
        }
        ++rg_count;
    }

    // Finalize Parquet file
    auto close_result = writer.close();
    if (!close_result) {
        std::cerr << "\nFailed to close writer: " << close_result.error().message << "\n";
        return 1;
    }

    // ---- Summary -----------------------------------------------------------
    auto t_end = std::chrono::steady_clock::now();
    double total_s = std::chrono::duration<double>(t_end - t_start).count();
    double rate    = (total_s > 0) ? row_count / total_s / 1e6 : 0;

    std::error_code ec;
    auto out_sz = std::filesystem::file_size(output_path, ec);

    std::cerr << "\r"; // clear progress line
    std::cout << "\n=== ticks_import complete ===\n";
    std::cout << "  Rows written   : " << row_count << "\n";
    std::cout << "  Row groups     : " << rg_count  << "\n";
    std::cout << "  Rows skipped   : " << skip_count << "\n";
    std::cout << "  Output file    : " << output_path << "\n";
    if (!ec)
        std::cout << "  Parquet size   : " << std::fixed << std::setprecision(1)
                  << out_sz / 1e6 << " MB\n";
    std::cout << "  Elapsed        : " << std::fixed << std::setprecision(2)
              << total_s << " s\n";
    std::cout << "  Throughput     : " << std::fixed << std::setprecision(2)
              << rate << " M rows/s\n";
    std::cout << "\nEncodings applied (via auto_encoding=true):\n";
    std::cout << "  timestamp_ms  → DELTA_BINARY_PACKED  (delta-of-deltas on sorted timestamps)\n";
    std::cout << "  symbol        → RLE_DICTIONARY        (6 unique values)\n";
    std::cout << "  exchange      → RLE_DICTIONARY        (3 unique values)\n";
    std::cout << "  bid/ask/qty/spread/mid → BYTE_STREAM_SPLIT  (best float compression)\n";
    std::cout << "  All columns   → Snappy compressed\n";

    return 0;
}
