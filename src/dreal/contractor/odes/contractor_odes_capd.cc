// CAPD v6 Taylor ODE contractor TU. C++17. (Taylor order is the tunable
// kCapdTaylorOrder constant below — default 20; see OPTIMIZATION_LOG.md.)
//
// CAPD is the sole ODE backend. It is always used for a non-trivial flow (the
// trivial-flow short-circuit in contractor_odes.cc handles the all-zero-RHS
// case separately); there is no longer any backend dispatch or gating. The
// previous Codac CtcLohner backend and the Codac/CAPD gated hybrid (with its
// --capd-t-gate / --capd-ndim-gate flags) were retired — see CODAC_MIGRATION.md
// for that history. CAPD's high-order Taylor IOdeSolver gives tight enclosures
// on long-horizon / high-dimensional dynamics (e.g. the 15-var quad flow with
// sin/cos sub-trees) where a low fixed order would widen out of usefulness.

#include "contractor_odes_capd.h"
#include "to_capd_string.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// CAPD public umbrella header — pulls in IMap, IOdeSolver, ITimeMap,
// C0Rect2Set, IVector, interval, and the basic algebra types.
#include "capd/capdlib.h"

namespace dreal
{
    // -------------------------------------------------------------------------
    // CAPD solver numeric configuration.
    //
    // The performance-critical knobs (Taylor order, abs/rel tolerance, per-step
    // hull grid, C0 set representation, max-step cap) are now runtime-tunable
    // via CapdSolverParams (resolved from Config; see --ode-* flags and the
    // kDefaultOde* constants in solver/config.h for the order-20 rationale).
    // They were previously compile-time constants here; the rationale moved to
    // config.h alongside the defaults. Order/tolerance only ever *widen* the
    // enclosure when loosened — sound, never a false-UNSAT.
    // -------------------------------------------------------------------------

    // Apply the runtime tolerances (and optional max-step cap) to a freshly-
    // constructed CAPD solver. The Taylor order is a constructor argument
    // (params.taylor_order) at each call site.
    template <typename Solver>
    inline void configure_capd_solver(Solver& solver, const CapdSolverParams& params) {
        solver.setAbsoluteTolerance(params.abs_tol);
        solver.setRelativeTolerance(params.rel_tol);
        // max_step <= 0 means "fully adaptive" — leave CAPD's step control alone.
        if (params.max_step > 0.0) solver.setMaxStep(params.max_step);
    }

    // -------------------------------------------------------------------------
    // CapdOdeCache — opaque wrapper holding the per-flow IMap objects.
    //
    // capd::IMap parses the RHS string at construction and builds the
    // automatic-differentiation tree CAPD's solver needs. This is the
    // expensive step; we translate once and amortize it via the cache.
    //
    // capd::IOdeSolver and capd::ITimeMap carry mutable step-size and
    // working state, so they are *not* safe to share across parallel ICP
    // workers. We construct them per-call inside run_capd_*. Cost is
    // negligible compared to the integration itself.
    // -------------------------------------------------------------------------

    class CapdOdeCache {
    public:
        capd::IMap fn_fwd;   // f(x)
        capd::IMap fn_bwd;   // -f(x), used for backward integration
        int        n_state_vars;  // # of TRUE state vars (= var: count = set dim)
        bool       trivial; // every RHS is the literal 0
        // CAPD parameter names (flow variables with d/dt == 0), in ode_list
        // order. Bound per-call from the box via setParameter. The integrated
        // system has dimension n_state_vars; parameters are NOT integrated.
        std::vector<std::string> par_names;

        // fwd/bwd are the CAPD vector-field strings. capd::IMap parses each and
        // builds its AD tree at construction; it is neither movable nor cheap to
        // copy, so build it *directly into the member* from the string rather
        // than copy a prebuilt IMap in. (The old by-value params + std::move were
        // two full IMap copies each — the std::move was a silent no-op because
        // IMap has no move ctor, so it bound to the copy ctor.)
        CapdOdeCache(const std::string& fwd, const std::string& bwd, int n,
                     bool is_trivial, std::vector<std::string> pars)
            : fn_fwd(fwd),
              fn_bwd(bwd),
              n_state_vars(n),
              trivial(is_trivial),
              par_names(std::move(pars)) {}
    };

    bool capd_ode_cache_is_trivial(const std::shared_ptr<CapdOdeCache>& c) {
        return c && c->trivial;
    }

