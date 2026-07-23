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
#include "dreal/contractor/contractor.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "dreal/contractor/contractor_cell.h"
#include "dreal/contractor/contractor_fixpoint.h"
#include "dreal/contractor/contractor_forall.h"
#include "dreal/contractor/contractor_ibex_acid.h"
#include "dreal/contractor/contractor_ibex_acid_mt.h"
#include "dreal/contractor/contractor_ibex_forall.h"
#include "dreal/contractor/contractor_ibex_forall_mt.h"
#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/contractor/contractor_ibex_fwdbwd_mt.h"
#include "dreal/contractor/contractor_ibex_mohc.h"
#include "dreal/contractor/contractor_ibex_mohc_mt.h"
#include "dreal/contractor/contractor_ibex_newton.h"
#include "dreal/contractor/contractor_ibex_newton_mt.h"
#include "dreal/contractor/contractor_ibex_obbt.h"
#include "dreal/contractor/contractor_ibex_obbt_mt.h"
#include "dreal/contractor/contractor_ibex_polytope.h"
#include "dreal/contractor/contractor_ibex_polytope_mt.h"
#include "dreal/contractor/contractor_id.h"
#include "dreal/contractor/contractor_integer.h"
#include "dreal/contractor/contractor_join.h"
#include "dreal/contractor/contractor_seq.h"
#include "dreal/contractor/contractor_worklist_fixpoint.h"
#include "dreal/util/exception.h"
#include "dreal/util/stat.h"
#include "odes/contractor_odes.h"

using std::any_of;
using std::cout;
using std::make_shared;
using std::ostream;
using std::shared_ptr;
using std::vector;

namespace dreal {

namespace {

// Flattens contractors and filters out ID contractors.
vector<Contractor> Flatten(const vector<Contractor>& contractors) {
  vector<Contractor> vec;
  vec.reserve(contractors.size());
  for (const Contractor& contractor : contractors) {
    const Contractor::Kind kind{contractor.kind()};
    if (kind == Contractor::Kind::ID) {
      // Skip if contractor == ID.
      continue;
    } else if (kind == Contractor::Kind::SEQ) {
      // Flatten it out if contractor == SEQ.
      const vector<Contractor>& contractors_inside{
          to_seq(contractor)->contractors()};
      vec.insert(vec.end(), contractors_inside.begin(),
                 contractors_inside.end());
    } else {
      vec.push_back(contractor);
    }
  }
  return vec;
}

// A class to show statistics information at destruction. We have a
// static instance in Contractor::Prune() to keep track of the number
// of pruning operations.
class ContractorStat : public Stat {
 public:
  explicit ContractorStat(const bool enabled) : Stat{enabled} {}
  ContractorStat(const ContractorStat&) = delete;
  ContractorStat(ContractorStat&&) = delete;
  ContractorStat& operator=(const ContractorStat&) = delete;
  ContractorStat& operator=(ContractorStat&&) = delete;
  ~ContractorStat() override {
    if (enabled()) {
      using fmt::print;
      print(cout, "{:<45} @ {:<20} = {:>15}\n", "Total # of Pruning",
            "Contractor level", num_prune_);
    }
  }

  void increase_prune() { increase(&num_prune_); }

