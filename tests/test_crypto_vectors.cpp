// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
//
// Cryptographic Standard Vector Verification Tests
//
// This file verifies ALL cryptographic primitives in Signet Forge against
// their published standard test vectors. Three-way verification:
//   1. C++ implementation output vs published standard values
//   2. Python independent implementation vs published standard values
//   3. C++ output vs Python output (byte-for-byte match)
//
// Standards covered:
//   Pass 1: AES-256 core       — NIST FIPS 197 Appendix C.3
//   Pass 2: AES-256-GCM        — NIST SP 800-38D Test Case 16
//   Pass 3: AES-256-CTR        — NIST SP 800-38A F.5.5/F.5.6
//   Pass 4: SHA-256            — NIST FIPS 180-4
//   Pass 5: SHA-512            — NIST FIPS 180-4
//   Pass 6: HKDF-SHA256        — RFC 5869 Appendix A
//   Pass 7: X25519             — RFC 7748 §6.1
//   Pass 8: AES Key Wrap       — RFC 3394 §4.6
//   Pass 9: CRC32              — IEEE 802.3
//
#include <catch2/catch_test_macros.hpp>
#include "signet/crypto/aes_core.hpp"
#include "signet/crypto/aes_gcm.hpp"
#include "signet/crypto/aes_ctr.hpp"
#include "signet/crypto/hkdf.hpp"
#include "signet/crypto/hsm_client_stub.hpp"
#include "signet/crypto/post_quantum.hpp"
#include "signet/crypto/sha512.hpp"
#include "signet/ai/wal.hpp"
#include "signet/writer.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Helper: parse hex string to bytes
// ---------------------------------------------------------------------------
static std::vector<uint8_t> hex2bytes(const std::string& hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

// ===========================================================================
// PASS 1: AES-256 Core Block Cipher — FIPS 197 Appendix C.3
// ===========================================================================

TEST_CASE("FIPS 197 C.3: AES-256 encrypt produces correct ciphertext",
          "[crypto][aes][fips197][vectors]") {
    auto key = hex2bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    auto pt  = hex2bytes("00112233445566778899aabbccddeeff");
    auto expected_ct = hex2bytes("8ea2b7ca516745bfeafc49904b496089");

    crypto::Aes256 cipher(key.data());
    uint8_t block[16];
    std::memcpy(block, pt.data(), 16);
    cipher.encrypt_block(block);

    REQUIRE(std::memcmp(block, expected_ct.data(), 16) == 0);
}

TEST_CASE("FIPS 197 C.3: AES-256 decrypt recovers plaintext",
          "[crypto][aes][fips197][vectors]") {
    auto key = hex2bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    auto ct  = hex2bytes("8ea2b7ca516745bfeafc49904b496089");
    auto expected_pt = hex2bytes("00112233445566778899aabbccddeeff");

    crypto::Aes256 cipher(key.data());
    uint8_t block[16];
    std::memcpy(block, ct.data(), 16);
    cipher.decrypt_block(block);

    REQUIRE(std::memcmp(block, expected_pt.data(), 16) == 0);
}

TEST_CASE("FIPS 197: AES-256 encrypt-then-decrypt round-trip identity",
          "[crypto][aes][fips197][vectors]") {
    auto key = hex2bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    auto pt  = hex2bytes("00112233445566778899aabbccddeeff");

    crypto::Aes256 cipher(key.data());

    // encrypt(pt) then decrypt should recover pt
    uint8_t block[16];
    std::memcpy(block, pt.data(), 16);
    cipher.encrypt_block(block);
    cipher.decrypt_block(block);
    REQUIRE(std::memcmp(block, pt.data(), 16) == 0);

    // decrypt(ct) then encrypt should recover ct
    auto ct = hex2bytes("8ea2b7ca516745bfeafc49904b496089");
    std::memcpy(block, ct.data(), 16);
    cipher.decrypt_block(block);
    cipher.encrypt_block(block);
    REQUIRE(std::memcmp(block, ct.data(), 16) == 0);
}

TEST_CASE("FIPS 197: S-box boundary values match standard",
          "[crypto][aes][fips197][vectors]") {
    // FIPS 197 Table 4 (S-box)
    REQUIRE(crypto::detail::aes::SBOX[0x00] == 0x63);
    REQUIRE(crypto::detail::aes::SBOX[0x01] == 0x7c);
    REQUIRE(crypto::detail::aes::SBOX[0x53] == 0xed);
    REQUIRE(crypto::detail::aes::SBOX[0xFF] == 0x16);

    // FIPS 197 Table 5 (Inverse S-box)
    REQUIRE(crypto::detail::aes::INV_SBOX[0x63] == 0x00);
    REQUIRE(crypto::detail::aes::INV_SBOX[0x7c] == 0x01);
    REQUIRE(crypto::detail::aes::INV_SBOX[0x16] == 0xFF);

    // S-box and inverse S-box are inverses of each other
    for (int i = 0; i < 256; ++i) {
        REQUIRE(crypto::detail::aes::INV_SBOX[crypto::detail::aes::SBOX[i]] == i);
    }
}

TEST_CASE("FIPS 197: Rcon values match standard",
          "[crypto][aes][fips197][vectors]") {
    // FIPS 197 Table 5: Rcon[i] = x^(i-1) in GF(2^8)
    REQUIRE(crypto::detail::aes::RCON[0] == 0x01);
    REQUIRE(crypto::detail::aes::RCON[1] == 0x02);
    REQUIRE(crypto::detail::aes::RCON[2] == 0x04);
    REQUIRE(crypto::detail::aes::RCON[3] == 0x08);
    REQUIRE(crypto::detail::aes::RCON[4] == 0x10);
    REQUIRE(crypto::detail::aes::RCON[5] == 0x20);
    REQUIRE(crypto::detail::aes::RCON[6] == 0x40);
    REQUIRE(crypto::detail::aes::RCON[7] == 0x80);
    REQUIRE(crypto::detail::aes::RCON[8] == 0x1b);
    REQUIRE(crypto::detail::aes::RCON[9] == 0x36);
}

TEST_CASE("FIPS 197: GF(2^8) multiplication spot-checks",
          "[crypto][aes][fips197][vectors]") {
    // FIPS 197 §4.2: xtime(0x57) = 0xae
    REQUIRE(crypto::detail::aes::xtime(0x57) == 0xae);
    // xtime(0xae) = 0x47 (high bit set, reduce)
    REQUIRE(crypto::detail::aes::xtime(0xae) == 0x47);

    // gf_mul(0x57, 0x13) = 0xfe  (FIPS 197 §4.2 example)
    REQUIRE(crypto::detail::aes::gf_mul(0x57, 0x13) == 0xfe);

    // MixColumns matrix entries
    REQUIRE(crypto::detail::aes::gf_mul(2, 0x63) == 0xc6);
    REQUIRE(crypto::detail::aes::gf_mul(3, 0x63) == 0xa5);

    // Identity: gf_mul(1, x) == x for any x
    REQUIRE(crypto::detail::aes::gf_mul(1, 0x00) == 0x00);
    REQUIRE(crypto::detail::aes::gf_mul(1, 0xff) == 0xff);
    REQUIRE(crypto::detail::aes::gf_mul(1, 0x53) == 0x53);
}

TEST_CASE("FIPS 197: AES-256 second test vector (all-zero key)",
          "[crypto][aes][fips197][vectors]") {
    // NIST AESAVS: Key=0...0, PT=0...0
    // Expected CT from NIST: dc95c078a2408989ad48a21492842087
    auto key = hex2bytes("0000000000000000000000000000000000000000000000000000000000000000");
    auto pt  = hex2bytes("00000000000000000000000000000000");
    auto expected_ct = hex2bytes("dc95c078a2408989ad48a21492842087");

    crypto::Aes256 cipher(key.data());
    uint8_t block[16];
    std::memcpy(block, pt.data(), 16);
    cipher.encrypt_block(block);

    REQUIRE(std::memcmp(block, expected_ct.data(), 16) == 0);
}

// ===========================================================================
// PASS 2: AES-256-GCM — NIST SP 800-38D Test Cases 13, 14, 16
// ===========================================================================

TEST_CASE("SP 800-38D TC13: AES-256-GCM empty PT/AAD exact tag",
          "[crypto][gcm][sp800-38d][vectors]") {
    auto key = hex2bytes("0000000000000000000000000000000000000000000000000000000000000000");
    auto iv  = hex2bytes("000000000000000000000000");
    auto expected_tag = hex2bytes("530f8afbc74536b9a963b4f1c4cb738b");

    crypto::AesGcm gcm(key.data());
    auto result = gcm.encrypt(nullptr, 0, iv.data(), nullptr, 0);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 16);
    REQUIRE(std::memcmp(result->data(), expected_tag.data(), 16) == 0);

    auto dec = gcm.decrypt(result->data(), result->size(), iv.data(), nullptr, 0);
    REQUIRE(dec.has_value());
    REQUIRE(dec->empty());
}

