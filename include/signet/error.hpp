// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <unordered_set>
#include <variant>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace signet::forge {

// ---------------------------------------------------------------------------
// Error type — all signet operations return expected<T, Error>
// ---------------------------------------------------------------------------

/// Error codes returned by all Signet Forge operations.
///
/// Every function in the library that can fail returns an `expected<T>` whose
/// error payload carries one of these codes together with a human-readable
/// message string. Codes are grouped by subsystem so that callers can
/// pattern-match on categories (I/O, format corruption, unsupported features,
/// licensing) without inspecting the message text.
enum class ErrorCode {
    /// Operation completed successfully (no error).
    OK = 0,
    /// A file-system or stream I/O operation failed (open, read, write, rename).
    IO_ERROR,
    /// The file is not a valid Parquet file (e.g. missing or wrong magic bytes).
    INVALID_FILE,
    /// The Parquet footer (FileMetaData) is missing, truncated, or malformed.
    CORRUPT_FOOTER,
    /// A data page failed integrity checks (bad CRC, truncated, or exceeds size limits).
    CORRUPT_PAGE,
    /// Decoded data is corrupt or inconsistent (e.g. out-of-range dictionary index).
    CORRUPT_DATA,
    /// A caller-supplied argument is outside the valid range or violates a precondition.
    INVALID_ARGUMENT,
    /// The file uses an encoding not supported by this build (e.g. BYTE_STREAM_SPLIT on integers).
    UNSUPPORTED_ENCODING,
    /// The file uses a compression codec not linked into this build (ZSTD, LZ4, Gzip).
    UNSUPPORTED_COMPRESSION,
    /// The file contains a Parquet physical or logical type that is not implemented.
    UNSUPPORTED_TYPE,
    /// The requested column name or type does not match the file schema.
    SCHEMA_MISMATCH,
    /// An index, offset, or size value is outside the valid range.
    OUT_OF_RANGE,
    /// The Thrift Compact Protocol decoder encountered invalid or malicious input.
    THRIFT_DECODE_ERROR,
    /// An encryption or decryption operation failed (bad key, tampered ciphertext, PME error).
    ENCRYPTION_ERROR,
    /// The cryptographic audit hash chain is broken, indicating data tampering.
    HASH_CHAIN_BROKEN,
    /// The commercial license is missing, invalid, or the build is misconfigured.
    LICENSE_ERROR,
    /// An evaluation-tier usage limit has been exceeded (rows, users, nodes, or time).
    LICENSE_LIMIT_EXCEEDED,
    /// An unexpected internal error that does not fit any other category.
    INTERNAL_ERROR
};

/// Lightweight error value carrying an ErrorCode and a human-readable message.
///
/// An `Error` with `code == ErrorCode::OK` represents "no error". The `ok()`
/// helper and `operator bool()` let callers test for failure idiomatically:
///
/// @code
///   Error e = some_operation();
///   if (e)          { /* has error */ }
///   if (e.ok())     { /* no error  */ }
/// @endcode
///
/// @note `operator bool()` returns `true` when the Error **is** an error
///       (i.e. `!ok()`), following the convention "truthy means failure".
struct Error {
    /// The machine-readable error category.
    ErrorCode   code;
    /// A human-readable description of what went wrong (may be empty for OK).
    std::string message;

    /// Construct a default (OK) error with no message.
    Error() : code(ErrorCode::OK) {}
    /// Construct an error with the given code and descriptive message.
    /// @param c   The error code.
    /// @param msg A human-readable error description (moved into the struct).
    Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}

    /// Return true if this error represents success (code == OK).
    [[nodiscard]] bool ok() const { return code == ErrorCode::OK; }
    /// Return true if this error represents a failure (code != OK).
    /// @note Explicit to prevent accidental implicit conversions.
    [[nodiscard]] explicit operator bool() const { return !ok(); }
};

// ---------------------------------------------------------------------------
// expected<T, Error> — lightweight result type
// ---------------------------------------------------------------------------

/// A lightweight result type that holds either a success value of type `T` or an Error.
///
/// Modeled after C++23 `std::expected` but self-contained for C++20 compatibility.
/// Every Signet Forge function that can fail returns `expected<T>` (or
/// `expected<void>` for side-effect-only operations). Callers should always
/// check `has_value()` or `operator bool()` before accessing the payload:
///
/// @code
///   auto result = reader.read_column<double>("price");
///   if (!result) {
///       handle_error(result.error());
///       return;
///   }
///   const auto& values = *result;   // operator* dereference
/// @endcode
///
/// @tparam T The success value type. Must not be `Error`.
/// @see expected<void> for the void specialization.
/// @see Error for the error payload type.
template <typename T>
class expected {
public:
    /// Construct a successful result by copying the value (enabled only for copyable types).
    /// @param val The success value to store.
    expected(const T& val) requires std::is_copy_constructible_v<T>
        : storage_(val) {}
    /// Construct a successful result by moving the value.
    /// @param val The success value to move-store.
    expected(T&& val) : storage_(std::move(val)) {}

    /// Construct a failed result from a const Error reference.
    /// @param err The error to store.
    expected(const Error& err) : storage_(err) {}
    /// Construct a failed result by moving an Error.
    /// @param err The error to move-store.
    expected(Error&& err) : storage_(std::move(err)) {}

    /// Convenience constructor: build an Error in-place from a code and message.
    /// @param code The ErrorCode category.
    /// @param msg  A human-readable error description.
    expected(ErrorCode code, std::string msg)
        : storage_(Error{code, std::move(msg)}) {}

