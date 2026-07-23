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

#include "dreal/contractor/odes/ode_types.h"
#include "dreal/solver/brancher.h"
#include "dreal/util/box.h"
#include "dreal/util/dynamic_bitset.h"
#include "dreal/util/option_value.h"

namespace dreal {

/// Order in which the per-constraint contractors are run inside the ICP
/// fixpoint. Sound: a fixpoint's result is order-independent, only its cost
/// (number of contraction passes / transcendental evals) changes.
enum class ConstraintOrder {
  kNone,  ///< declaration order (default; unchanged behavior)
  kAsc,   ///< fewest-variables first (cheap/tight constraints propagate first)
  kDesc,  ///< most-variables first
};

/// Jacobian-weighted "smear" branch-variable heuristic (IBEX's four
/// SmearFunction variants). Two binary axes: aggregate over constraints by
/// sum vs max, and absolute impact |J_ij|·w_j vs relative (normalized by the
/// per-constraint row-sum NC_i = Σ_k |J_ik|·w_k). Variable choice is a
/// completeness/perf lever only, never a soundness concern. kNone = the
/// default largest-first brancher (smear off).
enum class SmearVariant {
  kNone,    ///< off: use the configured largest-first brancher
  kSumRel,  ///< SmearSumRelative: Σ_i |J_ij|·w_j / NC_i (IBEX's go-to)
  kSum,     ///< SmearSum (Hansen): Σ_i |J_ij|·w_j
  kMax,     ///< SmearMax (Kearfott): max_i |J_ij|·w_j
  kMaxRel,  ///< SmearMaxRelative: max_i (|J_ij|·w_j / NC_i)
};

/// Activity-based ("ABS") branch-variable heuristic — our continuous
/// adaptation of Michel & Van Hentenryck's finite-domain activity-based
/// search (CPAIOR'12); the continuous scores and the decay default are ours,
/// not the paper's (see brancher_abs.h). Per-variable activity A_i is bumped
/// when a Prune strictly shrinks dim i and decays (--branch-decay) once per
/// search node. Variable choice is a completeness/perf lever only, never a
/// soundness concern. kLargest = the default largest-first brancher (ABS off).
enum class BrancherVariant {
  kLargest,  ///< off: use the configured largest-first brancher
  kAbs,      ///< score_i = A_i / diam_i (degenerates to smallest-diam-first)
  kAbsDiam,  ///< score_i = A_i * diam_i (degenerates to largest-first)
};


class Config {
 public:
  Config() = default;
  Config(const Config&) = default;
  Config(Config&&) = default;
  Config& operator=(const Config&) = default;
  Config& operator=(Config&&) = default;
  ~Config() = default;

  using Brancher = std::function<int(
      const Box& box, const DynamicBitset& bitset, Box* left, Box* right,
      const UpwardRounding& ur)>;

  /// Returns the precision option.
  double precision() const;

  /// Returns a mutable OptionValue for 'precision'.
  OptionValue<double>& mutable_precision();

  /// Returns the produce_models option.
  bool produce_models() const;

  bool visualize() const;

  /// Returns a mutable OptionValue for 'produce_models'.
  OptionValue<bool>& mutable_produce_models();

  OptionValue<bool>& mutable_visualize();

  /// Returns whether it uses polytope contractors or not.
  bool use_polytope() const;

  /// Returns a mutable OptionValue for 'use_polytope'.
  OptionValue<bool>& mutable_use_polytope();

  /// Returns whether it uses polytope contractors inside forall contractors.
  bool use_polytope_in_forall() const;

  /// Returns a mutable OptionValue for 'use_polytope_in_forall'.
  OptionValue<bool>& mutable_use_polytope_in_forall();

  /// Returns whether it runs the ibex::CtcForAll pre-pruner beside CEGIS.
  bool use_forall_pre_prune() const;

  /// Returns a mutable OptionValue for 'use_forall_pre_prune'.
  OptionValue<bool>& mutable_use_forall_pre_prune();

  /// Returns the universal-box bisection precision for the forall pre-pruner.
  double forall_pre_prune_prec() const;

