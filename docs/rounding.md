# FPU Rounding Mode, Typed Doubles, and Source-Hygiene Lint

Read this file before touching any interval/ODE/printing code. The failure mode is a **silent
false `unsat`**: the wrong ambient FPU rounding mode inverts gaol's directed rounding so an
inexactly-representable constant collapses to an empty interval — with no warning, no crash, and
no manifestation on exactly-representable constants like `0.5` or `1.0`.

Regression test for the silent failure: `test/dreal/api/test/gaol_directed_rounding_false_unsat_test.cc`.

---

## The two regimes

The FPU rounding mode is a thread-local resource. Two incompatible regimes coexist:

| | Interval regime | Nearest regime |
|---|---|---|
| Mode | `FE_UPWARD` | `FE_TONEAREST` |
| Scope (establish + mint token) | `UpwardRoundingScope` | `NearestRoundingScope` |
| Token (compile-time proof) | `UpwardRounding` | `NearestRounding` |
| Token-gated consumers | `safe_mid`/`safe_diam`, `sub_*`/`add_*`, `make_sound_interval`, `ibex_hc4_backward` (`util/rounded_interval.h`) | `format_double` (`util/rounded_format.h`), `dump_json` (`util/json_guarded.h`), `to_capd_string` (`contractor/odes/to_capd_string.h`), `run_capd_*` |
| Debug backstop | `DREAL_ASSERT_ROUNDING(FE_UPWARD)` | `DREAL_ASSERT_ROUNDING(FE_TONEAREST)` |

**gaol** (IBEX's interval backend) is sound only under `FE_UPWARD`. Under any other mode its
directed rounding inverts (`lo > hi`), collapsing a feasible interval to empty.

**CAPD** expects `FE_TONEAREST`. Its `DoubleRounding` sets directed modes per-op but **leaves
the FPU in a directed mode (`FE_UPWARD`) on return** rather than restoring nearest. Every CAPD
adapter brackets its work in an `ExpectClobber` `NearestRoundingScope`.

**Linking CAPD sets `FE_TONEAREST`** at process start (CAPD static init / `roundNearest()`).
There is no safe ambient mode to assume — every mode-sensitive operation must establish its
regime explicitly.

---

## The `RoundingModeGuard` mechanism (`src/dreal/util/rounding.h`)

The shared RAII primitive is **sealed** (private ctor; only `UpwardRoundingScope` and
`NearestRoundingScope` are friends). No call site outside `rounding.h` names a rounding-mode
constant.

**Check-before-set:** the ctor reads the live FPCR (`fegetround`) and issues the
pipeline-serializing `fesetround` only when the requested mode differs. On ARM64 a `fesetround`
(MSR FPCR) is ~5× a `fegetround` and ~11× when the value changes, so a nested scope
re-establishing an already-active mode costs zero writes.

**Dtor clobber tripwire:** the dtor reads the live FPCR at scope exit. If the mode the scope
established did not survive, some code changed the FPU mode out-of-band without a guard — a
soundness hazard — so it `abort()`s with a stderr diagnostic **in every build**, not just Debug.
`ExpectClobber` scopes opt out of this tripwire where a clobber is expected and healed by the
live-based restore.

---

## Phase-hoisting

`FE_UPWARD` is established **once per ICP phase**, not per contractor call — calling `fesetround`
on every `Prune` invocation in the ICP hot loop is expensive (pipeline-serializing). Entry points:

- `IcpParallel::CheckSat` — one `UpwardRoundingScope` around the root prune + seed proposal
- Each `IcpParallel` worker (including the jobs=1 main-thread worker) — FPU mode is thread-local, so every worker mints its own

The `UpwardRounding` token is threaded as a parameter through `Contractor::Prune(ContractorStatus*,
const UpwardRounding&)` and every override/combinator, AND through `safe_mid`/`safe_diam`,
`FormulaEvaluator`/`ExpressionEvaluator` `operator()`, `EvaluateBox`, the
`Brancher`/`TerminationCondition` `std::function`s, and `CounterexampleRefiner::Refine`. The
token is **the sole proof** a gaol op may run — there is no minter other than a scope, so the
invariant is compile-time-enforced.

The pure-gaol leaves (`ContractorIbexFwdbwd::Prune`, `ContractorIbexPolytope::Prune`) and
`ExpressionEvaluator::operator()` do not self-guard; they `DREAL_ASSERT_ROUNDING(FE_UPWARD)` in
Debug to verify the inherited phase mode.

CAPD code: `contractor_ode_lohner::Prune` establishes a nested `NearestRoundingScope`, passes
its token to `run_capd_*`, and re-establishes a further nested `UpwardRoundingScope` around the
ibex invariant sub-contractors it calls.

Standalone leaves self-establish: `Box::MaxDiam` mints an `UpwardRoundingScope`; NLopt callbacks
mint a `NearestRoundingScope` (no token — they call no token-gated consumer).

`math.cc`'s `is_integer`/`convert_int64_to_double` are mode-independent (`modf` is exact;
int→double within ±2^53 is exact) and establish **no** scope — `is_integer` is called per `pow`
in the hot `ExpressionEvaluator::VisitPow` loop (which runs under `FE_UPWARD`), so a scope there
would force a needless flip on every call.

---

## Sanctioned clobberers (`ExpectClobber` tag)

Two callers leave the FPU in a directed mode on return. Both are handled with `ExpectClobber`,
which suppresses the tripwire and relies on the live-based restore to heal the mode:

