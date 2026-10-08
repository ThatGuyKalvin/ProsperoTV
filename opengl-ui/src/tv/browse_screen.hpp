// ProsperoTV - Live TV and Favorites: the channel in focus as a hero, the list as a grid.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/shared.hpp"
#include "ui/components/grid.hpp"
#include "ui/components/stat.hpp"
#include "ui/components/tabs.hpp"
#include "ui/glyphs.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ptv
{

class BrowseScreen
{
  public:
    enum class Result
    {
        none,
        play,    // a channel was queued in the model
        search,  // open the search sheet
        go_live, // the favorites list is empty: go and find some
        series,  // a series was opened in the model: show its episodes
    };

    explicit BrowseScreen(Shared &shared);

    // Which list the screen shows from now on (the whole catalog by groups,
    // or the favorites), with the focus back on the channel it was on.
    void show(bool favorites);
    // Replays the entrance.
    void enter();

    // Everything but Back, which the app shares out through back().
    Result handle(const InputFrame &input, ui::Feedback &feedback);
    // One step back inside the screen. False when there is nowhere to go.
    bool back(ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;
    int hints(ui::Hint *out, int capacity) const;

    // The channel in focus, or null: the backdrop takes its colours.
    // The channel in focus, to read and let go: nothing when the list is empty.
    std::optional<iptv::ChannelView> focused() const;
    bool showing_favorites() const
    {
        return favorites_;
    }
    // The focus is on the letters at the right of the list.
    bool on_letters() const
    {
        return zone_ == Zone::rail;
    }

  private:
    enum class Zone
    {
        grid,
        groups,
        rail, // the letters at the right
    };

    unsigned focused_index() const;
    int current_letter() const;
    // L2 or R2 this frame: a press, or one more step of a hold.
    int page_turn(const InputFrame &input, bool *repeat);
    // The focus went from `before` to where it is: the hero follows, with its
    // hand-over or (in a run of steps) at once.
    void show_focus(int before, bool animate);
    void draw_rail(ui::Canvas &canvas) const;
    void apply_group(int chip);
    void restart_list();
    void begin_swap(int from);
    float appear(int order) const;
    void draw_hero_text(ui::Canvas &canvas, const iptv::ChannelView &channel, unsigned index,
                        bool favorite, float alpha, float dx) const;
    void draw_title_text(ui::Canvas &canvas, const iptv::ChannelView &title, bool favorite,
                         float alpha, float dx) const;
    void draw_hero_art(ui::Canvas &canvas, const iptv::ChannelView &channel, float alpha) const;
    // The lists of the shelf on screen, as chips.
    void set_chips();
    int chip_of(Group group) const;
    const char *shelf_title() const;
    // The shelf's list could not be had, or is on its way.
    bool list_failed() const;
    bool list_busy() const;
    void draw_list_header(ui::Canvas &canvas) const;
    void draw_waiting(ui::Canvas &canvas) const;

    Shared &shared_;
    ui::GridView grid_;
    ui::TabBar groups_;
    ui::EmptyState empty_;
    bool favorites_ = false;
    std::vector<Group> chips_;
    Zone zone_ = Zone::grid;
    // The title the focus rests on, and for how long: its details are asked
    // for once it has stayed.
    std::string dwell_id_;
    float dwell_ = 0.0f;
    bool dwell_asked_ = false;
    unsigned seen_revision_ = 0;
    bool keep_place_ = false; // the list is about to lose the focused channel: stay put
    std::string focused_id_;
    float age_ = 10.0f;
    // The hero changes hands when the focus moves: the channel that leaves
    // and how far along the hand-over is.
    iptv::Channel previous_;
    unsigned previous_index_ = 0;
    bool previous_favorite_ = false;
    bool has_previous_ = false;
    float travel_ = 1.0f;
    tween::Timer swap_;
    ui::Pulse star_;
    // The plate under the letter of the channel in focus glides along the rail.
    tween::Spring rail_y_;
    tween::Spring rail_focus_;
    // A held L2 or R2 keeps turning pages: which way, for how long, and when
    // the next page is due.
    int page_dir_ = 0;
    float page_hold_ = 0.0f;
    float page_due_ = 0.0f;
    bool handled_ = false;
};

} // namespace ptv
