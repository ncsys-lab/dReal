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
#include "dreal/solver/context_impl.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <dreal/symbolic/prefix_printer.h>
#include <dreal/symbolic/symbolic_formula_cell.h>

#include <fmt/format.h>

#include "dreal/contractor/odes/contractor_odes.h"
#include "dreal/util/pattern_matching/substitutions_map.h"
#include "dreal/version.h"
#include "dreal/solver/auditor.h"
#include "dreal/solver/filter_assertion.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/if_then_else_eliminator.h"
#include "dreal/util/interrupt.h"
#include "dreal/util/logging.h"
#include "dreal/util/nnfizer.h"
#include "dreal/util/rounded_interval.h"
#include "dreal/util/rounding.h"

namespace dreal {

using std::find_if;
using std::isfinite;
using std::ostringstream;
using std::pair;
using std::set;
using std::string;
using std::unordered_set;
using std::vector;

namespace {
// Returns true if @p f contains a `Formula::Forall` (the ∃∀ NRA quantifier,
// `Kind::FORALL`) anywhere in its boolean structure. Note: this is the NRA
// `forall`, NOT the ODE-time `forall_t` (`FormulaKind::ForallT`), which is a
// distinct, supported construct and is intentionally not matched here.
bool HasForall(const Formula& f) {
  if (is_forall(f)) {
    return true;
  }
  if (is_conjunction(f) || is_disjunction(f)) {
    for (const Formula& op : get_operands(f)) {
      if (HasForall(op)) {
        return true;
      }
    }
    return false;
  }
  if (is_negation(f)) {
    return HasForall(get_operand(f));
  }
  return false;
}

// dReal's ∃∀ machinery (ContractorForall / ForallFormulaEvaluator) supports a
// `forall` only as a top-level POSITIVE literal with a quantifier-free body.
// Any other occurrence — negated (`¬∀`, which the SAT layer can also produce by
// assigning a forall-Boolean to false), nested (`∀∀`), or buried under a
// sign-flipping disjunction — is unsupported and would otherwise crash with an
// opaque error deep in contractor construction (the `ibex_converter` /
// `DeltaStrengthen` VisitForall throws). Reject it here, early and clearly. @p f
// is one top-level conjunct (Context::Impl::Assert has already split conjunctions).
void RejectUnsupportedForall(const Formula& f) {
  if (is_forall(f)) {
    if (HasForall(get_quantified_formula(f))) {
      throw DREAL_RUNTIME_ERROR(
          "dReal only supports a top-level positive forall (exists-forall); a "
          "nested forall is unsupported: {}",
          f);
    }
    return;
  }
  if (HasForall(f)) {
    throw DREAL_RUNTIME_ERROR(
        "dReal only supports a top-level positive forall (exists-forall); this "
        "negated/nested/disjoined forall is unsupported: {}",
        f);
  }
}

// A user-asserted positive (forall_t …) is enforced ONLY by being attached to
// a companion positive (integral …) — same flow, invariant over the integral's
// endpoint (vec_t) variables (forallt_links_to_integral). One that can link to
// NO asserted integral is silently omitted by link_integral_invariants, so the
// "invariant" constrains nothing and the query returns a spurious delta-sat —
// COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed
// refutation), never false unsat. It bit simulink-to-dreal as BUG-010: an
// invariant written over the flow-template var x instead of the endpoint var
// x_t. Malformed input ⇒ fail loud here instead.
//
// This rejection cannot live in link_integral_invariants itself: that runs
// inside the DPLL(T) loop on the SAT solver's transient literal subset, where
// a positive forall_t legitimately appears without its companion integral
// (another mode/step is active — throwing there crashes the github airplane
// BMC family). Only here, against the full assertion stack, is "links to NO
// integral anywhere" distinguishable from a transient search state. A forall_t
// nested under a disjunction/negation is not collected by the conjunction
// traversal and keeps the documented BUG-002 silent-drop behavior (design
// axes 1–2 in test/dreal/smt2/test/dreal_future.cc).
void RejectUnlinkedForallT(const ScopedVector<Formula>& stack) {
  vector<Formula> int_ctrs;
  vector<Formula> inv_ctrs;
  for (const auto& a : stack.get_vector()) {
    for (const auto& f : unroll_conjunctions(a)) {
      if (is_integral(f)) int_ctrs.push_back(f);
      else if (is_forallT(f)) inv_ctrs.push_back(f);
    }
  }
  for (const auto& fc : inv_ctrs) {
    const bool linked =
        std::any_of(int_ctrs.begin(), int_ctrs.end(), [&fc](const Formula& ic) {
          return forallt_links_to_integral(fc, ic);
        });
    if (!linked) {
      throw DREAL_RUNTIME_ERROR(
          "(forall_t …) invariant cannot be linked to any asserted "
          "(integral …) and would be silently ignored: {}. It needs a "
          "companion integral over the same flow, with the invariant written "
          "over that integral's endpoint variables (e.g. x_t, not the "
          "flow-template variable x).",
          fc);
    }
  }
}

// Point-quantifier elimination. A positive top-level `forall` whose binder pins
// a universal variable y to a single point c (lb == ub) is degenerate there:
// dReal soundly over-approximates the strict domain-negation `(y>c) ∨ (y<c)`
// with closed intervals, which cannot refute it at the measure-zero boundary, so
// the forall would be reported vacuously consistent (a false `delta-sat` — a
// COMPLETENESS gap, never a false `unsat`). We remove the point quantifier by
// substituting `y := c` into the matrix; an all-point forall reduces to its
// quantifier-free body. @p f is a positive top-level forall (already validated).
// Returns @p f unchanged when no universal variable is point-pinned.
Formula EliminatePointUniversals(const Formula& f) {
  const Variables& qvars{get_quantified_variables(f)};
  const Formula& matrix{get_quantified_formula(f)};
  // The binder bounds are top-level conjuncts of NNF(¬matrix); absorb the ones
  // that constrain only universal variables into a universal box.
  Box ubox{vector<Variable>{qvars.begin(), qvars.end()}};
  const Formula neg{Nnfizer{}.Convert(!matrix, true)};
  const auto absorb = [&ubox, &qvars](const Formula& c) {
    if (c.GetFreeVariables().IsSubsetOf(qvars)) {
      FilterAssertion(c, &ubox);
    }
  };
  if (is_conjunction(neg)) {
    for (const Formula& c : get_operands(neg)) {
      absorb(c);
    }
  } else {
    absorb(neg);
  }
  // Substitute every point-pinned universal variable; collect the survivors.
  Formula result{matrix};
  Variables surviving;
  bool any_eliminated{false};
  for (const Variable& y : qvars) {
    const Box::Interval& iv{ubox[y]};
    if (iv.lb() == iv.ub()) {
      result = result.Substitute(y, Expression{iv.lb()});
      any_eliminated = true;
    } else {
      surviving.insert(y);
    }
  }
  if (!any_eliminated) {
    return f;
  }
  if (surviving.empty()) {
    return result;  // all universals point-pinned → quantifier-free body
  }
  return forall(surviving, result);  // partial elimination
}

// The --refine-witness post-pass on a delta-sat box: shrink it to a
// delta-tight, definite-valued witness. Without the flag this never runs and
// the box is reported VERBATIM — it is exactly the region ICP certified,
// idempotent under re-feeding; collapsing it is a downstream presentation
// choice that would destroy the certified-region information (a don't-care
// Boolean, never assigned by the SAT layer, prints as its whole
// [false, true] interval like any other dim).
//
// Under the flag: a don't-care BOOLEAN/BINARY dim pins to true; a
// CONTINUOUS/INTEGER dim wider than delta shrinks to its midpoint ±delta/2 —
// sound post-hoc because EvaluateBox's certificate is an interval evaluation
// over the whole box, and interval evaluation is inclusion-monotone, so
// every sub-box (the midpoint slice included) inherits the delta-certificate.
//
// A dim of an ODE atom (@p ode_vars) is never shrunk, even under the flag:
// the tube contractor's certificate is not inclusion-monotone (a sub-box of
// a surviving box can be refutable), so a midpoint slice can exclude every
// real solution — the reported model then FAILS when re-fed as bounds
// (BUG-011: tau reported [0.437, 0.438] when the sole solution was 0.38,
// re-feed unsat). Under --refine-witness the honest OdeFormulaEvaluator has
// already branched positive ODE atoms' dims below delta, so the exemption is
// a no-op for them; keeping it makes the idempotence invariant structural
// rather than a coupling to the evaluator (a negated ODE atom's dims are
// never branched).
void Tighten(Box* box, const double delta, const Variables& ode_vars) {
  // This runs as delta-sat post-processing from Context::Impl::CheckSat(),
  // whose ambient FPU mode is undefined (whatever CheckSatCore left — and once
  // CAPD is linked, that is FE_TONEAREST). safe_diam/safe_mid and the gaol `&=`
  // below are all gaol computations sound only under FE_UPWARD, so establish it
  // for the whole pass and mint the token the directed-rounding helpers need.
  const UpwardRoundingScope round_scope;
  const UpwardRounding ur{round_scope.token()};
  for (int i = 0; i < box->size(); ++i) {
    auto& interval = (*box)[i];
    if (safe_diam(interval, ur) > delta) {
      const Variable& var{box->variable(i)};
      switch (var.get_type()) {
        case Variable::Type::BINARY:
        case Variable::Type::BOOLEAN:
          // Always pick True (1.0)
          interval = 1.0;
          break;
        case Variable::Type::CONTINUOUS: {
          if (ode_vars.include(var)) continue;
          // Sound outward-rounded [mid - delta/2, mid + delta/2]. The previous
          // hand-built `Box::Interval(mid - half, mid + half)` was mis-rounded
          // under every single rounding mode (the lower endpoint pulled inward,
          // yielding a too-narrow box). The typed directed-rounding helpers make
          // the outward rounding explicit and compiler-checked; see
          // util/rounded_interval.h.
          const Exact mid{safe_mid(interval, ur)};
          const Exact half_delta{Exact{delta}.half()};
          interval &= make_sound_interval(sub_down(mid, half_delta, ur),
                                          add_up(mid, half_delta, ur));
        } break;
        case Variable::Type::INTEGER: {
          if (ode_vars.include(var)) continue;
          // std::trunc truncates toward zero independent of the rounding mode
          // (it is exact); only the safe_mid() is FE_UPWARD-sensitive. trunc,
          // not static_cast<int>: the Int domain spans +/-2^53
          // (dreal/dreal4#284), and an int cast is UB above 2^31.
          interval = std::trunc(safe_mid(interval, ur));
        } break;
      }
    }
  }
}

bool ParseBooleanOption(const string& key, const string& val) {
  if (val == "true") {
    return true;
  }
  if (val == "false") {
    return false;
  }
  throw DREAL_RUNTIME_ERROR("Unknown value {} is provided for option {}", val,
                            key);
}
}  // namespace

Context::Impl::Impl() : Impl{Config{}} {}

Context::Impl::Impl(Config config)
    : config_{std::move(config)},
      sat_solver_{config_},
      theory_solver_{config_} {
  boxes_.push_back(Box{});
}

void Context::Impl::Assert(const Formula& f) {
  if (is_true(f)) {
    return;
  }
  if (box().empty()) {
    return;
  }
  if (is_false(f)) {
    box().set_empty();
    return;
  }
  if (is_conjunction(f)) {  // because otherwise FilterAssertion may miss some box updates!
    for (const auto& operand : get_operands(f)) Assert(operand);
    return;
  }
  // Reject an unsupported `forall` occurrence early and clearly, before it
  // reaches (and crashes) contractor construction.
  RejectUnsupportedForall(f);
  // Eliminate any point-pinned universal quantifier and re-assert the simplified
  // form (which may now be quantifier-free or a smaller forall).
  if (is_forall(f)) {
    const Formula eliminated{EliminatePointUniversals(f)};
    if (!eliminated.EqualTo(f)) {
      Assert(eliminated);
      return;
    }
  }
  // if (true) {
  if (!FilterAssertion(f, &box()).filtered) {
    DREAL_LOG_DEBUG("ContextImpl::Assert: {} is added.", f);
    IfThenElseEliminator ite_eliminator;
    const Formula no_ite{ite_eliminator.Process(f)};
    for (const Variable& ite_var : ite_eliminator.variables()) {
      // Note that the following does not mark `ite_var` as a model variable.
      AddToBox(ite_var);
    }
    const Formula normalized{pn_.Convert(no_ite)};
    stack_.push_back(normalized);
    sat_solver_.AddFormula(normalized);
    return;
  } else {
    DREAL_LOG_DEBUG("ContextImpl::Assert: {} is not added.", f);
    DREAL_LOG_DEBUG("Box=\n{}", box());
    return;
  }
}

void drpm_benchmark_log(
  const bool fully_constrained_model,
  const double sat_solve_elapsed_ms,
  const double theory_solve_elapsed_ms,
  const unsigned lemma_size,
  const char mode,
  const double pm_elapsed_ms,
  const unsigned num_pm,
#if CAV26_FILTER_SYMMETRIES
  const unsigned CAV26_pm_num_not_pure_time,
  const unsigned CAV26_pm_num_not_pure_logic,
  const unsigned CAV26_pm_num_not_pure_any,
#endif
  std::ostream& out
) {
  out << ".\t";
  out << " S.FCM " << fully_constrained_model;
  out << " S.ms " << sat_solve_elapsed_ms;
  out << " T.ms " << theory_solve_elapsed_ms;
  out << '\t';
  out << " L " << lemma_size;
  out << ' ' << mode;
  out << "\t";
  out << " PM.ms " << pm_elapsed_ms;
  out << " PM " << num_pm;
#if CAV26_FILTER_SYMMETRIES
  out << '\t';
  out << " C26.npT " << CAV26_pm_num_not_pure_time;
  out << " C26.npL " << CAV26_pm_num_not_pure_logic;
  out << " C26.np* " << CAV26_pm_num_not_pure_any;
#endif
  out << '\n';
}

optional<Box> Context::Impl::CheckSatCore(const ScopedVector<Formula>& stack,
                                          Box box,
                                          SatSolver* const sat_solver) {
  DREAL_LOG_DEBUG("ContextImpl::CheckSatCore()");
  DREAL_LOG_TRACE("ContextImpl::CheckSat: Box =\n{}", box);
  if (box.empty()) return {};
  // If false ∈ stack, it's UNSAT.
  for (const auto& f : stack.get_vector()) if (is_false(f)) return {};
  // If stack = ∅ or stack = {true}, it's trivially SAT.
  if (stack.empty() || (stack.size() == 1 && is_true(stack.first()))) {
    DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Found Model\n{}", box);
    return box;
  }
  DREAL_LOG_INFO("Initialized. Beginning SAT <=> Theory cycles.");
  std::cerr << std::setprecision(3);
  while (true) {
    // Note that 'DREAL_CHECK_INTERRUPT' is only defined in setup.py,
    // when we build dReal python package.
#ifdef DREAL_CHECK_INTERRUPT
    if (g_interrupted) {
      DREAL_LOG_DEBUG("KeyboardInterrupt(SIGINT) Detected.");
      throw std::runtime_error("KeyboardInterrupt(SIGINT) Detected.");
    }
#endif

    const bool request_fully_constrained =
      DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS ||
      recent_under_constrained_deltasat > 0;

    /* ################################ START MEASURING TIME ################################ */
    const auto scs_start = std::chrono::high_resolution_clock::now();
    const auto optional_model_and_fully_constrained = sat_solver->CheckSat(request_fully_constrained);
    const auto scs_end = std::chrono::high_resolution_clock::now();
    const std::chrono::duration<double, std::milli> scs_elapsed_ms = scs_end - scs_start;
    /* ################################ STOP MEASURING TIME ################################ */

    if (optional_model_and_fully_constrained) {
      const auto& [optional_model, is_full_constrained] = *optional_model_and_fully_constrained;
      if (request_fully_constrained) DREAL_ASSERT(is_full_constrained);
      if (DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS) DREAL_ASSERT(request_fully_constrained && is_full_constrained);

      const vector<pair<Variable, bool>>& boolean_model{optional_model.first};
      const vector<pair<Variable, bool>>& theory_model{optional_model.second};

      for (const auto& [sat_var, assignment] : boolean_model)
        box[sat_var] = assignment ? 1.0 : 0.0;  // true -> 1.0 and false -> 0.0

      if (!theory_model.empty()) {
        // SAT from SATSolver.
        DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Sat Check = SAT");

        vector<Formula> assertions;
        assertions.reserve(theory_model.size());
        for (const auto& [sat_var, assignment] : theory_model)
          assertions.emplace_back(
            assignment ? sat_solver->theory_literal(sat_var) : !sat_solver->theory_literal(sat_var)
          );
        std::sort(assertions.begin(), assertions.end(), [](const Formula& a, const Formula& b) {
          return a.GetFreeVariables().size() < b.GetFreeVariables().size(); // ascending
        });

        /* ################################ START MEASURING TIME ################################ */
        const auto tcs_start = std::chrono::high_resolution_clock::now();
        const auto tscs_result = theory_solver_.CheckSat(box, assertions);
        const auto tcs_end = std::chrono::high_resolution_clock::now();
        const std::chrono::duration<double, std::milli> tcs_elapsed_ms = tcs_end - tcs_start;
        /* ################################ STOP MEASURING TIME ################################ */

        if (tscs_result && !is_full_constrained) {
          // SAT from TheorySolver.
          // the next 3 unsats should be fully constrained before we can try under constrained stuff again.
          // or, we get a fully constrained deltasat, in which case we are done :)
          recent_under_constrained_deltasat = recent_under_constrained_deltasat_limit;
          DREAL_LOG_WARN("ContextImpl::CheckSatCore() - Underconstrained Theory Check = delta-SAT. Exponential Backoff = {}", recent_under_constrained_deltasat_limit);
          // todo: log scs_elapsed_ms, tcs_elapsed_ms
          recent_under_constrained_deltasat_limit *= 2; // exponential backoff
          return CheckSatCore(stack, std::move(box), sat_solver);
        } else if (tscs_result && is_full_constrained) {
          DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Fully Constrained Theory Check = delta-SAT");
          return theory_solver_.GetModel();
        } else {
          // UNSAT from TheorySolver.
          if (recent_under_constrained_deltasat > 0) recent_under_constrained_deltasat--;

          DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Theory Check = UNSAT");
          std::vector explanation(
            theory_solver_.GetExplanation().begin(),
            theory_solver_.GetExplanation().end()
          );
          DREAL_LOG_DEBUG(
              "ContextImpl::CheckSatCore() - size of explanation = {} - stack size = {}",
              explanation.size(), stack.get_vector().size());

          // ordering the literals like this makes pattern matching fast.
          // todo: abstract this away better. should not happen at the top-level like it is now.
          std::sort(explanation.begin(), explanation.end(), [](const Formula &a, const Formula &b) {
              return a.GetFreeVariables().size() > b.GetFreeVariables().size(); // descending
          });

          if (explanation.size() < config().drpm_max_size()
            /*&& explanation.size() < 384 /* stack overflows around size=960 on x86 #1#
            && tscs_elapsed > std::chrono::milliseconds(3)*/) {
            /* ################################ START MEASURING TIME ################################ */
#define TO_MICROS(x) ( std::chrono::duration_cast<std::chrono::microseconds>((x)) )
            auto pm_timeout = TO_MICROS(scs_elapsed_ms + tcs_elapsed_ms);
            pm_timeout += std::min( // for edge-case of very, very long-running lemmas. try HARD to match those.
              99 * pm_timeout, // `+=`, so 100
              TO_MICROS(config().drpm_max_time())
            );
            const auto pm_start = std::chrono::high_resolution_clock::now();
            const auto pm_result = sat_solver->AddLearnedClausePattern(pn_, explanation, box, pm_timeout);
            const auto pm_end = std::chrono::high_resolution_clock::now();
            const std::chrono::duration<double, std::milli> pm_elapsed = pm_end - pm_start;
            /* ################################ STOP MEASURING TIME ################################ */

            // The pattern matching may time out. At the very minimum, make sure the original at least gets inserted.
            sat_solver->AddLearnedClauseDirect(explanation, box);

            drpm_benchmark_log(
              is_full_constrained, scs_elapsed_ms.count(), tcs_elapsed_ms.count(), explanation.size(),
              'M',
              pm_elapsed.count(), pm_result.matches,
#if CAV26_FILTER_SYMMETRIES
              pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_TIME),
              pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_LOGIC),
              pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_ANY),
#endif
              std::cerr
            );
          }
          else {
            if (DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED) pm_dump_all(explanation, {}, {}, {});
            sat_solver->AddLearnedClauseDirect(explanation, box);
            drpm_benchmark_log(
              is_full_constrained, scs_elapsed_ms.count(), tcs_elapsed_ms.count(), explanation.size(),
              'A',
              0 /*pm_elapsed.count()*/, 0 /*pm_result.matches*/,
#if CAV26_FILTER_SYMMETRIES
              0 /*pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_TIME)*/,
              0 /*pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_LOGIC)*/,
              0 /*pm_result.misses_bc.at(substitutions_map::substitution_status::CAV26_NOT_PURE_ANY)*/,
#endif
              std::cerr
            );
          }

          if (DREAL_LOG_TRACE_ENABLED) {
            for (const auto& f_i : stack.get_vector())
              DREAL_LOG_TRACE("ContextImpl::CheckSatCore: Stack {}", f_i);
            for (const auto& f_i : explanation)
              DREAL_LOG_TRACE("ContextImpl::CheckSatCore: Explanation {}", f_i);
          }
        }
      } else /* theory_model.empty() */ return box;
    } else /* !optional_model */ {
      // UNSAT from SATSolver. Escape the loop.
      DREAL_LOG_DEBUG("ContextImpl::CheckSatCore() - Sat Check = UNSAT");
      return {};
    }
  }
}

