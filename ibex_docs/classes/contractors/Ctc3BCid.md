# `Ctc3BCid` — 3B shaving + CID constructive disjunction (audit A)

Header: [`ibex_Ctc3BCid.h`](../../../../ibex-fork/src/contractor/ibex_Ctc3BCid.h)
(`contractor/`). `class Ctc3BCid : public Ctc`. 3BCID (Trombettoni & Chabert,
CP 2007): for each variable, *shave* its bounds (3B) then *constructive-disjoin*
the remaining interval (CID). The fixed-parameter parent of
[`CtcAcid`](CtcAcid.md).

> **dReal status:** **shipped** — `--3bcid` (default off, mutually exclusive with
> `--acid`). `ContractorIbexAcid` builds `ibex::Ctc3BCid(hc4_sub, --s3b)` over a stock
> `CtcHC4` when `use_3bcid()` (`contractor_ibex_acid.cc`); works under `--jobs>1` via
> the per-worker `ContractorIbexAcidMt` cell. Use it for the shaving power without
> ACID's adaptivity (e.g. to pin `--s3b` for a sweep). See [`../../AUDIT.md`](../../AUDIT.md) A.

## Constructors (verbatim)

```cpp
Ctc3BCid(const BitSet& cid_vars, Ctc& ctc, int s3b=default_s3b, int scid=default_scid,
         int vhandled=-1, double var_min_width=default_var_min_width);
Ctc3BCid(Ctc& ctc, int s3b=default_s3b, int scid=default_scid,    // all variables
         int vhandled=-1, double var_min_width=default_var_min_width);
static constexpr int    default_s3b           = 10;
static constexpr int    default_scid          = 1;
static constexpr double default_var_min_width = 1.e-11;
static constexpr int    LimitCIDDichotomy     = 16;   // >16 slices ⇒ dichotomic shave
```

## Parameters (from the header's own guidance)

| Param | Default | Header guidance |
|---|---|---|
| **`s3b`** | 10 | max slices the shaving may eliminate (inverse of the standard "3B" param). *"Often the most significant impact on performance and should be tuned in priority."* Best range **5–200**. |
| `scid` | 1 | slices for the CID constructive-disjunction hull. 0 = plain shaving. *"User should generally **not** play with this parameter."* |
| `vhandled` | −1 (all) | number of `var3BCID` calls per `contract` = how many variables get shaved. (ACID computes this adaptively instead.) |
| `var_min_width` | 1e-11 | a variable narrower than this isn't shaved; bounds `actual_s3b = min(s3b, floor(diam/var_min_width))`. *"Generally not tuned in priority."* |
| `cid_vars` | — | bitset mask of which variables to shave. |

`ctc` is the sub-contractor applied per slice (HC4 recommended; **not** `Box`).
Shaving is dichotomic above `LimitCIDDichotomy=16` slices, linear below.

Related: [`CtcAcid.md`](CtcAcid.md), [chapter](../../chapters/contractor.md),
[`../../KNOBS.md`](../../KNOBS.md).
