// ProsperoTV - Provider categories and favorite folders.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/i18n.hpp"
#include "tv/library_sheet.hpp"
#include "tv/draw.hpp"
#include "tv/category_path.hpp"
#include "iptv_ime.h"

#include <algorithm>
#include <map>

namespace ptv
{
LibrarySheet::LibrarySheet(Shared &shared) : shared_(shared)
{
    list_.style.theme = shared.theme;
    list_.style.row_height = 72;
    list_.style.title_size = 24;
    list_.style.entrance_step = 0;
    list_.set_bounds({400, 245, 1120, 640});
}

void LibrarySheet::sync()
{
    auto &model = shared_.model;
    const int focus = list_.focus();
    values_.clear();
    branches_.clear();
    std::vector<ui::ListItem> rows;
    if (channel_options_)
    {
        if (hidden_list_)
        {
            for (const auto at : model.hidden_channels())
            {
                const auto channel = model.channel(at);
                values_.emplace_back(channel.id);
                ui::ListItem row;
                row.title = label_text(channel.name);
                row.value = tr("Hidden");
                rows.push_back(std::move(row));
            }
        }
        else
        {
            for (const char *title :
                 {"Rename channel", "Use provider name", "Hide channel", "Move up", "Move down",
                  "Reset list order", "Hidden channels"})
            {
                ui::ListItem row;
                row.title = tr(title);
                const auto action = rows.size();
                const bool ranked =
                    model.group() == Group::recently_added || model.group() == Group::most_watched;
                row.disabled =
                    (channel_.empty() && action < 5) || (ranked && action >= 3 && action <= 5);
                rows.push_back(std::move(row));
            }
        }
        list_.set_items(std::move(rows));
        list_.set_focus(
            std::max(0, std::min(focus, hidden_list_ ? static_cast<int>(values_.size()) - 1 : 6)),
            true);
        seen_revision_ = model.revision();
        dirty_ = false;
        return;
    }
    ui::ListItem all;
    all.title = folders_ ? tr("All favorites") : tr("All provider categories");
    if (!folders_ && !category_path_.empty())
    {
        all.title = tr("Browse all in ") + label_text(category_path_);
        all.value = model.category_hidden(category_path_) ? tr("Hidden") : "";
    }
    rows.push_back(std::move(all));
    if (folders_)
    {
        values_ = model.folders();
        for (const auto &folder : values_)
        {
            ui::ListItem row;
            row.title = folder;
            row.value = !channel_.empty() && model.in_folder(folder, channel_)
                            ? tr("Channel included")
                            : "";
            row.badge = model.folder() == folder ? tr("In use") : "";
            rows.push_back(std::move(row));
        }
    }
    else
    {
        std::map<std::string, std::pair<unsigned, bool>> children;
        for (const auto &category : model.provider_categories())
        {
            std::string_view child = category.value;
            bool branch = false;
            while (category_parent(child) != category_path_ && !child.empty())
            {
                child = category_parent(child);
                branch = true;
            }
            if (child.empty())
                continue;
            auto &row = children[std::string(child)];
            row.first += category.count;
            row.second = row.second || branch;
        }
        for (const auto &[child, info] : children)
        {
            values_.push_back(child);
            branches_.push_back(info.second);
            ui::ListItem row;
            row.title = label_text(category_leaf(child));
            row.value = model.category_rule(child) == 1   ? tr("PIN required")
                        : model.category_rule(child) == 2 ? tr("Approved for kids")
                        : model.category_hidden(child)    ? tr("Hidden")
                        : info.second                     ? ""
                                                          : group_digits(info.first);
            row.badge = info.second                          ? tr("Subcategories")
                        : model.provider_category() == child ? tr("In use")
                                                             : "";
            rows.push_back(std::move(row));
        }
    }
    list_.set_items(std::move(rows));
    list_.set_focus(std::clamp(focus, 0, static_cast<int>(values_.size())), true);
    seen_revision_ = model.revision();
    dirty_ = false;
}

void LibrarySheet::open(bool folders, std::string channel, ui::Feedback &feedback, bool allow_play)
{
    folders_ = folders;
    channel_options_ = false;
    hidden_list_ = false;
    editing_source_ = shared_.model.selected_source_id();
    allow_play_ = allow_play;
    number_pad_ = false;
    number_.clear();
    number_key_ = 0;
    category_path_.clear();
    channel_ = std::move(channel);
    open_ = true;
    sync();
    list_.set_focus(0, true);
    list_.enter();
    feedback.play(audio::Cue::open);
}

void LibrarySheet::dismiss()
{
    if (open_)
        iptv_ime_cancel();
    open_ = false;
}

void LibrarySheet::named(const char *text, void *context)
{
    auto &self = *static_cast<LibrarySheet *>(context);
    if (text == nullptr || *text == '\0')
        return;
    if (!self.shared_.model.create_folder(text))
        self.shared_.model.announce(Level::error, tr("Folder could not be created"),
                                    tr("Use a new name. Check that storage is available."), 4);
    self.dirty_ = true;
}

void LibrarySheet::handle(const InputFrame &input, ui::Feedback &feedback)
{
    if (channel_options_)
    {
        handle_channel_options(input, feedback);
        return;
    }
    if (number_pad_)
    {
        handle_number_pad(input, feedback);
        return;
    }
    if (allow_play_ && input.is_pressed(Action::page_next))
    {
        number_pad_ = true;
        number_.clear();
        number_key_ = 0;
        feedback.play(audio::Cue::open);
        return;
    }
    if (input.is_pressed(Action::page_prev))
    {
        channel_options_ = true;
        sync();
        list_.set_focus(channel_.empty() ? 6 : 0, true);
        feedback.play(audio::Cue::open);
        return;
    }
    if (input.is_pressed(Action::back) || input.is_pressed(Action::touch))
    {
        if (!folders_ && !category_path_.empty() && input.is_pressed(Action::back))
        {
            category_path_ = category_parent(category_path_);
            sync();
            list_.set_focus(0, true);
            feedback.play(audio::Cue::back);
            return;
        }
        dismiss();
        feedback.play(audio::Cue::back);
        return;
    }
    auto &model = shared_.model;
    if (dirty_ || seen_revision_ != model.revision())
        sync();
    if (folders_ && input.is_pressed(Action::north))
    {
        if (model.keyboard_ready())
            iptv_ime_request_prompt("", tr("New favorite folder"), tr("Folder name"), 32,
                                    &LibrarySheet::named, this);
        return;
    }
    const auto event = list_.handle(input, feedback);
    const int at = list_.focus();
    const std::string value = at > 0 && static_cast<std::size_t>(at) <= values_.size()
                                  ? values_[static_cast<std::size_t>(at - 1)]
                                  : (folders_ ? std::string() : category_path_);
    if (folders_ && input.is_pressed(Action::menu) && !value.empty())
    {
        // The folder goes; the channels in it stay favorites.
        if (model.remove_folder(value))
        {
            model.announce(Level::busy, tr("Folder deleted"), value, 3);
            feedback.play(audio::Cue::back);
        }
        else
            feedback.play(audio::Cue::error);
        sync();
        list_.set_focus(0, true);
        return;
    }
    if (!folders_ && input.is_pressed(Action::north) && !value.empty())
    {
        if (!model.parental().enabled())
            model.announce(Level::warning, tr("Set a parent PIN in Settings first"), "", 3);
        else if (!model.set_category_rule(value, (model.category_rule(value, false) + 1) % 3))
            feedback.play(audio::Cue::error);
        sync();
        return;
    }
    if (input.is_pressed(Action::west) && !value.empty())
    {
        bool saved = false;
        if (folders_)
        {
            for (unsigned i = 0; i < model.channel_count(); ++i)
                if (model.channel(i).id == channel_)
                {
                    saved = model.put_in_folder(value, i, !model.in_folder(value, channel_));
                    break;
                }
        }
        else
        {
            if (model.category_hidden(category_parent(value)))
            {
                model.announce(Level::warning, tr("Show the parent category first"),
                               std::string(category_parent(value)), 3);
                return;
            }
            saved = model.hide_category(value, !model.category_hidden(value));
        }
        if (!saved)
            model.announce(Level::error, tr("The change could not be saved"), "", 3);
        sync();
    }
    else if (event == ui::Event::activated)
    {
        if (!folders_ && at > 0 && branches_[static_cast<std::size_t>(at - 1)])
        {
            category_path_ = value;
            sync();
            list_.set_focus(0, true);
            return;
        }
        if (folders_)
            model.set_folder(value);
        else if (!model.category_hidden(value))
            model.set_provider_category(value);
        else
        {
            feedback.play(audio::Cue::error);
            return;
        }
        dismiss();
    }
}

void LibrarySheet::renamed_channel(const char *text, void *context)
{
    auto &self = *static_cast<LibrarySheet *>(context);
    if (!text || !self.open_)
        return;
    auto &model = self.shared_.model;
    if (model.selected_source_id() != self.editing_source_ ||
        !model.rename_channel(self.channel_, text))
        model.announce(Level::error, tr("The change could not be saved"), "", 3);
    self.dirty_ = true;
}

void LibrarySheet::handle_channel_options(const InputFrame &input, ui::Feedback &feedback)
{
    auto &model = shared_.model;
    if (input.is_pressed(Action::back) || input.is_pressed(Action::touch))
    {
        if (hidden_list_)
            hidden_list_ = false;
        else
            channel_options_ = false;
        sync();
        list_.set_focus(0, true);
        return;
    }
    if (dirty_ || seen_revision_ != model.revision())
        sync();
    if (list_.handle(input, feedback) != ui::Event::activated)
        return;
    const auto at = static_cast<unsigned>(list_.focus());
    bool saved = false;
    if (model.selected_source_id() != editing_source_)
    {
        dismiss();
        return;
    }
    if (hidden_list_)
    {
        if (at >= values_.size())
            return;
        saved = model.hide_channel(values_[at], false);
    }
    else if (at == 6)
    {
        hidden_list_ = true;
        sync();
        list_.set_focus(0, true);
        return;
    }
    else if (at == 0)
    {
        const auto channel = model.find(channel_);
        if (channel && model.keyboard_ready())
            iptv_ime_request_prompt(std::string(channel->name).c_str(), tr("Rename channel"),
                                    tr("Channel name"), 64, &LibrarySheet::renamed_channel, this);
        return;
    }
    else if (at == 1)
        saved = model.rename_channel(channel_, "");
    else if (at == 2)
        saved = model.hide_channel(channel_, true);
    else if (at == 3 || at == 4)
        saved = model.move_channel(channel_, at == 3 ? -1 : 1);
    else if (at == 5)
        saved = model.reset_channel_order();
    if (!saved)
        model.announce(Level::error, tr("The change could not be saved"), "", 3);
    else
        feedback.play(audio::Cue::select);
    sync();
}

void LibrarySheet::handle_number_pad(const InputFrame &input, ui::Feedback &feedback)
{
    if (input.is_pressed(Action::back) || input.is_pressed(Action::touch))
    {
        number_pad_ = false;
        feedback.play(audio::Cue::back);
        return;
    }
    if (input.nav == Direction::left && number_key_ % 3 > 0)
        --number_key_;
    if (input.nav == Direction::right && number_key_ % 3 < 2)
        ++number_key_;
    if (input.nav == Direction::up && number_key_ >= 3)
        number_key_ -= 3;
    if (input.nav == Direction::down && number_key_ < 12)
        number_key_ += 3;
    if (input.is_pressed(Action::west))
    {
        if (!number_.empty())
            number_.pop_back();
    }
    if (!input.is_pressed(Action::confirm))
        return;
    if (number_key_ == 12)
        number_.clear();
    else if (number_key_ == 14)
        number_pad_ = false;
    else if (number_key_ == 13)
    {
        auto &model = shared_.model;
        const int position = model.position_for_number(number_);
        if (position < 0)
        {
            model.announce(Level::warning,
                           position == -2 ? tr("Several channels use this number")
                                          : tr("No channel with this number"),
                           tr("Choose a narrower list if numbers repeat."), 4);
            feedback.play(audio::Cue::error);
        }
        else if (model.play(model.visible(static_cast<unsigned>(position))))
            dismiss();
    }
    else if (number_key_ == 11)
    {
        if (!number_.empty())
            number_.pop_back();
    }
    else if (number_.size() < 16)
    {
        const char digit = number_key_ < 9    ? static_cast<char>('1' + number_key_)
                           : number_key_ == 9 ? '.'
                                              : '0';
        if (digit != '.' || (!number_.empty() && number_.find('.') == std::string::npos))
            number_ += digit;
    }
}

void LibrarySheet::draw_number_pad(ui::Canvas &canvas) const
{
    auto &draw = canvas.list;
    const auto &fonts = canvas.fonts;
    draw.rounded_rect({0, 0, kWidth, kHeight}, 0, tone::night.with_alpha(0.95f));
    draw_glass(canvas, shared_.theme, {570, 100, 780, 870}, 28);
    ui::text(draw, fonts.display, tr("Channel number"), 630, 175, 42, shared_.theme.text);
    ui::text(draw, fonts.regular, tr("Watch a channel in the current list"), 630, 220, 24,
             shared_.theme.text_muted);
    ui::text(draw, fonts.mono, number_.empty() ? "_" : number_, 630, 295, 48, shared_.theme.text);
    const char *keys[] = {"1", "2", "3", "4",      "5",     "6",     "7",   "8",
                          "9", ".", "0", "Delete", "Clear", "Watch", "Back"};
    for (int i = 0; i < 15; ++i)
    {
        const float x = 630.0f + static_cast<float>(i % 3) * 220.0f;
        const float y = 330.0f + static_cast<float>(i / 3) * 108.0f;
        draw.rounded_rect({x, y, 200, 88}, 16,
                          i == number_key_ ? tone::accent : shared_.theme.surface);
        ui::text(draw, fonts.semibold, tr(keys[i]), x + 100, y + 56, 30,
                 i == number_key_ ? tone::ink : shared_.theme.text, gfx::Align::center);
    }
    ui::text(draw, fonts.regular, tr("Cross: Enter   Square: Delete   Circle: Back"), 630, 925, 22,
             shared_.theme.text_muted);
}

void LibrarySheet::update(float dt)
{
    if (open_ && (dirty_ || seen_revision_ != shared_.model.revision()))
        sync();
    list_.style.reduced_motion = shared_.settings.reduced_motion;
    list_.update(dt);
}

void LibrarySheet::draw(ui::Canvas &canvas) const
{
    if (!open_)
        return;
    if (number_pad_)
    {
        draw_number_pad(canvas);
        return;
    }
    auto &draw = canvas.list;
    const auto &fonts = canvas.fonts;
    draw.rounded_rect({0, 0, kWidth, kHeight}, 0, tone::night.with_alpha(0.9f));
    draw_glass(canvas, shared_.theme, {360, 120, 1200, 830}, 28);
    ui::text(draw, fonts.display,
             channel_options_ ? (hidden_list_ ? tr("Hidden channels") : tr("Channel options"))
             : folders_       ? tr("Favorite folders")
                              : tr("Provider categories"),
             400, 190, 42, shared_.theme.text);
    list_.draw(canvas);
    if (channel_options_)
    {
        if (!hidden_list_)
            if (const auto channel = shared_.model.find(channel_);
                channel && shared_.model.content_allowed(*channel))
            {
                const auto name = shown_name(fonts, *channel);
                const auto &face = face_for(fonts, fonts.regular, name);
                ui::text(draw, face, face.font->fit(name, 22, 1080), 400, 225, 22,
                         shared_.theme.text_muted);
            }
        ui::text(draw, fonts.regular,
                 hidden_list_ ? tr("Cross: Show channel   Circle: Back")
                              : tr("Cross: Choose   Circle: Back"),
                 400, 915, 21, shared_.theme.text_muted);
        return;
    }
    ui::text(draw, fonts.regular, tr("L1: Channel options"), 400, 225, 21,
             shared_.theme.text_muted);
    if (allow_play_)
        ui::text(draw, fonts.regular, tr("R1: Channel number"), 1180, 190, 24,
                 shared_.theme.text_muted);
    ui::text(draw, fonts.regular,
             folders_
                 ? tr("Cross: Open   Square: Add/remove channel   Triangle: New folder   Options: "
                      "Delete   "
                      "Circle: Back")
                 : tr("Cross: Browse   Square: Show/hide   Triangle: Normal / PIN / Kids   Circle: "
                      "Back"),
             400, 915, 21, shared_.theme.text_muted);
}
} // namespace ptv