optional<Box> Context::Impl::CheckSat() {
  RejectUnlinkedForallT(stack_);
  auto result = CheckSatCore(stack_, box(), &sat_solver_);
  if (result) {
    // Delta-sat: the model is the raw terminating box. --refine-witness opts
    // into the Tighten post-pass (see its contract); dims occurring in ODE
    // atoms are exempt from the shrink.
    if (config_.refine_witness()) {
      Variables ode_vars;
      for (const Formula& f : stack_.get_vector()) {
        if (f.include_ode()) ode_vars.insert(f.GetFreeVariables());
      }
      Tighten(&(*result), config_.precision(), ode_vars);
    }
    DREAL_LOG_DEBUG("ContextImpl::CheckSat() - Found Model\n{}", *result);
    model_ = ExtractModel(*result);
    return model_;
  } else {
    model_.set_empty();
    return result;
  }
}

void Context::Impl::AddToBox(const Variable& v) {
  DREAL_LOG_TRACE("ContextImpl::AddToBox({})", fmt::streamed(v));
  const auto& variables = box().variables();
  if (find_if(variables.begin(), variables.end(), [&v](const Variable& v_) {
        return v.equal_to(v_);
      }) == variables.end()) {
    // v is not in box.
    box().Add(v);
  }
}

void Context::Impl::DeclareVariable(const Variable& v,
                                    const bool is_model_variable) {
  DREAL_LOG_TRACE("ContextImpl::DeclareVariable({})", fmt::streamed(v));
  AddToBox(v);
  if (is_model_variable) {
    mark_model_variable(v);
  }
}

