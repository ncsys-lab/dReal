# Volume-trace measurement (ICP frontier volume vs wall-clock)

> **Re-homing note (2026-07-24, R4 consolidation):** the recipe below targets
> `IcpSeq::CheckSat`'s iterable `vector<pair<Box,int>> stack`, and `IcpSeq` has been deleted —
> the only loop is now `IcpParallel` (`icp_parallel.cc`), whose `global_stack` is a lock-free
> `TreiberStack` with **no iteration**, at every `--jobs` including 1. To reuse the recipe,
> the throwaway build must also maintain a shadow frontier: at `-j1` (single worker, no
> races) keep a plain `std::multiset<double>` of per-box log-volumes beside the real stack —
> insert in `ParallelBranch` for the pushed child and on seed/root pushes, erase on pop/kept
> child — and run the sampling block at the top of `Worker`'s loop. CSV format, gating, and
> plotting below are unchanged. The code snippet is kept verbatim as the historical artifact
> of the 2026-06 `aim_tanh_n3` capture.

A throwaway diagnostic for **"why is this theory call slow?"** on a single, long-running
`CheckSat` (one SAT/theory call, no DPLL(T) ping-pong — e.g. the odeexpr_v2 QF `forall`
timeouts). It samples the total remaining search volume of the ICP branch-and-prune frontier
over wall-clock time and prints a CSV to stderr. Plotted, the **curve shape** is the signal:

- **fast decay → −∞**: healthy refutation, the search is shrinking the space.
- **early plateau** (volume flat while prune count climbs by millions): the solver is stuck
  bisecting a thin δ-width slab around the constraint boundary it cannot refute — the classic
  near-tight-UNSAT **completeness** wall.

It was the artifact that explained the 2026-06 `aim_tanh_n3` finding: baseline pinned at
`log_total_volume ≈ 3.16` for 60s / 8.9M prunes (flat plateau), while `--smear` dropped off
a cliff to refutation in seconds. Plateau-vs-cliff is the whole diagnosis.

## What to measure

`log_total_volume` = logsumexp over all frontier boxes of (per-box `sum_i log(diam_i)`).
- The DFS frontier is the unexplored-box stack; its **total** volume is the progress metric
  (monotone non-increasing → −∞ as the space is refuted), NOT any single "current box".
- Work in **log space**: raw volume (product of ~9 widths < 2) underflows to 0 instantly.
  logsumexp (subtract the max log-volume) avoids that; a zero-width dim makes a box's
  log-volume `-inf`, which drops out of the sum naturally.
- Also log `stack.size()` (frontier breadth) and the cumulative `num_prune_`/`num_branch_`.

## How to implement (recipe)

Reuse the **compile-time-constant gate** pattern (like `DREAL_EXPERIMENTAL_*_AUDIT_ENABLED`
in `version.h`), so the off build pays **zero** — proven last time by `IcpSeq::CheckSat` being
byte-for-byte identical (same 887 instructions) to the pre-change binary. Use the C
**preprocessor** `#if` (not a runtime `if`), because the trace needs loop-spanning local
declarations and `#if` excludes them entirely when off.

- `version.h`: `#define DREAL_EXPERIMENTAL_VOLUME_TRACE_MS 0` — **integer milliseconds**
  between samples, `0` = off. Must be integer: the preprocessor can't evaluate a float in
  `#if` (so no `_SECS 0.5`). Set to e.g. `500` in a throwaway build to capture.
