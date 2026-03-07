// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/audit_chain.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

// ---------------------------------------------------------------------------
// audit_chain.hpp -- Cryptographic hash chain for tamper-evident audit trails
//
// Provides a SHA-256-based hash chain that guarantees the integrity and
// ordering of AI decision records stored in Parquet files. Each entry in
// the chain commits to all previous entries through a cryptographic link,
// making any retrospective tampering (insertion, deletion, reordering, or
// modification of records) computationally detectable.
//
// Architecture:
//
//   AuditChainWriter   -- Builds hash chains during Parquet writes. Each
//                         record is hashed and linked to its predecessor.
//
//   AuditChainVerifier -- Verifies chain integrity during reads. Can
//                         verify individual entries, entire chains, or
//                         continuity across multiple Parquet files.
//
//   AuditMetadata      -- Stores chain summary in Parquet key-value
//                         metadata for fast cross-file verification
//                         without reading every entry.
//
// Hash chain algorithm:
//
//   entry_hash = SHA-256( LE64(sequence_number)
//                      || LE64(timestamp_ns)
//                      || prev_hash[32]
//                      || data_hash[32] )
//
//   where prev_hash is all zeros for the first entry (or a user-supplied
//   continuation hash when chaining across files).
//
// Binary serialization (112 bytes per entry, little-endian):
//
//   [0:8)    sequence_number  (int64_t LE)
//   [8:16)   timestamp_ns     (int64_t LE)
//   [16:48)  prev_hash        (32 bytes)
//   [48:80)  data_hash        (32 bytes)
//   [80:112) entry_hash       (32 bytes)
//
// Chain serialization: 4-byte entry count (uint32_t LE) followed by
// N entries of 112 bytes each.
//
// Header-only. No external dependencies beyond the project's own SHA-256
// implementation in signet/crypto/post_quantum.hpp.
//
// Part of SignetStack Signet Forge -- Phase 7: AI Decision Audit Trail.
// ---------------------------------------------------------------------------

#include "signet/crypto/post_quantum.hpp"
#include "signet/error.hpp"
#include "signet/types.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace signet::forge {

// ===========================================================================
// Forward declarations
// ===========================================================================
struct HashChainEntry;    ///< A single link in the cryptographic hash chain.
class  AuditChainWriter;  ///< Builds hash chains during Parquet writes.
class  AuditChainVerifier; ///< Verifies hash chain integrity.
struct AuditMetadata;      ///< Chain summary stored in Parquet key-value metadata.

// ===========================================================================
// Constants
// ===========================================================================

/// Size of a single serialized HashChainEntry in bytes.
inline constexpr size_t HASH_CHAIN_ENTRY_SIZE = 112;

// ===========================================================================
// Utility functions
// ===========================================================================

/// Return the current time as nanoseconds since the Unix epoch.
///
/// Uses std::chrono::high_resolution_clock for the best available
/// resolution on the platform. On most systems this is nanosecond
/// or better.
inline int64_t now_ns() {
    auto tp = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  tp.time_since_epoch());
    return static_cast<int64_t>(ns.count());
}

/// Convert a 32-byte SHA-256 hash to a lowercase hexadecimal string (64 chars).
inline std::string hash_to_hex(const std::array<uint8_t, 32>& hash) {
    static constexpr char hex_chars[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (uint8_t byte : hash) {
        result.push_back(hex_chars[(byte >> 4) & 0x0F]);
        result.push_back(hex_chars[ byte       & 0x0F]);
    }
    return result;
}

/// Convert a 64-character lowercase hex string back to a 32-byte hash.
///
/// Returns an error if the string is not exactly 64 hex characters.
inline expected<std::array<uint8_t, 32>> hex_to_hash(const std::string& hex) {
    if (hex.size() != 64) {
        return Error{ErrorCode::INVALID_FILE,
                     "hex_to_hash: expected 64 hex characters, got "
                     + std::to_string(hex.size())};
    }

    std::array<uint8_t, 32> hash{};
    for (size_t i = 0; i < 32; ++i) {
        char hi = hex[i * 2];
        char lo = hex[i * 2 + 1];

        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };

        int hi_val = hex_val(hi);
        int lo_val = hex_val(lo);

        if (hi_val < 0 || lo_val < 0) {
            return Error{ErrorCode::INVALID_FILE,
                         "hex_to_hash: invalid hex character at position "
                         + std::to_string(i * 2)};
        }

        hash[i] = static_cast<uint8_t>((hi_val << 4) | lo_val);
    }

    return hash;
}

