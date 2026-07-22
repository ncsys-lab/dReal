# Intervals and rounding (concept)

## What it is
How CAPD makes floating-point computation rigorous: the interval type
`Interval<T_Bound, T_Rnd>` carries a *bound type* (the endpoint scalar) and a *rounding policy*
(`T_Rnd`) that switches the FPU directed-rounding mode so every operation rounds endpoints
outward, guaranteeing the result encloses all true values. For `double`/`long double` endpoints
the default policy is `capd::rounding::DoubleRounding`; for integers it is the no-op
`IntRounding` (integer ops are exact).

## NATIVE vs. alternative backends
`capdlib.h`'s `capd::interval` / `DInterval` is the *default* double interval, selected at CAPD
configure time. There are (at least) three backends:
- **NATIVE CAPD intervals** (`CAPD_INTERVAL_TYPE=NATIVE`, dReal's choice) — the general
  template with `double` endpoints + `DoubleRounding`.
- **FILIB** wrapper (`capd::filib::Interval`) — a fast interval library with the same interface.
- **Multiprecision** (`MpInterval`, `MpFloat` endpoints) — the `mpcapdlib` instantiations.

**Caveat — and why dReal is already covered (from example_intervals.html +
verified build flags):** the CAPD docs say *"For the moment we recommend FILIB
intervals because GCC optimization of the native CAPD intervals with build-in
floating point types can produce not correct results."* The mechanism: aggressive
optimization can reorder/contract FP ops so the directed-rounding switch no longer
brackets the operation it was meant to. **However, the same CAPD docs state the
cure — `-frounding-math` — and dReal builds CAPD with exactly that flag**
(verified: `gcc_build/capd_ep/src/capd_external/CMakeLists.txt` →
`-O2 -frounding-math`; dReal's own `CMakeLists.txt` applies it to dReal source too; `user_programs.dox`:
*"without option -frounding-math compiler can optimize code so that it is not
rigorous anymore"*). So NATIVE here is **not** a live soundness hole — it is
mitigated by the build. `DoubleRounding::isWorking()` remains worthwhile as a
cheap startup self-test that would catch a *future* build regression dropping the
flag (a guard, not a fix for a present bug).

## Soundness details worth carrying
- `DInterval("2.5","3.0")` is the rigorous way to enclose a non-representable decimal; a literal
  `DInterval(0.1,0.1)` does NOT contain 0.1 (the compiler rounds the literal first).
- `operator<<` rounds endpoints to nearest and is **not** guaranteed to enclose on round-trip;
  `binWrite`/`bitWrite`/`hexWrite` round-trip exactly. (dReal prints at 17 sig figs for the same
  reason — `docs/decisions.md` "ODE feed faithfulness".)
- Division by an interval containing 0 throws; `nonnegativePart` throws if empty.

## dReal relevance
dReal does **not** drive CAPD's rounding policy directly. It manages the FPU mode at ICP-phase
granularity via its own `UpwardRoundingScope`/`NearestRoundingScope` (`docs/rounding.md`), and
treats CAPD adapters as sanctioned mode-clobberers (`ExpectClobber`). The interaction to watch:
CAPD interval ops need directed rounding to be sound, while CAPD's ODE stepper/formatting wants
round-to-nearest — dReal re-establishes the right scope around CAPD calls. The NATIVE-vs-FILIB
caveat above is **mitigated** by dReal's `-frounding-math` build (see the caveat box); the residual
action is the cheap `isWorking()` startup guard, not a backend switch.

## Source
[example_intervals.html](../../../CAPD/docs/html/example_intervals.html),
[group__rounding.html](../../../CAPD/docs/html/group__rounding.html)
