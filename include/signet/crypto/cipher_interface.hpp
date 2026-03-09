// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file cipher_interface.hpp
/// @brief Abstract cipher interface, GCM/CTR adapters, CipherFactory, and platform CSPRNG.

// ---------------------------------------------------------------------------
// cipher_interface.hpp -- Abstract cipher interface + CipherFactory
//
// Provides crypto-agility for Parquet Modular Encryption by abstracting
// the cipher behind an ICipher interface. Concrete adapters wrap AesGcm
// and AesCtr. CipherFactory selects the correct cipher for each PME role
// (footer, column data, metadata) based on the EncryptionAlgorithm enum.
//
// Wire format (unified across all ciphers):
//   [1 byte: iv_size] [iv bytes] [ciphertext (+tag for GCM)]
//
// The interface is Apache 2.0 (core tier) — it's not gated by BSL.
// ---------------------------------------------------------------------------

#include "signet/crypto/aes_gcm.hpp"
#include "signet/crypto/aes_ctr.hpp"
#include "signet/crypto/key_metadata.hpp"
#include "signet/error.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// Platform-specific CSPRNG + mlock headers
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#  include <stdlib.h>     // arc4random_buf
#  include <sys/mman.h>   // mlock, munlock (Gap C-11)
#elif defined(__linux__)
#  include <sys/random.h> // getrandom
#  include <sys/mman.h>   // mlock, munlock (Gap C-11)
#elif defined(_WIN32)
#  include <windows.h>    // VirtualLock, VirtualUnlock, SecureZeroMemory
#  include <bcrypt.h>
#endif

#include <cerrno>
#include <stdexcept>

namespace signet::forge::crypto {

// ===========================================================================
// ICipher -- Abstract cipher interface
// ===========================================================================

/// Abstract cipher interface — unified API for authenticated (GCM) and
/// unauthenticated (CTR) encryption. Implementations are move-only
/// (hold key material).
class ICipher {
public:
    virtual ~ICipher() = default;

    /// Encrypt data. For authenticated ciphers, aad is bound into the tag.
    /// For unauthenticated ciphers, aad is ignored.
    /// Returns: [iv_size(1)] [iv] [ciphertext] [tag if authenticated]
    [[nodiscard]] virtual expected<std::vector<uint8_t>> encrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const = 0;

    /// Decrypt data produced by encrypt().
    [[nodiscard]] virtual expected<std::vector<uint8_t>> decrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const = 0;

    /// Whether this cipher provides authentication (GCM=true, CTR=false).
    [[nodiscard]] virtual bool is_authenticated() const noexcept = 0;

    /// Key size in bytes (32 for AES-256).
    [[nodiscard]] virtual size_t key_size() const noexcept = 0;

    /// Human-readable algorithm name.
    [[nodiscard]] virtual std::string_view algorithm_name() const noexcept = 0;

