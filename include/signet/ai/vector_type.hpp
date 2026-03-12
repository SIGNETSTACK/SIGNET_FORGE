// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji

/// @file vector_type.hpp
/// @brief AI-native ML vector column type: VectorWriter/VectorReader for dense
///        float embedding storage as FIXED_LEN_BYTE_ARRAY Parquet columns, plus
///        SIMD-accelerated dot product, L2 distance, normalization, and copy.

#pragma once

// ---------------------------------------------------------------------------
// vector_type.hpp — AI-native ML vector column type for SignetStack Signet Forge
//
// Provides VectorWriter / VectorReader for storing and retrieving dense
// float embedding vectors as FIXED_LEN_BYTE_ARRAY Parquet columns, plus
// SIMD-accelerated vector utilities (dot product, L2 distance, normalize).
//
// Header-only. Part of the signet::forge AI module.
// ---------------------------------------------------------------------------

#include "signet/types.hpp"
#include "signet/error.hpp"
#include "signet/schema.hpp"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

// -- SIMD intrinsic headers (conditional) ------------------------------------
#if defined(__AVX2__)
    #include <immintrin.h>
#elif defined(__SSE4_2__) || defined(__SSE2__)
    #include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
#endif

namespace signet::forge {

// ===========================================================================
// VectorElementType — data type of individual vector elements
// ===========================================================================

/// Specifies the numerical precision of each element within a vector column.
enum class VectorElementType : int32_t {
    FLOAT32 = 0,   ///< IEEE 754 single-precision (4 bytes per element)
    FLOAT64 = 1,   ///< IEEE 754 double-precision (8 bytes per element)
    FLOAT16 = 2    ///< IEEE 754 half-precision   (2 bytes per element)
};

// ===========================================================================
// IEEE 754 half-precision (float16) conversion utilities
// ===========================================================================

/// Convert a 16-bit IEEE 754 half-precision value to a 32-bit float.
/// Handles normals, subnormals, infinities, NaNs, and signed zero.
inline float f16_to_f32(uint16_t h) noexcept {
    const uint32_t sign     = static_cast<uint32_t>(h >> 15) & 0x1u;
    const uint32_t exponent = static_cast<uint32_t>(h >> 10) & 0x1Fu;
    const uint32_t mantissa = static_cast<uint32_t>(h)       & 0x3FFu;

    uint32_t f32_bits = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            // +/- zero
            f32_bits = sign << 31;
        } else {
            // Subnormal: renormalize to float32 normal
            // f16 subnormal: value = (-1)^sign * 2^(-14) * (0.mantissa)
            uint32_t m = mantissa;
            int32_t e = -1;
            while ((m & 0x400u) == 0) {
                m <<= 1;
                e--;
            }
            m &= ~0x400u; // clear leading 1
            // f32 exponent: bias(127) + f16_exp(-14) + e
            uint32_t f32_exp = static_cast<uint32_t>(127 - 14 + e + 1);
            f32_bits = (sign << 31) | (f32_exp << 23) | (m << 13);
        }
    } else if (exponent == 0x1Fu) {
        // Infinity or NaN
        f32_bits = (sign << 31) | (0xFFu << 23) | (mantissa << 13);
    } else {
        // Normal number: rebias exponent from f16 bias (15) to f32 bias (127)
        uint32_t f32_exp = exponent - 15u + 127u;
        f32_bits = (sign << 31) | (f32_exp << 23) | (mantissa << 13);
    }

    float result;
    std::memcpy(&result, &f32_bits, sizeof(result));
    return result;
}

