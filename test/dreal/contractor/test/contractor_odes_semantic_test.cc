// Semantic correctness tests for the ODE contractor (Codac CtcLohner backend).
//
// The companion file `contractor_capd_test.cc` checks mechanical invariants
// (input/output bit masks, non-empty box) but never verifies that the
// narrowed intervals match the system's true reachable set. These tests
// close that gap by exercising `mk_contractor_ode_lohner` on systems with
// known closed-form solutions.
//
// Two gate TYPES per system. Label each by the property it protects, using
// model-theory notation (see docs/soundness-vs-completeness.md for the T-relation
// definitions and CLAUDE.md's terminology-discipline mandate):
//   - [SOUNDNESS GATE], SAT direction: on a SAT-known (X_0, X_t, t) instance the
//     box stays non-empty after Prune(). Emptying it would be a false `unsat`
//     — SOUNDNESS (asserts the integral T-unsatisfiable when it is
//     T-satisfiable, ∃M⊨T. M⊨φ): the contractor over-pruned a real solution.
//   - [COMPLETENESS GATE], UNSAT direction: on a robustly-UNSAT instance (gap ≫
//     δ) whose ODE enclosure is provably disjoint from the box gate, Prune()
//     MUST empty the box. CAPD enclosures are outward over-approximations, so a
//     disjoint enclosure proves true infeasibility and emptying is sound and
//     required. Failing to empty is a missed refutation — COMPLETENESS (asserts
//     φ^δ T-satisfiable when φ^δ is T-unsatisfiable → false `delta-sat`): the
//     always-VALID OdeFormulaEvaluator then rubber-stamps the un-refuted box.
//     This is BUG-006. See DecayFlowTest.*Infeasible*.
//
// The asymmetry is the whole point: a too-loose enclosure can only ever weaken
// the COMPLETENESS gate — never the SOUNDNESS gate — because widening an
// over-approximation never removes a real solution. (Failing to empty when the
// enclosure merely *overlaps* the gate within the over-approximation, gap ≤ δ,
// is acceptable delta-sat slack, not a bug.)
//
// The contractor empties the box on a disjoint enclosure directly
// (`contractor_ode_lohner::Prune`'s refute_or_narrow), for non-trivial flows
// as well as the trivial-flow short-circuit. Infeasibility is NOT deferred to
// the ICP/SAT layer.

#include "dreal/contractor/odes/contractor_odes.h"

#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/util/rounding.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"

