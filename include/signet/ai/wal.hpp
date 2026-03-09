// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
// wal.hpp — Write-Ahead Log for SignetStack Signet Forge
// Sub-millisecond append with crash recovery and segment rolling.
// Phase 8: Streaming WAL + Async Compaction.

/// @file wal.hpp
/// @brief Write-Ahead Log (WAL) with sub-millisecond append, CRC-32 integrity,
///        crash recovery, and automatic segment rolling.

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#  include <fcntl.h>
#  include <unistd.h>
#endif
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>  // _get_osfhandle, _fileno
#endif

#include "signet/error.hpp"
#include "signet/types.hpp"

namespace signet::forge {

// ---------------------------------------------------------------------------
// WAL record on-disk layout:
//   [magic(4)] [seq_num(8)] [timestamp_ns(8)] [data_size(4)] [data(N)] [crc32(4)]
// Total overhead per record: 28 bytes.
// ---------------------------------------------------------------------------

/// Magic number identifying the start of each WAL record ("WAL1" in ASCII).
static constexpr uint32_t WAL_RECORD_MAGIC  = 0x57414C31u; // "WAL1"

/// Size in bytes of the WAL file header (written once at file offset 0).
static constexpr uint16_t WAL_FILE_HDR_SIZE = 16;

/// Magic bytes written at the start of every WAL file for identification.
static constexpr char     WAL_FILE_MAGIC[16] = "SIGNETWAL1\0\0\0\0\0"; // written once at pos 0

/// Maximum record size (64 MB). Records approaching this limit trigger a
/// warning via FeatureWriter documentation. See CWE-770.
/// @note Records exceeding this limit are rejected by WalWriter::append()
///       and treated as end-of-valid-records by WalReader::next().
static constexpr uint32_t WAL_MAX_RECORD_SIZE = 64u * 1024u * 1024u;  // 64 MB hard cap per record

// ---------------------------------------------------------------------------
// detail::crc32 — standard CRC-32 (polynomial 0xEDB88320), table-driven
// ---------------------------------------------------------------------------
namespace detail {

/// Compute CRC-32 over a contiguous byte buffer (polynomial 0xEDB88320).
///
/// @note L20: This CRC-32 is used for **crash recovery only** (detecting torn
///       writes / partial records). It is NOT a cryptographic integrity check
///       and provides no tamper-evidence guarantees — CRC-32 is trivially
///       forgeable. For tamper-evident audit trails, use the SHA-256 hash
///       chain in audit_chain.hpp.
///
/// @param data   Pointer to input bytes.
/// @param length Number of bytes to checksum.
/// @return CRC-32 checksum.
inline uint32_t crc32(const void* data, size_t length) noexcept {
    // Build table on first call (lazy-init, thread-safe via constinit table below)
    static constexpr auto make_table = []() {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    };
    static constexpr auto table = make_table();

    uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < length; ++i)
        crc = table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/// Combine two CRC regions without concatenating buffers.
/// @note Currently a no-op placeholder; kept as a hook for future incremental CRC.
inline uint32_t crc32_combine(uint32_t crc_a, const void* data_b, size_t len_b) noexcept {
    // We want CRC over the combined region. Since we already have crc_a,
    // we can't extend incrementally without the table's running state.
    // This helper is intentionally unused in favour of the full-buffer variant;
    // kept as a hook for future incremental use.
    (void)crc_a; (void)data_b; (void)len_b;
    return 0;
}

/// Return nanoseconds since Unix epoch (cross-platform).
/// Uses CLOCK_REALTIME on POSIX, timespec_get on Windows.
/// @return Current wall-clock time in nanoseconds.
inline int64_t now_ns() noexcept {
    struct timespec ts{};
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    ::clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

/// Write a 32-bit unsigned integer in little-endian byte order.
/// @param dst Destination buffer (must have at least 4 bytes).
/// @param v   Value to write.
inline void write_le32(uint8_t* dst, uint32_t v) noexcept {
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
    dst[2] = static_cast<uint8_t>(v >> 16);
    dst[3] = static_cast<uint8_t>(v >> 24);
}
/// Write a 64-bit unsigned integer in little-endian byte order.
/// @param dst Destination buffer (must have at least 8 bytes).
/// @param v   Value to write.
inline void write_le64(uint8_t* dst, uint64_t v) noexcept {
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
    dst[2] = static_cast<uint8_t>(v >> 16);
    dst[3] = static_cast<uint8_t>(v >> 24);
    dst[4] = static_cast<uint8_t>(v >> 32);
    dst[5] = static_cast<uint8_t>(v >> 40);
    dst[6] = static_cast<uint8_t>(v >> 48);
    dst[7] = static_cast<uint8_t>(v >> 56);
}
/// Read a 32-bit unsigned integer from little-endian byte order.
/// @param src Source buffer (must have at least 4 bytes).
/// @return Decoded value.
inline uint32_t read_le32(const uint8_t* src) noexcept {
    return static_cast<uint32_t>(src[0])
         | (static_cast<uint32_t>(src[1]) << 8)
         | (static_cast<uint32_t>(src[2]) << 16)
         | (static_cast<uint32_t>(src[3]) << 24);
}
/// Read a 64-bit unsigned integer from little-endian byte order.
/// @param src Source buffer (must have at least 8 bytes).
/// @return Decoded value.
inline uint64_t read_le64(const uint8_t* src) noexcept {
    return static_cast<uint64_t>(src[0])
         | (static_cast<uint64_t>(src[1]) << 8)
         | (static_cast<uint64_t>(src[2]) << 16)
         | (static_cast<uint64_t>(src[3]) << 24)
         | (static_cast<uint64_t>(src[4]) << 32)
         | (static_cast<uint64_t>(src[5]) << 40)
         | (static_cast<uint64_t>(src[6]) << 48)
         | (static_cast<uint64_t>(src[7]) << 56);
}

/// Force durable flush to storage media.
///
/// Uses F_FULLFSYNC on macOS, FlushFileBuffers on Windows, and fsync on Linux.
/// @param fd File descriptor to sync.
/// @return 0 on success, -1 on failure.
inline int full_fsync(int fd) noexcept {
#if defined(__APPLE__)
    return ::fcntl(fd, F_FULLFSYNC);
#elif defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(
        _get_osfhandle(static_cast<int>(fd)));
    if (h == INVALID_HANDLE_VALUE) return -1;
    return FlushFileBuffers(h) ? 0 : -1;
#else
    return ::fsync(fd);
#endif
}

} // namespace detail

// ---------------------------------------------------------------------------
// WalEntry — one decoded record returned by WalReader
// ---------------------------------------------------------------------------

/// A single decoded WAL record returned by WalReader::next() or read_all().
struct WalEntry {
    int64_t              seq          = -1;    ///< Sequence number (0-based, monotonically increasing)
    int64_t              timestamp_ns = 0;     ///< Wall-clock timestamp in nanoseconds since Unix epoch
    std::vector<uint8_t> payload;              ///< Raw record bytes (application-defined content)
};

/// Alias so callers can use either WalEntry or WalRecord.
using WalRecord = WalEntry;

// ---------------------------------------------------------------------------
// WalWriterOptions — options for WalWriter::open()
// (defined outside WalWriter to avoid Apple Clang default-member-init bug)
// ---------------------------------------------------------------------------

/// Configuration options for WalWriter::open().
///
/// Defined at namespace scope (not nested in WalWriter) to avoid an Apple Clang
/// bug with default member initializers in nested structs.
struct WalWriterOptions {
    bool    sync_on_append = false;  ///< If true, fsync after every record append
    bool    sync_on_flush  = true;   ///< If true, fsync on explicit flush() calls
    size_t  buffer_size    = 65536;  ///< stdio setvbuf buffer size in bytes
    int64_t start_seq      = 0;      ///< First sequence number for brand-new files
};

// ---------------------------------------------------------------------------
// WalWriter — append-only WAL file writer
// ---------------------------------------------------------------------------

/// Append-only Write-Ahead Log writer with CRC-32 integrity per record.
///
/// Each record is written as: [magic(4)][seq(8)][ts_ns(8)][size(4)][data(N)][crc32(4)].
/// Thread-safe: all public methods acquire an internal mutex.
///
/// Non-copyable, move-constructible. Use WalWriter::open() to create.
/// @see WalReader, WalManager, WalWriterOptions
class WalWriter {
public:
    /// Alias for the options struct.
    using Options = WalWriterOptions;