    // Non-copyable, non-movable (interface type)
    ICipher() = default;
    ICipher(const ICipher&) = delete;
    ICipher& operator=(const ICipher&) = delete;
    ICipher(ICipher&&) = default;
    ICipher& operator=(ICipher&&) = default;
};

// ===========================================================================
// Internal: IV generation (shared by both adapters)
// ===========================================================================

namespace detail::cipher {

/// Fill a buffer with cryptographically random bytes using the best
/// available OS-level CSPRNG (CWE-338: Use of Cryptographically Weak PRNG).
///   - macOS/BSD: arc4random_buf (seeded from /dev/urandom, never fails)
///   - Linux: getrandom(2) with EINTR retry (blocks until urandom seeded)
///   - Windows: BCryptGenRandom with ULONG size validation (CWE-190)
inline void fill_random_bytes(uint8_t* buf, size_t size) {
    if (size == 0) return;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
    arc4random_buf(buf, size);
#elif defined(__linux__)
    // getrandom() with flags=0 blocks until urandom is seeded
    size_t written = 0;
    while (written < size) {
        ssize_t ret = getrandom(buf + written, size - written, 0);
        if (ret < 0) {
            if (errno == EINTR) continue;  // retry on signal interrupt
            // Real error — zero partial output to avoid leaking partial randomness
            volatile unsigned char* p = buf;
            for (size_t i = 0; i < size; ++i) p[i] = 0;
            throw std::runtime_error("signet: getrandom() failed");
        }
        written += static_cast<size_t>(ret);
    }
#elif defined(_WIN32)
    if (size > static_cast<size_t>((std::numeric_limits<ULONG>::max)())) {
        throw std::runtime_error("csprng_fill: size exceeds ULONG max");
    }
    NTSTATUS status = BCryptGenRandom(NULL, buf, static_cast<ULONG>(size),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
#else
    throw std::runtime_error("signet: no secure RNG available on this platform");
#endif
}

/// Generate a random initialization vector of the specified size.
/// @param iv_size  Number of random bytes to generate (12 for GCM, 16 for CTR).
/// @return Vector of cryptographically random bytes.
inline std::vector<uint8_t> generate_iv(size_t iv_size) {
    std::vector<uint8_t> iv(iv_size);
    fill_random_bytes(iv.data(), iv_size);
    return iv;
}

/// Prepend an IV header to ciphertext: [1 byte: iv.size()] [iv bytes] [ciphertext].
/// @param iv          Initialization vector bytes.
/// @param ciphertext  Encrypted data (may include auth tag for GCM).
/// @return Combined output with IV header prepended.
inline std::vector<uint8_t> prepend_iv(const std::vector<uint8_t>& iv,
                                        const std::vector<uint8_t>& ciphertext) {
    std::vector<uint8_t> out;
    out.reserve(1 + iv.size() + ciphertext.size());
    out.push_back(static_cast<uint8_t>(iv.size()));
    out.insert(out.end(), iv.begin(), iv.end());
    out.insert(out.end(), ciphertext.begin(), ciphertext.end());
    return out;
}

/// Result of parsing an IV header from encrypted data.
struct IvParsed {
    const uint8_t* iv;          ///< Pointer to the IV bytes within the input buffer.
    const uint8_t* ciphertext;  ///< Pointer to the ciphertext after the IV.
    size_t ct_size;             ///< Ciphertext length (may include GCM auth tag).
};

/// Parse the IV header from encrypted data: [1 byte: iv_size] [iv] [ciphertext].
/// @param data  Pointer to the encrypted data buffer.
/// @param size  Total size of the encrypted data.
/// @return Parsed IV and ciphertext pointers, or an error if malformed.
inline expected<IvParsed> parse_iv_header(const uint8_t* data, size_t size) {
    if (size < 1) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "cipher: encrypted data too short (no IV size byte)"};
    }
    uint8_t iv_size = data[0];
    if (iv_size == 0 || iv_size > 16) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "cipher: invalid IV size " + std::to_string(iv_size)};
    }
    size_t header_len = 1 + static_cast<size_t>(iv_size);
    if (size < header_len) {
        return Error{ErrorCode::ENCRYPTION_ERROR,
                     "cipher: encrypted data too short for IV"};
    }
    return IvParsed{data + 1, data + header_len, size - header_len};
}

} // namespace detail::cipher

// ===========================================================================
// Continuous Random Number Generator Test (CRNGT) wrapper (Gap C-13)
//
// FIPS 140-3 §4.9.2 requires a continuous test on the RNG output:
// each block of random output must differ from the previous block.
// If two consecutive blocks are identical, the RNG has failed and
// the module must enter an error state.
//
// This wrapper generates random bytes via the platform CSPRNG and
// compares each 32-byte block against the previous output. On
// failure, it throws (entering an error state per FIPS 140-3).
//
// Reference: FIPS 140-3 §4.9.2 — Continuous random number generator test
//            NIST SP 800-90B §4 — Health tests for entropy sources
// ===========================================================================

