# `forall` Semantics in dReal4

> **⚠ PITFALL — `forall` ≠ `forall_t` (`forall-vs-forall_t`).** Independent machinery that
> only share a prefix: **`forall`** is the ∃∀ NRA quantifier (this document); **`forall_t`**
> is the ODE trajectory invariant (`qf_nra_ode_semantics.md` §5). Full side-by-side in §7.
> Never substitute one for the other.

This document covers the standard SMT2-LIB `forall` quantifier (`Formula::Forall`,
`Kind::FORALL`, handled by `ContractorForall`) — **not** the ODE-specific `forall_t`.

Every claim is cross-referenced to the parser (`src/dreal/smt2/parser.yy`, `scanner.ll`),
the CNFizer (`src/dreal/util/tseitin_cnfizer.cc`), the contractor
(`src/dreal/contractor/contractor_forall.h`), the formula evaluator
(`src/dreal/solver/forall_formula_evaluator.cc`), and the theory solver
(`src/dreal/solver/theory_solver.cc`).

---

## 1. Supported Fragment: ∃∀ Only

dReal supports the **∃∀ alternation** in `QF_NRA`. The shape of every satisfiable query is:

```
∃ x₁,...,xₙ.  ∀ y₁,...,yₘ.  φ(x, y)
```

- **Existential variables** (`x`): declared at top level with `declare-fun`.
  Conceptually bound by the implicit `∃` of the SMT2 `check-sat` command.
- **Universally quantified variables** (`y`): declared inside the `forall` binder.
  Their domain must be specified with `[lb, ub]` bounds (see §2.2).

**Depth-one only.** A second quantifier alternation — `∀∃`, `∃∀∃`, nested `∀∀` — is not
supported and will crash at contractor construction time (see §6).

The logic is declared as:
```smt2
(set-logic QF_NRA)
```
There is no dedicated `NRA` or `NRA_ALL` tag. The `forall` token is recognized regardless of
the declared logic.

---

## 2. Syntax

### 2.1 Grammar

From `parser.yy:381`:

```
term:
    '(' TK_FORALL enter_scope '(' variable_sort_list ')' term exit_scope ')'
    {
        const Variables& vars   = $5.first;
        const Formula& domain   = $5.second;
        const Formula body      =
            Smt2Driver::EliminateBooleanVariables(vars, $7.formula());
        const Variables qvars   = intersect(vars, body.GetFreeVariables());
        if (qvars.empty())
            $$ = body;
        else
            $$ = forall(qvars, imply(domain, body));
    }
```

Example:

```smt2
(set-logic QF_NRA)
(declare-fun x () Real [0, 8])
(assert
  (forall ((y Real [0, 7.5]))
    (or (<= (+ (^ (- x 2) 2) (^ (- y 2) 2)) 9)
        (<= (+ (^ (- x 5) 2) (^ (- y 5) 2)) 9))))
(check-sat)
```

Multiple universally-quantified variables are listed in the same binder:

```smt2
(forall ((x2 Real [-32.0, 32.0]) (y2 Real [-32.0, 32.0]))
  (<= z (+ (* x2 x2) (* y2 y2))))
```

### 2.2 Bounds encoding

`variable_sort_list` (`parser.yy:593`) converts each `(y Real [lb, ub])` declaration into:
- A new `Variable` registered in the current scope
- A domain formula `(lb ≤ y) ∧ (y ≤ ub)` accumulated into `domain`

At the `forall` production, the body is wrapped as `domain ⟹ body` via `imply(domain, body)`.
Variables declared without bounds get `[-∞, +∞]` and the domain contribution drops to `true`.

**The `[lb, ub]` binder domain is input sugar, not retained.** After this desugaring a
`FormulaForall` holds only its quantified `Variables` and the fused body `imply(domain, matrix)`
(`symbolic_formula_cell.h:335`, members `vars_` + `f_`); there is no separate per-variable domain.
Consequences: (a) printing a `forall` back out — e.g. the auditor's `ToPrefix`
(`prefix_printer.cc::VisitForall`) — emits the **desugared** `(forall ((y Real)) <imply-form>)`
with an *unbounded* binder and the domain folded into the body; this round-trips exactly, because
an unbounded binder re-desugars to `domain = true`, leaving the stored body unchanged. The
`[lb, ub]` in-binder form cannot be reconstructed from a `FormulaForall` and is not meant to be.
(b) The pattern-matcher likewise sees only the free (existential) variables — the bound ones are
excluded from De Bruijn canonicalization (`docs/pattern-matching.md`).

**Practical requirement**: always supply bounds on forall variables. Without bounds the CE
search ranges over (-∞, +∞). If a solution with no counterexamples can be found quickly
(e.g., `x = 0` trivially satisfies `∀y. x·y ≤ 1`), the solver may still terminate. But
for problems where every candidate `x` has a counterexample at large `|y|`, ICP bisection
on the unbounded forall domain will not terminate.

### 2.3 Boolean variable elimination

`EliminateBooleanVariables(vars, body)` case-splits any Boolean variables in `vars` out of the
body before constructing the `forall` node. This is a parse-time rewrite, not a solver step.

### 2.4 Dead-variable pruning

If after `EliminateBooleanVariables` none of the declared variables actually appear free in
the body, the `forall` node is dropped and the body formula is returned directly
(`if (qvars.empty()) $$ = body`). The `forall` is a no-op when the body does not depend on
the quantified variables.

---

