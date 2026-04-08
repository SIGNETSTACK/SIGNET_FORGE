// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji

/// @file tensor_bridge.hpp
/// @brief Zero-copy tensor bridge: maps Parquet column data directly into
///        ML-framework-compatible tensor views (ONNX Runtime, PyTorch, etc.)
///        without copying. Provides TensorView, OwnedTensor, ColumnToTensor,
///        and BatchTensorBuilder.

#pragma once

// ---------------------------------------------------------------------------
// tensor_bridge.hpp — Zero-Copy Tensor Bridge for SignetStack Signet Forge
//
// Maps Parquet column data directly into ML-framework-compatible tensor views
// without copying. Provides:
//
//   TensorDataType  — enum mapping to common ML framework element types
//   TensorShape     — N-dimensional shape descriptor
//   TensorView      — non-owning, zero-copy view into contiguous memory
//   OwnedTensor     — owning tensor with heap-allocated storage
//   ColumnToTensor  — Parquet column data -> tensor conversion
//   BatchTensorBuilder — multi-column feature batch assembly
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/types.hpp"
#include "signet/error.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace signet::forge {

namespace detail {

template <typename T, std::size_t Alignment>
class AlignedAllocator {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    template <typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() noexcept = default;

    template <typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    [[nodiscard]] T* allocate(std::size_t n) {
        static_assert(Alignment >= alignof(void*), "alignment must satisfy allocator requirements");
        static_assert((Alignment & (Alignment - 1)) == 0, "alignment must be a power of two");
        if (n == 0) return nullptr;
        if (n > (std::numeric_limits<std::size_t>::max)() / sizeof(T)) {
            throw std::bad_alloc();
        }

        void* ptr = nullptr;
        const std::size_t bytes = n * sizeof(T);
#ifdef _WIN32
        ptr = _aligned_malloc(bytes, Alignment);
        if (!ptr) throw std::bad_alloc();
#else
        if (::posix_memalign(&ptr, Alignment, bytes) != 0) {
            throw std::bad_alloc();
        }
#endif
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    template <typename U>
    [[nodiscard]] bool operator==(const AlignedAllocator<U, Alignment>&) const noexcept {
        return true;
    }

    template <typename U>
    [[nodiscard]] bool operator!=(const AlignedAllocator<U, Alignment>&) const noexcept {
        return false;
    }
};

template <typename T>
[[nodiscard]] inline bool is_pointer_aligned(const void* ptr) noexcept {
    if (ptr == nullptr) return false;
    return (reinterpret_cast<std::uintptr_t>(ptr) % alignof(T)) == 0;
}

template <typename T>
[[nodiscard]] inline T* aligned_ptr(void* ptr) noexcept {
    return is_pointer_aligned<T>(ptr) ? static_cast<T*>(ptr) : nullptr;
}

template <typename T>
[[nodiscard]] inline const T* aligned_ptr(const void* ptr) noexcept {
    return is_pointer_aligned<T>(ptr) ? static_cast<const T*>(ptr) : nullptr;
}

template <typename T>
[[nodiscard]] inline T* aligned_ptr_at(void* base, std::size_t offset) noexcept {
    auto* ptr = static_cast<std::uint8_t*>(base) + offset;
    return aligned_ptr<T>(ptr);
}

template <typename T>
[[nodiscard]] inline const T* aligned_ptr_at(const void* base, std::size_t offset) noexcept {
    auto* ptr = static_cast<const std::uint8_t*>(base) + offset;
    return aligned_ptr<T>(ptr);
}

} // namespace detail

// ===========================================================================
// TensorDataType — element data types for tensor storage
// ===========================================================================

/// Element data type for tensor storage, mapping to ONNX/PyTorch/TF type enums.
enum class TensorDataType : int32_t {
    FLOAT32 = 0,  ///< IEEE 754 single-precision (4 bytes)
    FLOAT64 = 1,  ///< IEEE 754 double-precision (8 bytes)
    INT32   = 2,  ///< Signed 32-bit integer
    INT64   = 3,  ///< Signed 64-bit integer
    INT8    = 4,  ///< Signed 8-bit integer
    UINT8   = 5,  ///< Unsigned 8-bit integer
    INT16   = 6,  ///< Signed 16-bit integer
    FLOAT16 = 7,  ///< IEEE 754 half-precision (2 bytes)
    BOOL    = 8   ///< Boolean (1 byte)
};

// ===========================================================================
// tensor_element_size — bytes per element for a given TensorDataType
// ===========================================================================

/// Returns the byte size of a single element of the given tensor data type.
inline constexpr size_t tensor_element_size(TensorDataType dtype) noexcept {
    switch (dtype) {
        case TensorDataType::FLOAT32: return 4;
        case TensorDataType::FLOAT64: return 8;
        case TensorDataType::INT32:   return 4;
        case TensorDataType::INT64:   return 8;
        case TensorDataType::INT8:    return 1;
        case TensorDataType::UINT8:   return 1;
        case TensorDataType::INT16:   return 2;
        case TensorDataType::FLOAT16: return 2;
        case TensorDataType::BOOL:    return 1;
    }
    return 0; // unreachable
}

/// Returns a human-readable name for a TensorDataType.
inline const char* tensor_dtype_name(TensorDataType dtype) noexcept {
    switch (dtype) {
        case TensorDataType::FLOAT32: return "float32";
        case TensorDataType::FLOAT64: return "float64";
        case TensorDataType::INT32:   return "int32";
        case TensorDataType::INT64:   return "int64";
        case TensorDataType::INT8:    return "int8";
        case TensorDataType::UINT8:   return "uint8";
        case TensorDataType::INT16:   return "int16";
        case TensorDataType::FLOAT16: return "float16";
        case TensorDataType::BOOL:    return "bool";
    }
    return "unknown";
}

// ===========================================================================
// TensorShape — N-dimensional shape descriptor
// ===========================================================================

/// Describes the shape of a tensor as a vector of dimension sizes.
///
/// Examples:
///   {}        — scalar
///   {100}     — 1D vector of 100 elements
///   {32, 768} — 2D matrix (32 rows, 768 columns)
///   {4,3,3}   — 3D tensor (e.g. batch of 3x3 matrices)
struct TensorShape {
    std::vector<int64_t> dims;  ///< Dimension sizes (e.g. {32, 768} for a 32x768 matrix)

