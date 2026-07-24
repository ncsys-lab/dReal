# Benchmarking

Run `/benchmark` after every meaningful code change — the primary regression-detection mechanism.
Run proactively at natural breakpoints even if the user doesn't ask.

---

## Skills

- `/benchmark` — runs ~8-12 family-weighted benchmarks in parallel, spawns a Haiku subagent to
  interpret results, reports back a 2-4 sentence summary with regression/exceptional counts
- `/benchmark-baseline` — runs all odeexpr_v1 + all odeexpr_v2 + ~10 each of the three flat
  families to establish a fresh local baseline (use before branch merges or when the exceptional
  list grows stale)

---

## Thresholds

- **Regression:** PAR2 time >1.5× baseline (PAR2 = actual CPU time if solved, 2× timeout = 1200 s
  if TIM/OOM/ERR)
- **Exceptional:** PAR2 time <0.6× baseline
- **Correctness flip** (SAT↔UNSAT): immediate escalation regardless of timing (a "zero flips"
  result does **not** clear a correctness-class change — curated unit tests catch sharp cases a
  corpus can't)
- **Reporting a speedup — exclude both-TIM benchmarks.** Benchmarks that time out on *both* the
  control and the variant add an equal penalty to each side and can dominate the raw PAR2 sum
  (e.g. 12/50 odeexpr TIM for every config ≈ 92% of the sum), diluting a real win to near-zero.
  Report PAR2 over the subset solvable by *either* arm (≈ CPU on commonly-solved) as the honest
  speedup, alongside the raw aggregate — a win concentrated in a few benchmarks is otherwise hidden.

---

## Benchmark sources

Two **content-addressed manifest families** (`odeexpr_v*`) plus three flat-directory families.
A manifest family keys each logical benchmark on a stable `bench_id` (the manifest key) and
resolves through `manifest.json` to the *current* revision, so regeneration (new hashed files,
never overwritten) never silently repoints a name. `benchmark/odeexpr.py` is the registry for all
families (`MANIFEST_FAMILIES`, `FAMILY_WEIGHTS`, `family_of`, `load_manifest_names`,
`resolve_manifest`); `python3 odeexpr.py --all [FAMILY]` dumps a manifest family's TSV.

- `~/Documents/new_dreal/ode_expressivity/benchmarks/` — `odeexpr_v1` family (self-contained
  `.smt2`, content-addressed via `manifest.json` with `revisions[].file`; each sets its own
  `:precision`; NRA-only — no ODEs; no ground-truth `:status`)
- `~/Documents/new_dreal/ode_expressivity_energy/benchmarks/` — `odeexpr_v2` family (**newest
  high-priority target**; ∀/∃∀ MLP-expressivity queries in `forall/` + `exists_forall/`,
  content-addressed via `manifest.json` with `revisions[].smt2` — a *different* manifest schema
  from v1, handled by the same loader parameterized over the revision-file key)
- `~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/` — `github_oct5_` family
- `~/Documents/new_dreal/nraode_to_nra/VNAMSCwI_satoct11/rolled/` — `tacas_c2e2_` family
- `~/Documents/new_dreal/AMS-verification-bundle-of-sticks/saradc/rolled/` — `1mhz_` family

---

## Infrastructure (`benchmark/` directory)

- `baseline.csv` — frozen DRPM_0L reference times for 102 benchmarks (good_benchmarks.csv subset)
- `baseline_<family>.csv` (e.g. `baseline_odeexpr_v1.csv`, `baseline_odeexpr_v2.csv`) —
  manifest-family reference times (`cpu_time_s` column); produced by
  `do_baseline_odeexpr.sh <family>`; authoritative for `aggregate.py` on that family's rows
- `odeexpr.py` — registry for all families: `MANIFEST_FAMILIES` (the `odeexpr_v*` content-addressed
  families, each `(name, root, rev_file_key)`), `FAMILY_WEIGHTS`, `family_of`, `weight_of`,
  manifest-based `load_manifest_names`/`resolve_manifest`, `--all [FAMILY]` TSV dump
- `state.json` — persistent anomaly/exceptional tracker; updated automatically each run
- `run_batch.sh` — parallel runner: reads TSV from stdin, runs each with `gtime -v -o`,
  `nice -n 1`, `timeout 600`. Env hooks: `DREAL_ARGS` injects per-invocation solver flags;
  `TIMEOUT` overrides the 600 s cap; `DREAL_BINARY` overrides the solver binary. Throttle caps
  at 12 concurrent — count actual solvers with `pgrep -x dreal4`, NOT `pgrep -f gcc_build/dreal4`
  (the latter also matches `gtime`/`nice`/`timeout` wrappers, ~3 per solve)
- `select.py` — picks 8 **family-weighted** random benchmarks + all current anomalies; outputs
  TSV (csv_name TAB filepath). `--family a,b,c` restricts corpus to those families
  (`odeexpr_v1,odeexpr_v2,saradc,github,tacas`); `--all` emits every benchmark of the filtered corpus
  deterministically (no random, no anomalies) — for an A/B over a fixed set.
  **OOM exclusion** (`_is_oom_risk`): github/tacas `_k<N>_` with N ≥ 1024, saradc `_<N>b_` with
  N ≥ 9 — these crash the OS; the filter applies inside `load_benchmarks` so it covers both
  `select.py` and `select_baseline.py`
- `do_ab.sh BIN_A BIN_B [jobs_file]` — A/B two solver builds over the **same** jobs (default =
  full ODE family). Runs **sequentially** (never concurrently — overlapping batches starve jobs
  and turn real solves into false wall-clock TIMs) via `run_batch.sh` (`DREAL_BINARY`), parses
  each, and emits a `compare_solvers.py` table. The sanctioned way to compare e.g. committed-HEAD
  vs a working-tree build
- `do_sweep.sh NAME1="flags1" NAME2="flags2" …` — sweeps **one** binary over many flag configs
  on the same jobs, for meta-parameter tuning. Runs **all (config × benchmark) pairs in ONE
  shuffled 12-way pool** so a slow config's long-pole overlaps other configs' fast jobs — full CPU
  use, no idle tail, while still ≤`MAXJOBS` concurrent (per-process CPU-time stays accurate).
  Env: `JOBS` (default `probe_odes.tsv`), `DREAL_BINARY`, `MAXJOBS` (12), `TIMEOUT` (600).
  Emits per-config `summary.csv` + a `compare_solvers.py` table
- `parse_results.py` — parses gtime output + solver stdout into `summary.csv` (primary timing
  column `cpu_time_s` = user+sys; `wall_time_s` kept as reference/TIM backup)
- `drpm_log.py` — post-hoc extractor for `drpm_benchmark_log` stderr lines (one per learned theory
  lemma: `L <size> <mode>`, `T.ms`, `PM.ms`, optional CAV26 `C26.*`). Library
  (`parse_line`/`scan_sweep`/`summarize`/`ascii_histogram`) + a generic CLI over any numeric field:
  `python3 drpm_log.py <sweep_dir> --field lemma_size|theory_ms|… [--compare-to <ref> --same-verdict]
  [--csv out.csv]` → per-config histograms and verdict-gated paired per-benchmark median-Δ. Fail-loud
  on format drift (a signature line that won't parse raises). Tests: `test_drpm_log.py` (stdlib-only)
- `aggregate.py` — compares vs baseline on CPU time, flags regressions/exceptional, updates
  `state.json`
- `results/` — per-run output directories (gitignored)

---

## Families and weighting

Five families, classified by name prefix (`odeexpr.family_of`):

- `saradc` — prefix `1mhz_`
- `github` — prefix `github_oct5_`
- `tacas` — prefix `tacas_c2e2_`
- `odeexpr_v1` — prefix `odeexpr_v1_<bench_id>` (**high-priority**; NRA-only — no ODEs, runs the
  `Fixpoint[IbexFwdbwd, Integer]` ICP path)
- `odeexpr_v2` — prefix `odeexpr_v2_<bench_id>` (**newest, highest-priority**; ∀/∃∀
  MLP-expressivity queries — exercises the `ContractorForall` CE-guided path, coverage no other
  family provides)

`FAMILY_WEIGHTS = {odeexpr_v2:8, odeexpr_v1:6, saradc:3, github:2, tacas:2}` encodes relative
importance. The weight drives weighted-without-replacement selection (the `odeexpr_v*` families
appear proportionally more often per item) and a `weighted_overall` PAR2 in the family comparison;
regressions in either manifest family are tagged `ODEEXPR`/`ODEEXPR-HIGH` so reports lead with them.

**Note:** `OPTIMIZATION_LOG.md` (§Adopted/§Rejected) is all CAPD/ODE-path tuning and is
**orthogonal to the `odeexpr_v*` families** — odeexpr_v1 is NRA-only and odeexpr_v2 is ∀/∃∀, neither
has ODEs.

---

## Timing & running batches safely

The metric is **CPU time (user+sys)**, not wall clock — the machine is multi-tenant, so wall
clock is noisy. Solver runs under `nice -n 1`. `timeout` stays wall-clock at **600 s** (TIM
detection keys on exit code 124). Operational rules for any batch / A-B / sweep:

- **≤12 concurrent solvers, one pool at a time.** The `run_batch.sh` throttle (its own shell
  jobs via `jobs -r`, so it works for any `DREAL_BINARY` name) and `do_sweep`'s shuffled 12-way
  pool already enforce this and keep the cores saturated — see those bullets above. Never
  overlap two pools, and start no ad-hoc `dreal4` while a pool is live. (When *checking* on a
  live pool, count executables — `ps -axo comm | grep -c 'dreal4[^ ]*$'` — not `ps aux | grep`,
  whose gtime/timeout wrapper lines triple the apparent count.)
- **SIGKILL ⇒ blacklist, never restart.** The machine runs `oom_killer`/`swap_killer` daemons
  that SIGKILL any process over **8 GB RAM**. A solver exit *by signal* (exit code **137** =
  128+SIGKILL, or "Killed") is a memory event, not a result: do not retry it; append it to
  `benchmark/optsearch/blacklist.txt` and exclude it from future rounds (report it excluded,
  never as TIM/ERR). This is the dynamic complement to `select.py`'s static `_is_oom_risk`.
  Filter a jobs file with
  `awk -F'\t' 'FILENAME==ARGV[1]{bl[$0]=1;next} !($1 in bl)' blacklist jobs.tsv` — use the
  `FILENAME==ARGV[1]` form, **not** `NR==FNR`, which mis-handles an empty blacklist and silently
  drops every row (→ a 0-job no-op sweep).
- **A timing run needs a quiet machine — don't compile during one.** A background build (`-j`,
  or a CLion auto-build that recompiles on save) contends for cores and inflates wall time /
  risks false 600 s TIMs. Correctness work (ctest) may overlap; baselines / A-Bs / sweeps may not.
- **Compare ratios *within* a round, not absolute CPU across rounds.** Absolute CPU drifts
  ~10–17% between runs (memory-bandwidth contention), so always pool a `base`/control config with
  the variants and report ratio-vs-base; confirm a winner in a final pooled round, never by
  diffing two separately-run batches.

---

## Manual invocation

```bash
python3 benchmark/select.py | bash benchmark/run_batch.sh benchmark/results/run_$(git rev-parse --short HEAD)_$(date +%s)
python3 benchmark/parse_results.py <results_dir>
python3 benchmark/aggregate.py <results_dir>
```

Re-baseline a manifest family (after the set is regenerated, or to refresh reference times):

```bash
bash benchmark/do_baseline_odeexpr.sh odeexpr_v1   # all v1 at 600 s → benchmark/baseline_odeexpr_v1.csv
bash benchmark/do_baseline_odeexpr.sh odeexpr_v2   # all v2 at 600 s → benchmark/baseline_odeexpr_v2.csv
```

---

## Cross-solver comparison

`run_batch.sh` honors `DREAL_BINARY`, so any alternate native build can be run over the same jobs:

```bash
DREAL_BINARY=/usr/local/bin/dreal4_cav26 bash benchmark/run_batch.sh <out_dir> /tmp/jobs.tsv
```

`run_dreal3.sh` runs the set through dReal v3.16.12 in Docker (`dreal3:1.1`). **macOS gotcha:**
enforce the timeout *inside* the container (`timeout -s KILL 600 ./dReal`) — a host-side
`timeout` around `docker run` only kills the docker client, leaving the container running in the
VM as a zombie. Timing is the in-container CPU time (bash `time`); SIGKILL exit (137) normalized
to TIM.

`compare_solvers.py LABEL=summary.csv …` joins per-solver summaries by benchmark and reports
solve counts, SAT/UNSAT disagreements, solve-set deltas, and CPU-time speedups on
commonly-solved benchmarks. Frozen reference results (odeexpr_v1 set):
`baseline_odeexpr_cav26.csv`, `baseline_odeexpr_dreal3.csv`; rendered table:
`odeexpr_solver_comparison.txt`.

As of HEAD (arm64, upgraded IBEX/CAPD): identical solve-set + verdicts vs cav26 but ~2–3×
faster; ~6–20× faster than dReal3, which also solves 2 fewer. No SAT/UNSAT disagreements among
the three.

---

## Experimental design for sweeps

For meta-parameter tuning: OFAT probe → interaction check → 123-job confirm. Sweep metric and
soundness-flip rules: `OPTIMIZATION_LOG.md` "2026-06 re-tuning campaign".
