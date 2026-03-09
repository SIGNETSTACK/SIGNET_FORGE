// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once
/// @file quantized_vector.hpp
/// @brief INT8/INT4 quantized vector storage for AI embeddings with SIMD acceleration.

// ---------------------------------------------------------------------------
// quantized_vector.hpp -- INT8/INT4 quantized vector storage for AI embeddings
//
// Header-only. Provides quantization (float32 -> int8/int4) and dequantization
// (int8/int4 -> float32) for storing ML embedding vectors in Parquet files
// with 75-87.5% storage savings over native FLOAT32.
//
// Quantization schemes:
//   SYMMETRIC_INT8  -- value = round(float / scale), range [-127, 127]
//   ASYMMETRIC_INT8 -- value = round((float - zero_point) / scale), [0, 255]
//   SYMMETRIC_INT4  -- value = round(float / scale), range [-7, 7],
//                      two values per byte (high nibble first)
//
// SIMD acceleration: AVX2 (8 floats), SSE2 (4 floats), NEON (4 floats),
// with scalar fallback.
// ---------------------------------------------------------------------------

#include "signet/types.hpp"
#include "signet/error.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SIMD platform detection
// ---------------------------------------------------------------------------
#if defined(__AVX2__)
    #include <immintrin.h>
    #define SIGNET_QUANT_AVX2 1
#elif defined(__SSE4_2__) || defined(__SSE4_1__) || defined(__SSE2__)
    #include <immintrin.h>
    #define SIGNET_QUANT_SSE 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__)
    #include <arm_neon.h>
    #define SIGNET_QUANT_NEON 1
#endif

namespace signet::forge {

// ---------------------------------------------------------------------------
// QuantizationScheme -- identifies the quantization method
// ---------------------------------------------------------------------------

/// Identifies the quantization method used for vector compression.
///
/// @see QuantizationParams, Quantizer, Dequantizer
enum class QuantizationScheme : int32_t {
    SYMMETRIC_INT8   = 0,  ///< `value = round(float / scale)`, range [-127, 127].
    ASYMMETRIC_INT8  = 1,  ///< `value = round((float - zero_point) / scale)`, range [0, 255].
    SYMMETRIC_INT4   = 2   ///< `value = round(float / scale)`, range [-7, 7], nibble-packed.
};

// ---------------------------------------------------------------------------
// QuantizationParams -- parameters that fully describe a quantization mapping
// ---------------------------------------------------------------------------

/// Parameters that fully describe a quantization mapping.
///
/// Holds the scheme, scale factor, zero point, and vector dimension needed to
/// quantize float32 vectors to INT8/INT4 and dequantize them back.
///
/// @see Quantizer, Dequantizer, QuantizationScheme
struct QuantizationParams {
    QuantizationScheme scheme;              ///< Quantization scheme (symmetric/asymmetric, INT8/INT4).
    float              scale      = 0.0f;   ///< Scale factor (float units per quantization step).
    float              zero_point = 0.0f;   ///< Offset (used by ASYMMETRIC_INT8 only).
    uint32_t           dimension  = 0;      ///< Vector dimension (number of float elements).

    /// Compute optimal quantization parameters from a batch of vectors.
    ///
    /// Scans all float values to determine min/max, then derives scale
    /// (and zero_point for asymmetric) accordingly.
    ///
    /// @param data         Pointer to `num_vectors * dim` contiguous floats.
    /// @param num_vectors  Number of vectors in the batch.
    /// @param dim          Dimensionality of each vector.
    /// @param scheme       Desired quantization scheme.
    /// @return QuantizationParams ready for use with Quantizer/Dequantizer.
    [[nodiscard]] static inline QuantizationParams compute(
        const float* data,
        size_t       num_vectors,
        uint32_t     dim,
        QuantizationScheme scheme);

    /// Serialize to a compact key-value string for Parquet metadata.
    ///
    /// Format: `"scheme=0;scale=0.001234;zero_point=0.0;dimension=768"`.
    ///
    /// @return The serialized parameter string.
    [[nodiscard]] inline std::string serialize() const;

    /// Deserialize from the key-value string produced by serialize().
    ///
    /// @param s  The serialized parameter string.
    /// @return The parsed QuantizationParams, or an error if malformed.
    [[nodiscard]] static inline expected<QuantizationParams> deserialize(const std::string& s);

    /// Storage size per vector in bytes.
    ///
    /// - SYMMETRIC_INT8 / ASYMMETRIC_INT8: `dim` bytes.
    /// - SYMMETRIC_INT4: `ceil(dim / 2)` bytes.
    ///
    /// @return Number of bytes needed to store one quantized vector.
    [[nodiscard]] inline size_t bytes_per_vector() const;
};

// ---------------------------------------------------------------------------
// Quantizer -- quantize float32 vectors to int8 or int4
// ---------------------------------------------------------------------------

/// Quantizes float32 vectors to INT8 or INT4 representation.
///
/// Uses SIMD acceleration (AVX2, SSE2, or NEON) when available, with a
/// scalar fallback. INT4 quantization always uses the scalar path because
/// nibble packing makes SIMD less beneficial.
///
/// @see QuantizationParams, Dequantizer
class Quantizer {
public:
    /// Construct a quantizer with the given parameters.
    /// The params must have a valid scale (> 0) and dimension (> 0).
    explicit inline Quantizer(QuantizationParams params)
        : params_(std::move(params))
        , inv_scale_(params_.scale > 0.0f ? 1.0f / params_.scale : 0.0f) {}

    /// Quantize a single float32 vector into the output buffer.
    ///
    /// - INT8 schemes: `output` must hold at least `dim` bytes.
    /// - INT4 scheme: `output` must hold at least `ceil(dim/2)` bytes.
    ///
    /// @param input   Pointer to `dim` float32 values.
    /// @param output  Destination buffer for quantized bytes.
    inline void quantize(const float* input, uint8_t* output) const;

    /// Quantize a batch of vectors into a flat buffer of quantized bytes.
    ///
    /// @param input        Pointer to `num_vectors * dim` contiguous floats.
    /// @param num_vectors  Number of vectors to quantize.
    /// @return A flat byte buffer of size `num_vectors * bytes_per_vector()`.
    [[nodiscard]] inline std::vector<uint8_t> quantize_batch(
        const float* input, size_t num_vectors) const;

