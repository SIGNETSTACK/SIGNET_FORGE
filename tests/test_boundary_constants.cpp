// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji. All Rights Reserved.
//
// tests/test_boundary_constants.cpp
//
// Boundary Verification Protocol (BVP) v1.0 — Signet Forge
// Every constant with a mathematical identity or validity domain
// is tested at its boundary inputs. See:
//   BOUNDARY_VERIFICATION_PROTOCOL.md

#include <catch2/catch_test_macros.hpp>
#include "signet/crypto/sha256.hpp"
#include "signet/crypto/aes_core.hpp"
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
#include "signet/crypto/pme.hpp"
#endif

#include <cstdint>
#include <array>
#include <algorithm>

using namespace signet::forge::crypto;
namespace sha = signet::forge::crypto::detail::sha256;
namespace aes = signet::forge::crypto::detail::aes;
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
namespace pme = signet::forge::crypto::detail::pme;
#endif

// ═══════════════════════════════════════════════════════════════════════
// §BV-01: SHA-256 Initial Hash Values (FIPS 180-4 §5.3.3)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-01 Forge SHA-256 H0[0..7] match FIPS 180-4", "[boundary][constants]") {
    REQUIRE(sha::H0[0] == 0x6a09e667u);
    REQUIRE(sha::H0[7] == 0x5be0cd19u);
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-02: SHA-256 Round Constants K[0..63] (FIPS 180-4 §4.2.2)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-02 Forge SHA-256 K[0] and K[63] match FIPS 180-4", "[boundary][constants]") {
    REQUIRE(sha::K[0] == 0x428a2f98u);
    REQUIRE(sha::K[63] == 0xc67178f2u);
}

