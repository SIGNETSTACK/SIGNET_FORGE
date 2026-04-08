// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// SPDX-License-Identifier: AGPL-3.0-or-later
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/crypto/kms_local.hpp is a AGPL-3.0 + Commercial Exception commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

/// @file kms_local.hpp
/// @brief File-based local key store — IKmsClient implementation for on-premise deployments.
///
/// Stores AES-256 master keys on disk, wrapped under a passphrase-derived KEK.
/// Key derivation: passphrase → HKDF-Extract(salt, passphrase) → KEK
/// Key wrapping:   AES Key Wrap (RFC 3394) under the KEK
///
/// Storage layout:
///   <keystore_path>/
///     keys/           — Individual wrapped key files
///     audit.log       — Append-only key access log
///
/// NOT suitable for high-security environments — use cloud KMS or HSM for
/// production deployments handling regulated data. This adapter is designed
/// for on-premise, air-gapped, or development environments.
///
/// References:
///   - NIST SP 800-57 Part 1 §5.3 (key hierarchy)
///   - RFC 3394 (AES Key Wrap)
///   - RFC 5869 (HKDF)
///   - docs/internal/10_KEY_MANAGEMENT_AND_LICENSING.md §4

#include "signet/crypto/hkdf.hpp"
#include "signet/crypto/hsm_client_stub.hpp"  // IKmsClient, AES Key Wrap
#include "signet/crypto/key_metadata.hpp"
#include "signet/error.hpp"

#include <array>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace signet::forge::crypto {

/// File-based local key store for on-premise deployments.
///
/// Wraps master keys under a passphrase-derived KEK and stores them
/// on the local filesystem. Suitable for air-gapped or single-machine
/// deployments where cloud KMS is not available.
///
/// Thread safety: All public methods are protected by a mutable mutex.
///
/// Usage:
/// @code
///   auto store = std::make_shared<LocalKeyStore>(LocalKeyStore::Config{
///       .keystore_path = "/home/user/.signet/keystore",
///       .passphrase = "my-secure-passphrase"
///   });
///   // Generate a master key
///   store->generate_key("master-001");
///   // Use as IKmsClient
///   config.kms_client = store;
/// @endcode
class LocalKeyStore : public IKmsClient {
public:
    struct Config {
        std::string keystore_path;     ///< Directory path (e.g. ~/.signet/keystore)
        std::string passphrase;        ///< Passphrase for KEK derivation
        bool create_if_missing = true; ///< Create keystore directory on first use
        /// PBKDF2-SHA256 iteration count for passphrase → KEK stretching.
        /// OWASP 2023 / NIST SP 800-132 recommend ≥ 600 000 for production.
        /// Reduce only in automated tests (e.g. 1000) — never below 1000.
        uint32_t pbkdf2_iterations = 600'000u;
    };

    /// Construct a LocalKeyStore from configuration.
    explicit LocalKeyStore(Config config)
        : config_(std::move(config))
    {
        derive_kek();
    }

    ~LocalKeyStore() override {
        // Secure-zero KEK on destruction
        secure_zero(kek_.data(), kek_.size());
        // Secure-zero cached keys
        for (auto& [id, key] : keys_)
            secure_zero(key.data(), key.size());
    }

    LocalKeyStore(const LocalKeyStore&) = delete;
    LocalKeyStore& operator=(const LocalKeyStore&) = delete;

    // --- IKmsClient interface (const, thread-safe via mutable mutex) ---

    /// Wrap (encrypt) a DEK under the master key identified by key_id.
    [[nodiscard]] expected<std::vector<uint8_t>> wrap_key(
        const std::vector<uint8_t>& dek,
        const std::string& key_id) const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto master = load_key_internal(key_id);
        if (!master) return master.error();

        std::array<uint8_t, 32> master_arr{};
        std::memcpy(master_arr.data(), master->data(),
                    std::min(master->size(), size_t(32)));