/// Generate a simple chain identifier based on the current timestamp.
///
/// Format: "chain-<hex_timestamp_ns>" (e.g. "chain-1a2b3c4d5e6f7890").
/// This is NOT cryptographically random -- it is a human-readable
/// identifier for correlating chain segments across files.
inline std::string generate_chain_id() {
    int64_t ts = now_ns();
    std::ostringstream oss;
    oss << "chain-"
        << std::hex << std::setfill('0') << std::setw(16)
        << static_cast<uint64_t>(ts);
    return oss.str();
}

// ===========================================================================
// Internal helpers (detail namespace)
// ===========================================================================
namespace detail::audit {

/// Write an int64_t as 8 little-endian bytes to the output buffer.
inline void write_le64(uint8_t* dst, int64_t value) {
    auto v = static_cast<uint64_t>(value);
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
    dst[2] = static_cast<uint8_t>(v >> 16);
    dst[3] = static_cast<uint8_t>(v >> 24);
    dst[4] = static_cast<uint8_t>(v >> 32);
    dst[5] = static_cast<uint8_t>(v >> 40);
    dst[6] = static_cast<uint8_t>(v >> 48);
    dst[7] = static_cast<uint8_t>(v >> 56);
}

/// Read an int64_t from 8 little-endian bytes.
inline int64_t read_le64(const uint8_t* src) {
    uint64_t v = static_cast<uint64_t>(src[0])
               | (static_cast<uint64_t>(src[1]) << 8)
               | (static_cast<uint64_t>(src[2]) << 16)
               | (static_cast<uint64_t>(src[3]) << 24)
               | (static_cast<uint64_t>(src[4]) << 32)
               | (static_cast<uint64_t>(src[5]) << 40)
               | (static_cast<uint64_t>(src[6]) << 48)
               | (static_cast<uint64_t>(src[7]) << 56);
    return static_cast<int64_t>(v);
}

/// Write a uint32_t as 4 little-endian bytes.
inline void write_le32(uint8_t* dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
    dst[2] = static_cast<uint8_t>(value >> 16);
    dst[3] = static_cast<uint8_t>(value >> 24);
}

/// Read a uint32_t from 4 little-endian bytes.
inline uint32_t read_le32(const uint8_t* src) {
    return static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) << 8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}

} // namespace detail::audit

/// A single link in the cryptographic hash chain.
///
/// Each entry commits to:
///   - Its own data (via data_hash)
///   - All prior entries (transitively, via prev_hash)
///   - Its position in the chain (via sequence_number)
///   - When it was created (via timestamp_ns)
///
/// The entry_hash binds all of these fields together:
/// @code
///   entry_hash = SHA-256(LE64(sequence_number) || LE64(timestamp_ns)
///                        || prev_hash || data_hash)
/// @endcode
struct HashChainEntry {
    /// 0-indexed position in the chain, monotonically increasing.
    int64_t sequence_number = 0;

    /// Nanoseconds since Unix epoch when this entry was created.
    int64_t timestamp_ns = 0;

    /// SHA-256 hash of the previous entry (all zeros for the first entry,
    /// or a user-supplied continuation hash when spanning files).
    std::array<uint8_t, 32> prev_hash{};

    /// SHA-256 hash of the record/row data that this entry covers.
    std::array<uint8_t, 32> data_hash{};

    /// SHA-256 commitment over (sequence_number, timestamp_ns, prev_hash,
    /// data_hash). This is the cryptographic binding that makes the chain
    /// tamper-evident.
    std::array<uint8_t, 32> entry_hash{};

    /// Derive entry_hash from the other fields.
    ///
    /// Must be called after setting sequence_number, timestamp_ns,
    /// prev_hash, and data_hash. The AuditChainWriter calls this
    /// automatically; manual callers must invoke it explicitly.
    ///
    /// @note Builds an 80-byte preimage: LE64(seq) || LE64(ts) || prev_hash || data_hash.
    inline void compute_entry_hash() {
        // Build the preimage: 8 + 8 + 32 + 32 = 80 bytes
        uint8_t preimage[80];

        detail::audit::write_le64(preimage,      sequence_number);
        detail::audit::write_le64(preimage + 8,   timestamp_ns);
        std::memcpy(preimage + 16, prev_hash.data(),  32);
        std::memcpy(preimage + 48, data_hash.data(),  32);

        entry_hash = crypto::detail::sha256::sha256(preimage, sizeof(preimage));
    }

