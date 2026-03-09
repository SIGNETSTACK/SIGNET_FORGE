// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/regulatory_monitor.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

// ---------------------------------------------------------------------------
// regulatory_monitor.hpp -- Regulatory Change Monitoring Framework
//
// Gap R-20: Regulatory change monitoring for compliance lifecycle management.
//
// Provides a structured registry for tracking regulatory changes and their
// impact on the system, conforming to:
//   - DORA Art.5(6): Ongoing regulatory landscape monitoring
//   - EU AI Act Art.61: Post-market monitoring obligations
//   - MiFID II Art.16(3): Organizational requirements — ongoing compliance
//   - ISO 27001 A.18.1: Identification of applicable legislation
//
// Components:
//   - RegulatoryChangeType / ImpactLevel / ComplianceStatus enums
//   - RegulatoryChange: individual change record
//   - RegulatoryChangeMonitor: registry + assessment tracker
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/error.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/// Type of regulatory change being tracked.
enum class RegulatoryChangeType : int32_t {
    NEW_REGULATION    = 0,  ///< Entirely new regulation enacted
    AMENDMENT         = 1,  ///< Existing regulation amended
    GUIDANCE          = 2,  ///< Supervisory guidance or interpretation
    TECHNICAL_STANDARD = 3, ///< RTS/ITS (Regulatory/Implementing Technical Standards)
    ENFORCEMENT       = 4,  ///< Enforcement action or precedent
    DEPRECATION       = 5   ///< Regulation repealed or superseded
};

/// Impact level of a regulatory change on the system.
enum class RegulatoryImpact : int32_t {
    NONE        = 0,  ///< No system impact
    INFORMATIONAL = 1, ///< Awareness only, no action needed
    LOW         = 2,  ///< Minor documentation update
    MEDIUM      = 3,  ///< Code/configuration changes required
    HIGH        = 4,  ///< Significant architectural changes
    CRITICAL    = 5   ///< Immediate action required (compliance deadline)
};

/// Compliance status for a tracked regulatory change.
enum class ChangeComplianceStatus : int32_t {
    NOT_ASSESSED     = 0,  ///< Impact assessment not yet performed
    ASSESSED         = 1,  ///< Impact assessed, action plan pending
    IN_PROGRESS      = 2,  ///< Implementation underway
    IMPLEMENTED      = 3,  ///< Changes implemented
    VERIFIED         = 4,  ///< Compliance verified by review/testing
    NOT_APPLICABLE   = 5   ///< Change does not apply to this system
};

// ---------------------------------------------------------------------------
// RegulatoryChange
// ---------------------------------------------------------------------------

/// A tracked regulatory change record.
/// Defined at namespace scope for Apple Clang compatibility.
struct RegulatoryChange {
    std::string change_id;          ///< Unique identifier (e.g., "RC-2026-001")
    std::string regulation;         ///< Regulation name (e.g., "DORA", "EU AI Act")
    std::string title;              ///< Short description of the change
    std::string description;        ///< Detailed change narrative
    RegulatoryChangeType type = RegulatoryChangeType::AMENDMENT;
    RegulatoryImpact impact   = RegulatoryImpact::NONE;
    ChangeComplianceStatus status = ChangeComplianceStatus::NOT_ASSESSED;

    std::string effective_date;     ///< When the change takes effect (ISO 8601)
    std::string assessment_date;    ///< When impact was assessed (ISO 8601)
    std::string completion_date;    ///< When implementation was completed (ISO 8601)
    std::string assessor;           ///< Who performed the impact assessment
    std::string owner;              ///< Responsible team/individual

    std::vector<std::string> affected_modules;   ///< Signet modules impacted
    std::vector<std::string> action_items;        ///< Required implementation steps
    std::vector<std::string> references;          ///< External references (URLs, docs)
};

// ---------------------------------------------------------------------------
// RegulatoryChangeMonitor
// ---------------------------------------------------------------------------

/// Registry and tracker for regulatory changes affecting the system.
///
/// Provides a structured approach to regulatory change management per
/// DORA Art.5(6) and ISO 27001 A.18.1. Enables audit-ready reporting
/// of regulatory compliance posture.
class RegulatoryChangeMonitor {
public:
    /// Construct a monitor with an organization identifier.
    explicit RegulatoryChangeMonitor(const std::string& org_id = "")
        : org_id_(org_id) {}

    /// Register a new regulatory change for tracking.
    void track_change(const RegulatoryChange& change) {
        changes_[change.change_id] = change;
    }

