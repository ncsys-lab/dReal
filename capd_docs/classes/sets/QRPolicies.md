# QR & reorganization policies — the frame-maintenance menu

## What it is
A doubleton/tripleton set stores the box in a **moving frame**: `x + C·r0 + B·r`
(`C·r0` carries the initial set rigidly; `B·r` the accumulated error). Two
*composable* template policies govern that frame — and together they ARE the
wrapping-control mechanism that distinguishes `Rect2` from `Pped2` from `Intv2`.
The set-name suffix is just a **named bundle** of these two choices
(`dynset/typedefs.h:23-36`):

| Bundle (typedef) | = QR policy | + reorganization |
|---|---|---|
| `C0Rect2Policies` | `FullQRWithPivoting` | `FactorReorganization` |
| `C0Pped2Policies` | `InverseQRPolicy` | `FactorReorganization` |
| `C0Intv2Policies` | (none / plain) | `FactorReorganization<>` |
| `C0PpedPolicies` / `C0RectPolicies` | `InverseQR` / `FullQR` | (none — affine) |
| `C1Pped2Policies` / `C2Pped2Policies` | `InverseQRPolicy` | `QRReorganization` |
| `C1Rect2Policies` / `C2Rect2Policies` | `FullQRWithPivoting` | `FactorReorganization` |

So `C0Rect2Set` = doubleton with **FullQR-pivoting orthogonalization** of the error
frame, reorganized **by size factor**. These policies are an **independent knob**
from the set *geometry* — but dReal never varies them (it's hard-wired to the
`Rect2` bundle), so the whole right-hand menu below is unexplored.

## Menu 1 — QR orthogonalization policy (`dynset/QRPolicy.h`)
How the columns of the error frame `B` are re-orthonormalized each reorganization.

| Policy | What it does (header-grounded) | Strength | Weakness |
|---|---|---|---|
| `FullQRWithPivoting` | full QR with column pivoting (the `Rect2` choice) | robust general default; handles different-magnitude eigenvalues (`dynset_module.html`: "best choice… especially when the eigenvalues are of a different magnitude") | "Overestimation is introduced due to orthogonalization" (`dynset_module.html`) — pays on every step |
| `InverseQRPolicy` | inverse-QR frame (the `Pped`/`Pped2` choice) | better near an elliptic fixed point / equal-magnitude eigenvalues (`dynset_module.html`) | "in general it blow up quickly… coordinates system becomes almost singular" (`dynset_module.html`) |
| `SelectiveQRWithPivoting` | orthogonalizes a vector **only** when it is closer to parallel than a tolerance: "if vectors are closer to be parallel more than given tolerance level one of them will be orthogonalized" (`QRPolicy.h:172`) | **cheaper** than full QR while giving the *same* enclosure on a well-conditioned frame | needs a tolerance; no gain (and possible cost) on ill-conditioned frames |
| `PartialQRWithPivoting<N>` | QR on only the first `N` columns (`QRPolicy.h:92`) | bounds the QR cost on high-dim sets | only partial wrapping control |

(plus `IdQRPolicy`/`DefaultPolicy`/`BasePolicy` = base plumbing, not user choices.)

## Menu 2 — reorganization policy (`dynset/reorganization/`)
**When/how** the accumulated-error term `r` is folded back into `r0` to stop `B·r`
from drifting. All `Factor*` variants trigger when `size(r) > factor · size(r0)`.

| Policy | What it does (header-grounded) | Note |
|---|---|---|
| `FactorReorganization` | "reorganizes a doubleton set when `size(r) > factor·size(r0)`" (`FactorReorganization.h:26-30`) | the standard trigger used by every wired set |
| `NoReorganization` | "Reorganization that does nothing" (`NoReorganization.h:20`) | cheapest; frame never reset → unbounded wrapping |
| `CanonicalReorganization` | "set C and B to Identity and put everything into r0" (`CanonicalReorganization.h`) | collapses to a plain box on trigger — a hard reset |
| `QRReorganization` | "During reorganization we orthogonalize B" (`QRReorganization.h:26`) | used by the `Pped2` C1/C2 bundles |
| `CoordWiseReorganization` | `B_i` of the biggest `r_i` replaces the closest `C_j`; `r_i` moved to `r0` (`CoordWiseReorganization.h:26`) | coordinate-wise, selective fold-back |
| `SwapReorganization` | triggers when `size(r) > factor · size(B⁻¹·C·r0)` — "bigger than r0 but in coordinate system of r" (`SwapReorganization.h`) | a frame-relative trigger (Wilczak) |
| `InvBByCFactorReorganization` | "Factor based reorganization **for C1 sets**" (`InvBByCFactorReorganization.h`) | the C1-part analogue |

