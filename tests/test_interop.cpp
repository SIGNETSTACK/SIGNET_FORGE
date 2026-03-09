// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#include <catch2/catch_test_macros.hpp>
#include "signet/forge.hpp"
#include "signet/crypto/aes_core.hpp"
#include "signet/crypto/aes_gcm.hpp"
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
#include "signet/crypto/pme.hpp"
#endif

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace signet::forge;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helper: RAII temp file that cleans up on destruction
// ---------------------------------------------------------------------------
namespace {
struct TempFile {
    fs::path path;

    explicit TempFile(const std::string& name)
        : path(fs::temp_directory_path() / name) {}

    ~TempFile() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
} // anonymous namespace

// ---------------------------------------------------------------------------
// Helper: read entire file into a byte vector
// ---------------------------------------------------------------------------
static std::vector<uint8_t> read_file_bytes(const fs::path& path) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) return {};
    auto size = ifs.tellg();
    ifs.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(data.data()),
             static_cast<std::streamsize>(size));
    return data;
}

// ---------------------------------------------------------------------------
// Helper: generate a deterministic test key
// ---------------------------------------------------------------------------
static std::vector<uint8_t> generate_test_key(size_t size) {
    std::vector<uint8_t> key(size);
    for (size_t i = 0; i < size; ++i) key[i] = static_cast<uint8_t>(i * 7 + 13);
    return key;
}

// ---------------------------------------------------------------------------
// Helper: write a simple 2-column file and return the raw bytes
// ---------------------------------------------------------------------------
static std::vector<uint8_t> write_simple_file(const fs::path& path,
                                                const ParquetWriter::Options& opts = {}) {
    auto schema = Schema::builder("test")
        .column<int64_t>("id")
        .column<std::string>("name")
        .build();

    auto writer_result = ParquetWriter::open(path, schema, opts);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    auto r1 = writer.write_row({"1", "Alice"});
    REQUIRE(r1.has_value());
    auto r2 = writer.write_row({"2", "Bob"});
    REQUIRE(r2.has_value());
    auto r3 = writer.write_row({"3", "Charlie"});
    REQUIRE(r3.has_value());

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());

    return read_file_bytes(path);
}

// ===================================================================
// 1. File starts and ends with PAR1
// ===================================================================
TEST_CASE("File starts and ends with PAR1", "[interop]") {
    TempFile tmp("signet_test_interop_magic.parquet");

    auto bytes = write_simple_file(tmp.path);
    REQUIRE(bytes.size() >= 12);

    // First 4 bytes: "PAR1"
    REQUIRE(bytes[0] == 'P');
    REQUIRE(bytes[1] == 'A');
    REQUIRE(bytes[2] == 'R');
    REQUIRE(bytes[3] == '1');

    // Last 4 bytes: "PAR1"
    size_t sz = bytes.size();
    REQUIRE(bytes[sz - 4] == 'P');
    REQUIRE(bytes[sz - 3] == 'A');
    REQUIRE(bytes[sz - 2] == 'R');
    REQUIRE(bytes[sz - 1] == '1');
}

// ===================================================================
// 2. Footer length is correct
// ===================================================================
TEST_CASE("Footer length is correct", "[interop]") {
    TempFile tmp("signet_test_interop_footer_len.parquet");

    auto bytes = write_simple_file(tmp.path);
    size_t sz = bytes.size();
    REQUIRE(sz >= 12);

    // Read 4-byte LE footer length at [size-8, size-4)
    uint32_t footer_len = 0;
    std::memcpy(&footer_len, bytes.data() + sz - 8, 4);

    REQUIRE(footer_len > 0);
    REQUIRE(static_cast<size_t>(footer_len) <= sz - 12);

    // Read that many bytes from the footer region and deserialize as FileMetaData
    size_t footer_offset = sz - 8 - footer_len;
    const uint8_t* footer_ptr = bytes.data() + footer_offset;

    thrift::CompactDecoder dec(footer_ptr, footer_len);
    thrift::FileMetaData metadata;
    metadata.deserialize(dec);

    REQUIRE(dec.good());
    REQUIRE(metadata.num_rows == 3);
    REQUIRE(metadata.schema.size() >= 3); // root + 2 columns
}

