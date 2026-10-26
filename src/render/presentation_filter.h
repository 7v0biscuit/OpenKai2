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
    Shader edge_{}, cubic_{}, linear_{};
    RenderTexture2D target_{};
};
int test_presentation_filters();
} // namespace openkai2
