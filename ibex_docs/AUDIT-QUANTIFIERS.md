# AUDIT (second section) — leveraging IBEX for nested-quantifier queries

> ⚠ **PARTLY STALE on the LP/polytope claims (verified 2026-07-02)** — written pre-commit
> `fa3b74bd7`. `--forall-polytope` (`use_polytope_in_forall`) is **no longer "broken by `LP_LIB=none`"
> / crashing**: IBEX now builds `-DLP_LIB=soplex` (`CMakeLists.txt:189`, `dreal_main.cc:494-498`), so
> the Q7/D2 "revive polytope-in-forall" item is **done** and the flag is functional (measured *mixed*
> — helps some encodings, hurts others: `exists_forall_perf.md:195-197`). The quantifier cost-law /
> pre-prune / nested-crash findings are unaffected and still hold. Full correction:
> [`README.md`](README.md) top banner.

A focused companion to [`AUDIT.md`](AUDIT.md), for an upcoming workload of **∃∀
and deeper (`∀∃∃∀`) queries over high-dimensional, highly-nonlinear, transcendental
constraints — no ODEs, no Boolean structure.** Background (do not overfit):
[`../../ode_expressivity_energy/docs/design.md`](../../ode_expressivity_energy/docs/design.md)
§6 ("keep dReal in the universal fragment"; its stated residual risk is *"a ∀ over a
high-dimensional transcendental box can still be slow"*). The dReal-side ground
truth is the project's own [`../docs/forall-semantics.md`](../docs/forall-semantics.md);
the IBEX-side mechanics are verified here against the fork headers/`.cpp`.

## The two machineries are complementary, not competing (read this first)

The whole audit hinges on one distinction, verified on both sides:

| | dReal `ContractorForall` (CEGIS) | IBEX `CtcForAll` / `CtcExist` |
|---|---|---|
| **Kind** | a δ-complete **decision** sub-procedure | a sound interval **contractor** (pruner) |
| **How** | counterexample-guided: nested δ-solve finds a `y` violating φ, pins it, contracts `x` (`forall-semantics.md` §4; CAV 2018) | proj-inter / proj-union: bisect the parameter box `y` to `prec`, contract `(x, mid(y))`, intersect (∀) / union (∃) (`ibex_CtcForAll.cpp:37-101`, verified) |
| **Decides δ-sat?** | **yes** — δ-complete for ∃∀ (`forall-semantics.md` §4.5) | **no** — only prunes; no δ, no strengthening |
| **Cost** | each `Prune` = a **full nested dReal solve**, ~10–100× per forall constraint (`forall-semantics.md` §6.7) | exponential in `dim(y)`: `O((rad(y)/prec)^{dim y})` (IBEX `contractor.rst` warning) |
| **Witness** | a **point** counterexample (CE) | interval contraction at `mid(y)` |
| **Nesting** | **depth-one only** — nested quantifiers *crash* (`DeltaStrengthen` throws, `symbolic.cc:399`; `forall-semantics.md` §6.1) | **composes natively** — `CtcForAll`/`CtcExist` take *any* `Ctc`, so they nest (`ibex_CtcQuantif.h:59`, verified) |

**The thesis:** IBEX's quantifier contractors are a *cheap sound pruner* that
**feeds** dReal's expensive *δ-complete decider*, and — because they nest natively
— a *structural skeleton* for the alternations dReal currently can't express at
all. They never replace the CEGIS decision; they shrink the work it does and
extend the shapes it can attack.

> **Soundness/completeness (mandatory — `CLAUDE.md`, `docs/soundness-vs-completeness.md`):**
> every lever here is a **COMPLETENESS / performance** lever. `CtcForAll`'s
> proj-inter removes only `x` that fail `φ(x, mid(y))` for a **real** `y∈[y]`, so
> it can never delete a true ∃∀-solution — adding it is sound by construction
> (`asserts nothing T-unsatisfiable`; the worst case is "pruned less / slower," a
> COMPLETENESS cost, never a false `unsat`). The deeper-nesting enabler (Q2)
> grants *pruning* on `∀∃∃∀`, **not** δ-completeness for that fragment — that
> remains an open problem (CAV 2018's theorem is ∃∀ only).

## The three axes (mapped to the project's own rungs, design.md §6)

1. **Make the ∃∀ they already use more tractable** (the "elegant, try-first" rung) → Q1, Q3, Q4, Q7.
2. **Make the pure-`∀` fallback faster** (high-dim transcendental `∀`-box = plain NRA refutation `∃x¬φ`) → this is exactly [`AUDIT.md`](AUDIT.md) tier A (ACID/3BCID); see Q3.
3. **Enable the `∀∃∃∀` they currently crash on** → Q2, Q6 (a pruning skeleton + research direction).

## Leverage items (ranked)

| Q# | Lever | Axis | Ease | Likely value for these queries | Validate |
|---|---|---|---|---|---|
| **Q1** | `CtcForAll`/`CtcExist` as a **cheap, shallow sound pre-pruner** before/around CEGIS | 1 | 3 | **high** (skims easy prunings off the 10–100× loop) | `exist_forall_*.smt2` (Ackley3/4) + the project's transcendental ∃∀ set; PAR2, no verdict flips |
| **Q2** | **Nested composable** `CtcExist(CtcForAll(…))` as the pruning skeleton for `∀∃∃∀` | 3 | 2 | **enabling** (dReal currently *crashes*) | a hand-built `∀∃∃∀` micro-instance; check sound pruning, document it's pruning-not-deciding |
| **Q3** | **Stronger inner contractor** (ACID/3BCID/HC4) inside the quantifier *and* inside the CE-search context | 1+2 | 2 | **high, compounds** (exp-in-`dim(y)` ⇒ inner tightening pays off super-linearly) | same as AUDIT A1, on ∃∀ instances |
| **Q4** | **Adaptive `prec`** on the `y`-split + tune `inner_delta` for high-dim `y` | 1 | 4 | med (controls the `dim(y)` blow-up) | sweep `prec`/`inner_delta`; watch tractability cliff |
| **Q5** | **Smear-ordered** parameter splitting (split the `y` that most affects φ first) | 1 | 2 | med (high-dim transcendental) | needs a small `CtcQuantif` patch — see note |
| **Q6** | **Inner arithmetic / inner regions** (box witnesses) for the `∃` direction | 3 | 2 | med (accelerates the `∃` blocks of `∀∃∃∀`) | micro-bench `∃`-heavy instances |
| **Q7** | **Polytope-in-forall** — SHIPPED `--forall-polytope` (`use_polytope_in_forall`, the CAV 2018 CLP path; `LP_LIB=soplex` now linked) | 1 | ✓ done | measured *mixed* (helps some encodings, hurts others) | `exists_forall_perf.md:195-197`; rerun per new encoding |

### Q1 — `CtcForAll`/`CtcExist` as a cheap sound pre-pruner  ⭐ (accelerate existing ∃∀)
- **What it buys:** dReal's `ContractorForall::Prune` pays a *full nested solve* every
  iteration (`forall-semantics.md` §6.7). A single pass of IBEX `CtcForAll` over the
  same `∀y φ(x,y)` is **pure interval contraction** — no nested SAT, no δ-solve — that
  soundly shrinks the `x`-box. Run it *shallow* (coarse `prec`, few `y`-splits) as a
  pre-filter so CEGIS starts from a smaller candidate box and converges in fewer
  expensive CE iterations.
- **Mechanism (verified):** `CtcForAll(Ctc& c, BitSet vars, y_init, prec)`
  (`ibex_CtcForAll.h:52`) — `vars` marks which components of the inner contractor are
  the `∀`-parameters `y`; it bisects `y` (LargestFirst), contracts `(x, mid(y))` with
  `c`, and intersects, emptying `x` as soon as any `y` kills it
  (`ibex_CtcForAll.cpp:44`). Sound proj-inter.
- **Integration:** add it as an extra contractor in the `forall` constraint's slot in
  `generic_contractor_generator.cc`, *alongside* (not replacing) `ContractorForall`
  (built at `theory_solver.cc:193`). It composes into dReal's existing fixpoint
  loop like any other `Ctc`.
- **Cost/risk:** exponential in `dim(y)` if `prec` is small — so keep it **shallow**.
  For high-`dim(y)` it must be a skimmer, not a solver (a deep `CtcForAll` would be
  *worse* than CEGIS, whose CE search is targeted rather than exhaustive). It cannot
  decide δ-sat — CEGIS stays the decider. Node:
  [`classes/contractors/Quantifiers.md`](classes/contractors/Quantifiers.md).
- **IMPLEMENTED + MEASURED (2026-06-30):** shipped as `--forall-pre-prune`
  (`src/dreal/contractor/contractor_ibex_forall.{h,cc}`, run beside CEGIS in the forall
  fixpoint at `theory_solver.cc`). Sound (proj-inter + implication guard preserved by a
  recursive Formula→`ibex::Ctc` builder: `∧`→`CtcCompo`, `∨`→`CtcUnion`, atom→`CtcFwdBwd`
  over one shared `ibex::System`); no verdict flips anywhere. **The speedup is real but
  encoding-fragile:** on the *first* `odeexpr_v2` encoding it gave ~40–100× at δ=0.2–0.35 on
  `mlp2_n1_h1`, but a same-day re-encoding made those cases 0 s at baseline (no headroom
  left) and pre-prune rescued none of the still-hard cases — full record in
  `exists_forall_perf.md`. **No rescue at the pinned δ=0.0005** under any encoding (the
  existential-isolation wall, as predicted). Lessons for Q3–Q7: (a) re-validate per
  workload, the benefit does not carry; (b) `prec` (Q4) is near-irrelevant once it's
  ≥ ~half the universal-box width — and *inverts* for an unsat goal (finer ⇒ stronger
  refutation), so the "coarse is better" rule is SAT-specific. Runs under `--jobs>1` via a
  per-worker `ContractorIbexForallMt` cell (one `ibex::CtcForAll` per thread, keyed on
  `ThreadPool::get_thread_id()`; the worklist is not shareable, so each worker builds its
  own — mirrors `ContractorIbexFwdbwdMt`).

### Q2 — Nested composable contractors: the path to `∀∃∃∀`  ⭐ (enable the unsupported)
- **The gap:** dReal **crashes** on any nested quantifier (`DeltaStrengthen` throws on a
  `forall` inside a body, `symbolic.cc:399-401`; `forall-semantics.md` §6.1). Its §8
  workaround is external skolemization / iterative CEGIS — dReal has *no* native
  mechanism for `∀∃∃∀`.
- **What IBEX gives:** `CtcExist`/`CtcForAll` are `Ctc`s that **take a `Ctc`**
  (`ibex_CtcQuantif.h:59`, verified), so a prenex block `∀a ∃b ∃c ∀d φ` maps to a
  **nested contractor** `CtcForAll_a( CtcExist_b( CtcExist_c( CtcForAll_d( HC4(φ) ))))`,
  each level carrying its own parameter `BitSet`, `y_init`, and `prec`. This is a
  **sound interval pruner for the whole alternation** — a capability where dReal
  currently has only a crash.
- **Honest boundary:** this prunes; it does **not** decide δ-sat for `∀∃∃∀` (no
  strengthening, no completeness theorem for >1 alternation). Treat Q2 as **(a)** an
  immediate *sound pruner* that makes deeper-alternation instances tractable to attack,
  and **(b)** the concrete substrate for a research extension of the CAV 2018 CEGIS to
  multiple alternations. It is the single most relevant IBEX lever for the project's
  `∀∃∃∀` ambition, precisely because the alternative today is "unsupported."
- **Cost/risk:** the `dim` exponential **stacks across levels** — only viable for small
  per-block parameter dimension; pair hard with Q4 (adaptive prec) and Q3 (strong inner
  contractor). Architecture context:
  [`ARCHITECTURE-COMPARISON.md`](ARCHITECTURE-COMPARISON.md) (why dReal's ∀ machinery is
  its own — Q2 augments, doesn't replace it).

### Q3 — Stronger inner contractor inside the quantifier (compounds AUDIT tier A)
- The contractor that runs *inside* the quantifier — both IBEX's `CtcForAll`
  sub-contractor and dReal's inner CE-search context (`context_for_counterexample_`,
  `forall-semantics.md` §4.1) — sets how tightly each `(x, y)` sub-box is contracted.
  Tighter inner contraction ⇒ **fewer `y`-splits and faster CE convergence**, and
  because the quantifier cost is *exponential* in `dim(y)`, a constant-factor inner
  tightening pays off **super-linearly** here. Transcendentals are handled by HC4
  fwd-bwd already; **`CtcAcid`/`Ctc3BCid`** (the [`AUDIT.md`](AUDIT.md) tier-A headline)
  tightens further. So tier A is *more* valuable for quantified queries than for plain
  NRA. `--acid`/`--3bcid` are now **shipped** but only top-level (over the whole assertion
  system — `theory_solver.cc:240-244`, `contractor_ibex_acid.{cc,h}`); dReal still uses
  **HC4-only *inside* the quantifier** — both `CtcForAll`'s `CtcFwdBwd` leaves and the
  CE-search `context_for_counterexample_` (`theory_solver.cc:207`, verified). Wiring
  ACID/3BCID into that inner context is the unshipped lever.

### Q4 — Adaptive precision + the `inner_delta` knob (tame the `dim(y)` blow-up)
- IBEX's quantifier cost is `O((rad(y)/prec)^{dim y})`; the docs prescribe **adaptive
  `prec`** (set it relative to the current `x`-box width — `contractor.rst`
  "Adaptative precision"). For dReal's CEGIS the analogous knob is the
  `inner_delta < epsilon < delta` regime (`forall-semantics.md` §4.1); `inner_delta`
  controls the nested CE-search precision. For high-dim transcendental `y`, both are
  the difference between tractable and not. Cheap to sweep; no code structure change.

### Q5 — Smear-ordered parameter splitting (which `y` to bisect first)
- `CtcQuantif` **hardcodes `LargestFirst`** for the `y`-split (`bsc` is a
  `LargestFirst*`, `ibex_CtcQuantif.h:90`, verified). A **smear** bisector — split the
  `y` whose Jacobian-weighted width most affects φ (`SmearSumRelative`, see
  [`classes/strategy/Bisectors.md`](classes/strategy/Bisectors.md)) — would cut the
  number of splits on high-dim transcendental parameter boxes. Requires a small patch
  to `CtcQuantif` to accept a custom `Bsc` (it currently doesn't), so it's a deeper
  item — but a natural fork patch given dReal already maintains a fork patch series
  (`../ibex-fork/MIGRATION.md`).

### Q6 — Inner arithmetic / inner regions for the `∃` direction (box witnesses)
- For an `∃y φ(x,y)` block (the `∃` levels of `∀∃∃∀`), dReal/`CtcExist` certify by
  finding a point/`mid(y)`. IBEX **inner arithmetic** (`]f[`, `ibwd_*`; Araya 2014) can
  prove a whole `y`-sub-box is feasible — a **box witness** — without splitting to
  `prec`, accelerating the `∃` certification. Caveat (verified in the interval chapter):
  inner operators are *not implemented for all elementary functions yet*, so
  transcendental coverage is partial — confirm per-function before relying on it.
  Reference: [`chapters/interval.md`](chapters/interval.md) §inner arithmetic.

### Q7 — Polytope-in-forall (the CAV 2018 CLP path) — SHIPPED
- `use_polytope_in_forall` (`config.h:331`, default `false`) routes the CE-search context
  through IBEX **polytope** contractors — *"the LP pruning the CAV 2018 implementation ran
  on CLP"* (`forall-semantics.md` §6.7). Now **live**: IBEX builds with `-DLP_LIB=soplex`
  (`CMakeLists.txt:189`), so passing `--forall-polytope` (`dreal_main.cc:494-498`) runs the
  real LP relaxation instead of crashing — the same build decision as [`AUDIT.md`](AUDIT.md)
  D2, motivated here because CAV 2018 used it for ∃∀. **Measured *mixed*** (helps some
  encodings, hurts others — `exists_forall_perf.md:195-197`), never a verdict flip
  (COMPLETENESS/perf only). Transcendental applicability is fine (X-Taylor uses Hansen
  slopes of the derivatives). Enable per-encoding and measure.

## Honesty boundary (proven vs hypothesized)
**Proven** (verified in source this pass): the IBEX quantifier-contractor mechanics
and that they nest (`ibex_CtcQuantif.h:59`, `ibex_CtcForAll.cpp`); that `CtcQuantif`
hardcodes `LargestFirst`; that dReal is depth-one and crashes on nesting, uses
HC4-only inside, and that `use_polytope_in_forall` is now **live** under `LP_LIB=soplex`
(shipped `--forall-polytope`, measured mixed)
(the latter three via the project's cross-referenced `forall-semantics.md` + the
config/theory_solver anchors confirmed in [`AUDIT.md`](AUDIT.md)/[`dreal-ibex-usage.md`](dreal-ibex-usage.md)).
**Measured** (2026-06-30): **Q1** is now implemented and benchmarked — see its
"IMPLEMENTED + MEASURED" note above (sound; speedups real but **encoding-fragile** — large
on the first `odeexpr_v2` encoding, gone after a re-encoding; no rescue of the δ⁻ⁿ
existential wall under any encoding). **Q7** (`--forall-polytope`) is likewise shipped and
measured *mixed* (`exists_forall_perf.md`). **Hypothesized** (still NOT benchmarked):
every *other* speedup/tractability claim (Q2–Q6) and the ranking. The dominant risk for all
of Q1/Q2/Q5 is the **`dim(y)` exponential** — on
genuinely high-dimensional parameter blocks, exhaustive interval quantification can
be *worse* than CEGIS's targeted CE search; these levers are "cheap shallow pruning +
strong inner contractor," not "replace CEGIS with `CtcForAll`." Start validation with
Q3 (strong inner contractor, lowest risk, compounds) and Q1 (shallow pre-prune) on
the existing `exist_forall_*.smt2` transcendental set, measuring CE-iteration count
and PAR2, before touching Q2's deeper-alternation substrate.
