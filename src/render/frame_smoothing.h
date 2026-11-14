// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "runtime/gs/gs_backend.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace openkai2 {

enum class FrameSmoothing : unsigned { off = 0, smooth = 1, low_latency = 2 };

using RasterFactory = std::function<std::unique_ptr<GSRasterBackend>()>;
std::unique_ptr<GSRasterBackend> make_smoothing_backend(RasterFactory factory, FrameSmoothing mode);

void set_display_refresh(double hertz);
unsigned smoothing_pictures_per_frame();
uint64_t smoothed_generation();
bool copy_smoothed_picture(std::vector<uint8_t> &pixels, uint32_t &width, uint32_t &height, uint32_t *displayFbp,
                           uint32_t *sourceFbp, bool *usedPreferred);

namespace smoothing {
struct Match {
    std::vector<int32_t> previous;
    std::vector<std::array<std::array<float, 2>, 3>> motion;
    std::vector<uint8_t> carried;
    size_t matched = 0, jumped = 0, carriedCount = 0;
};
uint64_t primitive_key(const GSPrimitiveBatch &batch);
Match match_primitives(const std::vector<GSPrimitiveBatch> &previous, const std::vector<GSPrimitiveBatch> &current,
                       float jumpLimit);
GSPrimitiveBatch blend(const GSPrimitiveBatch &previous, const GSPrimitiveBatch &current, float amount, bool predict);
GSPrimitiveBatch carry(const GSPrimitiveBatch &current, const std::array<std::array<float, 2>, 3> &motion, float amount, bool predict);
void verify();
}

}
