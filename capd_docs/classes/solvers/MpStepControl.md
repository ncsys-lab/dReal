# Step control: `MpStepControl.h` — `MpLastTermsStepControl`

## What it is
The **multiprecision** analog of `ILastTermsStepControl`: `MpLastTermsStepControl(numberOfTerms=1,
minimalTimeStep=1/1048576)` (+ its `StepControlInterface<MpLastTermsStepControl, Scalar>`
specialization), for solvers whose scalar/tolerance type is `capd::MpFloat` rather than `double`
(header-confirmed, `MpStepControl.h:75,139`). Same last-terms prediction logic, `MpFloat`
arithmetic. Only built with the MpFloat module — absent from the pinned `CAPD_INTERVAL_TYPE=NATIVE`
`capd-install/`.

## dReal status
**Unused and inapplicable.** dReal builds CAPD with `CAPD_INTERVAL_TYPE=NATIVE` (double-based
intervals — see CLAUDE.md / dreal-capd-usage.md), so the `MpFloat` step-control path is never
instantiated (`capd::MpFloat` is itself `typedef ::capd::multiPrec::MpReal MpFloat`, mplib.h:26).

## Why it might matter
Only relevant if dReal ever needed multiprecision intervals (e.g. to push enclosure precision
far below double `1e-10` for an extremely ill-conditioned flow). That would be a large build
change (`CAPD_INTERVAL_TYPE`, the `Mp*` solver typedefs) and is orthogonal to the C0→C1
narrowing opportunities. Not a near-term lever.

## Source
[MpStepControl.h](../../../../CAPD/docs/html/MpStepControl_8h.html)
