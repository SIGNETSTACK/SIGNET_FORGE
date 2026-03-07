// SPDX-License-Identifier: Apache-2.0
// Fuzz harness for delta::decode_int64 — DELTA_BINARY_PACKED decoding.

#include "signet/encoding/delta.hpp"

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    // Try various num_values to exercise header parsing + block decoding
    for (size_t num_values : {size_t(1), size_t(32), size_t(128), size_t(1024), size_t(10000)}) {
        (void)signet::forge::delta::decode_int64(data, size, num_values);
        (void)signet::forge::delta::decode_int32(data, size, num_values);
    }

    return 0;
}
