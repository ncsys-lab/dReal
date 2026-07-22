# `CtcLinearRelax` — turnkey X-Taylor LP relaxation (LP backend now linked)

Header:
[`ibex_CtcLinearRelax.h`](../../../../ibex-fork/src/contractor/ibex_CtcLinearRelax.h)
(`contractor/`). `class CtcLinearRelax : public CtcPolytopeHull`. A convenience
subclass of [`CtcPolytopeHull`](./CtcPolytopeHull.md) that builds its own
`LinearizerXTaylor` over an `ExtendedSystem` — i.e. the polytope-hull contractor
pre-wired with the X-Taylor relaxation, no `Linearizer` to pass.

> **dReal status:** **not wired into dReal** — dReal reaches the polytope relaxation
> via `--polytope`/`CtcPolytopeHull` directly (with its own `LinearizerXTaylor`), not
> this turnkey wrapper. The LP backend the `2n` Simplex calls need **is** linked
> (`-DLP_LIB=soplex`, `CMakeLists.txt`). See [`../../dreal-ibex-usage.md`](../../dreal-ibex-usage.md).

## Constructor (verbatim)

```cpp
CtcLinearRelax(const ExtendedSystem& sys);
```

The *only* argument is an `ExtendedSystem`. Internally (`ibex_CtcLinearRelax.cpp`):

```cpp
CtcLinearRelax::CtcLinearRelax(const ExtendedSystem& sys)
  : CtcPolytopeHull(*new LinearizerXTaylor(sys)), sys(sys) { }
```

So it inherits the [`CtcPolytopeHull`](./CtcPolytopeHull.md) LP defaults
(`max_iter=100`, `time_out=100`s, `eps=1e-9`) and a **default-configured**
`LinearizerXTaylor` — no corner-policy / slope-formula choice is exposed at this
constructor (cf. dReal's shipped `--polytope` path, which *does* pick
`RELAX, RANDOM_OPP, HANSEN`). `contract` just calls `CtcPolytopeHull::contract`;
the commented-out `BxpLinearRelaxArgMin` line-search is disabled in the `.cpp`
(*"seems not interesting"*).

## Why dReal doesn't use it

This is the "easy button" for the X-Taylor polytope path, but it buys *less
tunability* than constructing `CtcPolytopeHull` + `LinearizerXTaylor` by hand: the
constructor hides the corner-policy / slope-formula choice. dReal's shipped
`--polytope` path takes the explicit construction precisely to pin
`RELAX, RANDOM_OPP, HANSEN` (`contractor_ibex_polytope.cc`), so `CtcLinearRelax`
stays unused. With `LP_LIB=soplex` linked, either form would run — the choice is
knob access, not build blockers.

Related: [`CtcPolytopeHull.md`](./CtcPolytopeHull.md),
[`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md),
[`../../AUDIT.md`](../../AUDIT.md) D2.
