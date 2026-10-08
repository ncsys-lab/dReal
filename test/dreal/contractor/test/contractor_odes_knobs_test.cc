// Knob-consumption tests for the CAPD ODE contractor's runtime tuning flags
// (--ode-taylor-order, --ode-backward-order, --ode-abs-tol, --ode-rel-tol,
// --ode-max-step, --ode-c0-set, --ode-backward). The companion
// `contractor_odes_semantic_test.cc` pins the contractor's *semantics* at the
// default knobs (plus the one existing knob pin, hull-grid=16, in
// GravityInvariantTest); these tests close the remaining zero-coverage gap:
// each knob must (1) actually reach the CAPD solver — a silent mis-wiring
// would make the flag a no-op — and (2) never move a verdict in the unsound
// direction.
//
// Consumption points (read from source, not assumed):
//   - taylor_order: capd::IOdeSolver ctor argument (contractor_odes_capd.cc,
//     integrate_tube_slices_impl / run_capd_trace_impl), resolved
//     direction-specifically in the contractor ctor (contractor_odes.cc:
//     FWD reads config.ode_taylor_order(), BWD reads
//     config.ode_backward_order(); abs/rel tol, hull-grid, c0-set and
//     max-step are direction-shared).
//   - abs_tol / rel_tol / max_step: configure_capd_solver ->
//     setAbsoluteTolerance / setRelativeTolerance / setMaxStep (max_step <= 0
//     means fully adaptive — the cap is not installed at all). Caveat: the
//     installed cap binds only the FIRST integration step — CAPD's
//     TimeMap::moveSet (poincare/TimeMap_template.h) overwrites the solver's
//     maxStep with the remaining horizon around every one-step call, so the
//     mid-integration predictor never sees it; see test (c) for the
//     mechanism and the isolating probes.
//   - c0_set: the integrate_tube_slices / run_capd_trace dispatch over
//     capd::C0Rect2Set / C0TripletonSet / C0HORect2Set.
//   - ode_backward: consumed ONLY in theory_solver.cc (BuildContractor) — it
//     gates whether a BWD lohner contractor is queued beside the FWD one. It
//     is therefore tested through TheorySolver::BuildContractor below, not
//     through mk_contractor_ode_lohner (which takes the direction directly).
//
// Soundness/completeness discipline (docs/soundness-vs-completeness.md):
//   - CAPD's enclosure is an outward over-approximation of the true
//     trajectory set at ANY knob value, so a knob-induced EMPTY box on a
//     T-satisfiable instance is SOUNDNESS (asserts φ T-unsatisfiable on a
//     T-satisfiable φ — false unsat). The retention assertions below gate
//     exactly that.
//   - Coarser knobs only ever WIDEN the enclosure; the cost is COMPLETENESS
//     (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed refutation),
//     which these tests do not gate (the semantic tests' refutation gates run
//     at the shipped defaults).

#include "dreal/contractor/odes/contractor_odes.h"

#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/solver/theory_solver.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/rounding.h"

namespace dreal {
namespace {

using std::make_shared;
using std::vector;

// =============================================================================
// Fixture 1: pinned-endpoint decay dx/dt = -0.5 x, x0 = 100, t pinned [1,1].
// Closed form: x(1) = 100 * e^-0.5 ≈ 60.6531. Same geometry as the semantic
// file's PinnedDecayTest (BUG-005): the pinned time makes the narrowed X_t a
// tight band around x(1), so two knob settings that reach CAPD at all produce
// numerically comparable bands.
// =============================================================================

class KnobsPinnedDecayTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"knpd_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"knpd_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"knpd_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"knpd_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};
  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "knobs_pinned_decay",
      vector<std::pair<Variable, Expression>>{{x_, -0.5 * x_}});
  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }

  void SetBounds() {
    box_[x_] = Box::Interval(-200.0, 200.0);
    box_[x0_] = Box::Interval(100.0, 100.0);
    box_[xt_] = Box::Interval(-200.0, 200.0);  // wide gate
    box_[t0_] = Box::Interval(1.0, 1.0);       // pinned terminal time
  }

  // Build a FWD lohner under `config`, run one Prune, return the result box.
  Box PruneFwd(const Config& config) {
    ContractorStatus cs{box_};
    const auto ic = MakeIc();
    const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}},
                                              ode_direction::FWD, config, 0.0);
    { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
    return cs.box();
  }
};

