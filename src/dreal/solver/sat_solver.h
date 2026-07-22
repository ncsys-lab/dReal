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

#include <memory>
#include <set>
#include <utility>
#include <vector>

// #include "./picosat.h"
#include <dreal/util/predicate_normalizer.h>

#include "cadical.hpp"
#include "dreal/version.h"

#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/optional.h"
#include "dreal/util/predicate_abstractor.h"
#include "dreal/util/scoped_unordered_map.h"
#include "dreal/util/scoped_unordered_set.h"
#include "dreal/util/tseitin_cnfizer.h"
#include "dreal/util/pattern_matching/matching_stats_t.h"


namespace dreal {

class SatSolver : public CaDiCaL::Learner {
 public:
  using Literal = std::pair<Variable, bool>;

  // Boolean model + Theory model.
  using Model = std::pair<std::vector<Literal>, std::vector<Literal>>;

  /// Constructs a SatSolver.
  explicit SatSolver(const Config& config);

  /// Deleted copy constructor.
  SatSolver(const SatSolver&) = delete;

  /// Deleted move constructor.
  SatSolver(SatSolver&&) = delete;

  /// Deleted copy-assignment operator.
  SatSolver& operator=(const SatSolver&) = delete;

  /// Deleted move-assignment operator.
  SatSolver& operator=(SatSolver&&) = delete;

  ~SatSolver();

  /// Adds a formula @p f to the solver.
  ///
  /// @note If @p f is a clause, please use AddClause function. This
  /// function does not assume anything about @p f and perform
  /// pre-processings (CNFize and PredicateAbstraction).
  void AddFormula(const Formula& f);

  /// Given a @p formulas = {f₁, ..., fₙ}, adds a clause (¬f₁ ∨ ... ∨ ¬ fₙ) to
  /// the solver.
  void AddLearnedClauseDirect(const std::vector<Formula>& conflicting_conjunction, const Box& unfitted_box);

  matching_stats_t AddLearnedClausePattern(
      PredicateNormalizer& pn,
      const std::vector<Formula>& base_conflict,
      const Box& base_box, std::chrono::duration<uint64_t, std::micro> timeout);

  /// Checks the satisfiability of the current configuration.
  ///
  /// @returns a witness, satisfying model if the problem is satisfiable.
  /// @returns nullopt if UNSAT.
  optional<std::pair<Model, bool>> CheckSat(
    bool request_fully_constrained = DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS
  );

  [[nodiscard]] Formula theory_literal(const Variable& var) const;

private:
  // Adds a formula @p f to the solver.
  //
  // @pre @p f is a clause. That is, it is either a literal (b or ¬b)
  // or a disjunction of literals (l₁ ∨ ... ∨ lₙ).
  void AddClause(const Formula& f);

  // Returns a corresponding literal ID of @p var. It maintains two
  // maps `lit_to_var_` and `var_to_lit_` to keep track of the
  // relationship between Variable ⇔ Literal (in SAT).
  void MakeSatVar(const Variable& var);

  // Add a symbolic formula @p f to @p clause.
  //
  // @pre @p f is either a Boolean variable or a negation of Boolean
  // variable.
  void AddLiteral(const Formula& f);

  int get_partial_model(std::vector<int>& model_is);

  // Member variables
  // ----------------
  CaDiCaL::Solver* const cadical;
  int cadical_next_var = 1;

  TseitinCnfizer cnfizer_;
  PredicateAbstractor predicate_abstractor_;

  // Map symbolic::Variable → int (Variable type in CaDiCaL).
  ScopedUnorderedMap<Variable::Id, int> to_sat_var_;

  // Map int (Variable type in CaDiCaL) → symbolic::Variable.
  ScopedUnorderedMap<int, Variable> to_sym_var_;

  /// Set of temporary Boolean variables introduced by Tseitin
  /// transformations.
  ScopedUnorderedSet<Variable::Id> tseitin_variables_;

  // learner stuff.
private:
  static constexpr size_t LOG_INFO_SIZE = 2;
  static constexpr size_t MAX_BUFFER_SIZE = 8;
  int buffer[MAX_BUFFER_SIZE] = {0};
  int buffer_i = 0;
  int expected_clause_size = 0;

public:
  bool learning(int size) override;
  void learn(int new_lit) override;
};
} // namespace dreal
