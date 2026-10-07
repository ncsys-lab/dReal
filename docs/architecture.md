# dReal4 Architecture

## Overview

dReal4 is a delta-complete decision procedure for nonlinear arithmetic over the reals (QF_NRA) and systems with ordinary differential equations (QF_NRA_ODE). "Delta-complete" means: given a formula φ and a precision δ > 0, the solver either proves φ is unsatisfiable or finds a witness that satisfies a δ-weakening of φ (every atomic constraint is relaxed by at most δ).

The solver implements a variant of DPLL(T) where the SAT layer handles propositional structure and the theory layer handles nonlinear arithmetic via interval constraint propagation.

> **Origin & paper map.** This `DPLL(ICP)` architecture is the one introduced in the original
> dReal tool paper (Gao, Kong, Clarke, CADE-24 2013 —
> `papers/gao-kong-clarke-2013-dreal.md`), built on the δ-complete decision procedures of
> `papers/gao-avigad-clarke-2012-delta-complete.md`. **The framework is unchanged; the
> components are not:** the 2013 paper's **opensmt** (DPLL(T)) and **realpaver** (ICP) have
> been replaced by **CaDiCaL**, the **IBEX** fork, and **CAPD** (ODE). Read the 2013 summary's
> Notes before taking any component claim in that paper as current.

---

## Layers

```
 ┌──────────────────────────────────────────────────┐
 │  Input (SMT2 / dReal .dr)                        │
 │  src/dreal/smt2/driver.h, src/dreal/dr/driver.h  │
 └───────────────────────┬──────────────────────────┘
                         │
 ┌───────────────────────▼──────────────────────────┐
 │  Context  (src/dreal/solver/context.h)            │
 │  Manages Assert / CheckSat (push/pop rejected —   │
 │  formally unsupported, docs/decisions.md)         │
 └───────────────────────┬──────────────────────────┘
                         │
 ┌───────────────────────▼──────────────────────────┐
 │  Preprocessing  (src/dreal/util/)                 │
 │  ITE elimination → NNF → Tseitin CNF              │
 │  → predicate abstraction                          │
 └───────────────────────┬──────────────────────────┘
                         │
           ┌─────────────┴──────────────┐
           │                            │
 ┌─────────▼─────────┐        ┌─────────▼──────────┐
 │  SAT Layer        │        │  Theory Layer       │
 │  CaDiCaL 3.0.0    │◄──────►│  TheorySolver +     │
 │  Boolean CDCL     │  DPLL  │  ICP (seq/parallel) │
 └───────────────────┘   (T)  └─────────────────────┘
                                        │
                         ┌──────────────▼─────────────┐
                         │  Contractors                │
                         │  IBEX fwdbwd, polytope,     │
                         │  fixpoint, ODE (CAPD),      │
                         │  forall, seq, join          │
                         └────────────────────────────┘
```

---

## Preprocessing Pipeline

Before the solver sees a formula it goes through four passes (in `context_impl.cc`):

1. **ITE elimination** (`if_then_else_eliminator.h`): Rewrites `ite(c, t, e)` nodes into equivalent formulas without branching expressions, because IBEX and the contractor layer cannot handle ITE directly.

2. **NNF** (Negation Normal Form, `nnfizer.h`): Pushes negations inward so that no `Not` nodes appear above atoms. Required for Tseitin.

3. **Tseitin CNF** (`tseitin_cnfizer.h`): Converts the NNF formula to CNF by introducing fresh Boolean variables for each sub-formula. This is the standard structure-preserving CNF transformation — it keeps formula size linear at the cost of auxiliary variables.

4. **Predicate abstraction** (`predicate_abstractor.h`): Each nonlinear atomic formula (e.g., `x² + y² ≤ 1`) becomes a fresh Boolean variable `b_i`. The SAT solver reasons only over these Boolean variables; the theory solver checks that a Boolean assignment corresponds to a geometrically consistent region.

---

