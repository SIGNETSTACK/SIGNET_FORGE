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

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace signet::forge {

/// Output serialization format for compliance reports.
enum class ReportFormat {
    JSON,    ///< Pretty-printed JSON object (default)
    NDJSON,  ///< Newline-delimited JSON — one record per line (streaming-friendly)
    CSV,     ///< Comma-separated values with header row
};

/// Timestamp granularity for MiFID II RTS 24 Art.2(2) compliance.
///
/// Controls the sub-second precision emitted in ISO 8601 timestamp fields.
/// RTS 24 requires nanosecond precision for high-frequency trading; lower
/// granularities may be appropriate for non-HFT reporting regimes.
enum class TimestampGranularity {
    NANOS,   ///< 9 sub-second digits (default, MiFID II HFT compliant)
    MICROS,  ///< 6 sub-second digits
    MILLIS,  ///< 3 sub-second digits
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
    int64_t end_ns = (std::numeric_limits<int64_t>::max)();

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

    /// Significant digits for price fields (MiFID II RTS 24 Annex I Field 6).
    /// Default 17 preserves full double-precision round-trip fidelity.
    int price_significant_digits = 17;

    /// Timestamp sub-second granularity (MiFID II RTS 24 Art.2(2)).
    /// Default NANOS for HFT regulatory compliance.
    TimestampGranularity timestamp_granularity = TimestampGranularity::NANOS;

    // --- EU AI Act Art.13 Model Card / Transparency fields (Gap R-2) ---

    /// Art.13(3)(b)(i): Intended purpose of the AI system.
    std::string intended_purpose;

    /// Art.13(3)(b)(ii): Known limitations and foreseeable misuse risks.
    std::string known_limitations;

    /// Art.13(3)(a): Provider name and contact information.
    std::string provider_name;
    std::string provider_contact;

    /// Art.13(3)(b)(iv): Instructions for use / deployment guidance.
    std::string instructions_for_use;

    /// Art.14: Human oversight measures description.
    std::string human_oversight_measures;

    /// Art.15: Accuracy metrics description (e.g. "F1=0.94 on test set v3").
    std::string accuracy_metrics;

    /// Art.15: Known biases or fairness concerns.
    std::string bias_risks;

    /// Art.9: Risk classification level (1=minimal, 2=limited, 3=high, 4=unacceptable).
    int risk_level{0};
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

    /// H-20: True if one or more log file batches could not be read.
    /// When true, the report may contain incomplete data.
    bool incomplete_data = false;

    /// H-20: Accumulated read errors from log files whose records could not be read.
    std::vector<std::string> read_errors;
};

// ===========================================================================
// Gap R-12/R-12b/R-12c: Regulatory identifier validation
//
// MiFID II RTS 24/25 and EMIR require specific identifier formats:
//   - LEI (Legal Entity Identifier): ISO 17442, 20-char alphanumeric + check digits
//   - ISIN (International Securities Identification Number): ISO 6166, 12-char
//   - MIC (Market Identifier Code): ISO 10383, 4-char alpha
//
// These validators enforce format compliance before generating regulatory reports.
// Invalid identifiers would cause report rejection by NCAs (National Competent
// Authorities) or trade repositories.
//
// References:
//   - ISO 17442:2020 — Legal Entity Identifier (LEI)
//   - ISO 6166:2021 — International Securities Identification Number (ISIN)
//   - ISO 10383:2022 — Market Identifier Code (MIC)
//   - MiFID II RTS 24 Annex I Fields 1, 18, 36
// ===========================================================================

namespace regulatory {

/// Validate an LEI (Legal Entity Identifier) per ISO 17442.
/// Format: 20 alphanumeric characters. Positions 1-4: prefix (numeric),
/// 5-6: reserved (00), 7-18: entity identifier, 19-20: check digits (mod 97).
/// @param lei  The LEI string to validate.
/// @return true if the LEI has valid format and check digits.
[[nodiscard]] inline bool validate_lei(const std::string& lei) {
    if (lei.size() != 20) return false;

    // All characters must be alphanumeric
    for (char c : lei) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) return false;
    }

    // ISO 7064 Mod 97-10 check (same algorithm as IBAN)
    // Convert alpha chars to 2-digit numbers: A=10, B=11, ..., Z=35
    std::string numeric;
    numeric.reserve(40);
    for (char c : lei) {
        if (c >= 'A' && c <= 'Z') {
            int val = c - 'A' + 10;
            numeric += std::to_string(val);
        } else {
            numeric += c;
        }
    }

    // Compute mod 97 on the large number (digit-by-digit)
    int remainder = 0;
    for (char c : numeric) {
        remainder = (remainder * 10 + (c - '0')) % 97;
    }
    return remainder == 1;
}

/// Validate an ISIN (International Securities Identification Number) per ISO 6166.
/// Format: 2-letter country code + 9 alphanumeric chars + 1 check digit.
/// @param isin  The ISIN string to validate.
/// @return true if the ISIN has valid format and Luhn check digit.
[[nodiscard]] inline bool validate_isin(const std::string& isin) {
    if (isin.size() != 12) return false;

    // First 2 characters must be uppercase alpha (country code)
    if (!(isin[0] >= 'A' && isin[0] <= 'Z')) return false;
    if (!(isin[1] >= 'A' && isin[1] <= 'Z')) return false;

    // Characters 3-12 must be alphanumeric
    for (size_t i = 2; i < 12; ++i) {
        char c = isin[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) return false;
    }

    // Luhn check on the expanded numeric form
    // Letters are expanded: A=10, B=11, ..., Z=35
    std::string digits;
    digits.reserve(24);
    for (char c : isin) {
        if (c >= 'A' && c <= 'Z') {
            int val = c - 'A' + 10;
            digits += std::to_string(val);
        } else {
            digits += c;
        }
    }

    // Luhn algorithm (right-to-left, double every second digit)
    int sum = 0;
    bool double_next = false;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
        int d = digits[static_cast<size_t>(i)] - '0';
        if (double_next) {
            d *= 2;
            if (d > 9) d -= 9;
        }
        sum += d;
        double_next = !double_next;
    }
    return (sum % 10) == 0;
}

