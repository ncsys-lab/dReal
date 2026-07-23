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
#include "dreal/dreal_main.h"

#include <csignal>
#include <cstdlib>
#include <iostream>

#include <fmt/format.h>

#include "dreal/dr/run.h"
#include "dreal/smt2/run.h"
#include "dreal/solver/config.h"
#include "dreal/solver/context.h"
#include "dreal/util/exception.h"
#include "dreal/util/filesystem.h"
#include "dreal/util/logging.h"
#include "util/rounding.h"
// gcc_build/git_version.h is generated at every build by the CMake custom
// target git_version_h (cmake/GenerateGitVersion.cmake).  It defines:
//   DREAL_GIT_HASH  — short SHA from `git rev-parse --short HEAD`
//   DREAL_GIT_DIRTY — 1 if tracked files are modified, 0 otherwise
//                     (untracked files are ignored; always 0 in Docker)
#include "git_version.h"

namespace dreal {

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

namespace {
// Value wiring for --version fields:
//
//   DREAL_GIT_HASH / DREAL_GIT_DIRTY
//     git → cmake/GenerateGitVersion.cmake (runs each build via add_custom_target)
//         → gcc_build/git_version.h (only rewritten when content changes)
//         → #include "git_version.h" above → here.
//
//   DREAL_BUILD_OS / DREAL_BUILD_OS_VERSION / DREAL_BUILD_ARCH
//     CMake configure-time variables CMAKE_SYSTEM_NAME / _VERSION / _PROCESSOR
//         → target_compile_definitions(dreal4 ...) in CMakeLists.txt
//         → -D flags passed to the compiler → here.
//
//   __DATE__ / __TIME__
//     Compiler built-ins stamped when this translation unit is compiled.
//     Because dreal_main.cc #includes git_version.h, it recompiles whenever
//     the hash or dirty status changes, keeping the timestamp in sync.
string get_version_string() {
#ifndef NDEBUG
  const string build_type{"Debug"};
#else
  const string build_type{"Release"};
#endif
  const string git_suffix = DREAL_GIT_DIRTY
      ? fmt::format("Commit {} <dirty>", DREAL_GIT_HASH)
      : fmt::format("Commit {}", DREAL_GIT_HASH);
  return fmt::format("{}\n"
                     "{}, {} Build.\n"
                     "Built for {} {} {}, on {} {}.",
                     Context::version(),
                     git_suffix, build_type,
                     DREAL_BUILD_OS, DREAL_BUILD_OS_VERSION, DREAL_BUILD_ARCH, __DATE__, __TIME__);
}
}  // namespace

MainProgram::MainProgram(int argc, const char* argv[]) {
  AddOptions();
  opt_.parse(argc, argv);  // Parse Options
  is_options_all_valid_ = ValidateOptions();
}

void MainProgram::PrintUsage() {
  NearestRoundingScope g; // may print doubles
  string usage;
  opt_.getUsage(usage);
  cerr << usage;
}

void MainProgram::AddOptions() {
  NearestRoundingScope g; // parses and maniuplates doubles
  opt_.overview =
      fmt::format("dReal {} : delta-complete SMT solver", get_version_string());
  opt_.syntax = "dreal [OPTIONS] <input file> (.smt2 or .dr)";

  opt_.add("" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Display usage instructions.", "-h", "-help", "--help", "--usage");

  opt_.add("" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Print version number of dReal.", "-v", "--version");

  auto* const positive_double_option_validator =
      new ez::ezOptionValidator("d" /* double */, "gt", "0");

  auto* const positive_int_option_validator =
      new ez::ezOptionValidator("s4" /* 4byte integer */, "gt", "0");

  auto* const nonneg_int_option_validator =
      new ez::ezOptionValidator("s4" /* 4byte integer */, "ge", "0");

  const string kDefaultPrecision{fmt::format("{}", Config::kDefaultPrecision)};
  opt_.add(kDefaultPrecision.c_str() /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           fmt::format("Precision (default = {})\n", kDefaultPrecision).c_str(),
           "--precision", positive_double_option_validator);

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Produce models if delta-sat\n", "--produce-models", "--model");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Output visualization file (.json)\n", "--visualize", "--vis");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Strictly follow the smtlib2 standard.\n", "--smtlib2-compliant");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Debug scanning/lexing\n", "--debug-scanning");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */, "Debug parsing\n",
           "--debug-parsing");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Read from standard input. Uses smt2 by default.\n", "--in");

