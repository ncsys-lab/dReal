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

/// Optimization-based bound tightening (OBBT) contractor: 2n certified LPs
/// over the X-Taylor linear relaxation, driving ibex::LPSolver directly.
///
/// Structurally mirrors ContractorIbexPolytope's assembly (same formula
/// filter, ibex::SystemFactory, LinearizerXTaylor(RELAX, RANDOM_OPP, HANSEN)),
/// but instead of delegating the LP loop to ibex::CtcPolytopeHull it owns the
/// ibex::LPSolver itself: per Prune it linearizes the system once over the
/// current box, then for each variable minimizes +x_i (lower bound) and -x_i
/// (upper bound) over that one polytope, intersecting only bounds the
/// Neumaier-Shcherbina postprocessing certifies (Status::OptimalProved /
/// Status::InfeasibleProved).
///
/// The LPSolver MUST be constructed with LPSolver::Mode::Certified: under the
/// default Mode::NotCertified the soplex wrapper never produces
/// OptimalProved/InfeasibleProved (the Neumaier-Shcherbina postprocessing is
/// gated on Certified in LPSolver::minimize,
/// lp_lib_wrapper/soplex/ibex_LPLibWrapper.cpp), so every LP result would be
/// rejected by the certification guard in Prune and the contractor would be a
/// silent no-op.
///
/// This is a COMPLETENESS lever — stronger contraction = fewer search nodes.
/// Soundness rests on two inclusions, argued at the two contraction sites in
/// the .cc: a certified LP optimum only ever discards points outside the
/// relaxation (which contains every real solution of the atoms), and a
/// certified-infeasible relaxation proves the atoms unsatisfiable over the
/// box. Uncertified LP statuses are never trusted — the first one stops the
/// LP pass as a pure identity (mirroring ibex::CtcPolytopeHull) — so a
/// false `unsat` (SOUNDNESS) cannot arise from LP round-off.
///
/// Prune runs gaol interval arithmetic (the X-Taylor cut coefficients and the
/// Neumaier-Shcherbina certification interval products), so the FE_UPWARD
/// rounding mode is load-bearing — a wrong ambient mode is a silent false
/// `unsat` (SOUNDNESS).
class ContractorIbexObbt : public ContractorCell {
 public:
  /// Constructs an OBBT contractor over @p formulas and @p box.
  ContractorIbexObbt(std::vector<Formula> formulas, const Box& box,
                     const Config& config);

  /// Deleted copy constructor.
  ContractorIbexObbt(const ContractorIbexObbt&) = delete;

  /// Deleted move constructor.
  ContractorIbexObbt(ContractorIbexObbt&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexObbt& operator=(const ContractorIbexObbt&) = delete;

  /// Deleted move assign operator.
  ContractorIbexObbt& operator=(ContractorIbexObbt&&) = delete;

  /// Default destructor.
  ~ContractorIbexObbt() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal LP machinery (no constraints).
  bool is_dummy() const;

 private:
  const std::vector<Formula> formulas_;
  bool is_dummy_{false};

  IbexConverter ibex_converter_;
  std::unique_ptr<ibex::SystemFactory> system_factory_;
  std::unique_ptr<ibex::System> system_;
  std::unique_ptr<ibex::LinearizerXTaylor> linear_relax_;
  std::unique_ptr<ibex::LPSolver> lp_;  // Mode::Certified — see class doc
  std::vector<std::unique_ptr<const ibex::ExprCtr, ExprCtrDeleter>> expr_ctrs_;
};

}  // namespace dreal
