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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>

#include "rounding.h"
#include "dreal/util/assert.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/math.h"
#include "dreal/util/precision_guard.h"

using std::ceil;
using std::equal;
using std::find_if;
using std::floor;
using std::make_pair;
using std::make_shared;
using std::numeric_limits;
using std::ostream;
using std::pair;
using std::unordered_map;
using std::vector;

namespace dreal {

Box::Box()
    : variables_{make_shared<vector<Variable>>()},
      // We have this hack here because it is not allowed to have a
      // zero interval vector. Note that because of this special case,
      // `variables_->size() == values_.size()` do not hold. We should
      // rely on `values_.size()`.
      values_(1),
      var_to_idx_{
          make_shared<unordered_map<Variable, int, hash_value<Variable>>>()},
      idx_to_var_{make_shared<unordered_map<int, Variable>>()} {}

Box::Box(const vector<Variable>& variables)
    : variables_{make_shared<vector<Variable>>()},
      values_(static_cast<int>(variables.size())),
      var_to_idx_{
          make_shared<unordered_map<Variable, int, hash_value<Variable>>>()},
      idx_to_var_{make_shared<unordered_map<int, Variable>>()} {
  for (const Variable& var : variables) {
    Add(var);
  }
}

void Box::Add(const Variable& v) {
  // Duplicate variables are not allowed.
  DREAL_ASSERT(find_if(variables_->begin(), variables_->end(),
                       [&v](const Variable& var) { return v.equal_to(var); }) ==
               variables_->end());

  if (!variables_.unique()) {
    // If the components of this box is shared by more than one
    // entity, we need to clone this before adding the variable `v`
    // so that these changes remain local.
    variables_ = make_shared<vector<Variable>>(*variables_);
    var_to_idx_ =
        make_shared<unordered_map<Variable, int, hash_value<Variable>>>(
            *var_to_idx_);
    idx_to_var_ = make_shared<unordered_map<int, Variable>>(*idx_to_var_);
  }
  const int n{size()};
  idx_to_var_->emplace(n, v);
  var_to_idx_->emplace(v, n);
  variables_->push_back(v);
  values_.resize(size());

  // Set up Domain.
  // TODO(soonho): For now, we allow Boolean variables in a box. Change this.
  if (v.get_type() == Variable::Type::BOOLEAN ||
      v.get_type() == Variable::Type::BINARY) {
    values_[n] = Interval(0.0, 1.0);
  } else if (v.get_type() == Variable::Type::INTEGER) {
    // dreal/dreal4#284: the default Int domain is the full double-exact
    // integer range +/-2^53, NOT the C int range — an int32-sized domain
    // silently excluded Int values >= 2^31 (x = 2147483648 contracted to
    // empty: a false unsat on an assertion-free query). Within +/-2^53 every
    // integer step the Int machinery performs (ceil/floor contraction, the
    // +/-1 arithmetic in bisect_int) is exact; beyond it the parser's
    // convert_int64_to_double already rejects literals loudly.
    values_[n] = Interval(-kMaxExactInt, kMaxExactInt);
  }
}

void Box::Add(const Variable& v, const double lb, const double ub) {
  Add(v);

  DREAL_ASSERT(lb <= ub);

  // Binary variable => lb, ub ∈ [0, 1].
  DREAL_ASSERT(v.get_type() != Variable::Type::BINARY ||
               (0.0 <= lb && ub <= 1.0));

  // Integer variable => lb, ub within the double-exact range +/-2^53
  // (dreal/dreal4#284). Integrality is NOT asserted here: mid-search re-adds
  // (e.g. the forall counterexample path) legitimately carry contracted
  // fractional bounds, which contractor_integer later tightens. Integrality
  // of *declared* domains is enforced at the parser (convert_int64_to_double
  // rejects out-of-range literals loudly).
  DREAL_ASSERT(v.get_type() != Variable::Type::INTEGER ||
               (-kMaxExactInt <= lb && ub <= kMaxExactInt));

  values_[(*var_to_idx_)[v]] = Interval(lb, ub);
}

bool Box::empty() const { return values_.is_empty(); }

void Box::set_empty() { values_.set_empty(); }

int Box::size() const { return variables_->size(); }

Box::Interval& Box::operator[](const int i) {
  DREAL_ASSERT(i < size());
  return values_[i];
}
Box::Interval& Box::operator[](const Variable& var) {
  return values_[(*var_to_idx_)[var]];
}
const Box::Interval& Box::operator[](const int i) const {
  DREAL_ASSERT(i < size());
  return values_[i];
}
const Box::Interval& Box::operator[](const Variable& var) const {
  return values_[(*var_to_idx_)[var]];
}

const vector<Variable>& Box::variables() const { return *variables_; }

const Variable& Box::variable(const int i) const { return (*idx_to_var_)[i]; }

bool Box::has_variable(const Variable& var) const {
  return var_to_idx_->count(var) > 0;
}

int Box::index(const Variable& var) const { return (*var_to_idx_)[var]; }

const Box::IntervalVector& Box::interval_vector() const { return values_; }
Box::IntervalVector& Box::mutable_interval_vector() { return values_; }

pair<double, int> Box::MaxDiam() const {
  // Standalone entry point: self-establish the FE_UPWARD scope and mint the
  // token for safe_diam (the nearest-regime analog is a NearestRoundingScope).
  const UpwardRoundingScope g;
  const UpwardRounding ur{g.token()};
  double max_diam{0.0};
  int idx{-1};
  for (size_t i{0}; i < variables_->size(); ++i) {
    // safe_diam(): .diam() is a gaol directed-rounding computation, sound only
    // under the FE_UPWARD this function established above.
    const double diam_i{safe_diam(values_[i], ur)};
    if (diam_i > max_diam && values_[i].is_bisectable()) {
      max_diam = diam_i;
      idx = i;
    }
  }
  return make_pair(max_diam, idx);
}

pair<Box, Box> Box::bisect(const int i, const double ratio) const {
  const Variable& var{(*idx_to_var_)[i]};
  if (!values_[i].is_bisectable()) {
    throw DREAL_RUNTIME_ERROR(
        "Variable {} = {} is not bisectable but Box::bisect is called.",
        fmt::streamed(var),
        fmt::streamed(values_[i]));
  }
  switch (var.get_type()) {
    case Variable::Type::CONTINUOUS:
      return bisect_continuous(i, ratio);
    case Variable::Type::INTEGER:
    case Variable::Type::BINARY:
      return bisect_int(i);
    case Variable::Type::BOOLEAN:
      DREAL_UNREACHABLE();
  }
  DREAL_UNREACHABLE();
}

pair<Box, Box> Box::bisect(const Variable& var) const {
  auto it = var_to_idx_->find(var);
  if (it != var_to_idx_->end()) {
    return bisect(it->second);
  } else {
    throw DREAL_RUNTIME_ERROR("Variable {} is not found in this box.", fmt::streamed(var));
  }
  return bisect((*var_to_idx_)[var]);
}

pair<Box, Box> Box::bisect_int(const int i) const {
  DREAL_ASSERT(idx_to_var_->at(i).get_type() == Variable::Type::INTEGER ||
               idx_to_var_->at(i).get_type() == Variable::Type::BINARY);
  const Interval& intv_i{values_[i]};
  const double lb{ceil(intv_i.lb())};
  const double ub{floor(intv_i.ub())};
  const double mid{intv_i.mid()};  // lint: allow (integer bisection; bounds are exact integers)
  const double mid_floor{floor(mid)};
  DREAL_ASSERT(intv_i.lb() <= lb);
  DREAL_ASSERT(lb <= mid_floor);
  DREAL_ASSERT(mid_floor + 1 <= ub);
  DREAL_ASSERT(ub <= intv_i.ub());

  Box b1{*this};
  Box b2{*this};
  b1[i] = Interval(lb, mid_floor);
  b2[i] = Interval(mid_floor + 1, ub);
  return make_pair(b1, b2);
}

pair<Box, Box> Box::bisect_continuous(const int i, const double ratio) const {
  DREAL_ASSERT(idx_to_var_->at(i).get_type() == Variable::Type::CONTINUOUS);
  Box b1{*this};
  Box b2{*this};
  const Interval intv_i{values_[i]};
  // .first = [lb, lb+ratio*diam] (size `ratio`), .second = the remainder. At the
  // default ratio 0.5 the two children are equal halves.
  const pair<Interval, Interval> bisected_intervals{intv_i.bisect(ratio)};
  b1[i] = bisected_intervals.first;
  b2[i] = bisected_intervals.second;
  return make_pair(b1, b2);
}

Box& Box::InplaceUnion(const Box& b) {
  // Checks variables() == b.variables().
  DREAL_ASSERT(equal(variables().begin(), variables().end(),
                     b.variables().begin(), b.variables().end(),
                     std::equal_to<Variable>{}));
  values_ |= b.values_;
  return *this;
}

ostream& operator<<(ostream& os, const Box& box) {
  PrecisionGuard precision_guard(&os, numeric_limits<double>::max_digits10);
  for (int i = 0; i < box.size(); ++i) {
    const Variable& var{box.variable(i)};
    const Box::Interval interval(box.values_[i]);
    os << var << " : ";
    switch (var.get_type()) {
      case Variable::Type::INTEGER:
      case Variable::Type::BINARY:
        if (interval.is_empty()) {
          os << "[ empty ]";
        } else {
          // int64_t: the Int domain spans +/-2^53 (dreal/dreal4#284) — an int
          // cast is UB above 2^31.
          os << "[" << static_cast<std::int64_t>(interval.lb()) << ", "
             << static_cast<std::int64_t>(interval.ub()) << "]";
        }
        break;
      case Variable::Type::CONTINUOUS: {
        // ibex's interval operator<< is a known FPU rounding-mode clobberer: it
        // does internal directed rounding and leaves the FPU in a directed mode
        // (FE_UPWARD) on return. Bracket it in an ExpectClobber nearest scope so
        // the print runs under FE_TONEAREST and the caller's mode is restored on
        // exit, containing the clobber. See ExpectClobber in rounding.h.
        const NearestRoundingScope interval_print{expect_clobber};
        os << interval;
        break;
      }
      case Variable::Type::BOOLEAN:
        if (interval.ub() == 0.0) {
          os << "False";
        } else if (interval.lb() == 1.0) {
          os << "True";
        } else {
          // Don't-care: the SAT layer never assigned it — both values
          // satisfy. The whole interval, like any other dim.
          os << "[false, true]";
        }
        break;
    }
    if (i != box.size() - 1) {
      os << "\n";
    }
  }
  return os;
}

bool operator==(const Box& b1, const Box& b2) {
  return equal(b1.variables().begin(), b1.variables().end(),
               b2.variables().begin(), b2.variables().end(),
               std::equal_to<Variable>{}) &&
         (b1.interval_vector() == b2.interval_vector());
}

bool operator!=(const Box& b1, const Box& b2) { return !(b1 == b2); }

ostream& DisplayDiff(ostream& os, const vector<Variable>& variables,
                     const Box::IntervalVector& old_iv,
                     const Box::IntervalVector& new_iv) {
  PrecisionGuard precision_guard(&os, numeric_limits<double>::max_digits10);
  for (size_t i = 0; i < variables.size(); ++i) {
    const Box::Interval& old_i{old_iv[i]};
    const Box::Interval& new_i{new_iv[i]};
    if (old_i != new_i) {
      os << variables[i] << " : " << old_i << " -> " << new_i << "\n";
    }
  }
  return os;
}

}  // namespace dreal
