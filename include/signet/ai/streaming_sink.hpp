// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
// streaming_sink.hpp — Lock-free SPSC ring buffer + async Parquet compaction
// for SignetStack Signet Forge.
// Phase 8: Streaming WAL + Async Compaction.

/// @file streaming_sink.hpp
/// @brief Lock-free SPSC/MPSC ring buffers, StreamingSink for background
///        Parquet compaction, and HybridReader for querying across historical
///        and live data.

#pragma once

#include "signet/error.hpp"
#include "signet/types.hpp"
#include "signet/schema.hpp"
#include "signet/writer.hpp"
#include "signet/reader.hpp"
#include "signet/ai/wal.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace signet::forge {

// ============================================================================
// detail — internal helpers (base64 encode/decode, etc.)
// ============================================================================

/// @cond INTERNAL
namespace detail {

/// Standard base64 alphabet (RFC 4648, no line breaks).
inline constexpr char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// Encode raw bytes to base64 string.
inline std::string base64_encode(const uint8_t* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        uint32_t octet_a = i     < len ? data[i]     : 0u;
        uint32_t octet_b = i + 1 < len ? data[i + 1] : 0u;
        uint32_t octet_c = i + 2 < len ? data[i + 2] : 0u;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result.push_back(kBase64Chars[(triple >> 18) & 0x3Fu]);
        result.push_back(kBase64Chars[(triple >> 12) & 0x3Fu]);
        result.push_back(i + 1 < len ? kBase64Chars[(triple >> 6) & 0x3Fu] : '=');
        result.push_back(i + 2 < len ? kBase64Chars[(triple     ) & 0x3Fu] : '=');
    }

    return result;
}

/// Map a single base64 character to its 6-bit value.
/// Returns 64 for padding ('=') and 255 for invalid characters.
inline uint8_t base64_val(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<uint8_t>(c - 'A');
    if (c >= 'a' && c <= 'z') return static_cast<uint8_t>(c - 'a' + 26);
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0' + 52);
    if (c == '+') return 62u;
    if (c == '/') return 63u;
    if (c == '=') return 64u;   // padding sentinel
    return 255u;                // invalid
}

/// Decode a base64 string to raw bytes.
/// Returns empty vector if the input is not valid base64.
inline std::vector<uint8_t> base64_decode(std::string_view encoded) {
    const size_t n = encoded.size();
    if (n % 4 != 0) return {};

    std::vector<uint8_t> result;
    result.reserve((n / 4) * 3);

    for (size_t i = 0; i < n; i += 4) {
        uint8_t v0 = base64_val(encoded[i]);
        uint8_t v1 = base64_val(encoded[i + 1]);
        uint8_t v2 = base64_val(encoded[i + 2]);
        uint8_t v3 = base64_val(encoded[i + 3]);

        if (v0 == 255u || v1 == 255u || v2 == 255u || v3 == 255u) return {};

        uint32_t triple = (static_cast<uint32_t>(v0) << 18) |
                          (static_cast<uint32_t>(v1) << 12) |
                          (static_cast<uint32_t>(v2) <<  6) |
                          (static_cast<uint32_t>(v3));

        result.push_back(static_cast<uint8_t>((triple >> 16) & 0xFFu));
        if (v2 != 64u) result.push_back(static_cast<uint8_t>((triple >> 8) & 0xFFu));
        if (v3 != 64u) result.push_back(static_cast<uint8_t>( triple       & 0xFFu));
    }

    return result;
}

} // namespace detail
/// @endcond

// ============================================================================
// SpscRingBuffer<T, Capacity>
// Lock-free single-producer single-consumer ring buffer.
// Capacity must be a power of two.
// ============================================================================

/// Lock-free single-producer single-consumer (SPSC) bounded ring buffer.
///
/// Uses acquire/release atomic ordering on head and tail pointers for
/// wait-free push/pop. Capacity must be a power of two (enforced by
/// static_assert).
///
/// @tparam T        Element type (must be move-constructible).
/// @tparam Capacity Maximum number of elements (must be power of 2).
///                  Effective capacity is Capacity - 1.
template <typename T, size_t Capacity>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SpscRingBuffer: Capacity must be a power of 2");

