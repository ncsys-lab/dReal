//
// Created by Kunal Sheth on 2/19/25.
//

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/symbolic/symbolic_expression_cell.h>
#include <dreal/util/assert.h>
#include <dreal/util/logging.h>

#include "dreal/util/exception.h"

namespace dreal
{
#define VISIT_DECL(name) \
void PatternMatchingTrie::name ( \
    const Expression &_e, const ExprNode &parent, \
    substitutions_map &substitutions, \
    const e_matches_vec &matches, \
    const e_partial_matches_vec& partial_matches, \
    const e_misses_vec &misses \
) const
#define ADD_DECL(name) \
PatternMatchingTrie::ExprNode& PatternMatchingTrie::name (const Expression &e, ExprNode &parent, const std::optional<Expression> &is_terminal)

    ADD_DECL(VisitVariable) {
        // todo: dedupe?
        return parent.c_like(e).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitVariable) {
        const auto& e = get_variable(_e);
        for (const auto& node : parent.c_like(_e)) {
            substitutions.push();

            const auto& matched_e = get_variable(*node.leaf);
            if (
                substitutions_map::substitution_status reason;
                (reason = substitutions.attempt_substitution(matched_e, e))
                != substitutions_map::SUCCESS
            )
                // type check failed
                // or match already substituted for something else, stop.
                misses(reason);

            else if (!node.terminal_expression.has_value())
                // partial match, keep going!
                partial_matches(node, substitutions);

            else
                // terminal match! BINGO!
                matches(*node.terminal_expression, substitutions);

            substitutions.pop();
        }
    }

