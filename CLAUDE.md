# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Docs index

- `docs/architecture.md` — layers, ICP loop, Box, explanations, ODE/PM pointers
- `docs/contractors.md` — contractor types, composition, caching
- `docs/writing-fast-dreal-formulas.md` — **standalone guide for external SMT-LIB2 generators**: high-level ICP + how to structure formulas for speed (variable-occurrence/dependency-problem master rule, factored≫expanded measured, bound-every-real, `let`/aux tradeoff, ODE & ∀ tips, `--polytope`/`--acid` escape hatches). Cites source, not the stale `ibex_docs` audit
- `docs/decisions.md` — topic-keyed ADRs (ODE backend, per-slice tube, feed faithfulness, underflow, backward narrowing)
- `docs/ode-integration.md` — CAPD ODE contractor mechanism, soundness invariants, input formats
- `docs/pattern-matching.md` — DeBruijn canonicalization, substitution_tree, symmetry filtering (CAV26)
- `docs/rounding.md` — FPU rounding regimes, phase-hoisting, typed doubles, source-hygiene lint rules
- `docs/benchmarking.md` — benchmark infrastructure, families, A/B, sweep, cross-solver comparison
- `docs/soundness-vs-completeness.md` — T-relation definitions, dReal guarantees, worked F1 example
- `docs/forall-semantics.md` — ∃∀ fragment: syntax, CE-guided contractor, nested-forall crash, QE limits, nested-quantifier project guide
- `exists_forall_perf.md` (repo-root **worklog**, not docs) — ∃∀ machinery notes: the durable solver findings (`--forall-pre-prune` is sound but its speedup is **encoding-fragile** and doesn't address the existential-isolation wall; `--forall-polytope` needs SoPlex and can *hurt*; CE-domain fix; lemma-PM is ground-only) plus a **correction header** — the odeexpr_v2 family was regenerated 2026-07-01 — a second same-day regen now (25 `forall/` + 25 `exists_forall/`, δ pinned **0.01**), so the older δ=0.0005 / "SAT-by-construction" body is superseded: only `sign_agreement` admits the zero witness, while the strict-margin `average_descends`/`both_descend` files are genuine UNSAT/separation targets; the symbolic-rewrite avenue (SymPy-verified `sig2tanh`/`factor`/`simplify` of the descent body) was tested & **rejected 2026-07-02** — UNSAT-null, `factor`/`simplify` only an N1-delta-sat artifact (net-negative + `simplify` hangs on N2), so don't re-try it (§2026-07-02)
- `odeexpr_v2_forall_perf.md` (repo-root **worklog**, not docs) — odeexpr_v2 `forall/` n2 intractability diagnosis: the affine-transformed descent bodies contract so poorly under ICP that the tractable-dimension threshold collapses (~6 free vars vs the 11–18 carried); singularity ruled out, no NRA flag helps; COMPLETENESS (missed refutation within budget on a true-`unsat` goal — never a false `unsat`)
- `upstream_gap_audit.md` (repo-root **worklog**, not docs) — 2026-07-21 audit of soonhokong/dreal4's unmerged fixes + all open dreal/dreal4 issues vs. this fork, **discharged 2026-07-22** (correction header cites the fix shas): both port candidates landed; #258 arctan2 false unsat (SOUNDNESS) fixed fork-side, #264/#284 false unsat + #280 missed refutation (COMPLETENESS) fixed here, #68/#265 now warn loudly. Remaining live set: crashes #176/#86/#80, #223, and the #265 unbounded-interval design limit (COMPLETENESS — missed refutation)
- `icp_parity_gaps.md` (repo-root **worklog**, not docs) — 2026-07-22 audit of residual IcpSeq↔IcpParallel divergences after the T5 parity campaign (none soundness). **Status 2026-07-24: R4 EXECUTED** — G1–G3 closed (`0c5e9a753`/`e8cdb7525`; canonical seed input = un-pruned root snapshot — the pruned-box variant hung nested-CE COBYLA), G4 measured (`par1` arm: zero verdict diffs, PAR2 1.001× — gate PASS), then `icp_seq.{h,cc}` + `--icp-force-parallel` deleted, one loop at every `--jobs`, nested forall-CE through it (G6 CDS nesting verified safe — libcds attach is refcounted), G5 died with the seq static. R5 families-level jobs=4-vs-1 diff pending
- `docs/seeding.md` — seed-and-verify (`--seed-samples`): the `AllRelational` gate, the CSE/COBYLA/NNF/outward-box pipeline, COMPLETENESS-only framing
- `docs/constraint-order-explanation-soundness.md` — **FIXED SOUNDNESS bug** (2026-06-29): a stiff ODE `integral` logically responsible for a conflict was dropped from `used_constraints_` (CAPD diverges → `contractor_ode_lohner::Prune` returned at its inconclusive exit without recording), so the learned theory clause was built from the satisfiable relational remainder → false `unsat` (triggered by `--constraint-order desc`). Fix: the inconclusive exits call `ContractorStatus::AddInconclusiveOde`, and `GenerateExplanation` splices the ODE in as a non-expanding leaf keyed on the emptying `unsat_witness` (minimal-relevant; witness not the broad closure, which regressed a c2e2 SAT instance to TIM). Validated by no-flip + auditor (2819 lemmas, 0 invalid) + corpus A/B (1.01× PAR2). Adversarial tests: `test/dreal/solver/test/constraint_order_soundness_test.cc`
- `docs/papers/` — foundational Gao et al. literature: companion summaries (δ-decidability/δ-complete/dReal-tool/∃∀) cross-referenced into the docs above, with paper↔code drift flagged (e.g. opensmt+realpaver → CaDiCaL+IBEX+CAPD)

---

## Project Overview

dReal4 is a delta-complete SMT solver for nonlinear arithmetic over the reals. It takes SMT-LIB2
(`.smt2`) or Delta-Real (`.dr`) formatted formulas and checks satisfiability up to a precision
parameter delta. The `cav26` branch holds pattern-matching/lemma-reuse research; `upgrade-ibex`
(current) source-builds IBEX from the `ncsys-lab/ibex-lib@dreal-perf-patches` fork and uses CAPD
as the sole ODE backend.

---

## Soundness vs. completeness (read before reporting either)

dReal is **sound but δ-complete**; conflating them has cost real experiments here (the hull-grid
"F1" finding was filed as soundness when it was completeness, and got runs cancelled).

- **false `unsat`** (returns `unsat` on a δ-satisfiable φ) = **SOUNDNESS** violation — forbidden.
  Soundness breaks only where a box is narrowed past a true model (wrong FPU rounding, truncated
  ODE feed, strict-bound `nextafter`, scrambled model, underflow).
- **missed refutation / false `delta-sat`** = **COMPLETENESS** violation. Looser enclosures
  weaken this, never soundness.

**Mandate:** whenever you report, label, comment, or commit a soundness/completeness issue —
in chat, code, docs, or commit messages — append the model-theory characterization in parentheses.
Templates: `SOUNDNESS (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat)`;
`COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed refutation)`.
Canonical reference: `docs/soundness-vs-completeness.md`. Model-theory depth: the
`smt-model-theory` skill.

---

## Build

End-user build guide for cloning the repo (prerequisites, native macOS/Linux, Docker):
`README.md`. Build-system configuration for modifying the build (`--version` wiring, CMake
git-version target, IBEX/CAPD source-build): `docs/build.md`.

`./FULL_BUILD.sh` (first build — creates `gcc_build/`) and `./BUILD.sh` (incremental) build
target `dreal4` with `-j8`; binary at `gcc_build/dreal4`. Override IBEX source via
`-DIBEX_GIT_REPOSITORY=file:///path/to/ibex-fork` for local-dev against an unpushed checkout
(`CMakeLists.txt` pins the fork sha `6b1b2c10`).

**Docker** (Linux hermetic verification): after `docker build -f Dockerfile.dreal_ubuntu -t
dreal-linux-verify .`, run `cat query.smt2 | docker run --rm -i dreal-linux-verify ./dreal4 --in
--model`. Bad-fd `-j` caveat: `README.md`.

---

## Running Tests

```bash
cd gcc_build
cmake --build . --target dreal4_cmake_test -j8
ctest                                      # run all tests
./dreal4_cmake_test --gtest_filter="*Box*" # run a single test by name pattern
```

Test sources live under `test/dreal/` mirroring `src/dreal/` structure.

**New test file footgun:** test sources are globbed at configure time (`file(GLOB_RECURSE)`,
no `CONFIGURE_DEPENDS`). A new `.cc` is only picked up after a CMake reconfigure (`cmake gcc_build`
or `FULL_BUILD.sh`). `BUILD.sh` and bare `cmake --build` silently skip new files — the suite
count going up is the tell.

**Rounding-mode gate:** `./rounding_debug_gate.sh` builds the Debug target (`cmake-build-debug`)
and runs the suite; `DREAL_ASSERT_ROUNDING` only fires in Debug. Run it before merges, alongside
`./copy_lint.sh` (incremental clang-tidy copy + UB/perf gate).

**Known flaky tests:** none — a clean run passes everything (the ITE counter
order-dependence and the Timer.Test1 timing jitter were fixed 2026-07).

---

## Running the Solver

```bash
./gcc_build/dreal4 --precision 0.001 --produce-models input.smt2
./gcc_build/dreal4 --in --model          # read from stdin
```

Key flags: `--precision <delta>`, `--produce-models`, `--logic <QF_NRA|QF_NRA_ODE>`, `--verbose`.

**Seed-and-verify (`--seed-samples N`, default 64 = ON):** speculative pre-pass for off-center NRA
SAT instances — propose candidate points (multi-start COBYLA) and verify a small box around each via
the existing prune/`EvaluateBox` (soundness/completeness free; gated off for ODE/forall so it never
fires there). `--seed-samples` is also the switch: `0` disables. This replaced the removed 0.56
split-ratio magic + `--explore-order` alternation (split ratio is hardcoded back to 0.5, no flag).
Mechanism + flags: `docs/seeding.md`. Rationale + A/B: `docs/decisions.md` §"Seed-and-verify".
Code: `src/dreal/solver/seed/seed.{h,cc}`.

**Smear branching (`--smear <variant>`, default off):** constraint-aware split-variable choice
(Jacobian-weighted) replacing largest-first; one of IBEX's four `SmearFunction` variants —
`smearsumrel`, `smearsum`, `smearmax`, `smearmaxrel` (required arg; bare `--smear` errors).
Soundness-free (variable choice never moves a verdict). Works at every `--jobs`
(`--jobs>1` builds one `SmearBrancher` per worker — the brancher is not thread-safe to share). On
the odeexpr families **`smearsum` is the strongest** (64/72 solved vs 53 off, PAR2 0.03×, 0 flips)
and `smearsumrel` actually regresses v1 — so prefer `--smear smearsum` there. **Forall-body-aware
(2026-07-03):** the Jacobian now includes each `forall` body's existential columns (universal vars
pinned at their binder intervals), so smear is constraint-aware for the ∃∀ `exists_forall`
subfamily's *outer* existential branching (previously inert there → largest-first). Completeness/speed
only — +2 delta-sat solves, `smearsum`≈`smearsumrel` best (here `smearsumrel` does **not** regress),
but branching can't crack the ∃∀ UNSAT enclosure wall. **NRA-only: the full-corpus A/B ruled out a
global default** — smearsum *collapses* the ODE families (saradc 20→1 solved with OOMs; overall
2.03× worse) because the smear Jacobian still skips ODE/`forall_t` (`Kind::ODE_LOHNER`) constraints
(distinct from the ∃∀ `forall`, which it now handles — grep `forall-vs-forall_t`). Enable
per-project for odeexpr only. Mechanism:
`docs/architecture.md` §Branching. A/B: `OPTIMIZATION_LOG.md` §odeexpr. Code:
`src/dreal/solver/brancher_smear.{h,cc}`.

