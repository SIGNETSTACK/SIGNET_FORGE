// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/incident_response.hpp requires SIGNET_ENABLE_COMMERCIAL=ON (AGPL-3.0 commercial tier). See LICENSE_COMMERCIAL."
#endif

// ---------------------------------------------------------------------------
// incident_response.hpp -- Incident Response Playbook Framework
//
// Gap R-19: Structured incident response playbook for regulatory compliance.
//
// Provides a machine-readable incident response framework conforming to:
//   - DORA Art.10/15/19: ICT incident management lifecycle
//   - NIST SP 800-61 Rev. 2: Computer Security Incident Handling Guide
//   - ISO 27035: Information security incident management
//   - EU AI Act Art.62: Serious incident reporting
//
// Components:
//   - IncidentPhase / IncidentSeverity / EscalationLevel enums
//   - PlaybookStep: individual response action with SLA
//   - IncidentPlaybook: ordered sequence of response steps
//   - PlaybookRegistry: lookup playbooks by incident type
//   - IncidentResponseTracker: tracks execution of playbook steps
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/error.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/// NIST SP 800-61 incident response lifecycle phases.
enum class IncidentPhase : int32_t {
    PREPARATION     = 0,  ///< Pre-incident readiness
    DETECTION       = 1,  ///< Anomaly detection / alert triage
    CONTAINMENT     = 2,  ///< Limit blast radius
    ERADICATION     = 3,  ///< Remove root cause
    RECOVERY        = 4,  ///< Restore normal operations
    LESSONS_LEARNED = 5   ///< Post-incident review (DORA Art.13)
};

/// Incident severity per DORA Art.10(1) classification.
enum class IncidentSeverity : int32_t {
    P4_LOW       = 0,  ///< Minor, no customer impact
    P3_MEDIUM    = 1,  ///< Limited impact, workaround available
    P2_HIGH      = 2,  ///< Significant impact, SLA breach risk
    P1_CRITICAL  = 3   ///< Major outage, data loss, regulatory notification required
};

/// Escalation hierarchy for incident routing.
enum class EscalationLevel : int32_t {
    L1_OPERATIONS  = 0,  ///< First-line operations team
    L2_ENGINEERING = 1,  ///< Engineering / DevOps on-call
    L3_MANAGEMENT  = 2,  ///< Management / CISO notification
    L4_REGULATORY  = 3   ///< Regulatory authority notification (DORA Art.19)
};

/// Notification channel for incident communications.
enum class NotificationChannel : int32_t {
    INTERNAL_LOG = 0,  ///< System log only
    EMAIL        = 1,  ///< Email to responsible parties
    PAGER        = 2,  ///< PagerDuty / on-call alert
    REGULATORY   = 3   ///< Formal regulatory notification (DORA Art.19(1))
};

// ---------------------------------------------------------------------------
// PlaybookStep
// ---------------------------------------------------------------------------

/// A single step in an incident response playbook.
/// Defined at namespace scope for Apple Clang compatibility.
struct PlaybookStep {
    std::string step_id;            ///< Unique step identifier
    IncidentPhase phase = IncidentPhase::DETECTION;
    std::string action;             ///< What to do
    std::string responsible_role;   ///< Who performs this step
    EscalationLevel escalation = EscalationLevel::L1_OPERATIONS;
    int64_t sla_seconds = 0;        ///< Maximum time to complete (0 = no SLA)
    NotificationChannel notify = NotificationChannel::INTERNAL_LOG;
    std::vector<std::string> checklist;  ///< Sub-items to verify
    bool requires_sign_off = false; ///< Needs explicit sign-off before proceeding
};

// ---------------------------------------------------------------------------
// IncidentPlaybook
// ---------------------------------------------------------------------------

/// An ordered sequence of response steps for a specific incident type.
struct IncidentPlaybook {
    std::string playbook_id;     ///< Unique playbook identifier
    std::string incident_type;   ///< Category (e.g., "data_breach", "key_compromise")
    std::string version;         ///< Playbook version
    IncidentSeverity min_severity = IncidentSeverity::P4_LOW;
    std::vector<PlaybookStep> steps;
    std::vector<std::string> regulatory_references;

