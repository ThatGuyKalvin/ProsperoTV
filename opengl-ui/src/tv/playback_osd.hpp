// ProsperoTV - Channel controls and text over the foreground video.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tv/model.hpp"
#include "tv/player_sheet.hpp"
#include "tv/settings.hpp"
#include "ui/fonts.hpp"
#include "iptv_player.h"
#include <array>
#include <memory>
#include <mutex>

namespace ptv
{
// A bounded, opaque luma panel in the interface's 1920 x 1080 coordinates.
// The presenter supplies a copy of the decoder surface; reference frames are
// never modified. Native Main10 surfaces hold their ten bits in the low bits.
struct VideoPanel
{
    unsigned x = 0, y = 0, width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    void reset(unsigned left, unsigned top, unsigned w, unsigned h);
    void text(const hui::gfx::Font &font, std::string_view value, float left, float baseline,
              float size, float max_width, std::uint8_t ink = 235);
    bool composite(void *surface, std::size_t bytes, unsigned pitch, unsigned surface_height,
                   unsigned visible_width, unsigned visible_height, unsigned depth,
                   unsigned hdr = false, float scale_x = 1, float scale_y = 1) const;
};
bool composite_subtitle_bitmaps(const iptv::SubtitleCue &cue, void *surface, std::size_t bytes,
                                unsigned pitch, unsigned surface_height, unsigned visible_width,
                                unsigned visible_height, unsigned depth, unsigned hdr = false,
                                float scale_x = 1, float scale_y = 1);

struct PlaybackResume
{
    iptv_player_audio_state_t audio{};
    iptv::SubtitleState subtitles;
    iptv_player_media_state_t media{};
};

class PlaybackOsd
{
  public:
    PlaybackOsd(Model &model, const PlayRequest &request, const Settings &settings = {});
    void set_paused(bool paused);
    void set_quality_state(const iptv_player_quality_state_t &state);
    std::optional<iptv_hls_quality_t> quality_selection();
    bool auto_quality_selection();
    PlaybackResume resume_state();
    void restore_playback(const PlaybackResume &state);
    bool copy_picture_settings(Settings &settings);
    bool load_fonts(const std::string &directory);
    // The control thread alone calls input and reads selected_channel. Drawing
    // can happen on the presentation worker, protected by the same mutex.
    int input(int action, std::uint64_t now);
    std::optional<unsigned> take_picture_selection();
    void set_audio_state(const iptv_player_audio_state_t &state);
    std::optional<std::uint32_t> take_audio_selection();
    std::optional<int> take_audio_delay();
    void set_subtitle_state(const iptv::SubtitleState &state);
    void set_live_state(const iptv_player_live_state_t &state);
    void set_media_state(const iptv_player_media_state_t &state);
    std::optional<std::uint64_t> take_media_seek();
    std::optional<bool> take_media_pause();
    std::optional<unsigned> take_media_speed();
    std::optional<std::uint32_t> take_subtitle_selection();
    std::optional<unsigned> selected_channel() const
    {
        return selected_;
    }
    // What the controls show now.
    ControlsModel controls();
    // The controls, menu, channel list and text subtitles at the screen's resolution, into the
    // presenter's planes (iptv_native_agc_set_screen_osd): premultiplied colour and coverage.
    // Once this is called, draw() puts only the stream's own picture subtitles into the picture.
    // Returns bit 0 when anything is on the planes and bit 1 when they changed.
    int draw_screen(void *color, void *mask, std::size_t bytes, unsigned pitch,
                    unsigned surface_height, unsigned width, unsigned height, unsigned depth,
                    int hdr, bool cleared = false);
    bool draw(void *surface, std::size_t bytes, unsigned pitch, unsigned surface_height,
              unsigned visible_width, unsigned visible_height, unsigned depth, std::uint64_t now,
              const std::vector<std::shared_ptr<const iptv::SubtitleCue>> &subtitles = {},
              unsigned hdr = false, bool buffering = false);

