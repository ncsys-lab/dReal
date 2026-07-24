# Writing fast dReal formulas — a working intuition for the solver

You are writing a program that **emits** SMT-LIB2 for dReal to solve. This guide is not a list of
tricks to pattern-match against. It gives you a **mental model of what dReal does to your formula**,
so that when you generate a constraint you can *see* whether dReal will handle it well and reshape it
if not — including for expressions this guide never anticipated.

It is self-contained; source citations are collected in the appendix, and every number below was
measured on a stock build. There is really only one idea to internalize, in §2. Everything else is
you reasoning from it.

---

## 1. The two operations: contract and bisect

dReal is a **δ-complete** solver: it either proves your formula unsatisfiable, or finds a point
satisfying a δ-relaxation of it (each constraint slackened by at most a precision δ you choose). It
works over **interval boxes** — every real variable is an interval, like `x ∈ [−1000, 1000]`, and the
box is the Cartesian product of them. It does exactly two things to a box, over and over:

- **Contract** — use a constraint to shrink the intervals, throwing away sub-regions that provably
  contain no solution. Cheap, exact, and it never splits anything.
- **Bisect** — when contraction stalls with the box still too wide, cut one interval in half and
  recurse into both halves. This is the expensive move: splitting *d* variables builds a search tree
  that is **exponential in *d***.

The search ends when contraction alone squeezes a box to satisfy every constraint within δ
(→ `delta-sat`), or when every box has been contracted to empty (→ `unsat`).

> **The entire performance question is: how much can contraction do before dReal is forced to
> bisect?** A formula dReal solves instantly is one contraction can carry; a formula that hangs is one
> where contraction gives up early and the bisection tree explodes. So the rest of this guide is about
> one thing — **what makes contraction strong or weak.**

---

## 2. The one idea: contraction is a tree walk, and it can't see repeats

Every constraint is an **expression tree** — variables and constants at the leaves, operators
(`+`, `*`, `pow`, `sin`, `/`, …) at the internal nodes. Contraction walks that tree twice:

1. **Forward (bottom-up):** compute an interval for each node by combining its children's intervals
   through its operator, all the way to the root.
2. **Backward (top-down):** starting from what the root *must* equal (the constraint itself), invert
   each operator to shrink that node's children, pushing tighter intervals down toward the leaves.

A variable's contracted interval is whatever survives the backward projection arriving at its leaf.
Two properties of this walk explain everything you need:

- **A leaf is narrowed only by the operators on the single path from the root down to it.** So *where
  a variable sits in the tree* and *how many leaves it occupies* decide how well it contracts.
- **The walk treats every leaf as an independent unknown — it cannot tell that two leaves are the
  same variable.**

That second property is the crux, and it is worth burning in:

> **Contraction is exact when a variable occupies one leaf, and degrades as it occupies more.**
> One leaf → the backward pass computes a perfect projection for that variable. Many leaves → each
> copy is projected on its own and the results are merely intersected; the operator that *should* tie
> the copies together (usually a top-level `+`) can't, so intervals over-estimate and dReal must
> bisect to recover.

This is the classic interval-arithmetic **dependency problem**, and it is documented behavior of the
IBEX contractor underneath dReal: forward-backward contraction is *optimal exactly when each variable
occurs once*, and worsens monotonically with each repeat.

**See it on the smallest case.** With `x ∈ [−1, 1]`, the forward pass sends `x * x` to
`[−1,1] · [−1,1] = [−1, 1]` — but the true range is `[0, 1]`. The two `x` leaves were treated as
independent, so the product node couldn't use the fact that they move together. One leaf (a single
square node) recovers the exact `[0, 1]`.

### The shape that follows: keep variables deep and singular

Because this is a fact about the *tree walk*, it holds for any expression — polynomial, trig,
exponential, rational. Two mathematically-equal trees, read by contraction very differently:

| Tree | What contraction sees |
|---|---|
| `(a + b) * (c + d)` — sums **under** a product | Each variable is one leaf on one path, funneling through a single product node. That node does one tight backward projection (`left ∩= root / right`, symmetrically), which flows cleanly down through the `+`s. **Sharp.** |
| `a*c + a*d + b*c + b*d` — products **under** a top-level sum | `a, b, c, d` each occupy *two* scattered leaves. The top-level `+` projects each summand independently and intersects; it cannot see that the two `a` leaves are the same. **Weak → bisects.** |

So the reshaping instinct you want is: **keep each variable (and each shared subexpression) on as few
leaves as possible, buried under the nonlinear operators rather than smeared across a top-level sum.**
Concretely, that means *add-inside-multiply* beats *multiply-inside-add* — and expanding a product
into a flat sum of terms is almost always the wrong move. Whenever a common factor multiplies several
summands, pulling it out collapses its leaves to one: `a*sin(x) + b*sin(x) → (a + b)*sin(x)` moves the
real work onto a single product node where the projection is exact. Nothing here is about polynomials
specifically; it is about occurrence count and depth.

### This is not a micro-optimization — measured

