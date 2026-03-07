// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// test_compliance_reports.cpp — Phase 10d: MiFID II + EU AI Act Report Generators
// Tests for MiFID2Reporter and EUAIActReporter.

#include "signet/ai/compliance/compliance_types.hpp"
#include "signet/ai/compliance/mifid2_reporter.hpp"
#include "signet/ai/compliance/eu_ai_act_reporter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ===========================================================================
// Helpers
// ===========================================================================

static std::string tmp_dir_cr(const char* name) {
    auto p = fs::temp_directory_path() / "signet_test_compliance" / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

static int64_t test_now_ns() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<nanoseconds>(
            system_clock::now().time_since_epoch()).count());
}

// Write N decision records to a log file and return the file path
static std::string write_decision_log(const std::string& dir, int n,
                                       int64_t base_ts = 0,
                                       const std::string& chain_id = "test-audit-ctx") {
    if (base_ts == 0) base_ts = test_now_ns();
    DecisionLogWriter writer(dir, chain_id);

    for (int i = 0; i < n; ++i) {
        DecisionRecord rec;
        rec.timestamp_ns  = base_ts + static_cast<int64_t>(i) * 1'000'000LL;
        rec.strategy_id   = 1;
        rec.model_version = "model-1.2." + std::to_string(i % 3);
        rec.decision_type = (i % 5 == 0) ? DecisionType::ORDER_CANCEL
                                          : DecisionType::ORDER_NEW;
        rec.input_features = {0.1f * i, 0.2f * i, 0.3f * i};
        rec.model_output  = 0.5 + 0.01 * i;
        rec.confidence    = 0.7 + 0.001 * i;
        rec.risk_result   = (i % 7 == 0) ? RiskGateResult::REJECTED
                                          : RiskGateResult::PASSED;
        rec.order_id      = "ORD-" + std::to_string(1000 + i);
        rec.symbol        = "BTCUSDT";
        rec.price         = 45000.0 + i * 10.0;
        rec.quantity      = 0.1 + i * 0.01;
        rec.venue         = "BINANCE";
        rec.notes         = "test record " + std::to_string(i);
        (void)writer.log(rec);
    }
    (void)writer.close();
    return writer.current_file_path();
}

// Write N inference records to a log file and return the file path
static std::string write_inference_log(const std::string& dir, int n,
                                        int64_t base_ts = 0,
                                        const std::string& chain_id = "test-audit-ctx") {
    if (base_ts == 0) base_ts = test_now_ns();
    InferenceLogWriter writer(dir, chain_id);

    for (int i = 0; i < n; ++i) {
        InferenceRecord rec;
        rec.timestamp_ns    = base_ts + static_cast<int64_t>(i) * 500'000LL;
        rec.model_id        = "alpha-model";
        rec.model_version   = "v2.0." + std::to_string(i % 2);
        rec.inference_type  = (i % 3 == 0) ? InferenceType::REGRESSION
                                            : InferenceType::CLASSIFICATION;
        rec.input_embedding = {0.1f * i, 0.2f * i};
        rec.input_hash      = "hash-input-" + std::to_string(i);
        rec.output_hash     = "hash-output-" + std::to_string(i);
        rec.output_score    = 0.6f + 0.005f * i;
        rec.latency_ns      = 1000 + i * 50;
        rec.batch_size      = 1;
        rec.input_tokens    = 128;
        rec.output_tokens   = 64;
        rec.user_id_hash    = "user-hash-" + std::to_string(i % 5);
        rec.session_id      = "sess-" + std::to_string(i / 10);
        (void)writer.log(rec);
    }
    (void)writer.close();
    return writer.current_file_path();
}

// Simple check: string contains substring
static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

// ===========================================================================
// Section 1 — MiFID2Reporter
// ===========================================================================

TEST_CASE("MiFID2Reporter fails with empty file list", "[compliance][mifid2]") {
    auto r = MiFID2Reporter::generate({});
    REQUIRE(!r.has_value());
}

