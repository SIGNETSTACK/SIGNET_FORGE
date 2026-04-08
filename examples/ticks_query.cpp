// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// ticks_query.cpp — Query a tick-data Parquet file with predicate pushdown
//
// Demonstrates:
//   - ParquetReader::open() on a large file
//   - column_statistics() for row-group-level predicate pushdown
//   - Per-row-group column reads (read_column<T>)
//   - Symbol and exchange filters with statistics-based skip
//   - Time-range filter (from_ms / to_ms)
//
// Usage:
//   ./ticks_query ticks.parquet
//   ./ticks_query ticks.parquet --symbol BTCUSDT
//   ./ticks_query ticks.parquet --exchange BINANCE --limit 20
//   ./ticks_query ticks.parquet --symbol ETHUSDT --from 1771977628000 --to 1771977630000
//   ./ticks_query ticks.parquet --stats          # print per-row-group statistics only
//
// Build (standalone):
//   g++ -std=c++20 -O2 -I../include ticks_query.cpp -o ticks_query
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Column indices — must match the schema written by ticks_import
// ---------------------------------------------------------------------------
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
// Helper: decode a raw binary Statistics string as int64 (little-endian)
// ---------------------------------------------------------------------------
static int64_t stat_bytes_to_i64(const std::string& bytes) {
    if (bytes.size() < 8) return 0;
    int64_t v = 0;
    std::memcpy(&v, bytes.data(), 8);
    return v;
}

// ---------------------------------------------------------------------------
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
static std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

static bool env_is_truthy(const char* v) {
    if (!v) return false;
    const std::string t = to_lower_ascii(std::string(v));
    return t == "1" || t == "true" || t == "yes" || t == "on";
}

static bool parse_hex_key_32(const std::string& hex, std::vector<uint8_t>& out) {
    auto hex_nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };

    if (hex.size() != 64) return false;
    out.assign(32, 0);
    for (size_t i = 0; i < 32; ++i) {
        const int hi = hex_nibble(hex[2 * i]);
        const int lo = hex_nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

static expected<std::optional<crypto::EncryptionConfig>> load_pare_decryption_from_env() {
    if (!env_is_truthy(std::getenv("SIGNET_PARE_ENABLE"))) {
        return std::optional<crypto::EncryptionConfig>{};
    }

    const char* footer_hex = std::getenv("SIGNET_PARE_FOOTER_KEY_HEX");
    if (!footer_hex || std::strlen(footer_hex) == 0) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "SIGNET_PARE_ENABLE=1 requires SIGNET_PARE_FOOTER_KEY_HEX (64 hex chars)"};
    }

    const char* col_hex = std::getenv("SIGNET_PARE_COLUMN_KEY_HEX");
    if (!col_hex || std::strlen(col_hex) == 0) col_hex = footer_hex;

    std::vector<uint8_t> footer_key;
    std::vector<uint8_t> column_key;
    if (!parse_hex_key_32(footer_hex, footer_key)) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "invalid SIGNET_PARE_FOOTER_KEY_HEX (must be 64 hex chars)"};
    }
    if (!parse_hex_key_32(col_hex, column_key)) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "invalid SIGNET_PARE_COLUMN_KEY_HEX (must be 64 hex chars)"};
    }

    crypto::EncryptionConfig cfg;
    cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    cfg.footer_key = std::move(footer_key);
    cfg.default_column_key = std::move(column_key);
    cfg.encrypt_footer = true;
    if (const char* aad = std::getenv("SIGNET_PARE_AAD_PREFIX"); aad && *aad) {
        cfg.aad_prefix = aad;
    } else {
        cfg.aad_prefix = "signet-pare-benchmark";
    }

    return std::optional<crypto::EncryptionConfig>{cfg};
}
#endif

// Helper: print usage
// ---------------------------------------------------------------------------
static void usage(const char* prog) {
    std::cerr << "Usage: " << prog << " <file.parquet> [options]\n\n";
    std::cerr << "Options:\n";
    std::cerr << "  --symbol   <SYM>   filter by symbol  (e.g. BTCUSDT)\n";
    std::cerr << "  --exchange <EXCH>  filter by exchange (e.g. BINANCE)\n";
    std::cerr << "  --from     <ms>    include only rows with timestamp_ms >= ms\n";
    std::cerr << "  --to       <ms>    include only rows with timestamp_ms <= ms\n";
    std::cerr << "  --limit    <N>     stop after printing N matching rows (default: 50)\n";
    std::cerr << "  --stats            print per-row-group statistics and exit\n";
    std::cerr << "  --no-header        omit the CSV header from output\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    // ---- Parse arguments ---------------------------------------------------
    std::string            file_path = argv[1];
    std::optional<std::string> filter_symbol;
    std::optional<std::string> filter_exchange;
    std::optional<int64_t>     from_ms;
    std::optional<int64_t>     to_ms;
    int64_t                    limit       = 50;
    bool                       stats_only  = false;
    bool                       no_header   = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--symbol" || arg == "-s") && i + 1 < argc) {
            filter_symbol = argv[++i];
        } else if ((arg == "--exchange" || arg == "-e") && i + 1 < argc) {
            filter_exchange = argv[++i];
        } else if (arg == "--from" && i + 1 < argc) {
            from_ms = std::stoll(argv[++i]);
        } else if (arg == "--to" && i + 1 < argc) {
            to_ms = std::stoll(argv[++i]);
        } else if ((arg == "--limit" || arg == "-n") && i + 1 < argc) {
            limit = std::stoll(argv[++i]);
        } else if (arg == "--stats") {
            stats_only = true;
        } else if (arg == "--no-header") {
            no_header = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n\n";
            usage(argv[0]);
            return 1;
        }
    }

    // ---- Open Parquet file -------------------------------------------------
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
    auto dec_cfg_r = load_pare_decryption_from_env();
    if (!dec_cfg_r) {
        std::cerr << "Encryption config error: " << dec_cfg_r.error().message << "\n";
        return 1;
    }

    auto reader_result = dec_cfg_r->has_value()
        ? ParquetReader::open(file_path, *dec_cfg_r)
        : ParquetReader::open(file_path);
