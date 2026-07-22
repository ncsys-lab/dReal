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

#include <mutex>
#include <ostream>
#include <utility>
#include <vector>

#include "ibex.h"

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/rounding.h"

namespace dreal {

class ExpressionEvaluator {
 public:
  explicit ExpressionEvaluator(Expression e);

  /// Copies share the expression but start with a fresh (empty) index cache —
  /// the cache is pure memoization keyed to the boxes each instance sees.
  ExpressionEvaluator(const ExpressionEvaluator& other);

  /// Evaluates the expression with @p box. The Visit functions use ibex/gaol,
  /// so the caller must hold an UpwardRounding token (FE_UPWARD established).
  Box::Interval operator()(const Box& box, const UpwardRounding& ur) const;

  const Variables& variables() const { return e_.GetVariables(); }

 private:
  Box::Interval Visit(const Expression& e, const Box& box) const;
  Box::Interval VisitVariable(const Expression& e, const Box& box) const;
  static Box::Interval VisitConstant(const Expression& e, const Box& box);
  static Box::Interval VisitRealConstant(const Expression& e, const Box& box);
  Box::Interval VisitAddition(const Expression& e, const Box& box) const;
  Box::Interval VisitMultiplication(const Expression& e, const Box& box) const;
  Box::Interval VisitDivision(const Expression& e, const Box& box) const;
  Box::Interval VisitLog(const Expression& e, const Box& box) const;
  Box::Interval VisitAbs(const Expression& e, const Box& box) const;
  Box::Interval VisitExp(const Expression& e, const Box& box) const;
  Box::Interval VisitSqrt(const Expression& e, const Box& box) const;
  Box::Interval VisitPow(const Expression& e, const Box& box) const;

  // Evaluates `pow(e1, e2)` with the @p box.
  Box::Interval VisitPow(const Expression& e1, const Expression& e2,
                         const Box& box) const;
  Box::Interval VisitSin(const Expression& e, const Box& box) const;
  Box::Interval VisitCos(const Expression& e, const Box& box) const;
  Box::Interval VisitTan(const Expression& e, const Box& box) const;
  Box::Interval VisitAsin(const Expression& e, const Box& box) const;
  Box::Interval VisitAcos(const Expression& e, const Box& box) const;
  Box::Interval VisitAtan(const Expression& e, const Box& box) const;
  Box::Interval VisitAtan2(const Expression& e, const Box& box) const;
  Box::Interval VisitSinh(const Expression& e, const Box& box) const;
  Box::Interval VisitCosh(const Expression& e, const Box& box) const;
  Box::Interval VisitTanh(const Expression& e, const Box& box) const;
  Box::Interval VisitMin(const Expression& e, const Box& box) const;
  Box::Interval VisitMax(const Expression& e, const Box& box) const;
  static Box::Interval VisitIfThenElse(const Expression& e, const Box& box);
  static Box::Interval VisitUninterpretedFunction(const Expression& e,
                                                  const Box& box);

  // Makes VisitExpression a friend of this class so that it can use private
  // operator()s.
  friend Box::Interval drake::symbolic::VisitExpression<Box::Interval>(
      const ExpressionEvaluator*, const Expression&, const Box&);

  friend std::ostream& operator<<(
      std::ostream& os, const ExpressionEvaluator& expression_evaluator);

  // Builds idx_cache_ from @p box's variable->index mapping. Throws if a
  // variable of e_ is absent from @p box.
  void BuildIndexCache(const Box& box) const;

  const Expression e_;

  // Flat-index cache killing the per-visit unordered_map lookup of
  // Box::operator[](Variable): (Variable::Id -> Box index) pairs sorted by id,
  // built via call_once from the FIRST box this instance evaluates (thread-safe;
  // immutable afterwards — evaluators are shared across IcpParallel workers).
  // Box::Add is append-only and copy-on-write clones preserve order, so an
  // index is stable across every box derived from the first one. VisitVariable
  // additionally validates each cached index against the box at hand before
  // using it (one vector deref + id compare); a box with a genuinely different
  // layout (e.g. a forall/CEGIS counterexample box hitting a shared evaluator)
  // falls back to honest per-visit resolution — a wrong-slot read is impossible.
  mutable std::once_flag idx_cache_once_;
  mutable std::vector<std::pair<Variable::Id, int>> idx_cache_;
};

std::ostream& operator<<(std::ostream& os,
                         const ExpressionEvaluator& expression_evaluator);

}  // namespace dreal

template <> struct fmt::formatter<dreal::ExpressionEvaluator> : fmt::ostream_formatter {};