/// Convert a 32-bit float to a 16-bit IEEE 754 half-precision value.
/// Rounds to nearest even. Handles overflow to infinity and subnormals.
inline uint16_t f32_to_f16(float val) noexcept {
    uint32_t f32_bits;
    std::memcpy(&f32_bits, &val, sizeof(f32_bits));

    const uint32_t sign     = (f32_bits >> 31) & 0x1u;
    const uint32_t exponent = (f32_bits >> 23) & 0xFFu;
    const uint32_t mantissa = f32_bits         & 0x7FFFFFu;

    uint16_t h_sign = static_cast<uint16_t>(sign << 15);

    if (exponent == 0xFF) {
        // Infinity or NaN
        if (mantissa == 0) {
            return h_sign | 0x7C00u; // Infinity
        } else {
            // NaN — preserve some mantissa bits
            return h_sign | 0x7C00u | static_cast<uint16_t>(mantissa >> 13);
        }
    }

    // Unbias the float32 exponent
    int32_t unbiased_exp = static_cast<int32_t>(exponent) - 127;

    if (unbiased_exp > 15) {
        // Overflow → infinity
        return h_sign | 0x7C00u;
    }

    if (unbiased_exp < -24) {
        // Too small → zero
        return h_sign;
    }

    if (unbiased_exp < -14) {
        // Subnormal in float16: shift mantissa right, adding the implicit 1
        uint32_t full_mantissa = mantissa | 0x800000u; // add implicit leading 1
        int32_t shift = -1 - unbiased_exp - 14 + 24;   // = -unbiased_exp - 14 + 23
        // Handle shift == 32 specially: preserve smallest subnormal (CWE-682)
        if (shift == 32) {
            uint16_t h_mantissa = static_cast<uint16_t>((full_mantissa >> 31) & 1u);
            return h_sign | h_mantissa;
        }
        // Guard: shift > 32 or negative is UB for uint32_t
        if (shift < 0 || shift > 32) {
            return h_sign; // too small for float16 subnormal — rounds to zero
        }
        // Round to nearest even
        uint32_t rounded = full_mantissa >> shift;
        uint32_t remainder = full_mantissa & ((1u << shift) - 1u);
        uint32_t midpoint = 1u << (shift - 1);
        if (remainder > midpoint || (remainder == midpoint && (rounded & 1u))) {
            rounded++;
        }
        return h_sign | static_cast<uint16_t>(rounded);
    }

    // Normal number
    uint16_t h_exp = static_cast<uint16_t>((unbiased_exp + 15) << 10);
    // Round mantissa (23 bits → 10 bits, dropping 13 bits)
    uint16_t h_man = static_cast<uint16_t>(mantissa >> 13);
    // Round to nearest even on the dropped bits
    uint32_t remainder = mantissa & 0x1FFFu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (h_man & 1u))) {
        h_man++;
        if (h_man > 0x3FFu) {
            // Mantissa overflow → increment exponent
            h_man = 0;
            h_exp += (1u << 10);
        }
    }
    return h_sign | h_exp | h_man;
}

// ===========================================================================
// VectorColumnSpec — describes the shape and element type of a vector column
// ===========================================================================

/// Configuration for a vector column: dimensionality and element precision.
///
/// Standard embedding dimensions include 128, 256, 384, 512, 768 (BERT),
/// 1024, 1536 (OpenAI ada-002), and 4096.
struct VectorColumnSpec {
    uint32_t          dimension    = 0;                          ///< Number of elements per vector
    VectorElementType element_type = VectorElementType::FLOAT32; ///< Element precision

    /// Returns the byte size of one element (2, 4, or 8).
    [[nodiscard]] constexpr size_t element_size() const noexcept {
        switch (element_type) {
            case VectorElementType::FLOAT16: return 2;
            case VectorElementType::FLOAT32: return 4;
            case VectorElementType::FLOAT64: return 8;
        }
        return 4; // unreachable, silence warnings
    }

    /// Returns the total byte size of one vector (dimension * element_size).
    [[nodiscard]] constexpr size_t bytes_per_vector() const noexcept {
        return static_cast<size_t>(dimension) * element_size();
    }
};

// ===========================================================================
// VectorWriter — encodes float vectors as FIXED_LEN_BYTE_ARRAY page data
// ===========================================================================

/// Buffers float vectors and encodes them as FIXED_LEN_BYTE_ARRAY PLAIN data.
///
/// Input is always float* (float32). When the spec's element_type is FLOAT16
/// or FLOAT64, the writer converts during add(). The internal buffer stores
/// data in the target element format, ready for direct page embedding.
///
/// Usage:
/// @code
///   VectorWriter w({768, VectorElementType::FLOAT32});
///   w.add(my_embedding_ptr);         // 768 floats
///   w.add_batch(batch_ptr, 100);     // 100 vectors of 768 floats each
///   auto page_bytes = w.flush();     // encoded FIXED_LEN_BYTE_ARRAY data
/// @endcode
///
/// @see VectorReader, VectorColumnSpec
class VectorWriter {
public:
    /// Construct a VectorWriter for the given column specification.
    /// @param spec Column specification (dimension and element type).
    explicit VectorWriter(VectorColumnSpec spec)
        : spec_(spec) {}

