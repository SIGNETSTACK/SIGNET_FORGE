#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REGISTRY="$ROOT/Benchmarking_Protocols/benchmark_id_registry.tsv"
RENDER_SCRIPT="$ROOT/scripts/render_benchmark_registry.sh"
DOC_OUT="$ROOT/Benchmarking_Protocols/BENCHMARK_ID_REGISTRY.md"

TMP_REGISTRY="$(mktemp)"
trap 'rm -f "$TMP_REGISTRY"' EXIT

{
  printf 'id\tfile\tkind\tsuite\tstatus\n'

  while IFS= read -r file; do
    rel="${file#$ROOT/}"
    case "$rel" in
      Benchmarking_Protocols/*.cpp)
        kind="protocol"
        ;;
      benchmarks/*.cpp)
        kind="micro"
        ;;
      *)
        continue
        ;;
    esac

    suite="$(basename "$rel" .cpp)"
    suite="${suite#bench_}"

    while IFS= read -r benchmark_id; do
      printf '%s\t%s\t%s\t%s\tactive\n' "$benchmark_id" "$rel" "$kind" "$suite"
    done < <(perl -ne 'if (/BENCHMARK(?:_ADVANCED)?\("([^"]+)"\)/) { print "$1\n"; }' "$file")
  done < <(find "$ROOT/Benchmarking_Protocols" "$ROOT/benchmarks" -type f -name '*.cpp' | sort)
} > "$TMP_REGISTRY"

mv "$TMP_REGISTRY" "$REGISTRY"
"$RENDER_SCRIPT" "$DOC_OUT"
