# Bisectors (`Bsc` family) — IBEX's branching strategies

> ⚠ **Stale `LP_LIB=none` aside** (pre-`fa3b74bd7`) — corrected 2026-07-02: the LSmear row's "not
> usable in dReal's build, ships `LP_LIB=none`, polytope path dormant" reasoning is superseded — IBEX
> builds `-DLP_LIB=soplex` (`CMakeLists.txt`); an LP backend **is** linked. (LSmear is still not
> wired into dReal, but only because it needs an `ExtendedSystem`/goal, not for lack of an LP solver.)
> Separately, the **smear formula IS now shipped** in dReal via `--smear` (`brancher_smear.cc`) — see
> the dReal-status block below. See [`README.md`](../../README.md) banner.

Headers in [`bisector/`](../../../../ibex-fork/src/bisector/). A **bisector** is the
operator an IBEX strategy (the [`Solver`](../../chapters/solver.md) /
[`Optimizer`](../../chapters/optim.md)) calls at each *choice point*: given a box, it
**picks one component (variable) and splits its interval at a `ratio`**, producing two
child boxes. The base class `Bsc` (`ibex_Bsc.h`) declares the one method every variant
implements — `BisectionPoint choose_var(const Cell& cell)` — and owns the precision /
skip logic; the subclasses differ only in *which* variable they choose.

> **dReal status:** the `Bsc` **classes** are unused (correctly) — dReal runs its **own**
> branch-and-prune inside DPLL(T) and never instantiates an IBEX `Bsc`. But the **smear
> *formula*** is now **shipped twice**: `--smear` (`brancher_smear.cc`) reimplements all four
> SmearFunction variants over box dimensions using an `ibex::System`'s interval Jacobian
> (`System::f_ctrs.jacobian`), and [`CtcAcid`](../contractors/CtcAcid.md) orders its shaved
> variables by the same `smearsumrel` criterion. So this node is a **design-comparison**
> reference for the `Bsc` hierarchy, but the useful idea it documents is no longer dormant.
> See the [strategy chapter](../../chapters/strategy.md), [`../../AUDIT.md`](../../AUDIT.md) E,
> and [`../../ARCHITECTURE-COMPARISON.md`](../../ARCHITECTURE-COMPARISON.md).

---

## Base class `Bsc` — precision, ratio, and the split point (verified)

All values below are read from `ibex_Bsc.cpp` / `ibex_Bsc.h`, not inferred.

- **`Bsc::default_ratio() == 0.45`** — `ibex_Bsc.cpp:19-21` returns the literal `0.45`
  (the `ibex_Bsc.h:119` doc-comment agrees: "Default ratio (0.45)"; **no drift**).
  ⚠️ Note: `0.45` is **not** the midpoint. The chapter prose's "0.5 = midpoint,
  `default_ratio()`" conflates the two — the midpoint is `0.5`, but the *default* split
  is the slightly off-center `0.45`. `ratio` is the fraction of the diameter at which the
  cut falls (a relative `BisectionPoint`, see below).
- **`prec` (skip-if-narrower)** — `Bsc::too_small(box,i)` (`ibex_Bsc.h:160-164`) is
  `box[i].diam() < prec(i) || !box[i].is_bisectable()`. So `prec` is the per-variable
  width threshold below which a variable is *not* split; a non-bisectable (e.g. infinite)
  interval is also skipped. When **every** variable is too small, the bisector throws
  `NoBisectableVariableException` (`ibex_NoBisectableVariableException.h`) — this is how
  the search learns a box is at the precision frontier. `prec == 0` is allowed
  (`ibex_Bsc.cpp:25` comment) and means an endless bisection (skip only on non-bisectable).
