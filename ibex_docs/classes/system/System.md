# `System` (+ `NumConstraint`) — a constraint set with one shared arg list

> ✅ **Both consumers SHIPPED** (was "(dormant) polytope path" pre-`fa3b74bd7`; corrected 2026-07-02):
> the polytope path is live via `--polytope` (`LP_LIB=soplex`, `CMakeLists.txt`), and `CtcAcid`/
> `Ctc3BCid` are live via `--acid`/`--3bcid`. See [`README.md`](../../README.md) top banner.

Header: [`ibex_System.h`](../../../../ibex-fork/src/system/ibex_System.h)
(`system/`). A set of [`NumConstraint`](#numconstraint)s sharing one argument
list, optionally a goal + initial box. Many IBEX algorithms *require* a `System`
(not a loose constraint array) — including [`CtcAcid`](../contractors/CtcAcid.md)
(shave-variable ordering by the smearsumrel criterion) and
[`LinearizerXTaylor`](../linear/LinearizerXTaylor.md) (the Jacobian).

> **dReal status:** built via [`SystemFactory`](SystemFactory.md) at **three**
> live sites, each assembling its own `System` over the box variables: the
> polytope path (`contractor_ibex_polytope.cc`, `--polytope`), the ACID/3BCID
> shaver (`contractor_ibex_acid.cc`, `--acid`/`--3bcid`), and the smear brancher
> (`brancher_smear.cc`, `--smear`, which reads `System::f_ctrs.jacobian`). All
> shipped — no longer an audit-A opportunity.

## Fields (verbatim from the [system chapter](../../chapters/system.md))

`const int nb_var`, `const int nb_ctr`, `Function* goal` (NULL if none),
`Function f` (vector-valued constraint fn; rhs → 0, sign kept), `IntervalVector
box` (initial domain), `Array<NumConstraint> ctrs`.

## Subclasses (optimizer-only — dReal ignores)

`NormalizedSystem` (all `g(x)≤0`, optional ε-thickening), `ExtendedSystem` (goal →
constraint + goal var), KKT generation (n+M+R+K+1 vars). These serve the IBEX
`Optimizer`, which dReal doesn't run.

## `NumConstraint`

Header: [`ibex_NumConstraint.h`](../../../../ibex-fork/src/function/ibex_NumConstraint.h)
(`function/` module, not `system/`).
`Function& f` + `CmpOp op` ∈ {`LT(<)`, `LEQ(≤)`, `EQ(=)`, `GEQ(≥)`, `GT(>)`}.
Inequalities must be scalar-valued. dReal feeds these into `SystemFactory::add_ctr`.

Related: [system chapter](../../chapters/system.md),
[constraint chapter](../../chapters/constraint.md),
[`SystemFactory.md`](SystemFactory.md).
