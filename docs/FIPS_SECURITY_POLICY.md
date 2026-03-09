# Signet Forge Cryptographic Security Policy

**Document**: Operator Guidance & Security Policy per FIPS 140-3 Section 7.1
**Version**: 1.0
**Date**: 2026-03-09
**Author**: Johnson Ogundeji

---

## 1. Scope

This document provides operator guidance for the cryptographic module embedded in
Signet Forge, a C++20 header-only Parquet library. It covers:

- Approved algorithms and their operational parameters
- Key management requirements
- Operational environment constraints
- Security boundaries and limitations

This policy applies to all deployments using `SIGNET_ENABLE_CRYPTO=ON` or
`SIGNET_ENABLE_PQ=ON`.

---

## 2. Module Identification

| Property | Value |
|----------|-------|
| Module name | Signet Forge Cryptographic Module |
| Version | 0.1.x |
| Type | Software module (header-only C++20) |
| Security level target | Informational (not CMVP-validated) |
| Operational environment | General Purpose Computer (GPC) |

**CMVP status**: This module has NOT undergone CMVP validation. Organizations
requiring FIPS 140-3 validated cryptography MUST use an external validated
provider (e.g., OpenSSL FIPS module) or await future CMVP submission.

---

## 3. Approved Algorithms

### 3.1 Symmetric Encryption

| Algorithm | Standard | Key Size | Mode | Implementation |
|-----------|----------|----------|------|----------------|
| AES-256-GCM | NIST SP 800-38D | 256-bit only | Authenticated encryption | `aes_gcm.hpp` |
| AES-256-CTR | NIST SP 800-38A | 256-bit only | Stream cipher | `aes_ctr.hpp` |

**Design decision**: AES-128 and AES-192 are NOT supported. The module enforces
AES-256 exclusively per NIST SP 800-131A Rev.2 Section 4, which recommends
256-bit keys for long-term security. See Gap C-14 documentation.

**AES-GCM constraints** (NIST SP 800-38D):
- IV/nonce: 12 bytes (96-bit), MUST be unique per key
- Maximum plaintext per invocation: ~64 GiB (2^32 blocks x 16 bytes)
- Tag length: 128 bits (full-length, no truncation below 96 bits permitted)
- An invocation counter tracks GCM operations per key and rejects operations
  if the counter would overflow, preventing IV reuse

### 3.2 Key Derivation

| Algorithm | Standard | Implementation |
|-----------|----------|----------------|
| HKDF-SHA256 | RFC 5869 | `hkdf.hpp` |

HKDF is used for deriving column encryption keys (DEKs) from key encryption
keys (KEKs) in the Parquet Modular Encryption (PME) layer.

### 3.3 Hash Functions

| Algorithm | Standard | Usage |
|-----------|----------|-------|
| SHA-256 | FIPS 180-4 | Audit chain integrity, HMAC-SHA256 footer signing |

### 3.4 Key Agreement

| Algorithm | Standard | Implementation |
|-----------|----------|----------------|
| X25519 | RFC 7748 | `post_quantum.hpp` (constant-time Montgomery ladder) |

The X25519 implementation uses:
- Constant-time field arithmetic (no data-dependent branching)
- `fe_cswap` for conditional swaps (arithmetic masking, no branches)
- `fe_inv` via fixed addition chain (branch-free, CWE-208 compliant)

### 3.5 Post-Quantum Algorithms

| Algorithm | Standard | Implementation |
|-----------|----------|----------------|
| ML-KEM-768 (Kyber) | FIPS 203 | Via liboqs (external) |
| ML-DSA-65 (Dilithium) | FIPS 204 | Via liboqs (external) |
| Hybrid KEM (X25519 + ML-KEM) | NIST SP 800-227 | `post_quantum.hpp` |

**Requirement**: Real PQ operations require `SIGNET_ENABLE_PQ=ON` and liboqs
installed. Without liboqs, bundled stubs provide API compatibility only and
emit a runtime warning via `is_real_pq_crypto()`.

---

## 4. Key Management

### 4.1 Key Storage

- Encryption keys are stored in `SecureKeyBuffer` containers that use
  platform `mlock()` / `VirtualLock()` to prevent paging to swap
- Key material is zeroed via volatile writes + compiler barriers on destruction
- Cipher objects (`Aes256`, `AesGcm`, `AesCtr`) are non-copyable; round keys
  are zeroed in destructors

### 4.2 Key Sizes

| Key Type | Required Size | Enforcement |
|----------|--------------|-------------|
| Footer encryption key | 32 bytes | Runtime validation in PME |
| Column encryption key | 32 bytes | Runtime validation in PME |
| HMAC signing key | 32 bytes | Derived via HKDF |
| X25519 private key | 32 bytes | Clamped per RFC 7748 Section 5 |

### 4.3 Key Rotation

- `KeyRotationManager` (Gap T-7) provides programmatic key rotation with
  configurable rotation intervals
- `AlgorithmDeprecation` registry (Gap C-4) tracks algorithm lifecycle and
  emits warnings when approaching deprecation dates
- `INTERNAL` key mode is rejected in production builds (`SIGNET_REQUIRE_COMMERCIAL_LICENSE=ON`)
  per FIPS 140-3 Section 7.7

