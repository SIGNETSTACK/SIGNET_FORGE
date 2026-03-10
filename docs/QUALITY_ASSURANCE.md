# Quality Assurance & Security

Signet Forge maintains rigorous quality assurance across every layer of the library,
validated against published cryptographic standards, regulatory frameworks, and
independent third-party test vector suites.

## Verification Infrastructure

| Layer | Coverage |
|-------|---------|
| **Unit Tests** | 618 tests across all features (C++), 35 Python tests, 10 Rust tests |
| **Security Hardening** | 10 dedicated audit passes — 458 vulnerabilities identified and fixed, zero open findings |
| **Compliance Gaps** | 92 of 92 enterprise compliance gaps resolved across 12 passes (FIPS, EU AI Act, MiFID II, GDPR, DORA, PME) |
| **Sanitizer CI** | AddressSanitizer, ThreadSanitizer, UndefinedBehaviorSanitizer on every push |
| **SAST** | CodeQL with `security-extended` query suite on every push |
| **SBOM** | CycloneDX via anchore/syft (US EO 14028 / EU CRA compliance) |
| **Fuzz Testing** | 9 libFuzzer harnesses (reader, Thrift, RLE, Delta, BSS, Dictionary, AES-GCM, PME, key metadata) |
| **Code Coverage** | Clang source-based coverage reported to Codecov |
| **Cross-Platform** | Linux (GCC 13, Clang 18), macOS (Apple Clang 17), Windows (MSVC 2022) |
| **Cross-Language** | Independent test suites for C++, Python, Rust, and WASM bindings |
| **Enterprise Benchmarks** | 59 real-world test cases with regression detection (20% alert threshold) |

## Security Hardening

| Pass | Vulnerabilities Fixed | Scope |
|------|----------------------|-------|
| #1 | 6 | Core parsers, encoders, Python bindings |
| #2 | 6 | WAL, feature store, Thrift parser, Arrow interop |
| #3 | 23 | Crypto (CSPRNG, key zeroing, counter overflow), all encoders, AI tier, compliance |
| #4 | 29 | All language bindings (C FFI, Rust, WASM, Python), integer overflow, decompression bombs |
| #5 | 53 | Full-scale audit: constant-time GHASH, GCM hardening, BYTE_ARRAY bounds, typed statistics, EU AI Act cross-chain, MiFID II precision, training metadata |
| Follow-up | 11 | Static audit cross-reference: page CRC-32, mmap parity, Float16 UB, feature flush, WAL fsync, compliance errors |
| #6 | 91 | Comprehensive end-to-end: crypto side-channels, GCM IV handling, X25519 MSVC fix, encoding overflow guards, decompression bombs, mmap safety, data races, EU AI Act training provenance |
| #7 | 126 | Full-scale audit: 5 CRITICAL (key gates, CSV injection, CSPRNG IDs, enum validation), 22 HIGH (CTR overflow, tensor overflow, type-aware column index, hash chains, CRC verification), 50 MEDIUM, 33 LOW |
| #8 | 21 | Delta completeness audit: OS command injection (CWE-78), Python use-after-free (CWE-416), ColumnIndex type confusion (CWE-843), FeatureReader race condition (CWE-362), CRNGT partial-block (FIPS 140-3) |
| **Total** | **458** | **Entire codebase, all language interfaces, all compliance reporters — zero open findings** |

## Cryptographic Validation

Every cryptographic primitive is verified against authoritative published test vectors
and third-party edge-case suites. This ensures correctness is not merely asserted but
independently reproducible from public standards documents.

