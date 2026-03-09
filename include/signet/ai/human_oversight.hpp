// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/human_oversight.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

// ---------------------------------------------------------------------------
// human_oversight.hpp -- EU AI Act Article 14 Human Oversight Implementation
//
// Provides a complete human oversight layer for high-risk AI systems:
//
//   Art.14(1): Systems designed to allow effective human oversight
//   Art.14(2): Ability to fully understand capabilities and limitations
//   Art.14(3): Ability to correctly interpret outputs
//   Art.14(4): Ability to override, interrupt, or halt the system ("stop button")
//   Art.14(5): All override events logged with full provenance
//
// Components:
//   - OverrideSource / OverrideAction / HaltReason enums
//   - HumanOverrideRecord: captures each human intervention event
//   - SystemHaltRecord: captures system halt ("stop button") events
//   - HumanOverrideLogWriter: Parquet writer with hash chaining
//   - HumanOverrideLogReader: reader with chain verification
//   - OverrideRateMonitor: sliding-window override frequency tracker
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
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

/// Source of a decision or override — EU AI Act Art.14(4).
/// Tracks whether an output was produced algorithmically or overridden by a human.
enum class OverrideSource : int32_t {
    ALGORITHMIC = 0,   ///< Original AI system output (no human intervention)
    HUMAN       = 1,   ///< Human operator override
    AUTOMATED   = 2,   ///< Automated safety system override (e.g. risk gate)
};

/// What action the human override took — EU AI Act Art.14(4).
enum class OverrideAction : int32_t {
    APPROVE     = 0,   ///< Human approved the AI system's output as-is
    MODIFY      = 1,   ///< Human modified the AI system's output
    REJECT      = 2,   ///< Human rejected the AI system's output entirely
    ESCALATE    = 3,   ///< Human escalated to a higher authority
    HALT        = 4,   ///< Human triggered system halt ("stop button")
};

/// Reason for system halt — EU AI Act Art.14(4) "stop button".
enum class HaltReason : int32_t {
    MANUAL           = 0,   ///< Operator manually halted the system
    SAFETY_THRESHOLD = 1,   ///< Override rate exceeded safety threshold
    ANOMALY_DETECTED = 2,   ///< Anomalous behavior detected
    REGULATORY       = 3,   ///< Regulatory or compliance-driven halt
    MAINTENANCE      = 4,   ///< Scheduled maintenance halt
    EXTERNAL         = 5,   ///< External event (market halt, circuit breaker)
};

// ---------------------------------------------------------------------------
// HumanOverrideRecord
// ---------------------------------------------------------------------------

/// A single human oversight event with full provenance.
///
/// EU AI Act Art.14(4)-(5): Every human intervention in the AI system's
/// operation must be logged with the original output, the override action,
/// the operator identity, and the rationale.
struct HumanOverrideRecord {
    int64_t         timestamp_ns{0};       ///< When the override occurred (ns since epoch)
    std::string     operator_id;           ///< Human operator identifier (pseudonymised per GDPR Art.25)
    std::string     operator_role;         ///< Operator role (e.g. "trader", "risk_officer", "supervisor")
    OverrideSource  source{OverrideSource::HUMAN}; ///< Who initiated this action
    OverrideAction  action{OverrideAction::APPROVE}; ///< What action was taken
    std::string     system_id;             ///< AI system identifier (matches ReportOptions::system_id)

    // --- Original AI output context ---
    std::string     original_decision_id;  ///< Reference to the DecisionRecord/order_id being overridden
    std::string     original_output;       ///< String representation of the AI system's original output
    float           original_confidence{0.0f}; ///< AI system's confidence in the original output

    // --- Override details ---
    std::string     override_output;       ///< The human's replacement output (if action == MODIFY)
    std::string     rationale;             ///< Human-provided reason for the override (Art.14(5))
    int32_t         urgency{0};            ///< Override urgency level (0=routine, 1=elevated, 2=critical)

    // --- Serialization ---

    [[nodiscard]] inline std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf;
        buf.reserve(256);

