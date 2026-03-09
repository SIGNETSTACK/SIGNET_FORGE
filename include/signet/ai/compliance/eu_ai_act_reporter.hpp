// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
// eu_ai_act_reporter.hpp — EU AI Act Compliance Reporter
// Phase 10d: Compliance Report Generators
//
// Generates compliance reports for EU Regulation 2024/1689 (EU AI Act):
//
//   Article 12 — Record-keeping / operational logging
//     Requires automatic logging of events during operation of high-risk AI
//     systems at a level of traceability appropriate to the system's purpose.
//     For financial AI: every inference with input reference, output, confidence,
//     model version, and timestamp to nanosecond precision.
//
//   Article 13 — Transparency and provision of information to deployers
//     Requires a transparency disclosure covering: system capabilities,
//     limitations, accuracy metrics, training data characteristics, and
//     intended purpose. Generated as a machine-readable JSON summary.
//
//   Article 19 — Conformity assessment (simplified technical summary)
//     A summary document demonstrating: chain-of-custody integrity,
//     aggregate performance statistics, anomaly counts, and coverage period.
//     For internal QA and external audit support.
//
// Reads from InferenceLogWriter / DecisionLogWriter output files.
//
// Usage:
//   ReportOptions opts;
//   opts.system_id = "trading-ai-v2";
//   opts.start_ns  = period_start;
//   opts.end_ns    = period_end;
//
//   auto art12 = EUAIActReporter::generate_article12({"inf_0.parquet"}, opts);
//   auto art13 = EUAIActReporter::generate_article13({"inf_0.parquet"}, opts);
//   auto art19 = EUAIActReporter::generate_article19(dec_files, inf_files, opts);

#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/compliance/eu_ai_act_reporter.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

#include "signet/error.hpp"
#include "signet/ai/decision_log.hpp"
#include "signet/ai/inference_log.hpp"
#include "signet/ai/compliance/compliance_types.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <numeric>
#include <string>
#include <vector>

namespace signet::forge {

/// EU AI Act compliance report generator (Regulation (EU) 2024/1689).
///
/// Generates compliance reports for three articles:
///   - Article 12: Operational logging of high-risk AI system events
///   - Article 13: Transparency disclosure (capabilities, limitations, metrics)
///   - Article 19: Conformity assessment summary (cross-referencing decisions + inferences)
///
/// Reads from InferenceLogWriter and/or DecisionLogWriter output files.
///
/// @see ComplianceStandard::EU_AI_ACT_ART12
/// @see ComplianceStandard::EU_AI_ACT_ART13
/// @see ComplianceStandard::EU_AI_ACT_ART19
class EUAIActReporter {
public:
    /// Generate an Article 12 operational logging report from inference log files.
    ///
    /// Output includes per-inference records with: ISO 8601 timestamp (ns precision),
    /// model ID + version, inference type, input/output hashes, output score,
    /// latency, anonymized user reference, and chain verification status.
    /// Raw embeddings are omitted unless opts.include_features is true.
    ///
    /// @param inference_log_files  Paths to InferenceLog Parquet files.
    /// @param opts                 Report options (time window, format, verification).
    /// @return The generated ComplianceReport, or an error.
    [[nodiscard]] static expected<ComplianceReport> generate_article12(
            const std::vector<std::string>& inference_log_files,
            const ReportOptions& opts = {}) {

        auto license = commercial::require_feature("EUAIActReporter::article12");
        if (!license) return license.error();

        if (inference_log_files.empty())
            return Error{ErrorCode::IO_ERROR,
                         "EUAIActReporter: no inference log files supplied"};

        std::vector<InferenceRecord> records;
        bool        chain_ok = true;
        std::string chain_id;

        for (const auto& path : inference_log_files) {
            auto rdr_result = InferenceLogReader::open(path);
            if (!rdr_result) return Error{ErrorCode::IO_ERROR,
                "EUAIActReporter: cannot open log file '" + path +
                "': " + rdr_result.error().message};
            auto& rdr = *rdr_result;

            if (opts.verify_chain) {
                auto vr = rdr.verify_chain();
                if (!vr.valid) chain_ok = false;
            }
            if (chain_id.empty()) {
                auto meta = rdr.audit_metadata();
                if (meta) chain_id = meta->chain_id;
            }
            auto all = rdr.read_all();
            if (!all) continue;
            for (auto& rec : *all)
                if (rec.timestamp_ns >= opts.start_ns &&
                    rec.timestamp_ns <= opts.end_ns)
                    records.push_back(std::move(rec));
        }

        std::sort(records.begin(), records.end(),
            [](const InferenceRecord& a, const InferenceRecord& b){
                return a.timestamp_ns < b.timestamp_ns; });

        auto usage = commercial::record_usage_rows(
            "EUAIActReporter::article12", static_cast<uint64_t>(records.size()));
        if (!usage) return usage.error();

        ComplianceReport report = make_report_skeleton(
            ComplianceStandard::EU_AI_ACT_ART12, opts,
            static_cast<int64_t>(records.size()), chain_ok, chain_id);

        report.content = format_article12_json(records, opts, report);
        return report;
    }

