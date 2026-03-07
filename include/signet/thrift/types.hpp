// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file types.hpp
/// @brief Parquet Thrift struct types -- C++ structs matching parquet.thrift,
///        with Compact Protocol serialize/deserialize methods.

// ---------------------------------------------------------------------------
// Parquet Thrift struct types — C++ structs matching parquet.thrift exactly
//
// Each struct has:
//   void serialize(CompactEncoder& enc) const
//   void deserialize(CompactDecoder& dec)
//
// Field IDs match the canonical parquet.thrift specification.
// Optional fields use std::optional<T> and are only written when present.
// ---------------------------------------------------------------------------

#include "signet/types.hpp"
#include "signet/thrift/compact.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace signet::forge::thrift {

/// Parquet KeyValue metadata entry (parquet.thrift field IDs 1-2).
///
/// Used for file-level and column-level key-value metadata pairs.
struct KeyValue {
    std::string                key;    ///< Metadata key (field 1, required).
    std::optional<std::string> value;  ///< Metadata value (field 2, optional).

    /// Default constructor.
    KeyValue() = default;

    /// Construct with key and value.
    KeyValue(std::string k, std::string v)
        : key(std::move(k)), value(std::move(v)) {}

    /// Serialize this KeyValue to the Thrift Compact Protocol encoder.
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

    /// Deserialize this KeyValue from the Thrift Compact Protocol decoder.
    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: key   = dec.read_string(); break;
                case 2: value = dec.read_string(); break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

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

    /// Default constructor.
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

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: max            = dec.read_string(); break;
                case 2: min            = dec.read_string(); break;
                case 3: null_count     = dec.read_i64();    break;
                case 4: distinct_count = dec.read_i64();    break;
                case 5: max_value      = dec.read_string(); break;
                case 6: min_value      = dec.read_string(); break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Parquet schema element (parquet.thrift fields 1-8).
///
/// Represents a single node in the Parquet schema tree. Leaf nodes have
/// a physical type; group nodes have num_children instead.
struct SchemaElement {
    std::optional<PhysicalType>  type;            ///< Physical type (field 1, absent for group nodes).
    std::optional<int32_t>       type_length;     ///< Type length for FIXED_LEN_BYTE_ARRAY (field 2).
    std::optional<Repetition>    repetition_type; ///< REQUIRED/OPTIONAL/REPEATED (field 3).
    std::string                  name;            ///< Column or group name (field 4, required).
    std::optional<int32_t>       num_children;    ///< Number of children for group nodes (field 5).
    std::optional<ConvertedType> converted_type;  ///< Logical/converted type (field 6).
    std::optional<int32_t>       scale;           ///< Decimal scale (field 7).
    std::optional<int32_t>       precision;       ///< Decimal precision (field 8).

    /// Default constructor.
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
        enc.write_stop();
        enc.end_struct();
    }

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: type            = static_cast<PhysicalType>(dec.read_i32());  break;
                case 2: type_length     = dec.read_i32();                             break;
                case 3: repetition_type = static_cast<Repetition>(dec.read_i32());    break;
                case 4: name            = dec.read_string();                          break;
                case 5: num_children    = dec.read_i32();                             break;
                case 6: converted_type  = static_cast<ConvertedType>(dec.read_i32()); break;
                case 7: scale           = dec.read_i32();                             break;
                case 8: precision       = dec.read_i32();                             break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Parquet data page header V1 (parquet.thrift fields 1-4, all required).
///
/// Describes the encoding and value count for a V1 data page.
struct DataPageHeader {
    int32_t  num_values                  = 0;             ///< Number of values in this page (field 1).
    Encoding encoding                    = Encoding::PLAIN; ///< Data encoding (field 2).
    Encoding definition_level_encoding   = Encoding::RLE;   ///< Definition level encoding (field 3).
    Encoding repetition_level_encoding   = Encoding::RLE;   ///< Repetition level encoding (field 4).

    /// Default constructor.
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
        enc.write_stop();
        enc.end_struct();
    }

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: num_values                = dec.read_i32();                          break;
                case 2: encoding                  = static_cast<Encoding>(dec.read_i32());   break;
                case 3: definition_level_encoding = static_cast<Encoding>(dec.read_i32());   break;
                case 4: repetition_level_encoding = static_cast<Encoding>(dec.read_i32());   break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Parquet dictionary page header (parquet.thrift fields 1-2, all required).
///
/// Describes the dictionary used by PLAIN_DICTIONARY / RLE_DICTIONARY encoded columns.
struct DictionaryPageHeader {
    int32_t  num_values = 0;                              ///< Number of dictionary entries (field 1).
    Encoding encoding   = Encoding::PLAIN_DICTIONARY;     ///< Dictionary encoding (field 2).

