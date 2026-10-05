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
#include <cstdint>
#include <istream>
#include <string>
#include <unordered_set>
#include <vector>

#include "dreal/symbolic/odes/OdeFlow.h"
#include "dreal/smt2/location.hh"
#include "dreal/smt2/scanner.h"
#include "dreal/smt2/sort.h"
#include "dreal/smt2/term.h"
#include "dreal/solver/context.h"
#include "dreal/util/exception.h"
#include "dreal/util/math.h"
#include "dreal/util/scoped_unordered_map.h"

namespace dreal {

class FunctionDefinition {
 public:
  FunctionDefinition(std::vector<Variable> parameters, Sort return_type,
                     Term body);

  Term operator()(const std::vector<Term>& arguments) const;

 private:
  std::vector<Variable> parameters_;
  Sort return_type_;
  Term body_;
};

/** The Smt2Driver class brings together all components. It creates an
 * instance of the Parser and Scanner classes and connects them. Then
 * the input stream is fed into the scanner object and the parser gets
 * it's token sequence. Furthermore the driver object is available in
 * the grammar rules as a parameter. Therefore the driver class
 * contains a reference to the structure into which the parsed data is
 * saved. */
class Smt2Driver {
 public:
  /// construct a new parser driver context
  Smt2Driver() = default;
  explicit Smt2Driver(Context context);

  /** Invoke the scanner and parser for a stream.
   * @param in	input stream
   * @param sname	stream name for error messages
   * @return		true if successfully parsed
   */
  bool parse_stream(std::istream& in,
                    const std::string& sname = "stream input");

  /** Invoke the scanner and parser on an input string.
   * @param input	input string
   * @param sname	stream name for error messages
   * @return		true if successfully parsed
   */
  bool parse_string(const std::string& input,
                    const std::string& sname = "string stream");

  /** Invoke the scanner and parser on a file. Use parse_stream with a
   * std::ifstream if detection of file reading errors is required.
   * @param filename	input file name
   * @return		true if successfully parsed
   */
  bool parse_file(const std::string& filename);

  // To demonstrate pure handling of parse errors, instead of
  // simply dumping them on the standard error output, we will pass
  // them to the driver using the following two member functions.

  /** Error handling with associated line number. This can be modified to
   * output the error e.g. to a dialog box. */
  static void error(const location& l, const std::string& m);

  /** General error handling. This can be modified to output the error
   * e.g. to a dialog box. */
  static void error(const std::string& m);

  /// Calls context_.CheckSat() and print proper output messages to cout.
  void CheckSat();

  /// Register a variable with name @p name and sort @p s in the scope. Note
  /// that it does not declare the variable in the context.
  Variable RegisterVariable(const std::string& name, Sort sort);

  /// Register a `forall`-bound variable. Same as RegisterVariable, but throws if
  /// @p name collides with a top-level declared (model) variable — that shadow
  /// leaves the outer variable unconstrained and silently mis-solves the query
  /// (QUIRK-001). Benign shadowing (no top-level variable of that name) is fine.
  Variable RegisterQuantifiedVariable(const std::string& name, Sort sort);

  /// Declare a variable with name @p name and sort @p sort.
  Variable DeclareVariable(const std::string& name, Sort sort);

  /// Declare a variable with name @p name and sort @p sort which is bounded by
  /// an interval `[lb, ub]`.
  void DeclareVariable(const std::string& name, Sort sort, const Term& lb,
                       const Term& ub);

  /// Declare a new variable with label @p name that is globally unique and
  /// cannot occur in an SMT-LIBv2 file.
  Variable DeclareLocalVariable(const std::string& name, Sort sort);

  /// Handles define-fun.
  void DefineFun(const std::string& name,
                 const std::vector<Variable>& parameters, Sort return_type,
                 const Term& body);

  /// Handles define-fun.
  void DefineOde(const std::string& flow_name,
                 const std::vector<std::pair<Variable, Expression>>& ode_list);

  /// Returns a representation of a model computed by the solver in
  /// response to an invocation of the check-sat.
  void GetModel() const;

  /// `GetValue([t1, t2, ..., tn])` returns a list of values [v1, v2,
  /// ..., vn] where v_i is equivalent to t_i in the current model.
  void GetValue(const std::vector<Term>& term_list) const;

  /// Handles `(get-option key)`.
  void GetOption(const std::string& key) const;

  /// Returns a variable associated with a name @p name.
  ///
  /// @throws if no variable is associated with @p name.
  const Variable& lookup_variable(const std::string& name);

  void PushScope() {
    scope_.push();
    function_definition_map_.push();
  }

  void PopScope() {
    function_definition_map_.pop();
    scope_.pop();
  }

  Term LookupFunction(const std::string& name,
                      const std::vector<Term>& arguments);

  const std::shared_ptr<const OdeFlow>& LookupOde(const std::string& name);
  const std::shared_ptr<const OdeFlow>& LookupOde(const std::int64_t id) {
      if (id < 0)
          throw DREAL_RUNTIME_ERROR("forall_t: the flow id must be non-negative, got {}", id);
      return LookupOde("flow_" + std::to_string(id));  // lint: allow int
  }

  static Variable ParseVariableSort(const std::string& name, Sort s);

  std::string MakeUniqueName(const std::string& name);

  bool trace_scanning() const { return trace_scanning_; }
  void set_trace_scanning(bool b) { trace_scanning_ = b; }

  bool trace_parsing() const { return trace_parsing_; }
  void set_trace_parsing(bool b) { trace_parsing_ = b; }

  Context& mutable_context() { return context_; }

  std::string& mutable_streamname() { return streamname_; }

  /** Pointer to the current scanenr instance, this is used to connect the
   * parser to the scanner. It is used in the yylex macro. */
  Smt2Scanner* scanner{nullptr};

  /// Eliminate Boolean variables `[b_1, …, b_n]` in @p vars from @p f
  /// by constructing `f[b ↦ true] ∧ f[b ↦ false]`. This is used in
  /// handling `forall` terms.
  static Formula EliminateBooleanVariables(const Variables& vars,
                                           const Formula& f);

 private:
  /// enable debug output in the flex scanner
  bool trace_scanning_{false};

  /// enable debug output in the bison parser
  bool trace_parsing_{false};

  /** Scoped map from a string to a corresponding Variable. */
  ScopedUnorderedMap<std::string, Variable> scope_;

  /** Names of top-level declared (model) variables — those passed through
   * DeclareVariable. Used to reject a `forall` binder that shadows one
   * (QUIRK-001). Monotonic: never shrinks (a declared name that later leaves
   * scope still triggers the guard, which is the fail-loud choice). */
  std::unordered_set<std::string> model_variable_names_;

  /** Scoped map from a string to a corresponding Variable. */
  ScopedUnorderedMap<std::string, FunctionDefinition> function_definition_map_;

  ScopedUnorderedMap<std::string, std::shared_ptr<const OdeFlow>> ode_definition_map_;

  /// Sequential value concatenated to names to make them unique.
  int64_t nextUniqueId_{};

  /// stream name (file or input stream) used for error messages.
  std::string streamname_;

  /** The context filled during parsing of the expressions. */
  Context context_;
};

}  // namespace dreal
