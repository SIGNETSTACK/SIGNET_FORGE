// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file arrow_bridge.hpp
/// @brief Arrow C Data Interface interop for SignetStack Signet Forge.
///
/// Import/export Arrow arrays WITHOUT linking to Apache Arrow. Uses the
/// standard Arrow C Data Interface ABI (two plain C structs) defined at:
///   https://arrow.apache.org/docs/format/CDataInterface.html
///
/// Zero-copy for numeric types: ArrowArray.buffers[1] points directly into
/// tensor/column data. The release() callbacks clean up any helper state.
///
/// Header-only. Part of the signet::forge interop module (Phase 6).
///
/// @see ArrowExporter, ArrowImporter
/// @see https://arrow.apache.org/docs/format/CDataInterface.html

#include "signet/ai/tensor_bridge.hpp"
#include "signet/types.hpp"
#include "signet/error.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

/// @name Arrow C Data Interface structs (standard ABI)
/// @{
/// These are the canonical struct definitions from the Arrow specification.
/// Any library that speaks the Arrow C Data Interface can consume them.
/// They use C linkage (`extern "C"`) for maximum FFI compatibility.

#ifndef SIGNET_ARROW_C_DATA_DEFINED
#define SIGNET_ARROW_C_DATA_DEFINED

extern "C" {

/// Schema description for a single Arrow array/column.
///
/// Describes the logical type, name, and structure of an Arrow column.
/// The release callback must be called exactly once when the consumer is
/// finished with the schema; a null release pointer indicates the schema
/// has already been released.
///
/// @note This is a C struct with C linkage -- safe to pass across FFI boundaries.
/// @see https://arrow.apache.org/docs/format/CDataInterface.html#the-arrowschema-structure
struct ArrowSchema {
    const char*    format;       ///< Arrow format string (e.g. "f" = float32)
    const char*    name;         ///< Column/field name (may be null)
    const char*    metadata;     ///< Arrow key-value metadata (may be null)
    int64_t        flags;        ///< Bitfield: bit 1 = nullable, bit 2 = dict-ordered
    int64_t        n_children;   ///< Number of child schemas (0 for primitives)
    ArrowSchema**  children;     ///< Child schema pointers (null if n_children == 0)
    ArrowSchema*   dictionary;   ///< Dictionary schema (null if not dict-encoded)
    void (*release)(ArrowSchema*); ///< Release callback (null = already released)
    void*          private_data; ///< Opaque data for the release callback
};

/// Data payload for a single Arrow array.
///
/// Holds the buffer pointers, length, null count, and structure of a single
/// Arrow column or nested array. For primitive types, buffers[0] is the
/// optional validity bitmap and buffers[1] is the data buffer.
///
/// The release callback must be called exactly once when the consumer is
/// finished with the array; a null release pointer indicates the array
/// has already been released.
///
/// @note This is a C struct with C linkage -- safe to pass across FFI boundaries.
/// @see https://arrow.apache.org/docs/format/CDataInterface.html#the-arrowarray-structure
struct ArrowArray {
    int64_t        length;       ///< Number of logical elements
    int64_t        null_count;   ///< Number of null elements (0 if non-nullable)
    int64_t        offset;       ///< Logical offset into buffers
    int64_t        n_buffers;    ///< Number of buffers (typically 2 for primitives)
    int64_t        n_children;   ///< Number of child arrays (0 for primitives)
    const void**   buffers;      ///< Buffer pointers (buffers[0]=validity, buffers[1]=data)
    ArrowArray**   children;     ///< Child array pointers (null if n_children == 0)
    ArrowArray*    dictionary;   ///< Dictionary array (null if not dict-encoded)
    void (*release)(ArrowArray*); ///< Release callback (null = already released)
    void*          private_data; ///< Opaque data for the release callback
};

} // extern "C"
/// @}

#endif // SIGNET_ARROW_C_DATA_DEFINED