    /// Generate an Article 13 transparency disclosure from inference log files.
    ///
    /// Produces a machine-readable disclosure covering: system identifier,
    /// model version range, inference type distribution, performance
    /// characteristics (latency percentiles, confidence distribution),
    /// data volume statistics (tokens, batch sizes), and operational coverage.
    ///
    /// @param inference_log_files  Paths to InferenceLog Parquet files.
    /// @param opts                 Report options (time window, format, verification).
    /// @return The generated ComplianceReport, or an error.
    [[nodiscard]] static expected<ComplianceReport> generate_article13(
            const std::vector<std::string>& inference_log_files,
            const ReportOptions& opts = {}) {

        auto license = commercial::require_feature("EUAIActReporter::article13");
        if (!license) return license.error();

        if (inference_log_files.empty())
            return Error{ErrorCode::IO_ERROR,
                         "EUAIActReporter: no inference log files supplied"};

        std::vector<InferenceRecord> records;
        bool        chain_ok = true;
        std::string chain_id;

        for (const auto& path : inference_log_files) {
            auto rdr_result = InferenceLogReader::open(path);
            if (!rdr_result) return Error{ErrorCode::IO_ERROR,
                "EUAIActReporter: cannot open log file '" + path +
                "': " + rdr_result.error().message};
            auto& rdr = *rdr_result;
            if (opts.verify_chain) {
                auto vr = rdr.verify_chain();
                if (!vr.valid) chain_ok = false;
            }
            if (chain_id.empty()) {
                auto meta = rdr.audit_metadata();
                if (meta) chain_id = meta->chain_id;
            }
            auto all = rdr.read_all();
            if (!all) continue;
            for (auto& rec : *all)
                if (rec.timestamp_ns >= opts.start_ns &&
                    rec.timestamp_ns <= opts.end_ns)
                    records.push_back(std::move(rec));
        }

        auto usage = commercial::record_usage_rows(
            "EUAIActReporter::article13", static_cast<uint64_t>(records.size()));
        if (!usage) return usage.error();

        ComplianceReport report = make_report_skeleton(
            ComplianceStandard::EU_AI_ACT_ART13, opts,
            static_cast<int64_t>(records.size()), chain_ok, chain_id);

        report.content = format_article13_json(records, opts, report);
        return report;
    }

    /// Generate an Article 19 conformity assessment summary.
    ///
    /// Cross-references DecisionLog (trading decisions) and InferenceLog
    /// (model inferences) to produce a unified audit summary showing:
    /// chain integrity across both log types, decision volume and risk gate
    /// outcome distribution, inference volume and anomaly flags, model
    /// version coverage, and aggregate performance metrics.
    ///
    /// @param decision_log_files   Paths to DecisionLog Parquet files.
    /// @param inference_log_files  Paths to InferenceLog Parquet files.
    /// @param opts                 Report options (time window, format, verification).
    /// @return The generated ComplianceReport, or an error.
    [[nodiscard]] static expected<ComplianceReport> generate_article19(
            const std::vector<std::string>& decision_log_files,
            const std::vector<std::string>& inference_log_files,
            const ReportOptions& opts = {}) {

        auto license = commercial::require_feature("EUAIActReporter::article19");
        if (!license) return license.error();

        if (decision_log_files.empty() && inference_log_files.empty())
            return Error{ErrorCode::IO_ERROR,
                         "EUAIActReporter: no log files supplied"};

        // -- Decision records -------------------------------------------------
        std::vector<DecisionRecord> dec_records;
        bool dec_chain_ok = true;
        std::string dec_chain_id;

        for (const auto& path : decision_log_files) {
            auto rdr_result = DecisionLogReader::open(path);
            if (!rdr_result) return Error{ErrorCode::IO_ERROR,
                "EUAIActReporter: cannot open decision log '" + path +
                "': " + rdr_result.error().message};
            auto& rdr = *rdr_result;
            if (opts.verify_chain) {
                auto vr = rdr.verify_chain();
                if (!vr.valid) dec_chain_ok = false;
            }
            if (dec_chain_id.empty()) {
                auto meta = rdr.audit_metadata();
                if (meta) dec_chain_id = meta->chain_id;
            }
            auto all = rdr.read_all();
            if (!all) continue;
            for (auto& rec : *all)
                if (rec.timestamp_ns >= opts.start_ns &&
                    rec.timestamp_ns <= opts.end_ns)
                    dec_records.push_back(std::move(rec));
        }

        // -- Inference records ------------------------------------------------
        std::vector<InferenceRecord> inf_records;
        bool inf_chain_ok = true;
        std::string inf_chain_id;

        for (const auto& path : inference_log_files) {
            auto rdr_result = InferenceLogReader::open(path);
            if (!rdr_result) return Error{ErrorCode::IO_ERROR,
                "EUAIActReporter: cannot open inference log '" + path +
                "': " + rdr_result.error().message};
            auto& rdr = *rdr_result;
            if (opts.verify_chain) {
                auto vr = rdr.verify_chain();
                if (!vr.valid) inf_chain_ok = false;
            }
            if (inf_chain_id.empty()) {
                auto meta = rdr.audit_metadata();
                if (meta) inf_chain_id = meta->chain_id;
            }
            auto all = rdr.read_all();
            if (!all) continue;
            for (auto& rec : *all)
                if (rec.timestamp_ns >= opts.start_ns &&
                    rec.timestamp_ns <= opts.end_ns)
                    inf_records.push_back(std::move(rec));
        }

        // EU AI Act Art.19: cross-chain verification — when verify_chain is
        // enabled and both log types are present, their chain IDs must match
        // to confirm they belong to the same audit context. A mismatch means
        // the decision and inference logs were produced by unrelated systems,
        // which is an audit finding that must be surfaced.
        if (opts.verify_chain &&
            !dec_chain_id.empty() && !inf_chain_id.empty() &&
            dec_chain_id != inf_chain_id) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "EU AI Act Art.19: decision chain_id ('" + dec_chain_id +
                         "') != inference chain_id ('" + inf_chain_id +
                         "'). Logs must share an audit context."};
        }

        const bool chain_ok = dec_chain_ok && inf_chain_ok;
        const int64_t total = static_cast<int64_t>(
            dec_records.size() + inf_records.size());

        auto usage = commercial::record_usage_rows(
            "EUAIActReporter::article19", static_cast<uint64_t>(total));
        if (!usage) return usage.error();

        ComplianceReport report = make_report_skeleton(
            ComplianceStandard::EU_AI_ACT_ART19, opts, total, chain_ok,
            dec_chain_id.empty() ? inf_chain_id : dec_chain_id);

        report.content = format_article19_json(
            dec_records, inf_records, opts, report,
            dec_chain_ok, inf_chain_ok, dec_chain_id, inf_chain_id);
        return report;
    }

