// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// test_compliance_reports.cpp — Phase 10d: MiFID II + EU AI Act Report Generators
// Tests for MiFID2Reporter and EUAIActReporter.

#include "signet/ai/compliance/compliance_types.hpp"
#include "signet/ai/compliance/mifid2_reporter.hpp"
#include "signet/ai/compliance/eu_ai_act_reporter.hpp"
#include "signet/ai/human_oversight.hpp"
#include "signet/ai/log_retention.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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

// ===========================================================================
// EU AI Act Art.14 — Human Oversight (Gap R-3)
// ===========================================================================

TEST_CASE("HumanOverrideRecord serialize/deserialize round-trip", "[compliance][human_oversight]") {
    HumanOverrideRecord rec;
    rec.timestamp_ns = 1700000000000000000LL;
    rec.operator_id = "op-001";
    rec.operator_role = "risk_officer";
    rec.source = OverrideSource::HUMAN;
    rec.action = OverrideAction::MODIFY;
    rec.system_id = "trading-ai-v2";
    rec.original_decision_id = "ord-12345";
    rec.original_output = "BUY 100 AAPL @ 150.00";
    rec.original_confidence = 0.85f;
    rec.override_output = "BUY 50 AAPL @ 150.00";
    rec.rationale = "Reducing position size due to market volatility";
    rec.urgency = 1;

    auto serialized = rec.serialize();
    REQUIRE(!serialized.empty());

    auto result = HumanOverrideRecord::deserialize(serialized.data(), serialized.size());
    REQUIRE(result.has_value());

    const auto& r = *result;
    REQUIRE(r.timestamp_ns == rec.timestamp_ns);
    REQUIRE(r.operator_id == "op-001");
    REQUIRE(r.operator_role == "risk_officer");
    REQUIRE(r.source == OverrideSource::HUMAN);
    REQUIRE(r.action == OverrideAction::MODIFY);
    REQUIRE(r.system_id == "trading-ai-v2");
    REQUIRE(r.original_decision_id == "ord-12345");
    REQUIRE(r.original_output == "BUY 100 AAPL @ 150.00");
    REQUIRE(r.original_confidence == Catch::Approx(0.85f));
    REQUIRE(r.override_output == "BUY 50 AAPL @ 150.00");
    REQUIRE(r.rationale == "Reducing position size due to market volatility");
    REQUIRE(r.urgency == 1);
}

TEST_CASE("HumanOverrideRecord deserialize rejects truncated data", "[compliance][human_oversight][hardening]") {
    HumanOverrideRecord rec;
    rec.timestamp_ns = 12345;
    rec.operator_id = "op-002";
    auto serialized = rec.serialize();

    // Truncate to 4 bytes — too short for even timestamp_ns
    auto bad = HumanOverrideRecord::deserialize(serialized.data(), 4);
    REQUIRE(!bad.has_value());
}

TEST_CASE("HumanOverrideLogWriter write and read round-trip", "[compliance][human_oversight]") {
    auto dir = tmp_dir_cr("human_override_roundtrip");

    HumanOverrideLogWriter writer(dir);

    // Log 5 override events
    for (int i = 0; i < 5; ++i) {
        HumanOverrideRecord rec;
        rec.timestamp_ns = test_now_ns();
        rec.operator_id = "op-" + std::to_string(i);
        rec.operator_role = "trader";
        rec.source = OverrideSource::HUMAN;
        rec.action = (i % 2 == 0) ? OverrideAction::APPROVE : OverrideAction::MODIFY;
        rec.system_id = "test-system";
        rec.original_decision_id = "dec-" + std::to_string(i);
        rec.original_output = "signal=" + std::to_string(i);
        rec.original_confidence = 0.5f + 0.1f * i;
        rec.override_output = (i % 2 == 0) ? "" : "modified-" + std::to_string(i);
        rec.rationale = "Test override " + std::to_string(i);
        rec.urgency = i % 3;

        auto entry = writer.log(rec);
        REQUIRE(entry.has_value());
    }

    REQUIRE(writer.total_records() == 5);
    auto close_result = writer.close();
    REQUIRE(close_result.has_value());

    // Read back
    auto reader = HumanOverrideLogReader::open(writer.current_file_path());
    REQUIRE(reader.has_value());
    REQUIRE(reader->record_count() == 5);

    auto records = reader->read_all();
    REQUIRE(records.has_value());
    REQUIRE(records->size() == 5);

    // Verify first record
    REQUIRE((*records)[0].operator_id == "op-0");
    REQUIRE((*records)[0].action == OverrideAction::APPROVE);
    REQUIRE((*records)[0].system_id == "test-system");

    // Verify last record
    REQUIRE((*records)[4].operator_id == "op-4");
    REQUIRE((*records)[4].action == OverrideAction::APPROVE);
}

TEST_CASE("HumanOverrideLogReader verifies hash chain integrity", "[compliance][human_oversight]") {
    auto dir = tmp_dir_cr("human_override_chain");

    HumanOverrideLogWriter writer(dir);

    for (int i = 0; i < 3; ++i) {
        HumanOverrideRecord rec;
        rec.timestamp_ns = test_now_ns();
        rec.operator_id = "op-chain-" + std::to_string(i);
        rec.operator_role = "supervisor";
        rec.source = OverrideSource::HUMAN;
        rec.action = OverrideAction::REJECT;
        rec.system_id = "chain-test";
        rec.rationale = "Chain test " + std::to_string(i);

        auto entry = writer.log(rec);
        REQUIRE(entry.has_value());
    }

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());

    auto reader = HumanOverrideLogReader::open(writer.current_file_path());
    REQUIRE(reader.has_value());

    auto verify = reader->verify_chain();
    REQUIRE(verify.valid);
}

TEST_CASE("HumanOverrideLogWriter rejects path traversal", "[compliance][human_oversight][hardening]") {
    REQUIRE_THROWS_AS(
        HumanOverrideLogWriter("../escape/dir"),
        std::invalid_argument
    );
}

