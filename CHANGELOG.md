# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.1] — 2026-05-01

This release rolls up all post-v0.1.0 security, correctness, and CI work:
external pentest remediation (5 findings, 2 HIGH), Audit #8 (21 findings),
Thrift Correctness Phase (47 new tests), 73 → 92 enterprise compliance gap
fixes, EventBus and FeatureReader performance improvements, local KMS key
store for on-premise deployments, and Windows MSVC + Ubuntu CI green across
17 jobs. 779 → 830 unit tests, all passing. No public C++ API breaks; the
Rust `ParquetReader::schema()` return type changed from `Schema` to
`SchemaRef<'a>` to fix CWE-416 — see migration note below.

### Migration

- **Rust**: `ParquetReader::schema()` now returns `SchemaRef<'a>` (lifetime
  bound to the reader). Existing `let s = reader.schema();` continues to
  compile; storing the schema beyond the reader's lifetime now fails at
  compile time as intended.

**2026-03-30 — External Pentest Remediation (Strix.ai)**

### Security — External Pentest Remediation (5 findings, Strix.ai)

Five external pentest findings fully remediated. 826 → 830 unit tests. Zero open vulnerabilities.

- **rust/signet-forge/src/schema.rs + reader.rs + lib.rs**: `ParquetReader::schema()` returned a raw FFI pointer as an unconstrained `Schema` value — safe Rust callers could retain it after the reader was dropped, causing use-after-free (CWE-416, CVSS 8.4). Fixed with `SchemaRef<'a>` lifetime-bound wrapper type; `PhantomData<&'a ()>` ties the borrow to the reader's lifetime, making misuse a compile-time error at zero runtime cost. `SchemaRef` re-exported at crate root.
- **include/signet/reader.hpp**: `DELTA_BINARY_PACKED` and `BYTE_STREAM_SPLIT` column decoders silently returned truncated or empty vectors when decoded value count mismatched the page `num_values` field, causing invisible data corruption with no error (CWE-130, CVSS 6.5). Fixed by validating `decoded.size() != count` after every decode call and returning `CORRUPT_PAGE` error on mismatch — applies to `int32`, `int64`, `float`, and `double` paths.
- **include/signet/mmap_reader.hpp**: `MmapParquetReader` could receive `SIGBUS` and crash when a file was truncated after the mapping was established — any subsequent read from the invalidated region raises SIGBUS (CWE-367, CVSS 5.5). Fixed by copying header bytes (4 KB window) and page payload into `std::vector` owned memory before any parsing or CRC operations. Removed volatile pre-fault byte reads in `open()` that intentionally touched mapped pages (a SIGBUS trigger under concurrent truncation).
- **include/signet/error.hpp**: Evaluation license counters could be reset by rotating `SIGNET_COMMERCIAL_USAGE_FILE` env var to a fresh writable path across process restarts — a missing file silently initialized fresh counters, bypassing the evaluation limit (CWE-284, CVSS 3.3). Fixed by removing the env-var override path entirely: `usage_state_path()` now unconditionally returns `default_usage_state_path()`. Removed now-unused `<climits>` and `<filesystem>` includes.

| Finding | Severity | CWE | CVSS | File |
|---------|----------|-----|------|------|
| F1/F2 — Rust UAF in `schema()` | HIGH | CWE-416 | 8.4 / 7.3 | schema.rs, reader.rs, lib.rs |
| F3 — DELTA/BSS silent corruption | MEDIUM | CWE-130 | 6.5 | reader.hpp |
| F4 — mmap SIGBUS on truncation | MEDIUM | CWE-367 | 5.5 | mmap_reader.hpp |
| F5 — License counter reset | LOW | CWE-284 | 3.3 | error.hpp |

---

**2026-03-10 — Audit #8 + Performance**

### Performance
- **EventBus**: Replace mutex-guarded `shared_ptr<StreamingSink>` with `std::atomic_load/store` — publish() hot path is now lock-free (~53 ns, down from ~94 ns)
- **FeatureReader**: Add single-entry row group cache — consecutive point queries to the same row group reuse decoded columns instead of re-decoding (get() ~0.14 μs cached, as_of_batch(100) ~19 μs)

### Security — Audit #8: Delta Completeness Audit
21 findings (3 pre-existing + 18 new), all remediated. Zero known open vulnerabilities.