private:
    // =========================================================================
    // Performance stats helper
    // =========================================================================

    struct PerfStats {
        int64_t total          = 0;
        double  avg_latency_ns = 0.0;
        double  p50_latency_ns = 0.0;
        double  p95_latency_ns = 0.0;
        double  p99_latency_ns = 0.0;
        double  avg_confidence = 0.0;
        int64_t low_conf_count = 0;
        int64_t anomaly_count  = 0;  // latency > 3σ above mean
        int64_t total_input_tokens  = 0;
        int64_t total_output_tokens = 0;
        int64_t total_batches       = 0;
    };

    static PerfStats compute_perf(const std::vector<InferenceRecord>& recs,
                                   float low_conf_thr) {
        PerfStats s;
        s.total = static_cast<int64_t>(recs.size());
        if (recs.empty()) return s;

        std::vector<int64_t> latencies;
        latencies.reserve(recs.size());
        double sum_lat = 0.0, sum_conf = 0.0;

        for (const auto& r : recs) {
            latencies.push_back(r.latency_ns);
            sum_lat  += static_cast<double>(r.latency_ns);
            sum_conf += static_cast<double>(r.output_score);
            if (r.output_score < low_conf_thr) ++s.low_conf_count;
            s.total_input_tokens  += r.input_tokens;
            s.total_output_tokens += r.output_tokens;
            s.total_batches       += r.batch_size;
        }

        s.avg_latency_ns = sum_lat / static_cast<double>(recs.size());
        s.avg_confidence = sum_conf / static_cast<double>(recs.size());

        std::sort(latencies.begin(), latencies.end());
        auto pct = [&](double p) -> double {
            size_t idx = static_cast<size_t>(p * static_cast<double>(latencies.size() - 1));
            return static_cast<double>(latencies[idx]);
        };
        s.p50_latency_ns = pct(0.50);
        s.p95_latency_ns = pct(0.95);
        s.p99_latency_ns = pct(0.99);

        // Anomaly: latency > mean + 3σ
        double var = 0.0;
        for (int64_t lat : latencies) {
            double diff = static_cast<double>(lat) - s.avg_latency_ns;
            var += diff * diff;
        }
        var /= static_cast<double>(latencies.size());
        const double sigma3 = s.avg_latency_ns + 3.0 * std::sqrt(var);
        for (int64_t lat : latencies)
            if (static_cast<double>(lat) > sigma3) ++s.anomaly_count;

        return s;
    }

    // =========================================================================
    // JSON formatters
    // =========================================================================

    static std::string format_article12_json(
            const std::vector<InferenceRecord>& records,
            const ReportOptions& opts,
            const ComplianceReport& meta) {

        const std::string ind  = opts.pretty_print ? "  "   : "";
        const std::string ind2 = opts.pretty_print ? "    " : "";
        const std::string nl   = opts.pretty_print ? "\n"   : "";
        const std::string sp   = opts.pretty_print ? " "    : "";

        PerfStats ps = compute_perf(records, opts.low_confidence_threshold);

        std::string o;
        o += "{" + nl;
        o += ind + "\"report_type\":" + sp + "\"EU_AI_ACT_ARTICLE_12\"," + nl;
        o += ind + "\"regulatory_reference\":" + sp
           + "\"Regulation (EU) 2024/1689 — Article 12\"," + nl;
        o += ind + "\"report_id\":" + sp + "\"" + j(meta.report_id) + "\"," + nl;
        o += ind + "\"system_id\":" + sp
           + "\"" + j(opts.system_id.empty() ? "UNSPECIFIED" : opts.system_id)
           + "\"," + nl;
        o += ind + "\"generated_at\":" + sp + "\"" + meta.generated_at_iso + "\"," + nl;
        o += ind + "\"period_start\":" + sp + "\"" + meta.period_start_iso + "\"," + nl;
        o += ind + "\"period_end\":" + sp + "\"" + meta.period_end_iso + "\"," + nl;
        o += ind + "\"chain_id\":" + sp + "\"" + j(meta.chain_id) + "\"," + nl;
        o += ind + "\"chain_verified\":" + sp + (meta.chain_verified ? "true" : "false") + "," + nl;
        o += ind + "\"total_inferences\":" + sp + std::to_string(ps.total) + "," + nl;
        o += ind + "\"anomaly_count\":" + sp + std::to_string(ps.anomaly_count) + "," + nl;
        o += ind + "\"low_confidence_count\":" + sp + std::to_string(ps.low_conf_count) + "," + nl;
        o += ind + "\"performance_summary\":" + sp + "{" + nl;
        o += ind2 + "\"avg_latency_ns\":" + sp + dbl(ps.avg_latency_ns) + "," + nl;
        o += ind2 + "\"p50_latency_ns\":" + sp + dbl(ps.p50_latency_ns) + "," + nl;
        o += ind2 + "\"p95_latency_ns\":" + sp + dbl(ps.p95_latency_ns) + "," + nl;
        o += ind2 + "\"p99_latency_ns\":" + sp + dbl(ps.p99_latency_ns) + "," + nl;
        o += ind2 + "\"avg_output_score\":" + sp + dbl(ps.avg_confidence) + nl;
        o += ind + "}," + nl;
        o += ind + "\"inference_records\":" + sp + "[" + nl;

        // EU AI Act Art.12(2): consistent statistical anomaly detection methodology.
        // Precompute mean + 3*sigma threshold for per-record anomaly flag — the
        // same formula used in compute_perf() for aggregate anomaly_count, ensuring
        // per-record and summary anomaly classifications are always consistent.
        double per_record_sigma3 = 0.0;
        if (!records.empty()) {
            double var = 0.0;
            for (const auto& r : records) {
                double diff = static_cast<double>(r.latency_ns) - ps.avg_latency_ns;
                var += diff * diff;
            }
            var /= static_cast<double>(records.size());
            per_record_sigma3 = ps.avg_latency_ns + 3.0 * std::sqrt(var);
        }

        for (size_t i = 0; i < records.size(); ++i) {
            const auto& rec = records[i];
            o += ind2 + "{" + nl;
            o += ind2 + ind + "\"timestamp_utc\":" + sp
               + "\"" + ns_to_iso8601(rec.timestamp_ns) + "\"," + nl;
            o += ind2 + ind + "\"model_id\":" + sp
               + "\"" + j(rec.model_id) + "\"," + nl;
            o += ind2 + ind + "\"model_version\":" + sp
               + "\"" + j(rec.model_version) + "\"," + nl;
            o += ind2 + ind + "\"inference_type\":" + sp
               + "\"" + inference_type_str(rec.inference_type) + "\"," + nl;
            o += ind2 + ind + "\"input_hash\":" + sp
               + "\"" + j(rec.input_hash) + "\"," + nl;
            o += ind2 + ind + "\"output_hash\":" + sp
               + "\"" + j(rec.output_hash) + "\"," + nl;
            o += ind2 + ind + "\"output_score\":" + sp
               + dbl(rec.output_score) + "," + nl;
            o += ind2 + ind + "\"latency_ns\":" + sp
               + std::to_string(rec.latency_ns) + "," + nl;
            o += ind2 + ind + "\"batch_size\":" + sp
               + std::to_string(rec.batch_size) + "," + nl;
            o += ind2 + ind + "\"user_id_hash\":" + sp
               + "\"" + j(rec.user_id_hash) + "\"," + nl;
            // L23: per-record anomaly uses mean + 3*stddev (consistent with aggregate)
            // Compute sigma3 threshold (same formula as compute_perf)
            o += ind2 + ind + "\"anomaly\":" + sp
               + (rec.latency_ns > static_cast<int64_t>(per_record_sigma3)
                  ? "true" : "false");
            if (opts.include_features && !rec.input_embedding.empty()) {
                o += "," + nl;
                o += ind2 + ind + "\"input_embedding_preview\":" + sp
                   + feats_preview(rec.input_embedding, 8);
            }
            o += nl + ind2 + "}";
            if (i + 1 < records.size()) o += ",";
            o += nl;
        }
        o += ind + "]" + nl;
        o += "}" + nl;
        return o;
    }

    static std::string format_article13_json(
            const std::vector<InferenceRecord>& records,
            const ReportOptions& opts,
            const ComplianceReport& meta) {

        const std::string ind  = opts.pretty_print ? "  "   : "";
        const std::string ind2 = opts.pretty_print ? "    " : "";
        const std::string nl   = opts.pretty_print ? "\n"   : "";
        const std::string sp   = opts.pretty_print ? " "    : "";

        PerfStats ps = compute_perf(records, opts.low_confidence_threshold);

        // Count inference types
        std::array<int64_t, 8> type_counts{};
        std::string model_versions_seen;
        std::string last_version;
        for (const auto& r : records) {
            int idx = static_cast<int>(r.inference_type);
            if (idx >= 0 && idx < 8) ++type_counts[idx];
            if (r.model_version != last_version) {
                if (!model_versions_seen.empty()) model_versions_seen += ", ";
                model_versions_seen += r.model_version;
                last_version = r.model_version;
            }
        }

        std::string o;
        o += "{" + nl;
        o += ind + "\"report_type\":" + sp + "\"EU_AI_ACT_ARTICLE_13\"," + nl;
        o += ind + "\"regulatory_reference\":" + sp
           + "\"Regulation (EU) 2024/1689 — Article 13: Transparency\"," + nl;
        o += ind + "\"report_id\":" + sp + "\"" + j(meta.report_id) + "\"," + nl;
        o += ind + "\"system_id\":" + sp
           + "\"" + j(opts.system_id.empty() ? "UNSPECIFIED" : opts.system_id) + "\"," + nl;
        o += ind + "\"generated_at\":" + sp + "\"" + meta.generated_at_iso + "\"," + nl;
        // EU AI Act Art.13(3) transparency disclosure (Gap R-2)
        o += ind + "\"provider\":" + sp + "{" + nl;
        o += ind2 + "\"name\":" + sp + "\""
           + j(opts.provider_name.empty() ? "UNSPECIFIED" : opts.provider_name)
           + "\"," + nl;
        o += ind2 + "\"contact\":" + sp + "\""
           + j(opts.provider_contact.empty() ? "UNSPECIFIED" : opts.provider_contact)
           + "\"" + nl;
        o += ind + "}," + nl;
        o += ind + "\"intended_purpose\":" + sp + "\""
           + j(opts.intended_purpose.empty()
               ? "Not specified — Art.13(3)(b)(i) requires disclosure"
               : opts.intended_purpose) + "\"," + nl;
        o += ind + "\"known_limitations\":" + sp + "\""
           + j(opts.known_limitations.empty()
               ? "Not specified — Art.13(3)(b)(ii) requires disclosure"
               : opts.known_limitations) + "\"," + nl;
        o += ind + "\"instructions_for_use\":" + sp + "\""
           + j(opts.instructions_for_use.empty()
               ? "Not specified — Art.13(3)(b)(iv) requires disclosure"
               : opts.instructions_for_use) + "\"," + nl;
        o += ind + "\"human_oversight_measures\":" + sp + "\""
           + j(opts.human_oversight_measures.empty()
               ? "Not specified — Art.14 requires disclosure"
               : opts.human_oversight_measures) + "\"," + nl;
        if (!opts.accuracy_metrics.empty()) {
            o += ind + "\"accuracy_metrics\":" + sp + "\""
               + j(opts.accuracy_metrics) + "\"," + nl;
        }
        if (!opts.bias_risks.empty()) {
            o += ind + "\"bias_risks\":" + sp + "\""
               + j(opts.bias_risks) + "\"," + nl;
        }
        if (opts.risk_level > 0) {
            o += ind + "\"risk_classification\":" + sp + "{" + nl;
            o += ind2 + "\"level\":" + sp + std::to_string(opts.risk_level) + "," + nl;
            const char* risk_labels[] = {"","minimal","limited","high","unacceptable"};
            o += ind2 + "\"label\":" + sp + "\""
               + std::string(opts.risk_level <= 4 ? risk_labels[opts.risk_level] : "unknown")
               + "\"" + nl;
            o += ind + "}," + nl;
        }
        o += ind + "\"system_capabilities\":" + sp + "{" + nl;
        o += ind2 + "\"supported_inference_types\":" + sp + "[";
        const char* type_names[] = {
            "CLASSIFICATION","REGRESSION","EMBEDDING","GENERATION",
            "RANKING","ANOMALY","RECOMMENDATION","CUSTOM"
        };
        bool first = true;
        for (int i = 0; i < 8; ++i) {
            if (type_counts[i] > 0) {
                if (!first) o += ",";
                o += "\"" + std::string(type_names[i]) + "\"";
                first = false;
            }
        }
        o += "]," + nl;
        o += ind2 + "\"model_versions_observed\":" + sp
           + "\"" + j(model_versions_seen) + "\"," + nl;
        o += ind2 + "\"total_inferences_in_period\":" + sp
           + std::to_string(ps.total) + nl;
        o += ind + "}," + nl;
        o += ind + "\"performance_characteristics\":" + sp + "{" + nl;
        o += ind2 + "\"latency_p50_ns\":" + sp + dbl(ps.p50_latency_ns) + "," + nl;
        o += ind2 + "\"latency_p95_ns\":" + sp + dbl(ps.p95_latency_ns) + "," + nl;
        o += ind2 + "\"latency_p99_ns\":" + sp + dbl(ps.p99_latency_ns) + "," + nl;
        o += ind2 + "\"avg_output_score\":" + sp + dbl(ps.avg_confidence) + "," + nl;
        o += ind2 + "\"low_confidence_rate\":" + sp
           + dbl(ps.total > 0
                 ? static_cast<double>(ps.low_conf_count) / ps.total
                 : 0.0) + nl;
        o += ind + "}," + nl;
        o += ind + "\"data_characteristics\":" + sp + "{" + nl;
        o += ind2 + "\"total_input_tokens\":" + sp
           + std::to_string(ps.total_input_tokens) + "," + nl;
        o += ind2 + "\"total_output_tokens\":" + sp
           + std::to_string(ps.total_output_tokens) + "," + nl;
        o += ind2 + "\"avg_batch_size\":" + sp
           + dbl(ps.total > 0
                 ? static_cast<double>(ps.total_batches) / ps.total
                 : 0.0);
        // EU AI Act Art.13(3)(b)(ii): training data provenance (if available)
        {
            std::string td_id, td_chars;
            int64_t td_size = 0;
            for (const auto& r : records) {
                if (td_id.empty() && !r.training_dataset_id.empty())
                    td_id = r.training_dataset_id;
                if (r.training_dataset_size > td_size)
                    td_size = r.training_dataset_size;
                if (td_chars.empty() && !r.training_data_characteristics.empty())
                    td_chars = r.training_data_characteristics;
            }
            if (!td_id.empty() || td_size > 0 || !td_chars.empty()) {
                o += "," + nl;
                if (!td_id.empty())
                    o += ind2 + "\"training_dataset_id\":" + sp
                       + "\"" + j(td_id) + "\"," + nl;
                o += ind2 + "\"training_dataset_size\":" + sp
                   + std::to_string(td_size);
                if (!td_chars.empty()) {
                    o += "," + nl;
                    o += ind2 + "\"training_data_characteristics\":" + sp
                       + "\"" + j(td_chars) + "\"";
                }
            }
        }
        o += nl;
        o += ind + "}," + nl;
        o += ind + "\"limitations_and_risks\":" + sp + "{" + nl;
        o += ind2 + "\"anomaly_count\":" + sp + std::to_string(ps.anomaly_count) + "," + nl;
        o += ind2 + "\"chain_integrity\":" + sp
           + (meta.chain_verified ? "\"VERIFIED\"" : "\"FAILED\"") + nl;
        o += ind + "}" + nl;
        o += "}" + nl;
        return o;
    }

    static std::string format_article19_json(
            const std::vector<DecisionRecord>& dec_recs,
            const std::vector<InferenceRecord>& inf_recs,
            const ReportOptions& opts,
            const ComplianceReport& meta,
            bool dec_chain_ok, bool inf_chain_ok,
            const std::string& dec_chain_id,
            const std::string& inf_chain_id) {

        const std::string ind  = opts.pretty_print ? "  "   : "";
        const std::string ind2 = opts.pretty_print ? "    " : "";
        const std::string nl   = opts.pretty_print ? "\n"   : "";
        const std::string sp   = opts.pretty_print ? " "    : "";

        PerfStats ps = compute_perf(inf_recs, opts.low_confidence_threshold);

        // Decision stats
        int64_t orders_new = 0, orders_rejected = 0, risk_overrides = 0;
        for (const auto& r : dec_recs) {
            if (r.decision_type == DecisionType::ORDER_NEW)    ++orders_new;
            if (r.risk_result == RiskGateResult::REJECTED)     ++orders_rejected;
            if (r.decision_type == DecisionType::RISK_OVERRIDE)++risk_overrides;
        }

        std::string o;
        o += "{" + nl;
        o += ind + "\"report_type\":" + sp + "\"EU_AI_ACT_ARTICLE_19\"," + nl;
        o += ind + "\"regulatory_reference\":" + sp
           + "\"Regulation (EU) 2024/1689 — Article 19: Conformity Assessment\"," + nl;
        o += ind + "\"report_id\":" + sp + "\"" + j(meta.report_id) + "\"," + nl;
        o += ind + "\"system_id\":" + sp
           + "\"" + j(opts.system_id.empty() ? "UNSPECIFIED" : opts.system_id) + "\"," + nl;
        o += ind + "\"generated_at\":" + sp + "\"" + meta.generated_at_iso + "\"," + nl;
        o += ind + "\"period_start\":" + sp + "\"" + meta.period_start_iso + "\"," + nl;
        o += ind + "\"period_end\":" + sp + "\"" + meta.period_end_iso + "\"," + nl;
        o += ind + "\"conformity_status\":" + sp
           + (dec_chain_ok && inf_chain_ok ? "\"CONFORMANT\"" : "\"NON_CONFORMANT\"")
           + "," + nl;
        o += ind + "\"audit_trail_integrity\":" + sp + "{" + nl;
        o += ind2 + "\"decision_chain_id\":" + sp + "\"" + j(dec_chain_id) + "\"," + nl;
        o += ind2 + "\"decision_chain_verified\":" + sp
           + (dec_chain_ok ? "true" : "false") + "," + nl;
        o += ind2 + "\"inference_chain_id\":" + sp + "\"" + j(inf_chain_id) + "\"," + nl;
        o += ind2 + "\"inference_chain_verified\":" + sp
           + (inf_chain_ok ? "true" : "false") + nl;
        o += ind + "}," + nl;
        o += ind + "\"decision_statistics\":" + sp + "{" + nl;
        o += ind2 + "\"total_decisions\":" + sp
           + std::to_string(dec_recs.size()) + "," + nl;
        o += ind2 + "\"orders_generated\":" + sp + std::to_string(orders_new) + "," + nl;
        o += ind2 + "\"orders_rejected_by_risk_gate\":" + sp
           + std::to_string(orders_rejected) + "," + nl;
        o += ind2 + "\"risk_overrides\":" + sp + std::to_string(risk_overrides) + nl;
        o += ind + "}," + nl;
        o += ind + "\"inference_statistics\":" + sp + "{" + nl;
        o += ind2 + "\"total_inferences\":" + sp + std::to_string(ps.total) + "," + nl;
        o += ind2 + "\"avg_latency_ns\":" + sp + dbl(ps.avg_latency_ns) + "," + nl;
        o += ind2 + "\"p99_latency_ns\":" + sp + dbl(ps.p99_latency_ns) + "," + nl;
        o += ind2 + "\"anomaly_count\":" + sp + std::to_string(ps.anomaly_count) + "," + nl;
        o += ind2 + "\"low_confidence_count\":" + sp
           + std::to_string(ps.low_conf_count) + nl;
        o += ind + "}" + nl;
        o += "}" + nl;
        return o;
    }

    // =========================================================================
    // Shared utilities
    // =========================================================================

    static ComplianceReport make_report_skeleton(
            ComplianceStandard std_,
            const ReportOptions& opts,
            int64_t total_records,
            bool chain_ok,
            const std::string& chain_id) {

        ComplianceReport r;
        r.standard       = std_;
        r.format         = opts.format;
        r.chain_verified = chain_ok;
        r.chain_id       = chain_id;
        r.total_records  = total_records;

        using namespace std::chrono;
        r.generated_at_ns  = static_cast<int64_t>(
            duration_cast<nanoseconds>(
                system_clock::now().time_since_epoch()).count());
        r.generated_at_iso = ns_to_iso8601(r.generated_at_ns);
        r.period_start_iso = ns_to_iso8601(opts.start_ns);
        r.period_end_iso   = (opts.end_ns == (std::numeric_limits<int64_t>::max)())
                                 ? "open"
                                 : ns_to_iso8601(opts.end_ns);
        r.report_id = opts.report_id.empty()
                          ? ("EUAIA-" + std::to_string(r.generated_at_ns))
                          : opts.report_id;
        return r;
    }

    static std::string ns_to_iso8601(int64_t ns) {
        static_assert(sizeof(std::time_t) >= 8,
            "Signet compliance reporters require 64-bit time_t for timestamps beyond 2038");
        if (ns <= 0) return "1970-01-01T00:00:00.000000000Z";
        const int64_t secs    = ns / 1'000'000'000LL;
        const int64_t ns_part = ns % 1'000'000'000LL;
        std::time_t t = static_cast<std::time_t>(secs);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
        std::tm* utc = &tm_buf;
#else
        std::tm* utc = gmtime_r(&t, &tm_buf);
#endif
        char date_buf[32];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%S", utc);
        char full_buf[48];
        std::snprintf(full_buf, sizeof(full_buf), "%s.%09lldZ",
                      date_buf, static_cast<long long>(ns_part));
        return full_buf;
    }

    static std::string inference_type_str(InferenceType t) {
        switch (t) {
            case InferenceType::CLASSIFICATION:  return "CLASSIFICATION";
            case InferenceType::REGRESSION:      return "REGRESSION";
            case InferenceType::EMBEDDING:       return "EMBEDDING";
            case InferenceType::GENERATION:      return "GENERATION";
            case InferenceType::RANKING:         return "RANKING";
            case InferenceType::ANOMALY:         return "ANOMALY";
            case InferenceType::RECOMMENDATION:  return "RECOMMENDATION";
            case InferenceType::CUSTOM:          return "CUSTOM";
        }
        return "UNKNOWN";
    }

    static constexpr size_t MAX_FIELD_LENGTH = 4096;

    static std::string truncate_field(const std::string& s) {
        if (s.size() <= MAX_FIELD_LENGTH) return s;
        return s.substr(0, MAX_FIELD_LENGTH) + "...[TRUNCATED]";
    }

    static std::string j(const std::string& s) {
        const std::string safe = truncate_field(s);
        std::string out;
        out.reserve(safe.size());
        for (unsigned char c : safe) {
            if (c == '"')       out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20) { char buf[8];
                                  std::snprintf(buf,sizeof(buf),"\\u%04x",c);
                                  out += buf; }
            else                out += static_cast<char>(c);
        }
        return out;
    }

    static std::string dbl(double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        return buf;
    }

    static std::string feats_preview(const std::vector<float>& f, size_t max_n) {
        std::string o = "[";
        const size_t n = (std::min)(f.size(), max_n);
        for (size_t i = 0; i < n; ++i) {
            char buf[16]; std::snprintf(buf, sizeof(buf), "%.4g", f[i]);
            o += buf;
            if (i + 1 < n) o += ",";
        }
        if (f.size() > max_n) o += ",...";
        o += "]";
        return o;
    }
};

