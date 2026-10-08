// ProsperoTV - A series: its seasons and episodes, in a drawer over the list.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/series_sheet.hpp"

#include "tv/draw.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace ptv
{

namespace
{

constexpr float kSheetWidth = 1040.0f;
constexpr float kChipsHeight = 48.0f;
// A row's tag for the one that goes on watching (episode rows carry their index).
constexpr int kContinueTag = -1;

std::string season_name(std::uint16_t season)
{
    return season == 0 ? std::string("Specials") : "Season " + std::to_string(season);
}

std::string minutes_words(std::uint32_t seconds)
{
    const std::uint32_t minutes = (seconds + 30u) / 60u;
    return minutes == 0 ? std::string() : std::to_string(minutes) + " min";
}

std::string clock_words(std::uint32_t seconds)
{
    char text[24]{};
    if (seconds >= 3600u)
        std::snprintf(text, sizeof(text), "%u:%02u:%02u", seconds / 3600u, seconds / 60u % 60u,
                      seconds % 60u);
    else
        std::snprintf(text, sizeof(text), "%u:%02u", seconds / 60u, seconds % 60u);
    return text;
}

// "E03  The title", without the "S01 E03" a provider puts in front of it.
std::string episode_title(const iptv::ChannelView &episode)
{
    char label[32]{};
    std::snprintf(label, sizeof(label), "S%02u E%02u", static_cast<unsigned>(episode.season),
                  static_cast<unsigned>(episode.episode));
    std::string title(episode.name);
    if (title.rfind(label, 0) == 0)
        title.erase(0, std::char_traits<char>::length(label));
    while (!title.empty() && (title.front() == ' ' || title.front() == '-'))
        title.erase(0, 1);
    char number[16]{};
    std::snprintf(number, sizeof(number), "E%02u", static_cast<unsigned>(episode.episode));
    return title.empty() ? std::string(number) : std::string(number) + "  " + title;
}

} // namespace

SeriesSheet::SeriesSheet(Shared &shared) : shared_(shared)
{
    const ui::Theme &theme = shared.theme;
    sheet_.style.theme = theme;
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = kSheetWidth;
    sheet_.style.padding = 48.0f;
    sheet_.style.title_size = 40.0f;
    sheet_.style.scrim = 0.55f;
    sheet_.style.scrim_color = tone::night;
    sheet_.content = [this](ui::Canvas &canvas, const Rect &, float opacity)
    { draw_content(canvas, opacity); };

    seasons_.style.theme = theme;
    seasons_.style.kind = ui::TabKind::pill;
    seasons_.style.height = kChipsHeight;
    seasons_.style.text_size = 21.0f;
    seasons_.style.padding = 20.0f;
    seasons_.style.gap = 10.0f;
    seasons_.style.track = false;
    seasons_.set_focused(false);

    list_.style.theme = theme;
    list_.style.highlight.kind = ui::HighlightKind::bar;
    list_.style.row_height = 84.0f;
    list_.style.title_size = 26.0f;
    list_.style.subtitle_size = 20.0f;
    list_.style.value_size = 22.0f;
    list_.style.cards = true;

    const Rect inside = area();
    seasons_.set_bounds({inside.x, inside.y, inside.w, kChipsHeight});
    list_.set_bounds(
        {inside.x, inside.y + kChipsHeight + 24.0f, inside.w, inside.h - kChipsHeight - 24.0f});
}

Rect SeriesSheet::area() const
{
    Rect inside = sheet_.content_rect();
    inside.h = std::min(inside.h, kHeight - 90.0f - inside.y);
    return inside;
}

void SeriesSheet::open(ui::Feedback &feedback)
{
    seen_series_.clear();
    seen_state_ = LibraryState::none;
    sync();
    sheet_.open(feedback);
}

void SeriesSheet::dismiss()
{
    sheet_.dismiss();
}

// The chips and the list as the model has them. A new series (or its episodes
// arriving) starts at the episode to go on with.
void SeriesSheet::sync()
{
    const Model &model = shared_.model;
    if (sheet_.title() != model.series_name())
    {
        // The title takes its room at the top: the chips and the list go under it.
        sheet_.set_title(model.series_name());
        const Rect inside = area();
        seasons_.set_bounds({inside.x, inside.y, inside.w, kChipsHeight});
        list_.set_bounds(
            {inside.x, inside.y + kChipsHeight + 24.0f, inside.w, inside.h - kChipsHeight - 24.0f});
    }
    if (model.series_id() == seen_series_ && model.episodes_state() == seen_state_)
        return;
    seen_series_ = model.series_id();
    seen_state_ = model.episodes_state();
    std::vector<ui::TabItem> chips;
    for (const std::uint16_t season : model.seasons())
        chips.push_back({season_name(season)});
    seasons_.set_tabs(std::move(chips));
    zone_ = Zone::episodes;
    if (model.episodes_state() != LibraryState::ready || model.seasons().empty())
    {
        shown_.clear();
        list_.set_items({});
        return;
    }
    // The season of the episode to go on with, else the view's, else the first.
    const int next = model.continue_episode();
    std::uint16_t season = model.seasons().front();
    if (next >= 0)
        season = model.episodes()[static_cast<unsigned>(next)].season;
    else if (shared_.model.view.open_season > 0)
        season = static_cast<std::uint16_t>(shared_.model.view.open_season);
    const auto found = std::find(model.seasons().begin(), model.seasons().end(), season);
    show_season(found != model.seasons().end() ? static_cast<int>(found - model.seasons().begin())
                                               : 0,
                true);
}

void SeriesSheet::show_season(int chip, bool snap)
{
    Model &model = shared_.model;
    if (model.seasons().empty())
        return;
    chip = std::clamp(chip, 0, static_cast<int>(model.seasons().size()) - 1);
    seasons_.set_active(chip, snap);
    season_ = model.seasons()[static_cast<std::size_t>(chip)];
    model.view.open_season = season_;
    shown_ = model.season_episodes(season_);

    const int next = model.continue_episode();
    std::vector<ui::ListItem> items;
    int focus = 0;
    // Go on where it was left: the first row, when that episode is in this season.
    if (next >= 0 && model.episodes()[static_cast<unsigned>(next)].season == season_ &&
        model.series_started())
    {
        const iptv::ChannelView episode = model.episodes()[static_cast<unsigned>(next)];
        ui::ListItem row;
        row.title = "Continue  \xC2\xB7  " + episode_title(episode);
        const std::uint32_t left_at = model.resume_secs(episode.id);
        row.subtitle = left_at != 0 ? "From " + clock_words(left_at) : "The next episode";
        row.tag = kContinueTag;
        row.chevron = true;
        items.push_back(std::move(row));
    }
    for (const unsigned index : shown_)
    {
        const iptv::ChannelView episode = model.episodes()[index];
        ui::ListItem row;
        row.title = readable(shared_.fonts.semibold, episode_title(episode));
        const std::uint32_t left_at = model.resume_secs(episode.id);
        if (left_at != 0)
            row.subtitle = "Left off at " + clock_words(left_at);
        else if (model.is_recent(episode))
            row.subtitle = "Watched";
        row.value = minutes_words(episode.duration_secs);
        row.tag = static_cast<int>(index);
        if (next >= 0 && index == static_cast<unsigned>(next) && items.empty())
            focus = static_cast<int>(items.size());
        items.push_back(std::move(row));
    }
    list_.set_items(std::move(items));
    list_.set_focus(focus, true);
    list_.enter();
}

SeriesSheet::Result SeriesSheet::handle(const InputFrame &input, ui::Feedback &feedback)
{
    Model &model = shared_.model;
    sync();
    if (sheet_.handle(input, feedback) == ui::Event::cancelled)
    {
        model.close_series();
        model.view.open_series.clear();
        return Result::closed;
    }
    if (model.episodes_state() == LibraryState::failed)
    {
        if (input.is_pressed(Action::confirm))
        {
            feedback.play(audio::Cue::select);
            model.retry_series();
        }
        return Result::none;
    }
    if (model.episodes_state() != LibraryState::ready || list_.items().empty())
        return Result::none;

    if (zone_ == Zone::seasons)
    {
        if (input.nav == Direction::down || input.is_pressed(Action::confirm))
        {
            zone_ = Zone::episodes;
            feedback.play(audio::Cue::focus, 0.97f);
            return Result::none;
        }
        if (seasons_.handle(input, feedback) == ui::Event::changed)
            show_season(seasons_.active(), false);
        return Result::none;
    }

    if (input.nav == Direction::up && list_.focus() == 0 && model.seasons().size() > 1)
    {
        zone_ = Zone::seasons;
        feedback.play(audio::Cue::focus, 1.03f);
        return Result::none;
    }
    // L2 and R2 go through the seasons from the list too.
    const int turn = input.is_pressed(Action::jump_next)   ? 1
                     : input.is_pressed(Action::jump_prev) ? -1
                                                           : 0;
    if (turn != 0)
    {
        if (seasons_.step(turn, input, feedback) == ui::Event::changed)
            show_season(seasons_.active(), false);
        return Result::none;
    }
    if (list_.handle(input, feedback) == ui::Event::activated)
    {
        const int tag = list_.items()[static_cast<std::size_t>(list_.focus())].tag;
        const int episode = tag == kContinueTag ? model.continue_episode() : tag;
        if (episode >= 0 && model.play_episode(static_cast<unsigned>(episode)))
        {
            model.view.open_series = model.series_id();
            return Result::play;
        }
        feedback.play(audio::Cue::error);
    }
    return Result::none;
}

void SeriesSheet::update(float dt)
{
    sync();
    const bool reduced = shared_.settings.reduced_motion;
    sheet_.style.reduced_motion = reduced;
    seasons_.style.reduced_motion = reduced;
    list_.style.reduced_motion = reduced;
    seasons_.set_focused(sheet_.is_open() && zone_ == Zone::seasons);
    list_.set_active(sheet_.is_open() && zone_ == Zone::episodes);
    sheet_.update(dt);
    seasons_.update(dt);
    list_.update(dt);
}

void SeriesSheet::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
}

