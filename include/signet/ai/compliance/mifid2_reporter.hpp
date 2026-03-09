// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
// mifid2_reporter.hpp — MiFID II RTS 24 Algorithmic Trading Compliance Reporter
// Phase 10d: Compliance Report Generators
//
// Reads DecisionLog Parquet files written by DecisionLogWriter and generates
// MiFID II RTS 24-compliant reports covering algorithmic trading decisions.
//
// Regulatory reference:
//   Commission Delegated Regulation (EU) 2017/589 — RTS 24
//   Article 9: record-keeping for algorithmic trading
//   Annex I: fields required for each order/decision
//
// MiFID II RTS 24 Annex I field mapping from DecisionRecord:
//   Field  1 — Entity ID (firm_id from ReportOptions, or strategy_id)
//   Field  2 — Trading venue transaction ID (order_id)
//   Field  3 — Client ID (redacted — GDPR Art.25 pseudonymisation)
//   Field  4 — Investment decision maker (strategy_id = algorithm identifier)
//   Field  5 — Financial instrument (symbol)
//   Field  6 — Unit price / limit price (price)
//   Field  7 — Original quantity (quantity)
//   Field  8 — Date/time (timestamp_ns → ISO 8601 to nanosecond precision)
//   Field  9 — Decision type (ORDER_NEW, ORDER_CANCEL, etc.)
//   Field 10 — Venue (venue)
//   Field 11 — Algorithm model version (model_version)
//   Field 12 — Risk gate outcome (risk_result)
//   Field 13 — Model output score (model_output)
//   Field 14 — Confidence (confidence)
//   Field 15 — Chain sequence number (tamper-evidence)
//   Field 16 — Chain entry hash (hex, 64 chars)
//
// Usage:
//   ReportOptions opts;
//   opts.firm_id   = "FIRM_LEI_123456";
//   opts.format    = ReportFormat::JSON;
//   opts.start_ns  = t0;
//   opts.end_ns    = t1;
//   auto report = MiFID2Reporter::generate({"decisions_0.parquet"}, opts);
//   if (report) std::cout << report->content;

#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/compliance/mifid2_reporter.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

#include "signet/error.hpp"
#include "signet/ai/decision_log.hpp"
#include "signet/ai/compliance/compliance_types.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
#  include <stdlib.h>  // arc4random_buf
#elif !defined(_WIN32)
#  include <sys/random.h>  // getrandom
#endif

