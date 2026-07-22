# All IBEX classes — stub index (the breadth catch-all)

Every public header in the fork (`ncsys-lab/ibex-lib@dreal-perf-patches`),
grouped by source module, with the header's own one-line `\brief` and a link to
the header (the ground truth — IBEX's docs are chapter-based, with no per-class
page). This is the catch-all for any class without a rich node under
[`../classes/`](../classes/) or a narrative in [`../chapters/`](../chapters/).

- **Briefs are harvested verbatim** from each header's `\brief` doxygen line
  attached to the file's primary class — source-faithful by construction, but
  terse and occasionally imperfect (a few headers carry a misleading first
  brief). When precision matters, open the linked header.
- Modules that dReal binds: `arithmetic`, `function`, `symbolic`, `contractor`,
  `numeric` (linearizers + LP), `system`. Modules dReal ignores by design:
  `solver`, `optim`, `loup`, `strategy` (search), `cell`, `bisector`, `set`,
  `predicate` (separators), `parser`, `combinatorial`. See
  [`../dreal-ibex-usage.md`](../dreal-ibex-usage.md).
- Regenerate with `python3 scripts/harvest_class_briefs.py --ibex` (walks `../ibex-fork/src/*/ibex_*.h`).

<!-- BODY BELOW IS AUTO-HARVESTED -->

<!-- harvested 216 headers across 19 modules -->

### `arithmetic/` (14)

| Class / header | One-line brief (from header) |
|---|---|
| [`Dim`](../../../ibex-fork/src/arithmetic/ibex_Dim.h) | Dimensions (of a mathematical value/expression) |
| [`Domain`](../../../ibex-fork/src/arithmetic/ibex_Domain.h) | Interval Domain. |
| [`DoubleIndex`](../../../ibex-fork/src/arithmetic/ibex_DoubleIndex.h) | Uninitialized object */ |
| [`InnerArith`](../../../ibex-fork/src/arithmetic/ibex_InnerArith.h) | Inner image of the addition. |
| [`Interval`](../../../ibex-fork/src/arithmetic/ibex_Interval.h) | Interval |
| [`IntervalMatrix`](../../../ibex-fork/src/arithmetic/ibex_IntervalMatrix.h) | Interval matrix. |
| [`IntervalVector`](../../../ibex-fork/src/arithmetic/ibex_IntervalVector.h) | Vector of Intervals |
| [`InvalidIntervalVectorOp`](../../../ibex-fork/src/arithmetic/ibex_InvalidIntervalVectorOp.h) | Base class for all exceptions raised by an operation on an interval vector |
| [`LinearArith`](../../../ibex-fork/src/arithmetic/ibex_LinearArith.h) |  |
| [`Matrix`](../../../ibex-fork/src/arithmetic/ibex_Matrix.h) | Real matrix. |
| [`TemplateDomain`](../../../ibex-fork/src/arithmetic/ibex_TemplateDomain.h) | Template Domain. |
| [`TemplateMatrix`](../../../ibex-fork/src/arithmetic/ibex_TemplateMatrix.h) |  |
| [`TemplateVector`](../../../ibex-fork/src/arithmetic/ibex_TemplateVector.h) |  |
| [`Vector`](../../../ibex-fork/src/arithmetic/ibex_Vector.h) | Vector of reals |

### `bisector/` (9)

| Class / header | One-line brief (from header) |
|---|---|
| [`Bisection`](../../../ibex-fork/src/bisector/ibex_Bisection.h) | Box bisection. |
| [`BisectionPoint`](../../../ibex-fork/src/bisector/ibex_BisectionPoint.h) | Bisection point. |
| [`Bsc`](../../../ibex-fork/src/bisector/ibex_Bsc.h) | Generic bisector |
| [`LargestFirst`](../../../ibex-fork/src/bisector/ibex_LargestFirst.h) | largest-first bisector. |
| [`LSmear`](../../../ibex-fork/src/bisector/ibex_LSmear.h) | bisector which first ponderates the constraints by using the dual |
| [`NoBisectableVariableException`](../../../ibex-fork/src/bisector/ibex_NoBisectableVariableException.h) | Thrown when no bisection is possible. |
| [`OptimLargestFirst`](../../../ibex-fork/src/bisector/ibex_OptimLargestFirst.h) | largest-first bisector. |
| [`RoundRobin`](../../../ibex-fork/src/bisector/ibex_RoundRobin.h) | Round-robin bisector. |
| [`SmearFunction`](../../../ibex-fork/src/bisector/ibex_SmearFunction.h) | bisector with Smear function heuristic (abstract class) |

