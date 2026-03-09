// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file numpy_bridge.hpp
/// @brief DLPack / NumPy interop for SignetStack Signet Forge tensor bridge.
///
/// Exports Signet tensors as DLManagedTensor (DLPack v0.8 compatible) for
/// zero-copy consumption by PyTorch, NumPy, JAX, TensorFlow, and any other
/// framework that supports `from_dlpack()`.
///
/// Also provides BufferInfo for Python buffer protocol (PEP 3118) and
/// pybind11's py::buffer_info integration.
///
/// Zero dependency: does NOT link to DLPack, NumPy, or PyTorch.
/// Header-only. Part of the signet::forge interop module (Phase 6).
///
/// @see NumpyBridge, BufferInfo, to_buffer_info

#include "signet/ai/tensor_bridge.hpp"
#include "signet/error.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace signet::forge {

/// @name DLPack type definitions (matching dlpack.h v0.8)
/// @brief Self-contained DLPack struct definitions for zero-dependency interop.
/// @{

/// DLPack device type, matching DLDeviceType from dlpack.h.
///
/// Only kDLCPU and kDLCUDAHost are supported for import by NumpyBridge.
/// Other device types are defined for completeness and forward compatibility.
enum class DLDeviceType : int32_t {
    kDLCPU      = 1,   ///< System main memory
    kDLCUDA     = 2,   ///< NVIDIA CUDA GPU memory
    kDLCUDAHost = 3,   ///< CUDA pinned host memory
    kDLROCM     = 10,  ///< AMD ROCm GPU memory
    kDLMetal    = 8,   ///< Apple Metal GPU memory
    kDLVulkan   = 7    ///< Vulkan GPU memory
};

/// DLPack data type code, matching DLDataTypeCode from dlpack.h.
enum class DLDataTypeCode : uint8_t {
    kDLInt    = 0,   ///< Signed integer
    kDLUInt   = 1,   ///< Unsigned integer
    kDLFloat  = 2,   ///< IEEE floating point
    kDLBfloat = 4    ///< Brain floating point (bfloat16)
};

/// DLPack data type descriptor.
///
/// Fully describes an element's bit-representation using a type code,
/// bit width, and SIMD lane count. For scalar data (the common case),
/// lanes is always 1.
///
/// @note Signet only supports single-lane (scalar) types. Multi-lane
///       types are rejected by from_dlpack_dtype().
struct DLDataType {
    DLDataTypeCode code;       ///< Type category (int/uint/float/bfloat)
    uint8_t        bits;       ///< Number of bits per element (e.g. 32 for float32)
    uint16_t       lanes = 1;  ///< Number of SIMD lanes (1 for scalar)
};

/// DLPack device descriptor (type + ordinal).
struct DLDevice {
    DLDeviceType device_type = DLDeviceType::kDLCPU; ///< Device type (CPU, CUDA, etc.)
    int32_t      device_id   = 0;                     ///< Device ordinal (0 for single-device systems)
};

/// DLPack tensor descriptor (non-owning).
///
/// Describes the layout and location of a multi-dimensional tensor. Does
/// not own the data or shape/strides arrays -- those are managed by the
/// enclosing DLManagedTensor's deleter callback.
struct DLTensor {
    void*       data;        ///< Pointer to the start of tensor data
    DLDevice    device;      ///< Device where data resides (CPU, CUDA, etc.)
    int32_t     ndim;        ///< Number of dimensions (must be > 0)
    DLDataType  dtype;       ///< Element data type descriptor
    int64_t*    shape;       ///< Shape array with ndim elements
    int64_t*    strides;     ///< Stride array in elements (nullptr = C-contiguous)
    uint64_t    byte_offset; ///< Byte offset from data pointer to first element
};

/// DLPack managed tensor -- the exchange object for from_dlpack().
///
/// This is the top-level struct passed between producers and consumers.
/// The consumer calls `deleter(self)` exactly once when done, which frees
/// all associated resources (shape arrays, manager context, and optionally
/// the tensor data itself).
///
/// @note In Python, this is typically wrapped in a PyCapsule with name
///       "dltensor". The capsule destructor calls the deleter.
/// @see NumpyBridge::export_tensor, NumpyBridge::export_owned_tensor
struct DLManagedTensor {
    DLTensor dl_tensor;                   ///< The tensor descriptor (layout, data pointer, dtype)
    void*    manager_ctx;                 ///< Opaque context for the deleter (owns shape/data)
    void     (*deleter)(DLManagedTensor*); ///< Destructor callback (must be called exactly once)
};

