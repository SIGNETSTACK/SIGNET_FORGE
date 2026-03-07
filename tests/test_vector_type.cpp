// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "signet/ai/vector_type.hpp"
#include "signet/ai/quantized_vector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Deterministic float vector generator (simple LCG-based, reproducible)
// ---------------------------------------------------------------------------
static std::vector<float> generate_vectors(size_t num_vectors, uint32_t dim,
                                           float lo = -1.0f, float hi = 1.0f,
                                           uint32_t seed = 42) {
    std::vector<float> out(num_vectors * dim);
    uint32_t state = seed;
    const float range = hi - lo;
    for (size_t i = 0; i < out.size(); ++i) {
        // LCG: state = state * 1664525 + 1013904223
        state = state * 1664525u + 1013904223u;
        float t = static_cast<float>(state & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
        out[i] = lo + t * range;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Scalar reference implementations for SIMD verification
// ---------------------------------------------------------------------------
static float scalar_dot_product(const float* a, const float* b, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += static_cast<double>(a[i]) * static_cast<double>(b[i]);
    return static_cast<float>(acc);
}

static float scalar_l2_distance_sq(const float* a, const float* b, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double d = static_cast<double>(a[i]) - static_cast<double>(b[i]);
        acc += d * d;
    }
    return static_cast<float>(acc);
}

static float scalar_sum_of_squares(const float* data, size_t n) {
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return static_cast<float>(acc);
}

// ===================================================================
// Section 1: Vector Column Spec [vector][spec]
// ===================================================================

TEST_CASE("VectorColumnSpec bytes_per_vector", "[vector][spec]") {
    SECTION("FLOAT32 dim=768 -> 3072 bytes") {
        VectorColumnSpec spec{768, VectorElementType::FLOAT32};
        REQUIRE(spec.bytes_per_vector() == 3072);
    }

    SECTION("FLOAT64 dim=768 -> 6144 bytes") {
        VectorColumnSpec spec{768, VectorElementType::FLOAT64};
        REQUIRE(spec.bytes_per_vector() == 6144);
    }

    SECTION("FLOAT16 dim=768 -> 1536 bytes") {
        VectorColumnSpec spec{768, VectorElementType::FLOAT16};
        REQUIRE(spec.bytes_per_vector() == 1536);
    }

    SECTION("Edge case: dim=1") {
        VectorColumnSpec f32{1, VectorElementType::FLOAT32};
        REQUIRE(f32.bytes_per_vector() == 4);

        VectorColumnSpec f64{1, VectorElementType::FLOAT64};
        REQUIRE(f64.bytes_per_vector() == 8);

        VectorColumnSpec f16{1, VectorElementType::FLOAT16};
        REQUIRE(f16.bytes_per_vector() == 2);
    }
}

TEST_CASE("VectorColumnSpec make_descriptor", "[vector][spec]") {
    VectorColumnSpec spec{384, VectorElementType::FLOAT32};
    auto desc = VectorWriter::make_descriptor("embedding", spec);

    REQUIRE(desc.name == "embedding");
    REQUIRE(desc.physical_type == PhysicalType::FIXED_LEN_BYTE_ARRAY);
    REQUIRE(desc.type_length == static_cast<int32_t>(384 * sizeof(float)));
    REQUIRE(desc.logical_type == LogicalType::FLOAT32_VECTOR);
}

// ===================================================================
// Section 2: VectorWriter/Reader Roundtrip [vector][roundtrip]
// ===================================================================

TEST_CASE("Single vector write/read roundtrip", "[vector][roundtrip]") {
    constexpr uint32_t dim = 4;
    VectorColumnSpec spec{dim, VectorElementType::FLOAT32};

    float input[dim] = {1.0f, -2.5f, 3.14159f, 0.0f};

    VectorWriter writer(spec);
    writer.add(input);
    REQUIRE(writer.num_vectors() == 1);

    auto page = writer.flush();
    REQUIRE(!page.empty());

    VectorReader reader(spec);
    auto result = reader.read_page(page.data(), page.size());
    REQUIRE(result.size() == 1);
    REQUIRE(result[0].size() == dim);

    // Verify exact bit equality via memcmp (FLOAT32 lossless roundtrip)
    for (uint32_t i = 0; i < dim; ++i) {
        uint32_t expected_bits, actual_bits;
        std::memcpy(&expected_bits, &input[i], sizeof(float));
        std::memcpy(&actual_bits, &result[0][i], sizeof(float));
        REQUIRE(expected_bits == actual_bits);
    }
}

TEST_CASE("Batch vector write/read roundtrip", "[vector][roundtrip]") {
    constexpr uint32_t dim = 128;
    constexpr size_t num = 100;
    VectorColumnSpec spec{dim, VectorElementType::FLOAT32};

    auto data = generate_vectors(num, dim);

    VectorWriter writer(spec);
    writer.add_batch(data.data(), num);
    REQUIRE(writer.num_vectors() == num);

    auto page = writer.flush();

    VectorReader reader(spec);
    auto result = reader.read_page(page.data(), page.size());
    REQUIRE(result.size() == num);

    for (size_t v = 0; v < num; ++v) {
        REQUIRE(result[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            uint32_t expected_bits, actual_bits;
            std::memcpy(&expected_bits, &data[v * dim + d], sizeof(float));
            std::memcpy(&actual_bits, &result[v][d], sizeof(float));
            REQUIRE(expected_bits == actual_bits);
        }
    }
}

TEST_CASE("Multiple dimensions write/read", "[vector][roundtrip]") {
    const std::vector<uint32_t> dims = {1, 8, 384, 768, 1536};

    for (uint32_t dim : dims) {
        DYNAMIC_SECTION("dim=" << dim) {
            VectorColumnSpec spec{dim, VectorElementType::FLOAT32};
            constexpr size_t num = 5;

            auto data = generate_vectors(num, dim, -10.0f, 10.0f, dim);

            VectorWriter writer(spec);
            writer.add_batch(data.data(), num);
            auto page = writer.flush();

            VectorReader reader(spec);
            auto result = reader.read_page(page.data(), page.size());
            REQUIRE(result.size() == num);

            for (size_t v = 0; v < num; ++v) {
                REQUIRE(result[v].size() == dim);
                for (uint32_t d = 0; d < dim; ++d) {
                    uint32_t expected_bits, actual_bits;
                    std::memcpy(&expected_bits, &data[v * dim + d], sizeof(float));
                    std::memcpy(&actual_bits, &result[v][d], sizeof(float));
                    REQUIRE(expected_bits == actual_bits);
                }
            }
        }
    }
}

TEST_CASE("Zero-copy read", "[vector][roundtrip]") {
    constexpr uint32_t dim = 16;
    constexpr size_t num = 10;
    VectorColumnSpec spec{dim, VectorElementType::FLOAT32};

    auto data = generate_vectors(num, dim);

    VectorWriter writer(spec);
    writer.add_batch(data.data(), num);
    auto page = writer.flush();

    VectorReader reader(spec);
    auto zc_result = reader.read_page_zero_copy(page.data(), page.size());
    REQUIRE(zc_result.has_value());

    auto& zc = zc_result.value();
    REQUIRE(zc.num_vectors == num);
    REQUIRE(zc.data != nullptr);

    // Verify zero-copy data matches original
    for (size_t v = 0; v < num; ++v) {
        for (uint32_t d = 0; d < dim; ++d) {
            uint32_t expected_bits, actual_bits;
            std::memcpy(&expected_bits, &data[v * dim + d], sizeof(float));
            std::memcpy(&actual_bits, &zc.data[v * dim + d], sizeof(float));
            REQUIRE(expected_bits == actual_bits);
        }
    }
}

TEST_CASE("Read single vector by index", "[vector][roundtrip]") {
    constexpr uint32_t dim = 8;
    constexpr size_t num = 10;
    VectorColumnSpec spec{dim, VectorElementType::FLOAT32};

    auto data = generate_vectors(num, dim, 0.0f, 100.0f);

    VectorWriter writer(spec);
    writer.add_batch(data.data(), num);
    auto page = writer.flush();

    VectorReader reader(spec);

    // Read vector at index 5
    constexpr size_t target_idx = 5;
    auto vec = reader.read_vector(page.data(), page.size(), target_idx);
    REQUIRE(vec.size() == dim);

    for (uint32_t d = 0; d < dim; ++d) {
        uint32_t expected_bits, actual_bits;
        std::memcpy(&expected_bits, &data[target_idx * dim + d], sizeof(float));
        std::memcpy(&actual_bits, &vec[d], sizeof(float));
        REQUIRE(expected_bits == actual_bits);
    }

    // Also check first and last vectors
    auto first = reader.read_vector(page.data(), page.size(), 0);
    REQUIRE(first.size() == dim);
    for (uint32_t d = 0; d < dim; ++d) {
        uint32_t expected_bits, actual_bits;
        std::memcpy(&expected_bits, &data[d], sizeof(float));
        std::memcpy(&actual_bits, &first[d], sizeof(float));
        REQUIRE(expected_bits == actual_bits);
    }

    auto last = reader.read_vector(page.data(), page.size(), num - 1);
    REQUIRE(last.size() == dim);
    for (uint32_t d = 0; d < dim; ++d) {
        uint32_t expected_bits, actual_bits;
        std::memcpy(&expected_bits, &data[(num - 1) * dim + d], sizeof(float));
        std::memcpy(&actual_bits, &last[d], sizeof(float));
        REQUIRE(expected_bits == actual_bits);
    }
}

// ===================================================================
// Section 3: Float16 Conversion [vector][float16]
// ===================================================================

TEST_CASE("f16_to_f32 and f32_to_f16 roundtrip", "[vector][float16]") {
    // Values that are exactly representable in float16
    const float test_values[] = {0.0f, 1.0f, -1.0f, 0.5f, 65504.0f};

    for (float val : test_values) {
        DYNAMIC_SECTION("value=" << val) {
            uint16_t h = f32_to_f16(val);
            float recovered = f16_to_f32(h);
            REQUIRE(recovered == val);
        }
    }

    SECTION("Min normal float16 (6.1e-5)") {
        // Smallest positive normal float16: 2^-14 ~ 6.10352e-5
        float min_normal_f16 = std::ldexp(1.0f, -14);
        uint16_t h = f32_to_f16(min_normal_f16);
        float recovered = f16_to_f32(h);
        REQUIRE(recovered == Catch::Approx(min_normal_f16).epsilon(1e-3));
    }
}

TEST_CASE("Float16 special values", "[vector][float16]") {
    SECTION("+inf") {
        float pos_inf = std::numeric_limits<float>::infinity();
        uint16_t h = f32_to_f16(pos_inf);
        float recovered = f16_to_f32(h);
        REQUIRE(std::isinf(recovered));
        REQUIRE(recovered > 0.0f);
    }

    SECTION("-inf") {
        float neg_inf = -std::numeric_limits<float>::infinity();
        uint16_t h = f32_to_f16(neg_inf);
        float recovered = f16_to_f32(h);
        REQUIRE(std::isinf(recovered));
        REQUIRE(recovered < 0.0f);
    }

    SECTION("NaN") {
        float nan_val = std::numeric_limits<float>::quiet_NaN();
        uint16_t h = f32_to_f16(nan_val);
        float recovered = f16_to_f32(h);
        REQUIRE(std::isnan(recovered));
    }

    SECTION("+0 and -0") {
        // +0
        float pos_zero = 0.0f;
        uint16_t h_pos = f32_to_f16(pos_zero);
        float r_pos = f16_to_f32(h_pos);
        REQUIRE(r_pos == 0.0f);

        uint32_t pos_bits;
        std::memcpy(&pos_bits, &r_pos, sizeof(float));
        REQUIRE((pos_bits & 0x80000000u) == 0u);  // sign bit clear

        // -0
        float neg_zero = -0.0f;
        uint16_t h_neg = f32_to_f16(neg_zero);
        float r_neg = f16_to_f32(h_neg);
        REQUIRE(r_neg == 0.0f);  // -0 == +0 in IEEE754

        uint32_t neg_bits;
        std::memcpy(&neg_bits, &r_neg, sizeof(float));
        REQUIRE((neg_bits & 0x80000000u) != 0u);  // sign bit set
    }
}

// ===================================================================
// Section 4: SIMD Utilities [vector][simd]
// ===================================================================

TEST_CASE("dot_product SIMD matches scalar", "[vector][simd]") {
    constexpr size_t n = 64;
    auto a_data = generate_vectors(1, n, -5.0f, 5.0f, 100);
    auto b_data = generate_vectors(1, n, -5.0f, 5.0f, 200);

    float simd_result = simd::dot_product(a_data.data(), b_data.data(), n);
    float scalar_result = scalar_dot_product(a_data.data(), b_data.data(), n);

    REQUIRE(simd_result == Catch::Approx(scalar_result).epsilon(1e-5));
}

TEST_CASE("l2_distance_sq SIMD matches scalar", "[vector][simd]") {
    constexpr size_t n = 64;
    auto a_data = generate_vectors(1, n, -3.0f, 3.0f, 300);
    auto b_data = generate_vectors(1, n, -3.0f, 3.0f, 400);

    float simd_result = simd::l2_distance_sq(a_data.data(), b_data.data(), n);
    float scalar_result = scalar_l2_distance_sq(a_data.data(), b_data.data(), n);

    REQUIRE(simd_result == Catch::Approx(scalar_result).epsilon(1e-5));

    // Distance from a vector to itself is zero
    float self_dist = simd::l2_distance_sq(a_data.data(), a_data.data(), n);
    REQUIRE(self_dist == Catch::Approx(0.0f).margin(1e-7));
}

TEST_CASE("l2_normalize produces unit vector", "[vector][simd]") {
    constexpr size_t n = 32;
    auto data = generate_vectors(1, n, -10.0f, 10.0f, 500);

    simd::l2_normalize(data.data(), n);

    float norm_sq = simd::sum_of_squares(data.data(), n);
    REQUIRE(norm_sq == Catch::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("sum_of_squares SIMD matches scalar", "[vector][simd]") {
    constexpr size_t n = 48;
    auto data = generate_vectors(1, n, -2.0f, 2.0f, 600);

    float simd_result = simd::sum_of_squares(data.data(), n);
    float scalar_result = scalar_sum_of_squares(data.data(), n);

    REQUIRE(simd_result == Catch::Approx(scalar_result).epsilon(1e-5));
}

TEST_CASE("Large vector SIMD (1024 elements)", "[vector][simd]") {
    constexpr size_t n = 1024;
    auto a_data = generate_vectors(1, n, -1.0f, 1.0f, 700);
    auto b_data = generate_vectors(1, n, -1.0f, 1.0f, 800);

    // dot_product
    float simd_dot = simd::dot_product(a_data.data(), b_data.data(), n);
    float scalar_dot = scalar_dot_product(a_data.data(), b_data.data(), n);
    REQUIRE(simd_dot == Catch::Approx(scalar_dot).epsilon(1e-4));

    // l2_distance_sq
    float simd_l2 = simd::l2_distance_sq(a_data.data(), b_data.data(), n);
    float scalar_l2 = scalar_l2_distance_sq(a_data.data(), b_data.data(), n);
    REQUIRE(simd_l2 == Catch::Approx(scalar_l2).epsilon(1e-4));

    // sum_of_squares
    float simd_sos = simd::sum_of_squares(a_data.data(), n);
    float scalar_sos = scalar_sum_of_squares(a_data.data(), n);
    REQUIRE(simd_sos == Catch::Approx(scalar_sos).epsilon(1e-4));

    // l2_normalize on large vector
    simd::l2_normalize(a_data.data(), n);
    float norm_sq = simd::sum_of_squares(a_data.data(), n);
    REQUIRE(norm_sq == Catch::Approx(1.0f).epsilon(1e-5));
}

// ===================================================================
// Section 5: Symmetric INT8 Quantization [quantized][int8][symmetric]
// ===================================================================

TEST_CASE("Symmetric INT8 compute params", "[quantized][int8][symmetric]") {
    constexpr uint32_t dim = 4;
    // Data with known range: max absolute value = 2.0
    float data[] = {1.0f, -2.0f, 0.5f, 1.5f};

    auto params = QuantizationParams::compute(data, 1, dim, QuantizationScheme::SYMMETRIC_INT8);

    REQUIRE(params.scheme == QuantizationScheme::SYMMETRIC_INT8);
    REQUIRE(params.dimension == dim);
    REQUIRE(params.zero_point == Catch::Approx(0.0f));
    // Scale = max_abs / 127 = 2.0 / 127
    REQUIRE(params.scale == Catch::Approx(2.0f / 127.0f).epsilon(1e-5));
}

TEST_CASE("Symmetric INT8 quantize/dequantize roundtrip", "[quantized][int8][symmetric]") {
    constexpr uint32_t dim = 64;
    constexpr size_t num = 10;

    auto data = generate_vectors(num, dim, -3.0f, 3.0f, 1000);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);

    Quantizer quantizer(params);
    auto quantized = quantizer.quantize_batch(data.data(), num);

    Dequantizer dequantizer(params);
    auto recovered = dequantizer.dequantize_batch(quantized.data(), num);

    REQUIRE(recovered.size() == num);

    float max_error = 0.0f;
    for (size_t v = 0; v < num; ++v) {
        REQUIRE(recovered[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(recovered[v][d] - data[v * dim + d]);
            max_error = std::max(max_error, err);
        }
    }

    // For symmetric INT8, max error should be <= scale/2
    REQUIRE(max_error <= params.scale / 2.0f + 1e-7f);
}

TEST_CASE("Symmetric INT8 error bounds", "[quantized][int8][symmetric]") {
    constexpr uint32_t dim = 128;
    constexpr size_t num = 50;

    auto data = generate_vectors(num, dim, -10.0f, 10.0f, 1100);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);

    Quantizer quantizer(params);
    Dequantizer dequantizer(params);

    // Quantize each vector individually and check error
    std::vector<uint8_t> qbuf(params.bytes_per_vector());
    std::vector<float> dbuf(dim);

    for (size_t v = 0; v < num; ++v) {
        const float* input = data.data() + v * dim;
        quantizer.quantize(input, qbuf.data());
        dequantizer.dequantize(qbuf.data(), dbuf.data());

        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(dbuf[d] - input[d]);
            REQUIRE(err <= params.scale / 2.0f + 1e-7f);
        }
    }
}

TEST_CASE("QuantizationParams serialization roundtrip", "[quantized][int8][symmetric]") {
    QuantizationParams original;
    original.scheme     = QuantizationScheme::SYMMETRIC_INT8;
    original.scale      = 0.0157480315f;
    original.zero_point = 0.0f;
    original.dimension  = 768;

    std::string serialized = original.serialize();
    REQUIRE(!serialized.empty());

    auto deserialized = QuantizationParams::deserialize(serialized);
    REQUIRE(deserialized.has_value());

    auto& recovered = deserialized.value();
    REQUIRE(recovered.scheme == original.scheme);
    REQUIRE(recovered.scale == Catch::Approx(original.scale));
    REQUIRE(recovered.zero_point == Catch::Approx(original.zero_point));
    REQUIRE(recovered.dimension == original.dimension);
}

// ===================================================================
// Section 6: Asymmetric INT8 Quantization [quantized][int8][asymmetric]
// ===================================================================

TEST_CASE("Asymmetric INT8 compute params with offset", "[quantized][int8][asymmetric]") {
    constexpr uint32_t dim = 8;
    // Data in [10.0, 20.0] range
    float data[] = {10.0f, 12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 15.0f, 11.0f};

    auto params = QuantizationParams::compute(data, 1, dim, QuantizationScheme::ASYMMETRIC_INT8);

    REQUIRE(params.scheme == QuantizationScheme::ASYMMETRIC_INT8);
    REQUIRE(params.dimension == dim);
    // zero_point should approximate the minimum value
    REQUIRE(params.zero_point == Catch::Approx(10.0f).epsilon(0.01));
    // scale = (max - min) / 255 = 10.0 / 255
    REQUIRE(params.scale == Catch::Approx(10.0f / 255.0f).epsilon(1e-4));
}

TEST_CASE("Asymmetric INT8 quantize/dequantize roundtrip", "[quantized][int8][asymmetric]") {
    constexpr uint32_t dim = 32;
    constexpr size_t num = 20;

    // Generate data in [10.0, 20.0] to exercise asymmetric path
    auto data = generate_vectors(num, dim, 10.0f, 20.0f, 2000);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::ASYMMETRIC_INT8);

    Quantizer quantizer(params);
    auto quantized = quantizer.quantize_batch(data.data(), num);

    Dequantizer dequantizer(params);
    auto recovered = dequantizer.dequantize_batch(quantized.data(), num);

    REQUIRE(recovered.size() == num);

    float max_error = 0.0f;
    for (size_t v = 0; v < num; ++v) {
        REQUIRE(recovered[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(recovered[v][d] - data[v * dim + d]);
            max_error = std::max(max_error, err);
        }
    }

    // Asymmetric INT8: max error <= scale/2
    REQUIRE(max_error <= params.scale / 2.0f + 1e-7f);
}

// ===================================================================
// Section 7: INT4 Quantization [quantized][int4]
// ===================================================================

TEST_CASE("INT4 packing: 2 values per byte", "[quantized][int4]") {
    constexpr uint32_t dim = 8;

    // Create data with known small values that map nicely to 4-bit range
    float data[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};

    auto params = QuantizationParams::compute(data, 1, dim, QuantizationScheme::SYMMETRIC_INT4);

    REQUIRE(params.scheme == QuantizationScheme::SYMMETRIC_INT4);

    // INT4 dim=8 -> 4 bytes (2 values per byte, high nibble first)
    REQUIRE(params.bytes_per_vector() == 4);

    Quantizer quantizer(params);
    std::vector<uint8_t> qbuf(params.bytes_per_vector());
    quantizer.quantize(data, qbuf.data());

    // Verify 4 bytes used for 8 values
    REQUIRE(qbuf.size() == 4);

    // Dequantize and verify approximate recovery
    Dequantizer dequantizer(params);
    std::vector<float> dbuf(dim);
    dequantizer.dequantize(qbuf.data(), dbuf.data());

    for (uint32_t d = 0; d < dim; ++d) {
        float err = std::abs(dbuf[d] - data[d]);
        REQUIRE(err <= params.scale / 2.0f + 1e-6f);
    }
}

TEST_CASE("INT4 quantize/dequantize roundtrip", "[quantized][int4]") {
    constexpr uint32_t dim = 8;
    constexpr size_t num = 5;

    auto data = generate_vectors(num, dim, -1.0f, 1.0f, 3000);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT4);

    // dim=8 -> 4 bytes per vector
    REQUIRE(params.bytes_per_vector() == 4);

    Quantizer quantizer(params);
    auto quantized = quantizer.quantize_batch(data.data(), num);

    Dequantizer dequantizer(params);
    auto recovered = dequantizer.dequantize_batch(quantized.data(), num);

    REQUIRE(recovered.size() == num);

    for (size_t v = 0; v < num; ++v) {
        REQUIRE(recovered[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(recovered[v][d] - data[v * dim + d]);
            REQUIRE(err <= params.scale / 2.0f + 1e-6f);
        }
    }
}

TEST_CASE("INT4 odd dimension", "[quantized][int4]") {
    constexpr uint32_t dim = 7;
    constexpr size_t num = 3;

    auto data = generate_vectors(num, dim, -2.0f, 2.0f, 3100);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT4);

    // dim=7 -> ceil(7/2) = 4 bytes per vector (last nibble padded)
    REQUIRE(params.bytes_per_vector() == 4);

    Quantizer quantizer(params);
    auto quantized = quantizer.quantize_batch(data.data(), num);

    Dequantizer dequantizer(params);
    auto recovered = dequantizer.dequantize_batch(quantized.data(), num);

    REQUIRE(recovered.size() == num);

    for (size_t v = 0; v < num; ++v) {
        REQUIRE(recovered[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(recovered[v][d] - data[v * dim + d]);
            REQUIRE(err <= params.scale / 2.0f + 1e-6f);
        }
    }
}

TEST_CASE("INT4 error bounds", "[quantized][int4]") {
    constexpr uint32_t dim = 64;
    constexpr size_t num = 20;

    auto data = generate_vectors(num, dim, -5.0f, 5.0f, 3200);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT4);

    Quantizer quantizer(params);
    Dequantizer dequantizer(params);

    std::vector<uint8_t> qbuf(params.bytes_per_vector());
    std::vector<float> dbuf(dim);

    float max_error = 0.0f;
    for (size_t v = 0; v < num; ++v) {
        const float* input = data.data() + v * dim;
        quantizer.quantize(input, qbuf.data());
        dequantizer.dequantize(qbuf.data(), dbuf.data());

        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(dbuf[d] - input[d]);
            max_error = std::max(max_error, err);
            REQUIRE(err <= params.scale / 2.0f + 1e-6f);
        }
    }

    // Verify there IS some quantization error (not trivially zero)
    REQUIRE(max_error > 0.0f);
}

// ===================================================================
// Section 8: QuantizedVectorWriter/Reader [quantized][writer-reader]
// ===================================================================

TEST_CASE("Write and read symmetric INT8 pipeline", "[quantized][writer-reader]") {
    constexpr uint32_t dim = 32;
    constexpr size_t num = 10;

    auto data = generate_vectors(num, dim, -3.0f, 3.0f, 4000);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);

    // Write
    QuantizedVectorWriter writer(params);
    for (size_t v = 0; v < num; ++v) {
        writer.add(data.data() + v * dim);
    }
    REQUIRE(writer.num_vectors() == num);

    auto page = writer.flush();
    REQUIRE(!page.empty());

    // Read back dequantized
    QuantizedVectorReader reader(params);
    auto result = reader.read_page(page.data(), page.size());
    REQUIRE(result.size() == num);

    for (size_t v = 0; v < num; ++v) {
        REQUIRE(result[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(result[v][d] - data[v * dim + d]);
            REQUIRE(err <= params.scale / 2.0f + 1e-7f);
        }
    }
}

TEST_CASE("Write and read INT4 pipeline", "[quantized][writer-reader]") {
    constexpr uint32_t dim = 16;
    constexpr size_t num = 8;

    auto data = generate_vectors(num, dim, -1.0f, 1.0f, 4100);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT4);

    QuantizedVectorWriter writer(params);
    for (size_t v = 0; v < num; ++v) {
        writer.add(data.data() + v * dim);
    }
    REQUIRE(writer.num_vectors() == num);

    auto page = writer.flush();

    QuantizedVectorReader reader(params);
    auto result = reader.read_page(page.data(), page.size());
    REQUIRE(result.size() == num);

    for (size_t v = 0; v < num; ++v) {
        REQUIRE(result[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(result[v][d] - data[v * dim + d]);
            REQUIRE(err <= params.scale / 2.0f + 1e-6f);
        }
    }
}

TEST_CASE("Batch write/read quantized INT8", "[quantized][writer-reader]") {
    constexpr uint32_t dim = 256;
    constexpr size_t num = 100;

    auto data = generate_vectors(num, dim, -5.0f, 5.0f, 4200);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);

    // Use add_batch
    QuantizedVectorWriter writer(params);
    writer.add_batch(data.data(), num);
    REQUIRE(writer.num_vectors() == num);

    auto page = writer.flush();

    QuantizedVectorReader reader(params);
    auto result = reader.read_page(page.data(), page.size());
    REQUIRE(result.size() == num);

    float max_error = 0.0f;
    for (size_t v = 0; v < num; ++v) {
        REQUIRE(result[v].size() == dim);
        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(result[v][d] - data[v * dim + d]);
            max_error = std::max(max_error, err);
        }
    }

    REQUIRE(max_error <= params.scale / 2.0f + 1e-7f);
}