#else
    auto reader_result = ParquetReader::open(file_path);
#endif
    if (!reader_result) {
        std::cerr << "Failed to open file: " << reader_result.error().message << "\n";
        return 1;
    }
    auto& reader = *reader_result;

    int64_t num_rg   = reader.num_row_groups();
    int64_t total_rows = reader.num_rows();

    // ---- Statistics-only mode ----------------------------------------------
    if (stats_only) {
        std::cout << "File: " << file_path << "\n";
        std::cout << "Total rows: " << total_rows << "  Row groups: " << num_rg << "\n\n";

        for (int64_t rg = 0; rg < num_rg; ++rg) {
            auto info = reader.row_group(static_cast<size_t>(rg));
            std::cout << "Row group " << rg << "  (" << info.num_rows << " rows)\n";

            // Timestamp stats
            auto* ts_stats = reader.column_statistics(static_cast<size_t>(rg), COL_TS);
            if (ts_stats && ts_stats->min_value.has_value() && ts_stats->max_value.has_value()) {
                int64_t ts_min = stat_bytes_to_i64(*ts_stats->min_value);
                int64_t ts_max = stat_bytes_to_i64(*ts_stats->max_value);
                std::cout << "  timestamp_ms : [" << ts_min << ", " << ts_max << "]\n";
            }

            // Symbol stats
            auto* sym_stats = reader.column_statistics(static_cast<size_t>(rg), COL_SYMBOL);
            if (sym_stats && sym_stats->min_value.has_value() && sym_stats->max_value.has_value()) {
                std::cout << "  symbol       : [\"" << *sym_stats->min_value
                          << "\" .. \"" << *sym_stats->max_value << "\"]\n";
            }

            // Exchange stats
            auto* exch_stats = reader.column_statistics(static_cast<size_t>(rg), COL_EXCH);
            if (exch_stats && exch_stats->min_value.has_value() && exch_stats->max_value.has_value()) {
                std::cout << "  exchange     : [\"" << *exch_stats->min_value
                          << "\" .. \"" << *exch_stats->max_value << "\"]\n";
            }

            // Null counts
            auto* bid_stats = reader.column_statistics(static_cast<size_t>(rg), COL_BID);
            if (bid_stats) {
                std::cout << "  bid_price    : null_count=" << bid_stats->null_count.value_or(0) << "\n";
            }
        }
        return 0;
    }

    // ---- Query mode --------------------------------------------------------
    auto t_start = std::chrono::steady_clock::now();

    if (!no_header) {
        std::cout << "timestamp_ms,symbol,exchange,bid_price,ask_price,"
                     "bid_qty,ask_qty,spread_bps,mid_price\n";
    }

    int64_t rows_scanned   = 0;
    int64_t rows_emitted   = 0;
    int64_t rgs_skipped    = 0;
    int64_t rgs_read       = 0;

    for (int64_t rg = 0; rg < num_rg && rows_emitted < limit; ++rg) {
        // ---- Predicate pushdown: symbol ------------------------------------
        if (filter_symbol.has_value()) {
            auto* stats = reader.column_statistics(static_cast<size_t>(rg), COL_SYMBOL);
            if (stats && stats->min_value.has_value() && stats->max_value.has_value()) {
                const std::string& sym = *filter_symbol;
                // Skip row group if sym is outside [min, max] lexicographically
                if (sym < *stats->min_value || sym > *stats->max_value) {
                    ++rgs_skipped;
                    continue;
                }
            }
        }

        // ---- Predicate pushdown: exchange ----------------------------------
        if (filter_exchange.has_value()) {
            auto* stats = reader.column_statistics(static_cast<size_t>(rg), COL_EXCH);
            if (stats && stats->min_value.has_value() && stats->max_value.has_value()) {
                const std::string& exch = *filter_exchange;
                if (exch < *stats->min_value || exch > *stats->max_value) {
                    ++rgs_skipped;
                    continue;
                }
            }
        }

        // ---- Predicate pushdown: time range (timestamp_ms) -----------------
        if (from_ms.has_value() || to_ms.has_value()) {
            auto* stats = reader.column_statistics(static_cast<size_t>(rg), COL_TS);
            if (stats && stats->min_value.has_value() && stats->max_value.has_value()) {
                int64_t rg_min = stat_bytes_to_i64(*stats->min_value);
                int64_t rg_max = stat_bytes_to_i64(*stats->max_value);
                bool skip = false;
                if (from_ms.has_value() && rg_max < *from_ms) skip = true;
                if (to_ms.has_value()   && rg_min > *to_ms)   skip = true;
                if (skip) {
                    ++rgs_skipped;
                    continue;
                }
            }
        }

        // ---- Read this row group -------------------------------------------
        ++rgs_read;

        auto ts_r  = reader.read_column<int64_t>(static_cast<size_t>(rg), COL_TS);
        auto sym_r = reader.read_column<std::string>(static_cast<size_t>(rg), COL_SYMBOL);
        auto ex_r  = reader.read_column<std::string>(static_cast<size_t>(rg), COL_EXCH);
        auto bid_r = reader.read_column<double>(static_cast<size_t>(rg), COL_BID);
        auto ask_r = reader.read_column<double>(static_cast<size_t>(rg), COL_ASK);
        auto bq_r  = reader.read_column<double>(static_cast<size_t>(rg), COL_BQTY);
        auto aq_r  = reader.read_column<double>(static_cast<size_t>(rg), COL_AQTY);
        auto sp_r  = reader.read_column<double>(static_cast<size_t>(rg), COL_SPREAD);
        auto mid_r = reader.read_column<double>(static_cast<size_t>(rg), COL_MID);

        if (!ts_r || !sym_r || !ex_r || !bid_r || !ask_r ||
            !bq_r || !aq_r  || !sp_r || !mid_r) {
            std::cerr << "Read error in row group " << rg << ":\n";
            if (!ts_r)  std::cerr << "  timestamp_ms: " << ts_r.error().message << "\n";
            if (!sym_r) std::cerr << "  symbol: " << sym_r.error().message << "\n";
            if (!ex_r)  std::cerr << "  exchange: " << ex_r.error().message << "\n";
            if (!bid_r) std::cerr << "  bid_price: " << bid_r.error().message << "\n";
            if (!ask_r) std::cerr << "  ask_price: " << ask_r.error().message << "\n";
            if (!bq_r)  std::cerr << "  bid_qty: " << bq_r.error().message << "\n";
            if (!aq_r)  std::cerr << "  ask_qty: " << aq_r.error().message << "\n";
            if (!sp_r)  std::cerr << "  spread_bps: " << sp_r.error().message << "\n";
            if (!mid_r) std::cerr << "  mid_price: " << mid_r.error().message << "\n";
            continue;
        }

        const auto& ts     = *ts_r;
        const auto& sym    = *sym_r;
        const auto& ex     = *ex_r;
        const auto& bid    = *bid_r;
        const auto& ask    = *ask_r;
        const auto& bq     = *bq_r;
        const auto& aq     = *aq_r;
        const auto& spread = *sp_r;
        const auto& mid    = *mid_r;

        size_t n = ts.size();
        rows_scanned += static_cast<int64_t>(n);

        // ---- Per-row filter and emit ---------------------------------------
        for (size_t i = 0; i < n && rows_emitted < limit; ++i) {
            // Symbol filter
            if (filter_symbol.has_value() && sym[i] != *filter_symbol) continue;
            // Exchange filter
            if (filter_exchange.has_value() && ex[i] != *filter_exchange) continue;
            // Time range filter
            if (from_ms.has_value() && ts[i] < *from_ms) continue;
            if (to_ms.has_value()   && ts[i] > *to_ms)   continue;

            // Emit CSV row
            std::cout << ts[i] << ','
                      << sym[i]    << ','
                      << ex[i]     << ','
                      << std::fixed << std::setprecision(8)
                      << bid[i]    << ','
                      << ask[i]    << ','
                      << bq[i]     << ','
                      << aq[i]     << ','
                      << spread[i] << ','
                      << mid[i]    << '\n';
            ++rows_emitted;
        }
    }

    // ---- Summary to stderr -------------------------------------------------
    auto t_end  = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();

    std::cerr << "\n=== ticks_query summary ===\n";
    std::cerr << "  File           : " << file_path << "\n";
    std::cerr << "  Total rows     : " << total_rows << "\n";
    std::cerr << "  Row groups     : " << num_rg << " total, "
              << rgs_skipped << " skipped (predicate pushdown), "
              << rgs_read    << " read\n";
    std::cerr << "  Rows scanned   : " << rows_scanned << "\n";
    std::cerr << "  Rows emitted   : " << rows_emitted << "\n";
    std::cerr << "  Elapsed        : " << std::fixed << std::setprecision(3)
              << elapsed << " s\n";

    if (num_rg > 0 && rgs_skipped > 0) {
        double skip_pct = 100.0 * rgs_skipped / num_rg;
        std::cerr << "  Pushdown saved : " << std::fixed << std::setprecision(1)
                  << skip_pct << "% of row groups never read from disk\n";
    }

    return 0;
}
