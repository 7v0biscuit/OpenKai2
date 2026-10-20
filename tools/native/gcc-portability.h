#pragma once
#include <cstring>
#include <cmath>
// MinGW uses these macros at -O0, while the optimized runtime defines the
// guest functions by their original names. Load the CRT declarations and
// remove the aliases before any guest stub header is parsed.
#ifdef _WIN32
#ifdef strcasecmp
#undef strcasecmp
#endif
#ifdef strncasecmp
#undef strncasecmp
#endif
#endif
// Upstream FPU comparison macros use the unqualified C spelling.
// Expose the standard overload without changing its NaN behavior.
using std::isnan;