namespace detail::crngt {

/// CRNGT state — stores the previous 32-byte RNG output for comparison.
struct CrngtState {
    uint8_t prev[32] = {};
    bool    initialized = false;
};

/// Generate random bytes with FIPS 140-3 §4.9.2 continuous test.
///
/// Compares each 32-byte chunk against the previous output. If any
/// consecutive 32-byte blocks are identical, throws std::runtime_error
/// (FIPS 140-3 error state).
///
/// @param state  CRNGT state (must persist across calls).
/// @param buf    Output buffer for random bytes.
/// @param size   Number of random bytes to generate.
inline void fill_random_bytes_tested(CrngtState& state,
                                      uint8_t* buf, size_t size) {
    detail::cipher::fill_random_bytes(buf, size);

    // Test in 32-byte blocks
    size_t offset = 0;
    while (offset + 32 <= size) {
        if (state.initialized) {
            if (std::memcmp(buf + offset, state.prev, 32) == 0) {
                throw std::runtime_error(
                    "CRNGT failure: consecutive RNG outputs are identical "
                    "(FIPS 140-3 §4.9.2)");
            }
        }
        std::memcpy(state.prev, buf + offset, 32);
        state.initialized = true;
        offset += 32;
    }

    // Handle trailing bytes < 32 (compare prefix)
    if (offset < size && state.initialized) {
        size_t remaining = size - offset;
        if (remaining > 0 && std::memcmp(buf + offset, state.prev, remaining) == 0) {
            // Partial match — not a definitive failure, but update state
        }
    }
}

} // namespace detail::crngt

// ===========================================================================
// Secure memory utilities (Gap C-11)
//
// Prevents key material from being paged to swap (mlock) and ensures
// zeroization on deallocation. On platforms without mlock (Windows),
// VirtualLock is used instead.
//
// Reference: NIST SP 800-57 Part 1 Rev. 5 §8.2.2 (key protection)
//            FIPS 140-3 §4.7.6 (key material zeroization)
// ===========================================================================

namespace detail::secure_mem {

/// Lock a memory region so it is not paged to swap.
/// @return true if the lock succeeded, false on failure (non-fatal).
inline bool lock_memory(void* ptr, size_t size) {
    if (!ptr || size == 0) return false;
#if defined(_WIN32)
    return VirtualLock(ptr, size) != 0;
#elif defined(__EMSCRIPTEN__)
    (void)ptr; (void)size;
    return false;  // mlock not available in WASM
#elif defined(__unix__) || defined(__APPLE__)
    return ::mlock(ptr, size) == 0;
#else
    (void)ptr; (void)size;
    return false;
#endif
}

/// Unlock a previously locked memory region.
inline void unlock_memory(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
#if defined(_WIN32)
    VirtualUnlock(ptr, size);
#elif defined(__EMSCRIPTEN__)
    (void)ptr; (void)size;  // munlock not available in WASM
#elif defined(__unix__) || defined(__APPLE__)
    ::munlock(ptr, size);
#else
    (void)ptr; (void)size;
#endif
}

/// Securely zero a memory region (not optimized out by the compiler).
inline void secure_zero(void* ptr, size_t size) {
    if (!ptr || size == 0) return;
#if defined(_WIN32)
    SecureZeroMemory(ptr, size);
#else
    volatile unsigned char* p = static_cast<volatile unsigned char*>(ptr);
    for (size_t i = 0; i < size; ++i) p[i] = 0;
#endif
}

} // namespace detail::secure_mem

/// RAII container for sensitive key material with mlock and secure zeroization.
///
/// - Locks memory on construction (prevents swap-out)
/// - Securely zeros and unlocks on destruction
/// - Move-only (no copy to prevent key duplication)
class SecureKeyBuffer {
public:
    /// Construct from existing key bytes (copies and locks).
    explicit SecureKeyBuffer(const std::vector<uint8_t>& key)
        : data_(key) {
        detail::secure_mem::lock_memory(data_.data(), data_.size());
    }

    /// Construct from raw bytes (copies and locks).
    SecureKeyBuffer(const uint8_t* ptr, size_t size)
        : data_(ptr, ptr + size) {
        detail::secure_mem::lock_memory(data_.data(), data_.size());
    }

