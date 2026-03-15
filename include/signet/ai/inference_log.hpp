// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/inference_log.hpp requires SIGNET_ENABLE_COMMERCIAL=ON (AGPL-3.0 commercial tier). See LICENSE_COMMERCIAL."
#endif

// ---------------------------------------------------------------------------
// inference_log.hpp -- LLM/ML Inference Audit Log
//
// Logs model inference operations with cryptographic hash chaining for
// regulatory compliance and operational auditing. Captures inference
// metadata (latency, batch size, token counts) without storing raw
// inputs/outputs (only their SHA-256 hashes for privacy).
//
// Regulatory requirements addressed:
//   - EU AI Act Art 12/19: Automatic logging of AI system operations
//   - GDPR Art 35:        Data protection impact — input/output hashing
//   - SEC 17a-4:          Tamper-evident records retention
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/ai/audit_chain.hpp"
#include "signet/ai/row_lineage.hpp"
#include "signet/error.hpp"
#include "signet/schema.hpp"
#include "signet/types.hpp"
#include "signet/writer.hpp"
#include "signet/reader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace signet::forge {

/// Classification of the ML inference operation.
/// Covers common ML workloads from classical models to LLM generation.
enum class InferenceType : int32_t {
    CLASSIFICATION = 0,   ///< Binary or multi-class classification
    REGRESSION     = 1,   ///< Continuous value prediction
    EMBEDDING      = 2,   ///< Vector embedding computation
    GENERATION     = 3,   ///< LLM text generation
    RANKING        = 4,   ///< Ranking/scoring of candidates
    ANOMALY        = 5,   ///< Anomaly/outlier detection
    RECOMMENDATION = 6,   ///< Recommendation system inference
    CUSTOM         = 255  ///< Application-specific inference type
};

/// A single ML inference event with full operational metadata.
///
/// Captures everything needed to audit and reproduce an inference:
/// model identity, timing, resource usage, and privacy-preserving
/// hashes of inputs and outputs. Raw data is never stored — only
/// SHA-256 hashes — to comply with GDPR data minimization.
struct InferenceRecord {
    int64_t              timestamp_ns{0};      ///< Inference timestamp (nanoseconds since epoch)
    std::string          model_id;             ///< Model identifier (e.g., "gpt-4", "bert-base")
    std::string          model_version;        ///< Model version hash or checkpoint ID
    InferenceType        inference_type{InferenceType::CLASSIFICATION}; ///< Type of inference
    std::vector<float>   input_embedding;      ///< Input embedding (optional, may be empty)
    std::string          input_hash;           ///< SHA-256 hash of raw input (for privacy)
    std::string          output_hash;          ///< SHA-256 hash of raw output
    float                output_score{0.0f};   ///< Primary output score/probability
    int64_t              latency_ns{0};        ///< Inference latency in nanoseconds
    int32_t              batch_size{1};        ///< Batch size
    int32_t              input_tokens{0};      ///< Input token count (LLM, 0 if N/A)
    int32_t              output_tokens{0};     ///< Output token count (LLM, 0 if N/A)
    std::string          user_id_hash;         ///< Hashed user ID (for privacy)
    std::string          session_id;           ///< Session identifier
    std::string          metadata_json;        ///< Additional JSON metadata

    // EU AI Act Art.13(3)(b)(ii): training data provenance for high-risk AI
    // transparency — deployers must be able to identify the data used to train
    // the model that produced each inference.
    std::string          training_dataset_id;           ///< Training data identifier
    int64_t              training_dataset_size{0};      ///< Number of samples in training dataset
    std::string          training_data_characteristics; ///< Description of training data properties

    // EU AI Act Art.12/13: additional training metadata (M-18)
    int64_t              model_training_end_ns{0};          ///< Timestamp when model training completed (EU AI Act Art.12)
    int64_t              model_training_data_cutoff_ns{0};  ///< Latest data timestamp used in training
    std::string          model_retraining_schedule;         ///< Cron or description of retraining schedule (EU AI Act Art.13)

    /// Serialize the record to a deterministic byte sequence.
    /// Format: each field written sequentially as little-endian values.
    /// Strings: 4-byte LE length prefix + raw bytes.
    /// Vectors: 4-byte LE count + float data (4 bytes each, LE).
    [[nodiscard]] inline std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf;
        buf.reserve(256);

        // timestamp_ns: 8 bytes LE
        append_le64(buf, static_cast<uint64_t>(timestamp_ns));

        // model_id: string
        append_string(buf, model_id);

        // model_version: string
        append_string(buf, model_version);

        // inference_type: 4 bytes LE
        append_le32(buf, static_cast<uint32_t>(inference_type));

        // input_embedding: vector of floats
        append_le32(buf, static_cast<uint32_t>(input_embedding.size()));
        for (float f : input_embedding) {
            append_float(buf, f);
        }

        // input_hash: string
        append_string(buf, input_hash);

        // output_hash: string
        append_string(buf, output_hash);

        // output_score: float (4 bytes)
        append_float(buf, output_score);

