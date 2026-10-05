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
#include <dreal/util/pattern_matching/trie_based/pattern_matching_trie.h>
#include <dreal/util/pattern_matching/map_based/DeBruijnCanonicalizer.h>
#include <dreal/symbolic/symbolic_formula_cell.h>
#include <gtest/gtest.h>
#include "dreal/symbolic/symbolic.h"


namespace dreal
{
    namespace
    {
        class PatternMatchingTest : public ::testing::Test
        {
        protected:
            void SetUp() override {}
            const Variable x1{"x1", Variable::Type::CONTINUOUS};
            const Variable y1{"y1", Variable::Type::CONTINUOUS};
            const Variable z1{"z1", Variable::Type::CONTINUOUS};

            const Variable x2{"x2", Variable::Type::CONTINUOUS};
            const Variable y2{"y2", Variable::Type::CONTINUOUS};
            const Variable z2{"z2", Variable::Type::CONTINUOUS};

            const Variable t0{"t0", Variable::Type::CONTINUOUS};
            const Variable t1{"t1", Variable::Type::CONTINUOUS};
            const Variable t2{"t2", Variable::Type::CONTINUOUS};

            const Variable p1{"p1", Variable::Type::INTEGER};
            const Variable q1{"q1", Variable::Type::INTEGER};
            const Variable r1{"r1", Variable::Type::INTEGER};

            const Variable p2{"p2", Variable::Type::INTEGER};
            const Variable q2{"q2", Variable::Type::INTEGER};
            const Variable r2{"r2", Variable::Type::INTEGER};

            const Variable b1{"b1", Variable::Type::BOOLEAN};
            const Variable b2{"b2", Variable::Type::BOOLEAN};
            const Variable b3{"b3", Variable::Type::BOOLEAN};
            const Variable b4{"b4", Variable::Type::BOOLEAN};

            const std::shared_ptr<const OdeFlow> flow1 = std::make_shared<OdeFlow>(
                "flow_1", std::vector<std::pair<Variable, Expression>>{
                    {x1, 10 * (y1 - x1)}, // Lorenz
                    {y1, x1 * (28 - z1) - y1},
                    {z1, x1 * y1 - 8.0 / 3.0 * z1},
                }
            );
            const std::shared_ptr<const OdeFlow> flow2 = std::make_shared<OdeFlow>(
                "flow_2", std::vector<std::pair<Variable, Expression>>{
                    {x2, -sin(x1) - x2}, // Pendulum
                    {x1, x2},
                }
            );
            const std::shared_ptr<const OdeFlow> flow3 = std::make_shared<OdeFlow>(
                "flow_3", std::vector<std::pair<Variable, Expression>>{
                    {x2, +3 * (1 - pow(x1, 2)) * x2 - x1}, // Hamiltonian Van der Pol
                    {y2, +3 * (1 - pow(x1, 2)) * y2 - y1},
                    {y1, y2},
                    {x1, x2}
                }
            );
        };

        // helpers.
        Expression rc(double lb, bool use_lb_as_repr = false) {
            return real_constant(lb, std::nextafter(lb, std::numeric_limits<double>::infinity()), use_lb_as_repr);
        }

        template <typename T1, typename T2=T1>
        void test_matches_and_misses(
            const T1& pattern, const std::vector<T1>& matches, const std::vector<T1>& misses1,
            const std::vector<T2>& misses2 = {}
        ) {
            // PatternMatchingTrie trie;
            // PatternMatchingTrie &trie2 = trie;
            uint64_t random_state = 0;
            DeBruijnCanonicalizer<T1> trie;
            DeBruijnCanonicalizer<T2> trie2;
            for (const auto& match : matches) trie.insert(match);
            for (const auto& miss : misses1) trie.insert(miss);
            for (const auto& miss : misses2) trie2.insert(miss);
            SymbolicSet<T1> found;
            for (const auto& [form, op_subs] : trie.find_matches(pattern, Box{}, true, random_state).first) {
                EXPECT_TRUE(op_subs.has_value()); // trie.find_matches(..., return_subs_maps=true)
                const auto& subs = *op_subs;
                // check substitutions are correct and injective:
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
                found.insert(form);
            }
            std::cout << "FOUND: " << found.size() << " out of " << matches.size() << " matches." << std::endl;
            for (const auto& match : matches) {
                EXPECT_EQ(found.count(match), 1);
                std::cout << pattern << " MATCHES " << match << std::endl;
                EXPECT_EQ(match.get_al_hash(), pattern.get_al_hash());
            }
            for (const auto& miss : misses1) {
                EXPECT_EQ(found.count(miss), 0);
                std::cout << pattern << " MISSES " << miss << std::endl;
                // EXPECT_NE(miss.get_al_hash(), pattern.get_al_hash()); // Close!! but not quite for valid reasons...
            }
        }