| Component | Standard | Verification |
|-----------|----------|-------------|
| AES-256-GCM | [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38d/final) | 18 NIST test vectors including Test Case 15 (256-bit key, 64-byte plaintext, no AAD — validates CTR mode ciphertext and 128-bit authentication tag independently). Counter overflow guard per §5.2.1. S-box cache-timing mitigation via full table prefetch. Constant-time `gf_mul` in MixColumns. |
| AES-256-GCM (Wycheproof) | [Google Wycheproof](https://github.com/google/wycheproof) | 4 edge-case vectors from the AES-256-GCM test group: empty plaintext with non-empty AAD (tcId 92), standard 16-byte message with ciphertext verification (tcId 97), modified tag rejection (16 byte-by-byte flips + all-zero + all-ones tags), and tampered ciphertext rejection (16 CT byte flips). These catch implementation bugs that NIST vectors alone miss — tag truncation acceptance, ciphertext malleability, and AAD binding failures. |
| AES-256-CTR | [NIST SP 800-38A](https://csrc.nist.gov/pubs/sp/800/38a/final) | Published CTR mode test vectors. Counter overflow guard (64 GiB limit matching GCM). |
| AES-256-only | [NIST SP 800-131A Rev.2](https://csrc.nist.gov/pubs/sp/800/131a/r2/final) | Intentional design decision: AES-128 and AES-192 excluded. AES-256 retains 128-bit security under Grover's algorithm (post-quantum threat model). Single key size eliminates key-length confusion bugs ([CWE-326](https://cwe.mitre.org/data/definitions/326.html)). Documented in `crypto/aes_core.hpp`. |
| IV Generation | Platform CSPRNG | `arc4random_buf` (macOS), `getrandom` (Linux), `BCryptGenRandom` (Windows). Uniqueness verified: consecutive `generate_iv()` calls produce distinct 12-byte nonces with overwhelming probability (2^-96 collision). EINTR retry on Linux `getrandom`. IV uniqueness is critical for GCM security — IV reuse enables key recovery via the "forbidden attack" (Joux 2006). |
| Kyber-768 KEM | [NIST FIPS 203](https://csrc.nist.gov/pubs/fips/203/final) | liboqs 0.15.0, runtime `is_real_pq_crypto()` query. ML-KEM-768 provides IND-CCA2 security at NIST Level 3. |
| Dilithium-3 | [NIST FIPS 204](https://csrc.nist.gov/pubs/fips/204/final) | liboqs 0.15.0. ML-DSA-65 provides EUF-CMA security at NIST Level 3. |
| X25519 | [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) | Montgomery ladder with scalar clamping per §5. MSVC 10-limb `fe_sub` corrected. Published test vectors. |
| X25519 (Wycheproof) | [Google Wycheproof](https://github.com/google/wycheproof) | 7 edge-case vectors: valid key exchange (tcId 1), RFC 7748 §6.1 test vector (tcId 100), RFC 8037 Appendix A.6 with manual scalar clamping (tcId 102), 5 low-order point rejection tests (all-zero shared secret detection), 3 non-canonical u-coordinates (must compute correctly per RFC 7748 §5), and 2 twist points (must produce valid shared secrets). These vectors catch common X25519 implementation flaws: missing scalar clamping, failure to reject small-subgroup points, and incorrect modular reduction. |
| Key material | Best practice | Volatile-pointer zeroing in destructors. `SecureKeyBuffer` with `mlock`/`munlock` prevents key material from being paged to swap. |
| Hybrid KEM | [NIST SP 800-227](https://csrc.nist.gov/pubs/sp/800/227/final) (Final, Sep 2025) | SHA-256 key combining with domain separation label `"signet-forge-hybrid-kem-v1"`. Combines X25519 classical security with Kyber-768 post-quantum resistance — if either primitive remains secure, the combined key is secure. |

## Compliance Module Verification

The MiFID II and EU AI Act compliance reporters undergo the same verification as
every other component. Each test validates not just output format but regulatory
substance — ensuring the library produces reports that meet the specific requirements
auditors and regulators expect.

- **101 dedicated compliance tests** verifying report structure, field content, chain integrity, and regulatory framework types (GDPR, DORA, EU AI Act, MiFID II)
- **Field injection prevention**: MAX_FIELD_LENGTH = 4096 truncation on all user-supplied strings prevents JSON/CSV injection in generated regulatory reports
- **Timestamp correctness**: 64-bit `time_t` enforcement ensures timestamp accuracy beyond the 2038 Unix epoch rollover — critical for MiFID II trade reporting and EU AI Act log retention
- **Enterprise benchmark validation**: Phases CR1-CR6 generate reports from real tick data schemas, verifying the full pipeline from raw market data through hash-chained decision logs to regulatory output
- **Regulatory article traceability**: Each reporter cites the specific articles it implements in source comments (e.g., MiFID II [RTS 24 Annex I](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32017R0580), EU AI Act [Art. 12](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R1689))
- **Report ID uniqueness**: CSPRNG random hex suffix on auto-generated MiFID II report IDs satisfies the uniqueness requirement per RTS 24 Annex I field 1 — predictable IDs would allow report spoofing
- **Statistical consistency**: 3-sigma anomaly detection in EU AI Act Article 12 reports uses mean + 3σ threshold consistently across per-record and aggregate modes — auditors can reproduce the classification
- **Training provenance integrity**: `InferenceRecord` training provenance fields (`training_dataset_id`, `training_dataset_size`, `training_data_characteristics`) are included in the SHA-256 hash chain, ensuring training data characteristics cannot be tampered with after the fact — required by EU AI Act [Art. 13(3)(b)(ii)](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R1689)
- **Regulatory coverage**: GDPR Articles 5(1)(e), 15, 17, 30, 35 | DORA Articles 5-15, 24-30 | EU AI Act Articles 9-13, 15, 17, 61-62 | MiFID II RTS 24/RTS 6

## Continuous Integration

10 GitHub Actions jobs run on every push:

| Job | Purpose |
|-----|---------|
| build-test | Linux + macOS matrix |
| asan | AddressSanitizer + UBSan |
| tsan | ThreadSanitizer |
| ubsan | UndefinedBehaviorSanitizer (strict) |
| windows | MSVC 2022 |
| server-codecs | ZSTD + LZ4 + Gzip codec validation |
| post-quantum | Kyber-768 + Dilithium-3 via liboqs |
| fuzz | libFuzzer, 60 seconds per harness |
| coverage | Clang coverage to Codecov |
| benchmarks | Performance regression detection |

## Standards & References

The following standards are directly implemented and tested against in Signet Forge:

| Standard | Use in Signet Forge |
|----------|-------------------|
| [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38d/final) — GCM | AES-256-GCM footer encryption, 18 test vectors, counter overflow guard |
| [NIST SP 800-38A](https://csrc.nist.gov/pubs/sp/800/38a/final) — CTR | AES-256-CTR column encryption, published test vectors |
| [NIST SP 800-131A Rev.2](https://csrc.nist.gov/pubs/sp/800/131a/r2/final) | AES-256-only design decision (§4 long-term security recommendation) |
| [NIST SP 800-227](https://csrc.nist.gov/pubs/sp/800/227/final) — KEM | Hybrid KEM key combining with domain separation |
| [NIST FIPS 203](https://csrc.nist.gov/pubs/fips/203/final) — ML-KEM | Kyber-768 key encapsulation (NIST Level 3) |
| [NIST FIPS 204](https://csrc.nist.gov/pubs/fips/204/final) — ML-DSA | Dilithium-3 digital signatures (NIST Level 3) |
| [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) | X25519 key agreement, scalar clamping §5, test vectors §6.1 |
| [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869) | HKDF key derivation for PME |
| [Google Wycheproof](https://github.com/google/wycheproof) | AES-256-GCM and X25519 edge-case test vectors |
| [EU AI Act (Regulation 2024/1689)](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R1689) | Articles 9, 10, 11, 12, 13, 15, 17, 19, 61, 62 |
| [MiFID II RTS 24](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32017R0580) | Annex I report fields, report ID uniqueness, order lifecycle |
| [MiFID II RTS 6](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32017R0589) | Pre-trade risk checks (Art. 17) |
| [DORA (Regulation 2022/2554)](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32022R2554) | ICT risk management, incident reporting, resilience testing |
| [GDPR (Regulation 2016/679)](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32016R0679) | Right to erasure, DSAR, ROPA, DPIA, data retention |

## Authorship & Accountability

Signet Forge is authored and maintained by Johnson Ogundeji. All architectural
decisions, security hardening, compliance mappings, and code were designed,
reviewed, and accepted by the author. Commercial support is available through
SIGNETSTACK (johnson@signetstack.io).

## Reporting Security Issues

Do not open public GitHub issues for security vulnerabilities. Report them
responsibly via email: **johnson@signetstack.io**. See [SECURITY.md](../SECURITY.md)
for our disclosure policy and response timeline.
