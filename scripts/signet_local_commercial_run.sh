#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
signet_local_commercial_run.sh

Configure + build + run a commercial Signet target using a runtime license key.
The script computes SIGNET_COMMERCIAL_LICENSE_HASH (FNV-1a 64-bit) from the
exact runtime key string and injects it into CMake.

Usage:
  scripts/signet_local_commercial_run.sh [options] [-- target-args...]

Options:
  --project-root PATH       Project root (default: script dir/..)
  --build-dir PATH          Build dir (default: <project-root>/build-commercial-local)
  --target NAME             Build/run target (default: example_ticks_wal_stream)
  --license-key KEY         Runtime license key (default: env SIGNET_COMMERCIAL_LICENSE_KEY)
  --enable-pq ON|OFF        Build with liboqs PQ path (default: ON)
  --require-real-pq ON|OFF  Forbid PQ stubs (default: OFF)
  --build-examples MODE     AUTO|ON|OFF (default: AUTO)
  --build-tests MODE        AUTO|ON|OFF (default: AUTO)
  --build-benchmarks MODE   AUTO|ON|OFF (default: AUTO)
  --build-tools MODE        AUTO|ON|OFF (default: AUTO)
  --jobs N                  Build parallelism (default: env SIGNET_BUILD_JOBS or 8)
  --no-run                  Configure/build only
  -h, --help                Show this help

Examples:
  export SIGNET_COMMERCIAL_LICENSE_KEY='tier=evaluation;max_rows_month=50000000;max_users=5;max_nodes=2;max_days=30;expires_at=2026-12-31'
  scripts/signet_local_commercial_run.sh -- '/path/to/your/ticks.csv.gz'

  scripts/signet_local_commercial_run.sh \
    --target signet_tests \
    --enable-pq ON \
    --require-real-pq ON
USAGE
}

normalize_mode() {
  local v
  v="$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')"
  case "$v" in
    AUTO|ON|OFF) printf '%s' "$v" ;;
    *)
      echo "ERROR: invalid mode '$1' (expected AUTO|ON|OFF)." >&2
      exit 1
      ;;
  esac
}

normalize_on_off() {
  local v
  v="$(printf '%s' "$1" | tr '[:lower:]' '[:upper:]')"
  case "$v" in
    ON|OFF) printf '%s' "$v" ;;
    *)
      echo "ERROR: invalid value '$1' (expected ON|OFF)." >&2
      exit 1
      ;;
  esac
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="${SCRIPT_DIR%/scripts}"
BUILD_DIR="$PROJECT_ROOT/build-commercial-local"
TARGET="example_ticks_wal_stream"
LICENSE_KEY="${SIGNET_COMMERCIAL_LICENSE_KEY:-}"
ENABLE_PQ="ON"
REQUIRE_REAL_PQ="OFF"
BUILD_EXAMPLES="AUTO"
BUILD_TESTS="AUTO"
BUILD_BENCHMARKS="AUTO"
BUILD_TOOLS="AUTO"
JOBS="${SIGNET_BUILD_JOBS:-8}"
NO_RUN=0

TARGET_ARGS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --project-root)
      PROJECT_ROOT="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --target)
      TARGET="$2"
      shift 2
      ;;
    --license-key)
      LICENSE_KEY="$2"
      shift 2
      ;;
    --enable-pq)
      ENABLE_PQ="$(normalize_on_off "$2")"
      shift 2
      ;;
    --require-real-pq)
      REQUIRE_REAL_PQ="$(normalize_on_off "$2")"
      shift 2
      ;;
    --build-examples)
      BUILD_EXAMPLES="$(normalize_mode "$2")"
      shift 2
      ;;
    --build-tests)
      BUILD_TESTS="$(normalize_mode "$2")"
      shift 2
      ;;
    --build-benchmarks)
      BUILD_BENCHMARKS="$(normalize_mode "$2")"
      shift 2
      ;;
    --build-tools)
      BUILD_TOOLS="$(normalize_mode "$2")"
      shift 2
      ;;
    --jobs)
      JOBS="$2"
      shift 2
      ;;
    --no-run)
      NO_RUN=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      TARGET_ARGS=("$@")
      break
      ;;
    *)
      TARGET_ARGS=("$@")
      break
      ;;
  esac
