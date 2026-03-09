// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// signet_cli.cpp -- Command-line tool for Parquet file inspection and conversion
//
// Usage:
//   signet <command> [options] <file>
//
// Commands:
//   inspect    Show file metadata, schema, row groups, and statistics
//   convert    Convert CSV to Parquet
//   validate   Verify Parquet file integrity
//   schema     Print schema in detail
//   head       Print first N rows
//   count      Print row count
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"
#include "signet/mmap_reader.hpp"
#include "signet/column_index.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

// Windows <mmsystem.h> defines TIME_MS as a macro — undefine to avoid collision.
#ifdef TIME_MS
#undef TIME_MS
#endif

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Helpers — enum-to-string conversion
// ---------------------------------------------------------------------------

static const char* physical_type_str(PhysicalType pt) {
    switch (pt) {
        case PhysicalType::BOOLEAN:              return "BOOLEAN";
        case PhysicalType::INT32:                return "INT32";
        case PhysicalType::INT64:                return "INT64";
        case PhysicalType::INT96:                return "INT96";
        case PhysicalType::FLOAT:                return "FLOAT";
        case PhysicalType::DOUBLE:               return "DOUBLE";
        case PhysicalType::BYTE_ARRAY:           return "BYTE_ARRAY";
        case PhysicalType::FIXED_LEN_BYTE_ARRAY: return "FIXED_LEN_BYTE_ARRAY";
        default:                                 return "UNKNOWN";
    }
}

static const char* logical_type_str(LogicalType lt) {
    switch (lt) {
        case LogicalType::NONE:           return "NONE";
        case LogicalType::STRING:         return "STRING";
        case LogicalType::ENUM:           return "ENUM";
        case LogicalType::UUID:           return "UUID";
        case LogicalType::DATE:           return "DATE";
        case LogicalType::TIME_MS:        return "TIME_MS";
        case LogicalType::TIME_US:        return "TIME_US";
        case LogicalType::TIME_NS:        return "TIME_NS";
        case LogicalType::TIMESTAMP_MS:   return "TIMESTAMP_MS";
        case LogicalType::TIMESTAMP_US:   return "TIMESTAMP_US";
        case LogicalType::TIMESTAMP_NS:   return "TIMESTAMP_NS";
        case LogicalType::DECIMAL:        return "DECIMAL";
        case LogicalType::JSON:           return "JSON";
        case LogicalType::BSON:           return "BSON";
        case LogicalType::FLOAT16:        return "FLOAT16";
        case LogicalType::FLOAT32_VECTOR: return "FLOAT32_VECTOR";
        default:                          return "UNKNOWN";
    }
}

static const char* encoding_str(Encoding enc) {
    switch (enc) {
        case Encoding::PLAIN:                   return "PLAIN";
        case Encoding::PLAIN_DICTIONARY:        return "PLAIN_DICTIONARY";
        case Encoding::RLE:                     return "RLE";
        case Encoding::BIT_PACKED:              return "BIT_PACKED";
        case Encoding::DELTA_BINARY_PACKED:     return "DELTA_BINARY_PACKED";
        case Encoding::DELTA_LENGTH_BYTE_ARRAY: return "DELTA_LENGTH_BYTE_ARRAY";
        case Encoding::DELTA_BYTE_ARRAY:        return "DELTA_BYTE_ARRAY";
        case Encoding::RLE_DICTIONARY:          return "RLE_DICTIONARY";
        case Encoding::BYTE_STREAM_SPLIT:       return "BYTE_STREAM_SPLIT";
        default:                                return "UNKNOWN";
    }
}

static const char* compression_str(Compression c) {
    switch (c) {
        case Compression::UNCOMPRESSED: return "UNCOMPRESSED";
        case Compression::SNAPPY:       return "SNAPPY";
        case Compression::GZIP:         return "GZIP";
        case Compression::LZO:          return "LZO";
        case Compression::BROTLI:       return "BROTLI";
        case Compression::LZ4:          return "LZ4";
        case Compression::ZSTD:         return "ZSTD";
        case Compression::LZ4_RAW:      return "LZ4_RAW";
        default:                        return "UNKNOWN";
    }
}

static const char* repetition_str(Repetition r) {
    switch (r) {
        case Repetition::REQUIRED: return "REQUIRED";
        case Repetition::OPTIONAL: return "OPTIONAL";
        case Repetition::REPEATED: return "REPEATED";
        default:                   return "UNKNOWN";
    }
}

static std::string human_size(size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f KiB", bytes / 1024.0);
        return buf;
    }
    if (bytes < 1024ULL * 1024 * 1024) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f MiB", bytes / (1024.0 * 1024.0));
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f GiB", bytes / (1024.0 * 1024.0 * 1024.0));
    return buf;
}

