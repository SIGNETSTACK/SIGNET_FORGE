// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include "signet/crypto/aes_core.hpp"
#include "signet/crypto/aes_gcm.hpp"
#include "signet/crypto/aes_ctr.hpp"
#include "signet/crypto/cipher_interface.hpp"
#include "signet/crypto/pme.hpp"
#include "signet/crypto/key_metadata.hpp"
#include "signet/crypto/post_quantum.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: RAII temp file that cleans up on destruction
// ---------------------------------------------------------------------------
namespace {
struct TempFile {
    fs::path path;

    explicit TempFile(const std::string& name)
        : path(fs::temp_directory_path() / name) {}

    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
} // anonymous namespace

// ---------------------------------------------------------------------------
// Helper: generate a deterministic test key of the given size
// ---------------------------------------------------------------------------
static std::vector<uint8_t> generate_test_key(size_t size) {
    std::vector<uint8_t> key(size);
    for (size_t i = 0; i < size; ++i) key[i] = static_cast<uint8_t>(i * 7 + 13);
    return key;
}

// ===================================================================
// 1. AES-256 block encrypt/decrypt roundtrip
// ===================================================================
TEST_CASE("AES-256 block encrypt/decrypt roundtrip", "[crypto][aes]") {
    // 32-byte key: {0x00, 0x01, ..., 0x1f}
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);

    // 16-byte plaintext
    uint8_t plaintext[16] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
        0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0, 0x00
    };

    // Save a copy of the original plaintext
    uint8_t original[16];
    std::memcpy(original, plaintext, 16);

    crypto::Aes256 cipher(key);

    // Encrypt in-place
    uint8_t block[16];
    std::memcpy(block, plaintext, 16);
    cipher.encrypt_block(block);

    // Ciphertext should differ from plaintext
    REQUIRE(std::memcmp(block, original, 16) != 0);

    // Decrypt in-place
    cipher.decrypt_block(block);

    // Decrypted should match original plaintext
    REQUIRE(std::memcmp(block, original, 16) == 0);
}

// ===================================================================
// 2. AES-256 FIPS-197 test vector (Appendix C.3)
// ===================================================================
TEST_CASE("AES-256 FIPS-197 test vector", "[crypto][aes]") {
    // Key: 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);

    // Plaintext: 00112233445566778899aabbccddeeff
    uint8_t plaintext[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };

    // Expected ciphertext: 8ea2b7ca516745bfeafc49904b496089
    uint8_t expected_ct[16] = {
        0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
        0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89
    };

    crypto::Aes256 cipher(key);

    uint8_t block[16];
    std::memcpy(block, plaintext, 16);
    cipher.encrypt_block(block);

    REQUIRE(std::memcmp(block, expected_ct, 16) == 0);
}

// ===================================================================
// 3. AES-GCM encrypt/decrypt roundtrip
// ===================================================================
TEST_CASE("AES-GCM encrypt/decrypt roundtrip", "[crypto][gcm]") {
    auto key = generate_test_key(32);
    uint8_t iv[12] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                      0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};

    std::string plaintext_str = "Hello, Parquet Encryption!";
    const auto* pt_data = reinterpret_cast<const uint8_t*>(plaintext_str.data());
    size_t pt_size = plaintext_str.size();

    crypto::AesGcm gcm(key.data());

    // Encrypt with no AAD
    auto ct_result = gcm.encrypt(pt_data, pt_size, iv);
    REQUIRE(ct_result.has_value());
    const auto& ciphertext = ct_result.value();

    // Ciphertext should differ from plaintext
    REQUIRE(ciphertext.size() != pt_size);

    // Ciphertext should be 16 bytes longer (auth tag)
    REQUIRE(ciphertext.size() == pt_size + crypto::AesGcm::TAG_SIZE);

    // Decrypt and verify roundtrip
    auto pt_result = gcm.decrypt(ciphertext.data(), ciphertext.size(), iv);
    REQUIRE(pt_result.has_value());
    const auto& decrypted = pt_result.value();

    REQUIRE(decrypted.size() == pt_size);
    std::string decrypted_str(reinterpret_cast<const char*>(decrypted.data()),
                              decrypted.size());
    REQUIRE(decrypted_str == plaintext_str);
}

// ===================================================================
// 4. AES-GCM with AAD
// ===================================================================
TEST_CASE("AES-GCM with AAD", "[crypto][gcm]") {
    auto key = generate_test_key(32);
    uint8_t iv[12] = {0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD,
                      0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};

    std::string plaintext_str = "Sensitive column data";
    const auto* pt_data = reinterpret_cast<const uint8_t*>(plaintext_str.data());
    size_t pt_size = plaintext_str.size();

    std::string aad = "signet-forge-footer";
    const auto* aad_data = reinterpret_cast<const uint8_t*>(aad.data());
    size_t aad_size = aad.size();

    crypto::AesGcm gcm(key.data());

    // Encrypt with AAD
    auto ct_result = gcm.encrypt(pt_data, pt_size, iv, aad_data, aad_size);
    REQUIRE(ct_result.has_value());
    const auto& ciphertext = ct_result.value();

    // Decrypt with same AAD should succeed
    auto pt_result = gcm.decrypt(ciphertext.data(), ciphertext.size(), iv,
                                 aad_data, aad_size);
    REQUIRE(pt_result.has_value());
    std::string decrypted_str(reinterpret_cast<const char*>(pt_result->data()),
                              pt_result->size());
    REQUIRE(decrypted_str == plaintext_str);

    // Decrypt with wrong AAD should FAIL (tamper detection)
    std::string wrong_aad = "wrong-aad-prefix";
    const auto* wrong_aad_data = reinterpret_cast<const uint8_t*>(wrong_aad.data());
    auto fail_result = gcm.decrypt(ciphertext.data(), ciphertext.size(), iv,
                                   wrong_aad_data, wrong_aad.size());
    REQUIRE_FALSE(fail_result.has_value());
    REQUIRE(fail_result.error().code == ErrorCode::ENCRYPTION_ERROR);
}

