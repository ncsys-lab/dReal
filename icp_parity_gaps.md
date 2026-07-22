# IcpSeq ↔ IcpParallel parity gaps (2026-07-22 audit)

**Worklog** (repo-root convention, like `upstream_gap_audit.md`): the residual divergences
between `src/dreal/solver/icp_seq.cc` and `src/dreal/solver/icp_parallel.cc` after the
2026-07-21 T5 parity campaign, plus the remediation plan. The parity **principle**: `--jobs N`
must be a pure scheduling choice — same features honored, same verdicts, no silently dropped
optimization or config. The campaign closed the *feature* gaps (ODE Lohner, ACID via
`ContractorIbexAcidMt`, `--seed-samples`) and enforces verdict/witness parity in
`test/dreal/solver/test/icp_parallel_parity_test.cc`; the gaps below are what the harness does
**not** see, because they move search order / cost, never verdicts.

None of these is a SOUNDNESS hazard (a false `unsat` requires narrowing past a true model;
search order, over-seeding a worklist, and ignored branch heuristics cannot do that). All are
perf/behavioral divergences; at worst COMPLETENESS-shaped (slower to a verdict within budget).

## Gaps

### G1 — `branching_point` incremental worklist seeding is sequential-only

`IcpSeq`'s stack holds `(Box, branching_dim)` pairs and writes the dim into
`ContractorStatus::mutable_branching_point()` on every pop (`icp_seq.cc:60,125`).
`ContractorWorklistFixpoint::Prune` consumes it (`contractor_worklist_fixpoint.cc:94-118`):
`branching_point >= 0` seeds the worklist with only the contractors whose inputs depend on the
branched dimension — the incremental-repruning optimization. `IcpParallel`'s `global_stack`
holds bare `Box`es and `Worker` never sets the field, so it stays at the initial `-1` and
**every parallel prune full-seeds the worklist**, at every `--jobs` including 1.

Scope: only under `--worklist-fixpoint` (default off, `config.h:334`; `theory_solver.cc:309`).
Flag-gated paths are first-class — "off by default" does not excuse the gap. Effect is
perf-only; full seeding is the conservative direction (over-pruning, never under-).

### G2 — `config().brancher()` honored sequentially, hardcoded in parallel

`IcpSeq` branches via `config().brancher()` (`icp_seq.cc:172`, an API-settable
`std::function`, `Config::mutable_brancher()`); `ParallelBranch` hardcodes
`BranchLargestFirst` when smear is off (`icp_parallel.cc:52`). An API user's custom brancher
is **silently ignored** under `--jobs > 1` — the "silently" is what violates the principle.

Scope: default is `BranchLargestFirst` (`config.h:434`) and the only current setter is
`test/dreal/solver/test/config_test.cc:63`, so no production caller diverges today; the gap is
API-surface truth, not observed behavior.

### G3 — seed-and-verify proposes from different boxes

`IcpSeq` computes `SeedBoxes` from the **un-pruned** root box, before the loop's first Prune
(`icp_seq.cc:96-100`). `IcpParallel` runs an initial main-thread Prune first and seeds from
the **pruned** box (`icp_parallel.cc:204-212`). Same machinery, different input ⇒ potentially
different candidate points at the same `--jobs`. Completeness/speed-only (the root box always
remains on the stack; Prune+EvaluateBox stays the sole arbiter). Side effect of the same
structure: at `--jobs 1` the root box is pruned twice (once at `icp_parallel.cc:207`, again on
first pop) — pure redundant work, cheap because fixpoint-idempotent, but asymmetric.

Seeding from the pruned box is arguably the *better* semantics (candidates inside the
contracted region); remediation should pick one canonically, not preserve both.

### G4 — jobs=1 overhead of the parallel machinery

What `--jobs 1` through `IcpParallel` pays that `IcpSeq` doesn't: libcds lock-free
`Stack<Box>` + `CdsInit`/`CdsScopeGuard` (`icp_parallel.cc:79,223-225`), atomic
`number_of_boxes`/`found_delta_sat` traffic per box, one `ContractorStatus` copy per job plus
the terminal `InplaceJoin` loop (`:258-260,279-282`), and a `ThreadPool(0)`
(`icp_parallel.h` pool of `jobs-1` workers). Magnitude **unmeasured** — a hypothesis until the
R3 A/B below isolates it. Note `--jobs 1` parallel *is* deterministic (zero pool workers; only
the main thread runs `Worker`), so determinism is not among the gaps.

