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

#include <cmath>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_cell.h"
#include "dreal/contractor/counterexample_refiner.h"
#include "dreal/contractor/forall_counterexample_query.h"
#include "dreal/contractor/generic_contractor_generator.h"
#include "dreal/util/assert.h"
#include "dreal/util/box.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/exception.h"
#include "dreal/util/interrupt.h"
#include "dreal/util/logging.h"
#include "dreal/util/nnfizer.h"
#include "dreal/util/optional.h"
#include "dreal/util/per_thread.h"

namespace dreal {

/// Contractor for the ∃∀ NRA `forall` quantifier (`Formula::Forall`, `Kind::FORALL`).
///
/// PITFALL forall-vs-forall_t: this is NOT the ODE-time `forall_t`
/// (`FormulaKind::ForallT`), whose per-slice trajectory-invariant check lives in the ODE
/// contractor (`contractor_odes*.cc`, `Kind::ODE_LOHNER`). Independent machinery — see
/// docs/forall-semantics.md §7.
///
/// See the following problem definition and our approach.
///
/// <pre>
/// Problem: Given a box B ∈ IRⁿ and a formula F =
/// ∃x₁...xₙ. ∀y₁....yₘ. φ(x₁, ..., xₙ, y₁, ..., yₘ), design a
/// contractor reducing B into B' where B' ⊆ B.
///
/// Approach: Find a counterexample (a₁, ..., aₙ, b₁, ..., bₘ) such
/// that ¬φ(a₁, ..., aₙ, b₁, ..., bₘ) holds while (a₁, ..., aₙ) ∈ B.
/// We do this by computing Solve(domain ∧ strengthen(¬body, ε), inner_delta),
/// strengthening only the matrix body and keeping the universal-domain bounds
/// exact (see forall_counterexample_query.h), with inner_delta < ε < precision.
///
///  - Case 1: No CE found.
///            This means that any point in B satisfies the quantified
///            portion of F.
///
///  - Case 2: Found a CE, (a₁, ..., aₙ, b₁, ..., bₘ).
///            Use the CE to reduce B into B'. That is compute,
///
///            B' = Contract(φ(x₁, ..., xₙ, b₁, ..., bₘ), B)
///
/// </pre>
template <typename ContextType>
class ContractorForall : public ContractorCell {
 public:
  /// Constructs Forall contractor using @p f and @p box. @p epsilon is
  /// used to strengthen ¬φ and @p inner_delta is used to solve (¬φ)⁻ᵟ¹.
  ///
  /// @pre 0.0 < inner_delta < epsilon < config.precision().
  ContractorForall(Formula f, const Box& box, double epsilon,
                   double inner_delta, const Config& config)
      : ContractorCell{Contractor::Kind::FORALL,
                       DynamicBitset(box.size()), config},
        f_{std::move(f)},
        quantified_variables_{get_quantified_variables(f_)},
        strengthend_negated_nested_f_{Nnfizer{}.Convert(
            StrengthenForallCounterexampleQuery(get_quantified_formula(f_),
                                                quantified_variables_, epsilon),
            true)},
        contractor_{config /* This one will be updated anyway. */},
        context_for_counterexample_{MakeCounterexampleConfig(config)} {
    DREAL_ASSERT(epsilon > 0.0);
    DREAL_ASSERT(inner_delta > 0.0);
    DREAL_ASSERT(config.precision() > epsilon);
    DREAL_ASSERT(epsilon > inner_delta);
    DREAL_ASSERT(!is_false(strengthend_negated_nested_f_));

    set_include_forall();

    // Setup context:
    // 0. Setup context, config, and the contractor for finding counterexamples.
    context_for_counterexample_.mutable_config().mutable_precision() =
        inner_delta;
    context_for_counterexample_.mutable_config().mutable_use_polytope() =
        config.use_polytope_in_forall();
    contractor_ = GenericContractorGenerator{}.Generate(
        get_quantified_formula(f_), ExtendBox(box, quantified_variables_),
        context_for_counterexample_.config());
    // 1. Add exist/forall variables.
    for (const Variable& exist_var : box.variables()) {
      context_for_counterexample_.DeclareVariable(exist_var);
    }
    for (const Variable& forall_var : get_quantified_variables(f_)) {
      context_for_counterexample_.DeclareVariable(forall_var);
    }
    // 2. Assert strengthen(¬φ, ε).
    if (is_conjunction(strengthend_negated_nested_f_)) {
      // Optimizations
      for (const Formula& formula :
           get_operands(strengthend_negated_nested_f_)) {
        context_for_counterexample_.Assert(formula);
      }
    } else {
      context_for_counterexample_.Assert(strengthend_negated_nested_f_);
    }

    // Build input.
    DynamicBitset& input{mutable_input()};
    for (const Variable& v : f_.GetFreeVariables()) {
      input.set(box.index(v));
    }
    if (this->config().use_local_optimization()) {
      refiner_ = std::make_unique<CounterexampleRefiner>(
          strengthend_negated_nested_f_, quantified_variables_,
          context_for_counterexample_.config());
    }
  }

