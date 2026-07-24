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

// Combinatorial net for the two under-maintained features in combination:
// `forall` (∃∀ CEGIS) and parallel ICP. None of these knobs — job count, the
// --forall-pre-prune pre-pruner — may move a verdict (variable/box contraction
// and worker count are verdict-neutral). We assert verdict EQUALITY across the
// full matrix {jobs 1,2,4} × {pre-prune off,on} vs the sequential CEGIS baseline,
// repeated to shake out races (parallel branch order is nondeterministic; the
// verdict is not). Critically this includes jobs>1 WITHOUT the pre-pruner — the
// ContractorForallMt CEGIS-only parallel path, which had no prior test.

namespace dreal {
namespace {

class ForallParallelMatrixTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};
  const Variable b_{"b", Variable::Type::CONTINUOUS};
  const Variable t_{"t", Variable::Type::CONTINUOUS};

  static Config Cfg(int jobs, bool pre_prune) {
    Config config;
    config.mutable_precision() = 0.001;
    config.mutable_number_of_jobs() = jobs;
    config.mutable_use_forall_pre_prune() = pre_prune;
    return config;
  }

  // Asserts that @p f solves to @p expect_sat across every (jobs, pre-prune)
  // combination, repeatedly.
  void ExpectInvariantVerdict(const Formula& f, bool expect_sat) {
    // Sequential CEGIS baseline (jobs=1, no pre-prune).
    ASSERT_EQ(static_cast<bool>(CheckSatisfiability(f, Cfg(1, false))),
              expect_sat)
        << "baseline verdict mismatch";
    for (const int jobs : {1, 2, 4}) {
      for (const bool pre_prune : {false, true}) {
        for (int rep = 0; rep < 6; ++rep) {
          EXPECT_EQ(
              static_cast<bool>(CheckSatisfiability(f, Cfg(jobs, pre_prune))),
              expect_sat)
              << "verdict moved at jobs=" << jobs
              << " pre_prune=" << pre_prune << " rep=" << rep;
        }
      }
    }
  }
};

// δ-SAT: ∃a∈[-1,1] ∀t∈[0,1]. (t²≥0.25) ⟹ (a·t ≤ −0.1). a=−0.5 is a witness.
TEST_F(ForallParallelMatrixTest, GuardedSat) {
  const Formula body{imply(pow(t_, 2.0) >= 0.25, a_ * t_ <= -0.1)};
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0), body))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

// UNSAT: ∃a∈[1,2] ∀t∈[0,1]. a·t ≤ −0.1. At t=1 this needs a ≤ −0.1; no a∈[1,2].
TEST_F(ForallParallelMatrixTest, GuardedUnsat) {
  const Formula f{(1.0 <= a_) && (a_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                     a_ * t_ <= -0.1))};
  ExpectInvariantVerdict(f, /*expect_sat=*/false);
}

// δ-SAT, transcendental: ∃a∈[0,2] ∀t∈[0,π]. a ≥ sin(t). a≥1 works.
TEST_F(ForallParallelMatrixTest, TranscendentalSat) {
  const Formula f{(0.0 <= a_) && (a_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 3.14159265),
                                     a_ >= sin(t_)))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

// δ-SAT, two existential variables: ∃a∈[-1,1],b∈[0.5,2] ∀t∈[0,1]. a·t + b ≥ 0.
TEST_F(ForallParallelMatrixTest, MultiExistentialSat) {
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) && (0.5 <= b_) && (b_ <= 2.0) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                     a_ * t_ + b_ >= 0.0))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

// icp_parity_gaps.md G6 (R4 consolidation): with IcpSeq deleted, the CEGIS
// counterexample sub-solve (ContractorForall's context_for_counterexample_
// and ForallFormulaEvaluator's per-thread CE context, both jobs = 1) runs
// IcpParallel::CheckSat NESTED inside the outer IcpParallel::CheckSat — at
// outer jobs > 1, on a POOL-WORKER thread. New surface: the nested call
// constructs a lock-free Stack and re-enters Worker on a thread already
// attached to libcds. Safe as-is, verified two ways: (a) Worker's
// CdsScopeGuard is a block-scope thread_local, initialized only on the
// thread's FIRST pass through the declaration, so the nested pass creates no
// second attach/detach; (b) even a hypothetical re-attach is refcounted in
// libcds (ThreadData::init() bumps m_nAttachCount and only attaches at
// 0 -> 1; detach only at 1 -> 0 — vendored libcds/src/thread_data.cpp).
// Red-first is NOT constructible for this pin: pre-R4 the nested solve
// dispatched to IcpSeq (no nested CDS use at all — trivially green), and
// post-R4 the surface is safe with no code change (no defective
// intermediate state exists to catch). The cases below are the tripwire for
// future regressions on this surface — a mis-scoped guard would detach a
// live worker's hazard-pointer record and crash or hang here. The circle
// equality forces sustained outer existential branching, so pool workers
// (not just the main thread) run nested CE solves; reps + both verdict
// polarities come from ExpectInvariantVerdict's {jobs 1,2,4} matrix.
//
// δ-SAT: ∃a,b (a²+b²=0.5) ∀t∈[0,1]. a·t + b ≥ 0. Witness a=0.1, b≈0.7.
TEST_F(ForallParallelMatrixTest, NestedCeOnWorkerThreadsCdsSurfaceSat) {
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) && (-1.0 <= b_) && (b_ <= 1.0) &&
                  (a_ * a_ + b_ * b_ == 0.5) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                     a_ * t_ + b_ >= 0.0))};
  ExpectInvariantVerdict(f, /*expect_sat=*/true);
}

// UNSAT: on a²+b²=0.5, a·t+b at t=0 is b ≥ -1/√2 ≈ -0.707, so the body
// a·t + b ≤ -1.2 fails at t=0 for every (a,b) — margin ≈ 0.49 >> δ = 0.001.
TEST_F(ForallParallelMatrixTest, NestedCeOnWorkerThreadsCdsSurfaceUnsat) {
  const Formula f{(-1.0 <= a_) && (a_ <= 1.0) && (-1.0 <= b_) && (b_ <= 1.0) &&
                  (a_ * a_ + b_ * b_ == 0.5) &&
                  forall({t_}, imply((t_ >= 0.0) && (t_ <= 1.0),
                                     a_ * t_ + b_ <= -1.2))};
  ExpectInvariantVerdict(f, /*expect_sat=*/false);
}

}  // namespace
}  // namespace dreal
