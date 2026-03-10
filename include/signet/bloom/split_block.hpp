// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file split_block.hpp
/// @brief Split Block Bloom Filter as specified by the Apache Parquet format.
///
/// Implements the bloom filter described in the Apache Parquet specification:
///   https://github.com/apache/parquet-format/blob/master/BloomFilter.md
///
/// Key properties:
///   - Filter is divided into 32-byte blocks (256 bits, 8 x uint32_t words).
///   - Each insertion sets 8 bits (one per word) within a single block.
///   - Block selection and bit positions are deterministic from the hash.
///   - Uses xxHash64 for hashing typed values.
///   - Total size is always a positive multiple of 32 bytes.
///
/// @see signet::forge::xxhash for the hash function used by convenience methods.

#include "signet/bloom/xxhash.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace signet::forge {

/// Parquet-spec Split Block Bloom Filter for probabilistic set membership.
///
/// A space-efficient probabilistic data structure that answers "is this value
/// possibly in the set?" with a tunable false-positive rate. False negatives
/// never occur: if might_contain() returns false, the value was definitely
/// never inserted.
///
/// The filter is organized as an array of 32-byte blocks. Each insertion
/// touches exactly one block, setting 8 bits (one per uint32_t word) derived
/// from the hash via the Parquet salt constants. This block-local design is
/// cache-friendly and allows efficient SIMD implementations.
///
/// @note Thread safety: not thread-safe. External synchronization is required
///       if multiple threads insert or query concurrently.
///
/// @code
///   // Size for 10,000 distinct values at 1% FPR
///   SplitBlockBloomFilter bloom(10000, 0.01);
///   bloom.insert_value(std::string("AAPL"));
///   bloom.insert_value(42);
///   assert(bloom.might_contain_value(std::string("AAPL")) == true);
/// @endcode
///
/// @see https://github.com/apache/parquet-format/blob/master/BloomFilter.md
class SplitBlockBloomFilter {
public:
    /// Block size in bytes (32 bytes = 256 bits = 8 x uint32_t words).
    static constexpr size_t kBytesPerBlock = 32;

    /// Number of uint32_t words per block (8).
    static constexpr size_t kWordsPerBlock = 8;

    /// Minimum filter size: one block (32 bytes).
    static constexpr size_t kMinBytes = kBytesPerBlock;

    /// Maximum filter size: 128 MiB (Parquet spec recommendation).
    static constexpr size_t kMaxBytes = 128ULL * 1024 * 1024;

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    /// Create a bloom filter sized for the expected number of distinct values.
    ///
    /// Uses the optimal sizing formula from the Parquet specification:
    ///   `num_bytes = -8 * ndv / ln(1 - fpr^(1/8))`
    /// rounded up to the next multiple of 32 bytes, then clamped to
    /// [kMinBytes, kMaxBytes].
    ///
    /// @param num_distinct_values Expected number of distinct values to insert.
    ///        A value of 0 is treated as 1 (at least one block is allocated).
    /// @param fpr Desired false-positive rate, must be in the open interval (0, 1).
    ///        Default is 0.01 (1%).
    /// @throws std::invalid_argument if @p fpr is not in (0, 1).
    explicit SplitBlockBloomFilter(size_t num_distinct_values,
                                    double fpr = 0.01)
    {
        if (fpr <= 0.0 || fpr >= 1.0) {
            throw std::invalid_argument(
                "SplitBlockBloomFilter: fpr must be in (0, 1)");
        }
        if (num_distinct_values == 0) {
            num_distinct_values = 1; // at least one block
        }

        // Optimal byte count: -8 * ndv / ln(1 - fpr^(1/8))
        double exponent  = 1.0 / 8.0;
        double fpr_root  = std::pow(fpr, exponent);
        double denom     = std::log(1.0 - fpr_root);
        double raw_bytes = -8.0 * static_cast<double>(num_distinct_values) / denom;

        // Round up to multiple of 32, clamp to [kMinBytes, kMaxBytes]
        auto num_bytes = static_cast<size_t>(std::ceil(raw_bytes));
        num_bytes = round_up_to_block(num_bytes);
        num_bytes = (std::max)(num_bytes, kMinBytes);
        num_bytes = (std::min)(num_bytes, kMaxBytes);

        data_.resize(num_bytes, 0);
    }

