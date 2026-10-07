#!/usr/bin/env python3
"""Extract and analyze `drpm_benchmark_log` records from dReal4 stderr.

The solver prints one line per learned theory lemma (per conflict) to std::cerr,
unconditionally (not gated on --verbose or lemma size), from `drpm_benchmark_log`
in src/dreal/solver/context_impl.cc. Each line carries, for that lemma:

    S.FCM   fully-constrained model?      (bool)
    S.ms    SAT-solve time for this round (ms)
    T.ms    theory-solve time            (ms)   <- "time per lemma"
    L       lemma size = #literals        (int)  <- the explanation length
    mode    'M' pattern-matched (size < drpm_max_size) | 'A' added direct
    PM.ms   pattern-match time            (ms)
    PM      #pattern matches found        (int)
    C26.*   CAV26 symmetry-filter counts  (int)  [only if built with that macro]

This module is the durable extraction layer (`parse_line`, `read_log`,
`scan_sweep`) plus a generic distribution/comparison CLI over ANY numeric field,
so lemma-size, time-per-lemma, pm-time, etc. analyses all reuse one tool.

Library (import):
    from drpm_log import parse_line, read_log, scan_sweep, summarize, field_values
CLI:
    python3 drpm_log.py <sweep_dir|file.solver_log> [--field lemma_size]
        [--group-by config|benchmark|none] [--bins 20]
        [--compare-to <ref_config>] [--same-verdict] [--csv out.csv]

stdlib-only, matching the rest of benchmark/.
"""
import argparse
import csv
import math
import os
import re
import statistics
import sys
from dataclasses import dataclass, fields as dataclass_fields

# A drpm line begins with ".\t S.FCM "; anything else on stderr is other output.
_SIGNATURE = re.compile(r"^\.\t S\.FCM ")
_FLOAT = r"[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?"

_RE = {
    "fcm": re.compile(r"S\.FCM (\d+)"),
    "sat_ms": re.compile(rf"S\.ms ({_FLOAT})"),
    "theory_ms": re.compile(rf"T\.ms ({_FLOAT})"),
    "lemma_mode": re.compile(r"\bL (\d+) ([MA])\b"),
    "pm_ms": re.compile(rf"PM\.ms ({_FLOAT})"),
    "pm_matches": re.compile(r"\bPM (\d+)\b"),
}
_RE_C26 = {
    "c26_not_pure_time": re.compile(r"C26\.npT (\d+)"),
    "c26_not_pure_logic": re.compile(r"C26\.npL (\d+)"),
    "c26_not_pure_any": re.compile(r"C26\.np\* (\d+)"),
}


@dataclass(frozen=True)
class DrpmRecord:
    fcm: bool
    sat_ms: float
    theory_ms: float
    lemma_size: int
    mode: str
    pm_ms: float
    pm_matches: int
    c26_not_pure_time: int | None = None
    c26_not_pure_logic: int | None = None
    c26_not_pure_any: int | None = None


# Numeric fields exposed to the generic analysis CLI (--field).
NUMERIC_FIELDS = (
    "lemma_size", "theory_ms", "sat_ms", "pm_ms", "pm_matches",
    "c26_not_pure_time", "c26_not_pure_logic", "c26_not_pure_any",
)


def _need(line: str, key: str):
    m = _RE[key].search(line)
    if m is None:
        # The line carried the drpm signature but a required field is missing:
        # a real format drift, not a foreign stderr line. Fail loud.
        raise ValueError(f"drpm line missing field {key!r}: {line!r}")
    return m


def parse_line(line: str) -> DrpmRecord | None:
    """Parse one stderr line. Returns None for non-drpm lines (other output);
    raises ValueError for a drpm-signature line that does not fully parse."""
    if _SIGNATURE.match(line) is None:
        return None
    lm = _need(line, "lemma_mode")
    c26 = {k: (int(m.group(1)) if (m := r.search(line)) else None)
           for k, r in _RE_C26.items()}
    return DrpmRecord(
        fcm=bool(int(_need(line, "fcm").group(1))),
        sat_ms=float(_need(line, "sat_ms").group(1)),
        theory_ms=float(_need(line, "theory_ms").group(1)),
        lemma_size=int(lm.group(1)),
        mode=lm.group(2),
        pm_ms=float(_need(line, "pm_ms").group(1)),
        pm_matches=int(_need(line, "pm_matches").group(1)),
        **c26,
    )


