//
// Created by Kunal Sheth on 12/24/25.
//

#include "DeBruijnCanonicalizer.h"

#include "dreal/version.h"
#include "dreal/smt2/term.h"
#include "dreal/symbolic/symbolic_expression_cell.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"
#include "dreal/util/assert.h"
#include "dreal/util/exception.h"
#include "dreal/util/logging.h"
#include <functional>

namespace dreal
{
#define RECURSE(sub) VisitExpression<void>(this, (sub), canon_var_seq);
#define RECURSEF(sub) VisitFormula<void>(this, (sub), canon_var_seq);

    template <typename T>
    void DeBruijnCanonicalizer<T>::insert_unary(const Expression& e, std::vector<Variable>& canon_var_seq) const {
        RECURSE(get_argument(e));
    }

    template <typename T>
    void DeBruijnCanonicalizer<T>::insert_binary(const Expression& e, std::vector<Variable>& canon_var_seq) const {
        RECURSE(get_first_argument(e));
        RECURSE(get_second_argument(e));
    }

    template <typename T>
    void DeBruijnCanonicalizer<T>::insert_binary(const Formula& f, std::vector<Variable>& canon_var_seq) const {
        RECURSE(get_lhs_expression(f));
        RECURSE(get_rhs_expression(f));
    }

    template <typename T>
    void DeBruijnCanonicalizer<T>::insert_nary(const Formula& f, std::vector<Variable>& canon_var_seq) const {
        const auto& ops = get_operands(f);
        using OSet = FormulaSet;
        std::vector<OSet::const_iterator> order;
        order.reserve(ops.size());
        for (/*copy*/ auto it = ops.cbegin(); it != ops.cend(); ++it) order.emplace_back(it);
        std::sort(order.begin(), order.end(),
                  [](const auto& A, const auto& B) { return A->get_al_hash() < B->get_al_hash(); });
        for (const auto& it : order)
            RECURSEF(*it);
    }

    void add_variable_to_seq(const Variable& v, std::vector<Variable>& canon_var_seq) {
        // if (std::find(canon_var_seq.cbegin(), canon_var_seq.cend(), v) == canon_var_seq.cend())
        canon_var_seq.emplace_back(v);
    }

    template <typename T>
    bool DeBruijnCanonicalizer<T>::is_bound(const Variable& v) const {
        for (const auto& b : bound_scope_)
            if (b.equal_to(v)) return true;
        return false;
    }

#define INSERT_DECL(name) \
template <typename T> \
void DeBruijnCanonicalizer<T>::name ( \
    const Expression &e, \
    std::vector<Variable>& canon_var_seq \
) const

    INSERT_DECL(VisitVariable) {
        const Variable& v = get_variable(e);
        if (is_bound(v)) return;  // bound (universal) variable — excluded from canonical sequence
        add_variable_to_seq(v, canon_var_seq);
    }

    INSERT_DECL(VisitConstant) { DREAL_ASSERT(e.GetVariables().empty()); }
    INSERT_DECL(VisitRealConstant) { DREAL_ASSERT(e.GetVariables().empty()); }

    INSERT_DECL(VisitAddition) {
        const auto& a = to_addition(e);
        using ECMap = ExpressionMap<double>;
        const ECMap& m = a->get_expr_to_coeff_map();

        std::vector<ECMap::const_iterator> order;
        order.reserve(m.size());
        for (/*copy*/auto it = m.cbegin(); it != m.cend(); ++it) order.emplace_back(it);
        std::sort(order.begin(), order.end(),
                  [](const auto& A, const auto& B) { return A->first.get_al_hash() < B->first.get_al_hash(); });

        for (const auto& it : order) {
            const auto& [expr, coeff] = *it;
            RECURSE(expr);
            // RECURSE(coeff); // double.
        }
    }

    INSERT_DECL(VisitMultiplication) {
        const auto& a = to_multiplication(e);
        using BEMap = ExpressionMap<Expression>;
        const BEMap& m = a->get_base_to_exponent_map();

        std::vector<BEMap::const_iterator> order;
        order.reserve(m.size());
        for (/*copy*/ auto it = m.cbegin(); it != m.cend(); ++it) order.emplace_back(it);
        std::sort(order.begin(), order.end(),
                  [](const auto& A, const auto& B) { return A->first.get_al_hash() < B->first.get_al_hash(); });

        for (const auto& it : order) {
            const auto& [expr, coeff] = *it;
            RECURSE(expr);
            RECURSE(coeff);
        }
    }

