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
#include "dreal/solver/theory_solver.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

#include <nlohmann/json.hpp>

#include "dreal/version.h"
#include "dreal/contractor/contractor_forall.h"
#include "dreal/contractor/odes/contractor_odes.h"
#include "dreal/solver/context.h"
#include "dreal/solver/filter_assertion.h"
#include "dreal/solver/formula_evaluator.h"
#include "dreal/solver/icp_parallel.h"
#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"
#include "odes/ode_formula_evaluator.h"

namespace dreal {

using std::cout;
using std::make_unique;
using std::numeric_limits;
using std::vector;

TheorySolver::TheorySolver(const Config& config)
    // The one ICP loop (icp_parity_gaps.md R4 consolidation; IcpSeq deleted
    // after the R3 gate measured jobs-1-through-IcpParallel at PAR2 1.001x
    // with zero verdict diffs). --jobs 1 spawns zero pool workers — the
    // calling thread runs the single Worker, deterministically.
    : config_{config}, icp_{make_unique<IcpParallel>(config)} {}

namespace {
bool DefaultTerminationCondition(const Box::IntervalVector& old_iv,
                                 const Box::IntervalVector& new_iv,
                                 const UpwardRounding& ur) {
  DREAL_ASSERT(!new_iv.is_empty());
  constexpr double kThreshold{DREAL_EXPERIMENTAL_THEORY_FIXEDPT_THRESHOLD};
  // If there is a dimension which is improved more than
  // threshold, we continue the current fixed-point computation
  // (return false).
  for (int i{0}; i < old_iv.size(); ++i) {
    const double new_i{safe_diam(new_iv[i], ur)};
    // If the width of new interval is +oo, it has no improvement
    if (new_i == numeric_limits<double>::infinity()) {
      continue;
    }
    // If the i-th dimension was already a point, nothing to improve.
    if (old_iv[i].is_degenerated()) {
      continue;
    }
    const double old_i{safe_diam(old_iv[i], ur)};
    const double improvement{1 - new_i / old_i};
    DREAL_ASSERT(!std::isnan(improvement));
    if (improvement >= kThreshold) {
      return false;
    }
  }
  // If an execution reaches at this point, it means there was no
  // significant improvement. So return true to stop fixed-point
  // computation
  return true;
}

class TheorySolverStat : public Stat {
 public:
  explicit TheorySolverStat(const bool enabled) : Stat{enabled} {}
  TheorySolverStat(const TheorySolverStat&) = delete;
  TheorySolverStat(TheorySolverStat&&) = delete;
  TheorySolverStat& operator=(const TheorySolverStat&) = delete;
  TheorySolverStat& operator=(TheorySolverStat&&) = delete;
  ~TheorySolverStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n", "Total # of CheckSat",
            "Theory level", num_check_sat_);
      print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
            "Total time spent in CheckSat", "Theory level",
            timer_check_sat_.seconds());
    }
  }

  void increase_num_check_sat() { increase(&num_check_sat_); }

  Timer timer_check_sat_;

 private:
  std::atomic<int> num_check_sat_{0};
};

/// Helper class to implement "--dump-theory-literals" option. It
/// collects a list of theory literals for each round and print outs
/// the information (list of list of literals) when an instance of
/// this class is destructed.
class DumpTheoryLiteralsHelper {
 public:
  /// Constructor.
  ///
  /// Its destructor only outputs the information when `enabled_` is true.
  explicit DumpTheoryLiteralsHelper(bool enabled) : enabled_{enabled} {}
  ~DumpTheoryLiteralsHelper() {
    if (enabled_) {
      std::cout << data_ << "\n";
    }
  }

  /// Starts a new round of theory-solving.
  void StartNewRound() { data_.push_back(nlohmann::json{}); }

  /// Adds a literfal `f` to the data.
  void AddLiteral(const Formula& f) { data_.back().push_back(f.to_string()); }

 private:
  bool enabled_{false};
  nlohmann::json data_;
};

}  // namespace

