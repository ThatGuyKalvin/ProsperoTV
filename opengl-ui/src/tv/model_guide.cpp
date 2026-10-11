// ProsperoTV - Guide downloads share the catalog's network worker time.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/i18n.hpp"
#include "tv/model.hpp"
#include "tv/usb_playlist.hpp"
#include "tv/platform.hpp"
#include "tv/local_tv.hpp"
#include "iptv_ime.h"

namespace ptv
{
std::string Model::guide_path() const
{
    return cache_path(active_source_) + ".guide.sqlite3";
}
std::uint64_t Model::guide_source_id() const
{
    std::string config = library_.guide_url(selected_source_id_);
    const auto mapping = library_.guide_mapping(selected_source_id_);
    for (const auto &[channel, guide_id] : mapping)
        config += "\n" + channel + "\n" + guide_id;
    return config.empty()
               ? catalog_.source_id
               : iptv::CustomSourceId(std::to_string(catalog_.source_id) + "\n" + config);
}

void Model::stop_guide()
{
    if (!guide_thread_)
        return;
    const bool completed = guide_done_.load(std::memory_order_acquire);
    guide_stop_.store(true, std::memory_order_release);
    while (!guide_done_.load(std::memory_order_acquire))
    {
        platform::network_cancel();
        platform::sleep_ms(10);
    }
    if (platform::thread_join(guide_thread_) != 0)
        platform::thread_detach(guide_thread_);
    guide_thread_ = nullptr;
    if (completed && guide_ok_)
    {
        guide_ = std::move(pending_guide_);
        rebuild_visible();
    }
    if (!completed)
        next_guide_check_ = 0;
    pending_guide_ = {};
}

void Model::poll_guide()
{
    const auto now = platform::unix_time();
    if (guide_thread_ && guide_done_.load(std::memory_order_acquire))
    {
        if (platform::thread_join(guide_thread_) != 0)
            platform::thread_detach(guide_thread_);
        guide_thread_ = nullptr;
        if (guide_ok_)
        {
            guide_ = std::move(pending_guide_);
            guide_status_ =
                guide_.count() == 0 ? tr("No programmes matched this source's channels.")
                : guide_.truncated ? tr("Guide loaded; this provider exceeded the programme limit.")
                : guide_saved_     ? tr("Guide up to date")
                                   : tr("Guide loaded, but could not be saved.");
            rebuild_visible();
        }
        else
            guide_status_ = guide_.count()
                                ? tr("Guide update failed. Showing the saved programmes.")
                                : tr("The programme guide could not be downloaded.");
        pending_guide_ = {};
    }
    if (now / 60 != guide_minute_)
    {
        guide_minute_ = now / 60;
        if (guide_.count())
        {
            if (!query_.empty())
                rebuild_visible();
            else
                ++revision_; // Refresh Home and guide labels once per minute.
        }
    }
    if (!refreshing() && !guide_thread_ && has_catalog() && now >= next_guide_check_)
    {
        next_guide_check_ = now + 3600;
        if (refresh_due(schedule_, guide_.saved_unix, now))
            refresh_guide();
    }
}

void Model::refresh_guide()
{
    if (home_waiting_ || refreshing() || vod_.busy() || vod_.requested() || search_busy() ||
        search_pending() || guide_thread_ || !has_catalog())
        return;
    guide_urls_ = catalog_.guide_urls;
    if (active_source_ == iptv::SourceKind::Xtream)
    {
        std::string url;
        if (iptv::BuildXtreamGuideUrl(xtream_, &url))
            guide_urls_ = {std::move(url)};
    }
    const auto override_url = library_.guide_url(selected_source_id_);
    if (!override_url.empty())
        guide_urls_ = {override_url};
    guide_mapping_ = library_.guide_mapping(selected_source_id_);
    next_guide_check_ = platform::unix_time() + 3600;
    if (guide_urls_.empty())
    {
        guide_status_ = tr("This source has not supplied a programme guide.");
        return;
    }
    guide_file_ = guide_path();
    guide_http_ =
        saved_source(selected_source_id_) ? *saved_source(selected_source_id_) : SavedSource{};
    guide_expected_source_ = guide_source_id();
    guide_authorization_ =
        active_source_ == iptv::SourceKind::Tvheadend ? local_tv_authorization(local_source_) : "";
    guide_origin_ = active_source_ == iptv::SourceKind::Tvheadend ? local_source_.url : "";
    guide_ok_ = guide_saved_ = false;
    guide_stop_.store(false, std::memory_order_relaxed);
    guide_done_.store(false, std::memory_order_relaxed);
    guide_thread_ =
        platform::thread_start(&Model::guide_entry, this, 2u * 1024u * 1024u, "iptv-guide");
    guide_status_ = guide_thread_ ? tr("Downloading the programme guide")
                                  : tr("The guide download could not start.");
}
void *Model::guide_entry(void *self)
{
    static_cast<Model *>(self)->run_guide();
    return nullptr;
}
void Model::run_guide()
{
    pending_guide_ = {};
    pending_guide_.source_id = catalog_.source_id;
    pending_guide_.saved_unix = platform::unix_time();
    const iptv::http::RequestControl control{
        [](void *self)
        { return static_cast<Model *>(self)->guide_stop_.load(std::memory_order_acquire); }, this};
    if (platform::network_init() == iptv::http::Status::ok)
    {
        bool all = true;
        for (const auto &url : guide_urls_)
        {
            std::string canonical;
            if (!iptv::CanonicalizeStreamUrl(url, &canonical))
            {
                all = false;
                break;
            }
            XmltvReader reader(catalog_, static_cast<std::int64_t>(platform::unix_time()),
                               pending_guide_, guide_mapping_);
            const iptv::http::ListSink sink{
                [](void *reader, const char *data, std::size_t bytes)
                { return static_cast<XmltvReader *>(reader)->feed({data, bytes}); }, &reader};
            iptv::http::RequestHeaders headers{guide_http_.user_agent.c_str(),
                                               guide_http_.referrer.c_str()};
            headers.extra_headers = guide_http_.extra_headers.c_str();
            headers.authorization = guide_authorization_.c_str();
            headers.credential_origin =
                guide_origin_.empty()
                    ? (usb_playlist_path(guide_http_.url) ? nullptr : guide_http_.url.c_str())
                    : guide_origin_.c_str();
            const auto scoped =
                iptv::http::HeadersForUrl(canonical.c_str(), canonical.c_str(), headers);
            const auto response = platform::fetch_list(canonical.c_str(), sink,
                                                       512u * 1024u * 1024u, &control, &scoped);
            if (response.status != iptv::http::Status::ok || !reader.finish() ||
                guide_stop_.load(std::memory_order_acquire))
            {
                all = false;
                break;
            }
        }
        guide_ok_ = all;
        if (guide_ok_)
        {
            pending_guide_.source_id = guide_expected_source_;
            guide_saved_ = pending_guide_.save(guide_file_);
        }
        platform::network_shutdown();
    }
    guide_done_.store(true, std::memory_order_release);
}

bool Model::play_programme(unsigned index, const Programme &programme)
{
    if (index >= channel_count() || pin_prompt() || !content_allowed(catalog_[index]))
        return false;
    const auto now = static_cast<std::int64_t>(platform::unix_time());
    if (programme.start <= now && programme.end > now)
        return play(index);
    const auto channel = catalog_[index];
    const auto url = catchup_url(channel, programme, now);
    if (url.empty())
    {
        notify(Level::warning, programme.start > now
                                   ? tr("This programme has not started")
                                   : tr("Catch-up is not available for this programme"));
        return false;
    }
    play_request_ = {};
    sleep_timer.wake();
    play_request_.channel_id = channel.id;
    play_request_.channel_name = std::string(channel.name) + " - " + programme.title;
    play_request_.source_id = channel.source_id;
    play_request_.urls = {url};
    play_request_.user_agent = channel.http_user_agent;
    play_request_.referrer = channel.http_referrer;
    apply_source_http(play_request_);
    if (active_source_ == iptv::SourceKind::Tvheadend)
    {
        play_request_.authorization = local_tv_authorization(local_source_);
        play_request_.credential_origin = local_source_.url;
    }
    play_request_.record_channel_result = false;
    play_requested_ = true;
    return true;
}
} // namespace ptv

