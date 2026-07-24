# Upstream gap audit — soonhokong/dreal4 fork + dreal/dreal4 open issues

> **CORRECTION HEADER (2026-07-22): the audit below is discharged.** The 2026-07-21/22
> tech-debt campaign closed both "Bottom line" port candidates and every reproducing
> defect except the crash class:
>
> - **#320 minimize equality-elimination** ported (`8a68a5f57`); **timer-test flake**
>   fixed (`597b9be21`).
> - **#258 arctan2 false unsat** — SOUNDNESS (asserted φ T-unsatisfiable on a
>   T-satisfiable φ — false unsat) — fixed in the ibex-fork (`a507cd10`, carried in the
>   `e054af7b` pin bump `664f3cac1`).
> - **#264 large-integer fold false unsat** (SOUNDNESS) — exact constant folding of
>   additions, no silent rounding (`27ae0db34`).
> - **#284 Int ≥ 2^31 false unsat** (SOUNDNESS-class) — Int domain is the full
>   double-exact ±2^53, not int32 (`b0794292f` + `1b814b046`).
> - **#280 Int-∀ invalid delta-sat** — COMPLETENESS (asserted φ^δ T-satisfiable on a
>   T-unsatisfiable φ — missed refutation) — forall evaluator now emits an UNSAT
>   refutation certificate (`908c22981`).
> - **#68 / #265 degenerate delta-sat** now warned loudly on stderr (`2c36dd086`) —
>   the silent-at-default-verbosity part of both findings is closed.
> - Behaviors this fork already got right (#324/#323/#302/#315) pinned by tests
>   (`35e41ca18`); the 87456f2ef gcc dangling-reference false positive silenced
>   (`3cf24b011`).
>
> **Remaining live set:** crashes **#176/#86/#80** (uncaught exceptions), **#223**
> (errors abort the process instead of emitting SMT-LIB `(error ...)`), and the
> **#265 unbounded-interval design limit** itself (COMPLETENESS — missed refutation;
> upstream-acknowledged; mitigated by bounding every real, and now warned). The body
> below is the historical 2026-07-21 snapshot — left un-rewritten.

**Point-in-time snapshot, 2026-07-21.** Question: which bugs/fixes/features from
soonho's unmerged fork work (github.com/soonhokong/dreal4) and the upstream issue
tracker (github.com/dreal/dreal4/issues) is this fork missing?

**Binary under test:** `gcc_build/dreal4` built 2026-07-13 from `ibex-transcendental-opt`
@ `c294eb435`. All reproducer runs: default flags unless stated, `timeout 300`,
oom_killer daemon active. Reproducers not inlined below are verbatim from the cited
issue body.

**Source-scope facts (verified via `gh api`):** upstream `master` froze 2023-12-23 and
our snapshot already contains its last solver-facing commit (`--dump-theory-literals`,
present in `src/dreal/dreal_main.cc`) — master contributes nothing. The fork's solver
content lives on branch `20251226` (26 commits; 6 substantive, rest bazel/CI). Other
fork branches: `bazel-8.5`/`CPP17`/`add-cadical`/`update-pkg-config-path` are build-only
or superseded here (we have CaDiCaL + CMake + C++17); `gnn` is a 2023 libtorch
GNN-branching experiment (`--branching-model`) — a feature neither fork ships, noted only.

---

## Part 1 — the fork's substantive commits vs. this fork

