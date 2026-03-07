// SPDX-License-Identifier: Apache-2.0
// Fuzz harness for RleDecoder — bit-packed RLE/hybrid decoding.

#include "signet/encoding/rle.hpp"

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1) return 0;

    // Use first byte as bit_width (clamped 0-64 inside RleDecoder)
    int bit_width = static_cast<int>(data[0] % 65);
    const uint8_t* buf = data + 1;
    size_t buf_size = size - 1;

    // Exercise iterator-style API
    {
        signet::forge::RleDecoder decoder(buf, buf_size, bit_width);
        uint64_t value = 0;
        while (decoder.get(&value)) {
            // consume value — fuzzer checks for UB/crashes
        }
    }

    // Exercise static batch-decode API
    for (size_t count : {size_t(1), size_t(16), size_t(256), size_t(4096)}) {
        (void)signet::forge::RleDecoder::decode(buf, buf_size, bit_width, count);
    }

    return 0;
}
