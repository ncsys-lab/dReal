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
#include "dreal/contractor/contractor_ibex_obbt.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/per_thread.h"

namespace dreal {

/// Multi-thread version of ContractorIbexObbt contractor.
///
/// The base ContractorIbexObbt is not thread-safe: it owns a single
/// ibex::LPSolver (a mutable soplex instance whose constraints/bounds/cost are
/// rewritten per Prune) and a mutable ibex::LinearizerXTaylor. When there are
/// N jobs, it creates N ContractorIbexObbt instances internally and makes sure
/// that each thread calls a designated instance — the same PerThread pattern
/// as ContractorIbexPolytopeMt.
class ContractorIbexObbtMt : public ContractorCell {
 public:
  /// Constructs IbexObbtMt contractor using @p formulas and @p box.
  ContractorIbexObbtMt(std::vector<Formula> formulas, const Box& box,
                       const Config& config);

  /// Deleted copy constructor.
  ContractorIbexObbtMt(const ContractorIbexObbtMt&) = delete;

  /// Deleted move constructor.
  ContractorIbexObbtMt(ContractorIbexObbtMt&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexObbtMt& operator=(const ContractorIbexObbtMt&) = delete;

  /// Deleted move assign operator.
  ContractorIbexObbtMt& operator=(ContractorIbexObbtMt&&) = delete;

  /// Default destructor.
  ~ContractorIbexObbtMt() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal LP machinery.
  bool is_dummy() const;

 private:
  ContractorIbexObbt* GetCtcOrCreate(const Box& box) const;
  bool is_dummy_{false};

  const std::vector<Formula> formulas_;
  const Config config_;

  // One ContractorIbexObbt per worker thread (the base is not thread-safe).
  mutable PerThread<ContractorIbexObbt> ctcs_;
};

}  // namespace dreal
