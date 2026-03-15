// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
//
// Thrift Correctness Phase — New Type Tests (parquet-format 2.9.0)
//
// Covers all additions from the Thrift Parser Gaps Audit implementation:
//   - Protocol correctness: zigzag i32/i64, i8 wire type
//   - Field-type validation (CWE-843): wrong wire type rejected
//   - Required-field bitmask enforcement: KeyValue, DataPageHeader, ColumnMetaData
//   - LogicalType Thrift union family: round-trip for all 5 Sub-phase A types
//   - TimeUnit round-trip (MILLIS, MICROS, NANOS)
//   - SchemaElement with logicalType field 10
//   - DataPageHeader with statistics field 5
//   - DictionaryPageHeader with is_sorted field 3
//   - Ordering types: ColumnOrder, SortingColumn
//   - Bloom filter header types: BloomFilterHeader round-trip
//   - Encryption types: AesGcmV1, EncryptionAlgorithm, FileCryptoMetaData, ColumnCryptoMetaData
//   - expected<void> error propagation from nested deserialize failures
//   - FileMetaData with column_orders field 7
//   - RowGroup with sorting_columns field 4

#include <catch2/catch_test_macros.hpp>

#include "signet/thrift/compact.hpp"
#include "signet/thrift/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

using namespace signet::forge;
using namespace signet::forge::thrift;

// Helper: serialize a struct to bytes, then deserialize into a new instance.
// Returns the deserialized object and verifies the round-trip succeeded.
template <typename T>
static T round_trip(const T& val) {
    CompactEncoder enc;
    val.serialize(enc);
    const auto& buf = enc.data();

    T out;
    CompactDecoder dec(buf.data(), buf.size());
    auto r = out.deserialize(dec);
    REQUIRE(r.has_value());
    REQUIRE(dec.good());
    return out;
}

// =============================================================================
// § Protocol correctness
// =============================================================================

TEST_CASE("zigzag_encode_i32: no signed-shift UB, matches i64 pattern", "[thrift][protocol]") {
    // Verify the fixed zigzag for i32 produces correct values across the range.
    // The encode function is exercised via write_i32 → read_i32.
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I32);
    enc.write_i32(0);
    enc.write_field(2, compact_type::I32);
    enc.write_i32(-1);
    enc.write_field(3, compact_type::I32);
    enc.write_i32(1);
    enc.write_field(4, compact_type::I32);
    enc.write_i32(std::numeric_limits<int32_t>::min());
    enc.write_field(5, compact_type::I32);
    enc.write_i32(std::numeric_limits<int32_t>::max());
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    dec.begin_struct();
    auto [f1, t1] = dec.read_field_header();
    REQUIRE(f1 == 1); REQUIRE(t1 == compact_type::I32);
    REQUIRE(dec.read_i32() == 0);
    auto [f2, t2] = dec.read_field_header();
    REQUIRE(f2 == 2); REQUIRE(t2 == compact_type::I32);
    REQUIRE(dec.read_i32() == -1);
    auto [f3, t3] = dec.read_field_header();
    REQUIRE(f3 == 3); REQUIRE(t3 == compact_type::I32);
    REQUIRE(dec.read_i32() == 1);
    auto [f4, t4] = dec.read_field_header();
    REQUIRE(f4 == 4); REQUIRE(t4 == compact_type::I32);
    REQUIRE(dec.read_i32() == std::numeric_limits<int32_t>::min());
    auto [f5, t5] = dec.read_field_header();
    REQUIRE(f5 == 5); REQUIRE(t5 == compact_type::I32);
    REQUIRE(dec.read_i32() == std::numeric_limits<int32_t>::max());
    dec.end_struct();
    REQUIRE(dec.good());
}

TEST_CASE("zigzag_encode_i64: round-trip boundary values", "[thrift][protocol]") {
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I64);
    enc.write_i64(std::numeric_limits<int64_t>::min());
    enc.write_field(2, compact_type::I64);
    enc.write_i64(std::numeric_limits<int64_t>::max());
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    dec.begin_struct();
    auto [f1, t1] = dec.read_field_header();
    REQUIRE(f1 == 1); REQUIRE(t1 == compact_type::I64);
    REQUIRE(dec.read_i64() == std::numeric_limits<int64_t>::min());
    auto [f2, t2] = dec.read_field_header();
    REQUIRE(f2 == 2); REQUIRE(t2 == compact_type::I64);
    REQUIRE(dec.read_i64() == std::numeric_limits<int64_t>::max());
    dec.end_struct();
    REQUIRE(dec.good());
}