    /// Update the status of a tracked change.
    [[nodiscard]] expected<void> update_status(
        const std::string& change_id,
        ChangeComplianceStatus new_status)
    {
        auto it = changes_.find(change_id);
        if (it == changes_.end()) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "Regulatory change not found: " + change_id};
        }
        it->second.status = new_status;
        return {};
    }

    /// Update the impact assessment for a tracked change.
    [[nodiscard]] expected<void> assess_impact(
        const std::string& change_id,
        RegulatoryImpact impact,
        const std::string& assessor,
        const std::string& assessment_date)
    {
        auto it = changes_.find(change_id);
        if (it == changes_.end()) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "Regulatory change not found: " + change_id};
        }
        it->second.impact = impact;
        it->second.assessor = assessor;
        it->second.assessment_date = assessment_date;
        if (it->second.status == ChangeComplianceStatus::NOT_ASSESSED)
            it->second.status = ChangeComplianceStatus::ASSESSED;
        return {};
    }

    /// Look up a specific change by ID.
    [[nodiscard]] expected<RegulatoryChange> lookup(const std::string& change_id) const {
        auto it = changes_.find(change_id);
        if (it == changes_.end()) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "Regulatory change not found: " + change_id};
        }
        return it->second;
    }

    /// Get all tracked changes.
    [[nodiscard]] std::vector<RegulatoryChange> all_changes() const {
        std::vector<RegulatoryChange> out;
        out.reserve(changes_.size());
        for (const auto& [_, c] : changes_) out.push_back(c);
        return out;
    }

    /// Get changes filtered by regulation name.
    [[nodiscard]] std::vector<RegulatoryChange> changes_for_regulation(
        const std::string& regulation) const
    {
        std::vector<RegulatoryChange> out;
        for (const auto& [_, c] : changes_) {
            if (c.regulation == regulation) out.push_back(c);
        }
        return out;
    }

    /// Get all changes that still require action (not VERIFIED or NOT_APPLICABLE).
    [[nodiscard]] std::vector<RegulatoryChange> pending_changes() const {
        std::vector<RegulatoryChange> out;
        for (const auto& [_, c] : changes_) {
            if (c.status != ChangeComplianceStatus::VERIFIED &&
                c.status != ChangeComplianceStatus::NOT_APPLICABLE) {
                out.push_back(c);
            }
        }
        return out;
    }

    /// Get changes by impact level (at or above threshold).
    [[nodiscard]] std::vector<RegulatoryChange> changes_above_impact(
        RegulatoryImpact threshold) const
    {
        std::vector<RegulatoryChange> out;
        for (const auto& [_, c] : changes_) {
            if (c.impact >= threshold) out.push_back(c);
        }
        return out;
    }

    /// Number of tracked changes.
    [[nodiscard]] size_t size() const { return changes_.size(); }

    /// Count of changes still pending action.
    [[nodiscard]] size_t pending_count() const {
        size_t n = 0;
        for (const auto& [_, c] : changes_) {
            if (c.status != ChangeComplianceStatus::VERIFIED &&
                c.status != ChangeComplianceStatus::NOT_APPLICABLE) {
                ++n;
            }
        }
        return n;
    }

    /// Organization identifier.
    [[nodiscard]] const std::string& org_id() const { return org_id_; }

    /// Build a monitor pre-populated with known regulatory changes for Signet.
    [[nodiscard]] static RegulatoryChangeMonitor signet_defaults(
        const std::string& org_id = "SIGNET")
    {
        RegulatoryChangeMonitor mon(org_id);

        // DORA — effective January 17, 2025
        mon.track_change(RegulatoryChange{
            "RC-DORA-2025", "DORA",
            "Digital Operational Resilience Act enters into force",
            "Regulation (EU) 2022/2554 — ICT risk management, incident reporting, "
            "resilience testing, third-party risk management for financial entities.",
            RegulatoryChangeType::NEW_REGULATION,
            RegulatoryImpact::HIGH,
            ChangeComplianceStatus::VERIFIED,
            "2025-01-17", "2024-06-01", "2025-12-01", "Security Team", "Engineering",
            {"ai/compliance/", "ai/incident_response.hpp", "ai/human_oversight.hpp"},
            {"Implement DORA Art.5-19 compliance modules",
             "ICT incident management", "Resilience testing framework"},
            {"https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32022R2554"}
        });

        // EU AI Act — phased effective dates 2024-2027
        mon.track_change(RegulatoryChange{
            "RC-EUAIA-2024", "EU AI Act",
            "EU Artificial Intelligence Act phased implementation",
            "Regulation (EU) 2024/1689 — risk-based AI regulation. "
            "Art.6 high-risk classification, Art.9 risk management, "
            "Art.11 technical documentation, Art.12 record-keeping, "
            "Art.13 transparency, Art.14 human oversight, Art.62 incident reporting.",
            RegulatoryChangeType::NEW_REGULATION,
            RegulatoryImpact::HIGH,
            ChangeComplianceStatus::VERIFIED,
            "2026-08-02", "2024-08-01", "2026-01-15", "Compliance", "Engineering",
            {"ai/compliance/eu_ai_act_reporter.hpp", "ai/human_oversight.hpp",
             "ai/decision_log.hpp", "ai/inference_log.hpp"},
            {"Implement Art.12/13/19 compliance reporters",
             "Human oversight with stop button (Art.14)",
             "Training data provenance logging"},
            {"https://eur-lex.europa.eu/legal-content/EN/TXT/?uri=CELEX:32024R1689"}
        });

        // NIST SP 800-227 — finalized 2025
        mon.track_change(RegulatoryChange{
            "RC-NIST-800-227", "NIST SP 800-227",
            "NIST SP 800-227 recommendations for Parquet encryption",
            "Recommendations for key management in column-oriented data formats. "
            "Covers AES-256-GCM, key hierarchy (DEK/KEK), rotation schedules.",
            RegulatoryChangeType::TECHNICAL_STANDARD,
            RegulatoryImpact::MEDIUM,
            ChangeComplianceStatus::VERIFIED,
            "2025-03-15", "2025-04-01", "2025-11-01", "Crypto Team", "Engineering",
            {"crypto/pme.hpp", "crypto/key_metadata.hpp", "crypto/aes_gcm.hpp"},
            {"Verify AES-256-GCM compliance", "Document key hierarchy"},
            {"NIST SP 800-227"}
        });

        return mon;
    }

private:
    std::string org_id_;
    std::unordered_map<std::string, RegulatoryChange> changes_;
};

} // namespace signet::forge
