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

s2d's log BUG-016 (a thin start exactly on a nonzero fixed point, CAPD "minimal time step
reached") reaches the same silent `!res.found` skip through a different CAPD failure; the two
are separate bugs.

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

## BUG-015 — SIGSEGV: an `integral` whose start variable has no box and whose flow has `sin`/`cos` sends CAPD's interval `sin` a [NaN, NaN] argument, on which `scaledSin1` recurses until the stack is exhausted (LIVE, 2026-10-04)

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

**Workaround**

Give every `integral` start variable whose flow uses `sin`/`cos` a finite box. s2d does not
(by decision, s2d `docs/decisions.md`, "no default box on a defined signal or on an
Integrator's state"); its six affected parity rows stay DREAL_ERROR. Candidate fixes, both
untested: refuse a non-finite initial set in dReal's CAPD adapter before integrating
(treat it as an inconclusive skip, like the other CAPD failures), or make CAPD's `sin`/`cos`
return `[-1, 1]` for a non-finite argument.

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

*Add new entries above this line.*
