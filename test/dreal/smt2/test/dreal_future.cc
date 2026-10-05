// Aspirational (not regression) specifications for FUTURE dReal semantics along
// two independent roadmaps:
//   • negated ODE constraints in QF_NRA_ODE — the BUG-002 roadmap
//     (simulink-to-dreal_bug_reports.md §BUG-002), Design Axes 1–2 below;
//   • negated NRA `forall` — the FEAT-001 roadmap
//     (ode_expressivity_energy/docs/dreal-bugs.md §FEAT-001), Design Axis 3 below.
//
// WHAT BUG-002 IS. Inside the DPLL(T) loop a negated `(integral …)` or
// `(forall_t …)` literal builds no contractor: `link_integral_invariants`
// (src/dreal/contractor/odes/contractor_odes.h, its is_negation branch) logs
// "Inverted ODE constraints are currently ignored" at DEBUG. Until 2026-10-05 a
// user assertion `(not (integral …))` reached that drop, and the state variable
// reverted to its declared range. Since then RejectNonPositiveOde
// (context_impl.cc) rejects, at assert time, any ODE atom in negative or mixed
// polarity or inside an NRA forall body (Group A3 below), so the in-loop drop
// only sees negations the SAT solver produces, where it is exact. The effect is
// verdict-level, reachable only through the full solver, so these tests drive
// RunSmt2String (parse → assert → theory_solver), like
// dreal_bugs_regression_test.cc. Every test below either (a) is a control or a
// live rejection, or (b) is GTEST_SKIP-ed and encodes DESIRED-but-unimplemented
// semantics. Remove a skip to watch the current build reject the input.
//
// ─────────────────────────────────────────────────────────────────────────────
// DESIGN AXIS 1 — negated forall_t: settled as genuine ∃-semantics.
//
//   (forall_t flow [0,T] φ)  ≡  ∀t∈[0,T]. φ(x(t))      [trajectory invariant]
//   ¬(forall_t flow [0,T] φ) ≡  ∃t∈[0,T]. ¬φ(x(t))     [a witnessing time exists]
//
// Refutable and meaningful: negating an invariant that TRULY holds on the whole
// trajectory is unsat (no witnessing t); negating one violated somewhere is
// delta-sat. Implementation-wise this is the per-slice tube check (contractor_
// odes.h step 4) with its accept/refute polarity inverted: a positive forall_t
// empties the box when SOME slice violates φ; a negated forall_t must empty the
// box when EVERY slice satisfies φ.
//
// KEY STRUCTURAL FACTS (link_integral_invariants, contractor_odes.h) —
// two silent-drop hazards in BUG-002's own spirit, both verified empirically
// while writing these tests:
//   (1) A forall_t — positive OR negated — is inert without a companion
//       *positive* (integral …). The trajectory tube is produced by the integral
//       contractor; a forall_t is only ever used by being attached to an integral
//       in int_ctrs. No positive integral ⇒ no tube ⇒ nothing for ∃ to range over
//       (documented by NoIntegral_DesignGap below).
//   (2) The forall_t invariant must range over a variable in the integral's
//       vars_t (the ENDPOINT var x_t), NOT the bare flow-template var x. The link
//       test is `vars(invariant) ⊆ ic.vars_t`; {x} ⊄ {x_t}, so an invariant
//       phrased over `x` cannot link and is silently dropped (spurious delta-sat
//       regardless of whether the invariant held) — a BUG-002-class hazard. Hence
//       every positive forall_t here is written `(… x_t …)`. The per-slice tube
//       filter evaluates that endpoint var against EACH trajectory slice, so
//       "x_t ≤ c" means "the state ≤ c at every time", not merely at the endpoint.
//
//       This silent drop is now a HARD ERROR for user assertions (Group A2
//       below pins it; live since 2026-07-13). It CANNOT be enforced inside
//       link_integral_invariants, because that function runs in the DPLL(T)
//       loop on the SAT solver's CURRENT literal subset — where a positive
//       forall_t routinely appears WITHOUT its companion integral (a different
//       flow/step is active, or the integral atom is unassigned in this node).
//       Throwing there crashes legitimate multi-step BMC benchmarks
//       (empirically: the github airplane family). Distinguishing a genuinely-
//       unlinkable (malformed) forall_t from a transiently-unlinked (valid
//       search) one needs the GLOBAL set of integrals — so the throw lives in
//       RejectUnlinkedForallT (context_impl.cc), run once per check-sat over
//       the full assertion stack, sharing the linker's predicate
//       (forallt_links_to_integral, contractor_odes.h).
//       The NEGATED forall_t / integral drops (BUG-002 proper, Axis 1/2 below)
//       sit on the is_negation branch and have the SAME in-loop constraint: a
//       negated ODE literal is a normal product of DPLL(T) search. So the
//       rejection of a user-asserted negation lives at assert time instead
//       (RejectNonPositiveOde, context_impl.cc, live since 2026-10-05); the
//       Axis 1/2 semantics that could replace it stay deliberately open.
//
// ─────────────────────────────────────────────────────────────────────────────
// DESIGN AXIS 2 — negated integral: deliberately left OPEN; both rival semantics
// are specified below side-by-side so a future implementation can A/B them. They
// disagree on the verdict for the SAME input, which is the whole point.
//
//   Atom:  (= [x_t] (integral 0 T [x_0] flow))      "x_t is the ODE endpoint"
//
//   (a) DISEQUALITY  —  ¬(x_t = ∫)  ≡  x_t ≠ endpoint     [negation distributes]
//       Standard first-order logic; compositional; NNF-/substitution-safe.
//       BUT δ-near-vacuous: a disequality is satisfiable for almost any box, so
//       it rarely changes a verdict. Consequences:
//         • BUG-002's own reproducer (¬integral ∧ x_t>95) is **delta-sat**, NOT
//           the doc's "expected: unsat" — see Repro_Disequality below. The doc's
//           expectation is a misanalysis (it assumed the integral still binds).
//         • A translator bug that negates an integral is INVISIBLE at the verdict
//           level (still delta-sat); only the silent-drop *diagnostic* is missed.
//           The lone refutable case is when x_t is otherwise pinned to the
//           endpoint (PinnedEndpoint_Disequality), and even that is δ-fragile
//           (the pin and the true endpoint must differ by > δ to refute, which a
//           point disequality cannot guarantee).
//
//   (b) DEFINITIONAL BINDING  —  the (integral …) atom is hoisted OUT of any
//       enclosing negation and ALWAYS binds x_t to the endpoint; only the
//       surrounding property is negated.
//       Matches the translator's intent and the doc's "expected: unsat" (Repro_
//       Definitional below). BUT:
//         • non-compositional — negation does not distribute, so NNF, polarity
//           pushing, and substitutivity all break for formulas containing ODE
//           atoms; the solver must special-case ODE atoms out of normal Boolean
//           semantics (a standing soundness/maintenance hazard — an (integral …)
//           buried inside a disjunction would still force-bind).
//         • makes "x_t is NOT the endpoint" inexpressible — the negation has no
//           property to attach to and degenerates to delta-sat (PinnedEndpoint_
//           Definitional below).
//
//   Verdict divergence table (decay flow, x_0=100, T=1, endpoint≈60.6531):
//     input                                   | disequality | definitional
//     ¬integral ∧ x_t>95                       | delta-sat   | unsat
//     (= x_t 60.6531) ∧ ¬integral              | unsat (δ!)  | delta-sat
//     posForallT(holds) ∧ ¬integral ∧ x_t>95   | delta-sat   | unsat
//
// Reference flow for Axes 1–2: d/dt[x] = -0.5·x, x_0=100, T=1.
//   true endpoint  = 100·e^−0.5 ≈ 60.6531
//   trajectory range over [0,1] = [60.65, 100], monotone decreasing.
//
// ─────────────────────────────────────────────────────────────────────────────
// DESIGN AXIS 3 — negated NRA `forall`: existential semantics (FEAT-001).
// A pure-NRA quantifier roadmap, UNRELATED to the ODE axes above; it shares this
// file only for its aspirational role. Group E below.
//
//   (forall ((x Real [lb,ub])) φ)        ≡  ∀x∈[lb,ub]. φ(x)
//   (not (forall ((x Real [lb,ub])) φ))  ≡  ∃x∈[lb,ub]. ¬φ(x)      [a witness exists]
//
// dReal has no ∃ node, but a top-level free variable IS an existential — so a
// negated forall is dischargeable by promoting its bound vars to fresh free box
// vars over [lb,ub] and asserting ¬φ (the binder domain becomes a conjunct:
// ¬(domain→matrix) = domain ∧ ¬matrix). The framework's blocked antecedent
// refutation ∃p̄.(φ(p̄) ∨ ¬∀x̄.d) is then a native NRA SAT query. Implementation-
// wise this INVERTS the CE-search the CEGIS ContractorForall already runs
// (contractor_forall.h context_for_counterexample_): a counterexample found ⇒
// ∃x̄.¬d sat (the ¬∀ literal holds); none found ⇒ prune the box to empty.
//
// CURRENT STATE differs from Axes 1–2. A negated/disjoined/nested forall is NOT
// silently mis-solved — it is HARD-REJECTED up front by RejectUnsupportedForall
// (solver/context_impl.cc). So removing a skip below yields a THROW, not a wrong
// verdict: the gap here is "can't answer yet," not "answers wrong."
//
// SOUNDNESS: dReal `unsat` on the negation ⟺ the universal holds — no false
// `unsat`. A future ∃-implementation only affects COMPLETENESS (refutation power).
//
// BOUNDARY (stays rejected even after FEAT-001): a POSITIVE forall whose body
// contains a forall is genuine ∀∃ alternation — the bound var cannot be both a
// CEGIS-universal and an existential, and ∃ cannot be hoisted past the outer ∀.
// Pinned by NegForall_UnderPositiveForall_StaysRejected.

