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
#include "dreal/contractor/contractor_ibex_acid.h"

#include <cmath>

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

// Knob-consumption tests for the ACID/3BCID numeric parameters (--acid-s3b,
// --acid-ct-ratio). The companion `contractor_ibex_acid_test.cc` pins the
// contractors' soundness contract at the default knobs; these tests pin that
// the knobs actually reach ibex::CtcAcid / ibex::Ctc3BCid and what they do
// there.
//
// Parameter semantics (read from the pinned IBEX fork sources, not assumed):
//   - s3b (ibex_Ctc3BCid.h): the shaving slice count — the studied interval
//     is split into at most s3b sub-intervals, each refuted by running the
//     HC4 sub-contractor on the corresponding sub-box. Feeds BOTH CtcAcid and
//     Ctc3BCid (contractor_ibex_acid.cc passes config.acid_s3b() to either
//     ctor). s3b <= 16 (LimitCIDDichotomy) shaves linearly; larger values
//     shave dichotomically.
//   - ct_ratio (ibex_CtcAcid.h/.cpp): ACID's adaptive-stop threshold,
//     consumed ONLY by the tuning statistics. During the first 50 contract()
//     calls (nbinitcalls, per-instance state) ACID shaves ALL variables
//     regardless of ct_ratio while recording per-shave gain statistics; at
//     the end of that phase it sets nbcidvar = the rounded tuning-phase
//     average of (1 + the highest smear-rank index whose gain exceeded
//     ct_ratio) — a rank cutoff, not a count — and post-tuning calls shave
//     only nbcidvar variables. The per-shave statistic is the gain
//     (1 - diam_after/diam_before) averaged over ALL box dimensions — a dim
//     contracted to a point contributes exactly 1.0, so the average is
//     <= 1.0 — and the comparison ctstat > ct_ratio is strict, so ct_ratio =
//     1.0 is unreachable: nbcidvar tunes to 0 and a post-tuning contract()
//     does nothing at all — CtcAcid skips even its HC4 sub-contractor (the
//     initial HC4 pass is commented out upstream in CtcAcid::contract).
//
// Soundness/completeness discipline: shaving is refutation-only pruning over
// sound HC4 sub-calls, so at ANY knob value pruning a T-model out of the box
// (or emptying a T-satisfiable box) would be SOUNDNESS (asserts φ
// T-unsatisfiable on a T-satisfiable φ — false unsat). Weaker shaving (small
// s3b, disengaged ACID) costs only COMPLETENESS (asserts φ^δ T-satisfiable on
// a T-unsatisfiable φ — missed refutation).

namespace dreal {
namespace {

using std::vector;

// The CtcAcid tuning-phase length (nbinitcalls in CtcAcid::contract).
// INTEGRATION-VERIFY: matches the pinned IBEX fork (ibex_CtcAcid.cpp).
constexpr int kIbexAcidTuningCalls = 50;

class ContractorIbexAcidKnobsTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};

  // One Prune on a fresh ContractorStatus copy of box_; returns the result.
  Box PruneOnce(const ContractorIbexAcid& ctc) {
    ContractorStatus cs{box_};
    { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
    return cs.box();
  }
};

// (f) Two shave depths — s3b=2 (linear shaving, 50% slices) and s3b=50
// (dichotomic shaving, > LimitCIDDichotomy=16) — must both satisfy the ACID
// soundness contract on the SAT instance x*x == 4: non-empty, witness x=2
// retained, and at least as strong as a plain HC4 forward-backward pass
// (shaving only refutes sub-slices via HC4 on sub-boxes, so its result is a
// subset of one HC4 pass — the AtLeastAsStrongAsHc4 helper pattern).
TEST_F(ContractorIbexAcidKnobsTest, S3bDepths_AtLeastAsStrongAsHc4AndSound) {
  const Formula f{x_ * x_ == 4.0};
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 1.0);

  ContractorStatus cs_hc4{box_};
  const ContractorIbexFwdbwd hc4{f, box_, Config{}};
  { const UpwardRoundingScope rms_; hc4.Prune(&cs_hc4, rms_.token()); }
  ASSERT_FALSE(cs_hc4.box().empty());

