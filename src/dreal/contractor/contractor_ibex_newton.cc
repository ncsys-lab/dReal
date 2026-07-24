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
#include "dreal/contractor/contractor_ibex_newton.h"

#include <sstream>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
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
// Gated stat block (mirrors contractor_ibex_acid.cc). The timer/counter
// work only happens when logging is enabled, so the default (off) path pays
// nothing — the stat-overhead lesson from OPTIMIZATION_LOG.md §odeexpr.
class ContractorIbexNewtonStat : public Stat {
 public:
  explicit ContractorIbexNewtonStat(const bool enabled) : Stat{enabled} {};
  ContractorIbexNewtonStat(const ContractorIbexNewtonStat&) = delete;
  ContractorIbexNewtonStat(ContractorIbexNewtonStat&&) = delete;
  ContractorIbexNewtonStat& operator=(const ContractorIbexNewtonStat&) = delete;
  ContractorIbexNewtonStat& operator=(ContractorIbexNewtonStat&&) = delete;
  ~ContractorIbexNewtonStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-newton Pruning", "Pruning level", num_pruning_);
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-newton Pruning (zero-effect)", "Pruning level",
            num_zero_effect_pruning_);
      if (num_pruning_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Pruning (newton)", "Pruning level",
              timer_pruning_.seconds());
      }
    }
  }

  int num_zero_effect_pruning_{0};
  int num_pruning_{0};

  Timer timer_pruning_;
};

// Keeps only the positive equality atoms of @p formulas. Interval Newton is
// defined for square systems f(x) = 0, so inequalities are out of scope;
// ∃∀ `forall`s and ODE atoms cannot be converted by IbexConverter (mirrors
// FilterIbexConvertible in contractor_ibex_polytope.cc — for those kinds
// is_equal_to is already false, so the extra checks are belt-and-braces).
// This is a pure filter (never a contraction): skipping a formula only
// weakens this optional cell, never a verdict.
vector<Formula> FilterEqualities(vector<Formula> formulas) {
  vector<Formula> equalities;
  for (Formula& f : formulas) {
    if (is_equal_to(f) && !is_forall(f) && !f.include_ode()) {
      equalities.push_back(std::move(f));
    }
  }
  return equalities;
}
}  // namespace