        append_le64(buf, static_cast<uint64_t>(timestamp_ns));
        append_string(buf, operator_id);
        append_string(buf, operator_role);
        append_le32(buf, static_cast<uint32_t>(source));
        append_le32(buf, static_cast<uint32_t>(action));
        append_string(buf, system_id);
        append_string(buf, original_decision_id);
        append_string(buf, original_output);
        append_float(buf, original_confidence);
        append_string(buf, override_output);
        append_string(buf, rationale);
        append_le32(buf, static_cast<uint32_t>(urgency));

        return buf;
    }

    [[nodiscard]] static inline expected<HumanOverrideRecord> deserialize(
            const uint8_t* data, size_t size) {
        size_t offset = 0;
        HumanOverrideRecord rec;

        if (!read_le64(data, size, offset, rec.timestamp_ns))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated timestamp_ns"};
        if (!read_string(data, size, offset, rec.operator_id))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated operator_id"};
        if (!read_string(data, size, offset, rec.operator_role))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated operator_role"};

        int32_t src_val = 0;
        if (!read_le32(data, size, offset, src_val))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated source"};
        rec.source = static_cast<OverrideSource>(src_val);

        int32_t act_val = 0;
        if (!read_le32(data, size, offset, act_val))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated action"};
        rec.action = static_cast<OverrideAction>(act_val);

        if (!read_string(data, size, offset, rec.system_id))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated system_id"};
        if (!read_string(data, size, offset, rec.original_decision_id))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated original_decision_id"};
        if (!read_string(data, size, offset, rec.original_output))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated original_output"};
        if (!read_float(data, size, offset, rec.original_confidence))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated original_confidence"};
        if (!read_string(data, size, offset, rec.override_output))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated override_output"};
        if (!read_string(data, size, offset, rec.rationale))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated rationale"};

        int32_t urg_val = 0;
        if (!read_le32(data, size, offset, urg_val))
            return Error{ErrorCode::CORRUPT_PAGE, "HumanOverrideRecord: truncated urgency"};
        rec.urgency = urg_val;

        return rec;
    }

private:
    // -- Serialization helpers (same as DecisionRecord) -----------------------

    static inline void append_le32(std::vector<uint8_t>& buf, uint32_t v) {
        buf.push_back(static_cast<uint8_t>(v));
        buf.push_back(static_cast<uint8_t>(v >> 8));
        buf.push_back(static_cast<uint8_t>(v >> 16));
        buf.push_back(static_cast<uint8_t>(v >> 24));
    }

    static inline void append_le64(std::vector<uint8_t>& buf, uint64_t v) {
        for (int i = 0; i < 8; ++i)
            buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }

    static inline void append_float(std::vector<uint8_t>& buf, float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        append_le32(buf, bits);
    }

    static inline void append_string(std::vector<uint8_t>& buf, const std::string& s) {
        const size_t clamped = std::min(s.size(), static_cast<size_t>(UINT32_MAX));
        append_le32(buf, static_cast<uint32_t>(clamped));
        buf.insert(buf.end(), s.begin(), s.begin() + static_cast<ptrdiff_t>(clamped));
    }

    // -- Deserialization helpers -----------------------------------------------

    static inline bool read_le64(const uint8_t* data, size_t size, size_t& offset, int64_t& out) {
        if (offset + 8 > size) return false;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        out = static_cast<int64_t>(v);
        offset += 8;
        return true;
    }

    static inline bool read_le32(const uint8_t* data, size_t size, size_t& offset, int32_t& out) {
        if (offset + 4 > size) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        out = static_cast<int32_t>(v);
        offset += 4;
        return true;
    }

    static inline bool read_float(const uint8_t* data, size_t size, size_t& offset, float& out) {
        if (offset + 4 > size) return false;
        uint32_t bits = 0;
        for (int i = 0; i < 4; ++i)
            bits |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
        std::memcpy(&out, &bits, 4);
        offset += 4;
        return true;
    }

    static inline bool read_string(const uint8_t* data, size_t size, size_t& offset, std::string& out) {
        int32_t len = 0;
        if (!read_le32(data, size, offset, len)) return false;
        if (len < 0) return false;
        auto ulen = static_cast<size_t>(len);
        if (offset + ulen > size) return false;
        out.assign(reinterpret_cast<const char*>(data + offset), ulen);
        offset += ulen;
        return true;
    }
};