    /// Total number of steps in this playbook.
    [[nodiscard]] size_t step_count() const { return steps.size(); }

    /// Get all steps for a specific phase.
    [[nodiscard]] std::vector<PlaybookStep> steps_for_phase(IncidentPhase phase) const {
        std::vector<PlaybookStep> result;
        for (const auto& s : steps) {
            if (s.phase == phase) result.push_back(s);
        }
        return result;
    }
};

// ---------------------------------------------------------------------------
// PlaybookRegistry
// ---------------------------------------------------------------------------

/// Registry of incident response playbooks indexed by incident type.
class PlaybookRegistry {
public:
    PlaybookRegistry() {
        (void)commercial::require_feature("PlaybookRegistry");
    }

    /// Register a playbook for a specific incident type.
    void register_playbook(const IncidentPlaybook& pb) {
        playbooks_[pb.incident_type] = pb;
    }

    /// Look up a playbook by incident type.
    [[nodiscard]] expected<IncidentPlaybook> lookup(const std::string& incident_type) const {
        auto it = playbooks_.find(incident_type);
        if (it == playbooks_.end()) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "No playbook registered for incident type: " + incident_type};
        }
        return it->second;
    }

    /// Get all registered incident types.
    [[nodiscard]] std::vector<std::string> incident_types() const {
        std::vector<std::string> types;
        types.reserve(playbooks_.size());
        for (const auto& [k, _] : playbooks_) types.push_back(k);
        return types;
    }

    /// Number of registered playbooks.
    [[nodiscard]] size_t size() const { return playbooks_.size(); }

    /// Build a registry with default playbooks for financial/compliance scenarios.
    [[nodiscard]] static PlaybookRegistry financial_defaults() {
        PlaybookRegistry reg;
        reg.register_playbook(key_compromise_playbook());
        reg.register_playbook(data_breach_playbook());
        reg.register_playbook(service_outage_playbook());
        return reg;
    }

