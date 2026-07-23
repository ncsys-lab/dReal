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
#include "dreal/contractor/contractor_ibex_newton.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/per_thread.h"

namespace dreal {

/// Multi-thread version of ContractorIbexNewton contractor.
///
/// The base ContractorIbexNewton is not thread-safe (ibex::Function
/// evaluation — the Jacobian/Gauss-Seidel work inside ibex::newton — keeps
/// mutable per-call state). When there are N jobs, it creates N
/// ContractorIbexNewton instances internally and makes sure that each thread
/// calls a designated instance — the same PerThread pattern as
/// ContractorIbexAcidMt / ContractorIbexPolytopeMt.
class ContractorIbexNewtonMt : public ContractorCell {
 public:
  /// Constructs IbexNewtonMt contractor using @p formulas and @p box.
  ContractorIbexNewtonMt(std::vector<Formula> formulas, const Box& box,
                         const Config& config);

  /// Deleted copy constructor.
  ContractorIbexNewtonMt(const ContractorIbexNewtonMt&) = delete;

  /// Deleted move constructor.
  ContractorIbexNewtonMt(ContractorIbexNewtonMt&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexNewtonMt& operator=(const ContractorIbexNewtonMt&) = delete;

  /// Deleted move assign operator.
  ContractorIbexNewtonMt& operator=(ContractorIbexNewtonMt&&) = delete;

  /// Default destructor.
  ~ContractorIbexNewtonMt() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor.
  bool is_dummy() const;

 private:
  ContractorIbexNewton* GetCtcOrCreate(const Box& box) const;
  bool is_dummy_{false};

  const std::vector<Formula> formulas_;
  const Config config_;

  // One ContractorIbexNewton per worker thread (the base is not thread-safe).
  mutable PerThread<ContractorIbexNewton> ctcs_;
};

}  // namespace dreal