"Is a sum of squared distances ever negative?" is always false (→ `unsat`). Written over
`v₀…v₇ ∈ [−1000, 1000]` as `Σ (vᵢ − cᵢ)²` (each variable **once**) versus its expansion
`Σ (vᵢ² − 2cᵢvᵢ + cᵢ²)` (each variable **twice**):

| Same query, 8 variables | Result | CPU time |
|---|---|---|
| Factored (one leaf per variable) | `unsat` | **0.015 s** |
| Expanded (two leaves per variable) | *(no answer)* | **timed out > 120 s** |

Identical mathematics. The factored form refutes each square exactly with **zero bisection**; the
expanded form can't relate `vᵢ²` to `−2cᵢvᵢ`, so it bisects across eight dependency-plagued dimensions
and the tree explodes. The gap widens with variable count.

⚠ Occurrence blowup can **weaken the answer, not just slow it.** For `(x−1)² + (y−2)² < 0`
(unsatisfiable, but its minimum touches zero) the factored form returns the sharp `unsat` while the
expanded form manages only `delta-sat`. This is the direction the error always takes: looser
contraction costs **completeness** (a missed refutation / a weaker δ-verdict — dReal asserting φ^δ
satisfiable), **never soundness**. A badly-shaped formula makes dReal slower or less decisive; it will
never make dReal return a *false* `unsat`.

---

## 3. Corollaries you can now read off the model

Once §2 is in your bones, most "advice" is just you reading it off:

**Bound every variable — contraction needs something to grip.** The backward pass narrows a leaf by
intersecting intervals; an unbounded variable is an infinite interval with no edges to push in from,
so contraction accomplishes nothing and dReal bisects an unbounded space. Give every real a finite
domain, as tight as the problem allows — `(declare-fun x () Real [-10, 10])` — and the whole search
starts from a smaller, more contractible box.

**Naming a repeated subexpression is a real trade, not a free win.** If a large subexpression genuinely
appears many times and you can't factor it away, you can give it a name — but understand what that
costs. dReal implements a `let` binding, and equivalently an explicit `(declare-fun a …)` +
`(assert (= a <expr>))`, by making `a` a **new search dimension** plus a defining equality. That turns
`a` into a single leaf everywhere it's used (good — §2), but adds a variable dReal must itself contract
and bisect (a new axis of that exponential tree). Worth it only when the subexpression is big and
repeated several times. And prefer the explicit bounded form: a `let` leaves its hidden variable
unbounded, which by the paragraph above is exactly what you don't want.

**One thing you *don't* need to do:** hand-rewrite `x*x` as `x^2`. dReal already collapses
`x*x → pow(x,2)` when it builds the tree, so both are the same one-leaf square node. More generally,
dReal does *local* cleanups for you (constant folding, `1*x → x`, collecting like terms) — but it will
**never** restructure the algebra: it does not expand, does not factor, does not Horner-nest, does not
hoist common subexpressions. The shape you emit is the shape contraction walks. That division of labor
— dReal owns the trivial folds, you own the structure — is the practical upshot of this whole section.

---

## 4. Know which engine you are feeding

The §2 intuition is about dReal's NRA contractor. Two constructs route your constraint into a
*different* engine with a *different* thing to fear — recognizing that is itself the skill.

**ODE constraints (`integral` / `forall_t`, `QF_NRA_ODE`)** hand the flow off to CAPD, a rigorous
Taylor integrator. CAPD does **not** propagate a naive interval, so the occurrence-count intuition
does *not* transfer — refactoring the vector field's algebra buys you nothing there. What CAPD fears is
the **wrapping effect**: it re-encloses the reachable set in a box every step, and that over-estimate
compounds geometrically with **integration length, initial-box width, and the rotation/stiffness of
the flow**. So the intuition to carry into ODEs is *tighten the initial conditions and keep horizons
short*, not *factor the field*. Two footguns worth naming because they fail silently (as lost
refutation power, never a false `unsat`): a division or `sqrt`/`log` whose argument can cross its
singular domain over the reachable set makes CAPD throw and skip that constraint; and an invariant
that references the flow variable instead of the trajectory **endpoint** variable, or a *negated*
`integral`/`forall_t`, is dropped entirely.

**Quantified constraints (`forall`, the ∃∀ fragment)** are solved by a counterexample-guided loop that
repeatedly bisects the *universal* domain. Its cost is roughly `(domain-width / precision)` raised to
the **number of universally-quantified variables** — exponential in both. So the governing intuition is
simply: **every ∀ variable, and every unit of ∀-domain width, is expensive.** Keep the universal
variables few and their domains tightly bounded (an unbounded universal domain will not terminate for a
nonlinear body). dReal is also depth-one here — nested `forall` crashes — and a `forall`-bound variable
must not reuse a top-level variable's name.

---

## 5. Structure beats knobs — with a number

When a formula is unavoidably occurrence-heavy, dReal offers stronger (off-by-default) contractors your
users can switch on: `--polytope` (an LP relaxation that sees global linear structure plain contraction
misses) and `--acid` (adaptive shaving). They are real and functional — but they are a fallback, not a
substitute, and it is worth seeing *how much* worse the fallback is. On the 8-variable expanded query
from §2 (which times out by default):

