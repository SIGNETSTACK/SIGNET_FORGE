# Quality Assurance & Security

Signet Forge maintains rigorous quality assurance across every layer of the library.

## Verification Infrastructure

| Layer | Coverage |
|-------|---------|
| **Unit Tests** | 554 tests across all features (C++), 35 Python tests, 10 Rust tests |
| **Security Hardening** | 6 dedicated audit passes + static audit follow-up — 242 vulnerabilities identified and fixed |
| **Compliance Gaps** | 67 of 92 enterprise compliance gaps resolved across 8 passes (FIPS, EU AI Act, MiFID II, GDPR, DORA, PME) |
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
| **Total** | **242** | **Entire codebase, all language interfaces, all compliance reporters** |

## Cryptographic Validation

| Component | Standard | Verification |
|-----------|----------|-------------|
| AES-256-GCM/CTR | NIST SP 800-38A/D | Published test vectors, counter overflow guard, S-box cache-timing mitigation (table prefetch), constant-time gf_mul |
| Kyber-768 KEM | NIST PQC | liboqs 0.15.0, runtime `is_real_pq_crypto()` query |
| Dilithium-3 | NIST PQC | liboqs 0.15.0 |
| X25519 | RFC 7748 | Montgomery ladder, published test vectors, MSVC 10-limb fe_sub corrected |
| Key material | Best practice | Volatile-pointer zeroing in destructors |
| Hybrid KEM | NIST SP 800-227 draft | SHA-256 with domain separation label |
| IV generation | Platform CSPRNG | `arc4random_buf` (macOS), `getrandom` (Linux), BCryptGenRandom validation (Windows), getrandom EINTR retry (Linux) |

## Compliance Module Verification

The MiFID II and EU AI Act compliance reporters undergo the same verification as
every other component:

- 52 dedicated compliance tests verifying report structure, field content, chain integrity, and regulatory framework types (GDPR, DORA, EU AI Act, MiFID II)
- Field length truncation (MAX_FIELD_LENGTH = 4096) preventing injection in generated reports
- 64-bit `time_t` enforcement for timestamp correctness beyond 2038
- Enterprise benchmark phases (CR1-CR6) validating report generation with real tick data schemas
- Each reporter cites the specific regulatory articles it implements in source comments
- CSPRNG random suffix on auto-generated MiFID II report IDs (uniqueness per RTS 24 Annex I)
- Consistent 3-sigma anomaly detection in EU AI Act Article 12 reports
- InferenceRecord training provenance fields included in hash chain (EU AI Act Art.13(3)(b)(ii))

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

## Authorship & Accountability

Signet Forge is authored and maintained by Johnson Ogundeji. All architectural
decisions, security hardening, compliance mappings, and code were designed,
reviewed, and accepted by the author. Commercial support is available through
SIGNETSTACK (johnson@signetstack.io).

## Reporting Security Issues

Do not open public GitHub issues for security vulnerabilities. Report them
responsibly via email: **johnson@signetstack.io**. See [SECURITY.md](../SECURITY.md)
for our disclosure policy and response timeline.
