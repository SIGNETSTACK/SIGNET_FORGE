// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file types.hpp
/// @brief Parquet Thrift struct types -- C++ structs matching parquet.thrift,
///        with Compact Protocol serialize/deserialize methods.
///
/// @note parquet-format target: 2.9.0
///
/// Each struct has:
///   void serialize(CompactEncoder& enc) const
///   [[nodiscard]] signet::forge::expected<void> deserialize(CompactDecoder& dec)
///
/// Field IDs match the canonical parquet.thrift specification (parquet-format 2.9.0).
/// Optional fields use std::optional<T> and are only written when present.
///
/// Correctness guarantees (Thrift Correctness Phase):
///   - Field-type (ftype) validated before every read_*() call (CWE-843).
///   - Required-field enforcement via uint32_t bitmask for: KeyValue,
///     DataPageHeader, ColumnMetaData.
///   - All deserialize() return expected<void>; structured failure on any
///     protocol violation.

#include "signet/error.hpp"
#include "signet/types.hpp"
#include "signet/thrift/compact.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace signet::forge::thrift {

// ============================================================================
// § 1  LogicalType Thrift union family  (parquet-format 2.9.0, Sub-phase A)
//
// Implements the 5 financial/AI-relevant LogicalType union members:
//   STRING (field 1), DECIMAL (field 5), TIMESTAMP (field 9),
//   INT (field 11), UUID (field 15).
// ============================================================================

/// Time unit discriminator for TimestampType (parquet.thrift TimeUnit union).
///
/// Wire encoding: union struct with one empty-struct variant set.
/// Field IDs: MILLIS=1, MICROS=2, NANOS=3.
struct TimeUnit {
    enum class Kind : int32_t { MILLIS = 1, MICROS = 2, NANOS = 3 } kind = Kind::MICROS;

    TimeUnit() = default;
    explicit TimeUnit(Kind k) : kind(k) {}

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        // Write the active unit variant as an empty struct at its union field ID.
        enc.write_field(static_cast<int16_t>(kind), compact_type::STRUCT);
        enc.begin_struct();
        enc.write_stop();
        enc.end_struct();
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "TimeUnit: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "TimeUnit: field header error"};
            switch (fid) {
                case 1: // MilliSeconds
                case 2: // MicroSeconds
                case 3: // NanoSeconds
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "TimeUnit: expected STRUCT for unit variant"};
                    }
                    kind = static_cast<Kind>(fid);
                    dec.skip_field(compact_type::STRUCT); // consume empty unit struct
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "TimeUnit: decoder error"};
        return {};
    }
};

/// IntType: integer logical type with explicit width and signedness.
///
/// Field 11 of the LogicalTypeUnion. Wire field IDs: bit_width=1 (I8), is_signed=2 (BOOL).
struct IntType {
    int8_t bit_width = 64;    ///< Bit width: 8, 16, 32, or 64.
    bool   is_signed = true;  ///< True for signed integers; false for unsigned.

    IntType() = default;
    IntType(int8_t bw, bool s) : bit_width(bw), is_signed(s) {}

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I8);
        enc.write_i8(bit_width);
        enc.write_field_bool(2, is_signed);
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "IntType: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "IntType: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I8) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "IntType.bitWidth: expected I8"};
                    }
                    bit_width = dec.read_i8();
                    break;
                case 2:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "IntType.isSigned: expected BOOL"};
                    }
                    is_signed = dec.read_bool();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "IntType: decoder error"};
        return {};
    }
};

/// DecimalType: fixed-point decimal logical type.
///
/// Field 5 of the LogicalTypeUnion. Wire field IDs: scale=1 (I32), precision=2 (I32).
struct DecimalType {
    int32_t scale     = 0;  ///< Number of digits to the right of the decimal point.
    int32_t precision = 0;  ///< Total number of significant decimal digits.

    DecimalType() = default;
    DecimalType(int32_t s, int32_t p) : scale(s), precision(p) {}

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(scale);
        enc.write_field(2, compact_type::I32);
        enc.write_i32(precision);
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DecimalType: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DecimalType: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DecimalType.scale: expected I32"};
                    }
                    scale = dec.read_i32();
                    break;
                case 2:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DecimalType.precision: expected I32"};
                    }
                    precision = dec.read_i32();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DecimalType: decoder error"};
        return {};
    }
};

/// TimestampType: timestamp logical type with UTC adjustment and time unit.
///
/// Field 9 of the LogicalTypeUnion. Wire field IDs: isAdjustedToUTC=1 (BOOL), unit=2 (STRUCT).
struct TimestampType {
    bool     is_adjusted_to_utc = true;  ///< True if the timestamp is UTC-normalized.
    TimeUnit unit;                        ///< Time unit (MILLIS, MICROS, or NANOS).

    TimestampType() = default;
    TimestampType(bool utc, TimeUnit u) : is_adjusted_to_utc(utc), unit(u) {}

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field_bool(1, is_adjusted_to_utc);
        enc.write_field(2, compact_type::STRUCT);
        unit.serialize(enc);
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "TimestampType: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "TimestampType: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "TimestampType.isAdjustedToUTC: expected BOOL"};
                    }
                    is_adjusted_to_utc = dec.read_bool();
                    break;
                case 2:
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "TimestampType.unit: expected STRUCT"};
                    }
                    if (auto r = unit.deserialize(dec); !r.has_value()) return r.error();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "TimestampType: decoder error"};
        return {};
    }
};

/// LogicalTypeUnion: Thrift wire union for parquet.thrift LogicalType (field 10 of SchemaElement).
///
/// Sub-phase A: implements 5 financial/AI-relevant members per parquet-format 2.9.0.
/// Wire union field IDs: STRING=1, DECIMAL=5, TIMESTAMP=9, INT=11, UUID=15.
/// STRING and UUID are empty structs (no payload beyond the kind discriminant).
struct LogicalTypeUnion {
    enum class Kind : int32_t {
        NONE      = 0,  ///< No logical type annotation.
        STRING    = 1,  ///< StringType (field 1 of union).
        DECIMAL   = 5,  ///< DecimalType (field 5 of union).
        TIMESTAMP = 9,  ///< TimestampType (field 9 of union).
        INT       = 11, ///< IntType (field 11 of union).
        UUID      = 15, ///< UUIDType (field 15 of union).
    } kind = Kind::NONE;

    std::optional<DecimalType>   decimal;    ///< Populated when kind == DECIMAL.
    std::optional<TimestampType> timestamp;  ///< Populated when kind == TIMESTAMP.
    std::optional<IntType>       integer;    ///< Populated when kind == INT.
    // STRING and UUID carry no payload; presence is implied by kind.

