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
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/solver/icp.h"  // EvaluateBox
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/rounding.h"

// Jobs-parity harness for the unified ICP loop: --jobs N vs --jobs 1 within
// the one implementation, IcpParallel (icp_parity_gaps.md R4 — the parity
// PRINCIPLE survives the IcpSeq deletion: --jobs must be a pure scheduling
// choice, same features honored, same verdicts).
//
// For every (corpus formula, jobs in {2,4}, mode in {default, acid, seed}) we
// assert (i) verdict EQUALITY against the jobs=1 run of the same formula+mode
// (and against the ground-truth verdict), repeated to shake out races —
// parallel branch order is nondeterministic, the verdict is not — and (ii) on
// delta-sat, witness validity: the returned Box must survive EvaluateBox (a
// nullopt there means an evaluator refuted the witness outright — an invalid
// delta-sat, COMPLETENESS (asserts phi^delta T-satisfiable on a T-unsatisfiable
// phi)). A jobs-split where jobs>1 says unsat on a delta-satisfiable phi would
// be SOUNDNESS (asserts phi T-unsatisfiable on a T-satisfiable phi — false
// unsat).
//
// Red-first record (pre-fix, gaps 1-2): the ODE corpus cases at jobs>1 threw
// "Parallel ODE solving is unsupported. todo: fix." (contractor.cc) and the
// acid-mode cases at jobs>1 threw "The ACID/3BCID contractor (--acid/--3bcid)
// is not implemented for parallel ICP (--jobs > 1)." — both conservative
// guards, deleted by this change. Gap 3 (--seed-samples) was CLI-red only:
// the binary threw "--seed-samples > 0 (seed-and-verify) is IcpSeq-only; not
// used with --jobs > 1." (dreal_main.cc, pre-consolidation); at the API level
// the parallel loop silently ignored seeds, so the seed cells here are parity
// regression guards (seeding is a COMPLETENESS-only speed lever and never
// moves a verdict).
//
// acid x ODE: formerly excluded — --acid on an `integral`-bearing assertion
// set threw "IbexConverter: integral constraint is not supported." at any
// job count (theory_solver passes the assertions wholesale to
// make_contractor_ibex_acid). Fixed: the system-wide cells now pre-filter
// ODE atoms out of their systems (FilterIbexConvertible,
// contractor_ibex_polytope.h; contractor_system_cells_ode_skip_test.cc), so
// these cells run as ordinary parity checks.
//
// Forall (∃∀) parity lives in forall_parallel_matrix_test.cc; not duplicated
// here.

