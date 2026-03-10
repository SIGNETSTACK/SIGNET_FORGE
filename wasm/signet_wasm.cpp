// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji

/// @file signet_wasm.cpp
/// @brief Emscripten embind bindings for Signet Forge — browser-side Parquet
///        read/write with optional encryption support.
///
/// Provides JavaScript-facing wrapper classes (WasmSchemaBuilder,
/// WasmWriterOptions, WasmParquetWriter, WasmParquetReader) that bridge the
/// core C++ API into the browser via embind.  Files are staged through the
/// Emscripten MEMFS virtual filesystem.
///
/// Build: @code emcmake cmake --preset wasm && cmake --build build-wasm @endcode

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
// MEMFS helpers
// ---------------------------------------------------------------------------

/// Maximum file size allowed for MEMFS transfers (256 MB).
/// Prevents runaway allocations inside the WASM linear memory.
static constexpr unsigned MEMFS_MAX_FILE_SIZE = 256u * 1024u * 1024u;

/// Write a JavaScript ArrayBuffer into the Emscripten MEMFS virtual filesystem.
///
/// Uses EM_ASM internally because `em::val::global("FS")` is unavailable under
/// `-sMODULARIZE` (FS is scoped to the module closure, not a browser global).
///
/// @param path         MEMFS destination path (e.g. "/data/out.parquet").
/// @param arrayBuffer  JS ArrayBuffer containing file bytes.
/// @return True on success, false if the buffer exceeds @ref MEMFS_MAX_FILE_SIZE.
static bool writeFileToMemfs(const std::string& path, const em::val& arrayBuffer) {
    auto view = em::val::global("Uint8Array").new_(arrayBuffer);
    auto len = view["length"].as<unsigned>();
    if (len > MEMFS_MAX_FILE_SIZE) {
        emscripten_log(EM_LOG_ERROR, "writeFileToMemfs: file too large (%u bytes, max %u)", len, MEMFS_MAX_FILE_SIZE);
        return false;
    }
    std::vector<uint8_t> buf(len);
    auto memView = em::val(em::typed_memory_view(len, buf.data()));
    memView.call<void>("set", view);
    EM_ASM({
        FS.writeFile(UTF8ToString($0), HEAPU8.subarray($1, $1 + $2));
    }, path.c_str(), buf.data(), static_cast<int>(len));
    return true;
}

/// Read a file from the Emscripten MEMFS virtual filesystem.
///
/// @param path  MEMFS source path (e.g. "/data/out.parquet").
/// @return      A JS Uint8Array with the file contents.
/// @note For large files prefer calling `Module.FS.readFile()` directly from
///       JavaScript. This helper exists for API symmetry with writeFileToMemfs().
static em::val readFileFromMemfs(const std::string& path) {
    em::val fs = em::val::module_property("FS");
    return fs.call<em::val>("readFile", path);
}

// ---------------------------------------------------------------------------
// Hex and minimal JSON helpers (for encryption key input)
// ---------------------------------------------------------------------------

/// Convert a hex-encoded string to a byte vector.
///
/// @param hex  Even-length string of hex characters (e.g. "aabb01ff").
/// @return     Decoded bytes, or an empty vector if @p hex has odd length or
///             contains non-hex characters.
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

/// Parse a flat JSON object of string key-value pairs.
///
/// Only handles the subset `{"key":"value", ...}` — no nesting, no arrays,
/// no escaped quotes.  Used to deserialize per-column encryption keys passed
/// from JavaScript.
///
/// @param json  JSON string, e.g. `{"col_a":"aabb...", "col_b":"ccdd..."}`.
/// @return      Vector of (column-name, hex-key) pairs.
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
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < json.size()) {
                ++i; // skip backslash, take next char literally
            }
            s += json[i++];
        }
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

/// Return the Signet Forge library version string (SIGNET_CREATED_BY macro).
static std::string version() {
    return SIGNET_CREATED_BY;
}

// ---------------------------------------------------------------------------
// SchemaBuilder wrapper — chain-style API for JS
// ---------------------------------------------------------------------------

/// Fluent Parquet schema builder exposed to JavaScript.
///
/// Wraps the core `SchemaBuilder` with a chainable API suitable for embind:
/// @code
///   const schema = new Module.SchemaBuilder("my_table")
///       .addInt64("id")
///       .addString("name")
///       .build();
/// @endcode
class WasmSchemaBuilder {
    SchemaBuilder builder_;
public:
    /// Construct a new schema builder.
    /// @param name  Root schema / message name for the Parquet file metadata.
    explicit WasmSchemaBuilder(const std::string& name) : builder_(name) {}

