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
#include "dreal/solver/expression_evaluator.h"

#include <iostream>
#include <sstream>

#include <gtest/gtest.h>

namespace dreal {
namespace {

using std::cerr;
using std::endl;
using std::ostringstream;

class ExpressionEvaluatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    box_.Add(x_);
    box_.Add(y_);
    box_.Add(z_);
  }

  const Variable x_{"x"};
  const Variable y_{"y"};
  const Variable z_{"z"};
  Box box_;

  // ExpressionEvaluator does gaol interval evaluation (FE_UPWARD); establish it
  // and mint the token operator() now requires.
  const UpwardRoundingScope rms_;
  const UpwardRounding ur{rms_.token()};
};

TEST_F(ExpressionEvaluatorTest, Arithmetic1) {
  const Expression e{x_ + y_ + z_};
  const ExpressionEvaluator evaluator{e};

  box_[x_] = Box::Interval(1, 2);
  box_[y_] = Box::Interval(2, 3);
  box_[z_] = Box::Interval(3, 4);

  EXPECT_EQ(evaluator(box_, ur), Box::Interval(1 + 2 + 3, 2 + 3 + 4));
  ostringstream oss;
  oss << evaluator;
  EXPECT_EQ(oss.str(), "ExpressionEvaluator((x + y + z))");
}

// One evaluator, several boxes that share the same variable→index mapping
// (copies and bisections — the ICP case). Results must be exact.
TEST_F(ExpressionEvaluatorTest, SameEvaluatorAcrossSharedMappingBoxes) {
  const Expression e{x_ + y_ * z_};
  const ExpressionEvaluator evaluator{e};

  box_[x_] = Box::Interval(1, 2);
  box_[y_] = Box::Interval(2, 3);
  box_[z_] = Box::Interval(3, 4);
  EXPECT_EQ(evaluator(box_, ur), Box::Interval(1 + 2 * 3, 2 + 3 * 4));

  // A copy shares the mapping but carries different values.
  Box box2{box_};
  box2[x_] = Box::Interval(5, 6);
  EXPECT_EQ(evaluator(box2, ur), Box::Interval(5 + 2 * 3, 6 + 3 * 4));

  // Bisection children share the mapping too.
  const auto boxes = box_.bisect(x_);
  EXPECT_EQ(evaluator(boxes.first, ur), Box::Interval(1 + 2 * 3, 1.5 + 3 * 4));
  EXPECT_EQ(evaluator(boxes.second, ur), Box::Interval(1.5 + 2 * 3, 2 + 3 * 4));
}

// The same evaluator against an independently constructed box whose
// variable→index layout differs (the forall/CEGIS counterexample-box shape).
// All variables are present, so the result must still be exact.
TEST_F(ExpressionEvaluatorTest, IndependentBoxDifferentLayoutStillCorrect) {
  const Expression e{x_ + y_};
  const ExpressionEvaluator evaluator{e};

  box_[x_] = Box::Interval(1, 2);
  box_[y_] = Box::Interval(10, 20);
  EXPECT_EQ(evaluator(box_, ur), Box::Interval(11, 22));

  // Reversed declaration order: x and y sit at different indices here.
  Box reversed;
  reversed.Add(z_);
  reversed.Add(y_);
  reversed.Add(x_);
  reversed[x_] = Box::Interval(3, 4);
  reversed[y_] = Box::Interval(100, 200);
  EXPECT_EQ(evaluator(reversed, ur), Box::Interval(103, 204));
}

// Evaluating against a box that lacks one of the expression's variables must
// fail LOUDLY — never silently read another variable's slot.
TEST_F(ExpressionEvaluatorTest, MissingVariableBoxThrows) {
  const Expression e{x_ + y_};

  // (a) First-ever evaluation sees the deficient box.
  {
    const ExpressionEvaluator evaluator{e};
    Box no_y;
    no_y.Add(x_);
    no_y.Add(z_);
    no_y[x_] = Box::Interval(1, 2);
    no_y[z_] = Box::Interval(3, 4);
    EXPECT_THROW(evaluator(no_y, ur), std::runtime_error);
  }

  // (b) Evaluator already used on a complete box, then fed a deficient one.
  {
    const ExpressionEvaluator evaluator{e};
    box_[x_] = Box::Interval(1, 2);
    box_[y_] = Box::Interval(10, 20);
    EXPECT_EQ(evaluator(box_, ur), Box::Interval(11, 22));
    Box no_y;
    no_y.Add(x_);
    no_y[x_] = Box::Interval(1, 2);
    EXPECT_THROW(evaluator(no_y, ur), std::runtime_error);
  }
}

}  // namespace
}  // namespace dreal