    /// Access the quantization parameters.
    [[nodiscard]] const QuantizationParams& params() const { return params_; }

private:
    QuantizationParams params_;
    float              inv_scale_;  // precomputed 1/scale for multiply-instead-of-divide

    // -- Scalar helpers --------------------------------------------------
    inline void quantize_symmetric_int8_scalar(const float* in, uint8_t* out, uint32_t dim) const;
    inline void quantize_asymmetric_int8_scalar(const float* in, uint8_t* out, uint32_t dim) const;
    inline void quantize_symmetric_int4_scalar(const float* in, uint8_t* out, uint32_t dim) const;

    // -- SIMD helpers (defined only when platform macros are set) ---------
#if defined(SIGNET_QUANT_AVX2)
    inline void quantize_symmetric_int8_avx2(const float* in, uint8_t* out, uint32_t dim) const;
    inline void quantize_asymmetric_int8_avx2(const float* in, uint8_t* out, uint32_t dim) const;
#elif defined(SIGNET_QUANT_SSE)
    inline void quantize_symmetric_int8_sse(const float* in, uint8_t* out, uint32_t dim) const;
    inline void quantize_asymmetric_int8_sse(const float* in, uint8_t* out, uint32_t dim) const;
#elif defined(SIGNET_QUANT_NEON)
    inline void quantize_symmetric_int8_neon(const float* in, uint8_t* out, uint32_t dim) const;
    inline void quantize_asymmetric_int8_neon(const float* in, uint8_t* out, uint32_t dim) const;
#endif
};

// ---------------------------------------------------------------------------
// Dequantizer -- dequantize int8/int4 back to float32
// ---------------------------------------------------------------------------

/// Dequantizes INT8/INT4 quantized vectors back to float32.
///
/// Uses SIMD acceleration (AVX2, SSE2, or NEON) when available, with a
/// scalar fallback.
///
/// @see QuantizationParams, Quantizer
class Dequantizer {
public:
    /// Construct a dequantizer with the given parameters.
    explicit inline Dequantizer(QuantizationParams params)
        : params_(std::move(params)) {}

    /// Dequantize a single quantized vector to float32.
    ///
    /// @param input   Pointer to `bytes_per_vector()` quantized bytes.
    /// @param output  Destination buffer; must hold at least `dim` floats.
    inline void dequantize(const uint8_t* input, float* output) const;

    /// Dequantize a batch of vectors, returning a vector-of-vectors.
    ///
    /// @param input        Pointer to `num_vectors * bytes_per_vector()` quantized bytes.
    /// @param num_vectors  Number of vectors to dequantize.
    /// @return A vector of float vectors, each of size `dim`.
    [[nodiscard]] inline std::vector<std::vector<float>> dequantize_batch(
        const uint8_t* input, size_t num_vectors) const;

    /// Flat batch dequantize: returns all floats in one contiguous buffer.
    ///
    /// @param input        Pointer to `num_vectors * bytes_per_vector()` quantized bytes.
    /// @param num_vectors  Number of vectors to dequantize.
    /// @return A flat float buffer of size `num_vectors * dim`.
    [[nodiscard]] inline std::vector<float> dequantize_flat(
        const uint8_t* input, size_t num_vectors) const;

    /// Access the quantization parameters.
    [[nodiscard]] const QuantizationParams& params() const { return params_; }

    /// EU AI Act Art.12 anomaly tracking: number of dequantized values that
    /// fell outside the representable quantization range and were clamped.
    [[nodiscard]] uint64_t anomaly_count() const { return anomaly_count_.load(std::memory_order_relaxed); }

private:
    QuantizationParams params_;
    mutable std::atomic<uint64_t> anomaly_count_{0}; ///< Count of out-of-range dequantized values (EU AI Act Art.12)

    // -- Scalar helpers --------------------------------------------------
    inline void dequantize_symmetric_int8_scalar(const uint8_t* in, float* out, uint32_t dim) const;
    inline void dequantize_asymmetric_int8_scalar(const uint8_t* in, float* out, uint32_t dim) const;
    inline void dequantize_symmetric_int4_scalar(const uint8_t* in, float* out, uint32_t dim) const;

    // -- SIMD helpers (defined only when platform macros are set) ---------
#if defined(SIGNET_QUANT_AVX2)
    inline void dequantize_symmetric_int8_avx2(const uint8_t* in, float* out, uint32_t dim) const;
    inline void dequantize_asymmetric_int8_avx2(const uint8_t* in, float* out, uint32_t dim) const;
#elif defined(SIGNET_QUANT_SSE)
    inline void dequantize_symmetric_int8_sse(const uint8_t* in, float* out, uint32_t dim) const;
    inline void dequantize_asymmetric_int8_sse(const uint8_t* in, float* out, uint32_t dim) const;
#elif defined(SIGNET_QUANT_NEON)
    inline void dequantize_symmetric_int8_neon(const uint8_t* in, float* out, uint32_t dim) const;
    inline void dequantize_asymmetric_int8_neon(const uint8_t* in, float* out, uint32_t dim) const;
#endif
};

// ---------------------------------------------------------------------------
// QuantizedVectorWriter -- accumulates float32 vectors, quantizes, and
// produces FIXED_LEN_BYTE_ARRAY page data for Parquet column chunks.
// ---------------------------------------------------------------------------

/// Accumulates float32 vectors, quantizes them, and produces
/// FIXED_LEN_BYTE_ARRAY page data suitable for Parquet column chunks.
///
/// @see QuantizedVectorReader, Quantizer, QuantizationParams
class QuantizedVectorWriter {
public:
    /// Construct a writer with the given quantization parameters.
    explicit inline QuantizedVectorWriter(QuantizationParams params)
        : quantizer_(params), num_vectors_(0) {}

    /// Add a single float32 vector (quantized internally).
    /// @param data  Pointer to `dim` floats.
    inline void add(const float* data);

    /// Add pre-quantized raw bytes for one vector.
    /// @param data  Pointer to `bytes_per_vector()` bytes.
    inline void add_raw(const uint8_t* data);

