// SPDX-License-Identifier: Apache-2.0
// Fuzz harness for AES-GCM and AES-CTR crypto primitives used by PME.
// Gap T-3: Crypto fuzz harnesses for PME spec compliance.
//
// Since FileEncryptor requires commercial licensing, we fuzz the underlying
// AES-GCM and AES-CTR primitives directly, which is where memory safety
// issues are most likely.

#include "signet/crypto/aes_gcm.hpp"
#include "signet/crypto/aes_ctr.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Minimum: 32 (key) + 16 (CTR IV) + 12 (GCM IV) + 1 (payload)
    if (size < 61) return 0;

    const uint8_t* key     = data;
    const uint8_t* ctr_iv  = data + 32;       // 16 bytes for AES-CTR
    const uint8_t* gcm_iv  = data + 48;       // 12 bytes for AES-GCM
    const uint8_t* payload = data + 60;
    size_t payload_len     = size - 60;

    // --- AES-CTR (column page encryption) ---
    // Returns expected<std::vector<uint8_t>>
    {
        signet::forge::crypto::AesCtr ctr(key);
        auto enc = ctr.encrypt(payload, payload_len, ctr_iv);
        if (!enc.has_value()) __builtin_trap();
        if (enc->size() != payload_len) __builtin_trap();
        auto dec = ctr.decrypt(enc->data(), enc->size(), ctr_iv);
        if (!dec.has_value()) __builtin_trap();
        if (dec->size() != payload_len) __builtin_trap();
        if (payload_len > 0 && std::memcmp(dec->data(), payload, payload_len) != 0)
            __builtin_trap();
    }

    // --- AES-GCM (footer encryption) ---
    // Returns expected<std::vector<uint8_t>>
    {
        signet::forge::crypto::AesGcm gcm(key);
        auto enc = gcm.encrypt(payload, payload_len, gcm_iv, nullptr, 0);
        if (enc.has_value()) {
            auto dec = gcm.decrypt(enc->data(), enc->size(), gcm_iv, nullptr, 0);
            if (!dec.has_value()) __builtin_trap();
            if (dec->size() != payload_len) __builtin_trap();
            if (payload_len > 0 && std::memcmp(dec->data(), payload, payload_len) != 0)
                __builtin_trap();
        }
    }

    return 0;
}
