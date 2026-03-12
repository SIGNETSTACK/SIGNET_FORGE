// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// column_batch.hpp — Arena-backed columnar event batch for SignetStack Signet Forge
//
// ColumnBatch stores N rows × M feature columns in column-major layout so
// that each column is a contiguous double[] suitable for zero-copy wrapping
// as a TensorView / OrtValue without transposition.
//
// Usage pattern (producer):
//   auto batch = ColumnBatch::with_schema({{"price", TDT::FLOAT64},
//                                          {"qty",   TDT::FLOAT64}}, 512);
//   batch.push_row({mid, qty});
//   ...
//   bus.publish(std::make_shared<ColumnBatch>(std::move(batch)));
//
// Usage pattern (consumer / ML inference):
//   auto tv  = batch->column_view(0);          // zero-copy TensorView
//   auto ot  = batch->as_tensor(TDT::FLOAT32); // 2D OwnedTensor [rows, cols]
//   auto rec = batch->to_stream_record(ts_ns);  // serialise → WAL
//
// Phase 9b: MPMC ColumnBatch Event Bus.

/// @file column_batch.hpp
/// @brief Column-major batch of feature rows for zero-copy tensor wrapping
///        and WAL serialization in ML inference pipelines.

#pragma once

#include "signet/error.hpp"
#include "signet/ai/tensor_bridge.hpp"
#include "signet/ai/streaming_sink.hpp"   // StreamRecord

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace signet::forge {

/// Convenience alias for TensorDataType (shorter schema declarations).
using TDT = TensorDataType;

// ============================================================================
// ColumnDesc — schema descriptor for one column in a ColumnBatch
// ============================================================================

/// Describes a single column in a ColumnBatch schema.
struct ColumnDesc {
    std::string    name;                                  ///< Column name (e.g. "price", "volume")
    TensorDataType dtype = TensorDataType::FLOAT64;       ///< Physical storage type (always stored as double internally)
};

// ============================================================================
// ColumnBatch — columnar, reference-counted event batch
//
// Data layout: columns_[col_idx][row_idx] — column-major for zero-copy tensor
// wrapping.  Each column is a contiguous std::vector<double>.
// ============================================================================

/// A column-major batch of feature rows for ML inference and WAL serialization.
///
/// Data is stored in column-major layout (columns_[col][row]) so each column
/// is a contiguous double array suitable for zero-copy wrapping as a TensorView
/// or ONNX OrtValue without transposition.
///
/// Typically shared across threads via SharedColumnBatch (std::shared_ptr).
/// @see SharedColumnBatch, make_column_batch, EventBus
class ColumnBatch {
public:
    // -------------------------------------------------------------------------
    // Producer-side metadata (set before publishing)
    // -------------------------------------------------------------------------

    std::string source_id;       ///< Exchange / feed identifier
    std::string symbol;          ///< Instrument symbol
    int64_t     seq_first  = 0;  ///< First WAL sequence number in this batch
    int64_t     seq_last   = 0;  ///< Last WAL sequence number in this batch
    int64_t     created_ns = 0;  ///< Batch creation timestamp (ns since epoch)

    // -------------------------------------------------------------------------
    // Factory
    // -------------------------------------------------------------------------

    /// Create an empty ColumnBatch with the given schema.
    /// @param schema       Column descriptors in order.
    /// @param reserve_rows Pre-allocate space for this many rows.
    static ColumnBatch with_schema(std::vector<ColumnDesc> schema,
                                   size_t reserve_rows = 64) {
        ColumnBatch b;
        b.schema_ = std::move(schema);
        b.columns_.resize(b.schema_.size());
        for (auto& col : b.columns_)
            col.reserve(reserve_rows);
        b.num_rows_ = 0;

        using namespace std::chrono;
        b.created_ns = static_cast<int64_t>(
            duration_cast<nanoseconds>(
                system_clock::now().time_since_epoch()).count());
        return b;
    }

    /// Default constructor (empty batch, no schema).
    ColumnBatch() = default;
    ColumnBatch(ColumnBatch&&) = default;            ///< Move constructor.
    ColumnBatch& operator=(ColumnBatch&&) = default; ///< Move assignment.
    ColumnBatch(const ColumnBatch&) = default;            ///< Copy constructor.
    ColumnBatch& operator=(const ColumnBatch&) = default; ///< Copy assignment.

    // -------------------------------------------------------------------------
    // Build API — called from producer thread
    // -------------------------------------------------------------------------

    /// Append one row of feature values.
    /// values.size() must equal num_columns().
    [[nodiscard]] expected<void> push_row(const double* values, size_t count) {
        if (count != schema_.size())
            return Error{ErrorCode::SCHEMA_MISMATCH,
                         "ColumnBatch::push_row: got " + std::to_string(count) +
                         " values, schema has " + std::to_string(schema_.size())};
        for (size_t i = 0; i < count; ++i)
            columns_[i].push_back(values[i]);
        ++num_rows_;
        return expected<void>{};
    }

    /// Append one row from an initializer list (e.g. `push_row({1.0, 2.0})`).
    /// @param values Feature values (must match num_columns()).
    /// @return Error on schema mismatch.
    [[nodiscard]] expected<void> push_row(std::initializer_list<double> values) {
        std::vector<double> tmp(values);
        return push_row(tmp.data(), tmp.size());
    }

    /// Append one row from a vector.
    /// @param values Feature values (must match num_columns()).
    /// @return Error on schema mismatch.
    [[nodiscard]] expected<void> push_row(const std::vector<double>& values) {
        return push_row(values.data(), values.size());
    }

    // -------------------------------------------------------------------------
    // Query API — called from consumer / ML thread
    // -------------------------------------------------------------------------

    /// Number of rows currently in the batch.
    [[nodiscard]] size_t num_rows()    const noexcept { return num_rows_; }
    /// Number of columns defined by the schema.
    [[nodiscard]] size_t num_columns() const noexcept { return schema_.size(); }
    /// True if the batch contains no rows.
    [[nodiscard]] bool   empty()       const noexcept { return num_rows_ == 0; }

    /// The schema (column descriptors) this batch was created with.
    [[nodiscard]] const std::vector<ColumnDesc>& schema() const noexcept {
        return schema_;
    }

    /// Zero-copy TensorView over a single column's contiguous double array.
    /// Shape: {num_rows_}, dtype: FLOAT64.
    /// The view is valid as long as this ColumnBatch is alive and unmodified.
    [[nodiscard]] TensorView column_view(size_t col_idx) const {
        if (col_idx >= columns_.size() || columns_[col_idx].empty())
            return TensorView{};  // invalid view
        return TensorView{
            columns_[col_idx].data(),
            TensorShape{static_cast<int64_t>(num_rows_)},
            TensorDataType::FLOAT64
        };
    }

    /// Span accessor for a single column — zero-copy, range-checked.
    [[nodiscard]] std::span<const double> column_span(size_t col_idx) const {
        if (col_idx >= columns_.size())
            return {};
        return {columns_[col_idx].data(),
                std::min(num_rows_, columns_[col_idx].size())};
    }

    // -------------------------------------------------------------------------
    // as_tensor — assemble all columns into a 2D [rows × cols] OwnedTensor
    //
    // Uses BatchTensorBuilder to interleave columns into a single contiguous
    // buffer.  output_dtype defaults to FLOAT32 for ONNX compatibility.
    // -------------------------------------------------------------------------

    /// Assemble all columns into a single 2D [rows x cols] OwnedTensor.
    ///
    /// Uses BatchTensorBuilder internally. The default output type is FLOAT32
    /// for direct ONNX Runtime consumption.
    ///
    /// @param output_dtype Desired element type (default FLOAT32).
    /// @return OwnedTensor of shape {num_rows, num_columns}, or Error if empty.
    [[nodiscard]] expected<OwnedTensor> as_tensor(
            TensorDataType output_dtype = TensorDataType::FLOAT32) const {

        if (num_rows_ == 0 || schema_.empty())
            return Error{ErrorCode::INTERNAL_ERROR,
                         "ColumnBatch::as_tensor: batch is empty"};

        BatchTensorBuilder builder;
        for (size_t i = 0; i < schema_.size(); ++i) {
            auto tv = column_view(i);
            if (!tv.is_valid())
                return Error{ErrorCode::INTERNAL_ERROR,
                             "ColumnBatch::as_tensor: column '" +
                             schema_[i].name + "' view is invalid"};
            builder.add_column(schema_[i].name, tv);
        }
        return builder.build(output_dtype);
    }

    // -------------------------------------------------------------------------
    // to_stream_record — serialise batch into a WAL StreamRecord
    //
    // Binary wire format (little-endian):
    //   [uint32 num_columns][uint32 num_rows]
    //   [uint64 column_name_len][column_name_bytes ...] × num_columns
    //   [float64 values × num_rows] × num_columns   (column-major)
    // -------------------------------------------------------------------------

    /// Serialize the batch into a WAL StreamRecord.
    ///
    /// The binary payload uses little-endian column-major format. The default
    /// type_id 0x434F4C42 ("COLB") identifies ColumnBatch records in the WAL.
    ///
    /// @param timestamp_ns Override timestamp (0 = use created_ns).
    /// @param type_id      Record type tag for WAL routing.
    /// @return StreamRecord with the serialized batch payload.
    [[nodiscard]] StreamRecord to_stream_record(
            int64_t  timestamp_ns = 0,
            uint32_t type_id      = 0x434F4C42u /*"COLB"*/) const {

        // CWE-190: Integer Overflow or Wraparound — check row count fits in
        // uint32_t before narrowing cast into the serialization header.
        if (num_rows_ > static_cast<size_t>(UINT32_MAX)) {
            throw std::overflow_error(
                "ColumnBatch::to_stream_record: num_rows exceeds UINT32_MAX ("
                + std::to_string(num_rows_) + ") — batch too large for WAL serialization");
        }
        const auto ncols = static_cast<uint32_t>(schema_.size());
        const auto nrows = static_cast<uint32_t>(num_rows_);

        // Compute total payload size
        size_t payload_bytes = sizeof(uint32_t) * 2;  // ncols + nrows
        for (const auto& desc : schema_) {
            payload_bytes += sizeof(uint32_t) + desc.name.size();
        }
        // CWE-190: overflow check for sizeof(double) * ncols * nrows
        {
            const size_t ncols_sz = static_cast<size_t>(ncols);
            const size_t nrows_sz = static_cast<size_t>(nrows);
            if (ncols_sz > 0 && nrows_sz > SIZE_MAX / ncols_sz) {
                throw std::overflow_error(
                    "ColumnBatch::to_stream_record: ncols*nrows overflows size_t");
            }
            const size_t cells = ncols_sz * nrows_sz;
            if (cells > SIZE_MAX / sizeof(double)) {
                throw std::overflow_error(
                    "ColumnBatch::to_stream_record: payload size overflows size_t");
            }
            payload_bytes += sizeof(double) * cells;
        }

        std::string payload;
        payload.resize(payload_bytes);

        char* p = payload.data();

        auto write_u32 = [&](uint32_t v) {
            std::memcpy(p, &v, sizeof(v)); p += sizeof(v);
        };
        auto write_f64 = [&](double v) {
            std::memcpy(p, &v, sizeof(v)); p += sizeof(v);
        };

        write_u32(ncols);
        write_u32(nrows);

        for (const auto& desc : schema_) {
            write_u32(static_cast<uint32_t>(desc.name.size()));
            std::memcpy(p, desc.name.data(), desc.name.size());
            p += desc.name.size();
        }

        for (size_t ci = 0; ci < schema_.size(); ++ci)
            for (size_t ri = 0; ri < num_rows_; ++ri)
                write_f64(columns_[ci][ri]);

        StreamRecord rec;
        rec.timestamp_ns = (timestamp_ns != 0) ? timestamp_ns : created_ns;
        rec.type_id      = type_id;
        rec.payload      = std::move(payload);
        return rec;
    }

    // -------------------------------------------------------------------------
    // Deserialise a StreamRecord payload back into a ColumnBatch
    // -------------------------------------------------------------------------

    /// Deserialize a StreamRecord payload back into a ColumnBatch.
    ///
    /// Inverse of to_stream_record(). Reads the binary column-major format
    /// and reconstructs the schema, columns, and row data.
    ///
    /// @param rec StreamRecord previously produced by to_stream_record().
    /// @return Reconstructed ColumnBatch, or Error on truncated/corrupt payload.
    [[nodiscard]] static expected<ColumnBatch> from_stream_record(
            const StreamRecord& rec) {

        const char* p   = rec.payload.data();
        const char* end = p + rec.payload.size();

        auto read_u32 = [&](uint32_t& v) -> bool {
            if (p + sizeof(v) > end) return false;
            std::memcpy(&v, p, sizeof(v)); p += sizeof(v);
            return true;
        };
        auto read_f64 = [&](double& v) -> bool {
            if (p + sizeof(v) > end) return false;
            std::memcpy(&v, p, sizeof(v)); p += sizeof(v);
            return true;
        };

        uint32_t ncols = 0, nrows = 0;
        if (!read_u32(ncols) || !read_u32(nrows))
            return Error{ErrorCode::IO_ERROR,
                         "ColumnBatch::from_stream_record: truncated header"};

        // OOM guard: cap total cells to prevent crafted payloads from exhausting memory
        static constexpr size_t MAX_BATCH_CELLS = 100'000'000; // 100M cells (~800 MB)
        if (static_cast<size_t>(ncols) * static_cast<size_t>(nrows) > MAX_BATCH_CELLS)
            return Error{ErrorCode::IO_ERROR,
                         "ColumnBatch::from_stream_record: ncols*nrows exceeds safety limit"};

        std::vector<ColumnDesc> schema;
        schema.reserve(ncols);
        for (uint32_t ci = 0; ci < ncols; ++ci) {
            uint32_t name_len = 0;
            if (!read_u32(name_len))
                return Error{ErrorCode::IO_ERROR,
                             "ColumnBatch::from_stream_record: truncated schema"};
            if (p + name_len > end)
                return Error{ErrorCode::IO_ERROR,
                             "ColumnBatch::from_stream_record: name overflow"};
            ColumnDesc desc;
            desc.name.assign(p, name_len);
            p += name_len;
            schema.push_back(std::move(desc));
        }

        ColumnBatch b = ColumnBatch::with_schema(std::move(schema), nrows);

        for (uint32_t ci = 0; ci < ncols; ++ci) {
            b.columns_[ci].resize(nrows);
            for (uint32_t ri = 0; ri < nrows; ++ri) {
                if (!read_f64(b.columns_[ci][ri]))
                    return Error{ErrorCode::IO_ERROR,
                                 "ColumnBatch::from_stream_record: truncated data"};
            }
        }
        b.num_rows_    = nrows;
        b.created_ns   = rec.timestamp_ns;

        return b;
    }

    // -------------------------------------------------------------------------
    // Utility
    // -------------------------------------------------------------------------

    /// Clear all row data while preserving the schema.
    void clear() {
        for (auto& col : columns_) col.clear();
        num_rows_ = 0;
    }

    /// Pre-allocate storage for the given number of rows in each column.
    /// @param rows Number of rows to reserve capacity for.
    void reserve(size_t rows) {
        for (auto& col : columns_) col.reserve(rows);
    }

private:
    std::vector<ColumnDesc>          schema_;
    std::vector<std::vector<double>> columns_;   ///< columns_[col][row]
    size_t                           num_rows_{0};
};

// ---------------------------------------------------------------------------
// SharedColumnBatch — the unit transferred between threads
// ---------------------------------------------------------------------------

/// Thread-safe shared pointer to a ColumnBatch -- the unit transferred
/// between producer and consumer threads via EventBus.
using SharedColumnBatch = std::shared_ptr<ColumnBatch>;

/// Convenience factory: create a shared batch with a given schema.
inline SharedColumnBatch make_column_batch(std::vector<ColumnDesc> schema,
                                           size_t reserve_rows = 64) {
    return std::make_shared<ColumnBatch>(
        ColumnBatch::with_schema(std::move(schema), reserve_rows));
}

} // namespace signet::forge