/// @}

/// @name Type conversion: TensorDataType <-> DLDataType
/// @{

/// Convert a Signet TensorDataType to a DLPack DLDataType.
///
/// This function is total -- all TensorDataType values have a DLPack mapping.
/// Lanes is always set to 1 (scalar).
///
/// Mapping:
/// | TensorDataType | DLDataTypeCode | bits |
/// |----------------|----------------|------|
/// | FLOAT32        | kDLFloat       | 32   |
/// | FLOAT64        | kDLFloat       | 64   |
/// | FLOAT16        | kDLFloat       | 16   |
/// | INT32          | kDLInt         | 32   |
/// | INT64          | kDLInt         | 64   |
/// | INT16          | kDLInt         | 16   |
/// | INT8           | kDLInt         | 8    |
/// | UINT8          | kDLUInt        | 8    |
/// | BOOL           | kDLUInt        | 8    |
///
/// @param dtype  The Signet tensor data type to convert.
/// @return       The corresponding DLDataType descriptor.
/// @note BOOL is mapped to UInt/8 per DLPack convention (no native bool type).
/// @see from_dlpack_dtype
inline DLDataType to_dlpack_dtype(TensorDataType dtype) {
    DLDataType dt;
    dt.lanes = 1;

    switch (dtype) {
        case TensorDataType::FLOAT32:
            dt.code = DLDataTypeCode::kDLFloat;
            dt.bits = 32;
            break;
        case TensorDataType::FLOAT64:
            dt.code = DLDataTypeCode::kDLFloat;
            dt.bits = 64;
            break;
        case TensorDataType::FLOAT16:
            dt.code = DLDataTypeCode::kDLFloat;
            dt.bits = 16;
            break;
        case TensorDataType::INT32:
            dt.code = DLDataTypeCode::kDLInt;
            dt.bits = 32;
            break;
        case TensorDataType::INT64:
            dt.code = DLDataTypeCode::kDLInt;
            dt.bits = 64;
            break;
        case TensorDataType::INT16:
            dt.code = DLDataTypeCode::kDLInt;
            dt.bits = 16;
            break;
        case TensorDataType::INT8:
            dt.code = DLDataTypeCode::kDLInt;
            dt.bits = 8;
            break;
        case TensorDataType::UINT8:
            dt.code = DLDataTypeCode::kDLUInt;
            dt.bits = 8;
            break;
        case TensorDataType::BOOL:
            // DLPack convention: booleans are represented as uint8
            dt.code = DLDataTypeCode::kDLUInt;
            dt.bits = 8;
            break;
    }

    return dt;
}

/// Convert a DLPack DLDataType back to a Signet TensorDataType.
///
/// Returns an error for types that have no Signet equivalent (e.g.
/// uint16, uint32, uint64, bfloat16, or multi-lane SIMD types).
///
/// @param dl_dtype  The DLPack data type descriptor.
/// @return          The corresponding TensorDataType, or UNSUPPORTED_TYPE
///                  for multi-lane types, bfloat16, or unsupported bit widths.
/// @see to_dlpack_dtype
inline expected<TensorDataType> from_dlpack_dtype(DLDataType dl_dtype) {
    if (dl_dtype.lanes != 1) {
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "multi-lane DLPack dtypes are not supported"};
    }

    switch (dl_dtype.code) {
        case DLDataTypeCode::kDLFloat:
            switch (dl_dtype.bits) {
                case 16: return TensorDataType::FLOAT16;
                case 32: return TensorDataType::FLOAT32;
                case 64: return TensorDataType::FLOAT64;
                default:
                    return Error{ErrorCode::UNSUPPORTED_TYPE,
                                 "unsupported DLPack float bit width: "
                                 + std::to_string(dl_dtype.bits)};
            }

        case DLDataTypeCode::kDLInt:
            switch (dl_dtype.bits) {
                case 8:  return TensorDataType::INT8;
                case 16: return TensorDataType::INT16;
                case 32: return TensorDataType::INT32;
                case 64: return TensorDataType::INT64;
                default:
                    return Error{ErrorCode::UNSUPPORTED_TYPE,
                                 "unsupported DLPack int bit width: "
                                 + std::to_string(dl_dtype.bits)};
            }

        case DLDataTypeCode::kDLUInt:
            switch (dl_dtype.bits) {
                case 8:  return TensorDataType::UINT8;
                default:
                    return Error{ErrorCode::UNSUPPORTED_TYPE,
                                 "unsupported DLPack uint bit width: "
                                 + std::to_string(dl_dtype.bits)
                                 + " (only uint8 is supported)"};
            }

        case DLDataTypeCode::kDLBfloat:
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "bfloat16 is not supported by Signet TensorDataType"};

        default:
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "unknown DLPack data type code"};
    }
}