    /// Return true if the result holds a success value, false if it holds an Error.
    [[nodiscard]] bool has_value() const { return std::holds_alternative<T>(storage_); }
    /// Return true if the result holds a success value.
    /// @note Explicit to prevent accidental implicit conversions.
    [[nodiscard]] explicit operator bool() const { return has_value(); }

    /// Access the success value (const lvalue reference).
    /// @pre `has_value() == true`. Asserts in debug builds.
    /// @return Const reference to the stored value.
    [[nodiscard]] const T& value() const& {
        assert(has_value());
        return std::get<T>(storage_);
    }
    /// Access the success value (mutable lvalue reference).
    /// @pre `has_value() == true`. Asserts in debug builds.
    /// @return Mutable reference to the stored value.
    [[nodiscard]] T& value() & {
        assert(has_value());
        return std::get<T>(storage_);
    }
    /// Move-access the success value (rvalue reference).
    /// @pre `has_value() == true`. Asserts in debug builds.
    /// @return Rvalue reference to the stored value.
    [[nodiscard]] T&& value() && {
        assert(has_value());
        return std::get<T>(std::move(storage_));
    }

    /// Access the error payload.
    /// @pre `has_value() == false`. Asserts in debug builds.
    /// @return Const reference to the stored Error.
    [[nodiscard]] const Error& error() const {
        assert(!has_value());
        return std::get<Error>(storage_);
    }

    /// Dereference the success value (const lvalue).
    /// @pre `has_value() == true`.
    [[nodiscard]] const T& operator*() const& { return value(); }
    /// Dereference the success value (mutable lvalue).
    /// @pre `has_value() == true`.
    [[nodiscard]] T& operator*() & { return value(); }
    /// Dereference the success value (rvalue, moves out).
    /// @pre `has_value() == true`.
    [[nodiscard]] T&& operator*() && { return std::move(*this).value(); }
    /// Arrow operator for member access on the success value (const).
    /// @pre `has_value() == true`.
    [[nodiscard]] const T* operator->() const { return &value(); }
    /// Arrow operator for member access on the success value (mutable).
    /// @pre `has_value() == true`.
    [[nodiscard]] T* operator->() { return &value(); }

private:
    std::variant<T, Error> storage_;
};

/// Specialization of expected for void — used for operations that return success or error only.
///
/// Side-effect-only operations (e.g. `write_column()`, `close()`, `flush_row_group()`)
/// return `expected<void>`. There is no success payload; callers simply check
/// `has_value()` or `operator bool()` and, on failure, inspect `error()`.
///
/// @code
///   auto result = writer.close();
///   if (!result) {
///       log("close failed: " + result.error().message);
///   }
/// @endcode
///
/// @see expected<T> for the general (non-void) template.
template <>
class expected<void> {
public:
    /// Construct a successful (OK) result with no payload.
    expected() : err_() {}
    /// Construct a failed result from a const Error reference.
    /// @param err The error to store.
    expected(const Error& err) : err_(err) {}
    /// Construct a failed result by moving an Error.
    /// @param err The error to move-store.
    expected(Error&& err) : err_(std::move(err)) {}
    /// Convenience constructor: build an Error in-place from a code and message.
    /// @param code The ErrorCode category.
    /// @param msg  A human-readable error description.
    expected(ErrorCode code, std::string msg) : err_(Error{code, std::move(msg)}) {}

    /// Return true if the result represents success (no error).
    [[nodiscard]] bool has_value() const { return err_.ok(); }
    /// Return true if the result represents success.
    /// @note Explicit to prevent accidental implicit conversions.
    [[nodiscard]] explicit operator bool() const { return has_value(); }
    /// Access the error payload (valid for both success and failure; check `ok()` on the returned Error).
    /// @return Const reference to the stored Error.
    [[nodiscard]] const Error& error() const { return err_; }

private:
    Error err_;
};