// (a) actually-changes-behavior: --ode-taylor-order must reach the
// capd::IOdeSolver ctor. Order 2 forces far smaller adaptive steps than the
// default 12 (per-step local error ~ C·h^{order+1} against the same 1e-10
// tolerance), so the two runs take different step sequences and must produce
// different X_t bands. Bit-identical results would mean the flag is a silent
// no-op (the mis-wiring this test guards).
TEST_F(KnobsPinnedDecayTest, TaylorOrder_ProducesDifferentEnclosure) {
  SetBounds();
  const double true_xt = 100.0 * std::exp(-0.5);  // ≈ 60.6531

  Config config_default;  // ode_taylor_order = 12 (kDefaultOdeTaylorOrder)
  const Box b_default = PruneFwd(config_default);

  Config config_low;
  config_low.mutable_ode_taylor_order().set_from_command_line(2);
  const Box b_low = PruneFwd(config_low);

  ASSERT_FALSE(b_default.empty())
      << "pinned decay is SAT at the default order [SOUNDNESS GATE: emptying "
         "asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat]";
  ASSERT_FALSE(b_low.empty())
      << "pinned decay is SAT at order 2 [SOUNDNESS GATE — a knob value must "
         "never falsely empty a box containing the true trajectory]";

  const ibex::Interval& xt_default = b_default[xt_];
  const ibex::Interval& xt_low = b_low[xt_];

  // Retention of the true endpoint at both orders (outward enclosures must
  // keep x(1); the 1e-9 slack absorbs the libm double vs. true-real gap).
  EXPECT_LE(xt_default.lb(), true_xt + 1e-9) << "default order dropped x(1)";
  EXPECT_GE(xt_default.ub(), true_xt - 1e-9) << "default order dropped x(1)";
  EXPECT_LE(xt_low.lb(), true_xt + 1e-9) << "order 2 dropped x(1)";
  EXPECT_GE(xt_low.ub(), true_xt - 1e-9) << "order 2 dropped x(1)";

  // Order 2 must have actually integrated and narrowed the wide gate. If
  // CAPD rejected the order and the integrate_tube_slices catch-all skipped
  // (found=false -> AddInconclusiveOde -> no narrowing), the gate would stay
  // [-200,200] and this catches it — so a "different enclosure" below cannot
  // be faked by an inconclusive skip.
  EXPECT_LT(xt_low.ub(), 199.0)
      << "order 2 did not narrow: CAPD integration did not run (inconclusive "
         "skip) — the knob likely broke the solver instead of tuning it";
  EXPECT_GT(xt_low.lb(), -199.0) << "order 2 did not narrow";

  // The knob-consumption pin: the two bands differ.
  EXPECT_TRUE(xt_low.lb() != xt_default.lb() || xt_low.ub() != xt_default.ub())
      << "order 2 and order 12 produced the bit-identical band ["
      << xt_default.lb() << ", " << xt_default.ub()
      << "] — --ode-taylor-order looks like a silent no-op";
  // Order 2 accumulates per-step remainder over ~10^3-10^4 steps; order 12
  // finishes in a few steps near machine precision, so its band is tighter.
  EXPECT_GT(xt_low.ub() - xt_low.lb(), xt_default.ub() - xt_default.lb())
      << "order-2 band is not wider than order-12";  // INTEGRATION-VERIFY: width ordering confirmed end-to-end 2026-07-22 (order-2 width ~1.5e-9 vs default ~8e-13); re-confirm through this single-Prune harness
}

// (c) --ode-max-step: the cap must reach setMaxStep (configure_capd_solver
// installs it only for max_step > 0) and change the executed step sequence.
// Mechanism (TimeMap_template.h moveSet + StepControl.h init, read not
// assumed): the sole consumer that survives moveSet's remaining-horizon
// overwrite is the first-step selection — ILastTermsStepControl::init runs
// at initComputations time, before the overwrite, and picks h = min(1/L, 1,
// max_step) (L = 0.5 here, so the uncapped init pick is 1.0). Cap 0.05
// therefore forces a 0.05 first step plus follow-up — a different step
// sequence and band than the adaptive run.
// Isolating probes (gcc_build/dreal4 f51f78b8e, 2026-07-22, this geometry):
// cap 0.05 moved X_t.ub (60.65306597126339 vs 60.65306597126406 adaptive);
// cap 1.2 — above the 1.0 first step, below the ~1.55 predictor steps — was
// bit-identical to adaptive at a t=4 horizon, isolating first-step-only
// binding. The flag's "cap" help text overstating its reach is an
// owner-level semantics gap; this test pins consumption only.
TEST_F(KnobsPinnedDecayTest, MaxStepCap_ProducesDifferentEnclosure) {
  SetBounds();
  const double true_xt = 100.0 * std::exp(-0.5);

  Config config_adaptive;  // ode_max_step = 0.0 => no cap installed
  const Box b_adaptive = PruneFwd(config_adaptive);

  Config config_capped;
  config_capped.mutable_ode_max_step().set_from_command_line(0.05);
  const Box b_capped = PruneFwd(config_capped);

  ASSERT_FALSE(b_adaptive.empty())
      << "pinned decay is SAT (adaptive steps) [SOUNDNESS GATE]";
  ASSERT_FALSE(b_capped.empty())
      << "pinned decay is SAT (capped steps) [SOUNDNESS GATE: a step cap must "
         "never falsely empty a box containing the true trajectory]";

  const ibex::Interval& xt_adaptive = b_adaptive[xt_];
  const ibex::Interval& xt_capped = b_capped[xt_];

  EXPECT_LE(xt_adaptive.lb(), true_xt + 1e-9) << "adaptive run dropped x(1)";
  EXPECT_GE(xt_adaptive.ub(), true_xt - 1e-9) << "adaptive run dropped x(1)";
  EXPECT_LE(xt_capped.lb(), true_xt + 1e-9) << "capped run dropped x(1)";
  EXPECT_GE(xt_capped.ub(), true_xt - 1e-9) << "capped run dropped x(1)";

  // The capped run must have integrated (not an inconclusive skip) …
  EXPECT_LT(xt_capped.ub(), 199.0) << "capped run did not narrow";
  EXPECT_GT(xt_capped.lb(), -199.0) << "capped run did not narrow";
  // … and taken a different step sequence than the adaptive run.
  EXPECT_TRUE(xt_capped.lb() != xt_adaptive.lb() ||
              xt_capped.ub() != xt_adaptive.ub())
      << "max-step 0.05 and adaptive produced the bit-identical band ["
      << xt_adaptive.lb() << ", " << xt_adaptive.ub()
      << "] — --ode-max-step looks like a silent no-op";  // INTEGRATION-VERIFY: difference confirmed end-to-end 2026-07-22 (ub 60.65306597126339 vs ...406); re-confirm through this single-Prune harness
}

