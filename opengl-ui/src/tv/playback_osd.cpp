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
namespace
{
// Seeking: how long after the last press the seek is asked for, and the longest the timeline
// shows the target while the video reopens.
constexpr std::uint64_t kSeekSettleUs = 700000;
constexpr std::uint64_t kSeekShownUs = 15000000;

const char *const kPictureLabels[] = {
    "Fit original picture",     "Stretch to screen", "Correct to 4:3", "Correct to 16:9",
    "Fill screen (crop edges)", "Zoom 125%",         "Zoom 150%"};
const char *const kSubtitleSizes[] = {"Small", "Medium", "Large"};
const char *const kSubtitleColors[] = {"White", "Yellow", "Cyan"};
const std::uint32_t kSubtitleRgb[] = {0xffffff, 0xffff00, 0x00ffff};

std::string clock_text(std::uint64_t us)
{
    const auto seconds = us / 1000000;
    const auto minutes = seconds / 60;
    return (seconds >= 3600 ? std::to_string(seconds / 3600) + ":" + (minutes % 60 < 10 ? "0" : "")
                            : "") +
           std::to_string(seconds >= 3600 ? minutes % 60 : minutes) + ":" +
           (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
}

std::string speed_text(unsigned percent)
{
    char text[24];
    std::snprintf(text, sizeof(text), "%gx", percent / 100.0);
    return text;
}

std::string minutes(std::uint64_t us)
{
    const auto seconds = us / 1000000;
    return std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") +
           std::to_string(seconds % 60);
}
} // namespace

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
    picture_mode_ = std::clamp(settings_.picture_mode, 0, 6);
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
    // The menu is drawn by the CPU, so each face is named by a font handle, not a texture.
    const auto load =
        [&](const char *name, hui::gfx::Font &font, hui::ui::FontRef &ref, unsigned slot)
    {
        std::string data;
        if (!hui::save::read_file(directory + "/" + name, &data, 64u << 20) ||
            !load_menu_font(std::move(data), font, settings_.larger_text))
            return false;
        ref.font = &font;
        ref.texture = hui::gfx::kFontHandleBase | slot;
        return true;
    };
    if (!load("inter-regular.huifont", regular_, fonts_.regular, 1))
        return false;
    if (!load("inter-semibold.huifont", semibold_, fonts_.semibold, 2))
        fonts_.semibold = fonts_.regular;
    if (!load("montserrat-medium.huifont", display_, fonts_.display, 3))
        fonts_.display = fonts_.semibold;
    fonts_.mono = fonts_.regular;
    // Subtitle language may differ from every channel name in the catalogue.
    (void)load("noto-sans-east-asian.huifont", east_asian_, fonts_.hand, 5);
    (void)load("noto-sans-korean.huifont", korean_, fonts_.pixel, 6);
    {
        std::lock_guard lock(mutex_);
        sheet_ = std::make_unique<PlayerSheet>(fonts_, settings_);
        painted_ = {};
    }
    refresh_menu();
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
    if (state.presented && !media_.presented)
        banner_until_ = platform::monotonic_us() + 5000000;
    media_ = state;
    media_.chapter_count = std::min(media_.chapter_count, unsigned(IPTV_PLAYER_MAX_CHAPTERS));
    if (seek_shown_ && state.presented &&
        (state.position_us > *seek_shown_ ? state.position_us - *seek_shown_
                                          : *seek_shown_ - state.position_us) < 3000000)
        seek_shown_.reset();
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
    int result;
    {
        std::lock_guard lock(mutex_);
        result = handle(action, now);
    }
    refresh_menu();
    return result;
}