/// Commercial licensing and evaluation-tier usage enforcement.
///
/// This namespace contains the license validation gate, usage metering, and
/// evaluation-mode policy enforcement used by the BSL 1.1 AI audit tier.
/// All symbols are header-only and thread-safe (guarded by `usage_state_mutex()`).
namespace commercial {

/// Environment variable name holding the commercial license key string.
inline constexpr const char* kLicenseEnvVar = "SIGNET_COMMERCIAL_LICENSE_KEY";
/// Environment variable name for the license tier override (e.g. "eval", "enterprise").
inline constexpr const char* kLicenseTierEnvVar = "SIGNET_COMMERCIAL_LICENSE_TIER";
/// Environment variable name for a custom usage-state file path.
inline constexpr const char* kUsageFileEnvVar = "SIGNET_COMMERCIAL_USAGE_FILE";
/// Environment variable name for the runtime user identity override.
inline constexpr const char* kRuntimeUserEnvVar = "SIGNET_COMMERCIAL_RUNTIME_USER";
/// Environment variable name for the runtime node/hostname identity override.
inline constexpr const char* kRuntimeNodeEnvVar = "SIGNET_COMMERCIAL_RUNTIME_NODE";
/// Number of row increments between automatic usage-state file persists.
inline constexpr uint64_t kUsagePersistIntervalRows = 10'000;

/// Default maximum rows per calendar month in evaluation mode (50 million).
/// Override at compile time with `-DSIGNET_EVAL_MAX_ROWS_MONTH_U64=<value>`.
#if defined(SIGNET_EVAL_MAX_ROWS_MONTH_U64)
inline constexpr uint64_t kDefaultEvalMaxRowsMonth =
    static_cast<uint64_t>(SIGNET_EVAL_MAX_ROWS_MONTH_U64);
#else
inline constexpr uint64_t kDefaultEvalMaxRowsMonth = 50'000'000ull;
#endif

/// Default maximum distinct users in evaluation mode (3).
/// Override at compile time with `-DSIGNET_EVAL_MAX_USERS_U32=<value>`.
#if defined(SIGNET_EVAL_MAX_USERS_U32)
inline constexpr uint32_t kDefaultEvalMaxUsers =
    static_cast<uint32_t>(SIGNET_EVAL_MAX_USERS_U32);
#else
inline constexpr uint32_t kDefaultEvalMaxUsers = 3u;
#endif

/// Default maximum distinct nodes in evaluation mode (1).
/// Override at compile time with `-DSIGNET_EVAL_MAX_NODES_U32=<value>`.
#if defined(SIGNET_EVAL_MAX_NODES_U32)
inline constexpr uint32_t kDefaultEvalMaxNodes =
    static_cast<uint32_t>(SIGNET_EVAL_MAX_NODES_U32);
#else
inline constexpr uint32_t kDefaultEvalMaxNodes = 1u;
#endif

/// Default maximum evaluation period in days (30).
/// Override at compile time with `-DSIGNET_EVAL_MAX_DAYS_U32=<value>`.
#if defined(SIGNET_EVAL_MAX_DAYS_U32)
inline constexpr uint32_t kDefaultEvalMaxDays =
    static_cast<uint32_t>(SIGNET_EVAL_MAX_DAYS_U32);
#else
inline constexpr uint32_t kDefaultEvalMaxDays = 30u;
#endif

/// Default first warning threshold as a percentage of monthly row limit (80%).
/// Override at compile time with `-DSIGNET_EVAL_WARN_PCT_1_U32=<value>`.
#if defined(SIGNET_EVAL_WARN_PCT_1_U32)
inline constexpr uint32_t kDefaultEvalWarnPct1 =
    static_cast<uint32_t>(SIGNET_EVAL_WARN_PCT_1_U32);
#else
inline constexpr uint32_t kDefaultEvalWarnPct1 = 80u;
#endif

/// Default second warning threshold as a percentage of monthly row limit (90%).
/// Override at compile time with `-DSIGNET_EVAL_WARN_PCT_2_U32=<value>`.
#if defined(SIGNET_EVAL_WARN_PCT_2_U32)
inline constexpr uint32_t kDefaultEvalWarnPct2 =
    static_cast<uint32_t>(SIGNET_EVAL_WARN_PCT_2_U32);
#else
inline constexpr uint32_t kDefaultEvalWarnPct2 = 90u;
#endif

/// Resolved license policy governing evaluation-mode limits.
///
/// Populated by `resolve_policy()` from environment variables and license-key
/// claims. When `evaluation_mode` is false, all limit fields are ignored and
/// the library operates without usage restrictions.
///
/// @see resolve_policy()
struct LicensePolicy {
    /// True if the license tier is an evaluation/trial tier.
    bool evaluation_mode{false};
    /// Maximum rows writable per calendar month (0 = unlimited).
    uint64_t max_rows_month{0};
    /// Maximum distinct runtime users allowed (0 = unlimited).
    uint32_t max_users{0};
    /// Maximum distinct runtime nodes/hosts allowed (0 = unlimited).
    uint32_t max_nodes{0};
    /// Explicit expiry as a Unix epoch day (0 = no explicit expiry; uses max_eval_days instead).
    int64_t explicit_expiry_day_utc{0};
    /// Maximum evaluation period in days from first use (0 = no time limit).
    uint32_t max_eval_days{0};
    /// First usage-percentage warning threshold (emits to stderr).
    uint32_t warn_pct_1{kDefaultEvalWarnPct1};
    /// Second usage-percentage warning threshold (emits to stderr).
    uint32_t warn_pct_2{kDefaultEvalWarnPct2};
};

/// Mutable usage-metering state for evaluation-tier enforcement.
///
/// Persisted to disk (key=value text format) so that usage counters survive
/// process restarts. Loaded lazily on first `enforce_eval_limits()` call and
/// written back atomically (rename) when counters cross persist thresholds.
///
/// @note All access must be guarded by `usage_state_mutex()`.
/// @see load_usage_state_from_file(), persist_usage_state_to_file()
struct UsageState {
    /// True once the state has been loaded from the persisted file.
    bool initialized{false};
    /// Year-month tag as YYYYMM integer (e.g. 202603). Resets counters on month rollover.
    int month_tag{0};
    /// Cumulative rows written in the current calendar month.
    uint64_t rows_this_month{0};
    /// Snapshot of rows_this_month at last successful file persist (avoids redundant writes).
    uint64_t last_persisted_rows_this_month{0};
    /// Unix epoch day when evaluation usage first began (0 = not yet started).
    int64_t eval_start_day_utc{0};
    /// True after the first usage-percentage warning has been printed to stderr.
    bool warn_pct_1_emitted{false};
    /// True after the second usage-percentage warning has been printed to stderr.
    bool warn_pct_2_emitted{false};
    /// Set of distinct runtime user identities observed this evaluation period.
    std::unordered_set<std::string> users;
    /// Set of distinct runtime node/host identities observed this evaluation period.
    std::unordered_set<std::string> nodes;
};

/// Compute a 64-bit FNV-1a hash over a byte buffer.
///
/// L15: FNV-1a is used here for license key validation only (not security).
/// It is NOT a cryptographic hash and should not be relied upon for tamper
/// resistance. A determined attacker can find collisions. For production
/// license enforcement, consider HMAC-SHA256 or a public-key signature.
[[nodiscard]] inline uint64_t fnv1a64(const char* data, size_t size) noexcept {
    constexpr uint64_t kOffset = 14695981039346656037ull;
    constexpr uint64_t kPrime  = 1099511628211ull;

    uint64_t hash = kOffset;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint8_t>(data[i]);
        hash *= kPrime;
    }
    return hash;
}