    LogicalTypeUnion() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        switch (kind) {
            case Kind::STRING:
                // StringType: field 1, empty struct body
                enc.write_field(1, compact_type::STRUCT);
                enc.begin_struct(); enc.write_stop(); enc.end_struct();
                break;
            case Kind::DECIMAL:
                if (decimal.has_value()) {
                    enc.write_field(5, compact_type::STRUCT);
                    decimal->serialize(enc);
                }
                break;
            case Kind::TIMESTAMP:
                if (timestamp.has_value()) {
                    enc.write_field(9, compact_type::STRUCT);
                    timestamp->serialize(enc);
                }
                break;
            case Kind::INT:
                if (integer.has_value()) {
                    enc.write_field(11, compact_type::STRUCT);
                    integer->serialize(enc);
                }
                break;
            case Kind::UUID:
                // UUIDType: field 15, empty struct body
                enc.write_field(15, compact_type::STRUCT);
                enc.begin_struct(); enc.write_stop(); enc.end_struct();
                break;
            case Kind::NONE:
                break;
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion: field header error"};
            switch (fid) {
                case 1: // StringType
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion.STRING: expected STRUCT"};
                    }
                    kind = Kind::STRING;
                    dec.skip_field(compact_type::STRUCT); // consume empty StringType body
                    break;
                case 5: // DecimalType
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion.DECIMAL: expected STRUCT"};
                    }
                    kind = Kind::DECIMAL;
                    decimal.emplace();
                    if (auto r = decimal->deserialize(dec); !r.has_value()) return r.error();
                    break;
                case 9: // TimestampType
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion.TIMESTAMP: expected STRUCT"};
                    }
                    kind = Kind::TIMESTAMP;
                    timestamp.emplace();
                    if (auto r = timestamp->deserialize(dec); !r.has_value()) return r.error();
                    break;
                case 11: // IntType
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion.INT: expected STRUCT"};
                    }
                    kind = Kind::INT;
                    integer.emplace();
                    if (auto r = integer->deserialize(dec); !r.has_value()) return r.error();
                    break;
                case 15: // UUIDType
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion.UUID: expected STRUCT"};
                    }
                    kind = Kind::UUID;
                    dec.skip_field(compact_type::STRUCT); // consume empty UUIDType body
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "LogicalTypeUnion: decoder error"};
        return {};
    }
};

// ============================================================================
// § 2  Core Parquet Thrift structs  (existing, updated for parquet-format 2.9.0)
// ============================================================================

/// Parquet column statistics (parquet.thrift fields 1-6).
///
/// Fields 1-2 are the legacy min/max (sort-order dependent, deprecated).
/// Fields 5-6 are the new-style min_value/max_value (preferred).
struct Statistics {
    std::optional<std::string> max;            ///< Old-style max (field 1, deprecated).
    std::optional<std::string> min;            ///< Old-style min (field 2, deprecated).
    std::optional<int64_t>     null_count;     ///< Number of null values (field 3).
    std::optional<int64_t>     distinct_count; ///< Approximate distinct count (field 4).
    std::optional<std::string> max_value;      ///< New-style max value (field 5, preferred).
    std::optional<std::string> min_value;      ///< New-style min value (field 6, preferred).

    Statistics() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (max.has_value()) {
            enc.write_field(1, compact_type::BINARY);
            enc.write_string(*max);
        }
        if (min.has_value()) {
            enc.write_field(2, compact_type::BINARY);
            enc.write_string(*min);
        }
        if (null_count.has_value()) {
            enc.write_field(3, compact_type::I64);
            enc.write_i64(*null_count);
        }
        if (distinct_count.has_value()) {
            enc.write_field(4, compact_type::I64);
            enc.write_i64(*distinct_count);
        }
        if (max_value.has_value()) {
            enc.write_field(5, compact_type::BINARY);
            enc.write_string(*max_value);
        }
        if (min_value.has_value()) {
            enc.write_field(6, compact_type::BINARY);
            enc.write_string(*min_value);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics.max: expected BINARY"};
                    }
                    max = dec.read_string();
                    break;
                case 2:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics.min: expected BINARY"};
                    }
                    min = dec.read_string();
                    break;
                case 3:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics.null_count: expected I64"};
                    }
                    null_count = dec.read_i64();
                    break;
                case 4:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics.distinct_count: expected I64"};
                    }
                    distinct_count = dec.read_i64();
                    break;
                case 5:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics.max_value: expected BINARY"};
                    }
                    max_value = dec.read_string();
                    break;
                case 6:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics.min_value: expected BINARY"};
                    }
                    min_value = dec.read_string();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "Statistics: decoder error"};
        return {};
    }
};

/// Parquet KeyValue metadata entry (parquet.thrift field IDs 1-2).
///
/// Used for file-level and column-level key-value metadata pairs.
/// Required-field enforcement: field 1 (key) must be present.
struct KeyValue {
    std::string                key;    ///< Metadata key (field 1, required).
    std::optional<std::string> value;  ///< Metadata value (field 2, optional).

    KeyValue() = default;
    KeyValue(std::string k, std::string v)
        : key(std::move(k)), value(std::move(v)) {}

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::BINARY);
        enc.write_string(key);
        if (value.has_value()) {
            enc.write_field(2, compact_type::BINARY);
            enc.write_string(*value);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "KeyValue: begin_struct failed"};
        uint32_t seen = 0;  // Required-field bitmask: bit 0 = field 1 (key)
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "KeyValue: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "KeyValue.key: expected BINARY"};
                    }
                    key = dec.read_string();
                    seen |= (1u << 0);
                    break;
                case 2:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "KeyValue.value: expected BINARY"};
                    }
                    value = dec.read_string();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "KeyValue: decoder error"};
        // Required-field validation
        if ((seen & 0x01u) == 0u) {
            return {ErrorCode::THRIFT_DECODE_ERROR, "KeyValue: missing required field 1 (key)"};
        }
        return {};
    }
};

/// Parquet schema element (parquet.thrift fields 1-10).
///
/// Represents a single node in the Parquet schema tree. Leaf nodes have
/// a physical type; group nodes have num_children instead.
/// Fields 9 (field_id) and 10 (logicalType) added per parquet-format 2.9.0.
struct SchemaElement {
    std::optional<PhysicalType>    type;            ///< Physical type (field 1, absent for group nodes).
    std::optional<int32_t>         type_length;     ///< Type length for FIXED_LEN_BYTE_ARRAY (field 2).
    std::optional<Repetition>      repetition_type; ///< REQUIRED/OPTIONAL/REPEATED (field 3).
    std::string                    name;            ///< Column or group name (field 4, required).
    std::optional<int32_t>         num_children;    ///< Number of children for group nodes (field 5).
    std::optional<ConvertedType>   converted_type;  ///< Legacy converted type (field 6).
    std::optional<int32_t>         scale;           ///< Decimal scale (field 7).
    std::optional<int32_t>         precision;       ///< Decimal precision (field 8).
    std::optional<int32_t>         field_id;        ///< Field ID for nested type evolution (field 9).
    std::optional<LogicalTypeUnion> logical_type;   ///< LogicalType union (field 10, preferred).

