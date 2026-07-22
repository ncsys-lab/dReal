# capd::diffAlgebra::BasicCurve

**What it is.** `BasicCurve<MatrixT>` is the raw coefficient *storage* for a parametric
Taylor curve `c(t,x) = c(t,x₀) + (∂c/∂x)(x−x₀) + smallRemainder(t,x)` (its docstring; x₀ is
the reference/center point).
`Curve` (the evaluation class dReal uses) derives from it. Not used directly by dReal — it is
the backing store behind `solver.getCurve()`.

**Key API** (from `$HDR/diffAlgebra/BasicCurve.h`; getters/iterators for each coefficient
family). Three scalar coefficient stores plus two matrix stores:

```cpp
size_type getOrder() const;  void setOrder(size_type);  size_type dimension() const;
void clearCoefficients();

// scalar coefficients (d = component, j = Taylor power):
const ScalarType& centerCoefficient(size_type d, size_type j) const;     // phi center expansion
const ScalarType& coefficient      (size_type d, size_type j) const;      // interval-valued
const ScalarType& remainderCoefficient(size_type d, size_type j) const;   // Taylor remainder
// matrix (variational) coefficients (d,k = entry, j = power):
const ScalarType& coefficient      (size_type d, size_type k, size_type j) const;  // ∂c/∂x_0 block
const ScalarType& remainderCoefficient(size_type d, size_type k, size_type j) const;

// bulk accessors + begin*/end* iterators for each family:
const VectorType* getCoefficientsAtCenter() const;  // ... getCoefficients, getRemainderCoefficients
const MatrixType* getMatrixCoefficients() const;    // ... getMatrixRemainderCoefficients
```

(The `setInitMatrix`/`setInitHessian`/`setInitJet` methods are NOT here — they are inherited
from `CurveInterface` as throwing stubs and only implemented in `Curve`/`C2Curve`/`CnCurve`.)

The separate **center / interval / remainder / matrix** stores are exactly the doubleton
decomposition the centered evaluation in `Curve::operator()` reads from (`centerCoefficient`,
`coefficient`, `remainderCoefficient` appear verbatim in `Curve.hpp`).

**dReal status.** Unused directly (storage layer beneath the `Curve` dReal evaluates). See
`dreal-capd-usage.md`.

**Why it might matter.** Direct coefficient access would only be relevant for a hand-rolled
custom enclosure form; the public `Curve` evaluation methods already expose the useful
combinations, so there is no tightness lever here that the higher-level API doesn't cover.

**Source.** [classcapd_1_1diffAlgebra_1_1BasicCurve.html](../../../../CAPD/docs/html/classcapd_1_1diffAlgebra_1_1BasicCurve.html)
