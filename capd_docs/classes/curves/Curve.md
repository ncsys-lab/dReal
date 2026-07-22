# capd::diffAlgebra::Curve

**What it is.** `Curve<BaseCurveT, isInterval>` adds evaluation methods on top of a
`BaseCurve`'s coefficient storage: given local time `h`, return the value / time-derivative /
Jacobian of the per-step Taylor solution curve. dReal holds this object via
`solver.getCurve()` (the `isInterval=true` specialization). It is the class dReal already
mined `timeDerivative` from for the 2026-06 tube fix.

**Two specializations** (`$HDR/diffAlgebra/Curve.h`): the generic `Curve<...,false>`
(non-interval) and `Curve<...,true>` (interval — dReal's). The interval one carries an extra
`initMatrix` and the interval-only methods listed below (absent from the doxygen page).

## Key API — all public evaluation methods

Signatures confirmed in `$HDR/diffAlgebra/Curve.h` and `Curve.hpp` unless noted.
`ScalarType` = `capd::interval`; `VectorType` = `IVector`; `MatrixType` = `IMatrix`.

```cpp
// present on BOTH specializations:
VectorType timeDerivative(const ScalarType& h) const;   // d/dt of curve, IC-spread-aware
VectorType operator()    (const ScalarType& h) const;   // tight value range (space-centered + ∩ direct)
MatrixType derivative    (const ScalarType& h) const;   // flow Jacobian ∂c/∂x_0
MatrixType operator[]    (const ScalarType& h) const;   // == derivative(h)

// interval (isInterval=true) specialization ONLY — NOT in the doxygen page:
VectorType valueAtCenter (const ScalarType& h) const;   // phi(h): thin center trajectory
VectorType remainder     (const ScalarType& h) const;   // rem(h): Taylor remainder term
VectorType getCenter     () const;                      // x̂_0 (h-independent)
MatrixType oneStepDerivative(const ScalarType& h) const;             // Jphi(h) incl. remainder
MatrixType oneStepDerivativeOfNumericalMethod(const ScalarType& h) const; // Jphi(h) excl. remainder
void       setInitMatrix (const MatrixType& M);

// inherited from ParametricCurve / CurveInterface:
virtual HessianType hessian(const ScalarType&) const;   // throws std::logic_error on a C0 curve
virtual JetType     jet    (const ScalarType&) const;   // throws std::logic_error on a C0 curve
virtual void        eval   (ScalarType, JetType&) const;// throws std::logic_error on a C0 curve
Real getLeftDomain() const;  Real getRightDomain() const;  void setDomain(Real, Real);
```

`operator()` (interval, `Curve.hpp:59`) is *already* the mean-value-in-space form: it forms
`phi + jacPhi·deltaX`, **intersects** it with the direct Horner sum, and adds `rem`. So the C0
value range dReal reads is as tight as this curve allows. `hessian`/`jet`/`eval` are inherited
stubs that **throw** `std::logic_error` (ParametricCurve.h:53-62) unless the object is actually a
`C2Curve`/`CnCurve` (a C0 `IOdeSolver` does not build those).

**dReal status.** `operator()` + `timeDerivative` used (`contractor_odes_capd.cc:374,377`).
`derivative`/`valueAtCenter`/`remainder`/`getCenter`/`oneStepDerivative*`/`setInitMatrix`
unused; `hessian`/`jet`/`eval` throw on a C0 curve. See `dreal-capd-usage.md`.

**Why it might matter.** `derivative(h)` is the in-step flow Jacobian — the C1 sensitivity an
interval-Newton backward step would use, but it is only populated meaningfully when the solver
integrates variational equations (C1 set); on a C0 set its value is the trivial/identity-driven
block. The real C1 lever is `SolutionCurve::derivative` on a C1 set (see
`../../concepts/variational-equations.md`). No unused method gives a *tighter C0 value range*
than the current `operator()`.

**Source.** [classcapd_1_1diffAlgebra_1_1Curve.html](../../../../CAPD/docs/html/classcapd_1_1diffAlgebra_1_1Curve.html)
