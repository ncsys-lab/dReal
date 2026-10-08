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
#include "dreal/contractor/contractor_status.h"

#include <atomic>
#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/stat.h"
#include "dreal/util/timer.h"

using std::vector;

namespace dreal {

namespace {

using std::cout;

// A class to show statistics information at destruction.
class ContractorStatusStat : public Stat {
 public:
  explicit ContractorStatusStat(const bool enabled) : Stat{enabled} {}
  ContractorStatusStat(const ContractorStatusStat&) = delete;
  ContractorStatusStat(ContractorStatusStat&&) = delete;
  ContractorStatusStat& operator=(const ContractorStatusStat&) = delete;
  ContractorStatusStat& operator=(ContractorStatusStat&&) = delete;
  ~ContractorStatusStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n",
            "Total # of Explanation Generations", "ContractorStatus level",
            num_explanation_generation_);
      if (num_explanation_generation_) {
        print(cout, "{:<45} @ {:<20} = {:>15f} sec\n",
              "Total time spent in Explanation Generations",
              "ContractorStatus level",
              timer_explanation_generation_.seconds());
      }
    }
  }

  void increase_num_explanation_generation() {
    increase(&num_explanation_generation_);
  }

  Timer timer_explanation_generation_;

 private:
  std::atomic<int> num_explanation_generation_{0};
};

}  // namespace

ContractorStatus::ContractorStatus(Box box, const int branching_point)
    : box_{std::move(box)},
      branching_point_{branching_point},
      output_{DynamicBitset(box_.size())} {
  DREAL_ASSERT(!box_.empty());
  DREAL_ASSERT(branching_point_ >= -1 && branching_point_ < box_.size());
}

const Box& ContractorStatus::box() const { return box_; }

Box& ContractorStatus::mutable_box() { return box_; }

int ContractorStatus::branching_point() const { return branching_point_; }

int& ContractorStatus::mutable_branching_point() { return branching_point_; }

const DynamicBitset& ContractorStatus::output() const { return output_; }

DynamicBitset& ContractorStatus::mutable_output() { return output_; }

void ContractorStatus::AddUsedConstraint(const Formula& f) {
  AddUsedConstraint(f, f.GetFreeVariables());
}

void ContractorStatus::AddUsedConstraint(const Formula& f,
                                         const Variables& reach) {
  DREAL_LOG_DEBUG("ContractorStatus::AddUsedConstraint({}, {}) box is empty? {}",
                  f, fmt::streamed(reach), box_.empty());
  if (!reach.IsSubsetOf(f.GetFreeVariables())) {
    throw DREAL_RUNTIME_ERROR(
        "ContractorStatus::AddUsedConstraint: {} is not a subset of the "
        "variables of {}", fmt::streamed(reach), f);
  }
  if (box_.empty()) {
    for (const Variable& v : reach) {
      AddUnsatWitness(v);
    }
  }
  used_constraints_[f].insert(reach);
}

void ContractorStatus::AddUsedConstraint(const vector<Formula>& formulas) {
  for (const Formula& f : formulas) {
    AddUsedConstraint(f);
  }
}

void ContractorStatus::AddInconclusiveOde(const Formula& f,
                                          const std::string& reason) {
  DREAL_LOG_DEBUG("ContractorStatus::AddInconclusiveOde({}): {}", f, reason);
  inconclusive_odes_.emplace(f, reason);
}

void ContractorStatus::AddUnsatWitness(const Variable& var) {
  DREAL_LOG_DEBUG("ContractorStatus::AddUnsatWitness({})", fmt::streamed(var));
  unsat_witness_.insert(var);
}

