// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <algorithm>
#include <cstdint>
namespace openkai2 {
struct GraphicsSettings {
    unsigned width = 1280, height = 720;
    unsigned textureFilter = 0, upscaler = 1, antialiasing = 0;
    unsigned aspect = 0; // 4:3, 16:9, stretch, native pixels
    bool borderless = false, vsync = true;
};
void set_graphics_settings(const GraphicsSettings &settings);
const GraphicsSettings &graphics_settings() noexcept;
bool texture_filter_enabled(bool gameFilter) noexcept;
int test_presentation_filters();
struct DisplayRectangle {
    float x, y, width, height;
};
inline DisplayRectangle display_rectangle(float screenWidth, float screenHeight, float sourceWidth,
                                          float sourceHeight, unsigned aspect) {
    if (screenWidth <= 0 || screenHeight <= 0 || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    const float ratio = aspect == 0   ? 4.0f / 3.0f
                        : aspect == 1 ? 16.0f / 9.0f
                        : aspect == 2 ? screenWidth / screenHeight
                                      : sourceWidth / sourceHeight;
    const float height = std::min(screenHeight, screenWidth / ratio), width = height * ratio;
    return {(screenWidth - width) * 0.5f, (screenHeight - height) * 0.5f, width, height};
}
} // namespace openkai2
