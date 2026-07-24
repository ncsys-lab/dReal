# dReal solver issues

Confirmed and suspected bugs, unintuitive behaviors, feature requests, and performance
problems in this project's pinned dReal binary, with minimal reproducers. Files are
committed under `docs/dreal-bugs/`. List entries with:
`grep "^## BUG-\|^## QUIRK-\|^## FEAT-\|^## PERF-" docs/dreal-bugs.md`

Numbering continues the project-wide sequence from `simulink-to-dreal_bug_reports.md`
(BUG-001 … BUG-012 live there; its `docs/dreal-bugs/bug*.smt2` reproducer convention is
shared). New entries land here.

## BUG-013 — `--ode-taylor-order` 16/20 and `--ode-abs-tol`/`--ode-rel-tol` ≥ 1e-6 make the FWD ODE prune silently inert on interval-IC instances (CAPD divergence → inconclusive skip; COMPLETENESS)

**Symptom / Description**

On an interval-IC decay tube instance (dx/dt = −x, x0 ∈ [1,2], t ∈ [0,1]) CAPD reports
divergence (step-control failure) when the Taylor order is raised to 16 or 20, or when either
integrator tolerance is loosened to 1e-6 or 1e-2 (defaults: order 12, tols 1e-10). The
contractor then takes the `!res.found` inconclusive exit (`contractor_odes.cc:366`,
`AddInconclusiveOde`) and narrows nothing: X_t stays bit-exactly at the untouched gate
(`[0.3, 0.8]`, lb 0.3) instead of the lb lifting to the tube minimum e^-1 ≈ 0.368.
Deterministic ×3 at both the unit level (single FWD `Prune`) and the CLI.

Two consequences:

- **COMPLETENESS-shaped** (missed refutation possible — asserts φ^δ T-satisfiable on a
  possibly T-unsatisfiable φ; the inconclusive skip only leaves boxes wide, never a false
  `unsat`). Verified end-to-end below: a refutation the tube makes at the default knobs is
  lost at the trigger knobs.
- **Knob-usability gap:** higher order / looser tolerance values are accepted silently and
  are simply inert on interval-IC instances — nothing warns that the flag made the solver
  strictly weaker.

**Reproducer(s)**

`docs/dreal-bugs/bug013_ode_order16_inert.smt2` — the knob-test geometry with the gate
tightened to `x_t ≤ 0.35`, disjoint from the tube minimum e^-1 ≈ 0.3679 (gap ≈ 0.018 ≫
δ = 0.001), so the FWD tube refutes at the default knobs:

```smt2
(set-logic QF_NRA_ODE)
(declare-fun x () Real [-100.000000, 100.000000])
(declare-fun x_0 () Real [1.000000, 2.000000])
(declare-fun x_t () Real [0.300000, 0.800000])
(declare-fun time () Real [0.000000, 1.000000])
(define-ode flow_1 ((= d/dt[x] (* -1.0 x))))
(assert (and
  (= [x_t] (integral 0. time [x_0] flow_1))
  (<= x_t 0.35)
))
(check-sat)
```

One file; the trigger is the CLI knob. Verified outputs (all < 0.05 s; key pair
deterministic ×3):

| invocation (`gcc_build/dreal4 <flags> bug013_ode_order16_inert.smt2`) | output |
|---|---|
| `--ode-backward false` (knob defaults: order 12, tols 1e-10) | `unsat` ✓ baseline |
| `--ode-backward false --ode-taylor-order 16` | `delta-sat with delta = 0.001` ✗ missed refutation |
| `--ode-backward false --ode-taylor-order 20` | `delta-sat with delta = 0.001` ✗ |
| `--ode-backward false --ode-abs-tol 1e-6` (also `1e-2`) | `delta-sat with delta = 0.001` ✗ |
| `--ode-backward false --ode-rel-tol 1e-6` (also `1e-2`) | `delta-sat with delta = 0.001` ✗ |
| backward ON (default) at every trigger knob above | `unsat` ✓ — masked, see below |

