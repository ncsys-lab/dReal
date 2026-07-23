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
#include "dreal/solver/brancher_abs.h"

#include <limits>
#include <stdexcept>
#include <vector>

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/solver/brancher.h"  // BranchLargestFirst
#include "dreal/solver/config.h"    // BrancherVariant
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

// TDD note: the expected first failure of this file is a COMPILE error naming
// BrancherAbs / BrancherVariant (neither exists before brancher_abs.{h,cc} and
// the config.h wiring land); the assertions below then pin behavior.

namespace dreal {
namespace {

using std::vector;

class BrancherAbsTest : public ::testing::Test {
 protected:
  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const Variable z_{"z", Variable::Type::CONTINUOUS};

  // Feeds `brancher` one Prune-shrink event for exactly the dims in `dims`:
  // snapshot `box`, then diff against a copy with each listed dim collapsed to
  // a point (a strict diameter shrink). Activity accrues only through the real
  // snapshot/diff path — there is no test-only activity setter.
  static void BumpDims(BrancherAbs* const brancher, const Box& box,
                       const vector<Variable>& dims, const UpwardRounding& ur) {
    brancher->SnapshotDiams(box, ur);
    Box shrunk{box};
    for (const Variable& v : dims) {
      shrunk[v] = Box::Interval(box[v].lb(), box[v].lb());
    }
    brancher->BumpShrunk(shrunk, ur);
  }

  // Runs `brancher` on `box` with every dim active; returns the chosen dim.
  static int Choose(const BrancherAbs& brancher, const Box& box,
                    const UpwardRounding& ur) {
    DynamicBitset active(box.size());
    for (int i = 0; i < static_cast<int>(box.size()); ++i) active.set(i);
    Box left{box};
    Box right{box};
    return brancher(box, active, &left, &right, ur);
  }

