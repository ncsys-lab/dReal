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
#include "dreal/solver/icp_seq.h"

#include <optional>
#include <tuple>
#include <utility>

#include "dreal/solver/brancher.h"
#include "dreal/solver/brancher_smear.h"
#include "dreal/solver/icp_stat.h"
#include "dreal/solver/seed/seed.h"
#include "dreal/util/interrupt.h"
#include "dreal/util/logging.h"

using std::pair;
using std::tie;
using std::vector;

namespace dreal {

IcpSeq::IcpSeq(const Config& config) : Icp{config} {}

bool IcpSeq::CheckSat(const Contractor& contractor,
                      const vector<FormulaEvaluator>& formula_evaluators,
                      ContractorStatus* const cs) {
  // Which child to explore first. ALTERNATES every branch (toggled below) — a
  // parameter-free DFS diversification that is the years-long default (the
  // parallel path alternates too). Why it is load-bearing: DFS commits fully to
  // the first child's whole subtree before backtracking, so a FIXED first-side
  // hugs one wall of the branch tree and can exhaustively grind a witness-free
  // region; BMC unrollings are DEEP, so that region is astronomically large.
  // Alternating zig-zags to diverse deep leaves fast — decisive on SAT, where we
  // stop at the first witness (it only reorders leaf visits, so it can never
  // change soundness or the UNSAT verdict). Removing it blows up deep ODE-BMC SAT
  // like bouncing ball — <1s with, >60s without (~70x). seed-and-verify
  // (--seed-samples) is the robust replacement for branch-order tricks, but it is
  // GATED OFF for ODE/forall, so alternation is the only diversification those
  // families get. It is a parity flip, not a tuned constant (cf. the removed 0.56
  // split-ratio): no knob to overfit. Rationale: docs/decisions.md §"Per-branch
  // alternation". The forall contractor seeds the starting side via
  // config().stack_left_box_first().
  bool explore_left_first{!config().stack_left_box_first()};
  static IcpStat stat{DREAL_LOG_INFO_ENABLED};
  DREAL_LOG_DEBUG("IcpSeq::CheckSat()");
  // Stack of Box x tag (a branched dimension, -1, or kAlreadyPrunedTag — see
  // icp.h). The root box is pushed after the pre-loop root prune below.
  vector<pair<Box, int>> stack;

  // `current_box` always points to the box in the contractor status
  // as a mutable reference.
  Box& current_box{cs->mutable_box()};
  // `current_branching_point` always points to the branching_point in
  // the contractor status as a mutable reference.
  int& current_branching_point{cs->mutable_branching_point()};

  TimerGuard prune_timer_guard(&stat.timer_prune_, stat.enabled(),
                               false /* start_timer */);
  TimerGuard eval_timer_guard(&stat.timer_eval_, stat.enabled(),
                              false /* start_timer */);
  TimerGuard branch_timer_guard(&stat.timer_branch_, stat.enabled(),
                                false /* start_timer */);

  // The entire ICP contraction phase runs under FE_UPWARD (gaol soundness).
  // Establish it ONCE here — not once per Prune — and thread the capability
  // token down through every contractor. This is the phase-hoist that removes
  // the per-Prune fesetround cost. CAPD contractors flip to FE_TONEAREST
  // internally and restore FE_UPWARD on exit; brancher/eval below inherit it.
  const UpwardRoundingScope phase_scope;
  const UpwardRounding ur{phase_scope.token()};

  // Seed-input snapshot: the --seed-samples pre-pass below proposes from the
  // UN-PRUNED root box (IcpSeq's historical seed input, now the canonical one
  // for BOTH loops), so the copy is taken before the root prune. The R3 draft
  // canonicalized on the PRUNED box instead (icp_parity_gaps.md G3) and that
  // regressed a previously-green ∃∀ path catastrophically
  // (MinimizeEqualityElimination.Issue320UnrelatedEqualityHang: <1 s -> >120 s
  // hang at delta=1e-16; isolated 2026-07-22 by varying only this input —
  // nested-CE seeding from the HC4-contracted CE box returns counterexamples
  // that stall the outer CE-guided contraction). COMPLETENESS/termination
  // only — seed choice never moves a verdict.
  std::optional<Box> seed_input;
  if (config().seed_samples() > 0 && AllRelational(formula_evaluators)) {
    seed_input.emplace(cs->box());
  }

  // Root prune — the canonical shape shared with IcpParallel::CheckSat: prune
  // the root box ONCE up front (branching_point = -1: full worklist seed,
  // stated explicitly rather than inherited from the caller's
  // ContractorStatus), so an empty root returns unsat without touching the
  // stack (skipping the seed exploration: every seed box is a sub-box of the
  // infeasible root). The root is then pushed tagged kAlreadyPrunedTag so its
  // pop skips the redundant re-prune (see icp.h).
  current_branching_point = -1;
  prune_timer_guard.resume();
  contractor.Prune(cs, ur);
  prune_timer_guard.pause();
  stat.num_prune_++;
  if (current_box.empty()) {
    DREAL_LOG_DEBUG("IcpSeq::CheckSat() Root box is empty after pruning");
    return false;
  }
  stack.emplace_back(current_box, kAlreadyPrunedTag);

  // --seed-samples seed-and-verify pre-pass. For pure-relational (NRA) theory
  // calls, propose candidate points (LHS sampling or COBYLA) from the
  // un-pruned root snapshot and push a small SOUND box around each so they
  // are explored FIRST (LIFO; the root box stays at the stack bottom). This
  // is a COMPLETENESS-only speed optimization atop the complete search — the
  // cache/recompute carve-out shape, NOT a fallback: the unchanged
  // Prune+EvaluateBox loop is the SOLE arbiter of delta-SAT, so a poor
  // candidate cannot cause a false delta-sat, and the root box at the stack
  // bottom guarantees no subspace is dropped (completeness preserved).
  if (seed_input) {
    for (Box& seed_box :
         SeedBoxes(formula_evaluators, *seed_input, config(), ur)) {
      stack.emplace_back(std::move(seed_box), -1);
    }
  }

  // Constraint-aware smear branching (--smear): assemble the constraint system
  // once (the variable set is fixed for the whole solve — branching only
  // narrows intervals). When off, the configured brancher (largest-first) is
  // used. Sound either way: variable choice never changes a verdict.
  std::optional<SmearBrancher> smear_brancher;
  if (config().smear_variant() != SmearVariant::kNone) {
    smear_brancher.emplace(formula_evaluators, cs->box(),
                           config().smear_variant());
  }

  while (!stack.empty()) {
    DREAL_LOG_DEBUG("IcpSeq::CheckSat() Loop Head");

    // Note that 'DREAL_CHECK_INTERRUPT' is only defined in setup.py,
    // when we build dReal python package.
#ifdef DREAL_CHECK_INTERRUPT
    if (g_interrupted) {
      DREAL_LOG_DEBUG("KeyboardInterrupt(SIGINT) Detected.");
      throw std::runtime_error("KeyboardInterrupt(SIGINT) Detected.");
    }
#endif

    // 1. Pop the current box from the stack
    int entry_tag{};
    tie(current_box, entry_tag) = stack.back();
    stack.pop_back();
    const bool already_pruned{entry_tag == kAlreadyPrunedTag};
    // The tag never enters ContractorStatus (its branching_point domain is -1
    // or a valid dimension).
    current_branching_point = already_pruned ? -1 : entry_tag;

    // 2. Prune the current box. GUARD (pure identity — no contraction): the
    // kAlreadyPrunedTag root was pruned once before being pushed; see icp.h.
    if (!already_pruned) {
      DREAL_LOG_TRACE("IcpSeq::CheckSat() Current Box:\n{}", current_box);
      prune_timer_guard.resume();
      contractor.Prune(cs, ur);
      prune_timer_guard.pause();
      stat.num_prune_++;
      DREAL_LOG_TRACE("IcpSeq::CheckSat() After pruning, the current box =\n{}",
                      current_box);
    }

    if (current_box.empty()) {
      // 3.1. The box is empty after pruning.
      DREAL_LOG_DEBUG("IcpSeq::CheckSat() Box is empty after pruning");
      continue;
    }
    // 3.2. The box is non-empty. Check if the box is still feasible
    // under evaluation and it's small enough.
    eval_timer_guard.resume();
    const optional<DynamicBitset> evaluation_result{
        EvaluateBox(formula_evaluators, current_box, config().precision(), cs,
                    ur)};
    if (!evaluation_result) {
      // 3.2.1. We detect that the current box is not a feasible solution.
      DREAL_LOG_DEBUG(
          "IcpSeq::CheckSat() Detect that the current box is not feasible by "
          "evaluation:\n{}",
          current_box);
      eval_timer_guard.pause();
      continue;
    }
    if (evaluation_result->none()) {
      // 3.2.2. delta-SAT : We find a box which is smaller enough.
      DREAL_LOG_DEBUG("IcpSeq::CheckSat() Found a delta-box:\n{}", current_box);
      return true;
    }
    eval_timer_guard.pause();

    // 3.2.3. This box is bigger than delta. Need branching.
    branch_timer_guard.resume();
    Box box_left;
    Box box_right;
    const int branching_dim =
        smear_brancher
            ? (*smear_brancher)(current_box, *evaluation_result, &box_left,
                                &box_right, ur)
            : config().brancher()(current_box, *evaluation_result, &box_left,
                                  &box_right, ur);
    if (branching_dim >= 0) {
      // Push both children; the one explored first is pushed LAST (LIFO stack).
      if (explore_left_first) {
        stack.emplace_back(box_right, branching_dim);  // bottom
        stack.emplace_back(box_left, branching_dim);   // top -> explored first
      } else {
        stack.emplace_back(box_left, branching_dim);
        stack.emplace_back(box_right, branching_dim);
      }
      explore_left_first = !explore_left_first;  // alternate each branch
    } else {
      DREAL_LOG_DEBUG(
          "IcpSeq::CheckSat() Found that the current box is not satisfying "
          "delta-condition but it's not bisectable.:\n{}",
          current_box);
      // Upstream dreal/dreal4#68: this delta-sat is degenerate — the box
      // still violates the delta-condition but cannot be bisected further.
      // COMPLETENESS hazard (may assert phi^delta T-satisfiable on a
      // T-unsatisfiable phi — missed refutation); warn loudly on stderr,
      // verdict unchanged.
      WarnDegenerateDeltaSat("non-bisectable box below delta", current_box);
      return true;
    }
    branch_timer_guard.pause();
    stat.num_branch_++;
  }
  DREAL_LOG_DEBUG("IcpSeq::CheckSat() No solution");
  return false;
}
}  // namespace dreal
