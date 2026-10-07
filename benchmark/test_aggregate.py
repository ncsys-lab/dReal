#!/usr/bin/env python3
"""Self-contained tests for aggregate.py (run: python3 test_aggregate.py).

aggregate.py compares the control and test arms of ONE corun.sh run, as ratios only; ground
truth (baseline.csv's annotations, never its times) checks every verdict the test arm gives.
compare_solvers.py makes the same comparison for any number of arms.
"""
import csv
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SUMMARY_FIELDS = ["benchmark_name", "solver_result", "cpu_time_s", "wall_time_s", "max_rss_kb",
                  "p_share", "exit_code"]

#            name                  control          test             ground truth
ROWS = [("1mhz_flip",          ("UNSAT", 4.0), ("SAT", 4.0),    "SAT"),
        ("1mhz_bad",           ("SAT", 4.0),   ("UNSAT", 4.0),  "SAT"),
        ("1mhz_tim_unsat",     ("TIM", 600.0), ("UNSAT", 3.0),  "SAT"),
        ("tacas_c2e2_unsgt",   ("UNSAT", 2.0), ("SAT", 2.0),    "UNSAT"),
        ("github_oct5_slow",   ("SAT", 10.0),  ("SAT", 30.0),   ""),
        ("github_oct5_fast",   ("SAT", 10.0),  ("SAT", 2.0),    ""),
        ("github_oct5_oom",    ("SAT", 20.0),  ("OOM", 50.0),   ""),
        ("odeexpr_v1_tim",     ("UNSAT", 5.0), ("TIM", 600.0),  None),
        ("odeexpr_v1_same",    ("UNSAT", 1.0), ("UNSAT", 1.1),  None)]


def _write_run(tmp):
    run = os.path.join(tmp, "run")
    for arm, col in (("control", 1), ("test", 2)):
        os.makedirs(os.path.join(run, arm))
        with open(os.path.join(run, arm, "summary.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=SUMMARY_FIELDS)
            w.writeheader()
            for row in ROWS:
                result, cpu = row[col]
                w.writerow({"benchmark_name": row[0], "solver_result": result,
                            "cpu_time_s": cpu, "wall_time_s": cpu, "max_rss_kb": 1,
                            "p_share": 0.0,
                            "exit_code": {"TIM": 152, "OOM": 137}.get(result, 0)})
    open(os.path.join(run, "cap"), "w").write("300\n")   # corun.sh records its CPU cap
    with open(os.path.join(run, "arms.tsv"), "w") as f:
        f.write("control\t/x/dreal4-abc\ntest\t/x/gcc_build/dreal4\n")
    truth = os.path.join(tmp, "truth.csv")
    with open(truth, "w", newline="") as f:   # baseline.csv's three-row header layout
        w = csv.writer(f)
        w.writerow(["", "elapsed_time", "solver_result", ""])
        w.writerow(["trial_name", "DRPM_0L", "DRPM_0L", "ground_truth"])
        w.writerow(["benchmark_file", "", "", ""])
        for name, _, _, gt in ROWS:
            if gt is not None:
                w.writerow([name + ".smt2", "1.0", "SolverResult.SAT", gt])
    state = os.path.join(tmp, "state.json")
    json.dump({"runs": []}, open(state, "w"))
    blacklist = os.path.join(tmp, "blacklist.txt")
    open(blacklist, "w").write("1mhz_old_kill\n")
    return run, truth, state, blacklist


def test_control_vs_test_of_one_run():
    with tempfile.TemporaryDirectory() as tmp:
        run, truth, state, blacklist = _write_run(tmp)
        r = subprocess.run([sys.executable, os.path.join(HERE, "aggregate.py"), run,
                            "--truth", truth, "--state", state, "--blacklist", blacklist],
                           capture_output=True, text=True, timeout=60)
        assert r.returncode == 0, r.stderr
        out = json.loads(r.stdout)
        # a wrong unsat is SOUNDNESS even when the control timed out
        assert out["correctness_flips"] == ["1mhz_bad", "1mhz_tim_unsat"], out
        assert [c["name"] for c in out["correctness_improvements"]] == ["1mhz_flip"], out
        reg = {x["name"]: x["priority"] for x in out["regressions"]}
        assert reg == {"1mhz_bad": "SOUNDNESS", "1mhz_tim_unsat": "SOUNDNESS",
                       "tacas_c2e2_unsgt": "COMPLETENESS", "github_oct5_slow": "TIMING",
                       "odeexpr_v1_tim": "ODEEXPR-HIGH"}, reg
        assert [e["name"] for e in out["exceptional"]] == ["github_oct5_fast"], out
        # a memory kill is excluded and blacklisted, never scored
        assert out["excluded_oom"] == ["github_oct5_oom"], out
        assert open(blacklist).read().split() == ["1mhz_old_kill", "github_oct5_oom"]
        # timing ratios only over benchmarks whose verdicts raise no question
        fam = out["family_ratios"]
        assert set(fam) == {"github", "odeexpr_v1", "weighted_overall"}, fam
        assert fam["github"]["ratio"] == 1.6, fam
        assert abs(fam["odeexpr_v1"]["ratio"] - 300.55 / 3) < 0.01, fam   # TIM = 2 x 300
        assert out["cap_s"] == 300, out
        # no absolute times in the report: every comparison is a ratio
        assert "_s\"" not in r.stdout.replace("\"cap_s\"", ""), r.stdout
        st = json.load(open(state))
        assert "anomalies" not in st and "exceptional" not in st, st
        assert st["runs"][-1]["arms"] == {"control": "/x/dreal4-abc", "test": "/x/gcc_build/dreal4"}


def test_compare_solvers_uses_the_cap_and_excludes_memory_kills():
    with tempfile.TemporaryDirectory() as tmp:
        run, _, _, _ = _write_run(tmp)
        r = subprocess.run([sys.executable, os.path.join(HERE, "compare_solvers.py"), "--cap", "50",
                            f"control={run}/control/summary.csv", f"test={run}/test/summary.csv"],
                           capture_output=True, text=True, timeout=60)
        assert r.returncode == 0, r.stderr
        assert "penalty 100" in r.stdout and "600 s" not in r.stdout, r.stdout
        assert "excluded (memory kill): github_oct5_oom" in r.stdout, r.stdout


def test_select_jobs_skips_blacklisted_benchmarks_and_carries_no_anomalies():
    blacklisted = set(open(os.path.join(HERE, "optsearch", "blacklist.txt")).read().split())
    r = subprocess.run([sys.executable, os.path.join(HERE, "select_jobs.py"), "--all"],
                       capture_output=True, text=True, timeout=120)
    assert r.returncode == 0, r.stderr
    picked = {ln.split("\t")[0].removesuffix(".smt2") for ln in r.stdout.splitlines()}
    assert picked and not picked & blacklisted, sorted(picked & blacklisted)
    r = subprocess.run([sys.executable, os.path.join(HERE, "select_jobs.py"), "--n", "8"],
                       capture_output=True, text=True, timeout=120)
    assert r.returncode == 0 and len(r.stdout.splitlines()) == 8, (r.stdout, r.stderr)


if __name__ == "__main__":
    import traceback
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = []
    for t in tests:
        try:
            t()
            print(f"ok    {t.__name__}")
        except Exception:
            failed.append(t.__name__)
            print(f"FAIL  {t.__name__}\n{traceback.format_exc(limit=1)}")
    print(f"{len(tests) - len(failed)} passed, {len(failed)} failed")
    sys.exit(1 if failed else 0)