/// Return a copy of the string with leading and trailing whitespace removed.
[[nodiscard]] inline std::string trim_copy(const std::string& in) {
    size_t start = 0;
    while (start < in.size() && std::isspace(static_cast<unsigned char>(in[start]))) {
        ++start;
    }

    size_t end = in.size();
    while (end > start && std::isspace(static_cast<unsigned char>(in[end - 1]))) {
        --end;
    }

    return in.substr(start, end - start);
}

/// Return a copy of the string with all ASCII uppercase letters converted to lowercase.
[[nodiscard]] inline std::string to_lower_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

/// Trim, truncate to 128 chars, and replace non-alphanumeric characters; return fallback if empty.
[[nodiscard]] inline std::string sanitize_identity(std::string value,
                                                   const char* fallback) {
    value = trim_copy(value);
    if (value.empty()) {
        return fallback;
    }

    if (value.size() > 128) {
        value.resize(128);
    }

    for (char& c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '@') {
            continue;
        }
        c = '_';
    }
    return value;
}

/// Parse a semicolon-delimited "key=value;key=value" string into a map (keys lowercased, trimmed).
[[nodiscard]] inline std::unordered_map<std::string, std::string>
parse_claims(const std::string& text) {
    std::unordered_map<std::string, std::string> out;
    std::stringstream ss(text);
    std::string token;

    while (std::getline(ss, token, ';')) {
        auto pos = token.find('=');
        if (pos == std::string::npos || pos == 0 || pos + 1 >= token.size()) {
            continue;
        }
        std::string key = to_lower_ascii(trim_copy(token.substr(0, pos)));
        std::string val = trim_copy(token.substr(pos + 1));
        if (!key.empty() && !val.empty()) {
            out[key] = val;
        }
    }

    return out;
}

/// Parse a decimal string into a uint64_t; return false on empty, non-digit, or overflow.
[[nodiscard]] inline bool parse_u64(const std::string& text, uint64_t& out) {
    if (text.empty()) return false;

    uint64_t value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        const uint64_t digit = static_cast<uint64_t>(ch - '0');
        if (value > ((std::numeric_limits<uint64_t>::max)() - digit) / 10ull) {
            return false;
        }
        value = (value * 10ull) + digit;
    }

    out = value;
    return true;
}

