// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// python/signet_forge/_bindings.cpp
// pybind11 Python bindings for Signet_Forge
// Module name: signet_forge._bindings

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>           // automatic std::vector/optional/string conversions
#include <pybind11/numpy.h>         // py::array_t<T> for NumPy integration

#include "signet/forge.hpp"

namespace py = pybind11;
using namespace signet::forge;

// ---------------------------------------------------------------------------
// Error-unwrapping helpers — convert expected<T> errors into Python exceptions
// ---------------------------------------------------------------------------
template<typename T>
T unwrap(expected<T> r) {
    if (!r.has_value())
        throw std::runtime_error(r.error().message);
    if constexpr (std::is_move_constructible_v<T>)
        return std::move(*r);
    else
        return *r;
}

void unwrap_void(expected<void> r) {
    if (!r.has_value())
        throw std::runtime_error(r.error().message);
}

// ---------------------------------------------------------------------------
// Module definition
// ---------------------------------------------------------------------------
PYBIND11_MODULE(_bindings, m) {
    m.doc() = "Signet Forge — C++20 Parquet library with AI-native extensions";

    // -----------------------------------------------------------------------
    // Enums
    // -----------------------------------------------------------------------
    py::enum_<PhysicalType>(m, "PhysicalType")
        .value("BOOLEAN", PhysicalType::BOOLEAN)
        .value("INT32",   PhysicalType::INT32)
        .value("INT64",   PhysicalType::INT64)
        .value("FLOAT",   PhysicalType::FLOAT)
        .value("DOUBLE",  PhysicalType::DOUBLE)
        .value("BYTE_ARRAY", PhysicalType::BYTE_ARRAY)
        .value("FIXED_LEN_BYTE_ARRAY", PhysicalType::FIXED_LEN_BYTE_ARRAY)
        .export_values();

    py::enum_<LogicalType>(m, "LogicalType")
        .value("NONE",         LogicalType::NONE)
        .value("STRING",       LogicalType::STRING)
        .value("DATE",         LogicalType::DATE)
        .value("TIMESTAMP_MS", LogicalType::TIMESTAMP_MS)
        .value("TIMESTAMP_US", LogicalType::TIMESTAMP_US)
        .value("TIMESTAMP_NS", LogicalType::TIMESTAMP_NS)
        .value("DECIMAL",      LogicalType::DECIMAL)
        .value("JSON",         LogicalType::JSON)
        .export_values();

    py::enum_<Encoding>(m, "Encoding")
        .value("PLAIN",               Encoding::PLAIN)
        .value("RLE_DICTIONARY",      Encoding::RLE_DICTIONARY)
        .value("DELTA_BINARY_PACKED", Encoding::DELTA_BINARY_PACKED)
        .value("BYTE_STREAM_SPLIT",   Encoding::BYTE_STREAM_SPLIT)
        .export_values();

    py::enum_<Compression>(m, "Compression")
        .value("UNCOMPRESSED", Compression::UNCOMPRESSED)
        .value("SNAPPY",       Compression::SNAPPY)
        .value("GZIP",         Compression::GZIP)
        .value("LZ4",          Compression::LZ4)
        .value("ZSTD",         Compression::ZSTD)
        .export_values();

    // -----------------------------------------------------------------------
    // ColumnDescriptor
    // -----------------------------------------------------------------------
    py::class_<ColumnDescriptor>(m, "ColumnDescriptor")
        .def_readonly("name",          &ColumnDescriptor::name)
        .def_readonly("physical_type", &ColumnDescriptor::physical_type)
        .def_readonly("logical_type",  &ColumnDescriptor::logical_type)
        .def("__repr__", [](const ColumnDescriptor& cd) {
            return "<ColumnDescriptor name='" + cd.name + "'>";
        });

    // -----------------------------------------------------------------------
    // Schema + SchemaBuilder
    // -----------------------------------------------------------------------
    py::class_<Schema>(m, "Schema")
        .def(py::init<>())
        .def_property_readonly("name",        &Schema::name)
        .def_property_readonly("num_columns", &Schema::num_columns)
        .def("column",      &Schema::column, py::arg("index"))
        .def("find_column", [](const Schema& s, const std::string& n) -> py::object {
            auto idx = s.find_column(n);
            if (!idx) return py::none();
            return py::int_(*idx);
        })
        .def("__repr__", [](const Schema& s) {
            return "<Schema name='" + s.name() + "' columns=" +
                   std::to_string(s.num_columns()) + ">";
        });

    // SchemaBuilder — Python-friendly fluent API
    // Use named methods per type instead of exposing C++ templates
    py::class_<SchemaBuilder>(m, "SchemaBuilder")
        .def(py::init<std::string>(), py::arg("name"))
        .def("bool_",   [](SchemaBuilder& b, std::string n) -> SchemaBuilder& {
            return b.column<bool>(std::move(n));
        }, py::arg("name"), py::return_value_policy::reference_internal)
        .def("int32",   [](SchemaBuilder& b, std::string n) -> SchemaBuilder& {
            return b.column<int32_t>(std::move(n));
        }, py::arg("name"), py::return_value_policy::reference_internal)
        .def("int64",   [](SchemaBuilder& b, std::string n) -> SchemaBuilder& {
            return b.column<int64_t>(std::move(n));
        }, py::arg("name"), py::return_value_policy::reference_internal)
        .def("int64_ts",[](SchemaBuilder& b, std::string n) -> SchemaBuilder& {
            return b.column<int64_t>(std::move(n), LogicalType::TIMESTAMP_NS);
        }, py::arg("name"), py::return_value_policy::reference_internal)
        .def("float_",  [](SchemaBuilder& b, std::string n) -> SchemaBuilder& {
            return b.column<float>(std::move(n));
        }, py::arg("name"), py::return_value_policy::reference_internal)
        .def("double_", [](SchemaBuilder& b, std::string n) -> SchemaBuilder& {
            return b.column<double>(std::move(n));
        }, py::arg("name"), py::return_value_policy::reference_internal)
        .def("string",  [](SchemaBuilder& b, std::string n) -> SchemaBuilder& {
            return b.column<std::string>(std::move(n));
        }, py::arg("name"), py::return_value_policy::reference_internal)
        .def("build",   &SchemaBuilder::build);

    // -----------------------------------------------------------------------
    // WriterOptions
    // -----------------------------------------------------------------------
    py::class_<WriterOptions>(m, "WriterOptions")
        .def(py::init<>())
        .def_readwrite("row_group_size",    &WriterOptions::row_group_size)
        .def_readwrite("default_encoding",  &WriterOptions::default_encoding)
        .def_readwrite("compression",       &WriterOptions::compression)
        .def_readwrite("auto_encoding",     &WriterOptions::auto_encoding)
        .def_readwrite("auto_compression",  &WriterOptions::auto_compression);

    // -----------------------------------------------------------------------
    // ParquetWriter
    // -----------------------------------------------------------------------
    py::class_<ParquetWriter>(m, "ParquetWriter")
        .def_static("open", [](const std::string& path, const Schema& schema,
                                const WriterOptions& opts) {
            return unwrap(ParquetWriter::open(path, schema, opts));
        }, py::arg("path"), py::arg("schema"), py::arg("options") = WriterOptions{})

        // write_column — dispatches on numpy dtype
        .def("write_column_int32", [](ParquetWriter& w, size_t col,
                                       py::array_t<int32_t, py::array::c_style | py::array::forcecast> arr) {
            if (col >= w.num_columns())
                throw py::index_error("column index " + std::to_string(col) + " out of range");
            auto buf = arr.request();
            unwrap_void(w.write_column<int32_t>(col,
                static_cast<const int32_t*>(buf.ptr),
                static_cast<size_t>(buf.size)));
        }, py::arg("col_index"), py::arg("array"))
        .def("write_column_int64", [](ParquetWriter& w, size_t col,
                                       py::array_t<int64_t, py::array::c_style | py::array::forcecast> arr) {
            if (col >= w.num_columns())
                throw py::index_error("column index " + std::to_string(col) + " out of range");
            auto buf = arr.request();
            unwrap_void(w.write_column<int64_t>(col,
                static_cast<const int64_t*>(buf.ptr),
                static_cast<size_t>(buf.size)));
        }, py::arg("col_index"), py::arg("array"))
        .def("write_column_float", [](ParquetWriter& w, size_t col,
                                       py::array_t<float, py::array::c_style | py::array::forcecast> arr) {
            if (col >= w.num_columns())
                throw py::index_error("column index " + std::to_string(col) + " out of range");
            auto buf = arr.request();
            unwrap_void(w.write_column<float>(col,
                static_cast<const float*>(buf.ptr),
                static_cast<size_t>(buf.size)));
        }, py::arg("col_index"), py::arg("array"))
        .def("write_column_double", [](ParquetWriter& w, size_t col,
                                        py::array_t<double, py::array::c_style | py::array::forcecast> arr) {
            if (col >= w.num_columns())
                throw py::index_error("column index " + std::to_string(col) + " out of range");
            auto buf = arr.request();
            unwrap_void(w.write_column<double>(col,
                static_cast<const double*>(buf.ptr),
                static_cast<size_t>(buf.size)));
        }, py::arg("col_index"), py::arg("array"))
        .def("write_column_string", [](ParquetWriter& w, size_t col,
                                        py::list lst) {
            if (col >= w.num_columns())
                throw py::index_error("column index " + std::to_string(col) + " out of range");
            std::vector<std::string> vals;
            vals.reserve(py::len(lst));
            for (auto& item : lst) vals.push_back(item.cast<std::string>());
            unwrap_void(w.write_column(col, vals.data(), vals.size()));
        }, py::arg("col_index"), py::arg("values"))
        .def("write_column_bool", [](ParquetWriter& w, size_t col,
                                      py::array_t<bool, py::array::c_style | py::array::forcecast> arr) {
            if (col >= w.num_columns())
                throw py::index_error("column index " + std::to_string(col) + " out of range");
            auto buf = arr.request();
            const bool* ptr = static_cast<const bool*>(buf.ptr);
            unwrap_void(w.write_column<bool>(col, ptr, static_cast<size_t>(buf.size)));
        }, py::arg("col_index"), py::arg("array"))

        .def("flush_row_group", [](ParquetWriter& w) {
            unwrap_void(w.flush_row_group());
        })
        .def("close", [](ParquetWriter& w) {
            auto result = w.close();
            if (!result.has_value())
                throw std::runtime_error(result.error().message);
        })
        .def("close_with_stats", [](ParquetWriter& w) -> py::dict {
            auto result = w.close();
            if (!result.has_value())
                throw std::runtime_error(result.error().message);
            const auto& s = *result;
            py::dict d;
            d["file_size_bytes"]          = s.file_size_bytes;
            d["total_rows"]               = s.total_rows;
            d["total_row_groups"]         = s.total_row_groups;
            d["total_uncompressed_bytes"] = s.total_uncompressed_bytes;
            d["total_compressed_bytes"]   = s.total_compressed_bytes;
            d["compression_ratio"]        = s.compression_ratio;
            d["bytes_per_row"]            = s.bytes_per_row;
            py::list cols;
            for (const auto& c : s.columns) {
                py::dict cd;
                cd["column_name"]        = c.column_name;
                cd["uncompressed_bytes"] = c.uncompressed_bytes;
                cd["compressed_bytes"]   = c.compressed_bytes;
                cd["num_values"]         = c.num_values;
                cd["null_count"]         = c.null_count;
                cols.append(cd);
            }
            d["columns"] = cols;
            return d;
        })
        .def_property_readonly("rows_written",       &ParquetWriter::rows_written)
        .def_property_readonly("row_groups_written",  &ParquetWriter::row_groups_written)
        .def_property_readonly("is_open",             &ParquetWriter::is_open)
        // context manager support
        .def("__enter__", [](ParquetWriter& w) -> ParquetWriter& { return w; })
        .def("__exit__",  [](ParquetWriter& w, py::object, py::object, py::object) {
            if (w.is_open()) (void)w.close();
        });

    // -----------------------------------------------------------------------
    // ParquetReader
    // -----------------------------------------------------------------------
    py::class_<ParquetReader>(m, "ParquetReader")
        .def_static("open", [](const std::string& path) {
            return unwrap(ParquetReader::open(path));
        }, py::arg("path"))

        .def("read_column_int32", [](ParquetReader& r, size_t rg, size_t col) {
            auto v = unwrap(r.read_column<int32_t>(rg, col));
            return py::array_t<int32_t>(v.size(), v.data());
        }, py::arg("row_group"), py::arg("col_index"))
        .def("read_column_int64", [](ParquetReader& r, size_t rg, size_t col) {
            auto v = unwrap(r.read_column<int64_t>(rg, col));
            return py::array_t<int64_t>(v.size(), v.data());
        }, py::arg("row_group"), py::arg("col_index"))
        .def("read_column_float", [](ParquetReader& r, size_t rg, size_t col) {
            auto v = unwrap(r.read_column<float>(rg, col));
            return py::array_t<float>(v.size(), v.data());
        }, py::arg("row_group"), py::arg("col_index"))
        .def("read_column_double", [](ParquetReader& r, size_t rg, size_t col) {
            auto v = unwrap(r.read_column<double>(rg, col));
            return py::array_t<double>(v.size(), v.data());
        }, py::arg("row_group"), py::arg("col_index"))
        .def("read_column_string", [](ParquetReader& r, size_t rg, size_t col) {
            auto v = unwrap(r.read_column_as_strings(rg, col));
            py::list out;
            for (auto& s : v) out.append(s);
            return out;
        }, py::arg("row_group"), py::arg("col_index"))
        .def("read_columns", [](ParquetReader& r, const std::vector<std::string>& names) {
            return unwrap(r.read_columns(names));
        }, py::arg("column_names"))
        .def("read_all", [](ParquetReader& r) {
            return unwrap(r.read_all());
        })
        .def_property_readonly("num_rows",       &ParquetReader::num_rows)
        .def_property_readonly("num_row_groups",  &ParquetReader::num_row_groups)
        .def_property_readonly("schema",          &ParquetReader::schema)
        .def_property_readonly("created_by",      &ParquetReader::created_by)
        .def("close", [](ParquetReader& r) { (void)r; /* reader closes on destruction */ })
        // context manager
        .def("__enter__", [](ParquetReader& r) -> ParquetReader& { return r; })
        .def("__exit__",  [](ParquetReader&, py::object, py::object, py::object) {});

    // -----------------------------------------------------------------------
    // Feature Store
    // -----------------------------------------------------------------------
    py::class_<FeatureGroupDef>(m, "FeatureGroupDef")
        .def(py::init<>())
        .def_readwrite("name",           &FeatureGroupDef::name)
        .def_readwrite("feature_names",  &FeatureGroupDef::feature_names)
        .def_readwrite("schema_version", &FeatureGroupDef::schema_version);

    py::class_<FeatureVector>(m, "FeatureVector")
        .def(py::init<>())
        .def_readwrite("entity_id",    &FeatureVector::entity_id)
        .def_readwrite("timestamp_ns", &FeatureVector::timestamp_ns)
        .def_readwrite("version",      &FeatureVector::version)
        .def_readwrite("values",       &FeatureVector::values)
        .def("__repr__", [](const FeatureVector& fv) {
            return "<FeatureVector entity='" + fv.entity_id +
                   "' ts=" + std::to_string(fv.timestamp_ns) +
                   " n=" + std::to_string(fv.values.size()) + ">";
        });

    py::class_<FeatureWriterOptions>(m, "FeatureWriterOptions")
        .def(py::init<>())
        .def_readwrite("output_dir",     &FeatureWriterOptions::output_dir)
        .def_readwrite("group",          &FeatureWriterOptions::group)
        .def_readwrite("row_group_size", &FeatureWriterOptions::row_group_size)
        .def_readwrite("file_prefix",    &FeatureWriterOptions::file_prefix);

    py::class_<FeatureWriter>(m, "FeatureWriter")
        .def_static("create", [](FeatureWriterOptions opts) {
            return unwrap(FeatureWriter::create(std::move(opts)));
        }, py::arg("options"))
        .def("write", [](FeatureWriter& fw, const FeatureVector& fv) {
            unwrap_void(fw.write(fv));
        }, py::arg("feature_vector"))
        .def("write_batch", [](FeatureWriter& fw, const std::vector<FeatureVector>& fvs) {
            unwrap_void(fw.write_batch(fvs));
        }, py::arg("feature_vectors"))
        .def("close", [](FeatureWriter& fw) { unwrap_void(fw.close()); })
        .def_property_readonly("rows_written",  &FeatureWriter::rows_written)
        .def_property_readonly("output_files",  &FeatureWriter::output_files)
        .def("__enter__", [](FeatureWriter& fw) -> FeatureWriter& { return fw; })
        .def("__exit__",  [](FeatureWriter& fw, py::object, py::object, py::object) {
            if (fw.is_open()) (void)fw.close();
        });

    py::class_<FeatureReaderOptions>(m, "FeatureReaderOptions")
        .def(py::init<>())
        .def_readwrite("parquet_files", &FeatureReaderOptions::parquet_files)
        .def_readwrite("feature_group", &FeatureReaderOptions::feature_group);

    py::class_<FeatureReader>(m, "FeatureReader")
        .def_static("open", [](FeatureReaderOptions opts) {
            return unwrap(FeatureReader::open(std::move(opts)));
        }, py::arg("options"))
        .def("get", [](FeatureReader& fr, const std::string& entity) -> py::object {
            auto r = unwrap(fr.get(entity));
            if (!r) return py::none();
            return py::cast(*r);
        }, py::arg("entity_id"))
        .def("as_of", [](FeatureReader& fr, const std::string& entity,
                          int64_t ts) -> py::object {
            auto r = unwrap(fr.as_of(entity, ts));
            if (!r) return py::none();
            return py::cast(*r);
        }, py::arg("entity_id"), py::arg("timestamp_ns"))
        .def("as_of_batch", [](FeatureReader& fr,
                                const std::vector<std::string>& ids, int64_t ts) {
            return unwrap(fr.as_of_batch(ids, ts));
        }, py::arg("entity_ids"), py::arg("timestamp_ns"))
        .def("history", [](FeatureReader& fr, const std::string& id,
                            int64_t start, int64_t end) {
            return unwrap(fr.history(id, start, end));
        }, py::arg("entity_id"), py::arg("start_ns"), py::arg("end_ns"))
        .def_property_readonly("feature_names", &FeatureReader::feature_names)
        .def_property_readonly("num_entities",  &FeatureReader::num_entities)
        .def_property_readonly("total_rows",    &FeatureReader::total_rows);

#ifdef SIGNET_HAS_AI_AUDIT
    // -----------------------------------------------------------------------
    // AI Audit & Compliance tier (BSL 1.1 — see LICENSE_COMMERCIAL)
    // -----------------------------------------------------------------------

    // Decision Log
    py::enum_<DecisionType>(m, "DecisionType")
        .value("SIGNAL",         DecisionType::SIGNAL)
        .value("ORDER_NEW",      DecisionType::ORDER_NEW)
        .value("ORDER_CANCEL",   DecisionType::ORDER_CANCEL)
        .value("ORDER_MODIFY",   DecisionType::ORDER_MODIFY)
        .value("POSITION_OPEN",  DecisionType::POSITION_OPEN)
        .value("POSITION_CLOSE", DecisionType::POSITION_CLOSE)
        .value("RISK_OVERRIDE",  DecisionType::RISK_OVERRIDE)
        .value("NO_ACTION",      DecisionType::NO_ACTION)
        .export_values();

    py::enum_<RiskGateResult>(m, "RiskGateResult")
        .value("PASSED",    RiskGateResult::PASSED)
        .value("REJECTED",  RiskGateResult::REJECTED)
        .value("MODIFIED",  RiskGateResult::MODIFIED)
        .value("THROTTLED", RiskGateResult::THROTTLED)
        .export_values();

    py::class_<DecisionRecord>(m, "DecisionRecord")
        .def(py::init<>())
        .def_readwrite("timestamp_ns",   &DecisionRecord::timestamp_ns)
        .def_readwrite("strategy_id",    &DecisionRecord::strategy_id)
        .def_readwrite("model_version",  &DecisionRecord::model_version)
        .def_readwrite("decision_type",  &DecisionRecord::decision_type)
        .def_readwrite("input_features", &DecisionRecord::input_features)
        .def_readwrite("model_output",   &DecisionRecord::model_output)
        .def_readwrite("confidence",     &DecisionRecord::confidence)
        .def_readwrite("risk_result",    &DecisionRecord::risk_result)
        .def_readwrite("order_id",       &DecisionRecord::order_id)
        .def_readwrite("symbol",         &DecisionRecord::symbol)
        .def_readwrite("price",          &DecisionRecord::price)
        .def_readwrite("quantity",       &DecisionRecord::quantity)
        .def_readwrite("venue",          &DecisionRecord::venue)
        .def_readwrite("notes",          &DecisionRecord::notes);

    py::class_<DecisionLogWriter>(m, "DecisionLogWriter")
        .def(py::init<std::string, std::string>(),
             py::arg("output_dir"), py::arg("chain_id"))
        .def("log", [](DecisionLogWriter& w, const DecisionRecord& r) {
            (void)unwrap(w.log(r));
        }, py::arg("record"))
        .def("close", [](DecisionLogWriter& w) { unwrap_void(w.close()); })
        .def("current_file_path", &DecisionLogWriter::current_file_path)
        .def("__enter__", [](DecisionLogWriter& w) -> DecisionLogWriter& { return w; })
        .def("__exit__",  [](DecisionLogWriter& w, py::object, py::object, py::object) {
            (void)w.close();
        });

    // -----------------------------------------------------------------------
    // Inference Log
    // -----------------------------------------------------------------------
    py::enum_<InferenceType>(m, "InferenceType")
        .value("CLASSIFICATION", InferenceType::CLASSIFICATION)
        .value("REGRESSION",     InferenceType::REGRESSION)
        .value("GENERATION",     InferenceType::GENERATION)
        .value("EMBEDDING",      InferenceType::EMBEDDING)
        .value("RANKING",        InferenceType::RANKING)
        .export_values();

    py::class_<InferenceRecord>(m, "InferenceRecord")
        .def(py::init<>())
        .def_readwrite("timestamp_ns",    &InferenceRecord::timestamp_ns)
        .def_readwrite("model_id",        &InferenceRecord::model_id)
        .def_readwrite("model_version",   &InferenceRecord::model_version)
        .def_readwrite("inference_type",  &InferenceRecord::inference_type)
        .def_readwrite("input_embedding", &InferenceRecord::input_embedding)
        .def_readwrite("input_hash",      &InferenceRecord::input_hash)
        .def_readwrite("output_hash",     &InferenceRecord::output_hash)
        .def_readwrite("output_score",    &InferenceRecord::output_score)
        .def_readwrite("latency_ns",      &InferenceRecord::latency_ns)
        .def_readwrite("batch_size",      &InferenceRecord::batch_size)
        .def_readwrite("input_tokens",    &InferenceRecord::input_tokens)
        .def_readwrite("output_tokens",   &InferenceRecord::output_tokens)
        .def_readwrite("user_id_hash",    &InferenceRecord::user_id_hash)
        .def_readwrite("session_id",      &InferenceRecord::session_id)
        .def_readwrite("metadata_json",   &InferenceRecord::metadata_json);

    py::class_<InferenceLogWriter>(m, "InferenceLogWriter")
        .def(py::init<std::string, std::string>(),
             py::arg("output_dir"), py::arg("chain_id"))
        .def("log", [](InferenceLogWriter& w, const InferenceRecord& r) {
            (void)unwrap(w.log(r));
        }, py::arg("record"))
        .def("close", [](InferenceLogWriter& w) { unwrap_void(w.close()); })
        .def("current_file_path", &InferenceLogWriter::current_file_path)
        .def("__enter__", [](InferenceLogWriter& w) -> InferenceLogWriter& { return w; })
        .def("__exit__",  [](InferenceLogWriter& w, py::object, py::object, py::object) {
            (void)w.close();
        });

    // -----------------------------------------------------------------------
    // Compliance Reports
    // -----------------------------------------------------------------------
    py::enum_<ReportFormat>(m, "ReportFormat")
        .value("JSON",   ReportFormat::JSON)
        .value("NDJSON", ReportFormat::NDJSON)
        .value("CSV",    ReportFormat::CSV)
        .export_values();

    py::enum_<ComplianceStandard>(m, "ComplianceStandard")
        .value("MIFID2_RTS24",    ComplianceStandard::MIFID2_RTS24)
        .value("EU_AI_ACT_ART12", ComplianceStandard::EU_AI_ACT_ART12)
        .value("EU_AI_ACT_ART13", ComplianceStandard::EU_AI_ACT_ART13)
        .value("EU_AI_ACT_ART19", ComplianceStandard::EU_AI_ACT_ART19)
        .export_values();

    py::class_<ReportOptions>(m, "ReportOptions")
        .def(py::init<>())
        .def_readwrite("start_ns",                  &ReportOptions::start_ns)
        .def_readwrite("end_ns",                    &ReportOptions::end_ns)
        .def_readwrite("format",                    &ReportOptions::format)
        .def_readwrite("verify_chain",              &ReportOptions::verify_chain)
        .def_readwrite("include_features",          &ReportOptions::include_features)
        .def_readwrite("pretty_print",              &ReportOptions::pretty_print)
        .def_readwrite("system_id",                 &ReportOptions::system_id)
        .def_readwrite("report_id",                 &ReportOptions::report_id)
        .def_readwrite("firm_id",                   &ReportOptions::firm_id)
        .def_readwrite("low_confidence_threshold",  &ReportOptions::low_confidence_threshold);

    py::class_<ComplianceReport>(m, "ComplianceReport")
        .def_readonly("standard",          &ComplianceReport::standard)
        .def_readonly("format",            &ComplianceReport::format)
        .def_readonly("content",           &ComplianceReport::content)
        .def_readonly("report_id",         &ComplianceReport::report_id)
        .def_readonly("generated_at_iso",  &ComplianceReport::generated_at_iso)
        .def_readonly("generated_at_ns",   &ComplianceReport::generated_at_ns)
        .def_readonly("total_records",     &ComplianceReport::total_records)
        .def_readonly("chain_verified",    &ComplianceReport::chain_verified)
        .def_readonly("chain_id",          &ComplianceReport::chain_id)
        .def_readonly("period_start_iso",  &ComplianceReport::period_start_iso)
        .def_readonly("period_end_iso",    &ComplianceReport::period_end_iso)
        .def("__repr__", [](const ComplianceReport& r) {
            return "<ComplianceReport records=" + std::to_string(r.total_records) +
                   " chain_ok=" + (r.chain_verified ? "True" : "False") + ">";
        });

    py::class_<MiFID2Reporter>(m, "MiFID2Reporter")
        .def_static("generate", [](const std::vector<std::string>& files,
                                    const ReportOptions& opts) {
            return unwrap(MiFID2Reporter::generate(files, opts));
        }, py::arg("log_files"), py::arg("options") = ReportOptions{})
        .def_static("csv_header", &MiFID2Reporter::csv_header);

    py::class_<EUAIActReporter>(m, "EUAIActReporter")
        .def_static("generate_article12", [](const std::vector<std::string>& files,
                                              const ReportOptions& opts) {
            return unwrap(EUAIActReporter::generate_article12(files, opts));
        }, py::arg("inference_log_files"), py::arg("options") = ReportOptions{})
        .def_static("generate_article13", [](const std::vector<std::string>& files,
                                              const ReportOptions& opts) {
            return unwrap(EUAIActReporter::generate_article13(files, opts));
        }, py::arg("inference_log_files"), py::arg("options") = ReportOptions{})
        .def_static("generate_article19", [](const std::vector<std::string>& dec_files,
                                              const std::vector<std::string>& inf_files,
                                              const ReportOptions& opts) {
            return unwrap(EUAIActReporter::generate_article19(dec_files, inf_files, opts));
        }, py::arg("decision_log_files"), py::arg("inference_log_files"),
           py::arg("options") = ReportOptions{});

#endif // SIGNET_HAS_AI_AUDIT

    // -----------------------------------------------------------------------
    // Version info
    // -----------------------------------------------------------------------
    m.attr("__version__")    = "0.1.0";
    m.attr("__author__")     = "Signet Stack";
    m.attr("SIGNET_VERSION") = SIGNET_CREATED_BY;
}
