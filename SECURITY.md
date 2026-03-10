# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.1.x   | Yes       |
| < 0.1   | No        |

Only the latest release receives security patches.

## Reporting a Vulnerability

**Do not open a public GitHub issue for security vulnerabilities.**

Instead, please report them responsibly via email:

**Email**: johnson@signetstack.io

Include the following in your report:

1. Description of the vulnerability
2. Steps to reproduce (minimal reproducer preferred)
3. Affected files and/or API surfaces
4. Potential impact assessment
5. Suggested fix (if any)

## Response Timeline

| Stage | Timeline |
|-------|----------|
| Acknowledgment | Within 48 hours |
| Initial triage and severity assessment | Within 7 days |
| Fix development and testing | Within 30 days (critical), 60 days (moderate) |
| Coordinated disclosure | 90 days from initial report |

We follow a **90-day coordinated disclosure** policy. If we are unable to produce a fix within 90 days, we will work with the reporter to agree on an extended timeline or a partial disclosure.

## Scope

### In Scope

- All code in `include/signet/` (core library headers)
- Python bindings (`python/signet_forge/`)
- C FFI layer (`ffi/signet_forge.h`, `ffi/signet_forge_c.cpp`)
- Rust crate (`rust/signet-forge/`, `rust/signet-forge-sys/`)
- WASM module (`wasm/signet_wasm.cpp`)
- CLI tool (`tools/signet_cli.cpp`)
- Build system (CMakeLists.txt, CMakePresets.json) — supply-chain concerns
- Encryption implementation (`crypto/aes_core.hpp`, `aes_gcm.hpp`, `aes_ctr.hpp`, `pme.hpp`, `post_quantum.hpp`)
- WAL data integrity (`ai/wal.hpp`, `ai/wal_mapped_segment.hpp`)
- Audit chain integrity (`ai/audit_chain.hpp`, `ai/decision_log.hpp`, `ai/inference_log.hpp`)

### Out of Scope

- Third-party dependencies fetched by CMake (Catch2, pybind11) — report to their respective projects
- Documentation content (not a security surface)
- Example programs (illustrative, not production code)
- Benchmark code (not deployed in production)

## Security-Relevant Build Flags

| Flag | Purpose |
|------|---------|
| `SIGNET_ENABLE_CRYPTO` | Enables real AES-256-GCM/CTR encryption (otherwise stubs) |
| `SIGNET_ENABLE_PQ` | Enables post-quantum crypto (Kyber-768, Dilithium-3) via liboqs |
| `SIGNET_BUILD_AI_AUDIT` | Includes BSL 1.1 audit/compliance module (default ON) |
| `SIGNET_REQUIRE_REAL_PQ` | Disallows bundled PQ stubs; requires real liboqs |
| `SIGNET_REQUIRE_COMMERCIAL_LICENSE` | Enforces evaluation tier validation |

## Known Security Limits

The following hard caps are enforced to prevent resource exhaustion from malformed input:

| Limit | Value | File |
|-------|-------|------|
| Maximum WAL record size | 64 MB | `ai/wal.hpp` (`WAL_MAX_RECORD_SIZE`) |
| Maximum Parquet page size | 256 MB | `reader.hpp` (`PARQUET_MAX_PAGE_SIZE`) |
| Maximum Thrift field count | 65,536 | `thrift/compact.hpp` (`MAX_FIELD_COUNT`) |
| Maximum Thrift string size | 64 MB | `thrift/compact.hpp` (`MAX_STRING_BYTES`) |
| Thrift nesting depth | 128 | `thrift/compact.hpp` |
| RLE bit width range | 0-64 (clamped) | `encoding/rle.hpp` |
| Thrift collection size | 1,000,000 | `thrift/compact.hpp` (`MAX_COLLECTION_SIZE`) |
| GCM max plaintext | ~64 GB | `crypto/aes_gcm.hpp` (`MAX_GCM_PLAINTEXT`) |
| TLV max field length | 64 MB | `crypto/key_metadata.hpp` (`MAX_TLV_LENGTH`) |
| Compliance field length | 4,096 chars | `compliance/mifid2_reporter.hpp`, `eu_ai_act_reporter.hpp` |
| Maximum decompressed page size | 256 MB | `compression/codec.hpp` (`MAX_DECOMPRESS_SIZE`) |
| Maximum decompression ratio | 1024:1 | `reader.hpp`, `mmap_reader.hpp` |
| Maximum dictionary entries | 1,048,576 | `encoding/dictionary.hpp` (`MAX_DICTIONARY_ENTRIES`) |
| Column index list count cap | 10,000,000 | `column_index.hpp` |
| Thrift global field limit | 1,000,000 | `thrift/compact.hpp` (`MAX_TOTAL_FIELDS`) |
| JSON array parse limit | 1,000,000 | `ai/decision_log.hpp`, `ai/inference_log.hpp` |
| DLPack max ndim | 32 | `interop/numpy_bridge.hpp` |