namespace signet::forge {

/// @name Internal helpers for Arrow C Data Interface release callbacks
/// @{
namespace detail {

/// Heap-allocated context attached to ArrowSchema.private_data.
///
/// Stores the dynamically-allocated format and name strings so the
/// release callback can free them. The ArrowSchema.format and
/// ArrowSchema.name pointers reference c_str() of these members.
struct ArrowSchemaPrivate {
    std::string format_storage;   ///< Backing storage for ArrowSchema.format
    std::string name_storage;     ///< Backing storage for ArrowSchema.name
};

/// Heap-allocated context attached to ArrowArray.private_data.
///
/// Tracks whether this bridge allocated the data buffer (owns_data),
/// and stores the two-element buffers pointer array that ArrowArray.buffers
/// references. When owns_data is true, the release callback will free
/// buffers[1] via std::free().
struct ArrowArrayPrivate {
    bool         owns_data   = false;   ///< If true, free buffers[1] on release
    const void*  buffer_ptrs[2] = {nullptr, nullptr}; ///< [0]=validity, [1]=data
};

/// Release callback for ArrowSchema. Frees the ArrowSchemaPrivate context.
///
/// After release, all pointer fields are set to nullptr and the release
/// function pointer itself is cleared (indicating "already released").
///
/// @param schema  The schema to release (null-safe).
inline void release_arrow_schema(ArrowSchema* schema) {
    if (schema == nullptr) return;
    if (schema->private_data != nullptr) {
        delete static_cast<ArrowSchemaPrivate*>(schema->private_data);
        schema->private_data = nullptr;
    }
    schema->format  = nullptr;
    schema->name    = nullptr;
    schema->release = nullptr;
}

/// Release callback for ArrowArray. Frees the ArrowArrayPrivate context
/// and optionally the data buffer (if owns_data is true).
///
/// After release, buffers and release are set to nullptr (indicating
/// "already released").
///
/// @param array  The array to release (null-safe).
inline void release_arrow_array(ArrowArray* array) {
    if (array == nullptr) return;
    if (array->private_data != nullptr) {
        auto* ctx = static_cast<ArrowArrayPrivate*>(array->private_data);
        if (ctx->owns_data && ctx->buffer_ptrs[1] != nullptr) {
            std::free(const_cast<void*>(ctx->buffer_ptrs[1]));
        }
        delete ctx;
        array->private_data = nullptr;
    }
    array->buffers = nullptr;
    array->release = nullptr;
}

} // namespace detail
/// @}

/// @name Format string mappings
/// @brief Conversion functions between Parquet/Tensor types and Arrow format strings.
/// @{

/// Map a Parquet PhysicalType to an Arrow format string.
///
/// Arrow format strings for primitive types:
///   "b" = bool, "c" = int8, "C" = uint8, "s" = int16, "S" = uint16,
///   "i" = int32, "I" = uint32, "l" = int64, "L" = uint64,
///   "f" = float32, "g" = float64, "e" = float16
///
/// @param pt  The Parquet physical type to convert.
/// @return    Single-character Arrow format string, or nullptr for types
///            that have no direct Arrow primitive mapping (BYTE_ARRAY,
///            FIXED_LEN_BYTE_ARRAY, INT96).
/// @see tensor_dtype_to_arrow_format
inline const char* parquet_to_arrow_format(PhysicalType pt) {
    switch (pt) {
        case PhysicalType::BOOLEAN: return "b";
        case PhysicalType::INT32:   return "i";
        case PhysicalType::INT64:   return "l";
        case PhysicalType::FLOAT:   return "f";
        case PhysicalType::DOUBLE:  return "g";
        default:                    return nullptr;
    }
}

/// Map a TensorDataType to an Arrow format string.
///
/// @param dtype  The tensor data type to convert.
/// @return       Single-character Arrow format string, or nullptr if
///               no mapping exists (should not occur for valid TensorDataType values).
/// @see parquet_to_arrow_format, arrow_format_to_tensor_dtype
inline const char* tensor_dtype_to_arrow_format(TensorDataType dtype) {
    switch (dtype) {
        case TensorDataType::FLOAT32: return "f";
        case TensorDataType::FLOAT64: return "g";
        case TensorDataType::INT32:   return "i";
        case TensorDataType::INT64:   return "l";
        case TensorDataType::INT8:    return "c";
        case TensorDataType::UINT8:   return "C";
        case TensorDataType::INT16:   return "s";
        case TensorDataType::FLOAT16: return "e";
        case TensorDataType::BOOL:    return "b";
    }
    return nullptr; // unreachable
}

/// Map an Arrow format string to a TensorDataType.
///
/// Supports the standard single-character format codes for primitive
/// numeric types and booleans. Multi-character format codes (e.g. "tss:"
/// for timestamp) are not supported and return UNSUPPORTED_TYPE.
///
/// @param format  Arrow format string (must not be null or empty).
/// @return        The corresponding TensorDataType on success, or an
///                UNSUPPORTED_TYPE error for unrecognized format strings.
/// @see tensor_dtype_to_arrow_format
inline expected<TensorDataType> arrow_format_to_tensor_dtype(const char* format) {
    if (format == nullptr || format[0] == '\0') {
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     "null or empty Arrow format string"};
    }