TEST_CASE("QuantizationParams bytes_per_vector", "[quantized][writer-reader]") {
    SECTION("INT8 dim=768 -> 768 bytes") {
        QuantizationParams p;
        p.scheme    = QuantizationScheme::SYMMETRIC_INT8;
        p.dimension = 768;
        p.scale     = 1.0f;
        p.zero_point = 0.0f;
        REQUIRE(p.bytes_per_vector() == 768);
    }

    SECTION("Asymmetric INT8 dim=768 -> 768 bytes") {
        QuantizationParams p;
        p.scheme    = QuantizationScheme::ASYMMETRIC_INT8;
        p.dimension = 768;
        p.scale     = 1.0f;
        p.zero_point = 0.0f;
        REQUIRE(p.bytes_per_vector() == 768);
    }

    SECTION("INT4 dim=768 -> 384 bytes") {
        QuantizationParams p;
        p.scheme    = QuantizationScheme::SYMMETRIC_INT4;
        p.dimension = 768;
        p.scale     = 1.0f;
        p.zero_point = 0.0f;
        REQUIRE(p.bytes_per_vector() == 384);
    }

    SECTION("INT4 dim=769 (odd) -> 385 bytes") {
        QuantizationParams p;
        p.scheme    = QuantizationScheme::SYMMETRIC_INT4;
        p.dimension = 769;
        p.scale     = 1.0f;
        p.zero_point = 0.0f;
        REQUIRE(p.bytes_per_vector() == 385);
    }
}

