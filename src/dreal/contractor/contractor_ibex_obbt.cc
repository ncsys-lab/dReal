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
#include "dreal/contractor/contractor_ibex_obbt.h"

#include <sstream>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"

using std::cout;
using std::make_unique;
using std::ostream;
using std::ostringstream;
using std::unique_ptr;
using std::vector;

namespace dreal {

namespace {
// Gated stat block (mirrors contractor_ibex_polytope.cc). The timer/counter
// work only happens when logging is enabled, so the default (off) path pays
// nothing.
class ContractorIbexObbtStat : public Stat {
 public:
  explicit ContractorIbexObbtStat(const bool enabled) : Stat{enabled} {};
  ContractorIbexObbtStat(const ContractorIbexObbtStat&) = delete;
  ContractorIbexObbtStat(ContractorIbexObbtStat&&) = delete;
  ContractorIbexObbtStat& operator=(const ContractorIbexObbtStat&) = delete;
  ContractorIbexObbtStat& operator=(ContractorIbexObbtStat&&) = delete;
  ~ContractorIbexObbtStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-obbt Pruning", "Pruning level", num_pruning_);
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-obbt Pruning (zero-effect)", "Pruning level",
            num_zero_effect_pruning_);
      if (num_pruning_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Pruning (obbt)", "Pruning level",
              timer_pruning_.seconds());
      }
    }
  }

  int num_zero_effect_pruning_{0};
  int num_pruning_{0};

  Timer timer_pruning_;
};
}  // namespace

//---------------------------------------
// Implementation of ContractorIbexObbt
//---------------------------------------
ContractorIbexObbt::ContractorIbexObbt(vector<Formula> formulas, const Box& box,
                                       const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_OBBT, DynamicBitset(box.size()),
                     config},
      formulas_{FilterIbexConvertible(std::move(formulas))},
      ibex_converter_{box} {
  DREAL_LOG_DEBUG("ContractorIbexObbt::ContractorIbexObbt");

  // Build SystemFactory: all box variables, then the pre-filtered (non-forall,
  // non-ODE) constraints. Identical to ContractorIbexPolytope's assembly.
  system_factory_ = make_unique<ibex::SystemFactory>();
  system_factory_->add_var(ibex_converter_.variables());
  for (const Formula& f : formulas_) {
    unique_ptr<const ibex::ExprCtr, ExprCtrDeleter> expr_ctr{
        ibex_converter_.Convert(f)};
    if (expr_ctr) {
      system_factory_->add_ctr(*expr_ctr);
      // Postpone destruction of expr_ctr; still used inside system_factory_.
      expr_ctrs_.push_back(std::move(expr_ctr));
    }
  }
  ibex_converter_.set_need_to_delete_variables(true);

  // Build System.
  system_ = make_unique<ibex::System>(*system_factory_);
  if (system_->nb_ctr == 0) {
    is_dummy_ = true;
    return;
  }

  // X-Taylor relaxation, same policy triple as ContractorIbexPolytope:
  // RELAX (outer linearization: f(x)<=0 => Ax<=b), RANDOM_OPP corners,
  // HANSEN slopes.
  linear_relax_ = make_unique<ibex::LinearizerXTaylor>(
      *system_, ibex::LinearizerXTaylor::RELAX,
      ibex::LinearizerXTaylor::RANDOM_OPP, ibex::LinearizerXTaylor::HANSEN);

  // The LP solver, sized to the system and — critically — in Mode::Certified.
  // The ctor being used (ibex_LPSolver.h):
  //   LPSolver(int nb_vars, LPSolver::Mode mode=LPSolver::Mode::NotCertified,
  //            double tolerance=default_tolerance,
  //            double timeout=default_timeout, int max_iter=default_max_iter);
  // Under the default Mode::NotCertified the soplex wrapper NEVER produces
  // OptimalProved/InfeasibleProved (the Neumaier-Shcherbina postprocessing in
  // LPSolver::minimize runs only when mode_ == Mode::Certified,
  // lp_lib_wrapper/soplex/ibex_LPLibWrapper.cpp), so Prune's certification
  // guard would reject every result and this contractor would be a silent
  // no-op. The tolerance/timeout/max_iter defaults are exactly what
  // ibex::CtcPolytopeHull passes to its own internal Certified LPSolver
  // (ibex_CtcPolytopeHull.h ctor defaults).
  lp_ = make_unique<ibex::LPSolver>(system_->nb_var,
                                    ibex::LPSolver::Mode::Certified);

  // Build input bitset (free vars across formulas_).
  DynamicBitset& input{mutable_input()};
  for (const Formula& f : formulas_) {
    for (const Variable& var : f.GetFreeVariables()) {
      input.set(box.index(var));
    }
  }
}

