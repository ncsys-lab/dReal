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
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

// Worklist-seeding (--worklist-fixpoint) and seed-and-verify (--seed-samples)
// harness for the unified ICP loop (icp_parity_gaps.md R4: IcpParallel is the
// only implementation; --jobs 1 = zero pool workers, main thread only,
// deterministic).
//
// History: this file was born as the IcpSeq vs jobs=1-IcpParallel identity
// harness for the parity campaign (R1 worklist branching_point seeding, R3
// canonical seed input + double-root-prune drop, and the G4 arm's correctness
// precondition for the --icp-force-parallel measurement flag). The R4
// consolidation deleted IcpSeq and the flag, so the old cross-implementation
// arms are ported here — not deleted — in the two forms that survive:
//   - the witness-identity assertion becomes a jobs=1 DETERMINISM pin (two
//     runs must agree byte-for-byte on verdict and witness Box), and
//   - the cross-arm verdict assertion becomes jobs=1 vs jobs=2 within the one
//     implementation.
// R4 red-first record: after the flag deletion this file's old form failed to
// COMPILE (`Config` has no member `mutable_use_icp_force_parallel`) — the
// mirror image of its original R1 red state, which was the same compile error
// for the flag's absence before the wiring landed.
//
// Prune counts are not exposed (IcpStat is write-only console output;
// ContractorStatus carries no counter), so the R1 mechanism —
// branching_point-driven incremental worklist seeding — is pinned only
// through its observable shadow: deterministic traversal implies identical
// verdicts and identical terminating boxes across runs.
//
// Verdict-split labeling convention (CLAUDE.md mandate): jobs=2 unsat vs
// jobs=1 delta-sat would be SOUNDNESS-suspect (asserts phi T-unsatisfiable on
// a T-satisfiable phi — false unsat); jobs=2 delta-sat vs jobs=1 unsat would
// be COMPLETENESS-suspect (asserts phi^delta T-satisfiable on a
// T-unsatisfiable phi — missed refutation).
//
// Corpus note: the chain_* cases are the worklist-relevant ones — three
// constraints with partially-disjoint variable supports (x,y | y,z | z,w),
// so input_to_contractors_ differs per dimension and branching_point-seeding
// genuinely skips independent contractors instead of degenerating to
// full-seed.
//
// The jobs>1 worklist cells (second suite) guard the concurrent
// Stack<pair<Box,int>> path; no other test runs --worklist-fixpoint above
// jobs=1 (icp_parallel_parity_test.cc's mode axis is {default, acid, seed}).

