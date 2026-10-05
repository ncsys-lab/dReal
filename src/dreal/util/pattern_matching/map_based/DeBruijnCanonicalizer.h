//
// Created by Kunal Sheth on 12/24/25.
//

#ifndef DREAL4_CMAKE_DEBRUIJNCANONICALIZER_H
#define DREAL4_CMAKE_DEBRUIJNCANONICALIZER_H

#include "DeBruijnCanonicalizer.h"

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/exception.h"
#include "dreal/util/pattern_matching/matching_stats_t.h"
#include "dreal/util/pattern_matching/substitutions_map.h"
#include "dreal/util/pattern_matching/substitution_tree/substitution_tree.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dreal
{
    template <typename T>
    class DeBruijnCanonicalizer
    {
    private:
        using DeBruijnIndices = std::vector<int>;
        // using DeBruijnEquivalenceClass = std::map<std::vector<Variable>, T>;
        using DeBruijnEquivalenceClass = substitution_tree<T>;

        SymbolicUnorderedMap<T, std::tuple<T, DeBruijnIndices, std::vector<Variable>>> canonicalization_cache;
        // SymbolicUnorderedMap<T, std::map<DeBruijnIndices, DeBruijnEquivalenceClass>> structure_to_indices_to_concrete;
        SymbolicUnorderedMap<T, DeBruijnEquivalenceClass> structure_to_concrete;

        // Bound (universal) variables currently in scope while canonicalizing a `forall` atom.
        // They are NOT free solver variables, so they are excluded from the canonical variable
        // sequence — otherwise they reach `attempt_substitution`'s `box[v]` domain check, which
        // silently inserts the (non-solver) bound variable into the Box's shared var->idx map.
        mutable std::vector<Variable> bound_scope_;
        bool is_bound(const Variable& v) const;

        std::tuple<T, DeBruijnIndices, std::vector<Variable>>
        canonicalize_atom(const T& atom) const;

        using matches_vec = std::function<void(const T& e, substitutions_map& s)>;
        using misses_vec = std::function<void(const substitutions_map::substitution_status& s)>;
        void find_matches(
            const T& f, substitutions_map& substitutions, const matches_vec& matches, const misses_vec& misses, uint64_t &random_state
        ) const;

    public:
        DeBruijnCanonicalizer();

        void insert(const T& atom);

        [[nodiscard]] std::pair<std::vector<std::pair<std::vector<T>, std::optional<substitutions_map>>>, matching_stats_t>
        find_matches(
            const std::vector<T>& literals, const Box& b, bool return_subs_maps,
            const std::chrono::duration<uint64_t, std::micro> timeout = std::chrono::microseconds{-1}
        ) const;


        [[nodiscard]] std::pair<std::vector<std::pair<T, std::optional<substitutions_map>>>, matching_stats_t> find_matches(
            const T& f, const Box& box, bool return_subs_maps, uint64_t &random_state
        ) const;


#define INSERT_DECL(name) void name ( \
        const Expression &e, \
        std::vector<Variable>& canon_var_seq \
) const
        INSERT_DECL(VisitVariable);
        INSERT_DECL(VisitConstant);
        INSERT_DECL(VisitRealConstant);
        INSERT_DECL(VisitAddition);
        INSERT_DECL(VisitMultiplication);
        INSERT_DECL(VisitDivision);
        INSERT_DECL(VisitLog);
        INSERT_DECL(VisitAbs);
        INSERT_DECL(VisitExp);
        INSERT_DECL(VisitSqrt);
        INSERT_DECL(VisitPow);
        INSERT_DECL(VisitSin);
        INSERT_DECL(VisitCos);
        INSERT_DECL(VisitTan);
        INSERT_DECL(VisitAsin);
        INSERT_DECL(VisitAcos);
        INSERT_DECL(VisitAtan);
        INSERT_DECL(VisitAtan2);
        INSERT_DECL(VisitSinh);
        INSERT_DECL(VisitCosh);
        INSERT_DECL(VisitTanh);
        INSERT_DECL(VisitMin);
        INSERT_DECL(VisitMax);
        INSERT_DECL(VisitIfThenElse);
        INSERT_DECL(VisitUninterpretedFunction);
#undef INSERT_DECL

#define INSERT_DECL(name) void name ( \
const Formula &f, \
std::vector<Variable>& canon_var_seq \
) const
        INSERT_DECL(VisitFalse);
        INSERT_DECL(VisitTrue);
        INSERT_DECL(VisitVariable);
        INSERT_DECL(VisitEqualTo);
        INSERT_DECL(VisitNotEqualTo);
        INSERT_DECL(VisitGreaterThan);
        INSERT_DECL(VisitGreaterThanOrEqualTo);
        INSERT_DECL(VisitLessThan);
        INSERT_DECL(VisitLessThanOrEqualTo);
        INSERT_DECL(VisitConjunction);
        INSERT_DECL(VisitDisjunction);
        INSERT_DECL(VisitNegation);
        INSERT_DECL(VisitForall);
        INSERT_DECL(VisitForallT);
        INSERT_DECL(VisitIntegral);
#undef INSERT_DECL

    private:
        void insert_nary(const Formula& f, std::vector<Variable>& canon_var_seq) const;
        void insert_binary(const Formula& f, std::vector<Variable>& canon_var_seq) const;

        void insert_binary(const Expression& f, std::vector<Variable>& canon_var_seq) const;
        void insert_unary(const Expression& f, std::vector<Variable>& canon_var_seq) const;
    };
}
#endif //DREAL4_CMAKE_DEBRUIJNCANONICALIZER_H
