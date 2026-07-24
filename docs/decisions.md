# Design Decisions (ADRs)

Topic-keyed architecture/soundness decision records. Each entry states the decision, the
load-bearing reason that survives even if the decision changes, the alternatives tried and
rejected, and the implementation site. Performance tried/rejected experiments live in
`OPTIMIZATION_LOG.md`; this file is the architectural + soundness record.

---

## ODE backend: CAPD-only (Codac removed)

**Decision:** CAPD master (`CAPD_INTERVAL_TYPE=NATIVE`, order-20 `IOdeSolver` + `ITimeMap`) is
the sole ODE backend, source-built from the IBEX fork (`ncsys-lab/ibex-lib@dreal-perf-patches`)
+ CAPD. Codac and Eigen3 are no longer dependencies.

**Why:** Two prior stacks were tried and discarded. (1) The pre-Codac `ncsys-lab` IBEX/CAPD
fork was an unmaintained x86-only snapshot (CAPD 4.x pulled FILIB, which `FATAL_ERROR`s on
non-x86). (2) Codac v2 + `lebarsfa/ibex-lib` got onto a maintained ARM64 stack but at order-2
Taylor (`CtcLohner`, hardcoded), and introduced a **dominant non-ODE slowdown** that was
root-caused by profiling: ~65% of `1mhz` saradc wall time was `ibex::Function::init` building
the `Gradient` / `ExprLinearity` objects unconditionally on every contractor-cache miss — work
`Function::backward` (the only path HC4 uses) never reads. The fix is the fork's lazy-grad
patch (build `_grad` on first use), which made CAPD-only viable without Codac.

**Alternatives tried and rejected:**
- *Keep Codac as the short-horizon "fast path" (gated hybrid).* Eliminated by direct
  measurement: native ARM64 CAPD order-20 was **never slower** than Codac order-2 across every
  tested benchmark, and ~23% faster on the canonical `bouncing_ball` reference where Codac was
  supposed to win — higher Taylor order needs fewer timesteps, and ARM64 per-step overhead is
  low enough that fewer steps wins. The ODE backend is not the bottleneck anyway (SAT layer +
  IBEX fwdbwd + `forall_t` dominate wall time).