    /// Check that entry_hash is consistent with the other fields.
    ///
    /// Recomputes the expected entry_hash and compares it to the stored
    /// value. Uses constant-time comparison to prevent timing side channels.
    ///
    /// @return True if the entry is self-consistent (entry_hash matches recomputed hash).
    [[nodiscard]] inline bool verify() const {
        uint8_t preimage[80];

        detail::audit::write_le64(preimage,      sequence_number);
        detail::audit::write_le64(preimage + 8,   timestamp_ns);
        std::memcpy(preimage + 16, prev_hash.data(),  32);
        std::memcpy(preimage + 48, data_hash.data(),  32);

        auto expected = crypto::detail::sha256::sha256(preimage, sizeof(preimage));

        // Constant-time comparison
        uint8_t diff = 0;
        for (size_t i = 0; i < 32; ++i) {
            diff |= entry_hash[i] ^ expected[i];
        }

        return diff == 0;
    }

    /// Serialize this entry as 112 little-endian bytes.
    ///
    /// Layout:
    /// @code
    ///   [0:8)    sequence_number (int64_t LE)
    ///   [8:16)   timestamp_ns    (int64_t LE)
    ///   [16:48)  prev_hash       (32 bytes, raw)
    ///   [48:80)  data_hash       (32 bytes, raw)
    ///   [80:112) entry_hash      (32 bytes, raw)
    /// @endcode
    /// @return 112-byte vector in the canonical binary format.
    [[nodiscard]] inline std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf(HASH_CHAIN_ENTRY_SIZE);

        detail::audit::write_le64(buf.data(),      sequence_number);
        detail::audit::write_le64(buf.data() + 8,   timestamp_ns);
        std::memcpy(buf.data() + 16, prev_hash.data(),  32);
        std::memcpy(buf.data() + 48, data_hash.data(),  32);
        std::memcpy(buf.data() + 80, entry_hash.data(), 32);

        return buf;
    }

    /// Reconstruct a HashChainEntry from 112 bytes.
    ///
    /// @param data  Pointer to the serialized binary data.
    /// @param size  Number of bytes available at @p data.
    /// @return The deserialized entry, or an error if the buffer is too small.
    /// @note Does NOT verify entry_hash -- call verify() separately.
    [[nodiscard]] static inline expected<HashChainEntry> deserialize(
            const uint8_t* data, size_t size) {

        if (size < HASH_CHAIN_ENTRY_SIZE) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "HashChainEntry::deserialize: need "
                         + std::to_string(HASH_CHAIN_ENTRY_SIZE)
                         + " bytes, got " + std::to_string(size)};
        }

        HashChainEntry entry;
        entry.sequence_number = detail::audit::read_le64(data);
        entry.timestamp_ns    = detail::audit::read_le64(data + 8);
        std::memcpy(entry.prev_hash.data(),  data + 16, 32);
        std::memcpy(entry.data_hash.data(),  data + 48, 32);
        std::memcpy(entry.entry_hash.data(), data + 80, 32);

        return entry;
    }
};

/// Builds SHA-256 hash chains during Parquet writes.
///
/// @code
///   AuditChainWriter writer;
///   for (const auto& record : records) {
///       auto bytes = serialize_record(record);
///       auto entry = writer.append(bytes.data(), bytes.size());
///   }
///   auto chain_bytes = writer.serialize_chain();
///   // Store chain_bytes in Parquet metadata or a sidecar file
/// @endcode
///
/// To continue a chain across files, save last_hash() from the previous
/// file and pass it to the new writer's reset():
/// @code
///   AuditChainWriter writer2;
///   writer2.reset(previous_writer.last_hash());
/// @endcode
///
/// @note NOT thread-safe. Callers must synchronize externally if appending
///       from multiple threads.
class AuditChainWriter {
public:
    /// Construct a new writer with an empty chain.
    /// The first entry's prev_hash will be all zeros.
    /// @throws std::runtime_error if the commercial license is required but missing.
    inline AuditChainWriter()
        : last_hash_{}
        , next_seq_(0) {
        auto gate = commercial::require_feature("AuditChainWriter");
        (void)gate;
    }

