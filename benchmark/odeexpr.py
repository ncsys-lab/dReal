#!/usr/bin/env python3
"""Benchmark family registry: classification, selection weights, and name<->path
resolution for every benchmark family.

Two kinds of family:
  * Flat-directory families (saradc/github/tacas) — the directory is encoded in
    the name; paths are resolved by select.py's resolve_path against fixed dirs.
  * Manifest content-addressed families (odeexpr_v1, odeexpr_v2) — each logical
    benchmark (bench_id = the manifest key) has hashed revision files and a
    manifest.json naming the *current* revision. We key on the stable bench_id
    and resolve through the manifest so regeneration (which mints new hashed
    files, never overwrites) never silently repoints a name.

    The two manifests differ in schema — v1 (expressivity/v1) keys a revision's
    file under "file"; v2 (expressivity/v2) under "smt2", with different
    record shapes — but share one contract: a record with status=="active" whose
    revisions[] holds a state=="current" entry naming the file under the family's
    rev_file_key. ManifestFamily parameterizes over that key so a single loader
    serves both without a parallel copy.

A manifest-family benchmark name is "<family>_<bench_id>", e.g.
`odeexpr_v1_box_sweep.ising__phi_7` or
`odeexpr_v2_aim_tanh_n2__mlp2_n2_h2__both_descend__k0__GNone__forall__a67f651b`.

CLI: `python3 odeexpr.py --all [FAMILY]` prints TSV (name <TAB> abspath) for the
named manifest family (default: every manifest family).
"""
import json
import os
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class ManifestFamily:
    name: str          # family label, e.g. "odeexpr_v1"
    root: str          # benchmarks dir holding manifest.json + revision files
    rev_file_key: str  # revision-dict key naming the .smt2 relpath ("file" | "smt2")

    @property
    def prefix(self) -> str:
        return self.name + "_"

    @property
    def manifest_path(self) -> str:
        return os.path.join(self.root, "manifest.json")


MANIFEST_FAMILIES = [
    ManifestFamily("odeexpr_v1",
                   "/Users/kunalsheth/Documents/expressivity/v1/benchmarks",
                   "file"),
    ManifestFamily("odeexpr_v2",
                   "/Users/kunalsheth/Documents/expressivity/v2/benchmarks",
                   "smt2"),
]
MANIFEST_FAMILY_NAMES = {f.name for f in MANIFEST_FAMILIES}

# Flat-directory families, classified by name prefix.
_FLAT_PREFIXES = {"1mhz_": "saradc", "github_oct5_": "github", "tacas_c2e2_": "tacas"}

# Per-family selection/severity weights. odeexpr_v2 is the newest high-priority
# target (8), above odeexpr_v1 (6); the flat ODE families keep the user's
# equivalence "1 odeexpr = 3 github = 2 saradc = 3 tacas" (saradc=3, github/tacas=2).
FAMILY_WEIGHTS = {"odeexpr_v2": 8, "odeexpr_v1": 6, "saradc": 3, "github": 2, "tacas": 2}


def family_of(name: str) -> str | None:
    """Classify a (possibly .smt2-suffixed) benchmark name into its family."""
    for fam in MANIFEST_FAMILIES:
        if name.startswith(fam.prefix):
            return fam.name
    for prefix, fam in _FLAT_PREFIXES.items():
        if name.startswith(prefix):
            return fam
    return None


def weight_of(name: str) -> float:
    """Selection weight for a benchmark name (defaults to 1 for unknown families)."""
    fam = family_of(name)
    return FAMILY_WEIGHTS.get(fam, 1) if fam else 1


def _manifest_family(name: str) -> ManifestFamily | None:
    for fam in MANIFEST_FAMILIES:
        if name.startswith(fam.prefix):
            return fam
    return None


def _load_manifest(fam: ManifestFamily) -> dict:
    with open(fam.manifest_path) as f:
        return json.load(f)


def _current_file(entry: dict, rev_file_key: str) -> str | None:
    """Relative path of the entry's current revision, under the family's key."""
    for rev in entry.get("revisions", []):
        if rev.get("state") == "current":
            return rev.get(rev_file_key)
    return None


def load_manifest_names() -> list[str]:
    """All active manifest-family benchmark names whose current file exists on disk."""
    names = []
    for fam in MANIFEST_FAMILIES:
        manifest = _load_manifest(fam)
        for bench_id, entry in manifest.items():
            if entry.get("status") != "active":
                continue
            rel = _current_file(entry, fam.rev_file_key)
            if rel and os.path.exists(os.path.join(fam.root, rel)):
                names.append(fam.prefix + bench_id)
    return sorted(names)


def resolve_manifest(name: str) -> str | None:
    """Map `<family>_<bench_id>` to its current revision's absolute path, or None
    if `name` is not a manifest-family name / has no resolvable current file."""
    fam = _manifest_family(name)
    if fam is None:
        return None
    bench_id = name[len(fam.prefix):].removesuffix(".smt2")
    entry = _load_manifest(fam).get(bench_id)
    if entry is None:
        return None
    rel = _current_file(entry, fam.rev_file_key)
    if not rel:
        return None
    path = os.path.join(fam.root, rel)
    return path if os.path.exists(path) else None


def main():
    args = sys.argv[1:]
    if not args or args[0] != "--all":
        print("Usage: odeexpr.py --all [FAMILY]", file=sys.stderr)
        sys.exit(1)
    want = args[1] if len(args) >= 2 else None
    if want and want not in MANIFEST_FAMILY_NAMES:
        print(f"ERROR: unknown manifest family {want!r}; "
              f"choices: {sorted(MANIFEST_FAMILY_NAMES)}", file=sys.stderr)
        sys.exit(1)
    n = 0
    for name in load_manifest_names():
        if want and family_of(name) != want:
            continue
        path = resolve_manifest(name)
        if path:
            print(f"{name}\t{path}")
            n += 1
        else:
            print(f"WARN: could not resolve {name}", file=sys.stderr)
    scope = want if want else "all manifest families"
    print(f"Listed {n} benchmarks ({scope}).", file=sys.stderr)


if __name__ == "__main__":
    main()