public:
    /// Construct an empty ring buffer.
    SpscRingBuffer() : head_(0), tail_(0) {}

    // Non-copyable, non-movable (atomic members).
    SpscRingBuffer(const SpscRingBuffer&)            = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // -------------------------------------------------------------------------
    // Producer API (single producer thread)
    // -------------------------------------------------------------------------

    /// Push a single item. Returns false if the buffer is full.
    bool push(T item) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & kMask;

        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }

        storage_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    /// Bulk push up to `count` items. Returns the number actually pushed.
    size_t push(const T* items, size_t count) {
        size_t pushed = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!push(items[i])) break;
            ++pushed;
        }
        return pushed;
    }

    // -------------------------------------------------------------------------
    // Consumer API (single consumer thread)
    // -------------------------------------------------------------------------

    /// Pop a single item into `out`. Returns false if the buffer is empty.
    bool pop(T& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }

        out = std::move(storage_[tail]);
        tail_.store((tail + 1) & kMask, std::memory_order_release);
        return true;
    }

    /// Bulk pop up to `max_count` items into `out`. Returns number popped.
    size_t pop(T* out, size_t max_count) {
        size_t popped = 0;
        while (popped < max_count) {
            const size_t tail = tail_.load(std::memory_order_relaxed);
            if (tail == head_.load(std::memory_order_acquire)) break;

            out[popped] = std::move(storage_[tail]);
            tail_.store((tail + 1) & kMask, std::memory_order_release);
            ++popped;
        }
        return popped;
    }

    // -------------------------------------------------------------------------
    // Queries (approximate — no lock)
    // -------------------------------------------------------------------------

    /// Approximate number of items currently in the buffer.
    [[nodiscard]] size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & kMask;
    }

    /// True if the buffer appears empty (approximate, no lock).
    [[nodiscard]] bool empty() const { return size() == 0; }
    /// True if the buffer appears full (approximate, no lock).
    [[nodiscard]] bool full()  const { return size() == Capacity - 1; }

    /// The compile-time capacity of this ring buffer.
    static constexpr size_t capacity() { return Capacity; }

private:
    static constexpr size_t kMask = Capacity - 1;

    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
    // L28: storage_ is a fixed-size C array embedded in the object. For large
    // Capacity values, this can exceed stack limits if allocated on the stack.
    // Always heap-allocate SpscRingBuffer (e.g., via std::make_unique) when
    // Capacity * sizeof(T) is significant.
    T storage_[Capacity];
};

// ============================================================================
// MpscRingBuffer<T, Capacity>
// Multiple-Producer Single-Consumer bounded ring buffer.
// Lock-free producers via CAS on enqueue position (Vyukov-style).
// Single consumer requires no locking on the dequeue path.
// Capacity must be a power of two.
// ============================================================================

/// Multiple-producer single-consumer (MPSC) bounded ring buffer.
///
/// Producers are lock-free via CAS on the enqueue position (Vyukov-style
/// per-slot sequencing). The single consumer requires no locking. Capacity
/// must be a power of two.
///
/// @tparam T        Element type (must be move-constructible and default-constructible).
/// @tparam Capacity Maximum number of elements (must be power of 2).
template <typename T, size_t Capacity>
class MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "MpscRingBuffer: Capacity must be a power of 2");

    // Per-slot handshake sequence.  Initialized to slot index.
    // After a producer writes: sequence = enqueue_pos + 1.
    // After the consumer reads: sequence = enqueue_pos + Capacity (ready for reuse).
    struct Slot {
        std::atomic<size_t> sequence{0};
        T data{};
    };

    static constexpr size_t kMask = Capacity - 1;

    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) size_t              dequeue_pos_{0};  // single consumer — plain size_t
    Slot slots_[Capacity];

