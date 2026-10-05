#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "dreal/symbolic/hash.h"

namespace dreal {
namespace drake {
namespace symbolic {

/** Represents a symbolic variable. */
class Variable {
 public:
  typedef int Id;

  /** Supported types of symbolic variables. */
  // TODO(soonho-tri): refines the following descriptions.
  enum class Type {
    CONTINUOUS,  ///< A CONTINUOUS variable takes a `double` value.
    INTEGER,     ///< An INTEGER variable takes an `int` value.
    BINARY,      ///< A BINARY variable takes an integer value from {0, 1}.
    BOOLEAN,     ///< A BOOLEAN variable takes a `bool` value.
  };

  Variable(const Variable&) = default;
  Variable& operator=(const Variable&) = default;
  Variable(Variable&&) = default;
  Variable& operator=(Variable&&) = default;

  /** Default constructor. Constructs a dummy variable of CONTINUOUS type. This
   *  is needed to have Eigen::Matrix<Variable>. The objects created by the
   *  default constructor share the same ID, zero. As a result, they all are
   *  identified as a single variable by equality operator (==). They all have
   *  the same hash value as well.
   *
   *  It is allowed to construct a dummy variable but it should not be used to
   *  construct a symbolic expression.
   */
  Variable() : name_{std::make_shared<std::string>()} {}

  /** Default destructor. */
  ~Variable() = default;

  /** Constructs a variable with a string. If not specified, it has CONTINUOUS
   * type by default.*/
  explicit Variable(std::string name, Type type = Type::CONTINUOUS);

  /** Constructs a dummy variable with an ID. If not specified, it has CONTINUOUS
   * type by default.*/
  explicit Variable(Id dummy_id, Type type = Type::CONTINUOUS);

  /** Constructs a variable with @p name and @p type. @p model_variable is
   * ignored. */
  [[deprecated("This is only for backward-compatibility.")]] Variable(
      std::string name, Type type, bool model_variable);

  /** Checks if this is a dummy variable (ID = 0) which is created by
   *  the default constructor. */
  bool is_dummy() const { return get_id() == 0; }
  [[nodiscard]] inline Id get_id() const { return id_; }
  Type get_type() const;
  size_t get_hash() const { return std::hash<Id>{}(id_); }
  std::string get_name() const; // todo: use `string_view` ?
  std::string to_string() const;

  /// Checks the equality of two variables based on their ID values.
  bool equal_to(const Variable& v) const { return get_id() == v.get_id(); }

  /// Compares two variables based on their ID values.
  bool less(const Variable& v) const { return get_id() < v.get_id(); }

  friend std::ostream& operator<<(std::ostream& os, const Variable& var);

 private:
  // Produces a unique ID for a variable.
  static Id get_next_id();
  Id id_{};  // Unique identifier.
  Type type_{Type::CONTINUOUS};

  // Variable class has shared_ptr<string> instead of string to be
  // drake::test::IsMemcpyMovable.
  // Please check https://github.com/RobotLocomotion/drake/issues/5974
  // for more information.
  std::shared_ptr<std::string> name_;  // Name of variable.
};

std::ostream& operator<<(std::ostream& os, Variable::Type type);

/** Orders variables by Variable::less, and compares them by Variable::equal_to.
 *  They are the comparators of every container keyed by Variable:
 *  std::less<Variable> and std::equal_to<Variable> would call the symbolic
 *  operator< and operator==, which build a Formula, not a bool. */
struct VariableLess {
  bool operator()(const Variable& lhs, const Variable& rhs) const {
    return lhs.less(rhs);
  }
};

struct VariableEqualTo {
  bool operator()(const Variable& lhs, const Variable& rhs) const {
    return lhs.equal_to(rhs);
  }
};

using VariableSet = std::set<Variable, VariableLess>;
template <typename T>
using VariableMap = std::map<Variable, T, VariableLess>;
using VariableUnorderedSet =
    std::unordered_set<Variable, std::hash<Variable>, VariableEqualTo>;
template <typename T>
using VariableUnorderedMap =
    std::unordered_map<Variable, T, std::hash<Variable>, VariableEqualTo>;

}  // namespace symbolic

/** Computes the hash value of a variable. */
template <>
struct hash_value<symbolic::Variable> {
  size_t operator()(const symbolic::Variable& v) const { return v.get_hash(); }
};

}  // namespace drake
}  // namespace dreal

namespace std {
template <>
struct hash<dreal::drake::symbolic::Variable> {
  size_t operator()(const dreal::drake::symbolic::Variable& v) const {
    return v.get_hash();
  }
};

}  // namespace std