namespace dreal {
namespace {

using std::make_shared;
using std::vector;

// =============================================================================
// Fixture 1: trivial flow dx/dt = 0.
// State variable does not change; integral constraint reduces to X_0 ∩ X_t.
// Exercises the trivial-flow short-circuit in contractor_odes.cc.
// =============================================================================

// Variables and OdeFlow are `inline static` — one instance per fixture class,
// shared across every test in the class. This was originally a workaround
// for a cache-key bug in `make_codac_ode_cache` (raw `OdeFlow*` keys,
// vulnerable to pointer reuse after destruction). That bug is now fixed —
// the cache holds a shared_ptr to the OdeFlow alongside the cache slot —
// but the inline-static pattern is kept because it's tidier and avoids
// unnecessary cache-map growth in the test binary.

class TrivialFlowTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"trivial_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"trivial_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"trivial_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"trivial_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "trivial",
      vector<std::pair<Variable, Expression>>{{x_, Expression{0.0}}});

  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }

  void SetBounds(double x_lb, double x_ub, double xt_lb, double xt_ub,
                 double t_ub) {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(x_lb, x_ub);
    box_[xt_] = Box::Interval(xt_lb, xt_ub);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// Ground truth: UNSAT (disjoint X_0 = [0,1] and X_t = [2,3]; x is constant).
TEST_F(TrivialFlowTest, FwdInfeasible_BoxEmpties) {
  SetBounds(0.0, 1.0, 2.0, 3.0, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "trivial flow, disjoint X_0 = [0,1] / X_t = [2,3] (FWD) should empty";
}

TEST_F(TrivialFlowTest, BwdInfeasible_BoxEmpties) {
  SetBounds(0.0, 1.0, 2.0, 3.0, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "trivial flow, disjoint X_0 / X_t (BWD) should empty via short-circuit";
}

// =============================================================================
// Fixture 2: linear decay dx/dt = -x.
// Closed form: x(t) = x_0 * exp(-t).
// =============================================================================

class DecayFlowTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"decay_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"decay_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"decay_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"decay_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "decay", vector<std::pair<Variable, Expression>>{{x_, -x_}});

  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }

  void SetBounds(double x0_lb, double x0_ub, double xt_lb, double xt_ub,
                 double t_ub) {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(x0_lb, x0_ub);
    box_[xt_] = Box::Interval(xt_lb, xt_ub);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// Ground truth: SAT. X_0 = [1,2], X_t = [0.3,0.8], t = 1.
// Closed form: x(1) ∈ [e^-1, 2e^-1] ≈ [0.368, 0.736] ⊂ [0.3, 0.8].
TEST_F(DecayFlowTest, FwdFeasible_BoxRemains) {
  SetBounds(1.0, 2.0, 0.3, 0.8, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty())
      << "decay feasible (FWD): box should remain non-empty [SOUNDNESS GATE]";
  // xt is narrowed by FWD to its overapprox of x(1); the closed-form
  // interval [0.368, 0.736] must remain contained in the narrowed xt.
  EXPECT_LE(cs.box()[xt_].lb(), std::exp(-1.0))
      << "FWD must not over-prune x_t below 1*e^-1";
  EXPECT_GE(cs.box()[xt_].ub(), 2.0 * std::exp(-1.0))
      << "FWD must not over-prune x_t above 2*e^-1";
}

// Ground truth: SAT. Same instance as FwdFeasible.
// On HEAD (BWD-skip): contractor is a no-op; box trivially non-empty.
// Under experiment: BWD narrows x_0 toward backward image. The backward
// image of [0.3, 0.8] at t=0 for dx/dt = -x is [0.3*e, 0.8*e] ≈
// [0.815, 2.174]. Intersected with X_0 = [1, 2] this gives [1, 2] (no
// narrowing). Soundness: x_0.ub() must remain ≥ 2 — any tighter would drop
// valid initial states.
TEST_F(DecayFlowTest, BwdFeasible_BoxRemains) {
  SetBounds(1.0, 2.0, 0.3, 0.8, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty())
      << "decay feasible (BWD): box should remain non-empty [SOUNDNESS GATE]";
  EXPECT_LE(cs.box()[x0_].lb(), 1.0)
      << "BWD must not over-prune x_0 above 1 (drops the true point x_0=1)";
  EXPECT_GE(cs.box()[x0_].ub(), 2.0)
      << "BWD must not over-prune x_0 below 2 (drops the true point x_0=2)";
}

// Ground truth: UNSAT (the bug006 / dreal-bugs.md regression, non-trivial flow).
// X_0 = [1,2], t ∈ [0,1]. The feasible terminal set is the trajectory TUBE
// {x0·e^-t : x0∈[1,2], t∈[0,1]} = [1·e^-1, 2·e^0] = [0.368, 2] (max at x0=2,
// t=0). X_t = [2.5,3] lies entirely above the tube, so NO (x0,t) satisfies the
// integral → infeasible; the contractor MUST empty the box. CAPD enclosures
// are outward over-approximations, so a hull disjoint from X_t proves true
// infeasibility — set_empty() here is sound (no false-UNSAT risk).
//
// (X_t must clear the whole tube, not just the t=1 endpoint x(1)∈[0.368,0.736]:
// X_t=[2,3] would touch the tube at x0=2,t=0 and is genuinely SAT. Refuting on
// the endpoint alone — the pre-fix endpoint-only contractor's latent behavior —
// would false-unsat such free-time instances; the tube hull is what makes the
// refutation sound. See run_capd_fwd's header.)
//
// This is a real completeness/refutation gate, not the "informational value gate" the file
// header used to describe: before the fix the contractor silently dropped the
// empty intersection (no set_empty), leaving the box non-empty for the
// always-VALID OdeFormulaEvaluator to rubber-stamp as delta-sat — BUG-006.
TEST_F(DecayFlowTest, FwdInfeasible_BoxEmpties) {
  SetBounds(1.0, 2.0, 2.5, 3.0, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "decay infeasible (FWD): trajectory tube [0.368,2] over t∈[0,1] is "
         "disjoint from X_t=[2.5,3]; box must empty [COMPLETENESS GATE, BUG-006]";
}

// Ground truth: UNSAT, same instance. The BWD backward image of X_t=[2.5,3]
// over τ∈[0,1] (reverse-time x grows) is the tube {xt·e^τ : xt∈[2.5,3],
// τ∈[0,1]} ⊆ [2.5, 3e] ≈ [2.5, 8.15], disjoint from X_0=[1,2]; the contractor
// must empty the box.
TEST_F(DecayFlowTest, BwdInfeasible_BoxEmpties) {
  SetBounds(1.0, 2.0, 2.5, 3.0, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "decay infeasible (BWD): backward tube [2.5,8.15] over τ∈[0,1] is "
         "disjoint from X_0=[1,2]; box must empty [COMPLETENESS GATE, BUG-006]";
}

// =============================================================================
// Fixture 3: coupled rational dynamics — mock-prostate.
// dx/dt = -x * (z / (z + 2)),  dz/dt = -z.
// z is decoupled exponential decay: z(t) = z_0 * exp(-t).
// x decreases monotonically while z > 0; no closed form for x but x(t) ≤ x_0.
// This is the load-bearing fixture — rational coupling is where prostate's
// suspected unsoundness might live.
// =============================================================================

class MockProstateTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"prostate_x", Variable::Type::CONTINUOUS};
  inline static const Variable z_{"prostate_z", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"prostate_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable z0_{"prostate_z_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"prostate_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable zt_{"prostate_z_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"prostate_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, z_, x0_, z0_, xt_, zt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "mock_prostate",
      vector<std::pair<Variable, Expression>>{
          {x_, -x_ * (z_ / (z_ + 2.0))},
          {z_, -z_},
      });

  Formula MakeIc() const {
    return integral(0.0, t0_, {x0_, z0_}, {xt_, zt_}, ode_);
  }

  void SetBounds(double x0_lb, double x0_ub, double z0_lb, double z0_ub,
                 double xt_lb, double xt_ub, double zt_lb, double zt_ub,
                 double t_ub) {
    box_[x_] = Box::Interval(0.01, 100.0);  // x > 0 to avoid singularity issues
    box_[z_] = Box::Interval(0.01, 100.0);
    box_[x0_] = Box::Interval(x0_lb, x0_ub);
    box_[z0_] = Box::Interval(z0_lb, z0_ub);
    box_[xt_] = Box::Interval(xt_lb, xt_ub);
    box_[zt_] = Box::Interval(zt_lb, zt_ub);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// Ground truth: SAT.
// X_0 = x∈[5,10], z∈[1,2]; X_t = x∈[0.5,10], z∈[0.2,0.9]; t = 1.
// z(1) ∈ [e^-1, 2e^-1] ≈ [0.368, 0.736] ⊂ [0.2, 0.9].
// x(1) is monotonically below x_0 (decay rate z/(z+2) ∈ [0, 0.5]); x(1) ⊂ [0.5, 10].
TEST_F(MockProstateTest, FwdFeasible_BoxRemains) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.5, 10.0, 0.2, 0.9, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_FALSE(cs.box().empty())
      << "mock-prostate feasible (FWD): box should remain non-empty "
         "[SOUNDNESS GATE for rational coupling]";
}

// Ground truth: SAT. Same instance as FwdFeasible.
TEST_F(MockProstateTest, BwdFeasible_BoxRemains) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.5, 10.0, 0.2, 0.9, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_FALSE(cs.box().empty())
      << "mock-prostate feasible (BWD): box should remain non-empty "
         "[SOUNDNESS GATE for rational coupling, BWD path]";
}

// Soundness probe: after BWD on the feasible instance, x_0 should still
// contain the singleton x_0 = 7.5, z_0 = 1.5 (a clearly-reachable starting
// point — the forward trajectory from (7.5, 1.5) at t=1 lands somewhere in
// (x ≈ 5, z ≈ 0.55), well inside X_t). If BWD prunes 7.5 out of x_0 or 1.5
// out of z_0 under the experiment, that's an unsound under-approximation
// of the backward image.
TEST_F(MockProstateTest, BwdFeasible_PreservesInteriorPoint) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.5, 10.0, 0.2, 0.9, 1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty())
      << "mock-prostate feasible (BWD): box should remain non-empty";
  EXPECT_TRUE(cs.box()[x0_].contains(7.5))
      << "BWD must not prune x_0 = 7.5 (interior point with feasible trajectory)";
  EXPECT_TRUE(cs.box()[z0_].contains(1.5))
      << "BWD must not prune z_0 = 1.5 (interior point with feasible trajectory)";
}