    /// Default constructor.
    DictionaryPageHeader() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        enc.write_field(1, compact_type::I32);
        enc.write_i32(num_values);
        enc.write_field(2, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(encoding));
        enc.write_stop();
        enc.end_struct();
    }

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: num_values = dec.read_i32();                        break;
                case 2: encoding   = static_cast<Encoding>(dec.read_i32()); break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
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

    /// Default constructor.
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

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: num_values                    = dec.read_i32();                        break;
                case 2: num_nulls                     = dec.read_i32();                        break;
                case 3: num_rows                      = dec.read_i32();                        break;
                case 4: encoding                      = static_cast<Encoding>(dec.read_i32()); break;
                case 5: definition_levels_byte_length = dec.read_i32();                        break;
                case 6: repetition_levels_byte_length = dec.read_i32();                        break;
                case 7: is_compressed                 = dec.read_bool();                       break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
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
struct PageHeader {
    PageType type                                          = PageType::DATA_PAGE; ///< Page type (field 1).
    int32_t  uncompressed_page_size                        = 0;  ///< Uncompressed size in bytes (field 2).
    int32_t  compressed_page_size                          = 0;  ///< Compressed size in bytes (field 3).
    std::optional<int32_t>              crc;                     ///< CRC-32 checksum (field 4).
    std::optional<DataPageHeader>       data_page_header;        ///< V1 data page sub-header (field 5).
    // field 6: index_page_header -- skipped (not used in Signet)
    std::optional<DictionaryPageHeader> dictionary_page_header;  ///< Dictionary page sub-header (field 7).
    std::optional<DataPageHeaderV2>     data_page_header_v2;     ///< V2 data page sub-header (field 8).

    /// Default constructor.
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

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: type                    = static_cast<PageType>(dec.read_i32()); break;
                case 2: uncompressed_page_size  = dec.read_i32();                        break;
                case 3: compressed_page_size    = dec.read_i32();                        break;
                case 4: crc                     = dec.read_i32();                        break;
                case 5: {
                    data_page_header.emplace();
                    data_page_header->deserialize(dec);
                    break;
                }
                case 6: dec.skip_field(ftype); break; // index_page_header — skipped
                case 7: {
                    dictionary_page_header.emplace();
                    dictionary_page_header->deserialize(dec);
                    break;
                }
                case 8: {
                    data_page_header_v2.emplace();
                    data_page_header_v2->deserialize(dec);
                    break;
                }
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Parquet column metadata (parquet.thrift fields 1-12).
///
/// Contains the physical type, encodings, compression, byte sizes, offsets,
/// and optional statistics for a single column chunk within a row group.
struct ColumnMetaData {
    PhysicalType                       type                    = PhysicalType::BYTE_ARRAY; ///< Physical type (field 1).
    std::vector<Encoding>              encodings;              ///< Encodings used (field 2).
    std::vector<std::string>           path_in_schema;         ///< Schema path components (field 3).
    Compression                        codec                   = Compression::UNCOMPRESSED; ///< Compression codec (field 4).
    int64_t                            num_values              = 0;  ///< Total value count (field 5).
    int64_t                            total_uncompressed_size = 0;  ///< Uncompressed byte size (field 6).
    int64_t                            total_compressed_size   = 0;  ///< Compressed byte size (field 7).
    std::optional<std::vector<KeyValue>> key_value_metadata;        ///< Column-level KV metadata (field 8).
    int64_t                            data_page_offset        = 0;  ///< File offset of first data page (field 9).
    std::optional<int64_t>             index_page_offset;            ///< File offset of index page (field 10).
    std::optional<int64_t>             dictionary_page_offset;       ///< File offset of dictionary page (field 11).
    std::optional<Statistics>          statistics;                    ///< Column statistics (field 12).

    /// Default constructor.
    ColumnMetaData() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        // field 1: type
        enc.write_field(1, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(type));

        // field 2: encodings (list<i32>)
        enc.write_field(2, compact_type::LIST);
        enc.write_list_header(compact_type::I32, static_cast<int32_t>(encodings.size()));
        for (auto e : encodings) {
            enc.write_i32(static_cast<int32_t>(e));
        }

        // field 3: path_in_schema (list<string>)
        enc.write_field(3, compact_type::LIST);
        enc.write_list_header(compact_type::BINARY, static_cast<int32_t>(path_in_schema.size()));
        for (const auto& p : path_in_schema) {
            enc.write_string(p);
        }

        // field 4: codec
        enc.write_field(4, compact_type::I32);
        enc.write_i32(static_cast<int32_t>(codec));

        // field 5: num_values
        enc.write_field(5, compact_type::I64);
        enc.write_i64(num_values);

