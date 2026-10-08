// ProsperoTV - Sources: where the channel list comes from, and how it is doing.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/sources_screen.hpp"

#include "tv/draw.hpp"
#include "tv/platform.hpp"
#include "ui/components/overlay.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace ptv
{

namespace
{

constexpr float kTop = 250.0f;
constexpr float kRowHeight = 150.0f;
constexpr float kRowGap = 18.0f;
constexpr Rect kList{kMargin, kTop, 1010.0f, 3.0f * kRowHeight + 2.0f * kRowGap};
constexpr Rect kPanel{1144.0f, kTop, kWidth - kMargin - 1144.0f, 640.0f};
constexpr float kInner = kPanel.x + 40.0f;
constexpr float kInnerWidth = kPanel.w - 80.0f;

const char *title_of(iptv::SourceKind source)
{
    switch (source)
    {
    case iptv::SourceKind::Custom:
        return "Custom playlist";
    case iptv::SourceKind::Xtream:
        return "Xtream Codes account";
    case iptv::SourceKind::OneStream:
        return "OneStream panel account";
    case iptv::SourceKind::BuiltIn:
        break;
    }
    return "iptv-org public catalog";
}

const char *health_words(SourceHealth health)
{
    switch (health)
    {
    case SourceHealth::saved:
        return "Set up";
    case SourceHealth::cached:
        return "Saved copy";
    case SourceHealth::refreshing:
        return "Updating";
    case SourceHealth::ready:
        return "Up to date";
    case SourceHealth::stale:
        return "Update failed";
    case SourceHealth::error:
        return "Not available";
    case SourceHealth::empty:
        break;
    }
    return "Not set up";
}

Color health_color(SourceHealth health)
{
    switch (health)
    {
    case SourceHealth::cached:
    case SourceHealth::ready:
        return tone::good;
    case SourceHealth::saved:
    case SourceHealth::refreshing:
    case SourceHealth::stale:
        return tone::wait;
    case SourceHealth::error:
        return tone::bad;
    case SourceHealth::empty:
        break;
    }
    return kWhite.with_alpha(0.35f);
}

// An address as a person reads it: without the scheme.
std::string plain_address(const std::string &url)
{
    const std::size_t at = url.find("://");
    return at == std::string::npos ? url : url.substr(at + 3);
}

std::string ago(std::uint64_t then, std::uint64_t now)
{
    if (then == 0 || now == 0 || now < then)
        return "Not known";
    const std::uint64_t seconds = now - then;
    char text[48];
    if (seconds < 90)
        return "Just now";
    if (seconds < 90 * 60)
        std::snprintf(text, sizeof(text), "%u minutes ago", static_cast<unsigned>(seconds / 60));
    else if (seconds < 36 * 3600)
        std::snprintf(text, sizeof(text), "%u hours ago", static_cast<unsigned>(seconds / 3600));
    else
        std::snprintf(text, sizeof(text), "%u days ago", static_cast<unsigned>(seconds / 86400));
    return text;
}

} // namespace

SourcesScreen::SourcesScreen(Shared &shared) : shared_(shared)
{
    const ui::Theme &theme = shared.theme;
    list_.style.theme = theme;
    list_.style.row_height = kRowHeight;
    list_.style.gap = kRowGap;
    list_.style.padding = 0.0f;
    list_.style.focus_shift = 0.0f;
    list_.style.scroll_thumb = false;
    list_.style.highlight.kind = ui::HighlightKind::ring;
    list_.style.highlight.radius = 24.0f;
    list_.style.highlight.breathe = false;
    std::vector<ui::ListItem> rows(3);
    for (int i = 0; i < 3; ++i)
        rows[static_cast<std::size_t>(i)].title = title_of(static_cast<iptv::SourceKind>(i));
    list_.set_items(std::move(rows));
    list_.set_bounds(kList);
    list_.set_focus(std::clamp(shared.model.view.focused_source, 0, 2), true);
    list_.content =
        [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &, int index, float focus)
    { draw_row(canvas, row, static_cast<iptv::SourceKind>(index), focus); };

    spinner_.style.theme = theme;
    spinner_.style.kind = ui::SpinnerKind::arc;
    spinner_.set_bounds({kInner, kPanel.y + kPanel.h - 84.0f, 44.0f, 44.0f});
    spinner_.set_spinning(false, true);
}

void SourcesScreen::enter()
{
    age_ = 0.0f;
    list_.enter();
}

iptv::SourceKind SourcesScreen::focused() const
{
    return static_cast<iptv::SourceKind>(std::clamp(list_.focus(), 0, 2));
}

void SourcesScreen::handle(const InputFrame &input, ui::Feedback &feedback)
{
    Model &model = shared_.model;
    const iptv::SourceKind source = focused();
    if (input.is_pressed(Action::north))
    {
        feedback.play(source == iptv::SourceKind::BuiltIn ? audio::Cue::error : audio::Cue::select);
        model.edit_source(source);
        return;
    }
    if (list_.handle(input, feedback) == ui::Event::activated)
        model.use_source(source);
}

void SourcesScreen::update(float dt)
{
    age_ += dt;
    const bool reduced = shared_.settings.reduced_motion;
    shared_.model.view.focused_source = list_.focus();
    list_.style.reduced_motion = reduced;
    spinner_.style.reduced_motion = reduced;
    spinner_.set_spinning(shared_.model.refreshing() && focused() == shared_.model.active_source());
    list_.update(dt);
    spinner_.update(dt);
}

void SourcesScreen::draw_row(ui::Canvas &canvas, const Rect &row, iptv::SourceKind source,
                             float focus) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = shared_.theme;
    const Model &model = shared_.model;
    const bool set_up = model.is_set_up(source);
    const bool active = source == model.active_source();

    list.rounded_rect(row, 24.0f,
                      gfx::mix(kWhite.with_alpha(0.06f), tone::panel_lit.with_alpha(0.95f), focus));
    list.bordered_rect(row, 24.0f, kClear, 1.5f, kWhite.with_alpha(0.12f + 0.12f * focus));

    // ---- what kind of source it is ----
    const Rect icon{row.x + 30.0f, row.cy() - 40.0f, 80.0f, 80.0f};
    if (set_up)
    {
        list.gradient_rect(icon, 22.0f, tone::ember, tone::wine);
        // A dish and its signal.
        list.arc(icon.cx(), icon.cy() + 16.0f, 30.0f, 4.5f, -0.9f, 1.8f, kWhite.with_alpha(0.55f));
        list.arc(icon.cx(), icon.cy() + 16.0f, 19.0f, 4.5f, -0.9f, 1.8f, kWhite.with_alpha(0.85f));
        list.circle(icon.cx(), icon.cy() + 16.0f, 5.5f, kWhite);
    }
    else
    {
        list.rounded_rect(icon, 22.0f, kWhite.with_alpha(0.08f));
        list.line(icon.cx() - 15.0f, icon.cy(), icon.cx() + 15.0f, icon.cy(), 4.5f,
                  theme.text_muted);
        list.line(icon.cx(), icon.cy() - 15.0f, icon.cx(), icon.cy() + 15.0f, 4.5f,
                  theme.text_muted);
    }

    // ---- how it is doing, at the right ----
    float right = row.x + row.w - 30.0f;
    const SourceHealth health = model.health(source);
    {
        const char *words = health_words(health);
        const float width = fonts.semibold.measure(words, 19.0f) + 54.0f;
        draw_status_chip(canvas, theme, right - width, row.cy() - (active ? 26.0f : 0.0f), words,
                         health_color(health));
        if (active)
        {
            ui::Painter paint(list, fonts, theme, canvas.glass);
            const float in_use = chip_width(paint, "In use");
            draw_chip(paint, right - in_use, row.cy() + 26.0f, "In use", 1.0f);
        }
        right -= std::max(width, 150.0f) + 26.0f;
    }

    // ---- its name and where it points ----
    const float x = icon.x + icon.w + 28.0f;
    const float room = right - x;
    ui::text(list, fonts.semibold, fonts.semibold.font->fit(title_of(source), 30.0f, room), x,
             row.cy() - 10.0f, 30.0f, theme.text);
    std::string line;
    switch (source)
    {
    case iptv::SourceKind::Custom:
        line = set_up ? plain_address(model.custom_url())
                      : "Add the address of an M3U or M3U8 playlist";
        break;
    case iptv::SourceKind::Xtream:
        line = set_up ? plain_address(model.xtream_server())
                      : "Add a server, a user name and a password";
        break;
    case iptv::SourceKind::OneStream:
        line = "Not available in this interface yet";
        break;
    case iptv::SourceKind::BuiltIn:
        line = plain_address(Model::builtin_url());
        break;
    }
    ui::text(list, fonts.regular, fonts.regular.font->fit(line, 22.0f, room), x, row.cy() + 28.0f,
             22.0f, theme.text_muted);
}

