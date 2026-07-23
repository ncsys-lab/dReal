// DRPM rigorous tests: sharpen the smoke test (sat_solver_drpm_test.cc) into
// boundary and adversarial coverage of the learned-lemma pattern-matching
// path (SatSolver::AddLearnedClausePattern, gated in context_impl.cc on
// explanation.size() < config().drpm_max_size() -- strict '<').
//
// Observable hook (same as the smoke test): drpm_benchmark_log prints one
// stderr line per theory conflict, "... L <size> <mode>\t PM.ms ..." with
// mode 'M' when AddLearnedClausePattern ran and 'A' when only
// AddLearnedClauseDirect ran. " L 2 M\t" therefore pins BOTH the explanation
// size (2) and the pattern path in one substring.
//
// Three properties:
//  1. MaxSizeBoundaryIsStrict: an explanation of size 2 is EXCLUDED at
//     drpm_max_size = 2 (size == max) and ADMITTED at drpm_max_size = 3
//     (size == max - 1).
//  2. NearMissLemmaReuseNeverFlipsDeltaSat: a delta-sat query carrying a
//     satisfiable atom pair that is a structural near-miss (same shape,
//     different constant) of a learned UNSAT lemma must stay delta-sat with
//     DRPM on. A wrong reuse would learn a clause killing the only
//     satisfiable branch -- SOUNDNESS (asserts phi T-unsatisfiable on a
//     T-satisfiable phi -- false unsat). Unit-level premise pin:
//     test/dreal/util/test/pattern_matching_commutative_test.cc
//     (ConstantsAreStructureNotPatternVariables).
//  3. TinyMaxTimeKeepsVerdictAndTerminates: drpm_max_time bounds only the
//     EXTENSION of the matcher's budget beyond the SAT+theory elapsed time
//     (the pm_timeout computation in context_impl.cc adds
//     min(99x, drpm_max_time) on top of the elapsed-time floor), so 1e-9
//     collapses the budget to that floor. Whichever side of the timeout race
//     FindSimilar lands on, the direct lemma is inserted unconditionally
//     after the PM call, so solving must terminate with the DRPM-off verdict.

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/api/api.h"
#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"