  auto* const format_option_validator =
      new ez::ezOptionValidator("t", "in", "auto,dr,smt2", false);
  opt_.add("auto" /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "File format. Any one of these (default = auto):\n"
           "smt2, dr, auto (use file extension)\n",
           "--format", format_option_validator);

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Use polytope contractor.\n", "--polytope");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Use polytope contractor in forall contractor.\n",
           "--forall-polytope");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Run the ibex::CtcForAll sound pre-pruner beside the CEGIS forall "
           "decider (NRA exist-forall; works under --jobs>1).\n",
           "--forall-pre-prune");

  opt_.add(
      fmt::format("{}", Config::kDefaultForallPrePrunePrec).c_str() /* Default */,
      false /* Required? */, 1 /* Number of args expected. */,
      0 /* Delimiter if expecting multiple args. */,
      fmt::format("Universal-box bisection precision for --forall-pre-prune "
                  "(default = {}; keep coarse)\n",
                  Config::kDefaultForallPrePrunePrec)
          .c_str(),
      "--forall-pre-prune-prec", positive_double_option_validator);

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Use worklist fixpoint algorithm in ICP.\n", "--worklist-fixpoint");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Use local optimization algorithm for exist-forall problems.\n",
           "--local-optimization");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Dump theory literals.\n", "--dump-theory-literals");

  opt_.add("1" /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */, "Number of jobs.\n",
           "--jobs", "-j");

  opt_.add("false" /* Default */, false /* Required? */,
           0 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Route ICP through the parallel loop (IcpParallel) even at --jobs "
           "1 (zero pool workers; the main thread does the work). EXPERIMENT: "
           "scaffolding for the sequential/parallel parity measurement "
           "(icp_parity_gaps.md R3/G4); removed when the two loops are "
           "consolidated (R4).\n",
           "--icp-force-parallel");

  const string kDefaultNloptFtolRel{
      fmt::format("{}", Config::kDefaultNloptFtolRel)};
  opt_.add(kDefaultNloptFtolRel.c_str() /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           fmt::format(
               "[NLopt] Relative tolerance on function value (default = {})\n",
               kDefaultNloptFtolRel)
               .c_str(),
           "--nlopt-ftol-rel", positive_double_option_validator);

  const string kDefaultNloptFtolAbs{
      fmt::format("{}", Config::kDefaultNloptFtolAbs)};
  opt_.add(kDefaultNloptFtolAbs.c_str() /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           fmt::format(
               "[NLopt] Absolute tolerance on function value (default = {})\n",
               kDefaultNloptFtolAbs)
               .c_str(),
           "--nlopt-ftol-abs", positive_double_option_validator);

  const string kDefaultNloptMaxEval{
      fmt::format("{}", Config::kDefaultNloptMaxEval)};
  opt_.add(
      kDefaultNloptMaxEval.c_str() /* Default */, false /* Required? */,
      1 /* Number of args expected. */,
      0 /* Delimiter if expecting multiple args. */,
      fmt::format(
          "[NLopt] Number of maximum function evaluations (default = {})\n",
          kDefaultNloptMaxEval)
          .c_str(),
      "--nlopt-maxeval", positive_int_option_validator);

  const string kDefaultNloptMaxTime{
      fmt::format("{}", Config::kDefaultNloptMaxTime)};
  opt_.add(
      kDefaultNloptMaxTime.c_str() /* Default */, false /* Required? */,
      1 /* Number of args expected. */,
      0 /* Delimiter if expecting multiple args. */,
      fmt::format(
          "[NLopt] Maximum optimization time (in second) (default = {} sec)\n",
          kDefaultNloptMaxTime)
          .c_str(),
      "--nlopt-maxtime", positive_double_option_validator);

  auto* const verbose_option_validator = new ez::ezOptionValidator(
      "t", "in", "trace,debug,info,warning,error,critical,off", true);
  opt_.add(
      "error",  // Default.
      0,        // Required?
      1,        // Number of args expected.
      0,        // Delimiter if expecting multiple args.
      "Verbosity level. Either one of these (default = error):\n"
      "trace, debug, info, warning, error, critical, off",  // Help description.
      "--verbose",                                          // Flag token.
      verbose_option_validator);

  opt_.add("2" /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Set default initial phase for SAT solver.\n"
           "  0 = false\n"
           "  1 = true\n"
           "  2 = Jeroslow-Wang (default)\n"
           "  3 = random initial phase\n",
           "--sat-default-phase");

  opt_.add("0" /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Set a seed for the random number generator.", "--random-seed");

  opt_.add("0" /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           "Set maximum lemma size to pattern match. (default = 0)", "--drpm-max-size", positive_int_option_validator);

  const string kDefaultDrpmMaxTime{fmt::format("{}", Config::kDefaultDrpmMaxTime)};
  opt_.add(kDefaultDrpmMaxTime.c_str() /* Default */, false /* Required? */,
           1 /* Number of args expected. */,
           0 /* Delimiter if expecting multiple args. */,
           fmt::format("Set pattern matching timeout in seconds. (default = {})", kDefaultDrpmMaxTime).c_str(),
           "--drpm-max-time", positive_double_option_validator);

  // ---- CAPD ODE-contractor tuning knobs --------------------------------------
  auto* const nonneg_double_option_validator =
      new ez::ezOptionValidator("d" /* double */, "ge", "0");
  auto* const c0_set_option_validator =
      new ez::ezOptionValidator("t", "in", "rect2,tripleton,horect2", false);
  auto* const bool_option_validator =
      new ez::ezOptionValidator("t", "in", "true,false", false);

  opt_.add(fmt::format("{}", Config::kDefaultOdeTaylorOrder).c_str(), false, 1, 0,
           fmt::format("CAPD forward-integration Taylor order. (default = {})",
                       Config::kDefaultOdeTaylorOrder).c_str(),
           "--ode-taylor-order", positive_int_option_validator);
  opt_.add(fmt::format("{}", Config::kDefaultOdeBackwardOrder).c_str(), false, 1, 0,
           fmt::format("CAPD backward-integration Taylor order. (default = {})",
                       Config::kDefaultOdeBackwardOrder).c_str(),
           "--ode-backward-order", positive_int_option_validator);
  opt_.add(fmt::format("{}", Config::kDefaultOdeAbsTol).c_str(), false, 1, 0,
           fmt::format("CAPD absolute integration tolerance. (default = {})",
                       Config::kDefaultOdeAbsTol).c_str(),
           "--ode-abs-tol", positive_double_option_validator);
  opt_.add(fmt::format("{}", Config::kDefaultOdeRelTol).c_str(), false, 1, 0,
           fmt::format("CAPD relative integration tolerance. (default = {})",
                       Config::kDefaultOdeRelTol).c_str(),
           "--ode-rel-tol", positive_double_option_validator);
  opt_.add(fmt::format("{}", Config::kDefaultOdeHullGrid).c_str(), false, 1, 0,
           fmt::format("CAPD per-step tube sub-slice count. (default = {})",
                       Config::kDefaultOdeHullGrid).c_str(),
           "--ode-hull-grid", positive_int_option_validator);
  opt_.add("rect2", false, 1, 0,
           "CAPD C0 enclosure set: rect2, tripleton, or horect2. (default = rect2)",
           "--ode-c0-set", c0_set_option_validator);
  opt_.add("true", false, 1, 0,
           "Enable the backward ODE contractor (X_0 narrowing). (default = true)",
           "--ode-backward", bool_option_validator);
  opt_.add(fmt::format("{}", Config::kDefaultOdeMaxStep).c_str(), false, 1, 0,
           "CAPD max integration step cap; 0 = fully adaptive. (default = 0)",
           "--ode-max-step", nonneg_double_option_validator);
  opt_.add("false", false, 0, 0,
           "Report a delta-tight --model witness instead of the raw "
           "terminating box: continuous/integer dimensions are shrunk to "
           "midpoint +/- delta/2 (sound by inclusion monotonicity), and every "
           "ODE dimension is branched below delta before accepting delta-sat. "
           "Costly on deep BMC (default = off).\n",
           "--refine-witness");

  auto* const constraint_order_validator =
      new ez::ezOptionValidator("t", "in", "none,asc,desc", false);
  opt_.add("none", false, 1, 0,
           "ICP fixpoint constraint order: none (declaration), asc (fewest "
           "variables first), desc (most first). (default = none)",
           "--constraint-order", constraint_order_validator);
  auto* const smear_validator = new ez::ezOptionValidator(
      "t", "in", "smearsumrel,smearsum,smearmax,smearmaxrel", false);
  opt_.add("smearsumrel", false, 1, 0,
           "Constraint-aware smear branching (Jacobian-weighted variable "
           "selection) instead of largest-first. One of IBEX's four variants: "
           "smearsumrel, smearsum, smearmax, smearmaxrel. Omit the flag to use "
           "largest-first.\n",
           "--smear", smear_validator);
  auto* const branch_validator =
      new ez::ezOptionValidator("t", "in", "largest,abs,absdiam", false);
  opt_.add("largest", false, 1, 0,
           "Branch-variable heuristic: largest (largest-first, default), abs "
           "(activity / diameter), absdiam (activity * diameter). abs/absdiam "
           "score variables by Prune-shrink activity (our continuous "
           "adaptation of ABS). Mutually exclusive with --smear.\n",
           "--branch", branch_validator);
  opt_.add(fmt::format("{}", Config::kDefaultBranchDecay).c_str(), false, 1, 0,
           fmt::format("Per-node activity decay for --branch abs|absdiam, in "
                       "(0, 1]. (default = {})",
                       Config::kDefaultBranchDecay).c_str(),
           "--branch-decay", positive_double_option_validator);
  // Seed-and-verify pre-pass (off-center NRA SAT instances). Speculative,
  // completeness-only: multi-start COBYLA proposes candidate points and a small
  // sound box around each is verified first by the existing prune+EvaluateBox.
  // The sample count is also the switch — 0 disables; default ON at 64. Gated
  // off for ODE/forall. Mechanism: docs/seeding.md.
  opt_.add(fmt::format("{}", Config::kDefaultSeedSamples).c_str(), false, 1, 0,
           fmt::format("Seed-and-verify pre-pass for pure-relational (NRA) theory "
                       "calls: COBYLA multi-start count; 0 disables. (default = {})",
                       Config::kDefaultSeedSamples).c_str(),
           "--seed-samples", nonneg_int_option_validator);
  opt_.add("false", false, 0, 0,
           "Use the ACID (adaptive 3BCID) shaving contractor on the HC4 path.\n",
           "--acid");
  opt_.add("false", false, 0, 0,
           "Use the 3BCID (fixed-param) shaving contractor on the HC4 path.\n",
           "--3bcid");
  opt_.add(fmt::format("{}", Config::kDefaultAcidS3b).c_str(), false, 1, 0,
           fmt::format("ACID/3BCID s3b shave depth (ibex: best 5-200). (default = {})",
                       Config::kDefaultAcidS3b).c_str(),
           "--s3b", positive_int_option_validator);
  opt_.add(fmt::format("{}", Config::kDefaultAcidCtRatio).c_str(), false, 1, 0,
           fmt::format("ACID ct_ratio adaptive-stop threshold. (default = {})",
                       Config::kDefaultAcidCtRatio).c_str(),
           "--acid-ct-ratio", positive_double_option_validator);
}

