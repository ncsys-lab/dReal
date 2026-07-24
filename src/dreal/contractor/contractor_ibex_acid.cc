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
#include "dreal/contractor/contractor_ibex_acid.h"

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
// Gated stat block (mirrors contractor_ibex_polytope.cc). The timer/counter
// work only happens when logging is enabled, so the default (off) path pays
// nothing — the stat-overhead lesson from OPTIMIZATION_LOG.md §odeexpr.
class ContractorIbexAcidStat : public Stat {
 public:
  explicit ContractorIbexAcidStat(const bool enabled) : Stat{enabled} {};
  ContractorIbexAcidStat(const ContractorIbexAcidStat&) = delete;
  ContractorIbexAcidStat(ContractorIbexAcidStat&&) = delete;
  ContractorIbexAcidStat& operator=(const ContractorIbexAcidStat&) = delete;
  ContractorIbexAcidStat& operator=(ContractorIbexAcidStat&&) = delete;
  ~ContractorIbexAcidStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-acid Pruning", "Pruning level", num_pruning_);
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-acid Pruning (zero-effect)", "Pruning level",
            num_zero_effect_pruning_);
      if (num_pruning_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Pruning (acid)", "Pruning level",
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
// Implementation of ContractorIbexAcid
//---------------------------------------
ContractorIbexAcid::ContractorIbexAcid(vector<Formula> formulas, const Box& box,
                                       const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_ACID, DynamicBitset(box.size()),
                     config},
      formulas_{FilterIbexConvertible(std::move(formulas))},
      ibex_converter_{box} {
  DREAL_LOG_DEBUG("ContractorIbexAcid::ContractorIbexAcid");

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

  // Build the shaving contractor over an HC4 sub-contractor. The sub-contractor
  // (a stock CtcHC4 over the same system) is owned here and must outlive ctc_,
  // which holds a reference to it. CtcAcid additionally takes the system to
  // order shaved variables by the smearsumrel criterion.
  hc4_sub_ = make_unique<ibex::CtcHC4>(*system_);
  if (config.use_acid()) {
    ctc_ = make_unique<ibex::CtcAcid>(
        *system_, *hc4_sub_, /*optim=*/false, config.acid_s3b(),
        ibex::Ctc3BCid::default_scid, ibex::Ctc3BCid::default_var_min_width,
        config.acid_ct_ratio());
  } else {
    ctc_ = make_unique<ibex::Ctc3BCid>(*hc4_sub_, config.acid_s3b());
  }

  // Build input bitset (free vars across formulas_).
  DynamicBitset& input{mutable_input()};
  for (const Formula& f : formulas_) {
    for (const Variable& var : f.GetFreeVariables()) {
      input.set(box.index(var));
    }
  }
}

void ContractorIbexAcid::Prune(ContractorStatus* cs,
                               const UpwardRounding& ur) const {
  thread_local ContractorIbexAcidStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_ASSERT(!is_dummy_ && ctc_);

  // CtcAcid/CtcHC4 run gaol interval arithmetic (HC4Revise + 3B shaving), sound
  // only under FE_UPWARD. The mode is established once per ICP phase by the
  // caller's UpwardRoundingScope and witnessed by `ur` (no per-call
  // fesetround). This Prune genuinely depends on the mode — a wrong ambient
  // mode here would be a false `unsat`.
  (void)ur;
  DREAL_ASSERT_ROUNDING(FE_UPWARD);

  Box::IntervalVector& iv{cs->mutable_box().mutable_interval_vector()};
  DREAL_LOG_TRACE("ContractorIbexAcid::Prune");

  // Input-restricted snapshot of the pre-contraction intervals (mirrors the
  // polytope contractor): only the constraint free-var indices, so the
  // changed-bit update below touches just the relevant dimensions. ACID only
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

ostream& ContractorIbexAcid::display(ostream& os) const {
  os << "IbexAcid(";
  for (const Formula& f : formulas_) {
    os << f << ";";
  }
  os << ")";
  return os;
}

bool ContractorIbexAcid::is_dummy() const { return is_dummy_; }

}  // namespace dreal