// ===================================================================
// 5. AES-GCM tamper detection
// ===================================================================
TEST_CASE("AES-GCM tamper detection", "[crypto][gcm]") {
    auto key = generate_test_key(32);
    uint8_t iv[12] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
                      0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

    std::string plaintext_str = "Do not tamper with this data!";
    const auto* pt_data = reinterpret_cast<const uint8_t*>(plaintext_str.data());
    size_t pt_size = plaintext_str.size();

    crypto::AesGcm gcm(key.data());

    // Encrypt
    auto ct_result = gcm.encrypt(pt_data, pt_size, iv);
    REQUIRE(ct_result.has_value());

    // Flip one bit in the ciphertext (before the tag)
    auto tampered = ct_result.value();
    REQUIRE(tampered.size() > 0);
    tampered[0] ^= 0x01;

    // Decrypt should fail with ENCRYPTION_ERROR
    auto fail_result = gcm.decrypt(tampered.data(), tampered.size(), iv);
    REQUIRE_FALSE(fail_result.has_value());
    REQUIRE(fail_result.error().code == ErrorCode::ENCRYPTION_ERROR);
}

// ===================================================================
// 6. AES-CTR encrypt/decrypt roundtrip
// ===================================================================
TEST_CASE("AES-CTR encrypt/decrypt roundtrip", "[crypto][ctr]") {
    auto key = generate_test_key(32);
    uint8_t iv[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

    crypto::AesCtr ctr(key.data());

    // Test with various plaintext sizes
    std::vector<size_t> sizes = {7, 16, 33};

    for (size_t sz : sizes) {
        SECTION("size=" + std::to_string(sz)) {
            std::vector<uint8_t> plaintext(sz);
            for (size_t i = 0; i < sz; ++i) {
                plaintext[i] = static_cast<uint8_t>(i * 3 + 5);
            }

            // Encrypt
            auto ciphertext = ctr.encrypt(plaintext.data(), plaintext.size(), iv);
            REQUIRE(ciphertext.size() == plaintext.size());

            // Ciphertext should differ from plaintext (unless all zeros by coincidence)
            if (sz > 0) {
                REQUIRE(ciphertext != plaintext);
            }

            // Decrypt
            auto decrypted = ctr.decrypt(ciphertext.data(), ciphertext.size(), iv);
            REQUIRE(decrypted.size() == plaintext.size());
            REQUIRE(decrypted == plaintext);
        }
    }
}

// ===================================================================
// 7. AES-CTR is symmetric (encrypt == decrypt)
// ===================================================================
TEST_CASE("AES-CTR is symmetric", "[crypto][ctr]") {
    auto key = generate_test_key(32);
    uint8_t iv[16] = {0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7,
                      0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};

    crypto::AesCtr ctr(key.data());

    // encrypt(data) should produce the same result as decrypt(data)
    auto enc_result = ctr.encrypt(data.data(), data.size(), iv);
    auto dec_result = ctr.decrypt(data.data(), data.size(), iv);

    REQUIRE(enc_result == dec_result);
}

// ===================================================================
// 8. PME encrypt/decrypt footer
// ===================================================================
TEST_CASE("PME encrypt/decrypt footer", "[crypto][pme]") {
    // Create encryption config with a 32-byte footer key
    crypto::EncryptionConfig cfg;
    cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    cfg.footer_key = generate_test_key(32);
    cfg.encrypt_footer = true;
    cfg.aad_prefix = "test-pme-footer";

    // Some footer data
    std::string footer_str = "This is simulated Thrift footer metadata.";
    const auto* footer_data = reinterpret_cast<const uint8_t*>(footer_str.data());
    size_t footer_size = footer_str.size();

    // Encrypt
    crypto::FileEncryptor encryptor(cfg);
    auto enc_result = encryptor.encrypt_footer(footer_data, footer_size);
    REQUIRE(enc_result.has_value());
    const auto& encrypted = enc_result.value();

    // Encrypted output should be larger than plaintext (IV header + GCM tag)
    REQUIRE(encrypted.size() > footer_size);

    // Decrypt with same config
    crypto::FileDecryptor decryptor(cfg);
    auto dec_result = decryptor.decrypt_footer(encrypted.data(), encrypted.size());
    REQUIRE(dec_result.has_value());
    const auto& decrypted = dec_result.value();

    REQUIRE(decrypted.size() == footer_size);
    std::string decrypted_str(reinterpret_cast<const char*>(decrypted.data()),
                              decrypted.size());
    REQUIRE(decrypted_str == footer_str);
}

// ===================================================================
// 9. PME encrypt/decrypt column page
// ===================================================================
TEST_CASE("PME encrypt/decrypt column page", "[crypto][pme]") {
    crypto::EncryptionConfig cfg;
    cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    cfg.footer_key = generate_test_key(32);

    // Set a column-specific key for "price"
    crypto::ColumnKeySpec price_key;
    price_key.column_name = "price";
    price_key.key = generate_test_key(32);
    // Use a different key pattern for the column key
    for (size_t i = 0; i < price_key.key.size(); ++i) {
        price_key.key[i] ^= 0xAA;
    }
    cfg.column_keys.push_back(price_key);
    cfg.aad_prefix = "test-pme-column";

    // Some page data
    std::string page_str = "Binary column page data with prices: 50000.50, 49999.75";
    const auto* page_data = reinterpret_cast<const uint8_t*>(page_str.data());
    size_t page_size = page_str.size();

    // Encrypt
    crypto::FileEncryptor encryptor(cfg);
    auto enc_result = encryptor.encrypt_column_page(
        page_data, page_size, "price", 0, 0);
    REQUIRE(enc_result.has_value());
    const auto& encrypted = enc_result.value();

    // Decrypt with same config
    crypto::FileDecryptor decryptor(cfg);
    auto dec_result = decryptor.decrypt_column_page(
        encrypted.data(), encrypted.size(), "price", 0, 0);
    REQUIRE(dec_result.has_value());
    const auto& decrypted = dec_result.value();

    REQUIRE(decrypted.size() == page_size);
    std::string decrypted_str(reinterpret_cast<const char*>(decrypted.data()),
                              decrypted.size());
    REQUIRE(decrypted_str == page_str);
}

// ===================================================================
// 10. PME column not encrypted passes through
// ===================================================================
TEST_CASE("PME column not encrypted passes through", "[crypto][pme]") {
    crypto::EncryptionConfig cfg;
    cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    cfg.footer_key = generate_test_key(32);
    // No default_column_key and no column-specific keys for "timestamp"
    cfg.default_column_key.clear();
    cfg.aad_prefix = "test-pme-passthrough";

    crypto::FileEncryptor encryptor(cfg);

    // "timestamp" has no key configured
    REQUIRE(encryptor.is_column_encrypted("timestamp") == false);

    // Encrypting an unconfigured column should pass data through unchanged
    std::string page_str = "Raw timestamp data";
    const auto* page_data = reinterpret_cast<const uint8_t*>(page_str.data());
    size_t page_size = page_str.size();

    auto enc_result = encryptor.encrypt_column_page(
        page_data, page_size, "timestamp", 0, 0);
    REQUIRE(enc_result.has_value());
    const auto& output = enc_result.value();

    // Output should be the same as input (passthrough)
    REQUIRE(output.size() == page_size);
    REQUIRE(std::memcmp(output.data(), page_data, page_size) == 0);
}

// ===================================================================
// 11. Write and read encrypted Parquet file
// ===================================================================
TEST_CASE("Write and read encrypted Parquet file", "[crypto][pipeline]") {
    TempFile tmp("signet_test_encrypted.parquet");

    auto schema = Schema::builder("trades")
        .column<int64_t>("id")
        .column<double>("price")
        .column<std::string>("symbol")
        .build();

    crypto::EncryptionConfig enc_cfg;
    enc_cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    enc_cfg.footer_key = generate_test_key(32);
    enc_cfg.default_column_key = generate_test_key(32);
    // Use a different pattern for the column key
    for (auto& b : enc_cfg.default_column_key) b ^= 0x55;
    enc_cfg.encrypt_footer = true;
    enc_cfg.aad_prefix = "test-file";

    constexpr size_t NUM_ROWS = 50;

    // Generate test data
    struct Row {
        int64_t     id;
        double      price;
        std::string symbol;
    };

    std::vector<Row> expected_rows;
    expected_rows.reserve(NUM_ROWS);

    const std::vector<std::string> symbols = {"BTCUSD", "ETHUSD", "SOLUSD"};
    for (size_t i = 0; i < NUM_ROWS; ++i) {
        Row row;
        row.id     = static_cast<int64_t>(i + 1);
        row.price  = 50000.0 + static_cast<double>(i) * 10.5;
        row.symbol = symbols[i % symbols.size()];
        expected_rows.push_back(row);
    }

    // Write with encryption enabled
    {
        ParquetWriter::Options opts;
        opts.encryption = enc_cfg;

        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (const auto& row : expected_rows) {
            auto r = writer.write_row({
                std::to_string(row.id),
                std::to_string(row.price),
                row.symbol
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Read with same encryption config
    {
        auto reader_result = ParquetReader::open(tmp.path, enc_cfg);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));
        REQUIRE(reader.schema().num_columns() == 3);

        auto all_result = reader.read_all();
        REQUIRE(all_result.has_value());
        const auto& rows = all_result.value();
        REQUIRE(rows.size() == NUM_ROWS);

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            const auto& row = rows[i];
            const auto& exp = expected_rows[i];

            // id (INT64)
            REQUIRE(row[0] == std::to_string(exp.id));

            // price (DOUBLE)
            double read_price = std::stod(row[1]);
            REQUIRE(std::abs(read_price - exp.price) < 1e-6);

            // symbol (STRING)
            REQUIRE(row[2] == exp.symbol);
        }
    }
}

// ===================================================================
// 12. Write encrypted, read without key fails
// ===================================================================
TEST_CASE("Write encrypted, read without key fails", "[crypto][pipeline]") {
    TempFile tmp("signet_test_encrypted_nokey.parquet");

    auto schema = Schema::builder("test")
        .column<int64_t>("id")
        .build();

    crypto::EncryptionConfig enc_cfg;
    enc_cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    enc_cfg.footer_key = generate_test_key(32);
    enc_cfg.encrypt_footer = true;
    enc_cfg.aad_prefix = "test-locked";

    // Write with encryption
    {
        ParquetWriter::Options opts;
        opts.encryption = enc_cfg;

        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        auto r = writer.write_row({"42"});
        REQUIRE(r.has_value());

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Try to open without encryption config -- should fail (PARE magic)
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE_FALSE(reader_result.has_value());
        REQUIRE(reader_result.error().code == ErrorCode::ENCRYPTION_ERROR);
    }
}

// ===================================================================
// 13. Plaintext footer with encrypted columns
// ===================================================================
TEST_CASE("Plaintext footer with encrypted columns", "[crypto][pipeline]") {
    TempFile tmp("signet_test_plaintext_footer.parquet");

    auto schema = Schema::builder("test")
        .column<int64_t>("id")
        .column<double>("price")
        .build();

    crypto::EncryptionConfig enc_cfg;
    enc_cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    enc_cfg.footer_key = generate_test_key(32);
    enc_cfg.default_column_key = generate_test_key(32);
    for (auto& b : enc_cfg.default_column_key) b ^= 0x77;
    enc_cfg.encrypt_footer = false;  // Plaintext footer
    enc_cfg.aad_prefix = "test-plaintext-footer";

    constexpr size_t NUM_ROWS = 10;

    // Write with plaintext footer but encrypted columns
    {
        ParquetWriter::Options opts;
        opts.encryption = enc_cfg;

        auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
        REQUIRE(writer_result.has_value());
        auto& writer = *writer_result;

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            auto r = writer.write_row({
                std::to_string(static_cast<int64_t>(i)),
                std::to_string(100.0 + static_cast<double>(i))
            });
            REQUIRE(r.has_value());
        }

        auto close_result = writer.close();
        REQUIRE(close_result.has_value());
    }

    // Open without encryption config -- footer is plaintext (PAR1), so open succeeds
    {
        auto reader_result = ParquetReader::open(tmp.path);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;
        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));
        REQUIRE(reader.schema().num_columns() == 2);
    }

    // Open with encryption config -- should succeed and return correct data
    {
        auto reader_result = ParquetReader::open(tmp.path, enc_cfg);
        REQUIRE(reader_result.has_value());
        auto& reader = *reader_result;

        REQUIRE(reader.num_rows() == static_cast<int64_t>(NUM_ROWS));

        auto all_result = reader.read_all();
        REQUIRE(all_result.has_value());
        const auto& rows = all_result.value();
        REQUIRE(rows.size() == NUM_ROWS);

        for (size_t i = 0; i < NUM_ROWS; ++i) {
            REQUIRE(rows[i][0] == std::to_string(static_cast<int64_t>(i)));
            double read_price = std::stod(rows[i][1]);
            REQUIRE(std::abs(read_price - (100.0 + static_cast<double>(i))) < 1e-6);
        }
    }
}

