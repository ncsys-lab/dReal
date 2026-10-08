// x' = 10 (1 - x) from the thin point x(0) = 1, a fixed point of the flow: the solver throws
// "minimal time step reached". The same set widened by one ulp, and the zero fixed point of
// x' = -x, integrate.
// Build: c++ -std=c++17 rest_fixed_point_throw.cpp -o rest_fixed_point_throw $(capd-config --cflags --libs)
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
