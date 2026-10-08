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
#include "dreal/solver/sat_solver.h"

#include <iostream>
#include <ostream>
#include <utility>
#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/util/predicate_normalizer.h>

#include "auditor.h"
#include "dreal/version.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"

namespace dreal {

using std::cout;
using std::vector;

SatSolver::SatSolver(const Config& config) : cadical(new CaDiCaL::Solver) {
  // SatSolver::CheckSat().
  bool success = false;
  if (config.random_seed() != 0) {
    success = cadical->set("seed", config.random_seed()); DREAL_ASSERT(success);
    DREAL_LOG_DEBUG("SatSolver::Set Random Seed {}", config.random_seed());
  }

  success = cadical->set("vivify", 1); DREAL_ASSERT(success);
  success = cadical->set("vivifyonce", 2); DREAL_ASSERT(success);
  success = cadical->set("eagersubsume", 1); DREAL_ASSERT(success);
  success = cadical->set("subsume", 1); DREAL_ASSERT(success);
  success = cadical->set("subsumeclslim", 1e3); DREAL_ASSERT(success);
  // Disable CaDiCaL bounded-variable-addition factoring. With factor on,
  // CaDiCaL creates internal *extension* variables; cadical->vars() then counts
  // them, and the model-reading loops below iterate up to cadical->vars() and
  // call val() on each index, which aborts on an extension var
  // ("extension variable ... defined by the solver internally"). dReal only
  // ever wants values for its own user variables (1 .. cadical_next_var-1). The
  // cadical->resize() guard in MakeSatVar is not enough because factoring still
  // adds fresh vars beyond the declared user range during solving.
  success = cadical->set("factor", 0); DREAL_ASSERT(success);
  cadical->options();

  if (DREAL_LOG_INFO_ENABLED || DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) cadical->connect_learner(this);
}

SatSolver::~SatSolver() { delete cadical; }

void SatSolver::AddFormula(const Formula& f) {
  DREAL_LOG_DEBUG("SatSolver::AddFormula({})", f);
  vector<Formula> clauses{cnfizer_.Convert(f)};
  // Collect Tseitin variables.
  for (const auto& p : cnfizer_.map()) {
    tseitin_variables_.insert(p.first.get_id());
  }
  for (Formula& clause : clauses) {
    AddClause(predicate_abstractor_.Convert(clause));
  }
}

void SatSolver::AddClause(const Formula& f) {
  DREAL_LOG_DEBUG("SatSolver::AddClause({})", f);
  // Set up Variable ⇔ Literal (in SAT) map.
  for (const Variable& var : f.GetFreeVariables()) {
    MakeSatVar(var);
  }
  // Add clauses to SAT solver.
  if (is_disjunction(f)) {
    // f = l₁ ∨ ... ∨ lₙ
    for (const Formula& l : get_operands(f)) {
      AddLiteral(l);
    }
  } else {
    // f = b or f = ¬b.
    AddLiteral(f);
  }
  cadical->add(0);
  sat_log_literal0();
}

void SatSolver::AddLearnedClauseDirect(
    const std::vector<Formula>& conflicting_conjunction, const Box& unfitted_box
) {
  sat_log_label_clause("SatSolver::AddLearnedClauseDirect");
  for (const Formula& f : conflicting_conjunction) {
    AddLiteral(!predicate_abstractor_.Convert(f));
  }
  cadical->add(0);
  sat_log_literal0();

  if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
    std::vector<Formula> a;
    a.reserve(conflicting_conjunction.size());
    for (const Formula& f : conflicting_conjunction) a.emplace_back(!predicate_abstractor_.Convert(f));
    theory_audit_literals("AddLearnedClauseDirect", predicate_abstractor_, a, unfitted_box);
  }
}

namespace {
class SatSolverStat : public Stat {
 public:
  explicit SatSolverStat(const bool enabled) : Stat{enabled} {};
  SatSolverStat(const SatSolverStat&) = default;
  SatSolverStat(SatSolverStat&&) = default;
  SatSolverStat& operator=(const SatSolverStat&) = delete;
  SatSolverStat& operator=(SatSolverStat&&) = delete;
  ~SatSolverStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n", "Total # of CheckSat",
            "SAT level", num_check_sat_);
      print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
            "Total time spent in SAT checks", "SAT level",
            timer_check_sat_.seconds());
    }
  }

  int num_check_sat_{0};
  Timer timer_check_sat_;
};
}  // namespace