## 3. CNF Distribution

The Tseitin CNFizer (`tseitin_cnfizer.cc:126`) distributes `∀` over CNF clauses:

```
∀y. φ(x, y)
    = ∀y. (clause₁(x,y) ∧ ... ∧ clauseₙ(x,y))    [φ CNF-ized by NaiveCnfizer]
    = (∀y. clause₁(x,y)) ∧ ... ∧ (∀y. clauseₙ(x,y))
```

Clauses that do not mention any `y` variable are emitted without the quantifier. If distribution
yields more than one clause total, Tseitin introduces a fresh Boolean variable `forallN`
representing their conjunction and records the relationship in `map_`.

**Effect on the body structure**: each `ContractorForall` sees a single clause as its
quantified formula, not the original conjunction. Multiple forall constraints map to
multiple `ContractorForall` instances in the fixpoint loop.

---

## 4. Contractor Algorithm and δ-Completeness

> **Paper map.** This CE-guided loop is the algorithm of Kong, Solar-Lezama & Gao,
> *Delta-Decision Procedures for Exists-Forall Problems over the Reals* (CAV 2018 —
> `papers/kong-solar-lezama-gao-2018-exists-forall.md`). The three δ levels in §4.1 are that
> paper's **double-sided error control** (§3.2): the chain `inner_delta < epsilon < delta`
> instantiates the paper's `δ' < ε < δ`, which exists to reject *spurious counterexamples*. The
> `--local-optimization` refiner (§6.7) is its §3.3 locally-optimized counterexamples.

`ContractorForall<ContextType>` (`contractor_forall.h`) implements a
**counterexample-guided (CE) pruning loop**:

```
Problem:
  Given box B (over existential vars x) and formula ∀y∈D. φ(x, y),
  reduce B to B' ⊆ B.

Algorithm:
  repeat
    1. Fix x ← B. Solve strengthen(¬φ, ε) ∪ {y ∈ D}  at precision inner_δ.
       (= find a y in D that violates φ for some x in B)
    2. If no CE found: B is consistent with ∀y∈D.φ(x,y). Break.
    3. If CE (a,b) found:
         Instantiate: φ(x, b)   [y fixed at midpoints of CE box]
         Contract:    B' ← Contract(φ(x,b), B)
         If B' = ∅: set B = ∅, break.
         B ← B'; continue.
  until fixed-point
```

### 4.1 Three δ levels — and two regimes

The CE search is governed by three precisions in the strict order
`inner_delta < epsilon < delta`, enforced by four `DREAL_ASSERT`s (`contractor_forall.h:91–94`;
the `@pre` is at `:78`). They map onto the CAV 2018 paper's
`δ' < ε < δ` — the **double-sided error control** (§3.2) — exactly:

| Paper symbol | Code name | Role |
|---|---|---|
| `δ` (outer precision) | `config.precision()` / `delta` | the user's `--precision`; the band the final answer is relaxed by |
| `ε` (CE strengthening) | `epsilon` | amount `¬φ` is strengthened by, so a found CE is *genuinely* violating (not a boundary artifact) |
| `δ'` (CE-search precision) | `inner_delta` | precision of the nested δ-decision that searches for a CE |

**Two regimes for the same constraint.** The `forall` constraint is handled by two
components that pick *different* values inside this ordering:

| Component | `epsilon` | `inner_delta` | Role | Site |
|---|---|---|---|---|
| **`ContractorForall`** | `delta * 0.5` | `epsilon * 0.5` (= `delta/4`) | in-loop **pruner** (the `Prune` step) | `theory_solver.cc:189–191` |
| **`ForallFormulaEvaluator`** (§5) | `0.99 * delta` | `0.99 * epsilon` (= `0.9801·delta`) | in-loop **δ-SAT accept + branch** test (the `EvaluateBox` step) | `theory_solver.cc:320–322` |

**Both run *inside* the ICP branch-and-prune loop, on every box** — the contractor in the
`Prune` step, the evaluator in the `EvaluateBox` step immediately after (both in
`icp_parallel.cc` `Worker`). Neither is a pre-solve or a post-solve /
pre-print phase; the evaluator is *not* a final model re-check — the box it first accepts
*is* the returned δ-SAT model (§5).

