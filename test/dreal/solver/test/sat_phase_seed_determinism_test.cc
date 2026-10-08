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
#include <cstdint>

#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/optional.h"

// Determinism/parity pins for the SAT layer's seed. These tests pin EXISTING
// behavior; a failure is a finding, not a test to loosen. (The file also
// covered --sat-default-phase, which no code read since the PICOSAT backend
// was retired; the flag was removed 2026-10-08.)
//
// --random-seed (default 0) reaches exactly two RNGs (grep random_seed):
//   * sat_solver.cc — CaDiCaL option "seed", set only when != 0;
//   * seed/seed.cc — the mt19937 behind LatinHypercubeSamples in the
//     seed-and-verify pre-pass (always, incl. 0; the pre-pass is ON by
//     default and fires on these pure-relational instances).
// A fixed seed must give run-to-run determinism in-process (fresh Context per
// CheckSatisfiability call, jobs=1 sequential ICP, fixed-seed mt19937,
// deterministic CaDiCaL/COBYLA). Differing seeds may legally produce
// different models, so no test compares models ACROSS seeds — and none
// asserts that differing seeds must differ (they may coincide; asserting
// difference would be flaky). Verdicts across seeds/phases are compared only
// on δ-robust instances (margin >> δ), where every search order is forced to
// the same verdict; a δ-boundary instance could legitimately answer either
// way and would make verdict parity ill-defined.

namespace dreal {
namespace {

class SatSeedDeterminismTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};

  // δ-SAT with real Boolean structure (the disjunction CNFizes through
  // Tseitin variables, so CaDiCaL makes genuine decisions): x=2, y=0.
  Formula SatCircle() const {
    return (-5.0 <= x_) && (x_ <= 5.0) && (-5.0 <= y_) && (y_ <= 5.0) &&
           (pow(x_, 2.0) + pow(y_, 2.0) == 4.0) &&
           ((x_ >= 1.0) || (y_ >= 1.0));
  }

  // δ-SAT, two-clause Boolean shape: x=1, y=1 satisfies x·y=1 with both
  // disjunctions on their right branch.
  Formula SatHyperbola() const {
    return (-5.0 <= x_) && (x_ <= 5.0) && (-5.0 <= y_) && (y_ <= 5.0) &&
           ((x_ <= -1.0) || (x_ >= 1.0)) && ((y_ <= -1.0) || (y_ >= 1.0)) &&
           (x_ * y_ == 1.0);
  }

  // UNSAT with margin >> δ: max of x+y on the unit disk is sqrt(2) < 3.
  Formula UnsatDisk() const {
    return (-5.0 <= x_) && (x_ <= 5.0) && (-5.0 <= y_) && (y_ <= 5.0) &&
           (pow(x_, 2.0) + pow(y_, 2.0) <= 1.0) && (x_ + y_ >= 3.0) &&
           ((x_ >= 0.0) || (y_ >= 0.0));
  }

  static Config Cfg(const uint32_t seed) {
    Config config;
    config.mutable_precision() = 0.001;
    config.mutable_random_seed() = seed;
    return config;
  }
};

// (a) Fixed seed: two in-process runs give identical verdicts AND identical
// model boxes. Covers seed 0 (default: CaDiCaL "seed" unset, LHS mt19937{0})
// and a nonzero seed (both RNG paths seeded).
TEST_F(SatSeedDeterminismTest, FixedSeedIsDeterministicInProcess) {
  for (const uint32_t seed : {0u, 7u}) {
    const Config config{Cfg(seed)};
    for (const Formula& f : {SatCircle(), SatHyperbola()}) {
      const optional<Box> first{CheckSatisfiability(f, config)};
      const optional<Box> second{CheckSatisfiability(f, config)};
      ASSERT_TRUE(first) << "seed=" << seed;
      ASSERT_TRUE(second) << "seed=" << seed;
      // INTEGRATION-VERIFY: in-process run-to-run box equality — derived from
      // reading the pipeline (fresh Context per call; jobs=1; fixed-seed
      // mt19937; deterministic CaDiCaL); the fresh Tseitin variable ids of the
      // second run must not perturb any behavior-bearing iteration order.
      EXPECT_EQ(*first, *second)
          << "model box moved between identical runs at seed=" << seed;
    }
    EXPECT_FALSE(CheckSatisfiability(UnsatDisk(), config));
    EXPECT_FALSE(CheckSatisfiability(UnsatDisk(), config));
  }
}

// (b) Differing seeds: only verdict agreement is well-defined (the seed is a
// search-order lever; on these δ-robust instances every order is forced to
// the same verdict). Models are NOT compared across seeds, and no assertion
// demands that differing seeds differ.
TEST_F(SatSeedDeterminismTest, SeedNeverMovesVerdict) {
  for (const uint32_t seed : {0u, 1u, 4242u}) {
    const Config config{Cfg(seed)};
    EXPECT_TRUE(CheckSatisfiability(SatCircle(), config))
        << "seed=" << seed;
    EXPECT_TRUE(CheckSatisfiability(SatHyperbola(), config))
        << "seed=" << seed;
    EXPECT_FALSE(CheckSatisfiability(UnsatDisk(), config))
        << "seed=" << seed;
  }
}

}  // namespace
}  // namespace dreal
