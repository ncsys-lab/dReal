# CnRect2Set

## What it is
The **arbitrary-order** set: stores **all derivatives up to a given order `n`** (a full jet
of the flow w.r.t. initial conditions) in doubleton form, propagated with reorganization by
the QR method (confirmed `CnRect2Set.h` / "Set that stores all derivatives to given order in
doubleton form with reorganization moved by QR decomposition method"). The jet order is set at
construction by a runtime `degree` argument (`CnRect2Set.h:42-46`); the `DEGREE` template
parameter is a compile-time bound (default 0). The C0/C1/C2 doubletons are the low-order special cases of
this same jet machinery; the multi-matrix sibling is `CnMultiMatrixRect2Set` =
`CnDoubletonSet` (derivatives w.r.t. a multiindex `α` as doubletons, `CnDoubletonSet.h`).

## Key API
- Constructor `<MatrixT, Policies, DEGREE>` — the jet degree fixes how many derivative
  orders are carried.
- `move(DynSysT& cndynsys)` / `move(cndynsys, result)` — needs a **Cn** dynamical system.
- Jet accessors: value, derivative, "maximal order of partial derivative stored in the jet",
  "Taylor coefficients corresponding to multipointer `(1/mp!)·d^{mp}f_i`", and "an enclosure
  for first order variational equations for last performed step" (confirmed class doc).
- `evalAffineFunctional(gradient, x0)` — mean-value functional over the doubleton frames,
  intersected with the direct evaluation (`CnRect2Set.h:64`). (No `evalAt` on this class — that
  form lives on the C0/C1/C2 doubletons.)

## dReal status
**Not used (structural).** dReal computes only C0 tubes; no jet of any order is requested.

## Why it might matter
The higher-jet story's top: a Cn set yields a Taylor expansion of the flow map in the
initial conditions, which is what a **high-order interval-Newton** or normal-form argument
would consume. For dReal's value-then-narrow loop the marginal value drops fast past C1 —
the Jacobian (C1) already gives the sensitivity a first-order backward narrowing needs, and
each extra order multiplies the per-step jet cost. Relevant as the general frame the whole
doubleton family specializes, not as a near-term wiring target.

## Source
[classcapd_1_1dynset_1_1CnRect2Set.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1CnRect2Set.html)
