# dReal4 Optimization Log (`upgrade-ibex-opts`)

Iterative profile → optimize → benchmark loop. Each idea lands in **Adopted** (committed)
or **Rejected** (with numbers + reason). Acceptance bar: net PAR2 improvement ≈≥10% on the
probe + gate sets, **zero** SAT/UNSAT correctness flips vs ground truth, no >1.5× regressions.
Soundness guardrails (FE_UPWARD/FE_TONEAREST guards, no silent fallbacks) are non-negotiable.

> **Scope:** §Adopted and §Rejected below are **all CAPD/ODE-path tuning** — the probe set
> is ODE-heavy and the baseline profile is an ODE benchmark where `contractor_ode_lohner`
> is 90.8% of runtime. The ODE-free `ode_expressivity` (odeexpr) family shares **none** of
> this code; for its assumption re-check and its (different) hotspot, see the
> **odeexpr** section at the bottom of this file.

## 2026-06 re-tuning campaign — runtime knobs + pooled sweep (IN PROGRESS)

The ODE contractor's performance regime **changed** under the soundness fixes on
`rounding-mode-fixes` (per-slice tube filter restored `5d619fe3f`; full-precision
vector-field feed `a925eba2c`; terminal-gate window clip `a60b0ff8e`). Those fixes were
measured against the *old* coarse-endpoint contractor, so the **§Adopted / §Rejected
numbers below are superseded** for the current architecture and are being re-measured by
this campaign. (They remain as the historical record of what was tried and why.)

### What changed in the code

The CAPD knobs are no longer compile-time `constexpr` in `contractor_odes_capd.cc`; they
are **runtime CLI flags** threaded through a `CapdSolverParams` struct (`ode_types.h`),
resolved per contractor instance from `Config` (`contractor_odes.cc` ctor, direction-aware
for the Taylor order):

| Flag | Default (= old constexpr) | Knob |
|---|---|---|
| `--ode-taylor-order` | 20 | forward CAPD `IOdeSolver` Taylor order |
| `--ode-backward-order` | 20 | backward (`-f(x)`) Taylor order (a lohner instance is single-direction) |
| `--ode-abs-tol` / `--ode-rel-tol` | 1e-10 | CAPD step-control tolerances |
| `--ode-hull-grid` | 16 | per-step tube sub-slices in the filter |
| `--ode-c0-set` | `rect2` | enclosure set: `rect2`/`tripleton`/`horect2` (runtime type-dispatch) |
| `--ode-backward` | `true` | enable the backward (X₀-narrowing) contractor (emit-guard in `theory_solver.cc`) |
| `--ode-max-step` | 0 (adaptive) | optional `setMaxStep` cap |

Defaults equal the prior constexprs, so default behavior is unchanged — **proven** by a
123-job A/B (`/tmp/dreal4_head` HEAD vs worktree@default): identical solve-set (117/123),
zero SAT/UNSAT disagreements, CPU within noise (aggregate 0.99×). The order-20 rationale
moved onto the `kDefaultOde*` constants in `solver/config.h`.

### Experimental design for a meta-parameter sweep

Screen wide and cheap, then confirm narrow and clean:

1. **OFAT on the probe set** (`benchmark/probe_odes.tsv`, 18 ODE-heavy: tacas inverters +
   github prostate/thermostat/quad/cardiac/water + saradc box/nonlinear; mix of fast k2 and
   stress k256+). Vary **one knob at a time** around the default; `base` is one of the
   configs. Screens main-effect *direction* and flags any verdict change.
