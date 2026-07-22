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
#include "dreal/solver/context.h"

#include <utility>

#include "dreal/version.h"
#include "dreal/solver/context_impl.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"

using std::make_unique;
using std::string;
using std::vector;

namespace dreal {

Context::Context() : Context{Config{}} {}

Context::Context(Context&& context) noexcept
    : impl_{std::move(context.impl_)} {}

Context::~Context() = default;

Context::Context(const Config& config) : impl_{make_unique<Impl>(config)} {}

void Context::Assert(const Formula& f) { impl_->Assert(f); }

optional<Box> Context::CheckSat() { return impl_->CheckSat(); }

void Context::DeclareVariable(const Variable& v, const bool is_model_variable) {
  impl_->DeclareVariable(v, is_model_variable);
}

void Context::DeclareVariable(const Variable& v, const Expression& lb,
                              const Expression& ub,
                              const bool is_model_variable) {
  impl_->DeclareVariable(v, is_model_variable);
  impl_->SetDomain(v, lb, ub);
}

void Context::Exit() { DREAL_LOG_DEBUG("Context::Exit()"); }

void Context::Minimize(const Expression& f) { impl_->Minimize({f}); }

void Context::Minimize(const vector<Expression>& functions) {
  impl_->Minimize(functions);
}

void Context::Maximize(const Expression& f) { impl_->Minimize({-f}); }

namespace {
// SMT-LIB2 push/pop is formally outside dReal4's supported fragment: the
// CaDiCaL SAT core cannot retract clauses, and learned theory lemmas are
// box-relative, so every sound retraction design either drops learned lemmas
// on pop or adds soundness-critical machinery. Incrementality is an
// encoder-side concern. Rationale + notes for any future re-attempt:
// docs/decisions.md §"SMT-LIB push/pop: formally unsupported".
[[noreturn]] void ThrowPushPopUnsupported() {
  throw DREAL_RUNTIME_ERROR(
      "push/pop is not supported: the CaDiCaL SAT core cannot retract "
      "clauses, and learned theory lemmas are box-relative (see "
      "docs/decisions.md \"SMT-LIB push/pop: formally unsupported\"). "
      "Re-emit a fresh script per query.");
}
}  // namespace

void Context::Pop(int) { ThrowPushPopUnsupported(); }

void Context::Push(int) { ThrowPushPopUnsupported(); }

void Context::SetInfo(const string& key, const double val) {
  impl_->SetInfo(key, val);
}

void Context::SetInfo(const string& key, const string& val) {
  impl_->SetInfo(key, val);
}

void Context::SetInterval(const Variable& v, const double lb, const double ub) {
  impl_->SetInterval(v, lb, ub);
}

void Context::SetLogic(const Logic& logic) { impl_->SetLogic(logic); }

void Context::SetOption(const string& key, const double val) {
  impl_->SetOption(key, val);
}

void Context::SetOption(const string& key, const string& val) {
  impl_->SetOption(key, val);
}

optional<string> Context::GetOption(const string& key) const {
  return impl_->GetOption(key);
}

const Config& Context::config() const { return impl_->config(); }
Config& Context::mutable_config() { return impl_->mutable_config(); }

string Context::version() {
  std::ostringstream oss;
  oss << DREAL_VERSION_FULL << '.';
  oss << DREAL_VERSION_MAJOR << '.';
  oss << DREAL_VERSION_MINOR << '.';
  oss << DREAL_VERSION_REVISION << '.';

#ifdef DREAL_EXPERIMENTAL_PM_USE_TRIE_IMPL
  oss << "PM_trie_";
#endif
#ifdef DREAL_EXPERIMENTAL_PM_USE_MAP_IMPL
  oss << "PM_map_";
#endif
#if DREAL_EXPERIMENTAL_PM_SUBSTREE_RANDOMIZE
  oss << "rand.";
#else
  oss << "sequ.";
#endif

  oss << "full_models" << (DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS ? 1 : 0) << '.';

  oss << "audit_pm_dump" << (DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED ? 1 : 0) << '.';
  oss << "audit_theory" << (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED ? 1 : 0) << '.';
  oss << "audit_sat" << (DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED ? 1 : 0);

#if CAV26_FILTER_SYMMETRIES
#define STRINGIFY(x) #x
#define STR(x) STRINGIFY(x)
  oss << ".CAV26_";
  if (CAV26_MATCH_PURE_TIME_SYM && CAV26_MATCH_PURE_LOGIC_SYM) oss << "PURE";
  if (CAV26_MATCH_PURE_TIME_SYM && !CAV26_MATCH_PURE_LOGIC_SYM) oss << "TIME";
  if (!CAV26_MATCH_PURE_TIME_SYM && CAV26_MATCH_PURE_LOGIC_SYM) oss << "LOGIC";
  if (!CAV26_MATCH_PURE_TIME_SYM && !CAV26_MATCH_PURE_LOGIC_SYM) oss << "MIXED";
  oss << "_SYM_using_" STR(CAV26_VARNAME_PARSER);
#undef STR
#undef STRINGIFY
#endif

  return oss.str();
}

const Box& Context::box() const { return impl_->box(); }

const Box& Context::get_model() const { return impl_->get_model(); }

const ScopedVector<Formula>& Context::assertions() const {
  return impl_->assertions();
}

}  // namespace dreal
