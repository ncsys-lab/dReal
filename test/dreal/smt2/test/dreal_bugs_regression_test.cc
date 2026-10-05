// End-to-end SMT2-path regression guards for the dReal bugs catalogued in
// simulink-to-dreal/docs/dreal-bugs.md and dreal4-cmake/docs/dreal-bugs.md (one
// shared numbering). Each test embeds the *exact* reproducer text committed
// there (`bugNNN_*.smt2`) and pins the correct verdict / model,
// so a regression on any of these resurfaces here rather than silently in the
// downstream translator.
//
// Covered here (the verdict + --model bugs):
//   BUG-001  nested-arithmetic define-ode RHS                -> delta-sat (not unsat)
//   BUG-003  ZOH-parameter coupled ODE under (integral …)    -> delta-sat (not unsat)
//   BUG-005  --model value of the (integral …) endpoint var  -> accurate (not scrambled)
//   BUG-006  unsat (integral …) formula                      -> unsat (not false delta-sat)
//   BUG-008  endpoint asserted below its true value          -> unsat (not false delta-sat)
//   BUG-009  seed pre-pass on a constraint that folds to True -> delta-sat (not a crash)
//   BUG-011  --model witness of a free integral endpoint-time -> contains the true crossing
//   BUG-015  integral start variable with no box, sin flow   -> delta-sat + warning (not SIGSEGV)
//   BUG-016  thin start exactly on a nonzero fixed point     -> endpoint contracts (not skipped)
//   BUG-018  interval initial condition near the tube edge   -> delta-sat (not unsat)
//   BUG-014  X-Taylor LP arm on the F3 instance              -> no signal
//
// BUG-002 is NOT here: the silent drop of a negated (integral …)/(forall_t …)
// is a design gap, not a settled behavior to regression-guard. Its DESIRED
// future semantics (genuine ∃ for negated forall_t; two rival readings for
// negated integral) are specified as aspirational GTEST_SKIP tests in the
// sibling file dreal_future.cc. The --visualize bugs (BUG-004 empty JSON,
// BUG-007 step field) are exercised at the generate_trace level in
// contractor_capd_test.cc, since the --visualize file-write path is not
// reachable through parse_string.

#include "dreal/smt2/driver.h"

#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// RAII redirect of std::cout, restored on BOTH the normal and exception paths.
// A plain "restore after parse_string" is skipped during stack unwinding if
// parse_string throws, leaving std::cout pointing at a destroyed local buffer —
// the next writer to std::cout then segfaults. None of the tests in this file
// throw today, but the guard keeps a future throwing test from reintroducing the
// crash (it bit dreal_future.cc's aspirational throw tests).
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

// Parse an SMT2 string (its (check-sat) prints the verdict to std::cout, and
// the model too when produce_models is set) and return the captured output.
// Default Config precision is 0.001 — the delta the bug doc's reproducers use.
std::string RunSmt2String(const std::string& smt2,
                          const Config& config = Config{}) {
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  const CoutRedirect redirect{captured.rdbuf()};
  driver.parse_string(smt2);
  return captured.str();
}

// BUG-001 — a define-ode RHS with a nested arithmetic subexpression
// (* (- (/ 1.0 10.0)) x) was a cav26 regression that false-`unsat`'d. The
// equivalent literal-coefficient form (* -0.1 x) was always correct. x decays
// from 100, so x_t ≈ 90.5 is reachable: delta-sat.
TEST(DrealBugsRegression, Bug001_NestedArithRhs_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* (- (/ 1.0 10.0)) x))))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (>= x_t 0)\n"
      "  (<= x_t 100)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-003 — a ZOH-parameter ODE (d/dt[y]=0) coupled into another state