  for (const int s3b : {2, 50}) {
    Config config;
    config.mutable_use_acid().set_from_command_line(true);
    config.mutable_acid_s3b().set_from_command_line(s3b);
    const ContractorIbexAcid acid{{f}, box_, config};
    ASSERT_FALSE(acid.is_dummy());
    const Box b = PruneOnce(acid);
    ASSERT_FALSE(b.empty())
        << "s3b=" << s3b
        << " emptied a SAT box [SOUNDNESS GATE: false unsat]";
    EXPECT_TRUE(b[x_].contains(2.0))
        << "s3b=" << s3b
        << " pruned out the T-model x=2 [SOUNDNESS GATE: false unsat by "
           "over-pruning a real solution]";
    EXPECT_TRUE(b[x_].is_subset(cs_hc4.box()[x_]))
        << "s3b=" << s3b
        << ": shaving applies HC4 on sub-boxes; its box must be a subset of "
           "one HC4 pass";
  }
}

// (f) actually-changes-behavior: contraction must measurably depend on s3b.
// Instance: x^2 + y^2 == 1  ∧  x == y on [-1,1]^2 (solutions x = y =
// ±1/sqrt(2) ≈ ±0.70711). Plain HC4 is a fixpoint no-op here (backward
// x^2 = 1 - y^2 ∈ [0,1] gives x ∈ [-1,1]: no gain), so ALL contraction is
// shaving-depth-dependent — the instance isolates s3b.
//
// The 3BCID variant is used because Ctc3BCid carries no adaptive state
// (vhandled = all variables every call), making one Prune a deterministic
// function of s3b.
//
// Slice arithmetic (hand-traced against ibex_Ctc3BCid.cpp var3BCID_slices):
//   - s3b=10 (default): 10%-wide slices. The outer slices [-1,-0.8] / [0.8,1]
//     empty under HC4 (x==y forces both vars into the slice; x²+y² ∈ [1.28,2]
//     excludes 1), the central CID slice empties too, and the surviving hull
//     is bounded by the slice grid: |x| <= 0.8 (+outward rounding); with the
//     stronger sqr projection it tightens to sqrt(0.59) ≈ 0.7684.
//   - s3b=2: 50%-wide slices [-1,0] / [0,1] BOTH survive immediately (sum
//     range [0,2] contains 1), hitting the adjacent-slices path (box = left
//     hull right) — the box comes back unchanged: no contraction at all.
TEST_F(ContractorIbexAcidKnobsTest, S3bChangesContraction_3bcid) {
  const Formula circle{x_ * x_ + y_ * y_ == 1.0};
  const Formula line{x_ == y_};
  box_[x_] = Box::Interval(-1.0, 1.0);
  box_[y_] = Box::Interval(-1.0, 1.0);
  const double kRoot = std::sqrt(0.5);  // ≈ 0.70711

  Config config_default;
  config_default.mutable_use_3bcid().set_from_command_line(true);
  const ContractorIbexAcid ctc_default{{circle, line}, box_, config_default};
  ASSERT_FALSE(ctc_default.is_dummy());
  const Box b_default = PruneOnce(ctc_default);  // s3b = 10 (kDefaultAcidS3b)

  Config config_shallow;
  config_shallow.mutable_use_3bcid().set_from_command_line(true);
  config_shallow.mutable_acid_s3b().set_from_command_line(2);
  const ContractorIbexAcid ctc_shallow{{circle, line}, box_, config_shallow};
  ASSERT_FALSE(ctc_shallow.is_dummy());
  const Box b_shallow = PruneOnce(ctc_shallow);

  // Neither depth may falsely empty the SAT box, and both solutions
  // x = y = ±1/sqrt(2) must survive [SOUNDNESS GATE: false unsat].
  ASSERT_FALSE(b_default.empty()) << "s3b=10 emptied a SAT box";
  ASSERT_FALSE(b_shallow.empty()) << "s3b=2 emptied a SAT box";
  for (const Box* b : {&b_default, &b_shallow}) {
    EXPECT_TRUE((*b)[x_].contains(kRoot)) << "pruned out x = +1/sqrt(2)";
    EXPECT_TRUE((*b)[x_].contains(-kRoot)) << "pruned out x = -1/sqrt(2)";
  }

  // Default depth contracts: hull bounded by the slice grid at |x| <= 0.8.
  EXPECT_LT(b_default[x_].ub(), 0.85)
      << "s3b=10 must shave the outer slices down to |x| <= ~0.8";  // INTEGRATION-VERIFY: hand-traced slice bound (0.8 weak-projection / 0.7684 sqr-projection)
  EXPECT_GT(b_default[x_].lb(), -0.85)
      << "s3b=10 must shave the outer slices down to |x| <= ~0.8";
  EXPECT_GT(b_default[x_].ub(), 0.70)
      << "s3b=10 must not shave past the solution 1/sqrt(2)";

  // Shallow depth does not contract at all (both 50% slices survive).
  EXPECT_GT(b_shallow[x_].ub(), 0.999)
      << "s3b=2's 50% slices both survive; the box must come back unchanged";
  EXPECT_LT(b_shallow[x_].lb(), -0.999)
      << "s3b=2's 50% slices both survive; the box must come back unchanged";

  // The measurable difference itself: deeper shaving contracts strictly more.
  EXPECT_LT(b_default[x_].ub(), b_shallow[x_].ub())
      << "s3b=10 and s3b=2 contracted identically — --acid-s3b looks like a "
         "silent no-op";
}