  /// Returns a mutable OptionValue for 'forall_pre_prune_prec'.
  OptionValue<double>& mutable_forall_pre_prune_prec();

  /// Returns whether it uses worklist-fixpoint algorithm.
  bool use_worklist_fixpoint() const;

  /// Returns a mutable OptionValue for 'use_worklist_fixpoint'.
  OptionValue<bool>& mutable_use_worklist_fixpoint();

  /// Returns whether it uses local optimization algorithm in exist-forall
  /// problems.
  bool use_local_optimization() const;

  /// Returns a mutable OptionValue for 'use_local_optimization'.
  OptionValue<bool>& mutable_use_local_optimization();

  /// Returns whether it uses local optimization algorithm in exist-forall
  /// problems.
  bool dump_theory_literals() const;

  /// Returns a mutable OptionValue for 'dump_theory_literals'.
  OptionValue<bool>& mutable_dump_theory_literals();

  /// Returns the number of parallel jobs.
  int number_of_jobs() const;

  /// Returns a mutable OptionValue for 'number_of_jobs'.
  OptionValue<int>& mutable_number_of_jobs();

  /// Returns whether ICP runs through IcpParallel even at jobs = 1
  /// (--icp-force-parallel). Experiment-scoped scaffolding for the
  /// seq/parallel parity measurement (icp_parity_gaps.md R3/G4); dies with
  /// the R4 consolidation.
  bool use_icp_force_parallel() const;

  /// Returns a mutable OptionValue for 'use_icp_force_parallel'.
  OptionValue<bool>& mutable_use_icp_force_parallel();

  /// Returns whether the ICP algorithm stacks the left box first
  /// after branching.
  bool stack_left_box_first() const;

  /// Returns a mutable OptionValue for 'stack_left_box_first'.
  OptionValue<bool>& mutable_stack_left_box_first();

  /// Returns the brancher.
  const Brancher& brancher() const;

  /// Returns a mutable OptionValue for `brancher`.
  OptionValue<Brancher>& mutable_brancher();

  /// Returns the constraint ordering for the ICP fixpoint.
  ConstraintOrder constraint_order() const;
  /// Returns a mutable OptionValue for `constraint_order`.
  OptionValue<ConstraintOrder>& mutable_constraint_order();

  /// Returns the smear branch-variable heuristic variant (kNone = off).
  SmearVariant smear_variant() const;
  /// Returns a mutable OptionValue for `smear_variant`.
  OptionValue<SmearVariant>& mutable_smear_variant();

  /// Returns the ABS branch-variable heuristic variant (kLargest = off).
  BrancherVariant brancher_variant() const;
  /// Returns a mutable OptionValue for `brancher_variant`.
  OptionValue<BrancherVariant>& mutable_brancher_variant();

  /// Returns the per-node activity decay for --branch abs|absdiam.
  double branch_decay() const;
  /// Returns a mutable OptionValue for `branch_decay`.
  OptionValue<double>& mutable_branch_decay();

  /// Returns the seed-and-verify candidate budget: the COBYLA multi-start count,
  /// and the on/off switch — `> 0` enables the `--seed-samples` pre-pass, `0`
  /// disables it.
  int seed_samples() const;
  /// Returns a mutable OptionValue for `seed_samples`.
  OptionValue<int>& mutable_seed_samples();

  /// Returns whether the ACID shaving contractor is enabled.
  bool use_acid() const;
  /// Returns a mutable OptionValue for `use_acid`.
  OptionValue<bool>& mutable_use_acid();

  /// Returns whether the 3BCID shaving contractor is enabled.
  bool use_3bcid() const;
  /// Returns a mutable OptionValue for `use_3bcid`.
  OptionValue<bool>& mutable_use_3bcid();

  /// Returns the ACID/3BCID `s3b` shave-depth parameter.
  int acid_s3b() const;
  /// Returns a mutable OptionValue for `acid_s3b`.
  OptionValue<int>& mutable_acid_s3b();