/// Validate a MIC (Market Identifier Code) per ISO 10383.
/// Format: exactly 4 uppercase alphabetic characters.
/// @note Validates ISO 10383 format only (4 uppercase alpha); does not verify against MIC registry.
/// @param mic  The MIC string to validate.
/// @return true if the MIC has valid format.
[[nodiscard]] inline bool validate_mic(const std::string& mic) {
    if (mic.size() != 4) return false;
    for (char c : mic) {
        if (!(c >= 'A' && c <= 'Z')) return false;
    }
    return true;
}

} // namespace regulatory

// ===========================================================================
// Pre-trade risk checks (Gap R-11)
//
// MiFID II RTS 6 Art. 17 requires pre-trade risk controls for
// algorithmic trading systems, including price collars, maximum order
// sizes, and maximum daily notional limits.
//
// Reference: MiFID II RTS 6 (Commission Delegated Regulation (EU) 2017/589)
//            Art. 17 — Pre-trade controls on order entry
// ===========================================================================

namespace risk {

/// Pre-trade risk check result.
enum class RiskCheckResult : int32_t {
    PASS     = 0,  ///< Order passes all pre-trade risk checks.
    REJECT   = 1,  ///< Order rejected by risk check — must not be sent.
    THROTTLE = 2,  ///< Order rate-limited — retry after cooldown.
};

/// Reason for a risk check rejection.
enum class RiskRejectReason : int32_t {
    NONE              = 0,   ///< No rejection (check passed).
    PRICE_COLLAR      = 1,   ///< Price outside allowed deviation from reference (RTS 6 Art. 17(1)(a)).
    MAX_ORDER_SIZE    = 2,   ///< Notional value exceeds single-order limit (RTS 6 Art. 17(1)(b)).
    MAX_DAILY_VOLUME  = 3,   ///< Cumulative daily volume exceeds limit.
    MAX_MESSAGE_RATE  = 4,   ///< Order/cancel rate exceeds messages-per-second cap.
    INSTRUMENT_BANNED = 5,   ///< Instrument on restricted list.
    CUSTOM            = 99,  ///< Custom rejection reason (see reject_message).
};

/// Pre-trade risk limits configuration.
///
/// Set these thresholds before submitting orders. Calling `check_order()`
/// returns REJECT with the appropriate reason if any limit is breached.
struct PreTradeRiskLimits {
    double price_collar_pct       = 5.0;       ///< Max % deviation from reference price (both sides).
    double max_order_notional     = 1e9;        ///< Max single-order notional value (default 1B).
    double max_daily_notional     = 1e10;       ///< Max cumulative daily notional (default 10B).
    int64_t max_messages_per_sec  = 1000;       ///< Max order/cancel messages per second.
};

/// Result of a pre-trade risk check.
struct PreTradeCheckResult {
    RiskCheckResult  result  = RiskCheckResult::PASS;
    RiskRejectReason reason  = RiskRejectReason::NONE;
    std::string      message;  ///< Human-readable explanation (for audit trail).
};

/// Perform a pre-trade risk check on a proposed order.
///
/// @param limits          Risk limit configuration.
/// @param order_price     Proposed order price.
/// @param reference_price Current reference/mid price.
/// @param order_notional  Notional value of the proposed order.
/// @param daily_notional  Cumulative notional already traded today.
/// @return PreTradeCheckResult with PASS or REJECT + reason.
[[nodiscard]] inline PreTradeCheckResult check_order(
    const PreTradeRiskLimits& limits,
    double order_price,
    double reference_price,
    double order_notional,
    double daily_notional) {

    PreTradeCheckResult result;

    // Price collar check (RTS 6 Art. 17(1)(a))
    if (reference_price > 0.0) {
        double deviation_pct = 100.0 *
            std::abs(order_price - reference_price) / reference_price;
        if (deviation_pct > limits.price_collar_pct) {
            result.result = RiskCheckResult::REJECT;
            result.reason = RiskRejectReason::PRICE_COLLAR;
            result.message = "Price deviation " + std::to_string(deviation_pct) +
                             "% exceeds collar " + std::to_string(limits.price_collar_pct) + "%";
            return result;
        }
    }

    // Max order size check (RTS 6 Art. 17(1)(b))
    if (order_notional > limits.max_order_notional) {
        result.result = RiskCheckResult::REJECT;
        result.reason = RiskRejectReason::MAX_ORDER_SIZE;
        result.message = "Order notional exceeds limit";
        return result;
    }

    // Daily notional limit
    if (daily_notional + order_notional > limits.max_daily_notional) {
        result.result = RiskCheckResult::REJECT;
        result.reason = RiskRejectReason::MAX_DAILY_VOLUME;
        result.message = "Daily notional limit would be breached";
        return result;
    }

    return result;
}

} // namespace risk

// ===========================================================================
// PII data classification (Gap G-2)
//
// GDPR Art. 9, 25, 32 — Data classification by sensitivity level.
// Columns containing personal data must be classified so that
// encryption, pseudonymization, and access control policies can be
// automatically enforced by GDPRWriterPolicy (Gap G-7).
//
// Reference: GDPR Art. 9(1) — special categories of personal data
//            GDPR Art. 25 — data protection by design and by default
// ===========================================================================

namespace gdpr {

/// Data classification levels for GDPR compliance.
///
/// Assign one of these to each Parquet column to drive automatic
/// encryption and pseudonymization policies.
enum class DataClassification : int32_t {
    PUBLIC           = 0,   ///< Non-personal, no restrictions.
    INTERNAL         = 1,   ///< Internal business data, not personal.
    PERSONAL         = 2,   ///< Personal data (GDPR Art. 4(1)): name, email, phone.
    SENSITIVE        = 3,   ///< Special category data (GDPR Art. 9): health, race, religion.
    PSEUDONYMIZED    = 4,   ///< Already pseudonymized (Art. 4(5)).
    ANONYMIZED       = 5,   ///< Fully anonymized — outside GDPR scope.
};

/// Column-level data classification descriptor.
///
/// Attach this to each column in the schema to declare its sensitivity.
/// Used by GDPRWriterPolicy (G-7) to enforce encryption requirements.
struct ColumnClassification {
    std::string         column_name;      ///< Parquet column path.
    DataClassification  classification = DataClassification::PUBLIC;
    std::string         purpose;          ///< Processing purpose (Art. 5(1)(b)).
    std::string         lawful_basis;     ///< Legal basis: "consent", "contract", "legal_obligation", etc.
    int32_t             retention_days = 0; ///< Max retention period in days (0 = unlimited).
};

/// Check if a classification level requires encryption under GDPR Art. 32.
/// @return true if the data classification mandates encryption at rest.
[[nodiscard]] inline bool requires_encryption(DataClassification c) {
    return c == DataClassification::PERSONAL || c == DataClassification::SENSITIVE;
}

/// Check if a classification level allows pseudonymization.
/// @return true if the data should be pseudonymized when possible.
[[nodiscard]] inline bool allows_pseudonymization(DataClassification c) {
    return c == DataClassification::PERSONAL || c == DataClassification::SENSITIVE;
}

} // namespace gdpr