// ---------------------------------------------------------------------------
// Art.15 Accuracy/Robustness Metrics (Gap R-3b)
// ---------------------------------------------------------------------------

/// Computed accuracy, robustness, and drift metrics per EU AI Act Art.15.
///
/// Art.15(1): Accuracy metrics appropriate to the system's intended purpose.
/// Art.15(3): Resilience against errors, faults, and inconsistencies.
/// Art.15(4): Cybersecurity posture (addressed at infrastructure level).
///
/// Defined at namespace scope for Apple Clang compatibility.
struct Art15Metrics {
    // --- Accuracy / Confidence ---
    int64_t total_inferences = 0;
    int64_t low_confidence_count = 0;       ///< Inferences below low_confidence_threshold
    float   low_confidence_rate = 0.0f;     ///< low_confidence_count / total_inferences
    float   mean_confidence = 0.0f;         ///< Mean output_score across all inferences
    float   median_confidence = 0.0f;       ///< Median output_score
    float   std_dev_confidence = 0.0f;      ///< Standard deviation of output_score
    float   min_confidence = 1.0f;          ///< Minimum output_score observed
    float   max_confidence = 0.0f;          ///< Maximum output_score observed

    // --- Latency / Robustness ---
    int64_t mean_latency_ns = 0;            ///< Mean inference latency
    int64_t p50_latency_ns = 0;             ///< Median (p50) latency
    int64_t p95_latency_ns = 0;             ///< 95th percentile latency
    int64_t p99_latency_ns = 0;             ///< 99th percentile latency
    int64_t max_latency_ns = 0;             ///< Maximum latency

