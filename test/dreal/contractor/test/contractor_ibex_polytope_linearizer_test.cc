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
// Tripwire tests for --polytope-linearizer (xtaylor | affine | both) — the
// dReal-side layers 3 & 4 of the audit's mandated unit test
// (ibex_docs/affine-rounding-audit.md §6: end-to-end interior-solution
// retention incl. a near-boundary witness, a large box exercising D2, and
// the gaol-inversion canary; plus the completeness shrink canary). Layers
// 1-2 (§6.1 kernel counterexample family, §6.2 eval-containment fuzz) target
// the vendored affine ops directly and live at the ibex-fork level, not
// here.
//
// TDD-first: the expected FIRST failure of this file is a COMPILE error
// naming the new symbols — PolytopeLinearizer / Config::polytope_linearizer
// / Config::mutable_polytope_linearizer undeclared — until the wiring spec
// (scratchpad wiring_affine_selector.md) lands. New-test-file footgun: the
// suite globs at CMake configure time, so this file is compiled only after a
// reconfigure (CLAUDE.md §Running Tests; the suite count going up is the
// tell).
//
// Flag-misuse guard (--polytope-linearizer without --polytope /
// --forall-polytope) is CLI-level ONLY: the throw lives in
// MainProgram::ExtractOptions (dreal_main.cc), which links only into the
// dreal4 executable — exactly like the existing --acid/--3bcid exclusion
// throw, which likewise has no unit test. It is deliberately NOT a library
// guard: the nested forall-CE Context rewrites use_polytope from
// use_polytope_in_forall (contractor_forall.h), so any library-funnel check
// would false-positive a legitimate "--polytope + selector" exist-forall
// run. Manual check: `dreal4 --polytope-linearizer affine file.smt2` must
// exit with "--polytope-linearizer requires --polytope".
//
// Every box here is bounded on purpose: CtcPolytopeHull::contract is an
// identity on any box with an unbounded dimension (fork
// ibex_CtcPolytopeHull.cpp:61), and the affine linearizer additionally skips
// any constraint whose affine form is invalid over the box — the
// bound-every-real rule (docs/writing-fast-dreal-formulas.md). The
// UnboundedBoxIsIdentity test pins that caveat behaviorally.
#include <cmath>
#include <limits>
#include <stdexcept>

#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_ibex_polytope.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::sqrt;
using std::vector;

Config MakeConfig(const PolytopeLinearizer lin) {
  Config config;
  config.mutable_use_polytope().set_from_command_line(true);
  config.mutable_polytope_linearizer().set_from_command_line(lin);
  return config;
}

class ContractorIbexPolytopeLinearizerTest : public ::testing::Test {
 protected:
  // Builds the cell for @p lin over @p formulas / @p box and runs one Prune
  // under the phase rounding mode the production call inherits
  // (UpwardRoundingScope — contractor_ibex_polytope.cc's FE_UPWARD assert).
  ContractorStatus PruneWith(const PolytopeLinearizer lin,
                             const vector<Formula>& formulas,
                             const Box& box) {
    ContractorStatus cs{box};
    const ContractorIbexPolytope ctc{formulas, box, MakeConfig(lin)};
    EXPECT_FALSE(ctc.is_dummy());
    const UpwardRoundingScope rms_;
    ctc.Prune(&cs, rms_.token());
    return cs;
  }

  // Witness-retention helper: the enclosure must contain the real witness;
  // 1e-12 slack for the ~1-ulp outward grain (same rationale as the newton
  // test).
  static void ExpectContains(const Box::Interval& iv, const double w) {
    EXPECT_LE(iv.lb(), w + 1e-12);
    EXPECT_GE(iv.ub(), w - 1e-12);
  }

  const Variable x_{"x", Variable::Type::CONTINUOUS};
  const Variable y_{"y", Variable::Type::CONTINUOUS};
  const vector<Variable> vars_{{x_, y_}};
  Box box_{vars_};
};

// Wiring check: the selector defaults to today's only path, so an unset flag
// is behaviorally identical to the pre-feature binary.
TEST_F(ContractorIbexPolytopeLinearizerTest, DefaultIsXTaylor) {
  EXPECT_EQ(Config{}.polytope_linearizer(), PolytopeLinearizer::kXTaylor);
}

