// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file forge.hpp
/// @brief Single-include umbrella header for the Signet Forge library.
///
/// Including this one file provides access to the complete Signet Forge API:
/// core Parquet reader/writer, all encodings (RLE, dictionary, delta,
/// byte-stream split), compression codecs (Snappy bundled; ZSTD, LZ4, Gzip
/// optional), Parquet Modular Encryption with optional post-quantum support,
/// bloom filters, column/offset indexes, memory-mapped I/O, AI-native
/// extensions (vector types, tensor bridge, WAL, streaming sink, feature
/// store, MPMC event bus), interop bridges (Arrow, ONNX, NumPy/DLPack),
/// and BSL 1.1-licensed AI audit/compliance modules (when enabled).
///
/// @code
///   #include "signet/forge.hpp"
///   using namespace signet::forge;
/// @endcode
///
/// @note Commercial AI audit and compliance modules (audit_chain, decision_log,
///       inference_log, MiFID II/EU AI Act reporters) are conditionally included
///       only when `SIGNET_ENABLE_COMMERCIAL` is defined and non-zero.
/// @note The mmap_reader module is excluded on Windows (`_WIN32`) and Emscripten (`__EMSCRIPTEN__`).
/// @note The streaming_sink and wal_mapped_segment modules are excluded on Emscripten
///       (they require `std::thread`, unavailable in single-threaded WASM).

/// @name Core: error handling, types, schema, statistics, memory, Thrift, column I/O
/// @{
#include "signet/error.hpp"
#include "signet/types.hpp"
#include "signet/schema.hpp"
#include "signet/statistics.hpp"
#include "signet/memory.hpp"
#include "signet/thrift/compact.hpp"
#include "signet/thrift/types.hpp"
#include "signet/column_writer.hpp"
#include "signet/column_reader.hpp"
/// @}

/// @name Encodings: RLE, dictionary, delta, byte-stream split
/// @{
#include "signet/encoding/rle.hpp"
#include "signet/encoding/dictionary.hpp"
#include "signet/encoding/delta.hpp"
#include "signet/encoding/byte_stream_split.hpp"
/// @}

/// @name Z-Ordering: spatial sort keys for multi-column range queries
/// @{
#include "signet/z_order.hpp"
/// @}

/// @name Compression: Snappy (bundled) + optional ZSTD, LZ4, Gzip
/// @{
#include "signet/compression/codec.hpp"
#include "signet/compression/snappy.hpp"
#include "signet/compression/zstd.hpp"
#include "signet/compression/lz4.hpp"
#include "signet/compression/gzip.hpp"
/// @}

/// @name Crypto: AES-GCM/CTR, Parquet Modular Encryption, post-quantum (Kyber/Dilithium)
/// @{
#include "signet/crypto/aes_core.hpp"
#include "signet/crypto/aes_gcm.hpp"
#include "signet/crypto/aes_ctr.hpp"
#include "signet/crypto/key_metadata.hpp"
#include "signet/crypto/cipher_interface.hpp"
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
#include "signet/crypto/pme.hpp"
#include "signet/crypto/post_quantum.hpp"
#endif
/// @}

/// @name Bloom Filters: xxHash64 + Split Block Bloom Filter (Parquet spec)
/// @{
#include "signet/bloom/xxhash.hpp"
#include "signet/bloom/split_block.hpp"
/// @}

/// @name Page Index + Memory-Mapped I/O
/// @{
#include "signet/column_index.hpp"
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include "signet/mmap_reader.hpp"
#endif
/// @}

/// @name AI-Native: vector types, quantized vectors, tensor bridge (Apache 2.0)
/// @{
#include "signet/ai/vector_type.hpp"
#include "signet/ai/quantized_vector.hpp"
#include "signet/ai/tensor_bridge.hpp"
/// @}

/// @name Streaming WAL + Async Compaction (Apache 2.0)
/// @{
#include "signet/ai/wal.hpp"
#ifndef __EMSCRIPTEN__
#include "signet/ai/streaming_sink.hpp"
#endif
/// @}

/// @name Feature Store: point-in-time feature retrieval (Apache 2.0)
/// @{
#include "signet/ai/feature_writer.hpp"
#include "signet/ai/feature_reader.hpp"
/// @}

/// @name MPMC ColumnBatch Event Bus (Apache 2.0)
/// @{
#include "signet/ai/mpmc_ring.hpp"
#include "signet/ai/column_batch.hpp"
#include "signet/ai/event_bus.hpp"
/// @}

/// @name AI Audit and Compliance (BSL 1.1) -- requires SIGNET_ENABLE_COMMERCIAL
/// @{
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
#  include "signet/ai/audit_chain.hpp"
#  include "signet/ai/row_lineage.hpp"
#  include "signet/ai/decision_log.hpp"
#  include "signet/ai/inference_log.hpp"
#  include "signet/ai/compliance/compliance_types.hpp"
#  include "signet/ai/compliance/mifid2_reporter.hpp"
#  include "signet/ai/compliance/eu_ai_act_reporter.hpp"
#endif
/// @}

/// @name Interop Bridges: Arrow C Data Interface, ONNX Runtime, NumPy/DLPack
/// @{
#include "signet/interop/onnx_bridge.hpp"
#include "signet/interop/arrow_bridge.hpp"
#include "signet/interop/numpy_bridge.hpp"
/// @}

/// @name Top-Level Reader and Writer
/// @{
#include "signet/writer.hpp"
#include "signet/reader.hpp"
/// @}
