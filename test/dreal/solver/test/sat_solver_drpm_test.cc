// DRPM smoke test: SatSolver::AddLearnedClausePattern is the lemma
// pattern-matching path, live only behind --drpm-max-size > 0
// (context_impl.cc gates on explanation.size() < config().drpm_max_size()).
// It was maintained-but-untested; this pins (a) the path actually runs, and
// (b) the verdict is unchanged by it.
//
// Observable hook: drpm_benchmark_log (context_impl.cc) prints one line per
// theory conflict to std::cerr with a mode char — 'M' when
// AddLearnedClausePattern ran, 'A' when only AddLearnedClauseDirect ran. The
// format "... L <size> <mode>\t PM.ms ..." makes " M\t" a unique marker for
// the pattern path. We capture std::cerr around CheckSatisfiability and
// assert on that marker.
//
// Sensitivity flip-check (recorded in the commit adding this file): setting
// drpm_max_size to 0 in the DRPM-on half makes the " M\t" assertion fail —
// the marker is genuinely DRPM-specific, not ambient stderr noise. The
// DRPM-off half of the test keeps that flip permanently checked.

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"

namespace dreal {
namespace {

// x in [-3,3] (filtered into the box, not SAT literals), plus two
// non-filterable quadratic atoms whose conjunction is infeasible by a margin
// >> delta. The theory conflict explanation is the 2-atom set, so with
// drpm_max_size = 4 the gate explanation.size() < 4 admits it and
// AddLearnedClausePattern runs. Verdict: UNSAT.
//
// The variable name is SAR-parseable ("<prefix>_t<N>(ad|bc)") because with
// CAV26_FILTER_SYMMETRIES compiled ON the symmetry filter runs
// CAV26_SAR_PARSER on every matched substitution's variable names and throws
// on a non-SAR name like "x" — this test must exercise the filter, not crash
// it, under that build.
Formula MakeConflictQuery(const Variable& x) {
  return x >= -3 && x <= 3 && x * x > 4 && x * x < 1;
}

// Runs CheckSatisfiability with std::cerr captured; asserts the UNSAT
// verdict, returns the captured log.
std::string CheckUnsatCapturingStderr(const Formula& f, const Config& config) {
  std::ostringstream captured;
  std::streambuf* const old = std::cerr.rdbuf(captured.rdbuf());
  const auto result = CheckSatisfiability(f, config);
  std::cerr.rdbuf(old);
  EXPECT_FALSE(result) << "query must be UNSAT regardless of drpm_max_size";
  return captured.str();
}

TEST(SatSolverDrpmTest, PatternPathRunsAndVerdictUnchanged) {
  const Variable x{"x_t0ad", Variable::Type::CONTINUOUS};

  Config drpm_on;
  drpm_on.mutable_drpm_max_size() = 4;
  const std::string log_on = CheckUnsatCapturingStderr(MakeConflictQuery(x), drpm_on);
  EXPECT_NE(log_on.find(" M\t"), std::string::npos)
      << "no 'M'-mode conflict line: AddLearnedClausePattern never ran; stderr:\n"
      << log_on;

  Config drpm_off;  // default drpm_max_size = 0
  const std::string log_off = CheckUnsatCapturingStderr(MakeConflictQuery(x), drpm_off);
  EXPECT_EQ(log_off.find(" M\t"), std::string::npos)
      << "'M'-mode line with DRPM disabled: marker is not DRPM-specific; stderr:\n"
      << log_off;
}

}  // namespace
}  // namespace dreal
