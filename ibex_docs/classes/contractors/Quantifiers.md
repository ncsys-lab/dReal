# Quantifier contractors — `CtcExist` / `CtcForAll` / `CtcQuantif`

Generic interval handling of one quantified constraint over a parameter box.

> **dReal status:** **`CtcForAll` is SHIPPED** — it is the base of
> `ContractorIbexForall`
> (`src/dreal/contractor/contractor_ibex_forall.{cc,h}`, flag `--forall-pre-prune`),
> a sound COMPLETENESS-only pre-pruner run *beside* — never instead of — dReal's own
> CEGIS ∃∀ decider (`contractor_forall.h`, tied to DPLL(T)). **`CtcExist` /
> `CtcQuantif`: not directly instantiated** (`CtcQuantif` is compiled as `CtcForAll`'s
> abstract base; `CtcExist` has no dReal use shape — it stays a design-comparison
> reference). Full audit + measured record:
> [`../../AUDIT-QUANTIFIERS.md`](../../AUDIT-QUANTIFIERS.md) Q1 (the single most-cited
> node — `theory_solver.cc` references it at the pre-prune call site).

## `CtcQuantif` — the abstract base

Header:
[`ibex_CtcQuantif.h`](../../../../ibex-fork/src/contractor/ibex_CtcQuantif.h)
(`contractor/`). `class CtcQuantif : public Ctc`. Holds the shared machinery: a
`LargestFirst` bisector over the parameters, a `VarSet` splitting variables `x`
from parameters `y`, the initial parameter box `y_init`, and the bisection
precision `prec`.

```cpp
CtcQuantif(const NumConstraint& c, const VarSet& vars, const IntervalVector& y_init, double prec);
CtcQuantif(Ctc& c, const VarSet& vars, const IntervalVector& y_init, double prec, bool own_ctc=false);
```

| Param | Default | Meaning |
|---|---|---|
| `c` | — | the constraint (or, second form, an implicit contractor for it) |
| `vars` | — | `VarSet`: which components are variables `x` vs parameters `y` |
| `y_init` | — | initial box for the parameters (settable dynamically) |
| `prec` | **required — no default** | bisection precision on `y`; the recursion stops splitting a parameter box once `max_diam(y) ≤ prec` |
| `own_ctc` | `false` | whether the contractor is owned/deleted by `this` |

## `CtcExist` — `∃y∈[y] c(x,y)` (proj-union)

Header: [`ibex_CtcExist.h`](../../../../ibex-fork/src/contractor/ibex_CtcExist.h).
`class CtcExist : public CtcQuantif` (`typedef CtcProjUnion`).

```cpp
CtcExist(const NumConstraint& c, const ExprNode& y1, const IntervalVector& y_init, double prec);
CtcExist(const NumConstraint& c, const VarSet& y,    const IntervalVector& y_init, double prec);
CtcExist(Ctc& c, const BitSet& vars, const IntervalVector& y_init, double prec, bool own_ctc=false);
```

Algorithm (`ibex_CtcExist.cpp`): a stack-driven branch-and-bound bisects `[y]`;
each sub-box is contracted, projected onto `x`, and **unioned** into the result.
It samples the mid-vector of `y` to get an early estimate of the projection (for
pruning) before reaching `prec`-sized parameter boxes.

## `CtcForAll` — `∀y∈[y] ⇒ c(x,y)` (proj-inter) — **shipped in dReal**

Header: [`ibex_CtcForAll.h`](../../../../ibex-fork/src/contractor/ibex_CtcForAll.h).
`class CtcForAll : public CtcQuantif` (`typedef CtcProjInter`). Same constructors
as `CtcExist`. The sub-boxes are **intersected** onto `x` instead of unioned.

**dReal wiring** (`contractor_ibex_forall.cc`): `ContractorIbexForall` builds the
`∀`-body Formula into IBEX contractors (`∨`→`CtcUnion`, which structurally preserves
the `domain ⟹ φ` implication guard) and wraps them in the generic
`ibex::CtcForAll(Ctc& inner, BitSet vars, y_init, prec)` — the existential indices
`[0, box.size())` are the "variables" `x`, the universal binder box is `y_init`, and
`prec = --forall-pre-prune-prec` (default 0.5). It fires inside the forall fixpoint
*beside* the CEGIS `ContractorForall` (`theory_solver.cc`). An unbounded universal
dimension or a non-relational body ⇒ `is_dummy()` no-op (CEGIS still decides).
**COMPLETENESS-only:** it removes an `x`-point only when the body fails at a *real*
universal point `mid(y) ∈ y_init ⊆` binder domain, so it can never delete a true ∃∀
solution — a false `unsat` is impossible (SOUNDNESS preserved; a tighter existential
box only cuts CE iterations, never moves a verdict). Per-worker under `--jobs>1` via
`ContractorIbexForallMt` (one `ibex::CtcForAll` per thread). The speedup is
encoding-fragile and doesn't crack the ∃∀ UNSAT enclosure wall —
[`../../AUDIT-QUANTIFIERS.md`](../../AUDIT-QUANTIFIERS.md) Q1.

## Composability — the key lever (takes any `Ctc`, so it nests)

The generic constructor `CtcQuantif(Ctc& c, …)` / `CtcForAll(Ctc& c, BitSet vars, …)`
takes **any contractor** as its inner `c`. Because `CtcForAll`/`CtcExist` are
themselves `Ctc`, they **nest**: a prenex block `∀a ∃b ∃c ∀d φ` becomes
`CtcForAll_a(CtcExist_b(CtcExist_c(CtcForAll_d(HC4(φ)))))`. This is a *sound interval
pruner for an arbitrary alternation* — exactly the shape dReal currently **crashes**
on (nested `forall`). It is the single most important fact for nested-quantifier work.

## Cost, and where dReal's CEGIS vs these contractors each win

Complexity is **exponential in `dim(y)`** — roughly `O((rad(y)/prec)^{n_y})` sub-boxes
— so `prec` must be adaptive and `n_y` kept small. dReal's depth-one `∃∀` is a
δ-**complete decision** via counterexample-guided refinement (point witnesses, no
blind bisection); these IBEX contractors are sound **pruners** with no δ-decision. For
the *general* audit they are therefore not a drop-in replacement.

The depth-one **pre-pruner is now shipped** (`--forall-pre-prune`, above); the
still-open lever is the **composable skeleton** for the `∀∃∃∀` alternations dReal
can't otherwise express (the nesting section above). The full analysis (Q1–Q7,
integration points, the `dim(y)`-exponential caveat) is the dedicated second audit
section: **[`../../AUDIT-QUANTIFIERS.md`](../../AUDIT-QUANTIFIERS.md)**.

Related: [`../../AUDIT-QUANTIFIERS.md`](../../AUDIT-QUANTIFIERS.md) (the nested-quantifier
audit), [`../../chapters/separator.md`](../../chapters/separator.md) (the set-inversion
cousin), [contractor chapter](../../chapters/contractor.md), [`../../AUDIT.md`](../../AUDIT.md).