  /// Returns the ACID `ct_ratio` adaptive-stop parameter.
  double acid_ct_ratio() const;
  /// Returns a mutable OptionValue for `acid_ct_ratio`.
  OptionValue<double>& mutable_acid_ct_ratio();

  /// Returns whether the interval-Newton square-equality contractor is
  /// enabled.
  bool use_newton() const;
  /// Returns a mutable OptionValue for `use_newton`.
  OptionValue<bool>& mutable_use_newton();

  /// Returns the Newton application ceiling (ibex CtcNewton `ceil`).
  double newton_ceil() const;
  /// Returns a mutable OptionValue for `newton_ceil`.
  OptionValue<double>& mutable_newton_ceil();

  /// Returns whether the OBBT (optimization-based bound tightening) LP
  /// contractor is enabled.
  bool use_obbt() const;
  /// Returns a mutable OptionValue for `use_obbt`.
  OptionValue<bool>& mutable_use_obbt();

  /// Returns whether the Mohc monotonic-occurrence propagation contractor is
  /// enabled.
  bool use_mohc() const;
  /// Returns a mutable OptionValue for `use_mohc`.
  OptionValue<bool>& mutable_use_mohc();

  /// @name NLopt Options
  ///
  /// Specifies stopping criteria of NLopt. See
  /// https://nlopt.readthedocs.io/en/latest/NLopt_Reference/#stopping-criteria
  /// for more information.
  ///
  /// @{

  /// Returns relative tolerance on function value in NLopt.
  double nlopt_ftol_rel() const;

  /// Returns a mutable OptionValue for `nlopt_ftol_rel`.
  OptionValue<double>& mutable_nlopt_ftol_rel();

  /// Returns absolute tolerance on function value in NLopt.
  double nlopt_ftol_abs() const;

  /// Returns a mutable OptionValue for `nlopt_ftol_abs`.
  OptionValue<double>& mutable_nlopt_ftol_abs();

  // Returns the number of maximum function evaluations allowed in NLopt.
  int nlopt_maxeval() const;

  /// Returns a mutable OptionValue for `nlopt_maxeval`.
  OptionValue<int>& mutable_nlopt_maxeval();

  /// Returns the maxtime in NLopt.
  double nlopt_maxtime() const;

  /// Returns a mutable OptionValue for `nlopt_maxtime`.
  OptionValue<double>& mutable_nlopt_maxtime();

  enum class SatDefaultPhase {
    False = 0,
    True = 1,
    JeroslowWang = 2,  // Default option
    RandomInitialPhase = 3
  };

  /// Returns the default phase for SAT solver.
  SatDefaultPhase sat_default_phase() const;

  /// Returns a mutable OptionValue for `sat_default_phase`.
  OptionValue<SatDefaultPhase>& mutable_sat_default_phase();

  /// Returns the random seed.
  uint32_t random_seed() const;

  /// Returns a mutable OptionValue for `random_seed`.
  OptionValue<uint32_t>& mutable_random_seed();

  int drpm_max_size() const;
  OptionValue<int>& mutable_drpm_max_size();
  std::chrono::duration<double, std::chrono::seconds::period> drpm_max_time() const;
  OptionValue<double>& mutable_drpm_max_time();

  /// @name CAPD ODE-contractor tuning knobs
  ///
  /// Runtime overrides for the CAPD rigorous Taylor integrator. Defaults match
  /// the values the contractor shipped as compile-time constants; see the
  /// kDefaultOde* constants below for the rationale. Forward and backward
  /// integration take separate Taylor orders (a lohner contractor instance is
  /// single-direction).
  /// @{
  int ode_taylor_order() const;
  OptionValue<int>& mutable_ode_taylor_order();
  int ode_backward_order() const;
  OptionValue<int>& mutable_ode_backward_order();
  double ode_abs_tol() const;
  OptionValue<double>& mutable_ode_abs_tol();
  double ode_rel_tol() const;
  OptionValue<double>& mutable_ode_rel_tol();
  int ode_hull_grid() const;
  OptionValue<int>& mutable_ode_hull_grid();
  OdeC0SetType ode_c0_set() const;
  OptionValue<OdeC0SetType>& mutable_ode_c0_set();
  bool ode_backward() const;
  OptionValue<bool>& mutable_ode_backward();
  double ode_max_step() const;
  OptionValue<double>& mutable_ode_max_step();
  /// @}