    // Non-copyable, movable.
    WalWriter(const WalWriter&)            = delete;
    WalWriter& operator=(const WalWriter&) = delete;
    WalWriter(WalWriter&& o) noexcept
        : file_(o.file_), path_(std::move(o.path_)),
          next_seq_(o.next_seq_), bytes_written_(o.bytes_written_),
          opts_(o.opts_), closed_(o.closed_)
    {
        o.file_   = nullptr;
        o.closed_ = true;
    }
    WalWriter& operator=(WalWriter&&) = delete;

    ~WalWriter() { if (!closed_) (void)close(); }

    /// Open or create a WAL file for appending.
    ///
    /// If the file already exists, it is scanned to determine the next sequence
    /// number so appends continue without gaps. The file header is written only
    /// when the file is first created (empty).
    ///
    /// @param path Filesystem path to the WAL file.
    /// @param opts Writer options (sync policy, buffer size, start sequence).
    /// @return WalWriter on success, Error on I/O failure.
    static expected<WalWriter> open(const std::string& path, Options opts = {}) {
        // Try to open existing file for appending.
        FILE* f = std::fopen(path.c_str(), "a+b");
        if (!f)
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalWriter: cannot open '") + path + "': " + std::strerror(errno)};

        // Apply stdio buffer size.
        if (opts.buffer_size > 0)
            std::setvbuf(f, nullptr, _IOFBF, opts.buffer_size);

        // Determine current file size to decide whether to write the file header.
        // H-10: Use platform-specific 64-bit seek to avoid long overflow
        // on Windows LLP64 where long is 32-bit.
#ifdef _WIN32
        if (_fseeki64(f, 0, SEEK_END) != 0) {
#else
        if (fseeko(f, 0, SEEK_END) != 0) {
#endif
            std::fclose(f);
            return Error{ErrorCode::IO_ERROR, "WalWriter: fseek failed"};
        }
        // L17: use 64-bit ftell on Windows for files > 2 GB.
        // Note: _ftelli64 is required on Windows LLP64 data model where
        // long is 32-bit; standard ftell would silently truncate offsets > 2 GB.
#ifdef _WIN32
        int64_t file_size = _ftelli64(f);
#else
        long file_size = std::ftell(f);
#endif
        if (file_size < 0) {
            std::fclose(f);
            return Error{ErrorCode::IO_ERROR, "WalWriter: ftell failed"};
        }

        int64_t next_seq    = 0;
        int64_t bytes_written = 0;

        if (file_size == 0) {
            // New file — start sequence at opts.start_seq (for segment continuity).
            next_seq = opts.start_seq;
            // Write 16-byte file header.
            if (std::fwrite(WAL_FILE_MAGIC, 1, WAL_FILE_HDR_SIZE, f) != WAL_FILE_HDR_SIZE) {
                std::fclose(f);
                return Error{ErrorCode::IO_ERROR, "WalWriter: failed to write file header"};
            }
            std::fflush(f);
        } else {
            // Existing file — scan to determine the highest seq_num so we can
            // continue the sequence without a gap.
            std::fclose(f);
            f = nullptr;

            // Scan via a temporary reader to find next_seq.
            FILE* rf = std::fopen(path.c_str(), "rb");
            if (rf) {
                // Skip file header.
                // H-10: Use platform-specific 64-bit seek
#ifdef _WIN32
                _fseeki64(rf, WAL_FILE_HDR_SIZE, SEEK_SET);
#else
                fseeko(rf, WAL_FILE_HDR_SIZE, SEEK_SET);
#endif
                // Scan forward collecting seq numbers.
                int64_t last_seq = -1;
                while (true) {
                    uint8_t hdr[24]; // magic(4)+seq(8)+ts(8)+size(4)
                    if (std::fread(hdr, 1, 24, rf) != 24) break;
                    uint32_t magic    = detail::read_le32(hdr);
                    if (magic != WAL_RECORD_MAGIC) break;
                    int64_t  seq      = static_cast<int64_t>(detail::read_le64(hdr + 4));
                    uint32_t data_sz  = detail::read_le32(hdr + 20);
                    // CWE-400: reject corrupt/oversized records during resume scan
                    // (H18+L18: prevents unbounded fseek from crafted data_sz).
                    if (data_sz > WAL_MAX_RECORD_SIZE) break;
                    // Read data + CRC and verify integrity
                    std::vector<uint8_t> record_data(data_sz);
                    if (data_sz > 0 && std::fread(record_data.data(), 1, data_sz, rf) != data_sz) break;
                    uint8_t crc_buf[4];
                    if (std::fread(crc_buf, 1, 4, rf) != 4) break;
                    uint32_t stored_crc = detail::read_le32(crc_buf);
                    // CRC covers header(24) + data
                    std::vector<uint8_t> combined(24 + data_sz);
                    std::memcpy(combined.data(), hdr, 24);
                    if (data_sz > 0)
                        std::memcpy(combined.data() + 24, record_data.data(), data_sz);
                    uint32_t computed_crc = detail::crc32(combined.data(), combined.size());
                    if (computed_crc != stored_crc) break; // reject records with bad CRC
                    last_seq = seq;
                }
                std::fclose(rf);
                if (last_seq >= 0) next_seq = last_seq + 1;
            }

            // Re-open for appending.
            f = std::fopen(path.c_str(), "ab");
            if (!f)
                return Error{ErrorCode::IO_ERROR,
                             std::string("WalWriter: cannot reopen '") + path + "': " + std::strerror(errno)};
            if (opts.buffer_size > 0)
                std::setvbuf(f, nullptr, _IOFBF, opts.buffer_size);
        }

        return WalWriter(f, path, next_seq, bytes_written, opts);
    }

    /// Append a raw-byte record to the WAL.
    ///
    /// Writes a complete record (header + payload + CRC-32) atomically under
    /// the internal mutex. If sync_on_append is enabled, fsyncs after each write.
    ///
    /// @param data Pointer to the record payload bytes.
    /// @param size Number of payload bytes (must be > 0 and <= UINT32_MAX).
    /// @return The sequence number assigned to this record, or Error on failure.
    [[nodiscard]] expected<int64_t> append(const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lk(mu_);
        if (closed_)
            return Error{ErrorCode::IO_ERROR, "WalWriter: already closed"};
        if (size == 0) {
            ++rejected_empty_count_;
            return Error{ErrorCode::IO_ERROR, "WalWriter: empty record rejected"};
        }
        if (size > WAL_MAX_RECORD_SIZE)
            return Error{ErrorCode::INVALID_ARGUMENT, "WAL record exceeds maximum size"};
        if (size > static_cast<size_t>(UINT32_MAX))
            return Error{ErrorCode::IO_ERROR, "WalWriter: record too large"};

        const int64_t  seq  = next_seq_;
        const int64_t  ts   = detail::now_ns();
        const uint32_t dsz  = static_cast<uint32_t>(size);

        // Build header for CRC: magic(4) + seq(8) + ts(8) + size(4) = 24 bytes
        uint8_t hdr[24];
        detail::write_le32(hdr,      WAL_RECORD_MAGIC);
        detail::write_le64(hdr + 4,  static_cast<uint64_t>(seq));
        detail::write_le64(hdr + 12, static_cast<uint64_t>(ts));
        detail::write_le32(hdr + 20, dsz);

        // Compute CRC over header + data.
        // We use a two-pass approach: hash header, then hash data.
        // Since our crc32() requires a contiguous buffer, build one for header
        // and extend manually using the table loop approach inline.
        static constexpr auto make_table = []() {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
            return t;
        };
        static constexpr auto tbl = make_table();

        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < 24; ++i)
            crc = tbl[(crc ^ hdr[i]) & 0xFFu] ^ (crc >> 8);
        for (size_t i = 0; i < size; ++i)
            crc = tbl[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
        crc ^= 0xFFFFFFFFu;

        uint8_t crc_buf[4];
        detail::write_le32(crc_buf, crc);

        // Write: header, data, crc.
        if (std::fwrite(hdr,     1, 24,   file_) != 24)   return io_err("fwrite hdr");
        if (size > 0)
            if (std::fwrite(data, 1, size, file_) != size) return io_err("fwrite data");
        if (std::fwrite(crc_buf, 1, 4,    file_) != 4)    return io_err("fwrite crc");

        bytes_written_ += static_cast<int64_t>(28 + size);
        ++next_seq_;

        if (opts_.sync_on_append) {
            if (std::fflush(file_) != 0)
                return Error{ErrorCode::IO_ERROR, "WalWriter: fflush failed on sync_on_append"};
            if (detail::full_fsync(::fileno(file_)) != 0)
                return Error{ErrorCode::IO_ERROR, "WalWriter: fsync failed on sync_on_append"};
        }

        return seq;
    }

    /// Append a record from a byte vector.
    /// @param data Record payload bytes.
    /// @return Sequence number assigned, or Error on failure.
    [[nodiscard]] expected<int64_t> append(const std::vector<uint8_t>& data) {
        return append(data.data(), data.size());
    }

    /// Append a record from a char pointer.
    /// @param data Record payload bytes (reinterpreted as uint8_t).
    /// @param size Number of bytes.
    /// @return Sequence number assigned, or Error on failure.
    [[nodiscard]] expected<int64_t> append(const char* data, size_t size) {
        return append(reinterpret_cast<const uint8_t*>(data), size);
    }

    /// Append a record from a string_view.
    /// @param sv Record payload.
    /// @return Sequence number assigned, or Error on failure.
    [[nodiscard]] expected<int64_t> append(std::string_view sv) {
        return append(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
    }

    /// Flush the stdio buffer to the OS page cache.
    ///
    /// If @p do_fsync is true (or sync_on_flush is enabled in Options), also
    /// performs a durable fsync to storage media.
    ///
    /// @param do_fsync Force an fsync regardless of Options.
    /// @return Error on I/O failure.
    [[nodiscard]] expected<void> flush(bool do_fsync = false) {
        std::lock_guard<std::mutex> lk(mu_);
        if (closed_)
            return Error{ErrorCode::IO_ERROR, "WalWriter: already closed"};
        if (std::fflush(file_) != 0)
            return Error{ErrorCode::IO_ERROR, "WalWriter: fflush failed"};
        if (do_fsync || opts_.sync_on_flush) {
            if (detail::full_fsync(::fileno(file_)) != 0)
                return Error{ErrorCode::IO_ERROR, "WalWriter: fsync failed"};
        }
        return {};
    }

    /// Seal the WAL file: flush buffered data, fsync, and close the file handle.
    /// @return Error on I/O failure.
    [[nodiscard]] expected<void> close() {
        std::lock_guard<std::mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
        if (std::fflush(file_) != 0) {
            std::fclose(file_); file_ = nullptr;
            return Error{ErrorCode::IO_ERROR, "WalWriter: fflush failed on close"};
        }
        if (detail::full_fsync(::fileno(file_)) != 0) {
            std::fclose(file_); file_ = nullptr;
            return Error{ErrorCode::IO_ERROR, "WalWriter: fsync failed on close"};
        }
        std::fclose(file_);
        file_ = nullptr;
        return {};
    }

    /// True if the writer is open and accepting appends.
    [[nodiscard]] bool        is_open()        const noexcept { return !closed_; }
    /// The sequence number that will be assigned to the next appended record.
    [[nodiscard]] int64_t     next_seq()       const noexcept { return next_seq_; }
    /// Filesystem path of the WAL file.
    [[nodiscard]] const std::string& path()    const noexcept { return path_; }
    /// Total bytes written to this WAL file (header + all records).
    [[nodiscard]] int64_t     bytes_written()  const noexcept { return bytes_written_; }
    /// Number of empty records rejected (CWE-754).
    [[nodiscard]] uint64_t    rejected_empty_count() const noexcept { return rejected_empty_count_; }

private:
    WalWriter(FILE* f, std::string path, int64_t next_seq,
              int64_t bytes_written, Options opts)
        : file_(f), path_(std::move(path)),
          next_seq_(next_seq), bytes_written_(bytes_written),
          opts_(opts), closed_(false) {}

    static expected<int64_t> io_err(const char* ctx) {
        return Error{ErrorCode::IO_ERROR,
                     std::string("WalWriter: ") + ctx + ": " + std::strerror(errno)};
    }

    FILE*       file_          = nullptr;
    std::string path_;
    int64_t     next_seq_      = 0;
    int64_t     bytes_written_ = 0;
    uint64_t    rejected_empty_count_ = 0; ///< Number of empty records rejected (CWE-754)
    Options     opts_;
    bool        closed_        = false;
    std::mutex  mu_;
};

// ---------------------------------------------------------------------------
// WalReader — sequential scan for crash recovery
// ---------------------------------------------------------------------------

/// Sequential WAL file reader for crash recovery and replay.
///
/// Reads records forward from the file header, validating magic bytes and
/// CRC-32 integrity. Stops gracefully on truncation, corruption, or EOF
/// (returns nullopt rather than Error for soft failures).
///
/// Non-copyable, move-constructible. Use WalReader::open() to create.
/// @see WalWriter, WalEntry
class WalReader {
public:
    WalReader(const WalReader&)            = delete;
    WalReader& operator=(const WalReader&) = delete;
    WalReader(WalReader&& o) noexcept
        : file_(o.file_), path_(std::move(o.path_)),
          last_seq_(o.last_seq_), count_(o.count_), offset_(o.offset_)
    { o.file_ = nullptr; }
    WalReader& operator=(WalReader&&) = delete;

    ~WalReader() { close(); }

    /// Open a WAL file for sequential reading.
    ///
    /// Validates the 16-byte file header magic. After opening, call next()
    /// or read_all() to iterate records.
    ///
    /// @param path Filesystem path to the WAL file.
    /// @return WalReader on success, Error if the file cannot be opened or has
    ///         an invalid header.
    static expected<WalReader> open(const std::string& path) {
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalReader: cannot open '") + path + "': " + std::strerror(errno)};

        // Read and validate 16-byte file header.
        char hdr[WAL_FILE_HDR_SIZE];
        if (std::fread(hdr, 1, WAL_FILE_HDR_SIZE, f) != WAL_FILE_HDR_SIZE) {
            std::fclose(f);
            return Error{ErrorCode::INVALID_FILE, "WalReader: file too short for header"};
        }
        if (std::memcmp(hdr, WAL_FILE_MAGIC, WAL_FILE_HDR_SIZE) != 0) {
            std::fclose(f);
            return Error{ErrorCode::INVALID_FILE, "WalReader: bad file magic"};
        }

        return WalReader(f, path, WAL_FILE_HDR_SIZE);
    }

    /// Read the next WAL entry from the file.
    ///
    /// Returns nullopt on clean EOF, truncation, bad magic, oversized records,
    /// or CRC mismatch (all treated as soft end-of-valid-data). Returns an
    /// Error only on hard I/O failures (fread sets errno).
    ///
    /// @return The next WalEntry, nullopt on soft stop, or Error on hard I/O failure.
    [[nodiscard]] expected<std::optional<WalEntry>> next() {
        if (!file_) return std::optional<WalEntry>{std::nullopt};

        // Read 24-byte record header.
        uint8_t hdr[24];
        size_t n = std::fread(hdr, 1, 24, file_);
        if (n == 0) {
            // True EOF or error.
            if (std::ferror(file_))
                return Error{ErrorCode::IO_ERROR, "WalReader: fread header failed"};
            return std::optional<WalEntry>{std::nullopt}; // clean EOF
        }
        if (n < 24)
            return std::optional<WalEntry>{std::nullopt}; // truncated header

        uint32_t magic   = detail::read_le32(hdr);
        if (magic != WAL_RECORD_MAGIC)
            return std::optional<WalEntry>{std::nullopt}; // bad magic — stop

        int64_t  seq     = static_cast<int64_t>(detail::read_le64(hdr + 4));
        int64_t  ts      = static_cast<int64_t>(detail::read_le64(hdr + 12));
        uint32_t data_sz = detail::read_le32(hdr + 20);

        // CWE-400: Uncontrolled Resource Consumption — reject oversized records
        // to prevent unbounded memory allocation from crafted data_sz values.
        if (data_sz > WAL_MAX_RECORD_SIZE) {
            return std::optional<WalEntry>{std::nullopt}; // treat as end-of-valid-records
        }

        // Read payload bytes into raw_buf (renamed to avoid collision with WalEntry::payload field).
        std::vector<uint8_t> raw_buf(data_sz);
        if (data_sz > 0) {
            size_t nr = std::fread(raw_buf.data(), 1, data_sz, file_);
            if (nr < data_sz) {
                if (std::ferror(file_))
                    return Error{ErrorCode::IO_ERROR, "WalReader: fread payload failed"};
                return std::optional<WalEntry>{std::nullopt}; // truncated payload
            }
        }

        // Read stored CRC.
        uint8_t crc_buf[4];
        if (std::fread(crc_buf, 1, 4, file_) != 4)
            return std::optional<WalEntry>{std::nullopt}; // truncated CRC

        uint32_t stored_crc = detail::read_le32(crc_buf);

        // Compute expected CRC over header + data.
        static constexpr auto make_table = []() {
            std::array<uint32_t, 256> t{};
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
            return t;
        };
        static constexpr auto tbl = make_table();

        uint32_t crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < 24; ++i)
            crc = tbl[(crc ^ hdr[i]) & 0xFFu] ^ (crc >> 8);
        for (size_t i = 0; i < data_sz; ++i)
            crc = tbl[(crc ^ raw_buf[i]) & 0xFFu] ^ (crc >> 8);
        crc ^= 0xFFFFFFFFu;

        if (crc != stored_crc)
            return std::optional<WalEntry>{std::nullopt}; // CRC mismatch — stop

        offset_ += 24 + data_sz + 4;
        last_seq_ = seq;
        ++count_;

        WalEntry entry;
        entry.seq          = seq;
        entry.timestamp_ns = ts;
        entry.payload      = std::move(raw_buf);
        return std::optional<WalEntry>{std::move(entry)};
    }

    /// Read all valid entries from the current position to end-of-valid-data.
    ///
    /// Stops at the first corrupt, truncated, or EOF record. Useful for full
    /// crash-recovery replay.
    ///
    /// @return Vector of all valid WalEntry records, or Error on hard I/O failure.
    [[nodiscard]] expected<std::vector<WalEntry>> read_all() {
        std::vector<WalEntry> results;
        while (true) {
            auto res = next();
            if (!res) return res.error();
            if (!res->has_value()) break;
            results.push_back(std::move(res->value()));
        }
        return results;
    }

    /// Sequence number of the last successfully read record (-1 if none).
    [[nodiscard]] int64_t last_seq() const noexcept { return last_seq_; }
    /// Number of records successfully read so far.
    [[nodiscard]] int64_t count()    const noexcept { return count_; }
    /// Current byte offset in the file (past the last read record).
    [[nodiscard]] size_t  offset()   const noexcept { return offset_; }

    /// Close the underlying file handle.
    void close() {
        if (file_) { std::fclose(file_); file_ = nullptr; }
    }

private:
    WalReader(FILE* f, std::string path, size_t initial_offset)
        : file_(f), path_(std::move(path)), offset_(initial_offset) {}

    FILE*       file_     = nullptr;
    std::string path_;
    int64_t     last_seq_ = -1;
    int64_t     count_    = 0;
    size_t      offset_   = WAL_FILE_HDR_SIZE;
};

// ---------------------------------------------------------------------------
// WalManagerOptions — options for WalManager::open()
// (defined outside WalManager to avoid Apple Clang default-member-init bug)
// ---------------------------------------------------------------------------

/// Controls safety guardrails for WAL segment lifecycle operations.
///
/// In Production mode, destructive operations like reset_on_open are denied.
enum class WalLifecycleMode : uint8_t {
    Development = 0,  ///< Permissive: allows reset_on_open
    Benchmark   = 1,  ///< Same as Development, for benchmark harnesses
    Production  = 2   ///< Strict: reset_on_open is rejected
};

/// Configuration options for WalManager::open().
///
/// Defined at namespace scope (not nested in WalManager) to avoid an Apple Clang
/// bug with default member initializers in nested structs.
/// @see WalManager
struct WalManagerOptions {
    size_t      max_segment_bytes = 64 * 1024 * 1024; ///< Max bytes per segment before rolling (64 MB)
    size_t      max_records    = 1'000'000;            ///< Max records per segment before rolling
    bool        sync_on_append = false;                ///< fsync after every record append
    bool        sync_on_roll   = true;                 ///< fsync when rolling to a new segment
    std::string file_prefix    = "wal";                ///< Segment filename prefix
    std::string file_ext       = ".wal";               ///< Segment filename extension

