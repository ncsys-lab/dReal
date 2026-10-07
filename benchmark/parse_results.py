#!/usr/bin/env python3
"""Parse one corun.sh arm directory into summary.csv.

Usage: python3 parse_results.py <arm_dir>
Produces <arm_dir>/summary.csv with columns:
  benchmark_name, solver_result, cpu_time_s, wall_time_s, max_rss_kb, p_share, exit_code

Each run left <bench>.{exit,stdout,rusage} (measure.py). `cpu_time_s` (user+sys) is the
timing metric, and only as a ratio against another arm of the same co-run. A run ends TIM when
the CPU-seconds cap fires (SIGXCPU, exit 152) and OOM when the oom_killer daemon SIGKILLs it
(137). Every run must have run on the E-cores: a run with P-core time means the background-band
pin stopped holding, and the whole directory is refused rather than compared.
"""
import csv
import json
import os
import sys

# A pinned run still shows a few ms of P-core time (measured 2026-10-07: share <= 0.022 on 70 ms
# runs, 0.00 on 4 s runs); a run the pin missed shows ~0.8 under load (0.73-0.82 measured).
P_SHARE_MAX = 0.10


def parse_solver_result(stdout_text: str, exit_code: int) -> str:
    if exit_code == 137:
        return "OOM"
    if exit_code == 152:
        return "TIM"
    if "delta-sat" in stdout_text:
        return "SAT"
    if "unsat" in stdout_text.lower():
        return "UNSAT"
    return "ERR"


def parse_results_dir(results_dir: str) -> list[dict]:
    rows, off_e_cores = [], []
    for fname in sorted(os.listdir(results_dir)):
        if not fname.endswith(".exit"):
            continue
        name = fname[:-5]
        exit_code = int(open(os.path.join(results_dir, fname)).read().strip())
        stdout_text = open(os.path.join(results_dir, name + ".stdout")).read()
        rusage_path = os.path.join(results_dir, name + ".rusage")
        ru = json.load(open(rusage_path))
        if ru["p_share"] > P_SHARE_MAX:
            off_e_cores.append(f"{rusage_path} (p_share {ru['p_share']:.2f})")
        rows.append({
            "benchmark_name": name,
            "solver_result": parse_solver_result(stdout_text, exit_code),
            "cpu_time_s": f"{ru['cpu_s']:.2f}",
            "wall_time_s": f"{ru['wall_s']:.2f}",
            "max_rss_kb": str(ru["max_footprint_kb"]),
            "p_share": f"{ru['p_share']:.3f}",
            "exit_code": str(exit_code),
        })
    if off_e_cores:
        raise RuntimeError("runs with P-core time — the E-core pin did not hold, so these arms "
                           "are not comparable:\n  " + "\n  ".join(off_e_cores))
    return rows


def main():
    if len(sys.argv) < 2:
        print("Usage: parse_results.py <arm_dir>", file=sys.stderr)
        sys.exit(1)

    results_dir = sys.argv[1]
    rows = parse_results_dir(results_dir)

    out_path = os.path.join(results_dir, "summary.csv")
    with open(out_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["benchmark_name", "solver_result", "cpu_time_s",
                                               "wall_time_s", "max_rss_kb", "p_share", "exit_code"])
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} rows to {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
