#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Copyright 2026 Johnson Ogundeji
# run_full_suite.sh — Orchestrator for the enterprise benchmark suite
#
# Usage:
#   bash Benchmarking_Protocols/scripts/run_full_suite.sh [samples] [build-dir]
#
# Arguments:
#   samples   Number of Catch2 benchmark samples (default: 10)
#   build-dir Path to build directory (default: build-benchmarks)
#
# Steps:
#   1. Purge OS page cache (macOS only, requires sudo)
#   2. Run all benchmark phases via signet_enterprise_bench
#   3. Save output to Benchmarking_Protocols/results/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_DIR="$SCRIPT_DIR/../results"
SAMPLES="${1:-10}"
BUILD_DIR="${2:-$PROJECT_ROOT/build-benchmarks}"

BENCH_BIN="$BUILD_DIR/signet_enterprise_bench"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
RESULT_FILE="$RESULTS_DIR/bench_${TIMESTAMP}.txt"

# ===========================================================================
# Pre-flight checks
# ===========================================================================

if [[ ! -x "$BENCH_BIN" ]]; then
    echo "Error: benchmark binary not found: $BENCH_BIN"
    echo ""
    echo "Build first:"
    echo "  cmake --preset benchmarks"
    echo "  cmake --build build-benchmarks --target signet_enterprise_bench"
    exit 1
fi

mkdir -p "$RESULTS_DIR"

# ===========================================================================
# Purge page cache (macOS — requires sudo, skipped if not available)
# ===========================================================================

echo "=== Enterprise Benchmark Suite ==="
echo "Timestamp: $TIMESTAMP"
echo "Samples:   $SAMPLES"
echo "Binary:    $BENCH_BIN"
echo "Output:    $RESULT_FILE"
echo ""

if [[ "$(uname)" == "Darwin" ]]; then
    echo "Purging macOS disk cache..."
    if sudo -n purge 2>/dev/null; then
        echo "  Page cache purged."
    else
        echo "  [skip] Cannot purge cache (no sudo). Results may be affected by warm cache."
    fi
fi

# ===========================================================================
# Run benchmarks
# ===========================================================================

echo ""
echo "Running all enterprise benchmarks ($SAMPLES samples)..."
echo ""

"$BENCH_BIN" "[bench-enterprise]" \
    --benchmark-samples "$SAMPLES" \
    --benchmark-warmup-time 100 \
    2>&1 | tee "$RESULT_FILE"

echo ""
echo "=== Complete ==="
echo "Results saved to: $RESULT_FILE"
echo ""

# Show summary
echo "--- Result files ---"
ls -lh "$RESULTS_DIR"/bench_*.txt 2>/dev/null || echo "No result files found."