    /// Add a single vector from a float32 pointer (must point to `dimension` floats).
    inline void add(const float* data) {
        const size_t dim = spec_.dimension;
        const size_t bpv = spec_.bytes_per_vector();

        switch (spec_.element_type) {
            case VectorElementType::FLOAT32: {
                const auto* raw = reinterpret_cast<const uint8_t*>(data);
                buf_.insert(buf_.end(), raw, raw + bpv);
                break;
            }
            case VectorElementType::FLOAT64: {
                size_t offset = buf_.size();
                buf_.resize(offset + bpv);
                // Use memcpy to avoid unaligned double writes (CWE-704)
                for (size_t i = 0; i < dim; ++i) {
                    double d = static_cast<double>(data[i]);
                    std::memcpy(buf_.data() + offset + i * sizeof(double), &d, sizeof(d));
                }
                break;
            }
            case VectorElementType::FLOAT16: {
                size_t offset = buf_.size();
                buf_.resize(offset + bpv);
                // Use memcpy to avoid unaligned uint16_t writes (CWE-704)
                for (size_t i = 0; i < dim; ++i) {
                    uint16_t h = f32_to_f16(data[i]);
                    std::memcpy(buf_.data() + offset + i * 2, &h, sizeof(h));
                }
                break;
            }
        }
        ++num_vectors_;
    }

    /// Add a batch of vectors (num_vectors vectors, each `dimension` elements, row-major).
    /// @return true on success, false on overflow (batch rejected entirely).
    inline bool add_batch(const float* data, size_t num_vectors) {
        const size_t dim = spec_.dimension;
        if (dim == 0 || num_vectors == 0) return true;
        if (num_vectors > SIZE_MAX / dim) return false;
        for (size_t i = 0; i < num_vectors; ++i) {
            add(data + i * dim);
        }
        return true;
    }

    /// Flush the buffered vectors and return the encoded page bytes.
    ///
    /// The returned buffer contains PLAIN-encoded FIXED_LEN_BYTE_ARRAY data:
    /// consecutive vectors of bytes_per_vector() bytes each, with no length
    /// prefix (the type_length is known from the column descriptor).
    ///
    /// After flush, the writer is reset and ready for the next page.
    [[nodiscard]] inline std::vector<uint8_t> flush() {
        std::vector<uint8_t> out = std::move(buf_);
        buf_.clear();
        num_vectors_ = 0;
        return out;
    }

    /// Number of vectors currently buffered (since last flush).
    [[nodiscard]] size_t num_vectors() const noexcept { return num_vectors_; }

    /// The column spec this writer was constructed with.
    [[nodiscard]] const VectorColumnSpec& spec() const noexcept { return spec_; }

    /// Create a ColumnDescriptor for a vector column with the given name and spec.
    ///
    /// The descriptor maps to:
    ///   physical_type = FIXED_LEN_BYTE_ARRAY
    ///   logical_type  = FLOAT32_VECTOR
    ///   type_length   = dimension * element_size
    [[nodiscard]] static inline ColumnDescriptor make_descriptor(
            const std::string& name,
            const VectorColumnSpec& spec) {
        ColumnDescriptor cd;
        cd.name          = name;
        cd.physical_type = PhysicalType::FIXED_LEN_BYTE_ARRAY;
        cd.logical_type  = LogicalType::FLOAT32_VECTOR;
        cd.type_length   = static_cast<int32_t>(spec.bytes_per_vector());
        return cd;
    }

private:
    VectorColumnSpec       spec_;
    std::vector<uint8_t>   buf_;
    size_t                 num_vectors_ = 0;
};

// ===========================================================================
// VectorReader — decodes FIXED_LEN_BYTE_ARRAY pages into float vectors
// ===========================================================================