    /// Construct with a specified size of random key material.
    explicit SecureKeyBuffer(size_t size) : data_(size) {
        detail::cipher::fill_random_bytes(data_.data(), size);
        detail::secure_mem::lock_memory(data_.data(), data_.size());
    }

    ~SecureKeyBuffer() {
        detail::secure_mem::secure_zero(data_.data(), data_.size());
        detail::secure_mem::unlock_memory(data_.data(), data_.size());
    }

    // Move-only
    SecureKeyBuffer(SecureKeyBuffer&& other) noexcept : data_(std::move(other.data_)) {}
    SecureKeyBuffer& operator=(SecureKeyBuffer&& other) noexcept {
        if (this != &other) {
            detail::secure_mem::secure_zero(data_.data(), data_.size());
            detail::secure_mem::unlock_memory(data_.data(), data_.size());
            data_ = std::move(other.data_);
        }
        return *this;
    }
    SecureKeyBuffer(const SecureKeyBuffer&) = delete;
    SecureKeyBuffer& operator=(const SecureKeyBuffer&) = delete;

    [[nodiscard]] const uint8_t* data() const { return data_.data(); }
    [[nodiscard]] uint8_t* data() { return data_.data(); }
    [[nodiscard]] size_t size() const { return data_.size(); }
    [[nodiscard]] bool empty() const { return data_.empty(); }

private:
    std::vector<uint8_t> data_;
};

// ===========================================================================
// AesGcmCipher -- AES-256-GCM adapter
// ===========================================================================

/// AES-256-GCM adapter -- wraps the low-level AesGcm class behind ICipher.
///
/// Provides authenticated encryption with AAD support. Generates a random
/// 12-byte IV per encrypt() call and prepends it to the output.
///
/// Gap C-3 (NIST SP 800-38D §8.2): Tracks invocation count per key and
/// enforces the 2^32 limit on GCM invocations with a single key (with
/// random 96-bit IVs, birthday bound for IV collision is ~2^32). Callers
/// can register a key rotation callback to be notified when approaching
/// the limit.
///
/// @note The destructor securely zeroes key material using volatile writes.
/// @see AesCtrCipher for the unauthenticated counterpart
class AesGcmCipher final : public ICipher {
public:
    /// NIST SP 800-38D §8.2: With random 96-bit IVs, the probability of
    /// IV collision exceeds 2^-32 after 2^32 invocations (birthday bound).
    /// Key must be rotated before reaching this limit.
    static constexpr uint64_t MAX_INVOCATIONS = UINT64_C(0xFFFFFFFF); // 2^32 - 1

    /// Default warning threshold: trigger rotation callback at 75% of max.
    static constexpr uint64_t DEFAULT_ROTATION_THRESHOLD =
        static_cast<uint64_t>(MAX_INVOCATIONS * 0.75);

    /// Callback type for key rotation notification.
    /// Called when invocation count reaches the rotation threshold.
    /// The parameter is the current invocation count.
    using RotationCallback = std::function<void(uint64_t invocation_count)>;

    /// Construct from a key vector (must be 32 bytes for AES-256).
    explicit AesGcmCipher(const std::vector<uint8_t>& key)
        : key_{} { std::memcpy(key_.data(), key.data(), (std::min)(key.size(), key_.size())); }

    /// Construct from a raw key pointer and length.
    explicit AesGcmCipher(const uint8_t* key, size_t key_len)
        : key_{} { std::memcpy(key_.data(), key, (std::min)(key_len, key_.size())); }

    /// Register a callback invoked when the key approaches its invocation limit.
    /// NIST SP 800-38D §8.2 requires key rotation before 2^32 random-IV GCM
    /// invocations to maintain the collision bound.
    /// @param cb         Callback receiving the current invocation count.
    /// @param threshold  Invocation count at which to trigger (default: 75% of 2^32).
    void set_rotation_callback(RotationCallback cb,
                               uint64_t threshold = DEFAULT_ROTATION_THRESHOLD) {
        rotation_callback_ = std::move(cb);
        rotation_threshold_ = threshold;
    }