The evaluator's `ε = 0.99δ` is the CAV 2018 experimental value verbatim (paper §5, p. 229:
"`ε = 0.99δ` … `δ' = 0.98δ`"); its `inner_delta` applies the same `0.99` ratio twice
(`0.9801·δ`, the paper's `0.98δ` up to that rounding). The contractor is deliberately
**tighter** (`δ/2, δ/4`) — a dReal choice, *not* the paper's experimental constants. Both
satisfy `δ' < ε < δ`, so both are δ-complete instantiations (§4.5) — the paper proves
completeness for *any* point in that open region. The direction of the difference is
deliberate: a **larger** `ε` strengthens the CE query more, so a counterexample must
violate by a bigger margin to be found — the evaluator (`0.99δ`) is therefore the more
**lenient** of the two (it accepts a box as satisfying `∀y` unless a near-full-`δ` violation
exists), while the contractor (`δ/2`) prunes on any half-`δ` violation. (The class doc-comment at
`contractor_forall.h:56–60` states the ordering directly: `Solve(domain ∧ strengthen(¬body,
ε), inner_delta)` with `inner_delta < ε < precision`.)

The CE-search context is built once at contractor construction
(`context_for_counterexample_`) with `precision = inner_delta` (`contractor_forall.h:101–102`),
the strengthened query built at `:85–88` and asserted conjunct-by-conjunct at `:116–124`. On
each `Prune` it re-sets the existential intervals and calls `CheckSat`.

### 4.2 Stacking alternation

`Prune` alternates `stack_left_box_first` on the CE-search context on each call:
```cpp
config_for_counterexample.mutable_stack_left_box_first() =
    !config_for_counterexample.stack_left_box_first();
```
This diversifies the search direction of the nested ICP between contractor invocations.

### 4.3 CE instantiation detail

When a CE box is found, the forall variables are pinned to the **midpoints** of their CE
intervals (`safe_mid(counterexample[forall_var], ur)`) and the existential-variable ranges from
the CE box are replaced with the current outer box values. This produces a point-instantiated
constraint `φ(x, b_mid)` that the inner `contractor_` (over existential variables only) uses
to narrow `B`.

### 4.4 Multithreaded variant

`ContractorForallMt` (`contractor_forall.h`) wraps `ContractorForall` with `PerThread<T>`
(`util/per_thread.h`, §4.8), creating one `ContractorForall` instance per worker thread.
The inner CE-search context forces `number_of_jobs = 1` regardless of the outer parallelism
setting.

### 4.5 δ-Completeness guarantee

The CE-guided procedure is **δ-complete** for the ∃∀ (`CNF^∀`) fragment: for any
`δ ∈ ℚ⁺` it terminates and returns either `unsat` (φ is unsatisfiable) or `delta-sat`
(the δ-weakening φ⁻ᵟ is satisfiable). This is Theorem 1 of CAV 2018 (p. 229), which
chains two results:

1. **The ∀-clause pruning operator is well-defined** (CAV 2018 Lemma 1): it satisfies the
   three conditions of a sound ICP pruning operator — contraction, never discards a real
   solution, non-empty results are faithful — from the IJCAR 2012 paper (W1–W3,
   `papers/gao-avigad-clarke-2012-delta-complete.md`).
2. **A branch-and-prune procedure with well-defined operators is δ-complete** (IJCAR 2012
   Thm 4.2 / Cor 4.1).

So `forall` inherits δ-completeness from the *same* theorem that covers the purely
existential `QF_NRA` contractor; the ∀-clause is "just another" well-defined pruning
operator, parameterized over the quantified variables. The guarantee covers both
components of §4.1 (the `ContractorForall` and the `ForallFormulaEvaluator`), since each
uses a valid `δ' < ε < δ` instantiation.

### 4.6 Why the three levels are mandatory: the spurious-counterexample hazard

The strengthening `ε` is not a tuning knob — it is what makes the procedure complete. The
CE search is itself a δ-decision (it can only certify `φ⁻ᵟ'`, never φ exactly), so without
strengthening it can return a **spurious counterexample**: a point `b` with
`⋀ᵢ fᵢ(a, b) ≤ δ'` instead of the genuine `< 0` (CAV 2018 §3.2). A spurious CE does not
actually violate the clause, so `Contract(φ(x, b), B)` prunes nothing — the fixpoint loop
(`Prune`, `contractor_forall.h:197–242`) stalls after one iteration. The outer
branch-and-prune is then left to terminate on **branching alone**, and can report
`delta-sat` with a box `‖B‖ ≤ δ` that contains *no* δ-solution (a missed refutation).

The fix (CAV 2018 §3.2, Eqs 2–3) is to strengthen the CE query by `ε` and solve it at
`δ'`, with **`δ' < ε`** (so a returned CE clears the strengthening and is genuine) and
**`ε < δ`** (so the strengthening stays inside the user's band). That is precisely the
`inner_delta < epsilon < delta` invariant of §4.1, and the reason for the ε-strengthened CE
query (`StrengthenForallCounterexampleQuery`, `contractor_forall.h:85–88`; body-only, §4.8).

### 4.7 Soundness vs. completeness of `forall` failure modes

Every way the `forall` machinery can go wrong is a **completeness** failure, never a
**soundness** one — consistent with the project-wide principle that over-approximating
contractors cannot produce a false `unsat` (`docs/soundness-vs-completeness.md`):

| Failure | Effect | Class |
|---|---|---|
| Spurious CE accepted (no `ε`-strengthening) | fixpoint stalls → `delta-sat` on a box with no δ-solution | **COMPLETENESS** (asserts φ⁻ᵟ T-satisfiable on a T-unsatisfiable φ⁻ᵟ — missed refutation) |
| Inner CE search misses a real CE (under-powered nested solve) | an invalid candidate `x` survives → `delta-sat` on an `x` that has a counterexample | **COMPLETENESS** (missed refutation) |
| CE search times out / over-branches | premature `delta-sat` with a small box | **COMPLETENESS** |

The reason no failure mode is a soundness hazard: the pruning step contracts `B` with
`φ(x, b_mid)` for a **real** point `b_mid` in the forall domain (§4.3). Since
`∀y. φ(x, y)` *requires* `φ(x, b_mid)`, removing the `x` that violate it can never delete
a true ∃∀-solution — it is a sound over-approximation no matter how the CE was found. A
bad CE only ever costs progress (refutation power), i.e. completeness. The double-sided
error control of §4.6 is therefore a **completeness safeguard**, not a soundness one.

### 4.8 2026-06 hardening (matrix-only strengthening, point-elimination, rejection, per-thread)

A soundness/robustness audit of `forall` and its interaction with parallel ICP and the
`--forall-pre-prune` pre-pruner produced four changes. All preserve soundness; corpus verdicts
are unchanged (23/24 forall instances; `ea_abs` is a pre-existing timeout) and the only
intended behavior changes are stronger *refutation* (completeness), never a new false `unsat`.

- **Narrow-domain-conditional CE strengthening (completeness fix).** The CE query was built as
  `DeltaStrengthen(¬(domain → body), ε) = DeltaStrengthen(domain ∧ ¬body, ε)`, which
  ε-tightens the **universal-domain** bound atoms too (`VisitConjunction` strengthens every
  conjunct). A universal domain narrower than ~2ε was searched as the empty set, so a real
  counterexample was missed and the forall was reported consistent (false `delta-sat`; never
  false `unsat`). The fix keeps the domain bounds **exact** — but only for **narrow** universal
  variables (binder width `< 3ε`, where the ε-shrink would empty/degenerate the domain); **wide**
  variables keep the old, faster ε-shrink. Rationale: keeping *every* domain exact (full
  CAV-2018) is correct but **9–45× slower at moderate δ** on wide-domain ∃∀ families (odeexpr_v2,
  `exists_forall_perf.md`) for **no verdict change**, because the exact domain enlarges the CE
  search; the residual wide-domain shell gap is bounded, pre-existing, and never a soundness
  issue. Shared helper `StrengthenForallCounterexampleQuery`
  (`contractor/forall_counterexample_query.{h,cc}`), used by **both** the contractor and the
  evaluator so the two cannot drift. COMPLETENESS-only. (The `width==0` point case is handled by
  point-quantifier elimination, next bullet.)

- **Point-quantifier elimination.** Even with exact bounds, a universal variable pinned to a
  single point `c` (lb == ub) cannot be refuted: dReal soundly over-approximates the strict
  domain-negation `(y > c) ∨ (y < c)` with **closed** intervals, so neither disjunct empties at
  the measure-zero boundary. `Context::Assert` now substitutes `y := c` into the matrix and
  drops the quantifier (an all-point forall reduces to its quantifier-free body) —
  `EliminatePointUniversals` (`solver/context_impl.cc`). COMPLETENESS-only.

- **Unsupported-forall rejection.** A negated / nested / sign-flipped `forall` (the SAT layer
  can flip a forall-Boolean to false under a disjunction) previously crashed with an opaque
  error deep in contractor construction (the `ibex_converter` / `DeltaStrengthen` VisitForall
  throws). `Context::Assert` now rejects it early and clearly — `HasForall` /
  `RejectUnsupportedForall` (`solver/context_impl.cc`): *"dReal only supports a top-level
  positive forall (exists-forall); …"*. The rejection is a fail-loud throw (an uncaught
  `runtime_error` aborts the process with the actionable message); dReal does not emit an
  SMT-LIB `(error …)` for it, by design. A future roadmap to actually *solve* the negated-forall
  fragment `∃p̄.(φ ∨ ¬∀x̄.d)` is specified in §8, with executable aspirational specs in
  `test/dreal/smt2/test/dreal_future.cc` (Group E, `NegatedNraForall`).

- **Forall-binder shadow rejection (QUIRK-001, ode_expressivity_energy).** A `forall`-bound
  variable whose name collides with a top-level declared (model) variable was silently
  mis-solved: the two same-named `Variable`s are distinct, the top-level one is left
  unconstrained (full real line) and driven to an absurd value, so a query whose intended reading
  is `unsat` returned a spurious `delta-sat` (a COMPLETENESS-direction hazard — a framework reads
  it as "no proof"). The SMT2 driver now tracks model-variable names (`DeclareVariable`) and the
  `forall` binder registers through `RegisterQuantifiedVariable`, which throws on such a collision
  (`smt2/driver.{h,cc}`, routed from the two `variable_sort` productions in `parser.yy`). Benign
  shadowing (no top-level variable of that name — e.g. `forall` over `forall`) is untouched. Net:
  `smt2/test/forall_binder_shadow_test.cc`.

- **Per-thread unification.** The five hand-rolled per-worker dispatchers (`ContractorForallMt`,
  `ContractorIbex{Fwdbwd,Polytope,Forall}Mt`, `ForallFormulaEvaluator`) — two with an off-by-one
  `<=` thread-id assert, three with no bounds check at all (a latent heap OOB if parallelism is
  ever nested) — were collapsed onto one `PerThread<T>` (`util/per_thread.h`) with a correct,
  always-on out-of-range **throw**.

Nets: `contractor_ibex_forall_adversarial_test.cc` (pre-pruner ⊆ + witness survival),
`forall_parallel_matrix_test.cc` (jobs × pre-prune verdict equality, incl. the CEGIS-only
parallel path), `forall_narrow_domain_test.cc`, `forall_unsupported_rejection_test.cc`,
`per_thread_test.cc`; plus the standing `benchmark/forall_differential.sh` (zero verdict flips
across jobs × pre-prune × polytope) and `tsan_gate.sh` (ThreadSanitizer over forall+parallel).

---

## 5. Formula Evaluator

`ForallFormulaEvaluator` (`forall_formula_evaluator.cc`) is the ICP loop's **δ-SAT
acceptance + branching** test — run on *every* box the loop pops, right after pruning
(`icp_parallel.cc` `Worker` → `EvaluateBox`, `icp.cc`). It runs the same
CE search as the contractor and returns a `FormulaEvaluationResult`
(`forall_formula_evaluator.cc:98–132`):

- **CE found** → `Type::UNKNOWN`, error interval `[0, maxᵢ|eᵢ(x, b)|]` (how badly the CE
  violates). `EvaluateBox` compares that diameter to `precision`: `> δ` ⇒ mark this
  formula's variables as branching candidates; `≤ δ` ⇒ the box is δ-close enough on this
  constraint, no candidate added.
- **No CE** → `Type::VALID`, `[0, 0]` (the whole box satisfies `∀y`).

It **never** returns `UNSAT` — a found CE means "not yet decided, measure/branch," not
"infeasible" (an `UNSAT` from an evaluator empties the box, `icp.cc:39–47`; only the
relational leaf evaluators do that). When every constraint's evaluator returns `VALID` or a
sub-`δ` `UNKNOWN` so that no branching candidate remains, the loop **accepts the current box
as the δ-SAT model** and returns (the `evaluation_result->none()` exit in
`icp_parallel.cc` `Worker`). That acceptance step is where the
evaluator's leniency (§4.1, `ε = 0.99δ`) matters: it declares `∀y. φ` satisfied on the box
unless a near-full-`δ` counterexample survives the strengthened search.

The evaluator holds one `Context` per worker thread via `PerThread<Context>` (built lazily on
first use; §4.8). Its CE search is *structurally* the same
as the contractor's — assert `domain ∧ DeltaStrengthen(¬body, epsilon)` (the shared
`StrengthenForallCounterexampleQuery`, §4.8), set the existential intervals,
`CheckSat` (`forall_formula_evaluator.cc:79–81, 101–104`) — but it runs at a **different
precision regime**: `epsilon = 0.99·delta`, `inner = 0.9801·delta`
(`theory_solver.cc:320–322`), the CAV 2018 experimental values (paper §5, p. 229), *not*
the contractor's `δ/2, δ/4` (§4.1). It is not "initialized identically" to the contractor —
only the CE query structure matches, not the precisions.

