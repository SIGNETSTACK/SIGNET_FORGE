// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// test_event_bus.cpp — Phase 9b: MPMC ColumnBatch Event Bus
// Tests for MpmcRing, ColumnBatch, and EventBus.

#include "signet/ai/mpmc_ring.hpp"
#include "signet/ai/column_batch.hpp"
#include "signet/ai/event_bus.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ===========================================================================
// Helpers
// ===========================================================================

static std::string tmp_dir_eb(const char* name) {
    auto p = fs::temp_directory_path() / "signet_test_eventbus" / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

static int64_t now_ns() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<nanoseconds>(
            system_clock::now().time_since_epoch()).count());
}

// ===========================================================================
// Section 1 — MpmcRing
// ===========================================================================

TEST_CASE("MpmcRing capacity rounded to power of two", "[mpmc_ring]") {
    MpmcRing<int> r3(3);
    REQUIRE(r3.capacity() == 4u);

    MpmcRing<int> r4(4);
    REQUIRE(r4.capacity() == 4u);

    MpmcRing<int> r5(5);
    REQUIRE(r5.capacity() == 8u);

    MpmcRing<int> r1(1);
    REQUIRE(r1.capacity() == 2u);
}

TEST_CASE("MpmcRing basic push and pop single-threaded", "[mpmc_ring]") {
    MpmcRing<int> ring(8);
    REQUIRE(ring.empty());
    REQUIRE(ring.size() == 0u);

    REQUIRE(ring.push(42));
    REQUIRE(ring.push(99));
    REQUIRE(ring.size() == 2u);
    REQUIRE(!ring.empty());

    int v = 0;
    REQUIRE(ring.pop(v));
    REQUIRE(v == 42);
    REQUIRE(ring.pop(v));
    REQUIRE(v == 99);
    REQUIRE(!ring.pop(v));   // empty
    REQUIRE(ring.empty());
}

TEST_CASE("MpmcRing returns false when full", "[mpmc_ring]") {
    MpmcRing<int> ring(4);
    REQUIRE(ring.push(1));
    REQUIRE(ring.push(2));
    REQUIRE(ring.push(3));
    REQUIRE(ring.push(4));
    REQUIRE(!ring.push(5));  // full

    int v = 0;
    REQUIRE(ring.pop(v));
    REQUIRE(v == 1);
    REQUIRE(ring.push(5));   // now has space
}

TEST_CASE("MpmcRing pop on empty returns false", "[mpmc_ring]") {
    MpmcRing<std::string> ring(4);
    std::string s;
    REQUIRE(!ring.pop(s));
}

TEST_CASE("MpmcRing can be reused after draining", "[mpmc_ring]") {
    MpmcRing<int> ring(4);
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 4; ++i)
            REQUIRE(ring.push(i + round * 100));
        for (int i = 0; i < 4; ++i) {
            int v = 0;
            REQUIRE(ring.pop(v));
            REQUIRE(v == i + round * 100);
        }
        REQUIRE(ring.empty());
    }
}

TEST_CASE("MpmcRing works with shared_ptr elements", "[mpmc_ring]") {
    MpmcRing<std::shared_ptr<int>> ring(8);
    auto p1 = std::make_shared<int>(10);
    auto p2 = std::make_shared<int>(20);
    REQUIRE(ring.push(p1));
    REQUIRE(ring.push(p2));

    std::shared_ptr<int> out;
    REQUIRE(ring.pop(out));
    REQUIRE(*out == 10);
    REQUIRE(ring.pop(out));
    REQUIRE(*out == 20);
}

