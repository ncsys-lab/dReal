#!/usr/bin/env python3
"""Self-contained tests for corun.sh, measure.py and parse_results.py (run: python3 test_corun.py).

A fake solver logs when each (arm, benchmark) run started and ended, then sleeps for the
seconds written in the benchmark file ("spin" busy-loops instead). The tests check the
comparison-validity rule a local A/B shares with Sherlock: every arm of one benchmark starts at
the same moment on the same core type (E-cores), the pool never exceeds MAXJOBS processes, the
time limit is CPU seconds, and a run bigger than a spot check is refused.
"""
import csv
import json
import os
import subprocess
import sys
import tempfile

import parse_results

HERE = os.path.dirname(os.path.abspath(__file__))
CORUN = os.path.join(HERE, "corun.sh")

FAKE_SOLVER = """#!/usr/bin/env python3
import json, os, sys, time
path = sys.argv[-1]
t0 = time.time()
spec = open(path).read()
while spec == "spin":
    pass
if spec == "killparent":   # measure.py dies before recording this run
    os.kill(os.getppid(), 9)
time.sleep(float(spec))
with open(os.environ["CORUN_TEST_LOG"], "a") as f:
    f.write(json.dumps({"arm": sys.argv[1], "bench": os.path.basename(path),
                        "t0": t0, "t1": time.time()}) + "\\n")
print("unsat")
"""


def _setup(tmp, sleeps, trailing_newline=True):
    solver = os.path.join(tmp, "fake_solver")
    with open(solver, "w") as f:
        f.write(FAKE_SOLVER)
    os.chmod(solver, 0o755)
    jobs = os.path.join(tmp, "jobs.tsv")
    with open(jobs, "w") as f:
        for i, s in enumerate(sleeps):
            p = os.path.join(tmp, f"b{i}.smt2")
            with open(p, "w") as g:
                g.write(str(s))
            f.write(f"b{i}.smt2\t{p}" + ("\n" if trailing_newline or i < len(sleeps) - 1 else ""))
    return solver, jobs


def _corun(tmp, jobs, arms, maxjobs=4, timeout="30", cwd=None):
    env = dict(os.environ, MAXJOBS=str(maxjobs), TIMEOUT=timeout,
               CORUN_TEST_LOG=os.path.join(tmp, "log.jsonl"))
    return subprocess.run(["bash", CORUN, os.path.join(tmp, "out"), jobs, *arms],
                          env=env, capture_output=True, text=True, timeout=120, cwd=cwd)


def _results(tmp, arm):
    return list(csv.DictReader(open(os.path.join(tmp, "out", arm, "summary.csv"))))


def test_arms_of_a_benchmark_start_together_and_the_pool_is_bounded():
    with tempfile.TemporaryDirectory() as tmp:
        solver, jobs = _setup(tmp, [1.5, 0.8, 1.2, 1.0, 1.4, 0.8])
        r = _corun(tmp, jobs, [f"a={solver} A", f"b={solver} B"], maxjobs=4)
        assert r.returncode == 0, r.stderr
        runs = [json.loads(ln) for ln in open(os.path.join(tmp, "log.jsonl"))]
        assert len(runs) == 12, runs
        for b in {x["bench"] for x in runs}:
            t0 = [x["t0"] for x in runs if x["bench"] == b]
            assert len(t0) == 2 and max(t0) - min(t0) < 0.5, (b, t0)
        events = sorted([(x["t0"], 1) for x in runs] + [(x["t1"], -1) for x in runs])
        live = peak = 0
        for _, d in events:
            live += d
            peak = max(peak, live)
        assert peak == 4, peak   # two groups of two arms at once, never more
        for arm in ("a", "b"):
            rows = list(csv.DictReader(open(os.path.join(tmp, "out", arm, "summary.csv"))))
            assert [r["solver_result"] for r in rows] == ["UNSAT"] * 6, rows
            assert all(float(r["p_share"]) <= parse_results.P_SHARE_MAX for r in rows), rows


