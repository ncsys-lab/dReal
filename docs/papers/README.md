# Papers — foundational literature for dReal4

Companion summaries for the papers that define the theory dReal implements. Each `.pdf` has a
sibling `.md` summary (Core Claim · Key Results · Systems/Functions it motivates · Open Proof
Targets · Notes, incl. **misalignments with the current code**). The PDFs predate the
continually-developed source, so every summary flags where paper and code diverge.

These four form a single lineage — **theory → procedure → tool → ∃∀ extension** — all by
Sicun Gao and collaborators (the original dReal developers):

| Paper (summary) | Citation | Role | Cross-referenced into |
|---|---|---|---|
| [Delta-Decidability over the Reals](gao-avigad-clarke-2012-delta-decidability.md) | Gao, Avigad, Clarke — LICS 2012, pp. 305–314 | **Theory**: δ-decision problem is decidable; NP/PSPACE complexity; boundedness+robustness necessary | `soundness-vs-completeness.md`, `smt-model-theory` skill |
| [δ-Complete Decision Procedures for Satisfiability over the Reals](gao-avigad-clarke-2012-delta-complete.md) | Gao, Avigad, Clarke — IJCAR 2012, LNAI 7364, pp. 286–300 | **Procedure**: δ-SMT, well-defined pruning (W1–W3), DPLL(ICP) is δ-complete | `soundness-vs-completeness.md`, `architecture.md`, `contractors.md`, `symbolic.cc` |
| [dReal: An SMT Solver for Nonlinear Theories over the Reals](gao-kong-clarke-2013-dreal.md) | Gao, Kong, Clarke — CADE-24 2013 (LNAI 7898, pp. 208–214) | **Tool**: the original dReal; certificates; Flyspeck results | `architecture.md`, `theory_solver.cc`, `auditor.cc`, `icp.h` |
| [Delta-Decision Procedures for Exists-Forall Problems over the Reals](kong-solar-lezama-gao-2018-exists-forall.md) | Kong, Solar-Lezama, Gao — CAV 2018, LNCS 10982, pp. 219–235 | **∃∀ extension**: CEGIS-in-branch-and-prune; double-sided error control | `forall-semantics.md`, `contractor_forall.h` |

## Headline misalignments flagged across these summaries

- **Backend drift** (2013 tool paper): "built on opensmt + realpaver" → dReal4 uses **CaDiCaL +
  IBEX + CAPD**. The DPLL(ICP) framework and the δ-sat/`unsat` contract are unchanged; the
  components are all replaced.
- **Citation fix** (both 2012 papers): the docs previously cited the IJCAR title *δ-Complete
  Decision Procedures* against the **LICS** venue — two distinct 2012 papers, now split correctly
  (theory = LICS, procedure = IJCAR).