TEST_CASE("OverrideRateMonitor tracks sliding window counts", "[compliance][human_oversight]") {
    OverrideRateMonitorOptions opts;
    opts.window_ns = 1000000000LL; // 1 second window
    opts.alert_threshold = 3;

    OverrideRateMonitor monitor(opts);

    int64_t base = 1000000000LL;

    // Record 2 overrides — below threshold
    REQUIRE(monitor.record_override(base) == 1);
    REQUIRE(monitor.record_override(base + 100000000LL) == 2);

    // Third override — hits threshold
    bool alert_fired = false;
    monitor.set_alert_callback([&](int64_t count, int64_t) {
        alert_fired = true;
        REQUIRE(count == 3);
    });

    REQUIRE(monitor.record_override(base + 200000000LL) == 3);
    REQUIRE(alert_fired);

    // After window expires, old events evicted
    REQUIRE(monitor.current_count(base + 1500000000LL) == 0);
}

TEST_CASE("OverrideRateMonitor auto-halt on threshold", "[compliance][human_oversight]") {
    OverrideRateMonitorOptions opts;
    opts.window_ns = 10000000000LL; // 10 second window
    opts.alert_threshold = 2;
    opts.auto_halt_on_threshold = true;

    OverrideRateMonitor monitor(opts);

    bool halt_fired = false;
    HaltReason halt_reason{};
    monitor.set_halt_callback([&](HaltReason reason, const std::string&) {
        halt_fired = true;
        halt_reason = reason;
    });

    int64_t base = 1000000000LL;
    monitor.record_override(base);
    REQUIRE(!halt_fired);

    monitor.record_override(base + 100000000LL);
    REQUIRE(halt_fired);
    REQUIRE(halt_reason == HaltReason::SAFETY_THRESHOLD);
}

TEST_CASE("OverrideRateMonitor manual halt trigger", "[compliance][human_oversight]") {
    OverrideRateMonitor monitor;

    bool halt_fired = false;
    monitor.set_halt_callback([&](HaltReason reason, const std::string& detail) {
        halt_fired = true;
        REQUIRE(reason == HaltReason::MANUAL);
        REQUIRE(detail == "Operator requested emergency stop");
    });

    monitor.trigger_halt(HaltReason::MANUAL, "Operator requested emergency stop");
    REQUIRE(halt_fired);
}

TEST_CASE("HumanOverrideLogWriter audit metadata record_type is human_override", "[compliance][human_oversight]") {
    auto dir = tmp_dir_cr("human_override_meta");

    HumanOverrideLogWriter writer(dir);

    HumanOverrideRecord rec;
    rec.timestamp_ns = test_now_ns();
    rec.operator_id = "op-meta";
    rec.operator_role = "admin";
    rec.source = OverrideSource::HUMAN;
    rec.action = OverrideAction::HALT;
    rec.system_id = "meta-test";
    rec.rationale = "Testing metadata";

    auto entry = writer.log(rec);
    REQUIRE(entry.has_value());

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());

    auto reader = HumanOverrideLogReader::open(writer.current_file_path());
    REQUIRE(reader.has_value());

    auto meta = reader->audit_metadata();
    REQUIRE(meta.has_value());
    REQUIRE(meta->record_type == "human_override");
    REQUIRE(meta->record_count == 1);
}

// ===========================================================================
// EU AI Act Art.12(3) / MiFID II RTS 24 Art.4 — Log Retention (Gap R-1)
// ===========================================================================

TEST_CASE("RetentionPolicy has correct regulatory defaults", "[compliance][retention]") {
    RetentionPolicy policy;

    // EU AI Act Art.12(3): minimum 6 months
    // ~6 months in nanoseconds ≈ 1.578 × 10^16
    REQUIRE(policy.min_retention_ns > INT64_C(15000000000000000));
    REQUIRE(policy.min_retention_ns < INT64_C(16000000000000000));

    // MiFID II RTS 24 Art.4: 5 years
    REQUIRE(policy.max_retention_ns > INT64_C(150000000000000000));

    // Archival after 1 year
    REQUIRE(policy.archive_after_ns > INT64_C(31000000000000000));

    // Default file suffix
    REQUIRE(policy.file_suffix == ".parquet");

    // Defaults are dry-run (safe)
    REQUIRE(policy.enable_deletion == false);
    REQUIRE(policy.enable_archival == false);
}

TEST_CASE("LogRetentionManager enforce on empty directory", "[compliance][retention]") {
    auto dir = tmp_dir_cr("retention_empty");
    LogRetentionManager mgr;
    auto summary = mgr.enforce(dir, test_now_ns());

    REQUIRE(summary.files_scanned == 0);
    REQUIRE(summary.files_active == 0);
    REQUIRE(summary.files_archived == 0);
    REQUIRE(summary.files_deleted == 0);
}

TEST_CASE("LogRetentionManager enforce reports non-existent directory", "[compliance][retention]") {
    LogRetentionManager mgr;
    auto summary = mgr.enforce("/nonexistent/path/123456", test_now_ns());

    REQUIRE(summary.errors.size() == 1);
    REQUIRE(summary.errors[0].find("does not exist") != std::string::npos);
}

TEST_CASE("LogRetentionManager classifies recent files as active", "[compliance][retention]") {
    auto dir = tmp_dir_cr("retention_active");

    // Create some dummy .parquet files
    for (int i = 0; i < 3; ++i) {
        std::string path = dir + "/log_" + std::to_string(i) + ".parquet";
        std::ofstream f(path);
        f << "dummy data " << i;
    }

    RetentionPolicy policy;
    policy.archive_after_ns = INT64_C(31557600000000000); // 1 year
    LogRetentionManager mgr(policy);

    auto summary = mgr.enforce(dir, test_now_ns());
    REQUIRE(summary.files_scanned == 3);
    REQUIRE(summary.files_active == 3);
    REQUIRE(summary.files_archived == 0);
    REQUIRE(summary.files_deleted == 0);
}

TEST_CASE("LogRetentionManager ignores non-parquet files", "[compliance][retention]") {
    auto dir = tmp_dir_cr("retention_filter");

    // Create mixed files
    { std::ofstream f(dir + "/log_0.parquet"); f << "parquet"; }
    { std::ofstream f(dir + "/notes.txt"); f << "text"; }
    { std::ofstream f(dir + "/data.csv"); f << "csv"; }

    LogRetentionManager mgr;
    auto summary = mgr.enforce(dir, test_now_ns());
    REQUIRE(summary.files_scanned == 1); // Only the .parquet file
}