// (d/dt[x]=y) under an (integral …) assertion was a cav26 regression that
// false-`unsat`'d. x(t)=20+30·t so x(1)=50 ≥ 49: delta-sat.
TEST(DrealBugsRegression, Bug003_ZohParameterCoupling_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun y () Real [0.000000, 50.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun y_0 () Real [0.000000, 50.000000])\n"
      "(declare-fun y_t () Real [0.000000, 50.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] y) (= d/dt[y] 0)))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0 20)\n"
      "  (= y_0 30)\n"
      "  (= [x_t y_t] (integral 0. time [x_0 y_0] flow_1))\n"
      "  (>= x_t 49)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-005 — the --model value of the (integral …) endpoint variable was
// scrambled/mis-printed (cav26 fully scrambled; pre-fix popl27 printed 61.597).
// True x_0_t = 100·e^−0.5 ≈ 60.6531. Assert the printed endpoint box contains
// the true value and excludes the bug's 61.597. (Same defect locus as BUG-008,
// also covered by PinnedDecayTest in contractor_odes_semantic_test.cc.)
TEST(DrealBugsRegression, Bug005_ModelEndpointValue_Accurate) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time_0 () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* -0.5 x))))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0_0 100)\n"
      "  (= [x_0_t] (integral 0. time_0 [x_0_0] flow_1))\n"
      "  (>= x_0_t 0)\n"
      "  (<= x_0_t 100)\n"
      "))\n"
      "(check-sat)\n",
      config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  // The model line for x_0_t must report a box around 60.6531, not the bug's
  // 61.597 (off by ~1, outside the printed delta-width box).
  const std::size_t pos{out.find("x_0_t")};
  ASSERT_NE(pos, std::string::npos) << "no x_0_t in model; got: " << out;
  const std::string line{out.substr(pos, out.find('\n', pos) - pos)};
  EXPECT_NE(line.find("60.653"), std::string::npos)
      << "endpoint must print x(1)≈60.6531, not the scrambled/61.597 value: "
      << line;
}

// BUG-006 — an unsat (integral …) formula (x decays to 60.65, asserted < 10;
// gap ≫ δ) false-`delta-sat`'d on early v5 builds. Must be unsat.
TEST(DrealBugsRegression, Bug006_UnsatIntegral_Unsat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* -0.5 x))))\n"
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (< x_t 10)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-008 — the (integral …) endpoint asserted below its true value
// (x(1)=100−80·e^−0.5≈51.4775, asserted ≤ 51.0; gap 0.477 ≫ δ) was false-
// `delta-sat` on pre-fix popl27 (endpoint var under-contracted). Must be unsat.
// This is bug005_contract_trigger.smt2; also covered by PinnedRiseTest.
TEST(DrealBugsRegression, Bug008_SubTrueEndpoint_Unsat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [0.000000, 100.000000])\n"
      "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
      "(declare-fun x_t () Real [0.000000, 100.000000])\n"
      "(declare-fun time () Real [1.000000, 1.000000])\n"
      "(declare-fun mode () Real [1.000000, 1.000000])\n"
      "(define-ode flow_1 ((= d/dt[x] (* 0.500000 (- 100.000000 x)))))\n"
      "(assert (and\n"
      "  (= mode 1)\n"
      "  (= x_0 20)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (<= x_t 51.0)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// Extracts the printed model interval for @p var — "var : [lb, ub]" (a point
// interval prints as "var : <lb, ub>"). Returns false if the line is absent or
// malformed.
bool ParseModelInterval(const std::string& out, const std::string& var,
                        double* lb, double* ub) {
  // Anchored at a line start, so "x : " does not match inside "tx : ".
  const std::string key{"\n" + var + " : "};
  const std::size_t pos{out.find(key)};
  if (pos == std::string::npos) return false;
  char open{};
  return std::sscanf(out.c_str() + pos + key.size(), " %c%lf, %lf", &open, lb,
                     ub) == 3;
}