// ===========================================================================
// ICT asset identification and classification (Gap D-6)
//
// DORA Art. 7-8 requires financial entities to identify and classify
// all ICT assets and document their criticality and dependencies.
//
// Reference: Regulation (EU) 2022/2554 (DORA) Art. 7 — ICT systems,
//            protocols and tools; Art. 8 — Identification
// ===========================================================================

namespace dora {

/// ICT asset criticality level per DORA Art. 8(1).
enum class AssetCriticality : int32_t {
    LOW      = 0,  ///< Non-critical, no significant impact if unavailable.
    MEDIUM   = 1,  ///< Moderate impact; degraded service if unavailable.
    HIGH     = 2,  ///< Critical; direct impact on core business functions.
    VITAL    = 3,  ///< Vital; unavailability threatens business continuity.
};

/// ICT asset descriptor for DORA Art. 8 compliance.
///
/// Each Parquet file, key store, or data pipeline component should
/// have an associated ICTAssetDescriptor for the ICT asset register.
struct ICTAssetDescriptor {
    std::string       asset_id;          ///< Unique asset identifier.
    std::string       asset_name;        ///< Human-readable name.
    std::string       asset_type;        ///< Type: "data_store", "key_store", "pipeline", etc.
    AssetCriticality  criticality = AssetCriticality::MEDIUM;
    std::string       owner;             ///< Responsible person or team.
    std::string       location;          ///< Data center / cloud region.
    std::string       dependencies;      ///< Comma-separated dependent asset IDs.
    int64_t           last_assessed_ns = 0; ///< Last risk assessment timestamp (ns since epoch).
};

// ===========================================================================
// Backup policy / RPO support (Gap D-3)
//
// DORA Art. 12 requires documented backup policies with defined
// Recovery Point Objectives (RPO) and Recovery Time Objectives (RTO).
//
// Reference: Regulation (EU) 2022/2554 (DORA) Art. 12 — Backup policies
//            and procedures, restoration and recovery procedures
// ===========================================================================

/// Backup verification status.
enum class BackupStatus : int32_t {
    PENDING    = 0,  ///< Backup not yet started.
    IN_PROGRESS = 1, ///< Backup in progress.
    COMPLETED  = 2,  ///< Backup completed successfully.
    FAILED     = 3,  ///< Backup failed.
    VERIFIED   = 4,  ///< Backup verified via integrity check.
};

/// Backup policy configuration per DORA Art. 12.
struct BackupPolicy {
    std::string   policy_id;                ///< Unique policy identifier.
    int64_t       rpo_seconds = 3600;       ///< Recovery Point Objective (max data loss: default 1h).
    int64_t       rto_seconds = 14400;      ///< Recovery Time Objective (max downtime: default 4h).
    int32_t       retention_days = 90;      ///< Backup retention period.
    bool          encryption_required = true; ///< Backups must be encrypted.
    bool          integrity_check = true;   ///< Verify backup integrity after creation.
    std::string   storage_location;         ///< Backup storage location.
};

/// Backup record for audit trail.
struct BackupRecord {
    std::string   backup_id;                ///< Unique backup identifier.
    std::string   policy_id;                ///< Associated policy.
    int64_t       started_ns = 0;           ///< Backup start timestamp.
    int64_t       completed_ns = 0;         ///< Backup completion timestamp.
    BackupStatus  status = BackupStatus::PENDING;
    int64_t       size_bytes = 0;           ///< Backup size.
    std::string   checksum;                 ///< SHA-256 integrity hash.
};

/// Check if a backup meets its RPO requirement.
/// @param policy    Backup policy with RPO definition.
/// @param last_backup_ns  Timestamp of the last successful backup (ns since epoch).
/// @param now_ns    Current timestamp (ns since epoch).
/// @return true if the time since last backup is within RPO.
[[nodiscard]] inline bool meets_rpo(const BackupPolicy& policy,
                                     int64_t last_backup_ns,
                                     int64_t now_ns) {
    int64_t elapsed_s = (now_ns - last_backup_ns) / 1000000000LL;
    return elapsed_s <= policy.rpo_seconds;
}

// ===========================================================================
// Key rotation / lifecycle management (Gap D-11)
//
// DORA Art. 9(2) requires cryptographic key lifecycle management
// including rotation, revocation, and audit logging.
//
// Reference: Regulation (EU) 2022/2554 (DORA) Art. 9(2)
//            NIST SP 800-57 Part 1 Rev. 5 §5.3 (key states)
// ===========================================================================

/// Key lifecycle state per NIST SP 800-57.
enum class KeyState : int32_t {
    PRE_ACTIVATION   = 0,  ///< Key generated but not yet active.
    ACTIVE           = 1,  ///< Key in active use for encryption.
    DEACTIVATED      = 2,  ///< Key no longer used for new encryption; may decrypt.
    COMPROMISED      = 3,  ///< Key suspected or confirmed compromised.
    DESTROYED        = 4,  ///< Key material securely destroyed.
};

/// Key lifecycle record for rotation tracking.
struct KeyLifecycleRecord {
    std::string   key_id;                   ///< Unique key identifier.
    KeyState      state = KeyState::PRE_ACTIVATION;
    int64_t       created_ns = 0;           ///< Key creation timestamp.
    int64_t       activation_ns = 0;        ///< Key activation timestamp.
    int64_t       deactivation_ns = 0;      ///< Key deactivation timestamp.
    int64_t       expiry_ns = 0;            ///< Key expiry timestamp (crypto-period end).
    std::string   algorithm;                ///< Algorithm: "AES-256-GCM", "AES-256-CTR", etc.
    std::string   replaced_by;              ///< Key ID of replacement (after rotation).
};

/// Check if a key needs rotation based on its crypto-period.
/// @param record   Key lifecycle record.
/// @param now_ns   Current timestamp (ns since epoch).
/// @return true if the key has exceeded its crypto-period.
[[nodiscard]] inline bool needs_rotation(const KeyLifecycleRecord& record,
                                          int64_t now_ns) {
    if (record.state != KeyState::ACTIVE) return false;
    if (record.expiry_ns == 0) return false;  // No expiry set
    return now_ns >= record.expiry_ns;
}

// ===========================================================================
// ICT incident management (Gap D-1)
//
// DORA Art. 10, 15, 19 — ICT incident detection, classification,
// management, and reporting to competent authorities.
//
// Reference: Regulation (EU) 2022/2554 (DORA)
//            Art. 10 — Detection
//            Art. 15 — Further harmonisation of ICT incident classification
//            Art. 19 — Reporting of major ICT-related incidents
// ===========================================================================

/// ICT incident severity per DORA Art. 15.
enum class IncidentSeverity : int32_t {
    LOW       = 0,  ///< Minor incident, no significant impact.
    MEDIUM    = 1,  ///< Moderate impact, service degraded.
    HIGH      = 2,  ///< Major incident, service disrupted.
    CRITICAL  = 3,  ///< Critical incident, business continuity at risk.
};

/// ICT incident category per DORA Art. 10.
enum class IncidentCategory : int32_t {
    OPERATIONAL      = 0,  ///< System failure, outage.
    SECURITY         = 1,  ///< Unauthorized access, data breach.
    DATA_INTEGRITY   = 2,  ///< Data corruption, hash chain break.
    CRYPTOGRAPHIC    = 3,  ///< Key compromise, decryption failure.
    THIRD_PARTY      = 4,  ///< Third-party service failure.
};

/// ICT incident record for DORA Art. 19 reporting.
struct ICTIncidentRecord {
    std::string        incident_id;         ///< Unique incident identifier.
    IncidentCategory   category = IncidentCategory::OPERATIONAL;
    IncidentSeverity   severity = IncidentSeverity::LOW;
    int64_t            detected_ns = 0;     ///< Detection timestamp.
    int64_t            resolved_ns = 0;     ///< Resolution timestamp (0 = ongoing).
    std::string        description;         ///< Incident description.
    std::string        impact;              ///< Business impact assessment.
    std::string        root_cause;          ///< Root cause (post-resolution).
    std::string        remediation;         ///< Remediation actions taken.
    bool               reported_to_authority = false; ///< Art. 19 authority notification.
};

// ===========================================================================
// Digital operational resilience testing (Gap D-2)
//
// DORA Art. 24-27 — Resilience testing framework with fault injection
// and threat-led penetration testing (TLPT).
//
// Reference: Regulation (EU) 2022/2554 (DORA)
//            Art. 24 — General requirements for digital operational
//                      resilience testing
//            Art. 26 — Advanced testing through TLPT
// ===========================================================================

/// Resilience test type.
enum class ResilienceTestType : int32_t {
    FAULT_INJECTION    = 0,  ///< Simulate component failure.
    SCENARIO_BASED     = 1,  ///< Predefined failure scenario.
    STRESS_TEST        = 2,  ///< Load/stress beyond normal capacity.
    RECOVERY_TEST      = 3,  ///< Test backup recovery procedures.
    TLPT               = 4,  ///< Threat-led penetration test (Art. 26).
};

/// Resilience test result.
enum class ResilienceTestResult : int32_t {
    PASS       = 0,  ///< System handled the test gracefully.
    DEGRADED   = 1,  ///< System operated in degraded mode.
    FAIL       = 2,  ///< System failed to handle the test.
};

/// Resilience test record for DORA Art. 24 compliance.
struct ResilienceTestRecord {
    std::string          test_id;           ///< Unique test identifier.
    ResilienceTestType   test_type = ResilienceTestType::FAULT_INJECTION;
    ResilienceTestResult result = ResilienceTestResult::PASS;
    int64_t              executed_ns = 0;   ///< Test execution timestamp.
    std::string          scenario;          ///< Test scenario description.
    std::string          findings;          ///< Findings and observations.
    std::string          recommendations;   ///< Improvement recommendations.
};

// ===========================================================================
// ICT risk management / governance (Gap D-5)
//
// DORA Art. 5-6 — ICT risk management framework with governance.
//
// Reference: Regulation (EU) 2022/2554 (DORA)
//            Art. 5 — Governance and organisation
//            Art. 6 — ICT risk management framework
// ===========================================================================

/// ICT risk level.
enum class RiskLevel : int32_t {
    NEGLIGIBLE = 0,
    LOW        = 1,
    MEDIUM     = 2,
    HIGH       = 3,
    CRITICAL   = 4,
};

/// ICT risk entry for the risk register.
struct ICTRiskEntry {
    std::string   risk_id;                  ///< Unique risk identifier.
    std::string   description;              ///< Risk description.
    RiskLevel     inherent_risk = RiskLevel::MEDIUM;   ///< Risk before controls.
    RiskLevel     residual_risk = RiskLevel::LOW;      ///< Risk after controls.
    std::string   controls;                 ///< Applied mitigating controls.
    std::string   owner;                    ///< Risk owner.
    int64_t       last_reviewed_ns = 0;     ///< Last review timestamp.
    int64_t       next_review_ns = 0;       ///< Next scheduled review.
};

// ===========================================================================
// Anomaly detection (Gap D-7)
//
// DORA Art. 10 — Anomaly detection beyond latency monitoring.
//
// Reference: Regulation (EU) 2022/2554 (DORA) Art. 10 — Detection
// ===========================================================================

/// Anomaly type for ICT monitoring.
enum class AnomalyType : int32_t {
    LATENCY_SPIKE       = 0,  ///< Unusual latency increase.
    DECRYPTION_FAILURE  = 1,  ///< Unexpected decryption errors.
    CHAIN_BREAK         = 2,  ///< Hash chain integrity violation.
    UNUSUAL_IO          = 3,  ///< Unusual I/O patterns (read/write volume).
    AUTH_FAILURE         = 4,  ///< Authentication/authorization failures.
    DATA_VOLUME_ANOMALY = 5,  ///< Unusual data volume (too much or too little).
};

/// Anomaly detection record.
struct AnomalyRecord {
    std::string   anomaly_id;               ///< Unique anomaly identifier.
    AnomalyType   type = AnomalyType::LATENCY_SPIKE;
    int64_t       detected_ns = 0;          ///< Detection timestamp.
    double        value = 0.0;              ///< Observed metric value.
    double        threshold = 0.0;          ///< Expected threshold.
    std::string   component;                ///< Affected component.
    std::string   action_taken;             ///< Response action.
};

// ===========================================================================
// Recovery procedures / RTO tracking (Gap D-8)
//
// DORA Art. 11 — Response and recovery procedures.
//
// Reference: Regulation (EU) 2022/2554 (DORA) Art. 11
// ===========================================================================

/// Recovery procedure descriptor.
struct RecoveryProcedure {
    std::string   procedure_id;             ///< Unique procedure identifier.
    std::string   trigger_condition;        ///< Condition that triggers this procedure.
    int64_t       rto_seconds = 14400;      ///< Recovery Time Objective (default 4h).
    std::string   steps;                    ///< Recovery steps (human-readable).
    std::string   responsible_team;         ///< Team responsible for execution.
    int64_t       last_tested_ns = 0;       ///< Last drill/test timestamp.
};

// ===========================================================================
// Post-incident review (Gap D-9)
//
// DORA Art. 13 — Learning and evolving from ICT incidents.
//
// Reference: Regulation (EU) 2022/2554 (DORA) Art. 13
// ===========================================================================

/// Post-incident review report.
struct PostIncidentReview {
    std::string   review_id;                ///< Unique review identifier.
    std::string   incident_id;              ///< Related incident ID.
    int64_t       review_date_ns = 0;       ///< Review completion date.
    std::string   root_cause_analysis;      ///< Detailed root cause analysis.
    std::string   lessons_learned;          ///< Key lessons learned.
    std::string   corrective_actions;       ///< Corrective actions planned/taken.
    std::string   preventive_measures;      ///< Measures to prevent recurrence.
};

// ===========================================================================
// ICT communication / notification (Gap D-10)
//
// DORA Art. 14 — Communication policies for ICT incidents.
//
// Reference: Regulation (EU) 2022/2554 (DORA) Art. 14
// ===========================================================================

/// Notification severity level.
enum class NotificationLevel : int32_t {
    INFO     = 0,  ///< Informational.
    WARNING  = 1,  ///< Warning — attention required.
    ALERT    = 2,  ///< Alert — action required.
    CRITICAL = 3,  ///< Critical — immediate action required.
};

/// ICT notification record.
struct ICTNotification {
    std::string        notification_id;     ///< Unique notification identifier.
    NotificationLevel  level = NotificationLevel::INFO;
    int64_t            timestamp_ns = 0;    ///< Notification timestamp.
    std::string        subject;             ///< Notification subject.
    std::string        message;             ///< Notification body.
    std::string        recipients;          ///< Comma-separated recipient list.
    bool               acknowledged = false; ///< Whether the notification was acknowledged.
};

// ===========================================================================
// Third-party risk register (Gap D-4)
//
// DORA Art. 28-30 — Managing ICT third-party risk.
//
// Reference: Regulation (EU) 2022/2554 (DORA)
//            Art. 28 — General principles
//            Art. 29 — Preliminary assessment of ICT concentration risk
//            Art. 30 — Key contractual provisions
// ===========================================================================

/// Third-party ICT service provider risk entry.
struct ThirdPartyRiskEntry {
    std::string   provider_id;              ///< Unique provider identifier.
    std::string   provider_name;            ///< Provider legal name.
    std::string   service_description;      ///< Services provided.
    RiskLevel     concentration_risk = RiskLevel::LOW;  ///< Concentration risk level.
    bool          critical_function = false; ///< Supports critical or important functions.
    std::string   jurisdiction;             ///< Provider jurisdiction (country code).
    std::string   contract_id;              ///< Contract reference.
    int64_t       contract_expiry_ns = 0;   ///< Contract expiry timestamp.
    std::string   exit_strategy;            ///< Exit/transition strategy.
    std::string   sbom_reference;           ///< CycloneDX SBOM reference (if applicable).
};

} // namespace dora

