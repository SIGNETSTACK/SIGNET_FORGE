// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file onnx_bridge.hpp
/// @brief ONNX Runtime interop for SignetStack Signet Forge tensor bridge.
///
/// Creates OrtValue-compatible memory layouts WITHOUT linking to ONNX Runtime.
/// Users who have ONNX Runtime can create OrtValues from the exported data
/// via OrtApi::CreateTensorWithDataAsOrtValue.
///
/// Zero-copy for all supported numeric types (FLOAT32, FLOAT64, INT32, INT64,
/// INT8, UINT8, INT16, FLOAT16, BOOL) -- exports the data pointer directly.
///
/// Header-only. Part of the signet::forge interop module (Phase 6).
///
/// @see OnnxTensorInfo, OnnxInputSet, prepare_for_onnx

#include "signet/ai/tensor_bridge.hpp"
#include "signet/error.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace signet::forge {

/// ONNX tensor element data types, mirroring OrtTensorElementDataType.
///
/// Numeric values match the ONNX Runtime C API's OrtTensorElementDataType
/// enum exactly, so they can be cast directly via `static_cast<>` when
/// constructing OrtValues.
///
/// @note Not all types have Signet TensorDataType equivalents. Use
///       from_onnx_type() to convert, which returns an error for
///       unsupported types (STRING, UINT16, UINT32, UINT64, BFLOAT16).
/// @see to_onnx_type, from_onnx_type, onnx_type_name
enum class OnnxTensorType : int32_t {
    UNDEFINED = 0,    ///< No type (invalid / uninitialized)
    FLOAT     = 1,    ///< 32-bit IEEE float (float32)
    UINT8     = 2,    ///< 8-bit unsigned integer
    INT8      = 3,    ///< 8-bit signed integer
    UINT16    = 4,    ///< 16-bit unsigned integer
    INT16     = 5,    ///< 16-bit signed integer
    INT32     = 6,    ///< 32-bit signed integer
    INT64     = 7,    ///< 64-bit signed integer
    STRING    = 8,    ///< Variable-length string
    BOOL      = 9,    ///< Boolean (1 byte)
    FLOAT16   = 10,   ///< 16-bit IEEE float (float16)
    DOUBLE    = 11,   ///< 64-bit IEEE float (float64)
    UINT32    = 12,   ///< 32-bit unsigned integer
    UINT64    = 13,   ///< 64-bit unsigned integer
    BFLOAT16  = 16    ///< Brain floating-point (bfloat16)
};

/// @name Type conversion: TensorDataType <-> OnnxTensorType
/// @{

/// Convert a Signet TensorDataType to the corresponding OnnxTensorType.
///
/// All TensorDataType values have a direct ONNX mapping; this function
/// is total (never returns UNDEFINED for valid inputs).
///
/// @param dtype  The Signet tensor data type.
/// @return       The corresponding OnnxTensorType.
/// @see from_onnx_type
inline OnnxTensorType to_onnx_type(TensorDataType dtype) {
    switch (dtype) {
        case TensorDataType::FLOAT32: return OnnxTensorType::FLOAT;
        case TensorDataType::FLOAT64: return OnnxTensorType::DOUBLE;
        case TensorDataType::INT32:   return OnnxTensorType::INT32;
        case TensorDataType::INT64:   return OnnxTensorType::INT64;
        case TensorDataType::INT8:    return OnnxTensorType::INT8;
        case TensorDataType::UINT8:   return OnnxTensorType::UINT8;
        case TensorDataType::INT16:   return OnnxTensorType::INT16;
        case TensorDataType::FLOAT16: return OnnxTensorType::FLOAT16;
        case TensorDataType::BOOL:    return OnnxTensorType::BOOL;
    }
    return OnnxTensorType::UNDEFINED; // unreachable, silence warnings
}

/// Convert an OnnxTensorType back to a Signet TensorDataType.
///
/// @param ort_type  The ONNX tensor element type.
/// @return          The corresponding TensorDataType, or UNSUPPORTED_TYPE
///                  for types that have no Signet equivalent (STRING,
///                  UINT16, UINT32, UINT64, BFLOAT16, UNDEFINED).
/// @see to_onnx_type
inline expected<TensorDataType> from_onnx_type(OnnxTensorType ort_type) {
    switch (ort_type) {
        case OnnxTensorType::FLOAT:   return TensorDataType::FLOAT32;
        case OnnxTensorType::DOUBLE:  return TensorDataType::FLOAT64;
        case OnnxTensorType::INT32:   return TensorDataType::INT32;
        case OnnxTensorType::INT64:   return TensorDataType::INT64;
        case OnnxTensorType::INT8:    return TensorDataType::INT8;
        case OnnxTensorType::UINT8:   return TensorDataType::UINT8;
        case OnnxTensorType::INT16:   return TensorDataType::INT16;
        case OnnxTensorType::FLOAT16: return TensorDataType::FLOAT16;
        case OnnxTensorType::BOOL:    return TensorDataType::BOOL;

        case OnnxTensorType::STRING:
        case OnnxTensorType::UINT16:
        case OnnxTensorType::UINT32:
        case OnnxTensorType::UINT64:
        case OnnxTensorType::BFLOAT16:
        case OnnxTensorType::UNDEFINED:
        default:
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "OnnxTensorType has no Signet TensorDataType equivalent"};
    }
}

