// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
/// @file wal_mapped_segment.hpp
/// @brief Cross-platform memory-mapped WAL segment and ring writer.
//
// Provides:
//   MappedSegment       — RAII mmap'd file (POSIX + Windows)
//   WalMmapOptions      — options struct (namespace scope — Apple Clang restriction)
//   WalMmapWriter       — ring of N mmap'd segments with bg pre-allocation thread
//
// File format: IDENTICAL to WalWriter (WAL_FILE_MAGIC at byte 0, records at byte 16).
// WalReader works unchanged on WalMmapWriter segment files.
//
// Thread safety: NOT thread-safe for concurrent appends. Single-writer model.
// flush() and close() must be called from the same thread as append().
//
// Crash safety: CRC is written LAST with a release fence.
// WalReader stops at bad/missing CRC, recovering all complete records.
//
// This file is included at the bottom of wal.hpp so that the detail_mmap helpers
// defined here do not pollute the signet::forge::detail namespace.

#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <unistd.h>
#  include <fcntl.h>
#  if defined(__APPLE__)
#    include <sys/types.h>
#  endif
#endif

#include "signet/error.hpp"

namespace signet::forge {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/// Minimum segment size (64 KB) -- matches Windows MapViewOfFile allocation granularity.
static constexpr size_t WAL_MMAP_MIN_SEGMENT     = 65536ULL;
/// Default segment size (64 MB).
static constexpr size_t WAL_MMAP_DEFAULT_SEGMENT = 64ULL * 1024ULL * 1024ULL;
/// Maximum segment size (16 GB).
static constexpr size_t WAL_MMAP_MAX_SEGMENT     = 16ULL * 1024ULL * 1024ULL * 1024ULL;
/// Maximum number of ring segments in WalMmapWriter.
static constexpr size_t WAL_MMAP_MAX_RING        = 16;

// ---------------------------------------------------------------------------
// detail_mmap — private helpers (no dependency on wal.hpp detail:: namespace)
// ---------------------------------------------------------------------------

/// @internal Implementation details for wal_mapped_segment -- not part of the public API.
namespace detail_mmap {

/// Return the current wall-clock time in nanoseconds since epoch.
inline int64_t now_ns() noexcept {
    struct timespec ts{};
#if defined(_WIN32)
    timespec_get(&ts, TIME_UTC);
#else
    ::clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL + ts.tv_nsec;
}

/// Write a 32-bit value in little-endian byte order.
inline void write_le32(uint8_t* dst, uint32_t v) noexcept {
    dst[0] = static_cast<uint8_t>(v);
    dst[1] = static_cast<uint8_t>(v >> 8);
    dst[2] = static_cast<uint8_t>(v >> 16);
    dst[3] = static_cast<uint8_t>(v >> 24);
}

/// Write a 64-bit value in little-endian byte order.
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

/// WAL record magic number ("WAL1"), mirrors wal.hpp.
static constexpr uint32_t MMAP_WAL_RECORD_MAGIC  = 0x57414C31u;
/// WAL file header size in bytes.
static constexpr uint16_t MMAP_WAL_FILE_HDR_SIZE = 16;
/// 16-byte file magic: "SIGNETWAL1" + 6 NULs (same as WAL_FILE_MAGIC in wal.hpp).
static constexpr char MMAP_WAL_FILE_MAGIC[16] = {'S','I','G','N','E','T','W','A','L','1',
                                                   '\0','\0','\0','\0','\0','\0'};

} // namespace detail_mmap

// ---------------------------------------------------------------------------
// MappedSegment — RAII cross-platform mmap'd file
// ---------------------------------------------------------------------------

/// RAII cross-platform memory-mapped file segment.
///
/// Creates, maps, and manages a file backed by OS-level memory mapping
/// (POSIX mmap / Windows MapViewOfFile). Supports async and sync flush,
/// pre-faulting, and safe close semantics.
///
/// @note Non-copyable but movable. Calling close() multiple times is safe.
///
/// @see WalMmapWriter
class MappedSegment {
public:
    /// Default-construct an invalid (unmapped) segment.
    MappedSegment() = default;
    ~MappedSegment() noexcept { close(); }
    MappedSegment(const MappedSegment&) = delete;
    MappedSegment& operator=(const MappedSegment&) = delete;

    MappedSegment(MappedSegment&& o) noexcept
        :
#if defined(_WIN32)
          hFile_(o.hFile_), hMap_(o.hMap_),
#else
          fd_(o.fd_),
#endif
          ptr_(o.ptr_), size_(o.size_), path_(std::move(o.path_))
    {
#if defined(_WIN32)
        o.hFile_ = INVALID_HANDLE_VALUE;
        o.hMap_  = nullptr;
#else
        o.fd_    = -1;
#endif
        o.ptr_  = nullptr;
        o.size_ = 0;
    }