private:
    // --- Default playbooks ---

    static IncidentPlaybook key_compromise_playbook() {
        IncidentPlaybook pb;
        pb.playbook_id = "PB-CRYPTO-001";
        pb.incident_type = "key_compromise";
        pb.version = "1.0.0";
        pb.min_severity = IncidentSeverity::P1_CRITICAL;
        pb.regulatory_references = {
            "NIST SP 800-57 Part 2 Rev. 1 §6.1",
            "DORA Art.10(1)", "PCI-DSS Req. 3.6.8"
        };

        pb.steps = {
            {"KC-01", IncidentPhase::DETECTION,
             "Detect key compromise via anomaly detection or external report",
             "Security Operations", EscalationLevel::L2_ENGINEERING,
             300, NotificationChannel::PAGER,
             {"Verify alert authenticity", "Identify affected key IDs"}, false},

            {"KC-02", IncidentPhase::CONTAINMENT,
             "Revoke compromised keys in KMS and block further use",
             "Crypto Engineering", EscalationLevel::L2_ENGINEERING,
             900, NotificationChannel::PAGER,
             {"Revoke KEK in KMS", "Invalidate cached DEKs",
              "Enable emergency key rotation"}, true},

            {"KC-03", IncidentPhase::CONTAINMENT,
             "Notify CISO and legal team; assess regulatory notification obligation",
             "CISO", EscalationLevel::L3_MANAGEMENT,
             3600, NotificationChannel::EMAIL,
             {"Assess GDPR Art.33 72h window", "Assess DORA Art.19(1) notification"}, true},

            {"KC-04", IncidentPhase::ERADICATION,
             "Rotate all affected DEKs and re-encrypt impacted data",
             "Crypto Engineering", EscalationLevel::L2_ENGINEERING,
             86400, NotificationChannel::INTERNAL_LOG,
             {"Generate new DEKs via KMS", "Re-encrypt affected Parquet files",
              "Verify re-encryption with test reads"}, true},

            {"KC-05", IncidentPhase::RECOVERY,
             "Verify data integrity and restore normal key rotation schedule",
             "Engineering", EscalationLevel::L1_OPERATIONS,
             172800, NotificationChannel::INTERNAL_LOG,
             {"Verify hash chain integrity", "Resume automated key rotation",
              "Update key inventory"}, false},

            {"KC-06", IncidentPhase::LESSONS_LEARNED,
             "Conduct post-incident review and update threat model (DORA Art.13)",
             "Security Team", EscalationLevel::L3_MANAGEMENT,
             604800, NotificationChannel::EMAIL,
             {"Root cause analysis", "Update threat model D-12",
              "Document timeline for regulatory record"}, true},
        };
        return pb;
    }

    static IncidentPlaybook data_breach_playbook() {
        IncidentPlaybook pb;
        pb.playbook_id = "PB-DATA-001";
        pb.incident_type = "data_breach";
        pb.version = "1.0.0";
        pb.min_severity = IncidentSeverity::P1_CRITICAL;
        pb.regulatory_references = {
            "GDPR Art.33/34", "DORA Art.19", "EU AI Act Art.62"
        };

        pb.steps = {
            {"DB-01", IncidentPhase::DETECTION,
             "Identify scope of data breach — affected records, data types, time window",
             "Security Operations", EscalationLevel::L2_ENGINEERING,
             600, NotificationChannel::PAGER,
             {"Identify affected data classification levels (G-9)",
              "Count affected data subjects", "Determine attack vector"}, false},

            {"DB-02", IncidentPhase::CONTAINMENT,
             "Isolate affected systems and preserve forensic evidence",
             "Engineering", EscalationLevel::L2_ENGINEERING,
             1800, NotificationChannel::PAGER,
             {"Block attacker access", "Snapshot affected systems",
              "Preserve audit chain logs"}, true},

            {"DB-03", IncidentPhase::CONTAINMENT,
             "Notify DPO within 24h; prepare GDPR Art.33 notification (72h deadline)",
             "DPO / Legal", EscalationLevel::L3_MANAGEMENT,
             86400, NotificationChannel::EMAIL,
             {"Draft supervisory authority notification",
              "Assess need for Art.34 data subject notification"}, true},

            {"DB-04", IncidentPhase::ERADICATION,
             "Patch vulnerability, invoke crypto-shredding (G-1) if applicable",
             "Engineering", EscalationLevel::L2_ENGINEERING,
             172800, NotificationChannel::INTERNAL_LOG,
             {"Apply security patch", "Crypto-shred affected key material",
              "Reset affected credentials"}, true},

            {"DB-05", IncidentPhase::RECOVERY,
             "Verify remediation, restore services, monitor for reoccurrence",
             "Operations", EscalationLevel::L1_OPERATIONS,
             259200, NotificationChannel::INTERNAL_LOG,
             {"Verify breach vector is closed", "Resume normal monitoring",
              "Update anomaly detection rules"}, false},

            {"DB-06", IncidentPhase::LESSONS_LEARNED,
             "Post-incident review, update ROPA (G-3), notify regulator of completion",
             "Security Team", EscalationLevel::L4_REGULATORY,
             604800, NotificationChannel::REGULATORY,
             {"Root cause analysis", "Update ROPA records",
              "File final regulatory report"}, true},
        };
        return pb;
    }

    static IncidentPlaybook service_outage_playbook() {
        IncidentPlaybook pb;
        pb.playbook_id = "PB-SVC-001";
        pb.incident_type = "service_outage";
        pb.version = "1.0.0";
        pb.min_severity = IncidentSeverity::P2_HIGH;
        pb.regulatory_references = {"DORA Art.11", "DORA Art.10(1)"};

        pb.steps = {
            {"SO-01", IncidentPhase::DETECTION,
             "Detect service degradation via monitoring and alerting",
             "Operations", EscalationLevel::L1_OPERATIONS,
             120, NotificationChannel::PAGER,
             {"Verify monitoring alerts", "Check WAL ingestion pipeline"}, false},

            {"SO-02", IncidentPhase::CONTAINMENT,
             "Activate recovery procedures (DORA Art.11)",
             "Engineering", EscalationLevel::L2_ENGINEERING,
             900, NotificationChannel::PAGER,
             {"Failover to backup systems", "Enable WAL recovery mode"}, false},

            {"SO-03", IncidentPhase::RECOVERY,
             "Restore service and verify data integrity",
             "Engineering", EscalationLevel::L2_ENGINEERING,
             7200, NotificationChannel::EMAIL,
             {"Verify hash chain integrity", "Confirm no data loss",
              "Resume normal operations"}, true},

            {"SO-04", IncidentPhase::LESSONS_LEARNED,
             "Update resilience testing scenarios (D-2)",
             "Engineering", EscalationLevel::L1_OPERATIONS,
             604800, NotificationChannel::INTERNAL_LOG,
             {"Document root cause", "Update resilience test cases"}, false},
        };
        return pb;
    }

    std::unordered_map<std::string, IncidentPlaybook> playbooks_;
};