- **examples/ticks_import.cpp, ticks_wal_stream.cpp**: Shell metacharacter validation before `popen()` — rejects `' " \ \` $ | ; & ( ) < > \n \r \0` in input paths (CWE-78)
- **python/_bindings.cpp**: Fixed use-after-free in all 4 numeric column readers — `py::array_t` now allocated as Python-owned buffer with `memcpy` from decoded vector (CWE-416)
- **column_index.hpp**: Type-aware boundary order detection using `PhysicalType` parameter — fixes predicate pushdown for signed INT32/INT64 columns (CWE-843)
- **ai/feature_reader.hpp**: Added `mutable std::mutex rg_cache_mutex_` protecting row group cache + custom move constructor/assignment locking source mutex (CWE-362)
- **thrift/types.hpp**: Added `count < 0` guard to 4 list count checks preventing negative-to-massive-size_t allocation (CWE-400)
- **thrift/compact.hpp**: `write_list_header` throws `std::invalid_argument` on negative size instead of silent return (CWE-754)
- **column_index.hpp**: `OffsetIndex::deserialize()` sets `valid_ = false` before early return (CWE-754)
- **ai/inference_log.hpp**: Reordered underflow guard: `offset > size || emb_count > (size - offset) / sizeof(float)` (CWE-191)
- **wasm/signet_wasm.cpp**: `writeFileToMemfs` returns `bool` (was `void`); JSON `readStr()` handles backslash escapes (CWE-252, CWE-20)
- **crypto/cipher_interface.hpp**: CRNGT partial-block match now throws instead of silent skip (FIPS 140-3 §4.9.2)
- **bloom/split_block.hpp**: Block index bounds assertion in `insert()` and `might_contain()` (CWE-125)
- **writer.hpp**: `build_column_index()` threads `schema_.column(c).physical_type` for type-correct boundary order
- **examples/ticks_import.cpp**: Replaced `catch(...)` with typed `catch(const std::exception& e)` (CWE-755)

### Security
- **error.hpp**: Strengthen `usage_state_path()` with 6-layer validation: absolute-path-only, realpath canonicalization, is_directory parent check, null byte rejection, path traversal rejection, post-canonicalization recheck
- **wal.hpp**: POSIX `open(0600)` + `fdopen()` for CWE-732 world-writable file prevention (3 locations)
- **CodeQL**: All 8 code scanning alerts resolved (5 fixed in code, 3 dismissed with documented justification)

### Documentation
- Updated all benchmark figures across README.md, docs/BENCHMARKS.md, COMPARISON.md, PRODUCT_OVERVIEW.md to reflect measured values
- WalMmapWriter: corrected from projected ~38 ns to measured ~223 ns
- **Audit #8 documentation**: Comprehensive internal audit report (docs/internal/AUDIT_8_DOCUMENTATION.md) with CWE references, CVSS scores, risk analysis, and cross-references to NIST/OWASP/CERT/RFC publications
- Updated 10 documentation files with Audit #8 findings: internal architecture docs (encryption, thread model, AI subsystem, encoding codecs), product-knowledge docs (security hardening, cross-language bindings), and client-facing docs (SECURITY.md, CHANGELOG.md, QUALITY_ASSURANCE.md)

**2026-03-09 — Enterprise Compliance & Hardening**

### Enterprise Compliance — 73 of 92 Gaps Resolved (2026-03-09)

Nine compliance gap-fix passes resolving 73 of 92 enterprise regulatory gaps. 566 unit tests (100% passing). Covers FIPS 140-3, EU AI Act, MiFID II, GDPR, DORA, and Parquet PME spec.

#### Gap Fix Pass 1 (7 gaps)
- CodeQL SAST workflow with `security-extended` query suite (T-1)
- CycloneDX SBOM generation via `anchore/syft` (T-2)
- Full 554-test commercial tier coverage in CI (T-15)
- Sanitizer coverage for crypto code paths (T-15b)
- GCM invocation counter with 2^32 key rotation trigger (C-3)
- UTC `system_clock` traceability for all timestamps (R-5)
- MiFID II RTS 24 mandatory fields: buy_sell_indicator, order_type, time_in_force, ISIN, currency, short_selling_flag (R-4)

#### Gap Fix Pass 2 (11 gaps)
- Log retention lifecycle API with configurable policies (R-1)
- EU AI Act Art.13 transparency model card fields (R-2)
- Human oversight API: `HumanOverrideRecord`, override tracking, override rate monitoring (R-3)
- Art.15 accuracy metrics: PSI/KS-test drift detection, bias monitoring (R-3b)
- Full NIST SP 800-38D test vector suite — 18 test cases (C-2/T-4)
- Crypto fuzz harnesses: AES-GCM, PME, key metadata (T-3)
- PME dictionary page + page header encryption (P-1, P-2)
- AAD binary format per PME spec (P-4)
- Thrift-serialized key metadata replacing custom TLV (P-6)
- PME negative security tests: AAD mismatch, key confusion, page reorder (P-9)