    // --- Drift Detection ---
    int64_t distinct_model_versions = 0;    ///< Number of distinct model versions seen
    float   psi_score = 0.0f;              ///< Population Stability Index (0 = no drift)
    ///< PSI < 0.1: no significant change
    ///< PSI 0.1–0.25: moderate shift
    ///< PSI > 0.25: significant distribution shift

    // --- Coverage ---
    int64_t period_start_ns = 0;
    int64_t period_end_ns = 0;
    int64_t period_duration_ns = 0;

    /// Serialize metrics to a JSON string.
    [[nodiscard]] std::string to_json(bool pretty = true) const {
        const char* nl = pretty ? "\n" : "";
        const char* sp = pretty ? " " : "";
        const std::string ind = pretty ? "  " : "";

        std::string o = "{" + std::string(nl);
        char buf[64];

        o += ind + "\"total_inferences\":" + sp + std::to_string(total_inferences) + "," + nl;
        o += ind + "\"low_confidence_count\":" + sp + std::to_string(low_confidence_count) + "," + nl;
        std::snprintf(buf, sizeof(buf), "%.6f", low_confidence_rate);
        o += ind + "\"low_confidence_rate\":" + sp + buf + "," + std::string(nl);
        std::snprintf(buf, sizeof(buf), "%.6f", mean_confidence);
        o += ind + "\"mean_confidence\":" + sp + buf + "," + std::string(nl);
        std::snprintf(buf, sizeof(buf), "%.6f", median_confidence);
        o += ind + "\"median_confidence\":" + sp + buf + "," + std::string(nl);
        std::snprintf(buf, sizeof(buf), "%.6f", std_dev_confidence);
        o += ind + "\"std_dev_confidence\":" + sp + buf + "," + std::string(nl);
        std::snprintf(buf, sizeof(buf), "%.6f", min_confidence);
        o += ind + "\"min_confidence\":" + sp + buf + "," + std::string(nl);
        std::snprintf(buf, sizeof(buf), "%.6f", max_confidence);
        o += ind + "\"max_confidence\":" + sp + buf + "," + std::string(nl);
        o += ind + "\"mean_latency_ns\":" + sp + std::to_string(mean_latency_ns) + "," + nl;
        o += ind + "\"p50_latency_ns\":" + sp + std::to_string(p50_latency_ns) + "," + nl;
        o += ind + "\"p95_latency_ns\":" + sp + std::to_string(p95_latency_ns) + "," + nl;
        o += ind + "\"p99_latency_ns\":" + sp + std::to_string(p99_latency_ns) + "," + nl;
        o += ind + "\"max_latency_ns\":" + sp + std::to_string(max_latency_ns) + "," + nl;
        o += ind + "\"distinct_model_versions\":" + sp + std::to_string(distinct_model_versions) + "," + nl;
        std::snprintf(buf, sizeof(buf), "%.6f", psi_score);
        o += ind + "\"psi_score\":" + sp + buf + "," + std::string(nl);
        o += ind + "\"period_start_ns\":" + sp + std::to_string(period_start_ns) + "," + nl;
        o += ind + "\"period_end_ns\":" + sp + std::to_string(period_end_ns) + "," + nl;
        o += ind + "\"period_duration_ns\":" + sp + std::to_string(period_duration_ns) + nl;
        o += "}";
        return o;
    }
};

