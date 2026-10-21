// ProsperoTV - Search and filter: a drawer over the channel list it narrows.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/i18n.hpp"
#include "tv/search_sheet.hpp"

#include "tv/draw.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace ptv
{

namespace
{

constexpr float kSheetWidth = 820.0f;
constexpr float kFieldHeight = 68.0f;
constexpr float kRowHeight = 64.0f;
constexpr float kRowGap = 12.0f;

std::vector<ui::SelectOption> options_of(const char *any, std::span<const Facet> facets)
{
    std::vector<ui::SelectOption> options;
    options.reserve(facets.size() + 1);
    options.emplace_back(any);
    for (const Facet &facet : facets)
        options.emplace_back(facet.value,
                             group_digits(facet.count) +
                                 (facet.count == 1 ? tr(" channel") : tr(" channels")));
    return options;
}

// The option a filter value stands for: 0 is "all".
int index_of(std::span<const Facet> facets, const std::string &value)
{
    if (value.empty())
        return 0;
    for (std::size_t i = 0; i < facets.size(); ++i)
        if (equals_nocase(facets[i].value, value))
            return static_cast<int>(i) + 1;
    return 0;
}

void style_select(ui::Select &select, const ui::Theme &theme, const char *label)
{
    select.style.theme = theme;
    select.style.label = ui::SelectLabel::inside;
    select.style.field_height = kRowHeight;
    select.style.value_size = 24.0f;
    select.style.max_rows = 6;
    select.style.scrim = 0.25f;
    select.set_label(label);
    select.set_limits({kMargin, 110.0f, kWidth - kMargin - 20.0f, 870.0f});
}

} // namespace

SearchSheet::SearchSheet(Shared &shared) : shared_(shared)
{
    const ui::Theme &theme = shared.theme;
    sheet_.style.theme = theme;
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = kSheetWidth;
    sheet_.style.padding = 48.0f;
    sheet_.style.title_size = 40.0f;
    sheet_.style.scrim = 0.55f;
    sheet_.style.scrim_color = tone::night;
    sheet_.set_title(tr("Search and filter"));
    sheet_.content = [this](ui::Canvas &canvas, const Rect &, float opacity)
    { draw_content(canvas, opacity); };

    const Rect area = this->area();
    float y = area.y + 8.0f;

    field_.style.theme = theme;
    field_.style.field_height = kFieldHeight;
    field_.style.text_size = 26.0f;
    field_.style.remember = false;
    field_.style.max_rows = 0;
    field_.style.exits.down = true;
    field_.style.exits.left = true; // to the results, for a remote without L1
    field_.set_placeholder(tr("Channels, movies, shows, programmes"));
    field_.set_bounds({area.x, y, area.w, kFieldHeight});
    y += kFieldHeight + 46.0f;

    style_select(country_, theme, tr("Country"));
    style_select(category_, theme, tr("Category"));
    style_select(language_, theme, tr("Language"));
    country_.set_bounds({area.x, y, area.w, kRowHeight});
    y += kRowHeight + kRowGap;
    category_.set_bounds({area.x, y, area.w, kRowHeight});
    y += kRowHeight + kRowGap;
    language_.set_bounds({area.x, y, area.w, kRowHeight});
    y += kRowHeight + 46.0f;

    quality_.style.theme = theme;
    quality_.style.kind = ui::TabKind::segmented;
    quality_.style.width = ui::TabWidth::fill;
    quality_.style.height = 58.0f;
    quality_.style.text_size = 22.0f;
    quality_.style.padding = 10.0f;
    std::vector<ui::TabItem> sizes;
    sizes.push_back({tr("Any")});
    for (unsigned quality = 1; quality < kQualityCount; ++quality)
        sizes.push_back({quality_filter_name(quality)});
    quality_.set_tabs(std::move(sizes));
    quality_.set_bounds({area.x, y, area.w, 58.0f});
    quality_.set_focused(false);

    count_.snap(0.0f);
    sync();
}

// Where the drawer's content goes: the sheet's own content area, stopped at
// the screen's safe edge.
Rect SearchSheet::area() const
{
    Rect inside = sheet_.content_rect();
    inside.w = kWidth - kMargin - inside.x;
    return inside;
}

ui::Select &SearchSheet::select_of(Zone zone)
{
    return zone == Zone::country ? country_ : zone == Zone::category ? category_ : language_;
}

bool SearchSheet::usable(Zone zone) const
{
    const Model &model = shared_.model;
    switch (zone)
    {
    case Zone::country:
        return !model.countries().empty();
    case Zone::category:
        return !model.categories().empty();
    case Zone::language:
        return !model.languages().empty();
    default:
        return true;
    }
}

void SearchSheet::sync()
{
    const Model &model = shared_.model;
    seen_revision_ = model.revision();
    country_.set_options(options_of(tr("All countries"), model.countries()));
    category_.set_options(options_of(tr("All categories"), model.categories()));
    language_.set_options(options_of(tr("All languages"), model.languages()));
    country_.set_index(index_of(model.countries(), model.country()));
    category_.set_index(index_of(model.categories(), model.category()));
    language_.set_index(index_of(model.languages(), model.language()));
    if (quality_.active() != static_cast<int>(model.quality()))
        quality_.set_active(static_cast<int>(model.quality()), true);
    if (field_.text() != model.query())
        field_.set_text(model.query());
}

void SearchSheet::open(ui::Feedback &feedback)
{
    shared_.model.begin_search();
    sync();
    sync_results();
    zone_ = Zone::field;
    count_.snap(static_cast<float>(shared_.model.visible_count()));
    sheet_.open(feedback);
}

void SearchSheet::dismiss()
{
    shared_.model.end_search();
    country_.dismiss();
    category_.dismiss();
    language_.dismiss();
    sheet_.dismiss();
}

void SearchSheet::go(Zone zone, ui::Feedback &feedback)
{
    if (zone == zone_)
        return;
    zone_ = zone;
    feedback.play(audio::Cue::focus, 1.0f, 0.4f);
}

// The next zone up (-1) or down (1) that has something to choose.
void SearchSheet::step(int direction, ui::Feedback &feedback)
{
    static constexpr Zone order[] = {Zone::field,    Zone::country, Zone::category,
                                     Zone::language, Zone::quality, Zone::show};
    constexpr int count = static_cast<int>(std::size(order));
    int at = 0;
    const Zone here = zone_ == Zone::reset ? Zone::show : zone_;
    for (int i = 0; i < count; ++i)
        if (order[i] == here)
            at = i;
    for (int next = at + direction; next >= 0 && next < count; next += direction)
        if (usable(order[next]))
        {
            go(order[next], feedback);
            return;
        }
    // The end of the drawer answers softly.
    if (direction != 0)
        feedback.play(audio::Cue::error, 1.0f, 0.4f, 0.6f);
}

void SearchSheet::reset(ui::Feedback &feedback)
{
    Model &model = shared_.model;
    if (!model.filtering())
    {
        feedback.play(audio::Cue::error, 1.0f, 0.4f, 0.6f);
        return;
    }
    model.clear_filters();
    sync();
    feedback.play(audio::Cue::erase);
}

void SearchSheet::handle(const InputFrame &input, ui::Feedback &feedback)
{
    Model &model = shared_.model;
    // The keyboard answers between two frames: the field takes its text
    // before anything compares the two.
    if (model.revision() != seen_revision_)
        sync();

    sync_results();
    if (input.is_pressed(Action::page_prev))
    {
        country_.dismiss();
        category_.dismiss();
        language_.dismiss();
        go(Zone::results, feedback);
        return;
    }
    if (input.is_pressed(Action::page_next))
    {
        go(Zone::field, feedback);
        return;
    }
    if (zone_ == Zone::results)
    {
        if (input.is_pressed(Action::back) || input.is_pressed(Action::north))
            sheet_.close(feedback);
        else if (input.nav == Direction::right)
            go(Zone::field, feedback);
        else if (input.nav == Direction::up)
            result_focus_ = std::max(0, result_focus_ - 1);
        else if (input.nav == Direction::down)
            result_focus_ = std::min(std::max(0, int(results_.size()) - 1), result_focus_ + 1);
        else if (input.is_pressed(Action::jump_prev))
            result_focus_ = std::max(0, result_focus_ - 7);
        else if (input.is_pressed(Action::jump_next))
            result_focus_ = std::min(std::max(0, int(results_.size()) - 1), result_focus_ + 7);
        else if (input.is_pressed(Action::confirm) && !results_.empty())
        {
            const auto result = results_[result_focus_];
            bool accepted = false;
            if (result.kind == Model::SearchResult::Kind::channel)
                accepted = model.play_channel(result.id);
            else if (result.kind == Model::SearchResult::Kind::programme)
            {
                navigation_ = result;
                accepted = true;
            }
            else if ((accepted = model.activate_search_vod(result)) &&
                     result.kind == Model::SearchResult::Kind::show)
                navigation_ = result;
            if (accepted)
                sheet_.close(feedback);
            else
                feedback.play(audio::Cue::error);
        }
        result_top_ = std::clamp(result_top_, std::max(0, result_focus_ - 6), result_focus_);
        return;
    }

    // ---- an open dropdown takes everything ----
    for (const Zone zone : {Zone::country, Zone::category, Zone::language})
    {
        ui::Select &select = select_of(zone);
        const bool wanted = zone_ == zone && input.is_pressed(Action::confirm);
        if (!select.is_open() && !wanted)
            continue;
        if (select.handle(input, feedback) == ui::Event::changed)
        {
            const int index = select.index();
            const auto pick = [&](std::span<const Facet> facets) {
                return index > 0 ? facets[static_cast<std::size_t>(index - 1)].value
                                 : std::string();
            };
            if (zone == Zone::country)
                model.set_country(pick(model.countries()));
            else if (zone == Zone::category)
                model.set_category(pick(model.categories()));
            else
                model.set_language(pick(model.languages()));
            seen_revision_ = model.revision();
        }
        return;
    }

    // Triangle opened the drawer and puts it away again.
    if (input.is_pressed(Action::north))
    {
        sheet_.close(feedback);
        return;
    }
    if (sheet_.handle(input, feedback) != ui::Event::none)
        return;
    if (input.is_pressed(Action::west))
    {
        reset(feedback);
        return;
    }

    const Direction nav = input.nav;
    switch (zone_)
    {
    case Zone::results:
        break;
    case Zone::field:
    {
        const ui::Event event = field_.handle(input, feedback);
        if (event == ui::Event::activated && !field_.has_pick())
            model.ask_query();
        if (field_.text() != model.query())
        {
            // The field's own clear button.
            model.set_query(field_.text());
            seen_revision_ = model.revision();
        }
        if (field_.exit() == Direction::down)
            step(1, feedback);
        else if (field_.exit() == Direction::left)
            go(Zone::results, feedback);
        break;
    }
    case Zone::country:
    case Zone::category:
    case Zone::language:
        if (nav == Direction::up)
            step(-1, feedback);
        else if (nav == Direction::down)
            step(1, feedback);
        break;
    case Zone::quality:
        if (nav == Direction::up)
        {
            step(-1, feedback);
        }
        else if (nav == Direction::down)
        {
            step(1, feedback);
        }
        else if (quality_.handle(input, feedback) == ui::Event::changed)
        {
            model.set_quality(static_cast<unsigned>(quality_.active()));
            seen_revision_ = model.revision();
        }
        break;
    case Zone::show:
    case Zone::reset:
        if (nav == Direction::up)
        {
            step(-1, feedback);
        }
        else if (nav == Direction::left || nav == Direction::right)
        {
            const Zone other = nav == Direction::right ? Zone::reset : Zone::show;
            if (other == zone_)
                feedback.play(audio::Cue::error, 1.0f, 0.4f, 0.6f);
            else
                go(other, feedback);
        }
        else if (nav == Direction::down)
        {
            feedback.play(audio::Cue::error, 1.0f, 0.4f, 0.6f);
        }
        else if (input.is_pressed(Action::confirm))
        {
            press_.trigger();
            if (zone_ == Zone::show)
            {
                feedback.play(audio::Cue::select, 1.0f, 0.4f);
                sheet_.close(feedback);
            }
            else
            {
                reset(feedback);
            }
        }
        break;
    }
}

void SearchSheet::update(float dt)
{
    const Model &model = shared_.model;
    const bool reduced = shared_.settings.reduced_motion;
    // The keyboard, a download or the list behind may have changed the answer.
    if (model.revision() != seen_revision_)
        sync();
    if (!usable(zone_))
        zone_ = Zone::field;

    const bool open = sheet_.is_open();
    if (open && !model.searching() && !model.refreshing())
        shared_.model.begin_search();
    if (!open && model.searching())
        shared_.model.end_search();
    if (open)
        sync_results();
    field_.set_active(open && zone_ == Zone::field);
    country_.set_active(open && zone_ == Zone::country);
    category_.set_active(open && zone_ == Zone::category);
    language_.set_active(open && zone_ == Zone::language);
    quality_.set_focused(open && zone_ == Zone::quality);
    show_focus_.target = open && zone_ == Zone::show ? 1.0f : 0.0f;
    reset_focus_.target = open && zone_ == Zone::reset ? 1.0f : 0.0f;
    count_.target = static_cast<float>(model.visible_count());

    for (ui::ComponentStyle *style : {static_cast<ui::ComponentStyle *>(&sheet_.style),
                                      static_cast<ui::ComponentStyle *>(&field_.style),
                                      static_cast<ui::ComponentStyle *>(&country_.style),
                                      static_cast<ui::ComponentStyle *>(&category_.style),
                                      static_cast<ui::ComponentStyle *>(&language_.style),
                                      static_cast<ui::ComponentStyle *>(&quality_.style)})
        style->reduced_motion = reduced;

    sheet_.update(dt);
    field_.update(dt);
    country_.update(dt);
    category_.update(dt);
    language_.update(dt);
    quality_.update(dt);
    const float omega = reduced ? 60.0f : shared_.theme.omega;
    show_focus_.update(dt, omega);
    reset_focus_.update(dt, omega);
    press_.update(dt, 10.0f);
    count_.update(dt, reduced ? 60.0f : 10.0f);
}

void SearchSheet::draw_unlisted(ui::Canvas &canvas, const Rect &row, const char *label) const
{
    // A playlist that does not say where its channels are from offers nothing
    // to choose: the row says so instead of opening an empty list.
    gfx::DrawList &list = canvas.list;
    const ui::Theme &theme = shared_.theme;
    list.bordered_rect(row, 16.0f, kWhite.with_alpha(0.03f), 1.5f, kWhite.with_alpha(0.08f));
    ui::text(list, canvas.fonts.regular, label, row.x + 20.0f, baseline_for(row.cy(), 24.0f), 24.0f,
             theme.text_muted.with_alpha(0.6f));
    ui::text(list, canvas.fonts.regular, tr("Not listed by this source"), row.x + row.w - 20.0f,
             baseline_for(row.cy(), 22.0f), 22.0f, theme.text_muted.with_alpha(0.6f),
             gfx::Align::right);
}

void SearchSheet::draw_content(ui::Canvas &canvas, float) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = shared_.theme;
    const Model &model = shared_.model;
    ui::Painter paint(list, fonts, theme, canvas.glass);
    const Rect area = this->area();

    field_.draw(canvas);

    ui::text(list, fonts.semibold, tr("NARROW IT DOWN"), area.x, country_.bounds().y - 16.0f, 16.0f,
             theme.text_muted, gfx::Align::left, 3.0f);
    if (usable(Zone::country))
        country_.draw(canvas);
    else
        draw_unlisted(canvas, country_.bounds(), tr("Country"));
    if (usable(Zone::category))
        category_.draw(canvas);
    else
        draw_unlisted(canvas, category_.bounds(), tr("Category"));
    if (usable(Zone::language))
        language_.draw(canvas);
    else
        draw_unlisted(canvas, language_.bounds(), tr("Language"));

    ui::text(list, fonts.semibold, tr("PICTURE SIZE"), area.x, quality_.bounds().y - 16.0f, 16.0f,
             theme.text_muted, gfx::Align::left, 3.0f);
    quality_.draw(canvas);

    // ---- the answer ----
    const float answer = quality_.bounds().y + quality_.bounds().h + 78.0f;
    const unsigned shown = static_cast<unsigned>(std::lround(std::max(count_.value, 0.0f)));
    const float number =
        ui::text(list, fonts.mono, group_digits(shown), area.x, answer, 56.0f, theme.text);
    ui::text(list, fonts.regular,
             model.visible_count() == 1 ? tr("channel matches") : tr("channels match"),
             area.x + number + 18.0f, answer - 4.0f, 26.0f, theme.text_muted);

    // ---- what to do with it ----
    const float buttons = answer + 34.0f;
    const float gap = 16.0f;
    const float show_width = (area.w - gap) * 0.62f;
    const Rect show{area.x, buttons, show_width, 68.0f};
    const Rect reset{area.x + show_width + gap, buttons, area.w - show_width - gap, 68.0f};
    paint.button(show, tr("Show the channels"), ui::ButtonKind::primary,
                 {show_focus_.value, zone_ == Zone::show ? press_.value : 0.0f, false});
    paint.button(
        reset, tr("Reset"), ui::ButtonKind::secondary,
        {reset_focus_.value, zone_ == Zone::reset ? press_.value : 0.0f, !model.filtering()});
    paint.focus_ring(show, paint.control_radius(show), show_focus_.value);
    paint.focus_ring(reset, paint.control_radius(reset), reset_focus_.value);

    // ---- the drawer's own hints ----
    ui::Hint hints[4];
    int count = 0;
    const bool listing = country_.is_open() || category_.is_open() || language_.is_open();
    if (listing)
    {
        hints[count++] = {ui::Button::cross, tr("Choose")};
        hints[count++] = {ui::Button::circle, tr("Close the list")};
    }
    else
    {
        switch (zone_)
        {
        case Zone::results:
            hints[count++] = {ui::Button::cross, tr("Open")};
            break;
        case Zone::field:
            hints[count++] = {ui::Button::cross, tr("Type")};
            break;
        case Zone::country:
        case Zone::category:
        case Zone::language:
            hints[count++] = {ui::Button::cross, tr("Open the list")};
            break;
        case Zone::quality:
            hints[count++] = {ui::Button::dpad, tr("Change")};
            break;
        case Zone::show:
        case Zone::reset:
            hints[count++] = {ui::Button::cross, tr("Choose")};
            break;
        }
        if (model.filtering())
            hints[count++] = {ui::Button::square, tr("Reset")};
        hints[count++] = {ui::Button::circle, tr("Close")};
    }
    ui::HintLayout layout;
    layout.size = 36.0f;
    layout.text_size = 23.0f;
    layout.cy = kHintsY;
    layout.item_gap = 34.0f;
    ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, count, area.x, false, layout);

    // The lists open over everything else in the drawer.
    country_.draw_popover(canvas);
    category_.draw_popover(canvas);
    language_.draw_popover(canvas);
}