void PlaybackOsd::refresh_menu()
{
    std::optional<SheetModel> model;
    std::optional<ControlsModel> controls;
    std::string badge;
    {
        std::lock_guard lock(mutex_);
        // The menu and the channel list share the sheet; one is open at a time.
        const bool sheet_open = audio_menu_ || list_;
        if (!sheet_open && !menu_cue_.bitmaps.empty())
        {
            menu_cue_.bitmaps.clear();
            ++ui_version_;
        }
        if (!sheet_open)
            painted_ = {};
        else if (sheet_)
            model = audio_menu_ ? menu_model() : list_model();
        if (!controls_wanted_ && painted_controls_)
        {
            controls_cue_.bitmaps.clear();
            painted_controls_.reset();
            ++ui_version_;
        }
        else if (controls_wanted_ && sheet_)
            controls = controls_model();
        if (!live_ && media_.available && media_.speed_percent && media_.speed_percent != 100)
            badge = speed_text(media_.speed_percent);
    }
    // Painting takes a while: the presenter keeps drawing what it has meanwhile.
    if (model && *model != painted_)
    {
        iptv::SubtitleCue cue;
        sheet_->paint(*model, cue);
        painted_ = std::move(*model);
        std::lock_guard lock(mutex_);
        if (audio_menu_ || list_)
        {
            menu_cue_ = std::move(cue);
            ++ui_version_;
        }
    }
    if (controls && controls != painted_controls_)
    {
        iptv::SubtitleCue cue;
        sheet_->paint_controls(*controls, cue);
        painted_controls_ = std::move(controls);
        std::lock_guard lock(mutex_);
        if (controls_wanted_)
        {
            controls_cue_ = std::move(cue);
            ++ui_version_;
        }
    }
    if (sheet_ && badge != painted_badge_)
    {
        iptv::SubtitleCue cue;
        sheet_->paint_badge(badge, cue);
        painted_badge_ = badge;
        std::lock_guard lock(mutex_);
        badge_cue_ = std::move(cue);
        ++ui_version_;
    }
}