        // latency_ns: 8 bytes LE
        append_le64(buf, static_cast<uint64_t>(latency_ns));

        // batch_size: 4 bytes LE
        append_le32(buf, static_cast<uint32_t>(batch_size));

        // input_tokens: 4 bytes LE
        append_le32(buf, static_cast<uint32_t>(input_tokens));

        // output_tokens: 4 bytes LE
        append_le32(buf, static_cast<uint32_t>(output_tokens));

        // user_id_hash: string
        append_string(buf, user_id_hash);

        // session_id: string
        append_string(buf, session_id);

        // metadata_json: string
        append_string(buf, metadata_json);

        // M31: EU AI Act Art.13(3)(b)(ii) training data provenance fields
        append_string(buf, training_dataset_id);
        append_le64(buf, static_cast<uint64_t>(training_dataset_size));
        append_string(buf, training_data_characteristics);

        // H-19: EU AI Act Art.12/13 additional training metadata
        append_le64(buf, static_cast<uint64_t>(model_training_end_ns));
        append_le64(buf, static_cast<uint64_t>(model_training_data_cutoff_ns));
        append_string(buf, model_retraining_schedule);

        return buf;
    }

    /// Reconstruct an InferenceRecord from its serialized byte representation.
    ///
    /// @param data  Pointer to the serialized bytes.
    /// @param size  Number of bytes available at @p data.
    /// @return The deserialized record, or an error if the data is truncated.
    [[nodiscard]] static inline expected<InferenceRecord> deserialize(
            const uint8_t* data, size_t size) {
        size_t offset = 0;
        InferenceRecord rec;

        if (!read_le64(data, size, offset, rec.timestamp_ns)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated timestamp_ns"};
        }
        if (!read_string(data, size, offset, rec.model_id)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated model_id"};
        }
        if (!read_string(data, size, offset, rec.model_version)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated model_version"};
        }

        int32_t it_val = 0;
        if (!read_le32(data, size, offset, it_val)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated inference_type"};
        }
        rec.inference_type = static_cast<InferenceType>(it_val);
        if (it_val < 0 || (it_val > 6 && it_val != 255))
            rec.inference_type = InferenceType::CLASSIFICATION;

        uint32_t emb_count = 0;
        if (!read_le32_u(data, size, offset, emb_count)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated embedding count"};
        }
        if (offset > size || emb_count > (size - offset) / sizeof(float)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: embedding count exceeds remaining data"};
        }
        rec.input_embedding.resize(emb_count);
        for (uint32_t i = 0; i < emb_count; ++i) {
            if (!read_float(data, size, offset, rec.input_embedding[i])) {
                return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated embedding data"};
            }
        }

        if (!read_string(data, size, offset, rec.input_hash)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated input_hash"};
        }
        if (!read_string(data, size, offset, rec.output_hash)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated output_hash"};
        }
        if (!read_float(data, size, offset, rec.output_score)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated output_score"};
        }
        if (!read_le64(data, size, offset, rec.latency_ns)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated latency_ns"};
        }
        if (!read_le32(data, size, offset, rec.batch_size)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated batch_size"};
        }
        if (!read_le32(data, size, offset, rec.input_tokens)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated input_tokens"};
        }
        if (!read_le32(data, size, offset, rec.output_tokens)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated output_tokens"};
        }
        if (!read_string(data, size, offset, rec.user_id_hash)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated user_id_hash"};
        }
        if (!read_string(data, size, offset, rec.session_id)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated session_id"};
        }
        if (!read_string(data, size, offset, rec.metadata_json)) {
            return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated metadata_json"};
        }

        // M31: EU AI Act Art.13(3)(b)(ii) training data provenance fields
        // These fields are optional for backward compatibility with older serialized data.
        if (offset < size) {
            if (!read_string(data, size, offset, rec.training_dataset_id)) {
                return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated training_dataset_id"};
            }
            if (!read_le64(data, size, offset, rec.training_dataset_size)) {
                return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated training_dataset_size"};
            }
            if (!read_string(data, size, offset, rec.training_data_characteristics)) {
                return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated training_data_characteristics"};
            }
        }

        // H-19: EU AI Act Art.12/13 additional training metadata (optional for backward compat)
        if (offset < size) {
            if (!read_le64(data, size, offset, rec.model_training_end_ns)) {
                return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated model_training_end_ns"};
            }
            if (!read_le64(data, size, offset, rec.model_training_data_cutoff_ns)) {
                return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated model_training_data_cutoff_ns"};
            }
            if (!read_string(data, size, offset, rec.model_retraining_schedule)) {
                return Error{ErrorCode::CORRUPT_PAGE, "InferenceRecord: truncated model_retraining_schedule"};
            }
        }

        return rec;
    }

