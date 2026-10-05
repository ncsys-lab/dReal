# `exists_forall` (odeexpr_v2) — ∃∀ tractability worklog

> **Worklog / speculation, not documentation.** Relocated from `docs/` on 2026-07-01
> because it is dated investigation notes, not a stable reference. The solver-mechanism and
> soundness content (pre-pruner, CE-domain fix, lemma-PM) is durable and cross-referenced from
> the real docs. The current empirical record is the **"Current state & empirical findings"** and
> **"Improvement opportunities"** sections below (2026-07-01 second regen, 25+25), with full sweep
> tables in `benchmark/SWEEP_odeexpr_v2.md`; the **June δ=0.0005 measurements** at the bottom are a
> superseded point-in-time snapshot — history, not the family on disk today.

---

## Current state & empirical findings (2026-07-01, second regen)

The `odeexpr_v2` family was **regenerated twice on 2026-07-01** (`source: full-cross-sample`; the
second regen fixed a benchmark bug). Current on-disk reality (verified), superseding both the June
δ=0.0005 body *and* the first-regen "50+50" correction:

| Earlier claim | Current reality (verified) |
|---|---|
| June: "`forall/` empty", "15 `exists_forall`", δ pinned 0.0005 | **25 `forall/` + 25 `exists_forall/`**, δ pinned **0.01** (`0.0100000000000000`) in every file |
| First regen: "50 `forall/` + 50 `exists_forall/`" | **25 + 25** (second regen halved the count) |
| "SAT-by-construction — zero MLP is an exact witness for the whole family" | Only `sign_agreement` admits the zero witness; the strict-margin files are genuine refutation targets |

**Zero-MLP witness depends on the comparator** (all-zero MLP makes every descent product `0`):
- **`sign_agreement`** — body `(>= <product> 0)` → `0 ≥ 0` holds → zero MLP **is** a witness →
  **delta-SAT** ("inconclusive" for the application).
- **`average_descends`** — body `(<= <product> −1e-9)` → `0 ≤ −1e-9` false → zero MLP rejected.
- **`both_descend`** — `(and (<= P₁ −1e-9) (<= P₂ −1e-9))` → rejected on both conjuncts.

So the `average_descends` + `both_descend` files are **candidate UNSAT / separation proofs**
(**UNSAT = "no witness c exists" = the valuable result**); `both_descend` (double strict conjunct)
is hardest. `∃∀` descent-test breakdown of the 25: `sign_agreement` 6, `average_descends` 9,
`both_descend` 10. Exclusion variants: `abs` `(>= (abs x_0) ε)` and `box`
`(or (>= x_0 ε) (<= x_0 -ε))`, ε ∈ {1/10, 1/100}; **CLI `--precision` overrides the baked
`set-option`** (`OptionValue` command-line ≻ file, `context_impl.cc`), so δ sweeps need no file
edits.

### The empirical verdict: no current lever proves any ∃∀ UNSAT

Full sweep tables + reproduction dirs: **`benchmark/SWEEP_odeexpr_v2.md`**. The durable findings:

- **∃∀ baseline (δ=0.01, 120 s CPU cap):** 25 files → **0 UNSAT**, 9 delta-sat (4 `sign_agreement`
  zero-witness + 5 strict-margin), 16 timeout. Target set = 16 timeouts + 5 strict delta-sats = 21.
- **17 configurations tested, all 0 ∃∀ UNSAT:** every `--precision` 0.005→1.0; `--forall-pre-prune`
  (±`-prec 0.1`); `--forall-polytope`; `--polytope`; `--acid`; `--3bcid`; `--local-optimization`;
  `--smear smearsum`; `--jobs 4` (±pre-prune); kitchen-sink (pre-prune+polytope+acid, ±finer δ).
  (**`--smear` caveat, 2026-07-03:** that sweep predates forall-body-aware smear — the old
  `SmearBrancher` skipped `forall`, so `--smear smearsum` was *inert* here, byte-identical to
  largest-first at the outer level (both solve 7/33). The now-forall-aware smear is COMPLETENESS-live
  — +2 delta-sat solves, `smearsum`≈`smearsumrel` best; `OPTIMIZATION_LOG.md` §"Forall-body-aware
  smear" — but it still yields **0 ∃∀ UNSAT**: branching reshapes search order, not enclosure
  tightness, so it cannot touch the wall below.)
  δ-coarser only buys more (inconclusive) delta-sat; δ-finer only adds timeouts. The contraction
  flags **engage but are insufficient**: they change per-file CPU (e.g. `--forall-polytope` halves
  one delta-sat's time and quadruples another's; `--forall-pre-prune` pushes an n2 delta-sat into
  timeout) yet leave **every hard file's verdict unchanged** — they contract the (J, ch) box, just
  never to empty. (An earlier note here said "byte-identical to baseline / do not contract" — wrong,
  inferred from the aggregate verdict-count; verified at set-level, no config cracked a timeout into
  anything but inconclusive delta-sat, and none produced UNSAT.)
