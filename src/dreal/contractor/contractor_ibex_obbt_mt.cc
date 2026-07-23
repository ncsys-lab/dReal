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
#include "dreal/contractor/contractor_ibex_obbt_mt.h"

#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"

using std::make_unique;
using std::ostream;
using std::vector;

namespace dreal {

ContractorIbexObbtMt::ContractorIbexObbtMt(vector<Formula> formulas,
                                           const Box& box,
                                           const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_OBBT, DynamicBitset(box.size()),
                     config},
      formulas_{std::move(formulas)},
      config_{config},
      ctcs_(config_.number_of_jobs()) {
  DREAL_LOG_DEBUG("ContractorIbexObbtMt::ContractorIbexObbtMt");
  ContractorIbexObbt* const ctc{GetCtcOrCreate(box)};
  DREAL_ASSERT(ctc);
  // Build input.
  mutable_input() = ctc->input();

  is_dummy_ = ctc->is_dummy();
}

ContractorIbexObbt* ContractorIbexObbtMt::GetCtcOrCreate(
    const Box& box) const {
  return &ctcs_.GetOrCreate(
      [&]() { return make_unique<ContractorIbexObbt>(formulas_, box, config_); });
}

void ContractorIbexObbtMt::Prune(ContractorStatus* cs,
                                 const UpwardRounding& ur) const {
  ContractorIbexObbt* const ctc{GetCtcOrCreate(cs->box())};
  DREAL_ASSERT(ctc && !is_dummy_);
  return ctc->Prune(cs, ur);
}

ostream& ContractorIbexObbtMt::display(ostream& os) const {
  os << "IbexObbtMt(";
  for (const Formula& f : formulas_) {
    os << f << ";";
  }
  return os << ")";
}

bool ContractorIbexObbtMt::is_dummy() const { return is_dummy_; }

}  // namespace dreal