private:
    // -- Serialization helpers -----------------------------------------------

    static inline void append_le32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v));
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 24));
    }

    static inline void append_le64(std::vector<uint8_t>& buf, uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
        }
    }

    static inline void append_float(std::vector<uint8_t>& buf, float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        append_le32(buf, bits);
    }

    static inline void append_string(std::vector<uint8_t>& buf, const std::string& s) {
        // Clamp string size to UINT32_MAX to prevent truncation on cast
        const size_t clamped = (std::min)(s.size(), static_cast<size_t>(UINT32_MAX));
        append_le32(buf, static_cast<uint32_t>(clamped));
        buf.insert(buf.end(), s.begin(), s.begin() + static_cast<ptrdiff_t>(clamped));
    }

    // -- Deserialization helpers -----------------------------------------------

    static inline bool read_le64(const uint8_t* data, size_t size, size_t& offset, int64_t& out) {
        if (offset + 8 > size) return false;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        }
        out = static_cast<int64_t>(v);
        offset += 8;
        return true;
    }

    static inline bool read_le32(const uint8_t* data, size_t size, size_t& offset, int32_t& out) {
        if (offset + 4 > size) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        }
        out = static_cast<int32_t>(v);
        offset += 4;
        return true;
    }

    static inline bool read_le32_u(const uint8_t* data, size_t size, size_t& offset, uint32_t& out) {
        if (offset + 4 > size) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            out |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        }
        offset += 4;
        return true;
    }

    static inline bool read_float(const uint8_t* data, size_t size, size_t& offset, float& out) {
        if (offset + 4 > size) return false;
        uint32_t bits = 0;
        for (int i = 0; i < 4; ++i) {
            bits |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        }
        std::memcpy(&out, &bits, 4);
        offset += 4;
        return true;
    }

    static constexpr uint32_t MAX_STRING_LEN = 16u * 1024u * 1024u; // 16 MB

    static inline bool read_string(const uint8_t* data, size_t size, size_t& offset, std::string& out) {
        uint32_t len = 0;
        if (!read_le32_u(data, size, offset, len)) return false;
        if (len > MAX_STRING_LEN) return false;
        if (offset + len > size) return false;
        out.assign(reinterpret_cast<const char*>(data + offset), len);
        offset += len;
        return true;
    }
};