2. **Targeted 2-factor sweeps** only for the architecturally-coupled pairs — **order ×
   hull-grid** (per-slice cost ≈ hull_grid × #steps, and #steps falls with order) and
   **order × c0-set**. Probe set only.
3. **Confirm** the best 1–3 configs on the **full 123 ODE-family** via the sequential
   `do_ab.sh` (cleaner timing than the pool) before any recommendation.

**Metric & guardrails.** CPU time (user+sys) **ratio vs `base`** is the screen — within one
pooled sweep, `base` and every variant are shuffled together so they share the same average
contention, making the *ratio* fair even though absolute CPU shifts ~10–17% vs an isolated
run (memory-bandwidth contention). A **SAT↔UNSAT change vs base is the soundness signal**: a
coarser enclosure (lower order / fewer slices / looser tol / backward-off) can only
*fail-to-refute* → a `base`-UNSAT turning delta-sat means base was the tighter/sounder answer;
investigate against the cav26 oracle, never silently accept. The probe screens **large**
effects reliably; sub-contention-noise effects need the confirmation run.

### Pooled sweep harness (`benchmark/do_sweep.sh`)

`do_sweep.sh NAME1="flags1" NAME2="flags2" …` sweeps **one** binary over many flag configs on
the same jobs and emits a `compare_solvers.py` table (N columns). Key behavior:

- **Pooled, not per-config batches.** All (config × benchmark) pairs run in **one shuffled
  12-way pool**, not 20 separate 18-job batches. *Why:* an 18-job probe drains to its 2–3
  long-poles (e.g. a k256 thermostat) while the other ~9 cores idle — across 20 configs that
  idle tail wastes most of the wall time. Pooling overlaps a slow config's long-pole with
  other configs' fast jobs, so the cores stay full (≈2.2 hr → ≈40 min for the 20×18 OFAT).
  It does **not** oversubscribe: ≤`MAXJOBS` solvers run at once, each `nice -n 1` on its own
  core, so per-process CPU-time stays accurate — same per-core fairness as `run_batch`'s 12-way,
  just better packed. The shuffle spreads long-poles so the only thin tail is the last ~12 jobs.
- **Env:** `JOBS` (default `probe_odes.tsv`; point at `select.py --family … --all` for the
  full-corpus confirm), `DREAL_BINARY`, `MAXJOBS` (default **12** — the project standard),
  `TIMEOUT` (default 600; the OFAT probe uses a tighter cap, e.g. 400, to bound stress-TIM cost
  while the 123-job confirm keeps 600). Per-config flags ride a bash array-free TSV column with
  an empty-flags `NONE` sentinel (bash 3.2 on macOS has no assoc arrays, and an empty TSV field
  is collapsed by tab-IFS `read`).
- **Enablers** (in `run_batch.sh`): `DREAL_ARGS` injects per-invocation solver flags; `TIMEOUT`
  overrides the 600 s cap. Both default to the prior behavior.
- **Usage:**
  ```bash
  # OFAT probe (one knob), tighter cap:
  JOBS=benchmark/probe_odes.tsv TIMEOUT=400 bash benchmark/do_sweep.sh \
      "base=" "ord12=--ode-taylor-order 12" "bwoff=--ode-backward false"
  # Full-corpus confirm of a winner (sequential do_ab is cleaner for final numbers):
  bash benchmark/do_ab.sh /path/to/binA gcc_build/dreal4
  ```

### Lessons learned

- **Pooling > per-config batching** for a sweep with skewed per-job runtimes (above).
- **Count solvers with `pgrep -x dreal4`, not `pgrep -f gcc_build/dreal4`** — the `-f` form also
  matches the `gtime`/`nice`/`timeout` wrapper procs (≈3 per solve), so 12 real solves read as
  ~39 and look like a broken throttle. The `while (( $(jobs -r | wc -l) >= MAX ))` throttle is
  correct (same pattern as `folderops/unroll_folder.sh`); cap is **12**.
- **A timing-sensitive run needs a quiet machine** — a background CLion `-j14` auto-build (it
  rebuilds on file save) silently inflates wall time and risks false 600 s TIMs; quit it before
  the A/B / sweep. Builds and ctest (correctness, not timing) can overlap; baselines/A-Bs cannot.
- **A cleanup wiped local-only benchmark artifacts** (`baseline.csv`, `baseline_local.csv`,
  `baseline_odeexpr.csv`, `state.json`, `probe_odes.tsv` — all untracked) and `do_baseline.sh`
  dies if `state.json` is absent (`set_local_baseline.py` reads it). Reconstructible:
  `baseline.csv` ← `/tmp/good_benchmarks.csv` (same schema, documented superset); seed an empty
  `{"anomalies":[],"exceptional":[]}` `state.json`.
- **A benchmark "zero verdict flips" does NOT clear a correctness-class (soundness *or*
  completeness) change** — the sweep's lower order/hull-grid flipped no verdict on 141
  benchmarks, yet a curated unit test (`GravityInvariantTest`) caught that hull-grid 4 silently
  breaks interior-invariant refutation — a *completeness* gate (asserts φ^δ T-satisfiable on a
  T-unsatisfiable φ — missed refutation), not soundness. Curated refutation/completeness tests
  exercise sharp cases a benchmark corpus can't. And the
  reflex to "bump hull-grid back up until the test passes" is gaming the verifier — the test was
  exposing a real looseness defect. **Root cause + the deferred fix: `HULL_COMPLETENESS.md`.**

### Per-knob findings (measured on the current — still ~4× loose — tube)

These drove the adopted default (order 12 / hull-grid 4 / backward 12); numbers reflect the
SHIPPED tube, which `HULL_COMPLETENESS.md` shows is ~4× looser than CAPD's precision and will
tighten once the width-based sub-slicing fix lands (re-tune then). Soundness direction: lower
order/hull only *widens* enclosures (no false-`unsat`); the cost is missed refutation
(completeness), which the F1 test catches for the sharp interior case below the hull-4 resolution.

- **Taylor order is problem-dependent** (the strongest reason it stays a flag): tacas inverters
  want low (~8–12, up to ~2.5× faster); stiff long-horizon github wants high (~16–20). Order 16
  was the best global compromise; order 10 *regressed* github.
- **hull-grid is NOT a free speed knob** — it is the time-resolution of interior-invariant
  detection (see `HULL_COMPLETENESS.md`). Lowering it looked like a universal win on benchmarks but
  trades away refutation completeness.
- **backward-order / tolerance / c0-set**: minor (±5%). `c0-set=horect2` slightly slower
  (matches the old §Rejected). Tolerance is a *minor* lever — the earlier smoke-test "looser tol
  is 2.4× slower" was contention noise; the clean OFAT vindicates the old "tolerance ≈ no effect".
- **max-step cap**: harmful (slower + lost the stress benchmark). Keep adaptive (0).
- **backward off**: ~2× faster but loses X₀-narrowing (completeness risk) — a per-workload flag,
  never a default.

### Campaign status (2026-06-23)

- Phase 0 re-baseline ✓; Phase 1 runtime-flag plumbing ✓ + gates; Phase 2 behavior-neutral A/B ✓.
- Phase 3 sweep ✓ (OFAT 20 configs + order×hull-grid interactions + 123-confirm).
- Phase 4: default **ADOPTED** at forward & backward order 12 + hull-grid 4. The F1
  "soundness" test failure was diagnosed (not gamed) as a *completeness* gate — missed
  refutation / false-`delta-sat`, never false-`unsat` — an owner-accepted tradeoff
  (`HULL_COMPLETENESS.md`). 123-confirm: ~2× faster (PAR2 0.49), +4 solved (121/123), zero flips;
  bwd-12 adds ~5% over bwd-20. F1 test pinned to hull-16 (guards the mechanism at adequate
  resolution). Suite green (641/641 modulo the Timer flaky); Debug rounding gate ✓.
- **Follow-up RESOLVED (2026-06):** the ~4× looseness was fixed — but by a **centered-in-time
  (mean-value) range**, not the width-based sub-slicing originally proposed (the real cause was
  naive Horner in the *time* argument, not sub-interval count; see `HULL_COMPLETENESS.md`
  "Resolution"). hull-grid is no longer completeness-relevant and the F1 case now refutes at the
  hull-4 default. Same-instance trade-off: github/tacas faster (~0.19×/0.60× PAR2), saradc ~+14%
  CPU, no correctness flips. The per-knob numbers above were measured on the OLD loose tube; a
  full OFAT/123 re-tune on the tightened tube is the remaining optional step before re-picking
  any default.

## Measurement setup

- **Baseline:** `benchmark/baseline_local.csv` (~30 stratified, refreshed on this branch).
  The previously-committed local baseline (at `213394110`) was **stale** — it recorded the
  pre-FE_UPWARD-fix false-UNSAT/crash results for `k20/k28/k40 box`; the refreshed one shows
  the correct delta-SAT.
- **Probe set:** `benchmark/probe_odes.tsv` (18 ODE-heavy benchmarks; tacas inverters are the
  most CAPD-sensitive). Own frozen baseline: `benchmark/probe_baseline.csv`. Compare with
  `benchmark/run_probe.sh` → `benchmark/probe_compare.py`.
- **Profiling:** the Release binary (`gcc_build/dreal4`) keeps full function symbols (not
  stripped), so macOS `sample <pid> <secs>` gives function-level attribution with **no**
  separate RelWithDebInfo build needed. (A `prof_build/` RelWithDebInfo dir is only worth
  adding if line-level detail becomes necessary.)
- **Sentinels:** `1mhz_k20/k28/k40 …box…` (the formerly-false-UNSAT trio) are kept in the probe
  set as **soundness sentinels** — if any change re-introduces false-UNSAT they flip to UNSAT.

## Baseline profile (the map we're optimizing against)

`sample` of `tacas_c2e2_0hz_k22_…NOR__sigmoid…SAT` (~112 s baseline, IcpSeq path), 30 s window:

| Frame | % of samples | Note |
|---|---|---|
| `IcpSeq::CheckSat` | 100% | sequential ICP (this benchmark) |
| `contractor_ode_lohner::Prune` | **90.8%** | the ODE contractor is the bottleneck |
| `run_capd_fwd` | 85.4% | forward CAPD integration (BWD contractor is the small remainder) |
| `OdeSolver::encloseC0Map` | 84.5% | |
| `computeTaylorCoefficients` (order 20) | **74.2%** | scales with Taylor order |
| `autodiff::Div::eval` | ~17% | vector-field division AD (inverter sigmoid has `/`) |
| `DoubleRounding::roundUp/roundDown` (leaves) | ~9.3% | per-op FPU-mode switches (CAPD NATIVE intervals) |
| `capd::intervals::operator*` (leaves) | ~6.9% | interval multiplies |

Takeaway: CAPD forward Taylor integration dominates nonlinear-ODE runtime. Set type is already
`C0Rect2Set` (doubleton + QR reorganization).

---

## Adopted

> **Note (2026-06):** the numbers in §Adopted and §Rejected were measured against the
> *pre-per-slice* coarse-endpoint contractor and are **superseded** — they are being
> re-measured by the "2026-06 re-tuning campaign" section above. Kept as the historical
> record of what was tried and the reasoning.

### 1. CAPD Taylor order = 20 (order-10 was adopted, then reversed)

`kCapdTaylorOrder = 20`. An earlier entry lowered the order 20→10 for a 48% fast-probe win, but
that was measured against the **coarse endpoint-narrowing** ODE contractor — which was also
**unsound** (its `run_capd_fwd` intersected the terminal box with only `enclosure(t_ub)`, false-
`unsat`'ing free-time integrals whose solution lands at an interior time `< t_ub`; **proven** on
`github_oct5_0hz_k2_prostate_cancer_*`: coarse → `unsat` 0.03 s, while cav26 and the restored
per-slice form → `delta-sat` with a witness at interior times ≈1.7–4.4 « horizon 20). See
`docs/decisions.md` "Per-slice ODE tube".

Restoring cav26's per-slice tube filter (sub-grids `kHullGrid=16` enclosures **per step**) fixes
the soundness bug at a cost: the cost model goes from ∝ #steps to ∝ 16 × #steps, so a low
order's many small steps **explode** the slice count. Order-20 is cav26's co-designed partner:
fewer, larger steps + tighter per-step enclosures that also localize interior invariant
violations. The k256 thermostat went 187 s → timeout at order 10, back to 196 s at order 20.
Per-slice verdicts match cav26; on the proven case this build is faster (68 s vs 209 s, both
`delta-sat`). A lower-order enclosure is always a sound superset (never a false-`unsat`), so
order is a speed/precision lever only. The order-10-era sweep tables are superseded.

*Caveat on attribution:* the per-slice change is **orthogonal** to the
`..._inverter_sigmoid_UNS` UNSAT↔delta-sat flip — that is the #321 ibex-backward
`underflow_saturate` tradeoff (`docs/decisions.md`; do not re-investigate); do not credit/blame
the ODE contractor for it.

### 2. thread_local reuse of the parameter-bound IMap

`with_params` deep-copied the cached IMap (the full automatic-differentiation
tree) on every fwd/bwd/trace call to bind parameters without mutating the
shared cache — ~part of the ~8% allocation churn at order 10 (relatively larger
once the integration itself got cheap). Replaced the per-call deep copy with one
reusable copy per (thread, base map) in a `thread_local` cache, re-binding only
the parameters (cheap `setParameter`) each call.

Strictly safe / behavior-identical: `setParameter` fully overwrites the named
parameters, the cached base maps are immutable and live for the whole process,
and thread_local storage means no copy is shared across parallel ICP workers
(works for IcpSeq and IcpParallel). No verdict can change — it only removes an
allocation. Fast sub-probe ~3.5% faster, 0 flips; ctest green except the flaky trio.

## Rejected

### C1 / variational integration (C1Rect2Set forward)

Scoped per the "interesting new contractor algorithm" direction: use the forward
integration's variational data (the monodromy ∂x_t/∂x_0) to narrow X_0 instead of
a separate backward integration. Tested the optimistic lower bound first — switch
the forward set to `capd::C1Rect2Set` (computes the monodromy) and *discard* the
monodromy, measuring only the integration-cost penalty / enclosure-tightness
effect:

- k17 alone: 27s→20.7s (faster — misleading single sample).
- Fast sub-probe: net **1.658 (65.8% SLOWER)**. The tacas inverters speed up
  (0.28–0.29) but **prostate goes SAT→TIM (13.4s→300s)** — the variational
  integration explodes/diverges there. A severe regression that dominates.

Rejected. The probe benchmarks have n≈6 integrated state vars, so C1 integrates
a 6×6 monodromy (~an order of magnitude more interval arithmetic than C0) and is
prone to variational blow-up on stiff dynamics. This was already the *optimistic*
case (free tighter enclosure, no interval-Newton backward yet); the full
variational-backward would add cost on top of an already net-losing forward.
C1/variational is not viable for these dynamics. (No IC1OdeSolver typedef in
CAPD; C1 comes via integrating a C1Rect2Set with the C0 IOdeSolver.)

### Set representation: C0HORect2Set (Hermite-Obreshkov) at order 10

Fast-probe net 0.532 vs order-10's 0.517 — marginally *worse*, 0 flips. The
tighter HO enclosure did not reduce ICP work on the tacas inverters (identical
0.31–0.33 — they already prune fine at order 10) and added per-step corrector
cost elsewhere (prostate 8.2s→11.3s). Confirms enclosure *tightness* is not the
bottleneck at order 10; per-step Taylor cost is. C0TripletonSet (more per-step
cost, same mechanism) not tested — same prediction. Kept C0Rect2Set. The
`CapdC0Set` type alias was added to centralize this knob for the A/B test and
is retained (mirrors the order/tolerance centralization).

### Backward ODE contractor — no safe focused win (cav26 X_0 narrowing kept)

The backward contractor (a one-shot `-f(x)` image narrowing X_0, one per ODE
constraint, interleaved with a full `nl_ctcs` sweep in the plain fixpoint) is
~10% of solve time directly and ~16% including its trailing nl sweeps. Three
ways to cut it, all rejected:

- **Disable it entirely:** fast-probe 0.432 (56.8% faster), full probe 0.826,
  30-gate 0 flips / 0 regressions / 15 exceptional — clean on all 48 benchmarks.
  But this *removes* cav26's X_0-narrowing capability; 48 benchmarks from 3
  families can't represent the space, and the risk is completeness flips
  (UNSAT→SAT) on unseen instances (it can never cause false-UNSAT — removing
  pruning is always sound). User chose to preserve the capability. Not adopted.
- **Cheaper via lower backward order (kCapdBackwardOrder=6):** dead end. Net
  0.511 ≈ no speedup (the one-shot backward is dominated by fixed overhead —
  IMap copy, set construction — not order-sensitive Taylor work like the
  forward multi-step pass), AND it flips prostate SAT→UNSAT (a lower-order
  enclosure is tighter in some projection). Rejected.
- **Conditional via worklist fixpoint (`--worklist-fixpoint`):** catastrophic
  variance. k17 (SAT inverter) 23.4s→0.85s (27× faster) but k70 (UNSAT saradc)
  47s→**7876s** (167× slower, 2.2h). Off-by-default for good reason; unsafe as
  a global change. Rejected. (A custom stateful per-constraint gate is too risky
  given this variance.)

Net: backward narrowing stays on at order 10. The remaining CAPD targets are
allocation (`with_params` IMap deep-copy, ~part of ~8% malloc at order 10) and
big-effort algorithmic work (C1/variational backward narrowing reusing the
forward solution curve).

### Taylor order 8 (and below)

55.4% faster on the fast sub-probe but flips `github …prostate_h2` SAT→UNSAT
vs the order-20 baseline — a correctness flip (halt). The flip is the inherent
delta-boundary ambiguity rather than a soundness bug (the enclosure stays a
valid superset at any order), but any verdict change vs baseline is
disqualifying. The order knee is between 8 and 10. Not retested below 8.

### CAPD tolerance 1e-10 → 1e-8 (at order 10)

No effect: fast-probe net 0.513 vs order-10's 0.517 (0.4% = noise), 0 flips.
**Why (important):** the integration step size is NOT tolerance-limited for
these benchmarks. `run_capd_fwd`/`run_capd_bwd` compute `n_steps`/`max_step`
(`adaptive_n_steps`) but **never pass them to the solver** — they are dead code
(only the `if (max_step <= 0)` guard uses them; `n_steps` is live only in
`run_capd_trace` for `--visualize` slicing). CAPD integrates with its own
tolerance-based adaptive control, and for these short/smooth horizons the step
count is already near-minimal, so loosening tolerance can't reduce it further.
Both the tolerance and n_steps levers are therefore closed for fwd/bwd; the
per-step Taylor-coefficient cost (order) is the only step-cost lever. Kept tol
1e-10 (tighter = safer, no speed cost). *(Update 2026-07-21: the dead
`n_steps`/`max_step` computation described above was excised by the per-slice
tube rewrite — `max_step` survives only as the live `--ode-max-step` knob and
`n_steps` only on the `run_capd_trace` visualization path. No cleanup remains.)*

### Vector-field CSE before to_capd_string (CAPD-side, deferred)

Pre-simplifying / CSE-ing the ODE RHS with Drake's symbolic layer before emitting
`to_capd_string` could cut per-step AD cost (CAPD's `autodiff::Div` ~17% of order-10 runtime).
Medium-high effort, **low confidence**: the `a/c → a*(1/c)` rewrite is N/A (the probe families'
divisions are state-dependent — prostate `(/ z (+ z 2))`, the inverter's sigmoid terms — so the
division AD is inherent, not a constant-fold artifact), and CAPD's parser already does
within-string CSE, so a Drake-level pass may add little. The last untried CAPD-side idea; the
high-confidence config/allocation wins are harvested.

---

## odeexpr (ODE-free QF_NRA) — separate code path

**Why separate.** The high-priority `ode_expressivity` (odeexpr) family is **pure QF_NRA with
no ODEs** (transcendental-heavy: `sin`/`tanh`/`pow`/`exp`; quantifier-free; each sets
`:precision 5e-4`; Lyapunov positivity/stability/decrease obligations). `sample` confirms **0
samples** in any `capd*`/`contractor_ode*` frame — the active path is
`IcpSeq → Fixpoint[ ContractorIbexFwdbwd × N, Integer ] → BranchLargestFirst`, single-threaded,
all of polytope/local-opt/pattern-matching off by default. Every §Adopted/§Rejected idea is in
CAPD code that **never runs here** — none can help or backfire.

**A/B of the shared / default-off levers** (all 50, 600 s timeout, IcpSeq; reference = default):

| arm | solved | PAR2 vs default | verdict flips |
|---|---|---|---|
| default | 36/50 | 1.00× | — |
| `--worklist-fixpoint` | 34/50 | **5.58× worse** | none |
| `--polytope` | 16/50\* | n/a (errors) | none |

- **`--worklist-fixpoint`: net negative here too** — the same faster-on-SAT / catastrophic-on-
  UNSAT variance as the ODE-side k17/k70 (pushes `tanh_decrease__J1.0` and `kuramoto_doe__N3`
  over timeout, −2 solves). The ODE-grounds rejection holds on odeexpr.
- **`--polytope`: not usable in this build.** \*The 16 "solved" are trivial pre-contractor
  instances; the rest exit 255 with `LPSolver method called but no LPSolver has been configured`
  — IBEX was built without an LP backend (`-DLP_LIB` unset). Would need an LP-enabled IBEX first.
  > ⚠ **SUPERSEDED (2026-07-02):** the LP backend was since added — IBEX now builds `-DLP_LIB=soplex`
  > (`CMakeLists.txt:204`, commit `fa3b74bd7`), so `--polytope` no longer errors on the "no LPSolver
  > configured" path. This A/B is historical (pre-`fa3b74bd7`); the polytope path is now functional
  > and would need re-measuring on odeexpr. Left as-recorded per the snapshot convention.
- **`--local-optimization`: provably inert** (exist-forall-only; odeexpr is quantifier-free).

**The odeexpr hotspots — three mechanical overheads, all addressed.** Self-sample showed ~half
of odeexpr runtime was mechanical (mode switches + exception unwinding), not interval algebra:

1. **`fesetround` (FPU mode switch), 23–44%.** Two slices. (a) A dReal-side slice: `is_integer`
   (called per `pow` in `ExpressionEvaluator::VisitPow`) opened a `NearestRoundingScope` for
   uniformity though it is mode-**independent**, forcing a needless `FE_UPWARD↔FE_TONEAREST`
   flip per call — **removed** (`util/math.cc`), plus check-before-set in `RoundingModeGuard`
   makes redundant nested scopes free (~9% relative on `pow`-dense). (b) The gaol-internal slice
   (each interval transcendental toggles the mode around its mathlib call) — addressed in the
   **ibex-fork**, levers #9 (inline aarch64 `msr` FPCR write) + #10 (batch the two directed
   bounds into one nearest/upward window, halving toggles): **~8% aggregate**, bit-identical
   (gated by `gaol_transcendental_bitidentity_test.cc`). See `../ibex-fork/MIGRATION.md`.
2. **C++ exception unwinding, 3–37%.** IBEX `HC4Revise` signalled an emptied domain by throwing
   `EmptyBoxException`; UNSAT decrease/positivity proofs prune to empty at extreme frequency, so
   `__cxa_throw`/`_Unwind_*` was on the ICP hot path (26.6% on `tanh_decrease__J1.0`).
   **Eliminated** in the ibex-fork (patch #11): the whole shared backward engine returns a `bool`
   instead of throwing; `Function::backward`'s public signature is unchanged (dReal already reads
   `is_empty()`). `__cxa_throw` → **0%**; +2 odeexpr newly solved, 0 flips, `tanh_J1` 311→183 s
   (1.7×). Guarded by the Phase-0 soundness net (`hc4_empty_propagation_soundness_test.cc`,
   engine-level `empty01/empty02`), all written against the throw-based code first.
3. **Stat timer overhead, 4–16%.** `ContractorIbexFwdbwd::Prune` (and `…Polytope::Prune`) called
   `stat.timer_pruning_.resume()/.pause()` unconditionally, so with default log level `off`
   (`stat.enabled() = false`) two `steady_clock::now()` calls per `Prune` were computed and
   discarded. **Fixed** by gating on `stat.enabled()`. `mach_continuous_time` 11.3% → 0% on
   kuramoto__N6 (its short per-Prune work made the fixed overhead relatively large).

**Post-fix profile (the remaining floor).** With all three addressed, `fesetround` and
`__cxa_throw` read ~0%. Three benchmarks (`sample`, leaf-level):

| category | xwin1.5 (TIM) | kuramoto__N6 (TIM) | J0.6 (SAT) |
|---|---|---|---|
| gaol transcendentals (atanh/tanh/cos/sin/div_rel/sqrt_rel) | **44.8%** | **30.5%** | **45.2%** |
| HC4 backward | 11.4% | 12.0% | 12.0% |
| HC4 forward | 9.3% | 13.6% | 8.9% |
| ExpressionEvaluator (Drake VisitExpression/VisitPow) | 6.9% | 4.5% | 6.6% |
| Allocation (IntervalVector copies) | 5.4% | 5.2% | 5.2% |
| gaol interval arithmetic | 5.1% | 6.2% | 4.7% |
| fesetround / `__cxa_throw` | **~0%** | **~0%** | **~0%** |

Gaol transcendentals (30–45%) are the **computational floor** — the actual correctly-rounded
interval math, irreducible without changing the interval library's soundness semantics.
Branching (`FindMaxDiam`) is ~0.4% of leaf time — but that is the *compute cost of the
heuristic*, not its *value*: the smear A/B below shows the choice of split **variable** reshapes
the search tree enough to cut total work by **30×+ PAR2** on these families. (The earlier "a
branching-heuristic A/B is ruled out" read here was wrong — it conflated the two; do not
disqualify a heuristic by its per-call cost.)

### Smear branching — `--smear` variant sweep (Adopted: `smearsum` is the best variant; 2026-06-29)

**What.** `--smear` was a boolean wired to `SmearSumRelative`. Generalized to a required-arg
selector over IBEX's four `SmearFunction` variants (`smearsumrel`/`smearsum`/`smearmax`/
`smearmaxrel`), parameterized in one scoring loop over two axes (sum-vs-max × absolute-vs-
relative). Code: `brancher_smear.{h,cc}`, `config.{h,cc}` (`SmearVariant`), `dreal_main.cc`;
unit pins per variant in `brancher_smear_test.cc`. (`LSmear` is optimization-only — needs an
objective's LP dual — so it is N/A to feasibility queries.)

**A/B** — full `odeexpr_v1` (50) + `odeexpr_v2` (22), pooled `do_sweep.sh`, 300 s cap, CPU PAR2,
reference = smear off. **Zero SAT/UNSAT flips on any variant** (variable choice cannot move a
verdict). Results dir `benchmark/results/sweep_20260629_181925/`.

| variant | v1 (50) solved / PAR2 | v2 (22) solved / PAR2 | overall solved | overall PAR2 |
|---|---|---|---|---|
| off (default) | 39 / 1.00× | 14 / 1.00× | 53/72 | 1.00× |
| `smearsumrel` | **37 / 1.18×** | 20 / 0.25× | 57/72 | 0.62× |
| **`smearsum`** | **42 / 0.73×** | **22 / 0.02×** | **64/72** | **0.03×** |
| `smearmax` | 42 / 0.73× | 20 / 0.25× | 62/72 | 0.19× |
| `smearmaxrel` | 39 / 1.00× | 16 / 0.74× | 55/72 | 0.79× |

- **`smearsum` is a near-monotonic win on both families** — solves a strict superset of the
  default (0 solve-regressions, +11: +3 v1, +8 v2; all 22 of v2), PAR2 0.03×. Its only slowdowns
  are sub-0.2 s on already-instant SAT instances (median ratio 1.00×). Big wins concentrate on
  the v2 `aim_tanh_n2/n3 …forall` (QF) cases: many TIM / 60–116 s → <0.2 s.
- **The previously-wired `smearsumrel` is a poor default** — it *regresses* v1 (37/50 < 39, PAR2
  1.18×, **worse than off**), breaking `kuramoto__N4` (UNSAT 1.1 s → TIM) and `__N5` (88 s → TIM);
  it only looked good because earlier spot-checks were on v2. `smearsum` keeps those solves.
- **`smearmax`** ties `smearsum` on v1 but is weaker on v2; **`smearmaxrel`** barely helps.
- 8 benchmarks (kuramoto_doe N4–N6, composite_lipschitz i0/i1, decrease_exact, cs5c_sigmoid
  __decrease, kuramoto__N6) time out under *every* variant — brancher-insensitive.

**Full-corpus A/B — global default ruled OUT; smearsum is NRA-only (2026-06-29).** `smearsum`
vs smear-off over all 191 (odeexpr_v1 50 + odeexpr_v2 22 + saradc 21 + github 56 + tacas 42),
300 s cap, CPU PAR2, **0 SAT/UNSAT flips**. Results `benchmark/results/sweep_20260629_215556/`.

| family | n | base solved / PAR2 | smearsum solved / PAR2 |
|---|---|---|---|
| odeexpr_v1 | 50 | 39 / 1.00× | 42 / **0.73×** ✅ |
| odeexpr_v2 | 22 | 14 / 1.00× | 22 / **0.02×** ✅ |
| saradc | 21 | 20 / 1.00× | **1 / 14.3×** ❌ (15 OOM'd that base solved in 7–58 s) |
| github | 56 | 51 / 1.00× | 44 / **2.21×** ❌ |
| tacas | 42 | 42 / 1.00× | 41 / **2.80×** ❌ |
| **overall** | 191 | 166 / 1.00× | 150 / **2.03×** ❌ |

smearsum **collapses on the ODE families** (net −16 solves, 2.03× worse PAR2) — the dividing
line is exactly NRA-vs-ODE. *Why:* `SmearBrancher` builds its Jacobian only from the relational
constraints, **skipping ODE/`forall_t`** (the smear criterion is undefined there). On ODE
benchmarks those skipped constraints carry most of the variable coupling, so smear steers the
split-variable choice against a partial, misleading view — worse than ODE-agnostic largest-first,
badly enough to OOM saradc. On pure-NRA odeexpr the Jacobian is complete, so it excels.

**Status.** Variant machinery + best variant identified; **default stays OFF — global flip ruled
out** by the sweep above. Enable `--smear smearsum` **per-project for odeexpr_v1/v2 only**; never
on the ODE families.

### Forall-body-aware smear — `--smear` now scores ∃∀ existential vars (2026-07-03)

**What.** `SmearBrancher` previously skipped `forall` constraints entirely, so on the `odeexpr_v2`
`exists_forall` subfamily it was **inert** at the outer existential level: the existential params
are bounded only by single-var bounds (folded into the box by `FilterAssertion`), leaving zero
relational rows → `is_dummy` → largest-first. (Smear was already active in the *nested* CEGIS
counterexample search, whose instantiated body is plain NRA — but that never moved a verdict on
this family.) Now the brancher adds each finite-domain `forall`'s body leaves as Jacobian rows:
universal vars become extra columns pinned at their binder intervals (recovered like
`ContractorIbexForall`), and only existential columns are scored. Inert wherever no `Formula::Forall`
survives (odeexpr_v1, flat families byte-identical) and ODE/`forall_t` still skipped. Code:
`brancher_smear.{h,cc}`; unit pin `brancher_smear_forall_test.cc`.

**A/B — `exists_forall` (33 files, δ=0.001, 60 s CPU cap, patched binary). 0 SAT/UNSAT flips.**

| variant | solved / 33 | PAR2 (to=120) |
|---|---|---|
| off (largest-first) | 7 | 3155.0 |
| **`smearsum`** | **9** | **2955.5** |
| **`smearsumrel`** | **9** | **2955.4** |
| `smearmax` | 8 | 3048.3 |
| `smearmaxrel` | 8 | 3048.0 |

- **Inertness confirmed, then broken.** The *old* binary's `--smear smearsum` solved **7/33 —
  exactly `off`'s 7** — proving it was inert on this family (skipped `forall` → largest-first at the
  outer level). Forall-aware smearsum solves **9/33**.
- **A completeness/speed win, not an UNSAT-wall crack.** +2 delta-sat solves over largest-first
  (`n1 average_descends dh3`, `n1 both_descend dh3` — TIM → 11 s / 26 s) plus a ~2.8× on an
  already-solved `sign_agreement`. Branching cannot tighten enclosures, so it produces **0 new ∃∀
  UNSAT** (the enclosure-looseness wall in `exists_forall_perf.md` is untouched).
- **Variant ranking changed vs v1.** Here the *aggregation* axis dominates: `smearsum` ≈ `smearsumrel`
  (tied 9, PAR2 within noise) both beat `smearmax` ≈ `smearmaxrel` (8) — the lone gap is
  `both_descend dh3`, which only the sum-variants crack. Unlike v1, **`smearsumrel` does not regress
  here**, so the "prefer smearsum, avoid smearsumrel" caveat is v1-specific. `smearsum` remains a safe
  default (tied-best coverage). Thin margin — the 9-vs-8 hinges on one benchmark.

### Pre-contractor assertion simplification (zero-width sub + CSE inlining) — REJECTED (2026-07-06)

**Motivation.** `ode_expressivity_energy` PERF-002 (`docs/dreal-bugs.md` there): a pure-∀
translation-family co-descent query times out at 300 s when the CSE'd form keeps a truth-cancelling
`+t−t` textual, even with `t` boxed to `[0,0]`; the structurally-cancelled identity twin is `unsat`
in ~2.4 s. The filed diagnosis was "ibex can't handle `t−t` / dReal doesn't combine like terms." I
built a pre-contractor pass (`TheorySolver::CheckSat`, off by default via `--simplify`) that
substitutes value-determined variables out of the formulas the contractor sees: (a) point-pinned
box vars, (b) CSE aux vars `v == e` inlined with chains resolved to a fixpoint (generalizing
`seed.cc`'s `DerivedSubstitution`), plus a `RepairExplanation` step to keep learned clauses over
registered literals. **Reverted** — the whole premise was wrong. Three findings, each isolated:

1. **The filed diagnosis is refuted (don't retry "combine like terms in sums").** Drake's symbolic
   layer *already* cancels additive `x + tx − tx → x` at PARSE time (its `ExpressionAdd` is a
   `{term→coeff}` map that erases zero-coeff entries; `symbolic_expression_cell.cc`). `tx` survives in
   PERF-002 **only** because sympy-CSE fractured the cancelling pair across an aux-variable boundary
   (`cse_23 = −tx`; `cse_24 = x2_px + tx + cse_23` — `cse_23` is opaque, so `tx` can't cancel against
   it). So it's a *generator* (CSE) artifact, not a dReal addition-handling gap.

2. **Removing the fragmentation does NOT help — the real wall is the nonlinear kernel (PERF-002 ≈
   PERF-001).** Inlining the CSE chain (`cse_24 → x2_px`, letting Drake re-cancel `tx` out of the
   kernel; measured total var-occurrences **152 → 120**) still times out (>150 s). **Fully eliminating**
   every CSE aux var (dropping the `cse_k == …` defs, not just inlining uses) *also* times out. The
   identity baseline has no CSE and is ~2.4 s. So the CSE/`t` surface is only ~20% of the occurrences
   and its removal doesn't cross the tractability threshold — the residual cost is the composite
   pair-force kernel `q·(q²+c)^(−3/2)` itself (PERF-001's dependency wall). **Lesson: a fast
   structurally-cancelled twin does NOT prove the un-cancelled form's slowness is the cancellation —
   the two queries differ in irreducible size too. Isolate by actually removing the suspected cause
   (I did; it didn't help) before believing it.**

3. **A/B settled the policy question anyway (for the record, if this is ever revived for a query where
   CSE *is* the bottleneck).** Inline-which-CSE policies: **affine** (inline `v==e` where `e` is affine)
   *regressed the baseline* 2.4 s → timeout (blindly inlines a harmful multi-use affine term).
   **Occurrence-count** (inline iff it doesn't raise total var-occurrences) is safe and beat affine —
   but note strict-decrease is *too weak*: inlining a CSE that resolves to a single variable
   (`cse_24→x2_px`) is occurrence-**neutral** yet is the useful move, so the bar must be
   *non-increasing* (`≤`), not `<`. Also: zero-width substitution never fired on assertion-pinned vars
   — their bounds are `FilterAssertion`-folded into the box (removed as literals) before `CheckSat`, so
   a literal-support gate finds nothing. All verdict-preserving (COMPLETENESS-only; the pins/CSE-defs
   are equals-for-equals). Code was reverted clean; a quick odeexpr_v2 probe showed no win either.

**Takeaway for future perf work here:** treat PERF-002 as a PERF-001 instance (attack the
composite-kernel enclosure — range-aware univariate composition for `u·(u²+c)^(−3/2)`, or naming the
kernel argument as a first-class branching variable — see PERF-001 §Root cause), NOT the term/CSE
structure. Assertion-level normalization is a dead end for this family.

---

## Open avenues (deferred — post-timer-fix)

These ideas are not yet attempted. Ordered by estimated confidence × effort.

### A. ExpressionEvaluator overhead (medium confidence, medium effort)

**What:** `EvaluateBox` calls dReal's own `ExpressionEvaluator` (the Drake symbolic traversal)
once per formula after every successful `Prune`, to check delta-satisfiability. This costs
4.5–7% of total runtime (profile above). Components: `VisitExpression` dispatch, hash-table
variable-index lookups (`__hash_table::__emplace_unique`, ~1.4%), and the coefficient
accumulate loop in `VisitAddition`.

**Direction:** The hash-table lookup is a `map<Variable, int>` index lookup done per
variable per eval. A flat sorted-vector or pre-built index array could replace it.
Alternatively, if the formula set is stable across ICP iterations (it is — it's set once),
precompiling the formula evaluators into IBEX `Function` objects (which already do CSE and
compile to a flat byte stream) and reusing the HC4 forward-eval path would eliminate the
Drake traversal entirely. That is a larger restructuring (the `FormulaEvaluator` and
`ExpressionEvaluator` classes are the eval layer).

**Caution:** `EvaluateBox` also determines which formulas are violated (the `DynamicBitset`
returned) — branching uses this. Any replacement must preserve that output.

### B. Allocation reduction — IntervalVector copies in HC4 (medium confidence, medium effort)

**What:** `IntervalVector::IntervalVector` (copy constructor) and `_xzm_free` collectively ~5%
of runtime. The copy constructor appears in the HC4 forward pass (allocating the input vector
for each `Eval::eval` call) and in the backward pass's local snapshots. Post-Phase-2,
exception-elimination removed the `try/catch` frame but may not have changed heap turnover in
the backward engine's local allocations.

**Direction:** Instrument IBEX's `eval` and backward paths with Instruments → Allocations
(or `malloc_count`) to confirm the allocation sites and count. If `IntervalVector` copies are
O(constraints × ICP-iterations), a preallocated workspace (thread-local or per-contractor)
that is resized-once and reused across calls could eliminate the per-call allocation.

**Note:** This is in IBEX's core (`ibex::Eval`, `CompiledFunction`), so the change would live
in the ibex-fork, following the same pattern as the exception-elimination patches.

### C. Per-constraint skip-if-unchanged gate (low confidence, medium effort)

**What:** IBEX's `ContractorFixpoint` iterates all N constraints until no box shrinks. For
odeexpr's Lyapunov formulas, many constraints involve non-overlapping variable clusters; a
narrowing in constraint `i` rarely propagates to constraint `j` unless they share a variable.
A dependency graph that tracks which variables each constraint reads/writes could gate
re-evaluation of constraint `j` until one of its input variables changes.

**Why low confidence:** The `--worklist-fixpoint` flag (a coarser version of this idea)
was measured net-negative on both ODE and odeexpr families — faster on SAT-easy instances
but catastrophically slow on UNSAT-difficult ones (k17 23 s → 0.85 s; k70 47 s → 7876 s).
A per-constraint graph rather than a global queue reorder might have better variance, but
that distinction is unproven. Profile above shows HC4 forward (13.6%) + backward (12%) = 25%
— so if this idea works, there is meaningful headroom. Worth a targeted A/B only if the
worklist-fixpoint catastrophe can be traced to the global reorder rather than the early-exit
logic.

### D. Constraint ordering heuristic (low confidence, low effort)

**What:** IBEX evaluates constraints in declaration order (as they appear in the parsed
`.smt2`). A heuristic that front-loads high-shrinkage constraints might cut fixpoint
iterations. The profile shows `HC4Revise::proj` (the per-constraint iteration entry point)
at ~1.6% leaf — the overhead of cycling through low-yield constraints is embedded in
`CompiledFunction::forward/backward`. Reordering is a one-time setup cost.

**How to A/B:** Read the current constraint order from `ContractorFixpoint`'s contractor list
at solve start, sort by some heuristic (e.g. number of variables, or by profiling iteration
zero's shrinkage), then run. This is dReal-side and does not require ibex-fork changes.

### E. Gaol Lever 3 — 1 toggle per transcendental (NO-GO for now)

After Levers 1+2 (inline FPCR write + batched dn_up pairs → 2 toggles per transcendental), the
remaining gaol `fesetround` cost is the essential **2-per-transcendental** directed-rounding
round-trip, and it cannot be cut bit-identically:

- The toggle is **structural.** Correctly-rounded transcendentals use double-double internal
  arithmetic whose error analysis is valid **only in round-to-nearest**; gaol's ambient is
  FE_UPWARD. So every interval transcendental round-trips upward→nearest→upward = 2 `msr fpcr`
  writes. Verified dead ends: patching mathlib/libultim to not require nearest (round-to-nearest
  is a documented correctness precondition, not a flag — `Init_Lib()` sets `FE_DFL_ENV`);
  switching to crlibm (its directed functions are *also* nearest-wrapped); the logged
  `lb = -round_up(-f(x))` "Lever 3" (a misconception — that identity flips upward⟷downward for
  *arithmetic*; `f` is still the mathlib routine needing nearest).
- The only true eliminations are architectural and **not bit-identical** (verdict-shift risk at
  the delta boundary, non-upstreamable): invert the ambient (keep FPU nearest, do interval
  arithmetic with `next_float`/`previous_float` ULP bumps) or write custom upward-mode directed
  transcendentals.

**Measured ceiling: ≈13–16% on transcendental-dense benchmarks, ~6–10% aggregate** over the 50
odeexpr (in-solver redundant-`msr` A/B: inject K extra round-trips between the two mathlib calls
— net mode unchanged, identical search trajectory — and measure the time delta; +11–13% per
added pair on the clean long-runners, a mild under-estimate). That is right at the bar for a
non-bit-identical change for a permanent fork liability — **NO-GO**. The unharvested
bit-identical avenues A–D (ExpressionEvaluator ~5–7%, allocation ~5%) are the better next step;
revisit msr-elimination only if A–D are exhausted and the densest instances remain msr-bound.

## 2026-07-22 RELATED-WORK candidate campaign (IN PROGRESS)

Implements the five `ibex_docs/RELATED-WORK.md` top candidates as opt-in flags —
`--newton` (CtcNewton square-equality cell), `--branch abs|absdiam` (ABS activity
brancher), `--obbt` (certified 2n-LP bound tightening), `--polytope-linearizer
xtaylor|affine|both` (vendored affine plugin; gated on the fAF2 rounding audit,
`ibex_docs/affine-rounding-audit.md`), `--mohc` (CtcMohc port, timeboxed) — plus ICP
parity R1/R2/R3 (`icp_parity_gaps.md`) and test-coverage hardening (DRPM, ODE/ACID
knobs, determinism). One binary, then one pooled 11-arm × 270-job all-family sweep
(arms incl. `par1` = jobs-1-through-IcpParallel for the R3/G4 overhead gate and
`drpm` = `--drpm-max-size 4`). Plan: `~/.claude/plans/take-a-look-at-rustling-crayon.md`.

**Baseline (Phase 0):** dreal4-cmake `f51f78b8e` + ibex-fork `e054af7b`, `gcc_build`
incremental build clean, full ctest green exit 0, **846 tests** (known-intentional
skips only). Local benchmark baseline: `8b0299ce9` re-freeze (181 rows).