- **The wall is algorithmic, not compute-bound.** `--jobs 4` (≈4× compute in-wall) cracked nothing
  and even matched fewer than single-thread. `--verbose debug` on the smallest hard case (n1
  `both_descend`, 4 vars, 20 s): **~1.2 M ICP loop heads, ~600 K branches, ~1 CEGIS refinement.**
  `Icp::EvaluateBox() Found an interval >= precision` fires ~600 K times — ICP repeatedly finds
  sub-boxes straddling the descent constraint that it **cannot prune**, so it branches a continuous
  (J, ch) box that never contracts to empty. Root cause: the descent body
  `poly(ch)·tanh(poly(J,ch,x))` has **interval enclosures too loose** (tanh/exp/pow widening + the
  ch/x **dependency problem**) → a feasible-looking sliver survives every branch. This is why more
  compute and the pre-pruner (same loose enclosure) do nothing.
- **abs vs box:** box terminates more often but **only into inconclusive delta-sat, never UNSAT**,
  and it is confounded (the generator paired box mostly with the wider ε=1/10). The 3 truly matched
  ∃∀ pairs are all TIM/TIM — no easier encoding to pivot to *for the UNSAT goal*. (On `forall/`,
  however, all UNSAT verdicts are box-form and all abs-form → delta-sat, so box is the
  better-behaved encoding to standardize on.)
- **`forall/` (secondary, near-solved):** baseline δ=0.01 → 3 UNSAT / 21 delta-sat / 1 timeout, and
  is the **best config** — coarser δ cracks the lone timeout only into inconclusive delta-sat *and
  loses one UNSAT* (a COMPLETENESS flip the guard correctly rejects).

All of this is **COMPLETENESS**, never SOUNDNESS: no config produced or could produce a false
`unsat` (a lever can only weaken refutation / fail to terminate, never delete a true model).

---

## Improvement opportunities (ranked, with implementation difficulty)

The wall is enclosure looseness on `poly(ch)·tanh(poly(J,ch,x))`, so the load-bearing directions
are tighter enclosures / dependency mitigation — **not** any runtime knob (all proven null above).
Difficulty assessed against the IBEX fork (`ncsys-lab/ibex-lib@dreal-perf-patches`) and dReal's
contractor path (`contractor_ibex_*`, `util/ibex_converter.cc`):

1. **Symbolic dependency mitigation (Horner / factoring / CSE / monotonicity) — LOW–MEDIUM;
   cheapest, try first.** dReal currently does **no** symbolic pre-simplification (no
   `Expand`/`Simplify`/`Horner`/`CSE` anywhere in `src/dreal/`); expressions are converted verbatim
   to `ibex::Function` and pruned by HC4 fwd/bwd (natural interval extension — the dependency
   problem lives exactly here). Drake symbolic (vendored) has `Expression::Expand()` + polynomial
   ops; a rewrite pass before `ibex_converter` (plus monotonicity — tanh is monotone in its
   argument) is a days-scale spike. **Caveat:** the coupling is `poly(ch)·tanh(poly(J,ch,x))`;
   polynomial rewriting doesn't break the tanh–coefficient coupling, so gains may be modest.
2. **Affine arithmetic — HIGH but bounded.** IBEX upstream historically ships an affine module
   (`ibex_Affine2`/`AffineMain`); **this fork has it stripped** (`src/arithmetic/` has `Interval*`
   only, no `Affine*`). Path: re-vendor the module (fits the existing 12-patch fork model), then
   build a dReal contractor around affine eval. Affine tracks first-order correlations (attacks
   dependency), but: (a) it yields a tighter *forward enclosure*, not a native box *contraction* —
   IBEX itself uses affine to feed **tighter linear relaxations**, i.e. it loops back into the
   polytope/X-Taylor path — which here is *partially effective, not null* (it contracts, 2× on one
   file, but never empties the hard boxes), so a stronger relaxation is attacking a lever with some
   traction rather than a dead one; (b) it still loosens on high-degree-poly × transcendental
   products (exactly the descent body). Substantial work, uncertain it clears the wall.
