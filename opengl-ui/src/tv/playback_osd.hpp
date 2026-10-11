// ProsperoTV - Channel controls and text over the foreground video.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "tv/model.hpp"
#include "tv/settings.hpp"
#include "ui/fonts.hpp"
#include "iptv_player.h"
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
    bool draw(void *surface, std::size_t bytes, unsigned pitch, unsigned surface_height,
              unsigned visible_width, unsigned visible_height, unsigned depth, std::uint64_t now,
              const std::vector<std::shared_ptr<const iptv::SubtitleCue>> &subtitles = {},
              unsigned hdr = false);

  private:
    void update_guide();
    void paint();
    void paint_subtitles();
    int select(unsigned index);
    void line(std::string_view value, float x, float y, float size, float width);
    Model &model_;
    Settings settings_;
    bool paused_ = false;
    std::uint64_t last_activity_ = 0;
    bool sleeping(std::uint64_t now) const;
    std::string current_id_, title_, now_, next_;
    bool live_ = false, list_ = false, dirty_ = true, started_ = false;
    bool audio_menu_ = false;
    iptv_player_quality_state_t quality_{};
    unsigned quality_focus_ = 0;
    std::optional<iptv_hls_quality_t> quality_selection_;
    std::optional<iptv_player_audio_state_t> restore_audio_;
    std::optional<iptv::SubtitleState> restore_subtitles_;
    std::optional<iptv_player_media_state_t> restore_media_;
    iptv_player_live_state_t history_{};
    iptv_player_media_state_t media_{};
    bool on_demand_ = false;
    unsigned chapter_focus_ = 0;
    std::uint64_t progress_saved_at_ = 0;
    std::optional<std::uint64_t> media_seek_;
    std::optional<bool> media_pause_;
    std::optional<unsigned> media_speed_;
    unsigned audio_focus_ = 0;
    iptv_player_audio_state_t audio_{};
    std::optional<std::uint32_t> audio_selection_;
    std::optional<int> audio_delay_;
    iptv::SubtitleState subtitles_;
    unsigned tab_ = 0, picture_focus_ = 0, picture_mode_ = 0;
    std::optional<unsigned> picture_selection_;
    unsigned subtitle_focus_ = 0;
    std::optional<std::uint32_t> subtitle_selection_;
    std::vector<std::shared_ptr<const iptv::SubtitleCue>> subtitle_cues_;
    std::uint64_t banner_until_ = 0, guide_minute_ = 0;
    std::vector<unsigned> channels_;
    unsigned focus_ = 0;
    int current_position_ = -1;
    std::optional<unsigned> selected_;
    std::mutex mutex_;
    hui::gfx::Font regular_, east_asian_, korean_;
    hui::ui::Fonts fonts_;
    VideoPanel panel_;
    VideoPanel subtitle_panel_;
    iptv::SubtitleCue styled_subtitles_;
};
} // namespace ptv
