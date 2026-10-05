//
// Created by Kunal Sheth on 8/20/25.
//

#include "symbolic_odes.h"
#include "symbolic_odes_cell.h"

#include "dreal/symbolic/symbolic_formula_cell.h"

#include <algorithm>
#include <cassert>
#include <set>
#include <sstream>
#include <stdexcept>

#include "OdeFlow.h"
#include "dreal/symbolic/hash.h"
#include "dreal/symbolic/symbolic_environment.h"
#include "dreal/symbolic/symbolic_expression.h"
#include "dreal/symbolic/symbolic_formula.h"
#include "dreal/symbolic/symbolic_variable.h"
#include "dreal/symbolic/symbolic_variables.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include "dreal/util/naive_cnfizer.h"

namespace dreal::drake::symbolic
{
    using std::all_of;
    using std::any_of;
    using std::equal;
    using std::hash;
    using std::lexicographical_compare;
    using std::ostream;
    using std::ostringstream;
    using std::runtime_error;
    using std::vector;
    using std::string;

    FormulaSet flatten_nested_boolean_structures(Formula f, bool inverted=false) {
        if (f.get_kind() == FormulaKind::True) {
            return {};
        }
        else if (f.get_kind() == FormulaKind::False) {
            DREAL_LOG_CRITICAL("false is not a valid invariant (forall_t constraint)");
            throw DREAL_RUNTIME_ERROR("false is not a valid invariant (forall_t constraint)");
        }
        else if (f.get_kind() == FormulaKind::Not) {
            return flatten_nested_boolean_structures(get_operand(f), !inverted);
        }
        else if (
            (f.get_kind() == FormulaKind::And && !inverted) ||
            (f.get_kind() == FormulaKind::Or && inverted)
        ) {
            FormulaSet ret;
            for (const auto& op : get_operands(f)) {
                auto const nlctrs = flatten_nested_boolean_structures(op, inverted);
                ret.insert(nlctrs.begin(), nlctrs.end());
            }
            return ret;
        }
        else if (
            (f.get_kind() == FormulaKind::And && inverted) ||
            (f.get_kind() == FormulaKind::Or && !inverted)
        ) {
            DREAL_LOG_CRITICAL("or is not a valid invariant for now, (forall_t constraint {})", f);
            throw DREAL_RUNTIME_ERROR("or is not a valid invariant for now, (forall_t constraint)");
        }
        else return {inverted ? !f : f};
    }

    FormulaForallT::FormulaForallT(const std::shared_ptr<const OdeFlow>& flow, const Expression& lb, const Expression& ub, const Formula& bound_f)
        : FormulaCell{
              FormulaKind::ForallT,
              hash_combine(bound_f.get_hash(), flow, lb.get_hash(), ub.get_hash()),
              hash_combine(bound_f.get_al_hash(), flow, lb.get_al_hash(), ub.get_al_hash()),
              lb.include_ite() || ub.include_ite() || bound_f.include_ite(),
              true,
              // PRIOR THINKING: variables in bound_f are bound, not free
              // lb.GetVariables() + ub.GetVariables() // variables in bound_f are bound, not free
              // NEW: see UPDATE comment in `ADD_DECL(VisitForallT)` of `pattern_matching_trie_form.cc`
              lb.GetVariables() + ub.GetVariables() + bound_f.GetFreeVariables()
          },
          flow_{flow},
          lb_{lb},
          ub_{ub},
          bound_f_{make_conjunction(flatten_nested_boolean_structures(bound_f, false))} {
        // the variable names are instantiated... e.g. `v_0_t`
        // can only be viewed/assigned in the context of an integral vec_t
        // for (const auto& var : get_bound_vars()) {
        // DREAL_ASSERT(flow->is_var(var) xor flow->is_par(var));
        // }
    }

