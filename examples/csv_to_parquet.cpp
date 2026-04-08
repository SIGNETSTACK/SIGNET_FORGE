// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// csv_to_parquet.cpp — CLI tool: convert a CSV file to Parquet
//
// Demonstrates:
//   - ParquetWriter::csv_to_parquet() — one-call conversion with auto type
//     detection (scans all rows, picks INT64 > DOUBLE > BOOLEAN > STRING)
//   - ParquetReader for post-conversion summary and compression stats
//
// Build:
//   g++ -std=c++17 -I../include csv_to_parquet.cpp -o csv_to_parquet
//
// Run:
//   ./csv_to_parquet data.csv                   # -> data.parquet
//   ./csv_to_parquet data.csv output.parquet    # -> output.parquet
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"
#include <filesystem>
#include <iomanip>
#include <iostream>

using namespace signet::forge;

// Helper: convert PhysicalType to a short display name for the summary table.
static const char* type_tag(PhysicalType pt) {
    switch (pt) {
    case PhysicalType::BOOLEAN:              return "bool";
    case PhysicalType::INT32:                return "int32";
    case PhysicalType::INT64:                return "int64";
    case PhysicalType::INT96:                return "int96";
    case PhysicalType::FLOAT:                return "float";
    case PhysicalType::DOUBLE:               return "double";
    case PhysicalType::BYTE_ARRAY:           return "string";
    case PhysicalType::FIXED_LEN_BYTE_ARRAY: return "fixed";
    }
    return "?";
}

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0] << " <input.csv> [output.parquet]\n";
        std::cerr << "\nConverts a CSV file to Parquet format.\n";
        std::cerr << "Column types are auto-detected (INT64 > DOUBLE > BOOLEAN > STRING).\n";
        std::cerr << "If output path is omitted, replaces .csv with .parquet.\n";
        return 1;
    }

    std::filesystem::path input = argv[1];
    std::filesystem::path output = (argc >= 3)
        ? std::filesystem::path(argv[2])
        : std::filesystem::path(input).replace_extension(".parquet");

    // Verify the input file exists before attempting conversion.
    if (!std::filesystem::exists(input)) {
        std::cerr << "Error: input file not found: " << input.string() << "\n";
        return 1;
    }

    // Convert. csv_to_parquet() reads the CSV header, scans all rows to detect
    // column types, builds a schema, and writes the Parquet file in one call.
    auto result = ParquetWriter::csv_to_parquet(input, output);
    if (!result) {
        std::cerr << "Conversion failed: " << result.error().message << "\n";
        return 1;
    }

    // Re-open the output file to print a summary.
    auto reader = ParquetReader::open(output);
    if (!reader) {
        // Conversion succeeded but we cannot read back — still report success.
        std::cout << "Converted " << input.filename().string()
                  << " -> " << output.filename().string() << "\n";
        return 0;
    }

    // Print conversion summary.
    std::cout << "Converted " << input.filename().string()
              << " -> " << output.filename().string() << "\n";
    std::cout << "  Rows:       " << reader->num_rows() << "\n";
    std::cout << "  Columns:    " << reader->schema().num_columns() << "\n";
    std::cout << "  Row groups: " << reader->num_row_groups() << "\n";

    // Show detected column types.
    const auto& schema = reader->schema();
    std::cout << "  Schema:\n";
    for (size_t i = 0; i < schema.num_columns(); ++i) {
        const auto& col = schema.column(i);
        std::cout << "    " << col.name << " : " << type_tag(col.physical_type) << "\n";
    }

    // Show file size comparison (CSV vs Parquet).
    std::error_code ec;
    auto in_size  = std::filesystem::file_size(input, ec);
    if (ec) in_size = 0;
    auto out_size = std::filesystem::file_size(output, ec);
    if (ec) out_size = 0;

    std::cout << "  Size:       " << in_size << " -> " << out_size << " bytes";
    if (in_size > 0) {
        double ratio = 100.0 * static_cast<double>(out_size)
                             / static_cast<double>(in_size);
        std::cout << " (" << std::fixed << std::setprecision(1) << ratio << "%)";
    }
    std::cout << "\n";

    return 0;
}