        // field 6: total_uncompressed_size
        enc.write_field(6, compact_type::I64);
        enc.write_i64(total_uncompressed_size);

        // field 7: total_compressed_size
        enc.write_field(7, compact_type::I64);
        enc.write_i64(total_compressed_size);

        // field 8: key_value_metadata (optional, list<KeyValue>)
        if (key_value_metadata.has_value()) {
            enc.write_field(8, compact_type::LIST);
            enc.write_list_header(compact_type::STRUCT,
                                  static_cast<int32_t>(key_value_metadata->size()));
            for (const auto& kv : *key_value_metadata) {
                kv.serialize(enc);
            }
        }

        // field 9: data_page_offset
        enc.write_field(9, compact_type::I64);
        enc.write_i64(data_page_offset);

        // field 10: index_page_offset (optional)
        if (index_page_offset.has_value()) {
            enc.write_field(10, compact_type::I64);
            enc.write_i64(*index_page_offset);
        }

        // field 11: dictionary_page_offset (optional)
        if (dictionary_page_offset.has_value()) {
            enc.write_field(11, compact_type::I64);
            enc.write_i64(*dictionary_page_offset);
        }

        // field 12: statistics (optional, struct)
        if (statistics.has_value()) {
            enc.write_field(12, compact_type::STRUCT);
            statistics->serialize(enc);
        }

        enc.write_stop();
        enc.end_struct();
    }

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1:
                    type = static_cast<PhysicalType>(dec.read_i32());
                    break;
                case 2: {
                    auto [elem_type, count] = dec.read_list_header();
                    encodings.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        encodings[static_cast<size_t>(i)] =
                            static_cast<Encoding>(dec.read_i32());
                    }
                    break;
                }
                case 3: {
                    auto [elem_type, count] = dec.read_list_header();
                    path_in_schema.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        path_in_schema[static_cast<size_t>(i)] = dec.read_string();
                    }
                    break;
                }
                case 4:
                    codec = static_cast<Compression>(dec.read_i32());
                    break;
                case 5:  num_values              = dec.read_i64(); break;
                case 6:  total_uncompressed_size = dec.read_i64(); break;
                case 7:  total_compressed_size   = dec.read_i64(); break;
                case 8: {
                    auto [elem_type, count] = dec.read_list_header();
                    key_value_metadata.emplace();
                    key_value_metadata->resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        (*key_value_metadata)[static_cast<size_t>(i)].deserialize(dec);
                    }
                    break;
                }
                case 9:  data_page_offset        = dec.read_i64(); break;
                case 10: index_page_offset       = dec.read_i64(); break;
                case 11: dictionary_page_offset  = dec.read_i64(); break;
                case 12: {
                    statistics.emplace();
                    statistics->deserialize(dec);
                    break;
                }
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Parquet column chunk descriptor (parquet.thrift fields 1-13).
///
/// Locates a single column chunk within the file and optionally carries
/// inline column metadata, bloom filter, and column/offset index locations.
struct ColumnChunk {
    std::optional<std::string>       file_path;              ///< External file path (field 1, absent if same file).
    int64_t                          file_offset = 0;        ///< Byte offset of the column chunk in the file (field 2).
    std::optional<ColumnMetaData>    meta_data;              ///< Inline column metadata (field 3).
    // fields 4-7 skipped (crypto_metadata, encrypted_column_metadata, etc.)
    std::optional<int64_t>           bloom_filter_offset;    ///< Bloom filter offset (field 8).
    std::optional<int32_t>           bloom_filter_length;    ///< Bloom filter byte length (field 9).
    std::optional<int64_t>           column_index_offset;    ///< Column index offset (field 10).
    std::optional<int32_t>           column_index_length;    ///< Column index byte length (field 11).
    std::optional<int64_t>           offset_index_offset;    ///< Offset index offset (field 12).
    std::optional<int32_t>           offset_index_length;    ///< Offset index byte length (field 13).

    /// Default constructor.
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

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: file_path = dec.read_string(); break;
                case 2: file_offset = dec.read_i64();  break;
                case 3: {
                    meta_data.emplace();
                    meta_data->deserialize(dec);
                    break;
                }
                case 8: bloom_filter_offset = dec.read_i64(); break;
                case 9: bloom_filter_length = dec.read_i32(); break;
                case 10: column_index_offset = dec.read_i64(); break;
                case 11: column_index_length = dec.read_i32(); break;
                case 12: offset_index_offset = dec.read_i64(); break;
                case 13: offset_index_length = dec.read_i32(); break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Parquet row group (parquet.thrift fields 1-3).
///
/// A row group is a horizontal partition of the table containing one column
/// chunk per column. Field 4 (sorting_columns) is not implemented.
struct RowGroup {
    std::vector<ColumnChunk> columns;          ///< Column chunks in this row group (field 1).
    int64_t                  total_byte_size = 0; ///< Total byte size of all column chunks (field 2).
    int64_t                  num_rows        = 0; ///< Number of rows in this row group (field 3).

