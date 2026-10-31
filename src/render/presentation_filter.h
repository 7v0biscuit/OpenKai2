// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "raylib.h"
#include <array>
#include <cstdint>
namespace openkai2 {
class PresentationFilter {
  public:
    ~PresentationFilter() { close(); }
    void draw(Texture2D frame, Rectangle source, Rectangle destination, uint64_t generation = 0);
    void close();

  private:
    void drawImage(Texture2D frame, Rectangle source, Rectangle destination, bool cacheOutput);
    void drawFsr(Texture2D frame, Rectangle source, Rectangle destination, bool cacheOutput);
    void drawCached(Rectangle destination);
    std::array<uint64_t,12> cacheKey_{};
    bool cacheValid_ = false;
    uint64_t processed_ = 0, reused_ = 0;
    Shader edge_{}, cubic_{}, linear_{}, easu_{}, rcas_{};
    RenderTexture2D target_{}, fsrTarget_{}, output_{};
};
int test_presentation_filters();
}