optional<std::pair<SatSolver::Model, bool>> SatSolver::CheckSat(const bool request_fully_constrained) {
  static SatSolverStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_LOG_TRACE("SatSolver::CheckSat(#vars = {}, #clauses = {})",
                  cadical->vars(),
                  cadical->irredundant());
  stat.num_check_sat_++;
  // Call SAT solver.
  TimerGuard check_sat_timer_guard(&stat.timer_check_sat_,DREAL_LOG_INFO_ENABLED);
  const int ret{cadical->solve()};
  // check_sat_timer_guard.pause();

  Model model;
  if (ret == CaDiCaL::SATISFIABLE) {
    // SAT Case.

    int num_literals_omitted = 0;
    // BRITTLE: every cadical->vars() loop in this function assumes CaDiCaL's
    // factor/BVA is OFF (disabled in the ctor). With factor on, cadical->vars()
    // includes solver-internal extension vars and val() on them aborts; these
    // loops would then have to be bounded by (cadical_next_var - 1) instead.
    std::vector<int> model_is(cadical->vars() + 1);
    if (request_fully_constrained) {
      num_literals_omitted = 0;
      for (int i = 1; i <= cadical->vars(); ++i) model_is[i] = cadical->val(i) > 0 ? +1 : -1;  // BRITTLE: see factor/BVA note above
    }
    else {
      num_literals_omitted = get_partial_model(model_is);
      // DREAL_LOG_INFO("SatSolver::CheckSat - Shrank model by {}%", num_literals_omitted * 100.0 / cadical->vars());
    }
    // std::cerr << "O " << num_literals_omitted << " l f m o s " << cadical->vars() << ".\n";

    if (DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) {
      if (/*model_is_fully_constrained*/ num_literals_omitted == 0) {
        sat_log_label_clause("SatSolver::CheckSat - Fully Constrained");
      } else {
        sat_log_label_clause("SatSolver::CheckSat - Partially Constrained");
      }
      for (int i = 1; i <= cadical->vars(); ++i) if (model_is[i] != 0) sat_log_literal(i * model_is[i]);  // BRITTLE: see factor/BVA note above
      sat_log_literal0();

      // check that we haven't OVER constrained somehow.
      for (int i = 1; i <= cadical->vars(); ++i) if (model_is[i] != 0) cadical->assume(i * model_is[i]);  // BRITTLE: see factor/BVA note above
      int result = cadical->solve(); // must call OUTSIDE of DREAL_ASSERT since we added a bunch of `assumes`
      DREAL_ASSERT(result == CaDiCaL::SATISFIABLE);
    }

    const auto& var_to_formula_map = predicate_abstractor_.var_to_formula_map();
    for (int i = 1; i <= cadical->vars(); ++i) {  // BRITTLE: see factor/BVA note above
      const auto model_i = model_is[i];
      if (model_i == 0) {
        continue;
      }
      const auto it_var = to_sym_var_.find(i);
      if (it_var == to_sym_var_.end()) {
        // There is no symbolic::Variable corresponding to this
        // picosat variable (int). This could be because of
        // picosat_push/pop.
        continue;
      }
      const Variable& var{it_var->second};
      const auto it = var_to_formula_map.find(var);
      if (it != var_to_formula_map.end()) {
        DREAL_LOG_TRACE("SatSolver::CheckSat: Add theory literal {}{} to Model",
                        model_i == 1 ? "" : "¬", fmt::streamed(var));
        auto& theory_model = model.second;
        theory_model.emplace_back(var, model_i == 1);
      } else if (tseitin_variables_.count(var.get_id()) == 0) {
        DREAL_LOG_TRACE(
            "SatSolver::CheckSat: Add Boolean literal {}{} to Model ",
            model_i == 1 ? "" : "¬", fmt::streamed(var));
        auto& boolean_model = model.first;
        boolean_model.emplace_back(var, model_i == 1);
      } else {
        DREAL_LOG_TRACE(
            "SatSolver::CheckSat: Skip {}{} which is a temporary variable.",
            model_i == 1 ? "" : "¬", fmt::streamed(var));
      }
    }
    DREAL_LOG_DEBUG("SatSolver::CheckSat() Found a model.");
    return {{model, /*model_is_fully_constrained*/ num_literals_omitted == 0}};
  } else if (ret == CaDiCaL::UNSATISFIABLE) {
    DREAL_LOG_DEBUG("SatSolver::CheckSat() No solution.");
    // UNSAT Case.
    return {};
  } else {
    DREAL_ASSERT(ret == CaDiCaL::UNKNOWN);
    DREAL_LOG_CRITICAL("CaDiCaL returns CaDiCaL::UNKNOWN.");
    throw DREAL_RUNTIME_ERROR("CaDiCaL returns CaDiCaL::UNKNOWN.");
  }
}