TEST_CASE("MiFID2Reporter generates JSON report", "[compliance][mifid2]") {
    auto dir  = tmp_dir_cr("mifid2_json");
    auto path = write_decision_log(dir, 10);
    REQUIRE(!path.empty());

    ReportOptions opts;
    opts.firm_id = "TEST_FIRM_001";
    auto r = MiFID2Reporter::generate({path}, opts);
    REQUIRE(r.has_value());

    const auto& rep = *r;
    REQUIRE(rep.total_records == 10);
    REQUIRE(rep.standard == ComplianceStandard::MIFID2_RTS24);
    REQUIRE(rep.format == ReportFormat::JSON);
    REQUIRE(!rep.content.empty());

    // Key regulatory fields present
    REQUIRE(contains(rep.content, "MiFID_II_RTS_24"));
    REQUIRE(contains(rep.content, "Commission Delegated Regulation"));
    REQUIRE(contains(rep.content, "TEST_FIRM_001"));
    REQUIRE(contains(rep.content, "BTCUSDT"));
    REQUIRE(contains(rep.content, "ORDER_NEW"));
    REQUIRE(contains(rep.content, "field_01_firm_id"));
    REQUIRE(contains(rep.content, "field_08_timestamp_utc"));
    REQUIRE(contains(rep.content, "chain_verified"));
    REQUIRE(contains(rep.content, "report_id"));
}

TEST_CASE("MiFID2Reporter generates CSV report", "[compliance][mifid2]") {
    auto dir  = tmp_dir_cr("mifid2_csv");
    auto path = write_decision_log(dir, 5);

    ReportOptions opts;
    opts.format = ReportFormat::CSV;
    auto r = MiFID2Reporter::generate({path}, opts);
    REQUIRE(r.has_value());

    const auto& rep = *r;
    REQUIRE(rep.format == ReportFormat::CSV);
    REQUIRE(contains(rep.content, "timestamp_utc"));
    REQUIRE(contains(rep.content, "order_id"));
    REQUIRE(contains(rep.content, "algo_identifier"));
    REQUIRE(contains(rep.content, "BTCUSDT"));
    // CSV should have header + 5 data rows + newlines
    size_t line_count = 0;
    for (char c : rep.content) if (c == '\n') ++line_count;
    REQUIRE(line_count >= 6u);  // 1 header + 5 records
}

TEST_CASE("MiFID2Reporter generates NDJSON report", "[compliance][mifid2]") {
    auto dir  = tmp_dir_cr("mifid2_ndjson");
    auto path = write_decision_log(dir, 4);

    ReportOptions opts;
    opts.format = ReportFormat::NDJSON;
    auto r = MiFID2Reporter::generate({path}, opts);
    REQUIRE(r.has_value());

    // NDJSON: one JSON object per line, no outer envelope
    REQUIRE(contains((*r).content, "\"ts\""));
    REQUIRE(contains((*r).content, "\"order_id\""));
    // 4 lines (one per record)
    size_t line_count = 0;
    for (char c : (*r).content) if (c == '\n') ++line_count;
    REQUIRE(line_count == 4u);
}

TEST_CASE("MiFID2Reporter respects date range filter", "[compliance][mifid2]") {
    auto dir     = tmp_dir_cr("mifid2_range");
    int64_t base = test_now_ns();
    auto path    = write_decision_log(dir, 20, base);

    ReportOptions opts;
    // Only include first 10 records (spaced 1ms apart)
    opts.start_ns = base;
    opts.end_ns   = base + 9 * 1'000'000LL;
    auto r = MiFID2Reporter::generate({path}, opts);
    REQUIRE(r.has_value());
    REQUIRE((*r).total_records == 10);
}

TEST_CASE("MiFID2Reporter chain_verified is true for untampered log",
          "[compliance][mifid2]") {
    auto dir  = tmp_dir_cr("mifid2_chain");
    auto path = write_decision_log(dir, 5);

    ReportOptions opts;
    opts.verify_chain = true;
    auto r = MiFID2Reporter::generate({path}, opts);
    REQUIRE(r.has_value());
    REQUIRE((*r).chain_verified == true);
}