        // tests.
        TEST_F(PatternMatchingTest, SimpleClauseFinder) {
            // PatternMatchingTrie trie;
            DeBruijnCanonicalizer<Formula> trie;
            FormulaSet literals{
                y1 == sin(x1),
                y2 == sin(x2),
                y1 == atan(x1),
                y2 == atan(x2),
            };
            for (const auto& lit : literals) trie.insert(lit);
            const auto [related_clauses, stats] = trie.find_matches(
                {y1 == sin(x1), y1 == atan(x1)}, Box{}, false
            );
            EXPECT_EQ(stats.misses_bc.at(substitutions_map::substitution_status::BIJ_MISS), 2);
            EXPECT_EQ(stats.matches, 2);
            EXPECT_EQ(related_clauses.size(), 2);
            EXPECT_EQ(related_clauses[0].second, std::nullopt);  // trie.find_matches(..., return_subs_maps=false)
            EXPECT_EQ(related_clauses[1].second, std::nullopt);
            // EXPECT_EQ(trie.estimate_branches(y1 == sin(x1)), 12);

            // todo: make less brittle... depends on hash values.
            EXPECT_TRUE(related_clauses[0].first[0].EqualTo(y1 == sin(x1)));
            EXPECT_TRUE(related_clauses[0].first[1].EqualTo(y1 == atan(x1)));
            EXPECT_TRUE(related_clauses[1].first[0].EqualTo(y2 == sin(x2)));
            EXPECT_TRUE(related_clauses[1].first[1].EqualTo(y2 == atan(x2)));
        }

