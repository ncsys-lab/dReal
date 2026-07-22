# `capd_docs/` — CAPD API reference + leverage audit for dReal

A navigable, agent-readable summary of CAPD's API, built to answer one recurring
question fast: **"does CAPD offer X, and does dReal already use it?"** — so the
next person doesn't have to re-read 70 MB of Doxygen to find a lever (the way the
2026-06 `timeDerivative()` tube fix was found).

Source docs: `../../CAPD/docs/html/` (2392 Doxygen pages; mapped by
`../../CAPD/docs/INDEX.md`). Everything here is distilled from those pages and
**confirmed against the installed headers** at
`../gcc_build/capd-install/include/capd/`.

## Start here

| File | What it is |
|---|---|
| **[AUDIT.md](AUDIT.md)** | **The payoff** — prioritized opportunities for dReal to leverage CAPD better, tiered A–E (structural / cheap-tuning / guards / conditional / negative-results), each cross-referenced to current usage and confirmed in headers. |
| **[KNOBS.md](KNOBS.md)** | **The complete tuning-knob surface** — every tunable in CAPD's ODE path (set type, QR/reorg policy, step control, order, tolerances, …), its option-menu size, what dReal does with it, and a coverage-status audit. Start here to check nothing is being left on the table. |
| **[classes/sets/COMPARISON.md](classes/sets/COMPARISON.md)** | **The set-representation catalog** — every CAPD dynamical-set type (the wrapping-effect / tightness lever) in one comparison table with strengths/weaknesses + ordered "what to try" recommendations. dReal exposes it as `--ode-c0-set`. |
| [classes/sets/QRPolicies.md](classes/sets/QRPolicies.md) | **The frame-maintenance menu** — the QR-orthogonalization × reorganization policies (the *other* half of what makes Rect2/Pped2/Intv2 differ), an independent knob dReal hard-wires. |
| [dreal-capd-usage.md](dreal-capd-usage.md) | The baseline: exactly what dReal binds against today (the audit's reference point). |
| [index/all-classes.md](index/all-classes.md) | Auto-harvested one-line stub for all 577 CAPD classes (name + real Doxygen description-cell brief + link; earlier junk placeholder briefs replaced) — the catch-all for anything without a rich node. Regenerate with `scripts/harvest_class_briefs.py --capd`. |

## The tree

- **[concepts/](concepts/)** — narrative summaries of CAPD's conceptual topics
  (the "what exists and why" layer):
  [odes-rigorous](concepts/odes-rigorous.md) ·
  [taylor-method](concepts/taylor-method.md) ·
  [timemap-integration](concepts/timemap-integration.md) ·
  [curves-and-jets](concepts/curves-and-jets.md) ·
  [variational-equations](concepts/variational-equations.md) ·
  [dynsets-and-wrapping](concepts/dynsets-and-wrapping.md) ·
  [geomsets](concepts/geomsets.md) ·
  [poincare-maps](concepts/poincare-maps.md) ·
  [diff-inclusions](concepts/diff-inclusions.md) ·
  [maps-and-autodiff](concepts/maps-and-autodiff.md) ·
  [intervals-and-rounding](concepts/intervals-and-rounding.md) ·
  [linear-algebra](concepts/linear-algebra.md) ·
  [threading](concepts/threading.md) ·
  [dissipative-pdes](concepts/dissipative-pdes.md)
- **[modules/](modules/)** — one node per Doxygen module group (all 27), each =
  what the module is for + its classes + "dReal uses? y/n". Most dReal-irrelevant
  groups (homology, normalForms, …) are one-line stubs for breadth; [pdes](modules/pdes.md)
  is a rich node (dissipative-PDE rigorous integration — a future-direction extension of the
  ODE path; concept: [dissipative-pdes](concepts/dissipative-pdes.md), classes:
  [classes/pdes/](classes/pdes/)).
- **[classes/](classes/)** — rich summaries for the ~25 performance-relevant
  classes:
  - `sets/` — the C0/C1 set zoo & wrapping policies (the overestimation lever)
  - `solvers/` — OdeSolver, C1/Cn, StepControl
  - `curves/` — Curve (where the tube fix came from), BasicCurve, Jet
  - `maps/` — Map / autodiff
  - `integration/` — TimeMap, PoincareMap, Sections, DiffInclusion
  - root: Interval, Vector, Matrix, Rounding
- **[index/all-classes.md](index/all-classes.md)** — everything else (stubs).

## Future directions (PDE / DAE)

dReal wants to grow beyond ODEs. The two targets land very differently against
CAPD:

| Target | CAPD support | Entry node |
|---|---|---|
| **Dissipative PDEs** (Kuramoto–Sivashinsky-type semilinear parabolic, spectral/Galerkin + rigorously-enclosed tail) | **Yes, but narrow** — `PdeSolver` + geometric-tail sets; *not* general elliptic/hyperbolic/method-of-lines | [concepts/dissipative-pdes.md](concepts/dissipative-pdes.md), [modules/pdes.md](modules/pdes.md), [classes/pdes/](classes/pdes/) |
| **DAEs** (`x'=f(x,y) ∧ 0=g(x,y)`) | **None** — CAPD has no DAE primitives at all | [concepts/dae-support.md](concepts/dae-support.md) |

Two gotchas the PDE crawl surfaced (verified): `PdeCurve` has **no
`timeDerivative`** (so the 2026-06 centered-in-time tube trick wouldn't transfer
as-is), and `C0DoubletonSetGeometricTail` calls **`exit(0)`** on internal
inconsistency rather than throwing (incompatible with dReal's catch-and-skip
soundness pattern). For DAEs the honest finding is that support is **dReal-side
composition** (CAPD integrates the differential part; the algebraic constraint
maps onto the existing per-slice `forall_t` invariant/HC4 machinery), not a CAPD
backend to adopt.

## How this was built / how to extend it

The crawl never reads raw HTML (pages reach 500 KB). The checked-in de-tagger
`scripts/refresh_capd_doxygen.py` (stdlib `html.parser`; `--name <ClassName>`
resolves the mangled Doxygen filename for you) renders any page to ~10–20× smaller
text first, and `scripts/harvest_class_briefs.py --capd` rebuilds the
`index/all-classes.md` stub index from `annotated.html`. To add or refresh a node:
extract the page, read the text, copy method signatures **from the page or the
header** (never inferred from a class name — the one fidelity rule here, since a
fabricated CAPD feature would send a future session chasing an API that doesn't
exist), and link back to the source html.

> Scope: this is a tiered map — complete breadth (every module + concept has a
> node; all classes are at least stubbed), selective depth (rich summaries only
> where they inform performance). It is a reference, not a substitute for the
> Doxygen site or the headers; when precision matters, the header is ground truth.
