# CAPD leverage audit — opportunities for dReal

Synthesized from a five-cluster crawl of CAPD's Doxygen docs (2026-06), each
finding cross-referenced against `dreal-capd-usage.md` (what dReal already uses)
and confirmed in the installed headers under
`gcc_build/capd-install/include/capd/`. Confidence tags: **doc** (in the Doxygen
text), **header** (signature confirmed in the real header), **spec** (speculative
/ unverified). Soundness framing: a looser or skipped enclosure is a
**COMPLETENESS** issue (missed refutation / false `delta-sat`), never SOUNDNESS —
so every *tightness* lever below is a completeness/speed lever; none risks a false
`unsat`. The two genuinely soundness-adjacent items are called out as such.

> **See also — alternative/complementary ODE backends:** levers that would *replace or
> augment* the CAPD Lohner path itself (DynIbex validated-RK, Flow* / Ariadne Taylor-model
> integrators) are surveyed, with feasibility sketches, in
> [`../ibex_docs/RELATED-WORK.md`](../ibex_docs/RELATED-WORK.md). This audit is about using
> the *current* CAPD backend better; that file is about swapping/adding one.

Priority key: **A** = high-value structural; **B** = cheap tuning (do first,
benchmark-gated); **C** = guards; **D** = conditional/speculative; **E** = negative
results (recorded so nobody re-chases them).

This audit has **two passes**: the *doc side* (tiers A–E below — what CAPD offers
vs. what dReal binds) and the *code side* (the Addendum — dReal's integration call
sites: A′/B6/B7). The table next consolidates and ranks every actionable item from
both; the detailed entries follow.

---

## Consolidated ranking (both passes)

Each item scored on the three axes you'd weigh before spending time. **Ease 1 =
trivial** (change a default / one call) … **5 = a project**. **Likelihood** = odds
it actually improves perf if tried (not magnitude). **Value** = magnitude if it
works. **Try # = the recommended order**, which front-loads cheap-and-informative
and defers the structural bet.

| Item | One line | Ease (1=easy) | Likely to help | Value | **Try #** |
|---|---|:--:|:--:|:--:|:--:|
| **B1** | `--ode-c0-set` default → `C0TripletonSet` (CAPD's own default; ≥ as tight) | **1** | med (tighter; speed cost to measure) | med | **1** |
| **B6** | thread-cache the `IOdeSolver`/`ITimeMap` (only the `IMap` is cached today) | 3 | med–high (pure speed; unmeasured) | med | **2** |
| **B2** | wire `C0HOTripletonSet` as an `--ode-c0-set` value (tightest C0, ~2× cost) | 2 | med (tightness; may be slow as default) | med | **3** |
| **B3** | step-control policy: `ILastTermsStepControl(2–3)` / `IEncFoundStepControl` | 2 | med (fewer abandoned steps) | low–med | **3** |
| **B4** | expose `minTimeStep` / `maxStep` as flags | 1 | low (niche) | low | **3** |
| **B5** | expose QR/reorg policy; `SelectiveQRWithPivoting` = same enclosure, cheaper | 3 | **high** (near-definitional speed win) | low–med | **4** |
| **B7** | reuse the slice range for the gate state when clip == sub (skip a dup `curve()`) | 1 | low (tiny, certain) | low | **5** |
| **C1** | `DoubleRounding::isWorking()` startup guard (NATIVE caveat, already mitigated) | 1 | n/a (safety, not perf) | n/a | **6** |
| **A / A′** | C1 variational: monodromy `∂φ/∂x₀` for X₀ narrowing — **may replace the BWD integration** | 5 | med (structural; A′ raises odds) | **high** | **7** |
| **D2** | affine `(x,C,r0)` IC seeding instead of axis-aligned `IVector` | 4 | med | med | **8** |
| **D1** | `DiffInclusion` for additive/separable parameter uncertainty | 5 | low–med (conditional) | med (niche) | **9** |

**Three quick reads of the same table:**
- *Easiest to try:* B1 = B4 = B7 = C1 (rank 1) → B2 = B3 → B5 = B6 → D2 → A, D1.
- *Most likely to actually help:* **B5** (cheaper, same enclosure) and **B6** (kills
  a per-call allocation) are the safest bets; **B1** likely helps solving but trades
  speed; **A′** is high-value but structurally uncertain.
- *Try first (recommended path):* **(1)** flip the `--ode-c0-set` default to
  tripleton and read `/benchmark` corpus-wide; **(2)** profile `Prune` to confirm
  solver construction is a real cost, then add the thread-local solver cache;
  **(3)** fold B2/B3/B4 into one OFAT sweep with B1; **(4)** expose the QR/reorg
  policy and test SelectiveQR; **(5–6)** B7 + the rounding guard opportunistically;
  **(7)** prototype the C1 path — measuring **FWD-C1-only vs. today's FWD-C0 +
  BWD-C0** (A′), since that decides whether C1 is a *cost* or a *saving*; **(8–9)**
  D2/D1 only if a correlation-heavy or parametric-uncertainty workload appears.

Caveat that applies to the whole table: only **B1's** premise (tripleton is CAPD's
default and ≥ as tight) and the **E**-tier negatives are doc/header-*proven*. Every
"likely to help" is a grounded **hypothesis** — none is benchmark-measured yet. The
ranking orders what to *try*, not what is known to work.