### 4.4 Crypto-Shredding

- `CryptoShredder` (Gap G-1) supports GDPR Art. 17 right-to-erasure by
  securely destroying encryption keys, rendering associated data unrecoverable

---

## 5. Random Number Generation

| Platform | Source | Standard |
|----------|--------|----------|
| Linux | `getrandom(2)` with `EINTR` retry | NIST SP 800-90B |
| macOS | `arc4random_buf(3)` | ChaCha20-based CSPRNG |
| Windows | `BCryptGenRandom` with status validation | FIPS 140-2 validated |
| WASM | Not available (no mlock; IV generation falls through to platform) | N/A |

All cryptographic nonces and IVs are generated via the platform CSPRNG.
The legacy `std::random_device` path has been removed.

**CRNGT**: A Continuous Random Number Generator Test (FIPS 140-3 Section 4.9.2)
is implemented to detect catastrophic CSPRNG failure by comparing consecutive
outputs (Gap C-13).

---

## 6. Operational Environment

### 6.1 Supported Platforms

| Platform | Compiler | Notes |
|----------|----------|-------|
| Linux x86_64 | GCC 13+, Clang 18+ | Full support including sanitizers |
| macOS arm64 | Apple Clang 17+ | Full support |
| Windows x86_64 | MSVC 2022+ | Core + crypto; no sanitizers |
| WASM (Emscripten) | emcc | Core only; mlock unavailable |

### 6.2 Build Configuration

**Production builds** MUST use:
```
-DSIGNET_ENABLE_CRYPTO=ON
-DSIGNET_REQUIRE_COMMERCIAL_LICENSE=ON
-DSIGNET_REQUIRE_REAL_PQ=ON     # if PQ features are needed
```

**Development/testing** builds may use:
```
-DSIGNET_ENABLE_COMMERCIAL=ON
-DSIGNET_REQUIRE_COMMERCIAL_LICENSE=OFF   # default — no hash needed
```

### 6.3 Security-Relevant Build Flags

| Flag | Default | Effect |
|------|---------|--------|
| `SIGNET_ENABLE_CRYPTO` | OFF | Enables real AES-256-GCM/CTR |
| `SIGNET_ENABLE_PQ` | OFF | Enables real ML-KEM-768 / ML-DSA-65 via liboqs |
| `SIGNET_REQUIRE_REAL_PQ` | OFF | Rejects bundled PQ stubs at build time |
| `SIGNET_REQUIRE_COMMERCIAL_LICENSE` | OFF | Enforces license hash + blocks INTERNAL key mode |

---

## 7. Known Limitations

### 7.1 Side-Channel Mitigations

| Mitigation | Status | Notes |
|------------|--------|-------|
| AES T-table prefetch | Implemented | Reduces cache-timing leakage; insufficient for SMT (Gap C-5) |
| Constant-time GHASH | Implemented | 4-bit table lookup, no branching |
| Constant-time X25519 fe_inv | Implemented | Branch-free addition chain (Gap C-10) |
| Constant-time fe_cswap | Implemented | Arithmetic masking |
| AES-NI hardware acceleration | Not yet | Would eliminate T-table side channel entirely |

**Recommendation**: For environments with SMT (Simultaneous Multi-Threading)
and untrusted co-tenants, consider deploying on hardware with AES-NI support.
A future release will add AES-NI intrinsics (Gap C-5).

### 7.2 Not a CMVP-Validated Module

This software module provides cryptographic functionality but has NOT been
submitted for CMVP (Cryptographic Module Validation Program) validation under
FIPS 140-3. Organizations subject to:

- US Federal (FISMA, FedRAMP)
- Financial services (PCI-DSS requiring FIPS)
- Healthcare (HIPAA requiring encryption)

...should use Signet Forge's KMS client interface (Gap P-5) to delegate
cryptographic operations to an externally validated module (e.g., OpenSSL
FIPS provider, AWS CloudHSM, Azure Key Vault HSM).

---

## 8. Incident Response

If a cryptographic vulnerability is discovered:

1. Report via the process in SECURITY.md (email: johnson@signetstack.io)
2. Do NOT open a public GitHub issue
3. 90-day coordinated disclosure policy applies
4. Critical crypto fixes are released within 30 days of triage

---

## 9. References

| Standard | Title |
|----------|-------|
| FIPS 140-3 | Security Requirements for Cryptographic Modules |
| FIPS 197 | Advanced Encryption Standard (AES) |
| FIPS 180-4 | Secure Hash Standard (SHA) |
| FIPS 203 | Module-Lattice-Based Key-Encapsulation Mechanism (ML-KEM) |
| FIPS 204 | Module-Lattice-Based Digital Signature (ML-DSA) |
| NIST SP 800-38A | Recommendation for Block Cipher Modes — CTR |
| NIST SP 800-38D | Recommendation for Block Cipher Modes — GCM |
| NIST SP 800-131A Rev.2 | Transitioning the Use of Cryptographic Algorithms |
| NIST SP 800-227 | Recommendations for Key-Encapsulation Mechanisms |
| RFC 5869 | HMAC-based Extract-and-Expand Key Derivation Function (HKDF) |
| RFC 7748 | Elliptic Curves for Security (X25519) |