1. **CAPD adapters** (`run_capd_*` / `make_capd_ode_cache` in `contractor_odes_capd.cc`): CAPD's
   `DoubleRounding` leaves `FE_UPWARD` on return. Each adapter opens an `ExpectClobber`
   `NearestRoundingScope` to contain this.

2. **ibex's interval `operator<<`** at two model-printing sites: `Box::operator<<`
   (`util/box.cc`) and `PrintModel` (`smt2/driver.cc`). Both bracket the call in an
   `ExpectClobber` `NearestRoundingScope`.

Regression tests: `BoxTest.IntervalPrintDoesNotClobberRounding`,
`ContractorCapdFullTest.GenerateTrace*`. The guard mechanism (tripwire + `ExpectClobber` heal,
both regimes) is unit-tested in `util/test/rounding_test.cc`, including `EXPECT_DEATH` cases.

---

## Debug-only assertions

`DREAL_ASSERT_ROUNDING(mode)` compiles to nothing under `NDEBUG` — free in release, safe to
sprinkle at gaol/CAPD boundaries. Current guards: `ContractorIbexFwdbwd::Prune`,
`ContractorIbexPolytope::Prune`, `Box::MaxDiam`, `contractor_ode_lohner::Prune`.

`RoundingModeGuard` maintains a debug-only `thread_local` shadow
(`rounding_detail::g_rounding_shadow`). `DREAL_ASSERT_ROUNDING_CONSISTENT()` (at the gaol→CAPD
boundary) asserts the live FPCR matches the shadow, catching any `fesetround`/CAPD clobber that
bypasses a guard — drift a plain guard cannot detect.

Because these checks only fire in Debug, `./rounding_debug_gate.sh` builds `cmake-build-debug`
and runs the suite before merges.

**`.diam()`/`.mid()` are NOT mode-safe getters** — they run gaol directed-rounding FP, sound
only under `FE_UPWARD`. Only `.lb()`/`.ub()` are pure stored-value reads. Use
`safe_diam()`/`safe_mid()` (token-gated) at solve time. Exceptions allow-listed: the SMT2 parser
at parse time, and ibex's `operator<<` (an `ExpectClobber` site). Nearest-regime analog test:
`test/dreal/util/test/rounded_format_test.cc`.

---

## Typed directed-rounding doubles (`util/rounded_interval.h`)

The scalar analog of the `UpwardRounding` token. Hand-written `double` arithmetic feeding an
interval endpoint is the trap — `ibex::Interval(mid - half, mid + half)` is mis-rounded under
every single mode (the lower endpoint is pulled inward → too-narrow box).

`Exact`/`RoundedDown`/`RoundedUp` encode the rounding direction in the type.
`make_sound_interval(RoundedDown lo, RoundedUp hi)` only accepts an outward-rounded pair, so the
compiler checks it. `sub_down`/`add_up`/etc. compute the round-down direction via
`roundDown(x) = -roundUp(-x)` under `FE_UPWARD` (no per-op `fesetround`) and require the
`UpwardRounding` token.

`Tighten` (`context_impl.cc`) uses this for its delta-box construction.
`test/dreal/util/test/rounded_double_test.cc` verifies the directed rounding matches gaol's own
enclosure.

---

## Source-hygiene lint

Two gating tools:

- **`python3 lint.py`** — dependency-free regex check (no LLVM/libTooling build;
  `CMAKE_CXX_CLANG_TIDY` stays unset). Run statically or via `./rounding_debug_gate.sh`
  (lint + Debug ctest).
- **`./copy_lint.sh`** — incremental clang-tidy gate over `src/dreal/` for `.cc` changed vs.
  `main` merge-base (`--all` for full sweep). Check set: all `performance-*` minus `enum-size`,
  plus an allow-listed UB/memory-safety subset of `bugprone-*` (`use-after-move`,
  `dangling-handle`, `undefined-memory-manipulation`, ptr/array mismatches, `sizeof` misuse).
  Needs brew llvm's clang-tidy + `--extra-arg=-isysroot $(xcrun --show-sdk-path)` against
  `cmake-build-debug/compile_commands.json`. Findings are fixed (const&/move) or suppressed with
  `// NOLINT(<check>) <reason>` (the type-aware twin of `// lint: allow`).

### Forbidden patterns in `src/dreal/` (`lint.py` routing rules)

| Forbidden | Use instead |
|---|---|
| Raw `ibex::Function::backward` | `ibex_hc4_backward` (`util/rounded_interval.h`) |
| Raw `.mid()`/`.diam()` | `safe_mid`/`safe_diam` (token-gated) |
| `ibex::Interval(a+b, ...)` or `ibex::Interval(..., a-b)` | `make_sound_interval(RoundedDown, RoundedUp)` |
| Raw `json.dump(` | `dump_json` (`util/json_guarded.h`) |
| `std::to_string(double)` feeding a parser | `to_capd_string` (CAPD feed) or `format_double` (display) |
| `arr[i++]`/`arr[++i]` subscript | Use one shared index (BUG-005 scrambled-model class — commit `5774191f2`) |

Exceptions carry `// lint: allow <reason>` inline. The parser/scanner sources and the
wrapper-definition files (`rounded_interval.h`, `rounded_format.h`, `json_guarded.h`) are skipped.

Scalar `double`→decimal display routing (`os << v`) is not regex-detectable — enforced at compile
time by `format_double`/`to_capd_string` requiring the `NearestRounding` token.