    /// Create a bloom filter with an explicit byte size.
    ///
    /// @param num_bytes Filter size in bytes. Must be a positive multiple of 32.
    ///        Clamped to kMaxBytes if larger.
    /// @return A zero-initialized filter of the requested size.
    /// @throws std::invalid_argument if @p num_bytes is 0 or not a multiple of 32.
    static SplitBlockBloomFilter with_size(size_t num_bytes) {
        if (num_bytes == 0 || (num_bytes % kBytesPerBlock) != 0) {
            throw std::invalid_argument(
                "SplitBlockBloomFilter::with_size: "
                "num_bytes must be a positive multiple of 32");
        }
        num_bytes = (std::min)(num_bytes, kMaxBytes);
        SplitBlockBloomFilter f;
        f.data_.resize(num_bytes, 0);
        return f;
    }

    // -----------------------------------------------------------------------
    // Core operations
    // -----------------------------------------------------------------------

    /// Insert a pre-computed xxHash64 value into the filter.
    ///
    /// The upper 32 bits select the block; the lower 32 bits, multiplied by
    /// each of the 8 salt constants, determine the bit positions within that block.
    ///
    /// @param hash A 64-bit hash (typically from xxhash::hash64()).
    void insert(uint64_t hash) {
        const size_t nblocks = num_blocks();
        // Block index: use upper 32 bits to select the block
        // Invariant: nblocks > 0 (enforced by constructor min 1 block)
        const auto block_idx = static_cast<size_t>(
            (static_cast<uint64_t>(hash >> 32) * nblocks) >> 32);
        assert(block_idx < nblocks);

        const auto key = static_cast<uint32_t>(hash);

        for (size_t i = 0; i < kWordsPerBlock; ++i) {
            uint32_t bit_pos = (key * kSalt[i]) >> 27;  // top 5 bits
            uint32_t word = block_read(block_idx, i);
            word |= (UINT32_C(1) << bit_pos);
            block_write(block_idx, i, word);
        }
    }

    /// Check if a pre-computed hash value might be present in the filter.
    ///
    /// @param hash A 64-bit hash (typically from xxhash::hash64()).
    /// @return `false` if the value is definitely not in the filter;
    ///         `true` if the value is probably in the filter (subject to FPR).
    [[nodiscard]] bool might_contain(uint64_t hash) const {
        const size_t nblocks = num_blocks();
        const auto block_idx = static_cast<size_t>(
            (static_cast<uint64_t>(hash >> 32) * nblocks) >> 32);
        assert(block_idx < nblocks);

        const auto key = static_cast<uint32_t>(hash);

        for (size_t i = 0; i < kWordsPerBlock; ++i) {
            uint32_t bit_pos = (key * kSalt[i]) >> 27;
            uint32_t word = block_read(block_idx, i);
            if ((word & (UINT32_C(1) << bit_pos)) == 0) {
                return false;
            }
        }
        return true;
    }

    // -----------------------------------------------------------------------
    // Convenience: insert/check typed values using xxHash64
    // -----------------------------------------------------------------------

    /// Insert a string value (hashed with xxHash64).
    /// @param val The string to insert.
    void insert_value(const std::string& val) {
        insert(xxhash::hash64(val));
    }
    /// Insert a 32-bit integer value (hashed with xxHash64).
    /// @param val The int32_t to insert.
    void insert_value(int32_t val) {
        insert(xxhash::hash64_value(val));
    }
    /// Insert a 64-bit integer value (hashed with xxHash64).
    /// @param val The int64_t to insert.
    void insert_value(int64_t val) {
        insert(xxhash::hash64_value(val));
    }
    /// Insert a 32-bit float value (hashed with xxHash64).
    /// @param val The float to insert.
    void insert_value(float val) {
        insert(xxhash::hash64_value(val));
    }
    /// Insert a 64-bit double value (hashed with xxHash64).
    /// @param val The double to insert.
    void insert_value(double val) {
        insert(xxhash::hash64_value(val));
    }

