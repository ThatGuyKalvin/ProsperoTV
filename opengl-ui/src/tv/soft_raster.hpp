// ProsperoTV - The kit's draw lists drawn by the CPU, for where OpenGL is not running.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/kit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ptv
{

// Draws a gfx::DrawList into premultiplied RGBA the way the kit's batch shader draws it on the
// GPU: the same distance fields, the same anti-aliasing, the same blending. The player's
// overlay is drawn this way while the video owns the screen. Textures other than fonts (images,
// glass) are not drawn; the screens that use this do not use them.
class SoftRaster
{
  public:
    // Where to draw: a virtual pixel (x, y) lands on the target's pixel (x - origin_x,
    // y - origin_y). One virtual pixel is one target pixel.
    struct Target
    {
        std::uint8_t *rgba = nullptr;
        int width = 0;
        int height = 0;
        std::size_t stride = 0; // bytes a row
        int origin_x = 0;
        int origin_y = 0;
    };

    // The part of the target that was drawn in (empty when x1 <= x0).
    struct Box
    {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
    };

    // The font a font handle (gfx::kFontHandleBase | slot) names; slots 1..6.
    void set_font(unsigned slot, const gfx::Font *font);

    Box draw(const gfx::DrawList &list, const Target &target) const;

  private:
    std::array<const gfx::Font *, 8> fonts_{};
};

} // namespace ptv
