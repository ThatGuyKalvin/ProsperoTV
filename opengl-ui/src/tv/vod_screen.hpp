// ProsperoTV - Movies, nested provider categories, shows and seasons.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tv/shared.hpp"
#include "ui/glyphs.hpp"
namespace ptv
{
class VodScreen
{
  public:
    explicit VodScreen(Shared &shared) : shared_(shared)
    {
    }
    void enter();
    void update();
    void handle(const InputFrame &, ui::Feedback &);
    bool back();
    void refresh();
    void draw(ui::Canvas &) const;
    std::vector<std::string> image_urls() const;
    int hints(ui::Hint *, int) const;
    bool details_open() const
    {
        return details_open_;
    }
    bool filters_open() const
    {
        return filters_open_;
    }
    bool at_top() const
    {
        return !details_open_ && !filters_open_ && shared_.model.view.vod_focus == 0;
    }
    unsigned result_count() const;
    std::vector<std::string> result_titles() const; // in the order they are listed

  private:
    enum class Kind
    {
        movies,
        shows,
        all,
        category,
        item,
        resume // carry on with a show, at the top of its episode list
    };
    struct Row
    {
        Kind kind;
        std::string text, category;
        unsigned item = 0;
        std::optional<unsigned> count = std::nullopt;
    };
    void sync();
    void draw_details(ui::Canvas &) const;
    void draw_filters(ui::Canvas &) const;
    void handle_filters(const InputFrame &);
    // A row that stands for one movie or episode, with its artwork beside it.
    static bool shows_a_title(const Row &row);
    const Row *focused() const;
    Shared &shared_;
    std::vector<Row> rows_;
    unsigned revision_ = ~0u, model_revision_ = ~0u, counts_revision_ = ~0u;
    int top_ = 0;
    bool details_open_ = false, filters_open_ = false;
    int filter_focus_ = 0;
    float detail_scroll_ = 0;
    std::string detail_item_id_;
    // A held L2 or R2 keeps turning pages, faster the longer it is held.
    int page_dir_ = 0;
    double page_started_ = 0.0, page_due_ = 0.0;
};
} // namespace ptv