        auto result = detail::aes_key_wrap::wrap(master_arr, dek);
        secure_zero(master_arr.data(), master_arr.size());
        log_access(key_id, "wrap");
        return result;
    }

    /// Unwrap (decrypt) a wrapped DEK using the master key identified by key_id.
    [[nodiscard]] expected<std::vector<uint8_t>> unwrap_key(
        const std::vector<uint8_t>& wrapped_dek,
        const std::string& key_id) const override
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto master = load_key_internal(key_id);
        if (!master) return master.error();

        std::array<uint8_t, 32> master_arr{};
        std::memcpy(master_arr.data(), master->data(),
                    std::min(master->size(), size_t(32)));

        auto result = detail::aes_key_wrap::unwrap(master_arr, wrapped_dek);
        secure_zero(master_arr.data(), master_arr.size());
        log_access(key_id, "unwrap");
        return result;
    }

    // --- Extended key lifecycle methods (not part of IKmsClient) ---

    /// Generate a new AES-256 master key and store it under key_id.
    [[nodiscard]] expected<std::string> generate_key(const std::string& key_id) {
        std::lock_guard<std::mutex> lock(mu_);
        std::array<uint8_t, 32> key{};
        csprng_fill(key.data(), key.size());
        std::vector<uint8_t> key_vec(key.begin(), key.end());

        auto result = store_key(key_id, key_vec);
        secure_zero(key.data(), key.size());
        if (!result) return result.error();

        log_access(key_id, "generate");
        return key_id;
    }

    /// Destroy a master key (crypto-shredding for GDPR Art. 17).
    [[nodiscard]] expected<void> destroy_key(const std::string& key_id) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = keys_.find(key_id);
        if (it != keys_.end()) {
            secure_zero(it->second.data(), it->second.size());
            keys_.erase(it);
        }

        std::string path = key_file_path(key_id);
        std::remove(path.c_str());

        log_access(key_id, "destroy");
        return expected<void>{};
    }

    /// Check if a key exists in the store (cached or on disk).
    [[nodiscard]] bool has_key(const std::string& key_id) const {
        std::lock_guard<std::mutex> lock(mu_);
        if (keys_.find(key_id) != keys_.end()) return true;
        std::ifstream ifs(key_file_path(key_id), std::ios::binary);
        return ifs.good();
    }

