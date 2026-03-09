// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// bench_phase8_compliance.cpp — MiFID II + EU AI Act report generation benchmarks (6 cases)
//
// CR1: MiFID2 JSON       — MiFID2Reporter::generate() in JSON format
// CR2: MiFID2 NDJSON     — MiFID2Reporter::generate() in NDJSON format
// CR3: MiFID2 CSV        — MiFID2Reporter::generate() in CSV format
// CR4: EU AI Act Art.12  — EUAIActReporter::generate_article12() (operational log)
// CR5: EU AI Act Art.13  — EUAIActReporter::generate_article13() (transparency)
// CR6: EU AI Act Art.19  — EUAIActReporter::generate_article19() (conformity)
//
// ALL cases require SIGNET_ENABLE_COMMERCIAL (BSL 1.1 tier).
//
// Build: cmake --preset benchmarks && cmake --build build-benchmarks
// Run:   ./build-benchmarks/signet_ebench "[bench-enterprise][compliance]"

#include "common.hpp"

#if SIGNET_ENABLE_COMMERCIAL

#include "signet/ai/decision_log.hpp"
#include "signet/ai/inference_log.hpp"
#include "signet/ai/compliance/mifid2_reporter.hpp"
#include "signet/ai/compliance/eu_ai_act_reporter.hpp"
#include "signet/ai/compliance/compliance_types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ===========================================================================
// Fixture: write 10K decision + 10K inference records for all 6 cases
// ===========================================================================
namespace {

inline int64_t bench_now_ns() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<nanoseconds>(
            system_clock::now().time_since_epoch()).count());
}

const std::array<std::string, 6> kSymbols = {
    "BTCUSDT", "ETHUSDT", "SOLUSDT", "BNBUSDT", "XRPUSDT", "ADAUSDT"
};

struct ComplianceFixtures {
    std::vector<std::string> decision_files;
    std::vector<std::string> inference_files;
    bench::TempDir dir{"ebench_compliance_"};

    void build() {
        // --- Write 10K decision records ---
        {
            DecisionLogWriter writer(dir.path, "bench-cr-dec", 100000);
            int64_t base_ts = bench_now_ns();
            for (int i = 0; i < 10'000; ++i) {
                DecisionRecord rec;
                rec.timestamp_ns  = base_ts + static_cast<int64_t>(i) * 1'000'000;
                rec.strategy_id   = static_cast<int32_t>(i % 5);
                rec.model_version = "v2.1.0";
                rec.decision_type = (i % 4 == 0) ? DecisionType::ORDER_NEW
                                                  : DecisionType::NO_ACTION;
                rec.input_features = {1.0f, 2.0f, 3.0f, 4.0f};
                rec.model_output  = 0.75f + static_cast<float>(i % 100) * 0.001f;
                rec.confidence    = 0.9f;
                rec.risk_result   = (i % 20 == 0) ? RiskGateResult::REJECTED
                                                   : RiskGateResult::PASSED;
                rec.order_id      = "ORD-" + std::to_string(i);
                rec.symbol        = std::string(kSymbols[i % kSymbols.size()]);
                rec.price         = 45000.0 + static_cast<double>(i % 1000) * 0.1;
                rec.quantity      = 0.01;
                rec.venue         = "BINANCE";
                (void)writer.log(rec);
            }
            (void)writer.flush();
            decision_files.push_back(writer.current_file_path());
            (void)writer.close();
        }

        // --- Write 10K inference records ---
        {
            InferenceLogWriter writer(dir.path, "bench-cr-inf", 100000);
            int64_t base_ts = bench_now_ns();
            for (int i = 0; i < 10'000; ++i) {
                InferenceRecord rec;
                rec.timestamp_ns    = base_ts + static_cast<int64_t>(i) * 1'000'000;
                rec.model_id        = "trend_classifier";
                rec.model_version   = "v1.3.0";
                rec.inference_type  = InferenceType::CLASSIFICATION;
                rec.input_embedding = {0.1f, 0.2f, 0.3f, 0.4f};
                rec.output_score    = 0.85f + static_cast<float>(i % 50) * 0.001f;
                rec.latency_ns      = 500'000 + static_cast<int64_t>(i % 1000) * 100;
                rec.batch_size      = 1;
                (void)writer.log(rec);
            }
            (void)writer.flush();
            inference_files.push_back(writer.current_file_path());
            (void)writer.close();
        }
    }
};

