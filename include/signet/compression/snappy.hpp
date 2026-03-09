// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file snappy.hpp
/// @brief Bundled, zero-dependency, header-only Snappy compression codec.
///
/// Implements the Snappy framing-free compression format as specified at:
///   https://github.com/google/snappy/blob/main/format_description.txt
///
/// This is a clean-room implementation providing correct Snappy compress and
/// decompress for use in Parquet files, where Snappy is the most commonly
/// used compression codec. The compressor is deliberately simple (single-pass,
/// greedy hash-chain matching) and optimized for correctness over speed.
///
/// Wire format summary:
///   @code [varint: uncompressed_length] [element]... @endcode
///
/// Element types (low 2 bits of tag byte):
///   - 00 = Literal -- Copy raw bytes into output
///   - 01 = Copy-1  -- Short back-reference (offset up to 2047, length 4-11)
///   - 02 = Copy-2  -- Medium back-reference (offset up to 65535, length 1-64)
///   - 03 = Copy-4  -- Long back-reference (offset up to 2^32-1, length 1-64)
///
/// @see CompressionCodec, CodecRegistry

#include "signet/compression/codec.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace signet::forge {

// ===========================================================================
// Internal helpers -- not part of the public API
// ===========================================================================
namespace detail::snappy {

// ---- Varint encoding/decoding (unsigned, up to 32-bit) --------------------

/// Encode a 32-bit unsigned integer as a Snappy varint (1-5 bytes).
/// Returns the number of bytes written.
inline size_t encode_varint32(uint8_t* dst, uint32_t value) {
    size_t n = 0;
    while (value >= 0x80) {
        dst[n++] = static_cast<uint8_t>(value | 0x80);
        value >>= 7;
    }
    dst[n++] = static_cast<uint8_t>(value);
    return n;
}

/// Decode a Snappy varint from the input stream.
/// On success, advances `pos` past the varint and writes the value to `out`.
/// Returns false if the varint is truncated or exceeds 32 bits.
inline bool decode_varint32(const uint8_t* data, size_t size, size_t& pos,
                            uint32_t& out) {
    uint32_t result = 0;
    uint32_t shift  = 0;
    for (int i = 0; i < 5; ++i) {
        if (pos >= size) return false;
        uint8_t byte = data[pos++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            out = result;
            return true;
        }
        shift += 7;
    }
    // More than 5 bytes -- invalid for a 32-bit varint.
    return false;
}

// ---- Little-endian helpers ------------------------------------------------

/// Read a 32-bit little-endian value from a potentially unaligned pointer.
inline uint32_t load_le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    return v;
}

/// Read a 16-bit little-endian value from a potentially unaligned pointer.
inline uint16_t load_le16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap16(v);
#endif
    return v;
}

/// Write a 16-bit little-endian value.
inline void store_le16(uint8_t* p, uint16_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap16(v);
#endif
    std::memcpy(p, &v, 2);
}

/// Write a 32-bit little-endian value.
inline void store_le32(uint8_t* p, uint32_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = __builtin_bswap32(v);
#endif
    std::memcpy(p, &v, 4);
}

// ---- Hash function for the compressor ------------------------------------

/// 14-bit hash of 4 bytes read as a little-endian uint32. The constant and
/// shift are chosen to distribute typical byte patterns across the 16384-entry
/// hash table.
inline uint32_t hash4(const uint8_t* p) {
    uint32_t val = load_le32(p);
    return (val * 0x1e35a7bd) >> 18; // Result is in [0, 16383]
}

// Hash table size: 2^14 = 16384 entries.
static constexpr size_t kHashTableSize = 16384;
static constexpr uint32_t kHashTableMask = kHashTableSize - 1;

// ---- Literal / copy element emitters (compressor) -------------------------

