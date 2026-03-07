// SPDX-License-Identifier: Apache-2.0
// Fuzz harness for ThriftCompactDecoder — all Thrift deserialization paths.

#include "signet/thrift/compact.hpp"

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    signet::forge::thrift::CompactDecoder decoder(data, size);

    // Iterate fields until STOP or error
    while (decoder.good() && decoder.remaining() > 0) {
        auto fh = decoder.read_field_header();
        if (!decoder.good() || fh.is_stop()) break;

        // Exercise every type's read path based on Thrift type id
        switch (fh.thrift_type) {
            case 1:  // BOOL_TRUE
            case 2:  // BOOL_FALSE
                (void)decoder.read_bool();
                break;
            case 3:  // I8 / BYTE
            case 5:  // I32
                (void)decoder.read_i32();
                break;
            case 6:  // I64
                (void)decoder.read_i64();
                break;
            case 7:  // DOUBLE
                (void)decoder.read_double();
                break;
            case 8:  // BINARY / STRING
                (void)decoder.read_string();
                break;
            case 9:  // LIST
            case 10: // SET
            {
                auto lh = decoder.read_list_header();
                if (!decoder.good()) break;
                // Read up to 64 elements to avoid hanging
                for (int32_t i = 0; i < lh.size && i < 64 && decoder.good(); ++i) {
                    decoder.skip_field(lh.elem_type);
                }
                break;
            }
            case 11: // MAP — handled entirely by skip_field
                decoder.skip_field(fh.thrift_type);
                break;
            case 12: // STRUCT
                decoder.begin_struct();
                // Read one level of nested fields
                while (decoder.good()) {
                    auto inner = decoder.read_field_header();
                    if (!decoder.good() || inner.is_stop()) break;
                    decoder.skip_field(inner.thrift_type);
                }
                decoder.end_struct();
                break;
            default:
                decoder.skip_field(fh.thrift_type);
                break;
        }
    }

    return 0;
}