// (b') actually-changes-behavior: --ode-abs-tol / --ode-rel-tol must reach
// setAbsoluteTolerance / setRelativeTolerance (configure_capd_solver). The
// (b) sweeps in Fixture 2 gate soundness only — their retention assertions
// hold even on an untouched gate — so this test is the tolerances' one
// consumption pin (without it, deleting both setter calls would fail
// nothing). Step mechanics (CAPD StepControl.h + poincare/TimeMap, read not
// assumed): the FIRST step is tolerance-independent (ILastTermsStepControl::
// init picks a Lipschitz-based h = min(1/L, 1) = 1.0 here); each later step
// is predicted as h ≈ (eps / ||c_order||)^(1/order) with
// eps = max(abs_tol, rel_tol·||c_0||) (getEffectiveTolerance), clamped to
// the remaining horizon (TimeMap::moveSet). The horizon-1 SetBounds
// instance is finished within the tolerance-independent opening step at
// every tolerance — bit-identical bands, an unpinnable instance (probed
// end-to-end 2026-07-22: default, abs 1e-2, and rel 1e-2 all returned the
// identical X_t band at t=1). At a pinned t = 4 the
// default needs several ~1.55 predictor steps after the opening one (eps
// and ||c_12|| both scale with |x|, so h is scale-invariant along the
// decay), while either loosened tolerance — abs 1e-2 (eps = 1e-2) or rel
// 1e-2 (eps ≈ 1e-2·|x|) — predicts h beyond the remaining horizon and
// finishes early: different step sequences, hence different bands (probed
// 2026-07-22, gcc_build/dreal4 f51f78b8e: default
// [13.53352832365008, 13.53352832386891]; both loosened runs
// [13.53352832287023, 13.53352833468584]).
TEST_F(KnobsPinnedDecayTest, TolLoosening_ProducesDifferentEnclosure) {
  // Horizon-4 variant of SetBounds (see the step-size derivation above).
  box_[x_] = Box::Interval(-200.0, 200.0);
  box_[x0_] = Box::Interval(100.0, 100.0);
  box_[xt_] = Box::Interval(-200.0, 200.0);       // wide gate
  box_[t0_] = Box::Interval(4.0, 4.0);            // pinned terminal time
  const double true_xt = 100.0 * std::exp(-2.0);  // 100·e^(-0.5·4) ≈ 13.5335

  Config config_default;  // ode_abs_tol = ode_rel_tol = 1e-10
  const Box b_default = PruneFwd(config_default);

  Config config_abs;
  config_abs.mutable_ode_abs_tol().set_from_command_line(1e-2);
  const Box b_abs = PruneFwd(config_abs);

  Config config_rel;
  config_rel.mutable_ode_rel_tol().set_from_command_line(1e-2);
  const Box b_rel = PruneFwd(config_rel);

  ASSERT_FALSE(b_default.empty())
      << "pinned decay is SAT at the default tolerances [SOUNDNESS GATE: "
         "emptying asserts φ T-unsatisfiable on a T-satisfiable φ — false "
         "unsat]";
  ASSERT_FALSE(b_abs.empty())
      << "pinned decay is SAT at abs_tol 1e-2 [SOUNDNESS GATE]";
  ASSERT_FALSE(b_rel.empty())
      << "pinned decay is SAT at rel_tol 1e-2 [SOUNDNESS GATE]";

  const ibex::Interval& xt_default = b_default[xt_];
  const ibex::Interval& xt_abs = b_abs[xt_];
  const ibex::Interval& xt_rel = b_rel[xt_];

  for (const ibex::Interval* iv : {&xt_default, &xt_abs, &xt_rel}) {
    // Retention of the true endpoint at every tolerance (the 1e-9 slack
    // absorbs the libm double vs. true-real gap in computing true_xt).
    EXPECT_LE(iv->lb(), true_xt + 1e-9) << "a tolerance run dropped x(4)";
    EXPECT_GE(iv->ub(), true_xt - 1e-9) << "a tolerance run dropped x(4)";
    // … and all three must have integrated — an inconclusive skip leaves
    // the wide gate untouched (see test (a)) and would fake the pins below.
    EXPECT_LT(iv->ub(), 199.0) << "a tolerance run did not narrow";
    EXPECT_GT(iv->lb(), -199.0) << "a tolerance run did not narrow";
  }

  // The knob-consumption pins: each loosened band differs from the default.
  EXPECT_TRUE(xt_abs.lb() != xt_default.lb() || xt_abs.ub() != xt_default.ub())
      << "abs_tol 1e-2 and 1e-10 produced the bit-identical band ["
      << xt_default.lb() << ", " << xt_default.ub()
      << "] — --ode-abs-tol looks like a silent no-op";  // INTEGRATION-VERIFY: difference confirmed end-to-end 2026-07-22 (bands in the header); re-confirm through this single-Prune harness
  EXPECT_TRUE(xt_rel.lb() != xt_default.lb() || xt_rel.ub() != xt_default.ub())
      << "rel_tol 1e-2 and 1e-10 produced the bit-identical band ["
      << xt_default.lb() << ", " << xt_default.ub()
      << "] — --ode-rel-tol looks like a silent no-op";  // INTEGRATION-VERIFY: difference confirmed end-to-end 2026-07-22 (bands in the header); re-confirm through this single-Prune harness
}

