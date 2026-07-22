# ARCHITECTURE-COMPARISON.md — IbexSolve vs dReal's CheckSat/ICP

**The question.** IBEX and dReal both reimplement box bisectors, branch-and-prune
search, contractor composition, and a ∃∀ operator. `IbexSolve`'s main loop
(`ibex::Solver`) looks structurally identical to dReal's `TheorySolver::CheckSat` →
`IcpSeq::CheckSat`. **Is dReal leaving performance on the table by hand-rolling its own
search/solver layer instead of binding IBEX's `Solver`/`Bsc`/`CellBuffer` machinery?**

**Headline verdict (source-grounded).** **No — not at the search-loop level.** Every
reimplemented piece of the *solve loop* is justified by a DPLL(T) integration constraint
that `ibex::Solver` structurally cannot satisfy (conflict explanations as theory lemmas,
incremental literal-driven re-entry, first-model-vs-covering semantics, recursive δ-CEGIS
∀). The genuine leftover opportunity is **not** in the loop dReal reimplements but in the
**atomic contractors it composes inside that loop** (ACID/3BCID/Newton) — exactly the lever
[`AUDIT.md`](AUDIT.md) identifies. This file pressure-tests the "Solver correctly ignored"
claim (AUDIT tier E) against source and confirms it, with one refinement: dReal has *already*
reimplemented even IBEX's impact-set incrementality (the part one might think was left on the
table), so the Solver is not a missed optimization but a different-shape component.

> Soundness framing (project `CLAUDE.md`): nothing below is a soundness lever. A
> search/composition difference can only change *which* δ-boxes are explored or *how fast* —
> i.e. **COMPLETENESS / performance** (`asserts φ^δ T-satisfiable on a T-unsatisfiable φ` =
> missed refutation is the only risk of a weaker loop), never a false `unsat`. Soundness lives
> in the interval ops below the loop (gaol rounding, the fork patches), not here.

> **Source-fidelity.** Every architectural claim cites `file:line` on both sides, read directly.
> IBEX paths point at the `.cpp`/`.h` where the logic lives. Interpretation (my reading of *why*)
> is separated from VERIFIED facts (what the code does). Two items I could not fully trace are
> marked "unverified — confirm in <file>".

---

## 1. Side-by-side: the two branch-and-prune loops

Both are a DFS over a stack/buffer of boxes: pop → contract/prune → (empty? discard) →
(small enough? accept) → bisect → push two children. The skeletons line up almost statement
for statement.

| Step | `ibex::Solver::next()` — `../../ibex-fork/src/solver/ibex_Solver.cpp` | `IcpSeq::CheckSat()` — `../src/dreal/solver/icp_seq.cc` |
|---|---|---|
| Cell buffer | `CellBuffer& buffer` (DFS = `CellStack`, `ibex_Solver.h:237`; `ibex_DefaultSolver.cpp:110`) | `vector<pair<Box,int>> stack` — DFS vector, `icp_seq.cc:60` |
| Pop current | `Cell* c = buffer.top()` `:184` | `tie(current_box, current_branching_point) = stack.back(); stack.pop_back()` `:125-126` |
| Incremental impact | `context.impact = BitSet::singleton(n, c->bisected_var)` `:188-192` | `int` branching dim carried in the stack pair `:60`, consumed by worklist (§2) |
| Contract / prune | `ctc.contract(c->box, context)` `:195` | `contractor.Prune(cs, ur)` `:131` |
| Empty → discard | `if (c->box.is_empty()) throw EmptyBoxException` → pop `:197,242-248` | `if (current_box.empty()) continue` `:137-140` |
| Accept criterion | `check_sol(box)` — Newton certification, adds to Cov `:201-209,344` | `EvaluateBox(...)` → `evaluation_result->none()` → `return true` `:146-160` |
| Stop bisection | `is_too_small`: `diam ≤ eps_x_min` `:212,474-478` | box not bisectable → `return true` `:184-195` (no model, but δ-leaf) |
| Bisect | `bsc.bisect(*c)` → 2 cells `:216` | `config().brancher()(...)` → `box_left/box_right` `:166-173` |
| Push children | `buffer.push(second); buffer.push(first)` `:221-222` | `stack.emplace_back(...left/right...)` `:176-182` |
| Outer driver | `solve(): while(next(status))` collect ALL boxes `:271-313` | `while(!stack.empty())` return at FIRST δ-box `:112,160` |

