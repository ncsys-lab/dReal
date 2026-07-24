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
#include "dreal/contractor/contractor_ibex_mohc.h"

#include <sstream>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"

using std::cout;
using std::make_unique;
using std::ostream;
using std::unique_ptr;
using std::vector;

namespace dreal {

namespace {
// Gated stat block (mirrors contractor_ibex_acid.cc). The timer/counter
// work only happens when logging is enabled, so the default (off) path pays
// nothing — the stat-overhead lesson from OPTIMIZATION_LOG.md §odeexpr.
class ContractorIbexMohcStat : public Stat {
 public:
  explicit ContractorIbexMohcStat(const bool enabled) : Stat{enabled} {};
  ContractorIbexMohcStat(const ContractorIbexMohcStat&) = delete;
  ContractorIbexMohcStat(ContractorIbexMohcStat&&) = delete;
  ContractorIbexMohcStat& operator=(const ContractorIbexMohcStat&) = delete;
  ContractorIbexMohcStat& operator=(ContractorIbexMohcStat&&) = delete;
  ~ContractorIbexMohcStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-mohc Pruning", "Pruning level", num_pruning_);
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-mohc Pruning (zero-effect)", "Pruning level",
            num_zero_effect_pruning_);
      if (num_pruning_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Pruning (mohc)", "Pruning level",
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
// Implementation of ContractorIbexMohc
//---------------------------------------
ContractorIbexMohc::ContractorIbexMohc(vector<Formula> formulas, const Box& box,
                                       const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_MOHC, DynamicBitset(box.size()),
                     config},
      formulas_{FilterIbexConvertible(std::move(formulas))},
      ibex_converter_{box} {
  DREAL_LOG_DEBUG("ContractorIbexMohc::ContractorIbexMohc");

  // Build SystemFactory: all box variables, then the pre-filtered (non-forall,
  // non-ODE) constraints. Identical to ContractorIbexAcid's assembly.
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

  // Build the Mohc propagation contractor over the System's NumConstraint
  // array (system_->ctrs, ibex_System.h). All defaults — no dReal knobs this
  // round: ratio = CtcPropag::default_ratio = 0.01, incremental = false,
  // epsilon = CtcMohc::default_epsilon = 0.1, univ_newton_min_width =
  // CtcMohc::default_univ_newton_min_width = 1e-8, tau_mohc =
  // CtcMohc::ADAPTIVE = -1.0 (amohc auto-tuning; racing its static
  // nb_calls/nb_interesting counters across workers is documented-benign —
  // see the class doc in the header).
  ctc_ = make_unique<ibex::CtcMohc>(system_->ctrs);

  // Build input bitset (free vars across formulas_).
  DynamicBitset& input{mutable_input()};
  for (const Formula& f : formulas_) {
    for (const Variable& var : f.GetFreeVariables()) {
      input.set(box.index(var));
    }
  }
}

void ContractorIbexMohc::Prune(ContractorStatus* cs,
                               const UpwardRounding& ur) const {
  thread_local ContractorIbexMohcStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_ASSERT(!is_dummy_ && ctc_);

  // CtcMohc runs gaol interval arithmetic (HC4Revise + occurrence-grouping
  // monotone evaluation + certified univariate Newton), sound only under
  // FE_UPWARD. The mode is established once per ICP phase by the caller's
  // UpwardRoundingScope and witnessed by `ur` (no per-call fesetround). This
  // Prune genuinely depends on the mode — a wrong ambient mode here would be
  // a false `unsat` (SOUNDNESS: asserts φ T-unsatisfiable on a T-satisfiable
  // φ).
  (void)ur;
  DREAL_ASSERT_ROUNDING(FE_UPWARD);

  Box::IntervalVector& iv{cs->mutable_box().mutable_interval_vector()};
  DREAL_LOG_TRACE("ContractorIbexMohc::Prune");

  // Input-restricted snapshot of the pre-contraction intervals (mirrors the
  // acid contractor): only the constraint free-var indices, so the
  // changed-bit update below touches just the relevant dimensions. Mohc only
  // narrows variables that appear in the system's constraints == input().
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
  if (changed) {
    cs->AddUsedConstraint(formulas_);
  } else {
    if (stat.enabled()) {
      stat.num_zero_effect_pruning_++;
    }
    DREAL_LOG_TRACE("NO CHANGE");
  }
}

ostream& ContractorIbexMohc::display(ostream& os) const {
  os << "IbexMohc(";
  for (const Formula& f : formulas_) {
    os << f << ";";
  }
  os << ")";
  return os;
}

bool ContractorIbexMohc::is_dummy() const { return is_dummy_; }

}  // namespace dreal