// =============================================================================
// Fixture 2: linear decay dx/dt = -x (the semantic file's DecayFlowTest
// geometry). Closed form x(t) = x0 * e^-t. Used for the SAT-retention knob
// sweeps and the backward-order direction pin.
// =============================================================================

class KnobsDecayTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"kndc_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"kndc_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"kndc_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"kndc_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};
  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "knobs_decay", vector<std::pair<Variable, Expression>>{{x_, -x_}});
  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }

  // SAT instance: X_0=[1,2], X_t=[0.3,0.8], t∈[0,1].
  // x(1) ∈ [e^-1, 2e^-1] ≈ [0.368, 0.736] ⊂ [0.3, 0.8].
  void SetFeasibleBounds() {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(1.0, 2.0);
    box_[xt_] = Box::Interval(0.3, 0.8);
    box_[t0_] = Box::Interval(0.0, 1.0);
  }

  // BWD-narrowing instance: X_t pinned [1,1] at pinned t=1, X_0 wide. The
  // backward image of X_t at t=0 is the point x0 = e ≈ 2.71828; only a BWD
  // contractor writes X_0 (the FWD contractor narrows m_vars_t = X_t and
  // time only — contractor_odes.cc Prune).
  void SetBwdNarrowBounds() {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(0.1, 10.0);
    box_[xt_] = Box::Interval(1.0, 1.0);
    box_[t0_] = Box::Interval(1.0, 1.0);
  }

  Box Prune(const Config& config, const ode_direction dir) {
    ContractorStatus cs{box_};
    const auto ic = MakeIc();
    const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}}, dir, config, 0.0);
    { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
    return cs.box();
  }
};

// (a') Direction-specific order resolution (contractor_odes.cc ctor): a BWD
// lohner reads --ode-backward-order and must IGNORE --ode-taylor-order.
// Three runs on the BWD-narrowing instance:
//   A: all defaults                     -> band around e
//   B: ode_taylor_order=2  (FWD's knob) -> must be bit-identical to A
//   C: ode_backward_order=2             -> must differ from A
// If someone re-wires BWD to read ode_taylor_order, B breaks; if the backward
// order stops reaching CAPD, C breaks.
TEST_F(KnobsDecayTest, BackwardOrder_IsTheBwdKnob_TaylorOrderIgnored) {
  SetBwdNarrowBounds();
  const double kE = std::exp(1.0);

  Config config_a;  // defaults: fwd order 12, bwd order 12
  const Box b_a = Prune(config_a, ode_direction::BWD);

  Config config_b;
  config_b.mutable_ode_taylor_order().set_from_command_line(2);  // FWD knob
  const Box b_b = Prune(config_b, ode_direction::BWD);

  Config config_c;
  config_c.mutable_ode_backward_order().set_from_command_line(2);  // BWD knob
  const Box b_c = Prune(config_c, ode_direction::BWD);

  ASSERT_FALSE(b_a.empty()) << "BWD-narrowing instance is SAT [SOUNDNESS GATE]";
  ASSERT_FALSE(b_b.empty()) << "BWD-narrowing instance is SAT [SOUNDNESS GATE]";
  ASSERT_FALSE(b_c.empty()) << "BWD-narrowing instance is SAT [SOUNDNESS GATE]";

  const ibex::Interval& xa = b_a[x0_];
  const ibex::Interval& xb = b_b[x0_];
  const ibex::Interval& xc = b_c[x0_];

  // All three must narrow X_0=[0.1,10] to a tight band retaining the true
  // preimage x0 = e (±0.05 mirrors the BUG-005 gate's libm-ULP slack).
  for (const ibex::Interval* iv : {&xa, &xb, &xc}) {
    EXPECT_GT(iv->lb(), kE - 0.05) << "BWD band must sit near x0 = e";
    EXPECT_LT(iv->ub(), kE + 0.05) << "BWD band must sit near x0 = e";
    EXPECT_LE(iv->lb(), kE + 1e-9) << "BWD band must retain x0 = e";
    EXPECT_GE(iv->ub(), kE - 1e-9) << "BWD band must retain x0 = e";
  }

  // B == A: the FWD order knob must be inert on a BWD instance. The CAPD run
  // is deterministic (fresh IOdeSolver/ITimeMap per call, cached IMap has no
  // parameters to re-bind), so identical params give identical bounds.
  EXPECT_EQ(xb.lb(), xa.lb())
      << "--ode-taylor-order leaked into the BWD direction";  // INTEGRATION-VERIFY: determinism of repeated CAPD runs on this thread
  EXPECT_EQ(xb.ub(), xa.ub())
      << "--ode-taylor-order leaked into the BWD direction";

  // C != A: the backward order knob must reach the BWD solver.
  EXPECT_TRUE(xc.lb() != xa.lb() || xc.ub() != xa.ub())
      << "--ode-backward-order 2 produced the bit-identical band ["
      << xa.lb() << ", " << xa.ub()
      << "] — the knob looks like a silent no-op";  // INTEGRATION-VERIFY: bit-level difference expected from order-2 vs order-12 step sequences (same coincidence caveat as the (a)/(c) pins)
}

