// ProsperoTV - A virtual list: only the visible movies or episodes are drawn.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include "tv/vod_screen.hpp"
#include "tv/category_path.hpp"
#include "tv/draw.hpp"
#include <chrono>
#include <algorithm>
#include <map>
#include <set>
#include <cstdlib>
#include <iterator>

namespace ptv
{
namespace
{
constexpr int kRows = 7;
constexpr float kLetterX = 1274.0f;
constexpr float kLetterTop = 294.0f;
constexpr float kLetterPitch = 22.0f;
// The first is the default: the provider's newest additions, or a show's episodes in order.
constexpr const char *kSorts[] = {"Recently added", "Title A-Z", "Title Z-A", "Newest year",
                                  "Highest rating"};
constexpr int kSortCount = static_cast<int>(std::size(kSorts));
} // namespace
int VodScreen::columns() const
{
    return shared_.model.view.vod_kind < 0                                      ? 2
           : shared_.model.view.vod_kind == static_cast<int>(VodKind::episodes) ? 1
                                                                                : 5;
}
int VodScreen::page_size() const
{
    return columns() == 1 ? kRows : columns() * 2;
}
bool VodScreen::has_letters() const
{
    const auto kind = shared_.model.view.vod_kind;
    return (kind == static_cast<int>(VodKind::movies) ||
            kind == static_cast<int>(VodKind::series)) &&
           !rows_.empty() &&
           std::any_of(letter_starts_.begin(), letter_starts_.end(),
                       [](int start) { return start >= 0; });
}
int VodScreen::current_letter() const
{
    const auto *row = focused();
    if (row && !row->key.empty() && letter_starts_[letter_of_key(row->key)] >= 0)
        return letter_of_key(row->key);
    for (int letter = 0; letter < kLetterCount; ++letter)
        if (letter_starts_[letter] >= 0)
            return letter;
    return 0;
}
void VodScreen::enter_letters()
{
    auto &view = shared_.model.view;
    const auto row = *focused();
    // A letter must lead to a continuous group, even after newest-first browsing.
    if (view.vod_sort != 1)
    {
        view.vod_sort = 1;
        sync();
        const auto kept = std::find_if(rows_.begin(), rows_.end(),
                                       [&](const Row &candidate)
                                       {
                                           return candidate.kind == row.kind &&
                                                  candidate.item == row.item &&
                                                  candidate.category == row.category;
                                       });
        if (kept != rows_.end())
            view.vod_focus = static_cast<int>(kept - rows_.begin());
    }
    if (focused()->key.empty() ||
        (focused()->kind == Kind::category && shared_.model.category_hidden(focused()->category)))
        view.vod_focus = letter_starts_[current_letter()];
    top_ = view.vod_focus / columns() * columns();
    page_dir_ = 0;
    letters_focused_ = true;
}
void VodScreen::enter()
{
    letters_focused_ = false;
    const auto &view = shared_.model.view;
    if (view.vod_kind >= 0)
        shared_.model.vod().select(static_cast<VodKind>(view.vod_kind), view.vod_series);
    sync();
}
bool VodScreen::shows_a_title(const Row &row)
{
    return row.kind == Kind::item || row.kind == Kind::resume;
}
const VodScreen::Row *VodScreen::focused() const
{
    const int focus = shared_.model.view.vod_focus;
    return focus >= 0 && static_cast<std::size_t>(focus) < rows_.size() ? &rows_[focus] : nullptr;
}
void VodScreen::sync()
{
    auto &model = shared_.model;
    auto &view = model.view;
    const auto &vod = model.vod();
    model.set_vod_overview(view.vod_kind < 0);
    rows_.clear();
    if (view.vod_kind < 0)
        rows_ = {{Kind::movies, tr("Movies"), {}, 0, model.vod_count(VodKind::movies)},
                 {Kind::shows, tr("TV shows"), {}, 0, model.vod_count(VodKind::series)}};
    else
    {
        const auto &catalog = vod.catalog();
        std::map<std::string, std::string> categories;
        std::vector<Row> items;
        for (unsigned i = 0; i < catalog.size(); ++i)
        {
            const auto item = catalog[i];
            if (!model.vod_allowed(i))
                continue;
            if (!view.vod_category.empty() &&
                !category_belongs(item.group_title, view.vod_category))
                continue;
            const auto &details = vod.details(item.id);
            const auto genres = vod_genres(details.genre);
            if ((!view.vod_genre.empty() &&
                 std::find(genres.begin(), genres.end(), view.vod_genre) == genres.end()) ||
                (!view.vod_year.empty() && details.year != view.vod_year))
                continue;
            const bool searching =
                !view.vod_query.empty() || !view.vod_genre.empty() || !view.vod_year.empty();
            if (!searching && !view.vod_all)
            {
                auto child = category_trim(item.group_title);
                while (!child.empty() && category_parent(child) != view.vod_category)
                    child = category_parent(child);
                if (!child.empty() && child != view.vod_category)
                {
                    categories.emplace(std::string(child), std::string(category_leaf(child)));
                    continue;
                }
            }
            if (!model.category_hidden(item.group_title) &&
                contains_nocase(item.name, view.vod_query))
                items.push_back({Kind::item, std::string(item.name), {}, i});
        }
        const bool plain = !view.vod_all && view.vod_query.empty() && view.vod_genre.empty() &&
                           view.vod_year.empty();
        // Where the show was left, above its seasons.
        const auto resume = plain && view.vod_category.empty() &&
                                    view.vod_kind == static_cast<int>(VodKind::episodes)
                                ? model.series_resume()
                                : std::nullopt;
        if (resume)
        {
            auto label =
                std::string(tr("Continue")) + " · " + std::string(catalog[resume->index].name);
            if (resume->position_us)
                label += " (" + std::to_string(resume->position_us / 60000000) +
                         tr(" min watched") + ")";
            rows_.push_back({Kind::resume, std::move(label), {}, resume->index});
        }
        if (plain && !catalog.empty())
            rows_.push_back({Kind::all, tr("Browse all"), {}, 0});
        std::vector<std::pair<std::string, std::string>> ordered(categories.begin(),
                                                                 categories.end());
        if (view.vod_kind == static_cast<int>(VodKind::episodes))
            std::sort(ordered.begin(), ordered.end(),
                      [](const auto &left, const auto &right)
                      {
                          return left.first.size() != right.first.size()
                                     ? left.first.size() < right.first.size()
                                     : left.first < right.first;
                      });
        for (const auto &[path, name] : ordered)
            rows_.push_back({Kind::category, name, path, 0});
        const bool episodes = view.vod_kind == static_cast<int>(VodKind::episodes);
        std::stable_sort(items.begin(), items.end(),
                         [&](const Row &left, const Row &right)
                         {
                             const auto &a = vod.details(catalog[left.item].id);
                             const auto &b = vod.details(catalog[right.item].id);
                             // Newest first, and titles added together keep the provider's
                             // order. Titles without a date (0) follow by title, so a playlist,
                             // which gives no dates, is listed by title as before.
                             if (view.vod_sort == 0 && !episodes)
                             {
                                 const auto older = vod.added(left.item);
                                 const auto newer = vod.added(right.item);
                                 if (older != newer)
                                     return older > newer;
                                 if (older)
                                     return false;
                             }
                             if (view.vod_sort == 2)
                                 return left.text > right.text;
                             if (view.vod_sort == 3 && a.year != b.year)
                                 return a.year > b.year;
                             if (view.vod_sort == 4 && std::strtof(a.rating.c_str(), nullptr) !=
                                                           std::strtof(b.rating.c_str(), nullptr))
                                 return std::strtof(a.rating.c_str(), nullptr) >
                                        std::strtof(b.rating.c_str(), nullptr);
                             if (episodes && view.vod_sort != 1)
                                 return catalog[left.item].source_line <
                                        catalog[right.item].source_line;
                             return left.text < right.text;
                         });
        rows_.insert(rows_.end(), std::make_move_iterator(items.begin()),
                     std::make_move_iterator(items.end()));
    }
    letter_starts_.fill(-1);
    if (view.vod_kind == static_cast<int>(VodKind::movies) ||
        view.vod_kind == static_cast<int>(VodKind::series))
    {
        for (auto &row : rows_)
            if (row.kind == Kind::item || row.kind == Kind::category)
            {
                iptv::ChannelView named{};
                named.name = row.text;
                row.key = sort_key(named);
            }
        if (view.vod_sort == 1)
            std::stable_sort(rows_.begin(), rows_.end(),
                             [](const Row &left, const Row &right)
                             {
                                 if (left.key.empty() || right.key.empty())
                                     return left.key.empty() && !right.key.empty();
                                 return left.key < right.key;
                             });
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i)
            if (!rows_[i].key.empty() &&
                (rows_[i].kind != Kind::category || !model.category_hidden(rows_[i].category)))
            {
                auto &start = letter_starts_[letter_of_key(rows_[i].key)];
                if (start < 0)
                    start = i;
            }
    }
    if (!has_letters())
        letters_focused_ = false;
    if (details_open_)
    {
        const auto found = std::find_if(
            rows_.begin(), rows_.end(), [&](const Row &row)
            { return row.kind == Kind::item && vod.catalog()[row.item].id == detail_item_id_; });
        if (found == rows_.end())
            details_open_ = false;
        else
            view.vod_focus = static_cast<int>(found - rows_.begin());
    }
    view.vod_focus = std::clamp(view.vod_focus, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
    top_ = std::max(0, (view.vod_focus / columns() - (columns() == 1 ? kRows - 1 : 1)) * columns());
    revision_ = vod.revision();
    model_revision_ = model.revision();
    counts_revision_ = model.search_revision();
}
void VodScreen::update()
{
    shared_.model.set_vod_overview(shared_.model.view.vod_kind < 0);
    if (revision_ != shared_.model.vod().revision() ||
        model_revision_ != shared_.model.revision() ||
        counts_revision_ != shared_.model.search_revision())
        sync();
}
void VodScreen::refresh()
{
    if (shared_.model.active_source() == iptv::SourceKind::Custom)
    {
        shared_.model.refresh();
        return;
    }
    const auto &view = shared_.model.view;
    if (view.vod_kind < 0)
    {
        shared_.model.set_vod_overview(true, true);
        sync();
        return;
    }
    if (view.vod_kind >= 0)
        shared_.model.vod().select(static_cast<VodKind>(view.vod_kind), view.vod_series, true);
}
bool VodScreen::back()
{
    auto &view = shared_.model.view;
    if (details_open_)
    {
        details_open_ = false;
        return true;
    }
    if (filters_open_)
    {
        filters_open_ = false;
        return true;
    }
    if (letters_focused_)
    {
        letters_focused_ = false;
        return true;
    }
    if (!view.vod_query.empty())
        view.vod_query.clear();
    else if (view.vod_all)
        view.vod_all = false;
    else if (!view.vod_category.empty())
        view.vod_category = category_parent(view.vod_category);
    else if (view.vod_kind == static_cast<int>(VodKind::episodes))
    {
        view.vod_kind = static_cast<int>(VodKind::series);
        view.vod_series.clear();
        view.vod_series_name.clear();
        view.vod_series_cover.clear();
        shared_.model.vod().select(VodKind::series);
    }
    else if (view.vod_kind >= 0)
    {
        view.vod_kind = -1;
        shared_.model.vod().stop();
    }
    else
        return false;
    view.vod_focus = 0;
    sync();
    return true;
}
void VodScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    auto &model = shared_.model;
    auto &view = model.view;
    if (filters_open_)
    {
        handle_filters(input);
        return;
    }
    if (details_open_)
    {
        if (input.nav == Direction::down)
            detail_scroll_ = std::min(2400.0f, detail_scroll_ + 80);
        if (input.nav == Direction::up)
            detail_scroll_ = std::max(0.0f, detail_scroll_ - 80);
        if (input.is_pressed(Action::west))
        {
            details_open_ = false;
            return;
        }
        if (!input.is_pressed(Action::confirm))
            return;
        details_open_ = false;
    }
    if (input.is_pressed(Action::r3) && view.vod_kind >= 0)
    {
        letters_focused_ = false;
        filters_open_ = true;
        filter_focus_ = 0;
        return;
    }
    if (letters_focused_)
    {
        if (input.nav == Direction::left || input.is_pressed(Action::confirm))
        {
            letters_focused_ = false;
            feedback.play(audio::Cue::focus);
        }
        else if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int step = input.nav == Direction::down ? 1 : -1;
            int letter = current_letter() + step;
            while (letter >= 0 && letter < kLetterCount && letter_starts_[letter] < 0)
                letter += step;
            if (letter >= 0 && letter < kLetterCount)
            {
                view.vod_focus = letter_starts_[letter];
                top_ = view.vod_focus / columns() * columns();
                feedback.play(audio::Cue::focus);
            }
            else if (!input.nav_repeat)
                feedback.play(audio::Cue::error);
        }
        return;
    }
    if (input.is_pressed(Action::west) && focused() && focused()->kind == Kind::item)
    {
        details_open_ = true;
        detail_item_id_ = model.vod().catalog()[focused()->item].id;
        detail_scroll_ = 0;
        model.vod().request_details(focused()->item);
        return;
    }
    if (input.is_pressed(Action::touch) && view.vod_all && columns() > 1)
    {
        view.vod_all = false;
        view.vod_query.clear();
        view.vod_genre.clear();
        view.vod_year.clear();
        view.vod_category.clear();
        view.vod_focus = 0;
        sync();
        return;
    }
    if (input.is_pressed(Action::touch) && focused() && focused()->kind == Kind::category &&
        view.vod_kind != static_cast<int>(VodKind::episodes))
    {
        const auto category = focused()->category;
        if (!model.parental().enabled())
            model.announce(Level::warning, tr("Set a parent PIN in Settings first"), "", 3);
        else if (!model.set_category_rule(category, (model.category_rule(category, false) + 1) % 3))
            feedback.play(audio::Cue::error);
        sync();
        return;
    }
    if (input.is_pressed(Action::west) && focused() && focused()->kind == Kind::category &&
        view.vod_kind != static_cast<int>(VodKind::episodes))
    {
        const auto category = focused()->category;
        if (!model.hide_category(category, !model.category_hidden(category)))
            feedback.play(audio::Cue::error);
        sync();
        return;
    }
    if (input.is_pressed(Action::north) && view.vod_kind >= 0)
    {
        model.ask_vod_query();
        return;
    }
    const auto old = view.vod_focus;
    if (input.nav == Direction::right && !input.nav_repeat && has_letters() &&
        (view.vod_focus % columns() == columns() - 1 ||
         view.vod_focus + 1 == static_cast<int>(rows_.size())))
    {
        enter_letters();
        feedback.play(audio::Cue::focus);
        return;
    }
    if (input.nav == Direction::up)
        view.vod_focus -= columns();
    if (input.nav == Direction::down)
        view.vod_focus += columns();
    if (input.nav == Direction::left && view.vod_focus % columns() > 0)
        --view.vod_focus;
    if (input.nav == Direction::right && view.vod_focus % columns() + 1 < columns())
        ++view.vod_focus;
    // L2 and R2 turn a page; held, they keep turning: after 0.4 s every
    // 0.12 s, after 1.6 s every 0.07 s, then two, four and eight pages a turn
    // (a provider's "all movies" is tens of thousands of rows).
    {
        const double now =
            std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        const int pressed = input.is_pressed(Action::jump_next)   ? 1
                            : input.is_pressed(Action::jump_prev) ? -1
                                                                  : 0;
        const int held = input.is_held(Action::jump_next)   ? 1
                         : input.is_held(Action::jump_prev) ? -1
                                                            : 0;
        int pages = 0;
        if (pressed != 0)
        {
            page_dir_ = pressed;
            page_started_ = now;
            page_due_ = 0.40;
            pages = pressed;
        }
        else if (held == 0 || held != page_dir_)
            page_dir_ = 0;
        else if (now - page_started_ >= page_due_)
        {
            const double hold = now - page_started_;
            page_due_ += hold >= 1.6 ? 0.07 : 0.12;
            pages = held * (hold >= 7.0 ? 8 : hold >= 5.0 ? 4 : hold >= 3.0 ? 2 : 1);
        }
        view.vod_focus += pages * page_size();
    }
    view.vod_focus = std::clamp(view.vod_focus, 0, std::max(0, static_cast<int>(rows_.size()) - 1));
    if (old != view.vod_focus)
        feedback.play(audio::Cue::focus);
    if (view.vod_focus < top_)
        top_ = view.vod_focus / columns() * columns();
    if (view.vod_focus >= top_ + page_size())
        top_ = (view.vod_focus / columns() - (columns() == 1 ? kRows - 1 : 1)) * columns();
    if (!input.is_pressed(Action::confirm) || !focused() || !model.vod().available())
        return;
    letters_focused_ = false;
    const auto row = *focused();
    if (row.count == 0)
        return;
    if (row.kind == Kind::movies || row.kind == Kind::shows)
    {
        view.vod_genre.clear();
        view.vod_year.clear();
        view.vod_sort = 0;
        view.vod_kind =
            static_cast<int>(row.kind == Kind::movies ? VodKind::movies : VodKind::series);
        view.vod_all = true;
        model.vod().select(static_cast<VodKind>(view.vod_kind));
    }
    else if (row.kind == Kind::resume)
    {
        if (model.play_series_resume())
            feedback.play(audio::Cue::select);
        return;
    }
    else if (row.kind == Kind::category)
        view.vod_category = row.category;
    else if (row.kind == Kind::all)
        view.vod_all = true;
    else if (view.vod_kind == static_cast<int>(VodKind::series))
    {
        if (!model.vod_allowed(row.item))
            return;
        const auto show = model.vod().catalog()[row.item];
        view.vod_genre.clear();
        view.vod_year.clear();
        view.vod_sort = 0;
        view.vod_series = show.tvg_id;
        view.vod_series_name = show.name;
        view.vod_series_cover = show.tvg_logo;
        view.vod_series_group = show.group_title;
        view.vod_series_adult = show.adult;
        view.vod_kind = static_cast<int>(VodKind::episodes);
        view.vod_category.clear();
        view.vod_query.clear();
        view.vod_all = false;
        model.vod().select(VodKind::episodes, view.vod_series);
    }
    else
    {
        if (model.play_vod(row.item))
            feedback.play(audio::Cue::select);
        return;
    }
    feedback.play(audio::Cue::select);
    view.vod_focus = 0;
    sync();
}
std::vector<std::string> VodScreen::image_urls() const
{
    std::vector<std::string> urls;
    const auto add = [&](const Row &row)
    {
        if (!shows_a_title(row))
            return;
        const auto item = shared_.model.vod().catalog()[row.item];
        const auto &details = shared_.model.vod().details(item.id);
        auto image = details.poster.empty() ? std::string(item.tvg_logo) : details.poster;
        if (image.empty())
            image = shared_.model.view.vod_series_cover;
        if (!image.empty() && std::find(urls.begin(), urls.end(), image) == urls.end())
            urls.push_back(std::move(image));
    };
    if (const auto *row = focused())
        add(*row);
    for (int i = top_; i < int(rows_.size()) && i < top_ + page_size(); ++i)
        add(rows_[i]);
    return urls;
}
void VodScreen::draw(ui::Canvas &canvas) const
{
    const auto &model = shared_.model;
    const auto &view = model.view;
    const auto &fonts = canvas.fonts;
    auto &draw = canvas.list;
    const auto white = Color::rgb(0xf2f5fc);
    const auto muted = Color::rgb(0x9ba9c0);
    const auto accent = Color::rgb(0x9fc0ff);
    const auto panel = Color::rgb(0x151e2e);
    const std::string heading = view.vod_kind < 0    ? tr("On demand")
                                : view.vod_kind == 0 ? tr("Movies")
                                : view.vod_kind == 1 ? tr("TV shows")
                                                     : view.vod_series_name;
    const auto &face = title_face(fonts, heading);
    ui::text(draw, fonts.semibold, tr("YOUR LIBRARY"), kMargin, 163, 17, accent, gfx::Align::left,
             3);
    ui::text(draw, face, face.font->fit(readable(face, heading), 46, 1180), kMargin, 219, 46,
             white);
    std::string location = view.vod_category.empty() ? tr("All categories") : view.vod_category;
    if (!view.vod_query.empty())
        location += " · " + view.vod_query;
    if (!view.vod_genre.empty())
        location += " · " + view.vod_genre;
    if (!view.vod_year.empty())
        location += " · " + view.vod_year;
    if (view.vod_kind >= 0 && view.vod_sort == 1)
        location += " · " + std::string(tr("Title A-Z"));
    const auto &location_face = face_for(fonts, fonts.regular, location);
    ui::text(draw, location_face,
             location_face.font->fit(readable(location_face, location), 21, 1160), kMargin, 252, 21,
             muted);
    const auto poster = [&](const Row &row, Rect box, bool selected)
    {
        const auto colors = art_colors(row.text);
        draw.bordered_rect(box, 12, panel, selected ? 3.5f : 1.0f,
                           selected ? accent : Color::rgb(0x26334a));
        ImageTexture image;
        if (shows_a_title(row))
        {
            const auto item = model.vod().catalog()[row.item];
            const auto &detail = model.vod().details(item.id);
            const auto url =
                detail.poster.empty() ? item.tvg_logo : std::string_view(detail.poster);
            image =
                shared_.images.find(url.empty() ? std::string_view(view.vod_series_cover) : url);
        }
        if (image.id)
        {
            const float scale = std::min((box.w - 8) / image.width, (box.h - 8) / image.height);
            const float w = image.width * scale, h = image.height * scale;
            draw.image(image.id, {box.cx() - w / 2, box.cy() - h / 2, w, h}, {0, 0, 1, 1}, kWhite);
        }
        else
        {
            draw.rounded_rect({box.x + 8, box.y + 8, box.w - 16, box.h - 16}, 8,
                              colors.bottom.with_alpha(0.45f));
            draw.rounded_rect({box.x + 23, box.y + 26, box.w - 46, box.h - 52}, 5,
                              Color::rgb(0x23344c).with_alpha(0.8f));
            iptv::Channel named;
            named.name = row.text;
            const auto initials = monogram(named);
            const auto &font = face_for(fonts, fonts.display, initials);
            ui::text(draw, font, readable(font, initials), box.cx(), box.cy() + 16, 52, accent,
                     gfx::Align::center);
            ui::text(draw, fonts.regular,
                     shows_a_title(row) ? tr("Artwork unavailable") : tr("Collection"), box.cx(),
                     box.y + box.h - 34, 16, muted, gfx::Align::center);
        }
    };
    if (view.vod_kind < 0)
    {
        ui::text(draw, fonts.regular, tr("Choose what to watch."), kMargin, 300, 25, muted);
        for (int i = 0; i < int(rows_.size()); ++i)
        {
            const auto &row = rows_[i];
            const bool selected = view.vod_focus == i;
            const Rect box{kMargin + i * 880.0f, 352, 848, 450};
            draw.bordered_rect(box, 24, selected ? Color::rgb(0x1d304c) : panel, selected ? 3 : 1,
                               selected ? accent : Color::rgb(0x26334a));
            for (int n = 2; n >= 0; --n)
                draw.bordered_rect({box.x + 518 + n * 37.0f, box.y + 65 + n * 24.0f, 142, 208}, 8,
                                   Color::rgb(n == 0 ? 0x4d6c97 : 0x293b56), 1,
                                   Color::rgb(0x7189aa));
            const auto &font = face_for(fonts, fonts.semibold, row.text);
            ui::text(draw, font, readable(font, row.text), box.x + 40, box.y + 116, 48, white);
            const auto count = row.count ? group_digits(*row.count) + " " +
                                               (*row.count == 1 ? tr("title") : tr("titles"))
                               : model.search_loading() ? std::string(tr("Loading…"))
                                                        : std::string("—");
            ui::text(draw, fonts.regular, count, box.x + 42, box.y + 166, 24, muted);
            ui::text(draw, fonts.semibold, tr("Browse library"), box.x + 42, box.y + 376, 25,
                     selected ? accent : muted);
        }
    }
    else if (columns() > 1)
    {
        for (int i = top_; i < int(rows_.size()) && i < top_ + page_size(); ++i)
        {
            const auto &row = rows_[i];
            const int slot = i - top_;
            const float x = kMargin + (slot % columns()) * 232.0f;
            const float y = 282 + (slot / columns()) * 332.0f;
            poster(row, {x, y, 208, 260}, i == view.vod_focus && !letters_focused_);
            const auto &font = face_for(fonts, fonts.semibold, row.text);
            ui::paragraph(draw, font, readable(font, row.text), x, y + 288, 21, 208, 25,
                          i == view.vod_focus ? white : muted, 2);
        }
        draw.rounded_rect({1320, 282, 504, 610}, 18, panel);
        if (const auto *row = focused())
        {
            const auto &font = face_for(fonts, fonts.semibold, row->text);
            ui::paragraph(draw, font, readable(font, row->text), 1352, 338, 34, 440, 43, white, 3);
            std::string metadata, plot;
            if (shows_a_title(*row))
            {
                const auto item = model.vod().catalog()[row->item];
                const auto &details = model.vod().details(item.id);
                metadata = details.year;
                if (!details.rating.empty())
                    metadata += std::string(metadata.empty() ? "" : " · ") + tr("Rating") + " " +
                                details.rating;
                if (!details.genre.empty())
                    metadata += (metadata.empty() ? "" : " · ") + details.genre;
                plot = details.plot;
                if (plot.empty())
                    plot = item.group_title;
            }
            else
            {
                metadata = tr("Collection");
                plot = tr("Open this collection to explore its titles.");
            }
            const auto &meta = face_for(fonts, fonts.regular, metadata);
            ui::paragraph(draw, meta, readable(meta, metadata), 1352, 497, 21, 440, 30, accent, 2);
            const auto &body = face_for(fonts, fonts.regular, plot);
            ui::paragraph(draw, body, readable(body, plot), 1352, 584, 24, 440, 35, muted, 6);
            ui::text(draw, fonts.semibold,
                     row->kind == Kind::item && view.vod_kind == 0 ? tr("Watch movie") : tr("Open"),
                     1352, 848, 24, white);
        }
        ui::text(draw, fonts.mono,
                 rows_.empty()
                     ? "0 / 0"
                     : group_digits(view.vod_focus + 1) + " / " + group_digits(rows_.size()),
                 1232, 951, 19, muted, gfx::Align::right);
    }
    else
    {
        for (int i = top_; i < int(rows_.size()) && i < top_ + kRows; ++i)
        {
            const auto &row = rows_[i];
            const float y = 288 + (i - top_) * 85.0f;
            const bool selected = i == view.vod_focus;
            draw.bordered_rect({kMargin, y, 1110, 75}, 12, selected ? Color::rgb(0x243b5b) : panel,
                               selected ? 2 : 0, accent);
            const auto &font = face_for(fonts, fonts.semibold, row.text);
            ui::text(draw, font, font.font->fit(readable(font, row.text), 25, 1040), kMargin + 24,
                     y + 46, 25, selected ? white : muted);
        }
        if (const auto *row = focused(); row && shows_a_title(*row))
            poster(*row, {1350, 300, 350, 470}, true);
    }
    if (rows_.empty())
        ui::text(draw, fonts.regular,
                 model.vod().busy() || model.vod().requested() ? tr("Loading…")
                                                               : tr("No matching titles"),
                 kMargin, 370, 30, muted);
    const auto status = model.vod().status();
    ui::text(draw, fonts.regular, fonts.regular.font->fit(status, 19, 1050), kMargin, 951, 19,
             muted);
    if (has_letters())
        draw_letters(canvas);
    if (details_open_)
        draw_details(canvas);
    if (filters_open_)
        draw_filters(canvas);
}
void VodScreen::draw_letters(ui::Canvas &canvas) const
{
    auto &draw = canvas.list;
    const auto accent = Color::rgb(0x9fc0ff);
    const auto muted = Color::rgb(0x9ba9c0);
    ui::text(draw, canvas.fonts.semibold, "A–Z", kLetterX, 268, 14, muted, gfx::Align::center);
    const int selected = current_letter();
    for (int letter = 0; letter < kLetterCount; ++letter)
    {
        const float y = kLetterTop + letter * kLetterPitch;
        const bool available = letter_starts_[letter] >= 0;
        const bool active = available && letter == selected;
        if (active)
            draw.rounded_rect({kLetterX - 15, y - 10, 30, 21}, 7,
                              letters_focused_ ? accent : Color::rgb(0x243b5b));
        const char label[]{letter_char(letter), '\0'};
        ui::text(draw, canvas.fonts.semibold, label, kLetterX, baseline_for(y, 17), 17,
                 active && letters_focused_ ? Color::rgb(0x0b101a)
                 : active                   ? accent
                                            : muted.with_alpha(available ? 0.9f : 0.25f),
                 gfx::Align::center);
    }
}
int VodScreen::hints(ui::Hint *out, int capacity) const
{
    if (letters_focused_)
    {
        const ui::Hint hints[] = {{ui::Button::dpad, tr("Choose letter")},
                                  {ui::Button::cross, tr("Back to titles")},
                                  {ui::Button::circle, tr("Back")}};
        const int count = std::min(capacity, static_cast<int>(std::size(hints)));
        std::copy_n(hints, count, out);
        return count;
    }
    ui::Hint hints[7];
    int count = 0;
    if (!focused() || focused()->count != 0)
        hints[count++] = {ui::Button::cross, tr("Open / Watch")};
    if (shared_.model.view.vod_kind >= 0)
    {
        if (focused() && focused()->kind == Kind::item)
            hints[count++] = {ui::Button::square, tr("Details")};
        if (shared_.model.view.vod_all && columns() > 1)
            hints[count++] = {ui::Button::touchpad, tr("Categories")};
        if (focused() && focused()->kind == Kind::category && columns() > 1)
        {
            hints[count++] = {ui::Button::square, tr("Show / Hide")};
            hints[count++] = {ui::Button::touchpad, tr("Normal / PIN / Kids")};
        }
        hints[count++] = {ui::Button::right_stick, tr("Filter / Sort")};
        hints[count++] = {ui::Button::triangle, tr("Search")};
    }
    hints[count++] = {ui::Button::circle, tr("Back")};
    if (count < 6)
        hints[count++] = {ui::Button::options, tr("Update")};
    const int copied = std::min(capacity, count);
    std::copy_n(hints, copied, out);
    return copied;
}
unsigned VodScreen::result_count() const
{
    return static_cast<unsigned>(std::count_if(rows_.begin(), rows_.end(), [](const Row &row)
                                               { return row.kind == Kind::item; }));
}
std::vector<std::string> VodScreen::result_titles() const
{
    std::vector<std::string> titles;
    for (const auto &row : rows_)
        if (row.kind == Kind::item)
            titles.push_back(row.text);
    return titles;
}
void VodScreen::handle_filters(const InputFrame &input)
{
    auto &view = shared_.model.view;
    if (input.is_pressed(Action::confirm) || input.is_pressed(Action::r3))
    {
        filters_open_ = false;
        return;
    }
    if (input.nav == Direction::up)
        filter_focus_ = std::max(0, filter_focus_ - 1);
    if (input.nav == Direction::down)
        filter_focus_ = std::min(2, filter_focus_ + 1);
    if (input.is_pressed(Action::west))
    {
        view.vod_genre.clear();
        view.vod_year.clear();
        view.vod_sort = 0;
        view.vod_focus = 0;
        sync();
        return;
    }
    const int direction = input.nav == Direction::right ? 1 : input.nav == Direction::left ? -1 : 0;
    if (!direction)
        return;
    if (filter_focus_ == 2)
        view.vod_sort = (view.vod_sort + direction + kSortCount) % kSortCount;
    else
    {
        std::set<std::string> facets;
        const auto &vod = shared_.model.vod();
        for (unsigned i = 0; i < vod.catalog().size(); ++i)
        {
            if (!shared_.model.vod_allowed(i))
                continue;
            const auto &details = vod.details(vod.catalog()[i].id);
            if (filter_focus_ == 0)
            {
                for (auto &genre : vod_genres(details.genre))
                    if (facets.size() < 256)
                        facets.insert(std::move(genre));
            }
            else if (!details.year.empty())
                facets.insert(details.year);
        }
        std::vector<std::string> choices{std::string()};
        choices.insert(choices.end(), facets.begin(), facets.end());
        auto &value = filter_focus_ == 0 ? view.vod_genre : view.vod_year;
        const auto found = std::find(choices.begin(), choices.end(), value);
        int at = found == choices.end() ? 0 : static_cast<int>(found - choices.begin());
        at = (at + direction + static_cast<int>(choices.size())) % static_cast<int>(choices.size());
        value = choices[at];
    }
    view.vod_focus = 0;
    sync();
}
void VodScreen::draw_filters(ui::Canvas &canvas) const
{
    auto &list = canvas.list;
    const auto &view = shared_.model.view;
    list.rounded_rect({220, 190, 1480, 650}, 24, Color::rgb(0x11121c));
    ui::text(list, canvas.fonts.semibold, tr("Filter and sort"), 280, 265, 38, kWhite);
    const int sort = std::clamp(view.vod_sort, 0, kSortCount - 1);
    const bool episodes = view.vod_kind == static_cast<int>(VodKind::episodes);
    const std::string values[] = {
        std::string(tr("Genre")) + ": " + (view.vod_genre.empty() ? tr("All") : view.vod_genre),
        std::string(tr("Year")) + ": " + (view.vod_year.empty() ? tr("All") : view.vod_year),
        std::string(tr("Sort")) + ": " +
            tr(sort == 0 && episodes ? "Episode order" : kSorts[sort])};
    for (int i = 0; i < 3; ++i)
    {
        const float y = 310 + i * 105.0f;
        list.bordered_rect({270, y, 1380, 86}, 12, kWhite.with_alpha(0.05f),
                           filter_focus_ == i ? 2 : 0, tone::accent);
        const auto &face = face_for(canvas.fonts, canvas.fonts.regular, values[i]);
        ui::text(list, face, face.font->fit(readable(face, values[i]), 28, 1280), 300, y + 54, 28,
                 kWhite);
    }
    const ui::Hint hints[] = {{ui::Button::dpad, tr("Change")},
                              {ui::Button::square, tr("Reset")},
                              {ui::Button::cross, tr("Close")}};
    ui::HintLayout layout;
    layout.size = 32.0f;
    layout.text_size = 24.0f;
    layout.item_gap = 34.0f;
    layout.cy = 702.0f;
    draw_hint_row(list, canvas.fonts, hints, 3, 280, 1360, layout);
    ui::text(list, canvas.fonts.regular,
             tr("Filters use loaded metadata. Open a title to fetch missing details."), 280, 770,
             22, shared_.theme.text_muted);
}
void VodScreen::draw_details(ui::Canvas &canvas) const
{
    const auto *row = focused();
    if (!row || row->kind != Kind::item)
        return;
    const auto &model = shared_.model;
    const auto item = model.vod().catalog()[row->item];
    const auto &details = model.vod().details(item.id);
    auto &list = canvas.list;
    list.rounded_rect({90, 120, 1740, 815}, 24, Color::rgb(0x11121c));
    const auto &title = face_for(canvas.fonts, canvas.fonts.semibold, item.name);
    ui::text(list, title, title.font->fit(readable(title, item.name), 36, 1610), 140, 183, 36,
             kWhite);
    const std::string summary =
        details.year + (details.genre.empty() ? "" : "  /  " + details.genre) +
        (details.rating.empty()
             ? ""
             : std::string("  /  ") + tr("Rating") + " " + details.rating + " / 10");
    const auto &summary_face = face_for(canvas.fonts, canvas.fonts.regular, summary);
    ui::text(list, summary_face, summary_face.font->fit(readable(summary_face, summary), 24, 1560),
             140, 230, 24, tone::accent);
    const auto urls = image_urls();
    const auto image = urls.empty() ? ImageTexture{} : shared_.images.find(urls.front());
    const Rect cover{140, 270, 320, 480};
    if (image.id)
    {
        const float scale = std::min(cover.w / image.width, cover.h / image.height);
        const float w = image.width * scale, h = image.height * scale;
        list.image(image.id, {cover.cx() - w / 2, cover.cy() - h / 2, w, h}, {0, 0, 1, 1}, kWhite);
    }
    list.push_clip({510, 270, 1240, 540});
    float y = 302 - detail_scroll_;
    const std::string plot = !details.plot.empty() ? details.plot
                             : model.vod().details_loading(item.id)
                                 ? tr("Loading title details...")
                                 : tr("No synopsis supplied by the provider.");
    const auto &plot_face = face_for(canvas.fonts, canvas.fonts.regular, plot);
    y = ui::paragraph(list, plot_face, readable(plot_face, plot), 510, y, 27, 1210, 39, kWhite,
                      200) +
        35;
    if (!details.cast.empty())
    {
        const std::string cast = std::string(tr("Cast")) + ": " + details.cast;
        const auto &cast_face = face_for(canvas.fonts, canvas.fonts.regular, cast);
        ui::paragraph(list, cast_face, readable(cast_face, cast), 510, y, 24, 1210, 36,
                      shared_.theme.text_muted, 100);
    }
    list.pop_clip();
    const ui::Hint hints[] = {{ui::Button::dpad, tr("Scroll")},
                              {ui::Button::cross, tr("Open / Watch")},
                              {ui::Button::circle, tr("Back")}};
    ui::HintLayout layout;
    layout.size = 32.0f;
    layout.text_size = 23.0f;
    layout.item_gap = 34.0f;
    layout.cy = 858.0f;
    draw_hint_row(list, canvas.fonts, hints, 3, 140, 1640, layout);
    ui::text(list, canvas.fonts.regular,
             canvas.fonts.regular.font->fit(model.vod().status(), 20, 1570), 140, 905, 20,
             shared_.theme.text_muted);
}
} // namespace ptv