/// Reads FIXED_LEN_BYTE_ARRAY page data back into float vectors.
///
/// Does not own any data -- operates on const pointers to page buffers.
/// Supports element type conversion: FLOAT16/FLOAT64 pages are converted
/// to float32 on read. For FLOAT32 data with natural alignment, a zero-copy
/// path is available via read_page_zero_copy().
///
/// @see VectorWriter, VectorColumnSpec
class VectorReader {
public:
    /// Construct a VectorReader for the given column specification.
    /// @param spec Column specification (dimension and element type).
    explicit VectorReader(VectorColumnSpec spec)
        : spec_(spec) {}

    /// Decode a PLAIN-encoded page of FIXED_LEN_BYTE_ARRAY vectors into
    /// float32 vectors. Each inner vector has `dimension` elements.
    ///
    /// @param data      Pointer to the start of page data.
    /// @param data_size Total byte size of the page data.
    /// @return A vector of float vectors (one per stored vector).
    [[nodiscard]] inline std::vector<std::vector<float>>
    read_page(const uint8_t* data, size_t data_size) const {
        const size_t bpv = spec_.bytes_per_vector();
        const size_t dim = spec_.dimension;

        if (bpv == 0) return {};

        const size_t count = data_size / bpv;
        std::vector<std::vector<float>> result;
        result.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            const uint8_t* src = data + i * bpv;
            std::vector<float> vec(dim);

            switch (spec_.element_type) {
                case VectorElementType::FLOAT32: {
                    std::memcpy(vec.data(), src, dim * sizeof(float));
                    break;
                }
                case VectorElementType::FLOAT64: {
                    // Use memcpy to avoid unaligned double reads (CWE-704)
                    for (size_t j = 0; j < dim; ++j) {
                        double d;
                        std::memcpy(&d, src + j * sizeof(double), sizeof(d));
                        vec[j] = static_cast<float>(d);
                    }
                    break;
                }
                case VectorElementType::FLOAT16: {
                    // Use memcpy to avoid unaligned uint16_t reads (CWE-704)
                    for (size_t j = 0; j < dim; ++j) {
                        uint16_t h;
                        std::memcpy(&h, src + j * 2, sizeof(h));
                        vec[j] = f16_to_f32(h);
                    }
                    break;
                }
            }
            result.push_back(std::move(vec));
        }
        return result;
    }

    /// Zero-copy read result: a pointer to the float data and the vector count.
    struct ZeroCopyResult {
        const float* data;         ///< Pointer to the first float of the first vector
        size_t       num_vectors;  ///< Number of complete vectors in the page
    };

    /// Attempt a zero-copy read of a FLOAT32 page.
    ///
    /// Returns a direct pointer into the page buffer without any data copying.
    /// Only succeeds when:
    ///   - element_type is FLOAT32 (no conversion needed)
    ///   - data pointer is 4-byte aligned (float alignment)
    ///   - data_size is an exact multiple of bytes_per_vector()
    ///
    /// @param data      Pointer to page data.
    /// @param data_size Total byte size of the page data.
    /// @return ZeroCopyResult on success, Error on failure.
    [[nodiscard]] inline expected<ZeroCopyResult>
    read_page_zero_copy(const uint8_t* data, size_t data_size) const {
        if (spec_.element_type != VectorElementType::FLOAT32) {
            return Error{ErrorCode::UNSUPPORTED_TYPE,
                         "zero-copy read requires FLOAT32 element type"};
        }

        // Check alignment (float requires 4-byte alignment)
        if (reinterpret_cast<uintptr_t>(data) % alignof(float) != 0) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "page data is not aligned for float access"};
        }

        const size_t bpv = spec_.bytes_per_vector();
        if (bpv == 0) {
            return Error{ErrorCode::SCHEMA_MISMATCH,
                         "vector dimension is zero"};
        }
        if (data_size % bpv != 0) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "page size is not a multiple of bytes_per_vector"};
        }

        ZeroCopyResult result;
        result.data        = reinterpret_cast<const float*>(data);
        result.num_vectors = data_size / bpv;
        return result;
    }

    /// Read a single vector at the given index from a page.
    ///
    /// @param page_data  Pointer to page data.
    /// @param page_size  Total byte size of the page data.
    /// @param index      Zero-based index of the vector to read.
    /// @return A float vector with `dimension` elements, or empty on OOB.
    [[nodiscard]] inline std::vector<float>
    read_vector(const uint8_t* page_data, size_t page_size, size_t index) const {
        const size_t bpv = spec_.bytes_per_vector();
        const size_t dim = spec_.dimension;

        if (bpv == 0 || (index + 1) * bpv > page_size) {
            return {};
        }

        const uint8_t* src = page_data + index * bpv;
        std::vector<float> vec(dim);

        switch (spec_.element_type) {
            case VectorElementType::FLOAT32: {
                std::memcpy(vec.data(), src, dim * sizeof(float));
                break;
            }
            case VectorElementType::FLOAT64: {
                // Use memcpy to avoid unaligned double reads (CWE-704)
                for (size_t j = 0; j < dim; ++j) {
                    double d;
                    std::memcpy(&d, src + j * sizeof(double), sizeof(d));
                    vec[j] = static_cast<float>(d);
                }
                break;
            }
            case VectorElementType::FLOAT16: {
                // Use memcpy to avoid unaligned uint16_t reads (CWE-704)
                for (size_t j = 0; j < dim; ++j) {
                    uint16_t h;
                    std::memcpy(&h, src + j * 2, sizeof(h));
                    vec[j] = f16_to_f32(h);
                }
                break;
            }
        }
        return vec;
    }

    /// The column spec this reader was constructed with.
    [[nodiscard]] const VectorColumnSpec& spec() const noexcept { return spec_; }

