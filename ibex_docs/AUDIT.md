# AUDIT.md — dReal's IBEX leverage: shipped vs. still-open

The payoff of the crawl: header-grounded opportunities for dReal to use IBEX more,
each cross-referenced to current usage
([`dreal-ibex-usage.md`](dreal-ibex-usage.md)) and the nearest fork patch. **Most of
the original audit is now shipped** — the headline (`CtcAcid` shaving) and the
polytope hull both landed as opt-in flags, and the 2026-07 RELATED-WORK campaign
shipped **Newton** too (`--newton` — the last absent contractor, measured **dead on
this corpus**; `../OPTIMIZATION_LOG.md` §"RELATED-WORK candidate campaign"). This
file is organized shipped-first, with the still-open items called out as such.
Ground truth is source + `CMakeLists.txt`.

> **Companion deep-dives (second pass):** the full contractor catalog with
> strengths/weaknesses is
> [`classes/contractors/COMPARISON.md`](classes/contractors/COMPARISON.md); the
> bisector catalog is [`classes/strategy/Bisectors.md`](classes/strategy/Bisectors.md);
> and the "does dReal needlessly reimplement IbexSolve?" question is answered
> (with file:line on both sides) in
> [`ARCHITECTURE-COMPARISON.md`](ARCHITECTURE-COMPARISON.md). Their net conclusion
> *reinforced* this audit: the search loop is correctly dReal's own, and the one
> real loop-level gap was precisely tier A — now shipped (see below).
>
> **Second audit section — nested quantifiers:** for ∃∀ / `∀∃∃∀` queries over
> high-dimensional transcendental constraints (a distinct workload), see the focused
> **[`AUDIT-QUANTIFIERS.md`](AUDIT-QUANTIFIERS.md)** — how IBEX's *composable*
> `CtcForAll`/`CtcExist` contractors pre-prune dReal's CEGIS loop and provide a
> sound skeleton for the deeper alternations dReal currently crashes on. (Its top
> lever, Q1's `CtcForAll` pre-pruner, **shipped** as `--forall-pre-prune` — a strong
> inner contractor compounds super-linearly when the cost is exponential in the
> quantified dimension.)
>
> **Third section — levers *beyond* what mainline IBEX ships:** affine arithmetic,
> OBBT, monotonicity-based `CtcMohc`, DynIbex validated-RK ODE, and alt-integrators
> are ranked by *(integration cost × completeness win)* with source-grounded
> feasibility sketches in the companion **[`RELATED-WORK.md`](RELATED-WORK.md)**. This
> AUDIT covers what IBEX *already ships*; RELATED-WORK covers what needed porting —
> its top five (Newton/affine/ABS/OBBT/Mohc) all shipped and were measured in the
> 2026-07 sweep (outcome stamps there; final record `../OPTIMIZATION_LOG.md`).