    ADD_DECL(VisitConstant) {
        return parent.c_like(e).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitConstant) {
        const auto e = get_constant_value(_e);
        for (const auto& node : parent.c_like(_e)) {
            const auto& matched_e = get_constant_value(*node.leaf);
            if (e != matched_e)
                misses(substitutions_map::CONST_MISS);
            else if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }

    ADD_DECL(VisitRealConstant) {
        return parent.c_like(e).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitRealConstant) {
        const auto *const e = to_real_constant(_e);
        for (const auto& node : parent.c_like(_e)) {
            const auto *const matched_e = to_real_constant(*node.leaf);
            if (!e->EqualTo(*matched_e) /* confirmed non-recursive, simple one-liner */)
                misses(substitutions_map::CONST_MISS);
            else if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }

    ADD_DECL(VisitAddition) {
        auto& n1 = parent.c_like(e).emplace_back(e);
        ExprNode *stateCoeff = &n1, *stateExpr = nullptr;
        std::multimap<double, Expression> multimap; // canonicalize
        for (const auto& [expr, coeff] : to_addition(e)->get_expr_to_coeff_map()) {
            multimap.emplace(coeff, expr);
        }
        size_t i = 0;
        for (const auto& [coeff, expr] : multimap) {
            stateExpr = &recAddExpr(coeff, *stateCoeff, {});
            stateCoeff = &recAddExpr(
                expr, *stateExpr,
                i++ == multimap.size() - 1 ? is_terminal : std::optional<Expression>{}
            );
        }
        return *stateCoeff;
    }

    VISIT_DECL(VisitAddition) {
        const auto *const e = to_addition(_e);
        for (const auto& n1 : parent.c_like(_e)) {
            const auto *const m = to_addition(*n1.leaf);
            if (e->get_constant() != m->get_constant()) continue;
            if (e->get_expr_to_coeff_map().size() != m->get_expr_to_coeff_map().size()) continue;

            std::multimap<double, Expression> e_canon_coeff_to_expr_map;
            for (const auto& [expr, coeff] : e->get_expr_to_coeff_map())
                e_canon_coeff_to_expr_map.emplace(coeff, expr);

            DREAL_ASSERT(!n1.terminal_expression.has_value());
            const auto ibegin = e_canon_coeff_to_expr_map.begin();
            const auto iend = e_canon_coeff_to_expr_map.end();
            std::function<e_partial_matches_vec(typeof(ibegin))> it_to_match_coeff, it_to_match_expr;
            it_to_match_coeff = [&](const auto& it1) {
                return [&, /*copy*/ it1](const auto& n, auto& s) {
                    auto it2 = it1;
                    ++it2;
                    if (it2 == iend) partial_matches(n, s);
                    else recMatchExpr(it2->first, n, s, matches, it_to_match_expr(it2), misses);
                };
            };
            it_to_match_expr = [&](const auto& it) {
                return [&, /*copy*/ it](const auto& n, auto& s) {
                    DREAL_ASSERT(!n.terminal_expression.has_value());
                    recMatchExpr(it->second, n, s, matches, it_to_match_coeff(it), misses);
                };
            };
            recMatchExpr(ibegin->first, n1, substitutions, matches, it_to_match_expr(ibegin), misses);
        }
    }

    ADD_DECL(VisitMultiplication) {
        auto& n1 = parent.c_like(e).emplace_back(e);
        ExprNode *stateExp = &n1, *stateBase = nullptr;
        std::multimap<Expression, Expression, ExpressionLess> multimap; // canonicalize..ish
        for (const auto& [base, expo] : to_multiplication(e)->get_base_to_exponent_map()) {
            multimap.emplace(expo, base);
        }
        size_t i = 0;
        for (const auto& [expo, base] : multimap) {
            stateBase = &recAddExpr(expo, *stateExp, {});
            stateExp = &recAddExpr(
                base, *stateBase,
                i++ == multimap.size() - 1 ? is_terminal : std::optional<Expression>{}
            );
        }
        return *stateExp;
    }

    VISIT_DECL(VisitMultiplication) {
        const auto *const e = to_multiplication(_e);
        for (const auto& n1 : parent.c_like(_e)) {
            const auto *const m = to_multiplication(*n1.leaf);
            if (e->get_constant() != m->get_constant()) continue;
            if (e->get_base_to_exponent_map().size() != m->get_base_to_exponent_map().size()) continue;

            std::multimap<Expression, Expression, ExpressionLess> e_canon_expo_to_base_map;
            for (const auto& [base, expo] : e->get_base_to_exponent_map())
                e_canon_expo_to_base_map.emplace(expo, base);

            DREAL_ASSERT(!n1.terminal_expression.has_value());
            const auto ibegin = e_canon_expo_to_base_map.begin();
            const auto iend = e_canon_expo_to_base_map.end();
            std::function<e_partial_matches_vec(typeof(ibegin))> it_to_match_expo, it_to_match_base;
            it_to_match_expo = [&](const auto& it1) {
                return [&, /*copy*/ it1](const auto& n, auto& s) {
                    auto it2 = it1;
                    ++it2;
                    if (it2 == iend) partial_matches(n, s);
                    else recMatchExpr(it2->first, n, s, matches, it_to_match_base(it2), misses);
                };
            };
            it_to_match_base = [&](const auto& it1) {
                return [&, /*copy*/ it1](const auto& n, auto& s) {
                    DREAL_ASSERT(!n.terminal_expression.has_value());
                    recMatchExpr(it1->second, n, s, matches, it_to_match_expo(it1), misses);
                };
            };
            recMatchExpr(ibegin->first, n1, substitutions, matches, it_to_match_base(ibegin), misses);
        }
    }

    PatternMatchingTrie::ExprNode& PatternMatchingTrie::BinaryOpAddHelper(
        const Expression& e, const ExpressionKind& k,
        ExprNode& parent, const std::optional<Expression>& is_terminal
    ) {
        auto& n1 = parent.c_leafless(k, e.get_al_hash());
        auto& n2 = recAddExpr(get_first_argument(e), n1, {});
        auto& n3 = recAddExpr(get_second_argument(e), n2, is_terminal);
        return n3;
    }

    void PatternMatchingTrie::BinaryOpMatchHelper(
        const Expression& _e, const ExpressionKind& k,
        const ExprNode& parent, substitutions_map& s1,
        const e_matches_vec& matches,
        const e_partial_matches_vec& partial_matches,
        const e_misses_vec& misses
    ) const {
        for (const auto& n1 : parent.c(k, _e.get_al_hash())) {
            recMatchExpr(get_first_argument(_e), n1, s1, matches, PM_CONT_LAMBDA(n2, s2) {
                DREAL_ASSERT(!n1.leaf.has_value());
                DREAL_ASSERT(!n1.terminal_expression.has_value());
                DREAL_ASSERT(!n2.terminal_expression.has_value());
                recMatchExpr(get_second_argument(_e), n2, s2, matches, partial_matches, misses);
            }, misses);
        }
    }

    PatternMatchingTrie::ExprNode& PatternMatchingTrie::UnaryOpAddHelper(
        const Expression& e, const ExpressionKind& k,
        ExprNode& parent, const std::optional<Expression>& is_terminal
    ) {
        auto& n1 = parent.c_leafless(k, e.get_al_hash());
        auto& n2 = recAddExpr(get_argument(e), n1, is_terminal);
        return n2;
    }

    void PatternMatchingTrie::UnaryOpMatchHelper(
        const Expression& e, const ExpressionKind& k,
        const ExprNode& parent, substitutions_map& s1,
        const e_matches_vec& matches,
        const e_partial_matches_vec& partial_matches,
        const e_misses_vec& misses
    ) const {
        for (const auto& n1 : parent.c(k, e.get_al_hash())) {
            DREAL_ASSERT(!n1.leaf.has_value());
            DREAL_ASSERT(!n1.terminal_expression.has_value());
            recMatchExpr(get_argument(e), n1, s1, matches, partial_matches, misses);
        }
    }

    ADD_DECL(VisitDivision) { return BinaryOpAddHelper(e, ExpressionKind::Div, parent, is_terminal); }
    VISIT_DECL(VisitDivision) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Div, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitLog) { return UnaryOpAddHelper(e, ExpressionKind::Log, parent, is_terminal); }
    VISIT_DECL(VisitLog) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Log, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitAbs) { return UnaryOpAddHelper(e, ExpressionKind::Abs, parent, is_terminal); }
    VISIT_DECL(VisitAbs) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Abs, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitExp) { return UnaryOpAddHelper(e, ExpressionKind::Exp, parent, is_terminal); }
    VISIT_DECL(VisitExp) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Exp, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitSqrt) { return UnaryOpAddHelper(e, ExpressionKind::Sqrt, parent, is_terminal); }
    VISIT_DECL(VisitSqrt) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Sqrt, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitPow) { return BinaryOpAddHelper(e, ExpressionKind::Pow, parent, is_terminal); }
    VISIT_DECL(VisitPow) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Pow, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitSin) { return UnaryOpAddHelper(e, ExpressionKind::Sin, parent, is_terminal); }
    VISIT_DECL(VisitSin) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Sin, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitCos) { return UnaryOpAddHelper(e, ExpressionKind::Cos, parent, is_terminal); }
    VISIT_DECL(VisitCos) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Cos, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitTan) { return UnaryOpAddHelper(e, ExpressionKind::Tan, parent, is_terminal); }
    VISIT_DECL(VisitTan) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Tan, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitAsin) { return UnaryOpAddHelper(e, ExpressionKind::Asin, parent, is_terminal); }
    VISIT_DECL(VisitAsin) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Asin, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitAcos) { return UnaryOpAddHelper(e, ExpressionKind::Acos, parent, is_terminal); }
    VISIT_DECL(VisitAcos) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Acos, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitAtan) { return UnaryOpAddHelper(e, ExpressionKind::Atan, parent, is_terminal); }
    VISIT_DECL(VisitAtan) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Atan, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitAtan2) { return BinaryOpAddHelper(e, ExpressionKind::Atan2, parent, is_terminal); }
    VISIT_DECL(VisitAtan2) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Atan2, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitSinh) { return UnaryOpAddHelper(e, ExpressionKind::Sinh, parent, is_terminal); }
    VISIT_DECL(VisitSinh) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Sinh, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitCosh) { return UnaryOpAddHelper(e, ExpressionKind::Cosh, parent, is_terminal); }
    VISIT_DECL(VisitCosh) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Cosh, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitTanh) { return UnaryOpAddHelper(e, ExpressionKind::Tanh, parent, is_terminal); }
    VISIT_DECL(VisitTanh) {
        return UnaryOpMatchHelper(_e, ExpressionKind::Tanh, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitMin) { return BinaryOpAddHelper(e, ExpressionKind::Min, parent, is_terminal); }
    VISIT_DECL(VisitMin) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Min, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitMax) { return BinaryOpAddHelper(e, ExpressionKind::Max, parent, is_terminal); }
    VISIT_DECL(VisitMax) {
        return BinaryOpMatchHelper(_e, ExpressionKind::Max, parent, substitutions, matches, partial_matches, misses);
    }

    ADD_DECL(VisitIfThenElse) {
        const auto *const ite = to_if_then_else(e);
        auto& n1 = parent.c_leafless(ExpressionKind::IfThenElse, e.get_al_hash());
        auto& n2 = recAddForm(ite->get_conditional_formula(), n1.init_switch_kind(), {});
        auto& n3 = recAddExpr(ite->get_then_expression(), n2.init_switch_kind(), {});
        auto& n4 = recAddExpr(ite->get_else_expression(), n3, is_terminal);
        return n4;
    }

    VISIT_DECL(VisitIfThenElse) {
        const auto *const e = to_if_then_else(_e);
        for (const auto& n1 : parent.c_like(_e)) {
            DREAL_ASSERT(n1.switch_kind != nullptr);
            recMatchForm(
                e->get_conditional_formula(), *n1.switch_kind, substitutions,
                PM_CONT_LAMBDA(m, s) { DREAL_UNREACHABLE(); },
                PM_CONT_LAMBDA(n2, s2) {
                    DREAL_ASSERT(n2.switch_kind != nullptr);
                    recMatchExpr(
                        e->get_then_expression(), *n2.switch_kind, s2, matches,
                        PM_CONT_LAMBDA(n3, s3) {
                            DREAL_ASSERT(!n1.leaf.has_value());
                            DREAL_ASSERT(!n1.terminal_expression.has_value());
                            DREAL_ASSERT(!n2.terminal_expression.has_value());
                            DREAL_ASSERT(!n3.terminal_expression.has_value());
                            recMatchExpr(e->get_else_expression(), n3, s3, matches, partial_matches, misses);
                        }, misses);
                }, misses);
        }
    }

    ADD_DECL(VisitUninterpretedFunction) {
        return parent.c_like(e).emplace_back(e, is_terminal);
    }

    VISIT_DECL(VisitUninterpretedFunction) {
        // todo:  can an uninterpreted function be substituted?  yeah probably...
        // but doesn't really make sense for dReal.
        const auto *const e = to_uninterpreted_function(_e);
        for (const auto& node : parent.c_like(_e)) {
            const auto *const matched_e = to_uninterpreted_function(*node.leaf);
            if (!e->EqualTo(*matched_e) /*confirmed non-recursive, simple one-liner*/)
                misses(substitutions_map::CONST_MISS);
            else if (!node.terminal_expression.has_value())
                partial_matches(node, substitutions);
            else
                matches(*node.terminal_expression, substitutions);
        }
    }
#undef VISIT_DECL
#undef ADD_DECL
}