// =============================================================================
// Long-horizon soundness tests.
//
// Post-Codac elimination, CAPD is the sole backend — the previously-needed
// A/B "_Capd"-suffixed variants and gate-dispatch tests have been removed.
// What remains are long-horizon soundness gates: CAPD must keep SAT
// instances non-empty even on t_ub = 20 (decay) and t_ub = 6 (prostate).
// =============================================================================

// Long-horizon decay: x(t) = x_0 * e^-t, t_ub = 20.
// Closed form: x(20) ∈ [e^-20, 2*e^-20] ≈ [2.06e-9, 4.12e-9].
TEST_F(DecayFlowTest, FwdFeasible_LongHorizonSoundness) {
  SetBounds(1.0, 2.0, 0.0, 1.0e-7, 20.0);
  Config config;  // default gates → CAPD triggers (t_ub=20 > 5.0)
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty())
      << "long-horizon decay (FWD, default gates): SAT instance should "
         "remain non-empty [SOUNDNESS GATE for CAPD on t_ub=20]";
  // After narrowing, x_t must still contain the closed-form trajectory.
  EXPECT_LE(cs.box()[xt_].lb(), std::exp(-20.0))
      << "FWD over-pruned x_t below 1*e^-20";
  EXPECT_GE(cs.box()[xt_].ub(), 2.0 * std::exp(-20.0))
      << "FWD over-pruned x_t above 2*e^-20";
}

TEST_F(DecayFlowTest, BwdFeasible_LongHorizonSoundness) {
  SetBounds(1.0, 2.0, 0.0, 1.0e-7, 20.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty())
      << "long-horizon decay (BWD, default gates): SAT instance should remain "
         "non-empty [SOUNDNESS GATE for CAPD backward integration on t_ub=20]";
  EXPECT_LE(cs.box()[x0_].lb(), 1.0)
      << "BWD over-pruned x_0 above 1 (drops true x_0=1)";
  EXPECT_GE(cs.box()[x0_].ub(), 2.0)
      << "BWD over-pruned x_0 below 2 (drops true x_0=2)";
}

// Long-horizon mock-prostate: rational coupling, t_ub = 6 just over the
// default gate. Tests the load-bearing fixture (rational dynamics) on
// the CAPD backward path.
TEST_F(MockProstateTest, FwdFeasible_LongHorizonSoundness) {
  // z(6) ≈ [z0*e^-6, 2*z0*e^-6] ≈ [2.48e-3, 4.96e-3] ⊂ [0, 0.01]
  // x is monotonically decreasing → x(6) ∈ [some pos value, 10]
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.001, 10.0, 0.0, 0.01, 6.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_FALSE(cs.box().empty())
      << "long-horizon mock-prostate (FWD, default gates → CAPD): SAT instance "
         "should remain non-empty [SOUNDNESS GATE]";
}

