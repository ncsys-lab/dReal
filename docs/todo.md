# To do

Known breakage outside the solver that is left open on purpose. Solver bugs go in
`docs/dreal-bugs.md`.

## `copy_lint.sh` cannot run: clang-tidy 23 rejects IBEX's bundled SoPlex headers (2026-10-08)

`./copy_lint.sh gcc_build` fails in every changed file (62) with
`ibex-install/include/ibex/3rd/soplex/datahashtable.h:363:9: error: array initializer must be an
initializer list [clang-diagnostic-error]` (Homebrew LLVM clang-tidy 23.1.2). SoPlex 4.0.2's
`DataHashTable` copy constructor initializes the array member `primes` from another array
(`primes(base.primes)`), which clang 23's front end rejects wherever the header is included.

ibex-fork's SoPlex hunk (`../ibex-fork/MIGRATION.md` entry 17, in the pinned `33b883f2`) fixed
only `operator=` at line 347, the line GCC 14 rejected. A build dir still on IBEX `6b1b2c10`
(`gcc_build_debug`) fails at both, lines 347 and 362, and also lacks the generated
`git_version.h` that `dreal_main.cc` includes.

Fix: extend the fork hunk to the copy constructor (copy `primes` in the body, as `operator=`
now does) and bump the IBEX pin. Until then the clang-tidy gate cannot run; `lint.py` and
`./rounding_debug_gate.sh` still do.

## Merge branch `partial-models-flag` after a spot check (paused 2026-10-08)

Commit `ba5a4d635` on `partial-models-flag` (parent `1bc29bc41`) replaces the compile-time
`DREAL_EXPERIMENTAL_SAT_MODEL_FULL_CONSTRAINTS` with a runtime `--partial-models` flag, default off
(full models, as before). Its tests pass (ctest, 1004) and `lint.py` is clean.

1. Run the co-run spot check (`/benchmark`) with that commit's build as the test arm and a stash
   of its parent as the control, default flags on both. Expected: zero flips, PAR2 ratio ≈ 1.
2. If it holds, merge `partial-models-flag`.
