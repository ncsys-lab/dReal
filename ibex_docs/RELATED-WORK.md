# Related work & extensions — levers beyond mainline CAPD/IBEX

> **Soundness/completeness (mandatory — `../CLAUDE.md`, `../docs/soundness-vs-completeness.md`):**
> every candidate here is a **COMPLETENESS / performance lever**, never a soundness lever.
> A tighter rigorous enclosure removes only points with no model → **fewer missed refutations
> and less over-splitting** (COMPLETENESS — bears on missed refutation / false `delta-sat`).
> dReal's verdict stays sound regardless of which tool produced the enclosure, because every
> one is consumed as a *sound over-approximation*. The one way any of these can *break*
> soundness is a **rounding / feed defect** in the port (an inward-rounded coefficient, an
> `_err` term that under-estimates, a truncated ODE feed) that narrows a box past a true model
> → **false `unsat`** (SOUNDNESS). Those hazards are called out per-candidate; they are port
> hygiene, not properties of the methods.

A companion to [`AUDIT.md`](AUDIT.md) (single-quantifier contractor leverage) and
[`AUDIT-QUANTIFIERS.md`](AUDIT-QUANTIFIERS.md) (∃∀). Those two audit *what IBEX already ships
and dReal could bind*; this node surveys the level above — **external methods, IBEX plugins
off the built branch, and rigorous-ODE siblings to CAPD** — as candidate additions, each with
its integration surface and the smallest soundness obligation that discharges it. dReal's
baseline is [`dreal-ibex-usage.md`](dreal-ibex-usage.md): HC4 by default; `--polytope`
(`CtcPolytopeHull` + `LinearizerXTaylor`), `--acid`/`--3bcid`, `--forall-pre-prune` opt-in; CAPD
Lohner (order 20) the sole ODE backend. Source ground truth for every IBEX symbol below is the
fork checkout `../../ibex-fork` (built branch `dreal-perf-patches`; the affine plugin lives on
the dead branch `origin/dev_affine_arith`, tip `0e4a8428`, 2019-04-08, **11 ahead / 673 behind**
master). Live upstream IBEX docs: <https://ibex-team.github.io/ibex-lib/>.

---

## Ranked opportunities

Ranked by *(low integration cost × credible completeness win)*; `try-order` is the recommended
prototyping sequence. "Verified?" = did the dossier read the actual source / API this session.

> **MEASURED 2026-07-23/24:** candidates 1–5 shipped as opt-in flags and went through an
> 11-arm × 270-job sweep + a 4-arm ODE-family re-sweep — final record in
> `../OPTIMIZATION_LOG.md` §"RELATED-WORK candidate campaign"; full analysis
> `../benchmark/results/sweep_20260723_115956/ANALYSIS.md` and
> `../benchmark/results/sweep_20260724_120008/compare.txt`. The Verified? cells below carry
> the outcome stamps; the surrounding analysis is kept as the historical record that
> motivated the arms.