    // Safety guardrails
    WalLifecycleMode lifecycle_mode                  = WalLifecycleMode::Development; ///< Lifecycle safety mode
    bool             reset_on_open                   = false;                         ///< Delete all existing segments on open (denied in Production)
    bool             require_checkpoint_before_prune = false;                         ///< Require checkpoint commit before segment removal
    std::string      checkpoint_manifest_path;                                        ///< Path for atomic checkpoint manifest file
};

// ---------------------------------------------------------------------------
// WalManager — rolling WAL segments
// ---------------------------------------------------------------------------

/// Manages multiple rolling WAL segment files in a directory.
///
/// Automatically rolls to a new segment when the current segment exceeds
/// max_segment_bytes or max_records. Provides crash-recovery read_all()
/// across all segments, compaction checkpoint support, and safe segment pruning.
///
/// Thread-safe: all public methods acquire an internal mutex.
/// Non-copyable, move-constructible. Use WalManager::open() to create.
/// @see WalWriter, WalReader, WalManagerOptions
class WalManager {
public:
    /// Alias for the options struct.
    using Options = WalManagerOptions;

    WalManager(const WalManager&)            = delete;
    WalManager& operator=(const WalManager&) = delete;
    WalManager(WalManager&& o) noexcept
        : dir_(std::move(o.dir_)), opts_(o.opts_),
          segments_(std::move(o.segments_)),
          writer_(std::move(o.writer_)),
          global_seq_(o.global_seq_),
          segment_record_count_(o.segment_record_count_),
          total_records_(o.total_records_),
          prune_checkpoint_ready_(o.prune_checkpoint_ready_),
          closed_(o.closed_)
    { o.closed_ = true; }
    WalManager& operator=(WalManager&&) = delete;