    /// Default constructor: scalar shape (empty dims).
    TensorShape() = default;

    /// Construct from a vector of dimensions.
    /// @param d Dimension sizes.
    explicit TensorShape(std::vector<int64_t> d) : dims(std::move(d)) {}

    /// Construct from an initializer list (e.g. TensorShape{100} for 1D, TensorShape{32, 768} for 2D).
    /// @note Use single-brace syntax: `TensorShape{n}`, not `TensorShape{{n}}`.
    TensorShape(std::initializer_list<int64_t> il) : dims(il) {}

    /// Total number of elements (product of all dimensions).
    /// Returns 1 for a scalar (empty dims), consistent with ML frameworks.
    [[nodiscard]] int64_t num_elements() const noexcept {
        if (dims.empty()) return 1;
        int64_t product = 1;
        for (auto d : dims) {
            if (d <= 0) return -1; // error sentinel: non-positive dimension
            if (product > INT64_MAX / d) return -1; // overflow sentinel
            product *= d;
        }
        return product;
    }

    /// Number of dimensions.
    [[nodiscard]] size_t ndim() const noexcept { return dims.size(); }

    /// True if this is a scalar (no dimensions, or a single dimension of 1).
    [[nodiscard]] bool is_scalar() const noexcept {
        return dims.empty() || (dims.size() == 1 && dims[0] == 1);
    }

    /// True if this is a 1D vector.
    [[nodiscard]] bool is_vector() const noexcept { return dims.size() == 1; }

    /// True if this is a 2D matrix.
    [[nodiscard]] bool is_matrix() const noexcept { return dims.size() == 2; }

    /// Equality comparison (element-wise dimension match).
    [[nodiscard]] bool operator==(const TensorShape& other) const {
        return dims == other.dims;
    }

    /// Inequality comparison.
    [[nodiscard]] bool operator!=(const TensorShape& other) const {
        return dims != other.dims;
    }
};

// ===========================================================================
// TensorView — non-owning, zero-copy view into contiguous tensor memory
// ===========================================================================

/// A lightweight, non-owning view into a contiguous block of typed memory,
/// interpreted as a multi-dimensional tensor.
///
/// This is the core zero-copy type: wrapping existing Parquet column data as
/// a TensorView involves no allocation or data movement. The caller is
/// responsible for ensuring the underlying data remains valid for the
/// lifetime of the view.
///
/// The optional byte_stride parameter supports non-contiguous views (e.g. a
/// single column extracted from a row-major matrix). When byte_stride is 0
/// (the default), the data is assumed to be densely packed.
class TensorView {
public:
    /// Default constructor: creates an invalid (null) view.
    TensorView() = default;

    /// Construct a view over existing memory.
    ///
    /// @param data        Pointer to the start of the data buffer.
    /// @param shape       Shape of the tensor.
    /// @param dtype       Element data type.
    /// @param byte_stride Stride in bytes between consecutive elements along
    ///                    the first axis. 0 means contiguous (dense packing).
    TensorView(void* data, TensorShape shape, TensorDataType dtype,
               size_t byte_stride = 0) noexcept
        : data_(data)
        , shape_(std::move(shape))
        , dtype_(dtype)
        , byte_stride_(byte_stride) {}

    /// Construct a const view (stores as void* internally, constness enforced
    /// by the const overloads of data()/typed_data()).
    TensorView(const void* data, TensorShape shape, TensorDataType dtype,
               size_t byte_stride = 0) noexcept
        : data_(const_cast<void*>(data))
        , shape_(std::move(shape))
        , dtype_(dtype)
        , byte_stride_(byte_stride) {}

    // -- Raw data access ------------------------------------------------------

    /// Raw mutable pointer to the underlying data buffer.
    [[nodiscard]] void* data() noexcept { return data_; }
    /// Raw const pointer to the underlying data buffer.
    [[nodiscard]] const void* data() const noexcept { return data_; }

    /// Reinterpret the data pointer as a typed mutable pointer.
    /// The caller is responsible for ensuring T matches dtype().
    /// @tparam T Element type (e.g. float, double, int32_t).
    template <typename T>
    [[nodiscard]] T* typed_data() noexcept {
        return detail::aligned_ptr<T>(data_);
    }

    /// Reinterpret the data pointer as a typed const pointer.
    /// @tparam T Element type (e.g. float, double, int32_t).
    template <typename T>
    [[nodiscard]] const T* typed_data() const noexcept {
        return detail::aligned_ptr<T>(data_);
    }

    // -- Shape and type info --------------------------------------------------

    /// The shape of this tensor view.
    [[nodiscard]] const TensorShape& shape() const noexcept { return shape_; }
    /// The element data type.
    [[nodiscard]] TensorDataType dtype() const noexcept { return dtype_; }