    INSERT_DECL(VisitDivision) { insert_binary(e, canon_var_seq); }
    INSERT_DECL(VisitLog) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitAbs) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitExp) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitSqrt) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitPow) { insert_binary(e, canon_var_seq); }
    INSERT_DECL(VisitSin) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitCos) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitTan) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitAsin) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitAcos) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitAtan) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitAtan2) { insert_binary(e, canon_var_seq); }
    INSERT_DECL(VisitSinh) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitCosh) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitTanh) { insert_unary(e, canon_var_seq); }
    INSERT_DECL(VisitMin) { insert_binary(e, canon_var_seq); }
    INSERT_DECL(VisitMax) { insert_binary(e, canon_var_seq); }
    INSERT_DECL(VisitIfThenElse) {
        const auto& ite = to_if_then_else(e);
        RECURSEF(ite->get_conditional_formula());
        RECURSE(ite->get_then_expression());
        RECURSE(ite->get_else_expression());
    }

    INSERT_DECL(VisitUninterpretedFunction) {
        throw DREAL_RUNTIME_ERROR("DeBruin Canonicalization not implemented for uninterpreted functions yet.");
    }

#undef INSERT_DECL
#define INSERT_DECL(name) \
template <typename T> \
void DeBruijnCanonicalizer<T>::name ( \
    const Formula &f, \
    std::vector<Variable>& canon_var_seq \
) const

    INSERT_DECL(VisitFalse) { DREAL_ASSERT(f.GetFreeVariables().empty()); }
    INSERT_DECL(VisitTrue) { DREAL_ASSERT(f.GetFreeVariables().empty()); }

    INSERT_DECL(VisitVariable) {
        const Variable& v = get_variable(f);
        if (is_bound(v)) return;  // bound (universal) variable — excluded from canonical sequence
        add_variable_to_seq(v, canon_var_seq);
    }

    INSERT_DECL(VisitEqualTo) { insert_binary(f, canon_var_seq); }
    INSERT_DECL(VisitNotEqualTo) { insert_binary(f, canon_var_seq); }
    INSERT_DECL(VisitGreaterThan) { insert_binary(f, canon_var_seq); }
    INSERT_DECL(VisitGreaterThanOrEqualTo) { insert_binary(f, canon_var_seq); }
    INSERT_DECL(VisitLessThan) { insert_binary(f, canon_var_seq); }
    INSERT_DECL(VisitLessThanOrEqualTo) { insert_binary(f, canon_var_seq); }
    INSERT_DECL(VisitConjunction) { insert_nary(f, canon_var_seq); }
    INSERT_DECL(VisitDisjunction) { insert_nary(f, canon_var_seq); }
    INSERT_DECL(VisitNegation) { RECURSEF(get_operand(f)); }

    INSERT_DECL(VisitForall) {
        const auto& a = to_forall(f);
        // Bound (universal) variables are not free solver variables. Push them onto the bound
        // scope so `VisitVariable` skips their occurrences in the body — they must never enter
        // the canonical variable sequence / `concrete_vars`, or they reach the `Box` domain
        // check on a variable that has no Box entry (corrupts the Box's shared var->idx map).
        // The free (existential) variables of the body still flow through normally and are
        // matched by the usual domain-preserving bijection.
        const size_t scope_base = bound_scope_.size();
        for (const auto& v : a->get_quantified_variables())
            bound_scope_.emplace_back(v);
        RECURSEF(a->get_quantified_formula());
        bound_scope_.resize(scope_base);
    }

    INSERT_DECL(VisitForallT) {
        const auto& a = to_forallT(f);
        RECURSE(a->get_lb());
        RECURSE(a->get_ub());
        RECURSEF(a->get_bound_f());
    }

    INSERT_DECL(VisitIntegral) {
        const auto& i = to_integral(f);
        RECURSE(i->get_time_0());
        RECURSE(i->get_time_t());
        for (const auto& v0 : i->get_vec_0())
            RECURSE(v0);
        for (const auto& vt : i->get_vec_t())
            RECURSE(vt);
    }

