// ProsperoTV - A series: its seasons and episodes, in a drawer over the list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/shared.hpp"
#include "ui/components/list.hpp"
#include "ui/components/sheet.hpp"
#include "ui/components/tabs.hpp"
#include "ui/glyphs.hpp"

#include <cstdint>
#include <vector>

namespace ptv
{

// Shows the series open in the model (Model::open_series): the seasons as
// chips, the episodes of one as a list, and where to go on watching.
class SeriesSheet
{
  public:
    enum class Result
    {
        none,
        play,   // an episode was queued in the model
        closed, // back: the series is put away
    };

    explicit SeriesSheet(Shared &shared);

    void open(ui::Feedback &feedback);
    // Without a sound or an exit (the menu is closing).
    void dismiss();
    bool is_open() const
    {
        return sheet_.is_open();
    }
    bool visible() const
    {
        return sheet_.visible();
    }

    // While open it takes every input.
    Result handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    int hints(ui::Hint *out, int capacity) const;

  private:
    enum class Zone
    {
        seasons,
        episodes,
    };

    // The model's episodes have come (or changed): the chips and the list follow.
    void sync();
    void show_season(int chip, bool snap);
    Rect area() const;
    void draw_content(ui::Canvas &canvas, float opacity) const;

    Shared &shared_;
    ui::Sheet sheet_;
    ui::TabBar seasons_;
    ui::ListView list_;
    Zone zone_ = Zone::episodes;
    // What the list shows: episode indices of the model's episodes, in order.
    std::vector<unsigned> shown_;
    std::uint16_t season_ = 0;
    LibraryState seen_state_ = LibraryState::none;
    std::string seen_series_;
};

} // namespace ptv
