//
// Created by Kunal Sheth on 3/30/25.
//

#include "auditor.h"

#include "dreal/solver/context_impl.h"

#include <limits>
#include <ostream>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <dreal/symbolic/prefix_printer.h>
#include <dreal/symbolic/symbolic_formula_cell.h>

#include <fmt/format.h>
#include <utility>

#include "dreal/solver/filter_assertion.h"
#include "dreal/util/assert.h"
#include "dreal/util/logging.h"
#include "dreal/util/pattern_matching/substitutions_map.h"
#include "dreal/version.h"
#include "dreal/symbolic/odes/symbolic_odes_cell.h"
#include "nlohmann/json.hpp"

#include "dreal/util/json_guarded.h"

namespace dreal
{
    std::ofstream literal_log("/tmp/dreal_audit_sat_literal_log.txt");
    std::set<int> literal_log_clause;

    void sat_log_label_clause(const std::string& s) {
        if (!DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) return;
        literal_log << s << '\n';
    }

    void sat_log_literal(const int lit, const std::optional<Variable>& def) {
        if (!DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) return;
        DREAL_ASSERT(lit != 0);

        static VariableSet seen_literals;
        if (def && !seen_literals.count(*def)) {
            literal_log << "def:\t" << ::abs(lit) << " := " << *def << '\n';
            seen_literals.emplace(*def);
        }

        literal_log_clause.insert(lit);
    }

    void sat_log_literal0() {
        if (!DREAL_EXPERIMENTAL_SAT_AUDIT_ENABLED) return;
        for (int v : literal_log_clause) literal_log << v << ' ';
        literal_log << '0' << '\n';
        literal_log_clause.clear();
    }

    void theory_audit_literals(
        const std::string& label,
        const PredicateAbstractor& pa, /*copy*/ std::vector<Formula> lits, const std::optional<Box>& box
    ) {
        // the operands passed here are expensive to produce, so really shouldn't be called unnecessarily.
        DREAL_ASSERT(DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED);

        for (auto& l : lits) {
            if (is_variable(l)) {
                // f = b
                const auto iter = pa.var_to_formula_map().find(get_variable(l));
                if (iter != pa.var_to_formula_map().end()) l = iter->second;
                // else ...  the variable is probably, actually, a BOOL such as `DigitalPDController_0__omega_sensor_bit0__t0ad`
            }
            else {
                // f = ¬b
                DREAL_ASSERT(is_negation(l) && is_variable(get_operand(l)));
                const auto iter = pa.var_to_formula_map().find(get_variable(get_operand(l)));
                if (iter != pa.var_to_formula_map().end()) l = !iter->second;
                // else ...  the variable is probably, actually, a BOOL such as `DigitalPDController_0__omega_sensor_bit0__t0ad`
            }
        }
        theory_audit_formula(label, make_disjunction(lits), box);
    }

    void get_odeflows(const Formula& f, const std::function<void(const std::shared_ptr<const OdeFlow>&)>& callback) {
        if (!f.include_ode()) { /*no-op*/ }
        else if (is_forallT(f)) callback(to_forallT(f)->get_flow());
        else if (is_integral(f)) callback(to_integral(f)->get_flow());
        else if (is_negation(f)) get_odeflows(get_operand(f), callback);
        else if (is_conjunction(f) || is_disjunction(f)) for (const auto& op : get_operands(f)) get_odeflows(op, callback);
        else
            DREAL_UNREACHABLE();
    }