    ~WalManager() { (void)close(); }

    /// Open a WAL directory, discovering existing segments and creating a new
    /// active segment ready for writing.
    ///
    /// If reset_on_open is true (and lifecycle_mode is not Production), all
    /// existing segment files are deleted before starting fresh.
    ///
    /// @param dir  Directory path (created if it does not exist).
    /// @param opts Manager options (segment limits, sync policy, lifecycle mode).
    /// @return WalManager on success, Error on I/O or policy failure.
    static expected<WalManager> open(const std::string& dir, Options opts = {}) {
        // Ensure directory exists first.
        std::error_code mk_ec;
        std::filesystem::create_directories(dir, mk_ec);
        if (mk_ec) {
            std::error_code isdir_ec;
            if (!std::filesystem::is_directory(dir, isdir_ec)) {
                return Error{ErrorCode::IO_ERROR,
                             std::string("WalManager: cannot create dir '") + dir + "': " + mk_ec.message()};
            }
        }

        if (opts.reset_on_open && opts.lifecycle_mode == WalLifecycleMode::Production) {
            return Error{ErrorCode::IO_ERROR,
                         "WalManager: reset_on_open denied in production mode"};
        }

        if (opts.reset_on_open) {
            auto reset_paths = scan_segments(dir, opts);
            for (const auto& seg : reset_paths) {
                std::error_code rm_ec;
                std::filesystem::remove(seg, rm_ec);
                if (rm_ec && rm_ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
                    return Error{ErrorCode::IO_ERROR,
                                 std::string("WalManager: reset remove '") + seg + "': " + rm_ec.message()};
                }
            }
        }

        // Discover existing WAL segment files in the directory.
        std::vector<std::string> existing = scan_segments(dir, opts);

        // Determine global_seq from the last segment if any exist.
        int64_t global_seq = 0;
        if (!existing.empty()) {
            auto rd = WalReader::open(existing.back());
            if (rd) {
                auto entries = rd->read_all();
                (void)entries;
                if (rd->last_seq() >= 0)
                    global_seq = rd->last_seq() + 1;
            }
        }

        // Count total records across all existing segments.
        int64_t total = 0;
        for (const auto& seg : existing) {
            auto rd = WalReader::open(seg);
            if (rd) {
                auto res = rd->read_all();
                if (res) total += static_cast<int64_t>(res->size());
            }
        }

        // Open a new active segment starting at the current global sequence number.
        WalWriter::Options wopts;
        wopts.sync_on_append = opts.sync_on_append;
        wopts.sync_on_flush  = opts.sync_on_roll;
        wopts.start_seq      = global_seq;

        std::string seg_path = make_segment_path(dir, opts, global_seq);
        auto writer = WalWriter::open(seg_path, wopts);
        if (!writer) return writer.error();

        WalManager mgr;
        mgr.dir_                    = dir;
        mgr.opts_                   = opts;
        mgr.segments_               = std::move(existing);
        mgr.segments_.push_back(seg_path);
        mgr.writer_                 = std::make_unique<WalWriter>(std::move(writer.value()));
        mgr.global_seq_             = global_seq;
        mgr.segment_record_count_   = 0;
        mgr.total_records_          = total;
        mgr.prune_checkpoint_ready_ = !opts.require_checkpoint_before_prune;
        mgr.closed_                 = false;
        return mgr;
    }

