# Concept: Long-time integration via TimeMap

## What it is
`ITimeMap` wraps an `IOdeSolver` to integrate a set over a time interval with automatic step
control, hiding the step bookkeeping. Three usage modes: integrate-to-time, dense output
(step-by-step), and solution-curve. (`odesrig_timemap.html` rigorous · `odes_timemap.html`
nonrigorous narrative for the step-control discussion.)

## Key API (confirmed in doc/`TimeMap.h`)
- Construct: `ITimeMap timeMap(solver);`  Integrate: `IVector y = timeMap(finalTime, set);`
  (integrates over `finalTime − set.getCurrentTime()`; for nonautonomous systems the set
  carries its `initTime`).
- **Dense output (the mode dReal uses):**
  - `timeMap.stopAfterStep(true)` → each call returns after one successful step.
  - loop `do { timeMap(finalTime, set); … } while (!timeMap.completed());`
  - `set.getCurrentTime()`, `set.getLastEnclosure()` (rough hull over the last step),
    `(IVector)set` (sharp value at current time).
- **Solution curve:** `ITimeMap::SolutionCurve sol(initTime); timeMap(finalTime, set, sol);`
  then `sol(t)` / `sol(interval(a,b))` evaluates the rigorous bound at any sub-time **without
  re-integration**; `getLeftDomain()`/`getRightDomain()` give the valid range; out-of-domain
  evaluation throws.
- **Variational overloads** (header-confirmed): `operator()(time, set, MatrixType& derivative)`
  returns the **monodromy matrix** ∂φ/∂x₀ at `time`; further overloads take/return
  `HessianType` and `JetType` for 2nd-order / Cn variational integration.
- Order/step passthrough: `getOrder`/`setOrder`, `getStep`/`setStep`,
  `turn{On,Off}StepControl`, `getSolver`.

## dReal status
**Used (dense-output mode, C0 only).** dReal calls `stopAfterStep(true)` then walks steps with
`completed()`/`getCurrentTime()`, evaluating `solver.getCurve()(sub)` and
`curve.timeDerivative(sub)` per step (dreal-capd-usage.md §integration path 3-4). The
solution-curve object and **all variational `derivative`/`hessian`/`jet` overloads are unused.**

## Why it might matter
- The `MatrixType& derivative` overload is the **drop-in monodromy** route: one extra arg to
  the same `timeMap(...)` call yields ∂φ/∂x₀, enabling interval-Newton/mean-value contraction
  on the initial box (backward narrowing) that ibex HC4 cannot do from C0 slice values alone.
  Needs only a **C1 set** (e.g. `C1Rect2Set`) fed to the *existing* `ITimeMap` — `IOdeSolver`
  (`= OdeSolver<IMap>`, a `C1DynSys`, `OdeSolver.h:39`) already integrates the first-order
  variational equation, so **no solver change**; CAPD's own worked example
  (`odesvar_rigorous.html`) drives the monodromy with plain `IOdeSolver` + `ITimeMap` +
  `C1Rect2Set`. `IC2OdeSolver`/`ICnOdeSolver` (via `IC2TimeMap`/`ICnTimeMap`,
  `poincare/typedefs.h:58-59`) are needed only for the 2nd-order `HessianType` and Cn
  `JetType` overloads, not the monodromy. See `variational-equations.md`.
- `SolutionCurve` gives O(1) re-evaluation at arbitrary sub-times — dReal currently re-derives
  per-slice from `getCurve()` each step, which is equivalent but the curve object could
  simplify the windowed gate-state evaluation.

## Source
[odesrig_timemap.html](../../../CAPD/docs/html/odesrig_timemap.html) ·
[odes_timemap.html](../../../CAPD/docs/html/odes_timemap.html)