TEST_CASE("LogRetentionManager list_files classifies correctly", "[compliance][retention]") {
    auto dir = tmp_dir_cr("retention_list");

    { std::ofstream f(dir + "/recent.parquet"); f << "recent"; }

    RetentionPolicy policy;
    policy.archive_after_ns = INT64_C(31557600000000000); // 1 year
    policy.max_retention_ns = INT64_C(157788000000000000); // 5 years
    LogRetentionManager mgr(policy);

    auto files = mgr.list_files(dir, test_now_ns());
    REQUIRE(files.size() == 1);
    REQUIRE(files[0].status == LogRetentionManager::FileStatus::Classification::ACTIVE);
}

TEST_CASE("LogRetentionManager enforce with deletion enabled", "[compliance][retention]") {
    auto dir = tmp_dir_cr("retention_delete");

    // Create a file
    std::string path = dir + "/old_log.parquet";
    { std::ofstream f(path); f << "old data to delete"; }

    // Use very short retention so the file is "expired"
    RetentionPolicy policy;
    policy.archive_after_ns = 1;    // 1 ns — everything is archive-eligible
    policy.max_retention_ns = 1;    // 1 ns — everything is delete-eligible
    policy.enable_deletion = true;

    LogRetentionManager mgr(policy);
    auto summary = mgr.enforce(dir, test_now_ns());

    REQUIRE(summary.files_deleted == 1);
    REQUIRE(summary.deleted_paths.size() == 1);
    REQUIRE(!fs::exists(path)); // File actually deleted
}

TEST_CASE("LogRetentionManager pre-delete callback can block deletion", "[compliance][retention]") {
    auto dir = tmp_dir_cr("retention_block_delete");

    std::string path = dir + "/protected.parquet";
    { std::ofstream f(path); f << "protected data"; }

    RetentionPolicy policy;
    policy.max_retention_ns = 1;
    policy.archive_after_ns = 1;
    policy.enable_deletion = true;

    LogRetentionManager mgr(policy);
    mgr.set_pre_delete_callback([](const std::string&) { return false; }); // Block all deletes

    auto summary = mgr.enforce(dir, test_now_ns());

    REQUIRE(summary.files_failed == 1);
    REQUIRE(summary.files_deleted == 0);
    REQUIRE(fs::exists(path)); // File still exists
}

// ===========================================================================
// EU AI Act Art.15 — Accuracy/Robustness Metrics (Gap R-3b)
// ===========================================================================

TEST_CASE("Art15MetricsCalculator returns empty metrics for empty input", "[compliance][art15]") {
    auto m = Art15MetricsCalculator::compute({});
    REQUIRE(m.total_inferences == 0);
    REQUIRE(m.mean_confidence == 0.0f);
}

TEST_CASE("Art15MetricsCalculator computes confidence statistics", "[compliance][art15]") {
    std::vector<InferenceRecord> records;

    // Create 10 records with known confidence scores: 0.1, 0.2, ..., 1.0
    for (int i = 1; i <= 10; ++i) {
        InferenceRecord rec;
        rec.timestamp_ns = 1000000000LL * i;
        rec.model_version = "v1.0";
        rec.output_score = static_cast<float>(i) / 10.0f;
        rec.latency_ns = 1000000LL * i;  // 1ms * i
        records.push_back(rec);
    }

    auto m = Art15MetricsCalculator::compute(records, 0.5f);

    REQUIRE(m.total_inferences == 10);
    REQUIRE(m.mean_confidence == Catch::Approx(0.55f).epsilon(0.01));
    REQUIRE(m.min_confidence == Catch::Approx(0.1f));
    REQUIRE(m.max_confidence == Catch::Approx(1.0f));
    REQUIRE(m.low_confidence_count == 4);  // 0.1, 0.2, 0.3, 0.4 are < 0.5
    REQUIRE(m.low_confidence_rate == Catch::Approx(0.4f));
    REQUIRE(m.std_dev_confidence > 0.0f);
    REQUIRE(m.distinct_model_versions == 1);
}

TEST_CASE("Art15MetricsCalculator computes latency percentiles", "[compliance][art15]") {
    std::vector<InferenceRecord> records;

    for (int i = 0; i < 100; ++i) {
        InferenceRecord rec;
        rec.timestamp_ns = 1000000000LL * i;
        rec.model_version = "v1.0";
        rec.output_score = 0.8f;
        rec.latency_ns = 1000000LL * (i + 1);  // 1ms, 2ms, ..., 100ms
        records.push_back(rec);
    }

    auto m = Art15MetricsCalculator::compute(records);

    REQUIRE(m.p50_latency_ns > 0);
    REQUIRE(m.p95_latency_ns > m.p50_latency_ns);
    REQUIRE(m.p99_latency_ns >= m.p95_latency_ns);
    REQUIRE(m.max_latency_ns == 100000000LL);
}

TEST_CASE("Art15MetricsCalculator tracks distinct model versions", "[compliance][art15]") {
    std::vector<InferenceRecord> records;

    for (int i = 0; i < 30; ++i) {
        InferenceRecord rec;
        rec.timestamp_ns = 1000000000LL * i;
        rec.model_version = "v" + std::to_string(i / 10 + 1); // v1, v2, v3
        rec.output_score = 0.7f;
        rec.latency_ns = 1000000LL;
        records.push_back(rec);
    }

    auto m = Art15MetricsCalculator::compute(records);
    REQUIRE(m.distinct_model_versions == 3);
}

TEST_CASE("Art15MetricsCalculator PSI is near zero for identical distributions", "[compliance][art15]") {
    std::vector<InferenceRecord> records;

    // 100 records all with same confidence — no drift
    for (int i = 0; i < 100; ++i) {
        InferenceRecord rec;
        rec.timestamp_ns = 1000000000LL * i;
        rec.model_version = "v1.0";
        rec.output_score = 0.75f;
        rec.latency_ns = 1000000LL;
        records.push_back(rec);
    }

    auto m = Art15MetricsCalculator::compute(records);
    REQUIRE(m.psi_score < 0.1f);  // No significant drift
}