TEST_CASE("write_i8/read_i8: raw byte round-trip", "[thrift][protocol]") {
    for (int v : {-128, -1, 0, 1, 127}) {
        CompactEncoder enc;
        enc.write_i8(static_cast<int8_t>(v));
        const auto& buf = enc.data();
        REQUIRE(buf.size() == 1);
        CompactDecoder dec(buf.data(), buf.size());
        REQUIRE(dec.read_i8() == static_cast<int8_t>(v));
        REQUIRE(dec.good());
    }
}

// =============================================================================
// § Field-type validation (CWE-843)
// =============================================================================

TEST_CASE("KeyValue: wrong wire type for key field returns error", "[thrift][ftype]") {
    // Encode field 1 as I32 instead of BINARY — must be rejected.
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I32);
    enc.write_i32(42);
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    KeyValue kv;
    auto r = kv.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("Statistics: wrong wire type for null_count returns error", "[thrift][ftype]") {
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(3, compact_type::I32); // null_count expects I64
    enc.write_i32(5);
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    Statistics st;
    auto r = st.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("DataPageHeader: wrong wire type for num_values returns error", "[thrift][ftype]") {
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::BINARY); // num_values expects I32
    enc.write_string("bad");
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    DataPageHeader dph;
    auto r = dph.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("IntType: wrong wire type for bit_width returns error", "[thrift][ftype]") {
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I32); // bit_width expects I8
    enc.write_i32(32);
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    IntType it;
    auto r = it.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("DecimalType: wrong wire type for scale returns error", "[thrift][ftype]") {
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I64); // scale expects I32
    enc.write_i64(4);
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    DecimalType dt;
    auto r = dt.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

// =============================================================================
// § Required-field bitmask enforcement
// =============================================================================

TEST_CASE("KeyValue: missing required key field returns error", "[thrift][required]") {
    // Encode only value (field 2), omitting the required key (field 1).
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(2, compact_type::BINARY);
    enc.write_string("only-value");
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    KeyValue kv;
    auto r = kv.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("KeyValue: all required fields present succeeds", "[thrift][required]") {
    KeyValue kv{"my-key", "my-value"};
    auto out = round_trip(kv);
    REQUIRE(out.key == "my-key");
    REQUIRE(out.value.has_value());
    REQUIRE(*out.value == "my-value");
}

TEST_CASE("DataPageHeader: missing required fields returns error", "[thrift][required]") {
    // Encode only field 1 (num_values); fields 2-4 also required.
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I32);
    enc.write_i32(100);
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    DataPageHeader dph;
    auto r = dph.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("DataPageHeader: all required fields present succeeds", "[thrift][required]") {
    DataPageHeader dph;
    dph.num_values       = 1000;
    dph.encoding         = Encoding::PLAIN;
    dph.definition_level_encoding = Encoding::RLE;
    dph.repetition_level_encoding = Encoding::RLE;

    auto out = round_trip(dph);
    REQUIRE(out.num_values == 1000);
    REQUIRE(out.encoding == Encoding::PLAIN);
    REQUIRE(out.definition_level_encoding == Encoding::RLE);
    REQUIRE(out.repetition_level_encoding == Encoding::RLE);
}

TEST_CASE("ColumnMetaData: missing required fields returns error", "[thrift][required]") {
    // Encode only field 1 (type); fields 2-7,9 also required.
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I32);
    enc.write_i32(0); // INT32
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    ColumnMetaData cmd;
    auto r = cmd.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

// =============================================================================
// § TimeUnit round-trip
// =============================================================================

TEST_CASE("TimeUnit: MILLIS round-trip", "[thrift][logical_type]") {
    TimeUnit tu{TimeUnit::Kind::MILLIS};
    auto out = round_trip(tu);
    REQUIRE(out.kind == TimeUnit::Kind::MILLIS);
}

TEST_CASE("TimeUnit: MICROS round-trip", "[thrift][logical_type]") {
    TimeUnit tu{TimeUnit::Kind::MICROS};
    auto out = round_trip(tu);
    REQUIRE(out.kind == TimeUnit::Kind::MICROS);
}

TEST_CASE("TimeUnit: NANOS round-trip", "[thrift][logical_type]") {
    TimeUnit tu{TimeUnit::Kind::NANOS};
    auto out = round_trip(tu);
    REQUIRE(out.kind == TimeUnit::Kind::NANOS);
}

// =============================================================================
// § LogicalTypeUnion round-trip (Sub-phase A: STRING, DECIMAL, TIMESTAMP, INT, UUID)
// =============================================================================

TEST_CASE("LogicalTypeUnion: STRING round-trip", "[thrift][logical_type]") {
    LogicalTypeUnion ltu;
    ltu.kind = LogicalTypeUnion::Kind::STRING;

    auto out = round_trip(ltu);
    REQUIRE(out.kind == LogicalTypeUnion::Kind::STRING);
    REQUIRE_FALSE(out.decimal.has_value());
    REQUIRE_FALSE(out.timestamp.has_value());
    REQUIRE_FALSE(out.integer.has_value());
}

TEST_CASE("LogicalTypeUnion: DECIMAL round-trip", "[thrift][logical_type]") {
    LogicalTypeUnion ltu;
    ltu.kind = LogicalTypeUnion::Kind::DECIMAL;
    ltu.decimal.emplace(4, 18); // scale=4, precision=18

    auto out = round_trip(ltu);
    REQUIRE(out.kind == LogicalTypeUnion::Kind::DECIMAL);
    REQUIRE(out.decimal.has_value());
    REQUIRE(out.decimal->scale == 4);
    REQUIRE(out.decimal->precision == 18);
}

TEST_CASE("LogicalTypeUnion: TIMESTAMP(MICROS,UTC) round-trip", "[thrift][logical_type]") {
    LogicalTypeUnion ltu;
    ltu.kind = LogicalTypeUnion::Kind::TIMESTAMP;
    ltu.timestamp.emplace(true, TimeUnit{TimeUnit::Kind::MICROS});

    auto out = round_trip(ltu);
    REQUIRE(out.kind == LogicalTypeUnion::Kind::TIMESTAMP);
    REQUIRE(out.timestamp.has_value());
    REQUIRE(out.timestamp->is_adjusted_to_utc == true);
    REQUIRE(out.timestamp->unit.kind == TimeUnit::Kind::MICROS);
}

TEST_CASE("LogicalTypeUnion: TIMESTAMP(MILLIS,non-UTC) round-trip", "[thrift][logical_type]") {
    LogicalTypeUnion ltu;
    ltu.kind = LogicalTypeUnion::Kind::TIMESTAMP;
    ltu.timestamp.emplace(false, TimeUnit{TimeUnit::Kind::MILLIS});

    auto out = round_trip(ltu);
    REQUIRE(out.kind == LogicalTypeUnion::Kind::TIMESTAMP);
    REQUIRE(out.timestamp->is_adjusted_to_utc == false);
    REQUIRE(out.timestamp->unit.kind == TimeUnit::Kind::MILLIS);
}

TEST_CASE("LogicalTypeUnion: INT(32,signed) round-trip", "[thrift][logical_type]") {
    LogicalTypeUnion ltu;
    ltu.kind = LogicalTypeUnion::Kind::INT;
    ltu.integer.emplace(static_cast<int8_t>(32), true);

    auto out = round_trip(ltu);
    REQUIRE(out.kind == LogicalTypeUnion::Kind::INT);
    REQUIRE(out.integer.has_value());
    REQUIRE(out.integer->bit_width == 32);
    REQUIRE(out.integer->is_signed == true);
}

TEST_CASE("LogicalTypeUnion: INT(8,unsigned) round-trip", "[thrift][logical_type]") {
    LogicalTypeUnion ltu;
    ltu.kind = LogicalTypeUnion::Kind::INT;
    ltu.integer.emplace(static_cast<int8_t>(8), false);

    auto out = round_trip(ltu);
    REQUIRE(out.kind == LogicalTypeUnion::Kind::INT);
    REQUIRE(out.integer->bit_width == 8);
    REQUIRE(out.integer->is_signed == false);
}

TEST_CASE("LogicalTypeUnion: UUID round-trip", "[thrift][logical_type]") {
    LogicalTypeUnion ltu;
    ltu.kind = LogicalTypeUnion::Kind::UUID;

    auto out = round_trip(ltu);
    REQUIRE(out.kind == LogicalTypeUnion::Kind::UUID);
}

TEST_CASE("LogicalTypeUnion: STRING type validation rejects non-STRUCT field", "[thrift][logical_type][ftype]") {
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::BINARY); // STRING expects STRUCT
    enc.write_string("bad");
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    LogicalTypeUnion ltu;
    auto r = ltu.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

// =============================================================================
// § SchemaElement with logicalType (field 10)
// =============================================================================

TEST_CASE("SchemaElement: field_id and logicalType STRING round-trip", "[thrift][schema]") {
    SchemaElement se;
    se.name              = "amount";
    se.type              = PhysicalType::BYTE_ARRAY;
    se.field_id          = 42;
    se.logical_type.emplace();
    se.logical_type->kind = LogicalTypeUnion::Kind::STRING;

    auto out = round_trip(se);
    REQUIRE(out.name == "amount");
    REQUIRE(out.field_id.has_value());
    REQUIRE(*out.field_id == 42);
    REQUIRE(out.logical_type.has_value());
    REQUIRE(out.logical_type->kind == LogicalTypeUnion::Kind::STRING);
}

TEST_CASE("SchemaElement: DECIMAL logical type preserved", "[thrift][schema]") {
    SchemaElement se;
    se.name = "price";
    se.type = PhysicalType::INT64;
    se.logical_type.emplace();
    se.logical_type->kind = LogicalTypeUnion::Kind::DECIMAL;
    se.logical_type->decimal.emplace(4, 18);

    auto out = round_trip(se);
    REQUIRE(out.logical_type.has_value());
    REQUIRE(out.logical_type->kind == LogicalTypeUnion::Kind::DECIMAL);
    REQUIRE(out.logical_type->decimal->scale == 4);
    REQUIRE(out.logical_type->decimal->precision == 18);
}

// =============================================================================
// § DataPageHeader with statistics (field 5)
// =============================================================================

TEST_CASE("DataPageHeader: statistics field 5 round-trip", "[thrift][page_header]") {
    DataPageHeader dph;
    dph.num_values                = 512;
    dph.encoding                  = Encoding::RLE;
    dph.definition_level_encoding = Encoding::RLE;
    dph.repetition_level_encoding = Encoding::RLE;
    dph.statistics.emplace();
    dph.statistics->null_count    = 3;
    dph.statistics->min_value     = "aaa";
    dph.statistics->max_value     = "zzz";

    auto out = round_trip(dph);
    REQUIRE(out.num_values == 512);
    REQUIRE(out.statistics.has_value());
    REQUIRE(*out.statistics->null_count == 3);
    REQUIRE(*out.statistics->min_value == "aaa");
    REQUIRE(*out.statistics->max_value == "zzz");
}

// =============================================================================
// § DictionaryPageHeader with is_sorted (field 3)
// =============================================================================

TEST_CASE("DictionaryPageHeader: is_sorted field 3 round-trip (true)", "[thrift][page_header]") {
    DictionaryPageHeader dph;
    dph.num_values = 256;
    dph.encoding   = Encoding::PLAIN_DICTIONARY;
    dph.is_sorted  = true;

    auto out = round_trip(dph);
    REQUIRE(out.num_values == 256);
    REQUIRE(out.is_sorted.has_value());
    REQUIRE(*out.is_sorted == true);
}

TEST_CASE("DictionaryPageHeader: is_sorted field 3 round-trip (false)", "[thrift][page_header]") {
    DictionaryPageHeader dph;
    dph.num_values = 128;
    dph.encoding   = Encoding::PLAIN_DICTIONARY;
    dph.is_sorted  = false;

    auto out = round_trip(dph);
    REQUIRE(out.is_sorted.has_value());
    REQUIRE(*out.is_sorted == false);
}

TEST_CASE("DictionaryPageHeader: without is_sorted round-trip", "[thrift][page_header]") {
    DictionaryPageHeader dph;
    dph.num_values = 64;
    dph.encoding   = Encoding::PLAIN_DICTIONARY;

    auto out = round_trip(dph);
    REQUIRE(out.num_values == 64);
    REQUIRE_FALSE(out.is_sorted.has_value());
}

// =============================================================================
// § Ordering types
// =============================================================================

TEST_CASE("ColumnOrder: TYPE_ORDER round-trip", "[thrift][ordering]") {
    ColumnOrder co;
    co.kind = ColumnOrder::Kind::TYPE_ORDER;

    auto out = round_trip(co);
    REQUIRE(out.kind == ColumnOrder::Kind::TYPE_ORDER);
}

TEST_CASE("SortingColumn: round-trip (descending, nulls_first)", "[thrift][ordering]") {
    SortingColumn sc;
    sc.column_idx  = 3;
    sc.descending  = true;
    sc.nulls_first = false;

    auto out = round_trip(sc);
    REQUIRE(out.column_idx == 3);
    REQUIRE(out.descending == true);
    REQUIRE(out.nulls_first == false);
}

TEST_CASE("SortingColumn: round-trip (ascending, nulls_first)", "[thrift][ordering]") {
    SortingColumn sc;
    sc.column_idx  = 0;
    sc.descending  = false;
    sc.nulls_first = true;

    auto out = round_trip(sc);
    REQUIRE(out.column_idx == 0);
    REQUIRE(out.descending == false);
    REQUIRE(out.nulls_first == true);
}

// =============================================================================
// § Bloom filter header types
// =============================================================================

TEST_CASE("BloomFilterAlgorithm: BLOCK round-trip", "[thrift][bloom]") {
    BloomFilterAlgorithm bfa;
    bfa.kind = BloomFilterAlgorithm::Kind::BLOCK;

    auto out = round_trip(bfa);
    REQUIRE(out.kind == BloomFilterAlgorithm::Kind::BLOCK);
}

TEST_CASE("BloomFilterHash: XXHASH round-trip", "[thrift][bloom]") {
    BloomFilterHash bfh;
    bfh.kind = BloomFilterHash::Kind::XXHASH;

    auto out = round_trip(bfh);
    REQUIRE(out.kind == BloomFilterHash::Kind::XXHASH);
}

TEST_CASE("BloomFilterCompression: UNCOMPRESSED round-trip", "[thrift][bloom]") {
    BloomFilterCompression bfc;
    bfc.kind = BloomFilterCompression::Kind::UNCOMPRESSED;

    auto out = round_trip(bfc);
    REQUIRE(out.kind == BloomFilterCompression::Kind::UNCOMPRESSED);
}

TEST_CASE("BloomFilterHeader: full round-trip", "[thrift][bloom]") {
    BloomFilterHeader bfhdr;
    bfhdr.num_bytes        = 1024;
    bfhdr.algorithm.kind   = BloomFilterAlgorithm::Kind::BLOCK;
    bfhdr.hash.kind        = BloomFilterHash::Kind::XXHASH;
    bfhdr.compression.kind = BloomFilterCompression::Kind::UNCOMPRESSED;

    auto out = round_trip(bfhdr);
    REQUIRE(out.num_bytes == 1024);
    REQUIRE(out.algorithm.kind == BloomFilterAlgorithm::Kind::BLOCK);
    REQUIRE(out.hash.kind == BloomFilterHash::Kind::XXHASH);
    REQUIRE(out.compression.kind == BloomFilterCompression::Kind::UNCOMPRESSED);
}

// =============================================================================
// § Encryption types
// =============================================================================

TEST_CASE("AesGcmV1: round-trip with all fields", "[thrift][encryption]") {
    AesGcmV1 v1;
    v1.aad_prefix         = {0x01, 0x02, 0x03};
    v1.aad_file_unique    = true;
    v1.supply_aad_prefix  = false;

    auto out = round_trip(v1);
    REQUIRE(out.aad_prefix.has_value());
    REQUIRE(*out.aad_prefix == std::vector<uint8_t>{0x01, 0x02, 0x03});
    REQUIRE(out.aad_file_unique.has_value());
    REQUIRE(*out.aad_file_unique == true);
    REQUIRE(out.supply_aad_prefix.has_value());
    REQUIRE(*out.supply_aad_prefix == false);
}

TEST_CASE("AesGcmV1: round-trip without optional fields", "[thrift][encryption]") {
    AesGcmV1 v1;
    // No fields set — empty struct
    auto out = round_trip(v1);
    REQUIRE_FALSE(out.aad_prefix.has_value());
    REQUIRE_FALSE(out.aad_file_unique.has_value());
    REQUIRE_FALSE(out.supply_aad_prefix.has_value());
}

TEST_CASE("EncryptionAlgorithm: AES_GCM_V1 round-trip", "[thrift][encryption]") {
    EncryptionAlgorithm ea;
    ea.kind = EncryptionAlgorithm::Kind::AES_GCM_V1;
    ea.aes_gcm_v1.emplace();
    ea.aes_gcm_v1->aad_file_unique = true;

    auto out = round_trip(ea);
    REQUIRE(out.kind == EncryptionAlgorithm::Kind::AES_GCM_V1);
    REQUIRE(out.aes_gcm_v1.has_value());
    REQUIRE(out.aes_gcm_v1->aad_file_unique.has_value());
    REQUIRE(*out.aes_gcm_v1->aad_file_unique == true);
}

TEST_CASE("EncryptionAlgorithm: AES_GCM_CTR_V1 round-trip", "[thrift][encryption]") {
    EncryptionAlgorithm ea;
    ea.kind = EncryptionAlgorithm::Kind::AES_GCM_CTR_V1;
    ea.aes_gcm_ctr_v1.emplace();
    ea.aes_gcm_ctr_v1->supply_aad_prefix = false;

    auto out = round_trip(ea);
    REQUIRE(out.kind == EncryptionAlgorithm::Kind::AES_GCM_CTR_V1);
    REQUIRE(out.aes_gcm_ctr_v1.has_value());
    REQUIRE(out.aes_gcm_ctr_v1->supply_aad_prefix.has_value());
    REQUIRE(*out.aes_gcm_ctr_v1->supply_aad_prefix == false);
}

TEST_CASE("FileCryptoMetaData: round-trip with key_metadata", "[thrift][encryption]") {
    FileCryptoMetaData fcmd;
    fcmd.encryption_algorithm.kind = EncryptionAlgorithm::Kind::AES_GCM_V1;
    fcmd.encryption_algorithm.aes_gcm_v1.emplace();
    fcmd.encryption_algorithm.aes_gcm_v1->aad_file_unique = true;
    fcmd.key_metadata = {0xAA, 0xBB, 0xCC};

    auto out = round_trip(fcmd);
    REQUIRE(out.encryption_algorithm.kind == EncryptionAlgorithm::Kind::AES_GCM_V1);
    REQUIRE(out.key_metadata.has_value());
    REQUIRE(*out.key_metadata == std::vector<uint8_t>{0xAA, 0xBB, 0xCC});
}

TEST_CASE("ColumnCryptoMetaData: COLUMN_KEY variant round-trip", "[thrift][encryption]") {
    ColumnCryptoMetaData ccmd;
    ccmd.kind = ColumnCryptoMetaData::Kind::COLUMN_KEY;
    ccmd.column_key.emplace();
    ccmd.column_key->path_in_schema = {"a", "b", "c"};
    ccmd.column_key->key_metadata   = {0x11, 0x22};

    auto out = round_trip(ccmd);
    REQUIRE(out.kind == ColumnCryptoMetaData::Kind::COLUMN_KEY);
    REQUIRE(out.column_key.has_value());
    REQUIRE(out.column_key->path_in_schema.size() == 3);
    REQUIRE(out.column_key->path_in_schema[0] == "a");
    REQUIRE(out.column_key->path_in_schema[2] == "c");
    REQUIRE(out.column_key->key_metadata.has_value());
    REQUIRE(out.column_key->key_metadata->size() == 2);
    REQUIRE((*out.column_key->key_metadata)[0] == 0x11);
}

// =============================================================================
// § expected<void> error propagation
// =============================================================================

TEST_CASE("expected<void>: nested Statistics failure propagates from DataPageHeader", "[thrift][error_propagation]") {
    // Build a DataPageHeader where the nested Statistics has a type error.
    // Manually encode: valid fields 1-4, then field 5 (statistics) with BINARY for null_count.
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(1, compact_type::I32); enc.write_i32(100); // num_values
    enc.write_field(2, compact_type::I32); enc.write_i32(0);   // encoding=PLAIN
    enc.write_field(3, compact_type::I32); enc.write_i32(3);   // def_level_enc=RLE
    enc.write_field(4, compact_type::I32); enc.write_i32(3);   // rep_level_enc=RLE
    // field 5 = statistics as a struct with a type error inside
    enc.write_field(5, compact_type::STRUCT);
    enc.begin_struct();
    enc.write_field(3, compact_type::I32); // null_count: expects I64, got I32
    enc.write_i32(9);
    enc.write_stop();
    enc.end_struct();
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    DataPageHeader dph;
    auto r = dph.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("expected<void>: error code is THRIFT_DECODE_ERROR", "[thrift][error_propagation]") {
    // Trigger a required-field error and verify the error code.
    CompactEncoder enc;
    enc.begin_struct();
    enc.write_field(2, compact_type::BINARY);
    enc.write_string("value-only");
    enc.write_stop();
    enc.end_struct();

    const auto& buf = enc.data();
    CompactDecoder dec(buf.data(), buf.size());
    KeyValue kv;
    auto r = kv.deserialize(dec);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::THRIFT_DECODE_ERROR);
}

// =============================================================================
// § RowGroup with sorting_columns (field 4)
// =============================================================================

TEST_CASE("RowGroup: sorting_columns field 4 round-trip", "[thrift][rowgroup]") {
    RowGroup rg;
    // Minimal valid RowGroup: need at least one ColumnChunk to survive serialize.
    // We test the sorting_columns path by constructing a RowGroup with them.
    rg.total_byte_size = 4096;
    rg.num_rows        = 100;
    SortingColumn sc1; sc1.column_idx = 0; sc1.descending = false; sc1.nulls_first = true;
    SortingColumn sc2; sc2.column_idx = 1; sc2.descending = true;  sc2.nulls_first = false;
    rg.sorting_columns = {sc1, sc2};

    // Serialize and deserialize via encoder/decoder.
    CompactEncoder enc;
    rg.serialize(enc);
    const auto& buf = enc.data();

    RowGroup out;
    CompactDecoder dec(buf.data(), buf.size());
    auto r = out.deserialize(dec);
    REQUIRE(r.has_value());
    REQUIRE(out.total_byte_size == 4096);
    REQUIRE(out.num_rows == 100);
    REQUIRE(out.sorting_columns.size() == 2);
    REQUIRE(out.sorting_columns[0].column_idx == 0);
    REQUIRE(out.sorting_columns[0].descending == false);
    REQUIRE(out.sorting_columns[1].column_idx == 1);
    REQUIRE(out.sorting_columns[1].descending == true);
}

// =============================================================================
// § FileMetaData with column_orders (field 7)
// =============================================================================

TEST_CASE("FileMetaData: column_orders field 7 round-trip", "[thrift][filemetadata]") {
    FileMetaData fm;
    fm.version             = 2;
    fm.num_rows            = 0;
    // Minimal schema (root group node only)
    SchemaElement root;
    root.name = "schema";
    fm.schema.push_back(root);
    // Two column orders
    ColumnOrder co1; co1.kind = ColumnOrder::Kind::TYPE_ORDER;
    ColumnOrder co2; co2.kind = ColumnOrder::Kind::TYPE_ORDER;
    fm.column_orders.emplace();
    fm.column_orders->push_back(co1);
    fm.column_orders->push_back(co2);

    CompactEncoder enc;
    fm.serialize(enc);
    const auto& buf = enc.data();

    FileMetaData out;
    CompactDecoder dec(buf.data(), buf.size());
    auto r = out.deserialize(dec);
    REQUIRE(r.has_value());
    REQUIRE(out.version == 2);
    REQUIRE(out.column_orders.has_value());
    REQUIRE(out.column_orders->size() == 2);
    REQUIRE((*out.column_orders)[0].kind == ColumnOrder::Kind::TYPE_ORDER);
}