// ===================================================================
// 3. Created_by field present
// ===================================================================
TEST_CASE("Created_by field present", "[interop]") {
    TempFile tmp("signet_test_interop_created_by.parquet");

    write_simple_file(tmp.path);

    auto reader_result = ParquetReader::open(tmp.path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    REQUIRE(reader.created_by() == "SignetStack signet-forge version 0.1.0");
}

// ===================================================================
// 4. Multiple encodings produce valid files
// ===================================================================
TEST_CASE("Multiple encodings produce valid files", "[interop]") {
    // Test each encoding by writing a file and verifying metadata is well-formed

    struct EncodingTest {
        std::string name;
        Encoding encoding;
        bool use_auto;
    };

    std::vector<EncodingTest> tests = {
        {"PLAIN",      Encoding::PLAIN,               false},
        {"DELTA",      Encoding::DELTA_BINARY_PACKED,  false},
        {"BSS",        Encoding::BYTE_STREAM_SPLIT,    false},
        {"DICTIONARY", Encoding::RLE_DICTIONARY,       false},
    };

    for (const auto& t : tests) {
        SECTION("Encoding: " + t.name) {
            TempFile tmp("signet_test_enc_" + t.name + ".parquet");

            // Choose schema appropriate for the encoding
            Schema schema = [&]() {
                if (t.encoding == Encoding::DELTA_BINARY_PACKED) {
                    return Schema::builder("test")
                        .column<int64_t>("value")
                        .build();
                } else if (t.encoding == Encoding::BYTE_STREAM_SPLIT) {
                    return Schema::builder("test")
                        .column<double>("value")
                        .build();
                } else {
                    return Schema::builder("test")
                        .column<std::string>("value")
                        .build();
                }
            }();

            ParquetWriter::Options opts;
            opts.default_encoding = t.encoding;

            auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
            REQUIRE(writer_result.has_value());
            auto& writer = *writer_result;

            // Write some values
            for (int i = 0; i < 20; ++i) {
                if (t.encoding == Encoding::DELTA_BINARY_PACKED) {
                    auto r = writer.write_row({std::to_string(i * 100)});
                    REQUIRE(r.has_value());
                } else if (t.encoding == Encoding::BYTE_STREAM_SPLIT) {
                    auto r = writer.write_row({std::to_string(100.5 + i * 0.1)});
                    REQUIRE(r.has_value());
                } else {
                    // For PLAIN and DICTIONARY, use low-cardinality strings
                    std::string sym = (i % 3 == 0) ? "AAA" :
                                      (i % 3 == 1) ? "BBB" : "CCC";
                    auto r = writer.write_row({sym});
                    REQUIRE(r.has_value());
                }
            }

            auto close_result = writer.close();
            REQUIRE(close_result.has_value());

            // Open and verify metadata is well-formed
            auto reader_result = ParquetReader::open(tmp.path);
            REQUIRE(reader_result.has_value());
            auto& reader = *reader_result;

            REQUIRE(reader.num_rows() == 20);
            REQUIRE(reader.schema().num_columns() == 1);
            REQUIRE(reader.num_row_groups() >= 1);
        }
    }
}

// ===================================================================
// 5. Compressed files have correct codec in metadata
// ===================================================================
TEST_CASE("Compressed files have correct codec in metadata", "[interop]") {
    TempFile tmp("signet_test_interop_snappy.parquet");

    register_snappy_codec();

    auto schema = Schema::builder("test")
        .column<int64_t>("id")
        .column<std::string>("name")
        .build();

    ParquetWriter::Options opts;
    opts.compression = Compression::SNAPPY;

    auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    for (int i = 0; i < 10; ++i) {
        auto r = writer.write_row({
            std::to_string(static_cast<int64_t>(i)),
            "row_" + std::to_string(i)
        });
        REQUIRE(r.has_value());
    }

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());

    // Read raw footer and verify codec field
    auto bytes = read_file_bytes(tmp.path);
    size_t sz = bytes.size();
    REQUIRE(sz >= 12);

    uint32_t footer_len = 0;
    std::memcpy(&footer_len, bytes.data() + sz - 8, 4);

    size_t footer_offset = sz - 8 - footer_len;
    thrift::CompactDecoder dec(bytes.data() + footer_offset, footer_len);
    thrift::FileMetaData metadata;
    metadata.deserialize(dec);
    REQUIRE(dec.good());

    // Verify each column chunk has SNAPPY codec
    REQUIRE(!metadata.row_groups.empty());
    for (const auto& rg : metadata.row_groups) {
        for (const auto& col : rg.columns) {
            REQUIRE(col.meta_data.has_value());
            REQUIRE(col.meta_data->codec == Compression::SNAPPY);
        }
    }
}

