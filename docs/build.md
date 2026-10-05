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

## Containers keyed by `Variable`, `Expression` or `Formula`

The symbolic types overload `operator<` and `operator==` to build a `Formula`, so `std::less` and
`std::equal_to` cannot compare them. Drake worked around this by specializing both for all three
types. libc++ 22 (the Xcode 27 SDK) no longer calls the `std::less` specializations: `std::set`
and `std::map` look keys up through the transparent `std::less<>`, which calls the symbolic
`operator<`, and compilation fails with "no viable conversion from Formula to bool". LLVM
considers such specializations non-conforming ([namespace.std]/2.2; llvm/llvm-project#178847,
closed), so libc++ will not change back.

The `std::equal_to` specializations are non-conforming in the same way, though libc++ 22 still
calls them. A future libc++ could bypass them too, and the failure might not be a compile error:
libc++'s hash table calls the equality comparator inside an `if`, where a `Formula` result would
convert through its explicit `operator bool` and call `Formula::Evaluate()` at run time. Both
specializations were therefore replaced at once.

Every container keyed by one of these types names its comparators. Each type's header defines
them and four aliases. The comparators call the type's existing `less`/`Less` and
`equal_to`/`EqualTo` members, which the `std` specializations also called, so iteration order is
unchanged. The `std::hash` specializations stay, since specializing `std::hash` is the standard's
intended customization point.

| Key | Comparators | Ordered | Unordered |
|---|---|---|---|
| `Variable` | `VariableLess`, `VariableEqualTo` | `VariableSet`, `VariableMap<V>` | `VariableUnorderedSet`, `VariableUnorderedMap<V>` |
| `Expression` | `ExpressionLess`, `ExpressionEqualTo` | `ExpressionSet`, `ExpressionMap<V>` | `ExpressionUnorderedSet`, `ExpressionUnorderedMap<V>` |
| `Formula` | `FormulaLess`, `FormulaEqualTo` | `FormulaSet`, `FormulaMap<V>` | `FormulaUnorderedSet`, `FormulaUnorderedMap<V>` |

Code generic over the key type uses `SymbolicSet<K>`, `SymbolicMap<K, V>`,
`SymbolicUnorderedSet<K>` and `SymbolicUnorderedMap<K, V>` from `dreal/symbolic/symbolic.h`,
which pick the comparators through the `SymbolicCompare<K>` trait. A multimap passes the
comparator itself (`std::multimap<Expression, Expression, ExpressionLess>`).

The `std::less` and `std::equal_to` specializations are deleted. A bare `std::set<Formula>` or
`std::unordered_map<Variable, V>`, including a `std::set s(first, last)` whose type is deduced, now
fails to compile on every standard library.

Before this change (2026-10), `gcc_build` and `gcc_build_debug` were pinned to the Command Line
Tools SDK (`-DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk`, libc++ 21)
as a stopgap. The pins are removed and both use the default Xcode SDK. CMake keeps a cached
sysroot, so a build dir configured with the pin keeps it until reconfigured with
`-UCMAKE_OSX_SYSROOT`.