void SourcesScreen::draw_details(ui::Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    const ui::Fonts &fonts = canvas.fonts;
    const ui::Theme &theme = shared_.theme;
    const Model &model = shared_.model;
    const iptv::SourceKind source = focused();
    const bool set_up = model.is_set_up(source);
    const bool active = source == model.active_source();
    ui::Painter paint(list, fonts, theme, canvas.glass);

    draw_glass(canvas, theme, kPanel, 26.0f);
    float y = kPanel.y + 56.0f;
    ui::text(list, fonts.semibold, "SELECTED SOURCE", kInner, y, 16.0f, tone::accent,
             gfx::Align::left, 3.0f);
    y += 54.0f;
    paint.heading(fonts.display.font->fit(title_of(source), 38.0f, kInnerWidth), kInner, y, 38.0f);
    y += 34.0f;

    // ---- the facts ----
    struct Fact
    {
        const char *label;
        std::string value;
    };
    std::vector<Fact> facts;
    facts.push_back({"Status", health_words(model.health(source))});
    if (active && model.has_catalog())
    {
        facts.push_back({"Channels", group_digits(model.channel_count())});
        facts.push_back({"Saved", ago(model.saved_unix(), platform::unix_time())});
    }
    facts.push_back(
        {"Can be edited", source == iptv::SourceKind::BuiltIn ? "No, it is built in" : "Yes"});
    for (const Fact &fact : facts)
    {
        y += 50.0f;
        ui::text(list, fonts.regular, fact.label, kInner, y, 24.0f, theme.text_muted);
        ui::text(list, fonts.semibold, fonts.semibold.font->fit(fact.value, 24.0f, 380.0f),
                 kInner + kInnerWidth, y, 24.0f, theme.text, gfx::Align::right);
        list.rounded_rect({kInner, y + 16.0f, kInnerWidth, 1.5f}, 0.0f, kWhite.with_alpha(0.12f));
    }

    // ---- what is going on, or what Cross would do ----
    y += 62.0f;
    std::string headline;
    std::string body;
    if (active)
    {
        headline = model.source_title();
        body = model.source_detail();
    }
    else if (set_up)
    {
        headline = "Ready to use";
        body = "Press Cross to switch to this source. Its saved channels show at once and a new "
               "copy is downloaded.";
    }
    else if (source == iptv::SourceKind::Custom)
    {
        headline = "Bring your own playlist";
        body = "Press Cross and type the address of an M3U or M3U8 playlist. It is saved on this "
               "console.";
    }
    else
    {
        headline = "Sign in to a provider";
        body = "Press Cross and type the server address, the user name and the password of your "
               "Xtream Codes account.";
    }
    ui::text(list, fonts.semibold, fonts.semibold.font->fit(headline, 25.0f, kInnerWidth), kInner,
             y, 25.0f, theme.text);
    ui::paragraph(list, fonts.regular, body, kInner, y + 38.0f, 22.0f, kInnerWidth, 31.0f,
                  theme.text_muted, 4);

    // ---- the update, at the foot ----
    const float foot = kPanel.y + kPanel.h - 62.0f;
    if (active && model.refreshing())
    {
        spinner_.draw(canvas);
        ui::text(list, fonts.semibold, "Downloading the channel list", kInner + 60.0f,
                 baseline_for(foot, 24.0f), 24.0f, theme.text);
    }
    else if (active)
    {
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        ui::draw_button(list, fonts, glyphs, ui::Button::options, kInner, foot, 36.0f);
        ui::text(list, fonts.semibold, "Update now",
                 kInner + ui::button_width(ui::Button::options, 36.0f) + 14.0f,
                 baseline_for(foot, 24.0f), 24.0f, theme.text);
    }
}