TEST_CASE("MpmcRing multi-producer multi-consumer correctness", "[mpmc_ring]") {
    constexpr size_t kItems        = 10000;
    constexpr size_t kNumProducers = 4;
    constexpr size_t kNumConsumers = 4;
    constexpr size_t kPerProducer  = kItems / kNumProducers;

    MpmcRing<int> ring(512);
    std::atomic<size_t> total_consumed{0};
    std::atomic<int64_t> checksum_produced{0};
    std::atomic<int64_t> checksum_consumed{0};

    std::vector<std::thread> producers;
    for (size_t p = 0; p < kNumProducers; ++p) {
        producers.emplace_back([&, p] {
            for (size_t i = 0; i < kPerProducer; ++i) {
                int val = static_cast<int>(p * kPerProducer + i);
                checksum_produced.fetch_add(val, std::memory_order_relaxed);
                while (!ring.push(val)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::vector<std::thread> consumers;
    for (size_t c = 0; c < kNumConsumers; ++c) {
        consumers.emplace_back([&] {
            while (total_consumed.load(std::memory_order_relaxed) < kItems) {
                int v = 0;
                if (ring.pop(v)) {
                    checksum_consumed.fetch_add(v, std::memory_order_relaxed);
                    total_consumed.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    REQUIRE(total_consumed.load() == kItems);
    REQUIRE(checksum_produced.load() == checksum_consumed.load());
}

// ===========================================================================
// Section 2 — ColumnBatch
// ===========================================================================

TEST_CASE("ColumnBatch with_schema creates correct structure", "[column_batch]") {
    auto b = ColumnBatch::with_schema({{"price"}, {"qty"}}, 64);
    REQUIRE(b.num_columns() == 2u);
    REQUIRE(b.num_rows() == 0u);
    REQUIRE(b.empty());
    REQUIRE(b.schema()[0].name == "price");
    REQUIRE(b.schema()[1].name == "qty");
}

TEST_CASE("ColumnBatch push_row appends correctly", "[column_batch]") {
    auto b = ColumnBatch::with_schema({{"a"}, {"b"}, {"c"}});
    REQUIRE(b.push_row({1.0, 2.0, 3.0}).has_value());
    REQUIRE(b.push_row({4.0, 5.0, 6.0}).has_value());
    REQUIRE(b.num_rows() == 2u);
    REQUIRE(!b.empty());

    auto s0 = b.column_span(0);
    REQUIRE(s0.size() == 2u);
    REQUIRE(s0[0] == Catch::Approx(1.0));
    REQUIRE(s0[1] == Catch::Approx(4.0));
}

TEST_CASE("ColumnBatch push_row rejects size mismatch", "[column_batch]") {
    auto b = ColumnBatch::with_schema({{"a"}, {"b"}});
    REQUIRE(!b.push_row({1.0, 2.0, 3.0}).has_value());  // 3 != 2
    REQUIRE(!b.push_row({1.0}).has_value());              // 1 != 2
}

TEST_CASE("ColumnBatch column_view returns valid TensorView", "[column_batch]") {
    auto b = ColumnBatch::with_schema({{"x"}, {"y"}});
    REQUIRE(b.push_row({3.14, 2.71}).has_value());
    REQUIRE(b.push_row({1.41, 1.73}).has_value());

    auto tv = b.column_view(0);
    REQUIRE(tv.is_valid());
    REQUIRE(tv.shape().ndim() == 1u);
    REQUIRE(tv.shape().dims[0] == 2);
    REQUIRE(tv.dtype() == TensorDataType::FLOAT64);

    const double* ptr = tv.typed_data<double>();
    REQUIRE(ptr[0] == Catch::Approx(3.14));
    REQUIRE(ptr[1] == Catch::Approx(1.41));
}

TEST_CASE("ColumnBatch as_tensor returns correct shape", "[column_batch]") {
    auto b = ColumnBatch::with_schema({{"f0"}, {"f1"}, {"f2"}});
    for (int i = 0; i < 5; ++i)
        REQUIRE(b.push_row({double(i), double(i*2), double(i*3)}).has_value());

    auto ot_result = b.as_tensor(TensorDataType::FLOAT32);
    REQUIRE(ot_result.has_value());

    const auto& ot = *ot_result;
    REQUIRE(ot.shape().is_matrix());
    REQUIRE(ot.shape().dims[0] == 5);
    REQUIRE(ot.shape().dims[1] == 3);
}

TEST_CASE("ColumnBatch to_stream_record round-trips via from_stream_record", "[column_batch]") {
    auto b = ColumnBatch::with_schema({{"mid"}, {"spread"}});
    REQUIRE(b.push_row({100.5, 0.1}).has_value());
    REQUIRE(b.push_row({101.0, 0.2}).has_value());
    b.source_id = "BINANCE";
    b.symbol    = "BTCUSDT";

    auto rec = b.to_stream_record(now_ns());

    auto b2_result = ColumnBatch::from_stream_record(rec);
    REQUIRE(b2_result.has_value());
    const auto& b2 = *b2_result;

    REQUIRE(b2.num_columns() == 2u);
    REQUIRE(b2.num_rows() == 2u);
    REQUIRE(b2.schema()[0].name == "mid");
    REQUIRE(b2.schema()[1].name == "spread");

    auto s0 = b2.column_span(0);
    REQUIRE(s0[0] == Catch::Approx(100.5));
    REQUIRE(s0[1] == Catch::Approx(101.0));

    auto s1 = b2.column_span(1);
    REQUIRE(s1[0] == Catch::Approx(0.1));
    REQUIRE(s1[1] == Catch::Approx(0.2));
}

TEST_CASE("ColumnBatch clear resets row count", "[column_batch]") {
    auto b = ColumnBatch::with_schema({{"v"}});
    REQUIRE(b.push_row({1.0}).has_value());
    REQUIRE(b.push_row({2.0}).has_value());
    REQUIRE(b.num_rows() == 2u);

    b.clear();
    REQUIRE(b.num_rows() == 0u);
    REQUIRE(b.empty());
    REQUIRE(b.num_columns() == 1u);  // schema unchanged
}

TEST_CASE("make_column_batch helper creates shared batch", "[column_batch]") {
    auto sb = make_column_batch({{"a"}, {"b"}}, 32);
    REQUIRE(sb != nullptr);
    REQUIRE(sb->num_columns() == 2u);
    REQUIRE(sb->empty());
}

// ===========================================================================
// Section 3 — EventBus
// ===========================================================================

TEST_CASE("EventBus make_channel creates named channel", "[event_bus]") {
    EventBus bus;
    auto ch1 = bus.make_channel("binance→risk");
    REQUIRE(ch1 != nullptr);
    REQUIRE(ch1->capacity() >= 256u);

    // Same name returns same channel
    auto ch2 = bus.make_channel("binance→risk");
    REQUIRE(ch1.get() == ch2.get());

    REQUIRE(bus.num_channels() == 1u);
}

TEST_CASE("EventBus channel lookup returns nullptr for unknown name", "[event_bus]") {
    EventBus bus;
    REQUIRE(bus.channel("unknown") == nullptr);
}

TEST_CASE("EventBus Tier-1 channel push/pop", "[event_bus]") {
    EventBus bus;
    auto ch = bus.make_channel("feed→ml", 64);

    auto batch = make_column_batch({{"price"}});
    REQUIRE(batch->push_row({42.0}).has_value());

    REQUIRE(ch->push(batch));

    SharedColumnBatch out;
    REQUIRE(ch->pop(out));
    REQUIRE(out != nullptr);
    REQUIRE(out->num_rows() == 1u);
    REQUIRE(out->column_span(0)[0] == Catch::Approx(42.0));
}

TEST_CASE("EventBus Tier-2 publish and pop", "[event_bus]") {
    EventBusOptions opts;
    opts.tier2_capacity = 64;
    EventBus bus(opts);

    for (int i = 0; i < 5; ++i) {
        auto b = make_column_batch({{"v"}});
        REQUIRE(b->push_row({double(i)}).has_value());
        REQUIRE(bus.publish(b));
    }

    REQUIRE(bus.tier2_size() == 5u);

    for (int i = 0; i < 5; ++i) {
        SharedColumnBatch out;
        REQUIRE(bus.pop(out));
        REQUIRE(out->column_span(0)[0] == Catch::Approx(double(i)));
    }

    SharedColumnBatch empty_out;
    REQUIRE(!bus.pop(empty_out));
}

TEST_CASE("EventBus Tier-2 publish increments stats", "[event_bus]") {
    EventBusOptions opts;
    opts.tier2_capacity = 4;
    EventBus bus(opts);

    auto b = make_column_batch({{"x"}});

    for (int i = 0; i < 4; ++i)
        REQUIRE(bus.publish(b));
    REQUIRE(!bus.publish(b));  // full → dropped

    auto s = bus.stats();
    REQUIRE(s.published == 4u);
    REQUIRE(s.dropped   == 1u);
}

TEST_CASE("EventBus Tier-2 multi-producer multi-consumer", "[event_bus]") {
    EventBusOptions opts;
    opts.tier2_capacity = 1024;
    EventBus bus(opts);

    constexpr int kTotal     = 2000;
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;

    std::atomic<int> consumed{0};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (int i = 0; i < kTotal / kProducers; ++i) {
                auto b = make_column_batch({{"val"}});
                (void)b->push_row({1.0});
                while (!bus.publish(b)) std::this_thread::yield();
            }
        });
    }

    std::vector<std::thread> consumers;
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

    REQUIRE(consumed.load() == kTotal);
}

TEST_CASE("EventBus reset_stats clears counters", "[event_bus]") {
    EventBusOptions opts;
    opts.tier2_capacity = 2;
    EventBus bus(opts);

    auto b = make_column_batch({{"x"}});
    bus.publish(b);
    bus.publish(b);
    bus.publish(b);  // dropped

    bus.reset_stats();
    auto s = bus.stats();
    REQUIRE(s.published == 0u);
    REQUIRE(s.dropped   == 0u);
}

TEST_CASE("EventBus Tier-3 sink integration routes batches to StreamingSink", "[event_bus]") {
    auto dir = tmp_dir_eb("tier3");

    StreamingSink::Options sopts;
    sopts.output_dir     = dir;
    sopts.file_prefix    = "tier3";
    sopts.row_group_size = 8;

    auto sink_result = StreamingSink::create(sopts);
    REQUIRE(sink_result.has_value());
    auto sink_ptr = std::make_shared<StreamingSink>(std::move(*sink_result));

    EventBusOptions eopts;
    eopts.tier2_capacity = 64;
    eopts.enable_tier3   = true;
    EventBus bus(eopts);
    bus.attach_sink(sink_ptr);
    REQUIRE(bus.has_sink());

    // Publish 3 batches
    for (int i = 0; i < 3; ++i) {
        auto b = make_column_batch({{"mid"}, {"spread"}});
        REQUIRE(b->push_row({double(i * 100), double(i)}).has_value());
        b->source_id = "TEST";
        REQUIRE(bus.publish(b));
    }

    // Flush and close sink
    REQUIRE(sink_ptr->flush().has_value());
    REQUIRE(sink_ptr->stop().has_value());

    // stats: 3 published, 0 dropped, 0 tier3_drops
    auto s = bus.stats();
    REQUIRE(s.published   == 3u);
    REQUIRE(s.dropped     == 0u);
    REQUIRE(s.tier3_drops == 0u);

    bus.detach_sink();
    REQUIRE(!bus.has_sink());
}

// ===========================================================================
// Hardening Pass #4 Tests
// ===========================================================================

TEST_CASE("MpmcRing rejects zero capacity", "[event_bus][hardening]") {
    // H-18: Zero-capacity guard
    REQUIRE_THROWS_AS(
        (MpmcRing<int>(0)),
        std::invalid_argument
    );
}

TEST_CASE("MpmcRing requires power-of-two capacity", "[event_bus][hardening]") {
    // H-18: Power-of-two capacity for masked index arithmetic
    // Implementation rounds up to next power of 2 (minimum 2)
    MpmcRing<int> ring2(2);
    REQUIRE(ring2.capacity() == 2);

    // Capacity 4 (2^2) should work
    MpmcRing<int> ring4(4);
    REQUIRE(ring4.capacity() == 4);

    // Non-power-of-two should be rounded up
    MpmcRing<int> ring3(3);
    REQUIRE(ring3.capacity() == 4); // Rounded up to next power of 2

    // Capacity 1 rounds up to 2
    MpmcRing<int> ring1(1);
    REQUIRE(ring1.capacity() == 2);
}
