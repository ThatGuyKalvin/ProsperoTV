// ProsperoTV - The player's settings menu, drawn with the kit over the video.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/player_menu.hpp"

#include "tv/theme.hpp"
#include "ui/glyphs.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace ptv
{

namespace
{

constexpr float kSheetWidth = 640.0f;
constexpr float kSheetMargin = 40.0f;
// The panel ends above the player's controls, and never starts higher than this.
constexpr float kBottom = 790.0f;
constexpr float kTop = 40.0f;
constexpr float kHintsHeight = 58.0f;
constexpr float kOptionHeight = 70.0f;
constexpr float kPreviewWidth = 72.0f;
constexpr float kPreviewHeight = 42.0f;

// Settings rows are told apart by their place: the form's ids are the menu's indices plus one.
int row_id(std::uint32_t index)
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
    if (milliseconds)
        std::snprintf(text, sizeof(text), "%+d ms", milliseconds);
    else
        std::snprintf(text, sizeof(text), "0 ms");
    return text;
}

int parse_delay(const char *value)
{
    int milliseconds = 0;
    return std::sscanf(value, "%d", &milliseconds) == 1 ? milliseconds : 0;
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

PlayerMenu::PlayerMenu(const ui::Fonts &fonts) : fonts_(fonts), theme_(player_theme())
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
    sheet_.style.edge = ui::SheetEdge::right;
    sheet_.style.size = kSheetWidth;
    sheet_.style.margin = kSheetMargin;
    sheet_.style.padding = 36.0f;
    sheet_.style.title_size = 34.0f;
    sheet_.style.scrim = 0.0f; // the picture stays as it is beside the panel
    sheet_.style.frosted = false;
    sheet_.style.handle = false;
    sheet_.content = [this](ui::Canvas &canvas, const Rect &area, float)
    {
        if (shown_.menu_page == IPTV_OSD_MENU_PICKER)
            options_.draw(canvas);
        else
            form_.draw(canvas);
        // What the buttons do, along the bottom of the panel.
        ui::Hint hints[3];
        int count = 0;
        if (shown_.menu_page == IPTV_OSD_MENU_PICKER)
        {
            hints[count++] = {ui::Button::cross, "Select"};
            hints[count++] = {ui::Button::circle, "Back"};
        }
        else
        {
            const std::uint32_t selected = shown_.menu_selected;
            const bool stepper =
                selected < shown_.menu_rows && shown_.menu[selected].kind == IPTV_OSD_ROW_STEPPER;
            hints[count++] = stepper ? ui::Hint{ui::Button::dpad, "Change"}
                                     : ui::Hint{ui::Button::cross, "Open"};
            hints[count++] = {ui::Button::circle, "Close"};
        }
        ui::HintLayout layout;
        layout.size = 32.0f;
        layout.text_size = 21.0f;
        layout.cy = area.y + area.h - kHintsHeight * 0.5f + 6.0f;
        layout.item_gap = 30.0f;
        ui::draw_hints(canvas.list, canvas.fonts, ui::GlyphStyle::dark(), hints, count,
                       area.x + area.w, true, layout);
    };

    form_.style.theme = theme_;
    form_.style.reduced_motion = true;
    form_.style.on_page = false;
    form_.style.entrance_step = 0.0f;
    form_.style.row_height = 62.0f;
    form_.style.header_height = 46.0f;
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
    // The picture's shape or size at the right.
    options_.trailing =
        [this](ui::Canvas &canvas, const Rect &row, const ui::ListItem &, int index, float focus)
    {
        if (index >= 0 && static_cast<std::uint32_t>(index) < shown_.menu_rows)
            draw_preview(canvas, row, shown_.menu[index], focus);
    };
}

void PlayerMenu::draw_preview(ui::Canvas &canvas, const Rect &row, const iptv_osd_menu_row_t &item,
                              float focus) const
{
    if (item.preview == IPTV_OSD_PREVIEW_NONE)
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
    case IPTV_OSD_PREVIEW_SHAPE:
    {
        // The shape itself, as large as the column allows.
        const float ratio = std::max(static_cast<float>(item.preview_milli) / 1000.0f, 0.5f);
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
    case IPTV_OSD_PREVIEW_STRETCH:
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
    case IPTV_OSD_PREVIEW_ZOOM:
    {
        // A 4:3 picture on a 16:9 screen: at Fit it has bars at the sides; zoomed it spills
        // past the screen (its outline shows how far) and that part is cut off.
        const Rect frame{right - kPreviewWidth * 0.5f - 28.0f, cy - 16.0f, 56.0f, 32.0f};
        float scale = static_cast<float>(item.preview_milli) / 1000.0f;
        if (!item.preview_milli) // as much as covers the screen
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
    default:
        break;
    }
}

void PlayerMenu::record(const iptv_osd_state_t &state)
{
    shown_ = state;
    const std::uint32_t count = std::min<std::uint32_t>(state.menu_rows, IPTV_OSD_MENU_ROWS);
    const bool picking = state.menu_page == IPTV_OSD_MENU_PICKER;
    sheet_.set_title(state.menu_title[0] ? state.menu_title : "Playback");

    // The rows first: the panel is as tall as they need.
    if (picking)
    {
        std::vector<ui::ListItem> items;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const iptv_osd_menu_row_t &row = state.menu[index];
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
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const iptv_osd_menu_row_t &row = state.menu[index];
            switch (row.kind)
            {
            case IPTV_OSD_ROW_HEADER:
                form_.add_header(row.label);
                continue;
            case IPTV_OSD_ROW_STEPPER:
            {
                ui::FormRow &added = form_.add_stepper(row_id(index), row.label,
                                                       parse_delay(row.value), -3000, 3000, 50);
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

    // The panel: right, above the controls, as tall as its rows. Laid out in all the room
    // first, to see where the last row ends.
    const auto place = [&](float height)
    {
        sheet_.set_bounds({kImageLeft, kBottom - height, kWidth - kImageLeft, height});
        if (!sheet_.is_open())
        {
            ui::Feedback quiet;
            sheet_.open(quiet);
        }
        Rect area = sheet_.content_rect();
        area.h -= kHintsHeight;
        if (picking)
        {
            options_.set_bounds(area);
            options_.set_focus(static_cast<int>(state.menu_selected), true);
        }
        else
        {
            form_.set_bounds(area);
            form_.focus_row(row_id(state.menu_selected), true);
        }
        settle(sheet_, form_, options_);
        return area;
    };
    const float room = kBottom - kTop;
    const Rect area = place(room);
    const Rect last =
        picking ? options_.row_rect(
                      options_.items().empty() ? 0 : static_cast<int>(options_.items().size()) - 1)
                : form_.row_rect(form_.row_count() - 1);
    const float used = last.y + last.h - area.y + 12.0f;
    if (used < area.h)
        place(room - (area.h - used));

    list_.clear();
    ui::Canvas canvas{list_, fonts_, 0u, 0.0f};
    sheet_.draw(canvas);
}

bool PlayerMenu::paint(const iptv_osd_state_t &state, iptv_osd_image_t *image)
{
    if (!image || !image->rgba || !state.menu_rows)
        return false;
    record(state);
    // Clear what the last paint drew, then draw.
    const std::size_t stride = static_cast<std::size_t>(IPTV_OSD_MENU_IMAGE_WIDTH) * 4u;
    std::memset(image->rgba, 0, stride * IPTV_OSD_OVERLAY_HEIGHT);
    SoftRaster::Target target;
    target.rgba = image->rgba;
    target.width = static_cast<int>(IPTV_OSD_MENU_IMAGE_WIDTH);
    target.height = static_cast<int>(IPTV_OSD_OVERLAY_HEIGHT);
    target.stride = stride;
    target.origin_x = static_cast<int>(kImageLeft);
    target.origin_y = 0;
    const SoftRaster::Box box = raster_.draw(list_, target);
    if (box.x1 <= box.x0 || box.y1 <= box.y0)
        return false;
    image->x = static_cast<std::uint32_t>(box.x0 - target.origin_x);
    image->y = static_cast<std::uint32_t>(box.y0 - target.origin_y);
    image->width = static_cast<std::uint32_t>(box.x1 - box.x0);
    image->height = static_cast<std::uint32_t>(box.y1 - box.y0);
    return true;
}

} // namespace ptv