private:
    Config config_;
    std::array<uint8_t, 32> kek_{};
    mutable std::unordered_map<std::string, std::vector<uint8_t>> keys_;
    mutable std::mutex mu_;

    /// PBKDF2-SHA256 single-block (32-byte) key derivation (RFC 8018 §5.2).
    ///
    /// Provides a memory-hard work factor for passphrase-derived keys so that
    /// offline brute-force attacks require O(iterations) HMAC-SHA256 evaluations
    /// per candidate (NIST SP 800-132, OWASP 2023).
    static std::array<uint8_t, 32> pbkdf2_sha256_32(
            const uint8_t* password, size_t password_len,
            const uint8_t* salt,     size_t salt_len,
            uint32_t iterations) {
        // U_1 = HMAC-SHA256(password, salt || INT(1))
        std::vector<uint8_t> u1_input;
        u1_input.reserve(salt_len + 4u);
        u1_input.insert(u1_input.end(), salt, salt + salt_len);
        // Block index 1 as 4-byte big-endian
        u1_input.push_back(0x00u);
        u1_input.push_back(0x00u);
        u1_input.push_back(0x00u);
        u1_input.push_back(0x01u);

        auto u = detail::hkdf::hmac_sha256(
            password, password_len, u1_input.data(), u1_input.size());

        // Zeroize the salt+INT(1) buffer (CWE-316)
        volatile uint8_t* vp = u1_input.data();
        for (size_t i = 0; i < u1_input.size(); ++i) vp[i] = 0u;

        // DK = U_1 ^ U_2 ^ ... ^ U_c
        std::array<uint8_t, 32> dk = u;
        for (uint32_t j = 1u; j < iterations; ++j) {
            u = detail::hkdf::hmac_sha256(password, password_len, u.data(), u.size());
            for (size_t k = 0; k < 32u; ++k) dk[k] ^= u[k];
        }

        // Zeroize last U block (CWE-316)
        volatile uint8_t* vpu = u.data();
        for (size_t i = 0; i < 32u; ++i) vpu[i] = 0u;

        return dk;
    }

    void derive_kek() {
        static constexpr uint8_t kek_salt[] = "signet:local-keystore:kek:v1";
        static constexpr uint8_t kek_info[] = "signet:kek-derivation";

        auto passphrase_bytes = reinterpret_cast<const uint8_t*>(config_.passphrase.data());

        // F4: PBKDF2-SHA256 password-stretching before HKDF provides work factor
        // for low-entropy operator passphrases (NIST SP 800-132, OWASP 2023).
        // The stretched output replaces raw passphrase bytes as the IKM for HKDF.
        auto stretched = pbkdf2_sha256_32(
            passphrase_bytes, config_.passphrase.size(),
            kek_salt, sizeof(kek_salt) - 1u,
            config_.pbkdf2_iterations);

        auto prk = hkdf_extract(kek_salt, sizeof(kek_salt) - 1,
                                stretched.data(), stretched.size());

        // Zeroize stretched key material before stack is reused (CWE-316)
        volatile uint8_t* vp = stretched.data();
        for (size_t i = 0; i < stretched.size(); ++i) vp[i] = 0u;

        (void)hkdf_expand(prk, kek_info, sizeof(kek_info) - 1, kek_.data(), kek_.size());
    }

    static void secure_zero(void* ptr, size_t len) {
        volatile auto* p = static_cast<volatile uint8_t*>(ptr);
        while (len--) *p++ = 0;
    }

    static void csprng_fill(uint8_t* buf, size_t len) {
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
        arc4random_buf(buf, len);
#elif defined(__linux__)
        // F3: Only EINTR is retryable. All other negative returns (ENOSYS,
        // EFAULT, EINVAL) indicate a permanently broken entropy source.
        // Rather than loop indefinitely, terminate — continuing with
        // uninitialized key material is catastrophically worse than crashing
        // (consistent with OpenSSL / libsodium abort-on-CSPRNG-failure policy).
        static constexpr int kMaxRetries = 100;
        int retries = 0;
        while (len > 0) {
            auto got = getrandom(buf, len, 0);
            if (got < 0) {
                if (errno == EINTR && ++retries < kMaxRetries) continue;
                // Non-retryable error or retry limit exceeded — hard fail.
                std::terminate();
            }
            retries = 0;
            buf += got;
            len -= static_cast<size_t>(got);
        }
#elif defined(_WIN32)
        BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#endif
    }

    std::string key_file_path(const std::string& key_id) const {
        return config_.keystore_path + "/keys/" + key_id + ".key";
    }

    /// Load a key from cache or from disk. Caller must hold mu_.
    [[nodiscard]] expected<std::vector<uint8_t>> load_key_internal(const std::string& key_id) const {
        auto it = keys_.find(key_id);
        if (it != keys_.end()) return it->second;

        std::string path = key_file_path(key_id);
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs) {
            return Error{ErrorCode::ENCRYPTION_ERROR, "key not found: " + key_id};
        }

        std::vector<uint8_t> wrapped((std::istreambuf_iterator<char>(ifs)),
                                      std::istreambuf_iterator<char>());
        ifs.close();

        auto plaintext = detail::aes_key_wrap::unwrap(kek_, wrapped);
        if (!plaintext) return plaintext.error();

        keys_[key_id] = *plaintext;
        return *plaintext;
    }

    /// Wrap and store a key to disk. Caller must hold mu_.
    [[nodiscard]] expected<void> store_key(const std::string& key_id,
                                            const std::vector<uint8_t>& plaintext) {
        auto wrapped = detail::aes_key_wrap::wrap(kek_, plaintext);
        if (!wrapped) return wrapped.error();

        std::string dir = config_.keystore_path + "/keys";
#if defined(_WIN32)
        (void)_mkdir(config_.keystore_path.c_str());
        (void)_mkdir(dir.c_str());
#else
        (void)::mkdir(config_.keystore_path.c_str(), 0700);
        (void)::mkdir(dir.c_str(), 0700);
#endif

        std::string path = key_file_path(key_id);
        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            return Error{ErrorCode::IO_ERROR, "failed to write key file: " + path};
        }
        ofs.write(reinterpret_cast<const char*>(wrapped->data()),
                  static_cast<std::streamsize>(wrapped->size()));
        ofs.close();

#if !defined(_WIN32)
        ::chmod(path.c_str(), 0600);
#endif

        keys_[key_id] = plaintext;
        return expected<void>{};
    }

    /// Append an audit log entry. Caller must hold mu_.
    void log_access(const std::string& key_id, const char* operation) const {
        std::string log_path = config_.keystore_path + "/audit.log";
        std::ofstream log_file(log_path, std::ios::app);
        if (!log_file) return;

        auto now = std::chrono::system_clock::now();
        auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();

        log_file << epoch << " " << operation << " " << key_id << "\n";
    }
};

} // namespace signet::forge::crypto