private:
    VectorColumnSpec spec_;
};

// ===========================================================================
// SIMD vector utilities
// ===========================================================================

/// Platform-optimized SIMD routines for common vector operations.
///
/// Compile-time dispatch:
///   __AVX2__                → 8-wide float (256-bit) using _mm256_*
///   __SSE4_2__ / __SSE2__   → 4-wide float (128-bit) using _mm_*
///   __ARM_NEON              → 4-wide float (128-bit) using NEON intrinsics
///   fallback                → scalar loop
///
/// All functions accept unaligned pointers and handle tail elements correctly.
namespace simd {

// ---------------------------------------------------------------------------
// dot_product — inner product of two float vectors
// ---------------------------------------------------------------------------

/// Compute the dot product (inner product) of two float vectors.
///
/// @param a  First vector (n elements).
/// @param b  Second vector (n elements).
/// @param n  Number of elements.
/// @return   sum(a[i] * b[i]) for i in [0, n).
inline float dot_product(const float* a, const float* b, size_t n) noexcept {
    float sum = 0.0f;
    size_t i = 0;

#if defined(__AVX2__)
    __m256 acc = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        acc = _mm256_fmadd_ps(va, vb, acc);
    }
    // Horizontal sum of 8 floats
    __m128 lo = _mm256_castps256_ps128(acc);
    __m128 hi = _mm256_extractf128_ps(acc, 1);
    __m128 s4 = _mm_add_ps(lo, hi);
    __m128 s2 = _mm_add_ps(s4, _mm_movehl_ps(s4, s4));
    __m128 s1 = _mm_add_ss(s2, _mm_movehdup_ps(s2));
    sum = _mm_cvtss_f32(s1);

#elif defined(__SSE4_2__) || defined(__SSE2__)
    __m128 acc = _mm_setzero_ps();
    for (; i + 4 <= n; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        acc = _mm_add_ps(acc, _mm_mul_ps(va, vb));
    }
    // Horizontal sum of 4 floats
    __m128 shuf = _mm_movehl_ps(acc, acc);
    __m128 sums = _mm_add_ps(acc, shuf);
    shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(0,0,0,1));  // SSE1 (no SSE3)
    sums = _mm_add_ss(sums, shuf);
    sum = _mm_cvtss_f32(sums);

#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t acc = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        acc = vmlaq_f32(acc, va, vb);
    }
    // Horizontal sum
    float32x2_t pair = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
    pair = vpadd_f32(pair, pair);
    sum = vget_lane_f32(pair, 0);
