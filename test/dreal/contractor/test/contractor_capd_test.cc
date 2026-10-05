/*********************************************************************
Author: Soonho Kong <soonhok@cs.cmu.edu>

dReal -- Copyright (C) 2013 - 2016, the dReal Team

dReal is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

dReal is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with dReal. If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

// ported from dReal3.

#include "dreal/contractor/odes/contractor_odes.h"
#include "dreal/contractor/odes/contractor_odes_capd.h"

#include <cfenv>
#include <iostream>
#include <dreal/util/rounding.h>

#include <gtest/gtest.h>

#include "dreal/contractor/contractor_status.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/interval.h"
#include "dreal/util/string_to_interval.h"

namespace dreal
{
    namespace
    {
        using std::cerr;
        using std::endl;
        using std::make_pair;
        using std::make_shared;
        using std::vector;

        class ContractorCapdFullTest : public ::testing::Test
        {
        protected:
            // Variables (mirrors the original test’s symbols)
            const Variable x_{"x", Variable::Type::CONTINUOUS};
            const Variable x0_{"x_0_0", Variable::Type::CONTINUOUS};
            const Variable xt_{"x_0_t", Variable::Type::CONTINUOUS};

            const Variable p_{"p", Variable::Type::CONTINUOUS};
            const Variable p0_{"p_0_0", Variable::Type::CONTINUOUS};
            const Variable pt_{"p_0_t", Variable::Type::CONTINUOUS};

            const Variable t0_{"time_0", Variable::Type::CONTINUOUS};

            // Variable ordering in the Box matches the original:
            // [x, x0, xt, p, p0, pt, t0]
            const vector<Variable> vars_{x_, x0_, xt_, p_, p0_, pt_, t0_};
            Box box_{vars_};

            // rhs: x' = 1, p' = (1/sqrt(2π)) * exp(-x²/2)
            const Expression rhs_x_{1.0};
            const Expression rhs_p_{(1.0 / sqrt(2.0 * M_PI)) * exp(-(pow(x_, 2) / 2.0))};

            // ODE with state vector [x, p]
            const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
                "flow_1", std::vector<std::pair<Variable, Expression>>{
                    {x_, rhs_x_}, {p_, rhs_p_}
                }
            );

            //   integral( [xt, pt], 0, t0, [x0, p0], ode_ )
            Formula MakeIntegralConstraint() const {
                return integral(
                    0.0, t0_, {x0_, p0_}, {xt_, pt_}, ode_
                );
            }

            // Utility: set the common base domains.
            void SetCommonDomains() {
                box_[x_] = Box::Interval(-100.0, 100.0);
                box_[x0_] = Box::Interval(-100.0, 100.0);
                box_[xt_] = Box::Interval(-100.0, 100.0);
                box_[p_] = Box::Interval(0.0, 1.0);
                box_[p0_] = Box::Interval(0.0, 1.0);
                box_[pt_] = Box::Interval(0.0, 1.0);
                box_[t0_] = Box::Interval(0.0, 40.0);
            }
        };

        TEST_F(ContractorCapdFullTest, CapdFwd) {
            SetCommonDomains();

            // Pin initial/terminal values as in the original:
            //   x0 = -10, xt = 10, p0 = 0
            box_[x0_] = Box::Interval(-10.0);
            box_[xt_] = Box::Interval(10.0);
            box_[p0_] = Box::Interval(0.0);

            Config config; // precision etc. if you want to tweak
            ContractorStatus cs{box_};

            const auto ic = MakeIntegralConstraint();

            // Create the CAPD contractor (forward)
            const auto ctc = mk_contractor_ode_lohner(
                box_, {ic, {}}, ode_direction::FWD, config, 0.0
            );

            // Check inputs before pruning (mirrors original expectations)
            // Original checked 7 booleans in this order: [x, x0, xt, p, p0, pt, t0]
            // After construction, contractor input mask should reflect which dims it reads.
            EXPECT_FALSE(ctc.input()[0]); // x
            EXPECT_TRUE(ctc.input()[1]); // x0
            EXPECT_TRUE(ctc.input()[2]); // xt
            EXPECT_FALSE(ctc.input()[3]); // p
            EXPECT_TRUE(ctc.input()[4]); // p0
            EXPECT_TRUE(ctc.input()[5]); // pt
            EXPECT_TRUE(ctc.input()[6]); // t0

            // Box is not empty before pruning.
            EXPECT_FALSE(cs.box().empty());

            { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

            // The box must NOT refute: this instance is genuinely delta-sat.
            // x'=1 gives x=10 at t=20; p is the cumulative standard normal, and
            // p(20) = Φ(10)−Φ(-10) = erf(10/√2) is *strictly below 1* (the
            // normal mass outside ±10 is ~1e-22 but nonzero). So the true
            // terminal (x=10, p≈0.99999998 ∈ [0,1]) satisfies every gate, and a
            // sound contractor keeps it. The per-slice filter does: it narrows
            // to that witness band — t0 ∋ 20 and pt to the sub-1 cumulative.
            //
            // The previous "box EMPTIES" expectation was a FALSE-UNSAT, not a
            // sound refutation. It was calibrated to the to_capd_string 6-digit
            // truncation (std::to_string), which fed CAPD an unfaithful gaussian
            // coefficient (1/√(2π) → "0.398942") so its rigorous enclosure
            // tracked the *wrong* vector field and the filter wrongly refuted.
            // Restoring full-precision coefficients (to_capd_string.h, 17 sig
            // figs) makes CAPD integrate the true field → correct delta-sat.
            // Same root cause as the water/thermostat automaton false-unsats and
            // ode_soundness_repros/ws_taupin.smt2 (a clock whose gate sits at the
            // integration-window end). The CAPD per-slice filter itself is sound;
            // it was the feed that lied.
            ASSERT_FALSE(cs.box().empty())
                << "(x=10, p=Φ(10)−Φ(-10)<1) is a true witness in the gates; "
                   "refuting it is a false-unsat";
            // Narrowed to the real witness time x=10 ⇒ t=20.
            EXPECT_LE(cs.box()[t0_].lb(), 20.0);
            EXPECT_GE(cs.box()[t0_].ub(), 20.0);
            // pt narrowed within the gate to the near-1 cumulative.
            EXPECT_LE(cs.box()[pt_].ub(), 1.0);
            EXPECT_GT(cs.box()[pt_].ub(), 0.999);
        }

        TEST_F(ContractorCapdFullTest, CapdBwd) {
            SetCommonDomains();

            // Pin initial/terminal values as in the original:
            //   x0 = -10, xt = 10, pt = 1
            box_[x0_] = Box::Interval(-10.0);
            box_[xt_] = Box::Interval(10.0);
            box_[pt_] = Box::Interval(1.0);

            Config config;
            ContractorStatus cs{box_};

            const auto ic = MakeIntegralConstraint();

            // Create the CAPD contractor (backward)
            const auto ctc = mk_contractor_ode_lohner(
                box_, {ic, {}}, ode_direction::BWD, config, 0.0
            );

            // Input mask — same as original:
            EXPECT_FALSE(ctc.input()[0]); // x
            EXPECT_TRUE(ctc.input()[1]); // x0
            EXPECT_TRUE(ctc.input()[2]); // xt
            EXPECT_FALSE(ctc.input()[3]); // p
            EXPECT_TRUE(ctc.input()[4]); // p0
            EXPECT_TRUE(ctc.input()[5]); // pt
            EXPECT_TRUE(ctc.input()[6]); // t0

            // Non-empty before pruning.
            EXPECT_FALSE(cs.box().empty());

            { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

            // The BWD mirror of CapdFwd: must NOT refute. BWD integrates -f from
            // the pinned terminal (xt=10, pt=1) back to X_0=(x0=-10, p0∈[0,1]).
            // To forward-reach (x=10, p=1) from x0=-10 needs p0 = 1 − ∫₋₁₀¹⁰
            // gaussian = 1 − (Φ(10)−Φ(-10)) ≈ 1.96e-8, which is *just above 0*
            // and inside the gate [0,1] (because the faithful integral is < 1).
            // So a sound contractor keeps it and narrows p0 to ≈[0, ~3e-9],
            // t0 ∋ 20. The earlier "box EMPTIES / needs p0<0" expectation was the
            // same to_capd_string-truncation false-unsat as CapdFwd (the
            // truncated coefficient drove the enclosed integral past 1, forcing
            // p0<0); the precision fix restores the correct delta-sat.
            ASSERT_FALSE(cs.box().empty())
                << "backward image of (x=10,p=1) lands at p0≈1−erf>0, inside the "
                   "gate; refuting it is a false-unsat";
            EXPECT_LE(cs.box()[t0_].lb(), 20.0);
            EXPECT_GE(cs.box()[t0_].ub(), 20.0);
            // p0 narrowed to the tiny residual 1 − ∫gaussian, within [0,1].
            EXPECT_GE(cs.box()[p0_].lb(), 0.0);
            EXPECT_LT(cs.box()[p0_].ub(), 0.5);

            // On a narrowing prune the contractor records the integral constraint.
            const auto& used = cs.UsedConstraints();
            EXPECT_TRUE(used.find(ic) != used.end());
        }

        // --visualize coverage: generate_trace() is the trajectory generator the
        // --visualize path calls (smt2/driver.cc). It runs CAPD via
        // run_capd_trace. No test exercised it before. These cover (1) the
        // rounding invariant — CAPD's directed-mode clobber must be contained so
        // generate_trace is safe to call inside the driver's NearestRoundingScope
        // — and (2) the shape of the emitted trajectory JSON.

        TEST_F(ContractorCapdFullTest, GenerateTraceDoesNotClobberRounding) {
            SetCommonDomains();
            box_[x0_] = Box::Interval(-10.0);  // pin the IC to a point and use a
            box_[p0_] = Box::Interval(0.0);    // short horizon so CAPD converges
            box_[t0_] = Box::Interval(0.0, 2.0);

            Config config;
            const auto ctc = mk_contractor_ode_lohner(
                box_, {MakeIntegralConstraint(), {}}, ode_direction::FWD, config, 0.0);

            ContractorStatus cs{box_};
            nlohmann::json trace;
            {
                // Mirror the driver's --visualize context: generate_trace runs
                // inside a NearestRoundingScope, whose dtor tripwire aborts if
                // CAPD's clobber escaped run_capd_trace's ExpectClobber scope.
                const NearestRoundingScope g;
                trace = to_ode_lohner(ctc)->generate_trace(cs);
                EXPECT_EQ(fegetround(), FE_TONEAREST);  // CAPD clobber contained
            }  // g's dtor would abort here if containment had failed
            EXPECT_FALSE(trace.empty());  // CAPD actually ran (not an early-return)
        }

        TEST_F(ContractorCapdFullTest, GenerateTraceTrajectoryShape) {
            SetCommonDomains();
            box_[x0_] = Box::Interval(-10.0);
            box_[p0_] = Box::Interval(0.0);
            box_[t0_] = Box::Interval(0.0, 2.0);

            Config config;
            const auto ctc = mk_contractor_ode_lohner(
                box_, {MakeIntegralConstraint(), {}}, ode_direction::FWD, config, 0.0);
            ContractorStatus cs{box_};

            const NearestRoundingScope g;
            const nlohmann::json trace = to_ode_lohner(ctc)->generate_trace(cs);

            // One JSON entry per state variable, in m_vars_0 order [x_0_0, p_0_0].
            ASSERT_TRUE(trace.is_array());
            ASSERT_EQ(trace.size(), 2u);
            EXPECT_EQ(trace[0]["key"].get<std::string>(), "x_0_0");
            EXPECT_EQ(trace[1]["key"].get<std::string>(), "p_0_0");
            for (const auto& entry : trace) {
                EXPECT_EQ(entry["mode"].get<std::string>(), "flow_1");
                ASSERT_TRUE(entry["values"].is_array());
                ASSERT_FALSE(entry["values"].empty());
                for (const auto& sample : entry["values"]) {
                    // Each sample is a [t_lb, t_ub] time and a [lb, ub] enclosure.
                    ASSERT_EQ(sample["time"].size(), 2u);
                    ASSERT_EQ(sample["enclosure"].size(), 2u);
                    EXPECT_LE(sample["time"][0].get<double>(),
                              sample["time"][1].get<double>());
                    EXPECT_LE(sample["enclosure"][0].get<double>(),
                              sample["enclosure"][1].get<double>());
                }
            }
        }

        // Regression: a flow with a TRUE parameter (a flow variable whose
        // d/dt is the literal 0, so it is constant along the trajectory).
        // OdeFlow classifies these as "pars"; they must be emitted in the
        // CAPD IMap's "par:" section and bound via setParameter, NOT as
        // integration variables. The previous code emitted every flow
        // variable in "var:", so the IMap dimension (here 2) exceeded the
        // C0Rect2Set built from only the true state vars (here 1). CAPD then
        // wrote its 2x2 Jacobian into 1-sized buffers, overrunning the heap
        // (confirmed via guard malloc: a crash in Map::operator() during
        // run_capd_fwd). This fixture exercises exactly that mismatch.
        class ContractorCapdParamTest : public ::testing::Test
        {
        protected:
            // x is a true state variable; a is a parameter (d/dt[a] = 0).
            const Variable x_{"x", Variable::Type::CONTINUOUS};
            const Variable x0_{"x_0_0", Variable::Type::CONTINUOUS};
            const Variable xt_{"x_0_t", Variable::Type::CONTINUOUS};
            const Variable a_{"a", Variable::Type::CONTINUOUS};
            const Variable a0_{"a_0_0", Variable::Type::CONTINUOUS};
            const Variable at_{"a_0_t", Variable::Type::CONTINUOUS};
            const Variable t0_{"time_0", Variable::Type::CONTINUOUS};

            // Box order: [x, x0, xt, a, a0, at, t0]
            const vector<Variable> vars_{x_, x0_, xt_, a_, a0_, at_, t0_};
            Box box_{vars_};

            // x' = a (couples through the parameter), a' = 0 (constant).
            const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
                "flow_p", std::vector<std::pair<Variable, Expression>>{
                    {x_, Expression{a_}}, {a_, Expression{0.0}}
                }
            );

            Formula MakeIntegralConstraint() const {
                return integral(0.0, t0_, {x0_, a0_}, {xt_, at_}, ode_);
            }
        };

        TEST_F(ContractorCapdParamTest, ForwardWithParameterDoesNotOverrun) {
            box_[x_]  = Box::Interval(-100.0, 100.0);
            box_[x0_] = Box::Interval(0.0);          // x(0) = 0
            box_[xt_] = Box::Interval(-100.0, 100.0);
            box_[a_]  = Box::Interval(1.0);
            box_[a0_] = Box::Interval(1.0);          // parameter value a = 1
            box_[at_] = Box::Interval(1.0);
            box_[t0_] = Box::Interval(2.0);          // integrate to t = 2

            Config config;
            ContractorStatus cs{box_};
            const auto ic = MakeIntegralConstraint();
            const auto ctc = mk_contractor_ode_lohner(
                box_, {ic, {}}, ode_direction::FWD, config, 0.0);

            // The integral binds only the true state var x; a is a parameter.
            EXPECT_TRUE(ctc.input()[1]);   // x0
            EXPECT_TRUE(ctc.input()[2]);   // xt

            // Pre-fix this Prune corrupted the heap (var/par dim mismatch).
            { const UpwardRoundingScope rms_; ctc.Prune(&cs, rms_.token()); }

            // Sound, non-trivial result: x(t) = x0 + a*t = 0 + 1*2 = 2, so the
            // terminal enclosure narrows xt toward {2}. (If the parameter were
            // dropped, x' would read 0 and xt would collapse to {0} instead —
            // this is the heap-overrun regression guard, the test's purpose.)
            //
            // With the per-slice tube filter and a *pinned* terminal time
            // (t0={2}), the narrowed xt is the last terminal-eligible sub-slice
            // of the integration step — here ~[1.875, 2.0] (mid 1.9375, width
            // = step/kHullGrid). Tighter than this would need cav26's terminal-
            // window clamp; the looser bound below still confirms the parameter
            // is applied (xt≈2, decisively not 0) and nothing overran. Free-time
            // ODE constraints (the benchmark norm) take many small CAPD steps,
            // so their terminal slices are fine-grained; the coarseness here is
            // specific to a single big step over a pinned dwell time.
            ASSERT_FALSE(cs.box().empty());
            EXPECT_TRUE(cs.output()[2]);   // xt narrowed
            EXPECT_NEAR(cs.box()[xt_].mid(), 2.0, 0.15);
            EXPECT_LT(cs.box()[xt_].diam(), 0.2);
        }

        // A parameter's start and end values must agree. Disjoint values mean the
        // box does not satisfy the integral, so the --visualize trace has no
        // trajectory to draw. (generate_trace used to return an empty array.)
        TEST_F(ContractorCapdParamTest, Trace_DisjointParameterValues_Throws) {
            box_[x_]  = Box::Interval(-100.0, 100.0);
            box_[x0_] = Box::Interval(0.0);
            box_[xt_] = Box::Interval(-100.0, 100.0);
            box_[a_]  = Box::Interval(1.0, 2.0);
            box_[a0_] = Box::Interval(1.0);
            box_[at_] = Box::Interval(2.0);
            box_[t0_] = Box::Interval(2.0);
            const auto ctc = mk_contractor_ode_lohner(
                box_, {MakeIntegralConstraint(), {}}, ode_direction::FWD, Config{}, 0.0);
            const NearestRoundingScope g;
            EXPECT_THROW(to_ode_lohner(ctc)->generate_trace(ContractorStatus{box_}),
                         std::runtime_error);
        }

        // --visualize regression for the non-conventional state-var naming the
        // translator emits (`x_k<step>` rather than dReal's `<base>_<step>_{0,t}`).
        // generate_trace's per-entry `key`/`step` come from the start variable
        // name (m_vars_0). See simulink-to-dreal/docs/dreal-bugs.md BUG-004/007.
        // A decay flow d/dt[x] = -0.5 x from x_k1 = 100 over t ∈ [0,1].
        class ContractorCapdNonConvNameTest : public ::testing::Test
        {
        protected:
            const Variable x_{"x", Variable::Type::CONTINUOUS};
            const Variable x0_{"x_k1", Variable::Type::CONTINUOUS};  // segment start
            const Variable xt_{"x_k2", Variable::Type::CONTINUOUS};  // segment end
            const Variable t0_{"time_k1", Variable::Type::CONTINUOUS};
            const vector<Variable> vars_{x_, x0_, xt_, t0_};
            Box box_{vars_};
            const std::shared_ptr<const OdeFlow> ode_ = make_shared<OdeFlow>(
                "flow_1", std::vector<std::pair<Variable, Expression>>{
                    {x_, -0.5 * x_}});
            Formula MakeIc() const { return integral(0.0, t0_, {x0_}, {xt_}, ode_); }
            nlohmann::json Trace() {
                box_[x_]  = Box::Interval(0.0, 100.0);
                box_[x0_] = Box::Interval(100.0);
                box_[xt_] = Box::Interval(0.0, 100.0);
                box_[t0_] = Box::Interval(0.0, 1.0);
                Config config;
                const auto ctc = mk_contractor_ode_lohner(
                    box_, {MakeIc(), {}}, ode_direction::FWD, config, 0.0);
                ContractorStatus cs{box_};
                const NearestRoundingScope g;
                return to_ode_lohner(ctc)->generate_trace(cs);
            }
        };

        // BUG-004: a non-conventional state-var name (`x_k1`) used to make the
        // --visualize trace generator throw out of extract_step and leave the
        // JSON empty (cav26 + dReal v3.16.12). It must now emit a full trace,
        // keyed verbatim by the variable name, with a non-empty trajectory.
        TEST_F(ContractorCapdNonConvNameTest, NonConventionalName_EmitsFullTrace) {
            const nlohmann::json trace = Trace();
            ASSERT_TRUE(trace.is_array());
            ASSERT_FALSE(trace.empty()) << "BUG-004: trace must not be empty";
            EXPECT_EQ(trace[0]["key"].get<std::string>(), "x_k1");
            ASSERT_TRUE(trace[0]["values"].is_array());
            EXPECT_FALSE(trace[0]["values"].empty())
                << "BUG-004: non-conventional name must still get a trajectory";
        }

        // BUG-007 (FIXED): generate_trace derives each segment's `step` field
        // via ode_step_from_name, which now recognizes the SMT-LIB BMC
        // unroller's `x_k<step>` suffix in addition to dReach's
        // `<base>_<step>_{0,t}`. The segment starting at `x_k1` must report
        // step 1 (was 0 — all segments collapsed onto index 0). See
        // docs/dreal-bugs.md BUG-007 and OdeStepFromName.* for the parser unit
        // tests covering both conventions.
        TEST_F(ContractorCapdNonConvNameTest, StepFieldReflectsSegmentIndex) {
            const nlohmann::json trace = Trace();
            ASSERT_TRUE(trace.is_array());
            ASSERT_FALSE(trace.empty());
            EXPECT_EQ(trace[0]["step"].get<int>(), 1)
                << "BUG-007: x_k1's segment must report step 1, not 0";
        }

        // run_capd_trace refuses a call its caller must not make (here a start set
        // sized for no state variables) instead of returning no points, so an
        // empty trace means CAPD failed. (It used to return an empty result.)
        TEST_F(ContractorCapdNonConvNameTest, RunCapdTrace_MismatchedStartSet_Throws) {
            const Config c;
            const CapdSolverParams params{c.ode_taylor_order(), c.ode_abs_tol(),
                                          c.ode_rel_tol(), c.ode_hull_grid(),
                                          c.ode_c0_set(), c.ode_max_step()};
            const NearestRoundingScope g;
            const auto cache = make_capd_ode_cache(ode_, {x_});
            EXPECT_THROW(run_capd_trace(cache, {}, {}, 1.0, true, params, g.token()),
                         std::logic_error);
        }
    } // namespace
} // namespace dreal