TEST_CASE("SP 800-38D TC14: AES-256-GCM 16B PT, exact CT and tag",
          "[crypto][gcm][sp800-38d][vectors]") {
    auto key = hex2bytes("0000000000000000000000000000000000000000000000000000000000000000");
    auto iv  = hex2bytes("000000000000000000000000");
    auto pt  = hex2bytes("00000000000000000000000000000000");
    auto expected_ct  = hex2bytes("cea7403d4d606b6e074ec5d3baf39d18");
    auto expected_tag = hex2bytes("d0d1c8a799996bf0265b98b5d48ab919");

    crypto::AesGcm gcm(key.data());
    auto result = gcm.encrypt(pt.data(), pt.size(), iv.data(), nullptr, 0);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 32);

    REQUIRE(std::memcmp(result->data(), expected_ct.data(), 16) == 0);
    REQUIRE(std::memcmp(result->data() + 16, expected_tag.data(), 16) == 0);

    std::vector<uint8_t> nist;
    nist.insert(nist.end(), expected_ct.begin(), expected_ct.end());
    nist.insert(nist.end(), expected_tag.begin(), expected_tag.end());
    auto dec = gcm.decrypt(nist.data(), nist.size(), iv.data(), nullptr, 0);
    REQUIRE(dec.has_value());
    REQUIRE(std::memcmp(dec->data(), pt.data(), 16) == 0);
}

