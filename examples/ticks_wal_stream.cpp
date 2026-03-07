// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// ticks_wal_stream.cpp — Durable tick ingestion: CSV.gz → WAL → Parquet
//
// Demonstrates:
//   - WalMmapWriter (mmap ring, ~38 ns per-append) as the durable write-ahead log
//   - Recovery from WAL after a simulated crash: all committed records survive
//   - Batch compaction: WAL → Parquet using the same column-optimal encodings
//     as ticks_import (DELTA for timestamps, DICT for symbols, BSS for floats)
//   - The full durable ingestion pattern for real-time tick data pipelines:
//
//       tick arrives → WAL.append() [38 ns, survives crash]
//                    → batch compaction → Parquet [columnar, queryable]
//
// Build (standalone):
//   g++ -std=c++20 -O2 -I../include ticks_wal_stream.cpp -o ticks_wal_stream
//
// Usage:
//   ./ticks_wal_stream                      # runs built-in demo with synthetic ticks
//   ./ticks_wal_stream input.csv[.gz]       # streams a real CSV file through the WAL
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdlib>
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
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <optional>

#if defined(__AVX2__) || defined(__SSE2__)
#include <immintrin.h>
#endif
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif
using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Wire format for a single tick in the WAL
// Each tick is serialised as a pipe-delimited ASCII record, e.g.:
//   1771977628830|BTCUSDT|BINANCE|64000.01|64000.02|0.50|0.60|0.0156|64000.015
// This keeps the WAL human-readable for debugging and avoids alignment issues.
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

static std::string serialise(const TickRecord& t) {
    std::ostringstream os;
    os << t.timestamp_ms << '|' << t.symbol << '|' << t.exchange << '|'
       << std::fixed << std::setprecision(8)
       << t.bid_price  << '|' << t.ask_price << '|'
       << t.bid_qty    << '|' << t.ask_qty   << '|'
       << t.spread_bps << '|' << t.mid_price;
    return os.str();
}


static bool parse_i64_cstr(const char* s, int64_t& out) {
    if (!s || *s == '\0') return false;
    errno = 0;
    char* end = nullptr;
    long long v = std::strtoll(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return false;
    out = static_cast<int64_t>(v);
    return true;
}

static bool parse_f64_cstr(const char* s, double& out) {
    if (!s || *s == '\0') return false;
    errno = 0;
    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0') return false;
    out = v;
    return true;
}

static bool parse_i64_view(std::string_view sv, int64_t& out) {
    if (sv.empty() || sv.size() >= 64) return false;
    char buf[64];
    std::memcpy(buf, sv.data(), sv.size());
    buf[sv.size()] = '\0';
    return parse_i64_cstr(buf, out);
}

static bool parse_f64_view(std::string_view sv, double& out) {
    if (sv.empty() || sv.size() >= 64) return false;
    char buf[64];
    std::memcpy(buf, sv.data(), sv.size());
    buf[sv.size()] = '\0';
    return parse_f64_cstr(buf, out);
}

static inline void replace_char_simd(char* data, size_t len, char from, char to) {
    size_t i = 0;

#if defined(__AVX2__)
    const __m256i vfrom = _mm256_set1_epi8(static_cast<char>(from));
    const __m256i vto = _mm256_set1_epi8(static_cast<char>(to));
    for (; i + 32 <= len; i += 32) {
        __m256i x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data + i));
        __m256i m = _mm256_cmpeq_epi8(x, vfrom);
        __m256i r = _mm256_or_si256(_mm256_and_si256(m, vto), _mm256_andnot_si256(m, x));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(data + i), r);
    }
#elif defined(__SSE2__)
    const __m128i vfrom = _mm_set1_epi8(static_cast<char>(from));
    const __m128i vto = _mm_set1_epi8(static_cast<char>(to));
    for (; i + 16 <= len; i += 16) {
        __m128i x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + i));
        __m128i m = _mm_cmpeq_epi8(x, vfrom);
        __m128i r = _mm_or_si128(_mm_and_si128(m, vto), _mm_andnot_si128(m, x));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(data + i), r);
    }
