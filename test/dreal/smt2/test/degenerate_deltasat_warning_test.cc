// Loud-surface guards for the two silent degenerate delta-sat mechanisms
// catalogued in upstream dreal/dreal4 issues #68 and #265. Both are
// COMPLETENESS hazards (the solver asserts phi^delta T-satisfiable on a
// possibly T-unsatisfiable phi — a missed refutation); neither is a
// SOUNDNESS bug (no false unsat is involved), and the fix here changes NO
// verdict: it only adds a stderr warning. stdout (consumed by downstream
// substring checks) and the exit path stay byte-identical.
//
//   #68  — a box that still violates the delta-condition but cannot be
//          bisected further (endpoints are adjacent floats at large
//          magnitude) is returned as delta-sat from the ICP loop with only
//          a DREAL_LOG_DEBUG note, invisible at default verbosity. Both the
//          single-threaded and pooled (icp_parallel.cc Worker)
//          exits are exercised below.
//   #265 — unbounded variable intervals admit +-inf endpoints in the
//          delta-sat witness (extended-real interval arithmetic "satisfies"
//          the constraint only at infinity). Upstream-acknowledged design
//          limit; the mitigation is bounding every real, which the warning
//          tells the user. Checked ONCE where the final model box is
//          extracted (context_impl.cc), covering both Icp variants.
//
// Each test asserts (a) stderr CONTAINS the warning and (b) the verdict on
// stdout is still delta-sat.

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// RAII stream redirects, restored on BOTH the normal and exception paths
// (same rationale as CoutRedirect in dreal_bugs_regression_test.cc: a plain
// restore-after-parse is skipped during stack unwinding, leaving the stream
// pointing at a destroyed local buffer).
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

struct CerrRedirect {
  explicit CerrRedirect(std::streambuf* buf) : old_{std::cerr.rdbuf(buf)} {}
  ~CerrRedirect() { std::cerr.rdbuf(old_); }
  std::streambuf* old_;
};

struct CapturedRun {
  std::string out;  // stdout: verdict (+ model when produce_models)
  std::string err;  // stderr: where the degenerate-delta-sat warning goes
};

// Parse an SMT2 string, capturing stdout and stderr separately. Default
// Config precision is 0.001 — the delta upstream issue #68 reports against.
CapturedRun RunSmt2String(const std::string& smt2,
                          const Config& config = Config{}) {
  Smt2Driver driver{Context{config}};
  std::ostringstream captured_out;
  std::ostringstream captured_err;
  const CoutRedirect out_redirect{captured_out.rdbuf()};
  const CerrRedirect err_redirect{captured_err.rdbuf()};
  driver.parse_string(smt2);
  return CapturedRun{captured_out.str(), captured_err.str()};
}

// The verbatim instance from dreal/dreal4#68 (`:status unsat`, violated by
// ~1e10 >> delta = 0.001). At the 1e30 magnitude the ~1e14 float spacing
// swamps the 1e10 gap, so `x0 = ?v_0` and `x0 > ?v_0 + 1e10` stay
// interval-consistent on boxes that can no longer be bisected — the only
// route to the (degenerate) delta-sat verdict is the non-bisectable ICP
// exit.
constexpr const char* kIssue68Smt2 =
    "(set-info :status unsat)\n"
    "(declare-fun x0 () Real)\n"
    "(declare-fun ?v_0 () Real)\n"
    "(assert (>= x0 0))\n"
    "(assert (= ?v_0 x0))\n"
    "(assert (> x0 (+ ?v_0 1e10)))\n"
    "(assert (<= x0 1e30))\n"
    "(check-sat)\n"
    "(exit)\n";

// #68, single-threaded path (default jobs = 1 — the main-thread-only
// Worker): the non-bisectable delta-violating exit must warn on stderr; the
// delta-sat verdict must be unchanged. COMPLETENESS (asserts phi^delta
// T-satisfiable on a T-unsatisfiable phi — missed refutation).
TEST(DegenerateDeltaSatWarning, Issue68NonBisectableSeqWarns) {
  const CapturedRun run{RunSmt2String(kIssue68Smt2)};
  EXPECT_NE(run.out.find("delta-sat"), std::string::npos)
      << "stdout: " << run.out;
  EXPECT_NE(run.err.find("non-bisectable box below delta"), std::string::npos)
      << "stderr: " << run.err;
}

// #68, pooled path (jobs = 4): the same non-bisectable Worker exit must
// warn from pool workers too. Same COMPLETENESS characterization as above.
TEST(DegenerateDeltaSatWarning, Issue68NonBisectableParallelJobs4Warns) {
  Config config;
  config.mutable_number_of_jobs() = 4;
  const CapturedRun run{RunSmt2String(kIssue68Smt2, config)};
  EXPECT_NE(run.out.find("delta-sat"), std::string::npos)
      << "stdout: " << run.out;
  EXPECT_NE(run.err.find("non-bisectable box below delta"), std::string::npos)
      << "stderr: " << run.err;
}

// The first verbatim formula from dreal/dreal4#265: `c <= -8 /\ -0.5 <= d
// <= 1 /\ d*(-c) <= c` is T-unsatisfiable for every finite c (margin >= 4),
// "satisfied" only at c = -inf in extended-real interval arithmetic — the
// witness box carries c : [-inf, -1.797e308]. The final-model unbounded-
// endpoint check must warn on stderr; the delta-sat verdict must be
// unchanged. COMPLETENESS (asserts phi^delta T-satisfiable on a
// T-unsatisfiable phi — missed refutation).
TEST(DegenerateDeltaSatWarning, Issue265UnboundedWitnessWarns) {
  const CapturedRun run{RunSmt2String(
      "(declare-fun c () Real)\n"
      "(declare-fun d () Real)\n"
      "(assert (<= c (- 8.0)))\n"
      "(assert (<= (- 0.5) d))\n"
      "(assert (<= d 1.0))\n"
      "(assert (<= (* d (- c)) c))\n"
      "(check-sat)\n")};
  EXPECT_NE(run.out.find("delta-sat"), std::string::npos)
      << "stdout: " << run.out;
  EXPECT_NE(run.err.find("witness box has unbounded endpoints"),
            std::string::npos)
      << "stderr: " << run.err;
}

// Negative control: a bounded, bisectable delta-sat query must NOT warn —
// the loud surface is for the two degenerate mechanisms only, not for every
// delta-sat.
TEST(DegenerateDeltaSatWarning, NormalBoundedDeltaSatNoWarning) {
  const CapturedRun run{RunSmt2String(
      "(declare-fun x () Real [0.0, 1.0])\n"
      "(assert (= x 0.5))\n"
      "(check-sat)\n")};
  EXPECT_NE(run.out.find("delta-sat"), std::string::npos)
      << "stdout: " << run.out;
  EXPECT_EQ(run.err.find("degenerate delta-sat"), std::string::npos)
      << "stderr: " << run.err;
}

}  // namespace
}  // namespace dreal
