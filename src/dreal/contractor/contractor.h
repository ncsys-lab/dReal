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
#include <iostream>
#include <memory>
#include <vector>

#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/config.h"
#include "dreal/util/box.h"
#include "dreal/util/rounding.h"
#include "odes/ode_types.h"

namespace dreal {
// Forward declarations.
class ContractorCell;
class ContractorId;
class ContractorInteger;
class ContractorSeq;
class ContractorIbexFwdbwd;
class ContractorIbexPolytope;
class ContractorIbexAcid;
class ContractorFixpoint;
class ContractorWorklistFixpoint;
class ContractorJoin;
class contractor_ode_lohner;
template <typename ContextType>
class ContractorForall;

// Box::IntervalVector × Box::IntervalVector → Bool. Evaluated inside a fixpoint
// Prune, so it carries the UpwardRounding token (it inspects interval diameters
// via safe_diam, which is gaol).
using TerminationCondition = std::function<bool(
    Box::IntervalVector const&, Box::IntervalVector const&,
    const UpwardRounding&)>;

class Contractor {
 public:
  enum class Kind {
    ID,
    INTEGER,
    SEQ,
    IBEX_FWDBWD,
    IBEX_POLYTOPE,
    IBEX_ACID,  // ACID / 3BCID shaving over the HC4 path (ContractorIbexAcid).
    IBEX_NEWTON,  // Interval-Newton (Hansen-Sengupta) contraction on the
                  // square equality subsystem (ContractorIbexNewton).
    IBEX_OBBT,  // Optimization-based bound tightening: 2n certified LPs over
                // the X-Taylor linear relaxation (ContractorIbexObbt).
    IBEX_MOHC,  // Mohc monotonic-occurrence propagation: occurrence-grouping
                // monotone revise + monotone BoxNarrow over the assertion
                // system (ContractorIbexMohc).
    IBEX_FORALL,  // Sound proj-inter pre-pruner over ibex::CtcForAll, run beside
                  // the CEGIS FORALL decider (ContractorIbexForall). PITFALL
                  // forall-vs-forall_t: the ∃∀ NRA quantifier, not `forall_t`.
    FIXPOINT,
    WORKLIST_FIXPOINT,
    FORALL,  // ∃∀ NRA quantifier (ContractorForall, CE-guided). PITFALL forall-vs-forall_t:
             // NOT `forall_t` — that ODE trajectory invariant is checked inside ODE_LOHNER
             // below. See docs/forall-semantics.md §7.
    JOIN,
    ODE_LOHNER  // Replaces CAPD_FULL; uses IBEX interval arithmetic for ODE bounds. Also
                // where the `forall_t` (FormulaKind::ForallT) per-slice invariant is enforced
                // — distinct from the ∃∀ FORALL kind above (PITFALL forall-vs-forall_t).
  };

  explicit Contractor(const Config& config);

  /// Default copy constructor.
  Contractor(const Contractor&) = default;

  /// Default move constructor.
  Contractor(Contractor&&) = default;

  /// Default copy assign operator.
  Contractor& operator=(const Contractor&) = default;

  /// Default move assign operator.
  Contractor& operator=(Contractor&&) = default;

  /// Default destructor.
  ~Contractor() = default;

  /// Returns the input vector of this contractor. `input()[i] = true`
  /// means that this contractor depends on the value of `box[i]`.
  const DynamicBitset& input() const;

  /// Prunes @p cs. Requires @p ur, the capability token witnessing that the
  /// FE_UPWARD interval-phase rounding mode is established (see
  /// UpwardRoundingScope). gaol/ibex contractors are sound only under
  /// FE_UPWARD; threading the token makes that a compile-time obligation.
  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const;

  /// Returns kind.
  Kind kind() const;

  /// Returns true if this contractor includes a forall contractor.
  bool include_forall() const;

  /// Sets include_forall true.
  void set_include_forall();

  friend std::ostream& operator<<(std::ostream& os, Contractor const& ctc);

 private:
  explicit Contractor(std::shared_ptr<ContractorCell> ptr);

  std::shared_ptr<ContractorCell> ptr_{};

