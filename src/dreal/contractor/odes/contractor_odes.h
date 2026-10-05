//
// Created by Kunal Sheth on 9/2/25.
// Post-Codac elimination: CAPD is the sole ODE backend.
//

#ifndef DREAL4_CMAKE_CONTRACTOR_ODES_H
#define DREAL4_CMAKE_CONTRACTOR_ODES_H
#include <iosfwd>
#include <memory>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "dreal/contractor/odes/ode_types.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"
#include "dreal/contractor/contractor_cell.h"
#include "dreal/symbolic/hash.h"
#include "dreal/util/box.h"
#include "nlohmann/json.hpp"

namespace dreal
{
    class CapdOdeCache;   // opaque; defined in contractor_odes_capd.cc

    std::ostream& operator<<(std::ostream& out, ode_direction const& d);

    // Parse the BMC step index out of an ODE state-variable name, for the
    // --visualize trace's per-segment `step` metadata. Two unroller naming
    // conventions are supported:
    //   dReach:      <base>_<step>_{0,t}   (e.g. height_3_t -> 3)
    //   SMT-LIB BMC: <base>_k<step>        (e.g. x_decay_IntX_k0 -> 0)
    // Returns 0 when neither shape is present (also the legitimate step-0 value).
    unsigned int ode_step_from_name(const std::string& name);

    // ODE contractor using IBEX interval arithmetic + CAPD order-20 Taylor.
    //
    // Prune() runs the following steps in order:
    //   1. Parameter consistency: pars_0 ∩ pars_t (parameters are constant
    //      along the trajectory and must agree at t=0 and t=T).
    //   2. T=0 special case: X_0 ∩ X_t (initial and final states must agree
    //      when time horizon is zero).
    //   3. Trivial-flow short-circuit: if every RHS is the literal 0, just
    //      intersect X_0 ∩ X_t (no integration needed — variables are
    //      constant along the trajectory).
    //   4. ODE trajectory integration via CAPD's IOdeSolver (order 20) +
    //      ITimeMap (backward via the negated -f(x) map), then cav26's
    //      per-slice tube filter: the ForallT invariant is checked on EACH
    //      trajectory slice (not just the endpoint — interior violations would
    //      otherwise be missed), each terminal-eligible slice is intersected
    //      with the X_t gate, survivors are hulled to narrow X_t and time, and
    //      an empty survivor set refutes (set_empty). See contractor_odes.cc.
    //
    // Sound for ODE problems: CAPD's order-20 Taylor enclosure provides a
    // guaranteed over-approximation of all trajectories from X_0.
    class contractor_ode_lohner : public ContractorCell
    {
    public:
        contractor_ode_lohner(Box const& box, const ode_constraint& ctr,
                              ode_direction dir, Config const& config,
                              double timeout = 0.0);
        std::ostream& display(std::ostream& out) const override;

        // Generate a JSON trace of the ODE trajectory for visualization
        // (the `--visualize` flag). Uses CAPD step-by-step IOdeSolver
        // (one enclosure per slice). Raises when the box has no trajectory to
        // draw: disjoint parameter values, or a time window ending below 0.
        nlohmann::json generate_trace(ContractorStatus cs_copy);

        void Prune(ContractorStatus* cs, const UpwardRounding& ur) const override;

    private:
        ode_direction const m_dir;
        const ode_constraint m_ctr;
        double const m_timeout; // unit: msec; 0 = no timeout
        // State and parameter variables (direction-adjusted: m_vars_0 is
        // always "start of integration", m_vars_t is always "end").
        std::vector<Variable> m_vars_0;
        std::vector<Variable> m_vars_t;
        std::vector<Variable> m_pars_0;
        std::vector<Variable> m_pars_t;
        bool m_need_to_check_inv{false};
        std::vector<Contractor> m_inv_ctcs;
        // CAPD integration knobs for THIS contractor instance (resolved from
        // Config in the constructor; the Taylor order is direction-specific).
        CapdSolverParams m_capd_params;
        // ODE state variables in flow.ode_list order (positional match with
        // m_vars_0/m_vars_t). Precomputed in the constructor so Prune() doesn't
        // rebuild it on every call.
        std::vector<Variable> m_ode_state_vars;
        // Cached capd::IMap (fwd + bwd via -f(x)) for this flow. Never null: the
        // constructor raises when the flow cannot be translated or no map comes
        // back.
        std::shared_ptr<CapdOdeCache> m_capd_cache;
    };

    std::vector<Formula> unroll_conjunctions(const Formula& f);

    // The ONE canonical ForallT↔Integral link test, shared by
    // link_integral_invariants (DPLL(T)-loop pairing) and the check-sat-time
    // unlinked-forall_t rejection (context_impl.cc RejectUnlinkedForallT):
    // fc attaches to ic iff they name the same flow AND every invariant
    // variable is one of the integral's endpoint (vec_t) variables — i.e. the
    // invariant must be written over x_t, not the flow-template var x.
    inline bool forallt_links_to_integral(const Formula& fc, const Formula& ic) {
        const auto* const fcc = to_forallT(fc);
        const auto* const icc = to_integral(ic);
        if (fcc->get_flow()->name != icc->get_flow()->name) return false;
        Variables vars_t_in_ic;
        for (const auto& vec_t : icc->get_vec_t()) vars_t_in_ic.insert(vec_t.GetVariables());
        return vars_t_in_ic.IsSupersetOf(fcc->get_bound_vars());
    }

    // Given a collection of assertions, finds all Integral formulas and links
    // each with its associated ForallT invariants (matching by flow name and
    // variable scope). Returns a vector of (Integral, [ForallT]) pairs.
    //
    // A ForallT that links to NO integral is silently omitted here — REQUIRED
    // in this DPLL(T)-loop context, where the assertion set is the SAT
    // solver's transient literal subset and a forall_t legitimately appears
    // without its companion integral (another mode/step active). A forall_t
    // that is unlinkable against the FULL problem is instead rejected up
    // front by RejectUnlinkedForallT (context_impl.cc).
    template <typename FormulaCollection>
    std::vector<ode_constraint> link_integral_invariants(const FormulaCollection& assertions) {
        std::vector<std::pair<Formula, std::vector<Formula>>> result;

        std::vector<Formula> int_ctrs;
        std::vector<Formula> inv_ctrs;
        for (const auto& a : assertions) {
            for (const auto& f : unroll_conjunctions(a)) {
                if (is_integral(f)) int_ctrs.push_back(f);
                else if (is_forallT(f)) inv_ctrs.push_back(f);
                else if (is_negation(f) && f.include_ode()) {
                    DREAL_LOG_DEBUG("Inverted ODE constraints are currently ignored: {}", f);
                }
                else if (f.include_ode()) {
                    DREAL_LOG_DEBUG("Nested ODE constraint currently ignored: {}", f);
                }
            }
        }

        for (const auto& ic : int_ctrs) {
            std::vector<Formula> local_invs;
            for (const auto& fc : inv_ctrs) {
                if (forallt_links_to_integral(fc, ic)) local_invs.push_back(fc);
            }
            result.emplace_back(ic, local_invs);
        }
        return result;
    }
}

#endif //DREAL4_CMAKE_CONTRACTOR_ODES_H
