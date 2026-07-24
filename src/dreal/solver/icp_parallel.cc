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
#include "dreal/solver/icp_parallel.h"

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

#include "dreal/solver/brancher.h"
#include "dreal/solver/brancher_abs.h"
#include "dreal/solver/brancher_smear.h"
#include "dreal/solver/icp_stat.h"
#include "dreal/solver/seed/seed.h"
#include "dreal/util/assert.h"
#include "dreal/util/cds.h"
#include "dreal/util/interrupt.h"
#include "dreal/util/logging.h"

using std::atomic;
using std::make_unique;
using std::pair;
using std::unique_ptr;
using std::vector;

namespace dreal {

namespace {

bool ParallelBranch(const SmearBrancher* const smear_brancher,
                    const BrancherAbs* const abs_brancher,
                    const Config::Brancher& fallback_brancher,
                    const DynamicBitset& bitset,
                    const bool stack_left_box_first, Box* const box,
                    int* const branching_point,
                    Stack<pair<Box, int>>* const global_stack,
                    atomic<int>* const number_of_boxes,
                    const UpwardRounding& ur) {
  // Constraint-aware smear / activity-based ABS (when enabled for this
  // worker) vs the configured fallback brancher; all three share the brancher
  // interface (dimension + left/right out-params). At most one of smear/abs
  // is non-null (the Icp base ctor rejects a Config with both set). The
  // fallback is config.brancher() — the API-settable Config slot (parity gap
  // G2, icp_parity_gaps.md R2). Its default IS BranchLargestFirst; a custom
  // Config::mutable_brancher() is honored at every job count.
  Box box_left;
  Box box_right;
  const int branching_dim{
      smear_brancher
          ? (*smear_brancher)(*box, bitset, &box_left, &box_right, ur)
      : abs_brancher
          ? (*abs_brancher)(*box, bitset, &box_left, &box_right, ur)
          : fallback_brancher(*box, bitset, &box_left, &box_right, ur)};
  if (branching_dim < 0) {
    // Fail to find a branching point.
    return false;
  }
  const Box& to_stack{stack_left_box_first ? box_left : box_right};
  const Box& to_keep{stack_left_box_first ? box_right : box_left};
  number_of_boxes->fetch_add(1, std::memory_order_relaxed);
  // Both children carry the branched dimension — the pushed child inside its
  // stack entry (read back on pop), the kept child through *branching_point
  // (the ContractorStatus field the next Prune reads) — so
  // ContractorWorklistFixpoint seeds only the contractors whose inputs
  // depend on the branched dimension: the incremental-repruning
  // optimization (icp_parity_gaps.md G1/R1).
  global_stack->emplace(to_stack, branching_dim);
  *box = to_keep;
  *branching_point = branching_dim;
  return true;
}

void Worker(const Contractor& contractor, const Config& config,
            const vector<FormulaEvaluator>& formula_evaluators,
            const SmearBrancher* const smear_brancher,
            BrancherAbs* const abs_brancher, const int id,
            const bool main_thread,
            Stack<pair<Box, int>>* const global_stack,
            ContractorStatus* const cs, atomic<int>* const found_delta_sat,
            atomic<int>* const number_of_boxes) {
  thread_local IcpStat stat{DREAL_LOG_INFO_ENABLED, id};
  TimerGuard prune_timer_guard(&stat.timer_prune_, stat.enabled(),
                               false /* start_timer */);
  TimerGuard eval_timer_guard(&stat.timer_eval_, stat.enabled(),
                              false /* start_timer */);
  TimerGuard branch_timer_guard(&stat.timer_branch_, stat.enabled(),
                                false /* start_timer */);

  // libcds attach for the lock-free global_stack. Pool workers attach here;
  // the main thread is attached by CheckSat's static CdsInit. Nested-CE
  // safety (icp_parity_gaps.md G6): a nested CheckSat on this thread re-runs
  // Worker, but a block-scope thread_local is initialized only on the FIRST
  // pass per thread, so no second guard (and no double attach/detach) is
  // created. Even a hypothetical re-attach would be safe: libcds refcounts
  // attachThread (ThreadData::init() bumps m_nAttachCount and only attaches
  // at 0 -> 1; detachThread only detaches at 1 -> 0 — vendored
  // libcds/src/thread_data.cpp).
  thread_local CdsScopeGuard cds_scope_guard(!main_thread);

  // Which child to keep in hand (explored first) vs push. ALTERNATES every
  // branch (toggled below) — a parameter-free DFS diversification that is the
  // years-long default. Why it is load-bearing: DFS commits fully to the
  // first child's whole subtree before backtracking, so a FIXED first-side
  // hugs one wall of the branch tree and can exhaustively grind a
  // witness-free region; BMC unrollings are DEEP, so that region is
  // astronomically large. Alternating zig-zags to diverse deep leaves fast —
  // decisive on SAT, where we stop at the first witness (it only reorders
  // leaf visits, so it can never change soundness or the UNSAT verdict).
  // Removing it blows up deep ODE-BMC SAT like bouncing ball — <1s with,
  // >60s without (~70x). seed-and-verify (--seed-samples) is the robust
  // replacement for branch-order tricks, but it is GATED OFF for ODE/forall,
  // so alternation is the only diversification those families get. It is a
  // parity flip, not a tuned constant (cf. the removed 0.56 split-ratio): no
  // knob to overfit. Rationale: docs/decisions.md §"Per-branch alternation".
  // The forall contractor seeds the starting side via
  // config.stack_left_box_first().
  bool stack_left_box_first{config.stack_left_box_first()};

  // `current_box` always points to the box in the contractor status
  // as a mutable reference.
  Box& current_box{cs->mutable_box()};
  // `current_branching_point` always points to the branching_point in
  // the contractor status as a mutable reference.
  int& current_branching_point{cs->mutable_branching_point()};

  // When this flag is true, we need to pop a box from the stack. Otherwise, it
  // indicates that we can work with the box inside of the ContractorStatus.
  bool need_to_pop{true};

  // This worker runs the ICP contraction phase under FE_UPWARD (gaol
  // soundness). FPU mode is thread-local, so each worker establishes its own
  // scope ONCE and threads the capability token to every Prune (the
  // phase-hoist that removes per-Prune fesetround). CAPD flips internally.
  const UpwardRoundingScope phase_scope;
  const UpwardRounding ur{phase_scope.token()};

  while ((*found_delta_sat == -1) &&
         (number_of_boxes->load(std::memory_order_acquire) > 0)) {
    // Note that 'DREAL_CHECK_INTERRUPT' is only defined in setup.py,
    // when we build dReal python package.
#ifdef DREAL_CHECK_INTERRUPT
    if (g_interrupted) {
      DREAL_LOG_DEBUG("KeyboardInterrupt(SIGINT) Detected.");
      throw std::runtime_error("KeyboardInterrupt(SIGINT) Detected.");
    }
#endif

    // 1. Pick a box from the global stack if needed. A kept child
    // (need_to_pop == false) skips this: ParallelBranch already wrote its box
    // and branched dimension into the contractor status.
    bool already_pruned{false};
    if (need_to_pop) {
      pair<Box, int> entry;
      if (!global_stack->pop(entry)) {
        continue;
      }
      current_box = std::move(entry.first);
      already_pruned = entry.second == kAlreadyPrunedTag;
      // The tag never enters ContractorStatus (its branching_point domain is
      // -1 or a valid dimension).
      current_branching_point = already_pruned ? -1 : entry.second;
    }
    need_to_pop = true;

    if (abs_brancher != nullptr) {
      // ABS hook (per-worker activity): age once per search node, then
      // snapshot the pre-Prune diameters the post-Prune bump diffs against.
      abs_brancher->Decay();
      abs_brancher->SnapshotDiams(current_box, ur);
    }

    // 2. Prune the current box. GUARD (pure identity — no contraction): the
    // kAlreadyPrunedTag root was pruned once on the main thread before being
    // pushed; see icp.h.
    if (!already_pruned) {
      prune_timer_guard.resume();
      contractor.Prune(cs, ur);
      prune_timer_guard.pause();
      if (stat.enabled()) {
        stat.num_prune_++;
      }
    }

    if (current_box.empty()) {
      // 3.1. The box is empty after pruning.
      number_of_boxes->fetch_sub(1, std::memory_order_acq_rel);
      continue;
    }

    if (abs_brancher != nullptr) {
      // ABS hook: bump every dim Prune strictly shrank (the emptied-box case
      // exited above; its snapshot is overwritten next node).
      abs_brancher->BumpShrunk(current_box, ur);
    }

    // 3.2. The box is non-empty. Check if the box is still feasible
    // under evaluation and it's small enough.
    eval_timer_guard.resume();
    const optional<DynamicBitset> evaluation_result{
        EvaluateBox(formula_evaluators, current_box, config.precision(), cs,
                    ur)};
    if (!evaluation_result) {
      // 3.2.1. We detect that the current box is not a feasible solution.
      number_of_boxes->fetch_sub(1, std::memory_order_acq_rel);
      DREAL_LOG_DEBUG(
          "IcpParallel::Worker() Detect that the current box is not feasible "
          "by evaluation:\n{}",
          current_box);
      continue;
    }
    if (evaluation_result->none()) {
      // 3.2.2. delta - SAT: We find a box which is smaller enough.
      DREAL_LOG_DEBUG("IcpParallel::Worker() Found a delta-box:\n{}",
                      current_box);
      *found_delta_sat = id;
      return;
    }
    eval_timer_guard.pause();

    // 3.2.3. This box is bigger than delta. Need branching.
    branch_timer_guard.resume();
    if (!ParallelBranch(smear_brancher, abs_brancher, config.brancher(),
                        *evaluation_result, stack_left_box_first, &current_box,
                        &current_branching_point, global_stack,
                        number_of_boxes, ur)) {
      DREAL_LOG_DEBUG(
          "IcpParallel::Worker() Found that the current box is not "
          "satisfying "
          "delta-condition but it's not bisectable.:\n{}",
          current_box);
      // Upstream dreal/dreal4#68: this delta-sat is degenerate — the box
      // still violates the delta-condition but cannot be bisected further.
      // COMPLETENESS hazard (may assert phi^delta T-satisfiable on a
      // T-unsatisfiable phi — missed refutation); warn loudly on stderr,
      // verdict unchanged. Racing workers may each warn once; duplicates
      // are benign.
      WarnDegenerateDeltaSat("non-bisectable box below delta", current_box);
      *found_delta_sat = id;
      return;
    }
    branch_timer_guard.pause();

    need_to_pop = false;

    // We alternate between adding-the-left-box-first policy and
    // adding-the-right-box-first policy.
    stack_left_box_first = !stack_left_box_first;
    stat.num_branch_++;
  }
}
}  // namespace

IcpParallel::IcpParallel(const Config& config)
    : Icp{config}, pool_{static_cast<size_t>(config.number_of_jobs() - 1)} {
  results_.reserve(config.number_of_jobs() - 1);
  status_vector_.reserve(config.number_of_jobs());
}

bool IcpParallel::CheckSat(const Contractor& contractor,
                           const vector<FormulaEvaluator>& formula_evaluators,
                           ContractorStatus* const cs) {
  // --seed-samples seed-and-verify pre-pass boxes. For pure-relational (NRA)
  // theory calls, propose candidate points (LHS sampling or COBYLA) and push
  // a small SOUND box around each so they are explored FIRST. Proposed
  // single-threaded here on the calling thread before any worker spawns, and
  // pushed onto the global stack AFTER the root box below so workers pop
  // them first (LIFO); the root box stays behind them, so no subspace is
  // dropped (COMPLETENESS preserved) and the unchanged Prune+EvaluateBox
  // loop remains the sole arbiter of delta-SAT — the cache/recompute
  // carve-out shape, NOT a fallback: a poor candidate cannot cause a false
  // delta-sat.
  vector<Box> seed_boxes;

  // Initial Prune (calling thread) — establish the FE_UPWARD phase here too.
  // branching_point = -1: full worklist seed for the root prune, stated
  // explicitly rather than inherited from the caller's ContractorStatus.
  // Seed-input snapshot: seeds are proposed from the UN-PRUNED root box, the
  // canonical seed input, so the copy is taken before the root prune — but
  // seeds are only computed after a non-empty root prune (an empty root is
  // unsat; every seed box is a sub-box of the infeasible root). The R3 draft
  // canonicalized on the PRUNED box instead (icp_parity_gaps.md G3) and that
  // regressed a previously-green ∃∀ path catastrophically
  // (MinimizeEqualityElimination.Issue320UnrelatedEqualityHang: <1 s -> >120 s
  // hang at delta=1e-16; isolated 2026-07-22 by varying only this input —
  // nested-CE seeding from the HC4-contracted CE box returns counterexamples
  // that stall the outer CE-guided contraction). COMPLETENESS/termination
  // only — seed choice never moves a verdict.
  {
    const UpwardRoundingScope phase_scope;
    const UpwardRounding ur{phase_scope.token()};
    std::optional<Box> seed_input;
    if (config().seed_samples() > 0 && AllRelational(formula_evaluators)) {
      seed_input.emplace(cs->box());
    }
    cs->mutable_branching_point() = -1;
    contractor.Prune(cs, ur);
    if (!cs->box().empty() && seed_input) {
      seed_boxes = SeedBoxes(formula_evaluators, *seed_input, config(), ur);
    }
  }
  if (cs->box().empty()) {
    return false;
  }

  results_.clear();
  status_vector_.clear();

  // -1 indicates that the process does not find a solution yet. i >= 0
  // indicates that the i-th worker already found a solution.
  atomic<int> found_delta_sat{-1};
  // Process-wide libcds init (cds::Initialize + HP GC singleton), attaching
  // the constructing thread. Function-local static: initialized exactly once,
  // by the FIRST thread ever to reach this line — the top-level solve's
  // calling thread (a nested forall-CE CheckSat triggered by the initial
  // Prune above initializes it on that same thread; pool workers only exist
  // after their spawning CheckSat passed this line). Nested CheckSat calls
  // (the forall-CE sub-solve at jobs = 1, possibly on an outer pool-worker
  // thread — icp_parity_gaps.md G6) skip it and rely on the caller's attach:
  // CdsInit's for the first thread, Worker's thread_local CdsScopeGuard for
  // pool workers.
  static CdsInit cds_init{
      true /* the constructing thread is using lock-free containers. */};
  Stack<pair<Box, int>> global_stack;

  const int number_of_jobs = config().number_of_jobs();

  // Constraint-aware smear branching (--smear): each worker owns its own
  // SmearBrancher because operator() mutates ibex Function eval scratch
  // (f_ctrs.jacobian) — the same per-worker-instance discipline as the
  // contractor copy below. Built once here on the main thread (sequential, so
  // the shared formula_evaluators reads don't race), from the fixed variable
  // set. Empty (nullptr passed) when smear is off -> largest-first.
  vector<unique_ptr<SmearBrancher>> smear_branchers;
  if (config().smear_variant() != SmearVariant::kNone) {
    for (int i = 0; i < number_of_jobs; ++i) {
      smear_branchers.push_back(make_unique<SmearBrancher>(
          formula_evaluators, cs->box(), config().smear_variant()));
    }
  }
  const auto brancher_for = [&smear_branchers](const int i) {
    return smear_branchers.empty() ? nullptr : smear_branchers[i].get();
  };

  // Activity-based ABS branching (--branch abs|absdiam): each worker owns its
  // own BrancherAbs because Decay/SnapshotDiams/BumpShrunk mutate its
  // activity state — the same per-worker-instance discipline as the
  // SmearBrancher above. Activity is therefore per-worker (sharing would need
  // synchronization on every node); the initial main-thread Prune above
  // predates the branchers, so that one node's shrink goes unrecorded —
  // heuristic bookkeeping only, no verdict impact. Empty (nullptr passed)
  // when --branch is largest -> the configured fallback brancher.
  vector<unique_ptr<BrancherAbs>> abs_branchers;
  if (config().brancher_variant() != BrancherVariant::kLargest) {
    for (int i = 0; i < number_of_jobs; ++i) {
      abs_branchers.push_back(make_unique<BrancherAbs>(
          cs->box(), config().brancher_variant(), config().branch_decay()));
    }
  }
  const auto abs_brancher_for = [&abs_branchers](const int i) {
    return abs_branchers.empty() ? nullptr : abs_branchers[i].get();
  };

  // Total number of boxes that are either 1) under processing in a worker or 2)
  // waiting for a worker in the stack. This number goes zero when there is no
  // more work to do.
  atomic<int> number_of_boxes{0};

  // Root pushed tagged kAlreadyPrunedTag (pruned above; the first pop skips
  // the redundant re-prune — see icp.h), seeds tagged -1 (full worklist
  // seed). Push order unchanged: root first, seeds on top (LIFO).
  global_stack.emplace(cs->box(), kAlreadyPrunedTag);
  ++number_of_boxes;
  for (Box& seed_box : seed_boxes) {
    global_stack.emplace(std::move(seed_box), -1);
    ++number_of_boxes;
  }

  for (int i = 0; i < number_of_jobs; ++i) {
    status_vector_.push_back(*cs);
  }

  for (int i = 0; i < number_of_jobs - 1; ++i) {
    results_.push_back(pool_.enqueue(
        Worker, contractor, config(), formula_evaluators, brancher_for(i),
        abs_brancher_for(i), i, false /* not main thread */, &global_stack,
        &status_vector_[i], &found_delta_sat, &number_of_boxes));
  }

  const int last_index{number_of_jobs - 1};
  Worker(contractor, config(), formula_evaluators, brancher_for(last_index),
         abs_brancher_for(last_index), last_index, true /* main thread */,
         &global_stack, &status_vector_[last_index], &found_delta_sat,
         &number_of_boxes);

  // barrier.
  for (auto&& result : results_) {
    result.get();
  }

  // Post-processing: Join all the contractor statuses.
  for (const auto& cs_i : status_vector_) {
    cs->InplaceJoin(cs_i);
  }

  if (found_delta_sat >= 0) {
    cs->mutable_box() = status_vector_[found_delta_sat].box();
    return true;
  } else {
    DREAL_ASSERT(found_delta_sat == -1);
    cs->mutable_box().set_empty();
    return false;
  }
}
}  // namespace dreal
