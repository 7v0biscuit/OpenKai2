// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace openkai2 {
struct RasterShadingResult {uint64_t hash; double nanosecondsPerDraw;};
RasterShadingResult raster_shading_workload(unsigned mode,unsigned iterations,unsigned workers=1u);
void verify_raster_shading();
}
