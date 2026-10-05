# Pattern Matching and Lemma Reuse (CAV26)

## Motivation

The ICP + CDCL loop produces UNSAT explanations — minimal subsets of formulas that jointly imply infeasibility. When the solver encounters a structurally similar problem later (same formula shape, different variable names), it would re-derive the same contradiction from scratch.

The CAV26 feature intercepts UNSAT explanations, generalizes them as reusable lemmas, and injects them into the SAT solver (via `CaDiCaL::Learner`) before the ICP loop runs. This can prune exponentially many future search branches at negligible cost when the matching succeeds.

---

## Overview

The pattern matching system lives in `src/dreal/util/pattern_matching/`. The two main components are:

1. **`DeBruijnCanonicalizer`** — converts a formula to a canonical form that is invariant under variable renaming, then indexes it.
2. **`substitution_tree`** — a trie-like structure that maps De Bruijn index sequences to stored formulas, supporting efficient alpha-bijection matching.

These are templated over `T` (instantiated as both `Formula` and `Expression`).

---

## De Bruijn Canonicalization

**File:** `src/dreal/util/pattern_matching/map_based/DeBruijnCanonicalizer.h`

A formula is **alpha-equivalent** to another if they differ only in variable names. For example, `x² + y² ≤ 1` and `a² + b² ≤ 1` are alpha-equivalent.

De Bruijn canonicalization converts a formula to a canonical form by replacing each variable occurrence with its index in the sequence of first appearances:

```
x² + y² ≤ 1   →   v₀² + v₁² ≤ 1   (De Bruijn: [0, 1])
a² + b² ≤ 1   →   v₀² + v₁² ≤ 1   (De Bruijn: [0, 1])
```

The traversal is structural (visitor pattern over the AST). Each variable, when first encountered, receives the next available De Bruijn index. Subsequent occurrences reuse the same index.

The canonicalization cache (`canonicalization_cache`) stores the mapping `original_formula → (canonical_formula, index_vector, variable_sequence)` to avoid recomputing it for formulas that appear multiple times.

### Quantified (`forall`) literals