// The bug011 reproducer without its (check-sat), so tests can append extra
// assertions (the idempotence re-feed) before checking.
const char* const kBug011Query =
    "(set-logic QF_NRA_ODE)\n"
    "(declare-fun x0 () Real)\n"
    "(declare-fun xt () Real)\n"
    "(declare-fun tau () Real)\n"
    "(assert (>= x0 (- 10.0)))\n"
    "(assert (<= x0 10.0))\n"
    "(assert (>= xt (- 10.0)))\n"
    "(assert (<= xt 10.0))\n"
    "(assert (>= tau 0.0))\n"
    "(assert (<= tau 1.0))\n"
    "(declare-fun x () Real)\n"
    "(assert (>= x (- 10.0)))\n"
    "(assert (<= x 10.0))\n"
    "(define-ode flow_1 ((= d/dt[x] 1.0)))\n"
    "(assert (= x0 0.0))\n"
    "(assert (= [xt] (integral 0. tau [x0] flow_1)))\n"
    "(assert (= xt 0.38))\n";

// BUG-011 — the --model witness of a FREE integral endpoint-time variable.
// dx/dt = 1 from x(0) = 0 with the endpoint state pinned x(τ) = 0.38 admits
// exactly one solution, τ = 0.38. The stub OdeFormulaEvaluator (VALID/[0,0])
// let ICP declare delta-sat with ZERO branching, freezing τ at tube-hull
// granularity — [0.375, 0.5] at the default --ode-hull-grid 4 — and the then
// unconditional model post-pass (Tighten) reported the hull MIDPOINT ±δ/2,
// τ ≈ [0.437, 0.438]: a box the solver itself refutes when asserted a priori.
// Under --refine-witness the witness box must contain the true crossing to
// within delta. (The flag is OFF by default — the fast tube-granularity accept
// is load-bearing for deep BMC, where δ-refining every ODE dim measured 4.89×
// github PAR2 with 18 SAT→TIM — so default-mode witnesses are the raw
// terminating box; see Bug011_DefaultModelIdempotent.)
TEST(DrealBugsRegression, Bug011_FreeEndpointTauWitness_Accurate) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  config.mutable_refine_witness().set_from_command_line(true);
  const std::string out{RunSmt2String(
      std::string(kBug011Query) + "(check-sat)\n", config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  double lb{0.0};
  double ub{0.0};
  ASSERT_TRUE(ParseModelInterval(out, "tau", &lb, &ub)) << "got: " << out;
  const double delta{0.001};  // the run's --precision
  EXPECT_TRUE(lb - delta <= 0.38 && 0.38 <= ub + delta)
      << "tau witness [" << lb << ", " << ub
      << "] does not contain the sole solution 0.38; got: " << out;
  // What the flag buys over the default: the witness is delta-TIGHT, not just
  // honest-wide (refinement branches every ODE dim below delta).
  EXPECT_LE(ub - lb, 2 * delta)
      << "tau witness [" << lb << ", " << ub << "] not refined to delta";
}

// BUG-011 (reporting half) — the --model box must be IDEMPOTENT: re-asserting
// the reported per-variable intervals as bounds over the same constraints must
// stay delta-sat. The formerly unconditional Tighten post-pass shrank every >δ
// dimension to its midpoint ±δ/2 — sound for dims outside ODE atoms
// (EvaluateBox's certificate is an interval evaluation over the whole box, so
// every sub-box inherits it) but FABRICATION for ODE dims, whose stub
// evaluator established nothing: in default (fast-accept) mode the theory box legitimately keeps ODE
// dims wide, and the midpoint slice τ = [0.437, 0.438] excludes the sole
// solution 0.38 — the solver itself refutes the re-fed box. The default model
// is now the raw terminating box, idempotent by construction.
TEST(DrealBugsRegression, Bug011_DefaultModelIdempotent) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  const std::string out{RunSmt2String(
      std::string(kBug011Query) + "(check-sat)\n", config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  const double delta{0.001};
  std::ostringstream refeed;
  refeed << std::setprecision(17) << kBug011Query;
  for (const char* var : {"x0", "xt", "tau", "x"}) {
    double lb{0.0};
    double ub{0.0};
    ASSERT_TRUE(ParseModelInterval(out, var, &lb, &ub))
        << "no " << var << " in model; got: " << out;
    refeed << "(assert (>= " << var << " " << lb << "))\n"
           << "(assert (<= " << var << " " << ub << "))\n";
  }
  // The un-refined ODE dim must report its whole interval, which contains the
  // sole solution — not a midpoint slice that excludes it.
  double tau_lb{0.0};
  double tau_ub{0.0};
  ASSERT_TRUE(ParseModelInterval(out, "tau", &tau_lb, &tau_ub));
  EXPECT_TRUE(tau_lb - delta <= 0.38 && 0.38 <= tau_ub + delta)
      << "tau witness [" << tau_lb << ", " << tau_ub
      << "] excludes the sole solution 0.38; got: " << out;
  // Idempotence: the reported box, fed back as bounds, is still delta-sat.
  refeed << "(check-sat)\n";
  const std::string out2{RunSmt2String(refeed.str())};
  EXPECT_NE(out2.find("delta-sat"), std::string::npos)
      << "reported model box is NOT delta-sat when re-fed: " << out2
      << "\noriginal model: " << out;
}

// Model-reporting contract (default): --model is the RAW TERMINATING BOX —
// the exact region ICP certified (every constraint VALID or δ-thin over it),
// not a midpoint±δ/2 slice of it. Collapsing to the midpoint is a downstream
// presentation choice; making it here destroys the certified-region
// information. Here every constraint is VALID over x = [0, 10], so ICP accepts
// the full box with zero branching and the model must report all of it.
// (Seeding is disabled: the seed-and-verify pre-pass would legitimately return
// a small verified box around a proposed point instead.)
TEST(DrealBugsRegression, ModelDefault_RawTerminatingBox) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  config.mutable_seed_samples().set_from_command_line(0);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-fun x () Real)\n"
      "(assert (>= x 0.0))\n"
      "(assert (<= x 10.0))\n"
      "(assert (>= x -1.0))\n"
      "(check-sat)\n",
      config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  double lb{0.0};
  double ub{0.0};
  ASSERT_TRUE(ParseModelInterval(out, "x", &lb, &ub)) << "got: " << out;
  EXPECT_GT(ub - lb, 5.0) << "x reported [" << lb << ", " << ub
                          << "], not the raw terminating box [0, 10]; got: "
                          << out;
}

