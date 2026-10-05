// DRPM (dReal lemma pattern matching): SatSolver::AddLearnedClausePattern is
// the pattern-matching learned-clause path, live behind --drpm-max-size > 0
// (context_impl.cc gates each theory conflict on
// explanation.size() < config().drpm_max_size()). Alongside it lives the
// CAV26 symmetry filter (compile-time CAV26_FILTER_SYMMETRIES, dreal/version.h):
// each matched substitution is classified pure / not-pure-time /
// not-pure-logic via CAV26_VARNAME_PARSER, and the CAV26_MATCH_PURE_* knobs
// select which symmetry classes are dropped instead of added.

#include <algorithm>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include <dreal/version.h>

#include "auditor.h"
#include "sat_solver.h"
#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/pattern_matching/CAV26_varname_parser.h"
#include "dreal/util/pattern_matching/matching_stats_t.h"
#include "dreal/util/pattern_matching/substitutions_map.h"

namespace dreal
{
#if CAV26_FILTER_SYMMETRIES
    std::pair<bool, bool> analyze_symmetries_npT_npL(const substitutions_map& subs) {
        bool npT = false;
        bool npL = false;
        std::optional<int> constant_time_offset{};
        for (const auto& [a,aP] : subs.get_map()) {
            if (npT && npL) break;

            const auto [pA,tA] = CAV26_VARNAME_PARSER(a.get_name());
            const auto [pAP,tAP] = CAV26_VARNAME_PARSER(aP.get_name());

            npT |= pA != pAP;

            if (tA.has_value() != tAP.has_value()) npT = true;
            if (tA.has_value() && tAP.has_value()) {
                const auto time_offset = *tAP - *tA;
                npT |= time_offset != constant_time_offset.value_or(time_offset);
                npL |= time_offset != 0;
                constant_time_offset = time_offset;
            }
        }
        return {npT, npL};
    }
#endif


    matching_stats_t SatSolver::AddLearnedClausePattern(
        PredicateNormalizer& pn,
        const std::vector<Formula>& base_conflict, const Box& base_box,
        const std::chrono::duration<uint64_t, std::micro> timeout
    ) {
        sat_log_label_clause("SatSolver::AddLearnedClausePattern");

        auto pnfs_result = pn.FindSimilar(base_conflict, base_box, DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED || CAV26_FILTER_SYMMETRIES, timeout);
        const auto& all_related_conflicts = pnfs_result.first;
        auto& match_statistics = pnfs_result.second;

        DREAL_ASSERT(match_statistics.matches == all_related_conflicts.size());
        if (all_related_conflicts.empty()) {
            DREAL_LOG_INFO("Clause did not match with itself... Adding regularly.");
            return match_statistics;
        }

        std::vector<bool> do_not_add_clause; //                            optional filtering,
        do_not_add_clause.resize(all_related_conflicts.size(), false); //  add all by default.

        std::vector<std::string_view> metadata_tags; // calculate metadata if needed, otherwise leave empty.

#if CAV26_FILTER_SYMMETRIES
        metadata_tags.resize(all_related_conflicts.size()); // empty string by default.
        DREAL_ASSERT(match_statistics.misses_bc.at(substitutions_map::CAV26_NOT_PURE_TIME) == 0);
        DREAL_ASSERT(match_statistics.misses_bc.at(substitutions_map::CAV26_NOT_PURE_LOGIC) == 0);
        DREAL_ASSERT(match_statistics.misses_bc.at(substitutions_map::CAV26_NOT_PURE_ANY) == 0);

        for (int i = 0; i < all_related_conflicts.size(); ++i) {
            const auto& [conflict_clause, subs] = all_related_conflicts[i];
            const auto [npT, npL] = analyze_symmetries_npT_npL(*subs);

            int stats_miss_reason = -1;
            if (npT && npL) {
                metadata_tags[i] = "np*";
                stats_miss_reason = substitutions_map::CAV26_NOT_PURE_ANY;
            }
            else if (npT) {
                metadata_tags[i] = "npT";
                stats_miss_reason = substitutions_map::CAV26_NOT_PURE_TIME;
            }
            else if (npL) {
                metadata_tags[i] = "npL";
                stats_miss_reason = substitutions_map::CAV26_NOT_PURE_LOGIC;
            }
            else {
                metadata_tags[i] = "P";
            }

            if ((CAV26_MATCH_PURE_TIME_SYM && npT) || (CAV26_MATCH_PURE_LOGIC_SYM && npL)) {
                ++match_statistics.misses_bc.at(stats_miss_reason);
                --match_statistics.matches;
                do_not_add_clause[i] = true;
            }
        }
#endif

        if (DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED) {
            FormulaSet s(base_conflict.begin(), base_conflict.end());
            theory_audit_formula(
                "AddLearnedClausePattern",
                !make_conjunction_SKIP_CHECKS_KUNAL_HACK(std::move(s)),
                base_box // conflict_box
            );
        }

        if (DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED) {
            pm_dump_all(base_conflict, all_related_conflicts, metadata_tags, do_not_add_clause);
        }

        DREAL_ASSERT(std::count(do_not_add_clause.begin(), do_not_add_clause.end(), false) == match_statistics.matches);
        for (int i = 0; i < all_related_conflicts.size(); ++i) {
            if (do_not_add_clause[i]) continue;
            const auto& [conflict_clause, subs] = all_related_conflicts[i];
            AddLearnedClauseDirect(conflict_clause, base_box);
        }
        return match_statistics;
    }
} // namespace dreal
