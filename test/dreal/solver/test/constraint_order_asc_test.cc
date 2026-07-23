// `--constraint-order asc` (kAsc, fewest-variables-first) coverage — the mirror
// of the kDesc soundness test (constraint_order_soundness_test.cc; full
// write-up: docs/constraint-order-explanation-soundness.md). Two layers:
//
//   * UNIT (microseconds) — the reorder itself, observed from the PUBLIC
//     surface: `TheorySolver::BuildContractor` (public, theory_solver.h)
//     returns the assembled fixpoint contractor, and
//     `ContractorFixpoint::display` streams its inner contractors IN ORDER —
//     so distinctively-named variables make each constraint's
//     `IbexFwdbwd(...)` rendering findable by substring, and relative
//     positions pin the `stable_sort` by `input().count()`
//     (theory_solver.cc). Declaration order deliberately puts the
//     many-variable constraint FIRST, so kAsc is the one order that must
//     flip it (kNone and kDesc both keep the declaration order here).
//   * INTEGRATION (~3 s) — verdict parity on the stiff C2E2 BMC instance
//     that caught the kDesc bug: `none` and `asc` must both return the
//     witnessed delta-sat. kAsc replays the same CAPD-divergence/splice
//     machinery in a different conflict order; a flip here would be the
//     kAsc analogue of that bug (SOUNDNESS — asserts φ T-unsatisfiable on
//     a T-satisfiable φ — false unsat).

#include "dreal/solver/theory_solver.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor.h"
#include "dreal/contractor/contractor_status.h"
#include "dreal/smt2/driver.h"
#include "dreal/solver/config.h"
#include "dreal/solver/context.h"
#include "dreal/solver/test/c2e2_stiff_smt2.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/optional.h"

namespace dreal {
namespace {

// A box whose variables all have finite (non-empty) intervals (mirrors the
// kDesc test's helper).
Box BoxOf(const std::vector<Variable>& vars) {
  Box box;
  for (const Variable& v : vars) box.Add(v, -10.0, 10.0);
  return box;
}

// ── UNIT — the reorder, observed through BuildContractor's display ───────────

// Streams the fixpoint contractor BuildContractor assembles for
// {f_many, f_few} (declaration order: many-variable constraint FIRST) under
// the given constraint order.
std::string ContractorDisplay(const ConstraintOrder order) {
  // Distinctive multi-char names so each constraint's rendering is findable
  // by substring (single letters collide with "Fixpoint"/"IbexFwdbwd" text).
  const Variable few_a{"ord_few_a"}, few_b{"ord_few_b"};
  const Variable many_a{"ord_many_a"}, many_b{"ord_many_b"},
      many_c{"ord_many_c"}, many_d{"ord_many_d"};

  // Multi-variable relationals: FilterAssertion only absorbs single-variable
  // bound atoms, so both survive to become ibex fwdbwd contractors.
  const Formula f_few{few_a * few_b <= 0.5};                       // 2 vars
  const Formula f_many{many_a + many_b * many_c + many_d <= 1.0};  // 4 vars

  Config config;
  config.mutable_constraint_order() = order;
  TheorySolver theory_solver{config};
  ContractorStatus cs{BoxOf({few_a, few_b, many_a, many_b, many_c, many_d})};
  const optional<Contractor> ctc{
      theory_solver.BuildContractor({f_many, f_few}, &cs)};
  // Neither constraint can empty a [-10,10] box at build time, so the
  // contractor is always assembled.
  EXPECT_TRUE(ctc);  // INTEGRATION-VERIFY: BuildContractor returns a value here
  if (!ctc) return "";
  std::ostringstream oss;
  oss << *ctc;
  return oss.str();
}

// Position of a constraint's rendering in the display, asserting presence.
std::size_t PosOf(const std::string& out, const std::string& name) {
  const std::size_t pos{out.find(name)};
  // INTEGRATION-VERIFY: IbexConverter keeps dreal variable names verbatim in
  // the ibex NumConstraint rendering (`IbexFwdbwd(<ctr>)`).
  EXPECT_NE(pos, std::string::npos)
      << "`" << name << "` missing from contractor display: " << out;
  return pos;
}

// THE KNOB: kAsc must run the 2-variable constraint before the 4-variable one
// — the only order that flips the declaration order, so this discriminates
// "sort ran" from "sort was a no-op".
TEST(ConstraintOrderAsc, AscSortsFewestVariablesFirst) {
  const std::string out{ContractorDisplay(ConstraintOrder::kAsc)};
  EXPECT_LT(PosOf(out, "ord_few_a"), PosOf(out, "ord_many_a"))
      << "kAsc (fewest-variables-first) must place the 2-var constraint "
         "ahead of the 4-var one; display: "
      << out;
}

// BASELINE: kNone keeps declaration order (many-variable constraint first).
TEST(ConstraintOrderAsc, NoneKeepsDeclarationOrder) {
  const std::string out{ContractorDisplay(ConstraintOrder::kNone)};
  EXPECT_LT(PosOf(out, "ord_many_a"), PosOf(out, "ord_few_a"))
      << "kNone must keep declaration order; display: " << out;
}

// CONSISTENCY: kDesc sorts most-variables-first (here identical to
// declaration order — the stable_sort must not disturb it).
TEST(ConstraintOrderAsc, DescSortsMostVariablesFirst) {
  const std::string out{ContractorDisplay(ConstraintOrder::kDesc)};
  EXPECT_LT(PosOf(out, "ord_many_a"), PosOf(out, "ord_few_a"))
      << "kDesc (most-variables-first) must place the 4-var constraint "
         "ahead of the 2-var one; display: "
      << out;
}

// ── INTEGRATION — kAsc on the stiff C2E2 instance, end-to-end (~3 s) ─────────

// Exception-safe stdout capture (the solver prints its verdict to std::cout).
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

// Solve an inline SMT2 string at the given constraint order (precision 0.001,
// where the kDesc flip reproduced). Parsed from the string — no external file.
std::string SolveString(const std::string& smt2, ConstraintOrder order) {
  Config config;
  config.mutable_precision() = 0.001;
  config.mutable_constraint_order() = order;
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  {
    const CoutRedirect redirect{captured.rdbuf()};
    driver.parse_string(smt2);
  }
  return captured.str();
}

bool HasDeltaSat(const std::string& out) {
  return out.find("delta-sat") != std::string::npos;
}
// "unsat" as its own verdict — "delta-sat" contains "sat" but not "unsat".
bool HasUnsat(const std::string& out) {
  return out.find("unsat") != std::string::npos &&
         out.find("delta-sat") == std::string::npos;
}

// The 5-step stiff C2E2 BMC reproducer, kC2E2Stiff — single canonical copy in
// the shared header dreal/solver/test/c2e2_stiff_smt2.h (instance anatomy:
// the NOTE in constraint_order_soundness_test.cc). CAPD diverges on
// essentially every box, so every constraint order drives the
// inconclusive-ODE splice — this instance is the hardest known exerciser of
// order-dependent explanation building.

TEST(ConstraintOrderAsc, DivergingOdeNoFalseUnsatEndToEnd) {
  const std::string none{SolveString(kC2E2Stiff, ConstraintOrder::kNone)};
  const std::string asc{SolveString(kC2E2Stiff, ConstraintOrder::kAsc)};
  EXPECT_TRUE(HasDeltaSat(none)) << "instance is genuinely delta-sat";
  EXPECT_TRUE(HasDeltaSat(asc)) << "asc must not flip to a false `unsat`";
  EXPECT_FALSE(HasUnsat(asc));
}

}  // namespace
}  // namespace dreal