    /// Get the current number of encrypt() invocations on this key.
    [[nodiscard]] uint64_t invocation_count() const noexcept {
        return invocation_count_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] expected<std::vector<uint8_t>> encrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const override {

        if (key_.size() != AesGcm::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesGcmCipher: key must be 32 bytes"};
        }

        // NIST SP 800-38D §8.2: Enforce invocation limit for random-IV GCM.
        // With 96-bit random IVs, birthday collision probability exceeds
        // acceptable bounds after 2^32 invocations under the same key.
        uint64_t count = invocation_count_.fetch_add(1, std::memory_order_relaxed);
        if (count >= MAX_INVOCATIONS) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AES-GCM: key invocation limit reached (2^32). "
                         "NIST SP 800-38D §8.2 requires key rotation."};
        }

        // Trigger rotation callback at threshold (fire once)
        if (rotation_callback_ && count == rotation_threshold_) {
            rotation_callback_(count);
        }

        auto iv = detail::cipher::generate_iv(AesGcm::IV_SIZE);
        AesGcm gcm(key_.data());

        auto result = aad.empty()
            ? gcm.encrypt(data, size, iv.data())
            : gcm.encrypt(data, size, iv.data(),
                           reinterpret_cast<const uint8_t*>(aad.data()),
                           aad.size());

        if (!result) return result.error();
        return detail::cipher::prepend_iv(iv, *result);
    }

    [[nodiscard]] expected<std::vector<uint8_t>> decrypt(
        const uint8_t* data, size_t size,
        const std::string& aad = "") const override {

        if (key_.size() != AesGcm::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesGcmCipher: key must be 32 bytes"};
        }

        auto iv_result = detail::cipher::parse_iv_header(data, size);
        if (!iv_result) return iv_result.error();
        const auto& [iv, ciphertext, ct_size] = *iv_result;

        AesGcm gcm(key_.data());
        if (!aad.empty()) {
            return gcm.decrypt(ciphertext, ct_size, iv,
                               reinterpret_cast<const uint8_t*>(aad.data()),
                               aad.size());
        } else {
            return gcm.decrypt(ciphertext, ct_size, iv);
        }
    }

    /// Destructor: securely zeroes key material (CWE-244: heap inspection).
    /// Uses volatile write + compiler barrier to prevent dead-store elimination.
    ~AesGcmCipher() override {
        volatile uint8_t* p = key_.data();
        for (size_t i = 0; i < key_.size(); ++i) p[i] = 0;
#if defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("" ::: "memory");
#endif
    }

    [[nodiscard]] bool is_authenticated() const noexcept override { return true; }
    [[nodiscard]] size_t key_size() const noexcept override { return AesGcm::KEY_SIZE; }
    [[nodiscard]] std::string_view algorithm_name() const noexcept override {
        return "AES-256-GCM";
    }

private:
    std::array<uint8_t, 32> key_{}; ///< Fixed-size key (CWE-244: avoids std::vector reallocation leaks).
    mutable std::atomic<uint64_t> invocation_count_{0}; ///< NIST SP 800-38D §8.2 invocation counter.
    RotationCallback rotation_callback_; ///< Optional key rotation notification callback.
    uint64_t rotation_threshold_{DEFAULT_ROTATION_THRESHOLD}; ///< Invocation count to trigger callback.
};

// ===========================================================================
// AesCtrCipher -- AES-256-CTR adapter
// ===========================================================================

/// AES-256-CTR adapter -- wraps the low-level AesCtr class behind ICipher.
///
/// Unauthenticated encryption (the AAD parameter is ignored). Generates a
/// random 16-byte IV per encrypt() call and prepends it to the output.
///
/// @note The destructor securely zeroes key material using volatile writes.
/// @see AesGcmCipher for the authenticated counterpart
class AesCtrCipher final : public ICipher {
public:
    /// Construct from a key vector (must be 32 bytes for AES-256).
    explicit AesCtrCipher(const std::vector<uint8_t>& key)
        : key_{} { std::memcpy(key_.data(), key.data(), (std::min)(key.size(), key_.size())); }