namespace detail {

/// Encode a float embedding vector as a JSON array string (e.g. "[1.5,2.3,-0.1]").
///
/// Same pattern as features_to_json() in decision_log.hpp. Defined as a
/// separate inline to avoid ODR issues when both headers are included.
///
/// @param embedding  The embedding vector to encode.
/// @return A JSON array string, or "[]" if empty.
inline std::string embedding_to_json(const std::vector<float>& embedding) {
    if (embedding.empty()) return "[]";

    std::string result = "[";
    for (size_t i = 0; i < embedding.size(); ++i) {
        if (i > 0) result += ',';
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.8g", static_cast<double>(embedding[i]));
        result += buf;
    }
    result += ']';
    return result;
}

/// Decode a JSON array string (e.g. "[1.5,2.3,-0.1]") to a float vector.
///
/// Unparseable values are silently skipped.
///
/// @param json  The JSON array string to decode.
/// @return The parsed float vector, or empty if the input is malformed.
inline std::vector<float> json_to_embedding(const std::string& json) {
    // L25: limit parsed elements to prevent memory exhaustion from crafted input
    static constexpr size_t MAX_JSON_ARRAY_ELEMENTS = 1'000'000;
    std::vector<float> result;
    if (json.size() < 2 || json.front() != '[' || json.back() != ']') {
        return result;
    }

    size_t pos = 1;
    size_t end = json.size() - 1;

    while (pos < end) {
        if (result.size() >= MAX_JSON_ARRAY_ELEMENTS) break;
        while (pos < end && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        if (pos >= end) break;

        size_t num_start = pos;
        while (pos < end && json[pos] != ',') ++pos;

        std::string num_str = json.substr(num_start, pos - num_start);
        while (!num_str.empty() && (num_str.back() == ' ' || num_str.back() == '\t')) {
            num_str.pop_back();
        }
        if (!num_str.empty()) {
            try {
                result.push_back(std::stof(num_str));
            } catch (...) {
                // Skip unparseable values
            }
        }

        if (pos < end && json[pos] == ',') ++pos;
    }

    return result;
}

} // namespace detail

/// Build the Parquet schema for ML inference log files.
///
/// Columns:
///   timestamp_ns     INT64  (TIMESTAMP_NS)   -- Inference timestamp
///   model_id         BYTE_ARRAY (STRING)      -- Model identifier
///   model_version    BYTE_ARRAY (STRING)      -- Model version hash
///   inference_type   INT32                    -- InferenceType enum value
///   input_embedding  BYTE_ARRAY (STRING)      -- JSON array of floats
///   input_hash       BYTE_ARRAY (STRING)      -- SHA-256 of raw input
///   output_hash      BYTE_ARRAY (STRING)      -- SHA-256 of raw output
///   output_score     DOUBLE                   -- Primary output score
///   latency_ns       INT64                    -- Inference latency (ns)
///   batch_size       INT32                    -- Batch size
///   input_tokens     INT32                    -- Input token count
///   output_tokens    INT32                    -- Output token count
///   user_id_hash     BYTE_ARRAY (STRING)      -- Hashed user ID
///   session_id       BYTE_ARRAY (STRING)      -- Session identifier
///   metadata_json    BYTE_ARRAY (STRING)      -- Additional JSON metadata
///   chain_seq        INT64                    -- Hash chain sequence number
///   chain_hash       BYTE_ARRAY (STRING)      -- Hex entry hash
///   prev_hash        BYTE_ARRAY (STRING)      -- Hex previous hash
[[nodiscard]] inline Schema inference_log_schema() {
    return Schema::builder("inference_log")
        .column<int64_t>("timestamp_ns", LogicalType::TIMESTAMP_NS)
        .column<std::string>("model_id")
        .column<std::string>("model_version")
        .column<int32_t>("inference_type")
        .column<std::string>("input_embedding")  // JSON array
        .column<std::string>("input_hash")
        .column<std::string>("output_hash")
        .column<double>("output_score")
        .column<int64_t>("latency_ns")
        .column<int32_t>("batch_size")
        .column<int32_t>("input_tokens")
        .column<int32_t>("output_tokens")
        .column<std::string>("user_id_hash")
        .column<std::string>("session_id")
        .column<std::string>("metadata_json")
        .column<int64_t>("chain_seq")
        .column<std::string>("chain_hash")
        .column<std::string>("prev_hash")
        .column<int64_t>("row_id")
        .column<int32_t>("row_version")
        .column<std::string>("row_origin_file")
        .column<std::string>("row_prev_hash")
        .build();
}

/// Writes ML inference records to Parquet files with cryptographic
/// hash chaining for tamper-evident audit trails.
///
/// Usage:
///   InferenceLogWriter writer("/audit/inference");
///   InferenceRecord rec;
///   rec.timestamp_ns  = now_ns();
///   rec.model_id      = "bert-base-v2";
///   rec.model_version = "a1b2c3d4";
///   rec.inference_type = InferenceType::CLASSIFICATION;
///   rec.latency_ns    = 12500000;  // 12.5ms
///   // ... fill other fields ...
///   auto entry = writer.log(rec);
///   writer.close();
///
/// Files are named: inference_log_{chain_id}_{start_seq}_{end_seq}.parquet
/// Each file embeds AuditMetadata in the Parquet key-value metadata for
/// chain continuity verification across file rotations.
class InferenceLogWriter {
public:
    /// Create an inference log writer.
    /// @param output_dir   Directory to write Parquet files to
    /// @param chain_id     Unique chain identifier (auto-generated if empty)
    /// @param max_records  Max records per file before rotating (default 100,000)
    inline InferenceLogWriter(const std::string& output_dir,
                              const std::string& chain_id = "",
                              size_t max_records = 100000)
        : output_dir_(output_dir)
        , chain_id_(chain_id.empty() ? generate_chain_id() : chain_id)
        , max_records_(max_records)
        , schema_(inference_log_schema())
        , lineage_tracker_(chain_id.empty() ? chain_id_ : chain_id, 1)
    {
        auto license = commercial::require_feature("InferenceLogWriter");
        if (!license) {
            throw std::runtime_error(license.error().message);
        }

        // Keep constructor hardening in parity with DecisionLogWriter.
        if (output_dir_.empty()) {
            throw std::invalid_argument("InferenceLogWriter: output_dir must not be empty");
        }
        for (size_t s = 0, e; s <= output_dir_.size(); s = e + 1) {
            e = output_dir_.find_first_of("/\\", s);
            if (e == std::string::npos) e = output_dir_.size();
            if (output_dir_.substr(s, e - s) == "..") {
                throw std::invalid_argument(
                    "InferenceLogWriter: output_dir must not contain '..' path traversal");
            }
        }
        for (char c : chain_id_) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
                throw std::invalid_argument(
                    "InferenceLogWriter: chain_id must only contain [a-zA-Z0-9_-]");
            }
        }
    }

    /// Log an inference event. Returns the hash chain entry.
    ///
    /// The record is serialized, hashed, and appended to the hash chain.
    /// If the pending buffer reaches max_records, the file is automatically
    /// flushed and rotated.
    [[nodiscard]] inline expected<HashChainEntry> log(const InferenceRecord& record) {
        auto usage = commercial::record_usage_rows("InferenceLogWriter::log", 1);
        if (!usage) return usage.error();

        // Serialize the record for hashing
        auto data = record.serialize();

        // Use the record's timestamp, or current time if zero
        int64_t ts = record.timestamp_ns;
        if (ts == 0) {
            ts = now_ns();
        }

        // Append to hash chain
        auto entry = chain_.append(data.data(), data.size(), ts);

        // Store the record, chain entry, and serialized data for lineage
        pending_records_.push_back(record);
        pending_entries_.push_back(entry);
        pending_data_.push_back(std::move(data));

        // Auto-flush if we've reached the rotation threshold
        if (pending_records_.size() >= max_records_) {
            auto result = flush();
            if (!result) return result.error();
        }

        ++total_records_;
        return entry;
    }

    /// Flush current records to a Parquet file.
    ///
    /// Writes all pending records to a new Parquet file with the schema
    /// defined by inference_log_schema(). The hash chain metadata is
    /// embedded in the file's key-value metadata section.
    [[nodiscard]] inline expected<void> flush() {
        if (pending_records_.empty()) {
            return expected<void>{};
        }

        // Build file path: inference_log_{chain_id}_{start_seq}_{end_seq}.parquet
        int64_t start_seq = pending_entries_.front().sequence_number;
        int64_t end_seq   = pending_entries_.back().sequence_number;

        current_file_path_ = output_dir_ + "/inference_log_" + chain_id_ + "_"
                           + std::to_string(start_seq) + "_"
                           + std::to_string(end_seq) + ".parquet";

        // Prepare writer options with audit metadata
        WriterOptions opts;
        opts.created_by = "SignetStack signet-forge inference_log v1.0";

        // Embed audit chain metadata
        auto meta = current_metadata();
        auto meta_kvs = meta.to_key_values();
        for (auto& [k, v] : meta_kvs) {
            opts.file_metadata.push_back(thrift::KeyValue(std::move(k), std::move(v)));
        }

        // Open the Parquet file
        auto writer_result = ParquetWriter::open(current_file_path_, schema_, opts);
        if (!writer_result) return writer_result.error();
        auto& writer = *writer_result;

        // Write each record as a row
        size_t n = pending_records_.size();
        for (size_t i = 0; i < n; ++i) {
            const auto& rec   = pending_records_[i];
            const auto& entry = pending_entries_[i];

            // Compute per-row lineage from serialized data
            const auto& row_data = pending_data_[i];
            auto lineage = lineage_tracker_.next(row_data.data(), row_data.size());

            std::vector<std::string> row;
            row.reserve(22);

            row.push_back(std::to_string(rec.timestamp_ns));
            row.push_back(rec.model_id);
            row.push_back(rec.model_version);
            row.push_back(std::to_string(static_cast<int32_t>(rec.inference_type)));
            row.push_back(detail::embedding_to_json(rec.input_embedding));
            row.push_back(rec.input_hash);
            row.push_back(rec.output_hash);
            row.push_back(double_to_string(static_cast<double>(rec.output_score)));
            row.push_back(std::to_string(rec.latency_ns));
            row.push_back(std::to_string(rec.batch_size));
            row.push_back(std::to_string(rec.input_tokens));
            row.push_back(std::to_string(rec.output_tokens));
            row.push_back(rec.user_id_hash);
            row.push_back(rec.session_id);
            row.push_back(rec.metadata_json);
            row.push_back(std::to_string(entry.sequence_number));
            row.push_back(hash_to_hex(entry.entry_hash));
            row.push_back(hash_to_hex(entry.prev_hash));
            row.push_back(std::to_string(lineage.row_id));
            row.push_back(std::to_string(lineage.row_version));
            row.push_back(lineage.row_origin_file);
            row.push_back(lineage.row_prev_hash);

            auto write_result = writer.write_row(row);
            if (!write_result) return write_result.error();
        }

        // Close the file (flushes internal buffers and writes footer)
        auto close_result = writer.close();
        if (!close_result) return close_result.error();

        // Clear pending buffers
        pending_records_.clear();
        pending_entries_.clear();
        pending_data_.clear();
        ++file_count_;

        return expected<void>{};
    }

    /// Close the writer (flushes remaining records).
    [[nodiscard]] inline expected<void> close() {
        if (!pending_records_.empty()) {
            return flush();
        }
        return expected<void>{};
    }

    /// Get the chain metadata for the current batch.
    ///
    /// Returns metadata describing the hash chain state, including the
    /// chain ID, first/last sequence numbers, and boundary hashes.
    [[nodiscard]] inline AuditMetadata current_metadata() const {
        AuditMetadata meta;
        meta.chain_id = chain_id_;

        if (!pending_entries_.empty()) {
            meta.start_sequence = pending_entries_.front().sequence_number;
            meta.end_sequence  = pending_entries_.back().sequence_number;
            meta.first_hash     = hash_to_hex(pending_entries_.front().entry_hash);
            meta.last_hash      = hash_to_hex(pending_entries_.back().entry_hash);
            meta.prev_file_hash = hash_to_hex(pending_entries_.front().prev_hash);
        } else if (!chain_.entries().empty()) {
            const auto& last = chain_.entries().back();
            meta.start_sequence = last.sequence_number;
            meta.end_sequence  = last.sequence_number;
            meta.first_hash     = hash_to_hex(last.entry_hash);
            meta.last_hash      = hash_to_hex(last.entry_hash);
        }

        meta.record_count   = static_cast<int64_t>(pending_entries_.size());
        meta.record_type    = "inference";
        return meta;
    }

    /// Get the number of records in the current (unflushed) batch.
    [[nodiscard]] inline size_t pending_records() const {
        return pending_records_.size();
    }

    /// Get the total number of records written across all files.
    [[nodiscard]] inline int64_t total_records() const {
        return total_records_;
    }

    /// Get the file path of the current (or last written) output file.
    [[nodiscard]] inline std::string current_file_path() const {
        return current_file_path_;
    }