void Context::Impl::SetDomain(const Variable& v, const Expression& lb,
                              const Expression& ub) {
  const double lb_fp =
      is_real_constant(lb) ? get_lb_of_real_constant(lb) : lb.Evaluate();
  const double ub_fp =
      is_real_constant(ub) ? get_ub_of_real_constant(ub) : ub.Evaluate();
  SetInterval(v, lb_fp, ub_fp);
}

void Context::Impl::Minimize(const vector<Expression>& functions) {
  // Given objective functions f₁(x), ... fₙ(x) and the current
  // constraints ϕᵢ which involves x. this method encodes them into a
  // universally quantified formula ψ:
  //
  //    ψ = ∀y. (⋀ᵢ ϕᵢ(y)) → (f₁(x) ≤ f₁(y) ∨ ... ∨ fₙ(x) ≤ fₙ(y))
  //      = ∀y. ¬(⋀ᵢ ϕᵢ(y)) ∨ (f₁(x) ≤ f₁(y) ∨ ... ∨ fₙ(x) ≤ fₙ(y))
  //      = ∀y. (∨ᵢ ¬ϕᵢ(y)) ∨ (f₁(x) ≤ f₁(y) ∨ ... ∨ fₙ(x) ≤ fₙ(y)).
  //
  // Here we introduce existential variables zᵢ for fᵢ(x) to have
  //
  //    ψ =  ∃z₁...zₙ. (z₁ = f₁(x) ∧ ... ∧ zₙ = fₙ(x)) ∧
  //              [∀y. (∨ᵢ ¬ϕᵢ(y)) ∨ (z₁ ≤ f₁(y) ∨ ... ∨ zₙ ≤ fₙ(y))].
  //
  // Note that when we have more than one objective function, this
  // encoding scheme denotes Pareto optimality.
  //
  // To construct ϕᵢ(y), we need to traverse both `box()` and
  // `stack_` because some of the asserted formulas are translated
  // and applied into `box()`.
  set<Formula> set_of_negated_phi;  // Collects ¬ϕᵢ(y).
  ExpressionSubstitution subst;  // Maps xᵢ ↦ yᵢ to build f(y₁, ..., yₙ).
  Variables quantified_variables;  // {y₁, ... yₙ}
  Variables x_vars;
  for (const Expression& f : functions) {
    x_vars += f.GetVariables();
  }

  // Collects side-constraints related to the cost functions.
  unordered_set<Formula> constraints;
  bool keep_going = true;
  while (keep_going) {
    keep_going = false;
    for (const Formula& constraint : stack_) {
      if (constraints.find(constraint) == constraints.end() &&
          HaveIntersection(x_vars, constraint.GetFreeVariables())) {
        x_vars += constraint.GetFreeVariables();
        constraints.insert(constraint);
        keep_going = true;
      }
    }
  }

  for (const Variable& x_i : x_vars) {
    // We add postfix '_' to name y_i
    const Variable y_i{x_i.get_name() + "_", x_i.get_type()};
    quantified_variables.insert(y_i);
    subst.emplace(x_i, y_i);
    // If `box()[x_i]` has a finite bound, let's add that information in
    // set_of_negated_phi as a constraint on y_i.
    const Box::Interval& bound_on_x_i{box()[x_i]};
    const double lb_x_i{bound_on_x_i.lb()};
    const double ub_x_i{bound_on_x_i.ub()};
    if (isfinite(lb_x_i)) {
      set_of_negated_phi.insert(!(lb_x_i <= y_i));
    }
    if (isfinite(ub_x_i)) {
      set_of_negated_phi.insert(!(y_i <= ub_x_i));
    }
  }

  for (const Formula& constraint : constraints) {
    set_of_negated_phi.insert(!constraint.Substitute(subst));
  }

  Formula quantified{make_disjunction(set_of_negated_phi)};  // ∨ᵢ ¬ϕᵢ(y)
  Formula new_z_block;  // This will have (z₁ = f₁(x) ∧ ... ∧ zₙ = fₙ(x)).
  static int counter{0};
  for (const Expression& f_i : functions) {
    const Variable z_i{fmt::format("Z{}", counter++),
                       Variable::Type::CONTINUOUS};
    AddToBox(z_i);
    new_z_block = new_z_block && (z_i == f_i);
    quantified = quantified || (z_i <= f_i.Substitute(subst));
  }
  const Formula psi{new_z_block && forall(quantified_variables, quantified)};
  return Assert(psi);
}