    /// Bytes per element.
    [[nodiscard]] size_t element_size() const noexcept {
        return tensor_element_size(dtype_);
    }

    /// Total number of elements.
    [[nodiscard]] int64_t num_elements() const noexcept {
        return shape_.num_elements();
    }

    /// Total byte size of the tensor data (num_elements * element_size).
    /// Returns 0 if num_elements() is non-positive (empty or error shape).
    [[nodiscard]] size_t byte_size() const noexcept {
        const int64_t n = num_elements();
        if (n <= 0) return 0;
        return static_cast<size_t>(n) * element_size();
    }

    /// Effective stride in bytes along the first dimension.
    /// If byte_stride_ is 0 (contiguous), computes the dense stride.
    [[nodiscard]] size_t effective_byte_stride() const noexcept {
        if (byte_stride_ != 0) return byte_stride_;
        // Dense stride: product of dims[1..] * element_size
        if (shape_.ndim() <= 1) return element_size();
        size_t inner_size = element_size();
        for (size_t i = 1; i < shape_.ndim(); ++i) {
            inner_size *= static_cast<size_t>(shape_.dims[i]);
        }
        return inner_size;
    }

    // -- Typed element accessors ----------------------------------------------

    /// Access a single element in a 1D tensor (mutable).
    /// @tparam T Element type (must match dtype()).
    /// @param i  Zero-based element index.
    /// @return Reference to the element.
    /// @throws std::out_of_range if index is invalid or data is null.
    template <typename T>
    [[nodiscard]] T& at(int64_t i) {
        if (data_ == nullptr || i < 0 || i >= num_elements())
            throw std::out_of_range("TensorView::at(i): index out of range");
        if (byte_stride_ != 0) {
            auto* elem = detail::aligned_ptr_at<T>(data_, static_cast<size_t>(i) * byte_stride_);
            if (elem == nullptr)
                throw std::runtime_error("TensorView::at(i): misaligned tensor access");
            return *elem;
        }
        auto* ptr = typed_data<T>();
        if (ptr == nullptr)
            throw std::runtime_error("TensorView::at(i): misaligned tensor access");
        return ptr[i];
    }

    /// Access a single element in a 1D tensor (const).
    /// @tparam T Element type (must match dtype()).
    /// @param i  Zero-based element index.
    /// @return Const reference to the element.
    /// @throws std::out_of_range if index is invalid or data is null.
    template <typename T>
    [[nodiscard]] const T& at(int64_t i) const {
        if (data_ == nullptr || i < 0 || i >= num_elements())
            throw std::out_of_range("TensorView::at(i): index out of range");
        if (byte_stride_ != 0) {
            const auto* elem = detail::aligned_ptr_at<T>(data_, static_cast<size_t>(i) * byte_stride_);
            if (elem == nullptr)
                throw std::runtime_error("TensorView::at(i): misaligned tensor access");
            return *elem;
        }
        const auto* ptr = typed_data<T>();
        if (ptr == nullptr)
            throw std::runtime_error("TensorView::at(i): misaligned tensor access");
        return ptr[i];
    }

    /// Access a single element in a 2D tensor by (row, col) (mutable).
    /// @tparam T Element type (must match dtype()).
    /// @param row Zero-based row index.
    /// @param col Zero-based column index.
    /// @return Reference to the element.
    /// @throws std::out_of_range if indices are invalid or data is null.
    template <typename T>
    [[nodiscard]] T& at(int64_t row, int64_t col) {
        if (data_ == nullptr || shape_.ndim() != 2 ||
            row < 0 || row >= shape_.dims[0] ||
            col < 0 || col >= shape_.dims[1])
            throw std::out_of_range("TensorView::at(row,col): index out of range");
        const int64_t cols = shape_.dims[1];
        if (byte_stride_ != 0) {
            auto* row_ptr = detail::aligned_ptr_at<T>(data_, static_cast<size_t>(row) * byte_stride_);
            if (row_ptr == nullptr)
                throw std::runtime_error("TensorView::at(row,col): misaligned tensor access");
            return row_ptr[col];
        }
        auto* ptr = typed_data<T>();
        if (ptr == nullptr)
            throw std::runtime_error("TensorView::at(row,col): misaligned tensor access");
        return ptr[row * cols + col];
    }

    /// Access a single element in a 2D tensor by (row, col) (const).
    /// @tparam T Element type (must match dtype()).
    /// @param row Zero-based row index.
    /// @param col Zero-based column index.
    /// @return Const reference to the element.
    /// @throws std::out_of_range if indices are invalid or data is null.
    template <typename T>
    [[nodiscard]] const T& at(int64_t row, int64_t col) const {
        if (data_ == nullptr || shape_.ndim() != 2 ||
            row < 0 || row >= shape_.dims[0] ||
            col < 0 || col >= shape_.dims[1])
            throw std::out_of_range("TensorView::at(row,col): index out of range");
        const int64_t cols = shape_.dims[1];
        if (byte_stride_ != 0) {
            const auto* row_ptr = detail::aligned_ptr_at<T>(data_, static_cast<size_t>(row) * byte_stride_);
            if (row_ptr == nullptr)
                throw std::runtime_error("TensorView::at(row,col): misaligned tensor access");
            return row_ptr[col];
        }
        const auto* ptr = typed_data<T>();
        if (ptr == nullptr)
            throw std::runtime_error("TensorView::at(row,col): misaligned tensor access");
        return ptr[row * cols + col];
    }

    // -- Predicates -----------------------------------------------------------

    /// True if the data is densely packed (no stride gaps).
    [[nodiscard]] bool is_contiguous() const noexcept {
        return byte_stride_ == 0;
    }

