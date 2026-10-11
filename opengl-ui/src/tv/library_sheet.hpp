// ProsperoTV - Provider categories and favorite folders.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "tv/shared.hpp"
#include "ui/components/list.hpp"

namespace ptv
{
class LibrarySheet
{
  public:
    explicit LibrarySheet(Shared &shared);
    void open(bool folders, std::string channel, ui::Feedback &feedback, bool allow_play = true);
    bool is_open() const
    {
        return open_;
    }
    void dismiss();
    void handle(const InputFrame &input, ui::Feedback &feedback);
    void update(float dt);
    void draw(ui::Canvas &canvas) const;

  private:
    void sync();
    void handle_number_pad(const InputFrame &input, ui::Feedback &feedback);
    void draw_number_pad(ui::Canvas &canvas) const;
    void handle_channel_options(const InputFrame &input, ui::Feedback &feedback);
    static void renamed_channel(const char *text, void *context);
    bool channel_options_ = false;
    bool hidden_list_ = false;
    std::int64_t editing_source_ = 0;
    bool number_pad_ = false;
    bool allow_play_ = true;
    int number_key_ = 0;
    std::string number_;
    static void named(const char *text, void *context);
    Shared &shared_;
    ui::ListView list_;
    bool open_ = false;
    bool folders_ = false;
    bool dirty_ = false;
    unsigned seen_revision_ = 0;
    std::vector<std::string> values_;
    std::vector<bool> branches_;
    std::string category_path_;
    std::string channel_;
};
} // namespace ptv
