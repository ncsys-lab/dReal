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

#include <memory>
#include <ostream>
#include <vector>

#include "ibex.h"

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_cell.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/ibex_converter.h"

namespace dreal {

// Custom deleter for ibex::ExprCtr. It deletes the internal
// ibex::ExprNode while keeping the ExprSymbols intact. Note that the
// ExprSymbols will be deleted separately in
// ~ContractorIbexPolytope().
struct ExprCtrDeleter {
  void operator()(const ibex::ExprCtr* const p) const {
    if (p) {
      ibex::cleanup(p->e, false);
      delete p;
    }
  }
};

// Keeps only the formulas IbexConverter can convert: drops ∃∀ `forall`s and
// ODE atoms (`integral`/`forall_t` — include_ode()), which the converter
// rejects with a throw. Shared by the system-wide cells (polytope, acid,
// mohc, obbt; mirrors the FilterEqualities precedent in
// contractor_ibex_newton.cc): each cell contracts the relational remainder —
// the ODE atoms belong to the Lohner contractor, which keeps enforcing them
// as today. This is a pure filter (never a contraction): skipping a formula
// only removes constraints from the cell's over-approximating system, so the
// cell contracts less — sound by inclusion, and COMPLETENESS-neutral for the
// solver. Because the result seeds each cell's formulas_, used-constraint
// reporting (AddUsedConstraint) names only formulas the cell actually
// converted.
std::vector<Formula> FilterIbexConvertible(std::vector<Formula> formulas);

class ContractorIbexPolytope : public ContractorCell {
 public:
  /// Constructs IbexPolytope contractor using @p f and @p vars.
  ContractorIbexPolytope(std::vector<Formula> formulas, const Box& box,
                         const Config& config);

  /// Deleted copy constructor.
  ContractorIbexPolytope(const ContractorIbexPolytope&) = delete;

  /// Deleted move constructor.
  ContractorIbexPolytope(ContractorIbexPolytope&&) = delete;

  /// Deleted copy assign operator.
  ContractorIbexPolytope& operator=(const ContractorIbexPolytope&) = delete;

  /// Deleted move assign operator.
  ContractorIbexPolytope& operator=(ContractorIbexPolytope&&) = delete;

  /// Default destructor.
  ~ContractorIbexPolytope() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;
  std::ostream& display(std::ostream& os) const override;

  /// Returns true if it has no internal ibex contractor.
  bool is_dummy() const;

 private:
  const std::vector<Formula> formulas_;
  bool is_dummy_{false};

  IbexConverter ibex_converter_;
  std::unique_ptr<ibex::SystemFactory> system_factory_;
  std::unique_ptr<ibex::System> system_;
  // Linear relaxation selected by config.polytope_linearizer(); only the
  // members that selection needs are constructed (kXTaylor: xtaylor only;
  // kAffine: affine only; kBoth: all three, the compo referencing the other
  // two). DECLARATION ORDER IS LOAD-BEARING: ctc_ holds a reference to the
  // selected linearizer and the compo holds references to both single
  // linearizers, so the linearizers are declared BEFORE ctc_ (and are
  // therefore destroyed after it).
  std::unique_ptr<ibex::LinearizerXTaylor> linear_relax_xtaylor_;
  std::unique_ptr<ibex::LinearizerAffine2> linear_relax_affine_;
  std::unique_ptr<ibex::LinearizerCompo> linear_relax_compo_;
  std::unique_ptr<ibex::CtcPolytopeHull> ctc_;
  std::vector<std::unique_ptr<const ibex::ExprCtr, ExprCtrDeleter>> expr_ctrs_;
};

}  // namespace dreal
