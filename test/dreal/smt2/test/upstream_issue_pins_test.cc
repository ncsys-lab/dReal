// Regression pins for upstream dreal/dreal4 issues this fork already resolves,
// from the 2026-07-21 upstream gap audit (upstream_gap_audit.md, "Reports where
// this fork is correct" + Part 1 "not affected" rows). Each test embeds the
// reproducer text verbatim — soonhokong/dreal4 branch `20251226`'s regression
// .smt2 files (fetched by blob sha) or the upstream issue body — and pins the
// verdict this fork already produces, so a future regression resurfaces here
// instead of silently re-opening a closed upstream class.
//
// Covered:
//   #324  disjunct-order / ITE-controlling-Boolean learned clauses
//         (upstream: false unsat = SOUNDNESS, asserts phi T-unsatisfiable on a
//         T-satisfiable phi) — soonho's 3 disjunct_order_*.smt2 + the audit's
//         3 adversarial variants, reconstructed from its description
//   #323  let inside define-fun must substitute, not assert at parse time —
//         soonho's 4 define_fun_let_*.smt2 + exact get-value pins
//   #302  delta-sat on (> (- expr1 expr2) 0) with expr1 identical to expr2 is
//         CORRECT delta-complete behavior, not a bug
//   #315  Boolean-valued define-fun bodies must constrain (upstream: spurious
//         sat on a=1 AND a=2 = COMPLETENESS-class, asserts phi^delta
//         T-satisfiable on a T-unsatisfiable phi — missed refutation)

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// RAII redirect of std::cout, restored on BOTH the normal and exception paths
// (same guard as dreal_bugs_regression_test.cc — a plain restore is skipped
// during stack unwinding if parse_string throws).
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

// Parse an SMT2 string (its (check-sat) prints the verdict to std::cout) and
// return the captured output. Default Config precision is 0.001 — the delta of
// the fork's .expected files ("delta-sat with delta = 0.001").
std::string RunSmt2String(const std::string& smt2) {
  Smt2Driver driver{Context{Config{}}};
  std::ostringstream captured;
  const CoutRedirect redirect{captured.rdbuf()};
  driver.parse_string(smt2);
  return captured.str();
}

// ---------------------------------------------------------------------------
// #324 — disjunct order flipped the verdict via ITE-controlling Booleans
// (upstream: false unsat = SOUNDNESS, asserts phi T-unsatisfiable on a
// T-satisfiable phi). soonho's fix (fork commit 460639c05) appends the SAT
// model's positive Boolean literals to learned clauses — a *mask* that makes a
// learned clause safe even when the theory explanation isn't genuinely
// T-unsat. This fork keeps the unmasked clause shape and attacks the root
// class instead (minimal-relevant explanations, the lemma auditor, the
// 2026-06-29 constraint-order explanation-soundness fix). CAVEAT (from the
// audit): we have NO Boolean-context belt-and-suspenders, so any future
// explanation-soundness bug re-opens this whole class — these pins are the
// guard that would catch it.
// ---------------------------------------------------------------------------

// soonhokong/dreal4@20251226 dreal/test/smt2/disjunct_order_01.smt2
// (blob a63798913e); .expected: delta-sat with delta = 0.001.
TEST(UpstreamIssuePins, Issue324_DisjunctOrder01_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-const b1 Bool)\n"
      "(declare-const b2 Bool)\n"
      "(declare-const x Real)\n"
      "(declare-const v Real)\n"
      "(assert (or b1 b2))\n"
      "(assert (not (and b1 b2)))\n"
      "(assert (= v (ite b1 1.0 2.0)))\n"
      "(assert (or\n"
      "  (and b1 (>= x 0) (<= x 1) (= v 1.0))\n"
      "  (and b2 (>= x 2) (<= x 3) (= v 2.0))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// soonhokong/dreal4@20251226 dreal/test/smt2/disjunct_order_02.smt2
// (blob ac3c248326) — disjunct_order_01 with the disjuncts swapped; upstream's
// bug made the verdict depend on this order. .expected: delta-sat.
TEST(UpstreamIssuePins, Issue324_DisjunctOrder02_SwappedDisjuncts_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-const b1 Bool)\n"
      "(declare-const b2 Bool)\n"
      "(declare-const x Real)\n"
      "(declare-const v Real)\n"
      "(assert (or b1 b2))\n"
      "(assert (not (and b1 b2)))\n"
      "(assert (= v (ite b1 1.0 2.0)))\n"
      "(assert (or\n"
      "  (and b2 (>= x 2) (<= x 3) (= v 2.0))\n"
      "  (and b1 (>= x 0) (<= x 1) (= v 1.0))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// soonhokong/dreal4@20251226 dreal/test/smt2/disjunct_order_03.smt2