/// Emit a literal element. `data` points to the literal bytes, `length` is
/// the number of bytes (must be >= 1).
inline void emit_literal(std::vector<uint8_t>& out,
                         const uint8_t* data, size_t length) {
    // Tag byte encoding depends on literal length.
    if (length <= 60) {
        // Lengths 1-60: tag encodes (length-1) in the upper 6 bits.
        out.push_back(static_cast<uint8_t>((length - 1) << 2 | 0));
    } else if (length <= 256) {
        // Lengths 61-256: tag = (60 << 2 | 0), followed by 1 byte of (length-1).
        out.push_back(static_cast<uint8_t>(60 << 2 | 0));
        out.push_back(static_cast<uint8_t>(length - 1));
    } else if (length <= 65536) {
        // Lengths 257-65536: tag = (61 << 2 | 0), followed by 2 LE bytes of (length-1).
        out.push_back(static_cast<uint8_t>(61 << 2 | 0));
        uint16_t len_minus_1 = static_cast<uint16_t>(length - 1);
        out.push_back(static_cast<uint8_t>(len_minus_1 & 0xFF));
        out.push_back(static_cast<uint8_t>((len_minus_1 >> 8) & 0xFF));
    } else if (length <= 16777216) {
        // Lengths 65537-16777216: tag = (62 << 2 | 0), followed by 3 LE bytes.
        out.push_back(static_cast<uint8_t>(62 << 2 | 0));
        uint32_t len_minus_1 = static_cast<uint32_t>(length - 1);
        out.push_back(static_cast<uint8_t>(len_minus_1 & 0xFF));
        out.push_back(static_cast<uint8_t>((len_minus_1 >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((len_minus_1 >> 16) & 0xFF));
    } else {
        // Lengths up to 2^32: tag = (63 << 2 | 0), followed by 4 LE bytes.
        out.push_back(static_cast<uint8_t>(63 << 2 | 0));
        uint32_t len_minus_1 = static_cast<uint32_t>(length - 1);
        out.push_back(static_cast<uint8_t>(len_minus_1 & 0xFF));
        out.push_back(static_cast<uint8_t>((len_minus_1 >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>((len_minus_1 >> 16) & 0xFF));
        out.push_back(static_cast<uint8_t>((len_minus_1 >> 24) & 0xFF));
    }

    // Append the literal bytes themselves.
    out.insert(out.end(), data, data + length);
}

/// Emit a copy element. Chooses the most compact encoding for the given
/// offset and length.
inline void emit_copy(std::vector<uint8_t>& out, uint32_t offset,
                      uint32_t length) {
    // Copy-1: offset 1-2047, length 4-11
    // Encodes in 2 bytes total (tag + 1 extra).
    while (length > 0) {
        if (length >= 4 && length <= 11 && offset <= 2047) {
            // Copy-1 encoding.
            uint8_t tag = static_cast<uint8_t>(
                ((offset >> 8) << 5) | ((length - 4) << 2) | 1);
            out.push_back(tag);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
            return;
        }

        if (offset <= 65535) {
            // Copy-2 encoding: offset in 2 LE bytes, length 1-64.
            uint32_t chunk = (length > 64) ? 64 : length;
            uint8_t tag = static_cast<uint8_t>(((chunk - 1) << 2) | 2);
            out.push_back(tag);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            length -= chunk;
        } else {
            // Copy-4 encoding: offset in 4 LE bytes, length 1-64.
            uint32_t chunk = (length > 64) ? 64 : length;
            uint8_t tag = static_cast<uint8_t>(((chunk - 1) << 2) | 3);
            out.push_back(tag);
            out.push_back(static_cast<uint8_t>(offset & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 16) & 0xFF));
            out.push_back(static_cast<uint8_t>((offset >> 24) & 0xFF));
            length -= chunk;
        }
    }
}

/// Find the match length between `src[s1..]` and `src[s2..]`, bounded by
/// `src_end`. Returns the number of matching bytes (0 if first byte differs).
/// CWE-125: Out-of-bounds Read — bounds guard prevents reading past src_end.
inline uint32_t match_length(const uint8_t* src, size_t s1, size_t s2,
                             size_t src_end) {
    if (s1 >= src_end || s2 >= src_end) return 0; // CWE-125 guard
    uint32_t len = 0;
    size_t limit = src_end - ((s1 > s2) ? s1 : s2);
    // Cap match length to avoid overly long copy chains. The Snappy format
    // doesn't impose a per-copy maximum, but we emit in chunks anyway.
    if (limit > 65535) limit = 65535;
    while (len < limit && src[s1 + len] == src[s2 + len]) {
        ++len;
    }
    return len;
}

} // namespace detail::snappy

// ===========================================================================
// SnappyCodec -- CompressionCodec implementation
// ===========================================================================

/// Bundled Snappy compression codec (header-only, no external dependency).
///
/// Implements both compression and decompression of the Snappy block format
/// used by Apache Parquet. The compressor uses a single-pass greedy hash-chain
/// matching strategy with a 16384-entry hash table. Worst-case expansion is
/// approximately 1.004x the input size.
///
/// Register with the CodecRegistry via register_snappy_codec().
///
/// @see CompressionCodec, register_snappy_codec()
class SnappyCodec : public CompressionCodec {
public:
    /// Snappy-compress the input data.
    ///
    /// The output contains a varint-encoded uncompressed length preamble
    /// followed by a sequence of literal and copy elements.
    ///
    /// @param data Pointer to the uncompressed input bytes.
    /// @param size Number of bytes to compress.
    /// @return The Snappy-compressed byte buffer on success.
    [[nodiscard]] expected<std::vector<uint8_t>> compress(
        const uint8_t* data, size_t size) const override {

        using namespace detail::snappy;

        // Pessimistic upper bound: varint(5) + tag overhead per literal +
        // the data itself. Snappy's worst case is about 1.004x the input.
        std::vector<uint8_t> out;
        out.reserve(size + size / 64 + 16);

        // Snappy uses a uint32 varint for uncompressed length; reject >4 GiB
        // CWE-190: Integer Overflow (Snappy uses 32-bit lengths)
        if (size > UINT32_MAX) {
            return Error{ErrorCode::INTERNAL_ERROR,
                         "Snappy: input exceeds 4 GiB limit"};
        }

        // -- 1. Write the preamble: uncompressed length as varint ----------
        uint8_t varint_buf[5];
        size_t varint_len = encode_varint32(varint_buf,
                                            static_cast<uint32_t>(size));
        out.insert(out.end(), varint_buf, varint_buf + varint_len);

        // Handle trivial cases.
        if (size == 0) {
            return out;
        }
        if (size < 4) {
            // Too short for any match -- emit a single literal.
            emit_literal(out, data, size);
            return out;
        }

        // -- 2. Compress using greedy hash matching ------------------------
        //
        // The hash table maps 4-byte sequences (hashed to 14 bits) to their
        // most recent position in the input. When a hash collision finds a
        // genuine byte match, we emit any pending literals followed by a copy
        // element.

        // Use uint64_t positions to support inputs > 4 GB (L-1).
        std::array<uint64_t, kHashTableSize> table{};
        // Sentinel: UINT64_MAX means "no entry". We use this rather than 0
        // because position 0 is a valid position.
        table.fill(UINT64_MAX);

        size_t pos          = 0;     // Current scan position in `data`.
        size_t literal_start = 0;    // Start of pending literal run.

        while (pos + 4 <= size) {
            uint32_t h = hash4(data + pos);
            uint64_t candidate = table[h];
            table[h] = pos;

            // Check for a genuine match: the candidate must exist, the 4
            // bytes must match, and the offset must be positive.
            if (candidate != UINT64_MAX &&
                pos - candidate <= 65535 &&
                load_le32(data + pos) == load_le32(data + candidate)) {

                // Emit any pending literals before the copy.
                if (pos > literal_start) {
                    emit_literal(out, data + literal_start,
                                 pos - literal_start);
                }

                // Determine how far the match extends.
                uint32_t ml = match_length(data, pos, candidate, size);
                if (ml < 4) ml = 4; // We already verified 4 bytes match.

                uint32_t offset = static_cast<uint32_t>(pos - candidate);
                emit_copy(out, offset, ml);

                // Advance past the matched region. Insert intermediate
                // positions into the hash table so future matches can find
                // them (skip-1 heuristic: hash every position in the match).
                size_t match_end = pos + ml;
                pos++;
                while (pos < match_end && pos + 4 <= size) {
                    table[hash4(data + pos)] = pos;
                    pos++;
                }
                pos = match_end;
                literal_start = pos;
            } else {
                // No match -- advance one byte and keep accumulating literals.
                ++pos;
            }
        }

        // -- 3. Flush remaining literals -----------------------------------
        if (literal_start < size) {
            emit_literal(out, data + literal_start, size - literal_start);
        }

        return out;
    }

    /// Snappy-decompress the input data.
    ///
    /// The Snappy stream contains its own uncompressed-length preamble; this
    /// method verifies that it agrees with the Parquet-declared
    /// @p uncompressed_size. A mismatch is reported as ErrorCode::CORRUPT_PAGE.
    ///
    /// @param data              Pointer to the Snappy-compressed input bytes.
    /// @param size              Number of compressed bytes.
    /// @param uncompressed_size Expected decompressed size (from the Parquet
    ///                          page header).
    /// @return The decompressed byte buffer, or an Error on corruption/truncation.
    [[nodiscard]] expected<std::vector<uint8_t>> decompress(
        const uint8_t* data, size_t size,
        size_t uncompressed_size) const override {

        using namespace detail::snappy;

        if (size == 0) {
            if (uncompressed_size == 0) {
                return std::vector<uint8_t>{};
            }
            return Error{ErrorCode::CORRUPT_PAGE,
                         "Snappy: empty compressed stream but "
                         "expected non-zero output"};
        }

        // -- 1. Read the preamble: uncompressed length varint --------------
        size_t pos = 0;
        uint32_t declared_len = 0;
        if (!decode_varint32(data, size, pos, declared_len)) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "Snappy: failed to decode uncompressed length varint"};
        }

        // Validate against the Parquet-declared uncompressed size.
        if (static_cast<size_t>(declared_len) != uncompressed_size) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "Snappy: declared uncompressed length (" +
                         std::to_string(declared_len) +
                         ") does not match expected (" +
                         std::to_string(uncompressed_size) + ")"};
        }

        // -- 2. Allocate the output buffer ---------------------------------
        // CWE-409: Improper Handling of Highly Compressed Data (Decompression Bomb)
        static constexpr size_t MAX_SNAPPY_DECOMPRESS = 256ULL * 1024 * 1024;
        if (uncompressed_size > MAX_SNAPPY_DECOMPRESS) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "Snappy: decompressed size exceeds 256 MB"};
        }
        std::vector<uint8_t> out(uncompressed_size);
        size_t out_pos = 0;

        // -- 3. Decode elements --------------------------------------------
        while (pos < size) {
            uint8_t tag = data[pos++];
            uint8_t element_type = tag & 0x03;

            switch (element_type) {

            // ---- Literal (type 0) ----------------------------------------
            case 0: {
                uint32_t literal_len = (tag >> 2) + 1;

                // If the encoded length-1 is 60..63, the actual length follows
                // as 1..4 extra bytes (little-endian).
                uint32_t encoded_len_minus_1 = tag >> 2;
                if (encoded_len_minus_1 >= 60) {
                    uint32_t extra_bytes = encoded_len_minus_1 - 59;
                    if (pos + extra_bytes > size) {
                        return Error{ErrorCode::CORRUPT_PAGE,
                                     "Snappy: literal length bytes truncated"};
                    }
                    literal_len = 0;
                    for (uint32_t i = 0; i < extra_bytes; ++i) {
                        literal_len |= static_cast<uint32_t>(data[pos++]) << (8 * i);
                    }
                    literal_len += 1; // Stored as (length - 1).
                }

                // Bounds check.
                if (pos + literal_len > size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: literal data extends past end of "
                                 "compressed stream"};
                }
                if (out_pos + literal_len > uncompressed_size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: literal would overflow output buffer"};
                }

                std::memcpy(out.data() + out_pos, data + pos, literal_len);
                pos     += literal_len;
                out_pos += literal_len;
                break;
            }

            // ---- Copy-1 (type 1): 1 extra byte --------------------------
            // Tag layout: [offset_hi_3 : 3][length-4 : 3][01 : 2]
            // Extra byte: offset low 8 bits.
            case 1: {
                if (pos >= size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-1 truncated"};
                }
                uint32_t length = ((tag >> 2) & 0x07) + 4; // 4..11
                uint32_t offset = (static_cast<uint32_t>(tag >> 5) << 8) |
                                  data[pos++];
                if (offset == 0) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-1 with zero offset"};
                }
                if (offset > out_pos) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-1 offset (" +
                                 std::to_string(offset) +
                                 ") exceeds output position (" +
                                 std::to_string(out_pos) + ")"};
                }
                if (out_pos + length > uncompressed_size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-1 would overflow output buffer"};
                }

                // Copy from earlier output. Byte-by-byte to handle the
                // overlapping case (length > offset), which is how Snappy
                // encodes run-length patterns.
                size_t src = out_pos - offset;
                for (uint32_t i = 0; i < length; ++i) {
                    out[out_pos++] = out[src + i];
                }
                break;
            }

            // ---- Copy-2 (type 2): 2 extra bytes (LE offset) -------------
            // Tag layout: [length-1 : 6][10 : 2]
            case 2: {
                if (pos + 2 > size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-2 truncated"};
                }
                uint32_t length = (tag >> 2) + 1; // 1..64
                uint32_t offset = load_le16(data + pos);
                pos += 2;

                if (offset == 0) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-2 with zero offset"};
                }
                if (offset > out_pos) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-2 offset (" +
                                 std::to_string(offset) +
                                 ") exceeds output position (" +
                                 std::to_string(out_pos) + ")"};
                }
                if (out_pos + length > uncompressed_size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-2 would overflow output buffer"};
                }

                size_t src = out_pos - offset;
                if (length <= offset) {
                    // Non-overlapping: safe to memcpy.
                    std::memcpy(out.data() + out_pos, out.data() + src, length);
                    out_pos += length;
                } else {
                    // Overlapping: byte-by-byte.
                    for (uint32_t i = 0; i < length; ++i) {
                        out[out_pos++] = out[src + i];
                    }
                }
                break;
            }

            // ---- Copy-4 (type 3): 4 extra bytes (LE offset) -------------
            // Tag layout: [length-1 : 6][11 : 2]
            case 3: {
                if (pos + 4 > size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-4 truncated"};
                }
                uint32_t length = (tag >> 2) + 1; // 1..64
                uint32_t offset = load_le32(data + pos);
                pos += 4;

                if (offset == 0) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-4 with zero offset"};
                }
                if (static_cast<size_t>(offset) > out_pos) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-4 offset (" +
                                 std::to_string(offset) +
                                 ") exceeds output position (" +
                                 std::to_string(out_pos) + ")"};
                }
                if (out_pos + length > uncompressed_size) {
                    return Error{ErrorCode::CORRUPT_PAGE,
                                 "Snappy: copy-4 would overflow output buffer"};
                }

                size_t src = out_pos - offset;
                if (length <= offset) {
                    std::memcpy(out.data() + out_pos, out.data() + src, length);
                    out_pos += length;
                } else {
                    for (uint32_t i = 0; i < length; ++i) {
                        out[out_pos++] = out[src + i];
                    }
                }
                break;
            }

            default:
                // Unreachable: element_type is (tag & 0x03), always 0-3.
                return Error{ErrorCode::CORRUPT_PAGE,
                             "Snappy: unknown element type"};
            }
        }

        // -- 4. Validate final output size ---------------------------------
        if (out_pos != uncompressed_size) {
            return Error{ErrorCode::CORRUPT_PAGE,
                         "Snappy: decompressed " + std::to_string(out_pos) +
                         " bytes but expected " +
                         std::to_string(uncompressed_size)};
        }

        return out;
    }

    /// @name Metadata
    /// @{

    /// Return Compression::SNAPPY.
    [[nodiscard]] Compression codec_type() const override {
        return Compression::SNAPPY;
    }

    /// Return the codec name "snappy".
    [[nodiscard]] const char* name() const override {
        return "snappy";
    }

    /// @}
};

// ===========================================================================
// Auto-registration helper
// ===========================================================================

/// Register the bundled Snappy codec with the global CodecRegistry.
///
/// Call this once at startup (e.g. from a top-level initializer or a
/// codec_init function) to make Compression::SNAPPY available through
/// compress() and decompress().
///
/// @see SnappyCodec, CodecRegistry::register_codec()
inline void register_snappy_codec() {
    CodecRegistry::instance().register_codec(std::make_unique<SnappyCodec>());
}

} // namespace signet::forge