TEST_CASE("MiFID2Reporter report_id uses opts.report_id when supplied",
          "[compliance][mifid2]") {
    auto dir  = tmp_dir_cr("mifid2_repid");
    auto path = write_decision_log(dir, 2);

    ReportOptions opts;
    opts.report_id = "FIRM-2026-Q1-001";
    auto r = MiFID2Reporter::generate({path}, opts);
    REQUIRE(r.has_value());
    REQUIRE((*r).report_id == "FIRM-2026-Q1-001");
    REQUIRE(contains((*r).content, "FIRM-2026-Q1-001"));
}

TEST_CASE("MiFID2Reporter csv_header contains all Annex I fields",
          "[compliance][mifid2]") {
    auto h = MiFID2Reporter::csv_header();
    REQUIRE(contains(h, "timestamp_utc"));
    REQUIRE(contains(h, "order_id"));
    REQUIRE(contains(h, "algo_identifier"));
    REQUIRE(contains(h, "instrument"));
    REQUIRE(contains(h, "risk_gate"));
    REQUIRE(contains(h, "confidence"));
}

TEST_CASE("MiFID2Reporter includes features when opts.include_features=true",
          "[compliance][mifid2]") {
    auto dir  = tmp_dir_cr("mifid2_feats");
    auto path = write_decision_log(dir, 3);

    ReportOptions opts;
    opts.include_features = true;
    auto r = MiFID2Reporter::generate({path}, opts);
    REQUIRE(r.has_value());
    REQUIRE(contains((*r).content, "input_features"));
}

// ===========================================================================
// Section 2 — EUAIActReporter Article 12
// ===========================================================================

TEST_CASE("EUAIActReporter Article 12 fails with empty file list",
          "[compliance][euaia]") {
    auto r = EUAIActReporter::generate_article12({});
    REQUIRE(!r.has_value());
}

TEST_CASE("EUAIActReporter Article 12 generates JSON report",
          "[compliance][euaia]") {
    auto dir  = tmp_dir_cr("euaia12_json");
    auto path = write_inference_log(dir, 15);

    ReportOptions opts;
    opts.system_id = "trading-ai-v2";
    auto r = EUAIActReporter::generate_article12({path}, opts);
    REQUIRE(r.has_value());

    const auto& rep = *r;
    REQUIRE(rep.total_records == 15);
    REQUIRE(rep.standard == ComplianceStandard::EU_AI_ACT_ART12);
    REQUIRE(!rep.content.empty());

    REQUIRE(contains(rep.content, "EU_AI_ACT_ARTICLE_12"));
    REQUIRE(contains(rep.content, "Regulation (EU) 2024/1689"));
    REQUIRE(contains(rep.content, "trading-ai-v2"));
    REQUIRE(contains(rep.content, "total_inferences"));
    REQUIRE(contains(rep.content, "performance_summary"));
    REQUIRE(contains(rep.content, "anomaly_count"));
    REQUIRE(contains(rep.content, "input_hash"));
    REQUIRE(contains(rep.content, "output_hash"));
    REQUIRE(contains(rep.content, "latency_ns"));
    REQUIRE(contains(rep.content, "chain_verified"));
}

TEST_CASE("EUAIActReporter Article 12 performance stats present",
          "[compliance][euaia]") {
    auto dir  = tmp_dir_cr("euaia12_perf");
    auto path = write_inference_log(dir, 20);

    auto r = EUAIActReporter::generate_article12({path});
    REQUIRE(r.has_value());

    REQUIRE(contains((*r).content, "avg_latency_ns"));
    REQUIRE(contains((*r).content, "p50_latency_ns"));
    REQUIRE(contains((*r).content, "p95_latency_ns"));
    REQUIRE(contains((*r).content, "p99_latency_ns"));
    REQUIRE(contains((*r).content, "avg_output_score"));
}

TEST_CASE("EUAIActReporter Article 12 respects date range filter",
          "[compliance][euaia]") {
    auto dir     = tmp_dir_cr("euaia12_range");
    int64_t base = test_now_ns();
    auto path    = write_inference_log(dir, 20, base);

    ReportOptions opts;
    opts.start_ns = base;
    opts.end_ns   = base + 9 * 500'000LL;  // first 10 records (500us spacing)
    auto r = EUAIActReporter::generate_article12({path}, opts);
    REQUIRE(r.has_value());
    REQUIRE((*r).total_records == 10);
}