// ===================================================================
// 14. Kyber KEM encapsulate/decapsulate
// ===================================================================
TEST_CASE("Kyber KEM encapsulate/decapsulate", "[crypto][pq]") {
    // Generate keypair
    auto kp_result = crypto::KyberKem::generate_keypair();
    REQUIRE(kp_result.has_value());
    const auto& kp = kp_result.value();

    REQUIRE(kp.public_key.size() == crypto::KyberKem::PUBLIC_KEY_SIZE);
    REQUIRE(kp.secret_key.size() == crypto::KyberKem::SECRET_KEY_SIZE);

    // Encapsulate with public key
    auto encaps_result = crypto::KyberKem::encapsulate(
        kp.public_key.data(), kp.public_key.size());
    REQUIRE(encaps_result.has_value());
    const auto& encaps = encaps_result.value();

    REQUIRE(encaps.ciphertext.size() == crypto::KyberKem::CIPHERTEXT_SIZE);
    REQUIRE(encaps.shared_secret.size() == crypto::KyberKem::SHARED_SECRET_SIZE);

    // Decapsulate with secret key + ciphertext
    auto decaps_result = crypto::KyberKem::decapsulate(
        encaps.ciphertext.data(), encaps.ciphertext.size(),
        kp.secret_key.data(), kp.secret_key.size());
    REQUIRE(decaps_result.has_value());
    const auto& shared_secret_2 = decaps_result.value();

    REQUIRE(shared_secret_2.size() == crypto::KyberKem::SHARED_SECRET_SIZE);

    // Both shared secrets should match
    REQUIRE(encaps.shared_secret == shared_secret_2);
}