// Model-reporting contract (--refine-witness): the same instance reports a
// δ-tight witness — Tighten shrinks continuous dims to midpoint ±δ/2, sound
// post-hoc because EvaluateBox's interval-evaluation certificate is
// inclusion-monotone. (The flag's ODE half is exercised by
// Bug011_FreeEndpointTauWitness_Accurate.)
TEST(DrealBugsRegression, RefineWitness_TightensNraDims) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  config.mutable_seed_samples().set_from_command_line(0);
  config.mutable_refine_witness().set_from_command_line(true);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-fun x () Real)\n"
      "(assert (>= x 0.0))\n"
      "(assert (<= x 10.0))\n"
      "(assert (>= x -1.0))\n"
      "(check-sat)\n",
      config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  double lb{0.0};
  double ub{0.0};
  ASSERT_TRUE(ParseModelInterval(out, "x", &lb, &ub)) << "got: " << out;
  const double delta{0.001};  // the run's --precision
  EXPECT_LE(ub - lb, 2 * delta)
      << "x witness [" << lb << ", " << ub << "] not refined to delta";
  EXPECT_TRUE(lb <= 5.0 && 5.0 <= ub)
      << "x witness [" << lb << ", " << ub << "] is not the midpoint slice";
}

// Model-reporting contract (don't-care Booleans): a Boolean the SAT layer
// never assigned is a genuine don't-care — both values satisfy — and reports
// its whole [false, true] interval, exactly like a wide real dim, in all
// three renderings (--model box print, (get-model), (get-value)).
TEST(DrealBugsRegression, ModelDefault_DontCareBooleanWholeInterval) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-fun b () Bool)\n"
      "(declare-fun x () Real)\n"
      "(assert (>= x 0.0))\n"
      "(assert (<= x 1.0))\n"
      "(check-sat)\n"
      "(get-model)\n"
      "(get-value (b))\n",
      config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_NE(out.find("b : [false, true]"), std::string::npos) << "got: " << out;
  EXPECT_NE(out.find("(define-fun b () Bool [false, true])"),
            std::string::npos)
      << "got: " << out;
  EXPECT_NE(out.find("(b [false, true])"), std::string::npos) << "got: " << out;
}