// ---------------------------------------------------------------------------
// Parquet schema for human override logs
// ---------------------------------------------------------------------------

/// Build the Parquet schema for human override log files.
///
/// Columns mirror HumanOverrideRecord fields plus hash chain + row lineage.
[[nodiscard]] inline Schema human_override_log_schema() {
    return Schema::builder("human_override_log")
        .column<int64_t>("timestamp_ns", LogicalType::TIMESTAMP_NS)
        .column<std::string>("operator_id")
        .column<std::string>("operator_role")
        .column<int32_t>("source")           // OverrideSource enum
        .column<int32_t>("action")           // OverrideAction enum
        .column<std::string>("system_id")
        .column<std::string>("original_decision_id")
        .column<std::string>("original_output")
        .column<double>("original_confidence")
        .column<std::string>("override_output")
        .column<std::string>("rationale")
        .column<int32_t>("urgency")
        .column<int64_t>("chain_seq")
        .column<std::string>("chain_hash")
        .column<std::string>("prev_hash")
        .column<int64_t>("row_id")
        .column<int32_t>("row_version")
        .column<std::string>("row_origin_file")
        .column<std::string>("row_prev_hash")
        .build();
}

// ---------------------------------------------------------------------------
// OverrideRateMonitor
// ---------------------------------------------------------------------------

/// Options for the override rate monitor.
/// Defined at namespace scope for Apple Clang compatibility.
struct OverrideRateMonitorOptions {
    /// Sliding window duration for rate calculation (nanoseconds).
    /// Default: 1 hour (3.6 × 10^12 ns).
    int64_t window_ns = INT64_C(3600000000000);

    /// Override rate threshold (overrides per window) that triggers an alert.
    /// Default: 10 overrides per window.
    int64_t alert_threshold = 10;

    /// If true, automatically fire the halt callback when threshold is exceeded.
    bool auto_halt_on_threshold = false;
};

/// Sliding-window override rate monitor — EU AI Act Art.14(5).
///
/// Tracks the frequency of human override events within a configurable time
/// window. When the override rate exceeds a threshold, fires an alert callback
/// (and optionally a halt callback for Art.14(4) "stop button" automation).
///
/// Thread-safe: all methods are guarded by a mutex.
class OverrideRateMonitor {
public:
    using AlertCallback = std::function<void(int64_t override_count, int64_t window_ns)>;
    using HaltCallback  = std::function<void(HaltReason reason, const std::string& detail)>;

    explicit OverrideRateMonitor(OverrideRateMonitorOptions opts = {})
        : opts_(std::move(opts)) {}

    /// Register a callback for when override rate exceeds threshold.
    void set_alert_callback(AlertCallback cb) {
        std::lock_guard<std::mutex> lock(mu_);
        alert_cb_ = std::move(cb);
    }

    /// Register a callback for system halt requests.
    void set_halt_callback(HaltCallback cb) {
        std::lock_guard<std::mutex> lock(mu_);
        halt_cb_ = std::move(cb);
    }

    /// Record an override event at the given timestamp.
    /// Returns the current override count within the window.
    int64_t record_override(int64_t timestamp_ns) {
        std::lock_guard<std::mutex> lock(mu_);

        timestamps_.push_back(timestamp_ns);
        evict_old(timestamp_ns);

        auto count = static_cast<int64_t>(timestamps_.size());

        if (count >= opts_.alert_threshold) {
            if (alert_cb_) {
                alert_cb_(count, opts_.window_ns);
            }
            if (opts_.auto_halt_on_threshold && halt_cb_) {
                halt_cb_(HaltReason::SAFETY_THRESHOLD,
                         "Override rate " + std::to_string(count) +
                         " exceeded threshold " + std::to_string(opts_.alert_threshold));
            }
        }

        return count;
    }

    /// Get the current override count within the window.
    [[nodiscard]] int64_t current_count(int64_t now_ns) {
        std::lock_guard<std::mutex> lock(mu_);
        evict_old(now_ns);
        return static_cast<int64_t>(timestamps_.size());
    }

    /// Manually trigger a system halt (Art.14(4) "stop button").
    void trigger_halt(HaltReason reason, const std::string& detail = "") {
        std::lock_guard<std::mutex> lock(mu_);
        if (halt_cb_) {
            halt_cb_(reason, detail);
        }
    }