### `cell/` (9)

| Class / header | One-line brief (from header) |
|---|---|
| [`Cell`](../../../ibex-fork/src/cell/ibex_Cell.h) | Node in an interval exploration binary tree. |
| [`CellBeamSearch`](../../../ibex-fork/src/cell/ibex_CellBeamSearch.h) | Beamsearch buffer (for global optimization) |
| [`CellBuffer`](../../../ibex-fork/src/cell/ibex_CellBuffer.h) | Cell Buffer |
| [`CellBufferOptim`](../../../ibex-fork/src/cell/ibex_CellBufferOptim.h) | Cell buffer for optimization. |
| [`CellCostFunc`](../../../ibex-fork/src/cell/ibex_CellCostFunc.h) | Root class of cell cost functions |
| [`CellDoubleHeap`](../../../ibex-fork/src/cell/ibex_CellDoubleHeap.h) | Double-heap buffer (for global optimization) |
| [`CellHeap`](../../../ibex-fork/src/cell/ibex_CellHeap.h) | cell Heap buffer (for global optimization) |
| [`CellList`](../../../ibex-fork/src/cell/ibex_CellList.h) | Cell List. |
| [`CellStack`](../../../ibex-fork/src/cell/ibex_CellStack.h) | Cell Stack. |

### `combinatorial/` (1)

| Class / header | One-line brief (from header) |
|---|---|
| [`QInter`](../../../ibex-fork/src/combinatorial/ibex_QInter.h) | Q-intersection - EXACT - Grid algorithm |

### `contractor/` (25)