    /// @name Column adders (chainable)
    /// Each method appends a column of the given physical type and returns
    /// `*this` so calls can be chained.
    /// @param col  Column name.
    /// @return     Reference to this builder for chaining.
    /// @{
    WasmSchemaBuilder& addBool(const std::string& col)       { builder_.column<bool>(col);        return *this; }
    WasmSchemaBuilder& addInt32(const std::string& col)      { builder_.column<int32_t>(col);     return *this; }
    WasmSchemaBuilder& addInt64(const std::string& col)      { builder_.column<int64_t>(col);     return *this; }
    WasmSchemaBuilder& addFloat(const std::string& col)      { builder_.column<float>(col);       return *this; }
    WasmSchemaBuilder& addDouble(const std::string& col)     { builder_.column<double>(col);      return *this; }
    WasmSchemaBuilder& addString(const std::string& col)     { builder_.column<std::string>(col); return *this; }
    /// @}

    /// Finalize and return the immutable Schema object.
    Schema build() { return builder_.build(); }
};

// ---------------------------------------------------------------------------
// Schema accessors (free functions bound onto the Schema class via embind)
// ---------------------------------------------------------------------------

/// Return the number of columns in the schema.
static size_t schemaNumColumns(const Schema& s) { return s.num_columns(); }

/// Return the name of column @p i, or "" if out of range.
static std::string schemaColumnName(const Schema& s, size_t i) {
    if (i >= s.num_columns()) return "";
    return s.column(i).name;
}

/// Return the integer-cast PhysicalType of column @p i, or -1 if out of range.
static int schemaColumnPhysicalType(const Schema& s, size_t i) {
    if (i >= s.num_columns()) return -1;
    return static_cast<int>(s.column(i).physical_type);
}

/// Return the root schema name.
static std::string schemaName(const Schema& s) { return s.name(); }

/// Map an integer PhysicalType to its human-readable name.
///
/// @param pt  Integer value of a PhysicalType enum member.
/// @return    Name string (e.g. "INT64"), or "UNKNOWN" for unrecognised values.
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

/// Thin wrapper around core WriterOptions for JavaScript consumption.
class WasmWriterOptions {
public:
    WriterOptions opts; ///< Underlying writer options struct.

    /// Construct with default options.
    WasmWriterOptions() = default;

    /// Set the target row group size (number of rows per group).
    /// @param n  Desired row group size.
    void setRowGroupSize(int64_t n) { opts.row_group_size = n; }

    /// Get the current row group size setting.
    int64_t getRowGroupSize() const { return opts.row_group_size; }
};

// ---------------------------------------------------------------------------
// ParquetWriter wrapper
// ---------------------------------------------------------------------------

/// JavaScript-facing Parquet writer.
///
/// Wraps the core `ParquetWriter` with per-type `writeColumn*` methods that
/// accept JS arrays via `em::val`, marshalling values into C++ vectors before
/// forwarding to the underlying typed `write_column<T>()`.
class WasmParquetWriter {
    std::unique_ptr<ParquetWriter> writer_;
public:
    /// Default-construct in an unopened state.
    WasmParquetWriter() = default;

    /// Open a new Parquet file for writing on MEMFS.
    /// @param path    MEMFS destination path.
    /// @param schema  Column schema built via WasmSchemaBuilder.
    /// @param opts    Writer options (row group size, etc.).
    /// @return `true` on success, `false` if the file could not be created.
    bool open(const std::string& path, const Schema& schema, const WasmWriterOptions& opts) {
        auto result = ParquetWriter::open(path, schema, opts.opts);
        if (!result.has_value()) return false;
        writer_ = std::make_unique<ParquetWriter>(std::move(*result));
        return true;
    }

    /// @name Typed column writers
    /// Write a JS array of values into the specified column.
    /// @param col  Zero-based column index.
    /// @param arr  JS Array of values matching the column's physical type.
    /// @return `true` on success, `false` if the writer is closed or @p col is
    ///         out of range.
    /// @{

    /// Write a boolean column from a JS array.
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

    /// Write an int32 column from a JS array.
    bool writeColumnInt32(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<int32_t> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<int32_t>();
        return writer_->write_column<int32_t>(col, buf.data(), len).has_value();
    }

    /// Write an int64 column from a JS array.
    bool writeColumnInt64(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<int64_t> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<int64_t>();
        return writer_->write_column<int64_t>(col, buf.data(), len).has_value();
    }

    /// Write a float column from a JS array.
    bool writeColumnFloat(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<float> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<float>();
        return writer_->write_column<float>(col, buf.data(), len).has_value();
    }

    /// Write a double column from a JS array.
    bool writeColumnDouble(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<double> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<double>();
        return writer_->write_column<double>(col, buf.data(), len).has_value();
    }

    /// Write a string (BYTE_ARRAY) column from a JS array.
    bool writeColumnString(size_t col, const em::val& arr) {
        if (!writer_ || col >= writer_->num_columns()) return false;
        auto len = arr["length"].as<unsigned>();
        std::vector<std::string> buf(len);
        for (unsigned i = 0; i < len; ++i) buf[i] = arr[i].as<std::string>();
        return writer_->write_column<std::string>(col, buf.data(), len).has_value();
    }

    /// @}

    /// Flush the current row group to disk and begin a new one.
    /// @return `true` on success.
    bool flushRowGroup() {
        if (!writer_) return false;
        return writer_->flush_row_group().has_value();
    }

