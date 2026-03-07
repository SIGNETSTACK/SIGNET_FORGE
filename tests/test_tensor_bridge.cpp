// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "signet/ai/tensor_bridge.hpp"
#include "signet/interop/onnx_bridge.hpp"
#include "signet/interop/arrow_bridge.hpp"
#include "signet/interop/numpy_bridge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

using namespace signet::forge;

// ===================================================================
// Section 1: TensorShape [tensor][shape]
// ===================================================================

TEST_CASE("TensorShape num_elements", "[tensor][shape]") {
    SECTION("{3, 4} -> 12") {
        TensorShape shape{{3, 4}};
        REQUIRE(shape.num_elements() == 12);
    }

    SECTION("{1} -> 1") {
        TensorShape shape{{1}};
        REQUIRE(shape.num_elements() == 1);
    }

    SECTION("{} -> 1 (scalar)") {
        TensorShape shape;  // default = empty dims = scalar
        REQUIRE(shape.num_elements() == 1);
    }

    SECTION("{2, 3, 4} -> 24") {
        TensorShape shape{{2, 3, 4}};
        REQUIRE(shape.num_elements() == 24);
    }

    SECTION("{100} -> 100") {
        TensorShape shape{{100}};
        REQUIRE(shape.num_elements() == 100);
    }
}

TEST_CASE("TensorShape ndim/is_scalar/is_vector/is_matrix", "[tensor][shape]") {
    SECTION("Scalar: {} -> ndim=0, is_scalar, not vector, not matrix") {
        TensorShape shape;  // default = empty dims = scalar
        REQUIRE(shape.ndim() == 0);
        REQUIRE(shape.is_scalar());
        REQUIRE_FALSE(shape.is_vector());
        REQUIRE_FALSE(shape.is_matrix());
    }

    SECTION("Vector: {10} -> ndim=1, not scalar, is_vector, not matrix") {
        TensorShape shape{{10}};
        REQUIRE(shape.ndim() == 1);
        REQUIRE_FALSE(shape.is_scalar());
        REQUIRE(shape.is_vector());
        REQUIRE_FALSE(shape.is_matrix());
    }

    SECTION("Matrix: {3, 4} -> ndim=2, not scalar, not vector, is_matrix") {
        TensorShape shape{{3, 4}};
        REQUIRE(shape.ndim() == 2);
        REQUIRE_FALSE(shape.is_scalar());
        REQUIRE_FALSE(shape.is_vector());
        REQUIRE(shape.is_matrix());
    }

    SECTION("3D tensor: {2, 3, 4} -> ndim=3, none of the classifiers") {
        TensorShape shape{{2, 3, 4}};
        REQUIRE(shape.ndim() == 3);
        REQUIRE_FALSE(shape.is_scalar());
        REQUIRE_FALSE(shape.is_vector());
        REQUIRE_FALSE(shape.is_matrix());
    }
}

// ===================================================================
// Section 2: TensorView [tensor][view]
// ===================================================================

TEST_CASE("TensorView construct and access", "[tensor][view]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    TensorShape shape{{6}};

    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    REQUIRE(view.data() == static_cast<void*>(data.data()));
    REQUIRE(view.typed_data<float>() == data.data());
    REQUIRE(view.shape().dims == std::vector<int64_t>{6});
    REQUIRE(view.dtype() == TensorDataType::FLOAT32);
    REQUIRE(view.is_valid());
}

TEST_CASE("TensorView element_size for all types", "[tensor][view]") {
    float f32_data = 0.0f;
    TensorShape scalar{{}};

    SECTION("FLOAT32 = 4 bytes") {
        REQUIRE(tensor_element_size(TensorDataType::FLOAT32) == 4);
        TensorView view(&f32_data, scalar, TensorDataType::FLOAT32);
        REQUIRE(view.element_size() == 4);
    }

    SECTION("FLOAT64 = 8 bytes") {
        REQUIRE(tensor_element_size(TensorDataType::FLOAT64) == 8);
    }

    SECTION("INT32 = 4 bytes") {
        REQUIRE(tensor_element_size(TensorDataType::INT32) == 4);
    }

    SECTION("INT64 = 8 bytes") {
        REQUIRE(tensor_element_size(TensorDataType::INT64) == 8);
    }

    SECTION("INT8 = 1 byte") {
        REQUIRE(tensor_element_size(TensorDataType::INT8) == 1);
    }

    SECTION("UINT8 = 1 byte") {
        REQUIRE(tensor_element_size(TensorDataType::UINT8) == 1);
    }

    SECTION("INT16 = 2 bytes") {
        REQUIRE(tensor_element_size(TensorDataType::INT16) == 2);
    }

    SECTION("FLOAT16 = 2 bytes") {
        REQUIRE(tensor_element_size(TensorDataType::FLOAT16) == 2);
    }

    SECTION("BOOL = 1 byte") {
        REQUIRE(tensor_element_size(TensorDataType::BOOL) == 1);
    }
}

TEST_CASE("TensorView num_elements and byte_size", "[tensor][view]") {
    std::vector<float> data(30, 1.0f);
    TensorShape shape{{10, 3}};

    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    REQUIRE(view.num_elements() == 30);
    REQUIRE(view.byte_size() == 30 * sizeof(float));  // 120 bytes
}

TEST_CASE("TensorView slice", "[tensor][view]") {
    std::vector<float> data(10);
    std::iota(data.begin(), data.end(), 0.0f);  // 0, 1, 2, ..., 9
    TensorShape shape{{10}};

    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    // Slice [3, 5) -- elements at index 3 and 4
    TensorView sliced = view.slice(3, 2);

    // Data pointer should be offset by 3 * sizeof(float) from original
    REQUIRE(sliced.data() == static_cast<void*>(data.data() + 3));
    REQUIRE(sliced.typed_data<float>()[0] == Catch::Approx(3.0f));
    REQUIRE(sliced.typed_data<float>()[1] == Catch::Approx(4.0f));
    REQUIRE(sliced.num_elements() == 2);
    REQUIRE(sliced.shape().dims == std::vector<int64_t>{2});
}