/// Parse a decimal string into a uint32_t; return false on empty, non-digit, or out-of-range.
[[nodiscard]] inline bool parse_u32(const std::string& text, uint32_t& out) {
    uint64_t value = 0;
    if (!parse_u64(text, value) || value > (std::numeric_limits<uint32_t>::max)()) {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

/// Convert a civil date (year, month, day) to a Unix epoch day count (Howard Hinnant algorithm).
[[nodiscard]] inline constexpr int64_t days_from_civil(int y, unsigned m,
                                                       unsigned d) noexcept {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = (m > 2u) ? (m - 3u) : (m + 9u);
    const unsigned doy = (153u * mp + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

/// Return true if the given year is a leap year (Gregorian calendar).
[[nodiscard]] inline bool is_leap_year(int y) noexcept {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/// Parse an ISO 8601 date string ("YYYY-MM-DD") into a Unix epoch day; return false on invalid input.
[[nodiscard]] inline bool parse_iso_date_to_epoch_day(const std::string& iso,
                                                       int64_t& out_day) {
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') return false;

    auto parse_two = [&](size_t idx, int& out) -> bool {
        if (!std::isdigit(static_cast<unsigned char>(iso[idx])) ||
            !std::isdigit(static_cast<unsigned char>(iso[idx + 1]))) {
            return false;
        }
        out = (iso[idx] - '0') * 10 + (iso[idx + 1] - '0');
        return true;
    };

    int year = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(iso[i]))) return false;
        year = year * 10 + (iso[i] - '0');
    }

    int month = 0;
    int day = 0;
    if (!parse_two(5, month) || !parse_two(8, day)) return false;
    if (month < 1 || month > 12) return false;

    static constexpr int kMonthDays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int max_day = kMonthDays[month - 1];
    if (month == 2 && is_leap_year(year)) max_day = 29;
    if (day < 1 || day > max_day) return false;

    out_day = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    return true;
}

/// Return the current UTC date as a Unix epoch day count.
[[nodiscard]] inline int64_t current_epoch_day_utc() {
    using namespace std::chrono;
    const auto now_days = floor<days>(system_clock::now());
    return now_days.time_since_epoch().count();
}

/// Return the current UTC year-month as a YYYYMM integer tag (e.g. 202603).
[[nodiscard]] inline int current_month_tag_utc() {
    using namespace std::chrono;
    const auto now_days = floor<days>(system_clock::now());
    const year_month_day ymd{now_days};
    return static_cast<int>(static_cast<int>(ymd.year()) * 100 +
                            static_cast<unsigned>(ymd.month()));
}

/// Return the default filesystem path for the usage-state persistence file.
///
/// M23: Uses XDG_STATE_HOME or HOME-based path instead of /tmp to avoid
/// world-readable state files and symlink attacks in shared /tmp.
[[nodiscard]] inline std::string default_usage_state_path() {
    // Prefer XDG_STATE_HOME (e.g. ~/.local/state)
    const char* xdg = std::getenv("XDG_STATE_HOME");
    if (xdg && xdg[0]) {
        return std::string(xdg) + "/signet-forge/usage_state";
    }
    // Fall back to HOME-based path
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home || !home[0]) home = std::getenv("USERPROFILE");
#endif
    if (home && home[0]) {
        return std::string(home) + "/.local/state/signet-forge/usage_state";
    }
    // Last resort: /tmp (less secure but functional)
#if defined(SIGNET_COMMERCIAL_LICENSE_HASH_U64)
    char buf[96];
    std::snprintf(buf, sizeof(buf),
                  "/tmp/signet_commercial_usage_%016llx.state",
                  static_cast<unsigned long long>(
                      static_cast<uint64_t>(SIGNET_COMMERCIAL_LICENSE_HASH_U64)));
    return std::string(buf);
#else
    return "/tmp/signet_commercial_usage.state";
#endif
}

/// Return the usage-state file path (environment override or default).
///
/// H15: The environment variable path is sanitized to reject path traversal
/// sequences ("..") which could be used to overwrite arbitrary files.
[[nodiscard]] inline std::string usage_state_path() {
    const char* env = std::getenv(kUsageFileEnvVar);
    if (env != nullptr && env[0] != '\0') {
        std::string raw(env);

        // CWE-22: Reject path traversal sequences.
        if (raw.find("..") != std::string::npos)
            return default_usage_state_path();

        // CWE-22: Reject embedded null bytes (truncation attack).
        if (raw.find('\0') != std::string::npos)
            return default_usage_state_path();

#ifndef _WIN32
        // CWE-22: Require absolute path — relative paths are ambiguous and
        // depend on the process cwd, which the env setter may not control.
        if (raw.empty() || raw[0] != '/')
            return default_usage_state_path();

        // CWE-426: Canonicalize parent directory via realpath() to resolve
        // symlinks.  The parent MUST exist and be a real directory.
        auto fs_path = std::filesystem::path(raw);
        auto parent  = fs_path.parent_path();
        if (parent.empty())
            return default_usage_state_path();

        char resolved[PATH_MAX] = {};
        if (!::realpath(parent.string().c_str(), resolved))
            return default_usage_state_path();  // parent doesn't exist

        // Verify the resolved parent is actually a directory (not a file or device).
        std::error_code ec;
        if (!std::filesystem::is_directory(resolved, ec))
            return default_usage_state_path();

        // Build sanitised path from canonicalized parent + original filename.
        std::string sanitised = std::string(resolved) + "/" +
                                fs_path.filename().string();

        // Final guard: reject if canonicalization re-introduced ".."
        if (sanitised.find("..") != std::string::npos)
            return default_usage_state_path();

        return sanitised;
#else
        // Windows: basic validation only (no realpath).
        return raw;
#endif
    }
    return default_usage_state_path();
}

/// Detect the current runtime user identity from environment variables, sanitized.
[[nodiscard]] inline std::string detect_runtime_user() {
    const char* user = std::getenv(kRuntimeUserEnvVar);
    if (user == nullptr || user[0] == '\0') {
        user = std::getenv("USER");
    }
    if (user == nullptr || user[0] == '\0') {
        user = std::getenv("LOGNAME");
    }
    return sanitize_identity(user ? std::string(user) : std::string(), "unknown-user");
}

/// Detect the current runtime node/hostname identity from environment variables, sanitized.
[[nodiscard]] inline std::string detect_runtime_node() {
    const char* node = std::getenv(kRuntimeNodeEnvVar);
    if (node == nullptr || node[0] == '\0') {
        node = std::getenv("HOSTNAME");
    }
    if (node == nullptr || node[0] == '\0') {
        node = std::getenv("COMPUTERNAME");
    }
    return sanitize_identity(node ? std::string(node) : std::string(), "unknown-node");
}

/// Split a comma-separated string into sanitized tokens, discarding empty entries.
[[nodiscard]] inline std::vector<std::string> split_csv(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string token;

    while (std::getline(ss, token, ',')) {
        token = sanitize_identity(token, "");
        if (!token.empty()) {
            out.push_back(token);
        }
    }

    return out;
}

/// Join a set of strings into a sorted, comma-separated string.
[[nodiscard]] inline std::string join_set_csv(const std::unordered_set<std::string>& values) {
    std::vector<std::string> ordered;
    ordered.reserve(values.size());
    for (const auto& v : values) {
        ordered.push_back(v);
    }
    std::sort(ordered.begin(), ordered.end());

    std::string out;
    for (size_t i = 0; i < ordered.size(); ++i) {
        if (i != 0) out.push_back(',');
        out += ordered[i];
    }
    return out;
}

/// Load usage-state fields from a key=value text file into the given UsageState.
inline void load_usage_state_from_file(const std::string& path, UsageState& st) {
    std::ifstream in(path);
    if (!in.good()) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim_copy(line.substr(0, eq));
        const std::string val = trim_copy(line.substr(eq + 1));

        uint64_t u64 = 0;
        if (key == "month_tag" && parse_u64(val, u64)) {
            st.month_tag = static_cast<int>(u64);
        } else if (key == "rows_this_month" && parse_u64(val, u64)) {
            st.rows_this_month = u64;
        } else if (key == "eval_start_day_utc" && parse_u64(val, u64)) {
            st.eval_start_day_utc = static_cast<int64_t>(u64);
        } else if (key == "warn_pct_1_emitted") {
            st.warn_pct_1_emitted = (val == "1");
        } else if (key == "warn_pct_2_emitted") {
            st.warn_pct_2_emitted = (val == "1");
        } else if (key == "users") {
            for (auto& token : split_csv(val)) {
                st.users.insert(token);
            }
        } else if (key == "nodes") {
            for (auto& token : split_csv(val)) {
                st.nodes.insert(token);
            }
        }
    }

    st.last_persisted_rows_this_month = st.rows_this_month;
}