private:
    std::string                          output_dir_;
    std::string                          chain_id_;
    size_t                               max_records_;
    Schema                               schema_;
    AuditChainWriter                     chain_;
    std::vector<InferenceRecord>         pending_records_;
    std::vector<HashChainEntry>          pending_entries_;
    std::vector<std::vector<uint8_t>>    pending_data_;  // Serialized record bytes for lineage hashing
    RowLineageTracker                    lineage_tracker_;
    std::string                          current_file_path_;
    int64_t                              total_records_{0};
    int64_t                              file_count_{0};

    /// Format a double to string with enough precision for round-tripping.
    static inline std::string double_to_string(double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        return buf;
    }
};

/// Reads ML inference log Parquet files and verifies hash chain integrity.
///
/// Usage:
///   auto reader = InferenceLogReader::open("inference_log_abc123_0_999.parquet");
///   auto records = reader->read_all();
///   auto verify  = reader->verify_chain();
///   if (!verify.valid) { /* tampered! */ }
class InferenceLogReader {
public:
    /// Open an inference log Parquet file and pre-load all column data.
    ///
    /// @param path  Path to the inference log Parquet file.
    /// @return The reader, or an error if the file cannot be opened or parsed.
    [[nodiscard]] static inline expected<InferenceLogReader> open(const std::string& path) {
        auto license = commercial::require_feature("InferenceLogReader");
        if (!license) return license.error();

        auto reader_result = ParquetReader::open(path);
        if (!reader_result) return reader_result.error();

        InferenceLogReader ilr;
        ilr.reader_ = std::make_unique<ParquetReader>(std::move(*reader_result));
        ilr.path_   = path;

        // Pre-read all column data from all row groups
        auto load_result = ilr.load_columns();
        if (!load_result) return load_result.error();

        return ilr;
    }