ComplianceFixtures& fixtures() {
    static ComplianceFixtures f;
    static bool built = false;
    if (!built) {
        f.build();
        built = true;
    }
    return f;
}

} // anonymous namespace

// ===========================================================================
// CR1: MiFID2 JSON
// ===========================================================================
TEST_CASE("CR1: MiFID2 JSON", "[bench-enterprise][compliance]") {
    auto& fx = fixtures();

    BENCHMARK("CR1: MiFID2 JSON") {
        ReportOptions opts;
        opts.format  = ReportFormat::JSON;
        opts.firm_id = "BENCH-FIRM-001";
        auto report = MiFID2Reporter::generate(fx.decision_files, opts);
        return report->content.size();
    };
}

// ===========================================================================
// CR2: MiFID2 NDJSON
// ===========================================================================
TEST_CASE("CR2: MiFID2 NDJSON", "[bench-enterprise][compliance]") {
    auto& fx = fixtures();

    BENCHMARK("CR2: MiFID2 NDJSON") {
        ReportOptions opts;
        opts.format  = ReportFormat::NDJSON;
        opts.firm_id = "BENCH-FIRM-001";
        auto report = MiFID2Reporter::generate(fx.decision_files, opts);
        return report->content.size();
    };
}

// ===========================================================================
// CR3: MiFID2 CSV
// ===========================================================================
TEST_CASE("CR3: MiFID2 CSV", "[bench-enterprise][compliance]") {
    auto& fx = fixtures();

    BENCHMARK("CR3: MiFID2 CSV") {
        ReportOptions opts;
        opts.format  = ReportFormat::CSV;
        opts.firm_id = "BENCH-FIRM-001";
        auto report = MiFID2Reporter::generate(fx.decision_files, opts);
        return report->content.size();
    };
}

// ===========================================================================
// CR4: EU AI Act Article 12 — Operational logging
// ===========================================================================
TEST_CASE("CR4: EU AI Act Art.12", "[bench-enterprise][compliance]") {
    auto& fx = fixtures();

    BENCHMARK("CR4: EU AI Act Art.12") {
        ReportOptions opts;
        opts.format = ReportFormat::JSON;
        auto report = EUAIActReporter::generate_article12(fx.inference_files, opts);
        return report->content.size();
    };
}

// ===========================================================================
// CR5: EU AI Act Article 13 — Transparency disclosure
// ===========================================================================
TEST_CASE("CR5: EU AI Act Art.13", "[bench-enterprise][compliance]") {
    auto& fx = fixtures();

    BENCHMARK("CR5: EU AI Act Art.13") {
        ReportOptions opts;
        opts.format = ReportFormat::JSON;
        auto report = EUAIActReporter::generate_article13(fx.inference_files, opts);
        return report->content.size();
    };
}

// ===========================================================================
// CR6: EU AI Act Article 19 — Conformity assessment (both logs)
// ===========================================================================
TEST_CASE("CR6: EU AI Act Art.19", "[bench-enterprise][compliance]") {
    auto& fx = fixtures();

    BENCHMARK("CR6: EU AI Act Art.19") {
        ReportOptions opts;
        opts.format = ReportFormat::JSON;
        // Fixture writes decision + inference logs with independent chain IDs;
        // Art.19 cross-chain validation would reject the mismatch, so skip it.
        opts.verify_chain = false;
        auto report = EUAIActReporter::generate_article19(
            fx.decision_files, fx.inference_files, opts);
        return report->content.size();
    };
}

#endif // SIGNET_ENABLE_COMMERCIAL