// BUG-013 (docs/dreal-bugs.md, LIVE): X_0 = [1,2], t ∈ [0,1], gate X_t =
// [0.3, 0.35], which lies below the tube minimum e^-1 ≈ 0.368 by 0.018. At
// order 16/20 or tolerances ≥ 1e-6 CAPD's first step covers the whole horizon,
// the last sub-slice is [0.75, 1], and the mean-value tube over that slice
// reaches below 0.3 for the interval start, so the gate survives — COMPLETENESS
// (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed refutation). CAPD
// does not fail there: the Prune narrows time to [0.75, 1]. The default knobs
// take a 0.75 + 0.25 step sequence and refute. A monotone-in-time hull closed
// the gap and was dropped (owner, 2026-10-08), so the trigger knobs are pinned
// as the open gap: if one starts refuting, tighten its pin to the refutation.
TEST_F(KnobsDecayTest, Bug013_LongStepTube_RefutesDisjointGate) {
  struct Knob {
    const char* name;
    void (*set)(Config*);
    bool refutes;
  };
  const Knob knobs[] = {
      {"default", [](Config*) {}, true},
      {"order 16", [](Config* c) { c->mutable_ode_taylor_order().set_from_command_line(16); }, false},
      {"order 20", [](Config* c) { c->mutable_ode_taylor_order().set_from_command_line(20); }, false},
      {"abs_tol 1e-6", [](Config* c) { c->mutable_ode_abs_tol().set_from_command_line(1e-6); }, false},
      {"abs_tol 1e-2", [](Config* c) { c->mutable_ode_abs_tol().set_from_command_line(1e-2); }, false},
      {"rel_tol 1e-6", [](Config* c) { c->mutable_ode_rel_tol().set_from_command_line(1e-6); }, false},
      {"rel_tol 1e-2", [](Config* c) { c->mutable_ode_rel_tol().set_from_command_line(1e-2); }, false},
  };
  for (const Knob& knob : knobs) {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(1.0, 2.0);
    box_[xt_] = Box::Interval(0.3, 0.35);
    box_[t0_] = Box::Interval(0.0, 1.0);
    Config config;
    knob.set(&config);
    const Box b = Prune(config, ode_direction::FWD);
    EXPECT_EQ(b.empty(), knob.refutes)
        << knob.name << (knob.refutes ? ": the gate [0.3, 0.35] survived as x_t = "
                                      : ": the BUG-013 gap closed, x_t = ")
        << (b.empty() ? Box::Interval() : b[xt_]) << " [COMPLETENESS GATE, BUG-013]";
  }
}

// (b) SOUNDNESS sweep over the Taylor order: at EVERY order the enclosure is
// an outward over-approximation, so the SAT instance must stay non-empty and
// the narrowed X_t must retain the closed-form endpoint band [e^-1, 2e^-1].
// A knob-induced empty here would be SOUNDNESS (asserts φ T-unsatisfiable on
// a T-satisfiable φ — false unsat). Higher order = tighter enclosure = the
// direction the lane guards ("tighter must never falsely empty").
TEST_F(KnobsDecayTest, OrderSweep_NeverFalselyEmpties) {
  SetFeasibleBounds();
  for (const int order : {2, 4, 8, 12, 16, 20}) {
    Config config;
    config.mutable_ode_taylor_order().set_from_command_line(order);
    const Box b = Prune(config, ode_direction::FWD);
    ASSERT_FALSE(b.empty())
        << "order " << order
        << " emptied a SAT decay instance [SOUNDNESS GATE: false unsat]";
    EXPECT_LE(b[xt_].lb(), std::exp(-1.0))
        << "order " << order << " over-pruned x_t below 1*e^-1";
    EXPECT_GE(b[xt_].ub(), 2.0 * std::exp(-1.0))
        << "order " << order << " over-pruned x_t above 2*e^-1";
    // Integration-ran guard (mirrors test (a)): an inconclusive skip (CAPD
    // throw -> integrate_tube_slices catch-all -> AddInconclusiveOde) leaves
    // the gate [0.3,0.8] untouched, and the untouched gate satisfies every
    // retention assertion above — the sweep would pass vacuously. The
    // surviving-slice hull lifts X_t.lb to ~e^-1 ≈ 0.368 (the tube minimum,
    // at t=1 from x0=1); X_t.ub stays exactly 0.8 by gate clipping, so only
    // the lb detects narrowing.
    if (order <= 12) {
      EXPECT_GT(b[xt_].lb(), 0.35)
          << "order " << order
          << " did not narrow X_t.lb toward e^-1 — either CAPD integration did "
             "not run (inconclusive skip) or the tube is too loose, so this "
             "sweep value proved nothing";  // INTEGRATION-VERIFY: 0.35 assumes enclosure excess below e^-1 stays < 0.018 at every swept order (order 2 -> ~1e-15/step over ~35k steps by the StepControl trace)
    } else {
      // KNOWN GAP (docs/dreal-bugs.md BUG-013, LIVE): one long CAPD step leaves
      // a wide last sub-slice whose mean-value tube reaches below the gate, so
      // X_t keeps its lower bound — COMPLETENESS (asserts φ^δ T-satisfiable on
      // a T-unsatisfiable φ — missed refutation), never a false unsat. If the
      // tube tightens, tighten this pin to the narrowing assertion above.
      EXPECT_EQ(b[xt_].lb(), 0.3)
          << "order " << order << " now narrows X_t: re-verify BUG-013 and "
             "tighten this pin to EXPECT_GT(lb, 0.35)";
    }
  }
}

