//
// Created by Kunal Sheth on 9/2/25.
// Post-Codac elimination: CAPD is the sole ODE backend (order-20 Taylor; see
// kCapdTaylorOrder in contractor_odes_capd.cc for why per-slice wants order-20).
//

#include "contractor_odes.h"
#include "contractor_odes_capd.h"

#include <cassert>
#include <chrono>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "dreal/contractor/contractor_ibex_fwdbwd.h"
#include "dreal/solver/config.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/rounding.h"

namespace dreal
{
    using nlohmann::json;
    using std::vector;
    using std::pair;
    using std::string;
    using std::ostringstream;

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------

    // See contractor_odes.h for the contract (both unroller naming conventions).
    unsigned int ode_step_from_name(const std::string& name) {
        const size_t last = name.rfind('_');
        if (last == std::string::npos || last == 0) return 0;
        // SMT-LIB BMC unroller: trailing "_k<int>"  (x_decay_IntX_k0 -> 0).
        if (last + 1 < name.size() && name[last + 1] == 'k') {
            try { return static_cast<unsigned int>(std::stoi(name.substr(last + 2))); }
            catch (...) {}
        }
        // dReach unroller: integer between the last two '_'  (height_3_t -> 3).
        const size_t second_last = name.rfind('_', last - 1);
        if (second_last != std::string::npos) {
            const std::string step_part = name.substr(second_last + 1, last - second_last - 1);
            try { return static_cast<unsigned int>(std::stoi(step_part)); }
            catch (...) {}
        }
        return 0;
    }

    std::ostream& operator<<(std::ostream& out, ode_direction const& d) {
        switch (d) {
        case ode_direction::FWD:  out << "FWD"; break;
        case ode_direction::BWD:  out << "BWD"; break;
        }
        return out;
    }

    std::vector<Formula> unroll_conjunctions(const Formula& f) {
        if (is_conjunction(f)) {
            std::vector<Formula> result;
            for (const Formula& f_i : get_operands(f)) {
                const auto unrolled = unroll_conjunctions(f_i);
                result.insert(result.end(), unrolled.begin(), unrolled.end());
            }
            return result;
        }
        return {f};
    }

    // The flow parameters of an integral, at its start and its end.
    static Variables parameter_vars(const FormulaIntegral* icc) {
        Variables vars;
        for (const auto& v : icc->get_pars_0()) vars.insert(v);
        for (const auto& v : icc->get_pars_t()) vars.insert(v);
        return vars;
    }

    // Intersect pars_0 and pars_t in place (a flow parameter is constant along
    // the trajectory). Returns false if a pair is disjoint (box emptied). Sets the
    // output bit of every parameter it narrows and reports in *narrowed whether it
    // narrowed any, so the caller can record the ODE as a used constraint.
    static bool intersect_params(ContractorStatus* cs, const FormulaIntegral* icc,
                                 bool* narrowed) {
        Box& b = cs->mutable_box();
        const auto& pars_0 = icc->get_pars_0();
        const auto& pars_t = icc->get_pars_t();
        *narrowed = false;
        for (size_t i = 0; i < pars_0.size(); ++i) {
            const ibex::Interval iv_0 = b[pars_0[i]];
            const ibex::Interval iv_t = b[pars_t[i]];
            const ibex::Interval intersected = iv_0 & iv_t;
            if (intersected.is_empty()) {
                b.set_empty();
                return false;
            }
            for (const auto& [v, before] : {std::pair{pars_0[i], iv_0}, std::pair{pars_t[i], iv_t}}) {
                if (intersected != before) {
                    b[v] = intersected;
                    cs->mutable_output().set(b.index(v));
                    *narrowed = true;
                }
            }
        }
        return true;
    }

    // The integration end time as an interval: a variable's box interval, a real
    // constant's bounds, or the point [c, c] of a constant c (cav26 accepted all
    // three). FormulaIntegral's constructor admits only these forms. Prune and
    // generate_trace both read the window here.
    static ibex::Interval end_time_window(const Expression& time_t, const Box& b) {
        if (is_variable(time_t)) return b[get_variable(time_t)];
        if (is_real_constant(time_t))
            return ibex::Interval(get_lb_of_real_constant(time_t),
                                  get_ub_of_real_constant(time_t));
        if (is_constant(time_t)) return ibex::Interval(get_constant_value(time_t));
        throw DREAL_RUNTIME_ERROR(
            "contractor_ode_lohner: integral end time {} is neither a "
            "variable nor a constant", fmt::streamed(time_t));
    }

