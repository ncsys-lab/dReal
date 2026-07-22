# Chapter: Interval Computations

Source: [`interval.rst.txt`](../../../ibex-docs/_sources/interval.rst.txt) ·
`interval.html`. The basic rigorous data types and the arithmetic dReal's whole
contraction layer is built on.

> **dReal status:** `Interval` and `IntervalVector` are used pervasively. The
> backward (relational) atoms below are exactly what `HC4Revise` composes, and
> are where the fork's gaol soundness patches (#8 log/pow, #12 underflow) live.
> Soundness here = false-`unsat` risk; see [`../dreal-ibex-usage.md`](../dreal-ibex-usage.md).

## The types (no inheritance — flat, for speed)

`Interval`, `IntervalVector` (a "box"), `IntervalMatrix`, `IntervalMatrixArray`;
real counterparts `double`, `Vector`, `Matrix`, `MatrixArray`. An `Interval` is
**not** a 1-D `IntervalVector` (deliberate, for efficiency). The empty set is
*typed* and *sized* (an empty 3-vector ≠ empty 4-vector). Never let an interval
contain NaN.

Constants: `Interval::pi()`, `two_pi()`, `half_pi()`, `empty_set()`,
`all_reals()`, `zero()`, `one()`, `pos_reals()`, `neg_reals()`.

## Geometric / inspection ops (the `safe_mid`/`safe_diam` cousins)

`x.lb() x.ub() x.diam() x.rad() x.mid() x.mig() x.mag()`, `x.inflate(eps)`,
`distance(x,y)`, `x.is_bisectable()`. For boxes: `x.max_diam() x.min_diam()
x.volume() x.extr_diam_index(b) x.sort_indices(b,tab) x.is_flat()`.

> dReal **wraps** `.mid()`/`.diam()` as `safe_mid`/`safe_diam` (rounding hygiene —
> `docs/rounding.md`); raw use is lint-forbidden in `src/dreal/`. `bisect(i,ratio)`
> splits a box (ratio 0.5 = midpoint) — dReal branches itself, doesn't call this.

## Forward arithmetic

Operators `+ - * /` on scalars/vectors/matrices (dot `v*w`, `hadamard_product`,
`outer_product`, matrix products). Nonlinear: `sqr sqrt pow root exp log
cos sin tan acos asin atan cosh sinh tanh acosh asinh atanh atan2 abs min max
sign`. Division is generalized: `[2,3]/[-1,2]` is a union → `/` returns its hull;
`div2(x,y,out1,out2)` / `div2_inter(...)` expose both pieces.

> **All arithmetic is "naive" (un-optimized) per the docs** — no Intlab-style
> tightening. The rigor comes from directed rounding, which the fork's gaol
> patches (#9 inline FPCR, #10 batched windows) make cheap. This is *the* reason
> dReal's interval ops are sound only under `UpwardRoundingScope` (FE_UPWARD).

> **atan2 unbounded-endpoint soundness** (fixed in-fork, `a507cd10`; dreal/dreal4#258):
> forward `atan2(y,x)`'s x-straddles-zero branch divided by an infinite `x` endpoint
> with no guard (its `y.ub()≤0` sibling had them), so `atan2([3,3],[−∞,∞])` collapsed
> the forward image to the **empty set** → false `unsat` — **SOUNDNESS** (asserts
> T-unsat on a T-sat φ). The fork guards ±∞ endpoints (x→+∞ contributes angle 0,
> x→−∞ contributes π), matching the sibling branch; bounded args are bit-identical.
> Any SMT-LIB2 generator emitting `arctan2` with an unbounded argument relied on this.

## Backward (relational) arithmetic — the HC4 atoms

`bwd_add(y,x1,x2)`, `bwd_mul`, `bwd_sqr`, `bwd_pow`, `bwd_exp`, `bwd_log`,
`bwd_cos`, `bwd_sin`, … each contracts the *pre-image* args in place to enforce
`y=f(x)`. `Function::backward` (HC4Revise) chains these from the DAG root to the
leaves — **this is dReal's contraction hot loop**, and where:
- fork #8 fixed `bwd`-relevant `log([0,0])` / fractional `pow` (false-`unsat`),
- fork #12 added `underflow_saturate` to `bwd_pow/exp/sqr/mul/div` (subnormal-band
  false-`unsat`, dreal/dreal4#321).

## Inner arithmetic (`iadd`, `ibwd_*`) — dReal does not use

`]f[` operators that *under*-approximate (inclusion reversed): used to grow a
proven-feasible inner box (inflation), the basis of inner-region / feasible-set
methods. Not implemented for all functions. Relevant to optimization / set
characterization, **not** to dReal's refutation-oriented contraction — listed in
[`../AUDIT.md`](../AUDIT.md) E (correctly ignored).

Class detail: [`../classes/arithmetic/Interval.md`](../classes/arithmetic/Interval.md),
[`IntervalVector.md`](../classes/arithmetic/IntervalVector.md).