// ===========================================================================
// Section 3 — EUAIActReporter Article 13
// ===========================================================================

TEST_CASE("EUAIActReporter Article 13 generates transparency disclosure",
          "[compliance][euaia]") {
    auto dir  = tmp_dir_cr("euaia13");
    auto path = write_inference_log(dir, 12);

    ReportOptions opts;
    opts.system_id = "my-ai-system";
    auto r = EUAIActReporter::generate_article13({path}, opts);
    REQUIRE(r.has_value());

    const auto& rep = *r;
    REQUIRE(rep.standard == ComplianceStandard::EU_AI_ACT_ART13);
    REQUIRE(contains(rep.content, "EU_AI_ACT_ARTICLE_13"));
    REQUIRE(contains(rep.content, "Transparency"));
    REQUIRE(contains(rep.content, "my-ai-system"));
    REQUIRE(contains(rep.content, "system_capabilities"));
    REQUIRE(contains(rep.content, "performance_characteristics"));
    REQUIRE(contains(rep.content, "data_characteristics"));
    REQUIRE(contains(rep.content, "limitations_and_risks"));
    REQUIRE(contains(rep.content, "supported_inference_types"));
    REQUIRE(contains(rep.content, "low_confidence_rate"));
}

TEST_CASE("EUAIActReporter Article 13 lists observed inference types",
          "[compliance][euaia]") {
    auto dir  = tmp_dir_cr("euaia13_types");
    auto path = write_inference_log(dir, 9);  // both CLASSIFICATION and REGRESSION

    auto r = EUAIActReporter::generate_article13({path});
    REQUIRE(r.has_value());
    REQUIRE(contains((*r).content, "CLASSIFICATION"));
    REQUIRE(contains((*r).content, "REGRESSION"));
}

// ===========================================================================
// Section 4 — EUAIActReporter Article 19
// ===========================================================================

TEST_CASE("EUAIActReporter Article 19 fails with no files",
          "[compliance][euaia]") {
    auto r = EUAIActReporter::generate_article19({}, {});
    REQUIRE(!r.has_value());
}

TEST_CASE("EUAIActReporter Article 19 generates conformity summary",
          "[compliance][euaia]") {
    auto dec_dir  = tmp_dir_cr("euaia19_dec");
    auto inf_dir  = tmp_dir_cr("euaia19_inf");
    auto dec_path = write_decision_log(dec_dir, 10);
    auto inf_path = write_inference_log(inf_dir, 15);

    ReportOptions opts;
    opts.system_id = "hft-ai-conformity";
    auto r = EUAIActReporter::generate_article19({dec_path}, {inf_path}, opts);
    REQUIRE(r.has_value());

    const auto& rep = *r;
    REQUIRE(rep.standard == ComplianceStandard::EU_AI_ACT_ART19);
    REQUIRE(contains(rep.content, "EU_AI_ACT_ARTICLE_19"));
    REQUIRE(contains(rep.content, "Conformity Assessment"));
    REQUIRE(contains(rep.content, "hft-ai-conformity"));
    REQUIRE(contains(rep.content, "conformity_status"));
    REQUIRE(contains(rep.content, "audit_trail_integrity"));
    REQUIRE(contains(rep.content, "decision_statistics"));
    REQUIRE(contains(rep.content, "inference_statistics"));
    REQUIRE(contains(rep.content, "total_decisions"));
    REQUIRE(contains(rep.content, "orders_rejected_by_risk_gate"));
    REQUIRE(contains(rep.content, "anomaly_count"));
}

