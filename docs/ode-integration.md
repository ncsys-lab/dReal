# ODE Integration

## Background

dReal supports QF_NRA_ODE: satisfiability over nonlinear arithmetic augmented with ordinary differential equations. The input describes a hybrid system: a sequence of modes, each with continuous dynamics `d/dt[x] = f(x, t)`, connected by transition guards and resets.

The solver must decide whether a sequence of states is reachable. This requires computing **guaranteed enclosures** of ODE trajectories — interval boxes that are certain to contain all solutions starting anywhere in the initial state box.

---

## AST Nodes for ODE Constraints

ODE-related AST extensions (in `src/dreal/symbolic/symbolic.h` and the Drake-derived expression layer):

- **`Integral`**: Represents integration of a continuous flow over a time interval. `Integral(flow, t_0, t_1)` says variables evolve according to `flow` from time `t_0` to `t_1`.
- **`ForallT`**: Universal quantification over time. `ForallT(φ, t_0, t_1)` means `φ` must hold for all `t ∈ [t_0, t_1]` along the trajectory.

Only the SMT2 parser builds them, from dReal3's SMT2 extensions (`define-ode`, `d/dt[x]`, `integral`, `forall_t`; §Input Formats). The `.dr` parser reads no ODEs.

### Constraint forms accepted, and the silent drops (BUG-002)

`link_integral_invariants` (`contractor_odes.h`) decides which ODE atoms become contractors:

- A **positive `integral`** becomes an ODE contractor.
- A **positive `forall_t`** is *linked* to an integral by **(a)** matching flow name and **(b)** `vars(invariant) ⊆ integral.vars_t` (the shared predicate `forallt_links_to_integral`). The invariant must therefore reference the integral's **endpoint** variables (`x_t`), **not** the bare flow-template variable (`x`) — `{x} ⊄ {x_t}`, so a `forall_t` phrased over `x` fails (b) and links to nothing (the per-slice tube checks the endpoint var against every slice; see *Mechanism* below). A `forall_t` that is unlinkable against the **whole problem** is rejected with a throw before solving (`RejectUnlinkedForallT`, `context_impl.cc`); inside the DPLL(T) loop the unlinked case remains a silent skip (see below).
- A **negated** `integral`/`forall_t` literal (under `not`) is **not enforced** inside the loop — the documented §6 QF_NRA_ODE semantics. Since 2026-10-05 a *user* formula that would produce one by construction is **rejected** at assert time (`RejectNonPositiveOde`, `context_impl.cc`; BUG-002): an ODE atom in negative or mixed polarity — under `not`, an `=>` antecedent, a Boolean `=`, `xor`, or an ite condition — or inside an NRA `forall` body. With every ODE atom positive-only, the in-loop drop of a negated literal is exact: the Boolean skeleton is monotone in such an atom, so an assignment that sets it false is also satisfied with it true. The unlinked-`forall_t` drop below remains a **COMPLETENESS** hazard (a dropped constraint only enlarges the box → missed refutation / false `delta-sat`, **never** a false `unsat`).

**Why these drops are not simply turned into errors in the loop.** `link_integral_invariants` runs *inside the DPLL(T) loop*, on the SAT solver's current literal subset — not on the user's source formula. There, a **negated** ODE literal is a normal product of search (the SAT solver assigns an ODE atom false), and a **positive** `forall_t` routinely appears **without** its companion integral (a different flow/step is active in that node). Throwing here therefore crashes legitimate multi-step BMC benchmarks (empirically: the github `airplane`/`gen` families). Telling genuinely-malformed/unsupported user input apart from a valid transient search state needs the **global** problem scope — which is exactly where the unlinked-`forall_t` rejection now lives: `RejectUnlinkedForallT` (`context_impl.cc`) runs once per check-sat against the full assertion stack and throws on a positive `forall_t` no asserted integral can accept. Its honest scope: a `forall_t` nested under a disjunction is not collected by the conjunction traversal and keeps the silent-drop behavior (one under a negation is rejected at assert time, above).