    /// Get the configured options.
    [[nodiscard]] const OverrideRateMonitorOptions& options() const noexcept { return opts_; }

private:
    void evict_old(int64_t now_ns) {
        int64_t cutoff = now_ns - opts_.window_ns;
        while (!timestamps_.empty() && timestamps_.front() < cutoff) {
            timestamps_.pop_front();
        }
    }

    OverrideRateMonitorOptions opts_;
    std::deque<int64_t>        timestamps_;
    AlertCallback              alert_cb_;
    HaltCallback               halt_cb_;
    std::mutex                 mu_;
};

// ---------------------------------------------------------------------------
// HumanOverrideLogWriter
// ---------------------------------------------------------------------------

/// Writes human override events to Parquet files with cryptographic hash
/// chaining for tamper-evident audit trails.
///
/// EU AI Act Art.14(4)-(5): All human intervention events are logged with
/// full provenance, including operator identity, rationale, and the original
/// AI system output that was overridden.
///
/// Usage:
///   HumanOverrideLogWriter writer("/audit/overrides");
///   HumanOverrideRecord rec;
///   rec.timestamp_ns = now_ns();
///   rec.operator_id  = "op-001";
///   rec.action = OverrideAction::MODIFY;
///   rec.rationale = "Model output inconsistent with market conditions";
///   auto entry = writer.log(rec);
///   writer.close();
class HumanOverrideLogWriter {
public:
    /// Create a human override log writer.
    /// @param output_dir   Directory to write Parquet files to
    /// @param chain_id     Unique chain identifier (auto-generated if empty)
    /// @param max_records  Max records per file before rotating (default 100,000)
    inline HumanOverrideLogWriter(const std::string& output_dir,
                                   const std::string& chain_id = "",
                                   size_t max_records = 100000)
        : output_dir_(output_dir)
        , chain_id_(chain_id.empty() ? generate_chain_id() : chain_id)
        , max_records_(max_records)
        , schema_(human_override_log_schema())
        , lineage_tracker_(chain_id.empty() ? chain_id_ : chain_id, 1)
    {
        auto license = commercial::require_feature("HumanOverrideLogWriter");
        if (!license) {
            throw std::runtime_error(license.error().message);
        }

        // Validate output_dir (CWE-22: Path Traversal)
        if (output_dir_.empty())
            throw std::invalid_argument("HumanOverrideLogWriter: output_dir must not be empty");
        for (size_t s = 0, e; s <= output_dir_.size(); s = e + 1) {
            e = output_dir_.find_first_of("/\\", s);
            if (e == std::string::npos) e = output_dir_.size();
            if (output_dir_.substr(s, e - s) == "..")
                throw std::invalid_argument(
                    "HumanOverrideLogWriter: output_dir must not contain '..' path traversal");
        }
        // Validate chain_id: [a-zA-Z0-9_-]+
        for (char c : chain_id_) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                throw std::invalid_argument(
                    "HumanOverrideLogWriter: chain_id must only contain [a-zA-Z0-9_-]");
        }
    }

    /// Log a human override event. Returns the hash chain entry.
    [[nodiscard]] inline expected<HashChainEntry> log(const HumanOverrideRecord& record) {
        auto usage = commercial::record_usage_rows("HumanOverrideLogWriter::log", 1);
        if (!usage) return usage.error();

        auto data = record.serialize();

        int64_t ts = record.timestamp_ns;
        if (ts == 0) ts = now_ns();

        auto entry = chain_.append(data.data(), data.size(), ts);

        pending_records_.push_back(record);
        pending_entries_.push_back(entry);
        pending_data_.push_back(std::move(data));

        if (pending_records_.size() >= max_records_) {
            auto result = flush();
            if (!result) return result.error();
        }

        ++total_records_;
        return entry;
    }

    /// Flush current records to a Parquet file.
    [[nodiscard]] inline expected<void> flush() {
        if (pending_records_.empty()) {
            return expected<void>{};
        }

        int64_t start_seq = pending_entries_.front().sequence_number;
        int64_t end_seq   = pending_entries_.back().sequence_number;

        current_file_path_ = output_dir_ + "/human_override_log_" + chain_id_ + "_"
                           + std::to_string(start_seq) + "_"
                           + std::to_string(end_seq) + ".parquet";

        WriterOptions opts;
        opts.created_by = "SignetStack signet-forge human_override_log v1.0";

        auto meta = current_metadata();
        auto meta_kvs = meta.to_key_values();
        for (auto& [k, v] : meta_kvs) {
            opts.file_metadata.push_back(thrift::KeyValue(std::move(k), std::move(v)));
        }

        auto writer_result = ParquetWriter::open(current_file_path_, schema_, opts);
        if (!writer_result) return writer_result.error();
        auto& writer = *writer_result;

        size_t n = pending_records_.size();
        for (size_t i = 0; i < n; ++i) {
            const auto& rec   = pending_records_[i];
            const auto& entry = pending_entries_[i];
            const auto& row_data = pending_data_[i];
            auto lineage = lineage_tracker_.next(row_data.data(), row_data.size());

            std::vector<std::string> row;
            row.reserve(19);

            row.push_back(std::to_string(rec.timestamp_ns));
            row.push_back(rec.operator_id);
            row.push_back(rec.operator_role);
            row.push_back(std::to_string(static_cast<int32_t>(rec.source)));
            row.push_back(std::to_string(static_cast<int32_t>(rec.action)));
            row.push_back(rec.system_id);
            row.push_back(rec.original_decision_id);
            row.push_back(rec.original_output);
            row.push_back(double_to_string(static_cast<double>(rec.original_confidence)));
            row.push_back(rec.override_output);
            row.push_back(rec.rationale);
            row.push_back(std::to_string(rec.urgency));
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

        auto close_result = writer.close();
        if (!close_result) return close_result.error();

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
        meta.record_type    = "human_override";
        return meta;
    }

    [[nodiscard]] inline size_t pending_records() const { return pending_records_.size(); }
    [[nodiscard]] inline int64_t total_records() const { return total_records_; }
    [[nodiscard]] inline std::string current_file_path() const { return current_file_path_; }

private:
    std::string                         output_dir_;
    std::string                         chain_id_;
    size_t                              max_records_;
    Schema                              schema_;
    AuditChainWriter                    chain_;
    std::vector<HumanOverrideRecord>    pending_records_;
    std::vector<HashChainEntry>         pending_entries_;
    std::vector<std::vector<uint8_t>>   pending_data_;
    RowLineageTracker                   lineage_tracker_;
    std::string                         current_file_path_;
    int64_t                             total_records_{0};
    int64_t                             file_count_{0};

    static inline std::string double_to_string(double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        return buf;
    }
};