    /// Append a record to the current segment, rolling to a new segment if needed.
    ///
    /// @param data Pointer to the record payload bytes.
    /// @param size Number of payload bytes.
    /// @return The global sequence number assigned, or Error on failure.
    [[nodiscard]] expected<int64_t> append(const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lk(mu_);
        if (closed_)
            return Error{ErrorCode::IO_ERROR, "WalManager: already closed"};

        // Check roll conditions.
        bool need_roll = false;
        if (opts_.max_records > 0 && static_cast<size_t>(segment_record_count_) >= opts_.max_records)
            need_roll = true;
        if (!need_roll && opts_.max_segment_bytes > 0) {
            int64_t bw = writer_->bytes_written();
            if (static_cast<size_t>(bw) + 28 + size > opts_.max_segment_bytes)
                need_roll = true;
        }

        if (need_roll) {
            auto rc = roll_locked();
            if (!rc) return rc.error();
        }

        auto res = writer_->append(data, size);
        if (!res) return res;
        ++segment_record_count_;
        ++total_records_;
        ++global_seq_;
        return res;
    }

    /// Append a record from a char pointer.
    /// @param data Record payload bytes.
    /// @param size Number of bytes.
    /// @return Global sequence number assigned, or Error on failure.
    [[nodiscard]] expected<int64_t> append(const char* data, size_t size) {
        return append(reinterpret_cast<const uint8_t*>(data), size);
    }