The **desired future semantics** — genuine `∃t ¬φ` for a negated `forall_t`; the two rival readings (disequality vs. definitional binding) for a negated `integral` — are specified as aspirational `GTEST_SKIP` tests in `test/dreal/smt2/test/dreal_future.cc` (the unlinked-`forall_t` rejection tests there are now live). Positive-form behavior is regression-guarded in `dreal_bugs_regression_test.cc`.

---

## The CAPD Integrator

**Files:** `src/dreal/contractor/odes/contractor_odes.cc` (contractor) and `contractor_odes_capd.cc` (CAPD backend)

The ODE contractor `contractor_ode_lohner` integrates flows with **CAPD**'s interval ODE solver — the sole ODE backend since the Codac elimination (see `docs/decisions.md` "ODE backend"). CAPD computes a **guaranteed** interval enclosure of the flow `dx/dt = f(x, t)`: a sequence of boxes certain to contain every trajectory starting anywhere in the initial box, with internal control of the **wrapping effect** (the exponential blow-up naive interval box arithmetic suffers because it cannot represent rotated or skewed sets).

### Mechanism — per-slice tube + filter

The contraction is split into a **numeric tube builder** (`contractor_odes_capd.cc`, CAPD-only) and a **box/invariant filter** (`contractor_ode_lohner::Prune`, `contractor_odes.cc`), so the CAPD layer stays purely numeric and all symbolic-box logic lives in one place. This restores the cav26 per-slice design that the Codac→CAPD rewrite had collapsed into a coarse single-endpoint intersection (an unsound over-narrowing — see *Soundness* below).