    // -------------------------------------------------------------------------
    // Build the capd::IMap string from the OdeFlow.
    //
    // Format: "var:x,v;par:a;fun:(a*v),(-9.8);"
    //
    // Parameters: flow variables whose d/dt is the literal 0 are constant along
    // the trajectory (OdeFlow classifies these as "pars"). They are emitted in
    // a "par:" section and bound per-call via setParameter, NOT integrated.
    // This must match the C0Rect2Set, which is built from only the true state
    // variables: if parameters were emitted as integration variables the IMap
    // dimension (and its n×n Jacobian) would exceed the set's buffers and
    // overrun the heap. (The previous var-only form had exactly that bug,
    // crashing on automaton models that carry mode/guard parameters.)
    // -------------------------------------------------------------------------

    namespace {
        // Every value that crosses into CAPD, and every enclosure that comes
        // back, must be finite and at most DBL_MAX/2 in magnitude. CAPD splits
        // each interval into a center (lb+ub)/2 and a remainder: an infinite
        // bound makes the center NaN or infinite — CAPD's interval sin then
        // recurses on NaN until the stack overflows (BUG-015) — and a bound
        // beyond DBL_MAX/2 overflows the sum. On the way back a NaN bound reads
        // as an EMPTY ibex interval, which the per-slice filter would take as a
        // refutation, and so would an inverted pair lo > hi. A violation throws
        // into the integrator's catch, so the whole run is inconclusive and no
        // slice of it is used.
        void require_capd_representable(const double lo, const double hi,
                                        const char* what) {
            constexpr double kLimit = std::numeric_limits<double>::max() / 2;
            // Every comparison with NaN is false, so NaN fails too.
            if (!(std::fabs(lo) <= kLimit && std::fabs(hi) <= kLimit && lo <= hi))
                throw std::domain_error(
                    std::string(what) +
                    " has a non-finite or inverted bound, or one beyond DBL_MAX/2");
        }

        void require_capd_representable(const capd::IVector& v, const char* what) {
            for (int i = 0; i < v.dimension(); ++i)
                require_capd_representable(v[i].leftBound(), v[i].rightBound(), what);
        }

        // A start-set interval as a CAPD interval, widened outward by one ulp per
        // bound. CAPD cannot integrate a thin start exactly on a nonzero fixed
        // point of its flow (BUG-016): predictNextEnclosure seeds the remainder
        // with an absolute 1e-300 while outward rounding widens the predicted
        // enclosure by ulp(|x|), so the inclusion test fails at every step size
        // and CAPD throws "minimal time step reached". A set one ulp wider
        // integrates, and it contains the box interval, so every enclosure stays
        // sound. std::nextafter is exact in every rounding mode. Flow parameters
        // are not widened: CAPD evaluates a parameter-only subexpression such as
        // sqrt(p) directly, and widening p = [0, c] below 0 would make it throw.
        capd::interval to_capd_interval(const double lo, const double hi) {
            const double wlo = std::nextafter(lo, -std::numeric_limits<double>::infinity());
            const double whi = std::nextafter(hi, std::numeric_limits<double>::infinity());
            require_capd_representable(wlo, whi, "the integration's start set");
            return capd::interval(wlo, whi);
        }

        struct ImapStrings {
            std::string fwd;
            std::string bwd;
            // CAPD parameter names (d/dt == 0 flow vars), in ode_list order.
            std::vector<std::string> par_names;
            // Number of TRUE state variables = var: count = integration dim.
            int n_vars{0};
        };

