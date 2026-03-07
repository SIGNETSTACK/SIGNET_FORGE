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
            if (!rdr_result) { chain_ok = false; continue; }
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
            if (!rdr_result) { chain_ok = false; continue; }
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
            if (!rdr_result) { dec_chain_ok = false; continue; }
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
            if (!rdr_result) { inf_chain_ok = false; continue; }
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
            o += ind2 + ind + "\"anomaly\":" + sp
               + (rec.latency_ns > static_cast<int64_t>(ps.avg_latency_ns * 3)
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
                 : 0.0) + nl;
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
        r.period_end_iso   = (opts.end_ns == std::numeric_limits<int64_t>::max())
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
        const size_t n = std::min(f.size(), max_n);
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

} // namespace signet::forge