    /// @name Move-only semantics
    /// @{
    InferenceLogReader() = default;
    InferenceLogReader(const InferenceLogReader&) = delete;
    InferenceLogReader& operator=(const InferenceLogReader&) = delete;
    InferenceLogReader(InferenceLogReader&&) = default;
    InferenceLogReader& operator=(InferenceLogReader&&) = default;
    /// @}

    /// Get all inference records from the file.
    ///
    /// @return A vector of InferenceRecord objects reconstructed from all row groups.
    [[nodiscard]] inline expected<std::vector<InferenceRecord>> read_all() const {
        size_t n = col_timestamp_ns_.size();
        std::vector<InferenceRecord> records;
        records.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            InferenceRecord rec;
            rec.timestamp_ns    = col_timestamp_ns_[i];
            rec.model_id        = col_model_id_[i];
            rec.model_version   = col_model_version_[i];
            rec.inference_type  = static_cast<InferenceType>(col_inference_type_[i]);
            if (col_inference_type_[i] < 0 || (col_inference_type_[i] > 6 && col_inference_type_[i] != 255))
                rec.inference_type = InferenceType::CLASSIFICATION;
            rec.input_embedding = detail::json_to_embedding(col_input_embedding_[i]);
            rec.input_hash      = col_input_hash_[i];
            rec.output_hash     = col_output_hash_[i];
            rec.output_score    = static_cast<float>(col_output_score_[i]);
            rec.latency_ns      = col_latency_ns_[i];
            rec.batch_size      = col_batch_size_[i];
            rec.input_tokens    = col_input_tokens_[i];
            rec.output_tokens   = col_output_tokens_[i];
            rec.user_id_hash    = col_user_id_hash_[i];
            rec.session_id      = col_session_id_[i];
            rec.metadata_json   = col_metadata_json_[i];
            records.push_back(std::move(rec));
        }