/// Atomically persist the UsageState to a file (write-to-tmp + rename); return false on I/O failure.
///
/// M24: Refuses to write through symlinks to prevent symlink-based file overwrites (CWE-61).
[[nodiscard]] inline bool persist_usage_state_to_file(const std::string& path,
                                                      UsageState& st) {
    const std::string tmp_path = path + ".tmp";

#ifndef _WIN32
    // CWE-59: Improper Link Resolution Before File Access
    // Refuse to write through symlinks — lstat() detects symlinks without following them.
    struct stat st_link;
    if (::lstat(tmp_path.c_str(), &st_link) == 0 && S_ISLNK(st_link.st_mode)) {
        return false;
    }
    if (::lstat(path.c_str(), &st_link) == 0 && S_ISLNK(st_link.st_mode)) {
        return false;
    }
#endif

    std::ofstream out;
#ifndef _WIN32
    // CWE-732: Create with explicit 0600 permissions to prevent world-readable state files.
    int sfd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (sfd < 0) return false;
    // Wrap the fd in a stdio FILE*, then attach to ofstream via the path
    // (fd ownership: we close it after ofstream opens the same path).
    ::close(sfd);
    out.open(tmp_path, std::ios::out | std::ios::trunc);
#else
    out.open(tmp_path, std::ios::out | std::ios::trunc);
#endif
    if (!out.is_open()) return false;

    out << "month_tag=" << st.month_tag << '\n';
    out << "rows_this_month=" << st.rows_this_month << '\n';
    out << "eval_start_day_utc=" << st.eval_start_day_utc << '\n';
    out << "warn_pct_1_emitted=" << (st.warn_pct_1_emitted ? 1 : 0) << '\n';
    out << "warn_pct_2_emitted=" << (st.warn_pct_2_emitted ? 1 : 0) << '\n';
    out << "users=" << join_set_csv(st.users) << '\n';
    out << "nodes=" << join_set_csv(st.nodes) << '\n';

    out.close();
    if (!out) {
        std::remove(tmp_path.c_str());
        return false;
    }

    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        return false;
    }

    st.last_persisted_rows_this_month = st.rows_this_month;
    return true;
}

/// Resolve the effective LicensePolicy from environment variables and license-key claims.
///
/// Reads `SIGNET_COMMERCIAL_LICENSE_KEY` for inline claims (semicolon-delimited
/// key=value pairs such as `tier=eval;max_rows_month=100000;expires_at=2026-12-31`)
/// and `SIGNET_COMMERCIAL_LICENSE_TIER` as a fallback tier source. When the tier
/// is not an evaluation tier, an empty (unrestricted) policy is returned.
///
/// Recognized claim keys: `tier`, `max_rows_month` / `rows_month`, `max_users`,
/// `max_nodes`, `max_days`, `warn_pct_1`, `warn_pct_2`, `expires_at` (ISO 8601).
///
/// @return A fully resolved LicensePolicy with defaults applied.
/// @see LicensePolicy
[[nodiscard]] inline LicensePolicy resolve_policy() {
    LicensePolicy policy;

    const char* license_key = std::getenv(kLicenseEnvVar);
    const auto claims = parse_claims(license_key ? std::string(license_key) : std::string());

    std::string tier;
    if (auto it = claims.find("tier"); it != claims.end()) {
        tier = to_lower_ascii(it->second);
    } else {
        const char* env_tier = std::getenv(kLicenseTierEnvVar);
        if (env_tier != nullptr && env_tier[0] != '\0') {
            tier = to_lower_ascii(env_tier);
        }
    }

    policy.evaluation_mode =
        (tier == "eval" || tier == "evaluation" || tier == "trial" ||
         tier == "testing" || tier == "test");

    if (!policy.evaluation_mode) {
        return policy;
    }

    policy.max_rows_month = kDefaultEvalMaxRowsMonth;
    policy.max_users = kDefaultEvalMaxUsers;
    policy.max_nodes = kDefaultEvalMaxNodes;
    policy.max_eval_days = kDefaultEvalMaxDays;
    policy.warn_pct_1 = kDefaultEvalWarnPct1;
    policy.warn_pct_2 = kDefaultEvalWarnPct2;

    auto set_u64 = [&](const char* key, uint64_t& out) {
        if (auto it = claims.find(key); it != claims.end()) {
            uint64_t parsed = 0;
            if (parse_u64(it->second, parsed)) out = parsed;
        }
    };

    auto set_u32 = [&](const char* key, uint32_t& out) {
        if (auto it = claims.find(key); it != claims.end()) {
            uint32_t parsed = 0;
            if (parse_u32(it->second, parsed)) out = parsed;
        }
    };

    set_u64("max_rows_month", policy.max_rows_month);
    set_u64("rows_month", policy.max_rows_month);
    set_u32("max_users", policy.max_users);
    set_u32("max_nodes", policy.max_nodes);
    set_u32("max_days", policy.max_eval_days);
    set_u32("warn_pct_1", policy.warn_pct_1);
    set_u32("warn_pct_2", policy.warn_pct_2);

    if (auto it = claims.find("expires_at"); it != claims.end()) {
        int64_t day = 0;
        if (parse_iso_date_to_epoch_day(it->second, day)) {
            policy.explicit_expiry_day_utc = day;
        }
    }

    if (policy.warn_pct_1 > 100u) policy.warn_pct_1 = 100u;
    if (policy.warn_pct_2 > 100u) policy.warn_pct_2 = 100u;
    if (policy.warn_pct_1 > policy.warn_pct_2) {
        std::swap(policy.warn_pct_1, policy.warn_pct_2);
    }

    return policy;
}