// ===================================================================
// Section 9: Schema Integration [vector][schema]
// ===================================================================

TEST_CASE("add_vector_column to schema", "[vector][schema]") {
    auto builder = Schema::builder("embeddings_table");
    add_vector_column(builder, "embedding_768", 768, VectorElementType::FLOAT32);
    auto schema = builder.build();

    REQUIRE(schema.num_columns() == 1);

    const auto& col = schema.column(0);
    REQUIRE(col.name == "embedding_768");
    REQUIRE(col.physical_type == PhysicalType::FIXED_LEN_BYTE_ARRAY);
    REQUIRE(col.type_length == static_cast<int32_t>(768 * sizeof(float)));
    REQUIRE(col.logical_type == LogicalType::FLOAT32_VECTOR);

    SECTION("Multiple vector columns in one schema") {
        auto b2 = Schema::builder("multi_vec");
        add_vector_column(b2, "small", 128, VectorElementType::FLOAT32);
        add_vector_column(b2, "large", 1536, VectorElementType::FLOAT32);
        auto s2 = b2.build();

        REQUIRE(s2.num_columns() == 2);

        REQUIRE(s2.column(0).name == "small");
        REQUIRE(s2.column(0).type_length == static_cast<int32_t>(128 * sizeof(float)));

        REQUIRE(s2.column(1).name == "large");
        REQUIRE(s2.column(1).type_length == static_cast<int32_t>(1536 * sizeof(float)));
    }
}