        // Build "var:...;par:...;fun:f1,...,fn;" (forward) and the same with
        // each RHS negated (backward). Throws on translation failure of any
        // RHS.
        //
        // Flow variables split into two roles (see OdeFlow ctor: a var is a
        // "par" iff its d/dt is the literal 0, i.e. it is constant along the
        // flow):
        //   - true state variables -> "var:" + an entry in "fun:"; integrated.
        //   - parameters           -> "par:"; constant, bound per-call via
        //                             setParameter, NOT integrated.
        // Emitting parameters as integration variables (the previous behavior)
        // made the IMap dimension exceed the C0Rect2Set built from only the
        // true-var bounds, so CAPD's n×n Jacobian write overran the set's
        // buffers -> heap corruption / crash on automaton models.
        ImapStrings build_imap_strings(
            const OdeFlow& flow,
            const std::vector<Variable>& ordered_vars,
            const NearestRounding& nr)
        {
            std::unordered_map<Variable::Id, Expression> rhs_map;
            for (const auto& [var, rhs] : flow.ode_list)
                rhs_map[var.get_id()] = rhs;

            // All-parameter (trivial) flow: CAPD rejects an empty var:/fun:.
            // Such flows are intercepted by the trivial-flow short-circuit in
            // contractor_odes.cc and never reach run_capd_*, but we still need
            // a valid (non-null) IMap so the cache is built. Fall back to the
            // every-variable-is-an-integration-variable form (no par:).
            bool any_var = false;
            for (const auto& var : ordered_vars)
                if (!flow.is_par(var)) { any_var = true; break; }
            const bool all_par = !any_var;

            std::vector<std::string> par_names;
            std::ostringstream var_part, par_part, fwd_fn, bwd_fn;
            var_part << "var:";
            fwd_fn << "fun:";
            bwd_fn << "fun:";
            bool first_var = true;
            int n_vars = 0;
            for (const auto& var : ordered_vars) {
                if (!all_par && flow.is_par(var)) {
                    par_names.push_back(var.get_name());
                    continue;
                }
                if (!first_var) {
                    var_part << ',';
                    fwd_fn << ',';
                    bwd_fn << ',';
                }
                first_var = false;
                ++n_vars;
                var_part << var.get_name();
                const std::string rhs_str = to_capd_string(rhs_map.at(var.get_id()), nr);
                fwd_fn << rhs_str;
                bwd_fn << "(0-(" << rhs_str << "))";
            }
            var_part << ';';
            fwd_fn << ';';
            bwd_fn << ';';

            std::string par_section;
            if (!par_names.empty()) {
                par_part << "par:";
                for (size_t i = 0; i < par_names.size(); ++i) {
                    if (i) par_part << ',';
                    par_part << par_names[i];
                }
                par_part << ';';
                par_section = par_part.str();
            }

            ImapStrings out;
            out.fwd = var_part.str() + par_section + fwd_fn.str();
            out.bwd = var_part.str() + par_section + bwd_fn.str();
            out.par_names = std::move(par_names);
            out.n_vars = n_vars;
            return out;
        }

        // Return a parameter-bound view of the cached (parsed) IMap with its
        // CAPD parameters bound to the caller's current intervals.
        //
        // We cannot mutate the cached IMap in place (it is shared across
        // parallel ICP workers and setParameter mutates), and a fresh per-call
        // deep copy of `base` rebuilds the whole automatic-differentiation tree
        // — ~part of the ~8% allocation churn at order 20, since the integration
        // itself got cheap. Instead we keep one reusable copy per (thread, base
        // map) in a thread_local cache: the AD tree is copied once per thread,
        // and each call only re-binds the parameters (cheap setParameter) into
        // that copy. This is behavior-identical to copying `base` fresh each
        // call — setParameter fully overwrites the named parameters, the cached
        // base maps are immutable and live for the whole process, and the
        // thread_local storage means no copy is ever shared across workers.
        //
        // The returned reference is valid until the next with_params call for
        // the same base map on this thread; each run_capd_* call binds, hands
        // the map to a local solver, integrates to completion, and returns
        // before the next bind, so there is no aliasing within a thread.
        // par_bounds is index-aligned with par_names (both in ode_list
        // parameter order).
        capd::IMap& with_params(
            const capd::IMap& base,
            const std::vector<std::string>& par_names,
            const std::vector<std::pair<double, double>>& par_bounds)
        {
            thread_local std::unordered_map<const capd::IMap*, capd::IMap> tls;
            auto it = tls.find(&base);
            if (it == tls.end())
                it = tls.emplace(&base, base).first;  // one AD-tree copy per thread
            capd::IMap& m = it->second;
            // Sizes are equal by construction (par_names and par_bounds both
            // come from this flow's parameter list, in ode_list order). A
            // mismatch is a programming error, not a runtime condition to
            // tolerate, so we index par_bounds directly.
            for (size_t i = 0; i < par_names.size(); ++i) {
                require_capd_representable(par_bounds[i].first, par_bounds[i].second,
                                           "a flow parameter");
                m.setParameter(par_names[i],
                               capd::interval(par_bounds[i].first,
                                              par_bounds[i].second));
            }
            return m;
        }

        // Per-process cache slot.
        // shared_ptr<const OdeFlow> in the slot keeps the keyed OdeFlow
        // alive so its raw-pointer key cannot be reused for a different
        // flow (closes the same pointer-reuse hazard).
        struct CacheSlot {
            std::shared_ptr<const OdeFlow> flow;
            std::shared_ptr<CapdOdeCache> cache;
        };
        std::mutex& flow_cache_mutex() {
            static std::mutex m;
            return m;
        }
        std::unordered_map<const OdeFlow*, CacheSlot>&
        flow_cache_map() {
            static std::unordered_map<const OdeFlow*, CacheSlot> m;
            return m;
        }

    } // namespace

