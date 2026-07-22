# `CtcPolytopeHull` — LP-relaxation hull contractor (opt-in `--polytope` in dReal)

Header:
[`ibex_CtcPolytopeHull.h`](../../../../ibex-fork/src/contractor/ibex_CtcPolytopeHull.h)
(`contractor/`). `class CtcPolytopeHull : public Ctc`. Linearizes a system into a
polytope and runs **2n LP solves** (min & max each variable) to contract to the
relaxation's hull. Header warning: *"can only be used if ibex is installed with a
LP solver (`-DLP_LIB`)."*

> **dReal status:** **shipped opt-in.** `--polytope` is off by default but functional
> when set — dReal builds IBEX with **`-DLP_LIB=soplex`** (`CMakeLists.txt`, the
> `-DLP_LIB=soplex` line; `libsoplex.a` linked), so the `2n` LP solves run.
> `ContractorIbexPolytope` builds it per constraint via
> `generic_contractor_generator.cc` (dispatching to `make_contractor_ibex_polytope`)
> and per whole-assertion-set in `theory_solver.cc`; works under `--jobs>1` via the
> per-worker `ContractorIbexPolytopeMt` cell. Perf is unmeasured on the main NRA path;
> `--forall-polytope` (the ∃∀ variant) measured *mixed* (`exists_forall_perf.md`).

## Constructors (verbatim)

```cpp
CtcPolytopeHull(Linearizer& lr, int max_iter=LPSolver::default_max_iter,
        int time_out=LPSolver::default_timeout, double eps=LPSolver::default_tolerance);
CtcPolytopeHull(const Matrix& A, const Vector& b, int max_iter=..., int time_out=..., double eps=...);
```

dReal uses the first form with a `LinearizerXTaylor` (`contractor_ibex_polytope.cc`):
`LinearizerXTaylor(system, RELAX, RANDOM_OPP, HANSEN)`.

| Param | Default (per header) | Meaning |
|---|---|---|
| `lr` | — | the linearization technique ([`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md)) |
| `max_iter` | `LPSolver::default_max_iter` = **100** | max LP solver iterations |
| `time_out` | `LPSolver::default_timeout` = **100** (s) | per-iteration LP timeout |
| `eps` | `LPSolver::default_tolerance` = **1e-9** | LP resolution accuracy. ⚠️ the `CtcPolytopeHull.h` doc-comment says 1e-10 — stale; the `LPSolver` constexpr (1e-9) is ground truth |

`set_contracted_vars(BitSet)` restricts which variables get the 2 LP solves
(e.g. only the objective in an extended system). Variable/bound order is chosen by
an Achterberg heuristic.

## Open questions (build decision already made)

The LP-backend build decision is settled — `LP_LIB=soplex` is linked. What remains:
1. **Payoff:** `--polytope` is unmeasured on the main NRA path — `/benchmark` before
   any default flip; a global default is unlikely (LP cost per box). Verdict flips
   would be an integration bug (this is a COMPLETENESS lever).
2. **Tuning:** the X-Taylor knobs (corner policy, slope formula) are fixed at the
   dReal-chosen `RELAX, RANDOM_OPP, HANSEN` — see
   [`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md).

Related: [contractor chapter](../../chapters/contractor.md),
[`../linear/LinearizerXTaylor.md`](../linear/LinearizerXTaylor.md).
