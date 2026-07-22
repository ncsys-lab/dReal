# Interval

## What it is
`capd::intervals::Interval<T_Bound, T_Rnd>` — CAPD's template interval type. `T_Bound` is the
endpoint scalar (e.g. `double`), `T_Rnd` the rounding policy (`DoubleRounding` by default for
`double`/`long double`). Aliased as `capd::interval` / `DInterval` for the double instantiation
dReal builds with (`CAPD_INTERVAL_TYPE=NATIVE`).

## Key API (member methods, from `intervals/Interval.h`)
- Construction: `Interval()` (=[0,0]), `Interval(x)`, `Interval(lo,hi)`, copy,
  `Interval("lo","hi")` (string ctor — outward-rounded endpoints: `readDown(lo)`/`readUp(hi)`
  in `Interval_Base.h`, so the enclosure is tightest-safe; width is non-zero **only** for
  non-representable decimals — an exactly-representable value like `"0.5"` gives zero width).
- Endpoints: `leftBound()`, `rightBound()` (scalars); `left()`, `right()` (point intervals);
  `setLeftBound(x)`, `setRightBound(x)`.
- Midpoint/width: `mid()`; free `diam` (rigorous), `width` (non-rigorous).
- Inclusion: `contains(x)`, `contains(iv)`, `containsInInterior(...)`, `subset(iv)`,
  `subsetInterior(iv)`.
- All arithmetic operators + transcendental free functions (see the `intervals` module).
- Caveat: `operator<<` rounds to nearest (not guaranteed to enclose on round-trip);
  `binWrite`/`bitWrite`/`hexWrite` are exact.

## dReal status
**Used (core, indirect).** dReal's referenced `capd::interval` (`dreal-capd-usage.md`); enclosures
flow out of the CAPD ODE solver as intervals and into dReal's Box. dReal calls few methods
directly — most usage is internal to CAPD's stepper.

## Why it might matter
The string constructor and the exact-serialization functions are the rigorous-I/O primitives that
parallel dReal's `to_capd_string` 17-sig-fig feed discipline. The native-backend correctness
caveat under GCC optimization attaches to this exact type — but dReal **mitigates** it by building
CAPD with `-frounding-math` (the cure CAPD's own docs prescribe); see
`concepts/intervals-and-rounding.md` for the verified details. Not a live soundness hole.

## Source
[group__intervals.html](../../../CAPD/docs/html/group__intervals.html)