    MappedSegment& operator=(MappedSegment&& o) noexcept {
        if (this != &o) {
            close();
#if defined(_WIN32)
            hFile_ = o.hFile_; o.hFile_ = INVALID_HANDLE_VALUE;
            hMap_  = o.hMap_;  o.hMap_  = nullptr;
#else
            fd_    = o.fd_;    o.fd_    = -1;
#endif
            ptr_   = o.ptr_;   o.ptr_   = nullptr;
            size_  = o.size_;  o.size_  = 0;
            path_  = std::move(o.path_);
        }
        return *this;
    }

    /// Create (overwrite) and memory-map a new segment file of the given byte size.
    ///
    /// On POSIX, uses ftruncate + mmap with MADV_SEQUENTIAL. On macOS, attempts
    /// contiguous pre-allocation via F_PREALLOCATE. On Windows, uses CreateFileMapping.
    ///
    /// @param path  File path for the new segment (created or overwritten).
    /// @param size  Segment size in bytes; must be in [WAL_MMAP_MIN_SEGMENT, WAL_MMAP_MAX_SEGMENT]
    ///              and a multiple of 65536.
    /// @return A mapped segment, or an Error on failure.
    static expected<MappedSegment> create(const std::string& path, size_t size) noexcept {
        if (size < WAL_MMAP_MIN_SEGMENT || size > WAL_MMAP_MAX_SEGMENT)
            return Error{ErrorCode::IO_ERROR, "MappedSegment: invalid size"};
        if (size % WAL_MMAP_MIN_SEGMENT != 0)
            return Error{ErrorCode::IO_ERROR, "MappedSegment: size must be multiple of 65536"};

        MappedSegment seg;
        seg.size_ = size;
        seg.path_ = path;

#if defined(_WIN32)
        // --- Windows path ---
        seg.hFile_ = CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (seg.hFile_ == INVALID_HANDLE_VALUE) {
            return Error{ErrorCode::IO_ERROR,
                std::string("MappedSegment: CreateFileA failed for '") + path + "'"};
        }

        // Extend file to requested size
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(seg.hFile_, li, nullptr, FILE_BEGIN) ||
            !SetEndOfFile(seg.hFile_)) {
            CloseHandle(seg.hFile_);
            seg.hFile_ = INVALID_HANDLE_VALUE;
            return Error{ErrorCode::IO_ERROR,
                std::string("MappedSegment: SetEndOfFile failed for '") + path + "'"};
        }

        DWORD size_hi = static_cast<DWORD>(size >> 32);
        DWORD size_lo = static_cast<DWORD>(size & 0xFFFFFFFFULL);
        seg.hMap_ = CreateFileMappingA(
            seg.hFile_,
            nullptr,
            PAGE_READWRITE,
            size_hi,
            size_lo,
            nullptr);
        if (!seg.hMap_) {
            CloseHandle(seg.hFile_);
            seg.hFile_ = INVALID_HANDLE_VALUE;
            return Error{ErrorCode::IO_ERROR,
                std::string("MappedSegment: CreateFileMappingA failed for '") + path + "'"};
        }

        void* ptr = MapViewOfFile(seg.hMap_, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (!ptr) {
            CloseHandle(seg.hMap_);
            CloseHandle(seg.hFile_);
            seg.hMap_  = nullptr;
            seg.hFile_ = INVALID_HANDLE_VALUE;
            return Error{ErrorCode::IO_ERROR,
                std::string("MappedSegment: MapViewOfFile failed for '") + path + "'"};
        }
        seg.ptr_ = static_cast<uint8_t*>(ptr);

#else
        // --- POSIX path ---
        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return Error{ErrorCode::IO_ERROR,
                std::string("MappedSegment: open failed for '") + path + "': " + std::strerror(errno)};
        }

#if defined(__APPLE__)
        // Best-effort pre-allocation hint on macOS
        struct fstore fs{};
        fs.fst_flags      = F_ALLOCATECONTIG;
        fs.fst_posmode    = F_PEOFPOSMODE;
        fs.fst_offset     = 0;
        fs.fst_length     = static_cast<off_t>(size);
        // Ignore return — fallback to non-contiguous if it fails
        if (::fcntl(fd, F_PREALLOCATE, &fs) < 0) {
            fs.fst_flags = F_ALLOCATEALL;
            (void)::fcntl(fd, F_PREALLOCATE, &fs);
        }
#endif

