#pragma once
#include <cstring>
#include <cmath>
#ifdef _WIN32
#ifdef strcasecmp
#undef strcasecmp
#endif
#ifdef strncasecmp
#undef strncasecmp
#endif
#endif
using std::isnan;