namespace dreal {
namespace {

using std::vector;

struct SeedingCase {
  std::string name;
  Formula formula;
  bool expect_sat;
};

vector<SeedingCase> BuildCorpus() {
  const Variable x{"x", Variable::Type::CONTINUOUS};
  const Variable y{"y", Variable::Type::CONTINUOUS};
  const Variable z{"z", Variable::Type::CONTINUOUS};
  const Variable w{"w", Variable::Type::CONTINUOUS};

  vector<SeedingCase> cases;

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
  // Chained delta-SAT: partially-disjoint supports {x,y}, {y,z}, {z,w}.
  // Witness: z=1 => w=1; y=1 => x=sqrt(3) ≈ 1.732 in [0,2].
  cases.push_back({"chain_sat",
                   0 <= x && x <= 2 && 0 <= y && y <= 2 && 0 <= z && z <= 2 &&
                       0 <= w && w <= 2 && x * x + y * y == 4 && y + z == 2 &&
                       z * w == 1,
                   true});
  // Chained UNSAT: on x*x+y*y == 4 with x,y in [0,2], max(x+y) = 2*sqrt(2)
  // ≈ 2.83 < 4 — a margin of ~1.17, far above delta = 0.001, so the
  // delta-weakening cannot admit it. The refuting subset {circle, x+y >= 4}
  // touches only dims x,y; the y+z / z*w links keep the worklist dependency
  // structure non-trivial while the refutation is found.
  cases.push_back({"chain_unsat",
                   0 <= x && x <= 2 && 0 <= y && y <= 2 && 0 <= z && z <= 2 &&
                       0 <= w && w <= 2 && x * x + y * y == 4 && y + z == 2 &&
                       z * w == 1 && x + y >= 4,
                   false});
  return cases;
}

const vector<SeedingCase>& Corpus() {
  static const vector<SeedingCase> corpus{BuildCorpus()};
  return corpus;
}

Config MakeConfig(const int jobs, const bool worklist, const bool seed) {
  Config config;
  config.mutable_precision() = 0.001;
  config.mutable_number_of_jobs() = jobs;
  config.mutable_use_worklist_fixpoint() = worklist;
  // Explicit either way (the Config default is ON = 64).
  config.mutable_seed_samples() = seed ? 64 : 0;
  return config;
}

// --- Suite 1: jobs=1 determinism + jobs=2 verdict parity ---------------------
//
// Every cell pins (a) the jobs=1 ground-truth verdict, (b) jobs=1 determinism
// — zero pool workers means run-to-run IDENTITY, not mere agreement: verdict
// plus, on delta-sat, the witness Box byte-for-byte — and (c) the jobs=2
// verdict against jobs=1. The worklist=true cells guard the R1 arm
// (branching_point seeding through the parallel stack) and the seed=true
// cells the R3 arm (seeds proposed from the canonical un-pruned root
// snapshot).
class IcpParallelWorklistSeedingTest
    : public ::testing::TestWithParam<
          std::tuple<int /* corpus index */, bool /* worklist */,
                     bool /* seed */>> {};

TEST_P(IcpParallelWorklistSeedingTest, Jobs1DeterminismAndJobs2Verdict) {
  const auto& [case_index, worklist, seed] = GetParam();
  const SeedingCase& c = Corpus()[case_index];

  // jobs=1 baseline (run A).
  const Config config_1{MakeConfig(1, worklist, seed)};
  Box box_a{};
  const bool sat_a{CheckSatisfiability(c.formula, config_1, &box_a)};
  ASSERT_EQ(sat_a, c.expect_sat)
      << c.name << ": jobs=1 baseline disagrees with ground truth";

  // jobs=1 determinism (run B): identity — not mere agreement — is the
  // contract (single thread, LIFO stack, no races).
  Box box_b{};
  const bool sat_b{CheckSatisfiability(c.formula, config_1, &box_b)};
  ASSERT_EQ(sat_b, sat_a) << c.name
                          << ": jobs=1 verdict is nondeterministic (worklist="
                          << worklist << " seed=" << seed << ")";
  if (sat_a) {
    EXPECT_EQ(box_a, box_b)
        << c.name << ": jobs=1 witness boxes differ across runs (worklist="
        << worklist << " seed=" << seed << ")\nrun A:\n"
        << box_a << "\nrun B:\n"
        << box_b;
  }

  // jobs=2 verdict parity (branch order is nondeterministic above jobs=1, so
  // witness identity does not apply).
  const Config config_2{MakeConfig(2, worklist, seed)};
  Box box_2{};
  const bool sat_2{CheckSatisfiability(c.formula, config_2, &box_2)};
  ASSERT_EQ(sat_2, sat_a)
      << c.name << ": verdict split at jobs=2, worklist=" << worklist
      << " seed=" << seed
      << (sat_2 ? " — jobs=2 delta-sat vs jobs=1 unsat: COMPLETENESS "
                  "(asserts phi^delta T-satisfiable on a T-unsatisfiable "
                  "phi) suspect"
                : " — jobs=2 unsat vs jobs=1 delta-sat: SOUNDNESS "
                  "(asserts phi T-unsatisfiable on a T-satisfiable phi) "
                  "suspect");
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, IcpParallelWorklistSeedingTest,
    ::testing::Combine(::testing::Range(0, static_cast<int>(Corpus().size())),
                       ::testing::Bool() /* worklist */,
                       ::testing::Bool() /* seed */),
    [](const ::testing::TestParamInfo<
        IcpParallelWorklistSeedingTest::ParamType>& info) {
      return Corpus()[std::get<0>(info.param)].name +
             (std::get<1>(info.param) ? "_worklist" : "_plain") +
             (std::get<2>(info.param) ? "_seed" : "_noseed");
    });

// --- Suite 2: worklist under real concurrency --------------------------------
//
// The concurrent global stack carries (Box, branched dim) pairs
// (Stack<pair<Box, int>>); no other test runs --worklist-fixpoint at jobs>1.
// Verdicts only — parallel branch order is nondeterministic, so witness
// identity does not apply; repeated to shake out races (same discipline as
// icp_parallel_parity_test.cc).
class IcpParallelWorklistJobs2Test
    : public ::testing::TestWithParam<int /* corpus index */> {};

TEST_P(IcpParallelWorklistJobs2Test, VerdictParity) {
  const SeedingCase& c = Corpus()[GetParam()];
  const Config config{MakeConfig(2, true /* worklist */, false)};
  for (int rep = 0; rep < 5; ++rep) {
    Box box{};
    const bool sat{CheckSatisfiability(c.formula, config, &box)};
    ASSERT_EQ(sat, c.expect_sat)
        << c.name << ": verdict split at jobs=2 worklist rep=" << rep
        << (sat ? " — parallel delta-sat on a ground-truth-unsat phi: "
                  "COMPLETENESS (asserts phi^delta T-satisfiable on a "
                  "T-unsatisfiable phi) suspect"
                : " — parallel unsat on a ground-truth-sat phi: SOUNDNESS "
                  "(asserts phi T-unsatisfiable on a T-satisfiable phi) "
                  "suspect");
  }
}

INSTANTIATE_TEST_SUITE_P(
    Corpus, IcpParallelWorklistJobs2Test,
    ::testing::Range(0, static_cast<int>(Corpus().size())),
    [](const ::testing::TestParamInfo<int>& info) {
      return Corpus()[info.param].name;
    });

}  // namespace
}  // namespace dreal
