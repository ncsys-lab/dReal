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
#include "dreal/contractor/contractor_ibex_mohc.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/per_thread.h"

namespace dreal {

/// Multi-thread version of ContractorIbexMohc contractor.
///
/// The base ContractorIbexMohc is not thread-safe (each ibex::CtcMohcRevise
/// keeps mutable per-call state — active_mono_proc, Apply arrays, LB/RB,
/// zmin/zmax, OG scratch — and CtcMohc::contract rewrites it every call).
/// When there are N jobs, it creates N ContractorIbexMohc instances
/// internally and makes sure that each thread calls a designated instance —
/// the same PerThread pattern as ContractorIbexAcidMt /
/// ContractorIbexNewtonMt. The amohc counters (CtcMohc::nb_calls /
/// nb_interesting) are static process-globals shared by ALL workers'
/// instances; that unsynchronized cross-worker counting is a documented,
/// accepted benign race — it only steers the adaptive tau_mohc heuristic
/// (contraction strength; COMPLETENESS-only, never a verdict).
class ContractorIbexMohcMt : public ContractorCell {
 public:
  /// Constructs IbexMohcMt contractor using @p formulas and @p box.
  ContractorIbexMohcMt(std::vector<Formula> formulas, const Box& box,
                       const Config& config);

  /// Deleted copy constructor.
  ContractorIbexMohcMt(const ContractorIbexMohcMt&) = delete;

  /// Deleted move constructor.
  ContractorIbexMohcMt(ContractorIbexMohcMt&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexMohcMt& operator=(const ContractorIbexMohcMt&) = delete;

  /// Deleted move assign operator.
  ContractorIbexMohcMt& operator=(ContractorIbexMohcMt&&) = delete;

  /// Default destructor.
  ~ContractorIbexMohcMt() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor.
  bool is_dummy() const;

 private:
  ContractorIbexMohc* GetCtcOrCreate(const Box& box) const;
  bool is_dummy_{false};

  const std::vector<Formula> formulas_;
  const Config config_;

  // One ContractorIbexMohc per worker thread (the base is not thread-safe).
  mutable PerThread<ContractorIbexMohc> ctcs_;
};

}  // namespace dreal