        if (::ftruncate(fd, static_cast<off_t>(size)) < 0) {
            ::close(fd);
            return Error{ErrorCode::IO_ERROR,
                std::string("MappedSegment: ftruncate failed for '") + path + "': " + std::strerror(errno)};
        }

        void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            ::close(fd);
            return Error{ErrorCode::IO_ERROR,
                std::string("MappedSegment: mmap failed for '") + path + "': " + std::strerror(errno)};
        }

        // Sequential access hint — best-effort
        ::madvise(ptr, size, MADV_SEQUENTIAL);

        seg.fd_  = fd;
        seg.ptr_ = static_cast<uint8_t*>(ptr);
#endif

        return seg;
    }

    /// Pointer to the start of the mapped memory region.
    [[nodiscard]] uint8_t*           data()     const noexcept { return ptr_; }
    /// Size of the mapped region in bytes.
    [[nodiscard]] size_t             capacity() const noexcept { return size_; }
    /// Check whether the segment is currently mapped and valid.
    [[nodiscard]] bool               is_valid() const noexcept { return ptr_ != nullptr; }
    /// File path of this segment.
    [[nodiscard]] const std::string& path()     const noexcept { return path_; }

    /// Asynchronously flush dirty pages to storage (non-blocking).
    ///
    /// Uses MS_ASYNC on POSIX, FlushViewOfFile (without FlushFileBuffers) on Windows.
    void flush_async() noexcept {
        if (!ptr_) return;
#if defined(_WIN32)
        FlushViewOfFile(ptr_, size_);
        // No FlushFileBuffers — async only
#else
        ::msync(ptr_, size_, MS_ASYNC);
#endif
    }

    /// Synchronously flush dirty pages to storage (blocks until complete).
    ///
    /// Uses MS_SYNC on POSIX, FlushViewOfFile + FlushFileBuffers on Windows.
    void flush_sync() noexcept {
        if (!ptr_) return;
#if defined(_WIN32)
        FlushViewOfFile(ptr_, size_);
        if (hFile_ != INVALID_HANDLE_VALUE)
            FlushFileBuffers(hFile_);
#else
        ::msync(ptr_, size_, MS_SYNC);
#endif
    }

    /// Unmap and close all OS handles. Safe to call multiple times.
    void close() noexcept {
        if (!ptr_) return;
#if defined(_WIN32)
        // STRICT ORDER (Windows Issue #3): Unmap → CloseMap → CloseFile
        UnmapViewOfFile(ptr_);
        ptr_ = nullptr;
        if (hMap_ != nullptr) {
            CloseHandle(hMap_);
            hMap_ = nullptr;
        }
        if (hFile_ != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile_);
            hFile_ = INVALID_HANDLE_VALUE;
        }
#else
        ::munmap(ptr_, size_);
        ptr_ = nullptr;
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
#endif
        size_ = 0;
    }

    /// Pre-fault every 4 KB page in the mapping by touching one byte per page.
    ///
    /// Call from a background thread only -- this can be expensive for large segments.
    void prefault() noexcept {
        if (!ptr_) return;
        volatile uint8_t* p = ptr_;
        for (size_t i = 0; i < size_; i += 4096)
            p[i] = 0;
        std::atomic_thread_fence(std::memory_order_release);
    }

private:
#if defined(_WIN32)
    HANDLE   hFile_ = INVALID_HANDLE_VALUE;
    HANDLE   hMap_  = nullptr;
#else
    int      fd_    = -1;
#endif
    uint8_t* ptr_   = nullptr;
    size_t   size_  = 0;
    std::string path_;
};

// ---------------------------------------------------------------------------
// WalMmapOptions — options for WalMmapWriter::open()
// (defined at namespace scope — Apple Clang default-member-init restriction)
// ---------------------------------------------------------------------------

/// Configuration options for WalMmapWriter::open().
///
/// Defined at namespace scope (not nested in WalMmapWriter) to work around an
/// Apple Clang restriction on default member initializers in nested aggregates.
///
/// @see WalMmapWriter
struct WalMmapOptions {
    std::string dir;                                           ///< Output directory for segment files.
    std::string name_prefix   = "wal_mmap";                    ///< Filename prefix for segment files.
    size_t      ring_segments = 4;                             ///< Number of ring segments (2 to WAL_MMAP_MAX_RING).
    size_t      segment_size  = WAL_MMAP_DEFAULT_SEGMENT;      ///< Size of each segment in bytes (multiple of 65536).
    bool        sync_on_append = false;                        ///< If true, flush_async() after every append (HFT: false).
    bool        sync_on_flush  = false;                        ///< If true, use MS_SYNC on flush() instead of MS_ASYNC.
    bool        use_large_pages = false;                       ///< Enable huge pages (requires OS privileges).
    int64_t     start_seq      = 0;                            ///< Starting sequence number for the first record.
};