bool MainProgram::ValidateOptions() {
  NearestRoundingScope g; // manipulates doubles
  // Checks bad options and bad arguments.
  vector<string> bad_options;
  vector<string> bad_args;
  if (!opt_.gotRequired(bad_options)) {
    for (const auto& bad_option : bad_options) {
      cerr << "ERROR: Missing required option " << bad_option << ".\n\n";
    }
    PrintUsage();
    return false;
  }
  if (!opt_.gotExpected(bad_options)) {
    for (const auto& bad_option : bad_options) {
      cerr << "ERROR: Got unexpected number of arguments for option "
           << bad_option << ".\n\n";
    }
    PrintUsage();
    return false;
  }
  if (!opt_.gotValid(bad_options, bad_args)) {
    for (size_t i = 0; i < bad_options.size(); ++i) {
      cerr << "ERROR: Got invalid argument \"" << bad_args[i]
           << "\" for option " << bad_options[i] << ".\n\n";
    }
    PrintUsage();
    return false;
  }
  // After filtering out bad options/arguments, save the valid ones in `args_`.
  args_.insert(args_.end(), opt_.firstArgs.begin() + 1, opt_.firstArgs.end());
  args_.insert(args_.end(), opt_.unknownArgs.begin(), opt_.unknownArgs.end());
  args_.insert(args_.end(), opt_.lastArgs.begin(), opt_.lastArgs.end());
  if (opt_.isSet("--version")) {
    return true;
  }
  if (opt_.isSet("-h") || (args_.empty() && !opt_.isSet("--in")) ||
      args_.size() > 1) {
    PrintUsage();
    return false;
  }
  return true;
}