    /// True if the view points to valid data.
    [[nodiscard]] bool is_valid() const noexcept {
        return data_ != nullptr;
    }

    // -- Subview and reshape --------------------------------------------------

    /// Slice along the first dimension: returns a view over rows [start, start+count).
    ///
    /// Zero-copy: the returned view's data pointer is offset into this view's
    /// buffer. No data is copied.
    ///
    /// @param start  First row index (inclusive).
    /// @param count  Number of rows.
    /// @return A new TensorView covering the requested slice.
    [[nodiscard]] TensorView slice(int64_t start, int64_t count) const {
        if (data_ == nullptr || shape_.ndim() < 1 ||
            start < 0 || count < 0 || start + count > shape_.dims[0])
            return TensorView{}; // return invalid view

        // Compute the byte offset to the start of the slice
        const size_t stride = effective_byte_stride();
        auto* base = static_cast<uint8_t*>(const_cast<void*>(data_));
        void* slice_data = base + static_cast<size_t>(start) * stride;

        // Build the new shape: replace dims[0] with count, keep the rest
        TensorShape new_shape;
        new_shape.dims = shape_.dims;
        new_shape.dims[0] = count;

        return TensorView(slice_data, std::move(new_shape), dtype_, byte_stride_);
    }

    /// Reshape the view to a new shape with the same total number of elements.
    ///
    /// Only valid for contiguous views. Returns an error if:
    ///   - the view is non-contiguous (has a non-zero byte_stride)
    ///   - the total element count does not match
    [[nodiscard]] expected<TensorView> reshape(TensorShape new_shape) const {
        if (!is_contiguous()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "cannot reshape a non-contiguous tensor view"};
        }
        if (new_shape.num_elements() != shape_.num_elements()) {
            return Error{ErrorCode::SCHEMA_MISMATCH,
                         "reshape: total elements mismatch ("
                         + std::to_string(shape_.num_elements()) + " vs "
                         + std::to_string(new_shape.num_elements()) + ")"};
        }
        return TensorView(data_, std::move(new_shape), dtype_, 0);
    }

private:
    void*          data_        = nullptr;
    TensorShape    shape_;
    TensorDataType dtype_       = TensorDataType::FLOAT32;
    size_t         byte_stride_ = 0;  // 0 = contiguous (densely packed)
};

// ===========================================================================
// OwnedTensor — heap-allocated, owning tensor
// ===========================================================================

/// An owning tensor that manages its own memory via a std::vector<uint8_t>
/// buffer. Move-only; use clone() for explicit deep copies.
///
/// Provides the same access interface as TensorView. Obtain a non-owning
/// view via view() for passing to functions that accept TensorView.
class OwnedTensor {
public:
    /// Default constructor: creates an invalid (empty) tensor.
    OwnedTensor() = default;

    /// Allocate an uninitialized tensor with the given shape and type.
    OwnedTensor(TensorShape shape, TensorDataType dtype)
        : shape_(std::move(shape))
        , dtype_(dtype) {
        const auto num_elements = shape_.num_elements();
        const auto element_size = tensor_element_size(dtype_);
        if (num_elements <= 0 || static_cast<size_t>(num_elements) > SIZE_MAX / element_size) {
            // Overflow or invalid shape — leave buffer empty (invalid tensor)
            return;
        }
        const size_t sz = static_cast<size_t>(num_elements) * element_size;
        buffer_.resize(sz, 0);
    }

    /// Allocate and copy data into the tensor.
    ///
    /// @param data   Pointer to source data (must contain at least
    ///               shape.num_elements() * tensor_element_size(dtype) bytes).
    /// @param shape  Shape of the tensor.
    /// @param dtype  Element type.
    OwnedTensor(const void* data, TensorShape shape, TensorDataType dtype)
        : shape_(std::move(shape))
        , dtype_(dtype) {
        const auto num_elements = shape_.num_elements();
        const auto element_size = tensor_element_size(dtype_);
        if (num_elements <= 0 || static_cast<size_t>(num_elements) > SIZE_MAX / element_size) {
            return; // Overflow or invalid shape — leave buffer empty
        }
        const size_t sz = static_cast<size_t>(num_elements) * element_size;
        buffer_.resize(sz);
        if (data && sz > 0) {
            std::memcpy(buffer_.data(), data, sz);
        }
    }

    // Move semantics
    OwnedTensor(OwnedTensor&&) noexcept = default;
    OwnedTensor& operator=(OwnedTensor&&) noexcept = default;

    // No implicit copy — use clone()
    OwnedTensor(const OwnedTensor&) = delete;
    OwnedTensor& operator=(const OwnedTensor&) = delete;

    /// Deep-copy this tensor.
    [[nodiscard]] OwnedTensor clone() const {
        OwnedTensor copy;
        copy.buffer_ = buffer_;
        copy.shape_  = shape_;
        copy.dtype_  = dtype_;
        return copy;
    }

    // -- View access ----------------------------------------------------------

    /// Get a mutable non-owning view.
    [[nodiscard]] TensorView view() {
        return TensorView(buffer_.data(), shape_, dtype_, 0);
    }

    /// Get a const non-owning view.
    [[nodiscard]] TensorView view() const {
        return TensorView(
            const_cast<uint8_t*>(buffer_.data()), shape_, dtype_, 0);
    }

    // -- Data access (forwarded from view) ------------------------------------

    /// Raw mutable pointer to the tensor buffer.
    [[nodiscard]] void* data() noexcept { return buffer_.data(); }
    /// Raw const pointer to the tensor buffer.
    [[nodiscard]] const void* data() const noexcept { return buffer_.data(); }

