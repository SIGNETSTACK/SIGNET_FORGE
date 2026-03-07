// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
// compliance_types.hpp — Shared types for MiFID II and EU AI Act compliance reports
// Phase 10d: Compliance Report Generators
//
// Provides common enumerations, options structs, and the ComplianceReport output
// type shared by MiFID2Reporter and EUAIActReporter.

#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/compliance/compliance_types.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

#include <cstdint>
#include <limits>
#include <string>

namespace signet::forge {

/// Output serialization format for compliance reports.
enum class ReportFormat {
    JSON,    ///< Pretty-printed JSON object (default)
    NDJSON,  ///< Newline-delimited JSON — one record per line (streaming-friendly)
    CSV,     ///< Comma-separated values with header row
};

/// Which regulatory standard a compliance report satisfies.
enum class ComplianceStandard {
    MIFID2_RTS24,       ///< MiFID II RTS 24 — algorithmic trading records
    EU_AI_ACT_ART12,    ///< EU AI Act Article 12 — operational logging
    EU_AI_ACT_ART13,    ///< EU AI Act Article 13 — transparency disclosure
    EU_AI_ACT_ART19,    ///< EU AI Act Article 19 — conformity assessment summary
};

/// Query and formatting parameters for compliance report generation.
///
/// Shared by MiFID2Reporter and EUAIActReporter. Controls the time window,
/// output format, chain verification, and report identification fields.
///
/// @note Defined at namespace scope (not nested) for Apple Clang compatibility.
struct ReportOptions {
    /// Inclusive start of the reporting period (nanoseconds since Unix epoch).
    /// Default: beginning of time.
    int64_t start_ns = 0;

    /// Inclusive end of the reporting period (nanoseconds since Unix epoch).
    /// Default: no upper bound.
    int64_t end_ns = std::numeric_limits<int64_t>::max();

    /// Output serialization format.
    ReportFormat format = ReportFormat::JSON;

    /// If true, verify the hash chain of each log file before generating the
    /// report and record the result in ComplianceReport::chain_verified.
    bool verify_chain = true;

    /// If true, include raw input feature vectors in the report output.
    /// Default false: omit raw features (typically proprietary alpha signals).
    /// Regulators generally require only input hashes, not raw values.
    bool include_features = false;

    /// If true, emit human-readable indented JSON (2-space indent).
    bool pretty_print = true;

    /// Logical identifier for the AI system being reported on.
    /// Appears in EU AI Act reports as the "system_id" field.
    std::string system_id;

    /// Optional: unique identifier for this report filing.
    /// If empty, auto-generated from timestamp.
    std::string report_id;

    /// Organisation / firm identifier for MiFID II field 1.
    /// Example: LEI code (20-char alphanumeric) or internal firm ID.
    std::string firm_id;

    /// Low-confidence threshold for EU AI Act anomaly counting.
    /// Inferences with confidence below this value are flagged.
    float low_confidence_threshold = 0.5f;
};

/// The generated compliance report returned to the caller.
///
/// Contains the serialized report body (JSON/NDJSON/CSV), metadata about
/// when it was generated, and chain verification status.
struct ComplianceReport {
    /// Which regulation this report satisfies.
    ComplianceStandard standard = ComplianceStandard::MIFID2_RTS24;

    /// Serialization format of content.
    ReportFormat format = ReportFormat::JSON;

    /// The report body — JSON object, NDJSON lines, or CSV text.
    std::string content;

    /// Unique identifier for this report (auto-generated if not supplied).
    std::string report_id;

    /// UTC ISO 8601 timestamp at which the report was generated.
    std::string generated_at_iso;

    /// Nanosecond timestamp at which the report was generated.
    int64_t generated_at_ns = 0;

    /// Number of records included in this report.
    int64_t total_records = 0;

    /// True if all log files' hash chains verified successfully.
    /// False if any file failed verification or verification was skipped
    /// (opts.verify_chain == false).
    bool chain_verified = false;

    /// Chain ID from the first log file processed.
    std::string chain_id;

    /// ISO 8601 representation of opts.start_ns.
    std::string period_start_iso;

    /// ISO 8601 representation of opts.end_ns (or "open" if unbounded).
    std::string period_end_iso;
};

} // namespace signet::forge