TEST_CASE("TensorView reshape", "[tensor][view]") {
    std::vector<float> data(12, 1.0f);
    TensorShape shape{{12}};

    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    SECTION("{12} -> {3, 4} succeeds") {
        TensorShape new_shape{{3, 4}};
        auto result = view.reshape(new_shape);
        REQUIRE(result.has_value());

        auto& reshaped = result.value();
        REQUIRE(reshaped.shape().dims == std::vector<int64_t>{3, 4});
        REQUIRE(reshaped.num_elements() == 12);
        // Zero-copy: same data pointer
        REQUIRE(reshaped.data() == view.data());
    }

    SECTION("{12} -> {3, 5} fails (element count mismatch)") {
        TensorShape bad_shape{{3, 5}};
        auto result = view.reshape(bad_shape);
        REQUIRE_FALSE(result.has_value());
    }

    SECTION("{12} -> {2, 6} succeeds") {
        TensorShape new_shape{{2, 6}};
        auto result = view.reshape(new_shape);
        REQUIRE(result.has_value());
        REQUIRE(result.value().shape().dims == std::vector<int64_t>{2, 6});
    }

    SECTION("{12} -> {2, 2, 3} succeeds") {
        TensorShape new_shape{{2, 2, 3}};
        auto result = view.reshape(new_shape);
        REQUIRE(result.has_value());
        REQUIRE(result.value().num_elements() == 12);
    }
}

TEST_CASE("TensorView is_valid", "[tensor][view]") {
    SECTION("Constructed with data is valid") {
        float data = 1.0f;
        TensorView view(&data, TensorShape{{1}}, TensorDataType::FLOAT32);
        REQUIRE(view.is_valid());
    }

    SECTION("Constructed with nullptr is not valid") {
        TensorView view(static_cast<void*>(nullptr), TensorShape{{1}}, TensorDataType::FLOAT32);
        REQUIRE_FALSE(view.is_valid());
    }
}

TEST_CASE("TensorView is_contiguous", "[tensor][view]") {
    std::vector<float> data(10, 1.0f);
    TensorShape shape{{10}};

    SECTION("Default stride = contiguous") {
        TensorView view(data.data(), shape, TensorDataType::FLOAT32);
        REQUIRE(view.is_contiguous());
    }

    SECTION("Explicit stride matching element size = not contiguous (only stride 0 is contiguous)") {
        // Implementation defines contiguous as byte_stride == 0 (the sentinel value)
        TensorView view(data.data(), shape, TensorDataType::FLOAT32, sizeof(float));
        REQUIRE_FALSE(view.is_contiguous());
    }

    SECTION("Stride larger than element size = not contiguous") {
        TensorView view(data.data(), shape, TensorDataType::FLOAT32, sizeof(float) * 2);
        REQUIRE_FALSE(view.is_contiguous());
    }
}

// ===================================================================
// Section 3: OwnedTensor [tensor][owned]
// ===================================================================

TEST_CASE("OwnedTensor construct with shape", "[tensor][owned]") {
    TensorShape shape{{5, 3}};
    OwnedTensor tensor(shape, TensorDataType::FLOAT32);

    REQUIRE(tensor.byte_size() == 5 * 3 * sizeof(float));  // 60 bytes
    REQUIRE(tensor.shape().dims == std::vector<int64_t>{5, 3});
    REQUIRE(tensor.dtype() == TensorDataType::FLOAT32);
    REQUIRE(tensor.data() != nullptr);
}

TEST_CASE("OwnedTensor construct from data", "[tensor][owned]") {
    std::vector<float> source = {1.0f, 2.0f, 3.0f, 4.0f};
    TensorShape shape{{4}};

    OwnedTensor tensor(source.data(), shape, TensorDataType::FLOAT32);

    // Owned tensor should copy the data, not alias
    REQUIRE(tensor.data() != static_cast<void*>(source.data()));

    // Verify the copied values are correct
    auto* ptr = tensor.typed_data<float>();
    REQUIRE(ptr[0] == Catch::Approx(1.0f));
    REQUIRE(ptr[1] == Catch::Approx(2.0f));
    REQUIRE(ptr[2] == Catch::Approx(3.0f));
    REQUIRE(ptr[3] == Catch::Approx(4.0f));

    // Modify original -- owned copy should be unaffected
    source[0] = 999.0f;
    REQUIRE(ptr[0] == Catch::Approx(1.0f));
}

TEST_CASE("OwnedTensor clone", "[tensor][owned]") {
    std::vector<float> source = {10.0f, 20.0f, 30.0f};
    TensorShape shape{{3}};

    OwnedTensor original(source.data(), shape, TensorDataType::FLOAT32);
    OwnedTensor cloned = original.clone();

    // Clone has different data pointer
    REQUIRE(cloned.data() != original.data());

    // Clone has same values
    auto* orig_ptr = original.typed_data<float>();
    auto* clone_ptr = cloned.typed_data<float>();
    for (int i = 0; i < 3; ++i) {
        REQUIRE(clone_ptr[i] == Catch::Approx(orig_ptr[i]));
    }

    // Modify original, clone unaffected
    orig_ptr[0] = -1.0f;
    REQUIRE(clone_ptr[0] == Catch::Approx(10.0f));
}

TEST_CASE("OwnedTensor view", "[tensor][owned]") {
    TensorShape shape{{4}};
    OwnedTensor tensor(shape, TensorDataType::FLOAT32);

    TensorView v = tensor.view();

    // View has same data pointer as owned tensor
    REQUIRE(v.data() == tensor.data());
    REQUIRE(v.shape().dims == tensor.shape().dims);
    REQUIRE(v.dtype() == tensor.dtype());
    REQUIRE(v.num_elements() == 4);
}

TEST_CASE("OwnedTensor move semantics", "[tensor][owned]") {
    TensorShape shape{{8}};
    OwnedTensor original(shape, TensorDataType::FLOAT32);
    void* original_data = original.data();
    REQUIRE(original_data != nullptr);

    // Move construct
    OwnedTensor moved(std::move(original));

    // Moved-to has the original data
    REQUIRE(moved.data() == original_data);
    REQUIRE(moved.byte_size() == 8 * sizeof(float));

    // Moved-from should be empty / null
    REQUIRE(original.data() == nullptr);
}

// ===================================================================
// Section 4: ColumnToTensor [tensor][column]
// ===================================================================

