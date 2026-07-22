// SMT-LIB2 push/pop is formally OUTSIDE dReal4's supported fragment (see
// docs/decisions.md §"SMT-LIB push/pop: formally unsupported"). The CaDiCaL SAT
// core cannot retract clauses and learned theory lemmas are box-relative, so
// every sound retraction design either drops learned lemmas on pop or adds
// soundness-critical machinery; with no incremental consumer, the decision is a
// clean, early, documented rejection — incrementality is an encoder-side concern.
//
// These tests pin that contract:
//  - (push N) / (pop N) surface the documented rejection message, for every N
//    (including 0 — the whole command is out of fragment, no no-op carve-out);
//  - the rejection happens BEFORE any state mutation, so a Context that caught
//    the throw still solves correctly afterwards.
//
// Pre-fix red states (recorded before the Context::Push/Pop rewrite):
//  - (push 1): "NOT YET IMPLEMENTED SatSolver::Push()" from deep in the SAT layer.
//  - (pop 1): bare "Nothing to pop." from ScopedVector — and Context::Impl::Pop
//    popped stack_/boxes_ BEFORE the throwing SAT call, corrupting the context.

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// RAII redirect of std::cout, restored on BOTH the normal and the exception
// paths (parse_string throws on the rejected commands under test).
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

std::string RunSmt2String(const std::string& smt2) {
  Config config;
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  const CoutRedirect redirect{captured.rdbuf()};
  driver.parse_string(smt2);
  return captured.str();
}

// Expect the documented rejection when parsing @p smt2.
void ExpectPushPopRejection(const std::string& smt2) {
  try {
    const std::string out{RunSmt2String(smt2)};
    FAIL() << "expected the documented push/pop rejection, but parsing "
              "succeeded with output: "
           << out;
  } catch (const std::runtime_error& e) {
    const std::string msg{e.what()};
    EXPECT_NE(msg.find("push/pop is not supported"), std::string::npos)
        << "message: " << msg;
  }
}

TEST(PushPopUnsupported, PushRejectedCleanly) {
  ExpectPushPopRejection(
      "(set-logic QF_NRA)\n"
      "(declare-fun x () Real)\n"
      "(push 1)\n");
}

TEST(PushPopUnsupported, PopRejectedCleanly) {
  ExpectPushPopRejection(
      "(set-logic QF_NRA)\n"
      "(declare-fun x () Real)\n"
      "(pop 1)\n");
}

TEST(PushPopUnsupported, PushZeroRejected) {
  // The whole command is out of fragment: no SMT-LIB n=0 no-op carve-out.
  ExpectPushPopRejection(
      "(set-logic QF_NRA)\n"
      "(push 0)\n");
}

// The rejection must precede ALL state mutation: after catching Push/Pop
// throws, the Context still solves correctly. (Pre-fix, Pop popped the
// assertion/box stacks before throwing, leaving the context corrupted.)
TEST(PushPopUnsupported, ContextUsableAfterRejectedPushPop) {
  Config config;
  config.mutable_precision() = 0.001;
  Context context{config};
  const Variable x{"x"};
  context.DeclareVariable(x, Expression{0.0}, Expression{10.0});

  EXPECT_THROW(context.Push(1), std::runtime_error);
  EXPECT_THROW(context.Pop(1), std::runtime_error);

  context.Assert(x >= 5);
  const auto result{context.CheckSat()};  // [0,10] ∧ x≥5: delta-sat
  ASSERT_TRUE(result.has_value());

  context.Assert(x <= 4);
  const auto result2{context.CheckSat()};  // … ∧ x≤4: unsat
  EXPECT_FALSE(result2.has_value());
}

}  // namespace
}  // namespace dreal
