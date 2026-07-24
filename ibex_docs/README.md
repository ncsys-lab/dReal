# `ibex_docs/` — IBEX API reference + leverage audit for dReal

A navigable, agent-readable summary of IBEX's API, built to answer one recurring
question fast: **"does IBEX offer X, and does dReal already use it?"** — so the
next person doesn't re-read the docs + 216 headers to find a lever (the way the
sibling [`capd_docs/`](../capd_docs/) crawl surfaced the CAPD tube fix).

Narrative ground truth: the fork's own `../../ibex-fork/doc/*.rst` (24 chapters —
this IS the latest upstream; mainline froze its docs 2025-04-27). API ground truth:
the **fork** headers `../../ibex-fork/src/*/ibex_*.h`
(`ncsys-lab/ibex-lib@dreal-perf-patches`, the fork dReal actually builds — live patch
catalog: `../../ibex-fork/MIGRATION.md`). IBEX is **2.9.1** (`ibex-fork/CMakeLists.txt:4`),
built `-DINTERVAL_LIB=gaol -DLP_LIB=soplex` (`CMakeLists.txt:188-189`; vendored SoPlex
4.0.2 → `libsoplex.a`). Every signature/default here is confirmed against a header, not
inferred from a name.

## Start here

| File | What it is |
|---|---|
| **[AUDIT.md](AUDIT.md)** | **The payoff** — the leverage audit, framed **shipped-vs-still-open**: its headline (`CtcAcid` shaving) and the polytope hull shipped as opt-in `--acid`/`--3bcid`/`--polytope`, and the 2026-07 RELATED-WORK campaign shipped the rest (`--newton` — the last absent contractor, measured dead — plus `--obbt`/`--mohc`/`--polytope-linearizer`). Each item cross-referenced to current usage, the nearest fork patch, and its measured verdict. |
| **[KNOBS.md](KNOBS.md)** | **The complete tuning surface** — every IBEX-side knob (build flags, HC4/propagation, shaving, Newton, polytope/linearizer, bisection), its option menu, IBEX default, and dReal status (with the `--acid`/`--3bcid`/`--polytope`/`--s3b`/`--acid-ct-ratio`/`--forall-pre-prune` flags that expose them). Start here to check nothing's left on the table. |
| **[classes/contractors/COMPARISON.md](classes/contractors/COMPARISON.md)** | **The contractor catalog** — every IBEX contractor (all 23, by category) in one strengths/weaknesses table with defaults + ordered recommendations. The "single biggest lever" surface. |
| **[ARCHITECTURE-COMPARISON.md](ARCHITECTURE-COMPARISON.md)** | **IbexSolve vs dReal's `CheckSat`/ICP**, side-by-side with file:line — answers "is dReal leaving performance on the table by reimplementing the search loop?" (verdict: no at the loop level; the lever is the atomic contractors, one level down). |
| **[AUDIT-QUANTIFIERS.md](AUDIT-QUANTIFIERS.md)** | **Second audit — nested quantifiers.** For ∃∀ / `∀∃∃∀` queries over high-dim transcendental constraints: how IBEX's *composable* `CtcForAll`/`CtcExist` pre-prune dReal's CEGIS and provide a sound skeleton for the deeper alternations dReal currently crashes on. |
| **[RELATED-WORK.md](RELATED-WORK.md)** | **Levers *beyond* mainline IBEX** — affine arithmetic (the `LinearizerAffine2` plugin), OBBT, monotonicity-based `CtcMohc`, DynIbex validated-RK ODE, and alternative rigorous integrators, ranked by *(integration cost × completeness win)* with source-grounded feasibility sketches + a paper reading list. All COMPLETENESS levers, never soundness. |
| **[dreal-ibex-usage.md](dreal-ibex-usage.md)** | **The baseline** — what dReal binds today (HC4 is the only contractor run *by default*; `--polytope`/`--acid`/`--3bcid`/`--forall-pre-prune` are live opt-in) + the fork patches as "dReal's IBEX divergence" (catalog: `../../ibex-fork/MIGRATION.md`). The audit's reference point. |
| [index/all-classes.md](index/all-classes.md) | Auto-harvested one-line stub for all 216 fork classes (name + brief + header link) — the catch-all. |

## The one-paragraph orientation

dReal binds a **thin slice** of IBEX: `Interval`/`IntervalVector` arithmetic,
`Function::backward` (HC4 — the contraction hot loop, the only contractor run by
default), plus **live opt-in** contractors — `--polytope` (`CtcPolytopeHull` +
`LinearizerXTaylor`, LP-relaxation hull over the `LP_LIB=soplex` backend),
`--acid`/`--3bcid` (`CtcAcid`/`Ctc3BCid` shaving on the HC4 path;
`theory_solver.cc:244`), and `--forall-pre-prune` (a sound `CtcForAll` pre-pruner for
the ∃∀ layer — [AUDIT-QUANTIFIERS.md](AUDIT-QUANTIFIERS.md)). All ship a per-worker
`*Mt` cell so they run under `--jobs>1`. It **hand-rolls** its own fixpoint/compose/∃∀
layer (inside DPLL(T)) and **ignores** IBEX's solver, optimizer, separators, sets,
parser, and bisectors by design. The fork's patches (`../../ibex-fork/MIGRATION.md`)
already spent the HC4 micro-optimization budget (rounding, exceptions, gradient). Since
the 2026-07 campaign every contractor IBEX ships has a dReal path — `--newton`/`--obbt`/
`--mohc`/`--polytope-linearizer` joined the opt-in set (Newton measured dead on this
corpus; mohc/affine are odeexpr-only wins). See [AUDIT.md](AUDIT.md).