FormulaSet GenerateExplanation(const Variables& unsat_witness,
                                 const FormulaMap<Variables>& used_constraints,
                                 const FormulaMap<std::string>& inconclusive_odes) {
  static ContractorStatusStat stat(DREAL_LOG_INFO_ENABLED);
  stat.increase_num_explanation_generation();
  TimerGuard timer_guard(&stat.timer_explanation_generation_, stat.enabled());

  // Explanation, where reach(f) is the variables a used constraint f is
  // reached through (all of vars(f) unless recorded otherwise):
  // = lfp. λE. E ∪ {fᵢ | fᵢ ∈ Used Constraints ∧ reach(fᵢ) ∩ unsat_witness ≠ ∅}
  //              ∪ {fᵢ | fᵢ ∈ E ∧
  //                      fⱼ ∈ Used Constraints ∧
  //                      reach(fᵢ) and reach(fⱼ) share a common variable}.

  // Set up the initial explanation based on variables.
  Variables seen;
  FormulaSet explanation;
  for (const auto& [f_i, reach] : used_constraints) {
    if (reach.empty()) {
      // It is possible that the constraint has no free variable but
      // we can't decide its truth value. For example, in SMT2, (0.01
      // < 1.0) will be translated into a formula with intervals and
      // we don't do constant-folding over intervals yet, so it
      // remains as it is.
      explanation.insert(f_i);
      continue;
    }
    if (HaveIntersection(unsat_witness, reach)) {
      explanation.insert(f_i);
      seen.insert(reach);
    }
  }

  if (unsat_witness.empty()) {
    return explanation;
  }

  bool keep_going = true;
  while (keep_going) {
    keep_going = false;
    for (const auto& [f_j, reach] : used_constraints) {
      if (explanation.count(f_j) > 0) continue;
      if (HaveIntersection(seen, reach)) {
        seen.insert(reach);
        explanation.insert(f_j);
        keep_going = true;
      }
    }
  }

  // Splice in the inconclusive ODE constraints (CAPD diverged etc., so they
  // recorded no narrowing) as NON-EXPANDING leaves: include each whose variables
  // touch the EMPTYING witness `unsat_witness` — the vars of the constraints that
  // actually pruned the box to empty — not the whole relational closure `seen`.
  // The responsible ODE shares the emptying constraint's time/state var (the
  // relational system pins the candidate the ODE rejects), so the witness is the
  // principled "minimal relevant" tie; intersecting with the broad `seen` instead
  // drags in ODEs from unrelated parts of the densely-chained BMC unrolling,
  // bloating the lemma and starving the SAT search (measured: a c2e2 k12 SAT
  // instance went 9 s -> timeout). Soundness is monotone either way — adding
  // constraints can only shrink the solution set of `R ∧ E`, never make a sound
  // ¬E unsound — so the witness tie is validated by the no-flip + auditor oracles,
  // not assumed minimal.
  for (const auto& [f, reason] : inconclusive_odes) {
    if (HaveIntersection(unsat_witness, f.GetFreeVariables())) {
      explanation.insert(f);
    }
  }
  return explanation;
}

FormulaSet ContractorStatus::Explanation() const {
  return GenerateExplanation(unsat_witness_, used_constraints_, inconclusive_odes_);
}

ContractorStatus& ContractorStatus::InplaceJoin(
    const ContractorStatus& contractor_status) {
  box_.InplaceUnion(contractor_status.box());
  output_ |= contractor_status.output();
  unsat_witness_.insert(contractor_status.unsat_witness_.begin(),
                        contractor_status.unsat_witness_.end());
  for (const auto& [f, reach] : contractor_status.used_constraints_) {
    used_constraints_[f].insert(reach);
  }
  inconclusive_odes_.insert(contractor_status.inconclusive_odes_.begin(),
                            contractor_status.inconclusive_odes_.end());
  return *this;
}

ContractorStatus Join(ContractorStatus contractor_status1,
                      const ContractorStatus& contractor_status2) {
  // This function updates `contractor_status1`, which is passed by value, and
  // returns it.
  return contractor_status1.InplaceJoin(contractor_status2);
}

}  // namespace dreal