    /// Append a record to the chain with an explicit timestamp.
    ///
    /// Hashes @p record_data to produce data_hash, sets prev_hash from the
    /// last entry (or zeros for the first), computes entry_hash, and
    /// appends the new entry to the internal list.
    ///
    /// @param record_data  Raw bytes of the record being audited.
    /// @param record_size  Size of @p record_data in bytes.
    /// @param timestamp_ns Nanosecond timestamp for this entry.
    /// @return The newly created HashChainEntry (by value).
    inline HashChainEntry append(const uint8_t* record_data, size_t record_size,
                                 int64_t timestamp_ns) {
        HashChainEntry entry;
        entry.sequence_number = next_seq_;
        entry.timestamp_ns    = timestamp_ns;
        entry.prev_hash       = last_hash_;
        entry.data_hash       = crypto::detail::sha256::sha256(record_data, record_size);

        entry.compute_entry_hash();

        last_hash_ = entry.entry_hash;
        ++next_seq_;
        entries_.push_back(entry);

        return entry;
    }

    /// Append a record with auto-generated timestamp from the system clock.
    ///
    /// @param record_data  Raw bytes of the record being audited.
    /// @param record_size  Size of @p record_data in bytes.
    /// @return The newly created HashChainEntry (by value).
    /// @see append(const uint8_t*, size_t, int64_t) for explicit timestamps.
    inline HashChainEntry append(const uint8_t* record_data, size_t record_size) {
        return append(record_data, record_size, now_ns());
    }

    /// Return the number of entries in the chain.
    [[nodiscard]] inline int64_t length() const {
        return static_cast<int64_t>(entries_.size());
    }

    /// Return the entry_hash of the last entry in the chain.
    /// If the chain is empty, returns all zeros (or the initial prev_hash
    /// if set via reset()).
    [[nodiscard]] inline std::array<uint8_t, 32> last_hash() const {
        return last_hash_;
    }

    /// Return a const reference to the internal entry list.
    [[nodiscard]] inline const std::vector<HashChainEntry>& entries() const {
        return entries_;
    }

    /// Serialize the entire chain to bytes.
    ///
    /// Format: 4-byte entry count (uint32_t LE) followed by N * 112-byte entries.
    /// Total size = 4 + N * 112 bytes.
    ///
    /// @return The serialized chain as a byte vector.
    [[nodiscard]] inline std::vector<uint8_t> serialize_chain() const {
        auto count = static_cast<uint32_t>(entries_.size());
        std::vector<uint8_t> buf;
        buf.reserve(4 + static_cast<size_t>(count) * HASH_CHAIN_ENTRY_SIZE);

        // Write entry count
        uint8_t count_buf[4];
        detail::audit::write_le32(count_buf, count);
        buf.insert(buf.end(), count_buf, count_buf + 4);

        // Write each entry
        for (const auto& entry : entries_) {
            auto entry_bytes = entry.serialize();
            buf.insert(buf.end(), entry_bytes.begin(), entry_bytes.end());
        }

        return buf;
    }

    /// Clear the chain and optionally set an initial prev_hash.
    ///
    /// Call this to start a new chain. If @p initial_prev_hash is provided,
    /// the first entry's prev_hash will be set to that value instead of
    /// all zeros. This is used to continue a chain from a previous file.
    ///
    /// @param initial_prev_hash  The prev_hash for the next appended entry (default: all zeros).
    inline void reset(std::array<uint8_t, 32> initial_prev_hash = {}) {
        entries_.clear();
        last_hash_ = initial_prev_hash;
        next_seq_  = 0;
    }

private:
    std::vector<HashChainEntry> entries_;
    std::array<uint8_t, 32>     last_hash_;
    int64_t                     next_seq_;
};

/// Verifies hash chain integrity.
///
/// Provides static methods for verifying:
///   - An entire chain (from serialized bytes or a vector of entries)
///   - Continuity between two files (cross-file chain linking)
///   - A single entry against its expected previous hash
///
/// All methods are pure functions with no side effects.
class AuditChainVerifier {
public:
    /// Result of a full chain verification.
    struct VerificationResult {
        /// True if the entire chain passed all integrity checks.
        bool valid = false;