| Class / header | One-line brief (from header) |
|---|---|
| [`ContractContext`](../../../ibex-fork/src/contractor/ibex_ContractContext.h) | Contraction context. |
| [`Ctc`](../../../ibex-fork/src/contractor/ibex_Ctc.h) | Contractor interface. |
| [`Ctc3BCid`](../../../ibex-fork/src/contractor/ibex_Ctc3BCid.h) | 3B+CID contractor (extension of 3B with CID) |
| [`CtcAcid`](../../../ibex-fork/src/contractor/ibex_CtcAcid.h) | ACID contractor (Adpative version of 3BCID) |
| [`CtcCompo`](../../../ibex-fork/src/contractor/ibex_CtcCompo.h) | Composition of contractors |
| [`CtcEmpty`](../../../ibex-fork/src/contractor/ibex_CtcEmpty.h) | Empty contractor |
| [`CtcExist`](../../../ibex-fork/src/contractor/ibex_CtcExist.h) | Projection-union operator (for existentially-quantified constraints) |
| [`CtcFixPoint`](../../../ibex-fork/src/contractor/ibex_CtcFixPoint.h) | FixPoint of a contractor |
| [`CtcForAll`](../../../ibex-fork/src/contractor/ibex_CtcForAll.h) | Projection-union operator (for universally-quantified constraints) |
| [`CtcFwdBwd`](../../../ibex-fork/src/contractor/ibex_CtcFwdBwd.h) | Forward-backward contractor (HC4Revise). |
| [`CtcHC4`](../../../ibex-fork/src/contractor/ibex_CtcHC4.h) | HC4 propagation. |
| [`CtcIdentity`](../../../ibex-fork/src/contractor/ibex_CtcIdentity.h) | Identity contractor |
| [`CtcInteger`](../../../ibex-fork/src/contractor/ibex_CtcInteger.h) | Integral contractor |
| [`CtcInverse`](../../../ibex-fork/src/contractor/ibex_CtcInverse.h) | Contract a box. |
| [`CtcKuhnTucker`](../../../ibex-fork/src/contractor/ibex_CtcKuhnTucker.h) | Contractor based on first-order (KKT) conditions (for NLP) |
| [`CtcKuhnTuckerLP`](../../../ibex-fork/src/contractor/ibex_CtcKuhnTuckerLP.h) | Contractor based on first-order (KKT) conditions (for NLP) |
| [`CtcLinearRelax`](../../../ibex-fork/src/contractor/ibex_CtcLinearRelax.h) | Contract a box by linearizing a system. |
| [`CtcNewton`](../../../ibex-fork/src/contractor/ibex_CtcNewton.h) | Newton contractor. |
| [`CtcNotIn`](../../../ibex-fork/src/contractor/ibex_CtcNotIn.h) | Build the contractor for "f(x) not-in [y]". |
| [`CtcOptimShaving`](../../../ibex-fork/src/contractor/ibex_CtcOptimShaving.h) | Contract a box. |
| [`CtcPolytopeHull`](../../../ibex-fork/src/contractor/ibex_CtcPolytopeHull.h) | Contract the bounds of a box with respect to a polytope. |
| [`CtcPropag`](../../../ibex-fork/src/contractor/ibex_CtcPropag.h) | Propagation contractor. |
| [`CtcQInter`](../../../ibex-fork/src/contractor/ibex_CtcQInter.h) | Q-intersection contractor. |
| [`CtcQuantif`](../../../ibex-fork/src/contractor/ibex_CtcQuantif.h) | Abstract contractor for quantified constraints (proj-union/proj-inter) |
| [`CtcUnion`](../../../ibex-fork/src/contractor/ibex_CtcUnion.h) | Union of contractors |

### `data/` (7)

| Class / header | One-line brief (from header) |
|---|---|
| [`Cov`](../../../ibex-fork/src/data/ibex_Cov.h) | Covering mother class. |
| [`CovIBUList`](../../../ibex-fork/src/data/ibex_CovIBUList.h) | Covering IBU list (with Inner, Boundary and Unknown boxes) |
| [`CovIUList`](../../../ibex-fork/src/data/ibex_CovIUList.h) | Covering IU list (with Inner and Unknown boxes) |
| [`CovList`](../../../ibex-fork/src/data/ibex_CovList.h) | Covering list. |
| [`CovManifold`](../../../ibex-fork/src/data/ibex_CovManifold.h) | Covering of a manifold |
| [`CovOptimData`](../../../ibex-fork/src/data/ibex_CovOptimData.h) | Optimizer (IbexOpt) data. |
| [`CovSolverData`](../../../ibex-fork/src/data/ibex_CovSolverData.h) | Solver (IbexSolve) data. |

### `function/` (14)

| Class / header | One-line brief (from header) |
|---|---|
| [`BwdAlgorithm`](../../../ibex-fork/src/function/ibex_BwdAlgorithm.h) | Interface for backward Algorithms. |
| [`CompiledFunction`](../../../ibex-fork/src/function/ibex_CompiledFunction.h) | A low-level representation of a function for speeding up forward/backward algorithms. |
| [`Eval`](../../../ibex-fork/src/function/ibex_Eval.h) | Function evaluator. |
| [`ExprData`](../../../ibex-fork/src/function/ibex_ExprData.h) | Expression Data factory. |
| [`ExprDomain`](../../../ibex-fork/src/function/ibex_ExprDomain.h) | Domain associated to nodes of a function. |
| [`Fnc`](../../../ibex-fork/src/function/ibex_Fnc.h) | Function (numerical) |
| [`FncProj`](../../../ibex-fork/src/function/ibex_FncProj.h) | Project a function onto selected components |
| [`Function`](../../../ibex-fork/src/function/ibex_Function.h) | Symbolic function (x->f(x) where f(x) is the DAG of an arithmetical expression). |
| [`FwdAlgorithm`](../../../ibex-fork/src/function/ibex_FwdAlgorithm.h) | Interface for forward algorithms. |
| [`Gradient`](../../../ibex-fork/src/function/ibex_Gradient.h) | Calculates the gradient of a function. |
| [`HC4Revise`](../../../ibex-fork/src/function/ibex_HC4Revise.h) | The famous forward-backward contraction algorithm. |
| [`InHC4Revise`](../../../ibex-fork/src/function/ibex_InHC4Revise.h) |  |
| [`NumConstraint`](../../../ibex-fork/src/function/ibex_NumConstraint.h) | Numerical constraint. |
| [`VarSet`](../../../ibex-fork/src/function/ibex_VarSet.h) | Set of Variables |