// (b) SOUNDNESS sweep over the integrator tolerances (abs and rel varied
// independently, the other left at its 1e-10 default). Tighter tolerances
// shrink the enclosure toward the true trajectory set but can never shrink it
// past a true solution; looser ones only widen (COMPLETENESS cost only —
// missed refutation, not gated here). Retention must hold at every value.
// Consumption of the tolerances is pinned by (b') in Fixture 1 — retention
// alone holds on an untouched gate — so each block below also carries the
// order sweep's integration-ran guard (X_t.lb lifted past 0.35 toward e^-1;
// the ub stays 0.8 by gate clipping and cannot detect narrowing).
TEST_F(KnobsDecayTest, TolSweep_NeverFalselyEmpties) {
  SetFeasibleBounds();
  for (const double tol : {1e-14, 1e-10, 1e-6, 1e-2}) {
    {
      Config config;
      config.mutable_ode_abs_tol().set_from_command_line(tol);
      const Box b = Prune(config, ode_direction::FWD);
      ASSERT_FALSE(b.empty())
          << "abs_tol " << tol
          << " emptied a SAT decay instance [SOUNDNESS GATE: false unsat]";
      EXPECT_LE(b[xt_].lb(), std::exp(-1.0))
          << "abs_tol " << tol << " over-pruned x_t below 1*e^-1";
      EXPECT_GE(b[xt_].ub(), 2.0 * std::exp(-1.0))
          << "abs_tol " << tol << " over-pruned x_t above 2*e^-1";
      if (tol <= 1e-10) {
        EXPECT_GT(b[xt_].lb(), 0.35)
            << "abs_tol " << tol
            << " did not narrow — inconclusive skip or loose tube, sweep value "
               "vacuous";
      } else {
        // KNOWN GAP (BUG-013) — the same pin as in the order sweep.
        EXPECT_EQ(b[xt_].lb(), 0.3)
            << "abs_tol " << tol << " now narrows X_t: re-verify BUG-013 and "
               "tighten this pin to EXPECT_GT(lb, 0.35)";
      }
    }
    {
      Config config;
      config.mutable_ode_rel_tol().set_from_command_line(tol);
      const Box b = Prune(config, ode_direction::FWD);
      ASSERT_FALSE(b.empty())
          << "rel_tol " << tol
          << " emptied a SAT decay instance [SOUNDNESS GATE: false unsat]";
      EXPECT_LE(b[xt_].lb(), std::exp(-1.0))
          << "rel_tol " << tol << " over-pruned x_t below 1*e^-1";
      EXPECT_GE(b[xt_].ub(), 2.0 * std::exp(-1.0))
          << "rel_tol " << tol << " over-pruned x_t above 2*e^-1";
      if (tol <= 1e-10) {
        EXPECT_GT(b[xt_].lb(), 0.35)
            << "rel_tol " << tol
            << " did not narrow — inconclusive skip or loose tube, sweep value "
               "vacuous";
      } else {
        // KNOWN GAP (BUG-013) — the same pin as in the order sweep.
        EXPECT_EQ(b[xt_].lb(), 0.3)
            << "rel_tol " << tol << " now narrows X_t: re-verify BUG-013 and "
               "tighten this pin to EXPECT_GT(lb, 0.35)";
      }
    }
  }
}

// =============================================================================
// Fixture 3: coupled rational dynamics (the semantic file's MockProstateTest):
//   dx/dt = -x * (z / (z + 2)),  dz/dt = -z.
// z(t) = z0*e^-t is decoupled; x has the exact closed form
//   x(t) = x0 * (z(t) + 2) / (z0 + 2)
// (check: d/dt [x0 (z+2)/(z0+2)] = x0 z'/(z0+2) = -x0 z/(z0+2)
//        = -[x0 (z+2)/(z0+2)] * z/(z+2) = -x * z/(z+2)  ✓).
// A nonlinear 2-D flow, so the three C0 set representations (doubleton /
// tripleton / Hermite-Obreshkov-corrected doubleton) actually exercise their
// different reorganization machinery.
// =============================================================================

class KnobsProstateTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"knpr_x", Variable::Type::CONTINUOUS};
  inline static const Variable z_{"knpr_z", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"knpr_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable z0_{"knpr_z_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"knpr_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable zt_{"knpr_z_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"knpr_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, z_, x0_, z0_, xt_, zt_, t0_}};
  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "knobs_mock_prostate",
      vector<std::pair<Variable, Expression>>{
          {x_, -x_ * (z_ / (z_ + 2.0))},
          {z_, -z_},
      });
  Formula MakeIc() const {
    return integral(0.0, t0_, {x0_, z0_}, {xt_, zt_}, ode_);
  }

  // The semantic file's SAT instance: X_0 = x∈[5,10] z∈[1,2],
  // X_t = x∈[0.5,10] z∈[0.2,0.9], t∈[0,1].
  void SetFeasibleBounds() {
    box_[x_] = Box::Interval(0.01, 100.0);  // x > 0 to avoid singularity issues
    box_[z_] = Box::Interval(0.01, 100.0);
    box_[x0_] = Box::Interval(5.0, 10.0);
    box_[z0_] = Box::Interval(1.0, 2.0);
    box_[xt_] = Box::Interval(0.5, 10.0);
    box_[zt_] = Box::Interval(0.2, 0.9);
    box_[t0_] = Box::Interval(0.0, 1.0);
  }

  Box PruneFwd(const Config& config) {
    ContractorStatus cs{box_};
    const auto ic = MakeIc();
    const auto ctc = mk_contractor_ode_lohner(box_, {ic, {}},
                                              ode_direction::FWD, config, 0.0);
    { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }
    return cs.box();
  }
};