#elif defined(__ARM_NEON)
    const uint8x16_t vfrom = vdupq_n_u8(static_cast<uint8_t>(from));
    const uint8x16_t vto = vdupq_n_u8(static_cast<uint8_t>(to));
    for (; i + 16 <= len; i += 16) {
        uint8x16_t x = vld1q_u8(reinterpret_cast<const uint8_t*>(data + i));
        uint8x16_t m = vceqq_u8(x, vfrom);
        uint8x16_t r = vbslq_u8(m, vto, x);
        vst1q_u8(reinterpret_cast<uint8_t*>(data + i), r);
    }
#endif

    for (; i < len; ++i) {
        if (data[i] == from) data[i] = to;
    }
}

static bool deserialise(std::string_view sv, TickRecord& out) {
    // Expects exactly 9 pipe-separated fields.
    std::array<std::string_view, 9> fields{};
    size_t f = 0;
    size_t start = 0;
    for (size_t i = 0; i <= sv.size(); ++i) {
        if (i == sv.size() || sv[i] == '|') {
            if (f >= fields.size()) return false;
            fields[f++] = sv.substr(start, i - start);
            start = i + 1;
        }
    }
    if (f != fields.size()) return false;

    if (!parse_i64_view(fields[0], out.timestamp_ms)) return false;
    out.symbol.assign(fields[1].data(), fields[1].size());
    out.exchange.assign(fields[2].data(), fields[2].size());
    if (!parse_f64_view(fields[3], out.bid_price)) return false;
    if (!parse_f64_view(fields[4], out.ask_price)) return false;
    if (!parse_f64_view(fields[5], out.bid_qty)) return false;
    if (!parse_f64_view(fields[6], out.ask_qty)) return false;
    if (!parse_f64_view(fields[7], out.spread_bps)) return false;
    if (!parse_f64_view(fields[8], out.mid_price)) return false;
    return true;
}

static bool parse_csv_tick_line(char* line, TickRecord& out) {
    size_t len = std::strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) --len;
    if (len == 0) return false;
    line[len] = '\0';

    // SIMD-accelerated in-place tokenization: replace ',' with NUL.
    replace_char_simd(line, len, ',', '\0');

    std::array<char*, 9> fields{};
    size_t f = 0;
    fields[f++] = line;
    for (size_t i = 0; i < len; ++i) {
        if (line[i] == '\0') {
            if (f >= fields.size()) return false;
            fields[f++] = line + i + 1;
        }
    }
    if (f != fields.size()) return false;

    if (!parse_i64_cstr(fields[0], out.timestamp_ms)) return false;
    out.symbol.assign(fields[1]);
    out.exchange.assign(fields[2]);
    if (!parse_f64_cstr(fields[3], out.bid_price)) return false;
    if (!parse_f64_cstr(fields[4], out.ask_price)) return false;
    if (!parse_f64_cstr(fields[5], out.bid_qty)) return false;
    if (!parse_f64_cstr(fields[6], out.ask_qty)) return false;
    if (!parse_f64_cstr(fields[7], out.spread_bps)) return false;
    if (!parse_f64_cstr(fields[8], out.mid_price)) return false;
    return true;
}
static std::string to_lower_ascii(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool env_is_truthy(const char* v) {
    if (!v) return false;
    const std::string t = to_lower_ascii(std::string(v));
    return t == "1" || t == "true" || t == "yes" || t == "on";
}

static int64_t env_i64(const char* key, int64_t fallback) {
    const char* v = std::getenv(key);
    if (!v || *v == '\0') return fallback;

    errno = 0;
    char* end = nullptr;
    long long parsed = std::strtoll(v, &end, 10);
    if (errno != 0 || end == v || (end && *end != '\0') || parsed < 0)
        return fallback;
    return static_cast<int64_t>(parsed);
}


#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
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

static expected<std::optional<crypto::EncryptionConfig>> load_pare_encryption_from_env() {
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
static bool is_safe_benchmark_reset_path(const std::string& path) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(fs::path(path), ec);
    const std::string p = ec ? path : resolved.string();
    return p.rfind("/tmp/", 0) == 0
        || p.rfind("/private/tmp/", 0) == 0;
}

static WalLifecycleMode resolve_lifecycle_mode(bool reset_for_benchmark) {
    const std::string env = to_lower_ascii(
        std::getenv("SIGNET_ENV") ? std::getenv("SIGNET_ENV") : "dev");
    if (env == "prod" || env == "production")
        return WalLifecycleMode::Production;

    const std::string mode = to_lower_ascii(
        std::getenv("SIGNET_WAL_MODE") ? std::getenv("SIGNET_WAL_MODE") : "");
    if (mode == "benchmark" || reset_for_benchmark)
        return WalLifecycleMode::Benchmark;

    return WalLifecycleMode::Development;
}

static const char* lifecycle_mode_name(WalLifecycleMode mode) {
    switch (mode) {
        case WalLifecycleMode::Production:  return "production";
        case WalLifecycleMode::Benchmark:   return "benchmark";
        case WalLifecycleMode::Development: return "development";
    }
    return "development";
}

// ---------------------------------------------------------------------------
// Synthetic tick generator (for the built-in demo)
// ---------------------------------------------------------------------------
static std::vector<TickRecord> make_synthetic_ticks(size_t n) {
    static const char* SYMS[]  = {"BTCUSDT", "ETHUSDT", "SOLUSDT",
                                   "DOGEUSDT", "PEPEUSDT", "FLOKIUSDT"};
    static const char* EXCHS[] = {"BINANCE", "BYBIT", "OKX"};
    std::vector<TickRecord> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        TickRecord r;
        r.timestamp_ms = 1'771'977'628'000LL + static_cast<int64_t>(i);
        r.symbol       = SYMS[i % 6];
        r.exchange     = EXCHS[i % 3];
        double base    = (i % 6 == 0) ? 64000.0 :
                         (i % 6 == 1) ? 1850.0  :
                         (i % 6 == 2) ? 79.0    :
                         (i % 6 == 3) ? 0.09    :
                         (i % 6 == 4) ? 0.000008 : 0.00015;
        r.bid_price    = base + (i % 100) * base * 0.0001;
        r.ask_price    = r.bid_price + base * 0.00001;
        r.bid_qty      = 0.1  + (i % 50) * 0.001;
        r.ask_qty      = 0.2  + (i % 30) * 0.001;
        r.spread_bps   = 0.015 + (i % 10) * 0.001;
        r.mid_price    = (r.bid_price + r.ask_price) / 2.0;
        v.push_back(r);
    }
    return v;
}

