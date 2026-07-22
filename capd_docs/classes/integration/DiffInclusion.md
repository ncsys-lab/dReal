# DiffInclusion (`capd::diffIncl::DiffInclusion<MapT, DynSysT>`)

## What it is

Abstract base for rigorous integration of differential inclusions ẋ ∈ F(x). `MapT` is the
set-valued RHS (a `MultiMap`); `DynSysT` is the underlying numerical ODE integrator (default
`capd::dynsys::OdeSolver`). Concrete subclasses `DiffInclusionLN` (Lohner / log-norm) and
`DiffInclusionCW` (component-wise) implement the perturbation-bounding step.

## Key API (from `DiffInclusion.h` + extracted class page)

```cpp
DiffInclusion(MultiMapType& diffInclusion, int order, NormType const& norm);

virtual VectorType enclosure(const ScalarType& t, const VectorType& x);            // one-step rough enclosure
virtual VectorType diffInclusionEnclosure(const ScalarType& t, const VectorType& x);
virtual VectorType dynamicalSystemEnclosure(const ScalarType& t, const VectorType& x); // selected ODE only

void setOrder(int);  void setStep(const ScalarType&);
void setAbsoluteTolerance(double);  void setRelativeTolerance(double);
void setMaxStep(ScalarType);  void turnOnStepControl();  // step control like IOdeSolver
DynSysType& getDynamicalSystem();   // the numerical integrator inside

// stepping is on the subclass:
void DiffInclusionCW::operator()(SetType& set);             // advance one step
void DiffInclusionCW::operator()(SetType& set, SetType& result);
```

The RHS `MultiMap<FMapT,GMapT>` holds `f` (selection) + `g` (perturbation) with
`operator()(X) = f(X) + g(X)`, modeling `f(x,e)=f(x)+g(x,ε)`, ε an interval set, g(x,e0)=0
(verified `MultiMap.h`). State carried in `InclRect2Set` (`C0DoubletonSet` subclass; ctors take
center x and the doubleton shape C, r0 — `InclRect2Set.h:47-50`). Tolerance/step-control surface mirrors the `IOdeSolver` dReal
already configures.

## dReal status

**Unused.** dReal treats uncertain flow params as CAPD parameters over a box
(`dreal-capd-usage.md` §1, and the "NOT used" list).

## Why it might matter

A candidate tighter model for bounded-uncertainty ODEs (see `concepts/diff-inclusions.md`):
the LN/CW methods bound perturbation accumulation rigorously instead of carrying a wide
constant-interval parameter through every Taylor coefficient (wrapping). **Constraint:** the
uncertainty must be recast as an additive perturbation g(x,ε) vanishing at nominal — a
per-constraint construction, not automatic from "parameter ∈ box". Integration effort is
moderate-to-high (new solver type + `InclRect2Set` + perturbation-map builder) and the payoff is
**unmeasured** against dReal's benchmarks.

## Source

[`../../../../CAPD/docs/html/classcapd_1_1diffIncl_1_1DiffInclusion.html`](../../../../CAPD/docs/html/classcapd_1_1diffIncl_1_1DiffInclusion.html)
