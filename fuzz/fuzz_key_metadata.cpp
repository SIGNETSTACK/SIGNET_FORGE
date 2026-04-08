// SPDX-License-Identifier: AGPL-3.0-or-later
// Fuzz harness for key metadata TLV parsing — exercises deserialization of
// encrypted key material from untrusted Parquet file metadata.
// Gap T-3: Crypto fuzz harnesses for key metadata TLV parsing.

#include "signet/crypto/key_metadata.hpp"

#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    using namespace signet::forge::crypto;

    // Exercise EncryptionKeyMetadata TLV deserialization
    {
        auto result = EncryptionKeyMetadata::deserialize(data, size);
        if (result.has_value()) {
            // Round-trip: serialize and re-deserialize
            auto serialized = result->serialize();
            auto result2 = EncryptionKeyMetadata::deserialize(
                serialized.data(), serialized.size());
            if (!result2.has_value()) __builtin_trap();
        }
    }

    // Exercise FileEncryptionProperties TLV deserialization
    {
        auto result = FileEncryptionProperties::deserialize(data, size);
        if (result.has_value()) {
            auto serialized = result->serialize();
            auto result2 = FileEncryptionProperties::deserialize(
                serialized.data(), serialized.size());
            if (!result2.has_value()) __builtin_trap();
        }
    }

    return 0;
}
