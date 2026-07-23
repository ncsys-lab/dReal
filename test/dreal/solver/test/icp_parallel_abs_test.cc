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
#include <stdexcept>

#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/solver/config.h"  // BrancherVariant
#include "dreal/symbolic/symbolic.h"

// IcpParallel (--jobs > 1) must drive --branch abs activity branching: each
// worker owns its own BrancherAbs (Decay/SnapshotDiams/BumpShrunk mutate the
// per-worker activity state, so an instance cannot be shared). Variable choice
// never moves a verdict, so the parallel-abs verdict must agree with the
// sequential (jobs = 1) abs run. The repeat loop shakes out data races on the
// per-worker activity state (verdicts are deterministic even though parallel
// branch order is not).
//
// TDD note: the expected first failure of this file is a COMPILE error naming
// BrancherVariant / mutable_brancher_variant (the Config slot does not exist
// before the wiring lands); the verdict assertions then pin behavior.

namespace dreal {
namespace {

class IcpParallelAbsTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};

  // jobs = 1 -> IcpSeq; jobs > 1 -> IcpParallel. kAbs active on both.
  Config AbsConfig(const int jobs) const {
    Config config;
    config.mutable_precision() = 0.001;
    config.mutable_number_of_jobs() = jobs;
    config.mutable_brancher_variant() = BrancherVariant::kAbs;
    // Isolate the brancher under test: the seed-and-verify pre-pass (default
    // on, orthogonal to branching) can solve the SAT instance before ABS ever
    // branches. --seed-samples 0 is the disable switch.
    config.mutable_seed_samples().set_from_command_line(0);
    return config;
  }
};

// δ-SAT nonlinear instance that requires branching (circle ∩ hyperbola, e.g.
// x=3,y=4): parallel-abs must agree with the sequential-abs baseline,
// repeatedly and without racing.
TEST_F(IcpParallelAbsTest, SatMatchesSequentialUnderRepeat) {
  const Formula f{0 <= x_ && x_ <= 10 && 0 <= y_ && y_ <= 10 &&
                  x_ * x_ + y_ * y_ == 25 && x_ * y_ >= 6};
  ASSERT_TRUE(CheckSatisfiability(f, AbsConfig(1)))
      << "sequential --branch abs baseline expected δ-SAT";
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(CheckSatisfiability(f, AbsConfig(2)))
        << "parallel --branch abs must be δ-SAT (iteration " << i << ")";
  }
}

// δ-UNSAT nonlinear instance: the radius-1 circle (x,y ∈ [0,1]) cannot meet
// x+y >= 5. Parallel workers join an empty box; activity branching must not
// change the refutation.
TEST_F(IcpParallelAbsTest, UnsatMatchesSequentialUnderRepeat) {
  const Formula f{0 <= x_ && x_ <= 10 && 0 <= y_ && y_ <= 10 &&
                  x_ * x_ + y_ * y_ == 1 && x_ + y_ >= 5};
  ASSERT_FALSE(CheckSatisfiability(f, AbsConfig(1)))
      << "sequential --branch abs baseline expected UNSAT";
  for (int i = 0; i < 20; ++i) {
    EXPECT_FALSE(CheckSatisfiability(f, AbsConfig(2)))
        << "parallel --branch abs must be UNSAT (iteration " << i << ")";
  }
}

// A Config with BOTH --branch abs|absdiam and --smear set has no defined
// dispatch (the three-way chain would silently prefer smear and drop abs).
// The shared Icp base ctor rejects it, so the guard is entry-path-independent:
// this test exercises the library/API path, which never passes through
// dreal_main's flag parsing, on both loops (jobs = 1 -> IcpSeq, 2 ->
// IcpParallel). The throw fires at Context construction — before any solving
// — and propagates uncaught through CheckSatisfiability (no catch anywhere in
// api.cc / context.cc / context_impl.cc).
TEST_F(IcpParallelAbsTest, ThrowsWhenBothAbsAndSmearAreSet) {
  const Formula f{0 <= x_ && x_ <= 10 && x_ * x_ == 2};
  for (const int jobs : {1, 2}) {
    Config config{AbsConfig(jobs)};
    config.mutable_smear_variant() = SmearVariant::kSum;
    EXPECT_THROW(CheckSatisfiability(f, config), std::runtime_error);
  }
}

}  // namespace
}  // namespace dreal