/// Computes Art.15 accuracy, robustness, and drift metrics from inference records.
///
/// Usage:
///   auto metrics = Art15MetricsCalculator::compute(records, 0.5f);
///   auto json = metrics.to_json();
class Art15MetricsCalculator {
public:
    /// Compute Art.15 metrics from a set of inference records.
    ///
    /// @param records                  Inference records to analyze
    /// @param low_confidence_threshold Confidence threshold for flagging (default 0.5)
    /// @return Computed metrics
    [[nodiscard]] static Art15Metrics compute(
            const std::vector<InferenceRecord>& records,
            float low_confidence_threshold = 0.5f) {
        Art15Metrics m;

        if (records.empty()) return m;

        m.total_inferences = static_cast<int64_t>(records.size());

        // Collect confidence scores and latencies
        std::vector<float>   confidences;
        std::vector<int64_t> latencies;
        confidences.reserve(records.size());
        latencies.reserve(records.size());

        std::vector<std::string> model_versions;
        double sum_conf = 0.0;
        int64_t sum_lat = 0;
        m.min_confidence = 1.0f;
        m.max_confidence = 0.0f;

        for (const auto& rec : records) {
            confidences.push_back(rec.output_score);
            latencies.push_back(rec.latency_ns);

            sum_conf += rec.output_score;
            if (rec.output_score < m.min_confidence) m.min_confidence = rec.output_score;
            if (rec.output_score > m.max_confidence) m.max_confidence = rec.output_score;
            if (rec.output_score < low_confidence_threshold) ++m.low_confidence_count;

            sum_lat += rec.latency_ns;

            // Track distinct model versions
            if (std::find(model_versions.begin(), model_versions.end(), rec.model_version)
                == model_versions.end()) {
                model_versions.push_back(rec.model_version);
            }

            // Period bounds
            if (m.period_start_ns == 0 || rec.timestamp_ns < m.period_start_ns)
                m.period_start_ns = rec.timestamp_ns;
            if (rec.timestamp_ns > m.period_end_ns)
                m.period_end_ns = rec.timestamp_ns;
        }

        auto n = static_cast<double>(records.size());

        // Confidence statistics
        m.mean_confidence = static_cast<float>(sum_conf / n);
        m.low_confidence_rate = static_cast<float>(m.low_confidence_count) / static_cast<float>(n);

        // Standard deviation
        double var_sum = 0.0;
        for (float c : confidences) {
            double diff = c - m.mean_confidence;
            var_sum += diff * diff;
        }
        m.std_dev_confidence = static_cast<float>(std::sqrt(var_sum / n));

        // Median confidence
        std::sort(confidences.begin(), confidences.end());
        m.median_confidence = percentile(confidences, 0.5f);

        // Latency statistics
        m.mean_latency_ns = sum_lat / static_cast<int64_t>(records.size());
        std::sort(latencies.begin(), latencies.end());
        m.p50_latency_ns = percentile_i64(latencies, 0.50f);
        m.p95_latency_ns = percentile_i64(latencies, 0.95f);
        m.p99_latency_ns = percentile_i64(latencies, 0.99f);
        m.max_latency_ns = latencies.back();

        // Drift
        m.distinct_model_versions = static_cast<int64_t>(model_versions.size());
        m.period_duration_ns = m.period_end_ns - m.period_start_ns;

        // PSI: split records into two halves (time-ordered) and compare
        // confidence distributions
        if (records.size() >= 20) {
            size_t half = records.size() / 2;
            std::vector<float> first_half, second_half;
            first_half.reserve(half);
            second_half.reserve(records.size() - half);

            // Records are typically in chronological order
            for (size_t i = 0; i < half; ++i)
                first_half.push_back(records[i].output_score);
            for (size_t i = half; i < records.size(); ++i)
                second_half.push_back(records[i].output_score);

            m.psi_score = compute_psi(first_half, second_half, 10);
        }

        return m;
    }

private:
    /// Compute percentile from a sorted vector.
    static float percentile(const std::vector<float>& sorted, float p) {
        if (sorted.empty()) return 0.0f;
        float idx = p * static_cast<float>(sorted.size() - 1);
        auto lo = static_cast<size_t>(idx);
        float frac = idx - static_cast<float>(lo);
        if (lo + 1 >= sorted.size()) return sorted.back();
        return sorted[lo] * (1.0f - frac) + sorted[lo + 1] * frac;
    }