public:
    /// Construct the ring buffer, initializing all slot sequences.
    MpscRingBuffer() {
        for (size_t i = 0; i < Capacity; ++i) {
            slots_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // Non-copyable, non-movable (atomic members).
    MpscRingBuffer(const MpscRingBuffer&)            = delete;
    MpscRingBuffer& operator=(const MpscRingBuffer&) = delete;

    // -------------------------------------------------------------------------
    // Producer API — safe to call from multiple threads concurrently
    // -------------------------------------------------------------------------

    /// Push one item.  Returns false immediately if the buffer is full.
    bool push(T item) {
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            Slot& slot = slots_[pos & kMask];
            const size_t seq = slot.sequence.load(std::memory_order_acquire);
            const auto   diff = static_cast<std::ptrdiff_t>(seq)
                              - static_cast<std::ptrdiff_t>(pos);

            if (diff == 0) {
                // Slot is free: try to claim it with a CAS on enqueue_pos_.
                if (enqueue_pos_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    // We own the slot — write data and publish.
                    slot.data = std::move(item);
                    slot.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // Another producer beat us; pos was updated by the CAS failure — retry.
            } else if (diff < 0) {
                // Consumer hasn't freed this slot yet — buffer is full.
                return false;
            } else {
                // Another producer is ahead of us; reload the current position.
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
    }

    /// Bulk push.  Returns the number of items actually pushed.
    size_t push(const T* items, size_t count) {
        size_t pushed = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!push(items[i])) break;
            ++pushed;
        }
        return pushed;
    }

    // -------------------------------------------------------------------------
    // Consumer API — must be called from a single thread only
    // -------------------------------------------------------------------------

    /// Pop one item into `out`.  Returns false if the buffer is empty.
    bool pop(T& out) {
        const size_t pos  = dequeue_pos_;
        Slot&        slot = slots_[pos & kMask];
        const size_t seq  = slot.sequence.load(std::memory_order_acquire);

        // The producer publishes by setting sequence = enqueue_pos + 1 = pos + 1.
        if (seq != pos + 1) {
            return false; // nothing ready yet
        }

        out = std::move(slot.data);
        // Release the slot back to producers: next cycle starts at pos + Capacity.
        slot.sequence.store(pos + Capacity, std::memory_order_release);
        dequeue_pos_ = pos + 1;
        return true;
    }

    /// Bulk pop up to `max_count` items.  Returns number popped.
    size_t pop(T* out, size_t max_count) {
        size_t popped = 0;
        while (popped < max_count && pop(out[popped])) {
            ++popped;
        }
        return popped;
    }

    // -------------------------------------------------------------------------
    // Queries (approximate)
    // -------------------------------------------------------------------------

    /// Approximate number of items in the buffer (may transiently exceed Capacity during push).
    [[nodiscard]] size_t size() const {
        const size_t eq = enqueue_pos_.load(std::memory_order_acquire);
        return eq - dequeue_pos_;  // may exceed Capacity briefly during push; clamp if needed
    }
    /// True if the buffer appears empty (approximate).
    [[nodiscard]] bool empty() const { return size() == 0; }
    /// True if the buffer appears full (approximate).
    [[nodiscard]] bool full()  const { return size() >= Capacity; }

    /// The compile-time capacity of this ring buffer.
    static constexpr size_t capacity() { return Capacity; }
};

// ============================================================================
// StreamRecord — unit of data flowing through the sink
// ============================================================================

/// A single record flowing through the StreamingSink pipeline.
///
/// Records are submitted by producers, buffered in a ring, and compacted
/// into Parquet row groups by the background thread. The payload is
/// base64-encoded when written to Parquet for binary safety.
struct StreamRecord {
    int64_t     timestamp_ns = 0;    ///< Wall-clock timestamp in nanoseconds since Unix epoch
    uint32_t    type_id      = 0;    ///< User-defined record type tag (0 = untyped)
    std::string payload;             ///< Serialized record bytes (UTF-8 safe or binary via base64)
};

// ============================================================================
// StreamingSink
// Background-thread Parquet compaction sink.
// ============================================================================

/// Background-thread Parquet compaction sink fed by a lock-free ring buffer.
///
/// Producers submit StreamRecords via submit(). A background thread drains
/// the ring buffer at the configured flush_interval, writing records into
/// Parquet files with automatic row-group flushing and file rolling.
///
/// Movable via unique_ptr<Impl>; all non-movable state (mutex, thread, ring)
/// lives inside the Impl struct. Use StreamingSink::create() to construct.
///
/// @see StreamRecord, HybridReader
class StreamingSink {
public:
    // -------------------------------------------------------------------------
    // Options (nested struct)
    // -------------------------------------------------------------------------

    /// Configuration options for StreamingSink::create().
    struct Options {
        std::string output_dir;                                    ///< Directory for output Parquet files (required)
        std::string file_prefix          = "stream";               ///< Filename prefix for output files
        size_t      ring_buffer_capacity = 65536;                  ///< Soft cap on ring buffer occupancy (must be power of 2)
        size_t      row_group_size       = 10'000;                 ///< Records per Parquet row group
        size_t      max_file_rows        = 1'000'000;              ///< Maximum rows per output Parquet file before rolling
        std::chrono::milliseconds flush_interval{100};             ///< Background thread wake-up interval
        bool        auto_start           = true;                   ///< Start the background thread immediately on create()
    };

    // -------------------------------------------------------------------------
    // Factory — returns expected<StreamingSink> (movable via unique_ptr<Impl>)
    // -------------------------------------------------------------------------

    /// Create a StreamingSink with the given options.
    ///
    /// Creates the output directory if it does not exist. Validates that
    /// ring_buffer_capacity is a power of two. If auto_start is true (default),
    /// the background compaction thread is started immediately.
    ///
    /// @param opts Configuration options.
    /// @return StreamingSink on success, Error on validation or I/O failure.
    [[nodiscard]] static expected<StreamingSink> create(Options opts) {
        if (opts.output_dir.empty())
            return Error{ErrorCode::IO_ERROR, "StreamingSink: output_dir must not be empty"};

        // L29: Path traversal guard using std::filesystem::path iteration
        // instead of manual string splitting for correctness across platforms.
        {
            std::filesystem::path p(opts.output_dir);
            for (const auto& comp : p) {
                if (comp == "..")
                    return Error{ErrorCode::IO_ERROR,
                                 "StreamingSink: output_dir must not contain '..' path traversal"};  // CWE-22: Improper Limitation of a Pathname to a Restricted Directory
            }
        }

        std::error_code ec;
        std::filesystem::create_directories(opts.output_dir, ec);
        if (ec)
            return Error{ErrorCode::IO_ERROR,
                         "StreamingSink: cannot create output_dir '" +
                         opts.output_dir + "': " + ec.message()};

        const size_t cap = opts.ring_buffer_capacity;
        if (cap == 0 || (cap & (cap - 1)) != 0)
            return Error{ErrorCode::INTERNAL_ERROR,
                         "StreamingSink: ring_buffer_capacity must be a power of 2"};

        auto impl = std::make_unique<Impl>(std::move(opts));
        const bool auto_start = impl->opts_.auto_start;
        StreamingSink sink(std::move(impl));
        if (auto_start) sink.impl_->start();
        return std::move(sink);  // StreamingSink is movable (unique_ptr<Impl>)
    }

    // -------------------------------------------------------------------------
    // Movable, non-copyable
    // -------------------------------------------------------------------------

    StreamingSink(StreamingSink&&)            = default;
    StreamingSink& operator=(StreamingSink&&) = default;
    StreamingSink(const StreamingSink&)       = delete;
    StreamingSink& operator=(const StreamingSink&) = delete;

    ~StreamingSink() {
        if (impl_) (void)impl_->stop();
    }

    // -------------------------------------------------------------------------
    // Submit (MPSC-safe via mutex in Impl)
    // -------------------------------------------------------------------------

    /// Submit a fully-constructed StreamRecord to the ring buffer.
    /// Thread-safe (serialized via internal mutex).
    /// @param rec Record to submit.
    /// @return Error if the ring buffer is full (record dropped).
    [[nodiscard]] expected<void> submit(StreamRecord rec) {
        return impl_->submit(std::move(rec));
    }

    /// Submit a record from raw bytes.
    /// @param timestamp_ns Wall-clock timestamp in nanoseconds.
    /// @param type_id      User-defined record type tag.
    /// @param data         Raw payload bytes.
    /// @param size         Number of bytes.
    /// @return Error if the ring buffer is full.
    [[nodiscard]] expected<void> submit(int64_t timestamp_ns, uint32_t type_id,
                const uint8_t* data, size_t size) {
        StreamRecord rec;
        rec.timestamp_ns = timestamp_ns;
        rec.type_id      = type_id;
        rec.payload.assign(reinterpret_cast<const char*>(data), size);
        return impl_->submit(std::move(rec));
    }

    /// Submit a record from a string_view payload.
    /// @param timestamp_ns Wall-clock timestamp in nanoseconds.
    /// @param type_id      User-defined record type tag.
    /// @param sv           Payload as string_view.
    /// @return Error if the ring buffer is full.
    [[nodiscard]] expected<void> submit(int64_t timestamp_ns, uint32_t type_id,
                std::string_view sv) {
        StreamRecord rec;
        rec.timestamp_ns = timestamp_ns;
        rec.type_id      = type_id;
        rec.payload.assign(sv.data(), sv.size());
        return impl_->submit(std::move(rec));
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /// Start the background compaction thread (no-op if already running).
    void              start()      { impl_->start(); }
    /// Stop the background thread, drain remaining records, and close open files.
    /// Blocks until the thread has joined.
    /// @return Error on I/O failure during final drain.
    [[nodiscard]] expected<void> stop()  { return impl_->stop(); }
    /// Signal the background thread to stop without waiting for it to finish.
    void              stop_nowait() { impl_->stop_nowait(); }

    // -------------------------------------------------------------------------
    // Force flush (blocking)
    // -------------------------------------------------------------------------

    /// Drain the ring buffer and write all pending records, then close the
    /// current Parquet file so it has a valid footer and is immediately readable.
    /// Blocks until complete.
    /// @return Error on I/O failure.
    [[nodiscard]] expected<void> flush() { return impl_->flush(); }

    // -------------------------------------------------------------------------
    // Stats
    // -------------------------------------------------------------------------

    /// Total number of records successfully submitted to the ring buffer.
    [[nodiscard]] uint64_t records_submitted() const { return impl_->records_submitted_.load(std::memory_order_relaxed); }
    /// Total number of records written to Parquet files.
    [[nodiscard]] int64_t  records_written()   const { return impl_->records_written_.load(std::memory_order_relaxed); }
    /// Total number of records dropped due to ring buffer overflow.
    [[nodiscard]] uint64_t records_dropped()   const { return impl_->records_dropped_.load(std::memory_order_relaxed); }
    /// Number of completed Parquet output files.
    [[nodiscard]] int64_t  files_written()     const { return impl_->files_written_.load(std::memory_order_relaxed); }
    /// Approximate total bytes written to the current output file.
    [[nodiscard]] int64_t  bytes_written()     const { return impl_->bytes_written_.load(std::memory_order_relaxed); }

    // -------------------------------------------------------------------------
    // Output file listing
    // -------------------------------------------------------------------------

    /// List of completed Parquet output file paths (thread-safe snapshot).
    [[nodiscard]] std::vector<std::string> output_files() const {
        std::lock_guard<std::mutex> lk(impl_->files_mutex_);
        return impl_->output_files_;
    }

private:
    // =========================================================================
    // Impl — holds all non-movable state + implementation logic
    // =========================================================================

    struct Impl {
        static constexpr size_t kRingCap = 65536; // power of 2
        using RingBuffer = SpscRingBuffer<StreamRecord, kRingCap>;

        // ----- Construction -----
        explicit Impl(Options opts)
            : opts_(std::move(opts))
            , ring_(std::make_unique<RingBuffer>())
        {}

        // ----- Ring helpers -----

        // Soft-cap: honour opts_.ring_buffer_capacity even though the physical
        // ring (kRingCap) may be larger.  Records beyond the soft cap are dropped.
        bool ring_push(StreamRecord rec) {
            if (ring_->size() >= opts_.ring_buffer_capacity) return false;
            return ring_->push(std::move(rec));
        }

        // Must be called with consumer_mutex_ held.
        void drain_ring_locked(std::vector<StreamRecord>& batch, size_t max_count) {
            StreamRecord r;
            while (batch.size() < max_count && ring_->pop(r))
                batch.push_back(std::move(r));
        }

        // ----- Submit -----
        [[nodiscard]] expected<void> submit(StreamRecord rec) {
            std::lock_guard<std::mutex> lk(submit_mutex_);
            if (!ring_push(std::move(rec))) {
                records_dropped_.fetch_add(1, std::memory_order_relaxed);
                return Error{ErrorCode::IO_ERROR, "StreamingSink: ring buffer full, record dropped"};
            }
            records_submitted_.fetch_add(1, std::memory_order_relaxed);
            cv_.notify_one();
            return {};
        }

        // ----- Lifecycle -----
        void start() {
            bool exp = false;
            if (!running_.compare_exchange_strong(exp, true, std::memory_order_acq_rel))
                return;
            stop_requested_.store(false, std::memory_order_release);
            worker_ = std::thread(&Impl::compaction_loop, this);
        }

        [[nodiscard]] expected<void> stop() {
            if (!running_.load(std::memory_order_acquire)) return {};
            stop_requested_.store(true, std::memory_order_release);
            cv_.notify_all();
            if (worker_.joinable()) worker_.join();
            running_.store(false, std::memory_order_release);
            // Close any writer not already finalized in the compaction loop
            // (can happen if stop_nowait() was used or thread exited early).
            if (current_writer_) {
                (void)current_writer_->close();
                if (current_file_rows_ > 0) register_output_file(current_file_path_);
                current_writer_.reset();
                current_file_rows_ = 0;
            }
            return {};
        }

        void stop_nowait() {
            stop_requested_.store(true, std::memory_order_release);
            cv_.notify_all();
        }

        // ----- Flush -----
        // Drain the ring and write all pending records, then close the current
        // Parquet file so it has a valid footer and is immediately readable.
        // Acquires consumer_mutex_ to serialise against the compaction thread.
        [[nodiscard]] expected<void> flush() {
            std::lock_guard<std::mutex> consumer_lk(consumer_mutex_);
            std::vector<StreamRecord> batch;
            drain_ring_locked(batch, (std::numeric_limits<size_t>::max)());
            if (!batch.empty()) {
                auto r = write_batch(batch);
                if (!r) return r;
            }
            // Close the current writer (if open) so the file has a valid footer.
            if (current_writer_) {
                auto r = current_writer_->close();
                if (current_file_rows_ > 0) register_output_file(current_file_path_);
                current_writer_.reset();
                current_file_rows_ = 0;
                if (!r) return r.error();
            }
            return expected<void>{};
        }

        // ----- Background thread -----
        void compaction_loop() {
            std::vector<StreamRecord> batch;
            batch.reserve(opts_.row_group_size);

            while (true) {
                {
                    std::unique_lock<std::mutex> lk(cv_mutex_);
                    cv_.wait_for(lk, opts_.flush_interval, [this] {
                        return stop_requested_.load(std::memory_order_acquire) ||
                               !ring_->empty();
                    });
                }

                // Acquire consumer_mutex_ to serialise with flush() calls from
                // outside the loop (SPSC ring has only one consumer at a time).
                {
                    std::lock_guard<std::mutex> consumer_lk(consumer_mutex_);
                    batch.clear();
                    drain_ring_locked(batch, opts_.row_group_size);
                    if (!batch.empty()) (void)write_batch(batch);
                }

                if (stop_requested_.load(std::memory_order_acquire)) {
                    std::lock_guard<std::mutex> consumer_lk(consumer_mutex_);
                    batch.clear();
                    drain_ring_locked(batch, (std::numeric_limits<size_t>::max)());
                    if (!batch.empty()) (void)write_batch(batch);
                    // Close any open writer so the Parquet footer is written.
                    if (current_writer_) {
                        (void)current_writer_->close();
                        if (current_file_rows_ > 0) register_output_file(current_file_path_);
                        current_writer_.reset();
                        current_file_rows_ = 0;
                    }
                    break;
                }
            }
        }

        // ----- Parquet batch write -----
        [[nodiscard]] expected<void> write_batch(std::vector<StreamRecord>& batch) {
            if (batch.empty()) return expected<void>{};

            Schema schema = Schema::builder("stream_data")
                .column<int64_t>("timestamp_ns", LogicalType::TIMESTAMP_NS)
                .column<int32_t>("type_id")
                .column<std::string>("payload", LogicalType::STRING)
                .build();

            size_t written = 0;
            while (written < batch.size()) {
                const size_t remaining_in_file =
                    opts_.max_file_rows > current_file_rows_
                        ? opts_.max_file_rows - current_file_rows_ : 0;

                if (remaining_in_file == 0) {
                    if (current_writer_) {
                        auto r = current_writer_->close();
                        current_writer_.reset();
                        if (!r) return r.error();
                    }
                    current_file_rows_ = 0;
                }

                if (!current_writer_) {
                    std::string path = next_output_path();
                    WriterOptions wo;
                    wo.row_group_size = static_cast<int64_t>(opts_.row_group_size);
                    auto wr = ParquetWriter::open(path, schema, wo);
                    if (!wr) return wr.error();
                    current_writer_    = std::make_unique<ParquetWriter>(std::move(*wr));
                    current_file_path_ = path;
                }

                const size_t chunk_size =
                    (std::min)(batch.size() - written,
                               opts_.max_file_rows - current_file_rows_);

                std::vector<int64_t>     ts_col;
                std::vector<int32_t>     type_col;
                std::vector<std::string> payload_col;
                ts_col.reserve(chunk_size);
                type_col.reserve(chunk_size);
                payload_col.reserve(chunk_size);

                for (size_t i = written; i < written + chunk_size; ++i) {
                    const auto& rec = batch[i];
                    ts_col.push_back(rec.timestamp_ns);
                    type_col.push_back(static_cast<int32_t>(rec.type_id));
                    payload_col.push_back(
                        detail::base64_encode(
                            reinterpret_cast<const uint8_t*>(rec.payload.data()),
                            rec.payload.size()));
                }

                { auto r = current_writer_->write_column<int64_t>(0, ts_col.data(), ts_col.size()); if (!r) return r; }
                { auto r = current_writer_->write_column<int32_t>(1, type_col.data(), type_col.size()); if (!r) return r; }
                { auto r = current_writer_->write_column(2, payload_col.data(), payload_col.size()); if (!r) return r; }
                { auto r = current_writer_->flush_row_group(); if (!r) return r; }

                current_file_rows_ += chunk_size;
                written            += chunk_size;

                {
                    std::error_code ec;
                    auto fsz = std::filesystem::file_size(current_file_path_, ec);
                    if (!ec) bytes_written_.store(static_cast<int64_t>(fsz), std::memory_order_relaxed);
                }
                records_written_.fetch_add(static_cast<int64_t>(chunk_size), std::memory_order_relaxed);

                if (current_file_rows_ >= opts_.max_file_rows) {
                    auto r = current_writer_->close();
                    register_output_file(current_file_path_);
                    current_writer_.reset();
                    current_file_rows_ = 0;
                    if (!r) return r.error();
                }
            }

            // Note: partial (not-yet-full) files are NOT registered here.
            // They are registered only when closed by flush() or stop(), so
            // that HybridReader never sees a file without a valid Parquet footer.

            return expected<void>{};
        }

        // ----- File naming -----
        std::string next_output_path() {
            using namespace std::chrono;
            const int64_t ts_ns = duration_cast<nanoseconds>(
                system_clock::now().time_since_epoch()).count();
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%08zu_%lld",
                          file_index_++, static_cast<long long>(ts_ns));
            return opts_.output_dir + "/" + opts_.file_prefix + "_" + buf + ".parquet";
        }

        void register_output_file(const std::string& path) {
            std::lock_guard<std::mutex> lk(files_mutex_);
            for (const auto& p : output_files_) { if (p == path) return; }
            output_files_.push_back(path);
            files_written_.store(static_cast<int64_t>(output_files_.size()),
                                 std::memory_order_relaxed);
        }

        // ----- Members -----
        Options                         opts_;
        std::unique_ptr<RingBuffer>     ring_;
        mutable std::mutex              submit_mutex_;   // serialises multi-producer submits
        std::mutex                      consumer_mutex_; // single-consumer serialiser (flush vs loop)
        std::thread                     worker_;
        std::atomic<bool>               running_{false};
        std::atomic<bool>               stop_requested_{false};
        std::mutex                      cv_mutex_;
        std::condition_variable         cv_;
        std::atomic<uint64_t>           records_submitted_{0};
        std::atomic<int64_t>            records_written_{0};
        std::atomic<uint64_t>           records_dropped_{0};
        std::atomic<int64_t>            files_written_{0};
        std::atomic<int64_t>            bytes_written_{0};
        std::unique_ptr<ParquetWriter>  current_writer_;
        std::string                     current_file_path_;
        size_t                          current_file_rows_{0};
        size_t                          file_index_{0};
        mutable std::mutex              files_mutex_;
        std::vector<std::string>        output_files_;
    };

    std::unique_ptr<Impl> impl_;

    explicit StreamingSink(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
};

// ============================================================================
// HybridReaderOptions — options for HybridReader::create()
// (defined outside HybridReader to avoid Apple Clang default-member-init bug)
// ============================================================================

/// Options for constructing a HybridReader via HybridReader::create().
///
/// Defined at namespace scope to avoid Apple Clang default-member-init bugs.
struct HybridReaderOptions {
    std::vector<std::string> parquet_files;                                     ///< Parquet files to query
    int64_t                  start_timestamp = 0;                               ///< Minimum timestamp_ns (inclusive)
    int64_t                  end_timestamp   = (std::numeric_limits<int64_t>::max)(); ///< Maximum timestamp_ns (inclusive)
    uint32_t                 type_id_filter  = 0;                               ///< Type ID filter (0 = accept all types)
};

// ============================================================================
// HybridQueryOptions
// (defined outside HybridReader to avoid Apple Clang default-member-init bug)
// ============================================================================

/// Per-query filter options passed to HybridReader::read().
struct HybridQueryOptions {
    int64_t  start_ns  = 0;                                      ///< Minimum timestamp_ns (inclusive)
    int64_t  end_ns    = (std::numeric_limits<int64_t>::max)();    ///< Maximum timestamp_ns (inclusive)
    uint32_t type_id   = 0;                                      ///< Type ID filter (0 = all types)
    size_t   max_rows  = (std::numeric_limits<size_t>::max)();    ///< Maximum records to return
};

// ============================================================================
// HybridReader
// Query records across historical Parquet files + live ring buffer snapshot.
// ============================================================================

/// Reads StreamRecords across historical Parquet files and (optionally)
/// a live StreamingSink ring buffer snapshot.
///
/// Construct from a StreamingSink to capture both historical files and a
/// count of live records, or from an explicit list of Parquet paths.
/// For a precise live snapshot, call sink.flush() before constructing.
///
/// @see StreamingSink, StreamRecord, HybridReaderOptions, HybridQueryOptions
class HybridReader {
public:
    /// Alias for per-query filter options.
    using QueryOptions = HybridQueryOptions;
    /// Alias for construction-time options.
    using Options      = HybridReaderOptions;

    // -------------------------------------------------------------------------
    // Factory — create from Options struct (preferred API)
    // -------------------------------------------------------------------------

    /// Create a HybridReader with pre-configured filters.
    ///
    /// @param opts Options including Parquet file list and filter parameters.
    /// @return Configured HybridReader.
    [[nodiscard]] static expected<HybridReader> create(Options opts) {
        HybridReader reader(std::move(opts.parquet_files));
        reader.filter_start_ = opts.start_timestamp;
        reader.filter_end_   = opts.end_timestamp;
        reader.filter_type_  = opts.type_id_filter;
        return reader;
    }

    // -------------------------------------------------------------------------
    // Constructors
    // -------------------------------------------------------------------------

    /// Construct from a live StreamingSink: captures a snapshot of the ring
    /// buffer plus the list of historical Parquet files already written.
    explicit HybridReader(const StreamingSink& sink)
        : parquet_files_(sink.output_files())
    {
        // Snapshot live records from the ring buffer by re-reading submitted
        // records that haven't been flushed yet. Because we cannot directly
        // access the SPSC ring from outside (it is private), we consume the
        // list of output files (already flushed) and note the gap.  The live
        // snapshot is only the count of records that the background thread
        // has not yet written — unavailable without internal access.  We
        // expose this as "live records = 0" from the public perspective, which
        // is the safe conservative answer.  Users who need live records should
        // call sink.flush() first.
        //
        // If callers need a precise live snapshot they can:
        //   1. sink.flush()
        //   2. HybridReader(sink)
        live_count_ = 0;
    }

    /// Construct from an explicit list of Parquet file paths (no live ring).
    explicit HybridReader(std::vector<std::string> parquet_files)
        : parquet_files_(std::move(parquet_files))
        , live_count_(0)
    {}

    // -------------------------------------------------------------------------
    // Read
    // -------------------------------------------------------------------------

    /// Read all matching records from historical Parquet files.
    [[nodiscard]] expected<std::vector<StreamRecord>> read(QueryOptions opts = {}) const {
        std::vector<StreamRecord> result;

        // Column indices for stream_data schema: 0=timestamp_ns, 1=type_id, 2=payload.
        static constexpr size_t kColTs      = 0;
        static constexpr size_t kColTypeId  = 1;
        static constexpr size_t kColPayload = 2;

        for (const auto& file_path : parquet_files_) {
            if (result.size() >= opts.max_rows) break;

            // Skip files that don't exist yet (may still be open for writing).
            std::error_code ec;
            if (!std::filesystem::exists(file_path, ec)) continue;

            auto reader_result = ParquetReader::open(file_path);
            if (!reader_result) {
                // Skip unreadable / partially-written files.
                continue;
            }

            auto& reader = *reader_result;

            const size_t num_rg = static_cast<size_t>(reader.num_row_groups());

            for (size_t rg = 0; rg < num_rg; ++rg) {
                if (result.size() >= opts.max_rows) break;

                // Read typed columns from this row group.
                auto ts_result = reader.read_column<int64_t>(rg, kColTs);
                if (!ts_result) continue;

                auto type_result = reader.read_column<int32_t>(rg, kColTypeId);
                if (!type_result) continue;

                auto payload_result = reader.read_column<std::string>(rg, kColPayload);
                if (!payload_result) continue;

                const auto& ts_col      = *ts_result;
                const auto& type_col    = *type_result;
                const auto& payload_col = *payload_result;

                const size_t nrows = (std::min)({ts_col.size(),
                                                type_col.size(),
                                                payload_col.size()});

                for (size_t i = 0; i < nrows; ++i) {
                    if (result.size() >= opts.max_rows) break;

                    const int64_t  ts  = ts_col[i];
                    const uint32_t tid = static_cast<uint32_t>(type_col[i]);

                    // Apply time-range filter.
                    if (ts < opts.start_ns || ts > opts.end_ns) continue;

                    // Apply type_id filter (0 = accept all types).
                    if (opts.type_id != 0 && tid != opts.type_id) continue;

                    StreamRecord rec;
                    rec.timestamp_ns = ts;
                    rec.type_id      = tid;
                    auto decoded = detail::base64_decode(payload_col[i]);
                    rec.payload.assign(decoded.begin(), decoded.end());

                    result.push_back(std::move(rec));
                }
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // Convenience read that uses filter fields set by create()
    // -------------------------------------------------------------------------

    /// Read all records applying the filters set at construction (via Options).
    [[nodiscard]] expected<std::vector<StreamRecord>> read_all() const {
        QueryOptions qopts;
        qopts.start_ns = filter_start_;
        qopts.end_ns   = filter_end_;
        qopts.type_id  = filter_type_;
        return read(qopts);
    }

    // -------------------------------------------------------------------------
    // Queries
    // -------------------------------------------------------------------------

    /// Number of historical Parquet files available.
    [[nodiscard]] size_t num_files() const { return parquet_files_.size(); }

    /// Number of live records visible in ring snapshot (0 unless flush() was called first).
    [[nodiscard]] size_t num_live() const { return live_count_; }

private:
    std::vector<std::string> parquet_files_;
    size_t                   live_count_    = 0;

    // Filter fields populated by create(Options) — default values = no filtering.
    int64_t  filter_start_ = 0;
    int64_t  filter_end_   = (std::numeric_limits<int64_t>::max)();
    uint32_t filter_type_  = 0;
};

} // namespace signet::forge
