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

// IcpSeq vs jobs=1 IcpParallel identity harness for the parity campaign
// (icp_parity_gaps.md R1 worklist seeding, R3 canonical seed input +
// double-root-prune drop, and the G4 arm's correctness precondition for the
// --icp-force-parallel measurement flag).
//
// Both arms run at jobs=1 and are deterministic (IcpSeq trivially;
// IcpParallel at jobs=1 spawns zero pool workers — only the main thread runs
// Worker), so the strongest assertion available from the public API surface
// is verdict EQUALITY plus, on delta-sat, witness-Box EQUALITY. Prune counts
// are not exposed (IcpStat is write-only console output; ContractorStatus
// carries no counter), so the R1 mechanism — branching_point-driven
// incremental worklist seeding reaching the parallel loop — is pinned only
// through its observable shadow: identical traversal implies identical
// verdicts and identical terminating boxes. Observing the seeding DIRECTLY
// would require exposing ContractorWorklistFixpoint's per-Prune
// seeded-contractor count (e.g. an IcpStat field or a test hook); noted, not
// built — the indirect assertion suffices for the parity gate.
//
// Red-first record (TDD): the first expected failure is a COMPILE error —
// `Config` has no member `mutable_use_icp_force_parallel` — naming the new
// flag this test wires in. The behavioral assertions can only run after the
// wiring spec lands; whether any cell is behaviorally red against a
// flag-only/partial wiring depends on how far the seq and parallel
// traversals happen to diverge on this corpus and is not guaranteed in
// advance (that unpredictability is exactly why prune counts would be the
// sharper observable).
//
// Verdict-split labeling convention (CLAUDE.md mandate): parallel unsat vs
// seq delta-sat would be SOUNDNESS-suspect (asserts phi T-unsatisfiable on a
// T-satisfiable phi — false unsat); parallel delta-sat vs seq unsat would be
// COMPLETENESS-suspect (asserts phi^delta T-satisfiable on a T-unsatisfiable
// phi — missed refutation).
//
// Corpus note: the chain_* cases are the worklist-relevant ones — three
// constraints with partially-disjoint variable supports (x,y | y,z | z,w),
// so input_to_contractors_ differs per dimension and branching_point-seeding
// genuinely skips independent contractors instead of degenerating to
// full-seed.
//
// The jobs>1 worklist cells (second suite) guard the concurrent
// Stack<pair<Box,int>> path R1 rewires; no other test runs
// --worklist-fixpoint above jobs=1 (icp_parallel_parity_test.cc's mode axis
// is {default, acid, seed}).

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

Config MakeConfig(const int jobs, const bool force_parallel,
                  const bool worklist, const bool seed) {
  Config config;
  config.mutable_precision() = 0.001;
  config.mutable_number_of_jobs() = jobs;
  config.mutable_use_icp_force_parallel() = force_parallel;
  config.mutable_use_worklist_fixpoint() = worklist;
  // Explicit either way (the Config default is ON = 64).
  config.mutable_seed_samples() = seed ? 64 : 0;
  return config;
}

// --- Suite 1: seq vs forced-parallel identity at jobs=1 ----------------------
//
// Every cell is the G4-arm correctness precondition (--icp-force-parallel at
// jobs=1 must be a pure scheduling identity); the worklist=true cells are
// additionally the R1 arm (branching_point seeding must reach the parallel
// loop) and the seed=true cells the R3 arm (both loops must propose seeds
// from the same, pruned, root box).
class IcpParallelWorklistSeedingTest
    : public ::testing::TestWithParam<
          std::tuple<int /* corpus index */, bool /* worklist */,
                     bool /* seed */>> {};

TEST_P(IcpParallelWorklistSeedingTest, SeqVsForcedParallelJobs1Identity) {
  const auto& [case_index, worklist, seed] = GetParam();
  const SeedingCase& c = Corpus()[case_index];

  // IcpSeq arm: jobs=1, flag off — today's default dispatch.
  const Config seq_config{MakeConfig(1, false, worklist, seed)};
  Box seq_box{};
  const bool seq_sat{CheckSatisfiability(c.formula, seq_config, &seq_box)};
  ASSERT_EQ(seq_sat, c.expect_sat)
      << c.name << ": IcpSeq baseline disagrees with ground truth";

  // IcpParallel arm at jobs=1 (--icp-force-parallel): deterministic (zero
  // pool workers), so identity — not mere agreement — is the contract.
  const Config par_config{MakeConfig(1, true, worklist, seed)};
  Box par_box{};
  const bool par_sat{CheckSatisfiability(c.formula, par_config, &par_box)};
  ASSERT_EQ(par_sat, seq_sat)
      << c.name << ": verdict split at jobs=1, worklist=" << worklist
      << " seed=" << seed
      << (par_sat ? " — forced-parallel delta-sat vs seq unsat: COMPLETENESS "
                    "(asserts phi^delta T-satisfiable on a T-unsatisfiable "
                    "phi) suspect"
                  : " — forced-parallel unsat vs seq delta-sat: SOUNDNESS "
                    "(asserts phi T-unsatisfiable on a T-satisfiable phi) "
                    "suspect");
  // INTEGRATION-VERIFY: bit-identical witness across IcpSeq and jobs=1
  // IcpParallel post-R1/R3 — derived from reading both loops (same LIFO pop
  // order, same left/right alternation policy, same canonical
  // root-prune-then-tagged-push shape, shared SeedBoxes/EvaluateBox/brancher
  // code); if a cell diverges, chase the residual parity gap
  // (icp_parity_gaps.md) — do not weaken this to verdict-only.
  if (seq_sat) {
    EXPECT_EQ(seq_box, par_box)
        << c.name << ": witness boxes differ at jobs=1 (worklist=" << worklist
        << " seed=" << seed << ")\nseq:\n"
        << seq_box << "\nforced-parallel:\n"
        << par_box;
  }
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
// R1 replaces the concurrent global stack's element type
// (Stack<Box> -> Stack<pair<Box, int>>); no existing test runs
// --worklist-fixpoint at jobs>1. Verdicts only — parallel branch order is
// nondeterministic, so witness identity does not apply; repeated to shake
// out races (same discipline as icp_parallel_parity_test.cc).
class IcpParallelWorklistJobs2Test
    : public ::testing::TestWithParam<int /* corpus index */> {};

TEST_P(IcpParallelWorklistJobs2Test, VerdictParity) {
  const SeedingCase& c = Corpus()[GetParam()];
  const Config config{MakeConfig(2, false, true /* worklist */, false)};
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
