# Benchmark ID Registry

This document is generated from `Benchmarking_Protocols/benchmark_id_registry.tsv`.
Do not edit this Markdown file by hand. Update the registry source and rerender it.

## Governance
1. Benchmark IDs are global across tracked benchmark suites in `benchmarks/` and `Benchmarking_Protocols/`.
2. Benchmark IDs are immutable once published, unless explicitly deprecated and replaced.
3. Every tracked `BENCHMARK(...)` or `BENCHMARK_ADVANCED(...)` label must exist in the registry.
4. Generic labels such as `write`, `read_all`, or `delta encode` are not permitted.
5. Micro benchmark IDs in `benchmarks/*.cpp` must be 30 characters or fewer so Catch2 console output cannot wrap them in the GitHub benchmark parser path.
6. `status=active` entries must exist in code. `deprecated` and `reserved` entries are retained for history and future planning.
7. The CI validator is the enforcement point. A benchmark change is incomplete until the registry and generated documentation are updated.

## Registry Source
- `Benchmarking_Protocols/benchmark_id_registry.tsv`

## Protocol Benchmarks
| ID | Suite | File | Status |
|---|---|---|---|
| `RT1: roundtrip 1M PLAIN` | `phase10_roundtrip` | `Benchmarking_Protocols/bench_phase10_roundtrip.cpp` | `active` |
| `RT2: roundtrip 1M optimal Snappy` | `phase10_roundtrip` | `Benchmarking_Protocols/bench_phase10_roundtrip.cpp` | `active` |
| `RT3: roundtrip 1M PME` | `phase10_roundtrip` | `Benchmarking_Protocols/bench_phase10_roundtrip.cpp` | `active` |
| `RT4: roundtrip 1M PQ` | `phase10_roundtrip` | `Benchmarking_Protocols/bench_phase10_roundtrip.cpp` | `active` |
| `W1: 1K PLAIN uncompressed` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W2: 100K PLAIN uncompressed` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W3: 1M PLAIN uncompressed` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W4: 10M PLAIN uncompressed` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W5: 1M PLAIN Snappy` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W6: 1M PLAIN ZSTD` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W7: 1M PLAIN LZ4` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W8: 1M PLAIN Gzip` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W9: 1M optimal Snappy` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W10: 1M optimal ZSTD` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W11: 1M optimal Snappy PME` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W12: 1M optimal Snappy PQ` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W13: 10M optimal Snappy` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `W14: 10M optimal Snappy PME` | `phase2_write` | `Benchmarking_Protocols/bench_phase2_write.cpp` | `active` |
| `R1: read all 1M PLAIN` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R2: projection bid+ask 1M` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R3: read 1M optimal Snappy` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R4: mmap read 1M optimal Snappy` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R5: typed read 1M optimal Snappy` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R6: read 1M optimal ZSTD` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R7: read 1M PME` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R8: read 1M PQ` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R9: read all 10M PLAIN` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `R10: read 10M optimal Snappy` | `phase3_read` | `Benchmarking_Protocols/bench_phase3_read.cpp` | `active` |
| `P1: filter symbol 1M` | `phase4_predicate` | `Benchmarking_Protocols/bench_phase4_predicate.cpp` | `active` |
| `P2: filter symbol+exchange 1M` | `phase4_predicate` | `Benchmarking_Protocols/bench_phase4_predicate.cpp` | `active` |
| `P3: time range filter 1M` | `phase4_predicate` | `Benchmarking_Protocols/bench_phase4_predicate.cpp` | `active` |
| `P4: bloom probe 1M` | `phase4_predicate` | `Benchmarking_Protocols/bench_phase4_predicate.cpp` | `active` |
| `P5: filter symbol 10M` | `phase4_predicate` | `Benchmarking_Protocols/bench_phase4_predicate.cpp` | `active` |
| `WAL1: 100K WalWriter` | `phase5_wal` | `Benchmarking_Protocols/bench_phase5_wal.cpp` | `active` |
| `WAL2: 100K WalMmapWriter` | `phase5_wal` | `Benchmarking_Protocols/bench_phase5_wal.cpp` | `active` |
| `WAL3: 100K WalManager` | `phase5_wal` | `Benchmarking_Protocols/bench_phase5_wal.cpp` | `active` |
| `WAL4: 1M WalWriter` | `phase5_wal` | `Benchmarking_Protocols/bench_phase5_wal.cpp` | `active` |
| `WAL5: 1M WalMmapWriter` | `phase5_wal` | `Benchmarking_Protocols/bench_phase5_wal.cpp` | `active` |
| `WAL6: 1M WAL write + read_all` | `phase5_wal` | `Benchmarking_Protocols/bench_phase5_wal.cpp` | `active` |
| `WAL7: 100K WalWriter (encrypted)` | `phase5_wal` | `Benchmarking_Protocols/bench_phase5_wal.cpp` | `active` |
| `FS1: write 100K vectors` | `phase6_features` | `Benchmarking_Protocols/bench_phase6_features.cpp` | `active` |
| `FS2: get latest 1000x` | `phase6_features` | `Benchmarking_Protocols/bench_phase6_features.cpp` | `active` |
| `FS3: as_of 1000x` | `phase6_features` | `Benchmarking_Protocols/bench_phase6_features.cpp` | `active` |
| `FS4: as_of_batch 6 entities` | `phase6_features` | `Benchmarking_Protocols/bench_phase6_features.cpp` | `active` |
| `FS5: history 1000 records` | `phase6_features` | `Benchmarking_Protocols/bench_phase6_features.cpp` | `active` |
| `AI1: DecisionLog 10K records` | `phase7_ai` | `Benchmarking_Protocols/bench_phase7_ai.cpp` | `active` |
| `AI2: InferenceLog 10K records` | `phase7_ai` | `Benchmarking_Protocols/bench_phase7_ai.cpp` | `active` |
| `AI3: verify_chain` | `phase7_ai` | `Benchmarking_Protocols/bench_phase7_ai.cpp` | `active` |
| `AI4: column_view 1M doubles` | `phase7_ai` | `Benchmarking_Protocols/bench_phase7_ai.cpp` | `active` |
| `AI5: EventBus 4P4C 100K` | `phase7_ai` | `Benchmarking_Protocols/bench_phase7_ai.cpp` | `active` |
| `CR1: MiFID2 JSON` | `phase8_compliance` | `Benchmarking_Protocols/bench_phase8_compliance.cpp` | `active` |
| `CR2: MiFID2 NDJSON` | `phase8_compliance` | `Benchmarking_Protocols/bench_phase8_compliance.cpp` | `active` |
| `CR3: MiFID2 CSV` | `phase8_compliance` | `Benchmarking_Protocols/bench_phase8_compliance.cpp` | `active` |
| `CR4: EU AI Act Art.12` | `phase8_compliance` | `Benchmarking_Protocols/bench_phase8_compliance.cpp` | `active` |
| `CR5: EU AI Act Art.13` | `phase8_compliance` | `Benchmarking_Protocols/bench_phase8_compliance.cpp` | `active` |
| `CR6: EU AI Act Art.19` | `phase8_compliance` | `Benchmarking_Protocols/bench_phase8_compliance.cpp` | `active` |
| `I1: Arrow export 1M doubles` | `phase9_interop` | `Benchmarking_Protocols/bench_phase9_interop.cpp` | `active` |
| `I2: tensor wrap 1M doubles` | `phase9_interop` | `Benchmarking_Protocols/bench_phase9_interop.cpp` | `active` |
| `I3: batch tensor 1M x 6` | `phase9_interop` | `Benchmarking_Protocols/bench_phase9_interop.cpp` | `active` |

