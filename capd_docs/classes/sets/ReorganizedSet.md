# ReorganizedSet (and the reorganization policies)

## What it is
Reorganization periodically **rebuilds the doubleton representation** before its coordinate frames degrade into near-singularity — the second wrapping defense after per-step QR orthogonalization. It triggers when the accumulated-error term outgrows the initial-size term (the `FactorReorganization` rule, below).

**Doc↔pinned-build drift (source-fidelity note).** The CAPD **6.0.0** docs model this as a standalone template adapter `ReorganizedSet<SetT, Reorg>` that "after each set's move calls `reorganizeIfNeeded(set)`" (`dynset_reorganizedset.html`), with facade typedefs `C0Rect2ReorganizedSet` / `C0Rect2RSet` and the recommendation to use the R-suffixed variants. **None of `ReorganizedSet`, `C0Rect2ReorganizedSet`, `C0Rect2Reorganization`, or `C0Rect2RSet` exists in the pinned 6.1.0 headers** (grep of `capd-install/include/capd/` is empty). In the pinned build the adapter was **folded into the set's `Policies` template parameter**: `C0DoubletonSet : public Policies` and its `move` ends with `this->Policies::reorganizeIfNeeded(result)` (`C0DoubletonSet.hpp:135`). So `C0Rect2Set` **already reorganizes** on the factor trigger — there is no separate reorganizing variant to opt into, and trying to wire `C0Rect2RSet` per the 6.0.0 docs will not compile.

## The reorganization policies (`dynset/reorganization/`)
- **`FactorReorganization`** — reorganizes when `size(r) > factor · size(r0)` (confirmed `FactorReorganization.h:26-27`). This is the policy baked into dReal's `C0Rect2Policies`.
- **`CanonicalReorganization`** — set `C` and `B` to identity, fold everything into `r0` (confirmed group page).
- **`CoordWiseReorganization`** — the `B`-column for the largest `r` coordinate replaces the closest `C`-column; that error is promoted into `r0`.
- **`SwapReorganization`** — reorganize when `r` is larger than `B⁻¹·C·r0` times a factor (i.e. error frame has overtaken the initial-size frame).
- **`QRReorganization`** — re-orthogonalize `B`.
- **`NoReorganization`** — no-op.

## dReal status
**Used implicitly, not configurable.** dReal does not name `ReorganizedSet`, but its `C0Rect2Policies = FactorReorganization<FullQRWithPivoting<>>` (`typedefs.h:24`) means **every** wired set already reorganizes on the factor trigger. The factor value and the policy choice are **not exposed** by any `--ode-*` flag.

## Why it might matter
The `R`-suffixed sets the docs recommend for small initial sets don't exist in the pinned build (above), but reorganization is **already on** in every wired set — so the live lever is the reorganization *factor*, a hidden compile-time constant (`FactorPolicy.h`). The dynset narrative says small initial sets — exactly dReal's regime (narrow ICP boxes) — get "reorganization can improve result a lot" (`dynset_module.html`); the factor controls how often it fires (smaller ⇒ tighter frames, more cost), so it is a plausible tightness/speed dial **[inference — the doc recommends reorganizing sets, not a specific factor value]**. Untested on the ODE benchmark families; exposing it (a `--ode-set-*` flag) is a low-risk sweep. Completeness/speed only — frame reorganization never moves soundness.

## Source
[dynset_reorganizedset.html](../../../../CAPD/docs/html/dynset_reorganizedset.html) · [dynset_module.html](../../../../CAPD/docs/html/dynset_module.html) · [group__dynset.html](../../../../CAPD/docs/html/group__dynset.html)