    /// Typed mutable pointer to the tensor buffer.
    /// @tparam T Element type (must match dtype()).
    template <typename T>
    [[nodiscard]] T* typed_data() noexcept {
        return detail::aligned_ptr<T>(buffer_.data());
    }

    /// Typed const pointer to the tensor buffer.
    /// @tparam T Element type (must match dtype()).
    template <typename T>
    [[nodiscard]] const T* typed_data() const noexcept {
        return detail::aligned_ptr<T>(buffer_.data());
    }

    /// The shape of this tensor.
    [[nodiscard]] const TensorShape& shape() const noexcept { return shape_; }
    /// The element data type.
    [[nodiscard]] TensorDataType dtype() const noexcept { return dtype_; }

    /// Total byte size of the tensor buffer.
    [[nodiscard]] size_t byte_size() const noexcept { return buffer_.size(); }

    /// Total number of elements.
    [[nodiscard]] int64_t num_elements() const noexcept {
        return shape_.num_elements();
    }

    /// True if the tensor has been allocated (non-empty buffer).
    [[nodiscard]] bool is_valid() const noexcept { return !buffer_.empty(); }

private:
    using Buffer = std::vector<uint8_t,
        detail::AlignedAllocator<uint8_t, alignof(std::max_align_t)>>;

    Buffer           buffer_;
    TensorShape      shape_;
    TensorDataType   dtype_ = TensorDataType::FLOAT32;
};

// ===========================================================================
// ColumnToTensor — map Parquet column data to tensor representations
// ===========================================================================

/// Provides static methods to convert Parquet column data into tensor form.
///
/// Two primary paths:
///   1. **Zero-copy** (wrap_column / wrap_vectors): constructs a TensorView
///      pointing directly into the Parquet page buffer. No allocation, no
///      memcpy. The caller must ensure the page buffer outlives the view.
///
///   2. **Copy** (copy_column / cast): allocates a new OwnedTensor and
///      converts data into the requested type. Use when the Parquet physical
///      type does not match the desired tensor type, or when the data must
///      outlive the source buffer.
class ColumnToTensor {
public:
    // -----------------------------------------------------------------------
    // Zero-copy path: wrap existing column data as a TensorView
    // -----------------------------------------------------------------------

    /// Wrap a contiguous numeric Parquet column as a 1D TensorView.
    ///
    /// Supported physical types: INT32, INT64, FLOAT, DOUBLE,
    /// FIXED_LEN_BYTE_ARRAY (returned as a 2D view of raw bytes or typed
    /// data when type_length aligns to a primitive size).
    ///
    /// No data is copied. The returned view points directly into column_data.
    ///
    /// @param column_data   Pointer to the column's contiguous value buffer.
    /// @param num_values    Number of values in the column.
    /// @param physical_type Parquet physical type of the column.
    /// @param type_length   Byte length per value (only for FIXED_LEN_BYTE_ARRAY).
    /// @return TensorView on success, Error on unsupported type.
    static inline expected<TensorView> wrap_column(
            const void* column_data,
            int64_t num_values,
            PhysicalType physical_type,
            int32_t type_length = -1) {
        if (!column_data || num_values <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "wrap_column: null data or non-positive count"};
        }