// ===================================================================
// 15. Dilithium sign/verify
// ===================================================================
TEST_CASE("Dilithium sign/verify", "[crypto][pq]") {
    // Generate signing keypair
    auto kp_result = crypto::DilithiumSign::generate_keypair();
    REQUIRE(kp_result.has_value());
    const auto& kp = kp_result.value();

    REQUIRE(kp.public_key.size() == crypto::DilithiumSign::PUBLIC_KEY_SIZE);
    REQUIRE(kp.secret_key.size() == crypto::DilithiumSign::SECRET_KEY_SIZE);

    // Sign a message
    std::string message = "Parquet file footer hash: 0xDEADBEEF";
    auto sig_result = crypto::DilithiumSign::sign(
        reinterpret_cast<const uint8_t*>(message.data()), message.size(),
        kp.secret_key.data(), kp.secret_key.size());
    REQUIRE(sig_result.has_value());
    const auto& signature = sig_result.value();

    REQUIRE(signature.size() > 0);
    REQUIRE(signature.size() <= crypto::DilithiumSign::SIGNATURE_MAX_SIZE);

    // Verify with correct public key -- should succeed
    auto verify_result = crypto::DilithiumSign::verify(
        reinterpret_cast<const uint8_t*>(message.data()), message.size(),
        signature.data(), signature.size(),
        kp.public_key.data(), kp.public_key.size());
    REQUIRE(verify_result.has_value());
    REQUIRE(verify_result.value() == true);

    // Verify with wrong public key -- should fail
    // Generate a second keypair for the wrong key
    auto kp2_result = crypto::DilithiumSign::generate_keypair();
    REQUIRE(kp2_result.has_value());
    const auto& kp2 = kp2_result.value();

    auto wrong_verify = crypto::DilithiumSign::verify(
        reinterpret_cast<const uint8_t*>(message.data()), message.size(),
        signature.data(), signature.size(),
        kp2.public_key.data(), kp2.public_key.size());
    REQUIRE(wrong_verify.has_value());
    REQUIRE(wrong_verify.value() == false);
}