    SchemaElement() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (type.has_value()) {
            enc.write_field(1, compact_type::I32);
            enc.write_i32(static_cast<int32_t>(*type));
        }
        if (type_length.has_value()) {
            enc.write_field(2, compact_type::I32);
            enc.write_i32(*type_length);
        }
        if (repetition_type.has_value()) {
            enc.write_field(3, compact_type::I32);
            enc.write_i32(static_cast<int32_t>(*repetition_type));
        }
        // field 4: name — always written (required)
        enc.write_field(4, compact_type::BINARY);
        enc.write_string(name);
        if (num_children.has_value()) {
            enc.write_field(5, compact_type::I32);
            enc.write_i32(*num_children);
        }
        if (converted_type.has_value()) {
            enc.write_field(6, compact_type::I32);
            enc.write_i32(static_cast<int32_t>(*converted_type));
        }
        if (scale.has_value()) {
            enc.write_field(7, compact_type::I32);
            enc.write_i32(*scale);
        }
        if (precision.has_value()) {
            enc.write_field(8, compact_type::I32);
            enc.write_i32(*precision);
        }
        if (field_id.has_value()) {
            enc.write_field(9, compact_type::I32);
            enc.write_i32(*field_id);
        }
        if (logical_type.has_value()) {
            enc.write_field(10, compact_type::STRUCT);
            logical_type->serialize(enc);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.type: expected I32"};
                    }
                    type = static_cast<PhysicalType>(dec.read_i32());
                    break;
                case 2:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.type_length: expected I32"};
                    }
                    type_length = dec.read_i32();
                    break;
                case 3:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.repetition_type: expected I32"};
                    }
                    repetition_type = static_cast<Repetition>(dec.read_i32());
                    break;
                case 4:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.name: expected BINARY"};
                    }
                    name = dec.read_string();
                    break;
                case 5:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.num_children: expected I32"};
                    }
                    num_children = dec.read_i32();
                    break;
                case 6:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.converted_type: expected I32"};
                    }
                    converted_type = static_cast<ConvertedType>(dec.read_i32());
                    break;
                case 7:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.scale: expected I32"};
                    }
                    scale = dec.read_i32();
                    break;
                case 8:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.precision: expected I32"};
                    }
                    precision = dec.read_i32();
                    break;
                case 9:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.field_id: expected I32"};
                    }
                    field_id = dec.read_i32();
                    break;
                case 10: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement.logicalType: expected STRUCT"};
                    }
                    logical_type.emplace();
                    if (auto r = logical_type->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "SchemaElement: decoder error"};
        return {};
    }
};

/// Parquet data page header V1 (parquet.thrift fields 1-5).
///
/// Fields 1-4 are required. Field 5 (statistics) is optional, added per parquet-format 2.9.0.
/// Required-field enforcement: fields 1-4 must all be present.
struct DataPageHeader {
    int32_t  num_values                = 0;               ///< Number of values (field 1, required).
    Encoding encoding                  = Encoding::PLAIN;  ///< Data encoding (field 2, required).
    Encoding definition_level_encoding = Encoding::RLE;    ///< Def level encoding (field 3, required).
    Encoding repetition_level_encoding = Encoding::RLE;    ///< Rep level encoding (field 4, required).
    std::optional<Statistics> statistics;                  ///< Page statistics (field 5, optional).

    DataPageHeader() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(num_values);
        enc.write_field(2, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(encoding));
        enc.write_field(3, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(definition_level_encoding));
        enc.write_field(4, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(repetition_level_encoding));
        if (statistics.has_value()) {
            enc.write_field(5, compact_type::STRUCT);
            statistics->serialize(enc);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeader: begin_struct failed"};
        uint32_t seen = 0;  // Required-field bitmask: bits 0-3 = fields 1-4
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeader: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeader.num_values: expected I32"};
                    }
                    num_values = dec.read_i32();
                    seen |= (1u << 0);
                    break;
                case 2:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeader.encoding: expected I32"};
                    }
                    encoding = static_cast<Encoding>(dec.read_i32());
                    seen |= (1u << 1);
                    break;
                case 3:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "DataPageHeader.definition_level_encoding: expected I32"};
                    }
                    definition_level_encoding = static_cast<Encoding>(dec.read_i32());
                    seen |= (1u << 2);
                    break;
                case 4:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "DataPageHeader.repetition_level_encoding: expected I32"};
                    }
                    repetition_level_encoding = static_cast<Encoding>(dec.read_i32());
                    seen |= (1u << 3);
                    break;
                case 5: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeader.statistics: expected STRUCT"};
                    }
                    statistics.emplace();
                    if (auto r = statistics->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeader: decoder error"};
        // Required-field validation (fields 1-4, mask = 0x0F)
        if ((seen & 0x0Fu) != 0x0Fu) {
            return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeader: missing one or more required fields (1-4)"};
        }
        return {};
    }
};

/// Parquet dictionary page header (parquet.thrift fields 1-3).
///
/// Fields 1-2 are required. Field 3 (is_sorted) added per parquet-format 2.9.0.
struct DictionaryPageHeader {
    int32_t  num_values = 0;                           ///< Number of dictionary entries (field 1).
    Encoding encoding   = Encoding::PLAIN_DICTIONARY;  ///< Dictionary encoding (field 2).
    std::optional<bool> is_sorted;                     ///< Whether entries are sorted (field 3).

    DictionaryPageHeader() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(num_values);
        enc.write_field(2, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(encoding));
        if (is_sorted.has_value()) {
            enc.write_field_bool(3, *is_sorted);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DictionaryPageHeader: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DictionaryPageHeader: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DictionaryPageHeader.num_values: expected I32"};
                    }
                    num_values = dec.read_i32();
                    break;
                case 2:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DictionaryPageHeader.encoding: expected I32"};
                    }
                    encoding = static_cast<Encoding>(dec.read_i32());
                    break;
                case 3:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "DictionaryPageHeader.is_sorted: expected BOOL"};
                    }
                    is_sorted = dec.read_bool();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DictionaryPageHeader: decoder error"};
        return {};
    }
};

/// Parquet data page header V2 (parquet.thrift fields 1-7).
///
/// V2 data pages separate level data from value data and can optionally
/// leave value data uncompressed (levels are always uncompressed).
struct DataPageHeaderV2 {
    int32_t  num_values                    = 0;             ///< Total values including nulls (field 1).
    int32_t  num_nulls                     = 0;             ///< Number of null values (field 2).
    int32_t  num_rows                      = 0;             ///< Number of rows in this page (field 3).
    Encoding encoding                      = Encoding::PLAIN; ///< Data encoding (field 4).
    int32_t  definition_levels_byte_length = 0;             ///< Byte length of def levels (field 5).
    int32_t  repetition_levels_byte_length = 0;             ///< Byte length of rep levels (field 6).
    std::optional<bool> is_compressed;                      ///< Whether values are compressed (field 7, default true).

    DataPageHeaderV2() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(num_values);
        enc.write_field(2, compact_type::I32);
        enc.write_i32(num_nulls);
        enc.write_field(3, compact_type::I32);
        enc.write_i32(num_rows);
        enc.write_field(4, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(encoding));
        enc.write_field(5, compact_type::I32);
        enc.write_i32(definition_levels_byte_length);
        enc.write_field(6, compact_type::I32);
        enc.write_i32(repetition_levels_byte_length);
        if (is_compressed.has_value()) {
            enc.write_field_bool(7, *is_compressed);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2.num_values: expected I32"};
                    }
                    num_values = dec.read_i32();
                    break;
                case 2:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2.num_nulls: expected I32"};
                    }
                    num_nulls = dec.read_i32();
                    break;
                case 3:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2.num_rows: expected I32"};
                    }
                    num_rows = dec.read_i32();
                    break;
                case 4:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2.encoding: expected I32"};
                    }
                    encoding = static_cast<Encoding>(dec.read_i32());
                    break;
                case 5:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "DataPageHeaderV2.definition_levels_byte_length: expected I32"};
                    }
                    definition_levels_byte_length = dec.read_i32();
                    break;
                case 6:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "DataPageHeaderV2.repetition_levels_byte_length: expected I32"};
                    }
                    repetition_levels_byte_length = dec.read_i32();
                    break;
                case 7:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2.is_compressed: expected BOOL"};
                    }
                    is_compressed = dec.read_bool();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "DataPageHeaderV2: decoder error"};
        return {};
    }

    /// Effective is_compressed value (defaults to true per Parquet spec if absent).
    [[nodiscard]] bool effective_is_compressed() const {
        return is_compressed.value_or(true);
    }
};