    // ---------------------------------------------------------------------------
    // contractor_ode_lohner — constructor
    // ---------------------------------------------------------------------------

    contractor_ode_lohner::contractor_ode_lohner(
        Box const& box, const ode_constraint& ctr,
        ode_direction const dir, Config const& config,
        double const timeout)
        : ContractorCell{Contractor::Kind::ODE_LOHNER, DynamicBitset(box.size()), config},
          m_dir{dir},
          m_ctr{ctr},
          m_timeout{timeout}
    {
        const auto& ic = m_ctr.first;
        const auto* const icc = to_integral(ic);

        // Guard an otherwise-silent assumption: the flow is always integrated
        // from t=0 (run_capd_* start the integrator at 0 with u0 = X_0), so the
        // integral's LOWER time bound get_time_0() is never read. A non-zero (or
        // variable) t0 would silently integrate from the wrong start — a
        // soundness hole — so fail loud in every build (not a debug-only
        // DREAL_ASSERT). Covers both Prune and generate_trace (same icc).
        {
            const Expression& t0 = icc->get_time_0();
            const bool t0_is_zero =
                is_constant(t0, 0.0) ||
                (is_real_constant(t0) && get_lb_of_real_constant(t0) == 0.0 &&
                 get_ub_of_real_constant(t0) == 0.0);
            if (!t0_is_zero)
                throw DREAL_RUNTIME_ERROR(
                    "contractor_ode_lohner: integral lower time bound must be 0 "
                    "(the flow is integrated from t=0); non-zero/variable t0 "
                    "is unsupported");
        }

        DynamicBitset& inp{mutable_input()};
        for (const auto& var : ic.GetFreeVariables()) inp.set(box.index(var));

        if (m_dir == ode_direction::FWD) {
            m_vars_0 = icc->get_vars_0();
            m_vars_t = icc->get_vars_t();
            m_pars_0 = icc->get_pars_0();
            m_pars_t = icc->get_pars_t();
        } else {
            m_vars_0 = icc->get_vars_t();
            m_vars_t = icc->get_vars_0();
            m_pars_0 = icc->get_pars_t();
            m_pars_t = icc->get_pars_0();
        }

        if (!m_ctr.second.empty()) {
            UpwardRoundingScope g;
            for (const auto& inv : m_ctr.second) {
                const auto* const invc = to_forallT(inv);
                if (is_conjunction(invc->get_bound_f())) {
                    std::vector<Contractor> ctcs;
                    for (const auto& nl_ctr : get_operands(invc->get_bound_f())) {
                        DREAL_LOG_INFO("Building ibex contractor for {} expr within {}", nl_ctr, inv);
                        ctcs.push_back(make_contractor_ibex_fwdbwd(nl_ctr, box, config));
                    }
                    m_inv_ctcs.push_back(make_contractor_seq(ctcs, config));
                } else {
                    DREAL_LOG_INFO("Building ibex contractor for {}", inv);
                    m_inv_ctcs.push_back(make_contractor_ibex_fwdbwd(invc->get_bound_f(), box, config));
                }
            }
            m_need_to_check_inv = true;
        }

        // Precompute the ode_state_vars ordering and build the CAPD IMap
        // cache once. Prune() reuses it on every call.
        m_ode_state_vars.reserve(icc->get_flow()->ode_list.size());
        for (const auto& [ode_var, _rhs] : icc->get_flow()->ode_list)
            m_ode_state_vars.push_back(ode_var);
        {
            NearestRoundingScope g;
            // CAPD's IMap parser is moderately expensive; doing it here
            // keeps Prune off the cold per-flow translation path. If the RHS
            // cannot be translated/parsed, make_capd_ode_cache *raises* (rather
            // than silently returning a no-op contractor); it returns null only
            // when there is no flow at all, which raises here, so Prune and
            // generate_trace never see a missing map.
            m_capd_cache = make_capd_ode_cache(icc->get_flow(), m_ode_state_vars);
        }
        if (!m_capd_cache)
            throw DREAL_RUNTIME_ERROR("contractor_ode_lohner: no CAPD map for {}",
                                      fmt::streamed(ic));

        // Resolve the runtime CAPD knobs for this instance. A lohner contractor
        // is single-direction, so the Taylor order comes from the matching flag
        // (forward vs backward); the rest are shared.
        m_capd_params = CapdSolverParams{
            /*taylor_order=*/ (m_dir == ode_direction::FWD)
                                  ? config.ode_taylor_order()
                                  : config.ode_backward_order(),
            /*abs_tol=*/ config.ode_abs_tol(),
            /*rel_tol=*/ config.ode_rel_tol(),
            /*hull_grid=*/ config.ode_hull_grid(),
            /*c0_set=*/ config.ode_c0_set(),
            /*max_step=*/ config.ode_max_step(),
        };
    }

