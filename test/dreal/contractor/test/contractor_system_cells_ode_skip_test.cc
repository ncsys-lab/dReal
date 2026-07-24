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
// TDD-first: the expected FIRST failure of this file is the IbexConverter
// throw — "IbexConverter: integral constraint is not supported."
// (ibex_converter.cc VisitIntegral) — raised from each cell's constructor
// loop, which today converts every non-`is_forall` formula and so chokes on
// the ODE atoms (`integral`/`forall_t`) present in a QF_NRA_ODE assertion
// list. The fix mirrors ContractorIbexNewton: filter `f.include_ode()` (and
// `is_forall`) out of formulas_ before conversion — the cell contracts the
// relational remainder; ODE atoms belong to the Lohner contractor. Skipping
// them only removes constraints from THIS over-approximating cell's system,
// i.e. it contracts less — sound by inclusion, COMPLETENESS-neutral for the
// cell (the ODE semantics stay enforced by contractor_ode_lohner as today).
// New-test-file footgun: the suite globs at CMake configure time, so this
// file is compiled only after a reconfigure (CLAUDE.md §Running Tests; the
// suite count going up is the tell).
#include <memory>
#include <utility>
#include <vector>

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_ibex_acid.h"
#include "dreal/contractor/contractor_ibex_mohc.h"
#include "dreal/contractor/contractor_ibex_obbt.h"
#include "dreal/contractor/contractor_ibex_polytope.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::make_shared;
using std::shared_ptr;
using std::vector;

class SystemCellsOdeSkipTest : public ::testing::Test {
 protected:
  // Relational vars (the remainder the cells should contract).
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  // ODE vars (flow var + init/terminal/time), mirrors contractor_capd_test.
  const Variable f_{"f", Variable::Type::CONTINUOUS};
  const Variable f0_{"f_0_0", Variable::Type::CONTINUOUS};
  const Variable ft_{"f_0_t", Variable::Type::CONTINUOUS};
  const Variable t0_{"time_0", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_, f_, f0_, ft_, t0_}};
  Box box_{vars_};

  // Trivial flow f' = 1 — construction-level only; no CAPD run here.
  const shared_ptr<const OdeFlow> ode_{make_shared<OdeFlow>(
      "flow_1",
      vector<std::pair<Variable, Expression>>{{f_, Expression{1.0}}})};

  // Relational remainder with known solution (x, y) = (0.75, 1):
  // x*(y+1) = 1.5 and x + y = 1.75 <= 2.
  const Formula rel1_{x_ * y_ + x_ == 1.5};
  const Formula rel2_{x_ + y_ <= 2.0};

  SystemCellsOdeSkipTest() {
    box_[x_] = Box::Interval(0.4, 1.0);
    box_[y_] = Box::Interval(0.0, 1.0);
    box_[f_] = Box::Interval(-5.0, 5.0);
    box_[f0_] = Box::Interval(0.0, 1.0);
    box_[ft_] = Box::Interval(-5.0, 5.0);
    box_[t0_] = Box::Interval(0.0, 1.0);
  }

  // A QF_NRA_ODE-shaped assertion list: relational atoms interleaved with
  // both ODE atom kinds (integral + forall_t; PITFALL forall-vs-forall_t —
  // this is the ODE forall_t, not the ∃∀ forall the cells already skip).
  vector<Formula> FormulasWithOdeAtoms() const {
    return {rel1_, integral(0.0, t0_, {f0_}, {ft_}, ode_), rel2_,
            forallT(ode_, 0.0, t0_, ft_ >= -10.0)};
  }

  // Shared per-cell check. Construction happens at the call site (that is
  // where the pre-fix converter throw fires); here: the cell is live (the
  // relational atoms made it into the system), Prune never falsely empties
  // a box holding the known relational solution (that would be the
  // SOUNDNESS direction — asserts φ T-unsatisfiable on a T-satisfiable φ —
  // false unsat), and used-constraint reporting only ever names formulas
  // the cell actually converted (never an ODE atom).
  template <typename Cell>
  void CheckCell(const Cell& ctc) {
    ASSERT_FALSE(ctc.is_dummy());
    ContractorStatus cs{box_};
    { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
    ASSERT_FALSE(cs.box().empty());
    EXPECT_LE(cs.box()[x_].lb(), 0.75 + 1e-12);
    EXPECT_GE(cs.box()[x_].ub(), 0.75 - 1e-12);
    EXPECT_LE(cs.box()[y_].lb(), 1.0 + 1e-12);
    EXPECT_GE(cs.box()[y_].ub(), 1.0 - 1e-12);
    for (const Formula& f : cs.UsedConstraints()) {
      EXPECT_FALSE(f.include_ode()) << f;
    }
  }
};

TEST_F(SystemCellsOdeSkipTest, Polytope) {
  const ContractorIbexPolytope ctc{FormulasWithOdeAtoms(), box_, Config{}};
  CheckCell(ctc);
}

TEST_F(SystemCellsOdeSkipTest, Acid) {
  Config config;  // --acid path: CtcAcid over the HC4 sub-contractor.
  config.mutable_use_acid().set_from_command_line(true);
  const ContractorIbexAcid ctc{FormulasWithOdeAtoms(), box_, config};
  CheckCell(ctc);
}

TEST_F(SystemCellsOdeSkipTest, ThreeBCid) {
  Config config;  // --3bcid path: same cell, Ctc3BCid shaving.
  config.mutable_use_acid().set_from_command_line(false);
  const ContractorIbexAcid ctc{FormulasWithOdeAtoms(), box_, config};
  CheckCell(ctc);
}

TEST_F(SystemCellsOdeSkipTest, Mohc) {
  Config config;
  config.mutable_use_mohc().set_from_command_line(true);
  const ContractorIbexMohc ctc{FormulasWithOdeAtoms(), box_, config};
  CheckCell(ctc);
}

TEST_F(SystemCellsOdeSkipTest, Obbt) {
  const ContractorIbexObbt ctc{FormulasWithOdeAtoms(), box_, Config{}};
  CheckCell(ctc);
}

}  // namespace
}  // namespace dreal
