//
// Created by Kunal Sheth on 2/19/25.
//

#ifndef pattern_matching_trie_H
#define pattern_matching_trie_H

#include <vector>
#include <optional>
#include <dreal/util/assert.h>
#include <dreal/util/pattern_matching/substitutions_map.h>

#include "dreal/symbolic/symbolic.h"
#include "dreal/util/box.h"
#include "dreal/util/pattern_matching/matching_stats_t.h"

namespace dreal
{
    class PatternMatchingTrie
    {
    public:
        static std::string matching_stats_csv_header(const std::string& prefix) {
            std::ostringstream s;
            s << prefix << "misses,";
            s << prefix << "partial_matches,";
            s << prefix << "matches";
            return s.str();
        }

        using e_matches_vec = std::function<void(const Expression& e, substitutions_map& s)>;
        using f_matches_vec = std::function<void(const Formula& f, substitutions_map& s)>;

        [[nodiscard]] std::pair<std::vector<std::pair<std::vector<Formula>, std::optional<substitutions_map>>>, matching_stats_t>
        find_matches(
            const std::vector<Formula>& literals, const Box& b, bool return_subs_maps,
            std::chrono::duration<uint64_t, std::micro> timeout = std::chrono::microseconds{-1}
        ) const;

        [[nodiscard]] std::pair<std::vector<std::pair<Formula, std::optional<substitutions_map>>>, matching_stats_t> find_matches(
            const Formula& f, substitutions_map& substitutions, bool return_subs_maps
        ) const;

        [[nodiscard]] std::pair<std::vector<std::pair<Formula, std::optional<substitutions_map>>>, matching_stats_t> find_matches(
            const Formula& f, const Box& box, bool return_subs_maps
        ) const {
            substitutions_map s(box, f.GetFreeVariables().size());
            return find_matches(f, s, return_subs_maps);
        }

        [[nodiscard]] std::pair<std::vector<std::pair<Expression, std::optional<substitutions_map>>>, matching_stats_t> find_matches(
            const Expression& e, substitutions_map& substitutions, bool return_subs_maps
        ) const;

        [[nodiscard]] std::pair<std::vector<std::pair<Expression, std::optional<substitutions_map>>>, matching_stats_t> find_matches(
            const Expression& e, const Box& b, bool return_subs_maps
        ) const {
            substitutions_map s(b, e.GetVariables().size());
            return find_matches(e, s, return_subs_maps);
        }

        // ended up being completely useless :(
        // uint64_t estimate_branches(const Formula& f);
        // uint64_t estimate_branches(const Expression& e);

        // uint64_t estimate_branches(const Formula& f) const;
        // uint64_t estimate_branches(const Expression& f) const;

        void insert(const Formula& f);
        void insert(const Expression& f);

    private:
        static uint64_t init_id() {
            static uint64_t global_id_counter = 0;
            return global_id_counter++;
        }

    public:
        template <typename TKind, typename This, typename OKind, typename Other>
        class TrieNode
        {
        public:
            TrieNode() : id{init_id()}, children{4} {}

            explicit TrieNode(
                const std::optional<This>& leaf,
                const std::optional<This>& terminal_expression = {}
            ) : leaf{leaf}, terminal_expression{terminal_expression}, id{init_id()}, children{4} {}

            TrieNode(const TrieNode& other) = delete; // these should never be copied.

            TrieNode(TrieNode&& other) noexcept
                : leaf{std::move(other.leaf)},
                  terminal_expression{std::move(other.terminal_expression)},
                  switch_kind{std::move(other.switch_kind)},
                  id{other.id},
                  children{std::move(other.children)} {
                other.switch_kind = nullptr; // ownership has been moved.
            }

            ~TrieNode() noexcept {
                if (switch_kind != nullptr) delete switch_kind;
            }

            [[nodiscard]] TrieNode<OKind, Other, TKind, This>& init_switch_kind() {
                if (switch_kind == nullptr) switch_kind = new TrieNode<OKind, Other, TKind, This>;
                return *switch_kind;
            }

