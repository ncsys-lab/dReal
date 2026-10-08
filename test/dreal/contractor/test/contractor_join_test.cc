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
#include "dreal/contractor/contractor_join.h"

#include <iostream>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/util/rounding.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/interval.h"

namespace dreal {
namespace {

using std::vector;

class ContractorJoinTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const Variable z_{"z", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_, z_}};
  Box box_{vars_};
};

TEST_F(ContractorJoinTest, Sat) {
  const Formula f1{cos(x_) >= sin(y_) - 0.1};
  const Formula f2{cos(x_) <= sin(y_) + 0.1};
  box_[x_] = Box::Interval(0.0, 3.14 / 2);
  box_[y_] = Box::Interval(0.2, 0.3);
  box_[z_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  Config config{};
  const Contractor ctc1 = make_contractor_ibex_fwdbwd(f1, box_, config);
  const Contractor ctc2 = make_contractor_ibex_fwdbwd(f2, box_, config);
  const Contractor ctc = make_contractor_join({ctc1, ctc2}, config);

  // Inputs
  EXPECT_TRUE(ctc.input()[0]);
  EXPECT_TRUE(ctc.input()[1]);
  EXPECT_FALSE(ctc.input()[2]);

  // Before pruning, the box is not empty.
  EXPECT_FALSE(cs.box().empty());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  // After pruning, the box is still not empty.
  EXPECT_FALSE(cs.box().empty());
}

// Phase-0 soundness net (bucket 3): a disjunction empties only when *every*
// branch empties. Both sin(x) == 2 and cos(x) == 2 are infeasible (sin/cos
// range is [-1,1]), so each branch empties and the join must empty too. Guards
// that the empty signal survives disjunctive composition.
TEST_F(ContractorJoinTest, AllBranchesEmpty) {
  const Formula f1{sin(x_) == 2.0};
  const Formula f2{cos(x_) == 2.0};
  box_[x_] = Box::Interval(-5.0, 5.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  box_[z_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  Config config{};
  const Contractor ctc1 = make_contractor_ibex_fwdbwd(f1, box_, config);
  const Contractor ctc2 = make_contractor_ibex_fwdbwd(f2, box_, config);
  const Contractor ctc = make_contractor_join({ctc1, ctc2}, config);

  EXPECT_FALSE(cs.box().empty());
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "Both sin(x) == 2 and cos(x) == 2 are infeasible; the join of two "
         "empty branches must be empty.";
}

// A join keeps every variable either side reaches a used constraint through.
// Keeping one side's set would drop constraints from the explanation, and the
// learned clause could cut a satisfiable branch — SOUNDNESS (asserts φ
// T-unsatisfiable on a T-satisfiable φ — false unsat).
TEST_F(ContractorJoinTest, JoinUnionsTheReachOfAUsedConstraint) {
  const Formula f{x_ + y_ <= 1.0};
  const Formula on_y{y_ >= 0.5};
  ContractorStatus cs1{box_};
  cs1.AddUsedConstraint(f, Variables{x_});
  ContractorStatus cs2{box_};
  cs2.AddUsedConstraint(f);
  cs2.AddUsedConstraint(on_y);
  cs1.InplaceJoin(cs2);
  cs1.AddUnsatWitness(x_);
  EXPECT_EQ(cs1.Explanation().count(on_y), 1u)
      << "the join reaches f through x alone";
}

TEST_F(ContractorJoinTest, ReachOutsideTheFormulaThrows) {
  ContractorStatus cs{box_};
  EXPECT_THROW(cs.AddUsedConstraint(x_ <= 1.0, Variables{z_}), std::runtime_error);
}

}  // namespace
}  // namespace dreal
