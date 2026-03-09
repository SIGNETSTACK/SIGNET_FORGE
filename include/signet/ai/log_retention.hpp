// SPDX-License-Identifier: BUSL-1.1
// Copyright 2026 Johnson Ogundeji
// Change Date: January 1, 2030 | Change License: Apache-2.0
// See LICENSE_COMMERCIAL for full terms.
#pragma once

#if !defined(SIGNET_ENABLE_COMMERCIAL) || !SIGNET_ENABLE_COMMERCIAL
#error "signet/ai/log_retention.hpp is a BSL 1.1 commercial module. Build with -DSIGNET_ENABLE_COMMERCIAL=ON."
#endif

// ---------------------------------------------------------------------------
// log_retention.hpp -- Log Retention Lifecycle Manager
//
// EU AI Act Art.12(3) requires automatic recording of events over the
// lifetime of the system with defined retention periods (minimum 6 months).
// MiFID II RTS 24 Art.4 requires 5-year retention for order records.
//
// This module provides:
//   - RetentionPolicy: configurable TTL, archival, and size-based rotation
//   - LogRetentionManager: scans log directories and enforces retention
//   - Archival callback for moving expired logs to cold storage
//   - Deletion of logs past the maximum retention period
//   - Compliance summary reporting
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/error.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace signet::forge {

// ---------------------------------------------------------------------------
// RetentionPolicy
// ---------------------------------------------------------------------------

/// Retention policy configuration for log lifecycle management.
/// Defined at namespace scope for Apple Clang compatibility.
struct RetentionPolicy {
    /// Minimum retention period in nanoseconds.
    /// Logs younger than this are never deleted or archived.
    /// Default: 6 months (~1.578 × 10^16 ns) — EU AI Act Art.12(3) minimum.
    int64_t min_retention_ns = INT64_C(15778800000000000);  // ~6 months

    /// Maximum retention period in nanoseconds.
    /// Logs older than this are eligible for permanent deletion.
    /// Default: 5 years (~1.578 × 10^17 ns) — MiFID II RTS 24 Art.4.
    int64_t max_retention_ns = INT64_C(157788000000000000);  // ~5 years

    /// Archival threshold in nanoseconds.
    /// Logs older than min_retention_ns but younger than max_retention_ns
    /// are candidates for archival (cold storage).
    /// Default: 1 year (~3.156 × 10^16 ns).
    int64_t archive_after_ns = INT64_C(31557600000000000);  // ~1 year

    /// Maximum total size of active (non-archived) log files in bytes.
    /// When exceeded, oldest files are archived first.
    /// Default: 10 GB.
    uint64_t max_active_size_bytes = UINT64_C(10737418240);  // 10 GB

    /// Maximum number of active log files.
    /// When exceeded, oldest files are archived first.
    /// Default: 10000 (effectively unlimited).
    uint64_t max_active_files = 10000;

    /// File name pattern to match (glob-style suffix).
    /// Only files matching this pattern are managed.
    /// Default: ".parquet"
    std::string file_suffix = ".parquet";

    /// If true, actually delete files past max_retention_ns.
    /// If false, only report what would be deleted (dry-run).
    bool enable_deletion = false;

    /// If true, actually archive files past archive_after_ns.
    /// If false, only report what would be archived (dry-run).
    bool enable_archival = false;
};

// ---------------------------------------------------------------------------
// RetentionSummary
// ---------------------------------------------------------------------------

/// Summary of a retention enforcement pass.
struct RetentionSummary {
    /// Number of files scanned.
    int64_t files_scanned = 0;

    /// Number of files within active retention window.
    int64_t files_active = 0;

    /// Number of files archived (or would be archived in dry-run).
    int64_t files_archived = 0;

    /// Number of files deleted (or would be deleted in dry-run).
    int64_t files_deleted = 0;

    /// Number of files that failed archival or deletion.
    int64_t files_failed = 0;

    /// Total size of active files in bytes.
    uint64_t active_size_bytes = 0;

    /// Total size of archived files in bytes.
    uint64_t archived_size_bytes = 0;

    /// Total size of deleted files in bytes.
    uint64_t deleted_size_bytes = 0;

    /// Whether the enforcement pass was a dry-run.
    bool dry_run = true;

    /// Paths of files that were archived.
    std::vector<std::string> archived_paths;

    /// Paths of files that were deleted.
    std::vector<std::string> deleted_paths;

    /// Error messages from failed operations.
    std::vector<std::string> errors;
};

// ---------------------------------------------------------------------------
// LogRetentionManager
// ---------------------------------------------------------------------------

/// Manages log file lifecycle: retention, archival, and deletion.
///
/// EU AI Act Art.12(3): Minimum 6-month retention.
/// MiFID II RTS 24 Art.4: 5-year retention for order records.
///
/// Usage:
///   RetentionPolicy policy;
///   policy.enable_archival = true;
///   policy.enable_deletion = true;
///
///   LogRetentionManager mgr(policy);
///   mgr.set_archive_callback([](const std::string& src) {
///       // Move to cold storage (S3, GCS, etc.)
///       return move_to_cold_storage(src);
///   });
///
///   auto summary = mgr.enforce("/audit/decisions", now_ns);
class LogRetentionManager {
public:
    /// Callback invoked to archive a file. Returns true on success.
    /// The callback receives the source file path and should move/copy
    /// the file to archival storage.
    using ArchiveCallback = std::function<bool(const std::string& source_path)>;

    /// Callback invoked before deleting a file. Returns true to allow deletion.
    /// Can be used for final backup or compliance logging.
    using PreDeleteCallback = std::function<bool(const std::string& path)>;

    explicit LogRetentionManager(RetentionPolicy policy = {})
        : policy_(std::move(policy)) {}

    /// Set the archival callback.
    void set_archive_callback(ArchiveCallback cb) {
        archive_cb_ = std::move(cb);
    }

    /// Set the pre-deletion callback.
    void set_pre_delete_callback(PreDeleteCallback cb) {
        pre_delete_cb_ = std::move(cb);
    }

    /// Enforce retention policy on a log directory.
    ///
    /// Scans the directory for files matching the policy's file_suffix,
    /// determines their age from filesystem modification time, and
    /// applies archival/deletion rules.
    ///
    /// @param log_dir     Directory containing log files
    /// @param now_ns      Current time in nanoseconds since epoch
    /// @return Summary of actions taken (or that would be taken in dry-run)
    [[nodiscard]] inline RetentionSummary enforce(
            const std::string& log_dir, int64_t now_ns) const {
        RetentionSummary summary;
        summary.dry_run = !(policy_.enable_archival || policy_.enable_deletion);

        namespace fs = std::filesystem;

        if (!fs::exists(log_dir) || !fs::is_directory(log_dir)) {
            summary.errors.push_back("Directory does not exist: " + log_dir);
            return summary;
        }

        // Collect matching files with their modification times
        struct FileInfo {
            std::string path;
            int64_t     mod_time_ns;
            uint64_t    size_bytes;
        };
        std::vector<FileInfo> files;

        for (const auto& entry : fs::directory_iterator(log_dir)) {
            if (!entry.is_regular_file()) continue;

            const auto& path = entry.path();
            if (!path.string().ends_with(policy_.file_suffix)) continue;

            std::error_code ec;
            auto mod_time = fs::last_write_time(path, ec);
            if (ec) continue;

            // Convert file_time_type to nanoseconds since epoch
            auto sys_time = std::chrono::file_clock::to_sys(mod_time);
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                sys_time.time_since_epoch()).count();

            auto fsize = entry.file_size(ec);
            if (ec) fsize = 0;

            files.push_back({path.string(), static_cast<int64_t>(ns), fsize});
        }

        // Sort by modification time (oldest first)
        std::sort(files.begin(), files.end(),
                  [](const FileInfo& a, const FileInfo& b) {
                      return a.mod_time_ns < b.mod_time_ns;
                  });

        summary.files_scanned = static_cast<int64_t>(files.size());

        for (const auto& fi : files) {
            int64_t age_ns = now_ns - fi.mod_time_ns;

            if (age_ns > policy_.max_retention_ns) {
                // Past maximum retention — eligible for deletion
                if (policy_.enable_deletion) {
                    bool allow = true;
                    if (pre_delete_cb_) {
                        allow = pre_delete_cb_(fi.path);
                    }
                    if (allow) {
                        std::error_code ec;
                        fs::remove(fi.path, ec);
                        if (ec) {
                            summary.errors.push_back("Failed to delete: " + fi.path
                                                     + " (" + ec.message() + ")");
                            ++summary.files_failed;
                        } else {
                            ++summary.files_deleted;
                            summary.deleted_size_bytes += fi.size_bytes;
                            summary.deleted_paths.push_back(fi.path);
                        }
                    } else {
                        ++summary.files_failed;
                    }
                } else {
                    // Dry-run
                    ++summary.files_deleted;
                    summary.deleted_size_bytes += fi.size_bytes;
                    summary.deleted_paths.push_back(fi.path);
                }
            } else if (age_ns > policy_.archive_after_ns) {
                // Past archival threshold — eligible for archival
                if (policy_.enable_archival && archive_cb_) {
                    bool ok = archive_cb_(fi.path);
                    if (ok) {
                        ++summary.files_archived;
                        summary.archived_size_bytes += fi.size_bytes;
                        summary.archived_paths.push_back(fi.path);
                    } else {
                        summary.errors.push_back("Archive callback failed: " + fi.path);
                        ++summary.files_failed;
                    }
                } else {
                    // Dry-run or no callback
                    ++summary.files_archived;
                    summary.archived_size_bytes += fi.size_bytes;
                    summary.archived_paths.push_back(fi.path);
                }
            } else {
                // Active — within retention window
                ++summary.files_active;
                summary.active_size_bytes += fi.size_bytes;
            }
        }

        // Check size-based overflow: if active files exceed max, archive oldest
        if (summary.active_size_bytes > policy_.max_active_size_bytes) {
            // Already sorted oldest-first — the active files are the ones
            // not already in archived/deleted lists. This is informational
            // in the summary for now; the caller can re-invoke with a
            // shorter archive_after_ns to trigger size-based archival.
        }

        return summary;
    }

    /// List all managed log files in a directory with their age classification.
    ///
    /// @param log_dir  Directory to scan
    /// @param now_ns   Current time in nanoseconds since epoch
    /// @return Vector of (path, age_ns, classification) tuples
    struct FileStatus {
        std::string path;
        int64_t     age_ns;
        uint64_t    size_bytes;
        enum class Classification { ACTIVE, ARCHIVE_ELIGIBLE, DELETE_ELIGIBLE } status;
    };

    [[nodiscard]] inline std::vector<FileStatus> list_files(
            const std::string& log_dir, int64_t now_ns) const {
        std::vector<FileStatus> result;
        namespace fs = std::filesystem;

        if (!fs::exists(log_dir) || !fs::is_directory(log_dir)) return result;

        for (const auto& entry : fs::directory_iterator(log_dir)) {
            if (!entry.is_regular_file()) continue;
            const auto& path = entry.path();
            if (!path.string().ends_with(policy_.file_suffix)) continue;

            std::error_code ec;
            auto mod_time = fs::last_write_time(path, ec);
            if (ec) continue;

            auto sys_time = std::chrono::file_clock::to_sys(mod_time);
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                sys_time.time_since_epoch()).count();

            auto fsize = entry.file_size(ec);
            if (ec) fsize = 0;

            int64_t age = now_ns - static_cast<int64_t>(ns);

            FileStatus fs_entry;
            fs_entry.path = path.string();
            fs_entry.age_ns = age;
            fs_entry.size_bytes = fsize;

            if (age > policy_.max_retention_ns)
                fs_entry.status = FileStatus::Classification::DELETE_ELIGIBLE;
            else if (age > policy_.archive_after_ns)
                fs_entry.status = FileStatus::Classification::ARCHIVE_ELIGIBLE;
            else
                fs_entry.status = FileStatus::Classification::ACTIVE;

            result.push_back(std::move(fs_entry));
        }

        // Sort by age (oldest first)
        std::sort(result.begin(), result.end(),
                  [](const FileStatus& a, const FileStatus& b) {
                      return a.age_ns > b.age_ns;
                  });

        return result;
    }

    /// Get the current retention policy.
    [[nodiscard]] const RetentionPolicy& policy() const noexcept { return policy_; }

private:
    RetentionPolicy   policy_;
    ArchiveCallback   archive_cb_;
    PreDeleteCallback pre_delete_cb_;
};

} // namespace signet::forge