**Forall pre-pruner (`--forall-pre-prune`, default off):** a sound, COMPLETENESS-only IBEX
`ibex::CtcForAll` proj-intersection pre-pruner (`ContractorIbexForall`) that runs *beside* —
never instead of — the δ-complete CEGIS `ContractorForall` in the forall fixpoint, shrinking
the existential box by pure interval contraction (no nested δ-solve). NRA ∃∀ only; works under
`--jobs>1` via a per-worker `ContractorIbexForallMt` cell (one `ibex::CtcForAll` per thread,
keyed on `ThreadPool::get_thread_id()`; mirrors `ContractorIbexFwdbwdMt`). `--forall-pre-prune-prec` (default 0.5) is the
universal-box bisection precision — near-irrelevant on odeexpr_v2 and capped at the
universal-box width above which `CtcForAll` degenerates to a single midpoint check; for an
*unsat* goal it inverts (finer ⇒ stronger refutation). **Encoding-fragile speedup that never
moves the pinned δ=0.0005** — measured record, the SAT-by-construction finding, and the
combine-with-polytope-hurts result: `exists_forall_perf.md`. Mechanism + IBEX-lever
audit: `ibex_docs/AUDIT-QUANTIFIERS.md` Q1. Code:
`src/dreal/contractor/contractor_ibex_forall.{h,cc}`.