  /// Deleted copy constructor.
  ContractorForall(const ContractorForall&) = delete;

  /// Deleted move constructor.
  ContractorForall(ContractorForall&&) = delete;

  /// Deleted copy assign operator.
  ContractorForall& operator=(const ContractorForall&) = delete;

  /// Deleted move assign operator.
  ContractorForall& operator=(ContractorForall&&) = delete;

  bool PruneWithCounterexample(ContractorStatus* cs, Box* const current_box,
                               const Box& counterexample,
                               const UpwardRounding& ur) const {
    // Need to prune the current_box using counterexample.
    ContractorStatus contractor_status(counterexample);
    // 1.1.1. Set up exist_var parts for pruning
    for (const Variable& exist_var : current_box->variables()) {
      contractor_status.mutable_box()[exist_var] = (*current_box)[exist_var];
    }
    // 1.1.2. Set up universal_var parts from
    // counterexample. Narrow down the forall variables part by
    // taking the mid-points of counterexample.
    for (const Variable& forall_var : get_quantified_variables(f_)) {
      contractor_status.mutable_box()[forall_var] =
          safe_mid(counterexample[forall_var], ur);
    }
    contractor_.Prune(&contractor_status, ur);
    if (contractor_status.box().empty()) {
      // If the pruning result is empty, there is nothing more to do. Exit
      // the loop.
      cs->mutable_output().set();
      current_box->set_empty();
      return true;
    } else {
      // Otherwise, we update the current box and keep looping.
      bool changed = false;
      for (int i = 0; i < cs->box().size(); ++i) {
        if (cs->box()[i] != contractor_status.box()[i]) {
          cs->mutable_output().set(i);
          (*current_box)[i] = contractor_status.box()[i];
          changed = true;
        }
      }
      if (!changed) {
        // We reached at a fixed-point.
        return true;
      }
    }
    return false;
  }

  /// Default destructor.
  ~ContractorForall() override = default;

  // Ref: docs/papers/kong-solar-lezama-gao-2018-exists-forall.md — this CE-guided
  // prune loop is Algorithm 2 (∀-clause pruning) of Kong, Solar-Lezama & Gao, CAV 2018;
  // the inner_delta < epsilon < delta levels are its double-sided error control (§3.2).
  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override {
    Box& current_box = cs->mutable_box();
    Config& config_for_counterexample{
        context_for_counterexample_.mutable_config()};
    while (true) {
      // Note that 'DREAL_CHECK_INTERRUPT' is only defined in setup.py,
      // when we build dReal python package.
#ifdef DREAL_CHECK_INTERRUPT
      if (g_interrupted) {
        DREAL_LOG_DEBUG("KeyboardInterrupt(SIGINT) Detected.");
        throw std::runtime_error("KeyboardInterrupt(SIGINT) Detected.");
      }
#endif

      // 1. Find Counterexample.
      for (const Variable& exist_var : current_box.variables()) {
        context_for_counterexample_.SetInterval(exist_var,
                                                current_box[exist_var].lb(),
                                                current_box[exist_var].ub());
      }
      // Alternate the stacking order.
      config_for_counterexample.mutable_stack_left_box_first() =
          !config_for_counterexample.stack_left_box_first();
      optional<Box> counterexample_opt = context_for_counterexample_.CheckSat();
      if (counterexample_opt) {
        Box& counterexample{*counterexample_opt};
        // 1.1. Counterexample found.
        DREAL_LOG_DEBUG("ContractorForall::Prune: Counterexample found:\n{}",
                        counterexample);

        if (config().use_local_optimization()) {
          counterexample = refiner_->Refine(counterexample, ur);
        }
        bool need_to_break_the_loop =
            PruneWithCounterexample(cs, &current_box, counterexample, ur);
        if (need_to_break_the_loop) {
          break;
        }
      } else {
        // 1.2. No counterexample found.
        DREAL_LOG_DEBUG("ContractorForall::Prune: No counterexample found.");
        break;
      }
    }
    cs->AddUsedConstraint(f_);
  }

  std::ostream& display(std::ostream& os) const override {
    return os << "ContractorForall(" << f_ << ")";
  }

 private:
  static Box ExtendBox(Box box, const Variables& vars) {
    for (const Variable& v : vars) {
      box.Add(v);
    }
    return box;
  }