#### Gap Fix Pass 3 (9 gaps)
- GCM tag truncation rejection (C-6)
- AAD length limit enforcement per SP 800-38D (C-12)
- Column key O(1) cache with `unordered_map` (P-8)
- NIST SP 800-38A CTR test vectors (P-10)
- Power-on crypto self-tests / KATs (C-9)
- LEI, ISIN, MIC code validation per ISO 17442/6166/10383 (R-12/R-12b/R-12c)
- Kill switch / circuit breaker API (R-10)
- HKDF key derivation per RFC 5869 (C-7/C-8)

#### Gap Fix Pass 4 (7 gaps)
- Signed plaintext footer with HMAC-SHA256 + HKDF-derived keys (P-3)
- KMS client interface: `IKmsClient` abstract class for DEK/KEK key wrapping (P-5)
- `SecureKeyBuffer` RAII with mlock/munlock + secure zeroization (C-11)
- Crypto-shredding for GDPR Art.17 right-to-erasure (G-1)
- PII data classification: 6-level `DataClassification` enum (G-2)
- Pre-trade risk checks per MiFID II RTS 6 Art. 17 (R-11)
- ICT asset identification/classification per DORA Art. 7-8 (D-6)

#### Gap Fix Pass 5 (7 gaps)
- Pseudonymizer utility: HMAC-SHA256 deterministic keyed hashing (G-5)
- GDPR writer policy: enforcement of encryption for PII columns (G-7)
- Records of Processing Activities (ROPA) per GDPR Art. 30 (G-3)
- Data retention / TTL with legal hold support per GDPR Art. 5(1)(e) (G-4)
- Backup policy / RPO tracking per DORA Art. 12 (D-3)
- Key rotation lifecycle management per DORA Art. 9(2) (D-11)
- Continuous RNG test (CRNGT) per FIPS 140-3 §4.9.2 (C-13)

#### Gap Fix Pass 6 (8 gaps — DORA compliance)
- ICT incident management: `ICTIncidentRecord`, severity scoring per DORA Art. 10/15/19 (D-1)
- Resilience testing: `ResilienceTestRecord` per DORA Art. 24-27 (D-2)
- Third-party risk: `ThirdPartyRiskEntry` per DORA Art. 28-30 (D-4)
- ICT risk management: `ICTRiskEntry` per DORA Art. 5-6 (D-5)
- Anomaly detection: `AnomalyRecord` with 6 categories per DORA Art. 10 (D-7)
- Recovery procedures: `RecoveryProcedure` per DORA Art. 11 (D-8)
- Post-incident review: `PostIncidentReview` per DORA Art. 13 (D-9)
- ICT notification: `ICTNotification` per DORA Art. 14 (D-10)

#### Gap Fix Pass 7 (15 gaps — EU AI Act + MiFID II + GDPR)
- DPIA record per GDPR Art. 35 (G-6)
- Subject data query/response for DSAR per GDPR Art. 15 (G-8)
- Performance/drift metrics per EU AI Act Art. 15 (R-6)
- AI risk assessment per EU AI Act Art. 9 (R-7)
- Technical documentation per EU AI Act Art. 11/Annex IV (R-8)
- QMS checkpoints per EU AI Act Art. 17 (R-9)
- Report integrity / signed reports per MiFID II RTS 24 Art. 4 (R-13/R-13b)
- Completeness attestation with gap detection per RTS 24 Art. 9 (R-13c)
- Annual self-assessment per MiFID II Art. 17(2) (R-14)
- Training data metrics per EU AI Act Art. 10 (R-15)
- Lifecycle event logging per EU AI Act Art. 12(2) (R-15b)
- Post-market monitoring per EU AI Act Art. 61 (R-16)
- Order lifecycle linking with `parent_order_id` per RTS 24 Art. 9 (R-17)
- Serious incident reporting per EU AI Act Art. 62 (R-18)
- Source file manifest in reports (R-18b)