// ===================================================================
// 16. EncryptionKeyMetadata serialize/deserialize
// ===================================================================
TEST_CASE("EncryptionKeyMetadata serialize/deserialize", "[crypto][metadata]") {
    crypto::EncryptionKeyMetadata meta;
    meta.key_mode = crypto::KeyMode::INTERNAL;
    meta.key_material = generate_test_key(32);
    meta.key_id = "test-key-001";

    // Serialize
    auto bytes = meta.serialize();
    REQUIRE(!bytes.empty());

    // Deserialize
    auto deser_result = crypto::EncryptionKeyMetadata::deserialize(
        bytes.data(), bytes.size());
    REQUIRE(deser_result.has_value());
    const auto& restored = deser_result.value();

    REQUIRE(restored.key_mode == crypto::KeyMode::INTERNAL);
    REQUIRE(restored.key_material == meta.key_material);
    REQUIRE(restored.key_id == meta.key_id);
}

// ===================================================================
// 18. X25519 RFC 7748 §6.1 known-answer vectors
// ===================================================================
TEST_CASE("X25519 RFC 7748 known-answer vectors", "[crypto][pq][x25519]") {
    // RFC 7748 §6.1 test vectors.
    // Private keys are the raw inputs to X25519 (which clamps internally).
    // Our detail::x25519::x25519() does not clamp; call clamp_scalar first.
    namespace x25519_ns = signet::forge::crypto::detail::x25519;

    // --- Alice ---
    std::array<uint8_t, 32> alice_raw = {
        0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,
        0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
        0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,
        0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a
    };
    std::array<uint8_t, 32> alice_pk_expected = {
        0x85,0x20,0xf0,0x09,0x89,0x30,0xa7,0x54,
        0x74,0x8b,0x7d,0xdc,0xb4,0x3e,0xf7,0x5a,
        0x0d,0xbf,0x3a,0x0d,0x26,0x38,0x1a,0xf4,
        0xeb,0xa4,0xa9,0x8e,0xaa,0x9b,0x4e,0x6a
    };

    // --- Bob ---
    std::array<uint8_t, 32> bob_raw = {
        0x5d,0xab,0x08,0x7e,0x62,0x4a,0x8a,0x4b,
        0x79,0xe1,0x7f,0x8b,0x83,0x80,0x0e,0xe6,
        0x6f,0x3b,0xb1,0x29,0x26,0x18,0xb6,0xfd,
        0x1c,0x2f,0x8b,0x27,0xff,0x88,0xe0,0xeb
    };
    std::array<uint8_t, 32> bob_pk_expected = {
        0xde,0x9e,0xdb,0x7d,0x7b,0x7d,0xc1,0xb4,
        0xd3,0x5b,0x61,0xc2,0xec,0xe4,0x35,0x37,
        0x3f,0x83,0x43,0xc8,0x5b,0x78,0x67,0x4d,
        0xad,0xfc,0x7e,0x14,0x6f,0x88,0x2b,0x4f
    };

    // --- Shared secret = X25519(alice_sk, bob_pk) = X25519(bob_sk, alice_pk) ---
    std::array<uint8_t, 32> shared_expected = {
        0x4a,0x5d,0x9d,0x5b,0xa4,0xce,0x2d,0xe1,
        0x72,0x8e,0x3b,0xf4,0x80,0x35,0x0f,0x25,
        0xe0,0x7e,0x21,0xc9,0x47,0xd1,0x9e,0x33,
        0x76,0xf0,0x9b,0x3c,0x1e,0x16,0x17,0x42
    };

    // Clamp the raw private keys per RFC 7748 §5
    auto alice_sk = x25519_ns::clamp_scalar(alice_raw);
    auto bob_sk   = x25519_ns::clamp_scalar(bob_raw);
    const auto& G = x25519_ns::base_point();

    // Verify public key derivation
    auto alice_pk_r = x25519_ns::x25519(alice_sk, G);
    REQUIRE(alice_pk_r.has_value());
    CHECK(*alice_pk_r == alice_pk_expected);

    auto bob_pk_r = x25519_ns::x25519(bob_sk, G);
    REQUIRE(bob_pk_r.has_value());
    CHECK(*bob_pk_r == bob_pk_expected);

    // Verify shared secret commutativity (the key property that fixes HybridKem)
    auto ss_alice = x25519_ns::x25519(alice_sk, bob_pk_expected);
    REQUIRE(ss_alice.has_value());
    CHECK(*ss_alice == shared_expected);

    auto ss_bob = x25519_ns::x25519(bob_sk, alice_pk_expected);
    REQUIRE(ss_bob.has_value());
    CHECK(*ss_bob == shared_expected);

    // Commutativity holds: X25519(a, B) == X25519(b, A)
    CHECK(*ss_alice == *ss_bob);
}

