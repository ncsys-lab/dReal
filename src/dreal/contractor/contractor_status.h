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

#include <set>
#include <string>
#include <vector>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/dynamic_bitset.h"

namespace dreal {

/// Contractor status
class ContractorStatus {
 public:
  ContractorStatus() = delete;

  /// Constructs a contractor status with @p box and @p branching_point.
  explicit ContractorStatus(Box box, int branching_point = -1);

  /// Returns a const reference of the embedded box.
  const Box& box() const;

  /// Returns a mutable reference of the embedded box.
  Box& mutable_box();

  /// Returns the branching_point.
  int branching_point() const;

  /// Returns a mutable reference of the branching_point.
  int& mutable_branching_point();

  /// Returns a const reference of the output field.
  const DynamicBitset& output() const;

  /// Returns a mutable reference of the output field.
  DynamicBitset& mutable_output();

  /// Returns explanation, a list of formula responsible for the unsat.
  FormulaSet Explanation() const;

  /// The used constraints, each with the variables through which an
  /// explanation reaches it (see AddUsedConstraint).
  const FormulaMap<Variables>& UsedConstraints() const {
    return used_constraints_;
  }

  /// Add a formula @p f into the used constraints. An explanation reaches it
  /// through any of its variables.
  void AddUsedConstraint(const Formula& f);

  /// Add a formula @p f into the used constraints for a contraction that read
  /// and wrote only @p reach, a subset of its variables: an explanation reaches
  /// @p f through @p reach alone. Recording @p f again unions the two sets.
  /// Throws if @p reach is not a subset of f's variables.
  void AddUsedConstraint(const Formula& f, const Variables& reach);

  /// Add a formula @p formulas into the used constraints.
  void AddUsedConstraint(const std::vector<Formula>& formulas);

  /// Records an ODE constraint that the Lohner contractor could neither refute
  /// nor narrow on this box (CAPD diverged, or another inconclusive early
  /// return). Such a constraint is logically in play but leaves no trace in
  /// @c used_constraints_, so the explanation would omit it (a soundness hole —
  /// see docs/constraint-order-explanation-soundness.md). Unlike
  /// AddUsedConstraint it never seeds the unsat witness (divergence does not
  /// empty the box); it is spliced into the explanation as a non-expanding leaf.
  /// @p reason says why the pass was inconclusive; the first reason recorded for
  /// a formula is kept.
  void AddInconclusiveOde(const Formula& f, const std::string& reason);

  /// ODE constraints left un-narrowed by an inconclusive pass, with the reason.
  const FormulaMap<std::string>& inconclusive_odes() const {
    return inconclusive_odes_;
  }

  /// Add a variable @p var which is directly responsible for the unsat.
  void AddUnsatWitness(const Variable& var);

  /// Updates the contractor status by taking join with @p contractor_status.
  ///
  /// @pre The boxes of this and @p contractor_status have the same variables
  /// vector.
  ContractorStatus& InplaceJoin(const ContractorStatus& contractor_status);

 private:
  // The current box to prune. Most of contractors are updating
  // this member.
  Box box_;

  // If the nested box is obtained from a branching operation, this field
  // records the dimension (variable) where the branching occurred. The default
  // value is -1, which indicates that the box is not obtained by a branching.
  //
  // This field is used in worklist-fixpoint contractor.
  int branching_point_{-1};

  // "output_[i] == 1" means that the value of the i-th variable is
  // changed after running the contractor.
  DynamicBitset output_;

  // The constraints used during pruning processes, each with the variables
  // through which an explanation reaches it. This is an over-approximation of
  // an explanation.
  FormulaMap<Variables> used_constraints_;

  // ODE constraints that an inconclusive Lohner pass (CAPD divergence etc.)
  // could not record in used_constraints_. Spliced into the explanation as
  // non-expanding leaves so only those touching the relational refutation
  // footprint enter the lemma (avoids dragging in the whole densely-chained
  // BMC ODE web). See AddInconclusiveOde and GenerateExplanation.
  FormulaMap<std::string> inconclusive_odes_;

  // A set of variables directly responsible for the unsat result. This
  // is used to generate an explanation.
  Variables unsat_witness_;
};

/// Returns a join of @p contractor_status1 and @p contractor_status2.
///
/// @pre The boxes of the two ContractorStatus should have the same variables
/// vector.
ContractorStatus Join(ContractorStatus contractor_status1,
                      const ContractorStatus& contractor_status2);

}  // namespace dreal
