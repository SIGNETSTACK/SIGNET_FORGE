// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
/// @file event_bus.hpp
/// @brief Multi-tier event bus for routing SharedColumnBatch events through three tiers.
//
// Routes SharedColumnBatch events through three tiers:
//
//   Tier 1 — SPSC dedicated channels (sub-μs, one per producer-consumer pair)
//             MpmcRing used as SPSC when only one writer; no locking overhead.
//             Use: exchange adapter → specific strategy engine.
//
//   Tier 2 — MPMC shared pool (μs, N producers → M worker threads)
//             All publish() calls land in a single MpmcRing shared by all
//             consumers.  Workers call pop() in a tight loop.
//             Use: all exchanges → ML inference pool (load-balanced).
//
//   Tier 3 — WAL / StreamingSink (ms, async durable logging)
//             Every publish() optionally serialises the batch and submits a
//             StreamRecord to an attached StreamingSink.  Non-blocking: drops
//             if the sink ring is full (increments tier3_drops counter).
//             Use: compliance log, Parquet compaction.
//
// Usage:
//   EventBus bus;
//
//   // Tier 1: dedicated channel
//   auto ch = bus.make_channel("binance→risk", 256);
//   ch->push(batch);        // producer (Binance adapter thread)
//   ch->pop(batch);         // consumer (risk gate thread)
//
//   // Tier 2: shared pool
//   bus.publish(batch);     // any producer
//   bus.pop(batch);         // any worker thread
//
//   // Tier 3: attach sink for compliance logging
//   bus.attach_sink(&my_streaming_sink);
//
// Phase 9b: MPMC ColumnBatch Event Bus.

#pragma once

#include "signet/error.hpp"
#include "signet/ai/mpmc_ring.hpp"
#include "signet/ai/column_batch.hpp"
#include "signet/ai/streaming_sink.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace signet::forge {

// ============================================================================
// EventBusOptions — defined outside EventBus for Apple Clang compat
// ============================================================================

/// Configuration options for EventBus.
///
/// Defined at namespace scope (not nested in EventBus) to work around an
/// Apple Clang restriction on default member initializers in nested aggregates.
///
/// @see EventBus
struct EventBusOptions {
    size_t tier2_capacity = 4096;   ///< Tier-2 MPMC ring capacity (power-of-2)
    size_t tier1_capacity = 256;    ///< Default capacity for make_channel()
    bool   enable_tier3   = false;  ///< Route batches to attached StreamingSink
};

// ============================================================================
// EventBus — multi-tier SharedColumnBatch router
// ============================================================================

/// Multi-tier event bus for routing SharedColumnBatch events.
///
/// Routes batches through three tiers:
///   - **Tier 1** -- SPSC dedicated channels (sub-us, one per producer-consumer pair).
///   - **Tier 2** -- MPMC shared pool (us, N producers to M worker threads).
///   - **Tier 3** -- WAL / StreamingSink (ms, async durable logging).
///
/// The bus is non-copyable and non-movable due to its internal mutex.
/// Wrap in `std::unique_ptr<EventBus>` if heap allocation is needed.
///
/// @see EventBusOptions, MpmcRing, StreamingSink
class EventBus {
public:
    /// Alias for the options struct.
    using Options = EventBusOptions;

    // -------------------------------------------------------------------------
    // Channel — Tier-1 dedicated MpmcRing used as SPSC
    // -------------------------------------------------------------------------

    /// Tier-1 dedicated channel type (MpmcRing used as SPSC when single-writer).
    using Channel = MpmcRing<SharedColumnBatch>;

    // -------------------------------------------------------------------------
    // Construction
    // -------------------------------------------------------------------------

    /// Construct an EventBus with the given options.
    /// @param opts  Configuration controlling tier capacities and Tier-3 enablement.
    explicit EventBus(Options opts = {})
        : opts_(opts)
        , tier2_(std::make_unique<MpmcRing<SharedColumnBatch>>(
                     opts.tier2_capacity)) {}

    EventBus(EventBus&&) = delete;            ///< Non-movable (contains mutex).
    EventBus& operator=(EventBus&&) = delete; ///< Non-movable (contains mutex).
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    ~EventBus() = default;

    // =========================================================================
    // Tier 1: dedicated per-channel rings
    // =========================================================================

    /// Create (or return existing) named SPSC/MPSC channel.
    ///
    /// @param name     Logical channel identifier.
    /// @param capacity Ring capacity (0 → use opts_.tier1_capacity).
    [[nodiscard]] std::shared_ptr<Channel> make_channel(
            const std::string& name,
            size_t capacity = 0) {
        std::lock_guard<std::mutex> lk(channels_mutex_);
        auto it = channels_.find(name);
        if (it != channels_.end()) return it->second;

        const size_t cap = (capacity == 0) ? opts_.tier1_capacity : capacity;
        auto ch = std::make_shared<Channel>(cap);
        channels_.emplace(name, ch);
        return ch;
    }

