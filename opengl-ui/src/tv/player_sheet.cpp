// ProsperoTV - The player's settings menu, drawn with the kit over the video.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/player_sheet.hpp"

#include "tv/draw.hpp"
#include "tv/i18n.hpp"
#include "tv/theme.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ptv
{

namespace
{

constexpr float kSheetWidth = 640.0f;
constexpr float kSheetMargin = 40.0f;
// The controls are hidden while the menu is open: the panel may use the whole height.
constexpr float kBottom = kHeight - kSheetMargin;
constexpr float kTop = kSheetMargin;
constexpr float kHintsHeight = 58.0f;
constexpr float kOptionHeight = 70.0f;
constexpr float kPreviewWidth = 72.0f;
constexpr float kPreviewHeight = 42.0f;

// Settings rows are told apart by their place: the form's ids are the rows' indices plus one.
int row_id(std::size_t index)
{
    return static_cast<int>(index) + 1;
}

// Over a moving picture the panel is nearly opaque; the rest is Dusk.
ui::Theme player_theme()
{
    ui::Theme theme = dusk();
    theme.surface = tone::panel.with_alpha(0.9f);
    return theme;
}

std::string delay_text(int milliseconds)
{
    char text[24];
    std::snprintf(text, sizeof(text), milliseconds ? "%+d ms" : "%d ms", milliseconds);
    return text;
}

void settle(ui::Sheet &sheet, ui::Form &form, ui::ListView &list)
{
    // Painted once per change: everything is drawn where it comes to rest.
    for (int step = 0; step < 90; ++step)
    {
        sheet.update(1.0f / 60.0f);
        form.update(1.0f / 60.0f);
        list.update(1.0f / 60.0f);
    }
}

} // namespace

std::string shape_name(unsigned milli)
{
    if (!milli)
        return {};
    static const struct
    {
        unsigned milli;
        const char *name;
    } known[] = {{1250, "5:4"},  {1333, "4:3"},    {1500, "3:2"},   {1667, "5:3"},
                 {1778, "16:9"}, {1850, "1.85:1"}, {2000, "2:1"},   {2200, "2.20:1"},
                 {2333, "21:9"}, {2390, "2.39:1"}, {2760, "2.76:1"}};
    for (const auto &shape : known)
        if (std::abs(static_cast<int>(milli) - static_cast<int>(shape.milli)) * 1000 <=
            static_cast<int>(shape.milli) * 15)
            return shape.name;
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%02u:1", milli / 1000, (milli % 1000 + 5) / 10 % 100);
    return text;
}

PlayerSheet::PlayerSheet(const ui::Fonts &fonts, const Settings &settings)
    : fonts_(fonts), settings_(settings), theme_(player_theme())
{
    const auto bind = [&](const ui::FontRef &font)
    {
        if (font.font && gfx::is_font_handle(font.texture))
            raster_.set_font(font.texture & 0xfu, font.font);
    };
    bind(fonts.regular);
    bind(fonts.semibold);
    bind(fonts.display);
    bind(fonts.mono);
    bind(fonts.pixel);
    bind(fonts.hand);

    sheet_.style.theme = theme_;
    sheet_.style.reduced_motion = true;
    sheet_.style.edge = interface_rtl() ? ui::SheetEdge::left : ui::SheetEdge::right;
    sheet_.style.size = kSheetWidth;
    sheet_.style.margin = kSheetMargin;
    sheet_.style.padding = 36.0f;
    sheet_.style.title_size = 34.0f;
    sheet_.style.scrim = 0.0f; // the picture stays as it is beside the panel
    sheet_.style.frosted = false;
    sheet_.style.handle = false;
    sheet_.content = [this](ui::Canvas &canvas, const Rect &area, float)
    {
        if (shown_.picker)
            options_.draw(canvas);
        else
            form_.draw(canvas);
        // What the buttons do, along the bottom of the panel.
        ui::Hint hints[3];
        int count = 0;
        if (shown_.picker)
        {
            hints[count++] = {ui::Button::cross,
                              shown_.confirm.empty() ? tr("Select") : shown_.confirm.c_str()};
            hints[count++] = {ui::Button::circle,
                              shown_.back.empty() ? tr("Back") : shown_.back.c_str()};
        }
        else
        {
            const bool stepper = shown_.selected < shown_.rows.size() &&
                                 shown_.rows[shown_.selected].kind == SheetRow::Kind::stepper;
            hints[count++] = stepper ? ui::Hint{ui::Button::dpad, tr("Change")}
                                     : ui::Hint{ui::Button::cross, tr("Open")};
            if (stepper)
                hints[count++] = {ui::Button::square, tr("Reset")};
            hints[count++] = {ui::Button::circle, tr("Close")};
        }
        ui::HintLayout layout;
        layout.size = 32.0f;
        layout.text_size = 21.0f;
        layout.cy = area.y + area.h - kHintsHeight * 0.5f + 6.0f;
        layout.item_gap = 30.0f;
        ui::draw_hints(canvas.list, canvas.fonts, ui::GlyphStyle::dark(), hints, count,
                       interface_rtl() ? area.x : area.x + area.w, !interface_rtl(), layout);
    };

    form_.style.theme = theme_;
    form_.style.reduced_motion = true;
    form_.style.on_page = false;
    form_.style.entrance_step = 0.0f;
    form_.style.row_height = 56.0f;
    form_.style.header_height = 40.0f;
    form_.style.label_ratio = 0.42f;
    form_.style.control_width = 300.0f;
    form_.style.stepper_width = 220.0f;
    form_.style.label_size = 25.0f;
    form_.style.value_size = 23.0f;
    form_.style.description_inline = false;

    options_.style.theme = theme_;
    options_.style.reduced_motion = true;
    options_.style.highlight.kind = ui::HighlightKind::bar;
    options_.style.entrance_step = 0.0f;
    options_.style.row_height = kOptionHeight - 6.0f;
    options_.style.title_size = 25.0f;
    options_.style.subtitle_size = 19.0f;
    options_.style.leading_width = 44.0f;
    options_.style.cards = true;
    // A tick before the value in use.
    options_.leading =
        [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item, int, float)
    {
        if (!item.tag)
            return;
        const float x = row.x + 26.0f;
        const float cy = row.cy();
        canvas.list.line(x - 8.0f, cy, x - 2.0f, cy + 7.0f, 3.5f, theme_.accent);
        canvas.list.line(x - 2.0f, cy + 7.0f, x + 10.0f, cy - 8.0f, 3.5f, theme_.accent);
    };
    // The picture's shape or size, or the colour, at the right.
    options_.trailing =
        [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &, int index, float focus)
    {
        if (index >= 0 && static_cast<std::size_t>(index) < shown_.rows.size())
            draw_preview(canvas, row, shown_.rows[index], focus);
    };
    // Provider labels need the same script fallback as the playback controls.
    options_.content = [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &item,
                              int index, float focus)
    {
        const auto &style = options_.style;
        const float left = row.x + style.padding;
        options_.leading(canvas, {left, row.y, style.leading_width, row.h}, item, index, focus);
        options_.trailing(canvas, row, item, index, focus);
        const float x = left + style.leading_width + 18.0f;
        const float room = std::max(row.x + row.w - style.padding - row.h - x, 0.0f);
        const Color ink = ui::Highlight::text_color(style, style.highlight, focus, theme_.text);
        const Color quiet = gfx::mix(theme_.text_muted, ink, focus * 0.6f);
        const auto text = [&](const std::string &value, const ui::FontRef &usual, float y,
                              float size, Color color)
        {
            const auto &face = face_for(fonts_, usual, value);
            ui::text(canvas.list, face, face.font->fit(readable(face, value), size, room), x, y,
                     size, color);
        };
        float y = row.cy() + style.title_size * 0.34f;
        if (!item.subtitle.empty())
            y = row.cy() - (style.title_size + style.subtitle_size * 1.25f) * 0.5f +
                style.title_size * 0.84f;
        text(item.title, fonts_.semibold, y, style.title_size, ink);
        if (!item.subtitle.empty())
            text(item.subtitle, fonts_.regular, y + style.subtitle_size * 1.3f, style.subtitle_size,
                 quiet);
    };
}

void PlayerSheet::draw_preview(ui::Canvas &canvas, const Rect &row, const SheetRow &item,
                               float focus) const
{
    if (item.preview == SheetRow::Preview::none)
        return;
    gfx::DrawList &list = canvas.list;
    const Color ink = gfx::mix(theme_.text_muted, theme_.text, focus);
    const Color tint = item.checked ? theme_.accent : ink;
    const float right = row.x + row.w - 24.0f;
    const float cy = row.cy();
    // The screen: 16:9, as wide as the preview column.
    const Rect screen{right - kPreviewWidth, cy - kPreviewWidth * 9.0f / 32.0f, kPreviewWidth,
                      kPreviewWidth * 9.0f / 16.0f};
    switch (item.preview)
    {
    case SheetRow::Preview::shape:
    {
        // The shape itself, as large as the column allows.
        const float ratio = std::max(static_cast<float>(item.milli) / 1000.0f, 0.5f);
        float width = kPreviewWidth;
        float height = width / ratio;
        if (height > kPreviewHeight)
        {
            height = kPreviewHeight;
            width = height * ratio;
        }
        const Rect shape{right - kPreviewWidth * 0.5f - width * 0.5f, cy - height * 0.5f, width,
                         height};
        list.bordered_rect(shape, 4.0f, tint.with_alpha(0.18f), 2.5f, tint);
        break;
    }
    case SheetRow::Preview::stretch:
    {
        list.bordered_rect(screen, 4.0f, tint.with_alpha(0.18f), 2.5f, tint);
        // Arrows out to both sides.
        const float y = screen.cy();
        const float left = screen.x + 10.0f;
        const float end = screen.x + screen.w - 10.0f;
        list.line(left, y, end, y, 2.5f, tint);
        list.line(left, y, left + 6.0f, y - 6.0f, 2.5f, tint);
        list.line(left, y, left + 6.0f, y + 6.0f, 2.5f, tint);
        list.line(end, y, end - 6.0f, y - 6.0f, 2.5f, tint);
        list.line(end, y, end - 6.0f, y + 6.0f, 2.5f, tint);
        break;
    }
    case SheetRow::Preview::zoom:
    {
        // A 4:3 picture on a 16:9 screen: fitted it has bars at the sides; zoomed it spills
        // past the screen (its outline shows how far) and that part is cut off.
        const Rect frame{right - kPreviewWidth * 0.5f - 28.0f, cy - 16.0f, 56.0f, 32.0f};
        float scale = static_cast<float>(item.milli) / 1000.0f;
        if (!item.milli) // as much as covers the screen
            scale = frame.w / (frame.h * 4.0f / 3.0f);
        const float height = frame.h * scale;
        const float width = height * 4.0f / 3.0f;
        const Rect picture{frame.cx() - width * 0.5f, frame.cy() - height * 0.5f, width, height};
        list.rounded_rect(frame, 3.0f, Color::rgb(0x000000, 0.55f));
        list.push_clip(frame);
        list.rounded_rect(picture, 2.0f, tint.with_alpha(0.6f));
        list.pop_clip();
        if (scale > 1.01f)
            list.bordered_rect(picture, 2.0f, kClear, 1.5f, tint.with_alpha(0.7f));
        list.bordered_rect(frame, 3.0f, kClear, 2.0f, ink);
        break;
    }
    case SheetRow::Preview::swatch:
    {
        const float x = right - kPreviewWidth * 0.5f;
        list.circle(x, cy, 13.0f, Color::rgb(item.rgb));
        list.ring(x, cy, 15.0f, 2.0f, tint);
        break;
    }
    default:
        break;
    }
}

void PlayerSheet::record(const SheetModel &model)
{
    shown_ = model;
    sheet_.set_title(model.title);

    // The rows first: the panel is as tall as they need.
    if (model.picker)
    {
        std::vector<ui::ListItem> items;
        for (const auto &row : model.rows)
        {
            ui::ListItem item;
            item.title = row.label;
            item.subtitle = row.value;
            item.tag = row.checked ? 1 : 0;
            items.push_back(std::move(item));
        }
        options_.set_items(std::move(items));
    }
    else
    {
        form_.clear();
        for (std::size_t index = 0; index < model.rows.size(); ++index)
        {
            const SheetRow &row = model.rows[index];
            switch (row.kind)
            {
            case SheetRow::Kind::header:
                form_.add_header(row.label);
                break;
            case SheetRow::Kind::stepper:
            {
                ui::FormRow &added = form_.add_stepper(row_id(index), row.label, row.current,
                                                       row.low, row.high, row.step);
                added.stepper.format = [](int value) { return delay_text(value); };
                break;
            }
            default:
            {
                ui::FormRow &added = form_.add_action(row_id(index), row.label);
                added.text = row.value;
                added.chevron = true;
                break;
            }
            }
        }
    }

    // The panel: at the side, as tall as its rows, resting on the bottom margin. Laid out in
    // all the room first, to see where the last row ends.
    const auto place = [&](float height)
    {
        sheet_.set_bounds({0.0f, kBottom - height, kWidth, height});
        if (!sheet_.is_open())
        {
            ui::Feedback quiet;
            sheet_.open(quiet);
        }
        Rect area = sheet_.content_rect();
        area.h -= kHintsHeight;
        if (model.picker)
        {
            options_.set_bounds(area);
            options_.set_focus(static_cast<int>(model.selected), true);
        }
        else
        {
            form_.set_bounds(area);
            form_.focus_row(row_id(model.selected), true);
        }
        settle(sheet_, form_, options_);
        return area;
    };
    const float room = kBottom - kTop;
    const Rect area = place(room);
    const Rect last = model.picker
                          ? options_.row_rect(options_.items().empty()
                                                  ? 0
                                                  : static_cast<int>(options_.items().size()) - 1)
                          : form_.row_rect(form_.row_count() - 1);
    const float used = last.y + last.h - area.y + 12.0f;
    if (used < area.h)
        place(room - (area.h - used));

    frame_.reset();
    ui::Canvas canvas{frame_.scene, fonts_, 0u, 0.0f};
    sheet_.draw(canvas);
    apply_appearance(frame_, settings_);
}

bool PlayerSheet::paint(const SheetModel &model, iptv::SubtitleCue &cue)
{
    cue.canvas_width = static_cast<unsigned>(kWidth);
    cue.canvas_height = static_cast<unsigned>(kHeight);
    cue.bitmaps.clear();
    if (model.rows.empty())
        return false;
    record(model);
    return rasterize(cue);
}

bool PlayerSheet::paint_controls(const ControlsModel &model, iptv::SubtitleCue &cue)
{
    cue.canvas_width = static_cast<unsigned>(kWidth);
    cue.canvas_height = static_cast<unsigned>(kHeight);
    cue.bitmaps.clear();
    constexpr float left = 80.0f, width = kWidth - 160.0f, pad = 36.0f;
    constexpr float title_size = 40.0f, line_size = 26.0f, detail_size = 24.0f;
    // Laid out top down from the panel's top, then moved to rest on the bottom margin.
    float height = pad + title_size * 1.25f;
    if (!model.line.empty())
        height += line_size * 1.6f;
    if (!model.detail.empty())
        height += detail_size * 1.6f;
    if (model.progress >= 0)
        height += 58.0f;
    if (!model.hints.empty())
        height += 62.0f;
    height += pad * 0.6f;
    const float top = kHeight - 40.0f - height;
    const Rect panel{left, top, width, height};

    frame_.reset();
    gfx::DrawList &list = frame_.scene;
    list.shadow(panel, theme_.radius_card, 28.0f, theme_.shadow.with_alpha(0.5f));
    list.bordered_rect(panel, theme_.radius_card, theme_.surface, 1.0f,
                       theme_.outline.with_alpha(0.35f));
    const float x = left + pad, right = left + width - pad;
    float y = top + pad + title_size * 0.8f;
    // The chip at the right of the title: what the picture is doing.
    float chip_left = right;
    if (!model.chip.empty())
    {
        const auto &face = face_for(fonts_, fonts_.semibold, model.chip);
        const std::string text = readable(face, model.chip);
        const float size = 23.0f;
        const float w = face.font->measure(text, size) + 36.0f;
        const Rect chip{right - w, y - title_size * 0.62f, w, 40.0f};
        list.rounded_rect(chip, 20.0f,
                          model.chip_lit ? theme_.accent : theme_.surface_high.with_alpha(0.9f));
        ui::text(list, face, text, chip.cx(), baseline_for(chip.cy(), size), size,
                 model.chip_lit ? tone::ink : theme_.text, gfx::Align::center);
        chip_left = chip.x - 24.0f;
    }
    {
        const auto &face = title_face(fonts_, model.title);
        const std::string text = readable(face, model.title);
        ui::text(list, face, face.font->fit(text, title_size, chip_left - x), x, y, title_size,
                 theme_.text);
    }
    const auto line = [&](const std::string &value, float size, Color color)
    {
        y += size * 1.6f;
        const auto &face = face_for(fonts_, fonts_.regular, value);
        const std::string text = readable(face, value);
        ui::text(list, face, face.font->fit(text, size, right - x), x, y, size, color);
    };
    if (!model.line.empty())
        line(model.line, line_size, theme_.text);
    if (!model.detail.empty())
        line(model.detail, detail_size, theme_.text_muted);
    if (model.progress >= 0)
    {
        // The timeline: elapsed at the left, the length at the right, the bar between.
        y += 58.0f;
        const float size = 24.0f;
        const float cy = y - size * 0.35f;
        const auto &face = fonts_.mono.font ? fonts_.mono : fonts_.regular;
        const float elapsed = face.font->measure(model.elapsed, size);
        const float total = face.font->measure(model.total, size);
        ui::text(list, face, model.elapsed, x, y, size, theme_.text);
        ui::text(list, face, model.total, right, y, size, theme_.text_muted, gfx::Align::right);
        const float bar_left = x + elapsed + 28.0f, bar_right = right - total - 28.0f;
        if (bar_right > bar_left)
        {
            const Rect track{bar_left, cy - 4.0f, bar_right - bar_left, 8.0f};
            list.rounded_rect(track, 4.0f, theme_.surface_high);
            const float at =
                bar_left + track.w * static_cast<float>(std::min(model.progress, 1000)) / 1000.0f;
            if (at > bar_left)
                list.rounded_rect({bar_left, track.y, at - bar_left, track.h}, 4.0f, theme_.accent);
            list.circle(at, cy, 12.0f, theme_.accent);
            list.circle(at, cy, 5.0f, tone::cream);
        }
    }
    if (!model.hints.empty())
    {
        std::vector<ui::Hint> hints;
        for (const auto &hint : model.hints)
            hints.push_back({hint.button, hint.label.c_str(), hint.second});
        ui::HintLayout layout;
        layout.size = 34.0f;
        layout.text_size = 23.0f;
        layout.cy = y + 52.0f;
        layout.item_gap = 34.0f;
        ui::draw_hints(list, fonts_, ui::GlyphStyle::dark(), hints.data(),
                       static_cast<int>(hints.size()), interface_rtl() ? right : x, interface_rtl(),
                       layout);
    }
    apply_appearance(frame_, settings_);
    return rasterize(cue);
}

bool PlayerSheet::paint_badge(const std::string &text, iptv::SubtitleCue &cue)
{
    cue.canvas_width = static_cast<unsigned>(kWidth);
    cue.canvas_height = static_cast<unsigned>(kHeight);
    cue.bitmaps.clear();
    if (text.empty())
        return false;
    frame_.reset();
    gfx::DrawList &list = frame_.scene;
    const auto &face = fonts_.display.font ? fonts_.display : fonts_.semibold;
    const float size = 32.0f;
    const float w = face.font->measure(text, size) + 48.0f;
    const float x = interface_rtl() ? 40.0f : kWidth - 40.0f - w;
    const Rect badge{x, 44.0f, w, 64.0f};
    list.shadow(badge, 32.0f, 20.0f, theme_.shadow.with_alpha(0.5f));
    list.rounded_rect(badge, 32.0f, theme_.surface);
    ui::text(list, face, text, badge.cx(), baseline_for(badge.cy(), size), size, theme_.accent,
             gfx::Align::center);
    apply_appearance(frame_, settings_);
    return rasterize(cue);
}

bool PlayerSheet::rasterize(iptv::SubtitleCue &cue)
{
    const int width = static_cast<int>(kWidth), height = static_cast<int>(kHeight);
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    pixels_.assign(stride * height, 0);
    SoftRaster::Target target;
    target.rgba = pixels_.data();
    target.width = width;
    target.height = height;
    target.stride = stride;
    const SoftRaster::Box box = raster_.draw(frame_.scene, target);
    if (box.x1 <= box.x0 || box.y1 <= box.y0)
        return false;
    // The raster is premultiplied; subtitle pictures carry straight alpha.
    iptv::SubtitleBitmap bitmap;
    bitmap.x = box.x0;
    bitmap.y = box.y0;
    bitmap.width = static_cast<unsigned>(box.x1 - box.x0);
    bitmap.height = static_cast<unsigned>(box.y1 - box.y0);
    bitmap.argb.reserve(static_cast<std::size_t>(bitmap.width) * bitmap.height);
    for (int y = box.y0; y < box.y1; ++y)
    {
        const std::uint8_t *pixel = pixels_.data() + y * stride + box.x0 * 4u;
        for (int x = box.x0; x < box.x1; ++x, pixel += 4)
        {
            const unsigned a = pixel[3];
            const auto straight = [a](unsigned c)
            { return a ? std::min(255u, (c * 255 + a / 2) / a) : 0u; };
            bitmap.argb.push_back(a << 24 | straight(pixel[0]) << 16 | straight(pixel[1]) << 8 |
                                  straight(pixel[2]));
        }
    }
    cue.bitmaps.push_back(std::move(bitmap));
    return true;
}

} // namespace ptv
