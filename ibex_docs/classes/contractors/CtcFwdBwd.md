# `CtcFwdBwd` — forward-backward / HC4Revise (dReal's contraction, reimplemented)

Header: [`ibex_CtcFwdBwd.h`](../../../../ibex-fork/src/contractor/ibex_CtcFwdBwd.h)
(`contractor/`). `class CtcFwdBwd : public Ctc`. The stock wrapper that turns one
constraint into a forward-backward (HC4Revise) contractor.

> **dReal status:** the *algorithm* is dReal's hot loop, but dReal does **not**
> instantiate this class — it calls `Function::backward(y, box, callback)` from
> its own `ContractorIbexFwdbwd` wrapper so it can pass the per-variable
> **callback** (fork patches #2/#5/#6/#7) that `CtcFwdBwd` doesn't expose. So
> `CtcFwdBwd` is the reference for *what* dReal computes, not the code path it
> runs.

## Constructors (verbatim)

```cpp
CtcFwdBwd(const Function& f, CmpOp op=EQ);          // f(x)=0 (default) or f(x)<=0, …
CtcFwdBwd(const Function& f, const Domain& y);       // f(x) in [y]
CtcFwdBwd(const Function& f, const Interval& y);
CtcFwdBwd(const Function& f, const IntervalVector& y);
CtcFwdBwd(const Function& f, const IntervalMatrix& y);
CtcFwdBwd(const NumConstraint& ctr);                 // ctr kept by reference
CtcFwdBwd(const System& sys, int i);                 // ith constraint — uses system cache
```

The `(System, i)` form *"benefits from the system cache"* — the same forward-eval
memoization the [Box Properties](../../chapters/strategy.md) `Bxp` mechanism
generalizes; dReal does its own caching instead.

## Why it matters — the shaving sub-contractor

`CtcFwdBwd` is IBEX's per-constraint atom; the shaving contractors
([`CtcAcid`](CtcAcid.md), [`Ctc3BCid`](Ctc3BCid.md)) call a fwd-bwd sub-contractor
per slice. dReal's shipped `--acid`/`--3bcid` do **not** thread its own
callback-bearing wrapper into that slot — they wrap a **stock `ibex::CtcHC4`** over
a freshly-assembled `ibex::System` (`contractor_ibex_acid.cc`), which is a
`CtcPropag` over one `CtcFwdBwd` per constraint. So the callback path (fork patches
#2/#5/#6/#7) is bypassed *inside* shaving: no per-variable lemma attribution
survives; the wrapper instead records `AddUsedConstraint(formulas_)` coarsely (the
whole formula set marked used on any narrowing). The callback wrapper stays
load-bearing only on dReal's own HC4 worklist, not in the shaving layer.

Related: [function chapter](../../chapters/function.md) (`Function::backward`),
[contractor chapter](../../chapters/contractor.md).
