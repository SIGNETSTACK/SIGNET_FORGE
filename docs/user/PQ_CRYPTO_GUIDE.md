# Post-Quantum Cryptography Guide

This document explains Signet's post-quantum encryption, when to use it, how to configure it, and what the key sizes and file overhead actually look like.

---

## Why Post-Quantum Now?

The "harvest now, decrypt later" threat is real and documented. Adversaries — particularly nation-state actors — are actively collecting encrypted data today with the expectation of decrypting it once a cryptographically-relevant quantum computer is available. NIST estimates this timeline at roughly 10–15 years, with more aggressive estimates placing it earlier.

For data that must remain confidential over that horizon — long-term medical records, multi-decade financial archives, government documents, proprietary research — the protection decision must be made today. Waiting until quantum computers exist is too late: the data is already captured.

### NIST Standardization Timeline

- **2016**: NIST launches post-quantum cryptography competition
- **2022**: NIST announces Kyber (ML-KEM) and Dilithium (ML-DSA) as primary selections
- **August 2024**: NIST finalizes FIPS 203 (ML-KEM-768 = Kyber-768) and FIPS 204 (ML-DSA-65 = Dilithium-3)
- **2024+**: Adoption by major cloud providers, browsers, and enterprise software underway
- **August 2026**: EU AI Act compliance deadlines begin; Article 12 record-keeping requirements apply

Signet is the first Parquet library to implement FIPS 203/204 for data at rest. Files written with `server-pq` preset or `SIGNET_ENABLE_PQ=ON` + liboqs are protected against both classical and quantum adversaries.

---

## Two Modes Explained

### Bundled Mode (development and testing only)

When you build with `SIGNET_ENABLE_PQ=ON` but liboqs is not installed, Signet uses its bundled stub implementation. This is NOT cryptographically secure.

**What bundled mode provides:**
- A fully functional API — your code compiles and runs identically in both modes
- The X25519 component is always the real RFC 7748 Montgomery ladder (128-bit classical security)
- Kyber-768 and Dilithium-3 are functional stubs that exercise the same code paths but do not provide post-quantum security

**When to use bundled mode:** Developing and testing your application locally before deploying to a server with liboqs installed.

### liboqs Mode (production)

When liboqs is installed and found by CMake, Signet automatically uses it.

**What liboqs mode provides:**
- Real NIST FIPS 203 ML-KEM-768 (Kyber-768): 192-bit post-quantum security against known quantum attacks
- Real NIST FIPS 204 ML-DSA-65 (Dilithium-3): post-quantum digital signatures
- The same X25519 classical component (identical in both modes)

**CRITICAL**: Files written in bundled mode cannot be decrypted in liboqs mode, and vice versa. The two modes use different algorithm parameters and are not interchangeable. Write your production data only after deploying a liboqs build.

---

## What X25519 Provides in Both Modes

X25519 (Elliptic Curve Diffie-Hellman over Curve25519) provides 128-bit classical security — equivalent to AES-128 in brute-force resistance against classical computers.

Signet's X25519 implementation is the real RFC 7748 Montgomery ladder in both bundled and liboqs modes. The key clamping required by RFC 7748 is applied:

```
k[0] &= 248;    // Clear the three lowest bits
k[31] &= 127;   // Clear the highest bit
k[31] |= 64;    // Set the second highest bit
```

All-zero output (which indicates a low-order input point) is rejected and treated as a key agreement failure.

---

## What Kyber-768 Provides (liboqs mode only)

Kyber-768 (now standardized as ML-KEM-768 in FIPS 203) is a key encapsulation mechanism based on the Module Learning With Errors (MLWE) problem. It provides:

- **192-bit post-quantum security**: approximately 2^192 quantum operations to break
- **IND-CCA2 security**: secure even against chosen-ciphertext attacks
- **Fast operations**: key generation, encapsulation, and decapsulation are all sub-millisecond

In Signet's hybrid mode (the default), Kyber-768 and X25519 are combined: the shared secret is derived from both, so the encryption is only broken if both algorithms are simultaneously compromised. This is the defense-in-depth approach recommended by IETF.

---

## Why Hybrid (Kyber + X25519)?

