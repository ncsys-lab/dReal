# `CtcOptimShaving` — left-only objective shaving (optimization, not for dReal)

Header:
[`ibex_CtcOptimShaving.h`](../../../../ibex-fork/src/contractor/ibex_CtcOptimShaving.h)
(`contractor/`). `class CtcOptimShaving : public Ctc3BCid`. A specialization of
[`Ctc3BCid`](./Ctc3BCid.md) for **branch-and-bound optimization**: it shaves only
the **objective variable**, and only its **left bound** (the side that can improve
the incumbent), rather than every variable's bounds.

> **dReal status:** **unused (correctly).** It exists to tighten an objective
> bound inside IBEX's `Optimizer`; dReal has no IBEX optimizer (it uses DPLL(T) +
> nlopt). Not a contraction lever for refutation. [`../../AUDIT.md`](../../AUDIT.md).

## Constructor (verbatim)

```cpp
CtcOptimShaving(Ctc& ctc, int s3b=default_s3b, int scid=default_scid,
                int vhandled=-1, double var_min_width=default_var_min_width);
static constexpr int LimitCIDDichotomy = 100;   // vs Ctc3BCid's 16
```

Params are inherited from [`Ctc3BCid`](./Ctc3BCid.md) (`s3b=10`, `scid=1`,
`vhandled=-1`, `var_min_width=1e-11`). The one override is
`LimitCIDDichotomy=100` (Ctc3BCid uses 16) — it switches to dichotomic shaving
above 100 slices instead of 16.

## What differs from `Ctc3BCid` (from `ibex_CtcOptimShaving.cpp`)

- `contract` shaves a **single** variable, `start_var` (the objective in the
  extended system), via `var3BCID(box, var_obj)` — not a loop over variables.
- The overridden `var3BCID_dicho` / `var3BCID_slices` do **left shaving only**:
  they refute slices from the lower bound upward and keep the first non-empty
  slice's left edge. The right bound is never shaved (in minimization, only the
  lower objective bound prunes the search).

## Why dReal skips it

dReal does not run IBEX's optimization loop, so there is no "objective variable"
in an extended system to shave. The general shaving power is
[`CtcAcid`](./CtcAcid.md) / [`Ctc3BCid`](./Ctc3BCid.md) over all variables — which
dReal already **ships** (`--acid`/`--3bcid`, `contractor_ibex_acid.{cc,h}`) — not
this one-variable optimization specialization. Recorded so it isn't mistaken for a
general shaving option.

Related: [`Ctc3BCid.md`](./Ctc3BCid.md), [`CtcAcid.md`](./CtcAcid.md),
[`../../chapters/optim.md`](../../chapters/optim.md).
