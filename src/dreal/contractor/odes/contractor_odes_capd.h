#pragma once
// C++17 interface for the CAPD v6 ODE integration backend.
// The implementation (contractor_odes_capd.cc) is also compiled at C++17.
//
// CAPD's Taylor order-20 integrator is the sole ODE backend for dReal after
// the Codac elimination — see ../../../../CODAC_MIGRATION.md.

#include <memory>
#include <utility>
#include <vector>

#include "dreal/contractor/odes/ode_types.h"
#include "dreal/symbolic/symbolic.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"
#include "dreal/util/rounding.h"

namespace dreal
{
    // One sub-slice of the integrated trajectory tube. `t_lb`/`t_ub` is the
    // slice's forward-time interval (measured from the integration start at
    // t=0); `state[i]` is the rigorous enclosure of state variable i over that
    // time interval. The slices tile the integration window [0, t_ub_query] in
    // time order — this is cav26's compute_enclosures output, returned to the
    // contractor so the box/invariant filter (intersect each slice with X_t,
    // check the ForallT invariant per slice, drop misses, hull survivors) runs
    // where the ibex/box logic lives, keeping CAPD numeric-only.
    struct CapdTubeSlice {
        double t_lb;
        double t_ub;
        // Full trajectory tube over [t_lb, t_ub]. Used for the per-slice ForallT
        // invariant check, which must see the entire interior the terminal is
        // reached through.
        std::vector<std::pair<double, double>> state;
        // The trajectory enclosure CLIPPED to the terminal window [win_lb,
        // win_ub] — i.e. over [max(t_lb,win_lb), min(t_ub,win_ub)]. Empty when
        // the slice does not overlap the window. The terminal X_t gate
        // intersects against THIS, not `state`: for a pinned time the clip
        // collapses to the point x(win_ub), so the endpoint contracts tightly
        // instead of fattening to the whole last sub-slice tube (BUG-005/008).
        std::vector<std::pair<double, double>> gate_state;
    };

    // Result of a tube integration: the time-ordered slices plus a `found`
    // flag. `found == false` means the integrator failed (step-control
    // divergence, or a singularity hit mid-enclosure — e.g. CAPD IntervalError
    // "possible division by zero"); a sound skip in every case (no enclosure,
    // never infeasibility). The adapter catches all such exceptions rather than
    // rethrowing — an escaping exception would terminate the whole solve.
    struct CapdTubeResult {
        std::vector<CapdTubeSlice> slices;
        bool found{false};
        std::string failure;  // why found is false (CAPD's what(), or the adapter's)
    };

    // Opaque cache holding the per-flow capd::IMap (and the negated -f(x)
    // IMap used for backward integration). The full type is only visible to
    // the CAPD translation unit so the rest of the codebase doesn't pull in
    // CAPD headers.
    //
    // Same lifetime guarantee as CodacOdeCache: built once per OdeFlow,
    // reused across every Prune call. capd::IOdeSolver instances are
    // constructed per-call inside run_capd_* because they carry mutable
    // step-size state and can't be safely shared across parallel ICP
    // workers (unlike Codac's const CtcLohner::contract).
    class CapdOdeCache;

    // Build a cache for `flow`. Returns nullptr only when `flow` is null;
    // it RAISES (std::runtime_error) if an RHS cannot be translated into the
    // capd::IMap string format (e.g. an unsupported ExpressionKind).
    //
    // `ordered_vars` MUST match the order in `flow.ode_list`.
    //
    // The cache holds a shared_ptr<const OdeFlow> in the slot to keep the
    // OdeFlow alive while the cache entry is live — same fix as the Codac
    // cache's pointer-reuse hazard (see contractor_odes_codac.cc).
    std::shared_ptr<CapdOdeCache> make_capd_ode_cache(
        std::shared_ptr<const OdeFlow> flow,
        const std::vector<Variable>& ordered_vars);

    // True if every RHS in the cached flow is the literal 0 constant. The
    // Codac cache also detects this; either flag is sufficient for the
    // trivial-flow short-circuit in contractor_odes.cc, but exposing it on
    // the CAPD side keeps the contractor self-contained.
    bool capd_ode_cache_is_trivial(const std::shared_ptr<CapdOdeCache>& cache);