  /// Returns whether --model reports a δ-tight witness instead of the raw
  /// terminating box (--refine-witness).
  bool refine_witness() const;
  OptionValue<bool>& mutable_refine_witness();

  /// Returns if it's smtlib2_compliant mode.
  bool smtlib2_compliant() const;

  /// Returns a mutable OptionValue for `smtlib2_compliant`.
  OptionValue<bool>& mutable_smtlib2_compliant();

  /// @}

  static constexpr double kDefaultPrecision{0.001};
  // Coarse by design: the pre-pruner is a shallow skimmer over the (low-dim)
  // universal box, not a solver. A fine prec over-bisects and *regresses*
  // (measured: prec=0.1 timed out mlp2_n1_h1 at δ=0.2 where prec≥0.3 solves in
  // <1s; too-coarse only loses pruning, never regresses). See
  // exists_forall_perf.md / AUDIT-QUANTIFIERS.md Q1/Q4.
  static constexpr double kDefaultForallPrePrunePrec{0.5};
  static constexpr double kDefaultNloptFtolRel{1e-6};
  static constexpr double kDefaultNloptFtolAbs{1e-6};
  static constexpr int kDefaultNloptMaxEval{100};
  static constexpr double kDefaultNloptMaxTime{0.01};
  static constexpr double kDefaultDrpmMaxTime{0.222};

  // CAPD ODE-contractor defaults (previously compile-time constants in
  // contractor_odes_capd.cc). Forward & backward order 12 + hull-grid 4 (down
  // from 20 / 16) — the 2026-06 re-tuning result. The campaign numbers, the
  // per-order problem-dependence (why order stays a flag: tacas inverters want
  // ~8-12, stiff long-horizon github ~16-20), and the F1/hull history are in
  // OPTIMIZATION_LOG.md §"2026-06 re-tuning campaign".
  //
  // SOUNDNESS/COMPLETENESS: lowering order / hull-grid only WIDENS the enclosures
  // (each sub-slice stays a sound outward over-approximation), so a coarser knob
  // can NEVER cause a false-`unsat` (SOUNDNESS) — its only cost is a missed
  // refutation (COMPLETENESS). Since the 2026-06 centered-in-time tube fix
  // (HULL_COMPLETENESS.md "Resolution") hull-grid is no longer
  // completeness-load-bearing: the per-slice range is mean-value-in-time, holding
  // the tube near CAPD precision regardless of step size (the GravityInvariantTest
  // F1 sharp-interior case now refutes at this hull-4 default). Raise it (16+) only
  // for pathologically sharp invariants. Tolerances 1e-10 (not step-limiting here).
  static constexpr int kDefaultOdeTaylorOrder{12};
  static constexpr int kDefaultOdeBackwardOrder{12};
  static constexpr double kDefaultOdeAbsTol{1e-10};
  static constexpr double kDefaultOdeRelTol{1e-10};
  static constexpr int kDefaultOdeHullGrid{4};
  static constexpr double kDefaultOdeMaxStep{0.0};  // 0 => fully adaptive

  // --seed-samples candidate budget: the COBYLA multi-start count, and the
  // seed-and-verify on/off switch (> 0 on, 0 off). Multi-start (not a single
  // center start) is what dodges the flat-center stall. Mechanism: docs/seeding.md;
  // the A/B that picked 64: docs/decisions.md §"Seed-and-verify".
  static constexpr int kDefaultSeedSamples{64};

  // ACID / 3BCID shaving contractor knobs (mirror ibex CtcAcid/Ctc3BCid
  // defaults). s3b is the dominant tuning parameter (ibex: best 5-200, 10
  // default); ct_ratio is ACID's adaptive-stop threshold.
  static constexpr int kDefaultAcidS3b{10};
  static constexpr double kDefaultAcidCtRatio{0.002};

