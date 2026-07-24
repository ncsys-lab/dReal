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
#pragma once

#include <vector>

#include "dreal/solver/config.h"  // BrancherVariant
#include "dreal/util/box.h"
#include "dreal/util/dynamic_bitset.h"
#include "dreal/util/rounding.h"

namespace dreal {

/// Activity-based ("ABS") brancher — OUR CONTINUOUS ADAPTATION of the
/// activity-based search of Michel & Van Hentenryck (CPAIOR'12). The paper's
/// rule is finite-domain (CP branching); the continuous scoring formulas and
/// the decay default (`--branch-decay`, 0.999) here are our choices — do not
/// attribute them to the paper.
///
/// Per-variable activity A (all-zero at construction) is fed by the ICP loop,
/// which owns the hook points (this functor never sees Prune itself):
///  - Decay()         once per search node: A_i *= decay;
///  - SnapshotDiams() right before Prune: records the per-dim diameters;
///  - BumpShrunk()    after a non-emptying Prune: A_i += 1 for every dim whose
///                    diameter strictly shrank vs the snapshot (consumes it).
///
/// operator() scores the branchable dims (active_set ∩ bisectable — the same
/// eligibility as BranchLargestFirst):
///  - kAbs:     score_i = A_i / diam_i  (the faithful activity analog; under
///              uniform nonzero A it degenerates to smallest-diam-first — the
///              degeneracy-hypothesis arm);
///  - kAbsDiam: score_i = A_i * diam_i  (diameter-guarded; under uniform
///              nonzero A it degenerates to largest-first).
/// Ties: larger diameter, then lower index. All-zero A (the initial state)
/// scores every dim 0, so the diameter tie-break makes BOTH variants start as
/// largest-first. The chosen dim is bisected at the midpoint (Box::bisect,
/// exactly like BranchLargestFirst); returns -1 if no dim is bisectable.
///
/// SOUNDNESS: variable choice can only change how fast the search converges
/// (node count), never a verdict — a completeness/perf lever, never soundness.
/// Stateful (Decay/SnapshotDiams/BumpShrunk mutate the activity), so not
/// shareable across threads: IcpParallel builds one per worker.
class BrancherAbs {
 public:
  /// Sizes the activity vector to @p box (the ICP variable set is fixed for a
  /// whole solve — branching only narrows intervals; the size guards below
  /// enforce this). @p variant must not be kLargest (the caller only
  /// constructs a BrancherAbs when ABS is enabled).
  /// @throws std::runtime_error if @p decay is outside (0, 1].
  BrancherAbs(const Box& box, BrancherVariant variant, double decay);

  BrancherAbs(const BrancherAbs&) = delete;
  BrancherAbs(BrancherAbs&&) = delete;
  BrancherAbs& operator=(const BrancherAbs&) = delete;
  BrancherAbs& operator=(BrancherAbs&&) = delete;
  ~BrancherAbs() = default;

  /// Ages all activities: A_i *= decay. Call once per search node.
  void Decay();

  /// Records the per-dim diameters of @p box (the pre-Prune snapshot),
  /// overwriting the previous snapshot in place (no per-node allocation after
  /// the first). Caller must hold FE_UPWARD (witnessed by @p ur).
  void SnapshotDiams(const Box& box, const UpwardRounding& ur);

  /// Bumps (A_i += 1) every dim whose diameter in @p box strictly shrank vs
  /// the last SnapshotDiams, then consumes the snapshot. Call only after a
  /// non-emptying Prune.
  /// @throws std::runtime_error without a preceding unconsumed SnapshotDiams
  /// (a second bump would silently diff against stale diameters).
  void BumpShrunk(const Box& box, const UpwardRounding& ur);

  /// Brancher interface (same shape as BranchLargestFirst): chooses a
  /// dimension in @p active_set, splits @p box into @p left / @p right at the
  /// midpoint, and returns the dimension (or -1 if none bisectable). Caller
  /// must hold FE_UPWARD (witnessed by @p ur).
  int operator()(const Box& box, const DynamicBitset& active_set, Box* left,
                 Box* right, const UpwardRounding& ur) const;

 private:
  /// The fixed-variable-set guard: throws if @p box does not match the size
  /// the activity vector was built for.
  void CheckSize(const Box& box) const;

  BrancherVariant variant_;
  double decay_;
  std::vector<double> activity_;         // A_i; all-zero init.
  std::vector<double> pre_prune_diams_;  // SnapshotDiams scratch (reused);
                                         // empty = no unconsumed snapshot.
};

}  // namespace dreal