    /// Add a batch of float32 vectors (quantized internally).
    /// @param data         Pointer to `num_vectors * dim` contiguous floats.
    /// @param num_vectors  Number of vectors to add.
    inline void add_batch(const float* data, size_t num_vectors);

    /// Flush accumulated data as FIXED_LEN_BYTE_ARRAY page bytes.
    ///
    /// After flush, the writer is empty and ready for a new page.
    ///
    /// @return A byte buffer containing all buffered quantized vectors.
    [[nodiscard]] inline std::vector<uint8_t> flush();

    /// Number of vectors currently buffered.
    [[nodiscard]] size_t num_vectors() const { return num_vectors_; }

    /// Create a ColumnDescriptor suitable for a quantized vector column.
    ///
    /// Physical type: FIXED_LEN_BYTE_ARRAY. Logical type: FLOAT32_VECTOR.
    /// The quantization metadata is stored separately in key-value metadata.
    ///
    /// @param name    Column name.
    /// @param params  Quantization parameters (used to derive type_length).
    /// @return A ColumnDescriptor with type_length = bytes_per_vector().
    [[nodiscard]] static inline ColumnDescriptor make_descriptor(
        const std::string&         name,
        const QuantizationParams&  params);

    /// Access the quantization parameters.
    [[nodiscard]] const QuantizationParams& params() const {
        return quantizer_.params();
    }

private:
    Quantizer              quantizer_;
    std::vector<uint8_t>   buf_;
    size_t                 num_vectors_;
};

// ---------------------------------------------------------------------------
// QuantizedVectorReader -- reads quantized page data and dequantizes to
// float32 on demand.
// ---------------------------------------------------------------------------

/// Reads quantized page data (FIXED_LEN_BYTE_ARRAY) and dequantizes to
/// float32 on demand.
///
/// @see QuantizedVectorWriter, Dequantizer, QuantizationParams
class QuantizedVectorReader {
public:
    /// Construct a reader with the given quantization parameters.
    explicit inline QuantizedVectorReader(QuantizationParams params)
        : params_(std::move(params)), dequantizer_(params_) {}

    /// Read an entire page and dequantize all vectors to float32.
    /// @param data       Pointer to page data (FIXED_LEN_BYTE_ARRAY values).
    /// @param data_size  Size of the page data in bytes.
    /// @return Vector of dequantized float vectors.
    [[nodiscard]] inline std::vector<std::vector<float>> read_page(
        const uint8_t* data, size_t data_size);

    /// Read and dequantize a single vector by index within the page.
    /// @param page_data  Pointer to page data.
    /// @param page_size  Size of the page data in bytes.
    /// @param index      Zero-based vector index within the page.
    /// @return Dequantized float vector of size `dim`.
    [[nodiscard]] inline std::vector<float> read_vector(
        const uint8_t* page_data, size_t page_size, size_t index);

    /// Result of a raw (non-dequantized) page read.
    struct RawResult {
        const uint8_t* data;         ///< Pointer to quantized byte data (not owned).
        size_t         num_vectors;  ///< Number of vectors in the page.
    };

    /// Read raw quantized bytes without dequantization.
    ///
    /// @param data       Pointer to page data.
    /// @param data_size  Size of the page data in bytes.
    /// @return A RawResult pointing into the input buffer, or an error.
    [[nodiscard]] inline expected<RawResult> read_raw(
        const uint8_t* data, size_t data_size);

private:
    QuantizationParams params_;
    Dequantizer        dequantizer_;
};


// ===========================================================================
//
//  IMPLEMENTATION -- QuantizationParams
//
// ===========================================================================

inline QuantizationParams QuantizationParams::compute(
    const float*       data,
    size_t             num_vectors,
    uint32_t           dim,
    QuantizationScheme scheme)
{
    QuantizationParams p;
    p.scheme    = scheme;
    p.dimension = dim;

    if (num_vectors == 0 || dim == 0) {
        p.scale      = 1.0f;
        p.zero_point = 0.0f;
        return p;
    }

    const size_t total = num_vectors * static_cast<size_t>(dim);

    // Find min and max across all finite values (skip NaN/Infinity)
    // Parenthesized to prevent MSVC min/max macro expansion
    float vmin = (std::numeric_limits<float>::max)();
    float vmax = (std::numeric_limits<float>::lowest)();
    for (size_t i = 0; i < total; ++i) {
        const float v = data[i];
        if (!std::isfinite(v)) continue;
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    // If no finite values found, fall back to safe defaults
    if (vmin > vmax) {
        p.scale      = 1.0f;
        p.zero_point = 0.0f;
        return p;
    }

    switch (scheme) {
    case QuantizationScheme::SYMMETRIC_INT8: {
        // scale = max(|min|, |max|) / 127
        const float abs_max = (std::max)(std::fabs(vmin), std::fabs(vmax));
        p.scale      = (abs_max > 0.0f) ? (abs_max / 127.0f) : 1.0f;
        p.zero_point = 0.0f;
        break;
    }
    case QuantizationScheme::ASYMMETRIC_INT8: {
        // scale = (max - min) / 255,  zero_point = min
        const float range = vmax - vmin;
        p.scale      = (range > 0.0f) ? (range / 255.0f) : 1.0f;
        p.zero_point = vmin;
        break;
    }
    case QuantizationScheme::SYMMETRIC_INT4: {
        // scale = max(|min|, |max|) / 7
        const float abs_max = (std::max)(std::fabs(vmin), std::fabs(vmax));
        p.scale      = (abs_max > 0.0f) ? (abs_max / 7.0f) : 1.0f;
        p.zero_point = 0.0f;
        break;
    }
    }

    // MiFID II Annex I Field 6: verify that quantization scale preserves
    // required price precision for regulated instruments.
    // The smallest representable delta equals `scale`. If this exceeds the
    // instrument tick size, quantized prices may violate precision requirements.
    // Users embedding regulated price data should confirm scale < tick_size.

    return p;
}

inline std::string QuantizationParams::serialize() const {
    // Format: scheme=N;scale=F;zero_point=F;dimension=N
    std::string s;
    s += "scheme=";
    s += std::to_string(static_cast<int32_t>(scheme));
    s += ";scale=";
    { char buf[32]; std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(scale)); s += buf; }
    s += ";zero_point=";
    { char buf[32]; std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(zero_point)); s += buf; }
    s += ";dimension=";
    s += std::to_string(dimension);
    return s;
}

