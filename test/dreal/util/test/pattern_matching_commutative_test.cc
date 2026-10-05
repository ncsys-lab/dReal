/*
   Copyright 2017 Toyota Research Institute

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
// Commutative-reorder coverage for the DeBruijn canonicalizer. The n-ary
// AND/OR (insert_nary) and ADD/MUL (VisitAddition/VisitMultiplication) paths
// order operands by get_al_hash() -- the alpha-equivalence hash, under which a
// variable hashes by TYPE only (FormulaVar/ExpressionVar constructors:
// hash_combine(41, type)) -- so the canonical operand order is
// renaming-invariant for structurally distinct operands and falls back to the
// underlying container order (FormulaSet / expr-keyed map order, i.e.
// variable-ID = creation order) on al_hash ties. pattern_matching_test.cc
// flags this ordering as brittle in several `todo` comments but never tests
// the reorder property itself. This file pins the sharpest true statements:
//
//  * source-level operand reorder is absorbed at construction (Drake stores
//    AND/OR operands in a std::set, ADD/MUL operands in expr-keyed maps);
//  * a renaming across permuted variable-CREATION order still matches when
//    the operands are structurally distinct (the al_hash sort order is
//    renaming-invariant) or al_hash-tied over DISJOINT variables (any
//    positional pairing the substitution tree picks is a valid bijection);
//  * the genuine boundary: al_hash-tied operands SHARING a variable (a
//    chain) miss under a creation-order-inverting renaming, because the tie
//    is broken by variable-ID order, which a renaming does not preserve.
//
// A missed match only forfeits lemma reuse in DRPM (performance); a spurious
// match is the dangerous direction -- it would let DRPM learn a clause
// forbidding a satisfiable assignment, SOUNDNESS (asserts phi T-unsatisfiable
// on a T-satisfiable phi -- false unsat). The constant-discrimination test at
// the bottom pins that direction at the unit level; the end-to-end guard is
// test/dreal/solver/test/sat_solver_drpm_rigorous_test.cc.

#include <dreal/util/pattern_matching/map_based/DeBruijnCanonicalizer.h>
#include <gtest/gtest.h>
#include "dreal/symbolic/symbolic.h"

#include <chrono>
#include <set>
#include <string>
#include <vector>

namespace dreal
{
    namespace
    {
        // The four substitution round-trip checks from
        // pattern_matching_test.cc's test_matches_and_misses, extracted for a
        // single (form, subs) match result.
        template <typename T>
        void check_substitution_round_trip(const T& pattern, const T& form, const substitutions_map& subs) {
            EXPECT_TRUE(substitutions_map::apply_substitution(form, subs, false).EqualTo(pattern));
            EXPECT_TRUE(substitutions_map::apply_substitution(pattern, subs, true).EqualTo(form));
            EXPECT_TRUE(
                substitutions_map::apply_substitution(
                    substitutions_map::apply_substitution(form, subs, false), subs, true
                ).EqualTo(form)
            );
            EXPECT_TRUE(
                substitutions_map::apply_substitution(
                    substitutions_map::apply_substitution(pattern, subs, true), subs, false
                ).EqualTo(pattern)
            );
        }

        // Inserts pattern + candidates into a fresh canonicalizer, runs
        // find_matches(pattern) with the empty Box (domain filtering vacuous;
        // pinned by AttemptSubstitutionOutOfBoxVariable in
        // pattern_matching_test.cc), round-trips every hit, and returns the
        // set of matched atoms. The self-match is the sanity floor.
        template <typename T>
        SymbolicSet<T> insert_and_find(const T& pattern, const std::vector<T>& candidates) {
            DeBruijnCanonicalizer<T> trie;
            uint64_t random_state = 0;
            trie.insert(pattern);
            for (const auto& c : candidates) trie.insert(c);
            SymbolicSet<T> found;
            for (const auto& [form, op_subs] : trie.find_matches(pattern, Box{}, true, random_state).first) {
                EXPECT_TRUE(op_subs.has_value()); // find_matches(..., return_subs_maps=true)
                check_substitution_round_trip(pattern, form, *op_subs);
                found.insert(form);
            }
            EXPECT_EQ(found.count(pattern), 1);
            return found;
        }

        // Builders parameterized over the variable tuple, so candidates can be
        // built over fresh variables created in permuted orders. The operands
        // are structurally DISTINCT (distinct al_hash: Lt(sin, var),
        // Gt(pow, const), Leq(exp, const), Eq(add, var)), so the canonical
        // operand order is renaming-invariant; `w + y == z` shares variables
        // across operands to make the bijection load-bearing.
        Formula distinct_conj(const Variable& w, const Variable& x, const Variable& y, const Variable& z) {
            return (sin(w) < x) && (y * y > 2) && (exp(z) <= 5) && (w + y == z);
        }
        Formula distinct_disj(const Variable& w, const Variable& x, const Variable& y, const Variable& z) {
            return (sin(w) < x) || (y * y > 2) || (exp(z) <= 5) || (w + y == z);
        }
        // Distinct-structure ADD terms: sin(w), pow(x,2), exp(y), and the bare
        // var z (coefficient 2) all carry distinct al_hash sort keys.
        Expression distinct_add(const Variable& w, const Variable& x, const Variable& y, const Variable& z) {
            return sin(w) + x * x + exp(y) + 2 * z;
        }
        // Distinct-structure MUL factors: the sort key is the BASE of the
        // base->exponent map, so every base must be structurally distinct.
        Expression distinct_mul(const Variable& w, const Variable& x, const Variable& y, const Variable& z) {
            return sin(w) * exp(x) * tanh(y) * z;
        }

        TEST(PatternMatchingCommutativeTest, SourceOrderReorderIsConstructionIdentity) {
            const Variable x{"x1", Variable::Type::CONTINUOUS};
            const Variable y{"y1", Variable::Type::CONTINUOUS};
            const Variable z{"z1", Variable::Type::CONTINUOUS};
            const Variable b1{"b1", Variable::Type::BOOLEAN};
            const Variable b2{"b2", Variable::Type::BOOLEAN};

            // Drake stores AND/OR operands in a std::set and ADD/MUL operands
            // in expr-keyed maps, so a source-level operand permutation over
            // the SAME variables is absorbed before the canonicalizer ever
            // runs: the permuted spellings are the identical formula object.
            EXPECT_TRUE((Formula{b1} && (x < y) && Formula{b2}).EqualTo(Formula{b2} && Formula{b1} && (x < y)));
            EXPECT_TRUE((Formula{b1} || (x < y) || Formula{b2}).EqualTo(Formula{b2} || Formula{b1} || (x < y)));
            EXPECT_TRUE((1 + 2 * x + 3 * y + sin(z)).EqualTo(sin(z) + 3 * y + 1 + 2 * x));
            EXPECT_TRUE((2 * x * tanh(y) * pow(z, 3)).EqualTo(pow(z, 3) * tanh(y) * 2 * x));

            // And find_matches sees the permuted spelling as the inserted atom.
            const Formula pattern = Formula{b1} && (x < y) && Formula{b2};
            const auto found = insert_and_find(Formula{b2} && Formula{b1} && (x < y), {pattern});
            EXPECT_EQ(found.size(), 1);
            EXPECT_EQ(found.count(pattern), 1);
        }

        TEST(PatternMatchingCommutativeTest, RenamedConjunctionMatchesAcrossPermutedCreationOrder) {
            // Pattern variables created in role order w, x, y, z.
            const Variable w{"w1", Variable::Type::CONTINUOUS};
            const Variable x{"x1", Variable::Type::CONTINUOUS};
            const Variable y{"y1", Variable::Type::CONTINUOUS};
            const Variable z{"z1", Variable::Type::CONTINUOUS};
            // Candidate A: fresh variables created in REVERSED role order.
            const Variable z2{"z2", Variable::Type::CONTINUOUS};
            const Variable y2{"y2", Variable::Type::CONTINUOUS};
            const Variable x2{"x2", Variable::Type::CONTINUOUS};
            const Variable w2{"w2", Variable::Type::CONTINUOUS};
            // Candidate B: fresh variables created in INTERLEAVED role order.
            const Variable y3{"y3", Variable::Type::CONTINUOUS};
            const Variable w3{"w3", Variable::Type::CONTINUOUS};
            const Variable z3{"z3", Variable::Type::CONTINUOUS};
            const Variable x3{"x3", Variable::Type::CONTINUOUS};

            const Formula pattern = distinct_conj(w, x, y, z);
            const Formula rev = distinct_conj(w2, x2, y2, z2);
            const Formula inter = distinct_conj(w3, x3, y3, z3);
            const auto found = insert_and_find(pattern, {rev, inter});
            EXPECT_EQ(found.size(), 3);
            EXPECT_EQ(found.count(rev), 1);
            EXPECT_EQ(found.count(inter), 1);
        }

        TEST(PatternMatchingCommutativeTest, RenamedDisjunctionMatchesAcrossPermutedCreationOrder) {
            const Variable w{"w1", Variable::Type::CONTINUOUS};
            const Variable x{"x1", Variable::Type::CONTINUOUS};
            const Variable y{"y1", Variable::Type::CONTINUOUS};
            const Variable z{"z1", Variable::Type::CONTINUOUS};
            const Variable z2{"z2", Variable::Type::CONTINUOUS};
            const Variable y2{"y2", Variable::Type::CONTINUOUS};
            const Variable x2{"x2", Variable::Type::CONTINUOUS};
            const Variable w2{"w2", Variable::Type::CONTINUOUS};
            const Variable y3{"y3", Variable::Type::CONTINUOUS};
            const Variable w3{"w3", Variable::Type::CONTINUOUS};
            const Variable z3{"z3", Variable::Type::CONTINUOUS};
            const Variable x3{"x3", Variable::Type::CONTINUOUS};

            const Formula pattern = distinct_disj(w, x, y, z);
            const Formula rev = distinct_disj(w2, x2, y2, z2);
            const Formula inter = distinct_disj(w3, x3, y3, z3);
            const auto found = insert_and_find(pattern, {rev, inter});
            EXPECT_EQ(found.size(), 3);
            EXPECT_EQ(found.count(rev), 1);
            EXPECT_EQ(found.count(inter), 1);
        }

        TEST(PatternMatchingCommutativeTest, RenamedAdditionMatchesAcrossPermutedCreationOrder) {
            const Variable w{"w1", Variable::Type::CONTINUOUS};
            const Variable x{"x1", Variable::Type::CONTINUOUS};
            const Variable y{"y1", Variable::Type::CONTINUOUS};
            const Variable z{"z1", Variable::Type::CONTINUOUS};
            const Variable z2{"z2", Variable::Type::CONTINUOUS};
            const Variable y2{"y2", Variable::Type::CONTINUOUS};
            const Variable x2{"x2", Variable::Type::CONTINUOUS};
            const Variable w2{"w2", Variable::Type::CONTINUOUS};
            const Variable y3{"y3", Variable::Type::CONTINUOUS};
            const Variable w3{"w3", Variable::Type::CONTINUOUS};
            const Variable z3{"z3", Variable::Type::CONTINUOUS};
            const Variable x3{"x3", Variable::Type::CONTINUOUS};

            const Expression pattern = distinct_add(w, x, y, z);
            const Expression rev = distinct_add(w2, x2, y2, z2);
            const Expression inter = distinct_add(w3, x3, y3, z3);
            const auto found = insert_and_find(pattern, {rev, inter});
            EXPECT_EQ(found.size(), 3);
            EXPECT_EQ(found.count(rev), 1);
            EXPECT_EQ(found.count(inter), 1);
        }

        TEST(PatternMatchingCommutativeTest, RenamedMultiplicationMatchesAcrossPermutedCreationOrder) {
            const Variable w{"w1", Variable::Type::CONTINUOUS};
            const Variable x{"x1", Variable::Type::CONTINUOUS};
            const Variable y{"y1", Variable::Type::CONTINUOUS};
            const Variable z{"z1", Variable::Type::CONTINUOUS};
            const Variable z2{"z2", Variable::Type::CONTINUOUS};
            const Variable y2{"y2", Variable::Type::CONTINUOUS};
            const Variable x2{"x2", Variable::Type::CONTINUOUS};
            const Variable w2{"w2", Variable::Type::CONTINUOUS};
            const Variable y3{"y3", Variable::Type::CONTINUOUS};
            const Variable w3{"w3", Variable::Type::CONTINUOUS};
            const Variable z3{"z3", Variable::Type::CONTINUOUS};
            const Variable x3{"x3", Variable::Type::CONTINUOUS};

            const Expression pattern = distinct_mul(w, x, y, z);
            const Expression rev = distinct_mul(w2, x2, y2, z2);
            const Expression inter = distinct_mul(w3, x3, y3, z3);
            const auto found = insert_and_find(pattern, {rev, inter});
            EXPECT_EQ(found.size(), 3);
            EXPECT_EQ(found.count(rev), 1);
            EXPECT_EQ(found.count(inter), 1);
        }

        TEST(PatternMatchingCommutativeTest, TiedOperandsDisjointVarsTenWide) {
            // k = 10 al_hash-tied operands (all four n-ary containers): the
            // worst case for the tie-break order, and the blow-up guard --
            // canonicalization sorts k tied operands and the substitution-tree
            // walk is only k (or 2k) deep, so this must complete instantly with
            // no stack risk. Because the operand variables are DISJOINT, any
            // positional pairing the tree picks is a valid bijection, so a
            // fresh renamed copy must match regardless of tie order; the
            // round-trip check validates whichever pairing was chosen.
            constexpr int k = 10;
            std::vector<Variable> v;
            std::vector<Variable> w;
            std::vector<Variable> bv;
            std::vector<Variable> bw;
            v.reserve(k); w.reserve(k); bv.reserve(k); bw.reserve(k);
            for (int i = 0; i < k; ++i) v.emplace_back("v" + std::to_string(i), Variable::Type::CONTINUOUS);
            for (int i = 0; i < k; ++i) w.emplace_back("w" + std::to_string(k - i), Variable::Type::CONTINUOUS);
            for (int i = 0; i < k; ++i) bv.emplace_back("p" + std::to_string(i), Variable::Type::BOOLEAN);
            for (int i = 0; i < k; ++i) bw.emplace_back("q" + std::to_string(k - i), Variable::Type::BOOLEAN);

            Expression add_pattern{0.0};
            Expression add_copy{0.0};
            Expression mul_pattern{1.0};
            Expression mul_copy{1.0};
            Formula and_pattern{Formula::True()};
            Formula and_copy{Formula::True()};
            Formula or_pattern{Formula::False()};
            Formula or_copy{Formula::False()};
            for (int i = 0; i < k; ++i) {
                add_pattern += sin(v[i]);
                add_copy += sin(w[i]);
                mul_pattern *= tanh(v[i]);
                mul_copy *= tanh(w[i]);
                and_pattern = and_pattern && Formula{bv[i]};
                and_copy = and_copy && Formula{bw[i]};
                or_pattern = or_pattern || Formula{bv[i]};
                or_copy = or_copy || Formula{bw[i]};
            }

            const auto add_found = insert_and_find(add_pattern, {add_copy});
            EXPECT_EQ(add_found.size(), 2);
            EXPECT_EQ(add_found.count(add_copy), 1);

            const auto mul_found = insert_and_find(mul_pattern, {mul_copy});
            EXPECT_EQ(mul_found.size(), 2);
            EXPECT_EQ(mul_found.count(mul_copy), 1);

            const auto and_found = insert_and_find(and_pattern, {and_copy});
            EXPECT_EQ(and_found.size(), 2);
            EXPECT_EQ(and_found.count(and_copy), 1);

            const auto or_found = insert_and_find(or_pattern, {or_copy});
            EXPECT_EQ(or_found.size(), 2);
            EXPECT_EQ(or_found.count(or_copy), 1);
        }

        TEST(PatternMatchingCommutativeTest, TiedOperandsSharedVarChainBoundary) {
            // (a < b) && (b < c): the two operands are al_hash-tied (both
            // Lt(Var, Var)) AND share the middle variable, so the tie order --
            // container order, i.e. variable-ID (creation) order -- decides
            // which occurrence slot the shared variable lands in, i.e. the
            // DeBruijn index vector itself.
            const Variable a{"a1", Variable::Type::CONTINUOUS};
            const Variable b{"b1", Variable::Type::CONTINUOUS};
            const Variable c{"c1", Variable::Type::CONTINUOUS};
            const Formula pattern = (a < b) && (b < c);

            // Renaming that PRESERVES creation order (u, v, t created in role
            // order): same tie order, same DeBruijn structure -- must match.
            const Variable u{"u1", Variable::Type::CONTINUOUS};
            const Variable vv{"v1", Variable::Type::CONTINUOUS};
            const Variable t{"t1", Variable::Type::CONTINUOUS};
            const Formula forward = (u < vv) && (vv < t);

            // Renaming that INVERTS creation order (r, q, p created in that
            // order, chained p < q < r): the tie-break visits Lt(q, r) before
            // Lt(p, q), yielding DeBruijn indices [1,2,3,1] against the
            // pattern's [1,2,2,3] -- a different canonical structure.
            const Variable r{"r1", Variable::Type::CONTINUOUS};
            const Variable q{"q1", Variable::Type::CONTINUOUS};
            const Variable p{"p1", Variable::Type::CONTINUOUS};
            const Formula inverted = (p < q) && (q < r);

            const auto found = insert_and_find(pattern, {forward, inverted});
            EXPECT_EQ(found.count(forward), 1);
            // Sharpest true statement about the CURRENT implementation: the
            // creation-order-inverted renaming is NOT found. The miss only
            // forfeits lemma reuse (performance, never a verdict); if a future
            // canonicalizer strengthening makes this match, flip this
            // expectation -- the impl got stronger, not wrong.
            EXPECT_EQ(found.count(inverted), 0);  // INTEGRATION-VERIFY: derived from insert_nary's al_hash-tie fallback to FormulaSet (variable-ID) order; if this matches instead, update the expectation and the file comment
            EXPECT_EQ(found.size(), 2);
        }

        TEST(PatternMatchingCommutativeTest, ClauseLevelPermutationsAreBoundedAndComplete) {
            // Clause-level matching (the DRPM entry point, via
            // PredicateNormalizer::FindSimilar) over k alpha-equivalent
            // literals with disjoint variables: every injective slot->atom
            // assignment is a full clause match, so the result is exactly k!
            // and must be enumerated without stack or time blow-up.
            constexpr int k = 6;  // 6! = 720
            DeBruijnCanonicalizer<Formula> trie;
            std::vector<Formula> literals;
            literals.reserve(k);
            for (int i = 0; i < k; ++i) {
                const Variable ai{"a" + std::to_string(i), Variable::Type::CONTINUOUS};
                const Variable bi{"b" + std::to_string(i), Variable::Type::CONTINUOUS};
                literals.emplace_back(sin(ai) < bi);
            }
            for (const auto& lit : literals) trie.insert(lit);

            const auto [result, stats] = trie.find_matches(literals, Box{}, true);
            EXPECT_EQ(stats.matches, 720u);  // INTEGRATION-VERIFY: k! assumes no clause-level dedup (the dedup block in DeBruijnCanonicalizer::find_matches is commented out); if dedup returns, this drops
            EXPECT_EQ(result.size(), stats.matches);

            // Round-trip one full clause match: literal-by-literal, the
            // matched clause maps back onto the query clause.
            ASSERT_FALSE(result.empty());
            const auto& [matched_clause, op_subs] = result.front();
            ASSERT_TRUE(op_subs.has_value());
            ASSERT_EQ(matched_clause.size(), literals.size());
            for (int i = 0; i < k; ++i) {
                EXPECT_TRUE(
                    substitutions_map::apply_substitution(matched_clause[i], *op_subs, false).EqualTo(literals[i])
                );
            }
        }

        TEST(PatternMatchingCommutativeTest, ClauseLevelZeroTimeoutCompletesEmpty) {
            // The solver-side pm_timeout can never reach zero (its floor is the
            // SAT+theory elapsed time; see the pm_timeout computation in
            // context_impl.cc), so pin the timeout machinery directly: an
            // already-expired budget must terminate and record nothing. What
            // preserves solver progress on this path is the caller's
            // unconditional AddLearnedClauseDirect after the PM call, exercised
            // end-to-end in sat_solver_drpm_rigorous_test.cc.
            constexpr int k = 3;
            DeBruijnCanonicalizer<Formula> trie;
            std::vector<Formula> literals;
            literals.reserve(k);
            for (int i = 0; i < k; ++i) {
                const Variable ai{"a" + std::to_string(i), Variable::Type::CONTINUOUS};
                const Variable bi{"b" + std::to_string(i), Variable::Type::CONTINUOUS};
                literals.emplace_back(sin(ai) < bi);
            }
            for (const auto& lit : literals) trie.insert(lit);

            const auto [result, stats] = trie.find_matches(
                literals, Box{}, true, std::chrono::duration<uint64_t, std::micro>{0}
            );
            EXPECT_EQ(result.size(), 0);  // INTEGRATION-VERIFY: assumes steady_clock advances (> 0 ns) between the walk's start and its first match callback; if flaky, completion (not emptiness) is the load-bearing assertion
            EXPECT_EQ(stats.matches, result.size());
        }

        TEST(PatternMatchingCommutativeTest, ConstantsAreStructureNotPatternVariables) {
            // Premise pin for the DRPM near-miss soundness test
            // (test/dreal/solver/test/sat_solver_drpm_rigorous_test.cc): only
            // variables become DeBruijn dummies; constants stay in the
            // canonical structure. x*x < 1 must reach the renamed z*z < 1 and
            // must NOT reach y*y < 9 -- a cross-constant match would let DRPM
            // learn a clause forbidding a satisfiable assignment: SOUNDNESS
            // (asserts phi T-unsatisfiable on a T-satisfiable phi -- false
            // unsat).
            const Variable x{"x1", Variable::Type::CONTINUOUS};
            const Variable y{"y1", Variable::Type::CONTINUOUS};
            const Variable z{"z1", Variable::Type::CONTINUOUS};
            const Formula pattern = x * x < 1;
            const Formula same_const = z * z < 1;
            const Formula near_miss = y * y < 9;
            const auto found = insert_and_find(pattern, {same_const, near_miss});
            EXPECT_EQ(found.count(same_const), 1);
            EXPECT_EQ(found.count(near_miss), 0);
            EXPECT_EQ(found.size(), 2);
        }
    } // namespace
} // namespace dreal