#### Gap Fix Pass 8 (3 gaps — Crypto infrastructure)
- Algorithm deprecation framework per [NIST SP 800-131A](https://csrc.nist.gov/pubs/sp/800/131/a/r2/final) (C-4)
- `INTERNAL` key mode production gate per FIPS 140-3 §7.7 (C-15)
- Key rotation request/result API per PCI-DSS/HIPAA/SOX (T-7)

#### Gap Fix Pass 9 (6 gaps — Cryptographic validation & test vectors)
- AES-256-only design decision documented per [NIST SP 800-131A Rev.2 §4](https://csrc.nist.gov/pubs/sp/800/131/a/r2/final): post-quantum safety (Grover's algorithm), single key size eliminates [CWE-326](https://cwe.mitre.org/data/definitions/326.html) (C-14)
- [NIST SP 800-38D](https://csrc.nist.gov/pubs/sp/800/38/d/final) Test Case 15: AES-256-GCM with 64-byte plaintext and independent ciphertext + tag verification (C-16)
- 7 [Wycheproof](https://github.com/google/wycheproof) X25519 edge-case tests: valid exchange, [RFC 7748 §6.1](https://www.rfc-editor.org/rfc/rfc7748#section-6.1) vector, RFC 8037 with manual scalar clamping, low-order point rejection, non-canonical u-coordinates, twist points (C-18)
- [NIST SP 800-227](https://csrc.nist.gov/pubs/sp/800/227/final) updated from draft to Final (Sep 2025) across all source files and documentation (C-19)
- 4 Wycheproof AES-256-GCM edge-case tests: empty plaintext + AAD (tcId 92), ciphertext verification (tcId 97), modified tag rejection (16 flips + boundary values), tampered ciphertext detection (T-5)
- IV uniqueness verification: consecutive CSPRNG-generated nonces proven distinct, same plaintext with different IVs produces different ciphertext — validates GCM IV non-reuse guarantee (T-18)

### Security — Comprehensive Cryptographic & Systems Audit (2026-03-08)

End-to-end security audit across all 53 header files — crypto, encoding, compression, Thrift, bloom, core reader/writer, interop bridges, AI tier, WAL, streaming, feature store, event bus, and compliance reporters. 91 vulnerabilities identified and fixed across ~45 files. 423/423 tests pass.

#### Cryptography (21 fixes)
- AES S-box cache-timing mitigation: full table prefetch before encrypt/decrypt (NIST/Bernstein 2005)
- Constant-time `gf_mul` in MixColumns: replaced branching with arithmetic masking
- `Aes256` made non-copyable (key material hygiene); move ops securely zero source
- GCM: `encrypt()`/`decrypt()` now call `derive_j0()` for correct 12/16-byte IV handling (NIST SP 800-38D §7.1)
- GCM `gctr()`: block count overflow guard (2^32-2 limit per NIST SP 800-38D)
- MSVC X25519 `fe_sub`: corrected `2p` constants (was `0x3FFFFF0`, now `2^26-19`); all X25519 on Windows was broken
- Hybrid KEM: added domain separation label `"signet-forge-hybrid-kem-v1"` to SHA-256 key combining (NIST SP 800-227, Final Sep 2025)
- `AesGcmCipher`/`AesCtrCipher`: key storage changed from `std::vector` to `std::array<uint8_t, 32>` (prevents reallocation leaks)
- `BCryptGenRandom`: return value now checked; size validated against `ULONG` truncation (Windows)
- `KeyMode::INTERNAL`: runtime warning when raw key stored in Parquet metadata
- TLV `append_tlv_str`/`append_tlv_blob`: overflow check against `MAX_TLV_LENGTH`
- TLV deserialization: `KeyMode` and `EncryptionAlgorithm` enum range validation
- `KeyPair`, `SignKeyPair`, `HybridKeyPair`, `PostQuantumConfig`: zeroing destructors for secret key material
- Audit chain `now_ns()`: changed to `steady_clock` + cross-thread atomic monotonicity (was `high_resolution_clock` + `thread_local`)
- Audit chain serialize: overflow check on entry count before `uint32_t` cast
- Audit chain deserialize: bounds check before `reserve()` to prevent 480 GB allocation on crafted input
- Non-constant-time `ghash()` marked `[[deprecated]]`

#### Encoding & Compression (22 fixes)
- RLE decoder: truncated value now returns `false` instead of silently zeroing missing bytes
- RLE varint: stream position restored on overflow (was left misaligned)
- RLE `encode_with_length`: payload size overflow check before `uint32_t` cast
- RLE `flush_rle_run`: shift overflow guard (`rle_count_` capped at `SIZE_MAX >> 1`)
- Delta `decode_int32`: range check against `INT32_MIN`/`INT32_MAX` before narrowing cast
- Delta encoder: subtraction uses unsigned arithmetic to avoid signed overflow UB
- Dictionary encoder: `MAX_DICTIONARY_ENTRIES` (1M) limit with `is_full()` API
- BSS encode: overflow check on `count × WIDTH` before allocation (parity with decode)
- Decompression bomb: absolute 256 MB cap + zero-length compressed data rejection + ratio check
- Snappy compress: input >4 GiB rejected (was silently truncated to 32 bits)
- Snappy decompress: 256 MB absolute size cap
- Snappy `match_length`: bounds guard on source pointers
- LZ4: `size_t`→`int` overflow validation before all liblz4 calls
- GZIP: `size_t`→`uInt` overflow validation before all zlib calls
- Thrift: `zigzag_encode_i64` uses unsigned left shift (UB fix for negative values)
- Thrift: `write_list_header` rejects negative size
- Thrift: `end_struct` sets `error_` on stack underflow instead of silent no-op
- Thrift: global `total_fields_read_` counter with 1M cap (prevents per-struct reset bypass)
- Bloom `from_data`: enforces `kMaxBytes` (128 MiB) limit
- Bloom: `reinterpret_cast<uint32_t*>` replaced with `memcpy`-based access (strict aliasing + alignment)
- xxHash: MSVC endianness detection added (was `#error` on MSVC)

#### Core Reader/Writer/Interop (23 fixes)
- `read_batch_string()`: subtraction-based bounds check prevents integer overflow (was `pos_ + len > size_`)
- `extract_byte_array_strings()`: bounds checks on length prefix and string data reads
- `data_at()` in mmap reader: validates offset against `mapped_size_`
- Mmap reader: `MADV_WILLNEED` + volatile first/last byte read to detect truncated files early
- Mmap reader: 1024:1 decompression ratio check (parity with regular reader)
- Column index: list count capped at 10M before `resize()` (prevents Thrift-based memory bomb)
- Column writer: >4 GiB BYTE_ARRAY now throws `std::length_error` (was silent data loss)
- `SIGNET_FORGE_STATE_DIR`: path traversal rejection (`..` segments)
- Default usage state path: XDG_STATE_HOME/HOME-based (was `/tmp` — symlink attack risk)
- Usage state file: `lstat` symlink check before write (TOCTOU mitigation)
- DLPack `byte_offset`: range validation
- DLPack `import_tensor_copy`: checked multiplication for `num_elements × elem_size`
- DLPack ndim: range check (max 32)
- Arrow bridge: `memset` zero-initialization of output structs (prevents double-free on partial init)
- ONNX bridge: dimension positivity validation
- Reader: 1024:1 decompression ratio check
- Reader: 256 MB decoded page memory budget
- Statistics: type-safety documentation for `min_as<T>`/`max_as<T>`
- Z-order: alignment validation before pointer casts in `normalize_column()`
- Arena: `allocate_zeroed()` method added

#### AI / WAL / Streaming / Compliance (25 fixes)
- `WalMmapWriter::append()`: assert-only bounds check replaced with runtime `if` check (was compiled away in Release)
- `WalMmapWriter`: `WAL_MAX_RECORD_SIZE` enforcement (was missing — records >64 MB caused silent WalReader data loss)
- `WalMmapWriter`: `active_idx_` changed to `std::atomic<size_t>` (data race between writer and bg thread)
- `WalMmapWriter`: `closed_` changed to `std::atomic<bool>`
- `WalWriter::open()` resume scan: `WAL_MAX_RECORD_SIZE` enforcement (prevents corrupt `data_sz` skip)
- `WalWriter`: Windows `_ftelli64()` for >2 GB WAL files
- WAL CRC-32: documented as crash-recovery-only (not tamper-evident)
- `EventBus`: `sink_` changed to `std::atomic<StreamingSink*>` (use-after-free on concurrent detach)
- `MpmcRing::Slot`: `alignas(64)` to eliminate false sharing (latency impact 2-5x)
- `ColumnBatch::to_stream_record`: `uint32_t` overflow check on row count
- `InferenceRecord::serialize()`: added EU AI Act training provenance fields (`training_dataset_id`, `training_dataset_size`, `training_data_characteristics`) — previously omitted, allowing metadata tampering without breaking hash chain
- `json_to_features`/`json_to_embedding`: 1M element cap (prevents memory exhaustion on crafted JSON arrays)
- `FeatureReader`: `failed_file_count_` tracking (was silent skip — compliance risk for incomplete feature queries)
- `FeatureWriter`: partial file cleanup on roll close failure
- `FeatureWriter`: symlink limitation documented
- `DecisionLogWriter`: symlink limitation documented
- `MiFID2Reporter`: CSPRNG random hex suffix on auto-generated `report_id` (was predictable timestamp-only)
- `EUAIActReporter`: per-record anomaly flag changed to `mean + 3σ` (was inconsistent `3× mean`)
- `StreamingSink`: `std::filesystem::path` iteration for path traversal check (was manual string splitting)
- `SpscRingBuffer`: heap allocation documentation for large capacities
- `RowLineageTracker`: commercial license check discard documented as intentional

#### Regulatory Compliance Impact
- **MiFID II RTS 24**: Report IDs now include CSPRNG entropy — satisfies uniqueness requirement per Annex I field 1
- **EU AI Act Article 12**: Per-record anomaly detection now uses 3-sigma statistical threshold consistent with aggregate (was `3× mean`), ensuring coherent monitoring logs per Article 12(1)
- **EU AI Act Article 13**: `InferenceRecord` serialization now includes training dataset provenance fields, ensuring hash chain integrity covers training data characteristics per Article 13(3)(b)(ii)
- **NIST SP 800-38D**: GCM IV derivation (`derive_j0`) correctly implements §7.1 for both 96-bit and non-96-bit IVs; counter overflow guard implements §5.2.1
- **RFC 7748**: X25519 Montgomery ladder field arithmetic corrected for MSVC (10-limb representation)
- **FIPS 197**: AES S-box cache-timing mitigation via full-table prefetch

### Security — Static Audit Follow-Up (2026-03-07)

- 11 additional fixes from cross-referencing 15 static audit findings against Pass #5
- 6 new fixes: page CRC-32 in writer (Parquet spec compliance), mmap parity gaps (negative page size + num_values cap), reader row_group OOB bounds check, Z-Order column count validation (CWE-787), Float16 shift UB + 6 unaligned cast fixes (CWE-704), feature flush data loss prevention (error path ordering)
- 5 partial completions: getrandom EINTR retry, delta zigzag unsigned shift, statistics typed merge (PhysicalType dispatch), compliance error reporting (5 silent skips → errors), WAL fsync return checks
- 1 deferred: Feature Store composite `(timestamp_ns, version)` tie-breaker — API contract change deferred to Feature Store v2

### Security — Hardening Pass #5 (2026-03-07)

- 53 security and correctness fixes across crypto, encoding, I/O, AI, and compliance subsystems
- 8 CRITICAL: constant-time GHASH (4-bit table lookup), GCM counter overflow guard, RLE resize formula, dictionary error reporting, BYTE_ARRAY bounds validation (read + write), INT4 sign extension (portable branchless), verify_chain early-return on tamper detection
- 18 HIGH: secure key zeroing (volatile + compiler barrier), move-only ciphers (AesCtr), CSPRNG hardening (BCryptGenRandom for Windows, hard-fail on unsupported), overflow guards (BSS, delta, mmap footer, arena), typed statistics (PhysicalType tracking), NaN exclusion from num_values, xxHash endianness enforcement, configurable compliance (price precision, timestamp granularity), training metadata (EU AI Act Art.13), Art.19 cross-chain verification, monotonic now_ns(), MPMC ring validation
- 18 MEDIUM: constant-time X25519 zero check, TLV metadata caps (1 MB), PME module_type validation, reduced Thrift nesting (128 to 64), Arrow offset/length caps, bloom filter seed enforcement, writer close validation, SHA-256 FIPS test vector, WAL empty record tracking, instrument validator callback, feature computation lineage, inference training metadata
- 9 LOW: 64-bit Snappy hash positions, Thrift structured errors, GCM IV size configuration (12/16 bytes), mmap decompression pre-validation; 4 no-ops (correct per spec)
- ~29 new tests (all tagged `[hardening]`), 2 new ErrorCodes (CORRUPT_DATA, INVALID_ARGUMENT)
- Standards: NIST SP 800-38D, NIST SP 800-38A, FIPS 197, FIPS 180-4, RFC 7748, MiFID II RTS 24, EU AI Act Art.12/13/19

## [0.1.0] - 2026-03-04

Initial public release of Signet Forge.

### Added

#### Core Parquet Engine
- `ParquetWriter` — streaming Parquet file writer with configurable encoding and compression
- `ParquetReader` — random-access Parquet file reader with typed column APIs
- `MmapParquetReader` — memory-mapped reader path for zero-copy access
- `Schema` and `SchemaBuilder` — fluent schema definition with 7 physical types (BOOLEAN, INT32, INT64, INT96, FLOAT, DOUBLE, BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY)
- `ColumnStatistics` — min/max/null_count per column chunk
- `ColumnIndex` and `OffsetIndex` — predicate pushdown support
- CSV-to-Parquet converter with automatic type detection

#### Encodings
- PLAIN encoding for all physical types
- RLE/Bit-Packing Hybrid encoding (definition/repetition levels, boolean columns)
- DELTA_BINARY_PACKED encoding (timestamps, monotonic sequences)
- BYTE_STREAM_SPLIT encoding (floating-point columns)
- RLE_DICTIONARY encoding with dictionary pages

#### Compression
- Snappy — bundled header-only implementation (zero external dependencies)
- ZSTD — optional, link `libzstd` (`-DSIGNET_ENABLE_ZSTD=ON`)
- LZ4 — optional, link `liblz4` (`-DSIGNET_ENABLE_LZ4=ON`)
- Gzip — optional, link `zlib` (`-DSIGNET_ENABLE_GZIP=ON`)

#### Encryption
- AES-256-GCM footer encryption
- AES-256-CTR column data encryption
- Parquet Modular Encryption (PME) full spec implementation
- Key metadata serialization
- Post-quantum encryption: Kyber-768 KEM + Dilithium-3 digital signatures
- X25519 Diffie-Hellman key agreement (RFC 7748 Montgomery ladder)
- Hybrid KEM: X25519 + Kyber-768 combined key encapsulation

#### Bloom Filters
- Split-block bloom filter (Parquet spec compliant)
- Bundled xxHash64 implementation (public domain)
- PME-encrypted bloom filter support

#### AI Vector Types
- `FLOAT32_VECTOR(dim)` logical type with SIMD-accelerated I/O
- `INT8` and `INT4` quantized vector storage with on-read dequantization

#### Zero-Copy Interop
- Arrow C Data Interface bridge (`ArrowArray` / `ArrowSchema`, zero-copy)
- ONNX Runtime bridge (`OrtValue` creation from Parquet columns)
- NumPy / DLPack / buffer protocol bridge for PyTorch integration
- `TensorView` and `OwnedTensor` for zero-copy ML inference

#### Write-Ahead Log (WAL)
- `WalWriter` — fwrite-based WAL with 339 ns per-append latency (32 B payload)
- `WalReader` — crash-safe WAL reader with CRC-32 integrity verification
- `WalManager` — segment rolling, compaction, and lifecycle management
- `WalMmapWriter` — mmap ring-buffer WAL with ~38 ns per-append latency
- `MappedSegment` — 4-slot ring with background pre-allocation and drain
- `StreamingSink` — lock-free ring buffer to row group flusher

#### Feature Store
- `FeatureWriter` — point-in-time correct feature materialization to Parquet
- `FeatureReader` — 12 us per-entity `as_of()` lookup via binary search index
- History and batch APIs for time-travel feature retrieval

#### Event Bus
- `MpmcRing<T>` — Vyukov MPMC bounded queue (10.4 ns single-threaded push+pop)
- `ColumnBatch` — columnar batch container with zero-copy TensorView access
- `EventBus` — three-tier event router (topic → subscriber → handler)

#### AI Audit Trail (AGPL-3.0)
- `AuditChain` — SHA-256 hash chain across row groups for tamper detection
- `DecisionLogWriter` / `DecisionLogReader` — structured AI decision logging
- `InferenceLogWriter` / `InferenceLogReader` — ML inference audit trail

#### Compliance Reporters (AGPL-3.0)
- `MiFID2Reporter` — MiFID II RTS 24 Annex I report generation (JSON/NDJSON/CSV)
- `EUAIActReporter` — EU AI Act Articles 12, 13, and 19 conformity assessment

#### Python Bindings
- pybind11 bindings with 44/44 C++ API exports
- NumPy array integration for all column types
- Full audit/compliance API bindings (gated by `SIGNET_HAS_AI_AUDIT`)
- 35 pytest test functions

#### CLI Tool
- `signet_cli` — Parquet file inspection (schema, row groups, statistics, metadata)

#### Build System
- 12 CMake presets: dev, dev-tests, release, asan, tsan, ubsan, msan, ci, benchmarks, python, minimal, server, server-pq
- Header-only core with zero mandatory dependencies
- FetchContent integration (Catch2 3.7.1 for tests, pybind11 for Python)
- `SIGNET_MINIMAL_DEPS` one-flag embedded build mode

#### CI/CD
- GitHub Actions with 7 matrix jobs: build-test (Ubuntu + macOS), ASan, TSan, UBSan, Windows MSVC, server codecs (ZSTD+LZ4+Gzip), post-quantum (liboqs)
- Concurrency control with cancel-in-progress

#### Examples
- `basic_write.cpp` — minimal write example
- `basic_read.cpp` — minimal read example
- `csv_to_parquet.cpp` — generic CSV to Parquet converter
- `ticks_import.cpp` — HFT tick data CSV.gz to Parquet with optimal encodings
- `ticks_query.cpp` — HFT tick data query with predicate pushdown
- `ticks_wal_stream.cpp` — HFT tick data WAL durable ingestion to Parquet compaction

#### Benchmarks
- 37 benchmark cases across 6 files: write throughput, read throughput, WAL latency (fwrite + mmap), encoding speed, feature store latency, MPMC ring throughput

#### Documentation
- 9 internal architecture documents (overview, build system, data structures, Parquet format, encryption, encoding codecs, AI subsystem, thread model, PQ-PME integration)
- 7 user-facing documents (quickstart, API reference, applications, compression guide, PQ crypto guide, AI features, production setup)
- Production README with feature matrix, quickstart, benchmark table, architecture diagram

### Security

- **Hardening Pass 1**: Fixed 6 vulnerabilities — BSS decode OOB reads, RLE bit_width boundary values (0 and 65), Thrift nesting depth exhaustion, bad/truncated Parquet magic bytes, Python write_column OOB, path traversal in DecisionLogWriter
- **Hardening Pass 2**: Fixed 6 additional vulnerabilities — WAL oversize record cap (64 MB), FeatureWriter path traversal, Parquet page size bomb cap (256 MB), Thrift field-count DoS cap (65536), Thrift string bomb cap (64 MB), ArrowArray offset overflow
- **Hardening Pass 3**: Fixed 23 additional vulnerabilities across 5 batches (~20 files modified):
  - *Encoding*: RLE bit_width clamped to [0,64] (prevents UB on shift > 64), Dictionary decoder OOB index (assert→runtime check), BSS count×WIDTH integer overflow (division-based guard), Delta decoder accumulation overflow (__builtin_add_overflow)
  - *Thrift*: Negative list count rejection, MAP/LIST/SET collection size DoS cap (1M entries)
  - *Crypto*: Replaced std::random_device byte-by-byte IV generation with platform CSPRNG (arc4random_buf on macOS, getrandom on Linux), GCM counter overflow guard per NIST SP 800-38D (~64 GB limit), TLV field size overflow guard (64 MB cap), AES-256 round keys zeroed in destructor via volatile pointer, cipher adapter key vectors zeroed on destruction
  - *Interop*: Arrow bridge offset×elem_size overflow check, raw new→std::make_unique RAII for ArrowSchemaPrivate/ArrowArrayPrivate
  - *AI*: TensorView assert()→throw std::out_of_range in production, string size truncation in decision/inference log append_string, FeatureWriter symlink bypass (weakly_canonical + raw path check)
  - *Compliance*: Field length truncation (MAX_FIELD_LENGTH=4096) in MiFID2 and EU AI Act reporters, 64-bit time_t static_assert for timestamps beyond 2038
  - *WAL*: Empty record rejection, verify_chain() early return on hash deserialization failure
  - *Post-quantum*: Refined #pragma message warning (acknowledges X25519 HybridKem provides real classical ECDH; only Kyber lattice portion is stubbed), added is_real_pq_crypto() runtime query
  - New header: `crypto/cipher_interface.hpp` — platform-aware CSPRNG + shared cipher adapters
- **Hardening Pass 4**: Fixed 29 additional vulnerabilities across 7 batches (all language bindings):
  - *Core C++*: Arena allocator overflow guards (count×sizeof, size+alignment), signed index overflow in column/offset index, dictionary page num_values validation, batch read count overflow in column_reader, INT64_MIN negation UB in delta encoder (cast to unsigned), page size >2GiB truncation guard, decompression ratio bomb limit (1024:1), NaN exclusion from statistics, schema column() bounds checking with std::out_of_range
  - *Crypto*: AES-CTR counter overflow guard (64 GiB limit matching AES-GCM)
  - *AI tier*: Feature/embedding count bounds checking in decision_log/inference_log deserialization, stoll try/catch in audit metadata parsing, StreamingSink output_dir path traversal guard, ColumnBatch 100M cell OOM limit
  - *C FFI*: try/catch around 5 extern "C" functions (signet_writer_open, write_column_string, write_row, reader_open, read_column_string) preventing C++ exception UB across FFI boundary, signet_schema_builder_free() for proper cleanup, string column read OOM cleanup on partial failure
  - *Rust FFI*: SchemaBuilder::new() returns Result<Self> with null check (was infallible), build() only forgets self on success, Drop uses signet_schema_builder_free, write_column_string panic→Result, WriterOptions null allocation assert
  - *WASM*: writeFileToMemfs 256 MB file size limit, schema column accessor bounds checks, writer column bounds checks, encryption key material zeroing after use (volatile write)
  - *Python*: __init__.py graceful degradation for AI audit types (try/except ImportError), write_column_bool bounds check parity with other write_column methods
  - *Keygen*: parse_hex_hash bare "0x" rejection, expiry_date overflow clamp [1,36500 days], semicolon injection prevention in custom claims
- 423 total unit tests + 5 Rust integration tests + 5 doc-compile tests, all passing across all 5 hardening passes plus static audit follow-up

[Unreleased]: https://github.com/SIGNETSTACK/signet-forge/compare/v0.1.1...HEAD
[0.1.1]: https://github.com/SIGNETSTACK/signet-forge/compare/v0.1.0...v0.1.1
[0.1.0]: https://github.com/SIGNETSTACK/signet-forge/releases/tag/v0.1.0