// ---------------------------------------------------------------------------
// HumanOverrideLogReader
// ---------------------------------------------------------------------------

/// Reads human override log Parquet files and verifies hash chain integrity.
///
/// Usage:
///   auto reader = HumanOverrideLogReader::open("human_override_log_abc_0_9.parquet");
///   auto records = reader->read_all();
///   auto verify  = reader->verify_chain();
class HumanOverrideLogReader {
public:
    [[nodiscard]] static inline expected<HumanOverrideLogReader> open(const std::string& path) {
        auto license = commercial::require_feature("HumanOverrideLogReader");
        if (!license) return license.error();

        auto reader_result = ParquetReader::open(path);
        if (!reader_result) return reader_result.error();

        HumanOverrideLogReader hlr;
        hlr.reader_ = std::make_unique<ParquetReader>(std::move(*reader_result));
        hlr.path_   = path;

        auto load_result = hlr.load_columns();
        if (!load_result) return load_result.error();

        return hlr;
    }

    /// @name Move-only semantics
    /// @{
    HumanOverrideLogReader() = default;
    HumanOverrideLogReader(const HumanOverrideLogReader&) = delete;
    HumanOverrideLogReader& operator=(const HumanOverrideLogReader&) = delete;
    HumanOverrideLogReader(HumanOverrideLogReader&&) = default;
    HumanOverrideLogReader& operator=(HumanOverrideLogReader&&) = default;
    /// @}