// (blob a6c36cc82f) — a Boolean matrix controlling ITE branches (the shape of
// the original report). Satisfiable via b_0_0, b_1_1, choice=1:
// vx0 = x1 = -5, vx1 = x2 = -10, vx1 - vx0 = -5 in [-6, 0]. .expected:
// delta-sat.
TEST(UpstreamIssuePins, Issue324_DisjunctOrder03_BooleanMatrixIte_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-const x1 Real) (assert (= x1 -5.0))\n"
      "(declare-const x2 Real) (assert (= x2 -10.0))\n"
      "(declare-const b_0_0 Bool)\n"
      "(declare-const b_0_1 Bool)\n"
      "(declare-const b_1_0 Bool)\n"
      "(declare-const b_1_1 Bool)\n"
      "(assert (or b_0_0 b_0_1))\n"
      "(assert (or b_1_0 b_1_1))\n"
      "(assert (not (and b_0_0 b_0_1)))\n"
      "(assert (not (and b_1_0 b_1_1)))\n"
      "(assert (not (and b_0_0 b_1_0)))\n"
      "(assert (not (and b_0_1 b_1_1)))\n"
      "(assert (or b_0_0 b_1_0))\n"
      "(assert (or b_0_1 b_1_1))\n"
      "(declare-const vx0 Real)\n"
      "(declare-const vx1 Real)\n"
      "(assert (= vx0 (ite b_0_0 x1 x2)))\n"
      "(assert (= vx1 (ite b_1_0 x1 x2)))\n"
      "(declare-const choice Int)\n"
      "(assert (or\n"
      "  (and (= choice 0) (>= vx1 -6.0) (<= vx1 -4.0))\n"
      "  (and (= choice 1) (>= (- vx1 vx0) -6.0) (<= (- vx1 vx0) 0.0))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// Audit adversarial variant (reconstructed from the audit's "swapped
// disjuncts" description): disjunct_order_03 with its final or's disjuncts
// swapped — the satisfying disjunct (choice=1) now comes first. Delta-sat
// either way.
TEST(UpstreamIssuePins, Issue324_DisjunctOrder03Swapped_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-const x1 Real) (assert (= x1 -5.0))\n"
      "(declare-const x2 Real) (assert (= x2 -10.0))\n"
      "(declare-const b_0_0 Bool)\n"
      "(declare-const b_0_1 Bool)\n"
      "(declare-const b_1_0 Bool)\n"
      "(declare-const b_1_1 Bool)\n"
      "(assert (or b_0_0 b_0_1))\n"
      "(assert (or b_1_0 b_1_1))\n"
      "(assert (not (and b_0_0 b_0_1)))\n"
      "(assert (not (and b_1_0 b_1_1)))\n"
      "(assert (not (and b_0_0 b_1_0)))\n"
      "(assert (not (and b_0_1 b_1_1)))\n"
      "(assert (or b_0_0 b_1_0))\n"
      "(assert (or b_0_1 b_1_1))\n"
      "(declare-const vx0 Real)\n"
      "(declare-const vx1 Real)\n"
      "(assert (= vx0 (ite b_0_0 x1 x2)))\n"
      "(assert (= vx1 (ite b_1_0 x1 x2)))\n"
      "(declare-const choice Int)\n"
      "(assert (or\n"
      "  (and (= choice 1) (>= (- vx1 vx0) -6.0) (<= (- vx1 vx0) 0.0))\n"
      "  (and (= choice 0) (>= vx1 -6.0) (<= vx1 -4.0))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// Audit adversarial variant (reconstructed from the audit's
// "only-last-disjunct-sat" description): the first disjunct is infeasible
// (b1 forces v = 1.0 via the ITE, contradicting its own (= v 2.0) by margin
// 1 >> delta), so only the LAST disjunct is satisfiable — the worst case for
// an order-sensitive learned-clause bug. Delta-sat via b2, x in [2,3], v = 2.
TEST(UpstreamIssuePins, Issue324_OnlyLastDisjunctSat_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-const b1 Bool)\n"
      "(declare-const b2 Bool)\n"
      "(declare-const x Real)\n"
      "(declare-const v Real)\n"
      "(assert (or b1 b2))\n"
      "(assert (not (and b1 b2)))\n"
      "(assert (= v (ite b1 1.0 2.0)))\n"
      "(assert (or\n"
      "  (and b1 (>= x 0) (<= x 1) (= v 2.0))\n"
      "  (and b2 (>= x 2) (<= x 3) (= v 2.0))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// Audit adversarial variant (reconstructed from the audit's
