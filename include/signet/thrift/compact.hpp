// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

/// @file compact.hpp
/// @brief Thrift Compact Protocol encoder and decoder for Parquet metadata serialization.

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stack>
#include <string>
#include <vector>

#include "signet/error.hpp"

namespace signet::forge::thrift {

/// Thrift Compact Protocol type identifiers (4-bit nibble values).
///
/// These IDs are used in field headers and list headers to identify the
/// wire type of each value. They follow the Apache Thrift Compact Protocol
/// specification.
namespace compact_type {
    inline constexpr uint8_t STOP       = 0;   ///< Struct stop marker.
    inline constexpr uint8_t BOOL_TRUE  = 1;   ///< Boolean true (embedded in field header).
    inline constexpr uint8_t BOOL_FALSE = 2;   ///< Boolean false (embedded in field header).
    inline constexpr uint8_t I8         = 3;   ///< 8-bit signed integer.
    inline constexpr uint8_t I16        = 4;   ///< 16-bit signed integer (zigzag + varint).
    inline constexpr uint8_t I32        = 5;   ///< 32-bit signed integer (zigzag + varint).
    inline constexpr uint8_t I64        = 6;   ///< 64-bit signed integer (zigzag + varint).
    inline constexpr uint8_t DOUBLE     = 7;   ///< IEEE 754 double (8 bytes LE).
    inline constexpr uint8_t BINARY     = 8;   ///< Length-prefixed bytes (also used for STRING).
    inline constexpr uint8_t LIST       = 9;   ///< List container.
    inline constexpr uint8_t SET        = 10;  ///< Set container.
    inline constexpr uint8_t MAP        = 11;  ///< Map container.
    inline constexpr uint8_t STRUCT     = 12;  ///< Nested struct.
} // namespace compact_type

/// Decoded field header from the Thrift Compact Protocol.
///
/// Returned by CompactDecoder::read_field_header(). A field with
/// field_id == 0 and thrift_type == 0 represents the STOP marker.
struct FieldHeader {
    int16_t  field_id;     ///< Thrift field identifier (from the schema).
    uint8_t  thrift_type;  ///< Wire type (one of the compact_type constants).

    /// Check if this is the STOP marker (end of struct).
    [[nodiscard]] bool is_stop() const { return field_id == 0 && thrift_type == 0; }
};

/// Decoded list/set header from the Thrift Compact Protocol.
///
/// Returned by CompactDecoder::read_list_header(). Contains the element
/// wire type and the number of elements.
struct ListHeader {
    uint8_t  elem_type;  ///< Wire type of each element (compact_type constant).
    int32_t  size;       ///< Number of elements in the list/set.
};

/// Thrift Compact Protocol writer.
///
/// Writes Thrift Compact Protocol data into an internal byte buffer. Supports
/// nested structs, delta field encoding, zigzag+varint integers, and all
/// standard compact types. Used to serialize Parquet FileMetaData and related
/// structures.
///
/// @see CompactDecoder for the corresponding reader
/// @see signet::forge::thrift (types.hpp) for Parquet-specific struct types
class CompactEncoder {
public:
    /// Default constructor. Initializes field-ID stack with a single zero entry.
    CompactEncoder() { last_field_ids_.push(0); }

    // -- field / struct helpers ------------------------------------------------

    /// Write a field header. Uses delta encoding when field_id - last_field_id
    /// is in [1, 15], otherwise writes the type byte followed by a zigzag
    /// varint of the field ID.
    void write_field(int16_t field_id, uint8_t thrift_type) {
        int16_t delta = field_id - last_field_ids_.top();
        if (delta > 0 && delta <= 15) {
            buf_.push_back(static_cast<uint8_t>((delta << 4) | thrift_type));
        } else {
            buf_.push_back(thrift_type);
            write_zigzag_i16(field_id);
        }
        last_field_ids_.top() = field_id;
    }

    /// Write struct stop marker (0x00).
    void write_stop() { buf_.push_back(0x00); }

    /// Push a new field-ID context for a nested struct.
    void begin_struct() { last_field_ids_.push(0); }

