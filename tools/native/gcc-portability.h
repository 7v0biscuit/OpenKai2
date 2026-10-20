#pragma once
#include <cmath>
// Upstream FPU comparison macros use the unqualified C spelling.
// Expose the standard overload without changing its NaN behavior.
using std::isnan;