/// Validate the commercial license key at most once (result cached in a static).
///
/// Checks three conditions in order:
/// 1. `SIGNET_ENABLE_COMMERCIAL` must be defined and non-zero (otherwise returns LICENSE_ERROR).
/// 2. If `SIGNET_REQUIRE_COMMERCIAL_LICENSE` is set, `SIGNET_COMMERCIAL_LICENSE_HASH_U64` must
///    also be defined at compile time.
/// 3. The runtime environment variable `SIGNET_COMMERCIAL_LICENSE_KEY` must be present
///    and its FNV-1a hash must match the compile-time expected hash.
///
/// @return `expected<void>{}` on success, or an Error with LICENSE_ERROR on failure.
[[nodiscard]] inline expected<void> validate_license_once() {
#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
    return Error{ErrorCode::LICENSE_ERROR,
                 "commercial feature disabled in this Apache build; "
                 "rebuild with -DSIGNET_ENABLE_COMMERCIAL=ON under BSL 1.1"};
#else
#  if defined(SIGNET_REQUIRE_COMMERCIAL_LICENSE) && SIGNET_REQUIRE_COMMERCIAL_LICENSE
#    if !defined(SIGNET_COMMERCIAL_LICENSE_HASH_U64)
    return Error{ErrorCode::LICENSE_ERROR,
                 "commercial tier misconfigured: missing SIGNET_COMMERCIAL_LICENSE_HASH_U64"};
#    else
    const char* license_key = std::getenv(kLicenseEnvVar);
    if (license_key == nullptr || license_key[0] == '\0') {
        return Error{ErrorCode::LICENSE_ERROR,
                     std::string("missing runtime license key: set ") + kLicenseEnvVar};
    }

    constexpr uint64_t kExpectedHash =
        static_cast<uint64_t>(SIGNET_COMMERCIAL_LICENSE_HASH_U64);
    const uint64_t actual_hash = fnv1a64(license_key, std::strlen(license_key));

    if (actual_hash != kExpectedHash) {
        return Error{ErrorCode::LICENSE_ERROR,
                     std::string("invalid runtime license key in ") + kLicenseEnvVar};
    }
#    endif
#  endif

    return expected<void>{};
#endif
}

/// Return a reference to the global mutex guarding all UsageState access.
inline std::mutex& usage_state_mutex() {
    static std::mutex m;
    return m;
}

/// Return a reference to the global singleton UsageState instance.
inline UsageState& usage_state() {
    static UsageState s;
    return s;
}

/// Lazily load usage state from the persisted file if not yet initialized (caller must hold mutex).
inline void ensure_usage_state_loaded_locked(UsageState& st) {
    if (st.initialized) return;

    load_usage_state_from_file(usage_state_path(), st);
    if (st.month_tag == 0) {
        st.month_tag = current_month_tag_utc();
    }
    st.initialized = true;
}

