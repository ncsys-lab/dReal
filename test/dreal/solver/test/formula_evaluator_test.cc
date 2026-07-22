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
#include "dreal/solver/formula_evaluator.h"

#include <iostream>

#include <gtest/gtest.h>

namespace dreal {
namespace {

using std::cerr;
using std::endl;

class FormulaEvaluatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    box_.Add(x_);
    box_.Add(y_);
    box_.Add(z_);
  }

  const Variable x_{"x"};
  const Variable y_{"y"};
  const Variable z_{"z"};

  const Formula gt_{x_ > y_};
  const Formula gte_{x_ >= y_};
  const Formula lt_{x_ < y_};
  const Formula lte_{x_ <= y_};
  const Formula eq_{x_ == y_};
  const Formula neq_{x_ != y_};

  Box box_;

  // The relational evaluator does gaol interval evaluation, which is sound only
  // under FE_UPWARD; establish it for the test and mint the token operator()
  // now requires.
  const UpwardRoundingScope rms_;
  const UpwardRounding ur{rms_.token()};
};

TEST_F(FormulaEvaluatorTest, Gt) {
  FormulaEvaluator formula_evaluator{make_relational_formula_evaluator(gt_)};
  box_[x_] = 10.0;
  box_[y_] = 0.0;
  const Box::Interval result{formula_evaluator(box_, ur).evaluation()};
  cerr << gt_ << "\t" << formula_evaluator << "\n" << result << endl;
  cerr << "-----------------------\n";
}

TEST_F(FormulaEvaluatorTest, Gte) {
  FormulaEvaluator formula_evaluator{make_relational_formula_evaluator(gte_)};
  box_[x_] = 10.0;
  box_[y_] = 0.0;
  const Box::Interval result{formula_evaluator(box_, ur).evaluation()};
  cerr << gte_ << "\t" << formula_evaluator << "\n" << result << endl;
  cerr << "-----------------------\n";
}

TEST_F(FormulaEvaluatorTest, Lt) {
  FormulaEvaluator formula_evaluator{make_relational_formula_evaluator(lt_)};
  box_[x_] = 10.0;
  box_[y_] = 0.0;
  const Box::Interval result{formula_evaluator(box_, ur).evaluation()};
  cerr << lt_ << "\t" << formula_evaluator << "\n" << result << endl;
  cerr << "-----------------------\n";
}

TEST_F(FormulaEvaluatorTest, Lte) {
  FormulaEvaluator formula_evaluator{make_relational_formula_evaluator(lte_)};
  box_[x_] = 10.0;
  box_[y_] = 0.0;
  const Box::Interval result{formula_evaluator(box_, ur).evaluation()};
  cerr << lte_ << "\t" << formula_evaluator << "\n" << result << endl;
  cerr << "-----------------------\n";
}

TEST_F(FormulaEvaluatorTest, Eq) {
  FormulaEvaluator formula_evaluator{make_relational_formula_evaluator(eq_)};
  box_[x_] = 10.0;
  box_[y_] = 0.0;
  const Box::Interval result{formula_evaluator(box_, ur).evaluation()};
  cerr << eq_ << "\t" << formula_evaluator << "\n" << result << endl;
  cerr << "-----------------------\n";
}

TEST_F(FormulaEvaluatorTest, Neq) {
  FormulaEvaluator formula_evaluator{make_relational_formula_evaluator(neq_)};
  box_[x_] = 10.0;
  box_[y_] = 0.0;
  const Box::Interval result{formula_evaluator(box_, ur).evaluation()};
  cerr << neq_ << "\t" << formula_evaluator << "\n" << result << endl;
  cerr << "-----------------------\n";
}

// dreal/dreal4#280 — a forall constraint whose body is DEFINITIVELY violated
// over the whole box must evaluate to UNSAT, not UNKNOWN. This is the exact
// forall-equality of the upstream reproducer at the invalid model's point box
// (c, e, f) = (1, -27, 22): forall p in [-0.1, 1.1]. f + p e + c p^2 = 1 is
// off by 21 at p = 0 — for EVERY point of the box. Returning UNKNOWN made
// IcpSeq branch; an all-Int existential box collapses to non-bisectable
// points, and the non-bisectable exit then ACCEPTED the violated box —
// COMPLETENESS (asserts phi^delta T-satisfiable on a T-unsatisfiable phi —
// missed refutation). (Continuous variables here: the evaluator defect is
// type-agnostic — Int only makes ICP hit the non-bisectable exit.)
TEST_F(FormulaEvaluatorTest, ForallDefinitelyViolatedIsUnsat) {
  const Variable p{"p"};
  const Formula f{forall(Variables{p},
                         !(Expression{-0.1} <= p) || !(p <= Expression{1.1}) ||
                             (z_ + p * y_ + x_ * pow(p, 2) == 1.0))};
  FormulaEvaluator evaluator{make_forall_formula_evaluator(
      f, /*epsilon=*/0.05, /*delta=*/0.01, /*number_of_jobs=*/1)};
  box_[x_] = 1.0;    // c
  box_[y_] = -27.0;  // e
  box_[z_] = 22.0;   // f
  const FormulaEvaluationResult result{evaluator(box_, ur)};
  EXPECT_EQ(result.type(), FormulaEvaluationResult::Type::UNSAT)
      << "definitively violated forall evaluated as " << result;
}

}  // namespace
}  // namespace dreal
