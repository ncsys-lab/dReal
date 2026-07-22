# capd::map::Map (capd::IMap)

**What it is.** `Map<MatrixT>` (typedef `IMap = Map<IMatrix>`) is CAPD's parsed,
auto-differentiable vector field. It parses an RHS string (or a C-routine of `autodiff::Node`s)
into an AD DAG and evaluates the map's value, Jacobian, Hessian, jet, and the iterative Taylor
coefficients of the ODE *solution*. This is the object dReal constructs once per flow and
caches (`fn_fwd = f`, `fn_bwd = −f`).

**Key API** (from `$HDR/map/Map.h` / `group__map.html`):

```cpp
Map();                                  // f(x)=0, R->R
Map(const std::string&, size_type degree=1);   // parse string, alloc jet to 'degree'
Map(const char*, size_type degree=1);
template<class Fn> Map(Fn f, int dimIn, int dimOut, int noParam, size_type degree=1); // C-routine
Map& operator=(const std::string&);     // re-parse, reallocate DAG (keeps degree)
template<class Fn> void reset(Fn f, int dimIn, int dimOut, int noParam, size_type degree);

// evaluation:
ImageVectorType operator()(const VectorType& u) const;                 // value f(u)
ImageVectorType operator()(ScalarType t, const VectorType& u) const;   // nonautonomous
ImageVectorType operator()(const VectorType& u, MatrixType& Df) const; // value + Jacobian (fast)
MatrixType      derivative(const VectorType& u) const;                 // Jacobian
MatrixType      operator[](const VectorType& u) const;                 // == derivative
ImageVectorType operator()(const VectorType& x, MatrixType& Df, HessianType& Hf) const;
JetType         operator()(const JetType& x) const;                    // jet propagation

// ODE coefficient generation (used by the solver):
void computeODECoefficients(VectorType coeffs[], size_type order) const;                 // C0
void computeODECoefficients(VectorType coeffs[], MatrixType dCoeffs[], size_type order) const; // + 1st variational
void computeODECoefficients(VectorType coeffs[], MatrixType dCoeffs[], HessianType hCoeffs[], size_type order) const;

// parameters / config:
void setParameter(size_type d, const ScalarType& v);  // rebind WITHOUT re-parsing (BasicFunction)
void setParameter(const std::string& name, const ScalarType&);  void setParameters(const VectorType&);
size_type dimension() const;  size_type imageDimension() const;
size_type degree() const;  void setDegree(size_type);
void setCurrentTime(const ScalarType&) const;  void differentiateTime() const;
```

Parser syntax: `"[par:...;][time:t;]var:x1,...;fun:expr1,...;"` (`par:`/`time:` optional).
Elementary funcs `sin/cos/exp/log/sqrt/sqr`, operators `+ - * / ^` (the `^` exponent may not
depend on a variable — its gradient is assumed zero). Docstring constant guidance: use
representable-number constants; route interval or very-high-precision constants through `par:`.
The `Map(Fn,...)` / `reset` C-routine path builds the same AD DAG, so per-call evaluation is
identical to the string path (our inference from the shared DAG) — it only skips the one-time
text parse.

**dReal status.** Used: string construction + `setParameter` rebinding, cached once per flow;
the `IOdeSolver` internally drives `computeODECoefficients` (C0 only). Direct
`derivative`/`operator()(u,Df)`/Hessian/jet evaluation and the variational
`computeODECoefficients` overloads are unused. See `dreal-capd-usage.md`.

**Why it might matter.**
- *Parse cost:* the string-parse + `createEvalPath` DAG build is the per-flow setup, already
  amortized by `CapdOdeCache`. The C-routine path offers no evaluation speedup (only avoids the
  text parse at construction), so it is not a per-call lever.
- *Faster-than-DAG eval (not usable here):* `codeTranslation(className, namespace, path)`
  (`Map.h:285`) generates a fixed-dimension static-array-DAG C++ class the docstring recommends
  for "most aggressive optimization" — the only documented way to beat the dynamic-DAG eval
  speed. But it *emits a source file to compile*, so it cannot apply to dReal's per-query
  runtime-parsed flows.
- *Backward narrowing:* `derivative(u)` / the variational `computeODECoefficients` overloads
  are the entry points a C1 sensitivity-based backward step would call (see
  `../../concepts/variational-equations.md`).

**Source.** [classcapd_1_1map_1_1Map.html](../../../../CAPD/docs/html/classcapd_1_1map_1_1Map.html)