// ---------------------------------------------------------------------------
// WalMmapWriter — ring of N memory-mapped WAL segments with bg pre-allocation
// ---------------------------------------------------------------------------

/// High-performance WAL writer using a ring of N memory-mapped segments.
///
/// Achieves sub-microsecond append latency (~38 ns) by writing directly into
/// mmap'd memory. A background thread pre-allocates and pre-faults standby
/// segments so rotation is nearly free.
///
/// **File format**: Identical to WalWriter (WAL_FILE_MAGIC at byte 0, records
/// at byte 16). WalReader works unchanged on WalMmapWriter segment files.
///
/// **Thread safety**: NOT thread-safe for concurrent appends. Single-writer
/// model. flush() and close() must be called from the same thread as append().
///
/// **Crash safety**: CRC is written LAST with a release fence. WalReader stops
/// at bad/missing CRC, recovering all complete records.
///
/// @note Move-constructible. The background thread is stopped and restarted
///       during move. Call segment_paths() BEFORE close() -- paths are
///       unavailable after the writer is closed.
///
/// @see WalMmapOptions, MappedSegment, WalReader
class WalMmapWriter {
public:
    WalMmapWriter(const WalMmapWriter&) = delete;
    WalMmapWriter& operator=(const WalMmapWriter&) = delete;

    WalMmapWriter(WalMmapWriter&& o) {
        // If the source has a running bg thread, stop it before transferring.
        // If bg_deferred_, the thread was never started — skip join.
        if (!o.bg_deferred_) {
            o.bg_stop_.store(true, std::memory_order_release);
            o.bg_cv_.notify_all();
            if (o.bg_thread_.joinable())
                o.bg_thread_.join();
        }

        opts_        = std::move(o.opts_);
        ring_        = std::move(o.ring_);
        active_idx_  = o.active_idx_;
        next_seq_    = o.next_seq_;
        next_seg_id_ = o.next_seg_id_;
        closed_      = o.closed_;
        bg_deferred_ = false;
        bg_stop_.store(false, std::memory_order_release);
        if (!closed_ && !ring_.empty())
            bg_thread_ = std::thread(&WalMmapWriter::bg_worker, this);

        o.closed_    = true;
        o.active_idx_ = 0;
    }

    WalMmapWriter& operator=(WalMmapWriter&& o) {
        if (this != &o) {
            (void)close();
            // Stop source bg thread (unless deferred)
            if (!o.bg_deferred_) {
                o.bg_stop_.store(true, std::memory_order_release);
                o.bg_cv_.notify_all();
                if (o.bg_thread_.joinable())
                    o.bg_thread_.join();
            }

            opts_        = std::move(o.opts_);
            ring_        = std::move(o.ring_);
            active_idx_  = o.active_idx_;
            next_seq_    = o.next_seq_;
            next_seg_id_ = o.next_seg_id_;
            closed_      = o.closed_;
            bg_deferred_ = false;
            bg_stop_.store(false, std::memory_order_release);
            if (!closed_ && !ring_.empty())
                bg_thread_ = std::thread(&WalMmapWriter::bg_worker, this);

            o.closed_    = true;
            o.active_idx_ = 0;
        }
        return *this;
    }

    ~WalMmapWriter() {
        if (!closed_)
            (void)close();
    }

