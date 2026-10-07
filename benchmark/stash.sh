#!/usr/bin/env bash
# Keep the current HEAD build as a co-run control: benchmark/bin/dreal4-<sha>.
# Builds first (BUILD.sh, incremental). Refuses when the build inputs differ from HEAD, so the
# binary is what its name says. Prints the stashed path.
# Usage: stash.sh
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_DIR"
dirty=$(git status --porcelain -- src cmake CMakeLists.txt)
[[ -z "$dirty" ]] || { printf 'ERROR: build inputs differ from HEAD; commit them first:\n%s\n' "$dirty" >&2; exit 1; }

./BUILD.sh >&2
# A pin bump doesn't reach an existing build dir (IBEX_GIT_TAG is a cache variable), so check
# the checked-out dependencies against CMakeLists.txt, not just the sources.
for dep in ibex capd; do
    DEP=$(echo "$dep" | tr a-z A-Z)
    want=$(sed -nE "s/^set\(${DEP}_GIT_TAG +\"([0-9a-f]+)\".*/\1/p" CMakeLists.txt)
    have=$(git -C "gcc_build/${dep}_ep/src/${dep}_external" rev-parse HEAD)
    [[ -n "$want" && "$have" == "$want"* ]] || {
        echo "ERROR: gcc_build has $DEP $have, CMakeLists.txt pins ${want:-?}; reconfigure gcc_build" \
             "(cmake -D${DEP}_GIT_TAG=$want gcc_build) first" >&2; exit 1; }
done
mkdir -p benchmark/bin
dest="benchmark/bin/dreal4-$(git rev-parse --short HEAD)"
cp gcc_build/dreal4 "$dest"
echo "$PROJECT_DIR/$dest"