/// @}

/// @name Internal helpers for DLManagedTensor lifetime management
/// @{
namespace detail {

/// Context stored in DLManagedTensor.manager_ctx for non-owning exports.
///
/// Holds the shape array that the DLTensor.shape pointer references. The
/// deleter callback frees this context (and thus the shape vector) when
/// the consumer is done with the DLManagedTensor.
///
/// @note Does NOT own the tensor data -- the source TensorView must outlive
///       the DLManagedTensor.
struct DLPackViewCtx {
    std::vector<int64_t> shape;   ///< Shape array; DLTensor.shape points here
    // strides left empty for contiguous tensors (dl_tensor.strides = nullptr)
};

/// Context stored in DLManagedTensor.manager_ctx for owning exports.
///
/// Takes ownership of the OwnedTensor's data so that it stays alive
/// until the DLPack consumer calls the deleter. This enables true
/// zero-copy ownership transfer from Signet to DLPack consumers.
struct DLPackOwnedCtx {
    OwnedTensor          owned_tensor;   ///< Keeps data alive
    std::vector<int64_t> shape;          ///< DLTensor.shape points here
};

/// Deleter for a DLManagedTensor created from a TensorView (non-owning).
///
/// Frees the DLPackViewCtx (shape array) and the DLManagedTensor itself.
/// Does NOT free the underlying tensor data.
///
/// @param self  The DLManagedTensor to destroy (null-safe).
inline void dlpack_view_deleter(DLManagedTensor* self) {
    if (self == nullptr) return;
    if (self->manager_ctx != nullptr) {
        delete static_cast<DLPackViewCtx*>(self->manager_ctx);
        self->manager_ctx = nullptr;
    }
    delete self;
}

/// Deleter for a DLManagedTensor created from an OwnedTensor (owning).
///
/// Frees the DLPackOwnedCtx (which destroys the OwnedTensor and its data),
/// then frees the DLManagedTensor itself.
///
/// @param self  The DLManagedTensor to destroy (null-safe).
inline void dlpack_owned_deleter(DLManagedTensor* self) {
    if (self == nullptr) return;
    if (self->manager_ctx != nullptr) {
        delete static_cast<DLPackOwnedCtx*>(self->manager_ctx);
        self->manager_ctx = nullptr;
    }
    delete self;
}

} // namespace detail
/// @}