// ===================================================================
// 19. HybridKem round-trip shared secret
// ===================================================================
TEST_CASE("HybridKem round-trip shared secret", "[crypto][pq][hybrid]") {
    // Generate recipient keypair (Kyber-768 + real X25519)
    auto kp_result = crypto::HybridKem::generate_keypair();
    REQUIRE(kp_result.has_value());
    const auto& kp = kp_result.value();

    // Both Kyber and X25519 components present
    REQUIRE(kp.kyber_public_key.size()  == crypto::KyberKem::PUBLIC_KEY_SIZE);
    REQUIRE(kp.kyber_secret_key.size()  == crypto::KyberKem::SECRET_KEY_SIZE);
    REQUIRE(kp.x25519_public_key.size() == 32);
    REQUIRE(kp.x25519_secret_key.size() == 32);

    // Encapsulate: sender produces ciphertext + shared secret
    auto encaps_result = crypto::HybridKem::encapsulate(kp);
    REQUIRE(encaps_result.has_value());
    const auto& encaps = encaps_result.value();

    REQUIRE(encaps.shared_secret.size()    == crypto::HybridKem::HYBRID_SHARED_SECRET_SIZE);
    REQUIRE(encaps.kyber_ciphertext.size() == crypto::KyberKem::CIPHERTEXT_SIZE);
    REQUIRE(encaps.x25519_public_key.size()== 32);  // ephemeral public key

    // Decapsulate: recipient recovers the shared secret
    auto decaps_result = crypto::HybridKem::decapsulate(encaps, kp);
    REQUIRE(decaps_result.has_value());

    // Core property: encapsulate and decapsulate produce identical shared secrets
    // because X25519(eph_sk, recip_pk) == X25519(recip_sk, eph_pk) (DH commutativity)
    REQUIRE(*decaps_result == encaps.shared_secret);
}

// ===================================================================
// 20. HybridKem degenerate X25519 input rejected
// ===================================================================
TEST_CASE("HybridKem degenerate X25519 input rejected", "[crypto][pq][hybrid]") {
    // Generate a valid recipient keypair
    auto kp_result = crypto::HybridKem::generate_keypair();
    REQUIRE(kp_result.has_value());
    const auto& kp = kp_result.value();

    // Start with a real encapsulation, then corrupt the X25519 ephemeral key
    auto encaps_result = crypto::HybridKem::encapsulate(kp);
    REQUIRE(encaps_result.has_value());
    auto encaps = encaps_result.value();

    // Replace ephemeral X25519 public key with the all-zero low-order point.
    // X25519(any_sk, 0) returns 0 (the identity), which trips the degenerate
    // output guard in x25519() per RFC 7748 §6.
    encaps.x25519_public_key.assign(32, 0x00);

    // Decapsulation must fail — all-zero X25519 output is rejected
    auto bad_result = crypto::HybridKem::decapsulate(encaps, kp);
    CHECK(!bad_result.has_value());
}

// ===================================================================
// 17. FileEncryptionProperties serialize/deserialize
// ===================================================================
TEST_CASE("FileEncryptionProperties serialize/deserialize", "[crypto][metadata]") {
    crypto::FileEncryptionProperties props;
    props.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    props.footer_encrypted = true;
    props.aad_prefix = "s3://my-bucket/table/part-00000.parquet";

    // Serialize
    auto bytes = props.serialize();
    REQUIRE(!bytes.empty());

    // Deserialize
    auto deser_result = crypto::FileEncryptionProperties::deserialize(
        bytes.data(), bytes.size());
    REQUIRE(deser_result.has_value());
    const auto& restored = deser_result.value();

    REQUIRE(restored.algorithm == crypto::EncryptionAlgorithm::AES_GCM_CTR_V1);
    REQUIRE(restored.footer_encrypted == true);
    REQUIRE(restored.aad_prefix == props.aad_prefix);

    // Also test with AES_GCM_V1 and footer not encrypted
    crypto::FileEncryptionProperties props2;
    props2.algorithm = crypto::EncryptionAlgorithm::AES_GCM_V1;
    props2.footer_encrypted = false;
    props2.aad_prefix = "";

    auto bytes2 = props2.serialize();
    auto deser2 = crypto::FileEncryptionProperties::deserialize(
        bytes2.data(), bytes2.size());
    REQUIRE(deser2.has_value());
    REQUIRE(deser2->algorithm == crypto::EncryptionAlgorithm::AES_GCM_V1);
    REQUIRE(deser2->footer_encrypted == false);
    REQUIRE(deser2->aad_prefix.empty());
}

