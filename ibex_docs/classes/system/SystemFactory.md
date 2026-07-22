# `SystemFactory` — programmatic `System` builder

Header:
[`ibex_SystemFactory.h`](../../../../ibex-fork/src/system/ibex_SystemFactory.h)
(`system/`). The temporary builder for a [`System`](System.md): declare the shared
argument list once, then add the goal/constraints.

> **dReal status:** used at three live sites, each running the same `add_var`
> (all box vars) + `add_ctr` (non-`forall` constraints) assembly:
> `contractor_ibex_polytope.cc` (polytope hull, `--polytope`),
> `contractor_ibex_acid.cc` ([`CtcAcid`](../contractors/CtcAcid.md)/`Ctc3BCid`
> shaving, `--acid`/`--3bcid`), and `brancher_smear.cc` (smear Jacobian,
> `--smear`). The CtcAcid integration is **shipped**, not an audit-A opportunity.

## Usage (from the [system chapter](../../chapters/system.md))

```cpp
SystemFactory fac;
fac.add_var(x);  fac.add_var(y);   // the shared argument list — declared once
fac.add_goal(x+y);                 // optional
fac.add_ctr(sqr(x)+sqr(y)<=1);     // a NumConstraint
System sys(fac);
```

The whole point: the goal and every constraint **share** the arg list fixed by
`add_var`, which is why a `System` is more than a constraint array and why the
algorithms that need cross-constraint variable knowledge (ACID's smear ordering,
the linearizer) take a `System`.

Related: [`System.md`](System.md), [system chapter](../../chapters/system.md).
