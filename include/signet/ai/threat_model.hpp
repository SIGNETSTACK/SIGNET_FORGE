// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/threat_model.hpp requires SIGNET_ENABLE_COMMERCIAL=ON (AGPL-3.0 commercial tier). See LICENSE_COMMERCIAL."
#endif

// ---------------------------------------------------------------------------
// threat_model.hpp -- STRIDE/DREAD Threat Modeling Framework
//
// Gap D-12: Structured threat modeling documentation for enterprise audits.
//
// Provides a machine-readable threat model conforming to:
//   - Microsoft STRIDE (Spoofing, Tampering, Repudiation, Info Disclosure,
//     Denial of Service, Elevation of Privilege)
//   - DREAD risk scoring (Damage, Reproducibility, Exploitability, Affected
//     users, Discoverability) — scale 1..10, composite = mean
//   - NIST SP 800-30 Rev. 1 risk assessment methodology
//   - OWASP Threat Modeling guidelines
//
// Components:
//   - StrideCategory / ThreatSeverity enums
//   - DreadScore: 5-factor risk quantification
//   - ThreatEntry: individual threat with mitigations
//   - ThreatModel: collection of threats for a component
//   - ThreatModelAnalyzer: validates coverage and generates JSON report
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/error.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/// Microsoft STRIDE threat categories.
enum class StrideCategory : int32_t {
    SPOOFING              = 0,  ///< Authentication bypass, identity impersonation
    TAMPERING             = 1,  ///< Unauthorized data modification
    REPUDIATION           = 2,  ///< Denying actions without proof
    INFORMATION_DISCLOSURE = 3, ///< Unauthorized data exposure
    DENIAL_OF_SERVICE     = 4,  ///< Resource exhaustion, availability attacks
    ELEVATION_OF_PRIVILEGE = 5  ///< Gaining unauthorized access levels
};

/// Threat severity classification per NIST SP 800-30.
enum class ThreatSeverity : int32_t {
    LOW      = 0,  ///< DREAD composite < 4.0
    MEDIUM   = 1,  ///< DREAD composite 4.0 - 6.9
    HIGH     = 2,  ///< DREAD composite 7.0 - 8.9
    CRITICAL = 3   ///< DREAD composite >= 9.0
};

/// Mitigation status for a threat.
enum class MitigationStatus : int32_t {
    NOT_MITIGATED  = 0,  ///< No mitigation in place
    PARTIAL        = 1,  ///< Some controls, residual risk remains
    MITIGATED      = 2,  ///< Fully mitigated by implemented controls
    ACCEPTED       = 3,  ///< Risk accepted per organizational policy
    TRANSFERRED    = 4   ///< Risk transferred (insurance, third-party)
};

// ---------------------------------------------------------------------------
// DREAD Risk Score
// ---------------------------------------------------------------------------

/// DREAD risk quantification — 5 factors scored 1..10.
/// Composite score = arithmetic mean of all 5 factors.
struct DreadScore {
    int32_t damage           = 1;  ///< Potential damage if exploited (1-10)
    int32_t reproducibility  = 1;  ///< Ease of reproducing the attack (1-10)
    int32_t exploitability   = 1;  ///< Effort required to exploit (1-10)
    int32_t affected_users   = 1;  ///< Fraction of users affected (1-10)
    int32_t discoverability  = 1;  ///< Ease of discovering the vulnerability (1-10)

    /// Composite DREAD score (arithmetic mean, 1.0 .. 10.0).
    [[nodiscard]] double composite() const {
        return (damage + reproducibility + exploitability +
                affected_users + discoverability) / 5.0;
    }

    /// Derive severity from composite score.
    [[nodiscard]] ThreatSeverity severity() const {
        double c = composite();
        if (c >= 9.0) return ThreatSeverity::CRITICAL;
        if (c >= 7.0) return ThreatSeverity::HIGH;
        if (c >= 4.0) return ThreatSeverity::MEDIUM;
        return ThreatSeverity::LOW;
    }

    /// Validate all factors are in range [1, 10].
    [[nodiscard]] bool valid() const {
        auto ok = [](int32_t v) { return v >= 1 && v <= 10; };
        return ok(damage) && ok(reproducibility) && ok(exploitability) &&
               ok(affected_users) && ok(discoverability);
    }
};

// ---------------------------------------------------------------------------
// Mitigation
// ---------------------------------------------------------------------------

/// A specific mitigation control for a threat.
struct Mitigation {
    std::string control_id;     ///< Unique identifier (e.g., "CTRL-AES-001")
    std::string description;    ///< What the control does
    std::string implementation; ///< Where in codebase (file:line or module)
    MitigationStatus status = MitigationStatus::NOT_MITIGATED;
};

// ---------------------------------------------------------------------------
// ThreatEntry
// ---------------------------------------------------------------------------

