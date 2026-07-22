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
#include "dreal/contractor/contractor_ibex_fwdbwd.h"

#include <iostream>
#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/interval.h"
#include "dreal/util/string_to_interval.h"

namespace dreal {
namespace {

using std::vector;

class ContractorIbexFwdbwdTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const Variable z_{"z", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_, z_}};
  Box box_{vars_};
};

TEST_F(ContractorIbexFwdbwdTest, Sat) {
  const Formula f{cos(x_) == sin(y_)};
  box_[x_] = Box::Interval(0.0, 3.14 / 2);
  box_[y_] = Box::Interval(0.2, 0.3);
  box_[z_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexFwdbwd ctc{f, box_, Config{}};

  // Inputs
  EXPECT_TRUE(ctc.input()[0]);
  EXPECT_TRUE(ctc.input()[1]);
  EXPECT_FALSE(ctc.input()[2]);

  // Before pruning, the box is not empty.
  EXPECT_FALSE(cs.box().empty());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  // After pruning, the box is still not empty.
  EXPECT_FALSE(cs.box().empty());

  // x : [1.2708, 1.3708]
  // y : [0.2, 0.3]
  // z : [0.0, 1.0]
  EXPECT_TRUE(cs.box()[x_].is_subset(Box::Interval(1.270, 1.371)));
  EXPECT_EQ(cs.box()[y_], Box::Interval(0.2, 0.3));
  EXPECT_EQ(cs.box()[z_], Box::Interval(0.0, 1.0));

  // Outputs. Only x-dimension is changed.
  EXPECT_TRUE(cs.output()[0]);
  EXPECT_FALSE(cs.output()[1]);
  EXPECT_FALSE(cs.output()[2]);
}

TEST_F(ContractorIbexFwdbwdTest, Unsat) {
  const Formula f{sin(x_) == y_};
  box_[x_] = Box::Interval(0.1, 0.2);
  box_[y_] = Box::Interval(0.2, 0.3);
  box_[z_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexFwdbwd ctc{f, box_, Config{}};

  // Inputs: only x and y.
  EXPECT_TRUE(ctc.input()[0]);
  EXPECT_TRUE(ctc.input()[1]);
  EXPECT_FALSE(ctc.input()[2]);

  // Before pruning, the box is not empty.
  EXPECT_FALSE(cs.box().empty());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  // After pruning, the box is empty.
  EXPECT_TRUE(cs.box().empty());

  // Outputs. All dimensions are changed.
  EXPECT_TRUE(cs.output()[0]);
  EXPECT_TRUE(cs.output()[1]);
  EXPECT_TRUE(cs.output()[2]);
}

TEST_F(ContractorIbexFwdbwdTest, TestSmt2Problem20) {
  const Formula f{y_ + z_ == x_};
  ContractorStatus cs{box_};
  const ContractorIbexFwdbwd ctc{f, box_, Config{}};
  Box& box = cs.mutable_box();

  box[x_] = StringToInterval("0.7");
  box[y_] = StringToInterval("0.0647");
  box[z_] = StringToInterval("0.6353");
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  // After pruning, the box is still not empty.
  EXPECT_FALSE(cs.box().empty());
}

// --- Phase-0 soundness net (bucket 1): empty-propagation through a single
// ContractorIbexFwdbwd::Prune, both directions. Each formula below is
// genuinely UNSAT (must empty) or genuinely SAT (must stay non-empty and keep
// its witness); the assertions encode correct empty-propagation as the gate
// the EmptyBoxException refactor must preserve. ---

// Out-of-range transcendental: tanh has range (-1, 1), so tanh(x) == 2 is
// infeasible for every x. HC4 empties this at the *root* intersection (the
// forward image of tanh never reaches 2), which is exactly the throw site
// Tier-0 reroutes to a return-status.
TEST_F(ContractorIbexFwdbwdTest, TanhSaturationEmpties) {
  const Formula f{tanh(x_) == 2.0};
  box_[x_] = Box::Interval(-5.0, 5.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  box_[z_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexFwdbwd ctc{f, box_, Config{}};

  EXPECT_FALSE(cs.box().empty());
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "tanh(x) == 2 is infeasible (range of tanh is (-1,1)); the box must "
         "be emptied.";
}

// Forward domain error funnel: log is undefined on a wholly-negative domain.
// IBEX's Eval catches its *own* forward EmptyBoxException, leaves the top
// domain empty, and the backward root intersection then empties the box. This
// directly exercises the funnel path Tier-0 must keep sound (forward-undefined
// -> root-empty). log(x) == 0 means x == 1, excluded by x in [-5,-1].
TEST_F(ContractorIbexFwdbwdTest, LogNegativeDomainFunnelEmpties) {
  const Formula f{log(x_) == 0.0};
  box_[x_] = Box::Interval(-5.0, -1.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  box_[z_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexFwdbwd ctc{f, box_, Config{}};

  EXPECT_FALSE(cs.box().empty());
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "log is undefined on [-5,-1]; the forward-undefined funnel must empty "
         "the box.";
}

// Product infeasibility through the div/mul backward operators: z == x / y with
// x,y pinned to a value whose quotient is disjoint from z's domain. Exercises a
// different operator path to empty than the add/sin cases above.
TEST_F(ContractorIbexFwdbwdTest, QuotientInfeasibleEmpties) {
  const Formula f{z_ == x_ / y_};
  box_[x_] = Box::Interval(1.0, 1.0);
  box_[y_] = Box::Interval(2.0, 2.0);  // x/y == 0.5, exactly representable
  box_[z_] = Box::Interval(10.0, 10.0);
  ContractorStatus cs{box_};
  const ContractorIbexFwdbwd ctc{f, box_, Config{}};

  EXPECT_FALSE(cs.box().empty());
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "0.5 (= 1/2) != 10; the constraint is infeasible and must empty.";
}

// Must-NOT-empty control (false-unsat guard): x*x == 4 with x in [0,10]
// contracts hard (down to ~[2,2]) but stays non-empty and must still contain
// the witness x == 2. A refactor that spuriously emptied feasible boxes would
// fail here. 4 and 2 are exactly representable, so this is rounding-insensitive.
TEST_F(ContractorIbexFwdbwdTest, HardContractionStaysNonEmpty) {
  const Formula f{x_ * x_ == 4.0};
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  box_[z_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexFwdbwd ctc{f, box_, Config{}};

  EXPECT_FALSE(cs.box().empty());
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_FALSE(cs.box().empty())
      << "x*x == 4 with x in [0,10] is satisfiable (x == 2); must not empty.";
  EXPECT_TRUE(cs.box()[x_].contains(2.0))
      << "The surviving box must still contain the witness x == 2.";
}

TEST_F(ContractorIbexFwdbwdTest, TestSmt2Problem20Lowlevel) {
  // Given a box,
  //     x = 0.2
  //     y = 0.5
  //     z = 0.7
  //
  // `x + y = z` holds. However, IBEX's contractor prunes out this
  // point-box because `0.2` and `0.7` are machine-representable.

  const double v1 = 0.2;
  const double v2 = 0.5;
  const double v3 = 0.7;

  // Double check the arithmetic first.
  {
    NearestRoundingScope g;
    EXPECT_EQ(v1 + v2 - v3, 0.0);
  }

  const auto* x = &ibex::ExprSymbol::new_();
  const auto* y = &ibex::ExprSymbol::new_();
  const auto* z = &ibex::ExprSymbol::new_();
  ibex::Function f(*x, *y, *z, *x + *y - *z);
  ibex::IntervalVector box(3);

  // Note that the use of BloatPoint is necessary here. Otherwise, we have an
  // empty box after pruning.
  box[0] = BloatPoint(v1);
  box[1] = BloatPoint(v2);
  box[2] = BloatPoint(v3);

  ibex::CtcFwdBwd c(f);
  c.contract(box);

  EXPECT_FALSE(box.is_empty());
}

}  // namespace
}  // namespace dreal