/// Parquet page header (parquet.thrift fields 1-8).
///
/// The top-level header for every Parquet page. Contains the page type,
/// sizes, optional CRC, and a union of type-specific sub-headers.
/// Field 6 (IndexPageHeader) is not used by this implementation and is skipped.
struct PageHeader {
    PageType type                                          = PageType::DATA_PAGE;
    int32_t  uncompressed_page_size                        = 0;
    int32_t  compressed_page_size                          = 0;
    std::optional<int32_t>              crc;
    std::optional<DataPageHeader>       data_page_header;
    // field 6: index_page_header — skipped (not used in Signet)
    std::optional<DictionaryPageHeader> dictionary_page_header;
    std::optional<DataPageHeaderV2>     data_page_header_v2;

    PageHeader() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(type));
        enc.write_field(2, compact_type::I32);
        enc.write_i32(uncompressed_page_size);
        enc.write_field(3, compact_type::I32);
        enc.write_i32(compressed_page_size);
        if (crc.has_value()) {
            enc.write_field(4, compact_type::I32);
            enc.write_i32(*crc);
        }
        if (data_page_header.has_value()) {
            enc.write_field(5, compact_type::STRUCT);
            data_page_header->serialize(enc);
        }
        // field 6: index_page_header — skipped
        if (dictionary_page_header.has_value()) {
            enc.write_field(7, compact_type::STRUCT);
            dictionary_page_header->serialize(enc);
        }
        if (data_page_header_v2.has_value()) {
            enc.write_field(8, compact_type::STRUCT);
            data_page_header_v2->serialize(enc);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "PageHeader: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "PageHeader: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "PageHeader.type: expected I32"};
                    }
                    type = static_cast<PageType>(dec.read_i32());
                    break;
                case 2:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "PageHeader.uncompressed_page_size: expected I32"};
                    }
                    uncompressed_page_size = dec.read_i32();
                    break;
                case 3:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "PageHeader.compressed_page_size: expected I32"};
                    }
                    compressed_page_size = dec.read_i32();
                    break;
                case 4:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "PageHeader.crc: expected I32"};
                    }
                    crc = dec.read_i32();
                    break;
                case 5: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "PageHeader.data_page_header: expected STRUCT"};
                    }
                    data_page_header.emplace();
                    if (auto r = data_page_header->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 6:
                    dec.skip_field(ftype); // index_page_header — skipped
                    break;
                case 7: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "PageHeader.dictionary_page_header: expected STRUCT"};
                    }
                    dictionary_page_header.emplace();
                    if (auto r = dictionary_page_header->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 8: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "PageHeader.data_page_header_v2: expected STRUCT"};
                    }
                    data_page_header_v2.emplace();
                    if (auto r = data_page_header_v2->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "PageHeader: decoder error"};
        return {};
    }
};

// ============================================================================
// § 3  Column metadata  (parquet.thrift ColumnMetaData, fields 1-12)
// ============================================================================

/// Parquet column metadata (parquet.thrift fields 1-12).
///
/// Contains the physical type, encodings, compression, byte sizes, offsets,
/// and optional statistics for a single column chunk within a row group.
/// Required-field enforcement: fields 1,2,3,4,5,6,7,9 must all be present.
struct ColumnMetaData {
    PhysicalType                       type                    = PhysicalType::BYTE_ARRAY;
    std::vector<Encoding>              encodings;
    std::vector<std::string>           path_in_schema;
    Compression                        codec                   = Compression::UNCOMPRESSED;
    int64_t                            num_values              = 0;
    int64_t                            total_uncompressed_size = 0;
    int64_t                            total_compressed_size   = 0;
    std::optional<std::vector<KeyValue>> key_value_metadata;
    int64_t                            data_page_offset        = 0;
    std::optional<int64_t>             index_page_offset;
    std::optional<int64_t>             dictionary_page_offset;
    std::optional<Statistics>          statistics;

    ColumnMetaData() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(type));

        enc.write_field(2, compact_type::LIST);
        enc.write_list_header(compact_type::I32, static_cast<int32_t>(encodings.size()));
        for (auto e : encodings) {
            enc.write_i32(static_cast<int32_t>(e));
        }

        enc.write_field(3, compact_type::LIST);
        enc.write_list_header(compact_type::BINARY, static_cast<int32_t>(path_in_schema.size()));
        for (const auto& p : path_in_schema) {
            enc.write_string(p);
        }

        enc.write_field(4, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(codec));
        enc.write_field(5, compact_type::I64);
        enc.write_i64(num_values);
        enc.write_field(6, compact_type::I64);
        enc.write_i64(total_uncompressed_size);
        enc.write_field(7, compact_type::I64);
        enc.write_i64(total_compressed_size);

        if (key_value_metadata.has_value()) {
            enc.write_field(8, compact_type::LIST);
            enc.write_list_header(compact_type::STRUCT,
                                  static_cast<int32_t>(key_value_metadata->size()));
            for (const auto& kv : *key_value_metadata) {
                kv.serialize(enc);
            }
        }

        enc.write_field(9, compact_type::I64);
        enc.write_i64(data_page_offset);

        if (index_page_offset.has_value()) {
            enc.write_field(10, compact_type::I64);
            enc.write_i64(*index_page_offset);
        }
        if (dictionary_page_offset.has_value()) {
            enc.write_field(11, compact_type::I64);
            enc.write_i64(*dictionary_page_offset);
        }
        if (statistics.has_value()) {
            enc.write_field(12, compact_type::STRUCT);
            statistics->serialize(enc);
        }

        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnMetaData: begin_struct failed"};
        // Required-field bitmask:
        //   bit 0 = field 1 (type), bit 1 = field 2 (encodings),
        //   bit 2 = field 3 (path_in_schema), bit 3 = field 4 (codec),
        //   bit 4 = field 5 (num_values), bit 5 = field 6 (total_uncompressed_size),
        //   bit 6 = field 7 (total_compressed_size), bit 7 = field 9 (data_page_offset)
        uint32_t seen = 0;
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnMetaData: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnMetaData.type: expected I32"};
                    }
                    type = static_cast<PhysicalType>(dec.read_i32());
                    seen |= (1u << 0);
                    break;
                case 2: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnMetaData.encodings: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_ENCODINGS = 10000;
                    if (count < 0 || count > MAX_ENCODINGS) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.encodings: list exceeds maximum size"};
                    }
                    encodings.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        encodings[static_cast<size_t>(i)] =
                            static_cast<Encoding>(dec.read_i32());
                    }
                    seen |= (1u << 1);
                    break;
                }
                case 3: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.path_in_schema: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_PATH_ELEMS = 10000;
                    if (count < 0 || count > MAX_PATH_ELEMS) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.path_in_schema: list exceeds maximum size"};
                    }
                    path_in_schema.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        path_in_schema[static_cast<size_t>(i)] = dec.read_string();
                    }
                    seen |= (1u << 2);
                    break;
                }
                case 4:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnMetaData.codec: expected I32"};
                    }
                    codec = static_cast<Compression>(dec.read_i32());
                    seen |= (1u << 3);
                    break;
                case 5:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnMetaData.num_values: expected I64"};
                    }
                    num_values = dec.read_i64();
                    seen |= (1u << 4);
                    break;
                case 6:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.total_uncompressed_size: expected I64"};
                    }
                    total_uncompressed_size = dec.read_i64();
                    seen |= (1u << 5);
                    break;
                case 7:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.total_compressed_size: expected I64"};
                    }
                    total_compressed_size = dec.read_i64();
                    seen |= (1u << 6);
                    break;
                case 8: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.key_value_metadata: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_STRUCT_LIST_SIZE = 10000;
                    if (count < 0 || count > MAX_STRUCT_LIST_SIZE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.key_value_metadata: list exceeds maximum size"};
                    }
                    key_value_metadata.emplace();
                    key_value_metadata->resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        if (auto r = (*key_value_metadata)[static_cast<size_t>(i)].deserialize(dec);
                            !r.has_value()) {
                            return r.error();
                        }
                    }
                    break;
                }
                case 9:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.data_page_offset: expected I64"};
                    }
                    data_page_offset = dec.read_i64();
                    seen |= (1u << 7);
                    break;
                case 10:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.index_page_offset: expected I64"};
                    }
                    index_page_offset = dec.read_i64();
                    break;
                case 11:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.dictionary_page_offset: expected I64"};
                    }
                    dictionary_page_offset = dec.read_i64();
                    break;
                case 12: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnMetaData.statistics: expected STRUCT"};
                    }
                    statistics.emplace();
                    if (auto r = statistics->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnMetaData: decoder error"};
        // Required-field validation: bits 0-7 (fields 1,2,3,4,5,6,7,9)
        if ((seen & 0xFFu) != 0xFFu) {
            return {ErrorCode::THRIFT_DECODE_ERROR,
                    "ColumnMetaData: missing one or more required fields (1-7, 9)"};
        }
        return {};
    }
};