### `loup/` (8)

| Class / header | One-line brief (from header) |
|---|---|
| [`LoupFinder`](../../../ibex-fork/src/loup/ibex_LoupFinder.h) | Root class of all upper-bounding algorithms. |
| [`LoupFinderCertify`](../../../ibex-fork/src/loup/ibex_LoupFinderCertify.h) | Certify a loup point. |
| [`LoupFinderDefault`](../../../ibex-fork/src/loup/ibex_LoupFinderDefault.h) | Default upper-bounding algorithm (for inequalities only). |
| [`LoupFinderDuality`](../../../ibex-fork/src/loup/ibex_LoupFinderDuality.h) | Upper-bounding algorithm based on duality. |
| [`LoupFinderFwdBwd`](../../../ibex-fork/src/loup/ibex_LoupFinderFwdBwd.h) | Contractor-based upper-bounding algorithm. |
| [`LoupFinderInHC4`](../../../ibex-fork/src/loup/ibex_LoupFinderInHC4.h) | Upper-bounding algorithm based on inner arithmetic. |
| [`LoupFinderProbing`](../../../ibex-fork/src/loup/ibex_LoupFinderProbing.h) | Upper-bounding algorithm based on simple sampling and probing. |
| [`LoupFinderXTaylor`](../../../ibex-fork/src/loup/ibex_LoupFinderXTaylor.h) | Upper-bounding algorithm based on XTaylor restriction. |

### `numeric/` (12)

| Class / header | One-line brief (from header) |
|---|---|
| [`Certificate`](../../../ibex-fork/src/numeric/ibex_Certificate.h) | Create a certificate. |
| [`Kernel`](../../../ibex-fork/src/numeric/ibex_Kernel.h) | Orthogonalizes the rows of the matrix A. |
| [`Linear`](../../../ibex-fork/src/numeric/ibex_Linear.h) | LU decomposition of a real matrix with partial pivoting |
| [`LinearException`](../../../ibex-fork/src/numeric/ibex_LinearException.h) | IbexExceptions related to linear systems |
| [`Linearizer`](../../../ibex-fork/src/numeric/ibex_Linearizer.h) | Linearization method. |
| [`LinearizerCompo`](../../../ibex-fork/src/numeric/ibex_LinearizerCompo.h) | Composition of linearizations (logical AND). |
| [`LinearizerDuality`](../../../ibex-fork/src/numeric/ibex_LinearizerDuality.h) | Duality-based linear restriction of a NLP. |
| [`LinearizerFixed`](../../../ibex-fork/src/numeric/ibex_LinearizerFixed.h) | Fixed linear system Ax<=b |
| [`LinearizerXTaylor`](../../../ibex-fork/src/numeric/ibex_LinearizerXTaylor.h) | X-Taylor linearization technique. |
| [`LPException`](../../../ibex-fork/src/numeric/ibex_LPException.h) | Thrown when the linear solver give an Exception. |
| [`LPSolver`](../../../ibex-fork/src/numeric/ibex_LPSolver.h) | Linear Programming Solver |
| [`Newton`](../../../ibex-fork/src/numeric/ibex_Newton.h) | Default Newton precision |

### `operators/` (5)