        return records;
    }

    /// Get the audit chain metadata from the Parquet file's key-value metadata.
    ///
    /// @return The AuditMetadata extracted from `signetstack.audit.*` keys.
    [[nodiscard]] inline expected<AuditMetadata> audit_metadata() const {
        const auto& kvs = reader_->key_value_metadata();
        AuditMetadata meta;

        for (const auto& kv : kvs) {
            if (!kv.value.has_value()) continue;
            const auto& val = *kv.value;

            if (kv.key == "signetstack.audit.chain_id")       meta.chain_id       = val;
            else if (kv.key == "signetstack.audit.first_seq")  { try { meta.start_sequence = std::stoll(val); } catch (...) {} }
            else if (kv.key == "signetstack.audit.last_seq")   { try { meta.end_sequence  = std::stoll(val); } catch (...) {} }
            else if (kv.key == "signetstack.audit.first_hash") meta.first_hash     = val;
            else if (kv.key == "signetstack.audit.last_hash")  meta.last_hash      = val;
            else if (kv.key == "signetstack.audit.prev_file_hash") meta.prev_file_hash = val;
            else if (kv.key == "signetstack.audit.record_count") { try { meta.record_count = std::stoll(val); } catch (...) {} }
            else if (kv.key == "signetstack.audit.record_type")  meta.record_type  = val;
        }

        return meta;
    }

    /// Verify the hash chain integrity by re-hashing each record and checking chain continuity.
    ///
    /// Reconstructs hash chain entries from stored columns, recomputes data_hash
    /// from the record fields, and runs the full AuditChainVerifier pipeline.
    ///
    /// @return A VerificationResult indicating whether the chain is valid
    ///         and, if not, which entry failed verification.
    /// @see AuditChainVerifier::verify
    [[nodiscard]] inline AuditChainVerifier::VerificationResult verify_chain() const {
        // Reconstruct chain entries from stored columns
        std::vector<HashChainEntry> entries;
        size_t n = col_chain_seq_.size();
        entries.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            HashChainEntry entry;
            entry.sequence_number = col_chain_seq_[i];
            entry.timestamp_ns    = col_timestamp_ns_[i];
            auto eh = hex_to_hash(col_chain_hash_[i]);
            auto ph = hex_to_hash(col_prev_hash_[i]);
            if (!eh || !ph) {
                // Hash deserialization failure — chain is invalid
                AuditChainVerifier::VerificationResult bad;
                bad.valid = false;
                bad.entries_checked = static_cast<int64_t>(i);
                bad.first_bad_index = static_cast<int64_t>(i);
                bad.error_message = !eh ? "entry_hash deserialization failed at record "
                                          + std::to_string(i)
                                        : "prev_hash deserialization failed at record "
                                          + std::to_string(i);
                return bad;
            }
            entry.entry_hash = *eh;
            entry.prev_hash  = *ph;

            // Re-compute data_hash from the record
            InferenceRecord rec;
            rec.timestamp_ns    = col_timestamp_ns_[i];
            rec.model_id        = col_model_id_[i];
            rec.model_version   = col_model_version_[i];
            rec.inference_type  = static_cast<InferenceType>(col_inference_type_[i]);
            if (col_inference_type_[i] < 0 || (col_inference_type_[i] > 6 && col_inference_type_[i] != 255))
                rec.inference_type = InferenceType::CLASSIFICATION;
            rec.input_embedding = detail::json_to_embedding(col_input_embedding_[i]);
            rec.input_hash      = col_input_hash_[i];
            rec.output_hash     = col_output_hash_[i];
            rec.output_score    = static_cast<float>(col_output_score_[i]);
            rec.latency_ns      = col_latency_ns_[i];
            rec.batch_size      = col_batch_size_[i];
            rec.input_tokens    = col_input_tokens_[i];
            rec.output_tokens   = col_output_tokens_[i];
            rec.user_id_hash    = col_user_id_hash_[i];
            rec.session_id      = col_session_id_[i];
            rec.metadata_json   = col_metadata_json_[i];

            auto data = rec.serialize();
            entry.data_hash = crypto::detail::sha256::sha256(data.data(), data.size());

            entries.push_back(std::move(entry));
        }

        // Verify using AuditChainVerifier
        AuditChainVerifier verifier;
        return verifier.verify(entries);
    }

    /// Get the schema of the inference log file.
    [[nodiscard]] inline const Schema& schema() const {
        return reader_->schema();
    }

    /// Number of records in the file.
    [[nodiscard]] inline int64_t num_records() const {
        return reader_->num_rows();
    }