    bool FormulaForallT::EqualTo(const FormulaCell& f) const {
        // Formula::EqualTo guarantees the following assertion.
        assert(get_kind() == f.get_kind());
        const FormulaForallT& f_forallT{static_cast<const FormulaForallT&>(f)};
        return (
            *flow_ == *f_forallT.flow_ &&
            lb_.EqualTo(f_forallT.lb_) &&
            ub_.EqualTo(f_forallT.ub_) &&
            bound_f_.EqualTo(f_forallT.bound_f_)
        );
    }

    bool FormulaForallT::Less(const FormulaCell& f) const {
        // Formula::Less guarantees the following assertion.
        assert(get_kind() == f.get_kind());
        const FormulaForallT& f_forallT{static_cast<const FormulaForallT&>(f)};
        if (flow_->name < f_forallT.flow_->name) return true; // todo: deeper comparison / handle scoping.
        if (f_forallT.flow_->name < flow_->name) return false;
        if (lb_.Less(f_forallT.lb_)) return true;
        if (f_forallT.lb_.Less(lb_)) return false;
        if (ub_.Less(f_forallT.ub_)) return true;
        if (f_forallT.ub_.Less(ub_)) return false;
        return bound_f_.Less(f_forallT.bound_f_);
    }

    bool FormulaForallT::Evaluate(const Environment&) const {
        throw runtime_error("not implemented yet");
    }

    Formula FormulaForallT::Substitute(const ExpressionSubstitution& expr_subst,
                                       const FormulaSubstitution& formula_subst) {
        // PRIOR THINKING: variables in bound_f are bound, not free
        // // Quantified variables are already bound and should not be substituted by s.
        // for (const auto& bvar : bound_f_.GetFreeVariables()) {
        //     DREAL_ASSERT(expr_subst.count(bvar) == 0);
        //     DREAL_ASSERT(formula_subst.count(bvar) == 0);
        // }

        // NEW: see UPDATE comment in `ADD_DECL(VisitForallT)` of `pattern_matching_trie_form.cc`

        const auto new_lb = lb_.Substitute(expr_subst, formula_subst);
        const auto new_ub = ub_.Substitute(expr_subst, formula_subst);
        const auto new_bf = bound_f_.Substitute(expr_subst, formula_subst);

        if (lb_.EqualTo(new_lb) && ub_.EqualTo(new_ub) && bound_f_.EqualTo(new_bf)) return GetFormula();
        else return forallT(flow_, new_lb, new_ub, new_bf);
    }

    ostream& FormulaForallT::Display(ostream& os) const {
        return os << "forallT(" << flow_->name << ", from t=" << lb_ << " to " << ub_ << " : " << bound_f_ << ")";
    }

    Formula forallT(const std::shared_ptr<const OdeFlow>& flow, const Expression& lb, const Expression& ub, const Formula& f) {
        return Formula{new FormulaForallT(flow, lb, ub, f)};
    }

    bool is_forallT(const Formula& f) { return is_forallT(*f.ptr_); }
    bool is_forallT(const FormulaCell& f) { return f.get_kind() == FormulaKind::ForallT; }

    const FormulaForallT* to_forallT(const FormulaCell* f_ptr) {
        assert(is_forallT(*f_ptr));
        return static_cast<const FormulaForallT*>(f_ptr);
    }

    const FormulaForallT* to_forallT(const Formula& f) { return to_forallT(f.ptr_); }

    ////////////////////////////////////////////////////////////////////////////////

    size_t alpha_hash_vec(const vector<Expression>& vec) {
        size_t seed{};
        for (const auto& v : vec)
            seed = hash_combine(seed, v.get_al_hash());
        return seed;
    }

    Variables extract_variables(const Expression& time_0, const Expression& time_t,
                                const std::vector<Expression>& vec_0, const std::vector<Expression>& vec_t) {
        Variables ret;
        ret.insert(time_0.GetVariables());
        ret.insert(time_t.GetVariables());
        for (const auto& v : vec_0)
            ret.insert(v.GetVariables());
        for (const auto& v : vec_t)
            ret.insert(v.GetVariables());
        return ret;
    }