- **Per-variable precision `Vector`** — two constructors: `Bsc(double prec)` stores a
  size-1 vector (uniform); `Bsc(const Vector& prec)` stores one threshold per variable.
  `uniform_prec()` is `_prec.size()==1` (`ibex_Bsc.h:151-153`); `prec(i)` returns
  `_prec[0]` when uniform else `_prec[i]`. Use the `Vector` form when variables have
  different physical magnitudes. **It also changes `LargestFirst`'s metric**: with a
  `Vector`, "largest" is measured as `diam(box[i]) / prec(i)` (width *relative to* each
  variable's precision), not raw `diam` (`ibex_LargestFirst.cpp:41,44`).
- **The split point** — `choose_var` returns a `BisectionPoint(var, ratio, true)`
  (`ibex_BisectionPoint.h`): variable index, position, and `rel_pos=true` meaning the
  position is the *relative* ratio (e.g. `0.45` of `[2,4]`), not an absolute coordinate.

---

## Constructors — verbatim from the headers

```cpp
// ibex_LargestFirst.h
LargestFirst(double prec=0,        double ratio=Bsc::default_ratio());   // prec defaults to 0
LargestFirst(const Vector& prec,   double ratio=Bsc::default_ratio());

// ibex_RoundRobin.h  — note: prec has NO default (must be supplied)
RoundRobin(double prec,            double ratio=Bsc::default_ratio());
RoundRobin(const Vector& prec,     double ratio=Bsc::default_ratio());

// ibex_SmearFunction.h  — every smear variant takes a System& (for the Jacobian)
SmearMax        (System& sys, double prec, double ratio=Bsc::default_ratio());
SmearSum        (System& sys, double prec, double ratio=Bsc::default_ratio());
SmearSumRelative(System& sys, double prec, double ratio=Bsc::default_ratio());
SmearMaxRelative(System& sys, double prec, double ratio=Bsc::default_ratio());
// (each also has a `const Vector& prec` form and a `LargestFirst& lf` fallback-injecting form)

// ibex_LSmear.h  — needs an ExtendedSystem (optimization) and builds an LPSolver
LSmear(ExtendedSystem& sys, double prec, double ratio=Bsc::default_ratio(),
       lsmear_mode lsmode=LSMEAR_MG);

// ibex_OptimLargestFirst.h  — optimizer variant: goal_var index + choose_obj flag
OptimLargestFirst(int goal_var, bool choose_obj, double prec=0,
                  double ratio=Bsc::default_ratio());
```

---

## The smear formulas — verified from `ibex_SmearFunction.cpp`

The "smear" of variable `j` in constraint `i` is the **Jacobian-weighted width**
`|J[i][j]| · diam(box[j])` (`.mag()` of the interval Jacobian entry times the box width)
— an estimate of how much variable `j`'s uncertainty propagates into constraint `f_i`.
The four variants differ only in how they aggregate this over constraints. `J` is the
interval Jacobian of `sys.f_ctrs` at the box (`ibex_SmearFunction.cpp:66-68`); variables
with `too_small` are skipped, and the whole bisector **falls back to its internal
`LargestFirst`** if any Jacobian entry is infinite or the chosen variable is
non-bisectable (`ibex_SmearFunction.cpp:71-88`).

| Variant | `var_to_bisect` rule (verbatim from `.cpp`) | Source |
|---|---|---|
| **SmearMax** (Kearfott 1990) | `argmax_j  max_i ( \|J[i][j]\|·diam(box[j]) )` — the single greatest impact, any constraint. | lines 92-107 |
| **SmearSum** (Hansen) | `argmax_j  Σ_i ( \|J[i][j]\|·diam(box[j]) )` — the greatest *sum* of impacts over constraints. | lines 110-127 |
| **SmearSumRelative** | `argmax_j  Σ_i ( \|J[i][j]\|·diam(box[j]) / NC_i )`, where `NC_i = Σ_k \|J[i][k]\|·diam(box[k])` is the per-constraint normalizer. Each constraint's contributions sum to 1, so **every constraint weighs equally** regardless of its raw magnitude. | lines 129-162 |
| **SmearMaxRelative** | intended: `argmax over i,j ( \|J[i][j]\|·diam(box[j]) / NC_i )` — the greatest *normalized* single impact. ⚠️ source observation: the `.cpp` loop (164-194) keeps `maxsmear` declared outside the `j`-loop and runs the `>max_magn` test outside the `constraint_to_consider` / `ctrjsum!=0` guards, so a stale `maxsmear` can leak across iterations — the implementation does not cleanly realize the documented "max" semantics. | lines 164-194 |

`constraint_to_consider` (`.cpp:45-48`) makes these optimization-aware: for a plain
`System` (`goal_ctr()==-1`) **all** constraints count; inside an `ExtendedSystem` an
inactive `≤`/`<` constraint (whose `eval(box).ub() < 0`) is dropped, and the objective
row can be skipped via `goal_to_consider` when it merely equals a variable.

---

## Comparison table

`prec` = skip-if-narrower threshold; `ratio` default `0.45` throughout (verified). "needs
a `System`?" = whether the bisector requires an assembled `ibex::System` (to compute a
Jacobian) beyond the bare box.

