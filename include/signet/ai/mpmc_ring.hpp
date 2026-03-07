// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
/// @file mpmc_ring.hpp
/// @brief Lock-free bounded MPMC ring buffer based on Dmitry Vyukov's algorithm.
//
// O(1) amortized push/pop, no allocation after construction, ABA-safe via
// per-slot sequence numbers.  Runtime capacity (always rounded up to next
// power of two, minimum 2).
//
// Phase 9b: MPMC ColumnBatch Event Bus.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace signet::forge {

// ============================================================================
// MpmcRing<T> — Vyukov bounded MPMC queue
// ============================================================================

/// Lock-free bounded multi-producer multi-consumer ring buffer.
///
/// Based on Dmitry Vyukov's bounded MPMC queue algorithm (public domain).
/// Multiple producers and consumers call push()/pop() concurrently without
/// external locking. O(1) amortized push/pop, no allocation after construction,
/// ABA-safe via per-slot sequence numbers.
///
/// Each slot carries a sequence number encoding its lifecycle phase:
///   - `seq == pos`      : slot is empty; a producer at `pos` may claim it.
///   - `seq == pos + 1`  : slot is full; a consumer at `pos` may drain it.
///   - `seq == pos + cap`: slot has been drained and is ready for the next cycle.
///
/// @tparam T  Element type. Must be move-assignable.
///
/// @note Non-copyable and non-movable (contains atomics).
/// @note Runtime capacity is always rounded up to the next power of two (minimum 2).
///
/// @see EventBus
template<typename T>
class MpmcRing {
public:
    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /// Construct a ring buffer with the given capacity.
    ///
    /// The actual capacity is rounded up to the next power of two (minimum 2).
    /// Power-of-two capacity is required for correct index masking (pos & mask_).
    ///
    /// @param capacity  Requested capacity; will be rounded up to a power of two.
    ///                  Must be >= 1.
    /// @throws std::invalid_argument if capacity is 0.
    ///
    /// @note Wraparound safety: the enqueue_/dequeue_ cursors are size_t (64-bit
    ///       on modern platforms). At 1 GHz sustained push rate, UINT64_MAX wraps
    ///       after ~584 years — a theoretical-only concern. The masking arithmetic
    ///       (pos & mask_) remains correct across the wraparound boundary because
    ///       capacity is always a power of two.
    explicit MpmcRing(size_t capacity) {
        if (capacity == 0) {
            throw std::invalid_argument("MpmcRing capacity must be >= 1");
        }
        size_t cap = 2;
        while (cap < capacity) cap <<= 1;
        // Invariant: cap is a power of two (enforced by construction above)
        capacity_ = cap;
        mask_     = cap - 1;

        slots_ = std::make_unique<Slot[]>(cap);
        for (size_t i = 0; i < cap; ++i)
            slots_[i].seq.store(i, std::memory_order_relaxed);

        enqueue_.store(0, std::memory_order_relaxed);
        dequeue_.store(0, std::memory_order_relaxed);
    }

    MpmcRing(const MpmcRing&)            = delete;
    MpmcRing& operator=(const MpmcRing&) = delete;
    MpmcRing(MpmcRing&&)                 = delete;
    MpmcRing& operator=(MpmcRing&&)      = delete;

    // -------------------------------------------------------------------------
    // push — non-blocking; returns false if ring is full
    // -------------------------------------------------------------------------

    /// Push an element into the ring (non-blocking).
    ///
    /// @param val  The element to enqueue (moved into the ring).
    /// @return true if the element was enqueued; false if the ring is full.
    [[nodiscard]] bool push(T val) noexcept(std::is_nothrow_move_assignable_v<T>) {
        size_t pos = enqueue_.load(std::memory_order_relaxed);
        for (;;) {
            Slot&        slot = slots_[pos & mask_];
            const size_t seq  = slot.seq.load(std::memory_order_acquire);
            const auto   diff = static_cast<intptr_t>(seq)
                              - static_cast<intptr_t>(pos);

            if (diff == 0) {
                // Slot free — try to claim it.
                if (enqueue_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    slot.data = std::move(val);
                    slot.seq.store(pos + 1, std::memory_order_release);
                    return true;
                }
                // Another producer won the CAS; reload and retry.
            } else if (diff < 0) {
                return false;  // ring is full
            } else {
                pos = enqueue_.load(std::memory_order_relaxed);
            }
        }
    }

    // -------------------------------------------------------------------------
    // pop — non-blocking; returns false if ring is empty
    // -------------------------------------------------------------------------

    /// Pop an element from the ring (non-blocking).
    ///
    /// @param out  Receives the dequeued element on success (via move assignment).
    /// @return true if an element was dequeued; false if the ring is empty.
    [[nodiscard]] bool pop(T& out) noexcept(std::is_nothrow_move_assignable_v<T>) {
        size_t pos = dequeue_.load(std::memory_order_relaxed);
        for (;;) {
            Slot&        slot = slots_[pos & mask_];
            const size_t seq  = slot.seq.load(std::memory_order_acquire);
            const auto   diff = static_cast<intptr_t>(seq)
                              - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                // Slot full — try to claim it.
                if (dequeue_.compare_exchange_weak(
                        pos, pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) {
                    out = std::move(slot.data);
                    // Mark slot free for the NEXT full cycle.
                    slot.seq.store(pos + mask_ + 1, std::memory_order_release);
                    return true;
                }
                // Another consumer won the CAS; reload and retry.
            } else if (diff < 0) {
                return false;  // ring is empty
            } else {
                pos = dequeue_.load(std::memory_order_relaxed);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Metadata
    // -------------------------------------------------------------------------

    /// Return the actual ring capacity (always a power of two).
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    /// Approximate occupancy (not linearizable without external synchronization).
    /// @return An estimate of the number of elements currently in the ring.
    [[nodiscard]] size_t size() const noexcept {
        const size_t head = enqueue_.load(std::memory_order_relaxed);
        const size_t tail = dequeue_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0u;
    }

    /// Check whether the ring appears empty (approximate).
    [[nodiscard]] bool empty() const noexcept { return size() == 0u; }

private:
    /// Per-slot storage: sequence number + data.
    struct Slot {
        std::atomic<size_t> seq{0};
        T                   data{};
    };

    // Separate cache lines for producer and consumer cursors.
    static constexpr size_t kCacheLine = 64;

    alignas(kCacheLine) std::atomic<size_t> enqueue_{0};
    alignas(kCacheLine) std::atomic<size_t> dequeue_{0};

    std::unique_ptr<Slot[]> slots_;
    size_t                  capacity_{0};
    size_t                  mask_{0};
};

} // namespace signet::forge
