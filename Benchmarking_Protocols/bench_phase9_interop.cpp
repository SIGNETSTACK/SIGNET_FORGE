// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// bench_phase9_interop.cpp — Arrow/Tensor interop bridge benchmarks
//
// I1: Arrow export 1M doubles     — ArrowExporter::export_column + release
// I2: tensor wrap 1M doubles      — ColumnToTensor::wrap_column zero-copy
// I3: batch tensor 1M x 6         — ColumnBatch::as_tensor assembly
//
// Build: cmake --preset benchmarks && cmake --build build-benchmarks
// Run:   ./build-benchmarks/signet_ebench "[bench-enterprise][interop]"

#include "common.hpp"
#include "signet/interop/arrow_bridge.hpp"
#include "signet/ai/tensor_bridge.hpp"
#include "signet/ai/column_batch.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace signet::forge;

// ===========================================================================
// Constants
// ===========================================================================
namespace {

constexpr size_t kNumValues = 1'000'000;
constexpr size_t kNumCols   = 6;

// Pre-allocated 1M-double column, shared across I1 and I2.
const std::vector<double>& cached_doubles() {
    static std::vector<double> data = [] {
        std::vector<double> v(kNumValues);
        for (size_t i = 0; i < kNumValues; ++i)
            v[i] = 45000.0 + static_cast<double>(i) * 0.01;
        return v;
    }();
    return data;
}

} // anonymous namespace

// ===========================================================================
// I1: Arrow export 1M doubles
// ===========================================================================
// Measures ArrowExporter::export_column latency for a 1M-element double
// column, including the release callbacks that free the private data
// contexts.  Zero-copy: buffers[1] points directly into the source vector.

TEST_CASE("I1: Arrow export 1M doubles", "[bench-enterprise][interop]") {
    const auto& data = cached_doubles();
    const auto  num_values = static_cast<int64_t>(data.size());

    BENCHMARK("I1: Arrow export 1M doubles") {
        ArrowArray  array{};
        ArrowSchema schema{};

        (void)ArrowExporter::export_column(
            data.data(), num_values, PhysicalType::DOUBLE,
            "mid_price", &array, &schema);

        // Must release to free the heap-allocated private_data contexts
        schema.release(&schema);
        array.release(&array);

        return num_values;
    };
}

// ===========================================================================
// I2: tensor wrap 1M doubles
// ===========================================================================
// Measures ColumnToTensor::wrap_column latency for a 1M-element double
// column.  Pure zero-copy: no allocation, no memcpy.  The returned
// TensorView points directly into the source vector.

TEST_CASE("I2: tensor wrap 1M doubles", "[bench-enterprise][interop]") {
    const auto& data = cached_doubles();
    const auto  num_values = static_cast<int64_t>(data.size());

    BENCHMARK("I2: tensor wrap 1M doubles") {
        auto result = ColumnToTensor::wrap_column(
            data.data(), num_values, PhysicalType::DOUBLE);

        return result->shape().dims[0];
    };
}

// ===========================================================================
// I3: batch tensor 1M x 6
// ===========================================================================
// Builds a ColumnBatch with 1M rows x 6 feature columns (simulating
// bid_price, ask_price, bid_qty, ask_qty, spread_bps, mid_price).
// The batch is built once outside the BENCHMARK loop; the timed portion
// is only the as_tensor() call which assembles all 6 columns into a
// single contiguous 2D OwnedTensor [1M, 6] via BatchTensorBuilder.

TEST_CASE("I3: batch tensor 1M x 6", "[bench-enterprise][interop]") {
    // Build the batch once (expensive — 1M rows x 6 cols)
    auto batch = ColumnBatch::with_schema({
        {"bid_price",  TensorDataType::FLOAT64},
        {"ask_price",  TensorDataType::FLOAT64},
        {"bid_qty",    TensorDataType::FLOAT64},
        {"ask_qty",    TensorDataType::FLOAT64},
        {"spread_bps", TensorDataType::FLOAT64},
        {"mid_price",  TensorDataType::FLOAT64},
    }, kNumValues);

    for (size_t i = 0; i < kNumValues; ++i) {
        double base = 45000.0 + static_cast<double>(i % 10000) * 0.01;
        double spread = 0.5 + static_cast<double>(i % 100) * 0.01;
        (void)batch.push_row({
            base,                       // bid_price
            base + spread,              // ask_price
            0.001 + double(i % 1000) * 0.001, // bid_qty
            0.002 + double(i % 500)  * 0.001, // ask_qty
            spread * 100.0 / base,      // spread_bps
            base + spread / 2.0,        // mid_price
        });
    }

    REQUIRE(batch.num_rows() == kNumValues);
    REQUIRE(batch.num_columns() == kNumCols);

    BENCHMARK("I3: batch tensor 1M x 6") {
        auto result = batch.as_tensor(TensorDataType::FLOAT32);
        return result->num_elements();
    };
}