        TEST_F(PatternMatchingTest, ForallTExpressions) {
            auto pattern = forallT(flow1, 0, t1, (x1 < 2) && (y1 > 3));

            std::vector matches{
                forallT(flow1, 0, t1, (x1 < 2) && (y1 > 3)),
                forallT(flow1, 0, t2, (x1 < 2) && (y1 > 3)),
                forallT(flow1, 0, t0, (y1 > 3) && (x1 < 2)),

                // see UPDATE comment in `ADD_DECL(VisitForallT)` of `pattern_matching_trie_form.cc`
                // variables in the bound_f can be renamed.
                forallT(flow1, 0, t2, (z1 < 2) && (z2 > 3)),
                forallT(flow1, 0, z1, (t1 < 2) && (y1 > 3))
            };
            std::vector misses{
                forallT(flow1, 1, t2, (x1 < 2) && (y1 > 3)),
                forallT(flow1, t0, t2, (x1 < 2) && (y1 > 3)),
                forallT(flow1, 0, t1, (x1 < 2) && (y1 > 4)),
                forallT(flow3, 0, t1, (x1 < 2) && (y1 > 3)),
                forallT(flow2, 0, t1, (x1 < 2) && (x2 > 3)),
                integral(t0, t1, {x1, x2}, {y1, y2}, flow2),

                forallT(flow1, 0, t1, (x1 < 2.1) && (y1 > 3)),
                forallT(flow1, 0, t1, (x1 < 2.1) && (y1 > 2)),
                forallT(flow1, 0, t1, (x1 < 2) && (x1 > 3)),
                forallT(flow1, 0, t1, (z1 < 2) && (t1 > 3)),
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, ForallBoundVarNoLeak) {
            // Regression: a `forall`'s bound (universal) variable must NOT enter the canonical
            // variable sequence / substitution map. Before the fix, VisitForall appended the
            // bound `y1` to `concrete_vars`, so it reached `attempt_substitution`, which does
            // `box[y1]` — and `Box::operator[](const Variable&) const` silently inserts `y1->0`
            // into the *shared* var->idx map for a variable that is not a solver variable
            // (latent box-index corruption). The self-match then carried a spurious (y1, y1)
            // pair. Only the free variable `x1` may appear in the substitution.
            DeBruijnCanonicalizer<Formula> trie;
            uint64_t random_state = 0;
            const Formula fa = forall(Variables{y1}, x1 + y1 >= 0);  // free {x1}, bound {y1}
            trie.insert(fa);
            const auto [results, stats] = trie.find_matches(fa, Box{}, true, random_state);
            ASSERT_EQ(results.size(), 1);
            ASSERT_TRUE(results[0].second.has_value());
            // Exactly one pair (free x1); was 2 with the bound-var leak.
            EXPECT_EQ(results[0].second->size(), 1);
        }

        TEST_F(PatternMatchingTest, AttemptSubstitutionOutOfBoxVariable) {
            // Mixed membership (one variable in the box, the other not) must be a
            // BOX_MISS. Pre-fix, `box[z1]` default-inserted z1->0 into the shared
            // name->index map — silent poison — and the domain check compared
            // dimension 0 against itself, passing vacuously.
            Box box{{x1}};
            substitutions_map subs{box, 2};
            EXPECT_EQ(subs.attempt_substitution(z1, x1), substitutions_map::BOX_MISS);
            EXPECT_FALSE(box.has_variable(z1));  // the poison witness

            // Both-absent: no domains to compare — matching stays structural. This
            // pins the PM test suite's empty-Box usage (domain filtering vacuous),
            // e.g. ForallBoundVarNoLeak above.
            substitutions_map no_domains{Box{}, 2};
            EXPECT_EQ(no_domains.attempt_substitution(z1, x1), substitutions_map::SUCCESS);
        }

        TEST_F(PatternMatchingTest, IntegralExpressions) {
            auto pattern = integral(t0, t1, {x1, x2}, {y1, y2}, flow2);

            std::vector matches{
                integral(t0, t1, {x1, x2}, {y1, y2}, flow2),
                integral(t1, t0, {x1, x2}, {y1, y2}, flow2),
                integral(t1, t2, {x1, x2}, {y1, y2}, flow2),
                integral(t0, t2, {x1, x2}, {y1, y2}, flow2),
                integral(t0, t1, {z1, x2}, {y1, y2}, flow2),
                integral(t0, t1, {y1, y2}, {x1, x2}, flow2),
            };
            std::vector misses{
                forallT(flow1, 1, t2, (x1 < 2) && (y1 > 3)),
                forallT(flow1, t0, t2, (x1 < 2) && (y1 > 3)),
                forallT(flow1, 0, t1, (z1 < 2) && (y1 > 3)),
                forallT(flow1, 0, t1, (x1 < 2) && (y1 > 4)),

                integral(t0, t1, {x1, x2, z1}, {y1, y2, z2}, flow1),
                integral(t1, t2, {x1, x2, z1, z2}, {y1, y2, z1, z2}, flow3),
                integral(t0, t2, {x1, x2, z1}, {y1, y2, z2}, flow1),
                integral(t0, t1, {z1, x2, z1, z2}, {y1, y2, z1, z2}, flow3),
                integral(t0, t1, {y1, y2, z1}, {x1, x2, z2}, flow1),

                integral(t0, t1, {x2, x2}, {y1, y2}, flow2),
                integral(t0, t0, {x1, x2}, {y1, y2}, flow2),
                integral(t0, t1, {x1, x2}, {x1, y2}, flow2)
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, ComplicatedIteExpression) {
            auto fs_monster = if_then_else(
                if_then_else(Formula{b1}, x1, x2) < 5,
                if_then_else(Formula{b2}, y1, y2),
                if_then_else(Formula{b2}, z1, z2)
            ) + 1 < if_then_else(
                (x1 < 5) || (x2 < 6) || (y1 > 3) || (z2 == z1),
                tanh(8 * y2 - y1), tanh(9 * z2 - z1)
            );
            std::vector fs{
                b1 || b2 && !b3,
                b4 && !(x2 < tanh(y2 / y1)),
                // nested ITEs
                if_then_else(b3 || (x2 < x1 + y1), tanh(3 * x1), tanh(5 / y1)) < z2,
            };
            std::vector es{
                sqrt(3 * x1) * 5 + y2 * x2 / sin(z2),
                tanh(4 * x1 + 3),
                atan2((y2 - y1), (x2 - x1)),
                min(exp(z1), z2),
                1 / y1, log(z2),
                10 + 15 * x1 + 13.5 * tan(y1) + 8 + 45 * (z1 / pow(z1, x1))
            };

            auto pattern = if_then_else(fs_monster && !fs[1], es[0], es[1]) != es[2];

            // changes alphabetical ordering, which may screw up hashing functions for AND and OR operations
            // changing order of litterals, de-canonicalizing unfortunately, and missing the pattern match
            // todo: figure out if it's even possible or worth fixing this
            ExpressionSubstitution esubs1 = {
                {x1, x2}, {y1, y2}, {z1, z2}, {x2, x1}, {y2, y1}, {z2, z1}
            };
            FormulaSubstitution fsubs1 = {
                {b1, Formula{b2}}, {b2, Formula{b3}}, {b3, Formula{b4}}, {b4, Formula{b1}}
            };
            ExpressionSubstitution esubs2 = {
                {x1, y1}, {y1, z1}, {z1, x1}, {x2, y2}, {y2, z2}, {z2, x2},
            };
            FormulaSubstitution fsubs2 = {
                {b1, Formula{b3}}, {b2, Formula{b4}}, {b3, Formula{b1}}, {b4, Formula{b2}}
            };

            // todo: this is extremely brittle because AND and OR operations can't really be cannonicalized,
            // and the order of operands depends on their alphabetical hash value
            std::vector matches{
                // see note above
                // pattern.Substitute(esubs1),
                pattern.Substitute(fsubs1),
                pattern.Substitute(esubs2),
                pattern.Substitute(fsubs2),
                pattern.Substitute(esubs2, fsubs1),
                // pattern.Substitute(esubs1, fsubs2),
                // pattern.Substitute({{x1, x2}, {x2, x1}}),
                if_then_else(!fs[1] && fs_monster, es[0], es[1]) != es[2]
            };

            std::vector misses{
                pattern.Substitute(z2, x1),
                pattern.Substitute({{x1, x2}, {y1, x2}}),
                // pattern = if_then_else(fs_monster && !(fs[1]), es[0], es[1]) != es[2]
                if_then_else(fs_monster && fs[1], es[0], es[1]) != es[2],
                if_then_else(fs_monster && !fs[0], es[0], es[1]) != es[2],
                if_then_else(fs[2] && !fs[1], es[0], es[1]) != es[2],
                if_then_else(fs[2] && !fs[1], es[1], es[1]) != es[2],
                if_then_else(fs_monster && !(fs[1]), es[1], es[0]) != es[2],
                if_then_else(fs_monster && !(fs[1]), es[0], es[1]) == es[2]
            };
            // pattern is a composite of fs'.. so all should miss
            misses.insert(misses.begin(), fs.begin(), fs.end());
            // pattern is a formula, so all es should miss too
            test_matches_and_misses(pattern, matches, misses, es);
        }

        TEST_F(PatternMatchingTest, SimpleIteExpression) {
            auto pattern = if_then_else(x1 > x2, y1 * 2, tanh(z1 / 3 + 4));

            std::vector matches{
                if_then_else(y1 > y2, z1 * 2, tanh(x1 / 3 + 4)),
                // todo: Canonical-ize GT/LT to just one or the other ?
                // if_then_else(y2 < y1, z1 * 2, tanh(x1 / 3 + 4)),
            };
            std::vector misses{
                if_then_else(x1 <= y1, y1 * 2, tanh(z1 / 3 + 4)),
                if_then_else(Formula{b3}, y1 * 2, tanh(z1 / 3 + 4)),
                if_then_else(x1 > x2, z1 * 2, tanh(z1 / 3 + 4)),
                if_then_else(x1 > x2, y1 * 2, tanh(y1 / 3 + 4)),
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, ComplicatedBooleanFormulas) {
            auto epattern = (x1 < x2) || (x2 > x1) || !((y1 <= y2) && (y2 >= y1));
            auto pattern = !(b1 && b2) || !(b2 || b3) && (b4 || epattern);

            // todo: this is extremely brittle because AND and OR operations can't really be cannonicalized,
            // and the order of operands depends on their alphabetical hash value
            const Variable b5{"b5", Variable::Type::BOOLEAN}; // so substitutions stay alphabetical...
            const Variable b6{"b6", Variable::Type::BOOLEAN};
            FormulaSubstitution fsubs1 = {
                {b1, Formula{b2}}, {b2, Formula{b3}}, {b3, Formula{b4}}, {b4, Formula{b5}}
            };
            ExpressionSubstitution esubs1 = {
                {x1, x2}, {y1, y2}, {z1, z2}, {x2, x1}, {y2, y1}, {z2, z1}
            };
            FormulaSubstitution fsubs2 = {
                {b1, Formula{b3}}, {b2, Formula{b4}}, {b3, Formula{b5}}, {b4, Formula{b6}}
            };

            std::vector matches{
                !(b1 && b2) || !(b2 || b3) && (b4 || (x1 < x2) || (x2 > x1) || !((y1 <= y2) && (y2 >= y1))),
                // exact match
                !(b1 && b2) || !(b2 || b3 || Formula::False()) && (b4 || (x1 < x2) || (x2 > x1) || !((y1 <= y2) && (y2
                    >= y1))),
                !(b1 && b2 && Formula::True()) || !(b2 || b3) && (b4 || (x1 < x2) || (x2 > x1) || !((y1 <= y2) && (y2 >=
                    y1))),
                pattern.Substitute(fsubs1),
                pattern.Substitute(esubs1, fsubs1),
                pattern.Substitute(esubs1),
                pattern.Substitute(fsubs2),
            };
            std::vector misses{
                // pattern = !(b1 && b2) || !(b2 || b3) && (b4 || (x1 < x2) || (x2 > x1) || !((y1 <= y2) && (y2 >= y1))),
                // exercise Gt, Geq, Lt, Leq...
                !(b1 && b2) || !(b2 || b3) && (b4 || (x1 < x2) || (x2 > x1) || !((y1 < y2) && (y2 >= y1))),
                !(b1 && b2) || !(b2 || b3) && (b4 || (x1 <= x2) || (x2 > x1) || !((y1 <= y2) && (y2 >= y1))),
                !(b1 && b2) || !(b2 || b3) && (b4 || (x1 < x2) || (x2 > x1) || !((p1 <= y2) && (y2 >= p1))),
                !(b4 && b2) || !(b2 || b3) && (b4 || (x1 < x2) || (x2 > x1) || !((y1 <= y2) && (y2 >= y1))),
                !(b4 && b2) || !(b2 || b3) && (b4 || (x1 < x2) || (x2 > x1) || !((y1 <= y2) && (y2 >= y1))),
                Formula{b1},
                Formula::True(),
                Formula::False(),
                !(b1 && b2) && !(b2 || b3) && (b4),
                !(b1 && b2) || !(b2 && b3) && (b4),
                !(b1 && b2) || (b2 || b3) && (b4),
                !(b1 && b2) || !(b2 || b3) && (b3)
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, NestedComplicatedAddMulFormulas) {
            const Variable z1{"z1", Variable::Type::INTEGER};
            const Variable z2{"z2", Variable::Type::INTEGER};
            const Variable z3{"z3", Variable::Type::INTEGER};
            const Variable z4{"z4", Variable::Type::INTEGER};
            const Variable z5{"z5", Variable::Type::INTEGER};
            const Variable z6{"z6", Variable::Type::INTEGER};

            // aims to find Continuation-Passing-Style related bugs in lambda capture of iterator references/objects
            // like those found in `NaryOpMatchHelper` by `ComplicatedBooleanFormulas` test. 
            auto epattern = (x1 / x2) + max(x2, x1) + -(atan2(y1, y2) * min(y2, y1));
            auto pattern = -(z1 * z2) + -(z2 + z3) * (z4 + epattern);

            // todo: this is extremely brittle because MUL and ADD operations can't really be cannonicalized,
            // and the order of operands depends on their alphabetical hash value
            ExpressionSubstitution fsubs1 = {
                {z1, z2}, {z2, z3}, {z3, z4}, {z4, z5}
            };
            ExpressionSubstitution esubs1 = {
                {x1, x2}, {y1, y2}, {x2, x1}, {y2, y1},
            };
            ExpressionSubstitution fsubs2 = {
                {z1, z3}, {z2, z4}, {z3, z5}, {z4, z6}
            };

            std::vector matches{
                -(z1 * z2) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * (min(y2, y1)))),
                // exact match
                -(z1 * z2) + -(z2 + z3 + 0) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * min(y2, y1))),
                -(z1 * z2 * 1) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * min(y2, y1))),
                pattern.Substitute(fsubs1),
                pattern.Substitute(esubs1).Substitute(fsubs1),
                pattern.Substitute(esubs1),
                pattern.Substitute(fsubs2),
            };
            std::vector misses{
                // pattern = -(z1 * z2) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * (min(y2, y1)))),
                -(z1 * z2) + -(z2 + z3 + 0.1) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * min(y2, y1))),
                -(z1 * z2 * 0.9) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * min(y2, y1))),

                -(z1 * z2) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((y1 / y2) * (min(y2, y1)))),
                -(z1 * z2) + -(z2 + z3) * (z4 + (atan2(x1, x2)) + (max(x2, x1)) + -((atan2(y1, y2)) * (min(y2, y1)))),
                -(z1 * z2) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(p1, y2)) * (min(y2, p1)))),
                -(z4 * z2) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * (min(y2, y1)))),
                -(z4 * z2) + -(z2 + z3) * (z4 + (x1 / x2) + (max(x2, x1)) + -((atan2(y1, y2)) * (min(y2, y1)))),
                -(z1 * z2) * -(z2 + z3) * (z4),
                -(z1 * z2) + -(z2 * z3) * (z4),
                -(z1 * z2) + (z2 + z3) * (z4),
                -(z1 * z2) + -(z2 + z3) * (z3)
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, SimpleBooleanFormulas) {
            auto pattern = (b1 || !b2 || !b3) && b2 && b3;
            std::vector matches{
                (b1 || !b2 || !b3) && b2 && b3,
                (b1 || !b2 || !b3 || Formula::False()) && b2 && b3,
                (b1 || !b2 || !b3 || Formula::False()) && b2 && b3 && Formula::True(),
                (b2 || !b3 || !b4) && b3 && b4 // brittle due to hashing
            };
            std::vector misses{
                Formula{b1},
                b1 || b2, b3 && !b4,
                (b1 || !b4 || !b3) && b2 && b3,
                (b2 || b3) && b3 && !b4,
                !((b1 || b2) && b3 && !b4),
                (b1 || b2) && b3 && b4,
                (b1 && b2) || b3 && !b4,
                Formula::True(),
                Formula::False(),
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, ComplicatedMultiplicationExpression) {
            std::vector es{
                tanh(4 * x1 + 3),
                atan2((y2 - y1), (x2 - x1)),
                sqrt(3 * x1) * 5 + y2 * x2 / sin(z2),
                min(exp(z1), z2),
                1 / y1, log(z2)
            };
            auto pattern = sqrt(5) * pow(es[0], es[1]) * pow(es[2], es[3]) * pow(es[4], es[5]);

            ExpressionSubstitution esubs1 = {
                {x1, x2}, {y1, y2}, {z1, z2}, {x2, x1}, {y2, y1}, {z2, z1}
            };
            ExpressionSubstitution esubs2 = {
                {x1, y1}, {y1, z1}, {z1, x1}, {x2, y2}, {y2, z2}, {z2, x2}
            };
            std::vector matches{
                pattern.Substitute(esubs1),
                pattern.Substitute(esubs2),
                pattern.Substitute({{x1, x2}, {x2, x1}}),

                pow(es[4], es[5]).Substitute(esubs1) *
                sqrt(5).Substitute(esubs1) *
                pow(es[2], es[3]).Substitute(esubs1) *
                pow(es[0], es[1]).Substitute(esubs1),

                pow(es[4], es[5]).Substitute(esubs2) *
                (
                    pow(es[0], es[1]) *
                    pow(es[2], es[3])
                ).Substitute(esubs2) *
                sqrt(5).Substitute(esubs2)
            };

            std::vector misses{
                sqrt(5) * pow(es[0], es[1]) * pow(es[2], es[3]),
                sqrt(5) * pow(es[0], es[1]) * pow(es[2], es[3]) * pow(es[4], es[5]) * pow(es[4] + es[0], es[5] + es[1]),
                2 * pow(x1, 4) * pow(5, x2),
                3 * pow(x1, 4) * pow(5, x1),
                3 * pow(4, x1) * pow(x2, 5),
                10 + 15 * x1 + 13.5 * tan(y1) + 8 + 45 * (z1 / pow(z1, x1)),
                pattern.Substitute(z2, x1),
                pattern.Substitute({{x1, x2}, {y1, x2}}),

                pow(es[4], es[5]).Substitute(esubs1) *
                sqrt(6).Substitute(esubs1) * // changed from 5 to 6
                pow(es[2], es[3]).Substitute(esubs1) *
                pow(es[0], es[1]).Substitute(esubs1),

                pow(es[4], es[5]).Substitute(esubs1) *
                (
                    pow(es[0], es[1]) *
                    pow(es[2], es[3])
                ).Substitute(esubs2) *
                sqrt(5).Substitute(esubs2),

                sqrt(5) * pow(es[0], es[1]) * pow(es[3], es[2]) * pow(es[4], es[5])
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, SimpleMultiplicationExpression) {
            auto pattern = 3 * pow(x1, 4) * pow(5, x2);

            std::vector matches{
                3 * pow(y1, 4) * pow(5, y2),
                pow(5, z2) * 3 * pow(z1, 4),
                pow(5, z2) * 1 * pow(x1, 4) * 3,
            };
            std::vector misses{
                3 * pow(x1, 4),
                2 * pow(x1, 4) * pow(5, x2),
                3 * pow(x1, 4) * pow(5, x1),
                3 * pow(4, x1) * pow(x2, 5)
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, ComplicatedAdditionExpression) {
            auto pattern = 10 + 15 * x1 + 13.5 * tan(y1) + 8 + 45 * (z1 / pow(z1, x1));

            std::vector matches{
                10 + 15 * x2 + 13.5 * tan(y2) + 8 + 45 * (z2 / pow(z2, x2)),
                45 * (z1 / pow(z1, x2)) + 10 + 15 * x2 + 13.5 * tan(y2) + 8,
                45 * (z2 / pow(z2, x1)) + 10 + 5 * x1 + 10 * x1 + 13.5 * tan(y2) + 8,
            };
            std::vector misses{
                10 + 13.5 * tan(y1) + 8 + 45 * (z1 / pow(z1, x1)),
                10 + 15 * x1 + 13.5 * tan(y1) + 8 + 45 * (z1 / pow(z1, x1)) + rc(10) * z2,
                9 + 15 * x1 + 13.3 * tan(y1) + 8 + 45 * (1 / pow(z1, 2)),
                10 + 15 * x1 + 13.5 * tan(y1) + 8 + 45 * (1 / pow(z1, 3)),
                10 + 15 * x2 + 13.5 * tan(y2) + 8 + 45 * (z2 / pow(x2, 2)),
                10 + 15 * x1 + 13.5 * tan(y1) + 8 + 45 * (z1 / pow(z1, y1)),
                10 + 15 * x1 + 13.5 * tan(y1) + 8 + 45 * (z1 / pow(z1, tan(y1)))
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, SimpleRealConstantAdditionExpression) {
            auto pattern = 3 + rc(4, true) * x1 + 5 * x2;

            std::vector matches{
                3 + rc(4, true) * y1 + 5 * y2,
                3 + rc(4, true) * z1 + 5 * z2,
                2 + 5 * x2 + rc(4, true) * z1 + 1,
            };
            std::vector misses{
                3 + rc(4, true) * x1,
                3 + rc(4, false) * x1 + 5 * x2,
                3 + 4 * x1 + 5 * x2,
                2 + 4 * x1 + 5 * x2,
                3 + 4 * x1 + 5 * x1
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, SimpleAdditionExpression) {
            auto pattern = 3 + 4 * x1 + 5 * x2;

            std::vector matches{
                3 + 4 * y1 + 5 * y2,
                3 + 4 * z1 + 5 * z2,
                2 + 5 * x2 + 4 * z1 + 1,
            };
            std::vector misses{
                3 + 4 * x1,
                2 + 4 * x1 + 5 * x2,
                3 + 4 * x1 + 5 * x1
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, BinaryExpression) {
            auto pattern = x1 / pow(1 + y1, z1);

            std::vector matches{
                x2 / pow(1 + y2, z2),
                x1 / pow(1 + y2, z2),
                x2 / pow(1 + y1, z2)
            };
            std::vector misses{
                x1 / pow(y1, 1 + z1),
                x1 / pow(z1, 1 + y1),
                pow(1 + y1, z1) / x1,
                x1 / pow(1 + x1, x1),
                x1 / pow(1 + y1, x1),
                x1 / pow(1 + x1, z1),
                z1 / pow(1 + y1, z1)
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, SimpleRealConstantExpression) {
            auto pattern = tan(x1 / rc(0.5));

            std::vector matches{
                tan(x1 / rc(0.5)),
                tan(z1 / rc(0.5)),
            };
            std::vector misses{
                tan(rc(0.5) / x1),
                tan(p1 / rc(0.5)), tan(x1 / rc(-0.6)),
                tan(z1 / rc(0.5, true)),
                tan(x1 / rc(1)),
                tan(z1 / rc(1E9)),
            };

            test_matches_and_misses(pattern, matches, misses);
        }

        TEST_F(PatternMatchingTest, BinaryAndUnaryOpsComplete) {
            uint64_t random_state = 0;
            DeBruijnCanonicalizer<Expression> trie;
            std::vector es{ // for coverage... make sure we didn't mess up call of the unary/binary op helper functions.
                x1 / x2, log(x1), abs(x1), exp(x1), sqrt(x1), pow(x1, x2), sin(x1), cos(x1), tan(x1), asin(x1),
                acos(x1), atan(x1), atan2(x1, x2), sinh(x1), cosh(x1), tanh(x1), min(x1, x2), max(x1, x2)
            };
            for (const auto& e : es) trie.insert(e);
            for (const auto& pattern : es) {
                std::vector matches{
                    pattern.Substitute({{x1, z1}, {z1, x1}}),
                };
                std::vector misses{
                    pattern.Substitute({{x1, es[0]}}),
                    pattern.Substitute({{x1, es[1]}}),
                    pattern.Substitute({{x1, es[2]}}),
                    pattern.Substitute({{x1, p1}, {x2, p1}}),
                    pattern.Substitute({{x1, p1}, {x2, p2}}),
                };
                for (const auto& match : matches) {
                    const auto found = trie.find_matches(match, Box{}, false, random_state).first;
                    EXPECT_EQ(found.size(), 1);
                    EXPECT_EQ(found[0].second, std::nullopt); // trie.find_matches(..., return_subs_maps=false)
                    std::cout << pattern << " MATCHES " << match << std::endl;
                }
                for (const auto& miss : misses) {
                    const auto found = trie.find_matches(miss, Box{}, false, random_state).first;
                    EXPECT_EQ(found.size(), 0);
                    std::cout << pattern << " MISSES " << miss << std::endl;
                }
            }
        }

        TEST_F(PatternMatchingTest, UnaryExpression) {
            auto pattern = tan(x1);

            std::vector matches{
                tan(x1), tan(x2), tan(y2), tan(z2)
            };
            std::vector misses{
                tan(p1),
                sin(x2), cos(y2), tan(x1 + x2),
                tan(tan(x1)), 1 / tan(x1), tan(x1) + 1,
            };

            test_matches_and_misses(pattern, matches, misses);
        }
    } // namespace
} // namespace dreal