done

if [[ -z "${LICENSE_KEY}" ]]; then
  echo "ERROR: missing license key. Set SIGNET_COMMERCIAL_LICENSE_KEY or pass --license-key." >&2
  exit 1
fi

# Auto-enable only the group needed by selected target.
if [[ "$BUILD_EXAMPLES" == "AUTO" ]]; then
  [[ "$TARGET" == example_* ]] && BUILD_EXAMPLES="ON" || BUILD_EXAMPLES="OFF"
fi
if [[ "$BUILD_TESTS" == "AUTO" ]]; then
  [[ "$TARGET" == "signet_tests" ]] && BUILD_TESTS="ON" || BUILD_TESTS="OFF"
fi
if [[ "$BUILD_BENCHMARKS" == "AUTO" ]]; then
  [[ "$TARGET" == "signet_benchmarks" ]] && BUILD_BENCHMARKS="ON" || BUILD_BENCHMARKS="OFF"
fi
if [[ "$BUILD_TOOLS" == "AUTO" ]]; then
  [[ "$TARGET" == "signet_cli" ]] && BUILD_TOOLS="ON" || BUILD_TOOLS="OFF"
fi

SIGNET_COMMERCIAL_LICENSE_KEY="${LICENSE_KEY}"
export SIGNET_COMMERCIAL_LICENSE_KEY
SIGNET_COMMERCIAL_LICENSE_HASH="$(python3 - <<'PY'
import os
s = os.environ["SIGNET_COMMERCIAL_LICENSE_KEY"].encode("utf-8")
h = 0xcbf29ce484222325
for b in s:
    h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
print(f"0x{h:016x}")
PY
)"

echo "== Signet commercial local runner =="
echo "Project root     : ${PROJECT_ROOT}"
echo "Build dir        : ${BUILD_DIR}"
echo "Target           : ${TARGET}"
echo "Enable PQ        : ${ENABLE_PQ}"
echo "Require real PQ  : ${REQUIRE_REAL_PQ}"
echo "Build examples   : ${BUILD_EXAMPLES}"
echo "Build tests      : ${BUILD_TESTS}"
echo "Build benchmarks : ${BUILD_BENCHMARKS}"
echo "Build tools      : ${BUILD_TOOLS}"
echo "License hash     : ${SIGNET_COMMERCIAL_LICENSE_HASH}"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
  -DSIGNET_ENABLE_COMMERCIAL=ON \
  -DSIGNET_REQUIRE_COMMERCIAL_LICENSE=ON \
  -DSIGNET_COMMERCIAL_LICENSE_HASH="${SIGNET_COMMERCIAL_LICENSE_HASH}" \
  -DSIGNET_ENABLE_PQ="${ENABLE_PQ}" \
  -DSIGNET_REQUIRE_REAL_PQ="${REQUIRE_REAL_PQ}" \
  -DSIGNET_BUILD_EXAMPLES="${BUILD_EXAMPLES}" \
  -DSIGNET_BUILD_TESTS="${BUILD_TESTS}" \
  -DSIGNET_BUILD_BENCHMARKS="${BUILD_BENCHMARKS}" \
  -DSIGNET_BUILD_TOOLS="${BUILD_TOOLS}"

cmake --build "${BUILD_DIR}" -j"${JOBS}" --target "${TARGET}"

if [[ "${NO_RUN}" -eq 1 ]]; then
  echo "Build completed (--no-run set)."
  exit 0
fi

TARGET_BIN="${BUILD_DIR}/${TARGET}"
if [[ ! -x "${TARGET_BIN}" ]]; then
  echo "ERROR: target binary not found/executable at ${TARGET_BIN}" >&2
  exit 1
fi

echo "Running: ${TARGET_BIN} ${TARGET_ARGS[*]-}"
SIGNET_COMMERCIAL_LICENSE_KEY="${LICENSE_KEY}" \
  "${TARGET_BIN}" "${TARGET_ARGS[@]}"
