# QF_NRA_ODE Semantics

This document gives a ground-up description of the `QF_NRA_ODE` logic as implemented in dReal4.
Every claim is cross-referenced to the parser (`src/dreal/smt2/parser.yy`, `scanner.ll`), the driver
(`src/dreal/smt2/driver.cc`), the symbolic layer
(`src/third_party/com_github_robotlocomotion_drake/dreal/symbolic/odes/`), and the contractor
(`src/dreal/contractor/odes/contractor_odes.cc`, `contractor_odes_capd.cc`).

---

## 1. Setting the Logic

```smt2
(set-logic QF_NRA_ODE)
```

The logic string `QF_NRA_ODE` is parsed by `dreal::parse_logic` (`src/dreal/smt2/logic.h`). It selects
quantifier-free nonlinear real arithmetic augmented with ODE-specific keywords. All four ODE tokens are
accepted in any file regardless of whether `set-logic` was called (the scanner always emits them), but
tools and users should declare `QF_NRA_ODE` to signal intent.

---

## 2. Variable Declarations

Variables are declared with the standard SMT2-LIB `declare-fun` or `declare-const` commands:

```smt2
(declare-fun x_0 () Real)
(declare-fun x_t () Real [0.0, 10.0])   ; with domain bounds
```

**Convention**: dReal4 does not enforce any naming convention, but every ODE-based benchmark uses
the pattern `<name>_<mode>_0` and `<name>_<mode>_t` for initial and final values of `<name>` in
`<mode>`. The `_0` / `_t` suffix is parsed purely as a user naming convention — the solver does not
assign special meaning to the suffix. The variable-extraction logic in `contractor_odes.cc`
(`extract_step`) does parse the `_<step>_` part of the name for trace JSON output, but this has no
effect on semantics.

The time variable for mode `k` is conventionally named `time_k`. Time variables must be real-valued;
dReal4 does not enforce a non-negativity constraint automatically — you must declare it explicitly:

```smt2
(declare-fun time_0 () Real)
(assert (>= time_0 0.0))
(assert (<= time_0 5.0))
```

---

## 3. `define-ode`: Flow Definitions

```smt2
(define-ode flow_1 (
    (= d/dt[x] e_x)
    (= d/dt[v] e_v)
))
```

### 3.1 Grammar

From `parser.yy`:

```
command_define_ode:
    '(' TK_DEFINEODE SYMBOL '(' ode_list ')' ')'
        { driver.DefineOde($3, $5); }

ode: '(' TK_EQ TK_DDT '[' SYMBOL ']' term ')'
        { $$.first  = Variable{driver.lookup_variable($5)};
          $$.second = $7.expression(); }
```

The scanner (`scanner.ll`) emits `TK_DEFINEODE` for `define-ode` and `TK_DDT` for `d/dt`.
The `[` `SYMBOL` `]` part names the state variable by looking it up with `driver.lookup_variable`.
This means **the variable named by `d/dt[x]` must already be declared** at the point the
`define-ode` command is parsed.

### 3.2 ODE variables vs. ODE parameters

`driver.DefineOde` calls `OdeFlow::OdeFlow(flow_id, ode_list)` (`OdeFlow.cc`):

```cpp
for (const auto& [ode_var, ode_rhs] : ode_list) {
    if (is_constant(ode_rhs, 0)) ode_pars.insert(ode_var);
    else ode_vars.insert(ode_var);
}
```

A variable whose RHS is the **exact constant `0`** (checked via `is_constant(rhs, 0)`) is classified
as an **ODE parameter** (`ode_pars`). Every other variable is an **ODE state variable** (`ode_vars`).

This distinction matters for the contractor:

- **State variables** (`ode_vars`): integrated by CAPD (`run_capd_fwd`, `contractor_odes_capd.cc`);
  their final-state domains are pruned by the ODE enclosure.
- **Parameters** (`ode_pars`): not integrated; instead, the contractor enforces
  `pars_0[i] ∩ pars_t[i]` — i.e., the initial and final values of a parameter must agree (they are
  constant along the trajectory by definition). This intersection is step 1 of `Prune` in
  `contractor_odes.cc`.

