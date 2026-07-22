# Curves and Jets — the Taylor-curve representation (centerpiece)

**What it is.** After each adaptive Taylor step, a CAPD `IOdeSolver` exposes the step's
solution as a *parametric curve* `c(h, x_0)` over local time `h ∈ [0, step]`, retrievable
with `solver.getCurve()`. This is the object dReal slices into its tube. For interval
arithmetic the curve is stored (see `BasicCurve`) as a **doubleton**:

    c(h, x_0) = phi(h)  +  Jphi(h)·(x_0 − x̂_0)  +  rem(h)

where `phi` is the Taylor expansion at the *center* `x̂_0` of the initial box,
`Jphi` is the Jacobian of the flow w.r.t. the initial point (the in-step variational
matrix), `(x_0 − x̂_0)` is the initial-condition spread, and `rem` is the rigorous
Taylor remainder. Storing the center and the spread *separately* (rather than one fat
interval) is what fights the wrapping effect — the dependency of `c` on `x_0` is kept linear
and explicit instead of being smeared into an interval each step.

## All evaluation modes of `Curve<...,true>` (the interval curve dReal holds)

Confirmed in `$HDR/diffAlgebra/Curve.h` + `Curve.hpp`. The doxygen page omits the
interval-only methods (`valueAtCenter`, `remainder`, `getCenter`, `oneStepDerivative`) — they
exist only on the `isInterval=true` specialization. Signatures (all take local time `h`):

| Method | Returns | What it computes |
|---|---|---|
| `operator()(h)` | `VectorType` (IVector) | **Tight range** of `c(h,·)`: builds `phi(h) + Jphi(h)·deltaX`, intersects with the direct Horner sum `xx`, adds `rem(h)`. Already a mean-value-in-*space* enclosure. |
| `timeDerivative(h)` | `VectorType` | Enclosure of `d/dt c(h,·)` — same doubleton machinery (`phi' + Jphi'·deltaX + rem'`), accounts for IC spread. Added to dReal's path in the 2026-06 fix. |
| `valueAtCenter(h)` | `VectorType` | `phi(h)` only — the **thin** center trajectory (no IC spread, no remainder). |
| `remainder(h)` | `VectorType` | `rem(h)` only — the Taylor remainder term. |
| `getCenter()` | `VectorType` | `x̂_0`, the center of the initial set (h-independent). |
| `derivative(h)` / `operator[](h)` | `MatrixType` (IMatrix) | The flow **Jacobian** `Jphi(h)·initMatrix` — `∂c/∂x_0`, the in-step variational/monodromy block. |
| `oneStepDerivative(h)` | `MatrixType` | `Jphi(h)` incl. its remainder; `oneStepDerivativeOfNumericalMethod` excludes it. |
| `setInitMatrix(M)` | `void` | Sets the left-multiplied initial matrix for `derivative`. |
| `hessian(h)`, `jet(h)`, `eval(h, jet)` | — | on the C0 curve all three **throw** `std::logic_error` (inherited abstract stubs, `ParametricCurve.h:53-62`); **only meaningful on `C2Curve`/`CnCurve`**, which a C0 `IOdeSolver` does not produce. |

## How dReal uses them, and the lever

dReal's `centered_curve_range` (`contractor_odes_capd.cc:371`) computes, per sub-slice
`sub`: `naive = curve(sub)`, then the **mean-value-in-time** bound
`x(mid) + curve.timeDerivative(sub)·(sub − mid)`, and intersects the two by bound comparison
(no arithmetic → rounding-mode-safe). This is the tube-tightening fix.

Note the *two distinct* mean-value forms: `operator()` already centers in **space** (IC
spread); dReal's wrapper additionally centers in **time** (sub-slice spread). Both are sound
outward enclosures; intersecting them keeps the trajectory enclosed.

**Audit angle.** The space-centered range (`operator()`) is already maximally tight given the
curve. The remaining unused levers are *structural* (the C1 `derivative`/monodromy matrix, see
`variational-equations.md`), not a tighter C0 range — there is no untapped centered/jet form
that beats the current intersection for the C0 value range.

**dReal status.** `operator()` and `timeDerivative` used; `valueAtCenter`/`remainder`/
`getCenter`/`derivative`/`oneStepDerivative` unused; `hessian`/`jet`/`eval` throw on the C0
curve (inherited abstract stubs). See `dreal-capd-usage.md`.

**Source.** [Curve.html](../../../CAPD/docs/html/classcapd_1_1diffAlgebra_1_1Curve.html),
[odesvar_rigorous.html](../../../CAPD/docs/html/odesvar_rigorous.html)