| Bisector | Selection strategy (the actual rule from source) | Constructor + params (defaults) | Strength | Weakness | needs a `System`? | dReal status | Recommendation |
|---|---|---|---|---|---|---|---|
| **LargestFirst** | split the widest component: `argmax_i diam(box[i])` (uniform `prec`), or `argmax_i diam(box[i])/prec(i)` with a `Vector` prec (`ibex_LargestFirst.cpp:31-60`). | `(prec=0, ratio=0.45)` | zero overhead; no system/Jacobian; robust default. | ignores constraint structure — wide-but-irrelevant variables get split. | no | unused | reference only; closest analog to a plain dimension-width heuristic. |
| **RoundRobin** | cycle through indices: `var=(last_var+1)%n`, skipping `too_small`, using `cell.bisected_var` (`ibex_RoundRobin.cpp:26-47`). | `(prec, ratio=0.45)` — **`prec` mandatory** | dead simple; guarantees every variable is eventually split (fairness/completeness of paving). | constraint-blind; usually slower convergence than smear. | no | unused | reference only; needs the `Cell` to carry `bisected_var` (search-tree state dReal lacks). |
| **SmearMax** | `argmax_j max_i (\|J[i][j]\|·diam(box[j]))` (`.cpp:92-107`). | `(System&, prec, ratio=0.45)` | targets the variable with one dominant constraint impact. | a single large entry dominates; ignores how broadly a variable matters. | yes | unused | idea only; `Bsc` class is tied to IBEX `Cell`/`Solver`. |
| **SmearSum** | `argmax_j Σ_i (\|J[i][j]\|·diam(box[j]))` (`.cpp:110-127`). | `(System&, prec, ratio=0.45)` | rewards variables that matter across many constraints. | large-magnitude constraints swamp small ones (no normalization). | yes | unused | idea only. |
| **SmearSumRelative** | `argmax_j Σ_i (\|J[i][j]\|·diam(box[j]) / NC_i)`, `NC_i=Σ_k \|J[i][k]\|·diam(box[k])` (`.cpp:129-162`). | `(System&, prec, ratio=0.45)` | normalized → constraints weigh equally; the most balanced smear, IBEX's go-to. | per-box Jacobian + normalizer cost; needs the assembled system. | yes | unused (class) | **formula shipped in dReal**: [`CtcAcid`](../contractors/CtcAcid.md)'s `smearsumrel` shave-ordering and the `--smear` brancher (`brancher_smear.cc`) both use this criterion. |
| **SmearMaxRelative** | `argmax (\|J[i][j]\|·diam/NC_i)` (`.cpp:164-194`; see ⚠️ above). | `(System&, prec, ratio=0.45)` | normalized single-impact target. | implementation quirk (stale `maxsmear` leak); rarely used. | yes | unused | not recommended even in IBEX context — prefer SmearSumRelative. |
| **LSmear** (Araya & Neveu) | dual-weighted smear: solve LP relaxation `mid(J)·x≤0`, weight each constraint by its dual multiplier, then smear; falls back to `SmearSumRelative` when the LP isn't optimal/bounded (`ibex_LSmear.cpp:111-169`). Default mode `LSMEAR_MG` linearizes the Jacobian at the inflated midpoint. | `(ExtendedSystem&, prec, ratio=0.45, lsmode=LSMEAR_MG)` | best-performing selector in its paper for optimization B&B; focuses on constraints active at the optimum. | needs an **`ExtendedSystem` + an LP solver** (`LPSolver`); heaviest per-node cost. | yes (+ LP) | unused | **optimization-only** — needs an `ExtendedSystem` (a goal var) that dReal's sat/unsat search never builds. Not a build limitation: dReal links `LP_LIB=soplex`, so the LP backend is present (the earlier "`LP_LIB=none`" claim was stale). |
| **OptimLargestFirst** | LargestFirst over all non-objective vars; bisects the objective `goal_var` only under special guards: `choose_obj && bisectable && l < diam(goal) && diam(goal)/l < 1e10` (`ibex_OptimLargestFirst.cpp:28-63`). | `(goal_var, choose_obj, prec=0, ratio=0.45)` | keeps the optimizer from wastefully splitting the objective variable. | optimization-specific; meaningless without a goal variable. | no (but needs `goal_var` index) | unused | optimization-only; no shape in dReal's sat/unsat search. |