// ============================================================================
// § 4  Encryption Thrift types  (parquet-format 2.9.0, Option A: AES-GCM-V1)
//
// Canonical typed structs for Parquet Modular Encryption (PME) metadata.
// Signet extensions are isolated in key_value_metadata with "signet." prefix.
// ============================================================================

/// AES-GCM-V1 encryption algorithm parameters (parquet.thrift AesGcmV1).
struct AesGcmV1 {
    std::optional<std::vector<uint8_t>> aad_prefix;        ///< AAD prefix bytes (field 1).
    std::optional<bool>                 aad_file_unique;   ///< Unique AAD per file (field 2).
    std::optional<bool>                 supply_aad_prefix; ///< Caller supplies AAD prefix (field 3).

    AesGcmV1() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (aad_prefix.has_value()) {
            enc.write_field(1, compact_type::BINARY);
            enc.write_binary(aad_prefix->data(), aad_prefix->size());
        }
        if (aad_file_unique.has_value()) {
            enc.write_field_bool(2, *aad_file_unique);
        }
        if (supply_aad_prefix.has_value()) {
            enc.write_field_bool(3, *supply_aad_prefix);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmV1: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmV1: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmV1.aad_prefix: expected BINARY"};
                    }
                    aad_prefix = dec.read_binary();
                    break;
                case 2:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmV1.aad_file_unique: expected BOOL"};
                    }
                    aad_file_unique = dec.read_bool();
                    break;
                case 3:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmV1.supply_aad_prefix: expected BOOL"};
                    }
                    supply_aad_prefix = dec.read_bool();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmV1: decoder error"};
        return {};
    }
};

/// AES-GCM-CTR-V1 encryption algorithm parameters (parquet.thrift AesGcmCtrV1).
struct AesGcmCtrV1 {
    std::optional<std::vector<uint8_t>> aad_prefix;        ///< AAD prefix bytes (field 1).
    std::optional<bool>                 aad_file_unique;   ///< Unique AAD per file (field 2).
    std::optional<bool>                 supply_aad_prefix; ///< Caller supplies AAD prefix (field 3).

    AesGcmCtrV1() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (aad_prefix.has_value()) {
            enc.write_field(1, compact_type::BINARY);
            enc.write_binary(aad_prefix->data(), aad_prefix->size());
        }
        if (aad_file_unique.has_value()) {
            enc.write_field_bool(2, *aad_file_unique);
        }
        if (supply_aad_prefix.has_value()) {
            enc.write_field_bool(3, *supply_aad_prefix);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmCtrV1: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmCtrV1: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmCtrV1.aad_prefix: expected BINARY"};
                    }
                    aad_prefix = dec.read_binary();
                    break;
                case 2:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmCtrV1.aad_file_unique: expected BOOL"};
                    }
                    aad_file_unique = dec.read_bool();
                    break;
                case 3:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmCtrV1.supply_aad_prefix: expected BOOL"};
                    }
                    supply_aad_prefix = dec.read_bool();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "AesGcmCtrV1: decoder error"};
        return {};
    }
};

/// EncryptionAlgorithm union: AES-GCM-V1 (field 1) or AES-GCM-CTR-V1 (field 2).
struct EncryptionAlgorithm {
    enum class Kind : int32_t { NONE = 0, AES_GCM_V1 = 1, AES_GCM_CTR_V1 = 2 } kind = Kind::NONE;
    std::optional<AesGcmV1>    aes_gcm_v1;      ///< Populated when kind == AES_GCM_V1.
    std::optional<AesGcmCtrV1> aes_gcm_ctr_v1;  ///< Populated when kind == AES_GCM_CTR_V1.

    EncryptionAlgorithm() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        switch (kind) {
            case Kind::AES_GCM_V1:
                enc.write_field(1, compact_type::STRUCT);
                aes_gcm_v1.value_or(AesGcmV1{}).serialize(enc);
                break;
            case Kind::AES_GCM_CTR_V1:
                enc.write_field(2, compact_type::STRUCT);
                aes_gcm_ctr_v1.value_or(AesGcmCtrV1{}).serialize(enc);
                break;
            case Kind::NONE:
                break;
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "EncryptionAlgorithm: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "EncryptionAlgorithm: field header error"};
            switch (fid) {
                case 1: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "EncryptionAlgorithm.AES_GCM_V1: expected STRUCT"};
                    }
                    kind = Kind::AES_GCM_V1;
                    aes_gcm_v1.emplace();
                    if (auto r = aes_gcm_v1->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 2: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "EncryptionAlgorithm.AES_GCM_CTR_V1: expected STRUCT"};
                    }
                    kind = Kind::AES_GCM_CTR_V1;
                    aes_gcm_ctr_v1.emplace();
                    if (auto r = aes_gcm_ctr_v1->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "EncryptionAlgorithm: decoder error"};
        return {};
    }
};

/// EncryptionWithColumnKey: per-column encryption key binding (parquet.thrift).
struct EncryptionWithColumnKey {
    std::vector<std::string>            path_in_schema; ///< Schema path of the encrypted column (field 1).
    std::optional<std::vector<uint8_t>> key_metadata;   ///< Serialized key metadata (field 2).

