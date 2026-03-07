// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// bench_phase7_ai.cpp — AI-native benchmarks (5 cases)
//
// AI1: DecisionLog 10K records      — BSL 1.1 commercial gate
// AI2: InferenceLog 10K records     — BSL 1.1 commercial gate
// AI3: verify_chain                 — BSL 1.1 commercial gate
// AI4: column_view 1M doubles       — zero-copy tensor view (Apache 2.0)
// AI5: EventBus 4P4C 100K           — MPMC throughput (Apache 2.0)
//
// Build: cmake --preset benchmarks && cmake --build build-benchmarks
// Run:   ./build-benchmarks/signet_ebench "[bench-enterprise][ai]"

#include "common.hpp"
#include "signet/ai/column_batch.hpp"
#include "signet/ai/event_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <atomic>
#include <thread>
#include <vector>

using namespace signet::forge;

// ===========================================================================
// AI1–AI3: Commercial-gated decision/inference log + chain verification
// ===========================================================================

#if SIGNET_ENABLE_COMMERCIAL

#include "signet/ai/decision_log.hpp"
#include "signet/ai/inference_log.hpp"
#include "signet/ai/audit_chain.hpp"

namespace {

// Timestamp helper (avoids clash with signet::forge::now_ns)
inline int64_t bench_now_ns() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<nanoseconds>(
            system_clock::now().time_since_epoch()).count());
}