## The tree

- **[chapters/](chapters/)** — one node per `.rst` source (IBEX's own
  organization), grouped by the upstream TOC. Each node: what it covers + key API +
  a "dReal status" line + source link.
  - *Programmer guide (rich, the dReal-relevant ones):*
    [interval](chapters/interval.md) ·
    [function](chapters/function.md) ·
    [constraint](chapters/constraint.md) ·
    [system](chapters/system.md) ·
    [**contractor**](chapters/contractor.md) (the goldmine) ·
    [strategy](chapters/strategy.md)
  - *Programmer guide (capability-extension / ignored):*
    [separator](chapters/separator.md) ·
    [set](chapters/set.md) ·
    [solver-prog](chapters/solver-prog.md) ·
    [optim-prog](chapters/optim-prog.md) ·
    [reference](chapters/reference.md) (algorithm → paper map) ·
    [tutorial](chapters/tutorial.md) · [lab](chapters/lab.md) ·
    [example-slam](chapters/example-slam.md) · [intro](chapters/intro.md)
  - *User guide:* [install-cmake](chapters/install-cmake.md) (the 2 build knobs) ·
    [solver](chapters/solver.md) · [optim](chapters/optim.md) ·
    [minibex](chapters/minibex.md) · [resources](chapters/resources.md)
  - *Developer:* [packages](chapters/packages.md) ·
    [plugins-dev](chapters/plugins-dev.md) · [dev-misc](chapters/dev-misc.md) ·
    [index](chapters/index.md) (upstream TOC)
- **[classes/](classes/)** — rich, header-faithful summaries for the
  performance-relevant classes (selective depth; everything else is stubbed in the
  index):
  - `contractors/` — [**COMPARISON**](classes/contractors/COMPARISON.md) (all 23, table)
    · [CtcAcid](classes/contractors/CtcAcid.md) ·
    [Ctc3BCid](classes/contractors/Ctc3BCid.md) ·
    [CtcNewton](classes/contractors/CtcNewton.md) ·
    [CtcPolytopeHull](classes/contractors/CtcPolytopeHull.md) ·
    [CtcFwdBwd](classes/contractors/CtcFwdBwd.md) ·
    [CtcHC4](classes/contractors/CtcHC4.md) ·
    [Operators](classes/contractors/Operators.md) (Compo/Union/FixPoint/Propag) ·
    [Quantifiers](classes/contractors/Quantifiers.md) (Exist/ForAll) ·
    [CtcInverse](classes/contractors/CtcInverse.md) ·
    [CtcQInter](classes/contractors/CtcQInter.md) ·
    [CtcLinearRelax](classes/contractors/CtcLinearRelax.md) ·
    [CtcOptimShaving](classes/contractors/CtcOptimShaving.md) ·
    [CtcKuhnTucker](classes/contractors/CtcKuhnTucker.md)
  - `arithmetic/` — [Interval](classes/arithmetic/Interval.md) ·
    [IntervalVector](classes/arithmetic/IntervalVector.md)
  - `function/` — [Function](classes/function/Function.md)
  - `linear/` — [LinearizerXTaylor](classes/linear/LinearizerXTaylor.md)
  - `system/` — [System](classes/system/System.md) (+ NumConstraint) ·
    [SystemFactory](classes/system/SystemFactory.md)
  - `strategy/` — [Bisectors](classes/strategy/Bisectors.md)
- **[index/all-classes.md](index/all-classes.md)** — everything else (stubs).

## Future directions (capability extensions)

Beyond performance, two IBEX capabilities dReal could adopt (detail in
[AUDIT.md](AUDIT.md) "capability extensions"):

| Target | IBEX support | Entry node |
|---|---|---|
| **Feasible-set output** for ∃∀/parametric queries (return the set, not just sat/unsat) | **Yes** — separators + pavings (SIVIA) | [separator](chapters/separator.md), [set](chapters/set.md) |
| **Rigorous optimization-modulo-theories** (sound global bound vs nlopt's local) | **Yes** — `Optimizer` + `ExtendedSystem` + KKT contractor | [optim](chapters/optim.md) |

## How this was built / how to extend it

Unlike the CAPD crawl, the IBEX docs are tiny and clean (24 `.rst`, 428 KB), so
the chapters are summarized directly from source (no de-tagger, no subagents). The
class layer's signatures/defaults are copied from the fork headers (the one
fidelity rule — a fabricated IBEX param would send a future session chasing an API
that doesn't exist; where upstream docs and the fork diverge, the **fork header
wins**, since it's what dReal builds). To refresh the stub index, re-run
`python3 scripts/harvest_class_briefs.py --ibex` (walks `../../ibex-fork/src/*/ibex_*.h`).

> Scope: a tiered map — complete breadth (every `.rst` chapter has a node; all 216
> classes are at least stubbed), selective depth (rich nodes only where they inform
> performance/audit). It is a reference, not a substitute for the source headers;
> when precision matters, the header is ground truth.
