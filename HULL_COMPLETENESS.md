# Hull-grid completeness coupling — a latent looseness in the per-slice ODE tube

> **Correction 2026-10-05 (BUG-018, `docs/dreal-bugs.md`).** The Resolution below says the
> mean-value tube carries "no soundness risk" and that flips are "provably impossible". That was
> false at the CAPD pin of the time (`b353e170`): CAPD's `Curve::timeDerivative` scaled the
> initial-condition spread by f(X)−f(c) instead of X−c, so the mean-value half cut real
> trajectories off interval initial sets — SOUNDNESS (asserts φ T-unsatisfiable on a
> T-satisfiable φ — false unsat), e.g. `x' = 0.1x`, x0 ∈ [1,2], `x_t ≥ 2.205` → `unsat`. CAPD
> fixed it in `2a2263c7` (2026-09-14); dReal pins `03dc5628`. The Measured outcome's github
> 0.19× / tacas 0.60× PAR2 was taken on the unsound tube; see the correction's A/B in BUG-018.
> The claim that hull-grid "is no longer completeness-load-bearing" held for thin initial sets
> only: on an interval start a long step leaves the tube too loose (BUG-013, left open; a
> monotone-in-time hull fixed it and was dropped). The body is kept as the record.

**Soundness vs. completeness:** the failure mode here is **COMPLETENESS**
(missed refutation / false-`delta-sat`), **not** SOUNDNESS (false-`unsat`). The
file's old name (`HULL_SOUNDNESS.md`) mislabeled it, which read as a correctness
emergency and cancelled experiments — the cautionary case behind
`docs/soundness-vs-completeness.md` (the dichotomy + notation discipline).

**Status: RESOLVED (2026-06) by a centered-in-time range, NOT the width-based
fix this file originally sketched.** The actual root cause is narrower than the
"sub-interval width" framing below: CAPD's `curve(sub)` was already centered on
the initial-condition spread but evaluated the Taylor polynomial in *time* by
naive interval Horner over the whole sub-interval — the time-dependency problem.
Replacing that single evaluation with a mean-value-in-time enclosure
`x(mid) + x'(sub)·(sub−mid)`, intersected with the naive result (so it is never
looser), tightens the whole tube uniformly with **no knob change and no
subdivision**. The F1 interior-violation case now refutes at the **default**
hull-grid; `--ode-hull-grid` is no longer completeness-load-bearing. See
"Resolution" below for the implementation and measured trade-off. The
width-based subdivision (Stage 2) and refine-on-demand (Stage 3) were **not
needed** and were not built. The investigation history below is retained as the
record of *why* hull-grid was a completeness knob and how the fix was scoped.

## TL;DR

The per-slice tube enclosures the ODE contractor produces are **far looser than
CAPD's actual precision allows**, because the sub-slicing uses a fixed *count*
(`kHullGrid`) per CAPD step. When CAPD takes a large adaptive step, each
sub-interval is wide, and evaluating the Taylor curve over a wide time-interval
(`curve(sub)`) blows up via the **polynomial dependency problem**. This silently
weakens refutation power — most visibly, **interior invariant-violation
detection** — and couples a *completeness-relevant* (refutation) capability to a
*performance* knob. Lowering `--ode-hull-grid` for speed therefore trades away refutation
completeness, invisibly. The fix is to bound the sub-interval **width**, not its
count.

## How it surfaced