    // ---------------------------------------------------------------------------
    // Prune
    // ---------------------------------------------------------------------------

    void contractor_ode_lohner::Prune(ContractorStatus* cs, const UpwardRounding& ur) const {
        // Crossing the gaol->CAPD boundary: before we switch to FE_TONEAREST,
        // assert the incoming mode is still what the guard stack expects. A
        // failure here means something changed the FPU mode without a guard
        // (the classic CAPD-clobber / un-guarded-getter hazard). Debug-only.
        DREAL_ASSERT_ROUNDING_CONSISTENT();
        NearestRoundingScope g;
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);

        DREAL_LOG_DEBUG("contractor_ode_lohner::Prune [{} dir={}]",
                        m_ctr.first, m_dir == ode_direction::FWD ? "FWD" : "BWD");

        const auto& ic     = m_ctr.first;
        const auto* const icc = to_integral(ic);

        // --- Step 1: Intersect parameters (pars_0 ∩ pars_t) ---
        // A parameter narrowing is a contraction like any other, so it is
        // recorded here whatever the rest of the Prune does: otherwise a conflict
        // that rests on it gets an explanation without this ODE (the class of
        // docs/constraint-order-explanation-soundness.md — a false unsat). The
        // step reads only the parameters' bounds and the p0 = pt the integral
        // implies, so an explanation reaches the ODE through its parameters
        // alone; through its states too, it pulled a BMC unrolling's state
        // constraints into every lemma (BUG-021). The linked forall_t
        // invariants take no part in it.
        bool params_narrowed = false;
        if (!intersect_params(cs, icc, &params_narrowed)) {
            for (const auto& v : icc->get_pars_0()) cs->mutable_output().set(cs->box().index(v));
            for (const auto& v : icc->get_pars_t()) cs->mutable_output().set(cs->box().index(v));
            cs->AddUsedConstraint(ic, parameter_vars(icc));
            return;
        }
        if (params_narrowed) {
            cs->AddUsedConstraint(ic, parameter_vars(icc));
        }

        // --- Step 2: T=0 special case ---
        const auto& icct = icc->get_time_t();
        const bool time_is_zero =
            (is_variable(icct) && cs->box()[get_variable(icct)].ub() == 0.0) ||
            is_constant(icct, 0.0);

        if (time_is_zero) {
            const Box old_box = cs->box();
            for (size_t i = 0; i < m_vars_0.size(); ++i) {
                ibex::Interval& iv_0 = cs->mutable_box()[m_vars_0[i]];
                ibex::Interval& iv_t = cs->mutable_box()[m_vars_t[i]];
                iv_0 &= iv_t;
                if (iv_0.is_empty()) {
                    cs->mutable_box().set_empty();
                    cs->AddUsedConstraint(ic);
                    cs->AddUsedConstraint(m_ctr.second);
                    cs->mutable_output() |= input();
                    return;
                }
                iv_t = iv_0;
            }
            for (int i = 0; i < old_box.size(); ++i) {
                if (cs->box()[i] != old_box[i])
                    cs->mutable_output().set(static_cast<DynamicBitset::size_type>(i));
            }
            cs->AddUsedConstraint(ic);
            cs->AddUsedConstraint(m_ctr.second);
            return;
        }