Masking note: with the default `--ode-backward true` the BWD lohner integrates from the
post-arithmetic-prune X_t = [0.3, 0.35] — a narrow IC on which CAPD succeeds at every
trigger knob — and refutes on its own, so the verdict flip needs `--ode-backward false`
on *this* instance. An instance whose backward IC is also wide loses both directions.
Also notable: the trigger's `delta-sat` arrives in ~10 ms — the inconclusive skip persists
down the entire bisection path to a δ-small box (bisection never rescues the prune here).

Canonical unit reproducer: `./gcc_build/dreal4_cmake_test --gtest_filter=KnobsDecayTest.*`
(`test/dreal/contractor/test/contractor_odes_knobs_test.cc` — the order/tol sweeps pin the
inert values bit-exactly at gate lb 0.3 with KNOWN-GAP comments referencing this entry;
when CAPD/step-control handling improves, those pins fail and should be tightened back to
the narrowing assertion).

**Root cause / Design notes**

The divergence→inertness mechanism is verified (CAPD step-control failure →
`integrate_tube_slices` reports `found = false` → the contractor's inconclusive exit
records the ODE for explanation splicing and skips narrowing). WHY CAPD's step control
fails at *higher* order / *looser* tolerance on this interval IC is an open
**hypothesis, not isolated** — candidate: the wide interval IC inflates high-order Taylor
coefficient enclosures until the predicted step underflows the controller's minimum, but
no experiment has varied this in isolation.

**Workaround**

None needed at the shipped defaults (order 12 / tols 1e-10 are unaffected). Avoid
`--ode-taylor-order > 12` and tolerances looser than 1e-10 on interval-IC ODE instances;
keeping the default `--ode-backward true` restores the refutation on this instance class.

**Binary**

`gcc_build/dreal4` @ `f51f78b8e` (branch `tech-debt-fixes` working tree, built 2026-07-23).

---

## BUG-014 — X-Taylor LP arms (`--obbt` / `--polytope` / `--polytope-linearizer both`) die on silent signals inside vendored SoPlex 4.0.2 `SPxMainSM::duplicateCols` (OOB write in presolve; heap corruption; latent SOUNDNESS risk, no wrong verdict observed)

**Symptom / Description**