---

## 6. Limitations and Caveats

### 6.1 Nested `forall` crashes

If the body of a `forall` contains another `forall`, the solver throws at contractor
construction time. The root cause is `DeltaStrengthen` (`symbolic.cc:399–401`):

```cpp
Formula VisitForall(const Formula&, const double) const {
    throw DREAL_RUNTIME_ERROR(
        "DeltaStrengthenVisitor: forall formula is not supported.");
}
```

`DeltaStrengthen(!body, epsilon)` is called on the negated body during `ContractorForall`
construction. If the body contains a `forall` node, this would throw deep in construction.
**As of the 2026-06 hardening (§4.8) this is caught earlier and reported clearly** at
`Context::Assert` (`RejectUnsupportedForall`) — a nested, negated, or sign-flipped `forall`
is rejected with an actionable message instead of an opaque deep crash. The fragment is still
unsupported; only the failure mode improved.

**Implication for nested-quantifier projects**: any ∃∀∃ or ∀∃∀ formula requires
transformation before it can be fed to dReal (see §8).

### 6.2 No quantifier elimination

dReal does not implement quantifier elimination (QE). There is no Cylindrical Algebraic
Decomposition (CAD) or virtual term substitution path. The CE-guided loop is a complete
procedure for the ∃∀ fragment over bounded domains (in the delta-completeness sense), but it
produces a delta-SAT witness for `x`, not a QE certificate. Returning an explicit description
of the set of `x` satisfying `∀y.φ(x,y)` is not supported. (One narrow, mechanical exception:
a universal variable pinned to a single point by its binder is eliminated by substitution —
point-quantifier elimination, §4.8 — but this is not general QE.)