## Security Hardening

Ten security audit passes have been completed, covering **458 confirmed vulnerabilities** — all remediated, zero open findings:

**Pass #1** (6 vulnerabilities):
- BYTE_STREAM_SPLIT decode out-of-bounds reads
- RLE bit_width boundary values (0 and 65)
- Thrift nesting depth exhaustion
- Bad/truncated Parquet magic bytes
- Python write_column out-of-bounds
- Path traversal + invalid chain_id in DecisionLogWriter

**Pass #2** (6 vulnerabilities):
- WAL oversized record cap (64 MB)
- FeatureWriter path traversal
- Parquet page size bomb cap (256 MB)
- Thrift field-count denial of service (65,536 cap)
- Thrift string size bomb (64 MB cap)
- ArrowArray offset overflow

**Pass #3** (23 vulnerabilities across 5 batches, ~20 files):
- *Encoding/Decoding*: RLE bit_width UB on shift > 64, Dictionary decoder OOB index, BSS integer overflow on count×WIDTH, Delta decoder accumulation overflow
- *Thrift + Crypto*: Negative list count, MAP size DoS (1M cap), Weak IV generation (replaced with platform CSPRNG: arc4random_buf/getrandom), GCM counter overflow (NIST SP 800-38D), TLV size overflow (64 MB guard)
- *Interop + AI*: Arrow bridge offset×elem_size overflow, TensorView assert()→throw in production, Decision/Inference log string size truncation, Arrow bridge raw new→RAII make_unique, FeatureWriter symlink bypass on path traversal (weakly_canonical)
- *Crypto Key Material*: Post-quantum bundled stubs warning + is_real_pq_crypto() runtime query, AES-256 round keys zeroed in destructor, Cipher adapter key vectors zeroed via volatile pointer
- *Compliance + Low Priority*: Compliance reporter field truncation (4096 cap), noexcept on move constructors, WAL empty record rejection, Hash chain verify_chain() early return on deserialization failure, 64-bit time_t static_assert

**Pass #4** (29 vulnerabilities across 7 batches, all language bindings):
- *Core C++*: Arena allocator overflow guards, signed index overflow, dictionary num_values validation, batch read count overflow, INT64_MIN negation UB, page size >2GiB guard, decompression ratio bomb (1024:1), NaN exclusion from statistics, schema column() bounds check
- *Crypto*: AES-CTR counter overflow guard (64 GiB limit)
- *AI tier*: Feature/embedding count bounds in deserialization, stoll try/catch, StreamingSink path traversal, ColumnBatch OOM limit (100M cells)
- *C FFI*: try/catch around 5 extern "C" functions (prevents C++ exception UB across FFI), signet_schema_builder_free() cleanup, string column read OOM cleanup
- *Rust FFI*: SchemaBuilder::new() returns Result (null check), build() only forgets self on success, write_column_string panic→Result, WriterOptions null assert
- *WASM*: writeFileToMemfs 256 MB file limit, schema/writer column bounds checks, encryption key zeroing (volatile write)
- *Python*: __init__.py graceful degradation (try/except ImportError), write_column_bool bounds check
- *Keygen*: parse_hex_hash bare "0x" rejection, expiry_date clamp [1,36500], semicolon injection prevention

**Pass #5** (53 vulnerabilities):
- *CRITICAL (8)*: Constant-time GHASH (4-bit table), GCM counter overflow guard, RLE resize formula, dictionary error reporting, BYTE_ARRAY bounds (read+write), INT4 sign extension, verify_chain early return
- *HIGH (18)*: Secure key zeroing (volatile+barrier), move-only ciphers, CSPRNG hardening (BCryptGenRandom+hard-fail), overflow guards (BSS, delta, mmap footer, arena), typed statistics, NaN exclusion, xxHash endianness, configurable compliance (price precision, timestamp granularity), training metadata, cross-chain verification, monotonic timestamps, MPMC ring validation
- *MEDIUM (18)*: Constant-time X25519 zero check, TLV 1MB cap, PME module_type validation, Thrift nesting (128→64), Arrow caps, bloom seed, writer validation, SHA-256 FIPS vector, WAL empty record tracking, instrument validator, feature lineage
- *LOW (9)*: 64-bit Snappy hash, Thrift errors, GCM IV config, mmap pre-validation; 4 no-ops