  // Config for the nested counterexample Context: a copy of `config` with
  // --icp-force-parallel cleared. The nested CE solve keeps the IcpSeq
  // dispatch even when the outer solve is forced onto IcpParallel: the flag
  // is top-level parity-measurement scaffolding (icp_parity_gaps.md R3), and
  // recursing it here would both unisolate that A/B and prematurely exercise
  // the nested-CDS surface deferred to R4 (G6). Dispatch choice never moves
  // a verdict (both loops are sound; jobs = 1 IcpParallel is deterministic),
  // so this is experiment isolation — neither SOUNDNESS (false unsat) nor
  // COMPLETENESS (missed refutation) is at stake.
  //
  // The clear MUST happen here, before the Context is constructed — a
  // mutable_config() write in the constructor body is too late: Context
  // eagerly builds its Impl, whose member-init list constructs the
  // TheorySolver, whose constructor consumes use_icp_force_parallel() and
  // freezes the ICP dispatch then and there (Block 15). The body's
  // precision / use_polytope writes work only because those options are read
  // lazily at CheckSat time through TheorySolver's `const Config&` member.
  static Config MakeCounterexampleConfig(const Config& config) {
    Config counterexample_config{config};
    counterexample_config.mutable_use_icp_force_parallel() = false;
    return counterexample_config;
  }

  const Formula f_;                             // ∀X.φ
  const Variables quantified_variables_;        // X
  const Formula strengthend_negated_nested_f_;  // (¬φ)⁻ᵟ¹
  // To compute `B' = Contract(φ(x₁, ..., xₙ, b₁, ..., bₘ), B)`.
  Contractor contractor_;
  // Context to do `Solve(¬φ', δ₂)`.
  mutable ContextType context_for_counterexample_;

  std::unique_ptr<CounterexampleRefiner> refiner_;
};

template <typename ContextType>
class ContractorForallMt : public ContractorCell {
 public:
  /// Deleted default constructor.
  ContractorForallMt() = delete;

  /// Constructs ForallMt contractor using @p f and @p box.
  ContractorForallMt(Formula f, const Box& box, double epsilon,
                     double inner_delta, const Config& config)
      : ContractorCell{Contractor::Kind::FORALL,
                       DynamicBitset(box.size()), config},
        f_{std::move(f)},
        epsilon_{epsilon},
        inner_delta_{inner_delta},
        ctcs_(config.number_of_jobs()) {
    ContractorForall<ContextType>* const ctc{GetCtcOrCreate(box)};
    DREAL_ASSERT(ctc);
    // Build input.
    mutable_input() = ctc->input();
  }

  /// Deleted copy constructor.
  ContractorForallMt(const ContractorForallMt&) = delete;

  /// Deleted move constructor.
  ContractorForallMt(ContractorForallMt&&) = delete;

  /// Deleted copy assign operator.
  ContractorForallMt& operator=(const ContractorForallMt&) = delete;

  /// Deleted move assign operator.
  ContractorForallMt& operator=(ContractorForallMt&&) = delete;

  ~ContractorForallMt() override = default;

  void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override {
    ContractorForall<ContextType>* const ctc{GetCtcOrCreate(cs->box())};
    DREAL_ASSERT(ctc);
    return ctc->Prune(cs, ur);
  }

  std::ostream& display(std::ostream& os) const override {
    return os << "ContractorForall(" << f_ << ")";
  }

 private:
  ContractorForall<ContextType>* GetCtcOrCreate(const Box& box) const {
    return &ctcs_.GetOrCreate([&]() {
      Config inner_config{config()};
      inner_config.mutable_number_of_jobs() = 1;  // FORCE SEQ ICP in INNER LOOP
      return std::make_unique<ContractorForall<ContextType>>(
          f_, box, epsilon_, inner_delta_, inner_config);
    });
  }

  const Formula f_;
  const double epsilon_{};
  const double inner_delta_{};

  // One ContractorForall per worker thread (each holds a nested CE solver).
  mutable PerThread<ContractorForall<ContextType>> ctcs_;
};

template <typename ContextType>
Contractor make_contractor_forall(Formula f, const Box& box, double epsilon,
                                  double inner_delta, const Config& config) {
  if (config.number_of_jobs() > 1) {
    return Contractor{std::make_shared<ContractorForallMt<ContextType>>(
        std::move(f), box, epsilon, inner_delta, config)};
  } else {
    return Contractor{std::make_shared<ContractorForall<ContextType>>(
        std::move(f), box, epsilon, inner_delta, config)};
  }
}

/// Converts @p contractor to ContractorForall.
template <typename ContextType>
std::shared_ptr<ContractorForall<ContextType>> to_forall(
    const Contractor& contractor) {
  DREAL_ASSERT(is_forall(contractor));
  return std::static_pointer_cast<ContractorForall<ContextType>>(
      contractor.ptr_);
}

}  // namespace dreal