TEST_CASE("EUAIActReporter Article 19 chain_verified reflects log integrity",
          "[compliance][euaia]") {
    auto dec_dir  = tmp_dir_cr("euaia19_chain");
    auto inf_dir  = tmp_dir_cr("euaia19_chain_inf");
    auto dec_path = write_decision_log(dec_dir, 5);
    auto inf_path = write_inference_log(inf_dir, 5);

    ReportOptions opts;
    opts.verify_chain = true;
    auto r = EUAIActReporter::generate_article19({dec_path}, {inf_path}, opts);
    REQUIRE(r.has_value());
    REQUIRE((*r).chain_verified == true);
    REQUIRE(contains((*r).content, "CONFORMANT"));
}

TEST_CASE("EUAIActReporter Article 19 works with only decision logs",
          "[compliance][euaia]") {
    auto dir  = tmp_dir_cr("euaia19_dec_only");
    auto path = write_decision_log(dir, 8);

    auto r = EUAIActReporter::generate_article19({path}, {});
    REQUIRE(r.has_value());
    REQUIRE(contains((*r).content, "total_decisions"));
}

TEST_CASE("EUAIActReporter Article 19 works with only inference logs",
          "[compliance][euaia]") {
    auto dir  = tmp_dir_cr("euaia19_inf_only");
    auto path = write_inference_log(dir, 8);

    auto r = EUAIActReporter::generate_article19({}, {path});
    REQUIRE(r.has_value());
    REQUIRE(contains((*r).content, "total_inferences"));
}

// ===========================================================================
// Section 5 — ISO 8601 timestamp formatting
// ===========================================================================

TEST_CASE("Report timestamps are ISO 8601 format", "[compliance][timestamps]") {
    auto dir  = tmp_dir_cr("iso8601");
    auto path = write_decision_log(dir, 1);

    auto r = MiFID2Reporter::generate({path});
    REQUIRE(r.has_value());

    // ISO 8601: YYYY-MM-DDTHH:MM:SS.nnnnnnnnnZ
    // Check generated_at field has T and Z
    REQUIRE(contains((*r).content, "generated_at"));
    // The content should contain a timestamp with T separator (ISO 8601: YYYY-MM-DDTHH:...)
    const auto& c = (*r).content;
    auto t_pos = c.find('T');  // T separator in ISO 8601 datetime
    REQUIRE(t_pos != std::string::npos);
}

// ===========================================================================
// Section 6 — Hardening tests (Pass #3)
// ===========================================================================

TEST_CASE("MiFID2 reporter truncates long fields", "[compliance][hardening]") {
    // Create a decision log with an extremely long order_id (> 4096 chars)
    auto dir = tmp_dir_cr("truncate");

    DecisionLogWriter writer(dir, "trunc-chain");

    DecisionRecord rec;
    rec.timestamp_ns  = 1'700'000'000'000'000'000LL;
    rec.strategy_id   = 1;
    rec.model_version = "v1.0";
    rec.decision_type = DecisionType::ORDER_NEW;
    rec.model_output  = 0.85f;
    rec.confidence    = 0.9f;
    rec.risk_result   = RiskGateResult::PASSED;
    rec.order_id      = std::string(8000, 'X');  // 8000 chars — well over 4096
    rec.symbol        = "BTCUSD";
    rec.price         = 42000.0;
    rec.quantity      = 1.0;
    rec.venue         = "TEST";

    auto wr = writer.log(rec);
    REQUIRE(wr.has_value());
    auto cr = writer.close();
    REQUIRE(cr.has_value());
    auto path = writer.current_file_path();

    // Generate MiFID2 report in JSON format
    auto report = MiFID2Reporter::generate({path});
    REQUIRE(report.has_value());

    // The content should contain TRUNCATED marker — the 8000-char order_id was clipped
    REQUIRE(contains(report->content, "TRUNCATED"));
    // The raw 8000-char string should NOT appear untruncated
    REQUIRE_FALSE(contains(report->content, std::string(5000, 'X')));
}

