#!/usr/bin/env bash
#
# ThreadSanitizer gate for the forall (∃∀) + parallel-ICP concurrency — the two
# under-maintained features whose COMBINATION this audit targets. It builds the
# `dreal4` binary with -fsanitize=thread in a dedicated build dir and drives it on
# forall instances under --jobs>1, repeated, exercising the per-thread machinery
# (ContractorForallMt, ContractorIbexForallMt, ForallFormulaEvaluator, all now on
# PerThread<T>) and the lock-free IcpParallel worker pool.
#
# NRA forall uses no CAPD, so this run avoids CAPD/rounding-mode TSan noise.
# Known-benign third-party reports (libcds reclamation, gaol FPU register) are
# filtered by tsan_suppressions.txt; dReal's own code is never suppressed.
#
# NOTE: the dedicated build dir rebuilds IBEX/CAPD/etc. from source — the first
# run is SLOW (tens of minutes). Subsequent runs are incremental.
#
# Usage: ./tsan_gate.sh        Env: JOBS=4 REPS=10
set -u
cd "$(dirname "$0")"
BUILD=cmake-build-tsan
SUPP="$(pwd)/tsan_suppressions.txt"
JOBS="${JOBS:-4}"
REPS="${REPS:-10}"

echo "=== configuring $BUILD with ThreadSanitizer ==="
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" || { echo "configure failed"; exit 1; }

echo "=== building dreal4 (TSan) — rebuilds IBEX/CAPD from source, slow ==="
# -j6 (not -j8): TSan instrumentation inflates per-compile memory; stay clear of
# the oom_killer daemon (10 GB per process, or the largest past 87.5% of RAM).
cmake --build "$BUILD" --target dreal4 -j"${BUILD_JOBS:-6}" || { echo "build failed"; exit 1; }

BIN="$BUILD/dreal4"
export TSAN_OPTIONS="suppressions=$SUPP halt_on_error=0 history_size=4"
CORPUS=test/dreal/test/smt2
# SAT and UNSAT forall instances exercise both the witness-keeping and the
# box-emptying parallel paths.
INSTANCES=(exist_forall_01.smt2 exist_forall_02.smt2 exist_forall_05.smt2 \
           github_issue_181.smt2 ea_01.smt2)

echo "=== running forall instances under --jobs=$JOBS x$REPS (±pre-prune) ==="
races=0
for inst in "${INSTANCES[@]}"; do
  for pp in "" "--forall-pre-prune"; do
    for r in $(seq 1 "$REPS"); do
      out=$(timeout 180 "$BIN" --precision 0.001 --jobs "$JOBS" $pp \
              "$CORPUS/$inst" 2>&1)
      if echo "$out" | grep -q 'WARNING: ThreadSanitizer'; then
        races=$((races + 1))
        echo "RACE: $inst (jobs=$JOBS ${pp:-no-preprune} rep=$r)"
        echo "$out" | grep -A25 'WARNING: ThreadSanitizer' | head -30
      fi
    done
  done
done
echo "-------------------------------------------------"
[ "$races" -eq 0 ] && { echo "PASS: no ThreadSanitizer races in forall+parallel"; exit 0; }
echo "FAIL: $races ThreadSanitizer report(s) — see output above"; exit 1