| Class / header | One-line brief (from header) |
|---|---|
| [`atanhc`](../../../ibex-fork/src/operators/ibex_atanhc.h) | Cardinal arctangent. |
| [`atanhccc`](../../../ibex-fork/src/operators/ibex_atanhccc.h) | 3rd-order cardinal arctangent. |
| [`crossproduct`](../../../ibex-fork/src/operators/ibex_crossproduct.h) | Cross product of two 3D vectors |
| [`sinc`](../../../ibex-fork/src/operators/ibex_sinc.h) | Cardinal sine. |
| [`trace`](../../../ibex-fork/src/operators/ibex_trace.h) |  |

### `optim/` (9)

| Class / header | One-line brief (from header) |
|---|---|
| [`BxpMultipliers`](../../../ibex-fork/src/optim/ibex_BxpMultipliers.h) | Lagrange Multipliers |
| [`BxpOptimData`](../../../ibex-fork/src/optim/ibex_BxpOptimData.h) | Data required for the Optimizer |
| [`DefaultOptimizer`](../../../ibex-fork/src/optim/ibex_DefaultOptimizer.h) | Default optimizer. |
| [`DefaultOptimizerConfig`](../../../ibex-fork/src/optim/ibex_DefaultOptimizerConfig.h) | Default optimizer configuration. |
| [`LineSearch`](../../../ibex-fork/src/optim/ibex_LineSearch.h) | Line search inside a bounding box |
| [`Optimizer`](../../../ibex-fork/src/optim/ibex_Optimizer.h) | Global Optimizer. |
| [`OptimizerConfig`](../../../ibex-fork/src/optim/ibex_OptimizerConfig.h) | Optimizer Configuration. |
| [`OptimMemory`](../../../ibex-fork/src/optim/ibex_OptimMemory.h) |  |
| [`UnconstrainedLocalSearch`](../../../ibex-fork/src/optim/ibex_UnconstrainedLocalSearch.h) | Local optimizer based on trust region |

### `parser/` (12)

| Class / header | One-line brief (from header) |
|---|---|
| [`P_CtrGenerator`](../../../ibex-fork/src/parser/ibex_P_CtrGenerator.h) |  |
| [`P_Expr`](../../../ibex-fork/src/parser/ibex_P_Expr.h) | Data associated to each node. |
| [`P_ExprGenerator`](../../../ibex-fork/src/parser/ibex_P_ExprGenerator.h) |  |
| [`P_ExprPrinter`](../../../ibex-fork/src/parser/ibex_P_ExprPrinter.h) |  |
| [`P_ExprVisitor`](../../../ibex-fork/src/parser/ibex_P_ExprVisitor.h) |  |
| [`P_NumConstraint`](../../../ibex-fork/src/parser/ibex_P_NumConstraint.h) |  |
| [`P_Scope`](../../../ibex-fork/src/parser/ibex_P_Scope.h) |  |
| [`P_Source`](../../../ibex-fork/src/parser/ibex_P_Source.h) | The source. |
| [`P_Struct`](../../../ibex-fork/src/parser/ibex_P_Struct.h) | Main parser structure |
| [`P_SysGenerator`](../../../ibex-fork/src/parser/ibex_P_SysGenerator.h) |  |
| [`SyntaxError`](../../../ibex-fork/src/parser/ibex_SyntaxError.h) | Syntax error exception. |
| [`UnknownFileException`](../../../ibex-fork/src/parser/ibex_UnknownFileException.h) | Unknown file exception. |

### `predicate/` (13)