namespace ptv
{
void Model::reload_guide_configuration()
{
    guide_ = {};
    (void)guide_.load(guide_path(), guide_source_id());
    next_guide_check_ = 0;
    rebuild_visible();
    refresh_guide();
}
bool Model::set_guide_url(std::int64_t source, std::string_view url)
{
    if (!require_parent() || !saved_source(source) || refreshing())
        return false;
    if (source == selected_source_id_)
        stop_guide();
    if (!library_.set_guide_url(source, url))
        return false;
    if (source == selected_source_id_)
        reload_guide_configuration();
    ++revision_;
    return true;
}
std::string Model::guide_match(std::string_view channel) const
{
    const auto mapping = library_.guide_mapping(selected_source_id_);
    const auto found = mapping.find(std::string(channel));
    return found == mapping.end() ? std::string() : found->second;
}
bool Model::set_guide_mapping(std::string_view channel, std::string_view guide_id)
{
    const auto index = catalog_.Find(channel);
    if (!require_parent() || refreshing() || index == iptv::Catalog::npos ||
        catalog_[index].media_kind != iptv::MediaKind::live || position_of(channel) < 0)
        return false;
    stop_guide();
    if (!library_.set_guide_mapping(selected_source_id_, channel, guide_id))
        return false;
    reload_guide_configuration();
    ++revision_;
    return true;
}
void Model::edit_guide_url(std::int64_t source)
{
    if (!require_parent() || !keyboard_ready_ || refreshing() || !saved_source(source))
        return;
    guide_edit_source_ = source;
    const auto current = library_.guide_url(source);
    iptv_ime_request_prompt(current.c_str(), tr("XMLTV URL (empty uses provider guide)"),
                            "https://example.com/guide.xml", IPTV_IME_BUFFER_CHARACTERS,
                            &Model::on_guide_url, this);
}
void Model::on_guide_url(const char *text, void *self)
{
    if (!self || !text)
        return;
    auto &model = *static_cast<Model *>(self);
    if (!model.set_guide_url(model.guide_edit_source_, text))
        model.notify(Level::error, tr("The guide address could not be saved"),
                     tr("Use an HTTP or HTTPS XMLTV address, or leave it empty."));
}
void Model::edit_guide_mapping(unsigned channel)
{
    if (!require_parent() || !keyboard_ready_ || channel >= channel_count() ||
        position_of(catalog_[channel].id) < 0)
        return;
    guide_edit_source_ = selected_source_id_;
    guide_edit_channel_ = catalog_[channel].id;
    const auto current = guide_match(guide_edit_channel_);
    iptv_ime_request_prompt(current.c_str(), tr("XMLTV channel ID (empty matches automatically)"),
                            "channel.id", 256, &Model::on_guide_mapping, this);
}
void Model::on_guide_mapping(const char *text, void *self)
{
    if (!self || !text)
        return;
    auto &model = *static_cast<Model *>(self);
    if (model.guide_edit_source_ != model.selected_source_id_ ||
        !model.set_guide_mapping(model.guide_edit_channel_, text))
        model.notify(Level::error, tr("The guide match could not be saved"));
}
} // namespace ptv
