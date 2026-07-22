# `Interval` — the rigorous scalar interval

Header: [`ibex_Interval.h`](../../../../ibex-fork/src/arithmetic/ibex_Interval.h)
(`arithmetic/`). Wraps the underlying interval library (**gaol** in dReal's build).
The atom of every rigorous computation.

> **dReal status:** used everywhere. Its directed-rounding correctness is *the*
> soundness foundation; the fork's gaol/arith patches harden this type — gaol
> `log([0,0])` / fractional `pow`, ARM64 rounding-mode fast-paths, subnormal
> `underflow_saturate`, and the forward `atan2` infinite-endpoint guard
> (dreal/dreal4#258) — see the live catalog
> [`MIGRATION.md`](../../../../ibex-fork/MIGRATION.md) (don't hardcode a patch
> count; it drifts). The #258 `atan2` and the `underflow` fixes were both
> false-`unsat` SOUNDNESS bugs (a box narrowed past a true model → asserts
> T-unsat on a T-sat φ). dReal wraps `.mid()`/`.diam()` as `safe_mid`/`safe_diam`
> (raw use lint-forbidden — `docs/rounding.md`).

## API surface (from the [interval chapter](../../chapters/interval.md))

- **Constants:** `pi() two_pi() half_pi() empty_set() all_reals() zero() one()
  pos_reals() neg_reals()`.
- **Inspection:** `lb() ub() diam() rad() mid() mig() mag()`, `inflate(eps)`,
  `is_empty() is_bisectable() is_unbounded()`, `distance(x,y)`, `rel_distance`.
- **Forward arith:** `+ - * /`, `sqr sqrt pow root exp log` and the full
  trig/hyperbolic set incl. `atan2` (its x-straddles-zero branch now yields
  `Interval::zero()`/`pi()` for an infinite `x` endpoint instead of emptying the
  image — the #258 fix, `ibex_Interval.h:1029-1051`); generalized division via
  `div2`/`div2_inter`. dReal reaches this via `IbexConverter::VisitAtan2`.
- **Set ops:** `& | &= |= == !=`, `is_subset`, `is_interior_subset`,
  `intersects`, `is_disjoint`, `complementary`.
- **Backward (relational) atoms:** `bwd_add bwd_mul bwd_sqr bwd_pow bwd_exp
  bwd_log bwd_cos …` — the in-place contractors `Function::backward` chains.
  These are where soundness bugs bite: the gaol `log([0,0])` / fractional-`pow`
  fix and the subnormal `underflow_saturate` on `bwd_pow/exp/sqr/mul/div` (both
  guarded false-`unsat` SOUNDNESS holes — see `MIGRATION.md`).
- **Inner arith:** `iadd … ibwd_*` (under-approximating; dReal does not use).

> **All ops are "naive"** (no Intlab-style tightening) per the docs — tightness
> comes from rounding direction, which is why every interval op is sound only
> under dReal's `UpwardRoundingScope` (FE_UPWARD).

Related: [`IntervalVector.md`](IntervalVector.md),
[interval chapter](../../chapters/interval.md), `../../KNOBS.md` (the gaol build flag).