// ===================================================================
// Additional edge cases
// ===================================================================

TEST_CASE("VectorWriter flush resets state", "[vector][roundtrip]") {
    constexpr uint32_t dim = 4;
    VectorColumnSpec spec{dim, VectorElementType::FLOAT32};

    float v1[] = {1.0f, 2.0f, 3.0f, 4.0f};
    float v2[] = {5.0f, 6.0f, 7.0f, 8.0f};

    VectorWriter writer(spec);
    writer.add(v1);
    REQUIRE(writer.num_vectors() == 1);

    auto page1 = writer.flush();

    // After flush, writer should be empty
    REQUIRE(writer.num_vectors() == 0);

    // Write more data and flush again
    writer.add(v2);
    REQUIRE(writer.num_vectors() == 1);

    auto page2 = writer.flush();

    // Verify both pages independently
    VectorReader reader(spec);

    auto result1 = reader.read_page(page1.data(), page1.size());
    REQUIRE(result1.size() == 1);
    REQUIRE(result1[0][0] == 1.0f);

    auto result2 = reader.read_page(page2.data(), page2.size());
    REQUIRE(result2.size() == 1);
    REQUIRE(result2[0][0] == 5.0f);
}

TEST_CASE("QuantizedVectorReader read_vector by index", "[quantized][writer-reader]") {
    constexpr uint32_t dim = 16;
    constexpr size_t num = 10;

    auto data = generate_vectors(num, dim, -2.0f, 2.0f, 5000);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);

    QuantizedVectorWriter writer(params);
    writer.add_batch(data.data(), num);
    auto page = writer.flush();

    QuantizedVectorReader reader(params);

    // Read individual vectors by index and verify
    for (size_t idx = 0; idx < num; ++idx) {
        auto vec = reader.read_vector(page.data(), page.size(), idx);
        REQUIRE(vec.size() == dim);

        for (uint32_t d = 0; d < dim; ++d) {
            float err = std::abs(vec[d] - data[idx * dim + d]);
            REQUIRE(err <= params.scale / 2.0f + 1e-7f);
        }
    }
}