// Model-reporting contract (--refine-witness): a definite-valued witness —
// the don't-care Boolean pins to a truth value like every other dim shrinks.
TEST(DrealBugsRegression, RefineWitness_PinsDontCareBoolean) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  config.mutable_refine_witness().set_from_command_line(true);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-fun b () Bool)\n"
      "(declare-fun x () Real)\n"
      "(assert (>= x 0.0))\n"
      "(assert (<= x 1.0))\n"
      "(check-sat)\n",
      config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_NE(out.find("b : True"), std::string::npos) << "got: " << out;
}

// dreal/dreal4#258 — arctan2 with an UNBOUNDED second argument returned a
// false `unsat`. SOUNDNESS (asserts φ T-unsatisfiable on a T-satisfiable φ —
// false unsat): the query is trivially satisfiable (y=0 gives z=π/2, indeed
// every y admits a z). Bounding y (e.g. y∈[3.9,4.1]) always gave the correct
// delta-sat, so the trigger is the unbounded second argument reaching the
// atan2 backward contractor.
TEST(DrealBugsRegression, Issue258_Atan2UnboundedSecondArg_DeltaSat) {
  const std::string out{RunSmt2String(
      "(declare-fun y () Real)\n"
      "(declare-fun z () Real)\n"
      "(assert (= z (arctan2 3 y)))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-009 — the seed-and-verify pre-pass (NRA-only, on by default) substitutes
// derived (equality-defined) variables into every constraint before handing
// them to COBYLA; here `(<= (sin x) y)` with derived `y == (sin x)` collapses
// to `sin x ≤ sin x` → True, which reached NloptOptimizer::AddConstraint and
// killed the process ("Unsupported formula True"). Post-fix the pre-pass skips
// Boolean-constant constraints (no feasibility gradient; the box-verify remains
// the arbiter): the formula is trivially satisfiable, so delta-sat.
TEST(DrealBugsRegression, Bug009_SeedTrueCollapse_NoCrash) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-fun x () Real)\n"
      "(declare-fun y () Real)\n"
      "(assert (>= x -10.0))\n"
      "(assert (<= x 10.0))\n"
      "(assert (= y (sin x)))\n"
      "(assert (<= (sin x) y))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// BUG-018 (dreal4-cmake docs/dreal-bugs.md) — an interval initial condition with
// the gate near the tube's edge returned `unsat` at default flags. SOUNDNESS
// (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat): the per-slice
// tube's mean-value term took its derivative from CAPD's Curve::timeDerivative,
// whose initial-condition spread was f(X)-f(c) instead of X-c until CAPD
// 2a2263c7e. Each query below is satisfiable; texts are the committed
// docs/dreal-bugs/bug018_meanvalue_*_trigger.smt2 files.
TEST(DrealBugsRegression, Bug018_GrowthTubeEdge_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [-100, 100])\n"
      "(declare-fun x_0 () Real [1, 2])\n"
      "(declare-fun x_t () Real [-100, 100])\n"
      "(declare-fun time () Real [0, 1])\n"
      "(define-ode flow_1 ((= d/dt[x] (* 0.1 x))))\n"
      "(assert (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "(assert (>= x_t 2.205))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos)
      << "x_0 = 2, time = 1 gives x_t = 2.2103; got: " << out;
}