def iter_records(lines) -> "list[DrpmRecord]":
    for line in lines:
        rec = parse_line(line.rstrip("\n"))
        if rec is not None:
            yield rec


def read_log(path: str) -> list[DrpmRecord]:
    with open(path, errors="replace") as f:
        return list(iter_records(f))


def scan_sweep(sweep_dir: str) -> dict[str, dict[str, list[DrpmRecord]]]:
    """sweep_dir/<config>/<benchmark>.solver_log -> {config: {benchmark: [records]}}.

    A corun.sh output dir (one subdir per arm). Benchmark names are normalized (no .smt2)."""
    out: dict[str, dict[str, list[DrpmRecord]]] = {}
    for config in sorted(os.listdir(sweep_dir)):
        cdir = os.path.join(sweep_dir, config)
        if not os.path.isdir(cdir):
            continue
        logs = [f for f in os.listdir(cdir) if f.endswith(".solver_log")]
        if not logs:
            continue
        per_bench: dict[str, list[DrpmRecord]] = {}
        for fname in sorted(logs):
            bench = fname[:-len(".solver_log")].removesuffix(".smt2")
            per_bench[bench] = read_log(os.path.join(cdir, fname))
        out[config] = per_bench
    return out


def read_verdicts(config_dir: str) -> dict[str, str]:
    """benchmark -> solver_result from a config's summary.csv (parse_results.py)."""
    path = os.path.join(config_dir, "summary.csv")
    if not os.path.exists(path):
        return {}
    out = {}
    with open(path) as f:
        for row in csv.DictReader(f):
            out[row["benchmark_name"].strip().removesuffix(".smt2")] = row["solver_result"].strip()
    return out


def field_values(records, field: str) -> list[float]:
    return [getattr(r, field) for r in records if getattr(r, field) is not None]


# --------------------------------------------------------------------------- #
# Distribution helpers
# --------------------------------------------------------------------------- #
def _pct(sorted_vals: list[float], p: float) -> float | None:
    if not sorted_vals:
        return None
    k = (len(sorted_vals) - 1) * p
    lo, hi = math.floor(k), math.ceil(k)
    if lo == hi:
        return float(sorted_vals[int(k)])
    return sorted_vals[lo] * (hi - k) + sorted_vals[hi] * (k - lo)


def summarize(values) -> dict:
    vals = sorted(float(v) for v in values)
    n = len(vals)
    if n == 0:
        return {"n": 0, "mean": None, "median": None, "p90": None, "p99": None,
                "min": None, "max": None, "total": None}
    return {
        "n": n,
        "mean": statistics.fmean(vals),
        "median": _pct(vals, 0.50),
        "p90": _pct(vals, 0.90),
        "p99": _pct(vals, 0.99),
        "min": vals[0],
        "max": vals[-1],
        "total": math.fsum(vals),
    }


def ascii_histogram(values, bins: int = 20, width: int = 50) -> str:
    vals = [float(v) for v in values]
    if not vals:
        return "  (no records)"
    lo, hi = min(vals), max(vals)
    if lo == hi:
        return f"  [{lo:g}] {'#' * min(width, len(vals))} {len(vals)}"
    step = (hi - lo) / bins
    counts = [0] * bins
    for v in vals:
        idx = min(bins - 1, int((v - lo) / step))
        counts[idx] += 1
    peak = max(counts) or 1
    lines = []
    for i, c in enumerate(counts):
        edge_lo, edge_hi = lo + i * step, lo + (i + 1) * step
        bar = "#" * round(width * c / peak)
        lines.append(f"  [{edge_lo:9.3g}, {edge_hi:9.3g})  {bar:<{width}} {c}")
    return "\n".join(lines)


def _fmt(x) -> str:
    return "—" if x is None else (f"{x:.3g}" if isinstance(x, float) else str(x))


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def _print_summary_table(groups: dict[str, list[float]], field: str) -> None:
    cols = ("n", "mean", "median", "p90", "p99", "max", "total")
    print(f"\n{field} distribution by group:")
    print(f"  {'group':<28} " + " ".join(f"{c:>10}" for c in cols))
    for name in sorted(groups):
        s = summarize(groups[name])
        print(f"  {name:<28} " + " ".join(f"{_fmt(s[c]):>10}" for c in cols))