namespace signet::forge {

/// MiFID II RTS 24 algorithmic trading compliance report generator.
///
/// Reads DecisionLog Parquet files written by DecisionLogWriter and generates
/// MiFID II RTS 24-compliant reports covering algorithmic trading decisions.
///
/// Maps DecisionRecord fields to RTS 24 Annex I fields (1--16) for
/// regulatory submission. Supports JSON, NDJSON, and CSV output formats.
///
/// @see ComplianceStandard::MIFID2_RTS24
/// @see DecisionLogWriter
class MiFID2Reporter {
public:
    /// Generate a MiFID II RTS 24 compliance report from decision log files.
    ///
    /// Reads all records from the supplied DecisionLog Parquet files, filters
    /// by opts.start_ns / opts.end_ns, optionally verifies hash chains, and
    /// serializes the result in the requested format.
    ///
    /// @param log_files  Paths to DecisionLog Parquet files.
    /// @param opts       Report options (time window, format, verification).
    /// @return The generated ComplianceReport, or an error.
    [[nodiscard]] static expected<ComplianceReport> generate(
            const std::vector<std::string>& log_files,
            const ReportOptions& opts = {}) {

        auto license = commercial::require_feature("MiFID2Reporter");
        if (!license) return license.error();

        if (log_files.empty())
            return Error{ErrorCode::IO_ERROR,
                         "MiFID2Reporter: no log files supplied"};

        std::vector<DecisionRecord> records;
        bool     chain_ok = true;
        std::string chain_id;

        for (const auto& path : log_files) {
            auto rdr_result = DecisionLogReader::open(path);
            if (!rdr_result) {
                // Fail on unreadable files — silent skip risks incomplete reports (CWE-754)
                return Error{ErrorCode::IO_ERROR,
                             "MiFID2Reporter: cannot open log file '" + path +
                             "': " + rdr_result.error().message};
            }
            auto& rdr = *rdr_result;

            // Chain verification
            if (opts.verify_chain) {
                auto vr = rdr.verify_chain();
                if (!vr.valid) chain_ok = false;
            }

            // Metadata (chain_id from first file)
            if (chain_id.empty()) {
                auto meta_result = rdr.audit_metadata();
                if (meta_result) chain_id = meta_result->chain_id;
            }

            // Read records
            auto all_result = rdr.read_all();
            if (!all_result) continue;

            for (auto& rec : *all_result) {
                if (rec.timestamp_ns >= opts.start_ns &&
                    rec.timestamp_ns <= opts.end_ns)
                    records.push_back(std::move(rec));
            }
        }

        // Sort by timestamp ascending
        std::sort(records.begin(), records.end(),
            [](const DecisionRecord& a, const DecisionRecord& b) {
                return a.timestamp_ns < b.timestamp_ns;
            });

        auto usage = commercial::record_usage_rows(
            "MiFID2Reporter::generate", static_cast<uint64_t>(records.size()));
        if (!usage) return usage.error();

        // Build report
        ComplianceReport report;
        report.standard        = ComplianceStandard::MIFID2_RTS24;
        report.format          = opts.format;
        report.chain_verified  = chain_ok;
        report.chain_id        = chain_id;
        report.total_records   = static_cast<int64_t>(records.size());
        report.generated_at_ns = now_ns_();
        report.generated_at_iso = ns_to_iso8601(report.generated_at_ns);
        report.period_start_iso = ns_to_iso8601(opts.start_ns);
        report.period_end_iso   = (opts.end_ns == (std::numeric_limits<int64_t>::max)())
                                      ? "open"
                                      : ns_to_iso8601(opts.end_ns);
        // MiFID II RTS 24 Annex I Table 2, Field 1: unique report ID.
        // CSPRNG random hex suffix prevents predictable/guessable report IDs
        // (timestamp alone is insufficient for uniqueness under concurrent generation).
        report.report_id = opts.report_id.empty()
                               ? ("MIFID2-" + std::to_string(report.generated_at_ns)
                                  + "-" + random_hex_suffix_())
                               : opts.report_id;

        switch (opts.format) {
            case ReportFormat::JSON:
            case ReportFormat::NDJSON:
                report.content = format_json(records, opts, report, chain_ok);
                break;
            case ReportFormat::CSV:
                report.content = format_csv(records, opts);
                break;
        }

        return report;
    }

    /// Get the static CSV column header line (Annex I field order).
    ///
    /// @return A CSV header string with trailing newline.
    [[nodiscard]] static std::string csv_header() {
        return "timestamp_utc,order_id,firm_id,algo_identifier,"
               "model_version,instrument,venue,decision_type,"
               "price,quantity,risk_gate,model_output,confidence,"
               "buy_sell,order_type,time_in_force,isin,currency,"
               "short_selling,aggregated_order,parent_order_id,"
               "chain_seq,chain_hash\n";
    }