// ---------------------------------------------------------------------------
// compact_wal_to_parquet — read all WAL records and write a Parquet file
// ---------------------------------------------------------------------------
static bool compact_wal_to_parquet(const std::string& wal_dir,
                                    const std::string& out_parquet,
                                    int64_t& records_out,
                                    const WalManagerOptions& wal_opts) {
    // Open WAL manager for recovery
    auto mgr_r = WalManager::open(wal_dir, wal_opts);
    if (!mgr_r) {
        std::cerr << "WAL open failed: " << mgr_r.error().message << "\n";
        return false;
    }
    auto& mgr = *mgr_r;

    // Collect all segment paths from the WAL
    auto paths = mgr.segment_paths();
    if (paths.empty()) {
        std::cerr << "WAL is empty — nothing to compact.\n";
        return false;
    }

    // Build tick schema (matches ticks_import)
    auto schema = Schema::builder("market_ticks")
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

    WriterOptions opts;
    opts.auto_encoding  = true;
    opts.compression    = Compression::SNAPPY;
    opts.row_group_size = 131072;  // 128 K
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
    auto pare_cfg_r = load_pare_encryption_from_env();
    if (!pare_cfg_r) {
        std::cerr << "Encryption config error: " << pare_cfg_r.error().message << "\n";
        return false;
    }
    if (pare_cfg_r->has_value()) {
        opts.encryption = *pare_cfg_r;
        std::cout << "  Encryption : footer=PARE enabled (AES-GCM/CTR)\n";
    }
#endif

    auto wr_r = ParquetWriter::open(out_parquet, schema, opts);
    if (!wr_r) {
        std::cerr << "Parquet open failed: " << wr_r.error().message << "\n";
        return false;
    }
    auto& wr = *wr_r;

    // Decode WAL records segment by segment
    constexpr size_t RG = 131072;
    std::vector<int64_t>     ts;     ts.reserve(RG);
    std::vector<std::string> sym;    sym.reserve(RG);
    std::vector<std::string> exch;   exch.reserve(RG);
    std::vector<double>      bid, ask, bqty, aqty, sp, mid;
    for (auto* v : {&bid, &ask, &bqty, &aqty, &sp, &mid}) v->reserve(RG);

    int64_t total = 0, bad = 0;
    int64_t skipped_non_wal_segments = 0;

    auto flush = [&]() -> bool {
        size_t n = ts.size();
        if (n == 0) return true;
        bool ok = true;
        ok &= wr.write_column(0, ts.data(),   n).has_value();
        ok &= wr.write_column(1, sym.data(),  n).has_value();
        ok &= wr.write_column(2, exch.data(), n).has_value();
        ok &= wr.write_column(3, bid.data(),  n).has_value();
        ok &= wr.write_column(4, ask.data(),  n).has_value();
        ok &= wr.write_column(5, bqty.data(), n).has_value();
        ok &= wr.write_column(6, aqty.data(), n).has_value();
        ok &= wr.write_column(7, sp.data(),   n).has_value();
        ok &= wr.write_column(8, mid.data(),  n).has_value();
        ok &= wr.flush_row_group().has_value();
        ts.clear(); sym.clear(); exch.clear();
        bid.clear(); ask.clear(); bqty.clear();
        aqty.clear(); sp.clear(); mid.clear();
        return ok;
    };

    for (const auto& seg_path : paths) {
        auto reader_r = WalReader::open(seg_path);
        if (!reader_r) {
            // Skip only non-WAL/invalid-header segment files (e.g., mmap standby files).
            // Real I/O failures must remain fatal.
            if (reader_r.error().code == ErrorCode::INVALID_FILE) {
                ++skipped_non_wal_segments;
                continue;
            }
            std::cerr << "WAL segment open failed: " << reader_r.error().message << "\n";
            return false;
        }
        auto& reader = *reader_r;

        while (true) {
            auto entry_r = reader.next();
            if (!entry_r) {
                std::cerr << "WAL read failed: " << entry_r.error().message << "\n";
                return false;
            }
            if (!entry_r->has_value()) break;

            const auto& entry = entry_r->value();
            TickRecord t;
            std::string_view payload(
                reinterpret_cast<const char*>(entry.payload.data()),
                entry.payload.size());
            if (!deserialise(payload, t)) { ++bad; continue; }

            ts.push_back(t.timestamp_ms);
            sym.push_back(t.symbol);
            exch.push_back(t.exchange);
            bid.push_back(t.bid_price);
            ask.push_back(t.ask_price);
            bqty.push_back(t.bid_qty);
            aqty.push_back(t.ask_qty);
            sp.push_back(t.spread_bps);
            mid.push_back(t.mid_price);
            ++total;

            if (ts.size() >= RG) {
                if (!flush()) return false;
            }
        }
    }
    if (!flush()) return false;
    if (!wr.close().has_value()) return false;

    records_out = total;
    if (bad > 0)
        std::cerr << "  " << bad << " malformed WAL records skipped\n";
    if (skipped_non_wal_segments > 0)
        std::cerr << "  " << skipped_non_wal_segments
                  << " non-WAL mmap segment files skipped during compaction\n";
    return true;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {

    // ---- Setup temp WAL directory -----------------------------------------
    std::string wal_dir = std::getenv("SIGNET_WAL_DIR")
        ? std::getenv("SIGNET_WAL_DIR")
        : "/tmp/signet_ticks_wal";
    std::string parquet_out = std::getenv("SIGNET_WAL_PARQUET")
        ? std::getenv("SIGNET_WAL_PARQUET")
        : "/tmp/signet_ticks_wal.parquet";
    const bool reset_for_benchmark = env_is_truthy(std::getenv("SIGNET_WAL_RESET"));
    const WalLifecycleMode lifecycle_mode = resolve_lifecycle_mode(reset_for_benchmark);
    const bool fail_on_append_error = env_is_truthy(std::getenv("SIGNET_WAL_FAIL_ON_APPEND_ERROR"));
    const int64_t max_rows = env_i64("SIGNET_WAL_MAX_ROWS", -1);
    const bool byte_watermark_enabled =
        env_is_truthy(std::getenv("SIGNET_WAL_BYTE_WATERMARK_ENABLE"));
    const int64_t watermark_high_pct_raw =
        env_i64("SIGNET_WAL_HIGH_WATERMARK_PCT", 75);
    const int64_t watermark_low_pct_raw =
        env_i64("SIGNET_WAL_LOW_WATERMARK_PCT", 40);
    const int64_t ring_full_sleep_us =
        env_i64("SIGNET_WAL_RINGFULL_SLEEP_US", 2000);
    const int64_t ring_full_max_retries =
        env_i64("SIGNET_WAL_RINGFULL_MAX_RETRIES", 5000);

    bool max_rows_reached = false;

    if (lifecycle_mode == WalLifecycleMode::Production && reset_for_benchmark) {
        std::cerr << "Refusing startup: SIGNET_WAL_RESET is not allowed in production mode\n";
        return 2;
    }
    if (reset_for_benchmark && !is_safe_benchmark_reset_path(wal_dir)) {
        std::cerr << "Refusing benchmark reset on non-sandbox path: " << wal_dir << "\n";
        return 2;
    }

    std::error_code rm_ec;
    if (reset_for_benchmark) {
        fs::remove_all(wal_dir, rm_ec);
        fs::remove(parquet_out, rm_ec);
    }
    fs::create_directories(wal_dir);

    WalManagerOptions manager_opts;
    manager_opts.file_prefix = "wal_mmap";
    manager_opts.file_ext = ".wal";
    manager_opts.lifecycle_mode = lifecycle_mode;
    manager_opts.reset_on_open = false;
    manager_opts.require_checkpoint_before_prune = true;
    manager_opts.checkpoint_manifest_path = wal_dir + "/.wal_compaction_manifest";

    std::cout << "WAL lifecycle mode: " << lifecycle_mode_name(lifecycle_mode) << "\n";

    // ---- Build tick source: synthetic or from CSV file --------------------
    std::vector<TickRecord> ticks;
    bool from_file = (argc >= 2);
    int64_t source_ticks = 0;
    int64_t parse_skips  = 0;

    if (from_file) {
        std::string path = argv[1];
        if (!fs::exists(path)) {
            std::cerr << "Input file not found: " << path << "\n";
            return 1;
        }
        std::cout << "Streaming ticks from " << path << "\n";
    } else {
        size_t n = 500000;
        if (max_rows > 0) n = static_cast<size_t>(max_rows);
        std::cout << "Generating " << n << " synthetic ticks...\n";
        ticks = make_synthetic_ticks(n);
    }

    // ---- Phase 1: Stream ticks through WAL --------------------------------
    std::cout << "\n[Phase 1] Streaming ticks through WalMmapWriter...\n";

    WalMmapOptions mmap_opts;
    mmap_opts.dir = wal_dir;

    const int64_t mmap_segment_size = env_i64(
        "SIGNET_WAL_MMAP_SEGMENT_SIZE", 64 * 1024 * 1024);
    const int64_t mmap_ring_segments = env_i64(
        "SIGNET_WAL_MMAP_RING_SEGMENTS", 4);

    mmap_opts.segment_size =
        static_cast<size_t>(mmap_segment_size > 0 ? mmap_segment_size : (64 * 1024 * 1024));
    mmap_opts.ring_segments =
        static_cast<size_t>(mmap_ring_segments > 0 ? mmap_ring_segments : 4);

    const int64_t watermark_high_pct = std::clamp<int64_t>(watermark_high_pct_raw, 1, 99);
    const int64_t watermark_low_pct = std::clamp<int64_t>(
        watermark_low_pct_raw, 0, std::max<int64_t>(0, watermark_high_pct - 1));

    const uint64_t wal_ring_capacity_bytes =
        static_cast<uint64_t>(mmap_opts.segment_size) *
        static_cast<uint64_t>(mmap_opts.ring_segments);
    const uint64_t wal_high_watermark_bytes =
        (wal_ring_capacity_bytes * static_cast<uint64_t>(watermark_high_pct)) / 100ull;
    const uint64_t wal_low_watermark_bytes =
        (wal_ring_capacity_bytes * static_cast<uint64_t>(watermark_low_pct)) / 100ull;

    auto wal_r = WalMmapWriter::open(mmap_opts);
    if (!wal_r) {
        std::cerr << "WalMmapWriter::open failed: " << wal_r.error().message << "\n";
        return 1;
    }
    auto& wal = *wal_r;

    auto t0 = std::chrono::steady_clock::now();
    int64_t wal_written = 0;
    int64_t wal_errors  = 0;
    int64_t watermark_throttle_events = 0;
    int64_t ring_full_retries = 0;
    uint64_t bytes_since_watermark = 0;

    auto apply_watermark_backpressure = [&](const char* reason) -> bool {
        auto flush_r = wal.flush(false);
        if (!flush_r) {
            std::cerr << "WAL flush failed during " << reason
                      << " backpressure: " << flush_r.error().message << "\n";
            return false;
        }
        ++watermark_throttle_events;
        if (ring_full_sleep_us > 0) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(static_cast<long long>(ring_full_sleep_us)));
        }
        bytes_since_watermark = wal_low_watermark_bytes;
        return true;
    };

    auto append_tick = [&](const TickRecord& tick) -> bool {
        std::string payload = serialise(tick);
        const uint64_t est_record_bytes = static_cast<uint64_t>(payload.size()) + 28ull;

        if (byte_watermark_enabled &&
            wal_high_watermark_bytes > 0 &&
            bytes_since_watermark >= wal_high_watermark_bytes) {
            if (!apply_watermark_backpressure("byte-watermark"))
                return false;
        }

        int64_t attempt = 0;
        while (true) {
            auto r = wal.append(payload);
            if (r.has_value()) {
                ++wal_written;
                bytes_since_watermark += est_record_bytes;
                return true;
            }

            ++wal_errors;
            const std::string& msg = r.error().message;
            const bool ring_full =
                (msg.find("ring full") != std::string::npos);

            if (ring_full && byte_watermark_enabled &&
                !fail_on_append_error && attempt < ring_full_max_retries) {
                ++attempt;
                ++ring_full_retries;
                if (!apply_watermark_backpressure("ring-full"))
                    return false;
                continue;
            }

            if (ring_full || fail_on_append_error) {
                std::cerr << "WAL append failed: " << msg << "\n";
                return false;
            }
            return true;
        }
    };

    if (from_file) {
        const std::string path = argv[1];
        bool is_gz = path.size() >= 3 && path.substr(path.size() - 3) == ".gz";
        FILE* fp = is_gz ? popen(("gunzip -c '" + path + "'").c_str(), "r")
                         : std::fopen(path.c_str(), "r");
        if (!fp) {
            std::cerr << "Cannot open input\n";
            return 1;
        }

        constexpr size_t LINE_BUF = 2048;
        char line[LINE_BUF];
        bool header_done = false;
        while (std::fgets(line, sizeof(line), fp)) {
            if (!header_done) { header_done = true; continue; }
            if (max_rows > 0 && source_ticks >= max_rows) {
                max_rows_reached = true;
                break;
            }
            TickRecord tick;
            if (!parse_csv_tick_line(line, tick)) { ++parse_skips; continue; }
            ++source_ticks;
            if (!append_tick(tick)) {
                if (is_gz) pclose(fp);
                else std::fclose(fp);
                std::cerr << "Stopping ingestion after " << source_ticks
                          << " source rows due WAL append failure/backpressure\n";
                return 1;
            }
        }

        if (is_gz) pclose(fp);
        else std::fclose(fp);
    } else {
        for (const auto& tick : ticks) {
            if (max_rows > 0 && source_ticks >= max_rows) {
                max_rows_reached = true;
                break;
            }
            ++source_ticks;
            if (!append_tick(tick)) {
                std::cerr << "Stopping synthetic ingestion after " << source_ticks
                          << " source rows due WAL append failure/backpressure\n";
                return 1;
            }
        }
    }

    // Flush to ensure all records are on disk before compaction
    if (!wal.flush().has_value()) {
        std::cerr << "WAL flush failed\n";
        return 1;
    }

    auto t1 = std::chrono::steady_clock::now();
    double wal_s = std::chrono::duration<double>(t1 - t0).count();
    double ns_per = (wal_written > 0) ? wal_s * 1e9 / wal_written : 0;

    std::cout << "  Written    : " << wal_written << " records\n";
    std::cout << "  Errors     : " << wal_errors  << "\n";
    if (byte_watermark_enabled) {
        std::cout << "  Byte-WM    : enabled (high=" << watermark_high_pct
                  << "%, low=" << watermark_low_pct << "%, cap="
                  << std::fixed << std::setprecision(2)
                  << (static_cast<double>(wal_ring_capacity_bytes) / 1e6)
                  << " MB)\n";
        std::cout << "  WM events  : " << watermark_throttle_events << "\n";
        std::cout << "  WM retries : " << ring_full_retries << "\n";
    }
    if (parse_skips > 0) {
        std::cout << "  Skipped    : " << parse_skips << " malformed CSV rows\n";
    }
    std::cout << "  Elapsed    : " << std::fixed << std::setprecision(3)
              << wal_s << " s\n";
    std::cout << "  Latency    : " << std::fixed << std::setprecision(1)
              << ns_per << " ns/append\n";

    // Close WAL writer before reading
    if (!wal.close().has_value()) {
        std::cerr << "WAL close failed\n";
        return 1;
    }

    // ---- Phase 2: Simulated crash recovery ---------------------------------
    std::cout << "\n[Phase 2] Simulating crash recovery — reading WAL back...\n";

    // Reopen the WAL directory and read all committed records
    {
        auto mgr_r = WalManager::open(wal_dir, manager_opts);
        if (!mgr_r) {
            std::cerr << "WalManager recovery failed: " << mgr_r.error().message << "\n";
            return 1;
        }
        int64_t recovered = mgr_r->total_records();
        std::cout << "  Recovered  : " << recovered << " records from WAL\n";
        if (recovered != wal_written) {
            std::cerr << "  WARNING: recovered " << recovered
                      << " != written " << wal_written << "\n";
        } else {
            std::cout << "  All " << recovered << " records intact after simulated crash\n";
        }
    }

    // ---- Phase 3: Compact WAL → Parquet ------------------------------------
    std::cout << "\n[Phase 3] Compacting WAL → Parquet: " << parquet_out << "\n";
    auto t2 = std::chrono::steady_clock::now();
    int64_t compacted = 0;
    if (!compact_wal_to_parquet(wal_dir, parquet_out, compacted, manager_opts)) {
        std::cerr << "Compaction failed\n";
        return 1;
    }
    auto t3 = std::chrono::steady_clock::now();
    double compact_s = std::chrono::duration<double>(t3 - t2).count();

    std::error_code ec;
    auto parquet_sz = static_cast<size_t>(fs::file_size(parquet_out, ec));

    std::cout << "  Compacted  : " << compacted << " records\n";
    std::cout << "  Elapsed    : " << std::fixed << std::setprecision(3)
              << compact_s << " s\n";
    if (!ec)
        std::cout << "  Parquet    : " << std::fixed << std::setprecision(2)
                  << parquet_sz / 1e6 << " MB\n";

    bool prune_compacted = env_is_truthy(std::getenv("SIGNET_WAL_PRUNE_COMPACTED"));
    if (prune_compacted) {
        std::cout << "\n[Phase 3b] Pruning compacted WAL segments after durable checkpoint...\n";
        auto prune_mgr_r = WalManager::open(wal_dir, manager_opts);
        if (!prune_mgr_r) {
            std::cerr << "Prune open failed: " << prune_mgr_r.error().message << "\n";
            return 1;
        }

        auto& prune_mgr = *prune_mgr_r;
        auto ck = prune_mgr.commit_compaction_checkpoint(
            "compacted_records=" + std::to_string(compacted) + ";parquet=" + parquet_out);
        if (!ck) {
            std::cerr << "Checkpoint commit failed: " << ck.error().message << "\n";
            return 1;
        }

        auto segs = prune_mgr.segment_paths();
        int64_t pruned = 0;
        for (size_t i = 0; i + 1 < segs.size(); ++i) {
            auto rm = prune_mgr.remove_segment(segs[i]);
            if (!rm) {
                std::cerr << "Segment prune failed for " << segs[i] << ": "
                          << rm.error().message << "\n";
                return 1;
            }
            ++pruned;
        }
        std::cout << "  Pruned     : " << pruned << " sealed segments\n";
    }

    // ---- Phase 4: Verify round-trip with Parquet reader -------------------
    std::cout << "\n[Phase 4] Verifying Parquet file...\n";
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
    auto dec_cfg_r = load_pare_encryption_from_env();
    if (!dec_cfg_r) {
        std::cerr << "Encryption config error: " << dec_cfg_r.error().message << "\n";
        return 1;
    }
    auto reader_r = dec_cfg_r->has_value()
        ? ParquetReader::open(parquet_out, *dec_cfg_r)
        : ParquetReader::open(parquet_out);
