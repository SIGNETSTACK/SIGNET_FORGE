// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// See LICENSE_COMMERCIAL for full terms.

/// @file row_lineage.hpp
/// @brief Per-row lineage tracking (Iceberg V3-style) with monotonic row IDs,
///        mutation versioning, and SHA-256 per-row hash chain for tamper evidence.
/// @note This is an AGPL-3.0 commercial module. See LICENSE_COMMERCIAL for proprietary use.

#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/row_lineage.hpp requires SIGNET_ENABLE_COMMERCIAL=ON (AGPL-3.0 commercial tier). See LICENSE_COMMERCIAL."
#endif

// ---------------------------------------------------------------------------
// row_lineage.hpp -- Per-row lineage tracking (Iceberg V3-style)
//
// Generates monotonic row_id, tracks mutation version, origin file/batch,
// and a per-row SHA-256 prev_hash chain for tamper-evident per-row ordering.
//
// This extends the batch-level audit chain (audit_chain.hpp) to provide
// row-granularity lineage suitable for Iceberg V3 style data governance.
// ---------------------------------------------------------------------------

#include "signet/crypto/sha256.hpp"  // crypto::detail::sha256::sha256
#include "signet/error.hpp"                // commercial::require_feature

#include <array>
#include <cstdint>
#include <string>

namespace signet::forge {

/// Per-row lineage tracking inspired by Iceberg V3-style data governance.
///
/// Generates monotonic row IDs, tracks mutation versions, records origin
/// file/batch identifiers, and maintains a SHA-256 per-row hash chain for
/// tamper-evident ordering. This extends the batch-level audit chain
/// (audit_chain.hpp) to row granularity.
///
/// Row IDs are monotonically increasing and never reset, even across
/// batches. The hash chain is also continuous across reset() calls.
///
/// @see audit_chain.hpp
class RowLineageTracker {
public:
    /// Lineage metadata for a single row.
    struct RowLineage {
        int64_t     row_id;           ///< Monotonic row identifier (0-based, never resets)
        int32_t     row_version;      ///< Mutation version counter
        std::string row_origin_file;  ///< Source file/batch identifier
        std::string row_prev_hash;    ///< SHA-256 hex of the previous row's serialized data
    };

    /// Construct a tracker for the given origin file and initial version.
    ///
    /// @param origin_file  Source file/batch identifier (stored in each RowLineage).
    /// @param initial_version  Starting mutation version counter.
    /// @throws std::runtime_error if the commercial license is required but missing.
    explicit RowLineageTracker(const std::string& origin_file = "",
                                int32_t initial_version = 1)
        : origin_file_(origin_file)
        , version_(initial_version) {
        // AGPL-3.0 commercial tier gating: the license check result is intentionally
        // discarded here. In demo/dev mode (SIGNET_REQUIRE_COMMERCIAL_LICENSE=OFF),
        // require_feature() always succeeds. In production mode, a missing license
        // causes a hard error at a higher level (InferenceLogWriter /
        // DecisionLogWriter constructor). Discarding here allows RowLineageTracker
        // to be used standalone in tests without the full license validation pipeline.
        auto gate = commercial::require_feature("RowLineageTracker");
        (void)gate;
    }

    /// Generate lineage for the next row. `row_data` is the serialized row bytes
    /// used to compute the per-row hash chain.
    [[nodiscard]] RowLineage next(const uint8_t* row_data, size_t row_size) {
        RowLineage lineage;
        lineage.row_id = next_row_id_++;
        lineage.row_version = version_;
        lineage.row_origin_file = origin_file_;
        lineage.row_prev_hash = prev_hash_hex_;

        // Compute cumulative hash: SHA-256(prev_hash_hex + row_data)
        // This creates a true chain where each row depends on all prior rows.
        std::vector<uint8_t> chain_input;
        chain_input.reserve(prev_hash_hex_.size() + row_size);
        chain_input.insert(chain_input.end(), prev_hash_hex_.begin(), prev_hash_hex_.end());
        chain_input.insert(chain_input.end(), row_data, row_data + row_size);
        auto hash = crypto::detail::sha256::sha256(chain_input.data(), chain_input.size());
        prev_hash_hex_ = hash_to_hex_impl(hash);

        return lineage;
    }

    /// Get current row counter (next row_id to be assigned).
    [[nodiscard]] int64_t current_row_id() const noexcept { return next_row_id_; }

    /// Reset the origin file and version for a new batch.
    ///
    /// Row ID is NOT reset (monotonic across batches).
    /// Hash chain is NOT reset (chain continuity is preserved).
    ///
    /// @param origin_file New source file/batch identifier.
    /// @param version     New mutation version counter.
    void reset(const std::string& origin_file, int32_t version = 1) {
        origin_file_ = origin_file;
        version_ = version;
    }

private:
    std::string origin_file_;
    int32_t version_ = 1;
    int64_t next_row_id_ = 0;
    std::string prev_hash_hex_ = std::string(64, '0');  // Genesis: all zeros

    /// Convert a 32-byte SHA-256 hash to a 64-character lowercase hex string.
    static std::string hash_to_hex_impl(const std::array<uint8_t, 32>& hash) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string result(64, '\0');
        for (size_t i = 0; i < 32; ++i) {
            result[2*i]     = hex[hash[i] >> 4];
            result[2*i + 1] = hex[hash[i] & 0x0F];
        }
        return result;
    }
};

} // namespace signet::forge
