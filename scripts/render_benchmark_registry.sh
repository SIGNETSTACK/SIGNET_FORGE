#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REGISTRY="$ROOT/Benchmarking_Protocols/benchmark_id_registry.tsv"
OUT="${1:-$ROOT/Benchmarking_Protocols/BENCHMARK_ID_REGISTRY.md}"

if [[ ! -f "$REGISTRY" ]]; then
  echo "Registry file not found: $REGISTRY" >&2
  exit 1
fi

cat > "$OUT" <<'HEADER'
# Benchmark ID Registry

This document is generated from `Benchmarking_Protocols/benchmark_id_registry.tsv`.
Do not edit this Markdown file by hand. Update the registry source and rerender it.

## Governance
1. Benchmark IDs are global across tracked benchmark suites in `benchmarks/` and `Benchmarking_Protocols/`.
2. Benchmark IDs are immutable once published, unless explicitly deprecated and replaced.
3. Every tracked `BENCHMARK(...)` or `BENCHMARK_ADVANCED(...)` label must exist in the registry.
4. Generic labels such as `write`, `read_all`, or `delta encode` are not permitted.
5. Micro benchmark IDs in `benchmarks/*.cpp` must be 30 characters or fewer so Catch2 console output cannot wrap them in the GitHub benchmark parser path.
6. `status=active` entries must exist in code. `deprecated` and `reserved` entries are retained for history and future planning.
7. The CI validator is the enforcement point. A benchmark change is incomplete until the registry and generated documentation are updated.

## Registry Source
- `Benchmarking_Protocols/benchmark_id_registry.tsv`

## Protocol Benchmarks
| ID | Suite | File | Status |
|---|---|---|---|
HEADER

awk -F '\t' 'NR > 1 && NF >= 5 && $3 == "protocol" { printf("| `%s` | `%s` | `%s` | `%s` |\n", $1, $4, $2, $5); }' "$REGISTRY" >> "$OUT"

cat >> "$OUT" <<'MIDDLE'

## Micro Benchmarks
| ID | Suite | File | Status |
|---|---|---|---|
MIDDLE

awk -F '\t' 'NR > 1 && NF >= 5 && $3 == "micro" { printf("| `%s` | `%s` | `%s` | `%s` |\n", $1, $4, $2, $5); }' "$REGISTRY" >> "$OUT"
