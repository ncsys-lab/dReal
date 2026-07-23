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
#include "dreal/contractor/contractor_ibex_obbt.h"

#include <limits>

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::vector;

class ContractorIbexObbtTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};
};

// LP-favorable coupled instance: the two linear constraints imply
// (x+y) + (x-y) = 2x <= 0, i.e. x <= 0 — a bound the LP recovers exactly but
// a single-constraint HC4 pass cannot see (each constraint alone leaves
// x <= 10, and x*y <= 60 propagates nothing through a 0-spanning divisor).
// The mild x*y nonlinearity keeps the X-Taylor relaxation nontrivial without
// giving HC4 anything to contract, so the HC4-only baseline is the original
// box and "strictly inside" is a clean one-sided comparison.
//
// THIS TEST IS THE Mode::Certified TRIPWIRE: under the LPSolver default
// Mode::NotCertified the soplex wrapper never returns OptimalProved, every LP
// result is skipped by the certification guard, and Prune is a silent no-op —
// x's ub stays 10 and the EXPECT_LT below fails. Containment alone would NOT
// catch that, hence the strict-contraction assert.
TEST_F(ContractorIbexObbtTest, SatCoupledLpTightensBeyondHc4) {
  const Formula f1{x_ + y_ <= 0.0};
  const Formula f2{x_ - y_ <= 0.0};
  const Formula f3{x_ * y_ <= 60.0};
  box_[x_] = Box::Interval(-10.0, 10.0);
  box_[y_] = Box::Interval(-10.0, 10.0);

  // HC4-only baseline: one fwdbwd pass per constraint.
  ContractorStatus cs_hc4{box_};
  {
    const UpwardRoundingScope rms_;
    for (const Formula& f : {f1, f2, f3}) {
      const ContractorIbexFwdbwd hc4{f, box_, Config{}};
      hc4.Prune(&cs_hc4, rms_.token());
    }
  }
  ASSERT_FALSE(cs_hc4.box().empty());
  EXPECT_EQ(cs_hc4.box()[x_].ub(), 10.0);  // INTEGRATION-VERIFY: HC4 leaves x untouched on this instance (each projection is vacuous over [-10,10]^2)

  // OBBT.
  ContractorStatus cs{box_};
  const ContractorIbexObbt ctc{{f1, f2, f3}, box_, Config{}};
  ASSERT_FALSE(ctc.is_dummy());
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_FALSE(cs.box().empty());
  // Soundness: the known feasible point (-1, 0.5) must be retained
  // (x+y = -0.5 <= 0, x-y = -1.5 <= 0, x*y = -0.5 <= 60).
  EXPECT_TRUE(cs.box()[x_].contains(-1.0));
  EXPECT_TRUE(cs.box()[y_].contains(0.5));
  // Intersect-only: OBBT never widens, so it stays inside the HC4-only box.
  EXPECT_TRUE(cs.box()[x_].is_subset(cs_hc4.box()[x_]));
  EXPECT_TRUE(cs.box()[y_].is_subset(cs_hc4.box()[y_]));
  // Strict contraction: the certified LP max of x over {x+y<=0, x-y<=0} is 0,
  // so the new ub is ~0 (+ certification slack); anything below 1.0 proves a
  // real certified tightening happened. Fails if Mode::Certified is missing.
  EXPECT_LT(cs.box()[x_].ub(), 1.0)
      << "OBBT must certify max x <= ~0 from the coupled linear rows; a no-op "
         "here means the LPs never returned OptimalProved (Mode::Certified "
         "missing?).";
  EXPECT_LT(cs.box()[x_].ub(), cs_hc4.box()[x_].ub())
      << "OBBT must tighten strictly beyond the HC4-only pass.";
}

// Infeasible LP relaxation: x+y <= 0 and x+y >= 1 are jointly (linearly)
// infeasible, but neither cut alone is violated over the box, so the
// contraction must come from the LP itself: soplex reports INFEASIBLE and the
// Neumaier-Shcherbina infeasibility test certifies it (InfeasibleProved) ->
// the box empties. The relaxation contains every real solution of the atoms,
// so proved-empty relaxation proves the atoms unsatisfiable over the box —
// never a false `unsat` (SOUNDNESS): this instance really has no solution.
// Also dies without Mode::Certified (InfeasibleProved never produced).
TEST_F(ContractorIbexObbtTest, LpInfeasibleProvedEmpties) {
  const Formula f1{x_ + y_ <= 0.0};
  const Formula f2{x_ + y_ >= 1.0};
  box_[x_] = Box::Interval(-10.0, 10.0);
  box_[y_] = Box::Interval(-10.0, 10.0);
  ContractorStatus cs{box_};
  const ContractorIbexObbt ctc{{f1, f2}, box_, Config{}};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_TRUE(cs.box().empty());  // INTEGRATION-VERIFY: NS Farkas certificate found for this trivial pair (else status is uncertified Infeasible and the box stays non-empty)
}

// Unbounded box: no corner point exists for the X-Taylor linearization and the
// certification bound-products degenerate, so Prune must take the pure-identity
// guard — box unchanged, non-empty, no throw. (Mirrors
// ibex::CtcPolytopeHull::contract's own unbounded-box early return.)
TEST_F(ContractorIbexObbtTest, UnboundedBoxIdentity) {
  const Formula f{x_ + y_ <= 0.0};
  box_[x_] = Box::Interval::ALL_REALS;
  box_[y_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexObbt ctc{{f}, box_, Config{}};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_FALSE(cs.box().empty());
  EXPECT_EQ(cs.box()[x_].lb(), -std::numeric_limits<double>::infinity());
  EXPECT_EQ(cs.box()[x_].ub(), std::numeric_limits<double>::infinity());
  EXPECT_EQ(cs.box()[y_].lb(), 0.0);
  EXPECT_EQ(cs.box()[y_].ub(), 1.0);
}

// Degenerate relaxation: x*x <= 1000 holds everywhere on [-10,10] (interval
// eval [0,100]), so the constraint is inactive, the linearizer emits 0 cuts,
// and Prune must take the pure-identity zero-cut guard — box unchanged,
// non-empty, no throw.
TEST_F(ContractorIbexObbtTest, ZeroCutsIdentity) {
  const Formula f{x_ * x_ <= 1000.0};
  box_[x_] = Box::Interval(-10.0, 10.0);
  box_[y_] = Box::Interval(-10.0, 10.0);
  ContractorStatus cs{box_};
  const ContractorIbexObbt ctc{{f}, box_, Config{}};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_FALSE(cs.box().empty());
  EXPECT_EQ(cs.box()[x_].lb(), -10.0);  // INTEGRATION-VERIFY: box-wide-satisfied constraint yields 0 cuts (inactive-constraint filter in LinearizerXTaylor), not a tightened bound
  EXPECT_EQ(cs.box()[x_].ub(), 10.0);
  EXPECT_EQ(cs.box()[y_].lb(), -10.0);
  EXPECT_EQ(cs.box()[y_].ub(), 10.0);
}

}  // namespace
}  // namespace dreal