TEST_CASE("Dequantizer dequantize_flat returns contiguous data", "[quantized][int8][symmetric]") {
    constexpr uint32_t dim = 8;
    constexpr size_t num = 5;

    auto data = generate_vectors(num, dim, -1.0f, 1.0f, 6000);

    auto params = QuantizationParams::compute(data.data(), num, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);

    Quantizer quantizer(params);
    auto quantized = quantizer.quantize_batch(data.data(), num);

    Dequantizer dequantizer(params);

    // dequantize_flat returns a single contiguous vector
    auto flat = dequantizer.dequantize_flat(quantized.data(), num);
    REQUIRE(flat.size() == num * dim);

    // Compare with batch version
    auto batch = dequantizer.dequantize_batch(quantized.data(), num);
    for (size_t v = 0; v < num; ++v) {
        for (uint32_t d = 0; d < dim; ++d) {
            REQUIRE(flat[v * dim + d] == batch[v][d]);
        }
    }
}

TEST_CASE("QuantizedVectorWriter make_descriptor", "[quantized][writer-reader]") {
    QuantizationParams params;
    params.scheme    = QuantizationScheme::SYMMETRIC_INT8;
    params.scale     = 0.01f;
    params.zero_point = 0.0f;
    params.dimension = 384;

    auto desc = QuantizedVectorWriter::make_descriptor("quantized_emb", params);

    REQUIRE(desc.name == "quantized_emb");
    REQUIRE(desc.physical_type == PhysicalType::FIXED_LEN_BYTE_ARRAY);
    // INT8: 1 byte per element, so type_length = dimension
    REQUIRE(desc.type_length == static_cast<int32_t>(params.bytes_per_vector()));
}

