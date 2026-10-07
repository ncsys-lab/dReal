#!/usr/bin/env bash
# Co-run solver arms over one spot-check job set — the only local A/B harness.
#
# Usage: corun.sh OUT JOBS ARM [ARM...]
#   OUT  : output dir; one subdir per arm with <bench>.{stdout,solver_log,rusage,exit} and a
#          summary.csv (parse_results.py), plus OUT/arms.tsv (label, command), OUT/cap (the
#          CPU cap) and OUT/compare.txt (compare_solvers.py).
#   JOBS : TSV (csv_name <TAB> filepath), at most 12 benchmarks. Anything bigger is not a spot
#          check: run it on Sherlock (../dreal-stanford-benchmarking).
#   ARM  : label=<binary> [flags...], e.g.  control=benchmark/bin/dreal4-abc123
#          test="gcc_build/dreal4 --ode-taylor-order 12"
# Env: TIMEOUT  CPU-seconds cap per run, a whole number (default 120: on the spot-check mix it
#               keeps ~86% of the informative results of 600 in ~30% of the time —
#               docs/benchmarking.md §"Choosing the cap")
#      MAXJOBS  solver processes at once (default: the E-core count)
#
# The comparison rule is Sherlock's (docs/benchmarking.md): two runs compare only if they ran at
# the same time on the same core type, and only as a ratio. So every arm of one benchmark
# launches together, the group finishes before its slot frees, and every run goes through
# measure.py: E-cores only (macOS cannot pin to P-cores), a CPU-time cap instead of a wall
# clock, P-core share recorded. Machine load then slows a spot check but cannot skew it.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SPOT_MAX=12
OUT="${1:?usage: corun.sh OUT JOBS ARM [ARM...]}"
JOBS="${2:?usage: corun.sh OUT JOBS ARM [ARM...]}"
shift 2
(( $# >= 1 )) || { echo "usage: corun.sh OUT JOBS ARM [ARM...]" >&2; exit 2; }
TIMEOUT="${TIMEOUT:-120}"
MAXJOBS="${MAXJOBS:-$(sysctl -n hw.perflevel1.logicalcpu)}"
[[ "$TIMEOUT" =~ ^[0-9]+$ ]] || { echo "ERROR: TIMEOUT must be whole CPU seconds, got $TIMEOUT" >&2; exit 2; }

labels=(); cmds=()
for spec in "$@"; do
    [[ "$spec" == *=* ]] || { echo "ERROR: arm must be label=<binary> [flags]: $spec" >&2; exit 2; }
    cmd="${spec#*=}"; bin="${cmd%% *}"
    [[ -f "$bin" && -x "$bin" ]] || { echo "ERROR: ${spec%%=*}: not an executable file: $bin" >&2; exit 2; }
    # absolute, so measure.py's exec runs this file, not a same-named one on PATH
    labels+=("${spec%%=*}"); cmds+=("$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")${cmd#"$bin"}")
done
NBENCH=$(grep -c . "$JOBS")
(( NBENCH <= SPOT_MAX )) || {
    echo "ERROR: $NBENCH benchmarks is more than a spot check (<= $SPOT_MAX); run it on Sherlock" \
         "(../dreal-stanford-benchmarking, experiments/<id>.py)" >&2; exit 2; }
GROUPS_AT_ONCE=$(( MAXJOBS / ${#labels[@]} ))
(( GROUPS_AT_ONCE >= 1 )) || { echo "ERROR: ${#labels[@]} arms exceed MAXJOBS=$MAXJOBS" >&2; exit 2; }

# disown: a daemon started here must not count as a pool job or hold the final wait
pgrep -x oom_killer > /dev/null || { nohup /usr/local/bin/oom_killer &>/tmp/oom_killer.log & disown; }
mkdir -p "$OUT"; : > "$OUT/arms.tsv"; echo "$TIMEOUT" > "$OUT/cap"
for i in "${!labels[@]}"; do
    mkdir -p "$OUT/${labels[$i]}"
    printf '%s\t%s\n' "${labels[$i]}" "${cmds[$i]}" >> "$OUT/arms.tsv"
done
echo "corun: ${#labels[@]} arms x $NBENCH benchmarks, $GROUPS_AT_ONCE at a time, ${TIMEOUT} CPU-s cap -> $OUT" >&2

while IFS=$'\t' read -r csv path || [[ -n "$csv" ]]; do   # || : a last line with no newline
    [[ -z "$csv" || -z "$path" ]] && continue
    bench="${csv%.smt2}"
    # -p: one line per job (plain `jobs -r` prints a multi-line group's whole body)
    while (( $(jobs -rp | wc -l) >= GROUPS_AT_ONCE )); do sleep 0.1; done
    (
        for i in "${!labels[@]}"; do
            d="$OUT/${labels[$i]}"
            # ${cmds[$i]} unquoted on purpose: binary + flags word-split
            python3 "$SCRIPT_DIR/measure.py" "$TIMEOUT" "$d/$bench" ${cmds[$i]} "$path" \
                > "$d/$bench.stdout" 2> "$d/$bench.solver_log" &
        done
        wait
    ) &
done < "$JOBS"
wait

# A run measure.py did not record (it crashed, or was killed) must not vanish from the comparison.
missing=$(while IFS=$'\t' read -r csv path || [[ -n "$csv" ]]; do
    [[ -z "$csv" ]] && continue
    for label in "${labels[@]}"; do
        [[ -f "$OUT/$label/${csv%.smt2}.exit" ]] || echo "  $label/${csv%.smt2} (see its .solver_log)"
    done
done < "$JOBS")
[[ -z "$missing" ]] || { printf 'ERROR: runs with no recorded result:\n%s\n' "$missing" >&2; exit 1; }

compare_args=()
for label in "${labels[@]}"; do
    python3 "$SCRIPT_DIR/parse_results.py" "$OUT/$label" >&2 || exit 1
    compare_args+=("$label=$OUT/$label/summary.csv")
done
python3 "$SCRIPT_DIR/compare_solvers.py" --cap "$TIMEOUT" "${compare_args[@]}" > "$OUT/compare.txt" || exit 1
echo "$OUT"
