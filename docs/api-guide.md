# C++ API Guide

The `dreal` C++ API lets you build, query, and solve nonlinear arithmetic formulas programmatically without writing SMT2 files.

---

## Headers

```cpp
#include "dreal/api/api.h"           // CheckSatisfiability, Minimize
#include "dreal/solver/config.h"     // Config
#include "dreal/symbolic/symbolic.h" // Variable, Expression, Formula
#include "dreal/util/box.h"          // Box (the solution type)
```

---

## Core Types

### Variable

```cpp
Variable x{"x"};              // real-valued variable (default)
Variable b{"b", Variable::Type::BOOLEAN};
Variable n{"n", Variable::Type::INTEGER};
```

Variables are hash-consed: two `Variable` objects with the same name and type are identical. Do not create two `Variable`s with the same name but different types.

### Expression

```cpp
Expression e = x * x + sin(y) - 2.0;
```

Arithmetic operators (`+`, `-`, `*`, `/`, `pow`, `sin`, `cos`, `tan`, `exp`, `log`, `sqrt`, `abs`, `min`, `max`) are overloaded. Constants are implicitly converted from `double`.

### Formula

```cpp
Formula f = (x * x + y * y <= 1.0) && (x >= 0.0);
Formula g = (x == 2.0) || !f;
```

Comparison operators (`==`, `!=`, `<`, `<=`, `>`, `>=`) produce `Formula`. Logical operators (`&&`, `||`, `!`) compose formulas. `forall({x, y}, f)` produces a universally quantified formula.

### Box

`Box` maps variables to `ibex::Interval` domains. A returned model gives you a witness — an interval box that is guaranteed to contain a satisfying point (up to precision δ).

```cpp
Box model = ...;
ibex::Interval ix = model[x];  // access by Variable
double mid = ix.mid();         // midpoint
double lb = ix.lb();           // lower bound
double ub = ix.ub();           // upper bound
```

---

## Satisfiability Checking

```cpp
// Returns optional<Box> — the model if delta-SAT, nullopt if UNSAT
optional<Box> result = CheckSatisfiability(formula, delta);

if (result) {
    Box model = *result;
    // use model
} else {
    // formula is unsatisfiable
}
```

Alternative using output parameter (avoids optional):

```cpp
Box box;
bool sat = CheckSatisfiability(formula, delta, &box);
```

---

## Configuration

`Config` controls solver behavior:

```cpp
Config config;
config.mutable_precision() = 1e-6;         // delta
config.mutable_produce_models() = true;    // produce witness box
config.mutable_use_polytope() = true;      // enable polytope contractor
config.mutable_use_worklist_fixpoint() = true; // smarter fixpoint iteration
config.mutable_number_of_jobs() = 4;       // parallel ICP threads

optional<Box> result = CheckSatisfiability(formula, config);
```

### Key Config Options

| Option | Default | Description |
|---|---|---|
| `precision` | `0.001` | δ tolerance for delta-completeness |
| `produce_models` | `false` | Whether to return a witness box |
| `use_polytope` | `false` | Add linear relaxation contractor |
| `use_worklist_fixpoint` | `false` | Dependency-aware fixpoint vs. naive |
| `number_of_jobs` | `1` | Parallel ICP worker threads |
| `stack_left_box_first` | `true` | Branching order preference |
| `use_local_optimization` | `false` | NLopt optimization for ∃∀ problems |
| `visualize` | `false` | Dump ODE trajectory JSON |

---

## Minimization

```cpp
Variable x{"x"}, y{"y"};
Expression objective = x * x + y * y;
Formula constraint = (x + y >= 1.0) && (x >= 0) && (y >= 0);

optional<Box> result = Minimize(objective, constraint, 0.001);
if (result) {
    // *result gives a box near the minimum
}
```

`Minimize` uses NLopt internally. It finds a local (not necessarily global) minimum satisfying the constraint, to within the given precision.

---

## Example: Lyapunov Verification

From `examples/check_lyapunov.cc`:

