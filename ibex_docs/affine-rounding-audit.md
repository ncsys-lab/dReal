# Affine plugin rounding-soundness audit — fAF2 `_err` under `FE_UPWARD`

**Verdict: SOUND-WITH-SCOPE-WRAP** (fine-grained: wrap the seven fAF2 EFT blocks in
`FE_TONEAREST`; do **not** wrap `linearize`/`eval`/`contract` wholesale — that breaks gaol),
**plus one co-requisite non-rounding fix** in `linearize`'s row emission (D2 below) that a
scope wrap does not touch. Details and obligations in §5.

Audited sources (read completely, line references are to these revisions):

| Source | Revision |
|---|---|
| `plugins/affine/src/arithmetic/ibex_Affine2_fAF2.{h,cpp}` | `ibex-fork` `origin/dev_affine_arith` (tip `0e4a8428`) |
| `plugins/affine/src/arithmetic/ibex_Affine.h`, `ibex_AffineVector.h` | same |
| `plugins/affine/src/function/ibex_AffineEval.h` | same |
| `plugins/affine/src/numeric/ibex_LinearizerAffine2.cpp` | same (vendored copy on `task/affine-vendor` diffs only by the `tolerance()` drift fix + comments; `fAF2.cpp` vendored copy adds a 13-line memory fix inside `AffineVarMain::operator=(Interval)` — line numbers below that point shift by +13) |
| `src/contractor/ibex_CtcPolytopeHull.cpp` | `ibex-fork` `dreal-perf-patches` (`e054af7b`, the built pin) |
| `src/dreal/contractor/contractor_ibex_polytope.cc`, `docs/rounding.md` | this repo, `tech-debt-fixes` |