// ---------------------------------------------------------------------------
// IncidentResponseTracker
// ---------------------------------------------------------------------------

/// Tracks execution progress of a playbook during an active incident.
///
/// Records timestamps and sign-offs for each completed step,
/// enabling post-incident audit trail generation.
class IncidentResponseTracker {
public:
    /// Step completion record for audit trail.
    struct StepRecord {
        std::string step_id;
        std::string completed_by;
        int64_t started_at_ns = 0;
        int64_t completed_at_ns = 0;
        bool sla_met = true;
        std::string notes;
    };

    /// Initialize tracker for a specific incident and playbook.
    IncidentResponseTracker(const std::string& incident_id,
                            const IncidentPlaybook& playbook)
        : incident_id_(incident_id), playbook_(playbook) {
        (void)commercial::require_feature("IncidentResponseTracker");
    }

    /// Record completion of a playbook step.
    [[nodiscard]] expected<void> complete_step(
        const std::string& step_id,
        const std::string& completed_by,
        int64_t started_at_ns,
        int64_t completed_at_ns,
        const std::string& notes = "")
    {
        // Verify step exists in playbook
        bool found = false;
        int64_t sla_ns = 0;
        for (const auto& s : playbook_.steps) {
            if (s.step_id == step_id) {
                found = true;
                sla_ns = s.sla_seconds * INT64_C(1000000000);
                break;
            }
        }
        if (!found) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "Step '" + step_id + "' not found in playbook " +
                playbook_.playbook_id};
        }

        if (completed_at_ns < started_at_ns) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "completed_at must be >= started_at"};
        }

        StepRecord rec;
        rec.step_id = step_id;
        rec.completed_by = completed_by;
        rec.started_at_ns = started_at_ns;
        rec.completed_at_ns = completed_at_ns;
        rec.sla_met = (sla_ns == 0) ||
                      ((completed_at_ns - started_at_ns) <= sla_ns);
        rec.notes = notes;
        completed_.push_back(std::move(rec));
        return {};
    }

    /// Get all completed step records.
    [[nodiscard]] const std::vector<StepRecord>& completed_steps() const {
        return completed_;
    }

    /// Check if all playbook steps have been completed.
    [[nodiscard]] bool all_steps_complete() const {
        return completed_.size() == playbook_.steps.size();
    }

    /// Get remaining (uncompleted) step IDs.
    [[nodiscard]] std::vector<std::string> remaining_steps() const {
        std::vector<std::string> remaining;
        for (const auto& s : playbook_.steps) {
            bool done = false;
            for (const auto& c : completed_) {
                if (c.step_id == s.step_id) { done = true; break; }
            }
            if (!done) remaining.push_back(s.step_id);
        }
        return remaining;
    }

    /// Count of SLA breaches across completed steps.
    [[nodiscard]] int32_t sla_breach_count() const {
        int32_t count = 0;
        for (const auto& c : completed_) {
            if (!c.sla_met) ++count;
        }
        return count;
    }

    /// Incident identifier.
    [[nodiscard]] const std::string& incident_id() const { return incident_id_; }

    /// Associated playbook.
    [[nodiscard]] const IncidentPlaybook& playbook() const { return playbook_; }

private:
    std::string incident_id_;
    IncidentPlaybook playbook_;
    std::vector<StepRecord> completed_;
};

} // namespace signet::forge