    /// Open a new WalMmapWriter in the given directory.
    ///
    /// Creates the directory if it does not exist. Synchronously allocates and
    /// pre-faults the first segment; the background thread is started after the
    /// returned writer is moved into its final location.
    ///
    /// @param opts  Writer options (directory, segment size, ring count, etc.).
    /// @return A ready-to-append writer, or an Error on validation/IO failure.
    static expected<WalMmapWriter> open(WalMmapOptions opts) {
        // --- Validate options ---
        if (opts.ring_segments < 2 || opts.ring_segments > WAL_MMAP_MAX_RING)
            return Error{ErrorCode::IO_ERROR,
                "WalMmapWriter: ring_segments must be in [2, WAL_MMAP_MAX_RING]"};
        if (opts.segment_size < WAL_MMAP_MIN_SEGMENT ||
            opts.segment_size > WAL_MMAP_MAX_SEGMENT)
            return Error{ErrorCode::IO_ERROR, "WalMmapWriter: invalid segment_size"};
        if (opts.segment_size % WAL_MMAP_MIN_SEGMENT != 0)
            return Error{ErrorCode::IO_ERROR,
                "WalMmapWriter: segment_size must be multiple of 65536"};
        if (opts.dir.empty())
            return Error{ErrorCode::IO_ERROR, "WalMmapWriter: dir must not be empty"};

        // Security: reject path traversal
        // Check for ".." components using filesystem path parsing
        {
            std::filesystem::path p(opts.dir);
            for (const auto& comp : p) {
                if (comp == "..")
                    return Error{ErrorCode::IO_ERROR,
                        "WalMmapWriter: path traversal detected in dir"};
            }
        }
        // Also reject literal ".." substring patterns
        if (opts.dir.find("..") != std::string::npos)
            return Error{ErrorCode::IO_ERROR,
                "WalMmapWriter: path traversal detected in dir"};

        // --- Create directory ---
        {
            std::error_code ec;
            std::filesystem::create_directories(opts.dir, ec);
            if (ec) {
                // If directory already exists that's fine; any other error is fatal
                std::error_code ec2;
                if (!std::filesystem::is_directory(opts.dir, ec2))
                    return Error{ErrorCode::IO_ERROR,
                        std::string("WalMmapWriter: cannot create dir '") +
                        opts.dir + "': " + ec.message()};
            }
        }

        // --- Construct writer ---
        WalMmapWriter w(opts);
        w.next_seq_ = opts.start_seq;

        // Allocate ring slots (heap-allocated to avoid copying atomics)
        for (size_t i = 0; i < opts.ring_segments; ++i)
            w.ring_.push_back(std::make_unique<RingSlot>());

        // Synchronously allocate + prefault slot 0 (writer thread, blocking is fine once)
        uint64_t first_id;
        { std::lock_guard<std::mutex> lk(w.bg_mutex_); first_id = w.next_seg_id_++; }
        auto r0 = w.allocate_slot(*w.ring_[0], first_id, /*skip_prefault=*/false);
        if (!r0) return r0.error();

        w.ring_[0]->first_seq    = static_cast<uint64_t>(w.next_seq_);
        w.ring_[0]->write_offset = 0;
        w.ring_[0]->state.store(SlotState::ACTIVE, std::memory_order_release);
        w.active_idx_ = 0;
        w.init_slot_header(*w.ring_[0]);

        // Background thread is started by the move constructor to avoid a
        // data race: starting the thread here would give it a pointer to the
        // local `w`, which is about to be moved into expected<WalMmapWriter>.
        w.bg_deferred_ = true;

        return w;
    }

    /// Append a record to the WAL. Returns the assigned sequence number.
    ///
    /// NOT thread-safe -- single-writer model. Automatically rotates to the
    /// next segment when the active segment is full. Wakes the background
    /// thread when the active segment is 75% full.
    ///
    /// @param data  Pointer to the record payload.
    /// @param size  Size of the payload in bytes (max 4 GB).
    /// @return The sequence number assigned to this record, or an Error.
    [[nodiscard]] expected<int64_t> append(const uint8_t* data, size_t size) {
        if (closed_)
            return Error{ErrorCode::IO_ERROR, "WalMmapWriter: already closed"};
        if (size > static_cast<size_t>(UINT32_MAX))
            return Error{ErrorCode::IO_ERROR, "WalMmapWriter: record too large (> 4 GB)"};

        const size_t entry_size = 28 + size; // 24-byte hdr + data + 4-byte crc

        if (entry_size > usable())
            return Error{ErrorCode::IO_ERROR,
                "WalMmapWriter: record larger than segment usable space"};

        // Check if active slot has room; rotate if not
        RingSlot* active = ring_[active_idx_].get();
        if (active->write_offset + entry_size > usable()) {
            auto r = rotate();
            if (!r) return r.error();
            active = ring_[active_idx_].get();
        }

        // --- Defensive bounds check ---
        assert(active->seg.data() != nullptr);
        assert(active->seg.data() + detail_mmap::MMAP_WAL_FILE_HDR_SIZE +
               active->write_offset + entry_size
               <= active->seg.data() + active->seg.capacity());

        const int64_t seq = next_seq_++;
        const int64_t ts  = detail_mmap::now_ns();
        const auto    dsz = static_cast<uint32_t>(size);

        uint8_t* dst = active->seg.data()
                     + detail_mmap::MMAP_WAL_FILE_HDR_SIZE
                     + active->write_offset;

        // Write 24-byte record header
        detail_mmap::write_le32(dst,      detail_mmap::MMAP_WAL_RECORD_MAGIC);
        detail_mmap::write_le64(dst + 4,  static_cast<uint64_t>(seq));
        detail_mmap::write_le64(dst + 12, static_cast<uint64_t>(ts));
        detail_mmap::write_le32(dst + 20, dsz);

        // Write payload
        if (size > 0)
            std::memcpy(dst + 24, data, size);

        // Compute CRC over header (24 bytes) + data (contiguous in mmap buffer)
        const uint32_t crc = compute_crc(dst, 24 + size);

        // RELEASE FENCE: ensure header + data bytes are visible before CRC
        // (crash safety on ARM/POWER architectures with weak memory ordering)
        std::atomic_thread_fence(std::memory_order_release);

        // Write CRC LAST — crash safety: WalReader stops at missing/bad CRC
        detail_mmap::write_le32(dst + 24 + size, crc);

        active->write_offset += entry_size;
        active->last_seq      = static_cast<uint64_t>(seq);

        if (opts_.sync_on_append)
            active->seg.flush_async();

        // Wake bg thread early when active segment is 75% full
        if (active->write_offset > usable() * 3 / 4)
            bg_cv_.notify_one();

        return seq;
    }

