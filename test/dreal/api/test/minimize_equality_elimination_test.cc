// Regression tests for dreal/dreal4#320 — Minimize equality-elimination
// (port of soonhokong/dreal4 e7ab1ff8a).
//
// SYMPTOM: `Context::Impl::Minimize` encodes minimization as
//     ψ = ∃z. z = f(x) ∧ ∀y. (∨ᵢ ¬ϕᵢ(y)) ∨ z ≤ f(y),
// and its side-constraint closure drags every variable reachable through any
// asserted constraint into the quantified y-block. An "unrelated" equality
// `X = e(A,B)` therefore turns X into a quantified dim — unbounded if X was
// never given bounds — and the CE-guided forall contractor stalls on the
// unbounded universal box. Issue #320's own numbers: 0.02 s CPU without the
// equality, >30 min with it. Pure performance/termination: no soundness
// dimension in the hang itself.
//
// FIX (ported): an equality constraint `v == e` (either orientation) where v
// appears neither in e nor in any objective is not quantified; v is eliminated
// by substituting e for it. Divergence from the upstream port (guard added
// here): elimination is restricted to v whose box interval is unbounded on
// both sides — eliminating a box-bounded v would silently drop its bounds from
// the ϕᵢ(y) antecedent, enlarging the universal set and flipping delta-sat
// optimization queries to unsat: SOUNDNESS-shaped at the Minimize-encoding
// level (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat).
// MinimizeBoundedDefinedVariableKeepsBounds pins that behavior down.

#include "dreal/api/api.h"

#include <gtest/gtest.h>

namespace dreal {
namespace {

// Issue #320's reproducer, verbatim modulo SMT2→API syntax. Pre-fix this HANGS
// (killed by `timeout 30`, exit 124); post-fix it terminates in well under a
// second with the true minimum 1e-10 at (A, B) = (3, 0).
TEST(MinimizeEqualityElimination, Issue320UnrelatedEqualityHang) {
  const Variable x{"variable_X", Variable::Type::CONTINUOUS};
  const Variable a{"variable_A", Variable::Type::CONTINUOUS};
  const Variable b{"variable_B", Variable::Type::CONTINUOUS};
  const Expression objective{4e-10 + 1e-10 * b - 1e-10 * a};
  const Formula constraint{0 <= b && b <= 3 && 0 <= a && a <= 3 &&
                           x == 4e-10 + 1e-10 * b - 1e-10 * a};
  const auto result = Minimize(objective, constraint, /* delta = */ 1e-16);
  ASSERT_TRUE(result);
  const double a_v{(*result)[a].mid()};
  const double b_v{(*result)[b].mid()};
  EXPECT_TRUE(0 <= a_v && a_v <= 3);
  EXPECT_TRUE(0 <= b_v && b_v <= 3);
  const double f_v{4e-10 + 1e-10 * b_v - 1e-10 * a_v};
  EXPECT_NEAR(f_v, 1e-10, 1e-12);
}

// Guard for the elimination's unboundedness restriction: here X is defined by
// an equality AND carries its own box bounds (2 ≤ X ≤ 100), which constrain
// the objective variable A through `x == a`. Naive (upstream-faithful)
// elimination drops X's bounds from ϕᵢ(y): the universal set grows to all of
// y_A ∈ [0, 3], forcing z ≤ 0 while the existential side still has A ≥ 2 —
// unsat on a delta-sat query (false unsat at the Minimize-encoding level).
// Correct behavior: delta-sat, A within the feasible [2, 3].
//
// The verdict is the discriminator; the witness is deliberately NOT asserted
// tight. The default `--model`-style result is the raw terminating box
// (BUG-011 reporting semantics), and on this quantified encoding ICP
// terminates on a box around A ≈ 2.33 — feasible, but not the minimizer.
// Witness quality is pre-existing behavior orthogonal to this port.
TEST(MinimizeEqualityElimination, MinimizeBoundedDefinedVariableKeepsBounds) {
  const Variable x{"variable_X", Variable::Type::CONTINUOUS};
  const Variable a{"variable_A", Variable::Type::CONTINUOUS};
  const Expression objective{a};
  const Formula constraint{0 <= a && a <= 3 && x == a && 2 <= x && x <= 100};
  const double delta{0.001};
  const auto result = Minimize(objective, constraint, delta);
  ASSERT_TRUE(result);
  const double a_v{(*result)[a].mid()};
  EXPECT_GE(a_v, 2.0 - delta - 0.01);
  EXPECT_LE(a_v, 3.0 + delta + 0.01);
}

}  // namespace
}  // namespace dreal