/// Enforce all evaluation-tier limits (rows, users, nodes, time) for a single operation.
///
/// Called internally by `require_feature()`. Resolves the LicensePolicy once
/// (cached in a function-local static), then under `usage_state_mutex()`:
/// - Resets counters on calendar month rollover.
/// - Records the evaluation start day on first call.
/// - Checks expiry (explicit or start + max_eval_days).
/// - Tracks distinct user and node identities.
/// - Increments and checks the monthly row counter (with overflow guard).
/// - Emits stderr warnings at configurable percentage thresholds.
/// - Persists usage state to disk when thresholds are crossed.
///
/// @param feature_name  A human-readable name included in error messages (e.g. "DecisionLog").
/// @param rows_increment  Number of rows to add to the monthly counter (0 for a check-only call).
/// @return `expected<void>{}` on success, or an Error with LICENSE_LIMIT_EXCEEDED on breach.
/// @see resolve_policy(), require_feature()
[[nodiscard]] inline expected<void> enforce_eval_limits(const char* feature_name,
                                                        uint64_t rows_increment) {
    static const LicensePolicy policy = resolve_policy();
    if (!policy.evaluation_mode) {
        return expected<void>{};
    }

    std::lock_guard<std::mutex> lock(usage_state_mutex());
    UsageState& st = usage_state();
    ensure_usage_state_loaded_locked(st);

    bool persist_required = false;

    const int current_month = current_month_tag_utc();
    if (st.month_tag != current_month) {
        st.month_tag = current_month;
        st.rows_this_month = 0;
        st.last_persisted_rows_this_month = 0;
        st.warn_pct_1_emitted = false;
        st.warn_pct_2_emitted = false;
        persist_required = true;
    }

    const int64_t today = current_epoch_day_utc();

    if (policy.max_eval_days > 0 && st.eval_start_day_utc == 0) {
        st.eval_start_day_utc = today;
        persist_required = true;
    }

    int64_t expiry_day = policy.explicit_expiry_day_utc;
    if (expiry_day == 0 && policy.max_eval_days > 0 && st.eval_start_day_utc > 0) {
        expiry_day = st.eval_start_day_utc + static_cast<int64_t>(policy.max_eval_days) - 1;
    }

    if (expiry_day > 0 && today > expiry_day) {
        return Error{ErrorCode::LICENSE_LIMIT_EXCEEDED,
                     std::string(feature_name) +
                     ": evaluation period expired; commercial license required"};
    }

    const std::string user_id = detect_runtime_user();
    if (st.users.insert(user_id).second) {
        persist_required = true;
    }
    if (policy.max_users > 0 && st.users.size() > policy.max_users) {
        return Error{ErrorCode::LICENSE_LIMIT_EXCEEDED,
                     std::string(feature_name) +
                     ": evaluation user threshold exceeded; commercial license required"};
    }

    const std::string node_id = detect_runtime_node();
    if (st.nodes.insert(node_id).second) {
        persist_required = true;
    }
    if (policy.max_nodes > 0 && st.nodes.size() > policy.max_nodes) {
        return Error{ErrorCode::LICENSE_LIMIT_EXCEEDED,
                     std::string(feature_name) +
                     ": evaluation node threshold exceeded; commercial license required"};
    }

    if (policy.max_rows_month > 0) {
        if (rows_increment > 0) {
            if (st.rows_this_month >
                (std::numeric_limits<uint64_t>::max)() - rows_increment) {
                return Error{ErrorCode::LICENSE_LIMIT_EXCEEDED,
                             std::string(feature_name) +
                             ": usage counter overflow; commercial license required"};
            }

            const uint64_t projected = st.rows_this_month + rows_increment;
            if (projected > policy.max_rows_month) {
                return Error{ErrorCode::LICENSE_LIMIT_EXCEEDED,
                             std::string(feature_name) +
                             ": evaluation monthly row threshold exceeded; "
                             "commercial license required"};
            }

            st.rows_this_month = projected;
            if ((st.rows_this_month - st.last_persisted_rows_this_month)
                >= kUsagePersistIntervalRows ||
                st.rows_this_month == policy.max_rows_month) {
                persist_required = true;
            }
        } else if (st.rows_this_month >= policy.max_rows_month) {
            return Error{ErrorCode::LICENSE_LIMIT_EXCEEDED,
                         std::string(feature_name) +
                         ": evaluation monthly row threshold reached; "
                         "commercial license required"};
        }

        const long double pct =
            (static_cast<long double>(st.rows_this_month) * 100.0L) /
            static_cast<long double>(policy.max_rows_month);

        if (policy.warn_pct_1 > 0 && !st.warn_pct_1_emitted &&
            pct >= static_cast<long double>(policy.warn_pct_1)) {
            std::fprintf(stderr,
                         "signet commercial eval warning: %.2Lf%% of monthly row "
                         "limit used (%llu/%llu)\n",
                         pct,
                         static_cast<unsigned long long>(st.rows_this_month),
                         static_cast<unsigned long long>(policy.max_rows_month));
            st.warn_pct_1_emitted = true;
            persist_required = true;
        }

        if (policy.warn_pct_2 > 0 && !st.warn_pct_2_emitted &&
            pct >= static_cast<long double>(policy.warn_pct_2)) {
            std::fprintf(stderr,
                         "signet commercial eval warning: %.2Lf%% of monthly row "
                         "limit used (%llu/%llu)\n",
                         pct,
                         static_cast<unsigned long long>(st.rows_this_month),
                         static_cast<unsigned long long>(policy.max_rows_month));
            st.warn_pct_2_emitted = true;
            persist_required = true;
        }
    }

    if (persist_required && !persist_usage_state_to_file(usage_state_path(), st)) {
        return Error{ErrorCode::LICENSE_ERROR,
                     std::string(feature_name) +
                     ": unable to persist evaluation usage state"};
    }

    return expected<void>{};
}

/// Gate access to a commercial feature: validate the license and enforce evaluation limits.
///
/// This is the primary entry point for BSL 1.1 feature gating. On first call
/// it validates the license key (cached), then delegates to `enforce_eval_limits()`
/// for per-operation metering. Callers in the AI audit tier invoke this at the
/// start of every write or log operation.
///
/// @param feature_name  A human-readable label for error messages (e.g. "InferenceLog").
/// @param usage_rows    Number of rows consumed by this operation (default 0 = check-only).
/// @return `expected<void>{}` on success, or an Error on license or limit failure.
/// @see validate_license_once(), enforce_eval_limits(), record_usage_rows()
[[nodiscard]] inline expected<void> require_feature(const char* feature_name,
                                                    uint64_t usage_rows = 0) {
    static const expected<void> gate = validate_license_once();
    if (!gate) {
        return Error{gate.error().code,
                     std::string(feature_name) + ": " + gate.error().message};
    }

    auto policy_gate = enforce_eval_limits(feature_name, usage_rows);
    if (!policy_gate) {
        return policy_gate.error();
    }

    return expected<void>{};
}

/// Record row-level usage for a commercial feature (convenience wrapper around require_feature).
///
/// Equivalent to `require_feature(feature_name, rows)`. Provided as a semantic
/// alias so that call sites that only need to meter rows (without a separate
/// license-check step) can express intent clearly.
///
/// @param feature_name  A human-readable label for error messages.
/// @param rows          Number of rows to record against the monthly quota.
/// @return `expected<void>{}` on success, or an Error on license or limit failure.
/// @see require_feature()
[[nodiscard]] inline expected<void> record_usage_rows(const char* feature_name,
                                                      uint64_t rows) {
    return require_feature(feature_name, rows);
}

} // namespace commercial

} // namespace signet::forge