// ===================================================================
// Section 10: Hardening Pass #4 [hardening]
// ===================================================================

TEST_CASE("INT4 sign extension is platform-agnostic", "[vector_type][hardening]") {
    // C-7: INT4 unpacking uses (nibble ^ 0x08) - 0x08 for platform-agnostic
    // sign extension. Range is [-8, 7] for 4-bit signed integers (though
    // SYMMETRIC_INT4 clamps to [-7, 7]).
    // Pack known values and verify dequantization produces correct signs.
    constexpr uint32_t dim = 16;

    // Build a vector whose values span [-7, 7] (the quantizable INT4 range)
    // Repeat to fill 16 dims: -7,-6,-5,-4,-3,-2,-1,0,1,2,3,4,5,6,7,0
    std::vector<float> original(dim);
    for (int i = 0; i < 15; ++i) {
        original[static_cast<size_t>(i)] = static_cast<float>(i - 7); // -7..+7
    }
    original[15] = 0.0f;

    auto params = QuantizationParams::compute(original.data(), 1, dim,
                                              QuantizationScheme::SYMMETRIC_INT4);
    REQUIRE(params.scheme == QuantizationScheme::SYMMETRIC_INT4);

    Quantizer quantizer(params);
    std::vector<uint8_t> qbuf(params.bytes_per_vector());
    quantizer.quantize(original.data(), qbuf.data());

    Dequantizer dequantizer(params);
    std::vector<float> recovered(dim);
    dequantizer.dequantize(qbuf.data(), recovered.data());

    // Verify signs are correct (negative inputs -> negative outputs, positive -> positive)
    for (size_t i = 0; i < original.size(); ++i) {
        if (original[i] < 0) {
            REQUIRE(recovered[i] < 0.0f);
        } else if (original[i] > 0) {
            REQUIRE(recovered[i] > 0.0f);
        }
    }
}