- `src/dreal/solver/icp_seq.cc`, `IcpSeq::CheckSat` (run at **`-j1`** so `IcpSeq` is selected
  and its `vector<pair<Box,int>> stack` is iterable; the `-j>1` `IcpParallel` global stack is
  lock-free and can't be summed):
  - guarded includes: `<chrono> <cmath> <iostream> <limits>`.
  - per-call state right after `static IcpStat stat{...}`: a `static` call-id counter (one
    segment per `CheckSat` = one theory call), `steady_clock::now()` start, and `last`
    initialised one interval in the past so the first iteration samples.
  - at the **top of the `while (!stack.empty())` loop** (before the pop): gate the clock read
    behind an iteration mask (`(stat.num_prune_ & 0xFF) == 0`) so even the traced build's
    timing axis stays usable; then a wall-clock gate (`since.count() >= ..._MS`) before
    computing the logsumexp over `stack` and emitting one CSV line.

```cpp
// version.h
#define DREAL_EXPERIMENTAL_VOLUME_TRACE_MS 0   // ms between samples; 0 = off (not compiled)

// icp_seq.cc — top of the while loop in IcpSeq::CheckSat, before popping the box
#if DREAL_EXPERIMENTAL_VOLUME_TRACE_MS > 0
    if ((stat.num_prune_ & 0xFF) == 0) {
      const auto now = std::chrono::steady_clock::now();
      const std::chrono::duration<double, std::milli> since{now - volume_trace_last};
      if (since.count() >= DREAL_EXPERIMENTAL_VOLUME_TRACE_MS) {
        volume_trace_last = now;
        constexpr double kNegInf = -std::numeric_limits<double>::infinity();
        const auto box_log_volume = [](const Box& b) {        // sum_i log(diam_i)
          double lv = 0.0;
          const auto& iv = b.interval_vector();
          for (int i = 0; i < iv.size(); ++i) {
            const double d = iv[i].diam();
            if (d <= 0.0) return -std::numeric_limits<double>::infinity();
            lv += std::log(d);
          }
          return lv;
        };
        double max_lv = kNegInf;                              // logsumexp over the frontier
        for (const auto& bp : stack) max_lv = std::max(max_lv, box_log_volume(bp.first));
        double log_total_volume = kNegInf;
        if (max_lv > kNegInf) {
          double sum_exp = 0.0;
          for (const auto& bp : stack) {
            const double lv = box_log_volume(bp.first);
            if (lv > kNegInf) sum_exp += std::exp(lv - max_lv);
          }
          log_total_volume = max_lv + std::log(sum_exp);
        }
        const std::chrono::duration<double> elapsed{now - volume_trace_start};
        std::cerr << "VOLTRACE," << volume_trace_call_id << ',' << elapsed.count()
                  << ',' << stack.size() << ',' << log_total_volume << ','
                  << stat.num_prune_ << ',' << stat.num_branch_ << '\n';
      }
    }
#endif
```

CSV columns: `VOLTRACE, call_id, elapsed_s, n_boxes, log_total_volume, num_prune, num_branch`.

## Gotchas (learned)

- **`-j1` only.** Sequential `IcpSeq` has an iterable `vector` stack; the parallel path does not.
- **Integer ms, not float seconds** — preprocessor `#if` constraint.
- **First sample is `nan`**: the root box has unbounded dims (declared-without-bounds CSE
  symbols → `diam = +inf` → `inf - inf` in logsumexp) before the first prune establishes
  bounds. Filter non-finite values when plotting; it's cosmetic.
- **Cumulative counters**: `stat` is `static`, so `num_prune_`/`num_branch_` accumulate across
  `CheckSat` calls (segment by `call_id` + `elapsed`, don't read them as per-call).
- **Traced build is shape-only**, never use its wall-clock for cross-config timing — the
  per-iteration clock reads perturb it (and never measure timing in a Debug build, ~10× slow).
- **Separate build dir**: flip the macro to e.g. `500`, build a throwaway binary, run; keep the
  normal (macro `0`) binary for any actual timing.

## Plotting

`grep VOLTRACE` the captured stderr; per `call_id` plot `log_total_volume` vs `elapsed_s`
(top) and `n_boxes` (log scale, bottom); overlay a timing-out instance against a well-behaved
one (e.g. a fast-`unsat` n1/n2 `forall`). matplotlib lives in the odeexpr_v2 `.venv`. The
plateau (timing-out) vs cliff (well-behaved) contrast is the deliverable.