//---------------------------------------
// Implementation of ContractorIbexNewton
//---------------------------------------
ContractorIbexNewton::ContractorIbexNewton(vector<Formula> formulas,
                                           const Box& box,
                                           const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_NEWTON, DynamicBitset(box.size()),
                     config},
      formulas_{FilterEqualities(std::move(formulas))},
      ibex_converter_{box} {
  DREAL_LOG_DEBUG("ContractorIbexNewton::ContractorIbexNewton");

  // Build SystemFactory: all box variables, then the equality constraints.
  // Same assembly as ContractorIbexAcid, over the pre-filtered formulas_.
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

  // Build input = S, the set of variables occurring in the equalities. The
  // system's column indexing matches the Box indexing (add_var above added
  // ibex_converter_.variables() in box order), so these bits double as the
  // f_ctrs column indices below.
  DynamicBitset& input{mutable_input()};
  for (const Formula& f : formulas_) {
    for (const Variable& var : f.GetFreeVariables()) {
      input.set(box.index(var));
    }
  }

  // Square-selection guard: interval Newton needs #equations == #unknowns
  // (ibex::CtcNewton's ctor throws not_implemented on rectangular systems,
  // ibex_CtcNewton.cpp). A non-square subsystem makes this cell a dummy
  // identity — a pure skip guard (no contraction), so its only cost is
  // losing this optional COMPLETENESS lever.
  const int m{system_->nb_ctr};
  if (m == 0 || input.count() != static_cast<DynamicBitset::size_type>(m)) {
    DREAL_LOG_DEBUG(
        "ContractorIbexNewton: non-square subsystem ({} equalities over {} "
        "variables) — dummy.",
        m, input.count());
    is_dummy_ = true;
    return;
  }

  // Coverage dispatch — ibex's own API split (ibex_CtcNewton.cpp has one
  // ctor per case). ceil = --newton-ceil: CtcNewton::contract is an identity
  // unless box.max_diam() <= ceil (avoids useless Jacobian evaluations on
  // wide boxes). prec/ratio stay at the IBEX defaults — default_newton_prec
  // = 1e-07, default_gauss_seidel_ratio = 1e-04 (ibex_Newton.cpp).
  if (input.count() == static_cast<DynamicBitset::size_type>(system_->nb_var)) {
    // S covers every box variable: use the plain all-variables ctor. The
    // parameterized route must NOT be used here — a VarSet with nb_param ==
    // 0 makes ibex's newton() build an m×0 parameter Jacobian whose rows
    // have NULL storage in a Release build (IntervalVector::resize(0)
    // early-returns), and Jp->is_empty() dereferences it (ibex_Newton.cpp).
    // Squareness (f.nb_var() == f.image_dim(), the plain ctor's own
    // requirement) follows from the |S| == m guard above.
    ctc_ = make_unique<ibex::CtcNewton>(system_->f_ctrs, config.newton_ceil());
  } else {
    // Proper subset (0 < |S| < nb_var, so nb_param > 0): restrict Newton to
    // exactly the S columns of f_ctrs via ibex::VarSet — non-S columns are
    // marked parameters (ibex treats them as interval constants). No
    // equality mentions them, so f_ctrs has zero dependence there and the
    // split is exact.
    ibex::BitSet s_columns{ibex::BitSet::empty(system_->nb_var)};
    DynamicBitset::size_type i_bit = input.find_first();
    while (i_bit != DynamicBitset::npos) {
      s_columns.add(static_cast<int>(i_bit));
      i_bit = input.find_next(i_bit);
    }
    var_set_ = make_unique<ibex::VarSet>(system_->nb_var, s_columns);
    ctc_ = make_unique<ibex::CtcNewton>(system_->f_ctrs, *var_set_,
                                        config.newton_ceil());
  }
}

void ContractorIbexNewton::Prune(ContractorStatus* cs,
                                 const UpwardRounding& ur) const {
  thread_local ContractorIbexNewtonStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_ASSERT(!is_dummy_ && ctc_);

  // CtcNewton runs gaol interval arithmetic (Jacobian evaluation +
  // Gauss-Seidel), sound only under FE_UPWARD. The mode is established once
  // per ICP phase by the caller's UpwardRoundingScope and witnessed by `ur`
  // (no per-call fesetround). This Prune genuinely depends on the mode — a
  // wrong ambient mode here would be a false `unsat` (SOUNDNESS: asserts φ
  // T-unsatisfiable on a T-satisfiable φ).
  (void)ur;
  DREAL_ASSERT_ROUNDING(FE_UPWARD);

  Box::IntervalVector& iv{cs->mutable_box().mutable_interval_vector()};
  DREAL_LOG_TRACE("ContractorIbexNewton::Prune");

  // Input-restricted snapshot of the pre-contraction intervals (mirrors the
  // polytope contractor): only the equality free-var indices, so the
  // changed-bit update below touches just the relevant dimensions. Newton
  // only narrows the S columns == input() (the whole box in the all-vars
  // case, where input() is every column anyway).
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
  ctc_->contract(iv);
  if (stat.enabled()) stat.timer_pruning_.pause();
  if (stat.enabled()) {
    stat.num_pruning_++;
  }
  bool changed{false};
  // Update output.
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
      // is enough for the changed-bit update above. DisplayDiff still wants
      // the full pair for human readability.
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

ostream& ContractorIbexNewton::display(ostream& os) const {
  os << "IbexNewton(";
  for (const Formula& f : formulas_) {
    os << f << ";";
  }
  os << ")";
  return os;
}

bool ContractorIbexNewton::is_dummy() const { return is_dummy_; }

}  // namespace dreal