/// A single identified threat in the threat model.
struct ThreatEntry {
    std::string threat_id;           ///< Unique identifier (e.g., "T-CRYPT-001")
    std::string title;               ///< Short description
    std::string description;         ///< Detailed threat narrative
    StrideCategory category = StrideCategory::TAMPERING;
    DreadScore dread;                ///< Risk quantification
    std::string attack_vector;       ///< How the attack is carried out
    std::string affected_component;  ///< Module or subsystem at risk
    std::vector<Mitigation> mitigations;
    std::vector<std::string> references; ///< CVEs, NIST refs, etc.

    /// Overall mitigation status — worst (lowest) across all mitigations.
    [[nodiscard]] MitigationStatus overall_status() const {
        if (mitigations.empty()) return MitigationStatus::NOT_MITIGATED;
        auto worst = MitigationStatus::TRANSFERRED; // highest enum value
        for (const auto& m : mitigations) {
            if (static_cast<int32_t>(m.status) < static_cast<int32_t>(worst))
                worst = m.status;
        }
        return worst;
    }
};

// ---------------------------------------------------------------------------
// ThreatModel
// ---------------------------------------------------------------------------

/// A threat model for a specific component or the entire system.
struct ThreatModel {
    std::string model_id;       ///< Unique identifier for this threat model
    std::string component;      ///< Component being modeled (e.g., "crypto", "pme")
    std::string version;        ///< Version of the threat model
    std::string author;         ///< Who created/reviewed the model
    std::string created_at;     ///< ISO 8601 creation timestamp
    std::string reviewed_at;    ///< ISO 8601 last review timestamp
    std::vector<ThreatEntry> threats;
};

// ---------------------------------------------------------------------------
// ThreatModelAnalyzer
// ---------------------------------------------------------------------------

/// Analysis result from validating a threat model.
struct ThreatModelAnalysis {
    int32_t total_threats = 0;
    int32_t critical_count = 0;
    int32_t high_count = 0;
    int32_t medium_count = 0;
    int32_t low_count = 0;
    int32_t mitigated_count = 0;
    int32_t unmitigated_count = 0;
    bool stride_complete = false;   ///< All 6 STRIDE categories covered
    std::vector<StrideCategory> missing_categories;
    double mean_dread_score = 0.0;
    std::string report_json;        ///< Full JSON report
};

/// Validates threat model coverage and produces audit-ready JSON.
class ThreatModelAnalyzer {
public:
    /// Analyze a threat model for completeness and risk posture.
    [[nodiscard]] static expected<ThreatModelAnalysis> analyze(const ThreatModel& model) {
        auto gate = commercial::require_feature("ThreatModelAnalyzer");
        if (!gate) return gate.error();
        ThreatModelAnalysis result;
        result.total_threats = static_cast<int32_t>(model.threats.size());

        // Track STRIDE coverage
        bool covered[6] = {};
        double dread_sum = 0.0;
        int32_t valid_count = 0;

        for (const auto& t : model.threats) {
            // Validate DREAD
            if (!t.dread.valid()) continue;

            ++valid_count;
            int cat = static_cast<int32_t>(t.category);
            if (cat >= 0 && cat < 6) covered[cat] = true;

            auto sev = t.dread.severity();
            switch (sev) {
            case ThreatSeverity::CRITICAL: ++result.critical_count; break;
            case ThreatSeverity::HIGH:     ++result.high_count;     break;
            case ThreatSeverity::MEDIUM:   ++result.medium_count;   break;
            case ThreatSeverity::LOW:      ++result.low_count;      break;
            }

            if (t.overall_status() >= MitigationStatus::MITIGATED)
                ++result.mitigated_count;
            else
                ++result.unmitigated_count;

            dread_sum += t.dread.composite();
        }

        if (valid_count > 0)
            result.mean_dread_score = dread_sum / valid_count;

        // Check STRIDE completeness
        result.stride_complete = true;
        for (int i = 0; i < 6; ++i) {
            if (!covered[i]) {
                result.stride_complete = false;
                result.missing_categories.push_back(
                    static_cast<StrideCategory>(i));
            }
        }

        // Generate JSON report
        result.report_json = generate_json(model, result);
        return result;
    }