        /// Number of entries that were successfully verified before
        /// a failure was detected (or the total count if all valid).
        int64_t entries_checked = 0;

        /// Index of the first entry that failed verification, or -1
        /// if all entries are valid.
        int64_t first_bad_index = -1;

        /// Human-readable description of the verification outcome.
        /// Empty on success; describes the failure mode otherwise.
        std::string error_message;
    };

    /// Verify a chain from serialized bytes.
    ///
    /// Deserializes the chain and then performs full verification.
    /// The input format must match AuditChainWriter::serialize_chain().
    ///
    /// @param chain_data  Pointer to the serialized chain bytes.
    /// @param chain_size  Size of the serialized chain in bytes.
    /// @return A VerificationResult describing the outcome.
    [[nodiscard]] static inline VerificationResult verify(
            const uint8_t* chain_data, size_t chain_size) {

        VerificationResult result;

        // Need at least the 4-byte count header
        if (chain_size < 4) {
            result.error_message = "chain data too small for header (need >= 4 bytes)";
            return result;
        }

        uint32_t count = detail::audit::read_le32(chain_data);
        size_t expected_size = 4 + static_cast<size_t>(count) * HASH_CHAIN_ENTRY_SIZE;

        if (chain_size < expected_size) {
            result.error_message = "chain data truncated: expected "
                                 + std::to_string(expected_size)
                                 + " bytes for " + std::to_string(count)
                                 + " entries, got " + std::to_string(chain_size);
            return result;
        }

        // Deserialize all entries
        std::vector<HashChainEntry> entries;
        entries.reserve(count);

        const uint8_t* ptr = chain_data + 4;
        for (uint32_t i = 0; i < count; ++i) {
            auto entry_result = HashChainEntry::deserialize(
                ptr, HASH_CHAIN_ENTRY_SIZE);

            if (!entry_result) {
                result.error_message = "failed to deserialize entry "
                                     + std::to_string(i) + ": "
                                     + entry_result.error().message;
                result.first_bad_index = static_cast<int64_t>(i);
                return result;
            }

            entries.push_back(std::move(*entry_result));
            ptr += HASH_CHAIN_ENTRY_SIZE;
        }

        return verify(entries);
    }

    /// Verify a vector of HashChainEntry objects.
    ///
    /// Checks performed for each entry:
    ///   1. sequence_number matches its position (0-indexed)
    ///   2. entry_hash is self-consistent (recomputed and compared)
    ///   3. prev_hash matches the entry_hash of the preceding entry
    ///      (or is all zeros for the first entry)
    ///   4. timestamp_ns is non-decreasing
    ///
    /// @param entries  The chain entries to verify, in order.
    /// @return A VerificationResult with details on the first failure (if any).
    [[nodiscard]] static inline VerificationResult verify(
            const std::vector<HashChainEntry>& entries) {

        VerificationResult result;

        if (entries.empty()) {
            result.valid           = true;
            result.entries_checked = 0;
            result.first_bad_index = -1;
            return result;
        }

        std::array<uint8_t, 32> expected_prev_hash{};  // zeros for first entry

        for (size_t i = 0; i < entries.size(); ++i) {
            const auto& entry = entries[i];

            // Check 1: sequence number must match position
            if (entry.sequence_number != static_cast<int64_t>(i)) {
                result.entries_checked = static_cast<int64_t>(i);
                result.first_bad_index = static_cast<int64_t>(i);
                result.error_message   = "entry " + std::to_string(i)
                    + ": expected sequence_number " + std::to_string(i)
                    + ", got " + std::to_string(entry.sequence_number);
                return result;
            }

            // Check 2: prev_hash must match expected
            if (entry.prev_hash != expected_prev_hash) {
                result.entries_checked = static_cast<int64_t>(i);
                result.first_bad_index = static_cast<int64_t>(i);
                result.error_message   = "entry " + std::to_string(i)
                    + ": prev_hash mismatch (chain link broken)";
                return result;
            }

            // Check 3: entry_hash must be self-consistent
            if (!entry.verify()) {
                result.entries_checked = static_cast<int64_t>(i);
                result.first_bad_index = static_cast<int64_t>(i);
                result.error_message   = "entry " + std::to_string(i)
                    + ": entry_hash does not match recomputed hash (data tampered)";
                return result;
            }

            // Check 4: timestamp must be non-decreasing
            if (i > 0 && entry.timestamp_ns < entries[i - 1].timestamp_ns) {
                result.entries_checked = static_cast<int64_t>(i);
                result.first_bad_index = static_cast<int64_t>(i);
                result.error_message   = "entry " + std::to_string(i)
                    + ": timestamp_ns (" + std::to_string(entry.timestamp_ns)
                    + ") < previous (" + std::to_string(entries[i - 1].timestamp_ns)
                    + ") — ordering violation";
                return result;
            }

            // Advance the expected prev_hash for the next entry
            expected_prev_hash = entry.entry_hash;
        }

        // All entries passed
        result.valid           = true;
        result.entries_checked = static_cast<int64_t>(entries.size());
        result.first_bad_index = -1;
        return result;
    }

