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
#include "dreal/util/box.h"

#include <cfenv>
#include <iostream>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/rounding.h"

using std::is_nothrow_move_constructible;
using std::numeric_limits;
using std::pair;
using std::vector;

namespace dreal {
namespace {

class BoxTest : public ::testing::Test {
 protected:
  // Real Variables.
  const Variable x_{"x"};
  const Variable y_{"y"};
  const Variable z_{"z"};
  const Variable w_{"w"};

  // Integer Variables.
  const Variable i_{"i", Variable::Type::INTEGER};
  const Variable j_{"j", Variable::Type::INTEGER};

  // Binary Variables.
  const Variable b1_{"i", Variable::Type::BINARY};
  const Variable b2_{"j", Variable::Type::BINARY};

  const double inf_{numeric_limits<double>::infinity()};
};

TEST_F(BoxTest, AddHasVariable) {
  Box b1{{x_}};
  EXPECT_EQ(b1[x_].lb(), -inf_);
  EXPECT_EQ(b1[x_].ub(), inf_);

  EXPECT_TRUE(b1.has_variable(x_));
  EXPECT_FALSE(b1.has_variable(y_));
  EXPECT_FALSE(b1.has_variable(z_));

  b1.Add(y_);
  EXPECT_EQ(b1[y_].lb(), -inf_);
  EXPECT_EQ(b1[y_].ub(), inf_);
  EXPECT_TRUE(b1.has_variable(x_));
  EXPECT_TRUE(b1.has_variable(y_));
  EXPECT_FALSE(b1.has_variable(z_));

  b1.Add(z_, -5, 10);
  EXPECT_EQ(b1[z_].lb(), -5);
  EXPECT_EQ(b1[z_].ub(), 10);
  EXPECT_TRUE(b1.has_variable(x_));
  EXPECT_TRUE(b1.has_variable(y_));
  EXPECT_TRUE(b1.has_variable(z_));
}

// A name lookup of a variable NOT in the box must fail loudly and must not
// mutate anything. Pre-fix, all four name/index accessors did
// `(*var_to_idx_)[var]` — `unordered_map::operator[]` default-inserts, so a
// miss silently returned dimension 0's interval AND poisoned the map shared
// (default copy ctor, bisect children) by every box copied from the same
// ancestor.
TEST_F(BoxTest, NameLookupMissThrowsAndDoesNotPoison) {
  Box b1{{x_}};
  const Box b2{b1};  // shares the name->index map with b1
  const Box& cb{b1};
  EXPECT_THROW(cb[z_], std::runtime_error);
  EXPECT_FALSE(b1.has_variable(z_));
  EXPECT_FALSE(b2.has_variable(z_));  // the shared-map propagation witness
}

// Pre-fix, a poisoned entry was permanent: a later legitimate Add(z_) hit
// `emplace`'s no-op on the existing key, so "z_" aliased dimension 0 forever —
// writes through the name clobbered another variable's interval.
TEST_F(BoxTest, MissThenAddDoesNotAliasDimensionZero) {
  Box b{{x_}};
  b[x_] = Box::Interval{1, 2};
  try {
    const Box& cb{b};
    (void)cb[z_];  // the miss (pre-fix: silently poisons z_ -> 0)
  } catch (const std::runtime_error&) {
  }
  b.Add(z_);
  b[z_] = Box::Interval{5, 6};  // pre-fix: lands on dimension 0 (= x_)
  EXPECT_EQ(b[x_].lb(), 1.0);
  EXPECT_EQ(b[x_].ub(), 2.0);
}

TEST_F(BoxTest, IndexAndVariableMissThrow) {
  Box b{{x_}};
  const Box& cb{b};
  EXPECT_THROW(cb.index(z_), std::runtime_error);
  EXPECT_THROW(cb.variable(1), std::runtime_error);  // valid: 0..size()-1
  EXPECT_THROW(cb.variable(-1), std::runtime_error);
  EXPECT_FALSE(b.has_variable(z_));
}

TEST_F(BoxTest, Empty) {
  Box b1{{x_}};
  EXPECT_FALSE(b1.empty());

  b1.set_empty();
  EXPECT_TRUE(b1.empty());
}

TEST_F(BoxTest, IndexOperator) {
  Box b1;
  b1.Add(x_, 3, 5);
  b1.Add(y_, 6, 10);
  EXPECT_EQ(b1[0], b1[x_]);
  EXPECT_EQ(b1[1], b1[y_]);

  const Box b2{b1};
  EXPECT_EQ(b2[0], b2[x_]);
  EXPECT_EQ(b2[1], b2[y_]);
  EXPECT_EQ(b1[0], b2[x_]);
  EXPECT_EQ(b1[1], b2[y_]);
}

TEST_F(BoxTest, IntervalVector) {
  Box b1;
  b1.Add(x_, 3, 5);
  b1.Add(y_, 6, 10);
  EXPECT_EQ(b1.interval_vector()[0], b1[x_]);

  // Update
  b1.mutable_interval_vector()[0] = Box::Interval(0, 1);
  EXPECT_EQ(b1[x_].lb(), 0);
  EXPECT_EQ(b1[x_].ub(), 1);

  const Box b2{b1};
  EXPECT_EQ(b2.interval_vector()[0].lb(), 0);
  EXPECT_EQ(b2.interval_vector()[0].ub(), 1);
}

TEST_F(BoxTest, VariableVariables) {
  const Box b1{{x_, y_, z_}};
  vector<Variable> variables{x_, y_, z_};
  const vector<Variable>& variables_in_b1{b1.variables()};
  EXPECT_EQ(variables_in_b1.size(), 3);
  EXPECT_EQ(variables_in_b1[0], x_);
  EXPECT_EQ(variables_in_b1[1], y_);
  EXPECT_EQ(variables_in_b1[2], z_);
}

TEST_F(BoxTest, Index) {
  const Box b1{{x_, y_, z_}};
  EXPECT_EQ(b1.index(x_), 0);
  EXPECT_EQ(b1.index(y_), 1);
  EXPECT_EQ(b1.index(z_), 2);
}

TEST_F(BoxTest, MaxDiam) {
  Box b1;
  b1.Add(x_, -10, 10);
  b1.Add(y_, 5, 5);
  b1.Add(z_, -1, 1);

  const pair<double, int> maxdiam_result{b1.MaxDiam()};
  EXPECT_EQ(maxdiam_result.first, 20.0);
  EXPECT_EQ(b1.variable(maxdiam_result.second), x_);
}

TEST_F(BoxTest, Sharing) {
  Box b1{{x_, y_}};
  // b1 is not shared yet, so we should just update its internal
  // states.
  b1.Add(z_);

  // Now, b1 and b2 are shared.
  Box b2{b1};

  // Internal structure of b2 should be cloned before updated. It makes sure
  // that b1 is not affected.
  b2.Add(w_);

  EXPECT_EQ(b1.size(), 3 /* x, y, z */);
  EXPECT_EQ(b2.size(), 4 /* x, y, z, w_ */);
}

TEST_F(BoxTest, InplaceUnion) {
  Box b1{{x_, y_}};
  b1[x_] = Box::Interval(0, 1);
  b1[y_] = Box::Interval(0, 1);

  Box b2{{x_, y_}};
  b2[x_] = Box::Interval(2, 3);
  b2[y_] = Box::Interval(3, 4);

  b1.InplaceUnion(b2);
  EXPECT_EQ(b1[x_], Box::Interval(0, 3));
  EXPECT_EQ(b1[y_], Box::Interval(0, 4));

  // No changes on b2.
  EXPECT_EQ(b2[x_], Box::Interval(2, 3));
  EXPECT_EQ(b2[y_], Box::Interval(3, 4));
}

TEST_F(BoxTest, BisectReal) {
  Box box;
  box.Add(x_, -10, 10);
  box.Add(i_, -5, 5);
  box.Add(b1_, 0, 1);

  const pair<Box, Box> p{box.bisect(x_)};
  const Box& box1{p.first};
  const Box& box2{p.second};

  EXPECT_EQ(box1[x_].lb(), box[x_].lb());
  EXPECT_EQ(box1[x_].ub(), 0.0);
  EXPECT_EQ(box1[i_], box[i_]);
  EXPECT_EQ(box1[b1_], box[b1_]);

  EXPECT_EQ(box2[x_].lb(), box1[x_].ub());
  EXPECT_EQ(box2[x_].ub(), box[x_].ub());
  EXPECT_EQ(box2[i_], box[i_]);
  EXPECT_EQ(box2[b1_], box[b1_]);
}

TEST_F(BoxTest, BisectRealWithRatio) {
  Box box;
  box.Add(x_, -10, 10);
  box.Add(i_, -5, 5);
  box.Add(b1_, 0, 1);

  // Split x = [-10, 10] off-center at ratio 0.25 -> cut at lb + 0.25*diam =
  // -10 + 0.25*20 = -5. The default (ratio 0.5) cuts at 0; this proves the
  // ratio argument is threaded into the continuous bisection.
  const pair<Box, Box> p{box.bisect(box.index(x_), 0.25)};
  const Box& box1{p.first};
  const Box& box2{p.second};

  EXPECT_EQ(box1[x_].lb(), box[x_].lb());
  EXPECT_EQ(box1[x_].ub(), -5.0);
  EXPECT_EQ(box1[i_], box[i_]);
  EXPECT_EQ(box1[b1_], box[b1_]);

  EXPECT_EQ(box2[x_].lb(), box1[x_].ub());
  EXPECT_EQ(box2[x_].ub(), box[x_].ub());

  // The no-arg / default path still cuts at the midpoint (0.0).
  const pair<Box, Box> half{box.bisect(box.index(x_))};
  EXPECT_EQ(half.first[x_].ub(), 0.0);
}

TEST_F(BoxTest, BisectInteger) {
  Box box;
  box.Add(x_, -10, 10);
  box.Add(i_, -5, 5);
  box.Add(b1_, 0, 1);

  const pair<Box, Box> p{box.bisect(i_)};
  const Box& box1{p.first};
  const Box& box2{p.second};

  EXPECT_EQ(box1[i_].lb(), box[i_].lb());
  EXPECT_EQ(box1[i_].ub(), 0.0);
  EXPECT_EQ(box1[x_], box[x_]);
  EXPECT_EQ(box1[b1_], box[b1_]);

  EXPECT_EQ(box2[i_].lb(), box1[i_].ub() + 1);
  EXPECT_EQ(box2[i_].ub(), box[i_].ub());
  EXPECT_EQ(box2[x_], box[x_]);
  EXPECT_EQ(box2[b1_], box[b1_]);
}

TEST_F(BoxTest, BisectBinary) {
  Box box;
  box.Add(x_, -10, 10);
  box.Add(i_, -5, 5);
  box.Add(b1_, 0, 1);

  const pair<Box, Box> p{box.bisect(b1_)};
  const Box& box1{p.first};
  const Box& box2{p.second};

  EXPECT_EQ(box1[b1_].lb(), box[b1_].lb());
  EXPECT_EQ(box1[b1_].ub(), 0.0);
  EXPECT_EQ(box1[x_], box[x_]);
  EXPECT_EQ(box1[i_], box[i_]);

  EXPECT_EQ(box2[b1_].lb(), box1[b1_].ub() + 1);
  EXPECT_EQ(box2[b1_].ub(), box[b1_].ub());
  EXPECT_EQ(box2[x_], box[x_]);
  EXPECT_EQ(box2[i_], box[i_]);
}

TEST_F(BoxTest, NotBisectable) {
  Box box;
  // x = [10, 10 + ε]
  box.Add(x_, 10, std::nextafter(10, 11));

  // [10, 10 + ε] is not bisectable.
  EXPECT_THROW(box.bisect(x_), std::runtime_error);

  // y is not in the box -> non-bisectable.
  EXPECT_THROW(box.bisect(y_), std::runtime_error);
}

TEST_F(BoxTest, Equality) {
  Box b1;
  b1.Add(x_, -10, 10);
  b1.Add(y_, -5, 5);

  Box b2{b1};
  EXPECT_TRUE(b1 == b2);
  EXPECT_FALSE(b1 != b2);

  b2.Add(z_, -1, 1);
  EXPECT_FALSE(b1 == b2);
  EXPECT_TRUE(b1 != b2);

  Box b3{b1};
  EXPECT_TRUE(b1 == b3);
  EXPECT_FALSE(b1 != b3);

  b3[y_] = Box::Interval(-5, 6);
  EXPECT_FALSE(b1 == b3);
  EXPECT_TRUE(b1 != b3);
}

// Checks types in Box are nothrow move-constructible so that the
// vectors including them can be processed efficiently.
TEST_F(BoxTest, IsNothrowMoveConstructible) {
  // todo: check how much of a performance hit this causes. -Kunal
  // static_assert(is_nothrow_move_constructible<Box::Interval>::value,
                // "Box::Interval should be nothrow_move_constructible.");
  // static_assert(is_nothrow_move_constructible<Box::IntervalVector>::value,
                // "Box::IntervalVector should be nothrow_move_constructible.");
  static_assert(is_nothrow_move_constructible<Box>::value,
                "Box should be nothrow_move_constructible.");
}

// Regression: ibex's interval operator<< (used by Box::operator<< for a
// non-degenerate continuous interval) clobbers the FPU to a directed mode
// rather than restoring nearest. Box::operator<< must contain that clobber with
// an ExpectClobber scope so a caller's NearestRoundingScope still finds
// FE_TONEAREST at its dtor. Pre-fix, printing a non-degenerate-interval model
// under the driver's nearest scope aborted via the dtor clobber tripwire.
TEST_F(BoxTest, IntervalPrintDoesNotClobberRounding) {
  Box b;
  b.Add(x_, 0.1, 0.3);  // non-degenerate, inexactly-representable endpoints
  std::ostringstream oss;
  {
    const NearestRoundingScope g;  // its dtor clobber tripwire is the backstop
    oss << b;
    // Box::operator<< contained ibex's directed-mode clobber: still nearest.
    EXPECT_EQ(fegetround(), FE_TONEAREST);
  }  // g's dtor would abort here if the clobber had escaped containment
  EXPECT_FALSE(oss.str().empty());
}

// Regression for BUG-005 (dreal/dreal4): Box::operator<< must pair every
// variable with its own value. A previous version sorted the variable names
// alphabetically but indexed values_ by a parallel counter, so once insertion
// order != alphabetical order each variable printed another's value (visible as
// a "scrambled model" on QF_NRA_ODE problems, whose vars x_0_0/x_0_t/time_0/...
// are declared out of alphabetical order). Insert in non-alphabetical order with
// distinct values and confirm each line carries its own value, in insertion
// order.
TEST_F(BoxTest, PrintPairsEachVariableWithItsOwnValue) {
  Box b;
  b.Add(y_, 1, 1);  // insertion order y, w, x -- distinct from alphabetical w,x,y
  b.Add(w_, 2, 2);
  b.Add(x_, 3, 3);
  std::ostringstream oss;
  oss << b;
  const std::string s{oss.str()};

  auto line_for = [&s](const std::string& name) -> std::string {
    std::istringstream iss{s};
    std::string line;
    while (std::getline(iss, line)) {
      if (line.rfind(name + " :", 0) == 0) return line;
    }
    return {};
  };
  // y=1, w=2, x=3: each line has its own digit and neither other's.
  EXPECT_NE(line_for("y").find('1'), std::string::npos);
  EXPECT_EQ(line_for("y").find('2'), std::string::npos);
  EXPECT_EQ(line_for("y").find('3'), std::string::npos);
  EXPECT_NE(line_for("w").find('2'), std::string::npos);
  EXPECT_EQ(line_for("w").find('1'), std::string::npos);
  EXPECT_EQ(line_for("w").find('3'), std::string::npos);
  EXPECT_NE(line_for("x").find('3'), std::string::npos);
  EXPECT_EQ(line_for("x").find('1'), std::string::npos);
  EXPECT_EQ(line_for("x").find('2'), std::string::npos);

  // Output is in insertion order (the alphabetical sort was reverted).
  EXPECT_LT(s.find("y :"), s.find("w :"));
  EXPECT_LT(s.find("w :"), s.find("x :"));
}

}  // namespace
}  // namespace dreal
