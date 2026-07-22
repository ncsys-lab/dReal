# Chapter: Strategies (search infrastructure)

Source: [`strategy.rst.txt`](../../../ibex-docs/_sources/strategy.rst.txt) ·
`strategy.html`. Box properties, file I/O, **bisectors**, and **cell buffers** —
the machinery of a tree search (`ibexsolve`/`ibexopt`).

> **dReal status:** dReal runs its **own** branch-and-prune inside DPLL(T), so it
> does not use IBEX bisectors or cell buffers. This chapter is mostly a
> design-comparison reference. The one piece worth knowing is **Box Properties**
> (the `Bxp` lazy-cache pattern) — conceptually the same trick as dReal's
> `thread_local` IMap cache in the ODE contractor.

## Box Properties (`Bxp`) — the lazy-cache pattern (release 2.7)

Attach computed data (e.g. an image `f([x])`) to a box so it propagates through
the search and is visible to all operators, instead of recomputing. Key ideas:
- subclass `Bxp`; register via `Ctc::add_property`; access through
  `ContractContext`'s `BoxProperties` map keyed by `id` (`next_id()`).
- **lazy update** — postpone the expensive recompute until something reads it
  (an eager `update()` makes things *slower*); the "trust chain" guarantees a
  property is up-to-date when passed as a function argument.
- **dependencies** — order updates (`BxpImageWidth` depends on `BxpImage`).

This is the same memoization discipline dReal applies ad hoc; if dReal ever
adopts an IBEX contractor that carries a `Bxp` (e.g. the polytope hull's linearizer
property), the property plumbing comes along.

## COV files

A hierarchy of covering file formats (`Cov ⊃ CovList ⊃ CovIUList ⊃ CovIBUList ⊃
CovManifold ⊃ CovSolverData`) used by `ibexsolve`/`ibexopt` to exchange box sets.
**Not used by dReal** (it has its own model/output path).

## Bisectors (`Bsc`) — dReal branches itself

Pick a component and split it at a `ratio` of its diameter. ⚠️ `Bsc::default_ratio()`
is **0.45** (verified `ibex_Bsc.cpp:19`), *not* the 0.5 midpoint — the default cut is
slightly off-center. Full detail + the verified smear formulas:
[`../classes/strategy/Bisectors.md`](../classes/strategy/Bisectors.md).
The `.rst` leaves the descriptions `*(to be completed)*`; the catalog from the
headers (`bisector/`):
- `LargestFirst(prec=0, ratio)` — split the widest component.
- `RoundRobin(prec, ratio)` — each component in turn.
- `SmearFunction` family — `SmearMax`, `SmearSum`, `SmearSumRelative`,
  `SmearMaxRelative` (all `(System&, prec, ratio)`): weight components by the
  Jacobian "smear" (impact). **dReal reimplements all four** — see below.
- `LSmear` — Lsmear, dual-based variable selection (Araya & Neveu 2018; extends
  `SmearSumRelative`).
- `OptimLargestFirst` — optimizer variant.

Per-variable precision: pass a `Vector` instead of a scalar `prec` (for physical
quantities of different magnitude). dReal does **not** instantiate IBEX's `Bsc`
classes (it branches inside DPLL(T)), but `--smear <variant>` (default off)
**reimplements** IBEX's four `SmearFunction` variants (`smearsum`, `smearsumrel`,
`smearmax`, `smearmaxrel`) as a dReal brancher (`brancher_smear.{h,cc}`), replacing
largest-first with Jacobian-smear variable choice — soundness-free (variable choice
never moves a verdict). NRA-only: the smear Jacobian skips `Kind::ODE_LOHNER`, so
`smearsum` helps the odeexpr families but *collapses* the ODE families — enable
per-project, not globally. A/B + mechanism: solver `--smear` docs (`CLAUDE.md`).

## Cell buffers — dReal manages its own search stack

`CellStack` (DFS), `CellHeap` / `CellDoubleHeap` / `CellBeamSearch` (best-first,
for optimization). All `*(to be completed)*` in the `.rst`. Not used by dReal.

Class detail (the few that matter for design comparison):
[`../classes/strategy/Bisectors.md`](../classes/strategy/Bisectors.md).