| fork commit | what it fixes | status here | evidence |
|---|---|---|---|
| `460639c05` — append the SAT model's positive Boolean literals to learned clauses (upstream #324: disjunct order flipped verdict via ITE-controlling Booleans → false unsat) | **not affected** | Their 3 `disjunct_order_*.smt2` + 3 adversarial variants (swapped disjuncts, only-last-disjunct-sat, genuinely-unsat control) all verdict correctly. Mechanism differs: their fix is a *mask* — the Boolean context makes a learned clause safe even when the theory explanation isn't genuinely T-unsat. We keep the unmasked clause shape (`src/dreal/solver/context_impl.cc:484` blocks only the theory explanation) but attack the root class instead: minimal-relevant explanations, the lemma auditor (`src/dreal/solver/auditor.cc`), and the 2026-06-29 constraint-order explanation-soundness fix. Caveat: any future explanation-soundness bug here re-opens this class, since we have no Boolean-context belt-and-suspenders. |
| `e6b3a2f83` — `let` inside `define-fun` asserted constraints at parse time instead of substituting (upstream #323) | **not affected** | Their 4 `define_fun_let_*.smt2` pass; `get-value` returns exact values (`A_TEST = 4`, nested-let `TEST = 9`). |
| `adcc806f1` + `fa2687aad` — pow underflow (0.5^1075→0) and overflow (2^1024→+inf) guards at three sites (Drake fold, `expression_evaluator`, `ibex_converter`) | **not affected — ours is strictly stronger** | All 4 of their regression files + 5 adversarial probes (incl. −inf overflow, UNSAT-expected positions, non-foldable variable-base paths) verdict correctly. Our design fixes it once at AST construction: `sound_constant_fold` (`src/third_party/com_github_robotlocomotion_drake/dreal/symbolic/symbolic_expression.cc:49-63`, applied at the pow fold `:778` and also mul `:595`/div `:693`, which the fork never covers) folds to a sound `RealConstant` interval, sign-symmetric (`[0,DBL_TRUE_MIN]`, `[DBL_MAX,+inf]`, `[-inf,-DBL_MAX]`); non-foldable paths ride gaol directed rounding; and the ibex-fork `underflow_saturate` backward patch (MIGRATION.md #12) closes a backward-op soundness hole the fork's branch doesn't address at all. Their evaluator guard even has a sign gap (`lb() > 1.0`, +inf-only) that ours doesn't. See `docs/decisions.md` §"Denormal / underflow soundness". |
| `e7ab1ff8a` — Minimize: eliminate equality-defined variables by substitution instead of quantifying them (upstream #320) | **MISSING — confirmed gap** (performance/termination; no soundness dimension) | Our `Context::Impl::Minimize` (`src/dreal/solver/context_impl.cc:582-659`) is byte-for-byte the pre-fix upstream structure: the side-constraint closure (`:613-623`) drags the equality-defined var into `x_vars`, it becomes a quantified forall dim (`:625-629`), unbounded if undeclared (`:635-640`). Reachable from both smt2 (`src/dreal/smt2/parser.yy:224-231`) and `.dr` front-ends. Issue #320's own example: 0.02 s CPU without the unrelated variable → >300 s timeout with it (and still >300 s with the var bounded to [-10,10], so the quantified *dimension*, not just unboundedness, is the cost — the fix's substitution-elimination removes both). Port is a self-contained ~74-line change to one function we carry in near-original form. |
| `c987a6cc3` — Python `set_log_level()` (upstream #319) | **N/A** | This fork ships no Python bindings. |
| `febcd8896` — flaky timer test fix (100× work ratio + volatile sink) | **MISSING — trivial port** | Our `test/dreal/util/test/timer_test.cc:40-63` still has the pre-fix `DoSomeWork(1000)`/`(10)` code; `Timer.Test1` is on our documented known-flaky list. Their fix maps 1:1. |
| `87456f2ef` — silence gcc dangling-reference false positive in `contractor_ibex_fwdbwd_test` | cosmetic, optional | Same `const auto& x = ibex::ExprSymbol::new_()` pattern at our `test/dreal/contractor/test/contractor_ibex_fwdbwd_test.cc:220-222`; the diagnostic is a false positive (heap-allocated referent). Only matters if a gcc version warns. |

Remaining `20251226` commits are bazel-8/fmt-12/CI/python2-removal — all N/A to the
CMake build (and `dc604faa1` tl::optional→std::optional was done here independently).

---

## Part 2 — open-issue sweep (all 51 open issues, snapshot 2026-07-21)

28 issues are build/packaging, docs/questions, or Python-binding-only — out of scope for
this fork (different build system, no bindings): #322 #318 #317 #316 #312 #311 #310 #308
#307 #297 #288 #285 #279 #278 #269 #268 #261 #256 #238 #153 #101 #46, feature requests
#226 #223 #193 #148 #122 #85 #83 #75 #68(feature part) #30. Of note among these: #193 is
resolved here (SAT backend is CaDiCaL); #223's point that errors abort the process
instead of emitting SMT-LIB `(error ...)` still applies (every crash below is an uncaught
exception).

Correctness findings, each verdict re-verified firsthand on the pinned binary:

### Defects that reproduce in this fork (fixed by NEITHER fork — upstream still open)

**#258 — arctan2 with unbounded argument: false unsat.** SOUNDNESS (asserts φ
T-unsatisfiable on a T-satisfiable φ — false unsat). Minimal reproducer (trivially sat,
y=0 ⇒ z=π/2):
```smt2
(declare-fun y () Real) (declare-fun z () Real)
(assert (= z (arctan2 3 y)))
(check-sat)                          ; → unsat
```
Bounding y (e.g. y∈[3.9,4.1]) gives the correct delta-sat, so the trigger is the
unbounded second argument. Hypothesis (not isolated): IBEX fwdbwd on `z−atan2(3,y)=0`
empties y from ENTIRE — matches upstream's trace into IBEX; our ibex-fork evidently
retains it. **Highest-priority follow-up from this audit.**

**#264 — large-integer constant folding: false unsat.** SOUNDNESS (false unsat).
Verbatim reproducer, `--precision 1e-100`:
```smt2
(assert (not (= (+ 9000000000000000 9000000000000001)
                (+ 9000000000000000 9000000000000000))))
(check-sat)                          ; → unsat, but sums differ by exactly 1
```
Hypothesis (mechanism not isolated in source): the sum 18000000000000001 exceeds 2^53
and ties-to-even to 18000000000000000 during constant folding, folding the equality to
true. `sound_constant_fold` covers underflow/overflow but not inexact large-integer
rounding.

**#284 — Int-sorted constant ≥ 2^31: false unsat.** SOUNDNESS (false unsat — on an
assertion-free, trivially satisfiable query):
```smt2
(define-fun x() Int 0x80000000)
(check-sat)                          ; → unsat  (0x7FFFFFFF → delta-sat)
```
Localized: decimal `2147483648` also unsat, `Real`-sorted hex fine — trigger is
Int-sorted value ≥ 2^31, not hex parsing. Hypothesis: an implicit int32 domain on Int
variables.

**#280 — Int-sorted existentials under ∀: invalid delta-sat.** COMPLETENESS (asserts φ^δ
T-satisfiable on a T-unsatisfiable φ — missed refutation). Issue's Real variant → correct
`unsat`; Int variant → `delta-sat with delta = 0.01` with model c=1,d=30,e=−27,f=22
violating the ∀-equality at p=0 by 21 ≫ δ.

**#265 — unbounded intervals admit infinity witnesses.** COMPLETENESS (missed
refutation). Both issue formulas return delta-sat with a `[-inf, -1.797e308]` variable
(re-verified: `c ≤ −8 ∧ −0.5 ≤ d ≤ 1 ∧ d·(−c) ≤ c` is unsat for every finite c by margin
≥ 4, "satisfied" only at c=−∞ in extended-real interval arithmetic). Upstream
acknowledged this as the unbounded-interval/IEEE-double design limitation; we inherit it.
Mitigation available today: bound every real (already the guidance in
`docs/writing-fast-dreal-formulas.md`).

