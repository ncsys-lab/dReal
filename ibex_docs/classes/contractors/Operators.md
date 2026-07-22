# Composition operators — `CtcCompo` / `CtcUnion` / `CtcFixPoint` / `CtcPropag`

The "contractor programming" combinators (Chabert & Jaulin 2009). In dReal's
DPLL(T) search loop **none** of them is used — it hand-rolls the equivalents so
they can carry SMT bookkeeping (explanations, theory lemmas, the Box abstraction).
(`CtcCompo`/`CtcUnion` *do* appear outside that loop: the shipped
`--forall-pre-prune` contractor materializes a `∀`-body with `ibex::CtcCompo` for
`∧` and `ibex::CtcUnion` for `∨` — `contractor_ibex_forall.cc`.) Each is documented
here as the *reference shape* for its dReal analog, with the one knob worth
cross-checking flagged.

> **dReal status:** in the DPLL(T) search loop, **none used** — hand-rolled analogs
> `CtcCompo`→`contractor_seq.cc`, `CtcUnion`→`contractor_join.cc`,
> `CtcFixPoint`→`contractor_fixpoint.cc`,
> `CtcPropag`→`contractor_worklist_fixpoint.cc`. **But `CtcCompo`/`CtcUnion` *are*
> instantiated directly** to build the `∀`-body inside the shipped
> `--forall-pre-prune` pre-pruner (`contractor_ibex_forall.cc`: `∧`→`CtcCompo`,
> `∨`→`CtcUnion`); `CtcFixPoint`/`CtcPropag` are used nowhere. See
> [`../../dreal-ibex-usage.md`](../../dreal-ibex-usage.md).

---

## `CtcCompo` — sequential composition

Header: [`ibex_CtcCompo.h`](../../../../ibex-fork/src/contractor/ibex_CtcCompo.h).
`class CtcCompo : public Ctc`. For a box `[x]`, applies `c_n(…(c_1(c_0([x]))))`.

```cpp
CtcCompo(const Array<Ctc>& list, bool incremental=false, double ratio=default_ratio);
CtcCompo(Ctc& c1, Ctc& c2, bool incremental=false, double ratio=default_ratio);
// … overloads up to 20 explicit sub-contractors …
static constexpr double default_ratio = 0.1;   // used only in incremental mode
```

| Param | Default | Meaning |
|---|---|---|
| `list` / `c1…c20` | — | sub-contractors, applied left-to-right |
| `incremental` | `false` | track per-variable impact to skip sub-contractors that can't fire |
| `ratio` | **0.1** (comment + constexpr agree) | impact-significance threshold, only consulted when `incremental` |

Plain sequential chaining; order-sensitive. dReal's `contractor_seq.cc` is the analog.

---

## `CtcUnion` — disjunctive composition

Header: [`ibex_CtcUnion.h`](../../../../ibex-fork/src/contractor/ibex_CtcUnion.h).
`class CtcUnion : public Ctc`. Returns the **hull** of the per-contractor results:
`□(c_1([x]) ∪ … ∪ c_n([x]))`.

```cpp
CtcUnion(const Array<Ctc>& list);
CtcUnion(const System& sys);          // forward-backward contractor for the NEGATION of a system
CtcUnion(Ctc& c1, Ctc& c2);
// … overloads up to 20 explicit sub-contractors …
```

No tunable params. The `(System sys)` form is the disjunction/negation builder
(owns a `NormalizedSystem`). The hull loses the gap between branches — a disjoint
union is over-approximated to one box. dReal's `contractor_join.cc` is the analog.

---

## `CtcFixPoint` — iterate one contractor to its fixpoint

Header:
[`ibex_CtcFixPoint.h`](../../../../ibex-fork/src/contractor/ibex_CtcFixPoint.h).
`class CtcFixPoint : public Ctc`. Re-applies a single `C` until the Hausdorff
distance between two iterations is `< ratio·diam`.

```cpp
CtcFixPoint(Ctc& ctc, double ratio=default_ratio);
static constexpr double default_ratio = 0.1;   // comment "set to 0.1" agrees
```

| Param | Default | Meaning |
|---|---|---|
| `ctc` | — | the sub-contractor iterated |
| `ratio` | **0.1** | stop when a step removes `< ratio·diam`. When the `FIXPOINT` output flag is set the *true* fixpoint (null ratio) was reached. |

`ratio` gives **no guarantee** on distance-to-fixpoint — a documented limitation.
dReal's `contractor_fixpoint.cc` is the analog; its stop-ratio is the B1
cross-check in [`../../AUDIT.md`](../../AUDIT.md).

---

## `CtcPropag` — AC3-like constraint propagation

Header:
[`ibex_CtcPropag.h`](../../../../ibex-fork/src/contractor/ibex_CtcPropag.h).
`class CtcPropag : public Ctc`. The interval AC3: an agenda over `{C_i}` with a
constraint hypergraph; only re-fires a contractor whose inputs were touched. The
base class of [`CtcHC4`](./CtcHC4.md).

```cpp
CtcPropag(const Array<Ctc>& cl, double ratio=default_ratio, bool incr=false);
static constexpr double default_ratio = 0.01;   // ⚠ member doc-comment says "set to 0.1"
```

| Param | Default | Meaning |
|---|---|---|
| `cl` | — | the contractors to propagate |
| `ratio` | **0.01** | a projection removing `< ratio·diam` is not re-propagated. **⚠ drift:** the member doc-comment says "set to 0.1"; the `static constexpr` (0.01) is ground truth and matches the chapter. |
| `incr` | `false` | start from impacted vars only (when called with an impact mask) |
| `accumulate` | **`false`** (set in ctor, not a constructor arg) | when `true`, compare against the box after the *last significant* contraction (fine propagation) instead of the immediately-previous projection (coarse) — slightly tighter, slightly slower |

Scales on **sparse** systems via the `input`/`output` bitsets ([`Ctc`
interface](../../chapters/contractor.md)); without them it degrades to a plain
fixpoint (every contractor re-fired). dReal's `contractor_worklist_fixpoint.cc`
makes the analogous choices; its stop-ratio + accumulate behavior is the B1
cross-check.

---

Related: [`CtcHC4.md`](./CtcHC4.md), [chapter](../../chapters/contractor.md),
[`../../AUDIT.md`](../../AUDIT.md) (B1), [`../../KNOBS.md`](../../KNOBS.md).