// "genuinely-unsat control" description): BOTH disjuncts contradict the ITE
// (b1 forces v = 1.0 vs (= v 2.0); b2 forces v = 2.0 vs (= v 1.0)), each by
// margin 1 >> delta, and exactly one of b1/b2 must hold — so unsat is the
// only correct verdict. This is the control that a masked-clause "fix" in
// the style of upstream 460639c05 must not weaken into a false delta-sat
// (COMPLETENESS, asserts phi^delta T-satisfiable on a T-unsatisfiable phi —
// missed refutation).
TEST(UpstreamIssuePins, Issue324_BothDisjunctsInfeasible_Unsat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-const b1 Bool)\n"
      "(declare-const b2 Bool)\n"
      "(declare-const x Real)\n"
      "(declare-const v Real)\n"
      "(assert (or b1 b2))\n"
      "(assert (not (and b1 b2)))\n"
      "(assert (= v (ite b1 1.0 2.0)))\n"
      "(assert (or\n"
      "  (and b1 (>= x 0) (<= x 1) (= v 2.0))\n"
      "  (and b2 (>= x 2) (<= x 3) (= v 1.0))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// ---------------------------------------------------------------------------
// #323 — let inside a define-fun body must SUBSTITUTE, not assert its
// bindings at parse time (upstream bug, fixed there by fork commit
// e6b3a2f83; this fork was never affected). Pins: soonho's 4
// define_fun_let_*.smt2 verdict delta-sat, and get-value returns the exact
// constants the audit cites (A_TEST = 4; triple-nested-let TEST = 9).
//
// KNOWN RESIDUAL (out of scope here, found while writing these pins): the
// LITERAL issue-body reproducer — get-value on a let-inside-define-fun
// application with NO assertions and NO declared variables (empty model box)
// — still prints the upstream report's garbage interval (~ -1.66e300), and
// the no-assert variant of define_fun_let_04 dies of SIGFPE. Both verdicts
// (delta-sat on an assertion-free query) are correct; the defect is confined
// to expression evaluation over the EMPTY box, and does not reproduce for
// constant or let-free define-fun get-value. Deliberately NOT embedded here:
// an in-process SIGFPE would kill the whole suite. Reported in the T11 task
// report for follow-up.
// ---------------------------------------------------------------------------