    // Only single-character format codes are handled for primitive types.
    if (format[1] != '\0') {
        return Error{ErrorCode::UNSUPPORTED_TYPE,
                     std::string("unsupported Arrow format: ") + format};
    }

    switch (format[0]) {
        case 'f': return TensorDataType::FLOAT32;
        case 'g': return TensorDataType::FLOAT64;
        case 'i': return TensorDataType::INT32;
        case 'l': return TensorDataType::INT64;
        case 'c': return TensorDataType::INT8;
        case 'C': return TensorDataType::UINT8;
        case 's': return TensorDataType::INT16;
        case 'e': return TensorDataType::FLOAT16;
        case 'b': return TensorDataType::BOOL;
        default:
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         std::string("unsupported Arrow format character: '")
                         + format[0] + "'"};
    }
}

/// Map a PhysicalType to a TensorDataType (for column export).
///
/// @param pt  The Parquet physical type.
/// @return    The corresponding TensorDataType, or UNSUPPORTED_TYPE for
///            variable-length types (BYTE_ARRAY, FIXED_LEN_BYTE_ARRAY, INT96).
inline expected<TensorDataType> physical_to_tensor_dtype(PhysicalType pt) {
    switch (pt) {
        case PhysicalType::BOOLEAN: return TensorDataType::BOOL;
        case PhysicalType::INT32:   return TensorDataType::INT32;
        case PhysicalType::INT64:   return TensorDataType::INT64;
        case PhysicalType::FLOAT:   return TensorDataType::FLOAT32;
        case PhysicalType::DOUBLE:  return TensorDataType::FLOAT64;
        default:
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "PhysicalType has no TensorDataType mapping"};
    }
}

/// Return the byte size for a PhysicalType (primitive types only).
///
/// @param pt  The Parquet physical type.
/// @return    Byte size of one element (1 for BOOLEAN, 4 for INT32/FLOAT,
///            8 for INT64/DOUBLE), or 0 for variable-length types.
/// @note      BOOLEAN returns 1 (byte-aligned storage), even though Arrow
///            uses 1-bit packing in validity bitmaps.
inline size_t physical_type_byte_size(PhysicalType pt) {
    switch (pt) {
        case PhysicalType::BOOLEAN: return 1; // Arrow uses 1 bit, but data buffer is byte-aligned
        case PhysicalType::INT32:   return 4;
        case PhysicalType::INT64:   return 8;
        case PhysicalType::FLOAT:   return 4;
        case PhysicalType::DOUBLE:  return 8;
        default:                    return 0;
    }
}

/// @}