    /// Build the Signet Forge default threat model with known threats.
    [[nodiscard]] static ThreatModel signet_default_model() {
        ThreatModel m;
        m.model_id  = "SIGNET-TM-001";
        m.component = "signet::forge";
        m.version   = "1.0.0";
        m.author    = "Signet Security Team";

        // --- SPOOFING ---
        m.threats.push_back(ThreatEntry{
            "T-AUTH-001", "Key impersonation via INTERNAL mode",
            "An attacker with access to plaintext keys in INTERNAL mode can "
            "impersonate any column encryption identity.",
            StrideCategory::SPOOFING,
            DreadScore{7, 8, 5, 6, 4},
            "Access to unencrypted key material in file metadata",
            "crypto/pme.hpp",
            {Mitigation{"CTRL-KMS-001",
                        "EXTERNAL key mode with KMS integration",
                        "crypto/key_metadata.hpp:IKmsClient",
                        MitigationStatus::MITIGATED},
             Mitigation{"CTRL-GATE-001",
                        "Production gate rejects INTERNAL mode (C-15)",
                        "crypto/pme.hpp:production_key_mode_gate()",
                        MitigationStatus::MITIGATED}},
            {"NIST SP 800-57", "PARQUET-1178"}
        });

        // --- TAMPERING ---
        m.threats.push_back(ThreatEntry{
            "T-TAMP-001", "Hash chain manipulation in audit logs",
            "An attacker modifies audit chain entries without detection.",
            StrideCategory::TAMPERING,
            DreadScore{9, 3, 4, 8, 3},
            "Direct modification of Parquet audit log files",
            "ai/audit_chain.hpp",
            {Mitigation{"CTRL-CHAIN-001",
                        "SHA-256 cryptographic hash chain with prev_hash linkage",
                        "ai/audit_chain.hpp:AuditChainHasher",
                        MitigationStatus::MITIGATED}},
            {"SEC 17a-4", "NIST SP 800-92"}
        });

        // --- REPUDIATION ---
        m.threats.push_back(ThreatEntry{
            "T-REP-001", "Denial of AI decision actions",
            "An operator denies having made or approved an AI trading decision.",
            StrideCategory::REPUDIATION,
            DreadScore{6, 7, 3, 5, 4},
            "Lack of non-repudiable logging for human overrides",
            "ai/decision_log.hpp",
            {Mitigation{"CTRL-LOG-001",
                        "Immutable decision log with operator_id and hash chain",
                        "ai/decision_log.hpp:DecisionLogWriter",
                        MitigationStatus::MITIGATED},
             Mitigation{"CTRL-OVER-001",
                        "Human override log with provenance (EU AI Act Art.14)",
                        "ai/human_oversight.hpp:HumanOverrideLogWriter",
                        MitigationStatus::MITIGATED}},
            {"EU AI Act Art.14", "MiFID II RTS 24"}
        });

        // --- INFORMATION DISCLOSURE ---
        m.threats.push_back(ThreatEntry{
            "T-DISC-001", "Side-channel leakage from AES timing",
            "An attacker observes timing variations in AES operations to "
            "recover key material.",
            StrideCategory::INFORMATION_DISCLOSURE,
            DreadScore{10, 4, 7, 3, 5},
            "Timing analysis of AES encrypt/decrypt operations",
            "crypto/aes_core.hpp",
            {Mitigation{"CTRL-CT-001",
                        "Constant-time AES via bitsliced S-box + AES-NI detection",
                        "crypto/aes_core.hpp:Aes256",
                        MitigationStatus::MITIGATED},
             Mitigation{"CTRL-ZERO-001",
                        "Key material zeroing in destructors",
                        "crypto/aes_core.hpp:~Aes256()",
                        MitigationStatus::MITIGATED}},
            {"NIST SP 800-38D", "CWE-208"}
        });

        // --- DENIAL OF SERVICE ---
        m.threats.push_back(ThreatEntry{
            "T-DOS-001", "Decompression bomb via crafted Parquet pages",
            "A malicious Parquet file with extreme compression ratios causes "
            "memory exhaustion during decompression.",
            StrideCategory::DENIAL_OF_SERVICE,
            DreadScore{7, 9, 8, 5, 7},
            "Crafted Parquet file with oversized uncompressed pages",
            "reader.hpp",
            {Mitigation{"CTRL-PAGE-001",
                        "PARQUET_MAX_PAGE_SIZE (256 MB) limit on decompressed pages",
                        "reader.hpp:PARQUET_MAX_PAGE_SIZE",
                        MitigationStatus::MITIGATED},
             Mitigation{"CTRL-THRIFT-001",
                        "Thrift field count (65536) and string size (64 MB) limits",
                        "thrift/compact.hpp:MAX_FIELD_COUNT",
                        MitigationStatus::MITIGATED}},
            {"CWE-409", "OWASP Decompression Bomb"}
        });

        // --- ELEVATION OF PRIVILEGE ---
        m.threats.push_back(ThreatEntry{
            "T-PRIV-001", "Path traversal in FeatureWriter output_dir",
            "An attacker supplies a path with '..' segments to write outside "
            "the intended directory.",
            StrideCategory::ELEVATION_OF_PRIVILEGE,
            DreadScore{8, 9, 7, 4, 8},
            "Controlled output_dir parameter with path traversal sequences",
            "ai/feature_writer.hpp",
            {Mitigation{"CTRL-PATH-001",
                        "Path traversal guard rejects '..' segments",
                        "ai/feature_writer.hpp:create()",
                        MitigationStatus::MITIGATED}},
            {"CWE-22", "OWASP Path Traversal"}
        });

        return m;
    }

private:
    static std::string stride_name(StrideCategory c) {
        switch (c) {
        case StrideCategory::SPOOFING:               return "Spoofing";
        case StrideCategory::TAMPERING:              return "Tampering";
        case StrideCategory::REPUDIATION:            return "Repudiation";
        case StrideCategory::INFORMATION_DISCLOSURE: return "Information Disclosure";
        case StrideCategory::DENIAL_OF_SERVICE:      return "Denial of Service";
        case StrideCategory::ELEVATION_OF_PRIVILEGE: return "Elevation of Privilege";
        }
        return "Unknown";
    }

