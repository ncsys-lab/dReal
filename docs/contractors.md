# Contractors

Contractors are the core computational primitive in the theory layer. A contractor `C` takes a box `B` (a set of variable domains) and returns a tighter box `C(B) ⊆ B` that is guaranteed to contain all solutions. If `C(B)` is empty, there are no solutions.

All contractors implement the same interface (`src/dreal/contractor/contractor.h`):

```cpp
void Contractor::Prune(ContractorStatus* cs, const UpwardRounding& ur) const;
```

`Prune` reads `cs->box()`, tightens it in-place, and updates the explanation set in `cs` to record which constraints were responsible for any pruning.

The `ur` parameter is a zero-size capability token (`src/dreal/util/rounding.h`). The gaol/IBEX interval backend is sound only with the FPU in `FE_UPWARD` rounding mode, so the whole ICP contraction phase runs under `FE_UPWARD`, established **once** per phase by an `UpwardRoundingScope` (in `IcpParallel::CheckSat`'s root prune and each worker) rather than per `Prune`. `UpwardRoundingScope` is the only minter of an `UpwardRounding`, and it is required to call `Prune` — so "the rounding mode is established" is a compile-time obligation a contractor cannot bypass. The pure-gaol leaves (`ContractorIbexFwdbwd`, `ContractorIbexPolytope`) no longer guard the mode themselves; they `DREAL_ASSERT_ROUNDING(FE_UPWARD)` to verify the inherited phase mode in Debug builds. See `docs/rounding.md` for the full design.

---

## Contractor Kinds

Defined in `Contractor::Kind`:

| Kind | File | Description |
|---|---|---|
| `ID` | `contractor_id.cc` | Identity — no pruning, pass-through |
| `INTEGER` | `contractor_integer.cc` | Round integer-typed variables to integer bounds |
| `SEQ` | `contractor_seq.cc` | Apply a list of contractors in sequence |
| `IBEX_FWDBWD` | `contractor_ibex_fwdbwd.cc` | HC4 forward-backward propagation (main workhorse) |
| `IBEX_POLYTOPE` | `contractor_ibex_polytope.cc` | Linear relaxation (polytope) contractor — `--polytope`, linearizer selectable via `--polytope-linearizer` |
| `IBEX_ACID` | `contractor_ibex_acid.cc` | ACID / 3BCID shaving over the HC4 path — `--acid` / `--3bcid` |
| `IBEX_NEWTON` | `contractor_ibex_newton.cc` | Interval-Newton (Hansen–Sengupta) on the square equality subsystem — `--newton` |
| `IBEX_OBBT` | `contractor_ibex_obbt.cc` | Optimization-based bound tightening: 2n certified LPs over the X-Taylor relaxation — `--obbt` |
| `IBEX_MOHC` | `contractor_ibex_mohc.cc` | Mohc monotonic-occurrence propagation (occurrence-grouping monotone revise + BoxNarrow) — `--mohc` |
| `IBEX_FORALL` | `contractor_ibex_forall.cc` | Sound `ibex::CtcForAll` pre-pruner beside the CEGIS `FORALL` — `--forall-pre-prune` (section below) |
| `FIXPOINT` | `contractor_fixpoint.cc` | Run a contractor to fixpoint |
| `WORKLIST_FIXPOINT` | `contractor_worklist_fixpoint.cc` | Fixpoint with dependency tracking |
| `FORALL` | `contractor_forall.h` | ∃∀ `QF_NRA`: CE-guided pruning for `forall` clauses (CAV 2018) — *not* the ODE-time `forall_t` |
| `JOIN` | `contractor_join.cc` | Disjunctive composition (convex hull of results) |
| `ODE_LOHNER` | `odes/contractor_odes.cc` | CAPD order-20 Taylor integration for ODEs (per-slice tube + filter) |

---

## IBEX Forward-Backward (HC4)

**File:** `src/dreal/contractor/contractor_ibex_fwdbwd.cc`

This is the primary contractor for algebraic constraints. Given a formula `f(x₁,...,xₙ) ≤ 0` (or `= 0`, `≥ 0`), it:

1. **Forward pass**: Evaluates `f` bottom-up using interval arithmetic, computing an interval enclosure for each subexpression.
2. **Backward pass**: Propagates tighter bounds top-down by inverting each operator.

Example for `x * y ≤ 1` with `x ∈ [0.5, 2]`, `y ∈ [0.5, 3]`:
- Forward: `x * y ∈ [0.25, 6]`
- Backward: knowing the product must be ≤ 1 and `x ≥ 0.5`, we get `y ≤ 1/0.5 = 2`, so `y ∈ [0.5, 2]`

IBEX implements this as `ibex::CtcFwdBwd`. The dReal wrapper converts `dreal::Formula` → `ibex::NumConstraint` via `IbexConverter` (`src/dreal/util/ibex_converter.h`).

**Important soundness note**: Strict inequalities (`<`, `>`) require careful handling of the bound. The `FilterAssertion` function in `contractor_ibex_fwdbwd.cc` uses `nextafter()` to convert strict bounds to IBEX's closed-interval representation. A past soundness bug existed here — see the git log for the fix.

---

## IBEX Polytope

**File:** `src/dreal/contractor/contractor_ibex_polytope.cc` — opt-in via `--polytope` (and `--forall-polytope` for the ∃∀ context).

Linearizes the constraint system and applies polytope (LP-based, `CtcPolytopeHull` over vendored SoPlex) contraction. More expensive than FWDBWD but can prune regions that interval arithmetic alone misses, especially for tightly coupled linear or near-linear constraints.

**Linearizer selector (`--polytope-linearizer xtaylor|affine|both`, default `xtaylor`):** `xtaylor` = the corner X-Taylor rows (`LinearizerXTaylor`); `affine` = affine-arithmetic rows from the fork-vendored `LinearizerAffine2` (fAF2 forms; rounding audit discharged as SOUND-WITH-SCOPE-WRAP, `ibex_docs/affine-rounding-audit.md`), which track first-order variable correlations and can prove a box infeasible outright; `both` = both row sets ANDed into one LP. Measured (2026-07 sweep): **prefer `affine`** — its solve set strictly contains xtaylor's on odeexpr (v1 PAR2 0.804×), `both` adds nothing, and **BUG-014** (`docs/dreal-bugs.md`) makes the xtaylor rows crash-exposed (OOB write in SoPlex 4.0.2 presolve — latent SOUNDNESS risk (a corrupted box narrowed past a true model would be a false unsat; none observed); affine rows through the same LP chain are clean).

---

## Opt-in IBEX system cells — Newton, OBBT, Mohc (2026-07)

**Files:** `src/dreal/contractor/contractor_ibex_{newton,obbt,mohc}.{h,cc}` (+ per-worker `*_mt` cells for `--jobs>1`). All default-off, soundness-neutral COMPLETENESS levers (weaker/absent contraction risks only a missed refutation — asserts φ^δ T-satisfiable on a T-unsatisfiable φ — never a false unsat).

- **`--newton` / `--newton-ceil` (default 0.01):** `ibex::CtcNewton` on the square equality subsystem of the assertions; the ceil gates it to boxes whose max diameter is small enough for Newton to bite.
- **`--obbt`:** 2n LPs (min/max each variable) over the X-Taylor relaxation, certified bounds only (`Mode::Certified`, `OptimalProved`-only). Rides the same LP path as `--polytope` — BUG-014-exposed.
- **`--mohc`:** the `CtcMohc` monotonicity contractor (fork port), optimal hull-consistency on monotone multi-occurrence constraints, no LP. Composable with `--acid`/`--3bcid`.

**ODE/forall filtering (`FilterIbexConvertible`, `contractor_ibex_polytope.cc`):** every system-wide cell (polytope, ACID, OBBT, Mohc — and Newton's equality filter) converts only atoms that are neither `forall` nor ODE-carrying (`include_ode()`), since `IbexConverter` throws on `integral`/`forall_t`. Dropping formulas from a pure contraction cell is COMPLETENESS-only. Before this filter the cells aborted the process on ODE-family inputs (pre-existing converter-crash class, fixed `f6d735254`).

**Measured verdicts (2026-07 sweep — family-conditional, no default changes):** `--mohc` is the only base-beater on the odeexpr families (115/151, PAR2 0.968×); `--polytope --polytope-linearizer affine` wins odeexpr_v1 (0.804×); both are **net-negative on the ODE families** (mohc 69/108 vs base 106/108) — keep base flags there. `--newton` and `--obbt` measured dead corpus-wide (kept for record). Full record: `OPTIMIZATION_LOG.md` §"RELATED-WORK candidate campaign"; `ibex_docs/RELATED-WORK.md` ranked-table stamps.

---

## Sequential Composition

**File:** `src/dreal/contractor/contractor_seq.cc`

`ContractorSeq` applies a list of contractors in order:

```
C_seq([C₁, C₂, ..., Cₙ]).Prune(B) = Cₙ(...C₂(C₁(B))...)
```

The result is tighter (or equal) to any individual contractor. Order matters because each contractor may prune domains that help subsequent contractors prune further.

---

## Fixpoint

**File:** `src/dreal/contractor/contractor_fixpoint.cc`

Runs a contractor repeatedly until the box stops shrinking:

```
C_fix(C, termination).Prune(B):
  loop:
    B' ← C.Prune(B)
    if termination(B, B'): break
    B ← B'
  return B'
```

The termination condition is a function `(old_box, new_box) → bool`. Typically it checks whether the relative improvement in box volume is below a threshold.

`ContractorWorklistFixpoint` is a smarter version that tracks which constraints depend on which variables, so it only re-runs a contractor when one of its input variables was tightened by a previous contraction.

---

## Join (Disjunctive Composition)

**File:** `src/dreal/contractor/contractor_join.cc`

For disjunctive formulas `φ₁ ∨ φ₂`, neither `C_{φ₁}` nor `C_{φ₂}` alone is sound (we can't prune based on one branch if the other might still be satisfiable). The join contractor:

```
C_join([C₁, C₂]).Prune(B) = hull(C₁(B), C₂(B))
```

returns the interval hull (smallest enclosing box) of both results. This is sound because any solution must be in at least one branch, hence in the hull.

---

## Forall Contractor

**File:** `src/dreal/contractor/contractor_forall.h`

Handles the **∃∀ `QF_NRA` quantifier** — `∃x. ∀y∈D. φ(x, y)` — *not* the ODE-time `forall_t` (that invariant check lives in the ODE contractor below; `docs/forall-semantics.md` §7 contrasts the two, and the `forall`/`forall_t` naming collision is a frequent confusion). `ContractorForall::Prune` runs the counterexample-guided loop of Kong, Solar-Lezama & Gao (CAV 2018, `papers/kong-solar-lezama-gao-2018-exists-forall.md`): find a `y` that violates `φ` for the current `x`-box, then contract the box with the real instantiation `φ(x, y_mid)`. It is a **well-defined pruning operator** (W1–W3 of `papers/gao-avigad-clarke-2012-delta-complete.md`), so it inherits δ-completeness from the same theorem as the algebraic contractors. Full mechanism — the two δ-regimes, the spurious-counterexample hazard, and the soundness/completeness analysis — is in `docs/forall-semantics.md` §4.

---

## IBEX Forall Pre-Pruner (`ContractorIbexForall`, `Kind::IBEX_FORALL`)

**File:** `src/dreal/contractor/contractor_ibex_forall.{h,cc}` — opt-in via `--forall-pre-prune` (off by default).

A **sound, COMPLETENESS-only** pre-pruner over IBEX's native `ibex::CtcForAll` (proj-intersection), wired *beside* — not instead of — the CEGIS `ContractorForall` above in the same forall fixpoint (`theory_solver.cc`). Unlike CEGIS (a δ-complete decision sub-procedure that pays a full nested dReal solve per `Prune`), this is **pure interval contraction**: bisect the universal box `y` to `--forall-pre-prune-prec`, contract the existential box at `mid(y)`, intersect. It cannot decide δ-sat — CEGIS stays the decider — it only skims easy prunings off the expensive loop. Soundness is by construction: it contracts the existential box only against a *real* universal point `mid(y)`, so it can never delete a true ∃∀ solution (false-`unsat` impossible). The construction's load-bearing piece is the recursive `Formula → ibex::Ctc` builder (`∧`→`CtcCompo`, `∨`→`CtcUnion`, atom→`CtcFwdBwd` over one shared `ibex::System`): the `∨`→`CtcUnion` mapping is what preserves the body's `domain ⟹ φ` implication guard (dropping it would be a false-`unsat` soundness bug — guarded by the mutation-checked unit test). Runs under `--jobs>1` via a per-worker `ContractorIbexForallMt` cell: the `CtcForAll` worklist is not shareable, so each thread lazily builds its own `ibex::CtcForAll`, keyed on `ThreadPool::get_thread_id()` (mirrors `ContractorIbexFwdbwdMt`). **Effectiveness is workload- and even encoding-specific** — see `exists_forall_perf.md` and the IBEX-lever audit `ibex_docs/AUDIT-QUANTIFIERS.md` Q1.

---

## ODE Contractor (`contractor_ode_lohner`)

**File:** `src/dreal/contractor/odes/contractor_odes.cc` (CAPD backend in `contractor_odes_capd.cc`)

> **⚠ PITFALL `forall-vs-forall_t`:** this contractor is also where the **`forall_t`** ODE
> trajectory invariant (`FormulaKind::ForallT`) is enforced (per-slice). That is unrelated to
> the ∃∀ NRA **`forall`** / `ContractorForall` (the "Forall Contractor" above). Canonical
> side-by-side: `docs/forall-semantics.md` §7.

See `docs/ode-integration.md` for a full description.

At the contractor interface level: given an ODE constraint and a time window, `contractor_ode_lohner::Prune` integrates with **CAPD** (order-20 `IOdeSolver` + `ITimeMap`) — the sole ODE backend since the Codac elimination. `run_capd_fwd` / `run_capd_bwd` return the **time-ordered per-slice tube** (each adaptive step's Taylor curve sub-gridded into `kHullGrid=16` enclosures); the `Prune` filter then walks the slices, checks the `ForallT` invariant per slice (FWD), intersects each terminal-eligible slice with the `X_t` gate, and hulls the survivors → narrowed `X_t` + time (no survivor → sound `set_empty`). The theory solver queues both a FWD (narrows `X_t`/time) and a BWD (narrows `X_0`) contractor per ODE constraint. A trivial-flow short-circuit (every RHS is the literal `0`) and a `T=0` short-circuit bypass CAPD entirely. On integration divergence (any CAPD exception, caught-and-skipped — no rethrow) the call narrows nothing for that `Prune` (sound but incomplete) but still records the diverged integral (`AddInconclusiveOde`) so the explanation can splice it back — dropping it was a false-`unsat` soundness bug (`docs/constraint-order-explanation-soundness.md`). The CAPD-integrated field must be a faithful image of the RHS — `to_capd_string` renders constants at 17 sig figs (a 6-digit truncation was a false-`unsat` soundness bug; see `docs/ode-integration.md` § Soundness). The previous Codac / CAPD-gated hybrid (and the `--capd-t-gate` / `--capd-ndim-gate` flags) was retired — see `docs/decisions.md` "ODE backend".

---

## Contractor Composition in TheorySolver

`TheorySolver::BuildContractor` constructs the composite contractor for a set of assertions:

1. For each asserted formula, create a `ContractorIbexFwdbwd` (or `contractor_ode_lohner` for ODE constraints).
2. Optionally create a `ContractorIbexPolytope` for the combined linear relaxation.
3. Wrap them all in `ContractorSeq`.
4. Wrap the sequence in `ContractorFixpoint` with a convergence termination condition.

The result is a single `Contractor` object that, when `Prune`d, repeatedly applies all constraints until convergence.

---

## Dependency Tracking

Every contractor maintains a `DynamicBitset input()` indicating which box dimensions it reads. `ContractorWorklistFixpoint` uses this to rebuild its worklist efficiently: if contractor `Cᵢ` prunes dimension `d`, only contractors with `d ∈ input()` need to be re-queued.

---

## Adding a New Contractor

1. Add a new `Kind` to `Contractor::Kind` in `contractor.h`.
2. Create `contractor_foo.h` / `contractor_foo.cc` implementing `ContractorCell`.
3. Add a factory function `make_contractor_foo(...)` and declare it as a `friend` of `Contractor`.
4. Implement `Prune(ContractorStatus* cs, const UpwardRounding& ur)`, `input()`, `include_forall()`, and `operator<<`. If the contractor wraps gaol/IBEX interval arithmetic, route the raw call through `ibex_hc4_backward` (`util/rounded_interval.h`) — passing `ur` — rather than calling `ibex::Function::backward` directly; forward `ur` to any child contractors' `Prune`. A contractor that needs `FE_TONEAREST` internally (like the CAPD ODE contractor) opens a `NearestRoundingScope` (and passes its `NearestRounding` token to nearest-regime consumers such as `run_capd_*`), then re-establishes a nested `UpwardRoundingScope` before invoking any gaol sub-contractor. The static lint `lint.py` enforces this routing.
5. Wire up construction in `TheorySolver::BuildContractor` or `context_impl.cc`.
