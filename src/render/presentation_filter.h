// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "raylib.h"
namespace openkai2 {
class PresentationFilter {
  public:
    ~PresentationFilter() { close(); }
    void draw(Texture2D frame, Rectangle source, Rectangle destination);
    void close();

  private:
    void drawFsr(Texture2D frame, Rectangle source, Rectangle destination);
    Shader edge_{}, cubic_{}, linear_{}, easu_{}, rcas_{};
    RenderTexture2D target_{}, fsrTarget_{};
};
int test_presentation_filters();
} // namespace openkai2