TEST_CASE("ColumnToTensor wrap_column float", "[tensor][column]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

    auto result = ColumnToTensor::wrap_column(data.data(), 5, PhysicalType::FLOAT);
    REQUIRE(result.has_value());

    auto& view = result.value();
    // Zero-copy: same pointer
    REQUIRE(view.data() == static_cast<const void*>(data.data()));
    REQUIRE(view.dtype() == TensorDataType::FLOAT32);
    REQUIRE(view.num_elements() == 5);
    REQUIRE(view.shape().dims == std::vector<int64_t>{5});
}

TEST_CASE("ColumnToTensor wrap_column int64", "[tensor][column]") {
    std::vector<int64_t> data = {100, 200, 300};

    auto result = ColumnToTensor::wrap_column(data.data(), 3, PhysicalType::INT64);
    REQUIRE(result.has_value());

    auto& view = result.value();
    REQUIRE(view.data() == static_cast<const void*>(data.data()));
    REQUIRE(view.dtype() == TensorDataType::INT64);
    REQUIRE(view.num_elements() == 3);
}

TEST_CASE("ColumnToTensor wrap_vectors", "[tensor][column]") {
    constexpr int64_t num_vectors = 5;
    constexpr uint32_t dimension = 4;
    std::vector<float> data(num_vectors * dimension);
    std::iota(data.begin(), data.end(), 0.0f);

    auto result = ColumnToTensor::wrap_vectors(data.data(), num_vectors, dimension);
    REQUIRE(result.has_value());

    auto& view = result.value();
    // Zero-copy
    REQUIRE(view.data() == static_cast<const void*>(data.data()));
    // Shape should be {N, dim}
    REQUIRE(view.shape().dims == std::vector<int64_t>{num_vectors, dimension});
    REQUIRE(view.num_elements() == num_vectors * dimension);
    REQUIRE(view.dtype() == TensorDataType::FLOAT32);
}

TEST_CASE("ColumnToTensor copy_column with type cast", "[tensor][column]") {
    std::vector<int64_t> data = {10, 20, 30, 40, 50};

    auto result = ColumnToTensor::copy_column(
        data.data(), 5, PhysicalType::INT64, TensorDataType::FLOAT32);
    REQUIRE(result.has_value());

    auto& tensor = result.value();
    REQUIRE(tensor.dtype() == TensorDataType::FLOAT32);
    REQUIRE(tensor.shape().dims == std::vector<int64_t>{5});

    // Data should be DIFFERENT pointer (copy was made)
    REQUIRE(tensor.data() != static_cast<const void*>(data.data()));

    // Values should be correctly cast
    auto* float_data = tensor.typed_data<float>();
    REQUIRE(float_data[0] == Catch::Approx(10.0f));
    REQUIRE(float_data[1] == Catch::Approx(20.0f));
    REQUIRE(float_data[2] == Catch::Approx(30.0f));
    REQUIRE(float_data[3] == Catch::Approx(40.0f));
    REQUIRE(float_data[4] == Catch::Approx(50.0f));
}

TEST_CASE("ColumnToTensor cast", "[tensor][column]") {
    std::vector<double> data = {1.5, 2.5, 3.5, 4.5};
    TensorShape shape{{4}};
    TensorView src(const_cast<double*>(data.data()), shape, TensorDataType::FLOAT64);

    auto result = ColumnToTensor::cast(src, TensorDataType::FLOAT32);
    REQUIRE(result.has_value());

    auto& tensor = result.value();
    REQUIRE(tensor.dtype() == TensorDataType::FLOAT32);

    auto* float_data = tensor.typed_data<float>();
    REQUIRE(float_data[0] == Catch::Approx(1.5f));
    REQUIRE(float_data[1] == Catch::Approx(2.5f));
    REQUIRE(float_data[2] == Catch::Approx(3.5f));
    REQUIRE(float_data[3] == Catch::Approx(4.5f));
}

TEST_CASE("ColumnToTensor parquet_to_tensor_dtype", "[tensor][column]") {
    SECTION("FLOAT -> FLOAT32") {
        auto result = ColumnToTensor::parquet_to_tensor_dtype(PhysicalType::FLOAT);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::FLOAT32);
    }

    SECTION("DOUBLE -> FLOAT64") {
        auto result = ColumnToTensor::parquet_to_tensor_dtype(PhysicalType::DOUBLE);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::FLOAT64);
    }

    SECTION("INT32 -> INT32") {
        auto result = ColumnToTensor::parquet_to_tensor_dtype(PhysicalType::INT32);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::INT32);
    }

    SECTION("INT64 -> INT64") {
        auto result = ColumnToTensor::parquet_to_tensor_dtype(PhysicalType::INT64);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::INT64);
    }

    SECTION("BOOLEAN -> BOOL") {
        auto result = ColumnToTensor::parquet_to_tensor_dtype(PhysicalType::BOOLEAN);
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::BOOL);
    }
}

// ===================================================================
// Section 5: BatchTensorBuilder [tensor][batch]
// ===================================================================

TEST_CASE("BatchTensorBuilder single column", "[tensor][batch]") {
    std::vector<float> col(10, 1.0f);
    TensorView col_view(col.data(), TensorShape{{10}}, TensorDataType::FLOAT32);

    BatchTensorBuilder builder;
    builder.add_column("feature_0", col_view);

    REQUIRE(builder.num_features() == 1);

    auto result = builder.build(TensorDataType::FLOAT32);
    REQUIRE(result.has_value());

    auto& tensor = result.value();
    // Single column of 10 values -> {10, 1}
    REQUIRE(tensor.shape().dims == std::vector<int64_t>{10, 1});
    REQUIRE(tensor.dtype() == TensorDataType::FLOAT32);

    auto* data = tensor.typed_data<float>();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(data[i] == Catch::Approx(1.0f));
    }
}