    FormulaIntegral::FormulaIntegral(
        const Expression& time_0, const Expression& time_t,
        std::vector<Expression> vec_0, std::vector<Expression> vec_t,
        const std::shared_ptr<const OdeFlow>& flow)
        : FormulaCell{
              FormulaKind::Integral,
              hash_combine(
                  hash_value<vector<Expression>>{}(vec_0),
                  hash_value<vector<Expression>>{}(vec_t),
                  time_0, time_t, flow
              ),
              hash_combine(
                  alpha_hash_vec(vec_0),
                  alpha_hash_vec(vec_t),
                  time_0.get_al_hash(), time_t.get_al_hash(), flow->name
              ),
              false, true, extract_variables(time_0, time_t, vec_0, vec_t)
          },
          time_0_{time_0},
          time_t_{time_t},
          vec_0_{std::move(vec_0)},
          vec_t_{std::move(vec_t)},
          flow_{flow} {
        // The ODE contractor reads a time bound only as a variable or a
        // constant, and the start and end vectors only as variables; anything
        // else would be skipped on every Prune, or read as a variable it is not
        // (get_variable on a non-variable is a bare static_cast). Always on.
        for (const Expression* t : {&time_0_, &time_t_}) {
            if (!(is_variable(*t) || is_constant(*t) || is_real_constant(*t)))
                throw DREAL_RUNTIME_ERROR(
                    "integral: a time bound must be a variable or a constant, got {}", fmt::streamed(*t));
        }

        if (vec_0_.size() != vec_t_.size())
            throw DREAL_RUNTIME_ERROR("vec_0.size() != vec_t.size()");
        if (vec_0_.size() != flow->ode_list.size())
            throw DREAL_RUNTIME_ERROR("vec_0_.size() != flow->ode_list.size()");

        for (size_t i = 0; i < flow_->ode_list.size(); ++i) {
            if (!is_variable(vec_0_[i]) || !is_variable(vec_t_[i]))
                throw DREAL_RUNTIME_ERROR(
                    "integral: start and end entries must be variables, got {} and {}",
                    fmt::streamed(vec_0_[i]), fmt::streamed(vec_t_[i]));
            DREAL_ASSERT(flow_->is_var(flow->ode_list[i].first) xor flow_->is_par(flow->ode_list[i].first));
            if (flow_->is_par(flow->ode_list[i].first)) {
                pars_0_.emplace_back(get_variable(vec_0_[i]));
                pars_t_.emplace_back(get_variable(vec_t_[i]));
            }
            else /* flow_->is_var(flow->ode_list[i].first) */ {
                vars_0_.emplace_back(get_variable(vec_0_[i]));
                vars_t_.emplace_back(get_variable(vec_t_[i]));
            }
        }
    }

