# dReal's current IBEX usage — the audit's reference point

> ⚠ **STALE on polytope/ACID/LP status (verified 2026-07-02)** — written pre-commit `fa3b74bd7`.
> IBEX now builds `-DLP_LIB=soplex` (`CMakeLists.txt:189`), so "polytope path is dormant / built with
> no LP solver / `LP_LIB=none`" is **wrong**: `--polytope`, `--acid`, `--3bcid`, `--forall-polytope`
> are all **live** opt-in contractors (default off; `--acid`/`--3bcid` run under `--jobs>1` via a per-worker Mt cell). Only
> the default run is HC4-only; **Newton** is the sole genuinely-absent contractor. Full correction +
> anchors: [`README.md`](README.md) top banner. Trust source + `CMakeLists.txt`, not this page.

What dReal actually binds against in `ncsys-lab/ibex-lib@dreal-perf-patches` (the
fork; not stock IBEX). Every `AUDIT.md` finding is framed as a gap relative to
this. Verified by grepping `src/dreal/` and reading the contractor-binding layer.

> Soundness note (read `docs/soundness-vs-completeness.md`): IBEX is the
> interval/contraction backend. A wrong-rounding or over-tight interval op here
> is a **SOUNDNESS** hazard (false `unsat`); a looser contractor only costs
> **COMPLETENESS** (missed refutation). The fork's soundness patches (#8/#12 gaol,
> #13 atan2) and dReal's `UpwardRoundingScope` exist precisely to protect soundness.

## The thin slice dReal binds

| IBEX surface | dReal symbol(s) | Where | Role |
|---|---|---|---|
| **Interval arithmetic** | `ibex::Interval` (34×), `ibex::IntervalVector` (4×) | throughout `src/dreal/` | the rigorous interval type underlying every Box/contraction |
| **Symbolic functions** | `ibex::Function`, `ExprNode` (37×), `ExprCtr` (28×), `ExprSymbol` (11×), `ExprConstant`, `Array`, `cleanup` | `generic_contractor_generator.cc`, `contractor_ibex_fwdbwd.cc` | builds the DAG for each theory atom, then contracts it |
| **HC4 forward-backward** | `ibex::Function::backward(...)` | `contractor/contractor_ibex_fwdbwd.cc` (+ `_mt`) | **the contraction hot loop** — the patched path; the only *always-on* IBEX contractor |
| **Polytope hull** *(opt-in, live)* | `ibex::CtcPolytopeHull`, `ibex::LinearizerXTaylor` (5×), `ibex::System`, `ibex::SystemFactory`, `ibex::NumConstraint` | `contractor/contractor_ibex_polytope.cc` (+ `_mt`) | LP relaxation; **`--polytope`-gated, default OFF**; LP backend **linked** (`LP_LIB=soplex`) — see below |

**The polytope config is fixed at the library defaults**
(`contractor_ibex_polytope.cc:108`):
`LinearizerXTaylor(system, RELAX, RANDOM_OPP, HANSEN)` → `CtcPolytopeHull(lr)`.
No corner-policy / slope-formula tuning is exposed.

### The polytope path is off by default — but live opt-in (corrected 2026-07-02)

**HC4 is dReal's only *default* IBEX contractor**, but `--polytope` (and the
shaving contractors `--acid`/`--3bcid`) are functional escape hatches:
- `config.use_polytope_` and `use_polytope_in_forall_` default to **`false`**
  (`config.h:330-331`); the `--polytope` / `--forall-polytope` flags
  (`dreal_main.cc:169,175`) must be passed to wire `ContractorIbexPolytope` into
  `generic_contractor_generator.cc:61-122`.
- **Since commit `fa3b74bd7`, dReal builds IBEX with `-DLP_LIB=soplex`**
  (`CMakeLists.txt:189`; vendored SoPlex 4.0.2, `libsoplex.a` linked at `:224`).

`CtcPolytopeHull`'s header requires *"ibex installed with a LP solver
(`-DLP_LIB`)"* — which is now satisfied. So passing `--polytope` **runs the real
2n-LP-solve relaxation** (no longer throws / no-ops). Perf on the main NRA path is
unmeasured; the ∃∀ variant `--forall-polytope` measured *mixed* (helps some
encodings, hurts others: `exists_forall_perf.md:195-197`). *(This section formerly
claimed `LP_LIB=none` / "dormant / unverified to even function" — that was
pre-`fa3b74bd7` and is wrong now.)*

## What dReal hand-rolls instead of using IBEX's version

dReal does **not** use IBEX's `CtcHC4` / `CtcPropag` / `CtcFixPoint` /
`CtcCompo`. It has its own contractor-composition + fixpoint layer:

- `contractor/contractor_worklist_fixpoint.cc`, `contractor_fixpoint.cc` — its own
  agenda/fixpoint loop (the analog of `CtcPropag`).
- `contractor_seq.cc`, `contractor_join.cc` — its own compose/union (analog of
  `CtcCompo`/`CtcUnion`).
