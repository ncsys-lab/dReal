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
#include "dreal/solver/config.h"

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "dreal/util/exception.h"

namespace dreal {

using std::ostream;

#if __cplusplus < 201703L
constexpr double Config::kDefaultPrecision;
constexpr double Config::kDefaultNloptFtolRel;
constexpr double Config::kDefaultNloptFtolAbs;
constexpr int Config::kDefaultNloptMaxEval;
constexpr double Config::kDefaultNloptMaxTime;
#endif

double Config::precision() const { return precision_.get(); }
OptionValue<double>& Config::mutable_precision() { return precision_; }

bool Config::produce_models() const { return produce_models_.get(); }
OptionValue<bool>& Config::mutable_produce_models() { return produce_models_; }

bool Config::visualize() const { return visualize_.get(); }
OptionValue<bool>& Config::mutable_visualize() { return visualize_; }

bool Config::use_polytope() const { return use_polytope_.get(); }
OptionValue<bool>& Config::mutable_use_polytope() { return use_polytope_; }

bool Config::use_polytope_in_forall() const {
  return use_polytope_in_forall_.get();
}
OptionValue<bool>& Config::mutable_use_polytope_in_forall() {
  return use_polytope_in_forall_;
}

PolytopeLinearizer Config::polytope_linearizer() const {
  return polytope_linearizer_.get();
}
OptionValue<PolytopeLinearizer>& Config::mutable_polytope_linearizer() {
  return polytope_linearizer_;
}

bool Config::use_forall_pre_prune() const {
  return use_forall_pre_prune_.get();
}
OptionValue<bool>& Config::mutable_use_forall_pre_prune() {
  return use_forall_pre_prune_;
}

double Config::forall_pre_prune_prec() const {
  return forall_pre_prune_prec_.get();
}
OptionValue<double>& Config::mutable_forall_pre_prune_prec() {
  return forall_pre_prune_prec_;
}

bool Config::use_worklist_fixpoint() const {
  return use_worklist_fixpoint_.get();
}
OptionValue<bool>& Config::mutable_use_worklist_fixpoint() {
  return use_worklist_fixpoint_;
}

bool Config::use_local_optimization() const {
  return use_local_optimization_.get();
}
OptionValue<bool>& Config::mutable_use_local_optimization() {
  return use_local_optimization_;
}

bool Config::dump_theory_literals() const {
  return dump_theory_literals_.get();
}
OptionValue<bool>& Config::mutable_dump_theory_literals() {
  return dump_theory_literals_;
}

int Config::number_of_jobs() const { return number_of_jobs_.get(); }
OptionValue<int>& Config::mutable_number_of_jobs() { return number_of_jobs_; }

bool Config::use_icp_force_parallel() const {
  return use_icp_force_parallel_.get();
}
OptionValue<bool>& Config::mutable_use_icp_force_parallel() {
  return use_icp_force_parallel_;
}

bool Config::stack_left_box_first() const {
  return stack_left_box_first_.get();
}
OptionValue<bool>& Config::mutable_stack_left_box_first() {
  return stack_left_box_first_;
}

bool Config::smtlib2_compliant() const { return smtlib2_compliant_.get(); }

OptionValue<bool>& Config::mutable_smtlib2_compliant() {
  return smtlib2_compliant_;
}

ConstraintOrder Config::constraint_order() const {
  return constraint_order_.get();
}
OptionValue<ConstraintOrder>& Config::mutable_constraint_order() {
  return constraint_order_;
}

SmearVariant Config::smear_variant() const { return smear_variant_.get(); }
OptionValue<SmearVariant>& Config::mutable_smear_variant() {
  return smear_variant_;
}

BrancherVariant Config::brancher_variant() const {
  return brancher_variant_.get();
}
OptionValue<BrancherVariant>& Config::mutable_brancher_variant() {
  return brancher_variant_;
}

double Config::branch_decay() const { return branch_decay_.get(); }
OptionValue<double>& Config::mutable_branch_decay() { return branch_decay_; }

int Config::seed_samples() const { return seed_samples_.get(); }
OptionValue<int>& Config::mutable_seed_samples() { return seed_samples_; }

bool Config::use_acid() const { return use_acid_.get(); }
OptionValue<bool>& Config::mutable_use_acid() { return use_acid_; }

bool Config::use_3bcid() const { return use_3bcid_.get(); }
OptionValue<bool>& Config::mutable_use_3bcid() { return use_3bcid_; }

int Config::acid_s3b() const { return acid_s3b_.get(); }
OptionValue<int>& Config::mutable_acid_s3b() { return acid_s3b_; }

double Config::acid_ct_ratio() const { return acid_ct_ratio_.get(); }
OptionValue<double>& Config::mutable_acid_ct_ratio() { return acid_ct_ratio_; }

bool Config::use_newton() const { return use_newton_.get(); }
OptionValue<bool>& Config::mutable_use_newton() { return use_newton_; }

double Config::newton_ceil() const { return newton_ceil_.get(); }
OptionValue<double>& Config::mutable_newton_ceil() { return newton_ceil_; }

bool Config::use_obbt() const { return use_obbt_.get(); }
OptionValue<bool>& Config::mutable_use_obbt() { return use_obbt_; }

bool Config::use_mohc() const { return use_mohc_.get(); }
OptionValue<bool>& Config::mutable_use_mohc() { return use_mohc_; }

const Config::Brancher& Config::brancher() const { return brancher_.get(); }

OptionValue<Config::Brancher>& Config::mutable_brancher() { return brancher_; }

double Config::nlopt_ftol_rel() const { return nlopt_ftol_rel_.get(); }

OptionValue<double>& Config::mutable_nlopt_ftol_rel() {
  return nlopt_ftol_rel_;
}

double Config::nlopt_ftol_abs() const { return nlopt_ftol_abs_.get(); }

OptionValue<double>& Config::mutable_nlopt_ftol_abs() {
  return nlopt_ftol_abs_;
}

int Config::nlopt_maxeval() const { return nlopt_maxeval_.get(); }

OptionValue<int>& Config::mutable_nlopt_maxeval() { return nlopt_maxeval_; }

double Config::nlopt_maxtime() const { return nlopt_maxtime_.get(); }

OptionValue<double>& Config::mutable_nlopt_maxtime() { return nlopt_maxtime_; }

Config::SatDefaultPhase Config::sat_default_phase() const {
  return sat_default_phase_.get();
}

OptionValue<Config::SatDefaultPhase>& Config::mutable_sat_default_phase() {
  return sat_default_phase_;
}

uint32_t Config::random_seed() const { return random_seed_.get(); }

OptionValue<uint32_t>& Config::mutable_random_seed() { return random_seed_; }

int Config::drpm_max_size() const { return drpm_max_size_.get(); }

OptionValue<int>& Config::mutable_drpm_max_size() { return drpm_max_size_; }

std::chrono::duration<double, std::chrono::seconds::period> Config::drpm_max_time() const {
  return std::chrono::duration<double, std::chrono::seconds::period>(drpm_max_time_.get());
}

OptionValue<double>& Config::mutable_drpm_max_time() { return drpm_max_time_; }

int Config::ode_taylor_order() const { return ode_taylor_order_.get(); }
OptionValue<int>& Config::mutable_ode_taylor_order() { return ode_taylor_order_; }

int Config::ode_backward_order() const { return ode_backward_order_.get(); }
OptionValue<int>& Config::mutable_ode_backward_order() { return ode_backward_order_; }

double Config::ode_abs_tol() const { return ode_abs_tol_.get(); }
OptionValue<double>& Config::mutable_ode_abs_tol() { return ode_abs_tol_; }

double Config::ode_rel_tol() const { return ode_rel_tol_.get(); }
OptionValue<double>& Config::mutable_ode_rel_tol() { return ode_rel_tol_; }

int Config::ode_hull_grid() const { return ode_hull_grid_.get(); }
OptionValue<int>& Config::mutable_ode_hull_grid() { return ode_hull_grid_; }

OdeC0SetType Config::ode_c0_set() const { return ode_c0_set_.get(); }
OptionValue<OdeC0SetType>& Config::mutable_ode_c0_set() { return ode_c0_set_; }

bool Config::ode_backward() const { return ode_backward_.get(); }
OptionValue<bool>& Config::mutable_ode_backward() { return ode_backward_; }

double Config::ode_max_step() const { return ode_max_step_.get(); }
OptionValue<double>& Config::mutable_ode_max_step() { return ode_max_step_; }

bool Config::refine_witness() const { return refine_witness_.get(); }
OptionValue<bool>& Config::mutable_refine_witness() { return refine_witness_; }

std::ostream& operator<<(std::ostream& os,
                         const Config::SatDefaultPhase& sat_default_phase) {
  switch (sat_default_phase) {
    case Config::SatDefaultPhase::False:
      return os << "False";
    case Config::SatDefaultPhase::True:
      return os << "True";
    case Config::SatDefaultPhase::JeroslowWang:
      return os << "Jeroslow-Wang";
    case Config::SatDefaultPhase::RandomInitialPhase:
      return os << "Random Initial Phase";
  }
  DREAL_UNREACHABLE();
}

ostream& operator<<(ostream& os, const Config& config) {
  return os << fmt::format(
             "Config("
             "precision = {}, "
             "produce_model = {}, "
             "visualize = {}, "
             "use_polytope = {}, "
             "use_polytope_in_forall = {}, "
             "use_worklist_fixpoint = {}, "
             "use_local_optimization = {}, "
             "dump_theory_literals = {}, "
             "number_of_jobs = {}, "
             "nlopt_ftol_rel = {}, "
             "nlopt_ftol_abs = {}, "
             "nlopt_maxeval = {}, "
             "nlopt_maxtime = {}, "
             "sat_default_phase = {}, "
             "random_seed = {}"
             ")",
             config.precision(), config.produce_models(), config.visualize(), config.use_polytope(),
             config.use_polytope_in_forall(), config.use_worklist_fixpoint(),
             config.use_local_optimization(), config.dump_theory_literals(),
             config.number_of_jobs(), config.nlopt_ftol_rel(),
             config.nlopt_ftol_abs(), config.nlopt_maxeval(),
             config.nlopt_maxtime(), config.sat_default_phase(),
             config.random_seed());
}

}  // namespace dreal
