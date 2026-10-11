// ProsperoTV - Channel and time grid.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include "tv/guide_sheet.hpp"
#include "tv/draw.hpp"
#include "tv/platform.hpp"
#include <algorithm>

namespace ptv
{
namespace
{
constexpr unsigned kRows = 7;
constexpr std::int64_t kWindow = 2 * 3600;
constexpr float kTimeX = 435, kTimeWidth = 1360, kRowY = 304, kRowHeight = 70;
} // namespace
void GuideSheet::open(std::string_view channel, ui::Feedback &feedback, std::int64_t time)
{
    open_ = true;
    actions_ = jobs_ = false;
    channel_ = channel;
    row_ = static_cast<unsigned>(std::max(0, shared_.model.position_of(channel)));
    top_ = row_;
    time_ = time ? time : static_cast<std::int64_t>(platform::unix_time());
    left_ = time_ / 1800 * 1800;
    revision_ = shared_.model.revision();
    keep_in_view();
    feedback.play(audio::Cue::open);
}
void GuideSheet::keep_in_view()
{
    const auto &model = shared_.model;
    row_ = std::min(row_, model.visible_count() ? model.visible_count() - 1 : 0);
    if (row_ < top_)
        top_ = row_;
    if (row_ >= top_ + kRows)
        top_ = row_ - kRows + 1;
    if (time_ < left_ || time_ >= left_ + kWindow)
        left_ = time_ / 1800 * 1800;
    if (model.visible_count())
        channel_ = model.channel(model.visible(row_)).id;
}
void GuideSheet::update()
{
    if (!open_)
        return;
    if (revision_ != shared_.model.revision())
    {
        row_ = static_cast<unsigned>(std::max(0, shared_.model.position_of(channel_)));
        revision_ = shared_.model.revision();
    }
    keep_in_view();
}
const Programme *GuideSheet::selected() const
{
    const auto &model = shared_.model;
    if (row_ >= model.visible_count())
        return nullptr;
    return model.guide().now(model.channel(model.visible(row_)).id, time_);
}
void GuideSheet::handle(const InputFrame &input, ui::Feedback &feedback)
{
    auto &model = shared_.model;
    if (actions_ || jobs_)
    {
        const auto jobs = model.programme_jobs();
        const unsigned count = jobs_ ? static_cast<unsigned>(jobs.size()) : 5u;
        if (input.is_pressed(Action::back) || input.is_pressed(Action::menu))
        {
            actions_ = jobs_ = false;
            return;
        }
        if (input.nav == Direction::up && action_row_)
            --action_row_;
        if (input.nav == Direction::down && action_row_ + 1 < count)
            ++action_row_;
        action_row_ = std::min(action_row_, count ? count - 1 : 0);
        if (input.is_pressed(Action::confirm))
        {
            if (jobs_)
            {
                if (action_row_ < jobs.size())
                    model.cancel_programme_job(jobs[action_row_].id);
            }
            else if (action_row_ == 2)
            {
                actions_ = false;
                jobs_ = true;
                action_row_ = 0;
            }
            else if (action_row_ == 4 && row_ < model.visible_count())
            {
                model.edit_guide_mapping(model.visible(row_));
                actions_ = false;
            }
            else if (action_row_ == 3)
            {
                model.refresh_guide();
                actions_ = false;
            }
            else if (const auto *p = selected())
            {
                model.schedule_programme(model.visible(row_), *p, action_row_ == 0);
                actions_ = false;
            }
        }
        return;
    }
    if (input.is_pressed(Action::back) || input.is_pressed(Action::r3))
    {
        dismiss();
        feedback.play(audio::Cue::back);
        return;
    }
    if (input.is_pressed(Action::menu))
    {
        actions_ = true;
        action_row_ = 0;
        return;
    }
    if (input.is_pressed(Action::north))
    {
        model.ask_query();
        return;
    }
    if (input.is_pressed(Action::west))
    {
        time_ = static_cast<std::int64_t>(platform::unix_time());
        keep_in_view();
        return;
    }
    if (input.is_pressed(Action::confirm) && row_ < model.visible_count())
    {
        const auto *p = selected();
        const bool playing =
            p ? model.play_programme(model.visible(row_), *p) : model.play(model.visible(row_));
        if (playing)
            dismiss();
        else
            feedback.play(audio::Cue::error);
        return;
    }
    const auto previous = time_;
    const auto row = row_;
    if (input.nav == Direction::up && row_)
        --row_;
    if (input.nav == Direction::down && row_ + 1 < model.visible_count())
        ++row_;
    if (input.is_pressed(Action::jump_prev))
        row_ = row_ > kRows ? row_ - kRows : 0;
    if (input.is_pressed(Action::jump_next))
        row_ += kRows;
    if (input.is_pressed(Action::page_prev))
        time_ -= 86400;
    if (input.is_pressed(Action::page_next))
        time_ += 86400;
    if (input.nav == Direction::left || input.nav == Direction::right)
    {
        const auto *p = selected();
        time_ = p ? input.nav == Direction::left ? p->start - 1 : p->end
                  : time_ + (input.nav == Direction::left ? -1800 : 1800);
    }
    const auto now = static_cast<std::int64_t>(platform::unix_time());
    time_ = std::clamp(time_, now - 14 * 86400, now + 14 * 86400);
    keep_in_view();
    if (previous != time_ || row != row_)
        feedback.play(audio::Cue::focus);
}
void GuideSheet::draw(ui::Canvas &canvas) const
{
    if (!open_)
        return;
    const auto &model = shared_.model;
    auto &list = canvas.list;
    const auto &fonts = canvas.fonts;
    const auto &theme = shared_.theme;
    list.rounded_rect({0, 0, kWidth, kHeight}, 0, tone::night.with_alpha(0.98f));
    ui::text(list, fonts.display, tr("Programme guide"), kMargin, 155, 48, theme.text);
    ui::text(list, fonts.semibold, programme_time(time_, true), kWidth - kMargin, 155, 27,
             tone::accent, gfx::Align::right);
    ui::text(list, fonts.regular, model.guide_status(), kMargin, 205, 22, theme.text_muted);
    if (!model.query().empty())
        ui::text(list, fonts.regular,
                 tr("Search: ") + model.query() + tr(" (channels or programmes on now)"), kMargin,
                 245, 22, tone::accent);
    for (int tick = 0; tick <= 4; ++tick)
    {
        const float x = kTimeX + kTimeWidth * static_cast<float>(tick) / 4;
        ui::text(list, fonts.mono, programme_time(left_ + tick * 1800), x, 283, 20,
                 theme.text_muted, tick == 4 ? gfx::Align::right : gfx::Align::left);
    }
    for (unsigned row = top_; row < model.visible_count() && row < top_ + kRows; ++row)
    {
        const auto channel = model.channel(model.visible(row));
        const float y = kRowY + static_cast<float>(row - top_) * kRowHeight;
        const bool focus = row == row_;
        list.rounded_rect({kMargin, y, kTimeX - kMargin - 10, kRowHeight - 5}, 9,
                          kWhite.with_alpha(focus ? 0.16f : 0.04f));
        const auto name = shown_name(fonts, channel);
        const auto &face = face_for(fonts, fonts.semibold, name);
        ui::text(list, face, face.font->fit(name, 22, kTimeX - kMargin - 40), kMargin + 15, y + 40,
                 22, theme.text);
        list.rounded_rect({kTimeX, y, kTimeWidth, kRowHeight - 5}, 8, kWhite.with_alpha(0.025f));
        const auto programmes = model.guide().programmes(channel.id);
        bool any = false;
        for (const auto &p : programmes)
        {
            const auto end = p.end > p.start ? p.end : p.start + 1800;
            if (end <= left_)
                continue;
            if (p.start >= left_ + kWindow)
                break;
            any = true;
            const float x = kTimeX + kTimeWidth *
                                         static_cast<float>(std::max(p.start, left_) - left_) /
                                         static_cast<float>(kWindow);
            const float right =
                kTimeX + kTimeWidth * static_cast<float>(std::min(end, left_ + kWindow) - left_) /
                             static_cast<float>(kWindow);
            const Rect cell{x + 1, y, std::max(1.0f, right - x - 3), kRowHeight - 5};
            const bool active = focus && p.start <= time_ && time_ < end;
            list.bordered_rect(cell, 8, kWhite.with_alpha(active ? 0.2f : 0.08f), active ? 2 : 0,
                               active ? tone::cream : kClear);
            if (cell.w > 32)
            {
                const auto &title = face_for(fonts, fonts.regular, p.title);
                ui::text(list, title, title.font->fit(p.title, 21, cell.w - 22), x + 12, y + 39, 21,
                         theme.text);
            }
        }
        if (!any)
            ui::text(list, fonts.regular, tr("No programme information"), kTimeX + 14, y + 40, 21,
                     theme.text_muted);
    }
    const auto now = static_cast<std::int64_t>(platform::unix_time());
    if (now >= left_ && now < left_ + kWindow)
    {
        const float x =
            kTimeX + kTimeWidth * static_cast<float>(now - left_) / static_cast<float>(kWindow);
        list.rounded_rect({x, kRowY - 7, 2, kRows * kRowHeight}, 0, tone::accent);
    }
    if (const auto *p = selected())
    {
        const auto &face = face_for(fonts, fonts.semibold, p->title);
        ui::text(list, face, face.font->fit(p->title, 29, 1450), kMargin, 855, 29, theme.text);
        const auto channel = model.channel(model.visible(row_));
        const auto available = !catchup_url(channel, *p, now).empty();
        const std::string status = programme_time(p->start) + " - " + programme_time(p->end) +
                                   (available                         ? tr("   Catch-up available")
                                    : p->start <= now && now < p->end ? tr("   On now")
                                    : p->start > now                  ? tr("   Upcoming")
                                                     : tr("   Archive unavailable"));
        ui::text(list, fonts.regular, status, kMargin, 890, 22,
                 available ? tone::accent : theme.text_muted);
        const auto &body = face_for(fonts, fonts.regular, p->description);
        ui::text(list, body, body.font->fit(p->description, 21, kWidth - 2 * kMargin), kMargin, 926,
                 21, theme.text_muted);
    }
    else if (model.visible_count() == 0)
        ui::text(list, fonts.regular,
                 tr("No channels match the current category, folder or search."), kMargin, 400, 26,
                 theme.text_muted);
    ui::text(
        list, fonts.regular,
        tr("Cross: Watch   D-pad: Channel / programme   L1/R1: Day   L2/R2: Page   Square: Now   "
           "Triangle: Search   Circle: Back"),
        kMargin, 990, 21, theme.text_muted);
    ui::text(list, fonts.regular,
             tr("Options: Record / Remind / Scheduled programmes / Update / Match XMLTV"), kMargin,
             1030, 21, theme.text_muted);
    if (actions_ || jobs_)
    {
        list.rounded_rect({420, 230, 1140, 650}, 16, tone::night);
        ui::text(list, fonts.semibold, tr(jobs_ ? "Scheduled programmes" : "Programme actions"),
                 460, 290, 32, theme.text);
        const auto jobs = model.programme_jobs();
        const char *actions[] = {"Record to USB / cancel recording", "Set / cancel reminder",
                                 "Scheduled programmes", "Update guide", "Match XMLTV channel"};
        const unsigned count = jobs_ ? static_cast<unsigned>(jobs.size()) : 5;
        const unsigned top = action_row_ / 6 * 6;
        for (unsigned i = top; i < count && i < top + 6; ++i)
        {
            const float y = 350 + static_cast<float>(i - top) * 68;
            if (i == action_row_)
                list.rounded_rect({448, y - 33, 1078, 56}, 8, kWhite.with_alpha(0.16f));
            std::string label;
            if (jobs_)
            {
                const auto &job = jobs[i];
                const char *state[] = {"Pending", "Recording", "Done", "Failed", "Cancelled"};
                label = std::string(tr(job.recording ? "Record  " : "Remind  ")) +
                        programme_time(job.start, true) + "  " + job.title + "  [" +
                        tr(state[std::clamp(job.state, 0, 4)]) + "]";
            }
            else
                label = tr(actions[i]);
            const auto &face = face_for(fonts, fonts.regular, label);
            ui::text(list, face, face.font->fit(label, 24, 1040), 465, y, 24, theme.text);
        }
        if (jobs_ && action_row_ < jobs.size())
            ui::text(list, fonts.regular,
                     fonts.regular.font->fit(tr(jobs[action_row_].result.c_str()), 21, 1040), 460,
                     760, 21, theme.text_muted);
        ui::text(list, fonts.regular,
                 tr(jobs_ ? "Cross: Remove / stop selected job   Circle: Back"
                          : "Cross: Select   Circle: Back"),
                 460, 806, 22, theme.text_muted);
        ui::text(list, fonts.regular, tr("Keep ProsperoTV running for recordings and reminders."),
                 460, 850, 22, theme.text_muted);
    }
}
} // namespace ptv
