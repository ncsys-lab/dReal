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
// TDD-first: the expected FIRST failure of this file is a COMPILE error
// naming the new symbols — "dreal/contractor/contractor_ibex_mohc.h" not
// found / ContractorIbexMohc undeclared / Config::use_mohc undeclared —
// until the cell files + the wiring spec (scratchpad wiring_mohc.md) land.
// New-test-file footgun: the suite globs at CMake configure time, so this
// file is compiled only after a reconfigure (CLAUDE.md §Running Tests; the
// suite count going up is the tell).
#include "dreal/contractor/contractor_ibex_mohc.h"

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

using std::vector;

class ContractorIbexMohcTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};
  Config mohc_config_;
  ContractorIbexMohcTest() {
    mohc_config_.mutable_use_mohc().set_from_command_line(true);
  }
};

// Multi-occurrence monotone SAT constraint: x*x - x == 0 on x in [0.6, 1.5]
// (f' = 2x - 1 >= 0.2 on the box, so f is strictly increasing; the only root
// in the box is x = 1). The double occurrence of x makes natural interval
// evaluation lose to the dependency problem, which is exactly the width
// Mohc's occurrence-grouping monotone machinery recovers. Assertions:
//   (a) the true root x = 1 is retained (sound contraction never narrows
//       past an exact solution — same slack rationale as the newton test:
//       the root may sit within ~1 ulp of a near-degenerate enclosure);
//   (b) the result is a subset of a single HC4 forward-backward pass over
//       the same constraint — guaranteed structurally, because every
//       CtcMohcRevise starts with the HC4Revise forward-eval + backward-proj
//       pair before the monotone procedures (fork ibex_CtcMohc.cpp,
//       CtcMohcRevise::contract);
//   (c) genuinely tighter than that single HC4 pass, which leaves
//       x in [sqrt(0.6), sqrt(1.5)] ~ [0.7746, 1.2247] (width ~0.45).
TEST_F(ContractorIbexMohcTest, SatMultiOccMonotoneContractsInsideHc4) {
  const Formula f{x_ * x_ - x_ == 0.0};
  box_[x_] = Box::Interval(0.6, 1.5);
  box_[y_] = Box::Interval(0.0, 1.0);

  // HC4-only reference: one forward-backward pass.
  ContractorStatus cs_hc4{box_};
  const ContractorIbexFwdbwd hc4{f, box_, Config{}};
  { const UpwardRoundingScope rms_; hc4.Prune(&cs_hc4, rms_.token()); }

  ContractorStatus cs{box_};
  const ContractorIbexMohc ctc{{f}, box_, mohc_config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  ASSERT_FALSE(cs_hc4.box().empty());
  ASSERT_FALSE(cs.box().empty());

  // (a) Root retention.
  EXPECT_LE(cs.box()[x_].lb(), 1.0 + 1e-12);
  EXPECT_GE(cs.box()[x_].ub(), 1.0 - 1e-12);

  // (b) Subset of the single HC4 pass.
  EXPECT_TRUE(cs.box()[x_].is_subset(cs_hc4.box()[x_]));

  // (c) Strictly tighter than the single HC4 pass (width ~0.45): the
  // CtcPropag fixpoint alone converges well below that here even before the
  // monotone procedures kick in, and MonoBoxNarrow narrows toward the root
  // when amohc activates it.
  // INTEGRATION-VERIFY: the 0.1 width bound assumes the propagation fixpoint
  // (ratio 0.01) plus MonoBoxNarrow reach near the x = 1 root on this box;
  // amohc (tau_mohc = ADAPTIVE) may or may not enable the monotone
  // procedures on the first call, so the exact terminal width is not
  // hand-derivable. Loosen toward 0.4 (still < HC4's 0.45) if the fixpoint
  // stalls earlier than expected — but a bound near HC4's own width would
  // mean the cell is dead wiring, which is what this assert is for.
  EXPECT_LT(cs.box()[x_].ub() - cs.box()[x_].lb(), 0.1);
}

// Two-variable multi-occurrence SAT case: x*y + x == 1.5 (x occurs twice;
// f = x*(y+1) is increasing in both x and y on the box), with the known
// solution (x, y) = (0.75, 1). The box must stay non-empty and the witness
// must be retained — SAT boxes never falsely empty (a false empty here would
// be the SOUNDNESS direction: asserts φ T-unsatisfiable on a T-satisfiable
// φ — false unsat).
TEST_F(ContractorIbexMohcTest, SatTwoVarMultiOccKeepsWitness) {
  const Formula f{x_ * y_ + x_ == 1.5};
  box_[x_] = Box::Interval(0.4, 1.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexMohc ctc{{f}, box_, mohc_config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  ASSERT_FALSE(cs.box().empty());
  EXPECT_LE(cs.box()[x_].lb(), 0.75 + 1e-12);
  EXPECT_GE(cs.box()[x_].ub(), 0.75 - 1e-12);
  EXPECT_LE(cs.box()[y_].lb(), 1.0 + 1e-12);
  EXPECT_GE(cs.box()[y_].ub(), 1.0 - 1e-12);
}

// Tight SAT box around the root: x*x - x == 0 on x in [0.995, 1.005]. The
// root is interior; the pruned box must stay non-empty and keep it. Guards
// the same false-empty direction as above at the near-terminal box widths
// ICP actually feeds this cell.
TEST_F(ContractorIbexMohcTest, TightSatBoxStaysNonEmpty) {
  const Formula f{x_ * x_ - x_ == 0.0};
  box_[x_] = Box::Interval(0.995, 1.005);
  box_[y_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexMohc ctc{{f}, box_, mohc_config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  ASSERT_FALSE(cs.box().empty());
  EXPECT_LE(cs.box()[x_].lb(), 1.0 + 1e-12);
  EXPECT_GE(cs.box()[x_].ub(), 1.0 - 1e-12);
}

// UNSAT instance empties: x*x - x == -0.3 on x in [0.6, 1.0]. On this box f
// is increasing with range [-0.24, 0], so -0.3 is unreachable — refuting it
// is the sound direction (the discarded region provably contains no
// solution). The monotone evaluation refutes in one revise ([f(0.6), f(1.0)]
// misses -0.3); even without the monotone procedures the propagation loop's
// HC4Revise iterates (ub: 1 -> 0.837 -> 0.733 -> 0.658 -> empty) reach the
// same verdict, so the test does not depend on amohc's activation choice.
TEST_F(ContractorIbexMohcTest, UnsatEmpties) {
  const Formula f{x_ * x_ - x_ == -0.3};
  box_[x_] = Box::Interval(0.6, 1.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  ContractorStatus cs{box_};
  const ContractorIbexMohc ctc{{f}, box_, mohc_config_};
  ASSERT_FALSE(ctc.is_dummy());

  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

  EXPECT_TRUE(cs.box().empty());
}

// Dummy guard: no convertible constraints (empty input, or forall-only input
// — the ∃∀ NRA `forall`, which the assembly loop skips exactly like acid's;
// PITFALL forall-vs-forall_t) leaves nb_ctr == 0, so the cell is a dummy
// identity and the factory maps it to the ID contractor. A dummy's Prune is
// never called; the guard is a pure skip (identity contraction), costing
// only this optional COMPLETENESS lever.
TEST_F(ContractorIbexMohcTest, DummyOnEmptyAndForallOnlyInput) {
  box_[x_] = Box::Interval(0.0, 1.0);
  box_[y_] = Box::Interval(0.0, 1.0);

  const ContractorIbexMohc no_constraints{{}, box_, mohc_config_};
  EXPECT_TRUE(no_constraints.is_dummy());

  const Formula forall_only{forall({y_}, x_ + y_ >= 0.0)};
  const ContractorIbexMohc forall_input{{forall_only}, box_, mohc_config_};
  EXPECT_TRUE(forall_input.is_dummy());

  const Contractor via_factory{
      make_contractor_ibex_mohc({}, box_, mohc_config_)};
  EXPECT_TRUE(is_id(via_factory));
}

}  // namespace
}  // namespace dreal
