# `LinearizerXTaylor` — corner-based interval Taylor relaxation

> ✅ **Polytope path is SHIPPED** (was mislabeled "dormant" pre-`fa3b74bd7`; corrected 2026-07-02):
> this linearizer feeds `--polytope`, which is live because IBEX builds `-DLP_LIB=soplex`
> (`CMakeLists.txt`, `LP_LIB=soplex`). Any "`--polytope` off / `LP_LIB=none` / knobs
> unexplored" text is WRONG. See [`README.md`](../../README.md) top banner.

Header:
[`ibex_LinearizerXTaylor.h`](../../../../ibex-fork/src/numeric/ibex_LinearizerXTaylor.h)
(`numeric/`). `class LinearizerXTaylor : public Linearizer`. The linear relaxation
dReal feeds to [`CtcPolytopeHull`](../contractors/CtcPolytopeHull.md) (Araya,
Trombettoni, Neveu, CPAIOR 2012).

> **dReal status:** constructed at `contractor_ibex_polytope.cc:108-109` with
> `(RELAX, RANDOM_OPP, HANSEN)` — the header defaults, passed explicitly. The
> polytope path is **live** (`--polytope`, default off; `LP_LIB=soplex`). The
> `approx_mode`/`corner_policy`/`slope_formula` knobs are **not** exposed as CLI
> flags — they sit at these defaults — so tuning them is the open lever, not
> reviving a dead path.

## Constructor + the option menu (verbatim from the header)

```cpp
LinearizerXTaylor(const System& sys,
                  approx_mode mode    = RELAX,
                  corner_policy corners = RANDOM_OPP,
                  slope_formula slope = HANSEN);

enum approx_mode   { RELAX, RESTRICT };
enum corner_policy { INF, SUP, RANDOM, RANDOM_OPP };
enum slope_formula { TAYLOR, HANSEN };
```

| Knob | Options | Default | Meaning |
|---|---|---|---|
| `mode` | `RELAX` / `RESTRICT` | RELAX | outer relaxation (sound enclosure) vs inner restriction |
| `corners` | `INF`, `SUP`, `RANDOM`, `RANDOM_OPP` | RANDOM_OPP | which box corner(s) to Taylor-expand from. `RANDOM_OPP` = a random point **and** its opposite (2 corners) |
| `slope` | `TAYLOR` / `HANSEN` | HANSEN | slope matrix: plain interval Taylor vs the thinner Hansen slope |

dReal uses `(RELAX, RANDOM_OPP, HANSEN)` — i.e. the library's own recommended
defaults (HANSEN slope is tighter than TAYLOR; RANDOM_OPP uses 2 corners).

## Other linearizers in the fork (`numeric/`)

- `LinearizerCompo` — intersect several linearizers' polytopes.
- `LinearizerFixed` — a fixed `Ax≤b`.
- `LinearizerDuality` — duality-based relaxation.
- **`LinearizerAffine2` is absent** — affine arithmetic lives in the separate
  `ibex-affine` plugin (verified: no `*affine*` source in the fork). Treat as a
  plugin task, not a knob ([`../../AUDIT.md`](../../AUDIT.md) D).

Related: [`../contractors/CtcPolytopeHull.md`](../contractors/CtcPolytopeHull.md),
[contractor chapter](../../chapters/contractor.md).
