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

#include <string>
#include <vector>

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/solver/formula_evaluator.h"
#include "dreal/util/box.h"
#include "dreal/util/dynamic_bitset.h"
#include "dreal/util/optional.h"

namespace dreal {

// Ref: docs/papers/gao-avigad-clarke-2012-delta-complete.md (DPLL(ICP) δ-completeness,
// Thm. 4.2 / Cor. 4.1) and docs/papers/gao-kong-clarke-2013-dreal.md (the dReal tool's
// branch-and-prune theory solver). The branch-and-prune loop below is δ-complete iff the
// pruning operators are well-defined (W1–W3).
/// Abstract Class for ICP (Interval Constraint Propagation) algorithm.
class Icp {
 public:
  /// Constructs an Icp based on @p config.
  explicit Icp(const Config& config);
  Icp(const Icp&) = default;
  Icp(Icp&&) = default;
  Icp& operator=(const Icp&) = delete;
  Icp& operator=(Icp&&) = delete;

  virtual ~Icp() = default;

  /// Checks the delta-satisfiability of the current assertions.
  /// @param[in] contractor Contractor to use in pruning phase
  /// @param[in] formula_evaluators A vector of FormulaEvaluator which
  ///                               determines when to stop and which
  ///                               dimension to branch.
  /// @param[in,out] cs A contractor to be updated.
  /// Returns true  if it's delta-SAT.
  /// Returns false if it's UNSAT.
  virtual bool CheckSat(const Contractor& contractor,
                        const std::vector<FormulaEvaluator>& formula_evaluators,
                        ContractorStatus* cs) = 0;

 protected:
  const Config& config() const { return config_; }

 private:
  const Config& config_;
};

/// Tag carried beside each Box in an ICP DFS stack entry `(Box, tag)`:
///
///   >= 0              — the dimension branched on to create this box.
///                       Written into ContractorStatus::branching_point before
///                       the box's Prune, so ContractorWorklistFixpoint seeds
///                       only the contractors whose inputs depend on that
///                       dimension (the incremental-repruning optimization).
///   -1                — no branching information (the --seed-samples seed
///                       boxes); the worklist fixpoint full-seeds.
///   kAlreadyPrunedTag — the ROOT box, pruned once before being pushed. The
///                       loop prunes the root up front (an empty root returns
///                       unsat immediately, and every box is pruned exactly
///                       once per pop; the --seed-samples pre-pass proposes
///                       from an UN-PRUNED root snapshot — see the seed-input
///                       comment in icp_parallel.cc for why the R3 draft's
///                       pruned-box input was rejected); the pop that sees
///                       this tag skips the redundant re-prune.
///                       The skip is a pure identity guard (no contraction):
///                       the box only stays (weakly) wider than a re-prune
///                       would leave it, which can never narrow past a true
///                       model — never SOUNDNESS (a false unsat requires
///                       narrowing past a T-model) — and delta-sat acceptance
///                       still passes through the unchanged EvaluateBox
///                       arbiter.
///
/// The tag itself never enters ContractorStatus::mutable_branching_point()
/// (whose domain is -1 or a valid dimension); the loops translate it to -1.
constexpr int kAlreadyPrunedTag{-2};

/// Evaluates each formula with @p box using interval
/// arithmetic. There are three possible outcomes:
///
/// Returns None                if there is fᵢ such that fᵢ(box) is empty.
///                             (This indicates the problem is UNSAT)
///
/// Returns Some(∅)             if for all fᵢ, we have either
///                             1) fᵢ(x) is valid for all x ∈ B *or*
///                             2) |fᵢ(B)| ≤ δ.
///                             (This indicates the problem is delta-SAT)
///
/// Returns Some(Vars)          if there is fᵢ such that
///                             1) Interval arithmetic can't validate that
///                                fᵢ(x) is valid for all x ∈ B *and*
///                             2) |fᵢ(B)| > δ.
///                             Vars = {v | v ∈ fᵢ ∧ |fᵢ(B)| > δ for all
///                             fᵢs}.
///
///                             It cannot conclude if the constraint
///                             is satisfied or not completely. It
///                             checks the width/diameter of the
///                             interval evaluation and adds the free
///                             variables in the constraint into the
///                             set that it will return.
///
/// If it returns an DynamicBitset, it represents the dimensions on
/// which the ICP algorithm needs to consider branching.
///
/// It sets @p cs's box empty if it detects UNSAT. It also calls
/// cs->AddUsedConstraint to store the constraint that is responsible
/// for the UNSAT.
optional<DynamicBitset> EvaluateBox(
    const std::vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    double precision, ContractorStatus* cs, const UpwardRounding& ur);

/// Loud stderr warning that a delta-sat verdict is degenerate — reachable from
/// both Icp variants and the final-verdict surface (context_impl.cc). The two
/// callers' reasons:
///   "non-bisectable box below delta"      — upstream dreal/dreal4#68: the box
///       still violates the delta-condition but its endpoints are adjacent
///       floats, so ICP cannot bisect further and returns delta-sat (the
///       IcpParallel::Worker exit).
///   "witness box has unbounded endpoints" — upstream dreal/dreal4#265: an
///       unbounded variable interval admits a +-inf witness endpoint; the
///       mitigation is giving every real finite bounds (checked once where the
///       final model box is extracted).
/// Both are COMPLETENESS hazards (the verdict may assert phi^delta
/// T-satisfiable on a T-unsatisfiable phi — a missed refutation); neither is a
/// SOUNDNESS bug, and this warning changes no verdict, exit code, or stdout
/// byte (stdout is consumed by downstream substring checks).
void WarnDegenerateDeltaSat(const std::string& reason, const Box& box);

/// stderr warning for a delta-sat theory check during which at least one ODE
/// constraint could not be integrated on some box (the approved inconclusive
/// skip, docs/decisions.md "ODE inconclusive skip"). Verdict unchanged.
void WarnInconclusiveOdeDeltaSat(const ContractorStatus& cs);

}  // namespace dreal