/// @}

/// Contains all information needed to create an OrtValue externally.
///
/// This struct aggregates the data pointer, shape, element type, and byte
/// size that OrtApi::CreateTensorWithDataAsOrtValue requires. No ONNX
/// Runtime headers are needed to populate or consume this struct.
///
/// @code
///   auto info = *prepare_for_onnx(tensor);
///   OrtValue* ort_val = nullptr;
///   OrtMemoryInfo* mem_info = nullptr;
///   ort_api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &mem_info);
///   ort_api->CreateTensorWithDataAsOrtValue(
///       mem_info,
///       info.data,
///       info.byte_size,
///       info.shape.data(),
///       info.shape.size(),
///       static_cast<ONNXTensorElementDataType>(info.element_type),
///       &ort_val);
/// @endcode
///
/// @see prepare_for_onnx, OnnxInputSet
struct OnnxTensorInfo {
    void*                  data         = nullptr;   ///< Pointer to contiguous tensor data (non-owning unless is_owner)
    std::vector<int64_t>   shape;                    ///< ONNX shape dimensions (e.g. {batch, features})
    OnnxTensorType         element_type = OnnxTensorType::UNDEFINED; ///< ONNX element data type
    size_t                 byte_size    = 0;          ///< Total data size in bytes (product of shape * element size)
    bool                   is_owner     = false;      ///< If true, the data was allocated by the bridge and the caller must free it

    /// Check whether this info is ready to be used with
    /// OrtApi::CreateTensorWithDataAsOrtValue.
    ///
    /// @return true if data is non-null, byte_size > 0, shape is non-empty,
    ///         and element_type is not UNDEFINED.
    [[nodiscard]] bool is_valid() const {
        return data != nullptr
            && byte_size > 0
            && !shape.empty()
            && element_type != OnnxTensorType::UNDEFINED;
    }
};

/// @name Zero-copy tensor export for ONNX Runtime
/// @{

/// Prepare a TensorView for ONNX Runtime consumption (zero-copy).
///
/// For all supported numeric types (FLOAT32, FLOAT64, INT32, INT64, INT8,
/// UINT8, INT16, FLOAT16, BOOL), this is zero-copy: the returned
/// OnnxTensorInfo.data points directly into the TensorView's memory.
///
/// The TensorView must remain valid for the lifetime of the returned info
/// (is_owner will be false). The tensor must be contiguous; non-contiguous
/// tensors are rejected -- call clone() first to produce a contiguous copy.
///
/// @param tensor  The TensorView to export (must be valid and contiguous).
/// @return        OnnxTensorInfo ready for OrtApi::CreateTensorWithDataAsOrtValue,
///                or INTERNAL_ERROR for invalid tensors, UNSUPPORTED_TYPE for
///                non-contiguous tensors or unmappable dtypes.
/// @see OnnxTensorInfo, prepare_inputs_for_onnx
inline expected<OnnxTensorInfo> prepare_for_onnx(const TensorView& tensor) {
    if (!tensor.is_valid()) {
        return Error{ErrorCode::INTERNAL_ERROR,
                     "cannot prepare invalid tensor for ONNX"};
    }

    if (!tensor.is_contiguous()) {
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "non-contiguous tensors cannot be exported to ONNX; "
                     "call clone() first to produce a contiguous copy"};
    }

    // CWE-20: Improper Input Validation — all ONNX dimensions must be positive
    for (auto d : tensor.shape().dims) {
        if (d <= 0) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "ONNX tensor dimensions must be positive"};
        }
    }

    OnnxTensorInfo info;
    // M28 WARNING: ONNX Runtime requires non-const void*. The caller MUST ensure
    // the source tensor data is not backed by read-only memory (e.g., mmap PROT_READ).
    // If the tensor originates from an mmap'd file, copy it first via OwnedTensor.
    info.data         = const_cast<void*>(tensor.data());
    info.shape        = tensor.shape().dims;
    info.element_type = to_onnx_type(tensor.dtype());
    info.byte_size    = tensor.byte_size();
    info.is_owner     = false; // zero-copy: TensorView owns the data

    if (info.element_type == OnnxTensorType::UNDEFINED) {
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "tensor dtype maps to UNDEFINED ONNX type"};
    }

    return info;
}

