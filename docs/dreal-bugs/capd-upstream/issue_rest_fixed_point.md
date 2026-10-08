Thank you for CAPD. We use it in dReal, an SMT solver for nonlinear real arithmetic with ODEs, to enclose the solutions of ODEs.

`IOdeSolver` cannot integrate x' = 10(1 − x) from the point x(0) = 1, which is the equilibrium of this flow. The first step throws `High Order Enclosure Error: minimal time step reached. Cannot integrate.` The same initial set widened by one ulp integrates. So does the point 0, the equilibrium of x' = −x.

### Reproducer

```cpp
#include "capd/capdlib.h"
#include <cmath>
#include <iostream>
#include <string>

static void run(const char* fun, double lo, double hi) {
  std::cout.precision(17);
  std::cout << fun << ", x(0) in [" << lo << ", " << hi << "]: ";
  try {
    capd::IMap map(fun);
    capd::IOdeSolver solver(map, 12);
    solver.setAbsoluteTolerance(1e-10);
    solver.setRelativeTolerance(1e-10);
    capd::ITimeMap time_map(solver);
    capd::IVector u0(1);
    u0[0] = capd::interval(lo, hi);
    capd::C0Rect2Set set(u0);
    const capd::IVector x = time_map(0.05, set);
    std::cout << "x(0.05) = " << x << "\n";
  } catch (const std::exception& e) {
    const std::string w = e.what();
    std::cout << "THROW " << w.substr(0, w.find('\n')) << "\n";
  }
}

int main() {
  run("var:x;fun:10*(1-x);", 1.0, 1.0);
  run("var:x;fun:10*(1-x);", std::nextafter(1.0, 0.0), std::nextafter(1.0, 2.0));
  run("var:x;fun:-x;", 0.0, 0.0);
}
```

Built with `c++ -std=c++17 rest_fixed_point_throw.cpp $(capd-config --cflags --libs)` against CAPD `03dc5628` (`CAPD_INTERVAL_TYPE=NATIVE`), Apple clang 21, macOS arm64:

```
var:x;fun:10*(1-x);, x(0) in [1, 1]: THROW High Order Enclosure Error: minimal time step reached. Cannot integrate.
var:x;fun:10*(1-x);, x(0) in [0.99999999999999989, 1.0000000000000003]: x(0.05) = {[0.99999999999999966,1.0000000000000005]}
var:x;fun:-x;, x(0) in [0, 0]: x(0.05) = {[-4.9406564584124655e-324,4.9406564584124655e-324]}
```

The flow k(c − x) from the point x(0) = c throws the same way for every pair we tried, with c ∈ {0.5, 1, 2, −1} and k ∈ {0.01, 1, 10}. `capdDynSys/include/capd/dynsys/HighOrderEnclosure.h` is the same on master (`2f06098`).

### Where we think it comes from

This is our guess; we have not traced it.

`predictNextEnclosure` (`HighOrderEnclosure.h`, line 164) sets the predicted remainder to `mulFactor*(stepToOrder*(*remCoeff) + epsilon)`, with `epsilon = 1.e-300` and `mulFactor = [-2, 2]`. At an equilibrium every Taylor coefficient after the first is zero, so the predicted remainder is about ±2e-300. Around a nonzero c that is far below the spacing of doubles near c, while the enclosure the solver then checks against the prediction is at least that spacing wide. If so, no step size passes the check, and the step shrinks to the minimum. Around 0 the 1e-300 is representable, which would explain why that equilibrium integrates.

### What we do in the meantime

We widen every initial set outward by one ulp before handing it to the solver. The wider set contains the original one, so the enclosures stay valid.