    /// Get all override records from the file.
    [[nodiscard]] inline expected<std::vector<HumanOverrideRecord>> read_all() const {
        size_t n = col_timestamp_ns_.size();
        std::vector<HumanOverrideRecord> records;
        records.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            HumanOverrideRecord rec;
            rec.timestamp_ns          = col_timestamp_ns_[i];
            rec.operator_id           = col_operator_id_[i];
            rec.operator_role         = col_operator_role_[i];
            rec.source                = static_cast<OverrideSource>(col_source_[i]);
            rec.action                = static_cast<OverrideAction>(col_action_[i]);
            rec.system_id             = col_system_id_[i];
            rec.original_decision_id  = col_original_decision_id_[i];
            rec.original_output       = col_original_output_[i];
            rec.original_confidence   = static_cast<float>(col_original_confidence_[i]);
            rec.override_output       = col_override_output_[i];
            rec.rationale             = col_rationale_[i];
            rec.urgency               = col_urgency_[i];
            records.push_back(std::move(rec));
        }

        return records;
    }

    /// Get the audit chain metadata from the Parquet file's key-value metadata.
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

    /// Verify the hash chain integrity.
    [[nodiscard]] inline AuditChainVerifier::VerificationResult verify_chain() const {
        size_t n = col_timestamp_ns_.size();
        if (n == 0) {
            AuditChainVerifier::VerificationResult empty_ok;
            empty_ok.valid = true;
            empty_ok.entries_checked = 0;
            empty_ok.error_message = "Empty file — no entries to verify";
            return empty_ok;
        }

        // Reconstruct chain entries from stored columns
        std::vector<HashChainEntry> entries;
        entries.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            HashChainEntry entry;
            entry.sequence_number = col_chain_seq_[i];
            entry.timestamp_ns    = col_timestamp_ns_[i];

            auto eh = hex_to_hash(col_chain_hash_[i]);
            auto ph = hex_to_hash(col_prev_hash_[i]);
            if (!eh || !ph) {
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

            // Recompute data_hash from the record
            HumanOverrideRecord rec;
            rec.timestamp_ns          = col_timestamp_ns_[i];
            rec.operator_id           = col_operator_id_[i];
            rec.operator_role         = col_operator_role_[i];
            rec.source                = static_cast<OverrideSource>(col_source_[i]);
            rec.action                = static_cast<OverrideAction>(col_action_[i]);
            rec.system_id             = col_system_id_[i];
            rec.original_decision_id  = col_original_decision_id_[i];
            rec.original_output       = col_original_output_[i];
            rec.original_confidence   = static_cast<float>(col_original_confidence_[i]);
            rec.override_output       = col_override_output_[i];
            rec.rationale             = col_rationale_[i];
            rec.urgency               = col_urgency_[i];

            auto serialized = rec.serialize();
            entry.data_hash = crypto::detail::sha256::sha256(serialized.data(), serialized.size());

            entries.push_back(std::move(entry));
        }

        AuditChainVerifier verifier;
        return verifier.verify(entries);
    }

    /// Get number of records in the file.
    [[nodiscard]] inline size_t record_count() const { return col_timestamp_ns_.size(); }

    /// Get the file path.
    [[nodiscard]] inline const std::string& path() const { return path_; }