| Class / header | One-line brief (from header) |
|---|---|
| [`BoolInterval`](../../../ibex-fork/src/predicate/ibex_BoolInterval.h) | Boolean interval. |
| [`Pdc`](../../../ibex-fork/src/predicate/ibex_Pdc.h) | Predicate (function that maps a box to a boolean interval) |
| [`PdcAnd`](../../../ibex-fork/src/predicate/ibex_PdcAnd.h) | Logical AND of predicates |
| [`PdcCleared`](../../../ibex-fork/src/predicate/ibex_PdcCleared.h) | Apply the predicate to the given box. |
| [`PdcDiameterLT`](../../../ibex-fork/src/predicate/ibex_PdcDiameterLT.h) | Precision predicate. |
| [`PdcFirstOrder`](../../../ibex-fork/src/predicate/ibex_PdcFirstOrder.h) | Rejection test based on first-order condition |
| [`PdcFwdBwd`](../../../ibex-fork/src/predicate/ibex_PdcFwdBwd.h) | Basic inner test wrt f(x)<=0. |
| [`PdcHansenFeasibility`](../../../ibex-fork/src/predicate/ibex_PdcHansenFeasibility.h) | Hansen feasibility test for equality (under-)constrained problems |
| [`PdcImageSubset`](../../../ibex-fork/src/predicate/ibex_PdcImageSubset.h) | Test if a box is inside the range of a function over an (implicit) set. |
| [`PdcNo`](../../../ibex-fork/src/predicate/ibex_PdcNo.h) | predicate which return YES every time |
| [`PdcNot`](../../../ibex-fork/src/predicate/ibex_PdcNot.h) | Logical negation |
| [`PdcOr`](../../../ibex-fork/src/predicate/ibex_PdcOr.h) | Logical OR of predicates |
| [`PdcYes`](../../../ibex-fork/src/predicate/ibex_PdcYes.h) | predicate which return YES every time |

### `set/` (16)

| Class / header | One-line brief (from header) |
|---|---|
| [`Sep`](../../../ibex-fork/src/set/ibex_Sep.h) | Separator interface. |
| [`SepBoundaryCtc`](../../../ibex-fork/src/set/ibex_SepBoundaryCtc.h) | A Separator based on a boundary contractor and a membership predicate |
| [`SepCtcPair`](../../../ibex-fork/src/set/ibex_SepCtcPair.h) | A pair of two independent/complementary contractors |
| [`SepFwdBwd`](../../../ibex-fork/src/set/ibex_SepFwdBwd.h) | Forward-Backward Separator |
| [`SepInter`](../../../ibex-fork/src/set/ibex_SepInter.h) | Intersection of separators |
| [`SepInverse`](../../../ibex-fork/src/set/ibex_SepInverse.h) | Image of a separator by a function in a forward-backaward manner |
| [`SepNot`](../../../ibex-fork/src/set/ibex_SepNot.h) | Negation of a Separator |
| [`SepQInter`](../../../ibex-fork/src/set/ibex_SepQInter.h) | Q-intersection separator. |
| [`SepUnion`](../../../ibex-fork/src/set/ibex_SepUnion.h) | Union of separators |
| [`Set`](../../../ibex-fork/src/set/ibex_Set.h) | Interval-based representation of a set |
| [`SetBisect`](../../../ibex-fork/src/set/ibex_SetBisect.h) | Bisection node (internal class used for set representation) |
| [`SetInterval`](../../../ibex-fork/src/set/ibex_SetInterval.h) | Set interval (or i-set) |
| [`SetLeaf`](../../../ibex-fork/src/set/ibex_SetLeaf.h) | Leaf node (internal class used for set representation) |
| [`SetNode`](../../../ibex-fork/src/set/ibex_SetNode.h) | Set node. |
| [`SetValueNode`](../../../ibex-fork/src/set/ibex_SetValueNode.h) |  |
| [`SetVisitor`](../../../ibex-fork/src/set/ibex_SetVisitor.h) | Set visitor |

### `solver/` (2)

| Class / header | One-line brief (from header) |
|---|---|
| [`DefaultSolver`](../../../ibex-fork/src/solver/ibex_DefaultSolver.h) | Default solver. |
| [`Solver`](../../../ibex-fork/src/solver/ibex_Solver.h) | Solver. |

### `strategy/` (13)