### 6.3 Single quantifier alternation

Only `∃∀` is supported — one block of existentials, one block of universals. The standard
SMT2 `∀x.∃y.φ` direction (validity checking) is not handled; feeding it to dReal will either
produce incorrect results or crash. Multiple alternation levels (`∃∀∃`, `∀∃∀`) are not
supported.

### 6.4 Bounded forall domain required in practice

Although the grammar allows unbounded quantified variables (`(y Real)` without `[lb, ub]`),
the CE search will not terminate over an unbounded domain for a nonlinear body. Always bound
forall variables.

### 6.5 Body structure after CNF distribution

The body passed to each `ContractorForall` is a single clause (from Tseitin distribution). A
body that is a conjunction of atoms produces multiple `ContractorForall` instances (one per
conjunct after distribution). The body passed to the CE solver is the strengthened negation
of that single clause, i.e., a conjunction of strengthened literals. If the original body was
already a single clause (disjunction), the CE search looks for a point violating all disjuncts
simultaneously.

### 6.6 `DeltaStrengthen` on strict vs. non-strict inequalities

`DeltaStrengthen(f, ε)` tightens `e ≥ 0` to `e ≥ ε` and `e > 0` to `e > ε`. After negation,
`¬(e ≥ 0)` → `e < 0` → strengthened to `e < -ε` → `e ≤ -ε - ε'`. This ensures the CE is a
genuine δ-violating point and not a boundary artifact. The strengthening amount is `ε = δ/2`
in the contractor and `ε = 0.99δ` in the evaluator (§4.1). **Note (§4.8):** the matrix *body*
is always strengthened; the universal-*domain* bound atoms are strengthened (ε-shrunk) only
for **wide** universal variables (binder width `≥ 3ε`) and kept **exact** for **narrow** ones —
shrinking a narrow domain emptied the searched region and missed counterexamples. So the
strengthening applies to all body literals but only to wide-variable binder bounds.

