//
// Created by Kunal Sheth on 2/19/25.
//

#include <unordered_set>

#include "pattern_matching_trie.h"

#include <dreal/symbolic/symbolic_formula_cell.h>
#include <dreal/util/assert.h>
#include <dreal/util/box.h>
#include <dreal/util/logging.h>
#include <cmath>

#include "dreal/util/exception.h"
#include "dreal/util/pattern_matching/matching_stats_t.h"

namespace dreal
{
    std::pair<std::vector<std::pair<std::vector<Formula>, std::optional<substitutions_map>>>, matching_stats_t>
    PatternMatchingTrie::find_matches(
        const std::vector<Formula>& literals, const Box& box, const bool return_subs_maps,
        const std::chrono::duration<uint64_t, std::micro> timeout
    ) const {
        matching_stats_t stats = {0};
        std::vector<Formula> matches_vec;
        std::vector<std::pair<std::vector<Formula>, std::optional<substitutions_map>>> result;
        matches_vec.reserve(literals.size());

        const auto start_time = std::chrono::steady_clock::now();

        const f_misses_vec misses = [&](const auto& reason) { ++stats.misses_bc.at(reason); };
        const f_partial_matches_vec partial_matches = PM_CONT_LAMBDA(n, s)
        {
            DREAL_LOG_ERROR("Unterminated partial matching?");
            DREAL_UNREACHABLE(); // everything should AT LEAST match itself !?!?!
        };

        bool did_time_out = false;

        const auto ibegin = literals.begin();
        const auto iend = literals.end();
        std::unordered_set<Formula> seen_truncateds; // lexo-compare for nary goes one by one...
        seen_truncateds.reserve(1024 * literals.size());
        std::function<
            std::function<void(const Formula& f, substitutions_map& s)> (typeof(ibegin))
        > match_next_literal = [&](const auto& it1) {
            return [&, /*copy*/ it1](const auto& f, auto& s2) {
                if (did_time_out || std::chrono::steady_clock::now() - start_time > timeout) {
                    did_time_out = true;
                    return;
                }

                matches_vec.emplace_back(f);

                // avoid re-finding 1000s of permutations of the same clause on fedor_13.smt2, etc.
                // copy required. do NOT modify matches_vec... that needs to be a pure stack
                FormulaSet matches_vec_set(matches_vec.begin(), matches_vec.end());
                const auto [_, successful_emplace] = seen_truncateds.emplace(
                    make_conjunction_SKIP_CHECKS_KUNAL_HACK(std::move(matches_vec_set))
                );
                if (!successful_emplace) {
                    matches_vec.pop_back();
                    return;
                }

                auto it2 = it1;
                ++it2;
                stats.partial_matches++;
                if (it2 == iend) {
                    DREAL_ASSERT(matches_vec.size() == literals.size());
                    // todo: GATE THIS USING THE SAME FLAG AS AUDIT MODE.
                    // if (DREAL_LOG_DEBUG_ENABLED)
                    // for (const auto& lit : matches_vec) {
                    // DREAL_ASSERT(literals.count(
                    // substitutions_map::apply_substitution(lit, s, false)
                    // ) == 1);
                    // }
                    stats.matches++;
                    if (return_subs_maps) result.emplace_back(matches_vec, s2);
                    else result.emplace_back(matches_vec, std::nullopt);
                }
                else recMatchForm(*it2, f_root, s2, match_next_literal(it2), partial_matches, misses);
                matches_vec.pop_back();
            };
        };

        size_t sub_preallocations = 0;
        for (const auto& literal : literals) sub_preallocations += literal.GetFreeVariables().size();
        substitutions_map substitutions(box, sub_preallocations);

        recMatchForm(*ibegin, f_root, substitutions, match_next_literal(ibegin), partial_matches, misses);
        DREAL_ASSERT(result.size() == stats.matches);
        if (did_time_out)
            DREAL_LOG_INFO("Pattern matching timed out after {} usec.", timeout.count());
        // if (DREAL_LOG_INFO_ENABLED) {
        //     for (const auto& [matched_clause, _] : result) {
        //         std::ostringstream s;
        //         s << "!(";
        //         for (const auto& lit : matched_clause) s << '(' << lit << ") and ";
        //         s << ")";
        //         DREAL_LOG_INFO("Match: {}", s.str());
        //     }
        // }
        return {result, stats};
    }

    std::pair<std::vector<std::pair<Formula, std::optional<substitutions_map>>>, matching_stats_t>
    PatternMatchingTrie::find_matches(
        const Formula& f, substitutions_map& substitutions, const bool return_subs_maps
    ) const {
        const auto init_size = substitutions.size();
        DREAL_LOG_TRACE("Finding matches for formula {}", fmt::streamed(f));
        matching_stats_t stats = {0};
        std::vector<std::pair<Formula, std::optional<substitutions_map>>> match_vec;
        recMatchForm(
            f, f_root, substitutions,
            PM_CONT_LAMBDA(m, s) {
                stats.matches++;
                if (return_subs_maps) match_vec.emplace_back(m, s);
                else match_vec.emplace_back(m, std::nullopt);
            },
            PM_CONT_LAMBDA(n, s) {
                stats.partial_matches++;
                DREAL_LOG_ERROR("Unterminated partial matching? Not sure if this should ever be reachable.");
            },
            [&](const auto& reason) { ++stats.misses_bc.at(reason); }
        );
        DREAL_ASSERT(substitutions.size() == init_size);
        return {match_vec, stats};
    }

    std::pair<std::vector<std::pair<Expression, std::optional<substitutions_map>>>, matching_stats_t>
    PatternMatchingTrie::find_matches(
        const Expression& e, substitutions_map& substitutions, const bool return_subs_maps
    ) const {
        const auto init_size = substitutions.size();
        DREAL_LOG_TRACE("Finding matches for expression {}", fmt::streamed(e));
        matching_stats_t stats = {0};
        std::vector<std::pair<Expression, std::optional<substitutions_map>>> match_vec;
        recMatchExpr(
            e, e_root, substitutions,
            PM_CONT_LAMBDA(m, s) {
                stats.matches++;
                if (return_subs_maps) match_vec.emplace_back(m, s);
                else match_vec.emplace_back(m, std::nullopt);
            },
            PM_CONT_LAMBDA(n, s) {
                stats.partial_matches++;
                DREAL_LOG_ERROR("Unterminated partial matching? Not sure if this should ever be reachable.");
            },
            [&](const auto& reason) { ++stats.misses_bc.at(reason); }
        );
        DREAL_ASSERT(substitutions.size() == init_size);
        return {match_vec, stats};
    }

    void PatternMatchingTrie::insert(const Formula& f) {
        // if (f_already_inserted.count(f)) return; // was relevant during c_unique() attempt.
        recAddForm(f, f_root, f);
        // f_already_inserted.emplace(f);
    }

    void PatternMatchingTrie::insert(const Expression& e) {
        // if (e_already_inserted.count(e)) return; // was relevant during c_unique() attempt.
        recAddExpr(e, e_root, e);
        // e_already_inserted.emplace(e);
    }
}