/// Exports Signet Forge tensors and columns as Arrow C Data Interface structs.
///
/// For numeric types this is zero-copy: the ArrowArray.buffers[1]
/// pointer is set directly to the source data. The source data must
/// remain valid for the entire lifetime of the exported ArrowArray.
///
/// The caller is responsible for calling the release() callbacks on both
/// the ArrowArray and ArrowSchema when done.
///
/// @note All exported arrays are non-nullable (null_count = 0, no validity bitmap).
/// @note All methods are static -- ArrowExporter is a stateless utility class.
///
/// @code
///   ArrowArray arr;
///   ArrowSchema sch;
///   auto result = ArrowExporter::export_tensor(view, "prices", &arr, &sch);
///   // ... pass arr + sch to Arrow consumer ...
///   arr.release(&arr);
///   sch.release(&sch);
/// @endcode
///
/// @see ArrowImporter
class ArrowExporter {
public:
    /// Export a TensorView as an ArrowArray + ArrowSchema pair (zero-copy).
    ///
    /// The tensor is flattened to 1D for Arrow (Arrow arrays are 1D).
    /// For numeric types, this is zero-copy: buffers[1] points into the
    /// TensorView's memory. The TensorView must remain valid for the
    /// entire lifetime of the ArrowArray.
    ///
    /// @param tensor      The tensor to export (must be valid and contiguous).
    /// @param name        The column/field name for the ArrowSchema.
    /// @param out_array   Output ArrowArray (caller-allocated, populated on success).
    /// @param out_schema  Output ArrowSchema (caller-allocated, populated on success).
    /// @return            Success, or INTERNAL_ERROR for invalid tensors,
    ///                    UNSUPPORTED_TYPE for non-contiguous or unmappable dtypes.
    /// @note The caller must call out_array->release(out_array) and
    ///       out_schema->release(out_schema) when done.
    /// @see export_column
    static inline expected<void> export_tensor(
        const TensorView& tensor,
        const std::string& name,
        ArrowArray* out_array,
        ArrowSchema* out_schema)
    {
        // CWE-457: Use of Uninitialized Variable — zero-init prevents double-free on partial init failure
        // M27: Zero-initialize outputs so callers see release=nullptr on early error
        std::memset(out_schema, 0, sizeof(ArrowSchema));
        std::memset(out_array, 0, sizeof(ArrowArray));

        if (!tensor.is_valid()) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "cannot export invalid tensor to Arrow"};
        }

        if (!tensor.is_contiguous()) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "non-contiguous tensor cannot be exported to Arrow"};
        }

        const char* fmt = tensor_dtype_to_arrow_format(tensor.dtype());
        if (fmt == nullptr) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "tensor dtype has no Arrow format mapping"};
        }

        // -- Fill schema (RAII: unique_ptr until setup complete) --
        auto schema_owner = std::make_unique<detail::ArrowSchemaPrivate>();
        schema_owner->format_storage = fmt;
        schema_owner->name_storage   = name;

        out_schema->format       = schema_owner->format_storage.c_str();
        out_schema->name         = schema_owner->name_storage.c_str();
        out_schema->metadata     = nullptr;
        out_schema->flags        = 0;   // non-nullable
        out_schema->n_children   = 0;
        out_schema->children     = nullptr;
        out_schema->dictionary   = nullptr;
        out_schema->release      = detail::release_arrow_schema;
        out_schema->private_data = schema_owner.release(); // transfer ownership

        // -- Fill array (RAII: unique_ptr until setup complete) --
        auto array_owner = std::make_unique<detail::ArrowArrayPrivate>();
        array_owner->owns_data      = false; // zero-copy
        array_owner->buffer_ptrs[0] = nullptr;  // no validity bitmap (non-nullable)
        array_owner->buffer_ptrs[1] = tensor.data();

        const int64_t num_elements = static_cast<int64_t>(tensor.num_elements());

        out_array->length       = num_elements;
        out_array->null_count   = 0;
        out_array->offset       = 0;
        out_array->n_buffers    = 2;
        out_array->n_children   = 0;
        out_array->buffers      = array_owner->buffer_ptrs;
        out_array->children     = nullptr;
        out_array->dictionary   = nullptr;
        out_array->release      = detail::release_arrow_array;
        out_array->private_data = array_owner.release(); // transfer ownership

        return expected<void>{};
    }

    /// Export a 1D column of a primitive Parquet type as ArrowArray + ArrowSchema (zero-copy).
    ///
    /// For numeric physical types (INT32, INT64, FLOAT, DOUBLE, BOOLEAN),
    /// this is zero-copy: buffers[1] points to the raw data pointer. The
    /// data pointer must remain valid for the entire lifetime of the ArrowArray.
    ///
    /// @param data           Pointer to contiguous column data (must not be null).
    /// @param num_values     Number of values in the column (must be > 0).
    /// @param physical_type  Parquet physical type of the column.
    /// @param name           Column name for the ArrowSchema.
    /// @param out_array      Output ArrowArray (caller-allocated, populated on success).
    /// @param out_schema     Output ArrowSchema (caller-allocated, populated on success).
    /// @return               Success, or INTERNAL_ERROR for null/empty data,
    ///                       UNSUPPORTED_TYPE for variable-length physical types.
    /// @note The caller must call out_array->release(out_array) and
    ///       out_schema->release(out_schema) when done.
    /// @see export_tensor
    static inline expected<void> export_column(
        const void* data,
        int64_t num_values,
        PhysicalType physical_type,
        const std::string& name,
        ArrowArray* out_array,
        ArrowSchema* out_schema)
    {
        // CWE-457: Use of Uninitialized Variable — zero-init prevents double-free on partial init failure
        // M27: Zero-initialize outputs so callers see release=nullptr on early error
        std::memset(out_schema, 0, sizeof(ArrowSchema));
        std::memset(out_array, 0, sizeof(ArrowArray));

        if (data == nullptr || num_values <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "cannot export null/empty column to Arrow"};
        }

        const char* fmt = parquet_to_arrow_format(physical_type);
        if (fmt == nullptr) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "PhysicalType has no direct Arrow format mapping"};
        }

        // -- Fill schema (RAII: unique_ptr until setup complete) --
        auto schema_owner = std::make_unique<detail::ArrowSchemaPrivate>();
        schema_owner->format_storage = fmt;
        schema_owner->name_storage   = name;

        out_schema->format       = schema_owner->format_storage.c_str();
        out_schema->name         = schema_owner->name_storage.c_str();
        out_schema->metadata     = nullptr;
        out_schema->flags        = 0;
        out_schema->n_children   = 0;
        out_schema->children     = nullptr;
        out_schema->dictionary   = nullptr;
        out_schema->release      = detail::release_arrow_schema;
        out_schema->private_data = schema_owner.release(); // transfer ownership

        // -- Fill array (RAII: unique_ptr until setup complete) --
        auto array_owner = std::make_unique<detail::ArrowArrayPrivate>();
        array_owner->owns_data      = false;
        array_owner->buffer_ptrs[0] = nullptr; // no validity bitmap
        array_owner->buffer_ptrs[1] = data;

        out_array->length       = num_values;
        out_array->null_count   = 0;
        out_array->offset       = 0;
        out_array->n_buffers    = 2;
        out_array->n_children   = 0;
        out_array->buffers      = array_owner->buffer_ptrs;
        out_array->children     = nullptr;
        out_array->dictionary   = nullptr;
        out_array->release      = detail::release_arrow_array;
        out_array->private_data = array_owner.release(); // transfer ownership

        return expected<void>{};
    }
};

