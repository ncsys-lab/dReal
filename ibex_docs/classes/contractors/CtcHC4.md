# `CtcHC4` — HC4 propagation (dReal hand-rolls the analog)

Header: [`ibex_CtcHC4.h`](../../../../ibex-fork/src/contractor/ibex_CtcHC4.h)
(`contractor/`). `class CtcHC4 : public CtcPropag`. The textbook **HC4**: build a
forward-backward ([`CtcFwdBwd`](./CtcFwdBwd.md)) contractor per constraint of a
CSP/System, then propagate them to a fixpoint with [`CtcPropag`](./Operators.md).

> **dReal status:** **used — as the shaving sub-contractor.** For the *standalone*
> propagation loop dReal runs the same algorithm through its own agenda/fixpoint
> (`contractor_worklist_fixpoint.cc`) over per-constraint callback-bearing fwd-bwd
> contractors, because it needs SMT bookkeeping (explanations, theory lemmas, the
> Box abstraction) the stock strategy class doesn't model. But the shipped
> `--acid`/`--3bcid` shaving contractors *do* instantiate a stock `ibex::CtcHC4`
> over a freshly-assembled `ibex::System` as their per-slice sub-contractor
> (`contractor_ibex_acid.cc`) — so this class is on a live path.
> See [`../../dreal-ibex-usage.md`](../../dreal-ibex-usage.md).

## Constructors (verbatim)

```cpp
CtcHC4(const Array<NumConstraint>& csp, double ratio=default_ratio, bool incremental=false);
CtcHC4(const System& sys,              double ratio=default_ratio, bool incremental=false);
```

`default_ratio` is **inherited from `CtcPropag` = 0.01** (not redefined here).
Note the `CtcPropag` member-comment drift: it reads "set to 0.1", but the
`static constexpr default_ratio = 0.01` is ground truth — see
[`Operators.md`](./Operators.md) and [COMPARISON](./COMPARISON.md).

| Param | Default | Meaning |
|---|---|---|
| `csp` / `sys` | — | the constraint set; one `CtcFwdBwd` is built per constraint |
| `ratio` | `CtcPropag::default_ratio` = **0.01** | a projection removing `< ratio·diam` of a domain is not re-propagated |
| `incremental` | `false` | start propagation from impacted vars only (when called with an impact mask) |

## Why it matters — two roles

`CtcHC4` is the *reference shape* for dReal's standalone worklist (not a porting
target for that loop). Its live role is different: the shipped shaving layer
([`CtcAcid`](./CtcAcid.md) / [`Ctc3BCid`](./Ctc3BCid.md), `--acid`/`--3bcid`)
wraps a real `CtcHC4` as its per-slice sub-contractor over a fresh `System`, rather
than dReal's callback fwd-bwd — so `CtcHC4`'s own propagation stop-`ratio` (0.01)
governs each shave slice. The remaining cross-check is that same stop-`ratio`
against dReal's worklist ratio — the B1 item in [`../../AUDIT.md`](../../AUDIT.md).

Related: [`Operators.md`](./Operators.md) (the `CtcPropag` base),
[`CtcFwdBwd.md`](./CtcFwdBwd.md) (the per-constraint sub-contractor),
[contractor chapter](../../chapters/contractor.md).