// Audit §6.3 core: on instances with KNOWN interior solutions, every
// linearizer variant must contract without ever cutting the known solution.
// Witness (0.4975, 0.4975) sits within 10*delta (delta = default 0.001) of
// the x + y <= 1 boundary — the region an under-covering row would cut
// first; (0.25, 0.25) is deep interior. A cut witness here is the SOUNDNESS
// direction (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat).
TEST_F(ContractorIbexPolytopeLinearizerTest, InteriorWitnessesRetained) {
  const vector<Formula> formulas{{x_ + y_ <= 1.0},
                                 {x_ * x_ + y_ * y_ <= 1.0}};
  box_[x_] = Box::Interval(-1.0, 1.0);
  box_[y_] = Box::Interval(-1.0, 1.0);
  for (const PolytopeLinearizer lin :
       {PolytopeLinearizer::kXTaylor, PolytopeLinearizer::kAffine,
        PolytopeLinearizer::kBoth}) {
    const ContractorStatus cs{PruneWith(lin, formulas, box_)};
    ASSERT_FALSE(cs.box().empty());
    for (const double w : {0.4975, 0.25}) {
      ExpectContains(cs.box()[x_], w);
      ExpectContains(cs.box()[y_], w);
    }
  }
}

// Audit §6.3 large-box case (exercises the D2 row-emission fix: pre-fix, the
// fixed 2^-50 slack proved rows sound only for boxes with radius <~ 4; a
// [-100, 100]^n box left an unsound over-cutting sliver per face). Witness
// (49.9, 0.5) sits near the circle boundary (49.9^2 + 0.5^2 = 2490.26 <=
// 2500); (0, 0) is deep interior.
TEST_F(ContractorIbexPolytopeLinearizerTest, AffineLargeBoxKeepsWitnesses) {
  const vector<Formula> formulas{{x_ * x_ + y_ * y_ <= 2500.0}};
  box_[x_] = Box::Interval(-100.0, 100.0);
  box_[y_] = Box::Interval(-100.0, 100.0);
  for (const PolytopeLinearizer lin :
       {PolytopeLinearizer::kAffine, PolytopeLinearizer::kBoth}) {
    const ContractorStatus cs{PruneWith(lin, formulas, box_)};
    ASSERT_FALSE(cs.box().empty());
    ExpectContains(cs.box()[x_], 49.9);
    ExpectContains(cs.box()[y_], 0.5);
    ExpectContains(cs.box()[x_], 0.0);
    ExpectContains(cs.box()[y_], 0.0);
  }
}

