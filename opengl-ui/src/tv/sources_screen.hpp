// ProsperoTV - Sources: where the channel list comes from, and how it is doing.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/shared.hpp"
#include "ui/components/list.hpp"
#include "ui/components/progress.hpp"
#include "ui/glyphs.hpp"

namespace ptv
{

class SourcesScreen
{
  public:
    explicit SourcesScreen(Shared &shared);

    // Replays the entrance, with the focus where it was.
    void enter();
    // Everything but Back.
    void handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    int hints(ui::Hint *out, int capacity) const;

  private:
    iptv::SourceKind focused() const;
    void draw_row(ui::Canvas &canvas, const Rect &row, iptv::SourceKind source, float focus) const;
    void draw_details(ui::Canvas &canvas) const;

    Shared &shared_;
    ui::ListView list_;
    ui::Spinner spinner_;
    float age_ = 10.0f;
};

} // namespace ptv
