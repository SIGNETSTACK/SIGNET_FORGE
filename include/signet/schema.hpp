// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Johnson Ogundeji
#pragma once

#include "signet/types.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <vector>

// Windows <sal.h> defines OPTIONAL as a SAL annotation macro — undefine.
#ifdef OPTIONAL
#undef OPTIONAL
#endif

namespace signet::forge {

/// @file schema.hpp
/// @brief Schema definition types: Column\<T\>, SchemaBuilder, and Schema.

/// Typed column descriptor for the Schema::build() variadic API.
///
/// Deduces the Parquet PhysicalType from the C++ template parameter @p T.
/// For `std::string`, the LogicalType defaults to STRING unless overridden.
///
/// @code
///   auto schema = Schema::build("trades",
///       Column<int64_t>{"timestamp", LogicalType::TIMESTAMP_NS},
///       Column<double>{"price"},
///       Column<std::string>{"symbol"});
/// @endcode
///
/// @tparam T C++ type mapped to a Parquet PhysicalType via parquet_type_of.
/// @see Schema::build(), SchemaBuilder, ColumnDescriptor
template <typename T>
struct Column {
    std::string name;  ///< Column name.
    LogicalType logical_type = LogicalType::NONE;  ///< Optional logical annotation.

    /// Construct a column with a name only (logical type auto-deduced for strings).
    /// @param n Column name.
    Column(std::string n) : name(std::move(n)) {
        apply_default_logical_type();
    }

    /// Construct a column with an explicit logical type.
    /// @param n  Column name.
    /// @param lt Logical type annotation (NONE falls through to auto-deduction).
    Column(std::string n, LogicalType lt) : name(std::move(n)), logical_type(lt) {
        if (logical_type == LogicalType::NONE) {
            apply_default_logical_type();
        }
    }

    /// Convert to a ColumnDescriptor for Schema construction.
    /// @return A ColumnDescriptor with physical type deduced from T.
    [[nodiscard]] ColumnDescriptor to_descriptor() const {
        ColumnDescriptor cd;
        cd.name          = name;
        cd.physical_type = parquet_type_of_v<T>;
        cd.logical_type  = logical_type;
        return cd;
    }

private:
    /// Apply default LogicalType::STRING for std::string columns.
    void apply_default_logical_type() {
        if constexpr (std::is_same_v<T, std::string>) {
            if (logical_type == LogicalType::NONE) {
                logical_type = LogicalType::STRING;
            }
        }
    }
};

class Schema;  // Forward declaration.

/// Fluent builder for constructing a Schema one column at a time.
///
/// @code
///   auto schema = Schema::builder("trades")
///       .column<int64_t>("timestamp", LogicalType::TIMESTAMP_NS)
///       .column<double>("price")
///       .optional_column<std::string>("note")
///       .build();
/// @endcode
///
/// @see Schema::builder(), Column
class SchemaBuilder {
public:
    /// Construct a builder with the given root schema name.
    /// @param name Root schema name (e.g. "trades").
    explicit SchemaBuilder(std::string name) : name_(std::move(name)) {}

    /// Add a typed column, deducing PhysicalType from @p T.
    ///
    /// For `std::string` columns with no explicit logical type, defaults to
    /// LogicalType::STRING.
    /// @tparam T C++ type (bool, int32_t, int64_t, float, double, std::string).
    /// @param col_name    Column name (must be unique within the schema).
    /// @param logical_type Optional logical annotation (default: NONE / auto).
    /// @return Reference to this builder for chaining.
    template <typename T>
    SchemaBuilder& column(std::string col_name,
                          LogicalType logical_type = LogicalType::NONE) {
        ColumnDescriptor cd;
        cd.name          = std::move(col_name);
        cd.physical_type = parquet_type_of_v<T>;
        cd.logical_type  = logical_type;

        // Default std::string → STRING if no explicit logical type
        if constexpr (std::is_same_v<T, std::string>) {
            if (cd.logical_type == LogicalType::NONE) {
                cd.logical_type = LogicalType::STRING;
            }
        }

        columns_.push_back(std::move(cd));
        return *this;
    }

    /// Add a column with an explicit repetition level.
    /// @tparam T C++ type.
    /// @param col_name    Column name.
    /// @param logical_type Logical annotation.
    /// @param repetition  Field repetition (REQUIRED, OPTIONAL, REPEATED).
    /// @return Reference to this builder for chaining.
    template <typename T>
    SchemaBuilder& column(std::string col_name,
                          LogicalType logical_type,
                          Repetition repetition) {
        ColumnDescriptor cd;
        cd.name          = std::move(col_name);
        cd.physical_type = parquet_type_of_v<T>;
        cd.logical_type  = logical_type;
        cd.repetition    = repetition;

        if constexpr (std::is_same_v<T, std::string>) {
            if (cd.logical_type == LogicalType::NONE) {
                cd.logical_type = LogicalType::STRING;
            }
        }

        columns_.push_back(std::move(cd));
        return *this;
    }

    /// Add an optional (nullable) column — shorthand for Repetition::OPTIONAL.
    /// @tparam T C++ type.
    /// @param col_name    Column name.
    /// @param logical_type Optional logical annotation.
    /// @return Reference to this builder for chaining.
    template <typename T>
    SchemaBuilder& optional_column(std::string col_name,
                                   LogicalType logical_type = LogicalType::NONE) {
        return column<T>(std::move(col_name), logical_type, Repetition::OPTIONAL);
    }

