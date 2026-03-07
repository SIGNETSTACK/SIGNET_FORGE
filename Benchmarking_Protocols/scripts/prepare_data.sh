#!/usr/bin/env bash
# Extracts tick data subsets from ticks.csv.gz for enterprise benchmarks
set -euo pipefail

TICK_SOURCE="${1:-./ticks.csv.gz}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="$SCRIPT_DIR/../data"

mkdir -p "$DATA_DIR"

if [[ ! -f "$TICK_SOURCE" ]]; then
    echo "Error: Tick source not found: $TICK_SOURCE"
    echo "Usage: $0 [path/to/ticks.csv.gz]"
    exit 1
fi

echo "Extracting tick data subsets from: $TICK_SOURCE"
echo "Output directory: $DATA_DIR"

# Extract header (disable pipefail for gunzip|head which triggers SIGPIPE)
HEADER=$(set +o pipefail; gunzip -c "$TICK_SOURCE" | head -1)

for SIZE in 1000 100000 1000000 10000000; do
    case $SIZE in
        1000)     LABEL="1k" ;;
        100000)   LABEL="100k" ;;
        1000000)  LABEL="1m" ;;
        10000000) LABEL="10m" ;;
    esac

    OUTFILE="$DATA_DIR/ticks_${LABEL}.csv"
    if [[ -f "$OUTFILE" ]]; then
        echo "  [skip] $OUTFILE already exists"
        continue
    fi

    echo "  Extracting $LABEL rows -> $OUTFILE ..."
    echo "$HEADER" > "$OUTFILE"
    # Disable pipefail: gunzip gets SIGPIPE when head/tail closes early — that's expected
    (set +o pipefail; gunzip -c "$TICK_SOURCE" | tail -n +2 | head -n "$SIZE" >> "$OUTFILE")

    LINES=$(wc -l < "$OUTFILE")
    echo "    -> $LINES lines (including header)"
done

echo "Done. Data files:"
ls -lh "$DATA_DIR"/ticks_*.csv