| # | Candidate | What it buys | Effort | Soundness risk | Verified? | Try-order |
|---|---|---|---|---|---|---|
| 1 | **`CtcNewton`** (interval Newton / Krawczyk / Hansen-Sengupta) | Quadratic contraction near a root on locally-square subsystems + *certified existence* — a completeness win on equality-heavy atoms HC4 handles only linearly | **S** | None (contraction under correct rounding; guarded to square subsystems) | Yes — shipped as `--newton`. **MEASURED: REFUTED** — zero unique solves over 270 files, github PAR2 2.75×, saradc 20→3 solved (10 OOMs ~10.5 GB during cell build); odeexpr inert (equality filter) | 1 |
| 2 | **Affine linearizer** `LinearizerAffine2` (+ **hybrid** with X-Taylor) | Second linear relaxation for `CtcPolytopeHull` that captures first-order variable *correlations* (dependency problem) an X-Taylor corner misses; the 2024 result says the two are complementary, not either/or | **M** | Low but real: reconcile the affine plugin's internal rounding (fAF2 error-free transforms) with dReal's phase-scoped rounding contract — **audit before trusting** (audit since discharged: SOUND-WITH-SCOPE-WRAP, [`affine-rounding-audit.md`](affine-rounding-audit.md)) | Yes — shipped as `--polytope --polytope-linearizer affine`. **MEASURED: odeexpr_v1 0.804× (+2 base-TIM cracks), but the 2024 complementarity did NOT transfer** — polytope's solve set ⊂ affine's, hybrid adds nothing; ODE families net-negative (34/108) | 2 |
| 3 | **ABS activity-based branching** (`--branch abs`; Michel & Van Hentenryck CPAIOR'12 — the VSIDS lift; [details below](#conflictimpact-aware-branching-the-vsids-analog-dreal-is-missing)) | Dynamic, history-aware split-variable choice; its bump signal (`ContractorStatus.output_`) is already computed per-prune by *every* contractor **including ODE** — constraint-aware exactly where `--smear` is structurally blind | **S–M** | None (branch choice never moves a verdict) | Yes — shipped as `--branch abs\|absdiam`. **MEASURED: degeneracy CONFIRMED, both variants dead** (abs 1.611×/1.865× on v1/github with absdiam ≈ base; on v2 both collapse — activity actively mis-guides) | 3 |
| 4 | **OBBT** (optimization-based bound tightening) | Strictly stronger use of the LP `CtcPolytopeHull` already builds: 2n LPs (min/max each var over the joint relaxation) instead of one hull projection | **M** | Soundness-critical: each LP bound needs Neumaier-Shcherbina safe directed-rounding post-verify (dReal's `UpwardRoundingScope` already models this) | Method sourced; shipped as `--obbt`. **MEASURED: dead** — ≈ polytope (1.026× its PAR2), worse than base on both odeexpr families; BUG-014 crash-exposed (X-Taylor LP path) | 4 |
| 5 | **`CtcMohc`** (monotonicity-based hull consistency) | Optimal hull-consistent contraction on monotone vars — attacks the multi-occurrence dependency problem with **no LP**; lighter complement to polytope | **M** | None (hull consistency removes only infeasible points) | Source ported into the built fork (`b5e7a212`); shipped as `--mohc`. **MEASURED: the sweep's only base-beater on odeexpr** (115/151 solved, PAR2 0.968×, two base-TIM cracks, cheapest aed75881 refutation 1.71 s) — but net-negative on the ODE families (69/108 vs base 106) | 5 |
| 6 | **DynIbex** (validated Runge-Kutta ODE plugin) | IBEX-native rigorous ODE backend; affine-RK counters the *wrapping effect* on linear/contracting flows where QR-Taylor struggles; cheap-per-step fast-path / cross-check beside CAPD | **M–L** | Completeness lever; re-audit feed precision + rounding scope for a second integrator | Present in this fork's history (`soonho-upstream/ibex-2.6.5-with-dynibex`); relative accuracy **unmeasured** | 6 |
| 7 | **GANRA-style GPU candidate generator** | Thousands of parallel gradient-descent SAT candidates feeding `--seed-samples`' verify step | **L** | Completeness-only (each point re-verified by `EvaluateBox`, as `--seed-samples` already does) | Abstract only; CUDA dep + code availability unconfirmed | 7 |
| 8 | **Alt. ODE integrators** (Flow*, Ariadne) | Taylor-model / function-calculus enclosures potentially tighter than CAPD doubletons on the stiff `odeexpr` cases the worklogs flag intractable | **L** | Completeness lever; whole-flowpipe APIs → large glue + feed/rounding re-audit | C++/embeddable verified; **no in-SMT-loop measurement** | 8 |

*(Rank 3 supersedes the survey's original "arithmetic-propagation branching (clauseSMT)" row: the
deep-dive confirmed clauseSMT itself is MCSAT-bound and does not transfer, and identified ABS as the
architecture-native form — see the branching section below for the full ranking incl. what got
demoted.)*

**Already bound — do not re-propose as new** (flagged so the next reader doesn't rediscover them):
the "Taylor-model contractor" for NRA atoms *is* `LinearizerXTaylor`/`--polytope`
(`contractor_ibex_polytope.cc:108`); `CtcForAll` pre-pruning *is* `--forall-pre-prune`;
constraint-aware split-variable choice *is* `--smear`; multi-start local SAT search *is*
`--seed-samples`.

**Dropped** (surveyed, not worth it): standalone AA libraries **aaflib / libaffa / Arpra** are
strictly dominated by IBEX's own rigorous affine plugin (which already consumes `ibex::Function`
DAGs and is a `Linearizer` drop-in) and, being non-rigorous floating-point AA, are a **false-`unsat`
soundness hazard** absent a documented outward-rounding guarantee; **AADD** targets discrete/
continuous co-analysis, not ICP contraction; **Bernstein** forms are polynomial-only (miss dReal's
transcendental/ODE core); **Kodiak** computes equilibria/bifurcation sets, *not* time-flow
enclosures (not an ODE integrator despite adjacency); **CORA** is MATLAB (not C++-embeddable);
**JuliaReach** is Julia (FFI-only); **`ibex-ode`** is a Lyapunov attraction-region tool, *not* an
integrator; **`ibex-sip`**'s useful primitive (`CtcForAll`) is mainline and already wired, and its
SIP *optimizer* layer solves a different problem.

---

## Affine arithmetic (the IBEX plugin path)

The deepest lever, because it is the only affine machinery that is *already an `ibex::Linearizer`*
— a genuine sibling of the `LinearizerXTaylor` dReal binds today — and it consumes exactly the
`ibex::Function` DAGs `util/ibex_converter.cc` already emits. It is **not** on the built branch;
it lives on `origin/dev_affine_arith` as `plugins/affine` (+ `plugins/affine-extended`), author
Jordan Ninin, LGPL (same as IBEX — no new license burden). Upstream: also mirrored at
[`ibex-team/ibex-affine`](https://github.com/ibex-team/ibex-affine).

### The three source pieces (verified in `../../ibex-fork`, branch `origin/dev_affine_arith`)

| Symbol | Header | What it is |
|---|---|---|
| `class LinearizerAffine2 : public Linearizer` | `plugins/affine/src/numeric/ibex_LinearizerAffine2.h:31` | The ART (Affine Reformulation Technique) linearizer. `virtual int linearize(const IntervalVector&, LPSolver&)` (`:44`) — **byte-identical signature to `LinearizerXTaylor`**. Per constraint: one forward affine eval, reads the affine form's linear coeffs, rescales noise-symbol coeffs back to box coords, emits a sound LP row using the accumulated error term `af2.err()` (+ a `2^-50` slack) as the rigorous remainder. Holds an `AffineEval<AF_Default>* goal_af_evl` (`:67`). |
| `class AffineEval : public FwdAlgorithm` | `plugins/affine/src/function/ibex_AffineEval.h:29` | Forward affine evaluator over the `Function` DAG (same structural role as interval `ibex_Eval.h`). `typedef AffineEval<AF_Default> Affine2Eval` (`:133`), `AffineEval<AF_Other> Affine3Eval` (`:134`). **Forward only** — there is no affine backward/HC4-revise, so it *cannot* replace `Function::backward` (HC4); the realistic use is the linearizer path, not tightening HC4. |
| affine forms + modes | `plugins/affine/src/arithmetic/ibex_Affine.h` | `typedef AF_fAF2 AF_Default` (`:58`, floating-point Affine Form v2, Ninin 2013 — dense `_val[]` coeffs + single `_err` accumulator, Dekker/Knuth `twoSum`/`twoProd` error-free transforms); `typedef AF_fAFFullI AF_Other` (`:63`, Affine3 — the DynIbex/ENSTA form, Sandretto & Chapoutot 2014, sparse center + ray list + interval garbage). Univariate nonlinear ops linearized via `change_mode` (`:158`): enum `{AF_Default=0, AF_Chebyshev=1, AF_MinRange=2}` (`:154`), default = **Chebyshev** (min-max abs error); MinRange avoids range overestimation. The `affine-extended` plugin adds `AF_fAF1/AF_iAF/AF_sAF/AF_No` behind `--with-affine-extended` (rarely needed). |

*(Form attributions read from the file headers, not inferred: fAF2 header = "Jordan Ninin,
Created Jul 16 2013"; fAFFullI header = "DYNIBEX … ENSTA ParisTech … Sandretto and Chapoutot,
Jul 18 2014".)*

### Why it helps

Affine arithmetic represents a quantity as `x0 + Σ xᵢ·εᵢ`, `εᵢ ∈ [−1,1]`, **sharing noise
symbols across subexpressions**. Linear ops are exact and preserve correlations; a nonlinear op
adds one fresh noise symbol bounding its linearization error. It beats natural-interval / HC4
exactly on the **dependency problem** — when the same variable recurs across a constraint (`x*x −
x`, long polynomial/rational bodies), interval arithmetic treats each occurrence as independent
and over-widens, while AA cancels correlated terms → markedly tighter enclosure. This is the
*contraction-side counterpart* of the very rule
[`../docs/writing-fast-dreal-formulas.md`](../docs/writing-fast-dreal-formulas.md) asks authors to
apply by hand (factor multi-occurrence expressions): `LinearizerAffine2` tightens the same
structure automatically. **Cost side:** AA is O(k) per op where k grows with each nonlinear op, so
on cheap / low-reuse constraints it is net-negative — the payoff is where a constraint is
expression-heavy *and* correlation-rich.

### The one soundness obligation (flag prominently)

fAF2's `_err` term is fed by the Dekker/Knuth `twoSum`/`twoProd` error-free transforms, which are
**exact only under round-to-nearest** (`FE_TONEAREST`). dReal runs interval/gaol arithmetic under
**`FE_UPWARD`** (`UpwardRoundingScope`) and CAPD/formatting under `FE_TONEAREST`
(`NearestRoundingScope`) — see [`../docs/rounding.md`](../docs/rounding.md), whose failure mode is
a *silent* false `unsat`. The dossier's own two verification passes **disagree** on whether this is
already discharged: one holds fAF2 sound because `itv()` is taken with outward interval rounding so
the wrapping dominates; the other warns that under `FE_UPWARD` the transforms mis-capture and
`_err` could *under*-estimate → **SOUNDNESS (false `unsat`)**. That disagreement is itself the
finding: **before trusting a ported affine linearizer, establish under which rounding scope its
error-free transforms execute and confirm `_err` stays an over-estimate** — either run the affine
pass under `NearestRoundingScope` (matching the transforms' assumption) or prove the outward
interval wrapping already dominates. Do not assume; audit. (The polytope hull itself is sound
regardless — `CtcPolytopeHull` intersects the LP-derived bound with the box by inclusion, under
`UpwardRoundingScope`, `contractor_ibex_polytope.cc:126`.) Secondary hygiene: the authors' own
`// TODO TO CHECK` sits on the soundness-critical `add_constraint` bound lines.

### The port reality

Not the "one-line swap" a naive read of the API match suggests — the base-class *interface* is a
drop-in (`Linearizer::linearize(box, lp) = 0` matches), but the plugin is a **dead 2019 branch**
with real drift against the built fork:
- **~12–13 interdependent files** to vendor (`Affine`/`Affine2`/`Affine3`, `AffineDomain`/`Matrix`/`Vector`, `AffineEval`, `LinearizerAffine2`) + moving a waf-plugin subtree into the CMake `ExternalProject`.
- **API drift** (verified): `LPSolver::get_epsilon()` used by the EQ branch is **removed** from the CMake fork (compile break); `FwdAlgorithm` gained `atan2_fwd`/`floor_fwd`/`ceil_fwd`/`saw_fwd` (`dreal-perf-patches:src/function/ibex_FwdAlgorithm.h:101/180/183/186`) that the 2019 `AffineEval` must override to compile.
- **Coverage gap** (verified, sound but loose): `max_fwd`/`min_fwd`/`atan2_fwd` fall back to plain interval — `AffineEval.h:282-296`, literal comment `// Not implemented in Affine Arithmetic`. dReal *does* support `atan2`, so those atoms get no affine tightening (a completeness limitation, not a bug).

### The hybrid (the actionable 2024 result)

Araya/Messine/Ninin/Trombettoni 2024 (J. Global Optim.) show IBEX's affine relaxation and its
X-Taylor relaxation are **complementary**: running both cuts into one `CtcPolytopeHull` LP yields
strictly more accurate linear relaxations than either alone. Since dReal already ships X-Taylor,
this is the published sweet spot — *add* `LinearizerAffine2` beside `LinearizerXTaylor`, don't
replace it. Reported qualitatively as solving more of 279 COCONUT instances in generally less CPU
time; **exact per-benchmark deltas UNVERIFIED** (publisher PDF was not loadable this session).
Method foundation (Ninin/Messine/Hansen 2015, 4OR): the affine relaxation solved **64/74 COCONUT
global-optimization problems (32 for the first time) to 1e-8 relative error** — verified against
the paper abstract (an earlier dossier draft's "61/74" was a transcription error). Note this is
**global-optimization branch-and-bound, a different task from dReal's δ-decision** — indirect
evidence for the ICP contractor loop, not a dReal-corpus number.

**MEASURED 2026-07 (the dReal-corpus number now exists — and diverges from the 2024
prediction).** Three-arm sweep (`--polytope` vs `…-linearizer affine` vs `both`,
`sweep_20260723_115956/ANALYSIS.md` §H-affine): on odeexpr (151 valid files) polytope's solve
set is a **strict subset** of affine's (polytope-only = 0, affine-only = 6), and hybrid `both`
solved *fewer* than affine alone (113 < 114, nothing exclusive, plus one silent-signal crash
loss) — so on this corpus there is no complementarity to exploit; **prefer affine outright**
(odeexpr_v1 0.804×, two ~2 s cracks of base-TIM instances). Honest divergence, not a
contradiction of the paper: COCONUT global optimization ≠ δ-decision ICP, and the prediction
simply did not transfer.

---

## Alternative / complementary rigorous ODE integrators vs our CAPD Lohner path

dReal binds **CAPD** only (`contractor_odes_capd.cc`, `capd/capdlib.h`), driven at Taylor **order
20**, integrating both directions (`--ode-backward`). CAPD is the reference the rest are measured
against — and dReal's own prior finding
([`../docs/decisions.md`](../docs/decisions.md) "ODE backend", `../OPTIMIZATION_LOG.md`) is that
**CAPD order-20 was at or below the retired Codac `CtcLohner` on every tested ODE benchmark**, so
the bar is high. Any replacement must reproduce two things CAPD gives: **backward** integration
(a box contractor needs it) and the phase-scoped rounding discipline.

| Tool | Lang / embeddable | Enclosure | Position vs CAPD | Integration verdict |
|---|---|---|---|---|
| **CAPD** (incumbent) | C++ | Cr-Lohner C0/C1/C2 doubleton/triangleton, interval Taylor | — (baseline) | Already integrated; GPL |
| **DynIbex** | C++, **IBEX-native** | validated Runge-Kutta (Heun/Midpoint/RK4/Radau/Lobatto/Gauss) over affine arithmetic | Method, not speed: affine-RK counters wrapping on linear/contracting flows; lower order (~4) ⇒ generally **looser** than CAPD order-20 on long horizons | **Least-friction ODE candidate** — same IBEX/gaol arithmetic (unified rounding contract), consumes the same `Function` DAGs; present in this fork's history. License = IBEX-plugin family (verify). Authors themselves: "not competitive on time computation" vs VNODE-LP (Reliable Computing 22, p.101) |
| **Codac** (`CtcLohner`) | C++/Py/Matlab | tube-based guaranteed integration | Did **not** beat CAPD (dReal's own retirement note) | Path known — was wired then **retired** (`--capd-t-gate`/`--capd-ndim-gate` removed). Still-novel bit is its separator/thickset catalog, not the integrator |
| **Flow\*** | C++ (`libflowstar.a`) | Taylor-model flowpipes (bounded-degree poly + interval remainder) | Can beat CAPD doubletons on some polynomial dynamics | In-process *conceivable* but no fine-grained contractor API (builds whole flowpipes) → substantial glue for per-slice forward+backward intersect. GPL-3 + GMP/MPFR/GSL/GLPK; GPL is pre-existing via CAPD |
| **Ariadne** | C++ (97%) + Py | Taylor models over sparse polys, rigorous function calculus | ARCH-COMP: **tighter on medium/high nonlinearity** where Flow*/CORA can fail to complete | Native C++ rigorous ODE evolution; whole-system set-evolution abstraction → non-trivial to extract a per-slice contractor. GPL-3 + MPFR/Cairo |
| **VNODE-LP** | C++ | interval Taylor + Hermite-Obreschkoff (IHO); QR/Lohner wrapping | Closest architectural sibling to CAPD; IHO = smaller truncation error / fewer coeffs at equal order | A second Lohner-family backend adds little over CAPD; own interval lib (PROFIL/BIAS or FILIB++) = a **second rounding contract to police**; effectively unmaintained (~2006). License unverified |
| **CORA** | MATLAB (99.7% `.m`) | **polynomial zonotopes** (richest rep, least over-approximation on non-convex sets) | Among fastest in ARCH-COMP (adaptive) | **Not C++-embeddable** — offline oracle/cross-check only; imported boxes would need re-verification in dReal's arithmetic |
| **JuliaReach** (`TMJets`) | Julia | Taylor models w/ rigorous remainders | **Best reported enclosure width on stiff cases** (ARCH-COMP), at higher CPU than CORA | Julia → embedding `libjulia`/IPC is high-cost/fragile; offline oracle only. License MIT (unverified this session) |
| **Kodiak** (NASA) | C++ | interval + Bernstein | **Not an ODE integrator** — computes equilibria/bifurcation sets | Off-target for the ODE slot; at most a Bernstein evaluator for *polynomial* NRA atoms (separate project). NOSA license |

**Takeaway:** the ODE slot offers no clear speed win over CAPD order-20. The credible plays are
(a) **DynIbex** as a cheap IBEX-native fast-path / cross-check (lowest soundness surface because it
rides the arithmetic dReal already links), and (b) **Ariadne/Flow\*** *only* as a targeted tighter
enclosure for the specific stiff `odeexpr` cases the worklogs
([`../odeexpr_v2_forall_perf.md`](../odeexpr_v2_forall_perf.md)) flag intractable — large glue,
justified only if a stiff case is provably enclosure-limited.

---

## Newer contractor / relaxation techniques

Beyond HC4 / ACID / 3BCID / polytope-hull, the adoptable last-~5-yr techniques:

- **`CtcNewton` — interval Newton / Krawczyk / Hansen-Sengupta** (already in the built fork,
  `src/contractor/ibex_CtcNewton.{cpp,h}`, unused by dReal). On locally-square subsystems (n
  equations, n vars) it contracts *quadratically* near a root and can **positively certify** a
  solution box — a completeness win on the equality-heavy portions HC4 contracts only linearly.
  Lowest-friction of everything here: no port, no new dependency, wire a contractor cell selecting
  square subsystems from the `ibex::System` dReal already builds. Sound by construction; the
  square-subsystem selection is a guard, not a fallback. This was the "one contractor IBEX
  ships and dReal has no path to" that [`AUDIT.md`](AUDIT.md) flagged as the sole still-open
  item — now shipped (`--newton`/`--newton-ceil`) and **MEASURED: REFUTED** (zero unique
  solves; the equality-dense github family it was predicted to win got 2.75× *worse*; 11 OOMs
  ~10.5 GB during cell construction on the giant unrollings — all COMPLETENESS-only harm,
  missed refutation/witness within budget, never a false unsat).

- **Affine linearizer + hybrid** — see the deep section above. The single highest-leverage *port*.

- **OBBT (optimization-based bound tightening)** — dReal's `CtcPolytopeHull` already builds the
  joint LP relaxation but takes only its hull; OBBT is the strictly-stronger use of the *same* LP:
  minimize and maximize each variable over the relaxation (2n LPs) to tighten each bound. The
  standard spatial-B&B heavy-hitter (SCIP, BARON). Reuses dReal's `ibex::System` + SoPlex, small
  glue, no new dependency. **Soundness-critical:** each LP-derived bound must be post-verified with
  safe directed-rounding (Neumaier-Shcherbina) — never trusted from the raw simplex optimum, or a
  too-tight bound cuts a real model → false `unsat`. dReal's `UpwardRoundingScope` already models
  this requirement. Cost is the known downside (up to 2n LPs/node); Gleixner et al. 2017's
  filtering / warm-starts / Lagrangian bounds target it. No dReal-corpus number exists (gap).

- **`CtcMohc` — monotonicity-based hull consistency** (Araya/Neveu/Trombettoni). Detects local
  monotonicity of a constraint's function in the current box and computes the *optimal*
  hull-consistent contraction on monotone vars — provably sharper than HC4-Revise / Box on
  multi-occurrence constraints, with **no linearization/LP** (a lighter dependency-problem
  attack than polytope). **Not** in the built fork — lives on `origin/mohc-optim`
  (`src/contractor/ibex_CtcMohc.{cpp,h}`), must be cherry-picked. Drops in as a sibling of the HC4
  revise contractor (`contractor_ibex_fwdbwd.cc`) over the same `ibex::Function`. Sound by
  construction. No NRA-SMT benchmark (gap).

- **Arithmetic-propagation branching (clauseSMT / Improving-NLSAT line)** — the literature's
  "CDCL-guided splitting" for NRA (clauseSMT, Wang arXiv:2406.02122, ASE'25) lives in the
  model-constructing NLSAT/MCSAT world, an *alternative architecture* to ICP, not a contractor for
  it: its feasible-set look-ahead needs a point partial-assignment making a literal *univariate* +
  real-root isolation, neither of which exists in the ICP box loop (`grep 'Sturm|CAD|root-isolat'`
  over `src/dreal` = ∅). Adopting it wholesale = a second theory solver; the SAT-finding *intent* it
  serves is already ICP-native as `--seed-samples`. So the "MCSAT-bound" filing is right *for
  clauseSMT* — but it conflates that architecture with the *branching heuristic*, which **does**
  transfer and is under-served: the genuinely architecture-native VSIDS-analogs come from the CP
  branch-and-prune lineage (ABS / CHS / dom-wdeg), ranked in
  **[Conflict/impact-aware branching](#conflictimpact-aware-branching-the-vsids-analog-dreal-is-missing)**
  below. Soundness/completeness-neutral (branch choice only).

- **GANRA-style GPU candidate generation** — a GPU-parallel analogue of dReal's existing
  `--seed-samples` (multi-start local search + verify). Not a contractor; the realistic fit is
  replacing/augmenting the candidate generator in `src/dreal/solver/seed/seed.cc`, verifying each
  candidate box through the existing prune/`EvaluateBox` (which is what makes it sound —
  completeness-only, can only assert SAT). Heavy CUDA dependency; released-code availability
  unconfirmed. Its benchmark (Sturm-MBO) is SAT-heavy polynomial arithmetic and may not transfer
  to dReal's ODE-dominated corpus.

- **Bernstein-form polynomial contractor** — tight rigorous multivariate-polynomial range bounds
  via the convex-hull/vertex property. Real and sound, but **polynomial-only** — misses dReal's
  transcendental/ODE core, so it would cover at most a sub-fragment while adding whole coefficient
  machinery (exponential blow-up in variable count). Out of scope for dReal's target formulas; the
  honest finding is low relevance.

### Conflict/impact-aware branching (the VSIDS-analog dReal is missing)

`--smear` (IBEX `SmearFunction`, Neveu 2012 — <https://ibex-team.github.io/ibex-lib/>; reimplemented
`brancher_smear.cc`) is dReal's only constraint-aware brancher, and it is **static and
conflict-blind** — a per-box interval-Jacobian sensitivity `Σ|J[i][k]|·diam`
(`brancher_smear.cc:187-218`) recomputed from local geometry each node, with zero memory of what the
search keeps failing on, and it **structurally skips ODE constraints** (`brancher_smear.cc:115`
`if (f.include_ode()) continue`; the `IbexConverter` throws on ODE atoms) — which is exactly why
`smearsum` *collapses* the ODE families corpus-wide (saradc 20→1 solved, ~2× worse; `../CLAUDE.md`).
The missing lever is a **dynamic, history-aware** brancher — SAT's VSIDS (Moskewicz et al., Chaff,
DAC'01) lifted to the ICP split-variable. Two facts make this native, not MCSAT (correcting the
survey's "arithmetic-propagation branching" filing above): the CP-lineage activity/conflict
heuristics were built for the tree-search-with-propagation shape the ICP DFS *already has*, and one
of the two signals they need is **already computed per-prune and thrown away at branch time**. All
are **COMPLETENESS / perf-only** — variable choice can never move a verdict (`brancher.cc:57` bisects
at the midpoint regardless), so zero soundness risk.

> **MEASURED 2026-07 (verdict line; the analysis below is the historical record that
> motivated the arm):** ABS shipped as `--branch abs|absdiam` (+ `--branch-decay`) and swept.
> **The degeneracy hypothesis flagged below was isolated and CONFIRMED** — on v1/github,
> `abs` (=A/diam, degenerates smallest-first) collapses (1.611×/1.865×) while `absdiam`
> (=A·diam, degenerates largest-first) ≈ base; on odeexpr_v2 *both* collapse (2.169×/1.671×),
> i.e. where activity variance exists it actively mis-guides. No family where either variant
> beats base ⇒ **both variants dead on this corpus**; the ABS thesis (covers what smear
> skips) did not materialize. `sweep_20260723_115956/ANALYSIS.md` §H-ABS.

| Idea | Rule | Signal it needs | In dReal today | Verdict / effort |
|---|---|---|---|---|
| **ABS** — Activity-Based Search (Michel & Van Hentenryck, CPAIOR'12, LNCS 7298:228-243; an explicit VSIDS lift) | per-var activity `A(x)`, bump on every prune that shrinks `x`'s domain, decay each node; branch `argmax A/diam` | "which dims shrank this prune" + a decay clock | **signal PRESENT** — `ContractorStatus.output_` (per-`Prune` reduced-dims bitset, `contractor_status.h:94`), set by *every* contractor **including the ODE contractor** (`contractor_odes.cc`). Missing: the `A(x)` vector + decay + interface widening | **top pick** — the one native-signal brancher; **S/M** |
| **Conflict-weighted smear** — CHS / dom-wdeg weights × the smear Jacobian (Habet & Terrioux, J. Heuristics 27:435-471, 2021; Boussemart et al., ECAI'04:146-150) | weight constraints by (decayed) wipeout-participation; per-var score `Σ w(c)·|J[i][k]|·diam` | per-**wipeout** culprit-constraint set + a conflict clock | **signal NOT free** (see below) — needs new per-box instrumentation | higher ceiling, more work; **M/L** |
| **iSAT-style conflict learning + non-chronological backjump** (Fränzle, Herde, Teige, Ratschan, Schubert, JSAT'07) | learn nogoods over interval-bound literals on emptied boxes (1UIP), backjump over irrelevant decisions | a per-bound implication graph across splits | **absent** — dReal learns theory lemmas only at the Boolean level and backtracks chronologically over the box stack | biggest ceiling, **architecture project** |
| dom/wdeg (Boussemart ECAI'04) · IBS (Refalo CP'04, LNCS 3258:557-571) · LSmear (Araya & Neveu, J. Global Optim. 71(3):483-500, 2018) | integer wipeout-degree · realized-search-space-impact · LP-dual constraint weights | culprit set · volume-shrink history · a solved LP per node | culprit-set gap (dom/wdeg); no theory restart (IBS); no objective → LP duals degenerate to a random-objective reweight (LSmear, `ibex_LSmear.cpp:28-31`) | **marginal** each |

**Why clauseSMT itself does not transfer (confirmed).** Its headline "best-on-SAT" number comes from
feasible-set look-ahead + value selection — Def 2.2 requires all-but-one var of a literal pinned to
reals → univariate → real-root isolation; dReal carries an interval box over all vars at once and
bisects the midpoint, so there is no point trail and no root isolation (`grep = ∅`). VSIDS is only
clauseSMT's *tier-3 fallback*, so crediting its numbers to a dReal branching change is a
misattribution. The SAT-finding intent is already covered ICP-natively by `--seed-samples`.

**ABS — top pick, because its signal is genuinely already-computed.** `output_` is a per-`Prune`
bitset of which dims narrowed, populated by every contractor **including the ODE contractor**
(`contractor_odes.cc`) — the one branching signal that is truly "computed and discarded at branch
time." The differentiator vs `--smear`: smear is blind to ODE constraints and collapses on them,
whereas ABS's reward reads `output_`, which ODE prunes *do* set — so ABS is constraint-aware exactly
where smear is not. **The one real risk (HYPOTHESIS, un-isolated):** interval HC4 typically narrows
*many* active dims per prune (unlike finite-domain CP, where propagation sharply cuts a few domains),
so "domain reduced" may fire near-uniformly → low `A(x)` variance → the ranking degenerates toward
`1/diam` = *smallest*-diam-first, the opposite of dReal's proven largest-first default. Isolate this
on the corpus before trusting it.

**The conflict-signal correction (deflates the tempting "it's already there" pitch).** The claim that
the per-wipeout culprit set is already computed and discarded at branch time is **false**.
`AddUsedConstraint` fires on *every* effective prune (`if (changed)`, `contractor_ibex_fwdbwd.cc:144`),
the `ContractorStatus` is created *once per theory call* (`theory_solver.cc:361`) and
`used_constraints_` is **never cleared**, so at any box-empty it holds the cumulative union of every
constraint that narrowed *any* box in the whole DFS ≈ "all active constraints" — not the wipeout
culprit. The witness-filtered `GenerateExplanation` runs *once per theory-UNSAT*
(`theory_solver.cc:370`), not per box-empty (`icp_seq.cc:137` is a bare `continue`), and even it is a
broad variable-connected closure. So any conflict-weighted brancher (CHS / dom-wdeg) needs **new**
per-box snapshot/diff instrumentation — real work, not a read. Its payoff if built: it can weight the
ODE constraints smear cannot score (they *do* land in the used set, `contractor_odes.cc`).

**iSAT — the biggest ceiling, but a project not a brancher swap.** iSAT *is* DPLL+ICP with midpoint
bisection — dReal's exact shape — and its headline win is conflict-driven *learning*: treat each
interval-bound assertion as a trail literal, maintain an implication graph, run 1UIP on an emptied
box, backjump non-chronologically. Strongest measured evidence of anything here, on the matching
architecture (learning ON vs OFF, same solver: bouncing-ball BMC k=10 >348e6 conflicts → 68;
orders-of-magnitude runtime), concentrated on complex-Boolean / BMC instances — i.e. dReal's
saradc/github/tacas. But dReal maintains no cross-split bound-implication graph and backtracks
chronologically over the box stack, so this is an **architecture extension** to `icp_seq.cc`, scoped
as a project, not a one-file change.

**Feasibility (ABS).** Plug-in point: `SmearBrancher` is constructed once per `CheckSat`
(`icp_seq.cc:106`); `Config::Brancher` is a stateless `const std::function` (`config.h:61`, default
`BranchLargestFirst`) receiving only `(box, active_set)` — widen it to carry an `A(x)` vector, add a
decay step, and accumulate `A[i]` for `i ∈ cs.output()` after each `Prune`. Within one `CheckSat` no
persistence is needed; cross-theory-call persistence goes via `Context`/`Config`; `--jobs>1` needs
per-worker thread-locals (mirror the existing per-worker `SmearBrancher`, `icp_parallel.cc:230`).
**Effort S/M** (within-call prototype), **M** (cross-call + parallel). **A/B:** `--branch abs` vs
largest-first vs `--smear smearsum`, on odeexpr (where smear helps) **and** saradc/github/tacas
(where smear collapses — the ABS thesis); PAR2 + solved count, **expect zero verdict flips**
(COMPLETENESS/perf-only).

**Gaps.** (1) No *published* ICP-branching number exists for any of these — all external
evidence is CP/XCSP3 (ABS/CHS/dom-wdeg/IBS) or BMC (iSAT). **The dReal-corpus gap is now
filled for ABS by the 2026-07 sweep** (both variants measured dead — verdict line above);
CHS/dom-wdeg/IBS/iSAT remain unmeasured here, and the ABS result is a strong negative prior
against funding them. (2) Empirical prior:
constraint-aware branching is *not* a global win here — smear collapses the ODE families ~2× — so any
brancher must clear that bar, and branching cannot crack the ∃∀/ODE-UNSAT enclosure wall regardless
(`../CLAUDE.md`). (3) Exact CHS constants (`r = 1/(Conflicts − Conflict(c) + 1)`, `α = 0.4`) are
UNVERIFIED against the primary source (font-encoded PDF) — confirm before implementing. (4) The SAT
core's own VSIDS is *not* reachable: CaDiCaL 3.0.1 exposes no activity getter and connects only as a
`Learner` (`sat_solver.cc`), so a "read the SAT activity" shortcut is blocked — the theory-side
`output_` activity above is the reachable form.

---

## Integration feasibility sketches (top candidates)

### 1. `CtcNewton` — a new square-subsystem contractor cell (Effort **S**)

> **Outcome (2026-07): built as planned (`--newton`/`--newton-ceil`, per-worker Mt cell) —
> and the A/B REFUTED the expected win** (github 2.75× worse, zero unique solves, 11 OOMs;
> zero verdict flips as predicted). Ranked-table stamp has the numbers.

- **Plug-in point:** a new contractor cell mirroring `contractor_ibex_fwdbwd.cc`, constructed from
  the `ibex::System` dReal already builds; wire behind a `--newton` flag (per-worker `*Mt` cell for
  `--jobs>1`, matching the existing `--polytope`/`--forall-pre-prune` pattern).
- **Effort:** small — `CtcNewton` **already compiles** in `dreal-perf-patches`; no port, no new
  dependency. Only glue is selecting locally-square (n eqns × n vars) subsystems.
- **Soundness discharge:** interval Newton/Krawczyk are rigorous contractors → they remove only
  points with no solution; run under the same rounding scope as HC4. The square-subsystem test is a
  guard (skip when not square), not a fallback.
- **Expected win:** faster convergence + certified existence on equality-heavy atoms (BMC / c2e2
  families carry many); COMPLETENESS + search-cost.
- **A/B plan:** `/benchmark-baseline` off vs `--newton` on across all families; watch the
  equality-dense github/tacas families for PAR2 improvement and **zero verdict flips**. Guard the
  ODE families against regression (Newton adds nothing to `forall_t`/`integral` atoms).

### 2. Affine `LinearizerAffine2` beside X-Taylor, then hybrid (Effort **M**)

> **Outcome (2026-07): built as planned (`--polytope-linearizer xtaylor|affine|both`; port +
> fAF2 scope-wrap landed fork-side @ `b5e7a212`, audit `affine-rounding-audit.md`). The A/B
> confirmed the family-specific-default expectation (affine wins odeexpr_v1 0.804×, ODE
> families net-negative) and REFUTED the hybrid≥max(single) prediction.**

- **Plug-in point:** `contractor_ibex_polytope.cc:108-111` — today
  `make_unique<ibex::LinearizerXTaylor>(*system_, RELAX, RANDOM_OPP, HANSEN)` → `CtcPolytopeHull`.
  Add a `--affine` variant constructing `LinearizerAffine2(*system_)`; for the hybrid, feed *both*
  linearizers' cuts into one `CtcPolytopeHull` LP.
- **Effort:** medium — vendor ~12-13 files from `origin/dev_affine_arith` into the IBEX
  `ExternalProject`, fix the verified drift (`LPSolver::get_epsilon()` removed; override the four
  new `FwdAlgorithm` opcodes), wire the waf-plugin subtree into CMake.
- **Soundness discharge:** **the** obligation — audit the affine plugin's rounding scope so `_err`
  stays an over-estimate (run under `NearestRoundingScope` matching the error-free transforms, or
  prove outward interval wrapping dominates); re-check the `// TODO TO CHECK` `add_constraint`
  lines. `CtcPolytopeHull` intersection stays sound by inclusion regardless.
- **Expected win:** tighter polytope contraction on dependency-heavy NRA bodies (the odeexpr NRA
  atoms, saradc); the 2024 result predicts affine+X-Taylor > X-Taylor alone. COMPLETENESS.
- **A/B plan:** three-arm — `--polytope` (X-Taylor, current) vs `--affine` vs `--affine --polytope`
  (hybrid) — on odeexpr_v1/saradc where reuse is heaviest. Metric: PAR2 + solved count, **zero
  flips**. Since AA is net-negative on cheap constraints, expect a family-specific default, not a
  global one (mirrors the `--smear` per-project finding in `../CLAUDE.md`).

### 3. ABS — `--branch abs` activity brancher (Effort **S–M**)

> **Outcome (2026-07): built, swept, dead — the degeneracy hypothesis the sketch said to
> isolate first was isolated and CONFIRMED** (verdict line in the branching section).

The canonical feasibility analysis (plug-in point, effort split, A/B plan, the degeneracy
hypothesis that gates it) lives inline at
**[Conflict/impact-aware branching → "Feasibility (ABS)"](#conflictimpact-aware-branching-the-vsids-analog-dreal-is-missing)**
— not duplicated here. One-line shape: widen `Config::Brancher` (`config.h:61`) to carry a
per-variable activity vector fed from `ContractorStatus.output_` after each `Prune`; branch
`argmax A/diam`; zero soundness surface; the first thing the prototype must isolate is the
low-activity-variance degeneracy hypothesis, on odeexpr **and** the ODE families where the ABS
thesis (covers what smear skips) actually bites.

### 4. OBBT on the existing `CtcPolytopeHull` LP (Effort **M**)

> **Outcome (2026-07): built as planned (`--obbt`, certified-LP bounds only) — dead:
> statistically indistinguishable from polytope (1.026× its PAR2), worse than base on both
> odeexpr families, its 3 unique-vs-base gains all matched or beaten by mohc/affine; and the
> X-Taylor LP path it rides is BUG-014 crash-exposed.**

- **Plug-in point:** a new contractor beside `contractor_ibex_polytope.cc` reusing its
  `ibex::System` + SoPlex; the 2n-LP min/max loop over the same relaxation.
- **Effort:** medium — the pieces exist (`LinearizerXTaylor` + `LPSolver`); the loop is small glue,
  no new dependency. Apply Gleixner et al. filtering/warm-starts to bound the 2n-LP cost.
- **Soundness discharge:** post-verify every LP optimum with Neumaier-Shcherbina safe bounds under
  `UpwardRoundingScope` — the exact discipline `contractor_ibex_polytope.cc:126` already documents
  for the hull. Without it, a too-tight bound is a false-`unsat` SOUNDNESS violation.
- **Expected win:** strictly tighter than hull projection on constraint-coupled boxes; COMPLETENESS
  + fewer splits. Cost/benefit unmeasured on dReal families (gap) — the reason it ranks below the
  cheaper affine/Newton plays.
- **A/B plan:** `--polytope` vs a new `--obbt` on the same LP-favorable families; measure PAR2 and
  the split-count reduction, and confirm the 2n-LP overhead doesn't dominate on low-coupling
  instances.

---

## Paper reading list

For the `/lit-review` skill: a prioritized download list. dReal-relevant levers first, then
ODE-backend comparanda, then background theory.

| Filename | Citation | DOI / URL | Why |
|---|---|---|---|
| `araya_messine_ninin_trombettoni_2024_hybridizing_linear_relaxations.pdf` | Araya, Messine, Ninin, Trombettoni. "Hybridizing two linear relaxation techniques in interval-based solvers." *J. Global Optim.*, 2024. | [10.1007/s10898-024-01449-2](https://doi.org/10.1007/s10898-024-01449-2) · [hal-04812289](https://hal.science/hal-04812289) | **Most decision-relevant.** Head-to-head of the two linearizers dReal must choose between (affine vs X-Taylor) and proof they are complementary. Pull for the real solve-count/CPU numbers (UNVERIFIED here — PDF not loadable). |
| `ninin_messine_hansen_2015_reliable_affine_relaxation.pdf` | Ninin, Messine, Hansen. "A reliable affine relaxation method for global optimization." *4OR*, 13(3):247-277, 2015. | [10.1007/s10288-014-0269-0](https://doi.org/10.1007/s10288-014-0269-0) · [hal-01194735](https://hal.science/hal-01194735) | The ART algorithm behind `LinearizerAffine2`, incl. the rigorous error term. Read to confirm the rounding-safe construction before trusting the port as a sound completeness lever. (Verified figure: 64/74 COCONUT, 32 first-time, 1e-8.) |
| `gleixner_etal_2017_three_enhancements_obbt.pdf` | Gleixner et al. "Three enhancements for optimization-based bound tightening." *J. Global Optim.*, 67:731-757, 2017. | [10.1007/s10898-016-0450-4](https://doi.org/10.1007/s10898-016-0450-4) | How to make the 2n-LP OBBT loop affordable (filtering, warm-starts, Lagrangian bounds). Read before building OBBT on the existing hull LP. |
| `araya_neveu_trombettoni_2010_mohc_adaptive_monotonicity.pdf` | Araya, Neveu, Trombettoni. "Making Adaptive an Interval Constraint Propagation Algorithm Exploiting Monotonicity." *CP 2010*, LNCS 6308. | [10.1007/978-3-642-15396-9_8](https://doi.org/10.1007/978-3-642-15396-9_8) | Defines `CtcMohc`, the monotonicity contractor for the multi-occurrence dependency problem. Read to judge the `origin/mohc-optim` cherry-pick. |
| `araya_trombettoni_neveu_2012_convex_interval_taylor.pdf` | Araya, Trombettoni, Neveu. "A Contractor Based on Convex Interval Taylor." *CPAIOR 2012*, LNCS 7298. | [10.1007/978-3-642-29828-8_1](https://doi.org/10.1007/978-3-642-29828-8_1) | Documents the X-Taylor relaxation that *is* dReal's `LinearizerXTaylor`. Read to confirm the "Taylor-model contractor" is already bound and scope what higher-degree TM arithmetic would add. |
| `de_figueiredo_stolfi_2004_affine_arithmetic_concepts_applications.pdf` | de Figueiredo, Stolfi. "Affine Arithmetic: Concepts and Applications." *Numerical Algorithms*, 37(1-4):147-158, 2004. | [10.1023/B:NUMA.0000049462.70970.b6](https://doi.org/10.1023/B:NUMA.0000049462.70970.b6) | Canonical AA survey: noise-symbol model, dependency-problem reduction, Chebyshev vs MinRange. Grounds the affine theory + the modes seen in the IBEX source. |
| `sandretto_2016_dynibex_validated_rk.pdf` | Alexandre dit Sandretto, Chapoutot. "Validated Explicit and Implicit Runge-Kutta Methods." *Reliable Computing*, 22:78-103, 2016. | [PDF](https://interval.louisiana.edu/reliable-computing-journal/volume-22/reliable-computing-22-pp-078-103.pdf) | DynIbex's method paper **and** the honest verdict: VNODE-LP is state of the art, DynIbex "not competitive on time computation" — the evidence DynIbex is a robustness lever, not a CAPD speed win. |
| `alexandre_chapoutot_2018_runge_kutta_constraint_programming.pdf` | Alexandre dit Sandretto, Chapoutot. "Runge-Kutta Theory and Constraint Programming." arXiv:1804.04847, 2018. | [arXiv:1804.04847](https://arxiv.org/abs/1804.04847) | Full derivation of DynIbex's guaranteed RK schemes + LTE bounding. Table 15 has the only concrete speed figures (all DynIbex-internal). Needed if DynIbex is seriously evaluated as a backend. |
| `kapela_2021_capd_dynsys.pdf` | Kapela, Mrozek, Wilczak, Zgliczyński. "CAPD::DynSys: A flexible C++ toolbox for rigorous numerical analysis of dynamical systems." *CNSNS*, 2021. | [ScienceDirect S1007570420304081](https://www.sciencedirect.com/science/article/abs/pii/S1007570420304081) | Documents the exact backend dReal runs (Cr-Lohner, C0/C1/C2 doubletons); the baseline any alternative must beat on tightness/speed. |
| `nedialkov_2006_vnodelp_manual.pdf` | Nedialkov. "VNODE-LP: A Validated Solver for IVPs in ODEs." Tech. Report CAS-06-06-NN, McMaster, 2006. | [PDF](https://www.cas.mcmaster.ca/~nedialk/vnodelp/doc/vnode.pdf) | Design of the closest C++ sibling to CAPD (Lohner + interval Hermite-Obreschkoff). Judge whether a second Lohner-family backend adds anything over CAPD. |
| `chen_2013_flowstar_cav.pdf` | Chen, Ábrahám, Sankaranarayanan. "Flow\*: An Analyzer for Non-linear Hybrid Systems." *CAV 2013*, LNCS 8044:258-263. | [10.1007/978-3-642-39799-8_18](https://doi.org/10.1007/978-3-642-39799-8_18) | The Taylor-model flowpipe design of the main C++-library ODE alternative; needed to scope Flow\* as an in-process backend. |
| `collins_2023_ariadne_function_calculi.pdf` | Collins, Geretti, et al. "Rigorous Function Calculi in Ariadne." arXiv:2306.17541, 2023. | [arXiv:2306.17541](https://arxiv.org/abs/2306.17541) | Ariadne's Taylor-model function calculus + C++ API — assess it as an embeddable backend for the stiff-nonlinearity cases. |
| `geretti_2021_archcomp_nonlinear.pdf` | Geretti et al. "ARCH-COMP21 Category Report: Continuous and Hybrid Systems with Nonlinear Dynamics." *EPiC Series in Computing*, 2021. | [PDF](https://www.cs.unc.edu/~psd/files/research/2021/geretti-arch-comp21-2021-category-report.pdf) · [EasyChair QD5d](https://easychair.org/publications/paper/QD5d/open) | The only apples-to-apples comparison of CORA/Flow\*/JuliaReach/Ariadne/DynIbex on shared nonlinear benchmarks — the source for their relative accuracy/speed. |
| `kochdumper_2021_sparse_poly_zonotopes.pdf` | Kochdumper, Althoff. "Sparse Polynomial Zonotopes: A Novel Set Representation for Reachability Analysis." *IEEE TAC*, 2021. | [arXiv:1901.01780](https://arxiv.org/pdf/1901.01780) | CORA's enclosure representation — why it can be tighter than CAPD doubletons on non-convex sets, and why it stays out-of-process (MATLAB). |
| `ganra_2026_gpu_llm_nra.pdf` | "Using GPUs And LLMs Can Be Satisfying for Nonlinear Real Arithmetic Problems (GANRA)." arXiv:2603.07764, 2026. | [arXiv:2603.07764](https://arxiv.org/abs/2603.07764) | The GPU-parallel angle. Confirm it is SAT-finding-only (completeness lever) and whether its gradient candidates could feed `--seed-samples`, and whether code is released. |
| `clausesmt_improving_nlsat_2024.pdf` | "clauseSMT: A NLSAT-Based Clause-Level Framework for SMT-NRA (Improving NLSAT for NRA)." arXiv:2406.02122, 2024. | [arXiv:2406.02122](https://arxiv.org/abs/2406.02122) | The "CDCL-guided splitting" NRA literature. Extract the arithmetic-propagation branching heuristic for `--smear`, while confirming the machinery is CAD/MCSAT (not an ICP contractor). |

**Gaps to close by reading:** the 2024 hybrid paper's concrete affine-vs-X-Taylor numbers are
now of historical interest only — the 2026-07 sweep produced the dReal-corpus measurement
directly (affine ⊃ polytope, hybrid refuted), which is the number the decision needed; the
OBBT / MOHC / Newton corpus-number gaps are likewise **filled** (dead / winner-on-odeexpr /
refuted — ranked-table stamps). Still genuinely open: DynIbex / VNODE-LP / JuliaReach licenses,
if the ODE-backend rows are ever pursued.