def _compare(sweep: dict[str, dict[str, list[DrpmRecord]]], sweep_dir: str,
             field: str, ref: str, same_verdict: bool) -> None:
    """Paired per-benchmark median(field) delta of each config vs ref, over
    benchmarks present in all configs (optionally only same-verdict ones)."""
    configs = sorted(sweep)
    if ref not in configs:
        raise ValueError(f"--compare-to {ref!r} not among configs {configs}")
    verdicts = {c: read_verdicts(os.path.join(sweep_dir, c)) for c in configs}
    common = set.intersection(*[set(sweep[c]) for c in configs])

    def same(b: str) -> bool:
        vs = {verdicts[c].get(b) for c in configs}
        return len(vs) == 1 and None not in vs

    benches = sorted(b for b in common if (not same_verdict or same(b)))
    print(f"\nPaired comparison of median {field} vs ref={ref!r} "
          f"({'same-verdict ' if same_verdict else ''}common benchmarks: {len(benches)}):")
    for c in configs:
        if c == ref:
            continue
        deltas, shrink, grow, flat = [], 0, 0, 0
        for b in benches:
            rv = field_values(sweep[ref][b], field)
            cv = field_values(sweep[c][b], field)
            if not rv or not cv:
                continue
            d = _pct(sorted(cv), 0.5) - _pct(sorted(rv), 0.5)
            deltas.append(d)
            shrink += d < 0
            grow += d > 0
            flat += d == 0
        med = _pct(sorted(deltas), 0.5) if deltas else None
        print(f"  {c:<20} median Δ={_fmt(med):>8}  "
              f"shrink={shrink} grow={grow} flat={flat}  (n={len(deltas)})")


def _write_csv(sweep: dict[str, dict[str, list[DrpmRecord]]], field: str, path: str) -> None:
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["config", "benchmark", "field", "n", "mean", "median", "p90", "max", "total"])
        for c in sorted(sweep):
            for b in sorted(sweep[c]):
                s = summarize(field_values(sweep[c][b], field))
                w.writerow([c, b, field, s["n"], _fmt(s["mean"]), _fmt(s["median"]),
                            _fmt(s["p90"]), _fmt(s["max"]), _fmt(s["total"])])
    print(f"wrote per-(config,benchmark) rows -> {path}", file=sys.stderr)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="a corun.sh output dir, or a single .solver_log")
    ap.add_argument("--field", default="lemma_size", choices=NUMERIC_FIELDS)
    ap.add_argument("--group-by", default="config", choices=("config", "benchmark", "none"))
    ap.add_argument("--bins", type=int, default=20)
    ap.add_argument("--compare-to", default=None, metavar="REF_CONFIG",
                    help="paired per-benchmark median delta vs this config")
    ap.add_argument("--same-verdict", action="store_true",
                    help="restrict comparison to benchmarks with one verdict across configs")
    ap.add_argument("--csv", default=None, help="dump per-(config,benchmark) stats here")
    args = ap.parse_args()

    if os.path.isfile(args.path):
        recs = read_log(args.path)
        vals = field_values(recs, args.field)
        print(f"{args.path}: {len(recs)} lemmas")
        print(f"\n{args.field}: {summarize(vals)}")
        print(ascii_histogram(vals, args.bins))
        return

    sweep = scan_sweep(args.path)
    if not sweep:
        raise ValueError(f"no <config>/*.solver_log under {args.path!r}")

    if args.group_by == "config":
        groups = {c: field_values([r for recs in per.values() for r in recs], args.field)
                  for c, per in sweep.items()}
        print("NOTE: per-config pooled stats are confounded by which benchmarks each "
              "config solved/TIMed; use --compare-to for the paired read.")
        _print_summary_table(groups, args.field)
        for c in sorted(sweep):
            print(f"\n[{c}] {args.field}:")
            print(ascii_histogram(groups[c], args.bins))
    elif args.group_by == "benchmark":
        groups = {b: field_values([r for c in sweep for r in sweep[c].get(b, [])], args.field)
                  for b in set().union(*[set(p) for p in sweep.values()])}
        _print_summary_table(groups, args.field)
    else:
        allvals = field_values([r for per in sweep.values() for recs in per.values()
                                for r in recs], args.field)
        print(f"\n{args.field} (all records): {summarize(allvals)}")
        print(ascii_histogram(allvals, args.bins))

    if args.compare_to:
        _compare(sweep, args.path, args.field, args.compare_to, args.same_verdict)
    if args.csv:
        _write_csv(sweep, args.field, args.csv)


if __name__ == "__main__":
    main()