void Context::Impl::Pop() {
  DREAL_LOG_DEBUG("ContextImpl::Pop()");
  stack_.pop();
  boxes_.pop();
  sat_solver_.Pop();
}

void Context::Impl::Push() {
  DREAL_LOG_DEBUG("ContextImpl::Push()");
  sat_solver_.Push();
  boxes_.push();
  boxes_.push_back(boxes_.last());
  stack_.push();
}

void Context::Impl::SetInfo(const string& key, const double val) {
  DREAL_LOG_DEBUG("ContextImpl::SetInfo({} ↦ {})", key, val);
  info_[key] = fmt::format("{}", val);
}

void Context::Impl::SetInfo(const string& key, const string& val) {
  DREAL_LOG_DEBUG("ContextImpl::SetInfo({} ↦ {})", key, val);
  info_[key] = val;
}

void Context::Impl::SetInterval(const Variable& v, const double lb,
                                const double ub) {
  DREAL_LOG_DEBUG("ContextImpl::SetInterval({} = [{}, {}])", fmt::streamed(v), lb, ub);
  box()[v] = Box::Interval(lb, ub);
}

void Context::Impl::SetLogic(const Logic& logic) {
  DREAL_LOG_DEBUG("ContextImpl::SetLogic({})", logic);
  logic_ = logic;
}

