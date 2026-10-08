---
name: benchmark
description: Run a quick co-run spot check (≤12 benchmarks) of the dReal4 working build against a stashed control build, and report verdict flips, regressions and exceptional speedups as ratios. Use after any meaningful code change for regression detection.
---

# /benchmark skill

A spot check co-runs two builds on the same benchmarks at the same moment, on the E-cores, and
compares them only as ratios (`docs/benchmarking.md` §"Comparison rule"). Anything bigger than
12 benchmarks runs on Sherlock, not here.

1. Pick the control: the stash of the commit the change is measured against, normally HEAD
   for uncommitted work (`benchmark/bin/dreal4-<sha>`). If it doesn't exist and the build
   inputs are committed, make it with `bash benchmark/stash.sh` (it builds HEAD and prints the
   path). If neither works, stop and tell the user which stash is missing. The test arm is
   `gcc_build/dreal4` — rebuild it first (`./BUILD.sh`) if the working tree changed.

2. Start the spot check as a background command (`run_in_background: true`; it can outlast the
   Bash tool's foreground limit), and tell the user: "Spot check running on the E-cores — I'll
   report back when done."
   ```bash
   bash /Users/kunalsheth/Documents/new_dreal/dreal4-cmake/benchmark/do_benchmark.sh <CONTROL>
   ```
   It prints OUT_DIR on stdout when done; a nonzero exit is a harness failure — report it, don't
   summarize.

3. When it completes, spawn a Haiku subagent (pass `model: "haiku"`) with this prompt,
   `<OUT_DIR>` filled in:

---
Use the Read tool to read `<OUT_DIR>/aggregate.json`. Do not run any commands.

Return a formatted summary as your only output:
- If `correctness_flips` is non-empty, lead with: **CORRECTNESS REGRESSION**: [names] — the test build gave `unsat` where ground truth is SAT: SOUNDNESS (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat).
- If any regression has priority `COMPLETENESS`, say so next: [names] gave `delta-sat` where ground truth is UNSAT: COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed refutation).
- If `undetermined_flips` is non-empty: [names] flipped with no ground truth (soundness or completeness — needs adjudication).
- If `excluded_oom` is non-empty: [names] were killed for memory, excluded and blacklisted.
- Then, if any regression has priority `ODEEXPR-HIGH`/`ODEEXPR` (names start `odeexpr_v1_` / `odeexpr_v2_` / `s2d_`), lead with those: **ODEEXPR REGRESSION**: [names].
- First line: `N ran, M regressions, K exceptional`
- 2–3 sentences on overall health using the per-benchmark ratios (e.g. "test/control PAR2 0.04×, control solved, test timed out"). Report ratios only — never absolute seconds.
- Last line: `anomaly_report: <OUT_DIR>/anomaly_report.txt`

Then ALWAYS render `family_ratios` as one markdown table: Family | Weight | n | test/control PAR2 ratio. Sort by descending weight, `weighted_overall` last. Flag ratio > 1.5 as a regression and < 0.6 as exceptional.

Be terse. Only return the summary and the table — no narration.

---

4. Relay the subagent's summary verbatim to the user.

## Notes
- A spot check runs on the 4 E-cores (macOS can't pin to P-cores), where a dReal run takes
  2.2–5.5× (median 3.3×) the CPU time it would on a P-core, under a CPU-seconds cap (`TIMEOUT`, default 120;
  ~5 min). It is load-proof: the user may compile or work while it runs.
- If the change also needs a Sherlock run, push it and start the Sherlock builds before the spot
  check, and stop them if it fails (`docs/benchmarking.md` §"Where a run happens").
- Ad hoc A/Bs and flag sweeps use `benchmark/corun.sh` directly (same 12-benchmark limit);
  anything larger is a Sherlock experiment (`../dreal-stanford-benchmarking`) with two arms,
  control and test: 4-core jobs schedule far sooner than bigger ones. Chain a third arm through
  a shared one (base→head, head→fix; multiply the ratios). `docs/benchmarking.md` §"Where a run
  happens".
