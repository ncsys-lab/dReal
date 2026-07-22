# intervals (module)

## What it is
The CAPD interval-arithmetic module: the template class `capd::intervals::Interval<T_Bound, T_Rnd>`
plus a large set of free functions implementing rigorous (outward-rounded) arithmetic and
elementary transcendental functions. With `CAPD_INTERVAL_TYPE=NATIVE`, dReal uses the
`double`-endpoint instantiation aliased as `capd::interval` / `DInterval`.

## Key API (from group__intervals.html, confirmed in `intervals/Interval.h`)
- Rigorous arithmetic operators: `+ - * / ^`, plus mixed `Interval op T_Bound` (`add`,
  `substract`, `multiply`, `divide`). Division by an interval containing 0 throws.
- Elementary functions, each returning a rigorous enclosure: `sqrt`, `power(x,int)`,
  `power(x,interval)`, `sin`, `cos`, `tan`, `cot`, `asin`, `acos`, `atan`, `atan2`,
  `sinh`, `cosh`, `tanh`, `coth`, `exp`, `log`, `sqr`.
- Endpoint access: `left`/`right` (point interval), `leftBound`/`rightBound` (scalar),
  member `setLeftBound`/`setRightBound`.
- Set ops: `intersection(a,b,&out)→bool`, `intervalHull(a,b)`, `iabs`, `imax`, `imin`,
  member `contains`, `containsInInterior`, `subset`, `subsetInterior`.
- Width/midpoint: `diam` (rigorous upper bound on diameter), `width` (non-rigorous), `mid`,
  `split`, `isSingular`.
- Predicates: `isinf`, `isnan`; `nonnegativePart` (throws if empty).
- Exact serialization: `binWrite`/`binRead`, `bitWrite`/`bitRead`, `hexWrite`/`hexRead`
  (round-trip without overestimation; plain `operator<<`/`>>` rounds to nearest and is
  NOT guaranteed to enclose).

## dReal status
**Used (core).** dReal consumes `capd::interval` indirectly through `IVector`/`IMap` enclosures
produced by the CAPD ODE solver; see `dreal-capd-usage.md` (build flag `CAPD_INTERVAL_TYPE=NATIVE`).
dReal does its own contraction in ibex, so most of these free functions are exercised inside CAPD's
Taylor stepper rather than called directly by dReal. In particular the `atan2` above is CAPD's
stepper-internal enclosure; the atan2 *contractor* dReal drives — and the unbounded-argument
false-`unsat` fix (dreal/dreal4#258, ibex-fork `a507cd10`, in-pin `e054af7b`) — live in IBEX, not
this module (SOUNDNESS: it had asserted φ T-unsatisfiable on a T-satisfiable φ; now fixed).

## Why it might matter
The string constructor `DInterval("2.5","3.0")` is the rigorous way to enclose a
non-representable decimal — relevant to dReal's `to_capd_string` feed-faithfulness discipline
(`docs/decisions.md` "ODE feed faithfulness"). The non-rigorous `operator<<` enclosure caveat
is exactly the soundness trap dReal's 17-sig-fig printing avoids.

## Source
[group__intervals.html](../../../CAPD/docs/html/group__intervals.html)