    /// Check that two chain segments link together across files.
    ///
    /// Verifies that the last entry_hash of one file matches the
    /// first prev_hash of the next file. This enables multi-file audit
    /// trails where each Parquet file contains a segment of the chain.
    ///
    /// @param file1_last_hash  entry_hash of the last entry in the prior file.
    /// @param file2_entries    Entries from the subsequent file.
    /// @return True if the chain links correctly across files.
    [[nodiscard]] static inline bool verify_continuity(
            const std::array<uint8_t, 32>& file1_last_hash,
            const std::vector<HashChainEntry>& file2_entries) {

        if (file2_entries.empty()) {
            return true;  // empty continuation is vacuously valid
        }

        return file2_entries[0].prev_hash == file1_last_hash;
    }

    /// Verify a single entry against an expected prev_hash.
    ///
    /// Checks that:
    ///   1. entry.prev_hash matches @p expected_prev_hash
    ///   2. entry.entry_hash is self-consistent
    ///
    /// Use this for streaming verification where you process entries
    /// one at a time without buffering the entire chain.
    ///
    /// @param entry               The entry to verify.
    /// @param expected_prev_hash  The entry_hash of the preceding entry.
    /// @return True if the entry's prev_hash and entry_hash are both valid.
    [[nodiscard]] static inline bool verify_entry(
            const HashChainEntry& entry,
            const std::array<uint8_t, 32>& expected_prev_hash) {

        if (entry.prev_hash != expected_prev_hash) {
            return false;
        }

        return entry.verify();
    }
};

/// Chain summary stored in Parquet key-value metadata.
///
/// Each Parquet file that contains a hash chain segment stores an
/// AuditMetadata record in its key-value metadata. This allows fast
/// cross-file chain verification: you only need to compare the
/// last_entry_hash of file N with the first_prev_hash of file N+1,
/// without reading every entry.
///
/// Serialization format (semicolon-delimited key=value pairs):
/// @code
///   chain_id=<id>;start_seq=<n>;end_seq=<n>;
///   first_prev=<64 hex>;last_hash=<64 hex>;created_by=<string>
/// @endcode
///
/// @see QuantizationParams for the same serialization pattern.
struct AuditMetadata {
    /// Unique identifier for this chain (generated by generate_chain_id()).
    /// All files in the same logical audit trail share the same chain_id.
    std::string chain_id;

    /// Sequence number of the first entry in this file's chain segment.
    int64_t start_sequence = 0;

    /// Sequence number of the last entry in this file's chain segment.
    int64_t end_sequence = 0;

    /// prev_hash of the first entry in this segment (links to the prior file).
    /// All zeros if this is the first file in the chain.
    std::array<uint8_t, 32> first_prev_hash{};

    /// entry_hash of the last entry in this segment.
    /// The next file's first entry must have this as its prev_hash.
    std::array<uint8_t, 32> last_entry_hash{};

    /// Creator string (e.g. "SignetStack signet-forge version 0.1.0").
    std::string created_by = SIGNET_CREATED_BY;

    /// @name Extended metadata fields
    /// Used by decision_log / inference_log writers to embed chain
    /// information in Parquet file key-value metadata.
    /// @{

    /// Hex string of the first entry's entry_hash in this segment.
    std::string first_hash;

    /// Hex string of the last entry's entry_hash in this segment.
    std::string last_hash;

    /// Hex string of the first entry's prev_hash (links to the prior file).
    std::string prev_file_hash;

    /// Number of audit records in this segment.
    int64_t record_count = 0;

