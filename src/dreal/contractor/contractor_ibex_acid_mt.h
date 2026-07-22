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

#include <ostream>
#include <vector>

#include "dreal/contractor/contractor_cell.h"
#include "dreal/contractor/contractor_ibex_acid.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/per_thread.h"

namespace dreal {

/// Multi-thread version of ContractorIbexAcid contractor.
///
/// The base ContractorIbexAcid is not thread-safe (ibex::CtcAcid keeps
/// mutable shaving/adaptivity state). When there are N jobs, it creates N
/// ContractorIbexAcid instances internally and makes sure that each thread
/// calls a designated instance — the same PerThread pattern as
/// ContractorIbexPolytopeMt. Each worker's ACID adaptivity (impact-ordered
/// var list, CID-var count) evolves independently; that is a per-worker
/// COMPLETENESS heuristic only and never moves a verdict.
class ContractorIbexAcidMt : public ContractorCell {
 public:
  /// Constructs IbexAcidMt contractor using @p formulas and @p box.
  ContractorIbexAcidMt(std::vector<Formula> formulas, const Box& box,
                       const Config& config);

  /// Deleted copy constructor.
  ContractorIbexAcidMt(const ContractorIbexAcidMt&) = delete;

  /// Deleted move constructor.
  ContractorIbexAcidMt(ContractorIbexAcidMt&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexAcidMt& operator=(const ContractorIbexAcidMt&) = delete;

  /// Deleted move assign operator.
  ContractorIbexAcidMt& operator=(ContractorIbexAcidMt&&) = delete;

  /// Default destructor.
  ~ContractorIbexAcidMt() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor.
  bool is_dummy() const;

 private:
  ContractorIbexAcid* GetCtcOrCreate(const Box& box) const;
  bool is_dummy_{false};

  const std::vector<Formula> formulas_;
  const Config config_;

  // One ContractorIbexAcid per worker thread (the base is not thread-safe).
  mutable PerThread<ContractorIbexAcid> ctcs_;
};

}  // namespace dreal