**Example**: in `fedor_01.smt2`, `v01` and `v02` have `(= d/dt[v01] 0)` so they are parameters.
The contractor does not integrate them; it simply constrains `v01_0_0 == v01_0_t`.

### 3.3 Flow names and numeric IDs

Flows are stored in a `ScopedUnorderedMap<std::string, shared_ptr<const OdeFlow>>` called
`ode_definition_map_` in `Smt2Driver`. The key is the flow name string (e.g., `"flow_1"`).

For `forall_t` (see §5), the parser uses a numeric ID:

```
'(' TK_FORALLT INT '[' term term ']' term_list ')'
    { ... forallT(driver.LookupOde($3), ...) }
```

`LookupOde(std::int64_t id)` in `driver.h` is:

```cpp
const std::shared_ptr<const OdeFlow>& LookupOde(const std::int64_t id) {
    if (id < 0)
        throw DREAL_RUNTIME_ERROR("forall_t: the flow id must be non-negative, got {}", id);
    return LookupOde("flow_" + std::to_string(id));
}
```

So `(forall_t 1 ...)` looks up `"flow_1"`. The numeric form is a dReal3 compatibility convention:
mode numbers directly encode the flow name by prepending `"flow_"`. The flow name must therefore
match `"flow_"` followed by an integer.

### 3.4 Scoping and shadowing

`ode_definition_map_` is a `ScopedUnorderedMap` but `DefineOde` explicitly throws if a name already
exists at the current scope level:

```cpp
if (ode_definition_map_.count(flow_name) != 0) {
    throw DREAL_RUNTIME_ERROR("Scoped/shadowed define-ode's are not well supported yet.");
}
```

This means **flow names must be globally unique** within a file. You cannot shadow a flow inside
a `(push)` / `(pop)` block. In practice all benchmark files use globally unique names like
`flow_1`, `flow_2`, etc.

### 3.5 The RHS may reference other ODE variables

