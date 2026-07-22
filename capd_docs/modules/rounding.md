# rounding (module)

## What it is
CAPD's directed-rounding control — the mechanism that makes interval arithmetic rigorous. Two
classes plus a rounding-mode enum, in `capd::rounding` (`rounding/DoubleRounding.h`,
`rounding/IntRounding.h`).

## Key API (from group__rounding.html, confirmed in `rounding/DoubleRounding.h`)
- `class DoubleRounding` — switches the FPU rounding mode for `double`/`long double`.
  Static methods: `roundNearest()`, `roundUp()`, `roundDown()`, `roundCut()` (toward zero),
  `RoundingMode test()` (current mode), `bool isWorking()` (self-test that mode switching
  actually takes effect).
- `class IntRounding` — no-op equivalent for integer types (all integer ops are exact).
- `enum RoundingMode { RoundUnknown = -1, RoundNearest, RoundDown, RoundUp, RoundCut }`.
- `capd::rounding::DoubleRounding` is the default `T_Rnd` template argument for `double`/`long
  double` intervals (`intervals/DoubleInterval.h`, `LongDoubleInterval.h`, `lib.h`).

## dReal status
**Used (underlying mechanism), but governed by dReal's own discipline.** dReal does NOT call
`DoubleRounding::roundUp()` etc. directly — it manages the FPU mode at the ICP-phase level via
its own `UpwardRoundingScope` / `NearestRoundingScope` (`docs/rounding.md`). CAPD's interval ops
rely on the ambient mode being correct; CAPD adapters are one of dReal's two sanctioned
"clobberers" (`ExpectClobber`) that may reset the mode, which is why dReal re-establishes
`NearestRoundingScope` around CAPD calls.

## Why it might matter
This is the exact interaction point flagged in the audit: CAPD interval soundness assumes
`FE_UPWARD`-style directed rounding, while CAPD's ODE solver/formatting wants `FE_TONEAREST`.
`DoubleRounding::isWorking()` is a ready-made guard — a one-call check that directed rounding is
actually in effect on this build/arch, which dovetails with dReal's "guard all assumptions"
discipline. The native-CAPD-interval correctness caveat (see `concepts/intervals-and-rounding.md`)
is precisely about this layer under aggressive GCC optimization.

## Source
[group__rounding.html](../../../CAPD/docs/html/group__rounding.html)