### G5 — stats plumbing differs (observability only)

`IcpSeq` uses one function-local `static IcpStat` shared across every instance and call
(`icp_seq.cc:57`); `Worker` uses `thread_local IcpStat` tagged with the worker id
(`icp_parallel.cc:71`). Reported prune/branch counts and timers are not comparable across
modes, and the seq `static` is itself cross-instance shared state. Cosmetic until someone
compares stats across modes and draws a wrong conclusion.

### G6 — nested forall-CE solve hardcodes the sequential path

The CEGIS counterexample sub-solve pins `number_of_jobs = 1` and therefore `IcpSeq`
(`forall_formula_evaluator.cc:70`, via the `theory_solver.cc:53-55` dispatch). Post-
consolidation this becomes a nested `IcpParallel::CheckSat` on a worker thread — new surface
(nested `CdsScopeGuard`/`CdsInit` attach semantics) that today's parity harness never
exercises.

## Non-gaps (checked, equivalent)

- **Branch-side alternation**: seq's `explore_left_first = !stack_left_box_first`
  (`icp_seq.cc:56`) and parallel's keep/stack inversion (`icp_parallel.cc:57-58,180`) are the
  same policy expressed from opposite sides; the forall contractor's `stack_left_box_first`
  seeding reaches both.
- **Degenerate delta-sat warning (#68)**: both exits warn (`icp_seq.cc:194`,
  `icp_parallel.cc:170` — T8).
- **FE_UPWARD phase hoist, interrupt check, EvaluateBox arbiter**: symmetric.

## Remediation plan (deferred; end-state = consolidate, delete `IcpSeq`)

The end-state under minimize-logic is **one ICP loop**: `IcpParallel` with the gaps closed
supersedes `IcpSeq` (`--jobs 1` = zero pool workers, main thread only, deterministic), and the
`theory_solver.cc:53-55` dispatch collapses. Ordered steps, each gated before the next:

1. **R1 — close G1**: make `global_stack` a `Stack<std::pair<Box, int>>` (or a small struct);
   `ParallelBranch` records the dim with the pushed child and keeps it for the in-hand child;
   `Worker` writes it into `mutable_branching_point()` before each Prune. Test: a
   `--worklist-fixpoint` case in the parity harness asserting jobs=1-parallel prune counts /
   verdicts match seq (red first: today the parallel side full-seeds).
2. **R2 — close G2**: thread `config().brancher()` into `ParallelBranch` in place of the
   hardcoded `BranchLargestFirst` (smear branch unchanged). Test: port
   `config_test.cc`'s `MyBrancher` into a jobs=2 solve asserting the custom brancher runs.
3. **R3 — measure G4 and align G3**: pick the canonical seed input (pruned box, and delete
   seq's variant with it — or justify otherwise), drop the double root-prune, then A/B
   jobs=1-parallel vs `IcpSeq` across the benchmark families (`do_ab.sh`, CPU-time, one pool
   at a time). Gate: zero verdict flips AND PAR2 within the 1.5× regression threshold —
   ideally ~1.0×. If overhead measures real, THAT is the one honest reason to keep both files;
   stop and surface the numbers rather than consolidating anyway.
4. **R4 — consolidate**: delete `icp_seq.{h,cc}`, collapse the `theory_solver.cc` dispatch,
   let the nested forall-CE solve (G6) run through the unified loop, and extend the parity
   harness with a nested-forall + jobs>1 case (CDS nesting surface). Unify stats plumbing
   (G5) as part of the move — per-instance or thread-local tagged, not function-local static.
   `preserve-tests`: the seq-specific tests get ported to the unified entry point, not
   deleted.
5. **R5 — re-verify**: full ctest + `rounding_debug_gate.sh`, the 72-instance parity suite
   (now jobs=N vs jobs=1 within one implementation), and a families-level jobs=4-vs-1 verdict
   diff.

Historical note (hypothesis, not verified from author intent): the split reads as
"`IcpParallel` was the later opt-in experiment" — it introduced the libcds dependency and
until 2026-07 threw on ODE/ACID/seed; G1 never being ported into the parallel loop is
consistent with parity having been incomplete in both directions since its introduction.