/// Imports Arrow C Data Interface arrays into Signet TensorView or OwnedTensor.
///
/// Supports two import modes:
///   - **Zero-copy** via import_array(): returns a TensorView wrapping the
///     Arrow data buffer directly. The ArrowArray must remain alive.
///   - **Deep-copy** via import_array_copy(): returns an OwnedTensor with
///     an independent heap copy. The ArrowArray can be released immediately.
///
/// @note Only primitive numeric types with single-character Arrow format
///       codes are supported. Nullable arrays (null_count != 0) are rejected.
/// @note A defense-in-depth cap of 1 billion elements is enforced to prevent
///       OOB reads from crafted metadata (Arrow C Data Interface does not
///       carry buffer sizes).
/// @note All methods are static -- ArrowImporter is a stateless utility class.
///
/// @see ArrowExporter
class ArrowImporter {
public:
    /// Import an ArrowArray as a TensorView (zero-copy).
    ///
    /// The ArrowArray must contain a single-buffer primitive numeric type
    /// (matching a single-character format string). The returned TensorView
    /// wraps buffers[1] directly -- the ArrowArray must remain alive and
    /// unreleased for the entire lifetime of the TensorView.
    ///
    /// The tensor is shaped as 1D: [length]. The ArrowArray.offset field
    /// is respected (data pointer is advanced by offset * element_size).
    ///
    /// @param array   Pointer to a valid, unreleased ArrowArray (must not be null).
    /// @param schema  Pointer to a valid ArrowSchema describing the array (must not be null).
    /// @return        A 1D TensorView wrapping the Arrow data, or an error.
    ///
    /// Validation checks performed:
    ///   - Null/released array or schema
    ///   - null_count != 0 (nullable arrays rejected)
    ///   - Unsupported or multi-character format strings
    ///   - Zero/negative length, negative offset
    ///   - Length exceeding 1 billion element import cap
    ///   - Offset + length overflow
    ///   - Missing data buffer (buffers[1])
    ///
    /// @see import_array_copy
    static inline expected<TensorView> import_array(
        const ArrowArray* array,
        const ArrowSchema* schema)
    {
        if (array == nullptr || schema == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "null ArrowArray or ArrowSchema pointer"};
        }