**RELATED-WORK candidates (2026-07, all default off):** the `ibex_docs/RELATED-WORK.md` top
five shipped as opt-in flags and were measured (11-arm × 270-job sweep + 4-arm ODE-family
re-sweep — `OPTIMIZATION_LOG.md` §"RELATED-WORK candidate campaign";
`benchmark/results/sweep_20260723_115956/ANALYSIS.md`). Family-conditional, no default
changes: **`--mohc`** (CtcMohc monotonicity cell) is the only base-beater on the odeexpr
families (115/151, PAR2 0.968×) and **`--polytope --polytope-linearizer affine`** (vendored
affine plugin) wins odeexpr_v1 (0.804×) — enable per-project for odeexpr only (the `--smear
smearsum` precedent); both are strongly net-negative on the ODE families (keep base flags
there). `--drpm-max-size 4` shows a real github lemma-PM signal (PAR2 0.688×, up to 28×
single-file) but has one undiagnosed tacas 9 s→TIM pathology — diagnose before recommending.
**Measured dead, kept for record:** `--newton`/`--newton-ceil` (zero unique solves, 11
OOMs), `--branch abs|absdiam`/`--branch-decay` (ABS degeneracy confirmed — both variants
lose everywhere), `--obbt` (≈polytope). **BUG-014** (`docs/dreal-bugs.md`, fixed 2026-10-05):
X-Taylor rows carry denormal coefficients that SoPlex 4.0.2's presolve cannot take, which led
to an OOB write; the fork now runs SoPlex with presolve off. **BUG-019** (fixed the same day):
the Neumaier–Shcherbina certificates fail closed on non-finite data and compute Aᵀy
rigorously, and non-finite LP rows are refused or (X-Taylor) skipped. The system cells skip `forall_t`/`integral` atoms
(`FilterIbexConvertible`, COMPLETENESS-only, `f6d735254`). Headline: the system-cell arms
scored the **first machine-verified UNSAT on the odeexpr_v2 `forall/` wall** (aed75881 —
a COMPLETENESS win for those arms; base's delta-sat is the legal δ-artifact).
ICP consolidation (R4, 2026-07-24): **one ICP loop** — `IcpSeq` and the `--icp-force-parallel`
scaffold are deleted; `IcpParallel` serves every `--jobs` (1 = zero pool workers,
main-thread-only, deterministic; R3 gate: zero verdict flips, PAR2 1.001×;
`icp_parity_gaps.md`).

**CAPD ODE tuning:** `--ode-taylor-order` (default 12), `--ode-hull-grid` (4 — per-step sub-slice
count; lower widens enclosures (never a false-`unsat`). Since the 2026-06 centered-in-time tube
fix (`HULL_COMPLETENESS.md`) the per-slice range is mean-value-in-time, so for a thin initial
set the default tube sits near CAPD precision; on an interval initial set a monotone-in-time hull
keeps monotone components tight (BUG-013), and a finer hull-grid can still refute more on the
rest. The mean-value half is sound only with CAPD ≥ `2a2263c7` — the older pin
gave false `unsat` on interval initial conditions (BUG-018)),
`--ode-backward` (true), `--ode-abs-tol`/`--ode-rel-tol` (1e-10), `--ode-max-step` (0=adaptive).
`--refine-witness` (default off): δ-**tight** `--model` witnesses. By default `--model` reports
the **raw terminating box** with zero post-processing — the exact region ICP certified,
idempotent when re-fed as bounds (the old always-on midpoint±δ/2 `Tighten` slice destroyed the
certified-region information and, for un-refined ODE dims, fabricated witnesses the solver
itself refutes — BUG-011); a don't-care Boolean prints as `[false, true]` in the box,
`(get-model)`, and `(get-value)` renderings. The flag pins don't-care Booleans to true, shrinks
continuous/integer dims to midpoint±δ/2 at report time (sound by inclusion monotonicity;
ODE-atom dims exempt), and δ-refines every ODE dim during search — measured 4.89× github PAR2 (18 SAT→TIM) corpus-wide, so
it's opt-in for precision-critical witness-reading queries. `docs/decisions.md` §"ODE formula
evaluator".
Full flag list + performance rationale: `docs/ode-integration.md` §Performance. 2026-06 retuning
campaign: `OPTIMIZATION_LOG.md`.

---

## Architecture

See `docs/architecture.md` (full pipeline, ICP loop, Box, explanations), `docs/contractors.md`
(types and composition), `docs/ode-integration.md` (ODE), `docs/pattern-matching.md` (CAV26 PM).

**Vendored third-party** (`src/third_party/`): Drake symbolic, libcds, threadpool,
dynamic_bitset, PicoSAT (legacy, unused). Do not modify without cause.

**Auto-downloaded:** IBEX (`ncsys-lab/ibex-lib@dreal-perf-patches`, sha `6b1b2c10`), CAPD
(`03dc5628`, `CAPD_INTERVAL_TYPE=NATIVE`), fmt, spdlog, nlopt, GTest. See `DEPENDENCIES.md` for
build wiring; `../ibex-fork/MIGRATION.md` for the ibex-fork patch catalog.

---

## Branch map

| branch | purpose |
|---|---|
| `main` | stable CMake base; CaDiCaL, IBEX 2.8.9, core perf fixes |
| `fmcad25-experiments` | first PM research; NN heuristic; SAR-ADC application |
| `tacas26-odes` | ODE AST nodes (`Integral`, `ForallT`); CAPD contractor; dReal3 `.dr` compat |
| `upgrade-ibex` **(current)** | post-Codac; IBEX fork (2.9.1; see `../ibex-fork/MIGRATION.md`); per-slice ODE tube |
| `cav26` | DeBruijn PM; `substitution_tree`; symmetry filtering |

---

## Benchmarking

Run `/benchmark` after every meaningful code change. Run proactively at natural breakpoints.

- `/benchmark` — ~8-12 benchmarks, Haiku subagent interprets, 2-4 sentence summary
- `/benchmark-baseline` — full baseline (all odeexpr_v1 + odeexpr_v2 + ~10 each flat family)

**Thresholds:** PAR2 >1.5× baseline = regression; <0.6× = exceptional; SAT↔UNSAT flip = immediate
escalation. CPU time (user+sys), not wall clock.

**Benchmark sources** (two content-addressed `odeexpr_v*` manifest families + three flat dirs):
- `~/Documents/expressivity/v1/benchmarks/` — `odeexpr_v1` family (NRA-only,
  manifest `revisions[].file`)
- `~/Documents/expressivity/v2/benchmarks/` — `odeexpr_v2` family
  (**newest high-priority target**; ∀/∃∀ MLP-expressivity queries in `forall/` + `exists_forall/`,
  manifest `revisions[].smt2`)
- `~/Documents/new_dreal/nraode_to_nra/drealgithub_sunoct5/rolled/` — github_oct5_
- `~/Documents/new_dreal/nraode_to_nra/VNAMSCwI_satoct11/rolled/` — tacas_c2e2_
- `~/Documents/new_dreal/AMS-verification-bundle-of-sticks/saradc/rolled/` — 1mhz_

Full infrastructure (run_batch.sh, select.py, do_ab.sh, do_sweep.sh, families/weighting,
cross-solver comparison): `docs/benchmarking.md`.

---

## Verification discipline

When reporting "tests pass" or "build green," cite the artifact: build dir, commit, or container
image. The trap: ctest reports a pass against `build-old-pin/` (a proven baseline) while the new
build dir is never exercised.

---

## Key Design Notes

**Known fallbacks** (grep `FALLBACK(approved)`): the ODE inconclusive skip — a CAPD failure,
a value CAPD cannot represent, or a negative time window makes that Prune narrow nothing; the
integral is recorded (explanations, and a stderr warning at a delta-sat verdict); the
`--visualize` trace keeps the points before a CAPD failure. COMPLETENESS only.
`docs/decisions.md` §"ODE inconclusive skip".

**SMT-LIB push/pop is formally unsupported.** `(push N)`/`(pop N)` and `Context::Push/Pop`
throw a documented rejection before any state mutation (CaDiCaL can't retract clauses;
learned theory lemmas are box-relative). Incrementality is encoder-side: one self-contained
script/Context per query; monotonic multi-check-sat still works. Rationale + re-attempt
notes: `docs/decisions.md` §"SMT-LIB push/pop: formally unsupported".

**Box name-lookups fail loudly.** `Box::operator[](Variable)`, `index()`, and `variable(int)`
throw on a miss — they used to silently default-insert into the index map shared by every
copied box (bisect children included) and read/WRITE dimension 0. That silent path hid a real
corruption on the ∃∀ path (`ForallFormulaEvaluator` copied foreign outer-box variables into
its nested CE context). Full record: `simulink-to-dreal_bug_reports.md` BUG-012.

**dReal3 backward compatibility is intentional.** The DR parser (`src/dreal/dr/`) handles the
older dReal3 ODE syntax. Don't break this.

**`auditor.cc`** (`src/dreal/solver/auditor.cc`) reprints learned lemmas in dReal3-compatible
format for independent re-checking. Not part of the core solving loop.

**FPU rounding mode:** Read `docs/rounding.md` before touching any interval/ODE/printing code.
The failure mode is a **silent false `unsat`** — wrong ambient mode inverts gaol's directed
rounding with no warning, invisible on exactly-representable constants. Two regimes:
`FE_UPWARD` (gaol/interval → `UpwardRoundingScope`) and `FE_TONEAREST` (CAPD/formatting →
`NearestRoundingScope`). Mode established once per ICP phase, not per contractor call. Two
sanctioned clobberers (`ExpectClobber` tag): CAPD adapters and ibex's interval `operator<<`.

**Source-hygiene lint:** `python3 lint.py` (regex routing) + `./rounding_debug_gate.sh` (lint +
Debug ctest) + `./copy_lint.sh` (clang-tidy copy/UB/perf gate). Key forbidden patterns in
`src/dreal/`: raw `.mid()`/`.diam()` (→ `safe_mid`/`safe_diam`), raw
`ibex::Function::backward` (→ `ibex_hc4_backward`), raw `std::to_string` feeding CAPD (→
`to_capd_string`), interval from scalar `+`/`-` (→ `make_sound_interval`), `arr[i++]`/`arr[++i]`
subscript (BUG-005 scrambled-model class). Full rules: `docs/rounding.md`.

**`filter_assertion` soundness:** strict bound handling had a wrong `nextafter()` call — fixed.
`substitutions_map` forward/backward naming had a soundness bug — fixed. Take care around strict
vs. non-strict inequalities in contractors and the SAT interval logic.

**`forall`-binder shadow guard (QUIRK-001):** a `forall`-bound var whose name collides with a
top-level declared (model) var was silently mis-solved (the unconstrained outer var → spurious
`delta-sat`). The SMT2 driver now throws via `RegisterQuantifiedVariable` on such a collision.
Details: `docs/forall-semantics.md` §4.8.

**ODE feed faithfulness** (`to_capd_string` precision): constants render at 17 sig figs;
`std::to_string`'s 6-digit truncation was a false-`unsat` soundness bug. Details:
`docs/decisions.md` "ODE feed faithfulness" and `docs/ode-integration.md` §Soundness.

**Denormal/underflow soundness** (dreal/dreal4#321): sound in two layers (ibex HC4-backward
`underflow_saturate` + Drake `sound_constant_fold`). Full record + accepted delta-completeness
tradeoff: `docs/decisions.md` "Denormal / underflow soundness".

**ODE performance baseline:** CAPD order-20 was at or below Codac CtcLohner on all tested ODE
benchmarks. The `--capd-t-gate`/`--capd-ndim-gate` flags have been removed (Codac hybrid retired).
Details: `docs/decisions.md` "ODE backend".

**`forall` vs `forall_t` are independent machinery (`forall-vs-forall_t`).** `forall` = the
∃∀ NRA quantifier (`Formula::Forall` → `ContractorForall`, `Kind::FORALL`, CE-guided;
`docs/forall-semantics.md`). `forall_t` = the ODE trajectory invariant (`FormulaKind::ForallT`,
checked per-slice in `contractor_ode_lohner` / `Kind::ODE_LOHNER`;
`docs/qf_nra_ode_semantics.md` §5). Same prefix, unrelated code paths — never swap them. The
docs were confused here once (a mislabeled contractor); grep `forall-vs-forall_t` for the
anchored warnings, and `docs/forall-semantics.md` §7 for the canonical side-by-side.

**Negated/unlinked ODE constraints (BUG-002):** a negated `integral`/`forall_t` literal is
dropped in `link_integral_invariants`. That can't be a throw there (it runs inside DPLL(T) on
transient search literals; throwing crashes valid BMC benchmarks), so since 2026-10-05 the
user formula is checked instead: an ODE atom in negative or mixed polarity, or inside an NRA
`forall` body, is **rejected** at assert time (`RejectNonPositiveOde`, `context_impl.cc`),
which makes the in-loop drop exact. An **unlinked positive `forall_t`** (invariant
must reference the endpoint var `x_t`, not the flow var `x`) is now **rejected loudly** at
check-sat time, where the full assertion stack is in scope (`RejectUnlinkedForallT`,
`context_impl.cc`; shared link predicate `forallt_links_to_integral`, `contractor_odes.h`).
Semantics that could replace the rejection remain aspirational `GTEST_SKIP` tests in
`test/dreal/smt2/test/dreal_future.cc`. Details: `docs/decisions.md` "Negated / unlinked ODE
constraints", `docs/ode-integration.md`.

**Benchmarking instrumentation:** `std::cerr` prints and JSON dumps exist for benchmarking runs.
`--verbose` (DEBUG) is useful for development; TRACE is deep debugging only.