---

## A. Structural lever — C1 variational integration for backward X₀ narrowing

**Four of the five clusters converged on this independently** (solvers, dynsets,
diffalgebra, poincaré), which is the strongest signal in the audit.

dReal integrates **C0** (value-only) sets, so it never computes the sensitivity
(monodromy) matrix `V(t) = ∂φ_t/∂x₀`. With a **C1 set** + a C1 solver, CAPD
returns `V(t)`, which feeds an **interval-Newton / mean-value contraction of the
initial-condition box** — a qualitatively new narrowing that ibex HC4 on C0 slice
*values* cannot perform. This is the natural sharpening for the **backward
(X₀-narrowing) contractor** dReal already runs per ODE constraint.

- **API confirmed (header):** `capd::ITimeMap::operator()(ScalarType time,
  SetType& theSet, MatrixType& derivative)` (`poincare/TimeMap.h:112`) transports a
  set to a **given** time and returns the monodromy matrix — i.e. dReal can get
  `∂φ/∂x₀` *at the same pinned terminal time it already integrates to*. Lower-level:
  `BasicOdeSolver::operator()(v, MatrixType& o_resultDerivative)`
  (`dynsys/BasicOdeSolver.h:69-72`); C1 set types `C1DoubletonSet`/`C1Rect2Set`
  (`dynset/typedefs.h`). Matrix-inversion support for the Newton step:
  `krawczykInverse` (tighter than Gauss; `vectalg/Matrix_Interval.hpp` /
  `newton/Krawczyk.h` — confirm exact signature at call site).
- **dReal status:** unused (C0 only; grep of `src/dreal/contractor/odes/` for any
  C1/derivative path is empty).
- **Effort/risk:** **high effort, low soundness risk.** Needs (a) switching the
  flow to a C1 set type, (b) a new dReal-side contraction step consuming `V(t)`
  (today the ibex HC4 path consumes only C0 slice intervals), (c) handling
  `V` non-invertibility (skip the Newton step → always sound). Per-step cost rises
  ~O(dim²) with smaller steps. It is a **completeness** lever: a failed or skipped
  narrowing is always safe.
- **Validate:** prototype on the backward contractor for one ODE family; measure
  X₀-box width reduction and PAR2 on github/tacas/saradc; confirm zero SAT↔UNSAT
  flips. See `concepts/variational-equations.md`, `classes/solvers/C1OdeSolver.md`,
  `classes/sets/C1DoubletonSet.md`.

---

## B. Cheap tuning — low effort, benchmark-gated, no soundness risk

These are flag/default changes, each independently testable with `/benchmark`.