/// Prepare an OwnedTensor for ONNX Runtime consumption (zero-copy).
///
/// Delegates to the TensorView overload via the OwnedTensor's view().
/// The OwnedTensor must remain valid for the lifetime of the returned info.
///
/// @param tensor  The OwnedTensor to export (must be valid and contiguous).
/// @return        OnnxTensorInfo (zero-copy, is_owner = false), or an error.
/// @see prepare_for_onnx(const TensorView&)
inline expected<OnnxTensorInfo> prepare_for_onnx(const OwnedTensor& tensor) {
    return prepare_for_onnx(tensor.view());
}

/// @}

/// A set of named ONNX tensors for multi-input model inference.
///
/// Names and tensors are stored in parallel vectors. The names correspond
/// to model input names as defined in the ONNX model graph, and the tensors
/// hold OnnxTensorInfo structs ready for OrtApi::CreateTensorWithDataAsOrtValue.
///
/// @see prepare_inputs_for_onnx
struct OnnxInputSet {
    std::vector<std::string>     names;    ///< Model input names (parallel with tensors)
    std::vector<OnnxTensorInfo>  tensors;  ///< Prepared tensor infos (parallel with names)

    /// Check whether all tensors are valid and the set is non-empty.
    ///
    /// @return true if names and tensors have the same non-zero size and
    ///         every OnnxTensorInfo passes is_valid().
    [[nodiscard]] bool is_valid() const {
        if (names.empty() || names.size() != tensors.size()) return false;
        for (const auto& t : tensors) {
            if (!t.is_valid()) return false;
        }
        return true;
    }
};

/// Prepare a batch of named TensorViews for ONNX Runtime inference.
///
/// Each pair is (input_name, tensor_view). All tensors must be valid and
/// contiguous. If any tensor fails preparation, the entire call fails
/// with an error message identifying the failing input by name.
///
/// @param inputs  Non-empty vector of (name, TensorView) pairs. Names
///                should match the model's input node names.
/// @return        OnnxInputSet with all tensors prepared (zero-copy),
///                or INTERNAL_ERROR for empty inputs, or any error that
///                prepare_for_onnx() would return (prefixed with the input name).
/// @see OnnxInputSet, prepare_for_onnx
inline expected<OnnxInputSet> prepare_inputs_for_onnx(
    const std::vector<std::pair<std::string, TensorView>>& inputs)
{
    if (inputs.empty()) {
        return Error{ErrorCode::INTERNAL_ERROR,
                     "cannot prepare empty input set for ONNX"};
    }

    OnnxInputSet result;
    result.names.reserve(inputs.size());
    result.tensors.reserve(inputs.size());

    for (const auto& [name, tensor] : inputs) {
        auto info = prepare_for_onnx(tensor);
        if (!info) {
            return Error{info.error().code,
                         "failed to prepare ONNX input '" + name + "': "
                         + info.error().message};
        }
        result.names.push_back(name);
        result.tensors.push_back(std::move(*info));
    }

    return result;
}

/// Return a human-readable string for an OnnxTensorType value.
///
/// Useful for diagnostics, logging, and error messages. Returns "UNKNOWN"
/// for values not in the OnnxTensorType enumeration.
///
/// @param t  The ONNX tensor type.
/// @return   A static string literal (e.g. "FLOAT", "INT64", "UNDEFINED").
///           Never returns nullptr.
inline const char* onnx_type_name(OnnxTensorType t) {
    switch (t) {
        case OnnxTensorType::UNDEFINED: return "UNDEFINED";
        case OnnxTensorType::FLOAT:     return "FLOAT";
        case OnnxTensorType::UINT8:     return "UINT8";
        case OnnxTensorType::INT8:      return "INT8";
        case OnnxTensorType::UINT16:    return "UINT16";
        case OnnxTensorType::INT16:     return "INT16";
        case OnnxTensorType::INT32:     return "INT32";
        case OnnxTensorType::INT64:     return "INT64";
        case OnnxTensorType::STRING:    return "STRING";
        case OnnxTensorType::BOOL:      return "BOOL";
        case OnnxTensorType::FLOAT16:   return "FLOAT16";
        case OnnxTensorType::DOUBLE:    return "DOUBLE";
        case OnnxTensorType::UINT32:    return "UINT32";
        case OnnxTensorType::UINT64:    return "UINT64";
        case OnnxTensorType::BFLOAT16:  return "BFLOAT16";
        default:                        return "UNKNOWN";
    }
}

} // namespace signet::forge
