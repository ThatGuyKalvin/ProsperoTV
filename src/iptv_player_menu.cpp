/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_player_menu.h"

#include "iptv_picture.h"

#include <cstdio>
#include <cstring>

namespace iptv::player_menu
{

namespace
{

constexpr unsigned kMaxSettings = 5u;

const char *SettingLabel(Setting setting)
{
    switch (setting)
    {
    case Setting::audio:
        return "Audio";
    case Setting::subtitles:
        return "Subtitles";
    case Setting::audio_delay:
        return "Audio delay";
    case Setting::aspect:
        return "Aspect ratio";
    case Setting::zoom:
        return "Zoom";
    }
    return "";
}

const char *ZoomLabel(int zoom)
{
    switch (zoom)
    {
    case IPTV_ZOOM_115:
        return "Zoom 115%";
    case IPTV_ZOOM_133:
        return "Zoom 133%";
    case IPTV_ZOOM_FILL:
        return "Fill screen";
    default:
        return "Fit";
    }
}

std::uint32_t ZoomMilli(int zoom)
{
    switch (zoom)
    {
    case IPTV_ZOOM_115:
        return 1150u;
    case IPTV_ZOOM_133:
        return 1333u;
    case IPTV_ZOOM_FILL:
        return 0u; // as much as covers the screen
    default:
        return 1000u;
    }
}

const char *TrackLabel(const char (*labels)[48], unsigned listed, unsigned number,
                       const char *fallback)
{
    if (labels && number >= 1u && number <= listed && labels[number - 1u][0])
        return labels[number - 1u];
    return fallback;
}

} // namespace

void Menu::Open()
{
    open_ = true;
    picking_ = false;
    focus_ = Setting::aspect;
    option_ = 0;
}

void Menu::Close()
{
    open_ = false;
    picking_ = false;
}

unsigned Menu::Settings(const Info &info, Setting *out)
{
    unsigned count = 0;
    if (info.tracks && info.audio_tracks > 1u)
        out[count++] = Setting::audio;
    if (info.tracks && info.subtitle_tracks)
        out[count++] = Setting::subtitles;
    out[count++] = Setting::audio_delay;
    out[count++] = Setting::aspect;
    out[count++] = Setting::zoom;
    return count;
}

void Menu::Value(Setting setting, const Info &info, char *out, std::size_t capacity)
{
    switch (setting)
    {
    case Setting::audio:
        std::snprintf(out, capacity, "%s", info.audio_label);
        break;
    case Setting::subtitles:
        std::snprintf(out, capacity, "%s", info.subtitle_track ? info.subtitle_label : "Off");
        break;
    case Setting::audio_delay:
    {
        const long long delay = static_cast<long long>(info.audio_delay_us / 1000);
        if (delay)
            std::snprintf(out, capacity, "%+lld ms", delay);
        else
            std::snprintf(out, capacity, "0 ms");
        break;
    }
    case Setting::aspect:
    {
        char shape[24]{};
        if (info.aspect == IPTV_ASPECT_AUTO)
            iptv_picture_shape_name(info.frame_milli, shape, sizeof(shape));
        if (shape[0])
            std::snprintf(out, capacity, "Auto \xC2\xB7 %s", shape);
        else
            std::snprintf(out, capacity, "%s", iptv_picture_aspect_label(info.aspect));
        break;
    }
    case Setting::zoom:
        std::snprintf(out, capacity, "%s", ZoomLabel(info.zoom));
        break;
    }
}

unsigned Menu::Options(Setting setting, const Info &info, Option *out)
{
    unsigned count = 0;
    const auto add = [&](const char *label, const char *note, bool checked, std::uint32_t preview,
                         std::uint32_t milli)
    {
        if (count >= IPTV_OSD_MENU_ROWS)
            return;
        Option &option = out[count++];
        std::snprintf(option.label, sizeof(option.label), "%s", label);
        std::snprintf(option.note, sizeof(option.note), "%s", note);
        option.checked = checked;
        option.preview = preview;
        option.preview_milli = milli;
    };
    switch (setting)
    {
    case Setting::audio:
        for (unsigned number = 1; number <= info.audio_tracks && number <= info.listed; ++number)
        {
            char fallback[16];
            std::snprintf(fallback, sizeof(fallback), "Track %u", number);
            add(TrackLabel(info.audio_labels, info.listed, number, fallback), "",
                number == info.audio_track, IPTV_OSD_PREVIEW_NONE, 0);
        }
        break;
    case Setting::subtitles:
        add("Off", "", info.subtitle_track == 0, IPTV_OSD_PREVIEW_NONE, 0);
        for (unsigned number = 1; number <= info.subtitle_tracks && number <= info.listed; ++number)
        {
            char fallback[16];
            std::snprintf(fallback, sizeof(fallback), "Track %u", number);
            add(TrackLabel(info.subtitle_labels, info.listed, number, fallback), "",
                number == info.subtitle_track, IPTV_OSD_PREVIEW_NONE, 0);
        }
        break;
    case Setting::aspect:
        for (int aspect = 0; aspect < IPTV_ASPECT_COUNT; ++aspect)
        {
            char note[24]{};
            std::uint32_t milli = iptv_picture_aspect_milli(aspect);
            std::uint32_t preview = IPTV_OSD_PREVIEW_SHAPE;
            if (aspect == IPTV_ASPECT_AUTO)
            {
                // The picture's own shape, named when it is known.
                iptv_picture_shape_name(info.frame_milli, note, sizeof(note));
                milli = info.frame_milli ? info.frame_milli : 1778u;
            }
            else if (aspect == IPTV_ASPECT_STRETCH)
            {
                preview = IPTV_OSD_PREVIEW_STRETCH;
                std::snprintf(note, sizeof(note), "Fills the screen");
            }
            add(iptv_picture_aspect_label(aspect), note, aspect == info.aspect, preview, milli);
        }
        break;
    case Setting::zoom:
        for (int zoom = 0; zoom < IPTV_ZOOM_COUNT; ++zoom)
            add(ZoomLabel(zoom), zoom == IPTV_ZOOM_FILL ? "Crops the edges" : "", zoom == info.zoom,
                IPTV_OSD_PREVIEW_ZOOM, ZoomMilli(zoom));
        break;
    case Setting::audio_delay:
        break;
    }
    return count;
}

Change Menu::Input(iptv_input_action_t action, const Info &info)
{
    Change change;
    if (!open_)
        return change;
    Setting settings[kMaxSettings];
    const unsigned count = Settings(info, settings);
    unsigned focus = 0;
    bool found = false;
    for (unsigned index = 0; index < count; ++index)
        if (settings[index] == focus_)
        {
            focus = index;
            found = true;
        }
    if (!found)
    {
        // The setting went away (its tracks did): the menu goes back to its first page.
        focus_ = settings[0];
        picking_ = false;
    }

    if (picking_)
    {
        Option options[IPTV_OSD_MENU_ROWS];
        const unsigned total = Options(focus_, info, options);
        if (!total)
        {
            picking_ = false;
            return change;
        }
        if (option_ >= total)
            option_ = 0;
        switch (action)
        {
        case IPTV_INPUT_UP:
            option_ = (option_ + total - 1u) % total;
            break;
        case IPTV_INPUT_DOWN:
            option_ = (option_ + 1u) % total;
            break;
        case IPTV_INPUT_CIRCLE:
        case IPTV_INPUT_LEFT:
            picking_ = false;
            break;
        case IPTV_INPUT_CROSS:
            picking_ = false;
            switch (focus_)
            {
            case Setting::audio:
                change = {Change::Kind::audio, static_cast<int>(option_ + 1u)};
                break;
            case Setting::subtitles:
                change = {Change::Kind::subtitles, static_cast<int>(option_)};
                break;
            case Setting::aspect:
                change = {Change::Kind::aspect, static_cast<int>(option_)};
                break;
            case Setting::zoom:
                change = {Change::Kind::zoom, static_cast<int>(option_)};
                break;
            case Setting::audio_delay:
                break;
            }
            break;
        default:
            break;
        }
        return change;
    }

    switch (action)
    {
    case IPTV_INPUT_UP:
        focus_ = settings[(focus + count - 1u) % count];
        break;
    case IPTV_INPUT_DOWN:
        focus_ = settings[(focus + 1u) % count];
        break;
    case IPTV_INPUT_CIRCLE:
        Close();
        change.kind = Change::Kind::close;
        break;
    case IPTV_INPUT_LEFT:
    case IPTV_INPUT_RIGHT:
        if (focus_ == Setting::audio_delay)
        {
            change = {Change::Kind::audio_delay, action == IPTV_INPUT_LEFT ? -1 : 1};
            break;
        }
        if (action == IPTV_INPUT_LEFT)
            break;
        [[fallthrough]];
    case IPTV_INPUT_CROSS:
    {
        // A setting with values opens them, at the one in use.
        Option options[IPTV_OSD_MENU_ROWS];
        const unsigned total = Options(focus_, info, options);
        if (!total)
            break;
        picking_ = true;
        option_ = 0;
        for (unsigned index = 0; index < total; ++index)
            if (options[index].checked)
                option_ = index;
        break;
    }
    default:
        break;
    }
    return change;
}

void Menu::Fill(const Info &info, iptv_osd_state_t *state) const
{
    state->menu_rows = 0;
    state->menu_selected = 0;
    state->menu_page = IPTV_OSD_MENU_SETTINGS;
    state->menu_title[0] = '\0';
    std::memset(state->menu, 0, sizeof(state->menu));
    if (!open_)
        return;
    Setting settings[kMaxSettings];
    const unsigned count = Settings(info, settings);
    bool found = false;
    for (unsigned index = 0; index < count; ++index)
        found = found || settings[index] == focus_;
    const Setting focus = found ? focus_ : settings[0];

    if (picking_ && found)
    {
        Option options[IPTV_OSD_MENU_ROWS];
        const unsigned total = Options(focus, info, options);
        if (total)
        {
            state->menu_page = IPTV_OSD_MENU_PICKER;
            std::snprintf(state->menu_title, sizeof(state->menu_title), "%s", SettingLabel(focus));
            for (unsigned index = 0; index < total; ++index)
            {
                iptv_osd_menu_row_t &row = state->menu[index];
                row.kind = IPTV_OSD_ROW_OPTION;
                row.checked = options[index].checked ? 1u : 0u;
                row.preview = options[index].preview;
                row.preview_milli = options[index].preview_milli;
                std::snprintf(row.label, sizeof(row.label), "%s", options[index].label);
                std::snprintf(row.value, sizeof(row.value), "%s", options[index].note);
            }
            state->menu_rows = total;
            state->menu_selected = option_ < total ? option_ : 0u;
            return;
        }
    }

    // The settings, in two sections.
    std::snprintf(state->menu_title, sizeof(state->menu_title), "Playback");
    unsigned rows = 0;
    const auto header = [&](const char *label)
    {
        iptv_osd_menu_row_t &row = state->menu[rows++];
        row.kind = IPTV_OSD_ROW_HEADER;
        std::snprintf(row.label, sizeof(row.label), "%s", label);
    };
    for (unsigned index = 0; index < count && rows < IPTV_OSD_MENU_ROWS; ++index)
    {
        const Setting setting = settings[index];
        if (index == 0)
            header("Sound");
        if (setting == Setting::aspect)
            header("Picture");
        if (setting == focus)
            state->menu_selected = rows;
        iptv_osd_menu_row_t &row = state->menu[rows++];
        row.kind = setting == Setting::audio_delay ? IPTV_OSD_ROW_STEPPER : IPTV_OSD_ROW_PICKER;
        std::snprintf(row.label, sizeof(row.label), "%s", SettingLabel(setting));
        Value(setting, info, row.value, sizeof(row.value));
    }
    state->menu_rows = rows;
}

} // namespace iptv::player_menu
