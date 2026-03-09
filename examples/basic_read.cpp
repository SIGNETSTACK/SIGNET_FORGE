// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// basic_read.cpp — Minimal example: read and inspect a Parquet file
//
// Demonstrates:
//   - Opening a ParquetReader
//   - Inspecting file metadata (row count, row groups, creator string)
//   - Iterating the schema to print column names and types
//   - Reading all rows as strings and printing them in tabular form
//
// Build:
//   g++ -std=c++17 -I../include basic_read.cpp -o basic_read
//
// Run:
//   ./basic_read example.parquet
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"
#include <algorithm>
#include <iostream>

using namespace signet::forge;

// Helper: convert PhysicalType enum to a human-readable name.
static const char* physical_type_name(PhysicalType pt) {
    switch (pt) {
    case PhysicalType::BOOLEAN:              return "BOOLEAN";
    case PhysicalType::INT32:                return "INT32";
    case PhysicalType::INT64:                return "INT64";
    case PhysicalType::INT96:                return "INT96";
    case PhysicalType::FLOAT:                return "FLOAT";
    case PhysicalType::DOUBLE:               return "DOUBLE";
    case PhysicalType::BYTE_ARRAY:           return "BYTE_ARRAY";
    case PhysicalType::FIXED_LEN_BYTE_ARRAY: return "FIXED_LEN_BYTE_ARRAY";
    }
    return "UNKNOWN";
}

// Helper: convert LogicalType enum to a human-readable name.
static const char* logical_type_name(LogicalType lt) {
    switch (lt) {
    case LogicalType::NONE:           return "";
    case LogicalType::STRING:         return " (STRING)";
    case LogicalType::ENUM:           return " (ENUM)";
    case LogicalType::UUID:           return " (UUID)";
    case LogicalType::DATE:           return " (DATE)";
    case LogicalType::TIME_MS:        return " (TIME_MS)";
    case LogicalType::TIME_US:        return " (TIME_US)";
    case LogicalType::TIME_NS:        return " (TIME_NS)";
    case LogicalType::TIMESTAMP_MS:   return " (TIMESTAMP_MS)";
    case LogicalType::TIMESTAMP_US:   return " (TIMESTAMP_US)";
    case LogicalType::TIMESTAMP_NS:   return " (TIMESTAMP_NS)";
    case LogicalType::DECIMAL:        return " (DECIMAL)";
    case LogicalType::JSON:           return " (JSON)";
    case LogicalType::BSON:           return " (BSON)";
    case LogicalType::FLOAT16:        return " (FLOAT16)";
    case LogicalType::FLOAT32_VECTOR: return " (FLOAT32_VECTOR)";
    }
    return "";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <file.parquet>\n";
        return 1;
    }

    // 1. Open the file. The reader maps the entire file into memory,
    //    verifies PAR1 magic bytes, and deserializes the Thrift footer.
    auto reader = ParquetReader::open(argv[1]);
    if (!reader) {
        std::cerr << "Failed to open: " << reader.error().message << "\n";
        return 1;
    }

    // 2. Print file-level metadata.
    std::cout << "File:       " << argv[1] << "\n";
    std::cout << "Rows:       " << reader->num_rows() << "\n";
    std::cout << "Row groups: " << reader->num_row_groups() << "\n";
    std::cout << "Created by: " << reader->created_by() << "\n";

    // 3. Print the schema — one line per column with type information.
    const auto& schema = reader->schema();
    std::cout << "\nSchema (" << schema.name() << "):\n";
    for (size_t i = 0; i < schema.num_columns(); ++i) {
        const auto& col = schema.column(i);
        std::cout << "  " << col.name << " : "
                  << physical_type_name(col.physical_type)
                  << logical_type_name(col.logical_type) << "\n";
    }

    // 4. Read all rows as strings.
    auto rows = reader->read_all();
    if (!rows) {
        std::cerr << "Failed to read: " << rows.error().message << "\n";
        return 1;
    }

    // 5. Print column headers (tab-separated).
    std::cout << "\n";
    for (size_t c = 0; c < schema.num_columns(); ++c) {
        if (c > 0) std::cout << "\t";
        std::cout << schema.column(c).name;
    }
    std::cout << "\n";

    // 6. Print data rows (cap at 20 to keep output readable).
    size_t limit = (std::min)(rows->size(), size_t(20));
    for (size_t r = 0; r < limit; ++r) {
        for (size_t c = 0; c < (*rows)[r].size(); ++c) {
            if (c > 0) std::cout << "\t";
            std::cout << (*rows)[r][c];
        }
        std::cout << "\n";
    }

    if (rows->size() > 20) {
        std::cout << "... (" << rows->size() - 20 << " more rows)\n";
    }

    return 0;
}