    /// Record type: "decision", "inference", etc.
    std::string record_type;
    /// @}

    /// Export as Parquet file key-value metadata pairs.
    ///
    /// @return A vector of (key, value) pairs suitable for embedding in
    ///         the Parquet file footer metadata (WriterOptions::file_metadata).
    [[nodiscard]] inline std::vector<std::pair<std::string, std::string>>
    to_key_values() const {
        std::vector<std::pair<std::string, std::string>> kvs;
        kvs.emplace_back("signetstack.audit.chain_id", chain_id);
        kvs.emplace_back("signetstack.audit.first_seq", std::to_string(start_sequence));
        kvs.emplace_back("signetstack.audit.last_seq",  std::to_string(end_sequence));
        kvs.emplace_back("signetstack.audit.first_hash", first_hash);
        kvs.emplace_back("signetstack.audit.last_hash",  last_hash);
        kvs.emplace_back("signetstack.audit.prev_file_hash", prev_file_hash);
        kvs.emplace_back("signetstack.audit.record_count", std::to_string(record_count));
        if (!record_type.empty()) {
            kvs.emplace_back("signetstack.audit.record_type", record_type);
        }
        return kvs;
    }

    /// Convert to a semicolon-delimited metadata string.
    ///
    /// Format:
    /// @code
    ///   chain_id=<id>;start_seq=<n>;end_seq=<n>;
    ///   first_prev=<64 hex>;last_hash=<64 hex>;created_by=<string>
    /// @endcode
    ///
    /// All values are plain ASCII. Hash values are lowercase hex.
    ///
    /// @return The serialized metadata string.
    [[nodiscard]] inline std::string serialize() const {
        std::ostringstream oss;
        oss << "chain_id=" << chain_id
            << ";start_seq=" << start_sequence
            << ";end_seq=" << end_sequence
            << ";first_prev=" << hash_to_hex(first_prev_hash)
            << ";last_hash=" << hash_to_hex(last_entry_hash)
            << ";created_by=" << created_by;
        return oss.str();
    }

    /// Parse a metadata string back into AuditMetadata.
    ///
    /// Expects the format produced by serialize(). Returns an error if
    /// required fields are missing or malformed.
    ///
    /// @param s  The semicolon-delimited metadata string.
    /// @return The deserialized AuditMetadata, or an error on parse failure.
    [[nodiscard]] static inline expected<AuditMetadata> deserialize(
            const std::string& s) {

        AuditMetadata meta;

        // Parse semicolon-delimited key=value pairs into a map
        auto parse_pairs = [](const std::string& input)
            -> std::vector<std::pair<std::string, std::string>> {

            std::vector<std::pair<std::string, std::string>> pairs;
            size_t pos = 0;

            while (pos < input.size()) {
                // Find the next semicolon (or end of string)
                size_t semi = input.find(';', pos);
                if (semi == std::string::npos) {
                    semi = input.size();
                }

                // Extract the key=value substring
                std::string token = input.substr(pos, semi - pos);

                // Split on first '='
                size_t eq = token.find('=');
                if (eq != std::string::npos) {
                    std::string key   = token.substr(0, eq);
                    std::string value = token.substr(eq + 1);
                    pairs.emplace_back(std::move(key), std::move(value));
                }

                pos = semi + 1;
            }

            return pairs;
        };

        auto pairs = parse_pairs(s);

        // Helper to find a key
        auto find_key = [&pairs](const std::string& key) -> const std::string* {
            for (const auto& [k, v] : pairs) {
                if (k == key) return &v;
            }
            return nullptr;
        };

        // chain_id (required)
        const auto* chain_id_val = find_key("chain_id");
        if (!chain_id_val) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: missing required field 'chain_id'"};
        }
        meta.chain_id = *chain_id_val;