TEST_F(MockProstateTest, BwdFeasible_LongHorizonSoundness) {
  SetBounds(5.0, 10.0, 1.0, 2.0, 0.001, 10.0, 0.0, 0.01, 6.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_FALSE(cs.box().empty())
      << "long-horizon mock-prostate (BWD, default gates → CAPD): SAT instance "
         "should remain non-empty [SOUNDNESS GATE]";
}

// Trivial-flow short-circuit precedence: the d/dt[x]=0 short-circuit must
// fire BEFORE the CAPD gate dispatch, even when the gate would otherwise
// trigger CAPD. Box should empty via the short-circuit's set_empty(), not
// fall through to CAPD integration of a zero RHS (which works but is wasted
// effort and not the right code path).
TEST_F(TrivialFlowTest, LongHorizon_ShortCircuitFires) {
  // t_ub = 20 would gate CAPD by default; trivial flow must short-circuit.
  SetBounds(0.0, 1.0, 2.0, 3.0, 20.0);
  Config config;  // default gates
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "trivial flow with t_ub=20 must short-circuit (disjoint X_0/X_t) "
         "regardless of CAPD gate threshold";
}

// =============================================================================
// Fixture 4: 6D decoupled decay.
// Each x_i obeys dx_i/dt = -x_i. Closed form: x_i(t) = x_i(0) * e^-t.
// Six dimensions stresses CAPD on a moderate-state-space flow.
// =============================================================================

class SixDimDecayTest : public ::testing::Test {
 protected:
  inline static const Variable x1_{"d6_x1", Variable::Type::CONTINUOUS};
  inline static const Variable x2_{"d6_x2", Variable::Type::CONTINUOUS};
  inline static const Variable x3_{"d6_x3", Variable::Type::CONTINUOUS};
  inline static const Variable x4_{"d6_x4", Variable::Type::CONTINUOUS};
  inline static const Variable x5_{"d6_x5", Variable::Type::CONTINUOUS};
  inline static const Variable x6_{"d6_x6", Variable::Type::CONTINUOUS};

  inline static const Variable x1_0_{"d6_x1_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x2_0_{"d6_x2_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x3_0_{"d6_x3_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x4_0_{"d6_x4_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x5_0_{"d6_x5_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable x6_0_{"d6_x6_0_0", Variable::Type::CONTINUOUS};

  inline static const Variable x1_t_{"d6_x1_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x2_t_{"d6_x2_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x3_t_{"d6_x3_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x4_t_{"d6_x4_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x5_t_{"d6_x5_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable x6_t_{"d6_x6_0_t", Variable::Type::CONTINUOUS};

  inline static const Variable t0_{"d6_time_0", Variable::Type::CONTINUOUS};

  Box box_{vector<Variable>{
      x1_, x2_, x3_, x4_, x5_, x6_,
      x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_,
      x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_,
      t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "decay6",
      vector<std::pair<Variable, Expression>>{
          {x1_, -x1_}, {x2_, -x2_}, {x3_, -x3_},
          {x4_, -x4_}, {x5_, -x5_}, {x6_, -x6_},
      });

  Formula MakeIc() const {
    return integral(0.0, t0_,
                    {x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_},
                    {x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_}, ode_);
  }

  void SetFeasible(double t_ub) {
    // X_0 = [1, 2]^6, X_t = [0, 1]^6 (contains the closed-form x_0 * e^-t for
    // t_ub in [1, 30] easily).
    for (const auto& v : {x1_, x2_, x3_, x4_, x5_, x6_})
      box_[v] = Box::Interval(-100.0, 100.0);
    for (const auto& v : {x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_})
      box_[v] = Box::Interval(1.0, 2.0);
    for (const auto& v : {x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_})
      box_[v] = Box::Interval(0.0, 1.0);
    box_[t0_] = Box::Interval(0.0, t_ub);
  }
};

// 6 state vars at t_ub=1 — exercise CAPD on a moderate-dimensional flow.
TEST_F(SixDimDecayTest, FwdFeasible_HighDim) {
  SetFeasible(1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty())
      << "6D decay (FWD, ndim gate → CAPD): SAT instance should stay non-empty";
  // Each xi_t must still contain its closed-form trajectory [e^-1, 2e^-1].
  for (const auto& vt : {x1_t_, x2_t_, x3_t_, x4_t_, x5_t_, x6_t_}) {
    EXPECT_LE(cs.box()[vt].lb(), std::exp(-1.0))
        << "over-pruned " << vt;
    EXPECT_GE(cs.box()[vt].ub(), 2.0 * std::exp(-1.0))
        << "over-pruned " << vt;
  }
}

TEST_F(SixDimDecayTest, BwdFeasible_HighDim) {
  SetFeasible(1.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::BWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty())
      << "6D decay (BWD, ndim gate → CAPD): SAT instance should stay non-empty";
  // Each xi_0 must still contain {1, 2} (no soundness regression).
  for (const auto& v0 : {x1_0_, x2_0_, x3_0_, x4_0_, x5_0_, x6_0_}) {
    EXPECT_LE(cs.box()[v0].lb(), 1.0) << "BWD over-pruned " << v0;
    EXPECT_GE(cs.box()[v0].ub(), 2.0) << "BWD over-pruned " << v0;
  }
}

// =============================================================================
// Fixture 5: gravity / parabola — dx/dt = v, dv/dt = -1.
// Closed form from (x0, v0): x(t) = x0 + v0*t - 0.5*t^2, v(t) = v0 - t.
// Non-monotonic x: with x0=0, v0=1 it rises to a peak x=0.5 at t=1, then falls
// back to x=0 at t=2. This is the fixture for ForallT invariants that hold at
// the endpoints but are violated in the trajectory *interior* (F1) — the gap
// the rewrite opened by checking the invariant only at the pre-integration box
// instead of along the CAPD tube (cav26's per-slice check_invariant).
// =============================================================================

class GravityInvariantTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"grav_x", Variable::Type::CONTINUOUS};
  inline static const Variable v_{"grav_v", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"grav_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable v0_{"grav_v_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"grav_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable vt_{"grav_v_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"grav_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, v_, x0_, v0_, xt_, vt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "gravity",
      vector<std::pair<Variable, Expression>>{{x_, v_}, {v_, Expression{-1.0}}});

  Formula MakeIc() const {
    return integral(0.0, t0_, {x0_, v0_}, {xt_, vt_}, ode_);
  }

  // x0=0, v0=1; terminal pinned at t=2 (so the trajectory MUST traverse the
  // interior peak at t=1); X_t brackets the true endpoint x(2)=0, v(2)=-1.
  void SetBounds() {
    box_[x_] = Box::Interval(-10.0, 10.0);
    box_[v_] = Box::Interval(-10.0, 10.0);
    box_[x0_] = Box::Interval(0.0, 0.0);
    box_[v0_] = Box::Interval(1.0, 1.0);
    box_[xt_] = Box::Interval(-0.2, 0.2);
    box_[vt_] = Box::Interval(-1.2, -0.8);
    box_[t0_] = Box::Interval(2.0, 2.0);
  }
};

// Ground truth WITHOUT the invariant: SAT (x(2)=0 ∈ X_t). WITH the invariant
// `∀t. x ≤ 0.3`: UNSAT — the interior peak x(1)=0.5 > 0.3 violates it, and the
// terminal is pinned at t=2 so the trajectory cannot avoid the peak.
//
// The invariant holds at BOTH endpoints (x(0)=0, x(2)=0 ≤ 0.3) and on the X_t
// box (xt ∈ [-0.2,0.2] ≤ 0.3), so the rewrite's pre-integration-box-only check
// passes and the box is wrongly left non-empty (false delta-sat). cav26 checked
// the invariant on every tube slice and refuted. [F1 COMPLETENESS GATE]
TEST_F(GravityInvariantTest, FwdInteriorInvariantViolation_BoxEmpties) {
  SetBounds();
  Config config;
  // Pin hull-grid to 16 (high resolution) so this test guards the per-slice
  // MECHANISM independently of the default-tube precision: an interior-only
  // violation IS refuted when given ample time-resolution. The companion test
  // FwdInteriorInvariantViolation_DefaultHull_BoxEmpties asserts the SHIPPED
  // default also refutes it — true since the 2026-06 centered-in-time tube fix
  // (HULL_COMPLETENESS.md "Resolution") tightened the default tube to CAPD
  // precision. A coarser hull-grid only ever widens (sound; never a false-unsat),
  // so this pin can never mask a soundness hole.
  config.mutable_ode_hull_grid().set_from_command_line(16);
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const Formula inv = forallT(ode_, 0.0, t0_, xt_ <= 0.3);
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {inv}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "gravity, ∀t. x≤0.3 violated only at the interior peak x(1)=0.5 while "
         "holding at both endpoints; box must empty [F1 interior-refutation "
         "mechanism, at pinned resolution]";
}

// Same interior violation, but at the SHIPPED DEFAULT hull-grid (no pin). This is
// the completeness gate for the centered-in-time tube fix (HULL_COMPLETENESS.md):
// with the naive curve(sub) range the default tube is ~4x too loose and MISSES the
// peak (delta-sat); the mean-value range tightens it enough that the default
// detects the violation. A margin of 0.2 (peak 0.5 vs bound 0.3) >> the 1e-3
// precision must never be missed regardless of the speed knob.
TEST_F(GravityInvariantTest, FwdInteriorInvariantViolation_DefaultHull_BoxEmpties) {
  SetBounds();
  Config config;  // shipped default hull-grid — no pin
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const Formula inv = forallT(ode_, 0.0, t0_, xt_ <= 0.3);
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {inv}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "gravity, ∀t. x≤0.3 violated at interior peak x(1)=0.5 (margin 0.2 ≫ "
         "precision); box must empty at the DEFAULT hull-grid [F1 completeness "
         "gate — centered-in-time tube]";
}

// Control: same instance WITHOUT the invariant is genuinely SAT — the fix must
// not over-prune it to empty (no false UNSAT from the per-slice machinery).
TEST_F(GravityInvariantTest, FwdNoInvariant_BoxRemains) {
  SetBounds();
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_FALSE(cs.box().empty())
      << "gravity without invariant: x(2)=0 ∈ X_t, genuinely SAT [SOUNDNESS]";
}

// =============================================================================
// Fixture 6: anti-correlated 2-D — dx/dt = -x, dy/dt = y.
// x(t) = x0*e^-t decays; y(t) = y0*e^t grows. With x0=2, y0=e^-2 the trajectory
// runs from (2, 0.135) to (0.27, 1) over t∈[0,2]. The component-wise hull is
// x∈[0.27,2], y∈[0.135,1], but the two extremes never co-occur at one time.
// This is the fixture for F2: a coarse component-wise hull overlaps a gate that
// no single trajectory time reaches, so hull-then-intersect fails to refute
// while cav26's per-slice intersect-drop-hull refutes.
// =============================================================================

class AntiCorrelatedTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"ac_x", Variable::Type::CONTINUOUS};
  inline static const Variable y_{"ac_y", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"ac_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable y0_{"ac_y_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"ac_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable yt_{"ac_y_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"ac_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, y_, x0_, y0_, xt_, yt_, t0_}};

  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "anticorr",
      vector<std::pair<Variable, Expression>>{{x_, -x_}, {y_, y_}});

  Formula MakeIc() const {
    return integral(0.0, t0_, {x0_, y0_}, {xt_, yt_}, ode_);
  }
};

// Ground truth: UNSAT. X_t requires x∈[1.5,2] AND y∈[0.8,1.2]. x∈[1.5,2] only
// early (t≲0.29, where y≲0.18 ≪ 0.8); y∈[0.8,1.2] only late (t≳1.79, where
// x≲0.33 ≪ 1.5). No single time satisfies both, so the integral is infeasible
// (gap ≫ δ). The component-wise hull x∈[0.27,2], y∈[0.135,1] overlaps BOTH gate
// intervals, so the rewrite's hull-then-intersect leaves the box non-empty
// (false delta-sat). Per-slice filtering refutes. [F2 COMPLETENESS GATE]
TEST_F(AntiCorrelatedTest, FwdAntiCorrelated_BoxEmpties) {
  box_[x_] = Box::Interval(0.0, 10.0);
  box_[y_] = Box::Interval(0.0, 10.0);
  box_[x0_] = Box::Interval(2.0, 2.0);
  box_[y0_] = Box::Interval(std::exp(-2.0), std::exp(-2.0));
  box_[xt_] = Box::Interval(1.5, 2.0);
  box_[yt_] = Box::Interval(0.8, 1.2);
  box_[t0_] = Box::Interval(0.0, 2.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "anti-correlated tube reaches x∈[1.5,2] and y∈[0.8,1.2] at disjoint "
         "times; no single time hits the gate, box must empty [F2 COMPLETENESS GATE]";
}

// =============================================================================
// F3 + F4 on the existing DecayFlowTest fixture (dx/dt = -x).
// =============================================================================

// F3 — time narrowing. X_t=[0.4,0.5] is reachable by x(t)=x0*e^-t only for
// t∈[ln2, ln(2/0.4)] ≈ [0.69, 1.61] (x0∈[1,2]); t outside that window cannot
// land in X_t. The contractor should narrow the time variable from its input
// [0,3] toward that feasible window. The rewrite never contracts time (F3);
// cav26 narrowed T to the surviving slices' time-hull.
TEST_F(DecayFlowTest, FwdFeasible_TimeNarrows) {
  SetBounds(1.0, 2.0, 0.4, 0.5, 3.0);
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty()) << "decay into X_t=[0.4,0.5] is SAT";
  EXPECT_LT(cs.box()[t0_].ub(), 2.5)
      << "time should narrow below 3.0 toward the feasible window ~[0.69,1.61] "
         "(no t>~1.61 lands x in [0.4,0.5]) [F3]";
}

// F4 — constant (non-variable) integration time. Same disjoint geometry as the
// BUG-006 DecayFlowTest.FwdInfeasible case, but the duration is the literal 1.0
// rather than a time variable. x(1)∈[e^-1,2e^-1]≈[0.368,0.736] is disjoint from
// X_t=[2,3] → UNSAT. The rewrite bails out on non-variable time
// (`if (!is_variable(icct)) return;`), enforcing nothing (latent false
// delta-sat); cav26 integrated constant durations. [F4 COMPLETENESS GATE]
TEST_F(DecayFlowTest, FwdConstantTime_Infeasible_BoxEmpties) {
  box_[x_] = Box::Interval(-100.0, 100.0);
  box_[x0_] = Box::Interval(1.0, 2.0);
  box_[xt_] = Box::Interval(2.0, 3.0);
  box_[t0_] = Box::Interval(0.0, 5.0);  // unused: time is the constant below
  Config config;
  ContractorStatus cs{box_};
  const Formula ic = integral(0.0, Expression{1.0}, {x0_}, {xt_}, ode_);
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "constant duration t=1: x(1)≈[0.368,0.736] disjoint from X_t=[2,3]; "
         "box must empty [F4 COMPLETENESS GATE]";
}

// =============================================================================
// Fixture 8: pinned terminal time — the endpoint must contract to the
// trajectory value AT t_ub, not to the fat tube over the last sub-grid slice.
//
// Regression for BUG-005 (witness mis-print) and BUG-008 (false delta-sat).
// integrate_tube_slices sub-grids each CAPD step's full domain [0,step] and
// sets each slice's enclosure to curve(sub) = the trajectory TUBE over that
// time sub-interval. For a pinned time (win_lb == win_ub == t_ub) the only
// window-overlapping slice is [t_ub - step/16, t_ub], whose tube fattens the
// endpoint by ~step/16·|dx/dt|. Intersecting X_t with that tube binds the
// endpoint variable to an interior-time value (BUG-005) and lets a sub-true
// gate survive (BUG-008). The gate must use the trajectory clipped to the
// time window, which for a pinned time collapses to the point x(t_ub).
// =============================================================================

// BUG-005: dx/dt = -0.5 x, x0=100, time pinned to [1,1]. True x(1)=100·e^-0.5
// ≈ 60.6531. With a wide X_t gate the FWD contractor must narrow X_t to a tight
// band around 60.6531 — NOT [60.6531, ~61.6] (the fat last-slice tube).
class PinnedDecayTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"pd_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"pd_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"pd_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"pd_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};
  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "pinned_decay",
      vector<std::pair<Variable, Expression>>{{x_, -0.5 * x_}});
  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }
};

