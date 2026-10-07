#!/usr/bin/env python3
"""Select benchmarks for a run, leaving out every benchmark in optsearch/blacklist.txt (a memory
kill, aggregate.py appends it) and the static OOM risks.

Default mode: 8 family-weighted random.
  python3 select_jobs.py [--n N] [--seed SEED]

Family-subset mode (a spot check aimed at some families, e.g. the ODE families —
the random weighting favours odeexpr, which has no ODEs):
  python3 select_jobs.py --family github,tacas,saradc --n 8   # 8 weighted from those
  python3 select_jobs.py --family github --n 6                 # 6 random from github
--all lists whole families, which is more than corun.sh's 12; a family-wide run is a Sherlock
experiment over the registered set.

Prints TSV (csv_name <TAB> filepath), one per line, to stdout.
"""
import argparse
import csv
import os
import random
import re
import sys

from odeexpr import family_of, load_manifest_names, resolve_manifest, weight_of

_LARGE_K_RE = re.compile(r'_k(\d+)_')
_BITWIDTH_RE = re.compile(r'_(\d+)b_')


def _is_oom_risk(name: str) -> bool:
    """Return True if the benchmark is known to exhaust memory.

    github/tacas: _k<N>_ with N >= 1024.
    saradc: _<N>b_ with N >= 9 (bitwidth encodes problem size independently of k).
    """
    for m in _LARGE_K_RE.finditer(name):
        if int(m.group(1)) >= 1024:
            return True
    for m in _BITWIDTH_RE.finditer(name):
        if int(m.group(1)) >= 9:
            return True
    return False


BENCHMARK_DIR = "/Users/kunalsheth/Documents/new_dreal/nraode_to_nra"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# SARADC files live in the AMS verification bundle; fall back to old split dirs.
SARADC_DIRS = [
    "/Users/kunalsheth/Documents/new_dreal/AMS-verification-bundle-of-sticks/saradc/rolled",
    os.path.join(BENCHMARK_DIR, "SARADC_tueoct14"),
    os.path.join(BENCHMARK_DIR, "REB_SAR_k1_dec9"),
]
GITHUB_DIR = os.path.join(BENCHMARK_DIR, "drealgithub_sunoct5", "rolled")
TACAS_DIR  = os.path.join(BENCHMARK_DIR, "VNAMSCwI_satoct11", "rolled")


def resolve_path(bench_name: str) -> str | None:
    if bench_name.startswith("github_oct5_"):
        filename = bench_name[len("github_oct5_"):]
        p = os.path.join(GITHUB_DIR, filename)
        return p if os.path.exists(p) else None
    if bench_name.startswith("tacas_c2e2_"):
        filename = bench_name[len("tacas_c2e2_"):]
        p = os.path.join(TACAS_DIR, filename)
        return p if os.path.exists(p) else None
    if bench_name.startswith("1mhz_"):
        for d in SARADC_DIRS:
            p = os.path.join(d, bench_name)
            if os.path.exists(p):
                return p
        return None
    # Manifest families (odeexpr_v1/v2) — resolve_manifest returns None for any
    # non-manifest name, so this is a safe fallthrough for the flat families above.
    return resolve_manifest(bench_name)


def load_benchmarks(baseline_csv: str) -> list[str]:
    names = []
    with open(baseline_csv) as f:
        reader = csv.reader(f)
        rows = list(reader)
    # Row 0: group headers, Row 1: sub-headers, Row 2: index label, Row 3+: data
    for row in rows[3:]:
        name = row[0].strip() if row else ""
        if name and not _is_oom_risk(name):
            names.append(name)
    return names


def weighted_sample_without_replacement(items, k, rng):
    """Pick k of `items` (each a (name, path) pair) weighted by family weight.

    Efraimidis-Spirakis A-Res: assign each item key = u**(1/w) with u~U(0,1),
    take the k largest keys. Heavier families (odeexpr) are proportionally more
    likely to be drawn per item.
    """
    if k >= len(items):
        return list(items)
    keyed = []
    for name, path in items:
        w = weight_of(name)
        u = rng.random()
        key = u ** (1.0 / w) if w > 0 else 0.0
        keyed.append((key, name, path))
    keyed.sort(key=lambda t: t[0], reverse=True)
    return [(name, path) for _key, name, path in keyed[:k]]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--n", type=int, default=8, help="random benchmarks to pick")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--family", default=None,
                        help="comma-separated family filter "
                             "(odeexpr_v1,odeexpr_v2,s2d,saradc,github,tacas); "
                             "restricts the corpus to those families before selection")
    parser.add_argument("--all", action="store_true",
                        help="emit EVERY benchmark of the (filtered) corpus, deterministically "
                             "sorted, no random sampling — more than corun.sh's 12 for any whole "
                             "family; cut it or run the family on Sherlock")
    args = parser.parse_args()

    baseline_csv = os.path.join(SCRIPT_DIR, "baseline.csv")
    blacklisted = set(open(os.path.join(SCRIPT_DIR, "optsearch", "blacklist.txt")).read().split())

    # Corpus = the frozen baseline CSV rows (saradc/github/tacas) plus the
    # manifest-derived families (content-addressed), minus the memory-killed ones.
    all_names = [n for n in load_benchmarks(baseline_csv) + load_manifest_names()
                 if n.removesuffix(".smt2") not in blacklisted]

    if args.family:
        want = {f.strip() for f in args.family.split(",") if f.strip()}
        all_names = [n for n in all_names if family_of(n) in want]
        if not all_names:
            print(f"ERROR: no benchmarks match --family {sorted(want)}", file=sys.stderr)
            return 1

    # --all: deterministic full enumeration of the (filtered) corpus. No
    # random — an A/B wants a fixed, reproducible job set.
    if args.all:
        rows = sorted((n, resolve_path(n)) for n in all_names)
        emitted = 0
        for name, path in rows:
            if not path:
                print(f"WARN: could not find file for {name}", file=sys.stderr)
                continue
            print(f"{name}\t{path}")
            emitted += 1
        print(f"Selected {emitted} benchmarks (--all"
              f"{', --family ' + args.family if args.family else ''}); "
              f"{len(rows) - emitted} not found on disk (skipped).", file=sys.stderr)
        return 0

    rng = random.Random(args.seed)
    resolved = [(n, resolve_path(n)) for n in all_names]
    resolvable = [(n, p) for n, p in resolved if p]
    for n, p in resolved:
        if not p:
            print(f"WARN: could not find file for {n}", file=sys.stderr)
    selected = weighted_sample_without_replacement(resolvable, min(args.n, len(resolvable)), rng)
    for name, path in selected:
        print(f"{name}\t{path}")   # csv_name <TAB> filepath; corun.sh labels results by csv_name
    print(f"Selected {len(selected)} benchmarks; {len(resolved) - len(resolvable)} in the corpus "
          f"not found on disk (skipped).", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main() or 0)