#endif

    // Scalar tail
    for (; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

// ---------------------------------------------------------------------------
// l2_distance_sq — squared Euclidean distance
// ---------------------------------------------------------------------------

/// Compute the squared L2 (Euclidean) distance between two float vectors.
///
/// @param a  First vector (n elements).
/// @param b  Second vector (n elements).
/// @param n  Number of elements.
/// @return   sum((a[i] - b[i])^2) for i in [0, n).
inline float l2_distance_sq(const float* a, const float* b, size_t n) noexcept {
    float sum = 0.0f;
    size_t i = 0;

#if defined(__AVX2__)
    __m256 acc = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        __m256 diff = _mm256_sub_ps(va, vb);
        acc = _mm256_fmadd_ps(diff, diff, acc);
    }
    __m128 lo = _mm256_castps256_ps128(acc);
    __m128 hi = _mm256_extractf128_ps(acc, 1);
    __m128 s4 = _mm_add_ps(lo, hi);
    __m128 s2 = _mm_add_ps(s4, _mm_movehl_ps(s4, s4));
    __m128 s1 = _mm_add_ss(s2, _mm_movehdup_ps(s2));
    sum = _mm_cvtss_f32(s1);

#elif defined(__SSE4_2__) || defined(__SSE2__)
    __m128 acc = _mm_setzero_ps();
    for (; i + 4 <= n; i += 4) {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        __m128 diff = _mm_sub_ps(va, vb);
        acc = _mm_add_ps(acc, _mm_mul_ps(diff, diff));
    }
    __m128 shuf = _mm_movehl_ps(acc, acc);
    __m128 sums = _mm_add_ps(acc, shuf);
    shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(0,0,0,1));  // SSE1 (no SSE3)
    sums = _mm_add_ss(sums, shuf);
    sum = _mm_cvtss_f32(sums);

#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t acc = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        float32x4_t diff = vsubq_f32(va, vb);
        acc = vmlaq_f32(acc, diff, diff);
    }
    float32x2_t pair = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
    pair = vpadd_f32(pair, pair);
    sum = vget_lane_f32(pair, 0);
#endif

    for (; i < n; ++i) {
        float d = a[i] - b[i];
        sum += d * d;
    }
    return sum;
}

// ---------------------------------------------------------------------------
// sum_of_squares — for norm computation
// ---------------------------------------------------------------------------

/// Compute the sum of squares of a float vector.
///
/// @param data  Input vector (n elements).
/// @param n     Number of elements.
/// @return      sum(data[i]^2) for i in [0, n).
inline float sum_of_squares(const float* data, size_t n) noexcept {
    float sum = 0.0f;
    size_t i = 0;

#if defined(__AVX2__)
    __m256 acc = _mm256_setzero_ps();
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_loadu_ps(data + i);
        acc = _mm256_fmadd_ps(v, v, acc);
    }
    __m128 lo = _mm256_castps256_ps128(acc);
    __m128 hi = _mm256_extractf128_ps(acc, 1);
    __m128 s4 = _mm_add_ps(lo, hi);
    __m128 s2 = _mm_add_ps(s4, _mm_movehl_ps(s4, s4));
    __m128 s1 = _mm_add_ss(s2, _mm_movehdup_ps(s2));
    sum = _mm_cvtss_f32(s1);

#elif defined(__SSE4_2__) || defined(__SSE2__)
    __m128 acc = _mm_setzero_ps();
    for (; i + 4 <= n; i += 4) {
        __m128 v = _mm_loadu_ps(data + i);
        acc = _mm_add_ps(acc, _mm_mul_ps(v, v));
    }
    __m128 shuf = _mm_movehl_ps(acc, acc);
    __m128 sums = _mm_add_ps(acc, shuf);
    shuf = _mm_shuffle_ps(sums, sums, _MM_SHUFFLE(0,0,0,1));  // SSE1 (no SSE3)
    sums = _mm_add_ss(sums, shuf);
    sum = _mm_cvtss_f32(sums);

#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t acc = vdupq_n_f32(0.0f);
    for (; i + 4 <= n; i += 4) {
        float32x4_t v = vld1q_f32(data + i);
        acc = vmlaq_f32(acc, v, v);
    }
    float32x2_t pair = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
    pair = vpadd_f32(pair, pair);
    sum = vget_lane_f32(pair, 0);