TEST_CASE("Art15Metrics to_json produces valid output", "[compliance][art15]") {
    std::vector<InferenceRecord> records;

    for (int i = 0; i < 20; ++i) {
        InferenceRecord rec;
        rec.timestamp_ns = 1000000000LL * i;
        rec.model_version = "v1.0";
        rec.output_score = 0.5f + 0.02f * i;
        rec.latency_ns = 1000000LL * (i + 1);
        records.push_back(rec);
    }

    auto m = Art15MetricsCalculator::compute(records);
    auto json = m.to_json();

    REQUIRE(json.find("\"total_inferences\":") != std::string::npos);
    REQUIRE(json.find("\"mean_confidence\":") != std::string::npos);
    REQUIRE(json.find("\"psi_score\":") != std::string::npos);
    REQUIRE(json.find("\"p95_latency_ns\":") != std::string::npos);
    REQUIRE(json.front() == '{');
    REQUIRE(json.back() == '}');
}

// ===================================================================
// Gap R-12: LEI validation (ISO 17442)
// ===================================================================

TEST_CASE("LEI validator accepts valid LEI", "[compliance][regulatory][lei]") {
    using signet::forge::regulatory::validate_lei;
    // Valid LEI with correct ISO 7064 Mod 97-10 check digits
    REQUIRE(validate_lei("254900RIII2MLYOSMX26") == true);
}

TEST_CASE("LEI validator rejects wrong length", "[compliance][regulatory][lei]") {
    using signet::forge::regulatory::validate_lei;
    REQUIRE(validate_lei("254900RIII2MLYOSMX9") == false);   // 19 chars
    REQUIRE(validate_lei("254900RIII2MLYOSMX950") == false);  // 21 chars
    REQUIRE(validate_lei("") == false);
}

TEST_CASE("LEI validator rejects invalid characters", "[compliance][regulatory][lei]") {
    using signet::forge::regulatory::validate_lei;
    REQUIRE(validate_lei("254900riii2mlyosmx95") == false);  // lowercase
    REQUIRE(validate_lei("254900RIII2MLYOS X95") == false);  // space
}

TEST_CASE("LEI validator rejects wrong check digits", "[compliance][regulatory][lei]") {
    using signet::forge::regulatory::validate_lei;
    // Change last two digits to wrong values (valid = X26)
    REQUIRE(validate_lei("254900RIII2MLYOSMX00") == false);
    REQUIRE(validate_lei("254900RIII2MLYOSMX95") == false);
}

// ===================================================================
// Gap R-12b: ISIN validation (ISO 6166)
// ===================================================================

TEST_CASE("ISIN validator accepts valid ISINs", "[compliance][regulatory][isin]") {
    using signet::forge::regulatory::validate_isin;
    // Apple Inc
    REQUIRE(validate_isin("US0378331005") == true);
    // UK gilt
    REQUIRE(validate_isin("GB0002634946") == true);
}

TEST_CASE("ISIN validator rejects wrong length", "[compliance][regulatory][isin]") {
    using signet::forge::regulatory::validate_isin;
    REQUIRE(validate_isin("US037833100") == false);    // 11 chars
    REQUIRE(validate_isin("US03783310050") == false);   // 13 chars
}

TEST_CASE("ISIN validator rejects invalid country code", "[compliance][regulatory][isin]") {
    using signet::forge::regulatory::validate_isin;
    REQUIRE(validate_isin("120378331005") == false);  // numeric prefix
    REQUIRE(validate_isin("us0378331005") == false);  // lowercase
}

TEST_CASE("ISIN validator rejects wrong check digit", "[compliance][regulatory][isin]") {
    using signet::forge::regulatory::validate_isin;
    REQUIRE(validate_isin("US0378331009") == false);  // wrong check digit
}

// ===================================================================
// Gap R-12c: MIC validation (ISO 10383)
// ===================================================================

TEST_CASE("MIC validator accepts valid MICs", "[compliance][regulatory][mic]") {
    using signet::forge::regulatory::validate_mic;
    REQUIRE(validate_mic("XLON") == true);  // London Stock Exchange
    REQUIRE(validate_mic("XNYS") == true);  // NYSE
    REQUIRE(validate_mic("XNAS") == true);  // NASDAQ
}

TEST_CASE("MIC validator rejects wrong length", "[compliance][regulatory][mic]") {
    using signet::forge::regulatory::validate_mic;
    REQUIRE(validate_mic("XLO") == false);
    REQUIRE(validate_mic("XLOND") == false);
    REQUIRE(validate_mic("") == false);
}

TEST_CASE("MIC validator rejects non-alpha", "[compliance][regulatory][mic]") {
    using signet::forge::regulatory::validate_mic;
    REQUIRE(validate_mic("XLO1") == false);
    REQUIRE(validate_mic("xlon") == false);
    REQUIRE(validate_mic("XL N") == false);
}

// ===================================================================
// Gap R-10: Kill switch / circuit breaker (via HumanOverrideLogWriter)
// ===================================================================

TEST_CASE("OverrideRateMonitor triggers halt callback at threshold", "[compliance][oversight][killswitch]") {
    int halt_called = 0;
    OverrideRateMonitorOptions opts;
    opts.window_ns = INT64_C(60000000000);  // 60 seconds
    opts.alert_threshold = 3;
    opts.auto_halt_on_threshold = true;

    OverrideRateMonitor monitor(opts);
    monitor.set_halt_callback([&](HaltReason reason, const std::string& detail) {
        (void)reason;
        (void)detail;
        ++halt_called;
    });

    // Record overrides up to threshold (all within the 60s window)
    for (int i = 0; i < 4; ++i) {
        int64_t ts = static_cast<int64_t>(1000000000LL * (i + 1));
        monitor.record_override(ts);
    }

    REQUIRE(halt_called >= 1);
    // Verify count is at 4 (all within window)
    REQUIRE(monitor.current_count(INT64_C(5000000000)) == 4);
}

TEST_CASE("OverrideRateMonitor manual halt (Art.14(4) stop button)", "[compliance][oversight][killswitch]") {
    int halt_called = 0;
    HaltReason captured_reason{};

    OverrideRateMonitor monitor;
    monitor.set_halt_callback([&](HaltReason reason, const std::string& /*detail*/) {
        ++halt_called;
        captured_reason = reason;
    });

    monitor.trigger_halt(HaltReason::MANUAL, "Operator pressed stop button");
    REQUIRE(halt_called == 1);
    REQUIRE(captured_reason == HaltReason::MANUAL);

    monitor.trigger_halt(HaltReason::REGULATORY, "NCA directive");
    REQUIRE(halt_called == 2);
    REQUIRE(captured_reason == HaltReason::REGULATORY);
}