// ---------------------------------------------------------------------------
// Timer utility
// ---------------------------------------------------------------------------

class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

static void print_usage() {
    std::cerr
        << "signet -- Parquet file toolkit\n"
        << "\n"
        << "Usage:\n"
        << "  signet <command> [options] <file>\n"
        << "\n"
        << "Commands:\n"
        << "  inspect  <file>                  Show metadata, schema, row groups\n"
        << "  convert  <csv> <parquet> [opts]  Convert CSV to Parquet\n"
        << "  validate <file>                  Verify file integrity\n"
        << "  schema   <file>                  Print detailed schema\n"
        << "  head     <file> [N]              Print first N rows (default 10)\n"
        << "  count    <file>                  Print row count\n"
        << "\n"
        << "Convert options:\n"
        << "  --compression <snappy|zstd|lz4|gzip|none>  (default: none)\n"
        << "  --encoding    <plain|auto>                  (default: auto)\n"
        << "  --row-group-size <N>                        (default: 65536)\n";
}

// ---------------------------------------------------------------------------
// cmd_inspect
// ---------------------------------------------------------------------------

static int cmd_inspect(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: signet inspect <file>\n";
        return 1;
    }

    Timer timer;
    std::filesystem::path file_path(argv[2]);

    auto reader_result = MmapParquetReader::open(file_path);
    if (!reader_result) {
        std::cerr << "Error: " << reader_result.error().message << "\n";
        return 1;
    }
    auto& reader = *reader_result;

    std::error_code ec;
    auto file_size = std::filesystem::file_size(file_path, ec);

    // File info
    std::cout << "File: " << file_path.string() << "\n";
    if (!ec) std::cout << "Size: " << human_size(file_size)
                       << " (" << file_size << " bytes)\n";
    std::cout << "Created by: " << (reader.created_by().empty()
                                        ? "(not set)" : reader.created_by()) << "\n";
    std::cout << "Rows: " << reader.num_rows() << "\n";
    std::cout << "Row groups: " << reader.num_row_groups() << "\n";
    std::cout << "Columns: " << reader.schema().num_columns() << "\n";
    std::cout << "\n";

    // Schema summary
    std::cout << "Schema: " << reader.schema().name() << "\n";
    for (size_t c = 0; c < reader.schema().num_columns(); ++c) {
        const auto& col = reader.schema().column(c);
        std::cout << "  " << c << ": " << col.name
                  << " (" << physical_type_str(col.physical_type);
        if (col.logical_type != LogicalType::NONE) {
            std::cout << " / " << logical_type_str(col.logical_type);
        }
        std::cout << ", " << repetition_str(col.repetition) << ")\n";
    }
    std::cout << "\n";

    // Row groups and column chunks
    for (int64_t rg_idx = 0; rg_idx < reader.num_row_groups(); ++rg_idx) {
        auto rg_info = reader.row_group(static_cast<size_t>(rg_idx));
        std::cout << "Row Group " << rg_idx << ": "
                  << rg_info.num_rows << " rows, "
                  << human_size(static_cast<size_t>(rg_info.total_byte_size)) << "\n";

        for (size_t c = 0; c < reader.schema().num_columns(); ++c) {
            const auto* stats = reader.column_statistics(
                static_cast<size_t>(rg_idx), c);

            std::cout << "  Column " << c << " ("
                      << reader.schema().column(c).name << "): ";

            if (stats) {
                if (stats->null_count.has_value()) {
                    std::cout << "nulls=" << *stats->null_count << " ";
                }
                if (stats->distinct_count.has_value()) {
                    std::cout << "distinct=" << *stats->distinct_count << " ";
                }
                if (stats->min_value.has_value() && stats->max_value.has_value()) {
                    std::cout << "has min/max";
                }
            } else {
                std::cout << "(no statistics)";
            }
            std::cout << "\n";
        }
        std::cout << "\n";
    }

    // Key-value metadata
    const auto& kv_meta = reader.key_value_metadata();
    if (!kv_meta.empty()) {
        std::cout << "Key-Value Metadata:\n";
        for (const auto& kv : kv_meta) {
            std::cout << "  " << kv.key << " = "
                      << kv.value.value_or("(null)") << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "Completed in " << std::fixed << std::setprecision(2)
              << timer.elapsed_ms() << "ms\n";
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_convert
// ---------------------------------------------------------------------------

static int cmd_convert(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: signet convert <csv_file> <parquet_file> [options]\n";
        return 1;
    }

    std::filesystem::path csv_path(argv[2]);
    std::filesystem::path parquet_path(argv[3]);

    // Parse options
    Compression compression = Compression::UNCOMPRESSED;
    bool auto_enc = true;
    int64_t row_group_size = 65536;

    for (int i = 4; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--compression" && i + 1 < argc) {
            std::string val = argv[++i];
            if      (val == "snappy") compression = Compression::SNAPPY;
            else if (val == "zstd")   compression = Compression::ZSTD;
            else if (val == "lz4")    compression = Compression::LZ4;
            else if (val == "gzip")   compression = Compression::GZIP;
            else if (val == "none")   compression = Compression::UNCOMPRESSED;
            else {
                std::cerr << "Unknown compression codec: " << val << "\n";
                return 1;
            }
        } else if (arg == "--encoding" && i + 1 < argc) {
            std::string val = argv[++i];
            if      (val == "plain") auto_enc = false;
            else if (val == "auto")  auto_enc = true;
            else {
                std::cerr << "Unknown encoding: " << val << "\n";
                return 1;
            }
        } else if (arg == "--row-group-size" && i + 1 < argc) {
            row_group_size = std::atoll(argv[++i]);
            if (row_group_size <= 0) {
                std::cerr << "Invalid row group size\n";
                return 1;
            }
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
    }

    // Register codecs
    register_snappy_codec();

    Timer timer;

    // Get input file size for compression ratio
    std::error_code ec;
    auto csv_size = std::filesystem::file_size(csv_path, ec);

    WriterOptions opts;
    opts.compression    = compression;
    opts.auto_encoding  = auto_enc;
    opts.row_group_size = row_group_size;

    auto result = ParquetWriter::csv_to_parquet(csv_path, parquet_path, opts);
    if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }

    // Read back summary from the written file
    auto read_result = MmapParquetReader::open(parquet_path);
    if (!read_result) {
        // File was written successfully but we cannot read back — still report success
        std::cout << "Conversion complete.\n";
    } else {
        auto& reader = *read_result;
        auto parquet_size = std::filesystem::file_size(parquet_path, ec);

        std::cout << "Converted: " << csv_path.filename().string()
                  << " -> " << parquet_path.filename().string() << "\n";
        std::cout << "Rows:      " << reader.num_rows() << "\n";
        std::cout << "Columns:   " << reader.schema().num_columns() << "\n";
        std::cout << "Size:      " << human_size(parquet_size) << "\n";

        if (!ec && csv_size > 0 && parquet_size > 0) {
            double ratio = static_cast<double>(csv_size)
                         / static_cast<double>(parquet_size);
            std::cout << "Ratio:     " << std::fixed << std::setprecision(2)
                      << ratio << "x ("
                      << human_size(csv_size) << " CSV -> "
                      << human_size(parquet_size) << " Parquet)\n";
        }
    }

    std::cout << "Completed in " << std::fixed << std::setprecision(2)
              << timer.elapsed_ms() << "ms\n";
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_validate
// ---------------------------------------------------------------------------

static int cmd_validate(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: signet validate <file>\n";
        return 1;
    }

    Timer timer;
    std::filesystem::path file_path(argv[2]);

    // Register codecs for decompression during validation
    register_snappy_codec();

    auto reader_result = MmapParquetReader::open(file_path);
    if (!reader_result) {
        std::cerr << "INVALID: " << reader_result.error().message << "\n";
        return 1;
    }
    auto& reader = *reader_result;

    std::cout << "Validating: " << file_path.string() << "\n";
    std::cout << "  Magic bytes: OK (PAR1)\n";
    std::cout << "  Footer deserialization: OK\n";
    std::cout << "  Schema: " << reader.schema().num_columns() << " columns\n";

    int64_t total_rows_verified = 0;
    size_t  columns_checked = 0;
    int     errors = 0;

    for (int64_t rg = 0; rg < reader.num_row_groups(); ++rg) {
        auto rg_info = reader.row_group(static_cast<size_t>(rg));
        std::cout << "  Row group " << rg << " (" << rg_info.num_rows << " rows): ";

        bool rg_ok = true;
        for (size_t c = 0; c < reader.schema().num_columns(); ++c) {
            auto col_result = reader.read_column_as_strings(
                static_cast<size_t>(rg), c);
            if (!col_result) {
                std::cerr << "\n    ERROR column " << c << " ("
                          << reader.schema().column(c).name << "): "
                          << col_result.error().message << "\n";
                ++errors;
                rg_ok = false;
            }
            ++columns_checked;
        }

        if (rg_ok) {
            std::cout << "OK\n";
            total_rows_verified += rg_info.num_rows;
        }
    }

    std::cout << "\n";
    if (errors == 0) {
        std::cout << "File is valid.\n";
    } else {
        std::cout << "VALIDATION FAILED: " << errors << " error(s) found.\n";
    }
    std::cout << "  Total rows verified: " << total_rows_verified << "\n";
    std::cout << "  Columns checked:     " << columns_checked << "\n";
    std::cout << "Completed in " << std::fixed << std::setprecision(2)
              << timer.elapsed_ms() << "ms\n";

    return errors > 0 ? 1 : 0;
}