TEST_CASE("BatchTensorBuilder multiple columns", "[tensor][batch]") {
    constexpr int64_t rows = 10;
    std::vector<float> col_a(rows), col_b(rows), col_c(rows);
    for (int64_t i = 0; i < rows; ++i) {
        col_a[i] = static_cast<float>(i);
        col_b[i] = static_cast<float>(i * 10);
        col_c[i] = static_cast<float>(i * 100);
    }

    TensorView va(col_a.data(), TensorShape{{rows}}, TensorDataType::FLOAT32);
    TensorView vb(col_b.data(), TensorShape{{rows}}, TensorDataType::FLOAT32);
    TensorView vc(col_c.data(), TensorShape{{rows}}, TensorDataType::FLOAT32);

    BatchTensorBuilder builder;
    builder.add_column("a", va);
    builder.add_column("b", vb);
    builder.add_column("c", vc);

    REQUIRE(builder.num_features() == 3);

    auto result = builder.build(TensorDataType::FLOAT32);
    REQUIRE(result.has_value());

    auto& tensor = result.value();
    // 3 scalar columns of 10 values -> {10, 3}
    REQUIRE(tensor.shape().dims == std::vector<int64_t>{rows, 3});

    // Verify data layout: row-major, so row i = [a[i], b[i], c[i]]
    auto* data = tensor.typed_data<float>();
    for (int64_t i = 0; i < rows; ++i) {
        REQUIRE(data[i * 3 + 0] == Catch::Approx(static_cast<float>(i)));
        REQUIRE(data[i * 3 + 1] == Catch::Approx(static_cast<float>(i * 10)));
        REQUIRE(data[i * 3 + 2] == Catch::Approx(static_cast<float>(i * 100)));
    }
}

TEST_CASE("BatchTensorBuilder mixed scalar + vector", "[tensor][batch]") {
    constexpr int64_t rows = 10;
    constexpr int64_t vec_dim = 4;

    // 1 scalar column
    std::vector<float> scalar_col(rows);
    for (int64_t i = 0; i < rows; ++i) scalar_col[i] = static_cast<float>(i);

    // 1 vector column (dim=4)
    std::vector<float> vec_col(rows * vec_dim);
    for (int64_t i = 0; i < rows * vec_dim; ++i) vec_col[i] = static_cast<float>(i) * 0.1f;

    TensorView scalar_view(scalar_col.data(), TensorShape{{rows}}, TensorDataType::FLOAT32);
    TensorView vec_view(vec_col.data(), TensorShape{{rows, vec_dim}}, TensorDataType::FLOAT32);

    BatchTensorBuilder builder;
    builder.add_column("scalar_feat", scalar_view);
    builder.add_column("vec_feat", vec_view);

    // num_features() returns the number of column sources added (2)
    REQUIRE(builder.num_features() == 2);

    auto result = builder.build(TensorDataType::FLOAT32);
    REQUIRE(result.has_value());

    auto& tensor = result.value();
    // Output shape: {10, 5}
    REQUIRE(tensor.shape().dims == std::vector<int64_t>{rows, 5});
}

TEST_CASE("BatchTensorBuilder empty builder returns error", "[tensor][batch]") {
    BatchTensorBuilder builder;
    REQUIRE(builder.num_features() == 0);

    auto result = builder.build(TensorDataType::FLOAT32);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("BatchTensorBuilder expected_shape", "[tensor][batch]") {
    std::vector<float> col_a(8, 0.0f);
    std::vector<float> col_b(8, 0.0f);

    TensorView va(col_a.data(), TensorShape{{8}}, TensorDataType::FLOAT32);
    TensorView vb(col_b.data(), TensorShape{{8}}, TensorDataType::FLOAT32);

    BatchTensorBuilder builder;
    builder.add_column("a", va);
    builder.add_column("b", vb);

    auto shape = builder.expected_shape();
    REQUIRE(shape.dims == std::vector<int64_t>{8, 2});
}

// ===================================================================
// Section 6: ONNX Bridge [interop][onnx]
// ===================================================================

TEST_CASE("to_onnx_type / from_onnx_type roundtrip", "[interop][onnx]") {
    SECTION("FLOAT32 <-> FLOAT(1)") {
        auto onnx_type = to_onnx_type(TensorDataType::FLOAT32);
        REQUIRE(onnx_type == OnnxTensorType::FLOAT);
        REQUIRE(static_cast<int32_t>(onnx_type) == 1);

        auto back = from_onnx_type(OnnxTensorType::FLOAT);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::FLOAT32);
    }

    SECTION("INT64 <-> INT64(7)") {
        auto onnx_type = to_onnx_type(TensorDataType::INT64);
        REQUIRE(static_cast<int32_t>(onnx_type) == 7);

        auto back = from_onnx_type(onnx_type);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::INT64);
    }

    SECTION("FLOAT64 <-> DOUBLE(11)") {
        auto onnx_type = to_onnx_type(TensorDataType::FLOAT64);
        REQUIRE(static_cast<int32_t>(onnx_type) == 11);
        REQUIRE(onnx_type == OnnxTensorType::DOUBLE);

        auto back = from_onnx_type(OnnxTensorType::DOUBLE);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::FLOAT64);
    }

    SECTION("INT32 <-> INT32(6)") {
        auto onnx_type = to_onnx_type(TensorDataType::INT32);
        REQUIRE(static_cast<int32_t>(onnx_type) == 6);

        auto back = from_onnx_type(onnx_type);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::INT32);
    }

    SECTION("UINT8 <-> UINT8(2)") {
        auto onnx_type = to_onnx_type(TensorDataType::UINT8);
        REQUIRE(onnx_type == OnnxTensorType::UINT8);

        auto back = from_onnx_type(OnnxTensorType::UINT8);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::UINT8);
    }

    SECTION("INT8 <-> INT8(3)") {
        auto onnx_type = to_onnx_type(TensorDataType::INT8);
        REQUIRE(onnx_type == OnnxTensorType::INT8);

        auto back = from_onnx_type(OnnxTensorType::INT8);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::INT8);
    }

    SECTION("UNDEFINED returns error") {
        auto back = from_onnx_type(OnnxTensorType::UNDEFINED);
        REQUIRE_FALSE(back.has_value());
    }
}

TEST_CASE("prepare_for_onnx TensorView", "[interop][onnx]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    TensorShape shape{{2, 3}};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    auto result = prepare_for_onnx(view);
    REQUIRE(result.has_value());

    auto& info = result.value();
    REQUIRE(info.is_valid());
    // Data pointer should match the view's data
    REQUIRE(info.data == static_cast<void*>(data.data()));
    REQUIRE(info.shape == std::vector<int64_t>{2, 3});
    REQUIRE(info.element_type == OnnxTensorType::FLOAT);
    REQUIRE(info.byte_size == 6 * sizeof(float));
    // View-based: not an owner
    REQUIRE_FALSE(info.is_owner);
}

