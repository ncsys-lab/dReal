// CAPD interval sin never returns for an argument below about -5.8e19.
// Build: c++ -std=c++17 sin_negative_hang.cpp -o sin_negative_hang $(capd-config --cflags --libs)
#include "capd/capdlib.h"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
  const double x = std::atof(argv[1]);
  std::cout.precision(17);
  std::cout << "sin(" << x << ") = " << std::flush;
  std::cout << sin(capd::interval(x)) << std::endl;
}
