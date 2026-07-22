# Chapter: Install (CMake)

Source: [`install-cmake.rst.txt`](../../../ibex-docs/_sources/install-cmake.rst.txt)
· `install-cmake.html`. How to build IBEX with CMake, incl. the `INTERVAL_LIB` and
`LP_LIB` choices.

> **dReal status:** dReal does **not** install IBEX separately — it source-builds
> the fork via ExternalProject (`CMakeLists.txt`, `ExternalProject_Add(ibex_external)`,
> pinned via `IBEX_GIT_TAG`) with **two deliberate flags**:
> - `-DINTERVAL_LIB=gaol` — gaol arithmetic (the patched backend; arm64-native
>   since mainline `971f8eb0`).
> - `-DLP_LIB=soplex` — vendored SoPlex 4.0.2 (hermetic; ZLIB/GMP off).

The `LP_LIB=soplex` choice makes the [polytope hull](contractor.md) / X-Newton
linear-relaxation path **live and shipped** (not dormant): `--polytope`
(`src/dreal/contractor/contractor_ibex_polytope.cc`), `--acid`/`--3bcid`
(`contractor_ibex_acid.{cc,h}`), and `--forall-polytope`. The interval-lib choice
(gaol) is the subject of the fork's rounding/soundness patches (see
`../ibex-fork/MIGRATION.md`, the live catalog). Both flags are the two build-time
"knobs" in [`../KNOBS.md`](../KNOBS.md).
