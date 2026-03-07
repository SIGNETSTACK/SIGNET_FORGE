// SPDX-License-Identifier: Apache-2.0
// Fuzz harness for Arrow C Data Interface import bridge — corrupted ArrowArray/ArrowSchema.

#include "signet/interop/arrow_bridge.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

// Dummy release callbacks (required by Arrow C Data Interface spec)
static void release_schema(ArrowSchema* schema) {
    if (schema) schema->release = nullptr;
}

static void release_array(ArrowArray* array) {
    if (array) array->release = nullptr;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Need at least enough bytes to populate the key fields
    if (size < 32) return 0;

    // Interpret fuzzed bytes as ArrowArray fields
    int64_t length     = 0;
    int64_t null_count = 0;
    int64_t offset     = 0;
    std::memcpy(&length,     data,      sizeof(int64_t));
    std::memcpy(&null_count, data + 8,  sizeof(int64_t));
    std::memcpy(&offset,     data + 16, sizeof(int64_t));

    // Use remaining bytes as the data buffer
    const uint8_t* buf_data = data + 24;
    size_t buf_size = size - 24;

    // Cap offset + length to what the buffer can actually hold (8 bytes
    // per float64) to test realistic corruption, not impossible buffer sizes.
    // The Arrow C Data Interface contract requires the producer to allocate
    // enough buffer for (offset + length) elements.
    int64_t max_elems = static_cast<int64_t>(buf_size / 8);
    if (max_elems < 1) max_elems = 1;
    if (offset < 0) offset = 0;
    if (offset >= max_elems) offset = 0;
    int64_t remaining = max_elems - offset;
    if (length > remaining) length = remaining;
    if (length <= 0) length = 1;

    // Build a minimal ArrowSchema for float64 ("g")
    ArrowSchema schema{};
    schema.format     = "g";  // float64
    schema.name       = "fuzz_col";
    schema.metadata   = nullptr;
    schema.flags      = 0;
    schema.n_children = 0;
    schema.children   = nullptr;
    schema.dictionary = nullptr;
    schema.release    = release_schema;
    schema.private_data = nullptr;

    // Build ArrowArray with fuzzed fields
    const void* buffers[2] = { nullptr, buf_data };

    ArrowArray array{};
    array.length      = length;
    array.null_count  = null_count;
    array.offset      = offset;
    array.n_buffers   = 2;
    array.n_children  = 0;
    array.buffers     = buffers;
    array.children    = nullptr;
    array.dictionary  = nullptr;
    array.release     = release_array;
    array.private_data = nullptr;

    // Try both import paths — they should reject bad inputs gracefully
    (void)signet::forge::ArrowImporter::import_array(&array, &schema);
    // Reset release for second call (import_array may have consumed it)
    array.release  = release_array;
    schema.release = release_schema;
    (void)signet::forge::ArrowImporter::import_array_copy(&array, &schema);

    // Try other format strings to exercise format dispatch
    const char* formats[] = { "l", "f", "i", "C", "c", "s", "L", "n", "Z" };
    for (const char* fmt : formats) {
        schema.format  = fmt;
        schema.release = release_schema;
        array.release  = release_array;
        (void)signet::forge::ArrowImporter::import_array(&array, &schema);
    }

    return 0;
}