    /// Append a record from a char buffer.
    /// @param data  Pointer to the record payload.
    /// @param size  Size of the payload in bytes.
    /// @return The assigned sequence number, or an Error.
    [[nodiscard]] expected<int64_t> append(const char* data, size_t size) {
        return append(reinterpret_cast<const uint8_t*>(data), size);
    }

    /// Append a record from a string_view.
    /// @param sv  The record payload.
    /// @return The assigned sequence number, or an Error.
    [[nodiscard]] expected<int64_t> append(std::string_view sv) {
        return append(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
    }

    /// Append a record from a byte vector.
    /// @param v  The record payload.
    /// @return The assigned sequence number, or an Error.
    [[nodiscard]] expected<int64_t> append(const std::vector<uint8_t>& v) {
        return append(v.data(), v.size());
    }

    /// Flush the active segment to storage.
    /// @param do_sync  If true (or opts_.sync_on_flush), use synchronous flush; otherwise async.
    /// @return Success, or an Error if already closed.
    [[nodiscard]] expected<void> flush(bool do_sync = false) {
        if (closed_)
            return Error{ErrorCode::IO_ERROR, "WalMmapWriter: already closed"};
        RingSlot& active = *ring_[active_idx_];
        if (do_sync || opts_.sync_on_flush)
            active.seg.flush_sync();
        else
            active.seg.flush_async();
        return {};
    }

    /// Close the writer: stop the background thread, flush and close all segments.
    /// @return Success (always succeeds after joining the background thread).
    [[nodiscard]] expected<void> close() {
        if (closed_) return {};
        closed_ = true;

        // Stop background thread
        bg_stop_.store(true, std::memory_order_release);
        bg_cv_.notify_all();
        if (bg_thread_.joinable())
            bg_thread_.join();

        // Flush and close all slots
        for (auto& sp : ring_) {
            if (!sp) continue;
            const auto st = sp->state.load(std::memory_order_acquire);
            if (st == SlotState::ACTIVE)
                sp->seg.flush_sync();
            sp->seg.close();
        }
        return {};
    }

    /// Check whether the writer is still open (not yet closed).
    [[nodiscard]] bool            is_open()  const noexcept { return !closed_; }
    /// Return the next sequence number that will be assigned.
    [[nodiscard]] int64_t         next_seq() const noexcept { return next_seq_; }
    /// Return the output directory path.
    [[nodiscard]] const std::string& dir()   const noexcept { return opts_.dir; }

    /// Return paths of all current ring segments plus any older WAL files in the directory.
    ///
    /// @note Call this BEFORE close() -- paths become unavailable after segments are unmapped.
    /// @return Sorted vector of segment file paths.
    [[nodiscard]] std::vector<std::string> segment_paths() const {
        std::vector<std::string> paths;

        // Hold bg_mutex_ while reading ring slots — the bg thread may be
        // writing to file_path in allocate_slot() concurrently.
        {
            std::lock_guard<std::mutex> lk(bg_mutex_);
            for (const auto& sp : ring_) {
                const auto st = sp->state.load(std::memory_order_acquire);
                if (!sp->file_path.empty() &&
                    st != SlotState::FREE && st != SlotState::ALLOCATING)
                    paths.push_back(sp->file_path);
            }
        }

        // Also scan directory for older segment files from previous sessions
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(opts_.dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file()) continue;
            const std::string fname = entry.path().filename().string();
            if (fname.size() < opts_.name_prefix.size() ||
                fname.substr(0, opts_.name_prefix.size()) != opts_.name_prefix)
                continue;
            const std::string fpath = entry.path().string();
            if (std::find(paths.begin(), paths.end(), fpath) == paths.end())
                paths.push_back(fpath);
        }

        std::sort(paths.begin(), paths.end());
        return paths;
    }

private:
    /// Lifecycle states for ring slots.
    enum class SlotState : uint8_t {
        FREE       = 0,   ///< Not allocated; background thread may claim.
        ALLOCATING = 1,   ///< Background thread is creating + mapping the file.
        STANDBY    = 2,   ///< Pre-faulted and ready; background thread finished.
        ACTIVE     = 3,   ///< Writer is appending to this slot.
        DRAINING   = 4    ///< Writer rotated away; background thread will recycle.
    };