void SeriesSheet::draw_content(ui::Canvas &canvas, float) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = shared_.theme;
    const Model &model = shared_.model;
    const Rect inside = area();
    switch (model.episodes_state())
    {
    case LibraryState::ready:
        if (list_.items().empty())
        {
            ui::text(list, fonts.semibold, "This series has no episodes yet", inside.x,
                     inside.y + 40.0f, 28.0f, theme.text);
            return;
        }
        if (model.seasons().size() > 1)
            seasons_.draw(canvas);
        else
            ui::text(list, fonts.semibold, ui::upper(season_name(season_)), inside.x,
                     baseline_for(inside.y + kChipsHeight * 0.5f, 18.0f), 18.0f, tone::accent,
                     gfx::Align::left, 4.0f);
        list_.draw(canvas);
        return;
    case LibraryState::failed:
        ui::text(list, fonts.semibold, "The episodes could not be read", inside.x, inside.y + 40.0f,
                 28.0f, theme.text);
        ui::paragraph(list, fonts.regular, model.episodes_error(), inside.x, inside.y + 84.0f,
                      23.0f, inside.w, 30.0f, theme.text_muted, 3);
        return;
    default:
        ui::text(list, fonts.semibold, "Reading the episodes\xE2\x80\xA6", inside.x,
                 inside.y + 40.0f, 28.0f, theme.text);
        for (int i = 0; i < 6; ++i)
            list.rounded_rect(
                {inside.x, inside.y + 90.0f + static_cast<float>(i) * 92.0f, inside.w, 84.0f},
                16.0f, kWhite.with_alpha(0.06f));
        return;
    }
}

int SeriesSheet::hints(ui::Hint *out, int capacity) const
{
    const Model &model = shared_.model;
    int count = 0;
    const auto add = [&](ui::Hint hint)
    {
        if (count < capacity)
            out[count++] = hint;
    };
    if (model.episodes_state() == LibraryState::ready && !list_.items().empty())
    {
        add({ui::Button::cross, "Watch"});
        if (model.seasons().size() > 1)
            add({ui::Button::l2, "Season", ui::Button::r2});
    }
    else if (model.episodes_state() == LibraryState::failed)
    {
        add({ui::Button::cross, "Try again"});
    }
    add({ui::Button::circle, "Back"});
    return count;
}

} // namespace ptv