private:
    std::unique_ptr<ParquetReader> reader_;
    std::string                    path_;

    // Column data
    std::vector<int64_t>      col_timestamp_ns_;
    std::vector<std::string>  col_operator_id_;
    std::vector<std::string>  col_operator_role_;
    std::vector<int32_t>      col_source_;
    std::vector<int32_t>      col_action_;
    std::vector<std::string>  col_system_id_;
    std::vector<std::string>  col_original_decision_id_;
    std::vector<std::string>  col_original_output_;
    std::vector<double>       col_original_confidence_;
    std::vector<std::string>  col_override_output_;
    std::vector<std::string>  col_rationale_;
    std::vector<int32_t>      col_urgency_;
    std::vector<int64_t>      col_chain_seq_;
    std::vector<std::string>  col_chain_hash_;
    std::vector<std::string>  col_prev_hash_;

    [[nodiscard]] inline expected<void> load_columns() {
        int64_t num_rgs = reader_->num_row_groups();

        for (int64_t rg = 0; rg < num_rgs; ++rg) {
            size_t rg_idx = static_cast<size_t>(rg);

            // Col 0: timestamp_ns (INT64)
            auto r0 = reader_->read_column<int64_t>(rg_idx, 0);
            if (!r0) return r0.error();
            col_timestamp_ns_.insert(col_timestamp_ns_.end(), r0->begin(), r0->end());

            // Col 1: operator_id (STRING)
            auto r1 = reader_->read_column<std::string>(rg_idx, 1);
            if (!r1) return r1.error();
            col_operator_id_.insert(col_operator_id_.end(),
                std::make_move_iterator(r1->begin()), std::make_move_iterator(r1->end()));

            // Col 2: operator_role (STRING)
            auto r2 = reader_->read_column<std::string>(rg_idx, 2);
            if (!r2) return r2.error();
            col_operator_role_.insert(col_operator_role_.end(),
                std::make_move_iterator(r2->begin()), std::make_move_iterator(r2->end()));

            // Col 3: source (INT32)
            auto r3 = reader_->read_column<int32_t>(rg_idx, 3);
            if (!r3) return r3.error();
            col_source_.insert(col_source_.end(), r3->begin(), r3->end());

            // Col 4: action (INT32)
            auto r4 = reader_->read_column<int32_t>(rg_idx, 4);
            if (!r4) return r4.error();
            col_action_.insert(col_action_.end(), r4->begin(), r4->end());

            // Col 5: system_id (STRING)
            auto r5 = reader_->read_column<std::string>(rg_idx, 5);
            if (!r5) return r5.error();
            col_system_id_.insert(col_system_id_.end(),
                std::make_move_iterator(r5->begin()), std::make_move_iterator(r5->end()));

            // Col 6: original_decision_id (STRING)
            auto r6 = reader_->read_column<std::string>(rg_idx, 6);
            if (!r6) return r6.error();
            col_original_decision_id_.insert(col_original_decision_id_.end(),
                std::make_move_iterator(r6->begin()), std::make_move_iterator(r6->end()));

            // Col 7: original_output (STRING)
            auto r7 = reader_->read_column<std::string>(rg_idx, 7);
            if (!r7) return r7.error();
            col_original_output_.insert(col_original_output_.end(),
                std::make_move_iterator(r7->begin()), std::make_move_iterator(r7->end()));

            // Col 8: original_confidence (DOUBLE)
            auto r8 = reader_->read_column<double>(rg_idx, 8);
            if (!r8) return r8.error();
            col_original_confidence_.insert(col_original_confidence_.end(), r8->begin(), r8->end());

            // Col 9: override_output (STRING)
            auto r9 = reader_->read_column<std::string>(rg_idx, 9);
            if (!r9) return r9.error();
            col_override_output_.insert(col_override_output_.end(),
                std::make_move_iterator(r9->begin()), std::make_move_iterator(r9->end()));

            // Col 10: rationale (STRING)
            auto r10 = reader_->read_column<std::string>(rg_idx, 10);
            if (!r10) return r10.error();
            col_rationale_.insert(col_rationale_.end(),
                std::make_move_iterator(r10->begin()), std::make_move_iterator(r10->end()));

            // Col 11: urgency (INT32)
            auto r11 = reader_->read_column<int32_t>(rg_idx, 11);
            if (!r11) return r11.error();
            col_urgency_.insert(col_urgency_.end(), r11->begin(), r11->end());

            // Col 12: chain_seq (INT64)
            auto r12 = reader_->read_column<int64_t>(rg_idx, 12);
            if (!r12) return r12.error();
            col_chain_seq_.insert(col_chain_seq_.end(), r12->begin(), r12->end());

            // Col 13: chain_hash (STRING)
            auto r13 = reader_->read_column<std::string>(rg_idx, 13);
            if (!r13) return r13.error();
            col_chain_hash_.insert(col_chain_hash_.end(),
                std::make_move_iterator(r13->begin()), std::make_move_iterator(r13->end()));

            // Col 14: prev_hash (STRING)
            auto r14 = reader_->read_column<std::string>(rg_idx, 14);
            if (!r14) return r14.error();
            col_prev_hash_.insert(col_prev_hash_.end(),
                std::make_move_iterator(r14->begin()), std::make_move_iterator(r14->end()));
        }
        return expected<void>{};
    }
};

} // namespace signet::forge
