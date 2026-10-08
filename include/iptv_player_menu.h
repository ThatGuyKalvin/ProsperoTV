/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_PLAYER_MENU_H
#define IPTV_PLAYER_MENU_H

#include "iptv_input.h"
#include "iptv_osd.h"

#include <cstddef>
#include <cstdint>

namespace iptv::player_menu
{

// What the settings menu shows: the playback's tracks and the picture and sound settings.
struct Info
{
    bool tracks = false; // a film or an episode that reports its tracks
    unsigned audio_track = 0;
    unsigned audio_tracks = 0;
    const char *audio_label = "";
    const char (*audio_labels)[48] = nullptr; // the first `listed` tracks' names
    unsigned subtitle_track = 0;              // 0: off
    unsigned subtitle_tracks = 0;
    const char *subtitle_label = "";
    const char (*subtitle_labels)[48] = nullptr;
    unsigned listed = 0;
    int aspect = 0;
    int zoom = 0;
    std::int64_t audio_delay_us = 0;
    std::uint32_t frame_milli = 0; // the picture's own shape, 0 when unknown
};

enum class Setting : std::uint8_t
{
    audio,
    subtitles,
    audio_delay,
    aspect,
    zoom,
};

// What an input asks the player to do.
struct Change
{
    enum class Kind : std::uint8_t
    {
        none,
        close,       // the menu closed
        audio,       // play track `value` (1-based)
        subtitles,   // show track `value` (0: off)
        aspect,      // IPTV_ASPECT_*
        zoom,        // IPTV_ZOOM_*
        audio_delay, // one step later (`value` 1) or earlier (-1)
    };
    Kind kind = Kind::none;
    int value = 0;
};

// The menu's pages and focus. The player owns the settings; the menu turns presses into
// changes and describes itself for the screen.
class Menu
{
  public:
    bool is_open() const
    {
        return open_;
    }
    void Open();
    void Close();
    // Up and Down choose, Left and Right change a value in place, Cross opens a setting's values
    // or picks one, Circle goes back a page or closes.
    Change Input(iptv_input_action_t action, const Info &info);
    void Fill(const Info &info, iptv_osd_state_t *state) const;

    // For tests.
    bool picking() const
    {
        return picking_;
    }
    Setting focus() const
    {
        return focus_;
    }

  private:
    struct Option
    {
        char label[32];
        char note[48];
        bool checked;
        std::uint32_t preview;
        std::uint32_t preview_milli;
    };
    static unsigned Settings(const Info &info, Setting *out);
    static unsigned Options(Setting setting, const Info &info, Option *out);
    static void Value(Setting setting, const Info &info, char *out, std::size_t capacity);

    bool open_ = false;
    bool picking_ = false;
    Setting focus_ = Setting::aspect;
    unsigned option_ = 0;
};

} // namespace iptv::player_menu

#endif
