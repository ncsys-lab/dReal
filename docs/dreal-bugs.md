# dReal solver issues

Confirmed and suspected bugs, unintuitive behaviors, feature requests, and performance
problems in this project's pinned dReal binary, with minimal reproducers. Files are
committed under `docs/dreal-bugs/`. List entries with:
`grep "^## BUG-\|^## QUIRK-\|^## FEAT-\|^## PERF-" docs/dreal-bugs.md`

Numbering is one project-wide sequence shared with s2d's log
(`~/Documents/MATLAB/simulink-to-dreal/docs/dreal-bugs.md`, once `simulink-to-dreal_bug_reports.md`):
BUG-001 … BUG-012 and BUG-016 live there, the rest here. A new entry takes the number after the
highest in either log and lands here; the `docs/dreal-bugs/bug*.smt2` reproducer convention is
shared.

## BUG-013 — `--ode-taylor-order` 16/20 and `--ode-abs-tol`/`--ode-rel-tol` ≥ 1e-6 miss a refutation on an interval-IC instance: one long CAPD step leaves a wide last sub-slice whose mean-value tube is too loose (COMPLETENESS; LIVE — fixed 2026-10-05, fix dropped 2026-10-08; re-diagnosed — CAPD does not diverge)

**Symptom / Description**

On an interval-IC decay instance (dx/dt = −x, x0 ∈ [1,2], t ∈ [0,1]) with the gate
`x_t ≤ 0.35` below the tube minimum e^-1 ≈ 0.368, the FWD prune refutes at the default
knobs (order 12, tolerances 1e-10) and does not refute when the Taylor order is raised to 16
or 20 or either tolerance is loosened to 1e-6 or 1e-2. The run is then `delta-sat` with zero
branchings. COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed
refutation); the tube only ever stayed an outward enclosure.

**Reproducer(s)**

`docs/dreal-bugs/bug013_ode_order16_inert.smt2`:

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

`timeout 30 gcc_build/dreal4 --ode-backward false <flags> bug013_ode_order16_inert.smt2`
(gcc_build at `a6eea0bfa`, before the fix):

| flags | output |
|---|---|
| (defaults) | `unsat` ✓ |
| `--ode-taylor-order 16`, `20`; `--ode-abs-tol 1e-6`, `1e-2`; `--ode-rel-tol 1e-6`, `1e-2` | `delta-sat`, 0 branchings ✗ |
| `--ode-taylor-order 16 --ode-hull-grid 16` | `unsat` |
| `--ode-taylor-order 16 --refine-witness` | `unsat` (4 branchings) |

With the default `--ode-backward true` the BWD prune refutes on its own on this instance, which
masks the miss.

