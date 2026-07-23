/*
   Copyright 2017 Toyota Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include "dreal/contractor/contractor_ibex_newton.h"

#include <cmath>

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::sqrt;
using std::vector;

class ContractorIbexNewtonTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};
  // The cell reads only config.newton_ceil(); the default
  // (kDefaultNewtonCeil = 0.01 = ibex::CtcNewton::default_ceil) is what the
  // ceil-gated cases below are written against.
  const Config config_{};
};

// Square SAT system {x² + y² == 1, x - y == 0} with root (√0.5, √0.5) on a
// small box (max_diam 0.0099 <= ceil 0.01, so CtcNewton's own gate lets the
// Newton step run). Newton (a) keeps the true root inside (soundness of the
// contraction — never narrows past an exact solution) and (b) lands strictly
// inside what a single HC4 forward-backward pass over the same constraints
// achieves — Hansen-Sengupta converges near-quadratically at this regular
// root while HC4 leaves ~5e-3-wide intervals here. The inequality x + y >= 0
// in the input exercises the equality filter: the cell must drop it and stay
// square.
TEST_F(ContractorIbexNewtonTest, SquareSatContractsInsideHc4AndKeepsRoot) {
  const Formula f1{x_ * x_ + y_ * y_ == 1.0};
  const Formula f2{x_ - y_ == 0.0};
  const Formula g{x_ + y_ >= 0.0};  // filtered out; cell stays square
  box_[x_] = Box::Interval(0.70, 0.7099);
  box_[y_] = Box::Interval(0.70, 0.7099);

  // HC4-only reference: one forward-backward pass per constraint.
  ContractorStatus cs_hc4{box_};
  const ContractorIbexFwdbwd hc4_f1{f1, box_, Config{}};
  const ContractorIbexFwdbwd hc4_f2{f2, box_, Config{}};
  {
    const UpwardRoundingScope rms_;
    hc4_f1.Prune(&cs_hc4, rms_.token());
    hc4_f2.Prune(&cs_hc4, rms_.token());
  }

  ContractorStatus cs{box_};
  const ContractorIbexNewton ctc{{f1, f2, g}, box_, config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  ASSERT_FALSE(cs_hc4.box().empty());
  ASSERT_FALSE(cs.box().empty());

  // Retains the true root x = y = √0.5. The enclosure must contain the real
  // root; sqrt(0.5) (the nearest double) can sit up to ~1 ulp outside a
  // near-degenerate enclosure, hence the 1e-12 slack.
  const double root{sqrt(0.5)};
  EXPECT_LE(cs.box()[x_].lb(), root + 1e-12);
  EXPECT_GE(cs.box()[x_].ub(), root - 1e-12);
  EXPECT_LE(cs.box()[y_].lb(), root + 1e-12);
  EXPECT_GE(cs.box()[y_].ub(), root - 1e-12);

  // Strictly inside the HC4-only result: subset, and far tighter than HC4
  // can reach on this box (HC4 leaves ~5e-3; Newton reaches ~1e-15 at a
  // regular root).
  EXPECT_TRUE(cs.box()[x_].is_subset(cs_hc4.box()[x_]));
  EXPECT_TRUE(cs.box()[y_].is_subset(cs_hc4.box()[y_]));
  EXPECT_LT(cs.box()[x_].ub() - cs.box()[x_].lb(),
            1e-6);  // INTEGRATION-VERIFY: Hansen-Sengupta fixpoint width here
  EXPECT_LT(cs.box()[y_].ub() - cs.box()[y_].lb(), 1e-6);
}

// Square UNSAT system {x² + y² == 1, x + y == 3} on a small box around
// (1, 2): the residual at the midpoint is (4, 0) and the Jacobian
// [[2x, 2y], [1, 1]] is regular (det ≈ -2), so the Hansen-Sengupta step lands
// far outside the box and the intersection empties. Refuting a truly
// unsatisfiable subsystem is the sound direction (never a false `unsat` — the
// discarded region provably contains no solution of the equalities).
TEST_F(ContractorIbexNewtonTest, SquareUnsatEmpties) {
  const Formula f1{x_ * x_ + y_ * y_ == 1.0};
  const Formula f2{x_ + y_ == 3.0};
  box_[x_] = Box::Interval(0.995, 1.0049);
  box_[y_] = Box::Interval(1.995, 2.0049);
  ContractorStatus cs{box_};
  const ContractorIbexNewton ctc{{f1, f2}, box_, config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_TRUE(cs.box().empty());
}

// Proper-subset case (the ibex::VarSet dispatch branch): a 3-variable box
// {x, y, z} where the equalities {x² + y² == 1, x - y == 0} mention only
// S = {x, y} (m=2, |S|=2 — square; nb_param = 1). z becomes an ibex
// parameter column: ibex's parameterized newton() writes back only VarSet
// columns (set_var_box skips parameter columns, ibex_VarSet.cpp), so z can
// change only if the whole box empties — which it doesn't, since the true
// root (√0.5, √0.5) exists for every z. z must itself be <= ceil (0.01)
// wide: CtcNewton's application gate tests the max diameter of the FULL box
// including parameter columns (ibex_CtcNewton.cpp), so a wide z would
// silently suppress the Newton step (and this test would fail its width
// assertions — pinning that gate semantics).
TEST_F(ContractorIbexNewtonTest, ProperSubsetNarrowsVarsKeepsParamUntouched) {
  const Variable z{"z", Variable::Type::CONTINUOUS};
  Box box{{x_, y_, z}};
  const Formula f1{x_ * x_ + y_ * y_ == 1.0};
  const Formula f2{x_ - y_ == 0.0};
  box[x_] = Box::Interval(0.70, 0.7099);
  box[y_] = Box::Interval(0.70, 0.7099);
  box[z] = Box::Interval(5.0, 5.0049);  // <= ceil wide; see comment above
  ContractorStatus cs{box};
  const ContractorIbexNewton ctc{{f1, f2}, box, config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  ASSERT_FALSE(cs.box().empty());

  // Retains the true root x = y = √0.5 (same slack rationale as the
  // all-vars SAT test) and converges near-quadratically on the S columns.
  const double root{sqrt(0.5)};
  EXPECT_LE(cs.box()[x_].lb(), root + 1e-12);
  EXPECT_GE(cs.box()[x_].ub(), root - 1e-12);
  EXPECT_LE(cs.box()[y_].lb(), root + 1e-12);
  EXPECT_GE(cs.box()[y_].ub(), root - 1e-12);
  EXPECT_LT(cs.box()[x_].ub() - cs.box()[x_].lb(),
            1e-6);  // INTEGRATION-VERIFY: Hansen-Sengupta fixpoint width here
  EXPECT_LT(cs.box()[y_].ub() - cs.box()[y_].lb(), 1e-6);

  // The parameter column is bitwise untouched.
  EXPECT_EQ(cs.box()[z].lb(), 5.0);
  EXPECT_EQ(cs.box()[z].ub(), 5.0049);
}

// Non-square subsystems make the cell a dummy identity guard: one equality
// over two variables (m=1, |S|=2), and no equality at all after filtering
// (m=0). The factory maps a dummy cell to the ID contractor, so a dummy's
// Prune is never called and the box is trivially unchanged.
TEST_F(ContractorIbexNewtonTest, NonSquareIsDummy) {
  const Formula f{x_ * x_ + y_ * y_ == 1.0};
  box_[x_] = Box::Interval(0.0, 1.0);
  box_[y_] = Box::Interval(0.0, 1.0);

  const ContractorIbexNewton one_eq_two_vars{{f}, box_, config_};
  EXPECT_TRUE(one_eq_two_vars.is_dummy());

  const ContractorIbexNewton no_equalities{{x_ + y_ >= 0.0}, box_, config_};
  EXPECT_TRUE(no_equalities.is_dummy());

  const Contractor via_factory{make_contractor_ibex_newton({f}, box_, config_)};
  EXPECT_TRUE(is_id(via_factory));
}

// Wide box (max_diam 10 > ceil 0.01): CtcNewton's own application gate makes
// contract() an identity, so the square system leaves the box untouched — no
// Jacobian work on wide boxes.
TEST_F(ContractorIbexNewtonTest, WideBoxUnchanged) {
  const Formula f1{x_ * x_ + y_ * y_ == 1.0};
  const Formula f2{x_ - y_ == 0.0};
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 10.0);
  ContractorStatus cs{box_};
  const ContractorIbexNewton ctc{{f1, f2}, box_, config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  ASSERT_FALSE(cs.box().empty());
  EXPECT_EQ(cs.box()[x_].lb(), 0.0);
  EXPECT_EQ(cs.box()[x_].ub(), 10.0);
  EXPECT_EQ(cs.box()[y_].lb(), 0.0);
  EXPECT_EQ(cs.box()[y_].ub(), 10.0);
}

}  // namespace
}  // namespace dreal
