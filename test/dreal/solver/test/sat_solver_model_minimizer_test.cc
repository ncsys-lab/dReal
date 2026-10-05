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
#include <dreal/solver/sat_solver.h>

#include <gtest/gtest.h>

#include "dreal/solver/config.h"
#include "dreal/symbolic/symbolic.h"

namespace dreal
{
    namespace
    {
        class SatSolverModelMinimizerTest : public ::testing::Test
        {
        protected:
            const Variable b1{"b1", Variable::Type::BOOLEAN};
            const Variable b2{"b2", Variable::Type::BOOLEAN};
            const Variable b3{"b3", Variable::Type::BOOLEAN};
            const Variable b4{"b4", Variable::Type::BOOLEAN};
            const Variable b5{"b5", Variable::Type::BOOLEAN};
            const Variable b6{"b6", Variable::Type::BOOLEAN};
            const Variable b7{"b7", Variable::Type::BOOLEAN};
            const Variable b8{"b8", Variable::Type::BOOLEAN};
        };

        Formula xor_of(const Formula& a, const Formula& b) {
            return (a || b) && (!a || !b);
        }
        Formula xor_of(const Variable& a, const Variable& b) {
            return xor_of(Formula{a}, Formula{b});
        }


        TEST_F(SatSolverModelMinimizerTest, Xor1) {
            SatSolver cdcl{Config()};

            cdcl.AddFormula(xor_of(b1, b2));
            cdcl.AddFormula(Formula{b1});
            const auto model = cdcl.CheckSat(false);
            EXPECT_TRUE(model);

            VariableMap<bool> assignments;
            for (const auto& kv : model->first.first) assignments.emplace(kv);

            EXPECT_EQ(assignments.size(), 2);
            EXPECT_TRUE(
                assignments[b1] & !assignments[b2]
            );
        }

        TEST_F(SatSolverModelMinimizerTest, Xor2) {
            SatSolver cdcl{Config()};
            cdcl.AddFormula(xor_of(b1, b2));
            const auto model = cdcl.CheckSat(false);
            EXPECT_TRUE(model);

            VariableMap<bool> assignments;
            for (const auto& kv : model->first.first) assignments.emplace(kv);

            EXPECT_EQ(assignments.size(), 2);
            EXPECT_TRUE(
                assignments[b1] ^ assignments[b2]
            );
        }

        TEST_F(SatSolverModelMinimizerTest, Or1) {
            SatSolver cdcl{Config()};
            cdcl.AddFormula(b1 || b2);
            const auto model = cdcl.CheckSat(false);
            EXPECT_TRUE(model);

            VariableMap<bool> assignments;
            for (const auto& kv : model->first.first) assignments.emplace(kv);

            EXPECT_EQ(assignments.size(), 1);
            EXPECT_TRUE(assignments.count(b1) ^ assignments.count(b2));
            if (assignments.count(b1)) EXPECT_TRUE(assignments[b1]);
            if (assignments.count(b2)) EXPECT_TRUE(assignments[b2]);
        }

        TEST_F(SatSolverModelMinimizerTest, Or2) {
            SatSolver cdcl{Config()};
            cdcl.AddFormula(b1 || b2);
            cdcl.AddFormula(b1 || b2 || b3);
            const auto model = cdcl.CheckSat(false);
            EXPECT_TRUE(model);

            VariableMap<bool> assignments;
            for (const auto& kv : model->first.first) assignments.emplace(kv);

            EXPECT_EQ(assignments.size(), 1);
            EXPECT_TRUE(assignments.count(b1) ^ assignments.count(b2));
            if (assignments.count(b1)) EXPECT_TRUE(assignments[b1]);
            if (assignments.count(b2)) EXPECT_TRUE(assignments[b2]);
            EXPECT_EQ(assignments.count(b3), 0);
        }

