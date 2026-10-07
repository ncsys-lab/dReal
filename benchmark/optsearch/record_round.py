#!/usr/bin/env python3
"""Record one optsearch sweep round into the cross-round leaderboard.

The autonomous odeexpr config search ran its rounds through benchmark/do_sweep.sh
(retired 2026-10-07; rounds that size now run on Sherlock); this script ingests a
round's per-config summary.csv files and:

  1. Computes per-config solved-count and PAR2 (CPU time if solved, else
     2*timeout) and the PAR2 ratio vs the `base` (current-default) config.
  2. Detects SAT<->UNSAT verdict flips vs base -- the load-bearing correctness
     signal (a flip is an integration bug or a delta-boundary completeness
     shift; NEVER an accepted win -- see project CLAUDE.md soundness framing).
  3. Harvests any SIGKILL/OOM (exit 137 / solver_result OOM -- the oom_killer
     daemon kills a process over 10 GB, or the largest past 87.5% of RAM) into
     benchmark/optsearch/blacklist.txt so the
     benchmark is excluded from future rounds and never restarted.
  4. Appends a row per config to benchmark/optsearch/leaderboard.csv and prints
     a ranked table for the /loop coordinator to read.

Usage:
  python3 record_round.py <sweep_out_dir> [--round TAG] [--base base]
                          [--timeout 600]

PAR2 penalty = 2*timeout. solver_result is produced by parse_results.py
(SAT/UNSAT/TIM/OOM/ERR). This script does not run the solver.
"""
import argparse
import csv
import os
import sys
from datetime import datetime, timezone

HERE = os.path.dirname(os.path.abspath(__file__))
LEADERBOARD = os.path.join(HERE, "leaderboard.csv")
BLACKLIST = os.path.join(HERE, "blacklist.txt")

LEADERBOARD_FIELDS = [
    "timestamp", "round", "config", "flags", "solved", "total",
    "par2", "par2_ratio_vs_base", "n_flips", "flips", "oom",
]


def load_summary(config_dir):
    """benchmark_name -> row dict, from a config's summary.csv."""
    path = os.path.join(config_dir, "summary.csv")
    if not os.path.exists(path):
        raise FileNotFoundError(f"missing summary.csv in {config_dir} "
                                f"(did do_sweep finish / parse_results run?)")
    with open(path, newline="") as f:
        return {r["benchmark_name"]: r for r in csv.DictReader(f)}


def load_flags(out_dir):
    """config -> flag string, from the sweep's _pool_jobs.tsv (NONE => empty)."""
    path = os.path.join(out_dir, "_pool_jobs.tsv")
    flags = {}
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                parts = line.rstrip("\n").split("\t")
                if len(parts) >= 2:
                    cfg, fl = parts[0], parts[1]
                    flags.setdefault(cfg, "" if fl == "NONE" else fl)
    return flags


def is_solved(result):
    return result in ("SAT", "UNSAT")


def par2(rows, timeout):
    penalty = 2.0 * timeout
    total = 0.0
    for r in rows.values():
        if is_solved(r["solver_result"]) and r["cpu_time_s"]:
            total += float(r["cpu_time_s"])
        else:
            total += penalty
    return total


def flips_vs_base(cfg_rows, base_rows):
    """Benchmarks where both base and cfg gave a verdict but they disagree."""
    out = []
    for name, br in base_rows.items():
        cr = cfg_rows.get(name)
        if cr is None:
            continue
        b, c = br["solver_result"], cr["solver_result"]
        if is_solved(b) and is_solved(c) and b != c:
            out.append(f"{name}({b}->{c})")
    return out


def oom_benchmarks(rows):
    return [n for n, r in rows.items()
            if r["solver_result"] == "OOM" or r.get("exit_code") == "137"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir", help="do_sweep results dir (its stdout)")
    ap.add_argument("--round", default="", help="round tag for the leaderboard")
    ap.add_argument("--base", default="base", help="reference config name")
    ap.add_argument("--timeout", type=float, default=600.0)
    args = ap.parse_args()

    configs = sorted(d for d in os.listdir(args.out_dir)
                     if os.path.isdir(os.path.join(args.out_dir, d))
                     and os.path.exists(os.path.join(args.out_dir, d, "summary.csv")))
    if not configs:
        sys.exit(f"no config summaries found under {args.out_dir}")

    flags = load_flags(args.out_dir)
    summaries = {c: load_summary(os.path.join(args.out_dir, c)) for c in configs}

    if args.base not in summaries:
        sys.exit(f"base config '{args.base}' not found among {configs}")
    base_rows = summaries[args.base]
    base_par2 = par2(base_rows, args.timeout)

    # Harvest OOM/SIGKILL across ALL configs into the blacklist (dedup).
    existing_bl = set()
    if os.path.exists(BLACKLIST):
        existing_bl = {l.strip() for l in open(BLACKLIST) if l.strip()}
    new_bl = set()
    for c in configs:
        new_bl.update(oom_benchmarks(summaries[c]))
    added_bl = sorted(new_bl - existing_bl)
    if added_bl:
        with open(BLACKLIST, "a") as f:
            for n in added_bl:
                f.write(n + "\n")

    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    write_header = not os.path.exists(LEADERBOARD)
    table = []
    with open(LEADERBOARD, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=LEADERBOARD_FIELDS)
        if write_header:
            w.writeheader()
        for c in configs:
            rows = summaries[c]
            solved = sum(1 for r in rows.values() if is_solved(r["solver_result"]))
            p2 = par2(rows, args.timeout)
            ratio = p2 / base_par2 if base_par2 else float("nan")
            fl = [] if c == args.base else flips_vs_base(rows, base_rows)
            oom = oom_benchmarks(rows)
            w.writerow({
                "timestamp": ts, "round": args.round, "config": c,
                "flags": flags.get(c, ""), "solved": solved, "total": len(rows),
                "par2": f"{p2:.2f}", "par2_ratio_vs_base": f"{ratio:.3f}",
                "n_flips": len(fl), "flips": ";".join(fl), "oom": ";".join(oom),
            })
            table.append((c, solved, len(rows), p2, ratio, fl, flags.get(c, "")))

    # Ranked report (best PAR2 first) for the /loop coordinator.
    table.sort(key=lambda t: t[3])
    print(f"\n=== Round '{args.round}' leaderboard (sorted by PAR2; base={args.base}) ===")
    print(f"{'config':<16}{'flags':<34}{'solved':>7}{'PAR2':>11}{'ratio':>8}{'flips':>7}")
    for c, solved, total, p2, ratio, fl, fstr in table:
        mark = "  <-- FLIPS!" if fl else ""
        print(f"{c:<16}{(fstr or '(default)'):<34}{solved:>4}/{total:<2}"
              f"{p2:>11.1f}{ratio:>8.3f}{len(fl):>7}{mark}")
    flagged = [(c, fl) for c, _, _, _, _, fl, _ in table if fl]
    if flagged:
        print("\n!!! VERDICT FLIPS vs base (investigate; never accept as a win):")
        for c, fl in flagged:
            print(f"    {c}: {', '.join(fl)}")
    if added_bl:
        print(f"\n!!! SIGKILL/OOM blacklisted (excluded from future rounds): {added_bl}")
    print()


if __name__ == "__main__":
    main()