The 2026-06 meta-parameter sweep (OPTIMIZATION_LOG.md "2026-06 re-tuning
campaign") found that forward order 12 + hull-grid 4 was ~2× faster on the
123-job ODE corpus with **zero** SAT↔UNSAT flips, and that default was briefly
adopted. The Debug suite then failed exactly one test:

```
[ FAILED ] GravityInvariantTest.FwdInteriorInvariantViolation_BoxEmpties
```

This is the regression test (`test/dreal/contractor/test/contractor_odes_semantic_test.cc`)
for the cav26 per-slice restoration (commit `5d619fe3f`) — it checks that an
invariant violated **only at a trajectory's interior** (not at either endpoint)
is still detected and refutes.

**The wrong first instinct** (caught in review) was to bump hull-grid back up
until the test passed (hull-8 passes). That is gaming the verifier: it hides the
defect behind the same fragile coupling instead of fixing it. See the amended
`rules/dont-game-the-verifier.md`.

## The scenario

Gravity flow `dx/dt = v, dv/dt = -1`, from a **point** initial condition
`x0 = 0, v0 = 1`, terminal pinned at `t = 2`. The exact trajectory is

```
v(t) = 1 - t           (exactly linear)
x(t) = t - t²/2        (exactly quadratic; peak x(1) = 0.5)
```

Invariant `∀t∈[0,2]. x ≤ 0.3`. It holds at both endpoints (`x(0)=x(2)=0`) and on
the terminal gate box, but is violated at the **interior peak** `x(1)=0.5 > 0.3`
(margin 0.2 — enormous vs the 1e-3 precision). Ground truth: **UNSAT**. A correct
per-slice filter refutes by finding a slice whose `x` enclosure lower bound
exceeds 0.3.

## The evidence (slice dump at hull-grid 4, order 12)

Instrumenting the per-slice invariant loop (`contractor_odes.cc`) to print each
slice's time span and state enclosure:

```
slice t=[0.0,0.5]  x=[0.000000, 0.500000]  v=[0.500000, 1.000000]  violated=0
slice t=[0.5,1.0]  x=[0.250000, 0.750000]  v=[0.000000, 0.500000]  violated=0
slice t=[1.0,1.5]  x=[0.250000, 0.750000]  v=[-0.500000, 0.000000] violated=0
slice t=[1.5,2.0]  x=[0.000000, 0.500000]  v=[-1.000000,-0.500000] violated=0
```

Read this carefully:

- There are **exactly 4 slices spanning [0,2]** → CAPD integrated the whole
  window in **one step of size 2.0** (the dynamics are trivial, so the adaptive
  step controller maximized the step), and hull-grid 4 split that one step into 4
  sub-intervals of width **0.5**.
- The **`v` enclosures are exact** (`v(t)=1-t` is linear; e.g. `[0.5,1.0]` over
  `t∈[0,0.5]` is the true range).
- The **`x` enclosures are ~4× too wide.** Over `t∈[0.5,1.0]` the TRUE range of
  `x` is `[x(0.5), x(1)] = [0.375, 0.500]` (x is monotone up to the peak), but
  the enclosure is **`[0.25, 0.75]`** — width 0.5 vs true 0.125. Its lower bound
  0.25 < 0.3, so the violation at the peak is **missed** on every slice.

`x(t)=t-t²/2` is an exact degree-2 polynomial and the IC is a point, so an
order-12 (let alone order-20) Taylor method has ~zero remainder — the *true*
enclosure should be essentially `[0.375, 0.500]`. The 4× blow-up is not a
fundamental interval-arithmetic limit; it is the **dependency problem** in
evaluating the Taylor curve over a wide time sub-interval.

At hull-grid 16 the same step is cut into width-0.125 sub-intervals; `curve(sub)`
over a narrow interval is tight enough that the near-peak slice has lb > 0.3, and
the test passes. **It passes by luck of the step size, not by design.**

## Root cause

```
sub_interval_width  =  (CAPD adaptive step size)  /  kHullGrid      ← fixed COUNT
enclosure looseness ≈  O(sub_interval_width²)   (polynomial dependency)
```

`kHullGrid` is a fixed **count per step**, so the sub-interval width — and hence
the per-slice enclosure tightness — is at the mercy of CAPD's adaptive step
controller. A trivial flow that admits a huge step gets wide sub-intervals and
loose enclosures. The per-slice invariant check refutes only when some slice's
`x`-enclosure lower bound clears the constraint, so its **refutation power is
coupled to `(step_size / hull_count)`** — a quantity no one is bounding.

## Why this is a real problem (not just this test)

1. **Interior-invariant detection silently weakens** when steps are large — the
   F1 test is one instance; production flows with large steps over a sharp
   interior excursion could be under-refuted at hull-16 too.
2. **The whole tube is looser than CAPD's precision**, so terminal-gate
   intersection and time-narrowing are weaker than they should be **corpus-wide**.
   The sweep's "speedups with zero flips" were partly measuring *less of an
   already-too-loose computation* — i.e. the baseline itself is leaving precision
   (and possibly solve-power) on the table.
3. It makes `--ode-hull-grid` a **completeness-relevant knob (refutation power)
   disguised as a performance knob.** Lowering it for speed quietly buys missed
   refutations (false-`delta-sat`), never false-`unsat`.

### Completeness classification (the precise model-theory)

This is a **COMPLETENESS** failure, **not** SOUNDNESS — and writing the
T-relations out (the discipline `CLAUDE.md` mandates) makes that unmissable:

> **COMPLETENESS** (returns `delta-sat` / asserts φ^δ is *T-satisfiable* on a
> *T-unsatisfiable* φ — a missed refutation). It is **not** SOUNDNESS: lowering
> hull-grid only makes each sub-slice enclosure *wider* (still a sound outward
> over-approximation), so it can never wrongly refute a feasible instance — there
> is no path to a false-`unsat` (which would be asserting φ *T-unsatisfiable* on a
> *T-satisfiable* φ).

The per-slice filter's *entire purpose* is this refutation, and a violation with
margin 0.2 ≫ δ should never be missed — so the F1 regression is a first-class
**completeness gate**, even though it is not a soundness hole. Filing it as "HULL
SOUNDNESS" was the mislabel that read as a correctness emergency and cancelled
experiments; see `docs/soundness-vs-completeness.md`.

## Resolution (2026-06) — centered-in-time mean-value range

The fix that shipped is **not** the width-based subdivision sketched below. While
scoping that fix, reading CAPD's `Curve::operator()` (`diffAlgebra/Curve.hpp:59`)
showed the dependency blow-up is purely in the **time** argument: the doubleton
form already cancels the initial-condition spread (`xx ∩ (phi + jacPhi·deltaX)`),
but the polynomial in time is summed by naive interval Horner
(`xx[d] = xx[d]*h + coeff`) over the *whole* wide sub-interval `h`. CAPD also
exposes `timeDerivative(h)` (Curve.hpp:178), so a mean-value-in-time bound is
computable from the existing API:

```
mid       = scalar midpoint of sub
enclosure = curve(point(mid)) + timeDerivative(sub) · (sub − mid)   // mean-value
result    = enclosure ∩ curve(sub)                                  // never looser
```

`curve(point(mid))` is a thin-time evaluation (no time-widening); the correction
term is derivative-bounded and `O(r²)` in the sub radius. Both forms are sound
outward enclosures, so their intersection still encloses the true trajectory —
and intersecting with the naive result guarantees the tube is **never looser**
than before, hence **no soundness risk and no completeness loss on any flow**
(narrow-sub stiff flows where naive was already tight just keep the naive bound).
Implemented as `centered_curve_range()` in `contractor_odes_capd.cc`, applied to
both the tube `state` and the window-clipped `gate_state`.

**Why this beats the width-based sketch:** it tightens the *same* computation
instead of adding sub-slices, so it has no `h_max` scaling question, no knob type
change, and no extra cost on the small-step flows that width-based would have
left untouched while still paying its branch. It is the subtract-before-add
option the file's "Open questions" gestured at ("does `curve` have a tighter
range mode").

### Measured outcome

- **Completeness:** `GravityInvariantTest.FwdInteriorInvariantViolation_DefaultHull_BoxEmpties`
  (new test, default hull-grid, no pin) failed pre-fix (missed refutation) and
  passes post-fix. The pinned-16 mechanism test and the no-invariant SAT control
  still pass; full unit suite 643/643.
- **No correctness flips** in any benchmark sample (provably impossible — the
  tube only tightens).
- **Performance is mixed and net-positive**, matching the "depends on the corpus
  step-size distribution" prediction:
  - github (UNSAT-via-ODE-refutation, same-instance vs frozen baseline): ~0.19×
    PAR2 — tighter tube refutes faster. tacas: ~0.60×.
  - saradc (same-instance A/B, old vs new binary): **+~14% CPU**, identical
    `unsat` verdicts on the two instances that decide; the one apparent
    "timeout regression" (`k90_4b_box`) times out on the **old** binary too
    (pre-existing hard instance — the family-aggregate 4.2× was a sampling
    artifact: `--family saradc` drew harder k84/k90 instances than the baseline's
    k20–k70 pool).
  - odeexpr is NRA (no ODE), code path untouched → neutral.
- **Owner's-call lever (not taken):** the ~14% saradc cost is the extra
  curve/timeDerivative evaluations on flows where the mean-value form doesn't pay
  off (already-tight narrow sub-intervals). It could be clawed back by computing
  the mean-value form only when the sub-interval is wide, at the cost of one
  branch — deliberately *not* added (minimize-logic; the cost is modest and the
  clean uniform path is preferred unless the owner says otherwise).

---

## Proposed fix — SPECULATIVE (design sketch, NOT validated) — SUPERSEDED, see "Resolution" above

> ⚠️ **Treat this entire section as a hypothesis, not a spec.** It is reasoned
> from a *single* gravity slice-dump (one trivial flow) plus the standard
> interval-overestimation scaling argument — it has **not** been prototyped,
> measured, or checked against CAPD's actual `curve` internals. Any of it can be
> wrong in practice: the cost predictions, the `O(r²)` scaling, even "width
> subdivision is the right lever." **If you implement this and the measurements
> disagree, trust the measurements and rewrite this section — do not bend the
> code to match the sketch** (that would be the exact verifier-gaming trap that
> produced this file; see `dont-game-the-verifier.md`). Re-derive the mechanism
> on 2–3 *different* flows (not just gravity) before committing to a direction.

### Why the enclosure blows up (the hypothesised mechanism)

`curve(sub)` evaluates the step's Taylor polynomial (with interval coefficients)
over a time sub-interval. Over a **wide** sub-interval this loses variable
correlation — the interval **dependency problem**. For gravity `x(s)=s−s²/2`
over `s∈[0.5,1.0]` (true range `[0.375,0.5]`, width 0.125):

- naive monomial interval eval: `s − s²/2 = [0.5,1] − [0.125,0.5] = [0.0,0.875]`
  — 7× too wide, because the two occurrences of `s` are treated as independent;
- CAPD's *actual* dump enclosure was `[0.25,0.75]` — 4× too wide: better than
  naive (so CAPD uses some centered/doubleton form), but still loses correlation
  over a width-0.5 interval.

A degree-2 interval extension over-estimates by `O(r²)` in the sub-interval
radius `r`, so **narrowing the sub-interval shrinks the slop quadratically**:
hull-16's sub-width 0.125 is 4× narrower than hull-4's 0.5, hence ~16× tighter —
which is why hull-16 clears `lb > 0.3` and hull-4 does not. *(This `O(r²)` claim
is the textbook interval-extension argument applied to this polynomial; the exact
CAPD `curve` representation is **unverified** — confirm before relying on it.)*

### The change (bound the width, not the count)

`integrate_tube_slices_impl` (contractor_odes_capd.cc) currently splits each CAPD
step into a fixed COUNT `params.hull_grid`:

```cpp
const double dd = (d_hi - d_lo) / kHullGrid;          // sub-width = step / count
for (int k = 0; k < kHullGrid; ++k) { ... curve(sub) ... }
```

so the sub-width = `step_size / hull_grid` floats with the adaptive step size.
The sketch: bound the sub-width by a constant `h_max` instead —

```cpp
const int n_sub = std::max(1, (int)std::ceil(step_size / h_max));
const double dd = step_size / n_sub;                  // sub-width <= h_max always
```

so `curve(sub)` is always evaluated over a narrow interval regardless of the
controller. This is a **type change of the knob**: `Config::ode_hull_grid`
int→double, `CapdSolverParams::hull_grid` int→double, the `--ode-hull-grid` flag
a positive double (consider renaming to `--ode-hull-width`). The trace path
(`run_capd_trace_impl`, used only by `--visualize`) carries its own fixed
`n_steps` — lower priority, but give it the same treatment or document it as
deliberately count-based.

### The catch — this is in DIRECT TENSION with the adopted speedup

Part of the ~2× from hull-4 is simply doing **fewer** sub-slice evaluations per
step. The looseness only bites flows that take **large** adaptive steps
(trivial/non-stiff dynamics — gravity took ONE step of size 2.0); many-small-step
(stiff) flows already get narrow sub-intervals at any count. Width-based
subdivision **adds** sub-slices precisely on the large-step flows to reach
adequate resolution — i.e. it **claws back speed exactly where hull-4 won it**,
while leaving small-step flows ~unchanged. So the fix is **not free**: net speed
depends on the corpus step-size distribution and MUST be re-measured (full OFAT +
123). Plausible outcome: the corrected tube lands somewhere *between* the old
order-20/hull-16 and the current order-12/hull-4 on speed.

### Alternatives considered

- **Refine-on-demand for the invariant check ONLY** — keep the fast coarse tube
  for terminal narrowing, but when a slice's enclosure *straddles* an invariant
  boundary (`lb ≤ threshold ≤ ub`), subdivide just that slice (re-evaluate
  `curve` on halves) until it resolves or hits a floor. **Now arguably the
  preferred direction:** the owner already *accepted* the looser terminal tube
  (the completeness/speed tradeoff is fine), so the only thing worth restoring is
  detection robustness — and this does it **without** re-adding cost to the
  terminal path the width-based fix would slow. Narrower blast radius, keeps the
  speedup. Downside: a second code path for the invariant check.
- **Cap CAPD's step (`--ode-max-step`)** — a band-aid: fights the adaptive
  controller globally, hurt performance in the OFAT, and doesn't fix `curve`
  looseness within whatever step remains.
- **A tighter CAPD range API** — if `curve`/the doubleton offers a non-naive
  range bound (centered form, monotonicity test), that could cut the needed
  subdivision. Worth a look, but subdivision is robust to whatever `curve` does.

## Current state / decision

> **SUPERSEDED (2026-06) — see the Resolution section at the top.** This section
> and everything below it are the pre-fix snapshot, kept for the historical
> record. The centered-in-time (mean-value) range fix shipped: the ~4× looseness
> defect is closed, the F1 case refutes at the default hull-grid, and hull-grid
> is no longer completeness-load-bearing. The width-based sub-slicing sketched
> below was **not** needed and was not built. The only remaining follow-up is
> the optional knob re-tune on the tightened tube (tracked in OPTIMIZATION_LOG.md).

After the soundness-vs-completeness distinction was clarified (this is a
**completeness** tradeoff — missed refutation / false-`delta-sat` — never a
false-`unsat`), the owner **accepted the tradeoff** and the faster default was
**adopted**:

- **Default = forward order 12 / backward order 12 / hull-grid 4** (123-confirm:
  ~2× faster, PAR2 0.49, +4 solved, zero SAT↔UNSAT flips; bwd-12 adds ~5% over
  bwd-20). `config.h` carries the soundness/completeness note + a pointer to this file.
- The F1 regression test (`GravityInvariantTest.FwdInteriorInvariantViolation`)
  is **pinned to hull-grid 16** so it still guards the per-slice *mechanism*
  (interior violations ARE refuted at adequate resolution); it intentionally
  does **not** assert the hull-4 default catches this sub-resolution sharp case —
  the accepted completeness limit, documented here (the owner chose not to add a
  separate limitation test).
- All Phase-1 runtime-flag plumbing stays; the `--ode-*` flags let any workload
  override (e.g. order 16–20 + hull 16 for refutation-critical / sharp-invariant
  problems).

**The looseness defect itself is still open** (independent of the accepted
tradeoff): the tube is ~4× looser than CAPD's precision allows. The width-based
sub-slicing fix below remains the proper next step — it would recover the lost
refutation precision (and likely tighten narrowing corpus-wide) **while keeping
the speed**, after which the default would detect the F1 case too and hull-grid
would stop being completeness-relevant. Tracked as a follow-up.

## Reproduce

```bash
# hull-4 is now the DEFAULT, but the F1 test pins hull-16 internally so it passes.
# To SEE the sub-resolution miss, drop the `set_from_command_line(16)` hull pin in
# the FwdInteriorInvariantViolation test (so it uses the hull-4 default), rebuild:
cmake --build gcc_build --target dreal4_cmake_test -j8
gcc_build/dreal4_cmake_test --gtest_filter='GravityInvariantTest.*'   # interior-violation test then fails to empty at hull 4, passes at >= 8
# slice dump: re-add the DREAL_DEBUG_INV fprintf in the invariant loop of
# contractor_odes.cc (removed after this investigation) and re-run.
```

## Open questions for the fix (resolve these BEFORE committing to a direction)

- **Width-based vs refine-on-demand** — which to build? Given the owner accepted
  the looser terminal tube, refine-on-demand (detection-only) may keep the
  speedup that width-based would partly spend. Decide based on whether anything
  *other than* interior-detection actually needs the tighter tube (does the loose
  terminal tube measurably cost solves on the 123?).
- **Confirm the mechanism on ≥2 non-gravity flows.** The `O(r²)` / dependency
  story is from one trivial polynomial flow; verify a stiff and a transcendental
  flow behave the same before trusting the scaling.
- **`h_max` units/scaling.** Absolute time, or relative to the step, or to
  `t_ub`? Horizons span ~1 to ~20+ across the corpus, so a single absolute
  constant may over-resolve short flows and under-resolve long ones.
- **Does `capd::...Curve::operator()` have a tighter-than-naive range mode** that
  would reduce (or remove) the needed subdivision?
- **Re-measure cost vs speed.** Width-based adds work on large-step flows — run
  the full OFAT + 123 on the corrected tube; the tighter tube may shift every
  knob's effect and could itself change solve counts. Do NOT re-pick a default
  from the loose-tube numbers in this campaign.
