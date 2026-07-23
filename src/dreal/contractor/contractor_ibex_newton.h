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
#include <ostream>
#include <vector>

#include "ibex.h"

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_cell.h"
#include "dreal/contractor/contractor_ibex_polytope.h"  // for ExprCtrDeleter
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/ibex_converter.h"

namespace dreal {

/// Contractor wrapping IBEX's interval-Newton (Hansen-Sengupta) contractor
/// ibex::CtcNewton on the square equality subsystem of the assertions.
/// Structurally mirrors ContractorIbexAcid: it assembles an ibex::System over
/// the box variables and the positive equality constraints, then wraps a
/// single ibex::CtcNewton over the variable set S occurring in those
/// equalities — via the plain all-variables ctor when S covers every box
/// variable, and restricted via ibex::VarSet (non-S columns become interval
/// parameters) only when S is a proper subset. The dispatch is ibex's own API
/// split: the parameterized newton() with zero parameter columns is
/// unsupported upstream (its m×0 parameter Jacobian has NULL row storage in a
/// Release build — see the ctor comment). If the subsystem is not square — m
/// equalities with |S| != m, or m == 0 — the cell is a dummy identity
/// (ibex::CtcNewton itself throws not_implemented on rectangular systems, so
/// squareness is guaranteed before construction).
///
/// Interval Newton only discards regions provably free of exact solutions of
/// the equality subsystem — the same contraction contract as HC4/ACID on
/// equalities. This is a COMPLETENESS lever (its absence can only mean a
/// missed refutation — asserts φ^δ T-satisfiable on a T-unsatisfiable φ —
/// never a verdict move); stronger contraction near regular roots = fewer
/// search nodes. CtcNewton additionally gates itself on
/// box.max_diam() <= ceil (--newton-ceil), so wide boxes pass through
/// untouched — the gate tests the FULL box's max diameter, parameter and
/// non-S columns included (ibex_CtcNewton.cpp), so a single wide or unbounded
/// dimension anywhere suppresses the Newton step entirely.
///
/// Prune runs real gaol interval arithmetic (Jacobian evaluation +
/// Gauss-Seidel), so the FE_UPWARD rounding mode is load-bearing — a wrong
/// ambient mode is a silent false `unsat` (SOUNDNESS: asserts φ
/// T-unsatisfiable on a T-satisfiable φ).
class ContractorIbexNewton : public ContractorCell {
 public:
  /// Constructs a Newton contractor over the equality subset of @p formulas
  /// and @p box.
  ContractorIbexNewton(std::vector<Formula> formulas, const Box& box,
                       const Config& config);

  /// Deleted copy constructor.
  ContractorIbexNewton(const ContractorIbexNewton&) = delete;

  /// Deleted move constructor.
  ContractorIbexNewton(ContractorIbexNewton&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexNewton& operator=(const ContractorIbexNewton&) = delete;

  /// Deleted move assign operator.
  ContractorIbexNewton& operator=(ContractorIbexNewton&&) = delete;

  /// Default destructor.
  ~ContractorIbexNewton() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor (no square equality
  /// subsystem).
  bool is_dummy() const;

 private:
  const std::vector<Formula> formulas_;
  bool is_dummy_{false};

  IbexConverter ibex_converter_;
  std::unique_ptr<ibex::SystemFactory> system_factory_;
  std::unique_ptr<ibex::System> system_;
  // Set only in the proper-subset dispatch branch (null when the equalities
  // cover every box variable); referenced by ctc_, so it outlives it.
  std::unique_ptr<ibex::VarSet> var_set_;
  std::unique_ptr<ibex::CtcNewton> ctc_;
  std::vector<std::unique_ptr<const ibex::ExprCtr, ExprCtrDeleter>> expr_ctrs_;
};

}  // namespace dreal