namespace dreal {
namespace {

// Same query as the smoke test: x in [-3,3] filtered into the box, plus two
// non-filterable quadratic atoms whose conjunction is infeasible by a margin
// >> delta; the theory-conflict explanation is the 2-atom set. Variable names
// are SAR-parseable ("<prefix>_t<N>(ad|bc)") so CAV26_FILTER_SYMMETRIES
// builds exercise the symmetry filter instead of crashing in
// CAV26_VARNAME_PARSER.
Formula MakeConflictQuery(const Variable& x) {
  return x >= -3 && x <= 3 && x * x > 4 && x * x < 1;
}

// delta-sat: x*x > 4 holds on |x| in (2,3]; the disjunction is then
// satisfiable only through the y branch, y*y in (4,9). x and y carry
// IDENTICAL bounds deliberately: substitutions_map::attempt_substitution
// requires equal Box domains (box[a] != box[aP] -> BOX_MISS), and this test
// must let the x->y match attempt survive to the CONSTANT discrimination
// (y*y < 9 vs the learned x*x < 1), not die earlier on a domain mismatch.
Formula MakeNearMissQuery(const Variable& x, const Variable& y) {
  return x >= -3 && x <= 3 && y >= -3 && y <= 3 && x * x > 4 &&
         (x * x < 1 || (y * y > 4 && y * y < 9));
}

struct CapturedRun {
  bool delta_sat{};
  std::string log;
};

// Runs CheckSatisfiability with std::cerr captured.
CapturedRun RunCapturingStderr(const Formula& f, const Config& config) {
  std::ostringstream captured;
  std::streambuf* const old = std::cerr.rdbuf(captured.rdbuf());
  const auto result = CheckSatisfiability(f, config);
  std::cerr.rdbuf(old);
  return {static_cast<bool>(result), captured.str()};
}

TEST(SatSolverDrpmRigorousTest, MaxSizeBoundaryIsStrict) {
  const Variable x{"x_t0ad", Variable::Type::CONTINUOUS};

  // size 2 == drpm_max_size - 1: ADMITTED.
  Config admit;
  admit.mutable_drpm_max_size() = 3;
  const CapturedRun on = RunCapturingStderr(MakeConflictQuery(x), admit);
  EXPECT_FALSE(on.delta_sat) << "query must be UNSAT regardless of drpm_max_size";
  EXPECT_NE(on.log.find(" L 2 M\t"), std::string::npos)  // INTEGRATION-VERIFY: explanation size is exactly 2 (the smoke test only proves size < 4); if it differs, re-pin both halves at max = size and max = size + 1
      << "size-2 explanation not admitted at drpm_max_size = 3; stderr:\n"
      << on.log;

  // size 2 == drpm_max_size: EXCLUDED (the gate is strict '<').
  Config exclude;
  exclude.mutable_drpm_max_size() = 2;
  const CapturedRun off = RunCapturingStderr(MakeConflictQuery(x), exclude);
  EXPECT_FALSE(off.delta_sat) << "query must be UNSAT regardless of drpm_max_size";
  EXPECT_EQ(off.log.find(" M\t"), std::string::npos)
      << "size == max must be excluded by the strict '<' gate; stderr:\n"
      << off.log;
  EXPECT_NE(off.log.find(" L 2 A\t"), std::string::npos)
      << "excluded conflict must still take the direct-lemma 'A' path; stderr:\n"
      << off.log;
}

TEST(SatSolverDrpmRigorousTest, NearMissLemmaReuseNeverFlipsDeltaSat) {
  const Variable x{"x_t0ad", Variable::Type::CONTINUOUS};
  const Variable y{"y_t0ad", Variable::Type::CONTINUOUS};

  // Ground truth with DRPM off.
  Config drpm_off;  // default drpm_max_size = 0
  const CapturedRun off = RunCapturingStderr(MakeNearMissQuery(x, y), drpm_off);
  EXPECT_TRUE(off.delta_sat) << "near-miss query must be delta-sat with DRPM off";

  // With DRPM on, the learned x conflict is pattern-matched against every
  // atom PredicateNormalizer::Convert registered at assert time -- including
  // the y atoms of the still-satisfiable branch. The y*y > 4 atom matches
  // the x*x > 4 atom under x->y, but y*y < 9 differs from the learned
  // x*x < 1 in its constant, and constants are canonical structure (only
  // variables become DeBruijn dummies), so no FULL clause match may cross
  // from x to y; mixing x and y literals is blocked by the substitution
  // bijection. A wrong reuse would add the clause !(y*y > 4 && y*y < 9),
  // killing the only satisfiable branch: SOUNDNESS (asserts phi
  // T-unsatisfiable on a T-satisfiable phi -- false unsat).
  Config drpm_on;
  drpm_on.mutable_drpm_max_size() = 8;  // admits the conflict whatever its exact size (<= 4 atoms exist)
  const CapturedRun on = RunCapturingStderr(MakeNearMissQuery(x, y), drpm_on);
  EXPECT_TRUE(on.delta_sat)
      << "DRPM flipped a delta-sat query to unsat: SOUNDNESS (asserts phi "
         "T-unsatisfiable on a T-satisfiable phi -- false unsat); stderr:\n"
      << on.log;

  // Non-vacuity: the x conflict must actually have been learned through the
  // pattern path, or this test never exercised reuse. This relies on the SAT
  // solver deciding the x*x < 1 literal true before settling on the y branch
  // (CaDiCaL's default decision phase is 'true').
  EXPECT_NE(on.log.find(" M\t"), std::string::npos)  // INTEGRATION-VERIFY: if no 'M' line, restructure the query so the conflicting branch is tried first -- do NOT weaken the delta-sat assertions above
      << "no 'M' conflict line: the near-miss reuse test ran vacuously; stderr:\n"
      << on.log;
}

TEST(SatSolverDrpmRigorousTest, TinyMaxTimeKeepsVerdictAndTerminates) {
  const Variable x{"x_t0ad", Variable::Type::CONTINUOUS};

  Config drpm_off;  // default drpm_max_size = 0
  const CapturedRun off = RunCapturingStderr(MakeConflictQuery(x), drpm_off);
  EXPECT_FALSE(off.delta_sat) << "query must be UNSAT with DRPM off";

  // drpm_max_time = 1e-9 s truncates to 0 us in the pm_timeout computation,
  // removing the up-to-100x extension: the matcher's budget collapses to its
  // floor (the SAT+theory elapsed time), so FindSimilar may or may not hit
  // its timeout -- the assertions below hold on either side of that race.
  // The size gate (2 < 4) still routes through AddLearnedClausePattern, so
  // the 'M' line must print; and context_impl.cc inserts the direct lemma
  // unconditionally after the PM call -- without it the SAT solver would
  // return the same assignment forever, and this test would hang (fail by
  // timeout) instead of failing an assertion.
  Config tiny;
  tiny.mutable_drpm_max_size() = 4;
  tiny.mutable_drpm_max_time() = 1e-9;
  const CapturedRun on = RunCapturingStderr(MakeConflictQuery(x), tiny);
  EXPECT_EQ(on.delta_sat, off.delta_sat)
      << "tiny drpm_max_time changed the verdict; stderr:\n"
      << on.log;
  EXPECT_NE(on.log.find(" M\t"), std::string::npos)
      << "pattern path did not run under tiny drpm_max_time; stderr:\n"
      << on.log;
}

}  // namespace
}  // namespace dreal