    /// Serialize a single DecisionRecord as a CSV row.
    ///
    /// Useful for streaming output where records are emitted one at a time.
    /// Chain fields (chain_seq, chain_hash) are left empty in the output.
    ///
    /// @param rec      The decision record to serialize.
    /// @param firm_id  Firm identifier for MiFID II field 1 (defaults to strategy_id if empty).
    /// @return A CSV row string with trailing newline.
    [[nodiscard]] static std::string record_to_csv_row(
            const DecisionRecord& rec,
            const std::string& firm_id = "") {

        std::string row;
        row += csv_escape(ns_to_iso8601(rec.timestamp_ns)) + ",";
        row += csv_escape(rec.order_id)                    + ",";
        row += csv_escape(firm_id.empty()
                              ? std::to_string(rec.strategy_id) : firm_id) + ",";
        row += csv_escape(std::to_string(rec.strategy_id))                 + ",";
        row += csv_escape(rec.model_version)               + ",";
        row += csv_escape(rec.symbol)                      + ",";
        row += csv_escape(rec.venue)                       + ",";
        row += csv_escape(decision_type_str(rec.decision_type)) + ",";
        row += double_str(rec.price)                       + ",";
        row += double_str(rec.quantity)                    + ",";
        row += csv_escape(risk_result_str(rec.risk_result)) + ",";
        row += double_str(rec.model_output)                + ",";
        row += double_str(rec.confidence)                  + ",";
        // RTS 24 Annex I mandatory fields (Gap R-4)
        row += csv_escape(buy_sell_str(rec.buy_sell))       + ",";
        row += csv_escape(order_type_str(rec.order_type))   + ",";
        row += csv_escape(time_in_force_str(rec.time_in_force)) + ",";
        row += csv_escape(rec.isin)                        + ",";
        row += csv_escape(rec.currency)                    + ",";
        row += (rec.short_selling_flag ? "true" : "false") + std::string(",");
        row += (rec.aggregated_order ? "true" : "false")   + std::string(",");
        row += csv_escape(rec.parent_order_id)             + ",";
        // chain fields omitted for single-record streaming use
        row += ",\n";
        return row;
    }

private:
    // -------------------------------------------------------------------------
    // JSON formatter
    // -------------------------------------------------------------------------