    /// Pop the field-ID context after finishing a nested struct.
    void end_struct() { last_field_ids_.pop(); }

    // -- primitive writers -----------------------------------------------------

    /// Write a standalone bool (not embedded in a field header).
    void write_bool(bool val) {
        buf_.push_back(val ? 0x01 : 0x00);
    }

    /// Write a bool field where the value is embedded in the field header's
    /// type nibble (1 = true, 2 = false). This is the standard compact
    /// protocol encoding for bool fields.
    void write_field_bool(int16_t field_id, bool val) {
        uint8_t thrift_type = val ? compact_type::BOOL_TRUE
                                  : compact_type::BOOL_FALSE;
        int16_t delta = field_id - last_field_ids_.top();
        if (delta > 0 && delta <= 15) {
            buf_.push_back(static_cast<uint8_t>((delta << 4) | thrift_type));
        } else {
            buf_.push_back(thrift_type);
            write_zigzag_i16(field_id);
        }
        last_field_ids_.top() = field_id;
    }

    /// Write a 32-bit integer as zigzag + varint.
    void write_i32(int32_t val) {
        write_varint32(zigzag_encode_i32(val));
    }

    /// Write a 64-bit integer as zigzag + varint.
    void write_i64(int64_t val) {
        write_varint64(zigzag_encode_i64(val));
    }

    /// Write a double as 8 bytes little-endian (IEEE 754).
    void write_double(double val) {
        uint64_t bits;
        std::memcpy(&bits, &val, 8);
        for (int i = 0; i < 8; ++i)
            buf_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }

    /// Write a float as 4 bytes little-endian (IEEE 754).
    /// Note: Not part of standard Thrift compact protocol, but useful for
    /// Parquet FLOAT columns and other binary formats.
    void write_float(float val) {
        uint32_t bits;
        std::memcpy(&bits, &val, 4);
        for (int i = 0; i < 4; ++i)
            buf_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
    }

    /// Write a string as varint-length-prefixed UTF-8 bytes.
    void write_string(const std::string& val) {
        write_varint32(static_cast<uint32_t>(val.size()));
        buf_.insert(buf_.end(), val.begin(), val.end());
    }

    /// Write raw binary data as varint-length-prefixed bytes.
    void write_binary(const uint8_t* data, size_t len) {
        write_varint32(static_cast<uint32_t>(len));
        buf_.insert(buf_.end(), data, data + len);
    }

    /// Write a list header. If size <= 14, uses the compact single-byte form.
    /// Otherwise writes (0xF0 | elem_type) followed by a varint size.
    void write_list_header(uint8_t elem_type, int32_t size) {
        if (size < 0) return; // negative size is invalid
        if (size <= 14) {
            buf_.push_back(static_cast<uint8_t>((size << 4) | elem_type));
        } else {
            buf_.push_back(static_cast<uint8_t>(0xF0 | elem_type));
            write_varint32(static_cast<uint32_t>(size));
        }
    }

    // -- access ---------------------------------------------------------------

    /// Returns a const reference to the underlying byte buffer.
    [[nodiscard]] const std::vector<uint8_t>& data() const { return buf_; }

    /// Returns the current size of the encoded buffer in bytes.
    [[nodiscard]] size_t size() const { return buf_.size(); }

    /// Resets the encoder to its initial state (empty buffer, field ID stack
    /// reset to a single zero entry).
    void clear() {
        buf_.clear();
        while (!last_field_ids_.empty()) last_field_ids_.pop();
        last_field_ids_.push(0);
    }

private:
    std::vector<uint8_t> buf_;
    std::stack<int16_t>  last_field_ids_;

    // -- varint encoding ------------------------------------------------------

    void write_varint32(uint32_t val) {
        while (val > 0x7F) {
            buf_.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
            val >>= 7;
        }
        buf_.push_back(static_cast<uint8_t>(val));
    }

    void write_varint64(uint64_t val) {
        while (val > 0x7F) {
            buf_.push_back(static_cast<uint8_t>((val & 0x7F) | 0x80));
            val >>= 7;
        }
        buf_.push_back(static_cast<uint8_t>(val));
    }