(`FactorPolicy` = base holding the `factor`; the **factor itself is a hidden scalar
knob** — `dynset_module.html` says reorganization "can improve result a lot" for small
initial sets [the doc's advice is to use reorganizing sets, which the pinned build
already does; that the *factor value* is a tunable lever is my inference], and it is
not exposed by dReal.)

## dReal status
**Hard-wired, unexposed.** All three `--ode-c0-set` sets bind
`C0Rect2Policies = FactorReorganization<FullQRWithPivoting<>>` (`typedefs.h:24`).
The QR policy, the reorganization policy, and the reorganization `factor` are all
fixed — none is a flag.

## Why it might matter (the leverage)
- **`SelectiveQRWithPivoting`** is a near-free speed win: same enclosure as
  `FullQR` on a well-conditioned frame, less QR work (`QRPolicy.h:172`). It is the
  policy-level analogue of "don't pay for orthogonalization you don't need."
- **The reorganization `factor`** is a plausible tightness/speed dial, currently a
  hidden constant. `dynset_module.html` says reorganization "can improve result a lot"
  for small initial sets — exactly dReal's regime (narrow ICP boxes) — though the doc
  recommends reorganizing *sets*, not a factor value (the factor lever is my inference).
- Exposing a `--ode-set-policy` (QR × reorg × factor) would make this whole menu
  reachable for per-family tuning, the same way `--ode-c0-set` exposes geometry.

This is **AUDIT.md B5**, expanded. All levers here are completeness/speed only —
a different frame policy changes tube width, never soundness.

## Header enumeration (ground truth — re-auditable)

This node is complete iff it covers both menus below.

QR policies — `grep -nE '^class [A-Za-z]+ : public BasePolicy' gcc_build/capd-install/include/capd/dynset/QRPolicy.h`:
```cpp
class InverseQRPolicy : public BasePolicy
class FullQRWithPivoting : public BasePolicy
class PartialQRWithPivoting<int N> : public BasePolicy
class SelectiveQRWithPivoting : public BasePolicy
// (IdQRPolicy / DefaultPolicy / BasePolicy = base plumbing, not user choices)
```

Reorganization policies — `ls gcc_build/capd-install/include/capd/dynset/reorganization/`:
```
CanonicalReorganization.h   CoordWiseReorganization.h   FactorPolicy.h
FactorReorganization.h      InvBByCFactorReorganization.h   NoReorganization.h
QRReorganization.h          SwapReorganization.h
```
(`FactorPolicy.h` = the base holding the `factor` scalar; the other 7 are the
selectable reorganization strategies tabulated above.)

The named bundles the sets actually use —
`grep -nE 'Policies;' gcc_build/capd-install/include/capd/dynset/typedefs.h` (lines 23–36):
```cpp
C0Rect2Policies = FactorReorganization<FullQRWithPivoting<>>   // ← every wired --ode-c0-set
C0Pped2Policies = FactorReorganization<InverseQRPolicy<>>
C0Intv2Policies = FactorReorganization<>
C1Rect2Policies = FactorReorganization<FullQRWithPivoting<>>
C1Pped2Policies = QRReorganization<InverseQRPolicy<>>     // (C2 analogues identical)
```

## Source
[QRPolicy.h](../../../../CAPD/docs/html/QRPolicy_8h.html) ·
[dynset_reorganizedset.html](../../../../CAPD/docs/html/dynset_reorganizedset.html) ·
[dynset_module.html](../../../../CAPD/docs/html/dynset_module.html) ·
header catalog `dynset/typedefs.h`, `dynset/reorganization/*.h`
