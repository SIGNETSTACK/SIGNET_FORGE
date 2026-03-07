// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// ---------------------------------------------------------------------------
// basic_write.cpp — Minimal example: write a Parquet file with Signet
//
// Demonstrates:
//   - Building a schema with SchemaBuilder (fluent API)
//   - Opening a ParquetWriter
//   - Writing rows as string vectors (auto-converted to typed columns)
//   - Closing the writer to finalize the file footer
//
// Build:
//   g++ -std=c++17 -I../include basic_write.cpp -o basic_write
//
// Run:
//   ./basic_write
//   # Produces: example.parquet
// ---------------------------------------------------------------------------

#include "signet/forge.hpp"
#include <iostream>

int main() {
    using namespace signet::forge;

    // 1. Build a typed schema using the fluent builder API.
    //    Each column<T>() call deduces the Parquet physical type from T.
    auto schema = SchemaBuilder("example")
        .column<int64_t>("id")
        .column<double>("price")
        .column<std::string>("symbol")
        .column<bool>("is_active")
        .build();

    // 2. Open a writer. Returns expected<ParquetWriter> — check for errors.
    auto writer = ParquetWriter::open("example.parquet", schema);
    if (!writer) {
        std::cerr << "Failed to open file: " << writer.error().message << "\n";
        return 1;
    }

    // 3. Write rows. Each row is a vector of strings that get converted to the
    //    column's physical type (INT64, DOUBLE, BYTE_ARRAY, BOOLEAN) on flush.
    writer->write_row({"1", "100.50", "AAPL", "true"});
    writer->write_row({"2", "250.75", "GOOGL", "true"});
    writer->write_row({"3", "50.25", "TSLA", "false"});

    // 4. Close the writer. This flushes any buffered rows, writes the Thrift
    //    FileMetaData footer, the footer length, and the closing PAR1 magic.
    auto result = writer->close();
    if (!result) {
        std::cerr << "Failed to close file: " << result.error().message << "\n";
        return 1;
    }

    std::cout << "Wrote " << writer->rows_written() << " rows to example.parquet\n";
    return 0;
}
