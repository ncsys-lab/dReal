# Benchmarking

Run `/benchmark` after every meaningful code change — the primary regression-detection mechanism.
Run proactively at natural breakpoints even if the user doesn't ask.

**Where a run happens.** A spot check — at most 12 benchmarks; `/benchmark`'s 8 take ~5 min at
the 120 s cap — runs on this Mac (`/benchmark`, or `benchmark/corun.sh` by hand). Anything larger (a family-wide A/B, a flag
sweep, a pre-merge gate) runs on Sherlock through `../dreal-stanford-benchmarking`: write an
`experiments/<id>.py` spec and follow that repo's CLAUDE.md workflow. `corun.sh` refuses a job set
over 12.

**Overlap the two.** When a change will also need a Sherlock run, commit and push it, and start
the Sherlock builds (`bench build`, ~30–40 min per commit, one at a time under the dev QOS)
before the local spot check, which finishes first (~5 min at the 120 s cap). If the spot check
shows a correctness problem or a regression you won't ship, stop the build — `scancel` its dev
job (`squeue --me`) — and nothing was submitted. If it passes, run the pre-submit gate and submit
as soon as the builds land. A submitted run that turns out unwanted is cancelled with
`scancel --name=<run>` (a cancelled job still counts against the hourly submission cap).

---

## Comparison rule

Sherlock's rule (`../dreal-stanford-benchmarking/docs/environment.md` §"Comparison-validity
invariant") holds here too: **two runs compare only if they ran at the same time, on the same
hardware, and only as a ratio.** An absolute time is never compared with one from another run.
Arms that never co-ran relate through an arm they share: from a base→head run and a head→fix
run, base→fix ≈ (base→head) × (head→fix). That is an estimate, not a measurement. It assumes the
shared arm kept the same relative speed in both runs, a timeout's 2×-cap PAR2 penalty does not
scale with the machine, and a verdict flip is only read off a pair that co-ran. So Sherlock
co-runs two arms per job and chains changes this way (`../dreal-stanford-benchmarking`
`config.MAX_JOB_CPUS`). This Mac adds a fourth condition, the core type: on Apple Silicon a run's speed depends on
whether it got P-cores or E-cores, and macOS gives no way to pin a process to P-cores
(`taskpolicy` only clamps down; `THREAD_AFFINITY_POLICY` is an L2-sharing hint).

`corun.sh` enforces all four. Every arm of one benchmark launches together and the group
finishes before its slot frees. Every run goes through `measure.py`, which puts it in the
background band (`PRIO_DARWIN_BG`, what `taskpolicy -b` sets): E-cores only, below every `nice`
level. The time limit is CPU seconds (`RLIMIT_CPU`), not wall clock, and `parse_results.py`
refuses an arm directory in which any run spent more than 10% of its CPU time on P-cores.

Measured 2026-10-07 (M4 Max, 4 short loops per setting):

| run | P-core share, quiet → under 12 competing processes | CPU time, same |
|---|---|---|
| default priority | 1.00 → 0.77–0.80 | +22% |
| background band (`taskpolicy -b`) | 0.00 → 0.00 | +3% |

So machine load — a compile, a CLion build — slows a spot check but can't skew it, and the Mac
stays usable while one runs. The cost: an E-core takes 2.2–5.5× the CPU time of a P-core for
the same dReal run (median 3.3×, measured 2026-10-07 by co-running 7 benchmarks pinned each way —
it depends on the benchmark, which is also why a mixed-core A/B is invalid), and there are 4 of
them, so a two-arm spot check runs 2 benchmarks at a time.

---

## Skills

- `/benchmark` — co-runs a stashed control build (`benchmark/bin/dreal4-<sha>`, made by
  `stash.sh`) against `gcc_build/dreal4` on ≤12 family-weighted benchmarks; a Haiku subagent
  reports flips, regressions and exceptional cases as test/control ratios

---

## Thresholds

All ratios are test/control within one co-run. PAR2 = CPU seconds if solved, 2× the CPU cap
(240 s at the default 120) if TIM/ERR; a memory kill (OOM) is excluded, not scored.