TEST_CASE("prepare_for_onnx OwnedTensor", "[interop][onnx]") {
    std::vector<float> source = {10.0f, 20.0f, 30.0f};
    TensorShape shape{{3}};
    OwnedTensor tensor(source.data(), shape, TensorDataType::FLOAT32);

    auto result = prepare_for_onnx(tensor);
    REQUIRE(result.has_value());

    auto& info = result.value();
    REQUIRE(info.is_valid());
    // Data pointer should match the owned tensor's data
    REQUIRE(info.data == tensor.data());
    REQUIRE(info.shape == std::vector<int64_t>{3});
    REQUIRE(info.element_type == OnnxTensorType::FLOAT);
    REQUIRE(info.byte_size == 3 * sizeof(float));
}

// ===================================================================
// Section 7: Arrow Bridge [interop][arrow]
// ===================================================================

TEST_CASE("ArrowExporter export_tensor float32", "[interop][arrow]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    TensorShape shape{{4}};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    ArrowArray array{};
    ArrowSchema schema{};

    auto result = ArrowExporter::export_tensor(view, "test_col", &array, &schema);
    REQUIRE(result.has_value());

    // Verify ArrowArray
    REQUIRE(array.length == 4);
    REQUIRE(array.null_count == 0);
    REQUIRE(array.n_buffers >= 2);
    // buffers[1] is the data buffer, should point to same data (zero-copy)
    REQUIRE(array.buffers[1] == static_cast<const void*>(data.data()));

    // Verify ArrowSchema
    REQUIRE(schema.format != nullptr);
    REQUIRE(std::string(schema.format) == "f");  // float32
    REQUIRE(schema.name != nullptr);
    REQUIRE(std::string(schema.name) == "test_col");

    // Release callbacks
    if (array.release) array.release(&array);
    if (schema.release) schema.release(&schema);
}

TEST_CASE("ArrowExporter export_column int64", "[interop][arrow]") {
    std::vector<int64_t> data = {100, 200, 300, 400, 500};

    ArrowArray array{};
    ArrowSchema schema{};

    auto result = ArrowExporter::export_column(
        data.data(), 5, PhysicalType::INT64, "ids", &array, &schema);
    REQUIRE(result.has_value());

    REQUIRE(array.length == 5);
    REQUIRE(schema.format != nullptr);
    REQUIRE(std::string(schema.format) == "l");  // int64

    if (array.release) array.release(&array);
    if (schema.release) schema.release(&schema);
}

TEST_CASE("ArrowImporter import_array zero-copy", "[interop][arrow]") {
    // First export a tensor
    std::vector<float> data = {10.0f, 20.0f, 30.0f};
    TensorShape shape{{3}};
    TensorView original(data.data(), shape, TensorDataType::FLOAT32);

    ArrowArray array{};
    ArrowSchema schema{};

    auto exp = ArrowExporter::export_tensor(original, "col", &array, &schema);
    REQUIRE(exp.has_value());

    // Import back
    auto imp = ArrowImporter::import_array(&array, &schema);
    REQUIRE(imp.has_value());

    auto& imported = imp.value();
    // Zero-copy: same data pointer
    REQUIRE(imported.data() == static_cast<void*>(data.data()));
    REQUIRE(imported.dtype() == TensorDataType::FLOAT32);
    REQUIRE(imported.num_elements() == 3);

    // Verify values
    auto* typed = imported.typed_data<float>();
    REQUIRE(typed[0] == Catch::Approx(10.0f));
    REQUIRE(typed[1] == Catch::Approx(20.0f));
    REQUIRE(typed[2] == Catch::Approx(30.0f));

    if (array.release) array.release(&array);
    if (schema.release) schema.release(&schema);
}

TEST_CASE("ArrowImporter import_array_copy", "[interop][arrow]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    TensorShape shape{{4}};
    TensorView original(data.data(), shape, TensorDataType::FLOAT32);

    ArrowArray array{};
    ArrowSchema schema{};

    auto exp = ArrowExporter::export_tensor(original, "col", &array, &schema);
    REQUIRE(exp.has_value());

    // Import as copy
    auto imp = ArrowImporter::import_array_copy(&array, &schema);
    REQUIRE(imp.has_value());

    auto& copied = imp.value();
    // Copy: different data pointer
    REQUIRE(copied.data() != static_cast<void*>(data.data()));

    // Values match
    auto* typed = copied.typed_data<float>();
    REQUIRE(typed[0] == Catch::Approx(1.0f));
    REQUIRE(typed[1] == Catch::Approx(2.0f));
    REQUIRE(typed[2] == Catch::Approx(3.0f));
    REQUIRE(typed[3] == Catch::Approx(4.0f));

    // Modify original, verify copy is unaffected
    data[0] = 999.0f;
    REQUIRE(typed[0] == Catch::Approx(1.0f));

    if (array.release) array.release(&array);
    if (schema.release) schema.release(&schema);
}

TEST_CASE("parquet_to_arrow_format", "[interop][arrow]") {
    SECTION("FLOAT -> f") {
        REQUIRE(std::string(parquet_to_arrow_format(PhysicalType::FLOAT)) == "f");
    }

    SECTION("DOUBLE -> g") {
        REQUIRE(std::string(parquet_to_arrow_format(PhysicalType::DOUBLE)) == "g");
    }

    SECTION("INT32 -> i") {
        REQUIRE(std::string(parquet_to_arrow_format(PhysicalType::INT32)) == "i");
    }

    SECTION("INT64 -> l") {
        REQUIRE(std::string(parquet_to_arrow_format(PhysicalType::INT64)) == "l");
    }

    SECTION("BOOLEAN -> b") {
        REQUIRE(std::string(parquet_to_arrow_format(PhysicalType::BOOLEAN)) == "b");
    }
}

TEST_CASE("arrow_format_to_tensor_dtype", "[interop][arrow]") {
    SECTION("f -> FLOAT32") {
        auto result = arrow_format_to_tensor_dtype("f");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::FLOAT32);
    }

    SECTION("g -> FLOAT64") {
        auto result = arrow_format_to_tensor_dtype("g");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::FLOAT64);
    }

    SECTION("i -> INT32") {
        auto result = arrow_format_to_tensor_dtype("i");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::INT32);
    }

    SECTION("l -> INT64") {
        auto result = arrow_format_to_tensor_dtype("l");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::INT64);
    }

    SECTION("b -> BOOL") {
        auto result = arrow_format_to_tensor_dtype("b");
        REQUIRE(result.has_value());
        REQUIRE(result.value() == TensorDataType::BOOL);
    }

    SECTION("Unknown format returns error") {
        auto result = arrow_format_to_tensor_dtype("Z");
        REQUIRE_FALSE(result.has_value());
    }
}

