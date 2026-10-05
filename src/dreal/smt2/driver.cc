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
#include "dreal/smt2/driver.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <fmt/format.h>
#include <fmt/std.h>
#include <fmt/ostream.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-literal-operator"
#include <gmpxx.h>
#pragma clang diagnostic pop

#include <dreal/util/json_guarded.h>
#include <dreal/util/rounded_format.h>
#include <dreal/util/rounding.h>

#include "dreal/contractor/odes/contractor_odes.h"
#include "dreal/smt2/scanner.h"
#include "dreal/solver/expression_evaluator.h"
#include "dreal/symbolic/prefix_printer.h"
#include "dreal/util/optional.h"
#include "dreal/util/precision_guard.h"
#include "nlohmann/json.hpp"

namespace dreal {

using std::cerr;
using std::cin;
using std::cout;
using std::ifstream;
using std::istream;
using std::istringstream;
using std::ostream;
using std::ostringstream;
using std::runtime_error;
using std::string;
using std::stringstream;
using std::vector;
using nlohmann::json;

FunctionDefinition::FunctionDefinition(vector<Variable> parameters,
                                       Sort return_type, Term body)
    : parameters_{std::move(parameters)},
      return_type_{return_type},
      body_{std::move(body)} {}

Term FunctionDefinition::operator()(const vector<Term>& arguments) const {
  if (parameters_.size() != arguments.size()) {
    throw runtime_error{
        fmt::format("This function definition expects {} arguments whereas the "
                    "provided arguments are of length {}.",
                    parameters_.size(), arguments.size())};
  }

  body_.Check(return_type_);
  Term t = body_;
  for (size_t i = 0; i < parameters_.size(); ++i) {
    const Variable& param_i{parameters_[i]};
    const Term& arg_i{arguments[i]};
    arg_i.Check(param_i.get_type());
    t = t.Substitute(param_i, arg_i);
  }

  return t;
}

Smt2Driver::Smt2Driver(Context context) : context_{std::move(context)} {}

void Smt2Driver::parse_stream(istream& in, const string& sname) {
  streamname_ = sname;

  Smt2Scanner new_scanner(&in);
  new_scanner.set_debug(trace_scanning_);
  this->scanner = &new_scanner;

  Smt2Parser parser(*this);
  parser.set_debug_level(trace_parsing_);
  // The syntax error, or the rule that aborted, has already printed its
  // location and reason to std::cerr.
  if (parser.parse() != 0) {
    throw DREAL_RUNTIME_ERROR("Failed to parse {}.", sname);
  }
}

void Smt2Driver::parse_file(const string& filename) {
  if (filename.empty()) {
    // Option --in passed to dreal.
    parse_stream(cin, "(stdin)");
    return;
  }
  ifstream in(filename.c_str());
  if (!in.good()) {
    throw DREAL_RUNTIME_ERROR("Cannot open {}.", filename);
  }
  parse_stream(in, filename);
}

void Smt2Driver::parse_string(const string& input, const string& sname) {
  istringstream iss(input);
  parse_stream(iss, sname);
}

void Smt2Driver::error(const location& l, const string& m) {
  cerr << l << " : " << m << "\n";
}

void Smt2Driver::error(const string& m) { cerr << m << "\n"; }

void Smt2Driver::CheckSat() {
  const optional<Box> model{context_.CheckSat()};
  if (model) {
    const NearestRoundingScope g; // for printing precision correctly
    const NearestRounding nr{g.token()};
    if (context_.config().smtlib2_compliant()) {
      cout << "delta-sat\n";
    } else {
      cout << "delta-sat with delta = " << context_.config().precision()
           << "\n";
      if (context_.config().produce_models()) {
        PrecisionGuard precision_guard(&cout);
        cout << *model << "\n";
      }
    }

    // --visualize
    if (context_.config().visualize()) {
      try {
        const auto filename = streamname_ + ".json";
        std::ofstream nra_json_out;
        nra_json_out.open(filename, std::ofstream::out | std::ofstream::trunc);
        if (nra_json_out.fail()) {
          cout << "Cannot create a file: " << filename << '\n';
          exit(1);
        }

        json traces = {};
        // Need to run ODE pruning operator once again to generate a trace
        const auto odes = link_integral_invariants(context_.assertions());
        for (const auto& ctr : odes) {
          Contractor fwd_full = mk_contractor_ode_lohner(*model, ctr, ode_direction::FWD, context_.config(), 0.0);
          ContractorStatus cs(*model);
          json trace = to_ode_lohner(fwd_full)->generate_trace(/*copy*/cs);
          traces.push_back(trace);
        }
        json vis_json;
        vis_json["traces"] = traces;

        // nlohmann serializes the trajectory doubles to decimal here; route
        // through the token-gated dump_json so FE_TONEAREST is proven.
        nra_json_out << dump_json(vis_json, nr) << '\n';
      } catch (std::exception const & e) {
        DREAL_LOG_CRITICAL("The following exception is generated while computing "
                           "a trace (visualization).");
        DREAL_LOG_CRITICAL(e.what());
        DREAL_LOG_CRITICAL("This indicates that this delta-sat result is not "
                           "properly checked by ODE pruning operators.");
        DREAL_LOG_CRITICAL("Please try with a smaller precision using the "
                           "--precision option (current precision = {}).", context_.config().precision());
      }
    }

  } else {
    cout << "unsat\n";
  }
  cout.flush();
}

namespace {
ostream& PrintModel(ostream& os, const Box& box) {
  const NearestRoundingScope g; // for printing double
  const NearestRounding nr{g.token()};
  PrecisionGuard precision_guard(&os);
  os << "(model\n";
  for (int i = 0; i < box.size(); ++i) {
    const Variable& var{box.variable(i)};
    os << "  (define-fun " << var << " () ";
    switch (var.get_type()) {
      case Variable::Type::CONTINUOUS:
        os << Sort::Real;
        break;
      case Variable::Type::BINARY:
        os << Sort::Binary;
        break;
      case Variable::Type::INTEGER:
        os << Sort::Int;
        break;
      case Variable::Type::BOOLEAN:
        os << Sort::Bool;
        break;
    }
    os << " ";
    const Box::Interval& iv{box[i]};
    if (var.get_type() == Variable::Type::BOOLEAN) {
      if (iv == Box::Interval::ONE) {
        os << "true";
      } else if (iv == Box::Interval::ZERO) {
        os << "false";
      } else {
        // Don't-care: the SAT layer never assigned it — both values satisfy.
        os << "[false, true]";
      }
    } else {
      if (iv.is_degenerated()) {
        format_double(os, iv.lb(), nr);
      } else {
        // Non-degenerate interval: ibex's own interval operator<< formats both
        // endpoints, but it clobbers the FPU rounding mode (leaves a directed
        // mode); contain it in an ExpectClobber nearest scope. See ExpectClobber.
        const NearestRoundingScope interval_print{expect_clobber};
        os << iv;
      }
    }
    os << ")\n";
  }
  return os << ")";
}

string ToString(const mpz_class& z) {
  NearestRoundingScope g;
  if (sgn(z) == -1) {
    return fmt::format("(- {})", fmt::streamed(-z));
  }
  return fmt::format("{}", fmt::streamed(z));
}

string ToRational(const double d) {
  NearestRoundingScope g;
  const mpq_class r{d};
  if (r.get_den() == 1) {
    return fmt::format("{}", ToString(r.get_num()));
  } else {
    return fmt::format("(/ {} {})", ToString(r.get_num()),
                       ToString(r.get_den()));
  }
}

// Returns the string representation of @p interval.
// It returns `(exact c)` or `(interval lb ub)`.
string ToString(const Box::Interval& interval) {
  if (interval.lb() == interval.ub()) {
    return ToRational(interval.lb());
  } else {
    return fmt::format("(interval (closed {}) (closed {}))",
                       ToRational(interval.lb()), ToRational(interval.ub()));
  }
}
}  // namespace

void Smt2Driver::GetModel() const {
  const Box& box{context_.get_model()};
  if (box.empty()) {
    cout << "(error \"model is not available\")\n";
  } else {
    PrintModel(cout, box) << "\n";
  }
  cout.flush();
}

void Smt2Driver::GetValue(const vector<Term>& term_list) const {
  const Box& box{context_.get_model()};
  // std::cout (not fmt::print-to-stdout) like every other driver print, so
  // the output honors a redirected cout (the smt2 string-test harness).
  cout << "(\n";
  for (const auto& term : term_list) {
    string term_str;
    string value_str;
    stringstream ss;
    PrefixPrinter pp{ss};

    switch (term.type()) {
      case Term::Type::EXPRESSION: {
        const Expression& e{term.expression()};
        const ExpressionEvaluator evaluator{e};
        pp.Print(e);
        term_str = ss.str();
        // Interval (gaol) evaluation needs FE_UPWARD; ToString below formats
        // under FE_TONEAREST. Scope the upward phase tightly around the eval.
        const Box::Interval iv{[&] {
          const UpwardRoundingScope eval_scope;
          return ExpressionEvaluator(term.expression())(box, eval_scope.token());
        }()};
        value_str = ToString(iv);
        break;
      }
      case Term::Type::FORMULA: {
        const Formula& f{term.formula()};
        pp.Print(f);
        term_str = ss.str();
        if (is_variable(f)) {
          const Box::Interval& iv{box[get_variable(f)]};
          value_str = iv == Box::Interval::ONE    ? "true"
                      : iv == Box::Interval::ZERO ? "false"
                                                  // don't-care: never assigned
                                                  : "[false, true]";
        } else {
          throw std::runtime_error(fmt::format(
              "get-value does not handle a compound formula {}.", term_str));
        }
        break;
      }
    }
    cout << "\t(" << term_str << " " << value_str << ")\n";
  }
  cout << ")\n";
  cout.flush();
}

void Smt2Driver::GetOption(const string& key) const {
  const optional<string> value{context_.GetOption(key)};
  if (value) {
    fmt::print("{}\n", *value);
  } else {
    fmt::print("unsupported\n");
  }
  cout.flush();
}

Variable Smt2Driver::RegisterVariable(const string& name, const Sort sort) {
  Variable v{ParseVariableSort(name, sort)};
  scope_.insert(v.get_name(), v);
  return v;
}

Variable Smt2Driver::RegisterQuantifiedVariable(const string& name,
                                                const Sort sort) {
  if (model_variable_names_.count(name) != 0) {
    throw DREAL_RUNTIME_ERROR(
        "forall-bound variable '{}' shadows a top-level declared variable of "
        "the same name. The outer '{}' would be left unconstrained and the "
        "query silently mis-solved; rename one of them.",
        name, name);
  }
  return RegisterVariable(name, sort);
}

Variable Smt2Driver::DeclareVariable(const string& name, const Sort sort) {
  Variable v{RegisterVariable(name, sort)};
  context_.DeclareVariable(v);
  model_variable_names_.insert(name);
  return v;
}

void Smt2Driver::DeclareVariable(const string& name, const Sort sort,
                                 const Term& lb, const Term& ub) {
  const Variable v{RegisterVariable(name, sort)};
  context_.DeclareVariable(v, lb.expression(), ub.expression());
  model_variable_names_.insert(name);
}

void Smt2Driver::DefineFun(const string& name,
                           const vector<Variable>& parameters, Sort return_type,
                           const Term& body) {
  FunctionDefinition func{parameters, return_type, body};
  function_definition_map_.insert(name, func);
}

void Smt2Driver::DefineOde(const std::string& flow_name,
                           const std::vector<std::pair<Variable, Expression>>& ode_list) {
  if (ode_definition_map_.count(flow_name) != 0) {
    throw DREAL_RUNTIME_ERROR("Scoped/shadowed define-ode's are not well supported yet.");
  }
  ode_definition_map_.insert(flow_name, std::make_shared<OdeFlow>(flow_name, ode_list));
}

string Smt2Driver::MakeUniqueName(const string& name) {
  ostringstream oss;
  // The \ character ensures that the name cannot occur in an SMT-LIBv2 file.
  oss << "L" << nextUniqueId_++ << "\\" << name;
  return oss.str();
}

Term Smt2Driver::LookupFunction(const string& name,
                                const vector<Term>& arguments) {
  const auto it = function_definition_map_.find(name);
  if (it != function_definition_map_.end()) {
    return it->second(arguments);
  } else {
    throw runtime_error{fmt::format("No function definition for {}.", name)};
  }
}

const std::shared_ptr<const OdeFlow>& Smt2Driver::LookupOde(const string& name) {
  const auto it = ode_definition_map_.find(name);
  if (it != ode_definition_map_.end()) return it->second;
  throw runtime_error{fmt::format("No ODE definition for {}.", name)};
}

Variable Smt2Driver::DeclareLocalVariable(const string& name, const Sort sort) {
  Variable v{ParseVariableSort(MakeUniqueName(name), sort)};
  scope_.insert(name, v);  // v is not inserted under its own name.
  context_.DeclareVariable(
      v, false /* This local variable is not a model variable. */);
  return v;
}

const Variable& Smt2Driver::lookup_variable(const string& name) {
  const auto it = scope_.find(name);
  if (it == scope_.cend()) {
    throw DREAL_RUNTIME_ERROR("{} is an undeclared variable.", name);
  }
  return it->second;
}

Variable Smt2Driver::ParseVariableSort(const string& name, const Sort s) {
  return Variable{name, SortToType(s)};
}

Formula Smt2Driver::EliminateBooleanVariables(const Variables& vars,
                                              const Formula& f) {
  Formula ret{f};
  for (const auto& b : vars) {
    if (b.get_type() == Variable::Type::BOOLEAN) {
      ret = ret.Substitute(b, Formula::True()) &&
            ret.Substitute(b, Formula::False());
    }
  }
  return ret;
}

}  // namespace dreal
