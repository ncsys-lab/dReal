#!/usr/bin/env python3
"""Compare the test arm against the control arm of ONE corun.sh run — as ratios only.

Usage: python3 aggregate.py <run_dir> [--control control] [--test test]
                            [--truth benchmark/baseline.csv] [--state benchmark/state.json]
                            [--blacklist benchmark/optsearch/blacklist.txt]
  - Reads <run_dir>/<control>/summary.csv, <run_dir>/<test>/summary.csv, <run_dir>/arms.tsv and
    <run_dir>/cap (the CPU-seconds cap corun.sh ran with)
  - Reads ground truth from --truth (baseline.csv's annotations; never its times)
  - Writes <run_dir>/anomaly_report.txt, appends the run to --state's log, appends memory-killed
    benchmarks to --blacklist, prints a JSON summary

Both arms co-ran (same moment, same core type), so a per-benchmark ratio is a valid comparison;
an absolute time is not, and none is reported. PAR2 = CPU seconds if solved, 2 x the CPU cap
otherwise. Each run stands alone: a regression is a regression against this run's control.
"""
import argparse
import csv
import json
import os
from collections import defaultdict
from datetime import datetime, timezone

from odeexpr import family_of, FAMILY_WEIGHTS, MANIFEST_FAMILY_NAMES

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

REGRESSION_RATIO = 1.5   # test PAR2 > 1.5x control → regression
EXCEPTIONAL_RATIO = 0.6  # test PAR2 < 0.6x control → exceptional

# Timing resolution / noise floors: when BOTH arms are this fast, skip the ratio (sub-0.1 s
# jitter is noise); otherwise floor the divisor so an instant→seconds jump still divides.
NEGLIGIBLE_S = 0.1
TIME_FLOOR_S = 0.01

# Report ordering: correctness first, then the high-priority odeexpr family,
# then ordinary solve→fail (HIGH) and plain timing regressions.
_PRIORITY_ORDER = ["SOUNDNESS", "COMPLETENESS", "ODEEXPR-HIGH", "ODEEXPR", "HIGH", "TIMING"]
_WRONG_VERDICT = {  # the test arm's verdict contradicts ground truth (CLAUDE.md's mandated labels)
    "UNSAT": ("SOUNDNESS", "asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat"),
    "SAT": ("COMPLETENESS", "asserts φ^δ T-satisfiable on a T-unsatisfiable φ — missed refutation"),
}
SOLVED = ("SAT", "UNSAT")


def load_ground_truth(truth_csv: str) -> dict[str, str]:
    """{benchmark_name (no .smt2): SAT|UNSAT} from baseline.csv's ground_truth column."""
    rows = list(csv.reader(open(truth_csv)))
    col = [h.strip() for h in rows[1]].index("ground_truth")
    return {r[0].strip().removesuffix(".smt2"): r[col].strip()
            for r in rows[3:] if r and r[0].strip() and col < len(r) and r[col].strip()}