**1. Feed.** Each RHS expression is translated once into CAPD's `IMap` string format (`to_capd_string`, `to_capd_string.h`) and cached per flow in a `CapdOdeCache` — both the forward map `f(x)` and the negated map `-f(x)`. Flow **parameters** (a flow variable whose `d/dt` is the literal `0`) are emitted in the `IMap`'s `par:` section and bound per call via `setParameter` to their current box intervals — they are constant along the flow and are *not* integration variables (emitting them as variables overruns the `C0Rect2Set`'s buffers; see the `build_imap_strings` comment).

**2. Tube.** `run_capd_fwd` / `run_capd_bwd` integrate the cached `f(x)` / `-f(x)` from the start box `u0` over `[0, t_ub]` with `capd::IOdeSolver` at Taylor order `kCapdTaylorOrder = 20`, driven by `capd::ITimeMap` in `stopAfterStep` mode. Each adaptive step's rigorous Taylor curve is **sub-gridded into `kHullGrid = 16` sub-slices** (`integrate_tube_slices`), and the result is the **full time-ordered list of per-slice enclosures** `{(t_lb, t_ub), state-box}` — *not* a coarse component-wise hull. The per-slice form is what preserves the per-time / per-component correlation the filter needs; a hull would collapse it and under-refute (a false delta-sat). `IOdeSolver` instances are per call (mutable step state, not shareable across parallel ICP workers); the parsed `IMap` AD-tree is the cached, reused part.

**3. Filter** (`contractor_ode_lohner::Prune`). Walk the slices in forward-time order:
   - **Invariant** (FWD only): write each slice's state into the `ForallT` variables (a reused box copy) and run the HC4 invariant contractors. The *first* slice they empty is a trajectory-**interior** violation → every later terminal is unreachable, so stop.
   - **Terminal window**: a slice whose time overlaps the dwell window `[win_lb, win_ub]` is terminal-eligible. Intersect its state with the `X_t` gate (`m_vars_t` box) component-wise; keep the non-empty intersections.
   - **Narrow / refute**: hull the kept intersections → narrowed `X_t`, hull their times → narrowed time variable. **No surviving slice → `set_empty()`** — a sound refutation, because CAPD's enclosures are outward over-approximations, so an empty survivor set proves true infeasibility.

`run_capd_fwd` narrows `X_t` and the time variable; `run_capd_bwd` runs the symmetric filter in a swapped frame (`m_vars_0 = original X_t`) to narrow `X_0`. The theory solver queues **both** a FWD and a BWD contractor per ODE constraint (mirroring cav26's two-contractor design), so each endpoint is narrowed by its own pass — the FWD pass is also the one that enforces the `ForallT` invariant (full mechanism next).

### The `ForallT` invariant mechanism (CAPD tube × IBEX HC4)

> **⚠ PITFALL `forall-vs-forall_t`:** this is the ODE trajectory invariant (`forall_t` /
> `FormulaKind::ForallT`), *not* the ∃∀ NRA `forall` / `ContractorForall`. Canonical
> side-by-side: `docs/forall-semantics.md` §7.

Enforcing a `forall_t` invariant is a **two-engine** mechanism, and the reason this contractor "spans CAPD and IBEX": **CAPD** produces the rigorous per-slice trajectory tube (numeric-only, §Mechanism step 2), and **IBEX HC4** contractors — compiled from the invariant *body* — test the invariant on each CAPD slice. The two never share data structures; they meet only at the box.

**1. Construction: invariant body → IBEX contractors** (`contractor_ode_lohner` ctor, `contractor_odes.cc:145–162`). Each linked `ForallT` in `m_ctr.second` is compiled **once** (at contractor-build time, not per `Prune`) into an IBEX forward-backward (HC4) contractor over the box:
- a **conjunction** body — e.g. `(forall_t 1 [0 T] (and (<= x 5) (>= v -10)))` — becomes one `make_contractor_ibex_fwdbwd` *per conjunct*, wrapped in a `make_contractor_seq`, as one entry of `m_inv_ctcs`;
- any other (single-atom) body becomes a single `make_contractor_ibex_fwdbwd`.

`m_inv_ctcs[i]` corresponds **positionally** to `m_ctr.second[i]` (asserted at `:387`). The IBEX constraints are phrased over the invariant's variables, which — by the linking rule (BUG-002, see "Constraint forms accepted" above) — are the integral's **endpoint** variables `m_vars_t` (`x_t`). `m_need_to_check_inv` is set iff at least one invariant linked.

**2. Per-slice application: CAPD slice → IBEX prune** (`Prune`, `contractor_odes.cc:405–428`). Inside the per-slice filter, with `check_inv = m_need_to_check_inv && m_dir == FWD`, walk the CAPD slices in forward-time order. For each slice:
1. write the slice's **full-interior** enclosure `slice.state` into the `m_vars_t` components of a box copy `cs_inv` (the invariant must see the whole interior the terminal is reached through — `slice.state`, *not* the window-clipped `gate_state`);
2. run each `m_inv_ctcs[i].Prune(cs_inv)` — the IBEX HC4 contractor narrows `cs_inv` to the part of the slice enclosure consistent with that invariant atom;
3. if any contractor **empties** `cs_inv`, the slice enclosure is **wholly outside** the invariant region → the trajectory provably leaves it at an interior time → `break` (every later terminal is then unreachable).

Because the check runs on **every interior slice**, an invariant violated only at an interior peak (and satisfied again by the endpoint) is caught — exactly what a single-endpoint check misses.

**3. Why FWD-only** (`contractor_odes.cc:389–404`). The invariant constrains the *forward* trajectory `x(t)`. In the FWD contractor the slices **are** that trajectory and `m_vars_t = original X_t = the invariant's variables`, so writing a slice into `m_vars_t` and pruning tests the invariant at that trajectory time. The BWD slices are the backward image over `m_vars_t = original X_0` (not the invariant's variables), so a BWD invariant check would merely re-test the static `X_t` box — a no-op. The theory solver always queues a FWD contractor beside every BWD one, so FWD covers the invariant and dropping the BWD check is sound (an over-permissive BWD only under-narrows → completeness, never soundness).

**The reused box copy** (`:401–407`). For FWD the invariant touches only the `m_vars_t` components, overwritten each slice, so **one** hoisted `cs_inv` is behavior-identical to a fresh per-slice copy — without the `O(box)` allocation on every sub-slice that made invariant-heavy flows (the `k256` thermostat) time out.

**No negated invariants reach the per-slice check.** `link_integral_invariants` links only positive `forall_t` atoms, so `m_ctr.second` never holds a negation (the former `is_negation` skip there was dead and is gone; a user-level negated `forall_t` is rejected at assert time, BUG-002).

**Rounding.** The per-slice invariant prune runs under an `UpwardRoundingScope` (`inv_scope`, `:386`) — gaol/IBEX directed rounding requires `FE_UPWARD` — nested inside the CAPD `NearestRoundingScope`; the interval `&`/hull ops are mode-independent and ride along safely (`docs/rounding.md`).

**Soundness / completeness.** Emptying a slice is a **sound refutation**: CAPD's per-slice enclosures are outward over-approximations, so "disjoint from the invariant region" really does mean the true trajectory leaves it. A looser enclosure (lower Taylor order / `hull-grid`) only *widens* a slice → it may *miss* a violation → **COMPLETENESS** (missed refutation), never a false-`unsat`. The negated/unlinked-`forall_t` drops are likewise completeness, not soundness.

### Short-circuits and divergence

- **Trivial flow** (every RHS is the literal `0`, `capd_ode_cache_is_trivial`): bypass CAPD entirely and just intersect `X_0 ∩ X_t`.
- **Start set widened by one ulp** (`to_capd_interval`): every start-set interval handed to CAPD is widened outward with `std::nextafter` (flow parameters are not: CAPD evaluates `sqrt(p)` on a parameter directly, and widening `p = [0, c]` below 0 would make it throw). CAPD cannot integrate a thin start exactly on a nonzero fixed point of its flow — `predictNextEnclosure` seeds the remainder with an absolute 1e-300 while outward rounding widens the predicted enclosure by ulp(|x|), so every step fails "minimal time step reached" (BUG-016). The wider set contains the box, so the tube stays sound.
- **`T = 0`** (time upper bound pinned to `0`): a zero-duration trajectory means initial = final, so intersect `vars_0[i] ∩ vars_t[i]` directly. The integration time may be a variable, a `RealConstant` interval, or an exact constant — all three are handled.
- **Divergence**: ANY CAPD exception — step-control failure (stiff tube / over-approximation explosion) *or* a mid-enclosure singularity (`capd::IntervalError` "possible division by zero", e.g. the sigmoid-inverter flows) — and any value CAPD cannot represent (`require_capd_representable`: a non-finite or inverted bound or one beyond DBL_MAX/2 in the start set, a flow parameter, the integration end time, or a returned enclosure — BUG-015) is caught inside `integrate_tube_slices` and reported as `found == false` with the reason in `CapdTubeResult::failure`; the contractor narrows nothing for that `Prune` call (sound but incomplete; an approved fallback, `docs/decisions.md` §"ODE inconclusive skip", reported on stderr at a delta-sat verdict) **but must still record the diverged integral via `cs->AddInconclusiveOde(ic, reason)` before returning** — omitting that record dropped the logically-responsible ODE from the explanation and produced a false `unsat` (SOUNDNESS — asserts φ T-unsatisfiable on a T-satisfiable φ; fixed, see `docs/constraint-order-explanation-soundness.md`). The catch is **catch-all-and-skip with no rethrow**: this architecture has no ICP-level contractor catch, so an escaping exception would `terminate()` the whole solve. (An *untranslatable* RHS is different: it raises at cache-build time — a loud failure, not a silent skip.)

### Soundness: feed faithfulness and the per-slice filter

The integrator is sound only if **two** things hold, and both were soundness bugs that have been fixed:

1. **The vector field CAPD integrates must be faithful to the true RHS.** `to_capd_string` renders every constant at `std::numeric_limits<double>::max_digits10` (17) significant digits, so CAPD's interval-parse of the decimal literal brackets the exact double. The previous `std::to_string` rendered only **6** fractional digits (`sprintf %f`), so a coefficient like `1/3 → "0.333333"` made CAPD integrate `3·(1/3)` as `0.999999` — a vector field unfaithful by `1e-6`. A clock whose terminal gate sits at the integration-window end (`tau=1` at `t=t_ub`) then has no surviving terminal slice → **false-`unsat`** (the water/thermostat automata; `ode_soundness_repros/ws_taupin.smt2`). Decimal formatting is correctly rounded only in `FE_TONEAREST`, so `to_capd_string` asserts that mode (it is always reached under `make_capd_ode_cache`'s `NearestRoundingScope`); see `docs/rounding.md`.
2. **The filter must keep per-time/per-component correlation.** Intersecting a single coarse endpoint hull with `X_t` (the Codac→CAPD rewrite's form) combines `x` reached at one time with `p` reached at another → a false delta-sat on anti-correlated tubes, or a missed interior invariant violation. The per-slice filter above keeps the correlation. Regression coverage: `contractor_odes_semantic_test.cc` (`GravityInvariantTest`, `AntiCorrelatedTest`, `DecayFlowTest.*`) and `contractor_capd_test.cc` (`CapdFwd`/`CapdBwd` — the cumulative-gaussian witness `p=Φ(10)−Φ(-10)<1`).

---

## Trajectory Visualization

When `--visualize` is set, the ODE code generates trajectory enclosures via `run_capd_trace` (`contractor_odes_capd.cc`), driven by the contractor's `generate_trace`. It integrates the cached flow over `[0, t_ub]` and records an over-approximating box for each of `n_steps` equally-spaced sub-slices — the full tube of all trajectories, not just the contracted endpoint boxes. `forward = true` integrates `f(x)`; `forward = false` integrates `-f(x)` for a reverse-time view. If the integrator diverges before reaching `t_ub`, the slices recorded up to that point are still emitted (partial traces stay visualizable). The output is dumped as JSON for downstream plotting.

---

## Mode Sequence Encoding

A hybrid system with `k` modes is encoded as a formula over `k` copies of the state variables, one per mode. The solver searches for an assignment of initial/final states for each mode such that:
- The final state of mode `i` matches the initial state of mode `i+1` (after applying the reset map).
- The guard condition for the transition holds.
- For each mode, the ODE constraint `Integral(flow_i, 0, τ_i)` is consistent with the assigned initial and final states.

The benchmark `bouncing_ball_with_drag_10_0.smt2` is a 10-mode bouncing ball — each bounce is one mode. The solver must find a trajectory that satisfies all 10 modes simultaneously.

---

## Performance

CAPD became the sole ODE backend after benchmarking confirmed it was at or below the old Codac `CtcLohner` runtime on the tested ODE families (cardiac, prostate, bouncing-ball). The Taylor order is `kCapdTaylorOrder = 20`: the per-slice tube sub-grids `kHullGrid = 16` enclosures *per adaptive step*, so cost scales with the step count — a low order takes many small steps and the `16×` explodes (the k256 thermostat went 187 s → timeout at order 10, back to 196 s at order 20). Order 20 takes fewer, larger steps and its tighter per-step enclosure also localizes interior invariant violations better. Lowering order or loosening tolerance only *widens* a sound enclosure (never a false-`unsat`). Per-flow caching of the parsed `IMap` (built once, reused across every `Prune`) keeps steady-state integration off the expression-translation path.

See `docs/decisions.md` "ODE backend" for the CAPD-vs-Codac finding and `OPTIMIZATION_LOG.md` for the order-tuning and full optimization timeline.

**Witness fidelity (`--refine-witness`, default off).** ICP accepts an ODE delta-sat at tube granularity: `OdeFormulaEvaluator` reports ODE atoms satisfied as-is, so the accepted box legitimately keeps un-pinned ODE dimensions wide (on `github water k32`, 626 of 703 dims were wider than δ at accept). The default `--model` reports the **raw terminating box** — the exact region ICP certified, idempotent when re-fed as bounds (the formerly unconditional midpoint±δ/2 `Tighten` slice could exclude every real solution for an ODE dim — BUG-011: τ reported ≈[0.437, 0.438] when the only solution was 0.38, re-feed `unsat` — and even where sound it destroyed the certified-region information). `--refine-witness` buys δ-**tight** witnesses: continuous/integer dims are midpoint±δ/2-shrunk at report time (sound by inclusion monotonicity; ODE-atom dims stay exempt), and the evaluator reports each positive ODE atom's widest variable so ICP branches every ODE dimension below δ before accepting — at the measured cost of 4.89× github PAR2 (18 SAT→TIM) / 1.99× saradc corpus-wide. Enable it when wide `--model` intervals are not precise enough; verdicts are unaffected either way (zero A/B flips). Full record: `docs/decisions.md` §"ODE formula evaluator".

---

## CAPD build wiring

CAPD runs on ARM64 via `CAPD_INTERVAL_TYPE=NATIVE` (master SHA `03dc5628`), which uses CAPD's own `DoubleRounding` and skips FILIB entirely. See `DEPENDENCIES.md` § "CAPD" for the full build-wiring details. CAPD expects the FPU in `FE_TONEAREST`; `contractor_ode_lohner::Prune` establishes a nested `NearestRoundingScope` for the CAPD work (and a further nested `UpwardRoundingScope` around the ibex invariant sub-contractors it calls), and the `run_capd_*` / `make_capd_ode_cache` adapters open an `ExpectClobber` `NearestRoundingScope` to contain CAPD's directed-mode clobber (CAPD leaves the FPU in a directed mode on return rather than restoring nearest) — see `docs/rounding.md`. (The earlier Codac/CAPD gated hybrid, and the `--capd-t-gate` / `--capd-ndim-gate` flags that selected between them, were retired when Codac was removed.)

---

## Dependency history

The current CAPD-only design is the result of two migrations: an earlier move to a Codac-based ODE contractor, then the **removal** of Codac (Codac and Eigen are no longer dependencies). The current stack source-builds the IBEX fork (`ncsys-lab/ibex-lib@dreal-perf-patches`) and CAPD (`CAPDGroup/CAPD@03dc5628`); ODE boundary values cross the interface as `ibex::Interval` / `ibex::IntervalVector`. See `DEPENDENCIES.md` for the current stack and `docs/decisions.md` "ODE backend" for the migration rationale.

---

## Input Formats

ODEs reach dReal4 only through the SMT2 parser (`src/dreal/smt2/`), in dReal3's SMT2
extensions. From the corpus file `0hz_k2_bouncing_ball_bouncing_ball.drh.o.smt2`
(declarations and most of the assertion cut):

```smt2
(set-logic QF_NRA_ODE)
(declare-fun v_2_0 () Real [-100.000000, 100.000000])
(declare-fun v_2_t () Real [-100.000000, 100.000000])
(declare-fun time_2 () Real [0.000000, 10.000000])
(define-ode flow_2 ((= d/dt[v] -9.8000000000000007) (= d/dt[x] v)))
(assert (and (= [v_2_t x_2_t] (integral 0. time_2 [v_2_0 x_2_0] flow_2))
             (forall_t 2 [0 time_2] (>= x_2_t 0))))
```

- `(define-ode flow_N ((= d/dt[x] e) …))` names a flow; each `d/dt[x]` refers to a declared
  template variable `x`.
- `(= [x_t …] (integral t0 t1 [x_0 …] flow_N))` binds end states to start states, in the
  flow's variable order. `t0` must be the constant 0 (the contractor throws otherwise) and `t1`
  a variable or a constant, and every `x_0`/`x_t` a variable (the `FormulaIntegral` constructor
  throws otherwise).
- `(forall_t N [t0 t1] φ)` requires φ along the trajectory of `flow_N`, phrased over the end
  variables (§Constraint forms above).

dReal4 has no parser for dReal3's `.drh` hybrid-automaton format (`mode`, `flow`, `jump`,
`init`, `goal`). A hybrid model arrives already unrolled into SMT2 like the file above; the
benchmark corpora's `rolled/` directories hold such unrollings. The `.dr` parser
(`src/dreal/dr/`) reads dReal3's NRA format only (`var:`, `ctr:`, `cost:` sections).