void SatSolver::AddLiteral(const Formula& f) {
  DREAL_ASSERT(is_variable(f) ||
               (is_negation(f) && is_variable(get_operand(f))));
  if (is_variable(f)) {
    // f = b
    const Variable& var{get_variable(f)};
    DREAL_ASSERT(var.get_type() == Variable::Type::BOOLEAN);
    // Add l = b
    cadical->add(to_sat_var_[var.get_id()]);
    sat_log_literal(to_sat_var_[var.get_id()], var);
  } else {
    // f = ¬b
    DREAL_ASSERT(is_negation(f) && is_variable(get_operand(f)));
    const Variable& var{get_variable(get_operand(f))};
    DREAL_ASSERT(var.get_type() == Variable::Type::BOOLEAN);
    // Add l = ¬b
    cadical->add(-to_sat_var_[var.get_id()]);
    sat_log_literal(-to_sat_var_[var.get_id()], var);
  }
}

void SatSolver::MakeSatVar(const Variable& var) {
  auto it = to_sat_var_.find(var.get_id());
  if (it != to_sat_var_.end()) {
    // Found.
    return;
  }
  // It's not in the maps, let's make one and add it.
  const int sat_var{cadical_next_var++};
  // CaDiCaL 3.0+ requires variables to be declared before use when factor/BVA is on.
  cadical->resize(sat_var);
  // std::cout << "Assigning `" << var << "` (id #"<< var.get_id() <<") to " << sat_var << std::endl;
  to_sat_var_.insert(var.get_id(), sat_var);
  to_sym_var_.insert(sat_var, var);
  DREAL_LOG_DEBUG("SatSolver::MakeSatVar({} ↦ {})", fmt::streamed(var), sat_var);
}

Formula SatSolver::theory_literal(const Variable& var) const {
  return predicate_abstractor_[var];
}

// CaDiCaL::Learner callbacks (connected in the constructor): observe clauses
// CaDiCaL itself learns, for logging/audit only.
bool SatSolver::learning(const int size) {
  buffer_i = 0;
  expected_clause_size = size;
  if (size <= LOG_INFO_SIZE) return true;
  if (DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED && size < MAX_BUFFER_SIZE) return true;
  return false;
}

void SatSolver::learn(const int new_lit) {
  if (new_lit != 0) {
    DREAL_ASSERT(buffer_i < expected_clause_size);
    buffer[buffer_i] = new_lit;
    buffer_i++;
    return;
  } // else {

  DREAL_ASSERT(buffer_i == expected_clause_size);

  sat_log_label_clause("SatSolver::learn");
  FormulaSet neg_conjunction;
  for (int i = 0; i < expected_clause_size; i++) {
    const int lit = buffer[i];
    sat_log_literal(lit);
    const bool lit_is_neg = lit < 0;

    const auto sym_var_it = to_sym_var_.find(lit_is_neg ? -lit : +lit);
    DREAL_ASSERT(sym_var_it != to_sym_var_.end());
    const auto theory_lit_it = predicate_abstractor_.var_to_formula_map().find(sym_var_it->second);
    const Formula theory_lit =
        theory_lit_it == predicate_abstractor_.var_to_formula_map().end()
            ? Formula{sym_var_it->second}
            : theory_lit_it->second;
    neg_conjunction.emplace(
        !(lit_is_neg ? !theory_lit : theory_lit)
    );
  }
  sat_log_literal0();
  if (DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) {
    const Formula sat_clause = !make_conjunction(neg_conjunction);
    sat_log_label_clause("i.e. " + sat_clause.to_string());
  }
  if (expected_clause_size <= LOG_INFO_SIZE) {
    const Formula sat_clause = !make_conjunction(neg_conjunction);
    std::cerr << "SAT Solver Learned: " << sat_clause << '\n';
  }
}

}  // namespace dreal