        if (array->release == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "ArrowArray has already been released"};
        }

        if (array->null_count != 0) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "ArrowArray with nulls cannot be imported as a dense tensor"};
        }

        auto dtype_result = arrow_format_to_tensor_dtype(schema->format);
        if (!dtype_result) {
            return dtype_result.error();
        }

        TensorDataType dtype = *dtype_result;
        const int64_t length = array->length;

        if (length <= 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "ArrowArray has zero or negative length"};
        }

        // Reject unreasonably large arrays to prevent OOB reads in copy paths.
        // Arrow C Data Interface doesn't carry buffer sizes, so this is a
        // defense-in-depth cap. 1 billion elements ≈ 8 GB for float64.
        static constexpr int64_t MAX_IMPORT_ELEMENTS = 1'000'000'000;
        static constexpr int64_t MAX_ARROW_OFFSET = 1'000'000'000LL;
        static constexpr int64_t MAX_ARROW_LENGTH = 1'000'000'000LL;

        if (array->offset > MAX_ARROW_OFFSET) {
            return Error{ErrorCode::INVALID_ARGUMENT, "ArrowArray offset exceeds 1B cap (CWE-190)"};
        }
        if (array->length > MAX_ARROW_LENGTH) {
            return Error{ErrorCode::INVALID_ARGUMENT, "ArrowArray length exceeds 1B cap (CWE-190)"};
        }

        if (length > MAX_IMPORT_ELEMENTS) {
            return Error{ErrorCode::IO_ERROR,
                         "ArrowArray length exceeds import limit (1 billion elements)"};
        }

        if (array->offset < 0) {
            return Error{ErrorCode::IO_ERROR,
                         "ArrowArray has negative offset"};
        }

        if (array->n_buffers < 2 || array->buffers == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "ArrowArray does not have expected buffer layout"};
        }

        const void* data_buf = array->buffers[1];
        if (data_buf == nullptr) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "ArrowArray data buffer (buffers[1]) is null"};
        }

        if (static_cast<int64_t>(length) > INT64_MAX - array->offset) {
            return Error{ErrorCode::IO_ERROR,
                         "ArrowArray: offset + length overflows int64"};
        }

        // Account for offset: data starts at offset * element_size bytes
        const size_t elem_size = tensor_element_size(dtype);
        const size_t offset_val = static_cast<size_t>(array->offset);
        if (offset_val > static_cast<size_t>(MAX_IMPORT_ELEMENTS)) {
            return Error{ErrorCode::IO_ERROR,
                         "ArrowArray offset exceeds import limit"};
        }

        // Validate (offset + length) * elem_size doesn't overflow size_t.
        // The Arrow C Data Interface doesn't carry buffer sizes, so we
        // cannot verify the buffer is large enough.  This overflow guard
        // prevents wild pointer arithmetic from crafted metadata.
        const size_t total_elems = offset_val + static_cast<size_t>(length);
        if (total_elems > static_cast<size_t>(MAX_IMPORT_ELEMENTS)) {
            return Error{ErrorCode::IO_ERROR,
                         "ArrowArray: offset + length exceeds import limit"};
        }
        if (elem_size > 0 && total_elems > SIZE_MAX / elem_size) {
            return Error{ErrorCode::IO_ERROR,
                         "ArrowArray: (offset + length) * elem_size overflows size_t"};
        }
        const uint8_t* base = static_cast<const uint8_t*>(data_buf)
                            + offset_val * elem_size;

        TensorShape shape;
        shape.dims = {length};

        return TensorView(const_cast<void*>(static_cast<const void*>(base)),
                          shape, dtype);
    }

    /// Import an ArrowArray as an OwnedTensor (deep copy).
    ///
    /// Copies the data from the ArrowArray into a new heap allocation.
    /// After this call returns, the ArrowArray can be released independently
    /// -- the OwnedTensor owns its data.
    ///
    /// Internally delegates to import_array() for validation, then copies
    /// the TensorView's data into the OwnedTensor.
    ///
    /// @param array   Pointer to a valid, unreleased ArrowArray (must not be null).
    /// @param schema  Pointer to a valid ArrowSchema describing the array (must not be null).
    /// @return        An OwnedTensor with a deep copy of the Arrow data, or
    ///                any error that import_array() would return.
    /// @see import_array
    static inline expected<OwnedTensor> import_array_copy(
        const ArrowArray* array,
        const ArrowSchema* schema)
    {
        auto view_result = import_array(array, schema);
        if (!view_result) {
            return view_result.error();
        }

        // Create an owned copy from the view
        const auto& v = *view_result;
        return OwnedTensor(v.data(), v.shape(), v.dtype());
    }
};

} // namespace signet::forge
