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
#include "dreal/contractor/contractor_ibex_acid_mt.h"

#include <utility>

#include "dreal/util/assert.h"
#include "dreal/util/logging.h"

using std::make_unique;
using std::ostream;
using std::vector;

namespace dreal {

ContractorIbexAcidMt::ContractorIbexAcidMt(vector<Formula> formulas,
                                           const Box& box,
                                           const Config& config)
    : ContractorCell{Contractor::Kind::IBEX_ACID, DynamicBitset(box.size()),
                     config},
      formulas_{std::move(formulas)},
      config_{config},
      ctcs_(config_.number_of_jobs()) {
  DREAL_LOG_DEBUG("ContractorIbexAcidMt::ContractorIbexAcidMt");
  ContractorIbexAcid* const ctc{GetCtcOrCreate(box)};
  DREAL_ASSERT(ctc);
  // Build input.
  mutable_input() = ctc->input();

  is_dummy_ = ctc->is_dummy();
}

ContractorIbexAcid* ContractorIbexAcidMt::GetCtcOrCreate(
    const Box& box) const {
  return &ctcs_.GetOrCreate(
      [&]() { return make_unique<ContractorIbexAcid>(formulas_, box, config_); });
}

void ContractorIbexAcidMt::Prune(ContractorStatus* cs,
                                 const UpwardRounding& ur) const {
  ContractorIbexAcid* const ctc{GetCtcOrCreate(cs->box())};
  DREAL_ASSERT(ctc && !is_dummy_);
  return ctc->Prune(cs, ur);
}

ostream& ContractorIbexAcidMt::display(ostream& os) const {
  os << "IbexAcidMt(";
  for (const Formula& f : formulas_) {
    os << f << ";";
  }
  return os << ")";
}

bool ContractorIbexAcidMt::is_dummy() const { return is_dummy_; }

}  // namespace dreal