// (d) --ode-c0-set: every enum value must produce a valid (true-trajectory-
// retaining) enclosure, and at least two of the three representations must
// give different bounds on this nonlinear instance (the knob-consumption
// pin for the integrate_tube_slices dispatch switch).
//
// Retained point: the t=1 endpoint of the trajectory from (x0,z0)=(7.5,1.5):
//   z(1) = 1.5*e^-1              ≈ 0.55182  ∈ gate [0.2, 0.9]
//   x(1) = 7.5*(z(1)+2)/3.5      ≈ 5.46818  ∈ gate [0.5, 10]
// Both coordinates lie in the gate simultaneously, so the surviving-slice
// hull must contain them at ANY sound knob value; dropping either is
// SOUNDNESS (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat,
// via over-pruning a real solution out of the certified region).
TEST_F(KnobsProstateTest, C0Set_AllRetainTruePoint_AtLeastTwoDiffer) {
  SetFeasibleBounds();
  const double zt_true = 1.5 * std::exp(-1.0);
  const double xt_true = 7.5 * (zt_true + 2.0) / 3.5;

  vector<ibex::Interval> xts;
  vector<ibex::Interval> zts;
  for (const OdeC0SetType set_type :
       {OdeC0SetType::Rect2, OdeC0SetType::Tripleton, OdeC0SetType::HORect2}) {
    Config config;
    config.mutable_ode_c0_set().set_from_command_line(set_type);
    const Box b = PruneFwd(config);
    ASSERT_FALSE(b.empty())
        << "c0-set variant " << static_cast<int>(set_type)
        << " emptied a SAT instance [SOUNDNESS GATE: false unsat]";
    EXPECT_LE(b[zt_].lb(), zt_true + 1e-9)
        << "c0-set variant " << static_cast<int>(set_type)
        << " dropped z(1) = 1.5*e^-1";
    EXPECT_GE(b[zt_].ub(), zt_true - 1e-9)
        << "c0-set variant " << static_cast<int>(set_type)
        << " dropped z(1) = 1.5*e^-1";
    EXPECT_LE(b[xt_].lb(), xt_true + 1e-6)
        << "c0-set variant " << static_cast<int>(set_type)
        << " dropped x(1) = 7.5*(z(1)+2)/3.5";
    EXPECT_GE(b[xt_].ub(), xt_true - 1e-6)
        << "c0-set variant " << static_cast<int>(set_type)
        << " dropped x(1) = 7.5*(z(1)+2)/3.5";
    xts.push_back(b[xt_]);
    zts.push_back(b[zt_]);
  }

  // Knob-consumption pin: the three representations must not all coincide on
  // BOTH narrowed endpoint dims (bit-identical everywhere would mean the
  // dispatch switch never reaches CAPD's set types).
  const bool all_identical =
      xts[0].lb() == xts[1].lb() && xts[0].ub() == xts[1].ub() &&
      xts[0].lb() == xts[2].lb() && xts[0].ub() == xts[2].ub() &&
      zts[0].lb() == zts[1].lb() && zts[0].ub() == zts[1].ub() &&
      zts[0].lb() == zts[2].lb() && zts[0].ub() == zts[2].ub();
  EXPECT_FALSE(all_identical)
      << "Rect2/Tripleton/HORect2 produced bit-identical enclosures — "
         "--ode-c0-set looks like a silent no-op";  // INTEGRATION-VERIFY: representations are expected to differ at some ULP on this nonlinear flow; if they truly coincide here, pick a wider-initial-set instance rather than weakening
}

// =============================================================================
// --ode-backward toggle. Consumed ONLY at theory_solver.cc BuildContractor
// ("--ode-backward=false skips the backward (X_0-narrowing) contractor
// entirely"), so these tests drive TheorySolver::BuildContractor — the layer
// that owns the flag — instead of mk_contractor_ode_lohner (whose direction
// argument bypasses the toggle by design).
//
// Note on the lane's "instance refutable ONLY via backward narrowing": for
// these exact 1-D flows no such instance exists — the contractor's per-slice
// survivor test is symmetric (a forward slice intersecting X_t exists iff a
// backward slice intersecting X_0 exists), so anything BWD refutes, FWD
// refutes too. The BWD contractor's UNIQUE observable is that it is the only
// writer of X_0 (FWD narrows only m_vars_t and time). The mechanism pin is
// therefore: with the toggle off, X_0 stays bit-identical (backward narrowing
// gone), while FWD refutation of a robustly-UNSAT instance is retained.
// =============================================================================

class OdeBackwardToggleTest : public ::testing::Test {
 protected:
  inline static const Variable x_{"kntg_x", Variable::Type::CONTINUOUS};
  inline static const Variable x0_{"kntg_x_0_0", Variable::Type::CONTINUOUS};
  inline static const Variable xt_{"kntg_x_0_t", Variable::Type::CONTINUOUS};
  inline static const Variable t0_{"kntg_time_0", Variable::Type::CONTINUOUS};
  Box box_{vector<Variable>{x_, x0_, xt_, t0_}};
  inline static const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
      "knobs_toggle_decay", vector<std::pair<Variable, Expression>>{{x_, -x_}});
  Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }

  // Only a BWD contractor narrows X_0 here (see KnobsDecayTest::
  // SetBwdNarrowBounds — same geometry): backward image of X_t=[1,1] at
  // pinned t=1 is the point x0 = e.
  void SetBwdNarrowBounds() {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(0.1, 10.0);
    box_[xt_] = Box::Interval(1.0, 1.0);
    box_[t0_] = Box::Interval(1.0, 1.0);
  }

  // Robustly-UNSAT (BUG-006 geometry): the forward tube {x0*e^-t} =
  // [0.368, 2] over t∈[0,1] is disjoint from X_t=[2.5,3]; FWD alone refutes.
  void SetUnsatBounds() {
    box_[x_] = Box::Interval(-100.0, 100.0);
    box_[x0_] = Box::Interval(1.0, 2.0);
    box_[xt_] = Box::Interval(2.5, 3.0);
    box_[t0_] = Box::Interval(0.0, 1.0);
  }

  // Build the theory solver's full fixpoint contractor for {ic} under
  // `config` and return its display string (ContractorFixpoint::display
  // prints every sub-contractor; contractor_ode_lohner prints its direction).
  std::string BuildDisplay(const Config& config, const Formula& ic) {
    TheorySolver theory_solver{config};
    ContractorStatus cs{box_};
    const optional<Contractor> ctc =
        theory_solver.BuildContractor({ic}, &cs);
    EXPECT_TRUE(ctc.has_value())
        << "an ODE-only assertion set must always yield a contractor";
    if (!ctc) return "";
    std::ostringstream oss;
    oss << *ctc;
    return oss.str();
  }

  // Build the fixpoint contractor and run one Prune; return the result box.
  Box BuildAndPrune(const Config& config, const Formula& ic) {
    TheorySolver theory_solver{config};
    ContractorStatus cs{box_};
    const optional<Contractor> ctc =
        theory_solver.BuildContractor({ic}, &cs);
    EXPECT_TRUE(ctc.has_value())
        << "an ODE-only assertion set must always yield a contractor";
    if (!ctc) return cs.box();
    { const UpwardRoundingScope rms_; ctc->Prune(&cs, rms_.token()); }
    return cs.box();
  }
};