  private:
    // The settings menu: a page of settings, each opening a list of its values.
    enum class Setting
    {
        audio,
        subtitles,
        delay,
        picture,
        subtitle_size,
        subtitle_color,
        chapters,
        quality,
    };
    int handle(int action, std::uint64_t now);
    void refresh_menu();
    std::vector<Setting> menu_settings() const;
    std::vector<SheetRow> menu_options(Setting setting) const;
    std::string menu_value(Setting setting) const;
    int choose(Setting setting, unsigned index, std::uint64_t now);
    SheetModel menu_model() const;
    SheetModel list_model() const;
    ControlsModel controls_model() const;
    void update_guide();
    void paint();
    void paint_subtitles();
    int select(unsigned index);
    void line(std::string_view value, float x, float y, float size, float width);
    Model &model_;
    Settings settings_;
    bool paused_ = false;
    bool buffering_ = false;
    std::uint64_t last_activity_ = 0;
    bool sleeping(std::uint64_t now) const;
    std::string current_id_, title_, now_, next_;
    bool live_ = false, list_ = false, dirty_ = true, started_ = false;
    bool audio_menu_ = false;
    iptv_player_quality_state_t quality_{};
    std::optional<iptv_hls_quality_t> quality_selection_;
    bool auto_quality_selection_ = false;
    std::optional<iptv_player_audio_state_t> restore_audio_;
    std::optional<iptv::SubtitleState> restore_subtitles_;
    std::optional<iptv_player_media_state_t> restore_media_;
    iptv_player_live_state_t history_{};
    iptv_player_media_state_t media_{};
    bool on_demand_ = false;
    std::uint64_t progress_saved_at_ = 0;
    std::optional<std::uint64_t> media_seek_;
    // Seeking reopens the video, so presses add up on a target first and it is asked for once,
    // a moment after the last; the timeline shows where it is going until the picture is there.
    std::optional<std::uint64_t> seek_target_, seek_shown_;
    std::uint64_t seek_commit_at_ = 0, seek_shown_until_ = 0;
    std::uint64_t seek_position() const;
    std::optional<bool> media_pause_;
    std::optional<unsigned> media_speed_;
    iptv_player_audio_state_t audio_{};
    std::optional<std::uint32_t> audio_selection_;
    std::optional<int> audio_delay_;
    iptv::SubtitleState subtitles_;
    unsigned picture_mode_ = 0;
    Setting setting_ = Setting::picture;
    bool picking_ = false;
    unsigned option_focus_ = 0;
    unsigned frame_milli_ = 0; // the picture's shape, as last drawn
    std::optional<unsigned> picture_selection_;
    std::optional<std::uint32_t> subtitle_selection_;
    std::vector<std::shared_ptr<const iptv::SubtitleCue>> subtitle_cues_;
    std::uint64_t banner_until_ = 0, guide_minute_ = 0;
    std::vector<unsigned> channels_;
    unsigned focus_ = 0;
    int current_position_ = -1;
    std::optional<unsigned> selected_;
    std::mutex mutex_;
    hui::gfx::Font regular_, semibold_, display_, east_asian_, korean_;
    hui::ui::Fonts fonts_;
    // The menu is painted on the control thread when it changes and drawn like a subtitle.
    std::unique_ptr<PlayerSheet> sheet_;
    SheetModel painted_;
    iptv::SubtitleCue menu_cue_;
    // The controls likewise, painted while the last frame showed them.
    bool controls_wanted_ = false;
    std::optional<ControlsModel> painted_controls_;
    iptv::SubtitleCue controls_cue_;
    std::string painted_badge_;
    iptv::SubtitleCue badge_cue_;
    // What the screen's planes show, and where (see draw_screen).
    bool screen_ = false, screen_sleeping_ = false;
    bool badge_wanted_ = false, overlay_wanted_ = false, captions_ = false;
    unsigned ui_version_ = 0;
    iptv::SubtitleCue panel_cue_; // the channel list, or the controls without the kit's fonts
    struct ScreenState
    {
        unsigned version = 0;
        bool panel = false, menu = false, controls = false, badge = false, captions = false;
        int captions_y = 0, hdr = -1;
        unsigned depth = 0;
        bool operator==(const ScreenState &) const = default;
    };
    ScreenState screen_state_;
    std::vector<std::array<int, 4>> screen_drawn_; // x0, y0, x1, y1 of what is on the planes
    VideoPanel panel_;
    VideoPanel speed_panel_;
    unsigned speed_panel_percent_ = 0;
    VideoPanel subtitle_panel_;
    iptv::SubtitleCue styled_subtitles_;
};
} // namespace ptv