    std::shared_ptr<CapdOdeCache> make_capd_ode_cache(
        std::shared_ptr<const OdeFlow> flow,
        const std::vector<Variable>& ordered_vars)
    {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // CAPD's IMap parser/builder leaves the FPU in a directed mode
        // (FE_UPWARD) on return — it does NOT restore nearest. Contain that
        // known clobber here so callers (and their NearestRoundingScope checks)
        // see nearest on return. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
        if (!flow) return nullptr;
        const OdeFlow* const flow_ptr = flow.get();

        // Fast path — existing cache for this flow pointer.
        {
            std::lock_guard<std::mutex> lock(flow_cache_mutex());
            auto& m = flow_cache_map();
            auto it = m.find(flow_ptr);
            if (it != m.end()) return it->second.cache;
        }

        // Trivial-flow detection: every RHS is the literal 0. CAPD copes
        // fine with constant-zero RHS but the trivial-flow short-circuit in
        // contractor_odes.cc bypasses CAPD entirely for these — we still
        // build the IMap so any unanticipated code path stays safe.
        bool is_trivial = true;
        for (const auto& [_var, rhs] : flow->ode_list) {
            if (!is_zero(rhs)) { is_trivial = false; break; }
        }

        // Raise — do not silently skip — if the flow cannot be translated.
        // A null cache would make the contractor's Prune a no-op, leaving the
        // ODE constraint un-narrowed: sound (no narrowing never over-prunes)
        // but a silent under-enforcement that hides an unhandled ODE from the
        // user. Well-formed flows over the supported expression set never hit
        // these paths; if one does, the right answer is a loud failure naming
        // the cause, not a quietly weaker solve. (There is no Codac fallback.)
        ImapStrings strs;
        try {
            strs = build_imap_strings(*flow, ordered_vars, capd_clobber.token());
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("CAPD ODE contractor: cannot translate an ODE flow "
                            "RHS to a CAPD vector field (") + e.what() +
                "). The RHS uses an expression kind to_capd_string does not "
                "support (e.g. if-then-else or an uninterpreted function). "
                "Refusing to silently skip the ODE constraint.");
        }