        switch (physical_type) {
            case PhysicalType::INT32:
                return TensorView(column_data,
                                  TensorShape{num_values},
                                  TensorDataType::INT32);

            case PhysicalType::INT64:
                return TensorView(column_data,
                                  TensorShape{num_values},
                                  TensorDataType::INT64);

            case PhysicalType::FLOAT:
                return TensorView(column_data,
                                  TensorShape{num_values},
                                  TensorDataType::FLOAT32);

            case PhysicalType::DOUBLE:
                return TensorView(column_data,
                                  TensorShape{num_values},
                                  TensorDataType::FLOAT64);

            case PhysicalType::FIXED_LEN_BYTE_ARRAY: {
                if (type_length <= 0) {
                    return Error{ErrorCode::SCHEMA_MISMATCH,
                                 "wrap_column: FIXED_LEN_BYTE_ARRAY requires "
                                 "positive type_length"};
                }
                // Expose as a 2D {num_values, type_length} uint8 view
                return TensorView(column_data,
                                  TensorShape{num_values,
                                              static_cast<int64_t>(type_length)},
                                  TensorDataType::UINT8);
            }

            case PhysicalType::BOOLEAN:
                // Parquet booleans are bit-packed; cannot zero-copy as a
                // byte-addressable tensor without unpacking.
                return Error{ErrorCode::UNSUPPORTED_TYPE,
                             "wrap_column: BOOLEAN columns require copy "
                             "(bit-packed, not byte-addressable)"};

            case PhysicalType::BYTE_ARRAY:
                return Error{ErrorCode::UNSUPPORTED_TYPE,
                             "wrap_column: BYTE_ARRAY (variable-length) "
                             "cannot be zero-copy wrapped as a tensor"};

            case PhysicalType::INT96:
                return Error{ErrorCode::UNSUPPORTED_TYPE,
                             "wrap_column: INT96 is deprecated and "
                             "not supported for tensor wrapping"};
        }

        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "wrap_column: unknown physical type"};
    }

    // -----------------------------------------------------------------------
    // Zero-copy path: wrap vector column data
    // -----------------------------------------------------------------------

    /// Wrap a contiguous FLOAT32_VECTOR column as a 2D TensorView.
    ///
    /// The data is assumed to be densely packed float32 vectors, each of
    /// the given dimension. The returned shape is {num_vectors, dimension}.
    ///
    /// @param column_data  Pointer to contiguous float data.
    /// @param num_vectors  Number of vectors.
    /// @param dimension    Elements per vector.
    /// @return 2D TensorView of shape {num_vectors, dimension}.
    static inline expected<TensorView> wrap_vectors(
            const void* column_data,
            int64_t num_vectors,
            uint32_t dimension) {
        if (!column_data || num_vectors <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "wrap_vectors: null data or non-positive count"};
        }
        if (dimension == 0) {
            return Error{ErrorCode::SCHEMA_MISMATCH,
                         "wrap_vectors: dimension must be > 0"};
        }

        return TensorView(column_data,
                          TensorShape{num_vectors,
                                      static_cast<int64_t>(dimension)},
                          TensorDataType::FLOAT32);
    }

    // -----------------------------------------------------------------------
    // Copy path: read + convert column data into an OwnedTensor
    // -----------------------------------------------------------------------

    /// Read column data and produce an OwnedTensor with the requested type.
    ///
    /// Supports all numeric Parquet physical types. BYTE_ARRAY (variable-
    /// length strings) cannot be represented as a dense tensor and returns
    /// an error.
    ///
    /// @param column_data   Source data pointer.
    /// @param num_values    Number of values.
    /// @param physical_type Parquet physical type.
    /// @param target_dtype  Desired tensor element type.
    /// @param type_length   For FIXED_LEN_BYTE_ARRAY only.
    /// @return OwnedTensor on success, Error otherwise.
    static inline expected<OwnedTensor> copy_column(
            const void* column_data,
            int64_t num_values,
            PhysicalType physical_type,
            TensorDataType target_dtype,
            int32_t type_length = -1) {
        if (!column_data || num_values <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "copy_column: null data or non-positive count"};
        }

        // For BYTE_ARRAY we cannot produce a dense tensor
        if (physical_type == PhysicalType::BYTE_ARRAY) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "copy_column: BYTE_ARRAY (strings) cannot be "
                         "converted to a dense tensor"};
        }
        if (physical_type == PhysicalType::INT96) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "copy_column: INT96 is deprecated and not supported"};
        }

        // First, try the zero-copy wrap to get a typed view of the source
        // For FIXED_LEN_BYTE_ARRAY, we handle specially below
        if (physical_type == PhysicalType::FIXED_LEN_BYTE_ARRAY) {
            if (type_length <= 0) {
                return Error{ErrorCode::SCHEMA_MISMATCH,
                             "copy_column: FIXED_LEN_BYTE_ARRAY requires "
                             "positive type_length"};
            }
            // Treat as flat bytes, then cast into the target dtype
            TensorView src(column_data,
                           TensorShape{num_values * static_cast<int64_t>(type_length)},
                           TensorDataType::UINT8);
            // If target is UINT8, just copy directly
            if (target_dtype == TensorDataType::UINT8) {
                OwnedTensor out(TensorShape{num_values,
                                            static_cast<int64_t>(type_length)},
                                target_dtype);
                std::memcpy(out.data(), column_data,
                            static_cast<size_t>(num_values) *
                            static_cast<size_t>(type_length));
                return out;
            }
            // Otherwise interpret as float32 vectors if type_length is a
            // multiple of sizeof(float)
            if (type_length % static_cast<int32_t>(sizeof(float)) == 0) {
                int64_t dim = type_length / static_cast<int32_t>(sizeof(float));
                TensorView float_src(column_data,
                                     TensorShape{num_values, dim},
                                     TensorDataType::FLOAT32);
                return cast(float_src, target_dtype);
            }
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "copy_column: FIXED_LEN_BYTE_ARRAY with type_length "
                         "not a multiple of 4 can only be copied as UINT8"};
        }

        // For standard numeric types, wrap then cast
        auto src_dtype_result = parquet_to_tensor_dtype(physical_type);
        if (!src_dtype_result) {
            return Error{src_dtype_result.error().code,
                         src_dtype_result.error().message};
        }

        TensorDataType src_dtype = src_dtype_result.value();
        TensorView src(column_data, TensorShape{num_values}, src_dtype);

        // If source dtype matches target, just copy the bytes
        if (src_dtype == target_dtype) {
            return OwnedTensor(column_data,
                               TensorShape{num_values}, target_dtype);
        }

        return cast(src, target_dtype);
    }

    // -----------------------------------------------------------------------
    // Type casting
    // -----------------------------------------------------------------------

    /// Cast a tensor view to a different element type, producing an OwnedTensor.
    ///
    /// Uses a type-dispatched inner loop. Supported source and target types:
    /// FLOAT32, FLOAT64, INT32, INT64, INT8, UINT8, INT16, BOOL.
    /// FLOAT16 as a source or target is not currently supported by cast().
    ///
    /// @param src          Source tensor view.
    /// @param target_dtype Desired output element type.
    /// @return OwnedTensor with the cast data.
    static inline expected<OwnedTensor> cast(
            const TensorView& src,
            TensorDataType target_dtype) {
        if (!src.is_valid()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "cast: source tensor is null"};
        }
        if (!src.is_contiguous()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "cast: source tensor must be contiguous"};
        }

        // Same type — just copy
        if (src.dtype() == target_dtype) {
            return OwnedTensor(src.data(), src.shape(), target_dtype);
        }

        const int64_t n = src.num_elements();
        OwnedTensor out(src.shape(), target_dtype);

        // Dispatch on (src_dtype, target_dtype) using a helper
        bool ok = dispatch_cast(src.data(), src.dtype(),
                                out.data(), target_dtype, n);
        if (!ok) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         std::string("cast: unsupported conversion from ")
                         + tensor_dtype_name(src.dtype()) + " to "
                         + tensor_dtype_name(target_dtype)};
        }

        return out;
    }

    // -----------------------------------------------------------------------
    // Parquet → Tensor dtype mapping
    // -----------------------------------------------------------------------

    /// Map a Parquet physical type to the natural TensorDataType.
    static inline expected<TensorDataType> parquet_to_tensor_dtype(
            PhysicalType pt) {
        switch (pt) {
            case PhysicalType::BOOLEAN:  return TensorDataType::BOOL;
            case PhysicalType::INT32:    return TensorDataType::INT32;
            case PhysicalType::INT64:    return TensorDataType::INT64;
            case PhysicalType::FLOAT:    return TensorDataType::FLOAT32;
            case PhysicalType::DOUBLE:   return TensorDataType::FLOAT64;
            case PhysicalType::FIXED_LEN_BYTE_ARRAY:
                return TensorDataType::UINT8;
            case PhysicalType::BYTE_ARRAY:
                return Error{ErrorCode::UNSUPPORTED_TYPE,
                             "BYTE_ARRAY has no fixed tensor type mapping"};
            case PhysicalType::INT96:
                return Error{ErrorCode::UNSUPPORTED_TYPE,
                             "INT96 has no tensor type mapping"};
        }
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "unknown PhysicalType"};
    }

