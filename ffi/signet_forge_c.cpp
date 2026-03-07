// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
//
// signet_forge_c.cpp — C FFI implementation for Signet Forge
//
// This translation unit #includes the header-only library, instantiates the
// templates for all 6 supported types, and exports the flat C API declared
// in signet_forge.h.

#include "signet_forge.h"

#include "signet/writer.hpp"
#include "signet/reader.hpp"
#include "signet/schema.hpp"
#include "signet/types.hpp"
#include "signet/error.hpp"

#include <cstring>
#include <new>
#include <string>
#include <vector>

using namespace signet::forge;

// ---------------------------------------------------------------------------
// Static assertions
// ---------------------------------------------------------------------------
static_assert(sizeof(bool) == 1, "bool must be 1 byte for FFI safety");

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Build a success error.
signet_error_t make_ok() {
    return signet_error_t{SIGNET_OK, nullptr};
}

/// Build an error from a signet::forge::Error.
signet_error_t make_error(const Error& err) {
    signet_error_t e;
    e.code = static_cast<int32_t>(err.code);
    if (!err.message.empty()) {
        char* msg = new (std::nothrow) char[err.message.size() + 1];
        if (msg) {
            std::memcpy(msg, err.message.c_str(), err.message.size() + 1);
        }
        e.message = msg;
    } else {
        e.message = nullptr;
    }
    return e;
}

/// Build an error from code + message.
signet_error_t make_error(int32_t code, const char* msg) {
    signet_error_t e;
    e.code = code;
    if (msg && msg[0] != '\0') {
        size_t len = std::strlen(msg);
        char* buf = new (std::nothrow) char[len + 1];
        if (buf) {
            std::memcpy(buf, msg, len + 1);
        }
        e.message = buf;
    } else {
        e.message = nullptr;
    }
    return e;
}