int PlaybackOsd::handle(int action, std::uint64_t now)
{
    if (action >= 0)
    {
        const bool wake = sleeping(now);
        last_activity_ = now;
        if (wake)
            return 1;
    }
    if (action == -1)
    {
        if (seek_target_ && now >= seek_commit_at_)
        {
            // The presses have stopped: one seek, shown until the picture gets there.
            media_seek_ = seek_shown_ = *seek_target_;
            seek_shown_until_ = now + kSeekShownUs;
            seek_target_.reset();
            banner_until_ = std::max(banner_until_, now + 5000000);
            dirty_ = true;
        }
        if (seek_shown_ && now >= seek_shown_until_)
            seek_shown_.reset();
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
        list_ = false;
        picking_ = false;
        setting_ = menu_settings().front();
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
        // L2 and R2 move the sound earlier and later wherever the menu is, 50 ms at a time;
        // Left and Right on Sound delay by 10 ms, fine enough to set lip sync.
        if (action == IPTV_INPUT_PLAY_PAUSE || action == IPTV_INPUT_GO_LIVE)
        {
            const int step = action == IPTV_INPUT_PLAY_PAUSE ? -50 : 50;
            audio_delay_ = audio_.delay_ms = std::clamp(audio_.delay_ms + step, -2000, 2000);
            dirty_ = true;
            return 1;
        }
        const auto settings = menu_settings();
        // A setting can go away while it is open: tracks change with the quality.
        if (std::find(settings.begin(), settings.end(), setting_) == settings.end())
        {
            setting_ = settings.front();
            picking_ = false;
        }
        dirty_ = true;
        if (picking_)
        {
            const auto options = menu_options(setting_);
            const auto count = static_cast<unsigned>(options.size());
            option_focus_ = std::min(option_focus_, count ? count - 1 : 0);
            if (action == IPTV_INPUT_CIRCLE || action == IPTV_INPUT_LEFT)
                picking_ = false;
            else if (action == IPTV_INPUT_CROSS && count)
                return choose(setting_, option_focus_, now);
            else if (count && (action == IPTV_INPUT_UP || action == IPTV_INPUT_DOWN))
                option_focus_ = (option_focus_ + (action == IPTV_INPUT_UP ? count - 1 : 1)) % count;
            else if (count && (action == IPTV_INPUT_L1 || action == IPTV_INPUT_R1))
                option_focus_ = static_cast<unsigned>(
                    std::clamp(static_cast<int>(option_focus_) + (action == IPTV_INPUT_L1 ? -9 : 9),
                               0, static_cast<int>(count) - 1));
            return 1;
        }
        const auto at = static_cast<unsigned>(
            std::find(settings.begin(), settings.end(), setting_) - settings.begin());
        const auto count = static_cast<unsigned>(settings.size());
        if (action == IPTV_INPUT_CIRCLE)
            audio_menu_ = false;
        else if (action == IPTV_INPUT_UP || action == IPTV_INPUT_DOWN)
            setting_ = settings[(at + (action == IPTV_INPUT_UP ? count - 1 : 1)) % count];
        else if (setting_ == Setting::delay &&
                 (action == IPTV_INPUT_LEFT || action == IPTV_INPUT_RIGHT ||
                  action == IPTV_INPUT_SQUARE))
            audio_delay_ = audio_.delay_ms =
                action == IPTV_INPUT_SQUARE
                    ? 0
                    : std::clamp(audio_.delay_ms + (action == IPTV_INPUT_LEFT ? -10 : 10), -2000,
                                 2000);
        else if (setting_ != Setting::delay &&
                 (action == IPTV_INPUT_CROSS || action == IPTV_INPUT_RIGHT))
        {
            // The list opens at the value in use.
            const auto options = menu_options(setting_);
            picking_ = true;
            option_focus_ = 0;
            for (unsigned i = 0; i < options.size(); ++i)
                if (options[i].checked)
                {
                    option_focus_ = i;
                    break;
                }
        }
        return 1;
    }
    if (live_ && action == IPTV_INPUT_TOUCHPAD)
    {
        const bool stopping = model_.recording();
        model_.toggle_recording(current_id_);
        now_ = stopping             ? tr("Stopping recording...")
               : model_.recording() ? tr("Recording to USB.")
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
        {
            seek_target_.reset();
            media_seek_ = seek_shown_ = 0;
            seek_shown_until_ = now + kSeekShownUs;
        }
        else if (media_.seekable && (action == IPTV_INPUT_LEFT || action == IPTV_INPUT_RIGHT ||
                                     action == IPTV_INPUT_L1 || action == IPTV_INPUT_R1))
        {
            const std::int64_t step =
                (action == IPTV_INPUT_L1 || action == IPTV_INPUT_R1) ? 60000000 : 10000000;
            const bool backwards = action == IPTV_INPUT_LEFT || action == IPTV_INPUT_L1;
            // From where the last press went, not where the picture still is.
            const auto position = static_cast<std::int64_t>(seek_position());
            seek_target_ = static_cast<std::uint64_t>(std::clamp<std::int64_t>(
                position + (backwards ? -step : step), 0,
                static_cast<std::int64_t>(media_.duration_us ? media_.duration_us - 1 : 0)));
            seek_commit_at_ = now + kSeekSettleUs;
        }
        else
            return 0;
        banner_until_ = now + 5000000;
        dirty_ = true;
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

std::vector<PlaybackOsd::Setting> PlaybackOsd::menu_settings() const
{
    std::vector<Setting> settings;
    if (audio_.count)
        settings.push_back(Setting::audio);
    settings.push_back(Setting::delay);
    if (!subtitles_.tracks.empty())
        settings.push_back(Setting::subtitles);
    settings.push_back(Setting::subtitle_size);
    settings.push_back(Setting::subtitle_color);
    settings.push_back(Setting::picture);
    if (!live_ && media_.chapter_count)
        settings.push_back(Setting::chapters);
    if (quality_.count > 1)
        settings.push_back(Setting::quality);
    return settings;
}

std::vector<SheetRow> PlaybackOsd::menu_options(Setting setting) const
{
    std::vector<SheetRow> rows;
    const auto add = [&](std::string label, std::string value, bool checked)
    {
        SheetRow row;
        row.label = std::move(label);
        row.value = std::move(value);
        row.checked = checked;
        rows.push_back(std::move(row));
        return &rows.back();
    };
    switch (setting)
    {
    case Setting::audio:
        if (!audio_.count)
            break;
        add(tr("Off"), "", audio_.disabled || !audio_.selected_pid);
        for (unsigned i = 0; i < audio_.count; ++i)
        {
            const auto &track = audio_.tracks[i];
            const auto *title = audio_.titles[i];
            const auto *language = audio_.languages[i];
            std::string label = title[0] ? std::string(title, strnlen(title, 128))
                                         : tr("Track ") + std::to_string(i + 1);
            if (language[0])
                label += " (" + std::string(language, strnlen(language, 32)) + ")";
            else if (track.language[0])
                label += " (" + std::string(track.language, strnlen(track.language, 3)) + ")";
            std::string detail = track.stream_type == 0x81                          ? "AC-3"
                                 : track.stream_type == 0x87                        ? "E-AC-3"
                                 : track.stream_type == 0x82                        ? "DTS"
                                 : track.stream_type == 3 || track.stream_type == 4 ? "MPEG audio"
                                                                                    : "AAC";
            if (track.audio_type == 2)
                detail += tr("  Hearing impaired");
            if (track.audio_type == 3)
                detail += tr("  Audio description");
            add(label, detail, !audio_.disabled && track.pid == audio_.selected_pid);
        }
        break;
    case Setting::subtitles:
        if (subtitles_.tracks.empty())
            break;
        add(tr("Off"),
            subtitles_.error != iptv::SubtitleError::none
                ? tr("Subtitles unavailable. Choose another track.")
                : "",
            !subtitles_.selected);
        for (unsigned i = 0; i < subtitles_.tracks.size(); ++i)
        {
            const auto &track = subtitles_.tracks[i];
            std::string label =
                !track.title.empty() ? track.title : tr("Track ") + std::to_string(i + 1);
            if (!track.language.empty())
                label += " (" + track.language + ")";
            std::string detail;
            if (track.forced)
                detail += tr("  Forced");
            if (track.hearing_impaired)
                detail += tr("  Hearing impaired");
            const auto begin = detail.find_first_not_of(' ');
            add(label, begin == std::string::npos ? "" : detail.substr(begin),
                track.id == subtitles_.selected);
        }
        break;
    case Setting::picture:
        for (unsigned mode = 0; mode < 7; ++mode)
        {
            auto *row = add(tr(kPictureLabels[mode]), "", mode == picture_mode_);
            switch (mode)
            {
            case 0: // the picture as it is
                row->preview = SheetRow::Preview::shape;
                row->milli = frame_milli_ ? frame_milli_ : 1778;
                row->value = shape_name(frame_milli_);
                break;
            case 1:
                row->preview = SheetRow::Preview::stretch;
                break;
            case 2:
            case 3:
                row->preview = SheetRow::Preview::shape;
                row->milli = mode == 2 ? 1333 : 1778;
                break;
            default:
                row->preview = SheetRow::Preview::zoom;
                row->milli = mode == 4 ? 0 : mode == 5 ? 1250 : 1500;
                break;
            }
        }
        break;
    case Setting::subtitle_size:
        for (unsigned size = 0; size < 3; ++size)
            add(tr(kSubtitleSizes[size]), "",
                static_cast<int>(size) == std::clamp(settings_.subtitle_size, 0, 2));
        break;
    case Setting::subtitle_color:
        for (unsigned color = 0; color < 3; ++color)
        {
            auto *row = add(tr(kSubtitleColors[color]), "",
                            static_cast<int>(color) == std::clamp(settings_.subtitle_color, 0, 2));
            row->preview = SheetRow::Preview::swatch;
            row->rgb = kSubtitleRgb[color];
        }
        break;
    case Setting::chapters:
    {
        unsigned current = 0;
        for (unsigned i = 0; i < media_.chapter_count; ++i)
            if (media_.chapter_positions[i] <= media_.position_us)
                current = i;
        for (unsigned i = 0; i < media_.chapter_count; ++i)
        {
            const std::string title(media_.chapter_titles[i],
                                    strnlen(media_.chapter_titles[i], 129));
            const auto at = minutes(media_.chapter_positions[i]);
            add(title.empty() ? at : title, title.empty() ? "" : at, i == current);
        }
        break;
    }
    case Setting::quality:
    {
        if (quality_.count < 2)
            break;
        add(tr("Best available"), tr("Reopens this video at the chosen quality"), false);
        for (unsigned i = 0; i < quality_.count; ++i)
        {
            const auto &v = quality_.variants[i];
            std::string label = v.bandwidth ? std::to_string(v.bandwidth / 1000) + " kbps"
                                            : tr("Unsupported quality");
            if (v.width && v.height)
                label = std::to_string(v.width) + " x " + std::to_string(v.height) + " / " + label;
            add(label,
                v.codec == IPTV_HLS_CODEC_HEVC  ? "HEVC"
                : v.codec == IPTV_HLS_CODEC_AVC ? "H.264"
                                                : "",
                !quality_.automatic && quality_.selected == i);
        }
        const bool available = quality_.selected < quality_.count &&
                               quality_.variants[quality_.selected].bandwidth &&
                               std::count_if(
                                   quality_.variants, quality_.variants + quality_.count,
                                   [](const auto &v) { return v.bandwidth != 0; }) > 1;
        add(tr(available ? "Auto (reduce on buffering)" : "Auto unavailable (missing bitrate)"),
            live_ ? tr("Live restarts at the live edge; replay buffer is cleared") : "",
            quality_.automatic != 0);
        break;
    }
    case Setting::delay:
        break;
    }
    return rows;
}

std::string PlaybackOsd::menu_value(Setting setting) const
{
    if (setting == Setting::audio && audio_.pending)
        return tr("Changing audio...");
    if (setting == Setting::audio && audio_.result < 0)
        return tr("Could not switch. Choose another track.");
    if (setting == Setting::picture && picture_mode_ == 0 && frame_milli_)
        return std::string(tr("Fit")) + " · " + shape_name(frame_milli_);
    const auto options = menu_options(setting);
    for (const auto &option : options)
        if (option.checked)
            return option.label;
    return setting == Setting::quality ? tr("Best available") : "";
}

int PlaybackOsd::choose(Setting setting, unsigned index, std::uint64_t now)
{
    picking_ = false;
    switch (setting)
    {
    case Setting::audio:
        if (audio_.count && !audio_.pending && index <= audio_.count)
            audio_selection_ = index ? audio_.tracks[index - 1].pid : 0;
        break;
    case Setting::subtitles:
        if (index <= subtitles_.tracks.size())
            subtitle_selection_ = index ? subtitles_.tracks[index - 1].id : 0;
        break;
    case Setting::picture:
        picture_selection_ = picture_mode_ = std::min(index, 6u);
        settings_.picture_mode = static_cast<int>(picture_mode_);
        break;
    case Setting::subtitle_size:
    case Setting::subtitle_color:
        (setting == Setting::subtitle_size ? settings_.subtitle_size : settings_.subtitle_color) =
            static_cast<int>(std::min(index, 2u));
        subtitle_cues_.clear();
        break;
    case Setting::chapters:
        if (media_.seekable && index < media_.chapter_count)
        {
            seek_target_.reset();
            media_seek_ = seek_shown_ = media_.chapter_positions[index];
            seek_shown_until_ = now + kSeekShownUs;
            audio_menu_ = false;
            banner_until_ = now + 5000000;
        }
        break;
    case Setting::quality:
    {
        const bool automatic = index == quality_.count + 1;
        const bool available = quality_.selected < quality_.count &&
                               quality_.variants[quality_.selected].bandwidth &&
                               std::count_if(
                                   quality_.variants, quality_.variants + quality_.count,
                                   [](const auto &v) { return v.bandwidth != 0; }) > 1;
        if (quality_.count < 2 || index > quality_.count + 1 ||
            !(index == 0 || (automatic ? available : quality_.variants[index - 1].bandwidth != 0)))
        {
            picking_ = true; // a quality that cannot play: the list stays
            break;
        }
        auto_quality_selection_ = automatic;
        quality_selection_ = automatic ? quality_.variants[quality_.selected]
                             : index   ? quality_.variants[index - 1]
                                       : iptv_hls_quality_t{};
        audio_menu_ = false;
        return 2;
    }
    case Setting::delay:
        break;
    }
    return 1;
}

SheetModel PlaybackOsd::menu_model() const
{
    SheetModel model;
    const auto name = [](Setting setting)
    {
        switch (setting)
        {
        case Setting::audio:
            return tr("Audio");
        case Setting::subtitles:
            return tr("Subtitles");
        case Setting::delay:
            return tr("Sound delay");
        case Setting::picture:
            return tr("Picture");
        case Setting::subtitle_size:
            return tr("Subtitle size");
        case Setting::subtitle_color:
            return tr("Subtitle color");
        case Setting::chapters:
            return tr("Chapters");
        case Setting::quality:
            return tr("Quality");
        }
        return "";
    };
    if (picking_)
    {
        model.picker = true;
        model.title = name(setting_);
        model.rows = menu_options(setting_);
        model.selected =
            std::min(option_focus_, model.rows.empty() ? 0u : unsigned(model.rows.size() - 1));
        return model;
    }
    model.title = tr("Playback");
    const char *section = nullptr;
    for (const auto setting : menu_settings())
    {
        const char *heading = setting == Setting::audio || setting == Setting::delay ? "Sound"
                              : setting == Setting::picture                          ? "Picture"
                              : setting == Setting::chapters || setting == Setting::quality
                                  ? "Video"
                                  : "Subtitles";
        if (heading != section)
        {
            section = heading;
            SheetRow header;
            header.kind = SheetRow::Kind::header;
            header.label = tr(heading);
            model.rows.push_back(std::move(header));
        }
        if (setting == setting_)
            model.selected = static_cast<unsigned>(model.rows.size());
        SheetRow row;
        row.label = name(setting);
        if (setting == Setting::delay)
        {
            row.kind = SheetRow::Kind::stepper;
            row.current = audio_.delay_ms;
            row.low = -2000;
            row.high = 2000;
            row.step = 10;
        }
        else
        {
            row.kind = SheetRow::Kind::picker;
            row.value = menu_value(setting);
        }
        model.rows.push_back(std::move(row));
    }
    return model;
}

std::uint64_t PlaybackOsd::seek_position() const
{
    return seek_target_ ? *seek_target_ : seek_shown_ ? *seek_shown_ : media_.position_us;
}

SheetModel PlaybackOsd::list_model() const
{
    // A page of channels about the one in focus: the list itself may be very long.
    constexpr unsigned kPage = 9;
    SheetModel model;
    model.picker = true;
    model.title = std::string(tr("Channels")) + "  " + std::to_string(focus_ + 1) + " / " +
                  std::to_string(channels_.size());
    model.confirm = tr("Watch");
    model.back = tr("Close");
    const auto time = static_cast<std::int64_t>(platform::unix_time());
    const unsigned begin = focus_ / kPage * kPage;
    for (unsigned at = begin; at < begin + kPage && at < channels_.size(); ++at)
    {
        const auto index = channels_[at];
        const auto channel = model_.channel(index);
        SheetRow row;
        const std::string name = model_.number_label(index) + "  " + display_name(channel);
        row.label = name;
        if (const auto programme = model_.guide().now(channel.id, time))
            row.value = programme->title;
        row.checked = channel.id == current_id_;
        model.rows.push_back(std::move(row));
    }
    model.selected = focus_ - begin;
    return model;
}

ControlsModel PlaybackOsd::controls()
{
    std::lock_guard lock(mutex_);
    return controls_model();
}

ControlsModel PlaybackOsd::controls_model() const
{
    using Button = ui::Button;
    ControlsModel model;
    model.title = title_;
    model.line = now_;
    const auto hint = [&](Button button, const char *label, Button second = Button::none)
    { model.hints.push_back({button, second, tr(label)}); };
    if (media_.available)
    {
        const auto percent = media_.speed_percent ? media_.speed_percent : 100;
        model.chip = buffering_       ? tr("Buffering...")
                     : media_.paused  ? tr("Paused")
                     : percent != 100 ? speed_text(percent)
                                      : "";
        model.chip_lit = !buffering_ && !media_.paused;
        const auto position = seek_position();
        model.elapsed = clock_text(position);
        model.total = media_.duration_us ? clock_text(media_.duration_us) : tr("Unknown duration");
        model.progress = media_.duration_us
                             ? static_cast<int>(1000 * std::min(position, media_.duration_us) /
                                                media_.duration_us)
                             : 0;
        if (seek_target_)
        {
            // Where the presses are going, from where the picture is: "+1:30", "-0:20".
            const auto from = media_.position_us;
            model.chip = (position >= from ? "+" : "-") +
                         clock_text(position >= from ? position - from : from - position);
            model.chip_lit = true;
        }
        hint(Button::l2, media_.paused ? "Resume" : "Pause");
        if (media_.seekable)
        {
            hint(Button::dpad, "Seek");
            hint(Button::l1, "1 minute", Button::r1);
            hint(Button::r2, "Speed");
            hint(Button::square, "Restart");
        }
    }
    if (live_)
    {
        model.detail = next_;
        if (history_.available)
        {
            const auto seconds = history_.last_us > history_.position_us
                                     ? (history_.last_us - history_.position_us) / 1000000
                                     : 0;
            model.chip = history_.paused ? tr("Paused") : seconds <= 3 ? tr("Live") : tr("Replay");
            model.chip_lit = !history_.paused && seconds <= 3;
            if (seconds > 3)
                model.chip += "  " + minutes(seconds * 1000000) + tr(" behind live");
            if (history_.expired)
                model.detail = tr("Earlier video is no longer retained");
            hint(Button::l2, history_.paused ? "Resume" : "Pause");
            hint(Button::dpad, "Seek");
            hint(Button::r2, "Back to live");
        }
        hint(Button::cross, "Channels");
        hint(Button::square, "Last channel");
        hint(Button::touchpad, model_.recording() ? "Stop recording" : "Record");
    }
    hint(Button::options, "Menu");
    hint(Button::circle, "Back");
    return model;
}

void PlaybackOsd::paint()
{
    if (list_)
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
                const auto minutes = seconds / 60;
                return (seconds >= 3600
                            ? std::to_string(seconds / 3600) + ":" + (minutes % 60 < 10 ? "0" : "")
                            : "") +
                       std::to_string(seconds >= 3600 ? minutes % 60 : minutes) + ":" +
                       (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
            };
            char speed[24];
            const auto percent = media_.speed_percent ? media_.speed_percent : 100;
            std::snprintf(speed, sizeof(speed), "  %gx", percent / 100.0);
            line((buffering_      ? std::string(tr("Buffering...")) + "  "
                  : media_.paused ? std::string(tr("Paused  "))
                                  : "") +
                     time(media_.position_us) + " / " +
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
                       unsigned hdr, bool buffering)
{
    std::lock_guard lock(mutex_);
    if (vw && vh && vw * 1000 / vh != frame_milli_)
    {
        frame_milli_ = vw * 1000 / vh;
        dirty_ = true;
    }
    if (buffering_ != buffering)
    {
        buffering_ = buffering;
        dirty_ = true;
        banner_until_ = now + 5000000;
    }
    if (!last_activity_ || !paused_)
        last_activity_ = now;
    screen_sleeping_ = sleeping(now);
    if (screen_sleeping_)
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
    const bool overlay = list_ || audio_menu_ || history_.paused || media_.paused || buffering_ ||
                         now < banner_until_;
    controls_wanted_ = overlay && !list_ && !audio_menu_;
    const bool captions = !list_ && !audio_menu_ && !subtitles.empty();
    const bool speed_badge = !live_ && media_.available && media_.speed_percent &&
                             media_.speed_percent != 100 && !overlay;
    overlay_wanted_ = overlay;
    badge_wanted_ = speed_badge;
    captions_ = captions;
    if (screen_)
    {
        // Everything but the stream's own picture subtitles is on the screen's planes.
        if (captions && subtitle_cues_ != subtitles)
        {
            subtitle_cues_ = subtitles;
            paint_subtitles();
            ++ui_version_;
        }
        if (overlay && dirty_ && !sheet_)
        {
            paint();
            panel_cue_.canvas_width = 1920;
            panel_cue_.canvas_height = 1080;
            panel_cue_.bitmaps.clear();
            if (panel_.width && panel_.height)
            {
                iptv::SubtitleBitmap bitmap;
                bitmap.x = static_cast<int>(panel_.x);
                bitmap.y = static_cast<int>(panel_.y);
                bitmap.width = panel_.width;
                bitmap.height = panel_.height;
                bitmap.argb.reserve(panel_.pixels.size());
                for (const auto value : panel_.pixels)
                    bitmap.argb.push_back(0xff000000u | value * 0x010101u);
                panel_cue_.bitmaps.push_back(std::move(bitmap));
            }
            ++ui_version_;
        }
        const bool pictures =
            captions && std::any_of(subtitles.begin(), subtitles.end(),
                                    [](const auto &cue) { return cue && !cue->bitmaps.empty(); });
        if (!captions)
            subtitle_cues_.clear();
        if (!pictures)
            return false;
        if (!surface)
            return true;
    }
    if (!overlay && !captions && !speed_badge)
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
    if (screen_)
    {
        for (const auto &cue : subtitles)
            if (cue)
                drawn = composite_subtitle_bitmaps(*cue, surface, bytes, pitch, sh, vw, vh, depth,
                                                   hdr, scale_x, scale_y) ||
                        drawn;
        return drawn;
    }
    if (speed_badge && sheet_)
        drawn = composite_subtitle_bitmaps(badge_cue_, surface, bytes, pitch, sh, vw, vh, depth,
                                           hdr, scale_x, scale_y);
    else if (speed_badge)
    {
        if (speed_panel_percent_ != media_.speed_percent)
        {
            char speed[24];
            std::snprintf(speed, sizeof(speed), "%gx", media_.speed_percent / 100.0);
            speed_panel_.reset(1720, 50, 140, 64);
            speed_panel_.text(regular_, speed, 22, 44, 32, 110);
            speed_panel_percent_ = media_.speed_percent;
        }
        drawn =
            speed_panel_.composite(surface, bytes, pitch, sh, vw, vh, depth, hdr, scale_x, scale_y);
    }
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
    if (audio_menu_ || (list_ && sheet_))
        return composite_subtitle_bitmaps(menu_cue_, surface, bytes, pitch, sh, vw, vh, depth, hdr,
                                          scale_x, scale_y) ||
               drawn;
    if (sheet_ && !list_)
    {
        // The controls' picture is painted on the control thread; it may lag a frame behind.
        if (overlay)
            drawn = composite_subtitle_bitmaps(controls_cue_, surface, bytes, pitch, sh, vw, vh,
                                               depth, hdr, scale_x, scale_y) ||
                    drawn;
        return overlay || drawn;
    }
    if (overlay && dirty_)
        paint();
    return (overlay &&
            panel_.composite(surface, bytes, pitch, sh, vw, vh, depth, hdr, scale_x, scale_y)) ||
           drawn;
}
namespace
{
// Makes [x0, x1) x [y0, y1) of a limited-range plane transparent: black, neutral chroma.
void clear_plane(std::uint8_t *plane, unsigned pitch, unsigned rows, unsigned depth,
                 const std::array<int, 4> &box)
{
    const auto x0 = static_cast<unsigned>(std::max(0, box[0])) & ~1u;
    const auto y0 = static_cast<unsigned>(std::max(0, box[1])) & ~1u;
    const auto x1 = std::min(pitch, (static_cast<unsigned>(std::max(0, box[2])) + 1u) & ~1u);
    const auto y1 = std::min(rows, (static_cast<unsigned>(std::max(0, box[3])) + 1u) & ~1u);
    if (x1 <= x0 || y1 <= y0)
        return;
    const std::size_t chroma = static_cast<std::size_t>(pitch) * rows;
    const auto fill = [&](std::size_t at, std::size_t count, unsigned value)
    {
        if (depth == 10)
        {
            const auto word = static_cast<std::uint16_t>(value * 4u);
            for (std::size_t i = 0; i < count; ++i)
                std::memcpy(plane + (at + i) * 2u, &word, 2u);
        }
        else
            std::memset(plane + at, static_cast<int>(value), count);
    };
    for (unsigned y = y0; y < y1; ++y)
        fill(static_cast<std::size_t>(y) * pitch + x0, x1 - x0, 16u);
    for (unsigned y = y0 / 2u; y < y1 / 2u; ++y)
        fill(chroma + static_cast<std::size_t>(y) * pitch + x0, x1 - x0, 128u);
}
} // namespace

int PlaybackOsd::draw_screen(void *color, void *mask, std::size_t bytes, unsigned pitch,
                             unsigned sh, unsigned width, unsigned height, unsigned depth, int hdr,
                             bool cleared)
{
    std::lock_guard lock(mutex_);
    screen_ = true;
    if (cleared)
    {
        screen_drawn_.clear();
        screen_state_ = {};
    }
    if (!color || !mask || (depth != 8 && depth != 10) || width != 1920 || height != 1080 ||
        pitch < width || sh < height)
        return 0;
    // What shows: the menu, the channel list or the controls, the speed alone, the captions.
    ScreenState state;
    state.version = ui_version_;
    state.menu = (audio_menu_ || list_) && sheet_ && !menu_cue_.bitmaps.empty();
    state.panel = overlay_wanted_ && !sheet_ && !panel_cue_.bitmaps.empty();
    state.controls = controls_wanted_ && sheet_ && !controls_cue_.bitmaps.empty();
    state.badge = badge_wanted_ && sheet_ && !badge_cue_.bitmaps.empty();
    state.captions = captions_ && !styled_subtitles_.bitmaps.empty();
    if (screen_sleeping_)
        state.menu = state.panel = state.controls = state.badge = state.captions = false;
    if (state.captions)
        state.captions_y = (overlay_wanted_ ? (history_.available ? 660 : 740) : 1030) -
                           static_cast<int>(subtitle_panel_.height);
    state.hdr = hdr;
    state.depth = depth;
    const bool visible =
        state.menu || state.panel || state.controls || state.badge || state.captions;
    if (state == screen_state_)
        return visible ? 1 : 0;
    // Take off what was drawn, then draw what shows now: its colour, and its coverage as white.
    for (const auto &box : screen_drawn_)
    {
        clear_plane(static_cast<std::uint8_t *>(color), pitch, sh, depth, box);
        clear_plane(static_cast<std::uint8_t *>(mask), pitch, sh, depth, box);
    }
    screen_drawn_.clear();
    const auto put = [&](const iptv::SubtitleCue &cue)
    {
        iptv::SubtitleCue coverage = cue;
        for (auto &bitmap : coverage.bitmaps)
        {
            for (auto &value : bitmap.argb)
                value |= 0x00ffffffu;
            screen_drawn_.push_back(
                {bitmap.x, bitmap.y, bitmap.x + int(bitmap.width), bitmap.y + int(bitmap.height)});
        }
        composite_subtitle_bitmaps(cue, color, bytes, pitch, sh, width, height, depth,
                                   static_cast<unsigned>(hdr));
        composite_subtitle_bitmaps(coverage, mask, bytes, pitch, sh, width, height, depth, 0);
    };
    if (state.captions)
    {
        styled_subtitles_.bitmaps[0].y = state.captions_y;
        put(styled_subtitles_);
    }
    if (state.panel)
        put(panel_cue_);
    if (state.controls)
        put(controls_cue_);
    if (state.badge)
        put(badge_cue_);
    if (state.menu)
        put(menu_cue_);
    screen_state_ = state;
    return (visible ? 1 : 0) | 2;
}

} // namespace ptv