/// Exports and imports Signet tensors via DLPack, enabling zero-copy
/// interoperability with PyTorch, NumPy, JAX, and other DLPack-aware
/// frameworks.
///
/// Three export modes:
///   - **export_tensor()**: Zero-copy from TensorView (non-owning). Source must outlive consumer.
///   - **export_owned_tensor()**: Zero-copy ownership transfer from OwnedTensor. Consumer owns data.
///   - **to_buffer_info()**: Free function for Python buffer protocol (see below).
///
/// Two import modes:
///   - **import_tensor()**: Zero-copy into TensorView. DLManagedTensor must outlive the view.
///   - **import_tensor_copy()**: Deep copy into OwnedTensor. Supports strided (non-contiguous) data.
///
/// @code
///   // Export (Python side via pybind11):
///   DLManagedTensor* dl = *NumpyBridge::export_tensor(view);
///   // Wrap in PyCapsule named "dltensor", then:
///   //   torch.from_dlpack(capsule) or numpy.from_dlpack(capsule)
///
///   // Import (C++ side):
///   auto view = *NumpyBridge::import_tensor(managed);
/// @endcode
///
/// @note All methods are static -- NumpyBridge is a stateless utility class.
/// @note Only CPU and CUDAHost devices are supported for import.
/// @see BufferInfo, to_buffer_info
class NumpyBridge {
public:
    /// Export a TensorView as a DLManagedTensor (zero-copy, non-owning).
    ///
    /// The TensorView's data is NOT copied. The returned DLManagedTensor's
    /// DLTensor.data points directly into the TensorView's memory.
    ///
    /// IMPORTANT: The TensorView (and its underlying data) must outlive
    /// the DLManagedTensor. The consumer calls `deleter()` when done, which
    /// frees only the DLManagedTensor and its helper arrays, NOT the data.
    ///
    /// @param tensor  A valid, contiguous TensorView.
    /// @return        A heap-allocated DLManagedTensor (caller takes ownership
    ///                of the struct, not the data), or INTERNAL_ERROR for
    ///                invalid tensors, UNSUPPORTED_TYPE for non-contiguous tensors.
    /// @see export_owned_tensor, import_tensor
    static inline expected<DLManagedTensor*> export_tensor(const TensorView& tensor) {
        if (!tensor.is_valid()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "cannot export invalid tensor to DLPack"};
        }

