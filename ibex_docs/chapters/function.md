# Chapter: Functions

Source: [`function.rst.txt`](../../../ibex-docs/_sources/function.rst.txt) ·
`function.html`. The symbolic `Function` (a DAG over an arithmetic expression)
and the numerical operations on it.

> **dReal status:** central. `generic_contractor_generator.cc` builds an
> `ibex::Function` (via `ExprNode`/`ExprSymbol`/`ExprConstant`) per theory atom;
> `contractor_ibex_fwdbwd.cc` calls `Function::backward` to contract. **Several
> fork patches touch this path** (gradient laziness, the backward callback
> contract, the exception→return-status refactor) — the live catalog is
> [`../ibex-fork/MIGRATION.md`](../../../ibex-fork/MIGRATION.md). See
> [`../dreal-ibex-usage.md`](../dreal-ibex-usage.md).

## Arguments vs variables

An **argument** is a formal symbol (`ExprSymbol`, sugar class `Variable`) that may
be scalar/vector/matrix; a **variable** is one real component. `f:(x∈R²,y∈R³)↦…`
has 2 arguments / 5 variables. Every numerical call flattens to a single box of
the 5 variables in declaration order. Up to 20 args inline; more via
`Array<const ExprSymbol>`.

## Numerical operations on a `Function`

| Call | Returns | dReal | Notes |
|---|---|---|---|
| `f.eval(box)` / `eval_vector` / `eval_matrix` | scalar/vec/mat interval | indirect | forward interval evaluation |
| **`f.backward(y, box[, callback])`** | contracts `box` in place | **used (hot loop)** | HC4Revise; optimal when each var occurs once. The **callback** `(var_idx, before, after)` is fork patch #2/#5/#6/#7 |
| `f.gradient(box[, g])` | gradient vector | **cold** | the alloc fork #1 made lazy — Newton/Jacobian ideas pay it back |
| `f.jacobian(box[, J])` | interval Jacobian | no | used by interval Newton |
| Hansen matrix | thinner slope matrix | no | sharper than Jacobian, slower; inside `CtcNewton` |
| copy with `Function::DIFF` | symbolic derivative | no | symbolic differentiation |

## Building functions (DAGs)

C++ operator overloading or the [Minibex](minibex.md) language. **Share
subexpressions** with `const ExprNode&` references to build a DAG, not a tree —
"a gain in performance" (the standard mitigation for the dependency problem at the
expression level). Allowed symbols: `sign min max sqr sqrt exp log pow cos sin
tan acos asin atan cosh sinh tanh acosh asinh atanh atan2` — no `^` operator.

## Why this matters for the audit

- The **backward callback** (#2,#5,#6,#7) is dReal-specific machinery for tracking
  narrowed variables → theory lemmas. Any new IBEX contractor dReal adopts must
  respect/feed this, or lemmas degrade.
- **Gradient is lazy/cold** (#1): a `CtcNewton`/Jacobian-based idea reintroduces
  the gradient build it removed — factor that into the cost (audit D).
- The exception→return-status refactor (#11) means "domain emptied" is now a
  `bool`, not a throw — relevant if borrowing IBEX contractors that still expect
  the old `EmptyBoxException` path (they were updated in-fork; check on rebase).

Class detail: [`../classes/function/Function.md`](../classes/function/Function.md).
Related: [constraint](constraint.md) (wraps a `Function` + comparison op),
[system](system.md) (a vector of constraints).