void MainProgram::ExtractOptions() {
  NearestRoundingScope g;  // parses and manipulates doubles
  // Temporary variables used to set options.
  string verbosity;
  opt_.get("--verbose")->getString(verbosity);
  if (verbosity == "trace") {
    log()->set_level(spdlog::level::trace);
  } else if (verbosity == "debug") {
    log()->set_level(spdlog::level::debug);
  } else if (verbosity == "info") {
    log()->set_level(spdlog::level::info);
  } else if (verbosity == "warning") {
    log()->set_level(spdlog::level::warn);
  } else if (verbosity == "error") {
    log()->set_level(spdlog::level::err);
  } else if (verbosity == "critical") {
    log()->set_level(spdlog::level::critical);
  } else {
    log()->set_level(spdlog::level::off);
  }

  // --precision
  if (opt_.isSet("--precision")) {
    double precision{0.0};
    opt_.get("--precision")->getDouble(precision);
    config_.mutable_precision().set_from_command_line(precision);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --precision = {}",
                    config_.precision());
  }

  // --produce-model
  if (opt_.isSet("--produce-models")) {
    config_.mutable_produce_models().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --produce-models = {}",
                    config_.produce_models());
  }

  if (opt_.isSet("--visualize")) {
    config_.mutable_visualize().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --visualize = {}",
                    config_.visualize());
  }

  // --smt2-compliant
  if (opt_.isSet("--smtlib2-compliant")) {
    config_.mutable_smtlib2_compliant().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --smtlib2-compliant = {}",
                    config_.smtlib2_compliant());
  }

  // --polytope
  if (opt_.isSet("--polytope")) {
    config_.mutable_use_polytope().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --polytope = {}",
                    config_.use_polytope());
  }

  // --jobs
  if (opt_.isSet("--jobs")) {
    int jobs{};
    opt_.get("--jobs")->getInt(jobs);
    config_.mutable_number_of_jobs().set_from_command_line(jobs);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --jobs = {}",
                    config_.number_of_jobs());
  }

  // --icp-force-parallel
  if (opt_.isSet("--icp-force-parallel")) {
    config_.mutable_use_icp_force_parallel().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --icp-force-parallel = {}",
                    config_.use_icp_force_parallel());
  }

  // --forall-polytope
  if (opt_.isSet("--forall-polytope")) {
    config_.mutable_use_polytope_in_forall().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --forall-polytope = {}",
                    config_.use_polytope_in_forall());
  }

  if (opt_.isSet("--forall-pre-prune")) {
    config_.mutable_use_forall_pre_prune().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --forall-pre-prune = {}",
                    config_.use_forall_pre_prune());
  }

  if (opt_.isSet("--forall-pre-prune-prec")) {
    double prec{0.0};
    opt_.get("--forall-pre-prune-prec")->getDouble(prec);
    config_.mutable_forall_pre_prune_prec().set_from_command_line(prec);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --forall-pre-prune-prec = {}",
                    config_.forall_pre_prune_prec());
  }

  // --worklist-fixpoint
  if (opt_.isSet("--worklist-fixpoint")) {
    config_.mutable_use_worklist_fixpoint().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --worklist-fixpoint = {}",
                    config_.use_worklist_fixpoint());
  }

  // --local-optimization
  if (opt_.isSet("--local-optimization")) {
    config_.mutable_use_local_optimization().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --local-optimization = {}",
                    config_.use_local_optimization());
  }

  // --dump-theory-literals
  if (opt_.isSet("--dump-theory-literals")) {
    config_.mutable_dump_theory_literals().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --dump-theory-literals = {}",
                    config_.dump_theory_literals());
  }

  // --nlopt-ftol-rel
  if (opt_.isSet("--nlopt-ftol-rel")) {
    double nlopt_ftol_rel{0.0};
    opt_.get("--nlopt-ftol-rel")->getDouble(nlopt_ftol_rel);
    config_.mutable_nlopt_ftol_rel().set_from_command_line(nlopt_ftol_rel);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --nlopt-ftol-rel = {}",
                    config_.nlopt_ftol_rel());
  }

  // --nlopt-ftol-abs
  if (opt_.isSet("--nlopt-ftol-abs")) {
    double nlopt_ftol_abs{0.0};
    opt_.get("--nlopt-ftol-abs")->getDouble(nlopt_ftol_abs);
    config_.mutable_nlopt_ftol_abs().set_from_command_line(nlopt_ftol_abs);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --nlopt-ftol-abs = {}",
                    config_.nlopt_ftol_abs());
  }

  // --nlopt-maxeval
  if (opt_.isSet("--nlopt-maxeval")) {
    int nlopt_maxeval{0};
    opt_.get("--nlopt-maxeval")->getInt(nlopt_maxeval);
    config_.mutable_nlopt_maxeval().set_from_command_line(nlopt_maxeval);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --nlopt-maxeval = {}",
                    config_.nlopt_maxeval());
  }

  // --nlopt-maxtime
  if (opt_.isSet("--nlopt-maxtime")) {
    double nlopt_maxtime{0.0};
    opt_.get("--nlopt-maxtime")->getDouble(nlopt_maxtime);
    config_.mutable_nlopt_maxtime().set_from_command_line(nlopt_maxtime);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --nlopt-maxtime = {}",
                    config_.nlopt_maxtime());
  }

  // --sat-default-phase
  if (opt_.isSet("--sat-default-phase")) {
    int sat_default_phase{2};
    opt_.get("--sat-default-phase")->getInt(sat_default_phase);
    config_.mutable_sat_default_phase().set_from_command_line(
        static_cast<Config::SatDefaultPhase>(sat_default_phase));
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --sat-default-phase = {}",
                    config_.sat_default_phase());
  }

  // --random-seed
  if (opt_.isSet("--random-seed")) {
    // NOLINTNEXTLINE(runtime/int)
    static_assert(sizeof(unsigned long) == sizeof(std::uint64_t),
                  "sizeof(unsigned long) != sizeof(std::uint64_t).");
    // NOLINTNEXTLINE(runtime/int)
    unsigned long random_seed{0};
    opt_.get("--random-seed")->getULong(random_seed);
    config_.mutable_random_seed().set_from_command_line(random_seed);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --random-seed = {}",
                    config_.random_seed());
  }

  if (opt_.isSet("--drpm-max-size")) {
    int drpm{0};
    opt_.get("--drpm-max-size")->getInt(drpm);
    config_.mutable_drpm_max_size().set_from_command_line(drpm);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --drpm-max-size= {}",
                    config_.drpm_max_size());
  }
  if (opt_.isSet("--drpm-max-time")) {
    double drpm{0};
    opt_.get("--drpm-max-time")->getDouble(drpm);
    config_.mutable_drpm_max_time().set_from_command_line(drpm);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --drpm-max-time = {}",
                    config_.drpm_max_time());
  }
  if (opt_.isSet("--ode-taylor-order")) {
    int v{0};
    opt_.get("--ode-taylor-order")->getInt(v);
    config_.mutable_ode_taylor_order().set_from_command_line(v);
  }
  if (opt_.isSet("--ode-backward-order")) {
    int v{0};
    opt_.get("--ode-backward-order")->getInt(v);
    config_.mutable_ode_backward_order().set_from_command_line(v);
  }
  if (opt_.isSet("--ode-abs-tol")) {
    double v{0};
    opt_.get("--ode-abs-tol")->getDouble(v);
    config_.mutable_ode_abs_tol().set_from_command_line(v);
  }
  if (opt_.isSet("--ode-rel-tol")) {
    double v{0};
    opt_.get("--ode-rel-tol")->getDouble(v);
    config_.mutable_ode_rel_tol().set_from_command_line(v);
  }
  if (opt_.isSet("--ode-hull-grid")) {
    int v{0};
    opt_.get("--ode-hull-grid")->getInt(v);
    config_.mutable_ode_hull_grid().set_from_command_line(v);
  }
  if (opt_.isSet("--ode-c0-set")) {
    string v;
    opt_.get("--ode-c0-set")->getString(v);
    const OdeC0SetType set_type = (v == "tripleton") ? OdeC0SetType::Tripleton
                                : (v == "horect2")   ? OdeC0SetType::HORect2
                                                     : OdeC0SetType::Rect2;
    config_.mutable_ode_c0_set().set_from_command_line(set_type);
  }
  if (opt_.isSet("--ode-backward")) {
    string v;
    opt_.get("--ode-backward")->getString(v);
    config_.mutable_ode_backward().set_from_command_line(v == "true");
  }
  if (opt_.isSet("--ode-max-step")) {
    double v{0};
    opt_.get("--ode-max-step")->getDouble(v);
    config_.mutable_ode_max_step().set_from_command_line(v);
  }
  if (opt_.isSet("--refine-witness")) {
    config_.mutable_refine_witness().set_from_command_line(true);
    DREAL_LOG_DEBUG("MainProgram::ExtractOptions() --refine-witness = {}",
                    config_.refine_witness());
  }
  if (opt_.isSet("--constraint-order")) {
    string v;
    opt_.get("--constraint-order")->getString(v);
    const ConstraintOrder order = (v == "asc")    ? ConstraintOrder::kAsc
                                  : (v == "desc")  ? ConstraintOrder::kDesc
                                                   : ConstraintOrder::kNone;
    config_.mutable_constraint_order().set_from_command_line(order);
  }
  if (opt_.isSet("--smear")) {
    string v;
    opt_.get("--smear")->getString(v);
    const SmearVariant variant = (v == "smearsum")    ? SmearVariant::kSum
                                 : (v == "smearmax")  ? SmearVariant::kMax
                                 : (v == "smearmaxrel")
                                     ? SmearVariant::kMaxRel
                                     : SmearVariant::kSumRel;
    config_.mutable_smear_variant().set_from_command_line(variant);
  }
  if (opt_.isSet("--branch")) {
    string v;
    opt_.get("--branch")->getString(v);
    const BrancherVariant variant = (v == "abs") ? BrancherVariant::kAbs
                                    : (v == "absdiam")
                                        ? BrancherVariant::kAbsDiam
                                        : BrancherVariant::kLargest;
    config_.mutable_brancher_variant().set_from_command_line(variant);
  }
  if (opt_.isSet("--branch-decay")) {
    double v{0};
    opt_.get("--branch-decay")->getDouble(v);
    if (!(v > 0.0 && v <= 1.0)) {
      throw DREAL_RUNTIME_ERROR("--branch-decay must be in (0, 1]; got {}.",
                                v);
    }
    config_.mutable_branch_decay().set_from_command_line(v);
  }
  if (opt_.isSet("--seed-samples")) {
    int v{0};
    opt_.get("--seed-samples")->getInt(v);
    config_.mutable_seed_samples().set_from_command_line(v);
  }
  if (opt_.isSet("--acid")) {
    config_.mutable_use_acid().set_from_command_line(true);
  }
  if (opt_.isSet("--3bcid")) {
    config_.mutable_use_3bcid().set_from_command_line(true);
  }
  if (config_.use_acid() && config_.use_3bcid()) {
    throw DREAL_RUNTIME_ERROR("--acid and --3bcid are mutually exclusive.");
  }
  if (opt_.isSet("--s3b")) {
    int v{0};
    opt_.get("--s3b")->getInt(v);
    config_.mutable_acid_s3b().set_from_command_line(v);
  }
  if (opt_.isSet("--acid-ct-ratio")) {
    double v{0};
    opt_.get("--acid-ct-ratio")->getDouble(v);
    config_.mutable_acid_ct_ratio().set_from_command_line(v);
  }
}

