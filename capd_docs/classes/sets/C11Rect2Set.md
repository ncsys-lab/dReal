# C11Rect2Set

## What it is
An alternate **C1 doubleton** set: it derives from `C1DoubletonSet<MatrixT, C11Rect2Policies>`
with `C11Rect2Policies = FactorReorganization<FullQRWithPivoting<>>` — i.e. the **same Rect2
geometry and policy** as `C1Rect2Set`. Both the C0 and C1 parts are stored as `x + C·r0 + B·r`
(confirmed `C11Rect2Set.h`). The distinguishing feature is how the **derivative** (the flow
Jacobian) is *moved*: "derivative of the flow moved via QR decomposition (3rd method)"
(confirmed `C11Rect2.h` / "C^1-Lohner algorithm… the set part — QR decomposition (3-rd
method)"). So same shape as `C1Rect2Set`, different C1-part propagation.

## Key API
- Own override `move(DynSysType&, C11Rect2Set& result)` plus the inherited `C1DoubletonSet`
  move surface (`using BaseSet::move`) (`C11Rect2Set.h:53-54`); `operator VectorType()`
  (position) and `operator MatrixType()` (Jacobian) inherited from `C1DoubletonSet`
  (`C1DoubletonSet.h:86-87`).
- Exposes the full reorganization control suite (`reorganizeIfNeeded`,
  `reorganizeC1IfNeeded`, `onoffReorganization`, …) at both C0 and C1 levels.

## dReal status
**Not used (structural).** Part of the unwired C1 variational family; reachable only after
dReal grows a C1 narrowing path.

## Why it might matter
A secondary tuning choice *within* the C1 step, not a separate capability: if dReal adopts C1
sets and `C1Rect2Set` (the `DefaultC1Set`) proves loose on the Jacobian, `C11Rect2Set`'s
3rd-method QR propagation of the derivative is the alternate to A/B against it. Whether it is
actually tighter or faster is **unverified** — the headers state only that it is a different
propagation method, not which wins.

## Source
[classcapd_1_1dynset_1_1C11Rect2Set.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C11Rect2Set.html)