inline expected<QuantizationParams> QuantizationParams::deserialize(const std::string& s) {
    QuantizationParams p;

    // Parse semicolon-delimited key=value pairs
    bool got_scheme = false, got_scale = false, got_dim = false;

    size_t pos = 0;
    while (pos < s.size()) {
        // Find '='
        size_t eq = s.find('=', pos);
        if (eq == std::string::npos) break;

        std::string key = s.substr(pos, eq - pos);

        // Find ';' or end
        size_t semi = s.find(';', eq + 1);
        std::string val;
        if (semi == std::string::npos) {
            val = s.substr(eq + 1);
            pos = s.size();
        } else {
            val = s.substr(eq + 1, semi - eq - 1);
            pos = semi + 1;
        }

        if (key == "scheme") {
            try {
                int32_t v = std::stoi(val);
                if (v < 0 || v > 2) {
                    return Error{ErrorCode::INVALID_FILE,
                                 "quantization params: invalid scheme value"};
                }
                p.scheme = static_cast<QuantizationScheme>(v);
                got_scheme = true;
            } catch (...) {
                return Error{ErrorCode::INVALID_FILE,
                             "quantization params: malformed scheme"};
            }
        } else if (key == "scale") {
            try {
                p.scale = std::stof(val);
                got_scale = true;
            } catch (...) {
                return Error{ErrorCode::INVALID_FILE,
                             "quantization params: malformed scale"};
            }
        } else if (key == "zero_point") {
            try {
                p.zero_point = std::stof(val);
            } catch (...) {
                return Error{ErrorCode::INVALID_FILE,
                             "quantization params: malformed zero_point"};
            }
        } else if (key == "dimension") {
            try {
                int v = std::stoi(val);
                if (v <= 0) {
                    return Error{ErrorCode::INVALID_FILE,
                                 "quantization params: dimension must be positive"};
                }
                p.dimension = static_cast<uint32_t>(v);
                got_dim = true;
            } catch (...) {
                return Error{ErrorCode::INVALID_FILE,
                             "quantization params: malformed dimension"};
            }
        }
        // Unknown keys are silently ignored for forward compatibility
    }

    if (!got_scheme || !got_scale || !got_dim) {
        return Error{ErrorCode::INVALID_FILE,
                     "quantization params: missing required field(s)"};
    }

    return p;
}

inline size_t QuantizationParams::bytes_per_vector() const {
    switch (scheme) {
    case QuantizationScheme::SYMMETRIC_INT8:
    case QuantizationScheme::ASYMMETRIC_INT8:
        return static_cast<size_t>(dimension);
    case QuantizationScheme::SYMMETRIC_INT4:
        return (static_cast<size_t>(dimension) + 1) / 2;
    }
    return static_cast<size_t>(dimension); // unreachable, silence warnings
}


// ===========================================================================
//
//  IMPLEMENTATION -- Quantizer (scalar paths)
//
// ===========================================================================

inline void Quantizer::quantize_symmetric_int8_scalar(
    const float* in, uint8_t* out, uint32_t dim) const
{
    for (uint32_t i = 0; i < dim; ++i) {
        float scaled = std::nearbyintf(in[i] * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, -127, 127);
        // Store as int8_t reinterpreted to uint8_t
        out[i] = static_cast<uint8_t>(static_cast<int8_t>(q));
    }
}

inline void Quantizer::quantize_asymmetric_int8_scalar(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const float zp = params_.zero_point;
    for (uint32_t i = 0; i < dim; ++i) {
        float scaled = std::nearbyintf((in[i] - zp) * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, 0, 255);
        out[i] = static_cast<uint8_t>(q);
    }
}

inline void Quantizer::quantize_symmetric_int4_scalar(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const size_t packed_len = (static_cast<size_t>(dim) + 1) / 2;
    std::memset(out, 0, packed_len);

    for (uint32_t i = 0; i < dim; ++i) {
        float scaled = std::nearbyintf(in[i] * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, -7, 7);

        // Encode as 4-bit two's complement: value & 0xF
        uint8_t nibble = static_cast<uint8_t>(q & 0x0F);
        uint32_t byte_idx = i / 2;

        if ((i & 1) == 0) {
            // Even index -> high nibble
            out[byte_idx] |= static_cast<uint8_t>(nibble << 4);
        } else {
            // Odd index -> low nibble
            out[byte_idx] |= nibble;
        }
    }
}


// ===========================================================================
//
//  IMPLEMENTATION -- Quantizer (SIMD paths)
//
// ===========================================================================

#if defined(SIGNET_QUANT_AVX2)