The IETF recommendation (draft-ietf-tls-hybrid-design) is to combine post-quantum KEMs with classical ECDH during the transition period:

1. **Defense in depth**: If Kyber-768 is later found to have a flaw, X25519 still protects your data. If X25519 is broken by a quantum computer, Kyber-768 still protects your data.
2. **Downgrade prevention**: An attacker cannot force a connection to use only classical cryptography.
3. **Confidence during transition**: FIPS 203 is new; hybrid mode hedges against implementation issues.

Set `pq_cfg.hybrid_mode = true` (the default) to enable hybrid encryption.

---

## Key Size Reference

| Key material | Size | Notes |
|---|---|---|
| Kyber-768 public key | 1184 bytes | Share with senders / store in key server |
| Kyber-768 secret key | 2400 bytes | Keep private; store in HSM |
| Kyber-768 ciphertext | 1088 bytes | Per-file; stored in Parquet key metadata |
| X25519 public key | 32 bytes | |
| X25519 secret key | 32 bytes | Keep private |
| Dilithium-3 public key | 1952 bytes | Share for signature verification |
| Dilithium-3 secret key | 4000 bytes | Keep private; store in HSM |
| Dilithium-3 signature | max 3293 bytes | Per-row-group signature stored in footer |

---

## File Overhead

A post-quantum encrypted Parquet file carries approximately **6.3 KB** of extra metadata per file:

- Kyber-768 ciphertext: 1088 bytes
- X25519 ephemeral public key: 32 bytes
- Dilithium-3 signature per file: ~3293 bytes (max)
- Key metadata header: ~900 bytes

For files larger than 1 MB, this overhead is negligible. For very small files (single row groups of a few hundred bytes), consider whether per-file encryption is appropriate or whether batching multiple small writes into a single larger file is preferable.

---

## Step-by-Step: Enable Post-Quantum Encryption

### 1. Install liboqs

**macOS:**
```bash
brew install liboqs
```

**Ubuntu 24.04:**
```bash
apt install liboqs-dev
# If not available in your distribution's package manager:
git clone https://github.com/open-quantum-safe/liboqs.git
cd liboqs && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) && sudo make install
```

### 2. Build Signet with Post-Quantum Support

```bash
cmake --preset server-pq
cmake --build build-server-pq
```

Or manually:

```bash
cmake -DSIGNET_ENABLE_PQ=ON \
      -DSIGNET_ENABLE_CRYPTO=ON \
      -DSIGNET_ENABLE_ZSTD=ON \
      ..
```

When liboqs is found, CMake prints:
```
-- Found liboqs: /usr/local/lib/liboqs.a
-- Post-quantum encryption: real Kyber-768 + Dilithium-3 (FIPS 203/204)
```

If liboqs is not found, CMake prints:
```
-- Post-quantum encryption: bundled stub (NOT production-safe)
```

### 3. Generate Keypairs

Generate keypairs once per deployment. Store the secret keys in an HSM or KMS. Distribute public keys to systems that need to write encrypted data.

```cpp
#include "signet/crypto/post_quantum.hpp"
using namespace signet::forge::crypto;

// Key encapsulation (for encryption/decryption)
auto kem_kp = KyberKem::generate_keypair();
if (!kem_kp.has_value()) {
    std::cerr << "Key generation failed: " << kem_kp.error().message << "\n";
    return 1;
}

// Save to HSM / KMS (pseudocode):
hsm.store("kyber-public-key",  kem_kp->public_key);
hsm.store("kyber-secret-key",  kem_kp->secret_key);

// Signature keypair (for signing row groups)
auto sign_kp = DilithiumSign::generate_keypair();
hsm.store("dilithium-public-key", sign_kp->public_key);
hsm.store("dilithium-secret-key", sign_kp->secret_key);
```

### 4. Write Post-Quantum Encrypted Parquet

