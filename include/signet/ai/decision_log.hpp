// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/decision_log.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

// ---------------------------------------------------------------------------
// decision_log.hpp -- AI Trading Decision Audit Trail
//
// Logs every AI-driven trading decision with cryptographic hash chaining
// for regulatory compliance. Each decision record is appended to a tamper-
// evident hash chain and persisted to Parquet files with full provenance.
//
// Regulatory requirements addressed:
//   - MiFID II RTS 24:  Nanosecond-timestamped order decision records
//   - EU AI Act Art 12/19: Automatic logging of AI system operations
//   - SEC 17a-4:        Tamper-evident records retention
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
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace signet::forge {

/// Classification of the AI-driven trading decision.
/// Covers the full lifecycle of order management decisions that must be
/// logged under MiFID II RTS 24 and EU AI Act Article 12.
enum class DecisionType : int32_t {
    SIGNAL         = 0,  ///< Raw model signal/prediction
    ORDER_NEW      = 1,  ///< Decision to submit a new order
    ORDER_CANCEL   = 2,  ///< Decision to cancel an existing order
    ORDER_MODIFY   = 3,  ///< Decision to modify an existing order
    POSITION_OPEN  = 4,  ///< Decision to open a position
    POSITION_CLOSE = 5,  ///< Decision to close a position
    RISK_OVERRIDE  = 6,  ///< Risk gate override/rejection
    NO_ACTION      = 7   ///< Model evaluated but no action taken
};

/// Outcome of the pre-trade risk gate evaluation.
/// Records whether the order passed, was rejected, modified, or throttled
/// by the risk management system.
enum class RiskGateResult : int32_t {
    PASSED     = 0,  ///< All risk checks passed
    REJECTED   = 1,  ///< Order rejected by risk gate
    MODIFIED   = 2,  ///< Order modified by risk gate (e.g., size reduced)
    THROTTLED  = 3   ///< Order delayed by rate limiting
};

/// Order type classification for MiFID II RTS 24 Annex I Table 2 Field 7.
enum class OrderType : int32_t {
    MARKET         = 0,  ///< Market order
    LIMIT          = 1,  ///< Limit order
    STOP           = 2,  ///< Stop order
    STOP_LIMIT     = 3,  ///< Stop-limit order
    PEGGED         = 4,  ///< Pegged order
    OTHER          = 99  ///< Other order type
};

/// Time-in-force classification for MiFID II RTS 24 Annex I Table 2 Field 8.
enum class TimeInForce : int32_t {
    DAY            = 0,  ///< Day order (valid until end of trading day)
    GTC            = 1,  ///< Good-Till-Cancelled
    IOC            = 2,  ///< Immediate-Or-Cancel
    FOK            = 3,  ///< Fill-Or-Kill
    GTD            = 4,  ///< Good-Till-Date
    OTHER          = 99  ///< Other
};

/// Buy/sell direction for MiFID II RTS 24 Annex I Table 2 Field 6.
enum class BuySellIndicator : int32_t {
    BUY            = 0,
    SELL           = 1,
    SHORT_SELL     = 2   ///< Short selling (RTS 24 Annex I Field 16)
};

/// A single AI-driven trading decision with full provenance.
///
/// Each record captures the complete decision context: what the model saw
/// (input features), what it decided (decision type, price, quantity),
/// how confident it was (confidence), and whether risk management approved
/// (risk result). All fields are serialized deterministically for hashing.
///
/// Gap R-4 (MiFID II RTS 24 Annex I): Includes all mandatory fields from
/// Table 2: buy_sell_indicator, order_type, time_in_force, isin, currency,
/// short_selling_flag, aggregated_order, validity_period, parent_order_id.
struct DecisionRecord {
    int64_t              timestamp_ns{0};      ///< Decision timestamp (nanoseconds since epoch)
    int32_t              strategy_id{0};       ///< Which strategy made this decision
    std::string          model_version;        ///< Model version hash or identifier
    DecisionType         decision_type{DecisionType::NO_ACTION}; ///< What type of decision
    std::vector<float>   input_features;       ///< Input feature vector to the model
    float                model_output{0.0f};   ///< Primary model output (e.g., signal strength)
    float                confidence{0.0f};     ///< Model confidence [0.0, 1.0]
    RiskGateResult       risk_result{RiskGateResult::PASSED}; ///< Risk gate outcome
    std::string          order_id;             ///< Associated order ID (empty if none)
    std::string          symbol;               ///< Trading symbol
    double               price{0.0};           ///< Decision price
    double               quantity{0.0};        ///< Decision quantity
    std::string          venue;                ///< Execution venue
    std::string          notes;                ///< Optional free-text notes

    // --- MiFID II RTS 24 Annex I mandatory fields (Gap R-4) ---
    BuySellIndicator     buy_sell{BuySellIndicator::BUY};     ///< Field 6: Buy/sell direction
    OrderType            order_type{OrderType::MARKET};       ///< Field 7: Order type
    TimeInForce          time_in_force{TimeInForce::DAY};     ///< Field 8: Time-in-force
    std::string          isin;                ///< Field 5: ISIN (ISO 6166, 12 chars)
    std::string          currency;            ///< Field 9: Currency (ISO 4217, 3 chars, e.g. "USD")
    bool                 short_selling_flag{false}; ///< Field 16: Short selling indicator
    bool                 aggregated_order{false};   ///< Field 17: Aggregated order flag
    int64_t              validity_period_ns{0};     ///< Field 10: GTD validity timestamp (0=N/A)
    std::string          parent_order_id;     ///< R-17: Parent order for lifecycle linking

    /// Serialize the record to a deterministic byte sequence.
    /// Format: each field written sequentially as little-endian values.
    /// Strings: 4-byte LE length prefix + raw bytes.
    /// Vectors: 4-byte LE count + float data (4 bytes each, LE).
    [[nodiscard]] inline std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> buf;
        buf.reserve(256);  // Pre-allocate reasonable size

        // timestamp_ns: 8 bytes LE
        append_le64(buf, static_cast<uint64_t>(timestamp_ns));

        // strategy_id: 4 bytes LE
        append_le32(buf, static_cast<uint32_t>(strategy_id));

        // model_version: string
        append_string(buf, model_version);

        // decision_type: 4 bytes LE
        append_le32(buf, static_cast<uint32_t>(decision_type));

        // input_features: vector of floats
        append_le32(buf, static_cast<uint32_t>(input_features.size()));
        for (float f : input_features) {
            append_float(buf, f);
        }

        // model_output: float (4 bytes)
        append_float(buf, model_output);

        // confidence: float (4 bytes)
        append_float(buf, confidence);

        // risk_result: 4 bytes LE
        append_le32(buf, static_cast<uint32_t>(risk_result));

        // order_id: string
        append_string(buf, order_id);

        // symbol: string
        append_string(buf, symbol);

        // price: 8 bytes LE (double)
        append_double(buf, price);

        // quantity: 8 bytes LE (double)
        append_double(buf, quantity);

        // venue: string
        append_string(buf, venue);

        // notes: string
        append_string(buf, notes);

        // MiFID II RTS 24 Annex I mandatory fields (Gap R-4)
        append_le32(buf, static_cast<uint32_t>(buy_sell));
        append_le32(buf, static_cast<uint32_t>(order_type));
        append_le32(buf, static_cast<uint32_t>(time_in_force));
        append_string(buf, isin);
        append_string(buf, currency);
        append_le32(buf, short_selling_flag ? 1u : 0u);
        append_le32(buf, aggregated_order ? 1u : 0u);
        append_le64(buf, static_cast<uint64_t>(validity_period_ns));
        append_string(buf, parent_order_id);

        return buf;
    }

    /// Reconstruct a DecisionRecord from its serialized byte representation.
    ///
    /// @param data  Pointer to the serialized bytes.
    /// @param size  Number of bytes available at @p data.
    /// @return The deserialized record, or an error if the data is truncated.
    [[nodiscard]] static inline expected<DecisionRecord> deserialize(
            const uint8_t* data, size_t size) {
        size_t offset = 0;
        DecisionRecord rec;

        if (!read_le64(data, size, offset, rec.timestamp_ns)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated timestamp_ns"};
        }
        if (!read_le32(data, size, offset, rec.strategy_id)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated strategy_id"};
        }
        if (!read_string(data, size, offset, rec.model_version)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated model_version"};
        }

        int32_t dt_val = 0;
        if (!read_le32(data, size, offset, dt_val)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated decision_type"};
        }
        rec.decision_type = static_cast<DecisionType>(dt_val);
        if (dt_val < 0 || dt_val > 7)
            rec.decision_type = DecisionType::SIGNAL;

        uint32_t feat_count = 0;
        if (!read_le32_u(data, size, offset, feat_count)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated feature count"};
        }
        if (feat_count > (size - offset) / sizeof(float)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: feature count exceeds remaining data"};
        }
        rec.input_features.resize(feat_count);
        for (uint32_t i = 0; i < feat_count; ++i) {
            if (!read_float(data, size, offset, rec.input_features[i])) {
                return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated feature data"};
            }
        }

        if (!read_float(data, size, offset, rec.model_output)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated model_output"};
        }
        if (!read_float(data, size, offset, rec.confidence)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated confidence"};
        }

        int32_t rr_val = 0;
        if (!read_le32(data, size, offset, rr_val)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated risk_result"};
        }
        rec.risk_result = static_cast<RiskGateResult>(rr_val);
        if (rr_val < 0 || rr_val > 3)
            rec.risk_result = RiskGateResult::PASSED;

        if (!read_string(data, size, offset, rec.order_id)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated order_id"};
        }
        if (!read_string(data, size, offset, rec.symbol)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated symbol"};
        }
        if (!read_double(data, size, offset, rec.price)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated price"};
        }
        if (!read_double(data, size, offset, rec.quantity)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated quantity"};
        }
        if (!read_string(data, size, offset, rec.venue)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated venue"};
        }
        if (!read_string(data, size, offset, rec.notes)) {
            return Error{ErrorCode::CORRUPT_PAGE, "DecisionRecord: truncated notes"};
        }

        // MiFID II RTS 24 Annex I fields (Gap R-4) — optional for backward compat
        if (offset < size) {
            int32_t bs_val = 0;
            if (read_le32(data, size, offset, bs_val)) {
                rec.buy_sell = static_cast<BuySellIndicator>(bs_val);
                if (bs_val < 0 || bs_val > 2)
                    rec.buy_sell = BuySellIndicator::BUY;
            }
            int32_t ot_val = 0;
            if (read_le32(data, size, offset, ot_val)) {
                rec.order_type = static_cast<OrderType>(ot_val);
                if (ot_val < 0 || (ot_val > 4 && ot_val != 99))
                    rec.order_type = OrderType::MARKET;
            }
            int32_t tif_val = 0;
            if (read_le32(data, size, offset, tif_val)) {
                rec.time_in_force = static_cast<TimeInForce>(tif_val);
                if (tif_val < 0 || (tif_val > 4 && tif_val != 99))
                    rec.time_in_force = TimeInForce::DAY;
            }
            read_string(data, size, offset, rec.isin);
            read_string(data, size, offset, rec.currency);
            uint32_t ssf = 0;
            if (read_le32_u(data, size, offset, ssf))
                rec.short_selling_flag = (ssf != 0);
            uint32_t agg = 0;
            if (read_le32_u(data, size, offset, agg))
                rec.aggregated_order = (agg != 0);
            int64_t vp = 0;
            if (read_le64(data, size, offset, vp))
                rec.validity_period_ns = vp;
            read_string(data, size, offset, rec.parent_order_id);
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

    static inline void append_double(std::vector<uint8_t>& buf, double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, 8);
        append_le64(buf, bits);
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

    static inline bool read_double(const uint8_t* data, size_t size, size_t& offset, double& out) {
        if (offset + 8 > size) return false;
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            bits |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
        }
        std::memcpy(&out, &bits, 8);
        offset += 8;
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

/// Encode a float vector as a JSON array string (e.g. "[1.5,2.3,-0.1]").
///
/// No external JSON library needed. Uses snprintf for portable float formatting.
///
/// @param features  The feature vector to encode.
/// @return A JSON array string, or "[]" if empty.
inline std::string features_to_json(const std::vector<float>& features) {
    if (features.empty()) return "[]";

    std::string result = "[";
    for (size_t i = 0; i < features.size(); ++i) {
        if (i > 0) result += ',';
        // Use snprintf for portable float formatting
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.8g", static_cast<double>(features[i]));
        result += buf;
    }
    result += ']';
    return result;
}

/// Decode a JSON array string (e.g. "[1.5,2.3,-0.1]") to a float vector.
///
/// Handles simple JSON arrays of numeric values. Unparseable values are silently skipped.
///
/// @param json  The JSON array string to decode.
/// @return The parsed float vector, or empty if the input is malformed.
inline std::vector<float> json_to_features(const std::string& json) {
    // CWE-400: Uncontrolled Resource Consumption — cap parsed elements at 1M
    // to prevent memory exhaustion from crafted JSON input.
    static constexpr size_t MAX_JSON_ARRAY_ELEMENTS = 1'000'000;
    std::vector<float> result;
    if (json.size() < 2 || json.front() != '[' || json.back() != ']') {
        return result;
    }

    size_t pos = 1;  // Skip '['
    size_t end = json.size() - 1;  // Before ']'

    while (pos < end) {
        if (result.size() >= MAX_JSON_ARRAY_ELEMENTS) break;
        // Skip whitespace
        while (pos < end && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        if (pos >= end) break;

        // Find end of number (next comma or end)
        size_t num_start = pos;
        while (pos < end && json[pos] != ',') ++pos;

        // Parse the number
        std::string num_str = json.substr(num_start, pos - num_start);
        // Trim trailing whitespace
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

        // Skip comma
        if (pos < end && json[pos] == ',') ++pos;
    }

    return result;
}

} // namespace detail

/// Build the Parquet schema for AI decision log files.
///
/// Columns:
///   timestamp_ns     INT64  (TIMESTAMP_NS)   -- Decision timestamp
///   strategy_id      INT32                    -- Strategy identifier
///   model_version    BYTE_ARRAY (STRING)      -- Model version hash
///   decision_type    INT32                    -- DecisionType enum value
///   input_features   BYTE_ARRAY (STRING)      -- JSON array of floats
///   model_output     DOUBLE                   -- Primary model output
///   confidence       DOUBLE                   -- Model confidence [0,1]
///   risk_result      INT32                    -- RiskGateResult enum value
///   order_id         BYTE_ARRAY (STRING)      -- Associated order ID
///   symbol           BYTE_ARRAY (STRING)      -- Trading symbol
///   price            DOUBLE                   -- Decision price
///   quantity         DOUBLE                   -- Decision quantity
///   venue            BYTE_ARRAY (STRING)      -- Execution venue
///   notes            BYTE_ARRAY (STRING)      -- Free-text notes
///   chain_seq        INT64                    -- Hash chain sequence number
///   chain_hash       BYTE_ARRAY (STRING)      -- Hex entry hash
///   prev_hash        BYTE_ARRAY (STRING)      -- Hex previous hash
[[nodiscard]] inline Schema decision_log_schema() {
    return Schema::builder("decision_log")
        .column<int64_t>("timestamp_ns", LogicalType::TIMESTAMP_NS)
        .column<int32_t>("strategy_id")
        .column<std::string>("model_version")
        .column<int32_t>("decision_type")
        .column<std::string>("input_features")  // JSON array
        .column<double>("model_output")
        .column<double>("confidence")
        .column<int32_t>("risk_result")
        .column<std::string>("order_id")
        .column<std::string>("symbol")
        .column<double>("price")
        .column<double>("quantity")
        .column<std::string>("venue")
        .column<std::string>("notes")
        .column<int64_t>("chain_seq")
        .column<std::string>("chain_hash")
        .column<std::string>("prev_hash")
        .column<int64_t>("row_id")
        .column<int32_t>("row_version")
        .column<std::string>("row_origin_file")
        .column<std::string>("row_prev_hash")
        .build();
}

/// Writes AI trading decision records to Parquet files with cryptographic
/// hash chaining for tamper-evident audit trails.
///
/// Usage:
///   DecisionLogWriter writer("/audit/decisions");
///   DecisionRecord rec;
///   rec.timestamp_ns = now_ns();
///   rec.strategy_id  = 1;
///   rec.symbol       = "BTC-USD";
///   // ... fill other fields ...
///   auto entry = writer.log(rec);
///   writer.close();
///
/// Files are named: decision_log_{chain_id}_{start_seq}_{end_seq}.parquet
/// Each file embeds AuditMetadata in the Parquet key-value metadata for
/// chain continuity verification across file rotations.
class DecisionLogWriter {
public:
    /// Create a decision log writer.
    /// @param output_dir   Directory to write Parquet files to
    /// @param chain_id     Unique chain identifier (auto-generated if empty)
    /// @param max_records  Max records per file before rotating (default 100,000)
    inline DecisionLogWriter(const std::string& output_dir,
                             const std::string& chain_id = "",
                             size_t max_records = 100000)
        : output_dir_(output_dir)
        , chain_id_(chain_id.empty() ? generate_chain_id() : chain_id)
        , max_records_(max_records)
        , schema_(decision_log_schema())
        , lineage_tracker_(chain_id.empty() ? chain_id_ : chain_id, 1)
    {
        auto license = commercial::require_feature("DecisionLogWriter");
        if (!license) {
            throw std::runtime_error(license.error().message);
        }

        // Validate output_dir: must not be empty or contain ".." traversal segments
        // CWE-59: Improper Link Resolution Before File Access — this check
        // catches literal ".." path components but does NOT resolve symlinks.
        // A symlink pointing outside the intended directory tree would bypass
        // this guard. Full symlink resolution (e.g., via
        // std::filesystem::canonical()) is not used here because the target
        // directory may not exist yet at validation time. Deployers should
        // ensure the output directory is not a symlink to an untrusted location.
        if (output_dir_.empty())
            throw std::invalid_argument("DecisionLogWriter: output_dir must not be empty");
        for (size_t s = 0, e; s <= output_dir_.size(); s = e + 1) {
            e = output_dir_.find_first_of("/\\", s);
            if (e == std::string::npos) e = output_dir_.size();
            if (output_dir_.substr(s, e - s) == "..")
                throw std::invalid_argument(
                    "DecisionLogWriter: output_dir must not contain '..' path traversal");
        }
        // Validate chain_id: must match [a-zA-Z0-9_-]+
        for (char c : chain_id_) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-')
                throw std::invalid_argument(
                    "DecisionLogWriter: chain_id must only contain [a-zA-Z0-9_-]");
        }
    }

    /// Set an optional symbol/instrument validator callback (MiFID II RTS 24).
    /// If set, each record's `symbol` is validated before logging. Invalid
    /// symbols cause log() to return INVALID_ARGUMENT.
    void set_instrument_validator(std::function<bool(const std::string&)> validator) {
        instrument_validator_ = std::move(validator);
    }

    /// Log a trading decision. Returns the hash chain entry.
    ///
    /// The record is serialized, hashed, and appended to the hash chain.
    /// If the pending buffer reaches max_records, the file is automatically
    /// flushed and rotated.
    [[nodiscard]] inline expected<HashChainEntry> log(const DecisionRecord& record) {
        auto usage = commercial::record_usage_rows("DecisionLogWriter::log", 1);
        if (!usage) return usage.error();

        // Validate symbol if an instrument validator is registered (MiFID II RTS 24)
        if (instrument_validator_ && !instrument_validator_(record.symbol)) {
            return Error{ErrorCode::INVALID_ARGUMENT,
                         "DecisionLog: invalid symbol '" + record.symbol + "' (MiFID II RTS 24)"};
        }

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
    /// defined by decision_log_schema(). The hash chain metadata is
    /// embedded in the file's key-value metadata section.
    [[nodiscard]] inline expected<void> flush() {
        if (pending_records_.empty()) {
            return expected<void>{};
        }

        // Build file path: decision_log_{chain_id}_{start_seq}_{end_seq}.parquet
        int64_t start_seq = pending_entries_.front().sequence_number;
        int64_t end_seq   = pending_entries_.back().sequence_number;

        current_file_path_ = output_dir_ + "/decision_log_" + chain_id_ + "_"
                           + std::to_string(start_seq) + "_"
                           + std::to_string(end_seq) + ".parquet";

        // Prepare writer options with audit metadata
        WriterOptions opts;
        opts.created_by = "SignetStack signet-forge decision_log v1.0";

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
            row.reserve(21);

            row.push_back(std::to_string(rec.timestamp_ns));
            row.push_back(std::to_string(rec.strategy_id));
            row.push_back(rec.model_version);
            row.push_back(std::to_string(static_cast<int32_t>(rec.decision_type)));
            row.push_back(detail::features_to_json(rec.input_features));
            row.push_back(double_to_string(static_cast<double>(rec.model_output)));
            row.push_back(double_to_string(static_cast<double>(rec.confidence)));
            row.push_back(std::to_string(static_cast<int32_t>(rec.risk_result)));
            row.push_back(rec.order_id);
            row.push_back(rec.symbol);
            row.push_back(double_to_string(rec.price));
            row.push_back(double_to_string(rec.quantity));
            row.push_back(rec.venue);
            row.push_back(rec.notes);
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
            // No pending, but chain has history
            const auto& last = chain_.entries().back();
            meta.start_sequence = last.sequence_number;
            meta.end_sequence  = last.sequence_number;
            meta.first_hash     = hash_to_hex(last.entry_hash);
            meta.last_hash      = hash_to_hex(last.entry_hash);
        }

        meta.record_count   = static_cast<int64_t>(pending_entries_.size());
        meta.record_type    = "decision";
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
    std::string                         output_dir_;
    std::string                         chain_id_;
    size_t                              max_records_;
    Schema                              schema_;
    AuditChainWriter                    chain_;
    std::vector<DecisionRecord>         pending_records_;
    std::vector<HashChainEntry>         pending_entries_;
    std::vector<std::vector<uint8_t>>   pending_data_;  // Serialized record bytes for lineage hashing
    RowLineageTracker                   lineage_tracker_;
    std::string                         current_file_path_;
    int64_t                             total_records_{0};
    int64_t                             file_count_{0};
    std::function<bool(const std::string&)> instrument_validator_; ///< Optional symbol validator (MiFID II RTS 24)

    /// Format a double to string with enough precision for round-tripping.
    static inline std::string double_to_string(double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        return buf;
    }
};

/// Reads AI decision log Parquet files and verifies hash chain integrity.
///
/// Usage:
///   auto reader = DecisionLogReader::open("decision_log_abc123_0_999.parquet");
///   auto records = reader->read_all();
///   auto verify  = reader->verify_chain();
///   if (!verify.valid) { /* tampered! */ }
class DecisionLogReader {
public:
    /// Open a decision log Parquet file and pre-load all column data.
    ///
    /// @param path  Path to the decision log Parquet file.
    /// @return The reader, or an error if the file cannot be opened or parsed.
    [[nodiscard]] static inline expected<DecisionLogReader> open(const std::string& path) {
        auto license = commercial::require_feature("DecisionLogReader");
        if (!license) return license.error();

        auto reader_result = ParquetReader::open(path);
        if (!reader_result) return reader_result.error();

        DecisionLogReader dlr;
        dlr.reader_ = std::make_unique<ParquetReader>(std::move(*reader_result));
        dlr.path_   = path;

        // Pre-read all column data from all row groups
        auto load_result = dlr.load_columns();
        if (!load_result) return load_result.error();

        return dlr;
    }

    /// @name Move-only semantics
    /// @{
    DecisionLogReader() = default;
    DecisionLogReader(const DecisionLogReader&) = delete;
    DecisionLogReader& operator=(const DecisionLogReader&) = delete;
    DecisionLogReader(DecisionLogReader&&) = default;
    DecisionLogReader& operator=(DecisionLogReader&&) = default;
    /// @}

    /// Get all decision records from the file.
    ///
    /// @return A vector of DecisionRecord objects reconstructed from all row groups.
    [[nodiscard]] inline expected<std::vector<DecisionRecord>> read_all() const {
        size_t n = col_timestamp_ns_.size();
        std::vector<DecisionRecord> records;
        records.reserve(n);

        for (size_t i = 0; i < n; ++i) {
            DecisionRecord rec;
            rec.timestamp_ns  = col_timestamp_ns_[i];
            rec.strategy_id   = col_strategy_id_[i];
            rec.model_version = col_model_version_[i];
            rec.decision_type = static_cast<DecisionType>(col_decision_type_[i]);
            if (col_decision_type_[i] < 0 || col_decision_type_[i] > 7)
                rec.decision_type = DecisionType::SIGNAL;
            rec.input_features = detail::json_to_features(col_input_features_[i]);
            rec.model_output  = static_cast<float>(col_model_output_[i]);
            rec.confidence    = static_cast<float>(col_confidence_[i]);
            rec.risk_result   = static_cast<RiskGateResult>(col_risk_result_[i]);
            if (col_risk_result_[i] < 0 || col_risk_result_[i] > 3)
                rec.risk_result = RiskGateResult::PASSED;
            rec.order_id      = col_order_id_[i];
            rec.symbol        = col_symbol_[i];
            rec.price         = col_price_[i];
            rec.quantity      = col_quantity_[i];
            rec.venue         = col_venue_[i];
            rec.notes         = col_notes_[i];
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
            DecisionRecord rec;
            rec.timestamp_ns  = col_timestamp_ns_[i];
            rec.strategy_id   = col_strategy_id_[i];
            rec.model_version = col_model_version_[i];
            rec.decision_type = static_cast<DecisionType>(col_decision_type_[i]);
            if (col_decision_type_[i] < 0 || col_decision_type_[i] > 7)
                rec.decision_type = DecisionType::SIGNAL;
            rec.input_features = detail::json_to_features(col_input_features_[i]);
            rec.model_output  = static_cast<float>(col_model_output_[i]);
            rec.confidence    = static_cast<float>(col_confidence_[i]);
            rec.risk_result   = static_cast<RiskGateResult>(col_risk_result_[i]);
            if (col_risk_result_[i] < 0 || col_risk_result_[i] > 3)
                rec.risk_result = RiskGateResult::PASSED;
            rec.order_id      = col_order_id_[i];
            rec.symbol        = col_symbol_[i];
            rec.price         = col_price_[i];
            rec.quantity      = col_quantity_[i];
            rec.venue         = col_venue_[i];
            rec.notes         = col_notes_[i];

            auto data = rec.serialize();
            entry.data_hash = crypto::detail::sha256::sha256(data.data(), data.size());

            entries.push_back(std::move(entry));
        }

        // Verify using AuditChainVerifier
        AuditChainVerifier verifier;
        return verifier.verify(entries);
    }

    /// Get the schema of the decision log file.
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
    std::vector<int32_t>     col_strategy_id_;
    std::vector<std::string> col_model_version_;
    std::vector<int32_t>     col_decision_type_;
    std::vector<std::string> col_input_features_;
    std::vector<double>      col_model_output_;
    std::vector<double>      col_confidence_;
    std::vector<int32_t>     col_risk_result_;
    std::vector<std::string> col_order_id_;
    std::vector<std::string> col_symbol_;
    std::vector<double>      col_price_;
    std::vector<double>      col_quantity_;
    std::vector<std::string> col_venue_;
    std::vector<std::string> col_notes_;
    std::vector<int64_t>     col_chain_seq_;
    std::vector<std::string> col_chain_hash_;
    std::vector<std::string> col_prev_hash_;

    /// Load all columns from all row groups into the internal vectors.
    [[nodiscard]] inline expected<void> load_columns() {
        int64_t num_rgs = reader_->num_row_groups();

        for (int64_t rg = 0; rg < num_rgs; ++rg) {
            size_t rg_idx = static_cast<size_t>(rg);

            // Read each typed column and append to our vectors
            auto r0 = reader_->read_column<int64_t>(rg_idx, 0);
            if (!r0) return r0.error();
            col_timestamp_ns_.insert(col_timestamp_ns_.end(), r0->begin(), r0->end());

            auto r1 = reader_->read_column<int32_t>(rg_idx, 1);
            if (!r1) return r1.error();
            col_strategy_id_.insert(col_strategy_id_.end(), r1->begin(), r1->end());

            auto r2 = reader_->read_column<std::string>(rg_idx, 2);
            if (!r2) return r2.error();
            col_model_version_.insert(col_model_version_.end(),
                std::make_move_iterator(r2->begin()), std::make_move_iterator(r2->end()));

            auto r3 = reader_->read_column<int32_t>(rg_idx, 3);
            if (!r3) return r3.error();
            col_decision_type_.insert(col_decision_type_.end(), r3->begin(), r3->end());

            auto r4 = reader_->read_column<std::string>(rg_idx, 4);
            if (!r4) return r4.error();
            col_input_features_.insert(col_input_features_.end(),
                std::make_move_iterator(r4->begin()), std::make_move_iterator(r4->end()));

            auto r5 = reader_->read_column<double>(rg_idx, 5);
            if (!r5) return r5.error();
            col_model_output_.insert(col_model_output_.end(), r5->begin(), r5->end());

            auto r6 = reader_->read_column<double>(rg_idx, 6);
            if (!r6) return r6.error();
            col_confidence_.insert(col_confidence_.end(), r6->begin(), r6->end());

            auto r7 = reader_->read_column<int32_t>(rg_idx, 7);
            if (!r7) return r7.error();
            col_risk_result_.insert(col_risk_result_.end(), r7->begin(), r7->end());

            auto r8 = reader_->read_column<std::string>(rg_idx, 8);
            if (!r8) return r8.error();
            col_order_id_.insert(col_order_id_.end(),
                std::make_move_iterator(r8->begin()), std::make_move_iterator(r8->end()));

            auto r9 = reader_->read_column<std::string>(rg_idx, 9);
            if (!r9) return r9.error();
            col_symbol_.insert(col_symbol_.end(),
                std::make_move_iterator(r9->begin()), std::make_move_iterator(r9->end()));

            auto r10 = reader_->read_column<double>(rg_idx, 10);
            if (!r10) return r10.error();
            col_price_.insert(col_price_.end(), r10->begin(), r10->end());

            auto r11 = reader_->read_column<double>(rg_idx, 11);
            if (!r11) return r11.error();
            col_quantity_.insert(col_quantity_.end(), r11->begin(), r11->end());

            auto r12 = reader_->read_column<std::string>(rg_idx, 12);
            if (!r12) return r12.error();
            col_venue_.insert(col_venue_.end(),
                std::make_move_iterator(r12->begin()), std::make_move_iterator(r12->end()));

            auto r13 = reader_->read_column<std::string>(rg_idx, 13);
            if (!r13) return r13.error();
            col_notes_.insert(col_notes_.end(),
                std::make_move_iterator(r13->begin()), std::make_move_iterator(r13->end()));

            auto r14 = reader_->read_column<int64_t>(rg_idx, 14);
            if (!r14) return r14.error();
            col_chain_seq_.insert(col_chain_seq_.end(), r14->begin(), r14->end());

            auto r15 = reader_->read_column<std::string>(rg_idx, 15);
            if (!r15) return r15.error();
            col_chain_hash_.insert(col_chain_hash_.end(),
                std::make_move_iterator(r15->begin()), std::make_move_iterator(r15->end()));

            auto r16 = reader_->read_column<std::string>(rg_idx, 16);
            if (!r16) return r16.error();
            col_prev_hash_.insert(col_prev_hash_.end(),
                std::make_move_iterator(r16->begin()), std::make_move_iterator(r16->end()));
        }

        return expected<void>{};
    }
};

} // namespace signet::forge