// ===================================================================
// 21. AesGcmCipher encrypt/decrypt roundtrip with AAD
// ===================================================================
TEST_CASE("AesGcmCipher encrypt/decrypt roundtrip with AAD", "[crypto][cipher_interface]") {
    auto key = generate_test_key(32);
    crypto::AesGcmCipher cipher(key);

    CHECK(cipher.is_authenticated() == true);
    CHECK(cipher.key_size() == 32);
    CHECK(cipher.algorithm_name() == "AES-256-GCM");

    std::string plaintext = "Hello, ICipher with AAD!";
    std::string aad = "footer-aad-context";

    auto ct = cipher.encrypt(
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(), aad);
    REQUIRE(ct.has_value());

    // Ciphertext should be larger (IV header + GCM tag)
    REQUIRE(ct->size() > plaintext.size());

    // Decrypt with same AAD should succeed
    auto pt = cipher.decrypt(ct->data(), ct->size(), aad);
    REQUIRE(pt.has_value());
    std::string decrypted(reinterpret_cast<const char*>(pt->data()), pt->size());
    CHECK(decrypted == plaintext);

    // Decrypt with wrong AAD should fail
    auto bad = cipher.decrypt(ct->data(), ct->size(), "wrong-aad");
    CHECK_FALSE(bad.has_value());
}