    void theory_audit_formula(const std::string& label, const Formula& formula, const std::optional<Box>& box) {
        // the operands passed here are expensive to produce, so really shouldn't be called unnecessarily.
        DREAL_ASSERT(DREAL_EXPERIMENTAL_THEORY_AUDIT_ENABLED);

        auto /*copy*/ vs = formula.GetFreeVariables();

        std::set<std::shared_ptr<const OdeFlow>> flows;
        get_odeflows(formula, [&](const std::shared_ptr<const OdeFlow>& _) { flows.insert(_); });
        for (const auto& odeflow : flows) for (const auto& [var, _] : odeflow->ode_list) vs.insert(var);

        std::string lemma_comment;
        {
            std::ostringstream s;
            s << ";\t" << label << "\t∀ ";
            for (const auto& v : vs) {
                s << v;
                if (box) s << "∈" << (*box)[v];
                s << ", ";
            }
            s << "\n;\t\t" << formula;
            lemma_comment = s.str();
        }

        std::cout << lemma_comment << '\n'; // todo: gate with macro or flag and use spdlog

        static int audit_no = 0;
        std::ofstream myfile;
        myfile.open("/tmp/dreal_audit/lemma" + std::to_string(audit_no++) + ".smt2");  // lint: allow int

        myfile << "\n" << lemma_comment << "\n\n";
        myfile << "(set-logic QF_NRA_ODE)\n";
        for (const auto& v : vs) {
            myfile << "(declare-const " << v << " Real)\n";
            if (box) {
                const auto& boxv = (*box)[v]; // todo: confirm inclusive v.s. exclusive
                if (isfinite(boxv.lb())) myfile << "(assert " << ToPrefix(boxv.lb() <= v) << " )\n";
                if (isfinite(boxv.ub())) myfile << "(assert " << ToPrefix(v <= boxv.ub()) << " )\n";
            }
        }
        for (const auto& odeflow : flows) myfile << ToPrefix(*odeflow) << '\n';

        myfile << "(assert " << ToPrefix(!formula) << " )\n";

        myfile << "(check-sat)(exit)" << '\n';
        myfile.flush();
        myfile.close();
    }

    void pm_dump_all(
        const std::vector<Formula>& base_conflict,
        const std::vector<std::pair<std::vector<Formula>, std::optional<substitutions_map>>>& all_matches,
        const std::vector<std::string_view>& metadata_tags, const std::vector<bool>& was_not_added
    ) {
        DREAL_ASSERT(DREAL_EXPERIMENTAL_PM_DUMP_ALL_ENABLED);
        DREAL_ASSERT(metadata_tags.size() == all_matches.size() || metadata_tags.empty());
        DREAL_ASSERT(was_not_added.size() == was_not_added.size() || was_not_added.empty());

        nlohmann::json dump;

        dump["dump_time_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        std::vector<std::string> base_conflict_str;
        base_conflict_str.reserve(base_conflict.size());
        for (const auto& a : base_conflict) base_conflict_str.emplace_back(a.to_string());
        dump["base_conflict"] = base_conflict_str;

        std::vector<std::tuple<std::vector<std::string>, std::map<std::string, std::string>, std::string_view, bool>> all_matches_json;
        all_matches_json.reserve(all_matches.size());
        for (int i = 0; i < all_matches.size(); i++) {
            const auto& [match_conflict, subs] = all_matches[i];
            std::vector<std::string> match_conflict_str;
            match_conflict_str.reserve(match_conflict.size());
            for (const auto& a : match_conflict) match_conflict_str.emplace_back(a.to_string());

            std::map<std::string, std::string> subs_strs;
            for (const auto& [a,b] : subs->get_map()) subs_strs[a.get_name()] = b.get_name();

            const auto& tag = metadata_tags.empty() ? "?" : metadata_tags.at(i);
            const auto& wna = was_not_added.empty() ? false : was_not_added.at(i);

            all_matches_json.emplace_back(match_conflict_str, subs_strs, tag, wna);
        }
        dump["all_matches"] = all_matches_json;
        const NearestRoundingScope nearest_scope;
        const auto dump_str = dump_json(dump, nearest_scope.token(), -1, ' ', true);
        std::cout << "; pm_dump_all = " << dump_str << '\n';

        static int audit_no = 0;
        std::ofstream myfile;
        myfile.open("/tmp/dreal_audit/pm" + std::to_string(audit_no++) + ".json");  // lint: allow int
        myfile << dump_str << '\n';
        myfile.flush();
        myfile.close();
    }
}