def test_a_last_line_without_newline_still_runs():
    with tempfile.TemporaryDirectory() as tmp:
        solver, jobs = _setup(tmp, [0.1, 0.1, 0.1], trailing_newline=False)
        r = _corun(tmp, jobs, [f"a={solver} A"])
        assert r.returncode == 0, r.stderr
        assert len(_results(tmp, "a")) == 3


def test_a_slash_less_binary_is_the_file_in_the_working_directory():
    with tempfile.TemporaryDirectory() as tmp:
        _, jobs = _setup(tmp, [0.1])
        r = _corun(tmp, jobs, ["a=fake_solver A"], cwd=tmp)
        assert r.returncode == 0, r.stderr
        assert _results(tmp, "a")[0]["solver_result"] == "UNSAT"


def test_a_run_that_was_not_recorded_fails_the_corun():
    with tempfile.TemporaryDirectory() as tmp:
        solver, jobs = _setup(tmp, [0.1, "killparent"])
        r = _corun(tmp, jobs, [f"a={solver} A"])
        assert r.returncode != 0 and "b1" in r.stderr, (r.returncode, r.stderr)


def test_a_fractional_timeout_is_refused():
    with tempfile.TemporaryDirectory() as tmp:
        solver, jobs = _setup(tmp, [0.1])
        r = _corun(tmp, jobs, [f"a={solver} A"], timeout="1.5")
        assert r.returncode != 0 and "TIMEOUT" in r.stderr, r.stderr
        assert not os.path.exists(os.path.join(tmp, "log.jsonl"))


def test_the_time_limit_is_cpu_seconds():
    with tempfile.TemporaryDirectory() as tmp:
        solver, jobs = _setup(tmp, ["spin"])
        env = dict(os.environ, TIMEOUT="1", CORUN_TEST_LOG=os.path.join(tmp, "log.jsonl"))
        r = subprocess.run(["bash", CORUN, os.path.join(tmp, "out"), jobs, f"a={solver} A"],
                           env=env, capture_output=True, text=True, timeout=120)
        assert r.returncode == 0, r.stderr
        rows = list(csv.DictReader(open(os.path.join(tmp, "out", "a", "summary.csv"))))
        assert rows[0]["solver_result"] == "TIM" and rows[0]["exit_code"] == "152", rows
        assert open(os.path.join(tmp, "out", "cap")).read().strip() == "1"


def test_a_run_with_p_core_time_fails_the_parse():
    with tempfile.TemporaryDirectory() as tmp:
        for name, share in (("e", 0.0), ("p", 0.4)):
            open(os.path.join(tmp, f"{name}.exit"), "w").write("0\n")
            open(os.path.join(tmp, f"{name}.stdout"), "w").write("unsat\n")
            json.dump({"cpu_s": 1.0, "wall_s": 1.0, "p_share": share, "max_footprint_kb": 1},
                      open(os.path.join(tmp, f"{name}.rusage"), "w"))
        try:
            parse_results.parse_results_dir(tmp)
        except RuntimeError as e:
            assert "p.rusage" in str(e) and "e.rusage" not in str(e), e
            return
        raise AssertionError("a run with P-core time parsed without error")


def test_more_than_a_spot_check_is_refused():
    with tempfile.TemporaryDirectory() as tmp:
        solver, jobs = _setup(tmp, [0.0] * 13)
        r = _corun(tmp, jobs, [f"a={solver} A", f"b={solver} B"], maxjobs=12)
        assert r.returncode != 0 and "Sherlock" in r.stderr, (r.returncode, r.stderr)
        assert not os.path.exists(os.path.join(tmp, "log.jsonl"))


def test_more_arms_than_the_pool_is_refused():
    with tempfile.TemporaryDirectory() as tmp:
        solver, jobs = _setup(tmp, [0.0])
        r = _corun(tmp, jobs, [f"a{i}={solver} A" for i in range(3)], maxjobs=2)
        assert r.returncode != 0, r.stderr
        assert not os.path.exists(os.path.join(tmp, "log.jsonl"))


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
