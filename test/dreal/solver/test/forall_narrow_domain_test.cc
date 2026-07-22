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
#include "dreal/symbolic/symbolic.h"

// H1 (COMPLETENESS — missed refutation → false `delta-sat`): the CE-search query
// is built as DeltaStrengthen(¬(domain → body), ε). Because ¬(domain → body) =
// domain ∧ ¬body and DeltaStrengthen distributes over ∧, the universal-domain
// bound atoms are ALSO ε-tightened. A universal domain narrower than ~2ε — or a
// point domain — is therefore strengthened to the EMPTY set, so the inner solve
// finds no counterexample and the forall is reported satisfied even when it is
// robustly violated. The fix strengthens only the matrix body and keeps the
// universal-domain bounds exact (Kong/Solar-Lezama/Gao CAV-2018).
//
// Each instance below is robustly UNSAT (no δ-model: the existential bound and
// the universal obligation are separated by a margin ≫ δ), so the correct
// verdict is UNSAT. Before the fix the solver returns δ-SAT (the bug); after the
// fix it refutes. These are SOUNDNESS-preserving for the fix (UNSAT was always
// the true answer); the bug was a completeness hole, not a false `unsat`.

namespace dreal {
namespace {

class ForallNarrowDomainTest : public ::testing::Test {
 protected:
  const Variable a_{"a", Variable::Type::CONTINUOUS};  // existential
  const Variable t_{"t", Variable::Type::CONTINUOUS};  // universal (∀-bound)
};

// ∃a∈[-1,0] ∀t∈[0.4999, 0.5001]. a ≥ t.
// ∀t requires a ≥ sup t = 0.5001; with a ≤ 0 this is impossible even δ-relaxed
// (need a ≥ 0.5001 − δ ≈ 0.4991 ≫ 0). True verdict: UNSAT. The universal domain
// has width 2e-4 ≪ 2ε (ε = δ/2 = 5e-4 for the contractor), so the buggy strengthening
// empties it and the violation at, e.g., t = 0.5 is never searched.
TEST_F(ForallNarrowDomainTest, NarrowIntervalDomainIsRefuted) {
  const Formula domain{(t_ >= 0.4999) && (t_ <= 0.5001)};
  const Formula f{(-1.0 <= a_) && (a_ <= 0.0) &&
                  forall({t_}, imply(domain, a_ >= t_))};
  EXPECT_FALSE(CheckSatisfiability(f, 0.001))
      << "robustly UNSAT (a≤0 can never reach a≥0.5001); a narrow universal "
         "domain must still be searched at its true bounds, not ε-emptied.";
}

// ∃a∈[-1,0] ∀t∈[0.5, 0.5]. a ≥ t.  Point domain: requires a ≥ 0.5, a ≤ 0 ⇒ UNSAT
// (margin 0.5). A point domain is ε-emptied by the buggy strengthening for any ε>0.
TEST_F(ForallNarrowDomainTest, PointDomainIsRefuted) {
  const Formula domain{(t_ >= 0.5) && (t_ <= 0.5)};
  const Formula f{(-1.0 <= a_) && (a_ <= 0.0) &&
                  forall({t_}, imply(domain, a_ >= t_))};
  EXPECT_FALSE(CheckSatisfiability(f, 0.001))
      << "robustly UNSAT (a≤0 < 0.5); a point universal domain must be searched "
         "at the point, not ε-emptied.";
}

// Control: the SAME body over a WIDE domain is already refuted today (the
// violation sits in the ε-interior), so this passes both before and after the
// fix. It guards against the fix accidentally changing the wide-domain verdict.
TEST_F(ForallNarrowDomainTest, WideDomainControlIsRefuted) {
  const Formula domain{(t_ >= 0.0) && (t_ <= 1.0)};
  const Formula f{(-1.0 <= a_) && (a_ <= 0.0) &&
                  forall({t_}, imply(domain, a_ >= t_))};
  EXPECT_FALSE(CheckSatisfiability(f, 0.001))
      << "wide-domain control: ∀t∈[0,1] a≥t needs a≥1, impossible for a≤0.";
}

// An outer query whose box contains a variable that does NOT occur in the
// forall atom. ForallFormulaEvaluator's nested CE context declares only the
// atom's own variables (free + quantified), so it must copy exactly those —
// not every outer-box variable. Pre-fix it iterated box.variables(): each
// unrelated variable's interval was silently WRITTEN onto dimension 0 of the
// nested CE box (Box::operator[] default-inserted into the shared name->index
// map), corrupting the CE search domain; with the hardened Box accessors the
// same path surfaced as a throw ("Box: variable extra is not in the box.")
// from inside CheckSatisfiability — which is this test's red state.
TEST_F(ForallNarrowDomainTest, OuterBoxVariableAbsentFromForallAtom) {
  const Variable extra{"extra", Variable::Type::CONTINUOUS};
  const Formula domain{(t_ >= 0.0) && (t_ <= 1.0)};
  const Formula f{(0.0 <= extra) && (extra <= 1.0) && (extra >= 0.5) &&
                  (-1.0 <= a_) && (a_ <= 10.0) &&
                  forall({t_}, imply(domain, a_ >= t_))};
  const auto result = CheckSatisfiability(f, 0.001);
  ASSERT_TRUE(result) << "robustly SAT: a = 2, extra = 0.7 is a model.";
  EXPECT_GE((*result)[a_].ub(), 1.0 - 0.001) << "∀t∈[0,1] a≥t needs a ≥ ~1";
  EXPECT_GE((*result)[extra].ub(), 0.5 - 0.001);
}

}  // namespace
}  // namespace dreal
