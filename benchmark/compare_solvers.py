#!/usr/bin/env python3
"""Compare the arms of one corun.sh run (corun.sh writes OUT/compare.txt with this).

Joins per-arm summary CSVs (benchmark_name, solver_result, cpu_time_s, ...)
by benchmark and reports: solve counts, per-benchmark verdict/timing, SAT↔UNSAT
disagreements, and CPU-time speedups on commonly-solved benchmarks. The inputs must be arms
of ONE co-run: times from different runs are not comparable (docs/benchmarking.md), so the
stored baseline_odeexpr_*.csv files are records, not inputs.

Usage:
  compare_solvers.py --cap CPU_S control=OUT/control/summary.csv test=OUT/test/summary.csv
A benchmark any arm lost to a memory kill (OOM) is excluded, not scored.
"""
import csv
import statistics
import sys

PAR2_PENALTY = None  # 2 x the run's CPU cap, set from --cap in main()
SOLVED = ("SAT", "UNSAT")


def load(path: str) -> dict[str, dict]:
    out = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            name = row["benchmark_name"].strip().removesuffix(".smt2")
            t = row.get("cpu_time_s", "") or row.get("wall_time_s", "")
            try:
                t = float(t)
            except (ValueError, TypeError):
                t = None
            out[name] = {"result": row["solver_result"].strip(), "cpu": t}
    return out


def par2(entry: dict) -> float:
    return entry["cpu"] if entry["result"] in SOLVED and entry["cpu"] is not None else PAR2_PENALTY