    EncryptionWithColumnKey() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::LIST);
        enc.write_list_header(compact_type::BINARY, static_cast<int32_t>(path_in_schema.size()));
        for (const auto& p : path_in_schema) {
            enc.write_string(p);
        }
        if (key_metadata.has_value()) {
            enc.write_field(2, compact_type::BINARY);
            enc.write_binary(key_metadata->data(), key_metadata->size());
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "EncryptionWithColumnKey: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "EncryptionWithColumnKey: field header error"};
            switch (fid) {
                case 1: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "EncryptionWithColumnKey.path_in_schema: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_PATH = 10000;
                    if (count < 0 || count > MAX_PATH) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "EncryptionWithColumnKey.path_in_schema: list exceeds maximum size"};
                    }
                    path_in_schema.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        path_in_schema[static_cast<size_t>(i)] = dec.read_string();
                    }
                    break;
                }
                case 2:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "EncryptionWithColumnKey.key_metadata: expected BINARY"};
                    }
                    key_metadata = dec.read_binary();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "EncryptionWithColumnKey: decoder error"};
        return {};
    }
};

/// ColumnCryptoMetaData union: footer-key (field 1) or column-key (field 2) encryption.
///
/// ColumnChunk field 4 per parquet-format 2.9.0.
struct ColumnCryptoMetaData {
    enum class Kind : int32_t {
        NONE              = 0,
        FOOTER_KEY        = 1,  ///< EncryptionWithFooterKey (empty struct).
        COLUMN_KEY        = 2,  ///< EncryptionWithColumnKey.
    } kind = Kind::NONE;

    std::optional<EncryptionWithColumnKey> column_key;  ///< Populated when kind == COLUMN_KEY.

    ColumnCryptoMetaData() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        switch (kind) {
            case Kind::FOOTER_KEY:
                enc.write_field(1, compact_type::STRUCT);
                // EncryptionWithFooterKey: empty struct
                enc.begin_struct(); enc.write_stop(); enc.end_struct();
                break;
            case Kind::COLUMN_KEY:
                enc.write_field(2, compact_type::STRUCT);
                column_key.value_or(EncryptionWithColumnKey{}).serialize(enc);
                break;
            case Kind::NONE:
                break;
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnCryptoMetaData: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnCryptoMetaData: field header error"};
            switch (fid) {
                case 1: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnCryptoMetaData.FOOTER_KEY: expected STRUCT"};
                    }
                    kind = Kind::FOOTER_KEY;
                    dec.skip_field(compact_type::STRUCT); // consume empty EncryptionWithFooterKey
                    break;
                }
                case 2: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnCryptoMetaData.COLUMN_KEY: expected STRUCT"};
                    }
                    kind = Kind::COLUMN_KEY;
                    column_key.emplace();
                    if (auto r = column_key->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnCryptoMetaData: decoder error"};
        return {};
    }
};

/// FileCryptoMetaData: file-level encryption metadata (parquet.thrift).
///
/// Written as a separate Thrift blob at the end of encrypted Parquet files,
/// after the regular footer. Contains the file-level encryption algorithm
/// and optional key wrapping metadata.
struct FileCryptoMetaData {
    EncryptionAlgorithm                 encryption_algorithm; ///< Encryption algorithm (field 1, required).
    std::optional<std::vector<uint8_t>> key_metadata;         ///< Key wrapping metadata (field 2).

    FileCryptoMetaData() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::STRUCT);
        encryption_algorithm.serialize(enc);
        if (key_metadata.has_value()) {
            enc.write_field(2, compact_type::BINARY);
            enc.write_binary(key_metadata->data(), key_metadata->size());
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "FileCryptoMetaData: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "FileCryptoMetaData: field header error"};
            switch (fid) {
                case 1: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileCryptoMetaData.encryption_algorithm: expected STRUCT"};
                    }
                    if (auto r = encryption_algorithm.deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 2:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileCryptoMetaData.key_metadata: expected BINARY"};
                    }
                    key_metadata = dec.read_binary();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "FileCryptoMetaData: decoder error"};
        return {};
    }
};

// ============================================================================
// § 5  Bloom filter Thrift types  (parquet-format 2.9.0)
// ============================================================================

/// BloomFilterAlgorithm union: BLOCK (SplitBlock) is the only defined algorithm.
struct BloomFilterAlgorithm {
    enum class Kind : int32_t { NONE = 0, BLOCK = 1 } kind = Kind::BLOCK;

    BloomFilterAlgorithm() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (kind == Kind::BLOCK) {
            enc.write_field(1, compact_type::STRUCT);
            // SplitBlockAlgorithm: empty struct
            enc.begin_struct(); enc.write_stop(); enc.end_struct();
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterAlgorithm: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterAlgorithm: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "BloomFilterAlgorithm.BLOCK: expected STRUCT"};
                    }
                    kind = Kind::BLOCK;
                    dec.skip_field(compact_type::STRUCT);
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterAlgorithm: decoder error"};
        return {};
    }
};

/// BloomFilterHash union: XXHASH (field 1) is the only defined hash function.
struct BloomFilterHash {
    enum class Kind : int32_t { NONE = 0, XXHASH = 1 } kind = Kind::XXHASH;

    BloomFilterHash() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (kind == Kind::XXHASH) {
            enc.write_field(1, compact_type::STRUCT);
            // XxHash: empty struct
            enc.begin_struct(); enc.write_stop(); enc.end_struct();
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHash: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHash: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHash.XXHASH: expected STRUCT"};
                    }
                    kind = Kind::XXHASH;
                    dec.skip_field(compact_type::STRUCT);
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHash: decoder error"};
        return {};
    }
};

/// BloomFilterCompression union: UNCOMPRESSED (field 1) is the only defined mode.
struct BloomFilterCompression {
    enum class Kind : int32_t { NONE = 0, UNCOMPRESSED = 1 } kind = Kind::UNCOMPRESSED;

    BloomFilterCompression() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (kind == Kind::UNCOMPRESSED) {
            enc.write_field(1, compact_type::STRUCT);
            // BloomFilterUncompressed: empty struct
            enc.begin_struct(); enc.write_stop(); enc.end_struct();
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterCompression: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterCompression: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "BloomFilterCompression.UNCOMPRESSED: expected STRUCT"};
                    }
                    kind = Kind::UNCOMPRESSED;
                    dec.skip_field(compact_type::STRUCT);
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterCompression: decoder error"};
        return {};
    }
};

/// BloomFilterHeader: describes the bloom filter block at a column's bloom_filter_offset.
///
/// Fields: num_bytes=1 (I32), algorithm=2 (STRUCT), hash=3 (STRUCT), compression=4 (STRUCT).
struct BloomFilterHeader {
    int32_t              num_bytes    = 0;   ///< Size of the bloom filter in bytes (field 1).
    BloomFilterAlgorithm algorithm;          ///< Hash algorithm (field 2).
    BloomFilterHash      hash;               ///< Hash function (field 3).
    BloomFilterCompression compression;      ///< Compression mode (field 4).

    BloomFilterHeader() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(num_bytes);
        enc.write_field(2, compact_type::STRUCT);
        algorithm.serialize(enc);
        enc.write_field(3, compact_type::STRUCT);
        hash.serialize(enc);
        enc.write_field(4, compact_type::STRUCT);
        compression.serialize(enc);
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHeader: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHeader: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHeader.num_bytes: expected I32"};
                    }
                    num_bytes = dec.read_i32();
                    break;
                case 2: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHeader.algorithm: expected STRUCT"};
                    }
                    if (auto r = algorithm.deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 3: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHeader.hash: expected STRUCT"};
                    }
                    if (auto r = hash.deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 4: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHeader.compression: expected STRUCT"};
                    }
                    if (auto r = compression.deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "BloomFilterHeader: decoder error"};
        return {};
    }
};