TEST(DrealBugsRegression, Bug018_DecayTubeEdge_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [-100, 100])\n"
      "(declare-fun x_0 () Real [1, 2])\n"
      "(declare-fun x_t () Real [-100, 100])\n"
      "(declare-fun time () Real [0, 1])\n"
      "(define-ode flow_1 ((= d/dt[x] (* -0.1 x))))\n"
      "(assert (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "(assert (>= x_t 1.995))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos)
      << "x_0 = 2, time ≤ 0.025 gives x_t ≥ 1.995; got: " << out;
}

TEST(DrealBugsRegression, Bug018_ProjectileTubeEdge_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real [-100, 100])\n"
      "(declare-fun v () Real [-100, 100])\n"
      "(declare-fun x_0 () Real [0, 0])\n"
      "(declare-fun v_0 () Real [0.5, 1.5])\n"
      "(declare-fun x_t () Real [-100, 100])\n"
      "(declare-fun v_t () Real [-100, 100])\n"
      "(declare-fun time () Real [0, 0.5])\n"
      "(define-ode flow_1 ((= d/dt[x] v) (= d/dt[v] -1.0)))\n"
      "(assert (= [x_t v_t] (integral 0. time [x_0 v_0] flow_1)))\n"
      "(assert (>= x_t 0.6))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos)
      << "v_0 = 1.5, time = 0.5 gives x_t = 0.625; got: " << out;
}