def main():
    global PAR2_PENALTY
    if sys.argv[1] != "--cap":
        raise SystemExit("usage: compare_solvers.py --cap CPU_S LABEL=summary.csv ...")
    cap = int(sys.argv[2])
    PAR2_PENALTY = 2.0 * cap
    solvers: dict[str, dict] = {}
    order: list[str] = []
    for arg in sys.argv[3:]:
        label, path = arg.split("=", 1)
        solvers[label] = load(path)
        order.append(label)

    names = sorted(set().union(*[set(s) for s in solvers.values()]))
    killed = [n for n in names if any(solvers[lab].get(n, {}).get("result") == "OOM" for lab in order)]
    names = [n for n in names if n not in killed]
    ref = order[0]  # first arm is the reference (the control)

    # PAR2 is scored ONLY over benchmarks solved by at least one solver. A
    # benchmark no solver cracks contributes the same 2 x cap penalty to every
    # solver — pure constant offset that dilutes real differences and carries no
    # comparative information.
    def solved_by_any(n):
        return any(solvers[lab].get(n, {}).get("result") in SOLVED for lab in order)
    scored = [n for n in names if solved_by_any(n)]
    never = [n for n in names if not solved_by_any(n)]

    print(f"{'='*78}\nCROSS-SOLVER COMPARISON — {len(names)} benchmarks, reference = {ref}\n{'='*78}\n")
    if killed:
        print(f"excluded (memory kill): {', '.join(killed)}\n")

    # Solve counts (over the full set)
    print(f"Solve counts (within the {cap} CPU-s cap, full set):")
    for lab in order:
        s = solvers[lab]
        sat = sum(1 for n in names if s.get(n, {}).get("result") == "SAT")
        uns = sum(1 for n in names if s.get(n, {}).get("result") == "UNSAT")
        solved = sat + uns
        print(f"  {lab:8s}  solved {solved:2d}/{len(names)}  (SAT {sat}, UNSAT {uns})  unsolved {len(names)-solved:2d}")
    print()

    # PAR2 table over the scored set (solved by >=1 solver)
    print(f"PAR2 score — scored over {len(scored)} benchmarks solved by >=1 solver "
          f"(excluded {len(never)} solved by none); penalty {PAR2_PENALTY:.0f}s:")
    print(f"  {'solver':8s} {'solved':>10s} {'PAR2 sum':>11s} {'PAR2 mean':>11s} {'vs '+ref:>10s}")
    ref_mean = None
    for lab in order:
        s = solvers[lab]
        nsolved = sum(1 for n in scored if s.get(n, {}).get("result") in SOLVED)
        p2sum = sum(par2(s[n]) if n in s else PAR2_PENALTY for n in scored)
        p2mean = p2sum / len(scored) if scored else 0.0
        if ref_mean is None:
            ref_mean = p2mean
        rel = f"{p2mean/ref_mean:.2f}x" if ref_mean else "—"
        print(f"  {lab:8s} {nsolved:>7d}/{len(scored):<2d} {p2sum:>10.1f}s {p2mean:>10.1f}s {rel:>10s}")
    if never:
        print(f"\n  excluded (solved by none): {', '.join(n.replace('odeexpr_v1_','').replace('odeexpr_v2_','') for n in never)}")
    print()

    # Verdict disagreements (SAT vs UNSAT between any two solvers — notable)
    disagree = []
    for n in names:
        verdicts = {lab: solvers[lab].get(n, {}).get("result") for lab in order}
        solved_v = {lab: v for lab, v in verdicts.items() if v in SOLVED}
        if len(set(solved_v.values())) > 1:
            disagree.append((n, verdicts))
    if disagree:
        print(f"!! SAT/UNSAT DISAGREEMENTS ({len(disagree)}) — both solved but differ (delta-completeness or bug):")
        for n, v in disagree:
            print(f"   {n}")
            print(f"      " + "  ".join(f"{lab}={v[lab]}" for lab in order))
        print()
    else:
        print("No SAT/UNSAT disagreements among solved benchmarks.\n")

    # Solve-set deltas vs reference
    for lab in order[1:]:
        only_ref = [n for n in names
                    if solvers[ref].get(n, {}).get("result") in SOLVED
                    and solvers[lab].get(n, {}).get("result") not in SOLVED]
        only_lab = [n for n in names
                    if solvers[lab].get(n, {}).get("result") in SOLVED
                    and solvers[ref].get(n, {}).get("result") not in SOLVED]
        print(f"Solve-set {ref} vs {lab}:")
        print(f"  solved by {ref} but not {lab} ({len(only_ref)}): " + (", ".join(only_ref) or "—"))
        print(f"  solved by {lab} but not {ref} ({len(only_lab)}): " + (", ".join(only_lab) or "—"))
        print()

    # Speedup on commonly-solved (CPU time)
    for lab in order[1:]:
        common = [n for n in names
                  if solvers[ref].get(n, {}).get("result") in SOLVED
                  and solvers[lab].get(n, {}).get("result") in SOLVED
                  and solvers[ref][n]["cpu"] and solvers[lab][n]["cpu"]]
        if common:
            ratios = [solvers[lab][n]["cpu"] / solvers[ref][n]["cpu"] for n in common]
            ref_tot = sum(solvers[ref][n]["cpu"] for n in common)
            lab_tot = sum(solvers[lab][n]["cpu"] for n in common)
            print(f"CPU time on {len(common)} commonly-solved ({lab} / {ref}):")
            print(f"  total: {ref}={ref_tot:.2f}s  {lab}={lab_tot:.2f}s  "
                  f"aggregate ratio {lab_tot/ref_tot:.2f}x")
            print(f"  per-benchmark ratio: median {statistics.median(ratios):.2f}x  "
                  f"min {min(ratios):.2f}x  max {max(ratios):.2f}x  "
                  f"({sum(1 for r in ratios if r>1)} slower / {sum(1 for r in ratios if r<1)} faster than {ref})")
            print()

    # Per-benchmark table
    print(f"{'-'*78}\nPER-BENCHMARK (result / CPU s):\n{'-'*78}")
    hdr = f"{'benchmark':44s}" + "".join(f"{lab:>16s}" for lab in order)
    print(hdr)
    for n in names:
        cells = ""
        for lab in order:
            e = solvers[lab].get(n)
            if e is None:
                cells += f"{'—':>16s}"
            else:
                t = f"{e['cpu']:.2f}" if e["cpu"] is not None else "—"
                cells += f"{e['result']+' '+t:>16s}"
        print(f"{n:44s}{cells}")


if __name__ == "__main__":
    main()