TEST_F(PinnedDecayTest, FwdPinnedEndpoint_NarrowsToTrueValue) {
  box_[x_] = Box::Interval(-200.0, 200.0);
  box_[x0_] = Box::Interval(100.0, 100.0);
  box_[xt_] = Box::Interval(-200.0, 200.0);  // wide gate
  box_[t0_] = Box::Interval(1.0, 1.0);       // PINNED terminal time
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  ASSERT_FALSE(cs.box().empty()) << "pinned decay endpoint is reachable: SAT";
  const double true_xt = 100.0 * std::exp(-0.5);  // ≈ 60.6531
  const ibex::Interval xt = cs.box()[xt_];
  // The whole narrowed band must sit tightly around x(1) — NOT fattened to the
  // last-slice tube [60.65, ~61.6] (the bug). Tolerance bounds (true_xt ± 0.05)
  // avoid ULP-level sensitivity of the libm reference to the ambient FPU
  // rounding mode left by a prior test. The bug's ub (~61.6) and width (~3.25)
  // both blow past 0.05; the fix's band is ~1e-9 wide centered on x(1).
  EXPECT_GT(xt.lb(), true_xt - 0.05) << "narrowed X_t must sit near x(1)≈60.6531";
  EXPECT_LT(xt.ub(), true_xt + 0.05)
      << "pinned endpoint must contract to x(1)≈60.6531, not the fat last-slice "
         "tube reaching ~61.6 [BUG-005 GATE]";
  EXPECT_LT(xt.ub() - xt.lb(), 0.05)
      << "pinned endpoint band must be tight (a single value), not the fat "
         "last-slice tube [BUG-005 GATE]";
}

