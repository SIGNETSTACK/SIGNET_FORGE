// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
//
// Emscripten embind bindings for Signet Forge — browser-side Parquet preview.
// Build: emcmake cmake --preset wasm && cmake --build build-wasm

#include <emscripten.h>
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <string>
#include <vector>

#include "signet/forge.hpp"

namespace em = emscripten;
using namespace signet::forge;

// ---------------------------------------------------------------------------
// MEMFS helpers — use EM_ASM which runs inside the module closure where
// the FS object is available (em::val::global("FS") doesn't work with
// -sMODULARIZE since FS is not a browser global).
// ---------------------------------------------------------------------------

static constexpr unsigned MEMFS_MAX_FILE_SIZE = 256u * 1024u * 1024u; // 256 MB

static void writeFileToMemfs(const std::string& path, const em::val& arrayBuffer) {
    auto view = em::val::global("Uint8Array").new_(arrayBuffer);
    auto len = view["length"].as<unsigned>();
    if (len > MEMFS_MAX_FILE_SIZE) {
        emscripten_log(EM_LOG_ERROR, "writeFileToMemfs: file too large (%u bytes, max %u)", len, MEMFS_MAX_FILE_SIZE);
        return;
    }
    std::vector<uint8_t> buf(len);
    auto memView = em::val(em::typed_memory_view(len, buf.data()));
    memView.call<void>("set", view);
    EM_ASM({
        FS.writeFile(UTF8ToString($0), HEAPU8.subarray($1, $1 + $2));
    }, path.c_str(), buf.data(), static_cast<int>(len));
}

static em::val readFileFromMemfs(const std::string& path) {
    // Caller should use Module.FS.readFile() from JS instead for large files.
    // This helper is provided for API symmetry.
    em::val fs = em::val::module_property("FS");
    return fs.call<em::val>("readFile", path);
}

// ---------------------------------------------------------------------------
// Hex and minimal JSON helpers (for encryption key input)
// ---------------------------------------------------------------------------

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    if (hex.size() % 2 != 0) return bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto hi = hex[i], lo = hex[i + 1];
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
            return -1;
        };
        int h = nibble(hi), l = nibble(lo);
        if (h < 0 || l < 0) return {};
        bytes.push_back(static_cast<uint8_t>((h << 4) | l));
    }
    return bytes;
}