private:
    // -- Cast dispatch --------------------------------------------------------

    /// Read a single element from a typed buffer at the given index.
    template <typename T>
    static inline T read_element(const void* data, int64_t idx) {
        return static_cast<const T*>(data)[idx];
    }

    /// Write a single element to a typed buffer.
    template <typename T>
    static inline void write_element(void* data, int64_t idx, T val) {
        static_cast<T*>(data)[idx] = val;
    }

    /// Templated inner conversion loop: read Src, static_cast to Dst, write.
    template <typename Src, typename Dst>
    static inline void convert_loop(const void* src, void* dst, int64_t n) {
        const auto* s = static_cast<const Src*>(src);
        auto* d = static_cast<Dst*>(dst);
        for (int64_t i = 0; i < n; ++i) {
            d[i] = static_cast<Dst>(s[i]);
        }
    }

    /// Dispatch the target type for a known source type.
    template <typename Src>
    static inline bool dispatch_target(const void* src, void* dst,
                                       TensorDataType target, int64_t n) {
        switch (target) {
            case TensorDataType::FLOAT32: convert_loop<Src, float>(src, dst, n); return true;
            case TensorDataType::FLOAT64: convert_loop<Src, double>(src, dst, n); return true;
            case TensorDataType::INT32:   convert_loop<Src, int32_t>(src, dst, n); return true;
            case TensorDataType::INT64:   convert_loop<Src, int64_t>(src, dst, n); return true;
            case TensorDataType::INT8:    convert_loop<Src, int8_t>(src, dst, n); return true;
            case TensorDataType::UINT8:   convert_loop<Src, uint8_t>(src, dst, n); return true;
            case TensorDataType::INT16:   convert_loop<Src, int16_t>(src, dst, n); return true;
            case TensorDataType::BOOL:    convert_loop<Src, bool>(src, dst, n); return true;
            default: return false;
        }
    }

    /// Full (src_dtype, target_dtype) dispatch.
    static inline bool dispatch_cast(const void* src, TensorDataType src_dtype,
                                     void* dst, TensorDataType target, int64_t n) {
        switch (src_dtype) {
            case TensorDataType::FLOAT32: return dispatch_target<float>(src, dst, target, n);
            case TensorDataType::FLOAT64: return dispatch_target<double>(src, dst, target, n);
            case TensorDataType::INT32:   return dispatch_target<int32_t>(src, dst, target, n);
            case TensorDataType::INT64:   return dispatch_target<int64_t>(src, dst, target, n);
            case TensorDataType::INT8:    return dispatch_target<int8_t>(src, dst, target, n);
            case TensorDataType::UINT8:   return dispatch_target<uint8_t>(src, dst, target, n);
            case TensorDataType::INT16:   return dispatch_target<int16_t>(src, dst, target, n);
            case TensorDataType::BOOL:    return dispatch_target<bool>(src, dst, target, n);
            default: return false;
        }
    }
};

// ===========================================================================
// BatchTensorBuilder — assemble multi-column feature batches for ML inference
// ===========================================================================

/// Builds a single contiguous 2D tensor from multiple column tensors,
/// suitable for passing to an ML inference engine (ONNX Runtime, etc.).
///
/// Each added column contributes one or more feature columns to the output.
/// For 1D numeric columns (shape {N}), each adds 1 column to the batch.
/// For 2D vector columns (shape {N, D}), each adds D columns.
///
/// All input columns must have the same number of rows. The output is a
/// single {rows, total_cols} tensor with the requested element type.
///
/// Usage:
///   BatchTensorBuilder builder;
///   builder.add_column("price",    price_view);     // {1000}
///   builder.add_column("volume",   volume_view);    // {1000}
///   builder.add_column("embedding", embed_view);    // {1000, 128}
///   auto batch = builder.build(TensorDataType::FLOAT32);
///   // batch.shape() == {1000, 130}
class BatchTensorBuilder {
public:
    /// Default constructor: creates an empty builder with no columns.
    BatchTensorBuilder() = default;