  friend Contractor make_contractor_id(const Config& config);
  friend Contractor make_contractor_integer(const Box& box,
                                            const Config& config);
  friend Contractor make_contractor_seq(
      const std::vector<Contractor>& contractors, const Config& config);
  friend Contractor make_contractor_ibex_fwdbwd(Formula f, const Box& box,
                                                const Config& config);
  friend Contractor make_contractor_ibex_polytope(std::vector<Formula> formulas,
                                                  const Box& box,
                                                  const Config& config);
  friend Contractor make_contractor_ibex_acid(std::vector<Formula> formulas,
                                              const Box& box,
                                              const Config& config);
  friend Contractor make_contractor_ibex_newton(std::vector<Formula> formulas,
                                                const Box& box,
                                                const Config& config);
  friend Contractor make_contractor_ibex_obbt(std::vector<Formula> formulas,
                                              const Box& box,
                                              const Config& config);
  friend Contractor make_contractor_ibex_mohc(std::vector<Formula> formulas,
                                              const Box& box,
                                              const Config& config);
  friend Contractor make_contractor_ibex_forall(Formula f, const Box& box,
                                                const Config& config);
  friend Contractor make_contractor_fixpoint(
      TerminationCondition term_cond,
      const std::vector<Contractor>& contractors, const Config& config);
  friend Contractor make_contractor_worklist_fixpoint(
      TerminationCondition term_cond,
      const std::vector<Contractor>& contractors, const Config& config);
  template <typename ContextType>
  friend Contractor make_contractor_forall(Formula f, const Box& box,
                                           double epsilon, double inner_delta,
                                           const Config& config);
  friend Contractor make_contractor_join(std::vector<Contractor> vec,
                                         const Config& config);
  friend Contractor mk_contractor_ode_lohner(Box const& box, const ode_constraint& ctr,
                                             ode_direction dir, Config const& config,
                                             double timeout);