- **∃∀ implementation artifacts** (2018 paper): nested-`forall` crash and the `--polytope`
  LP-not-linked crash are un-handled-input/packaging issues outside the paper; the pinned
  `ε = δ/2`, `inner = ε/2` is one valid instantiation of the paper's general `δ' < ε < δ`.

## Reading for the theory after QF_NRA_ODE (`future/`)

The papers behind [`docs/future-theory.md`](../future-theory.md), the requirements record for the
successor theory. Each note maps the paper onto that document's requirement IDs and open decisions
and lists where the paper contradicts what the document recalled. Venues are given as the PDF
prints them. Where the PDF prints none, the venue is marked as from memory.

| Paper (note) | Citation | What it settles for `future-theory.md` |
|---|---|---|
| [Satisfiability Modulo ODEs](future/gao-2013-sat-modulo-odes.md) | Gao, Kong, Clarke. Printed pp. 105–112. Venue not printed (FMCAD 2013 from memory) | Defines the problem, not a syntax. Formulas range over a signature in which the ODE solution is a fixed function symbol, and the invariant is an ordinary bounded $\forall$ that shares the term $x_0$ with its flow. The 2013 semantics was compositional. dReal's linking rule is not in it. $\delta$-weakening touches arithmetic atoms only. |
| [dReach: δ-Reachability Analysis for Hybrid Systems](future/kong-2015-dreach.md) | Kong, Gao, Chen, Clarke. LNCS 9035 (TACAS 2015), pp. 200–205 | Where `flow_N`, numbered modes and `forall_t n` enter, as the SMT-LIB that dReach emits from `.drh`. Resets are relations and guards are permissive. The unrolling itself is deferred to a technical report. |
| [SAT Modulo ODE](future/eggers-2008-sat-modulo-ode.md) | Eggers, Fränzle, Herde. LNCS 5311 (ATVA 2008), pp. 171–185 | The sibling design. ODE enclosures are narrowing operators in an interval search. Binding is by unrolling depth and variable name, so it is not compositional either. The duration horizon is a solver option, which the meaning of a formula should never depend on. |
| [A quantifier-free SMT encoding of non-linear hybrid automata](future/cimatti-2012-qf-encoding-nonlinear-ha.md) | Cimatti, Mover, Tonetta. Venue not printed (FMCAD 2012 from memory) | One automaton and one global time. A closed-form solution replaces the ODE. Quantifier-freeness is proved when solution and invariant are polynomial in time. Singular intervals allow several discrete steps at one instant. Networks and local time are only cited. |
| [Quantifier-free encoding of invariants for hybrid systems](future/cimatti-2014-qf-invariants.md) | Cimatti, Mover, Tonetta. Formal Methods in System Design 45 (2014), pp. 165–188 | The $\forall t$ invariant is eliminated exactly, by the mean value theorem, when invariant and solution are polynomial in time. Split points become extra steps. The output is a `forall_t` under a disjunction, which is plain first-order logic in the successor theory. |
| [Partial Order Reductions for Timed Systems](future/bengtsson-1998-local-time.md) | Bengtsson, Jonsson, Lilius, Yi. Printed pp. 485–500. Venue not printed (CONCUR 1998 from memory) | Local-time semantics is a pruning device over an interleaving the method keeps. It adds no behaviour, and reachability agrees with global time. Independent phases come from the global-time semantics itself. |
| [Hybrid Systems in TLA+](future/lamport-1993-hybrid-tla.md) | Lamport. LNCS 736 (Springer chapter 3-540-57318-6_25), pp. 77–102 | Whole specifications are conjoined, actions are disjoined only within a component, and `now` advances by a conjunct. Continuous variables obey Riemann-integral equations built with `choose`, so a missing solution passes silently. Zeno is not addressed. |
| [Hybrid I/O automata](future/lynch-2003-hybrid-io-automata.md) | Lynch, Segala, Vaandrager. Information and Computation 185 (2003), pp. 105–157 | Trajectories and alternating sequences are the semantic objects. Both values at an action instant are kept, and neither is a limit. Composition is on automata, and traces project. Zeno executions are allowed. Non-Zeno is a property of an automaton. |
| [A Framework for Comparing Models of Computation](future/lee-1998-tagged-signal-model.md) | Lee, Sangiovanni-Vincentelli. IEEE TCAD 17(12), December 1998, pp. 1217–1229 | A signal is any set of tag-value pairs. The order on the tags is what distinguishes models of computation. Composition is intersection and projection is $\exists$. Causality is defined under the Cantor metric and needs an input/output split. |
| [From Timed to Hybrid Systems](future/maler-1992-timed-to-hybrid.md) | Maler, Manna, Pnueli. Printed pp. 447–484. Venue not printed (LNCS 600, from the Springer chapter id BFb0032003) | The time structure is pairs of a phase index and a time, ordered lexicographically and induced per trace. The word superdense does not appear. Non-Zeno is part of the definition of a computation. Rule INVH ties a $\forall t$ invariant to its trajectory by a shared function. |
| [Non-standard semantics of hybrid systems modelers](future/benveniste-2012-nonstandard-hybrid.md) | Benveniste, Bourke, Caillaud, Pouzet. JCSS 78 (2012), pp. 877–910 | Non-standard time with infinitesimal steps, preferred over superdense time. Cascades are successive steps. Standardisation needs unique solutions and finitely many cascaded steps. Argues against a non-Zeno condition in the semantics. |
| [Differential Dynamic Logic for Hybrid Systems](future/platzer-2008-dl.md) | Platzer. J. Automated Reasoning 41 (2008), pp. 143–189 | The ODE sits inside a modality that is a relation on states. The flow is existentially quantified in the semantics and never a term. Solutions are classical and unique. There is no parallel composition. Rule D12 rewrites the ODE box as $\forall t$ over the solution. |
| [Differential-Algebraic Dynamic Logic DAL](future/platzer-2010-dal.md) | Platzer. Chapter 3 of *Logical Analysis of Hybrid Systems*, Springer 2010, DOI 10.1007/978-3-642-14509-4_3, pp. 123–202 | Differential-algebraic constraints are first-order formulas over $x$ and its derivatives, and a disturbance is an $\exists$ inside the constraint chosen at each instant. Solutions are classical and need not be unique. Zeno runs are excluded by definition. Invariants are a modality proved by differential induction. |
| [Uniform Substitution for Dynamic Logic with Communicating Hybrid Programs](future/brieger-2023-dlchp.md) | Brieger, Mitsch, Platzer. Springer 2023 (chapter 978-3-031-38499-8_6; CADE 2023 from memory) | A uniform-substitution calculus for a language defined in a companion paper. One global clock, components with disjoint continuous variables, synchronous rendezvous with non-decreasing timestamps, and no shared continuous state. |
| [Monitoring Temporal Properties of Continuous Signals](future/maler-2004-stl.md) | Maler, Nickovic. LNCS 3253 (2004), pp. 152–166 | Bounded always and eventually are $\forall$ and $\exists$ over an interval at an implicit instant, dual under negation. Atoms are static predicates on the current value. Finite variability is part of the signal definition. Satisfaction is undefined on a signal shorter than the formula needs. |
| [The SMT-LIB Standard, Version 2.7](future/smtlib-2025-standard-2.7.md) | Barrett, Fontaine, Tinelli. Release of 5 February 2025 | The base language the successor theory would extend: sorts, theories as classes of structures, logics, commands and models. |
| [Extending SMT Solvers to Higher-Order Logic](future/barbosa-2019-ho-smt.md) | Barbosa, Reynolds, El Ouraoui, Tinelli, Barrett. Printed pp. 35–54 (CADE 2019 from memory) | Henkin semantics with extensionality and choice, in CVC4 and veriT. Function variables are instantiated only with functions already in the problem. Models are almost-constant functions, which cannot witness a trajectory. |
| [An Automatable Formal Semantics for IEEE-754 Floating-Point Arithmetic](future/brain-2015-fp-semantics.md) | Brain, Tinelli, Rümmer, Wahl. Revision of 11 June 2015. Venue not printed (ARITH 2015 from memory) | The theory is a class of interpretations. RoundingMode is a sort and an explicit argument. Only Real-to-float rounds, and float-to-Real is exact. Out-of-range cases are total but unspecified. Reals and bitvectors share one signature through a disjoint-union universe. |
| [Verification of Analog/Mixed-Signal Circuits Using Labeled Hybrid Petri Nets](future/little-2011-lhpn-ams.md) | Little, Walter, Myers, Thacker, Batchu, Yoneda. IEEE TCAD 30(4), April 2011, pp. 617–630 | Continuous variables with constant rates in a range. The digital state is the marking plus Boolean signals. Zero-delay firings happen in zero time with every order explored. The digital side sees the analog side only through threshold predicates. The rate-range abstraction is neither sound nor complete. |
| [Discontinuous Dynamical Systems: A Tutorial](future/cortes-2008-discontinuous-tutorial.md) | Cortés. Venue not printed (IEEE Control Systems Magazine 2008 from memory) | No solution concept is canonical. Classical, Carathéodory, Filippov and sample-and-hold solutions with their existence and uniqueness conditions. Filippov for switches and sliding, Carathéodory for jumps in time. One differential inclusion covers all three. |

## Adding a paper

Drop the PDF in `unorganized/` (or `future/unorganized/` for the successor-theory reading) and
invoke `/lit-review` (Phase 2). The skill reads page 1 to
identify it, renames to `author-year-keyword.pdf`, and creates the `.md` from the template
below. **Source-fidelity:** every cited detail must be verified in the PDF, not recalled; math in
the `.md` uses pandoc inline `$...$` (these compile to PDF via the `reading-packet` skill, where
bare unicode math symbols are missing from the body font).

### Companion `.md` template

```markdown
# [Full Paper Title]

**File:** `papers/<filename>.pdf`
**Authors:** ...
**Venue:** ...

## Core Claim
One paragraph: what the paper proves/shows and why it matters here.

## Key Results
Bullet list of the key theorems, definitions, or empirical findings.

## Systems / Functions This Paper Motivates
Table mapping paper concepts → project code (factories, utils, contractors, docs).

## Open Proof Targets
Concrete proof obligations this paper suggests, phrased as dReal/SymPy tasks.

## Notes
Non-obvious caveats — especially **misalignments with the current code** (the PDFs are
older than the source).
```