    static std::string format_json(
            const std::vector<DecisionRecord>& records,
            const ReportOptions& opts,
            const ComplianceReport& meta,
            bool chain_ok) {

        const bool ndjson = (opts.format == ReportFormat::NDJSON);
        const std::string ind  = opts.pretty_print && !ndjson ? "  "   : "";
        const std::string ind2 = opts.pretty_print && !ndjson ? "    " : "";
        const std::string nl   = opts.pretty_print && !ndjson ? "\n"   : "";
        const std::string sp   = opts.pretty_print && !ndjson ? " "    : "";

        if (ndjson) {
            // One JSON object per record, no outer envelope
            std::string out;
            for (const auto& rec : records)
                out += record_to_json_line(rec, opts) + "\n";
            return out;
        }

        std::string o;
        o += "{" + nl;
        o += ind + "\"report_type\":" + sp + "\"MiFID_II_RTS_24\"," + nl;
        o += ind + "\"regulatory_reference\":" + sp
           + "\"Commission Delegated Regulation (EU) 2017/589 — RTS 24\"," + nl;
        o += ind + "\"report_id\":" + sp + "\"" + j(meta.report_id) + "\"," + nl;
        o += ind + "\"generated_at\":" + sp + "\"" + meta.generated_at_iso + "\"," + nl;
        o += ind + "\"period_start\":" + sp + "\"" + meta.period_start_iso + "\"," + nl;
        o += ind + "\"period_end\":" + sp + "\"" + meta.period_end_iso + "\"," + nl;
        o += ind + "\"firm_id\":" + sp + "\""
           + j(opts.firm_id.empty() ? "UNSPECIFIED" : opts.firm_id)
           + "\"," + nl;
        o += ind + "\"chain_id\":" + sp + "\"" + j(meta.chain_id) + "\"," + nl;
        o += ind + "\"chain_verified\":" + sp + (chain_ok ? "true" : "false") + "," + nl;
        o += ind + "\"total_records\":" + sp + std::to_string(records.size()) + "," + nl;
        o += ind + "\"records\":" + sp + "[" + nl;

        for (size_t i = 0; i < records.size(); ++i) {
            const auto& rec = records[i];
            o += ind2 + "{" + nl;
            // Annex I fields
            o += ind2 + ind + "\"field_01_firm_id\":" + sp
               + "\"" + j(opts.firm_id.empty() ? std::to_string(rec.strategy_id) : opts.firm_id)
               + "\"," + nl;
            o += ind2 + ind + "\"field_02_order_id\":" + sp
               + "\"" + j(rec.order_id) + "\"," + nl;
            o += ind2 + ind + "\"field_03_client_id\":" + sp
               + "\"[REDACTED-GDPR]\"," + nl;
            o += ind2 + ind + "\"field_04_algo_identifier\":" + sp
               + "\"" + j(std::to_string(rec.strategy_id)) + "\"," + nl;
            o += ind2 + ind + "\"field_05_instrument\":" + sp
               + "\"" + j(rec.symbol) + "\"," + nl;
            o += ind2 + ind + "\"field_06_price\":" + sp
               + double_str(rec.price, opts.price_significant_digits) + "," + nl;
            o += ind2 + ind + "\"field_07_quantity\":" + sp
               + double_str(rec.quantity) + "," + nl;
            o += ind2 + ind + "\"field_08_timestamp_utc\":" + sp
               + "\"" + ns_to_iso8601(rec.timestamp_ns, opts.timestamp_granularity) + "\"," + nl;
            o += ind2 + ind + "\"field_09_decision_type\":" + sp
               + "\"" + decision_type_str(rec.decision_type) + "\"," + nl;
            o += ind2 + ind + "\"field_10_venue\":" + sp
               + "\"" + j(rec.venue) + "\"," + nl;
            o += ind2 + ind + "\"field_11_model_version\":" + sp
               + "\"" + j(rec.model_version) + "\"," + nl;
            o += ind2 + ind + "\"field_12_risk_gate\":" + sp
               + "\"" + risk_result_str(rec.risk_result) + "\"," + nl;
            o += ind2 + ind + "\"field_13_model_output\":" + sp
               + double_str(rec.model_output) + "," + nl;
            o += ind2 + ind + "\"field_14_confidence\":" + sp
               + double_str(rec.confidence) + "," + nl;
            // MiFID II RTS 24 Annex I mandatory fields (Gap R-4)
            o += ind2 + ind + "\"field_15_buy_sell\":" + sp
               + "\"" + buy_sell_str(rec.buy_sell) + "\"," + nl;
            o += ind2 + ind + "\"field_16_order_type\":" + sp
               + "\"" + order_type_str(rec.order_type) + "\"," + nl;
            o += ind2 + ind + "\"field_17_time_in_force\":" + sp
               + "\"" + time_in_force_str(rec.time_in_force) + "\"," + nl;
            o += ind2 + ind + "\"field_18_isin\":" + sp
               + "\"" + j(rec.isin) + "\"," + nl;
            o += ind2 + ind + "\"field_19_currency\":" + sp
               + "\"" + j(rec.currency) + "\"," + nl;
            o += ind2 + ind + "\"field_20_short_selling\":" + sp
               + (rec.short_selling_flag ? "true" : "false") + "," + nl;
            o += ind2 + ind + "\"field_21_aggregated_order\":" + sp
               + (rec.aggregated_order ? "true" : "false") + "," + nl;
            if (rec.validity_period_ns > 0) {
                o += ind2 + ind + "\"field_22_validity_period\":" + sp
                   + "\"" + ns_to_iso8601(rec.validity_period_ns) + "\"," + nl;
            }
            if (!rec.parent_order_id.empty()) {
                o += ind2 + ind + "\"field_23_parent_order_id\":" + sp
                   + "\"" + j(rec.parent_order_id) + "\"," + nl;
            }
            o += ind2 + ind + "\"field_24_notes\":" + sp
               + "\"" + j(rec.notes) + "\"";
            if (opts.include_features && !rec.input_features.empty()) {
                o += "," + nl;
                o += ind2 + ind + "\"input_features\":" + sp
                   + features_array(rec.input_features);
            }
            o += nl + ind2 + "}";
            if (i + 1 < records.size()) o += ",";
            o += nl;
        }
        o += ind + "]" + nl;
        o += "}" + nl;
        return o;
    }

