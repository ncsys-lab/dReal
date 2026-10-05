# Build internals

The end-user build guide (prerequisites, native macOS/Linux, Docker) lives in `README.md`; the
agent quickstart in `CLAUDE.md` §Build. This file documents the build-system configuration for
developers modifying the build.

## `--version` output

`--version` prints three lines — feature flags, commit identity, and build platform:

```
dReal 5.0.0.1.<feature-flags>
Commit <hash> [<dirty>], Release Build.
Built for Darwin 25.5.0 arm64, on Jun 25 2026 12:51:30.
```

### How the values are wired in

- `<hash>` / `<dirty>` — captured at **every build** by `cmake/GenerateGitVersion.cmake`,
  which runs `git rev-parse --short HEAD` and `git status --porcelain --untracked-files=no`.
  This is a CMake `add_custom_target` (a build-time hook, **not** a git hook — it fires on
  `cmake --build`, not on `git commit`/`push`). The script writes `gcc_build/git_version.h`
  only when content changes, so `dreal_main.cc` is not recompiled unnecessarily.
- OS / arch — `CMAKE_SYSTEM_NAME`, `CMAKE_SYSTEM_VERSION`, `CMAKE_SYSTEM_PROCESSOR` injected
  as `target_compile_definitions` at CMake configure time.
- Timestamp — `__DATE__`/`__TIME__` compiler built-ins, stamped when `dreal_main.cc` is
  compiled (which happens whenever the hash/dirty status changes).
- Docker: `.git/HEAD`, `.git/refs/`, and a stub `objects/` dir are `COPY`'d into the image
  so `git rev-parse` works and the real hash appears. Dirty is always 0 in Docker (no index
  or object store to compare against).

## Ordered containers keyed by `Variable`, `Expression` or `Formula`

The symbolic types overload `operator<` to build a `Formula`, so `std::less` cannot order them.
Drake worked around this by specializing `std::less` for all three types. libc++ 22 (the Xcode 27
SDK) no longer calls those specializations: `std::set` and `std::map` look keys up through the
transparent `std::less<>`, which calls the symbolic `operator<`, and compilation fails with "no
viable conversion from Formula to bool". LLVM considers such specializations non-conforming
([namespace.std]/2.2; llvm/llvm-project#178847, closed), so libc++ will not change back.

Every ordered container keyed by one of these types therefore names its comparator. Each type's
header defines the comparator and two aliases. The comparator calls the type's existing `less` /
`Less` member, which the `std::less` specialization also called, so iteration order is unchanged.

| Key | Comparator | Set | Map |
|---|---|---|---|
| `Variable` | `VariableLess` | `VariableSet` | `VariableMap<V>` |
| `Expression` | `ExpressionLess` | `ExpressionSet` | `ExpressionMap<V>` |
| `Formula` | `FormulaLess` | `FormulaSet` | `FormulaMap<V>` |

A multimap passes the comparator itself (`std::multimap<Expression, Expression, ExpressionLess>`).
The `std::less` specializations are deleted. A bare `std::set<Formula>`, including one deduced by
`std::set s(first, last)`, now fails to compile on every standard library, not only on libc++ 22.
The `std::equal_to` and `std::hash` specializations stay; libc++ 22 still calls them, and the
unordered containers depend on them.

Before this change (2026-10), `gcc_build` and `gcc_build_debug` were pinned to the Command Line
Tools SDK (`-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`, libc++ 21)
as a stopgap. The pins are removed and both use the default Xcode SDK. CMake keeps a cached
sysroot, so a build dir configured with the pin keeps it until reconfigured with
`-UCMAKE_OSX_SYSROOT`.