    /// Check if a string value might be present in the filter.
    /// @param val The string to test.
    /// @return `false` if definitely absent; `true` if probably present.
    [[nodiscard]] bool might_contain_value(const std::string& val) const {
        return might_contain(xxhash::hash64(val));
    }
    /// Check if a 32-bit integer value might be present in the filter.
    /// @param val The int32_t to test.
    /// @return `false` if definitely absent; `true` if probably present.
    [[nodiscard]] bool might_contain_value(int32_t val) const {
        return might_contain(xxhash::hash64_value(val));
    }
    /// Check if a 64-bit integer value might be present in the filter.
    /// @param val The int64_t to test.
    /// @return `false` if definitely absent; `true` if probably present.
    [[nodiscard]] bool might_contain_value(int64_t val) const {
        return might_contain(xxhash::hash64_value(val));
    }
    /// Check if a 32-bit float value might be present in the filter.
    /// @param val The float to test.
    /// @return `false` if definitely absent; `true` if probably present.
    [[nodiscard]] bool might_contain_value(float val) const {
        return might_contain(xxhash::hash64_value(val));
    }
    /// Check if a 64-bit double value might be present in the filter.
    /// @param val The double to test.
    /// @return `false` if definitely absent; `true` if probably present.
    [[nodiscard]] bool might_contain_value(double val) const {
        return might_contain(xxhash::hash64_value(val));
    }

    // -----------------------------------------------------------------------
    // Serialization / deserialization
    // -----------------------------------------------------------------------

    /// Access the raw filter data for serialization into a Parquet file.
    /// @return Const reference to the internal byte vector.
    [[nodiscard]] const std::vector<uint8_t>& data() const { return data_; }

    /// Total size of the filter in bytes (always a multiple of 32).
    [[nodiscard]] size_t size_bytes() const { return data_.size(); }

    /// Number of 32-byte blocks in the filter.
    [[nodiscard]] size_t num_blocks() const {
        return data_.size() / kBytesPerBlock;
    }

    /// Reconstruct a filter from previously serialized bytes.
    ///
    /// @param src  Pointer to the serialized filter data.
    /// @param size Length of the data in bytes. Must be a positive multiple of 32.
    /// @return A filter initialized with the provided data.
    /// @throws std::invalid_argument if @p size is 0 or not a multiple of 32.
    static SplitBlockBloomFilter from_data(const uint8_t* src, size_t size) {
        if (size == 0 || (size % kBytesPerBlock) != 0) {
            throw std::invalid_argument(
                "SplitBlockBloomFilter::from_data: "
                "size must be a positive multiple of 32");
        }
        // CWE-400: Uncontrolled Resource Consumption — kMaxBytes (128 MiB) cap
        if (size > kMaxBytes) {
            throw std::invalid_argument(
                "SplitBlockBloomFilter::from_data: "
                "data exceeds maximum filter size");
        }
        SplitBlockBloomFilter f;
        f.data_.assign(src, src + size);
        return f;
    }

    // -----------------------------------------------------------------------
    // Utilities
    // -----------------------------------------------------------------------

    /// Merge another filter into this one via bitwise OR.
    ///
    /// After merging, this filter represents the union of both sets.
    /// Both filters must have identical byte sizes.
    ///
    /// @param other The filter to merge into this one.
    /// @throws std::invalid_argument if the filters have different sizes.
    void merge(const SplitBlockBloomFilter& other) {
        if (data_.size() != other.data_.size()) {
            throw std::invalid_argument(
                "SplitBlockBloomFilter::merge: filter sizes must match");
        }
        for (size_t i = 0; i < data_.size(); ++i) {
            data_[i] |= other.data_[i];
        }
    }

    /// Reset the filter (clear all bits to zero).
    void reset() {
        std::fill(data_.begin(), data_.end(), uint8_t{0});
    }