The Phase-7 sweep (`benchmark/results/sweep_20260723_115956/ANALYSIS.md` §New crash class)
recorded 8 jobs on pure QF-NRA odeexpr files dying on SIGBUS/SIGSEGV/SIGTRAP/abort with zero
diagnostics (no verdict, empty or stats-only stderr), confined to the three arms that
exercise the X-Taylor LP path; the affine-linearizer and mohc arms were clean on the same
files. Reproduced solo: **6 of 8 combos crash 3/3 deterministically**, one 1/3, one 0/3
(sweep crash was at that run's finish line). Signals flip across identical invocations
(poly×F6: 138,139,138) — same defect, different landing spot.

All four signal flavors collapse to **one defect**: an out-of-bounds indexed *write* inside
`soplex::SPxMainSM::duplicateCols` (the Bixby–Wagner duplicate-column presolve of the
vendored SoPlex 4.0.2, `ibex-fork/lp_lib_wrapper/soplex/3rd/soplex-4.0.2.tar`), reached via
`ibex::LPSolver::minimize`. When the wild write hits unmapped memory → immediate
SIGBUS/SIGSEGV in `duplicateCols`; when it lands in mapped heap → corruption detected later
(SIGTRAP = libmalloc xzone freelist trap on `duplicateCols`'s own next `spx_alloc<int*>`;
SIGABRT = `malloc_vreport → abort()`) — or **not detected at all: the same combos sometimes
run to completion and print a verdict** (obbt×F5: delta-sat 2/3, SIGBUS 1/3).

**Reproducer(s)** (no minimized file committed — read-only investigation; corpus paths are
stable). F6 = `~/Documents/new_dreal/ode_expressivity_energy/benchmarks/forall/kuramoto_sat_N2__kuramoto_ideal_N2__both_descend__k0__dh1__GNone__gradient__forall__fbd0a7b3__a85c192e.smt2`,
F4 = sibling `kuramoto_ideal_N2…d96326f1__a85c192e.smt2`, F5 = `kuramoto_ideal_N3…804a1e71__9471f6d6.smt2`,
F1–F3 = `~/Documents/new_dreal/ode_expressivity/benchmarks/tanh_coupling/decrease_{d_i,exact,slope}__tau0.0015__*.smt2`.
Exit codes: 138=SIGBUS, 139=SIGSEGV, 133=SIGTRAP, 134=SIGABRT.

Solo repro, sweep binary (`gcc_build/dreal4` stamp `f51f78b8e <dirty>` = sweep's `60fca6f69`,
built 2026-07-23 07:20), `timeout 300`, 3 runs each — verified outputs:

| combo (sweep signal) | r1 / r2 / r3 |
|---|---|
| `--obbt` F5 (138) | 0 delta-sat / **138** / 0 delta-sat |
| `--obbt` F6 (138) | **133 / 133 / 133** (~15 s; under lldb: completed `unsat`) |
| `--polytope` F1 (134 @39.9 s) | 0 / 0 / 0 — delta-sat @38 s (sweep crash at finish line) |
| `--polytope` F4 (138) | **138 / 138 / 138** (~9 s) |
| `--polytope` F6 (139) | **138 / 139 / 138** (~8 s) |
| `…-linearizer both` F2 (138) | **138 / 138 / 138** (~8 s) |
| `…-linearizer both` F3 (139) | **139 / 139 / 139** (instant) |
| `…-linearizer both` F5 (133) | **133 / 133 / 138** (~4 s) |

**Stack** (lldb, identical shape for poly×F4, poly×F6, hyb×F5, hyb×F3; ~20 CrashReporter
`.ips` files corroborate, incl. the SIGTRAP/SIGABRT flavors):

```
frame #0 soplex::SPxMainSM::duplicateCols(soplex::SPxLPBase<double>&, bool&) + 800   ← faulting RMW store
frame #1 soplex::SPxMainSM::simplify(...) + 3452
frame #2 soplex::SoPlex::_preprocessAndSolveReal(bool) + 684
frame #3 soplex::SoPlex::_optimizeReal() + 340
frame #4 soplex::SoPlex::optimize() + 628
frame #5 ibex::LPSolver::minimize() + 64
frame #6 ibex::CtcPolytopeHull::optimizer(ibex::IntervalVector&) + 288    (obbt arm: dreal::ContractorIbexObbt::Prune's own 2n-LP loop instead)
frame #9 dreal::ContractorIbexPolytope::Prune(...)  ← IcpSeq, main thread
```

Disassembly→source: `+796/+800/+808` = `spxmainsm.cpp:4607-4608`
(`pClass[m_classSetCols[k].index(l)] = classIdx; ++classSize[classIdx];`); a second observed
PC `+448` = `:4572` (`scale[j]` load, garbage `j` from an already-corrupted row SVector).
Register proof at the fault: `classIdx` (w10) = `0xdcc715c0` — the low 32 bits of a heap
pointer (live pointers `0x8dcd8xxxx`) — i.e. `++classSize[<pointer fragment>]`.

**Isolation matrix** (one axis varied per row, off the crashing default; ESTABLISHED level):

| axis varied | result | reading |
|---|---|---|
| linearizer → `affine`, same file/cell/binary (F4, F6) | **0/6 crash**, clean `unsat` | X-Taylor linearizer required — affine drives the *same* CtcPolytopeHull→LPSolver→SoPlex chain cleanly |
| linearizer → explicit `xtaylor` (F6) | 3/3 crash (138,138,134) | confirms default = xtaylor = crash |
| `--jobs 2` (poly F6) | 2/3 crash (133,138), 1/3 `unsat` | not thread-dependent; crashes in single-threaded IcpSeq and in parallel alike |
| binary → `cmake-build-release/dreal4` @ `fc4c3da04` (2026-07-01, fork pin `d9930909`, pre-campaign; `--polytope` only) (F4, F6) | **6/6 crash** (138/139) | **predates the campaign** — `--polytope`+XTaylor+soplex was already affected; the campaign's obbt/hybrid arms only added more routes into the same path |
| binary → `cmake-build-debug/dreal4` (Debug dReal, F6) | 2/2 crash (138,139) | dReal-side asserts don't catch it — defect is inside soplex, which the ExternalProject builds Release regardless |

**Established vs hypothesis**

- ESTABLISHED: crash site + character — OOB garbage-index write inside SoPlex 4.0.2
  `SPxMainSM::duplicateCols` (5 lldb stacks at 2 PCs, register-level pointer-fragment index,
  CrashReporter set covering all four signals).
- ESTABLISHED (flag level, single-variable swap): the trigger requires the X-Taylor
  linearizer's LPs; affine LPs through the identical solve chain never trigger it.
- HYPOTHESIS (not isolated): *why* — candidate chain: X-Taylor cut rows with degenerate
  (parallel / non-finite) coefficients exhaust `duplicateCols`' parallel-class index pool
  (`idxSet` underflow) → garbage `classIdx` → wild writes. No source-level (assert-enabled
  soplex) confirmation; no NaN observed in a cut row.
- Upstream status UNVERIFIED: scipopt/soplex master CHANGELOG has **no** post-4.0.2 entry
  matching this defect (the duplicate-column fixes listed are 2011-era postsolve issues), so
  "fixed upstream" cannot be assumed; whether newer SoPlex still crashes is untested.

**Classification**

Memory corruption in a contraction path. Today the failure is loud (8/8 sweep deaths, no
verdict printed) — but the SIGTRAP/SIGABRT flavors prove execution *continues between
corruption and detection*, and the same combos sometimes complete with a verdict, so a
silent-corruption-then-verdict run is possible in principle: a corrupted LP bound/box could
narrow past a true model → **latent SOUNDNESS risk (would assert φ T-unsatisfiable on a
T-satisfiable φ — false unsat)**. No wrong verdict observed: the sweep's flip audit found
zero unexplained disagreements, and every completed verdict on the six files agrees across
arms (F4/F6 `unsat`, F5 sat, F1 delta-sat). Every lost run is otherwise COMPLETENESS-shaped
(budget lost to a crash).

**Workaround**

Prefer `--polytope --polytope-linearizer affine` (sweep: solve-set strict superset of
polytope's, 0 crashes) or `--mohc`. Treat `--obbt`, bare `--polytope` (= xtaylor), and
`--polytope-linearizer both` as crash-exposed until fixed. Candidate fixes, both untested:
disable the SoPlex simplifier in the fork wrapper (`ibex_LPLibWrapper.cpp` never sets
`SoPlex::SIMPLIFIER`, so presolve runs at default-on; `setIntParam(SIMPLIFIER,
SIMPLIFIER_OFF)` would bypass `SPxMainSM` entirely at some LP-solve cost) or bump the
vendored soplex-4.0.2 tarball.

**Binary**

Sweep binary copy `dreal4_60fca6f69` (= `gcc_build/dreal4`, stamp `f51f78b8e <dirty>`, built
2026-07-23 07:20, IBEX fork @ `b5e7a212`); cross-checked against `cmake-build-release/dreal4`
@ `fc4c3da04` (fork @ `d9930909`) and `cmake-build-debug/dreal4`. Repro artifacts (24-run
matrix, isolation battery, lldb logs): `/private/tmp/claude-501/…/tmp/{repro1,isolate,lldb_*.txt}`
(ephemeral).

---

*Add new entries above this line.*
