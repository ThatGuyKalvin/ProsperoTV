// ProsperoTV - The player's settings menu, drawn with the kit over the video.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tv/kit.hpp"
#include "tv/settings.hpp"
#include "tv/soft_raster.hpp"

#include "iptv_subtitles.h"
#include "ui/components/form.hpp"
#include "ui/components/list.hpp"
#include "ui/components/sheet.hpp"
#include "ui/glyphs.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ptv
{

// One line of the menu. The settings page holds headers, pickers (a value that opens a list)
// and the sound delay stepper; a picker's list holds options, the one in use ticked.
struct SheetRow
{
    enum class Kind
    {
        header,
        picker,
        stepper,
        option,
    };
    // What an option shows at its right: the picture's shape, the picture stretched, a 4:3
    // picture zoomed on a 16:9 screen, or a colour.
    enum class Preview
    {
        none,
        shape,
        stretch,
        zoom,
        swatch,
    };
    Kind kind = Kind::option;
    std::string label, value;
    bool checked = false;
    Preview preview = Preview::none;
    unsigned milli = 0;    // shape: width / height x 1000; zoom: scale x 1000, 0 to fill
    std::uint32_t rgb = 0; // swatch
    int step = 0, low = 0, high = 0, current = 0; // stepper
    bool operator==(const SheetRow &) const = default;
};

struct SheetModel
{
    bool picker = false;
    std::string title;
    unsigned selected = 0;
    std::vector<SheetRow> rows;
    std::string confirm, back; // a list's hints; "Select" and "Back" when empty
    bool operator==(const SheetModel &) const = default;
};

// The controls along the bottom while a video plays: what is on, where it has got to, and
// what the buttons do, each shown by the button's own shape.
struct ControlsModel
{
    struct Hint
    {
        ui::Button button = ui::Button::none;
        ui::Button second = ui::Button::none; // pairs such as L1 / R1
        std::string label;
        bool operator==(const Hint &) const = default;
    };
    std::string title, line, detail;
    std::string chip; // "Paused", "Live", "2x" ...
    bool chip_lit = false;
    int progress = -1; // 0..1000 along the timeline, -1 for none
    std::string elapsed, total;
    std::vector<Hint> hints;
    bool operator==(const ControlsModel &) const = default;
};

// While a video plays OpenGL is not running and the picture owns the screen, so the menu is
// recorded into a draw list as the screens are, drawn by the CPU and composited onto the
// presenter's copy of the picture like a subtitle (composite_subtitle_bitmaps).
class PlayerSheet
{
  public:
    // fonts.*.texture must be font handles (gfx::kFontHandleBase | slot).
    PlayerSheet(const ui::Fonts &fonts, const Settings &settings);

    // The menu as one straight-alpha picture on a 1920 x 1080 canvas; false when nothing shows.
    bool paint(const SheetModel &model, iptv::SubtitleCue &cue);
    // The controls, and the speed alone at the top right when they are hidden.
    bool paint_controls(const ControlsModel &model, iptv::SubtitleCue &cue);
    bool paint_badge(const std::string &text, iptv::SubtitleCue &cue);

    const gfx::DrawList &recorded() const
    {
        return frame_.scene;
    }
    Rect panel() const
    {
        return sheet_.panel_rect();
    }

  private:
    void record(const SheetModel &model);
    bool rasterize(iptv::SubtitleCue &cue);
    void draw_preview(ui::Canvas &canvas, const Rect &row, const SheetRow &item, float focus) const;

    const ui::Fonts &fonts_;
    Settings settings_;
    ui::Theme theme_;
    ui::Sheet sheet_;
    ui::Form form_;
    ui::ListView options_;
    SoftRaster raster_;
    Frame frame_;
    SheetModel shown_;
    std::vector<std::uint8_t> pixels_;
};

// "16:9", "2.39:1", ...: a picture's shape by name, empty when unknown.
std::string shape_name(unsigned milli);

} // namespace ptv