- `contractor_forall.h`, `counterexample_refiner.cc` — its own ∃∀ handling
  (analog of `CtcForAll`/`CtcExist`, but CEGIS-style, tied to DPLL(T)).

These exist because dReal's contractors run **inside DPLL(T) on transient search
literals** with SMT-specific bookkeeping (explanations, theory lemmas, the Box
abstraction), which the stock IBEX strategy classes don't model. So "replace with
IBEX's `CtcPropag`" is usually **not** on the table; "borrow an IBEX *atomic*
contractor (ACID/3BCID/Newton) into dReal's existing loop" is.

## What dReal does NOT touch (the opportunity + the correctly-ignored)

- **Shaving contractors** `CtcAcid` / `Ctc3BCid` (shaving / constructive disjunction) are
  **shipped** as opt-in `--acid` / `--3bcid` (`make_contractor_ibex_acid` over the assertion
  system, `theory_solver.cc:240-244`; `contractor_ibex_acid.{cc,h}`; default OFF, parallel-safe
  under `--jobs>1` via a per-worker `ContractorIbexAcidMt` cell, `contractor.cc:215-217`).
  **Genuinely unused** atomic contractors (still opportunity): `CtcNewton`
  (interval Newton), `CtcInverse`.
- **Unavailable** → `LinearizerAffine2` / affine arithmetic: **not in the fork**
  (it lives in the separate `ibex-affine` plugin, which dReal does not build —
  verified: no `*affine*` source in the fork). Any affine-relaxation idea is a
  plugin-integration task, not a drop-in.
- **Correctly ignored** (different problem shape — dReal is an SMT solver, not a
  standalone CSP/NLP solver): the IBEX `Solver`, `Optimizer`, separators (`Sep*`),
  `Set`/paving, the Minibex parser, the COV file format, bisectors + cell buffers
  (dReal branches inside DPLL(T)).

## The fork patches = dReal's IBEX divergence

`../ibex-fork/MIGRATION.md` is the live catalog (de-numberized — don't hardcode a count). Cross-reference before proposing
anything in the same area — several "obvious" levers are **already pulled**, and
the patches' profiling numbers tell you where the hot path actually is.

| # | sha | What | Area | Why it matters to the audit |
|---|---|---|---|---|
| 1 | `f5bf3361` | lazy-init gradient in `Function::init` | Function | ~65% of `init` was eager `Gradient` alloc dReal never used → **gradient is cold**; Newton/Jacobian ideas pay that cost back |
| 2 | `a512418b` | `Function::backward` per-variable callback | Function/HC4 | dReal tracks narrowed vars via callback for theory lemmas |
| 3 | `9a23379c` | parser.yc namespace/include fix | parser | build-only |
| 4 | `ca21f309` | mathlib aarch64 Linux | build | Docker arm64 |
| 5 | `f04f5db5` | backward callback fires for **vector/matrix** args | Function/HC4 | closed a missed-notification gap (soundness for lemma tracking) |
| 6 | `1836b569` | copy old-value in callback (alias fix) | Function/HC4 | correctness of the callback contract |
| 7 | `d2b978b9` | report partial narrowings on `EmptyBoxException` | HC4 | sharper lemmas on the empty-box path |
| 8 | `33eb6676` | gaol `Interval::log`/`pow` soundness | gaol arith | `log([0,0])`, fractional `pow` — **false-`unsat` fixes** |
| 9 | `e0311233` | **inline aarch64 FPCR** rounding fast-path | gaol rounding | **23–44%** of solve time on `odeexpr` — the rounding-toggle hot path is already optimized |
| 10 | `3902fa35` | batch the nearest-rounding window in transcendentals | gaol rounding | halves FPU mode toggles per transcendental |
| 11 | `cc6fb001` | empty domains via **return-status, not exception** | HC4 | `__cxa_throw` was up to **~27%** CPU on throw-dense (UNSAT) benches — eliminated |
| 12 | `9500de6b` | `underflow_saturate` backward targets (dreal/dreal4#321) | gaol arith | subnormal-band backward ops were false-`unsat` — **soundness fix** (accepted δ-completeness tradeoff) |
| 13 | `a507cd10` | guard infinite `x` endpoints in forward `atan2` straddle branch (dreal/dreal4#258) | arith (`ibex_Interval.h`) | `atan2([3,3],[−∞,∞])` collapsed to empty → **false-`unsat` SOUNDNESS fix** (in-pin at `e054af7b`) |

**Reading for the audit:** patches 1,2,5,6,7,11 are all on the `Function`/HC4
backward path — that path is heavily profiled and tuned. Patches 8,9,10,12 are on
gaol interval arithmetic (rounding + soundness), and #13 is an `ibex_Interval.h`
arithmetic soundness fix. So the *micro*-optimization budget of the existing path
is largely spent; the remaining leverage is **algorithmic** — Newton is unshipped,
ACID/3BCID now opt-in (`--acid`/`--3bcid`) — see [AUDIT.md](AUDIT.md).
