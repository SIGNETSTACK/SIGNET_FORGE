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
| **Fuzz Testing** | 11 libFuzzer harnesses (reader, Thrift, WAL, RLE, Delta, Arrow, AES-GCM, PME, key metadata, HKDF, X25519) |
| **Code Coverage** | Clang source-based coverage reported to [Codecov](https://codecov.io/) |
| **Mutation Testing** | [Mull](https://github.com/mull-project/mull) 0.24.0 mutation analysis on crypto module |
| **Property-Based Testing** | 7 C++ tests (Catch2 GENERATE) + 9 Python tests ([Hypothesis](https://hypothesis.readthedocs.io/)) |
| **Resilience Testing** | 13 fault injection tests: file corruption, truncation, garbage input, WAL CRC corruption |
| **Concurrency Stress** | 32-thread MPMC ring + EventBus stress tests (16P×16C, 100K items) |
| **API Stability** | 19 regression tests locking enum values, struct sizes, error codes |
| **Cross-Platform** | Linux (GCC 13, Clang 18), macOS (Apple Clang 17), Windows (MSVC 2022) |
| **Cross-Language** | Independent test suites for C++, Python, Rust, and WASM bindings |
| **Enterprise Benchmarks** | 104 benchmark cases (45 core + 59 enterprise) with regression gate (50% threshold, build fails on regression) |
| **Fuzz Corpus Persistence** | GitHub Actions cache preserves libFuzzer corpus across runs — each session builds on prior discoveries |
| **Secrets Scanning** | [gitleaks](https://github.com/gitleaks/gitleaks) 8.24.3 on full repository history |
| **License Compliance** | SPDX header verification + GPL/AGPL contamination scan |
| **SBOM** | CycloneDX + SPDX JSON via [anchore/syft](https://github.com/anchore/sbom-action) ([US EO 14028](https://www.whitehouse.gov/briefing-room/presidential-actions/2021/05/12/executive-order-on-improving-the-nations-cybersecurity/) / [EU CRA](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R2847)) |

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

## Dynamic Testing Infrastructure

### Sanitizer Suite

Every push runs the full test suite under three independent sanitizers, catching
vulnerability classes that conventional testing cannot reach:

| Sanitizer | What It Detects | Standard |
|-----------|----------------|----------|
| **AddressSanitizer + LeakSanitizer** | Heap/stack buffer overflows ([CWE-122](https://cwe.mitre.org/data/definitions/122.html)), use-after-free ([CWE-416](https://cwe.mitre.org/data/definitions/416.html)), memory leaks ([CWE-401](https://cwe.mitre.org/data/definitions/401.html)) | [NIST SP 800-53 SI-16](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) |
| **ThreadSanitizer** | Data races ([CWE-362](https://cwe.mitre.org/data/definitions/362.html)), lock-order inversions | [CERT C CON00-C](https://wiki.sei.cmu.edu/confluence/display/c/14+Concurrency) |
| **UndefinedBehaviorSanitizer** | Signed overflow ([CWE-190](https://cwe.mitre.org/data/definitions/190.html)), null dereference, misaligned access | [MISRA C++:2023](https://www.misra.org.uk/product/misra-cpp2023/) |

All sanitizer jobs enable the commercial tier (`SIGNET_ENABLE_COMMERCIAL=ON`) to ensure
cryptographic code paths receive full runtime verification.

### Fuzz Testing (11 Harnesses)

[libFuzzer](https://llvm.org/docs/LibFuzzer.html) harnesses exercise every parser and cryptographic
primitive with randomized inputs, instrumented with AddressSanitizer:

| Harness | Target | Coverage |
|---------|--------|----------|
| `fuzz_parquet_reader` | Parquet file parsing | Magic bytes, Thrift footer, page decoding |
| `fuzz_thrift_decoder` | Compact Thrift format | All field types, varint, MAP, nesting |
| `fuzz_wal_reader` | WAL entry parsing | CRC-32 validation, 64-bit seek bounds |
| `fuzz_rle_decoder` | RLE bit-packed hybrid | bit_width 0–64, iterator + batch API |
| `fuzz_delta_decoder` | Delta binary packed | Varint header, zigzag, block decoding |
| `fuzz_arrow_import` | Arrow C Data Interface | ArrowArray/ArrowSchema import validation |
| `fuzz_aes_gcm` | AES-256-GCM | Encrypt→decrypt roundtrip, tag verification |
| `fuzz_pme` | PME orchestrator | 4-part AAD ordinals, column key cache |
| `fuzz_key_metadata` | Key metadata TLV | Serialization/deserialization, overflow |
| `fuzz_hkdf` | HKDF [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869) | Extract/Expand, variable-length IKM |
| `fuzz_x25519` | X25519 [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748) | Montgomery ladder, scalar clamping, low-order rejection |

Fuzz testing satisfies [NIST SP 800-53 SA-11(8)](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) "Dynamic Code Analysis"
and [DORA Art. 25](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32022R2554) "Threat-Led Penetration Testing" requirements.

### Mutation Testing

[Mull](https://github.com/mull-project/mull) 0.24.0 mutation analysis verifies that
the test suite detects real code mutations — not just that tests pass, but that they
*would fail* if logic were broken. The project includes a [`mull.yml`](../mull.yml)
configuration that restricts mutation to Signet Forge source (`include/signet/.*`) and
excludes third-party dependencies (`_deps/.*`).

**Running mutation testing locally (Linux, Clang 18 required):**

```bash
# 1. Install Mull 0.24.0 (Ubuntu 24.04)
curl -sSfL https://github.com/mull-project/mull/releases/download/0.24.0/Mull-18-0.24.0-LLVM-18.1-ubuntu-24.04.deb -o mull.deb
sudo dpkg -i mull.deb

# 2. Create a compiler wrapper that applies the Mull plugin only to project source,
#    skipping third-party deps (Catch2, etc.) which can crash the LLVM pass.
cat > /tmp/clang++-18-mull <<'WRAPPER'
#!/bin/bash
for arg in "$@"; do
  case "$arg" in
    *_deps/*|*catch2*|*Catch2*)
      exec /usr/bin/clang++-18 "$@"
      ;;
  esac
done
exec /usr/bin/clang++-18 -fpass-plugin=/usr/lib/mull-ir-frontend-18 -g -grecord-command-line "$@"
WRAPPER
chmod +x /tmp/clang++-18-mull

# 3. Configure with the Mull wrapper
cmake -S . -B build-mull -G Ninja \
    -DCMAKE_C_COMPILER=clang-18 \
    -DCMAKE_CXX_COMPILER=/tmp/clang++-18-mull \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSIGNET_BUILD_TESTS=ON

# 4. Build
cmake --build build-mull --target signet_tests

# 5. Run mutation analysis on the crypto module
mull-runner-18 build-mull/signet_tests \
    --reporters=Elements \
    --report-name=mutation-report \
    --include-path="include/signet/crypto/*" \
    -- "[encryption]"
```

The `mull.yml` file in the project root defines 19 mutation operators covering
arithmetic (`+`→`-`, `*`→`/`), comparison (`==`→`!=`, `<`→`>=`), logical (`&&`→`||`),
and constant replacement mutations. CI runs mutation testing automatically on every
push to `main` via the `mutation-baseline` job.

**Why a compiler wrapper?** Mull instruments code via an LLVM pass plugin applied at
compile time. Without the wrapper, the plugin processes third-party code (Catch2) and
can crash on unsupported LLVM IR patterns. The wrapper detects `_deps/`, `catch2`, or
`Catch2` in compiler arguments and bypasses the plugin for those files.

Mutation testing satisfies [IEEE 1008-2024](https://standards.ieee.org/ieee/1008/11491/)
test adequacy criteria and supports [NIST SP 800-53 SA-11](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final)
"Developer Testing" requirements.

### Property-Based & Regression Testing

- **7 C++ property-based tests** validate universal invariants (`decode(encode(x)) == x` for all inputs) using randomized generation with deterministic seeding — catching edge cases that hand-picked test vectors miss.
- **9 Python property-based tests** using [Hypothesis](https://hypothesis.readthedocs.io/) with automatic shrinking: int64/double/string roundtrip identity, length preservation, constant-value arrays, empty strings, and multi-column roundtrips. Run with `pip install hypothesis && pytest python/tests/test_property_based.py`.
- **19 API stability tests** lock enum values, struct sizes, and error codes to prevent accidental ABI breakage in downstream systems.
- **100+ hardening tests** verify safe rejection of malformed, adversarial, and boundary-condition inputs across all 10 security audit passes.

### Resilience & Fault Injection Testing

13 fault injection tests (`[resilience]` tag) verify graceful handling of adversarial
I/O conditions — no crashes, no undefined behavior, no silent data corruption:

| Test | What It Injects | Validates |
|------|----------------|-----------|
| Corrupted magic bytes | Overwrites PAR1 header/footer | Reader rejects with error, not crash |
| Truncated file (header only) | Resize to 8 bytes | Reader returns error |
| Truncated file (mid-footer) | Resize to 50% of file | Reader returns error |
| Zero-byte file | Empty file | Reader returns error |
| Corrupted Thrift footer | Overwrites footer metadata | No segfault on malformed Thrift |
| Random garbage content | 4KB of random data | Reader rejects non-Parquet input |
| Corrupted page data | Overwrites column data pages | No crash; error or silent (data layer) |
| Nonexistent directory | Write to `/nonexistent/path/` | Writer returns IO error |
| Nonexistent file | Read from missing path | Reader returns IO error |
| Truncated WAL | Resize WAL mid-entry | Reader recovers partial entries |
| Corrupted WAL CRC | Overwrites CRC bytes | Reader stops at corruption, no crash |

Resilience testing satisfies [NIST SP 800-53 SI-10](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) "Information Input Validation" and [DORA Art. 9](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32022R2554) "ICT Risk Management" requirements.

### Concurrency Stress Testing

High-thread-count stress tests verify lock-free data structures under contention
levels exceeding typical production deployments:

| Test | Threads | Items | Ring Size | Validates |
|------|---------|-------|-----------|-----------|
| MpmcRing 16P×16C | 32 | 100K | 4096 | Checksum integrity under high contention |
| EventBus 16P×16C | 32 | 50K | 8192 | Full publish/pop path with ColumnBatch allocation |
| MpmcRing sustained 32-thread | 32 | 100K | 8192 | Throughput + completion within 10s deadline |

These tests exercise the Vyukov MPMC bounded queue under 4× the concurrency of the
standard test suite (4P×4C → 16P×16C), catching ABA problems, false sharing, and
memory ordering bugs that only manifest under heavy contention.

Run: `ctest -R stress`

## Continuous Integration

16 GitHub Actions jobs run on every push:

| Job | Purpose |
|-----|---------|
| build-test | Linux (Ubuntu 24.04) + macOS (14) matrix |
| asan | AddressSanitizer + UBSan (commercial tier) |
| tsan | ThreadSanitizer (commercial tier) |
| ubsan | UndefinedBehaviorSanitizer (strict, halt-on-error) |
| windows | MSVC 2022 + `/analyze` static analysis |
| server-codecs | ZSTD + LZ4 + Gzip codec validation |
| commercial-full | Full test suite with commercial tier |
| post-quantum | Kyber-768 + Dilithium-3 via liboqs |
| fuzz | 11 libFuzzer harnesses, 60 seconds each |
| coverage | Clang source-based coverage to [Codecov](https://codecov.io/) |
| benchmarks | Performance regression detection (150% threshold) |
| secrets-scan | [gitleaks](https://github.com/gitleaks/gitleaks) credential detection |
| python-sast | [bandit](https://bandit.readthedocs.io/) Python security scanning |
| license-check | SPDX + GPL contamination verification |
| mutation-baseline | [Mull](https://github.com/mull-project/mull) mutation testing (crypto module) |
| CodeQL | [CodeQL](https://codeql.github.com/) SAST with `security-extended` queries |

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
| [NIST SP 800-53 Rev. 5](https://csrc.nist.gov/pubs/sp/800/53/r5/upd1/final) | SA-11 developer testing, SI-10 input validation, SI-16 memory protection |
| [NIST SP 800-218 (SSDF)](https://csrc.nist.gov/pubs/sp/800/218/final) | Secure Software Development Framework — SBOM, testing, code review |
| [US Executive Order 14028](https://www.whitehouse.gov/briefing-room/presidential-actions/2021/05/12/executive-order-on-improving-the-nations-cybersecurity/) | §4(e): Software Bill of Materials (SBOM) requirements |
| [EU Cyber Resilience Act](https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R2847) | Art. 13, Annex I: SBOM, secure development, attack surface reduction |
| [ISO/IEC 27001:2022](https://www.iso.org/standard/27001) | A.8.25: Secure development lifecycle testing requirements |
| [SOC 2 Type II (AICPA)](https://www.aicpa-cima.com/topic/system-and-organization-controls) | CC7.1: System operations monitoring and vulnerability management |
| [CWE Top 25 (2024)](https://cwe.mitre.org/top25/archive/2024/2024_cwe_top25.html) | Hardening tests directly address 12 of the Top 25 weakness categories |

## Authorship & Accountability

Signet Forge is authored and maintained by Johnson Ogundeji. All architectural
decisions, security hardening, compliance mappings, and code were designed,
reviewed, and accepted by the author. Commercial support is available through
SIGNETSTACK (johnson@signetstack.io).

## Reporting Security Issues

Do not open public GitHub issues for security vulnerabilities. Report them
responsibly via email: **johnson@signetstack.io**. See [SECURITY.md](../SECURITY.md)
for our disclosure policy and response timeline.
