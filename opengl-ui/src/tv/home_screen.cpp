// ProsperoTV - Continue watching, favorites and usual channels.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/home_screen.hpp"
#include "tv/draw.hpp"
#include "tv/i18n.hpp"
#include <algorithm>
namespace ptv
{
void HomeScreen::update()
{
    if (revision_ == shared_.model.revision())
        return;
    revision_ = shared_.model.revision();
    std::array<std::string, 3> selected;
    for (unsigned i = 0; i < rows_.size(); ++i)
        if (!rows_[i].empty())
            selected[i] = rows_[i][focus_[i]].id;
    rows_ = shared_.model.home_items();
    for (unsigned i = 0; i < rows_.size(); ++i)
    {
        for (unsigned row = 0; row < rows_[i].size(); ++row)
            if (rows_[i][row].id == selected[i])
                focus_[i] = static_cast<int>(row);
        focus_[i] = std::clamp(focus_[i], 0, std::max(0, int(rows_[i].size()) - 1));
        top_[i] = std::clamp(top_[i], std::max(0, focus_[i] - 4), focus_[i]);
    }
}
void HomeScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    update();
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        column_ = std::clamp(column_ + (input.nav == Direction::left ? -1 : 1), 0, 2);
        feedback.play(audio::Cue::focus);
    }
    else if (input.nav == Direction::up || input.nav == Direction::down ||
             input.is_pressed(Action::jump_prev) || input.is_pressed(Action::jump_next))
    {
        const int move = input.is_pressed(Action::jump_prev)   ? -5
                         : input.is_pressed(Action::jump_next) ? 5
                         : input.nav == Direction::up          ? -1
                                                               : 1;
        focus_[column_] =
            std::clamp(focus_[column_] + move, 0, std::max(0, int(rows_[column_].size()) - 1));
        top_[column_] =
            std::clamp(top_[column_], std::max(0, focus_[column_] - 4), focus_[column_]);
        feedback.play(audio::Cue::focus);
    }
    else if (input.is_pressed(Action::confirm) && !rows_[column_].empty())
    {
        const auto &item = rows_[column_][focus_[column_]];
        const bool ok = shared_.model.play_home(item.id, item.on_demand);
        feedback.play(ok ? audio::Cue::select : audio::Cue::error);
        if (!ok)
            shared_.toasts.push(ui::StatusKind::warning, tr("Channel unavailable or locked"));
    }
}
std::vector<std::string> HomeScreen::image_urls() const
{
    std::vector<std::string> result;
    for (unsigned column = 0; column < rows_.size(); ++column)
        for (int row = top_[column]; row < int(rows_[column].size()) && row < top_[column] + 5;
             ++row)
            if (!rows_[column][row].logo.empty())
                result.push_back(rows_[column][row].logo);
    return result;
}
void HomeScreen::draw(ui::Canvas &canvas) const
{
    const auto &theme = shared_.theme;
    const auto &fonts = canvas.fonts;
    auto &list = canvas.list;
    const char *titles[] = {"Continue watching", "Favorites", "On your usual channels"};
    const char *empty[] = {
        "Your unfinished movies, episodes and last live channel appear here.",
        "Favorite a channel in Live TV to keep it here.",
        "Watch channels to build this list. Programme details appear when a guide is available."};
    for (unsigned column = 0; column < rows_.size(); ++column)
    {
        const float x = kMargin + column * 584;
        ui::text(list, fonts.semibold, tr(titles[column]), x, 204, 27, theme.text);
        if (rows_[column].empty())
        {
            list.bordered_rect({x, 232, 552, 592}, 18, kWhite.with_alpha(.025f),
                               int(column) == column_ ? 2 : 0, tone::accent);
            if (int(column) == column_)
                list.rounded_rect({x + 8, 252, 5, 52}, 2, tone::accent);
            ui::paragraph(list, fonts.regular, tr(empty[column]), x + 24, 298, 24, 504, 38,
                          theme.text_muted, 8);
        }
        for (int row = top_[column]; row < int(rows_[column].size()) && row < top_[column] + 5;
             ++row)
        {
            const auto &item = rows_[column][row];
            const float y = 232 + (row - top_[column]) * 120;
            const bool selected = int(column) == column_ && row == focus_[column];
            list.bordered_rect({x, y, 552, 112}, 15, kWhite.with_alpha(selected ? .12f : .035f),
                               selected ? 2 : 0, tone::accent);
            if (selected)
                list.rounded_rect({x + 7, y + 14, 5, 84}, 2, tone::accent);
            const auto image = shared_.images.find(item.logo);
            if (image.id)
            {
                const float scale = std::min(60.f / image.width, 68.f / image.height);
                list.image(image.id,
                           {x + 16 + (60 - image.width * scale) / 2,
                            y + 18 + (68 - image.height * scale) / 2, image.width * scale,
                            image.height * scale},
                           {0, 0, 1, 1}, kWhite);
            }
            const float text_x = x + (image.id ? 92 : 18), width = x + 532 - text_x;
            const auto &title = face_for(fonts, fonts.semibold, item.title);
            const auto &detail = face_for(fonts, fonts.regular, item.detail);
            ui::text(list, title, title.font->fit(readable(title, item.title), 24, width), text_x,
                     y + 34, 24, theme.text);
            ui::text(list, detail, detail.font->fit(readable(detail, item.detail), 20, width),
                     text_x, y + 66, 20, theme.text_muted);
            if (item.on_demand)
            {
                const auto minutes = item.position_us / 60000000;
                const auto text = std::to_string(minutes) + tr(" min watched");
                ui::text(list, fonts.regular, text, text_x, y + 94, 18, theme.text_muted);
                if (item.duration_us)
                {
                    const float fraction =
                        std::clamp(float(double(item.position_us) / item.duration_us), 0.f, 1.f);
                    list.rounded_rect({x + 18, y + 106, 516 * fraction, 3}, 1, tone::accent);
                }
            }
        }
        if (!rows_[column].empty())
            ui::text(list, fonts.regular,
                     std::to_string(focus_[column] + 1) + " / " +
                         std::to_string(rows_[column].size()),
                     x, 864, 19, theme.text_muted);
    }
    const auto *source = shared_.model.saved_source(shared_.model.selected_source_id());
    const std::string scope =
        std::string(tr("Current source")) + ": " + (source ? source->name : tr("None yet"));
    const auto &face = face_for(fonts, fonts.regular, scope);
    ui::text(list, face, face.font->fit(readable(face, scope), 22, 1728), kMargin, 931, 22,
             theme.text_muted);
}
} // namespace ptv