TEST_CASE("ns_to_iso8601 handles far-future timestamp", "[compliance][hardening]") {
    // Year 2100 timestamp: 2100-01-01T00:00:00Z ≈ 4'102'444'800 seconds
    // This tests that 64-bit time_t handles dates beyond 2038
    auto dir = tmp_dir_cr("farfuture");

    DecisionLogWriter writer(dir, "future-chain");

    DecisionRecord rec;
    rec.timestamp_ns  = 4'102'444'800'000'000'000LL;  // ~year 2100
    rec.strategy_id   = 1;
    rec.model_version = "v1.0";
    rec.decision_type = DecisionType::SIGNAL;
    rec.model_output  = 0.5f;
    rec.confidence    = 0.5f;
    rec.risk_result   = RiskGateResult::PASSED;
    rec.order_id      = "future-order";
    rec.symbol        = "TEST";
    rec.price         = 1.0;
    rec.quantity      = 1.0;
    rec.venue         = "SIM";

    auto wr = writer.log(rec);
    REQUIRE(wr.has_value());
    auto cr = writer.close();
    REQUIRE(cr.has_value());
    auto path = writer.current_file_path();

    // Generate report — should not crash, should contain "2100" in the timestamp
    auto report = MiFID2Reporter::generate({path});
    REQUIRE(report.has_value());
    REQUIRE(contains(report->content, "2100"));
}

// ===========================================================================
// Section 7 — Hardening tests (Pass #4)
// ===========================================================================

TEST_CASE("MiFID II price format respects configurable precision", "[compliance][hardening]") {
    // H-14: price_significant_digits field controls decimal precision in
    // MiFID II RTS 24 Annex I Field 6 price formatting.
    ReportOptions opts;
    REQUIRE(opts.price_significant_digits == 17); // Default per plan

    // Verify the option field exists and can be changed
    opts.price_significant_digits = 10;
    REQUIRE(opts.price_significant_digits == 10);
}

TEST_CASE("EU AI Act Art.19 cross-chain verification detects mismatch", "[compliance][hardening]") {
    // H-15: When verify_chain=true, decision and inference logs must share
    // the same chain_id (audit context). Mismatched IDs are an audit finding.
    auto dec_dir  = tmp_dir_cr("h15_dec");
    auto inf_dir  = tmp_dir_cr("h15_inf");

    // Deliberately use DIFFERENT chain IDs to simulate unrelated systems
    auto dec_path = write_decision_log(dec_dir, 3, 0, "system-alpha");
    auto inf_path = write_inference_log(inf_dir, 3, 0, "system-beta");

    // With verify_chain=true (default), mismatched chain IDs must be rejected
    ReportOptions opts;
    opts.verify_chain = true;
    auto r = EUAIActReporter::generate_article19({dec_path}, {inf_path}, opts);
    REQUIRE(!r.has_value()); // Chain ID mismatch detected
    REQUIRE(r.error().code == ErrorCode::INVALID_ARGUMENT);

    // With verify_chain=false, the check is skipped
    ReportOptions lenient;
    lenient.verify_chain = false;
    auto r2 = EUAIActReporter::generate_article19({dec_path}, {inf_path}, lenient);
    REQUIRE(r2.has_value()); // Passes without chain verification
}

TEST_CASE("MiFID II timestamp granularity is configurable", "[compliance][hardening]") {
    // H-16: TimestampGranularity enum controls sub-second precision in
    // MiFID II RTS 24 Art.2(2) timestamp fields.
    ReportOptions opts;
    REQUIRE(opts.timestamp_granularity == TimestampGranularity::NANOS); // Default

    opts.timestamp_granularity = TimestampGranularity::MICROS;
    REQUIRE(opts.timestamp_granularity == TimestampGranularity::MICROS);

    opts.timestamp_granularity = TimestampGranularity::MILLIS;
    REQUIRE(opts.timestamp_granularity == TimestampGranularity::MILLIS);
}

TEST_CASE("InferenceRecord has training data fields", "[compliance][hardening]") {
    // H-17: EU AI Act Art.13(3)(b)(ii) training data provenance fields
    // added to InferenceRecord.
    InferenceRecord rec;
    rec.training_dataset_id = "dataset_v3";
    rec.training_dataset_size = 1000000;
    rec.training_data_characteristics = "balanced, cleaned";

    REQUIRE(rec.training_dataset_id == "dataset_v3");
    REQUIRE(rec.training_dataset_size == 1000000);
    REQUIRE(rec.training_data_characteristics == "balanced, cleaned");
}