**The one structural divergence that drives everything else** (VERIFIED): IBEX's loop is a
*covering* algorithm. `solve()` keeps calling `next()` until the buffer drains, accumulating
every solution/boundary/unknown box into a `CovSolverData` manifold
(`ibex_Solver.cpp:271-313`, `check_sol` at `:344-427` runs inflating-Newton existence proofs
and appends to `manif`). dReal's loop is a *satisfiability* decision: the instant
`EvaluateBox` reports the box is within δ on every constraint, it `return true`s with that one
box as the model (`icp_seq.cc:157-160`) — it never enumerates a covering, never runs Newton
certification, never builds a Cov. That is the SMT-vs-CSP problem-shape gap AUDIT tier E
names, now confirmed in source.

---

## 2. Feature-by-feature reimplementation table

| Feature | IBEX class (`../../ibex-fork/src/…`) | dReal equivalent (`../src/dreal/…`) | Why dReal reimplements | Leftover opportunity? |
|---|---|---|---|---|
| **Solve loop** | `Solver::next/solve` (`solver/ibex_Solver.cpp:167-313`) | `IcpSeq::CheckSat` (`solver/icp_seq.cc:37-201`); driver `TheorySolver::CheckSat` (`solver/theory_solver.cc:345-383`) | First-model decision, not covering; must return a **conflict explanation** on UNSAT for the SAT layer (§3) — `Solver` only emits a Cov | **No** (a) — different output contract |
| **Bisector / var-select** | `Bsc`: `RoundRobin` / `SmearSumRelative` (`solver/ibex_DefaultSolver.cpp:107-109`) | `BranchLargestFirst` / `FindMaxDiam` (`solver/brancher.cc:29-69`), pluggable `config().brancher()` | Branch only over the **δ-active dimensions** `EvaluateBox` flags (`icp_seq.cc:166-173`), not all vars; rounding-token-aware (`safe_diam`, `brancher.cc:37`) | **Minor (b?)** — smear/grad-based selection unused; gradient is cold (fork #1), so low-value. See §3 |
| **Propagation / fixpoint** | `CtcPropag` (agenda), `CtcFixPoint` (ratio) | `ContractorWorklistFixpoint` (`contractor/contractor_worklist_fixpoint.cc:90-156`); `ContractorFixpoint` (`contractor_fixpoint.cc:48-68`) | Worklist keyed on `branching_point` + `input_to_contractors_` (`:94-130`) = **impact-set incrementality**, hand-rolled; carries dReal's `output()` bitset | **No** (a) — already reimplements IBEX's impact optimization |
| **Compose (∘)** | `CtcCompo` (`contractor/ibex_CtcCompo`) | `ContractorSeq` (`contractor/contractor_seq.cc:43-50`) | Threads dReal's `ContractorStatus` (box + output bitset + used-constraints) through each sub-contractor | **No** (a) — trivial, must carry SMT status |
| **Union (∪)** | `CtcUnion` (`contractor/ibex_CtcUnion`) | `ContractorJoin` (`contractor/contractor_join.cc:43-51`, `InplaceJoin`) | Same — joins `ContractorStatus`es, not bare boxes | **No** (a) |
| **Cell buffer / search stack** | `CellBuffer` → `CellStack` (DFS) / `CellHeap` (best-first) (`cell/ibex_CellStack.h:25-49`); rich `Cell` w/ `BoxProperties` (`cell/ibex_Cell.h:77-95`) | `vector<pair<Box,int>>` (`solver/icp_seq.cc:60`); alternating push order (`:176-183`) | No per-cell property map needed; box+branch-dim suffices; **DPLL(T) owns the real backtrack stack** (§3) | **No** (a) — but no best-first option exists (see §3 note) |
| **∃∀ contractor** | `CtcForAll` / `CtcQuantif` (`contractor/ibex_CtcForAll.cpp:37-102`): bisect parameter box `y`, contract `x` at `mid(y)` — pure interval proj-intersection (∀ ⇒ intersect the per-`y` projections, `../../ibex-fork/doc/contractor.rst:664-667`) | `ContractorForall` (`contractor/contractor_forall.h:73-265`) + `CounterexampleRefiner` (`contractor/counterexample_refiner.cc`) | **Recursive δ-CEGIS**: runs a *nested* `Context::CheckSat()` to find a CE (`contractor_forall.h:220`), prunes against it, nlopt-refines it (`:228`); double-sided `inner_δ<ε<δ` error (Kong/Solar-Lezama/Gao CAV18, `:195-197`) | **No** (a) — a genuinely different, stronger algorithm tied to dReal's own solver |
| **Newton certification** | `inflating_newton` in `check_sol` (`solver/ibex_Solver.cpp:344-427`) | none in the loop | dReal decides δ-sat by `EvaluateBox` width test, not existence proof; equalities are δ-relaxed | **No** (a) for sat; possible **(b)** as a *contractor* (AUDIT D1, `CtcNewton` late) |

---

## 3. The verdict — justified (a) vs missed opportunity (b), per feature

For each reimplemented piece: is it (a) **forced by DPLL(T) integration** `ibex::Solver`
cannot provide, or (b) a **real missed lever** where IBEX's tuned code could replace dReal's?

**(a) The whole solve loop — JUSTIFIED.** Three integration facts make `ibex::Solver`
structurally unusable as dReal's `CheckSat`, each VERIFIED:
1. **Explanation as theory lemma.** On UNSAT, dReal must hand the SAT layer a *conflict set*
   of literals, not a paving. `TheorySolver::CheckSat` reads
   `contractor_status.Explanation()` (`theory_solver.cc:370,379`), built from
   `used_constraints_`/`unsat_witness_` accumulated *during* pruning
   (`contractor_status.h:56-95`; e.g. `ContractorForall::Prune` calls `AddUsedConstraint`,
   `contractor_forall.h:241`). `context_impl.cc:459-523` then feeds that explanation to
   `sat_solver->AddLearnedClauseDirect/Pattern` to drive CDCL backjumping. `ibex::Solver`
   emits a `CovSolverData`, which has no conflict-clause notion — adopting it would mean
   discarding the lemma channel that the entire DPLL(T)/CAV26 pattern-matching layer consumes.
2. **First-model, not covering.** `IcpSeq` returns at the first δ-box (`icp_seq.cc:160`);
   `ibex::Solver::solve` is defined to drain the buffer and validate *all* solution boxes
   (`ibex_Solver.cpp:271-313`). Using it would do strictly more work for a decision query.
3. **Incremental literal-driven re-entry.** The real branch-and-backtrack tree lives in the
   SAT solver (`context_impl.cc:430-525` SAT⇄theory loop); the theory solver is invoked per
   assignment with a fresh `ContractorStatus(box)` (`theory_solver.cc:361`). IBEX's `Solver`
   owns its own persistent cell tree and Cov — two stateful search drivers would fight.

**(a) Compose / Union / Fixpoint / Worklist — JUSTIFIED, and notably thorough.** These are
near-line-for-line reimplementations of `CtcCompo`/`CtcUnion`/`CtcFixPoint`/`CtcPropag`, but
each threads dReal's `ContractorStatus` (box **+** `output()` change-set **+** used-constraints)
instead of a bare `IntervalVector` (`contractor_seq.cc:43`, `contractor_join.cc:43-51`,
`contractor_worklist_fixpoint.cc:90-156`). The strongest evidence that dReal isn't leaving the
*search* lever on the table: the worklist keys re-propagation on `branching_point` +
`input_to_contractors_` (`contractor_worklist_fixpoint.cc:94-130`) — this **is** IBEX's
impact-set incrementality (`ibex_Solver.cpp:188-192`, `context.impact`), independently
reimplemented. The thing one might assume was the missed optimization is already present.

**(a) ∃∀ — JUSTIFIED, and dReal's is the stronger algorithm.** This is the clearest case
*against* "borrow IBEX." `CtcForAll` (`ibex_CtcForAll.cpp:37-102`) is a pure interval
projection-intersection (∀ ⇒ intersect the per-`y` projections,
`../../ibex-fork/doc/contractor.rst:664-667`): bisect the parameter box `y` down to `prec`,
contract `x` against `mid(y)` at each leaf. dReal's `ContractorForall` instead solves a
*nested SMT problem* for a
counterexample (`contractor_forall.h:220`, `context_for_counterexample_.CheckSat()`),
δ-strengthens `¬φ` (`:85-87`), refines the CE with nlopt (`:228`,
`counterexample_refiner.cc`), and controls two-sided δ-error via `inner_δ<ε<δ`
(`:91-94`, the constructor's `DREAL_ASSERT`s enforcing `inner_delta < epsilon < precision`). Swapping in `CtcForAll` would *lose* capability (the δ-decision guarantees and
the local-opt refinement), not gain speed. Confirms AUDIT's "different problem shape."

**(b) The honest, narrow opportunities — and they are NOT the search loop:**
- **Atomic contractors inside the loop (the real lever, per AUDIT tier A).** dReal's *default*
  run composes *only* HC4 fwd-bwd per atom (`theory_solver.cc:207`) + integer + optional
  polytope. IBEX's `DefaultSolver` composes HC4 **then `CtcAcid(HC4)`** by default
  (`ibex_DefaultSolver.cpp:84-85`), plus Newton on square systems (`:87-90`). ACID/3BCID are
  now **shipped** as opt-in contractors (`--acid`/`--3bcid` → `make_contractor_ibex_acid` over
  the assertion system, `theory_solver.cc:240-244`; `contractor_ibex_acid.{cc,h}`) — appended
  after the per-formula contractors, **default OFF**, and parallel-safe under `--jobs>1` via a
  per-worker `ContractorIbexAcidMt` cell (`contractor.cc:215-217`, mirrors polytope's Mt variant).
  So the leftover lever is no longer a missing capability but a **default-tuning** question (measure
  ACID-on per family); `CtcNewton` remains genuinely absent (AUDIT D1). It lives in the
  *contractor* slot dReal already exposes, requiring **no** change to the search loop.
  Consistent with [`AUDIT.md`](AUDIT.md) A1/A2 and [`KNOBS.md`](KNOBS.md).
- **Variable-selection heuristic (minor, low-confidence).** dReal branches on max-diameter
  over active dims (`brancher.cc:48-69`); IBEX's default for square systems is
  `SmearSumRelative` (gradient-weighted, `ibex_DefaultSolver.cpp:108`). A smear-style brancher
  *could* slot into `config().brancher()` with no loop change. Low value: it needs the
  Jacobian that fork patch #1 made cold, and most dReal targets (ODE/`forall_t`) aren't square.
  Unverified whether smear helps dReal's instances — would need `/benchmark`, not asserted here.
- **Best-first search (capability, not perf).** IBEX offers `CellHeap`/`CellList` for
  best-first/BFS (`ibex_DefaultSolver.cpp:110`); dReal's stack is DFS-only with an alternating
  push order (`icp_seq.cc:176-183`). Not a clear win for a decision procedure and orthogonal to
  the contractor lever; noted for completeness, not recommended.

**Bottom line.** The search/solver loop is a *faithful, leaner* reimplementation whose every
divergence from `ibex::Solver` is forced by DPLL(T) (explanations, incremental literal-driven
entry, first-model semantics) or is a strict capability upgrade (δ-CEGIS ∀). dReal is **not**
leaving performance on the table by skipping `IbexSolve`. Where performance *is* on the table
is one level down — the **atomic contractors** (ACID above all) that plug into dReal's existing
composition layer without touching the loop — exactly where [`AUDIT.md`](AUDIT.md) already
points the lever.

---

## Source-fidelity caveats

- **VERIFIED in source:** every `file:line` above was read directly. IBEX `Solver::next` loop
  structure, `check_sol` certification/Cov output, `DefaultSolver` contractor composition
  (HC4+ACID(+Newton)(+polytope)), `CellStack`=DFS, `CtcForAll` proj-intersection algorithm; dReal
  `IcpSeq::CheckSat`, `BranchLargestFirst`, `BuildContractor` composition, worklist
  impact-set incrementality, `ContractorForall` recursive-CEGIS, the `context_impl.cc`
  SAT⇄theory loop and explanation→learned-clause path.
- **INTERPRETATION (clearly mine, not source-asserted):** the "(a) justified / (b) opportunity"
  judgments, and the claim that swapping `ibex::Solver` in would be net-negative — these are my
  architectural reading of the verified facts, not statements the code makes.
- **Unverified — confirm if it matters:** (i) `CtcPropag`/`CtcFixPoint` internal ratio values
  were not opened here (cited from AUDIT B1 / KNOBS, not re-read) — confirm in
  `../../ibex-fork/src/contractor/ibex_CtcPropag.cpp` before any ratio A/B. (ii) Whether a
  smear brancher actually helps dReal's instances is **hypothesized, unmeasured** — confirm via
  `/benchmark`, do not report as fact.