TEST_CASE("Arrow release callbacks invalidate structs", "[interop][arrow]") {
    std::vector<float> data = {1.0f, 2.0f};
    TensorShape shape{{2}};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    ArrowArray array{};
    ArrowSchema schema{};

    auto result = ArrowExporter::export_tensor(view, "test", &array, &schema);
    REQUIRE(result.has_value());

    // Schema should have a release callback
    REQUIRE(schema.release != nullptr);
    REQUIRE(array.release != nullptr);

    // Call release on schema
    auto schema_release = schema.release;
    schema_release(&schema);
    // After release, the release pointer should be set to nullptr (Arrow C ABI convention)
    REQUIRE(schema.release == nullptr);

    // Call release on array
    auto array_release = array.release;
    array_release(&array);
    REQUIRE(array.release == nullptr);
}

// ===================================================================
// Section 8: DLPack / NumPy Bridge [interop][numpy]
// ===================================================================

TEST_CASE("to_dlpack_dtype / from_dlpack_dtype roundtrip", "[interop][numpy]") {
    SECTION("FLOAT32 <-> {DLDataTypeCode::kDLFloat, 32, 1}") {
        DLDataType dl_dtype = to_dlpack_dtype(TensorDataType::FLOAT32);
        REQUIRE(dl_dtype.code == DLDataTypeCode::kDLFloat);
        REQUIRE(dl_dtype.bits == 32);
        REQUIRE(dl_dtype.lanes == 1);

        auto back = from_dlpack_dtype(dl_dtype);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::FLOAT32);
    }

    SECTION("FLOAT64 <-> {DLDataTypeCode::kDLFloat, 64, 1}") {
        DLDataType dl_dtype = to_dlpack_dtype(TensorDataType::FLOAT64);
        REQUIRE(dl_dtype.code == DLDataTypeCode::kDLFloat);
        REQUIRE(dl_dtype.bits == 64);
        REQUIRE(dl_dtype.lanes == 1);

        auto back = from_dlpack_dtype(dl_dtype);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::FLOAT64);
    }

    SECTION("INT32 <-> {DLDataTypeCode::kDLInt, 32, 1}") {
        DLDataType dl_dtype = to_dlpack_dtype(TensorDataType::INT32);
        REQUIRE(dl_dtype.code == DLDataTypeCode::kDLInt);
        REQUIRE(dl_dtype.bits == 32);
        REQUIRE(dl_dtype.lanes == 1);

        auto back = from_dlpack_dtype(dl_dtype);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::INT32);
    }

    SECTION("INT64 <-> {DLDataTypeCode::kDLInt, 64, 1}") {
        DLDataType dl_dtype = to_dlpack_dtype(TensorDataType::INT64);
        REQUIRE(dl_dtype.code == DLDataTypeCode::kDLInt);
        REQUIRE(dl_dtype.bits == 64);
        REQUIRE(dl_dtype.lanes == 1);

        auto back = from_dlpack_dtype(dl_dtype);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::INT64);
    }

    SECTION("INT8 <-> {DLDataTypeCode::kDLInt, 8, 1}") {
        DLDataType dl_dtype = to_dlpack_dtype(TensorDataType::INT8);
        REQUIRE(dl_dtype.code == DLDataTypeCode::kDLInt);
        REQUIRE(dl_dtype.bits == 8);
        REQUIRE(dl_dtype.lanes == 1);

        auto back = from_dlpack_dtype(dl_dtype);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::INT8);
    }

    SECTION("UINT8 <-> {DLDataTypeCode::kDLUInt, 8, 1}") {
        DLDataType dl_dtype = to_dlpack_dtype(TensorDataType::UINT8);
        REQUIRE(dl_dtype.code == DLDataTypeCode::kDLUInt);
        REQUIRE(dl_dtype.bits == 8);
        REQUIRE(dl_dtype.lanes == 1);

        auto back = from_dlpack_dtype(dl_dtype);
        REQUIRE(back.has_value());
        REQUIRE(back.value() == TensorDataType::UINT8);
    }
}

TEST_CASE("NumpyBridge export_tensor DLPack", "[interop][numpy]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    TensorShape shape{{2, 3}};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    auto result = NumpyBridge::export_tensor(view);
    REQUIRE(result.has_value());

    DLManagedTensor* managed = result.value();
    REQUIRE(managed != nullptr);

    auto& dl = managed->dl_tensor;

    // Data pointer should match (zero-copy from view)
    REQUIRE(dl.data == static_cast<void*>(data.data()));

    // Shape
    REQUIRE(dl.ndim == 2);
    REQUIRE(dl.shape[0] == 2);
    REQUIRE(dl.shape[1] == 3);

    // Device type should be CPU
    REQUIRE(dl.device.device_type == DLDeviceType::kDLCPU);
    REQUIRE(dl.device.device_id == 0);

    // Data type
    REQUIRE(dl.dtype.code == DLDataTypeCode::kDLFloat);
    REQUIRE(dl.dtype.bits == 32);
    REQUIRE(dl.dtype.lanes == 1);

    // Clean up
    REQUIRE(managed->deleter != nullptr);
    managed->deleter(managed);
}

TEST_CASE("NumpyBridge export_owned_tensor", "[interop][numpy]") {
    std::vector<float> source = {10.0f, 20.0f, 30.0f};
    TensorShape shape{{3}};
    OwnedTensor tensor(source.data(), shape, TensorDataType::FLOAT32);
    void* tensor_data = tensor.data();

    auto result = NumpyBridge::export_owned_tensor(std::move(tensor));
    REQUIRE(result.has_value());

    DLManagedTensor* managed = result.value();
    REQUIRE(managed != nullptr);

    // Data should be the owned tensor's data (transferred ownership)
    REQUIRE(managed->dl_tensor.data == tensor_data);
    REQUIRE(managed->dl_tensor.ndim == 1);
    REQUIRE(managed->dl_tensor.shape[0] == 3);

    // Verify values through DLPack
    auto* fdata = static_cast<float*>(managed->dl_tensor.data);
    REQUIRE(fdata[0] == Catch::Approx(10.0f));
    REQUIRE(fdata[1] == Catch::Approx(20.0f));
    REQUIRE(fdata[2] == Catch::Approx(30.0f));

    // Call deleter -- should free the underlying memory without leak
    managed->deleter(managed);
}