// BUG-015 (dreal4-cmake docs/dreal-bugs.md) — an integral whose start variable
// has no box and whose flow has sin crashed with SIGSEGV: CAPD splits the start
// [-inf, inf] into a NaN center, and its interval sin recurses on NaN until the
// stack overflows. Post-fix the adapter refuses the non-finite start, the ODE is
// inconclusive (COMPLETENESS only — asserts φ^δ T-satisfiable on a possibly
// T-unsatisfiable φ — missed refutation), and delta-sat comes with the
// inconclusive-ODE warning on stderr. Run in a re-executed child so the
// pre-fix crash fails the test instead of the suite.
class DrealBugsRegressionDeathTest : public ::testing::Test {
 protected:
  void SetUp() override { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
  static void ExpectDeltaSatOrExit(const std::string& smt2) {
    alarm(30);
    const std::string out{RunSmt2String(smt2)};
    std::_Exit(out.find("delta-sat") != std::string::npos ? 0 : 1);
  }
};

TEST_F(DrealBugsRegressionDeathTest, Bug015_UnboundedSinStart_DeltaSatWithWarning) {
  EXPECT_EXIT(ExpectDeltaSatOrExit("(set-logic QF_NRA_ODE)\n"
                                   "(declare-fun x () Real)\n"
                                   "(declare-fun x0 () Real)\n"
                                   "(declare-fun xt () Real)\n"
                                   "(declare-fun t () Real [0, 1])\n"
                                   "(define-ode flow_1 ((= d/dt[x] (sin x))))\n"
                                   "(assert (= [xt] (integral 0. t [x0] flow_1)))\n"
                                   "(check-sat)\n"),
              ::testing::ExitedWithCode(0), "not integrated");
}

// The same with d/dt[x] = -x. A control: it answered delta-sat before the fix
// too (CAPD threw on the NaN center instead of looping in sin).
TEST_F(DrealBugsRegressionDeathTest, Bug015_UnboundedDecayStart_DeltaSat) {
  EXPECT_EXIT(ExpectDeltaSatOrExit("(set-logic QF_NRA_ODE)\n"
                                   "(declare-fun x () Real)\n"
                                   "(declare-fun x0 () Real)\n"
                                   "(declare-fun xt () Real)\n"
                                   "(declare-fun t () Real [0, 1])\n"
                                   "(define-ode flow_1 ((= d/dt[x] (- x))))\n"
                                   "(assert (= [xt] (integral 0. t [x0] flow_1)))\n"
                                   "(check-sat)\n"),
              ::testing::ExitedWithCode(0), "");
}

// BUG-014 (dreal4-cmake docs/dreal-bugs.md) — the X-Taylor LP arms died on
// signals inside vendored SoPlex 4.0.2's presolve (SPxMainSM::duplicateCols,
// an out-of-bounds write; heap corruption, so a latent SOUNDNESS risk). This
// is the entry's F3 (expressivity v1 tanh_coupling decrease_slope), which
// crashed 3/3 with --polytope --polytope-linearizer both. F3 is hard (600 s
// timeout in the probe), so the child counts surviving 5 s as a pass.
TEST_F(DrealBugsRegressionDeathTest, Bug014_XTaylorBothSurvivesF3) {
  Config config;
  config.mutable_use_polytope().set_from_command_line(true);
  config.mutable_polytope_linearizer().set_from_command_line(PolytopeLinearizer::kBoth);
  const std::string f3{
      "(set-option :precision 0.000500000000000000)\n"
      "(declare-const J_00 Real)\n"
      "(declare-const J_01 Real)\n"
      "(declare-const J_11 Real)\n"
      "(declare-const cse_optimization_symbol_0 Real)\n"
      "(declare-const cse_optimization_symbol_1 Real)\n"
      "(declare-const s_0 Real)\n"
      "(declare-const s_1 Real)\n"
      "(declare-const x_0 Real)\n"
      "(declare-const x_1 Real)\n"
      "(declare-const y_0 Real)\n"
      "(declare-const y_1 Real)\n"
      "(assert (= cse_optimization_symbol_0 (+ x_0 (* -1 y_0))))\n"
      "(assert (= cse_optimization_symbol_1 (+ x_1 (* -1 y_1))))\n"
      "(assert (> x_0 -1))\n"
      "(assert (> x_1 -1))\n"
      "(assert (< x_0 1))\n"
      "(assert (< x_1 1))\n"
      "(assert (> y_0 -1))\n"
      "(assert (> y_1 -1))\n"
      "(assert (< y_0 1))\n"
      "(assert (< y_1 1))\n"
      "(assert (> J_00 (/ -2 5)))\n"
      "(assert (< J_00 (/ 2 5)))\n"
      "(assert (> J_01 (/ -2 5)))\n"
      "(assert (< J_01 (/ 2 5)))\n"
      "(assert (> J_11 (/ -2 5)))\n"
      "(assert (< J_11 (/ 2 5)))\n"
      "(assert (>= s_0 0))\n"
      "(assert (<= s_0 1))\n"
      "(assert (>= s_1 0))\n"
      "(assert (<= s_1 1))\n"
      "(assert (> (+ (* -2 (pow cse_optimization_symbol_0 2)) (* -2 (pow cse_optimization_symbol_1 2)) (* -1 (+ y_0 (* -4 (tanh (* (/ 1 4) y_0)))) (+ (* -2 y_0) (* 2 x_0))) (* -1 (+ y_1 (* -4 (tanh (* (/ 1 4) y_1)))) (+ (* -2 y_1) (* 2 x_1))) (* 2 cse_optimization_symbol_0 s_0 (+ (* J_00 cse_optimization_symbol_0) (* J_01 cse_optimization_symbol_1))) (* 2 cse_optimization_symbol_1 s_1 (+ (* J_01 cse_optimization_symbol_0) (* J_11 cse_optimization_symbol_1)))) (/ 3 2000)))\n"
      "(check-sat )\n"};
  EXPECT_EXIT(
      {
        signal(SIGALRM, [](int) { std::_Exit(0); });
        alarm(5);
        RunSmt2String(f3, config);
        std::_Exit(0);
      },
      ::testing::ExitedWithCode(0), "");
}

// BUG-016 (simulink-to-dreal docs/dreal-bugs.md) — a thin start exactly on a
// nonzero fixed point (x__k0 = 1, d/dt[x] = 10(1 - x)) made CAPD throw "minimal
// time step reached" on its first step; the integral was skipped and x__k1 kept
// its declared box — COMPLETENESS (asserts φ^δ T-satisfiable on a
// T-unsatisfiable φ — missed refutation). The true endpoint is exactly 1.
// Text: bug016_rest_trigger.smt2.
TEST(DrealBugsRegression, Bug016_RestingFixedPoint_EndpointContracts) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real)\n"
      "(declare-fun x__k0 () Real)\n"
      "(declare-fun x__k1 () Real)\n"
      "(assert (>= x__k1 (- 1000000.0))) (assert (<= x__k1 1000000.0))\n"
      "(assert (= x__k0 1.0))\n"
      "(declare-fun time_k0 () Real)\n"
      "(assert (= time_k0 0.05))\n"
      "(define-ode flow_1 ((= d/dt[x] (* 10.0 (+ 1.0 (- x))))))\n"
      "(assert (= [x__k1] (integral 0. time_k0 [x__k0] flow_1)))\n"
      "(check-sat)\n",
      config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  double lb{};
  double ub{};
  ASSERT_TRUE(ParseModelInterval(out, "x__k1", &lb, &ub)) << "got: " << out;
  EXPECT_GT(lb, 1.0 - 1e-6) << "got: " << out;
  EXPECT_LT(ub, 1.0 + 1e-6) << "got: " << out;
}