1. **Flip the `--ode-c0-set` default from doubleton to tripleton.** dReal defaults
   to `C0Rect2Set` (the *cheapest* set), but CAPD's own default is **`C0TripletonSet`**
   (`dynset/typedefs.h:50` `DefaultC0Set`). Tripleton's dual-frame
   `intersection(B·r, Q·q)` is provably ≥ as tight as doubleton, at modest extra
   cost. **doc+header.** Lowest-risk high-value action — benchmark a default flip.
   (`classes/sets/C0TripletonSet.md`.)
2. **Wire `C0HOTripletonSet` as an `--ode-c0-set` value.** It exists
   (`typedefs.h:48` `C0HOSet<C0TripletonSet>`) but is **not** wired — it stacks the
   higher-order time-remainder tightening on the tripleton frame = the tightest
   available C0 set. ~2× integration cost; best when time-discretization dominates
   tube width. **header.** (`classes/sets/C0HOSet.md`.)
3. **Tune the step-control policy.** `IOdeSolver` runs `ILastTermsStepControl` with
   default `_terms = 1` (`dynsys/StepControl.h:283`); the header warns 1
   over-predicts near a vanishing last coefficient (the nonrigorous analog defaults
   to 2). Try `setStepControl(ILastTermsStepControl(2..3))` — fewer over-predicted →
   abandoned steps. Also consider `IEncFoundStepControl` (ties step size to
   enclosure-validation success, may cut the step-divergence throws dReal
   catch-and-skips on stiff flows). **header.** Pure speed/robustness, no soundness
   impact. (`classes/solvers/StepControl.md`.)
4. **Expose `minTimeStep` / `maxStep`.** `minTimeStep ≈ 9.54e-7` is a hard
   give-up floor; raising it ends hopeless near-singular flows faster, lowering it
   grinds harder. `setMaxStep` is used only when `--ode-max-step>0`. **header.**
   Trivial; no soundness risk. (`classes/solvers/OdeSolver.md`.)
5. **Expose the QR / reorganization policy.** All three wired sets hard-code
   `FactorReorganization<FullQRWithPivoting<>>` (`dynset/typedefs.h:24`). CAPD also
   offers `SelectiveQRWithPivoting` (orthogonalizes only near-parallel vectors →
   identical enclosure on well-conditioned frames, cheaper) and a configurable
   reorganization `factor` (`reorganization/FactorReorganization.h:26`; narrative
   says small initial sets benefit "a lot"). **doc+header.** A per-family speed
   lever. **Complete policy menu (4 QR × 7 reorg, all header-grounded):**
   [`classes/sets/QRPolicies.md`](classes/sets/QRPolicies.md). (Also
   `classes/sets/Rect2QRPolicy.md`, `ReorganizedSet.md`.)

---

## C. Guards (belt-and-suspenders; soundness-adjacent)

