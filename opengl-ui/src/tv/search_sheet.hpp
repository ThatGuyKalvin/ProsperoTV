// ProsperoTV - Search and filter: a drawer over the channel list it narrows.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/shared.hpp"
#include "ui/components/search_field.hpp"
#include "ui/components/select.hpp"
#include "ui/components/sheet.hpp"
#include "ui/components/tabs.hpp"
#include "ui/glyphs.hpp"

namespace ptv
{

// The list behind the drawer answers every change at once, so there is
// nothing to apply: closing it only puts the drawer away.
class SearchSheet
{
  public:
    explicit SearchSheet(Shared &shared);

    void open(ui::Feedback &feedback);
    // Without a sound or an exit (the menu is closing).
    void dismiss();
    bool is_open() const
    {
        return sheet_.is_open();
    }

    // While open it takes every input.
    void handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    enum class Zone
    {
        field,
        country,
        category,
        language,
        quality,
        show,
        reset,
    };

    bool usable(Zone zone) const;
    void go(Zone zone, ui::Feedback &feedback);
    void step(int direction, ui::Feedback &feedback);
    // The dropdowns and the field as the model has them.
    void sync();
    void reset(ui::Feedback &feedback);
    Rect area() const;
    ui::Select &select_of(Zone zone);
    void draw_content(ui::Canvas &canvas, float opacity) const;
    void draw_unlisted(ui::Canvas &canvas, const Rect &row, const char *label) const;

    Shared &shared_;
    ui::Sheet sheet_;
    ui::SearchField field_;
    ui::Select country_;
    ui::Select category_;
    ui::Select language_;
    ui::TabBar quality_;
    Zone zone_ = Zone::field;
    unsigned seen_revision_ = 0;
    tween::Spring show_focus_;
    tween::Spring reset_focus_;
    ui::Pulse press_;
    tween::Spring count_; // the number of matches, counting toward the new total
};

} // namespace ptv