Settles the disagreement recorded in [`RELATED-WORK.md` §"The one soundness
obligation"](RELATED-WORK.md) (line ~104): pass-A ("`itv()`'s outward wrapping dominates")
is **refuted** — §2.4; pass-B ("`_err` can under-estimate under `FE_UPWARD`") is
**confirmed at the kernel level with a hand-verifiable counterexample**, but its magnitude
is now established as second-order (§3), which is what makes the scope-wrap fix sufficient
for the EFT question.

---

## 1. Question + stakes

dReal's polytope Prune asserts `FE_UPWARD` for the whole call
(`src/dreal/contractor/contractor_ibex_polytope.cc:126-135`; phase-hoisted per
`docs/rounding.md`). `CtcPolytopeHull::contract` → `Linearizer::linearize` runs inside that
scope. The candidate `LinearizerAffine2` evaluates every constraint in fAF2 affine
arithmetic, whose error term `_err` is maintained with Dekker/Knuth error-free transforms
(`twoSum`/`twoProd`, `ibex_Affine2_fAF2.h:106-146`) — algorithms whose exactness proofs
assume `FE_TONEAREST`.

Two distinct failure channels if `_err` under-estimates the true rounding error:

1. **LP row channel.** `linearize` emits rows whose right-hand side is built from
   `af2.err()` (`ibex_LinearizerAffine2.cpp:205/216/226-228`). An under-covering row fails
   to over-approximate the constraint; `CtcPolytopeHull::optimizer` then shrinks a bound
   past a true model or proves the LP infeasible
   (`ibex_CtcPolytopeHull.cpp:136/154/190/209` → `box.set_empty()`). The LP runs in
   `LPSolver::Mode::Certified` (`ibex_CtcPolytopeHull.cpp:27`), but certification treats
   the rows **as data** — it guards LP-solver numerics, not row generation.
2. **Interval-refutation channel.** `AffineEval` intersects the interval domain with the
   affine range at **every node**: `d[y].i() = (af2[y].i().itv() & (d[x1].i()+d[x2].i()))`
   (`ibex_AffineEval.h:258` and siblings). `linearize`'s `ev` is that intersected
   enclosure, and `ev.lb()/ev.ub()` sign tests `return -1`
   (`ibex_LinearizerAffine2.cpp:200/202/211/213/222`), which
   `CtcPolytopeHull::contract` converts directly to `box.set_empty()`
   (`ibex_CtcPolytopeHull.cpp:69→81`). An under-covering affine form narrows `ev` and can
   refute a satisfiable constraint **without the LP ever running**.

Both channels are **SOUNDNESS (asserts φ T-unsatisfiable on a T-satisfiable φ — false
unsat)** — the forbidden direction. Everything below is graded against that bar.

---

## 2. Per-site catalogue — every `_err` update

Classes: **(i)** EFT-identity-dependent (twoSum/twoProd correctness needed);
**(ii)** directed-rounding-safe (gaol interval arithmetic or stored-value reads; requires
the ambient `FE_UPWARD` dReal already provides); **(iii)** other.

### 2.1 fAF2 kernels (`ibex_Affine2_fAF2.h`)

| Site | Lines | Class | Finding |
|---|---|---|---|
| `twoSum(a,b,*res)` | 137-144 | (i) | Møller–Knuth branchless 2Sum. Under `FE_TONEAREST`: `res + t = a + b` exactly. Under `FE_UPWARD`: **not exact**; `t` can be `0` while the true residual is nonzero (§3 counterexample). Empirical deficit bound: `|r|−|t| ≤ 0.5·2⁻¹⁰⁴·|s|` (second-order). |
| `twoProd` FMA path | 109-114 | (i) | `fma(x,y,−r₁)` where `r₁ = fl(x·y)`. The product residual is exactly representable for **any** faithful rounding of `r₁` (no underflow), and rounding a representable value is the identity in every mode ⇒ **exact under `FE_UPWARD` too**, provided `|x·y| ≳ 2⁻⁹⁶⁹`. Empirically confirmed (probe tracks the **max** `log₂|xy|` and max `\|t−ref\|` over all mismatches, not samples): 0 mismatches / 20 M samples at exponent spread ±200; at spread ±500 all 3 793 mismatches had `log₂|xy| ≤ −969.2` (consistent with the 2⁻⁹⁶⁹ representability bound), every one an under-estimate, max deficit exactly 1 subnormal-ulp = 2⁻¹⁰⁷⁴ (D5, mode-independent in kind). |
| `twoProd` Dekker path (no `IBEX_FMA`) | 115-131 (+ `Split` 100-107) | (i) | Veltkamp split + Dekker product; the split's bit-count property is proven under round-to-nearest only. Under `FE_UPWARD`: **unproven, potentially first-order wrong** (an inexact `x_high·y_high` alone is ~2⁻⁵²·\|xy\| — the same order as the residual being measured). macOS arm64 clang defines `__FP_FAST_FMA` ⇒ FMA path; Linux x86-64 gcc without `-mfma` compiles the Dekker path. // INTEGRATION-VERIFY: which path the Docker verification build takes. Discharged by the wrap (Dekker is proven under nearest). |

### 2.2 `AffineMain<AF_fAF2>` methods (`ibex_Affine2_fAF2.cpp`)

All class-(i) sites share one accounting scheme: each EFT residual `eee` is accumulated as
`ttt = (1+2·AF_EM)·(ttt+fabs(eee))`, dropped-coefficient mass as `sss`, and folded into
`_err` with factor `AF_EE·ttt` — constants `AF_EM = 2⁻⁵¹`, `AF_EC = 2⁻⁵⁵`, **`AF_EE = 2.0`**
(`ibex_Affine.h:689-691`). So the scheme carries a 2× margin on the residuals plus
`(1+2⁻⁵⁰)` inflation per accumulation step. Under `FE_UPWARD` the accounting *arithmetic*
itself rounds toward +∞ — the safe direction; the only vulnerability is the `eee` values.

| Site | Lines | Class | Finding |
|---|---|---|---|
| `operator=(const Interval&)` | 26-64 | (ii) | `_val[0]=x.mid()`, `_err=x.rad()`. gaol `rad()` (`dreal-perf-patches:src/arithmetic/ibex_Interval.h:931-940`) computes `max((t−*this).ub(), (*this−t).ub())` **via interval subtraction** ⇒ under `FE_UPWARD`, `rad ≥ max(mid−lb, ub−mid)` in real arithmetic ⇒ `[mid−rad, mid+rad] ⊇ x` for *any* `mid`. Sound; **must stay under `FE_UPWARD`** (under nearest this guarantee dies). |
| `AffineMain(int,int,const Interval&)` ctor | 70-87 | (ii) | The variable-load path used by `AffineMainVector(const IntervalVector&)` (`ibex_AffineVector.h`, used by `AffineEval::forward(box)`); same `mid()/rad()` argument; `_err` starts 0. Sound under `FE_UPWARD`; do not wrap. |
| `AffineVarMain::operator=(const Interval&)` | 91-135 | (ii) | Same accessors. Latent (vendor commit's own NOTE): neither branch resets `_elt._err`, so a stale — possibly **negative** (`_n=-3/-4` store a *bound* in `_err`) — value survives re-assignment; a negative `_err` enters later sums additively and under-accounts. **Unreachable via the linearizer** (`AffineEval` constructs fresh `AffineMainVector(box)` per eval, which uses the ctor path), but fix or assert at vendor time (D7). |
| `itv()` | 214-234 | (ii) | Interval conversion `res += _val[i]·[−1,1]; res += _err·[−1,1]` — gaol ops, outward under `FE_UPWARD`. **This is where pass-A's argument fails:** `itv()` rigorously encloses *the stored form* `⟨c₀,cᵢ,_err⟩`. If `_err` itself fails to cover `f`'s distance from the linear part, `itv()` faithfully wraps the **wrong set** — outward rounding of the conversion cannot repair an under-accounted `_err`. Moreover the LP row channel reads `val(i)/err()` directly and never passes through `itv()` at all. |
| `operator*=(double)` | 361-397 | **(i)** | twoProd per coefficient (:373), accounting :375-382. |
| `operator+=(const AffineMain&)` | 424-475 | **(i)** | twoSum per coefficient (:436), `_err` update :447. The §3 counterexample maps 1:1 onto :436 with `_err = 0` prior ⇒ post-op `_err` misses the true residual entirely. |
| `operator+=(double)` | 479-503 | **(i)** | twoSum on the center (:484), `_err` :494. |
| `inflate(double)` | 507-528 | **(i)** | twoSum on `_err` itself (:512). |
| `operator*=(const AffineMain&)` | 532-687 | **(i)** | Dense twoProd/twoSum chains (:549-649), `_err` :652-660. Contains the one *subtractive* term `(1−2·AF_EM)·(−0.5)·Sxy` (:656) — the ε² recentering credit. Checked: under `FE_UPWARD` the computed `Sxy` can over-estimate `Σ|xᵢyᵢ|` only by amounts whose residuals were simultaneously added to `ttt` with factor `AF_EE = 2` ⇒ net over-coverage; the `(1−2·AF_EM)` de-rating and the upward-rounded (less-negative) product keep the subtraction conservative. Safe **given** the `eee` values, i.e. reduces to the kernel question. |
| `operator*=(const Interval&)` | 693-705 | (ii)+(i) | `y.mid()/y.rad()` (gaol, keep upward) then delegates to `inflate` + `operator*=(Affine)`. |
| `Asqr(const Interval&)` | 711-814 | **(i)** | Same shape as `*=`(Affine), `_err` :791-798, same `−0.5·Sx2` structure — same analysis. |
| `compact(double)` | 818-835 | **(i)** | twoSum folding dropped coefficients into `_err` (:823). |

### 2.3 Nonlinear linearizations (`ibex_Affine.h`)

`Ainv/Asqrt/Aexp/Alog` (`_CH` :1220-1374, `_MR` :2258-2407), `Acos/Asin/Atan`
(:1386-1631), `Aacos/Aasin/Aatan` (:1633-1837), `Acosh/Asinh/Atanh` (:1839-2014), `Aabs`
(:2016-2058), `Apow/Aroot` (:2063-2226) — all one pattern:

- `alpha`, `beta` computed in **plain doubles** (libm calls, ambient rounding). Their
  *values are soundness-irrelevant*: any slope/offset is admissible because…
- `ddelta` is computed **by interval arithmetic with the as-computed `alpha`/`beta`**
  (endpoint deviations + interior stationary-point deviations, e.g. `Aexp_CH`'s band floor
  `(alpha·(1−log(Interval(alpha)))).lb()` is the *global* minimum of `exp(x)−αx`), so the
  bound is rigorous for whatever α,β came out — class (ii), needs `FE_UPWARD`. The
  candidate-point enumeration (endpoints + every real stationary point, ±2 periods for
  trig) is a real-analysis completeness argument, rounding-independent.
- Application is `*this *= alpha; *this += beta; inflate(ddelta)` — delegates to the
  class-(i) sites of §2.2.

One corner (D6, pre-existing, **mode-independent**): `Aabs` computes
`alpha = (…/itv.diam()).ub()` (:2033) which can round to `> 1`; its band floor is pinned
at `0.0` (:2039/:2042), but for `α > 1` the true deviation `|x|−αx` dips below 0 by up to
`(α−1)·mag(itv) ≈ 2⁻⁵¹·mag(itv)` — a second-order under-coverage present under nearest
too. Same order as the EFT issue; folded into the D2 fix's slack floor or accepted.

> **Correction (2026-07-22, found during implementation — fork commit `e247288f`):** the
> §2.3 premise that `Aabs`'s `ddelta` is built from *endpoint deviations* was wrong for
> the ceiling term: `TEMP1` used `res_itv.lb()` (= 0 on a straddling interval), not the
> deviation at `x = lb`, so the band ceiling relied on `TEMP2` alone and under-covered
> `dev(lb) = |lb|·(1+α)` by `(α−α*)·(ub+|lb|)` whenever the upward-rounded α exceeds the
> exact chord slope — second-order, present under nearest too, and NOT covered by the D5
> absolute floor (it is a *relative* miss). Fixed alongside D6 in `e247288f`
> (`TEMP1 = abs(Interval(itv.lb())) − α·Interval(itv.lb())`; α additionally clamped to
> ≤ 1, the clamp being the sound D6 resolution since the D5 floor cannot cover a
> `2⁻⁵¹·mag(itv)` relative term).

### 2.4 `AffineEval` (`ibex_AffineEval.h`)

Every scalar `*_fwd` interleaves one affine op (class (i) inside) with gaol ops
(`itv()`, `&`, interval `+`/`*`/`exp`/…) **in the same function body** (:256-441). Two
consequences: (a) a *whole-function* rounding wrap can never satisfy both regimes — the
wrap must live inside the fAF2 methods; (b) the `d[]` intersection makes interval-eval
soundness depend on affine-form validity (channel 2 of §1). `max/min/atan2/acosh/asinh/
atanh` fall back to pure interval (sound, no affine tightening).

---

## 3. The kernel result — what actually breaks under `FE_UPWARD`

### 3.1 Hand-verifiable counterexample (twoSum, `t = 0` while `r ≠ 0`)

Inputs `a = −0x1.1b9e462499d4cp+60`, `b = −0x1.4c7a833d7b36ep−55` (found by random
search, verified by hand; any `a,b<0` with `|b| ≪ ulp(ulp(a))`-structure works):

```
s  = RU(a+b)   = a                    (a+b < a; toward +∞ picks a)      r = (a+b)−s = b ≠ 0
a2 = RU(s−b)   = a + ulp(a)           (a+|b| rounds up a full ulp)
b2 = RU(s−a2)  = −ulp(a)              (exact)
da = RU(a−a2)  = −ulp(a)              (exact)
db = RU(b−b2)  = RU(ulp(a)−|b|) = ulp(a)   (|b| ≪ ulp(ulp(a)); rounds up)
t  = RU(da+db) = 0
```

The computed residual is `0`; the true residual is `b`. `AF_EE = 2` absorbs nothing times
zero. Mapped into `operator+=(const AffineMain&)`:436 with `_err = 0` beforehand, the
post-op `_err` fails to cover a true deviation of `|b| ≈ 2⁻¹¹⁵·|s|` — **the pass-B claim
is correct: `_err` can under-estimate under `FE_UPWARD`.**

### 3.2 Magnitude (empirical + derivation sketch)

Probe (20 M random pairs across ±60 exponent spread + ~34 k adversarial structured pairs;
kernels transcribed verbatim from `ibex_Affine2_fAF2.h`; exact references via
nearest-mode 2Sum/FMA, which are exact):

| Metric | Result |
|---|---|
| violations of `2·\|t\| ≥ \|r\|` (the accounting's margin) | **82 169** of 19.4 M inexact sums |
| worst `2\|t\|/\|r\|` | **0** (complete miss, §3.1 family) |
| worst deficit `(\|r\|−\|t\|)/(2⁻⁵²·\|s\|)` | 1.1·10⁻¹⁶ ≈ 2⁻⁵³ |
| worst deficit `(\|r\|−\|t\|)/(2⁻¹⁰⁴·\|s\|)` | **0.5** |
| twoProd-FMA mismatches, products ≥ 2⁻⁴⁰⁰ | 0 / 20 M |
| twoProd-FMA mismatches, spread ±500 (max `log₂|xy|` = −969.2) | 3 793 / 20 M, **all** under-estimates, max deficit = 1 subnormal-ulp |

So: the under-estimation is real but **bounded by ~2⁻¹⁰⁵·|s| absolute** — it occurs
exactly when the true residual is itself below ~2⁻¹⁰⁴·|s|, and when the residual is larger
`t` tracks it within relative 2⁻⁵². Derivation sketch of why (not machine-checked;
corroborated by the sweep): when the internal subtractions are exact,
`da+db = r − η` with `η ≥ 0` the same sign as `r`, so `|t| ≥ (|r|+η)(1−2⁻⁵²)`; the miss
comes only from the final rounding and from `η`, both `O(ulp(ulp(s)))`. When a subtraction
is inexact its operand is already `O(ulp(s))`, so its error is again second-order.
(Published analyses of 2Sum under directed rounding exist — Boldo/Graillat/Muller, TOMS
2017 — **not verified here**; nothing below relies on them.)

**Consequence.** Per class-(i) op, `_err` can miss up to `~2⁻¹⁰⁵·|coefficient scale|`.
There is no absolute-slack term anywhere in fAF2 to absorb it, so a formal false-unsat
channel exists (a true model within `~#ops·2⁻¹⁰⁵·scale` of a constraint boundary can be
cut — **SOUNDNESS (asserts φ T-unsatisfiable on a T-satisfiable φ — false unsat)**,
astronomically thin but the bar here is formal). And no clean domination argument closes
it: intermediate coefficients are unbounded relative to emitted row coefficients
(cancellation), so "the 2⁻⁵⁰ emission slack dwarfs it" is a heuristic, not a proof.
**SOUND-AS-IS is therefore not provable — refuted at the kernel, unclosable at the
accounting.**

---

## 4. `linearize` emission audit (`ibex_LinearizerAffine2.cpp:155-241`)

Setup per constraint: affine validity means ∀x∈box, `f(x) = c₀ + Σcᵢεᵢ(x) + e·θ(x)`,
`θ∈[−1,1]`, `εᵢ = (xᵢ−mᵢ)/rᵢ ∈ [−1,1]` (sound var load, §2.2), with `cᵢ = af2.val(i)`,
`c₀ = af2.mid()` (stored-value reads for an actif form — no gaol arithmetic), `e =
af2.err()`. Emission (`:183-196`): `aᵢ = fl(cᵢ/rᵢ)` **plain double**; `center` is an
`Interval` accumulating **plain-double products** `fl(aᵢ·mᵢ)`; `err` accumulates
`|aᵢ|·2⁻⁵⁰` (exact — power-of-two multiply). `rᵢ = 0` handled by `b_abort` (row dropped
unless `cᵢ = 0` — sound by omission). LEQ row (`:205`): `Σaᵢxᵢ ≤ ub((e+err)−(c₀−center))`.

Worked LEQ soundness chain (`f(x) ≤ 0` ⇒ `Σcᵢεᵢ ≤ e−c₀`):

```
Σaᵢxᵢ = Σaᵢmᵢ + Σaᵢrᵢεᵢ = Σaᵢmᵢ + Σcᵢεᵢ + Σδᵢrᵢεᵢ ,   δᵢ := aᵢ − cᵢ/rᵢ, |δᵢ| ≤ 2⁻⁵¹·⁹|aᵢ|
Σaᵢmᵢ ≤ Σ fl_up(aᵢmᵢ) ≤ center.ub()        (FE_UPWARD: plain-double product rounds up — safe HERE)
⇒ need   Σ|aᵢ|·2⁻⁵⁰  ≥  Σ|δᵢ|rᵢ            ⇔  per-i  rᵢ ≲ 3.7
```

For the GEQ row (`:216`) the center leak flips direction (`center.lb()` can exceed
`Σaᵢmᵢ` by up to `Σ2⁻⁵²|aᵢmᵢ|` because `fl_up ≥` true), adding a `|mᵢ|` term:
sufficiency needs `|mᵢ| + rᵢ ≲ 4`. EQ (`:226-228`) emits both rows ⇒ same condition.

**D2 (first-order vs. slack, mode-independent in kind):** the fixed `2⁻⁵⁰` slack proves
the rows sound only for boxes with `rᵢ ≲ 4` (LEQ) / `|mᵢ|+rᵢ ≲ 4` (GEQ/EQ). Beyond that
the row can over-cut by up to `~2⁻⁵²·Σ|aᵢ|(|mᵢ|+rᵢ) − 2⁻⁵⁰·Σ|aᵢ|` — e.g. a `[−100,100]ⁿ`
box leaves a `~3·10⁻¹⁴`-relative unsound sliver per face — **SOUNDNESS (asserts φ
T-unsatisfiable on a T-satisfiable φ — false unsat)**, independent of the EFT question and
**not fixed by any rounding-scope change**. This sits exactly on the authors' own
`// TODO TO CHECK` lines (the dead `inlinearization` copy; the live `linearize` rows are
the same construction). Also in this class: `aᵢ` overflow → `LPException` → row skipped
(sound); `cᵢ/rᵢ` underflow to 0 drops `|cᵢ| ≤ 2·2⁻¹⁰²²·rᵢ` with zero slack (dust, but
formally same family).

The `ev`-based `return -1` refutations (`:200/:202/:211/:213/:222`) are sound *given* a
valid affine form and gaol under `FE_UPWARD` (e.g. `LEQ: 0.0 < ev.lb()` ⇒ `f(x) ≥ lb > 0`
∀x ⇒ infeasible); their entire risk load is channel 2 of §1.

**Where arithmetic happens vs. where data flows** (the scope-wrap validity argument): an
emitted row is a `(Vector, double)` pair — pure data; its validity is a statement about
real numbers. Consumption under `FE_UPWARD` afterwards (LP solve + certified bounds +
`box.set_empty()`) is sound *because each piece of arithmetic runs under the mode its own
proof assumes*: EFT blocks under `FE_TONEAREST` (their design regime), every gaol op —
var load `mid()/rad()`, `itv()`, `ddelta` bands, `center/err/rhs` interval arithmetic, LP
certification — under `FE_UPWARD` (its regime; `docs/rounding.md`: gaol under any other
mode *inverts* directed rounding, first-order catastrophic). No value's meaning changes at
a mode boundary; only operations care.

---

## 5. Verdict + implementation obligations

**SOUND-WITH-SCOPE-WRAP**, with the wrap at exactly this granularity:

- **Wrap in `FE_TONEAREST`** (save-entering-mode/restore RAII, local to the vendored ibex
  tree — dReal's `NearestRoundingScope` is not linkable there) **the straight-line EFT
  general-case blocks of the seven class-(i) sites** in `ibex_Affine2_fAF2.cpp`:
  `operator*=(double)` (finite-α branch), `operator+=(const AffineMain&)` (both-actif
  branch, including `resize`), `operator+=(double)` (actif branch), `inflate(double)`
  (actif branch), `operator*=(const AffineMain&)` (both-actif branch), `Asqr` (general
  branch), `compact(double)` (loop body). The `*this = Interval::ALL_REALS` overflow
  resets inside those blocks are safe to include (that assignment path does no gaol
  arithmetic); the `itv()`-based fallback branches are **outside** the wrap.
- **Do not wrap** — must stay under ambient `FE_UPWARD`: `AffineMain(size,var,itv)` ctor,
  both `operator=(const Interval&)`s, `AffineVarMain::operator=` (gaol `mid()/rad()`
  soundness *requires* upward, §2.2), `itv()`, all of `Affine.h`'s nonlinear band/`ddelta`
  computation, all of `AffineEval`, all of `linearize`, all of `CtcPolytopeHull`. A
  wholesale `NearestRoundingScope` around `linearize` would replace a 2⁻¹⁰⁵-scale defect
  with a first-order gaol-inversion false-unsat generator (`docs/rounding.md` regime
  table) — the single most dangerous "obvious fix" here.
- **Why the wrap discharges the EFT question:** under `FE_TONEAREST` the EFTs are exact
  (twoSum unconditionally; twoProd modulo D5 underflow), which is the regime fAF2's
  `(1+2·AF_EM)`-inflation accounting was designed and published for; the accounting's own
  nearest-mode rounding is covered by those factors (`1+2⁻⁵⁰` per step vs. `(1+2⁻⁵³)²`
  needed). The wrap also **discharges the Dekker/non-FMA obligation** (proven under
  nearest), removing the platform `-mfma` question.
- **Exception safety:** the wrapped blocks allocate (`resize`, `new double[]` in
  `operator*=`) and can throw `std::bad_alloc`; the guard must be RAII (restore in dtor),
  restoring the *saved* entering mode, not hardcoding `FE_UPWARD`. dReal's
  `RoundingModeGuard` dtor-tripwire tolerates this pattern (mode is restored on unwind).

**Residual obligations (the wrap is necessary, not sufficient):**

1. **D2 — fix the emission** (co-requisite; first-order vs. slack; a rounding wrap cannot
   touch it). Replace the three plain-double leaks in `linearize` with gaol-enclosed
   arithmetic under the ambient `FE_UPWARD`: enclose `cᵢ/rᵢ` as
   `Interval(cᵢ)/Interval(rᵢ)` (take any interior double as `rowconst[i]`, add the
   enclosure's distance ×`rᵢ` — i.e. `mag(Interval(rowconst[i])·Interval(rᵢ) −
   Interval(cᵢ))` — to `err`), and accumulate `center += Interval(rowconst[i]) ·
   Interval(mᵢ)`. This makes the rows sound for **all** box geometries and retires the
   magic `2⁻⁵⁰`. Sound interim while unfixed: `b_abort` any row with `|mᵢ|+rᵢ > 4`.
2. **D5 — subnormal-residual dust** (mode-independent, survives the wrap): twoProd's
   residual is lost when `|x·y| ≲ 2⁻⁹⁶⁹` (≤ 2⁻¹⁰⁷⁴/op). Shared with upstream-under-
   nearest. Discharge by folding an absolute slack floor (e.g. `#ops·2⁻¹⁰⁰⁰`-scale, or
   simply a per-constraint `err += [tiny]`) into the D2 fix, or record explicit owner
   acceptance. Do not leave it undocumented.
3. **D6/D7 — opportunistic:** `Aabs` α>1 band-floor corner (§2.3; cover via the D2 slack
   floor or clamp α into `[−1,1]` with the deviation re-measured); `AffineVarMain::
   operator=` stale/negative `_err` (unreachable via the linearizer; assert or zero it at
   vendor time — the vendor commit already flags it).
4. **Performance:** 2 `fesetround`s per wrapped op (the mode genuinely differs each time,
   so check-before-set never saves the write; ARM64 MSR FPCR is pipeline-serializing).
   Alternative if hot: hoist to one nearest-window per `AffineEval::*_fwd` node (the
   affine op line precedes the gaol lines in every `_fwd`). Measure via `/benchmark`; not
   a soundness question.
5. **Competing design worth pricing:** `plugins/affine-extended`'s `AF_iAF` backend keeps
   `_err` in **interval arithmetic** — no EFTs at all, mode-consistent with gaol under
   ambient `FE_UPWARD`, dissolving the wrap and this entire class of audit. Cost:
   interval ops per coefficient (~2× dataflow) and vendoring the less-maintained extended
   plugin. `AF_Default` is a typedef (`ibex_Affine.h:58`); `LinearizerAffine2` follows it.

**What would upgrade the verdict:** a machine-checked proof that every class-(i) miss is
`≤ K·2⁻¹⁰⁴·|s|` *plus* a per-op absolute inflation of `_err` by that bound would make a
patched SOUND-AS-IS available — rejected here as strictly more bespoke logic than
restoring the code's native proof regime (and it still would not remove obligations 1-3).

---

## 6. Tripwire unit test (what it must assert)

TDD-first; expected first failure is a **compile error naming the ported symbols**
(`LinearizerAffine2` / the dReal-side contractor wrapper), then behavioral. All layers run
inside an `UpwardRoundingScope` — the phase mode the production call inherits
(`contractor_ibex_polytope.cc:126-135`).

1. **Kernel layer (deterministic, catches an unwrapped EFT directly).** Feed the §3.1
   family through the vendored affine ops: build 1-var forms and execute
   `operator+=(AffineMain)` with coefficient pairs
   `(a,b) = (−0x1.1b9e462499d4cp+60, −0x1.4c7a833d7b36ep−55)` (and the parametric family
   `a = ±2ʲ`, `b = ±(2^{j−k}+2^{j−k−52})`, `k ∈ [1,70]`); assert
   `result.itv() ∋ a+b` computed exactly (nearest 2Sum in the test gives the exact pair).
   Under the wrap this holds by EFT exactness; unwrapped, the §3.1 inputs make `_err`
   miss the residual and the containment check fails at the 2⁻¹⁰⁵ scale — assert with
   zero tolerance. // INTEGRATION-VERIFY: containment is checked via itv() endpoints;
   confirm the chosen family drives |r| above itv()'s own outward-rounding grain so the
   miss is observable.
2. **Eval-containment fuzz (channel 2).** For cancellation-heavy expressions (expanded
   `(x−1)⁷`; `x·y − x·y` variants; deep `+=`/`*=` chains) over randomized boxes:
   `Affine2Eval::eval(box)` then assert the intersected `d.top` interval and `af2.itv()`
   contain `f` evaluated at sampled points (midpoint, endpoints, random interior)
   computed in plain nearest-mode doubles bracketed by an outward ulp — soundness of the
   enclosure is the invariant, and it is exactly what the `return -1` refutation path
   consumes.
3. **End-to-end interior-solution retention.** `CtcPolytopeHull(LinearizerAffine2)` (and
   the dReal contractor wrapper once wired) on constraint sets with *known feasible
   witnesses* — including witnesses within `10·δ` of a constraint boundary and a
   large-box instance (`[−100,100]ⁿ`, exercising D2): after `contract(box)`, assert every
   witness is still in `box` and `box` is nonempty. Include a gaol-inversion canary: a
   constraint with the inexactly-representable constant `0.1` whose feasible box must
   survive — this is the test that fails loudly if anyone ever "fixes" rounding by
   wrapping `linearize` wholesale in `FE_TONEAREST` (cf.
   `test/dreal/api/test/gaol_directed_rounding_false_unsat_test.cc` for the pattern).
4. **Completeness canary (not soundness):** one instance where the affine relaxation is
   known to tighten (correlated occurrences, e.g. `x − x` structure) — assert the box
   actually shrinks. Guards against a silently-dead linearizer (e.g. `af2.size() !=
   nb_var` aborting every row), which would pass every soundness assert while doing
   nothing. Label: **COMPLETENESS (asserts φ^δ T-satisfiable on a T-unsatisfiable φ —
   missed refutation)** is the failure mode of a dead/loose relaxation, never false
   `unsat`.

---

*Probe source (twoSum/twoProd transcriptions + exact references) lives in the session
scratchpad (`eft_upward_test.c`, `prod_isolate.c`); the §3.1 counterexample and the
adversarial family are reproduced above in full, so the result is re-derivable without
them.*