        if (!tensor.is_contiguous()) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "non-contiguous tensor cannot be exported to DLPack; "
                         "call clone() first"};
        }

        // Build the manager context (owns shape array, NOT the data)
        auto* ctx = new detail::DLPackViewCtx();
        ctx->shape = tensor.shape().dims;

        // Build the DLManagedTensor
        auto* managed = new DLManagedTensor();
        managed->manager_ctx = ctx;
        managed->deleter     = detail::dlpack_view_deleter;

        DLTensor& dl = managed->dl_tensor;
        dl.data        = const_cast<void*>(tensor.data());
        dl.device      = DLDevice{DLDeviceType::kDLCPU, 0};
        dl.ndim        = static_cast<int32_t>(tensor.shape().ndim());
        dl.dtype       = to_dlpack_dtype(tensor.dtype());
        dl.shape       = ctx->shape.data();
        dl.strides     = nullptr; // contiguous C-order
        dl.byte_offset = 0;

        return managed;
    }

    /// Export an OwnedTensor as a DLManagedTensor (zero-copy ownership transfer).
    ///
    /// The OwnedTensor is moved into the DLManagedTensor's manager context.
    /// When the consumer calls the deleter, the OwnedTensor (and its data)
    /// is freed. This is a true zero-copy ownership transfer -- no data is
    /// copied and no source lifetime constraints apply.
    ///
    /// @param tensor  An OwnedTensor to transfer (moved from; left in a
    ///                valid but unspecified state after the call).
    /// @return        A heap-allocated DLManagedTensor that owns both the
    ///                struct and the tensor data, or INTERNAL_ERROR for
    ///                invalid tensors, UNSUPPORTED_TYPE for non-contiguous tensors.
    /// @see export_tensor, import_tensor_copy
    static inline expected<DLManagedTensor*> export_owned_tensor(OwnedTensor&& tensor) {
        TensorView view = tensor.view();

        if (!view.is_valid()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "cannot export invalid OwnedTensor to DLPack"};
        }

        if (!view.is_contiguous()) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "non-contiguous OwnedTensor cannot be exported to DLPack"};
        }

        // Build the manager context (owns the tensor data + shape array)
        auto* ctx = new detail::DLPackOwnedCtx();
        ctx->owned_tensor = std::move(tensor);
        ctx->shape = view.shape().dims;

        // Build the DLManagedTensor
        auto* managed = new DLManagedTensor();
        managed->manager_ctx = ctx;
        managed->deleter     = detail::dlpack_owned_deleter;

        DLTensor& dl = managed->dl_tensor;
        dl.data        = const_cast<void*>(view.data());
        dl.device      = DLDevice{DLDeviceType::kDLCPU, 0};
        dl.ndim        = static_cast<int32_t>(view.shape().ndim());
        dl.dtype       = to_dlpack_dtype(view.dtype());
        dl.shape       = ctx->shape.data();
        dl.strides     = nullptr; // contiguous C-order
        dl.byte_offset = 0;

        return managed;
    }

    /// Import a DLManagedTensor as a TensorView (zero-copy).
    ///
    /// The returned TensorView wraps the DLTensor's data directly. The
    /// DLManagedTensor (and thus the underlying data) must remain valid
    /// for the lifetime of the TensorView. The caller is still responsible
    /// for calling the DLManagedTensor's deleter when done.
    ///
    /// Only CPU and CUDAHost tensors are supported. Non-CPU devices are
    /// rejected. Strided (non-contiguous) tensors are rejected unless
    /// the strides match C-contiguous layout exactly.
    ///
    /// @param managed  A valid DLManagedTensor (must not be null).
    /// @return         A TensorView wrapping the DLPack data, or
    ///                 INTERNAL_ERROR for null/invalid inputs,
    ///                 UNSUPPORTED_TYPE for non-CPU devices, non-contiguous
    ///                 strides, or unsupported DLPack data types.
    /// @see import_tensor_copy, export_tensor
    static inline expected<TensorView> import_tensor(const DLManagedTensor* managed) {
        if (managed == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "null DLManagedTensor pointer"};
        }

        const DLTensor& dl = managed->dl_tensor;

        // Only CPU tensors are supported
        if (dl.device.device_type != DLDeviceType::kDLCPU &&
            dl.device.device_type != DLDeviceType::kDLCUDAHost) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "only CPU/CUDAHost DLPack tensors can be imported"};
        }

        if (dl.data == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "DLTensor data pointer is null"};
        }

        if (dl.ndim <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "DLTensor has zero or negative ndim"};
        }

        if (dl.shape == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "DLTensor shape pointer is null"};
        }

        // Reject strided (non-contiguous) tensors for TensorView import.
        // TensorView assumes C-contiguous layout.
        if (dl.strides != nullptr) {
            // Verify strides match C-contiguous layout
            int64_t expected_stride = 1;
            for (int32_t d = dl.ndim - 1; d >= 0; --d) {
                if (dl.strides[d] != expected_stride) {
                    return Error{ErrorCode::UNSUPPORTED_TYPE,
                                 "strided (non-contiguous) DLPack tensor cannot "
                                 "be imported as TensorView; use import_tensor_copy()"};
                }
                expected_stride *= dl.shape[d];
            }
        }

        auto dtype_result = from_dlpack_dtype(dl.dtype);
        if (!dtype_result) {
            return dtype_result.error();
        }

        // CWE-20: Improper Input Validation (DLPack §3.2 max_ndim)
        // L14: Reject unreasonable ndim values (DLPack spec uses int32_t)
        if (dl.ndim > 32) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "DLTensor ndim exceeds reasonable limit (32)"};
        }

        TensorShape shape;
        shape.dims.assign(dl.shape, dl.shape + dl.ndim);

        // CWE-190: Integer Overflow — validate byte_offset is within tensor data bounds
        const size_t elem_size = tensor_element_size(*dtype_result);
        size_t total_elements = 1;
        for (int32_t d = 0; d < dl.ndim; ++d) {
            total_elements *= static_cast<size_t>(dl.shape[d]);
        }
        // CWE-190: Integer Overflow — num_elements*elem_size checked below
        const size_t total_size = total_elements * elem_size;
        if (dl.byte_offset > total_size) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "DLPack byte_offset out of range"};
        }

        // Apply byte_offset
        void* data_ptr = static_cast<uint8_t*>(dl.data) + dl.byte_offset;

        return TensorView(data_ptr, shape, *dtype_result);
    }

    /// Import a DLManagedTensor as an OwnedTensor (deep copy).
    ///
    /// Copies the data from the DLPack tensor into a new heap allocation.
    /// After this call returns, the DLManagedTensor can be released
    /// independently -- the OwnedTensor owns its data.
    ///
    /// Only CPU and CUDAHost tensors are supported. Strided (non-contiguous)
    /// tensors ARE supported -- data is gathered into a contiguous C-order
    /// layout via element-by-element copy.
    ///
    /// For contiguous tensors, a fast memcpy path is used. For strided
    /// tensors, a multi-index iteration walks the source layout.
    ///
    /// @param managed  A valid DLManagedTensor (must not be null).
    /// @return         An OwnedTensor with a contiguous deep copy of the data,
    ///                 or INTERNAL_ERROR for null/invalid inputs,
    ///                 UNSUPPORTED_TYPE for non-CPU devices or unsupported dtypes.
    /// @see import_tensor, export_owned_tensor
    static inline expected<OwnedTensor> import_tensor_copy(const DLManagedTensor* managed) {
        if (managed == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "null DLManagedTensor pointer"};
        }

        const DLTensor& dl = managed->dl_tensor;

        if (dl.device.device_type != DLDeviceType::kDLCPU &&
            dl.device.device_type != DLDeviceType::kDLCUDAHost) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "only CPU/CUDAHost DLPack tensors can be imported"};
        }

        if (dl.data == nullptr || dl.ndim <= 0 || dl.shape == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "invalid DLTensor (null data/shape or non-positive ndim)"};
        }

        auto dtype_result = from_dlpack_dtype(dl.dtype);
        if (!dtype_result) {
            return dtype_result.error();
        }

        TensorDataType dtype = *dtype_result;
        const size_t elem_size = tensor_element_size(dtype);

        TensorShape shape;
        shape.dims.assign(dl.shape, dl.shape + dl.ndim);
        const size_t num_elements = shape.num_elements();

        // CWE-190: Integer Overflow — check for multiplication overflow before allocating
        if (elem_size != 0 && num_elements > SIZE_MAX / elem_size) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "DLPack tensor size overflow (num_elements * elem_size)"};
        }

        // Source data pointer with byte offset applied
        const uint8_t* src_base = static_cast<const uint8_t*>(dl.data)
                                + dl.byte_offset;

        // Check if contiguous -- if so, fast memcpy path
        bool is_contiguous = (dl.strides == nullptr);
        if (!is_contiguous && dl.strides != nullptr) {
            // Check if strides match C-contiguous
            int64_t expected_stride = 1;
            is_contiguous = true;
            for (int32_t d = dl.ndim - 1; d >= 0; --d) {
                if (dl.strides[d] != expected_stride) {
                    is_contiguous = false;
                    break;
                }
                expected_stride *= dl.shape[d];
            }
        }

        if (is_contiguous) {
            // Fast path: contiguous data, direct memcpy
            OwnedTensor result(shape, dtype);
            std::memcpy(result.data(), src_base, num_elements * elem_size);
            return result;
        }

        // Slow path: strided data, element-by-element copy.
        // Walk the multi-dimensional index space and compute source offsets
        // from strides.
        OwnedTensor result(shape, dtype);
        uint8_t* dst = static_cast<uint8_t*>(result.data());

        // Multi-index iteration
        std::vector<int64_t> idx(static_cast<size_t>(dl.ndim), 0);
        for (size_t flat = 0; flat < num_elements; ++flat) {
            // Compute source byte offset from strides
            int64_t src_elem_offset = 0;
            for (int32_t d = 0; d < dl.ndim; ++d) {
                src_elem_offset += idx[static_cast<size_t>(d)]
                                 * dl.strides[static_cast<size_t>(d)];
            }

            const uint8_t* src_elem = src_base
                + static_cast<size_t>(src_elem_offset) * elem_size;
            std::memcpy(dst + flat * elem_size, src_elem, elem_size);

            // Increment multi-index (row-major / C-order)
            for (int32_t d = dl.ndim - 1; d >= 0; --d) {
                auto ud = static_cast<size_t>(d);
                idx[ud]++;
                if (idx[ud] < dl.shape[d]) break;
                idx[ud] = 0;
            }
        }

        return result;
    }
};