// ---------------------------------------------------------------------------
// cmd_schema
// ---------------------------------------------------------------------------

static int cmd_schema(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: signet schema <file>\n";
        return 1;
    }

    Timer timer;
    std::filesystem::path file_path(argv[2]);

    auto reader_result = MmapParquetReader::open(file_path);
    if (!reader_result) {
        std::cerr << "Error: " << reader_result.error().message << "\n";
        return 1;
    }
    auto& reader = *reader_result;
    const auto& schema = reader.schema();

    std::cout << "Schema: " << schema.name() << "\n";
    std::cout << "Columns: " << schema.num_columns() << "\n";
    std::cout << std::string(60, '-') << "\n";

    for (size_t c = 0; c < schema.num_columns(); ++c) {
        const auto& col = schema.column(c);

        std::cout << "[" << c << "] " << col.name << "\n";
        std::cout << "    Physical type: " << physical_type_str(col.physical_type) << "\n";
        std::cout << "    Logical type:  " << logical_type_str(col.logical_type) << "\n";
        std::cout << "    Repetition:    " << repetition_str(col.repetition) << "\n";

        if (col.physical_type == PhysicalType::FIXED_LEN_BYTE_ARRAY &&
            col.type_length > 0) {
            std::cout << "    Type length:   " << col.type_length << "\n";
        }
        if (col.logical_type == LogicalType::DECIMAL) {
            if (col.precision > 0) {
                std::cout << "    Precision:     " << col.precision << "\n";
            }
            if (col.scale >= 0) {
                std::cout << "    Scale:         " << col.scale << "\n";
            }
        }
    }

    std::cout << std::string(60, '-') << "\n";
    std::cout << "Completed in " << std::fixed << std::setprecision(2)
              << timer.elapsed_ms() << "ms\n";
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_head
// ---------------------------------------------------------------------------