TEST_CASE("BV-02b Forge SHA-256 K table has no duplicates", "[boundary][constants]") {
    for (int i = 0; i < 64; ++i) {
        for (int j = i + 1; j < 64; ++j) {
            REQUIRE(sha::K[i] != sha::K[j]);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-03: AES S-Box (FIPS 197 §5.1.1)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-03 AES S-Box is a permutation of 0..255", "[boundary][constants]") {
    // Every byte value must appear exactly once
    std::array<int, 256> count{};
    for (int i = 0; i < 256; ++i) {
        count[aes::SBOX[i]]++;
    }
    for (int i = 0; i < 256; ++i) {
        REQUIRE(count[i] == 1);
    }
}

TEST_CASE("BV-03b AES S-Box known values match FIPS 197", "[boundary][constants]") {
    // FIPS 197 Table 4: S-Box reference values
    REQUIRE(aes::SBOX[0x00] == 0x63);
    REQUIRE(aes::SBOX[0x01] == 0x7c);
    REQUIRE(aes::SBOX[0xFF] == 0x16);
    REQUIRE(aes::SBOX[0x53] == 0xed);  // spot-check mid-table
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-04: AES Inverse S-Box (FIPS 197 §5.3.2)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-04 AES INV_SBOX is the inverse of SBOX", "[boundary][constants]") {
    // For every x in 0..255: INV_SBOX[SBOX[x]] == x
    for (int x = 0; x < 256; ++x) {
        REQUIRE(aes::INV_SBOX[aes::SBOX[x]] == x);
    }
}

TEST_CASE("BV-04b AES INV_SBOX is also a permutation", "[boundary][constants]") {
    std::array<int, 256> count{};
    for (int i = 0; i < 256; ++i) {
        count[aes::INV_SBOX[i]]++;
    }
    for (int i = 0; i < 256; ++i) {
        REQUIRE(count[i] == 1);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-05: AES RCON (FIPS 197 §5.2)
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-05 AES RCON matches FIPS 197 key schedule", "[boundary][constants]") {
    // RCON[i] = x^i in GF(2^8) with reduction polynomial x^8+x^4+x^3+x+1
    // RCON[0] = 0x01 (x^0)
    // RCON[1] = 0x02 (x^1)
    // RCON[9] = 0x36 (x^9)
    REQUIRE(aes::RCON[0] == 0x01);
    REQUIRE(aes::RCON[1] == 0x02);
    REQUIRE(aes::RCON[2] == 0x04);
    REQUIRE(aes::RCON[3] == 0x08);
    REQUIRE(aes::RCON[4] == 0x10);
    REQUIRE(aes::RCON[5] == 0x20);
    REQUIRE(aes::RCON[6] == 0x40);
    REQUIRE(aes::RCON[7] == 0x80);
    REQUIRE(aes::RCON[8] == 0x1b);  // x^8 mod poly = x^4+x^3+x+1 = 0x1b
    REQUIRE(aes::RCON[9] == 0x36);  // 2 * 0x1b = 0x36
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-06: AES-256 Dimension Constants
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-06 AES-256 dimensions match FIPS 197", "[boundary][constants]") {
    REQUIRE(Aes256::KEY_SIZE == 32);
    REQUIRE(Aes256::BLOCK_SIZE == 16);
    REQUIRE(Aes256::NUM_ROUNDS == 14);
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-07: GF(2^8) xtime boundary
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-07 GF(2^8) xtime at boundary values", "[boundary][constants]") {
    // xtime(a) = a << 1, XOR with 0x1b if high bit set
    REQUIRE(aes::xtime(0x00) == 0x00);
    REQUIRE(aes::xtime(0x01) == 0x02);
    REQUIRE(aes::xtime(0x7F) == 0xFE);  // no reduction (high bit 0)
    REQUIRE(aes::xtime(0x80) == 0x1B);  // reduction: 0x100 XOR 0x11B = 0x1B
    REQUIRE(aes::xtime(0xFF) == 0xE5);  // 0x1FE XOR 0x11B = 0xE5
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-08: GF(2^8) multiplication boundary
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-08 GF(2^8) multiplication identities", "[boundary][constants]") {
    // Multiplicative identity: gf_mul(a, 1) == a for all a
    for (int a = 0; a < 256; ++a) {
        REQUIRE(aes::gf_mul(static_cast<uint8_t>(a), 1) == a);
    }
    // Multiplicative zero: gf_mul(a, 0) == 0 for all a
    for (int a = 0; a < 256; ++a) {
        REQUIRE(aes::gf_mul(static_cast<uint8_t>(a), 0) == 0);
    }
    // Known MixColumns constants: 2, 3 are used in AES
    REQUIRE(aes::gf_mul(0x57, 0x02) == 0xae);  // FIPS 197 §4.2.1 example
    REQUIRE(aes::gf_mul(0x57, 0x13) == 0xfe);  // FIPS 197 §4.2.1 example
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-09: PME Constants (commercial tier only)
// ═══════════════════════════════════════════════════════════════════════

#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
TEST_CASE("BV-09 PME key and module type constants", "[boundary][constants]") {
    REQUIRE(PME_REQUIRED_KEY_SIZE == 32);      // AES-256
    REQUIRE(signet::forge::crypto::PME_AES128_KEY_SIZE == 16);        // AES-128 (for AAD)
    REQUIRE(pme::MODULE_FOOTER == 0);
    REQUIRE(pme::MODULE_COLUMN_META == 1);
    REQUIRE(pme::MODULE_DATA_PAGE == 2);
    REQUIRE(pme::MODULE_DICT_PAGE == 3);
    // Module types must be distinct
    uint8_t mods[] = {pme::MODULE_FOOTER, pme::MODULE_COLUMN_META, pme::MODULE_DATA_PAGE, pme::MODULE_DICT_PAGE};
    for (int i = 0; i < 4; ++i)
        for (int j = i+1; j < 4; ++j)
            REQUIRE(mods[i] != mods[j]);
}
#endif

// ═══════════════════════════════════════════════════════════════════════
// §BV-10: SHA-256 rotr at boundary rotations
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-10 SHA-256 rotr boundaries", "[boundary][constants]") {
    REQUIRE(sha::rotr(0xDEADBEEFu, 0) == 0xDEADBEEFu);
    REQUIRE(sha::rotr(1u, 31) == 2u);
    REQUIRE(sha::rotr(0xFFFFFFFFu, 7) == 0xFFFFFFFFu);
    REQUIRE(sha::rotr(0x80000000u, 1) == 0x40000000u);
}

// ═══════════════════════════════════════════════════════════════════════
// §BV-11: SHA-256 KAT — validates ALL constants together
// ═══════════════════════════════════════════════════════════════════════

TEST_CASE("BV-11 SHA-256('abc') validates all K/H/rotation constants", "[boundary][constants]") {
    // If ANY constant is wrong, this hash will not match.
    // Expected: ba7816bf 8f01cfea 414140de 5dae2223
    //           b00361a3 96177a9c b410ff61 f20015ad
    uint8_t msg[] = {'a', 'b', 'c'};
    auto hash = sha::sha256(msg, 3);
    REQUIRE(hash[0] == 0xba);
    REQUIRE(hash[1] == 0x78);
    REQUIRE(hash[2] == 0x16);
    REQUIRE(hash[3] == 0xbf);
    REQUIRE(hash[31] == 0xad);
}