### 6.7 Performance

Each `ContractorForall::Prune` call runs a **full nested dReal solve**. For nonlinear bodies
over multi-variable forall domains, the inner solve is itself an ICP loop with contractors,
branching, and SAT calls. The outer ICP's learned facts about the existential box are not
available to the inner solve (each `Prune` only sets the existential intervals, not any
derived constraints). Expect roughly 10–100× overhead per forall constraint compared to a
purely existential formula of the same complexity.

**`--local-optimization`** (`dreal_main.cc:515–519`): enables `CounterexampleRefiner`
(`counterexample_refiner.{h,cc}`), which sharpens each CE before pruning by running a
**local optimization** that pushes the CE to *further* violate the clause — it reframes the
violated atom `e₁ ◦ e₂` as an objective (`minimize e₁ − e₂`) plus the clause atoms as
constraints (`counterexample_refiner.cc:77–104`). A sharper CE prunes more per iteration,
cutting the number of fixpoint iterations (CAV 2018 §3.3, Fig. 1). The optimizer is
**NLopt**: `LD_SLSQP` (Sequential Least-Squares QP) when the query is differentiable, else
`LN_COBYLA` (Constrained Optimization BY Linear Approximations)
(`counterexample_refiner.cc:68–75`) — the exact pair the CAV 2018 implementation used (§5;
its experiments set `1e-6` tolerances, a `1e-3 s` timeout, and 100 max evaluations, and
report speed-ups on 20 of 23 global-optimization instances, up to ~250×). The refiner is a
no-op — returns the raw CE, `opt_` stays null — only when the strengthened query reduces
*entirely* to binder-bound atoms that `FilterAssertion` absorbs (`formulas.empty()`,
`counterexample_refiner.cc:59–63`). With constraints present but no exist∧forall coupling it
still runs the optimizer over the constraints, just without an objective
(`counterexample_refiner.cc:79, 88–90`).

**`--polytope`** / `(set-option :polytope true)`: routes the **contractor's** CE-search inner
context through IBEX polytope (LP-based) contractors, which can prune the forall domain more
aggressively. Controlled by `use_polytope_in_forall` (`contractor_forall.h:103–104`). Note the
asymmetry: only the `ContractorForall` inner context reads this flag; the
`ForallFormulaEvaluator`'s CE context is built from a fresh default `Config` (only `precision`
overridden, `forall_formula_evaluator.cc:70–72`), so `--polytope` never reaches the evaluator's
CE search. This is the linear-programming pruning the CAV 2018 implementation ran on **CLP** (§5). **Caveat**:
it needs an LP solver linked into the build, and `cmake-build-release` (this repo) does not
link one — `--polytope` on problems whose linear assertions trigger the LP path crashes with
`LPSolver method called but no LPSolver has been configured`. Simple forall problems with no
top-level linear assertions may work; the Ackley-family tests fail with it. (The current
source build links **SoPlex** — `CMakeLists.txt` `-DLP_LIB=soplex` — so the polytope path is
live; this caveat is the historical `LP_LIB=none` state.)

**`--forall-pre-prune`** / `(set-option :forall-pre-prune true)` (default off): runs a sound
`ibex::CtcForAll` **proj-intersection pre-pruner** (`ContractorIbexForall`) *beside* the CEGIS
decider in the forall fixpoint — pure interval contraction on the existential box, no nested
δ-solve. It augments, never replaces, the δ-complete `ContractorForall`. `--forall-pre-prune-prec`
(default 0.5) sets the universal-box bisection precision. COMPLETENESS-only (cannot move a
verdict). NRA ∃∀ only; runs under `--jobs>1` via a per-worker `ContractorIbexForallMt` cell.
The mechanism and its IBEX-lever rationale are in
`ibex_docs/AUDIT-QUANTIFIERS.md` Q1; the measured tractability outcome on odeexpr_v2 (sound,
but speedup is encoding-fragile and never cracks the pinned δ) is in
`exists_forall_perf.md`.