private:
    std::unique_ptr<ParquetReader>  reader_;
    std::string                     path_;

    // Column data (loaded once on open)
    std::vector<int64_t>     col_timestamp_ns_;
    std::vector<std::string> col_model_id_;
    std::vector<std::string> col_model_version_;
    std::vector<int32_t>     col_inference_type_;
    std::vector<std::string> col_input_embedding_;
    std::vector<std::string> col_input_hash_;
    std::vector<std::string> col_output_hash_;
    std::vector<double>      col_output_score_;
    std::vector<int64_t>     col_latency_ns_;
    std::vector<int32_t>     col_batch_size_;
    std::vector<int32_t>     col_input_tokens_;
    std::vector<int32_t>     col_output_tokens_;
    std::vector<std::string> col_user_id_hash_;
    std::vector<std::string> col_session_id_;
    std::vector<std::string> col_metadata_json_;
    std::vector<int64_t>     col_chain_seq_;
    std::vector<std::string> col_chain_hash_;
    std::vector<std::string> col_prev_hash_;

    /// Load all columns from all row groups into the internal vectors.
    [[nodiscard]] inline expected<void> load_columns() {
        int64_t num_rgs = reader_->num_row_groups();

        for (int64_t rg = 0; rg < num_rgs; ++rg) {
            size_t rg_idx = static_cast<size_t>(rg);

            // Column 0: timestamp_ns (INT64)
            auto r0 = reader_->read_column<int64_t>(rg_idx, 0);
            if (!r0) return r0.error();
            col_timestamp_ns_.insert(col_timestamp_ns_.end(), r0->begin(), r0->end());

            // Column 1: model_id (STRING)
            auto r1 = reader_->read_column<std::string>(rg_idx, 1);
            if (!r1) return r1.error();
            col_model_id_.insert(col_model_id_.end(),
                std::make_move_iterator(r1->begin()), std::make_move_iterator(r1->end()));

            // Column 2: model_version (STRING)
            auto r2 = reader_->read_column<std::string>(rg_idx, 2);
            if (!r2) return r2.error();
            col_model_version_.insert(col_model_version_.end(),
                std::make_move_iterator(r2->begin()), std::make_move_iterator(r2->end()));

            // Column 3: inference_type (INT32)
            auto r3 = reader_->read_column<int32_t>(rg_idx, 3);
            if (!r3) return r3.error();
            col_inference_type_.insert(col_inference_type_.end(), r3->begin(), r3->end());

            // Column 4: input_embedding (STRING — JSON array)
            auto r4 = reader_->read_column<std::string>(rg_idx, 4);
            if (!r4) return r4.error();
            col_input_embedding_.insert(col_input_embedding_.end(),
                std::make_move_iterator(r4->begin()), std::make_move_iterator(r4->end()));

            // Column 5: input_hash (STRING)
            auto r5 = reader_->read_column<std::string>(rg_idx, 5);
            if (!r5) return r5.error();
            col_input_hash_.insert(col_input_hash_.end(),
                std::make_move_iterator(r5->begin()), std::make_move_iterator(r5->end()));

            // Column 6: output_hash (STRING)
            auto r6 = reader_->read_column<std::string>(rg_idx, 6);
            if (!r6) return r6.error();
            col_output_hash_.insert(col_output_hash_.end(),
                std::make_move_iterator(r6->begin()), std::make_move_iterator(r6->end()));

            // Column 7: output_score (DOUBLE)
            auto r7 = reader_->read_column<double>(rg_idx, 7);
            if (!r7) return r7.error();
            col_output_score_.insert(col_output_score_.end(), r7->begin(), r7->end());

            // Column 8: latency_ns (INT64)
            auto r8 = reader_->read_column<int64_t>(rg_idx, 8);
            if (!r8) return r8.error();
            col_latency_ns_.insert(col_latency_ns_.end(), r8->begin(), r8->end());

            // Column 9: batch_size (INT32)
            auto r9 = reader_->read_column<int32_t>(rg_idx, 9);
            if (!r9) return r9.error();
            col_batch_size_.insert(col_batch_size_.end(), r9->begin(), r9->end());

            // Column 10: input_tokens (INT32)
            auto r10 = reader_->read_column<int32_t>(rg_idx, 10);
            if (!r10) return r10.error();
            col_input_tokens_.insert(col_input_tokens_.end(), r10->begin(), r10->end());

            // Column 11: output_tokens (INT32)
            auto r11 = reader_->read_column<int32_t>(rg_idx, 11);
            if (!r11) return r11.error();
            col_output_tokens_.insert(col_output_tokens_.end(), r11->begin(), r11->end());

            // Column 12: user_id_hash (STRING)
            auto r12 = reader_->read_column<std::string>(rg_idx, 12);
            if (!r12) return r12.error();
            col_user_id_hash_.insert(col_user_id_hash_.end(),
                std::make_move_iterator(r12->begin()), std::make_move_iterator(r12->end()));

            // Column 13: session_id (STRING)
            auto r13 = reader_->read_column<std::string>(rg_idx, 13);
            if (!r13) return r13.error();
            col_session_id_.insert(col_session_id_.end(),
                std::make_move_iterator(r13->begin()), std::make_move_iterator(r13->end()));

            // Column 14: metadata_json (STRING)
            auto r14 = reader_->read_column<std::string>(rg_idx, 14);
            if (!r14) return r14.error();
            col_metadata_json_.insert(col_metadata_json_.end(),
                std::make_move_iterator(r14->begin()), std::make_move_iterator(r14->end()));

            // Column 15: chain_seq (INT64)
            auto r15 = reader_->read_column<int64_t>(rg_idx, 15);
            if (!r15) return r15.error();
            col_chain_seq_.insert(col_chain_seq_.end(), r15->begin(), r15->end());

            // Column 16: chain_hash (STRING)
            auto r16 = reader_->read_column<std::string>(rg_idx, 16);
            if (!r16) return r16.error();
            col_chain_hash_.insert(col_chain_hash_.end(),
                std::make_move_iterator(r16->begin()), std::make_move_iterator(r16->end()));

            // Column 17: prev_hash (STRING)
            auto r17 = reader_->read_column<std::string>(rg_idx, 17);
            if (!r17) return r17.error();
            col_prev_hash_.insert(col_prev_hash_.end(),
                std::make_move_iterator(r17->begin()), std::make_move_iterator(r17->end()));
        }

        return expected<void>{};
    }
};

} // namespace signet::forge