| Class / header | One-line brief (from header) |
|---|---|
| [`BoxEvent`](../../../ibex-fork/src/strategy/ibex_BoxEvent.h) | Box event. |
| [`BoxProperties`](../../../ibex-fork/src/strategy/ibex_BoxProperties.h) | Box properties |
| [`Bxp`](../../../ibex-fork/src/strategy/ibex_Bxp.h) | Box property. |
| [`BxpActiveCtr`](../../../ibex-fork/src/strategy/ibex_BxpActiveCtr.h) | Whether an inequality is potentially active or not. |
| [`BxpActiveCtrs`](../../../ibex-fork/src/strategy/ibex_BxpActiveCtrs.h) | Which inequalities are potentially active in a system. |
| [`BxpLinearRelaxArgMin`](../../../ibex-fork/src/strategy/ibex_BxpLinearRelaxArgMin.h) | Store the argmin of a system linear relaxation. |
| [`BxpSystemCache`](../../../ibex-fork/src/strategy/ibex_BxpSystemCache.h) | Cache for system computations |
| [`Paver`](../../../ibex-fork/src/strategy/ibex_Paver.h) | Paver. |
| [`SetImage`](../../../ibex-fork/src/strategy/ibex_SetImage.h) | Set image using Goldsztejn-Jaulin algorithm (Reliable Computing, 2010). |
| [`Statistics`](../../../ibex-fork/src/strategy/ibex_Statistics.h) | Statistics |
| [`Sts`](../../../ibex-fork/src/strategy/ibex_Sts.h) | Abstract class for the statistics of a given operator. |
| [`StsLPSolver`](../../../ibex-fork/src/strategy/ibex_StsLPSolver.h) | Statistics of LP Solver |
| [`SubPaving`](../../../ibex-fork/src/strategy/ibex_SubPaving.h) | Subpaving |

### `symbolic/` (22)

| Class / header | One-line brief (from header) |
|---|---|
| [`CmpOp`](../../../ibex-fork/src/symbolic/ibex_CmpOp.h) | Comparison operator. |
| [`Expr`](../../../ibex-fork/src/symbolic/ibex_Expr.h) | Node in an expression. |
| [`Expr2DAG`](../../../ibex-fork/src/symbolic/ibex_Expr2DAG.h) | Transform an expression to a minimal-size DAG |
| [`Expr2Minibex`](../../../ibex-fork/src/symbolic/ibex_Expr2Minibex.h) | Get the Minibex code of an expression |
| [`Expr2Polynom`](../../../ibex-fork/src/symbolic/ibex_Expr2Polynom.h) |  |
| [`ExprCmp`](../../../ibex-fork/src/symbolic/ibex_ExprCmp.h) | Compare two expressions |
| [`ExprCopy`](../../../ibex-fork/src/symbolic/ibex_ExprCopy.h) | Duplicate an expression |
| [`ExprCtr`](../../../ibex-fork/src/symbolic/ibex_ExprCtr.h) | Constraint expression. |
| [`ExprDiff`](../../../ibex-fork/src/symbolic/ibex_ExprDiff.h) | Differentiate an expression. |
| [`ExprFuncDomain`](../../../ibex-fork/src/symbolic/ibex_ExprFuncDomain.h) | Domain definition of a function |
| [`ExprLinearity`](../../../ibex-fork/src/symbolic/ibex_ExprLinearity.h) | Linearity Test for Expressions |
| [`ExprMonomial`](../../../ibex-fork/src/symbolic/ibex_ExprMonomial.h) |  |
| [`ExprOccCounter`](../../../ibex-fork/src/symbolic/ibex_ExprOccCounter.h) | Count symbol occurrences in an expression. |
| [`ExprPolynomial`](../../../ibex-fork/src/symbolic/ibex_ExprPolynomial.h) | Expression polynomial. |
| [`ExprPrinter`](../../../ibex-fork/src/symbolic/ibex_ExprPrinter.h) | Print an expression into an ostream. |
| [`ExprSimplify`](../../../ibex-fork/src/symbolic/ibex_ExprSimplify.h) |  |
| [`ExprSimplify2`](../../../ibex-fork/src/symbolic/ibex_ExprSimplify2.h) |  |
| [`ExprSize`](../../../ibex-fork/src/symbolic/ibex_ExprSize.h) | Calculate the size of a DAG |
| [`ExprSubNodes`](../../../ibex-fork/src/symbolic/ibex_ExprSubNodes.h) | All the (sub)nodes of an expression (including itself) sorted by |
| [`ExprVisitor`](../../../ibex-fork/src/symbolic/ibex_ExprVisitor.h) | Interface for expression visitors. |
| [`InputNodeMap`](../../../ibex-fork/src/symbolic/ibex_InputNodeMap.h) |  |
| [`NodeMap`](../../../ibex-fork/src/symbolic/ibex_NodeMap.h) | An unordered map which keys are expression nodes. |

