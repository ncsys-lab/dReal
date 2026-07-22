# C0TripletonSet

## What it is
A C0 set whose **error term is split across two intersected frames**: `x + C*r0 + intersection(B*r, Q*q)`, where `B` and `Q` are point invertible matrices, `Q` kept close to orthogonal, and the set stores rigorous inverses of both (confirmed `C0TripletonSet.h` / class detail). Intersecting two valid error enclosures is never looser than one and usually tighter.

## Key API
- Constructors: `C0TripletonSet(x[, r0][, C][, ...][, t])` — same progressive form as the doubleton.
- `move(DynSysType&)` / `move(dynsys, result)` — propagate one step, maintaining `B`, `Q`, and their inverses.
- `operator VectorType()` — cast to `IVector`.
- `affineTransformation(M, x0)` — `M*(x-x0) + (M*C)*r0 + intersection((M*B)*r, (M*Q)*q)` (the intersection carries through affine maps).
- Template params `<MatrixT, Policies>`.

## dReal status
**Used.** `--ode-c0-set tripleton` selects `C0TripletonSet<IMatrix, C0Rect2Policies>` (`dynset/typedefs.h:46`; `dreal-capd-usage.md`). Note: this is also CAPD's library-wide `DefaultC0Set` (`typedefs.h:50`) — i.e. CAPD considers the tripleton the sensible default, whereas dReal defaults to the cheaper doubleton.

## Why it might matter
The **tightest of the three wired C0 sets without HO's double-integration cost** — the dual-frame intersection on the error term directly attacks wrapping from strong rotation / disparate eigenvalues. The audit question of whether dReal's default should move from doubleton to tripleton is exactly the doubleton-vs-tripleton tradeoff this class embodies (`AUDIT.md` tier B1 — a rank-1 "easiest to try" lever); CAPD's own choice is a data point for "yes."

## Source
[classcapd_1_1dynset_1_1C0TripletonSet.html](../../../../CAPD/docs/html/classcapd_1_1dynset_1_1C0TripletonSet.html)