```cpp
// Verify V = (1 - cos(x₁)) + 0.5x₂² is a Lyapunov function for:
//   ẋ₁ = x₂,  ẋ₂ = -sin(x₁)
Variable x1{"x1"}, x2{"x2"};
Config config;
config.mutable_precision() = 1e-5;

// CheckLyapunov (in examples/control.h) calls CheckSatisfiability internally
// looking for a counterexample where V is not decreasing along trajectories.
auto result = CheckLyapunov(
    {x1, x2},               // state variables
    {x2, -sin(x1)},         // dynamics f(x)
    (1 - cos(x1)) + 0.5 * x2 * x2,  // candidate V(x)
    0.001,                  // inner radius (exclude neighborhood of origin)
    M_PI * M_PI,            // outer radius
    config
);
// result is nullopt if V is valid; Box counterexample otherwise
```

---

## Example: Simple Constraint

```cpp
#include "dreal/api/api.h"
#include "dreal/symbolic/symbolic.h"

int main() {
    using namespace dreal;
    Variable x{"x"}, y{"y"};

    // Find x, y such that x² + y² = 1 and x > 0 and y > 0
    Formula f = (x * x + y * y == 1.0) && (x > 0.0) && (y > 0.0);

    auto result = CheckSatisfiability(f, 0.001);
    if (result) {
        const Box& m = *result;
        printf("x ∈ [%g, %g]\n", m[x].lb(), m[x].ub());
        printf("y ∈ [%g, %g]\n", m[y].lb(), m[y].ub());
    }
}
```

---

## Linking

With CMake, link against the `dreal` target built by this project:

```cmake
target_link_libraries(my_app PRIVATE dreal)
target_include_directories(my_app PRIVATE ${CMAKE_SOURCE_DIR}/src)
```

The `dreal` target transitively links IBEX, CAPD, CaDiCaL, fmt, spdlog, and nlopt.

---

## Precision and Delta-Completeness

The precision parameter `δ` controls the tradeoff between speed and answer strength:

- If the solver returns **UNSAT**: the formula is guaranteed unsatisfiable (exact).
- If the solver returns **delta-SAT** with model box `B`: there exists a δ-perturbation of the formula that is satisfied by some point in `B`. In practice this means any point within the returned box is "close to" a satisfying point.

For verification tasks: a delta-SAT counterexample is a real counterexample only if the returned box is small enough (i.e., within the tolerance you care about). Choose `δ` smaller than the smallest geometric feature of your problem.

For synthesis tasks: a delta-SAT witness gives you a good starting point for local refinement.

---

## Thread Safety

`CheckSatisfiability` and `Minimize` are **not** thread-safe in the sense that you should not call them concurrently on formulas that share `Variable` objects. Each call creates its own `Context`, `TheorySolver`, and IBEX/CAPD state internally.

Within a single call, `Config::number_of_jobs > 1` uses parallel ICP safely via the thread pool in `icp_parallel.cc`.

---

## Context API (Advanced)

For monotonic incremental solving (assert → check → assert → check, no retraction), use
`Context` directly:

```cpp
#include "dreal/solver/context.h"

Context ctx{config};
ctx.DeclareVariable(x, 0.0, 10.0);  // x ∈ [0, 10]
ctx.Assert(x * x <= 4.0);
auto result = ctx.CheckSat();        // delta-SAT with x ∈ [0, 2.0]
ctx.Assert(x >= 1.9);
auto result2 = ctx.CheckSat();       // delta-SAT with x ∈ [1.9, 2.0]
```

This is the same interface used by the SMT2 parser for `(assert)` / `(check-sat)` commands.

**`Push`/`Pop` are formally unsupported and always throw** — the CaDiCaL SAT core cannot
retract clauses, and learned theory lemmas are box-relative. Retraction is an encoder-side
concern: build a fresh `Context` (or emit a fresh script) per query. Rationale and notes for
any future re-attempt: `docs/decisions.md` §"SMT-LIB push/pop: formally unsupported".