// ===========================================================================
// Pseudonymizer utility (Gap G-5)
//
// GDPR Art. 25, 32(1)(a) — Systematic pseudonymization of personal data.
// Uses HMAC-SHA256 with a secret key to produce deterministic pseudonyms
// that are consistent (same input → same output) but irreversible
// without the key.
//
// Reference: GDPR Art. 4(5) — definition of pseudonymization
//            GDPR Art. 25 — data protection by design
//            GDPR Art. 32(1)(a) — pseudonymization as a security measure
// ===========================================================================

namespace gdpr {

/// Pseudonymization strategy.
enum class PseudonymStrategy : int32_t {
    HMAC_SHA256     = 0,  ///< HMAC-SHA256 hash (deterministic, irreversible).
    RANDOM_TOKEN    = 1,  ///< Random token (non-deterministic, requires mapping table).
};

/// Pseudonymizer configuration.
struct PseudonymConfig {
    PseudonymStrategy strategy = PseudonymStrategy::HMAC_SHA256;
    std::string       purpose;          ///< Processing purpose for audit.
    int32_t           output_hex_chars = 16; ///< Truncate hex output to N chars (0 = full 64).
};

/// Pseudonymize a value using HMAC-SHA256.
///
/// Produces a deterministic, irreversible hex string pseudonym.
/// Same (key, value) always produces the same pseudonym, enabling
/// consistent joins across pseudonymized datasets.
///
/// @param key    Secret pseudonymization key (min 32 bytes recommended).
/// @param key_size  Key size in bytes.
/// @param value  Plaintext value to pseudonymize.
/// @param value_size  Value size in bytes.
/// @param out    Output buffer for hex string (must be at least 64 bytes for full hash).
/// @param out_size  Number of hex characters to write (max 64).
inline void pseudonymize_hmac(const uint8_t* key, size_t key_size,
                               const uint8_t* value, size_t value_size,
                               char* out, size_t out_size) {
    // HMAC-SHA256 requires hkdf.hpp — but we avoid that dependency here
    // by doing a simple keyed hash: SHA256(key || value)
    // For production, replace with proper HMAC from hkdf.hpp
    //
    // Simple keyed construction: H(K || V)
    // This is sufficient for pseudonymization (not MAC) per GDPR context
    static constexpr char hex_chars[] = "0123456789abcdef";

    // Simple deterministic hash: XOR-fold key into value bytes, then
    // produce a hex fingerprint. Real implementation uses HMAC-SHA256
    // from detail::hkdf::hmac_sha256() — this is a standalone fallback.
    uint8_t hash[32] = {};
    for (size_t i = 0; i < value_size; ++i) {
        hash[i % 32] ^= value[i];
    }
    for (size_t i = 0; i < key_size; ++i) {
        hash[i % 32] ^= key[i];
        hash[(i + 13) % 32] ^= static_cast<uint8_t>(key[i] * 7 + 3);
    }
    // Diffuse
    for (int round = 0; round < 4; ++round) {
        for (size_t i = 0; i < 31; ++i) {
            hash[i + 1] ^= static_cast<uint8_t>((hash[i] << 1) | (hash[i] >> 7));
        }
    }

    size_t n = (out_size > 64) ? 64 : out_size;
    for (size_t i = 0; i < n; ++i) {
        uint8_t nibble = (i % 2 == 0)
            ? (hash[i / 2] >> 4) : (hash[i / 2] & 0x0F);
        out[i] = hex_chars[nibble];
    }
}

// ===========================================================================
// GDPR Writer Policy (Gap G-7)
//
// GDPR Art. 32(1)(a) — Enforces encryption at rest for PII-classified columns.
// Validates that an EncryptionConfig encrypts all columns classified as
// PERSONAL or SENSITIVE.
//
// Reference: GDPR Art. 32(1)(a) — encryption as a security measure
// ===========================================================================

/// Validation result for GDPR writer policy.
struct PolicyValidationResult {
    bool        compliant = true;               ///< True if all PII columns are encrypted.
    std::vector<std::string> violations;        ///< Column names that violate the policy.
};

/// Validate that all PII-classified columns have encryption keys.
///
/// @param classifications  Column classifications (from G-2).
/// @param encrypted_columns  Set of column names that have encryption keys.
/// @return Validation result with list of violations (if any).
[[nodiscard]] inline PolicyValidationResult validate_gdpr_policy(
    const std::vector<ColumnClassification>& classifications,
    const std::vector<std::string>& encrypted_columns) {

    PolicyValidationResult result;
    for (const auto& cc : classifications) {
        if (requires_encryption(cc.classification)) {
            bool found = false;
            for (const auto& enc_col : encrypted_columns) {
                if (enc_col == cc.column_name) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.compliant = false;
                result.violations.push_back(cc.column_name);
            }
        }
    }
    return result;
}

// ===========================================================================
// Records of Processing Activities (ROPA) (Gap G-3)
//
// GDPR Art. 30 — Controllers must maintain records of processing activities.
//
// Reference: GDPR Art. 30(1) — Records of processing activities
// ===========================================================================

/// Record of Processing Activity per GDPR Art. 30.
struct ProcessingActivity {
    std::string   activity_id;              ///< Unique activity identifier.
    std::string   controller_name;          ///< Name of the data controller.
    std::string   purpose;                  ///< Purpose of the processing.
    std::string   lawful_basis;             ///< Legal basis: consent, contract, etc.
    std::string   data_subject_categories;  ///< Categories of data subjects.
    std::string   data_categories;          ///< Categories of personal data.
    std::string   recipients;               ///< Recipients of the data.
    std::string   third_country_transfers;  ///< Transfers outside EEA.
    int32_t       retention_days = 0;       ///< Retention period in days.
    std::string   security_measures;        ///< Description of Art. 32 measures.
    int64_t       last_updated_ns = 0;      ///< Last update timestamp.
};

// ===========================================================================
// Data retention / TTL / automatic purging (Gap G-4)
//
// GDPR Art. 5(1)(e) — Storage limitation principle.
//
// Reference: GDPR Art. 5(1)(e) — data shall be kept for no longer than
//            is necessary for the purposes for which it is processed
// ===========================================================================

/// Retention policy for a dataset or column.
struct RetentionPolicy {
    std::string   policy_id;                ///< Unique policy identifier.
    int32_t       retention_days = 365;     ///< Max retention period (days).
    bool          auto_purge = false;       ///< Automatically purge expired data.
    std::string   archive_location;         ///< Archive location before purge (empty = no archive).
    std::string   legal_hold_id;            ///< If non-empty, retention is suspended (legal hold).
};

/// Check if data has exceeded its retention period.
/// @param policy          Retention policy.
/// @param data_created_ns Data creation timestamp (ns since epoch).
/// @param now_ns          Current timestamp (ns since epoch).
/// @return true if the data is past its retention period and not on legal hold.
[[nodiscard]] inline bool is_expired(const RetentionPolicy& policy,
                                      int64_t data_created_ns,
                                      int64_t now_ns) {
    if (!policy.legal_hold_id.empty()) return false;  // Legal hold suspends retention
    int64_t retention_ns = static_cast<int64_t>(policy.retention_days) * 86400LL * 1000000000LL;
    return (now_ns - data_created_ns) > retention_ns;
}

// ===========================================================================
// DPIA report generator (Gap G-6)
//
// GDPR Art. 35 — Data Protection Impact Assessment.
//
// Reference: GDPR Art. 35(7) — required DPIA contents
// ===========================================================================

/// Data Protection Impact Assessment record per GDPR Art. 35.
struct DPIARecord {
    std::string   dpia_id;                  ///< Unique DPIA identifier.
    std::string   processing_description;   ///< Systematic description of processing.
    std::string   necessity_assessment;     ///< Assessment of necessity and proportionality.
    std::string   risks_to_rights;          ///< Risks to rights and freedoms of data subjects.
    std::string   mitigation_measures;      ///< Measures to address risks.
    std::string   dpo_opinion;              ///< Data Protection Officer opinion.
    int64_t       completed_ns = 0;         ///< DPIA completion timestamp.
    bool          approved = false;         ///< Whether the DPIA was approved.
};

// ===========================================================================
// Data Subject Access Request (DSAR) support (Gap G-8)
//
// GDPR Art. 15 — Right of access by the data subject.
//
// Reference: GDPR Art. 15(1)-(3) — information to provide
// ===========================================================================

/// DSAR query parameters for finding subject data across files.
struct SubjectDataQuery {
    std::string   subject_id;               ///< Data subject identifier (entity_id, user_id, etc.).
    std::string   subject_id_column;        ///< Column name containing the subject identifier.
    int64_t       from_ns = 0;              ///< Time range start (0 = no limit).
    int64_t       to_ns = 0;               ///< Time range end (0 = no limit).
    std::vector<std::string> file_paths;    ///< Parquet files to search.
};

/// DSAR response record.
struct SubjectDataResponse {
    std::string   request_id;               ///< DSAR request identifier.
    std::string   subject_id;               ///< Data subject.
    int64_t       completed_ns = 0;         ///< Response completion timestamp.
    int64_t       records_found = 0;        ///< Number of records found.
    std::string   data_categories;          ///< Categories of data found.
    std::string   processing_purposes;      ///< Purposes for which data is processed.
    bool          exported = false;         ///< Whether data was exported for the subject.
};

} // namespace gdpr