    bool FormulaIntegral::EqualTo(const FormulaCell& e) const {
        // Expression::EqualTo guarantees the following assertion.
        assert(get_kind() == e.get_kind());
        const FormulaIntegral& int_e{static_cast<const FormulaIntegral&>(e)};

        if (*flow_ != *int_e.flow_) return false;
        if (!time_0_.EqualTo(int_e.time_0_)) return false;
        if (!time_t_.EqualTo(int_e.time_t_)) return false;
        if (!equal( // checks length automatically
            vec_0_.cbegin(), vec_0_.cend(), int_e.vec_0_.cbegin(), int_e.vec_0_.cend(),
            [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); }))
            return false;
        if (!equal( // checks length automatically
            vec_t_.cbegin(), vec_t_.cend(), int_e.vec_t_.cbegin(), int_e.vec_t_.cend(),
            [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); }))
            return false;

        return true;
    }

    bool FormulaIntegral::Less(const FormulaCell& e) const {
        // Expression::Less guarantees the following assertion.
        assert(get_kind() == e.get_kind());
        const FormulaIntegral& int_e{static_cast<const FormulaIntegral&>(e)};

        if (flow_->name < int_e.flow_->name) return true; // todo: deeper comparison.
        if (int_e.flow_->name < flow_->name) return false;
        if (time_0_.Less(int_e.time_0_)) return true;
        if (int_e.time_0_.Less(time_0_)) return false;
        if (time_t_.Less(int_e.time_t_)) return true;
        if (int_e.time_t_.Less(time_t_)) return false;
        if (vec_0_.size() < int_e.vec_0_.size()) return true;
        if (int_e.vec_0_.size() < vec_0_.size()) return false;
        if (vec_t_.size() < int_e.vec_t_.size()) return true;
        if (int_e.vec_t_.size() < vec_t_.size()) return false;
        for (size_t i = 0; i < vec_0_.size(); ++i) {
            if (vec_0_[i].Less(int_e.vec_0_[i])) return true;
            if (int_e.vec_0_[i].Less(vec_0_[i])) return false;
        }
        for (size_t i = 0; i < vec_t_.size(); ++i) {
            if (vec_t_[i].Less(int_e.vec_t_[i])) return true;
            if (int_e.vec_t_[i].Less(vec_t_[i])) return false;
        }
        return false;
    }

    bool FormulaIntegral::Evaluate(const Environment& env) const {
        throw runtime_error("Not yet implemented.");
    }

    Formula FormulaIntegral::Substitute(
        const ExpressionSubstitution& expr_subst,
        const FormulaSubstitution& formula_subst) {
        const auto time_0_subst{time_0_.Substitute(expr_subst, formula_subst)};
        const auto time_t_subst{time_t_.Substitute(expr_subst, formula_subst)};

        std::vector<Expression> vec_0_subst(vec_0_.size());
        std::vector<Expression> vec_t_subst(vec_t_.size());
        for (size_t i = 0; i < vec_0_.size(); ++i) vec_0_subst[i] = vec_0_[i].Substitute(expr_subst, formula_subst);
        for (size_t i = 0; i < vec_t_.size(); ++i) vec_t_subst[i] = vec_t_[i].Substitute(expr_subst, formula_subst);

        const bool equals = (
            time_0_subst.EqualTo(time_0_) &&
            time_t_subst.EqualTo(time_t_) &&
            equal( // checks length automatically
                vec_0_.cbegin(), vec_0_.cend(), vec_0_subst.cbegin(), vec_0_subst.cend(),
                [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); }) &&
            equal( // checks length automatically
                vec_t_.cbegin(), vec_t_.cend(), vec_t_subst.cbegin(), vec_t_subst.cend(),
                [](const Expression& e1, const Expression& e2) { return e1.EqualTo(e2); })
        );

        if (!equals) return integral(time_0_subst, time_t_subst, vec_0_subst, vec_t_subst, flow_);
        else return GetFormula();
    }

    ostream& FormulaIntegral::Display(ostream& os) const {
        os << "(= [";
        for (size_t i = 0; i < vec_0_.size(); ++i) {
            if (i > 0) os << ", ";
            os << vec_0_[i];
        }
        os << "] (integral " << flow_->name << ", from t=" << time_0_ << " to " << time_t_ << ", ";
        os << "[";
        for (size_t i = 0; i < vec_t_.size(); ++i) {
            if (i > 0) os << ", ";
            os << vec_t_[i];
        }
        os << "]";
        return os << "))";
    }

    Formula integral(const Expression& time_0, const Expression& time_t,
                     const std::vector<Expression>& vec_0, const std::vector<Expression>& vec_t,
                     const std::shared_ptr<const OdeFlow>& flow) {
        return Formula{new FormulaIntegral(time_0, time_t, vec_0, vec_t, flow)};
    }

    bool is_integral(const Formula& e) { return is_integral(*e.ptr_); }
    bool is_integral(const FormulaCell& c) { return c.get_kind() == FormulaKind::Integral; }

    const FormulaIntegral* to_integral(const FormulaCell* const expr_ptr) {
        assert(is_integral(*expr_ptr));
        return static_cast<const FormulaIntegral*>(expr_ptr);
    }

    const FormulaIntegral* to_integral(const Formula& e) { return to_integral(e.ptr_); }
} // namespace dreal::drake::symbolic
