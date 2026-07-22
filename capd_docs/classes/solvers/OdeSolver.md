# Class: `capd::dynsys::OdeSolver` (= `capd::IOdeSolver`)

## What it is
The C0 + first-order-variational rigorous Taylor/HO ODE stepper. `IOdeSolver` =
`OdeSolver<IMap>`. This is **the** solver dReal constructs per flow. Template:
`OdeSolver<MapT, StepControlPolicyT = ILastTermsStepControl, EnclosurePolicy = HighOrderEnclosure, CurveT>`.

## Key API (confirmed in `OdeSolver.h` page)
Construction / config:
- `OdeSolver(VectorFieldType& vField, size_type order, const StepControlPolicy& = StepControlPolicy())`
- `setOrder(order)`, `setStep(h)` (fixes step, **turns off** step control),
  `adjustTimeStep(h)` (sets step, keeps control settings).
- Step control (inherited `StepControlInterface`): `setAbsoluteTolerance`,
  `setRelativeTolerance`, `setMaxStep`, `getAbsoluteTolerance`/…, `turn{On,Off}StepControl`,
  `onOffStepControl(bool)`, `getStepControl`, `setStepControl(const StepControlType&)`.

Stepping / enclosure:
- `operator()(SetType& set)` and `operator()(SetType& set, SetType& result)` — move a C0/C1
  set one step (runtime-checked: this solver integrates C0 and C1 sets, not nonrigorous jets).
- `enclosure(const ScalarType& t, const VectorType& x)` → rigorous bound over the step, **throws**
  if existence cannot be validated.
- `encloseC0Map(t, x0, x, o_phi, o_rem, o_enc, o_jacPhi)` — simultaneously returns the scheme
  value, remainder, trajectory enclosure, **and the Jacobian `o_jacPhi`**.
- `encloseC1Map(t, x0, x, …, o_jacPhi, o_jacRem, o_jacEnc)` — first-order variational enclosure.
- `Phi(t, iv)` (scheme value), `JacPhi(t, iv)` (scheme derivative), `Remainder(t, iv, o_enc)`.
- Vector overloads returning the **monodromy/derivative**:
  `operator()(VectorType, MatrixType& o_resultDerivative)` and the `(t, v, derivative, o_…)`
  forms — image of `v` plus ∂(flow)/∂(init cond).

Curve / dense output:
- `getCurve()` → `SolutionCurve`; on the curve: `operator()(h)`, `timeDerivative(h)`,
  `derivative(h)`, `operator[](h)`, `hessian(h)`, `jet(h)`, `getLeftDomain`/`getRightDomain`.
- `getStep()`, `getCoeffNorm(r, degree)`, coefficient/remainder builders
  (`computeTaylorCoefficients`, `computeRemainderCoefficients`, `computeImplicitCoefficients`).
- Jet mask: `setMask`, `addMultiindexToMask`, `resetMask`.

## dReal status
**Used (C0 path).** dReal constructs `IOdeSolver(map, order)`, sets tolerances + maxStep, then
drives it through `ITimeMap`; per step it reads `getCurve()` + `getStep()` and evaluates
`curve(sub)` / `curve.timeDerivative(sub)` (dreal-capd-usage.md §integration path 2-4). The
Jacobian outputs (`encloseC0Map`'s `o_jacPhi`, the `MatrixType& derivative` overloads,
`JacPhi`, `encloseC1Map`) are **not** used.

## Why it might matter
`encloseC0Map` returns `o_jacPhi` and the `operator()(v, MatrixType& der)` overloads return ∂φ/∂x₀
alongside the value dReal consumes — a candidate monodromy for interval-Newton backward narrowing,
which dReal does not exploit at all (it hands C0 values to ibex HC4). But the header settles the
rigor question against the C0 route: `encloseC0Map`'s `o_phi`/`o_jacPhi` are the *numerical-scheme*
value and its Jacobian (`OdeSolver.h:67,70` — "phi is a numerical method", "Dphi(x)"); the rigorous
flow enclosure is `o_phi + o_rem`, and the rigorous flow Jacobian additionally needs `o_jacRem`,
which only `encloseC1Map` supplies (`OdeSolver.h:74-84`). So the unambiguous rigorous monodromy is
the C1 route (`encloseC1Map`, or `IC2OdeSolver`), not C0 `o_jacPhi` alone. Backward narrowing is a
COMPLETENESS lever (sharper enclosures never cause a false `unsat`).

## Source
[OdeSolver.h](../../../../CAPD/docs/html/OdeSolver_8h.html) ·
[class page](../../../../CAPD/docs/html/classcapd_1_1dynsys_1_1OdeSolver.html)
