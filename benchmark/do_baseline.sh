#!/usr/bin/env bash
# Re-establish a local performance baseline (~30 stratified benchmarks).
# Prints OUT_DIR to stdout on completion; all other output goes to stderr.
# Usage: do_baseline.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BINARY="$PROJECT_DIR/gcc_build/dreal4"

if [[ ! -x "$BINARY" ]]; then
    echo "ERROR: dreal4 binary not found at $BINARY" >&2
    exit 1
fi

# Check/start oom_killer
if ! pgrep -f oom_killer.sh > /dev/null 2>&1; then
    nohup /usr/local/bin/oom_killer.sh &>/tmp/oom_killer.log &
fi

SHA=$(git -C "$PROJECT_DIR" rev-parse --short HEAD)
TS=$(date +%Y%m%d_%H%M%S)
OUT_DIR="$PROJECT_DIR/benchmark/results/baseline_${SHA}_${TS}"
JOBS_FILE="/tmp/dreal_baseline_jobs_${SHA}_${TS}.tsv"

mkdir -p "$OUT_DIR"

python3 "$SCRIPT_DIR/select_baseline.py" --input "$SCRIPT_DIR/baseline.csv" > "$JOBS_FILE"
bash "$SCRIPT_DIR/run_batch.sh" "$OUT_DIR" "$JOBS_FILE"
python3 "$SCRIPT_DIR/parse_results.py" "$OUT_DIR" >&2

# Save as local baseline and update state.json
cp "$OUT_DIR/summary.csv" "$SCRIPT_DIR/baseline_local.csv"
python3 "$SCRIPT_DIR/set_local_baseline.py" "$SHA"
python3 "$SCRIPT_DIR/aggregate.py" "$OUT_DIR" --frozen-baseline "$SCRIPT_DIR/baseline.csv" > "$OUT_DIR/aggregate.json"

echo "$OUT_DIR"