> **`--forall-pre-prune-prec` is NOT a fourth δ — false friends.** It is easy to read
> "precision" and file it alongside the `δ' < ε < δ` chain of §4.1. It is a completely
> unrelated quantity, on a different component:
>
> | | `δ' < ε < δ` (§4.1) | `--forall-pre-prune-prec` |
> |---|---|---|
> | **Component** | the always-on δ-complete CEGIS decider (`ContractorForall` / `ForallFormulaEvaluator`) | the opt-in `ibex::CtcForAll` pre-pruner (`ContractorIbexForall`), only with `--forall-pre-prune` |
> | **What it is** | slacks on constraint *values* `fᵢ` (how much to strengthen `¬body` / weaken the nested solve) | a bisection *width threshold* on the universal-domain box `y` (`ibex_CtcForAll.cpp:46`: bisect while `y.max_diam() > prec`) |
> | **Units** | a slack on function values `fᵢ` | a width on `y`-intervals (same units as the ∀-variable) |
> | **Mechanism** | nested δ-SAT counterexample search with ε-strengthening; prunes via *one searched* CE pinned to a real `y`-midpoint (§4.3) | bisect `y` into slices, contract `x` (forward-backward) against the constraint with `y` pinned to *each slice's* midpoint, intersect — a systematic *grid* of `y`-midpoints (`ibex_CtcForAll.cpp:39`); no search, no nested solve, no strengthening |
> | **Refutes by asking** | "does a robust *violating point* exist in `y`?" | "over each *slice* of `y`, what `x` can I interval-eliminate?" |
> | **Effect of "finer"** | smaller `δ` ⇒ more-complete, slower *decision* | smaller `prec` ⇒ more/narrower `y`-slices ⇒ stronger contraction, slower; once `prec ≥ y.max_diam()` bisection never fires and `CtcForAll` degenerates to one whole-box check |
> | **Class** | correctness + completeness of the decision procedure | COMPLETENESS-only speed heuristic; sound either way (never a false `unsat`) |
>
> **What a value like `0.5` means concretely.** `prec` is absolute, in the ∀-variable's own
> units, so "coarse" vs "fine" is *relative to the domain width* `W` — `0.5` is not intrinsically
> either. Bisection halts once every universal dimension is `≤ prec`, so a single ∀-variable of
> width `W` is cut into `≈ W/prec` slices (each between `prec/2` and `prec` wide), and across `m`
> universal variables the slice count is the **product** `≈ ∏ⱼ Wⱼ/prec` — exponential in `m`.
> Thus `0.5` is ~2 slices on a width-1 domain (near-degenerate — why the flag is near-irrelevant
> on odeexpr_v2's narrow ∀-domains), ~128 on `[−32, 32]`, and ~128×128 for two such variables.
> Once `prec ≥ W` the `max_diam > prec` guard never fires: no bisection, and `x` is contracted
> against the single `mid(y_init)` (the degenerate whole-box check).
>
> The two run side-by-side in the same forall fixpoint (`theory_solver.cc:196–205`); `prec`
> tunes only the pre-pruner and never touches the `δ' < ε < δ` decider.

---

## 7. Relationship to `forall_t` — the canonical disambiguation (`forall-vs-forall_t`)

> This table is the **canonical** `forall` vs `forall_t` reference; the other docs and code
> anchors (grep `forall-vs-forall_t`) point here. They are entirely separate code paths — a
> confusion that has bitten this repo before (a contractor was mislabeled as the other). When
> in doubt, check the AST node and the contractor: `Formula::Forall` / `ContractorForall`
> (`Kind::FORALL`) is the NRA quantifier; `FormulaKind::ForallT` / `contractor_ode_lohner`
> (`Kind::ODE_LOHNER`) is the ODE invariant.

| Aspect | `forall` | `forall_t` |
|--------|----------|------------|
| Purpose | ∃∀ quantified NRA | ODE trajectory invariant |
| Token | `TK_FORALL` | `TK_FORALLT` |
| AST node | `Formula::Forall` | `FormulaKind::ForallT` |
| Quantified over | real-valued SMT variables | ODE time parameter |
| Bounds notation | `(y Real [lb, ub])` in variable list | `[lb ub]` as a time interval |
| Contractor | `ContractorForall` | `contractor_ode_lohner` invariant check |
| Requires linking | no | yes — must match an `integral` formula (an unlinkable one throws at check-sat: `RejectUnlinkedForallT`, `context_impl.cc`) |
| `NaiveCnfizer` | recursively CNFizes body | passes through unchanged |
| `DeltaStrengthen` | throws if nested inside another body | passes through unchanged |
| Logic tag | `QF_NRA` | `QF_NRA_ODE` |

---

## 8. Nested-Quantifier Projects: What Is and Is Not Possible

For a planned project making heavy use of nested quantifiers, the relevant capabilities and
hard limits are:

**Supported (∃∀)**:
- Finding a parameter `x` such that a constraint `φ(x, y)` holds for all `y` in a bounded domain.
- Lyapunov certificate synthesis: `∃V. ∀x∈D. (V(x) ≥ 0) ∧ (∇V·f(x) ≤ 0)`.
- Minimax / robust optimization: `∃x. ∀y∈D. f(x) ≤ g(y)` (combined with a point-value assertion on `x`).

**Not supported without transformation**:
- `∀x.∃y.φ(x,y)` — validity checking direction. Would require negation + ∃∀ re-encoding.
- `∃x.∀y.∃z.φ(x,y,z)` — the innermost ∃ over `z` cannot be expressed.
- Quantifier elimination results (explicit semi-algebraic description of the satisfying set).
- Counting or optimization over the full satisfying set.

**Workaround for deeper alternation**: quantifier alternations beyond ∃∀ must be handled by
external preprocessing — e.g., skolemization to reduce `∃∀∃` to `∃∀` at the cost of
introducing Skolem function terms, or iterative CEGIS loops that call dReal as a subroutine
for the inner ∃∀ check.

**Roadmap — negated forall `¬∀ = ∃` (FEAT-001, ode_expressivity_energy).** One near-term
extension is tractable and distinct from the deeper alternations above. A *negated* universal
`¬∀x∈[lb,ub].φ` is logically `∃x∈[lb,ub].¬φ`, and since a top-level free variable already *is* an
existential in dReal, the fragment `∃p̄.(φ(p̄) ∨ ¬∀x̄.d)` — exactly what `verification.prove`
emits when refuting a containment antecedent — can be discharged by promoting each negated
forall's bound vars to fresh free box vars over `[lb,ub]` and asserting `¬φ` (the binder domain
becomes a conjunct: `¬(domain→matrix) = domain ∧ ¬matrix`). It then reduces to a native NRA SAT
query. The implementation would **invert** the CE-search the CEGIS `ContractorForall` already
runs (`context_for_counterexample_`): a counterexample found ⇒ the `¬∀` literal is δ-sat; none
found ⇒ prune the box to empty — reusing that machinery rather than adding a parallel copy.
Sound: dReal `unsat` on the negation ⟺ the universal holds (no false `unsat`; only COMPLETENESS
is affected). The design challenge is **polarity** — predicate abstraction lets the SAT layer
query the *same* forall atom both positively (CEGIS-universal `x̄`) and negatively (existential
`x̄`), so the bound var cannot be a fixed box dimension; the clean realization is polarity-keyed
theory-solver handling. **Boundary:** a *positive* forall whose body contains a forall stays
rejected — genuine `∀∃` alternation, where `∃` cannot be hoisted past the outer `∀`. Today the
whole fragment is hard-rejected (§4.8); the desired verdicts are pinned as aspirational
`GTEST_SKIP` specs in `test/dreal/smt2/test/dreal_future.cc` (Group E, `NegatedNraForall`).

