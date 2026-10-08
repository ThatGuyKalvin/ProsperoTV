// ProsperoTV - The player's settings menu, drawn with the kit over the video.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/kit.hpp"
#include "tv/soft_raster.hpp"

#include "iptv_osd.h"
#include "ui/components/form.hpp"
#include "ui/components/list.hpp"
#include "ui/components/sheet.hpp"

namespace ptv
{

// While a film or a channel plays, OpenGL is not running: the video owns the screen and the
// player draws its controls into an overlay. The settings menu is still the interface's own:
// a sheet like the series drawer, with the Settings screen's form and the drawer's list, in
// Dusk. They are recorded into a draw list as always and drawn by the CPU into the image the
// overlay shows (iptv_osd_set_menu_painter).
class PlayerMenu
{
  public:
    // fonts.*.texture must be font handles (gfx::kFontHandleBase | slot) of these fonts.
    explicit PlayerMenu(const ui::Fonts &fonts);

    // Paints the menu of `state` into image->rgba (IPTV_OSD_MENU_IMAGE_WIDTH x
    // IPTV_OSD_OVERLAY_HEIGHT, the right of the overlay). False when there is nothing to paint.
    bool paint(const iptv_osd_state_t &state, iptv_osd_image_t *image);

    // What the last paint recorded, and where its panel was.
    const gfx::DrawList &recorded() const
    {
        return list_;
    }
    Rect panel() const
    {
        return sheet_.panel_rect();
    }

    // Where the image goes on the overlay.
    static constexpr float kImageLeft = kWidth - static_cast<float>(IPTV_OSD_MENU_IMAGE_WIDTH);

  private:
    void record(const iptv_osd_state_t &state);
    void draw_preview(ui::Canvas &canvas, const Rect &row, const iptv_osd_menu_row_t &item,
                      float focus) const;

    const ui::Fonts &fonts_;
    ui::Theme theme_;
    ui::Sheet sheet_;
    ui::Form form_;
    ui::ListView options_;
    SoftRaster raster_;
    gfx::DrawList list_;
    iptv_osd_state_t shown_{}; // the state being recorded
};

} // namespace ptv