**#68 — non-bisectable box returns delta-sat.** COMPLETENESS (missed refutation).
Verbatim instance (`:status unsat`, violated by ~1e10 ≫ δ) → `delta-sat with delta =
0.001`, exit 0. Unchanged code: `src/dreal/solver/icp_seq.cc:184-190` returns delta-sat
when the δ-violating box can't be bisected, with only a `DREAL_LOG_DEBUG` note —
invisible at default verbosity.

**Crashes (uncaught exceptions, exit 134):**
- **#176** — Boolean vars + multiple objectives: verbatim upstream message (`Variable v2_
  is of type BOOLEAN and it should not be used to construct a symbolic expression.`).
- **#86** — Boolean-variable ITE conditions inside `forall`: crashes at
  `generic_contractor_generator.cc:150 "Negation is detected."` (different message than
  upstream, same trigger; ITE conditioned only on the universal var works).
- **#80** — `let` inside `forall`: verbatim defect at `ibex_converter.cc:144 "Variable x
  is not appeared in L0\y"` (let-hoisting leaks the ∀-bound variable).
- **#178/#177** — multi-objective `minimize`/`maximize`: upstream's `symbolic.cc`
  assertions are gone; we reject loudly (nested-forall unsupported,
  `context_impl.cc:93`). Superseded, still a terminating rejection of that fragment.

### Reports where this fork is correct

- **#315** — Boolean-valued `define-fun`: correct `unsat` on `a=1 ∧ a=2` (upstream
  ignored the body → spurious sat).
- **#302** — "wrong delta-sat on unsat query": delta-sat is the *correct* δ-complete
  verdict (the δ-weakening of `expr1−expr2 > 0` with expr1≡expr2 is satisfiable); only a
  cosmetic model artifact remains (identical aux expressions printed with different
  overflow-region intervals — same ±inf mechanism as #265).
- **#321** — pow underflow: fixed independently (Part 1).
- **#314** — Python-API `a * Expression(-1)` aliasing: inexpressible here (no bindings).

---

## Bottom line (ranked)

Port candidates from the fork (only two):
1. **`e7ab1ff8a` minimize equality-elimination** — confirmed ≥15,000× / non-terminating
   gap on issue #320's own example; self-contained ~74 lines in a function we carry
   near-verbatim.
2. **`febcd8896` timer-test flake fix** — trivial, retires a documented known-flaky test.

Everything else the fork fixed, this fork either fixed independently and more strongly
(pow under/overflow, #323 define-fun let, #315) or doesn't exhibit on every reproducer
tried (#324 learned clauses — with the noted no-mask caveat).

Defects surfaced by the sweep that *neither* fork fixes, in severity order: **#258
arctan2 false unsat (SOUNDNESS — the one live soundness bug found)**, #264 large-integer
fold false unsat (SOUNDNESS; obscure inputs), #284 Int ≥ 2^31 false unsat (SOUNDNESS-class;
Int fringe), #280 Int-∀ missed refutation (COMPLETENESS), #265 infinity-witness delta-sat
(COMPLETENESS; upstream-acknowledged design limit, mitigated by bounding), #68
non-bisectable delta-sat (COMPLETENESS; at minimum deserves a loud warning), crashes
#176/#86/#80.
