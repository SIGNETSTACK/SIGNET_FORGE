#!/usr/bin/env bash
# =============================================================================
# scripts/run_benchmarks.sh
# Signet_Forge benchmark runner — build + run by category with reporting
#
# Usage:
#   ./scripts/run_benchmarks.sh [OPTIONS]
#
# Options:
#   --category <name>     Category to run (default: all)
#                         Categories: all, write, read, wal, encoding,
#                                     feature_store, event_bus
#   --samples <n>         Number of Catch2 benchmark samples (default: 100)
#   --build               (Re)build the benchmarks binary before running
#   --no-build            Skip build, just run (default)
#   --output <file>       Tee output to file (default: none)
#   --help                Show this help
#
# Examples:
#   ./scripts/run_benchmarks.sh --category wal --samples 200
#   ./scripts/run_benchmarks.sh --category all --build --output results.txt
#   ./scripts/run_benchmarks.sh --category encoding
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
CATEGORY="all"
SAMPLES=100
DO_BUILD=0
OUTPUT_FILE=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_ROOT/build-benchmarks/signet_benchmarks"

# ---------------------------------------------------------------------------
# Colour helpers (disabled when not a tty)
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    BOLD=$'\033[1m'
    CYAN=$'\033[36m'
    GREEN=$'\033[32m'
    YELLOW=$'\033[33m'
    RED=$'\033[31m'
    RESET=$'\033[0m'
else
    BOLD="" CYAN="" GREEN="" YELLOW="" RED="" RESET=""
fi

log()  { echo "${CYAN}[bench]${RESET} $*"; }
ok()   { echo "${GREEN}[ok]${RESET}    $*"; }
warn() { echo "${YELLOW}[warn]${RESET}  $*"; }
err()  { echo "${RED}[err]${RESET}   $*" >&2; }
sep()  { echo "${BOLD}${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RESET}"; }

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --category)   CATEGORY="$2"; shift 2 ;;
        --samples)    SAMPLES="$2";  shift 2 ;;
        --build)      DO_BUILD=1;    shift   ;;
        --no-build)   DO_BUILD=0;    shift   ;;
        --output)     OUTPUT_FILE="$2"; shift 2 ;;
        --help|-h)
            sed -n '3,25p' "$0"
            exit 0
            ;;
        *)
            err "Unknown option: $1"
            exit 1
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Map category → Catch2 tag filter
# ---------------------------------------------------------------------------
case "$CATEGORY" in
    all)           TAG_FILTER="[bench]"                    ;;
    write)         TAG_FILTER="[bench][write]"             ;;
    read)          TAG_FILTER="[bench][read]"              ;;
    wal)           TAG_FILTER="[wal][bench]"               ;;
    encoding)      TAG_FILTER="[encoding][bench]"          ;;
    feature_store) TAG_FILTER="[bench][feature_store]"     ;;
    event_bus)     TAG_FILTER="[bench][mpmc_ring],[bench][column_batch],[bench][event_bus]" ;;
    *)
        err "Unknown category: '$CATEGORY'"
        err "Valid categories: all, write, read, wal, encoding, feature_store, event_bus"
        exit 1
        ;;
esac

# ---------------------------------------------------------------------------
# Header
# ---------------------------------------------------------------------------
sep
echo "${BOLD}  Signet_Forge Benchmark Runner${RESET}"
echo "  Category : ${CYAN}${CATEGORY}${RESET}   (tag filter: ${TAG_FILTER})"
echo "  Samples  : ${CYAN}${SAMPLES}${RESET}"
echo "  Binary   : ${BINARY}"
echo "  Date     : $(date '+%Y-%m-%d %H:%M:%S')"
sep

# ---------------------------------------------------------------------------
# Optionally build
# ---------------------------------------------------------------------------
if [[ "$DO_BUILD" == "1" ]]; then
    log "Configuring benchmarks preset..."
    cmake --preset benchmarks -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/build-benchmarks" 2>&1

    log "Building signet_benchmarks..."
    cmake --build "$PROJECT_ROOT/build-benchmarks" --target signet_benchmarks -- -j"$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)" 2>&1
    ok "Build complete."
    sep
fi

# ---------------------------------------------------------------------------
# Check binary exists
# ---------------------------------------------------------------------------
if [[ ! -x "$BINARY" ]]; then
    err "Binary not found: $BINARY"
    err "Run with --build to build first, or manually:"
    err "  cmake --preset benchmarks && cmake --build build-benchmarks"
    exit 1
