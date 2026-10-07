#!/usr/bin/env python3
"""Select benchmarks for a run.

Default mode: 8 family-weighted random + all current anomalies.
  python3 select.py [--n N] [--seed SEED]

Family-subset mode (for A/B over a targeted family, e.g. the ODE families —
the random weighting favours odeexpr, which has no ODEs):
  python3 select.py --family github,tacas,saradc --all   # every job in those
  python3 select.py --family github --n 6                 # 6 random from github

Prints TSV (csv_name <TAB> filepath), one per line, to stdout.
"""
import argparse
import csv
import json
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
    parser.add_argument("--n", type=int, default=8, help="random benchmarks to add (not counting anomalies)")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--family", default=None,
                        help="comma-separated family filter "
                             "(odeexpr_v1,odeexpr_v2,s2d,saradc,github,tacas); "
                             "restricts the corpus to those families before selection")
    parser.add_argument("--all", action="store_true",
                        help="emit EVERY benchmark of the (filtered) corpus, deterministically "
                             "sorted — no random sampling, no anomaly injection. Intended for an "
                             "A/B over a fixed family set (e.g. --family github,tacas,saradc --all)")
    args = parser.parse_args()

    baseline_csv = os.path.join(SCRIPT_DIR, "baseline.csv")
    state_path = os.path.join(SCRIPT_DIR, "state.json")

    # Corpus = the frozen baseline CSV rows (saradc/github/tacas) plus the
    # manifest-derived odeexpr_v1/odeexpr_v2 families (content-addressed).
    all_names = load_benchmarks(baseline_csv) + load_manifest_names()

    if args.family:
        want = {f.strip() for f in args.family.split(",") if f.strip()}
        all_names = [n for n in all_names if family_of(n) in want]
        if not all_names:
            print(f"ERROR: no benchmarks match --family {sorted(want)}", file=sys.stderr)
            return 1

    # --all: deterministic full enumeration of the (filtered) corpus. No
    # anomalies, no random — an A/B wants a fixed, reproducible job set.
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

    with open(state_path) as f:
        state = json.load(f)
    anomalies = set(state.get("anomalies", []))

    # state.json stores names without .smt2; baseline.csv names include .smt2 —
    # normalize for comparison but preserve the original name for output/path resolution.
    anomaly_names = [n for n in all_names if n.removesuffix(".smt2") in anomalies]
    pool = [n for n in all_names if n.removesuffix(".smt2") not in anomalies]

    rng = random.Random(args.seed)

    # Resolve paths eagerly so we can filter out missing files without under-delivering
    resolved_pool = [(n, resolve_path(n)) for n in pool]
    resolvable = [(n, p) for n, p in resolved_pool if p]
    unresolvable = [n for n, p in resolved_pool if not p]
    for n in unresolvable:
        print(f"WARN: could not find file for {n}", file=sys.stderr)

    sample_size = min(args.n, len(resolvable))
    selected_pool = weighted_sample_without_replacement(resolvable, sample_size, rng)

    # Always include anomalies (resolved)
    anomaly_paths = [(n, resolve_path(n)) for n in anomaly_names]
    for n, p in anomaly_paths:
        if not p:
            print(f"WARN: anomaly file missing on disk: {n}", file=sys.stderr)

    all_selected = selected_pool + [(n, p) for n, p in anomaly_paths if p]

    for name, path in all_selected:
        # Output TSV: csv_name <TAB> filepath
        # run_batch.sh uses csv_name as the output label so aggregate.py can match baseline.
        print(f"{name}\t{path}")

    print(
        f"Selected {len(all_selected)} benchmarks "
        f"({len([x for x in anomaly_paths if x[1]])} anomalies, {sample_size} random); "
        f"{len(unresolvable)} in pool not found on disk (skipped).",
        file=sys.stderr,
    )


if __name__ == "__main__":
    sys.exit(main() or 0)
