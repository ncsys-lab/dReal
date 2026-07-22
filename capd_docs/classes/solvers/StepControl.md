# Step control: `StepControl.h` classes

## What it is
The header defining CAPD's automatic time-step strategies and the `StepControlInterface`
mix-in that every solver/TimeMap inherits (giving `setAbsoluteTolerance`,
`setRelativeTolerance`, `setMaxStep`, `turn{On,Off}StepControl`, `get/setStepControl`).
(`StepControl.h` page + the real header.)

## Key API (confirmed in header)
Free function:
- `computeNextStep(solver, numberOfTerms, epsilon, minTimeStep, degree)` — predicts
  `h = min_i (epsilon/‖coeff_i‖)^{1/i}` over the last `numberOfTerms` Taylor terms,
  clamped to `[minTimeStep, maxStep]`, mantissa-cleared.
- `clearMantissaBits(step, maxValue=32)` — rounds a step to a clean binary value.

Strategy classes (constructor defaults verified in header):
- `ILastTermsStepControl(_terms = 1, _minStep = 1/1048576)` — **default for `IOdeSolver` and
  `IC2OdeSolver`.** Interval step control from the last `_terms` terms. `init()` also derives
  a Lipschitz-based first step.
- `DLastTermsStepControl(_terms = 2, _minStep = 1/1048576)` — nonrigorous (`DOdeSolver`)
  counterpart; note its default `_terms` is **2**, not 1.
- `IEncFoundStepControl(minStep = 1/1048576, stepFactor = 0.25)` — **default for `ICnOdeSolver`.**
  Picks `optStep = step/factor·1.5`, then shrinks ×0.8 until `solver.enclosure(t,x)` validates.
- `FixedStepControl<ScalarType>` — user-fixed step.
- `NoStepControl` / `NoStepControlInterface` — control disabled (fixed step): `computeNextTimeStep`
  keeps the current step (`min(getStep(), maxStep)`), and the `StepControlInterface<NoStepControl,double>`
  specialization no-ops the tolerance setters. Header frames it as "empty time step control for the
  solutions to ODEs" (StepControl.h:172).

`minStep` floor ≈ 9.54e-7; below it the solver gives up (throws).

## dReal status
**Used implicitly (defaults only).** dReal sets tolerances + maxStep through the
`StepControlInterface` methods but never calls `setStepControl`, so it runs the
`ILastTermsStepControl(_terms=1)` default. `IEncFoundStepControl`, `FixedStepControl`, and the
`_terms`/`minStep` knobs are unexplored.

## Why it might matter
- **The abs/rel tolerances dReal sets ARE this step-control's only knob.** The `epsilon` fed to
  `computeNextStep` is `getEffectiveTolerance = max(absTol, relTol·‖coeff₀‖)`, where `‖coeff₀‖`
  is the current state norm (0th Taylor coefficient; `StepControlInterface::getEffectiveTolerance`,
  StepControl.h:119-126). So the adaptive step is `h ≈ min_i (ε/‖coeff_i‖)^{1/i}` over the last
  `_terms` orders. dReal pins both `--ode-abs-tol`/`--ode-rel-tol` at 1e-10 (`kDefaultOdeAbsTol`/
  `kDefaultOdeRelTol`, config.h) and never touches the policy object, so this is the *only*
  step-control lever it currently exposes: loosening either tol raises ε → larger steps → faster
  but looser per-step enclosures (COMPLETENESS lever, never soundness). `--ode-max-step` (default 0
  = `m_maxStep` left at 1e100, fully adaptive) is the hard cap on the resulting `h`.
- **`_terms = 1` is aggressive.** The header's own remark (on the analogous nonrigorous
  control) warns a single last-term predictor over-predicts when that coefficient is near zero;
  the nonrigorous default compensates with `_terms = 2`. dReal could pass
  `solver.setStepControl(ILastTermsStepControl(2 or 3))` to trade a few % speed for fewer
  thrown/abandoned steps — a robustness experiment, no soundness impact.
- **`IEncFoundStepControl`** ties the step to enclosure success, possibly reducing the
  step-divergence throws dReal currently catches-and-skips on stiff flows.
- `minTimeStep` is the give-up floor; raising it ends hopeless flows faster, lowering it lets
  CAPD grind on near-singular steps. Both are pure speed/completeness trade-offs (never
  soundness — an abandoned narrowing is always safe).

## Header enumeration (ground truth — re-auditable)

This node is complete iff it covers every policy class below. Re-audit with
`grep -nE '^class [A-Za-z]+StepControl|explicit [A-Za-z]+StepControl\(' gcc_build/capd-install/include/capd/dynsys/StepControl.h`:

```cpp
class NoStepControl                                                 // disabled (fixed step)
class FixedStepControl(timeStep = 1./1024.)                         // user-fixed
class ILastTermsStepControl(_terms = 1,   _minStep = 1./1048576.)   // ← IOdeSolver/IC2OdeSolver DEFAULT
class DLastTermsStepControl(_terms = 2,   _minStep = 1./1048576.)   // nonrigorous (DOdeSolver)
class IEncFoundStepControl(minStep = 1./1048576., stepFactor = 0.25)// ← ICnOdeSolver default
```
(`NoStepControlInterface` is the base mix-in, not a selectable policy. All five
selectable policies are documented above. `1./1048576. ≈ 9.54e-7` is the min-step floor.)

## Source
[StepControl.h](../../../../CAPD/docs/html/StepControl_8h.html)