int MainProgram::Run() {
  if (opt_.isSet("--version")) {
    cout << "dReal " << get_version_string() << '\n';
    return 0;
  }
  if (opt_.isSet("--help")) {
    return 0;
  }
  if (!is_options_all_valid_) {
    return 1;
  }
  ExtractOptions();
  string filename;
  if (!args_.empty()) {
    filename = *args_[0];
    if (filename.empty()) {
      PrintUsage();
      return 1;
    }
  }
  if (!opt_.isSet("--in") && !file_exists(filename)) {
    cerr << "File not found: " << filename << "\n" << '\n';
    PrintUsage();
    return 1;
  }
  const string extension{get_extension(filename)};
  string format_opt;
  opt_.get("--format")->getString(format_opt);
  if (format_opt == "smt2" ||
      (format_opt == "auto" && (extension == "smt2" || opt_.isSet("--in")))) {
    RunSmt2(filename, config_, opt_.isSet("--debug-scanning"),
            opt_.isSet("--debug-parsing"));
  } else if (format_opt == "dr" ||
             (format_opt == "auto" && extension == "dr")) {
    RunDr(filename, config_, opt_.isSet("--debug-scanning"),
          opt_.isSet("--debug-parsing"));
  } else {
    cerr << "Unknown extension: " << filename << "\n" << '\n';
    PrintUsage();
    return 1;
  }
  return 0;
}
}  // namespace dreal