// soonhokong/dreal4@20251226 dreal/test/smt2/define_fun_let_01.smt2
// (blob a9ed01575f): A_TEST = (3-1) + (4-2) = 4. .expected: delta-sat.
TEST(UpstreamIssuePins, Issue323_DefineFunLet01_Substitutes_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(define-fun A ((x_1 Real) (y_1 Real) (x_2 Real) (y_2 Real)) Real\n"
      "  (let ((dx (- x_2 x_1))\n"
      "        (dy (- y_2 y_1)))\n"
      "    (+ dx dy)))\n"
      "(define-fun A_TEST () Real (A 1.0 2.0 3.0 4.0))\n"
      "(assert (= A_TEST 4.0))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// soonhokong/dreal4@20251226 dreal/test/smt2/define_fun_let_02.smt2
// (blob 04f9a26190): RESULT = (3+4)*2 = 14. .expected: delta-sat.
TEST(UpstreamIssuePins, Issue323_DefineFunLet02_NestedLet_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(define-fun nested ((a Real) (b Real)) Real\n"
      "  (let ((sum (+ a b)))\n"
      "    (let ((doubled (* sum 2)))\n"
      "      doubled)))\n"
      "(define-fun RESULT () Real (nested 3.0 4.0))\n"
      "(assert (= RESULT 14.0))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// soonhokong/dreal4@20251226 dreal/test/smt2/define_fun_let_03.smt2
// (blob 2d9d67bdef) — inner let references the outer binding:
// TEST = (2+3)^2 = 25. .expected: delta-sat.
TEST(UpstreamIssuePins, Issue323_DefineFunLet03_InnerRefsOuter_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(define-fun nested_ref ((a Real) (b Real)) Real\n"
      "  (let ((sum (+ a b)))\n"
      "    (let ((result (* sum sum)))\n"
      "      result)))\n"
      "(define-fun TEST () Real (nested_ref 2.0 3.0))\n"
      "(assert (= TEST 25.0))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// soonhokong/dreal4@20251226 dreal/test/smt2/define_fun_let_04.smt2
// (blob 0716f335ce) — triple-nested let: TEST = ((5+1)*2)-3 = 9. .expected:
// delta-sat.
TEST(UpstreamIssuePins, Issue323_DefineFunLet04_TripleNestedLet_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(define-fun triple ((x Real)) Real\n"
      "  (let ((a (+ x 1)))\n"
      "    (let ((b (* a 2)))\n"
      "      (let ((c (- b 3)))\n"
      "        c))))\n"
      "(define-fun TEST () Real (triple 5.0))\n"
      "(assert (= TEST 9.0))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// The audit's exact-value citation "A_TEST = 4": define_fun_let_01 plus
// (get-value (A_TEST)). get-value prints exact rationals — the substituted
// let body must evaluate to the point value 4, not upstream #323's garbage
// interval.
TEST(UpstreamIssuePins, Issue323_GetValueATest_Exact4) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(define-fun A ((x_1 Real) (y_1 Real) (x_2 Real) (y_2 Real)) Real\n"
      "  (let ((dx (- x_2 x_1))\n"
      "        (dy (- y_2 y_1)))\n"
      "    (+ dx dy)))\n"
      "(define-fun A_TEST () Real (A 1.0 2.0 3.0 4.0))\n"
      "(assert (= A_TEST 4.0))\n"
      "(check-sat)\n"
      "(get-value (A_TEST))\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_NE(out.find("(A_TEST 4)"), std::string::npos) << "got: " << out;
}

// The audit's exact-value citation "nested-let TEST = 9": define_fun_let_04
// plus (get-value (TEST)).
TEST(UpstreamIssuePins, Issue323_GetValueTripleNestedLet_Exact9) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(define-fun triple ((x Real)) Real\n"
      "  (let ((a (+ x 1)))\n"
      "    (let ((b (* a 2)))\n"
      "      (let ((c (- b 3)))\n"
      "        c))))\n"
      "(define-fun TEST () Real (triple 5.0))\n"
      "(assert (= TEST 9.0))\n"
      "(check-sat)\n"
      "(get-value (TEST))\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_NE(out.find("(TEST 9)"), std::string::npos) << "got: " << out;
}

// ---------------------------------------------------------------------------
// #302 — delta-sat here is CORRECT delta-complete behavior. Do NOT "fix"
// this to a required unsat. expr1 and expr2 are syntactically identical, so
// (- expr1 expr2) is 0 wherever defined and the exact formula
// (> (- expr1 expr2) 0) is T-unsatisfiable — but its delta-weakening
// (> (- expr1 expr2) -delta) is satisfied by EVERY point of the domain, so
// the query sits squarely in the delta-boundary zone where the
// delta-decision contract admits the delta-sat answer. (The remaining
// artifact upstream reported — identical aux expressions printed with
// different overflow-region intervals near p = 1 — is cosmetic model output,
// same +/-inf mechanism as #265; the verdict is not in question.) Verbatim
// issue-body encoding.
// ---------------------------------------------------------------------------
TEST(UpstreamIssuePins, Issue302_IdenticalExprsStrictDiff_DeltaSatIsCorrect) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-fun f () Int)\n"
      "(declare-fun p () Real)\n"
      "(declare-fun z () Real)\n"
      "(define-fun expr1 () Real\n"
      "  (ite (= f 0)\n"
      "    (+ (/ p (- 1.0 p)) z)\n"
      "    0.0\n"
      "  )\n"
      ")\n"
      "(define-fun expr2 () Real\n"
      "  (ite (= f 0)\n"
      "  (+ (/ p (- 1.0 p)) z)\n"
      "    0.0\n"
      "  )\n"
      ")\n"
      "(assert (<= 0 p))\n"
      "(assert (<= p 1))\n"
      "(assert (<= 0 z))\n"
      "(assert (<= z 25))\n"
      "(assert (<= 0 f))\n"
      "(assert (<= f 1))\n"
      "(assert (> (- expr1 expr2) 0))\n"
      "(check-sat)\n"
      "(exit)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

// ---------------------------------------------------------------------------
// #315 — a Boolean-valued define-fun body must constrain. Upstream ignored
// the body and returned sat on (eqfn a 1.0) AND (eqfn a 2.0) — a spurious
// sat (COMPLETENESS-class: asserts phi^delta T-satisfiable on a
// T-unsatisfiable phi — missed refutation; the gap |1-2| = 1 >> delta). This
// fork expands eqfn to (= a 1.0) AND (= a 2.0) and correctly answers unsat.
// Verbatim issue-body reproducer; on unsat, its (get-model) prints
// (error "model is not available") rather than crashing.
// ---------------------------------------------------------------------------
TEST(UpstreamIssuePins, Issue315_BooleanDefineFunContradiction_Unsat) {
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(declare-fun a () Real)\n"
      "(define-fun eqfn ((x Real) (y Real)) Bool (= x y))\n"
      "(assert (eqfn a 1.0))\n"
      "(assert (eqfn a 2.0))\n"
      "(check-sat)\n"
      "(get-model)\n"
      "(exit)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_NE(out.find("(error \"model is not available\")"), std::string::npos)
      << "got: " << out;
}

}  // namespace
}  // namespace dreal