TEST_CASE("NumpyBridge import_tensor zero-copy", "[interop][numpy]") {
    std::vector<float> data = {5.0f, 6.0f, 7.0f, 8.0f};
    TensorShape shape{{4}};
    TensorView original(data.data(), shape, TensorDataType::FLOAT32);

    // Export
    auto exp = NumpyBridge::export_tensor(original);
    REQUIRE(exp.has_value());
    DLManagedTensor* managed = exp.value();

    // Import
    auto imp = NumpyBridge::import_tensor(managed);
    REQUIRE(imp.has_value());

    auto& imported = imp.value();
    // Zero-copy: same data pointer
    REQUIRE(imported.data() == static_cast<void*>(data.data()));
    REQUIRE(imported.dtype() == TensorDataType::FLOAT32);
    REQUIRE(imported.num_elements() == 4);

    // Verify values
    auto* typed = imported.typed_data<float>();
    REQUIRE(typed[0] == Catch::Approx(5.0f));
    REQUIRE(typed[1] == Catch::Approx(6.0f));
    REQUIRE(typed[2] == Catch::Approx(7.0f));
    REQUIRE(typed[3] == Catch::Approx(8.0f));

    // Clean up
    managed->deleter(managed);
}

TEST_CASE("NumpyBridge import_tensor_copy", "[interop][numpy]") {
    std::vector<float> data = {100.0f, 200.0f};
    TensorShape shape{{2}};
    TensorView original(data.data(), shape, TensorDataType::FLOAT32);

    auto exp = NumpyBridge::export_tensor(original);
    REQUIRE(exp.has_value());
    DLManagedTensor* managed = exp.value();

    auto imp = NumpyBridge::import_tensor_copy(managed);
    REQUIRE(imp.has_value());

    auto& copied = imp.value();
    // Copy: different data pointer
    REQUIRE(copied.data() != static_cast<void*>(data.data()));

    auto* typed = copied.typed_data<float>();
    REQUIRE(typed[0] == Catch::Approx(100.0f));
    REQUIRE(typed[1] == Catch::Approx(200.0f));

    // Modify original, verify copy is unaffected
    data[0] = -1.0f;
    REQUIRE(typed[0] == Catch::Approx(100.0f));

    managed->deleter(managed);
}

TEST_CASE("to_buffer_info", "[interop][numpy]") {
    SECTION("FLOAT32 1D") {
        std::vector<float> data = {1.0f, 2.0f, 3.0f};
        TensorView view(data.data(), TensorShape{{3}}, TensorDataType::FLOAT32);

        auto result = to_buffer_info(view);
        REQUIRE(result.has_value());

        auto& info = result.value();
        REQUIRE(info.data == static_cast<void*>(data.data()));
        REQUIRE(info.itemsize == sizeof(float));
        REQUIRE(info.format == "f");
        REQUIRE(info.ndim == 1);
        REQUIRE(info.shape == std::vector<int64_t>{3});
        REQUIRE(info.strides.size() == 1);
        REQUIRE(info.strides[0] == static_cast<int64_t>(sizeof(float)));
    }

    SECTION("FLOAT32 2D") {
        std::vector<float> data(12, 1.0f);
        TensorView view(data.data(), TensorShape{{3, 4}}, TensorDataType::FLOAT32);

        auto result = to_buffer_info(view);
        REQUIRE(result.has_value());

        auto& info = result.value();
        REQUIRE(info.ndim == 2);
        REQUIRE(info.shape == std::vector<int64_t>{3, 4});
        REQUIRE(info.strides.size() == 2);
        // Row-major strides: stride[0] = 4 * sizeof(float), stride[1] = sizeof(float)
        REQUIRE(info.strides[0] == static_cast<int64_t>(4 * sizeof(float)));
        REQUIRE(info.strides[1] == static_cast<int64_t>(sizeof(float)));
    }

    SECTION("FLOAT64 1D") {
        std::vector<double> data = {1.0, 2.0};
        TensorView view(data.data(), TensorShape{{2}}, TensorDataType::FLOAT64);

        auto result = to_buffer_info(view);
        REQUIRE(result.has_value());

        auto& info = result.value();
        REQUIRE(info.itemsize == sizeof(double));
        REQUIRE(info.format == "d");
    }

    SECTION("INT32 1D") {
        std::vector<int32_t> data = {1, 2, 3};
        TensorView view(data.data(), TensorShape{{3}}, TensorDataType::INT32);

        auto result = to_buffer_info(view);
        REQUIRE(result.has_value());

        auto& info = result.value();
        REQUIRE(info.itemsize == sizeof(int32_t));
        REQUIRE(info.format == "i");
    }

    SECTION("INT64 1D") {
        std::vector<int64_t> data = {1, 2, 3};
        TensorView view(data.data(), TensorShape{{3}}, TensorDataType::INT64);

        auto result = to_buffer_info(view);
        REQUIRE(result.has_value());

        auto& info = result.value();
        REQUIRE(info.itemsize == sizeof(int64_t));
        REQUIRE(info.format == "l");
    }
}

// ===================================================================
// Section 9: Edge Cases and Integration [tensor][edge]
// ===================================================================

TEST_CASE("TensorView const correctness", "[tensor][view]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f};
    TensorShape shape{{3}};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    // Non-const access
    REQUIRE(view.data() != nullptr);
    REQUIRE(view.typed_data<float>() == data.data());

    // Const view
    const TensorView& cview = view;
    REQUIRE(cview.data() != nullptr);
    REQUIRE(cview.shape().dims == std::vector<int64_t>{3});
    REQUIRE(cview.dtype() == TensorDataType::FLOAT32);
}