#endif

    for (; i < n; ++i) {
        sum += data[i] * data[i];
    }
    return sum;
}

// ---------------------------------------------------------------------------
// l2_normalize — normalize a vector to unit length in-place
// ---------------------------------------------------------------------------

/// L2-normalize a float vector in-place (divide each element by the L2 norm).
///
/// If the vector has zero norm (all zeros), it is left unchanged.
///
/// @param data  Vector to normalize (n elements, modified in-place).
/// @param n     Number of elements.
inline void l2_normalize(float* data, size_t n) noexcept {
    const float norm_sq = sum_of_squares(data, n);
    if (norm_sq == 0.0f) return;

    const float inv_norm = 1.0f / std::sqrt(norm_sq);
    size_t i = 0;

#if defined(__AVX2__)
    __m256 scale = _mm256_set1_ps(inv_norm);
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_loadu_ps(data + i);
        _mm256_storeu_ps(data + i, _mm256_mul_ps(v, scale));
    }

#elif defined(__SSE4_2__) || defined(__SSE2__)
    __m128 scale = _mm_set1_ps(inv_norm);
    for (; i + 4 <= n; i += 4) {
        __m128 v = _mm_loadu_ps(data + i);
        _mm_storeu_ps(data + i, _mm_mul_ps(v, scale));
    }

#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t scale = vdupq_n_f32(inv_norm);
    for (; i + 4 <= n; i += 4) {
        float32x4_t v = vld1q_f32(data + i);
        vst1q_f32(data + i, vmulq_f32(v, scale));
    }
#endif

    for (; i < n; ++i) {
        data[i] *= inv_norm;
    }
}

// ---------------------------------------------------------------------------
// copy_floats — fast memcpy for float arrays
// ---------------------------------------------------------------------------

/// Fast copy of n floats from src to dst.
///
/// Uses SIMD stores/loads when available for bandwidth. Handles unaligned
/// pointers and non-multiple-of-SIMD-width sizes correctly.
///
/// @param dst  Destination (n floats).
/// @param src  Source (n floats).
/// @param n    Number of floats to copy.
inline void copy_floats(float* dst, const float* src, size_t n) noexcept {
    size_t i = 0;

#if defined(__AVX2__)
    for (; i + 8 <= n; i += 8) {
        __m256 v = _mm256_loadu_ps(src + i);
        _mm256_storeu_ps(dst + i, v);
    }

#elif defined(__SSE4_2__) || defined(__SSE2__)
    for (; i + 4 <= n; i += 4) {
        __m128 v = _mm_loadu_ps(src + i);
        _mm_storeu_ps(dst + i, v);
    }

#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (; i + 4 <= n; i += 4) {
        float32x4_t v = vld1q_f32(src + i);
        vst1q_f32(dst + i, v);
    }
#endif

    // Scalar tail (also handles the full copy on platforms without SIMD)
    if (i < n) {
        std::memcpy(dst + i, src + i, (n - i) * sizeof(float));
    }
}

} // namespace simd

// ===========================================================================
// SchemaBuilder extension — add_vector_column
// ===========================================================================

/// Add a vector column to a SchemaBuilder.
///
/// Creates a FIXED_LEN_BYTE_ARRAY column with FLOAT32_VECTOR logical type,
/// type_length set to dimension * element_size.
///
/// Usage:
///   auto schema = add_vector_column(
///       Schema::builder("embeddings")
///           .column<int64_t>("id")
///           .column<std::string>("text"),
///       "embedding", 768)
///       .build();
///
/// @param builder    The SchemaBuilder to add the column to.
/// @param name       Column name.
/// @param dimension  Number of elements per vector (e.g. 768).
/// @param elem       Element type (default FLOAT32).
/// @return Reference to the builder for chaining.
inline SchemaBuilder& add_vector_column(
        SchemaBuilder& builder,
        const std::string& name,
        uint32_t dimension,
        VectorElementType elem = VectorElementType::FLOAT32) {
    VectorColumnSpec spec{dimension, elem};
    return builder.raw_column(VectorWriter::make_descriptor(name, spec));
}

} // namespace signet::forge