// One resting component made the whole vector inconclusive: y (d/dt[y] = -y,
// y__k0 = 1) lost its endpoint too. True y__k1 = e^-0.05 = 0.951229…
// Text: bug016_rest_vector.smt2.
TEST(DrealBugsRegression, Bug016_RestingFixedPoint_VectorKeepsMovingState) {
  Config config;
  config.mutable_produce_models().set_from_command_line(true);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA_ODE)\n"
      "(declare-fun x () Real)\n"
      "(declare-fun y () Real)\n"
      "(declare-fun x__k0 () Real)\n"
      "(assert (>= x__k0 (- 1000000.0)))\n"
      "(assert (<= x__k0 1000000.0))\n"
      "(declare-fun x__k1 () Real)\n"
      "(assert (>= x__k1 (- 1000000.0)))\n"
      "(assert (<= x__k1 1000000.0))\n"
      "(assert (= x__k0 1.0))\n"
      "(declare-fun y__k0 () Real)\n"
      "(assert (>= y__k0 (- 1000000.0)))\n"
      "(assert (<= y__k0 1000000.0))\n"
      "(declare-fun y__k1 () Real)\n"
      "(assert (>= y__k1 (- 1000000.0)))\n"
      "(assert (<= y__k1 1000000.0))\n"
      "(assert (= y__k0 1.0))\n"
      "(declare-fun time_k0 () Real)\n"
      "(assert (= time_k0 0.05))\n"
      "(define-ode flow_1 ((= d/dt[x] (* 10.0 (+ 1.0 (- x)))) (= d/dt[y] (- y))))\n"
      "(assert (= [x__k1 y__k1] (integral 0. time_k0 [x__k0 y__k0] flow_1)))\n"
      "(check-sat)\n",
      config)};
  ASSERT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  double lb{};
  double ub{};
  ASSERT_TRUE(ParseModelInterval(out, "y__k1", &lb, &ub)) << "got: " << out;
  EXPECT_GT(lb, 0.951229 - 1e-5) << "got: " << out;
  EXPECT_LT(ub, 0.951230 + 1e-5) << "got: " << out;
}

}  // namespace
}  // namespace dreal
