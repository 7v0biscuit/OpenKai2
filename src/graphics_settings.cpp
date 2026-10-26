// SPDX-License-Identifier: GPL-3.0-only
#include "graphics_settings.h"
#include <stdexcept>
namespace openkai2 {
namespace {
GraphicsSettings options;
}
void set_graphics_settings(const GraphicsSettings &s) {
    if (s.width < 640 || s.width > 7680 || s.height < 448 || s.height > 4320 ||
        s.textureFilter > 2 || s.upscaler > 2 || s.antialiasing > 1 || s.aspect > 3)
        throw std::runtime_error("Invalid display settings");
    options = s;
}
const GraphicsSettings &graphics_settings() noexcept { return options; }
bool texture_filter_enabled(bool original) noexcept {
    return options.textureFilter == 0 ? original : options.textureFilter == 2;
}
} // namespace openkai2