    void write_zigzag_i16(int16_t val) {
        uint32_t zz = zigzag_encode_i32(static_cast<int32_t>(val));
        write_varint32(zz);
    }

    // -- zigzag encoding ------------------------------------------------------

    static uint32_t zigzag_encode_i32(int32_t val) {
        return static_cast<uint32_t>((val << 1) ^ (val >> 31));
    }

    // CWE-190, C++ [expr.shift] p7.6.7 — left shift on unsigned to avoid UB
    static uint64_t zigzag_encode_i64(int64_t val) {
        return (static_cast<uint64_t>(val) << 1) ^ static_cast<uint64_t>(val >> 63);
    }
};

/// Thrift Compact Protocol reader.
///
/// Reads Thrift Compact Protocol data from a caller-owned byte buffer.
/// All read operations perform bounds checking and set an internal error
/// flag on overflow, after which all subsequent reads return zero/empty
/// values and good() returns false.
///
/// Hardening limits:
///   - Maximum nesting depth: 128 (prevents stack exhaustion).
///   - Maximum fields per struct: 65536 (prevents DoS via field-count inflation).
///   - Maximum string/binary field: 64 MB (prevents memory exhaustion).
///   - Maximum collection (LIST/SET/MAP) size: 1M entries.
///
/// @see CompactEncoder for the corresponding writer
class CompactDecoder {
public:
    /// Construct a decoder over a byte buffer.
    ///
    /// The buffer must remain valid for the lifetime of the decoder.
    /// @param data  Pointer to the Thrift Compact Protocol byte buffer.
    /// @param size  Total buffer size in bytes.
    CompactDecoder(const uint8_t* data, size_t size)
        : data_(data), size_(size), pos_(0), error_(false),
          pending_bool_{}, pending_bool_valid_(false) {
        last_field_ids_.push(0);
    }

    // -- field / struct helpers ------------------------------------------------

    /// Read a field header. Returns {0, 0} for the STOP marker.
    /// For bool fields (types 1 and 2), the value is captured internally
    /// and returned by a subsequent read_bool() call.
    [[nodiscard]] FieldHeader read_field_header() {
        if (!ensure(1)) return {0, 0};

        uint8_t byte = data_[pos_++];

        // STOP marker
        if (byte == 0x00) return {0, 0};

        uint8_t type = byte & 0x0F;
        int16_t delta = static_cast<int16_t>((byte >> 4) & 0x0F);

        int16_t field_id;
        if (delta != 0) {
            // Delta-encoded field ID
            field_id = last_field_ids_.top() + delta;
        } else {
            // Full field ID follows as zigzag varint
            int32_t id32 = read_zigzag_i32();
            field_id = static_cast<int16_t>(id32);
        }
        last_field_ids_.top() = field_id;

        // For bool fields, the value is embedded in the type nibble.
        // Cache it for the next read_bool() call.
        if (type == compact_type::BOOL_TRUE) {
            pending_bool_ = true;
            pending_bool_valid_ = true;
        } else if (type == compact_type::BOOL_FALSE) {
            pending_bool_ = false;
            pending_bool_valid_ = true;
        }

        if (++field_count_ > MAX_FIELD_COUNT) {
            error_ = true;
            return {0, 0};
        }
        // CWE-400: Uncontrolled Resource Consumption (DoS prevention)
        if (++total_fields_read_ > MAX_TOTAL_FIELDS) {
            error_ = true;
            return {0, 0};
        }

        return {field_id, type};
    }

    /// Read a boolean value. If the bool was embedded in a field header
    /// (types 1/2), returns that cached value. Otherwise reads a single byte.
    [[nodiscard]] bool read_bool() {
        if (pending_bool_valid_) {
            pending_bool_valid_ = false;
            return pending_bool_;
        }
        if (!ensure(1)) return false;
        return data_[pos_++] != 0;
    }

    /// Read a 32-bit integer (zigzag + varint decode).
    [[nodiscard]] int32_t read_i32() {
        return read_zigzag_i32();
    }

    /// Read a 64-bit integer (zigzag + varint64 decode).
    [[nodiscard]] int64_t read_i64() {
        return read_zigzag_i64();
    }

