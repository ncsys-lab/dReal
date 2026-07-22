# Variational equations — the flow Jacobian (monodromy)

**What it is.** Alongside the trajectory `x(t)`, CAPD can rigorously integrate the
*variational equation* — the derivative of the flow w.r.t. the initial condition,
`V(t) = ∂φ_t(x_0)/∂x_0` (the monodromy / fundamental-solution matrix). This is a C1
computation: instead of a C0 set type, you hand `timeMap` a **C1 set**
(`C1Rect2Set` / `C1HORect2Set`, doubleton representations of both the point and the matrix),
and after integration extract the matrix:

    C1Rect2Set set(initialCondition, initialTime);   // identity init for V
    IVector y = timeMap(finalTime, set);
    IMatrix monodromy = (IMatrix) set;                // V(finalTime)

**The solver is unchanged — only the set is.** `IOdeSolver` (`= OdeSolver<IMap>`, a
`C1DynSys`, `OdeSolver.h:39`) — the exact solver dReal already constructs — integrates the
first-order variational equation itself; the monodromy is a **first-order** quantity, so
switching a flow to it costs only a C0→C1 *set-type* change, no solver upgrade. CAPD's own
worked example (`odesvar_rigorous.html`) is literally `IOdeSolver` + `ITimeMap` +
`C1Rect2Set`; `IC2OdeSolver`/`ICnOdeSolver` enter only for 2nd-order Hessians / higher jets.
**Two extraction routes:** the set-based `(IMatrix)set` above carries the Jacobian as a
**doubleton** (`(MatrixType)originalSet`, `TimeMap_template.h:114` — wrapping-robust); the
convenience overload `timeMap(t, v, D)` with a plain `IVector v` threads a raw `C1TimeJet`
(`TimeMap.hpp:113` — no manual set, but the value part is an un-split box, looser over long
horizons). Prefer the C1 set for tightness.

**Functional form.** With a `SolutionCurve`, the monodromy is available at any intermediate
time: `timeMap(t, set, solution); solution.derivative(t)` returns `V(t)` as an `IMatrix`
(confirmed `$HDR/diffAlgebra/SolutionCurve.h:185,287`). `solution(t)` / `solution.timeDerivative(t)`
give value and time-derivative — the same eval API as a single-step `Curve`, chained over steps.

**Cost.** Step control then bounds error for *both* the main and variational equations
(documented in `odesvar_rigorous.html`), so the step sizes shrink and each step does
`O(dim²)` more coefficient work than C0 — materially more expensive than the current C0 path.

**Why it might matter to dReal.** dReal narrows the terminal box forward and the initial box
backward using only C0 enclosures. `V(t)` is exactly the sensitivity matrix an
**interval-Newton / mean-value backward step** needs: given a tight terminal constraint, the
monodromy maps terminal slack back onto the initial box `X_0` far more sharply than re-running
C0 backward integration. This is the single biggest unused capability for backward (X₀)
narrowing — but it requires switching the relevant flows to a C1 set type and feeding the
matrix into a new contraction step on dReal's side (ibex HC4 today consumes only the C0 slice
intervals).

**dReal status.** Unused. dReal uses only C0 sets (`C0Rect2Set`/`C0HORect2Set`/`C0TripletonSet`).
See `dreal-capd-usage.md`.

**Source.** [odesvar_rigorous.html](../../../CAPD/docs/html/odesvar_rigorous.html)