void Context::Impl::SetOption(const string& key, const double val) {
  DREAL_LOG_DEBUG("ContextImpl::SetOption({} ↦ {})", key, val);
  option_[key] = fmt::format("{}", val);

  if (key == ":precision") {
    if (val <= 0.0) {
      throw DREAL_RUNTIME_ERROR("Precision has to be positive (input = {}).",
                                val);
    }
    return config_.mutable_precision().set_from_file(val);
  }
  if (key == ":forall-pre-prune-prec") {
    if (val <= 0.0) {
      throw DREAL_RUNTIME_ERROR(
          "forall-pre-prune-prec has to be positive (input = {}).", val);
    }
    return config_.mutable_forall_pre_prune_prec().set_from_file(val);
  }
}

optional<string> Context::Impl::GetOption(const string& key) const {
  DREAL_LOG_DEBUG("ContextImpl::GetOption({})");
  const auto it = option_.find(key);
  if (it != option_.end()) {
    return it->second;
  }
  // Handle :precision as a special case.
  if (key == ":precision") {
    return fmt::format("{}", config_.precision());
  }
  return nullopt;
}

void Context::Impl::SetOption(const string& key, const string& val) {
  DREAL_LOG_DEBUG("ContextImpl::SetOption({} ↦ {})", key, val);
  option_[key] = val;
  if (key == ":polytope") {
    return config_.mutable_use_polytope().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":forall-polytope" || key == "forall_polytope") {
    return config_.mutable_use_polytope_in_forall().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":forall-pre-prune" || key == "forall_pre_prune") {
    return config_.mutable_use_forall_pre_prune().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":local-optimization" || key == "local_optimization") {
    return config_.mutable_use_local_optimization().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":worklist-fixpoint" || key == ":worklist_fixpoint") {
    return config_.mutable_use_worklist_fixpoint().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":produce-models" || key == ":produce_models") {
    return config_.mutable_produce_models().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":visualize") {
    return config_.mutable_visualize().set_from_file(
        ParseBooleanOption(key, val));
  }
  if (key == ":smtlib2-compliant" || key == ":smtlib2_compliant") {
    return config_.mutable_smtlib2_compliant().set_from_file(
        ParseBooleanOption(key, val));
  }
}

Box Context::Impl::ExtractModel(const Box& box) const {
  if (static_cast<int>(model_variables_.size()) == box.size()) {
    // Every variable is a model variable. Simply return the @p box.
    return box;
  }
  Box new_box;
  for (const Variable& v : box.variables()) {
    if (is_model_variable(v)) {
      new_box.Add(v, box[v].lb(), box[v].ub());
    }
  }
  return new_box;
}

bool Context::Impl::is_model_variable(const Variable& v) const {
  return (model_variables_.find(v.get_id()) != model_variables_.end());
}

void Context::Impl::mark_model_variable(const Variable& v) {
  model_variables_.insert(v.get_id());
}

const ScopedVector<Formula>& Context::Impl::assertions() const {
  return stack_;
}

}  // namespace dreal