  // Note that the following converter functions are only for
  // low-level operations. To use them, you need to include
  // "contractor_cell.h" file.
  friend std::shared_ptr<ContractorId> to_id(const Contractor& contractor);
  friend std::shared_ptr<ContractorInteger> to_integer(
      const Contractor& contractor);
  friend std::shared_ptr<ContractorSeq> to_seq(const Contractor& contractor);
  friend std::shared_ptr<ContractorIbexFwdbwd> to_ibex_fwdbwd(
      const Contractor& contractor);
  friend std::shared_ptr<ContractorIbexPolytope> to_ibex_polytope(
      const Contractor& contractor);
  friend std::shared_ptr<ContractorFixpoint> to_fixpoint(
      const Contractor& contractor);
  friend std::shared_ptr<ContractorWorklistFixpoint> to_worklist_fixpoint(
      const Contractor& contractor);
  friend std::shared_ptr<ContractorJoin> to_join(const Contractor& contractor);
  friend std::shared_ptr<contractor_ode_lohner> to_ode_lohner(const Contractor& contractor);
  template <typename ContextType>
  friend std::shared_ptr<ContractorForall<ContextType>> to_forall(
      const Contractor& contractor);
};

/// Returns an idempotent contractor.
/// @see ContractorId.
Contractor make_contractor_id(const Config& config);

/// Returns an integer contractor. For an integer variable `v`, it
/// prunes `b[v] = [lb, ub]` into `[ceil(lb), floor(ub)]`. It sets the box empty
/// if it detects an empty interval in pruning.
///
/// @see ContractorInteger.
Contractor make_contractor_integer(const Box& box, const Config& config);

/// Returns a sequential contractor `C` from a vector of contractors
/// @p vec = [C₁, ..., Cₙ]. It applies `Cᵢ` sequentially. That is, we have:
/// <pre>
///     C(box) = (Cₙ∘...∘C₁)(box)
/// </pre>
///
/// @see ContractorSeq.
Contractor make_contractor_seq(const std::vector<Contractor>& contractors,
                               const Config& config);

/// Returns a contractor wrapping IBEX's forward/backward contractor.
/// If the number of jobs (in @p config) > 1, it creates a
/// multi-threaded version of the contractor, which is based on
/// ContractorIbexFwdbwdMt. Otherwise, it creates an instance of
/// ContractorIbexFwdbwd.
///
/// @see ContractorIbexFwdbwd.
/// @see ContractorIbexFwdbwdMt.
Contractor make_contractor_ibex_fwdbwd(Formula f, const Box& box,
                                       const Config& config);

/// Returns a contractor wrapping IBEX's polytope contractor.  If then
/// number of jobs (in @p config) > 1, it creates a multi-threaded version of
/// the contractor, which is based on ContractorIbexPolytopeMt. Otherwise, it
/// creates an instance of ContractorIbexPolytope.
///
/// @see ContractorIbexPolytope.
/// @see ContractorIbexPolytopeMt.
Contractor make_contractor_ibex_polytope(std::vector<Formula> formulas,
                                         const Box& box, const Config& config);

/// Returns a contractor wrapping IBEX's ACID (config.use_acid()) or 3BCID
/// (config.use_3bcid()) shaving contractor over the HC4 path. If the number
/// of jobs (in @p config) > 1, it creates a multi-threaded version based on
/// ContractorIbexAcidMt (one ContractorIbexAcid per worker thread —
/// ibex::CtcAcid keeps mutable shaving/adaptivity state; see contractor.cc).
///
/// @see ContractorIbexAcid.
Contractor make_contractor_ibex_acid(std::vector<Formula> formulas,
                                     const Box& box, const Config& config);

/// Returns a contractor wrapping IBEX's interval-Newton contractor
/// (ibex::CtcNewton) on the square equality subsystem of @p formulas: the
/// positive equality atoms over the variable set S they mention, kept only
/// when #equalities == |S| > 0; otherwise the ID contractor is returned. If
/// the number of jobs (in @p config) > 1, it creates a multi-threaded version
/// based on ContractorIbexNewtonMt (one cell per worker thread —
/// ibex::Function evaluation keeps mutable state and is not thread-safe to
/// share).
///
/// @see ContractorIbexNewton.
/// @see ContractorIbexNewtonMt.
Contractor make_contractor_ibex_newton(std::vector<Formula> formulas,
                                       const Box& box, const Config& config);

/// Returns a contractor performing optimization-based bound tightening: 2n
/// certified LPs (min/max each variable) over one X-Taylor linearization of
/// @p formulas, driving ibex::LPSolver in Mode::Certified directly. Only
/// Neumaier-Shcherbina-certified LP results are used for contraction. If the
/// number of jobs (in @p config) > 1, it creates a multi-threaded version of
/// the contractor, which is based on ContractorIbexObbtMt.
///
/// @see ContractorIbexObbt.
/// @see ContractorIbexObbtMt.
Contractor make_contractor_ibex_obbt(std::vector<Formula> formulas,
                                     const Box& box, const Config& config);

/// Returns a contractor wrapping IBEX's Mohc monotonicity-exploiting
/// propagation contractor (ibex::CtcMohc over the assertion system, all IBEX
/// defaults; Araya et al., AAAI'10). Every MohcRevise starts with the
/// HC4Revise pair and adds occurrence-grouping monotone revise + monotone
/// BoxNarrow for multi-occurrence constraints. Composable with --acid/--3bcid
/// (different mechanisms). If the number of jobs (in @p config) > 1, it
/// creates a multi-threaded version based on ContractorIbexMohcMt (one cell
/// per worker thread — CtcMohcRevise keeps mutable per-call state and is not
/// thread-safe to share).
///
/// @see ContractorIbexMohc.
/// @see ContractorIbexMohcMt.
Contractor make_contractor_ibex_mohc(std::vector<Formula> formulas,
                                     const Box& box, const Config& config);

/// Returns a sound proj-intersection pre-pruner over IBEX's ibex::CtcForAll for
/// the ∀-formula @p f, meant to run *alongside* the CEGIS ContractorForall.
/// Under --jobs>1 it returns a ContractorIbexForallMt that builds one
/// ibex::CtcForAll per worker thread (the base holds a mutable worklist and is
/// not thread-safe to share).
///
/// @see ContractorIbexForall, ContractorIbexForallMt.
Contractor make_contractor_ibex_forall(Formula f, const Box& box,
                                       const Config& config);

/// Returns a fixed-point contractor. The returned contractor applies
/// the contractors in @p vec sequentially until @p term_cond is met.
///
/// @see ContractorFixpoint.
Contractor make_contractor_fixpoint(TerminationCondition term_cond,
                                    const std::vector<Contractor>& contractors,
                                    const Config& config);

/// Returns a worklist fixed-point contractor. The returned contractor
/// applies the contractors in @p vec sequentially until @p term_cond
/// is met.
///
/// @see ContractorFixpoint.
Contractor make_contractor_worklist_fixpoint(
    TerminationCondition term_cond, const std::vector<Contractor>& contractors,
    const Config& config);

/// Returns a join contractor. The returned contractor does the following
/// operation:
/// <pre>
///     (C₁ ∨ ... ∨ Cₙ)(box) = C₁(box) ∨ ... ∨ Cₙ(box).
/// </pre>
///
/// @see ContractorJoin.
Contractor make_contractor_join(std::vector<Contractor> vec,
                                const Config& config);

Contractor mk_contractor_ode_lohner(Box const& box, const ode_constraint& ctr,
                                    ode_direction dir, Config const& config,
                                    double timeout);

/// Returns a forall contractor.
///
/// @note the implementation is at `dreal/contractor/contractor_forall.h` file.
/// @see ContractorForall.
template <typename ContextType>
Contractor make_contractor_forall(Formula f, const Box& box, double epsilon,
                                  double inner_delta, const Config& config);

std::ostream& operator<<(std::ostream& os, const Contractor& ctc);

/// Returns true if @p contractor is idempotent contractor.
bool is_id(const Contractor& contractor);

/// Returns true if @p contractor is integer contractor.
bool is_integer(const Contractor& contractor);

/// Returns true if @p contractor is sequential contractor.
bool is_seq(const Contractor& contractor);

/// Returns true if @p contractor is IBEX fwdbwd contractor.
bool is_ibex_fwdbwd(const Contractor& contractor);

/// Returns true if @p contractor is IBEX polytope contractor.
bool is_ibex_polytope(const Contractor& contractor);

/// Returns true if @p contractor is fixpoint contractor.
bool is_fixpoint(const Contractor& contractor);

/// Returns true if @p contractor is worklist-fixpoint contractor.
bool is_worklist_fixpoint(const Contractor& contractor);

/// Returns true if @p contractor is forall contractor.
bool is_forall(const Contractor& contractor);

/// Returns true if @p contractor is join contractor.
bool is_join(const Contractor& contractor);

bool is_ode_lohner(const Contractor& contractor);

}  // namespace dreal