  // Interval-Newton contractor knob (mirrors ibex::CtcNewton::default_ceil =
  // 0.01, ibex_CtcNewton.h): the Newton step runs only when the box's max
  // diameter is <= ceil, avoiding useless Jacobian evaluations on wide boxes.
  static constexpr double kDefaultNewtonCeil{0.01};

  // --branch-decay default for --branch abs|absdiam: per-node multiplicative
  // activity aging, in (0, 1]. 0.999 (a slow ~693-node half-life) is our
  // choice, not a value from the ABS paper (the paper's rule is
  // finite-domain; see brancher_abs.h).
  static constexpr double kDefaultBranchDecay{0.999};

 private:
  // NOTE: Make sure to match the default values specified here with the ones
  // specified in dreal/dreal_main.cc.
  OptionValue<double> precision_{kDefaultPrecision};
  OptionValue<bool> produce_models_{false};
  OptionValue<bool> visualize_{false};
  OptionValue<bool> use_polytope_{false};
  OptionValue<bool> use_polytope_in_forall_{false};
  OptionValue<bool> use_forall_pre_prune_{false};
  OptionValue<double> forall_pre_prune_prec_{kDefaultForallPrePrunePrec};
  OptionValue<bool> use_worklist_fixpoint_{false};
  OptionValue<bool> use_local_optimization_{false};
  OptionValue<bool> dump_theory_literals_{false};
  OptionValue<int> number_of_jobs_{1};
  // --icp-force-parallel (parity-experiment scaffolding; see the getter).
  OptionValue<bool> use_icp_force_parallel_{false};
  OptionValue<bool> stack_left_box_first_{false};
  OptionValue<bool> smtlib2_compliant_{false};

  // --------------------------------------------------------------------------
  // NLopt options (stopping criteria)
  // --------------------------------------------------------------------------
  // These options are used when we use NLopt in refining
  // counterexamples via local-optimization. The following
  // descriptions are from
  // https://nlopt.readthedocs.io/en/latest/NLopt_Reference/#stopping-criteria
  //
  // Set relative tolerance on function value: stop when an
  // optimization step (or an estimate of the optimum) changes the
  // objective function value by less than tol multiplied by the
  // absolute value of the function value. (If there is any chance
  // that your optimum function value is close to zero, you might want
  // to set an absolute tolerance with nlopt_set_ftol_abs as well.)
  // Criterion is disabled if tol is non-positive.
  OptionValue<double> nlopt_ftol_rel_{kDefaultNloptFtolRel};

  // Set absolute tolerance on function value: stop when an
  // optimization step (or an estimate of the optimum) changes the
  // function value by less than tol. Criterion is disabled if tol is
  // non-positive.
  OptionValue<double> nlopt_ftol_abs_{kDefaultNloptFtolAbs};

  // Stop when the number of function evaluations exceeds
  // maxeval. (This is not a strict maximum: the number of function
  // evaluations may exceed maxeval slightly, depending upon the
  // algorithm.) Criterion is disabled if maxeval is non-positive.
  OptionValue<int> nlopt_maxeval_{kDefaultNloptMaxEval};

  // Stop when the optimization time (in seconds) exceeds
  // maxtime. (This is not a strict maximum: the time may exceed
  // maxtime slightly, depending upon the algorithm and on how slow
  // your function evaluation is.) Criterion is disabled if maxtime is
  // non-positive.
  OptionValue<double> nlopt_maxtime_{kDefaultNloptMaxTime};

  // Default initial phase (for PICOSAT):
  //   0 = false
  //   1 = true
  //   2 = Jeroslow-Wang (default)
  //   3 = random initial phase
  OptionValue<SatDefaultPhase> sat_default_phase_{
      SatDefaultPhase::JeroslowWang};

  // Seed for Random Number Generator.
  OptionValue<uint32_t> random_seed_{0};

  OptionValue<int> drpm_max_size_{0};
  OptionValue<double> drpm_max_time_{0.222};

