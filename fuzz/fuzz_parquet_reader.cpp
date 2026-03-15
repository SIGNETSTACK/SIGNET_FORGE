// SPDX-License-Identifier: AGPL-3.0-or-later
// Fuzz harness for ParquetReader — header, Thrift metadata, page decoding.

#include "signet/reader.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    // Write fuzzed bytes to a temp file and attempt to open as Parquet
    char path[] = "/tmp/signet_fuzz_parquet_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;

    FILE* f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(path); return 0; }
    (void)fwrite(data, 1, size, f);
    fclose(f);

    auto result = signet::forge::ParquetReader::open(path);
    if (result.has_value()) {
        auto& reader = result.value();
        // Try reading each column from each row group
        for (int rg = 0; rg < reader.num_row_groups(); ++rg) {
            for (int col = 0; col < static_cast<int>(reader.schema().columns().size()); ++col) {
                (void)reader.read_column_as_strings(col, rg);
            }
        }
    }

    unlink(path);
    return 0;
}