/// Simple C-contiguous buffer descriptor for Python interop.
///
/// Compatible with Python's buffer protocol (PEP 3118) and pybind11's
/// py::buffer_info. Can also be used to construct NumPy's
/// `__array_interface__` dict.
///
/// Format strings follow Python struct module conventions:
///   "f" = float32, "d" = float64, "i" = int32, "l" = int64,
///   "b" = int8, "B" = uint8, "h" = int16, "e" = float16, "?" = bool
///
/// @note Strides are always in bytes (not elements), matching PEP 3118.
/// @see to_buffer_info
struct BufferInfo {
    void*                data;      ///< Pointer to contiguous data (non-owning)
    size_t               itemsize;  ///< Bytes per element (e.g. 4 for float32)
    std::string          format;    ///< Python struct format character (e.g. "f", "d")
    int64_t              ndim;      ///< Number of dimensions
    std::vector<int64_t> shape;     ///< Shape in each dimension (ndim elements)
    std::vector<int64_t> strides;   ///< Stride in bytes for each dimension (ndim elements)
};

/// @name Python buffer format helpers
/// @{
namespace detail {

/// Map TensorDataType to a Python struct format character (PEP 3118).
///
/// @param dtype  The Signet tensor data type.
/// @return       Single-character format string, or nullptr if no mapping
///               exists (should not occur for valid TensorDataType values).
inline const char* tensor_dtype_to_pybuf_format(TensorDataType dtype) {
    switch (dtype) {
        case TensorDataType::FLOAT32: return "f";
        case TensorDataType::FLOAT64: return "d";
        case TensorDataType::INT32:   return "i";
        case TensorDataType::INT64:   return "l";
        case TensorDataType::INT8:    return "b";
        case TensorDataType::UINT8:   return "B";
        case TensorDataType::INT16:   return "h";
        case TensorDataType::FLOAT16: return "e";
        case TensorDataType::BOOL:    return "?";
    }
    return nullptr; // unreachable
}

} // namespace detail
/// @}