            [[nodiscard]] const std::vector<TrieNode>& c(const TKind& k, const size_t alpha_hash) const {
                const auto it1 = children.find(k);
                if (it1 != children.end()) {
                    const auto it2 = it1->second.find(alpha_hash);
                    if (it2 != it1->second.end()) {
                        return it2->second;
                    }
                }
                const static std::vector<TrieNode> t_empty;
                return t_empty;
            }


            [[nodiscard]] std::vector<TrieNode>& c(const TKind& k, const size_t alpha_hash) {
                return children[k][alpha_hash];
            }

            [[nodiscard]] const std::vector<TrieNode>& c_like(const This& t) const {
                return c(t.get_kind(), t.get_al_hash());
            }

            [[nodiscard]] std::vector<TrieNode>& c_like(const This& t) {
                return c(t.get_kind(), t.get_al_hash());
            }

            [[nodiscard]] const TrieNode& c_leafless(const TKind& k, const size_t alpha_hash) const {
                const auto& vec = c(k, alpha_hash);
                DREAL_ASSERT(vec.size() == 1);
                return vec[0];
            }

            [[nodiscard]] TrieNode& c_leafless(const TKind& k, const size_t alpha_hash) {
                auto& vec = c(k, alpha_hash);
                if (vec.empty()) vec.emplace_back();
                DREAL_ASSERT(vec.size() == 1);
                return vec[0];
            }

            // template <typename Key>
            // [[nodiscard]] const std::unordered_map<Key, TrieNode>& c_unique(const TKind& kind) const {
            //     return unique_children<Key>(kind);
            // }
            //
            // template <typename Key>
            // [[nodiscard]] std::unordered_map<Key, TrieNode>& c_unique(const TKind& kind) {
            //     return unique_children<Key>(kind);
            // }

            std::optional<This> leaf{};
            std::optional<This> terminal_expression{};
            // important for ITEs which contain both Formulas and Expressions
            TrieNode<OKind, Other, TKind, This>* switch_kind = nullptr; // todo: switch to unique_ptr or similar.

            const uint64_t id;

        private:
            std::unordered_map<TKind, std::unordered_map<size_t, std::vector<TrieNode>>> children;

            // template <typename Key>
            // std::unordered_map<Key, TrieNode>& unique_children(const TKind& kind) const {
            //     static std::unordered_map<
            //         uint64_t, std::unordered_map<TKind, std::unordered_map<Key, TrieNode>>
            //     > additional_members;
            //     return additional_members[id][kind];
            // }

            // friend PatternMatchingTrie;
        };

        using ExprNode = TrieNode<ExpressionKind, Expression, FormulaKind, Formula>;
        using FormNode = TrieNode<FormulaKind, Formula, ExpressionKind, Expression>;
        ExprNode e_root;
        FormNode f_root;
        // FormulaUnorderedSet f_already_inserted;
        // ExpressionUnorderedSet e_already_inserted;
        // FormulaUnorderedMap<uint64_t> f_branch_est_cache;
        // ExpressionUnorderedMap<uint64_t> e_branch_est_cache;

        // using e_partial_matches_vec = std::vector<std::pair<const ExprNode*, substitutions_map>>;
        using e_partial_matches_vec = std::function<void(const ExprNode& n, substitutions_map& s)>;
        using e_est_continuation_vec = std::function<void(const ExprNode& n)>;
        // using f_partial_matches_vec = std::vector<std::pair<const FormNode*, substitutions_map>>;
        using f_partial_matches_vec = std::function<void(const FormNode& n, substitutions_map& s)>;
        using f_est_continuation_vec = std::function<void(const FormNode& n)>;