fi

# ---------------------------------------------------------------------------
# Run benchmarks
# ---------------------------------------------------------------------------
log "Running benchmarks: ${TAG_FILTER} with ${SAMPLES} samples..."
echo ""

RUN_CMD=(
    "$BINARY"
    "$TAG_FILTER"
    --benchmark-samples "$SAMPLES"
    --benchmark-warmup-time 0.1
    -r console
    --colour-mode ansi
)

if [[ -n "$OUTPUT_FILE" ]]; then
    log "Teeing output to: $OUTPUT_FILE"
    "${RUN_CMD[@]}" 2>&1 | tee "$OUTPUT_FILE"
    STATUS="${PIPESTATUS[0]}"
else
    "${RUN_CMD[@]}"
    STATUS=$?
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
sep
if [[ "$STATUS" == "0" ]]; then
    ok "All benchmarks completed successfully."
else
    err "One or more benchmarks failed (exit code: $STATUS)."
fi
sep

# ---------------------------------------------------------------------------
# Category-specific interpretation hints
# ---------------------------------------------------------------------------
case "$CATEGORY" in
    wal)
        echo ""
        echo "${BOLD}WAL Interpretation Guide:${RESET}"
        echo "  'append 32B' mean < 1000 ns  → sub-µs claim CONFIRMED"
        echo "  'append 32B' mean > 2000 ns  → check: tmpfs? fsync disabled? thermal throttle?"
        echo "  'append 256B' latency should be < 2× the 32B latency (fixed-overhead dominated)"
        echo "  'read_all 10K records' gives cold-recovery time at startup"
        echo ""
        ;;
    encoding)
        echo ""
        echo "${BOLD}Encoding Interpretation Guide:${RESET}"
        echo "  DELTA encode mean   → µs per 10K timestamps. Divide by 10K for per-record cost."
        echo "  BSS encode/decode   → should be memory-bandwidth limited (~10 GB/s on modern CPUs)"
        echo "  DELTA size check    → must be < 40,000 bytes (< half of 80,000 byte PLAIN)"
        echo "  BSS size check      → must be exactly 80,000 bytes (size-preserving)"
        echo ""
        ;;
    feature_store)
        echo ""
        echo "${BOLD}Feature Store Interpretation Guide:${RESET}"
        echo "  'get latest' mean ÷ 1000      → per-call latency (target < 50 µs)"
        echo "  'as_of mid-range' mean ÷ 1000 → point-in-time latency (target < 50 µs)"
        echo "  'as_of_batch 100' mean        → batch latency (target < 1 ms)"
        echo "  'history 100 records' mean    → range-scan latency"
        echo ""
        ;;
    event_bus)
        echo ""
        echo "${BOLD}Event Bus Interpretation Guide:${RESET}"
        echo "  'push+pop' single-thread mean     → baseline CAS cost (target < 1 µs)"
        echo "  '4P4C 4000 items' total ÷ 4000   → per-item amortised throughput under contention"
        echo "  'push 1000 rows + column_view'    → zero-copy validation (no allocation in hot path)"
        echo "  'as_tensor' mean                  → column→row-major transpose for ML inference"
        echo ""
        ;;
    write)
        echo ""
        echo "${BOLD}Write Throughput Interpretation Guide:${RESET}"
        echo "  'write int64' mean      → time to write+close 10K row file (target < 5 ms)"
        echo "  'write double BSS' mean → BSS encoding overhead vs plain memcpy"
        echo "  'write mixed 5 cols'    → realistic tick record write latency"
        echo "  '100K rows 10 rgs'      → multi-row-group overhead check (should scale linearly)"
        echo "  'write string DICT'     → dictionary encoding throughput for symbol columns"
        echo ""
        ;;
    read)
        echo ""
        echo "${BOLD}Read Throughput Interpretation Guide:${RESET}"
        echo "  'read_column<double>'        → typed read (fastest path, target < 2 ms for 50K)"
        echo "  'read_all string conversion' → generic read (includes double→string conversion)"
        echo "  'read_columns price+qty'     → projection read (should be < read_all)"
        echo "  'read_column<int64_t>'       → integer read baseline"
        echo "  'open + num_rows'            → footer parse latency (irreducible per-file overhead)"
        echo ""
        ;;
    all)
        echo ""
        echo "${BOLD}See docs/BENCHMARKING_ORIGIN_AND_RELEVANCE.md for full interpretation guide.${RESET}"
        echo ""
        ;;
esac

exit "$STATUS"
