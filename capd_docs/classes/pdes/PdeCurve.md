# PdeCurve

## What it is
The **Taylor-curve data structure** for the PDE solver: "stores a parametric curve together with
first-order derivatives with respect to the initial point" (namespace-page brief). Template
`PdeCurve<SeriesT>`; base class of `PdeSolver`, and the `CurveType`/`SolutionCurve` returned by
`PdeSolver::getCurve()`. It holds, over a time step, the Taylor coefficients of the solution at
the center, of the full set, and of the variational (matrix) blocks, each as series-valued
(tail-bound) coefficients. Derives from **both** `capd::diffAlgebra::CurveInterface<IMatrix>`
and `capd::diffAlgebra::ParametricCurve<IMatrix,SeriesT>` (confirmed `PdeCurve.h`).

## Key API (confirmed in `PdeCurve.h`)
- `PdeCurve(size_type dim, size_type order)`.
- Evaluation over the step: `operator()(h)` → series value at time `h` (sums the Taylor series
  of center + Jacobian·δ + remainder); `valueAtCenter(h)`, `remainder(h)`, `getCenter()`.
- Derivatives: `derivative(h)` / `operator[](h)` → finite Jacobian matrix at `h`;
  `oneStepDerivativeOfNumericalMethod(h)`.
- Order/shape: `getOrder()`, `setOrder()`, `getAllocatedOrder()`, `dimension()`,
  `clearCoefficients()`.
- Coefficient access: `centerCoefficient(i,j)`, `coefficient(i,j)`, `coefficient(i,j,k)`,
  `remainderCoefficient(i,j)`; coefficient arrays via `getCoefficients`,
  `getCoefficientsAtCenter`, `getMatrixCoefficients`, `getRemainderCoefficients`, etc.

## dReal status
**Unused — future direction (dReal has no PDE constraints today).** No `src/` reference.

## Why it might matter
This is the PDE analogue of CAPD's ODE `Curve` — and the ODE `Curve` is precisely where dReal's
2026-06 per-slice tube fix came from (`timeDerivative()`,
[curves-and-jets](../../concepts/curves-and-jets.md)). A dReal PDE contractor would evaluate
`PdeCurve` over time sub-intervals to build a tube exactly as the ODE path evaluates `curve(sub)`
today ([dreal-capd-usage.md](../../dreal-capd-usage.md)). Note: `PdeCurve` has **no**
`timeDerivative` method, and neither of its two bases (`diffAlgebra::CurveInterface<IMatrix>`,
`diffAlgebra::ParametricCurve<IMatrix,SeriesT>`) does either (verified: `PdeCurve.h`,
`diffAlgebra/CurveInterface.h`, `diffAlgebra/ParametricCurve.h`) — `timeDerivative` lives only on
the ODE `diffAlgebra::Curve`/`SolutionCurve`, which `PdeCurve` does **not** derive from. So dReal's centered-in-time
tube-tightening (`centered_curve_range` in `contractor_odes_capd.cc`, which calls
`curve.timeDerivative(sub)`) has **no drop-in PdeCurve analogue**: a PDE tube would have to
recover the mean-value-in-time slice by hand from the `remainder(h)`/`derivative(h)` accessors.

## Source
[classcapd_1_1pdes_1_1PdeCurve.html](../../../../CAPD/docs/html/classcapd_1_1pdes_1_1PdeCurve.html)
(header: `capd/pdes/PdeCurve.h`)