// Audit §6.3 gaol-inversion canary: an equality with the
// inexactly-representable constant 0.1's real solution (10*x == 1, real root
// exactly 1/10). gaol's directed rounding is sound only under the ambient
// FE_UPWARD; this is the test that fails loudly if anyone ever "fixes" the
// affine EFT rounding by wrapping linearize/eval wholesale in FE_TONEAREST
// (the audit's single most dangerous "obvious fix" — §5; cf.
// test/dreal/api/test/gaol_directed_rounding_false_unsat_test.cc for the
// failure pattern). The real root 1/10 must survive contraction.
TEST_F(ContractorIbexPolytopeLinearizerTest, GaolInversionCanaryKeepsTenth) {
  const vector<Formula> formulas{{10.0 * x_ == 1.0}};
  box_[x_] = Box::Interval(0.0, 1.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  for (const PolytopeLinearizer lin :
       {PolytopeLinearizer::kAffine, PolytopeLinearizer::kBoth}) {
    const ContractorStatus cs{PruneWith(lin, formulas, box_)};
    ASSERT_FALSE(cs.box().empty());
    // double 0.1 is within 1e-17 of the real root 1/10; the 1e-12 slack in
    // ExpectContains covers both that and the enclosure's outward grain.
    ExpectContains(cs.box()[x_], 0.1);
  }
}

// Compo semantics: kBoth ANDs the two row sets into one LP
// (ibex::LinearizerCompo — a -1 from either side propagates), so its
// feasible polytope is contained in each single linearizer's polytope and
// the certified per-variable bounds should be at least as tight.
// INTEGRATION-VERIFY (both asserts): exact nestedness of the FINAL boxes is
// not structurally forced — (a) soplex may fail to certify a bound on the
// larger LP that it certified on the smaller one (uncertified statuses are
// dropped, loosening kBoth's box on that dim), and (b) vs kXTaylor
// specifically, LinearizerXTaylor's RANDOM_OPP corner choice consumes a
// process-global RNG, so the kBoth cell's X-Taylor rows can use different
// corners than the kXTaylor cell's. The kBoth-vs-kAffine comparison shares
// no RNG and is the one expected to hold solidly; if (b) flakes, drop the
// kXTaylor comparison and keep kAffine.
TEST_F(ContractorIbexPolytopeLinearizerTest, BothIsSubsetOfEachSingle) {
  const vector<Formula> formulas{{x_ * x_ + y_ * y_ <= 1.0},
                                 {x_ + y_ >= 0.5}};
  box_[x_] = Box::Interval(-1.0, 1.0);
  box_[y_] = Box::Interval(-1.0, 1.0);
  const ContractorStatus cs_xtaylor{
      PruneWith(PolytopeLinearizer::kXTaylor, formulas, box_)};
  const ContractorStatus cs_affine{
      PruneWith(PolytopeLinearizer::kAffine, formulas, box_)};
  const ContractorStatus cs_both{
      PruneWith(PolytopeLinearizer::kBoth, formulas, box_)};
  ASSERT_FALSE(cs_xtaylor.box().empty());
  ASSERT_FALSE(cs_affine.box().empty());
  ASSERT_FALSE(cs_both.box().empty());
  for (const Variable& v : vars_) {
    EXPECT_TRUE(cs_both.box()[v].is_subset(cs_affine.box()[v]));
    // The kXTaylor comparison was dropped per the INTEGRATION-VERIFY rule
    // above. Measured at first wiring (2026-07-23): kBoth's certified
    // x.lb = -0.5 - 1ulp vs kXTaylor's exact -0.5 (y held; kAffine's lb is
    // the same -0.5 - 1ulp) — a deterministic 1-ulp certified-bound grain on
    // the merged LP, family (a); 10/10 in-process --gtest_repeat runs with
    // advancing RNG state failed identically, ruling out per-draw corner
    // luck (b) as the driver. Sound direction (looser box), so only the
    // RNG-free kBoth-vs-kAffine nesting is asserted.
  }
}

// Dependency-heavy instance (audit §6.4 completeness canary + the
// affine-vs-xtaylor comparison): x*x - x <= -0.1 on x in [0.6, 1.0]. The
// double occurrence of x is where affine forms keep the correlation interval
// evaluation loses. Real feasible set: [0.6, (1+sqrt(0.6))/2 ~ 0.887298].
// Hand-derived affine row (x = 0.8 + 0.2e; Asqr gives x^2 = 0.66 + 0.32e +/-
// 0.02): -0.04 + 0.12e <= 0.02 => e <= 0.5 => x <= 0.9 — a genuine cut from
// ub = 1.0.
//   - Soundness: ub must stay >= the real boundary 0.887298... (all real
//     solutions retained).
//   - Completeness canary (hard assert): the affine cell must actually
//     shrink ub — a silently-dead linearizer (e.g. af2.size() != nb_var
//     aborting every row) would pass every soundness assert while doing
//     nothing; that failure mode is COMPLETENESS (asserts φ^δ T-satisfiable
//     on a T-unsatisfiable φ — missed refutation), never false unsat.
//   - Strict beat over xtaylor: expected from the dependency structure but
//     not structurally forced.
TEST_F(ContractorIbexPolytopeLinearizerTest, AffineShrinksDependencyHeavy) {
  const vector<Formula> formulas{{x_ * x_ - x_ <= -0.1}};
  box_[x_] = Box::Interval(0.6, 1.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  const double real_ub{(1.0 + sqrt(0.6)) / 2.0};  // ~0.8872983346207417

  const ContractorStatus cs_affine{
      PruneWith(PolytopeLinearizer::kAffine, formulas, box_)};
  ASSERT_FALSE(cs_affine.box().empty());
  // Soundness: never below the true boundary.
  EXPECT_GE(cs_affine.box()[x_].ub(), real_ub - 1e-9);
  // Completeness canary: a real cut happened (hand-derived row gives ~0.9).
  // INTEGRATION-VERIFY: 0.95 assumes the LP certifies the hand-derived
  // affine bound within upward-rounding inflation of the Asqr error term.
  EXPECT_LT(cs_affine.box()[x_].ub(), 0.95);

  const ContractorStatus cs_xtaylor{
      PruneWith(PolytopeLinearizer::kXTaylor, formulas, box_)};
  ASSERT_FALSE(cs_xtaylor.box().empty());
  // INTEGRATION-VERIFY: affine <= xtaylor on the dependency-heavy ub. Not
  // structurally forced (RANDOM_OPP corner luck can hand xtaylor a good
  // cut); if this flakes, keep the two asserts above (they are the audit's
  // canary) and document the measured comparison instead.
  EXPECT_LE(cs_affine.box()[x_].ub(), cs_xtaylor.box()[x_].ub() + 1e-12);
}

// The affine -1 fast path: on a box the affine evaluation alone refutes
// (x + y <= -10 over [0,1]^2: ev = [10, 12] > 0 for f = x + y + 10),
// LinearizerAffine2::linearize returns -1 = infeasibility PROVEN, which
// CtcPolytopeHull maps to box.set_empty() (fork ibex_CtcPolytopeHull.cpp:
// 69->81) — surfacing through Prune's existing empty-box branch without any
// LP call. LinearizerCompo propagates the -1, so kBoth refutes identically.
// Refuting a truly infeasible box is the sound direction.
TEST_F(ContractorIbexPolytopeLinearizerTest, UnsatEmptiesViaAffineMinusOne) {
  const vector<Formula> formulas{{x_ + y_ <= -10.0}};
  box_[x_] = Box::Interval(0.0, 1.0);
  box_[y_] = Box::Interval(0.0, 1.0);
  for (const PolytopeLinearizer lin :
       {PolytopeLinearizer::kAffine, PolytopeLinearizer::kBoth}) {
    const ContractorStatus cs{PruneWith(lin, formulas, box_)};
    EXPECT_TRUE(cs.box().empty());
  }
}

// Bound-every-real caveat, pinned: CtcPolytopeHull::contract is an identity
// on any box with an unbounded dimension (fork ibex_CtcPolytopeHull.cpp:61),
// for every linearizer — the affine cell cannot bite on unbounded input. An
// identity here is a pure COMPLETENESS degradation (missed narrowing), never
// a soundness issue.
TEST_F(ContractorIbexPolytopeLinearizerTest, UnboundedBoxIsIdentity) {
  const vector<Formula> formulas{{x_ + y_ <= 1.0}};
  box_[x_] = Box::Interval(0.0, std::numeric_limits<double>::infinity());
  box_[y_] = Box::Interval(0.0, 1.0);
  const ContractorStatus cs{
      PruneWith(PolytopeLinearizer::kAffine, formulas, box_)};
  ASSERT_FALSE(cs.box().empty());
  EXPECT_EQ(cs.box()[x_].lb(), 0.0);
  EXPECT_EQ(cs.box()[x_].ub(), std::numeric_limits<double>::infinity());
  EXPECT_EQ(cs.box()[y_].lb(), 0.0);
  EXPECT_EQ(cs.box()[y_].ub(), 1.0);
}

// BUG-019 (docs/dreal-bugs.md): ibex's LPSolver::add_constraint checked
// finiteness only with asserts, which the Release IBEX build strips, so a NaN
// or infinite row reached SoPlex (it has no NaN checks) and the
// Neumaier–Shcherbina certificates, where a NaN reads as an empty interval
// and an empty d passed the infeasibility test. A non-finite row must be
// refused.
TEST(LpSolverBoundary, NonFiniteRowIsRejected) {
  ibex::LPSolver lp(2, ibex::LPSolver::Mode::Certified);
  ibex::Vector row(2);
  row[0] = 1.0;
  row[1] = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(lp.add_constraint(row, ibex::LEQ, 1.0), std::invalid_argument);
  row[1] = 0.0;
  EXPECT_THROW(lp.add_constraint(row, ibex::LEQ, std::numeric_limits<double>::infinity()),
               std::invalid_argument);
  ibex::Vector inf_row(2);
  inf_row[0] = std::numeric_limits<double>::infinity();
  inf_row[1] = 0.0;
  EXPECT_THROW(lp.add_constraint(-1.0, inf_row, 1.0), std::invalid_argument);
}

// X-Taylor's emptiness checks look only at component 0 of the gradient, so an
// empty gradient in a later component (sqrt'(y) = 0.5/sqrt(y) at y = [0, 0])
// became a NaN coefficient in an LP row. With the LP refusing such rows, the
// linearizer must skip the row itself (sound in RELAX mode): Prune must not
// throw, and the real solution x = 1, y = 0 must stay.
TEST_F(ContractorIbexPolytopeLinearizerTest, EmptyGradientComponentSkipsRow) {
  const vector<Formula> formulas{{x_ + sqrt(y_) <= 1.0}};
  box_[x_] = Box::Interval(0.0, 2.0);
  box_[y_] = Box::Interval(0.0, 0.0);
  for (const PolytopeLinearizer lin :
       {PolytopeLinearizer::kXTaylor, PolytopeLinearizer::kBoth}) {
    ContractorStatus cs{box_};
    const ContractorIbexPolytope ctc{formulas, box_, MakeConfig(lin)};
    const UpwardRoundingScope rms_;
    EXPECT_NO_THROW(ctc.Prune(&cs, rms_.token()));
    ASSERT_FALSE(cs.box().empty()) << "x = 1, y = 0 satisfies the constraint [SOUNDNESS GATE]";
    ExpectContains(cs.box()[x_], 1.0);
  }
}

}  // namespace
}  // namespace dreal