// Gating pin: with the default (--ode-backward=true) the built fixpoint
// contains both a FWD and a BWD lohner; with the toggle off the BWD one is
// omitted while FWD remains. (Variable names above avoid the uppercase
// "FWD"/"BWD" substrings, so the display markers below are unambiguous.)
TEST_F(OdeBackwardToggleTest, Off_OmitsBwdContractorFromFixpoint) {
  SetBwdNarrowBounds();
  const Formula ic = MakeIc();

  {
    Config config;  // default: ode_backward = true
    const std::string display = BuildDisplay(config, ic);
    EXPECT_NE(display.find("contractor_ode_lohner(FWD"), std::string::npos)
        << "default build must queue the FWD lohner; got: " << display;
    EXPECT_NE(display.find("contractor_ode_lohner(BWD"), std::string::npos)
        << "default build must queue the BWD lohner; got: " << display;
  }
  {
    Config config;
    config.mutable_ode_backward().set_from_command_line(false);
    const std::string display = BuildDisplay(config, ic);
    EXPECT_NE(display.find("contractor_ode_lohner(FWD"), std::string::npos)
        << "toggle off must keep the FWD lohner; got: " << display;
    EXPECT_EQ(display.find("contractor_ode_lohner(BWD"), std::string::npos)
        << "--ode-backward=false must omit the BWD lohner; got: " << display;
  }
}

// Mechanism pin: BWD is the only writer of X_0. Toggle on: the fixpoint
// narrows X_0=[0.1,10] to a tight band around e. Toggle off: X_0 stays
// bit-identical (FWD narrows only X_t/time; the integer contractor is an
// identity on these CONTINUOUS vars). Turning the toggle off only removes a
// contractor — a pure COMPLETENESS lever (weaker narrowing / missed
// refutation at worst), never SOUNDNESS.
TEST_F(OdeBackwardToggleTest, Off_SkipsBackwardNarrowing_OnKeepsIt) {
  SetBwdNarrowBounds();
  const Formula ic = MakeIc();
  const double kE = std::exp(1.0);

  {
    Config config;  // default: ode_backward = true
    const Box b = BuildAndPrune(config, ic);
    ASSERT_FALSE(b.empty()) << "BWD-narrowing instance is SAT [SOUNDNESS GATE]";
    EXPECT_GT(b[x0_].lb(), kE - 0.05)
        << "with the toggle on, BWD must narrow X_0 to a band near e";
    EXPECT_LT(b[x0_].ub(), kE + 0.05)
        << "with the toggle on, BWD must narrow X_0 to a band near e";
    EXPECT_LE(b[x0_].lb(), kE + 1e-9) << "band must retain the true x0 = e";
    EXPECT_GE(b[x0_].ub(), kE - 1e-9) << "band must retain the true x0 = e";
  }
  {
    Config config;
    config.mutable_ode_backward().set_from_command_line(false);
    const Box b = BuildAndPrune(config, ic);
    ASSERT_FALSE(b.empty()) << "instance is SAT with the toggle off";
    EXPECT_EQ(b[x0_].lb(), 0.1)
        << "with the toggle off nothing may write X_0 — backward narrowing "
           "must be gone, not merely weakened";
    EXPECT_EQ(b[x0_].ub(), 10.0)
        << "with the toggle off nothing may write X_0";
  }
}

// Refutation retention: on the robustly-UNSAT instance the FWD contractor
// refutes alone (semantic DecayFlowTest.FwdInfeasible_BoxEmpties), so the
// toggle being off must not cost this refutation. Failing to empty would be
// COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed
// refutation / false delta-sat).
TEST_F(OdeBackwardToggleTest, Off_FwdRefutationStillEmpties) {
  SetUnsatBounds();
  const Formula ic = MakeIc();
  Config config;
  config.mutable_ode_backward().set_from_command_line(false);
  const Box b = BuildAndPrune(config, ic);
  EXPECT_TRUE(b.empty())
      << "forward tube [0.368,2] is disjoint from X_t=[2.5,3]; with "
         "--ode-backward=false the FWD lohner must still refute "
         "[COMPLETENESS GATE]";
}

}  // namespace
}  // namespace dreal