## Micro Benchmarks
| ID | Suite | File | Status |
|---|---|---|---|
| `enc_delta_enc_ts_10k` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_delta_dec_ts_10k` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_bss_enc_px_10k` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_bss_dec_px_10k` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_rle_enc_bw1_10k` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_rle_dec_bw1_10k` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_delta_ts_10k_plain` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_plain_copy_ts_10k` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_bss_enc_px_10k_sz` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `enc_bss_dec_px_10k_sz` | `encodings` | `benchmarks/bench_encodings.cpp` | `active` |
| `ring_push_pop_1t` | `event_bus` | `benchmarks/bench_event_bus.cpp` | `active` |
| `ring_batch_4p4c_4k` | `event_bus` | `benchmarks/bench_event_bus.cpp` | `active` |
| `batch_push_1k_view` | `event_bus` | `benchmarks/bench_event_bus.cpp` | `active` |
| `batch_tensor_1024x8` | `event_bus` | `benchmarks/bench_event_bus.cpp` | `active` |
| `bus_pub_pop_1k_1t` | `event_bus` | `benchmarks/bench_event_bus.cpp` | `active` |
| `feat_wr_batch_10k` | `feature_store` | `benchmarks/bench_feature_store.cpp` | `active` |
| `feat_get_latest_1k` | `feature_store` | `benchmarks/bench_feature_store.cpp` | `active` |
| `feat_asof_mid_1k` | `feature_store` | `benchmarks/bench_feature_store.cpp` | `active` |
| `feat_asof_batch_100e` | `feature_store` | `benchmarks/bench_feature_store.cpp` | `active` |
| `feat_hist_100` | `feature_store` | `benchmarks/bench_feature_store.cpp` | `active` |
| `rd_price_f64_50k` | `read` | `benchmarks/bench_read.cpp` | `active` |
| `rd_all_str_50k` | `read` | `benchmarks/bench_read.cpp` | `active` |
| `rd_proj_px_qty_50k` | `read` | `benchmarks/bench_read.cpp` | `active` |
| `rd_ts_i64_50k` | `read` | `benchmarks/bench_read.cpp` | `active` |
| `rd_footer_rows_50k` | `read` | `benchmarks/bench_read.cpp` | `active` |
| `wal_writer_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_writer_256b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_writer_1k_batch` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_writer_flush_nofs` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_mgr_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_read_all_10k` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_mmap_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_mmap_256b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_mmap_1k_batch` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_mmap_flush_noms` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_cmp_fwrite_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_cmp_mmap_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_3way_writer_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_3way_mgr_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wal_3way_mmap_32b` | `wal` | `benchmarks/bench_wal.cpp` | `active` |
| `wr_i64_10k_raw` | `write` | `benchmarks/bench_write.cpp` | `active` |
| `wr_f64_10k_bss_raw` | `write` | `benchmarks/bench_write.cpp` | `active` |
| `wr_mix5_10k_raw` | `write` | `benchmarks/bench_write.cpp` | `active` |
| `wr_i64_f64_100k_10rgs` | `write` | `benchmarks/bench_write.cpp` | `active` |
| `wr_str_10k_dict_raw` | `write` | `benchmarks/bench_write.cpp` | `active` |