The right-hand side `e` in `(= d/dt[x] e)` is a full dReal `Expression` and may reference:
- any variable declared before the `define-ode` command (parsed via `driver.lookup_variable`)
- numeric constants
- all arithmetic operators supported in QF_NRA (`+`, `-`, `*`, `/`, `^`/`pow`, `exp`, `log`,
  `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `min`,
  `max`, `abs`)

The expression translator `to_capd_string` (`src/dreal/contractor/odes/to_capd_string.h`) converts
each RHS into the string format that `capd::IMap` parses. It covers essentially the full QF_NRA
operator set — `+ - * / ^`/`pow`, `exp`, `log`, `sqrt`, `sin`, `cos`, `tan`, `asin`, `acos`,
`atan`, `atan2`, `sinh`, `cosh`, `tanh`, `min`, `max`, `abs` — emulating the ones `IMap` has no
primitive for (`abs` → `sqrt(sqr(·))`, `tan` → `sin/cos`, `sinh`/`cosh`/`tanh` → `exp`, `atan2` →
an `atan` formula). Only `IfThenElse`, `UninterpretedFunction`, and `NaN` are unsupported and throw:

```cpp
default:
    throw std::runtime_error("to_capd_string: unsupported ExpressionKind");
```

Translation happens when the per-flow cache is built (`make_capd_ode_cache`, called from the
`contractor_ode_lohner` constructor). If any RHS is untranslatable the build **raises**
(`std::runtime_error`) rather than silently skipping the ODE — a deliberate fail-loud choice
(commit `f4a6eb8da`). There is no separate parse-time check.

---

## 4. `integral`: The Trajectory Constraint

```smt2
(= [x_t P_t] (integral 0. time_0 [x_0 P_0] flow_1))
```

### 4.1 Grammar

```
term:
    '(' TK_EQ '[' term_list ']'
        '(' TK_INTEGRAL term term '[' term_list ']' SYMBOL ')'
    ')'
    {
        // $4  = vec_t (variables on the LHS [...])
        // $8  = time_0 expression
        // $9  = time_t expression
        // $11 = vec_0 (variables inside (integral ... [...] flow))
        // $13 = flow name (SYMBOL)
        $$ = integral($8.expression(), $9.expression(),
                      vec_0_expr, vec_t_expr,
                      driver.LookupOde($13));
    }
```

The full parse production is:

```
'(' TK_EQ '[' term_list ']'
        '(' TK_INTEGRAL term term '[' term_list ']' SYMBOL ')'
')'
```

Positions (token-numbered from `$1 = '('`):
- `$4` (`term_list` after first `[`) — the **final-state vector** `vec_t`
- `$8` (first `term` inside `(integral ...)`) — `time_0` expression (start time)
- `$9` (second `term` inside `(integral ...)`) — `time_t` expression (end time)
- `$11` (`term_list` inside `(integral ... [...])`) — the **initial-state vector** `vec_0`
- `$13` (`SYMBOL` at end) — flow name

### 4.2 Semantic invariants (checked in `FormulaIntegral` constructor)

In `symbolic_odes.cc`, `FormulaIntegral::FormulaIntegral` enforces at construction time:

1. `time_0` and `time_t` must each be a variable, a constant, or a real-constant expression:
   ```cpp
   for (const Expression* t : {&time_0_, &time_t_}) {
       if (!(is_variable(*t) || is_constant(*t) || is_real_constant(*t)))
           throw DREAL_RUNTIME_ERROR(
               "integral: a time bound must be a variable or a constant, got {}", ...);
   }
   ```

2. `vec_0.size() == vec_t.size()` — the initial and final vectors must have equal length.

3. `vec_0.size() == flow->ode_list.size()` — the vector length must equal the number of
   differential equations in the flow.

4. Each element of `vec_0` and `vec_t` must be a single variable (not an expression):
   ```cpp
   if (!is_variable(vec_0_[i]) || !is_variable(vec_t_[i]))
       throw DREAL_RUNTIME_ERROR(
           "integral: start and end entries must be variables, got {} and {}", ...);
   ```
   (1) and (4) were `DREAL_ASSERT`s, compiled out in Release, until 2026-10-05.

5. Each ODE variable in the flow is classified: the constructor splits the combined
   `(vec_0, vec_t)` pair into `(vars_0, vars_t)` for state variables and
   `(pars_0, pars_t)` for parameter variables, preserving positional correspondence.

Violation of (1)–(4) is a `DREAL_RUNTIME_ERROR` at parse time.

### 4.3 Positional correspondence

The mapping between `vec_0`/`vec_t` elements and the `ode_list` is **positional**:
element `i` of `vec_0` is the initial value of the variable whose ODE is `ode_list[i]`.
There is no name-based matching.

**Example**: if `flow_1` has `ode_list = [(x, ...), (P, ...)]` and you write
`(integral 0. t [P_0 x_0] flow_1)`, then `P_0` is treated as the initial condition for
`x` (not for `P`) because it appears first. The variable names `P_0` and `x_0` are opaque.

### 4.4 `time_0` vs. `time_t`: direction and zero-time case

The `integral` formula represents: _integrate `flow` from time `time_0` to time `time_t`, starting
at state `vec_0`, ending at state `vec_t`._

In all current benchmarks, `time_0` is the literal constant `0.` and `time_t` is a variable
(e.g., `time_0` in the problem). The constructor accepts either time bound as a variable or a
constant (invariant (1)), but the contractor integrates from t = 0 only: it throws unless `time_0`
is the constant 0 (`contractor_odes.cc`, top of `Prune`). `time_t` may be a variable, a constant or
a real-constant interval; `Prune` and the `--visualize` trace both read the integration window from
whichever it is through `end_time_window` (§10.2).

**T=0 special case** (`Prune`, Step 2):

```cpp
const bool time_is_zero =
    (is_variable(icct) && cs->box()[get_variable(icct)].ub() == 0.0) ||
    is_constant(icct, 0.0);
```

If the upper bound of the time variable is exactly `0.0`, the contractor intersects each
`vars_0[i]` with `vars_t[i]` in-place (since a zero-duration trajectory means initial = final
state). This avoids a CAPD integration call when the time domain is already pinned to zero.

### 4.5 Direction: FWD vs. BWD

`TheorySolver` constructs two contractors per `Integral` formula: one with
`ode_direction::FWD` and one with `ode_direction::BWD` (cached in separate maps
`fwd_ode_contractor_cache_` and `bwd_ode_contractor_cache_`).

In `contractor_ode_lohner`'s constructor, the direction controls variable assignment:

```cpp
if (m_dir == ode_direction::FWD) {
    m_vars_0 = icc->get_vars_0();   // integration starts from vars_0
    m_vars_t = icc->get_vars_t();   // integration ends at vars_t
} else {
    m_vars_0 = icc->get_vars_t();   // integration starts from vars_t (backwards)
    m_vars_t = icc->get_vars_0();   // integration ends at vars_0
}
```

In both cases, `m_vars_0` is the initial condition for the CAPD integration and `m_vars_t` is the
target. `run_capd_fwd` integrates `f(x)` forward from `m_vars_0` to narrow the terminal `m_vars_t`,
then integrates the negated `-f(x)` backward from the narrowed `m_vars_t` to narrow `m_vars_0` — so
a single call narrows **both** endpoints (recovering the joint narrowing that Codac's `CtcLohner`
FWD_BWD did in one shot). The FWD vs. BWD distinction in dReal controls _which endpoint is
considered the "initial" state_ for the outer ICP loop.

---

## 5. `forall_t`: Invariant Constraints

> **⚠ PITFALL — `forall_t` ≠ `forall` (`forall-vs-forall_t`).** This section is the ODE
> trajectory invariant (`FormulaKind::ForallT`, checked per-slice inside the ODE contractor
> `contractor_ode_lohner` / `Kind::ODE_LOHNER`). The similarly-named **`forall`** is the
> unrelated ∃∀ NRA quantifier (`Formula::Forall` → `ContractorForall`); see §7 below and the
> canonical side-by-side in `docs/forall-semantics.md` §7. Never conflate them.

```smt2
(forall_t 1 [0 time_0] (< s1_0_t (+ s2_0_t (* v2_0_t 2.0))))
```

This section covers `forall_t` **syntax, linking, and AST**. For how a linked invariant is
*enforced* — the CAPD per-slice trajectory tube fed through IBEX HC4 invariant contractors —
see `docs/ode-integration.md` § "The `ForallT` invariant mechanism (CAPD tube × IBEX HC4)".

### 5.1 Grammar

```
'(' TK_FORALLT double_or_int_value '[' term term ']' term_list ')'
    { Formula f = Formula::True();
      for (const Term& t : $8) f = f && t.formula();
      $$ = forallT(driver.LookupOde($3), $5.expression(), $6.expression(), f); }
```

Fields:
- `$3` (`double_or_int_value`) — flow ID (numeric, mapped to `"flow_N"`)
- `$5` (`term`) — lower bound of time interval `lb`
- `$6` (`term`) — upper bound of time interval `ub`
- `$8` (`term_list`) — conjunction of invariant formulas; multiple terms are `&&`-folded

The `term_list` variant lets you write multiple invariant predicates:
`(forall_t 1 [0 T] (< x 5) (> v -10))` produces `forallT(flow_1, 0, T, x < 5 && v > -10)`.

### 5.2 Invariant flattening in `FormulaForallT` constructor

`FormulaForallT::FormulaForallT` in `symbolic_odes.cc` calls `flatten_nested_boolean_structures`
on the body `bound_f`:

```cpp
bound_f_{make_conjunction(flatten_nested_boolean_structures(bound_f, false))}
```

`flatten_nested_boolean_structures` recursively:
- Strips `Not` nodes (toggling an `inverted` flag)
- Flattens `And` (when not inverted) into a flat set of atomic formulas
- **Throws a `DREAL_RUNTIME_ERROR`** for `Or` when not inverted:
  ```cpp
  DREAL_LOG_CRITICAL("or is not a valid invariant for now, (forall_t constraint {})", f);
  throw DREAL_RUNTIME_ERROR("or is not a valid invariant for now, (forall_t constraint)");
  ```
- **Throws** if the body is `false`
- Passes all other formulas through (comparison operators become leaves)

Consequence: the invariant body must be a **conjunction of atomic (non-disjunctive) constraints**.
Disjunctive invariants are not supported and will crash the parser.

### 5.3 Linking `forall_t` to `integral`

`forall_t` formulas are not independently asserted — they must be linked to an `integral` formula
at contractor-construction time. This linking happens in `link_integral_invariants` in
`contractor_odes.h`:

```cpp
for (const auto& ic : int_ctrs) {
    Variables vars_t_in_ic;
    // collect vars_t variables from the integral
    for (const auto& vec_t : icc->get_vec_t()) vars_t_in_ic.insert(vec_t.GetVariables());

    for (const auto& fc : inv_ctrs) {
        // Link if:
        //   fc.flow->name == ic.flow->name
        //   vars(fc.bound_f) ⊆ ic.vars_t
        if (fcc->get_flow()->name == icc->get_flow()->name) {
            bool const included = vars_t_in_ic.IsSupersetOf(vars_in_fc);
            if (included) local_invs.push_back(fc);
        }
    }
    result.emplace_back(ic, local_invs);
}
```

Two conditions must hold for a `forall_t` to be linked to an `integral`:

1. **Same flow name**: `fc.flow->name == ic.flow->name`. This is the flow numeric ID that maps to
   `"flow_N"`.

2. **Variable subset**: the free variables of the `forall_t` body must be a subset of the
   `vec_t` variables of the `integral`.

If a `forall_t` fails either condition, it is silently discarded — it never becomes a constraint.
There is no error message for this case.

### 5.4 Free variables in `FormulaForallT`

There was a design decision about which variables to expose as free in a `ForallT` formula.
A comment in `symbolic_odes.cc` documents the evolution:

```cpp
// PRIOR THINKING: variables in bound_f are bound, not free
// NEW: see UPDATE comment in `ADD_DECL(VisitForallT)` of `pattern_matching_trie_form.cc`
lb.GetVariables() + ub.GetVariables() + bound_f.GetFreeVariables()
```

The **current** implementation treats the variables in `bound_f` as **free** (not bound). This
is semantically unusual — in standard first-order logic, `∀t: φ(x, t)` would bind `t`. Here,
`bound_f` references time-indexed state variables (`x_0_t`, `v_0_t`, etc.) which are solver
variables, not quantified over in the classical sense. The `forall_t` quantifies over the
time parameter of the ODE trajectory, not over solver variables. The solver variables are the
state variables at the endpoints.

`get_bound_vars()` returns `bound_f_.GetFreeVariables()`, which is used in
`link_integral_invariants` to verify the subset condition.

---

## 6. Negated ODE Constraints: rejected in the input, dropped in the search

### 6.1 The input rule (since 2026-10-05, BUG-002)

dReal enforces `(integral …)` and `(forall_t …)` only **positively**. `Context::Impl::Assert`
rejects an assertion that puts an ODE atom in negative or mixed polarity — under `not`, an `=>`
antecedent, a Boolean `=`, `xor`, or an ite condition (the parser turns each into `Not`) — or
inside an NRA `forall` body, with an error naming the atom (`RejectNonPositiveOde`,
`context_impl.cc`). Positive occurrences under `or`, `and` or an `=>` consequent are accepted,
for example s2d's implied integrals `(or (not G) I)` and dReach's mode-guarded invariants
`(=> (= mode k) (forall_t …))`. Before 2026-10-05 such a formula was solved with the negated
atom silently removed (a missed refutation).

### 6.2 Negated literals inside the search

The SAT solver still assigns ODE atoms false during search, and `link_integral_invariants`
(`contractor_odes.h`) drops such a literal (and an ODE formula nested inside a larger one):

```cpp
else if (is_negation(f) && f.include_ode()) {
    DREAL_LOG_DEBUG("Inverted ODE constraints are currently ignored: {}", f);
}
else if (f.include_ode()) {
    DREAL_LOG_DEBUG("Nested ODE constraint currently ignored: {}", f);
}
```

With the input rule this drop is exact, not a relaxation: every ODE atom occurs only positively,
so the Boolean skeleton is monotone in it, and an assignment that satisfies the skeleton with the
atom false also satisfies it with the atom true. Only positive `forall_t` atoms are linked to an
integral, so the per-slice invariant check never sees a negation.

### 6.3 Why the rule is in the input and not at the drop

A throw at the drop site would fire on legitimate search states (a negated literal of a
positive-only atom, or a `forall_t` whose integral is not active in that node) and crashes valid
BMC benchmarks; only the asserted formula tells malformed input from search. The semantics a
negated ODE atom could be given instead (∃t ¬φ for `forall_t`; disequality or definitional
binding for `integral`) are specified as skipped tests in `test/dreal/smt2/test/dreal_future.cc`.

### 6.4 `OdeFormulaEvaluator` returns valid by default

`ode_formula_evaluator.cc` (`OdeFormulaEvaluator::operator()`) returns `VALID` for every ODE atom
unless `--refine-witness` is given (`docs/decisions.md`, "ODE formula evaluator"); a negated
literal is `VALID` in both modes. So by default the ICP loop never branches on an ODE atom; the ODE
contractor prunes domains through the normal `Prune` path, and a box that survives it is accepted
at tube granularity.

---

## 7. `forall` (not `forall_t`): Quantified Constraints

The standard SMT2-LIB `forall` (quantification over real variables) is also supported:

```smt2
(forall ((x Real [0.0, 1.0])) (>= (* x x) 0.0))
```

Grammar:

```
'(' TK_FORALL enter_scope '(' variable_sort_list ')' term exit_scope ')'
    { const Variables& vars = $5.first;
      const Formula& domain = $5.second;
      const Formula body = Smt2Driver::EliminateBooleanVariables(vars, $7.formula());
      const Variables quantified_variables = intersect(vars, body.GetFreeVariables());
      if (quantified_variables.empty()) $$ = body;
      else $$ = forall(quantified_variables, imply(domain, body)); }
```

This is completely separate from `forall_t` (`forall-vs-forall_t`; canonical side-by-side:
`docs/forall-semantics.md` §7). Key differences:
- `forall` uses `variable_sort_list`, which creates variables with domain bounds; `forall_t` uses
  a numeric flow ID.
- `forall` produces a `Formula::Forall` AST node; `forall_t` produces `FormulaKind::ForallT`.
- `forall` variables that do not appear free in the body are silently dropped
  (`intersect(vars, body.GetFreeVariables())`).
- Boolean variables are eliminated by case-splitting (`EliminateBooleanVariables`).
- `forall` is handled by `ContractorForall` in the contractor layer; `forall_t` by
  `contractor_ode_lohner`.

---

## 8. `ignored_dreal3_precision_value`: Backward Compatibility

dReal3 allowed an optional precision annotation on comparison operators and conjunction:

```smt2
(<= x 5.0 [0.001])   ; dReal3 precision annotation
(and ... [0.001])     ; dReal3 precision annotation on and
```

The parser grammar handles this with:

```
ignored_dreal3_precision_value
  : '[' DOUBLE ']'  { $$ = ""; }
  |                 { $$ = ""; }
  ;
```

The precision value is consumed and **discarded**. It has no effect on the solver. This is present
in the productions for `TK_LT`, `TK_LTE`, `TK_GT`, `TK_GTE`, and `TK_AND`.

---

## 9. How the Pieces Connect: Full Data Flow

```
(define-ode flow_1 (...))
         │
         ▼
  driver.DefineOde("flow_1", ode_list)
         │
         ▼
  OdeFlow{name="flow_1", ode_list, ode_vars, ode_pars}
  stored in ode_definition_map_["flow_1"]
         │
         │  (reference held by)
         ▼
(= [x_t] (integral 0. time_0 [x_0] flow_1))
         │
         ▼
  FormulaIntegral{time_0=0., time_t=time_0,
                  vec_0=[x_0], vec_t=[x_t], flow=flow_1}
  classified: vars_0=[x_0], vars_t=[x_t], pars=[]
         │
(forall_t 1 [0 time_0] (< x_t 5))
         │
         ▼
  FormulaForallT{flow=flow_1, lb=0, ub=time_0,
                 bound_f = (x_t < 5)}
         │
         │  both formulas appear in context_.assertions()
         ▼
  link_integral_invariants(assertions)
         │  matches: flow_1 == flow_1  AND  {x_t} ⊆ {x_t}
         ▼
  ode_constraint = { FormulaIntegral, [FormulaForallT] }
         │
         ▼
  contractor_ode_lohner(box, ode_constraint, FWD/BWD, config)
         │
         ▼  Prune():
  Step 1: intersect parameters
  Step 2: T=0 case → intersect state vars
  Step 3: check invariants at X_0 via ibex contractors
           (negated invariants silently skipped)
  Step 4: run_capd_fwd() — forward f(x), then backward -f(x) sweep
           → narrow [x_0, x_t, time_0]
```

---

## 10. Edge Cases and Gotchas

### 10.1 Multiple `integral` formulas with the same flow

It is legal to write two `integral` constraints that reference the same flow:

```smt2
(= [P_t]    (integral 0. time_0 [P_0]    flow_2))
(= [x_t P_t] (integral 0. time_0 [x_0 P_0] flow_1))
```

(`normal_partial_flow_KUNAL.smt2` does exactly this.) `link_integral_invariants` creates a
separate `ode_constraint` for each `integral`, each linked to the `forall_t` formulas matching
its flow name and variable subset. Two `integral`s over the same flow but different variable
sets are independent constraints and both are contracted.

### 10.2 `integral` with a constant `time_t`

```smt2
(= [x_t] (integral 0. 5.0 [x_0] flow_1))
```

A constant end time is integrated like a variable whose box is that point: `Prune` reads the
window from the constant (`contractor_odes.cc`, the `is_constant(icct)` branch). `deab8ccae`
(2025-12-27) added that branch; the Codac rewrite dropped it (`331b20703`, 2026-04-27, then
`a21b7a40d`'s `if (!is_variable(icct)) return;`, the line this section used to quote), and
`5d619fe3f` (2026-06-22) restored it. Builds from 2026-04-27 to 2026-06-22 skipped a constant end
time — COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed refutation). Any other time term (`(* 2 t)`) is rejected when the formula is built: the
`FormulaIntegral` constructor throws unless each time bound is a variable or a constant
(an always-on check since 2026-10-05; it was a `DREAL_ASSERT`, compiled out in Release, and the
contractor then skipped such an integral on every `Prune`).

### 10.3 `forall_t` with a flow not defined in any `integral`

A `forall_t` that references a flow name for which no matching `integral` exists is silently
dropped by `link_integral_invariants`. The condition

```cpp
if (fcc->get_flow()->name == icc->get_flow()->name)
```

will never be true, so the invariant simply never appears in any `ode_constraint`.

### 10.4 `forall_t` linking uses subset, not equality

If the `forall_t` body references a variable not in `vars_t` of the corresponding `integral`,
the invariant is not linked. But there is no error. Example:

```smt2
(define-ode flow_1 ((= d/dt[x] v) (= d/dt[v] -9.8)))
(= [x_t v_t] (integral 0. T [x_0 v_0] flow_1))
(forall_t 1 [0 T] (>= height_sensor_t 0.0))  ; ← height_sensor_t NOT in {x_t, v_t}
```

The `forall_t` is silently discarded because `{height_sensor_t}` is not a subset of `{x_t, v_t}`.

### 10.5 `OdeFlow` equality is by name only

`OdeFlow::operator==` compares only by `name`:

```cpp
bool operator==(const OdeFlow& lhs, const OdeFlow& rhs) {
    if (&lhs == &rhs) { return true; }
    return lhs.name == rhs.name; // commented-out deep comparison
}
```

Two `OdeFlow` objects with the same name but different `ode_list` contents are considered equal.
Since `DefineOde` throws on name collision, this situation cannot occur in a single file, but
it could arise if `OdeFlow` objects from different parses were compared.

### 10.6 dReal3 `arcsin`/`arctan` aliases

The scanner (`scanner.ll`) maps both `asin` and `arcsin` to `TK_ASIN`, and both `acos`/`arccos`,
`atan`/`arctan`, `atan2`/`arctan2` to their respective tokens. dReal3 files using the `arc`
prefix work without modification.

### 10.7 `^` as `pow`

The scanner maps both `^` and `pow` to `TK_POW`. The expression `x^2` is parsed identically
to `(pow x 2)`.

### 10.8 Flow IDs in `forall_t` must be non-negative integers

The grammar takes the id as an INT token and `driver.LookupOde(std::int64_t id)` throws on a
negative one. Until 2026-10-05 the id was parsed as a double and checked by a `DREAL_ASSERT`,
compiled out in Release, so a decimal id was truncated and silently named another flow.

### 10.9 CAPD integration divergence

If CAPD's `IOdeSolver` cannot maintain step-control (the trajectory tube is too stiff or the
over-approximation explodes), it throws. The same happens, by the adapter's own check, when a
start set, flow parameter, time window or computed slice has a bound CAPD cannot represent
(non-finite, inverted, or beyond DBL_MAX/2). `integrate_tube_slices_impl` catches the exception
and records its message in `CapdTubeResult::failure`. The contractor then narrows nothing beyond
Step 1's parameter intersection, which has already run, and records the integral with that reason
(`AddInconclusiveOde`). A conflict explanation then keeps the integral when its variables meet the
emptying witness (`GenerateExplanation`, `contractor_status.cc`), and a `delta-sat` verdict prints
`WARNING: delta-sat while N ODE constraint(s) were not integrated on some box of this search …`
on stderr when any box of the search recorded such a skip. This is the one approved fallback on
the ODE path (`docs/decisions.md` "ODE inconclusive skip"; grep `FALLBACK(approved)`). It is
COMPLETENESS only (asserts φ^δ T-satisfiable on a possibly T-unsatisfiable φ — missed
refutation), never a false `unsat`. It can happen on long time horizons or highly nonlinear RHS.
(Distinct from an *untranslatable* RHS, which raises at cache-build time — see §3.)

---

## 11. Output Format: Trajectory Visualization

When `--visualize` is set, `driver.cc` calls `link_integral_invariants` on the model and runs
`contractor_ode_lohner::generate_trace` on each `ode_constraint`. The output is a JSON file at
`<input_file>.json` with structure:

```json
{
  "traces": [
    [
      {
        "key": "x_0_0",
        "mode": "flow_1",
        "step": 0,
        "values": [
          {"time": [0.0, 0.1], "enclosure": [1.0, 1.1]},
          {"time": [0.1, 0.2], "enclosure": [1.05, 1.15]},
          ...
        ]
      },
      ...
    ]
  ]
}
```

One entry per ODE state variable and one per parameter. The `step` field is parsed from the
variable name by `ode_step_from_name`: the middle component of `<name>_<step>_{0,t}`, or the
`<step>` of `<name>_k<step>`. Parameters have only two time points (start and end) since they are
constant.

The trace reads the integration window as `Prune` does (§4.4), so a constant end time is traced up to
that constant. A state variable's `values` are empty in two cases: the window has length zero
(T = 0), or CAPD fails before its first point. A CAPD failure keeps the points before it (the
approved fallback, §10.9). A trivial flow, whose every variable is a parameter, has only parameter
entries. `generate_trace` raises in two cases, because there is no trajectory to draw: a parameter's
start and end values are disjoint, or the window ends below 0 (which `Prune` skips as
inconclusive). Before 2026-10-05 every case in this paragraph returned an empty array for the
integral. The driver does not catch the error, so `dreal --visualize` prints the verdict and then
terminates with it. It used to log the error at CRITICAL, invisible at the default verbosity, and
leave an empty JSON file.

---

## 12. Grammar Summary

```
define-ode   := (define-ode <flow-id> (ode-entry*))
ode-entry    := (= d/dt[<var>] <expr>)

integral     := (= [<var>+] (integral <time0> <timet> [<var>+] <flow-id>))
forall_t     := (forall_t <flow-num> [<lb> <ub>] <formula>+)

<flow-id>    := SYMBOL  (e.g. "flow_1")
<flow-num>   := non-negative integer (mapped to "flow_N")

<time0>      := variable | constant       (must satisfy one of these)
<timet>      := variable | constant       (integration only runs if variable)

ODE vars     := ode entries where RHS ≠ constant 0
ODE pars     := ode entries where RHS = exact constant 0
```