**Root cause** (corrected 2026-10-05; the first diagnosis, "CAPD divergence → inconclusive
skip", was wrong)

- ESTABLISHED: CAPD integrates successfully at every trigger knob. The `--verbose debug` log
  has no `ContractorStatus::AddInconclusiveOde` line, and the first FWD Prune narrows time
  from [0,1] to [0.75,1] while x_t stays at [0.3, 0.35].
- ESTABLISHED (single-factor runs): step length decides. At order 16, `--ode-max-step 0.375`
  (which forces the default 0.75 + 0.25 step sequence) and `--ode-hull-grid 16` both refute;
  `--ode-hull-grid 8` and `--ode-max-step 0.5` do not. The flip happens when the last
  sub-slice narrows from 0.125 to 0.0625 wide.
- Mechanism: the trigger knobs make CAPD's first step cover the whole horizon, so the four
  sub-slices are 0.25 wide. On the last one, [0.75, 1], the mean-value bound
  `x(mid) + x'(sub)·(sub − mid)` pairs the midpoint value of one trajectory (x0 = 1) with the
  derivative bound of another (x0 = 2): about 0.417 − 1.103·0.125 ≈ 0.279 < 0.3. A model of
  CAPD's formulas reproduces the measured lower bound, and the hull-grid 8 / 16 results.
  Even an exact derivative would give 0.299, so a tighter derivative cannot fix it.
- The fast-accept ODE evaluator then accepts the un-refuted root box with no branching
  (`docs/decisions.md`, "ODE formula evaluator").

**Fix (2026-10-05)**

`centered_curve_range` (`src/dreal/contractor/odes/contractor_odes_capd.cc`) adds a
monotone-in-time hull: for each component whose derivative enclosure over the sub-slice
excludes 0, every trajectory of the set is monotone there, so the component lies between its
values at the two ends of the sub-slice; that hull is intersected with the naive and
mean-value ranges. It keeps the initial-condition correlation that the mean-value form loses.
On the last sub-slice above it gives about [0.368, 0.945], so the gate is refuted. Sound as
long as the derivative enclosure is rigorous, which CAPD's `timeDerivative` is from CAPD
`2a2263c7` on (BUG-018).

Tests: `KnobsDecayTest.Bug013_LongStepTube_RefutesDisjointGate` (each trigger knob; red before
the fix with time at [0.75,1]); the OrderSweep / TolSweep KNOWN-GAP pins (`lb == 0.3`, which
could not tell a loose tube from a skip) are replaced by the narrowing assertion at every knob.
After the fix all seven knob settings of the reproducer answer `unsat`.

Cost. ODE-family A/B against the BUG-015/016 build (`benchmark/results/ab_20261005_032318`,
the 103 non-blacklisted jobs, CPU time): no SAT/UNSAT disagreement; 1.16× CPU on the 102 jobs
both solved (median 1.00×; github 1.25×, tacas 1.00×, saradc 0.98×); PAR2 1.29×, because
`github_oct5_0hz_k128_cardomain_car-3-single-linear-no-acc-no-lock` (554 s before, near the
600 s cap) timed out. No job gained a refutation. Evaluating each sub-slice boundary once
(adjacent sub-slices share an end) moved the aggregate only from 1.19× to 1.16×, so the extra
curve evaluations are not most of the cost; where it goes (a different search on the SAT-heavy
github instances is the leading guess) was not measured.

**Fix dropped (owner, 2026-10-08)**

The hull is reverted: the gap is COMPLETENESS only, on a few instances, so it does not
earn the extra code. In the two Sherlock gate runs the branch with the hull sat at PAR2
0.991–1.018 per set against base, apart from s2d's BUG-021 loss (no arm isolated the hull). The knob tests now pin the gap
(`Bug013_LongStepTube_RefutesDisjointGate`: the default knobs refute, the six trigger knobs
do not; the order and tolerance sweeps pin `X_t.lb == 0.3` at the trigger knobs), so a
change that closes it fails loudly.

---

## BUG-014 — X-Taylor LP arms (`--obbt` / `--polytope` / `--polytope-linearizer both`) die on silent signals inside vendored SoPlex 4.0.2 `SPxMainSM::duplicateCols` (OOB write in presolve; heap corruption; latent SOUNDNESS risk, no wrong verdict observed; fixed 2026-10-05)

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

**Root cause (isolated 2026-10-05)**

The trigger is X-Taylor rows that carry denormal coefficients. dReal's scratch build against an
assert-enabled IBEX/SoPlex (EP rebuilt `-O0 -g -DDEBUG`, no `NDEBUG`) on F3 with
`--polytope --polytope-linearizer both --jobs 1`, which is deterministic:

1. the first assertion is SoPlex's soft invariant `WMAISM12 isNotZero(aij, 1.0 / infinity)`
   (`spxmainsm.cpp:2594`, `simplifyRows`): presolve assumes no stored coefficient has magnitude
   ≤ 1e-100;
2. with that assertion made print-only, the next one is hard: `position != -1` in
   `doRemoveRow` (`spxlpbase.h:2072`), i.e. presolve's row and column copies of the LP disagree
   — the structural break the earlier hypothesis H1 guessed at;
3. with entries of magnitude ≤ 1e-100 dropped when the wrapper builds a row (experiment only),
   no assertion fires and F3 runs to the 120 s timeout. The dropped entries were ±3.95e-323
   (about 48k of them), i.e. gradient components that are zero widened outward to denormals.

So the out-of-bounds write in `duplicateCols` follows from an LP that violates SoPlex 4.0.2's
presolve precondition, not from NaN rows (none were seen).

**Fix (2026-10-05)**

ibex-fork `6b1b2c10` sets `SoPlex::SIMPLIFIER_OFF` in `LPSolver::init`: presolve
(`SPxMainSM`) never runs, the simplex takes the LP as built, and the Neumaier–Shcherbina
certificates read it as given. `CMakeLists.txt` pins IBEX `6b1b2c10`, which also carries the
BUG-019 fixes. Test: `DrealBugsRegressionDeathTest.Bug014_XTaylorBothSurvivesF3` (SIGBUS 3/3
before, passes 3/3 after). The entry's 8-combination × 3-run matrix on the fixed build:
no signal in any of the 24 runs (`timeout 300`, 12 at a time): `--obbt` F5 `delta-sat` 3/3, F6
`unsat` 3/3; `--polytope` F1 `delta-sat` 3/3, F4 and F6 `unsat` 3/3; `--polytope-linearizer
both` F5 `delta-sat` 3/3, F2 and F3 timeouts 3/3 (they had crashed at about 8 s and at once).
The verdicts agree with the July sweep's.

The corpus paths above moved: F1–F3 are in `~/Documents/expressivity/v1/benchmarks/tanh_coupling/`
and F4–F6 in `~/Documents/expressivity/v2/benchmarks/forall/` (same file names).

**Corpus check (2026-10-08)**

The `tech_debt_fixes_gate` Sherlock run (600 s) co-ran `--polytope --polytope-linearizer affine`
on two dReal commits with identical sources: 30926ebc3 with IBEX 6637e6e7 (`b5e7a212` plus the
SoPlex GCC 14 hunk) and c9bd88f06 with IBEX 33b883f2 (`6b1b2c10` plus that hunk). Over 7336
benchmarks: no verdict flips, 1351 vs 1349 solved (4 and 2 one-sided), per-set PAR2 new/old
1.000–1.006, memouts equal to within one per set. The affine linearizer never reached the
crash, so this shows that presolve off and the BUG-019 changes cost nothing on the LP path; it
does not test the crash fix itself.

**Binary**

Sweep binary copy `dreal4_60fca6f69` (= `gcc_build/dreal4`, stamp `f51f78b8e <dirty>`, built
2026-07-23 07:20, IBEX fork @ `b5e7a212`); cross-checked against `cmake-build-release/dreal4`
@ `fc4c3da04` (fork @ `d9930909`) and `cmake-build-debug/dreal4`. Repro artifacts (24-run
matrix, isolation battery, lldb logs): `/private/tmp/claude-501/…/tmp/{repro1,isolate,lldb_*.txt}`
(ephemeral). Fixed on `gcc_build/dreal4` with IBEX `6b1b2c10`.

---

## BUG-015 — SIGSEGV: an `integral` whose start variable has no box and whose flow has `sin`/`cos` sends CAPD's interval `sin` a [NaN, NaN] argument, on which `scaledSin1` recurses until the stack is exhausted (fixed 2026-10-05)

**Symptom / Description**

dReal dies of signal 11 (exit 139, no verdict, nothing on stderr) in about 0.03 s on a
one-line `integral` whose flow contains `sin` or `cos` and whose start variable is declared
without a box. Both pinned builds, `--ode-backward` true or false. With a finite box on the
start variable the same query is `delta-sat`. It is loud, so not a soundness hazard; every
affected run is lost (COMPLETENESS-shaped).

Found through s2d: since s2d `d4cc8e5` a defined signal and an Integrator state without
limits get no default ±1e6 box, and six parity queries of the case study's vendor-copy
models `microgrid_cs_core_{grid,islanded}[_ts0p1]_norefgen_nobs` (swing equation with
`sin δ`) crash after one to three lemmas (s2d `triage/campaign_results.json`, 2026-10-04,
DREAL_ERROR rows; s2d keeps the change, so the fix is dReal's).

**Reproducer(s)**

`docs/dreal-bugs/bug015_unbounded_sin_baseline.smt2` (248 B) — the start variable boxed:
```smt2
(set-logic QF_NRA_ODE)
(declare-fun x () Real)
(declare-fun x0 () Real [-1000000, 1000000])
(declare-fun xt () Real)
(declare-fun t () Real [0, 1])
(define-ode flow_1 ((= d/dt[x] (sin x))))
(assert (= [xt] (integral 0. t [x0] flow_1)))
(check-sat)
```

`docs/dreal-bugs/bug015_unbounded_sin_trigger.smt2` (228 B) — identical except `x0` has no box:
```smt2
(set-logic QF_NRA_ODE)
(declare-fun x () Real)
(declare-fun x0 () Real)
(declare-fun xt () Real)
(declare-fun t () Real [0, 1])
(define-ode flow_1 ((= d/dt[x] (sin x))))
(assert (= [xt] (integral 0. t [x0] flow_1)))
(check-sat)
```

`timeout 10 ./dreal_popl27 --model --ode-backward {true,false} <file>`, and the same on
`./dreal_partial_models`; identical on all four:

| file | output |
|---|---|
| baseline | `delta-sat with delta = 0.001`, `x0 : [-1000000, 1000000]`, `xt : [-2284424.786620999, 2284424.786620999]`, `t : [0, 1]` ✓ |
| trigger | exit 139 (SIGSEGV), no output ✗ |

Other probes, all with the start unboxed: a box on `xt` alone still crashes; `cos` in
place of `sin` crashes; `d/dt[d] = 1, d/dt[w] = sin d` crashes, but `d/dt[d] = 0,
d/dt[w] = sin d` does not; `d/dt[x] = -x`, and `d/dt[d] = 1` with `d/dt[w]` equal to `d`,
`d²` or `exp d`, do not crash (timeout at 6 s), nor did s2d's earlier `-0.5·x`, `1/x`, `x²`.
On these probes the crash needed `sin`/`cos` of a state that moves.

How it was found: delta debugging of s2d's campaign query for
`microgrid_cs_core_islanded_ts0p1_norefgen_nobs / load_step_0p10` (428,097 B, 2,881 lines;
in s2d's history as `14c6817:docs/dreal-bugs/bug014_stack_exhaustion_unboxed_core.smt2`),
keeping a candidate only if both builds exit by signal 11 within 6 s, one process at a time
(417 runs): the 1,518 assertions reduce to one `integral` of the swing-equation flow; its
eleven ODE components reduce to `d/dt[δ] = 1`, `d/dt[ω] = sin δ` with everything unboxed;
the trigger above is that with one component.

**Root cause / Design notes**

- ESTABLISHED (lldb, trigger and the 2,881-line original on `dreal_popl27`): the fault is
  `EXC_BAD_ACCESS (code=2)` on the stack guard page in
  `capd::intervals::operator/` called from `capd::intervals::scaledSin1`, under an unbroken
  chain of `scaledSin1` frames calling themselves (offset +396). On the trigger the argument
  of `scaledSin1` read at depths 1, 5 and 9 is `[NaN, NaN]`.
- ESTABLISHED (CAPD source, `gcc_build/capd-install/include/capd/intervals/Interval_Fun.hpp`):
  `sin` (:424) returns `[-1, 1]` only when `diam(x) ≥ 2π`, and range-reduces with loops
  guarded by `y.leftBound() < 0.` and `y.leftBound() >= pi2.rightBound()`; every one of
  these comparisons is false for NaN, so a NaN interval reaches `scaledSin1(y)` (:463).
  There the two branch guards (`x.leftBound() <= piby2.rightBound()`, `<= pi.rightBound()`)
  are false too, and it falls through to `return - scaledSin1 (x - pi);` (:418). NaN − π is
  NaN, so the recursion has no base case. It is a stack overflow, not a C++ exception, so the
  catch-all skip in `contractor_odes_capd.cc:505` does not apply.
- HYPOTHESIS (not isolated): the NaN is produced inside the CAPD integration from the
  unbounded start set `x0 = [-inf, inf]` (for example a midpoint or an ∞ − ∞ of the initial
  enclosure). A finite box on `x0` removes it; where in CAPD the NaN first appears was not
  traced.

**Root cause, refined (2026-10-05).** The NaN appears before any integration step: CAPD's
`Interval::split` (`Interval_Base.h`) computes the center (lb+ub)/2, which is NaN for
[-inf, inf] and infinite for a half-line, when the initial `C0Rect2Set` is built from the box.
`C0DoubletonSet::move` then evaluates the Taylor coefficients at that center. A half-line start
gives an infinite center instead; for `[-inf, 0]` that is the negative branch of `sin`'s range
reduction, whose guard can never fire (BUG-020). The same unguarded
boundary also copied CAPD enclosures into the box without a finiteness check, and a NaN bound
reads as an EMPTY ibex interval, which the per-slice filter takes as a refutation: a latent
false-`unsat` path, not observed.

**Fix (2026-10-05)**

`contractor_odes_capd.cc` checks every value that crosses into CAPD (start set, flow
parameters, integration end time) and every enclosure that comes back
(`require_capd_representable`): each bound must be finite and at most DBL_MAX/2 in magnitude,
so CAPD's (lb+ub)/2 cannot overflow, and an interval must not be inverted (which ibex reads as
EMPTY). A violation makes the integration inconclusive: no narrowing for that Prune, the ODE
recorded via `AddInconclusiveOde` with the reason, and a stderr warning if the theory check
ends `delta-sat` (the approved fallback, `docs/decisions.md` §"ODE inconclusive skip").
COMPLETENESS only (asserts φ^δ T-satisfiable on a possibly T-unsatisfiable φ — missed
refutation). After the fix the trigger answers `delta-sat` in milliseconds with

```
WARNING: delta-sat while 1 ODE constraint(s) were not integrated on some box of this search
(e.g. the integration's start set has a non-finite or inverted bound, or one beyond DBL_MAX/2, in …)
```

Tests: `NonFiniteCapdInputTest.*` in `contractor_odes_semantic_test.cc` (run in re-executed
children; each also checks that the boundary check recorded the ODE: before the fix the
unbounded `sin`/`cos` starts, FWD and BWD, died by signal 11 and the unbounded-time case by
SIGALRM; the `-x`, huge-start and unbounded-parameter cases left the box unchanged because
CAPD itself threw) and `DrealBugsRegressionDeathTest.Bug015_*`. A start set inside ±DBL_MAX/2
whose midpoint is below about −5.8e19 still hangs in CAPD's `sin`; that is BUG-020.

The same change widens every start-set interval by one ulp, which fixes s2d's BUG-016 (a thin
start on a nonzero fixed point), and records a flow parameter's start/end intersection as a
used constraint (it was unrecorded on the inconclusive exits — an explanation gap of the
`docs/constraint-order-explanation-soundness.md` class, not reproduced end to end;
`NonFiniteCapdInputTest.ParameterNarrowing_IsRecorded`). ODE-family A/B against the Phase-1
build (`benchmark/results/ab_20261005_023503`, 119 jobs, CPU time): no SAT/UNSAT
disagreement, 0.96× CPU on the 103 jobs both solved (median 0.99×); the solve-set differences
are four saradc k70 jobs killed by the memory daemon on one side or the other (excluded,
blacklisted) and `battery-double k2`, `unsat` in 44 s on the new build and a timeout on the
old.

**Workaround (before the fix)**

Give every `integral` start variable whose flow uses `sin`/`cos` a finite box.

**Binary**

`dreal_popl27` (Commit `c294eb435`, built 2026-07-13) and `dreal_partial_models` (Commit
`ce5c8971c`, built 2026-09-18). Recorded 2026-10-04.

---

## BUG-017 — Documentation: `docs/pattern-matching.md` misdescribes `--drpm-max-size` and `--drpm-max-time` (fixed 2026-10-05)

The log has no documentation category; this is filed as a BUG because the text states the
wrong behavior. Nothing in the solver is wrong.

**Symptom / Description**

`docs/pattern-matching.md:130-131` reads:

- `--drpm-max-size <n>`: "maximum number of formulas in the pattern database before pruning
  old entries"
- `--drpm-max-time <µs>`: "timeout (microseconds) for a single matching call"

The source says otherwise (all verified at `290bd8474`):

| flag | the source | correct reading |
|---|---|---|
| `--drpm-max-size` | `src/dreal/dreal_main.cc:303` "Set maximum lemma size to pattern match. (default = 0)"; `src/dreal/solver/context_impl.cc:472` `if (explanation.size() < config().drpm_max_size()` | a lemma (explanation) is pattern-matched only if it has **fewer literals** than this; it says nothing about the database size. The default 0 matches nothing, so DRPM is off unless the flag is given. |
| `--drpm-max-time` | `dreal_main.cc:309-310` "Set pattern matching timeout in seconds"; `src/dreal/solver/config.cc:201-202` (a `duration<double, seconds>`); `config.h:359` default `0.222`; `context_impl.cc:477-481` | **seconds**, not microseconds, and not the whole budget: a matching call gets `t_sat + t_theory + min(99·(t_sat + t_theory), drpm_max_time)`, where `t_sat` and `t_theory` are that lemma's own SAT and theory check times (`context_impl.cc:403-406`, `:434-438`). |

Also: `--drpm-max-size 0`, the documented default, is rejected on the command line
(`dreal_main.cc:114-115`: `positive_int_option_validator` is `gt 0`; `./dreal_popl27
--drpm-max-size 0 q.smt2` prints `ERROR: Got invalid argument "0" for option
--drpm-max-size.`). To turn DRPM off, omit the flag.

**Other docs checked** (dReal `docs/`, `CLAUDE.md`, `OPTIMIZATION_LOG.md`,
`exists_forall_perf.md`; s2d `docs/` and `triage/`; the case study's `data/findings.md`,
`pipeline/`, `slides/`): none repeats the wrong reading. The case study states the correct
one (`data/findings.md`, "What `--drpm-max-time` actually caps";
`slides/solver/drpm_and_partial_models.md:27-29`). One related error:
`exists_forall_perf.md:246` proposes a "`--drpm-max-size 0` A/B", which the validator
rejects.

**Workaround**

Read the flags as in the table; `dreal --help` prints the correct text.

**Fix (2026-10-05)**

`docs/pattern-matching.md` §"CLI Configuration" now states both flags as in the table, and
`exists_forall_perf.md` proposes an A/B with and without the flag instead of
`--drpm-max-size 0`.

---

## BUG-018 — False `unsat` at default flags on an ODE with an interval initial condition: the per-slice tube's mean-value term used CAPD's `Curve::timeDerivative`, which scaled the initial-condition spread wrongly (SOUNDNESS; fixed 2026-10-05 by the CAPD bump)

**Symptom / Description**

On a satisfiable `integral` whose start variable is an interval, dReal returns `unsat` with
default flags. This is a SOUNDNESS violation (asserts φ T-unsatisfiable on a T-satisfiable φ —
false unsat). It needs no flag: both `--ode-backward` values, every `--ode-c0-set`, and
`--ode-hull-grid 1` and `4` show it. Point initial conditions are unaffected, which is why the
existing ODE tests (all point-IC or with gates far from the tube edge) never saw it.

**Reproducer(s)**

`bug018_meanvalue_growth_baseline.smt2` and `bug018_meanvalue_growth_trigger.smt2` differ only
in the threshold (`2.200` / `2.205`):
```smt2
(set-logic QF_NRA_ODE)
(declare-fun x () Real [-100, 100])
(declare-fun x_0 () Real [1, 2])
(declare-fun x_t () Real [-100, 100])
(declare-fun time () Real [0, 1])
(define-ode flow_1 ((= d/dt[x] (* 0.1 x))))
(assert (= [x_t] (integral 0. time [x_0] flow_1)))
(assert (>= x_t 2.205))
(check-sat)
```
x0 = 2, t = 1 gives x_t = 2·e^0.1 = 2.2103, 0.0053 above the threshold (more than 5δ).

`bug018_meanvalue_decay_trigger.smt2`: the same with `(* -0.1 x)` and `(>= x_t 1.995)`
(satisfiable by x0 = 2, t ≤ 0.025).

`bug018_meanvalue_projectile_trigger.smt2`: `x' = v`, `v' = -1`, `x_0 = 0`, `v_0 ∈ [0.5, 1.5]`,
`time ∈ [0, 0.5]`, `(>= x_t 0.6)` (satisfiable by v0 = 1.5, t = 0.5: x_t = 0.625).

`timeout 60 <bin> --model <file>`, identical on `gcc_build/dreal4` and `dreal_popl27`:

| file | output |
|---|---|
| growth_baseline | `delta-sat with delta = 0.001`, `x_t : [2.199999999999999, 2.20429721542362]` ✗ — the box excludes the true maximum 2.2103 |
| growth_trigger | `unsat` ✗ false unsat |
| decay_trigger | `unsat` ✗ false unsat |
| projectile_trigger | `unsat` ✗ false unsat |

**Root cause / Design notes**

- ESTABLISHED (code): `centered_curve_range` (`src/dreal/contractor/odes/contractor_odes_capd.cc`,
  added in aa409c688, 2026-06-23) intersects the naive slice range `curve(sub)` with the
  mean-value form `curve(mid) + curve.timeDerivative(sub)·(sub − mid)`. At the CAPD pin
  `b353e170`, `Curve<…,true>::timeDerivative` (`capdDynSys/include/capd/diffAlgebra/Curve.hpp:194`)
  sets the initial-condition spread to `coefficient(d,1) − centerCoefficient(d,1)`, i.e.
  f(X) − f(c), where its sibling `operator()` (`:75`) uses `coefficient(d,0) −
  centerCoefficient(d,0)`, i.e. X − c. The spread is understated when |Df| < 1 and vanishes for a
  state-independent rate such as `v' = -1`, so the derivative enclosure misses real trajectories
  and the mean-value bound cuts them off.
- ESTABLISHED (upstream): CAPD commit `2a2263c7e` (2026-09-14, "Bug fixed in
  Curve::timeDerivative") changes exactly that line to `coefficient(d,0) −
  centerCoefficient(d,0)`.
- ESTABLISHED (isolation, one variable): with the CAPD pin set to `f59e2546`, the parent of
  `2a2263c7`, all six BUG-018 tests below fail; at `2a2263c7`, whose only change is that line,
  all six pass. A numeric model of CAPD's formulas also predicts the growth tube's upper bound
  as 2.20430, where the baseline's model box ends.

**Fix**

`CMakeLists.txt` pins CAPD `03dc5628` (upstream master, 2026-09-22), which includes
`2a2263c7`. Regression tests: `IntervalIcSpreadTest.*` in
`test/dreal/contractor/test/contractor_odes_semantic_test.cc` (FWD and BWD, hull-grid 1 and 4,
each C0 set type; every FWD configuration emptied the SAT box before the bump) and
`DrealBugsRegression.Bug018_*` (the three trigger files, `delta-sat`). After the bump all four
reproducers answer `delta-sat`; the growth baseline's `x_t` box contains 2.2103.

ODE-family A/B, old pin vs new (`benchmark/results/ab_20261005_005735`, 119 jobs, CPU time):
PAR2 0.98×, 107/119 solved on both sides, no SAT/UNSAT disagreement. The solve sets differ in
one job each way: `github_oct5_0hz_k4_battery_battery-double-sat` timed out on the old pin and
is `delta-sat` in 78 s on the new one, and `github_oct5_0hz_k2_battery_battery-double` was
`unsat` in 110 s on the old pin and timed out on the new one. Re-run alone on the new pin (900 s cap), battery-double k2 is `unsat` in 717 s CPU: the same
verdict, about 6.5× slower on that instance, not a lost refutation.

Any `unsat` obtained before the bump on a query with an interval start variable should be
re-checked on a binary built after it.

**Corpus check (2026-10-08)**

Base 6f02d4010 (old CAPD) against head f7333b0dc (new CAPD) on Sherlock: no verdict flips over
7336 benchmarks at 600 s (`tech_debt_fixes_gate`) or 7338 at 120 s (`bug021_fix_gate`). No
base `unsat` became a head `delta-sat`, so the false `unsat` does not show in this corpus; the
reproducers above remain its only witnesses.

**Binary**

Reproduced on `gcc_build/dreal4` (Commit `6f02d4010`, CAPD `b353e170`, built 2026-10-05 against
the MacOSX 26.5 SDK) and `dreal_popl27` (Commit `c294eb435`, built 2026-07-13). Fixed on
`gcc_build/dreal4` with CAPD `03dc5628`.

---

## BUG-019 — The LP certificates read non-finite data as a proof: an empty or NaN interval passed the Neumaier–Shcherbina infeasibility test, and Aᵀy was a plain floating-point product (latent SOUNDNESS, `--polytope`/`--obbt` only; fixed 2026-10-05)

**Symptom / Description**

Latent, found by code reading during the BUG-014 work; no false verdict was observed. ibex's
`LPSolver` certifies an LP result with Neumaier–Shcherbina post-processing before dReal trusts
it (`Mode::Certified`; `--polytope`, `--obbt`, `--forall-polytope`). Two defects made a
certificate possible from data that proves nothing:

- `neumaier_shcherbina_infeasibility_test` (`ibex-fork/src/numeric/ibex_LPSolver.cpp`) returned
  `!d.contains(0.0)` for `d = (Aᵀλ)·X − λ·b`. `contains` is false on an empty interval, and gaol
  reads a NaN bound, an infinite scalar or `±inf · interval` as empty. So a NaN or infinite entry
  in A, b, the bounds or the Farkas ray made the LP `InfeasibleProved`, and `CtcPolytopeHull` /
  `ContractorIbexObbt` then empty the box — SOUNDNESS (asserts φ T-unsatisfiable on a
  T-satisfiable φ — false unsat) whenever SoPlex wrongly reports infeasibility on such data.
- Both certificates computed Aᵀy as `Matrix * Vector`, a double product, not an enclosure, so a
  certificate within rounding of 0 could be wrong even on finite data.

Non-finite rows could reach the LP: `LPSolver::add_constraint`'s `isfinite` checks are asserts
(compiled out in the Release IBEX build), and X-Taylor's emptiness checks look only at component
0 of the gradient, so an empty component j > 0 became a NaN coefficient.

**Reproducer(s)**

No end-to-end false `unsat` was constructed: SoPlex must also misreport infeasibility on the
bad data. Unit-level, in dReal's suite (red before the fix):

- `LpSolverBoundary.NonFiniteRowIsRejected` (`contractor_ibex_polytope_linearizer_test.cc`):
  `add_constraint` with a NaN coefficient, an infinite bound, or an infinite coefficient
  accepted the row silently.
- `ContractorIbexPolytopeLinearizerTest.EmptyGradientComponentSkipsRow`: `x + sqrt(y) ≤ 1` on
  `y = [0, 0]` under X-Taylor. Once `add_constraint` refused non-finite rows (ibex-fork
  `b52f8626`), this Prune threw `std::invalid_argument`, which shows that X-Taylor emitted a
  NaN row there; it passes from `03712a0a` on.

**Fix (2026-10-05)**, ibex-fork `b52f8626` and `03712a0a` (pinned via `6b1b2c10`): both
certificates use `Matrix * IntervalVector` for Aᵀy; an empty `d` or objective is a failed
certificate and `minimize()` reports `OptimalProved` only when post-processing succeeds;
`add_constraint` throws on a non-finite coefficient or bound; X-Taylor checks every coefficient
and the bound and skips the row (`BadConstraint`, sound in RELAX mode). The empty-`d` and
rigorous-product changes have no red test: no deterministic input makes SoPlex hand back a
non-finite Farkas ray, so they are guarded by review only.

**Corpus check (2026-10-08)**: the affine A/B under BUG-014 covers these changes too (no
flips, PAR2 1.000–1.006).

**Binary**

`gcc_build/dreal4` with IBEX `b5e7a212` (before) and `6b1b2c10` (after), 2026-10-05.

---

## BUG-020 — CAPD's interval `sin` never returns for an argument below about −5.8e19 (its negative-overflow guard can never fire); an `integral` started there hangs (LIVE, 2026-10-05)

**Symptom / Description**

An `integral` whose flow applies `sin` (or `cos`, which CAPD computes as `sin(π/2 − x)`) to
a state that starts at a huge negative value never returns. `x0 = -1e20` hangs; `x0 = 1e20`
answers at once. CAPD evaluates the Taylor coefficients at the center of the start set, so
HYPOTHESIS (not run): any start set whose center is below about −5.8e19, such as
`[-1e21, 0]`, hangs the same way. Loud only through a timeout. COMPLETENESS-shaped (the run
is lost; no verdict is produced). Reachable from bisection of a variable with an unbounded or
huge box, so after BUG-015's fix rejects the unbounded start it is the remaining way into
CAPD's `sin` range reduction with a bad argument.

**Reproducer(s)**

`bug020_sin_huge_negative_baseline.smt2` and `bug020_sin_huge_negative_trigger.smt2` differ
only in the sign of `x0`:
```smt2
(set-logic QF_NRA_ODE)
(declare-fun x () Real [-1e21, 1e21])
(declare-fun x0 () Real [-1e21, 1e21])
(declare-fun xt () Real [-1e21, 1e21])
(declare-fun t () Real [0, 1])
(define-ode flow_1 ((= d/dt[x] (sin x))))
(assert (= x0 -1e20))
(assert (= [xt] (integral 0. t [x0] flow_1)))
(check-sat)
```

`timeout 20 <bin> --ode-backward false <file>` on `gcc_build/dreal4` with CAPD `03dc5628` and
with CAPD `b353e170` (the same on both, and with default flags):

| file | output |
|---|---|
| baseline (`1e20`) | `delta-sat with delta = 0.001` in 0.00 s ✓ |
| trigger (`-1e20`) | no output, killed by the 20 s timeout ✗ |

**Root cause / Design notes**

ESTABLISHED (CAPD source, `capdAlg/include/capd/intervals/Interval_Fun.hpp`, unchanged on
upstream master 2026-10-05): `sin` reduces its argument by k·2π with k from `toLongInt`. For a
positive argument it guards the conversion with `if (temp > std::numeric_limits<long>::max())
return [-1, 1]` (`:440`). For a negative argument it computes `temp = (-x.leftBound()) / 2π + 1`,
which is at least 1, and guards with `if (temp < std::numeric_limits<long>::min())` (`:447`),
which is never true. `toLongInt` is a bare `static_cast`, so for |x| beyond about 5.8e19 the
conversion overflows, `y = x − k·2π` stays near x, and `while (y.leftBound() < 0.) y += pi2;`
(`:454`) adds 2π to a value whose ulp is far larger than 2π, so it never terminates. HYPOTHESIS
(not run): the guard was meant to read `temp > std::numeric_limits<long>::max()`, mirroring
`:440`; with it the argument returns `[-1, 1]`.

**Workaround**

Bound every state that feeds `sin`/`cos` to a moderate magnitude. Candidate fixes, untested:
patch the one guard in CAPD (dReal pins CAPD with no `PATCH_COMMAND` today) and report it
upstream, or bound the magnitude of values handed to CAPD below about 5e19 in
`require_capd_representable` (a magic constant).

**Binary**

`gcc_build/dreal4` (Commit `57953403b` + the BUG-015/016 working tree, CAPD `03dc5628`, built
2026-10-05) and the same source with CAPD `b353e170`.

---

## BUG-021 — Since `a6eea0bfa`, a flow-parameter narrowing linked its integral into explanations through every state variable, and three s2d `pause__` queries that base refutes in 10–30 s ran past 600 s (COMPLETENESS; fixed 2026-10-07)

**Symptom / Description**

The 2026-10-07 `tech_debt_fixes_gate` Sherlock run (600 s cap, `--random-seed 777`) found
three s2d queries that base `6f02d4010` proves `unsat` in 16–27 s of CPU and head `f7333b0dc`
times out on. On Sherlock, head learned 14,190–23,141 lemmas per query where base needed 28–29.
COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed refutation): head gave
no wrong verdict; it stopped refuting.

**Reproducer(s)**

The three queries, s2d's current revisions in
`~/Documents/MATLAB/final_presentation_casestudies/benchmarks/s2d/manifest.json` (226 KB each,
so not copied here): `pause/pause__margin_shared_dU0.05_L59.0_u20__7183bca4.smt2`,
`pause/pause__margin_siloed_dU0.05_L59.4_u20__3b13ac08.smt2`,
`pause/pause__probe_iH0.001_L59.4_u20__d8bcd846.smt2`. Each asserts its 19 integrals as
top-level units. The mechanism alone:
`NonFiniteCapdInputTest.ParameterNarrowing_LinksOnlyThroughParameters`
(`test/dreal/contractor/test/contractor_odes_semantic_test.cc`).

Bisect, co-run with `benchmark/corun.sh` (E-cores, 120 CPU-s cap), CPU seconds for the three
queries in the order above:

| arm | result |
|---|---|
| `6f02d4010` (base), `90eaf1419` (CAPD bump), `57953403b` (Xcode 26.5 and 27 SDKs) | `unsat`, 10–31 s |
| `a6eea0bfa`, `30926ebc3`, `f7333b0dc` (head) | timeout |
| `a6eea0bfa`, its input check off, without the step-1 used-constraint record | `unsat`, 11–27 s |
| the same, without the step-1 output bit, the one-ulp start widening, or the input check | timeout |

**Root cause / Design notes**

- ESTABLISHED (the bisect above): `a6eea0bfa` made `contractor_ode_lohner::Prune` record the
  integral and its linked `forall_t` invariants as used constraints whenever step 1
  (pars_0 ∩ pars_t) narrows a parameter, to close the explanation gap of
  `docs/constraint-order-explanation-soundness.md`. That record alone loses the refutations.
- HYPOTHESIS (not isolated): an ordinary used constraint joins the explanation closure through
  all of its variables, so the integral pulled in every constraint on its start and end states
  and, through them, much of the BMC unrolling. The Sherlock median lemma lengths do not show it
  plainly (head 444–535, base 595–720 literals).
- Base's `unsat` on these queries is sound: each integral is a unit assertion, so a lemma that
  omits it is still implied by φ. This is an argument; no checker has confirmed it. The CAPD
  bump (BUG-018) also refutes them, so BUG-018 is not behind base's verdicts.
- Not this bug: the same run's tacas_c2e2 inverter `_UNS` queries that base refuted in about
  1 s on Sherlock time out at 120 s on the Mac for base too. Whether those refute depends on
  the platform.

**Fix**

Each used constraint now carries the variables through which an explanation reaches it
(`ContractorStatus::AddUsedConstraint(f, reach)`): all of its variables by default, and a join
takes the union. Step 1 records the integral reachable through its flow parameters alone and
no longer records the invariants. That is sound because the step reads only the parameters'
bounds and the p0 = pt the integral implies, and a later narrowing in the same Prune still
records the integral in full. Co-run after the fix, the same three queries: fix 4.2, 21.8,
5.4 s; base 9.8, 25.3, 11.0 s; head timeout (fix/base 0.43, 0.86, 0.49). Tests:
`ParameterNarrowing_LinksOnlyThroughParameters`, `ContractorJoinTest.JoinUnionsTheReachOfAUsedConstraint`,
`ContractorJoinTest.ReachOutsideTheFormulaThrows` (each red before its change).

**Corpus check (2026-10-08)**

`bug021_fix_gate` on Sherlock (120 s, 7338 benchmarks, co-run): fix 0e8887931 against head
f7333b0dc solves 17 vs 14 s2d and 1255 vs 1237 github_dreach queries and the same number
elsewhere, per-set PAR2 fix/head 0.974–1.000. One verdict differs, a `delta-sat` that is a
missed refutation through BUG-022, not a wrong `unsat`. Against base 6f02d4010 (the branch
gate): 1876 vs 1865 solved, per-set PAR2 0.965–1.010. tacas_c2e2 is the one set behind, 332
vs 339: 21 solved only by base and 14 only by the fix (head: 18 and 11). Cause not isolated.

**Workaround**

None needed.

**Binary**

Bisect arms built clean from each commit (`6f02d4010` and `90eaf1419` against the
CommandLineTools 26.5 SDK, which predates libc++ 22); head is `benchmark/bin/dreal4-f7333b0dc`.

---

## BUG-022 — A CAPD step a few denormals long makes the adapter's hull-grid sub-slices overrun the step, CAPD throws, and the skipped integral lets a `delta-sat` witness break its ODE (COMPLETENESS; LIVE, 2026-10-08)

**Symptom / Description**

In the 2026-10-07 `bug021_fix_gate` Sherlock run (120 s), `github_dreach/0hz_k4_airplane_airplane-single.drh.o.smt2`
was `unsat` on 6f02d4010 and f7333b0dc and `delta-sat` on 0e8887931. The witness has
`time_4 ∈ [0, 9.88e-324]` while `beta` goes from `beta_4_0 ≈ 0.1445` to `beta_4_t ∈ [-1.95, -1.35]`,
so it violates the step-4 flow by far more than δ = 0.001. COMPLETENESS (asserts φ^δ
T-satisfiable on a T-unsatisfiable φ — missed refutation). The run warns, as the approved
inconclusive-ODE skip should:

```
WARNING: delta-sat while 2 ODE constraint(s) were not integrated on some box of this search
(e.g. capd::diffAlgebra::Curve::operator(h) error: argument [1.4822e-323,1.4822e-323] is out
of domain=[0,9.88131e-324], in (= [beta_4_t, …] (integral flow_1, from t=0 to time_4, …))).
```

**Reproducer(s)**

`timeout 60 dreal4-0e88879317b8 --random-seed 777 --verbose warning --model <that file>` on
Sherlock (Linux, GCC 14), in a dev allocation. The same binary and file give `unsat` on macOS.
Base and head give `unsat` on both. Minimal attempts (`d/dt[x] = -x`, `t` narrowed to a
denormal through `(<= (* 1e300 t) 1e-23)`) give `unsat` on macOS; a denormal literal for the
bound crashes the parser instead (BUG-023).

**Root cause / Design notes**

- ESTABLISHED (CAPD's message): the curve was evaluated at 1.4822e-323 = 3 ulp of the
  smallest denormal, past its domain [0, 2 ulp].
- ESTABLISHED (source, `contractor_odes_capd.cc`, `integrate_tube_slices_impl`): sub-slice k
  spans [d_lo + k·dd, d_lo + (k+1)·dd] with `dd = (d_hi − d_lo)/kHullGrid` (the last ends at
  d_hi), and nothing keeps those points ≤ d_hi. For d_hi = 2 ulp, dd = 0.5 ulp: rounded up it
  is 1 ulp and the third sub-slice reaches 3 ulp; rounded to nearest (ties to even) it is 0.
  (In 0e8887931, which had the BUG-013 hull, the same points came from a `grid` vector.)
- HYPOTHESIS (not isolated): the grid is computed under upward rounding left behind by CAPD,
  which would also explain why only the Linux build shows it. The fix arm reached the box
  through a different search path, not through a change to this code.

**Workaround**

None. The stderr warning flags every affected `delta-sat`. Candidate fix, untested: build
the grid so that it cannot leave [d_lo, d_hi] (a monotone grid ending at d_hi, computed
under a `NearestRoundingScope`, with a throw if a point still exceeds d_hi).

**Binary**

`/scratch/users/ks1/exec/dreal4-0e88879317b8` (sha256 f4dbe5f1d7e2), built by `bench build`
on Sherlock, 2026-10-07.

---

## BUG-023 — A decimal literal that underflows to a denormal crashes the SMT-LIB2 parser: `std::stod` throws `out_of_range` (LIVE, 2026-10-08)

**Symptom / Description**

`1e-320` anywhere in an `.smt2` file aborts dReal (exit 134, no verdict). The scanner turns
every decimal literal into a double with `std::stod` (`src/dreal/smt2/scanner.ll:213`, also
`parser.yy` lines 248, 268 and 415), and `stod` throws `std::out_of_range` when `strtod` sets
`ERANGE`, which includes underflow to a denormal. Loud, so no wrong verdict;
COMPLETENESS-shaped (the run is lost).

**Reproducer(s)**

`docs/dreal-bugs/bug023_denormal_literal.smt2`:
```smt2
(set-logic QF_NRA)
(declare-fun x () Real [0, 1])
(assert (<= x 1e-320))
(check-sat)
```
Output on 6f02d4010, f7333b0dc and 0e8887931 (macOS): `libc++abi: terminating due to
uncaught exception of type std::out_of_range: stod: out of range`, exit 134 ✗.

**Root cause / Design notes**

ESTABLISHED (above). Not checked: whether a literal that does parse but is inexact enters
the box soundly, i.e. as an interval that contains it.

**Workaround**

Scale the term instead of writing the denormal: `(<= (* 1e300 x) 1e-20)` parses and solves
(tested with `t` in place of `x` on the three binaries above).

**Binary**

`benchmark/bin/dreal4-{6f02d4010-sdk265,f7333b0dc,bug021-fix}`, 2026-10-08.

---

*Add new entries above this line.*