// BUG-008: rising flow dx/dt = 0.5(100-x), x0=20, time pinned [1,1]. True
// endpoint x(1)=100-80·e^-0.5 ≈ 51.4775. X_t gate [0,51.0] sits below the true
// value by 0.477 ≫ delta, so the only sound verdict is refutation (box empties).
// The bug's fat last-slice tube reaches down to ~50.9 and wrongly survives the
// gate (false delta-sat).
class PinnedRiseTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"pr_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"pr_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"pr_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"pr_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};
  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "pinned_rise",
      vector<std::pair<Variable, Expression>>{{x_, 0.5 * (100.0 - x_)}});
  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }
};

TEST_F(PinnedRiseTest, FwdPinnedSubTrueGate_BoxEmpties) {
  box_[x_] = Box::Interval(-200.0, 200.0);
  box_[x0_] = Box::Interval(20.0, 20.0);
  box_[xt_] = Box::Interval(0.0, 51.0);  // sub-true gate (true endpoint ≈51.4775)
  box_[t0_] = Box::Interval(1.0, 1.0);   // PINNED terminal time
  Config config;
  ContractorStatus cs{box_};
  const auto ic = MakeIc();
  const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, ode_direction::FWD,
                                            config, 0.0);
  { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
  EXPECT_TRUE(cs.box().empty())
      << "pinned rise: x(1)≈51.4775 > gate ub 51.0 by 0.477 ≫ delta; the "
         "endpoint clipped to t=1 is disjoint from X_t → box must empty "
         "[BUG-008 COMPLETENESS GATE]";
}

