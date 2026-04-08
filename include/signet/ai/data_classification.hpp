// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/data_classification.hpp requires SIGNET_ENABLE_COMMERCIAL=ON (AGPL-3.0 commercial tier). See LICENSE_COMMERCIAL."
#endif

// ---------------------------------------------------------------------------
// data_classification.hpp -- Formal Data Classification Ontology
//
// Gap G-9: Formal data classification ontology per DORA Art.8 + GDPR Art.32.
//
// Provides a structured, machine-readable data classification framework:
//   - DORA Art.8: ICT asset classification (data at rest, in transit)
//   - GDPR Art.9: Special categories of personal data
//   - GDPR Art.32: Appropriate security measures per classification
//   - NIST SP 800-60: Information types and security categorization
//
// Components:
//   - DataClassification: 4-tier confidentiality levels
//   - DataSensitivity: GDPR Art.9 special category types
//   - RegulatoryRegime: applicable regulatory frameworks
//   - DataClassificationRule: per-field classification + handling policy
//   - DataClassificationOntology: rule registry with validation
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/error.hpp"

#include <algorithm>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/// Data confidentiality level per DORA Art.8 + ISO 27001 Annex A.
enum class DataClassification : int32_t {
    PUBLIC            = 0,  ///< No confidentiality requirement
    INTERNAL          = 1,  ///< Business-internal, not for external sharing
    RESTRICTED        = 2,  ///< Regulated data (GDPR, FCA, MiFID II)
    HIGHLY_RESTRICTED = 3   ///< Cryptographic keys, trading secrets, PII
};

/// Data sensitivity per GDPR Art.9 special categories.
enum class DataSensitivity : int32_t {
    NEUTRAL        = 0,  ///< No special sensitivity
    PSEUDONYMISED  = 1,  ///< Identifiable only with additional key (Art.25)
    ANONYMISED     = 2,  ///< Irreversibly de-identified (Art.4(1))
    PII            = 3,  ///< Personally Identifiable Information
    FINANCIAL_PII  = 4,  ///< Financial account data, trading activity
    BIOMETRIC      = 5,  ///< Biometric data (Art.9 special category)
    HEALTH         = 6   ///< Health/genetic data (Art.9 special category)
};

/// Regulatory regime(s) applicable to the data.
enum class RegulatoryRegime : int32_t {
    NONE       = 0,
    GDPR       = 1,  ///< EU General Data Protection Regulation
    MIFID2     = 2,  ///< Markets in Financial Instruments Directive II
    DORA       = 3,  ///< Digital Operational Resilience Act
    EU_AI_ACT  = 4,  ///< EU Artificial Intelligence Act
    SOX        = 5,  ///< Sarbanes-Oxley Act
    SEC_17A4   = 6,  ///< SEC Rule 17a-4 (records retention)
    PCI_DSS    = 7,  ///< Payment Card Industry Data Security Standard
    HIPAA      = 8   ///< Health Insurance Portability and Accountability Act
};

// ---------------------------------------------------------------------------
// DataClassificationRule
// ---------------------------------------------------------------------------

/// Per-field data classification and handling policy.
/// Defined at namespace scope for Apple Clang compatibility.
struct DataClassificationRule {
    std::string field_name;     ///< Column/field path (e.g., "user.email", "price")
    DataClassification classification = DataClassification::INTERNAL;
    DataSensitivity sensitivity = DataSensitivity::NEUTRAL;
    RegulatoryRegime regime     = RegulatoryRegime::NONE;

    // --- Retention lifecycle ---
    int64_t min_retention_ns = 0;              ///< Minimum retention (0 = no min)
    int64_t max_retention_ns = INT64_C(157788000000000000); ///< Max retention (default 5y)

    // --- Processing restrictions ---
    bool require_encryption   = false;  ///< RESTRICTED/HIGHLY_RESTRICTED → true
    bool allow_pseudonymisation = true;
    bool allow_aggregation    = true;
    bool allow_ml_training    = true;   ///< PII, secrets → false
    bool allow_export         = true;   ///< HIGHLY_RESTRICTED → false
    bool allow_logging        = true;   ///< Biometric, health → false in plaintext

    // --- Purpose limitation (GDPR Art.5(1)(b)) ---
    std::vector<std::string> allowed_purposes;
};

// ---------------------------------------------------------------------------
// DataClassificationOntology
// ---------------------------------------------------------------------------

/// A named collection of data classification rules forming a formal ontology.
///
/// Validates field-level data handling against the registered rules.
/// Supports lookup by field name and bulk validation.
class DataClassificationOntology {
public:
    /// Construct an ontology with the given identifier.
    explicit DataClassificationOntology(const std::string& ontology_id = "default")
        : ontology_id_(ontology_id) {
        auto gate = commercial::require_feature("DataClassificationOntology");
        if (!gate) throw std::runtime_error(gate.error().message);
    }

    /// Add a classification rule for a field.
    void add_rule(const DataClassificationRule& rule) {
        rules_[rule.field_name] = rule;
    }

    /// Look up the classification rule for a field.
    /// Returns a default PUBLIC/NEUTRAL rule if the field is not registered.
    [[nodiscard]] DataClassificationRule lookup(const std::string& field_name) const {
        auto it = rules_.find(field_name);
        if (it != rules_.end()) return it->second;
        // Unknown fields default to PUBLIC/NEUTRAL (least restrictive) —
        // callers that need fail-closed semantics should register all fields
        // explicitly or use validate_handling() with require_encryption=true.
        DataClassificationRule dflt;
        dflt.field_name = field_name;
        dflt.classification = DataClassification::PUBLIC;
        dflt.sensitivity = DataSensitivity::NEUTRAL;
        return dflt;
    }