1. **`DoubleRounding::isWorking()` startup self-test** (`rounding/DoubleRounding.h:54`)
   — a one-call runtime check that FPU directed rounding actually takes effect.
   **Context (corrected from the crawl):** CAPD's docs *do* discourage the NATIVE
   interval backend ("GCC optimization of the native CAPD intervals … can produce
   not correct results", `example_intervals.html`) — **but dReal already mitigates
   this**: it builds CAPD with **`-frounding-math`** (verified:
   `capd_ep/src/capd_external/CMakeLists.txt` → `-O2 -frounding-math`; dReal's own
   `CMakeLists.txt` `target_compile_options(dreal4_cmake PUBLIC -frounding-math)`),
   which is exactly the flag CAPD says makes NATIVE rigorous
   ("without `-frounding-math` compiler can optimize code so that it is not
   rigorous", `user_programs.dox`). So this is **not a live soundness hole.** The
   `isWorking()` guard is still worth a startup call as cheap insurance that would
   catch a future build regression dropping the flag — fits the project's
   guard-all-assumptions discipline. **header.** Trivial effort. (`classes/Rounding.md`,
   `concepts/intervals-and-rounding.md`.)

---

## D. Conditional / speculative

1. **`DiffInclusion` for bounded parameter uncertainty** — models `ẋ ∈ F(x)` and
   bounds perturbation *accumulation* (less wrapping than treating an uncertain
   parameter as a frozen box per ICP call). **Caveat (header,
   `diffIncl/MultiMap.h:35-41`):** requires an *additive/separable* form
   `f(x,e)=f(x)+g(x,ε)` with `g(x,e0)=0`; nonlinear-in-parameter uncertainty needs a
   hand-built perturbation map per constraint. Needs a new solver type
   (`DiffInclusionLN`/`CW`) + `InclRect2Set`. **No benchmark exists** comparing it
   to dReal's frozen-parameter-box — the decision-blocking unknown. Medium-high
   effort. (`concepts/diff-inclusions.md`, `classes/integration/DiffInclusion.md`.)
2. **Affine `(x, C, r0)` set seeding** — seed the CAPD set from the box's
   correlation structure instead of an axis-aligned `IVector`, to avoid
   re-wrapping each ICP call. **doc.** Medium effort; a completeness lever. Likely
   subsumed by / complementary to the C1 work (A). (`classes/solvers/OdeSolver.md`.)

---

## E. Negative results — investigated, do NOT pursue

- **PoincareMap / sections do not fit pinned-time gating.** Sections are state
  functions `α(x)=0` with **no time argument** (`poincare/AbstractSection.h:69`
  `operator()(const VectorType&)`), and PoincareMap *solves for* an unknown
  crossing time — the inverse of dReal's "time is known, find φ(t)". dReal already
  holds the right tool, `ITimeMap`. (A Doxygen docstring on the Section classes
  copy-pastes "…Poincare section…" misleadingly; the signatures are ground truth.)
  **header.** (`concepts/poincare-maps.md`.)
- **The C0 value range is already maximally tight.** `Curve::operator()(interval)`
  is itself a space-centered mean-value form (`phi + jacPhi·deltaX` ∩ Horner + rem,
  `diffAlgebra/Curve.hpp:59-99`) — there is no looser "naive" C0 mode left to
  improve. Further C0-side range tweaking (incl. the 2026-06 time-centered fix's
  cousins: `valueAtCenter`/`remainder`/`getCenter`) won't help; the remaining gains
  are structural (A). **header.**
- **Jet / Hessian (C2/Cn) range forms** — tighten IC-spread *nonlinearity*, but
  dReal's wrapping is time-correlation-dominated (already addressed by the 2026-06
  fix), not IC-nonlinearity-dominated. High effort, low payoff. **doc.**
- **`IMap` C-routine construction vs string parse** — docs state evaluation
  performance is *identical*; it only skips the text parse at construction, which
  `CapdOdeCache` already amortizes once per flow. No per-call win. **doc.**
- **CAPD threading** — data-parallel over *independent* integrations, not
  intra-step; an ICP-call integration is sequential and gets no speedup, and it
  would contend with dReal's ICP-level worker pool. **header.**
- **Multiprecision intervals (`MpInterval`/`mpcapdlib`)** — inapplicable to dReal's
  `CAPD_INTERVAL_TYPE=NATIVE` (double-by-design) build; only warranted if double
  precision (1e-10 tol) ever proves limiting. **doc.**

---

## Recommended sequence

See the **[Consolidated ranking](#consolidated-ranking-both-passes)** table at the
top — it ranks every item from both passes by ease / likelihood / value and gives
the recommended try-order (1 → 9). The detailed entries above (A–E) and the code-side
Addendum below supply the grounding for each row.

---

## Addendum — code-side audit (dReal integration call sites, 2026-06 2nd pass)

The tiers above came from the *doc* side (what CAPD offers vs. what dReal binds).
This addendum is the *code* side: reading dReal's CAPD integration
(`contractor_odes_capd.cc`, `contractor_odes.cc`, `theory_solver.cc`) for call
sites that leave CAPD capability unused. Two new findings + one minor; the rest of
the surface confirmed already-tight.

### A′ (sharpens A) — C1 monodromy could *replace* the second integration, not just add tightness

**The cost framing of A changes once you read the call graph.** `theory_solver.cc`
(the `ode_direction` queue) queues **two** contractors per ODE constraint — a FWD
(`ode_direction::FWD`) and, when `--ode-backward` is set (default true), a BWD
(`ode_direction::BWD`) — each performing a **full, independent CAPD integration** when
pruned (`contractor_odes.cc` → `run_capd_fwd`/`run_capd_bwd`). The BWD contractor's
**sole job is X₀ narrowing**: the per-slice invariant check is FWD-only
(`contractor_odes.cc`, `check_inv = m_need_to_check_inv && m_dir == ode_direction::FWD`),
so BWD only intersects backward-image slices with the X₀ gate. That is *exactly* what the forward pass's
monodromy `V(t)=∂φ_t/∂x₀` (`ITimeMap::operator()(time,set,derivative)`,
`poincare/TimeMap.h:112`) delivers via an interval-Newton/mean-value step.
**Implication:** A is not necessarily "+O(dim²) cost for tightness" — on constraints
where both directions run, a C1 forward pass that narrows X₀ directly could **retire
the separate backward integration**, making the structural change potentially
*net-neutral or faster*, not just tighter. This materially improves A's cost/benefit
and is the first thing to measure when prototyping A: FWD-C1-only vs. today's
FWD-C0 + BWD-C0.

### B6 (new, cheap) — cache the `IOdeSolver`/`ITimeMap` per thread, not just the `IMap`

dReal already `thread_local`-caches the parameter-bound `IMap`
(`contractor_odes_capd.cc:230`, `thread_local std::unordered_map<const IMap*, IMap> tls`),
but constructs a **fresh `capd::IOdeSolver` + `capd::ITimeMap` on every `Prune`**
(`contractor_odes_capd.cc:433`, and the trace twin at `:626`). Each solver
construction allocates the order×dim Taylor-coefficient buffers. The code comment
(`:65`) *asserts* this is "negligible compared to the integration itself" — but that
is **unmeasured**, and it is least true exactly where ICP calls are most frequent:
short-horizon / few-step integrations, where buffer allocation isn't amortized over
many steps. A `thread_local` solver cache keyed by `(IMap*, order)` (mirroring the
existing IMap cache) would remove the per-`Prune` allocation. **header+code.** Pure
speed, low risk (the solver carries only mutable step state, reset per integrate).
Validate: profile `Prune` allocation on a short-horizon family (tacas inverters)
before/after. *This was not visible from the doc side — it is a usage-pattern lever.*

### B7 (new, minor) — redundant `curve` re-evaluation for the gate state

For each in-window slice the tube loop evaluates the centered range **twice**: once
for `state` and once for the window-clipped `gate_state`
(`contractor_odes_capd.cc` ~`:461` and `:480`, both `centered_curve_range(curve,…)`).
When a slice lies **fully** inside the terminal window the clip equals the sub, so
the second evaluation reproduces the first. A guard (`clip == sub → reuse state`)
skips the duplicate `curve()`/`timeDerivative()` calls on those slices. **code.**
Small, local, completeness-neutral; relevant only on the in-window portion of the
tube. (CAPD's `SolutionCurve` functional object — solvers cluster finding — is the
heavier alternative if the gate eval is ever restructured.)

### Confirmed already-tight (no action)

- **`IMap` parse** is correctly amortized (thread_local cache `:230`; per-flow
  build in `CapdOdeCache`) — matches the doc-side "no parse win" (E).
- **`setParameter` rebinding** per call (`with_params`) is the documented cheap path
  (Map cluster) — correct.
- **Rounding discipline** around every CAPD call (`NearestRoundingScope{expect_clobber}`,
  `run_capd_fwd/bwd`) is in place — the NATIVE caveat is mitigated (C1).
- **Exception → skip** (`integrate_tube_slices_impl` catch-all) is sound; tying step
  size to enclosure success (`IEncFoundStepControl`, B3) is the only refinement, and
  it's already listed.