const std::array<std::string, 6> kSymbols = {
    "BTCUSDT", "ETHUSDT", "SOLUSDT", "BNBUSDT", "XRPUSDT", "ADAUSDT"
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// AI1: DecisionLogWriter — 10K trading decision records
// ---------------------------------------------------------------------------
TEST_CASE("AI1: DecisionLog 10K records", "[bench-enterprise][ai]") {
    BENCHMARK("AI1: DecisionLog 10K records") {
        bench::TempDir tmp("ebench_ai1_");

        DecisionLogWriter writer(tmp.path, "bench-ai1", 100000);

        int64_t base_ts = bench_now_ns();
        for (int i = 0; i < 10'000; ++i) {
            DecisionRecord rec;
            rec.timestamp_ns  = base_ts + static_cast<int64_t>(i) * 1'000'000;
            rec.strategy_id   = static_cast<int32_t>(i % 5);
            rec.model_version = "v2.1.0";
            rec.decision_type = (i % 3 == 0) ? DecisionType::ORDER_NEW
                                              : DecisionType::NO_ACTION;
            rec.input_features = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
            rec.model_output  = 0.75f + static_cast<float>(i % 100) * 0.001f;
            rec.confidence    = 0.9f;
            rec.risk_result   = RiskGateResult::PASSED;
            rec.order_id      = "ORD-" + std::to_string(i);
            rec.symbol        = std::string(kSymbols[i % kSymbols.size()]);
            rec.price         = 45000.0 + static_cast<double>(i % 1000) * 0.1;
            rec.quantity      = 0.01 + static_cast<double>(i % 100) * 0.001;
            rec.venue         = "BINANCE";

            (void)writer.log(rec);
        }

        (void)writer.flush();
        auto total = writer.total_records();
        (void)writer.close();
        return total;
    };
}

// ---------------------------------------------------------------------------
// AI2: InferenceLogWriter — 10K ML inference records
// ---------------------------------------------------------------------------
TEST_CASE("AI2: InferenceLog 10K records", "[bench-enterprise][ai]") {
    BENCHMARK("AI2: InferenceLog 10K records") {
        bench::TempDir tmp("ebench_ai2_");

        InferenceLogWriter writer(tmp.path, "bench-ai2", 100000);

        int64_t base_ts = bench_now_ns();
        for (int i = 0; i < 10'000; ++i) {
            InferenceRecord rec;
            rec.timestamp_ns    = base_ts + static_cast<int64_t>(i) * 1'000'000;
            rec.model_id        = "trend_classifier";
            rec.model_version   = "v1.3.0";
            rec.inference_type  = InferenceType::CLASSIFICATION;
            rec.input_embedding = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
            rec.output_score    = 0.85f + static_cast<float>(i % 50) * 0.001f;
            rec.latency_ns      = 500'000 + static_cast<int64_t>(i % 1000) * 100;
            rec.batch_size      = 1;

            (void)writer.log(rec);
        }

        (void)writer.flush();
        auto total = writer.total_records();
        (void)writer.close();
        return total;
    };
}

// ---------------------------------------------------------------------------
// AI3: verify_chain — chain integrity verification
// ---------------------------------------------------------------------------
TEST_CASE("AI3: verify_chain", "[bench-enterprise][ai]") {
    // Write fixture: 10K decision records outside BENCHMARK
    bench::TempDir tmp("ebench_ai3_");

    DecisionLogWriter writer(tmp.path, "bench-ai3", 100000);
    int64_t base_ts = bench_now_ns();
    for (int i = 0; i < 10'000; ++i) {
        DecisionRecord rec;
        rec.timestamp_ns  = base_ts + static_cast<int64_t>(i) * 1'000'000;
        rec.strategy_id   = static_cast<int32_t>(i % 3);
        rec.model_version = "v2.1.0";
        rec.decision_type = DecisionType::SIGNAL;
        rec.symbol        = std::string(kSymbols[i % kSymbols.size()]);
        rec.price         = 45000.0;
        rec.quantity      = 0.01;
        rec.venue         = "OKX";
        (void)writer.log(rec);
    }
    (void)writer.flush();
    auto file_path = writer.current_file_path();
    (void)writer.close();

    BENCHMARK("AI3: verify_chain") {
        auto reader_result = DecisionLogReader::open(file_path);
        if (!reader_result.has_value()) return static_cast<int64_t>(0);
        auto& reader = *reader_result;

        auto result = reader.verify_chain();
        return result.entries_checked;
    };
}

#endif // SIGNET_ENABLE_COMMERCIAL

// ===========================================================================
// AI4: column_view on 1M price column (zero-copy tensor path)
// ===========================================================================
// Builds a ColumnBatch with 1M rows × 6 columns, then benchmarks the
// column_view() call which returns a TensorView — pure zero-copy, no alloc.

TEST_CASE("AI4: column_view 1M doubles", "[bench-enterprise][ai]") {
    constexpr size_t kRows = 1'000'000;

    // Build batch outside BENCHMARK
    auto batch = ColumnBatch::with_schema({
        {"bid_price",  TensorDataType::FLOAT64},
        {"ask_price",  TensorDataType::FLOAT64},
        {"bid_qty",    TensorDataType::FLOAT64},
        {"ask_qty",    TensorDataType::FLOAT64},
        {"spread_bps", TensorDataType::FLOAT64},
        {"mid_price",  TensorDataType::FLOAT64},
    }, kRows);

    for (size_t i = 0; i < kRows; ++i) {
        double base = 45000.0 + static_cast<double>(i % 10000) * 0.01;
        (void)batch.push_row({base, base + 0.5, 0.01, 0.02, 1.1, base + 0.25});
    }

    REQUIRE(batch.num_rows() == kRows);

    BENCHMARK("AI4: column_view 1M doubles") {
        auto tv0 = batch.column_view(0);
        auto tv5 = batch.column_view(5);
        return static_cast<size_t>(tv0.shape().dims[0])
             + static_cast<size_t>(tv5.shape().dims[0]);
    };
}

// ===========================================================================
// AI5: EventBus 4P×4C — 100K ColumnBatch events through tier-2 ring
// ===========================================================================

TEST_CASE("AI5: EventBus 4P4C 100K", "[bench-enterprise][ai]") {
    constexpr int kProducers   = 4;
    constexpr int kConsumers   = 4;
    constexpr int kPerProducer = 25'000;
    constexpr int kTotal       = kProducers * kPerProducer;  // 100K

    // Pre-build a shared batch that all producers will copy-push
    auto proto = make_column_batch({{"price"}, {"qty"}}, 1);
    (void)proto->push_row({45123.50, 0.100});

    int64_t total_consumed = 0;

    BENCHMARK("AI5: EventBus 4P4C 100K") {
        EventBusOptions eopts;
        eopts.tier2_capacity = 8192;
        EventBus bus(eopts);

        std::atomic<int64_t> consumed{0};

        std::vector<std::thread> producers;
        producers.reserve(kProducers);
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&] {
                for (int i = 0; i < kPerProducer; ++i) {
                    while (!bus.publish(proto))
                        std::this_thread::yield();
                }
            });
        }

        std::vector<std::thread> consumers;
        consumers.reserve(kConsumers);
        for (int c = 0; c < kConsumers; ++c) {
            consumers.emplace_back([&] {
                while (consumed.load(std::memory_order_relaxed) < kTotal) {
                    SharedColumnBatch out;
                    if (bus.pop(out))
                        consumed.fetch_add(1, std::memory_order_relaxed);
                    else
                        std::this_thread::yield();
                }
            });
        }

        for (auto& t : producers) t.join();
        for (auto& t : consumers) t.join();

        total_consumed = consumed.load(std::memory_order_relaxed);
        return total_consumed;
    };
}
