# Module: dynset

## What it is
The `capd::dynset` module provides **set representations** (the geometric objects an ODE solver propagates) plus the algorithms that **move** them one solver step at a time. The choice of set type is CAPD's main lever against the *wrapping effect* — the runaway overestimation that accrues when a deformed set is re-enclosed in axis-aligned boxes step after step.

A set is named `[Class]Method[2][Details]Set` (per `dynset_module.html`):
- **Class** = `C0/C1/C2/Cn` — which derivatives w.r.t. initial conditions it stores. `C0` = position only (all three dReal sets are C0).
- **Method** = how the error coordinate frame is maintained: `Rect` (rectangular/orthogonalized frame — best general ODE choice, "overestimation introduced due to orthogonalization"), `Pped` (parallelepiped — best when all eigenvalues are similar magnitude / under rotation), `Intv` (errors in a plain interval vector — fastest but "prone to wrapping").
- **`2`** = doubleton storage `x + C*r0 + B*r` (center `x`; initial size `r0` in frame `C`; accumulated errors `r` in frame `B`).
- trailing **`R`** = built-in reorganization.

## Key classes (the C0 set zoo)
- `C0AffineSet<MatrixT, Policies>` — `x + B*r` (single frame). Base for `C0RectSet`, `C0PpedSet`.
- `C0DoubletonSet<MatrixT, Policies>` — `x + C*r0 + B*r`. **dReal's `C0Rect2Set` is this** with `C0Rect2Policies`.
- `C0TripletonSet<MatrixT, Policies>` — `x + C*r0 + intersection(B*r, Q*q)` — splits the error term across two frames and intersects. CAPD's own `DefaultC0Set`.
- `C0HOSet<BaseSetT>` — Hermite–Obreshkov higher-order wrapper; intersects a Taylor and an HO enclosure. **dReal's `C0HORect2Set` = `C0HOSet<C0Rect2Set>`**.
- `C0HODoubletonSet`, `C0HOTripletonSet` — HO variants directly over doubleton/tripleton.
- C1+: `C1DoubletonSet`, `C1AffineSet`, `C2DoubletonSet`, `CnDoubletonSet`, `CnRect2Set` — also carry the flow's Jacobian/higher derivatives (variational). **None used by dReal.**

## Policies — the wrapping-fight machinery (`QRPolicy.h`, `reorganization/`)
A set's `Policies` template parameter bundles a **QR orthogonalization policy** and a **reorganization policy**:
- QR: `FullQRWithPivoting`, `PartialQRWithPivoting<N>`, `SelectiveQRWithPivoting` ("orthogonalize only if vectors are close to parallel"), `InverseQRPolicy`.
- Reorganization: `FactorReorganization` ("reorganizes when size(r) > factor·size(r0)"), `CanonicalReorganization` (reset C,B to identity, fold all into r0), `CoordWiseReorganization`, `SwapReorganization`, `NoReorganization`.

dReal's `C0Rect2Policies = FactorReorganization<FullQRWithPivoting<>>` (confirmed in `dynset/typedefs.h:24`).

Reorganization is the set's own `Policies` **base**, not a wrapper: each C0 set inherits `public Policies` and reorganizes through it at the end of `move()` (the doubleton via `this->Policies::reorganizeIfNeeded(result)`, `C0DoubletonSet.hpp:135`; the tripleton via the base's `isReorganizationNeeded`/`reorganize`, `C0TripletonSet.hpp:111`). The extracted Doxygen is CAPD 6.0.0 and still shows a separate `ReorganizedSet<SetT,Reorg>` wrapper template — that class is **gone** in the pinned 6.1.0 headers (folded into the set), so don't reach for it.

## dReal status
dReal wires **three C0 sets**, selectable via `--ode-c0-set`: `C0Rect2Set` (default), `C0HORect2Set`, `C0TripletonSet` (`dreal-capd-usage.md`). All three share `C0Rect2Policies` — i.e. the QR/reorganization policy is **fixed**, not exposed. No C1/C2/Cn set is used.

## Why it might matter
This module *is* the tightness lever. The three wired sets span the doubleton/tripleton/HO axis but pin one policy; the unexposed knobs (Pped method, alternate QR/reorg policies, C1 variational sets for sharper backward narrowing) are the audit's main opportunities.

## Source
[group__dynset.html](../../../CAPD/docs/html/group__dynset.html) · [dynset_module.html](../../../CAPD/docs/html/dynset_module.html) · [dynset_reorganizedset.html](../../../CAPD/docs/html/dynset_reorganizedset.html)
