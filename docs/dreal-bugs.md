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

*Add new entries above this line.*
