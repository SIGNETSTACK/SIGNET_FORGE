#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REGISTRY="$ROOT/Benchmarking_Protocols/benchmark_id_registry.tsv"
DOC="$ROOT/Benchmarking_Protocols/BENCHMARK_ID_REGISTRY.md"
RENDER_SCRIPT="$ROOT/scripts/render_benchmark_registry.sh"
MAX_MICRO_ID_LEN=30

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

CODE_FILE="$TMP_DIR/code.tsv"
CODE_IDS="$TMP_DIR/code_ids.tsv"
REGISTRY_FILE="$TMP_DIR/registry.tsv"
REGISTRY_ACTIVE="$TMP_DIR/registry_active.tsv"
RENDERED_DOC="$TMP_DIR/BENCHMARK_ID_REGISTRY.md"

ERRORS=0

if [[ ! -f "$REGISTRY" ]]; then
  echo "Missing registry file: $REGISTRY" >&2
  exit 1
fi

if [[ ! -f "$DOC" ]]; then
  echo "Missing registry doc: $DOC" >&2
  exit 1
fi

if [[ ! -x "$RENDER_SCRIPT" ]]; then
  echo "Render script is not executable: $RENDER_SCRIPT" >&2
  exit 1
fi

HEADER_EXPECTED=$'id\tfile\tkind\tsuite\tstatus'
HEADER_ACTUAL="$(head -n 1 "$REGISTRY")"
if [[ "$HEADER_ACTUAL" != "$HEADER_EXPECTED" ]]; then
  echo "Registry header mismatch in $REGISTRY" >&2
  echo "Expected: $HEADER_EXPECTED" >&2
  echo "Actual:   $HEADER_ACTUAL" >&2
  ERRORS=1
fi

: > "$CODE_FILE"
while IFS= read -r file; do
  rel="${file#$ROOT/}"
  while IFS=$'\t' read -r line benchmark_id; do
    [[ -n "$benchmark_id" ]] || continue
    printf '%s\t%s\t%s\n' "$benchmark_id" "$rel" "$line" >> "$CODE_FILE"
  done < <(perl -ne 'if (/BENCHMARK(?:_ADVANCED)?\("([^"]+)"\)/) { print "$.\t$1\n"; }' "$file")
done < <(find "$ROOT/Benchmarking_Protocols" "$ROOT/benchmarks" -type f -name '*.cpp' | sort)

CODE_DUPLICATES="$TMP_DIR/code_duplicates.txt"
awk -F '\t' '
  {
    count[$1]++;
    loc[$1] = loc[$1] sprintf("  %s:%s\n", $2, $3);
  }
  END {
    for (id in count) {
      if (count[id] > 1) {
        printf("Duplicate benchmark ID in code: %s\n%s", id, loc[id]);
      }
    }
  }
' "$CODE_FILE" > "$CODE_DUPLICATES"
if [[ -s "$CODE_DUPLICATES" ]]; then
  cat "$CODE_DUPLICATES" >&2
  ERRORS=1
fi

MICRO_LENGTH_ERRORS="$TMP_DIR/micro_length_errors.txt"
awk -F '\t' -v max_len="$MAX_MICRO_ID_LEN" '
  $2 ~ /^benchmarks\// {
    id_len = length($1);
    if (id_len > max_len) {
      printf("Micro benchmark ID exceeds %d characters: %s (%d) at %s:%s\n",
             max_len, $1, id_len, $2, $3);
    }
  }
' "$CODE_FILE" > "$MICRO_LENGTH_ERRORS"
if [[ -s "$MICRO_LENGTH_ERRORS" ]]; then
  cat "$MICRO_LENGTH_ERRORS" >&2
  ERRORS=1
fi

: > "$REGISTRY_FILE"
while IFS=$'\t' read -r id file kind suite status extra; do
  [[ -n "$id$file$kind$suite$status" ]] || continue

  if [[ -n "${extra:-}" ]]; then
    echo "Registry row has too many columns: $id" >&2
    ERRORS=1
    continue
  fi

  case "$kind" in
    protocol|micro) ;;
    *)
      echo "Invalid kind '$kind' for benchmark ID '$id'" >&2
      ERRORS=1
      continue
      ;;
  esac

  case "$status" in
    active|deprecated|reserved) ;;
    *)
      echo "Invalid status '$status' for benchmark ID '$id'" >&2
      ERRORS=1
      continue
      ;;
  esac

  if [[ ! -f "$ROOT/$file" ]]; then
    echo "Registry file path does not exist for benchmark ID '$id': $file" >&2
    ERRORS=1
    continue
  fi

  printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$file" "$kind" "$suite" "$status" >> "$REGISTRY_FILE"
done < <(tail -n +2 "$REGISTRY")

REGISTRY_DUPLICATES="$TMP_DIR/registry_duplicates.txt"
awk -F '\t' '
  {
    count[$1]++;
    loc[$1] = loc[$1] sprintf("  %s\n", $2);
  }
  END {
    for (id in count) {
      if (count[id] > 1) {
        printf("Duplicate benchmark ID in registry: %s\n%s", id, loc[id]);
      }
    }
  }
' "$REGISTRY_FILE" > "$REGISTRY_DUPLICATES"
if [[ -s "$REGISTRY_DUPLICATES" ]]; then
  cat "$REGISTRY_DUPLICATES" >&2
  ERRORS=1
fi

awk -F '\t' '{ print $1 "\t" $2; }' "$CODE_FILE" | sort > "$CODE_IDS"
awk -F '\t' '$5 == "active" { print $1 "\t" $2; }' "$REGISTRY_FILE" | sort > "$REGISTRY_ACTIVE"

MISSING_REGISTRY="$TMP_DIR/missing_registry.txt"
join -t $'\t' -v 1 "$CODE_IDS" "$REGISTRY_ACTIVE" > "$MISSING_REGISTRY" || true
if [[ -s "$MISSING_REGISTRY" ]]; then
  echo "Benchmarks present in code but missing from active registry:" >&2
  awk -F '\t' '{ printf("  %s -> %s\n", $1, $2); }' "$MISSING_REGISTRY" >&2
  ERRORS=1
fi

STALE_REGISTRY="$TMP_DIR/stale_registry.txt"
join -t $'\t' -v 2 "$CODE_IDS" "$REGISTRY_ACTIVE" > "$STALE_REGISTRY" || true
if [[ -s "$STALE_REGISTRY" ]]; then
  echo "Active registry entries missing from code:" >&2
  awk -F '\t' '{ printf("  %s -> %s\n", $1, $2); }' "$STALE_REGISTRY" >&2
  ERRORS=1
fi

FILE_MISMATCH="$TMP_DIR/file_mismatch.txt"
join -t $'\t' "$CODE_IDS" "$REGISTRY_ACTIVE" | awk -F '\t' '$2 != $3 { printf("  %s -> code:%s registry:%s\n", $1, $2, $3); }' > "$FILE_MISMATCH" || true
if [[ -s "$FILE_MISMATCH" ]]; then
  echo "Benchmark IDs mapped to different files in code and registry:" >&2
  cat "$FILE_MISMATCH" >&2
  ERRORS=1
fi

"$RENDER_SCRIPT" "$RENDERED_DOC"
if ! cmp -s "$RENDERED_DOC" "$DOC"; then
  echo "Benchmark registry doc is out of date: $DOC" >&2
  echo "Run scripts/render_benchmark_registry.sh or scripts/update_benchmark_registry.sh" >&2
  ERRORS=1
fi

if [[ "$ERRORS" -ne 0 ]]; then
  exit 1
fi

echo "Benchmark registry validation passed." >&2