/// Make a signet_string_t from a std::string.
signet_string_t make_string(const std::string& s) {
    signet_string_t out;
    out.len = s.size();
    char* buf = new (std::nothrow) char[s.size() + 1];
    if (buf) {
        std::memcpy(buf, s.c_str(), s.size() + 1);
    }
    out.ptr = buf;
    return out;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Opaque handle wrappers
// ---------------------------------------------------------------------------

struct signet_schema_builder_s {
    SchemaBuilder builder;
    explicit signet_schema_builder_s(const char* name)
        : builder(name) {}
};

struct signet_schema_s {
    Schema schema;
    explicit signet_schema_s(Schema s) : schema(std::move(s)) {}
};

struct signet_writer_options_s {
    WriterOptions options;
};

struct signet_writer_s {
    ParquetWriter writer;
    explicit signet_writer_s(ParquetWriter w) : writer(std::move(w)) {}
};

struct signet_reader_s {
    ParquetReader reader;
    Schema        owned_schema;  // Schema returned via signet_reader_schema
    signet_schema_s* schema_handle = nullptr;

    explicit signet_reader_s(ParquetReader r) : reader(std::move(r)) {
        owned_schema = reader.schema();
        schema_handle = new (std::nothrow) signet_schema_s(owned_schema);
    }
    ~signet_reader_s() {
        // schema_handle is NOT freed here — it's a borrowed pointer
        // whose lifetime is tied to the reader. We do free it since
        // signet_reader_schema says "valid while reader is alive".
        delete schema_handle;
    }
};

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

extern "C"
const char* signet_version(void) {
    return "0.1.0";
}

// ---------------------------------------------------------------------------
// Error + String
// ---------------------------------------------------------------------------

extern "C"
void signet_error_free(signet_error_t* err) {
    if (err && err->message) {
        delete[] err->message;
        err->message = nullptr;
    }
}

extern "C"
void signet_string_free(signet_string_t* s) {
    if (s && s->ptr) {
        delete[] s->ptr;
        s->ptr = nullptr;
        s->len = 0;
    }
}

// ---------------------------------------------------------------------------
// SchemaBuilder
// ---------------------------------------------------------------------------

extern "C"
signet_schema_builder_t* signet_schema_builder_new(const char* name) {
    if (!name) return nullptr;
    return new (std::nothrow) signet_schema_builder_t(name);
}

extern "C"
signet_error_t signet_schema_builder_add_bool(
    signet_schema_builder_t* builder, const char* col_name)
{
    if (!builder || !col_name)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    builder->builder.column<bool>(col_name);
    return make_ok();
}

extern "C"
signet_error_t signet_schema_builder_add_int32(
    signet_schema_builder_t* builder, const char* col_name)
{
    if (!builder || !col_name)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    builder->builder.column<int32_t>(col_name);
    return make_ok();
}

extern "C"
signet_error_t signet_schema_builder_add_int64(
    signet_schema_builder_t* builder, const char* col_name)
{
    if (!builder || !col_name)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    builder->builder.column<int64_t>(col_name);
    return make_ok();
}

extern "C"
signet_error_t signet_schema_builder_add_float(
    signet_schema_builder_t* builder, const char* col_name)
{
    if (!builder || !col_name)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    builder->builder.column<float>(col_name);
    return make_ok();
}

extern "C"
signet_error_t signet_schema_builder_add_double(
    signet_schema_builder_t* builder, const char* col_name)
{
    if (!builder || !col_name)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    builder->builder.column<double>(col_name);
    return make_ok();
}

extern "C"
signet_error_t signet_schema_builder_add_string(
    signet_schema_builder_t* builder, const char* col_name)
{
    if (!builder || !col_name)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    builder->builder.column<std::string>(col_name);
    return make_ok();
}

extern "C"
signet_error_t signet_schema_builder_build(
    signet_schema_builder_t* builder,
    signet_schema_t** out_schema)
{
    if (!builder || !out_schema)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    Schema schema = builder->builder.build();
    *out_schema = new (std::nothrow) signet_schema_t(std::move(schema));
    if (!*out_schema)
        return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");

    delete builder;
    return make_ok();
}

extern "C"
void signet_schema_builder_free(signet_schema_builder_t* builder) {
    delete builder;
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

extern "C"
void signet_schema_free(signet_schema_t* schema) {
    delete schema;
}

extern "C"
size_t signet_schema_num_columns(const signet_schema_t* schema) {
    if (!schema) return 0;
    return schema->schema.num_columns();
}

extern "C"
signet_string_t signet_schema_column_name(
    const signet_schema_t* schema, size_t index)
{
    if (!schema || index >= schema->schema.num_columns()) {
        signet_string_t empty = {nullptr, 0};
        return empty;
    }
    return make_string(schema->schema.column(index).name);
}

extern "C"
int32_t signet_schema_column_physical_type(
    const signet_schema_t* schema, size_t index)
{
    if (!schema || index >= schema->schema.num_columns()) return -1;
    return static_cast<int32_t>(schema->schema.column(index).physical_type);
}

// ---------------------------------------------------------------------------
// WriterOptions
// ---------------------------------------------------------------------------

extern "C"
signet_writer_options_t* signet_writer_options_new(void) {
    return new (std::nothrow) signet_writer_options_t{};
}

extern "C"
void signet_writer_options_free(signet_writer_options_t* opts) {
    delete opts;
}

extern "C"
void signet_writer_options_set_row_group_size(
    signet_writer_options_t* opts, int64_t size)
{
    if (opts) opts->options.row_group_size = size;
}

extern "C"
void signet_writer_options_set_encoding(
    signet_writer_options_t* opts, int32_t encoding)
{
    if (opts) opts->options.default_encoding = static_cast<Encoding>(encoding);
}

extern "C"
void signet_writer_options_set_compression(
    signet_writer_options_t* opts, int32_t compression)
{
    if (opts) opts->options.compression = static_cast<Compression>(compression);
}

extern "C"
void signet_writer_options_set_auto_encoding(
    signet_writer_options_t* opts, uint8_t enabled)
{
    if (opts) opts->options.auto_encoding = (enabled != 0);
}

// ---------------------------------------------------------------------------
// ParquetWriter
// ---------------------------------------------------------------------------

extern "C"
signet_error_t signet_writer_open(
    const char* path,
    const signet_schema_t* schema,
    const signet_writer_options_t* options,
    signet_writer_t** out_writer)
{
    if (!path || !schema || !out_writer)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    try {
        WriterOptions opts;
        if (options) opts = options->options;

        auto result = ParquetWriter::open(path, schema->schema, opts);
        if (!result)
            return make_error(result.error());

        *out_writer = new (std::nothrow) signet_writer_t(std::move(*result));
        if (!*out_writer)
            return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");

        return make_ok();
    } catch (const std::exception& e) {
        return make_error(SIGNET_INTERNAL_ERROR, e.what());
    } catch (...) {
        return make_error(SIGNET_INTERNAL_ERROR, "unknown C++ exception");
    }
}

extern "C"
signet_error_t signet_writer_write_column_bool(
    signet_writer_t* writer, size_t col_index,
    const uint8_t* values, size_t count)
{
    if (!writer || !values)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    // Convert uint8_t array to bool array
    std::vector<bool> bools(count);
    for (size_t i = 0; i < count; ++i) {
        bools[i] = (values[i] != 0);
    }

    // write_column<bool> takes const bool*, but vector<bool> is special.
    // Use a temporary contiguous array.
    std::vector<char> tmp(count);
    for (size_t i = 0; i < count; ++i) {
        tmp[i] = static_cast<char>(bools[i]);
    }

    auto result = writer->writer.write_column<bool>(
        col_index, reinterpret_cast<const bool*>(tmp.data()), count);
    if (!result)
        return make_error(result.error());

    return make_ok();
}

extern "C"
signet_error_t signet_writer_write_column_int32(
    signet_writer_t* writer, size_t col_index,
    const int32_t* values, size_t count)
{
    if (!writer || !values)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = writer->writer.write_column<int32_t>(col_index, values, count);
    if (!result)
        return make_error(result.error());
    return make_ok();
}

extern "C"
signet_error_t signet_writer_write_column_int64(
    signet_writer_t* writer, size_t col_index,
    const int64_t* values, size_t count)
{
    if (!writer || !values)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = writer->writer.write_column<int64_t>(col_index, values, count);
    if (!result)
        return make_error(result.error());
    return make_ok();
}

extern "C"
signet_error_t signet_writer_write_column_float(
    signet_writer_t* writer, size_t col_index,
    const float* values, size_t count)
{
    if (!writer || !values)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = writer->writer.write_column<float>(col_index, values, count);
    if (!result)
        return make_error(result.error());
    return make_ok();
}

extern "C"
signet_error_t signet_writer_write_column_double(
    signet_writer_t* writer, size_t col_index,
    const double* values, size_t count)
{
    if (!writer || !values)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = writer->writer.write_column<double>(col_index, values, count);
    if (!result)
        return make_error(result.error());
    return make_ok();
}

extern "C"
signet_error_t signet_writer_write_column_string(
    signet_writer_t* writer, size_t col_index,
    const char* const* values, size_t count)
{
    if (!writer || !values)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    try {
        std::vector<std::string> strs;
        strs.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            strs.emplace_back(values[i] ? values[i] : "");
        }

        auto result = writer->writer.write_column(
            col_index, strs.data(), strs.size());
        if (!result)
            return make_error(result.error());
        return make_ok();
    } catch (const std::exception& e) {
        return make_error(SIGNET_INTERNAL_ERROR, e.what());
    } catch (...) {
        return make_error(SIGNET_INTERNAL_ERROR, "unknown C++ exception");
    }
}

extern "C"
signet_error_t signet_writer_write_row(
    signet_writer_t* writer,
    const char* const* values, size_t num_values)
{
    if (!writer || !values)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    try {
        std::vector<std::string> row;
        row.reserve(num_values);
        for (size_t i = 0; i < num_values; ++i) {
            row.emplace_back(values[i] ? values[i] : "");
        }

        auto result = writer->writer.write_row(row);
        if (!result)
            return make_error(result.error());
        return make_ok();
    } catch (const std::exception& e) {
        return make_error(SIGNET_INTERNAL_ERROR, e.what());
    } catch (...) {
        return make_error(SIGNET_INTERNAL_ERROR, "unknown C++ exception");
    }
}

extern "C"
signet_error_t signet_writer_flush_row_group(signet_writer_t* writer) {
    if (!writer)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = writer->writer.flush_row_group();
    if (!result)
        return make_error(result.error());
    return make_ok();
}

extern "C"
signet_error_t signet_writer_close(signet_writer_t* writer) {
    if (!writer)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = writer->writer.close();
    if (!result)
        return make_error(result.error());
    return make_ok();
}

extern "C"
int64_t signet_writer_rows_written(const signet_writer_t* writer) {
    if (!writer) return 0;
    return writer->writer.rows_written();
}

extern "C"
uint8_t signet_writer_is_open(const signet_writer_t* writer) {
    if (!writer) return 0;
    return writer->writer.is_open() ? 1 : 0;
}

extern "C"
void signet_writer_free(signet_writer_t* writer) {
    delete writer;
}

// ---------------------------------------------------------------------------
// ParquetReader
// ---------------------------------------------------------------------------

extern "C"
signet_error_t signet_reader_open(
    const char* path,
    signet_reader_t** out_reader)
{
    if (!path || !out_reader)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    try {
        auto result = ParquetReader::open(path);
        if (!result)
            return make_error(result.error());

        *out_reader = new (std::nothrow) signet_reader_s(std::move(*result));
        if (!*out_reader)
            return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");

        return make_ok();
    } catch (const std::exception& e) {
        return make_error(SIGNET_INTERNAL_ERROR, e.what());
    } catch (...) {
        return make_error(SIGNET_INTERNAL_ERROR, "unknown C++ exception");
    }
}

extern "C"
void signet_reader_free(signet_reader_t* reader) {
    delete reader;
}

extern "C"
int64_t signet_reader_num_rows(const signet_reader_t* reader) {
    if (!reader) return 0;
    return reader->reader.num_rows();
}

extern "C"
int64_t signet_reader_num_row_groups(const signet_reader_t* reader) {
    if (!reader) return 0;
    return reader->reader.num_row_groups();
}

extern "C"
const signet_schema_t* signet_reader_schema(const signet_reader_t* reader) {
    if (!reader) return nullptr;
    return reader->schema_handle;
}

extern "C"
signet_string_t signet_reader_created_by(const signet_reader_t* reader) {
    if (!reader) {
        signet_string_t empty = {nullptr, 0};
        return empty;
    }
    return make_string(reader->reader.created_by());
}

extern "C"
signet_error_t signet_reader_read_column_bool(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    uint8_t** out_values, size_t* out_count)
{
    if (!reader || !out_values || !out_count)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = reader->reader.read_column<bool>(row_group, col_index);
    if (!result)
        return make_error(result.error());

    const auto& vec = *result;
    *out_count = vec.size();
    uint8_t* arr = new (std::nothrow) uint8_t[vec.size()];
    if (!arr)
        return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");

    for (size_t i = 0; i < vec.size(); ++i) {
        arr[i] = vec[i] ? 1 : 0;
    }
    *out_values = arr;
    return make_ok();
}

extern "C"
signet_error_t signet_reader_read_column_int32(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    int32_t** out_values, size_t* out_count)
{
    if (!reader || !out_values || !out_count)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = reader->reader.read_column<int32_t>(row_group, col_index);
    if (!result)
        return make_error(result.error());

    const auto& vec = *result;
    *out_count = vec.size();
    int32_t* arr = new (std::nothrow) int32_t[vec.size()];
    if (!arr)
        return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");
    std::memcpy(arr, vec.data(), vec.size() * sizeof(int32_t));
    *out_values = arr;
    return make_ok();
}

extern "C"
signet_error_t signet_reader_read_column_int64(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    int64_t** out_values, size_t* out_count)
{
    if (!reader || !out_values || !out_count)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = reader->reader.read_column<int64_t>(row_group, col_index);
    if (!result)
        return make_error(result.error());

    const auto& vec = *result;
    *out_count = vec.size();
    int64_t* arr = new (std::nothrow) int64_t[vec.size()];
    if (!arr)
        return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");
    std::memcpy(arr, vec.data(), vec.size() * sizeof(int64_t));
    *out_values = arr;
    return make_ok();
}

extern "C"
signet_error_t signet_reader_read_column_float(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    float** out_values, size_t* out_count)
{
    if (!reader || !out_values || !out_count)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = reader->reader.read_column<float>(row_group, col_index);
    if (!result)
        return make_error(result.error());

    const auto& vec = *result;
    *out_count = vec.size();
    float* arr = new (std::nothrow) float[vec.size()];
    if (!arr)
        return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");
    std::memcpy(arr, vec.data(), vec.size() * sizeof(float));
    *out_values = arr;
    return make_ok();
}

extern "C"
signet_error_t signet_reader_read_column_double(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    double** out_values, size_t* out_count)
{
    if (!reader || !out_values || !out_count)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");

    auto result = reader->reader.read_column<double>(row_group, col_index);
    if (!result)
        return make_error(result.error());

    const auto& vec = *result;
    *out_count = vec.size();
    double* arr = new (std::nothrow) double[vec.size()];
    if (!arr)
        return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");
    std::memcpy(arr, vec.data(), vec.size() * sizeof(double));
    *out_values = arr;
    return make_ok();
}

extern "C"
signet_error_t signet_reader_read_column_string(
    signet_reader_t* reader, size_t row_group, size_t col_index,
    signet_string_t** out_values, size_t* out_count)
{
    if (!reader || !out_values || !out_count)
        return make_error(SIGNET_INTERNAL_ERROR, "null argument");
    try {
        auto result = reader->reader.read_column_as_strings(row_group, col_index);
        if (!result)
            return make_error(result.error());

        const auto& vec = *result;
        *out_count = vec.size();
        if (vec.empty()) {
            *out_values = nullptr;
            return make_ok();
        }
        signet_string_t* arr = new (std::nothrow) signet_string_t[vec.size()];
        if (!arr)
            return make_error(SIGNET_INTERNAL_ERROR, "allocation failed");

        for (size_t i = 0; i < vec.size(); ++i) {
            arr[i] = make_string(vec[i]);
            if (arr[i].ptr == nullptr && !vec[i].empty()) {
                // OOM during string copy — clean up partial array
                for (size_t j = 0; j < i; ++j) {
                    delete[] arr[j].ptr;
                }
                delete[] arr;
                return make_error(SIGNET_INTERNAL_ERROR, "string allocation failed");
            }
        }
        *out_values = arr;
        return make_ok();
    } catch (const std::exception& e) {
        return make_error(SIGNET_INTERNAL_ERROR, e.what());
    } catch (...) {
        return make_error(SIGNET_INTERNAL_ERROR, "unknown C++ exception");
    }
}

// ---------------------------------------------------------------------------
// Array free functions
// ---------------------------------------------------------------------------

extern "C"
void signet_free_bool_array(uint8_t* arr) {
    delete[] arr;
}

extern "C"
void signet_free_int32_array(int32_t* arr) {
    delete[] arr;
}

extern "C"
void signet_free_int64_array(int64_t* arr) {
    delete[] arr;
}

extern "C"
void signet_free_float_array(float* arr) {
    delete[] arr;
}

extern "C"
void signet_free_double_array(double* arr) {
    delete[] arr;
}

extern "C"
void signet_free_string_array(signet_string_t* arr, size_t count) {
    if (!arr) return;
    for (size_t i = 0; i < count; ++i) {
        signet_string_free(&arr[i]);
    }
    delete[] arr;
}