// =============================================================================
// BUG-018: interval initial conditions with the gate near the tube's edge.
// Every instance below is SAT, with a witness trajectory that ends inside the
// X_t gate. Before the CAPD bump the per-slice tube's mean-value term used
// CAPD's Curve::timeDerivative, which scaled the initial-condition spread by
// f(X)-f(c) instead of X-c: the spread shrank by |Df| (0.1 here) or vanished
// (the projectile's v' = -1), the tube missed the extreme trajectories, and
// Prune emptied a SAT box — SOUNDNESS (asserts φ T-unsatisfiable on a
// T-satisfiable φ — false unsat). Point-IC fixtures above cannot see this: with
// X = c the spread is zero either way.
// Each check runs FWD and BWD at hull-grid 1 and 4 for every C0 set type.
// =============================================================================

class IntervalIcSpreadTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"spread_x", Variable::Type::CONTINUOUS};
  inline static const Variable v_{"spread_v", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"spread_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable v0_{"spread_v_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"spread_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable vt_{"spread_v_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"spread_time_0", Variable::Type::CONTINUOUS};
  inline static const std::shared_ptr<const OdeFlow> growth_ = make_shared<OdeFlow>(
      "spread_growth", vector<std::pair<Variable, Expression>>{{x_, 0.1 * x_}});
  inline static const std::shared_ptr<const OdeFlow> decay_ = make_shared<OdeFlow>(
      "spread_decay", vector<std::pair<Variable, Expression>>{{x_, -0.1 * x_}});
  inline static const std::shared_ptr<const OdeFlow> projectile_ =
      make_shared<OdeFlow>("spread_projectile",
                           vector<std::pair<Variable, Expression>>{
                               {x_, v_}, {v_, Expression{-1.0}}});

  // One Prune per (direction, hull-grid, C0 set); each must keep the box
  // non-empty [SOUNDNESS GATE] and keep every witness value.
  static void ExpectPruneKeepsWitness(
      const Box& box, const Formula& ic,
      const vector<std::pair<Variable, double>>& witness) {
    for (const ode_direction dir : {ode_direction::FWD, ode_direction::BWD}) {
      for (const int hull_grid : {1, 4}) {
        for (const OdeC0SetType c0 : {OdeC0SetType::Rect2, OdeC0SetType::Tripleton,
                                      OdeC0SetType::HORect2}) {
          SCOPED_TRACE(::testing::Message()
                       << (dir == ode_direction::FWD ? "FWD" : "BWD")
                       << " hull-grid " << hull_grid << " c0-set "
                       << static_cast<int>(c0));
          Config config;
          config.mutable_ode_hull_grid().set_from_command_line(hull_grid);
          config.mutable_ode_c0_set().set_from_command_line(c0);
          ContractorStatus cs{box};
          const auto ctc = mk_contractor_ode_lohner(box, {ic, {}}, dir, config, 0.0);
          { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
          if (cs.box().empty()) {
            ADD_FAILURE() << "SAT box emptied [SOUNDNESS GATE, BUG-018]";
            continue;
          }
          for (const auto& [var, value] : witness) {
            EXPECT_TRUE(cs.box()[var].contains(value))
                << var << " = " << cs.box()[var] << " lost the witness value "
                << value << " [SOUNDNESS GATE, BUG-018]";
          }
        }
      }
    }
  }
};