    /// Internal ring slot holding a mapped segment and its metadata.
    struct RingSlot {
        MappedSegment          seg;
        std::atomic<size_t>    write_offset{0};   // bytes written past WAL_FILE_HDR_SIZE
        uint64_t               segment_id{0};
        uint64_t               first_seq{0};
        uint64_t               last_seq{0};
        std::string            file_path;
        std::atomic<SlotState> state{SlotState::FREE};

        RingSlot() = default;
        RingSlot(const RingSlot&) = delete;
        RingSlot& operator=(const RingSlot&) = delete;
    };

    explicit WalMmapWriter(WalMmapOptions opts)
        : opts_(std::move(opts)) {}

    /// Bytes available for records in each segment (excludes 16-byte file header).
    size_t usable() const noexcept {
        return opts_.segment_size - static_cast<size_t>(detail_mmap::MMAP_WAL_FILE_HDR_SIZE);
    }

    /// Build the file path for a segment with the given ID.
    std::string make_segment_path(uint64_t segment_id) const {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%010llu", static_cast<unsigned long long>(segment_id));
        return opts_.dir + "/" + opts_.name_prefix + "_" + buf + ".wal";
    }

    /// Write WAL_FILE_MAGIC into the first 16 bytes of the slot's mapping.
    void init_slot_header(RingSlot& slot) noexcept {
        std::memcpy(slot.seg.data(), detail_mmap::MMAP_WAL_FILE_MAGIC, 16);
        std::atomic_thread_fence(std::memory_order_release);
    }

    /// Create + map a new segment file for a slot.
    expected<void> allocate_slot(RingSlot& slot, uint64_t segment_id, bool skip_prefault) {
        slot.segment_id   = segment_id;
        { std::lock_guard<std::mutex> lk(bg_mutex_); slot.file_path = make_segment_path(segment_id); }
        slot.write_offset = 0;
        slot.first_seq    = 0;
        slot.last_seq     = 0;

        auto r = MappedSegment::create(slot.file_path, opts_.segment_size);
        if (!r) {
            slot.state.store(SlotState::FREE, std::memory_order_release);
            return r.error();
        }
        slot.seg = std::move(r.value());

        if (!skip_prefault)
            slot.seg.prefault();

        // Standby segments are initialized as syntactically valid empty WAL files
        // so recovery/compaction can safely parse or skip them deterministically.
        init_slot_header(slot);
        slot.seg.flush_async();

        slot.state.store(SlotState::STANDBY, std::memory_order_release);
        return {};
    }

    /// Rotate the active slot to a standby one (called when active is full).
    expected<void> rotate() {
        RingSlot& old_slot = *ring_[active_idx_];
        old_slot.last_seq  = static_cast<uint64_t>(next_seq_ - 1);

        // Async flush before setting DRAINING so the bg thread doesn't close
        // the segment before the flush is scheduled
        old_slot.seg.flush_async();
        old_slot.state.store(SlotState::DRAINING, std::memory_order_release);

        // Try to find a STANDBY slot pre-allocated by the bg thread
        int standby_idx = find_standby_idx();

        if (standby_idx < 0) {
            // No standby available — fallback: find a FREE slot and allocate synchronously
            // (writer thread, skip prefault to minimize latency)
            for (size_t i = 0; i < ring_.size(); ++i) {
                SlotState expected_s = SlotState::FREE;
                if (ring_[i]->state.compare_exchange_strong(
                        expected_s, SlotState::ALLOCATING,
                        std::memory_order_acq_rel,
                        std::memory_order_relaxed)) {
                    uint64_t new_id;
                    { std::lock_guard<std::mutex> lk(bg_mutex_); new_id = next_seg_id_++; }
                    auto r = allocate_slot(*ring_[i], new_id, /*skip_prefault=*/true);
                    if (!r) return r.error();
                    standby_idx = static_cast<int>(i);
                    break;
                }
            }
        }

        if (standby_idx < 0)
            return Error{ErrorCode::IO_ERROR,
                "WalMmapWriter: ring full — all segments occupied. "
                "Process WAL segments before appending more data."};

        RingSlot& new_slot    = *ring_[static_cast<size_t>(standby_idx)];
        new_slot.first_seq    = static_cast<uint64_t>(next_seq_);
        new_slot.write_offset = 0;
        new_slot.state.store(SlotState::ACTIVE, std::memory_order_release);
        active_idx_ = static_cast<size_t>(standby_idx);
        init_slot_header(new_slot);

        bg_cv_.notify_one();
        return {};
    }