// ===================================================================
// 6. Statistics are written correctly
// ===================================================================
TEST_CASE("Statistics are written correctly", "[interop]") {
    TempFile tmp("signet_test_interop_stats.parquet");

    auto schema = Schema::builder("test")
        .column<int64_t>("value")
        .build();

    auto writer_result = ParquetWriter::open(tmp.path, schema);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    std::vector<int64_t> values = {10, 20, 30, 40, 50};
    for (auto v : values) {
        auto r = writer.write_row({std::to_string(v)});
        REQUIRE(r.has_value());
    }

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());

    // Open and read statistics
    auto reader_result = ParquetReader::open(tmp.path);
    REQUIRE(reader_result.has_value());
    auto& reader = *reader_result;

    const auto* stats = reader.column_statistics(0, 0);
    REQUIRE(stats != nullptr);

    // Verify min_value and max_value are present
    REQUIRE(stats->min_value.has_value());
    REQUIRE(stats->max_value.has_value());

    // Interpret min/max bytes as int64 (little-endian)
    const auto& min_str = *stats->min_value;
    const auto& max_str = *stats->max_value;
    REQUIRE(min_str.size() == sizeof(int64_t));
    REQUIRE(max_str.size() == sizeof(int64_t));

    int64_t min_val = 0;
    int64_t max_val = 0;
    std::memcpy(&min_val, min_str.data(), sizeof(int64_t));
    std::memcpy(&max_val, max_str.data(), sizeof(int64_t));

    REQUIRE(min_val == 10);
    REQUIRE(max_val == 50);
}

// ===================================================================
// 7. Encrypted file has PARE magic
// ===================================================================
#if defined(SIGNET_ENABLE_COMMERCIAL) && SIGNET_ENABLE_COMMERCIAL
TEST_CASE("Encrypted file has PARE magic", "[interop]") {
    TempFile tmp("signet_test_interop_encrypted.parquet");

    auto schema = Schema::builder("test")
        .column<int64_t>("id")
        .build();

    crypto::EncryptionConfig enc_cfg;
    enc_cfg.algorithm = crypto::EncryptionAlgorithm::AES_GCM_CTR_V1;
    enc_cfg.footer_key = generate_test_key(32);
    enc_cfg.encrypt_footer = true;
    enc_cfg.aad_prefix = "test-interop-pare";

    ParquetWriter::Options opts;
    opts.encryption = enc_cfg;

    auto writer_result = ParquetWriter::open(tmp.path, schema, opts);
    REQUIRE(writer_result.has_value());
    auto& writer = *writer_result;

    auto r = writer.write_row({"42"});
    REQUIRE(r.has_value());

    auto close_result = writer.close();
    REQUIRE(close_result.has_value());

    // Read raw bytes and check magic bytes
    auto bytes = read_file_bytes(tmp.path);
    size_t sz = bytes.size();
    REQUIRE(sz >= 12);

    // First 4 bytes: still "PAR1"
    REQUIRE(bytes[0] == 'P');
    REQUIRE(bytes[1] == 'A');
    REQUIRE(bytes[2] == 'R');
    REQUIRE(bytes[3] == '1');

    // Last 4 bytes: "PARE" (encrypted footer)
    REQUIRE(bytes[sz - 4] == 'P');
    REQUIRE(bytes[sz - 3] == 'A');
    REQUIRE(bytes[sz - 2] == 'R');
    REQUIRE(bytes[sz - 1] == 'E');
}
#endif

