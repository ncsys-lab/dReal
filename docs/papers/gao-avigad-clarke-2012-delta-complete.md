# $\delta$-Complete Decision Procedures for Satisfiability over the Reals

**File:** `papers/gao-avigad-clarke-2012-delta-complete.pdf`
**Authors:** Sicun Gao, Jeremy Avigad, Edmund M. Clarke (Carnegie Mellon University)
**Venue:** IJCAR 2012 (6th International Joint Conference on Automated Reasoning), LNAI 7364,
pp. 286–300, Springer.

> Lineage position: theory ([gao-avigad-clarke-2012-delta-decidability](gao-avigad-clarke-2012-delta-decidability.md)) $\to$ **procedure**
> (this paper) $\to$ tool ([gao-kong-clarke-2013-dreal](gao-kong-clarke-2013-dreal.md)) $\to$ $\exists\forall$ extension
> ([kong-solar-lezama-gao-2018-exists-forall](kong-solar-lezama-gao-2018-exists-forall.md)). This is the paper that defines the **$\delta$-SMT
> problem** and proves **DPLL(ICP)** is $\delta$-complete — the exact algorithm dReal implements.

## Core Claim

Introduces the **$\delta$-SMT problem** and **$\delta$-completeness** as the correct soundness
requirement for numerically-driven SMT solvers (the conventional notion of completeness "can
never be met in this context"). Given a formula $\varphi$ and $\delta \in \mathbb{Q}^+$, a
$\delta$-complete procedure returns **`unsat`** ($\varphi$ is unsatisfiable) or **`delta-sat`** (the
$\delta$-weakening $\varphi^\delta$ is satisfiable). The paper proves the bounded $\delta$-SMT
problem is decidable, pins its complexity, and — central for this codebase — gives **sufficient
and necessary conditions** under which the **DPLL(ICP)** framework (Interval Constraint
Propagation inside DPLL(T)) is $\delta$-complete.

## Key Results

- **$\delta$-weakening** (Def. 3.1). For $\varphi := \exists \boldsymbol{x}.\, \bigwedge_i
  (\bigvee_j f_{ij}(\boldsymbol{x}) = 0)$ in standard form, $\varphi^\delta := \exists
  \boldsymbol{x}.\, \bigwedge_i (\bigvee_j |f_{ij}(\boldsymbol{x})| \leq \delta)$. Standard-form
  reduction (Lemma 2.1) turns any bounded $\Sigma_1$-sentence into a conjunction of equalities.
- **$\delta$-SMT problem** (Def. 3.2): `unsat` ($\varphi$ false) or `delta-sat` ($\varphi^\delta$
  true); when both hold, either answer is allowed. **Decidable** (Thm. 3.1); complexity is
  $\mathsf{NP}^{\mathsf{C}}$ (Thm. 3.2), **NP-complete** for $\{+,\times,\exp,\sin\}$ (Cor. 3.1),
  **PSPACE-complete** for Lipschitz ODEs (Cor. 3.2).
- **Well-defined pruning operators** (Def. 4.3) — the heart of the soundness story. A pruning
  operator $\mathrm{Prune}_\sharp(B, f)$ must satisfy: **(W1)** $\mathrm{Prune}(B,f) \subseteq B$
  (contraction); **(W2)** if the result is non-empty then $0 \in \sharp f(\mathrm{result})$;
  **(W3)** $B \cap Z_f \subseteq \mathrm{Prune}(B,f)$ — pruning **never discards a real solution**
  ($Z_f$ = zero set of $f$).
- **Theorem 4.2 ($\delta$-Completeness of $\mathrm{ICP}_\varepsilon$).** $\mathrm{ICP}_\varepsilon$
  (the decision version of branch-and-prune, Alg. 1) is $\delta$-complete for conjunctive
  $\Sigma_1$-sentences **if and only if** the pruning operator is well-defined. Box-consistent
  pruning is well-defined (Def. 4.4, Prop. 4.2).
- **ODEs** (§4.2). For an IVP $\dot{\boldsymbol{y}} = g(\boldsymbol{y})$, an interval ODE solver
  returns boxes enclosing the trajectory; the induced pruning operator (Prop. 4.3, "Simple
  ODE-Pruning") is well-defined, so $\mathrm{ICP}_\varepsilon$ is $\delta$-complete for
  equalities involving ODEs.
- **DPLL(ICP)** (§4.3) and **Cor. 4.1**: putting $\mathrm{ICP}_\varepsilon$ as the theory solver
  inside DPLL(T), the full procedure is $\delta$-complete **iff** the pruning operators are
  well-defined.
- **Applications** (§5): bounded model checking + invariant validation (an `unsat` answer means
  "safe up to depth $n$"; a `delta-sat` answer means "unsafe under some $\delta$-perturbation");
  theorem proving by refining $\delta$ on $\neg\varphi$.

## Systems / Functions This Paper Motivates

| Paper concept | Where it lives in this project |
|---|---|
| DPLL(ICP) = ICP as the theory solver in DPLL(T) | `docs/architecture.md` (SAT layer + Theory layer + ICP loop); `src/dreal/solver/theory_solver.cc`, `icp.h` / `icp_parallel.cc` |
| Branch-and-prune $\mathrm{ICP}_\varepsilon$ (Alg. 1) | The ICP loop in `docs/architecture.md` §"ICP Loop"; `Icp` implementations |
| **(W3)** pruning never discards a real solution | **The soundness invariant** in `docs/soundness-vs-completeness.md`: a contractor is an outward over-approximation, so a looser enclosure can never cause a false `unsat`. (W3) *is* this argument, formalized. |
| **(W1)/(W2)** contraction + zero-containment | `docs/contractors.md` — what every contractor must guarantee; the IBEX HC4 forward/backward operators |
| $\delta$-weakening of atoms | `DeltaStrengthen` in `src/dreal/symbolic/symbolic.cc` |
| ODE pruning well-definedness (Prop. 4.3) | `docs/ode-integration.md` §Soundness — feed-faithful interval enclosure = a well-defined ODE pruning operator |
| BMC / invariant-validation application | `docs/forall-semantics.md` §8 (Lyapunov / safety); the QF_NRA_ODE BMC benchmarks |

## Open Proof Targets

- Connect (W3) to the project's contractor-soundness claim verbatim: show each contractor in
  `docs/contractors.md` satisfies (W1)–(W3), making "looser enclosure $\Rightarrow$ completeness
  loss, never soundness loss" a corollary of Thm. 4.2 rather than a standalone assertion.
- Confirm the CAPD ODE contractor's enclosure satisfies the Prop. 4.3 well-definedness
  conditions (interval extension respects $\sharp y_i$) — the formal underpinning of
  `docs/ode-integration.md` §Soundness.

## Notes

- **This is the algorithm dReal implements.** The "well-defined pruning operator" conditions
  (W1/W2/W3) are the formal source of the project's repeated claim that *over-approximating
  contractors are sound no matter how loose* — see `docs/soundness-vs-completeness.md`.
  Looseness costs **refutation power (completeness)**, never soundness, precisely because (W3)
  holds.
- **Distinct from the LICS'12 paper.** This IJCAR paper is the *decision procedure*; the
  same-year LICS paper [gao-avigad-clarke-2012-delta-decidability](gao-avigad-clarke-2012-delta-decidability.md) is the *decidability/
  complexity theory*. The docs previously conflated the two (citing this title against the LICS
  venue) — fixed when these summaries were added.
- **Misalignment (components, not framework).** The DPLL(ICP) *framework* here is exactly
  dReal4's architecture, but every concrete component named in the 2013 tool paper has since
  been replaced — see [gao-kong-clarke-2013-dreal](gao-kong-clarke-2013-dreal.md) Notes (opensmt$\to$CaDiCaL,
  realpaver$\to$IBEX, plus CAPD for ODEs).
