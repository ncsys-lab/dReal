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
#include "dreal/solver/forall_formula_evaluator.h"

#include <limits>
#include <memory>
#include <set>
#include <utility>

#include "dreal/contractor/forall_counterexample_query.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/optional.h"

namespace dreal {

using std::ostream;
using std::set;
using std::vector;

namespace {

// Given f = [(e₁(x, y) ≥ 0) ∨ ... ∨ (eₙ(x, y) ≥ 0)], build an
// evaluator for each (eᵢ(x, y) ≥ 0) and return a vector of
// evaluators.
vector<RelationalFormulaEvaluator> BuildFormulaEvaluators(
    const set<Formula>& disjuncts) {
  vector<RelationalFormulaEvaluator> evaluators;
  evaluators.reserve(disjuncts.size());
  for (const Formula& disjunct : disjuncts) {
    DREAL_LOG_DEBUG("BuildFormulaEvaluators: disjunct = {}", disjunct);
    DREAL_ASSERT(
        is_relational(disjunct) ||
        (is_negation(disjunct) && is_relational(get_operand(disjunct))));
    evaluators.emplace_back(disjunct);
  }
  return evaluators;
}

vector<RelationalFormulaEvaluator> BuildFormulaEvaluators(const Formula& f) {
  DREAL_LOG_DEBUG("BuildFormulaEvaluators");
  const Formula& quantified_formula{get_quantified_formula(f)};
  DREAL_ASSERT(is_clause(quantified_formula));
  if (is_disjunction(quantified_formula)) {
    return BuildFormulaEvaluators(get_operands(quantified_formula));
  } else {
    return BuildFormulaEvaluators(std::set<Formula>{quantified_formula});
  }
}
}  // namespace

Context& ForallFormulaEvaluator::GetContext() const {
  return contexts_.GetOrCreate([this]() {
    Config config;  // number_of_jobs defaults to 1: the nested CE solve is IcpSeq.
    config.mutable_precision() = delta_;
    auto context = std::make_unique<Context>(config);
    for (const Variable& exist_var : formula().GetFreeVariables()) {
      context->DeclareVariable(exist_var);
    }
    for (const Variable& forall_var : get_quantified_variables(formula())) {
      context->DeclareVariable(forall_var);
    }
    context->Assert(StrengthenForallCounterexampleQuery(
        get_quantified_formula(formula()),
        get_quantified_variables(formula()), epsilon_));
    return context;
  });
}

ForallFormulaEvaluator::ForallFormulaEvaluator(Formula f, const double epsilon,
                                               const double delta,
                                               const int number_of_jobs)
    : FormulaEvaluatorCell{std::move(f)},
      evaluators_{BuildFormulaEvaluators(formula())},
      epsilon_{epsilon},
      delta_{delta},
      contexts_{number_of_jobs} {
  DREAL_ASSERT(is_forall(formula()));
  DREAL_LOG_DEBUG("ForallFormulaEvaluator({})", formula());
}

FormulaEvaluationResult ForallFormulaEvaluator::operator()(
    const Box& box, const UpwardRounding& ur) const {
  Context& context{GetContext()};
  // Copy exactly the variables the nested CE context declared: this atom's
  // free (existential) variables. The outer box can hold variables foreign to
  // this atom — copying those wrote through Box::operator[]'s old
  // default-insert onto dimension 0 of the nested CE box, silently corrupting
  // the CE search domain (see forall_narrow_domain_test.cc
  // OuterBoxVariableAbsentFromForallAtom).
  for (const Variable& v : formula().GetFreeVariables()) {
    context.SetInterval(v, box[v].lb(), box[v].ub());
  }
  optional<Box> counterexample = context.CheckSat();
  DREAL_LOG_DEBUG("ForallFormulaEvaluator::operator({})", box);
  if (counterexample) {
    DREAL_LOG_DEBUG("ForallFormulaEvaluator::operator()  --  CE found: ",
                    *counterexample);
    for (const Variable& exist_var : formula().GetFreeVariables()) {
      (*counterexample)[exist_var] = box[exist_var];
    }
    double max_diam = 0.0;
    for (const RelationalFormulaEvaluator& evaluator : evaluators_) {
      const FormulaEvaluationResult eval_result = evaluator(*counterexample, ur);
      double diam_i{0.0};
      if (eval_result.type() == FormulaEvaluationResult::Type::UNSAT) {
        diam_i = eval_result.evaluation().mag();
      } else {
        diam_i = safe_diam(eval_result.evaluation(), ur);
      }
      if (diam_i > max_diam) {
        max_diam = diam_i;
      }
    }
    return FormulaEvaluationResult{FormulaEvaluationResult::Type::UNKNOWN,
                                   Box::Interval(0.0, max_diam)};
  } else {
    DREAL_LOG_DEBUG("ForallFormulaEvaluator::operator()  --  No CE found: ");
    return FormulaEvaluationResult{FormulaEvaluationResult::Type::VALID,
                                   Box::Interval(0.0, 0.0)};
  }
}

ostream& ForallFormulaEvaluator::Display(ostream& os) const {
  return os << "ForallFormulaEvaluator(" << formula() << ")";
}

const Variables& ForallFormulaEvaluator::variables() const {
  return formula().GetFreeVariables();
}

}  // namespace dreal