optional<Contractor> TheorySolver::BuildContractor(
    const vector<Formula>& assertions,
    ContractorStatus* const contractor_status) {
  Box& box = contractor_status->mutable_box();
  if (assertions.empty()) {
    return make_contractor_integer(box, config_);
  }
  DREAL_LOG_TRACE("TheorySolver::BuildContractor: Filtering Assertions\n{}",
                  box);
  vector<Contractor> nl_ctcs;
  for (const Formula& f : assertions) {
    if (f.include_ode()) continue; // deal with these next...
    const auto result = FilterAssertion(f, &box);
    if (!result.filtered && !result.changed) {
      DREAL_LOG_TRACE("TheorySolver::BuildContractor: {} - Not Filtered.", f);
    } else if (!result.filtered && result.changed) {
      contractor_status->AddUsedConstraint(f);
    } else if (result.filtered && result.changed) {
      DREAL_LOG_TRACE(
        "TheorySolver::BuildContractor: {} - Filtered with Change.\n{}", f,
        box);
      contractor_status->AddUsedConstraint(f);
      if (box.empty()) {
        for (const auto& v : f.GetFreeVariables()) {
          contractor_status->AddUnsatWitness(v);
        }
        DREAL_LOG_TRACE(
          "TheorySolver::BuildContractor: {} - Filtered with Change => "
          "EMPTY BOX",
          f);
        return {};
      }
      continue;
    } else if (result.filtered && !result.changed) {
      continue;
    }
    auto it = contractor_cache_.find(f);
    if (it == contractor_cache_.end()) {
      // There is no contractor for `f`, build one.
      DREAL_LOG_TRACE(
          "TheorySolver::BuildContractor: Turn {} into a contractor", f);
      if (is_forall(f)) {
        // We should have `inner_delta < epsilon < delta`.
        const double delta{config_.precision()};
        const double epsilon{delta * 0.5};
        const double inner_delta{epsilon * 0.5};
        DREAL_ASSERT(inner_delta < epsilon && epsilon < delta);
        const Contractor ctc{make_contractor_forall<Context>(
            f, box, epsilon, inner_delta, config_)};
        std::vector<Contractor> forall_ctcs;
        if (config_.use_forall_pre_prune()) {
          // A cheap, sound proj-intersection skim (ibex::CtcForAll) ahead of the
          // expensive δ-complete CEGIS decider in the same fixpoint. Pure
          // contraction (no nested δ-solve); COMPLETENESS-only, never a false
          // unsat. See exists_forall_perf.md / ibex_docs/AUDIT-QUANTIFIERS.md.
          forall_ctcs.push_back(make_contractor_ibex_forall(f, box, config_));
        }
        forall_ctcs.push_back(ctc);
        nl_ctcs.emplace_back(make_contractor_fixpoint(
            DefaultTerminationCondition, forall_ctcs, config_));
      } else {
        nl_ctcs.emplace_back(make_contractor_ibex_fwdbwd(f, box, config_));
      }
      // Add it to the cache.
      contractor_cache_.emplace_hint(it, f, nl_ctcs.back());
    } else {
      // Cache hit!
      nl_ctcs.emplace_back(it->second);
    }
  }
  // Optional constraint ordering: reorder the per-constraint contractors by
  // their variable count before assembling the fixpoint. Sound — a fixpoint's
  // result is order-independent, only the number of passes/evals changes. A
  // "reorder, no extra evals" perf lever (a completeness/search-order lever).
  // Applied to the per-formula contractors only (integer/polytope/acid are
  // appended after, keeping their fixed positions).
  if (config_.constraint_order() != ConstraintOrder::kNone) {
    const bool asc = config_.constraint_order() == ConstraintOrder::kAsc;
    std::stable_sort(nl_ctcs.begin(), nl_ctcs.end(),
                     [asc](const Contractor& a, const Contractor& b) {
                       const auto ca = a.input().count();
                       const auto cb = b.input().count();
                       return asc ? (ca < cb) : (ca > cb);
                     });
  }

  // Add integer contractor.
  nl_ctcs.push_back(make_contractor_integer(box, config_));

  if (config_.use_polytope()) {
    // Add polytope contractor.
    nl_ctcs.push_back(make_contractor_ibex_polytope(assertions, box, config_));
  }

  if (config_.use_acid() || config_.use_3bcid()) {
    // Add ACID/3BCID shaving contractor over the assertion system. A
    // COMPLETENESS lever (stronger contraction = fewer search nodes); it is
    // IBEX's own default (HC4 then ACID) which dReal otherwise omits.
    nl_ctcs.push_back(make_contractor_ibex_acid(assertions, box, config_));
  }

  if (config_.use_newton()) {
    // Add the interval-Newton contractor over the square equality subsystem
    // of the assertions (the cell filters to positive equality atoms itself
    // and degenerates to ID when the subsystem is not square). A COMPLETENESS
    // lever (its absence can only mean a missed refutation — asserts φ^δ
    // T-satisfiable on a T-unsatisfiable φ), never a soundness change.
    nl_ctcs.push_back(make_contractor_ibex_newton(assertions, box, config_));
  }

  if (config_.use_obbt()) {
    // Add the OBBT contractor: 2n certified LPs (min/max each variable) over
    // one X-Taylor linearization of the assertion system, intersecting only
    // Neumaier-Shcherbina-certified bounds. A COMPLETENESS lever (stronger
    // contraction = fewer search nodes); uncertified LP results are never
    // trusted for contraction, so LP round-off can never cause a false
    // `unsat` (SOUNDNESS).
    nl_ctcs.push_back(make_contractor_ibex_obbt(assertions, box, config_));
  }

  if (config_.use_mohc()) {
    // Add the Mohc monotonicity-aware propagation contractor over the
    // assertion system (Araya et al., AAAI'10): occurrence-grouping monotone
    // revise + monotone BoxNarrow recover width that plain HC4/ACID interval
    // evaluation loses to the dependency problem on multi-occurrence
    // constraints. A COMPLETENESS lever (its absence can only mean a missed
    // refutation — asserts φ^δ T-satisfiable on a T-unsatisfiable φ), never
    // a soundness change. Composable with --acid/--3bcid — shaving and
    // monotone revise are different mechanisms.
    nl_ctcs.push_back(make_contractor_ibex_mohc(assertions, box, config_));
  }


  // ODEs
  vector<Contractor> ode_fwd_ctcs;
  vector<Contractor> ode_bwd_ctcs;
  const auto ode_constraints = link_integral_invariants(assertions);
  for (const auto & ode_constraint : ode_constraints) {
    const auto f = ode_constraint.first && make_conjunction(ode_constraint.second);
    {
      auto &cache = fwd_ode_contractor_cache_;
      auto dir = ode_direction::FWD;
      auto &ctcs = ode_fwd_ctcs;

      auto it = cache.find(f);
      if (it == cache.end()) {
        // There is no contractor for `f`, build one.
        DREAL_LOG_TRACE("TheorySolver::BuildContractor: Turn {} into a ode fwd contractor", f);
        ctcs.emplace_back(mk_contractor_ode_lohner(box, ode_constraint, dir, config_, 0.0));
        cache.emplace_hint(it, f, ctcs.back());
      } else {
        ctcs.emplace_back(it->second);
      }
    }

    // --ode-backward=false skips the backward (X_0-narrowing) contractor
    // entirely; ode_bwd_ctcs stays empty so the downstream insertion is a no-op.
    if (config_.ode_backward()) {
      auto &cache = bwd_ode_contractor_cache_;
      auto dir = ode_direction::BWD;
      auto &ctcs = ode_bwd_ctcs;

      auto it = cache.find(f);
      if (it == cache.end()) {
        // There is no contractor for `f`, build one.
        DREAL_LOG_TRACE("TheorySolver::BuildContractor: Turn {} into a ode bwd contractor", f);
        ctcs.emplace_back(mk_contractor_ode_lohner(box, ode_constraint, dir, config_, 0.0));
        cache.emplace_hint(it, f, ctcs.back());
      } else {
        ctcs.emplace_back(it->second);
      }
    }
  }

  vector<Contractor> ctcs;
  ctcs.insert(ctcs.end(), nl_ctcs.begin(), nl_ctcs.end());
  for (auto const & ode_ctc : ode_fwd_ctcs) {
    ctcs.insert(ctcs.end(), ode_ctc);
    ctcs.insert(ctcs.end(), nl_ctcs.begin(), nl_ctcs.end());
  }
  for (auto const & ode_ctc : ode_bwd_ctcs) {
    ctcs.insert(ctcs.end(), ode_ctc);
    ctcs.insert(ctcs.end(), nl_ctcs.begin(), nl_ctcs.end());
  }

  if (DREAL_LOG_TRACE_ENABLED) {
    for (const auto& ctc : ctcs) {
      DREAL_LOG_TRACE("TheorySolver::BuildContractor: CTC = {}", fmt::streamed(ctc));
    }
    if (ctcs.empty()) {
      DREAL_LOG_TRACE("TheorySolver::BuildContractor: CTCS = empty");
    }
  }
  if (config_.use_worklist_fixpoint()) {
    return make_contractor_worklist_fixpoint(DefaultTerminationCondition, ctcs, config_);
  } else {
    return make_contractor_fixpoint(DefaultTerminationCondition, ctcs, config_);
  }
}