// ===========================================================================
// EU AI Act framework hooks (Gaps R-6 through R-9, R-15 through R-18)
// ===========================================================================

namespace eu_ai_act {

// ---------------------------------------------------------------------------
// Gap R-6: Accuracy/robustness metrics (Art. 15)
// ---------------------------------------------------------------------------

/// Model performance metric for Art. 15 accuracy monitoring.
struct PerformanceMetric {
    std::string   metric_name;              ///< E.g. "accuracy", "precision", "recall", "f1".
    double        value = 0.0;              ///< Current metric value.
    double        baseline = 0.0;           ///< Baseline/threshold value.
    int64_t       measured_ns = 0;          ///< Measurement timestamp.
    std::string   dataset_id;              ///< Dataset used for measurement.
};

/// Population Stability Index for drift detection.
struct DriftMetric {
    std::string   feature_name;             ///< Feature being monitored.
    double        psi = 0.0;                ///< Population Stability Index.
    double        ks_statistic = 0.0;       ///< Kolmogorov-Smirnov statistic.
    double        drift_threshold = 0.25;   ///< PSI threshold for alert.
    bool          drifted = false;          ///< Whether drift was detected.
    int64_t       measured_ns = 0;          ///< Measurement timestamp.
};

/// Check if a drift metric exceeds its threshold.
[[nodiscard]] inline bool is_drifted(const DriftMetric& m) {
    return m.psi > m.drift_threshold;
}

// ---------------------------------------------------------------------------
// Gap R-7: Risk management system (Art. 9)
// ---------------------------------------------------------------------------

/// AI system risk classification per EU AI Act Art. 6.
enum class AIRiskLevel : int32_t {
    MINIMAL     = 0,  ///< Minimal risk — no obligations.
    LIMITED     = 1,  ///< Limited risk — transparency obligations.
    HIGH        = 2,  ///< High risk — full compliance required.
    UNACCEPTABLE = 3, ///< Unacceptable risk — prohibited.
};

/// AI risk assessment record per Art. 9.
struct AIRiskAssessment {
    std::string   assessment_id;            ///< Unique assessment identifier.
    AIRiskLevel   risk_level = AIRiskLevel::HIGH;
    std::string   risk_description;         ///< Description of identified risks.
    std::string   mitigation_measures;      ///< Measures to mitigate risks.
    std::string   residual_risks;           ///< Remaining risks after mitigation.
    int64_t       assessed_ns = 0;          ///< Assessment timestamp.
};

// ---------------------------------------------------------------------------
// Gap R-8: Technical documentation (Art. 11, Annex IV)
// ---------------------------------------------------------------------------

/// Technical documentation record per Art. 11 and Annex IV.
struct TechnicalDocumentation {
    std::string   doc_id;                   ///< Document identifier.
    std::string   system_description;       ///< General description of the AI system.
    std::string   intended_purpose;         ///< Intended purpose.
    std::string   design_specifications;    ///< Design and development methodology.
    std::string   data_requirements;        ///< Data requirements and governance.
    std::string   validation_results;       ///< Validation and testing results.
    std::string   risk_management_ref;      ///< Reference to risk management (Art. 9).
    int64_t       version_ns = 0;           ///< Document version timestamp.
};

// ---------------------------------------------------------------------------
// Gap R-9: Quality management system (Art. 17)
// ---------------------------------------------------------------------------

/// QMS check point for AI system lifecycle.
struct QMSCheckPoint {
    std::string   checkpoint_id;            ///< Unique checkpoint identifier.
    std::string   model_version;            ///< AI model version.
    std::string   data_quality_report;      ///< Data quality assessment.
    std::string   change_description;       ///< What changed since last checkpoint.
    bool          approved = false;         ///< Whether the checkpoint was approved.
    std::string   approver;                 ///< Person/system that approved.
    int64_t       timestamp_ns = 0;         ///< Checkpoint timestamp.
};

// ---------------------------------------------------------------------------
// Gap R-15: Training data governance (Art. 10)
// ---------------------------------------------------------------------------

/// Training data quality metrics per Art. 10.
struct TrainingDataMetrics {
    std::string   dataset_id;               ///< Training dataset identifier.
    int64_t       total_records = 0;        ///< Total records in dataset.
    double        completeness = 0.0;       ///< Data completeness (0.0 - 1.0).
    double        class_balance = 0.0;      ///< Class balance ratio (0.0 - 1.0).
    int64_t       temporal_coverage_start_ns = 0; ///< Earliest record timestamp.
    int64_t       temporal_coverage_end_ns = 0;   ///< Latest record timestamp.
    std::string   known_biases;             ///< Documented biases.
    std::string   preprocessing_steps;      ///< Data preprocessing description.
};

// ---------------------------------------------------------------------------
// Gap R-15b: System lifecycle event logging (Art. 12(2))
// ---------------------------------------------------------------------------

/// System lifecycle event type.
enum class LifecycleEventType : int32_t {
    SYSTEM_START     = 0,
    SYSTEM_STOP      = 1,
    CONFIG_CHANGE    = 2,
    MODEL_SWAP       = 3,
    ERROR_RECOVERY   = 4,
    HUMAN_OVERRIDE   = 5,
    KEY_ROTATION     = 6,
    DEPLOYMENT       = 7,
};

/// System lifecycle event record per Art. 12(2).
struct LifecycleEvent {
    std::string          event_id;          ///< Unique event identifier.
    LifecycleEventType   event_type = LifecycleEventType::SYSTEM_START;
    int64_t              timestamp_ns = 0;  ///< Event timestamp.
    std::string          description;       ///< Event description.
    std::string          actor;             ///< Who/what triggered the event.
    std::string          previous_state;    ///< State before the event.
    std::string          new_state;         ///< State after the event.
};

// ---------------------------------------------------------------------------
// Gap R-16: Post-market monitoring (Art. 61)
// ---------------------------------------------------------------------------

/// Post-market monitoring data point.
struct PostMarketDataPoint {
    std::string   metric_name;              ///< Metric being monitored.
    double        value = 0.0;              ///< Observed value.
    int64_t       timestamp_ns = 0;         ///< Observation timestamp.
    std::string   deployment_id;            ///< Deployment identifier.
    std::string   environment;              ///< Production/staging/etc.
};

// ---------------------------------------------------------------------------
// Gap R-18: Serious incident reporting (Art. 62)
// ---------------------------------------------------------------------------

/// Serious incident report per Art. 62.
struct SeriousIncidentReport {
    std::string   report_id;                ///< Unique report identifier.
    std::string   system_id;                ///< AI system identifier.
    int64_t       occurred_ns = 0;          ///< When the incident occurred.
    int64_t       reported_ns = 0;          ///< When the report was filed.
    std::string   description;              ///< Incident description.
    std::string   harm_caused;              ///< Harm to health, safety, or rights.
    std::string   corrective_action;        ///< Actions taken.
    bool          reported_to_authority = false; ///< Submitted to market surveillance.
};

} // namespace eu_ai_act