        try {
            // CapdOdeCache builds both IMaps in place from these strings — no
            // intermediate IMap to copy. A malformed string throws here inside
            // make_shared (IMap ctor) and is caught below.
            auto cache = std::make_shared<CapdOdeCache>(
                strs.fwd, strs.bwd,
                strs.n_vars, is_trivial, std::move(strs.par_names));
            std::lock_guard<std::mutex> lock(flow_cache_mutex());
            auto& m = flow_cache_map();
            auto [it, _inserted] = m.try_emplace(
                flow_ptr, CacheSlot{std::move(flow), std::move(cache)});
            return it->second.cache;
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("CAPD ODE contractor: capd::IMap rejected the "
                            "generated vector-field string (") + e.what() +
                "). Forward field was: '" + strs.fwd +
                "'. Refusing to silently skip the ODE constraint.");
        }
    }

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    namespace {
        // Build a capd::IVector from a [(lb, ub), ...] list.
        capd::IVector to_ivector(const std::vector<std::pair<double, double>>& v) {
            capd::IVector out(static_cast<int>(v.size()));
            for (size_t i = 0; i < v.size(); ++i)
                out[static_cast<int>(i)] = to_capd_interval(v[i].first, v[i].second);
            return out;
        }

        // Centered-in-time (mean-value) range of the step's Taylor curve over a
        // time sub-interval, intersected with CAPD's direct curve(sub) so the
        // result is NEVER looser than the naive evaluation.
        //
        // curve(sub) evaluates the degree-`order` solution polynomial in time by
        // interval Horner over the WHOLE sub: every power of the time variable is
        // treated independently, so a wide sub (large adaptive step and/or low
        // hull-grid) loses time-correlation and the enclosure blows up
        // super-linearly — the interval dependency problem. This is the
        // HULL_COMPLETENESS.md looseness: the tube ends up ~4x wider than CAPD's
        // actual precision, silently weakening interior invariant refutation.
        //
        // The mean-value form  x(sub) ⊆ x(mid) + x'(sub)·(sub - mid)  evaluates
        // the curve at the THIN midpoint time (no time-widening) plus a small
        // derivative-bounded correction. The bound is sound only if
        // timeDerivative(sub) encloses x' for EVERY trajectory of the set and
        // every t in sub; intersecting two sound enclosures still encloses it.
        // CAPD's timeDerivative does so only from CAPD 2a2263c7 on: before it,
        // the initial-condition spread term was f(X)-f(c) instead of X-c, which
        // cut real trajectories off interval initial sets — a false unsat
        // (BUG-018, docs/dreal-bugs.md; CMakeLists.txt pins a fixed CAPD).
        // For a thin initial set the slop is O(r²) in the sub radius r. For an
        // interval initial set the derivative carries the set's spread, so the
        // correction is O(r · spread); the monotone hull below removes that
        // slack for a component that is monotone on sub (BUG-013), and for the
        // others a finer hull-grid can still refute more. The terminal-window
        // clip below uses the same range.
        template <typename CurveT>
        // x_lo and x_hi are curve() at the two ends of sub; the caller passes
        // them because adjacent sub-slices share an end.
        capd::IVector centered_curve_range(const CurveT& curve,
                                           const capd::interval& sub,
                                           const capd::IVector& x_lo,
                                           const capd::IVector& x_hi) {
            const capd::IVector naive = curve(sub);
            const double mid = (sub.leftBound() + sub.rightBound()) / 2.0;
            const capd::IVector x_mid = curve(capd::interval(mid));
            const capd::IVector deriv = curve.timeDerivative(sub);
            const capd::interval dt = sub - capd::interval(mid);
            const int dim = naive.dimension();
            capd::IVector out(dim);
            for (int i = 0; i < dim; ++i) {
                const capd::interval mv = x_mid[i] + deriv[i] * dt;
                // Both mv and naive enclose x_i(sub), so the true range lies in
                // their intersection. Bound comparisons only (no arithmetic) —
                // exact and sound regardless of the ambient FPU rounding mode.
                double lo = std::max(mv.leftBound(), naive[i].leftBound());
                double hi = std::min(mv.rightBound(), naive[i].rightBound());
                // If x_i' keeps one sign over sub for every trajectory of the set
                // (deriv[i] excludes 0), each trajectory's x_i is monotone on sub,
                // so it lies between its values at the two ends: x_i(sub) is
                // inside hull(x_lo[i], x_hi[i]). The mean-value form loses the
                // initial-condition correlation over a wide sub on an interval
                // start set (it pairs the mid value of one trajectory with the
                // derivative of another — BUG-013); the end values keep it.
                if (!deriv[i].contains(0.0)) {
                    lo = std::max(lo, std::min(x_lo[i].leftBound(), x_hi[i].leftBound()));
                    hi = std::min(hi, std::max(x_lo[i].rightBound(), x_hi[i].rightBound()));
                }
                out[i] = capd::interval(lo, hi);
            }
            return out;
        }

        // Integrate `map` from u0 over forward-time [0, t_ub] and append, to
        // out_slices, the time-ordered trajectory sub-slices — cav26's
        // compute_enclosures tube, returned as the full per-slice list rather
        // than a hull. Each slice carries its forward-time interval and the
        // rigorous Taylor curve enclosure over that interval. The contractor
        // then filters per slice (intersect with X_t, check the invariant, drop
        // misses/violators, hull survivors); returning a hull here instead
        // would collapse the per-time / per-component correlation that filter
        // needs (the F2 refutation and the F3 time narrowing both depend on it).
        //
        // Slices span the WHOLE [0, t_ub] (no t_lb clamp): the ForallT
        // invariant must be checked over the entire trajectory the terminal is
        // reached through, while terminal-eligibility ([t_lb, t_ub]) is the
        // caller's decision.
        //
        // Walks CAPD's adaptive steps (stopAfterStep) and sub-grids each step's
        // time domain for tightness. Sets found = false (a sound skip, NOT
        // infeasibility) on ANY CAPD exception — both step-control divergence
        // (range_error / ISolverException) and a mid-enclosure singularity
        // (capd::IntervalError "possible division by zero", thrown e.g. by the
        // sigmoid-inverter flows). cav26 differentiated these and *rethrew* the
        // singularity/logic/runtime classes, relying on an ICP-level catch to
        // turn them into a skip; THIS architecture has no such catch, so a
        // rethrow escapes to terminate() and kills the whole solve (confirmed:
        // it crashed uniform_inverter instances the pre-rewrite code solved).
        // Catch-all-skip is therefore the only sound, non-fatal option here —
        // an un-narrowed box is never a false unsat, so skipping is safe; the
        // body's catch block documents this in full. (No partial-slice hazard:
        // the caller checks found==false and discards out_slices, so a tube
        // truncated by an exception is never used for refutation.)
        template <typename SetT>
        void integrate_tube_slices_impl(
            const capd::IMap& base_map,
            const std::vector<std::string>& par_names,
            const std::vector<std::pair<double, double>>& par_bounds,
            const std::vector<std::pair<double, double>>& u0,
            double win_lb, double t_ub, int n,
            const CapdSolverParams& params,
            CapdTubeResult& result)
        {
            const int kHullGrid = params.hull_grid;  // sub-intervals per step
            std::vector<CapdTubeSlice>& out_slices = result.slices;
            try {
                capd::IMap& map = with_params(base_map, par_names, par_bounds);
                // win_lb only clips the window below; CAPD integrates over [0, t_ub].
                require_capd_representable(0.0, t_ub, "the integration time window");
                capd::IOdeSolver solver(map, params.taylor_order);
                configure_capd_solver(solver, params);
                capd::ITimeMap time_map(solver);
                time_map.stopAfterStep(true);

                SetT set(to_ivector(u0));
                capd::interval prev_time(0.0);

                do {
                    time_map(t_ub, set);
                    // This step covered forward-time [prev_time, currentTime];
                    // the curve's [0, step] domain maps onto it.
                    const auto& curve = solver.getCurve();
                    const capd::interval domain =
                        capd::interval(0.0, 1.0) * solver.getStep();
                    const double d_lo = domain.leftBound();
                    const double d_hi = domain.rightBound();
                    const double dd = (d_hi - d_lo) / kHullGrid;
                    // Curve-domain image of the terminal window [win_lb, t_ub].
                    // CAPD's interval subtraction is outward-rounded, so taking
                    // [lb, ub] of the result yields a sound (over-wide) clip.
                    const capd::interval win_dom =
                        capd::interval(win_lb, t_ub) - prev_time;
                    // The set at each sub-slice boundary, evaluated once.
                    std::vector<double> grid(static_cast<size_t>(kHullGrid) + 1);
                    std::vector<capd::IVector> at_grid;
                    at_grid.reserve(grid.size());
                    for (int k = 0; k <= kHullGrid; ++k) {
                        grid[static_cast<size_t>(k)] = (k == kHullGrid) ? d_hi : d_lo + k * dd;
                        at_grid.push_back(curve(capd::interval(grid[static_cast<size_t>(k)])));
                    }
                    for (int k = 0; k < kHullGrid; ++k) {
                        const auto uk = static_cast<size_t>(k);
                        const capd::interval sub(grid[uk], grid[uk + 1]);
                        const capd::interval slice_time = prev_time + sub;
                        const capd::IVector v =
                            centered_curve_range(curve, sub, at_grid[uk], at_grid[uk + 1]);
                        require_capd_representable(v, "a CAPD tube enclosure");
                        CapdTubeSlice s;
                        s.t_lb = slice_time.leftBound();
                        s.t_ub = slice_time.rightBound();
                        s.state.reserve(static_cast<size_t>(n));
                        for (int i = 0; i < n; ++i)
                            s.state.emplace_back(v[i].leftBound(), v[i].rightBound());
                        // gate_state: the trajectory clipped to the terminal
                        // window. curve(clipped) ⊆ curve(sub), and for a pinned
                        // time the clip collapses to the endpoint point x(t_ub)
                        // — so the X_t gate sees the tight endpoint, not the fat
                        // last-slice tube (BUG-005/008). Left empty (no overlap)
                        // when the sub-slice's curve-domain misses the window.
                        const double gd_lo =
                            std::max(sub.leftBound(), win_dom.leftBound());
                        const double gd_hi =
                            std::min(sub.rightBound(), win_dom.rightBound());
                        if (gd_lo <= gd_hi) {
                            const capd::IVector gv = centered_curve_range(
                                curve, capd::interval(gd_lo, gd_hi),
                                curve(capd::interval(gd_lo)), curve(capd::interval(gd_hi)));
                            require_capd_representable(gv, "a CAPD tube enclosure");
                            s.gate_state.reserve(static_cast<size_t>(n));
                            for (int i = 0; i < n; ++i)
                                s.gate_state.emplace_back(gv[i].leftBound(),
                                                          gv[i].rightBound());
                        }
                        out_slices.push_back(std::move(s));
                    }
                    prev_time = time_map.getCurrentTime();
                } while (!time_map.completed());
                result.found = true;
            }
            // Any exception out of CAPD's integrator is a soundness-neutral
            // numerical event — step-control divergence (ISolverException /
            // range_error) OR a singularity hit mid-enclosure
            // (capd::IntervalError "possible division by zero", thrown e.g. by
            // the sigmoid-inverter flows). Skipping narrowing for this call is
            // always sound (an un-narrowed box is never a false unsat). cav26
            // nominally *rethrew* IntervalError/logic/runtime, relying on an
            // ICP-level catch to turn it into a skip; this architecture has no
            // such catch, so rethrowing escapes to terminate() and kills the
            // whole solve (confirmed: it crashed uniform_inverter instances the
            // pre-rewrite code solved). Catch-all-skip is the sound, non-fatal
            // behavior — fail-loud is not worth aborting a solve on a recoverable
            // integrator singularity. The adapter's own boundary checks
            // (require_capd_representable) land here too.
            // FALLBACK(approved): CAPD failure -> inconclusive ODE, no narrowing — see docs/decisions.md "ODE inconclusive skip"
            catch (const std::exception& e) {
                result.found = false;
                result.failure = e.what();
            }
        }

        // Dispatch the templated integrator over the runtime-selected C0 set
        // type (--ode-c0-set). C++17 has no templated lambdas, so this is one
        // small explicit switch over the enum; the loop body lives once in
        // integrate_tube_slices_impl<SetT>. (run_capd_trace has the twin switch
        // for the visualization path.)
        CapdTubeResult integrate_tube_slices(
            const capd::IMap& base_map,
            const std::vector<std::string>& par_names,
            const std::vector<std::pair<double, double>>& par_bounds,
            const std::vector<std::pair<double, double>>& u0,
            double win_lb, double t_ub, int n,
            const CapdSolverParams& params)
        {
            CapdTubeResult result;
            switch (params.c0_set) {
                case OdeC0SetType::Tripleton:
                    integrate_tube_slices_impl<capd::C0TripletonSet>(
                        base_map, par_names, par_bounds, u0, win_lb, t_ub, n, params, result);
                    return result;
                case OdeC0SetType::HORect2:
                    integrate_tube_slices_impl<capd::C0HORect2Set>(
                        base_map, par_names, par_bounds, u0, win_lb, t_ub, n, params, result);
                    return result;
                case OdeC0SetType::Rect2:
                    break;
            }
            integrate_tube_slices_impl<capd::C0Rect2Set>(
                base_map, par_names, par_bounds, u0, win_lb, t_ub, n, params, result);
            return result;
        }

    } // namespace

    // -------------------------------------------------------------------------
    // FWD: forward-integrate f(x) from X_0 over [0, t_ub] and return the
    // time-ordered trajectory slices. The caller intersects each terminal-
    // eligible slice with the X_t box, checks the invariant per slice, and
    // decides narrow-vs-refute — see the header. X_0 narrowing is the standalone
    // BWD contractor's job (theory_solver.cc queues one per ODE constraint),
    // mirroring cav26's two-contractor design.
    // -------------------------------------------------------------------------

    CapdTubeResult run_capd_fwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double win_lb,
        double t_ub,
        const CapdSolverParams& params,
        const NearestRounding& /*nr*/)
    {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // Contain CAPD's directed-mode clobber so this adapter is nearest-in /
        // nearest-out. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
        // Prune establishes these before calling (no CAPD map, the trivial-flow
        // short-circuit, the time window); reaching here without them is a bug.
        if (!cache || cache->n_state_vars == 0 || t_ub <= 0.0)
            throw std::logic_error("run_capd_fwd: called without a CAPD map, "
                                   "state variables, or a positive end time");
        return integrate_tube_slices(cache->fn_fwd, cache->par_names, par_bounds,
                                     u0_bounds, win_lb, t_ub, cache->n_state_vars, params);
    }

    // -------------------------------------------------------------------------
    // BWD: integrate -f(x) from X_t over [0, t_ub] and return the time-ordered
    // backward-image slices — each the set of states whose forward trajectory
    // under f(x) reaches X_t at that reverse-time slice. The caller (in its
    // swapped frame where m_vars_t = original X_0) intersects each with the
    // right gate. Symmetric with run_capd_fwd (-f vs f).
    // -------------------------------------------------------------------------

    CapdTubeResult run_capd_bwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& Xt_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double win_lb,
        double t_ub,
        const CapdSolverParams& params,
        const NearestRounding& /*nr*/)
    {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // Contain CAPD's directed-mode clobber so this adapter is nearest-in /
        // nearest-out. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
        // Prune establishes these before calling (no CAPD map, the trivial-flow
        // short-circuit, the time window); reaching here without them is a bug.
        if (!cache || cache->n_state_vars == 0 || t_ub <= 0.0)
            throw std::logic_error("run_capd_bwd: called without a CAPD map, "
                                   "state variables, or a positive end time");
        return integrate_tube_slices(cache->fn_bwd, cache->par_names, par_bounds,
                                     Xt_bounds, win_lb, t_ub, cache->n_state_vars, params);
    }

    // -------------------------------------------------------------------------
    // run_capd_trace
    //
    // Integrate the chosen RHS (fn_fwd / fn_bwd) from u0 over [0, t_ub] and
    // record one CapdTracePoint per equally-spaced sub-slice. Each point
    // captures the slice (t_prev, t_now) and the enclosure at t_now (which
    // covers all states reachable up to that time since the previous slice).
    //
    // ITimeMap advances `set` in place across successive calls. We rely on
    // that: the i-th call returns the enclosure at t_i = i * (t_ub / n_steps),
    // having advanced `set` from the result of call i-1.
    // -------------------------------------------------------------------------

    namespace {
        template <typename SetT>
        void run_capd_trace_impl(
            const std::shared_ptr<CapdOdeCache>& cache,
            const std::vector<std::pair<double, double>>& u0,
            const std::vector<std::pair<double, double>>& par_bounds,
            double t_ub, bool forward, int n, int n_steps,
            const CapdSolverParams& params,
            CapdTraceResult& result)
        {
            try {
                capd::IMap& chosen_map = with_params(
                    forward ? cache->fn_fwd : cache->fn_bwd,
                    cache->par_names, par_bounds);
                require_capd_representable(0.0, t_ub, "the trace time window");
                capd::IOdeSolver solver(chosen_map, params.taylor_order);
                configure_capd_solver(solver, params);
                capd::ITimeMap time_map(solver);

                SetT set(to_ivector(u0));

                const double dt = t_ub / static_cast<double>(n_steps);
                double t_prev = 0.0;
                result.points.reserve(static_cast<size_t>(n_steps));

                for (int i = 1; i <= n_steps; ++i) {
                    const double t_i = (i == n_steps) ? t_ub
                                                      : static_cast<double>(i) * dt;
                    const capd::IVector encl = time_map(t_i, set);
                    require_capd_representable(encl, "a CAPD trace enclosure");

                    CapdTracePoint pt;
                    pt.t_lb = t_prev;
                    pt.t_ub = t_i;
                    pt.var_enclosures.reserve(static_cast<size_t>(n));
                    for (int j = 0; j < n; ++j) {
                        pt.var_enclosures.emplace_back(encl[j].leftBound(),
                                                       encl[j].rightBound());
                    }
                    result.points.push_back(std::move(pt));
                    t_prev = t_i;
                }
                result.succeeded = true;
            }
            // FALLBACK(approved): CAPD failure -> --visualize trace keeps the points before it — see docs/decisions.md "ODE inconclusive skip"
            catch (const std::exception&) {
                // Partial trace remains in result.points; succeeded stays false.
            }
        }
    } // namespace

    CapdTraceResult run_capd_trace(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        bool forward,
        const CapdSolverParams& params,
        const NearestRounding& /*nr*/,
        int n_steps)
    {
        DREAL_ASSERT_ROUNDING(FE_TONEAREST);
        // Contain CAPD's directed-mode clobber so this adapter is nearest-in /
        // nearest-out. See ExpectClobber in rounding.h.
        const NearestRoundingScope capd_clobber{expect_clobber};
        CapdTraceResult result;
        if (!cache) return result;
        const int n = cache->n_state_vars;
        if (n == 0 || t_ub <= 0.0 || n_steps <= 0) return result;
        if (u0.size() != static_cast<size_t>(n)) return result;

        // Twin of the integrate_tube_slices dispatch — pick the C0 set type.
        switch (params.c0_set) {
            case OdeC0SetType::Tripleton:
                run_capd_trace_impl<capd::C0TripletonSet>(
                    cache, u0, par_bounds, t_ub, forward, n, n_steps, params, result);
                return result;
            case OdeC0SetType::HORect2:
                run_capd_trace_impl<capd::C0HORect2Set>(
                    cache, u0, par_bounds, t_ub, forward, n, n_steps, params, result);
                return result;
            case OdeC0SetType::Rect2:
                break;
        }
        run_capd_trace_impl<capd::C0Rect2Set>(
            cache, u0, par_bounds, t_ub, forward, n, n_steps, params, result);
        return result;
    }

} // namespace dreal