// Minimal flat JSON object parser: {"key":"value", ...}
static std::vector<std::pair<std::string, std::string>>
parseColumnKeys(const std::string& json) {
    std::vector<std::pair<std::string, std::string>> result;
    size_t i = json.find('{');
    if (i == std::string::npos) return result;
    ++i;
    auto skipWs = [&]() { while (i < json.size() && json[i] <= ' ') ++i; };
    auto readStr = [&]() -> std::string {
        skipWs();
        if (i >= json.size() || json[i] != '"') return {};
        ++i;
        std::string s;
        while (i < json.size() && json[i] != '"') s += json[i++];
        if (i < json.size()) ++i; // skip closing quote
        return s;
    };
    while (i < json.size()) {
        skipWs();
        if (json[i] == '}') break;
        auto key = readStr();
        skipWs();
        if (i < json.size() && json[i] == ':') ++i;
        auto val = readStr();
        if (!key.empty()) result.emplace_back(std::move(key), std::move(val));
        skipWs();
        if (i < json.size() && json[i] == ',') ++i;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

static std::string version() {
    return SIGNET_CREATED_BY;
}

// ---------------------------------------------------------------------------
// SchemaBuilder wrapper — chain-style API for JS
// ---------------------------------------------------------------------------

class WasmSchemaBuilder {
    SchemaBuilder builder_;
public:
    explicit WasmSchemaBuilder(const std::string& name) : builder_(name) {}

    WasmSchemaBuilder& addBool(const std::string& col)       { builder_.column<bool>(col);        return *this; }
    WasmSchemaBuilder& addInt32(const std::string& col)      { builder_.column<int32_t>(col);     return *this; }
    WasmSchemaBuilder& addInt64(const std::string& col)      { builder_.column<int64_t>(col);     return *this; }
    WasmSchemaBuilder& addFloat(const std::string& col)      { builder_.column<float>(col);       return *this; }
    WasmSchemaBuilder& addDouble(const std::string& col)     { builder_.column<double>(col);      return *this; }
    WasmSchemaBuilder& addString(const std::string& col)     { builder_.column<std::string>(col); return *this; }

    Schema build() { return builder_.build(); }
};

// ---------------------------------------------------------------------------
// Schema accessors
// ---------------------------------------------------------------------------

static size_t schemaNumColumns(const Schema& s) { return s.num_columns(); }
static std::string schemaColumnName(const Schema& s, size_t i) {
    if (i >= s.num_columns()) return "";
    return s.column(i).name;
}
static int schemaColumnPhysicalType(const Schema& s, size_t i) {
    if (i >= s.num_columns()) return -1;
    return static_cast<int>(s.column(i).physical_type);
}
static std::string schemaName(const Schema& s) { return s.name(); }

static std::string physicalTypeName(int pt) {
    switch (static_cast<PhysicalType>(pt)) {
        case PhysicalType::BOOLEAN:              return "BOOLEAN";
        case PhysicalType::INT32:                return "INT32";
        case PhysicalType::INT64:                return "INT64";
        case PhysicalType::INT96:                return "INT96";
        case PhysicalType::FLOAT:                return "FLOAT";
        case PhysicalType::DOUBLE:               return "DOUBLE";
        case PhysicalType::BYTE_ARRAY:           return "BYTE_ARRAY";
        case PhysicalType::FIXED_LEN_BYTE_ARRAY: return "FIXED_LEN_BYTE_ARRAY";
        default:                                  return "UNKNOWN";
    }
}

// ---------------------------------------------------------------------------
// WriterOptions wrapper
// ---------------------------------------------------------------------------

class WasmWriterOptions {
public:
    WriterOptions opts;
    WasmWriterOptions() = default;
    void setRowGroupSize(int64_t n) { opts.row_group_size = n; }
    int64_t getRowGroupSize() const { return opts.row_group_size; }
};

// ---------------------------------------------------------------------------
// ParquetWriter wrapper
// ---------------------------------------------------------------------------

class WasmParquetWriter {
    std::unique_ptr<ParquetWriter> writer_;
public:
    WasmParquetWriter() = default;

    bool open(const std::string& path, const Schema& schema, const WasmWriterOptions& opts) {
        auto result = ParquetWriter::open(path, schema, opts.opts);
        if (!result.has_value()) return false;
        writer_ = std::make_unique<ParquetWriter>(std::move(*result));
        return true;
    }

    bool writeColumnBool(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<bool> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<bool>();
        // bool write_column needs a raw bool array
        std::vector<uint8_t> raw(len);
        for (unsigned i = 0; i < len; ++i) raw[i] = buf[i] ? 1 : 0;
        return writer_->write_column<bool>(col, reinterpret_cast<const bool*>(raw.data()), len).has_value();
    }

    bool writeColumnInt32(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<int32_t> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<int32_t>();
        return writer_->write_column<int32_t>(col, buf.data(), len).has_value();
    }

    bool writeColumnInt64(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<int64_t> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<int64_t>();
        return writer_->write_column<int64_t>(col, buf.data(), len).has_value();
    }

    bool writeColumnFloat(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<float> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<float>();
        return writer_->write_column<float>(col, buf.data(), len).has_value();
    }

    bool writeColumnDouble(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<double> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<double>();
        return writer_->write_column<double>(col, buf.data(), len).has_value();
    }

    bool writeColumnString(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<std::string> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<std::string>();
        return writer_->write_column<std::string>(col, buf.data(), len).has_value();
    }

    bool flushRowGroup() {
        if (!writer_) return false;
        return writer_->flush_row_group().has_value();
    }

    bool close() {
        if (!writer_) return false;
        return writer_->close().has_value();
    }

    int64_t rowsWritten() const {
        return writer_ ? writer_->rows_written() : 0;
    }

    bool isOpen() const {
        return writer_ && writer_->is_open();
    }
};

// ---------------------------------------------------------------------------
// ParquetReader wrapper
// ---------------------------------------------------------------------------

class WasmParquetReader {
    std::unique_ptr<ParquetReader> reader_;
public:
    WasmParquetReader() = default;

    bool open(const std::string& path) {
        auto result = ParquetReader::open(path);
        if (!result.has_value()) return false;
        reader_ = std::make_unique<ParquetReader>(std::move(*result));
        return true;
    }

#if SIGNET_ENABLE_COMMERCIAL
    // Open an encrypted Parquet file.
    // footerKeyHex:    64-char hex string → 32-byte AES-256 footer key.
    // columnKeyHex:    64-char hex default column key (applied to all columns), or empty.
    // aadPrefix:       AAD prefix string bound into GCM auth tags, or empty.
    // columnKeysJson:  Per-column keys as {"col_name":"hex64",...}, or empty.
    bool openEncrypted(const std::string& path,
                       const std::string& footerKeyHex,
                       const std::string& columnKeyHex,
                       const std::string& aadPrefix,
                       const std::string& columnKeysJson) {
        crypto::EncryptionConfig cfg;
        cfg.footer_key = hexToBytes(footerKeyHex);
        if (cfg.footer_key.size() != 32) return false;

        if (!columnKeyHex.empty()) {
            cfg.default_column_key = hexToBytes(columnKeyHex);
            if (cfg.default_column_key.size() != 32) return false;
        }

        if (!aadPrefix.empty()) {
            cfg.aad_prefix = aadPrefix;
        }

        if (!columnKeysJson.empty()) {
            auto keys = parseColumnKeys(columnKeysJson);
            for (auto& [name, hexKey] : keys) {
                crypto::ColumnKeySpec spec;
                spec.column_name = name;
                spec.key = hexToBytes(hexKey);
                if (spec.key.size() != 32) return false;
                cfg.column_keys.push_back(std::move(spec));
            }
        }

        auto result = ParquetReader::open(path, cfg);

        // Zero key material from WASM memory regardless of success/failure
        auto zero_vec = [](std::vector<uint8_t>& v) {
            if (!v.empty()) {
                volatile uint8_t* p = v.data();
                for (size_t i = 0; i < v.size(); ++i) p[i] = 0;
            }
            v.clear();
        };
        zero_vec(cfg.footer_key);
        zero_vec(cfg.default_column_key);
        for (auto& ck : cfg.column_keys) zero_vec(ck.key);

        if (!result.has_value()) return false;
        reader_ = std::make_unique<ParquetReader>(std::move(*result));
        return true;
    }
#endif

    int64_t numRows() const {
        return reader_ ? reader_->num_rows() : 0;
    }

    int64_t numRowGroups() const {
        return reader_ ? reader_->num_row_groups() : 0;
    }

    Schema schema() const {
        if (!reader_) return Schema{};
        return reader_->schema();
    }

    std::string createdBy() const {
        return reader_ ? reader_->created_by() : "";
    }

    em::val readColumnBool(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<bool>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    em::val readColumnInt32(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<int32_t>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    em::val readColumnInt64(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<int64_t>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", static_cast<double>((*result)[i]));
        return arr;
    }

    em::val readColumnFloat(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<float>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    em::val readColumnDouble(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<double>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    em::val readColumnString(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<std::string>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    em::val readColumnAsStrings(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column_as_strings(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }
};

// ---------------------------------------------------------------------------
// Embind registrations
// ---------------------------------------------------------------------------

EMSCRIPTEN_BINDINGS(signet_forge) {
    // Free functions
    em::function("version", &version);
    em::function("physicalTypeName", &physicalTypeName);
    em::function("writeFileToMemfs", &writeFileToMemfs);
    em::function("readFileFromMemfs", &readFileFromMemfs);

    // Schema
    em::class_<Schema>("Schema")
        .constructor<>()
        .function("numColumns", &schemaNumColumns)
        .function("columnName", &schemaColumnName)
        .function("columnPhysicalType", &schemaColumnPhysicalType)
        .function("name", &schemaName)
        ;

    // SchemaBuilder
    em::class_<WasmSchemaBuilder>("SchemaBuilder")
        .constructor<std::string>()
        .function("addBool",   &WasmSchemaBuilder::addBool)
        .function("addInt32",  &WasmSchemaBuilder::addInt32)
        .function("addInt64",  &WasmSchemaBuilder::addInt64)
        .function("addFloat",  &WasmSchemaBuilder::addFloat)
        .function("addDouble", &WasmSchemaBuilder::addDouble)
        .function("addString", &WasmSchemaBuilder::addString)
        .function("build",     &WasmSchemaBuilder::build)
        ;

    // WriterOptions
    em::class_<WasmWriterOptions>("WriterOptions")
        .constructor<>()
        .function("setRowGroupSize", &WasmWriterOptions::setRowGroupSize)
        .function("getRowGroupSize", &WasmWriterOptions::getRowGroupSize)
        ;

    // ParquetWriter
    em::class_<WasmParquetWriter>("ParquetWriter")
        .constructor<>()
        .function("open",              &WasmParquetWriter::open)
        .function("writeColumnBool",   &WasmParquetWriter::writeColumnBool)
        .function("writeColumnInt32",  &WasmParquetWriter::writeColumnInt32)
        .function("writeColumnInt64",  &WasmParquetWriter::writeColumnInt64)
        .function("writeColumnFloat",  &WasmParquetWriter::writeColumnFloat)
        .function("writeColumnDouble", &WasmParquetWriter::writeColumnDouble)
        .function("writeColumnString", &WasmParquetWriter::writeColumnString)
        .function("flushRowGroup",     &WasmParquetWriter::flushRowGroup)
        .function("close",            &WasmParquetWriter::close)
        .function("rowsWritten",      &WasmParquetWriter::rowsWritten)
        .function("isOpen",           &WasmParquetWriter::isOpen)
        ;

    // ParquetReader
    em::class_<WasmParquetReader>("ParquetReader")
        .constructor<>()
        .function("open",              &WasmParquetReader::open)
#if SIGNET_ENABLE_COMMERCIAL
        .function("openEncrypted",     &WasmParquetReader::openEncrypted)
#endif
        .function("numRows",          &WasmParquetReader::numRows)
        .function("numRowGroups",     &WasmParquetReader::numRowGroups)
        .function("schema",           &WasmParquetReader::schema)
        .function("createdBy",        &WasmParquetReader::createdBy)
        .function("readColumnBool",    &WasmParquetReader::readColumnBool)
        .function("readColumnInt32",   &WasmParquetReader::readColumnInt32)
        .function("readColumnInt64",   &WasmParquetReader::readColumnInt64)
        .function("readColumnFloat",   &WasmParquetReader::readColumnFloat)
        .function("readColumnDouble",  &WasmParquetReader::readColumnDouble)
        .function("readColumnString",  &WasmParquetReader::readColumnString)
        .function("readColumnAsStrings", &WasmParquetReader::readColumnAsStrings)
        ;
}