    /// Read a double (8 bytes little-endian, IEEE 754).
    [[nodiscard]] double read_double() {
        if (!ensure(8)) return 0.0;
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i)
            bits |= static_cast<uint64_t>(data_[pos_++]) << (i * 8);
        double val;
        std::memcpy(&val, &bits, 8);
        return val;
    }

    /// Read a float (4 bytes little-endian, IEEE 754).
    [[nodiscard]] float read_float() {
        if (!ensure(4)) return 0.0f;
        uint32_t bits = 0;
        for (int i = 0; i < 4; ++i)
            bits |= static_cast<uint32_t>(data_[pos_++]) << (i * 8);
        float val;
        std::memcpy(&val, &bits, 4);
        return val;
    }

    /// Read a string (varint-length-prefixed UTF-8 bytes).
    [[nodiscard]] std::string read_string() {
        uint32_t len = read_varint32();
        if (len > MAX_STRING_BYTES) { error_ = true; return {}; }
        if (!ensure(len)) return {};
        std::string result(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return result;
    }

    /// Read raw binary data (varint-length-prefixed bytes).
    [[nodiscard]] std::vector<uint8_t> read_binary() {
        uint32_t len = read_varint32();
        if (len > MAX_STRING_BYTES) { error_ = true; return {}; }
        if (!ensure(len)) return {};
        std::vector<uint8_t> result(data_ + pos_, data_ + pos_ + len);
        pos_ += len;
        return result;
    }

    /// Read a list header. Returns element type and count.
    [[nodiscard]] ListHeader read_list_header() {
        if (!ensure(1)) return {0, 0};
        uint8_t byte = data_[pos_++];
        uint8_t elem_type = byte & 0x0F;
        int32_t size = (byte >> 4) & 0x0F;
        if (size == 15) {
            // Large list: size follows as varint
            uint32_t raw = read_varint32();
            if (raw > static_cast<uint32_t>((std::numeric_limits<int32_t>::max)())) {
                error_ = true;
                return {0, 0};
            }
            size = static_cast<int32_t>(raw);
        }
        if (size < 0) {
            // CWE-20: Improper Input Validation — negative list size (likely corrupt data)
            error_ = true;
            return {0, 0};
        }
        if (static_cast<uint32_t>(size) > MAX_COLLECTION_SIZE) {
            error_ = true; return {0, 0};
        }
        return {elem_type, size};
    }

    /// Skip a field without parsing its value. Used for forward compatibility
    /// when encountering unknown field IDs.
    void skip_field(uint8_t thrift_type) {
        switch (thrift_type) {
        case compact_type::BOOL_TRUE:
        case compact_type::BOOL_FALSE:
            // Value already consumed in field header; nothing to skip.
            break;

        case compact_type::I8:
            // Single byte
            if (ensure(1)) pos_ += 1;
            break;

        case compact_type::I16:
        case compact_type::I32:
            // Zigzag varint — just consume it
            (void)read_varint32();
            break;

        case compact_type::I64:
            // Zigzag varint64 — just consume it
            (void)read_varint64();
            break;

        case compact_type::DOUBLE:
            if (ensure(8)) pos_ += 8;
            break;

        case compact_type::BINARY: {
            // Length-prefixed bytes
            uint32_t len = read_varint32();
            if (ensure(len)) pos_ += len;
            break;
        }

        case compact_type::LIST:
        case compact_type::SET: {
            auto hdr = read_list_header();
            if (hdr.size < 0 || static_cast<uint32_t>(hdr.size) > MAX_COLLECTION_SIZE) {
                error_ = true; break;
            }
            for (int32_t i = 0; i < hdr.size && good(); ++i) {
                skip_field(hdr.elem_type);
            }
            break;
        }

        case compact_type::MAP: {
            uint32_t map_size = read_varint32();
            if (map_size == 0) break;
            if (map_size > MAX_COLLECTION_SIZE) { error_ = true; break; }
            if (!ensure(1)) break;
            uint8_t kv_types = data_[pos_++];
            uint8_t key_type = (kv_types >> 4) & 0x0F;
            uint8_t val_type = kv_types & 0x0F;
            for (uint32_t i = 0; i < map_size && good(); ++i) {
                skip_field(key_type);
                skip_field(val_type);
            }
            break;
        }

        case compact_type::STRUCT: {
            // Read fields until STOP
            begin_struct();
            while (good()) {
                auto fh = read_field_header();
                if (fh.is_stop()) break;
                skip_field(fh.thrift_type);
            }
            end_struct();
            break;
        }

        default:
            // Unknown type — mark as error
            error_ = true;
            break;
        }
    }

    /// Push a new field-ID context for reading a nested struct.
    void begin_struct() {
        if (last_field_ids_.size() >= MAX_NESTING_DEPTH) { error_ = true; return; }
        last_field_ids_.push(0);
        field_count_ = 0;
    }

    /// Pop the field-ID context after finishing a nested struct.
    void end_struct() {
        if (last_field_ids_.empty()) { error_ = true; return; } // CWE-124: Buffer Underwrite
        last_field_ids_.pop();
    }

    // -- state queries --------------------------------------------------------

    /// Returns the number of bytes remaining in the buffer.
    [[nodiscard]] size_t remaining() const {
        return (pos_ <= size_) ? (size_ - pos_) : 0;
    }

    /// Returns the current read position (offset from start of buffer).
    [[nodiscard]] size_t position() const { return pos_; }

    /// Returns true if no errors have occurred (no bounds violations).
    [[nodiscard]] bool good() const { return !error_; }

private:
    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
    bool           error_;

    // Bool values embedded in field headers are cached here.
    bool           pending_bool_;
    bool           pending_bool_valid_;

    static constexpr size_t   MAX_NESTING_DEPTH   = 64;               ///< Reduced from 128 for embedded safety (CWE-674)
    static constexpr size_t   MAX_FIELD_COUNT     = 65536;            ///< Max fields per struct.
    static constexpr size_t   MAX_TOTAL_FIELDS    = 1'000'000;        ///< 1M total fields across all nesting.
    static constexpr uint32_t MAX_STRING_BYTES    = 64u * 1024u * 1024u;  ///< 64 MB max string/binary field.
    static constexpr uint32_t MAX_COLLECTION_SIZE = 1'000'000u;       ///< 1M max entries for LIST/SET/MAP.
    std::stack<int16_t> last_field_ids_;
    size_t field_count_ = 0;
    size_t total_fields_read_ = 0;                                    ///< Global field counter across all nesting.

    // -- bounds checking ------------------------------------------------------

    /// Ensure at least `n` bytes remain. Sets error flag if not.
    [[nodiscard]] bool ensure(size_t n) {
        if (error_ || pos_ + n > size_) {
            error_ = true;
            return false;
        }
        return true;
    }

    // -- varint decoding ------------------------------------------------------

    [[nodiscard]] uint32_t read_varint32() {
        uint32_t result = 0;
        int shift = 0;
        while (shift < 35) {
            if (!ensure(1)) return 0;
            uint8_t byte = data_[pos_++];
            result |= static_cast<uint32_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return result;
            shift += 7;
        }
        // Varint too long — malformed
        error_ = true;
        return 0;
    }

    [[nodiscard]] uint64_t read_varint64() {
        uint64_t result = 0;
        int shift = 0;
        while (shift < 70) {
            if (!ensure(1)) return 0;
            uint8_t byte = data_[pos_++];
            result |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) return result;
            shift += 7;
        }
        // Varint too long — malformed
        error_ = true;
        return 0;
    }

    // -- zigzag decoding ------------------------------------------------------

    [[nodiscard]] int32_t read_zigzag_i32() {
        uint32_t raw = read_varint32();
        return static_cast<int32_t>((raw >> 1) ^ -(static_cast<int32_t>(raw & 1)));
    }

    [[nodiscard]] int64_t read_zigzag_i64() {
        uint64_t raw = read_varint64();
        return static_cast<int64_t>((raw >> 1) ^ -(static_cast<int64_t>(raw & 1)));
    }
};

} // namespace signet::forge::thrift
