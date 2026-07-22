# Class: `capd::dynsys::CnOdeSolver` (= `capd::ICnOdeSolver`) — higher-order variational

## What it is
Rigorous solver that integrates variational equations up to arbitrary degree `n`: the flow
value plus all partial derivatives w.r.t. initial conditions through order `n` (full Taylor
jet of the flow in `x₀`). `ICnOdeSolver` = `CnOdeSolver<IMap>`. Template:
`CnOdeSolver<MapT, StepControlT = IEncFoundStepControl, EnclosurePolicyT = FirstOrderEnclosure, CurveT = CnCurve<…>>`
(`CnOdeSolver.h:37-39`). The rigorous `CnOdeSolver` defaults to **`IEncFoundStepControl`**, unlike
`IOdeSolver`/`IC2OdeSolver`'s `ILastTermsStepControl`; its enclosure policy default is
`FirstOrderEnclosure`, not the `HighOrderEnclosure` C0/C2 use. (The nonrigorous base
`BasicCnOdeSolver` defaults to `DLastTermsStepControl` — `BasicCnOdeSolver.h:32`.)

## Key API (confirmed in `CnOdeSolver` page)
- `operator()(SetType& set)` / `(set, result)` — runtime-checked to integrate C0/C1/C2 sets.
- `encloseCnMap(t, x, xx, JetT& phi, JetT& rem, JetT& enc)` — jet enclosure over the step
  (value + all derivatives to degree `n` + remainders).
- `degree()` — the configured `n`; `setMask(b, e)` / `resetMask()` to compute only selected
  multiindices (avoids paying for unwanted partials).
- `getCurve()` → `CnCurve` with `jet(h)` (and inherited `hessian`, `derivative`).
- `getCoeffNorm(i, degree)`.

## dReal status
**Unused.** Bound by the `ICnOdeSolver` typedef but never referenced in dReal.

## Why it might matter
For dReal's value-only narrowing, `n ≥ 2` jets are overkill — first-order monodromy (see
`C1OdeSolver.md`) captures essentially all the backward-narrowing benefit at far lower cost.
`CnOdeSolver` would only matter if dReal ever needed higher-order Taylor models of the flow
(e.g. tighter wrapping control via 2nd-order terms). The `setMask` mechanism is the relevant
detail: it lets a hypothetical C1-only use of this solver skip the expensive higher partials.
Low priority relative to the C1 monodromy route. The `IEncFoundStepControl` default also means
adopting `ICnOdeSolver` silently changes step-control behavior vs. `IOdeSolver`'s
`ILastTermsStepControl` — worth noting if benchmarked.

## Source
[CnOdeSolver.h](../../../../CAPD/docs/html/CnOdeSolver_8h.html) ·
[class page](../../../../CAPD/docs/html/classcapd_1_1dynsys_1_1CnOdeSolver.html)