void SourcesScreen::draw(ui::Canvas &canvas) const
{
    gfx::DrawList &list = canvas.list;
    const bool reduced = shared_.settings.reduced_motion;
    ui::Painter paint(list, canvas.fonts, shared_.theme, canvas.glass);

    list.push_opacity(tween::stagger(age_, 0, 0.06f, 0.45f));
    ui::text(list, canvas.fonts.semibold, "WHERE THE CHANNELS COME FROM", kMargin, 162.0f, 18.0f,
             tone::accent, gfx::Align::left, 4.0f);
    paint.heading("Sources", kMargin - 3.0f, 224.0f, 60.0f);
    list.pop_opacity();

    list_.draw(canvas);

    const float in = tween::stagger(age_, 2, 0.06f, 0.45f);
    list.push_opacity(in);
    list.push_transform(1.0f, 0.0f, 0.0f, reduced ? 0.0f : 24.0f * (1.0f - in), 0.0f);
    draw_details(canvas);
    list.pop_transform();
    list.pop_opacity();
}

int SourcesScreen::hints(ui::Hint *out, int capacity) const
{
    const Model &model = shared_.model;
    const iptv::SourceKind source = focused();
    int count = 0;
    const auto add = [&](ui::Hint hint)
    {
        if (count < capacity)
            out[count++] = hint;
    };
    if (!model.is_set_up(source))
        add({ui::Button::cross, "Set up"});
    else if (source != model.active_source())
        add({ui::Button::cross, "Use this source"});
    if (source != iptv::SourceKind::BuiltIn && model.is_set_up(source))
        add({ui::Button::triangle, "Edit"});
    return count;
}

} // namespace ptv
