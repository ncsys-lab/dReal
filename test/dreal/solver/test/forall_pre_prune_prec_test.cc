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
#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"

// --forall-pre-prune-prec is the universal-box bisection precision of the
// ibex::CtcForAll pre-pruner (contractor_ibex_forall.cc feeds it straight into
// the CtcForAll ctor). The pre-pruner is claimed verdict-neutral — a pure
// proj-intersection contraction beside the δ-complete CEGIS decider,
// COMPLETENESS-only (a missed refutation at worst — asserts φ^δ T-satisfiable
// on a T-unsatisfiable φ — never a false `unsat` (SOUNDNESS)). This test pins
// exactly that claim ACROSS the prec knob: coarse (0.5, the default —
// Config::kDefaultForallPrePrunePrec) vs fine (0.05) must agree with each
// other, sequentially and under parallel ICP (--jobs 2, one CtcForAll per
// worker via ContractorIbexForallMt), and with the pre-pruner OFF entirely.
// A fine prec is a known *perf* hazard on real MLP instances (the
// kDefaultForallPrePrunePrec rationale in config.h) — never a verdict lever;
// on these toy ∃∀ instances (shared with forall_parallel_matrix_test.cc /
// icp_parallel_forall_pre_prune_test.cc) it is cheap. Mirrors the
// forall_parallel_matrix repeated-verdict pattern (parallel branch order is
// nondeterministic; the verdict is not).

namespace dreal {
namespace {

class ForallPrePrunePrecTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};  // existential
  const Variable t_{"t", Variable::Type::CONTINUOUS};  // universal (∀-bound)

  static Config Cfg(const bool pre_prune, const double prec, const int jobs) {
    Config config;
    config.mutable_precision() = 0.001;
    config.mutable_number_of_jobs() = jobs;
    config.mutable_use_forall_pre_prune() = pre_prune;
    config.mutable_forall_pre_prune_prec() = prec;
    return config;
  }

  // Asserts @p f solves to @p expect_sat across the whole
  // {prec coarse,fine} × {jobs 1,2} pre-prune matrix, repeatedly, and that
  // every cell agrees with the pre-prune-OFF baseline (prec is inert there —
  // the CtcForAll is never built).
  void ExpectInvariantVerdict(const Formula& f, const bool expect_sat) {
    for (const int jobs : {1, 2}) {
      ASSERT_EQ(static_cast<bool>(CheckSatisfiability(
                    f, Cfg(false, Config::kDefaultForallPrePrunePrec, jobs))),
                expect_sat)
          << "pre-prune-OFF baseline verdict mismatch at jobs=" << jobs;
    }
    for (const double prec : {Config::kDefaultForallPrePrunePrec, 0.05}) {
      for (const int jobs : {1, 2}) {
        for (int rep = 0; rep < 6; ++rep) {
          EXPECT_EQ(
              static_cast<bool>(CheckSatisfiability(f, Cfg(true, prec, jobs))),
              expect_sat)
              << "verdict moved at prec=" << prec << " jobs=" << jobs
              << " rep=" << rep;
        }
      }
    }
  }
};

// δ-SAT: ∃a∈[-1,1] ∀t∈[0,1]. (t²≥0.25) ⟹ (a·t ≤ −0.1). The guard makes
// t<0.5 vacuous, so a=−0.5 is a true witness. A finer prec bisects the
// universal box deeper — it may only contract the existential box HARDER, and
// must keep the witness.
TEST_F(ForallPrePrunePrecTest, GuardedSat) {
  const Formula body{imply(pow(t_, 2.0) >= 0.25, a_ * t_ <= -0.1)};
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0), body))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

// UNSAT: ∃a∈[1,2] ∀t∈[0,1]. a·t ≤ −0.1. At t=1 this needs a ≤ −0.1; no
// a∈[1,2]. For an unsat goal the prec inverts (finer ⇒ stronger refutation;
// config.h) — but the CEGIS decider refutes regardless, so both precs and the
// OFF baseline must all say unsat.
TEST_F(ForallPrePrunePrecTest, GuardedUnsat) {
  const Formula f{(1.0 <= a_) && (a_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                     a_ * t_ <= -0.1))};
  ExpectInvariantVerdict(f, /*expect_sat=*/false);
}

// δ-SAT, transcendental: ∃a∈[0,2] ∀t∈[0,π]. a ≥ sin(t). a≥1 works. Exercises
// the prec knob on a non-polynomial inner contraction.
TEST_F(ForallPrePrunePrecTest, TranscendentalSat) {
  const Formula f{(0.0 <= a_) && (a_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 3.14159265),
                                     a_ >= sin(t_)))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

}  // namespace
}  // namespace dreal