inline void Quantizer::quantize_symmetric_int8_avx2(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const __m256 vscale = _mm256_set1_ps(inv_scale_);
    const __m256 vmin   = _mm256_set1_ps(-127.0f);
    const __m256 vmax   = _mm256_set1_ps(127.0f);

    uint32_t i = 0;
    for (; i + 8 <= dim; i += 8) {
        __m256 vf = _mm256_loadu_ps(in + i);
        vf = _mm256_mul_ps(vf, vscale);
        vf = _mm256_round_ps(vf, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
        vf = _mm256_max_ps(vf, vmin);
        vf = _mm256_min_ps(vf, vmax);

        // Convert to int32
        __m256i vi32 = _mm256_cvtps_epi32(vf);

        // Pack int32 -> int16 -> int8 (with saturation)
        // Extract 128-bit halves
        __m128i lo = _mm256_castsi256_si128(vi32);
        __m128i hi = _mm256_extracti128_si256(vi32, 1);
        __m128i vi16 = _mm_packs_epi32(lo, hi);
        __m128i vi8  = _mm_packs_epi16(vi16, vi16);

        // Store lower 8 bytes
        // Note: _mm_packs_epi16 with itself duplicates; we only need lower 8
        uint64_t packed;
        std::memcpy(&packed, &vi8, 8);
        std::memcpy(out + i, &packed, 8);
    }

    // Scalar tail
    for (; i < dim; ++i) {
        float scaled = std::nearbyintf(in[i] * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, -127, 127);
        out[i] = static_cast<uint8_t>(static_cast<int8_t>(q));
    }
}

inline void Quantizer::quantize_asymmetric_int8_avx2(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const __m256 vscale = _mm256_set1_ps(inv_scale_);
    const __m256 vzp    = _mm256_set1_ps(params_.zero_point);
    const __m256 vmin   = _mm256_set1_ps(0.0f);
    const __m256 vmax   = _mm256_set1_ps(255.0f);

    uint32_t i = 0;
    for (; i + 8 <= dim; i += 8) {
        __m256 vf = _mm256_loadu_ps(in + i);
        vf = _mm256_sub_ps(vf, vzp);
        vf = _mm256_mul_ps(vf, vscale);
        vf = _mm256_round_ps(vf, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
        vf = _mm256_max_ps(vf, vmin);
        vf = _mm256_min_ps(vf, vmax);

        __m256i vi32 = _mm256_cvtps_epi32(vf);

        // Pack int32 -> uint16 -> uint8 (unsigned saturation)
        __m128i lo = _mm256_castsi256_si128(vi32);
        __m128i hi = _mm256_extracti128_si256(vi32, 1);
        __m128i vi16 = _mm_packus_epi32(lo, hi);
        __m128i vi8  = _mm_packus_epi16(vi16, vi16);

        uint64_t packed;
        std::memcpy(&packed, &vi8, 8);
        std::memcpy(out + i, &packed, 8);
    }

    // Scalar tail
    const float zp = params_.zero_point;
    for (; i < dim; ++i) {
        float scaled = std::nearbyintf((in[i] - zp) * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, 0, 255);
        out[i] = static_cast<uint8_t>(q);
    }
}

#elif defined(SIGNET_QUANT_SSE)

inline void Quantizer::quantize_symmetric_int8_sse(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const __m128 vscale = _mm_set1_ps(inv_scale_);
    const __m128 vmin   = _mm_set1_ps(-127.0f);
    const __m128 vmax   = _mm_set1_ps(127.0f);

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        __m128 vf = _mm_loadu_ps(in + i);
        vf = _mm_mul_ps(vf, vscale);
        // Clamp before cvt; cvtps_epi32 rounds to nearest (default FP mode)
        vf = _mm_max_ps(vf, vmin);
        vf = _mm_min_ps(vf, vmax);

        __m128i vi32 = _mm_cvtps_epi32(vf);
        __m128i vi16 = _mm_packs_epi32(vi32, vi32);
        __m128i vi8  = _mm_packs_epi16(vi16, vi16);

        uint32_t packed;
        std::memcpy(&packed, &vi8, 4);
        std::memcpy(out + i, &packed, 4);
    }

    for (; i < dim; ++i) {
        float scaled = std::nearbyintf(in[i] * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, -127, 127);
        out[i] = static_cast<uint8_t>(static_cast<int8_t>(q));
    }
}

inline void Quantizer::quantize_asymmetric_int8_sse(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const __m128 vscale = _mm_set1_ps(inv_scale_);
    const __m128 vzp    = _mm_set1_ps(params_.zero_point);
    const __m128 vmin   = _mm_set1_ps(0.0f);
    const __m128 vmax   = _mm_set1_ps(255.0f);

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        __m128 vf = _mm_loadu_ps(in + i);
        vf = _mm_sub_ps(vf, vzp);
        vf = _mm_mul_ps(vf, vscale);
        // Clamp before cvt; cvtps_epi32 rounds to nearest (default FP mode)
        vf = _mm_max_ps(vf, vmin);
        vf = _mm_min_ps(vf, vmax);

        __m128i vi32 = _mm_cvtps_epi32(vf);

        // Pack int32 -> uint8 via saturation (SSE2-compatible path)
        // packs_epi32 -> packs_epi16 gives signed int16 with saturation
        // Then packus_epi16 gives unsigned uint8 with saturation
        __m128i vi16 = _mm_packs_epi32(vi32, vi32);
        __m128i vi8  = _mm_packus_epi16(vi16, vi16);

        uint32_t packed;
        std::memcpy(&packed, &vi8, 4);
        std::memcpy(out + i, &packed, 4);
    }

    const float zp = params_.zero_point;
    for (; i < dim; ++i) {
        float scaled = std::nearbyintf((in[i] - zp) * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, 0, 255);
        out[i] = static_cast<uint8_t>(q);
    }
}

#elif defined(SIGNET_QUANT_NEON)

inline void Quantizer::quantize_symmetric_int8_neon(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const float32x4_t vscale = vdupq_n_f32(inv_scale_);

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        float32x4_t vf = vld1q_f32(in + i);
        vf = vmulq_f32(vf, vscale);
        vf = vrndnq_f32(vf);   // round to nearest

        // Clamp to [-127, 127]
        vf = vmaxq_f32(vf, vdupq_n_f32(-127.0f));
        vf = vminq_f32(vf, vdupq_n_f32(127.0f));

        int32x4_t vi32 = vcvtq_s32_f32(vf);
        int16x4_t vi16 = vmovn_s32(vi32);
        int8x8_t  vi8  = vmovn_s16(vcombine_s16(vi16, vi16));

        // Store lower 4 bytes
        vst1_lane_u32(reinterpret_cast<uint32_t*>(out + i),
                       vreinterpret_u32_s8(vi8), 0);
    }

    for (; i < dim; ++i) {
        float scaled = std::nearbyintf(in[i] * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, -127, 127);
        out[i] = static_cast<uint8_t>(static_cast<int8_t>(q));
    }
}