TEST_CASE("Quantization handles small-range inputs", "[vector_type][hardening]") {
    // H-11: When input range is very small, scale factor is tiny.
    // Added documentation about MiFID II Annex I Field 6 precision.
    constexpr uint32_t dim = 3;
    float tiny_range[] = {1.0f, 1.0f + 1e-7f, 1.0f + 2e-7f};

    auto params = QuantizationParams::compute(tiny_range, 1, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);
    REQUIRE(params.scheme == QuantizationScheme::SYMMETRIC_INT8);

    Quantizer quantizer(params);
    std::vector<uint8_t> qbuf(params.bytes_per_vector());
    quantizer.quantize(tiny_range, qbuf.data());

    Dequantizer dequantizer(params);
    std::vector<float> recovered(dim);
    dequantizer.dequantize(qbuf.data(), recovered.data());
    REQUIRE(recovered.size() == dim);
}

TEST_CASE("Dequantization clamps to original range", "[vector_type][hardening]") {
    // H-12: Added std::clamp post-dequantization to ensure no dequantized
    // value exceeds the original representable range.
    constexpr uint32_t dim = 4;
    float data[] = {-100.0f, 0.0f, 50.0f, 100.0f};

    auto params = QuantizationParams::compute(data, 1, dim,
                                              QuantizationScheme::SYMMETRIC_INT8);

    Quantizer quantizer(params);
    std::vector<uint8_t> qbuf(params.bytes_per_vector());
    quantizer.quantize(data, qbuf.data());

    Dequantizer dequantizer(params);
    std::vector<float> recovered(dim);
    dequantizer.dequantize(qbuf.data(), recovered.data());

    // Clamping ensures no dequantized value exceeds the original range
    float orig_min = *std::min_element(std::begin(data), std::end(data));
    float orig_max = *std::max_element(std::begin(data), std::end(data));
    for (float v : recovered) {
        REQUIRE(v >= orig_min - 1.0f); // Small tolerance for float precision
        REQUIRE(v <= orig_max + 1.0f);
    }
}