namespace {
void HandleSigInt(const int) {
  // Properly exit so that we can see stat information produced by destructors
  // even if a user press C-c.
  std::exit(1);
}
}  // namespace

int main(int argc, const char* argv[]) {
  // default stack size is 8MB
  // CPS-pattern matching algo goes DEEP...
  // doing 63MB because that's approximately the max on macOS
  constexpr rlim_t desired_stack_size = rlim_t{63} * 1024 * 1024;
  rlimit rl{0};
  getrlimit(RLIMIT_STACK, &rl);
  rl.rlim_cur = std::max(rl.rlim_cur, desired_stack_size);
  setrlimit(RLIMIT_STACK, &rl);
  rl.rlim_cur = 0;
  getrlimit(RLIMIT_STACK, &rl);
  if (rl.rlim_cur < desired_stack_size) {
    // `DREAL_LOG_*` functions have not been initialized yet.
    std::cerr << "Failed to configure desired stack size limit. Exiting." << '\n';
    std::cerr << "\tCurrent Size = " << rl.rlim_cur << '\n';
    std::cerr << "\tMaximum Size = " << rl.rlim_max << '\n';
    std::cerr << "\tDesired Size = " << desired_stack_size << '\n';
    // exit(-1);
  }

  std::signal(SIGINT, HandleSigInt);
  dreal::MainProgram main_program{argc, argv};
  return main_program.Run();
}