## SAT Layer

**File:** `src/dreal/solver/sat_solver.cc` (core + CaDiCaL `Learner` callbacks) and `sat_solver_drpm.cc` (DRPM lemma pattern matching + CAV26 symmetry filter) / `sat_solver_model_logic.cc`

CaDiCaL is used as the Boolean CDCL solver. Two modes exist:

- **Interval logic** (`--logic QF_NRA`): The SAT solver guesses which nonlinear predicates to assert (true/false) and the theory solver checks consistency.
- **Model logic** (default for `--produce-models`): Slightly different bookkeeping to preserve witness information.

The `CaDiCaL::Learner` interface is used by the pattern-matching layer (CAV26 research) to inject learned clauses derived from previously solved subproblems.

---

## Theory Layer

**File:** `src/dreal/solver/theory_solver.h`

`TheorySolver::CheckSat(box, assertions)` is the main entry point. It:

1. Builds a contractor for each asserted formula (or retrieves it from the contractor cache).
2. Composes contractors with `ContractorFixpoint` or `ContractorSeq`.
3. Delegates to an `Icp` implementation (sequential or parallel) to run the ICP loop.
4. Returns `true` (delta-SAT with model in `model_`) or `false` (UNSAT with explanation in `explanation_`).

Caches are keyed by formula identity (`FormulaUnorderedMap<Contractor>`). Three separate caches exist for forward ODE contractors, backward ODE contractors, and constraint contractors.

---

## ICP Loop

**Files:** `src/dreal/solver/icp.h`, `icp_parallel.cc`

Interval Constraint Propagation is a branch-and-prune search:

```
procedure ICP(box B, contractor C, formula_evaluators FE):
  while true:
    B' ← C.Prune(B)          // shrink domains
    result ← Evaluate(FE, B') // check all formulas over B'
    if result = UNSAT:  return UNSAT
    if result = δ-SAT:  return SAT, witness B'
    // still undecided: branch on a variable
    dim ← select_branch_dimension(FE, B')
    [B_left, B_right] ← split(B', dim)
    push B_right onto worklist
    B ← B_left
```

`FormulaEvaluator` implements the three-valued evaluation:
- **None** (⊥): some formula evaluates to the empty interval — UNSAT.
- **Some(∅)**: all formulas are either valid or within δ — delta-SAT.
- **Some(Vars)**: some formula is undecided and its variables must be branched.

One implementation (`icp_parallel.cc`) serves every `--jobs`: workers share a lock-free stack of boxes; `--jobs 1` spawns zero pool workers, so the calling thread runs the single worker deterministically (the separate sequential loop `icp_seq.cc` was deleted in the 2026-07 R4 consolidation after a measured-parity gate — `icp_parity_gaps.md`).

---

## Branching

**File:** `src/dreal/solver/brancher.h`, `src/dreal/solver/brancher_smear.h`

When the ICP loop cannot determine satisfiability, it picks a dimension to bisect the current box. The default strategy (`BranchLargestFirst`) branches on the variable with the largest interval width among those implicated by undecided formulas.

**Followed integral durations first.** While the duration of an integral whose end state starts another integral (same variable) can still be split, such durations are the only candidates, for whichever brancher is configured (`FollowedDurations`, `icp_parallel.cc`). Without it a free duration loses to wider derived signals and ODE queries stall (s2d PERF-001). Rationale and measurements: `docs/decisions.md` §"Branching on followed integral durations first".