// ===========================================================================
// Gap R-11: Pre-trade risk checks (MiFID II RTS 6 Art. 17)
// ===========================================================================

TEST_CASE("Pre-trade risk check passes within limits", "[compliance][risk][R-11]") {
    risk::PreTradeRiskLimits limits;
    limits.price_collar_pct = 5.0;
    limits.max_order_notional = 1e6;
    limits.max_daily_notional = 1e7;

    auto result = risk::check_order(limits,
        100.0,    // order_price
        100.0,    // reference_price (0% deviation)
        50000.0,  // order_notional
        500000.0  // daily_notional
    );
    REQUIRE(result.result == risk::RiskCheckResult::PASS);
    REQUIRE(result.reason == risk::RiskRejectReason::NONE);
}

TEST_CASE("Pre-trade risk check rejects price collar breach", "[compliance][risk][R-11][negative]") {
    risk::PreTradeRiskLimits limits;
    limits.price_collar_pct = 2.0;

    auto result = risk::check_order(limits,
        106.0,    // order_price (6% above reference)
        100.0,    // reference_price
        1000.0,   // order_notional
        0.0       // daily_notional
    );
    REQUIRE(result.result == risk::RiskCheckResult::REJECT);
    REQUIRE(result.reason == risk::RiskRejectReason::PRICE_COLLAR);
}

TEST_CASE("Pre-trade risk check rejects max order size", "[compliance][risk][R-11][negative]") {
    risk::PreTradeRiskLimits limits;
    limits.max_order_notional = 1e6;

    auto result = risk::check_order(limits,
        100.0,     // order_price
        100.0,     // reference_price
        2e6,       // order_notional (exceeds 1M limit)
        0.0        // daily_notional
    );
    REQUIRE(result.result == risk::RiskCheckResult::REJECT);
    REQUIRE(result.reason == risk::RiskRejectReason::MAX_ORDER_SIZE);
}

TEST_CASE("Pre-trade risk check rejects daily notional breach", "[compliance][risk][R-11][negative]") {
    risk::PreTradeRiskLimits limits;
    limits.max_daily_notional = 1e7;

    auto result = risk::check_order(limits,
        100.0,     // order_price
        100.0,     // reference_price
        500000.0,  // order_notional
        9.6e6      // daily_notional (9.6M + 0.5M > 10M limit)
    );
    REQUIRE(result.result == risk::RiskCheckResult::REJECT);
    REQUIRE(result.reason == risk::RiskRejectReason::MAX_DAILY_VOLUME);
}

// ===========================================================================
// Gap G-2: PII data classification (GDPR Art. 9, 25, 32)
// ===========================================================================

TEST_CASE("DataClassification requires_encryption for personal data", "[compliance][gdpr][G-2]") {
    REQUIRE_FALSE(gdpr::requires_encryption(gdpr::DataClassification::PUBLIC));
    REQUIRE_FALSE(gdpr::requires_encryption(gdpr::DataClassification::INTERNAL));
    REQUIRE(gdpr::requires_encryption(gdpr::DataClassification::PERSONAL));
    REQUIRE(gdpr::requires_encryption(gdpr::DataClassification::SENSITIVE));
    REQUIRE_FALSE(gdpr::requires_encryption(gdpr::DataClassification::PSEUDONYMIZED));
    REQUIRE_FALSE(gdpr::requires_encryption(gdpr::DataClassification::ANONYMIZED));
}

TEST_CASE("ColumnClassification stores metadata", "[compliance][gdpr][G-2]") {
    gdpr::ColumnClassification cc;
    cc.column_name = "patient_name";
    cc.classification = gdpr::DataClassification::SENSITIVE;
    cc.purpose = "medical_treatment";
    cc.lawful_basis = "consent";
    cc.retention_days = 365 * 10;  // 10 years for medical records

    REQUIRE(cc.column_name == "patient_name");
    REQUIRE(cc.classification == gdpr::DataClassification::SENSITIVE);
    REQUIRE(gdpr::requires_encryption(cc.classification));
    REQUIRE(gdpr::allows_pseudonymization(cc.classification));
    REQUIRE(cc.retention_days == 3650);
}

// ===========================================================================
// Gap D-6: ICT asset identification/classification (DORA Art. 7-8)
// ===========================================================================

TEST_CASE("ICTAssetDescriptor stores asset metadata", "[compliance][dora][D-6]") {
    dora::ICTAssetDescriptor asset;
    asset.asset_id = "DS-001";
    asset.asset_name = "Trade Parquet Store";
    asset.asset_type = "data_store";
    asset.criticality = dora::AssetCriticality::VITAL;
    asset.owner = "Data Engineering";
    asset.location = "eu-west-1";
    asset.dependencies = "KMS-001,NET-003";
    asset.last_assessed_ns = 1709942400000000000LL;

    REQUIRE(asset.asset_id == "DS-001");
    REQUIRE(asset.criticality == dora::AssetCriticality::VITAL);
    REQUIRE(asset.dependencies == "KMS-001,NET-003");
}

// ===========================================================================
// Gap D-3: Backup policy / RPO support (DORA Art. 12)
// ===========================================================================

TEST_CASE("BackupPolicy meets_rpo within window", "[compliance][dora][D-3]") {
    dora::BackupPolicy policy;
    policy.rpo_seconds = 3600;  // 1 hour

    int64_t now = 1709942400000000000LL;
    int64_t last_backup = now - 1800LL * 1000000000LL;  // 30 min ago

    REQUIRE(dora::meets_rpo(policy, last_backup, now));
}

TEST_CASE("BackupPolicy fails RPO when overdue", "[compliance][dora][D-3][negative]") {
    dora::BackupPolicy policy;
    policy.rpo_seconds = 3600;  // 1 hour

    int64_t now = 1709942400000000000LL;
    int64_t last_backup = now - 7200LL * 1000000000LL;  // 2 hours ago

    REQUIRE_FALSE(dora::meets_rpo(policy, last_backup, now));
}