    /// Get all registered rules.
    [[nodiscard]] std::vector<DataClassificationRule> all_rules() const {
        std::vector<DataClassificationRule> out;
        out.reserve(rules_.size());
        for (const auto& [_, r] : rules_) out.push_back(r);
        return out;
    }

    /// Number of registered rules.
    [[nodiscard]] size_t size() const { return rules_.size(); }

    /// Ontology identifier.
    [[nodiscard]] const std::string& ontology_id() const { return ontology_id_; }

    /// Validate that a field's actual handling meets classification requirements.
    ///
    /// Returns an error if the field is classified above the actual sensitivity
    /// level (e.g., a HIGHLY_RESTRICTED field being processed without encryption).
    [[nodiscard]] expected<void> validate_handling(
        const std::string& field_name,
        bool is_encrypted,
        bool is_pseudonymised,
        bool purpose_is_allowed = true) const
    {
        auto rule = lookup(field_name);

        // HIGHLY_RESTRICTED or RESTRICTED fields must be encrypted
        if (rule.require_encryption && !is_encrypted) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "Data classification violation: field '" + field_name +
                "' requires encryption (classification=" +
                classification_name(rule.classification) + ")"};
        }

        // PII fields should be pseudonymised unless explicitly allowed
        if (rule.sensitivity >= DataSensitivity::PII &&
            !is_pseudonymised && !rule.allow_logging) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "Data classification violation: field '" + field_name +
                "' contains sensitive data and must be pseudonymised for logging"};
        }

        // Purpose limitation check
        if (!purpose_is_allowed && !rule.allowed_purposes.empty()) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                "Data classification violation: field '" + field_name +
                "' processing purpose not in allowed list (GDPR Art.5(1)(b))"};
        }

        return {};
    }

    /// Build a default ontology with standard financial/compliance field rules.
    [[nodiscard]] static DataClassificationOntology financial_default() {
        DataClassificationOntology ont("financial-default");

        // Public data
        ont.add_rule({"symbol", DataClassification::PUBLIC,
                      DataSensitivity::NEUTRAL, RegulatoryRegime::NONE});
        ont.add_rule({"timestamp", DataClassification::PUBLIC,
                      DataSensitivity::NEUTRAL, RegulatoryRegime::NONE});

        // Internal market data
        {
            DataClassificationRule r;
            r.field_name = "price";
            r.classification = DataClassification::INTERNAL;
            r.sensitivity = DataSensitivity::NEUTRAL;
            r.regime = RegulatoryRegime::MIFID2;
            r.min_retention_ns = INT64_C(157788000000000000); // 5y MiFID II
            ont.add_rule(r);
        }
        {
            DataClassificationRule r;
            r.field_name = "volume";
            r.classification = DataClassification::INTERNAL;
            r.sensitivity = DataSensitivity::NEUTRAL;
            r.regime = RegulatoryRegime::MIFID2;
            r.min_retention_ns = INT64_C(157788000000000000);
            ont.add_rule(r);
        }

        // Restricted trading data
        {
            DataClassificationRule r;
            r.field_name = "strategy_id";
            r.classification = DataClassification::RESTRICTED;
            r.sensitivity = DataSensitivity::NEUTRAL;
            r.regime = RegulatoryRegime::MIFID2;
            r.require_encryption = true;
            r.allow_ml_training = false;
            r.min_retention_ns = INT64_C(157788000000000000);
            ont.add_rule(r);
        }

        // Highly restricted PII
        {
            DataClassificationRule r;
            r.field_name = "trader_id";
            r.classification = DataClassification::HIGHLY_RESTRICTED;
            r.sensitivity = DataSensitivity::FINANCIAL_PII;
            r.regime = RegulatoryRegime::GDPR;
            r.require_encryption = true;
            r.allow_ml_training = false;
            r.allow_export = false;
            r.allow_logging = false;
            r.allowed_purposes = {"compliance-reporting", "regulatory-inquiry"};
            ont.add_rule(r);
        }

        // Cryptographic key material
        {
            DataClassificationRule r;
            r.field_name = "encryption_key";
            r.classification = DataClassification::HIGHLY_RESTRICTED;
            r.sensitivity = DataSensitivity::NEUTRAL;
            r.regime = RegulatoryRegime::PCI_DSS;
            r.require_encryption = true;
            r.allow_pseudonymisation = false;
            r.allow_aggregation = false;
            r.allow_ml_training = false;
            r.allow_export = false;
            r.allow_logging = false;
            ont.add_rule(r);
        }

        return ont;
    }

private:
    static std::string classification_name(DataClassification c) {
        switch (c) {
        case DataClassification::PUBLIC:            return "PUBLIC";
        case DataClassification::INTERNAL:          return "INTERNAL";
        case DataClassification::RESTRICTED:        return "RESTRICTED";
        case DataClassification::HIGHLY_RESTRICTED: return "HIGHLY_RESTRICTED";
        }
        return "UNKNOWN";
    }

    std::string ontology_id_;
    std::unordered_map<std::string, DataClassificationRule> rules_;
};

} // namespace signet::forge