// x' = 0.1x, x0 ∈ [1,2], t ∈ [0,1], gate x_t ∈ [2.205, 2.3]. Witness: x0 = 2
// reaches 2.2103 at t ≈ 0.9998 (max 2e^0.1 = 2.21034 at t = 1).
TEST_F(IntervalIcSpreadTest, Growth_KeepsTubeEdge) {
  Box box{vector<Variable>{x_, x0_, xt_, t0_}};
  box[x_] = Box::Interval(-100.0, 100.0);
  box[x0_] = Box::Interval(1.0, 2.0);
  box[xt_] = Box::Interval(2.205, 2.3);
  box[t0_] = Box::Interval(0.0, 1.0);
  ExpectPruneKeepsWitness(box, integral(0.0, t0_, {x0_}, {xt_}, growth_),
                          {{x0_, 2.0}, {xt_, 2.2103}});
}

// x' = -0.1x, x0 ∈ [1,2], t ∈ [0,1], gate x_t ∈ [1.995, 2.1]. Witness: x0 = 2
// reaches 1.998 at t ≈ 0.010.
TEST_F(IntervalIcSpreadTest, Decay_KeepsTubeEdge) {
  Box box{vector<Variable>{x_, x0_, xt_, t0_}};
  box[x_] = Box::Interval(-100.0, 100.0);
  box[x0_] = Box::Interval(1.0, 2.0);
  box[xt_] = Box::Interval(1.995, 2.1);
  box[t0_] = Box::Interval(0.0, 1.0);
  ExpectPruneKeepsWitness(box, integral(0.0, t0_, {x0_}, {xt_}, decay_),
                          {{x0_, 2.0}, {xt_, 1.998}});
}

// x' = v, v' = -1 (state-independent rate: CAPD's old spread for v was exactly
// zero), x0 = 0, v0 ∈ [0.5,1.5], t ∈ [0,0.5], gate x_t ∈ [0.6, 1]. Witness:
// v0 = 1.5 reaches x = 0.62 at t ≈ 0.484 (max 0.625 at t = 0.5).
TEST_F(IntervalIcSpreadTest, Projectile_KeepsTubeEdge) {
  Box box{vector<Variable>{x_, v_, x0_, v0_, xt_, vt_, t0_}};
  box[x_] = Box::Interval(-100.0, 100.0);
  box[v_] = Box::Interval(-100.0, 100.0);
  box[x0_] = Box::Interval(0.0, 0.0);
  box[v0_] = Box::Interval(0.5, 1.5);
  box[xt_] = Box::Interval(0.6, 1.0);
  box[vt_] = Box::Interval(-100.0, 100.0);
  box[t0_] = Box::Interval(0.0, 0.5);
  ExpectPruneKeepsWitness(
      box, integral(0.0, t0_, {x0_, v0_}, {xt_, vt_}, projectile_),
      {{v0_, 1.5}, {xt_, 0.62}});
}

}  // namespace
}  // namespace dreal