vector<FormulaEvaluator> TheorySolver::BuildFormulaEvaluator(
    const vector<Formula>& assertions) {
  vector<FormulaEvaluator> formula_evaluators;
  formula_evaluators.reserve(assertions.size());
  // We should have `inner_delta < epsilon < delta`.
  const double delta{config_.precision()};
  const double epsilon{0.99 * delta};
  const double inner_delta{0.99 * epsilon};
  DREAL_ASSERT(inner_delta < epsilon && epsilon < delta);
  for (const Formula& f : assertions) {
    auto it = formula_evaluator_cache_.find(f);
    if (it == formula_evaluator_cache_.end()) {
      DREAL_LOG_DEBUG("TheorySolver::BuildFormulaEvaluator: {}", f);
      if (is_forall(f)) {
        formula_evaluators.push_back(make_forall_formula_evaluator(
            f, epsilon, inner_delta, config_.number_of_jobs()));
      } else if (f.include_ode()) {
        formula_evaluators.push_back(
            make_ode_formula_evaluator(f, config_.refine_witness()));
      } else {
        formula_evaluators.push_back(make_relational_formula_evaluator(f));
      }
      formula_evaluator_cache_.emplace_hint(it, f, formula_evaluators.back());
    } else {
      formula_evaluators.push_back(it->second);
    }
  }
  return formula_evaluators;
}

