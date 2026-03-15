// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// bench_event_bus.cpp — MPMC ring + ColumnBatch + EventBus throughput benchmarks
//
// Key claims:
//   - MpmcRing<int64_t> push+pop           sub-microsecond per op
//   - ColumnBatch column_view              zero-copy (no heap allocation)
//   - EventBus publish+pop single-thread   < 2 µs per round-trip

#include "signet/ai/mpmc_ring.hpp"
#include "signet/ai/column_batch.hpp"
#include "signet/ai/event_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace signet::forge;

// ===========================================================================
// Benchmark 1 — MpmcRing<int64_t> push+pop latency (single-threaded)
// ===========================================================================

TEST_CASE("MpmcRing<int64_t> push+pop latency (single-threaded)",
          "[bench][mpmc_ring]") {
    // Ring lives outside the BENCHMARK so it persists across iterations.
    MpmcRing<int64_t> ring(1024);

    BENCHMARK("push+pop") {
        ring.push(42LL);
        int64_t v = 0;
        ring.pop(v);
        return v;
    };
}

// ===========================================================================
// Benchmark 2 — MpmcRing<SharedColumnBatch> 4P × 4C concurrent throughput
// ===========================================================================

TEST_CASE("MpmcRing<SharedColumnBatch> 4P×4C concurrent throughput",
          "[bench][mpmc_ring]") {
    constexpr int kProducers       = 4;
    constexpr int kConsumers       = 4;
    constexpr int kPerProducer     = 1000;
    constexpr int kTotal           = kProducers * kPerProducer;  // 4000

    // Pre-build one shared batch that all producers will copy-push.
    // ColumnBatch is copy-constructible; shared_ptr copies are cheap.
    auto proto = make_column_batch({{"v0"}, {"v1"}}, 1);
    (void)proto->push_row({1.0, 2.0});

    int64_t total_consumed = 0;

    BENCHMARK("4P4C 4000 items") {
        MpmcRing<SharedColumnBatch> ring(4096);
        std::atomic<int64_t> produced{0};
        std::atomic<int64_t> consumed{0};

        std::vector<std::thread> producers;
        producers.reserve(kProducers);
        for (int p = 0; p < kProducers; ++p) {
            producers.emplace_back([&] {
                for (int i = 0; i < kPerProducer; ++i) {
                    while (!ring.push(proto))
                        std::this_thread::yield();
                    produced.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        std::vector<std::thread> consumers;
        consumers.reserve(kConsumers);
        for (int c = 0; c < kConsumers; ++c) {
            consumers.emplace_back([&] {
                while (consumed.load(std::memory_order_relaxed) < kTotal) {
                    SharedColumnBatch out;
                    if (ring.pop(out))
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

// ===========================================================================
// Benchmark 3 — ColumnBatch push_row + column_view (no copy)
// ===========================================================================

TEST_CASE("ColumnBatch push_row + column_view (no copy)",
          "[bench][column_batch]") {
    // Build a row of 8 doubles once to pass to push_row.
    const std::vector<double> row_vals = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8};

    size_t view_size = 0;

    BENCHMARK("push 1000 rows + column_view") {
        // Fresh batch per iteration — measures allocation + fill + column_view.
        auto b = ColumnBatch::with_schema(
            {{"f0"}, {"f1"}, {"f2"}, {"f3"}, {"f4"}, {"f5"}, {"f6"}, {"f7"}},
            1024);

        for (int i = 0; i < 1000; ++i)
            (void)b.push_row(row_vals);

        auto tv0 = b.column_view(0);
        auto tv7 = b.column_view(7);

        // Return combined element count so the compiler cannot elide the views.
        view_size = static_cast<size_t>(tv0.shape().dims[0])
                  + static_cast<size_t>(tv7.shape().dims[0]);
        return view_size;
    };
}

// ===========================================================================
// Benchmark 4 — ColumnBatch as_tensor() — 1024 rows × 8 columns
// ===========================================================================

TEST_CASE("ColumnBatch as_tensor() — 1024 rows × 8 columns",
          "[bench][column_batch]") {
    constexpr size_t kRows = 1024;
    constexpr size_t kCols = 8;

    // Build and fill the batch OUTSIDE the BENCHMARK (setup cost excluded).
    const std::vector<double> row_vals = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
    auto batch = ColumnBatch::with_schema(
        {{"f0"}, {"f1"}, {"f2"}, {"f3"}, {"f4"}, {"f5"}, {"f6"}, {"f7"}},
        kRows);
    for (size_t i = 0; i < kRows; ++i)
        (void)batch.push_row(row_vals);

    REQUIRE(batch.num_rows() == kRows);
    REQUIRE(batch.num_columns() == kCols);

    int64_t n_elements = 0;

    BENCHMARK("as_tensor") {
        auto result = batch.as_tensor();   // default: FLOAT32 output
        if (result.has_value())
            n_elements = result->num_elements();
        return n_elements;
    };
}

// ===========================================================================
// Benchmark 5 — EventBus publish + pop single-threaded throughput
// ===========================================================================

TEST_CASE("EventBus publish + pop single-threaded throughput",
          "[bench][event_bus]") {
    // Tier-2 ring large enough that it never fills during 1000 iterations.
    EventBusOptions eopts;
    eopts.tier2_capacity = 4096;

    // Build a small 1-row batch to publish (setup outside BENCHMARK).
    auto batch = make_column_batch({{"price"}, {"qty"}}, 1);
    (void)batch->push_row({100.0, 0.5});

    uint64_t published_count = 0;

    BENCHMARK("publish+pop 1000") {
        EventBus bus(eopts);

        for (int i = 0; i < 1000; ++i) {
            bus.publish(batch);
            SharedColumnBatch out;
            bus.pop(out);
        }

        published_count = bus.stats().published;
        return published_count;
    };
}
