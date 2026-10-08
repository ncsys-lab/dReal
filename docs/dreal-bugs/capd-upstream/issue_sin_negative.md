Thank you for CAPD. We use it in dReal, an SMT solver for nonlinear real arithmetic with ODEs, to enclose the solutions of ODEs.

Interval `sin` never returns when the argument's left bound is below about −5.8e19. A positive argument of the same size returns [-1, 1] at once.

### Reproducer

```cpp
#include "capd/capdlib.h"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
  const double x = std::atof(argv[1]);
  std::cout.precision(17);
  std::cout << "sin(" << x << ") = " << std::flush;
  std::cout << sin(capd::interval(x)) << std::endl;
}
```

Built with `c++ -std=c++17 sin_negative_hang.cpp $(capd-config --cflags --libs)` against CAPD `03dc5628` (`CAPD_INTERVAL_TYPE=NATIVE`), Apple clang 21, macOS arm64:

| argument | result |
|---|---|
| 1e20 | [-1,1] |
| -1e19 | [-1,1] |
| -5e19 | [-1,1] |
| -6e19 | no result after 5 s |
| -1e20 | no result after 5 s |

`capdAlg/include/capd/intervals/Interval_Fun.hpp` is the same on master (`2f06098`).

### Where we think it comes from

This is our reading of the source; we have not tried a fix.

For a negative argument, `sin` computes `temp = (-x.leftBound()) / pi2.rightBound() + 1`, which is at least 1. It returns [-1, 1] only if `temp < std::numeric_limits<long>::min()` (line 447), and that never holds. Past about 5.8e19, `toLongInt(temp)` overflows, so `y = x - k*pi2` stays close to `x`. The loop `while (y.leftBound() < 0.) y += pi2;` (line 454) then adds 2π to a number whose spacing is far larger than 2π, and `y` never changes.

The positive branch tests `temp > std::numeric_limits<long>::max()` (line 440). The same test in the negative branch would return [-1, 1] here.

### A NaN argument

The same program with `nan` crashes with SIGSEGV. In our reading, every comparison in `sin` is false for NaN, so the argument reaches `scaledSin1`. Its branch tests are false too, and `return -scaledSin1(x - pi)` recurses until the stack runs out. We now keep NaN away from CAPD on our side.