For a `∀y. φ(x, y)` atom, the **bound (universal) variables `y` are excluded from the canonical variable sequence** (`VisitForall` pushes them onto a `bound_scope_` that `VisitVariable` skips). They are not free solver variables and have no `Box` entry, so admitting them would send a non-solver variable into the domain check (`substitutions_map::attempt_substitution`; historically `box[y]` silently default-inserted into the `Box`'s shared index map — the accessors now throw on a miss, and the fit check handles membership explicitly: mixed membership is a `BOX_MISS`, both-absent matches structurally). Only the free (existential) `x` are canonicalized and matched by the domain-preserving bijection; the bound `y` ride along inside the atom's structure and match by exact structural equality — there is **no cross-α-renaming of bound variables** (a lower-payoff extension deferred; see `exists_forall_perf.md` §"Lemma pattern-matching + quantifiers"). `forall_t`/`integral` are unaffected — their variables are genuine `Box` variables.

---

## Substitution Tree

**File:** `src/dreal/util/pattern_matching/substitution_tree/substitution_tree.h`

`substitution_tree<Leaf>` is a trie keyed by sequences of `Variable`. When a formula `f` is indexed:

1. Canonicalize `f` → `(f_canonical, indices, var_seq)`.
2. Store `f` at the path corresponding to `var_seq` in the substitution tree under the key `f_canonical` in `DeBruijnCanonicalizer::structure_to_concrete`.

Each internal node in the trie is a `std::vector<std::pair<Variable, Node>>`. Each leaf holds the original `Formula` (not the canonical form).

**Why a trie?** Because matching amounts to walking the trie while building a variable bijection. At each step, we extend the bijection or fail immediately if a consistent extension doesn't exist. This prunes incompatible candidates before fully exploring them.

---

## Substitutions Map

**File:** `src/dreal/util/pattern_matching/substitutions_map.h`

`substitutions_map` maintains a bidirectional partial bijection between variables during matching:

- **"forward"**: maps `matched_variable → original_variable`
- **"backward"**: maps `original_variable → matched_variable`

Both directions are required to check bijectivity: we need to know not only "does `a` already map to something?" but also "does something already map to `b`?"

Internally it's a flat `std::vector<std::pair<Variable, Variable>>` (cache-friendly, replaced an earlier `std::unordered_map`). `push()` / `pop()` implement backtracking by saving and restoring the vector length on an `index_stack`.

`attempt_substitution(a, aP)` tries to add the pairing `a ↔ aP` to the bijection. It fails (returns a non-SUCCESS status) if:

- `STRUCTURE_MISS`: De Bruijn structure doesn't match
- `INDICES_MISS`: index sequence mismatch
- `TYPE_MISS`: variable types differ (e.g., Boolean vs. real)
- `BOX_MISS`: variable domains are incompatible (checked against the current `Box`)
- `BIJ_MISS`: extending the bijection would violate bijectivity
- `CONST_MISS`: a literal constant doesn't match
- `CAV26_NOT_PURE_*`: symmetry filter rejection (see below)

---

## Symmetry Filtering

**Compile-time flag:** `CAV26_FILTER_SYMMETRIES`

CAV26's primary contribution over earlier branches: filtering redundant lemmas at generation time using variable symmetry information.

If a problem has symmetric variables (e.g., `x₁, x₂, ..., xₙ` are all interchangeable), then a learned lemma `L(x₁, x₂)` implies `L(x₂, x₁)` as well. Without filtering, both would be injected — wasting SAT solver budget.

The symmetry filter labels variables as:
- **Pure time** (`CAV26_NOT_PURE_TIME` if violated): variables that appear only in temporal contexts (ODE time parameters)
- **Pure logic** (`CAV26_NOT_PURE_LOGIC` if violated): variables that appear only in logical constraints
- **Mixed** (`CAV26_NOT_PURE_ANY`): variables that appear in both contexts — these are filtered out since mixed-symmetry lemmas rarely generalize usefully

The filter is applied inside `attempt_substitution` when `CAV26_FILTER_SYMMETRIES` is defined.

---

## Randomized Iteration

**File:** `substitution_tree.cc` (iteration over `std::vector<std::pair<Variable, Node>>`)

When a timeout interrupts matching midway, the order in which trie children are visited determines which lemmas are found first. Without randomization, the solver would consistently find lemmas involving early-indexed variables (those that appear first in the formula) and miss lemmas involving later variables.

A random state (`uint64_t &random_state`) is threaded through the matching calls. At each trie node, the children are shuffled using this state before iteration. This ensures that over multiple solver calls, different lemmas are discovered, reducing systematic bias.

---

## Integration with the SAT Solver

**Location:** `src/dreal/solver/context_impl.cc`, `sat_solver.cc`

When `TheorySolver::CheckSat` returns UNSAT with an explanation set `{f₁, ..., fₖ}`:

1. The explanation is passed to `LemmaGenerator` (or equivalent) in `context_impl.cc`.
2. `DeBruijnCanonicalizer::find_matches` is called with the explanation literals and the current `Box`.
3. For each match found, `substitutions_map::apply_substitution` instantiates the stored lemma with the variable mapping of the current problem.
4. The instantiated lemma (a clause) is injected into CaDiCaL via the `CaDiCaL::Learner` interface.
5. CaDiCaL uses the injected clause to immediately prune branches that would lead to the same contradiction.

---

## CLI Configuration

Two flags control pattern matching (`src/dreal/dreal_main.cc`, `src/dreal/solver/context_impl.cc`):

- `--drpm-max-size <n>`: a lemma (explanation) is pattern-matched only if it has **fewer than
  `n` literals** (`explanation.size() < config().drpm_max_size()`). It does not bound the pattern
  database. The default is 0, which matches nothing, so DRPM is off unless the flag is given; the
  flag itself must be positive (`--drpm-max-size 0` is rejected), so omit it to turn DRPM off.
- `--drpm-max-time <s>`: in **seconds** (default 0.222). A matching call for a lemma gets
  `t_sat + t_theory + min(99·(t_sat + t_theory), drpm_max_time)`, where `t_sat` and `t_theory` are
  that lemma's own SAT and theory check times; when the budget runs out, matching stops and keeps
  what it found.

These replaced compile-time constants in earlier branches.

---

## Dummy Variables

Variables with **negative IDs** are internal dummy variables used by the pattern matcher. They are not solver variables and should not appear in `Box` lookups or be exported in models. The `Variable::get_id()` inlining optimization is relevant here — negative IDs gate pattern-matcher-internal code paths in `context_impl.cc`.

---

## Data Flow Summary

```
TheorySolver returns UNSAT explanation {f₁, ..., fₖ}
            │
            ▼
DeBruijnCanonicalizer::insert({f₁, ..., fₖ})   ← index new explanation
            │
            ▼
DeBruijnCanonicalizer::find_matches(current_literals, box)
            │
            ├── canonicalize each literal
            ├── walk substitution_tree for structural matches
            ├── attempt_substitution at each step (bijection check + symmetry)
            └── on match: return (stored_formula, substitutions_map)
            │
            ▼
substitutions_map::apply_substitution(lemma, subs, backward=true)
            │
            ▼
CaDiCaL::Learner::learn_clause(instantiated_clause)
```
