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

/// Contractor wrapping IBEX's Mohc contractor (Araya, Trombettoni, Neveu —
/// "Exploiting Monotonicity in Interval Constraint Propagation", AAAI'10),
/// ported onto the 2.9.1 fork kernel (fork src/contractor/ibex_CtcMohc.h).
/// Structurally mirrors ContractorIbexAcid: it assembles an ibex::System over
/// the box variables and the (non-forall) assertion constraints, then wraps a
/// single ibex::CtcMohc built from `system_->ctrs` (the System's
/// Array<NumConstraint>, ibex_System.h) with ALL IBEX defaults:
///   ratio                 = CtcPropag::default_ratio = 0.01 (inherited),
///   incremental           = false,
///   epsilon               = CtcMohc::default_epsilon = 0.1,
///   univ_newton_min_width = CtcMohc::default_univ_newton_min_width = 1e-8,
///   tau_mohc              = CtcMohc::ADAPTIVE = -1.0 (the amohc auto-tuner).
///
/// Each per-constraint CtcMohcRevise starts with the HC4Revise pair (natural
/// forward evaluation + backward projection) and then, for constraints with
/// multiple occurrences of a variable, runs the Occurrence-Grouping monotone
/// MinRevise/MaxRevise and the certified MonotonicBoxNarrow — so every revise
/// result is contained in the HC4Revise result on the same constraint, and
/// the extra narrowing recovers width that plain interval evaluation loses to
/// the dependency problem. This is a COMPLETENESS lever (its absence can only
/// mean a missed refutation — asserts φ^δ T-satisfiable on a T-unsatisfiable
/// φ), never a soundness change. It is composable with --acid/--3bcid:
/// shaving and monotone revise are different mechanisms.
///
/// Thread-safety (why the Mt sibling exists): CtcMohcRevise keeps mutable
/// per-call state (active_mono_proc, the ApplyFmin/ApplyFmax arrays, LB/RB,
/// zmin/zmax, a saved box, and the Function_OG grouping scratch), and
/// CtcMohc::contract rewrites every revise cell's active_mono_proc on each
/// call — never share one cell across threads; ContractorIbexMohcMt builds
/// one ContractorIbexMohc per worker (PerThread).
///
/// Accepted benign race: the amohc counters CtcMohc::nb_calls /
/// CtcMohc::nb_interesting are STATIC process-globals (ibex_CtcMohc.h),
/// incremented unsynchronized by every worker's cells under --jobs>1. They
/// only steer the adaptive tau_mohc choice (0.5 vs 0.9999) — a
/// contraction-strength heuristic, COMPLETENESS-only, never a verdict — so
/// the race is documented and accepted rather than locked.
///
/// Prune runs real gaol interval arithmetic (HC4Revise + monotone evaluation
/// + certified univariate Newton), so the FE_UPWARD rounding mode is
/// load-bearing — a wrong ambient mode is a silent false `unsat` (SOUNDNESS:
/// asserts φ T-unsatisfiable on a T-satisfiable φ).
class ContractorIbexMohc : public ContractorCell {
 public:
  /// Constructs a Mohc contractor over @p formulas and @p box.
  ContractorIbexMohc(std::vector<Formula> formulas, const Box& box,
                     const Config& config);

  /// Deleted copy constructor.
  ContractorIbexMohc(const ContractorIbexMohc&) = delete;

  /// Deleted move constructor.
  ContractorIbexMohc(ContractorIbexMohc&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexMohc& operator=(const ContractorIbexMohc&) = delete;

  /// Deleted move assign operator.
  ContractorIbexMohc& operator=(ContractorIbexMohc&&) = delete;

  /// Default destructor.
  ~ContractorIbexMohc() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor (no constraints).
  bool is_dummy() const;

 private:
  const std::vector<Formula> formulas_;
  bool is_dummy_{false};

  IbexConverter ibex_converter_;
  std::unique_ptr<ibex::SystemFactory> system_factory_;
  std::unique_ptr<ibex::System> system_;
  // CtcMohc's revise cells reference system_->ctrs, so system_ (declared
  // above) outlives ctc_ (destroyed first, in reverse declaration order).
  std::unique_ptr<ibex::CtcMohc> ctc_;
  std::vector<std::unique_ptr<const ibex::ExprCtr, ExprCtrDeleter>> expr_ctrs_;
};

}  // namespace dreal