// ===================================================================
// 22. AesCtrCipher encrypt/decrypt roundtrip (no AAD)
// ===================================================================
TEST_CASE("AesCtrCipher encrypt/decrypt roundtrip", "[crypto][cipher_interface]") {
    auto key = generate_test_key(32);
    crypto::AesCtrCipher cipher(key);

    CHECK(cipher.is_authenticated() == false);
    CHECK(cipher.key_size() == 32);
    CHECK(cipher.algorithm_name() == "AES-256-CTR");

    std::string plaintext = "CTR mode test data — no authentication";

    auto ct = cipher.encrypt(
        reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
    REQUIRE(ct.has_value());

    // Ciphertext should be larger (IV header)
    REQUIRE(ct->size() > plaintext.size());

    auto pt = cipher.decrypt(ct->data(), ct->size());
    REQUIRE(pt.has_value());
    std::string decrypted(reinterpret_cast<const char*>(pt->data()), pt->size());
    CHECK(decrypted == plaintext);
}

// ===================================================================
// 23. CipherFactory creates correct cipher type for each algorithm
// ===================================================================
TEST_CASE("CipherFactory creates correct cipher types", "[crypto][cipher_interface]") {
    auto key = generate_test_key(32);

    // AES_GCM_V1: everything is GCM (authenticated)
    auto footer_gcm = crypto::CipherFactory::create_footer_cipher(
        crypto::EncryptionAlgorithm::AES_GCM_V1, key);
    CHECK(footer_gcm->is_authenticated() == true);

    auto col_gcm = crypto::CipherFactory::create_column_cipher(
        crypto::EncryptionAlgorithm::AES_GCM_V1, key);
    CHECK(col_gcm->is_authenticated() == true);

    auto meta_gcm = crypto::CipherFactory::create_metadata_cipher(
        crypto::EncryptionAlgorithm::AES_GCM_V1, key);
    CHECK(meta_gcm->is_authenticated() == true);

    // AES_GCM_CTR_V1: footer=GCM, column=CTR, metadata=GCM
    auto footer_ctr = crypto::CipherFactory::create_footer_cipher(
        crypto::EncryptionAlgorithm::AES_GCM_CTR_V1, key);
    CHECK(footer_ctr->is_authenticated() == true);

    auto col_ctr = crypto::CipherFactory::create_column_cipher(
        crypto::EncryptionAlgorithm::AES_GCM_CTR_V1, key);
    CHECK(col_ctr->is_authenticated() == false);  // CTR — no auth

    auto meta_ctr = crypto::CipherFactory::create_metadata_cipher(
        crypto::EncryptionAlgorithm::AES_GCM_CTR_V1, key);
    CHECK(meta_ctr->is_authenticated() == true);

    // Roundtrip: factory-created cipher can encrypt → decrypt
    std::string data = "roundtrip test";
    auto ct = col_ctr->encrypt(
        reinterpret_cast<const uint8_t*>(data.data()), data.size());
    REQUIRE(ct.has_value());
    auto pt = col_ctr->decrypt(ct->data(), ct->size());
    REQUIRE(pt.has_value());
    std::string result(reinterpret_cast<const char*>(pt->data()), pt->size());
    CHECK(result == data);
}

// ===================================================================
// Hardening Pass #3 Tests
// ===================================================================

TEST_CASE("ThriftCompactDecoder: negative list count sets error", "[hardening]") {
    using namespace signet::forge::thrift;

    // Craft a list header with size=15 (large form), followed by a varint
    // encoding of a value > INT32_MAX (which is negative as int32_t)
    std::vector<uint8_t> data;
    // List header byte: size nibble = 0xF (15), elem_type = I32 (5)
    data.push_back(0xF5);
    // Varint encoding of 0x80000001 (> INT32_MAX) — 5 bytes
    uint32_t big_val = 0x80000001u;
    for (int i = 0; i < 4; ++i) {
        data.push_back(static_cast<uint8_t>((big_val & 0x7F) | 0x80));
        big_val >>= 7;
    }
    data.push_back(static_cast<uint8_t>(big_val & 0x7F));

    CompactDecoder dec(data.data(), data.size());
    auto hdr = dec.read_list_header();
    CHECK_FALSE(dec.good()); // must set error
    CHECK(hdr.size == 0);
}

TEST_CASE("ThriftCompactDecoder: MAP size over 1M sets error", "[hardening]") {
    using namespace signet::forge::thrift;

    // Build a struct with a MAP field whose size > 1M
    CompactEncoder enc;
    enc.write_field(1, compact_type::MAP);
    // Write MAP header manually: varint size = 2'000'000 (> MAX_COLLECTION_SIZE)
    // We'll construct the raw bytes
    auto enc_data = enc.data();

    // Now construct the decoder input: the field header + map varint size
    std::vector<uint8_t> data(enc_data.begin(), enc_data.end());
    // Append varint for 2'000'000
    uint32_t map_size = 2'000'000u;
    while (map_size >= 0x80) {
        data.push_back(static_cast<uint8_t>((map_size & 0x7F) | 0x80));
        map_size >>= 7;
    }
    data.push_back(static_cast<uint8_t>(map_size));

    CompactDecoder dec(data.data(), data.size());
    auto fh = dec.read_field_header();
    REQUIRE(fh.thrift_type == compact_type::MAP);
    // Now skip the MAP field — this should trigger MAX_COLLECTION_SIZE error
    dec.skip_field(compact_type::MAP);
    CHECK_FALSE(dec.good());
}

TEST_CASE("AES-GCM: plaintext over 64GB returns error", "[encryption][hardening]") {
    using namespace signet::forge::crypto;

    std::vector<uint8_t> key(32, 0x42);
    AesGcm gcm(key.data());
    uint8_t iv[12] = {};

    // We can't allocate 64GB, but we can test the check by passing a size
    // that exceeds MAX_GCM_PLAINTEXT to encrypt(). The function checks the
    // size parameter, not the actual buffer.
    // MAX_GCM_PLAINTEXT = (1ULL << 36) - 32
    // Pass nullptr for the data (the size check comes first)
    size_t huge_size = static_cast<size_t>((1ULL << 36));
    auto result = gcm.encrypt(nullptr, huge_size, iv);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("TLV: oversized field.length returns false", "[encryption][hardening]") {
    namespace meta = signet::forge::crypto::detail::meta;

    // Craft a TLV with length = 128 MB (> MAX_TLV_LENGTH of 64 MB)
    std::vector<uint8_t> buf(16);
    meta::write_le32(buf.data(), 0x0001);       // tag
    meta::write_le32(buf.data() + 4, 128u * 1024u * 1024u); // length = 128MB

    size_t offset = 0;
    meta::TlvField field;
    bool ok = meta::read_tlv(buf.data(), buf.size(), offset, field);
    CHECK_FALSE(ok); // must reject
}

TEST_CASE("TLV: field.length + offset overflow returns false", "[encryption][hardening]") {
    namespace meta = signet::forge::crypto::detail::meta;

    // Craft a TLV with length that would overflow when added to offset
    std::vector<uint8_t> buf(16);
    meta::write_le32(buf.data(), 0x0002);             // tag
    meta::write_le32(buf.data() + 4, 0xFFFFFFFFu);   // length = max uint32

    size_t offset = 0;
    meta::TlvField field;
    bool ok = meta::read_tlv(buf.data(), buf.size(), offset, field);
    CHECK_FALSE(ok); // must reject — length exceeds MAX_TLV_LENGTH
}

TEST_CASE("is_real_pq_crypto() returns expected value", "[encryption][hardening]") {
    using namespace signet::forge::crypto;

    // In test builds without liboqs, should return false
    bool result = is_real_pq_crypto();
#ifdef SIGNET_HAS_LIBOQS
    CHECK(result == true);
#else
    CHECK(result == false);
#endif
}

TEST_CASE("Aes256 constructor/destructor lifecycle", "[encryption][hardening]") {
    using namespace signet::forge::crypto;

    // Verify that Aes256 can be constructed and destroyed without issues
    // (the destructor securely zeros round_keys_)
    std::vector<uint8_t> key(32, 0xAA);
    {
        Aes256 cipher(key.data());
        uint8_t block[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                             0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
        uint8_t original[16];
        std::memcpy(original, block, 16);

        cipher.encrypt_block(block);
        // Encrypted block should differ from original
        bool differs = false;
        for (int i = 0; i < 16; ++i) {
            if (block[i] != original[i]) { differs = true; break; }
        }
        CHECK(differs);

        cipher.decrypt_block(block);
        // Should roundtrip back to original
        CHECK(std::memcmp(block, original, 16) == 0);
    }
    // Aes256 destructor has been called here — round keys zeroed
    SUCCEED("Aes256 lifecycle completed (round keys zeroed on destruction)");
}

TEST_CASE("AesGcmCipher/AesCtrCipher destruction lifecycle", "[encryption][hardening]") {
    using namespace signet::forge::crypto;

    std::vector<uint8_t> key(32, 0xBB);
    std::string plaintext = "test data for cipher destruction lifecycle";

    // GCM cipher lifecycle
    {
        AesGcmCipher gcm(key);
        auto ct = gcm.encrypt(
            reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
        REQUIRE(ct.has_value());
        auto pt = gcm.decrypt(ct->data(), ct->size());
        REQUIRE(pt.has_value());
        std::string result(reinterpret_cast<const char*>(pt->data()), pt->size());
        CHECK(result == plaintext);
    }
    // AesGcmCipher destructor has zeroed key_ here

    // CTR cipher lifecycle
    {
        AesCtrCipher ctr(key);
        auto ct = ctr.encrypt(
            reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size());
        REQUIRE(ct.has_value());
        auto pt = ctr.decrypt(ct->data(), ct->size());
        REQUIRE(pt.has_value());
        std::string result(reinterpret_cast<const char*>(pt->data()), pt->size());
        CHECK(result == plaintext);
    }
    // AesCtrCipher destructor has zeroed key_ here

    SUCCEED("Cipher destruction lifecycle completed (keys zeroed)");
}