void SearchSheet::draw(ui::Canvas &canvas) const
{
    sheet_.draw(canvas);
    if (sheet_.is_open())
        draw_results(canvas);
}

std::optional<Model::SearchResult> SearchSheet::take_navigation()
{
    auto result = std::move(navigation_);
    navigation_.reset();
    return result;
}
void SearchSheet::sync_results()
{
    const auto &model = shared_.model;
    if (results_revision_ == model.revision() && catalog_revision_ == model.search_revision() &&
        results_query_ == model.query())
        return;
    std::optional<Model::SearchResult> focused;
    if (!results_.empty() && results_query_ == model.query())
        focused = results_[result_focus_];
    results_ = model.search_results(model.query(), &truncated_);
    results_query_ = model.query();
    results_revision_ = model.revision();
    catalog_revision_ = model.search_revision();
    result_focus_ = 0;
    if (focused)
        for (unsigned i = 0; i < results_.size(); ++i)
            if (results_[i].id == focused->id && results_[i].kind == focused->kind &&
                results_[i].programme_start == focused->programme_start)
                result_focus_ = i;
    result_top_ = std::clamp(result_top_, std::max(0, result_focus_ - 6), result_focus_);
}
void SearchSheet::draw_results(ui::Canvas &canvas) const
{
    const auto &theme = shared_.theme;
    draw_glass(canvas, theme, {kMargin, 120, 960, 850}, 24);
    ui::text(canvas.list, canvas.fonts.semibold, tr("Search results"), kMargin + 24, 170, 32,
             theme.text);
    ui::text(canvas.list, canvas.fonts.regular,
             tr("Active source / channel filters apply to live channels"), kMargin + 24, 205, 20,
             theme.text_muted);
    const char *kinds[] = {"Live", "Movie", "TV show", "Programme"};
    for (int i = result_top_; i < int(results_.size()) && i < result_top_ + 7; ++i)
    {
        const auto &result = results_[i];
        const float y = 230 + (i - result_top_) * 84;
        const bool focused = zone_ == Zone::results && i == result_focus_;
        canvas.list.bordered_rect({kMargin + 12, y, 936, 78}, 12,
                                  kWhite.with_alpha(focused ? .15f : .03f), focused ? 2 : 0,
                                  theme.text);
        ui::text(canvas.list, canvas.fonts.semibold, tr(kinds[unsigned(result.kind)]), kMargin + 26,
                 y + 32, 18, tone::accent);
        const auto &title = face_for(canvas.fonts, canvas.fonts.semibold, result.title);
        const auto &detail = face_for(canvas.fonts, canvas.fonts.regular, result.detail);
        ui::text(canvas.list, title, title.font->fit(readable(title, result.title), 26, 748),
                 kMargin + 174, y + 32, 26, theme.text);
        ui::text(canvas.list, detail, detail.font->fit(readable(detail, result.detail), 19, 748),
                 kMargin + 174, y + 61, 19, theme.text_muted);
    }
    if (results_.empty())
        ui::paragraph(canvas.list, canvas.fonts.regular,
                      results_query_.empty()
                          ? tr("Enter a title or channel name to search everything.")
                          : tr("No matches in the available lists."),
                      kMargin + 24, 300, 27, 900, 38, theme.text, 3);
    ui::paragraph(canvas.list, canvas.fonts.regular, shared_.model.search_status(), kMargin + 24,
                  850, 18, 900, 26, theme.text_muted, 3);
    ui::text(canvas.list, canvas.fonts.regular,
             truncated_ ? tr("First 250 matches per kind. Narrow the search.")
                        : group_digits(results_.size()) + tr(" results"),
             kMargin + 24, 951, 19, theme.text_muted);
    const ui::Hint hints[] = {{ui::Button::l1, tr("Results")},
                              {ui::Button::r1, tr("Search and filter")},
                              {ui::Button::cross, tr("Open")}};
    ui::HintLayout layout;
    layout.size = 36.0f;
    layout.text_size = 23.0f;
    layout.cy = kHintsY;
    layout.item_gap = 34.0f;
    draw_hint_row(canvas.list, canvas.fonts, hints, 3, kMargin + 24, 912, layout);
}

} // namespace ptv