        // --- Step 3: ODE trajectory integration via CAPD order-20 Taylor ---
        //
        // The ForallT invariant is NOT checked here at the pre-integration box.
        // It is checked per trajectory slice in the filter below: an invariant
        // that holds at the endpoints but is violated in the trajectory
        // *interior* is invisible to a box-only check (the regression this
        // restores — cav26 checked check_invariant on every tube slice).
        //
        // Direction handling:
        //   FWD (m_dir == FWD): m_vars_0 = original X_0, m_vars_t = original
        //     X_t. run_capd_fwd forward-integrates f(x) from X_0 over [0, t_ub]
        //     and returns the trajectory slices; we intersect each terminal-
        //     eligible slice with X_t. X_0 narrowing is the standalone BWD
        //     contractor's job (theory_solver queues one per ODE constraint).
        //   BWD (m_dir == BWD): the constructor swapped variables, so m_vars_0
        //     = original X_t, m_vars_t = original X_0. run_capd_bwd integrates
        //     -f(x) from u0 (= original X_t); the slices are the backward image
        //     (states whose forward trajectory reaches X_t), intersected with
        //     m_vars_t (= original X_0). Sound by construction.
        //
        // If CAPD fails — any exception out of the integrator, or a start set,
        // parameter, time window or enclosure CAPD cannot represent — the run
        // returns found=false and this pass narrows nothing; the ODE is recorded
        // as inconclusive (spliced into explanations, reported at a delta-sat
        // verdict). There is no other backend to fall back to.

        // Integration-time window [win_lb, win_ub]. The rewrite had read only a
        // variable end time, silently skipping the ODE for a literal duration (a
        // latent false delta-sat); end_time_window reads all three forms. (T == 0
        // was already handled in Step 2.)
        const ibex::Interval win = end_time_window(icct, cs->box());
        const double win_lb = win.lb();
        const double win_ub = win.ub();
        // FALLBACK(approved): negative time window -> inconclusive ODE — see docs/decisions.md "ODE inconclusive skip"
        if (win_ub <= 0.0) {
            cs->AddInconclusiveOde(ic, "the integration time window ends below 0");
            return;
        }

        const int n = static_cast<int>(m_vars_0.size());

        // Trivial-flow short-circuit: every RHS is the literal constant 0, so
        // every state variable is constant along the trajectory. The original
        // constraint reduces to X_0 == X_t componentwise — identical to the
        // time_is_zero case handled above. Without this, CAPD's IOdeSolver
        // is called ~N_modes times per ICP step and dominates Prune cost on
        // the k1280 planning benchmark. Direction-agnostic.
        if (capd_ode_cache_is_trivial(m_capd_cache)) {
            const Box old_box = cs->box();
            for (size_t i = 0; i < m_vars_0.size(); ++i) {
                ibex::Interval& iv_0 = cs->mutable_box()[m_vars_0[i]];
                ibex::Interval& iv_t = cs->mutable_box()[m_vars_t[i]];
                iv_0 &= iv_t;
                if (iv_0.is_empty()) {
                    cs->mutable_box().set_empty();
                    cs->AddUsedConstraint(ic);
                    cs->AddUsedConstraint(m_ctr.second);
                    cs->mutable_output() |= input();
                    return;
                }
                iv_t = iv_0;
            }
            for (int i = 0; i < old_box.size(); ++i) {
                if (cs->box()[i] != old_box[i])
                    cs->mutable_output().set(static_cast<DynamicBitset::size_type>(i));
            }
            cs->AddUsedConstraint(ic);
            return;
        }

