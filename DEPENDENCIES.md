# Dependency Notes

This document describes the external libraries dReal4 depends on, including the
rationale for choosing specific versions and the changes made from upstream.

> **Status:** Codac is no longer used. IBEX is source-built from the dReal
> team's fork (`ncsys-lab/ibex-lib@dreal-perf-patches`) and CAPD is the sole
> ODE backend. See `docs/decisions.md` "ODE backend" for the migration
> rationale and `../ibex-fork/MIGRATION.md` for the IBEX fork's patch catalog.

---

## IBEX (`ncsys-lab/ibex-lib`)

**Version**: `dreal-perf-patches` on top of mainline `ibex-team/ibex-lib`; `CMakeLists.txt` pins the exact fork sha.
**Build**: `ExternalProject_Add` source-build from `https://github.com/ncsys-lab/ibex-lib.git` (cache var `IBEX_GIT_REPOSITORY`, overridable to a `file://` path for local-dev iteration against `../ibex-fork`), installed into `gcc_build/ibex-install/`.
**Role**: Interval arithmetic + constraint propagation. Provides `IntervalVector`, `Function`, `HC4Revise` (the forward-backward contractor), polytope hull (`CtcPolytopeHull`), and the symbolic expression tree.

### Why a fork at all

The fork hosts a series of surgical, intended-as-upstream-PR patches that aren't yet in
mainline. The authoritative, per-patch catalog (with file/line counts, soundness gates, and
measured effects) lives in **`../ibex-fork/MIGRATION.md`** — it is the single source of truth for
the patch set and its count; do not restate the list here (it drifts). The categories:

- **Performance levers (the reason the fork exists):** lazy-init `Gradient` (the dominant
  non-ODE cost — ~65% of `Function::init` wall time built an object `Function::backward` never
  reads; see `docs/decisions.md` "ODE backend"); inline aarch64 FPCR rounding write + batched
  nearest-rounding region in gaol transcendentals (bit-identical, ~8% on transcendental-dense
  odeexpr); and replacing `HC4Revise`'s `EmptyBoxException` control flow with a return-status
  (`__cxa_throw` off the contraction hot path).
