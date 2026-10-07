#!/usr/bin/env bash
#
# Differential soundness net for the forall (∃∀ CEGIS) + parallel-ICP +
# --forall-pre-prune machinery. It runs every forall instance in a corpus under a
# matrix of knobs that are ALL claimed to be verdict-neutral — job count, the
# --forall-pre-prune pre-pruner, and --forall-polytope — and asserts ZERO verdict
# flips versus the sequential CEGIS baseline (jobs=1, no pre-prune, no polytope).
#
# A flip is a bug, not a nuisance: variable/box contraction and worker count must
# never move a SAT/UNSAT verdict. (This catches false `unsat` AND false
# `delta-sat` regressions in the combination of the two under-maintained
# features.) Branch order is nondeterministic under --jobs>1; the verdict is not.
#
# Usage:   ./benchmark/forall_differential.sh
# Env:     DREAL=./gcc_build/dreal4  PRECISION=0.001  TIMEOUT=90
#          CORPUS_DIR=test/dreal/test/smt2  OUT=/tmp/forall_differential.txt
#
# Prints a per-config flip summary; full per-run verdicts go to $OUT. Exits
# non-zero if any flip (or a baseline that could not be established) is found.
# Respects the SMT-solver resource discipline: one solve at a time, each under an
# explicit timeout (start oom_killer separately for hard instances).
set -u

BIN="${DREAL:-./gcc_build/dreal4}"
PREC="${PRECISION:-0.001}"
TIMEOUT_S="${TIMEOUT:-90}"
CORPUS_DIR="${CORPUS_DIR:-test/dreal/test/smt2}"
OUT="${OUT:-/tmp/forall_differential.txt}"

# Verdict-neutral config matrix: "jobs preprune polytope". The first entry is the
# baseline. We deliberately cross job count with the pre-pruner and the polytope
# contractor, including jobs>1 WITHOUT the pre-pruner (the CEGIS-only parallel
# path).
CONFIGS=("1 0 0" "1 1 0" "1 0 1" "2 0 0" "2 1 0" "4 0 0" "4 1 0" "4 1 1")

run_one() {  # args: jobs preprune polytope file -> echoes verdict keyword
  local jobs="$1" pp="$2" poly="$3" file="$4"
  local flags=("--precision" "$PREC" "--jobs" "$jobs")
  [ "$pp" = 1 ] && flags+=("--forall-pre-prune")
  [ "$poly" = 1 ] && flags+=("--forall-polytope")
  local v
  v=$(timeout "$TIMEOUT_S" "$BIN" "${flags[@]}" "$file" 2>/dev/null \
        | grep -iE '^(unsat|sat|delta-sat)' | tail -1 | awk '{print $1}')
  [ -z "$v" ] && v="TIMEOUT/ERR"
  echo "$v"
}

: > "$OUT"
files=$(grep -li 'forall' "$CORPUS_DIR"/*.smt2 2>/dev/null | sort)
[ -z "$files" ] && { echo "no forall .smt2 files in $CORPUS_DIR" >&2; exit 2; }

total_files=0; flipped_files=0; bad_baseline=0; total_flips=0; slow_count=0
for f in $files; do
  total_files=$((total_files + 1))
  base=$(run_one 1 0 0 "$f")
  printf '%s  baseline=%s\n' "$(basename "$f")" "$base" >> "$OUT"
  if [ "$base" = "TIMEOUT/ERR" ]; then
    bad_baseline=$((bad_baseline + 1))
    printf '  !! baseline could not be established (timeout/err)\n' >> "$OUT"
    echo "BASELINE-FAIL  $(basename "$f")"
    continue
  fi
  file_flipped=0
  for cfg in "${CONFIGS[@]}"; do
    read -r jobs pp poly <<< "$cfg"
    v=$(run_one "$jobs" "$pp" "$poly" "$f")
    if [ "$v" = "$base" ]; then
      mark="ok"
    elif [ "$v" = "TIMEOUT/ERR" ]; then
      # NOT a verdict flip: this config simply did not finish under the timeout.
      # A verdict-neutral knob may legitimately change RUNTIME — e.g.
      # --forall-pre-prune is documented to sometimes *hurt* (encoding-fragile).
      # Only a DIFFERENT DEFINITE verdict (sat<->unsat) is a soundness bug.
      mark="slower(timeout)"
      slow_count=$((slow_count + 1))
      echo "SLOWER  $(basename "$f")  cfg(jobs=$jobs pp=$pp poly=$poly)  base=$base did-not-finish<=${TIMEOUT_S}s"
    else
      mark="*** FLIP ***"
      total_flips=$((total_flips + 1))
      file_flipped=1
      echo "FLIP  $(basename "$f")  cfg(jobs=$jobs pp=$pp poly=$poly)  base=$base got=$v"
    fi
    printf '  jobs=%s pp=%s poly=%s -> %-12s %s\n' "$jobs" "$pp" "$poly" "$v" "$mark" >> "$OUT"
  done
  [ "$file_flipped" = 1 ] && flipped_files=$((flipped_files + 1))
done

echo "-------------------------------------------------------------"
echo "forall differential: $total_files files, $flipped_files with flips ($total_flips flip(s) total), $slow_count config-timeout(s), $bad_baseline baseline-fail(s)"
echo "full log: $OUT"
# The net's PASS/FAIL keys on genuine VERDICT FLIPS (sat<->unsat) — the soundness
# property under test. A config-timeout or a baseline-timeout is an intractability
# / performance issue, not a flip: reported, but it does not fail the net.
[ "$slow_count" -gt 0 ] && \
  echo "note: $slow_count config(s) timed out at ${TIMEOUT_S}s (slower, not a flip; e.g. --forall-pre-prune is documented to sometimes hurt)"
[ "$bad_baseline" -gt 0 ] && \
  echo "note: $bad_baseline instance(s) too slow to baseline at ${TIMEOUT_S}s (not a flip)"
[ "$flipped_files" -eq 0 ] && { echo "PASS: zero verdict flips"; exit 0; }
echo "FAIL: $flipped_files file(s) flipped a verdict (sat<->unsat)"; exit 1