3. **Taylor models (higher-order poly + interval remainder) — VERY HIGH, research-scale.** Nothing
   to reuse: IBEX has no Taylor-model module. CAPD (already a dReal dep) has TM machinery but it is
   **ODE-flow-oriented** (time integration), not a drop-in static-NRA expression enclosure —
   repurposing means a new arithmetic type threaded through the contractor plus a TM→box backward
   contractor. A new subsystem, not a patch.
4. **Revisit `--forall-pre-prune` (`CtcForAll`) only after (1)/(2).** It **engages but is
   net-negative** on this set (adds overhead, pushed an n2 delta-sat into timeout; never helped a
   verdict); its usefulness is gated on the same enclosure quality. Improve enclosures first, then
   re-measure.

**Key context for all of the above:** IBEX gives dReal exactly *one* of the requested tools —
first-order **X-Taylor linear relaxation** (`ibex_LinearizerXTaylor` → `CtcPolytopeHull`, wired as
`--polytope`/`--forall-polytope` in `contractor_ibex_polytope.cc:108`) — and it is **already present
but only partially effective** (verdict-null: it contracts the box, ~2× on one completing file, but
never empties the hard ones). That the one IBEX-provided "Taylor-ish" lever engages yet still can't
crack the wall is the evidence that the cheap wins are exhausted and the remaining options are heavy.

**Not worth further investment:** more compute (`--jobs` proven null for the goal — not
compute-bound), precision tuning (coarser δ only buys inconclusive delta-sat; finer only adds
timeouts), local-opt/acid/3bcid/general-polytope (**verdict-null** — they shift CPU but no
verdict; ceiling set by avenues 1–3), and longer timeouts (`--jobs 4` already delivered ≈4×
compute-in-wall and cracked nothing). `--smear` is **UNSAT-null too** (branching can't tighten
enclosures) but, unlike these, is a COMPLETENESS win worth keeping (+2 delta-sat; §2026-07-03 caveat
above) — just not a route to the UNSAT wall. If (1)/(2) stall, the honest escalation
is encoder-side (a different proof route — QE over a polynomial over-approximation — or accepting
specific instances as open); that is an owner's call, surfaced not pre-empted.

### 2026-07-02 — avenue #1 (symbolic rewrite) empirically tested and REJECTED

A SymPy parse→transform→emit pre-pass (scratchpad `odeexpr_v2_rewrite/`; frontend modeled on
`nraode_to_nra/unroller`, **every variant lambdify-verified equivalent before solving** — machine-checked
transformation, not a weakened check) tested whether a better-conditioned expression form cracks the
wall. **Result: null for the UNSAT goal, and no robust win otherwise** (all COMPLETENESS — no form
could produce a false `unsat`).

- **0 UNSAT** across the `exists_forall/` set for every provably-equivalent form — `sig2tanh` (the
  inflated sigmoid `−1+2(1+e^{−2x})⁻¹` collapsed to a native `tanh` node, removing genuine exp/pow
  widening), `expand`, `factor`, `horner`, `simplify`, and combos. On the smallest hard `both_descend`
  (n1) all 8 forms still timeout at 120 s, and the graded ICP metric is **invariant**: prune-to-empty
  and interval-≥-precision events are each ~0.50 per loop-head for *every* form. No rewrite shifts the
  contraction balance — the multiplicative `poly·tanh(poly)` dependency is untouched by syntactic form.
- **`sig2tanh` is exactly verdict-neutral** (8/8/14 sat/·/timeout = baseline). IBEX images `tanh([a,b])`
  as tightly as the sigmoid composition; the looseness was never the transcendental *image*. So even
  the narrow "recognize the sigmoid identity in the converter" rewrite is pointless.
- The apparent `factor`/`simplify` speedup is an **N1-delta-sat artifact, not a pre-pass**: `factor`
  helps exactly one small file (`average_descends` n1 dh3, 18.9→3.5 s) but **net-reduces** the solved
  count (16× expression blowup on N2 pushes `both_descend` n2 dh1 from delta-sat/25.9 s into timeout);
  `simplify` (the biggest apparent win, n1 dh3 →1.9 s) is **uncomputable** on the N2 bodies (>45 s hang
  — the forbidden `sp.simplify` of `ode_expressivity/docs/sympy-notes.md`). Neither touches UNSAT.

Bottom line: avenue #1 is dead for this coupling; the honest escalation (affine #2 / encoder-side)
stands. Note a dReal-side symbolic pre-pass would also inherit the *build-side* SymPy-blowup wall the
sibling `ode_expressivity` project already documents (`docs/optimize_lazy_expand.md`,
`docs/sympy-notes.md`).

