# dReal4 Documentation

Deeper reference documentation beyond the project-level `CLAUDE.md`.

| Document | Contents |
|---|---|
| [build.md](build.md) | Build-system internals — `--version` output and how its git hash/dirty flag, OS/arch, and timestamp are wired in (CMake git-version target, compile definitions, Docker `.git` copy) |
| [architecture.md](architecture.md) | Full solving pipeline — preprocessing, SAT/theory layers, ICP loop, branching, Box, ContractorStatus, UNSAT explanations |
| [contractors.md](contractors.md) | All contractor kinds, how HC4 forward-backward works, fixpoint/worklist variants, join, how TheorySolver composes them |
| [ode-integration.md](ode-integration.md) | CAPD order-20 Taylor integration (forward/backward/trace), hybrid system encoding, input format examples |
| [decisions.md](decisions.md) | Architecture & soundness ADRs — ODE backend (Codac→CAPD-only), per-slice tube, ODE feed faithfulness, denormal/underflow (#321), backward narrowing |
| [pattern-matching.md](pattern-matching.md) | CAV26 lemma reuse — De Bruijn canonicalization, substitution tree matching, bijection checking, symmetry filtering, CaDiCaL integration |
| [api-guide.md](api-guide.md) | C++ API: Variable/Expression/Formula/Box types, CheckSatisfiability, Minimize, Config options, incremental Context, linking |
| [qf_nra_ode_semantics.md](qf_nra_ode_semantics.md) | Deep QF_NRA_ODE semantics: define-ode vars vs. pars, integral grammar and invariants, forall_t linking and flattening, negated ODE constraints (semantically ignored), edge cases grounded in parser and C++ source |
| [syntax-reference.md](syntax-reference.md) | Complete SMT2 syntax reference: all sorts (Real/Int/Bool), numeric literal forms, every expression operator and trig function (including arcsin aliases, xor n-ary semantics, ite desugaring), quantifiers, let, all solver commands, edge cases |
| [writing-fast-dreal-formulas.md](writing-fast-dreal-formulas.md) | **Standalone authoring guide for external SMT-LIB2 generators** — how ICP works at a high level, and how to structure formulas for speed: the variable-occurrence/dependency-problem master rule (factored ≫ expanded, measured), bound-every-real, `let`/aux tradeoffs, ODE & ∀ tips, and the `--polytope`/`--acid` escape hatches. Complements the *syntax* reference with *performance* guidance |
| [rounding.md](rounding.md) | FPU rounding regimes (`FE_UPWARD`/`FE_TONEAREST`), `RoundingModeGuard` mechanism, phase-hoisting, sanctioned clobberers, typed directed-rounding doubles, source-hygiene lint rules |
| [benchmarking.md](benchmarking.md) | Local co-run spot checks (corun.sh, measure.py: E-cores, CPU cap, ratios only) vs Sherlock runs, families/weighting, thresholds, cross-solver comparison, manual invocation |
| [soundness-vs-completeness.md](soundness-vs-completeness.md) | T-relation definitions, dReal's `unsat`/`δ-sat` guarantees, worked F1 example |
| [future-theory.md](future-theory.md) | Requirements and discussion record for the theory that replaces QF_NRA_ODE: what is wrong with the current theory as model theory, the forall_t / quantifier / concurrency / time answers, the requirement catalog (K = Kunal's, C = proposed), open decisions, and the reading list whose papers go in `papers/future/` |
| [papers/](papers/README.md) | Foundational Gao et al. literature — companion summaries (theory→procedure→tool→∃∀ extension) cross-referenced into the docs above, with paper↔code misalignments flagged |