bool TheorySolver::CheckSat(const Box& box, const vector<Formula>& assertions) {
  static TheorySolverStat stat{DREAL_LOG_INFO_ENABLED};
  static DumpTheoryLiteralsHelper dump_theory_literals{
      config_.dump_theory_literals()};
  stat.increase_num_check_sat();
  TimerGuard check_sat_timer_guard(&stat.timer_check_sat_, stat.enabled(),
                                   true /* start_timer */);

  if (config_.dump_theory_literals()) {
    dump_theory_literals.StartNewRound();
    for (const auto& f : assertions) {
      dump_theory_literals.AddLiteral(f);
    }
  }

  DREAL_LOG_DEBUG("TheorySolver::CheckSat()");
  ContractorStatus contractor_status(box);

  // Icp Step
  const optional<Contractor> contractor{
      BuildContractor(assertions, &contractor_status)};
  if (contractor) {
    icp_->CheckSat(*contractor, BuildFormulaEvaluator(assertions),
                   &contractor_status);
    if (contractor_status.box().empty()) {
      explanation_ = contractor_status.Explanation();
      return false;
    } else {
      model_ = contractor_status.box();
      return true;
    }
    return !contractor_status.box().empty();
  } else {
    DREAL_ASSERT(contractor_status.box().empty());
    explanation_ = contractor_status.Explanation();
    return false;
  }
}

const Box& TheorySolver::GetModel() const {
  DREAL_LOG_DEBUG("TheorySolver::GetModel():\n{}", model_);
  return model_;
}

const FormulaSet& TheorySolver::GetExplanation() const {
  return explanation_;
}

}  // namespace dreal
