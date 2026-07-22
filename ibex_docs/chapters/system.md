# Chapter: Systems

Source: [`system.rst.txt`](../../../ibex-docs/_sources/system.rst.txt) ·
`system.html`. A `System` = a set of `NumConstraint`s sharing one argument list,
optionally a goal + initial box. Many IBEX algorithms require a `System` (not a
loose constraint array).

> **dReal status:** builds `SystemFactory` + `System` to assemble the constraint
> set for `CtcPolytopeHull` (`contractor_ibex_polytope.cc`) **and** for the shipped
> `CtcAcid`/`Ctc3BCid` shaving (`contractor_ibex_acid.cc` — the `System` supplies
> the variable ordering `CtcAcid` needs). Does **not** use the goal,
> `NormalizedSystem`, `ExtendedSystem`, or KKT machinery (those are for the IBEX
> optimizer).

## Fields

`const int nb_var` (total variable count), `const int nb_ctr`, `Function* goal`
(NULL if none), `Function f` (the vector-valued constraint function — rhs moved to
0, sign kept), `IntervalVector box` (initial domain if loaded from file),
`Array<NumConstraint> ctrs`.

## Building (C++)

```cpp
SystemFactory fac;
fac.add_var(x); fac.add_var(y);
fac.add_goal(x+y);            // optional
fac.add_ctr(sqr(x)+sqr(y)<=1);
System sys(fac);
```
The shared arg list is fixed once via `add_var` — this is *why* a `System` is more
than a constraint array, and why `CtcAcid`/`LinearizerXTaylor` take a `System`.

## Transformations (dReal-irrelevant, listed for completeness)

- **Copy** with mode `COPY` / `INEQ_ONLY` / `EQ_ONLY`.
- **`NormalizedSystem`** — rewrite all constraints to `g(x)≤0` (with optional
  ε-thickening of equalities). Used by optimization (Lagrange).
- **`ExtendedSystem`** — turn the goal into a constraint `x+y=__goal__` + the
  goal variable. For the optimizer.
- **Kuhn-Tucker** — generate the KKT first-order system (n+M+R+K+1 vars). For NLP.

These three are why dReal ignores most of `system`: they serve the IBEX
`Optimizer`, which dReal doesn't run.

Class detail: [`../classes/system/System.md`](../classes/system/System.md),
[`SystemFactory.md`](../classes/system/SystemFactory.md).
Related: [constraint](constraint.md).