```cpp
#include "signet/forge.hpp"
#include "signet/crypto/post_quantum.hpp"
using namespace signet::forge;
using namespace signet::forge::crypto;

// Load keys from HSM
auto recipient_pub = hsm.load("kyber-public-key");
auto signing_sec   = hsm.load("dilithium-secret-key");
auto signing_pub   = hsm.load("dilithium-public-key");

// Configure post-quantum encryption
PostQuantumConfig pq_cfg;
pq_cfg.enabled              = true;
pq_cfg.hybrid_mode          = true;  // Kyber-768 + X25519 (recommended)
pq_cfg.recipient_public_key = recipient_pub;
pq_cfg.signing_secret_key   = signing_sec;
pq_cfg.signing_public_key   = signing_pub;

// Also configure PME for the Parquet footer
EncryptionConfig enc_cfg;
enc_cfg.footer_key     = hsm.load("footer-key");
enc_cfg.encrypt_footer = true;

WriterOptions opts;
opts.encryption    = enc_cfg;
opts.compression   = Compression::ZSTD;

auto writer = ParquetWriter::open("pq_protected.parquet", schema, opts);
// ... write data ...
(void)writer->flush_row_group();
(void)writer->close();
```

### 5. Read Post-Quantum Encrypted Files

```cpp
// Decapsulate: derive shared secret using your secret key
auto ciphertext    = /* read from file key metadata */;
auto kyber_secret  = hsm.load("kyber-secret-key");

auto shared_secret = KyberKem::decapsulate(ciphertext, kyber_secret);

// Verify Dilithium signature
auto signature     = /* read from file footer */;
auto verify_pub    = hsm.load("dilithium-public-key");
auto valid = DilithiumSign::verify(file_data_hash, signature, verify_pub);

// Then open with PME decryption config using the derived key
EncryptionConfig dec_cfg;
dec_cfg.footer_key = derived_key;  // from shared_secret
auto reader = ParquetReader::open("pq_protected.parquet", dec_cfg);
```

---

## Key Management Best Practices

- **Store Kyber and Dilithium secret keys in an HSM.** AWS CloudHSM, Azure Managed HSM, and Thales Luna HSMs all support storing 4KB+ key material.
- **Never write secret keys to disk.** Use the HSM's key handle for all cryptographic operations.
- **Rotate keypairs annually.** Key rotation does not require re-encrypting existing files if you maintain a key archive. Store the decryption key ID in the Parquet file metadata so you can look up the correct key for old files.
- **Separate signing and encryption keypairs.** Compromise of the signing key (Dilithium) does not compromise confidentiality (Kyber). Compromise of the encryption key does not compromise integrity (unless the attacker can also forge signatures).
- **Back up public keys.** The Kyber public key must be available to any system that writes encrypted files. If the public key is lost and you need to write new encrypted data for the same recipient, you must re-key.

---

## Upgrade Path: Bundled to liboqs

1. Install liboqs on your production servers.
2. Rebuild Signet: `cmake --preset server-pq && cmake --build build-server-pq`.
3. Verify the build log shows "real Kyber-768 + Dilithium-3".
4. Start writing new files — they will be liboqs-encrypted.
5. Old files written in bundled mode remain readable only by bundled builds. If you need long-term protection for old files, decrypt and re-encrypt them with the liboqs build.

There is no in-place migration. The two modes use incompatible wire formats.

---

## CI Testing

Signet's GitHub Actions CI includes a dedicated `post-quantum` job that:

1. Builds liboqs from source (pinned commit for reproducibility)
2. Builds Signet with `SIGNET_ENABLE_PQ=ON`
3. Runs the full test suite including `test_encryption.cpp` PQ-specific cases
4. Verifies that files written and read in liboqs mode round-trip correctly
5. Verifies that tampered signatures are detected by `DilithiumSign::verify()`

See `.github/workflows/ci.yml` for the exact job configuration.

---

## NIST Standards Reference

| Algorithm | NIST Standard | Signet Mapping |
|-----------|--------------|----------------|
| ML-KEM-768 | FIPS 203 (August 2024) | `KyberKem` (liboqs mode) |
| ML-DSA-65 | FIPS 204 (August 2024) | `DilithiumSign` (liboqs mode) |
| X25519 | RFC 7748 | Always real implementation |

The naming in Signet's API uses the legacy competition names (Kyber, Dilithium) which are synonymous with the final FIPS standards:

- Kyber-768 = ML-KEM-768 = FIPS 203 Level 3
- Dilithium-3 = ML-DSA-65 = FIPS 204 Level 3