- *Fork `lebarsfa/ibex-lib` and keep Codac.* `lebarsfa` was 100+ commits behind
  `ibex-team/ibex-lib` and not project-controlled; Codac shipped as a per-arch-per-OS prebuilt
  ZIP (a maintenance liability on each new macOS/glibc). Forking `ibex-team` directly and
  dropping Codac removes both — `IBEXConfig.cmake` (lebarsfa's only needed feature) was required
  only by Codac's own build, and dReal wires IBEX via manual IMPORTED targets, never
  `find_package(IBEX)`.

**Code:** `CMakeLists.txt` (`ExternalProject_Add(ibex_external)` + `capd_external`);
`src/dreal/contractor/odes/`. See `DEPENDENCIES.md` for the current build wiring and
`../ibex-fork/MIGRATION.md` for the fork patch catalog.

---

## Per-slice ODE tube, not a coarse endpoint hull

**Decision:** The ODE contractor builds the **time-ordered per-slice tube** (each adaptive
step's Taylor curve sub-gridded into `kHullGrid=16` enclosures) and filters slice-by-slice —
it does not intersect the terminal box with a single endpoint enclosure.

**Why:** The single-endpoint form is **unsound for free-time integrals**. Intersecting only
the `enclosure(t_ub)` over-narrows away a solution reached at an *interior* time `< t_ub`,
producing a false `unsat` — the catastrophic direction for a delta-complete solver. Proven on
`github_oct5_0hz_k2_prostate_cancer_*`: the coarse form returned `unsat` in 0.03 s while the
per-slice form (and cav26) return `delta-sat` with a witness at interior times ≈1.7–4.4 «
horizon 20. A coarse hull also collapses per-time/per-component correlation, under-refuting
anti-correlated tubes and missing interior-only invariant violations.

**Alternatives tried and rejected:** the coarse single-endpoint intersection (the
Codac→CAPD rewrite's form) — unsound as above. dReal3 segfaults on these inputs, so cav26 +
the explicit witness are the oracle.

**Code:** `contractor_ode_lohner::Prune` + `integrate_tube_slices`
(`src/dreal/contractor/odes/`). Mechanism detail: `docs/ode-integration.md` §Mechanism.
Regression coverage: `contractor_odes_semantic_test.cc` (`GravityInvariantTest`,
`AntiCorrelatedTest`, `DecayFlowTest.*`).

---

## ODE feed faithfulness: 17-digit constant rendering

**Decision:** `to_capd_string(double)` renders every constant at
`std::numeric_limits<double>::max_digits10` (**17**) significant digits, so CAPD's interval
parse of the decimal literal brackets the exact double.

**Why:** The vector field CAPD integrates must be a faithful image of the symbolic RHS; the
prior `std::to_string`'s 6-digit truncation made it unfaithful by `1e-6` and produced a genuine
false `unsat` (**SOUNDNESS** — false unsat). Mechanism + the worked `1/3 → 0.999999` clock-gate
example: `docs/ode-integration.md` §Soundness. The bug was longstanding and shared by
`main`/cav26; their looser filters masked it while the tighter per-slice tube exposed it — *the
feed lied, the filter is sound*. cav26-oracle A/B evidence (123 ODE jobs): the fixed build has
**zero** false-`unsat`s, solves 118/123 vs cav26's 109, and fixes 2 of cav26's own residual
false-`unsat`s.

**Alternatives tried and rejected:** `std::to_string` (the truncating default — the bug);
scientific-notation output (CAPD's parser is unreliable on `1e-3` forms, so |v|<1e-4 or huge
values are re-rendered as `fixed`-`setprecision(40)`).

**Code:** `src/dreal/contractor/odes/to_capd_string.h` (carries
`DREAL_ASSERT_ROUNDING(FE_TONEAREST)` — decimal formatting is correctly rounded only in
nearest). Repros: `ode_soundness_repros/ws_taupin.smt2`. Tests:
`to_capd_string_test.cc::ConstantRoundTripsExactly`, `contractor_capd_test.cc::{CapdFwd,CapdBwd}`
(cumulative-gaussian witness `p=Φ(10)−Φ(-10)<1`). Mechanism detail: `docs/ode-integration.md`
§Soundness.

---

## Denormal / underflow soundness (dreal/dreal4#321)

**Decision:** A value like `2^-1075` (`pow(0.5,1075)`) underflows; it is made sound in **two
layers** — an ibex HC4-backward fork patch and a Drake constant-fold guard.

**Why:** The underflow surfaces as a genuine false `unsat` in Drake folding and as a
subnormal-scale delta-completeness imprecision in the ibex backward.

- **ibex HC4 backward** (fork patch `underflow_saturate`): a forward op soundly
  over-approximates an underflowed result to the subnormal ceiling (`pow(0.5,1075) →
  [0, DBL_TRUE_MIN]`), but the *tight* inverting backward ops (`bwd_pow`/`exp`/`sqr`/`mul`/`div`)
  were tighter than the forward and emptied a feasible operand on a subnormal-band target.
  `underflow_saturate(y)` widens a target lying entirely in the subnormal band to include 0
  before the tight inverse (keying on the endpoint farthest from 0, so `[DBL_TRUE_MIN,+inf]` is
  left alone — that keeps `bwd_div08` etc. unchanged). Siblings `bwd_sqrt`/`log`/`root` invert
  via a loose forward op and were already sound.
- **Drake constant fold** (`sound_constant_fold`): the parser builds an exactly-representable
  literal (`0.5`, integers) as a `Constant` (double), so `pow(0.5,1075)` eagerly folds via
  `std::pow` where `2^-1075` rounds to `0.0` — a lying literal making `pow(0.5,1075) > 0` a
  false `unsat`. `sound_constant_fold` (at the `pow`/`mul`/`div` fold sites) detects an
  unfaithful fold (nonzero true value underflowed to ±0, or finite overflowed to ±inf) and
  folds to a sound `RealConstant` interval bracketing the true value (`[0, DBL_TRUE_MIN]`,
  `[DBL_MAX, +inf]`, …) instead of the scalar. It stays a *constant* — a `Pow`/`Mul` with
  all-`Constant` operands violates a Drake AST invariant (`ExpressionMulFactory::AddTerm`
  asserts it) — yet is sound.

**Accepted tradeoff:** the box `underflow_saturate` declines to empty is crisp-unsat but
*delta*-sat (the true solution is within δ), so this is a delta-completeness imprecision, not a
crisp soundness bug. It can also stop legitimate pruning of robustly-unsat subnormal
infeasibility — observed once as `tacas_c2e2_0hz_k7_..._inverter_sigmoid_UNS` flipping
UNSAT→delta-sat. Acceptable by design: dReal is sound but only delta-complete, so a false
`unsat` is catastrophic while an over-permissive delta-sat is allowed. (Do not re-investigate
that flip.)

**Alternatives tried and rejected:** keeping the underflowed fold symbolic instead of a
`RealConstant` interval — aborted the Debug build via the all-`Constant` AST invariant.

**Code:** `underflow_saturate` in `../ibex-fork` (patch #12); `sound_constant_fold` in
`symbolic_expression.cc`. Tests: `ibex_log_pow_edge_cases_test.cc`,
`denorm_constant_fold_test.cc`, `gaol_directed_rounding_false_unsat_test.cc::DenormUnderflowEndToEnd`,
`denorm_underflow_smt2_test.cc`.

---

## Backward narrowing integrates −f(x), it does not swap gates

**Decision:** To narrow `X_0`, the BWD contractor integrates the genuine backward dynamics
`−f(x)` from `X_t`; it does not run the forward contractor on swapped endpoint gates.

**Why:** For a non-time-symmetric ODE the set of points in `X_t` whose *forward* trajectory
lands in `X_0` is not the backward image of `X_t` under `f`, which is what soundness for the
integral constraint demands. Narrowing `X_0` off the swapped-gate forward question can remove
valid endpoint values → false `unsat`. Every concrete `x_0 ∈ X_0` whose forward trajectory hits
`X_t` lies in the `−f(x)` backward image of `X_t`, so removing states outside that image is
sound.

**Code:** `run_capd_bwd` (`src/dreal/contractor/odes/contractor_odes_capd.cc`); FWD/BWD
direction handling in `qf_nra_ode_semantics.md` §4.5.

---

## Negated / unlinked ODE constraints: negations dropped in-loop, unlinked forall_t rejected globally

**Decision:** A negated `integral`/`forall_t` literal is silently dropped in
`link_integral_invariants` — *not* turned into a loud error. The silent drop of negated ODE
atoms is the documented §6 behavior and the root of **BUG-002** (a user-asserted negation is
silently removed). A positive `forall_t` that links to no integral anywhere in the problem
(same-flow + invariant-over-endpoint-vars test) is **rejected with a throw at check-sat time**
(`RejectUnlinkedForallT`, `context_impl.cc`, 2026-07-13; it previously fell through the same
silent drop — surfaced as simulink-to-dreal **BUG-010**, an invariant written over the flow
var `x` instead of `x_t`). The link predicate is shared with the linker:
`forallt_links_to_integral` (`contractor_odes.h`).

**Why the split:** `link_integral_invariants` runs inside the DPLL(T) loop on the SAT solver's
*transient* literal subset, where a negated ODE literal or an unlinked positive `forall_t` is a
normal product of search — so a throw there crashes valid multi-step BMC benchmarks (github
`airplane`/`gen`). Distinguishing malformed *user input* from a valid transient state needs the
global problem scope — hence the rejection lives in `Context::Impl::CheckSat`, which sees the
full assertion stack (a `forall_t` nested under a disjunction/negation is not collected there
and keeps the silent-drop behavior). These drops are **COMPLETENESS** hazards (missed refutation
/ false `delta-sat`), never soundness (a removed constraint only enlarges the box). Full
transient-literal mechanism: `docs/ode-integration.md` §"Constraint forms accepted, and the
silent drops (BUG-002)".

**Alternatives tried/rejected:** throwing in `link_integral_invariants` on (a) negated ODE atoms
and (b) unlinked positive `forall_t` — both reverted after crashing legitimate benchmarks.

**Desired future semantics + roadmap:** genuine `∃t ¬φ` (negated `forall_t`); disequality vs.
definitional binding (negated `integral`) — specified as aspirational `GTEST_SKIP` tests in
`test/dreal/smt2/test/dreal_future.cc` (the unlinked-forall_t rejection tests there are now
live: `PosForallT_NoIntegral_ShouldReject`, `PosForallT_InvariantOverFlowVar_ShouldReject`). Full
mechanism: `docs/ode-integration.md` §"Constraint forms accepted, and the silent drops (BUG-002)".

## ODE formula evaluator: δ-tight witnesses are flag-gated (`--refine-witness`, default off)

**Decision:** `OdeFormulaEvaluator::operator()` (`src/dreal/solver/odes/ode_formula_evaluator.cc`)
keeps the historical fast accept by default — every ODE atom reports `VALID/[0,0]`, so ICP
accepts delta-sat at tube granularity with no ODE-driven branching. Under
`--refine-witness`, a **positive** ODE atom (`integral`/`forall_t`) instead reports
`UNKNOWN` with evaluation interval `[0, w]`, `w` = the widest of the atom's variables in the
box — so `EvaluateBox` keeps those variables branching until every one is below δ, each split
re-entering the tube contractor. A **negated** ODE literal (normal DPLL(T) product; unenforced
by design, see the entry above) stays `VALID/[0,0]` in both regimes — branching it would
enforce nothing.

**Why the honest mode exists:** the evaluator had been a stub (`TODO: IMPLEMENT CAPD STUFF
HERE`) returning `VALID/[0,0]` unconditionally since the TRI-era port. Any box that survived
tube pruning was declared "exactly satisfied": ICP exited delta-sat with **zero branching**,
freezing the box at tube-hull granularity, and the `Tighten` model post-pass presented each
dimension's **hull midpoint ± δ/2** as the witness. Any *un-pinned* ODE dimension therefore got
a fabricated witness — surfaced as simulink-to-dreal **BUG-011**: `dx/dt = 1`, `x(0) = 0`,
endpoint pinned `x(τ) = 0.38` ⇒ τ = 0.38 uniquely, yet `--model` reported τ ≈ [0.437, 0.438]
(the hull midpoint; hull-grid-dependent: grid 4 → 0.4375, 32 → 0.378, 512 → ∋ 0.38), a box the
solver itself **refutes** when asserted a priori. The stub semantics also weaken the
δ-contract: delta-sat is emitted for boxes where φ^δ was never established at δ granularity —
**COMPLETENESS** hazard (asserts φ^δ T-satisfiable without establishing it — missed
refutation), never false-`unsat` (the evaluator refutes nothing; `set_empty` stays with the
sound tube contractor).

**Why it is not the default — measured, then owner-decided (2026-07-13):** the fast accept is
load-bearing for deep-BMC SAT. At accept time the box is *mostly unrefined* — on
`github water k32` (0.4 s solve), **626 of 703 dims were wider than δ** (median width 5, many
at full declared range): the tube only pins what the constraint chain constrains, and SAT
reachability instances leave per-step dwell times and states legitimately wide. δ-refining them
all turns a 0-branch accept into a 10³–10⁴-bisection descent whose every step re-enters CAPD
(on `gen-1`: 0 → 7,367 branches, 223k → 9.4M contractor prunes, 2.4 s → 65 s). Full-corpus A/B
(119 ODE jobs, `results/ab_20260713_132033`): **github PAR2 4.89×** with 18 SAT→TIM,
**saradc 1.99×**, tacas 1.05×, zero SAT↔UNSAT flips. Not an implementation artifact — the cost
is the refinement work itself. Options weighed: adopt globally (rejected by the numbers),
revert + document (loses the honest mode), flag-gate (chosen; user decision after escalation).

**Model reporting: the default `--model` is the raw terminating box (2026-07-13, user
decision — supersedes the same-day ODE-dims-only `Tighten` exemption):** the reported box,
re-asserted as bounds over the same constraints, must stay delta-sat (idempotence). Two
escalating fixes landed the same day. First, `Tighten`'s always-on midpoint±δ/2 shrink was
found to be **fabrication for ODE-atom dims** — the tube certificate is not
inclusion-monotone, so the BUG-011 slice τ = [0.437, 0.438] excluded the sole solution 0.38
and re-fed → `unsat` — and ODE dims were exempted. Then the user rejected the midpoint shrink
*wholesale*: even where sound (dims outside ODE atoms — EvaluateBox's interval-evaluation
certificate is inclusion-monotone, every sub-box inherits it; the boundary is the dim's
certificate class, not the logic — the shrink of non-ODE dims is equally sound inside a
QF_NRA_ODE query), it destroys the certified-region information to manufacture a point-like
witness, a presentation choice that belongs downstream. `--model` now reports the terminating box **verbatim** with zero default
post-processing (τ : [0.375, 0.5] ∋ 0.38). This includes don't-care Booleans — a Boolean the
SAT layer never assigned means both values satisfy, and all three renderings (`Box`
`operator<<`, `PrintModel`, `get-value` in `smt2/driver.cc`) print it as its whole
`[false, true]` interval, symmetric with wide numeric dims. `Tighten` survives solely as the
`--refine-witness` post-pass: don't-care Boolean/binary dims pin to true and
continuous/integer dims shrink to midpoint±δ/2, still exempting ODE-atom dims (structural
idempotence; under the flag the honest evaluator has already branched them below δ).

**Regression guards** (`test/dreal/smt2/test/dreal_bugs_regression_test.cc`, all fail-first
verified): `Bug011_FreeEndpointTauWitness_Accurate` (flag ON: ODE witness contains 0.38 AND is
δ-tight), `Bug011_DefaultModelIdempotent` (default: witness contains 0.38, full reported box
re-fed as bounds stays delta-sat), `ModelDefault_RawTerminatingBox` (default: a vacuously-wide
NRA dim reports its whole certified interval, not a slice), `RefineWitness_TightensNraDims`
(flag ON: same dim reports midpoint±δ/2), `ModelDefault_DontCareBooleanWholeInterval` (default:
an unassigned Boolean renders `[false, true]` in box/`get-model`/`get-value`),
`RefineWitness_PinsDontCareBoolean` (flag ON: it pins to a definite value).

## Branching split-ratio 0.56 is a symmetry-break, not magic; order has no robust winner

**Context:** the `--split-ratio 0.56` + "alternating" traversal default gave a huge speedup on
symmetric Lyapunov SAT instances (e.g. `tanh_decrease__J1.0` 22.8M → **69** branch nodes) that
nobody could rationalize — a load-bearing decision that felt like a liability.

**What it is:** these problems have an off-center feasible region and a **critical point**
(`∇=0`) at the symmetric origin, so the constraint is *flat* near the center where the search
dwells — the contractor can't prune, and bisection must subdivide many dimensions. The 0.56
off-center cut is a **gradient-free symmetry-break** that escapes this flat basin; it is
load-bearing (`--split-ratio 0.5` regresses both transformative benchmarks under any traversal).
So 0.56 is principled, not a magic number — keep it.

**What was tried and rejected:**
- **Feasibility-guided ordering** (dive toward the less-violating child center): *refuted* —
  gradient-following has no gradient at the symmetric center, and definitional equalities swamp
  the point-score. (Implemented as `kFeasibilityGuided`, then reverted.)
- **Depth-parity** replacing the legacy global-branch-count toggle (more intelligible,
  path-independent): zero flips, but perf-neutral-to-slightly-worse at the default (J0.6 +30%
  nodes). *Not adopted* — kept the proven global-toggle; no code change.

**Conclusion:** branching **order** is a high-variance lever with no robust deterministic winner;
the magic was a lucky lottery ticket (`J0.6` is still 24.7M nodes), confirming the smell. The real
robust fix for off-center-solution-in-flat-landscape is **nlopt local-search seeding**
(`docs/seeding.md`), not a branching tweak. Full instrumentation, tables, and
refutations: `benchmark/optsearch/SEARCH_LOG.md` §"Why branching matters here, and why no order is
robust".

**Superseded (2026-06):** that nlopt seeding was built (`--seed-local`, next ADR) and adopted as
the default, so the 0.56 magic, the `--explore-order` alternate/larger/smaller-first machinery, and
the per-level alternation toggle were all **removed** — the split ratio returned to 0.5 (midpoint)
and the sequential exploration order is now a single fixed per-solve choice. This "keep 0.56" snapshot
records the reasoning at the time; the seeding ADR below is the resolution. **(Later correction:
per-branch alternation was *restored* in 2026-06 — it is load-bearing for ODE-BMC, which seeding does
not cover; only the 0.56 ratio and `--explore-order` flag stay gone. See the final ADR, §"Per-branch
alternation re-instated".)**

## Seed-and-verify (`--seed-local`): guided nlopt seeding beats the 0.56 branching magic

**Context:** the branching ADR above concluded the robust fix for "find an off-center solution
where the constraint is flat (∇=0) at the symmetric center" is to find that point *directly*. Built
as `--seed-local` (`src/dreal/solver/seed/seed.{h,cc}`, hooked in `IcpSeq::CheckSat`), default off.

**What it is:** a speculative, COMPLETENESS-only pre-pass gated to pure-relational (NRA) theory
calls (`AllRelational` — skips `forall`/ODE). It **proposes** candidate points (LHS sampling or
multi-start COBYLA), pins a small SOUND box around each (`make_sound_interval` ∩ root box), and
pushes them onto the ICP stack to be explored first; the **unchanged** prune+`EvaluateBox` loop is
the sole arbiter. Soundness/completeness are free (cache/recompute carve-out shape, not a fallback):
a bad candidate cannot cause a false delta-sat and the root box stays on the stack. *(COMPLETENESS-
only — asserting φ T-sat is gated by the real verifier, never by the proposer.)*

**Result (odeexpr A/B, 50 jobs, honest PAR2 over 40 ever-solved; zero SAT↔UNSAT flips everywhere):**
`nlopt050_64` solves **39** (PAR2 **17.3**) vs the `base056` 0.56 magic's **37** (PAR2 51.0) — it
cracks two `xwin` off-center SAT instances the magic *times out* on, 3× lower PAR2, and **no
overhead regressions**. So it removes the need for the 0.56 magic and beats it at its own job.

**Why guided (nlopt) beats blind (LHS):** LHS is gradient-free and flat-center-immune (vindicating
the "why not just sampling?" intuition) and cracks both transformative instances, but is caught in
a coverage-vs-overhead dilemma — small budget misses the tiny J0.6-class region (f ≈ 1e-4), large
budget (64k samples) cracks it but its pushed boxes regress easy instances to TIM. Guided COBYLA
needs ~1000× fewer candidates on tiny regions (64 vs 64 000 on J0.6), so it gets coverage *and* low
overhead. nlopt required three structural fixes to work on these CSE-heavy boxes (unbounded-dim
sub-box, CSE-equality substitution, NNF objective) — see SEARCH_LOG.

**Flat-center caveat (the original risk, confirmed):** a single COBYLA start from the box center
*does* stall at ∇=0 (`nlopt-1` TIMs on J1.0); ≥2 multi-starts dodge it via off-center LHS starts.

**Status: adopted as the default (2026-06, owner decision).** Default is `--seed-samples 64`
(multi-start COBYLA); `--seed-samples` is also the on/off switch (`0` disables). The superseded
branching machinery (0.56 default, `--explore-order`, per-level alternation) was removed in the same
change (per-branch **alternation** was later restored for ODE-BMC — see the final ADR; only the 0.56
ratio and `--explore-order` flag stay gone); a later consolidation also dropped `--split-ratio` (hardcoded 0.5), `--seed-method` (nlopt
is the sole proposer; the standalone LHS path was removed — LHS survives only as nlopt's multi-start
generator), and the separate `--seed-local` toggle (folded into `--seed-samples > 0`). Gated off for ODE/forall — verified it fires 0 times on github/tacas/saradc
representatives, so the ODE families are unaffected. Soundness proven by a test→RED→fix→GREEN bypass
test (`test/dreal/solver/seed/test/seed_test.cc`, since expanded into an adversarial unit
suite — see `docs/seeding.md`). Full investigation: `benchmark/optsearch/SEARCH_LOG.md`
§"Seed-and-verify". A follow-up 2×2 confirmed seeding is **orthogonal to**
`DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS` (same +4 in both settings; the under-constrained-model
path does not let seeding leak onto the ODE families; FULL=false is a net regression on its own,
helping UNSAT but hurting SAT) — SEARCH_LOG §"Interaction with …FULL_CONSTRAINTS".

## Per-branch alternation re-instated — load-bearing for ODE-BMC; seeding doesn't reach it

**Supersedes the two "alternation removed" notes above.** The seed-and-verify rollout de-alternated
the **sequential** ICP path (`IcpSeq::CheckSat`), leaving `explore_left_first` a fixed per-solve
choice on the theory that branch order is a high-variance non-lever. But that A/B ran on **odeexpr —
the only NRA family**, exactly where seed-and-verify fires; seed-and-verify is **gated off for
ODE/forall** (skips non-`AllRelational` calls), so on the ODE-BMC families it replaced *nothing*. The
`run_me.smt2` / bouncing-ball investigation (2026-06) found the de-alternated sequential path
regresses deep ODE-BMC SAT instances catastrophically — **<1s with alternation, >60s without
(~70×)** — so per-branch alternation was **restored** in `IcpSeq::CheckSat`. (The parallel path
`IcpParallel` never lost it — it has alternated since the base commit, so the removal only ever
de-synced the two paths.)

**Why it works (intuition).** ICP search is depth-first over a binary branch tree, and DFS commits
fully to the first-explored child's whole subtree before backtracking. A **fixed** first-side
therefore hugs one wall of the tree; if the witness lies toward the other wall — and BMC unrollings
are *deep*, so a root-level subtree is astronomically large — the search exhaustively grinds a
witness-free region before it is ever "allowed" to drift across. Alternating the first side
**zig-zags** down the tree, reaching diverse *deep* leaves quickly. On a SAT instance you stop at the
first feasible box, so reaching a witness sooner is the whole game. Both orders explore the same tree
to the same verdict in the limit; alternation only changes leaf **visit order** — so it can never
affect soundness or the UNSAT verdict, only time-to-first-witness on SAT.

**Is it a "magic number"? No — and that is why it is the robust default.** Unlike the removed `0.56`
split-ratio (a genuinely tuned constant, a "lucky lottery ticket"), alternation is a
**parameter-free parity flip** — there is no constant to fit or overfit. Its robustness is
structural: a fixed order has an adversarial worst case (witness maximally far from the bias → grind
everything first), while alternation has *no bias for an instance to be adversarial against*, so its
worst case is strictly better. The more "principled" alternatives were considered and do not win
here: **depth-parity** toggle — tried, perf-neutral-to-worse, not adopted (branching ADR above);
**best-first** search — trades the ordering question for unbounded memory (DFS is memory-bounded);
**informed first-side** (steer toward a sampled candidate) — this is precisely what seed-and-verify
already does for NRA, and blind alternation is the residual robust default for the ODE/forall
families it cannot reach. Code + intuition: `src/dreal/solver/icp_parallel.cc` (the `stack_left_box_first`
alternation comment in `Worker`).

---

## SMT-LIB push/pop: formally unsupported

**Decision (2026-07-22):** `(push N)` / `(pop N)` — and `Context::Push/Pop` — are formally
outside the supported fragment. Both throw one documented rejection *before any state
mutation* (`ThrowPushPopUnsupported`, `src/dreal/solver/context.cc`); the parser still parses
the commands so the error carries source context. Incrementality is an **encoder-side
concern**: emit one self-contained script per query. Monotonic incremental use within one
process (assert → check-sat → assert → check-sat, no retraction) remains supported —
CaDiCaL is incremental; only retraction is out. `(reset)` / `(reset-assertions)` remain
unparsed. The dead plumbing was deleted (`SatSolver::Push/Pop` — throwing stubs since the
CaDiCaL swap `299785e2f` — `Context::Impl::Push/Pop`, the unused `PushCommand`/`PopCommand`
cells). Historical fixtures `test/dreal/test/smt2/push_pop_01..05.smt2` stay in-tree as
pre-CaDiCaL artifacts. Tests: `test/dreal/smt2/test/push_pop_unsupported_test.cc`.

**Why:** CaDiCaL (3.0.x — verified against the installed header) cannot retract clauses: no
push/pop, only per-solve `assume()`, one-shot `constrain()`, `freeze`/`melt`. PicoSAT (the
pre-2025 core) had native `picosat_push/pop`. Deeper: learned theory lemmas are
**box-relative** — `AddLearnedClauseDirect` adds `¬l₁ ∨ … ∨ ¬lₙ` with *no box premises
encoded* (`unfitted_box` is audit-only), and `FilterAssertion` folds bound atoms into the
box rather than the literal space — so a pop that widens the box invalidates lemmas learned
under the narrower box. Naive retention is SOUNDNESS (asserts φ T-unsatisfiable on a
T-satisfiable φ — false unsat). Every sound design either drops learned lemmas on pop
(unacceptable for saradc-class incremental workloads) or adds soundness-critical machinery;
with no concrete incremental consumer, the clean rejection is the minimal-risk choice.

**For a future re-attempt** (investigated 2026-07-21/22; ranked):
1. *Rebuild-on-pop*: the scoped stacks still exist (`Impl::stack_`/`boxes_` ScopedVectors;
   SatSolver's ScopedUnorderedMaps). Pop = pop scopes, destroy+reconstruct `SatSolver`
   (`optional::emplace` — nothing survives by accident), replay `AddFormula` over the
   surviving `stack_` (formulas are stored post-ITE/post-normalization — replay is a pure
   loop). Drops SAT-learned clauses on pop: COMPLETENESS/speed, never SOUNDNESS. The rebuild
   must be **unconditional** — a scope can narrow the box via `FilterAssertion` while adding
   nothing to `stack_`.
2. *+ certified lemma replay*: log each learned clause's literals + the check-time box
   restricted to its free variables; on rebuild re-add iff every lemma variable is in the
   current box and current interval ⊆ premise interval (inclusion monotonicity — machine-
   checkable per lemma). Total retention whenever bounds stay at level 0 (encoder-controllable).
3. *Activation literals*: theory lemmas would need act-tagging anyway (box-relative), giving
   coarser retention than the certificate on exactly the expensive clauses, and the
   assume/flip/model-extraction surface joins the soundness argument.

Hazards a re-attempt must handle: `ExtractModel`'s size-equality fast path
(`context_impl.cc`, `model_variables_.size() == box.size()`) leaks aux variables once
declarations can be retracted; the SMT2 driver's `scope_`/`function_definition_map_`/
`ode_definition_map_` are not tied to assertion levels (popped names would stay resolvable);
Box name-lookups throw on a miss (see `simulink-to-dreal_bug_reports.md` BUG-012 —
pre-hardening they silently default-inserted into the shared index map, which a pop-era
pattern trie could weaponize into learned clauses over phantom variables).