    /// Append a record from a string_view.
    /// @param sv Record payload.
    /// @return Global sequence number assigned, or Error on failure.
    [[nodiscard]] expected<int64_t> append(std::string_view sv) {
        return append(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
    }

    /// List all WAL segment paths in sequence order (oldest first).
    /// @return Sorted vector of segment file paths.
    [[nodiscard]] std::vector<std::string> segment_paths() const {
        std::lock_guard<std::mutex> lk(mu_);
        return segments_;
    }

    /// Record that an external compaction checkpoint has been made durable,
    /// allowing old fully-compacted WAL segments to be pruned safely.
    ///
    /// If checkpoint_manifest_path is configured, writes an atomic marker file.
    /// Must be called before remove_segment() when require_checkpoint_before_prune
    /// is enabled.
    ///
    /// @param note Optional human-readable note written to the manifest.
    /// @return Error on I/O failure.
    [[nodiscard]] expected<void> commit_compaction_checkpoint(const std::string& note = "") {
        std::lock_guard<std::mutex> lk(mu_);
        if (!opts_.require_checkpoint_before_prune) {
            prune_checkpoint_ready_ = true;
            return {};
        }

        if (opts_.checkpoint_manifest_path.empty()) {
            // Caller opts out of marker-file persistence and takes responsibility
            // for external durable checkpointing.
            prune_checkpoint_ready_ = true;
            return {};
        }

        auto wr = write_manifest_atomic(opts_.checkpoint_manifest_path, note);
        if (!wr) return wr;
        prune_checkpoint_ready_ = true;
        return {};
    }

    /// Reset the checkpoint-ready flag so subsequent prune calls are blocked
    /// again until the next commit_compaction_checkpoint().
    void clear_compaction_checkpoint() {
        std::lock_guard<std::mutex> lk(mu_);
        prune_checkpoint_ready_ = !opts_.require_checkpoint_before_prune;
    }

    /// Remove a fully-compacted segment from disk and from the tracking list.
    ///
    /// The currently active (most recent) segment cannot be removed.
    /// If require_checkpoint_before_prune is enabled, commit_compaction_checkpoint()
    /// must be called first.
    ///
    /// @param path Filesystem path of the segment to remove.
    /// @return Error if the segment is active, checkpoint not committed, or I/O failure.
    [[nodiscard]] expected<void> remove_segment(const std::string& path) {
        std::lock_guard<std::mutex> lk(mu_);
        if (opts_.require_checkpoint_before_prune && !prune_checkpoint_ready_)
            return Error{ErrorCode::IO_ERROR,
                         "WalManager: prune blocked until compaction checkpoint is committed"};
        // Do not remove the currently active segment.
        if (!segments_.empty() && segments_.back() == path)
            return Error{ErrorCode::IO_ERROR,
                         "WalManager: cannot remove active segment"};
        {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory))
                return Error{ErrorCode::IO_ERROR,
                             std::string("WalManager: remove '") + path + "': " + ec.message()};
        }
        auto it = std::find(segments_.begin(), segments_.end(), path);
        if (it != segments_.end()) segments_.erase(it);
        return {};
    }