    /// Add a pre-built ColumnDescriptor directly.
    ///
    /// Useful for FIXED_LEN_BYTE_ARRAY columns where type_length must be set
    /// manually, or for columns that cannot be expressed via Column\<T\>.
    /// @param cd A fully-populated column descriptor.
    /// @return Reference to this builder for chaining.
    SchemaBuilder& raw_column(ColumnDescriptor cd) {
        columns_.push_back(std::move(cd));
        return *this;
    }

    /// Build the final Schema, consuming the builder.
    ///
    /// After calling build(), the builder is left in a moved-from state and
    /// should not be reused.
    /// @return An immutable Schema.
    [[nodiscard]] Schema build();

private:
    std::string                  name_;
    std::vector<ColumnDescriptor> columns_;
};

/// Immutable schema description for a Parquet file.
///
/// A Schema has a root name and an ordered list of ColumnDescriptor entries.
/// Create one via the variadic Schema::build() factory or the fluent
/// Schema::builder() API.
///
/// @see Column, SchemaBuilder, ColumnDescriptor
class Schema {
public:
    /// Default-construct an empty schema.
    Schema() = default;

    /// Construct a schema directly from a name and column list.
    /// @param name    Root schema name.
    /// @param columns Ordered column descriptors.
    Schema(std::string name, std::vector<ColumnDescriptor> columns)
        : name_(std::move(name)), columns_(std::move(columns)) {}

    /// Build a Schema from typed Column\<T\> descriptors (variadic factory).
    ///
    /// @code
    ///   auto s = Schema::build("tick_data",
    ///       Column<int64_t>{"timestamp", LogicalType::TIMESTAMP_NS},
    ///       Column<double>{"price"},
    ///       Column<std::string>{"symbol"});
    /// @endcode
    ///
    /// @tparam Cols Pack of Column\<T\> types.
    /// @param name Schema root name.
    /// @param cols One or more Column\<T\> objects.
    /// @return An immutable Schema.
    template <typename... Cols>
    [[nodiscard]] static Schema build(std::string name, Cols&&... cols) {
        std::vector<ColumnDescriptor> descs;
        descs.reserve(sizeof...(Cols));
        (descs.push_back(std::forward<Cols>(cols).to_descriptor()), ...);
        return Schema(std::move(name), std::move(descs));
    }

    /// Create a SchemaBuilder for fluent column construction.
    /// @param name Root schema name.
    /// @return A SchemaBuilder instance.
    /// @see SchemaBuilder
    [[nodiscard]] static SchemaBuilder builder(std::string name) {
        return SchemaBuilder(std::move(name));
    }

    // -- Accessors -------------------------------------------------------------

    /// Root schema name (e.g. "tick_data").
    [[nodiscard]] const std::string& name() const { return name_; }

    /// Number of columns in this schema.
    [[nodiscard]] size_t num_columns() const { return columns_.size(); }

    /// Access a column descriptor by index.
    /// @param index Zero-based column index.
    /// @return Const reference to the ColumnDescriptor.
    /// @throws std::out_of_range if index >= num_columns().
    [[nodiscard]] const ColumnDescriptor& column(size_t index) const {
        if (index >= columns_.size()) {
            throw std::out_of_range("Schema::column: index "
                + std::to_string(index) + " out of range (num_columns="
                + std::to_string(columns_.size()) + ")");
        }
        return columns_[index];
    }

    /// All column descriptors (ordered).
    [[nodiscard]] const std::vector<ColumnDescriptor>& columns() const {
        return columns_;
    }

    /// Find a column index by name.
    /// @param col_name Column name to search for.
    /// @return The zero-based index, or @c std::nullopt if not found.
    [[nodiscard]] std::optional<size_t> find_column(const std::string& col_name) const {
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].name == col_name) {
                return i;
            }
        }
        return std::nullopt;
    }

    /// Equality — schemas match if they have the same name and identical columns
    /// (name, physical_type, logical_type, repetition, type_length).
    [[nodiscard]] bool operator==(const Schema& other) const {
        if (name_ != other.name_ || columns_.size() != other.columns_.size()) {
            return false;
        }
        for (size_t i = 0; i < columns_.size(); ++i) {
            const auto& a = columns_[i];
            const auto& b = other.columns_[i];
            if (a.name != b.name ||
                a.physical_type != b.physical_type ||
                a.logical_type != b.logical_type ||
                a.repetition != b.repetition ||
                a.type_length != b.type_length) {
                return false;
            }
        }
        return true;
    }

    /// Inequality operator.
    [[nodiscard]] bool operator!=(const Schema& other) const {
        return !(*this == other);
    }

private:
    std::string                   name_;     ///< Root schema name.
    std::vector<ColumnDescriptor> columns_;  ///< Ordered column descriptors.
};

// ---------------------------------------------------------------------------
// SchemaBuilder::build() — defined after Schema is complete
// ---------------------------------------------------------------------------
inline Schema SchemaBuilder::build() {
    // Detect duplicate column names at build time
    std::unordered_set<std::string> seen;
    seen.reserve(columns_.size());
    for (const auto& cd : columns_) {
        if (!seen.insert(cd.name).second) {
            throw std::invalid_argument(
                "Schema::build: duplicate column name '" + cd.name + "'");
        }
    }
    return Schema(std::move(name_), std::move(columns_));
}

} // namespace signet::forge