    /// Add a column tensor as a feature source.
    ///
    /// For 1D tensors: contributes 1 feature column per row.
    /// For 2D tensors: contributes dims[1] feature columns per row.
    /// Higher-dimensional tensors are not supported.
    ///
    /// @param name   Human-readable column name (for diagnostics).
    /// @param tensor View of the column data.
    /// @return Reference to this builder for chaining.
    BatchTensorBuilder& add_column(const std::string& name,
                                   const TensorView& tensor) {
        columns_.push_back(ColumnEntry{name, tensor});
        return *this;
    }

    /// Compute the expected output shape based on currently added columns.
    ///
    /// Returns {rows, total_cols} where rows is taken from the first column
    /// and total_cols is the sum of per-column widths.
    [[nodiscard]] TensorShape expected_shape() const {
        if (columns_.empty()) return TensorShape{0, 0};

        const int64_t rows = column_rows(columns_[0].tensor);
        int64_t total_cols = 0;
        for (const auto& entry : columns_) {
            total_cols += column_width(entry.tensor);
        }
        return TensorShape{rows, total_cols};
    }

    /// Number of feature sources (columns) added.
    [[nodiscard]] size_t num_features() const noexcept {
        return columns_.size();
    }

    /// Build the final batch tensor.
    ///
    /// Allocates a single contiguous output tensor of shape {rows, total_cols}
    /// and copies each column's data into the appropriate column positions.
    ///
    /// Currently requires all input columns to be contiguous. Mixed source
    /// types are handled via ColumnToTensor::cast().
    ///
    /// @param output_dtype Desired element type of the output tensor
    ///                     (default: FLOAT32).
    /// @return OwnedTensor on success, Error on failure.
    [[nodiscard]] expected<OwnedTensor> build(
            TensorDataType output_dtype = TensorDataType::FLOAT32) {
        if (columns_.empty()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "BatchTensorBuilder: no columns added"};
        }

        // Determine row count from first column
        const int64_t rows = column_rows(columns_[0].tensor);
        if (rows <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "BatchTensorBuilder: first column has no rows"};
        }

        // Validate all columns have the same row count
        for (size_t i = 1; i < columns_.size(); ++i) {
            const int64_t col_rows = column_rows(columns_[i].tensor);
            if (col_rows != rows) {
                return Error{ErrorCode::SCHEMA_MISMATCH,
                             "BatchTensorBuilder: column '"
                             + columns_[i].name + "' has "
                             + std::to_string(col_rows) + " rows, expected "
                             + std::to_string(rows)};
            }
        }

        // Compute total output columns
        int64_t total_cols = 0;
        for (const auto& entry : columns_) {
            total_cols += column_width(entry.tensor);
        }

        // Allocate output tensor
        TensorShape out_shape{rows, total_cols};
        OwnedTensor output(out_shape, output_dtype);
        const size_t out_elem_size = tensor_element_size(output_dtype);

        // Fill column by column
        int64_t col_offset = 0;
        for (const auto& entry : columns_) {
            const TensorView& src = entry.tensor;
            const int64_t width = column_width(src);

            // Get or cast the source data to the output dtype
            // We need a contiguous float32 (or target type) source
            if (src.dtype() == output_dtype && src.is_contiguous()) {
                // Direct copy path
                copy_column_into(output, rows, total_cols, col_offset,
                                 width, src.data(), out_elem_size);
            } else {
                // Need to cast first
                auto cast_result = ColumnToTensor::cast(src, output_dtype);
                if (!cast_result) {
                    return Error{cast_result.error().code,
                                 "BatchTensorBuilder: failed to cast column '"
                                 + entry.name + "': "
                                 + cast_result.error().message};
                }
                copy_column_into(output, rows, total_cols, col_offset,
                                 width, cast_result.value().data(),
                                 out_elem_size);
            }

            col_offset += width;
        }

        return output;
    }

private:
    struct ColumnEntry {
        std::string name;
        TensorView  tensor;
    };

    std::vector<ColumnEntry> columns_;

    /// Number of rows in a column tensor.
    static int64_t column_rows(const TensorView& t) noexcept {
        if (t.shape().ndim() == 0) return 1;
        return t.shape().dims[0];
    }

    /// Number of feature columns contributed by a tensor.
    /// 1D → 1 column, 2D → dims[1] columns, scalar → 1.
    static int64_t column_width(const TensorView& t) noexcept {
        if (t.shape().ndim() <= 1) return 1;
        return t.shape().dims[1];
    }

    /// Copy a source column's data into the output tensor at the given
    /// column offset. The source is assumed to be contiguous and already
    /// in the output element type.
    ///
    /// For 1D sources (width=1): copies one element per row.
    /// For 2D sources (width>1): copies a contiguous row slice per row.
    static void copy_column_into(
            OwnedTensor& output,
            int64_t rows,
            int64_t total_cols,
            int64_t col_offset,
            int64_t width,
            const void* src_data,
            size_t elem_size) {
        auto* dst_base = static_cast<uint8_t*>(output.data());
        const auto* src_base = static_cast<const uint8_t*>(src_data);

        const size_t row_byte_stride = static_cast<size_t>(total_cols) * elem_size;
        const size_t src_row_bytes   = static_cast<size_t>(width) * elem_size;
        const size_t col_byte_offset = static_cast<size_t>(col_offset) * elem_size;

        for (int64_t r = 0; r < rows; ++r) {
            const size_t dst_offset = static_cast<size_t>(r) * row_byte_stride
                                      + col_byte_offset;
            const size_t src_offset = static_cast<size_t>(r) * src_row_bytes;
            std::memcpy(dst_base + dst_offset,
                        src_base + src_offset,
                        src_row_bytes);
        }
    }
};

} // namespace signet::forge