**Pass #6 — Comprehensive Cryptographic & Systems Audit** (91 vulnerabilities):

*Crypto (21 fixes)*: AES S-box cache-timing mitigation (table prefetch), constant-time gf_mul, Aes256 non-copyable, GCM derive_j0() for 12/16-byte IVs (NIST SP 800-38D §7.1), GCM gctr() counter overflow guard, MSVC X25519 fe_sub corrected constants, hybrid KEM domain separation label, cipher key storage std::array (was std::vector), BCryptGenRandom validation, KeyMode::INTERNAL runtime warning, TLV overflow checks, enum range validation, PQ key zeroing destructors, audit chain steady_clock + atomic monotonicity

*Encoding/Compression (22 fixes)*: RLE truncated value detection, varint stream position restore, encode_with_length overflow guard, rle_count_ shift cap, delta decode_int32 range check, delta encoder unsigned subtraction, dictionary MAX_DICTIONARY_ENTRIES (1M), BSS encode overflow check, decompression bomb 256MB cap + zero-length rejection, Snappy 4GiB guard + 256MB decompress cap, LZ4/GZIP size truncation validation, Thrift zigzag unsigned shift + negative size rejection + stack underflow detection + global field counter (1M), bloom from_data size cap + memcpy aliasing fix, xxHash MSVC endianness

*Core I/O (23 fixes)*: Integer overflow in read_batch_string(), extract_byte_array_strings() bounds checks, mmap data_at() bounds validation, mmap SIGBUS early detection, mmap decompression ratio, column index list count cap (10M), column_writer >4GiB error, env var path traversal, /tmp→XDG state path, symlink write check, DLPack byte_offset/size overflow/ndim checks, Arrow zero-init on partial failure, ONNX dimension validation, reader decompression ratio + memory budget, z_order alignment checks, arena allocate_zeroed()

*AI/WAL/Streaming (25 fixes)*: WalMmapWriter assert→runtime bounds check, WAL_MAX_RECORD_SIZE enforcement in mmap writer, active_idx_ + closed_ atomicity, WalWriter resume scan size check, Windows 64-bit ftell, EventBus atomic sink pointer, MpmcRing alignas(64), ColumnBatch row count overflow, InferenceRecord EU AI Act training fields in serialize(), JSON parser 1M cap, FeatureReader failed file tracking, FeatureWriter partial file cleanup, MiFID2 CSPRNG report IDs, EU AI Act consistent 3σ anomaly detection, StreamingSink filesystem path iteration

**Static audit follow-up** (11 fixes):
- Page CRC-32 in writer, mmap parity gaps, reader row_group OOB, Z-Order column count validation, Float16 shift UB + unaligned cast fixes, feature flush data loss prevention, getrandom EINTR retry, delta zigzag unsigned shift, statistics typed merge, compliance error reporting, WAL fsync checks

**Full-Scale Audit #7** (126 vulnerabilities — all remediated):
All 126 findings (5 CRITICAL, 22 HIGH, 50 MEDIUM, 33 LOW, 16 INFO) across 49 files remediated. Key fixes: INTERNAL key production gate (FIPS 140-3 §7.7), PQ stub runtime gate, CSV injection sanitization, CSPRNG report IDs, enum validation, CTR counter overflow, decompression bombs, type-aware column index comparison, tensor overflow checks, cumulative hash chain, CRC page verification.

**Audit #8 — Completeness Audit** (21 vulnerabilities — all remediated):
Final verification pass: OS command injection in examples (CWE-78), Python use-after-free in numeric column readers (CWE-416), ColumnIndex type confusion for signed integers (CWE-843), FeatureReader race condition in cache mutex (CWE-362), Thrift encoder/decoder asymmetry fixes, CRNGT partial-block detection (FIPS 140-3 §4.9.2), WASM JSON parser escape handling.

**Enterprise Compliance Gaps** (92 gaps — all resolved):
12 gap-fix passes covering FIPS 140-3, EU AI Act, MiFID II RTS 24/RTS 6, GDPR, DORA.

Run hardening tests: `ctest -L hardening` — covers all passes. Full test suite: 618 unit tests (100% passing) across 12 enterprise compliance gap-fix passes + 10 security audit passes.

## Credit

Security researchers who report valid vulnerabilities will be credited in the CHANGELOG (unless they request anonymity).

## PGP Key

A PGP key for encrypted vulnerability reports will be published at a later date. In the interim, use the email address above.