        using e_misses_vec = std::function<void(const substitutions_map::substitution_status& s)>;
        using f_misses_vec = std::function<void(const substitutions_map::substitution_status& s)>;

#define PM_CONT_LAMBDA(n,s) [&](const auto &n, auto &s)
#define EST_CONT_LAMBDA(n) [&](const auto &n)

#define VISIT_DECL(name) void name ( \
    const Expression &_e, const ExprNode &parent, \
    substitutions_map &substitutions, \
    const e_matches_vec &matches, \
    const e_partial_matches_vec &partial_matches, \
    const e_misses_vec &misses \
) const
#define ADD_DECL(name) ExprNode& name (const Expression &e, ExprNode &parent, const std::optional<Expression> &is_terminal)
#define ALL_DECLS(name) \
        VISIT_DECL(name); \
        ADD_DECL(name);
        ALL_DECLS(VisitVariable);
        ALL_DECLS(VisitConstant);
        ALL_DECLS(VisitRealConstant);
        ALL_DECLS(VisitAddition);
        ALL_DECLS(VisitMultiplication);
        ALL_DECLS(VisitDivision);
        ALL_DECLS(VisitLog);
        ALL_DECLS(VisitAbs);
        ALL_DECLS(VisitExp);
        ALL_DECLS(VisitSqrt);
        ALL_DECLS(VisitPow);
        ALL_DECLS(VisitSin);
        ALL_DECLS(VisitCos);
        ALL_DECLS(VisitTan);
        ALL_DECLS(VisitAsin);
        ALL_DECLS(VisitAcos);
        ALL_DECLS(VisitAtan);
        ALL_DECLS(VisitAtan2);
        ALL_DECLS(VisitSinh);
        ALL_DECLS(VisitCosh);
        ALL_DECLS(VisitTanh);
        ALL_DECLS(VisitMin);
        ALL_DECLS(VisitMax);
        ALL_DECLS(VisitIfThenElse);
        ALL_DECLS(VisitUninterpretedFunction);
#undef VISIT_DECL
#undef ADD_DECL
#undef ALL_DECLS

#define VISIT_DECL(name) void name ( \
    const Formula &f, const FormNode &parent, \
    substitutions_map &substitutions, \
    const f_matches_vec &matches, \
    const f_partial_matches_vec& partial_matches, \
    const f_misses_vec& misses \
) const
#define ADD_DECL(name) FormNode& name (const Formula &f, FormNode &parent, const std::optional<Formula> &is_terminal)
#define ALL_DECLS(name) \
        VISIT_DECL(name); \
        ADD_DECL(name);
        ALL_DECLS(VisitFalse);
        ALL_DECLS(VisitTrue);
        ALL_DECLS(VisitVariable);
        ALL_DECLS(VisitEqualTo);
        ALL_DECLS(VisitNotEqualTo);
        ALL_DECLS(VisitGreaterThan);
        ALL_DECLS(VisitGreaterThanOrEqualTo);
        ALL_DECLS(VisitLessThan);
        ALL_DECLS(VisitLessThanOrEqualTo);
        ALL_DECLS(VisitConjunction);
        ALL_DECLS(VisitDisjunction);
        ALL_DECLS(VisitNegation);
        ALL_DECLS(VisitForall);
        ALL_DECLS(VisitForallT);
        ALL_DECLS(VisitIntegral);