  // CAPD ODE-contractor knobs (defaults = current shipped behavior).
  OptionValue<int> ode_taylor_order_{kDefaultOdeTaylorOrder};
  OptionValue<int> ode_backward_order_{kDefaultOdeBackwardOrder};
  OptionValue<double> ode_abs_tol_{kDefaultOdeAbsTol};
  OptionValue<double> ode_rel_tol_{kDefaultOdeRelTol};
  OptionValue<int> ode_hull_grid_{kDefaultOdeHullGrid};
  OptionValue<OdeC0SetType> ode_c0_set_{OdeC0SetType::Rect2};
  OptionValue<bool> ode_backward_{true};
  OptionValue<double> ode_max_step_{kDefaultOdeMaxStep};
  // δ-tight --model witnesses (--refine-witness). OFF (default), the model is
  // the raw terminating box — the exact region ICP certified, idempotent
  // under re-feeding; a don't-care Boolean prints as [false, true]. ON, per
  // certificate class: don't-care Boolean/binary dims pin to true and
  // continuous/integer dims are shrunk to midpoint±δ/2 at report time
  // (Tighten — sound post-hoc by inclusion monotonicity; ODE-atom dims
  // exempt), and
  // OdeFormulaEvaluator reports a positive ODE atom's widest variable so ICP
  // branches every ODE dimension below δ before accepting delta-sat (an ODE
  // certificate is not inclusion-monotone, so it cannot be post-hoc sliced).
  // The ODE half is costly on deep-BMC SAT (A/B: 4.89× github PAR2 with 18
  // SAT→TIM, 1.99× saradc; a 0-branch accept becomes a ~10³–10⁴-bisection
  // refinement descent). docs/decisions.md §"ODE formula evaluator".
  OptionValue<bool> refine_witness_{false};

  // ICP fixpoint constraint ordering (default kNone = declaration order).
  OptionValue<ConstraintOrder> constraint_order_{ConstraintOrder::kNone};

  // Smear constraint-aware branching variant (default kNone = off; see
  // brancher_smear.cc). Picks the split variable, not the split point.
  OptionValue<SmearVariant> smear_variant_{SmearVariant::kNone};

  // Activity-based ABS branching variant (default kLargest = off; see
  // brancher_abs.cc). Picks the split variable, not the split point.
  OptionValue<BrancherVariant> brancher_variant_{BrancherVariant::kLargest};
  // Per-node activity decay for --branch abs|absdiam.
  OptionValue<double> branch_decay_{kDefaultBranchDecay};

  // Seed-and-verify pre-pass: speculative, COMPLETENESS-only, gated to
  // pure-relational (NRA) calls (AllRelational). The count is also the on/off
  // switch (0 disables). See seed/seed.h, docs/seeding.md.
  OptionValue<int> seed_samples_{kDefaultSeedSamples};

  // ACID / 3BCID shaving contractor (default off; see contractor_ibex_acid.cc).
  OptionValue<bool> use_acid_{false};
  OptionValue<bool> use_3bcid_{false};
  OptionValue<int> acid_s3b_{kDefaultAcidS3b};
  OptionValue<double> acid_ct_ratio_{kDefaultAcidCtRatio};

  // Interval-Newton square-equality contractor (default off; see
  // contractor_ibex_newton.cc).
  OptionValue<bool> use_newton_{false};
  OptionValue<double> newton_ceil_{kDefaultNewtonCeil};

  // OBBT 2n-certified-LP contractor (default off; see contractor_ibex_obbt.cc).
  OptionValue<bool> use_obbt_{false};

  // Mohc monotonic-occurrence propagation contractor (default off; see
  // contractor_ibex_mohc.cc).
  OptionValue<bool> use_mohc_{false};

  // Brancher to use. By default it uses `BranchLargestFirst`.
  OptionValue<Brancher> brancher_{BranchLargestFirst};
};
std::ostream& operator<<(std::ostream& os,
                         const Config::SatDefaultPhase& sat_default_phase);

std::ostream& operator<<(std::ostream& os, const Config& config);

}  // namespace dreal

template <> struct fmt::formatter<dreal::Config> : fmt::ostream_formatter {};
template <> struct fmt::formatter<dreal::Config::SatDefaultPhase> : fmt::ostream_formatter {};
