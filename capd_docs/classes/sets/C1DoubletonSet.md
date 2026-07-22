# C1DoubletonSet

## What it is
A **C1** (position **plus first derivatives w.r.t. initial conditions**) set in doubleton form: `x + C*r0 + B*r` for the position, with the flow's **Jacobian / monodromy matrix** carried alongside as a matrix-doubleton (confirmed `C1DoubletonSet.h` detail: "represents derivatives in a doubleton form as described in the C¹-Lohner algorithm by Piotr Zgliczyński, FoCM 2001"). Representative of the whole variational family (`C1AffineSet`, `C2DoubletonSet`, `CnDoubletonSet`, `C1Rect2Set`, `CnRect2Set`).

## Key API
- Position side identical to `C0DoubletonSet` (`move`, `operator VectorType()`, `affineTransformation`).
- Derivative side: `operator MatrixType()` — cast to the `IMatrix` enclosing the flow Jacobian; `getElement_Cjac(i,j)` and matrix-doubleton accessors.
- `evalAffineFunctional`, `getElement_C(i,j)`, `setToIdentity()` for the derivative frame.
- Construction takes only the doubleton frames — no extra argument beyond the C0 form (constructors `C1DoubletonSet.h:56-61`). It must be **moved** by at least a C1 dynamical system (`C1DynSys`); the logarithmic-norm bound on derivatives is the C1 *solver's* enclosure-step concern (`dynsys/FirstOrderEnclosure.h`), not a set-constructor input.

## dReal status
**Not used.** dReal uses only C0 (value-only) sets; the entire variational layer — the flow Jacobian / monodromy — is never requested (`dreal-capd-usage.md` "Conspicuously NOT used: C1/C2/Cn solvers & variational equations").

## Why it might matter
The Jacobian `∂φ/∂x₀` is exactly the object an **interval-Newton / mean-value backward-narrowing** step needs to sharpen the *initial-condition* box `X₀` from a terminal constraint — dReal currently does backward narrowing with ibex HC4 on the C0 tube only. A C1 set would let the narrowing use the true sensitivity of the flow, potentially much tighter on stiff terminal gates. It costs an n×n matrix integration per step (≈ n× the work) and needs new plumbing in `contractor_odes_capd.cc`, but the cost may be **offset, not just added**: a C1 forward pass narrows `X₀` directly, so it could *retire* today's separate BWD-C0 integration — the decisive comparison is FWD-C1-only vs. FWD-C0 + BWD-C0 (`AUDIT.md` §A′), which is what settles whether C1 is a net cost or a net saving. **Highest-value, highest-effort opportunity in this cluster (AUDIT tier A).**

## Source
[classcapd_1_1dynset_1_1C1DoubletonSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C1DoubletonSet.html)