        // Initial condition: start-of-integration intervals.
        // FWD: m_vars_0 = original X_0 (start of forward integration)
        // BWD: m_vars_0 = original X_t (start of backward integration)
        std::vector<std::pair<double, double>> u0_bounds;
        u0_bounds.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const ibex::Interval& iv = cs->box()[m_vars_0[static_cast<size_t>(i)]];
            u0_bounds.emplace_back(iv.lb(), iv.ub());
        }

        // Flow parameters (d/dt == 0 vars): bound the CAPD map's par: section
        // to their current box intervals. Ordered to match the cache's
        // par_names (== ode_list parameter order == m_pars_0 order). Params are
        // constant along the flow, so m_pars_0's interval is a sound value.
        std::vector<std::pair<double, double>> par_bounds;
        par_bounds.reserve(m_pars_0.size());
        for (const auto& pvar : m_pars_0) {
            const ibex::Interval& iv = cs->box()[pvar];
            par_bounds.emplace_back(iv.lb(), iv.ub());
        }

        const CapdTubeResult res =
            (m_dir == ode_direction::FWD)
            ? run_capd_fwd(m_capd_cache, u0_bounds, par_bounds, win_lb, win_ub, m_capd_params, g.token())
            : run_capd_bwd(m_capd_cache, u0_bounds, par_bounds, win_lb, win_ub, m_capd_params, g.token());

        // found == false means CAPD diverged (step-control failure): no sound
        // enclosure, so skip narrowing for this call. This is NOT infeasibility
        // — divergence carries no information, whereas a successful integration
        // whose tube is disjoint from the gate (or whose interior violates the
        // invariant) IS infeasibility and is refuted below.
        // FALLBACK(approved): CAPD failure -> inconclusive ODE, no narrowing — see docs/decisions.md "ODE inconclusive skip"
        if (!res.found) {
            cs->AddInconclusiveOde(ic, res.failure);
            return;
        }

        // --- Per-slice filter (cav26 compute_enclosures terminal-window filter
        //     + check_invariant), done here where the box and invariant
        //     contractors live so CAPD stays numeric-only:
        //   * walk slices in forward-time order;
        //   * invariant: write each slice's state into m_vars_t (of a box copy)
        //     and run the ForallT contractors; the FIRST slice they EMPTY is a
        //     trajectory-interior violation — every later terminal is then
        //     unreachable, so stop;
        //   * terminal window: a slice that precedes any violation and whose
        //     time overlaps [win_lb, win_ub] is terminal-eligible; intersect its
        //     state with the X_t gate (m_vars_t box) and keep non-empty results;
        //   * hull the kept intersections → narrowed X_t, hull their times →
        //     narrowed T. If NONE survive → infeasible → set_empty.
        // CAPD enclosures are outward over-approximations, so an empty survivor
        // set (disjoint tube and/or invariant violation) proves true
        // infeasibility: set_empty is sound and cannot cause a false unsat.
        //
        // FE_UPWARD for the ibex/gaol invariant contractors; the interval ops
        // (& / hull) are mode-independent so they ride along safely.
        const UpwardRoundingScope inv_scope;
        DREAL_ASSERT(m_ctr.second.size() == m_inv_ctcs.size());

        // The ForallT invariant is enforced per-slice ONLY in the FWD contractor.
        // The invariant constrains the *forward* trajectory x(t); FWD's slices
        // ARE that trajectory (m_vars_t = original X_t = the invariant's
        // variables), so writing a slice into m_vars_t and running the HC4
        // invariant contractors tests the invariant at that trajectory time.
        // The theory solver queues a FWD contractor alongside every BWD one, so
        // FWD always runs and the invariant is always enforced. The BWD slices
        // are the backward image expressed over m_vars_t = original X_0 (NOT the
        // invariant's variables), so a BWD invariant check would only re-test the
        // invariant against the static X_t box — cav26's BWD check was exactly
        // this no-op-ish form; we drop it (sound: FWD covers the invariant, and
        // an over-permissive BWD only under-narrows). This also lets us hoist a
        // single box copy: for FWD the invariant touches only m_vars_t, which we
        // overwrite each slice, so one reused copy is behavior-identical to a
        // fresh per-slice copy — without the O(box) copy on every sub-slice that
        // made invariant-heavy flows (e.g. the k256 thermostat) time out.
        const bool check_inv = m_need_to_check_inv && m_dir == ode_direction::FWD;
        std::unique_ptr<ContractorStatus> cs_inv;       // one copy, reused
        if (check_inv) cs_inv = std::make_unique<ContractorStatus>(*cs);

        bool have_keep = false;
        std::vector<ibex::Interval> keep_state(static_cast<size_t>(n));
        double keep_t_lb = 0.0, keep_t_ub = 0.0;

        for (const CapdTubeSlice& slice : res.slices) {
            // Invariant: a slice enclosure wholly outside the invariant region
            // (the contractor empties it) proves the trajectory leaves that
            // region at some interior time → infeasible from here onward.
            if (check_inv) {
                for (size_t i = 0; i < m_vars_t.size(); ++i)
                    cs_inv->mutable_box()[m_vars_t[i]] =
                        ibex::Interval(slice.state[i].first, slice.state[i].second);
                bool violated = false;
                for (size_t i = 0; i < m_inv_ctcs.size(); ++i) {
                    m_inv_ctcs[i].Prune(cs_inv.get(), inv_scope.token());
                    if (cs_inv->box().empty()) { violated = true; break; }
                }
                if (violated) break;
            }

            // Terminal-eligible iff the slice overlaps the dwell window
            // [win_lb, win_ub] — encoded as a non-empty gate_state (the
            // window-clipped enclosure; integrate_tube_slices leaves it empty
            // off-window). NB: the invariant check above still ran on the full
            // tube `state`, over the whole [0, win_ub] interior.
            if (slice.gate_state.empty()) continue;

            // Intersect the WINDOW-CLIPPED enclosure (not the full tube `state`)
            // with the X_t gate. This is the BUG-005/008 fix: for a pinned time
            // the clip collapses to the point x(win_ub), so X_t contracts to the
            // tight endpoint rather than the fat last-slice tube.
            std::vector<ibex::Interval> inter(static_cast<size_t>(n));
            bool slice_kept = true;
            for (int i = 0; i < n; ++i) {
                const ibex::Interval gate = cs->box()[m_vars_t[static_cast<size_t>(i)]];
                const ibex::Interval enc(slice.gate_state[static_cast<size_t>(i)].first,
                                         slice.gate_state[static_cast<size_t>(i)].second);
                inter[static_cast<size_t>(i)] = gate & enc;
                if (inter[static_cast<size_t>(i)].is_empty()) { slice_kept = false; break; }
            }
            if (!slice_kept) continue;

            // Time hull uses the in-window portion of the slice, matching the
            // clipped gate_state.
            const double in_t_lb = std::max(slice.t_lb, win_lb);
            const double in_t_ub = std::min(slice.t_ub, win_ub);
            if (!have_keep) {
                keep_state = std::move(inter);
                keep_t_lb = in_t_lb;
                keep_t_ub = in_t_ub;
                have_keep = true;
            } else {
                for (int i = 0; i < n; ++i)
                    keep_state[static_cast<size_t>(i)] |= inter[static_cast<size_t>(i)];
                keep_t_lb = std::min(keep_t_lb, in_t_lb);
                keep_t_ub = std::max(keep_t_ub, in_t_ub);
            }
        }

        // No surviving terminal slice → the constraint is infeasible on this box.
        if (!have_keep) {
            cs->mutable_box().set_empty();
            cs->AddUsedConstraint(ic);
            cs->AddUsedConstraint(m_ctr.second);
            cs->mutable_output() |= input();
            return;
        }

        // Narrow the X_t gate (m_vars_t) to the surviving-slice state hull, and
        // the time variable to the surviving-slice time hull.
        bool changed = false;
        for (int i = 0; i < n; ++i) {
            const ibex::Interval old_iv = cs->box()[m_vars_t[static_cast<size_t>(i)]];
            if (keep_state[static_cast<size_t>(i)] != old_iv) {
                cs->mutable_box()[m_vars_t[static_cast<size_t>(i)]] =
                    keep_state[static_cast<size_t>(i)];
                cs->mutable_output().set(cs->box().index(m_vars_t[static_cast<size_t>(i)]));
                changed = true;
            }
        }
        if (is_variable(icct)) {
            const Variable time_var = get_variable(icct);
            const ibex::Interval old_t = cs->box()[time_var];
            const ibex::Interval new_t = old_t & ibex::Interval(keep_t_lb, keep_t_ub);
            if (!new_t.is_empty() && new_t != old_t) {
                cs->mutable_box()[time_var] = new_t;
                cs->mutable_output().set(cs->box().index(time_var));
                changed = true;
            }
        }

        if (changed) {
            cs->AddUsedConstraint(ic);
            cs->AddUsedConstraint(m_ctr.second);
        }
    }

    // ---------------------------------------------------------------------------
    // generate_trace
    // ---------------------------------------------------------------------------

    json contractor_ode_lohner::generate_trace(ContractorStatus cs_copy) {
        // CAPD's interval integrator expects the FPU in round-to-nearest. (Its
        // DoubleRounding sets directed modes per-op but — as the rounding
        // tripwire caught — leaves the FPU in a directed mode on return rather
        // than restoring nearest; run_capd_trace contains that clobber
        // internally via an ExpectClobber scope.) Prune() establishes nearest at
        // its top; generate_trace is a separate entry point (the --visualize
        // path) and must establish it too, rather than relying on whatever mode
        // the caller left the FPU in. This scope's dtor verifies containment.
        NearestRoundingScope g;

        const auto& ic     = m_ctr.first;
        const auto* const icc = to_integral(ic);
        Box& b = cs_copy.mutable_box();

        // Intersect parameters before tracing. Disjoint start and end values
        // mean the box does not satisfy this integral: no trajectory to draw.
        bool params_narrowed = false;
        if (!intersect_params(&cs_copy, icc, &params_narrowed))
            throw DREAL_RUNTIME_ERROR(
                "generate_trace: a flow parameter of {} has disjoint start and "
                "end values", fmt::streamed(ic));

        // The integration window, read as Prune reads it. A window ending below
        // 0, which Prune skips as inconclusive, has no trajectory to draw.
        const ibex::Interval win = end_time_window(icc->get_time_t(), b);
        if (win.ub() < 0.0)
            throw DREAL_RUNTIME_ERROR(
                "generate_trace: the integration time window of {} ends below 0",
                fmt::streamed(ic));

        // Initial condition for integration (direction-adjusted: m_vars_0 is start).
        std::vector<std::pair<double, double>> u0_bounds;
        u0_bounds.reserve(m_vars_0.size());
        for (const auto& var : m_vars_0) {
            const ibex::Interval& iv = b[var];
            u0_bounds.emplace_back(iv.lb(), iv.ub());
        }

        // Flow parameters (d/dt == 0 vars), ordered to match the cache's
        // par_names; bound into the CAPD map's par: section before tracing.
        std::vector<std::pair<double, double>> par_bounds;
        par_bounds.reserve(m_pars_0.size());
        for (const auto& pvar : m_pars_0) {
            const ibex::Interval& iv = b[pvar];
            par_bounds.emplace_back(iv.lb(), iv.ub());
        }

        // No trajectory points over a zero-length window (Prune's Step 2) or for
        // a trivial flow, whose every variable is a parameter (Prune's
        // short-circuit; m_vars_0 is empty). Otherwise CAPD's enclosures over
        // [0, win.ub()]; a CAPD failure keeps the points before it, possibly none.
        std::vector<CapdTracePoint> points;
        if (win.ub() > 0.0 && !capd_ode_cache_is_trivial(m_capd_cache))
            points = run_capd_trace(m_capd_cache, u0_bounds, par_bounds, win.ub(),
                                    m_dir == ode_direction::FWD, m_capd_params,
                                    g.token()).points;

        json ret = json::array();
        const std::string& mode_name = icc->get_flow()->name;
        const size_t n = m_vars_0.size();

        // One JSON entry per ODE state variable.
        for (size_t i = 0; i < n; ++i) {
            const std::string name = m_vars_0[i].get_name();
            json entry;
            entry["key"]    = name;
            entry["mode"]   = mode_name;
            entry["step"]   = ode_step_from_name(name);
            entry["values"] = json::array();
            for (const auto& pt : points) {
                json value;
                value["time"]      = {pt.t_lb, pt.t_ub};
                value["enclosure"] = {pt.var_enclosures[i].first,
                                      pt.var_enclosures[i].second};
                entry["values"].push_back(value);
            }
            ret.push_back(entry);
        }

        // One JSON entry per parameter variable: just start and end time points.
        for (const auto& var : m_pars_0) {
            const std::string name = var.get_name();
            const ibex::Interval& iv = b[var];
            json entry;
            entry["key"]    = name;
            entry["mode"]   = mode_name;
            entry["step"]   = ode_step_from_name(name);
            entry["values"] = json::array();
            json v_begin, v_end;
            v_begin["time"]      = {0.0, 0.0};
            v_begin["enclosure"] = {iv.lb(), iv.ub()};
            v_end["time"]        = {win.lb(), win.ub()};
            v_end["enclosure"]   = {iv.lb(), iv.ub()};
            entry["values"].push_back(v_begin);
            entry["values"].push_back(v_end);
            ret.push_back(entry);
        }

        return ret;
    }

    // ---------------------------------------------------------------------------
    // display
    // ---------------------------------------------------------------------------

    std::ostream& contractor_ode_lohner::display(std::ostream& out) const {
        out << "contractor_ode_lohner(" << m_dir << ", " << m_ctr.first << ")";
        return out;
    }

} // namespace dreal