// ============================================================================
// § 6  Column chunk  (parquet.thrift ColumnChunk, fields 1-13)
//       Field 4 (ColumnCryptoMetaData) added per parquet-format 2.9.0.
// ============================================================================

/// Parquet column chunk descriptor (parquet.thrift fields 1-13).
///
/// Locates a single column chunk within the file and optionally carries
/// inline column metadata, PME crypto metadata, bloom filter, and index locations.
struct ColumnChunk {
    std::optional<std::string>         file_path;           ///< External file path (field 1).
    int64_t                            file_offset = 0;     ///< Byte offset in file (field 2).
    std::optional<ColumnMetaData>      meta_data;           ///< Inline column metadata (field 3).
    std::optional<ColumnCryptoMetaData> crypto_metadata;    ///< PME crypto metadata (field 4).
    // fields 5-7 skipped (encrypted_column_metadata, offset_index_offset (legacy), etc.)
    std::optional<int64_t>             bloom_filter_offset; ///< Bloom filter offset (field 8).
    std::optional<int32_t>             bloom_filter_length; ///< Bloom filter byte length (field 9).
    std::optional<int64_t>             column_index_offset; ///< Column index offset (field 10).
    std::optional<int32_t>             column_index_length; ///< Column index byte length (field 11).
    std::optional<int64_t>             offset_index_offset; ///< Offset index offset (field 12).
    std::optional<int32_t>             offset_index_length; ///< Offset index byte length (field 13).

    ColumnChunk() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (file_path.has_value()) {
            enc.write_field(1, compact_type::BINARY);
            enc.write_string(*file_path);
        }
        enc.write_field(2, compact_type::I64);
        enc.write_i64(file_offset);
        if (meta_data.has_value()) {
            enc.write_field(3, compact_type::STRUCT);
            meta_data->serialize(enc);
        }
        if (crypto_metadata.has_value()) {
            enc.write_field(4, compact_type::STRUCT);
            crypto_metadata->serialize(enc);
        }
        if (bloom_filter_offset.has_value()) {
            enc.write_field(8, compact_type::I64);
            enc.write_i64(*bloom_filter_offset);
        }
        if (bloom_filter_length.has_value()) {
            enc.write_field(9, compact_type::I32);
            enc.write_i32(*bloom_filter_length);
        }
        if (column_index_offset.has_value()) {
            enc.write_field(10, compact_type::I64);
            enc.write_i64(*column_index_offset);
        }
        if (column_index_length.has_value()) {
            enc.write_field(11, compact_type::I32);
            enc.write_i32(*column_index_length);
        }
        if (offset_index_offset.has_value()) {
            enc.write_field(12, compact_type::I64);
            enc.write_i64(*offset_index_offset);
        }
        if (offset_index_length.has_value()) {
            enc.write_field(13, compact_type::I32);
            enc.write_i32(*offset_index_length);
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnChunk: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnChunk: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnChunk.file_path: expected BINARY"};
                    }
                    file_path = dec.read_string();
                    break;
                case 2:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnChunk.file_offset: expected I64"};
                    }
                    file_offset = dec.read_i64();
                    break;
                case 3: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnChunk.meta_data: expected STRUCT"};
                    }
                    meta_data.emplace();
                    if (auto r = meta_data->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 4: {
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnChunk.crypto_metadata: expected STRUCT"};
                    }
                    crypto_metadata.emplace();
                    if (auto r = crypto_metadata->deserialize(dec); !r.has_value()) return r.error();
                    break;
                }
                case 8:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnChunk.bloom_filter_offset: expected I64"};
                    }
                    bloom_filter_offset = dec.read_i64();
                    break;
                case 9:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnChunk.bloom_filter_length: expected I32"};
                    }
                    bloom_filter_length = dec.read_i32();
                    break;
                case 10:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnChunk.column_index_offset: expected I64"};
                    }
                    column_index_offset = dec.read_i64();
                    break;
                case 11:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnChunk.column_index_length: expected I32"};
                    }
                    column_index_length = dec.read_i32();
                    break;
                case 12:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnChunk.offset_index_offset: expected I64"};
                    }
                    offset_index_offset = dec.read_i64();
                    break;
                case 13:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "ColumnChunk.offset_index_length: expected I32"};
                    }
                    offset_index_length = dec.read_i32();
                    break;
                default:
                    dec.skip_field(ftype); // includes fields 5-7
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnChunk: decoder error"};
        return {};
    }
};

// ============================================================================
// § 7  Ordering family  (parquet-format 2.9.0)
// ============================================================================

/// Sort order for column statistics (parquet.thrift SortOrder enum).
enum class SortOrder : int32_t {
    SIGNED   = 0,  ///< Values compared as signed integers or IEEE 754 floats.
    UNSIGNED = 1,  ///< Values compared as unsigned integers or bytes.
    UNKNOWN  = 2   ///< Sort order unknown or inapplicable.
};

/// ColumnOrder union: describes how a column's values are compared for statistics.
///
/// TypeDefinedOrder (field 1) is the only defined variant: statistics follow the
/// canonical type-defined order per parquet-format 2.9.0.
struct ColumnOrder {
    enum class Kind : int32_t { NONE = 0, TYPE_ORDER = 1 } kind = Kind::TYPE_ORDER;

    ColumnOrder() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        if (kind == Kind::TYPE_ORDER) {
            enc.write_field(1, compact_type::STRUCT);
            // TypeDefinedOrder: empty struct
            enc.begin_struct(); enc.write_stop(); enc.end_struct();
        }
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnOrder: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnOrder: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::STRUCT) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnOrder.TYPE_ORDER: expected STRUCT"};
                    }
                    kind = Kind::TYPE_ORDER;
                    dec.skip_field(compact_type::STRUCT); // consume empty TypeDefinedOrder body
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "ColumnOrder: decoder error"};
        return {};
    }
};

/// SortingColumn: describes sort key for a column within a RowGroup.
///
/// RowGroup field 4 per parquet-format 2.9.0.
struct SortingColumn {
    int32_t column_idx  = 0;     ///< Zero-based column index within the schema (field 1).
    bool    descending  = false; ///< True for descending sort order (field 2).
    bool    nulls_first = true;  ///< True if nulls sort before non-null values (field 3).

    SortingColumn() = default;
    SortingColumn(int32_t idx, bool desc, bool nf)
        : column_idx(idx), descending(desc), nulls_first(nf) {}

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(column_idx);
        enc.write_field_bool(2, descending);
        enc.write_field_bool(3, nulls_first);
        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "SortingColumn: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "SortingColumn: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SortingColumn.column_idx: expected I32"};
                    }
                    column_idx = dec.read_i32();
                    break;
                case 2:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SortingColumn.descending: expected BOOL"};
                    }
                    descending = dec.read_bool();
                    break;
                case 3:
                    if (ftype != compact_type::BOOL_TRUE && ftype != compact_type::BOOL_FALSE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "SortingColumn.nulls_first: expected BOOL"};
                    }
                    nulls_first = dec.read_bool();
                    break;
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "SortingColumn: decoder error"};
        return {};
    }
};

// ============================================================================
// § 8  Row group and file metadata  (existing, updated for parquet-format 2.9.0)
// ============================================================================