    /// Default constructor.
    RowGroup() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        // field 1: columns (list<ColumnChunk>)
        enc.write_field(1, compact_type::LIST);
        enc.write_list_header(compact_type::STRUCT, static_cast<int32_t>(columns.size()));
        for (const auto& col : columns) {
            col.serialize(enc);
        }

        enc.write_field(2, compact_type::I64);
        enc.write_i64(total_byte_size);

        enc.write_field(3, compact_type::I64);
        enc.write_i64(num_rows);

        // field 4: sorting_columns — skipped
        enc.write_stop();
        enc.end_struct();
    }

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: {
                    auto [elem_type, count] = dec.read_list_header();
                    columns.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        columns[static_cast<size_t>(i)].deserialize(dec);
                    }
                    break;
                }
                case 2: total_byte_size = dec.read_i64(); break;
                case 3: num_rows        = dec.read_i64(); break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

/// Parquet file metadata (parquet.thrift fields 1-6).
///
/// The root metadata structure written in the Parquet footer. Contains the
/// schema, row groups, file-level key-value metadata, and creator string.
/// Serialized using Thrift Compact Protocol at the end of every Parquet file.
struct FileMetaData {
    int32_t                              version = PARQUET_VERSION;  ///< Parquet format version (field 1).
    std::vector<SchemaElement>           schema;                     ///< Flattened schema tree (field 2).
    int64_t                              num_rows = 0;               ///< Total row count across all row groups (field 3).
    std::vector<RowGroup>                row_groups;                  ///< Row groups in this file (field 4).
    std::optional<std::vector<KeyValue>> key_value_metadata;         ///< File-level KV metadata (field 5).
    std::optional<std::string>           created_by;                 ///< Creator application string (field 6).

    /// Default constructor.
    FileMetaData() = default;

    void serialize(CompactEncoder& enc) const {
        enc.begin_struct();
        // field 1: version
        enc.write_field(1, compact_type::I32);
        enc.write_i32(version);

        // field 2: schema (list<SchemaElement>)
        enc.write_field(2, compact_type::LIST);
        enc.write_list_header(compact_type::STRUCT, static_cast<int32_t>(schema.size()));
        for (const auto& elem : schema) {
            elem.serialize(enc);
        }

        // field 3: num_rows
        enc.write_field(3, compact_type::I64);
        enc.write_i64(num_rows);

        // field 4: row_groups (list<RowGroup>)
        enc.write_field(4, compact_type::LIST);
        enc.write_list_header(compact_type::STRUCT, static_cast<int32_t>(row_groups.size()));
        for (const auto& rg : row_groups) {
            rg.serialize(enc);
        }

        // field 5: key_value_metadata (optional, list<KeyValue>)
        if (key_value_metadata.has_value()) {
            enc.write_field(5, compact_type::LIST);
            enc.write_list_header(compact_type::STRUCT,
                                  static_cast<int32_t>(key_value_metadata->size()));
            for (const auto& kv : *key_value_metadata) {
                kv.serialize(enc);
            }
        }

        // field 6: created_by (optional)
        if (created_by.has_value()) {
            enc.write_field(6, compact_type::BINARY);
            enc.write_string(*created_by);
        }

        enc.write_stop();
        enc.end_struct();
    }

    void deserialize(CompactDecoder& dec) {
        dec.begin_struct();
        for (;;) {
            auto [fid, ftype] = dec.read_field_header();
            if (ftype == compact_type::STOP) break;
            switch (fid) {
                case 1: version = dec.read_i32(); break;
                case 2: {
                    auto [elem_type, count] = dec.read_list_header();
                    schema.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        schema[static_cast<size_t>(i)].deserialize(dec);
                    }
                    break;
                }
                case 3: num_rows = dec.read_i64(); break;
                case 4: {
                    auto [elem_type, count] = dec.read_list_header();
                    row_groups.resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        row_groups[static_cast<size_t>(i)].deserialize(dec);
                    }
                    break;
                }
                case 5: {
                    auto [elem_type, count] = dec.read_list_header();
                    key_value_metadata.emplace();
                    key_value_metadata->resize(static_cast<size_t>(count));
                    for (int32_t i = 0; i < count; ++i) {
                        (*key_value_metadata)[static_cast<size_t>(i)].deserialize(dec);
                    }
                    break;
                }
                case 6: created_by = dec.read_string(); break;
                default: dec.skip_field(ftype); break;
            }
        }
        dec.end_struct();
    }
};

} // namespace signet::forge::thrift