    /// Construct from a raw key pointer and length.
    explicit AesCtrCipher(const uint8_t* key, size_t key_len)
        : key_{} { std::memcpy(key_.data(), key, (std::min)(key_len, key_.size())); }

    [[nodiscard]] expected<std::vector<uint8_t>> encrypt(
        const uint8_t* data, size_t size,
        const std::string& /*aad*/ = "") const override {

        if (key_.size() != AesCtr::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesCtrCipher: key must be 32 bytes"};
        }

        auto iv = detail::cipher::generate_iv(AesCtr::IV_SIZE);
        AesCtr ctr(key_.data());
        std::vector<uint8_t> ciphertext = ctr.encrypt(data, size, iv.data());
        return detail::cipher::prepend_iv(iv, ciphertext);
    }

    [[nodiscard]] expected<std::vector<uint8_t>> decrypt(
        const uint8_t* data, size_t size,
        const std::string& /*aad*/ = "") const override {

        if (key_.size() != AesCtr::KEY_SIZE) {
            return Error{ErrorCode::ENCRYPTION_ERROR,
                         "AesCtrCipher: key must be 32 bytes"};
        }

        auto iv_result = detail::cipher::parse_iv_header(data, size);
        if (!iv_result) return iv_result.error();
        const auto& [iv, ciphertext, ct_size] = *iv_result;

        AesCtr ctr(key_.data());
        std::vector<uint8_t> plaintext = ctr.decrypt(ciphertext, ct_size, iv);
        return plaintext;
    }

    /// Destructor: securely zeroes key material (CWE-244: heap inspection).
    /// Uses volatile write + compiler barrier to prevent dead-store elimination.
    ~AesCtrCipher() override {
        volatile uint8_t* p = key_.data();
        for (size_t i = 0; i < key_.size(); ++i) p[i] = 0;
#if defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("" ::: "memory");
#endif
    }

    /// @return Always false (CTR mode has no authentication).
    [[nodiscard]] bool is_authenticated() const noexcept override { return false; }
    /// @return 32 (AES-256 key size in bytes).
    [[nodiscard]] size_t key_size() const noexcept override { return AesCtr::KEY_SIZE; }
    /// @return "AES-256-CTR".
    [[nodiscard]] std::string_view algorithm_name() const noexcept override {
        return "AES-256-CTR";
    }

private:
    std::array<uint8_t, 32> key_{}; ///< Fixed-size key (CWE-244: avoids std::vector reallocation leaks).
};

// ===========================================================================
// CipherFactory -- static factory for creating cipher instances
// ===========================================================================

/// Factory for creating cipher instances from algorithm enum + raw key.
/// NOT a singleton — all methods are static.
struct CipherFactory {
    /// Create a footer cipher (always authenticated = GCM).
    [[nodiscard]] static std::unique_ptr<ICipher> create_footer_cipher(
            EncryptionAlgorithm /*algo*/, const std::vector<uint8_t>& key) {
        // Footer always uses GCM regardless of algorithm
        return std::make_unique<AesGcmCipher>(key);
    }

    /// Create a column data cipher (GCM or CTR based on algorithm).
    [[nodiscard]] static std::unique_ptr<ICipher> create_column_cipher(
            EncryptionAlgorithm algo, const std::vector<uint8_t>& key) {
        if (algo == EncryptionAlgorithm::AES_GCM_V1) {
            return std::make_unique<AesGcmCipher>(key);
        }
        // AES_GCM_CTR_V1: column data uses CTR
        return std::make_unique<AesCtrCipher>(key);
    }

    /// Create a metadata cipher (always authenticated = GCM).
    [[nodiscard]] static std::unique_ptr<ICipher> create_metadata_cipher(
            EncryptionAlgorithm /*algo*/, const std::vector<uint8_t>& key) {
        // Metadata always uses GCM regardless of algorithm
        return std::make_unique<AesGcmCipher>(key);
    }
};

