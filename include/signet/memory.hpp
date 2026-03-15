// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file memory.hpp
/// @brief Arena (bump-pointer) allocator for batch Parquet reads.
///
/// Provides a fast, allocation-recycling arena that maintains a list of memory
/// blocks. Allocations advance a pointer within the current block; when a block
/// is exhausted, a new one is allocated (at least @c block_size or the requested
/// size, whichever is larger). reset() recycles all blocks without freeing,
/// making them available for the next batch.

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace signet::forge {

/// Bump-pointer arena allocator for batch Parquet reads.
///
/// Maintains a list of memory blocks. Allocations bump a pointer within the
/// current block; when a block is exhausted a new one is allocated. reset()
/// recycles all blocks without freeing, making them available for reuse.
///
/// Non-copyable, movable. Thread-safety: **not** thread-safe -- callers must
/// synchronize externally if shared across threads.
///
/// @see ColumnReader (primary consumer for zero-copy page decoding)
class Arena {
public:
    /// Construct an arena with the given default block size.
    /// @param block_size  Default allocation block size in bytes (default 64 KiB).
    explicit Arena(size_t block_size = 64 * 1024)
        : block_size_(block_size) {}

    ~Arena() = default;

    /// Allocate aligned memory from the arena.
    ///
    /// Attempts to satisfy the allocation from the current block. If the
    /// current block cannot accommodate the request, a new block is allocated
    /// whose size is at least @p size + @p alignment or @c block_size_,
    /// whichever is larger.
    ///
    /// @param size       Number of bytes to allocate (0 returns nullptr).
    /// @param alignment  Required alignment (must be a power of 2, default 8).
    /// @return Pointer to the allocated memory, or nullptr if @p size is 0.
    void* allocate(size_t size, size_t alignment = 8) {
        if (size == 0) return nullptr;

        // Try to fit in the current (last) block
        if (!blocks_.empty()) {
            Block& current = blocks_.back();
            void* ptr = try_allocate_from(current, size, alignment);
            if (ptr) {
                total_used_ += size;
                return ptr;
            }
        }

        // Current block cannot satisfy — allocate a new block
        if (size > SIZE_MAX - alignment) return nullptr; // overflow guard
        size_t new_block_size = (size + alignment > block_size_)
                                    ? (size + alignment)
                                    : block_size_;
        allocate_block(new_block_size);

        Block& fresh = blocks_.back();
        void* ptr = try_allocate_from(fresh, size, alignment);
        // A fresh block is guaranteed to fit (we sized it above)
        if (ptr) {
            total_used_ += size;
        }
        return ptr;
    }

    /// Allocate zero-initialized memory from the arena.
    ///
    /// CWE-908: Use of Uninitialized Resource — zeroing prevents information
    /// leaks from recycled arena blocks or stale heap memory.
    ///
    /// Equivalent to allocate() followed by memset to zero. Useful for
    /// security-sensitive buffers where uninitialized memory could leak data.
    ///
    /// @param size       Number of bytes to allocate (0 returns nullptr).
    /// @param alignment  Required alignment (must be a power of 2, default max_align_t).
    /// @return Pointer to zero-filled memory, or nullptr if @p size is 0.
    void* allocate_zeroed(size_t size, size_t alignment = alignof(std::max_align_t)) {
        void* ptr = allocate(size, alignment);
        if (ptr) std::memset(ptr, 0, size);
        return ptr;
    }

    /// Allocate a typed array of @p count elements from the arena.
    ///
    /// Elements are **not** constructed (raw memory only). The alignment
    /// is derived from @c alignof(T).
    ///
    /// @tparam T     Element type.
    /// @param  count Number of elements (0 returns nullptr).
    /// @return Pointer to the first element, or nullptr if @p count is 0.
    template <typename T>
    T* allocate_array(size_t count) {
        if (count == 0) return nullptr;
        if (count > SIZE_MAX / sizeof(T)) return nullptr; // overflow guard
        void* raw = allocate(count * sizeof(T), alignof(T));
        return static_cast<T*>(raw);
    }

    /// Copy raw bytes into the arena and return a pointer to the copy.
    ///
    /// @param data  Source bytes to copy (nullptr returns nullptr).
    /// @param size  Number of bytes to copy (0 returns nullptr).
    /// @return Pointer to the arena-owned copy, or nullptr on empty input.
    const uint8_t* copy(const uint8_t* data, size_t size) {
        if (size == 0 || data == nullptr) return nullptr;
        void* dst = allocate(size, 1);
        if (!dst) return nullptr; // allocation failed — propagate null
        std::memcpy(dst, data, size);
        return static_cast<const uint8_t*>(dst);
    }

    /// Copy a string into the arena (including null terminator).
    ///
    /// @param str  The string to copy.
    /// @return Pointer to the null-terminated arena-owned copy.
    const char* copy_string(const std::string& str) {
        size_t len = str.size() + 1; // include null terminator
        void* dst = allocate(len, 1);
        if (!dst) return nullptr; // allocation failed — propagate null
        std::memcpy(dst, str.c_str(), len);
        return static_cast<const char*>(dst);
    }

    /// Total bytes allocated (excluding alignment padding).
    /// @return Cumulative allocation count in bytes.
    [[nodiscard]] size_t bytes_used() const { return total_used_; }

    /// Reset the arena, reusing all memory blocks without freeing them.
    ///
    /// After reset, subsequent allocations reuse existing block memory.
    /// This avoids the cost of repeated malloc/free across batches.
    void reset() {
        for (auto& block : blocks_) {
            block.used = 0;
        }
        total_used_ = 0;
    }

    Arena(const Arena&) = delete;            ///< Non-copyable.
    Arena& operator=(const Arena&) = delete; ///< Non-copyable.

    Arena(Arena&&) noexcept = default;            ///< Move-constructible.
    Arena& operator=(Arena&&) noexcept = default; ///< Move-assignable.

private:
    /// Internal block descriptor.
    struct Block {
        std::unique_ptr<uint8_t[]> data; ///< Raw storage.
        size_t size;                     ///< Capacity in bytes.
        size_t used;                     ///< Bytes consumed so far.
    };

    std::vector<Block> blocks_;
    size_t block_size_;
    size_t total_used_ = 0;

    /// Try to allocate from a specific block, respecting alignment.
    static void* try_allocate_from(Block& block, size_t size, size_t alignment) {
        // Round up 'used' to the next multiple of alignment
        size_t aligned_offset = align_up(block.used, alignment);
        if (aligned_offset + size > block.size) {
            return nullptr;
        }
        void* ptr = block.data.get() + aligned_offset;
        block.used = aligned_offset + size;
        return ptr;
    }

    /// Allocate a new block and append it to the block list.
    /// Returns nullptr-equivalent (empty block) on allocation failure.
    void allocate_block(size_t size) {
        try {
            Block block;
            block.data = std::make_unique<uint8_t[]>(size);
            block.size = size;
            block.used = 0;
            blocks_.push_back(std::move(block));
        } catch (const std::bad_alloc&) {
            // Push an empty block so callers see a block with size 0
            Block empty;
            empty.size = 0;
            empty.used = 0;
            blocks_.push_back(std::move(empty));
        }
    }

    /// Round up to the nearest multiple of alignment.
    static size_t align_up(size_t offset, size_t alignment) {
        size_t mask = alignment - 1;
        // Overflow guard: if offset + mask would wrap, return SIZE_MAX (CWE-190)
        if (offset > SIZE_MAX - mask) return SIZE_MAX;
        return (offset + mask) & ~mask;
    }
};

} // namespace signet::forge