    /// Estimate the false-positive rate after a given number of insertions.
    ///
    /// Uses the standard Bloom filter FPR formula adapted for split blocks:
    ///   `fpr ~ (1 - exp(-k * n / (num_blocks * 256)))^k`
    /// where `n` = @p num_insertions, `k` = 8 (bits per insertion), and each
    /// block contains 256 bits.
    ///
    /// @param num_insertions Number of distinct values inserted so far.
    /// @return Estimated false-positive probability in [0.0, 1.0].
    ///         Returns 0.0 if @p num_insertions is 0.
    [[nodiscard]] double estimated_fpr(size_t num_insertions) const {
        if (num_insertions == 0) return 0.0;

        const double k = 8.0;
        const double m = static_cast<double>(num_blocks()) * 256.0; // total bits
        const double n = static_cast<double>(num_insertions);

        // Probability that a single bit is set after n insertions
        double p = 1.0 - std::exp(-k * n / m);
        // FPR = probability all k bits match
        return std::pow(p, k);
    }

private:
    /// Default constructor for internal use by factory methods (with_size, from_data).
    SplitBlockBloomFilter() = default;

    /// Per Parquet spec: bloom filter hash seed must be 0. The xxHash64
    /// functions in xxhash.hpp default to seed=0, satisfying this requirement.
    static constexpr uint64_t kBloomSeed = 0;
    static_assert(kBloomSeed == 0, "Parquet spec requires bloom filter seed = 0");

    /// Salt values from the Parquet specification.
    ///
    /// These 8 constants are multiplied by the lower 32 bits of the hash to
    /// derive 8 independent bit positions (one per word) within a single block.
    static constexpr uint32_t kSalt[kWordsPerBlock] = {
        0x47b6137bU,
        0x44974d91U,
        0x8824ad5bU,
        0xa2b7289dU,
        0x705495c7U,
        0x2df1424bU,
        0x9efc4947U,
        0x5c6bfb31U
    };

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// Round a byte count up to the next multiple of kBytesPerBlock (32 bytes).
    /// @param bytes Raw byte count.
    /// @return @p bytes rounded up to the nearest 32-byte boundary.
    static constexpr size_t round_up_to_block(size_t bytes) {
        return ((bytes + kBytesPerBlock - 1) / kBytesPerBlock) * kBytesPerBlock;
    }

    /// Read a uint32_t word from block @p block_idx at word position @p word_idx.
    /// CWE-704: Incorrect Type Conversion (strict aliasing, C++ [basic.lval] §6.7.2)
    /// — memcpy avoids UB from reinterpret_cast<uint32_t*> on a uint8_t[] buffer.
    uint32_t block_read(size_t block_idx, size_t word_idx) const {
        uint32_t val;
        std::memcpy(&val, data_.data() + block_idx * kBytesPerBlock + word_idx * sizeof(uint32_t), sizeof(uint32_t));
        return val;
    }

    /// Write a uint32_t word to block @p block_idx at word position @p word_idx.
    /// CWE-704: Incorrect Type Conversion (strict aliasing, C++ [basic.lval] §6.7.2)
    /// — memcpy avoids UB from reinterpret_cast<uint32_t*> on a uint8_t[] buffer.
    void block_write(size_t block_idx, size_t word_idx, uint32_t val) {
        std::memcpy(data_.data() + block_idx * kBytesPerBlock + word_idx * sizeof(uint32_t), &val, sizeof(uint32_t));
    }

    /// Get a mutable pointer to the 8 uint32_t words of block @p idx.
    /// @note Provided for backward compatibility; prefer block_read/block_write.
    uint32_t* block_ptr(size_t idx) {
        return reinterpret_cast<uint32_t*>(data_.data() + idx * kBytesPerBlock);
    }

    /// Get a const pointer to the 8 uint32_t words of block @p idx.
    /// @note Provided for backward compatibility; prefer block_read/block_write.
    const uint32_t* block_ptr(size_t idx) const {
        return reinterpret_cast<const uint32_t*>(
            data_.data() + idx * kBytesPerBlock);
    }

    /// Raw filter storage. Size is always a positive multiple of kBytesPerBlock.
    std::vector<uint8_t> data_;
};

} // namespace signet::forge