    static int64_t percentile_i64(const std::vector<int64_t>& sorted, float p) {
        if (sorted.empty()) return 0;
        auto idx = static_cast<size_t>(p * static_cast<float>(sorted.size() - 1));
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return sorted[idx];
    }

    /// Compute Population Stability Index (PSI) between two distributions.
    /// Uses histogram binning with `n_bins` equal-width bins.
    /// PSI = Σ (P_i - Q_i) * ln(P_i / Q_i)
    static float compute_psi(const std::vector<float>& reference,
                              const std::vector<float>& current,
                              int n_bins) {
        if (reference.empty() || current.empty() || n_bins <= 0) return 0.0f;

        // Find global min/max
        float gmin = (std::min)(*std::min_element(reference.begin(), reference.end()),
                              *std::min_element(current.begin(), current.end()));
        float gmax = (std::max)(*std::max_element(reference.begin(), reference.end()),
                              *std::max_element(current.begin(), current.end()));

        if (gmax <= gmin) return 0.0f;

        float bin_width = (gmax - gmin) / static_cast<float>(n_bins);
        // Small epsilon to avoid division by zero
        constexpr float eps = 1e-6f;

        std::vector<float> ref_pct(static_cast<size_t>(n_bins), 0.0f);
        std::vector<float> cur_pct(static_cast<size_t>(n_bins), 0.0f);

        auto bin_idx = [&](float v) -> int {
            int b = static_cast<int>((v - gmin) / bin_width);
            if (b >= n_bins) b = n_bins - 1;
            if (b < 0) b = 0;
            return b;
        };

        for (float v : reference) ref_pct[static_cast<size_t>(bin_idx(v))] += 1.0f;
        for (float v : current)   cur_pct[static_cast<size_t>(bin_idx(v))] += 1.0f;

        // Normalize to proportions
        float ref_total = static_cast<float>(reference.size());
        float cur_total = static_cast<float>(current.size());
        for (int i = 0; i < n_bins; ++i) {
            ref_pct[static_cast<size_t>(i)] = ref_pct[static_cast<size_t>(i)] / ref_total + eps;
            cur_pct[static_cast<size_t>(i)] = cur_pct[static_cast<size_t>(i)] / cur_total + eps;
        }

        float psi = 0.0f;
        for (int i = 0; i < n_bins; ++i) {
            float p = cur_pct[static_cast<size_t>(i)];
            float q = ref_pct[static_cast<size_t>(i)];
            psi += (p - q) * std::log(p / q);
        }

        return psi;
    }
};

} // namespace signet::forge
