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
#include "dreal/solver/icp.h"

#include <iostream>
#include <ostream>
#include <tuple>
#include <utility>

#include "dreal/util/logging.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/rounding.h"

using std::vector;

namespace dreal {

Icp::Icp(const Config& config) : config_{config} {}

void WarnDegenerateDeltaSat(const std::string& reason, const Box& box) {
  // Decimal formatting must run under FE_TONEAREST (docs/rounding.md). The
  // caller may be inside the ICP FE_UPWARD phase, so open a nested nearest
  // scope for the print; Box's operator<< brackets ibex's clobbering interval
  // printer itself, so the mode is FE_TONEAREST again at our dtor.
  const NearestRoundingScope print_scope;
  std::cerr << "WARNING: degenerate delta-sat: " << reason
            << ". The delta-sat verdict may be a missed refutation "
               "(COMPLETENESS; the verdict itself is unchanged). Mitigation: "
               "give every real variable finite, moderate bounds. Box:\n"
            << box << "\n";
}

optional<DynamicBitset> EvaluateBox(
    const vector<FormulaEvaluator>& formula_evaluators, const Box& box,
    const double precision, ContractorStatus* const cs,
    const UpwardRounding& ur) {
  DynamicBitset branching_candidates(box.size());  // Return value.
  for (const FormulaEvaluator& formula_evaluator : formula_evaluators) {
    const FormulaEvaluationResult result{formula_evaluator(box, ur)};
    switch (result.type()) {
      case FormulaEvaluationResult::Type::UNSAT:
        DREAL_LOG_DEBUG(
            "Icp::EvaluateBox() Found that the box\n"
            "{0}\n"
            "has no solution for {1} (evaluation = {2}).",
            box, formula_evaluator, result.evaluation());
        cs->mutable_box().set_empty();
        cs->AddUsedConstraint(formula_evaluator.formula());
        return nullopt;
      case FormulaEvaluationResult::Type::VALID:
        DREAL_LOG_DEBUG(
            "Icp::EvaluateBox() Found that all points in the box\n"
            "{0}\n"
            "satisfies the constraint {1} (evaluation = {2}).",
            box, formula_evaluator, result.evaluation());
        continue;
      case FormulaEvaluationResult::Type::UNKNOWN: {
        const Box::Interval& evaluation{result.evaluation()};
        const double diam = safe_diam(evaluation, ur);
        if (diam > precision) {
          DREAL_LOG_DEBUG(
              "Icp::EvaluateBox() Found an interval >= precision({2}):\n"
              "{0} -> {1}",
              formula_evaluator, evaluation, precision);
          if (formula_evaluator.is_simple_relational() ||
              formula_evaluator.is_neq()) {
            // Note: when the base formula is simple relational or not-equal, we
            // do not need to branch on the base variable.
          } else {
            for (const Variable& v : formula_evaluator.variables()) {
              branching_candidates.set(box.index(v));
            }
          }
        }
        break;
      }
    }
  }
  return branching_candidates;
}

}  // namespace dreal
