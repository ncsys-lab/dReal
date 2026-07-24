# KNOBS.md — the complete IBEX tuning surface, audited

Every IBEX-side tunable that affects dReal's contraction, its option menu, the
IBEX default, and **dReal's current status**. Audit-by-construction: if a knob
isn't here, it isn't being left on the table. Defaults are confirmed against the
fork headers (cited per row); the `.rst` docs leave several "to be completed", so
**the header is ground truth**. IBEX is **2.9.1** (`ibex-fork/CMakeLists.txt:4`).

Legend — dReal status: 🟢 active · 🟡 shipped but default-off (opt-in flag) ·
⚪ unused (no code) · 🔒 fixed at default (not exposed as a flag).

## 1. Build-time flags (the two CMake knobs)

| Knob | Menu | dReal value | Status | Note |
|---|---|---|---|---|
| `INTERVAL_LIB` | gaol / filib / … | **gaol** | 🟢🔒 | the patched backend (fork gaol patches); arm64-native. `CMakeLists.txt:188` |
| `LP_LIB` | none / soplex / clp | **soplex** | 🟢🔒 | vendored SoPlex 4.0.2 (`libsoplex.a`); the LP backend that makes the **polytope path live** (§4). `CMakeLists.txt:189` |

## 2. Active path — HC4 forward-backward + propagation

dReal runs the HC4 algorithm but through its **own** fixpoint loop, so the IBEX
class defaults below are the *reference*, not literally dReal's values.