    static std::string record_to_json_line(const DecisionRecord& rec,
                                            const ReportOptions& opts) {
        std::string o = "{";
        o += "\"ts\":\"" + ns_to_iso8601(rec.timestamp_ns, opts.timestamp_granularity) + "\",";
        o += "\"order_id\":\"" + j(rec.order_id) + "\",";
        o += "\"algo\":\"" + j(std::to_string(rec.strategy_id)) + "\",";
        o += "\"model\":\"" + j(rec.model_version) + "\",";
        o += "\"instrument\":\"" + j(rec.symbol) + "\",";
        o += "\"venue\":\"" + j(rec.venue) + "\",";
        o += "\"type\":\"" + decision_type_str(rec.decision_type) + "\",";
        o += "\"price\":" + double_str(rec.price, opts.price_significant_digits) + ",";
        o += "\"qty\":" + double_str(rec.quantity) + ",";
        o += "\"risk_gate\":\"" + risk_result_str(rec.risk_result) + "\",";
        o += "\"output\":" + double_str(rec.model_output) + ",";
        o += "\"conf\":" + double_str(rec.confidence) + ",";
        // RTS 24 Annex I mandatory fields (Gap R-4)
        o += "\"buy_sell\":\"" + buy_sell_str(rec.buy_sell) + "\",";
        o += "\"order_type\":\"" + order_type_str(rec.order_type) + "\",";
        o += "\"tif\":\"" + time_in_force_str(rec.time_in_force) + "\",";
        o += "\"isin\":\"" + j(rec.isin) + "\",";
        o += "\"ccy\":\"" + j(rec.currency) + "\",";
        o += "\"short_sell\":" + std::string(rec.short_selling_flag ? "true" : "false") + ",";
        o += "\"aggregated\":" + std::string(rec.aggregated_order ? "true" : "false");
        o += "}";
        return o;
    }

    // -------------------------------------------------------------------------
    // CSV formatter
    // -------------------------------------------------------------------------

    static std::string format_csv(const std::vector<DecisionRecord>& records,
                                   const ReportOptions& opts) {
        std::string out = csv_header();
        for (const auto& rec : records) {
            out += csv_escape(ns_to_iso8601(rec.timestamp_ns, opts.timestamp_granularity)) + ",";
            out += csv_escape(rec.order_id)                            + ",";
            out += csv_escape(opts.firm_id.empty()
                                  ? std::to_string(rec.strategy_id) : opts.firm_id)    + ",";
            out += csv_escape(std::to_string(rec.strategy_id))                         + ",";
            out += csv_escape(rec.model_version)                       + ",";
            out += csv_escape(rec.symbol)                              + ",";
            out += csv_escape(rec.venue)                               + ",";
            out += csv_escape(decision_type_str(rec.decision_type))    + ",";
            out += double_str(rec.price, opts.price_significant_digits) + ",";
            out += double_str(rec.quantity)                            + ",";
            out += csv_escape(risk_result_str(rec.risk_result))        + ",";
            out += double_str(rec.model_output)                        + ",";
            out += double_str(rec.confidence)                          + ",";
            // RTS 24 Annex I mandatory fields (Gap R-4)
            out += csv_escape(buy_sell_str(rec.buy_sell))               + ",";
            out += csv_escape(order_type_str(rec.order_type))           + ",";
            out += csv_escape(time_in_force_str(rec.time_in_force))    + ",";
            out += csv_escape(rec.isin)                                + ",";
            out += csv_escape(rec.currency)                            + ",";
            out += std::string(rec.short_selling_flag ? "true":"false") + ",";
            out += std::string(rec.aggregated_order ? "true":"false")   + ",";
            out += csv_escape(rec.parent_order_id)                     + ",";
            out += ",\n";  // chain_seq + chain_hash left empty (not in DecisionRecord)
        }
        return out;
    }