    /// Total number of records written across all segments (including rolled ones).
    [[nodiscard]] int64_t total_records() const noexcept {
        std::lock_guard<std::mutex> lk(mu_);
        return total_records_;
    }

    /// Close the manager and the active segment writer.
    /// @return Error on I/O failure.
    [[nodiscard]] expected<void> close() {
        std::lock_guard<std::mutex> lk(mu_);
        if (closed_) return {};
        closed_ = true;
        if (writer_) (void)writer_->close();
        return {};
    }

    /// Read all WAL entries across all segments (sealed and active).
    ///
    /// The active segment is flushed first so all buffered records become
    /// visible to the reader. Records are returned in sequence order.
    ///
    /// @return All valid WalEntry records across every segment.
    [[nodiscard]] expected<std::vector<WalEntry>> read_all() {
        std::lock_guard<std::mutex> lk(mu_);
        // Flush active segment so the reader can see all written records.
        if (writer_ && !closed_) (void)writer_->flush(false);
        std::vector<WalEntry> result;
        for (const auto& seg : segments_) {
            auto rd = WalReader::open(seg);
            if (!rd) continue;  // skip unreadable/missing segments
            auto entries = rd->read_all();
            if (!entries) continue;
            for (auto& e : *entries)
                result.push_back(std::move(e));
        }
        return result;
    }

private:
    WalManager() = default;

    static expected<void> sync_parent_directory(const std::string& file_path) {
#if !defined(_WIN32)
        std::filesystem::path p(file_path);
        std::filesystem::path parent = p.parent_path();
        if (parent.empty()) return {};

        int dfd = ::open(parent.string().c_str(), O_RDONLY);
        if (dfd < 0) {
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: open parent dir failed: ") + std::strerror(errno)};
        }
        if (::fsync(dfd) != 0) {
            int e = errno;
            ::close(dfd);
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: fsync parent dir failed: ") + std::strerror(e)};
        }
        ::close(dfd);
#endif
        return {};
    }

    static expected<void> write_manifest_atomic(const std::string& path,
                                                const std::string& note) {
        std::filesystem::path p(path);
        if (p.empty()) {
            return Error{ErrorCode::IO_ERROR,
                         "WalManager: checkpoint_manifest_path is empty"};
        }

        std::error_code mk_ec;
        auto parent = p.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, mk_ec);
        if (mk_ec) {
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: cannot create manifest dir: ") + mk_ec.message()};
        }