/// Create a BufferInfo from a TensorView for Python buffer protocol export.
///
/// The tensor must be valid and contiguous. The returned BufferInfo's data
/// pointer points directly into the TensorView's memory (zero-copy). The
/// TensorView must remain valid for the lifetime of the BufferInfo.
///
/// Strides are computed as C-contiguous byte strides (innermost dimension
/// has stride = itemsize, outer dimensions are products of inner shapes).
///
/// @param tensor  A valid, contiguous TensorView.
/// @return        BufferInfo describing the tensor layout, or INTERNAL_ERROR
///                for invalid tensors, UNSUPPORTED_TYPE for non-contiguous
///                tensors or unmappable dtypes.
///
/// @code
///   auto info = *to_buffer_info(view);
///   // Use with pybind11:
///   //   return py::buffer_info(info.data, info.itemsize, info.format,
///   //                          info.ndim, info.shape, info.strides);
/// @endcode
///
/// @see BufferInfo, NumpyBridge
inline expected<BufferInfo> to_buffer_info(const TensorView& tensor) {
    if (!tensor.is_valid()) {
        return Error{ErrorCode::INTERNAL_ERROR,
                     "cannot create BufferInfo from invalid tensor"};
    }

    if (!tensor.is_contiguous()) {
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "non-contiguous tensor cannot be described by BufferInfo; "
                     "call clone() first"};
    }

    const char* fmt = detail::tensor_dtype_to_pybuf_format(tensor.dtype());
    if (fmt == nullptr) {
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "tensor dtype has no Python buffer format mapping"};
    }

    const size_t elem_size = tensor.element_size();
    const auto& dims = tensor.shape().dims;
    const int64_t ndim = static_cast<int64_t>(dims.size());

    // Compute C-contiguous strides (in bytes) from innermost to outermost
    std::vector<int64_t> strides(static_cast<size_t>(ndim));
    if (ndim > 0) {
        strides[static_cast<size_t>(ndim - 1)] = static_cast<int64_t>(elem_size);
        for (int64_t d = ndim - 2; d >= 0; --d) {
            auto ud  = static_cast<size_t>(d);
            auto ud1 = static_cast<size_t>(d + 1);
            strides[ud] = strides[ud1] * dims[ud1];
        }
    }

    BufferInfo info;
    info.data     = const_cast<void*>(tensor.data());
    info.itemsize = elem_size;
    info.format   = fmt;
    info.ndim     = ndim;
    info.shape    = dims;
    info.strides  = std::move(strides);

    return info;
}

} // namespace signet::forge