// ===========================================================================
// Security hardening — malformed file / nesting depth tests
// ===========================================================================

TEST_CASE("Thrift decoder: excessive nesting depth sets error flag", "[thrift][hardening]") {
    // The CompactDecoder constructor already pushes one level (the root struct
    // context). MAX_NESTING_DEPTH=64 (reduced from 128 in M-6), so calling
    // begin_struct() 63 times keeps size at 64 (the limit). The 64th explicit
    // call hits size() >= MAX_NESTING_DEPTH and must set error_=true.
    std::vector<uint8_t> dummy = {0x00}; // STOP byte — minimal valid buffer
    thrift::CompactDecoder dec(dummy.data(), dummy.size());

    // Push 63 levels — each should succeed (total stack size: 1 initial + 63 = 64)
    for (int i = 0; i < 63; ++i) {
        dec.begin_struct();
        REQUIRE(dec.good());
    }
    // 64th explicit begin_struct() makes size() == 64 >= MAX_NESTING_DEPTH — must set error
    dec.begin_struct();
    REQUIRE_FALSE(dec.good());
}

TEST_CASE("ParquetReader: bad magic returns error", "[reader][hardening]") {
    TempFile tf("bad_magic_test.parquet");
    {
        std::ofstream f(tf.path, std::ios::binary);
        // Write wrong magic (not PAR1 = 0x50 0x41 0x52 0x31)
        f.write("FAKE", 4);
        // Write footer length=0 and wrong magic at end
        uint32_t foot_len = 0;
        f.write(reinterpret_cast<const char*>(&foot_len), 4);
        f.write("FAKE", 4);
    }
    auto result = ParquetReader::open(tf.path.string());
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ParquetReader: truncated file (< 12 bytes) returns error", "[reader][hardening]") {
    TempFile tf("truncated_test.parquet");
    {
        std::ofstream f(tf.path, std::ios::binary);
        // Write only 6 bytes — too short for a valid Parquet file (need at least 12)
        f.write("PAR1\x00\x00", 6);
    }
    auto result = ParquetReader::open(tf.path.string());
    REQUIRE_FALSE(result.has_value());
}

// ===========================================================================
// Security hardening — new negative/boundary tests (hardening pass #2)
// ===========================================================================