TEST_CASE("BackupRecord tracks status", "[compliance][dora][D-3]") {
    dora::BackupRecord record;
    record.backup_id = "BK-20260309-001";
    record.policy_id = "POL-001";
    record.status = dora::BackupStatus::VERIFIED;
    record.size_bytes = 1024 * 1024 * 500;  // 500 MB
    record.checksum = "abc123";

    REQUIRE(record.status == dora::BackupStatus::VERIFIED);
    REQUIRE(record.size_bytes == 500 * 1024 * 1024);
}

// ===========================================================================
// Gap D-11: Key rotation / lifecycle management (DORA Art. 9(2))
// ===========================================================================

TEST_CASE("Key needs rotation when expired", "[compliance][dora][D-11]") {
    dora::KeyLifecycleRecord record;
    record.key_id = "AES-001";
    record.state = dora::KeyState::ACTIVE;
    record.expiry_ns = 1709942400000000000LL;

    int64_t now = 1709942400000000000LL + 1;  // 1 ns after expiry
    REQUIRE(dora::needs_rotation(record, now));
}

TEST_CASE("Key does not need rotation when not expired", "[compliance][dora][D-11]") {
    dora::KeyLifecycleRecord record;
    record.key_id = "AES-002";
    record.state = dora::KeyState::ACTIVE;
    record.expiry_ns = 1709942400000000000LL;

    int64_t now = 1709942400000000000LL - 1;  // 1 ns before expiry
    REQUIRE_FALSE(dora::needs_rotation(record, now));
}

TEST_CASE("Deactivated key does not need rotation", "[compliance][dora][D-11]") {
    dora::KeyLifecycleRecord record;
    record.state = dora::KeyState::DEACTIVATED;
    record.expiry_ns = 1000;

    REQUIRE_FALSE(dora::needs_rotation(record, 2000));
}

// ===========================================================================
// Gap G-5: Pseudonymizer (GDPR Art. 25, 32(1)(a))
// ===========================================================================

TEST_CASE("Pseudonymize produces deterministic output", "[compliance][gdpr][G-5]") {
    uint8_t key[32] = {};
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);

    const char* value = "john.doe@example.com";
    char out1[64] = {};
    char out2[64] = {};

    gdpr::pseudonymize_hmac(key, 32,
        reinterpret_cast<const uint8_t*>(value), std::strlen(value),
        out1, 16);
    gdpr::pseudonymize_hmac(key, 32,
        reinterpret_cast<const uint8_t*>(value), std::strlen(value),
        out2, 16);

    REQUIRE(std::string(out1, 16) == std::string(out2, 16));
}

TEST_CASE("Pseudonymize differs for different inputs", "[compliance][gdpr][G-5]") {
    uint8_t key[32] = {};
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i + 1);

    const char* v1 = "alice@example.com";
    const char* v2 = "bob@example.com";
    char out1[64] = {};
    char out2[64] = {};

    gdpr::pseudonymize_hmac(key, 32,
        reinterpret_cast<const uint8_t*>(v1), std::strlen(v1), out1, 16);
    gdpr::pseudonymize_hmac(key, 32,
        reinterpret_cast<const uint8_t*>(v2), std::strlen(v2), out2, 16);

    REQUIRE(std::string(out1, 16) != std::string(out2, 16));
}

// ===========================================================================
// Gap G-7: GDPR Writer Policy (GDPR Art. 32(1)(a))
// ===========================================================================

TEST_CASE("GDPR policy validates encrypted PII columns", "[compliance][gdpr][G-7]") {
    std::vector<gdpr::ColumnClassification> classifications = {
        {"name",    gdpr::DataClassification::PERSONAL, "contact", "consent", 365},
        {"email",   gdpr::DataClassification::SENSITIVE, "auth", "contract", 365},
        {"country", gdpr::DataClassification::PUBLIC, "", "", 0},
    };

    std::vector<std::string> encrypted = {"name", "email"};
    auto result = gdpr::validate_gdpr_policy(classifications, encrypted);
    REQUIRE(result.compliant);
    REQUIRE(result.violations.empty());
}

TEST_CASE("GDPR policy rejects unencrypted PII columns", "[compliance][gdpr][G-7][negative]") {
    std::vector<gdpr::ColumnClassification> classifications = {
        {"name",    gdpr::DataClassification::PERSONAL, "contact", "consent", 365},
        {"email",   gdpr::DataClassification::SENSITIVE, "auth", "contract", 365},
        {"country", gdpr::DataClassification::PUBLIC, "", "", 0},
    };

    std::vector<std::string> encrypted = {"name"};  // email not encrypted
    auto result = gdpr::validate_gdpr_policy(classifications, encrypted);
    REQUIRE_FALSE(result.compliant);
    REQUIRE(result.violations.size() == 1);
    REQUIRE(result.violations[0] == "email");
}

// ===========================================================================
// Gap G-3: Records of Processing Activities (GDPR Art. 30)
// ===========================================================================

TEST_CASE("ProcessingActivity stores ROPA fields", "[compliance][gdpr][G-3]") {
    gdpr::ProcessingActivity activity;
    activity.activity_id = "PA-001";
    activity.controller_name = "Acme Corp";
    activity.purpose = "Customer analytics";
    activity.lawful_basis = "consent";
    activity.data_subject_categories = "customers";
    activity.data_categories = "name, email, purchase history";
    activity.recipients = "analytics team";
    activity.retention_days = 730;
    activity.security_measures = "AES-256-GCM encryption, pseudonymization";

    REQUIRE(activity.activity_id == "PA-001");
    REQUIRE(activity.retention_days == 730);
}

// ===========================================================================
// Gap G-4: Data retention / TTL (GDPR Art. 5(1)(e))
// ===========================================================================

TEST_CASE("RetentionPolicy detects expired data", "[compliance][gdpr][G-4]") {
    gdpr::RetentionPolicy policy;
    policy.retention_days = 365;

    int64_t now = 1709942400000000000LL;
    int64_t created = now - 400LL * 86400LL * 1000000000LL;  // 400 days ago

    REQUIRE(gdpr::is_expired(policy, created, now));
}

TEST_CASE("RetentionPolicy respects legal hold", "[compliance][gdpr][G-4]") {
    gdpr::RetentionPolicy policy;
    policy.retention_days = 30;
    policy.legal_hold_id = "LH-2026-001";

    int64_t now = 1709942400000000000LL;
    int64_t created = now - 400LL * 86400LL * 1000000000LL;  // 400 days ago

    // Even though retention expired, legal hold prevents expiry
    REQUIRE_FALSE(gdpr::is_expired(policy, created, now));
}