TEST_CASE("OwnedTensor from zero-filled allocation", "[tensor][owned]") {
    TensorShape shape{{100}};
    OwnedTensor tensor(shape, TensorDataType::FLOAT32);

    // Allocated memory, write known values
    auto* ptr = tensor.typed_data<float>();
    for (int i = 0; i < 100; ++i) {
        ptr[i] = static_cast<float>(i);
    }

    // Verify through view
    TensorView v = tensor.view();
    auto* vptr = v.typed_data<float>();
    for (int i = 0; i < 100; ++i) {
        REQUIRE(vptr[i] == Catch::Approx(static_cast<float>(i)));
    }
}

TEST_CASE("ColumnToTensor wrap_column int32", "[tensor][column]") {
    std::vector<int32_t> data = {-10, 0, 10, 20, 30};

    auto result = ColumnToTensor::wrap_column(data.data(), 5, PhysicalType::INT32);
    REQUIRE(result.has_value());

    auto& view = result.value();
    REQUIRE(view.data() == static_cast<const void*>(data.data()));
    REQUIRE(view.dtype() == TensorDataType::INT32);
    REQUIRE(view.num_elements() == 5);
}

TEST_CASE("ColumnToTensor wrap_column double", "[tensor][column]") {
    std::vector<double> data = {1.1, 2.2, 3.3};

    auto result = ColumnToTensor::wrap_column(data.data(), 3, PhysicalType::DOUBLE);
    REQUIRE(result.has_value());

    auto& view = result.value();
    REQUIRE(view.data() == static_cast<const void*>(data.data()));
    REQUIRE(view.dtype() == TensorDataType::FLOAT64);
    REQUIRE(view.num_elements() == 3);
}

TEST_CASE("Large tensor batch build end-to-end", "[tensor][batch]") {
    // Simulate a realistic ML feature matrix: 1000 rows, 8 scalar features
    constexpr int64_t rows = 1000;
    constexpr int num_cols = 8;

    std::vector<std::vector<float>> columns(num_cols, std::vector<float>(rows));
    for (int c = 0; c < num_cols; ++c) {
        for (int64_t r = 0; r < rows; ++r) {
            columns[c][r] = static_cast<float>(c * 1000 + r) * 0.001f;
        }
    }

    BatchTensorBuilder builder;
    for (int c = 0; c < num_cols; ++c) {
        TensorView col_view(columns[c].data(), TensorShape{{rows}}, TensorDataType::FLOAT32);
        builder.add_column("feat_" + std::to_string(c), col_view);
    }

    REQUIRE(builder.num_features() == num_cols);

    auto result = builder.build(TensorDataType::FLOAT32);
    REQUIRE(result.has_value());

    auto& tensor = result.value();
    REQUIRE(tensor.shape().dims == std::vector<int64_t>{rows, num_cols});
    REQUIRE(tensor.byte_size() == rows * num_cols * sizeof(float));

    // Spot-check a few values in the output matrix
    auto* data = tensor.typed_data<float>();
    // Row 0, col 0 = 0.0
    REQUIRE(data[0 * num_cols + 0] == Catch::Approx(0.0f));
    // Row 0, col 7 = 7 * 1000 * 0.001 = 7.0
    REQUIRE(data[0 * num_cols + 7] == Catch::Approx(7.0f));
    // Row 999, col 0 = 999 * 0.001 = 0.999
    REQUIRE(data[999 * num_cols + 0] == Catch::Approx(0.999f));
    // Row 500, col 3 = (3*1000 + 500) * 0.001 = 3.5
    REQUIRE(data[500 * num_cols + 3] == Catch::Approx(3.5f));
}

TEST_CASE("ONNX bridge with 2D tensor", "[interop][onnx]") {
    // {batch_size=4, features=3}
    std::vector<float> data(12);
    std::iota(data.begin(), data.end(), 0.0f);
    TensorShape shape{{4, 3}};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    auto result = prepare_for_onnx(view);
    REQUIRE(result.has_value());

    auto& info = result.value();
    REQUIRE(info.shape.size() == 2);
    REQUIRE(info.shape[0] == 4);
    REQUIRE(info.shape[1] == 3);
    REQUIRE(info.byte_size == 12 * sizeof(float));
}

TEST_CASE("DLPack 2D tensor strides", "[interop][numpy]") {
    std::vector<float> data(6);
    std::iota(data.begin(), data.end(), 1.0f);
    TensorShape shape{{2, 3}};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    auto result = NumpyBridge::export_tensor(view);
    REQUIRE(result.has_value());

    DLManagedTensor* managed = result.value();
    auto& dl = managed->dl_tensor;

    REQUIRE(dl.ndim == 2);
    REQUIRE(dl.shape[0] == 2);
    REQUIRE(dl.shape[1] == 3);

    // If strides are provided, verify row-major
    if (dl.strides != nullptr) {
        // Row-major strides in element count: stride[0] = 3, stride[1] = 1
        REQUIRE(dl.strides[0] == 3);
        REQUIRE(dl.strides[1] == 1);
    }

    managed->deleter(managed);
}

// ===================================================================
// Hardening Pass #3 Tests
// ===================================================================

TEST_CASE("TensorView::at out of bounds throws", "[tensor][hardening]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    TensorShape shape;
    shape.dims = {4};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    // Valid access
    REQUIRE(view.at<float>(0) == 1.0f);
    REQUIRE(view.at<float>(3) == 4.0f);

    // Out-of-bounds access must throw
    REQUIRE_THROWS_AS(view.at<float>(4), std::out_of_range);
    REQUIRE_THROWS_AS(view.at<float>(-1), std::out_of_range);

    // Null view must throw
    TensorView null_view;
    REQUIRE_THROWS_AS(null_view.at<float>(0), std::out_of_range);
}

TEST_CASE("TensorView::slice out of bounds returns invalid", "[tensor][hardening]") {
    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    TensorShape shape;
    shape.dims = {4};
    TensorView view(data.data(), shape, TensorDataType::FLOAT32);

    // Valid slice
    auto valid = view.slice(0, 2);
    REQUIRE(valid.is_valid());
    REQUIRE(valid.num_elements() == 2);

    // OOB slice — must return invalid view (not crash)
    auto oob = view.slice(2, 5); // start=2, count=5 → exceeds dims[0]=4
    REQUIRE_FALSE(oob.is_valid());

    // Negative start
    auto neg = view.slice(-1, 1);
    REQUIRE_FALSE(neg.is_valid());

    // Null view
    TensorView null_view;
    auto from_null = null_view.slice(0, 1);
    REQUIRE_FALSE(from_null.is_valid());
}
