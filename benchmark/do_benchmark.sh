#!/usr/bin/env bash
# /benchmark spot check: co-run a control build against gcc_build/dreal4 on select_jobs.py's
# family-weighted picks (<= 12), then compare the two arms as ratios (aggregate.py).
# Prints OUT_DIR to stdout on completion; all other output goes to stderr.
# Usage: do_benchmark.sh CONTROL     CONTROL = a stashed build, benchmark/bin/dreal4-<sha>
#                                    (stash.sh makes one at the commit you want to compare to)
# Env: TIMEOUT  CPU-seconds cap per run, passed through to corun.sh (which sets the default)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CONTROL="${1:?usage: do_benchmark.sh CONTROL (a stashed build, benchmark/bin/dreal4-<sha>)}"

SHA=$(git -C "$PROJECT_DIR" rev-parse --short HEAD)
OUT_DIR="$PROJECT_DIR/benchmark/results/run_${SHA}_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT_DIR"

python3 "$SCRIPT_DIR/select_jobs.py" > "$OUT_DIR/jobs.tsv"
bash "$SCRIPT_DIR/corun.sh" "$OUT_DIR" "$OUT_DIR/jobs.tsv" \
    control="$CONTROL" test="$PROJECT_DIR/gcc_build/dreal4" >&2
python3 "$SCRIPT_DIR/aggregate.py" "$OUT_DIR" > "$OUT_DIR/aggregate.json"

echo "$OUT_DIR"