### `system/` (7)

| Class / header | One-line brief (from header) |
|---|---|
| [`ExtendedSystem`](../../../ibex-fork/src/system/ibex_ExtendedSystem.h) | System where a goal is encoded as a constraint. |
| [`FncActiveCtrs`](../../../ibex-fork/src/system/ibex_FncActiveCtrs.h) | Constraints and domain bounds that correspond to active constraints. |
| [`FncKuhnTucker`](../../../ibex-fork/src/system/ibex_FncKuhnTucker.h) | Function corresponding to KKT conditions. |
| [`KuhnTuckerSystem`](../../../ibex-fork/src/system/ibex_KuhnTuckerSystem.h) | System of KKT conditions |
| [`NormalizedSystem`](../../../ibex-fork/src/system/ibex_NormalizedSystem.h) | Normalized system |
| [`System`](../../../ibex-fork/src/system/ibex_System.h) | System. |
| [`SystemFactory`](../../../ibex-fork/src/system/ibex_SystemFactory.h) | System factory. |

### `tools/` (18)

| Class / header | One-line brief (from header) |
|---|---|
| [`Agenda`](../../../ibex-fork/src/tools/ibex_Agenda.h) | Agenda. |
| [`Array`](../../../ibex-fork/src/tools/ibex_Array.h) | Array of references. |
| [`BitSet`](../../../ibex-fork/src/tools/ibex_BitSet.h) | Bit set (of dynamically-fixed size). |
| [`DirectedHyperGraph`](../../../ibex-fork/src/tools/ibex_DirectedHyperGraph.h) | Directed hyper-graph. |
| [`DoubleHeap`](../../../ibex-fork/src/tools/ibex_DoubleHeap.h) | Double-heap |
| [`Exception`](../../../ibex-fork/src/tools/ibex_Exception.h) | Root class of all exceptions raised by IBEX |
| [`Heap`](../../../ibex-fork/src/tools/ibex_Heap.h) | Heap |
| [`HyperGraph`](../../../ibex-fork/src/tools/ibex_HyperGraph.h) | Hypergraph. |
| [`Id`](../../../ibex-fork/src/tools/ibex_Id.h) | Generate an identifier. |
| [`IntStack`](../../../ibex-fork/src/tools/ibex_IntStack.h) |  |
| [`Map`](../../../ibex-fork/src/tools/ibex_Map.h) | Map a number to something, either by reference or copy. |
| [`Memory`](../../../ibex-fork/src/tools/ibex_Memory.h) |  |
| [`mistral_Bitset`](../../../ibex-fork/src/tools/ibex_mistral_Bitset.h) | A representation of sets using a vector of bits. |
| [`Random`](../../../ibex-fork/src/tools/ibex_Random.h) | Custom class for random number generator |
| [`SharedHeap`](../../../ibex-fork/src/tools/ibex_SharedHeap.h) | Shared heap (internal) |
| [`String`](../../../ibex-fork/src/tools/ibex_String.h) | Write an index at the end of a string, surrounded with two |
| [`SymbolMap`](../../../ibex-fork/src/tools/ibex_SymbolMap.h) | Structure to map symbol to any data. |
| [`Timer`](../../../ibex-fork/src/tools/ibex_Timer.h) | Timer. |