| Knob | Class · header | Menu / range | IBEX default | dReal status |
|---|---|---|---|---|
| fixpoint ratio (propagation) | `CtcPropag` | (0,1) | **0.01** (constexpr; a stale comment says 0.1) | 🟢 own worklist; analog knob in `contractor_worklist_fixpoint.cc` |
| fixpoint ratio (plain) | `CtcFixPoint` | (0,1) | **0.1** | 🟢 analog in `contractor_fixpoint.cc` |
| `accumulate` flag | `CtcPropag` | bool | false | ⚪ — "slightly tighter, a little slower" |
| input/output bitsets | `Ctc` | per-var | unset | 🟢 own analog (skip-when-can't-fire) |
| backward callback | `Function::backward` | on/off | (fork-added) | 🟢 **on** — drives theory lemmas (fork #2/#5/#6/#7) |

## 3. Shaving + system contractors — SHIPPED opt-in (audit A + the 2026-07 campaign)

The shaving contractors the original audit flagged are live, and the 2026-07
RELATED-WORK campaign shipped the rest (Newton/OBBT/Mohc — measured verdicts in
`../OPTIMIZATION_LOG.md` §"RELATED-WORK candidate campaign"). `--acid`/`--3bcid`
are **mutually exclusive** (`dreal_main.cc` throws if both set); all default off, all
run under `--jobs>1` via per-worker `*Mt` cells. All system-wide cells filter out
`forall`/ODE atoms (`FilterIbexConvertible` — `IbexConverter` throws on
`integral`/`forall_t`; COMPLETENESS-only drop, fixed `f6d735254`).

| Knob | Class · header | Menu / range | IBEX default | dReal status |
|---|---|---|---|---|
| enable ACID | dReal `config` | bool | — | 🟡 **`--acid`** (off) |
| enable 3BCID | dReal `config` | bool | — | 🟡 **`--3bcid`** (off, excl. `--acid`) |
| **`s3b`** (max shaved slices) | `Ctc3BCid` | int, best **5–200** | **10** — *"tune in priority"* | 🟢 **`--s3b`** (default 10; `config.h` `kDefaultAcidS3b`) |
| **`ct_ratio`** (adaptive kernel) | `CtcAcid` | double | **0.002** (constexpr; ctor *comment* says 0.005 — code wins) | 🟢 **`--acid-ct-ratio`** (default 0.002; `kDefaultAcidCtRatio`) |
| `scid` (CID slices) | `Ctc3BCid` | int | **1** — *"don't touch"* | 🔒 not exposed |
| `vhandled` (vars/contract) | `Ctc3BCid` | int / −1=all | −1 | 🔒 (ACID computes this) |
| `var_min_width` | `Ctc3BCid` | double | **1e-11** | 🔒 not exposed |
| `optim` (objective-first) | `CtcAcid` | bool | false | 🔒 (passed `false`; n/a for SMT) |
| enable Newton | dReal `config` | bool | — | 🟡 **`--newton`** (off; square equality subsystem; **measured dead 2026-07** — zero unique solves, github 2.75×, 11 OOMs — kept for record) |
| **`ceil`** (Newton gate) | `CtcNewton` | double | **0.01** | 🟢 **`--newton-ceil`** (default 0.01) |
| `prec`, `gauss_seidel_ratio` | `CtcNewton` | double | lib defaults | 🔒 not exposed |
| enable OBBT | dReal `config` | bool | — | 🟡 **`--obbt`** (off; certified 2n-LP over the X-Taylor relaxation; **measured ≈polytope, dead**; BUG-014-exposed — `../docs/dreal-bugs.md`) |
| enable Mohc | dReal `config` | bool | — | 🟡 **`--mohc`** (off; `CtcMohc` fork port, composable with `--acid`/`--3bcid`; **measured: the odeexpr-only winner** — 115/151, 0.968× — net-negative on the ODE families) |

**∃∀ pre-pruner (audit-quantifiers Q1, SHIPPED):**

| Knob | Class · header | Menu / range | IBEX default | dReal status |
|---|---|---|---|---|
| enable ∀-pre-prune | dReal `config` | bool | — | 🟡 **`--forall-pre-prune`** (off; sound `ibex::CtcForAll` proj-intersection beside CEGIS) |
| universal-box bisection prec | `contractor_ibex_forall` | double | — | 🟢 **`--forall-pre-prune-prec`** (default 0.5; `kDefaultForallPrePrunePrec`) |

## 4. Polytope hull + X-Taylor — SHIPPED opt-in (`LP_LIB=soplex`, live)

`--polytope`/`--forall-polytope` are live (default off), assembled at
`theory_solver.cc:237`, per-worker `ContractorIbexPolytopeMt` under `--jobs>1`. dReal
constructs `LinearizerXTaylor(system, RELAX, RANDOM_OPP, HANSEN)` +
`CtcPolytopeHull(...)` at `contractor_ibex_polytope.cc:108`, so the linearizer knobs
below are **hardcoded at those IBEX defaults**, not yet exposed as flags.

| Knob | Class · header | Menu / range | IBEX default | dReal status |
|---|---|---|---|---|
| `--polytope` / `--forall-polytope` | dReal `config` | bool | — | 🟡 **off, opt-in** (`config.h:330-331`) |
| `max_iter` | `CtcPolytopeHull` | int | 100 | 🔒 hardcoded |
| `time_out` | `CtcPolytopeHull` | seconds | 100 | 🔒 hardcoded |
| `eps` | `CtcPolytopeHull` / `LPSolver` | double | **1e-9** (`LPSolver::default_tolerance`; the `CtcPolytopeHull.h` doc-comment's "1e-10" is stale) | 🔒 |
| `mode` | `LinearizerXTaylor` | RELAX / RESTRICT | RELAX | 🔒 (dReal passes RELAX) |
| `corners` | `LinearizerXTaylor` | INF / SUP / RANDOM / RANDOM_OPP | RANDOM_OPP | 🔒 unexplored (dReal passes RANDOM_OPP) |
| `slope` | `LinearizerXTaylor` | TAYLOR / HANSEN | HANSEN | 🔒 unexplored (dReal passes HANSEN) |
| linearizer choice | `Linearizer*` | XTaylor / Affine2 / both | XTaylor | 🟡 **`--polytope-linearizer xtaylor\|affine\|both`** (default xtaylor; `LinearizerAffine2` vendored fork-side @ `b5e7a212`, fAF2 audit `affine-rounding-audit.md`). **Measured 2026-07: prefer `affine`** — solve-set ⊃ xtaylor's, hybrid adds nothing, and BUG-014 (SoPlex presolve OOB write, latent SOUNDNESS risk) is xtaylor-row-triggered |

## 5. Ignored by design — search strategy (dReal branches inside DPLL(T))

| Knob | Class | Menu | dReal status |
|---|---|---|---|
| bisector | `Bsc` family | LargestFirst / RoundRobin / SmearMax/Sum/SumRelative / LSmear | 🟡 **`--smear <variant>`** (off) reimplements IBEX's 4 SmearFunction variants (`smearsum`/`smearsumrel`/`smearmax`/`smearmaxrel`) as dReal's own brancher (`brancher_smear.cc`); default is largest-first |
| activity brancher (ABS lift, dReal-own — no IBEX analog) | `brancher_abs.cc` | largest / abs / absdiam | 🟡 **`--branch abs\|absdiam`** + **`--branch-decay`** (default 0.999). **Measured dead 2026-07** — degeneracy hypothesis confirmed, both variants lose to largest-first on every family; kept for record |
| bisection `ratio` | `Bsc` | `Bsc::default_ratio()` | ⚪ |
| per-var precision | `Bsc` | scalar / `Vector` | ⚪ |
| cell buffer | `CellStack` / `CellHeap` / … | DFS / best-first | ⚪ own search stack |

## Coverage audit

- **The micro-optimization budget of the active path is spent** — the fork's
  rounding/exception/gradient patches (numbers in
  [`dreal-ibex-usage.md`](dreal-ibex-usage.md); catalog `../../ibex-fork/MIGRATION.md`)
  already pulled those levers. No §2 knob is a likely big win.
- **§3 is fully shipped opt-in** — `--acid`/`--3bcid` with `--s3b`/`--acid-ct-ratio`,
  the ∃∀ `--forall-pre-prune`, and (2026-07) `--newton`/`--newton-ceil`, `--obbt`,
  `--mohc`. **No IBEX contractor is absent anymore.** Measured verdicts are in the rows
  above; the `--acid`/`--3bcid` *tuning* sweep is the remaining unmeasured item.
- **§4 is also shipped** — `LP_LIB=soplex` makes `--polytope`/`--forall-polytope`
  live, the linearizer is selectable (`--polytope-linearizer`); only the X-Taylor
  `corners`/`slope` knobs remain unexposed (🔒, moot while BUG-014 stands).
- **§5 is partly adopted** — `--smear` brings IBEX's SmearFunction heuristic into
  dReal's own branching (`--branch abs|absdiam` added the ABS lift, measured dead);
  the rest stays dReal's by design.
- The prioritized read of all this is [`AUDIT.md`](AUDIT.md).