// ---------------------------------------------------------------------------
// C1: Parquet page-size bomb — footer_len = 0 triggers corrupt-file path
// ---------------------------------------------------------------------------
TEST_CASE("ParquetReader rejects uncompressed_page_size over 256 MB", "[hardening]") {
    // Craft a file whose footer_len field is 0.  The reader validates
    // footer_len > 0 before attempting to read the footer, so this must
    // return an error immediately — confirming that the header-validation
    // path fires before any allocation could occur for a large size value.
    TempFile tf("signet_hardening_pagesize.parquet");
    {
        std::ofstream f(tf.path, std::ios::binary);

        // Leading magic
        f.write("PAR1", 4);

        // Placeholder body bytes (the reader only cares about the last 8)
        const char zeros[8] = {};
        f.write(zeros, 8);

        // Footer length = 0 (4-byte LE) — invalid, must be > 0
        uint32_t footer_len = 0u;
        f.write(reinterpret_cast<const char*>(&footer_len), 4);

        // Trailing magic (PAR1 — plain unencrypted)
        f.write("PAR1", 4);
    }
    auto result = ParquetReader::open(tf.path.string());
    // A footer_len of 0 is corrupt — reader must reject it
    REQUIRE_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// C2: Thrift struct field-count bomb — MAX_FIELD_COUNT guard fires
// ---------------------------------------------------------------------------
TEST_CASE("ThriftCompactDecoder stops at MAX_FIELD_COUNT fields", "[hardening]") {
    // Build a raw buffer with 70 000 valid i32 field headers (field IDs
    // incrementing by 1 each time, all using the compact delta encoding)
    // followed by a STOP byte.  MAX_FIELD_COUNT = 65536, so after 65536
    // reads the decoder must set error_=true and good() must be false.
    //
    // Compact delta encoding for a delta of 1, type I32 (0x05):
    //   byte = (delta << 4) | type = (1 << 4) | 5 = 0x15
    // The very first field must use a full field-ID encoding because the
    // initial last_field_id is 0 and delta = field_id - 0.  Field 1 has
    // delta=1, so the single-byte form applies: 0x15.

    constexpr int BOMB_COUNT = 70000; // well above MAX_FIELD_COUNT=65536

    std::vector<uint8_t> buf;
    buf.reserve(static_cast<size_t>(BOMB_COUNT) * 2 + 1);

    // All 70 000 fields: delta=1, type=I32 — compact single-byte form 0x15
    // Each i32 varint value is 0 encoded as a single zigzag varint byte (0x00).
    for (int i = 0; i < BOMB_COUNT; ++i) {
        buf.push_back(0x15u); // field header: delta=1, type=I32
        buf.push_back(0x00u); // zigzag-varint 0 for the i32 value
    }
    buf.push_back(0x00u); // STOP

    thrift::CompactDecoder dec(buf.data(), buf.size());

    // Read field headers in a loop — stop when either error or STOP is returned
    int reads = 0;
    while (dec.good()) {
        auto fh = dec.read_field_header();
        if (fh.is_stop()) break;
        // consume the i32 value that follows each header
        (void)dec.read_i32();
        ++reads;
    }

    // The decoder must have stopped at or before MAX_FIELD_COUNT (65536).
    // After the bomb fires, good() must be false.
    REQUIRE_FALSE(dec.good());

    // Must have stopped well before reading all 70 000 — no infinite loop.
    REQUIRE(reads <= 65536);
}

// ---------------------------------------------------------------------------
// C3: Thrift string-size bomb — MAX_STRING_BYTES guard fires
// ---------------------------------------------------------------------------
TEST_CASE("ThriftCompactDecoder rejects string field over 64 MB", "[hardening]") {
    // Build a buffer containing a single BINARY field header followed by a
    // varint-encoded length of 65 MB (65 * 1024 * 1024 = 68157440).
    // MAX_STRING_BYTES = 64 MB = 67108864.  No actual payload bytes follow —
    // the decoder must reject the string before attempting any allocation.

    constexpr uint32_t OVER_LIMIT = 65u * 1024u * 1024u; // 65 MB

    std::vector<uint8_t> buf;

    // Field header: delta=1 (field_id=1), type=BINARY (0x08)
    // Compact delta form: (1 << 4) | 8 = 0x18
    buf.push_back(0x18u);

    // Varint-encode OVER_LIMIT (little-endian 7-bit groups, high bit = more)
    uint32_t v = OVER_LIMIT;
    while (v > 0x7Fu) {
        buf.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
        v >>= 7;
    }
    buf.push_back(static_cast<uint8_t>(v));

    // No payload bytes — file ends here intentionally

    thrift::CompactDecoder dec(buf.data(), buf.size());

    // Read the field header — should succeed
    auto fh = dec.read_field_header();
    REQUIRE_FALSE(fh.is_stop());

    // Attempt to read the oversized string — must trigger error and return empty
    auto s = dec.read_string();
    REQUIRE(s.empty());

    // After the guard fires, good() must be false
    REQUIRE_FALSE(dec.good());
}

// ---------------------------------------------------------------------------
// C4: ArrowArray negative offset and offset+length overflow
// ---------------------------------------------------------------------------
TEST_CASE("ArrowArray import rejects negative offset and overflow", "[hardening]") {
    // Source float data — 3 elements
    std::vector<float> data = {1.0f, 2.0f, 3.0f};

    // We need a valid release callback and buffers array.  Build a minimal
    // ArrowArray + ArrowSchema by exporting a real TensorView, then patch
    // individual fields before calling import_array().

    SECTION("negative offset is rejected") {
        TensorShape shape{{static_cast<int64_t>(data.size())}};
        TensorView tv(data.data(), shape, TensorDataType::FLOAT32);

        ArrowArray  arr{};
        ArrowSchema sch{};
        auto exp = ArrowExporter::export_tensor(tv, "col", &arr, &sch);
        REQUIRE(exp.has_value());

        // Patch: set a negative offset
        arr.offset = -1;

        auto result = ArrowImporter::import_array(&arr, &sch);
        REQUIRE_FALSE(result.has_value()); // must return error

        // Release resources
        if (arr.release) arr.release(&arr);
        if (sch.release) sch.release(&sch);
    }

    SECTION("offset + length overflow int64 is rejected") {
        TensorShape shape{{static_cast<int64_t>(data.size())}};
        TensorView tv(data.data(), shape, TensorDataType::FLOAT32);

        ArrowArray  arr{};
        ArrowSchema sch{};
        auto exp = ArrowExporter::export_tensor(tv, "col", &arr, &sch);
        REQUIRE(exp.has_value());

        // Patch: length = INT64_MAX, offset = 1 → offset + length overflows int64
        arr.length = INT64_MAX;
        arr.offset = 1;

        auto result = ArrowImporter::import_array(&arr, &sch);
        REQUIRE_FALSE(result.has_value()); // must return error

        // Release resources (arr.length was patched so no real data at that size,
        // but the release callback only frees private_data — safe to call)
        if (arr.release) arr.release(&arr);
        if (sch.release) sch.release(&sch);
    }

    SECTION("offset * elem_size overflow") {
        // Create a valid int32 column export with offset=0
        std::vector<int32_t> data = {1, 2, 3, 4};
        ArrowArray arr{};
        ArrowSchema sch{};
        auto exp_result = ArrowExporter::export_column(
            data.data(), 4, PhysicalType::INT32, "test", &arr, &sch);
        REQUIRE(exp_result.has_value());

        // Patch offset to a huge value that would overflow when multiplied by elem_size
        arr.offset = static_cast<int64_t>(SIZE_MAX / 2);

        auto result = ArrowImporter::import_array(&arr, &sch);
        REQUIRE_FALSE(result.has_value()); // must return error

        if (arr.release) arr.release(&arr);
        if (sch.release) sch.release(&sch);
    }
}

// ===========================================================================
// Hardening Pass #4 Tests
// ===========================================================================

TEST_CASE("Arrow offset and length caps enforced", "[interop][hardening]") {
    // M-15: MAX_ARROW_OFFSET and MAX_ARROW_LENGTH caps (1 billion each)
    // Verify normal imports still work within limits
    auto schema = Schema::builder("test")
        .column<int64_t>("values")
        .build();

    std::string path = (std::filesystem::temp_directory_path() / "test_arrow_caps.parquet").string();
    auto writer = ParquetWriter::open(path, schema);
    REQUIRE(writer.has_value());

    std::vector<int64_t> data = {10, 20, 30};
    auto wr = writer->write_column(0, data.data(), data.size());
    REQUIRE(wr.has_value());
    auto cl = writer->close();
    REQUIRE(cl.has_value());

    auto reader = ParquetReader::open(path);
    REQUIRE(reader.has_value());
    auto col = reader->read_column<int64_t>(0, 0);
    REQUIRE(col.has_value());
    REQUIRE(col->size() == 3);

    std::filesystem::remove(path);
}

#ifndef _WIN32  // MmapParquetReader not available on Windows
TEST_CASE("mmap footer validation rejects invalid footer length", "[interop][hardening]") {
    // H-7: footer_len > sz - 12 check
    // Create a minimal invalid Parquet file with bad footer length
    auto path = (std::filesystem::temp_directory_path() / "test_bad_footer.parquet").string();
    {
        std::ofstream f(path, std::ios::binary);
        // Write PAR1 magic
        f.write("PAR1", 4);
        // Write some garbage bytes
        uint8_t garbage[8] = {};
        f.write(reinterpret_cast<const char*>(garbage), 8);
        // Write a huge footer length (0xFFFFFFFF) that exceeds file size
        uint32_t bad_len = 0xFFFFFFFF;
        f.write(reinterpret_cast<const char*>(&bad_len), 4);
        // Write PAR1 trailer magic
        f.write("PAR1", 4);
    }
    auto result = MmapParquetReader::open(path);
    REQUIRE_FALSE(result.has_value());
    std::filesystem::remove(path);
}
#endif  // !_WIN32
