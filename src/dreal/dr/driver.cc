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
#include "dreal/dr/driver.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <dreal/util/rounded_format.h>
#include <dreal/util/rounded_interval.h>
#include <dreal/util/rounding.h>

#include "dreal/dr/scanner.h"
#include "dreal/solver/expression_evaluator.h"
#include "dreal/util/exception.h"
#include "dreal/util/optional.h"

namespace dreal {

using std::cerr;
using std::cin;
using std::cout;
using std::endl;
using std::ifstream;
using std::istream;
using std::istringstream;
using std::string;

DrDriver::DrDriver(Context context) : context_{std::move(context)} {}

void DrDriver::parse_stream(istream& in, const string& sname) {
  streamname_ = sname;

  DrScanner scanner(&in);
  scanner.set_debug(trace_scanning_);
  this->scanner_ = &scanner;

  DrParser parser(*this);
  parser.set_debug_level(trace_parsing_);
  // The syntax error, or the rule that aborted, has already printed its
  // location and reason to std::cerr.
  if (parser.parse() != 0) {
    throw DREAL_RUNTIME_ERROR("Failed to parse {}.", sname);
  }
}

void DrDriver::parse_file(const string& filename) {
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

void DrDriver::parse_string(const string& input, const string& sname) {
  istringstream iss(input);
  parse_stream(iss, sname);
}

void DrDriver::error(const location& l, const string& m) {
  cerr << l << " : " << m << '\n';
}

void DrDriver::error(const string& m) { cerr << m << '\n'; }

const Variable& DrDriver::lookup_variable(const std::string& name) {
  const auto it = scope_.find(name);
  if (it == scope_.cend()) {
    throw DREAL_RUNTIME_ERROR("{} is an undeclared variable.", name);
  }
  return it->second;
}

void DrDriver::DeclareVariable(const Variable& v) {
  scope_.insert(v.get_name(), v);
  context_.DeclareVariable(v);
}

void DrDriver::DeclareVariable(const Variable& v, const Expression& lb,
                               const Expression& ub) {
  scope_.insert(v.get_name(), v);
  context_.DeclareVariable(v, lb, ub);
}

void DrDriver::Assert(const Formula& f) { constraints_.push_back(f); }

void DrDriver::Minimize(const Expression& f) { objectives_.push_back(f); }

void DrDriver::Solve() {
  // Add constraints.
  for (const Formula& f : constraints_) {
    context_.Assert(f);
  }
  // Add objective functions.
  if (!objectives_.empty()) {
    context_.Minimize(objectives_);
  }
  const optional<Box> model{context_.CheckSat()};
  if (model) {
    const NearestRoundingScope g; // for printing precision correctly
    const NearestRounding nr{g.token()};
    cout << "delta-sat with delta = " << context_.config().precision() << '\n';
    if (context_.config().produce_models()) {
      cout << *model << '\n';
      for (const Expression& f : objectives_) {
        // Interval (gaol) evaluation + safe_mid need FE_UPWARD; capture the
        // scalar under a tight upward scope, then print it under the nearest
        // scope `g` above.
        const double minimum{[&] {
          const UpwardRoundingScope eval_scope;
          const UpwardRounding eval_ur{eval_scope.token()};
          return safe_mid(ExpressionEvaluator(f)(*model, eval_ur), eval_ur);
        }()};
        cout << "Found minimum for " << f << " is ";
        format_double(cout, minimum, nr);
        cout << '\n';
      }
    }
  } else {
    cout << "unsat" << '\n';
  }
  Context::Exit();
}

}  // namespace dreal
