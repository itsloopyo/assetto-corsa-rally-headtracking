#pragma once

#include <cmath>
#include <iostream>

// The shared plumbing for the mod's own tests, in the same shape as
// cameraunlock-core/cpp/tests: each file exports one Run*Tests() that returns
// its failure count, and test_main.cpp sums them.

namespace acr_ht_tests {

inline void Check(int& failures, bool condition, const char* name) {
    if (condition) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++failures;
    }
}

inline bool Near(double a, double b, double epsilon = 1e-9) {
    return std::fabs(a - b) <= epsilon;
}

}  // namespace acr_ht_tests