| Expanded n=8, same query | Result | CPU time |
|---|---|---|
| default | *(timeout)* | > 120 s |
| `--acid` | *(timeout)* | > 90 s |
| `--polytope` | `unsat` | 49 s |
| *just emitting it factored* | `unsat` | **0.015 s** |

`--polytope` rescued the verdict; `--acid` didn't touch this case; and **both are thousands of times
slower than simply writing the constraint in the right shape.** Reach for the flags only when you truly
can't control the formula. The one command-line dial that is always legitimate is **`--precision δ`**:
larger δ is faster but yields a weaker witness, smaller δ is sharper but slower — and it never affects
soundness.

> **A caveat against over-believing §2.** Reshaping for occurrence count is a strong default, not a
> law. When a polynomial and a transcendental are *intrinsically* multiplied — think `p(x)·tanh(q(x))`
> — the variable coupling is in the mathematics, and no syntactic re-factoring moves it; dReal's own
> experiments found `expand`/`factor`/`horner` pre-passes gave no improvement there. The model tells
> you where to look; it does not promise every expression can be made easy.

---

## The input surface — the parts that bite silently

dReal accepts the usual arithmetic, `exp`/`log`, and trig operators; the exhaustive token list lives
in `docs/syntax-reference.md`. Only the non-obvious facts belong here — the ones that surprise a
generator or fail without an obvious message:

- **`log` is the natural log** — there is no base-2/base-10 variant, so emitting one silently means the
  wrong function.
- **Unsupported functions fail loud, not silent.** Anything outside the accepted set (`cot`/`sec`/`csc`,
  the inverse hyperbolics, base-N `log`) is a hard `No function definition for …` error — a generator
  that emits them crashes rather than quietly mis-solving. This is the good failure mode; rely on it.
- **Inequalities must be scalar-valued** — emit one atom per component, never a vector `f(x) ≤ 0`.
- **A strict `<` is treated as `≤`** internally (a completeness, not soundness, effect) — don't lean on
  strictness for correctness.
- **Bounds** attach to a variable inline, `(declare-fun x () Real [lb, ub])` (the comma is mandatory),
  or as ordinary assertions.
- **No incremental interface.** `(push)`/`(pop)` are formally unsupported and rejected with a hard
  error (`(reset)`/`(reset-assertions)` don't even parse) — emit one self-contained script per query.
  Multiple `(check-sat)` in one script are fine as long as assertions only accumulate. Solver caches
  don't persist across invocations, so structure a family as independent files rather than one
  retracting script. Rationale: `docs/decisions.md` §"SMT-LIB push/pop: formally unsupported".

---

## Appendix: sources

Grounded in the dReal source, the vendored IBEX/CAPD sources, and behavioral runs against a stock
`gcc_build/dreal4` (`--precision 0.001`, single-threaded). The audit markdown under `ibex_docs/` and
`DEPENDENCIES.md` carry stale contractor-reachability claims (`LP_LIB=none`, "polytope dormant", "ACID
never runs") that are **wrong** for the current tree — the facts here come from code and CMake.

| Claim | Source |
|---|---|
| Contract-then-bisect loop; contraction cheap, bisection exponential | `src/dreal/solver/icp_parallel.cc`; `docs/architecture.md` |
| Forward/backward tree walk; optimal at one occurrence, degrades with repeats | IBEX docs `ibex-fork/doc/contractor.rst`, `ibex-fork/doc/function.rst`; `ibex-fork/src/contractor/ibex_CtcFwdBwd.cpp` |
| `x*x → pow(x,2)`; local folds but no expand/factor/Horner/CSE | `src/third_party/…/drake/dreal/symbolic/symbolic_expression.cc`; no simplify pass in `src/dreal/` solve path |
| `let` / aux reifies to a new dimension + defining equality; `let` var unbounded | `src/dreal/smt2/parser.yy` (`let_binding_list`), `src/dreal/smt2/driver.cc` |
| Operator set; unsupported tokens throw; inline `[lb,ub]` bound | `src/dreal/smt2/scanner.ll`; `docs/syntax-reference.md`; behavioral (`sec` → hard error) |
| HC4 default; `--polytope`/`--acid`/`--3bcid` opt-in & live; soplex LP linked; no Newton | `src/dreal/solver/theory_solver.cc`, `src/dreal/dreal_main.cc`, `CMakeLists.txt` (`-DLP_LIB=soplex`); behavioral (`--polytope` solves the n=8 case in 49 s) |
| ODE = CAPD; wrapping effect; singular-domain throw; endpoint/negation drops | `capd_docs/`; `docs/ode-integration.md`; `docs/decisions.md` |
| ∀ cost exponential in variable count and domain width; no nested `forall` | `docs/forall-semantics.md`; `ibex_docs/AUDIT-QUANTIFIERS.md` |
| `p(x)·tanh(q(x))` rewrites gave no speedup | `exists_forall_perf.md` (repo root) |