        const std::string tmp = path + ".tmp." + std::to_string(detail::now_ns());
        FILE* f = std::fopen(tmp.c_str(), "wb");
        if (!f) {
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: cannot open temp manifest '") + tmp + "': " + std::strerror(errno)};
        }

        std::string body = "checkpoint_ns=" + std::to_string(detail::now_ns()) + "\n";
        if (!note.empty()) {
            body += "note=" + note + "\n";
        }

        if (std::fwrite(body.data(), 1, body.size(), f) != body.size()) {
            std::fclose(f);
            std::remove(tmp.c_str());
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: write manifest failed: ") + std::strerror(errno)};
        }
        if (std::fflush(f) != 0) {
            std::fclose(f);
            std::remove(tmp.c_str());
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: fflush manifest failed: ") + std::strerror(errno)};
        }
        if (detail::full_fsync(::fileno(f)) != 0) {
            std::fclose(f);
            std::remove(tmp.c_str());
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: fsync manifest failed: ") + std::strerror(errno)};
        }
        std::fclose(f);

        std::error_code mv_ec;
        std::filesystem::rename(tmp, path, mv_ec);
        if (mv_ec) {
            std::remove(tmp.c_str());
            return Error{ErrorCode::IO_ERROR,
                         std::string("WalManager: atomic rename manifest failed: ") + mv_ec.message()};
        }

        auto sync_res = sync_parent_directory(path);
        if (!sync_res) return sync_res;
        return {};
    }

    // Must be called with mu_ held.
    expected<void> roll_locked() {
        // Flush + fsync the current segment.
        if (opts_.sync_on_roll) {
            (void)writer_->flush(true);
        }
        (void)writer_->close();

        // Open a new segment continuing the global sequence.
        WalWriter::Options wopts;
        wopts.sync_on_append = opts_.sync_on_append;
        wopts.sync_on_flush  = opts_.sync_on_roll;
        wopts.start_seq      = global_seq_;

        std::string seg_path = make_segment_path(dir_, opts_, global_seq_);
        auto writer = WalWriter::open(seg_path, wopts);
        if (!writer) return writer.error();

        writer_               = std::make_unique<WalWriter>(std::move(writer.value()));
        segments_.push_back(seg_path);
        segment_record_count_ = 0;
        return {};
    }

    // Generate segment filename: {prefix}_{seq_start}_{timestamp_ms}.{ext}
    static std::string make_segment_path(const std::string& dir,
                                         const Options& opts,
                                         int64_t seq_start) {
        // timestamp in milliseconds for the filename (cross-platform).
        struct timespec ts{};
#if defined(_WIN32)
        timespec_get(&ts, TIME_UTC);
#else
        ::clock_gettime(CLOCK_REALTIME, &ts);
#endif
        int64_t ts_ms = static_cast<int64_t>(ts.tv_sec) * 1000LL
                      + ts.tv_nsec / 1'000'000LL;

        // Build: dir + "/" + prefix + "_" + seq + "_" + ts_ms + ext
        std::string name = dir + "/" + opts.file_prefix
                         + "_" + std::to_string(seq_start)
                         + "_" + std::to_string(ts_ms)
                         + opts.file_ext;
        return name;
    }

    // Scan directory for existing WAL segments matching prefix/ext.
    // Returns sorted list by embedded seq_start (numeric parse of filename).
    // Uses std::filesystem::directory_iterator (cross-platform; no opendir/readdir).
    static std::vector<std::string> scan_segments(const std::string& dir,
                                                   const Options& opts) {
        std::vector<std::pair<int64_t, std::string>> found;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const std::string fname = entry.path().filename().string();
            // Must start with prefix + "_" and end with ext.
            const std::string pfx = opts.file_prefix + "_";
            if (fname.size() <= pfx.size() + opts.file_ext.size()) continue;
            if (fname.substr(0, pfx.size()) != pfx) continue;
            if (fname.substr(fname.size() - opts.file_ext.size()) != opts.file_ext) continue;
            // Extract seq_start from either:
            //   prefix_<seq>_<ts>.wal  (WalManager format)
            //   prefix_<seq>.wal       (WalMmapWriter format)
            size_t p1 = pfx.size();
            size_t ext_pos = fname.size() - opts.file_ext.size();
            if (ext_pos <= p1) continue;
            size_t p2 = fname.find('_', p1);

            std::string seq_str;
            if (p2 == std::string::npos || p2 > ext_pos) {
                seq_str = fname.substr(p1, ext_pos - p1);
            } else {
                seq_str = fname.substr(p1, p2 - p1);
            }

            int64_t seq_start = 0;
            try { seq_start = std::stoll(seq_str); } catch (...) { continue; }
            found.emplace_back(seq_start, entry.path().string());
        }
        std::sort(found.begin(), found.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<std::string> paths;
        paths.reserve(found.size());
        for (auto& [seq, p] : found) paths.push_back(std::move(p));
        return paths;
    }

    std::string              dir_;
    Options                  opts_;
    std::vector<std::string> segments_;
    std::unique_ptr<WalWriter> writer_;
    int64_t                  global_seq_           = 0;
    int64_t                  segment_record_count_ = 0;
    int64_t                  total_records_         = 0;
    bool                     prune_checkpoint_ready_ = true;
    bool                     closed_                = false;
    mutable std::mutex       mu_;
};

} // namespace signet::forge

// ---------------------------------------------------------------------------
// Memory-mapped WAL segment and ring writer.
// Included here so detail_mmap:: helpers have access to the signet::forge
// namespace already opened above; WalMmapWriter produces files readable by
// WalReader without any changes to the reader.
// Excluded on Emscripten — WalMmapWriter requires std::thread + POSIX mmap.
// ---------------------------------------------------------------------------
#ifndef __EMSCRIPTEN__
#include "signet/ai/wal_mapped_segment.hpp"
#endif