    // -------------------------------------------------------------------------
    // Utility helpers
    // -------------------------------------------------------------------------

    static int64_t now_ns_() {
        using namespace std::chrono;
        return static_cast<int64_t>(
            duration_cast<nanoseconds>(
                system_clock::now().time_since_epoch()).count());
    }

    static std::string ns_to_iso8601(int64_t ns,
                                     TimestampGranularity gran = TimestampGranularity::NANOS) {
        static_assert(sizeof(std::time_t) >= 8,
            "Signet compliance reporters require 64-bit time_t for timestamps beyond 2038");
        if (ns <= 0) return "1970-01-01T00:00:00.000000000Z";
        const int64_t secs    = ns / 1'000'000'000LL;
        const int64_t ns_part = ns % 1'000'000'000LL;
        std::time_t t = static_cast<std::time_t>(secs);
        std::tm tm_buf{};
#if defined(_WIN32)
        gmtime_s(&tm_buf, &t);
        std::tm* utc = &tm_buf;
#else
        std::tm* utc = gmtime_r(&t, &tm_buf);
#endif
        char date_buf[32];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%S", utc);
        char full_buf[48];
        // MiFID II RTS 24 Art.2(2): configurable timestamp granularity
        switch (gran) {
            case TimestampGranularity::MILLIS:
                std::snprintf(full_buf, sizeof(full_buf), "%s.%03lldZ",
                              date_buf, static_cast<long long>(ns_part / 1'000'000LL));
                break;
            case TimestampGranularity::MICROS:
                std::snprintf(full_buf, sizeof(full_buf), "%s.%06lldZ",
                              date_buf, static_cast<long long>(ns_part / 1'000LL));
                break;
            case TimestampGranularity::NANOS:
            default:
                std::snprintf(full_buf, sizeof(full_buf), "%s.%09lldZ",
                              date_buf, static_cast<long long>(ns_part));
                break;
        }
        return full_buf;
    }

    static std::string decision_type_str(DecisionType dt) {
        switch (dt) {
            case DecisionType::SIGNAL:         return "SIGNAL";
            case DecisionType::ORDER_NEW:      return "ORDER_NEW";
            case DecisionType::ORDER_CANCEL:   return "ORDER_CANCEL";
            case DecisionType::ORDER_MODIFY:   return "ORDER_MODIFY";
            case DecisionType::POSITION_OPEN:  return "POSITION_OPEN";
            case DecisionType::POSITION_CLOSE: return "POSITION_CLOSE";
            case DecisionType::RISK_OVERRIDE:  return "RISK_OVERRIDE";
            case DecisionType::NO_ACTION:      return "NO_ACTION";
        }
        return "UNKNOWN";
    }

    static std::string risk_result_str(RiskGateResult rg) {
        switch (rg) {
            case RiskGateResult::PASSED:   return "PASSED";
            case RiskGateResult::REJECTED: return "REJECTED";
            case RiskGateResult::MODIFIED: return "MODIFIED";
            case RiskGateResult::THROTTLED:return "THROTTLED";
        }
        return "UNKNOWN";
    }

    // MiFID II RTS 24 Annex I enum stringifiers (Gap R-4)
    static std::string buy_sell_str(BuySellIndicator bs) {
        switch (bs) {
            case BuySellIndicator::BUY:        return "BUYI";
            case BuySellIndicator::SELL:       return "SELL";
            case BuySellIndicator::SHORT_SELL: return "SSEX";
        }
        return "UNKNOWN";
    }

    static std::string order_type_str(OrderType ot) {
        switch (ot) {
            case OrderType::MARKET:     return "MARKET";
            case OrderType::LIMIT:      return "LIMIT";
            case OrderType::STOP:       return "STOP";
            case OrderType::STOP_LIMIT: return "STOP_LIMIT";
            case OrderType::PEGGED:     return "PEGGED";
            case OrderType::OTHER:      return "OTHER";
        }
        return "UNKNOWN";
    }

    static std::string time_in_force_str(TimeInForce tif) {
        switch (tif) {
            case TimeInForce::DAY:   return "DAY";
            case TimeInForce::GTC:   return "GTC";
            case TimeInForce::IOC:   return "IOC";
            case TimeInForce::FOK:   return "FOK";
            case TimeInForce::GTD:   return "GTD";
            case TimeInForce::OTHER: return "OTHER";
        }
        return "UNKNOWN";
    }

    static constexpr size_t MAX_FIELD_LENGTH = 4096;

    static std::string truncate_field(const std::string& s) {
        if (s.size() <= MAX_FIELD_LENGTH) return s;
        return s.substr(0, MAX_FIELD_LENGTH) + "...[TRUNCATED]";
    }

    static std::string j(const std::string& s) {
        const std::string safe = truncate_field(s);
        std::string out;
        out.reserve(safe.size());
        for (unsigned char c : safe) {
            if (c == '"')       out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20) { char buf[8];
                                  std::snprintf(buf,sizeof(buf),"\\u%04x",c);
                                  out += buf; }
            else                out += static_cast<char>(c);
        }
        return out;
    }

    static std::string csv_escape(const std::string& s) {
        const std::string safe = truncate_field(s);
        // If s contains comma, quote, or newline — wrap in quotes and double any quotes
        if (safe.find_first_of(",\"\n\r") == std::string::npos) return safe;
        std::string out = "\"";
        for (char c : safe) {
            if (c == '"') out += "\"\"";
            else          out += c;
        }
        out += "\"";
        return out;
    }

    static std::string double_str(double v, int significant_digits = 10) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.*g", significant_digits, v);
        return buf;
    }