### 2026-07-24 — RELATED-WORK sweep addendum: lemma-PM measured; the `forall/` wall breached

Two campaign results land here (full record: `OPTIMIZATION_LOG.md` §"RELATED-WORK candidate
campaign"; `benchmark/results/sweep_20260723_115956/ANALYSIS.md`):

- **Lemma-PM measured on-pin** (closing the §"Lemma pattern-matching + quantifiers"
  measure-first item): the `--drpm-max-size 4` arm is exactly base on odeexpr (114/151
  solved, PAR2 1.000× — v1 1.002, v2 0.999), so the constant-factor ceiling predicted there
  is real and buys nothing against the ∃∀ wall. The signal lives elsewhere: github PAR2
  **0.688×** (repetitive BMC unrollings — car-3 k128 541.8→19.1 s, up to 28× single-file),
  plus one tacas single-file pathology (9.2 s → TIM) to diagnose before recommending.
  Family-conditional candidate only.
- **First UNSAT on the odeexpr_v2 `forall/` wall** (the QF subfamily; the ∃∀
  `exists_forall/` wall is intact across all 11 arms — ANALYSIS.md §E): the five
  system-cell arms (obbt/mohc/polytope/affine/hybrid) all refuted
  `sign_agreement …aed75881`, and the instance is machine-verified truly-UNSAT (grid margin
  max −4.0e-06 < 0; J-bound 0.49 < the tanh contraction threshold ½). The refutation is a
  **COMPLETENESS win** for those arms (they refute a T-unsatisfiable φ); base's `delta-sat`
  at δ=0.01 is the legal δ-artifact, not a violation. Cheapest refutation: `--mohc` at
  1.71 s. That the breach came from stronger contraction cells (monotonicity / linear
  relaxation — avenue #2's territory), not from branching or compute, is consistent with
  the enclosure-looseness diagnosis above. (`--mohc` also cracked one base-TIM ∃∀ file
  into SAT — e95d4844, 427.4 s — but no arm moved any ∃∀ verdict toward UNSAT.)

---

## Solver mechanism & soundness (encoding-independent — still current)

These findings are about the ∃∀ *machinery*, not the family, and remain accurate.

**Shape & decision procedure.** `∃ J_*, ch*_* (bounded box). ∀ x ∈ [-1,1]. guard(x) ⟹ φ(c,x)`,
φ a descent (in)equality over `tanh`, `exp`, `pow`. Depth-one ∃∀ NRA
(`docs/forall-semantics.md`), discharged by the CE-guided `ContractorForall` (a nested CE-search
`Context` solving over `c` and `x` as free vars at `inner_delta`). δ-complete ∃∀ (Kong,
Solar-Lezama & Gao, CAV 2018) is **guaranteed to terminate but carries no useful time bound** —
the CE loop bisects the existential box to ~δ width, exponential at tight δ over many
existential dims. That **existential-isolation wall** (not the universal search) is the dominant
cost; strong universal-side contraction cannot escape it.

**Soundness is never at stake.** Every way the `forall` machinery can go wrong *when it answers*
is COMPLETENESS-class (missed refutation → false `delta-sat`), never SOUNDNESS: pruning contracts
only against a *real* forall-domain point, so it can never delete a true ∃∀ solution
(`docs/forall-semantics.md` §4.7). Non-termination yields no verdict — intractability, not a
violation.

**`--forall-pre-prune`** — a sound, COMPLETENESS-only `ibex::CtcForAll` proj-intersection
pre-pruner (`ContractorIbexForall`) running *beside* CEGIS; pure interval contraction, no nested
δ-solve (mechanism: `ibex_docs/AUDIT-QUANTIFIERS.md` Q1; `docs/contractors.md`). Runs under
`--jobs>1` via a per-worker `ContractorIbexForallMt` cell.
**The load-bearing lesson: its benefit is encoding-specific and fragile.** On the *first* June
encoding it was ~100× on `mlp2_n1_h1` (δ=0.2: 114 s → 1 s); the *second* June re-encoding erased
that headroom entirely (baseline already 0 s). It can also *hurt* — on the flat NRA corpus it
slowed `exist_forall_10` from <90 s to >300 s, verdict preserved. **Never assume it carries;
re-validate per workload.** It does not address the existential-isolation hardness — **confirmed
verdict-null again on the 2026-07-01 regenerated ∃∀ set** (engages but changes no verdict, sometimes
net-negative — pushed an n2 delta-sat into timeout; see findings above).

**`--forall-pre-prune-prec`** — near-irrelevant when the universal check isn't the bottleneck.
Mechanical ceiling: prec ≥ universal-box width (2.0 for x∈[-1,1]) makes `LargestFirst`
non-bisectable, collapsing `CtcForAll` to a single midpoint check (prec=4.0 ≡ prec=2.0). For an
**UNSAT goal the heuristic inverts** — finer prec samples more universal points ⇒ stronger
refutation — so "bigger = faster" is a SAT-only artifact. Default 0.5.

**`--forall-polytope`** — enables IBEX's SoPlex LP contractor in the forall context. Requires the
LP backend, switched from `LP_LIB=none` to vendored SoPlex 4.0.2 in commit `fa3b74bd7`
(`-DLP_LIB=soplex`). Helps at moderate δ on some encodings but **measured to hurt** on others
(a June `n1_dh3` at δ=0.5: 19 s baseline → timeout with polytope). Kept as a capability add. It
wraps IBEX's first-order X-Taylor linear relaxation (`ibex_LinearizerXTaylor` → `CtcPolytopeHull`,
`contractor_ibex_polytope.cc:108`) — the closest thing IBEX has to a Taylor method, and
**verdict-null but active on the 2026-07-01 ∃∀ set** (it contracts — ~2× faster on one completing
file, ~4× slower on another — but empties no hard box; see improvement-opportunities above).

**H1 CE-domain fix** (`forall-semantics.md` §4.8). The CE search formerly ε-shrank the universal
domain (`domain^{-ε}`), a silent completeness bug on narrow/point domains. Making it **exact** is
correct but 9×+ slower on wide-domain ∃∀. Resolution: exact domain **only for narrow binders**
(width `< 3ε`, the actual hazard); wide binders keep the fast ε-shrink. COMPLETENESS-only; no
verdict flips. Test: `test/dreal/contractor/test/contractor_ibex_forall_test.cc`.

**Lemma pattern-matching + quantifiers** (2026-07-01 scoping). The lemmas these queries emit are
already **ground** (`QF_NRA_ODE` over `J_*/ch*/x_*`; the `∀` is discharged by the nested CE-search
before any lemma exists), and the CAV26 matcher already fires on them. Perf upside is at most a
constant factor on per-node CE cost — it cannot reduce node count and cannot beat the existential
wall; unmeasured off-pin, so measure (an A/B with and without `--drpm-max-size`; the flag rejects 0, and omitting it turns DRPM off) before building anything. Two fixes
landed while scoping (commit `fc4c3da04`):
- **Bound-var canonicalization UB fix** (`DeBruijnCanonicalizer.cc`): the old `VisitForall`
  appended a `forall`'s *bound* variable to the canonical sequence, driving
  `attempt_substitution`'s `box[y]` check to silently insert a non-solver variable into the Box's
  shared index map (latent corruption) plus a spurious self-match pair. Bound vars are now excluded
  (they are not free solver vars). Test: `pattern_matching_test.cc::ForallBoundVarNoLeak`.
  `forall_t`/`integral` unaffected (genuine Box vars).
- **Auditor forall printing** (`prefix_printer.cc::VisitForall`): previously threw
  `"Not implemented."`, crashing every ∃∀ solve under the default-on theory audit. Now emits the
  faithful desugared `(forall ((v Real)...) (=> domain body))`, which round-trips through `dreal4`.

---

## Historical measurements — June 2026 encodings, δ=0.0005 pin (SUPERSEDED)

*Point-in-time snapshot of the pre-regeneration files; kept for the δ-scaling shape, not as a
description of anything on disk now.* Smallest instance (`mlp2_n1_h1__…__dh1`), default brancher
vs `--forall-polytope`:

| δ | default | `--forall-polytope` (SoPlex) |
|---|---|---|
| 0.5    | delta-sat, 0.19 s | delta-sat |
| 0.2    | timeout (>30 s)   | delta-sat, 21.7 s |
| 0.15   | —                 | delta-sat, 39.1 s |
| 0.1    | —                 | delta-sat, 96.6 s |
| 0.0005 (pinned) | non-terminating | timeout (>60 s) |

Polytope runtime scaled ≈ δ⁻²·²; extrapolating δ=0.1 (96.6 s) → δ=0.0005 (200× tighter) ≈ 10⁵× ⇒
**months** even on the smallest instance — the existential-isolation wall. `--local-optimization`
gave no improvement. At the pinned δ=0.0005, **no solver-side lever changed the verdict** — the
conclusion was that tractability requires an encoder-side change (relax the δ pin / margin), which
is what the 2026-07-01 regeneration (δ=0.01 + zero-witness exclusion) delivered.