        // start_seq (required)
        const auto* start_val = find_key("start_seq");
        if (!start_val) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: missing required field 'start_seq'"};
        }
        try {
            meta.start_sequence = std::stoll(*start_val);
        } catch (const std::exception&) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: invalid start_seq value: " + *start_val};
        }

        // end_seq (required)
        const auto* end_val = find_key("end_seq");
        if (!end_val) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: missing required field 'end_seq'"};
        }
        try {
            meta.end_sequence = std::stoll(*end_val);
        } catch (const std::exception&) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: invalid end_seq value: " + *end_val};
        }

        // first_prev (required, 64 hex chars)
        const auto* first_prev_val = find_key("first_prev");
        if (!first_prev_val) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: missing required field 'first_prev'"};
        }
        auto first_prev_result = hex_to_hash(*first_prev_val);
        if (!first_prev_result) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: invalid first_prev hash: "
                         + first_prev_result.error().message};
        }
        meta.first_prev_hash = *first_prev_result;

        // last_hash (required, 64 hex chars)
        const auto* last_hash_val = find_key("last_hash");
        if (!last_hash_val) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: missing required field 'last_hash'"};
        }
        auto last_hash_result = hex_to_hash(*last_hash_val);
        if (!last_hash_result) {
            return Error{ErrorCode::INVALID_FILE,
                         "AuditMetadata: invalid last_hash: "
                         + last_hash_result.error().message};
        }
        meta.last_entry_hash = *last_hash_result;

        // created_by (optional, defaults to SIGNET_CREATED_BY)
        const auto* created_val = find_key("created_by");
        if (created_val) {
            meta.created_by = *created_val;
        }

        return meta;
    }
};

/// Build an AuditMetadata from a populated AuditChainWriter.
///
/// Extracts the chain summary from a writer that has accumulated entries.
/// The chain_id must be provided by the caller (use generate_chain_id()
/// to create one, or reuse an existing ID for chain continuation).
///
/// @param writer    The chain writer to extract metadata from.
/// @param chain_id  Unique chain identifier for this audit trail.
/// @return The populated AuditMetadata, or an error if the writer has no entries.
inline expected<AuditMetadata> build_audit_metadata(
        const AuditChainWriter& writer,
        const std::string& chain_id) {

    if (writer.length() == 0) {
        return Error{ErrorCode::INTERNAL_ERROR,
                     "build_audit_metadata: writer has no entries"};
    }

    const auto& entries = writer.entries();

    AuditMetadata meta;
    meta.chain_id        = chain_id;
    meta.start_sequence  = entries.front().sequence_number;
    meta.end_sequence    = entries.back().sequence_number;
    meta.first_prev_hash = entries.front().prev_hash;
    meta.last_entry_hash = entries.back().entry_hash;
    meta.created_by      = SIGNET_CREATED_BY;

    return meta;
}

/// Deserialize and verify a chain from serialized bytes in one call.
///
/// Combines deserialization with AuditChainVerifier::verify(), returning
/// HASH_CHAIN_BROKEN when verification fails.
///
/// @param chain_data  Pointer to the serialized chain bytes.
/// @param chain_size  Size of the serialized chain in bytes.
/// @return The verified entries, or an error if deserialization or verification fails.
/// @see AuditChainVerifier::verify
inline expected<std::vector<HashChainEntry>> deserialize_and_verify_chain(
        const uint8_t* chain_data, size_t chain_size) {

    // Deserialize
    if (chain_size < 4) {
        return Error{ErrorCode::HASH_CHAIN_BROKEN,
                     "chain data too small for header"};
    }

    uint32_t count = detail::audit::read_le32(chain_data);
    size_t expected_size = 4 + static_cast<size_t>(count) * HASH_CHAIN_ENTRY_SIZE;

    if (chain_size < expected_size) {
        return Error{ErrorCode::HASH_CHAIN_BROKEN,
                     "chain data truncated: expected "
                     + std::to_string(expected_size) + " bytes, got "
                     + std::to_string(chain_size)};
    }

    std::vector<HashChainEntry> entries;
    entries.reserve(count);

    const uint8_t* ptr = chain_data + 4;
    for (uint32_t i = 0; i < count; ++i) {
        auto entry_result = HashChainEntry::deserialize(ptr, HASH_CHAIN_ENTRY_SIZE);
        if (!entry_result) {
            return Error{ErrorCode::HASH_CHAIN_BROKEN,
                         "entry " + std::to_string(i) + ": "
                         + entry_result.error().message};
        }
        entries.push_back(std::move(*entry_result));
        ptr += HASH_CHAIN_ENTRY_SIZE;
    }

    // Verify
    auto result = AuditChainVerifier::verify(entries);
    if (!result.valid) {
        return Error{ErrorCode::HASH_CHAIN_BROKEN, result.error_message};
    }

    return entries;
}

} // namespace signet::forge