    /// Find the first STANDBY slot index, or -1 if none.
    int find_standby_idx() const noexcept {
        for (size_t i = 0; i < ring_.size(); ++i) {
            if (ring_[i]->state.load(std::memory_order_acquire) == SlotState::STANDBY)
                return static_cast<int>(i);
        }
        return -1;
    }

    /// CRC-32 (polynomial 0xEDB88320) over a contiguous buffer.
    static uint32_t compute_crc(const uint8_t* data, size_t len) noexcept {
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
        for (size_t i = 0; i < len; ++i)
            crc = tbl[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    /// Background worker: pre-allocates STANDBY slots and recycles DRAINING ones.
    void bg_worker() {
        while (true) {
            try {
                {
                    std::unique_lock<std::mutex> lk(bg_mutex_);
                    bg_cv_.wait_for(lk, std::chrono::milliseconds(100), [this]() -> bool {
                        if (bg_stop_.load(std::memory_order_relaxed)) return true;
                        // Wake if any slot needs servicing
                        for (const auto& sp : ring_) {
                            const auto st = sp->state.load(std::memory_order_acquire);
                            if (st == SlotState::DRAINING || st == SlotState::FREE)
                                return true;
                        }
                        // Wake if active slot is getting full
                        if (active_idx_ < ring_.size()) {
                            const RingSlot& a = *ring_[active_idx_];
                            if (a.write_offset > usable() * 3 / 4)
                                return true;
                        }
                        return false;
                    });
                }

                const bool stop = bg_stop_.load(std::memory_order_relaxed);

                // Step 1: Recycle DRAINING slots — close + re-create as fresh segments
                for (auto& sp : ring_) {
                    RingSlot& slot = *sp;
                    if (slot.state.load(std::memory_order_acquire) != SlotState::DRAINING)
                        continue;

                    // Close (unmap) the exhausted segment
                    slot.seg.close();

                    // Allocate a new segment into this slot
                    uint64_t new_id;
                    { std::lock_guard<std::mutex> lk(bg_mutex_); new_id = next_seg_id_++; }

                    // Transition DRAINING → FREE so allocate_slot CAS succeeds
                    slot.state.store(SlotState::FREE, std::memory_order_release);

                    // allocate_slot does its own CAS (FREE → ALLOCATING → STANDBY)
                    // Use a temporary ALLOCATING state here
                    SlotState expected_s = SlotState::FREE;
                    if (slot.state.compare_exchange_strong(
                            expected_s, SlotState::ALLOCATING,
                            std::memory_order_acq_rel,
                            std::memory_order_relaxed)) {
                        (void)allocate_slot(slot, new_id, /*skip_prefault=*/false);
                        // If allocation fails (e.g. disk full), slot stays FREE
                        // and rotate() fallback will handle it
                    }
                }

                // Step 2: Ensure at least 1 STANDBY slot exists
                int standby_count = 0;
                for (const auto& sp : ring_) {
                    if (sp->state.load(std::memory_order_acquire) == SlotState::STANDBY)
                        ++standby_count;
                }
                if (standby_count < 1) {
                    for (auto& sp : ring_) {
                        RingSlot& slot = *sp;
                        SlotState expected_s = SlotState::FREE;
                        if (slot.state.compare_exchange_strong(
                                expected_s, SlotState::ALLOCATING,
                                std::memory_order_acq_rel,
                                std::memory_order_relaxed)) {
                            uint64_t new_id;
                            { std::lock_guard<std::mutex> lk(bg_mutex_); new_id = next_seg_id_++; }
                            (void)allocate_slot(slot, new_id, /*skip_prefault=*/false);
                            break;
                        }
                    }
                }

                if (stop) break;

            } catch (...) {
                // Prevent std::terminate — bg thread must not throw
                if (bg_stop_.load(std::memory_order_relaxed)) break;
            }
        }
    }

    // --- Data members ---
    WalMmapOptions                    opts_;
    std::vector<std::unique_ptr<RingSlot>> ring_;
    size_t                            active_idx_  = 0;
    int64_t                           next_seq_    = 0;
    uint64_t                          next_seg_id_ = 0;  // protected by bg_mutex_
    bool                              closed_      = false;
    bool                              bg_deferred_ = false;  // bg thread start deferred until after move

    std::thread             bg_thread_;
    mutable std::mutex      bg_mutex_;
    std::condition_variable bg_cv_;
    std::atomic<bool>       bg_stop_{false};
};

} // namespace signet::forge