#include "dreal/smt2/driver.h"

#include <iostream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/solver/context.h"

namespace dreal {
namespace {

// RAII redirect of std::cout, restored on BOTH the normal and the exception
// paths. This matters here (unlike the non-throwing regression file this helper
// was copied from): several tests below intentionally make parse_string throw,
// and a plain "restore after parse_string" would be skipped during unwinding,
// leaving std::cout pointing at a destroyed local buffer — the next writer to
// std::cout (e.g. another test) then segfaults.
struct CoutRedirect {
  explicit CoutRedirect(std::streambuf* buf) : old_{std::cout.rdbuf(buf)} {}
  ~CoutRedirect() { std::cout.rdbuf(old_); }
  std::streambuf* old_;
};

// Parse an SMT2 string (its (check-sat) prints the verdict to std::cout) and
// return the captured output. Default Config precision is 0.001 — the delta the
// reference reproducers use. Based on dreal_bugs_regression_test.cc's runner, made
// exception-safe (see CoutRedirect) so the throwing tests don't corrupt cout.
std::string RunSmt2String(const std::string& smt2) {
  Config config;
  Smt2Driver driver{Context{config}};
  std::ostringstream captured;
  const CoutRedirect redirect{captured.rdbuf()};
  driver.parse_string(smt2);
  return captured.str();
}

// Shared QF_NRA_ODE preamble: the decay flow d/dt[x] = -0.5·x and its variables.
// Each test appends its own (assert …)(check-sat). `time` is pinned to [1,1] so
// the integration horizon T = 1.
const char* const kDecayPreamble =
    "(set-logic QF_NRA_ODE)\n"
    "(declare-fun x () Real [0.000000, 100.000000])\n"
    "(declare-fun x_0 () Real [0.000000, 100.000000])\n"
    "(declare-fun x_t () Real [0.000000, 100.000000])\n"
    "(declare-fun time () Real [1.000000, 1.000000])\n"
    "(declare-fun mode () Real [1.000000, 1.000000])\n"
    "(define-ode flow_1 ((= d/dt[x] (* -0.5 x))))\n";

bool HasDeltaSat(const std::string& out) {
  return out.find("delta-sat") != std::string::npos;
}
// "unsat" must be matched as its own token: "delta-sat" does not contain it, but
// be explicit so a future verdict-string change can't silently confuse them.
bool HasUnsat(const std::string& out) {
  return out.find("unsat") != std::string::npos &&
         out.find("delta-sat") == std::string::npos;
}

// =============================================================================
// Group A — positive forall_t CONTROLS (not skipped; must pass on today's build).
// These establish that the per-slice tube machinery runs end-to-end through the
// SMT2 path, and pin the baseline polarity that Axis-1's ∃-semantics inverts.
// =============================================================================

// Invariant x ≤ 150 holds on the whole trajectory (max is 100), so the forall_t
// imposes nothing and x_t≈60.65 > 50 is reachable. (Margin 50 ≫ outward-rounding
// slop, so the holding invariant is not spuriously flagged at the t=0 slice.)
TEST(Bug002NegatedOde, PosForallT_InvariantHolds_DeltaSat) {
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (forall_t 1 [0 time] (<= x_t 150))\n"
      "  (> x_t 50)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// Invariant x ≤ 50 is violated on the ENTIRE horizon (x stays ≥ 60.65 > 50 for
// all t ∈ [0,1]; gap ≥ 10.65 ≫ δ), so the positive forall_t must refute: unsat.
TEST(Bug002NegatedOde, PosForallT_InvariantViolated_Unsat) {
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (forall_t 1 [0 time] (<= x_t 50))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

// =============================================================================
// Group A2 — silent-unlink IS a hard error (LIVE since 2026-07-13; previously
// aspirational). A positive forall_t whose invariant cannot be linked to a
// companion (integral …) over the same flow used to be silently dropped,
// returning a spurious delta-sat — a BUG-002-class hazard (COMPLETENESS: a
// dropped invariant only enlarges the box → missed refutation / false
// delta-sat, never false unsat); it surfaced downstream as simulink-to-dreal
// BUG-010.
//
// WHY THE THROW IS NOT IN link_integral_invariants. That function runs inside
// the DPLL(T) loop on the SAT solver's current literal subset, where a positive
// forall_t routinely appears WITHOUT its companion integral (another flow/step
// is active, or the integral atom is unassigned in this search node). Throwing
// there crashes legitimate multi-step BMC benchmarks (empirically confirmed on
// the github airplane family). Telling a genuinely-malformed forall_t (its
// variable is in NO integral anywhere in the problem) apart from a transiently-
// unlinked one (valid mid-search) requires the GLOBAL integral set — so the
// throw lives in RejectUnlinkedForallT (context_impl.cc), run once per
// check-sat over the full assertion stack, sharing the linker's predicate
// (forallt_links_to_integral, contractor_odes.h).
// =============================================================================

// A positive forall_t with NO companion integral at all: no trajectory tube,
// nothing to check the invariant against ⇒ throws rather than vacuously
// delta-sat. (Contrast NegForallT_NoIntegral_DesignGap below — the *negated*
// form on the separate negation path.)
TEST(Bug002NegatedOde, PosForallT_NoIntegral_ShouldReject) {
  EXPECT_ANY_THROW(RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (forall_t 1 [0 time] (<= x_t 50))\n"
      "))\n"
      "(check-sat)\n"));
}

// A positive forall_t whose invariant ranges over the bare flow-template var `x`
// instead of the integral's endpoint var `x_t`: the integral IS present, but the
// link test vars(inv)={x} ⊆ vars_t={x_t} fails, so the invariant can never link.
// This is the exact mistake that slipped through as a false delta-sat (it is how
// the initial draft of PosForallT_InvariantViolated failed to refute, and how
// simulink-to-dreal BUG-010 arose). Throws — `x` appears in no integral's vars_t
// anywhere, so this IS distinguishable from a transient search drop at global scope.
TEST(Bug002NegatedOde, PosForallT_InvariantOverFlowVar_ShouldReject) {
  EXPECT_ANY_THROW(RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (forall_t 1 [0 time] (<= x 50))\n"
      "))\n"
      "(check-sat)\n"));
}

// =============================================================================
// Group A3 — an ODE atom in negative or mixed polarity IS a hard error (LIVE
// since 2026-10-05). The theory solver enforces (integral …) and (forall_t …)
// only positively: it drops a negated ODE literal (which DPLL(T) produces
// legitimately for a positive-only atom, so the drop itself must stay quiet —
// Group A2's rationale). A user formula that puts an ODE atom under `not`, an
// `=>` antecedent, a Boolean `=`, `xor` or an ite condition (the parser turns
// all of them into Not), or inside an NRA `forall` body, was therefore solved
// without it — COMPLETENESS (asserts φ^δ T-satisfiable on a possibly
// T-unsatisfiable φ — missed refutation). Context::Impl::Assert now rejects it
// with the whole assertion in view (RejectNonPositiveOde). Positive occurrences
// under `or` / `and` / an `=>` consequent stay accepted (s2d's implied
// integrals, dReach's mode-guarded invariants).
// =============================================================================

// The rejection names its reason; a different throw (e.g. the unlinked
// forall_t check) must not satisfy these tests.
void ExpectNonPositiveOdeRejected(const std::string& smt2,
                                  const std::string& phrase = "negative polarity") {
  try {
    const std::string out{RunSmt2String(smt2)};
    ADD_FAILURE() << "expected a rejection, got: " << out;
  } catch (const std::exception& e) {
    EXPECT_NE(std::string{e.what()}.find(phrase), std::string::npos) << e.what();
  }
}

TEST(Bug002NegatedOde, NegIntegral_Rejected) {
  ExpectNonPositiveOdeRejected(
      std::string(kDecayPreamble) +
      "(assert (and (= x_0 100) (not (= [x_t] (integral 0. time [x_0] flow_1))) "
      "(> x_t 95)))\n(check-sat)\n");
}

TEST(Bug002NegatedOde, NegForallT_WithIntegral_Rejected) {
  ExpectNonPositiveOdeRejected(
      std::string(kDecayPreamble) +
      "(assert (and (= x_0 100) (= [x_t] (integral 0. time [x_0] flow_1)) "
      "(not (forall_t 1 [0 time] (<= x_t 150)))))\n(check-sat)\n");
}

TEST(Bug002NegatedOde, ImplicationAntecedentIntegral_Rejected) {
  ExpectNonPositiveOdeRejected(
      std::string(kDecayPreamble) +
      "(assert (= x_0 100))\n"
      "(assert (=> (= [x_t] (integral 0. time [x_0] flow_1)) (> x_t 95)))\n"
      "(check-sat)\n");
}

// (= g I) is mixed polarity: (or (not g) I) and (or (not I) g).
TEST(Bug002NegatedOde, BoolEqualityIntegral_Rejected) {
  ExpectNonPositiveOdeRejected(
      std::string(kDecayPreamble) +
      "(declare-fun g () Bool)\n"
      "(assert (= x_0 100))\n"
      "(assert (= g (= [x_t] (integral 0. time [x_0] flow_1))))\n"
      "(check-sat)\n");
}

// An ite condition is lifted to both polarities by ITE elimination; the check
// runs after it (a relational formula hides an ite condition's ODE atom).
TEST(Bug002NegatedOde, IteConditionIntegral_Rejected) {
  ExpectNonPositiveOdeRejected(
      std::string(kDecayPreamble) +
      "(declare-fun y () Real [0, 1])\n"
      "(assert (= x_0 100))\n"
      "(assert (= y (ite (= [x_t] (integral 0. time [x_0] flow_1)) 1 0)))\n"
      "(check-sat)\n");
}

// An ODE atom inside an NRA forall body: the theory solver skips the whole
// forall, so it cannot be enforced in either polarity. (Before, the positive
// case aborted in relational_formula_evaluator.cc "Should not be reachable".)
TEST(Bug002NegatedOde, IntegralInsideForallBody_Rejected) {
  ExpectNonPositiveOdeRejected(
      std::string(kDecayPreamble) +
      "(assert (= x_0 100))\n"
      "(assert (> x_t 95))\n"
      "(assert (forall ((y Real [0, 1])) (or (= [x_t] (integral 0. time [x_0] flow_1)) "
      "(> y 2))))\n"
      "(check-sat)\n",
      "inside a (forall …) body");
}

// Controls: positive occurrences are accepted and enforced.
TEST(Bug002NegatedOde, GuardedIntegral_GuardTrue_Unsat) {
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(declare-fun g () Bool)\n"
      "(assert g)\n"
      "(assert (or (not g) (= [x_t] (integral 0. time [x_0] flow_1))))\n"
      "(assert (= x_0 100))\n"
      "(assert (> x_t 95))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

TEST(Bug002NegatedOde, GuardedIntegral_GuardFree_DeltaSat) {
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(declare-fun g () Bool)\n"
      "(assert (or (not g) (= [x_t] (integral 0. time [x_0] flow_1))))\n"
      "(assert (= x_0 100))\n"
      "(assert (> x_t 95))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

TEST(Bug002NegatedOde, ImplicationConsequentForallT_Accepted) {
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (= x_0 100))\n"
      "(assert (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "(assert (=> (= mode 1) (forall_t 1 [0 time] (<= x_t 150))))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// Two negations around the integral cancel: it is positive, so no rejection
// (a check for "any not above an ODE atom" would wrongly fire here).
TEST(Bug002NegatedOde, DoubleNegationParity_Accepted) {
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (= x_0 100))\n"
      "(assert (not (and (not (= [x_t] (integral 0. time [x_0] flow_1))) (< x_t 0))))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// =============================================================================
// Group B — negated forall_t, genuine ∃-semantics (skipped; Axis 1).
// Since 2026-10-05 these inputs are rejected (Group A3); the skipped tests keep
// the semantics a future implementation would put in place of the rejection,
// and their "current" notes describe the build before it.
// =============================================================================

// ¬(∀t. x ≤ 150) ≡ ∃t. x(t) > 150. Trajectory max is 100, gap 50 ≫ δ, so NO
// witnessing time exists ⇒ desired unsat. The flagship refutable ∃ case.
// CURRENT (negated forall_t dropped): the positive integral alone binds
// x_t≈60.65 with no further constraint ⇒ delta-sat.
TEST(Bug002NegatedOde, NegForallT_InvariantHolds_Unsat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, Axis-1 ∃): desired unsat (no t with "
                  "x>150), current (¬forall_t dropped) delta-sat. Remove skip "
                  "to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (not (forall_t 1 [0 time] (<= x_t 150)))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

// ¬(∀t. x ≥ 70) ≡ ∃t. x(t) < 70. x decays to ≈60.65 < 70 near t=1, so a witness
// exists ⇒ desired delta-sat. Current also delta-sat (right verdict, wrong
// reason — the constraint is dropped, not satisfied). Kept as the witness/refute
// companion to the case above: it guards a future ∃-implementation against
// OVER-refuting (emptying when a genuine witness exists).
TEST(Bug002NegatedOde, NegForallT_InvariantViolated_DeltaSat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, Axis-1 ∃): desired delta-sat (∃t "
                  "x<70 witnessed), current delta-sat (but via drop, not ∃). "
                  "Remove skip to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= [x_t] (integral 0. time [x_0] flow_1))\n"
      "  (not (forall_t 1 [0 time] (>= x_t 70)))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// Negated forall_t with NO positive integral ⇒ no trajectory tube for ∃ to range
// over (see KEY STRUCTURAL FACT in the header). Open design corner: the fail-loud
// choice is to RAISE ("forall_t with no companion integral"). Desired = raises.
// Before 2026-10-05 it was silently dropped (delta-sat, x_0 pinned, x/x_t free).
TEST(Bug002NegatedOde, NegForallT_NoIntegral_DesignGap) {
  // LIVE since 2026-10-05 (Group A3): rejected for its negative polarity.
  ExpectNonPositiveOdeRejected((
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (not (forall_t 1 [0 time] (<= x_t 50)))\n"
      "))\n"
      "(check-sat)\n"));
}

// =============================================================================
// Group C — negated integral, BOTH rival semantics (skipped; Axis 2). Rejected
// since 2026-10-05 (Group A3); see the Group B note.
// Each scenario runs the SAME smt2 under both readings; the verdicts diverge.
// =============================================================================

// Scenario I — BUG-002's exact reproducer: ¬(x_t = ∫) ∧ x_t > 95.
//
// (a) DISEQUALITY: x_t ≠ 60.65 ∧ x_t ∈ (95,100] is satisfiable (60.65 ∉ that
//     range) ⇒ delta-sat. This OVERTURNS the bug doc's "expected: unsat".
//     NOTE: current (drop) also yields delta-sat, so this test passes even
//     un-skipped — that coincidence is NOT evidence the disequality is honored;
//     the distinguishing case is PinnedEndpoint below.
TEST(Bug002NegatedOde, NegIntegral_Repro_Disequality_DeltaSat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, Axis-2 disequality): desired "
                  "delta-sat (x_t≠60.65 ∧ x_t>95), current delta-sat. Overturns "
                  "the doc's 'expected unsat'. Remove skip to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (not (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "  (> x_t 95)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// (b) DEFINITIONAL: the integral is hoisted out of the negation and binds
//     x_t≈60.65 unconditionally; x_t > 95 is then impossible ⇒ unsat. Matches
//     the doc's intent. Diverges from the disequality verdict on identical text.
//     CURRENT (drop): delta-sat — so this DOES fail when un-skipped.
TEST(Bug002NegatedOde, NegIntegral_Repro_Definitional_Unsat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, Axis-2 definitional): desired unsat "
                  "(integral force-binds x_t≈60.65, x_t>95 impossible), current "
                  "delta-sat (dropped). Remove skip to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (not (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "  (> x_t 95)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

// Scenario II — endpoint-pinned: (= x_t 60.6531) ∧ ¬(x_t = ∫). The one shape
// where the two semantics' verdicts FLIP relative to Scenario I.
//
// (a) DISEQUALITY: x_t = 60.6531 ∧ x_t ≠ endpoint ⇒ desired unsat — the SOLE
//     case where a negated integral genuinely refutes. This is the test that
//     distinguishes "disequality honored" from "constraint dropped" (current ⇒
//     delta-sat). δ-FRAGILITY CAVEAT: the pin 60.6531 and the true endpoint
//     60.65307 differ by ~3e-5 < δ=0.001, so a faithful δ-decision may still
//     call this delta-sat — a point disequality cannot force separation > δ.
//     That fragility is itself the finding: disequality semantics can express a
//     refutable endpoint constraint only up to δ.
TEST(Bug002NegatedOde, NegIntegral_PinnedEndpoint_Disequality_Unsat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, Axis-2 disequality): desired unsat "
                  "(x_t pinned to endpoint ∧ x_t≠endpoint), current delta-sat "
                  "(dropped). δ-fragile — see comment. Remove skip to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= x_t 60.6531)\n"
      "  (not (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

// (b) DEFINITIONAL: the integral binds x_t≈60.65 (consistent with the pin) and
//     the negation has no surviving property to negate ⇒ degenerate delta-sat.
//     This is the concrete cost of definitional binding: "x_t is NOT the
//     endpoint" is INEXPRESSIBLE — wrapping the binding in ¬ changes nothing.
//     CURRENT (drop) also delta-sat (for the unrelated reason that the integral
//     is ignored), so this passes un-skipped — again a coincidence, not a check
//     that definitional binding is implemented.
TEST(Bug002NegatedOde, NegIntegral_PinnedEndpoint_Definitional_DeltaSat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, Axis-2 definitional): desired "
                  "delta-sat (negation degenerate; 'x_t≠endpoint' inexpressible), "
                  "current delta-sat. Remove skip to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (= x_t 60.6531)\n"
      "  (not (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// =============================================================================
// Group D — mixed combinatorial corners (skipped). The "what if one is negated
// and the other asserted, and vice versa" cases.
// =============================================================================

// Scenario III — positive forall_t (holds) + negated integral + x_t > 95.
// The positive invariant x ≤ 150 holds and constrains only the interior flow
// variable x; the endpoint variable x_t is governed by the (negated) integral.
//
// (a) DISEQUALITY: x_t ≠ endpoint leaves x_t free above 95 ⇒ delta-sat. (Under
//     drop the forall_t is ALSO inert here — no positive integral to attach to —
//     so current is delta-sat too.)
TEST(Bug002NegatedOde, Mixed_PosForallT_NegIntegral_Disequality_DeltaSat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, mixed, Axis-2 disequality): desired "
                  "delta-sat (x_t free above 95), current delta-sat. Remove "
                  "skip to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (forall_t 1 [0 time] (<= x_t 150))\n"
      "  (not (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "  (> x_t 95)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// (b) DEFINITIONAL: the integral force-binds x_t≈60.65 regardless of the ¬, so
//     x_t > 95 is impossible ⇒ unsat. Demonstrates that it is the endpoint
//     BINDING, not the invariant, that drives x_t — the two ODE constructs play
//     different roles. CURRENT (drop): delta-sat.
TEST(Bug002NegatedOde, Mixed_PosForallT_NegIntegral_Definitional_Unsat) {
  GTEST_SKIP() << "ASPIRATIONAL (BUG-002, mixed, Axis-2 definitional): desired "
                  "unsat (integral binds x_t≈60.65, x_t>95 impossible), current "
                  "delta-sat (dropped). Remove skip to exercise.";
  const std::string out{RunSmt2String(
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (forall_t 1 [0 time] (<= x_t 150))\n"
      "  (not (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "  (> x_t 95)\n"
      "))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

// Scenario IV — BOTH negated, no positive integral: ¬forall_t ∧ ¬integral.
// Doubly degenerate: the negated forall_t has no trajectory tube (no positive
// integral — KEY STRUCTURAL FACT), and the negated integral is at most a
// disequality. The fail-loud design choice is to RAISE on the trajectory-less
// ¬forall_t (same gap as NoIntegral_DesignGap). Desired = raises.
// CURRENT: both dropped ⇒ delta-sat. NOTE: add a *positive* integral here and
// the ∃-refutation of the negated forall_t would dominate the verdict
// regardless of the negated-integral reading — the integral axis becomes
// verdict-irrelevant once ¬forall_t refutes.
TEST(Bug002NegatedOde, Mixed_BothNegated_DesignGap) {
  // LIVE since 2026-10-05 (Group A3): rejected for its negative polarity.
  ExpectNonPositiveOdeRejected((
      std::string(kDecayPreamble) +
      "(assert (and\n"
      "  (= x_0 100)\n"
      "  (not (forall_t 1 [0 time] (<= x_t 150)))\n"
      "  (not (= [x_t] (integral 0. time [x_0] flow_1)))\n"
      "))\n"
      "(check-sat)\n"));
}

// =============================================================================
// Group E — negated NRA forall, existential semantics (Axis 3 / FEAT-001).
// Bodies embed the committed reproducers docs/dreal-bugs/bug001_*.smt2 (in the
// ode_expressivity_energy project). Controls (not skipped) pass on today's build;
// the GTEST_SKIP cases encode desired verdicts the current build instead HARD-
// REJECTS (throws) — remove a skip to watch RunSmt2String throw.
// =============================================================================

// CONTROL (passes today): the ∃(free J)∀(bound x) idiom dReal already lifts to a
// ContractorForall. J∈[-1,1], ∀x∈[-1,1]. -J²-x²≤0 (always true) ⇒ delta-sat.
// Pins the positive polarity that the negated cases invert.
TEST(NegatedNraForall, PosForall_Baseline_DeltaSat) {
  const std::string out{RunSmt2String(
      "(set-option :precision 0.000500000000000000)\n"
      "(declare-const J Real)\n"
      "(assert (>= J -1))\n"
      "(assert (<= J 1))\n"
      "(assert (forall ((x Real [-1, 1])) (<= (+ (* -1 (* J J)) (* -1 (* x x))) 0)))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// ASPIRATIONAL (Axis-3 ∃): ¬∀x∈[-1,1]. -x²≤0 ≡ ∃x. x²<0 — no witness ⇒ desired
// unsat. Flagship refutable. CURRENT: hard-rejected (throws). A delta-sat here
// would be COMPLETENESS (missed refutation / false delta-sat).
TEST(NegatedNraForall, NegForall_InvariantHolds_Unsat) {
  GTEST_SKIP() << "ASPIRATIONAL (FEAT-001, Axis-3 ∃): desired unsat (no x with "
                  "x²<0), current REJECTED (throws). Remove skip to exercise.";
  const std::string out{RunSmt2String(
      "(set-option :precision 0.000500000000000000)\n"
      "(assert (not (forall ((x Real [-1, 1])) (<= (* -1 (* x x)) 0))))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

// ASPIRATIONAL (Axis-3 ∃): ¬∀x∈[-1,1]. x≤0.5 ≡ ∃x∈[-1,1]. x>0.5 — witnessed at
// x≈1 ⇒ desired delta-sat. Guards a future ∃-impl against OVER-refuting; an unsat
// here would be SOUNDNESS (false unsat). CURRENT: hard-rejected (throws).
TEST(NegatedNraForall, NegForall_InvariantViolated_DeltaSat) {
  GTEST_SKIP() << "ASPIRATIONAL (FEAT-001, Axis-3 ∃): desired delta-sat (∃x "
                  "x>0.5 witnessed), current REJECTED (throws). Remove skip.";
  const std::string out{RunSmt2String(
      "(set-option :precision 0.000500000000000000)\n"
      "(assert (not (forall ((x Real [-1, 1])) (<= x 0.5))))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasDeltaSat(out)) << "got: " << out;
}

// ASPIRATIONAL (Axis-3 ∃): the real P2 antecedent refutation
// ∃J∈[-1,1]. (¬mem(J) ∨ ¬∀x. d), mem = 2J∈[-2,2], d = -J²-x²≤0. Both disjuncts
// are unsatisfiable within J∈[-1,1] ⇒ desired unsat (antecedent valid). The
// framework's actual blocked query. CURRENT: hard-rejected (throws).
TEST(NegatedNraForall, NegForall_NestedAntecedent_Unsat) {
  GTEST_SKIP() << "ASPIRATIONAL (FEAT-001, Axis-3 ∃): desired unsat (P2 "
                  "antecedent valid), current REJECTED (throws). Remove skip.";
  const std::string out{RunSmt2String(
      "(set-option :precision 0.000500000000000000)\n"
      "(declare-const J Real)\n"
      "(assert (>= J -1))\n"
      "(assert (<= J 1))\n"
      "(assert (or (< (* 2 J) -2)\n"
      "            (> (* 2 J) 2)\n"
      "            (not (forall ((x Real [-1, 1])) (<= (+ (* -1 (* J J)) (* -1 (* x x))) 0)))))\n"
      "(check-sat)\n")};
  EXPECT_TRUE(HasUnsat(out)) << "got: " << out;
}

// CONTROL (throws today, must STAY rejected after FEAT-001): a positive forall
// whose body holds a negated forall is genuine ∀∃ alternation — ∃ cannot be
// hoisted past the outer ∀. Pins the FEAT-001 boundary.
TEST(NegatedNraForall, NegForall_UnderPositiveForall_StaysRejected) {
  EXPECT_ANY_THROW(RunSmt2String(
      "(set-option :precision 0.000500000000000000)\n"
      "(assert (forall ((a Real [-1, 1]))\n"
      "          (and (>= a -2)\n"
      "               (not (forall ((b Real [-1, 1])) (<= (* a b) 2))))))\n"
      "(check-sat)\n"));
}

}  // namespace
}  // namespace dreal
