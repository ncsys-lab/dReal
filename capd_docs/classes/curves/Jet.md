# capd::diffAlgebra::Jet

**What it is.** `Jet<MatrixT, DEGREE>` stores a multivariate truncated power series — the
normalized partial derivatives (Taylor coefficients) of a map up to a given degree. It is the
return/coefficient type for higher-order derivative work: `Map::operator()(jet)` propagates a
jet through a map, and `C2Curve`/`CnCurve` evaluation returns one. `Curve::JetType` is
`Jet<MatrixType, 0>`.

**Key API** (from `$HDR/diffAlgebra/Jet.h`; indexed by `Multiindex`/`Multipointer`):

```cpp
size_type degree() const;  size_type dimension() const;  size_type imageDimension() const; // #vars / #polys
RefVectorType operator()(const Multipointer& mp) const;    // vector of d^{mp} f_i
RefVectorType operator()(const Multiindex&   mi) const;
RefVectorType operator()(void) const;                      // 0-order: the value
operator ImageVectorType() const;   // value (0-order derivatives)
operator MatrixType()     const;    // first-order derivatives as a matrix
operator HessianType()    const;    // second-order derivatives as a hessian
void setMatrix(const MatrixType&);
ImageVectorType operator()(const VectorType&) const;          // evaluate polynomial at a point
MatrixType      derivative(const VectorType& v) const;        // derivative of the polynomial
ScalarType& operator()(size_type i, const Multiindex&);       // raw coefficient access
std::string toString(int minFun=0, int maxFun=-1, ...) const;
```

**dReal status.** Unused. dReal never requests jets, hessians, or degree>0 derivatives from a
curve or map (`dreal-capd-usage.md` — "does NOT use the curve's higher derivatives, hessian,
jet, or eval").

**Why it might matter.** A jet/Hessian-based enclosure (second-order Taylor model in the
initial conditions) could in principle tighten the IC-spread term beyond the linear
`Jphi·deltaX` mean-value form — but only on a C2/Cn solver, at materially higher per-step cost,
and dReal's wrapping is dominated by time-correlation (handled by the C0 time-centered fix),
not by IC-spread nonlinearity. Low expected payoff for the cost.

**Source.** [classcapd_1_1diffAlgebra_1_1Jet.html](../../../../CAPD/docs/html/classcapd_1_1diffAlgebra_1_1Jet.html)
