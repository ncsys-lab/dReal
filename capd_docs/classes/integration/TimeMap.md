# Class: `capd::poincare::TimeMap` (= `capd::ITimeMap`)

## What it is
Transports a set (or point) by a flow over a time interval, wrapping a solver and hiding the
step loop. `ITimeMap` = `TimeMap<IOdeSolver>` (it lives in the `poincare` namespace — CAPD
groups TimeMaps and PoincareMaps there — but integrates to a **time**, not a section; it does
no section crossing). Template parameter `SolverT`. (`TimeMap.h` page.)

## Key API (confirmed in `TimeMap.h` page)
Construction: `TimeMap(Solver& solver)`. Accessors: `getSolver`, `getDynamicalSystem`,
`getVectorField`, `getOrder`/`setOrder`, `getStep`/`setStep`,
`turn{On,Off}StepControl`/`onOffStepControl`.

Integration overloads (`operator()`), grouped by what they return:
- **Value only:** `operator()(ScalarType time, SetType& theSet)` → integrates `theSet` to
  `time` (uses `theSet.getCurrentTime()` as start for nonautonomous). Also the point form
  `operator()(time, VectorType& v[, ScalarType& in_out_time])`.
- **Dense output:** `stopAfterStep(bool)`, then loop on `completed()`; `getCurrentTime()`.
  A `operator()(time, set, SolutionCurve& solution)` fills a re-evaluable curve.
- **First-order variational:** `operator()(time, set, MatrixType& derivative)` → **monodromy
  matrix** ∂φ/∂x₀ at `time`; `(time, v, initMatrix, derivative)` propagates a given init matrix.
- **Second-order:** `operator()(time, v, MatrixType& derivative, HessianType& hessian)`
  (and `initMatrix`/`initHessian` forms).
- **Cn:** `operator()(time, v, JetType& jet)` / `(time, initJet, jet)` for full-jet transport.

## dReal status
**Used (value + dense output, C0).** dReal constructs `ITimeMap(solver)`, calls
`stopAfterStep(true)`, and loops with `completed()`/`getCurrentTime()` reading enclosures off
`solver.getCurve()` (dreal-capd-usage.md §integration path 3-4). The **`MatrixType& derivative`
(monodromy), `HessianType`, and `JetType` overloads are unused**, as is the `SolutionCurve`
out-parameter form.

## Why it might matter
The variational overloads are the lowest-friction route to the monodromy: changing
`timeMap(finalTime, set)` to `timeMap(finalTime, set, derivative)` (with an `IC2OdeSolver` and
a C1 set) yields ∂φ/∂x₀ at the terminal time in the same call — directly feeding an
interval-Newton backward narrowing of the initial-condition box that dReal cannot do from C0
slice values (see `../solvers/C1OdeSolver.md`). Completeness lever; soundness-preserving.
Note `TimeMap` lives in `poincare/`, so the same object is the natural bridge if dReal ever
wants true section-crossing (`PoincareMap`) for terminal-gate handling.

## Source
[TimeMap.h](../../../../CAPD/docs/html/TimeMap_8h.html) ·
[class page](../../../../CAPD/docs/html/classcapd_1_1poincare_1_1TimeMap.html)
