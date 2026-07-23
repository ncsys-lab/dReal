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

#include <cmath>
#include <utility>

#include <dreal/util/rounded_interval.h>

#include "dreal/util/assert.h"
#include "dreal/util/exception.h"

namespace dreal {

BrancherAbs::BrancherAbs(const Box& box, const BrancherVariant variant,
                         const double decay)
    : variant_{variant},
      decay_{decay},
      // Paren-init: vector<double>(n, 0.0) sizes the vector; brace-init would
      // hit the initializer_list ctor and make the 2-element vector {n, 0.0}.
      activity_(box.size(), 0.0) {
  DREAL_ASSERT(variant_ != BrancherVariant::kLargest);
  if (!(decay_ > 0.0 && decay_ <= 1.0)) {
    throw DREAL_RUNTIME_ERROR(
        "BrancherAbs: decay (--branch-decay) must be in (0, 1]; got {}.",
        decay_);
  }
}

void BrancherAbs::CheckSize(const Box& box) const {
  if (static_cast<int>(activity_.size()) != box.size()) {
    throw DREAL_RUNTIME_ERROR(
        "BrancherAbs: box has {} dimensions but the activity vector has {} — "
        "the ICP variable set must be fixed for a whole solve.",
        box.size(), activity_.size());
  }
}

void BrancherAbs::Decay() {
  // Heuristic bookkeeping on plain doubles (never an interval endpoint):
  // last-ulp rounding-mode drift cannot move a verdict, so no token needed.
  for (double& a : activity_) {
    a *= decay_;
  }
}

void BrancherAbs::SnapshotDiams(const Box& box, const UpwardRounding& ur) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  CheckSize(box);
  const Box::IntervalVector& iv{box.interval_vector()};
  const int n{box.size()};
  pre_prune_diams_.resize(n);  // capacity retained after the first node
  for (int i = 0; i < n; ++i) {
    pre_prune_diams_[i] = safe_diam(iv[i], ur);
  }
}

void BrancherAbs::BumpShrunk(const Box& box, const UpwardRounding& ur) {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  CheckSize(box);
  if (pre_prune_diams_.size() != activity_.size()) {
    throw DREAL_RUNTIME_ERROR(
        "BrancherAbs::BumpShrunk called without a preceding (unconsumed) "
        "SnapshotDiams — the bump would diff against stale diameters.");
  }
  const Box::IntervalVector& iv{box.interval_vector()};
  const int n{box.size()};
  for (int i = 0; i < n; ++i) {
    if (safe_diam(iv[i], ur) < pre_prune_diams_[i]) {
      activity_[i] += 1.0;
    }
  }
  // Consume the snapshot: clear() keeps capacity (the resize in SnapshotDiams
  // never reallocates after the first node) and makes a protocol-violating
  // second bump throw above instead of silently reusing stale diameters.
  pre_prune_diams_.clear();
}

int BrancherAbs::operator()(const Box& box, const DynamicBitset& active_set,
                            Box* const left, Box* const right,
                            const UpwardRounding& ur) const {
  DREAL_ASSERT_ROUNDING(FE_UPWARD);
  DREAL_ASSERT(!active_set.none());
  CheckSize(box);

  int best{-1};
  double best_score{0.0};
  double best_diam{0.0};
  DynamicBitset::size_type j{active_set.find_first()};
  while (j != DynamicBitset::npos) {
    const Box::Interval& iv_j{box[j]};
    if (iv_j.is_bisectable()) {
      const double d{safe_diam(iv_j, ur)};  // > 0: iv_j is bisectable
      double score{variant_ == BrancherVariant::kAbs ? activity_[j] / d
                                                     : activity_[j] * d};
      // 0·∞ (zero activity on an unbounded dim under kAbsDiam) is NaN; define
      // it as 0 so the diameter tie-break decides — preserving the all-zero-
      // activity largest-first start on unbounded dims. This is the scoring
      // function's defined behavior, not an error path: every variable choice
      // is sound.
      if (std::isnan(score)) {
        score = 0.0;
      }
      // Ties: larger diameter, then lower index (ascending iteration + strict
      // comparisons keep the earliest index among full ties).
      if (best < 0 || score > best_score ||
          (score == best_score && d > best_diam)) {
        best_score = score;
        best_diam = d;
        best = static_cast<int>(j);
      }
    }
    j = active_set.find_next(j);
  }
  if (best < 0) {
    return -1;
  }
  std::pair<Box, Box> bisected{box.bisect(best)};  // midpoint
  *left = std::move(bisected.first);
  *right = std::move(bisected.second);
  return best;
}

}  // namespace dreal