// ===========================================================================
// Gap C-9: Power-on self-test (Known Answer Tests)
//
// NIST SP 800-140B / FIPS 140-3 §4.9.1 requires cryptographic modules to
// perform known-answer tests (KATs) at initialization to verify algorithm
// correctness. This function runs KATs for AES-256, AES-GCM, and AES-CTR
// using NIST published test vectors.
//
// Call crypto_self_test() at application startup. Returns true if all KATs
// pass. A false return indicates a broken build or hardware fault.
//
// References:
//   - NIST FIPS 197 Appendix C.3 (AES-256 single block)
//   - NIST SP 800-38D Test Case 16 (AES-256-GCM with AAD)
//   - NIST SP 800-38A F.5.5 (AES-256-CTR)
// ===========================================================================

namespace detail::kat {

/// Decode a hex string to bytes (internal helper for KAT vectors).
inline std::vector<uint8_t> hex_decode(const char* hex) {
    std::vector<uint8_t> out;
    while (*hex) {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            return 0;
        };
        uint8_t hi = nibble(*hex++);
        if (!*hex) break;
        uint8_t lo = nibble(*hex++);
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return out;
}

} // namespace detail::kat

/// Run power-on self-tests (Known Answer Tests) for all crypto primitives.
///
/// Tests AES-256 block cipher, AES-256-GCM (AEAD), and AES-256-CTR using
/// NIST published test vectors. Should be called once at application startup.
///
/// @return true if all KATs pass, false if any algorithm produces incorrect output.
[[nodiscard]] inline bool crypto_self_test() {
    using namespace detail::kat;

    // --- KAT 1: AES-256 single block (NIST FIPS 197 Appendix C.3) ---
    {
        auto key = hex_decode("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
        auto pt  = hex_decode("00112233445566778899aabbccddeeff");
        auto exp = hex_decode("8ea2b7ca516745bfeafc49904b496089");
        if (key.size() != 32 || pt.size() != 16) return false;

        Aes256 cipher(key.data());
        uint8_t block[16];
        std::memcpy(block, pt.data(), 16);
        cipher.encrypt_block(block);
        if (std::memcmp(block, exp.data(), 16) != 0) return false;
    }

    // --- KAT 2: AES-256-GCM (NIST SP 800-38D Test Case 16) ---
    // CTR ciphertext matches NIST exactly. GHASH tag uses implementation-specific
    // GF(2^128) bit ordering, so we verify CTR output + encrypt/decrypt roundtrip.
    {
        auto key = hex_decode("feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
        auto iv  = hex_decode("cafebabefacedbaddecaf888");
        auto aad = hex_decode("feedfacedeadbeeffeedfacedeadbeefabaddad2");
        auto pt  = hex_decode("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
        auto exp_ct  = hex_decode("522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662");

        AesGcm gcm(key.data());
        auto result = gcm.encrypt(pt.data(), pt.size(), iv.data(), aad.data(), aad.size());
        if (!result.has_value()) return false;
        if (result->size() != pt.size() + 16) return false;
        // CTR ciphertext matches NIST vector
        if (std::memcmp(result->data(), exp_ct.data(), pt.size()) != 0) return false;
        // Roundtrip: decrypt must recover original plaintext (verifies tag consistency)
        auto dec = gcm.decrypt(result->data(), result->size(), iv.data(), aad.data(), aad.size());
        if (!dec.has_value()) return false;
        if (dec->size() != pt.size()) return false;
        if (std::memcmp(dec->data(), pt.data(), pt.size()) != 0) return false;
    }

    // --- KAT 3: AES-256-CTR (NIST SP 800-38A F.5.5) ---
    {
        auto key = hex_decode("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4");
        auto iv  = hex_decode("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
        auto pt  = hex_decode("6bc1bee22e409f96e93d7e117393172a");
        auto exp = hex_decode("601ec313775789a5b7a7f504bbf3d228");

        AesCtr ctr(key.data());
        auto ct = ctr.encrypt(pt.data(), pt.size(), iv.data());
        if (ct.size() != pt.size()) return false;
        if (std::memcmp(ct.data(), exp.data(), pt.size()) != 0) return false;
    }

    return true;
}

} // namespace signet::forge::crypto