---

## Can dReal use these?

**No — not the `Bsc` classes themselves; at most the smear *criterion* as a formula.**
The `Bsc` hierarchy is welded to IBEX's tree-search machinery: `choose_var` consumes a
`Cell` (RoundRobin reads `cell.bisected_var`), signals the precision frontier by throwing
`NoBisectableVariableException`, and is driven by an IBEX `Solver`/`Optimizer` cell buffer
— none of which dReal has. dReal's branching lives **inside DPLL(T)**: the SAT layer makes
Boolean decisions and the theory solver's ICP bisects boxes, and that variable choice is
already its own theory-driven heuristic. So the IBEX bisector *objects* don't plug in; this
is exactly why [`../../AUDIT.md`](../../AUDIT.md) tier E records bisectors as "correctly
ignored." (Architecture contrast in detail:
[`../../ARCHITECTURE-COMPARISON.md`](../../ARCHITECTURE-COMPARISON.md).)

What *was* portable is the **smear formula** — `Σ_i |J[i][j]|·diam(box[j]) / NC_i` — as a
ranking over box dimensions, and dReal now **ships** it. `--smear` (`brancher_smear.cc`)
assembles its own `ibex::System` over the joint variables, reads the interval Jacobian via
`System::f_ctrs.jacobian`, and reimplements all four SmearFunction variants — the
`relative` (normalize by `NC_i`) and `use_max` (max-vs-sum) axes — directly over box
dimensions, falling back to largest-first on an uninformative/non-finite Jacobian exactly as
IBEX's `SmearFunction` does. The `Bsc` *class* is bypassed; only the formula is borrowed.
[`CtcAcid`](../contractors/CtcAcid.md) independently uses the same `smearsumrel` criterion to
order its shaved variables. Both are **completeness** levers (better split/shave order →
fewer nodes / more refutations), never soundness ones: variable *choice* cannot make a sound
contractor unsound, only change how fast the search converges. `--smear` is measured strongest
as `smearsum` on the odeexpr families but collapses the ODE families corpus-wide (the Jacobian
skips `Kind::ODE_LOHNER`) — enable per-project, not globally (dReal `CLAUDE.md` §Smear).

Related: [strategy chapter](../../chapters/strategy.md),
[`../contractors/CtcAcid.md`](../contractors/CtcAcid.md), [`../../AUDIT.md`](../../AUDIT.md),
[`../../ARCHITECTURE-COMPARISON.md`](../../ARCHITECTURE-COMPARISON.md).

---

### Source-fidelity flags

- `default_ratio()` = **0.45** (constexpr-equivalent literal in `ibex_Bsc.cpp:20`),
  matching the header doc-comment. The *chapter* prose equates it with the midpoint `0.5`
  — that is the conflation flagged above; `0.45 ≠ 0.5`.
- **LSmear citation year:** `ibex_LSmear.h:34-36` cites *Araya, I., Neveu, B. — lsmear …
  (**2017**)*. The strategy chapter / task brief say "2018" — the **header says 2017**;
  treat 2017 as the source-of-record (the work has both a 2017 and a later journal date;
  unverified which the brief intends).
- **SmearMaxRelative** is a real 4th concrete variant (the `SmearFunction` doc-comment
  itself names all four: `ibex_SmearFunction.h:34`), included here for completeness; its
  `.cpp` aggregation loop has the stale-`maxsmear` quirk noted in the smear table.