inline void Quantizer::quantize_asymmetric_int8_neon(
    const float* in, uint8_t* out, uint32_t dim) const
{
    const float32x4_t vscale = vdupq_n_f32(inv_scale_);
    const float32x4_t vzp    = vdupq_n_f32(params_.zero_point);

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        float32x4_t vf = vld1q_f32(in + i);
        vf = vsubq_f32(vf, vzp);
        vf = vmulq_f32(vf, vscale);
        vf = vrndnq_f32(vf);

        // Clamp to [0, 255]
        vf = vmaxq_f32(vf, vdupq_n_f32(0.0f));
        vf = vminq_f32(vf, vdupq_n_f32(255.0f));

        uint32x4_t vu32 = vcvtq_u32_f32(vf);
        uint16x4_t vu16 = vmovn_u32(vu32);
        uint8x8_t  vu8  = vmovn_u16(vcombine_u16(vu16, vu16));

        vst1_lane_u32(reinterpret_cast<uint32_t*>(out + i),
                       vreinterpret_u32_u8(vu8), 0);
    }

    const float zp = params_.zero_point;
    for (; i < dim; ++i) {
        float scaled = std::nearbyintf((in[i] - zp) * inv_scale_);
        int32_t q = static_cast<int32_t>(scaled);
        q = std::clamp(q, 0, 255);
        out[i] = static_cast<uint8_t>(q);
    }
}

#endif // SIMD quantize helpers


// ---------------------------------------------------------------------------
// Quantizer::quantize -- dispatch to best available path
// ---------------------------------------------------------------------------
inline void Quantizer::quantize(const float* input, uint8_t* output) const {
    const uint32_t dim = params_.dimension;

    switch (params_.scheme) {
    case QuantizationScheme::SYMMETRIC_INT8:
#if defined(SIGNET_QUANT_AVX2)
        quantize_symmetric_int8_avx2(input, output, dim);
#elif defined(SIGNET_QUANT_SSE)
        quantize_symmetric_int8_sse(input, output, dim);
#elif defined(SIGNET_QUANT_NEON)
        quantize_symmetric_int8_neon(input, output, dim);
#else
        quantize_symmetric_int8_scalar(input, output, dim);
#endif
        break;

    case QuantizationScheme::ASYMMETRIC_INT8:
#if defined(SIGNET_QUANT_AVX2)
        quantize_asymmetric_int8_avx2(input, output, dim);
#elif defined(SIGNET_QUANT_SSE)
        quantize_asymmetric_int8_sse(input, output, dim);
#elif defined(SIGNET_QUANT_NEON)
        quantize_asymmetric_int8_neon(input, output, dim);
#else
        quantize_asymmetric_int8_scalar(input, output, dim);
#endif
        break;

    case QuantizationScheme::SYMMETRIC_INT4:
        // INT4 is always scalar (nibble packing makes SIMD less beneficial)
        quantize_symmetric_int4_scalar(input, output, dim);
        break;
    }
}

inline std::vector<uint8_t> Quantizer::quantize_batch(
    const float* input, size_t num_vectors) const
{
    const size_t bpv = params_.bytes_per_vector();
    std::vector<uint8_t> result(num_vectors * bpv);

    for (size_t v = 0; v < num_vectors; ++v) {
        quantize(input + v * params_.dimension,
                 result.data() + v * bpv);
    }

    return result;
}


// ===========================================================================
//
//  IMPLEMENTATION -- Dequantizer (scalar paths)
//
// ===========================================================================