static int cmd_head(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: signet head <file> [N]\n";
        return 1;
    }

    Timer timer;
    std::filesystem::path file_path(argv[2]);
    int64_t limit = 10;
    if (argc >= 4) {
        limit = std::atoll(argv[3]);
        if (limit <= 0) {
            std::cerr << "Invalid row count: " << argv[3] << "\n";
            return 1;
        }
    }

    // Register codecs for decompression during reads
    register_snappy_codec();

    auto reader_result = MmapParquetReader::open(file_path);
    if (!reader_result) {
        std::cerr << "Error: " << reader_result.error().message << "\n";
        return 1;
    }
    auto& reader = *reader_result;
    const auto& schema = reader.schema();

    // Print header
    for (size_t c = 0; c < schema.num_columns(); ++c) {
        if (c > 0) std::cout << "\t";
        std::cout << schema.column(c).name;
    }
    std::cout << "\n";

    // Read rows, stopping at limit
    int64_t rows_printed = 0;

    auto all_result = reader.read_all();
    if (!all_result) {
        std::cerr << "Error reading data: " << all_result.error().message << "\n";
        return 1;
    }

    const auto& rows = *all_result;
    for (const auto& row : rows) {
        if (rows_printed >= limit) break;

        for (size_t c = 0; c < row.size(); ++c) {
            if (c > 0) std::cout << "\t";
            std::cout << row[c];
        }
        std::cout << "\n";
        ++rows_printed;
    }

    std::cout << "---\n";
    std::cout << rows_printed << " of " << reader.num_rows() << " rows shown\n";
    std::cout << "Completed in " << std::fixed << std::setprecision(2)
              << timer.elapsed_ms() << "ms\n";
    return 0;
}

// ---------------------------------------------------------------------------
// cmd_count
// ---------------------------------------------------------------------------

static int cmd_count(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: signet count <file>\n";
        return 1;
    }

    Timer timer;
    std::filesystem::path file_path(argv[2]);

    auto reader_result = MmapParquetReader::open(file_path);
    if (!reader_result) {
        std::cerr << "Error: " << reader_result.error().message << "\n";
        return 1;
    }

    std::cout << reader_result->num_rows() << "\n";
    std::cout << "Completed in " << std::fixed << std::setprecision(2)
              << timer.elapsed_ms() << "ms\n";
    return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    if (command == "inspect")  return cmd_inspect(argc, argv);
    if (command == "convert")  return cmd_convert(argc, argv);
    if (command == "validate") return cmd_validate(argc, argv);
    if (command == "schema")   return cmd_schema(argc, argv);
    if (command == "head")     return cmd_head(argc, argv);
    if (command == "count")    return cmd_count(argc, argv);

    if (command == "--help" || command == "-h" || command == "help") {
        print_usage();
        return 0;
    }

    std::cerr << "Unknown command: " << command << "\n\n";
    print_usage();
    return 1;
}