#undef INSERT_DECL


    const Variables& GetVars(const Formula& f) { return f.GetFreeVariables(); }
    const Variables& GetVars(const Expression& e) { return e.GetVariables(); }

    void Visit(const DeBruijnCanonicalizer<Expression>* const v, const Expression& e, std::vector<Variable>& cvs) { return VisitExpression<void>(v, e, cvs); }
    void Visit(const DeBruijnCanonicalizer<Formula>* const v, const Formula& f, std::vector<Variable>& cvs) { return VisitFormula<void>(v, f, cvs); }

    template <typename T>
    std::tuple<T, typename DeBruijnCanonicalizer<T>::DeBruijnIndices, std::vector<Variable>> DeBruijnCanonicalizer<T>::canonicalize_atom(const T& atom) const {
        bound_scope_.clear();  // defensive: balanced push/pop leaves it empty, but a prior throw may not
        std::vector<Variable> canonical_variable_sequence;
        canonical_variable_sequence.reserve(GetVars(atom).size() * 2);

        Visit(this, atom, canonical_variable_sequence);

        DeBruijnIndices indices;
        std::vector<Variable> concrete_vars;
        indices.reserve(canonical_variable_sequence.size());
        concrete_vars.reserve(GetVars(atom).size());

        Variable::Id next_dummy_idx = 0;

        std::unordered_map<Variable, Variable> as;
        FormulaSubstitution fs;
        ExpressionSubstitution es;
        as.reserve(GetVars(atom).size());
        fs.reserve(GetVars(atom).size());
        es.reserve(GetVars(atom).size());

        for (const auto& v : canonical_variable_sequence) {
            Variable dummy;
            const auto it = as.find(v);
            if (it == as.cend()) {
                dummy = Variable(--next_dummy_idx, v.get_type());
                as.emplace(v, dummy);
                if (v.get_type() == Variable::Type::BOOLEAN) fs.emplace(v, dummy);
                else es.emplace(v, dummy);
                concrete_vars.emplace_back(v);
                DREAL_ASSERT(concrete_vars.size() == -dummy.get_id());
            }
            else dummy = it->second;
            indices.emplace_back(-dummy.get_id() - 1);
        }

        const auto structure = atom.Substitute(es, fs);
        return {std::move(structure), std::move(indices), std::move(concrete_vars)};
    }

    template <typename T>
    void DeBruijnCanonicalizer<T>::insert(const T& atom) {
        if (canonicalization_cache.count(atom) > 0) return;

        auto [structure, indices, concrete_vars] = canonicalize_atom(atom);
        // const auto [it, _] = structure_to_indices_to_concrete.try_emplace(std::move(structure));
        const auto [it, _] = structure_to_concrete.try_emplace(std::move(structure));
        const auto& canon_structure = it->first; // canonicalizes underlying FormulaCell ptr if it already existed in the map.

        // DeBruijnEquivalenceClass& concretes = it->second[indices];
        DeBruijnEquivalenceClass& concretes = it->second;
        // DREAL_ASSERT(it->second.size() == 1); // We shouldn't need this middle layer... indices should be baked into the dummy variable names. odot: remove.

        concretes.insert(concrete_vars, atom);
        canonicalization_cache.try_emplace(atom, canon_structure, std::move(indices), std::move(concrete_vars));
    }

    template <typename T>
    void DeBruijnCanonicalizer<T>::find_matches(
        const T& atom, substitutions_map& subs, const matches_vec& matches, const misses_vec& misses, uint64_t &random_state
    ) const {
        const auto init_size = subs.size();

        const auto cache_it = canonicalization_cache.find(atom);
        const auto& [structure, indices, concrete_vars] = (
            cache_it == canonicalization_cache.cend() ? canonicalize_atom(atom) : cache_it->second
        );

        const auto concrete_it = structure_to_concrete.find(structure);
        if (concrete_it == structure_to_concrete.cend()) return misses(substitutions_map::STRUCTURE_MISS);
        const auto& concrete = concrete_it->second;

        concrete.find_matches(concrete_vars, subs, matches, misses, random_state);

        DREAL_ASSERT(subs.size() == init_size);
        return;
    }

    template <typename T>
    std::pair<std::vector<std::pair<T, std::optional<substitutions_map>>>, matching_stats_t> DeBruijnCanonicalizer<T>::find_matches(
        const T& f, const Box& box, const bool return_subs_maps, uint64_t &random_state
    ) const {
        substitutions_map s(box, GetVars(f).size());

        matching_stats_t stats = {0};
        std::vector<std::pair<T, std::optional<substitutions_map>>> match_vec;

        find_matches(
            f, s,
            [&](const auto& m, const auto& s) -> auto {
                ++stats.matches;
                if (return_subs_maps) match_vec.emplace_back(m, s);
                else match_vec.emplace_back(m, std::nullopt);
            },
            [&](const auto& reason) -> auto { ++stats.misses_bc.at(reason); },
            random_state
        );

        return {std::move(match_vec), std::move(stats)};
    }

    template <typename T>
    std::pair<std::vector<std::pair<std::vector<T>, std::optional<substitutions_map>>>, matching_stats_t> DeBruijnCanonicalizer<T>::find_matches(
        const std::vector<T>& literals, const Box& box, const bool return_subs_maps, const std::chrono::duration<uint64_t, std::micro> timeout
    ) const {
        const auto start_time = std::chrono::steady_clock::now();
        uint64_t random_state = timeout.count() == -1 ? 0 : start_time.time_since_epoch().count();

        matching_stats_t stats = {0};
        std::vector<T> matches_vec;
        std::vector<std::pair<std::vector<T>, std::optional<substitutions_map>>> result;
        matches_vec.reserve(literals.size());

        const misses_vec misses = [&](const auto& reason) { ++stats.misses_bc.at(reason); };

        bool did_time_out = false;

        const auto ibegin = literals.begin();
        const auto iend = literals.end();
        // std::unordered_set<Formula> seen_truncateds; // lexo-compare for nary goes one by one...
        // seen_truncateds.reserve(1024 * literals.size());
        std::function<
            std::function<void(const T& f, substitutions_map& s)> (typeof(ibegin))
        > match_next_literal = [&](const auto& it1) {
            return [&, /*copy*/ it1](const auto& f, auto& s2) {
                if (did_time_out || std::chrono::steady_clock::now() - start_time > timeout) {
                    did_time_out = true;
                    return;
                }

                matches_vec.emplace_back(f);

                // avoid re-finding 1000s of permutations of the same clause on fedor_13.smt2, etc.
                // copy required. do NOT modify matches_vec... that needs to be a pure stack
                // FormulaSet matches_vec_set(matches_vec.begin(), matches_vec.end());
                // const auto [_, successful_emplace] = seen_truncateds.emplace(
                // make_conjunction_SKIP_CHECKS_KUNAL_HACK(std::move(matches_vec_set))
                // );
                // if (!successful_emplace) {
                // matches_vec.pop_back();
                // return;
                // }

                auto it2 = it1;
                ++it2;
                // stats.partial_matches++;
                if (it2 == iend) {
                    DREAL_ASSERT(matches_vec.size() == literals.size());
                    if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
                        for (int i = 0; i < literals.size(); ++i) {
                            const auto& subs_lit = substitutions_map::apply_substitution(matches_vec[i], s2, false);
                            DREAL_ASSERT(literals[i].EqualTo(subs_lit));
                        }
                    }
                    ++stats.matches;
                    if (return_subs_maps) result.emplace_back(matches_vec, s2);
                    else result.emplace_back(matches_vec, std::nullopt);
                }
                else find_matches(*it2, s2, match_next_literal(it2), misses, random_state);
                matches_vec.pop_back();
            };
        };

        size_t sub_preallocations = 0;
        for (const auto& literal : literals) sub_preallocations += GetVars(literal).size();
        substitutions_map substitutions(box, sub_preallocations);
        find_matches(*ibegin, substitutions, match_next_literal(ibegin), misses, random_state);

        DREAL_ASSERT(result.size() == stats.matches);
        if (did_time_out)
            DREAL_LOG_INFO("Pattern matching timed out after {} usec.", timeout.count());
        return {result, stats};
    }

    template <typename T>
    DeBruijnCanonicalizer<T>::DeBruijnCanonicalizer() {
        canonicalization_cache.max_load_factor(0.25);
        // structure_to_indices_to_concrete.max_load_factor(0.25);
        structure_to_concrete.max_load_factor(0.25);
    }

    // Force template code generation into this translation unit.
    template class DeBruijnCanonicalizer<Expression>;
    template class DeBruijnCanonicalizer<Formula>;
}
