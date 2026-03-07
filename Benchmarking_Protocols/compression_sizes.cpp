// SPDX-License-Identifier: Apache-2.0
// Benchmarking_Protocols/compression_sizes.cpp
// Standalone tool: writes 1M-row Parquet files with each codec and reports sizes.
// Saves files to results/ for inspection.
//
// Build: add to CMakeLists.txt or compile manually
// Run:   ./build-benchmarks/compression_sizes

#include "common.hpp"
#include <iostream>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;
using namespace signet::forge;

int main() {
    // Register all compression codecs (writer only auto-registers Snappy;
    // reader auto-registers all, but we might not call reader first)
#ifdef SIGNET_HAS_ZSTD
    signet::forge::register_zstd_codec();
#endif
#ifdef SIGNET_HAS_LZ4
    signet::forge::register_lz4_codec();
#endif
#ifdef SIGNET_HAS_GZIP
    signet::forge::register_gzip_codec();
#endif

    auto data_dir = bench::default_data_dir();
    auto data = bench::load_or_generate(data_dir, 1'000'000);
    std::cout << "Loaded " << data.size() << " rows\n\n";

    // Raw data size: 9 columns
    // 1 x int64 (8B) + 2 x string (~8B avg) + 6 x double (8B) = ~72 bytes/row
    size_t raw_size = data.size() * (8 + 8 + 8 + 6 * 8);  // approximate
    std::cout << "Approximate raw data size: " << raw_size / (1024*1024) << " MB ("
              << raw_size << " bytes)\n\n";

    std::string out_dir = "Benchmarking_Protocols/results";
    if (!fs::exists(out_dir)) {
        out_dir = "results";  // fallback if run from Benchmarking_Protocols/
    }

    struct Config {
        std::string name;
        std::string filename;
        bench::WriterOptions (*opts_fn)();
    };

    // Use lambdas that return WriterOptions
    auto plain_fn     = []() -> bench::WriterOptions { return bench::plain_opts(); };
    auto snappy_fn    = []() -> bench::WriterOptions { return bench::snappy_opts(); };
    auto zstd_fn      = []() -> bench::WriterOptions { return bench::zstd_opts(); };
    auto lz4_fn       = []() -> bench::WriterOptions { return bench::lz4_opts(); };
    auto gzip_fn      = []() -> bench::WriterOptions { return bench::gzip_opts(); };
    auto opt_snap_fn  = []() -> bench::WriterOptions { return bench::optimal_snappy_opts(); };
    auto opt_zstd_fn  = []() -> bench::WriterOptions { return bench::optimal_zstd_opts(); };

    // PLAIN encoding variants
    struct TestCase {
        std::string name;
        std::string filename;
        WriterOptions opts;
    };

    std::vector<TestCase> cases;
    cases.push_back({"PLAIN + Uncompressed",       "1m_plain_uncompressed.parquet",  bench::plain_opts()});
    cases.push_back({"PLAIN + Snappy",             "1m_plain_snappy.parquet",        bench::snappy_opts()});
#ifdef SIGNET_HAS_ZSTD
    cases.push_back({"PLAIN + ZSTD",              "1m_plain_zstd.parquet",          bench::zstd_opts()});
#endif
#ifdef SIGNET_HAS_LZ4
    cases.push_back({"PLAIN + LZ4",               "1m_plain_lz4.parquet",           bench::lz4_opts()});
#endif
#ifdef SIGNET_HAS_GZIP
    cases.push_back({"PLAIN + Gzip",              "1m_plain_gzip.parquet",          bench::gzip_opts()});
#endif
    cases.push_back({"Optimal + Snappy",           "1m_optimal_snappy.parquet",      bench::optimal_snappy_opts()});
#ifdef SIGNET_HAS_ZSTD
    cases.push_back({"Optimal + ZSTD",            "1m_optimal_zstd.parquet",        bench::optimal_zstd_opts()});
#endif

    // Optimal encoding only (no bloom/page index overhead)
    {
        WriterOptions o;
        o.compression = Compression::UNCOMPRESSED;
        o.auto_encoding = true;
        cases.push_back({"Optimal enc, no compress", "1m_optimal_nocompress.parquet", o});
    }
    // Optimal + Uncompressed (with bloom + page index = full overhead)
    {
        WriterOptions o;
        o.compression = Compression::UNCOMPRESSED;
        o.auto_encoding = true;
        o.enable_bloom_filter = true;
        o.enable_page_index = true;
        cases.push_back({"Optimal + bloom + idx", "1m_optimal_bloom_idx.parquet", o});
    }
    // Optimal encoding + ZSTD (no bloom/page index)
    {
        WriterOptions o;
        o.compression = Compression::ZSTD;
        o.auto_encoding = true;
        cases.push_back({"Optimal enc + ZSTD", "1m_optimal_zstd_nobloom.parquet", o});
    }
#ifdef SIGNET_HAS_LZ4
    // Optimal encoding + LZ4 (no bloom/page index)
    {
        WriterOptions o;
        o.compression = Compression::LZ4_RAW;
        o.auto_encoding = true;
        cases.push_back({"Optimal enc + LZ4", "1m_optimal_lz4_nobloom.parquet", o});
    }
#endif

    std::cout << std::left << std::setw(30) << "Configuration"
              << std::right << std::setw(14) << "File Size"
              << std::setw(12) << "Size (MB)"
              << std::setw(12) << "Ratio"
              << std::setw(14) << "Bytes/Row"
              << "\n";
    std::cout << std::string(82, '-') << "\n";

    size_t uncompressed_size = 0;
    size_t n_rows = data.size();

    for (auto& tc : cases) {
        std::string path = out_dir + "/" + tc.filename;
        bool ok = bench::write_tick_parquet(path, data, tc.opts);
        if (!ok) {
            std::cout << std::left << std::setw(30) << tc.name << "  FAILED\n";
            continue;
        }

        size_t sz = bench::file_size_bytes(path);
        double mb = static_cast<double>(sz) / (1024.0 * 1024.0);

        if (tc.name == "PLAIN + Uncompressed") {
            uncompressed_size = sz;
        }

        double ratio = uncompressed_size > 0 ? static_cast<double>(uncompressed_size) / static_cast<double>(sz) : 0;
        double bytes_per_row = static_cast<double>(sz) / static_cast<double>(n_rows);

        std::cout << std::left << std::setw(30) << tc.name
                  << std::right << std::setw(14) << sz
                  << std::setw(10) << std::fixed << std::setprecision(2) << mb << " MB"
                  << std::setw(10) << std::fixed << std::setprecision(2) << ratio << ":1"
                  << std::setw(12) << std::fixed << std::setprecision(1) << bytes_per_row
                  << "\n";
    }

    std::cout << "\nFiles saved to " << out_dir << "/\n";
    return 0;
}
