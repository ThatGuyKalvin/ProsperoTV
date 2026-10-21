// ProsperoTV - Sources: where the channel list comes from, and how it is doing.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/shared.hpp"
#include "ui/components/list.hpp"
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
    std::int64_t focused_id() const;
    bool choosing_user_agent() const
    {
        return http_source_ != 0;
    }
    bool at_top() const
    {
        return !choosing_user_agent() && list_.focus() <= 0;
    }

  private:
    void sync();

    Shared &shared_;
    ui::ListView list_;
    ui::ListView user_agents_;
    std::int64_t http_source_ = 0;
    unsigned seen_revision_ = 0;
    std::vector<std::int64_t> ids_;
    std::vector<std::string> usb_files_;
};

} // namespace ptv