    /// Look up an existing channel by name.
    /// @param name  Logical channel identifier.
    /// @return Shared pointer to the channel, or nullptr if not found.
    [[nodiscard]] std::shared_ptr<Channel> channel(
            const std::string& name) const {
        std::lock_guard<std::mutex> lk(channels_mutex_);
        auto it = channels_.find(name);
        return (it != channels_.end()) ? it->second : nullptr;
    }

    // =========================================================================
    // Tier 2: shared MPMC pool
    // =========================================================================

    /// Publish a batch to the shared Tier-2 ring.
    ///
    /// Also forwards to Tier-3 sink (if attached and opts_.enable_tier3).
    ///
    /// @param batch  The column batch to publish (moved into the ring).
    /// @return true on success; false if the Tier-2 ring is full (caller may retry or drop).
    bool publish(SharedColumnBatch batch) {
        // Tier 3 first — copy shared_ptr under lock so sink stays alive for the call
        std::shared_ptr<StreamingSink> sink;
        { std::lock_guard<std::mutex> lk(sink_mutex_); sink = sink_; }
        if (opts_.enable_tier3 && sink) {
            auto rec = batch->to_stream_record();
            auto r   = sink->submit(std::move(rec));
            if (!r) tier3_drops_.fetch_add(1, std::memory_order_relaxed);
        }

        // Tier 2
        if (!tier2_->push(std::move(batch))) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        published_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    /// Pop a batch from the shared Tier-2 ring (for worker threads).
    /// @param out  Receives the popped batch on success.
    /// @return true if a batch was popped; false if the ring is empty (caller should back off / yield).
    bool pop(SharedColumnBatch& out) {
        return tier2_->pop(out);
    }

    // =========================================================================
    // Tier 3: WAL / StreamingSink
    // =========================================================================

    /// Attach a StreamingSink for Tier-3 durable logging.
    ///
    /// @param sink  Shared pointer to a StreamingSink (bus shares ownership).
    void attach_sink(std::shared_ptr<StreamingSink> sink) {
        std::lock_guard<std::mutex> lk(sink_mutex_);
        sink_ = std::move(sink);
    }

    /// Detach the currently attached Tier-3 sink (no-op if none attached).
    void detach_sink() {
        std::lock_guard<std::mutex> lk(sink_mutex_);
        sink_.reset();
    }

    /// Check whether a Tier-3 StreamingSink is currently attached.
    /// @return true if a sink is attached.
    [[nodiscard]] bool has_sink() const {
        std::lock_guard<std::mutex> lk(sink_mutex_);
        return sink_ != nullptr;
    }

    // =========================================================================
    // Stats
    // =========================================================================

    /// Snapshot of cumulative event bus counters.
    struct Stats {
        uint64_t published   = 0;   ///< Total batches successfully enqueued to Tier-2.
        uint64_t dropped     = 0;   ///< Batches dropped because the Tier-2 ring was full.
        uint64_t tier3_drops = 0;   ///< Batches dropped at the Tier-3 sink (full or not attached).
    };

    /// Return a snapshot of the cumulative event bus statistics.
    /// @return Stats struct with relaxed-order atomic reads.
    [[nodiscard]] Stats stats() const noexcept {
        return Stats{
            published_.load(std::memory_order_relaxed),
            dropped_.load(std::memory_order_relaxed),
            tier3_drops_.load(std::memory_order_relaxed)
        };
    }

    /// Reset all counters to zero.
    void reset_stats() noexcept {
        published_.store(0, std::memory_order_relaxed);
        dropped_.store(0, std::memory_order_relaxed);
        tier3_drops_.store(0, std::memory_order_relaxed);
    }

    // =========================================================================
    // Introspection
    // =========================================================================

    /// Approximate number of batches currently in the Tier-2 ring.
    [[nodiscard]] size_t tier2_size()     const noexcept { return tier2_->size(); }
    /// Capacity of the Tier-2 MPMC ring (power-of-two, set by EventBusOptions).
    [[nodiscard]] size_t tier2_capacity() const noexcept { return tier2_->capacity(); }
    /// Number of named Tier-1 channels that have been created.
    [[nodiscard]] size_t num_channels()   const {
        std::lock_guard<std::mutex> lk(channels_mutex_);
        return channels_.size();
    }

private:
    Options opts_;

    // Tier 2 — shared MPMC ring
    std::unique_ptr<MpmcRing<SharedColumnBatch>> tier2_;

    // Tier 1 — named dedicated channels
    mutable std::mutex channels_mutex_;
    std::unordered_map<std::string, std::shared_ptr<Channel>> channels_;

    // Tier 3 — WAL sink (shared ownership; mutex-guarded for thread safety).
    // CWE-362: H-16 fix — shared_ptr prevents use-after-detach race between
    // publish() and attach_sink()/detach_sink() called from other threads.
    mutable std::mutex sink_mutex_;
    std::shared_ptr<StreamingSink> sink_;

    // Stats
    std::atomic<uint64_t> published_{0};
    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> tier3_drops_{0};
};

} // namespace signet::forge
