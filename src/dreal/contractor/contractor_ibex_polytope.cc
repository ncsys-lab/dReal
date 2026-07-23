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
#include "dreal/contractor/contractor_ibex_polytope.h"

#include <sstream>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/math.h"
#include "dreal/util/rounding.h"
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
// Temporary instrumentation for the non-ODE-slowdown investigation. Mirrors
// the stat block in contractor_ibex_fwdbwd.cc. Drop once profiling is done.
class ContractorIbexPolytopeStat : public Stat {
 public:
  explicit ContractorIbexPolytopeStat(const bool enabled) : Stat{enabled} {};
  ContractorIbexPolytopeStat(const ContractorIbexPolytopeStat&) = delete;
  ContractorIbexPolytopeStat(ContractorIbexPolytopeStat&&) = delete;
  ContractorIbexPolytopeStat& operator=(const ContractorIbexPolytopeStat&) = delete;
  ContractorIbexPolytopeStat& operator=(ContractorIbexPolytopeStat&&) = delete;
  ~ContractorIbexPolytopeStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-polytope Pruning", "Pruning level", num_pruning_);
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of ibex-polytope Pruning (zero-effect)", "Pruning level",
            num_zero_effect_pruning_);
      if (num_pruning_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Pruning (polytope)", "Pruning level",
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
// Implementation of ContractorIbexPolytope
//---------------------------------------
ContractorIbexPolytope::ContractorIbexPolytope(vector<Formula> formulas,
                                               const Box& box,
                                               const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_POLYTOPE,
                     DynamicBitset(box.size()), config},
      formulas_{std::move(formulas)},
      ibex_converter_{box} {
  DREAL_LOG_DEBUG("ContractorIbexPolytope::ContractorIbexPolytope");

  // Build SystemFactory. Add variables and constraints.
  system_factory_ = make_unique<ibex::SystemFactory>();
  system_factory_->add_var(ibex_converter_.variables());
  for (const Formula& f : formulas_) {
    if (!is_forall(f)) {
      unique_ptr<const ibex::ExprCtr, ExprCtrDeleter> expr_ctr{
          ibex_converter_.Convert(f)};
      if (expr_ctr) {
        system_factory_->add_ctr(*expr_ctr);
        // We need to postpone the destruction of expr_ctr as it is
        // still used inside of system_factory_.
        expr_ctrs_.push_back(std::move(expr_ctr));
      }
    }
  }
  ibex_converter_.set_need_to_delete_variables(true);

  // Build System.
  system_ = make_unique<ibex::System>(*system_factory_);
  if (system_->nb_ctr == 0) {
    is_dummy_ = true;
    return;
  }

  // Build the linear relaxation selected by --polytope-linearizer, then the
  // polytope-hull contractor over it.
  //
  // LinearizerAffine2::linearize returns the emitted cut count, or -1 when
  // the affine evaluation alone proves the box infeasible (the ev-sign
  // `return -1` sites, fork ibex_LinearizerAffine2.cpp); LinearizerCompo
  // propagates a -1 from either side. CtcPolytopeHull::contract maps -1 to
  // box.set_empty() (fork src/contractor/ibex_CtcPolytopeHull.cpp:69->81),
  // so the -1 fast path surfaces in Prune below through the existing
  // iv.is_empty() branch — the same all-output-bits + AddUsedConstraint
  // unsat route as an LP-certified infeasibility. Soundness of the affine
  // rows and of that refutation rests on the audited vendored kernels
  // (ibex_docs/affine-rounding-audit.md, SOUND-WITH-SCOPE-WRAP; the scope
  // wrap + D2 row-emission fix are in the built fork pin — the AFFINE-AUDIT
  // comments in ibex_LinearizerAffine2.cpp are the tell).
  //
  // Unbounded-box caveat: CtcPolytopeHull::contract is an identity on any
  // box with an unbounded dimension (ibex_CtcPolytopeHull.cpp:61), and the
  // affine relaxation additionally skips any constraint whose affine form
  // is invalid over the box — bound every real for this cell to bite
  // (docs/writing-fast-dreal-formulas.md).
  switch (config.polytope_linearizer()) {
    case PolytopeLinearizer::kXTaylor:
      // Today's default path, unchanged.
      // https://github.com/ibex-team/ibex-lib/blob/283365f677012a80c0df3920878374f6888c398d/src/numeric/ibex_LinearizerCombo.cpp#L27C15-L27C126
      linear_relax_xtaylor_ = make_unique<ibex::LinearizerXTaylor>(
          *system_, ibex::LinearizerXTaylor::RELAX,
          ibex::LinearizerXTaylor::RANDOM_OPP,
          ibex::LinearizerXTaylor::HANSEN);
      ctc_ = make_unique<ibex::CtcPolytopeHull>(*linear_relax_xtaylor_);
      break;
    case PolytopeLinearizer::kAffine:
      linear_relax_affine_ = make_unique<ibex::LinearizerAffine2>(*system_);
      ctc_ = make_unique<ibex::CtcPolytopeHull>(*linear_relax_affine_);
      break;
    case PolytopeLinearizer::kBoth:
      // ibex::LinearizerCompo(Linearizer& l1, Linearizer& l2): logical AND —
      // one LP holding both row sets (ibex_LinearizerCompo.h:29).
      linear_relax_xtaylor_ = make_unique<ibex::LinearizerXTaylor>(
          *system_, ibex::LinearizerXTaylor::RELAX,
          ibex::LinearizerXTaylor::RANDOM_OPP,
          ibex::LinearizerXTaylor::HANSEN);
      linear_relax_affine_ = make_unique<ibex::LinearizerAffine2>(*system_);
      linear_relax_compo_ = make_unique<ibex::LinearizerCompo>(
          *linear_relax_xtaylor_, *linear_relax_affine_);
      ctc_ = make_unique<ibex::CtcPolytopeHull>(*linear_relax_compo_);
      break;
  }

  // Build input.
  DynamicBitset& input{mutable_input()};
  for (const Formula& f : formulas_) {
    for (const Variable& var : f.GetFreeVariables()) {
      input.set(box.index(var));
    }
  }
}

void ContractorIbexPolytope::Prune(ContractorStatus* cs, const UpwardRounding& ur) const {
  thread_local ContractorIbexPolytopeStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_ASSERT(!is_dummy_ && ctc_);

  // CtcPolytopeHull runs gaol interval arithmetic, which is sound only under
  // FE_UPWARD. That mode is established once per ICP phase by the caller's
  // UpwardRoundingScope and proven here by the `ur` token (no per-call
  // fesetround). The assert verifies the inherited phase mode in Debug.
  // Since IBEX is built with LP_LIB=soplex (CMakeLists.txt), contract() below
  // runs the real 2n-LP-solve relaxation over gaol interval arithmetic, so this
  // token is load-bearing: a wrong ambient mode would loosen/tighten the linear
  // relaxation unsoundly (a silent false `unsat`), same failure mode as HC4.
  (void)ur;
  DREAL_ASSERT_ROUNDING(FE_UPWARD);

  Box::IntervalVector& iv{cs->mutable_box().mutable_interval_vector()};
  DREAL_LOG_TRACE("ContractorIbexPolytope::Prune");

  // Input-restricted snapshot: save only the intervals at the constraint
  // set's free-var indices instead of copying the whole IntervalVector.
  // Mirrors the Pass-2 trick from contractor_ibex_fwdbwd.cc. Polytope's
  // input() bitset aggregates free vars across formulas_; whenever that's
  // smaller than the full box, this is a strict win. thread_local keeps the
  // buffer's capacity so steady-state Prune does zero heap activity.
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

ostream& ContractorIbexPolytope::display(ostream& os) const {
  os << "IbexPolytope(";
  for (const Formula& f : formulas_) {
    os << f << ";";
  }
  os << ")";
  return os;
}

bool ContractorIbexPolytope::is_dummy() const { return is_dummy_; }

}  // namespace dreal