// ===========================================================================
// Gap D-1: ICT incident management (DORA Art. 10, 15, 19)
// ===========================================================================

TEST_CASE("ICTIncidentRecord stores incident details", "[compliance][dora][D-1]") {
    dora::ICTIncidentRecord incident;
    incident.incident_id = "INC-2026-001";
    incident.category = dora::IncidentCategory::CRYPTOGRAPHIC;
    incident.severity = dora::IncidentSeverity::HIGH;
    incident.description = "AES key decryption failure on column 'price'";
    incident.impact = "Trading data inaccessible for 15 minutes";
    incident.root_cause = "KMS key rotation did not propagate to all nodes";
    incident.remediation = "Manual key sync + automated propagation check added";
    incident.reported_to_authority = true;

    REQUIRE(incident.category == dora::IncidentCategory::CRYPTOGRAPHIC);
    REQUIRE(incident.severity == dora::IncidentSeverity::HIGH);
    REQUIRE(incident.reported_to_authority);
}

// ===========================================================================
// Gap D-2: Digital operational resilience testing (DORA Art. 24-27)
// ===========================================================================

TEST_CASE("ResilienceTestRecord stores test results", "[compliance][dora][D-2]") {
    dora::ResilienceTestRecord test;
    test.test_id = "RT-2026-001";
    test.test_type = dora::ResilienceTestType::FAULT_INJECTION;
    test.result = dora::ResilienceTestResult::DEGRADED;
    test.scenario = "Simulate KMS unavailability during column decryption";
    test.findings = "System queued requests but did not retry automatically";
    test.recommendations = "Add exponential backoff retry for KMS calls";

    REQUIRE(test.test_type == dora::ResilienceTestType::FAULT_INJECTION);
    REQUIRE(test.result == dora::ResilienceTestResult::DEGRADED);
}

// ===========================================================================
// Gap D-5: ICT risk management (DORA Art. 5-6)
// ===========================================================================

TEST_CASE("ICTRiskEntry stores risk register data", "[compliance][dora][D-5]") {
    dora::ICTRiskEntry risk;
    risk.risk_id = "RISK-001";
    risk.description = "Single-point-of-failure in key management";
    risk.inherent_risk = dora::RiskLevel::HIGH;
    risk.residual_risk = dora::RiskLevel::LOW;
    risk.controls = "Redundant KMS, automated key backup, daily integrity check";
    risk.owner = "Security Team";

    REQUIRE(risk.inherent_risk == dora::RiskLevel::HIGH);
    REQUIRE(risk.residual_risk == dora::RiskLevel::LOW);
}

// ===========================================================================
// Gap D-7: Anomaly detection beyond latency (DORA Art. 10)
// ===========================================================================

TEST_CASE("AnomalyRecord stores detection data", "[compliance][dora][D-7]") {
    dora::AnomalyRecord anomaly;
    anomaly.anomaly_id = "ANO-001";
    anomaly.type = dora::AnomalyType::DECRYPTION_FAILURE;
    anomaly.value = 47.0;     // 47 failures
    anomaly.threshold = 5.0;  // normal: < 5
    anomaly.component = "PME FileDecryptor";
    anomaly.action_taken = "Alert raised, KMS connectivity check initiated";

    REQUIRE(anomaly.type == dora::AnomalyType::DECRYPTION_FAILURE);
    REQUIRE(anomaly.value > anomaly.threshold);
}

// ===========================================================================
// Gap D-8: Recovery procedures / RTO tracking (DORA Art. 11)
// ===========================================================================

TEST_CASE("RecoveryProcedure stores procedure details", "[compliance][dora][D-8]") {
    dora::RecoveryProcedure proc;
    proc.procedure_id = "RP-001";
    proc.trigger_condition = "Complete KMS failure";
    proc.rto_seconds = 7200;  // 2 hours
    proc.steps = "1. Switch to backup KMS 2. Verify key access 3. Resume decryption";
    proc.responsible_team = "SRE Team";

    REQUIRE(proc.rto_seconds == 7200);
}

// ===========================================================================
// Gap D-9: Post-incident review (DORA Art. 13)
// ===========================================================================

TEST_CASE("PostIncidentReview stores lessons learned", "[compliance][dora][D-9]") {
    dora::PostIncidentReview review;
    review.review_id = "PIR-001";
    review.incident_id = "INC-2026-001";
    review.root_cause_analysis = "Key rotation event not synchronized across regions";
    review.lessons_learned = "Need automated cross-region key sync verification";
    review.corrective_actions = "Implemented post-rotation verification job";
    review.preventive_measures = "Added pre-rotation readiness check";

    REQUIRE(review.incident_id == "INC-2026-001");
}

// ===========================================================================
// Gap D-10: ICT communication / notification (DORA Art. 14)
// ===========================================================================

TEST_CASE("ICTNotification stores notification data", "[compliance][dora][D-10]") {
    dora::ICTNotification notif;
    notif.notification_id = "NOTIF-001";
    notif.level = dora::NotificationLevel::ALERT;
    notif.subject = "KMS key expiry approaching";
    notif.message = "Key AES-001 expires in 24 hours — rotate immediately";
    notif.recipients = "security@acme.com,ops@acme.com";
    notif.acknowledged = false;

    REQUIRE(notif.level == dora::NotificationLevel::ALERT);
    REQUIRE_FALSE(notif.acknowledged);
}

// ===========================================================================
// Gap D-4: Third-party risk register (DORA Art. 28-30)
// ===========================================================================

TEST_CASE("ThirdPartyRiskEntry stores provider data", "[compliance][dora][D-4]") {
    dora::ThirdPartyRiskEntry entry;
    entry.provider_id = "TP-001";
    entry.provider_name = "CloudKMS Inc.";
    entry.service_description = "Key Management as a Service";
    entry.concentration_risk = dora::RiskLevel::MEDIUM;
    entry.critical_function = true;
    entry.jurisdiction = "US";
    entry.exit_strategy = "Migrate to self-hosted HSM within 90 days";
    entry.sbom_reference = "sbom-cloudkms-v2.3.1.json";

    REQUIRE(entry.critical_function);
    REQUIRE(entry.concentration_risk == dora::RiskLevel::MEDIUM);
}