    /// Finalize the Parquet file (writes footer metadata and closes the file).
    /// @return `true` on success.
    bool close() {
        if (!writer_) return false;
        return writer_->close().has_value();
    }

    /// Return the total number of rows written so far (across all row groups).
    int64_t rowsWritten() const {
        return writer_ ? writer_->rows_written() : 0;
    }

    /// Check whether the writer is currently open and accepting data.
    bool isOpen() const {
        return writer_ && writer_->is_open();
    }
};

// ---------------------------------------------------------------------------
// ParquetReader wrapper
// ---------------------------------------------------------------------------

/// JavaScript-facing Parquet reader.
///
/// Wraps the core `ParquetReader` with per-type `readColumn*` methods that
/// return JS arrays via `em::val`.  Supports optional encrypted file opening
/// when built with `SIGNET_ENABLE_COMMERCIAL`.
class WasmParquetReader {
    std::unique_ptr<ParquetReader> reader_;
public:
    /// Default-construct in an unopened state.
    WasmParquetReader() = default;

    /// Open a plaintext Parquet file from MEMFS.
    /// @param path  MEMFS path to an existing Parquet file.
    /// @return `true` on success, `false` if the file is missing or corrupt.
    bool open(const std::string& path) {
        auto result = ParquetReader::open(path);
        if (!result.has_value()) return false;
        reader_ = std::make_unique<ParquetReader>(std::move(*result));
        return true;
    }

#if SIGNET_ENABLE_COMMERCIAL
    /// Open an encrypted Parquet file (PME / AES-256-GCM).
    ///
    /// @param path           MEMFS path to the encrypted Parquet file.
    /// @param footerKeyHex   64-char hex string encoding the 32-byte AES-256
    ///                       footer decryption key (required).
    /// @param columnKeyHex   64-char hex default column key applied to all
    ///                       columns, or empty string to omit.
    /// @param aadPrefix      AAD prefix bound into GCM authentication tags, or
    ///                       empty string to omit.
    /// @param columnKeysJson Per-column keys as a flat JSON object
    ///                       `{"col_name":"hex64", ...}`, or empty string.
    /// @return `true` on success, `false` on decryption failure or bad keys.
    ///
    /// @note **Security**: all key material (footer key, column keys) is zeroed
    ///       from WASM linear memory via volatile writes after use, regardless
    ///       of success or failure.
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

    /// Return total row count across all row groups, or 0 if not open.
    int64_t numRows() const {
        return reader_ ? reader_->num_rows() : 0;
    }

    /// Return the number of row groups in the file, or 0 if not open.
    int64_t numRowGroups() const {
        return reader_ ? reader_->num_row_groups() : 0;
    }

    /// Return the file's schema, or an empty Schema if not open.
    Schema schema() const {
        if (!reader_) return Schema{};
        return reader_->schema();
    }

    /// Return the "created by" metadata string, or "" if not open.
    std::string createdBy() const {
        return reader_ ? reader_->created_by() : "";
    }

    /// @name Typed column readers
    /// Read all values from a single column within a row group, returning a
    /// JS Array.
    /// @param rg   Zero-based row group index.
    /// @param col  Zero-based column index.
    /// @return     JS Array of typed values, or an empty array on error.
    /// @{

    /// Read a boolean column as a JS Array of booleans.
    em::val readColumnBool(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<bool>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    /// Read an int32 column as a JS Array of numbers.
    em::val readColumnInt32(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<int32_t>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    /// Read an int64 column as a JS Array of doubles (JS has no native int64).
    em::val readColumnInt64(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<int64_t>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", static_cast<double>((*result)[i]));
        return arr;
    }

    /// Read a float column as a JS Array of numbers.
    em::val readColumnFloat(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<float>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    /// Read a double column as a JS Array of numbers.
    em::val readColumnDouble(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<double>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    /// Read a string (BYTE_ARRAY) column as a JS Array of strings.
    em::val readColumnString(size_t rg, size_t col) {
        if (!reader_) return em::val::array();
        auto result = reader_->read_column<std::string>(rg, col);
        if (!result.has_value()) return em::val::array();
        auto arr = em::val::array();
        for (size_t i = 0; i < result->size(); ++i)
            arr.call<void>("push", (*result)[i]);
        return arr;
    }

    /// @}

    /// Read any column as a JS Array of strings (type-erased).
    ///
    /// Useful for display/preview where the caller does not know the column's
    /// physical type at compile time.
    /// @param rg   Zero-based row group index.
    /// @param col  Zero-based column index.
    /// @return     JS Array of stringified values, or an empty array on error.
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

/// @brief Emscripten embind registration block.
///
/// Exports the following to JavaScript under `Module.*`:
/// - Free functions: version(), physicalTypeName(), writeFileToMemfs(),
///   readFileFromMemfs()
/// - Classes: Schema, SchemaBuilder, WriterOptions, ParquetWriter,
///   ParquetReader (with openEncrypted() when SIGNET_ENABLE_COMMERCIAL)
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