    /// L21: Generate an 8-character hex suffix using platform CSPRNG.
    /// Platform-specific random sources:
    ///   macOS/FreeBSD/OpenBSD: arc4random_buf (kernel CSPRNG, never blocks)
    ///   Windows:               rand_s (RtlGenRandom-backed)
    ///   Linux:                 getrandom(2) (blocks until urandom is seeded)
    static std::string random_hex_suffix_() {
        uint8_t buf[4];
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)
        arc4random_buf(buf, sizeof(buf));   // kernel CSPRNG, never fails
#elif defined(_WIN32)
        // BCryptGenRandom is available on Vista+; for simplicity use rand_s
        for (auto& b : buf) {
            unsigned int val;
            rand_s(&val);                   // RtlGenRandom-backed CSPRNG
            b = static_cast<uint8_t>(val);
        }
#else
        // Linux: getrandom() with flags=0 blocks until urandom is seeded
        size_t written = 0;
        while (written < sizeof(buf)) {
            ssize_t ret = getrandom(buf + written, sizeof(buf) - written, 0);
            if (ret > 0) written += static_cast<size_t>(ret);
            else break;
        }
#endif
        static constexpr char hex[] = "0123456789abcdef";
        std::string result(8, '\0');
        for (size_t i = 0; i < 4; ++i) {
            result[2 * i]     = hex[buf[i] >> 4];
            result[2 * i + 1] = hex[buf[i] & 0x0F];
        }
        return result;
    }

    static std::string features_array(const std::vector<float>& feats) {
        std::string o = "[";
        for (size_t i = 0; i < feats.size(); ++i) {
            char buf[32]; std::snprintf(buf, sizeof(buf), "%.6g", feats[i]);
            o += buf;
            if (i + 1 < feats.size()) o += ",";
        }
        o += "]";
        return o;
    }
};

} // namespace signet::forge