  // x is 10x wider than y; z sits in between.
  Box MakeBox() const {
    Box box{{x_, y_, z_}};
    box[x_] = Box::Interval(-10, 10);  // width 20
    box[y_] = Box::Interval(-1, 1);    // width 2
    box[z_] = Box::Interval(0, 5);     // width 5
    return box;
  }
};

// Both variants must pick the high-ACTIVITY variable, not merely the widest:
// A = [1, 25, 0] on widths [20, 2, 5] gives
//   abs:     x = 1/20 = 0.05, y = 25/2 = 12.5, z = 0  -> y
//   absdiam: x = 1*20 = 20,   y = 25*2 = 50,   z = 0  -> y
// Largest-first would pick x, so this distinguishes ABS from the default
// brancher.
TEST_F(BrancherAbsTest, PicksHighActivityVariableNotWidest) {
  const Box box{MakeBox()};
  const UpwardRoundingScope rms;
  for (const BrancherVariant variant :
       {BrancherVariant::kAbs, BrancherVariant::kAbsDiam}) {
    BrancherAbs brancher{box, variant, 0.999};
    BumpDims(&brancher, box, {x_, y_}, rms.token());  // A = [1, 1, 0]
    for (int i = 0; i < 24; ++i) {
      BumpDims(&brancher, box, {y_}, rms.token());  // A = [1, 25, 0]
    }
    EXPECT_EQ(Choose(brancher, box, rms.token()), box.index(y_))
        << "high-activity y must beat the 10x-wider x";
  }
}

// THE UNIFORM-A PROPERTY, absdiam arm: with all activities equal and nonzero
// the activity factor cancels (score = A*diam ∝ diam), so absdiam must choose
// exactly BranchLargestFirst's dimension — the degeneracy hypothesis,
// mechanized against the real default brancher.
TEST_F(BrancherAbsTest, UniformActivityAbsDiamMatchesBranchLargestFirst) {
  const Box box{MakeBox()};
  const UpwardRoundingScope rms;
  BrancherAbs brancher{box, BrancherVariant::kAbsDiam, 0.999};
  BumpDims(&brancher, box, {x_, y_, z_}, rms.token());
  BumpDims(&brancher, box, {x_, y_, z_}, rms.token());  // uniform A = 2

  DynamicBitset active(box.size());
  for (int i = 0; i < static_cast<int>(box.size()); ++i) active.set(i);
  Box left{box};
  Box right{box};
  const int blf_dim{
      BranchLargestFirst(box, active, &left, &right, rms.token())};
  EXPECT_EQ(Choose(brancher, box, rms.token()), blf_dim)
      << "uniform-A absdiam must degenerate to largest-first";
  EXPECT_EQ(blf_dim, box.index(x_));  // sanity: largest-first picks x (20)
}

// THE UNIFORM-A PROPERTY, abs arm: score = A/diam ∝ 1/diam, so abs must choose
// the smallest-diam dim: x = 2/20 = 0.1, y = 2/2 = 1, z = 2/5 = 0.4 -> y.
TEST_F(BrancherAbsTest, UniformActivityAbsPicksSmallestDiam) {
  const Box box{MakeBox()};
  const UpwardRoundingScope rms;
  BrancherAbs brancher{box, BrancherVariant::kAbs, 0.999};
  BumpDims(&brancher, box, {x_, y_, z_}, rms.token());
  BumpDims(&brancher, box, {x_, y_, z_}, rms.token());  // uniform A = 2
  EXPECT_EQ(Choose(brancher, box, rms.token()), box.index(y_))
      << "uniform-A abs must degenerate to smallest-diam-first";
}

// All-zero activity (the initial state) scores every dim 0 for BOTH variants,
// so the larger-diameter tie-break makes ABS start exactly as largest-first.
TEST_F(BrancherAbsTest, AllZeroActivityStartsAsLargestFirst) {
  const Box box{MakeBox()};
  const UpwardRoundingScope rms;
  for (const BrancherVariant variant :
       {BrancherVariant::kAbs, BrancherVariant::kAbsDiam}) {
    const BrancherAbs brancher{box, variant, 0.999};
    EXPECT_EQ(Choose(brancher, box, rms.token()), box.index(x_))
        << "all-zero activity must fall to the diameter tie-break (x)";
  }
}

// The largest-first start must extend to UNBOUNDED dims: with all-zero
// activity, kAbsDiam's raw score on [0, inf) is 0 * inf = NaN — this is the
// only test that reaches the scorer's NaN->0 clamp — while kAbs's is
// 0 / inf = 0. Both fall to the diameter tie-break, where diam = inf beats
// any finite width, so the unbounded dim is chosen. ([0, inf) IS bisectable:
// gaol's mid() of an unbounded interval is finite, so lb < mid < ub.)
TEST_F(BrancherAbsTest, AllZeroActivityPicksUnboundedDim) {
  Box box{{x_, y_}};
  box[x_] = Box::Interval(-1, 1);  // width 2
  box[y_] = Box::Interval(0, std::numeric_limits<double>::infinity());
  const UpwardRoundingScope rms;
  for (const BrancherVariant variant :
       {BrancherVariant::kAbs, BrancherVariant::kAbsDiam}) {
    const BrancherAbs brancher{box, variant, 0.999};
    EXPECT_EQ(Choose(brancher, box, rms.token()), box.index(y_))
        << "the unbounded y (diam = inf) must win the diameter tie-break";
  }
}

// Decay crossover, exact arithmetic (gamma = 0.5; equal widths so only
// activity decides): stale A_x = 3, then k decays, then a fresh A_y = 1.
//   k = 1: A_x = 1.5 > 1    -> x still wins.
//   k = 2: A_x = 0.75 < 1   -> the freshly-bumped y wins.
// Powers of 0.5 are exact in binary floating point, so both comparisons are
// exact; the scores 3*0.5^k/2 vs 1/2 are never equal for k >= 1, so the
// tie-break never engages.
TEST_F(BrancherAbsTest, DecayCrossoverExactHalving) {
  Box box{{x_, y_}};
  box[x_] = Box::Interval(0, 2);
  box[y_] = Box::Interval(0, 2);
  const UpwardRoundingScope rms;
  for (const int k : {1, 2}) {
    BrancherAbs brancher{box, BrancherVariant::kAbs, 0.5};
    for (int i = 0; i < 3; ++i) BumpDims(&brancher, box, {x_}, rms.token());
    for (int i = 0; i < k; ++i) brancher.Decay();
    BumpDims(&brancher, box, {y_}, rms.token());
    EXPECT_EQ(Choose(brancher, box, rms.token()),
              k == 1 ? box.index(x_) : box.index(y_))
        << "crossover must happen between k=1 and k=2 decays";
  }
}

// The same crossover at the shipped default gamma = 0.999: 3*gamma^k drops
// below 1 first at k = 1099 (3*0.999^1098 ~ 1.000063 > 1 > 3*0.999^1099 ~
// 0.999063; the ~6e-5 decision margin dwarfs the ~1e-13 relative error of
// 1099 rounded multiplications).
TEST_F(BrancherAbsTest, DecayCrossoverDefaultGamma) {
  Box box{{x_, y_}};
  box[x_] = Box::Interval(0, 2);
  box[y_] = Box::Interval(0, 2);
  const UpwardRoundingScope rms;
  for (const int k : {1098, 1099}) {
    BrancherAbs brancher{box, BrancherVariant::kAbs, 0.999};
    for (int i = 0; i < 3; ++i) BumpDims(&brancher, box, {x_}, rms.token());
    for (int i = 0; i < k; ++i) brancher.Decay();
    BumpDims(&brancher, box, {y_}, rms.token());
    EXPECT_EQ(Choose(brancher, box, rms.token()),
              k == 1098 ? box.index(x_) : box.index(y_))
        << "crossover must happen between k=1098 and k=1099 decays";
  }
}

// Equal scores, different diameters -> the larger diameter wins. abs with A
// proportional to diam: A_x = 2 on width 2, A_y = 4 on width 4 -> both score
// exactly 1.0. The wider y (the HIGHER index) must win, isolating the
// diameter tie-break from the lower-index one.
TEST_F(BrancherAbsTest, TieBreakPrefersLargerDiam) {
  Box box{{x_, y_}};
  box[x_] = Box::Interval(0, 2);  // width 2
  box[y_] = Box::Interval(0, 4);  // width 4
  const UpwardRoundingScope rms;
  BrancherAbs brancher{box, BrancherVariant::kAbs, 0.999};
  BumpDims(&brancher, box, {x_, y_}, rms.token());
  BumpDims(&brancher, box, {x_, y_}, rms.token());  // A = [2, 2]
  BumpDims(&brancher, box, {y_}, rms.token());
  BumpDims(&brancher, box, {y_}, rms.token());  // A = [2, 4]
  EXPECT_EQ(Choose(brancher, box, rms.token()), box.index(y_))
      << "score tie (1.0 == 1.0) must break toward the wider y";
}

// Equal scores AND equal diameters -> lower index wins. All-zero activity on
// two equal-width dims plus a narrower z: every score is 0, x and y tie on
// diameter, and the lower index (x) is chosen.
TEST_F(BrancherAbsTest, TieBreakFallsToLowerIndex) {
  Box box{{x_, y_, z_}};
  box[x_] = Box::Interval(0, 2);
  box[y_] = Box::Interval(0, 2);
  box[z_] = Box::Interval(0, 1);
  const UpwardRoundingScope rms;
  const BrancherAbs brancher{box, BrancherVariant::kAbsDiam, 0.999};
  EXPECT_EQ(Choose(brancher, box, rms.token()), box.index(x_));
  EXPECT_EQ(box.index(x_), 0);  // INTEGRATION-VERIFY: Box{{x_, y_, z_}} assigns indices in declaration order
}

// The chosen dimension is bisected at the midpoint; the others are untouched
// (the same split contract as BranchLargestFirst).
TEST_F(BrancherAbsTest, SplitsChosenDimAtMidpoint) {
  const Box box{MakeBox()};
  const UpwardRoundingScope rms;
  BrancherAbs brancher{box, BrancherVariant::kAbs, 0.999};
  BumpDims(&brancher, box, {y_}, rms.token());
  DynamicBitset active(box.size());
  for (int i = 0; i < static_cast<int>(box.size()); ++i) active.set(i);
  Box left{box};
  Box right{box};
  const int dim{brancher(box, active, &left, &right, rms.token())};
  ASSERT_EQ(dim, box.index(y_));  // the only nonzero-activity dim
  EXPECT_EQ(left[x_], box[x_]);
  EXPECT_EQ(right[x_], box[x_]);
  EXPECT_EQ(left[z_], box[z_]);
  EXPECT_EQ(right[z_], box[z_]);
  EXPECT_EQ(left[y_].lb(), box[y_].lb());
  EXPECT_EQ(right[y_].ub(), box[y_].ub());
  EXPECT_EQ(left[y_].ub(), right[y_].lb());  // contiguous split (midpoint)
}

// A point (degenerate) interval is not bisectable; with no bisectable dim in
// the active set the brancher returns -1 (and never calls Box::bisect, which
// would throw).
TEST_F(BrancherAbsTest, ReturnsMinusOneWhenNoDimBisectable) {
  Box box{{x_}};
  box[x_] = Box::Interval(1, 1);  // INTEGRATION-VERIFY: ibex is_bisectable([1,1]) == false
  const UpwardRoundingScope rms;
  const BrancherAbs brancher{box, BrancherVariant::kAbs, 0.999};
  DynamicBitset active(box.size());
  active.set(0);
  Box left{box};
  Box right{box};
  EXPECT_EQ(brancher(box, active, &left, &right, rms.token()), -1);
}

// A decay outside (0, 1] is a misconfiguration; the ctor fails loud.
TEST_F(BrancherAbsTest, ThrowsOnDecayOutOfRange) {
  Box box{{x_}};
  box[x_] = Box::Interval(0, 1);
  EXPECT_THROW((BrancherAbs{box, BrancherVariant::kAbs, 0.0}),
               std::runtime_error);
  EXPECT_THROW((BrancherAbs{box, BrancherVariant::kAbs, 1.5}),
               std::runtime_error);
}

// BumpShrunk without a preceding (unconsumed) SnapshotDiams would silently
// diff against stale/absent diameters; the protocol violation fails loud.
TEST_F(BrancherAbsTest, ThrowsOnBumpWithoutSnapshot) {
  Box box{{x_}};
  box[x_] = Box::Interval(0, 1);
  const UpwardRoundingScope rms;
  BrancherAbs brancher{box, BrancherVariant::kAbs, 0.999};
  EXPECT_THROW(brancher.BumpShrunk(box, rms.token()), std::runtime_error);
  brancher.SnapshotDiams(box, rms.token());
  brancher.BumpShrunk(box, rms.token());  // consumes the snapshot
  EXPECT_THROW(brancher.BumpShrunk(box, rms.token()), std::runtime_error);
}

}  // namespace
}  // namespace dreal