### 8.1 Lyapunov synthesis — the canonical ∃∀ application

The headline application in CAV 2018 (§5.2) is synthesizing a Lyapunov function for a
dynamical system `ẋ = f(x)`, `x ∈ X`, in a **single ∃∀ solve** — *searching for* the
certificate and *verifying* it at once, unlike verify-only pipelines (e.g. Kapinski et al.,
HSCC 2014, where dReal only checked a simulation-guided candidate). Fix a polynomial
template `v(x) = zᵀ P z` (`z` a monomial vector over `x`, `P` a symmetric coefficient
matrix); the unknowns are the entries of `P`. The synthesis query asserts the standard
Lyapunov conditions — positive definite and non-increasing along trajectories:

```
∃P. [ v(x) = zᵀ P z
      ∧ ∀x ∈ X∖{0}. v(x) > 0
      ∧ ∀x ∈ X.      ∇v(x)ᵀ · f(x) ≤ 0 ]
```

A `delta-sat` witness is a δ-valid Lyapunov function. Worked result (CAV 2018, normalized
pendulum `ẋ₁ = x₂`, `ẋ₂ = −sin(x₁) − x₂`, quadratic template, `‖x‖ ∈ [0.1, 1.0]`,
`δ = 0.05`): the solver returns `v = 40.6843·x₁x₂ + 35.6870·x₁² + 84.3906·x₂²` in ~44 s.
A time-varying system (the damped **Mathieu** equation) is handled identically by adding
the time `t` as an extra forall variable.

This is the ∃∀ shape a control-synthesis project would build on. Note the two universal
clauses become **two separate `ContractorForall` instances** after CNF distribution (§3),
each running its own CE loop; and the `∀x ∈ X∖{0}` domain (a punctured set) must be encoded
explicitly (e.g. `0.01 ≤ ‖x‖²`), since the contractor only takes box `[lb, ub]` domains
(§2.2).

---

## 9. Example: Global Minimum via ∃∀

The pattern: assert that `z` is a lower bound for `f` over the forall domain AND that `z`
equals `f` at a specific point. `delta-sat` witness is a δ-global minimizer.

```smt2
; find x ∈ [-2,2] minimizing x² (trivial: min = 0 at x = 0)
(set-logic QF_NRA)
(declare-fun x () Real [-2, 2])
(declare-fun z () Real)
(assert (forall ((y Real [-2, 2])) (<= z (* y y))))  ; z ≤ y² for all y in [-2,2]
(assert (= z (* x x)))                               ; z = x²
(check-sat)
; → delta-sat  x ≈ 0, z ≈ 0
```

For more complex objectives (transcendental functions, multiple variables), the same
two-assertion pattern applies. See `test/dreal/test/smt2/exist_forall_06.smt2` (Ackley3)
and `exist_forall_07.smt2` (Ackley4) for multi-variable examples — both return `delta-sat`
without `--polytope`. Do not add `--polytope` to transcendental benchmarks unless LP is
compiled in (see §6.7).

The general pattern (CAV 2018 §5.1): a constrained global minimization `min f(x) s.t. φ(x)`
over `x ∈ ℝⁿ` encodes as the ∃∀ formula

```
φ(x) ∧ ∀y. (φ(y) → f(x) ≤ f(y))
```

— "x is feasible, and no feasible y beats it." Because this is a *guaranteed* global search
(not local descent), it solves highly non-convex objectives that defeat gradient methods:
CAV 2018 Table 1 reports 23 standard benchmarks at `δ = 1e-4`, including Ripple1 (252 004
local minima) driven to its global optimum. `--local-optimization` (§6.7) only sharpens the
*inner* CE search; the outer global guarantee is unaffected.

---

## 10. Grammar Summary

```
forall-term := '(' 'forall' '(' var-sort-list ')' body ')'
var-sort-list := (var-sort)*
var-sort      := '(' SYMBOL 'Real' ')'              ; unbounded
             |  '(' SYMBOL 'Real' '[' lb ',' ub ']' ')'  ; bounded

body := any SMT2 term that evaluates to a formula
        (including disjunctions, conjunctions, implications)
        WARNING: must not contain a nested 'forall'

Result AST: forall(qvars, imply(domain, body))
  where domain = ⋀ᵢ (lbᵢ ≤ yᵢ) ∧ (yᵢ ≤ ubᵢ)
```
