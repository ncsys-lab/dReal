# `CtcNewton` — interval Newton contractor (audit D)

Header: [`ibex_CtcNewton.h`](../../../../ibex-fork/src/contractor/ibex_CtcNewton.h)
(`contractor/`). `class CtcNewton : public Ctc`. Wraps the interval-Newton /
Hansen-Sengupta iteration; contracts hard near an isolated solution of a **square**
system, but only once the box is already small.

> **dReal status:** unused. Conditional value — see the cost caveat. [`../../AUDIT.md`](../../AUDIT.md) D.

## Constructors (verbatim)

```cpp
CtcNewton(const Fnc& f, double ceil=default_ceil,
          double prec=default_newton_prec, double ratio=default_gauss_seidel_ratio);
CtcNewton(const Fnc& f, const VarSet& vars,   // Newton on a sub-set of vars
          double ceil=default_ceil,
          double prec=default_newton_prec, double ratio=default_gauss_seidel_ratio);
static constexpr double default_ceil = 0.01;
```

## Parameters

| Param | Default | Meaning |
|---|---|---|
| **`ceil`** | 0.01 | apply Newton only when **every** box component's diameter < `ceil` — avoids computing the Jacobian on wide boxes (where Newton is useless). The gating knob. |
| `prec` | `default_newton_prec` | Newton stopping precision. |
| `ratio` | `default_gauss_seidel_ratio` | the inner Gauss-Seidel ratio. |
| `vars` | NULL = all | restrict Newton to a variable subset (others = parameters). |

## Cost caveat for dReal

- Newton needs the **Jacobian/Hansen matrix** → it reintroduces exactly the
  gradient/derivative build that the fork's lazy-init-gradient patch (`f5bf3361`;
  see the catalog [`MIGRATION.md`](../../../../ibex-fork/MIGRATION.md)) made
  *lazy/cold* because dReal's HC4 path never uses it. So `CtcNewton` is not free on
  top of the current build.
- It shines on **square, solution-isolating** subsystems near convergence — many
  dReal queries (especially ODE/`forall_t`) aren't that shape. Most useful as a
  *late* contractor (small boxes, `ceil` small) layered after HC4, not as a
  general replacement.

Related: [function chapter](../../chapters/function.md) (Jacobian/Hansen),
[contractor chapter](../../chapters/contractor.md).