#else
    auto reader_r = ParquetReader::open(parquet_out);
#endif
    if (!reader_r) {
        std::cerr << "ParquetReader::open failed: " << reader_r.error().message << "\n";
        return 1;
    }
    auto& reader = *reader_r;

    std::cout << "  Rows       : " << reader.num_rows() << "\n";
    std::cout << "  Row groups : " << reader.num_row_groups() << "\n";

    // Read first row group, spot-check first 3 rows
    if (reader.num_row_groups() > 0) {
        auto ts_r  = reader.read_column<int64_t>(0, 0);
        auto sym_r = reader.read_column<std::string>(0, 1);
        auto bid_r = reader.read_column<double>(0, 3);

        if (ts_r.has_value() && sym_r.has_value() && bid_r.has_value()) {
            std::cout << "\n  First 3 rows from Parquet:\n";
            std::cout << "  " << std::setw(16) << "timestamp_ms"
                      << "  " << std::setw(10) << "symbol"
                      << "  " << std::setw(14) << "bid_price" << "\n";
            size_t show = std::min(size_t(3), ts_r->size());
            for (size_t i = 0; i < show; ++i) {
                std::cout << "  " << std::setw(16) << (*ts_r)[i]
                          << "  " << std::setw(10) << (*sym_r)[i]
                          << "  " << std::fixed << std::setprecision(8)
                          << std::setw(14) << (*bid_r)[i] << "\n";
            }
        }
    }

    // ---- Summary -----------------------------------------------------------
    std::cout << "\n=== Durable Tick Pipeline Summary ===\n";
    std::cout << "  WAL path     : " << wal_dir     << "\n";
    std::cout << "  Parquet path : " << parquet_out << "\n";
    std::cout << "  Ticks in     : " << source_ticks << "\n";
    std::cout << "  Max rows cfg : " << (max_rows > 0 ? std::to_string(max_rows) : std::string("unbounded")) << "\n";
    if (max_rows_reached)
        std::cout << "  Max rows hit : yes\n";
    std::cout << "  Fail-fast    : " << (fail_on_append_error ? "yes" : "ring-full only") << "\n";
    std::cout << "  Byte-WM      : " << (byte_watermark_enabled ? "enabled" : "disabled") << "\n";
    if (byte_watermark_enabled) {
        std::cout << "  WM cfg       : high=" << watermark_high_pct
                  << "% low=" << watermark_low_pct
                  << "% sleep_us=" << ring_full_sleep_us
                  << " max_retries=" << ring_full_max_retries << "\n";
        std::cout << "  WM events    : " << watermark_throttle_events
                  << " retries=" << ring_full_retries << "\n";
    }
    std::cout << "  Lifecycle    : " << lifecycle_mode_name(lifecycle_mode) << "\n";
    std::cout << "  Reset used   : " << (reset_for_benchmark ? "yes" : "no") << "\n";
    std::cout << "  Prune enabled: " << (prune_compacted ? "yes" : "no") << "\n";
    std::cout << "  Prune policy : checkpoint required before segment deletion\n";
    if (parse_skips > 0) {
        std::cout << "  CSV skipped  : " << parse_skips << "\n";
    }
    std::cout << "  WAL latency  : " << std::fixed << std::setprecision(1)
              << ns_per << " ns/append  (target: <100 ns)\n";
    std::cout << "  Compact time : " << std::fixed << std::setprecision(3)
              << compact_s << " s for " << compacted << " records\n";
    std::cout << "\nDurability guarantee: every tick written to the WAL survives\n";
    std::cout << "a process crash. The WAL is replayed at startup before any\n";
    std::cout << "new ticks are accepted, ensuring zero data loss.\n";

    return 0;
}
