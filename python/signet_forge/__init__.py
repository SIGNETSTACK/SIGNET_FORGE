# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026 Johnson Ogundeji
# signet_forge — Python bindings for Signet Forge C++20 library
# Import everything from the compiled pybind11 extension module.
# AI Audit types (AGPL-3.0 commercial tier) are optional — gracefully degrade when
# built without SIGNET_BUILD_AI_AUDIT.

from signet_forge._bindings import (
    # Enums
    PhysicalType,
    LogicalType,
    Encoding,
    Compression,
    # Schema
    ColumnDescriptor,
    Schema,
    SchemaBuilder,
    # Writer / Reader
    WriterOptions,
    ParquetWriter,
    ParquetReader,
    # Feature Store
    FeatureGroupDef,
    FeatureVector,
    FeatureWriterOptions,
    FeatureWriter,
    FeatureReaderOptions,
    FeatureReader,
    # Meta
    __version__,
)

__all__ = [
    "PhysicalType", "LogicalType", "Encoding", "Compression",
    "ColumnDescriptor", "Schema", "SchemaBuilder",
    "WriterOptions", "ParquetWriter", "ParquetReader",
    "FeatureGroupDef", "FeatureVector", "FeatureWriterOptions", "FeatureWriter",
    "FeatureReaderOptions", "FeatureReader",
]

# AI Audit & Compliance tier (AGPL-3.0 commercial tier) — only present when built with
# SIGNET_BUILD_AI_AUDIT=ON (which defines SIGNET_HAS_AI_AUDIT in the
# pybind11 module). Gracefully degrade so OSS users don't get ImportError.
try:
    from signet_forge._bindings import (
        ReportFormat,
        ComplianceStandard,
        DecisionType,
        RiskGateResult,
        InferenceType,
        DecisionRecord,
        DecisionLogWriter,
        InferenceRecord,
        InferenceLogWriter,
        ReportOptions,
        ComplianceReport,
        MiFID2Reporter,
        EUAIActReporter,
    )
    __all__ += [
        "ReportFormat", "ComplianceStandard", "DecisionType", "RiskGateResult",
        "InferenceType",
        "DecisionRecord", "DecisionLogWriter", "InferenceRecord", "InferenceLogWriter",
        "ReportOptions", "ComplianceReport", "MiFID2Reporter", "EUAIActReporter",
    ]
except ImportError:
    pass  # AI Audit tier not available in this build

# Encrypted Parquet I/O — PME Facade (commercial tier, requires SIGNET_ENABLE_COMMERCIAL)
# Provides opaque KeyHandle (raw key bytes never cross FFI boundary),
# EncryptedWriterOptions/EncryptedReaderOptions with RBAC, and
# open_encrypted_writer/reader factory functions.
try:
    from signet_forge._bindings import (
        ColumnClassification,
        KeyHandle,
        EncryptedWriterOptions,
        EncryptedReaderOptions,
        open_encrypted_writer,
        open_encrypted_reader,
    )
    __all__ += [
        "ColumnClassification", "KeyHandle",
        "EncryptedWriterOptions", "EncryptedReaderOptions",
        "open_encrypted_writer", "open_encrypted_reader",
    ]
except ImportError:
    pass  # PME facade not available in this build