- **Correctness / soundness:** `Function::backward` per-variable callback (lemma quality) plus
  its audit fixes for non-scalar args, reference aliasing, and partial-narrowing precision; gaol
  `Interval::log`/`pow` soundness gaps; and `underflow_saturate` for subnormal-band HC4 backward
  targets (dreal/dreal4#321 — see `docs/decisions.md` "Denormal / underflow soundness").
- **Platform / build:** Bison-parser `apply(...)` namespace qualification (clang-18/GCC-13 ADL);
  mathlib aarch64/arm64-Linux support (Apple-Silicon Docker build).

Once a patch merges upstream, drop it; when all land, swap `GIT_REPOSITORY` back to
`ibex-team/ibex-lib` and delete the fork.

### Source-build invocation (from `CMakeLists.txt`)

`ExternalProject_Add(ibex_external)` configures with `-DINTERVAL_LIB=gaol -DLP_LIB=soplex -DBUILD_TESTING=0 -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON` (`CMakeLists.txt:204`; `LP_LIB=soplex` since commit `fa3b74bd7` — vendored SoPlex 4.0.2, `libsoplex.a` linked, which is what makes the `--polytope`/`--forall-polytope`/X-Newton LP path functional; it was `none` before that commit). arm64 native macOS is supported by mainline `971f8eb0` (March 2025) — no Rosetta needed. Linux source-build under clang-18 / GCC 13 needs the parser-fix patch from the fork, which is why the source pin is to the fork rather than mainline.

The `IBEX_GIT_REPOSITORY` CMake cache variable defaults to `https://github.com/ncsys-lab/ibex-lib.git`. Override with `-DIBEX_GIT_REPOSITORY=file:///path/to/ibex-fork` for local-dev iteration against an unpushed working tree, or `-DIBEX_GIT_TAG=<sha>` for testing alternate revisions.

---

## CAPD (`CAPDGroup/CAPD`)

**Version**: master SHA `03dc5628` (2026-09-22; in-development v6.1.0). The pin must include
`2a2263c7` (2026-09-14, "Bug fixed in Curve::timeDerivative"): the per-slice tube's mean-value
term calls `Curve::timeDerivative`, which before that commit scaled the initial-condition spread
by f(X)−f(c) instead of X−c and produced false `unsat` on interval initial conditions (BUG-018,
`docs/dreal-bugs.md`). An existing build dir does not fetch on a bump (`UPDATE_DISCONNECTED`):
run `git -C <build>/capd_ep/src/capd_external fetch` before rebuilding.
**Build**: `ExternalProject_Add`, builds from source into `gcc_build/capd-install/`.
**Build flags**: `-DCAPD_INTERVAL_TYPE=NATIVE -DCAPD_ENABLE_MULTIPRECISION=OFF -DCAPD_BUILD_ALL=OFF -DCAPD_BUILD_TESTS=OFF`
**Role**: Sole ODE backend (after Codac removal). Taylor integration (default order 12, `--ode-taylor-order`) via `capd::IOdeSolver` + `capd::ITimeMap`. Backward integration uses the negated `-f(x)` map stored alongside the forward map in the per-flow cache.

### Why CAPD master (not v4.x or v6.0)

CAPD 4.x (the old `ncsys-lab/capdDynSys-4.0` fork) was x86-only due to an unconditional FILIB dependency. CAPD master exposes `CAPD_INTERVAL_TYPE=NATIVE`, which causes `capdExt/CMakeLists.txt` to skip `add_subdirectory(filibsrc)` entirely and use CAPD's own `DoubleRounding` (`msr fpcr` on ARM64, SSE on x86) as the interval backend. No `v6.1.0` release tag exists yet, so we pin a master SHA at a known-good post-switch point. Bump only after re-verifying the `CAPD_INTERVAL_TYPE` plumbing in the new commit.

### Interval backend note

Consumers of `capd_imported` must define `__USE_NATIVE__` to select the matching template instantiations. The `INTERFACE_COMPILE_DEFINITIONS "__USE_NATIVE__"` on the `capd_imported` IMPORTED target propagates this automatically.

### Why CAPD-only (no more Codac fallback)

Codac's `CtcLohner` was order-2 Taylor and cheap on short horizons but widened out of usefulness on longer ones. The previous "gated hybrid" (Codac for short horizons, CAPD for long) added complexity to `contractor_odes.cc` and a `--capd-t-gate`/`--capd-ndim-gate` CLI surface. After removing Codac, CAPD's order-20 enclosures are tight enough that the gate is no longer beneficial; the trivial-flow short-circuit covers the truly degenerate cases.

A CAPD-based trace generator (`run_capd_trace`) preserves the `--visualize` flag's output: step-by-step CAPD `IOdeSolver` invocations produce per-slice enclosures matching the JSON shape of the now-removed `run_lohner_trace`.

---

## Other Dependencies

Unchanged from before the Codac migration:

| Dependency | Source | Role |
|---|---|---|
| fmt | FetchContent | String formatting |
| spdlog | FetchContent | Logging |
| nlopt | FetchContent | Nonlinear optimization (for branch/split heuristics) |
| GTest | FetchContent | Unit testing |
| picosat | Vendored (`src/third_party/`) | SAT solver (legacy, mostly inactive) |
| libcds | Vendored (`src/third_party/`) | Lock-free concurrent data structures |
| nlohmann/json | Vendored (`src/third_party/`) | JSON output for visualization |
| dynamic_bitset | Vendored (`src/third_party/`) | Bitset for variable index sets |
| Drake symbolic | Vendored (`src/third_party/`) | Expression/formula symbolic layer |
| ezoptionparser | Vendored (`src/third_party/`) | CLI option parsing |

Eigen3 has been dropped from the dependency list — it was required only by Codac.

---

## Build invocation

### macOS (arm64 native, no Rosetta)

```bash
cd dreal4-cmake
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
ctest --output-on-failure
```

Brew packages (auto-detected by `find_brew_package` in `CMakeLists.txt`): `bison`, `flex`, `gmp`, `cadical`.

Optional dev-lint tool: `./copy_lint.sh` (the incremental clang-tidy copy gate) needs `brew install llvm` for its `clang-tidy`. It is not a build dependency — `CMAKE_CXX_CLANG_TIDY` stays unset and the build never invokes it.

### Linux (Ubuntu 24.04 + clang-18) via Docker

The `Dockerfile.dreal_ubuntu` is the hermetic Linux test harness. Because the IBEX source-build needs `../ibex-fork` available inside the container, the Docker build context must be the parent of `dreal4-cmake/`:

```bash
cd <parent containing dreal4-cmake and ibex-fork>
docker build -f dreal4-cmake/Dockerfile.dreal_ubuntu -t dreal-linux-verify .
```

The container builds CaDiCaL 3.0.0, GMP 6.3.0, Bison 3.8.2, and Flex 2.6.4 from source (matching the project's pinned versions), then runs `FULL_BUILD.sh` which configures + builds dreal4. End-to-end success is the Linux verification gate.

---

## Migration History

- `docs/decisions.md` "ODE backend" — the Codac→CAPD-only migration rationale, the perf-regression analysis that motivated returning to a fork, and the resolution.
- `../ibex-fork/MIGRATION.md` — divergence catalog of the IBEX fork.