inline void Dequantizer::dequantize_symmetric_int8_scalar(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const float s = params_.scale;
    const float range_max =  127.0f * s;
    const float range_min = -127.0f * s;
    for (uint32_t i = 0; i < dim; ++i) {
        int8_t q = static_cast<int8_t>(in[i]);
        out[i] = static_cast<float>(q) * s;
        // Clamp to quantization range bounds (EU AI Act Art.12 anomaly tracking)
        if (out[i] < range_min || out[i] > range_max) {
            out[i] = std::clamp(out[i], range_min, range_max);
            anomaly_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

inline void Dequantizer::dequantize_asymmetric_int8_scalar(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const float s  = params_.scale;
    const float zp = params_.zero_point;
    const float range_min = zp;                // 0 * s + zp
    const float range_max = 255.0f * s + zp;   // 255 * s + zp
    for (uint32_t i = 0; i < dim; ++i) {
        out[i] = static_cast<float>(in[i]) * s + zp;
        // Clamp to quantization range bounds (EU AI Act Art.12 anomaly tracking)
        if (out[i] < range_min || out[i] > range_max) {
            out[i] = std::clamp(out[i], range_min, range_max);
            anomaly_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

inline void Dequantizer::dequantize_symmetric_int4_scalar(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const float s = params_.scale;
    const float range_max =  7.0f * s;
    const float range_min = -7.0f * s;
    for (uint32_t i = 0; i < dim; ++i) {
        uint32_t byte_idx = i / 2;
        uint8_t nibble;
        if ((i & 1) == 0) {
            // Even index -> high nibble
            nibble = (in[byte_idx] >> 4) & 0x0F;
        } else {
            // Odd index -> low nibble
            nibble = in[byte_idx] & 0x0F;
        }

        // Sign-extend 4-bit two's complement to int8 (CWE-194).
        // Portable, branchless, defined behavior per C++20 standard:
        //   XOR with 0x08 flips the sign bit, subtract 0x08 restores offset.
        int8_t signed_val = static_cast<int8_t>((nibble ^ 0x08) - 0x08);

        out[i] = static_cast<float>(signed_val) * s;
        // Clamp to quantization range bounds (EU AI Act Art.12 anomaly tracking)
        if (out[i] < range_min || out[i] > range_max) {
            out[i] = std::clamp(out[i], range_min, range_max);
            anomaly_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}


// ===========================================================================
//
//  IMPLEMENTATION -- Dequantizer (SIMD paths)
//
// ===========================================================================

#if defined(SIGNET_QUANT_AVX2)

inline void Dequantizer::dequantize_symmetric_int8_avx2(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const __m256 vscale = _mm256_set1_ps(params_.scale);

    uint32_t i = 0;
    for (; i + 8 <= dim; i += 8) {
        // Load 8 int8 values, sign-extend to int32, convert to float
        // Use a 64-bit load into low half of __m128i
        __m128i raw = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(in + i));

        // Sign-extend int8 -> int16 (lower 8 bytes)
        __m128i vi16 = _mm_cvtepi8_epi16(raw);

        // Sign-extend int16 -> int32 (lower 4 and upper 4 separately)
        __m128i lo32 = _mm_cvtepi16_epi32(vi16);
        __m128i hi16 = _mm_unpackhi_epi64(vi16, vi16);
        __m128i hi32 = _mm_cvtepi16_epi32(hi16);

        // Combine into 256-bit
        __m256i vi32 = _mm256_set_m128i(hi32, lo32);

        // Convert int32 -> float and multiply by scale
        __m256 vf = _mm256_cvtepi32_ps(vi32);
        vf = _mm256_mul_ps(vf, vscale);

        _mm256_storeu_ps(out + i, vf);
    }

    // Scalar tail
    const float s = params_.scale;
    for (; i < dim; ++i) {
        int8_t q = static_cast<int8_t>(in[i]);
        out[i] = static_cast<float>(q) * s;
    }
}

inline void Dequantizer::dequantize_asymmetric_int8_avx2(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const __m256 vscale = _mm256_set1_ps(params_.scale);
    const __m256 vzp    = _mm256_set1_ps(params_.zero_point);

    uint32_t i = 0;
    for (; i + 8 <= dim; i += 8) {
        // Load 8 uint8 values, zero-extend to int32
        __m128i raw = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(in + i));
        __m128i vu16 = _mm_cvtepu8_epi16(raw);

        __m128i lo32 = _mm_cvtepu16_epi32(vu16);
        __m128i hi16 = _mm_unpackhi_epi64(vu16, vu16);
        __m128i hi32 = _mm_cvtepu16_epi32(hi16);

        __m256i vu32 = _mm256_set_m128i(hi32, lo32);
        __m256 vf = _mm256_cvtepi32_ps(vu32);

        // dequantized = value * scale + zero_point
        vf = _mm256_fmadd_ps(vf, vscale, vzp);

        _mm256_storeu_ps(out + i, vf);
    }

    const float s  = params_.scale;
    const float zp = params_.zero_point;
    for (; i < dim; ++i) {
        out[i] = static_cast<float>(in[i]) * s + zp;
    }
}

#elif defined(SIGNET_QUANT_SSE)

inline void Dequantizer::dequantize_symmetric_int8_sse(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const __m128 vscale = _mm_set1_ps(params_.scale);

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        // Load 4 int8 values into the lowest 32 bits
        int32_t raw32;
        std::memcpy(&raw32, in + i, 4);
        __m128i raw = _mm_cvtsi32_si128(raw32);

        // Sign-extend int8 -> int16 (SSE2: unpacklo with zero, then fix sign)
        __m128i vi16 = _mm_unpacklo_epi8(raw, raw);       // duplicate bytes
        vi16 = _mm_srai_epi16(vi16, 8);                   // arithmetic shift right by 8

        // Sign-extend int16 -> int32 (SSE2: unpacklo with sign-extension)
        __m128i sign = _mm_srai_epi16(vi16, 15);           // all-1s or all-0s per lane
        __m128i vi32 = _mm_unpacklo_epi16(vi16, sign);

        __m128 vf = _mm_cvtepi32_ps(vi32);
        vf = _mm_mul_ps(vf, vscale);

        _mm_storeu_ps(out + i, vf);
    }

    const float s = params_.scale;
    for (; i < dim; ++i) {
        int8_t q = static_cast<int8_t>(in[i]);
        out[i] = static_cast<float>(q) * s;
    }
}

inline void Dequantizer::dequantize_asymmetric_int8_sse(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const __m128 vscale = _mm_set1_ps(params_.scale);
    const __m128 vzp    = _mm_set1_ps(params_.zero_point);
    const __m128i vzero = _mm_setzero_si128();

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        int32_t raw32;
        std::memcpy(&raw32, in + i, 4);
        __m128i raw = _mm_cvtsi32_si128(raw32);

        // Zero-extend uint8 -> uint16 -> int32 (SSE2)
        __m128i vu16 = _mm_unpacklo_epi8(raw, vzero);
        __m128i vu32 = _mm_unpacklo_epi16(vu16, vzero);

        __m128 vf = _mm_cvtepi32_ps(vu32);
        // dequantized = value * scale + zero_point
        vf = _mm_add_ps(_mm_mul_ps(vf, vscale), vzp);

        _mm_storeu_ps(out + i, vf);
    }

    const float s  = params_.scale;
    const float zp = params_.zero_point;
    for (; i < dim; ++i) {
        out[i] = static_cast<float>(in[i]) * s + zp;
    }
}

#elif defined(SIGNET_QUANT_NEON)

inline void Dequantizer::dequantize_symmetric_int8_neon(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const float32x4_t vscale = vdupq_n_f32(params_.scale);

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        // Load 4 int8 values
        int8x8_t raw8 = vreinterpret_s8_u32(
            vld1_dup_u32(reinterpret_cast<const uint32_t*>(in + i)));

        // Widen int8 -> int16 -> int32
        int16x8_t vi16 = vmovl_s8(raw8);
        int32x4_t vi32 = vmovl_s16(vget_low_s16(vi16));

        float32x4_t vf = vcvtq_f32_s32(vi32);
        vf = vmulq_f32(vf, vscale);

        vst1q_f32(out + i, vf);
    }

    const float s = params_.scale;
    for (; i < dim; ++i) {
        int8_t q = static_cast<int8_t>(in[i]);
        out[i] = static_cast<float>(q) * s;
    }
}

inline void Dequantizer::dequantize_asymmetric_int8_neon(
    const uint8_t* in, float* out, uint32_t dim) const
{
    const float32x4_t vscale = vdupq_n_f32(params_.scale);
    const float32x4_t vzp    = vdupq_n_f32(params_.zero_point);

    uint32_t i = 0;
    for (; i + 4 <= dim; i += 4) {
        uint8x8_t raw8 = vreinterpret_u8_u32(
            vld1_dup_u32(reinterpret_cast<const uint32_t*>(in + i)));

        uint16x8_t vu16 = vmovl_u8(raw8);
        uint32x4_t vu32 = vmovl_u16(vget_low_u16(vu16));

        float32x4_t vf = vcvtq_f32_u32(vu32);
        vf = vmlaq_f32(vzp, vf, vscale);  // vf = vf * scale + zero_point

        vst1q_f32(out + i, vf);
    }

    const float s  = params_.scale;
    const float zp = params_.zero_point;
    for (; i < dim; ++i) {
        out[i] = static_cast<float>(in[i]) * s + zp;
    }
}

#endif // SIMD dequantize helpers


// ---------------------------------------------------------------------------
// Dequantizer::dequantize -- dispatch to best available path
// ---------------------------------------------------------------------------
inline void Dequantizer::dequantize(const uint8_t* input, float* output) const {
    const uint32_t dim = params_.dimension;

    switch (params_.scheme) {
    case QuantizationScheme::SYMMETRIC_INT8:
#if defined(SIGNET_QUANT_AVX2)
        dequantize_symmetric_int8_avx2(input, output, dim);
#elif defined(SIGNET_QUANT_SSE)
        dequantize_symmetric_int8_sse(input, output, dim);
#elif defined(SIGNET_QUANT_NEON)
        dequantize_symmetric_int8_neon(input, output, dim);
#else
        dequantize_symmetric_int8_scalar(input, output, dim);
#endif
        break;

    case QuantizationScheme::ASYMMETRIC_INT8:
#if defined(SIGNET_QUANT_AVX2)
        dequantize_asymmetric_int8_avx2(input, output, dim);
#elif defined(SIGNET_QUANT_SSE)
        dequantize_asymmetric_int8_sse(input, output, dim);
#elif defined(SIGNET_QUANT_NEON)
        dequantize_asymmetric_int8_neon(input, output, dim);
#else
        dequantize_asymmetric_int8_scalar(input, output, dim);
#endif
        break;

    case QuantizationScheme::SYMMETRIC_INT4:
        dequantize_symmetric_int4_scalar(input, output, dim);
        break;
    }
}

inline std::vector<std::vector<float>> Dequantizer::dequantize_batch(
    const uint8_t* input, size_t num_vectors) const
{
    const size_t bpv = params_.bytes_per_vector();
    const uint32_t dim = params_.dimension;

    std::vector<std::vector<float>> result(num_vectors);

    for (size_t v = 0; v < num_vectors; ++v) {
        result[v].resize(dim);
        dequantize(input + v * bpv, result[v].data());
    }

    return result;
}

inline std::vector<float> Dequantizer::dequantize_flat(
    const uint8_t* input, size_t num_vectors) const
{
    const size_t bpv = params_.bytes_per_vector();
    const uint32_t dim = params_.dimension;

    std::vector<float> result(num_vectors * dim);

    for (size_t v = 0; v < num_vectors; ++v) {
        dequantize(input + v * bpv, result.data() + v * dim);
    }

    return result;
}


// ===========================================================================
//
//  IMPLEMENTATION -- QuantizedVectorWriter
//
// ===========================================================================

inline void QuantizedVectorWriter::add(const float* data) {
    const size_t bpv = quantizer_.params().bytes_per_vector();
    const size_t offset = buf_.size();
    buf_.resize(offset + bpv);
    quantizer_.quantize(data, buf_.data() + offset);
    ++num_vectors_;
}

inline void QuantizedVectorWriter::add_raw(const uint8_t* data) {
    const size_t bpv = quantizer_.params().bytes_per_vector();
    buf_.insert(buf_.end(), data, data + bpv);
    ++num_vectors_;
}

inline void QuantizedVectorWriter::add_batch(const float* data, size_t num_vectors) {
    const uint32_t dim = quantizer_.params().dimension;
    const size_t bpv = quantizer_.params().bytes_per_vector();
    const size_t offset = buf_.size();
    buf_.resize(offset + num_vectors * bpv);

    for (size_t v = 0; v < num_vectors; ++v) {
        quantizer_.quantize(data + v * dim,
                            buf_.data() + offset + v * bpv);
    }

    num_vectors_ += num_vectors;
}

inline std::vector<uint8_t> QuantizedVectorWriter::flush() {
    std::vector<uint8_t> result = std::move(buf_);
    buf_.clear();
    num_vectors_ = 0;
    return result;
}

inline ColumnDescriptor QuantizedVectorWriter::make_descriptor(
    const std::string&        name,
    const QuantizationParams& params)
{
    ColumnDescriptor cd;
    cd.name          = name;
    cd.physical_type = PhysicalType::FIXED_LEN_BYTE_ARRAY;
    cd.logical_type  = LogicalType::FLOAT32_VECTOR;
    cd.repetition    = Repetition::REQUIRED;
    cd.type_length   = static_cast<int32_t>(params.bytes_per_vector());
    return cd;
}


// ===========================================================================
//
//  IMPLEMENTATION -- QuantizedVectorReader
//
// ===========================================================================

inline std::vector<std::vector<float>> QuantizedVectorReader::read_page(
    const uint8_t* data, size_t data_size)
{
    const size_t bpv = params_.bytes_per_vector();
    if (bpv == 0) return {};

    if (data_size % bpv != 0) {
        // Tail bytes that don't form a complete vector are silently dropped.
        // Use read_raw() for strict validation.
    }

    const size_t num = data_size / bpv;
    return dequantizer_.dequantize_batch(data, num);
}

inline std::vector<float> QuantizedVectorReader::read_vector(
    const uint8_t* page_data, size_t page_size, size_t index)
{
    const size_t bpv = params_.bytes_per_vector();
    const size_t offset = index * bpv;

    std::vector<float> result(params_.dimension);

    if (offset + bpv > page_size) {
        // Out of bounds -- return zero vector
        return result;
    }

    dequantizer_.dequantize(page_data + offset, result.data());
    return result;
}

inline expected<QuantizedVectorReader::RawResult> QuantizedVectorReader::read_raw(
    const uint8_t* data, size_t data_size)
{
    const size_t bpv = params_.bytes_per_vector();
    if (bpv == 0) {
        return Error{ErrorCode::INTERNAL_ERROR,
                     "quantized vector: bytes_per_vector is zero"};
    }

    if (data_size % bpv != 0) {
        return Error{ErrorCode::CORRUPT_PAGE,
                     "quantized vector page size is not a multiple of bytes_per_vector"};
    }

    RawResult r;
    r.data        = data;
    r.num_vectors = data_size / bpv;
    return r;
}

} // namespace signet::forge