namespace dreal {
namespace {

using std::vector;

enum class Mode { kDefault, kAcid, kSeed };

const char* ModeName(const Mode mode) {
  switch (mode) {
    case Mode::kDefault:
      return "default";
    case Mode::kAcid:
      return "acid";
    case Mode::kSeed:
      return "seed";
  }
  DREAL_UNREACHABLE();
}

struct ParityCase {
  std::string name;
  Formula formula;
  bool expect_sat;
};

vector<ParityCase> BuildCorpus() {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const Variable z{"z", Variable::Type::CONTINUOUS};

  vector<ParityCase> cases;

  // --- NRA -----------------------------------------------------------------
  // delta-SAT: circle ∩ hyperbola (e.g. x=3, y=4).
  cases.push_back({"nra_circle_hyperbola_sat",
                   0 <= x && x <= 10 && 0 <= y && y <= 10 &&
                       x * x + y * y == 25 && x * y >= 6,
                   true});
  // UNSAT: radius-1 circle cannot meet x+y >= 5.
  cases.push_back({"nra_circle_line_unsat",
                   0 <= x && x <= 10 && 0 <= y && y <= 10 &&
                       x * x + y * y == 1 && x + y >= 5,
                   false});
  // Disjunctive delta-SAT: only the x <= -3 branch admits y >= x on the circle.
  cases.push_back({"nra_disjunctive_sat",
                   -5 <= x && x <= 5 && -5 <= y && y <= 5 &&
                       (x <= -3 || x >= 3) && x * x + y * y == 10 && y >= x,
                   true});
  // Disjunctive UNSAT: x*x == 25 needs x = 5, excluded by both disjuncts.
  cases.push_back({"nra_disjunctive_unsat",
                   0 <= x && x <= 10 && (x <= 1 || x >= 9) && x * x == 25,
                   false});
  // Equality-heavy delta-SAT: x = y = 2, z = 4.
  cases.push_back({"nra_equality_sat",
                   0 <= x && x <= 5 && 0 <= y && y <= 5 && 0 <= z && z <= 5 &&
                       x + y == z && x * y == z && x == y && z >= 1,
                   true});
  // Equality-heavy UNSAT: (x+y)^2 = 12.25 but x^2+y^2+2xy = 4+8 = 12.
  cases.push_back({"nra_equality_unsat",
                   0 <= x && x <= 5 && 0 <= y && y <= 5 &&
                       x * x + y * y == 4 && x + y == 3.5 && x * y == 4,
                   false});
  // Unbounded-above variable, delta-SAT at x = 2.
  cases.push_back({"nra_unbounded_sat", x >= 0 && x * x == 4, true});
  // Unbounded-above variable, UNSAT: x*x == 4 forces x <= 2 < 3.
  cases.push_back({"nra_unbounded_unsat", x >= 3 && x * x == 4, false});
  // Transcendental delta-SAT: sin(x) = 0.5 has roots in [0, 3.2].
  cases.push_back({"nra_transcendental_sat",
                   0 <= x && x <= 3.2 && sin(x) == 0.5, true});

  // --- ODE (QF_NRA_ODE, CAPD Lohner contractor) ----------------------------
  // Trivial flow dx/dt = 0: integral reduces to X_0 ∩ X_t.
  {
    const Variable px{"parity_trivial_x", Variable::Type::CONTINUOUS};
    const Variable px0{"parity_trivial_x_0_0", Variable::Type::CONTINUOUS};
    const Variable pxt{"parity_trivial_x_0_t", Variable::Type::CONTINUOUS};
    const Variable pt{"parity_trivial_time_0", Variable::Type::CONTINUOUS};
    const auto flow = std::make_shared<OdeFlow>(
        "parity_trivial",
        vector<std::pair<Variable, Expression>>{{px, Expression{0.0}}});
    // SAT: X_0 = [0,1] ∩ X_t = [0.4,0.6] is non-empty.
    cases.push_back({"ode_trivial_sat",
                     0 <= px0 && px0 <= 1 && 0.4 <= pxt && pxt <= 0.6 &&
                         0 <= pt && pt <= 1 &&
                         integral(0.0, pt, {px0}, {pxt}, flow),
                     true});
  }
  // Linear decay dx/dt = -x, closed form x(t) = x_0 * e^{-t}, t in [0,1].
  {
    const Variable px{"parity_decay_x", Variable::Type::CONTINUOUS};
    const Variable px0{"parity_decay_x_0_0", Variable::Type::CONTINUOUS};
    const Variable pxt{"parity_decay_x_0_t", Variable::Type::CONTINUOUS};
    const Variable pt{"parity_decay_time_0", Variable::Type::CONTINUOUS};
    const auto flow = std::make_shared<OdeFlow>(
        "parity_decay", vector<std::pair<Variable, Expression>>{{px, -px}});
    // SAT: x(1) in [e^-1, 2e^-1] ≈ [0.368, 0.736] ⊂ [0.3, 0.8].
    cases.push_back({"ode_decay_sat",
                     1 <= px0 && px0 <= 2 && 0.3 <= pxt && pxt <= 0.8 &&
                         0 <= pt && pt <= 1 &&
                         integral(0.0, pt, {px0}, {pxt}, flow),
                     true});
    // UNSAT: x(t) <= x_0 <= 1 for t >= 0, disjoint from X_t = [2,3] (gap 1).
    cases.push_back({"ode_decay_unsat",
                     0 <= px0 && px0 <= 1 && 2 <= pxt && pxt <= 3 &&
                         0 <= pt && pt <= 1 &&
                         integral(0.0, pt, {px0}, {pxt}, flow),
                     false});
  }
  return cases;
}

const vector<ParityCase>& Corpus() {
  static const vector<ParityCase> corpus{BuildCorpus()};
  return corpus;
}

Config MakeConfig(const int jobs, const Mode mode) {
  Config config;
  config.mutable_precision() = 0.001;
  config.mutable_number_of_jobs() = jobs;
  // Baseline: seeding off (its Config default is ON), acid off.
  config.mutable_seed_samples() = 0;
  switch (mode) {
    case Mode::kDefault:
      break;
    case Mode::kAcid:
      config.mutable_use_acid() = true;
      break;
    case Mode::kSeed:
      config.mutable_seed_samples() = 64;
      break;
  }
  return config;
}

// Witness validity: re-check the returned delta-sat Box against every
// relational conjunct via EvaluateBox. A nullopt return means some evaluator
// refuted the box outright — an invalid witness. (Or-conjuncts are skipped —
// the relational evaluator only takes atoms — and integral conjuncts are
// skipped because the ODE evaluator is an unconditional VALID rubber-stamp,
// adding no discrimination.)
bool WitnessBoxConsistent(const Formula& f, const Box& box,
                          const double precision) {
  vector<FormulaEvaluator> evaluators;
  const FormulaSet conjuncts{is_conjunction(f) ? get_operands(f)
                                                      : FormulaSet{f}};
  for (const Formula& conjunct : conjuncts) {
    if (is_relational(conjunct)) {
      evaluators.push_back(make_relational_formula_evaluator(conjunct));
    }
  }
  ContractorStatus cs{box};
  const UpwardRoundingScope rms;
  return EvaluateBox(evaluators, box, precision, &cs, rms.token()).has_value();
}

class IcpParallelParityTest
    : public ::testing::TestWithParam<std::tuple<int /* corpus index */,
                                                 int /* jobs */, Mode>> {};

TEST_P(IcpParallelParityTest, VerdictAndWitnessParity) {
  const auto& [case_index, jobs, mode] = GetParam();
  const ParityCase& c = Corpus()[case_index];

  // jobs=1 sequential baseline of the same formula+mode.
  const Config seq_config{MakeConfig(1, mode)};
  Box seq_box{};
  const bool seq_sat{CheckSatisfiability(c.formula, seq_config, &seq_box)};
  ASSERT_EQ(seq_sat, c.expect_sat)
      << c.name << ": jobs=1 baseline disagrees with ground truth";
  if (seq_sat) {
    EXPECT_TRUE(
        WitnessBoxConsistent(c.formula, seq_box, seq_config.precision()))
        << c.name << ": jobs=1 witness box refuted by EvaluateBox:\n"
        << seq_box;
  }

  const Config par_config{MakeConfig(jobs, mode)};
  for (int rep = 0; rep < 10; ++rep) {
    Box par_box{};
    const bool par_sat{CheckSatisfiability(c.formula, par_config, &par_box)};
    ASSERT_EQ(par_sat, seq_sat)
        << c.name << ": verdict split at jobs=" << jobs
        << " mode=" << ModeName(mode) << " rep=" << rep
        << (par_sat ? " — parallel delta-sat vs seq unsat: COMPLETENESS "
                      "(asserts phi^delta T-satisfiable on a T-unsatisfiable "
                      "phi) suspect"
                    : " — parallel unsat vs seq delta-sat: SOUNDNESS (asserts "
                      "phi T-unsatisfiable on a T-satisfiable phi) suspect");
    if (par_sat) {
      EXPECT_TRUE(
          WitnessBoxConsistent(c.formula, par_box, par_config.precision()))
          << c.name << ": parallel witness box refuted by EvaluateBox at jobs="
          << jobs << " mode=" << ModeName(mode) << " rep=" << rep << ":\n"
          << par_box;
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, IcpParallelParityTest,
    ::testing::Combine(::testing::Range(0,
                                        static_cast<int>(Corpus().size())),
                       ::testing::Values(2, 4),
                       ::testing::Values(Mode::kDefault, Mode::kAcid,
                                         Mode::kSeed)),
    [](const ::testing::TestParamInfo<IcpParallelParityTest::ParamType>&
           info) {
      return Corpus()[std::get<0>(info.param)].name + "_jobs" +
             std::to_string(std::get<1>(info.param)) + "_" +
             ModeName(std::get<2>(info.param));
    });

}  // namespace
}  // namespace dreal
