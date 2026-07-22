# Chapter: Contractors (the audit goldmine)

Source: [`contractor.rst.txt`](../../../ibex-docs/_sources/contractor.rst.txt) ·
upstream HTML `contractor.html`. **This is the richest chapter for dReal** — it
defines the contractor catalog and the composition operators ("contractor
programming", Chabert & Jaulin 2009).

> **dReal status at a glance:** dReal uses `CtcFwdBwd` (HC4Revise) always, plus —
> opt-in, default off — three IBEX add-ons now **shipped**: `CtcAcid`/`Ctc3BCid`
> shaving (`--acid`/`--3bcid`, `contractor_ibex_acid.{cc,h}`), `CtcPolytopeHull`
> (`--polytope`, LP path **live** since IBEX builds `-DLP_LIB=soplex` — vendored
> SoPlex 4.0.2), and `ibex::CtcForAll` as a ∃∀ pre-pruner (`--forall-pre-prune`,
> `contractor_ibex_forall.{cc,h}`). It hand-rolls the composition/fixpoint layer
> itself and does **not** use `CtcNewton`/`CtcInverse` (the remaining Tier-D
> leverage). See [`../AUDIT.md`](../AUDIT.md),
> [`../dreal-ibex-usage.md`](../dreal-ibex-usage.md).

## The `Ctc` interface

```cpp
class Ctc {
  Ctc(int nb_var);
  virtual void contract(IntervalVector& box) = 0;   // in-place; the only must-implement
  BitSet* input;   // vars that potentially impact the contractor (NULL = unspecified)
  BitSet* output;  // vars potentially impacted — drives propagation efficiency
};
```
A contractor is a pure function `C: IR^n → IR^n` with **contraction** (`C([x])⊆[x]`)
and **no lost solutions**. The `input`/`output` bitsets (unset by default) let
`CtcPropag` skip contractors that can't fire — relevant on sparse systems.

## Atomic / numerical contractors

| Class | What | dReal | Header |
|---|---|---|---|
| **`CtcFwdBwd`** (HC4Revise) | forward-backward on one constraint `f(x)∈[y]`; linear-time, optimal when each var occurs once | **used** (via `Function::backward`) | [hdr](../../../ibex-fork/src/contractor/ibex_CtcFwdBwd.h) · [node](../classes/contractors/CtcFwdBwd.md) |
| **`CtcHC4`** | `CtcPropag` over the per-constraint fwd-bwd contractors of a `System` | no (dReal's own loop) | [hdr](../../../ibex-fork/src/contractor/ibex_CtcHC4.h) |
| **`CtcNewton`** | interval-Newton (Hansen-Sengupta); contracts near a solution of a square system; gated to narrow boxes (`ceil`) | **no** → audit D | [hdr](../../../ibex-fork/src/contractor/ibex_CtcNewton.h) · [node](../classes/contractors/CtcNewton.md) |
| **`Ctc3BCid`** | 3B shaving + CID constructive disjunction; strong, fixed params | **used** — `--3bcid` (`contractor_ibex_acid`) | [hdr](../../../ibex-fork/src/contractor/ibex_Ctc3BCid.h) · [node](../classes/contractors/Ctc3BCid.md) |
| **`CtcAcid`** | adaptive 3BCID — auto-tunes how many vars to shave | **used** — `--acid` (`contractor_ibex_acid`) | [hdr](../../../ibex-fork/src/contractor/ibex_CtcAcid.h) · [node](../classes/contractors/CtcAcid.md) |
| **`CtcPolytopeHull`** | contract to the hull of an LP relaxation (2n Simplex calls) | **used** — `--polytope` (default off); LP live (`LP_LIB=soplex`) | [hdr](../../../ibex-fork/src/contractor/ibex_CtcPolytopeHull.h) · [node](../classes/contractors/CtcPolytopeHull.md) |
| **`CtcInverse`** | `f⁻¹(C)`: contract `[x]` w.r.t. a contractor on `f([x])` | no → audit D | [hdr](../../../ibex-fork/src/contractor/ibex_CtcInverse.h) |
| `CtcNotIn` | contract for `f(x) ∉ [y]` | no | [hdr](../../../ibex-fork/src/contractor/ibex_CtcNotIn.h) |
| `CtcInteger` | integrality contractor | dReal has its own `contractor_integer` | [hdr](../../../ibex-fork/src/contractor/ibex_CtcInteger.h) |
| `CtcKuhnTucker(LP)` | first-order (KKT) contractor — NLP optimization only | no (correct) | — |

## Composition operators (contractor programming)

| Class | arity | Definition | dReal analog |
|---|---|---|---|
| `CtcIdentity` | 0 | `[x]↦[x]` | — |
| `CtcCompo` | n | `C₁∘…∘Cₙ` | `contractor_seq.cc` |
| `CtcUnion` | n | `□(C₁([x])∪…∪Cₙ([x]))` | `contractor_join.cc` |
| `CtcFixPoint` | 1 | `C^∞`, stop when a step removes < `ratio·diam` (default **0.1**) | `contractor_fixpoint.cc` |
| `CtcPropag` | n | agenda/fixpoint over `{Cᵢ}`, skipping via input/output; stop ratio default **0.01**; `accumulate` flag | `contractor_worklist_fixpoint.cc` |

**Propagation tuning (`CtcPropag`/`CtcFixPoint`):**
- *fixpoint ratio* — a contraction removing less than `ratio × diam` is not
  re-propagated. Default 0.01 (propag) / 0.1 (fixpoint). Lower = tighter, slower.
- *`accumulate` flag* — compares against the box after the *last significant*
  contraction, not the immediately-previous one; avoids stopping early when many
  small contractions sum to a significant one. "Slightly better contraction, a
  little more time."
- *input/output bitsets* — without them `CtcPropag` degrades to a plain fixpoint
  (every contractor re-fired). dReal's own worklist makes the analogous choice.

## Shaving / 3B / CID (SHIPPED — `--acid`/`--3bcid`; docs empty here, headers are ground truth)

Wrapped in `contractor_ibex_acid.{cc,h}` as `CtcAcid(system, CtcHC4(system))`
(`--acid`) or `Ctc3BCid(CtcHC4(system))` (`--3bcid`); the two are mutually
exclusive. dReal surfaces two of the IBEX knobs below as flags: **`--s3b`** (shave
depth) and **`--acid-ct-ratio`**.

The `.rst` leaves **Shaving** and **Acid & 3BCid** as `*(to be completed)*`. The
algorithm and every tuning parameter come from the headers:

- **Shaving** — call sub-contractor `C` on slices of `[x]`; if a slice is wholly
  eliminated, remove it. Generalizes SAC (Bessiere & Debruyne 2004) / "3B"
  (Lhomme 1993).
- **`Ctc3BCid`** params (defaults): `s3b=10` (max shaved slices — *"often the most
  significant impact on performance, tune in priority"*, best 5–200), `scid=1`
  (CID slices — *"user should generally not play with this"*), `vhandled=-1`
  (vars per call = all), `var_min_width=1e-11`.
- **`CtcAcid`** — adaptive: alternates short tuning phases (measure shaving gain
  per var) with long running phases (shave the `nbcidvar` most-promising vars,
  ordered by smearsumrel). Key param `ct_ratio` (**code default `0.002`**; note a
  constructor-comment says 0.005 — code wins). Needs a `System` (for the var
  ordering). This is the contractor IBEX's own solver enables by default.

Full param semantics + integration notes:
[`../classes/contractors/CtcAcid.md`](../classes/contractors/CtcAcid.md),
[`Ctc3BCid.md`](../classes/contractors/Ctc3BCid.md).

## Polytope hull + linearizations

`CtcPolytopeHull` takes a `Linearizer` and runs 2n LP solves (min/max each var)
on the relaxation. Built-in linearizers:
- **`LinearizerXTaylor`** — corner-based interval Taylor (Araya 2012). **dReal uses
  this**, at defaults `RELAX, RANDOM_OPP, HANSEN`.
- **`LinearizerCompo`** — intersection of several linearizers' polytopes.
- **`LinearizerFixed`** — a fixed `Ax≤b`.
- **`LinearizerAffine2`** — affine-arithmetic relaxation (Ninin & Messine 2009).
  **Not in the fork** — it lives in the `ibex-affine` plugin (verified absent).
  Treat as a plugin-integration task, not a knob.

Details + the corner/slope menu:
[`../classes/linear/LinearizerXTaylor.md`](../classes/linear/LinearizerXTaylor.md).

## Quantifiers: `CtcExist` / `CtcForAll`

Generic handling of `∃y∈[y] c(x,y)` / `∀y∈[y] c(x,y)`: split `[y]` to precision
ε, contract each sub-box, project onto x, then **union** (Exist) or **intersect**
(ForAll). Complexity is exponential in `dim(y)` (`O((rad(y)/ε)^{n_y})`) — adaptive
ε strongly recommended.

`CtcForAll` is **shipped** as the `--forall-pre-prune` pre-pruner
(`contractor_ibex_forall.{cc,h}`): pure interval proj-intersection contraction
that runs *alongside* — never instead of — dReal's own δ-complete CEGIS ∃∀
machinery (`contractor_forall.h`, `counterexample_refiner.cc`, DPLL(T)-tied),
shrinking the existential box so CEGIS converges in fewer counterexample rounds.
It is COMPLETENESS-only and sound: it deletes an existential point only when the
body fails at a *real* universal point `mid(y)∈y_init⊆` binder, so it can never
drop a true ∃∀ solution (false `unsat` impossible — SOUNDNESS safe). `CtcExist`
remains unused. Perf record + the encoding-fragile speedup:
[`../../exists_forall_perf.md`](../../exists_forall_perf.md); IBEX-lever audit:
[`../AUDIT-QUANTIFIERS.md`](../AUDIT-QUANTIFIERS.md) Q1. See
[`../chapters/separator.md`](separator.md) for the set-inversion cousin.