#undef VISIT_DECL
#undef ADD_DECL
#undef ALL_DECLS

        void UnaryOpMatchHelper(const Expression& e, const ExpressionKind& k, const ExprNode& parent,
                                substitutions_map& substitutions,
                                const e_matches_vec& matches,
                                const e_partial_matches_vec& partial_matches,
                                const e_misses_vec& misses) const;
        void UnaryOpMatchHelper(const Formula& e, const FormulaKind& k, const FormNode& parent,
                                substitutions_map& substitutions,
                                const f_matches_vec& matches,
                                const f_partial_matches_vec& partial_matches,
                                const f_misses_vec& misses) const;
        void BinaryOpMatchHelper(const Expression& e, const ExpressionKind& k, const ExprNode& parent,
                                 substitutions_map& substitutions,
                                 const e_matches_vec& matches,
                                 const e_partial_matches_vec& partial_matches,
                                 const e_misses_vec& misses) const;
        void BinaryOpMatchHelper(const Formula& f, const FormulaKind& k, const FormNode& parent,
                                 substitutions_map& substitutions,
                                 const f_matches_vec& matches,
                                 const f_partial_matches_vec& partial_matches,
                                 const f_misses_vec& misses) const;
        void NaryOpMatchHelper(const Formula& _f, const FormulaKind& k, const FormNode& parent,
                               substitutions_map& s1,
                               const f_matches_vec& matches,
                               const f_partial_matches_vec& partial_matches,
                               const f_misses_vec& misses) const;
        ExprNode& UnaryOpAddHelper(const Expression& f, const ExpressionKind& k, ExprNode& parent,
                                   const std::optional<Expression>& is_terminal);
        FormNode& UnaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                   const std::optional<Formula>& is_terminal);
        ExprNode& BinaryOpAddHelper(const Expression& f, const ExpressionKind& k, ExprNode& parent,
                                    const std::optional<Expression>& is_terminal);
        FormNode& BinaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                    const std::optional<Formula>& is_terminal);
        FormNode& NaryOpAddHelper(const Formula& f, const FormulaKind& k, FormNode& parent,
                                  const std::optional<Formula>& is_terminal);

        void recMatchExpr(
            const Expression& e, const ExprNode& parent, substitutions_map& substitutions,
            const e_matches_vec& matches, const e_partial_matches_vec& partial_matches, const e_misses_vec& misses
        ) const {
            // std::cout << "recMatchExpr: " << e << std::endl;
            return VisitExpression<void>(this, e, parent, substitutions, matches, partial_matches, misses);
        }

        ExprNode& recAddExpr(
            const Expression& e, ExprNode& parent, const std::optional<Expression>& is_terminal
        ) {
            // std::cout << "recAddExpr: " << e << std::endl;
            return VisitExpression<ExprNode&>(this, e, parent, is_terminal);
        }

        void recMatchForm(
            const Formula& f, const FormNode& parent, substitutions_map& substitutions,
            const f_matches_vec& matches, const f_partial_matches_vec& partial_matches, const f_misses_vec& misses
        ) const {
            // std::cout << "recMatchForm: " << f << std::endl;
            return VisitFormula<void>(this, f, parent, substitutions, matches, partial_matches, misses);
        }

        FormNode& recAddForm(
            const Formula& f, FormNode& parent, const std::optional<Formula>& is_terminal
        ) {
            // std::cout << "recAddForm: " << f << std::endl;
            return VisitFormula<FormNode&>(this, f, parent, is_terminal);
        }

        friend void drake::symbolic::VisitExpression<void>(
            PatternMatchingTrie*, const Expression& e,
            const ExprNode& parent, substitutions_map& substitutions,
            const e_matches_vec& matches, const e_partial_matches_vec& partial_matches, const e_misses_vec& misses
        );
        friend ExprNode& drake::symbolic::VisitExpression<ExprNode&>(
            PatternMatchingTrie*, const Expression& e, ExprNode& parent,
            const std::optional<Expression>& is_terminal
        );
        friend void drake::symbolic::VisitFormula<void>(
            PatternMatchingTrie*, const Formula& e,
            const FormNode& parent, substitutions_map& substitutions,
            const f_matches_vec& matches, const f_partial_matches_vec& partial_matches, const f_misses_vec& misses
        );
        friend FormNode& drake::symbolic::VisitFormula<FormNode&>(
            PatternMatchingTrie*, const Formula& e, FormNode& parent,
            const std::optional<Expression>& is_terminal
        );
    };

    // inline std::ostream& operator<<(std::ostream& os, const matching_stats_t& stats) {
    //     os << "matching_stats_t {\n"
    //        << "  misses {\n"
    //        << "    bc_type: " << stats.misses.bc_type << ",\n"
    //        << "    bc_box: " << stats.misses.bc_box << ",\n"
    //        << "    bc_bij: " << stats.misses.bc_bij << ",\n"
    //        << "    bc_const: " << stats.misses.bc_const << "\n"
    //        << "  },\n"
    //        << "  partial_matches: " << stats.partial_matches << ",\n"
    //        << "  matches: " << stats.matches << "\n"
    //        << "}";
    //     return os;
    // }
} // namespace dreal


#endif //pattern_matching_trie_H