        TEST_F(SatSolverModelMinimizerTest, Or3) {
            SatSolver cdcl{Config()};
            cdcl.AddFormula(b1 || b2);
            cdcl.AddFormula(b1 || !b2);
            const auto model = cdcl.CheckSat(false);
            EXPECT_TRUE(model);

            VariableMap<bool> assignments;
            for (const auto& kv : model->first.first) assignments.emplace(kv);

            EXPECT_EQ(assignments.size(), 1);
            EXPECT_TRUE(assignments.count(b1) == 1);
            EXPECT_TRUE(assignments.count(b2) == 0);
            EXPECT_TRUE(assignments[b1]);
        }

        TEST_F(SatSolverModelMinimizerTest, Or4) {
            SatSolver cdcl{Config()};
            cdcl.AddFormula(b1 || b2 || b3 || b4);
            cdcl.AddFormula(b5 || b6 || b7 || b8);
            const auto model = cdcl.CheckSat(false);
            EXPECT_TRUE(model);

            VariableMap<bool> assignments;
            for (const auto& kv : model->first.first) assignments.emplace(kv);
            EXPECT_EQ(assignments.size(), 2);
            for (const auto & [k,v] : assignments) EXPECT_TRUE(v);
        }

        TEST_F(SatSolverModelMinimizerTest, Or5) {
            SatSolver cdcl{Config()};
            cdcl.AddFormula(b1 || b2 || b3 || b4);
            cdcl.AddFormula(b1 || b6 || b7 || b8);
            const auto model = cdcl.CheckSat(false);
            EXPECT_TRUE(model);

            VariableMap<bool> assignments;
            for (const auto& kv : model->first.first) assignments.emplace(kv);
            EXPECT_LE(assignments.size(), 2);
        }

        // update: gave up on the idea of "perfect" partial models...
        // see comments inside `SatSolver::CheckSat()` for details.

        // TEST_F(SatSolverModelMinimizerTest, FlippableMasking) {
        //     SatSolver cdcl{Config()};
        //     // order of variable initialization matters in internal data structures
        //     cdcl.AddFormula(b1 || !b1);
        //     cdcl.AddFormula(b2 || !b2);
        //
        //     // mess with private members to try and replicate this
        //     // happens extremely rarely on larger problems
        //     cdcl.AddFormula(b1 || b2);
        //
        //     // force it to be a particular model to trigger it.
        //     cdcl.cadical->assume(-cdcl.to_sat_var_[b1.get_id()]);
        //     cdcl.cadical->assume(+cdcl.to_sat_var_[b2.get_id()]);
        //
        //     auto model = cdcl.CheckSat(false);
        //     EXPECT_TRUE(model);
        //     VariableMap<bool> assignments;
        //     for (const auto& kv : model->first.first) assignments.emplace(kv);
        //     EXPECT_EQ(assignments.size(), 1);
        //     EXPECT_TRUE(assignments[b2]);
        // }

        // TEST_F(SatSolverModelMinimizerTest, Flippable3rdPass) {
        //     // cannot replicate :(
        //     // but it does happen.
        //
        //     SatSolver cdcl{Config()};
        //     // order of variable initialization matters in internal data structures
        //     cdcl.AddFormula(b1 || !b1);
        //     cdcl.AddFormula(b2 || !b2);
        //
        //     // mess with private members to try and replicate this
        //     // happens extremely rarely on larger problems
        //     cdcl.AddFormula(b1 || b2 || b3);
        //
        //     // force it to be a particular model to trigger it.
        //     cdcl.cadical->assume(+cdcl.to_sat_var_[b1.get_id()]);
        //     cdcl.cadical->assume(-cdcl.to_sat_var_[b2.get_id()]);
        //     cdcl.cadical->assume(+cdcl.to_sat_var_[b3.get_id()]);
        //
        //     auto model = cdcl.CheckSat(false);
        //     EXPECT_TRUE(model);
        //     VariableMap<bool> assignments;
        //     for (const auto& kv : model->first.first) assignments.emplace(kv);
        //     EXPECT_EQ(assignments.size(), 1);
        //     EXPECT_TRUE(assignments[b2]);
        // }
    } // namespace
} // namespace dreal
