//
// Created by Kunal Sheth on 4/1/25.
//

#include "substitutions_map.h"

#include <unordered_set>
#include <utility>
#include <dreal/util/assert.h>
#include <dreal/util/logging.h>

namespace dreal
{
    substitutions_map::substitutions_map(const Box& b, const size_t reserved_size) : box(b) {
        reserve(reserved_size);
    }

    void substitutions_map::push() {
        index_stack.emplace_back(mapping.size());
    }

    void substitutions_map::pop() {
        DREAL_ASSERT(!index_stack.empty());
        DREAL_ASSERT(index_stack.back() <= mapping.size());
        mapping.resize(index_stack.back());
        index_stack.pop_back();
    }

    substitutions_map::substitution_status substitutions_map::attempt_substitution(
        const Variable& a, const Variable& aP
    ) {
        if (a.get_type() != aP.get_type()) return TYPE_MISS;
        // Membership first: Box::operator[] throws on a variable not in the box
        // (it used to default-insert into the shared name->index map — silent
        // poison). Mixed membership cannot certify a domain fit -> BOX_MISS.
        // Both absent (e.g. the PM tests drive find_matches with an empty Box):
        // no domains to compare — matching stays structural.
        const bool a_in{box.has_variable(a)};
        const bool aP_in{box.has_variable(aP)};
        if (a_in != aP_in) return BOX_MISS;
        if (a_in && box[a] != box[aP]) return BOX_MISS; // todo: subset / superset ?

        for (const auto& [mA, mAP] : mapping) {
            const bool eqA = mA.equal_to(a);
            const bool eqAP = mAP.equal_to(aP);

            if (eqA && eqAP) return SUCCESS;
            if (eqA || eqAP) {
                DREAL_LOG_TRACE("Substitution failed. Attempted ({} <=> {}) conflicts with existing ({} <=> {}).",
                                a.get_name(), aP.get_name(), mA.get_name(), mAP.get_name());
                return BIJ_MISS;
            }
        }

        mapping.emplace_back(a, aP);
        return SUCCESS;
    }

    size_t substitutions_map::size() const {
        return mapping.size();
    }

    void substitutions_map::reserve(const size_t n) {
        mapping.reserve(n);
        index_stack.reserve(n);
    }

    // "forward" takes the matched and maps it to original
    // "backward" takes original and maps it to matched
    template <typename T>
    T substitutions_map::apply_substitution(const T& f, const substitutions_map& subs, bool backward) {
        ExpressionSubstitution esub;
        FormulaSubstitution fsub;
        for (const auto& [fst,snd] : subs.mapping) {
            const auto& a = backward ? snd : fst;
            const auto& aP = backward ? fst : snd;
            if (a.get_type() == Variable::Type::BOOLEAN) fsub.emplace(a, Formula{aP});
            else esub.emplace(a, aP);
        }
        return f.Substitute(esub, fsub);
    }

    // Force template code generation into this translation unit.
    template Expression substitutions_map::apply_substitution<Expression>(const Expression& f, const substitutions_map& subs, bool backward);
    template Formula substitutions_map::apply_substitution<Formula>(const Formula& f, const substitutions_map& subs, bool backward);
}