// ProsperoTV - Channel controls and text over the foreground video.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include "tv/playback_osd.hpp"
#include "tv/theme.hpp"
#include "tv/draw.hpp"
#include "tv/platform.hpp"
#include "core/save_file.hpp"
#include "iptv_input.h"
#include "iptv_color.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ptv
{
void VideoPanel::reset(unsigned left, unsigned top, unsigned w, unsigned h)
{
    x = interface_rtl() && left <= 1920 && w <= 1920 - left ? 1920 - left - w : left;
    y = top;
    width = w;
    height = h;
    if (x > 1920 || y > 1080 || w > 1920 - x || h > 1080 - y)
        width = height = 0;
    pixels.assign(static_cast<std::size_t>(width) * height, 28);
}

void VideoPanel::text(const hui::gfx::Font &font, std::string_view value, float left,
                      float baseline, float size, float max_width, std::uint8_t ink)
{
    if (font.atlas().empty() || size <= 0 || max_width <= 0)
        return;
    std::vector<hui::gfx::GlyphQuad> quads;
    font.layout(font.fit(value, size, max_width), interface_rtl() ? width - left : left, baseline,
                size, interface_rtl() ? hui::gfx::Align::right : hui::gfx::Align::left, quads);
    const int aw = font.atlas_width(), ah = font.atlas_height();
    const auto sample = [&](int sx, int sy)
    {
        return static_cast<float>(
                   font.atlas()[std::clamp(sy, 0, ah - 1) * aw + std::clamp(sx, 0, aw - 1)]) /
               255.0f;
    };
    for (const auto &q : quads)
    {
        if (q.x1 <= q.x0 || q.y1 <= q.y0)
            continue;
        const int x0 = std::max(0, static_cast<int>(std::floor(q.x0)));
        const int y0 = std::max(0, static_cast<int>(std::floor(q.y0)));
        const int x1 = std::min(static_cast<int>(width), static_cast<int>(std::ceil(q.x1)));
        const int y1 = std::min(static_cast<int>(height), static_cast<int>(std::ceil(q.y1)));
        for (int py = y0; py < y1; ++py)
            for (int px = x0; px < x1; ++px)
            {
                const float u =
                    (q.u0 + (q.u1 - q.u0) * (px + 0.5f - q.x0) / (q.x1 - q.x0)) * aw - 0.5f;
                const float v =
                    (q.v0 + (q.v1 - q.v0) * (py + 0.5f - q.y0) / (q.y1 - q.y0)) * ah - 0.5f;
                const int sx = static_cast<int>(std::floor(u)),
                          sy = static_cast<int>(std::floor(v));
                const float fx = u - sx, fy = v - sy;
                const float sdf =
                    std::lerp(std::lerp(sample(sx, sy), sample(sx + 1, sy), fx),
                              std::lerp(sample(sx, sy + 1), sample(sx + 1, sy + 1), fx), fy);
                const float alpha =
                    std::clamp((sdf - 0.5f) * 2.0f * font.sdf_range(size) + 0.5f, 0.0f, 1.0f);
                auto &pixel = pixels[static_cast<std::size_t>(py) * width + px];
                pixel = static_cast<std::uint8_t>(
                    std::lerp(static_cast<float>(pixel), static_cast<float>(ink), alpha));
            }
    }
}

bool VideoPanel::composite(void *surface, std::size_t bytes, unsigned pitch, unsigned sh,
                           unsigned vw, unsigned vh, unsigned depth, unsigned hdr, float scale_x,
                           float scale_y) const
{
    if (!std::isfinite(scale_x) || !std::isfinite(scale_y) || scale_x < 1 || scale_y < 1 ||
        scale_x > 8192 || scale_y > 8192 || !surface || (depth != 8 && depth != 10) ||
        (hdr && depth != 10) || !vw || !vh || vw > pitch || vh > sh || pitch > 8192 || sh > 8192 ||
        (pitch & 1u) || !width || !height || x > 1920 || y > 1080 || width > 1920 - x ||
        height > 1080 - y || pixels.size() != static_cast<std::size_t>(width) * height)
        return false;
    const unsigned component = depth == 10 ? 2 : 1;
    const std::size_t y_size = static_cast<std::size_t>(pitch) * sh;
    if ((y_size + static_cast<std::size_t>(pitch) * ((sh + 1) / 2)) * component > bytes)
        return false;
    const auto map_x = [&](unsigned value)
    {
        return scale_x == 1 ? value * vw / 1920
                            : unsigned(std::lround((.5 + (value / 1920.0 - .5) / scale_x) * vw));
    };
    const auto map_y = [&](unsigned value)
    {
        return scale_y == 1 ? value * vh / 1080
                            : unsigned(std::lround((.5 + (value / 1080.0 - .5) / scale_y) * vh));
    };
    const unsigned left = map_x(x), top = map_y(y);
    const unsigned right = map_x(x + width), bottom = map_y(y + height);
    if (left == right || top == bottom)
        return false;
    auto *out = static_cast<std::uint8_t *>(surface);
    const auto put = [&](std::size_t at, std::uint8_t value, bool luma)
    {
        if (component == 1)
            out[at] = value;
        else
        {
            const std::uint16_t word = hdr && luma ? iptv_color_ui_luma(value, hdr == 2)
                                                   : static_cast<std::uint16_t>(value) << 2;
            std::memcpy(out + at * 2, &word, sizeof(word));
        }
    };
    for (unsigned py = top; py < bottom; ++py)
    {
        const auto row = static_cast<std::size_t>((py - top) * height / (bottom - top)) * width;
        for (unsigned px = left; px < right; ++px)
            put(static_cast<std::size_t>(py) * pitch + px,
                pixels[row + (px - left) * width / (right - left)], true);
    }
    // Neutral chroma for the panel, preserving all other video and row padding.
    for (unsigned py = top / 2; py < (bottom + 1) / 2; ++py)
        for (unsigned px = left & ~1u; px < ((right + 1) & ~1u); ++px)
            put(y_size + static_cast<std::size_t>(py) * pitch + px, 128, false);
    return true;
}

PlaybackOsd::PlaybackOsd(Model &model, const PlayRequest &request, const Settings &settings)
    : model_(model), settings_(settings), current_id_(request.channel_id),
      title_(request.channel_name), live_(request.record_channel_result),
      on_demand_(request.on_demand)
{
    picture_focus_ = picture_mode_ = std::clamp(settings_.picture_mode, 0, 6);
    if (live_)
    {
        for (unsigned i = 0; i < model.visible_count(); ++i)
        {
            const auto index = model.visible(i);
            if (model.channel(index).id == current_id_)
            {
                focus_ = i;
                current_position_ = static_cast<int>(i);
            }
            channels_.push_back(index);
        }
    }
    update_guide();
}

bool PlaybackOsd::load_fonts(const std::string &directory)
{
    const auto load = [&](const char *name, hui::gfx::Font &font, hui::ui::FontRef &ref)
    {
        std::string data;
        if (!hui::save::read_file(directory + "/" + name, &data, 64u << 20) ||
            !load_menu_font(std::move(data), font, settings_.larger_text))
            return false;
        ref.font = &font;
        return true;
    };
    if (!load("inter-regular.huifont", regular_, fonts_.regular))
        return false;
    fonts_.semibold = fonts_.regular;
    // Subtitle language may differ from every channel name in the catalogue.
    (void)load("noto-sans-east-asian.huifont", east_asian_, fonts_.hand);
    (void)load("noto-sans-korean.huifont", korean_, fonts_.pixel);
    return true;
}

void PlaybackOsd::update_guide()
{
    const auto time = static_cast<std::int64_t>(platform::unix_time());
    guide_minute_ = time / 60;
    now_ = live_ ? tr("Programme guide unavailable") : "";
    next_.clear();
    if (live_)
        if (const auto channel = model_.find(current_id_))
        {
            title_ = display_name(*channel);
            if (const auto programme = model_.guide().now(channel->id, time))
                now_ = tr("Now  ") + programme_time(programme->start) + "  " + programme->title;
            if (const auto programme = model_.guide().next(channel->id, time))
                next_ = tr("Next  ") + programme_time(programme->start) + "  " + programme->title;
        }
    dirty_ = true;
}

int PlaybackOsd::select(unsigned index)
{
    list_ = false;
    dirty_ = true;
    if (model_.channel(index).id == current_id_)
        return 1;
    selected_ = index;
    return 2;
}

void PlaybackOsd::set_quality_state(const iptv_player_quality_state_t &state)
{
    std::lock_guard lock(mutex_);
    if (std::memcmp(&quality_, &state, sizeof(state)) != 0)
    {
        quality_ = state;
        quality_.count = std::min(quality_.count, unsigned(IPTV_HLS_MAX_VARIANTS));
        quality_focus_ = std::min(quality_focus_, quality_.count + 1);
        dirty_ = true;
    }
}
std::optional<iptv_hls_quality_t> PlaybackOsd::quality_selection()
{
    std::lock_guard lock(mutex_);
    return quality_selection_;
}
bool PlaybackOsd::auto_quality_selection()
{
    std::lock_guard lock(mutex_);
    return auto_quality_selection_;
}
PlaybackResume PlaybackOsd::resume_state()
{
    std::lock_guard lock(mutex_);
    return {audio_, subtitles_, media_};
}
void PlaybackOsd::restore_playback(const PlaybackResume &state)
{
    std::lock_guard lock(mutex_);
    if (state.audio.count || state.audio.disabled)
        restore_audio_ = state.audio;
    restore_subtitles_ = state.subtitles;
    restore_media_ = state.media;
    audio_delay_ = state.audio.delay_ms;
}

void PlaybackOsd::set_audio_state(const iptv_player_audio_state_t &state)
{
    std::lock_guard lock(mutex_);
    bool changed = audio_.count != state.count || audio_.selected_pid != state.selected_pid ||
                   audio_.disabled != state.disabled || audio_.pending != state.pending ||
                   audio_.result != state.result || audio_.delay_ms != state.delay_ms;
    const auto count = std::min(state.count, IPTV_STREAM_MAX_AUDIO_TRACKS);
    if (restore_audio_ && count)
    {
        const auto previous = *restore_audio_;
        audio_selection_ = previous.disabled ? std::optional<std::uint32_t>(0) : std::nullopt;
        for (unsigned old = 0;
             !previous.disabled && old < std::min(previous.count, IPTV_STREAM_MAX_AUDIO_TRACKS);
             ++old)
        {
            if (previous.tracks[old].pid != previous.selected_pid)
                continue;
            for (unsigned at = 0; at < count; ++at)
            {
                const bool metadata = previous.languages[old][0] || previous.titles[old][0] ||
                                      previous.tracks[old].language[0];
                if (previous.tracks[old].audio_type == state.tracks[at].audio_type &&
                    std::strncmp(previous.languages[old], state.languages[at], 32) == 0 &&
                    std::strncmp(previous.titles[old], state.titles[at], 128) == 0 &&
                    std::strncmp(previous.tracks[old].language, state.tracks[at].language, 3) ==
                        0 &&
                    (metadata || previous.tracks[old].pid == state.tracks[at].pid))
                {
                    audio_selection_ = state.tracks[at].pid;
                    break;
                }
            }
        }
        if (!previous.disabled && !audio_selection_)
        {
            now_ = tr("Previous audio track unavailable at this quality");
            banner_until_ = platform::monotonic_us() + 8000000;
        }
        restore_audio_.reset();
    }
    for (unsigned i = 0; i < count && !changed; ++i)
        changed = audio_.tracks[i].pid != state.tracks[i].pid ||
                  audio_.tracks[i].stream_type != state.tracks[i].stream_type ||
                  audio_.tracks[i].audio_type != state.tracks[i].audio_type ||
                  std::strncmp(audio_.titles[i], state.titles[i], 128) != 0 ||
                  std::strncmp(audio_.languages[i], state.languages[i], 32) != 0 ||
                  std::strncmp(audio_.tracks[i].language, state.tracks[i].language, 3) != 0;
    if (changed)
    {
        audio_ = state;
        audio_.count = count;
        audio_focus_ = std::min(audio_focus_, count);
        dirty_ = true;
    }
}

std::optional<int> PlaybackOsd::take_audio_delay()
{
    std::lock_guard lock(mutex_);
    const auto result = audio_delay_;
    audio_delay_.reset();
    return result;
}

std::optional<std::uint32_t> PlaybackOsd::take_audio_selection()
{
    std::lock_guard lock(mutex_);
    const auto result = audio_selection_;
    audio_selection_.reset();
    return result;
}

void PlaybackOsd::set_subtitle_state(const iptv::SubtitleState &state)
{
    std::lock_guard lock(mutex_);
    if (restore_subtitles_ && (!restore_subtitles_->selected || !state.tracks.empty()))
    {
        subtitle_selection_ = 0;
        for (const auto &previous : restore_subtitles_->tracks)
            if (previous.id == restore_subtitles_->selected)
                for (const auto &track : state.tracks)
                    if (previous.language == track.language && previous.title == track.title &&
                        previous.forced == track.forced &&
                        previous.hearing_impaired == track.hearing_impaired &&
                        ((!previous.language.empty() || !previous.title.empty()) ||
                         previous.id == track.id))
                    {
                        subtitle_selection_ = track.id;
                        break;
                    }
        if (restore_subtitles_->selected && !*subtitle_selection_)
        {
            now_ = tr("Previous subtitle track unavailable at this quality");
            banner_until_ = platform::monotonic_us() + 8000000;
        }
        restore_subtitles_.reset();
    }
    if (subtitles_.tracks != state.tracks || subtitles_.selected != state.selected ||
        subtitles_.error != state.error)
    {
        subtitles_ = state;
        if (subtitles_.tracks.size() > iptv::Subtitles::max_tracks)
            subtitles_.tracks.resize(iptv::Subtitles::max_tracks);
        subtitle_focus_ =
            std::min(subtitle_focus_, static_cast<unsigned>(subtitles_.tracks.size()));
        dirty_ = true;
    }
}
std::optional<std::uint32_t> PlaybackOsd::take_subtitle_selection()
{
    std::lock_guard lock(mutex_);
    const auto result = subtitle_selection_;
    subtitle_selection_.reset();
    return result;
}

void PlaybackOsd::set_live_state(const iptv_player_live_state_t &state)
{
    std::lock_guard lock(mutex_);
    const auto behind = [](const iptv_player_live_state_t &value)
    {
        return value.last_us > value.position_us ? (value.last_us - value.position_us) / 1000000
                                                 : 0;
    };
    if (state.available != history_.available || state.paused != history_.paused ||
        state.expired != history_.expired || behind(state) != behind(history_))
        dirty_ = true;
    if (live_ && paused_ != (state.paused != 0))
    {
        paused_ = state.paused != 0;
        last_activity_ = 0;
    }
    history_ = state;
}

void PlaybackOsd::set_paused(bool paused)
{
    std::lock_guard lock(mutex_);
    if (paused_ != paused)
    {
        paused_ = paused;
        last_activity_ = 0;
    }
}

bool PlaybackOsd::sleeping(std::uint64_t now) const
{
    return paused_ && settings_.screensaver_minutes > 0 && last_activity_ &&
           now >= last_activity_ &&
           now - last_activity_ >=
               static_cast<std::uint64_t>(settings_.screensaver_minutes) * 60000000;
}

void PlaybackOsd::set_media_state(const iptv_player_media_state_t &state)
{
    std::lock_guard lock(mutex_);
    if (restore_media_ && state.available)
    {
        media_pause_ = restore_media_->paused != 0;
        restore_media_.reset();
    }
    if (media_.available != state.available || media_.paused != state.paused ||
        media_.seekable != state.seekable || media_.speed_percent != state.speed_percent ||
        media_.position_us / 1000000 != state.position_us / 1000000 ||
        media_.duration_us != state.duration_us || media_.chapter_count != state.chapter_count)
        dirty_ = true;
    media_ = state;
    media_.chapter_count = std::min(media_.chapter_count, unsigned(IPTV_PLAYER_MAX_CHAPTERS));
    chapter_focus_ = std::min(chapter_focus_, media_.chapter_count ? media_.chapter_count - 1 : 0);
    const auto now = platform::monotonic_us();
    if (on_demand_ && state.presented && state.position_us && now - progress_saved_at_ >= 15000000)
    {
        if (model_.save_vod_progress(current_id_, state.position_us, state.duration_us))
            progress_saved_at_ = now;
    }
}
std::optional<std::uint64_t> PlaybackOsd::take_media_seek()
{
    std::lock_guard lock(mutex_);
    const auto result = media_seek_;
    media_seek_.reset();
    return result;
}
std::optional<unsigned> PlaybackOsd::take_media_speed()
{
    std::lock_guard lock(mutex_);
    const auto result = media_speed_;
    media_speed_.reset();
    return result;
}

std::optional<bool> PlaybackOsd::take_media_pause()
{
    std::lock_guard lock(mutex_);
    const auto result = media_pause_;
    media_pause_.reset();
    return result;
}

int PlaybackOsd::input(int action, std::uint64_t now)
{
    std::lock_guard lock(mutex_);
    if (action >= 0)
    {
        const bool wake = sleeping(now);
        last_activity_ = now;
        if (wake)
            return 1;
    }
    if (action == -1)
    {
        if (platform::unix_time() / 60 != guide_minute_)
            update_guide();
        model_.poll_programme_jobs();
        if (const auto notice = model_.take_programme_notice(); !notice.empty())
        {
            now_ = notice;
            banner_until_ = now + 8000000;
            dirty_ = true;
        }

        return 0;
    }
    if (action == IPTV_INPUT_OPTIONS)
    {
        audio_menu_ = !audio_menu_;
        tab_ = 0;
        list_ = false;
        audio_focus_ = 0;
        for (unsigned i = 0; i < audio_.count; ++i)
            if (audio_.tracks[i].pid == audio_.selected_pid)
                audio_focus_ = i + 1;
        dirty_ = true;
        return 1;
    }
    if (audio_menu_)
    {
        if (interface_rtl())
        {
            if (action == IPTV_INPUT_LEFT)
                action = IPTV_INPUT_RIGHT;
            else if (action == IPTV_INPUT_RIGHT)
                action = IPTV_INPUT_LEFT;
        }
        if (action == IPTV_INPUT_L1 || action == IPTV_INPUT_R1)
        {
            const unsigned tabs = live_ ? 4 : 5;
            tab_ = (tab_ + (action == IPTV_INPUT_R1 ? 1 : tabs - 1)) % tabs;
            subtitle_focus_ = 0;
            for (unsigned i = 0; i < subtitles_.tracks.size(); ++i)
                if (subtitles_.tracks[i].id == subtitles_.selected)
                    subtitle_focus_ = i + 1;
            dirty_ = true;
            return 1;
        }
        if (tab_ == 0 && (action == IPTV_INPUT_PLAY_PAUSE || action == IPTV_INPUT_GO_LIVE ||
                          action == IPTV_INPUT_SQUARE))
        {
            const int step = action == IPTV_INPUT_PLAY_PAUSE ? -50 : 50;
            audio_delay_ = audio_.delay_ms =
                action == IPTV_INPUT_SQUARE ? 0 : std::clamp(audio_.delay_ms + step, -2000, 2000);
            dirty_ = true;
            return 1;
        }
        if (tab_ == (live_ ? 3u : 4u))
        {
            const bool auto_available =
                quality_.selected < quality_.count &&
                quality_.variants[quality_.selected].bandwidth &&
                std::count_if(quality_.variants, quality_.variants + quality_.count,
                              [](const auto &v) { return v.bandwidth != 0; }) > 1;
            if (action == IPTV_INPUT_CIRCLE)
                audio_menu_ = false;
            else if (action == IPTV_INPUT_UP || action == IPTV_INPUT_LEFT)
                quality_focus_ = quality_focus_ ? quality_focus_ - 1 : 0;
            else if (action == IPTV_INPUT_DOWN || action == IPTV_INPUT_RIGHT)
                quality_focus_ = std::min(quality_focus_ + 1, quality_.count + 1);
            else if (action == IPTV_INPUT_CROSS && quality_.count > 1 &&
                     (!quality_focus_ ||
                      (quality_focus_ == quality_.count + 1
                           ? auto_available
                           : quality_.variants[quality_focus_ - 1].bandwidth != 0)))
            {
                auto_quality_selection_ = quality_focus_ == quality_.count + 1;
                quality_selection_ = quality_focus_ && !auto_quality_selection_
                                         ? quality_.variants[quality_focus_ - 1]
                                     : auto_quality_selection_
                                         ? quality_.variants[quality_.selected]
                                         : iptv_hls_quality_t{};
                audio_menu_ = false;
                return 2;
            }
            dirty_ = true;
            return 1;
        }
        if (tab_ == 3)
        {
            if (action == IPTV_INPUT_CIRCLE)
                audio_menu_ = false;
            else if (action == IPTV_INPUT_CROSS && media_.seekable && media_.chapter_count)
            {
                media_seek_ = media_.chapter_positions[chapter_focus_];
                audio_menu_ = false;
                banner_until_ = now + 5000000;
            }
            else if (action == IPTV_INPUT_UP || action == IPTV_INPUT_LEFT)
                chapter_focus_ = chapter_focus_ ? chapter_focus_ - 1 : 0;
            else if (action == IPTV_INPUT_DOWN || action == IPTV_INPUT_RIGHT)
                chapter_focus_ = std::min(chapter_focus_ + 1,
                                          media_.chapter_count ? media_.chapter_count - 1 : 0);
            dirty_ = true;
            return 1;
        }
        if (tab_ == 2)
        {
            if (action == IPTV_INPUT_CIRCLE)
                audio_menu_ = false;
            else if (picture_focus_ >= 7 &&
                     (action == IPTV_INPUT_CROSS || action == IPTV_INPUT_LEFT ||
                      action == IPTV_INPUT_RIGHT))
            {
                int &value =
                    picture_focus_ == 7 ? settings_.subtitle_size : settings_.subtitle_color;
                value = (value + (action == IPTV_INPUT_LEFT ? 2 : 1)) % 3;
                subtitle_cues_.clear();
            }
            else if (action == IPTV_INPUT_CROSS)
            {
                picture_selection_ = picture_mode_ = picture_focus_;
                settings_.picture_mode = picture_mode_;
            }
            else if (action == IPTV_INPUT_UP || action == IPTV_INPUT_LEFT)
                picture_focus_ = picture_focus_ ? picture_focus_ - 1 : 0;
            else if (action == IPTV_INPUT_DOWN || action == IPTV_INPUT_RIGHT)
                picture_focus_ = std::min(8u, picture_focus_ + 1);
            dirty_ = true;
            return 1;
        }
        auto &focus = (tab_ == 1) ? subtitle_focus_ : audio_focus_;
        const unsigned count =
            (tab_ == 1) ? static_cast<unsigned>(subtitles_.tracks.size()) : audio_.count;
        if (action == IPTV_INPUT_CIRCLE)
            audio_menu_ = false;
        else if (action == IPTV_INPUT_CROSS && count)
        {
            if (tab_ == 1)
                subtitle_selection_ = focus ? subtitles_.tracks[focus - 1].id : 0;
            else if (!audio_.pending)
                audio_selection_ = focus ? audio_.tracks[focus - 1].pid : 0;
        }
        else if (action == IPTV_INPUT_UP || action == IPTV_INPUT_DOWN ||
                 action == IPTV_INPUT_LEFT || action == IPTV_INPUT_RIGHT)
        {
            const int delta = action == IPTV_INPUT_UP     ? -1
                              : action == IPTV_INPUT_DOWN ? 1
                              : action == IPTV_INPUT_LEFT ? -9
                                                          : 9;
            focus = static_cast<unsigned>(
                std::clamp(static_cast<int>(focus) + delta, 0, static_cast<int>(count)));
        }
        dirty_ = true;
        return 1;
    }
    if (live_ && action == IPTV_INPUT_TOUCHPAD)
    {
        const bool stopping = model_.recording();
        model_.toggle_recording(current_id_);
        now_ = stopping             ? tr("Stopping recording...")
               : model_.recording() ? tr("Recording to USB. Touchpad stops.")
                                    : tr("Recording requested; check USB and programme schedules.");
        banner_until_ = now + 5000000;
        dirty_ = true;
        return 1;
    }
    if (action == IPTV_INPUT_TRIANGLE || action == IPTV_INPUT_TOUCHPAD)
    {
        banner_until_ = now + 5000000;
        return 1;
    }
    if (action == IPTV_INPUT_CIRCLE && list_)
    {
        list_ = false;
        dirty_ = true;
        return 1;
    }
    if (!live_)
    {
        if (media_.available && action == IPTV_INPUT_PLAY_PAUSE)
            media_pause_ = !media_.paused;
        else if (media_.seekable && action == IPTV_INPUT_GO_LIVE)
        {
            constexpr unsigned speeds[] = {50, 75, 100, 125, 150, 200};
            const auto current = media_.speed_percent ? media_.speed_percent : 100;
            unsigned next = 50;
            for (const auto speed : speeds)
                if (speed > current)
                {
                    next = speed;
                    break;
                }
            media_speed_ = media_.speed_percent = next;
        }
        else if (media_.seekable && action == IPTV_INPUT_SQUARE)
            media_seek_ = 0;
        else if (media_.seekable && (action == IPTV_INPUT_LEFT || action == IPTV_INPUT_RIGHT ||
                                     action == IPTV_INPUT_L1 || action == IPTV_INPUT_R1))
        {
            const std::int64_t step =
                (action == IPTV_INPUT_L1 || action == IPTV_INPUT_R1) ? 60000000 : 10000000;
            const bool backwards = action == IPTV_INPUT_LEFT || action == IPTV_INPUT_L1;
            const auto position = static_cast<std::int64_t>(media_.position_us);
            media_seek_ = static_cast<std::uint64_t>(std::clamp<std::int64_t>(
                position + (backwards ? -step : step), 0,
                static_cast<std::int64_t>(media_.duration_us ? media_.duration_us - 1 : 0)));
        }
        else
            return 0;
        banner_until_ = now + 5000000;
        return 1;
    }
    if (!list_ && (action == IPTV_INPUT_PLAY_PAUSE || action == IPTV_INPUT_GO_LIVE ||
                   action == IPTV_INPUT_LEFT || action == IPTV_INPUT_RIGHT))
    {
        banner_until_ = now + 5000000;
        return 0; // The player owns the live-history request.
    }
    if (action == IPTV_INPUT_SQUARE)
    {
        if (const auto previous = model_.previous_channel(current_id_))
            return select(*previous);
        banner_until_ = now + 5000000;
        return 1;
    }
    if (channels_.empty())
        return 0;
    if (action == IPTV_INPUT_L1 || action == IPTV_INPUT_R1 ||
        (!list_ && (action == IPTV_INPUT_UP || action == IPTV_INPUT_DOWN)))
    {
        const bool backwards = action == IPTV_INPUT_L1 || action == IPTV_INPUT_DOWN;
        const auto current = current_position_;
        const unsigned at = current < 0
                                ? (backwards ? 0u : static_cast<unsigned>(channels_.size() - 1))
                                : static_cast<unsigned>(current);
        return select(channels_[(at + channels_.size() + (backwards ? -1 : 1)) % channels_.size()]);
    }
    if (action == IPTV_INPUT_CROSS)
    {
        if (list_)
            return select(channels_[focus_]);
        list_ = true;
        dirty_ = true;
        return 1;
    }
    if (action == IPTV_INPUT_UP || action == IPTV_INPUT_DOWN ||
        (list_ && (action == IPTV_INPUT_LEFT || action == IPTV_INPUT_RIGHT)))
    {
        if (list_)
        {
            const int delta = action == IPTV_INPUT_UP     ? -1
                              : action == IPTV_INPUT_DOWN ? 1
                              : action == IPTV_INPUT_LEFT ? -9
                                                          : 9;
            focus_ = static_cast<unsigned>(std::clamp(static_cast<int>(focus_) + delta, 0,
                                                      static_cast<int>(channels_.size()) - 1));
        }
        list_ = true;
        dirty_ = true;
        return 1;
    }
    return 0;
}

void PlaybackOsd::line(std::string_view value, float x, float y, float size, float width)
{
    const auto &face = face_for(fonts_, fonts_.regular, value);
    if (face.font)
        panel_.text(*face.font, readable(face, value), x, y, size, width);
}

std::optional<unsigned> PlaybackOsd::take_picture_selection()
{
    std::lock_guard lock(mutex_);
    const auto selection = picture_selection_;
    picture_selection_.reset();
    return selection;
}

bool PlaybackOsd::copy_picture_settings(Settings &settings)
{
    std::lock_guard lock(mutex_);
    const bool changed = settings.picture_mode != settings_.picture_mode ||
                         settings.subtitle_size != settings_.subtitle_size ||
                         settings.subtitle_color != settings_.subtitle_color;
    settings.picture_mode = settings_.picture_mode;
    settings.subtitle_size = settings_.subtitle_size;
    settings.subtitle_color = settings_.subtitle_color;
    return changed;
}

void PlaybackOsd::paint()
{
    if (audio_menu_ && tab_ == (live_ ? 3u : 4u))
    {
        panel_.reset(80, 100, 820, 860);
        line(tr("Quality"), 28, 54, 32, 760);
        line(quality_.count > 1 ? tr("Reopens this video at the chosen quality")
                                : tr("This stream provides one quality"),
             28, 88, 22, 760);
        if (quality_.count > 1)
        {
            const unsigned begin = quality_focus_ / 9 * 9;
            for (unsigned row = 0; row < 9 && begin + row <= quality_.count + 1; ++row)
            {
                const auto index = begin + row, top = 110 + row * 66;
                if (index == quality_focus_)
                    for (unsigned y = top; y < top + 60; ++y)
                        std::fill_n(panel_.pixels.begin() + y * panel_.width + 16, 788, 66);
                std::string label = tr("Best available");
                if (index == quality_.count + 1)
                {
                    const bool available =
                        quality_.selected < quality_.count &&
                        quality_.variants[quality_.selected].bandwidth &&
                        std::count_if(quality_.variants, quality_.variants + quality_.count,
                                      [](const auto &v) { return v.bandwidth != 0; }) > 1;
                    label = tr(available ? "Auto (reduce on buffering)"
                                         : "Auto unavailable (missing bitrate)");
                    if (quality_.automatic)
                        label = std::string(tr("Playing")) + ": " + label;
                }
                else if (index)
                {
                    const auto &v = quality_.variants[index - 1];
                    label = v.bandwidth ? std::to_string(v.bandwidth / 1000) + " kbps"
                                        : tr("Unsupported quality");
                    if (v.width && v.height)
                        label = std::to_string(v.width) + " x " + std::to_string(v.height) + " / " +
                                label;
                    if (v.codec == IPTV_HLS_CODEC_HEVC)
                        label += " / HEVC";
                    else if (v.codec == IPTV_HLS_CODEC_AVC)
                        label += " / H.264";
                    if (quality_.selected == index - 1)
                        label = std::string(tr("Playing")) + ": " + label;
                }
                line(label, 28, top + 40, 26, 760);
            }
        }
        if (live_)
            line(tr("Live restarts at the live edge; replay buffer is cleared"), 28, 746, 20, 760);
        line(tr("L1/R1 Change tab"), 28, 790, 22, 760);
        line(tr("Cross Select · Circle Close"), 28, 833, 22, 760);
    }
    else if (audio_menu_ && tab_ == 3)
    {
        panel_.reset(80, 100, 820, 860);
        line(tr("Chapters"), 28, 54, 32, 760);
        line(media_.chapter_count ? tr("Choose a chapter") : tr("No chapters in this video"), 28,
             88, 22, 760);
        const unsigned begin = chapter_focus_ / 9 * 9;
        for (unsigned row = 0; row < 9 && begin + row < media_.chapter_count; ++row)
        {
            const auto index = begin + row, top = 110 + row * 72;
            if (index == chapter_focus_)
                for (unsigned y = top; y < top + 66; ++y)
                    std::fill_n(panel_.pixels.begin() + y * panel_.width + 16, 788, 66);
            const auto seconds = media_.chapter_positions[index] / 1000000;
            const auto title = std::string(media_.chapter_titles[index],
                                           strnlen(media_.chapter_titles[index], 129));
            line(std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") +
                     std::to_string(seconds % 60) + "  " + title,
                 28, top + 43, 28, 760);
        }
        line(tr("L1/R1 Change tab"), 28, 764, 22, 760);
        line(tr("Cross Select · Circle Close"), 28, 833, 22, 760);
    }
    else if (audio_menu_ && tab_ == 2)
    {
        panel_.reset(80, 100, 820, 860);
        line(tr("Picture"), 28, 54, 32, 760);
        line(tr("Choose the shape of the picture"), 28, 88, 22, 760);
        const char *labels[] = {
            "Fit original picture",     "Stretch to screen", "Correct to 4:3", "Correct to 16:9",
            "Fill screen (crop edges)", "Zoom 125%",         "Zoom 150%"};
        const char *sizes[] = {"Small", "Medium", "Large"};
        const char *colors[] = {"White", "Yellow", "Cyan"};
        for (unsigned row = 0; row < 9; ++row)
        {
            const unsigned top = 110 + row * 64;
            if (row == picture_focus_)
                for (unsigned y = top; y < top + 58; ++y)
                    std::fill_n(panel_.pixels.begin() + y * panel_.width + 16, 788, 66);
            const std::string label =
                row < 7 ? std::string(row == picture_mode_ ? "> " : "") + tr(labels[row])
                : row == 7
                    ? std::string(tr("Subtitle size")) + ": " + tr(sizes[settings_.subtitle_size])
                    : std::string(tr("Subtitle color")) + ": " +
                          tr(colors[settings_.subtitle_color]);
            line(label, 28, top + 40, 28, 760);
        }
        line(tr("L1/R1 Change tab"), 28, 764, 22, 760);
        line(tr("Cross Select · Circle Close"), 28, 833, 22, 760);
    }
    else if (audio_menu_ && (tab_ == 1))
    {
        panel_.reset(80, 100, 820, 860);
        line(tr("Subtitles"), 28, 54, 32, 760);
        line(subtitles_.tracks.empty() ? tr("No supported subtitles advertised")
             : subtitles_.error != iptv::SubtitleError::none
                 ? tr("Subtitles unavailable. Choose another track.")
                 : tr("Choose a language or turn subtitles off"),
             28, 88, 22, 760);
        const unsigned begin = (subtitle_focus_ / 9) * 9;
        for (unsigned row = 0; row < 9 && begin + row <= subtitles_.tracks.size(); ++row)
        {
            const unsigned index = begin + row, top = 110 + row * 72;
            if (index == subtitle_focus_)
                for (unsigned y = top; y < top + 66; ++y)
                    std::fill_n(panel_.pixels.begin() + y * panel_.width + 16, 788, 66);
            std::string label = tr("Off");
            unsigned id = 0;
            if (index)
            {
                const auto &track = subtitles_.tracks[index - 1];
                id = track.id;
                label = tr("Track ") + std::to_string(index);
                if (!track.language.empty())
                    label += " (" + track.language + ")";
                if (track.forced)
                    label += tr("  Forced");
                if (track.hearing_impaired)
                    label += tr("  Hearing impaired");
                if (!track.title.empty())
                    label += "  " + track.title;
            }
            line((id == subtitles_.selected ? "> " : "") + label, 28, top + 43, 28, 760);
        }
        line(tr("L1/R1 Change tab"), 28, 764, 22, 760);
        line(tr("Up/Down Browse · Left/Right Page"), 28, 800, 22, 760);
        line(tr("Cross Select · Circle Close"), 28, 833, 22, 760);
    }
    else if (audio_menu_)
    {
        panel_.reset(80, 70, 820, 950);
        line(tr("Audio"), 28, 54, 32, 760);
        line(!audio_.count       ? tr("No supported audio tracks advertised")
             : audio_.pending    ? tr("Changing audio...")
             : audio_.result < 0 ? tr("Could not switch. Choose another track.")
             : audio_.selected_pid && audio_.disabled
                 ? tr("Audio unavailable. Choose another track.")
                 : tr("Choose a language or turn audio off"),
             28, 88, 22, 760);
        const unsigned begin = (audio_focus_ / 9) * 9;
        for (unsigned row = 0; row < 9 && begin + row <= audio_.count; ++row)
        {
            const unsigned index = begin + row, top = 110 + row * 72;
            if (index == audio_focus_)
                for (unsigned y = top; y < top + 66; ++y)
                    std::fill_n(panel_.pixels.begin() + y * panel_.width + 16, 788, 66);
            std::string label = tr("Off");
            std::uint32_t pid = 0;
            if (index)
            {
                const auto &track = audio_.tracks[index - 1];
                pid = track.pid;
                const auto *title = audio_.titles[index - 1];
                const auto *language = audio_.languages[index - 1];
                label = title[0] ? std::string(title, strnlen(title, 128))
                                 : tr("Track ") + std::to_string(index);
                if (language[0])
                    label += " (" + std::string(language, strnlen(language, 32)) + ")";
                else if (track.language[0])
                    label += " (" + std::string(track.language, 3) + ")";
                const char *codec = track.stream_type == 0x81   ? "AC-3"
                                    : track.stream_type == 0x87 ? "E-AC-3"
                                    : track.stream_type == 3 || track.stream_type == 4
                                        ? "MPEG audio"
                                        : "AAC";
                label += std::string("  ") + codec;
                if (track.audio_type == 2)
                    label += tr("  Hearing impaired");
                if (track.audio_type == 3)
                    label += tr("  Audio description");
            }
            line((pid == audio_.selected_pid ? "> " : "") + label, 28, top + 43, 28, 760);
        }
        line(std::string(tr("Sound delay: ")) + (audio_.delay_ms > 0 ? "+" : "") +
                 std::to_string(audio_.delay_ms) + tr(" ms (positive = later)"),
             28, 785, 22, 760);
        line(tr("L2 Earlier | R2 Later | Square Reset"), 28, 815, 22, 760);
        line(tr("L1/R1 Change tab"), 28, 846, 22, 760);
        line(tr("Up/Down Browse · Left/Right Page"), 28, 876, 22, 760);
        line(tr("Cross Select · Circle Close"), 28, 909, 22, 760);
    }
    else if (list_)
    {
        panel_.reset(80, 100, 760, 860);
        line(tr("Channels"), 28, 54, 32, 700);
        line(std::to_string(focus_ + 1) + " / " + std::to_string(channels_.size()), 28, 88, 22,
             700);
        const unsigned begin = (focus_ / 9) * 9;
        for (unsigned row = 0; row < 9 && begin + row < channels_.size(); ++row)
        {
            const auto index = channels_[begin + row];
            const auto channel = model_.channel(index);
            const unsigned top = 110 + row * 72;
            if (begin + row == focus_)
                for (unsigned y = top; y < top + 66; ++y)
                    std::fill_n(panel_.pixels.begin() + y * panel_.width + 16, 728, 66);
            line((channel.id == current_id_ ? "> " : "") + model_.number_label(index) + "  " +
                     display_name(channel),
                 28, top + 43, 28, 700);
        }
        line(tr("Up/Down Browse · Left/Right Page"), 28, 800, 22, 700);
        line(tr("Cross Watch · Circle Close"), 28, 833, 22, 700);
    }
    else
    {
        const bool timeline = history_.available || media_.available;
        panel_.reset(80, timeline ? 690 : 770, 1760, timeline ? 310 : 230);
        line(title_, 30, 52, 36, 1700);
        line(now_, 30, 101, 28, 1700);
        line(next_, 30, 143, 28, 1700);
        if (history_.available)
        {
            const auto seconds = history_.last_us > history_.position_us
                                     ? (history_.last_us - history_.position_us) / 1000000
                                     : 0;
            std::string position = history_.paused ? tr("Paused")
                                   : seconds <= 3  ? tr("Live")
                                                   : tr("Replay");
            if (seconds > 3)
                position += "  " + std::to_string(seconds / 60) + ":" +
                            (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60) +
                            tr(" behind live");
            if (history_.expired)
                position += tr("  Earlier video is no longer retained");
            line(position, 30, 187, 24, 1700);
            line(tr("L2 Pause / resume  |  Left / Right 30 seconds  |  R2 Back to live"), 30, 231,
                 24, 1700);
        }
        if (media_.available)
        {
            const auto time = [](std::uint64_t us)
            {
                const auto seconds = us / 1000000;
                return std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") +
                       std::to_string(seconds % 60);
            };
            char speed[24];
            const auto percent = media_.speed_percent ? media_.speed_percent : 100;
            std::snprintf(speed, sizeof(speed), "  %u.%02ux", percent / 100, percent % 100);
            line((media_.paused ? tr("Paused  ") : "") + time(media_.position_us) + " / " +
                     (media_.duration_us ? time(media_.duration_us) : tr("Unknown duration")) +
                     speed,
                 30, 101, 28, 1700);
            if (media_.duration_us)
            {
                const auto width = static_cast<unsigned>(
                    1700.0 * std::min(media_.position_us, media_.duration_us) / media_.duration_us);
                for (unsigned y = 125; y < 137; ++y)
                {
                    std::fill_n(panel_.pixels.begin() + y * panel_.width + 30, 1700, 66);
                    std::fill_n(panel_.pixels.begin() + y * panel_.width + 30, width, 210);
                }
            }
            line(media_.seekable
                     ? tr("Left / Right 10s | L1 / R1 1 min | L2 Pause | R2 Speed | Square Restart")
                     : tr("Seeking unavailable | L2 Pause / resume"),
                 30, 187, 24, 1700);
        }
        line(live_ ? tr("Touchpad Record · Square Last · Cross List · Options Tracks · Triangle "
                        "Info · "
                        "Circle Back")
                   : tr("Options Tracks · Triangle Info · Circle Back"),
             30, timeline ? 281 : 204, 24, 1700);
    }
    dirty_ = false;
}

void PlaybackOsd::paint_subtitles()
{
    struct Line
    {
        const hui::gfx::Font *font;
        std::string text;
    };
    std::vector<Line> lines;
    float widest = 0;
    const float sizes[] = {28, 36, 46};
    const float size = sizes[std::clamp(settings_.subtitle_size, 0, 2)], max_width = 1560;
    for (const auto &cue : subtitle_cues_)
    {
        if (!cue)
            continue;
        const auto &face = face_for(fonts_, fonts_.regular, cue->text);
        if (!face.font)
            continue;
        for (const auto &wrapped : face.font->wrap(cue->text, size, max_width))
        {
            std::string_view remaining = wrapped;
            while (!remaining.empty() && lines.size() < 8)
            {
                std::size_t end = 0, next = 0;
                while (next < remaining.size())
                {
                    hui::gfx::next_codepoint(remaining, &next);
                    if (end && face.font->measure(remaining.substr(0, next), size) > max_width)
                        break;
                    end = next;
                }
                const auto text = remaining.substr(0, end);
                widest = std::max(widest, face.font->measure(text, size));
                lines.push_back({face.font, std::string(text)});
                remaining.remove_prefix(end);
            }
            if (lines.size() == 8)
                break;
        }
        if (lines.size() == 8)
            break;
    }
    const unsigned width = lines.empty() ? 0 : static_cast<unsigned>(std::ceil(widest)) + 44;
    const float scale = settings_.larger_text ? 1.25f : 1;
    const unsigned line_height = static_cast<unsigned>(size * scale * 1.28f);
    const unsigned baseline = static_cast<unsigned>(size * scale * 1.17f);
    subtitle_panel_.reset((1920 - width) / 2, 0, width,
                          lines.size() * line_height + (lines.empty() ? 0 : 20));
    for (unsigned i = 0; i < lines.size(); ++i)
        subtitle_panel_.text(*lines[i].font, lines[i].text,
                             (width - lines[i].font->measure(lines[i].text, size)) / 2,
                             baseline + i * line_height, size, max_width);
    styled_subtitles_.canvas_width = 1920;
    styled_subtitles_.canvas_height = 1080;
    styled_subtitles_.bitmaps.clear();
    if (!width)
        return;
    iptv::SubtitleBitmap bitmap;
    bitmap.x = subtitle_panel_.x;
    bitmap.width = width;
    bitmap.height = subtitle_panel_.height;
    const unsigned colors[] = {0xffffff, 0xffff00, 0x00ffff};
    const unsigned color = colors[std::clamp(settings_.subtitle_color, 0, 2)];
    bitmap.argb.reserve(subtitle_panel_.pixels.size());
    for (const auto value : subtitle_panel_.pixels)
    {
        const unsigned alpha = (std::max(28u, unsigned(value)) - 28) * 255 / 207;
        const auto channel = [&](unsigned shift)
        { return (14 * (255 - alpha) + ((color >> shift) & 255) * alpha) / 255; };
        bitmap.argb.push_back(0xff000000u | (channel(16) << 16) | (channel(8) << 8) | channel(0));
    }
    styled_subtitles_.bitmaps.push_back(std::move(bitmap));
}

bool PlaybackOsd::draw(void *surface, std::size_t bytes, unsigned pitch, unsigned sh, unsigned vw,
                       unsigned vh, unsigned depth, std::uint64_t now,
                       const std::vector<std::shared_ptr<const iptv::SubtitleCue>> &subtitles,
                       unsigned hdr)
{
    std::lock_guard lock(mutex_);
    if (!last_activity_ || !paused_)
        last_activity_ = now;
    if (sleeping(now))
    {
        if (!surface)
            return true;
        if ((depth != 8 && depth != 10) || !vw || !vh || vw > pitch || vh > sh || pitch > 8192 ||
            sh > 8192 || (pitch & 1u))
            return false;
        const std::size_t luma = static_cast<std::size_t>(pitch) * sh;
        const std::size_t samples = luma + static_cast<std::size_t>(pitch) * ((sh + 1) / 2);
        if (samples * (depth == 10 ? 2 : 1) > bytes)
            return false;
        if (depth == 8)
        {
            std::memset(surface, 16, luma);
            std::memset(static_cast<std::uint8_t *>(surface) + luma, 128, samples - luma);
        }
        else
        {
            auto *pixels = static_cast<std::uint8_t *>(surface);
            for (std::size_t i = 0; i < samples; ++i)
            {
                const std::uint16_t value = i < luma ? 64 : 512;
                std::memcpy(pixels + i * 2, &value, sizeof(value));
            }
        }
        return true;
    }
    if (!fonts_.regular.font)
        return false;
    if (!started_)
    {
        started_ = true;
        banner_until_ = now + 5000000;
    }
    const bool overlay =
        list_ || audio_menu_ || history_.paused || media_.paused || now < banner_until_;
    const bool captions = !list_ && !audio_menu_ && !subtitles.empty();
    if (!overlay && !captions)
    {
        subtitle_cues_.clear();
        return false;
    }
    if (!surface)
        return true;
    // Keep controls and captions inside the visible crop, independent of picture zoom.
    const float aspect = vh ? float(vw) / vh / (1920.0f / 1080.0f) : 1;
    float scale_x = 1, scale_y = 1;
    if (picture_mode_ == 4)
    {
        scale_x = std::max(1.0f, aspect);
        scale_y = std::max(1.0f, 1 / aspect);
    }
    else if (picture_mode_ >= 5)
    {
        const float zoom = picture_mode_ == 5 ? 1.25f : 1.5f;
        scale_x = std::max(1.0f, std::min(1.0f, aspect) * zoom);
        scale_y = std::max(1.0f, std::min(1.0f, 1 / aspect) * zoom);
    }
    bool drawn = false;
    if (captions)
    {
        if (subtitle_cues_ != subtitles)
        {
            subtitle_cues_ = subtitles;
            paint_subtitles();
        }
        for (const auto &cue : subtitles)
            if (cue)
                drawn = composite_subtitle_bitmaps(*cue, surface, bytes, pitch, sh, vw, vh, depth,
                                                   hdr, scale_x, scale_y) ||
                        drawn;
        if (subtitle_panel_.height)
        {
            subtitle_panel_.y =
                (overlay ? (history_.available ? 660 : 740) : 1030) - subtitle_panel_.height;
            if (!styled_subtitles_.bitmaps.empty())
            {
                styled_subtitles_.bitmaps[0].y = subtitle_panel_.y;
                drawn = composite_subtitle_bitmaps(styled_subtitles_, surface, bytes, pitch, sh, vw,
                                                   vh, depth, hdr, scale_x, scale_y) ||
                        drawn;
            }
        }
    }
    if (overlay && dirty_)
        paint();
    return (overlay &&
            panel_.composite(surface, bytes, pitch, sh, vw, vh, depth, hdr, scale_x, scale_y)) ||
           drawn;
}
} // namespace ptv