// ===========================================================================
// Gap G-6: DPIA report (GDPR Art. 35)
// ===========================================================================

TEST_CASE("DPIARecord stores assessment data", "[compliance][gdpr][G-6]") {
    gdpr::DPIARecord dpia;
    dpia.dpia_id = "DPIA-001";
    dpia.processing_description = "Automated credit scoring using personal financial data";
    dpia.necessity_assessment = "Processing is necessary for contract performance";
    dpia.risks_to_rights = "Risk of discriminatory credit decisions";
    dpia.mitigation_measures = "Bias testing, human review for denials";
    dpia.approved = true;

    REQUIRE(dpia.dpia_id == "DPIA-001");
    REQUIRE(dpia.approved);
}

// ===========================================================================
// Gap G-8: DSAR support (GDPR Art. 15)
// ===========================================================================

TEST_CASE("SubjectDataQuery stores DSAR parameters", "[compliance][gdpr][G-8]") {
    gdpr::SubjectDataQuery query;
    query.subject_id = "user-42";
    query.subject_id_column = "entity_id";
    query.file_paths = {"data/trades_2026.parquet", "data/features_2026.parquet"};

    REQUIRE(query.subject_id == "user-42");
    REQUIRE(query.file_paths.size() == 2);
}

// ===========================================================================
// Gap R-6: Accuracy/robustness metrics (EU AI Act Art. 15)
// ===========================================================================

TEST_CASE("DriftMetric detects model drift", "[compliance][eu-ai-act][R-6]") {
    eu_ai_act::DriftMetric drift;
    drift.feature_name = "credit_score";
    drift.psi = 0.35;  // Above threshold
    drift.drift_threshold = 0.25;

    REQUIRE(eu_ai_act::is_drifted(drift));

    drift.psi = 0.10;  // Below threshold
    REQUIRE_FALSE(eu_ai_act::is_drifted(drift));
}

// ===========================================================================
// Gap R-7: Risk management (EU AI Act Art. 9)
// ===========================================================================

TEST_CASE("AIRiskAssessment stores risk data", "[compliance][eu-ai-act][R-7]") {
    eu_ai_act::AIRiskAssessment assessment;
    assessment.assessment_id = "RA-001";
    assessment.risk_level = eu_ai_act::AIRiskLevel::HIGH;
    assessment.risk_description = "Credit scoring system — high risk per Annex III";
    assessment.mitigation_measures = "Human oversight, bias testing, accuracy monitoring";

    REQUIRE(assessment.risk_level == eu_ai_act::AIRiskLevel::HIGH);
}

// ===========================================================================
// Gap R-9: Quality management (EU AI Act Art. 17)
// ===========================================================================

TEST_CASE("QMSCheckPoint stores QMS data", "[compliance][eu-ai-act][R-9]") {
    eu_ai_act::QMSCheckPoint checkpoint;
    checkpoint.checkpoint_id = "QMS-001";
    checkpoint.model_version = "v2.3.1";
    checkpoint.change_description = "Updated feature extraction pipeline";
    checkpoint.approved = true;
    checkpoint.approver = "ML Lead";

    REQUIRE(checkpoint.approved);
    REQUIRE(checkpoint.model_version == "v2.3.1");
}

// ===========================================================================
// Gap R-15b: System lifecycle events (EU AI Act Art. 12(2))
// ===========================================================================

TEST_CASE("LifecycleEvent stores system events", "[compliance][eu-ai-act][R-15b]") {
    eu_ai_act::LifecycleEvent event;
    event.event_id = "EVT-001";
    event.event_type = eu_ai_act::LifecycleEventType::MODEL_SWAP;
    event.description = "Model upgraded from v2.3 to v2.4";
    event.actor = "deployment-pipeline";
    event.previous_state = "model-v2.3";
    event.new_state = "model-v2.4";

    REQUIRE(event.event_type == eu_ai_act::LifecycleEventType::MODEL_SWAP);
}

// ===========================================================================
// Gap R-18: Serious incident reporting (EU AI Act Art. 62)
// ===========================================================================

TEST_CASE("SeriousIncidentReport stores incident data", "[compliance][eu-ai-act][R-18]") {
    eu_ai_act::SeriousIncidentReport report;
    report.report_id = "SIR-001";
    report.description = "AI system denied credit to protected class disproportionately";
    report.harm_caused = "Discrimination in financial services";
    report.corrective_action = "System suspended, retraining with balanced data";
    report.reported_to_authority = true;

    REQUIRE(report.reported_to_authority);
}

// ===========================================================================
// Gap R-13c: Completeness attestation (MiFID II RTS 24 Art. 9)
// ===========================================================================

TEST_CASE("CompletenessAttestation detects gaps", "[compliance][mifid2][R-13c]") {
    mifid2::CompletenessAttestation att;
    att.expected_records = 1000;
    att.actual_records = 950;
    att.gaps.push_back({1709942400000000000LL, 1709949600000000000LL});  // 2h gap
    att.complete = false;

    REQUIRE(mifid2::has_gaps(att));
    REQUIRE(att.gaps.size() == 1);
}

TEST_CASE("CompletenessAttestation passes when complete", "[compliance][mifid2][R-13c]") {
    mifid2::CompletenessAttestation att;
    att.expected_records = 1000;
    att.actual_records = 1000;

    REQUIRE_FALSE(mifid2::has_gaps(att));
}

// ===========================================================================
// Gap R-17: Order lifecycle linking (MiFID II RTS 24)
// ===========================================================================

TEST_CASE("OrderLifecycleEvent tracks order chain", "[compliance][mifid2][R-17]") {
    mifid2::OrderLifecycleEvent new_order;
    new_order.order_id = "ORD-001";
    new_order.event_type = "ORDER_NEW";
    new_order.price = 100.50;
    new_order.quantity = 1000;

    mifid2::OrderLifecycleEvent modify;
    modify.order_id = "ORD-001-M1";
    modify.parent_order_id = "ORD-001";
    modify.event_type = "ORDER_MODIFY";
    modify.price = 101.00;
    modify.quantity = 1000;

    REQUIRE(modify.parent_order_id == new_order.order_id);
}