void ContractorIbexObbt::Prune(ContractorStatus* cs,
                               const UpwardRounding& ur) const {
  thread_local ContractorIbexObbtStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_ASSERT(!is_dummy_ && lp_ && linear_relax_);

  // The X-Taylor cut coefficients (interval-gradient endpoints, interval rhs)
  // and the Neumaier-Shcherbina certificates (the interval product Aᵀy in
  // neumaier_shcherbina_postprocessing and _infeasibility_test, rigorous since
  // the fork's BUG-019 fix, docs/dreal-bugs.md) run gaol interval arithmetic, sound
  // only under FE_UPWARD. The mode is established once per ICP phase by the
  // caller's UpwardRoundingScope and witnessed by `ur` (no per-call
  // fesetround); a wrong ambient mode here would be a false `unsat`
  // (SOUNDNESS — asserts phi T-unsatisfiable on a T-satisfiable phi).
  DREAL_ASSERT_ROUNDING(FE_UPWARD);

  Box::IntervalVector& iv{cs->mutable_box().mutable_interval_vector()};
  DREAL_LOG_TRACE("ContractorIbexObbt::Prune");

  // Guard (pure identity — no contraction): an unbounded box has no corner
  // point for the X-Taylor linearization and degenerates the
  // Neumaier-Shcherbina bound products to +/-oo. Mirrors
  // ibex::CtcPolytopeHull::contract's own `if (box.is_unbounded()) return;`.
  if (iv.is_unbounded()) {
    DREAL_LOG_TRACE("NO CHANGE (unbounded box)");
    return;
  }

  // Input-restricted snapshot of the pre-contraction intervals (mirrors the
  // polytope contractor). Skipping the NON-input dims is exact, not an
  // approximation: a variable absent from every cut meets the LP only through
  // its own bound row, so its LP min/max IS its current bound. input() is
  // exactly the system's constraint vars — the ctor builds it over formulas_,
  // pre-filtered by FilterIbexConvertible to the same (non-forall, non-ODE)
  // set the system assembly converted (ibex::CtcPolytopeHull's own 2n loop
  // is broader: it visits every box variable).
  thread_local std::vector<std::pair<int, ibex::Interval>> saved_inputs;
  saved_inputs.clear();
  {
    DynamicBitset::size_type i_bit = input().find_first();
    while (i_bit != DynamicBitset::npos) {
      saved_inputs.emplace_back(static_cast<int>(i_bit), iv[i_bit]);
      i_bit = input().find_next(i_bit);
    }
  }

  if (stat.enabled()) stat.timer_pruning_.resume();

  // Fresh LP per Prune: drop the previous box's cuts and any cost left by an
  // early exit. Bound rows persist but are fully overwritten by set_bounds
  // below before any minimize.
  lp_->clear_constraints();
  lp_->set_cost_to_zero();

  // Linearize ONCE per Prune. All 2n LPs below run against this single cut
  // set — between them only the cost vector and the fed-back variable bounds
  // change. This is exactly how IBEX's own CtcPolytopeHull drives the same
  // Linearizer API (one lr.linearize per contract, then the 2n-LP optimizer
  // loop, ibex_CtcPolytopeHull.cpp); nothing in the Linearizer contract
  // requires re-linearizing when only the objective changes.
  const int num_cuts{linear_relax_->linearize(iv, *lp_)};
  if (num_cuts == 0) {
    // Guard (pure identity): every constraint is inactive over this box (or
    // every generated cut is satisfied box-wide), so the polytope is the box
    // itself and no LP can tighten it.
    if (stat.enabled()) stat.timer_pruning_.pause();
    DREAL_LOG_TRACE("NO CHANGE (no cuts)");
    return;
  }
  if (num_cuts < 0) {
    // linearize == -1: interval arithmetic proved one RELAX cut a*x <= b
    // violated over the whole box (Unsatisfiability in
    // ibex_LinearizerXTaylor.cpp check_and_add_constraint). In RELAX mode the
    // cut is implied by the atoms — f(x)<=0 => Ax<=b — so no point of the box
    // satisfies the atoms and emptying is sound, never a false `unsat`
    // (SOUNDNESS). ibex::CtcPolytopeHull maps -1 to an empty box the same way.
    iv.set_empty();
  } else {
    // Load the current box as the LP variable bounds (also encoded as the
    // first nb_vars rows — see ibex_LPSolver.h class doc).
    lp_->set_bounds(iv);

    // One uncertified LP status stops the whole pass (see the guard below).
    bool stop_lp_pass{false};
    for (const auto& idx_saved : saved_inputs) {
      const int idx{idx_saved.first};
      // Guard (pure identity): a certified enclosure cannot meaningfully
      // improve a bound already within the LP's own primal/dual tolerance;
      // skipping such dims saves 2 LPs each and never widens anything.
      if (safe_diam(iv[idx], ur) <= lp_->tolerance()) {
        continue;
      }
      // sense = +1: minimize +x_idx — tighten the LOWER bound.
      // sense = -1: minimize -x_idx — tighten the UPPER bound
      //             (max x = -min(-x)).
      for (const double sense : {1.0, -1.0}) {
        lp_->set_cost(idx, sense);
        const ibex::LPSolver::Status status{lp_->minimize()};
        lp_->set_cost(idx, 0.0);  // reset for the next LP (only cost changes)

        if (status == ibex::LPSolver::Status::InfeasibleProved) {
          // Neumaier-Shcherbina certified the RELAXATION infeasible over the
          // current bounds. Relaxation ⊇ {x in box : atoms hold} (RELAX mode),
          // so the atoms are unsatisfiable over the box and emptying is sound,
          // never a false `unsat` (SOUNDNESS).
          iv.set_empty();
          break;
        }
        if (status != ibex::LPSolver::Status::OptimalProved) {
          // Guard (pure identity for this and every remaining bound): no
          // certificate — Optimal / Infeasible (uncertified), Unbounded,
          // Timeout, MaxIter, Unknown. Uncertified floating-point LP values
          // must NOT be trusted for contraction: an over-tight uncertified
          // bound could cut off a real solution — a false `unsat`
          // (SOUNDNESS). Stop the whole 2n-LP pass rather than skip one LP:
          // the cut set is fixed for the pass, so an uncertified-Infeasible
          // polytope stays infeasible for every remaining cost vector, and a
          // Timeout/MaxIter LP stays just as hard. ibex::CtcPolytopeHull
          // stops the same way — an explicit break on uncertified Infeasible
          // ("the infeasibility is found but not proved, no other call is
          // needed") and a terminal else (commented for MAX_ITER) for the
          // rest; it retries other bounds on Unknown, which we fold into the
          // uniform stop instead. Forgone LPs never widen anything — at worst
          // forgone contraction, i.e. a missed refutation within budget
          // (COMPLETENESS), never a false `unsat`.
          stop_lp_pass = true;
          break;
        }

        // OptimalProved: minimum() is the Neumaier-Shcherbina certified
        // enclosure of the true LP optimum. Sign algebra for the upper bound:
        // min(-x_idx) ∈ m ⇒ max(x_idx) = -min(-x_idx) ∈ -m, so we read the
        // certified sides off opt = ±m uniformly: lower bound = opt.lb()
        // (underestimate of the true min), upper bound = opt.ub()
        // (overestimate of the true max) — mirrors CtcPolytopeHull's
        // `opt = -minimum()` for the sup case.
        const ibex::Interval opt{sense > 0.0 ? lp_->minimum()
                                             : -lp_->minimum()};
        if (sense > 0.0) {
          // Inclusion argument (lower bound): {x in box : atoms} ⊆ relaxation
          // polytope, and true_min = min over the relaxation, so every real
          // solution has x_idx >= true_min >= opt.lb() (certified
          // underestimate). Intersecting with [opt.lb(), +oo) discards only
          // non-solutions — sound; intersect-only, never widen.
          const double new_lb{opt.lb()};
          if (new_lb > iv[idx].ub()) {
            // The certified lower bound clears the whole interval: every real
            // solution needs x_idx >= new_lb > ub, yet the box caps x_idx at
            // ub — no solution in the box, empty is sound (SOUNDNESS-safe).
            iv.set_empty();
            break;
          }
          if (new_lb > iv[idx].lb()) {
            iv[idx] = Box::Interval(new_lb, iv[idx].ub());
            // Feed the tightened bound back so the remaining LPs run over the
            // smaller polytope (mirrors CtcPolytopeHull's optimizer loop).
            lp_->set_bounds(idx, iv[idx]);
          }
        } else {
          // Inclusion argument (upper bound), symmetric: every real solution
          // has x_idx <= true_max <= opt.ub() (certified overestimate).
          const double new_ub{opt.ub()};
          if (new_ub < iv[idx].lb()) {
            iv.set_empty();
            break;
          }
          if (new_ub < iv[idx].ub()) {
            iv[idx] = Box::Interval(iv[idx].lb(), new_ub);
            lp_->set_bounds(idx, iv[idx]);
          }
        }
      }
      if (stop_lp_pass || iv.is_empty()) {
        break;
      }
    }
  }

  if (stat.enabled()) stat.timer_pruning_.pause();
  if (stat.enabled()) {
    stat.num_pruning_++;
  }

  bool changed{false};
  // Update output (mirrors contractor_ibex_polytope.cc).
  if (iv.is_empty()) {
    changed = true;
    cs->mutable_output().set();
  } else {
    for (const auto& [idx, saved] : saved_inputs) {
      if (iv[idx] != saved) {
        cs->mutable_output().set(static_cast<DynamicBitset::size_type>(idx));
        changed = true;
      }
    }
  }
  // Update used constraints.
  if (changed) {
    cs->AddUsedConstraint(formulas_);
    if (DREAL_LOG_TRACE_ENABLED) {
      // Reconstruct old_iv only when tracing — the input-restricted snapshot
      // is enough for the changed-bit update above.
      Box::IntervalVector old_iv = iv;
      for (const auto& [idx, saved] : saved_inputs) {
        old_iv[idx] = saved;
      }
      ostringstream oss;
      DisplayDiff(oss, cs->box().variables(), old_iv, iv);
      DREAL_LOG_TRACE("Changed\n{}", oss.str());
    }
  } else {
    if (stat.enabled()) {
      stat.num_zero_effect_pruning_++;
    }
    DREAL_LOG_TRACE("NO CHANGE");
  }
}

ostream& ContractorIbexObbt::display(ostream& os) const {
  os << "IbexObbt(";
  for (const Formula& f : formulas_) {
    os << f << ";";
  }
  os << ")";
  return os;
}

bool ContractorIbexObbt::is_dummy() const { return is_dummy_; }

}  // namespace dreal