**The one-paragraph framing (the crawl's main finding, now largely realized).** By
default, dReal's *only active IBEX contractor is HC4* (`Function::backward`). The
micro-optimization budget of that HC4 path is **already spent** — fork patches
inlined the rounding-mode toggles (23–44%), batched the rounding windows, eliminated
the exception unwinding (~27%), and made the unused gradient lazy (see
`../../ibex-fork/MIGRATION.md`). So the remaining leverage was **algorithmic**: the
strong atomic contractors IBEX ships and dReal didn't run — above all **ACID** (which
IBEX's own solver enables by default). **That headline is now shipped** as opt-in
`--acid`/`--3bcid` (`theory_solver.cc:244`), alongside the LP-relaxation polytope hull
(`--polytope`, over `LP_LIB=soplex`) and the `--forall-pre-prune` CtcForAll pre-pruner.
All default-off (soundness-neutral opt-ins), running under `--jobs>1` via a
per-worker `*Mt` cell. **Newton** — formerly the sole contractor IBEX ships that dReal
had no path to — shipped 2026-07 as `--newton`/`--newton-ceil` and was **measured dead**
(zero unique solves, github 2.75× worse, 11 OOMs on giant unrollings; kept for record).

> **Soundness framing (mandatory, per project `CLAUDE.md`):** every item here is a
> **COMPLETENESS** lever (tighter contraction → fewer search nodes / more
> refutations — `asserts φ^δ T-satisfiable on a T-unsatisfiable φ` is what looser
> contraction risks; a missed refutation, never a false `unsat`). None of these
> change soundness: a contractor that contracts *less* can only cost completeness.
> The soundness-load-bearing parts (gaol rounding, underflow) are the fork's job,
> already done (#8,#9,#10,#12). So these are safe to try — the worst case is "no
> speedup," not "wrong answer." SAT↔UNSAT *verdict* flips on benchmarks would
> signal an **integration bug**, not an expected outcome → escalate per `/benchmark`.

## Shipped (was the audit's headline try-order)

| Item | Tier | Flag | Wired at | Tune with |
|---|---|---|---|---|
| **`CtcAcid` shaving on the HC4 path** | A | `--acid` | `theory_solver.cc:244`; `contractor_ibex_acid.cc:110` | `--acid-ct-ratio` (default 0.002), `--s3b` (default 10) |
| `Ctc3BCid` (fixed-parameter sibling) | A | `--3bcid` (mut. excl. with `--acid`) | `theory_solver.cc:244`; `contractor_ibex_acid.cc:114` | `--s3b` (best 5–200) |
| LP-relaxation **polytope hull** | D | `--polytope` / `--forall-polytope` | `theory_solver.cc:237`; `contractor_ibex_polytope.cc:108` | corners/slope hardcoded RANDOM_OPP/HANSEN (no flag) |
| `CtcForAll` **pre-pruner** for ∃∀ | Q1 | `--forall-pre-prune` | `contractor_ibex_forall.cc` | `--forall-pre-prune-prec` (default 0.5) |
| **`CtcNewton`** square-equality cell (2026-07; **measured dead** — REFUTED) | D | `--newton` | `contractor_ibex_newton.cc` | `--newton-ceil` (default 0.01) |
| **OBBT** certified 2n-LP bound tightening (2026-07; **measured dead**, ≈polytope; BUG-014-exposed) | D | `--obbt` | `contractor_ibex_obbt.cc` | — |
| **`CtcMohc`** monotonic propagation, fork port (2026-07; **measured odeexpr winner** 0.968×, ODE families net-negative) | D | `--mohc` | `contractor_ibex_mohc.cc` | composable with `--acid`/`--3bcid` |
| polytope **linearizer selector** (2026-07; **prefer `affine`** — solve-set ⊃ xtaylor's, crash-free) | D | `--polytope-linearizer xtaylor\|affine\|both` | `contractor_ibex_polytope.cc` | requires `--polytope`/`--forall-polytope` |

All default-off, soundness-neutral, `--jobs>1`-safe (per-worker `*Mt` cell). The 2026-07
RELATED-WORK sweep supplied the A/Bs for the new rows (final record:
`../OPTIMIZATION_LOG.md`; analysis `../benchmark/results/sweep_20260723_115956/ANALYSIS.md`
+ `sweep_20260724_120008/compare.txt`): family-conditional — `--mohc` / `--polytope
--polytope-linearizer affine` on odeexpr only, ODE families keep base flags. The
`--acid`/`--3bcid` tuning sweep (try-order #1) is still uncommitted.

## Still-open try-order

| Try # | Item | Tier | Ease (1=hard,5=easy) | Likelihood it helps | Value if it does | Validate with |
|---|---|---|---|---|---|---|
| 1 | **Tune the shipped shaving** — `--acid-ct-ratio`/`--s3b` sweep on the odeexpr NRA family | A | 4 | high | high | OFAT sweep `s3b`∈{5,10,20,50}, `ct_ratio`∈{0.001,0.002,0.005}; PAR2, no verdict flips |
| 2 | Cross-check dReal's own fixpoint stop-ratio vs IBEX (0.01/0.1) | B | 4 | low–med | low–med | A/B the worklist ratio; likely small (micro-opt spent) |
| 4 | Expose polytope `corners`/`slope` as flags + sweep | D | 3 | unknown | med | the linearizer family now has a win (`--polytope-linearizer affine`, odeexpr_v1 0.804×) — but X-Taylor's own corners/slope are moot while BUG-014 keeps the xtaylor path crash-exposed |

(Former try-orders #3 `CtcNewton` and #5 `LinearizerAffine2` shipped 2026-07 and moved to
the shipped table above — Newton measured dead, affine measured the odeexpr_v1 winner.)

## Tier A — strong contraction add-ons (the headline — SHIPPED)

### A1. `CtcAcid` — adaptive 3BCID shaving on top of HC4  ⭐ SHIPPED
- **What it buys:** ACID shaves variable bounds and constructive-disjoins the
  remainder, *adaptively* choosing how many variables to shave per box. It is the
  single strongest general-purpose contractor IBEX ships.
- **Hard evidence (why it was the headline):** IBEX's own `DefaultSolver` composes,
  in order, `CtcHC4(sys, 0.01)` **then** `CtcAcid(sys, CtcHC4(sys, 0.1, true))`
  (`ibex-fork/src/solver/ibex_DefaultSolver.cpp:82-85`, verified). dReal's default
  composes **HC4 fwd-bwd only** (`src/dreal/solver/theory_solver.cc:207`) — the first
  half of IBEX's default stack. ACID is not an exotic add-on, it's the piece IBEX
  considers standard. Full cross-side analysis:
  [ARCHITECTURE-COMPARISON.md](ARCHITECTURE-COMPARISON.md).
- **dReal status:** 🟢 **shipped, opt-in `--acid`.** Assembled at
  `theory_solver.cc:244`; `ibex::CtcAcid(system, hc4_sub, /*optim=*/false, s3b, scid, var_min_width, ct_ratio)`
  (`scid`/`var_min_width` passed at IBEX defaults — KNOBS §3) built at
  `contractor_ibex_acid.cc:110`, wrapping dReal's callback-bearing HC4 sub
  (so theory lemmas survive — the C1 checklist was met at build time). Per-worker
  `ContractorIbexAcidMt` under `--jobs>1` (`contractor.cc:215`). Node:
  [`classes/contractors/CtcAcid.md`](classes/contractors/CtcAcid.md).
- **Remaining work:** tune `--acid-ct-ratio` (default 0.002) and `--s3b` (default 10)
  — see the still-open try-order #1 and KNOBS §3.
- **Cost/risk:** per-box cost rises (ACID calls HC4 many times); net win depends on
  search-node reduction outweighing it. Soundness-neutral — a COMPLETENESS lever.
- **Validate:** `/benchmark` odeexpr first (pure NRA, isolates the contractor),
  then ODE families. PAR2 < baseline = win; any SAT↔UNSAT flip = integration bug.

### A2. `Ctc3BCid` — the fixed-parameter sibling  SHIPPED
- **What:** same shaving without ACID's adaptivity; you set `s3b` directly.
  🟢 **shipped, opt-in `--3bcid`** (mutually exclusive with `--acid` —
  `dreal_main.cc:687` throws if both set); `ibex::Ctc3BCid(hc4_sub, s3b)` at
  `contractor_ibex_acid.cc:114`. Node: [`Ctc3BCid.md`](classes/contractors/Ctc3BCid.md).
- **Why keep it beside A1:** the clean experiment to (a) confirm shaving helps dReal's
  instances at all and (b) find a good `s3b` (the header's "tune-first" param, best
  5–200) before trusting ACID's auto-tuning. Cheaper to reason about; a good first probe.

## Tier B — cheap tuning of the path dReal already uses
### B1. Fixpoint stop-ratio cross-check
dReal's hand-rolled worklist (`contractor_worklist_fixpoint.cc`) has the analog of
`CtcPropag`'s ratio (IBEX 0.01) / `CtcFixPoint`'s (0.1). Worth confirming dReal's
value is in the same regime and A/B-ing it. **Expected small** — the docs warn the
ratio gives no guarantee on fixpoint distance, and the hot-path budget is spent.
KNOBS §2.

## Tier C — guards (correctness, not speed)
### C1. Fork-patch integration checklist for any borrowed contractor
Met when A1/A2/polytope shipped; **re-apply for any future borrowed contractor**
(e.g. Newton). The borrowed IBEX contractor must:
1. use dReal's **callback-bearing** fwd-bwd as sub-contractor (lemma tracking,
   fork #2/#5/#6/#7) — not a fresh `CtcFwdBwd`;
2. detect emptiness via `is_empty()` (return-status, fork #11) — not a caught
   `EmptyBoxException`;
3. run under `UpwardRoundingScope` so gaol rounding stays sound (#8,#9,#10,#12);
4. tolerate running inside DPLL(T) on transient literals (no global state that
   leaks across a throw — see the `Bug002` SIGBUS class in dReal's own tests);
5. ship a per-worker `*Mt` cell (IBEX contractors hold mutable state) for `--jobs>1`.
This is a **conscious checklist**, not a code change — it's where an integration
silently degrades lemmas or soundness if skipped.

## Tier D — conditional / niche
- **D1. `CtcNewton` — SHIPPED 2026-07 (`--newton`/`--newton-ceil`), MEASURED DEAD.**
  The cell landed as sketched (interval-Newton over the square equality subsystem,
  `contractor_ibex_newton.cc`, per-worker `*Mt`, C1 checklist met) and the sweep
  **refuted** RELATED-WORK's #1-ranked premise: zero unique solves over 270 files, the
  equality-dense github family 2.75× *worse*, saradc 20→3 solved with 10 OOMs (~10.5 GB
  during cell construction on giant unrollings); inert on odeexpr (the
  `!include_ode` equality filter leaves it nothing to build there). All harm is
  COMPLETENESS-only (missed refutation/witness within budget — asserts φ^δ T-satisfiable
  undetermined within budget, never a false unsat). Kept as an opt-in for record. Node:
  [`CtcNewton.md`](classes/contractors/CtcNewton.md).
- **D2. Polytope hull — SHIPPED.** `LP_LIB=soplex` (`CMakeLists.txt:189`, vendored
  SoPlex 4.0.2) and `--polytope`/`--forall-polytope` are live (`theory_solver.cc:237`;
  `ibex::CtcPolytopeHull(LinearizerXTaylor(system, RELAX, RANDOM_OPP, HANSEN))` at
  `contractor_ibex_polytope.cc:108`). The X-Taylor `corners`/`slope` knobs are
  hardcoded at those IBEX defaults, not yet exposed as flags (still-open try-order #4;
  KNOBS §4); the *linearizer itself* is now selectable (`--polytope-linearizer
  xtaylor|affine|both`, D3). `--forall-polytope` has *mixed* measured perf
  (`exists_forall_perf.md`). **BUG-014** (`../docs/dreal-bugs.md`): the X-Taylor LP rows
  trigger an OOB write in vendored SoPlex 4.0.2 presolve (`SPxMainSM::duplicateCols`) —
  pre-existing, latent SOUNDNESS risk (a corrupted box narrowed past a true model would
  be a false unsat; none observed), affine rows through the same LP chain are clean —
  so prefer `--polytope-linearizer affine` until fixed.
  Nodes: [`CtcPolytopeHull.md`](classes/contractors/CtcPolytopeHull.md),
  [`LinearizerXTaylor.md`](classes/linear/LinearizerXTaylor.md).
- **D3. Affine linearization (`LinearizerAffine2`) — SHIPPED 2026-07
  (`--polytope-linearizer affine|both`), MEASURED.** The plugin port landed fork-side
  (`b5e7a212`) with the fAF2 rounding obligation discharged
  (SOUND-WITH-SCOPE-WRAP — [`affine-rounding-audit.md`](affine-rounding-audit.md)).
  Sweep verdict: **affine strictly dominates X-Taylor here** (polytope's solve set ⊂
  affine's; odeexpr_v1 0.804× with two base-TIM cracks; hybrid `both` refuted, 113 <
  affine's 114) and dodges BUG-014. Full record + the Araya-2024 divergence note:
  [`RELATED-WORK.md`](RELATED-WORK.md).
- **D4. `CtcInverse` / `CtcQInter`.** Inverse-image contraction and outlier-robust
  q-intersection — niche; no obvious dReal use shape.

## Tier E — negative results (confirm dReal correctly ignores)
Recorded so future sessions don't re-investigate. dReal is an **SMT solver**, not a
standalone CSP/NLP solver; these serve a different problem shape:
- IBEX `Solver`/`Optimizer` ([solver](chapters/solver.md), [optim](chapters/optim.md))
  — dReal has DPLL(T) + nlopt.
- Separators / `Set` paving ([separator](chapters/separator.md), [set](chapters/set.md))
  — set *characterization*, not refutation (but see capability extensions).
- Minibex parser ([minibex](chapters/minibex.md)) — dReal has SMT2 + `.dr` parsers
  (dReal3 compat is intentional).
- Bisectors / cell buffers ([strategy](chapters/strategy.md)) — dReal branches
  inside DPLL(T).
- Inner arithmetic (`]f[`, `ibwd_*`) — for inner-region/inflation, not refutation.

## Capability extensions (non-perf future directions)
Not speed levers — new *capabilities* IBEX could give dReal, the IBEX analog of
the CAPD PDE/DAE notes:
- **Feasible-set output via separators + pavings.** If dReal wants to *return the
  set* of solutions to an ∃∀/parametric query (not just sat/unsat), IBEX's
  `Sep*` + `Set`/`SetInterval` machinery is the path
  ([separator](chapters/separator.md), [set](chapters/set.md)).
- **Rigorous optimization-modulo-theories** via the IBEX `Optimizer` +
  `ExtendedSystem` + `CtcKuhnTucker` — a *sound* global bound where dReal currently
  uses nlopt's local one ([optim](chapters/optim.md)).

## Honesty boundary (what's proven vs hypothesized)
**Proven** (header/doc/code-verified): every default and option menu in
[KNOBS.md](KNOBS.md); that ACID is IBEX's default contractor; that the opt-in flags
(`--acid`/`--3bcid`/`--polytope`/`--polytope-linearizer`/`--newton`/`--obbt`/`--mohc`/
`--forall-pre-prune`) are built at the cited `theory_solver.cc`/`contractor_ibex_*.cc`
sites over `LP_LIB=soplex`; that the HC4 hot-path levers are already pulled (fork
patches, with their measured %); the Tier-E "different problem shape" reasoning. (The
pre-2026-07 claims "Newton is the sole absent contractor" / "affine isn't in the fork"
are now historical — both shipped.)
**Measured** (2026-07 sweep — `../OPTIMIZATION_LOG.md` §"RELATED-WORK candidate
campaign"): newton dead, obbt dead, mohc = the odeexpr-only winner (0.968×), affine >
xtaylor with the hybrid refuted, everything family-conditional (ODE families keep base
flags), zero verdict flips beyond the machine-verified aed75881 refutation
(COMPLETENESS win for the refuting arms — base's delta-sat is the legal δ-artifact).
**Hypothesized** (still no committed A/B): the `--acid`/`--3bcid` *tuning* payoff
(try-order #1 on the odeexpr NRA family) and the polytope `corners`/`slope` sweep
(try-order #4, gated on BUG-014).