    // Run CAPD forward integration via Taylor order-20.
    //
    // Integrate the cached f(x) IMap from u0_bounds = X_0 using
    // capd::IOdeSolver(order=kCapdTaylorOrder, i.e. 20) + capd::ITimeMap over
    // the WHOLE window [0, t_ub] and return the time-ordered list of trajectory
    // sub-slices (CapdTubeResult::slices), each carrying its forward-time
    // interval and the rigorous state enclosure over it. This is cav26's
    // compute_enclosures tube — but the full tube, not just a hull: the caller
    // (contractor_ode_lohner::Prune) does the filter (intersect each slice with
    // the X_t box, check the ForallT invariant on each slice over [0, t_ub],
    // drop misses/violators, hull the survivors → narrowed X_t + time). A hull
    // returned by the adapter would collapse the per-time, per-component
    // correlation the filter needs to refute (and to narrow time). The slices
    // span [0, t_ub] — NOT [t_lb, t_ub] — because the invariant must be checked
    // over the entire trajectory the terminal is reached through, while only
    // slices whose time lands in [t_lb, t_ub] are terminal-eligible (the caller
    // applies that window).
    //
    // Returns found == false if the cache is null or the integrator fails
    // (step-control divergence OR a mid-enclosure singularity such as CAPD
    // IntervalError "division by zero") — a sound skip in all cases, never
    // infeasibility. All such exceptions are caught, not rethrown (an escaping
    // one would terminate the solve; there is no ICP-level contractor catch).
    //
    // par_bounds carries the current interval value of each flow parameter
    // (a flow variable whose d/dt is the literal 0), ordered to match the
    // cache's parameter list (== ode_list parameter order == m_pars_0 order).
    // These are bound into a private copy of the cached IMap via
    // setParameter before integration; they are NOT integration variables.
    //
    // win_lb is the lower bound of the terminal window [win_lb, t_ub] (t_ub ==
    // win_ub == the integration horizon). Each slice's `gate_state` is the
    // trajectory clipped to that window; for a pinned time (win_lb == t_ub) the
    // terminal slice's gate_state collapses to the point x(t_ub).
    CapdTubeResult run_capd_fwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double win_lb,
        double t_ub,
        const CapdSolverParams& params,
        const NearestRounding& nr);

    // Run CAPD backward integration.
    //
    // Integrates the cached -f(x) IMap from u0 = Xt_bounds (in the caller's
    // swapped frame, this is original X_t) over [0, t_ub] and returns the
    // time-ordered backward-image slices — each the set of states whose forward
    // trajectory under f(x) reaches X_t at that reverse-time slice. The caller
    // intersects with the current m_vars_t bounds (= original X_0). Symmetric
    // with run_capd_fwd (same per-slice tube, -f vs f).
    //
    // Soundness: any x_0 reaching some point of X_t at some t in [t_lb, t_ub]
    // under forward dynamics lies in some returned slice; removing points the
    // surviving-slice hull excludes is sound by construction.
    CapdTubeResult run_capd_bwd(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& Xt_bounds,
        const std::vector<std::pair<double, double>>& par_bounds,
        double win_lb,
        double t_ub,
        const CapdSolverParams& params,
        const NearestRounding& nr);

    // -------------------------------------------------------------------------
    // Trace generation (for `dreal4 --visualize`).
    // -------------------------------------------------------------------------

    // One slice of the integrated trajectory. The slice spans real time
    // [t_lb, t_ub] and var_enclosures[i] is the over-approximating box of
    // state variable i over that slice.
    struct CapdTracePoint {
        double t_lb;
        double t_ub;
        std::vector<std::pair<double, double>> var_enclosures;
    };

    struct CapdTraceResult {
        std::vector<CapdTracePoint> points;
        bool succeeded{false};
    };

    // Integrate the cached flow from `u0` over [0, t_ub] and record an
    // enclosure for each of n_steps equally-spaced sub-slices. Mirrors the
    // JSON shape of the now-deleted Codac run_lohner_trace() so the
    // visualizer's input contract is preserved.
    //
    // forward = true  → integrate f(x)  (cache->fn_fwd)
    // forward = false → integrate -f(x) (cache->fn_bwd) for reverse-time view
    //
    // Throws std::logic_error on a call the caller must not make: a null cache,
    // no state variables, t_ub or n_steps not positive, or u0 not sized to the
    // state. Returns succeeded=false if the integrator fails before reaching
    // t_ub. The points recorded up to the failure (possibly none) are still
    // populated so partial traces remain visualizable.
    CapdTraceResult run_capd_trace(
        const std::shared_ptr<CapdOdeCache>& cache,
        const std::vector<std::pair<double, double>>& u0,
        const std::vector<std::pair<double, double>>& par_bounds,
        double t_ub,
        bool forward,
        const CapdSolverParams& params,
        const NearestRounding& nr,
        int n_steps = 50);

} // namespace dreal