/// Parquet row group (parquet.thrift fields 1-4).
///
/// A row group is a horizontal partition of the table containing one column
/// chunk per column. Field 4 (sorting_columns) added per parquet-format 2.9.0.
struct RowGroup {
    std::vector<ColumnChunk>    columns;              ///< Column chunks (field 1).
    int64_t                     total_byte_size = 0;  ///< Total byte size (field 2).
    int64_t                     num_rows        = 0;  ///< Number of rows (field 3).
    std::vector<SortingColumn>  sorting_columns;      ///< Sort keys (field 4, optional).

    RowGroup() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::LIST);
        enc.write_list_header(compact_type::STRUCT, static_cast<int32_t>(columns.size()));
        for (const auto& col : columns) {
            col.serialize(enc);
        }

        enc.write_field(2, compact_type::I64);
        enc.write_i64(total_byte_size);
        enc.write_field(3, compact_type::I64);
        enc.write_i64(num_rows);

        if (!sorting_columns.empty()) {
            enc.write_field(4, compact_type::LIST);
            enc.write_list_header(compact_type::STRUCT,
                                  static_cast<int32_t>(sorting_columns.size()));
            for (const auto& sc : sorting_columns) {
                sc.serialize(enc);
            }
        }

        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "RowGroup: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "RowGroup: field header error"};
            switch (fid) {
                case 1: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "RowGroup.columns: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_STRUCT_LIST_SIZE = 10000;
                    if (count < 0 || count > MAX_STRUCT_LIST_SIZE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "RowGroup.columns: list exceeds maximum size"};
                    }
                    columns.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        if (auto r = columns[static_cast<size_t>(i)].deserialize(dec);
                            !r.has_value()) {
                            return r.error();
                        }
                    }
                    break;
                }
                case 2:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "RowGroup.total_byte_size: expected I64"};
                    }
                    total_byte_size = dec.read_i64();
                    break;
                case 3:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "RowGroup.num_rows: expected I64"};
                    }
                    num_rows = dec.read_i64();
                    break;
                case 4: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "RowGroup.sorting_columns: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_SORT_COLS = 10000;
                    if (count < 0 || count > MAX_SORT_COLS) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "RowGroup.sorting_columns: list exceeds maximum size"};
                    }
                    sorting_columns.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        if (auto r = sorting_columns[static_cast<size_t>(i)].deserialize(dec);
                            !r.has_value()) {
                            return r.error();
                        }
                    }
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "RowGroup: decoder error"};
        return {};
    }
};

/// Parquet file metadata (parquet.thrift fields 1-7).
///
/// The root metadata structure written in the Parquet footer. Contains the
/// schema, row groups, file-level key-value metadata, creator string, and
/// column orders (field 7, added per parquet-format 2.9.0).
/// Serialized using Thrift Compact Protocol at the end of every Parquet file.
struct FileMetaData {
    int32_t                              version = PARQUET_VERSION;
    std::vector<SchemaElement>           schema;
    int64_t                              num_rows = 0;
    std::vector<RowGroup>                row_groups;
    std::optional<std::vector<KeyValue>> key_value_metadata;
    std::optional<std::string>           created_by;
    std::optional<std::vector<ColumnOrder>> column_orders; ///< Per-column ordering (field 7).

    FileMetaData() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(version);

        enc.write_field(2, compact_type::LIST);
        enc.write_list_header(compact_type::STRUCT, static_cast<int32_t>(schema.size()));
        for (const auto& elem : schema) {
            elem.serialize(enc);
        }

        enc.write_field(3, compact_type::I64);
        enc.write_i64(num_rows);

        enc.write_field(4, compact_type::LIST);
        enc.write_list_header(compact_type::STRUCT, static_cast<int32_t>(row_groups.size()));
        for (const auto& rg : row_groups) {
            rg.serialize(enc);
        }

        if (key_value_metadata.has_value()) {
            enc.write_field(5, compact_type::LIST);
            enc.write_list_header(compact_type::STRUCT,
                                  static_cast<int32_t>(key_value_metadata->size()));
            for (const auto& kv : *key_value_metadata) {
                kv.serialize(enc);
            }
        }

        if (created_by.has_value()) {
            enc.write_field(6, compact_type::BINARY);
            enc.write_string(*created_by);
        }

        if (column_orders.has_value()) {
            enc.write_field(7, compact_type::LIST);
            enc.write_list_header(compact_type::STRUCT,
                                  static_cast<int32_t>(column_orders->size()));
            for (const auto& co : *column_orders) {
                co.serialize(enc);
            }
        }

        enc.write_stop();
        enc.end_struct();
    }

    [[nodiscard]] expected<void> deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData: begin_struct failed"};
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData: field header error"};
            switch (fid) {
                case 1:
                    if (ftype != compact_type::I32) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData.version: expected I32"};
                    }
                    version = dec.read_i32();
                    break;
                case 2: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData.schema: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_STRUCT_LIST_SIZE = 10000;
                    if (count < 0 || count > MAX_STRUCT_LIST_SIZE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileMetaData.schema: list exceeds maximum size"};
                    }
                    schema.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        if (auto r = schema[static_cast<size_t>(i)].deserialize(dec);
                            !r.has_value()) {
                            return r.error();
                        }
                    }
                    break;
                }
                case 3:
                    if (ftype != compact_type::I64) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData.num_rows: expected I64"};
                    }
                    num_rows = dec.read_i64();
                    break;
                case 4: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData.row_groups: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_STRUCT_LIST_SIZE_RG = 10000;
                    if (count < 0 || count > MAX_STRUCT_LIST_SIZE_RG) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileMetaData.row_groups: list exceeds maximum size"};
                    }
                    row_groups.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        if (auto r = row_groups[static_cast<size_t>(i)].deserialize(dec);
                            !r.has_value()) {
                            return r.error();
                        }
                    }
                    break;
                }
                case 5: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileMetaData.key_value_metadata: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_KV_LIST_SIZE = 1'000'000;
                    if (count < 0 || count > MAX_KV_LIST_SIZE) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileMetaData.key_value_metadata: list exceeds maximum size"};
                    }
                    key_value_metadata.emplace();
                    key_value_metadata->resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        if (auto r = (*key_value_metadata)[static_cast<size_t>(i)].deserialize(dec);
                            !r.has_value()) {
                            return r.error();
                        }
                    }
                    break;
                }
                case 6:
                    if (ftype != compact_type::BINARY) {
                        return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData.created_by: expected BINARY"};
                    }
                    created_by = dec.read_string();
                    break;
                case 7: {
                    if (ftype != compact_type::LIST) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileMetaData.column_orders: expected LIST"};
                    }
                    auto [elem_type, count] = dec.read_list_header();
                    static constexpr int32_t MAX_COL_ORDERS = 10000;
                    if (count < 0 || count > MAX_COL_ORDERS) {
                        return {ErrorCode::THRIFT_DECODE_ERROR,
                                "FileMetaData.column_orders: list exceeds maximum size"};
                    }
                    column_orders.emplace();
                    column_orders->resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        if (auto r = (*column_orders)[static_cast<size_t>(i)].deserialize(dec);
                            !r.has_value()) {
                            return r.error();
                        }
                    }
                    break;
                }
                default:
                    dec.skip_field(ftype);
                    break;
            }
        }
        dec.end_struct();
        if (!dec.good()) return {ErrorCode::THRIFT_DECODE_ERROR, "FileMetaData: decoder error"};
        return {};
    }
};

} // namespace signet::forge::thrift