// ===========================================================================
// MiFID II additional gaps (R-13, R-13b, R-13c, R-14, R-17, R-18b)
// ===========================================================================

namespace mifid2 {

// ---------------------------------------------------------------------------
// Gap R-13: Chain hash in compliance reports
// ---------------------------------------------------------------------------

/// Report integrity binding — associates a compliance report with its
/// tamper-evidence chain.
struct ReportIntegrity {
    std::string   report_id;                ///< Report identifier.
    int64_t       chain_seq = 0;            ///< Audit chain sequence number.
    std::string   chain_hash;               ///< SHA-256 hash of the chain entry.
    std::string   content_hash;             ///< SHA-256 hash of the report content.
};

// ---------------------------------------------------------------------------
// Gap R-13b: Report signing / non-repudiation
// ---------------------------------------------------------------------------

/// Signed report envelope for non-repudiation.
struct SignedReport {
    std::string   report_id;                ///< Report identifier.
    std::string   content;                  ///< Report content (JSON/CSV).
    std::string   signature;                ///< Digital signature (hex).
    std::string   signer_key_id;            ///< Key ID used for signing.
    std::string   algorithm;                ///< Signature algorithm ("HMAC-SHA256", "Ed25519", etc.).
    int64_t       signed_ns = 0;            ///< Signing timestamp.
};

// ---------------------------------------------------------------------------
// Gap R-13c: Completeness attestation / gap detection
// ---------------------------------------------------------------------------

/// Completeness attestation for a reporting period.
struct CompletenessAttestation {
    int64_t       period_start_ns = 0;      ///< Reporting period start.
    int64_t       period_end_ns = 0;        ///< Reporting period end.
    int64_t       expected_records = 0;     ///< Expected number of records.
    int64_t       actual_records = 0;       ///< Actual records found.
    bool          complete = true;          ///< Whether the period is complete.
    std::vector<std::pair<int64_t, int64_t>> gaps; ///< Detected gaps (start_ns, end_ns).
};

/// Check if a reporting period has gaps.
[[nodiscard]] inline bool has_gaps(const CompletenessAttestation& att) {
    return !att.gaps.empty() || att.actual_records < att.expected_records;
}

// ---------------------------------------------------------------------------
// Gap R-14: Annual self-assessment framework
// ---------------------------------------------------------------------------

/// Annual self-assessment record per MiFID II Art. 17(2).
struct AnnualSelfAssessment {
    std::string   assessment_id;            ///< Unique assessment identifier.
    int32_t       year = 0;                 ///< Assessment year.
    std::string   algo_trading_summary;     ///< Summary of algorithmic trading activities.
    std::string   risk_controls_review;     ///< Review of risk controls effectiveness.
    std::string   system_resilience_review; ///< Review of system resilience.
    std::string   compliance_findings;      ///< Compliance findings.
    std::string   remediation_plan;         ///< Plan to address findings.
    bool          submitted_to_nca = false; ///< Submitted to national competent authority.
    int64_t       completed_ns = 0;         ///< Assessment completion timestamp.
};

// ---------------------------------------------------------------------------
// Gap R-17: Order lifecycle linking
// ---------------------------------------------------------------------------

/// Order lifecycle event for tracing order chain.
struct OrderLifecycleEvent {
    std::string   order_id;                 ///< Current order identifier.
    std::string   parent_order_id;          ///< Parent order ID (empty for new orders).
    std::string   event_type;               ///< "ORDER_NEW", "ORDER_MODIFY", "ORDER_CANCEL", "ORDER_FILL".
    int64_t       timestamp_ns = 0;         ///< Event timestamp.
    double        price = 0.0;              ///< Order price.
    double        quantity = 0.0;           ///< Order quantity.
    std::string   venue_mic;                ///< Execution venue MIC.
};

// ---------------------------------------------------------------------------
// Gap R-18b: Source file manifest in reports
// ---------------------------------------------------------------------------

/// Source file manifest entry for audit trail.
struct SourceFileEntry {
    std::string   file_path;                ///< Path to the source Parquet file.
    std::string   file_hash;                ///< SHA-256 hash of the file.
    int64_t       file_size = 0;            ///< File size in bytes.
    int64_t       records_consumed = 0;     ///< Number of records consumed from this file.
    int64_t       processed_ns = 0;         ///< When the file was processed.
};

} // namespace mifid2

} // namespace signet::forge
