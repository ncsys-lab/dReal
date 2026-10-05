//
// Created by Kunal Sheth on 8/24/25.
//

#ifndef DREAL4_CMAKE_ODEFLOW_H
#define DREAL4_CMAKE_ODEFLOW_H

#include <string>

#include "dreal/symbolic/symbolic_expression.h"
#include "dreal/symbolic/symbolic_variable.h"

namespace dreal::drake::symbolic
{
    class OdeFlow
    {
    public:
        const std::string name;
        const std::vector<std::pair<Variable, Expression>> ode_list;

        OdeFlow(std::string flow_id, const std::vector<std::pair<Variable, Expression>>& ode_list);

        const VariableSet& get_ode_vars() const { return ode_vars; }
        const VariableSet& get_ode_pars() const { return ode_pars; }
        bool is_var(const Variable& v) const { return ode_vars.count(v) > 0; }
        bool is_par(const Variable& v) const { return ode_pars.count(v) > 0; }

        friend bool operator==(const OdeFlow& lhs, const OdeFlow& rhs);
        bool operator!=(const OdeFlow& flow) const { return !(*this == flow); }

    private:
        VariableSet ode_vars;
        VariableSet ode_pars;
    };
}

#endif //DREAL4_CMAKE_ODEFLOW_H