- **Regression:** PAR2 ratio > 1.5
- **Exceptional:** PAR2 ratio < 0.6
- **Wrong verdict:** `aggregate.py` checks the test arm's every verdict against `baseline.csv`'s
  ground truth, not only verdicts that differ from the control's. `unsat` on a ground-truth-SAT
  benchmark is SOUNDNESS (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat), and
  `delta-sat` on a ground-truth-UNSAT one is COMPLETENESS (asserts φ^δ T-satisfiable on a
  T-unsatisfiable φ — missed refutation). A SAT↔UNSAT flip with no ground truth is undetermined.
  Any of these is immediate escalation regardless of timing, and is left out of the PAR2 ratios
  (a "zero flips" result does **not** clear a correctness-class change — curated unit tests
  catch sharp cases a corpus can't)
- **Reporting a speedup — exclude both-TIM benchmarks.** Benchmarks that time out on *both* the
  control and the variant add an equal penalty to each side and can dominate the raw PAR2 sum
  (e.g. 12/50 odeexpr TIM for every config ≈ 92% of the sum), diluting a real win to near-zero.
  Report PAR2 over the subset solvable by *either* arm (≈ CPU on commonly-solved) as the honest
  speedup, alongside the raw aggregate — a win concentrated in a few benchmarks is otherwise hidden.
  `OUT/compare.txt` (`compare_solvers.py`) scores that subset; `aggregate.py`'s `family_ratios`,
  the table `/benchmark` reports, are the raw aggregate (both-TIM benchmarks included).

---

## Benchmark sources

Three **content-addressed manifest families** (`odeexpr_v*`, `s2d`) plus three flat-directory families.
A manifest family keys each logical benchmark on a stable `bench_id` (the manifest key) and
resolves through `manifest.json` to the *current* revision, so regeneration (new hashed files,
never overwritten) never silently repoints a name. `benchmark/odeexpr.py` is the registry for all
families (`MANIFEST_FAMILIES`, `FAMILY_WEIGHTS`, `family_of`, `load_manifest_names`,
`resolve_manifest`); `python3 odeexpr.py --all [FAMILY]` dumps a manifest family's TSV.

- `~/Documents/expressivity/v1/benchmarks/` — `odeexpr_v1` family (self-contained
  `.smt2`, content-addressed via `manifest.json` with `revisions[].file`; each sets its own
  `:precision`; NRA-only — no ODEs; no ground-truth `:status`)
- `~/Documents/expressivity/v2/benchmarks/` — `odeexpr_v2` family (**newest
  high-priority target**; ∀/∃∀ MLP-expressivity queries in `forall/` + `exists_forall/`,
  content-addressed via `manifest.json` with `revisions[].smt2` — a *different* manifest schema
  from v1, handled by the same loader parameterized over the revision-file key)
- `~/Documents/MATLAB/final_presentation_casestudies/benchmarks/s2d/` — `s2d` family (Simulink
  models translated by s2d, `QF_NRA_ODE` hybrid unrollings with Stateflow charts; picked for
  being hard: about a third timed out where they were recorded; v2's manifest schema).  Written by `python -m
  pipeline.benchmarks` in that repo, whose docstring says how a query is picked.  The queries
  were asked there with other options, but the family runs at dReal's defaults.  The longest
  queries run to 82 steps, and a 71-step one peaked near 5.7 GB there; the whole family
  (`--family s2d --all`) is more than a spot check, so it runs on Sherlock (set `s2d`).
- `~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/` — `github_oct5_` family
- `~/Documents/new_dreal/nraode_to_nra/VNAMSCwI_satoct11/rolled/` — `tacas_c2e2_` family
- `~/Documents/new_dreal/AMS-verification-bundle-of-sticks/saradc/rolled/` — `1mhz_` family

---

## Infrastructure (`benchmark/` directory)

- `corun.sh OUT JOBS ARM…` — the local A/B harness (see §Comparison rule). `ARM` is
  `label=<binary> [flags…]`; one arm or many (a flag sweep is arms of one binary). Output: one
  subdir per arm with `<bench>.{stdout,solver_log,rusage,exit}` and `summary.csv`, plus
  `OUT/compare.txt`. Env: `TIMEOUT` (CPU-seconds cap, 120), `MAXJOBS` (the E-core count)
- `measure.py CAP PREFIX CMD…` — runs one solve in the background band under `RLIMIT_CPU`, and
  writes `PREFIX.rusage` (CPU and wall seconds, P-core share, instructions, cycles, peak
  footprint, from `proc_pid_rusage`) and `PREFIX.exit` (128+N for signal N: 152 at the CPU cap,
  137 for the oom_killer)
- `do_benchmark.sh CONTROL` — the `/benchmark` driver: `select_jobs.py` → `corun.sh` (control vs
  `gcc_build/dreal4`) → `aggregate.py`
- `stash.sh` — builds HEAD (`BUILD.sh`) and keeps the binary as `bin/dreal4-<sha>`, a control for
  later spot checks; refuses when `src/`, `cmake/` or `CMakeLists.txt` differ from HEAD, or when
  `gcc_build`'s IBEX/CAPD checkouts differ from the `CMakeLists.txt` pins. `bin/` is gitignored
- `baseline.csv` — the flat families' corpus index (`select_jobs.py` reads its names) and their
  `ground_truth` annotations (`aggregate.py` checks every test verdict against them). Its time
  columns (DRPM_0L, DRPM_16L_200ms, dReal3) are a record from another machine, not a reference
- `odeexpr.py` — registry for all families: `MANIFEST_FAMILIES` (the content-addressed families
  `odeexpr_v1`, `odeexpr_v2`, `s2d`, each `(name, root, rev_file_key)`), `FAMILY_WEIGHTS`, `family_of`, `weight_of`,
  manifest-based `load_manifest_names`/`resolve_manifest`, `--all [FAMILY]` TSV dump
- `state.json` — a log of past spot checks (arms, counts); `aggregate.py` appends to it. Nothing
  carries between runs: a regression is a regression against that run's control
- `select_jobs.py` — picks 8 **family-weighted** random benchmarks, never a blacklisted one; outputs
  TSV (csv_name TAB filepath). `--family a,b,c` restricts corpus to those families
  (`odeexpr_v1,odeexpr_v2,s2d,saradc,github,tacas`); `--all` emits every benchmark of the filtered
  corpus deterministically (no random) — cut it to ≤12 for `corun.sh`. A family-wide run on
  Sherlock names the registered set instead (`Experiment.benchmark_sets`, from
  `../dreal-stanford-benchmarking/benchmarks/registry.py`): `odeexpr_v1` → `expressivity_v1`,
  `odeexpr_v2` → `expressivity_v2` (not Sherlock's older `odeexpr_v2` set, a different corpus),
  `s2d` → `s2d`, `saradc` → `saradc`, `github` → `github_dreach`, `tacas` → `tacas_c2e2`
  (the flat-family sets are whole source directories, larger than `baseline.csv`'s rows).
  **OOM exclusion** (`_is_oom_risk`): github/tacas `_k<N>_` with N ≥ 1024, saradc `_<N>b_` with
  N ≥ 9 — these crash the OS; the filter applies inside `load_benchmarks`
- `parse_results.py` — parses one arm dir (`.rusage` + solver stdout) into `summary.csv`;
  `cpu_time_s` = user+sys is the timing column, `p_share` the P-core share it checks
- `drpm_log.py` — post-hoc extractor for `drpm_benchmark_log` stderr lines (one per learned theory
  lemma: `L <size> <mode>`, `T.ms`, `PM.ms`, optional CAV26 `C26.*`). Library
  (`parse_line`/`scan_sweep`/`summarize`/`ascii_histogram`) + a generic CLI over any numeric field:
  `python3 drpm_log.py <sweep_dir> --field lemma_size|theory_ms|… [--compare-to <ref> --same-verdict]
  [--csv out.csv]` → per-config histograms and verdict-gated paired per-benchmark median-Δ. Fail-loud
  on format drift (a signature line that won't parse raises). Tests: `test_drpm_log.py` (stdlib-only)
- `aggregate.py RUN_DIR` — compares the `test` arm with the `control` arm of one co-run as
  ratios: wrong verdicts and flips (by ground truth), regressions, exceptional cases, per-family
  PAR2 ratios; excludes and blacklists a memory-killed benchmark; logs the run in `state.json`
- `compare_solvers.py --cap N` — the same comparison for any number of arms of one co-run
  (`corun.sh` writes it to `compare.txt`); excludes a memory-killed benchmark
- `baseline_odeexpr_cav26.csv`, `baseline_odeexpr_dreal3.csv`, `baseline_quant.csv`,
  `odeexpr_solver_comparison.txt`, `optsearch/` (except `optsearch/blacklist.txt`, the live
  memory-kill blacklist) — records of past runs; not comparable with a new run
- `results/` — per-run output directories (gitignored)

---

## Families and weighting

Six families, classified by name prefix (`odeexpr.family_of`):

- `saradc` — prefix `1mhz_`
- `github` — prefix `github_oct5_`
- `tacas` — prefix `tacas_c2e2_`
- `odeexpr_v1` — prefix `odeexpr_v1_<bench_id>` (**high-priority**; NRA-only — no ODEs, runs the
  `Fixpoint[IbexFwdbwd, Integer]` ICP path)
- `odeexpr_v2` — prefix `odeexpr_v2_<bench_id>` (**newest, highest-priority**; ∀/∃∀
  MLP-expressivity queries — exercises the `ContractorForall` CE-guided path, coverage no other
  family provides)
- `s2d` — prefix `s2d_<bench_id>` (**high-priority**; ODE hybrid unrollings from Simulink, the
  CAPD/ODE path at tens of steps with Stateflow mode logic)

`FAMILY_WEIGHTS = {odeexpr_v2:8, s2d:8, odeexpr_v1:6, saradc:3, github:2, tacas:2}` encodes relative
importance. The weight drives weighted-without-replacement selection (the `odeexpr_v*` families
appear proportionally more often per item) and a `weighted_overall` PAR2 ratio in `aggregate.py`;
regressions in any manifest family are tagged `ODEEXPR`/`ODEEXPR-HIGH` so reports lead with them.

**Note:** `OPTIMIZATION_LOG.md` (§Adopted/§Rejected) is all CAPD/ODE-path tuning and is
**orthogonal to the `odeexpr_v*` families** — odeexpr_v1 is NRA-only and odeexpr_v2 is ∀/∃∀, neither
has ODEs.

---

## Running a spot check safely

The metric is **CPU time (user+sys)** on the E-cores, as a ratio within one co-run. The limit is
**120 CPU seconds** (`RLIMIT_CPU`; a capped run exits 152 and parses as TIM), so machine load
lengthens a spot check without turning solves into timeouts. Batches leave dReal's own `-j`/`--jobs` at its default
**1** on purpose: pool width is the parallelism, and one thread per solve is what keeps
per-process CPU time an interpretable metric. Operational rules for a spot check:

- **One spot check at a time, on the E-cores.** `corun.sh` runs as many solvers as there are
  E-cores (`hw.perflevel1.logicalcpu`, 4), whole co-run groups at a time. Never overlap two
  spot checks: they would share the four E-cores unevenly.
- **SIGKILL ⇒ blacklist, never restart.** The machine runs an `oom_killer` daemon (C,
  `/usr/local/src/oom_killer/`; replaced the old oom_killer.sh/swap_killer.sh pair) that
  SIGKILLs any process over **10 GB** phys_footprint — or the largest process when total
  user footprint crosses 87.5% of RAM. A solver killed by SIGKILL (exit code **137** =
  128+SIGKILL, or "Killed") is a memory event, not a result: never retry it (152, SIGXCPU, is the
  CPU cap: TIM). `aggregate.py`
  reports it excluded (never TIM/ERR) and appends it to `benchmark/optsearch/blacklist.txt`,
  which `select_jobs.py` skips — the dynamic complement to its static `_is_oom_risk`. To filter
  a hand-made jobs file, use
  `awk -F'\t' 'FILENAME==ARGV[1]{bl[$0]=1;next} {n=$1; sub(/\.smt2$/,"",n)} !(n in bl)' blacklist jobs.tsv`
  (the `FILENAME==ARGV[1]` form, **not** `NR==FNR`, which mis-handles an empty blacklist and
  silently drops every row). Blacklist names carry no `.smt2`.
- **Compiling during a spot check is fine.** The solvers sit below everything else on the
  E-cores and are capped in CPU seconds, so a build slows the check without changing its
  ratios (§Comparison rule, measured).
- **Compare ratios *within* a run, never absolute CPU across runs.** Absolute CPU drifts
  ~10–17% between runs (memory-bandwidth contention) even on one core type; put the control in
  the same `corun.sh` call as the variants.

---

## Choosing the cap

A benchmark that times out in both arms says nothing, so the cap trades informative picks for
wall time. Modeled 2026-10-07 from P-core solve times (odeexpr: the 2026-07-24 default-flag sweep;
ODE families: the 2026-10-05 PERF-001 A/B; s2d: its manifest's recorded runs), converted at the
measured median E/P factor 3.3, and weighted by `select_jobs.py`'s family mix (odeexpr_v2 42%,
s2d 31%, odeexpr_v1 16%, github 6%, tacas 4%, saradc 1%) — an estimate, not a measurement:

| E-core cap | informative picks of 8 | spot-check wall time |
|---|---|---|
| 60 s | 4.6 | ~3 min |
| **120 s (default)** | **5.4** | **~5 min** |
| 300 s | 5.8 | ~9 min |
| 600 s | 6.0 | ~16 min |

120 s keeps ~90% of the informative picks of 600 s in ~30% of the time (83–93% across the
measured E/P range). The picks lost are the slow tail (s2d most: 0.63 of its picks solve at 120 s
against 0.70 at 600 s), and that is the useful side of the trade. A spot check is for the big
changes: an optimization worth having moves a benchmark from a timeout (PAR2 penalty 2× the cap)
to a solve under the cap, a clean signal, while a longer cap mostly adds small ratio shifts on
slow solves.

Sherlock runs use 120 s too (`Experiment.timeout_min` defaults to 2). The 2026-10-07
`tech_debt_fixes_gate` run, which used 600 s, shows why: 67% of its benchmarks had an arm that
reached the cap, so its 459 jobs took ~2 h each and ~15 h overall at 64 concurrent jobs. Recomputed
from that run's trial times, a 120 s cap cuts the summed benchmark time from 746 h to 165 h and
loses the 14.7% of solves that needed more than 120 s of CPU.

---

## Manual invocation

```bash
bash benchmark/stash.sh                                   # → benchmark/bin/dreal4-<sha>
bash benchmark/do_benchmark.sh benchmark/bin/dreal4-<sha> # /benchmark without the subagent
python3 benchmark/select_jobs.py --family s2d --n 6 > /tmp/jobs.tsv
bash benchmark/corun.sh /tmp/ab /tmp/jobs.tsv control=benchmark/bin/dreal4-<sha> \
    test=gcc_build/dreal4 o20="gcc_build/dreal4 --ode-taylor-order 20"
```

---

## Cross-solver comparison

Another native binary is just another `corun.sh` arm. The stored `baseline_odeexpr_cav26.csv` /
`baseline_odeexpr_dreal3.csv` and `odeexpr_solver_comparison.txt` were measured in separate runs
and stay as records only. A cross-solver comparison at scale (z3, cvc5, another dReal) is a
Sherlock experiment with `Solver.binary_path` arms. The Docker dReal3 runner (`run_dreal3.sh`)
was retired 2026-10-07 with the stored baselines; it is in git history.

---

## Experimental design for sweeps

Sweeps run on Sherlock (`../dreal-stanford-benchmarking`, one `Solver` per config in an
`experiments/<id>.py`). For meta-parameter tuning: OFAT probe → interaction check → full
confirm, each config read as a ratio against a `base` solver of the same run, and every verdict
judged against ground truth as in §Thresholds, not only against `base`. `OPTIMIZATION_LOG.md`
"2026-06 re-tuning campaign" records the campaign that used this design; its harnesses are retired.