// (g) --acid-ct-ratio boundary behavior. ct_ratio's consumption point is the
// ACID tuning statistics (see the header comment): during the 50-call tuning
// phase shaving runs on all variables REGARDLESS of ct_ratio; afterwards, a
// ratio the per-shave statistic can never exceed (1.0 — the statistic is a
// <= 1.0 average over the box dims, compared with a strict >) tunes
// nbcidvar to 0, and post-tuning contract() calls become a pure no-op
// (not even the HC4 sub-contractor runs). The default ratio (0.002) keeps the
// gainful variable engaged past tuning.
//
// Disengaged shaving is a pure COMPLETENESS lever (weaker contraction /
// missed refutation at worst); both configurations must retain the T-model
// x=2 throughout [SOUNDNESS GATE].
TEST_F(ContractorIbexAcidKnobsTest, CtRatioUnreachable_DisengagesAfterTuning) {
  const Formula f{x_ * x_ == 4.0};
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 1.0);

  Config config_hi;
  config_hi.mutable_use_acid().set_from_command_line(true);
  config_hi.mutable_acid_ct_ratio().set_from_command_line(1.0);  // unreachable
  const ContractorIbexAcid ctc_hi{{f}, box_, config_hi};
  ASSERT_FALSE(ctc_hi.is_dummy());

  // Tuning-phase call #1 shaves all variables regardless of ct_ratio: it
  // must contract (this pins that ct_ratio does NOT gate the tuning phase).
  {
    const Box b = PruneOnce(ctc_hi);
    ASSERT_FALSE(b.empty());
    EXPECT_TRUE(b[x_].contains(2.0));
    EXPECT_LT(b[x_].ub(), 9.0)
        << "tuning-phase shaving runs regardless of ct_ratio and must "
           "contract x from [0,10]";
  }
  // Burn the rest of the tuning phase (per-instance nbcalls state; each call
  // re-contracts a fresh copy of the same box, so every tuning call records
  // the same sub-ct_ratio gains).
  for (int i = 1; i < kIbexAcidTuningCalls; ++i) {
    PruneOnce(ctc_hi);
  }
  // Post-tuning call: nbcidvar tuned to 0 -> contract() is an identity. The
  // box must come back bit-identical (a pure no-op, not merely weaker).
  {
    const Box b = PruneOnce(ctc_hi);
    ASSERT_FALSE(b.empty());
    EXPECT_TRUE(b[x_].contains(2.0));
    EXPECT_EQ(b[x_].lb(), 0.0)
        << "ct_ratio=1.0 post-tuning: adaptive shaving must be disengaged "
           "(nbcidvar=0 -> no shaving, no sub-HC4)";  // INTEGRATION-VERIFY: identity (not an HC4-only pass) derived from CtcAcid::contract skipping the loop at vhandled=0 with the initial HC4 call commented out upstream
    EXPECT_EQ(b[x_].ub(), 10.0)
        << "ct_ratio=1.0 post-tuning: contract() must be a pure no-op";
    EXPECT_EQ(b[y_].lb(), 0.0);
    EXPECT_EQ(b[y_].ub(), 1.0);
  }

  // Control: at the default ct_ratio (0.002) the x-shave's large average gain
  // keeps nbcidvar >= 1, so the same post-tuning call still contracts.
  Config config_default;
  config_default.mutable_use_acid().set_from_command_line(true);
  const ContractorIbexAcid ctc_default{{f}, box_, config_default};
  ASSERT_FALSE(ctc_default.is_dummy());
  for (int i = 0; i < kIbexAcidTuningCalls; ++i) {
    PruneOnce(ctc_default);
  }
  {
    const Box b = PruneOnce(ctc_default);
    ASSERT_FALSE(b.empty());
    EXPECT_TRUE(b[x_].contains(2.0));
    EXPECT_LT(b[x_].ub(), 9.0)
        << "default ct_ratio must keep the gainful variable engaged past the "
           "tuning phase";
  }
}

}  // namespace
}  // namespace dreal