**Constraint-aware "smear" branching (`--smear <variant>`).** Opt-in: instead of widest-first, score each variable by its Jacobian-weighted impact over the relational constraints and split the highest. `SmearBrancher` assembles an `ibex::System` once from the relational formula evaluators **plus each `forall`'s body leaves** (so the ∃∀ existential variables get a constraint-aware score: universal vars enter as extra columns pinned at their binder intervals, recovered like `ContractorIbexForall`, and are never scored/split). ODE/`forall_t` constraints are still skipped (`IbexConverter` throws on them). Because the `IbexConverter` is built from the joint variable set (box dims first), Jacobian column `j` maps 1:1 to box dimension `j` for `j < box.size()`. The flag selects one of IBEX's four `SmearFunction` variants along two binary axes — aggregate over constraints by **sum** vs **max**, absolute impact `|J_ij|·diam_j` vs **relative** (normalized by the per-constraint row-sum `NC_i = Σ_k |J_ik|·diam_k`):

| `--smear` value | criterion |
|---|---|
| `smearsumrel` | `argmax_j Σ_i |J_ij|·diam_j / NC_i` (IBEX's go-to) |
| `smearsum` | `argmax_j Σ_i |J_ij|·diam_j` (Hansen) |
| `smearmax` | `argmax_j max_i |J_ij|·diam_j` (Kearfott) |
| `smearmaxrel` | `argmax_j max_i (|J_ij|·diam_j / NC_i)` |

(IBEX's fifth smear bisector, `LSmear`, is optimization-only — it reweights constraints by the LP dual of an objective — so it is inapplicable to dReal's feasibility queries.) When the Jacobian is uninformative (no relational constraints, all-zero or infinite entries) it falls back to largest-first, matching IBEX. **Soundness:** the split-variable choice only changes how fast the search converges (node count), never a verdict — a completeness/perf lever. Works at every `--jobs` (one `SmearBrancher` per worker — the brancher is not thread-safe to share). On the odeexpr families `smearsum` is the strongest variant (on the ∃∀ `exists_forall` subfamily `smearsum`≈`smearsumrel`, both > the max variants); see `OPTIMIZATION_LOG.md` §odeexpr.

---

## Box

**File:** `src/dreal/util/box.h`

`Box` is the central data structure — a map from `Variable` to `ibex::Interval`. Internally it wraps `ibex::IntervalVector` and maintains a parallel `Variable` index for named access. Operations:

- `operator[](Variable)` — access interval by variable name
- `operator[](int)` — access by index
- `bisect(int dim)` — split box along dimension `dim`, returns `(left, right)` pair
- `MaxDiam()` — index of widest interval (used by brancher)
- `empty()` — whether any interval is empty (UNSAT witness)

Box copies are the main per-node allocation in ICP. The lambda-callback optimization in `contractor_ibex_fwdbwd.cc` avoids gratuitous copies during constraint building.

---

## Contractor Status

**File:** `src/dreal/contractor/contractor_status.h`

`ContractorStatus` threads a mutable `Box` plus bookkeeping (explanation set, active constraints) through the contractor pipeline. Contractors read from and write to the `ContractorStatus` rather than returning new boxes, which avoids allocation.

---

## Explanation / UNSAT Witnesses

When the ICP loop returns UNSAT, the `explanation_` set in `TheorySolver` contains the minimal subset of asserted formulas that jointly imply infeasibility. This explanation is returned to the SAT layer as a learned clause, causing CaDiCaL to backtrack and try other assignments.

The quality of explanations (their minimality) directly impacts solver performance because it determines how many future SAT assignments are pruned.

---

## Pattern Matching / Lemma Reuse (CAV26)

See `docs/pattern-matching.md` for full details. At a high level: when the theory solver produces an UNSAT explanation, the pattern matcher checks whether this explanation is structurally similar (up to variable renaming) to previously seen explanations. If so, it injects a generalized lemma into the SAT solver via `CaDiCaL::Learner`, potentially pruning exponentially many future search paths.

---

## ODE Support

See `docs/ode-integration.md` for details. ODE constraints (`d/dt[x] = f(x,t)` combined with `Integral` and `ForallT` AST nodes) are handled by `contractor_ode_lohner`, which integrates with CAPD's `IOdeSolver` (order-20 Taylor) + `ITimeMap` — the sole ODE backend — to compute guaranteed interval enclosures of ODE trajectories.