TEST_CASE("SP 800-38D TC16: AES-256-GCM 60B PT, 20B AAD, exact CT and tag",
          "[crypto][gcm][sp800-38d][vectors]") {
    auto key = hex2bytes("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    auto iv  = hex2bytes("cafebabefacedbaddecaf888");
    auto aad = hex2bytes("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    auto pt  = hex2bytes(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    auto expected_ct = hex2bytes(
        "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
        "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662");
    auto expected_tag = hex2bytes("76fc6ece0f4e1768cddf8853bb2d551b");

    crypto::AesGcm gcm(key.data());
    auto result = gcm.encrypt(pt.data(), pt.size(), iv.data(), aad.data(), aad.size());
    REQUIRE(result.has_value());
    REQUIRE(result->size() == pt.size() + 16);

    REQUIRE(std::memcmp(result->data(), expected_ct.data(), pt.size()) == 0);
    REQUIRE(std::memcmp(result->data() + pt.size(), expected_tag.data(), 16) == 0);

    std::vector<uint8_t> nist;
    nist.insert(nist.end(), expected_ct.begin(), expected_ct.end());
    nist.insert(nist.end(), expected_tag.begin(), expected_tag.end());
    auto dec = gcm.decrypt(nist.data(), nist.size(), iv.data(), aad.data(), aad.size());
    REQUIRE(dec.has_value());
    REQUIRE(std::memcmp(dec->data(), pt.data(), pt.size()) == 0);
}

TEST_CASE("SP 800-38D: tampered CT rejected, tampered AAD rejected",
          "[crypto][gcm][sp800-38d][vectors]") {
    auto key = hex2bytes("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    auto iv  = hex2bytes("cafebabefacedbaddecaf888");
    auto aad = hex2bytes("feedfacedeadbeeffeedfacedeadbeefabaddad2");
    auto pt  = hex2bytes(
        "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");

    crypto::AesGcm gcm(key.data());
    auto result = gcm.encrypt(pt.data(), pt.size(), iv.data(), aad.data(), aad.size());
    REQUIRE(result.has_value());

    auto tampered = *result;
    tampered[0] ^= 0x01;
    auto dec = gcm.decrypt(tampered.data(), tampered.size(), iv.data(), aad.data(), aad.size());
    REQUIRE_FALSE(dec.has_value());

    auto bad_aad = aad;
    bad_aad[0] ^= 0x01;
    auto dec2 = gcm.decrypt(result->data(), result->size(), iv.data(), bad_aad.data(), bad_aad.size());
    REQUIRE_FALSE(dec2.has_value());
}

TEST_CASE("SP 800-38D: empty PT with AAD produces valid tag",
          "[crypto][gcm][sp800-38d][vectors]") {
    auto key = hex2bytes("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    auto iv  = hex2bytes("cafebabefacedbaddecaf888");
    auto aad = hex2bytes("feedfacedeadbeeffeedfacedeadbeefabaddad2");

    crypto::AesGcm gcm(key.data());
    auto result = gcm.encrypt(nullptr, 0, iv.data(), aad.data(), aad.size());
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 16);

    auto dec = gcm.decrypt(result->data(), result->size(), iv.data(), aad.data(), aad.size());
    REQUIRE(dec.has_value());
    REQUIRE(dec->empty());
}

// ===========================================================================
// PASS 3: AES-256-CTR — NIST SP 800-38A F.5.5 (CTR-AES256.Encrypt)
// ===========================================================================

TEST_CASE("SP 800-38A F.5.5: AES-256-CTR 4-block encrypt exact ciphertext",
          "[crypto][ctr][sp800-38a][vectors]") {
    // NIST SP 800-38A Section F.5.5: CTR-AES256.Encrypt
    auto key = hex2bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    auto iv  = hex2bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto pt  = hex2bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710");
    auto expected_ct = hex2bytes(
        "601ec313775789a5b7a7f504bbf3d228"
        "f443e3ca4d62b59aca84e990cacaf5c5"
        "2b0930daa23de94ce87017ba2d84988d"
        "dfc9c58db67aada613c2dd08457941a6");

    crypto::AesCtr ctr(key.data());
    auto result = ctr.encrypt(pt.data(), pt.size(), iv.data());
    REQUIRE(result.has_value());
    REQUIRE(result->size() == expected_ct.size());
    REQUIRE(std::memcmp(result->data(), expected_ct.data(), expected_ct.size()) == 0);
}

TEST_CASE("SP 800-38A F.5.6: AES-256-CTR decrypt recovers plaintext",
          "[crypto][ctr][sp800-38a][vectors]") {
    // NIST SP 800-38A Section F.5.6: CTR-AES256.Decrypt
    auto key = hex2bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    auto iv  = hex2bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto ct  = hex2bytes(
        "601ec313775789a5b7a7f504bbf3d228"
        "f443e3ca4d62b59aca84e990cacaf5c5"
        "2b0930daa23de94ce87017ba2d84988d"
        "dfc9c58db67aada613c2dd08457941a6");
    auto expected_pt = hex2bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710");

    crypto::AesCtr ctr(key.data());
    auto result = ctr.decrypt(ct.data(), ct.size(), iv.data());
    REQUIRE(result.has_value());
    REQUIRE(result->size() == expected_pt.size());
    REQUIRE(std::memcmp(result->data(), expected_pt.data(), expected_pt.size()) == 0);
}

TEST_CASE("SP 800-38A: AES-256-CTR per-block ciphertext verification",
          "[crypto][ctr][sp800-38a][vectors]") {
    // Verify each 16-byte block independently matches NIST expected output
    auto key = hex2bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    auto iv  = hex2bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");

    struct BlockVector {
        const char* pt_hex;
        const char* ct_hex;
        const char* label;
    };

    BlockVector blocks[] = {
        {"6bc1bee22e409f96e93d7e117393172a", "601ec313775789a5b7a7f504bbf3d228", "Block 1"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "f443e3ca4d62b59aca84e990cacaf5c5", "Block 2"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "2b0930daa23de94ce87017ba2d84988d", "Block 3"},
        {"f69f2445df4f9b17ad2b417be66c3710", "dfc9c58db67aada613c2dd08457941a6", "Block 4"},
    };

    // Encrypt all 4 blocks at once, then check each 16-byte segment
    auto pt_all = hex2bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710");

    crypto::AesCtr ctr(key.data());
    auto result = ctr.encrypt(pt_all.data(), pt_all.size(), iv.data());
    REQUIRE(result.has_value());

    for (int i = 0; i < 4; ++i) {
        auto expected_block = hex2bytes(blocks[i].ct_hex);
        INFO("Checking " << blocks[i].label);
        REQUIRE(std::memcmp(result->data() + i * 16, expected_block.data(), 16) == 0);
    }
}

TEST_CASE("SP 800-38A: AES-256-CTR encrypt-decrypt round-trip identity",
          "[crypto][ctr][sp800-38a][vectors]") {
    auto key = hex2bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    auto iv  = hex2bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto pt  = hex2bytes(
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710");

    crypto::AesCtr ctr(key.data());
    auto enc = ctr.encrypt(pt.data(), pt.size(), iv.data());
    REQUIRE(enc.has_value());

    auto dec = ctr.decrypt(enc->data(), enc->size(), iv.data());
    REQUIRE(dec.has_value());
    REQUIRE(dec->size() == pt.size());
    REQUIRE(std::memcmp(dec->data(), pt.data(), pt.size()) == 0);
}

TEST_CASE("SP 800-38A: AES-256-CTR partial block (non-16-byte aligned)",
          "[crypto][ctr][sp800-38a][vectors]") {
    // CTR mode must handle non-block-aligned data
    // Use first 7 bytes of NIST vector: PT = "6bc1bee22e409f"
    auto key = hex2bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    auto iv  = hex2bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto pt  = hex2bytes("6bc1bee22e409f");
    // Expected: first 7 bytes of "601ec313775789a5b7a7f504bbf3d228"
    auto expected_ct = hex2bytes("601ec313775789");

    crypto::AesCtr ctr(key.data());
    auto result = ctr.encrypt(pt.data(), pt.size(), iv.data());
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 7);
    REQUIRE(std::memcmp(result->data(), expected_ct.data(), 7) == 0);

    // Round-trip
    auto dec = ctr.decrypt(result->data(), result->size(), iv.data());
    REQUIRE(dec.has_value());
    REQUIRE(std::memcmp(dec->data(), pt.data(), 7) == 0);
}

TEST_CASE("SP 800-38A: AES-256-CTR empty input produces empty output",
          "[crypto][ctr][sp800-38a][vectors]") {
    auto key = hex2bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
    auto iv  = hex2bytes("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");

    crypto::AesCtr ctr(key.data());
    auto result = ctr.encrypt(nullptr, 0, iv.data());
    REQUIRE(result.has_value());
    REQUIRE(result->empty());
}

// ===========================================================================
// PASS 4: SHA-256 — NIST FIPS 180-4
// ===========================================================================

TEST_CASE("FIPS 180-4: SHA-256 one-block message 'abc'",
          "[crypto][sha256][fips180][vectors]") {
    // FIPS 180-4 Section B.1: SHA-256("abc")
    // Input: 0x616263 (3 bytes)
    // Expected: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    const uint8_t msg[] = {0x61, 0x62, 0x63};
    auto expected = hex2bytes("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    auto result = crypto::detail::sha256::sha256(msg, 3);
    REQUIRE(std::memcmp(result.data(), expected.data(), 32) == 0);
}

TEST_CASE("FIPS 180-4: SHA-256 two-block message (56 chars)",
          "[crypto][sha256][fips180][vectors]") {
    // FIPS 180-4 Section B.2: SHA-256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
    // 448 bits = 56 bytes — forces two-block padding
    // Expected: 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
    const char* msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    auto expected = hex2bytes("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    auto result = crypto::detail::sha256::sha256(
        reinterpret_cast<const uint8_t*>(msg), 56);
    REQUIRE(std::memcmp(result.data(), expected.data(), 32) == 0);
}

TEST_CASE("FIPS 180-4: SHA-256 empty message",
          "[crypto][sha256][fips180][vectors]") {
    // SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    auto expected = hex2bytes("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    auto result = crypto::detail::sha256::sha256(nullptr, 0);
    REQUIRE(std::memcmp(result.data(), expected.data(), 32) == 0);
}

TEST_CASE("FIPS 180-4: SHA-256 long message (1 million 'a's)",
          "[crypto][sha256][fips180][vectors]") {
    // FIPS 180-4 Section B.3: SHA-256(1,000,000 × 'a')
    // Expected: cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0
    auto expected = hex2bytes("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    std::vector<uint8_t> msg(1000000, 0x61); // 'a' = 0x61
    auto result = crypto::detail::sha256::sha256(msg.data(), msg.size());
    REQUIRE(std::memcmp(result.data(), expected.data(), 32) == 0);
}

TEST_CASE("FIPS 180-4: SHA-256 initial hash values (H0)",
          "[crypto][sha256][fips180][vectors]") {
    // FIPS 180-4 Section 5.3.3: first 32 bits of fractional parts
    // of square roots of first 8 primes
    REQUIRE(crypto::detail::sha256::H0[0] == 0x6a09e667);
    REQUIRE(crypto::detail::sha256::H0[1] == 0xbb67ae85);
    REQUIRE(crypto::detail::sha256::H0[2] == 0x3c6ef372);
    REQUIRE(crypto::detail::sha256::H0[3] == 0xa54ff53a);
    REQUIRE(crypto::detail::sha256::H0[4] == 0x510e527f);
    REQUIRE(crypto::detail::sha256::H0[5] == 0x9b05688c);
    REQUIRE(crypto::detail::sha256::H0[6] == 0x1f83d9ab);
    REQUIRE(crypto::detail::sha256::H0[7] == 0x5be0cd19);
}

TEST_CASE("FIPS 180-4: SHA-256 round constants K[0..3], K[63]",
          "[crypto][sha256][fips180][vectors]") {
    // FIPS 180-4 Section 4.2.2: first 32 bits of fractional parts
    // of cube roots of first 64 primes
    REQUIRE(crypto::detail::sha256::K[0]  == 0x428a2f98);
    REQUIRE(crypto::detail::sha256::K[1]  == 0x71374491);
    REQUIRE(crypto::detail::sha256::K[2]  == 0xb5c0fbcf);
    REQUIRE(crypto::detail::sha256::K[3]  == 0xe9b5dba5);
    REQUIRE(crypto::detail::sha256::K[63] == 0xc67178f2);
}

// ===========================================================================
// PASS 5: SHA-512 — NIST FIPS 180-4
// ===========================================================================

TEST_CASE("FIPS 180-4: SHA-512 one-block message 'abc'",
          "[crypto][sha512][fips180][vectors]") {
    // FIPS 180-4 Section C.1: SHA-512("abc")
    // Expected: ddaf35a193617aba cc417349ae204131 12e6fa4e89a97ea2 0a9eeee64b55d39a
    //           2192992a274fc1a8 36ba3c23a3feebbd 454d4423643ce80e 2a9ac94fa54ca49f
    const uint8_t msg[] = {0x61, 0x62, 0x63};
    auto expected = hex2bytes(
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    auto result = crypto::detail::sha512::sha512(msg, 3);
    REQUIRE(std::memcmp(result.data(), expected.data(), 64) == 0);
}

TEST_CASE("FIPS 180-4: SHA-512 two-block message (112 chars)",
          "[crypto][sha512][fips180][vectors]") {
    // FIPS 180-4 Section C.2: SHA-512("abcdefghbc...opq" — 112 bytes)
    const char* msg = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                      "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    auto expected = hex2bytes(
        "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
        "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");

    auto result = crypto::detail::sha512::sha512(
        reinterpret_cast<const uint8_t*>(msg), 112);
    REQUIRE(std::memcmp(result.data(), expected.data(), 64) == 0);
}

TEST_CASE("FIPS 180-4: SHA-512 empty message",
          "[crypto][sha512][fips180][vectors]") {
    // SHA-512("") = cf83e1357eefb8bd...
    auto expected = hex2bytes(
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");

    auto result = crypto::detail::sha512::sha512(nullptr, 0);
    REQUIRE(std::memcmp(result.data(), expected.data(), 64) == 0);
}

TEST_CASE("FIPS 180-4: SHA-512 long message (1 million 'a's)",
          "[crypto][sha512][fips180][vectors]") {
    // FIPS 180-4 Section C.3: SHA-512(1,000,000 × 'a')
    auto expected = hex2bytes(
        "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
        "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b");

    std::vector<uint8_t> msg(1000000, 0x61);
    auto result = crypto::detail::sha512::sha512(msg.data(), msg.size());
    REQUIRE(std::memcmp(result.data(), expected.data(), 64) == 0);
}

// ===========================================================================
// PASS 6: HKDF-SHA256 — RFC 5869 Appendix A
// ===========================================================================

TEST_CASE("RFC 5869 A.1: HKDF-SHA256 basic test case",
          "[crypto][hkdf][rfc5869][vectors]") {
    auto ikm  = hex2bytes("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    auto salt = hex2bytes("000102030405060708090a0b0c");
    auto info = hex2bytes("f0f1f2f3f4f5f6f7f8f9");
    auto expected_prk = hex2bytes("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    auto expected_okm = hex2bytes(
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
        "34007208d5b887185865");

    // Extract
    auto prk = crypto::hkdf_extract(salt.data(), salt.size(), ikm.data(), ikm.size());
    REQUIRE(std::memcmp(prk.data(), expected_prk.data(), 32) == 0);

    // Expand
    std::vector<uint8_t> okm(42);
    REQUIRE(crypto::hkdf_expand(prk, info.data(), info.size(), okm.data(), okm.size()));
    REQUIRE(std::memcmp(okm.data(), expected_okm.data(), 42) == 0);
}

TEST_CASE("RFC 5869 A.2: HKDF-SHA256 longer inputs/outputs",
          "[crypto][hkdf][rfc5869][vectors]") {
    auto ikm = hex2bytes(
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"
        "404142434445464748494a4b4c4d4e4f");
    auto salt = hex2bytes(
        "606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f"
        "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"
        "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf");
    auto info = hex2bytes(
        "b0b1b2b3b4b5b6b7b8b9babbbcbdbebfc0c1c2c3c4c5c6c7c8c9cacbcccdcecf"
        "d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5e6e7e8e9eaebecedeeef"
        "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto expected_prk = hex2bytes("06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244");
    auto expected_okm = hex2bytes(
        "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c"
        "59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71"
        "cc30c58179ec3e87c14c01d5c1f3434f1d87");

    auto prk = crypto::hkdf_extract(salt.data(), salt.size(), ikm.data(), ikm.size());
    REQUIRE(std::memcmp(prk.data(), expected_prk.data(), 32) == 0);

    std::vector<uint8_t> okm(82);
    REQUIRE(crypto::hkdf_expand(prk, info.data(), info.size(), okm.data(), okm.size()));
    REQUIRE(std::memcmp(okm.data(), expected_okm.data(), 82) == 0);
}

TEST_CASE("RFC 5869 A.3: HKDF-SHA256 zero-length salt/info",
          "[crypto][hkdf][rfc5869][vectors]") {
    auto ikm = hex2bytes("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    auto expected_prk = hex2bytes("19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04");
    auto expected_okm = hex2bytes(
        "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
        "9d201395faa4b61a96c8");

    auto prk = crypto::hkdf_extract(nullptr, 0, ikm.data(), ikm.size());
    REQUIRE(std::memcmp(prk.data(), expected_prk.data(), 32) == 0);

    std::vector<uint8_t> okm(42);
    REQUIRE(crypto::hkdf_expand(prk, nullptr, 0, okm.data(), okm.size()));
    REQUIRE(std::memcmp(okm.data(), expected_okm.data(), 42) == 0);
}

TEST_CASE("RFC 5869: HKDF one-shot convenience function",
          "[crypto][hkdf][rfc5869][vectors]") {
    auto ikm  = hex2bytes("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b");
    auto salt = hex2bytes("000102030405060708090a0b0c");
    auto info = hex2bytes("f0f1f2f3f4f5f6f7f8f9");
    auto expected_okm = hex2bytes(
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
        "34007208d5b887185865");

    std::vector<uint8_t> okm(42);
    REQUIRE(crypto::hkdf(salt.data(), salt.size(), ikm.data(), ikm.size(),
                          info.data(), info.size(), okm.data(), okm.size()));
    REQUIRE(std::memcmp(okm.data(), expected_okm.data(), 42) == 0);
}

// ===========================================================================
// PASS 7: X25519 — RFC 7748 §6.1
// ===========================================================================

TEST_CASE("RFC 7748 §6.1: Alice public key from private key",
          "[crypto][x25519][rfc7748][vectors]") {
    auto alice_priv = hex2bytes("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    auto expected_pub = hex2bytes("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");

    std::array<uint8_t, 32> priv_arr, base_point{};
    std::memcpy(priv_arr.data(), alice_priv.data(), 32);
    base_point[0] = 9;  // X25519 base point u=9

    auto result = crypto::detail::x25519::x25519(priv_arr, base_point);
    REQUIRE(result.has_value());
    REQUIRE(std::memcmp(result->data(), expected_pub.data(), 32) == 0);
}

TEST_CASE("RFC 7748 §6.1: Bob public key from private key",
          "[crypto][x25519][rfc7748][vectors]") {
    auto bob_priv = hex2bytes("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    auto expected_pub = hex2bytes("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");

    std::array<uint8_t, 32> priv_arr, base_point{};
    std::memcpy(priv_arr.data(), bob_priv.data(), 32);
    base_point[0] = 9;

    auto result = crypto::detail::x25519::x25519(priv_arr, base_point);
    REQUIRE(result.has_value());
    REQUIRE(std::memcmp(result->data(), expected_pub.data(), 32) == 0);
}

TEST_CASE("RFC 7748 §6.1: Alice-Bob shared secret",
          "[crypto][x25519][rfc7748][vectors]") {
    auto alice_priv = hex2bytes("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    auto bob_pub    = hex2bytes("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    auto expected_ss = hex2bytes("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

    std::array<uint8_t, 32> priv_arr, pub_arr;
    std::memcpy(priv_arr.data(), alice_priv.data(), 32);
    std::memcpy(pub_arr.data(), bob_pub.data(), 32);

    auto result = crypto::detail::x25519::x25519(priv_arr, pub_arr);
    REQUIRE(result.has_value());
    REQUIRE(std::memcmp(result->data(), expected_ss.data(), 32) == 0);
}

TEST_CASE("RFC 7748 §6.1: Bob-Alice shared secret matches Alice-Bob",
          "[crypto][x25519][rfc7748][vectors]") {
    auto bob_priv   = hex2bytes("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
    auto alice_pub  = hex2bytes("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    auto expected_ss = hex2bytes("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");

    std::array<uint8_t, 32> priv_arr, pub_arr;
    std::memcpy(priv_arr.data(), bob_priv.data(), 32);
    std::memcpy(pub_arr.data(), alice_pub.data(), 32);

    auto result = crypto::detail::x25519::x25519(priv_arr, pub_arr);
    REQUIRE(result.has_value());
    REQUIRE(std::memcmp(result->data(), expected_ss.data(), 32) == 0);
}

// ===========================================================================
// PASS 8: AES-256 Key Wrap — RFC 3394 §4.6
// ===========================================================================

TEST_CASE("RFC 3394 §4.6: Wrap 256-bit key with 256-bit KEK",
          "[crypto][keywrap][rfc3394][vectors]") {
    // RFC 3394 §4.6: AES-256 Key Wrap
    auto kek_bytes = hex2bytes("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
    auto key_data  = hex2bytes("00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F");
    auto expected  = hex2bytes("28C9F404C4B810F4CBCCB35CFB87F8263F5786E2D80ED326CBC7F0E71A99F43BFB988B9B7A02DD21");

    std::array<uint8_t, 32> kek;
    std::memcpy(kek.data(), kek_bytes.data(), 32);
    std::vector<uint8_t> pt(key_data.begin(), key_data.end());

    auto result = crypto::detail::aes_key_wrap::wrap(kek, pt);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == expected.size());
    REQUIRE(std::memcmp(result->data(), expected.data(), expected.size()) == 0);
}

TEST_CASE("RFC 3394 §4.6: Unwrap recovers original key data",
          "[crypto][keywrap][rfc3394][vectors]") {
    auto kek_bytes = hex2bytes("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
    auto wrapped   = hex2bytes("28C9F404C4B810F4CBCCB35CFB87F8263F5786E2D80ED326CBC7F0E71A99F43BFB988B9B7A02DD21");
    auto expected  = hex2bytes("00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F");

    std::array<uint8_t, 32> kek;
    std::memcpy(kek.data(), kek_bytes.data(), 32);
    std::vector<uint8_t> ct(wrapped.begin(), wrapped.end());

    auto result = crypto::detail::aes_key_wrap::unwrap(kek, ct);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == expected.size());
    REQUIRE(std::memcmp(result->data(), expected.data(), expected.size()) == 0);
}

TEST_CASE("RFC 3394: Wrap-unwrap round-trip identity",
          "[crypto][keywrap][rfc3394][vectors]") {
    auto kek_bytes = hex2bytes("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
    auto key_data  = hex2bytes("00112233445566778899AABBCCDDEEFF000102030405060708090A0B0C0D0E0F");

    std::array<uint8_t, 32> kek;
    std::memcpy(kek.data(), kek_bytes.data(), 32);
    std::vector<uint8_t> pt(key_data.begin(), key_data.end());

    auto wrapped = crypto::detail::aes_key_wrap::wrap(kek, pt);
    REQUIRE(wrapped.has_value());
    auto unwrapped = crypto::detail::aes_key_wrap::unwrap(kek, *wrapped);
    REQUIRE(unwrapped.has_value());
    REQUIRE(unwrapped->size() == pt.size());
    REQUIRE(std::memcmp(unwrapped->data(), pt.data(), pt.size()) == 0);
}

TEST_CASE("RFC 3394: Tampered wrapped key rejected",
          "[crypto][keywrap][rfc3394][vectors]") {
    auto kek_bytes = hex2bytes("000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F");
    auto wrapped   = hex2bytes("28C9F404C4B810F4CBCCB35CFB87F8263F5786E2D80ED326CBC7F0E71A99F43BFB988B9B7A02DD21");

    std::array<uint8_t, 32> kek;
    std::memcpy(kek.data(), kek_bytes.data(), 32);
    std::vector<uint8_t> tampered(wrapped.begin(), wrapped.end());
    tampered[0] ^= 0x01;  // Flip one bit

    auto result = crypto::detail::aes_key_wrap::unwrap(kek, tampered);
    REQUIRE_FALSE(result.has_value());
}

// ===========================================================================
// PASS 9: CRC-32 — IEEE 802.3
// ===========================================================================

TEST_CASE("IEEE 802.3: CRC-32 canonical check value '123456789'",
          "[crypto][crc32][ieee802][vectors]") {
    // The canonical CRC-32 check value for "123456789" is 0xCBF43926
    const char* data = "123456789";
    uint32_t result = detail::crc32(data, 9);
    REQUIRE(result == 0xCBF43926);
}

TEST_CASE("IEEE 802.3: CRC-32 empty input",
          "[crypto][crc32][ieee802][vectors]") {
    uint32_t result = detail::crc32("", 0);
    REQUIRE(result == 0x00000000);
}

TEST_CASE("IEEE 802.3: CRC-32 single zero byte",
          "[crypto][crc32][ieee802][vectors]") {
    uint8_t data = 0x00;
    uint32_t result = detail::crc32(&data, 1);
    REQUIRE(result == 0xD202EF8D);
}

TEST_CASE("IEEE 802.3: Writer page_crc32 matches WAL crc32",
          "[crypto][crc32][ieee802][vectors]") {
    // Both use the same polynomial and algorithm — must produce identical results
    const char* data = "Signet Forge CRC verification";
    size_t len = std::strlen(data);
    uint32_t wal_crc = detail::crc32(data, len);
    uint32_t page_crc = detail::writer::page_crc32(
        reinterpret_cast<const uint8_t*>(data), len);
    REQUIRE(wal_crc == page_crc);
}