def load_arm(run_dir: str, label: str) -> dict[str, dict]:
    with open(os.path.join(run_dir, label, "summary.csv")) as f:
        return {r["benchmark_name"]: r for r in csv.DictReader(f)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run_dir")
    ap.add_argument("--control", default="control")
    ap.add_argument("--test", default="test")
    ap.add_argument("--truth", default=os.path.join(SCRIPT_DIR, "baseline.csv"))
    ap.add_argument("--state", default=os.path.join(SCRIPT_DIR, "state.json"))
    ap.add_argument("--blacklist", default=os.path.join(SCRIPT_DIR, "optsearch", "blacklist.txt"))
    args = ap.parse_args()
    cap = int(open(os.path.join(args.run_dir, "cap")).read())

    control, test = load_arm(args.run_dir, args.control), load_arm(args.run_dir, args.test)
    if control.keys() != test.keys():
        raise ValueError(f"arms ran different benchmarks: {sorted(control.keys() ^ test.keys())}")
    truth = load_ground_truth(args.truth)
    arms = dict(line.rstrip("\n").split("\t", 1) for line in open(os.path.join(args.run_dir, "arms.tsv")))

    def par2(row):
        return float(row["cpu_time_s"]) if row["solver_result"] in SOLVED else 2 * cap

    regressions, exceptional, improvements, undetermined, excluded_oom = [], [], [], [], []
    fam_par2 = defaultdict(lambda: ([], []))
    for name in sorted(control):
        cr, tr = control[name]["solver_result"], test[name]["solver_result"]
        entry = {"name": name, "control_result": cr, "test_result": tr}
        gt = truth.get(name, "")
        if "OOM" in (cr, tr):          # a memory event, not a result: excluded and blacklisted
            excluded_oom.append(name)
        elif gt and tr in SOLVED and tr != gt:
            kind, what = _WRONG_VERDICT[tr]
            regressions.append({**entry, "priority": kind, "ground_truth": gt,
                                "reason": f"{kind} ({what})"})
        elif gt and cr in SOLVED and cr != gt:
            improvements.append({**entry, "ground_truth": gt})
        elif {cr, tr} == {"SAT", "UNSAT"}:
            undetermined.append(entry)
        else:                          # verdicts raise no question: compare the times
            pc, pt = par2(control[name]), par2(test[name])
            fam = family_of(name)
            if fam is not None:
                fam_par2[fam][0].append(pc)
                fam_par2[fam][1].append(pt)
            ratio = 1.0 if max(pc, pt) < NEGLIGIBLE_S else max(pt, TIME_FLOOR_S) / max(pc, TIME_FLOOR_S)
            reason = f"PAR2 ratio test/control {ratio:.2f}x ({cr} -> {tr})"
            if ratio > REGRESSION_RATIO:
                to_fail = cr in SOLVED and tr not in SOLVED
                high = fam in MANIFEST_FAMILY_NAMES
                priority = ("ODEEXPR-HIGH" if to_fail else "ODEEXPR") if high else ("HIGH" if to_fail else "TIMING")
                regressions.append({**entry, "priority": priority, "ratio": round(ratio, 3), "reason": reason})
            elif ratio < EXCEPTIONAL_RATIO:
                exceptional.append({**entry, "ratio": round(ratio, 3), "reason": reason})

    family_ratios, w_test, w_ctrl, w_sum = {}, 0.0, 0.0, 0.0
    for fam in sorted(FAMILY_WEIGHTS, key=lambda f: -FAMILY_WEIGHTS[f]):
        if fam not in fam_par2:
            continue
        pcs, pts = fam_par2[fam]
        mc, mt, w = sum(pcs) / len(pcs), sum(pts) / len(pts), FAMILY_WEIGHTS[fam]
        family_ratios[fam] = {"n": len(pcs), "weight": w, "ratio": round(mt / mc, 3)}
        w_test, w_ctrl, w_sum = w_test + w * mt, w_ctrl + w * mc, w_sum + w
    if w_sum:
        family_ratios["weighted_overall"] = {"ratio": round(w_test / w_ctrl, 3)}

    listed = set(open(args.blacklist).read().split())
    with open(args.blacklist, "a") as f:
        f.writelines(f"{n}\n" for n in excluded_oom if n not in listed)

    regressions.sort(key=lambda r: _PRIORITY_ORDER.index(r["priority"]))
    soundness = [r["name"] for r in regressions if r["priority"] == "SOUNDNESS"]
    report_path = os.path.join(args.run_dir, "anomaly_report.txt")
    with open(report_path, "w") as f:
        f.write(f"Co-run: {args.run_dir}  ({args.test} vs {args.control}, {len(control)} benchmarks)\n\n")
        for title, items in (("REGRESSIONS", regressions), ("EXCEPTIONAL", exceptional),
                             ("CORRECTNESS IMPROVEMENTS (control contradicts ground truth, test does not)", improvements),
                             ("UNDETERMINED FLIPS (no ground truth: soundness or completeness)", undetermined)):
            f.write(f"=== {title} ({len(items)}) ===\n")
            for it in items:
                f.write(f"  [{it.get('priority', '')}] {it['name']}: {it.get('reason', '')} "
                        f"control={it['control_result']} test={it['test_result']}\n")
        f.write(f"=== EXCLUDED, MEMORY KILL — blacklisted ({len(excluded_oom)}) ===\n"
                + "".join(f"  {n}\n" for n in excluded_oom))

    state = json.load(open(args.state))
    state.setdefault("runs", []).append({
        "timestamp": datetime.now(timezone.utc).isoformat(), "results_dir": args.run_dir,
        "arms": {args.control: arms[args.control], args.test: arms[args.test]},
        "n_ran": len(control), "n_regressions": len(regressions), "n_exceptional": len(exceptional),
        "n_correctness_improvements": len(improvements), "n_undetermined_flips": len(undetermined),
        "n_excluded_oom": len(excluded_oom), "correctness_flips": soundness})
    json.dump(state, open(args.state, "w"), indent=2)

    print(json.dumps({
        "n_ran": len(control), "cap_s": cap,
        "n_regressions": len(regressions), "n_exceptional": len(exceptional),
        "n_correctness_improvements": len(improvements), "n_undetermined_flips": len(undetermined),
        "correctness_flips": soundness, "regressions": regressions, "exceptional": exceptional,
        "correctness_improvements": improvements, "undetermined_flips": undetermined,
        "excluded_oom": excluded_oom, "family_ratios": family_ratios,
        "anomaly_report": open(report_path).read(),
    }, indent=2))


if __name__ == "__main__":
    main()