    static std::string severity_name(ThreatSeverity s) {
        switch (s) {
        case ThreatSeverity::LOW:      return "Low";
        case ThreatSeverity::MEDIUM:   return "Medium";
        case ThreatSeverity::HIGH:     return "High";
        case ThreatSeverity::CRITICAL: return "Critical";
        }
        return "Unknown";
    }

    static std::string mitigation_status_name(MitigationStatus s) {
        switch (s) {
        case MitigationStatus::NOT_MITIGATED: return "Not Mitigated";
        case MitigationStatus::PARTIAL:       return "Partial";
        case MitigationStatus::MITIGATED:     return "Mitigated";
        case MitigationStatus::ACCEPTED:      return "Accepted";
        case MitigationStatus::TRANSFERRED:   return "Transferred";
        }
        return "Unknown";
    }

    static std::string escape_json(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 16);
        for (char c : s) {
            switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
            }
        }
        return out;
    }

    static std::string generate_json(const ThreatModel& model,
                                     const ThreatModelAnalysis& analysis) {
        std::ostringstream o;
        o << "{\n";
        o << "  \"model_id\": \"" << escape_json(model.model_id) << "\",\n";
        o << "  \"component\": \"" << escape_json(model.component) << "\",\n";
        o << "  \"version\": \"" << escape_json(model.version) << "\",\n";
        o << "  \"methodology\": \"STRIDE/DREAD\",\n";
        o << "  \"summary\": {\n";
        o << "    \"total_threats\": " << analysis.total_threats << ",\n";
        o << "    \"critical\": " << analysis.critical_count << ",\n";
        o << "    \"high\": " << analysis.high_count << ",\n";
        o << "    \"medium\": " << analysis.medium_count << ",\n";
        o << "    \"low\": " << analysis.low_count << ",\n";
        o << "    \"mitigated\": " << analysis.mitigated_count << ",\n";
        o << "    \"unmitigated\": " << analysis.unmitigated_count << ",\n";
        o << "    \"stride_complete\": " << (analysis.stride_complete ? "true" : "false") << ",\n";
        o << "    \"mean_dread_score\": " << analysis.mean_dread_score << "\n";
        o << "  },\n";
        o << "  \"threats\": [\n";

        for (size_t i = 0; i < model.threats.size(); ++i) {
            const auto& t = model.threats[i];
            o << "    {\n";
            o << "      \"id\": \"" << escape_json(t.threat_id) << "\",\n";
            o << "      \"title\": \"" << escape_json(t.title) << "\",\n";
            o << "      \"stride\": \"" << stride_name(t.category) << "\",\n";
            o << "      \"severity\": \"" << severity_name(t.dread.severity()) << "\",\n";
            o << "      \"dread\": {\n";
            o << "        \"damage\": " << t.dread.damage << ",\n";
            o << "        \"reproducibility\": " << t.dread.reproducibility << ",\n";
            o << "        \"exploitability\": " << t.dread.exploitability << ",\n";
            o << "        \"affected_users\": " << t.dread.affected_users << ",\n";
            o << "        \"discoverability\": " << t.dread.discoverability << ",\n";
            o << "        \"composite\": " << t.dread.composite() << "\n";
            o << "      },\n";
            o << "      \"status\": \"" << mitigation_status_name(t.overall_status()) << "\",\n";
            o << "      \"mitigations\": [";
            for (size_t j = 0; j < t.mitigations.size(); ++j) {
                const auto& m = t.mitigations[j];
                o << "\n        {\"id\": \"" << escape_json(m.control_id)
                  << "\", \"status\": \"" << mitigation_status_name(m.status) << "\"}";
                if (j + 1 < t.mitigations.size()) o << ",";
            }
            o << (t.mitigations.empty() ? "" : "\n      ") << "]\n";
            o << "    }";
            if (i + 1 < model.threats.size()) o << ",";
            o << "\n";
        }

        o << "  ]\n";
        o << "}";
        return o.str();
    }
};

} // namespace signet::forge
