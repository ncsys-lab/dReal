# `CtcAcid` — adaptive 3BCID shaving (SHIPPED as `--acid`)

> **Shipped.** ACID is live as `--acid` (and 3BCID as `--3bcid`), constructing
> `ibex::CtcAcid`/`ibex::Ctc3BCid` over a stock `CtcHC4` sub-contractor in
> `contractor_ibex_acid.cc` (flags in `dreal_main.cc`; default off, mutually exclusive). It **works
> under `--jobs>1`** — `make_contractor_ibex_acid` dispatches to a per-worker `ContractorIbexAcidMt`
> cell, one `ibex::CtcAcid` per worker thread (`ibex::CtcAcid` keeps mutable adaptivity state, so it
> cannot be shared). Any "dReal doesn't run this / should adopt it" framing below is historical.

Header: [`ibex_CtcAcid.h`](../../../../ibex-fork/src/contractor/ibex_CtcAcid.h)
(`contractor/`). `class CtcAcid : public Ctc3BCid`. The **adaptive** version of
3BCID (Neveu, Trombettoni, Araya 2015) — it auto-tunes *how many* variables to
shave, so the user doesn't pick `vhandled`. **IBEX's own solver enables ACID by
default**, which is why it's the audit's top candidate.

> **dReal status:** **shipped** (`--acid`, default off) — the strongest single
> lever layered onto dReal's HC4 contraction. See [`../../AUDIT.md`](../../AUDIT.md) A.

## Constructors (verbatim from the header)

```cpp
CtcAcid(const System& sys, const BitSet& cid_vars, Ctc& ctc, bool optim=false,
        int s3b=default_s3b, int scid=default_scid,
        double var_min_width=default_var_min_width, double ct_ratio=default_ctratio);
CtcAcid(const System& sys, Ctc& ctc, bool optim=false,            // all variables
        int s3b=default_s3b, int scid=default_scid,
        double var_min_width=default_var_min_width, double ct_ratio=default_ctratio);
static constexpr double default_ctratio = 0.002;
```

- `sys` — a `System`, used to order variables by the *smearsumrel* criterion
  (which vars to shave first). dReal assembles one over the box vars + non-`forall`
  assertions in `ContractorIbexAcid`'s constructor (`contractor_ibex_acid.cc`).
- `ctc` — the **sub-contractor** applied on each slice. dReal passes a stock
  `ibex::CtcHC4` over that same `System` (not its callback-bearing fwd-bwd, so no
  per-variable lemma attribution — `AddUsedConstraint(formulas_)` records the whole
  formula set coarsely on any narrowing). IBEX docs warn *don't* use `Box` here;
  HC4 is the recommended choice.
- `ct_ratio` — the adaptive kernel: keep shaving variables until the average gain
  drops below `ct_ratio`. **Code default `0.002`.** ⚠️ The constructor's doc
  comment says "default value is 0.005" — the `static constexpr` (line 94) is the
  source of truth: **0.002**. (Recorded per source-fidelity; flag on upstream.)
- inherited `s3b`/`scid`/`var_min_width` — see [`Ctc3BCid.md`](Ctc3BCid.md).

## How it adapts (from the header's `contract` doc)

Alternates **tuning phases** (every `factor·nbinitcalls` nodes, for `nbinitcalls`
nodes: try `nbcidvar = min(max(2,2·nbcidvar), 5·nbvar)`, measure per-variable
gain, set `nbcidvar` to where average gain falls below `ct_ratio`) with **running
phases** (apply 3BCID to the first `nbcidvar` smear-ordered variables). So cost
self-regulates to the instance.

## How it's wired (as shipped)

1. **System assembly** — `ContractorIbexAcid` builds an `ibex::System` over the box
   vars + non-`forall` assertions (same assembly as `ContractorIbexPolytope`), then
   `is_dummy_` short-circuits when there are 0 constraints.
2. **Cost profile** — the sub-contractor (stock `CtcHC4`) is called many times per
   box, amplifying the HC4 cost (which the fork already drove down — patches
   #1,#11). Net contraction is **stronger** (fewer search nodes), per-node cost
   **higher** — a classic completeness-vs-time trade. Enable per-project and
   `/benchmark`; the `--s3b` (shave depth) and `--acid-ct-ratio` (adaptive-stop)
   knobs tune it.
3. **Runs inside DPLL(T)** on transient literals — respects dReal's empty-detection
   (`iv.is_empty()`) and the FE_UPWARD rounding contract (`DREAL_ASSERT_ROUNDING`,
   a wrong ambient mode here is a silent false `unsat` — SOUNDNESS). Assembled in
   `theory_solver.cc` via `make_contractor_ibex_acid`, appended after the
   per-constraint contractors so it sees the whole assertion system.

Related: [`Ctc3BCid.md`](Ctc3BCid.md) (the fixed-param parent),
[chapter](../../chapters/contractor.md), [`../../KNOBS.md`](../../KNOBS.md).
