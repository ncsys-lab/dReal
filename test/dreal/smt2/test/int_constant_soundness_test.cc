// End-to-end SMT2-path regression guards for upstream dReal4 issues #264,
// #284, #280 — Int / large-constant correctness. Each test embeds the exact
// upstream reproducer and pins the correct verdict.
//
//   #264  large-integer constant fold        -> delta-sat (not false unsat)
//         SOUNDNESS (asserts phi T-unsatisfiable on a T-satisfiable phi —
//         false unsat): the sums differ by exactly 1, but 18000000000000001
//         exceeds 2^53 and a rounding constant fold collapses both sides to
//         the same double.
//   #284  Int-sorted constant >= 2^31        -> delta-sat (not false unsat)
//         SOUNDNESS-class (asserts phi T-unsatisfiable on a T-satisfiable,
//         assertion-free phi — false unsat): an implicit int32-sized default
//         domain on Int variables excludes the asserted value.
//   #280  Int existentials under forall      -> unsat (not invalid delta-sat)
//         COMPLETENESS (asserts phi^delta T-satisfiable on a T-unsatisfiable
//         phi — missed refutation): the reported model violates the
//         forall-equality at p=0 by ~21 >> delta=0.01. The Real-sorted
//         variant of the same query is correctly unsat.

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// RAII redirect of std::cout, restored on BOTH the normal and exception
// paths (same rationale as dreal_bugs_regression_test.cc).
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

// Parse an SMT2 string (its (check-sat) prints the verdict to std::cout) and
// return the captured output. Default Config precision is 0.001.
std::string RunSmt2String(const std::string& smt2,
                          const Config& config = Config{}) {
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  const CoutRedirect redirect{captured.rdbuf()};
  driver.parse_string(smt2);
  return captured.str();
}

// dreal/dreal4#264 — the two sums differ by exactly 1, so the negated
// equality is T-satisfiable (trivially: it contains no variables) and the
// verdict must be delta-sat even at precision 1e-100. A constant fold that
// rounds 18000000000000001 (> 2^53) to 18000000000000000 collapses both
// sides to the same double and yields a false unsat — SOUNDNESS (asserts phi
// T-unsatisfiable on a T-satisfiable phi).
TEST(IntConstantSoundness, Issue264_LargeIntegerFold_DeltaSat) {
  Config config;
  config.mutable_precision().set_from_command_line(1e-100);
  const std::string out{RunSmt2String(
      "(set-logic QF_NRA)\n"
      "(assert (not (= (+ 9000000000000000 9000000000000001)"
      " (+ 9000000000000000 9000000000000000))))\n"
      "(check-sat)\n",
      config)};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("unsat"), std::string::npos) << "got: " << out;
}

// dreal/dreal4#284 — an assertion-free query defining an Int constant 2^31
// must be delta-sat (the issue's 0x7FFFFFFF = 2^31 - 1 variant already is).
// An int32-sized Int domain makes x = 2147483648 contract to empty — false
// unsat, SOUNDNESS-class (asserts phi T-unsatisfiable on a T-satisfiable
// phi).
TEST(IntConstantSoundness, Issue284_IntConstantAboveInt32_DeltaSat) {
  const std::string out{RunSmt2String(
      "(define-fun x() Int 0x80000000)\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("delta-sat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("unsat"), std::string::npos) << "got: " << out;
}

// dreal/dreal4#280 — Int-sorted existentials under forall. The
// forall-equality (forall p in [-0.1, 1.1]. f + p e + c p^2 = 1) pins
// c = e = 0, f = 1 over the integers even after delta=0.01 weakening (any
// nonzero integer e or c moves the polynomial by >= 0.25 somewhere in the
// binder range), which forces d = -1 and then falsifies the last forall at
// p = q = 0.6 by 0.14 >> delta. phi and phi^delta are both T-unsatisfiable:
// the verdict must be unsat, as it already is for the Real-sorted variant.
// Returning delta-sat (upstream model c=1, d=16, e=-14, f=10 violates the
// forall-equality at p=0 by 9 >> delta) is a missed refutation —
// COMPLETENESS (asserts phi^delta T-satisfiable on a T-unsatisfiable phi).
TEST(IntConstantSoundness, Issue280_IntForallEquality_Unsat) {
  const std::string out{RunSmt2String(
      "(set-option :precision 0.01)\n"
      "(declare-const c Int)\n"
      "(declare-const d Int)\n"
      "(declare-const e Int)\n"
      "(declare-const f Int)\n"
      "(assert (and (> d -50) (< d 50)))\n"
      "(assert (and (> e -50) (< e 50)))\n"
      "(assert (and (> c -50) (< c 50)))\n"
      "(assert (and (> f -50) (< f 50)))\n"
      "(assert (forall ( (p Real [0, 1]) (q Real [0, 1])) true))\n"
      "(assert (forall ( (p Real [0, 1])) (and (<= 0 (+ f (* (/ 1 2) e)"
      " (* (/ 1 4) c) (* p e) (* c (pow p 2)) (* (/ 1 2) p d)))"
      " (<= (+ f (* (/ 1 2) e) (* (/ 1 4) c) (* p e) (* c (pow p 2))"
      " (* (/ 1 2) p d)) 1))))\n"
      "(assert (forall ( (p Real [-0.1, 1.1]))"
      " (= (+ f (* p e) (* c (pow p 2))) 1)))\n"
      "(assert (= (+ d f (* 2 c) (* 2 e)) 0))\n"
      "(assert (>= (+ e f (* (/ 1 2) c) (* (/ 1 4) d)) (/ 1 2)))\n"
      "(assert (forall ( (p Real [0.6, 1]) (q Real [0.6, 1]))"
      " (< (+ f (* p e) (* q e) (* c (pow p 2)) (* c (pow q 2)) (* p q d))"
      " (/ 1 2))))\n"
      "(check-sat)\n")};
  EXPECT_NE(out.find("unsat"), std::string::npos) << "got: " << out;
  EXPECT_EQ(out.find("delta-sat"), std::string::npos) << "got: " << out;
}

}  // namespace
}  // namespace dreal
