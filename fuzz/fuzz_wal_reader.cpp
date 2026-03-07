// SPDX-License-Identifier: Apache-2.0
// Fuzz harness for WalReader — WAL file header and record parsing.

#include "signet/ai/wal.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    // Write fuzzed bytes to a temp file and attempt to open as WAL
    char path[] = "/tmp/signet_fuzz_wal_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return 0;

    FILE* f = fdopen(fd, "wb");
    if (!f) { close(fd); unlink(path); return 0; }
    (void)fwrite(data, 1, size, f);
    fclose(f);

    auto result = signet::forge::WalReader::open(path);
    if (result.has_value()) {
        auto& reader = result.value();
        // Iterate all entries until EOF or corruption
        while (true) {
            auto entry = reader.next();
            if (!entry.has_value()) break;        // I/O error
            if (!entry.value().has_value()) break; // EOF or truncation
        }
        reader.close();
    }

    unlink(path);
    return 0;
}