 private:
  std::atomic<int> num_prune_{0};
};

}  // namespace

Contractor::Contractor(const Config& config)
    : ptr_{make_shared<ContractorId>(config)} {}

Contractor::Contractor(std::shared_ptr<ContractorCell> ptr)
    : ptr_{std::move(ptr)} {}

const DynamicBitset& Contractor::input() const { return ptr_->input(); }

void Contractor::Prune(ContractorStatus* cs, const UpwardRounding& ur) const {
  static ContractorStat stat{DREAL_LOG_INFO_ENABLED};
  if (stat.enabled()) {
    stat.increase_prune();
  }
  ptr_->Prune(cs, ur);
}

Contractor::Kind Contractor::kind() const { return ptr_->kind(); }

bool Contractor::include_forall() const { return ptr_->include_forall(); }

void Contractor::set_include_forall() { ptr_->set_include_forall(); }

Contractor make_contractor_id(const Config& config) {
  return Contractor{config};
}

Contractor make_contractor_integer(const Box& box, const Config& config) {
  if (any_of(box.variables().begin(), box.variables().end(),
             [](const Variable& v) {
               const Variable::Type type{v.get_type()};
               return type == Variable::Type::INTEGER ||
                      type == Variable::Type::BINARY;
             })) {
    return Contractor{make_shared<ContractorInteger>(box, config)};
  } else {
    return make_contractor_id(config);
  }
}

Contractor make_contractor_seq(const vector<Contractor>& contractors,
                               const Config& config) {
  return Contractor{make_shared<ContractorSeq>(Flatten(contractors), config)};
}

Contractor make_contractor_ibex_fwdbwd(Formula f, const Box& box,
                                       const Config& config) {
  if (config.number_of_jobs() > 1) {
    const auto ctc =
        make_shared<ContractorIbexFwdbwdMt>(std::move(f), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  } else {
    const auto ctc =
        make_shared<ContractorIbexFwdbwd>(std::move(f), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  }
}  // namespace dreal

Contractor make_contractor_ibex_polytope(vector<Formula> formulas,
                                         const Box& box, const Config& config) {
  if (config.number_of_jobs() > 1) {
    const auto ctc =
        make_shared<ContractorIbexPolytopeMt>(std::move(formulas), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  }
  const auto ctc =
      make_shared<ContractorIbexPolytope>(std::move(formulas), box, config);
  if (ctc->is_dummy()) {
    return make_contractor_id(config);
  } else {
    return Contractor{ctc};
  }
}

Contractor make_contractor_ibex_forall(Formula f, const Box& box,
                                       const Config& config) {
  // ibex::CtcForAll holds a mutable parameter-box worklist, so a single cell
  // cannot be shared across parallel ICP workers; the Mt variant builds one
  // ContractorIbexForall per worker thread (mirrors make_contractor_ibex_fwdbwd).
  if (config.number_of_jobs() > 1) {
    const auto ctc =
        make_shared<ContractorIbexForallMt>(std::move(f), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  } else {
    const auto ctc =
        make_shared<ContractorIbexForall>(std::move(f), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  }
}

Contractor make_contractor_ibex_acid(vector<Formula> formulas, const Box& box,
                                     const Config& config) {
  // ibex::CtcAcid keeps mutable shaving/adaptivity state, so a single cell
  // cannot be shared across parallel ICP workers; the Mt variant builds one
  // ContractorIbexAcid per worker thread (mirrors make_contractor_ibex_polytope).
  if (config.number_of_jobs() > 1) {
    const auto ctc =
        make_shared<ContractorIbexAcidMt>(std::move(formulas), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  }
  const auto ctc =
      make_shared<ContractorIbexAcid>(std::move(formulas), box, config);
  if (ctc->is_dummy()) {
    return make_contractor_id(config);
  } else {
    return Contractor{ctc};
  }
}

Contractor make_contractor_ibex_newton(vector<Formula> formulas, const Box& box,
                                       const Config& config) {
  // ibex::Function evaluation (the Jacobian + Gauss-Seidel work inside
  // ibex::newton) keeps mutable per-call state, so a single cell cannot be
  // shared across parallel ICP workers; the Mt variant builds one
  // ContractorIbexNewton per worker thread (mirrors
  // make_contractor_ibex_acid).
  if (config.number_of_jobs() > 1) {
    const auto ctc =
        make_shared<ContractorIbexNewtonMt>(std::move(formulas), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  }
  const auto ctc =
      make_shared<ContractorIbexNewton>(std::move(formulas), box, config);
  if (ctc->is_dummy()) {
    return make_contractor_id(config);
  } else {
    return Contractor{ctc};
  }
}

Contractor make_contractor_ibex_obbt(vector<Formula> formulas, const Box& box,
                                     const Config& config) {
  // The base ContractorIbexObbt owns a single mutable ibex::LPSolver (soplex
  // instance) and LinearizerXTaylor, so a single cell cannot be shared across
  // parallel ICP workers; the Mt variant builds one ContractorIbexObbt per
  // worker thread (mirrors make_contractor_ibex_polytope).
  if (config.number_of_jobs() > 1) {
    const auto ctc =
        make_shared<ContractorIbexObbtMt>(std::move(formulas), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  }
  const auto ctc =
      make_shared<ContractorIbexObbt>(std::move(formulas), box, config);
  if (ctc->is_dummy()) {
    return make_contractor_id(config);
  } else {
    return Contractor{ctc};
  }
}

Contractor make_contractor_ibex_mohc(vector<Formula> formulas, const Box& box,
                                     const Config& config) {
  // Each ibex::CtcMohcRevise keeps mutable per-call state (active_mono_proc,
  // Apply arrays, LB/RB, OG scratch) and CtcMohc::contract rewrites it every
  // call, so a single cell cannot be shared across parallel ICP workers; the
  // Mt variant builds one ContractorIbexMohc per worker thread (mirrors
  // make_contractor_ibex_acid). The amohc statics race benignly across
  // workers — documented in contractor_ibex_mohc.h.
  if (config.number_of_jobs() > 1) {
    const auto ctc =
        make_shared<ContractorIbexMohcMt>(std::move(formulas), box, config);
    if (ctc->is_dummy()) {
      return make_contractor_id(config);
    } else {
      return Contractor{ctc};
    }
  }
  const auto ctc =
      make_shared<ContractorIbexMohc>(std::move(formulas), box, config);
  if (ctc->is_dummy()) {
    return make_contractor_id(config);
  } else {
    return Contractor{ctc};
  }
}

Contractor make_contractor_fixpoint(TerminationCondition term_cond,
                                    const vector<Contractor>& contractors,
                                    const Config& config) {
  vector<Contractor> ctcs{Flatten(contractors)};
  if (ctcs.empty()) {
    return make_contractor_id(config);
  } else {
    return Contractor{make_shared<ContractorFixpoint>(std::move(term_cond),
                                                      std::move(ctcs), config)};
  }
}

Contractor make_contractor_worklist_fixpoint(
    TerminationCondition term_cond, const vector<Contractor>& contractors,
    const Config& config) {
  vector<Contractor> ctcs{Flatten(contractors)};
  if (ctcs.empty()) {
    return make_contractor_id(config);
  } else {
    return Contractor{make_shared<ContractorWorklistFixpoint>(
        std::move(term_cond), std::move(ctcs), config)};
  }
}

Contractor make_contractor_join(vector<Contractor> vec, const Config& config) {
  return Contractor{make_shared<ContractorJoin>(std::move(vec), config)};
}

Contractor mk_contractor_ode_lohner(Box const& box, const ode_constraint& ctr,
                                          ode_direction const dir, Config const& config,
                                          double const timeout) {
  // Safe to share one instance across parallel ICP workers: Prune is const
  // with no mutable members; CAPD IOdeSolver/ITimeMap are per-call and the
  // parsed IMap parameter copies are thread_local (contractor_odes_capd.cc);
  // the per-flow cache is mutex-guarded; AddInconclusiveOde writes each
  // worker's own ContractorStatus, unioned by InplaceJoin; and the nested
  // invariant contractors dispatch to their Mt variants under jobs > 1 via
  // make_contractor_ibex_fwdbwd.
  return Contractor{std::make_shared<contractor_ode_lohner>(box, ctr, dir, config, timeout)};
}

ostream& operator<<(ostream& os, const Contractor& ctc) {
  if (ctc.ptr_) {
    os << *(ctc.ptr_);
  }
  return os;
}

bool is_id(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::ID;
}
bool is_integer(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::INTEGER;
}
bool is_seq(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::SEQ;
}
bool is_ibex_fwdbwd(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::IBEX_FWDBWD;
}
bool is_ibex_polytope(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::IBEX_POLYTOPE;
}
bool is_fixpoint(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::FIXPOINT;
}
bool is_worklist_fixpoint(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::WORKLIST_FIXPOINT;
}
bool is_forall(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::FORALL;
}
bool is_join(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::JOIN;
}
bool is_ode_lohner(const Contractor& contractor) {
  return contractor.kind() == Contractor::Kind::ODE_LOHNER;
}

}  // namespace dreal
