# Seed-and-verify (`--seed-samples`)

A speculative pre-pass that helps ICP find an **off-center** solution when the constraint
is **flat (∇ = 0) at the symmetric center** — the structure that makes the `tanh_decrease__*`
Lyapunov SAT instances pathologically slow under plain bisection. It proposes candidate
points by local optimization, pins a small sound box around each, and lets the *existing*
prune+evaluate loop verify them first.

- **Why this design, and the A/B that adopted it:** `docs/decisions.md` §"Seed-and-verify".
- **Full investigation / experiment tables:** `benchmark/optsearch/SEARCH_LOG.md` §"Seed-and-verify".
- **Code:** `src/dreal/solver/seed/seed.{h,cc}`; hooked in `src/dreal/solver/icp_parallel.cc`
  (`IcpParallel::CheckSat`). Tests: `test/dreal/solver/seed/test/seed_test.cc`.

## Soundness and completeness

Seed-and-verify is a **COMPLETENESS-only** speed optimization — it can never change a verdict.

- **Soundness is untouched.** `delta-sat` is established *only* by the unchanged
  `EvaluateBox` on each candidate box. A bad proposal cannot manufacture a false `delta-sat`:
  it either evaluates to a real `delta-sat` box (a genuine model) or it does not.
- **Completeness is preserved.** The root box is always on the stack; seeding only *reorders*
  exploration (the candidate boxes are pushed on top, LIFO, so they are tried first). No
  subspace is dropped. On an UNSAT instance every candidate fails to verify and the full
  search returns `false`.
- This is the **cache/recompute carve-out shape, not a fallback** (`no-silent-fallbacks.md`):
  the canonical complete path always runs; seeding is a fast-path atop it. A proposer start
  that throws simply yields no candidate.

Model-theoretically: seeding can only weaken the *missed-refutation* axis, never the
false-`unsat` axis — it **cannot assert φ T-satisfiable on a T-unsatisfiable φ** (the verifier
gates that), so the worst it can do is fail to speed something up.

## The gate (`AllRelational`)

Seeding fires only when every formula evaluator is a plain relational constraint —
no `forall`, no ODE/integral (`AllRelational`, matching `theory_solver.cc`'s dispatch).
`forall` already runs nlopt internally, and per-node proposal over ODE flows is too costly;
both also lie outside the relational objective the proposer can build. The gate is verified
inert on the ODE families (it fires 0 times on github/tacas/saradc).

## Mechanism (the `SeedBoxes` pipeline)

For a gated `CheckSat`, `SeedBoxes` runs:

1. **`FiniteDims`** — the dimensions with both bounds finite. Only these are sampled/pinned.
   Unbounded dims (typically equality-defined CSE auxiliaries that carry no explicit bound)
   are left at full range and re-derived by the contractor's HC4 from their linking
   equalities.
2. **`DerivedSubstitution`** — for each `v == e` whose `v` is unbounded, map `v → e`, with
   chained CSEs resolved to a fixpoint so every RHS references only bounded primaries. This
   lets COBYLA see the hard constraint as a self-contained function of the bounded variables;
   it removes the equality constraints (which COBYLA handles poorly) and the unbounded
   0-initialized CSE dims (which pull the optimizer toward the infeasible origin).
3. **`NloptSeeds`** — multi-start derivative-free COBYLA over the sub-box of finite dims,
   from the box center plus (`seed_samples − 1`) Latin-hypercube starts. The objective is the
   summed constraint violation; **the objective formula is NNF-normalized with the negation
   pushed in** (`Nnfizer::Convert(f, true)`) because the theory hands negated literals as
   `¬(≤)`, for which `ConstraintViolation` has no signed direction unless rewritten to a
   positive `>`. COBYLA's `center ± rhobeg` simplex probes off-center even where ∇ = 0, and
   the LHS starts (gradient-free) cover a tiny or multi-modal feasible region; a single center
   start *does* stall at the flat center, so ≥ 2 starts are what dodge it.
4. **`SmallBoxAround`** — a small sound box around each proposed point: every finite dim pinned
   to `[pt − w, pt + w]` **rounded outward** (so the interval soundly contains it) and
   **intersected with the root box** (so it stays in-domain); unbounded dims left full-range.
   The outward rounding is *compile-enforced* — `make_sound_interval` only accepts a
   down-rounded lower bound and an up-rounded upper bound (`docs/rounding.md`), so the
   construction cannot silently produce a too-narrow (unsound) box.

## Flag

`--seed-samples N` is both the candidate budget (the COBYLA multi-start count) and the
on/off switch: `N > 0` enables seeding, `N = 0` disables it. Default `64`. Reproducible via
`--random-seed` at `--jobs 1` (seeds are proposed once, single-threaded, before any worker
starts; above jobs=1 the search order after the seed pre-pass is nondeterministic).
