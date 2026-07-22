# Contractor comparison — every IBEX `Ctc*` in the fork, with dReal recommendations

> **Shipped, not opportunities.** IBEX builds `-DLP_LIB=soplex` (`CMakeLists.txt`, the `-DLP_LIB=soplex`
> line; `libsoplex.a` linked), so `--polytope`/`--forall-polytope` (LP-relaxation hull) and
> `--acid`/`--3bcid` (shaving) are **live opt-in** contractors (default off; `--acid`/`--3bcid` mutually
> exclusive). All three work under `--jobs>1` via per-worker `*Mt` cells (`contractor.cc`
> `make_contractor_ibex_{acid,polytope,forall}` dispatch to `ContractorIbex*Mt`, one `ibex::Ctc*` per
> worker thread) — **no `--jobs>1` throw**. **Newton** is the sole genuinely-absent contractor. The
> status cells below reflect this; the occurrence-count/HC4-optimality rows (e.g. `CtcFwdBwd` "optimal
> when each var occurs once") are unaffected.

Master table for all 25 contractor classes in
[`ibex-fork/src/contractor/`](../../../../ibex-fork/src/contractor/) (plus the
`qinter` kernel in `combinatorial/`). Defaults are copied from the headers /
`.cpp` actually built; **drifts between a doc-comment and the `static constexpr`
are flagged inline** (constexpr wins). Read alongside
[`../../AUDIT.md`](../../AUDIT.md) (try-order) and
[`../../dreal-ibex-usage.md`](../../dreal-ibex-usage.md) (what dReal binds today).

> **Soundness framing (project `CLAUDE.md`):** every contractor here is a
> **COMPLETENESS** lever. A looser/absent contractor costs *missed refutation*
> (`asserts φ^δ T-satisfiable on a T-unsatisfiable φ`), **never** a false `unsat`.
> Soundness lives in gaol rounding + the fork patches, not in contractor choice. A
> SAT↔UNSAT verdict flip when wiring one in = **integration bug**, not expected.

Two listed names are not contractors: **`Ctc`** is the abstract base interface
(`contract(IntervalVector&)`, `input`/`output` bitsets — see
[chapter](../../chapters/contractor.md)) and **`ContractContext`** is the
per-call context struct (output flags `FIXPOINT`/`INACTIVE`, box-property map)
threaded through `contract(box, context)`. They frame the table; they have no rows.

## (1) Atomic / numerical

| Contractor | What it does (1 line) | Key params (header defaults) | Strength | Weakness / cost | dReal status | Recommendation |
|---|---|---|---|---|---|---|
| **`CtcFwdBwd`** | forward-backward (HC4Revise) on one constraint `f(x)∈[y]` | `op=EQ`; or `(f, Domain/Interval/IntervalVector/IntervalMatrix y)`; `(NumConstraint)`; `(System,i)` | linear-time; *optimal* when each var occurs once | dependency problem when a var repeats; one constraint only | **active** — but via `Function::backward(...,callback)`, not this class ([node](./CtcFwdBwd.md)) | **keep** — it is the hot loop; the callback variant is load-bearing for lemmas |
| **`CtcHC4`** | `CtcPropag` over the per-constraint fwd-bwd of a `System` | `ratio=0.01`, `incremental=false` | stock HC4 propagation in one object | no SMT bookkeeping (explanations/lemmas) | **used** — the per-slice sub-contractor for `--acid`/`--3bcid` (stock `CtcHC4` over a fresh `System`, `contractor_ibex_acid.cc`); the standalone loop is still hand-rolled | dReal's `contractor_worklist_fixpoint.cc` is the standalone analog; the shaving path wraps a real `CtcHC4` ([node](./CtcHC4.md)) |
| **`CtcNewton`** | interval Newton / Hansen-Sengupta on a **square** system | `ceil=0.01`, `prec=default_newton_prec`, `ratio=default_gauss_seidel_ratio` | quadratic convergence near an isolated solution | needs Jacobian (fork #1 made it cold); square systems only | not used → **audit D** | **late** contractor on small boxes (`ceil` small) only ([node](./CtcNewton.md)) |
| **`CtcInverse`** | `f⁻¹(C)`: contract `[x]` w.r.t. a contractor `C` acting on `f([x])` | `(Ctc& c, Function& f)` — nothing tunable | composes a contractor through an outer map | niche; needs an inner contractor + `f` | not used → audit D | **skip** — no obvious dReal shape ([node](./CtcInverse.md)) |
| `CtcNotIn` | contract for `f(x) ∉ [y]` (union over the complement of `[y]`) | `(f, Domain/Interval/IntervalVector/IntervalMatrix y)` | handles strict-exclusion / a single negated atom | header itself flags it as maybe-obsolete vs language disjunction | not used — dReal negates in DPLL(T) | **skip** |
| `CtcInteger` | contract integer vars to their largest integer subinterval | `(int nb_var, const BitSet& is_int)` | integrality pruning | — | dReal has its own `contractor_integer` | **skip** (dReal equivalent) |
| `CtcIdentity` | `[x] ↦ [x]` (no-op) | `(int n)` | arity-0 neutral element for operators | — | n/a | **skip** (trivial) |
| `CtcEmpty` | contract to ∅ iff a predicate `Pdc` returns YES on the box | `(int n)` or `(Pdc& pdc, bool own=false)` | predicate-gated emptiness | — | n/a | **skip** (trivial) |

## (2) Composition operators (contractor programming)

| Contractor | What it does (1 line) | Key params (header defaults) | Strength | Weakness / cost | dReal status | Recommendation |
|---|---|---|---|---|---|---|
| **`CtcCompo`** | sequential `C_n(…C_1([x]))` (overloads for 1–20 sub-ctcs) | `incremental=false`, `ratio=0.1` (`default_ratio`) | chain atomic contractors | order-sensitive; no propagation skip unless `incremental` | not used → dReal's `contractor_seq.cc` | dReal analog exists ([node](./Operators.md)) |
| **`CtcUnion`** | `□(C_1([x]) ∪ … ∪ C_n([x]))` (hull of the union) | list / `(System sys)` builds the negation contractor / 2–20 overloads | disjunctive contraction | hull loses the gap between branches | not used → dReal's `contractor_join.cc` | dReal analog exists ([node](./Operators.md)) |
| **`CtcFixPoint`** | iterate one `C` until Hausdorff step `< ratio·diam` | `ratio=0.1` (`default_ratio`, comment agrees) | wraps any `C` into its fixpoint | ratio gives no guarantee on distance-to-fixpoint | not used → dReal's `contractor_fixpoint.cc` | **B1** cross-check the ratio regime ([node](./Operators.md)) |
| **`CtcPropag`** | AC3-like agenda fixpoint over `{C_i}`, skipping via input/output bitsets | `ratio=0.01` **⚠ member-comment says 0.1 — `constexpr=0.01` wins**; `incr=false`; `accumulate=false` | scales on sparse systems (fires only impacted ctcs) | needs input/output bitsets or it degrades to a plain fixpoint | not used → dReal's `contractor_worklist_fixpoint.cc` | **B1** cross-check stop-ratio + `accumulate` ([node](./Operators.md)) |

## (3) Shaving / constructive disjunction

| Contractor | What it does (1 line) | Key params (header defaults) | Strength | Weakness / cost | dReal status | Recommendation |
|---|---|---|---|---|---|---|
| **`CtcAcid`** ⭐ | **adaptive** 3BCID — auto-tunes how many vars to shave (`nbcidvar`) | `optim=false`, `s3b=10`, `scid=1`, `var_min_width=1e-11`, `ct_ratio=0.002` **⚠ ctor-comment says 0.005 — `constexpr=0.002` wins** | **IBEX's own solver enables it by default**; self-regulating cost | needs a `System` (var ordering); calls sub-ctc many times/box | **shipped** — `--acid` (default off); assembles a `System`, wraps a stock `CtcHC4` sub-ctc (`contractor_ibex_acid.cc`); `--s3b`/`--acid-ct-ratio` tune it | enable per-project + `/benchmark`; strongest general lever ([node](./CtcAcid.md)) |
| **`Ctc3BCid`** | 3B shave + CID constructive-disjoin per variable (fixed params) | `s3b=10`, `scid=1`, `vhandled=-1` (all vars), `var_min_width=1e-11`; `LimitCIDDichotomy=16` | strong contraction, fully explicit knobs | per-box cost; manual `s3b` tuning | **shipped** — `--3bcid` (default off, excl. `--acid`); same assembly, fixed `--s3b` | sweep `--s3b` {5–200} before trusting ACID's auto-tune ([node](./Ctc3BCid.md)) |
| `CtcOptimShaving` | **left-only** 3BCID shaving of the *objective* variable (optimization) | inherits `s3b=10`, `scid=1`, `vhandled=-1`, `var_min_width=1e-11`; `LimitCIDDichotomy=100` | tightens an objective bound in B&B | optimization-only; shaves one var (`start_var`), left bound only | not used (dReal has no IBEX `Optimizer`) | **skip** (NLP-only) ([node](./CtcOptimShaving.md)) |

## (4) Quantifier

| Contractor | What it does (1 line) | Key params (header defaults) | Strength | Weakness / cost | dReal status | Recommendation |
|---|---|---|---|---|---|---|
| `CtcExist` | `∃y∈[y] c(x,y)`: bisect `y` to `prec`, contract, **proj-union** onto x | `prec` **(required — no default)**, `y_init` | generic ∃ over a parameter box | exponential in `dim(y)`; ε must be adaptive | not used — dReal has CEGIS ∃∀ | design-comparison only ([node](./Quantifiers.md)) |
| `CtcForAll` | `∀y∈[y] c(x,y)`: same split, **proj-inter** onto x | `prec` **(required)**, `y_init` | generic ∀ over a parameter box | exponential in `dim(y)` | not used — dReal has CEGIS ∃∀ | design-comparison only ([node](./Quantifiers.md)) |
| `CtcQuantif` | abstract base for the two above (`LargestFirst` bisector + `VarSet`) | `prec` **(required)** | shared bisect/contract loop | abstract — not used directly | not used | reference ([node](./Quantifiers.md)) |

## (5) Linear / LP relaxation

| Contractor | What it does (1 line) | Key params (header defaults) | Strength | Weakness / cost | dReal status | Recommendation |
|---|---|---|---|---|---|---|
| **`CtcPolytopeHull`** | linearize, then `2n` LP solves (min/max each var) → hull of the relaxation | `max_iter=100`, `time_out=100`s, `eps=1e-9` **⚠ header-comment says 1e-10 — `LPSolver` constexpr 1e-9 wins** | global linear cuts HC4 can't see | `2n` LP solves per box; `LP_LIB=soplex` now linked | **shipped** — `--polytope`/`--forall-polytope` (default off); `LinearizerXTaylor(RELAX,RANDOM_OPP,HANSEN)` (`contractor_ibex_polytope.cc`) | unmeasured on main NRA path — `/benchmark` before any default ([node](./CtcPolytopeHull.md)) |
| `CtcLinearRelax` | `CtcPolytopeHull` over an internally-built `LinearizerXTaylor(ExtendedSystem)` | `(const ExtendedSystem& sys)` — inherits PolytopeHull LP defaults | turnkey X-Taylor relaxation | `ExtendedSystem` only; hides the corner/slope knobs | not wired — dReal calls `CtcPolytopeHull` directly via `--polytope`; LP backend now linked | subsumed by `--polytope`; prefer the explicit `CtcPolytopeHull`+`LinearizerXTaylor` for knob access ([node](./CtcLinearRelax.md)) |

## (6) Optimization-only (KKT / first-order)

| Contractor | What it does (1 line) | Key params (header defaults) | Strength | Weakness / cost | dReal status | Recommendation |
|---|---|---|---|---|---|---|
| `CtcKuhnTucker` | contract by first-order **KKT** conditions of an NLP (Newton-based) | `(NormalizedSystem& sys, bool reject_unbounded=true)` | sound first-order pruning near optima | **costly to build** (symbolic gradients, no auto-Hessian); NLP-only | not used | capability-extension (rigorous OMT), not a perf lever ([node](./CtcKuhnTucker.md)) |
| `CtcKuhnTuckerLP` | same KKT, via **LP** (`CtcPolytopeHull`) instead of Newton | `(NormalizedSystem& sys, bool reject_unbounded=true)` | avoids Newton preconditioning pessimism | costly build; NLP-only | not used | same as `CtcKuhnTucker` (no dReal `Optimizer`); LP backend no longer a blocker ([node](./CtcKuhnTucker.md)) |

## (7) Robust / outlier-tolerant

| Contractor | What it does (1 line) | Key params (header defaults) | Strength | Weakness / cost | dReal status | Recommendation |
|---|---|---|---|---|---|---|
| `CtcQInter` | **q-intersection**: smallest box covering points in **≥ q** of the contracted boxes | `(Array<Ctc>& list, int q)` | robust to up to `n−q` faulty/outlier constraints (measurement noise) | combinatorial `qinter` kernel; refutation, not robustness, is dReal's problem | not used → audit D4 | **skip** — no dReal use shape ([node](./CtcQInter.md)) |

---

## Status — the audit's top candidates shipped

The three headline levers are **implemented opt-in contractors** (default off), no longer
opportunities. What each turned into:

1. **`CtcAcid` shaving — `--acid`.** IBEX's own default lever, shipped over dReal's HC4 path.
   `ContractorIbexAcid` assembles an `ibex::System` over the box vars + assertions and wraps
   `ibex::CtcAcid` on a **stock `ibex::CtcHC4`** sub-contractor (`contractor_ibex_acid.cc`) — *not*
   dReal's callback-bearing fwd-bwd, so used-constraint tracking is coarse: `AddUsedConstraint(formulas_)`
   marks the whole formula set used on any narrowing, no per-variable lemma attribution. `--s3b` /
   `--acid-ct-ratio` expose shave depth / adaptive-stop.
2. **`Ctc3BCid` fixed-param shaving — `--3bcid`** (mutually exclusive with `--acid`). Same assembly,
   `ibex::Ctc3BCid` instead; `--s3b` is the priority knob (best 5–200) — sweep it before trusting
   ACID's auto-tune.
3. **`CtcPolytopeHull` LP-relaxation hull — `--polytope`** (and `--forall-polytope`). `LP_LIB=soplex`
   is linked, so the `2n` LP solves run; `LinearizerXTaylor(RELAX, RANDOM_OPP, HANSEN)`
   (`contractor_ibex_polytope.cc`). Unmeasured on the main NRA path; `--forall-polytope` measured
   *mixed* (`exists_forall_perf.md`). All three dispatch to per-worker `*Mt` cells under `--jobs>1`.

Validation rule for any of them: PAR2 < baseline = win, **any verdict flip = integration bug** (these
are COMPLETENESS levers only).

## Still open

- **Cross-check dReal's worklist stop-ratio against `CtcPropag` (0.01) / `CtcFixPoint` (0.1) (B1).**
  Cheap A/B on the path dReal already runs; expected small (the micro-opt budget is spent).
- **`CtcNewton` as a *late* contractor (D1).** Square, solution-isolating small boxes only;
  reintroduces the gradient build fork #1 made cold — gate `ceil` small. The **sole genuinely-absent**
  IBEX contractor.
- **Integration-guard checklist (C1, correctness not speed).** Any borrowed contractor must detect
  emptiness by `is_empty()` (#11), run under `UpwardRoundingScope` (#8/#9/#10/#12), and tolerate
  transient DPLL(T) literals — all satisfied by the shipped shaving/polytope cells.

**Correctly skipped** (different problem shape — recorded so they aren't re-investigated):
`CtcInverse`, `CtcNotIn`, `CtcQInter`, `CtcKuhnTucker(LP)`, `CtcOptimShaving` (NLP/robustness/inverse —
not refutation); the composition operators `CtcCompo`/`CtcUnion`/`CtcFixPoint`/`CtcPropag` (dReal
hand-rolls these inside DPLL(T)); the quantifier trio `CtcExist`/`CtcForAll`/`CtcQuantif` (dReal has its
own CEGIS ∃∀ — design-comparison only); and the trivial `CtcIdentity`/`CtcEmpty`/`CtcInteger` (no-op /
equivalent already in dReal). `CtcHC4` is **not** skipped — it is the shipped shaving sub-contractor.
`LinearizerAffine2` is **not in the fork** (the `ibex-affine` plugin) — a plugin-integration task, not a
knob.

## Source-fidelity drifts found (constexpr is ground truth)

| Class | Doc-comment says | `static constexpr` says | Where |
|---|---|---|---|
| `CtcPropag` | member comment "set to 0.1" | `default_ratio = 0.01` | `ibex_CtcPropag.h:84-85` |
| `CtcAcid` | ctor-comment "default value is 0.005" | `default_ctratio = 0.002` | `ibex_CtcAcid.h:46,94` |
| `CtcPolytopeHull` | header-comment `eps … 1e-10` | `LPSolver::default_tolerance = 1e-9` | `ibex_CtcPolytopeHull.h` / `LPSolver` |
