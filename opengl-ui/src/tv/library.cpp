// ProsperoTV - Films and series: the shelves beside the live channels, and their worker.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// An Xtream account lists films and series as well as channels. Each is a
// shelf of its own with the same lists, search and filters as the live one:
// the members of Model hold the shelf on screen, and swap_shelf() trades them
// for another's. The lists, a series' episodes and a title's details come from
// one worker thread, one job at a time, so the frame loop never waits on the
// drive or the network.

#include "tv/model.hpp"

#include "iptv_store.h"
#include "iptv_xtream.h"
#include "tv/diag.hpp"
#include "tv/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ptv
{

namespace
{

// A saved list this old is downloaded again when its shelf is opened.
constexpr std::uint64_t kLibraryRefreshSeconds = UINT64_C(24) * 60u * 60u;
constexpr std::size_t kLibraryThreadStackBytes = 2u * 1024u * 1024u;
// A title's details, or a series with all its episodes: small answers.
constexpr std::size_t kInfoBytes = 4u * 1024u * 1024u;
constexpr std::size_t kDetailsKept = 48;

const char *shelf_words(Shelf shelf)
{
    return shelf == Shelf::series ? "series" : "movies";
}

iptv::http::RequestControl stop_control(const std::atomic<bool> *stop)
{
    return {[](void *context)
            {
                return static_cast<const std::atomic<bool> *>(context)->load(
                    std::memory_order_acquire);
            },
            const_cast<std::atomic<bool> *>(stop)};
}

// The account's answers for a library: each one whole, into one buffer.
struct LibraryFetch
{
    std::vector<char> buffer;
    iptv::http::RequestControl control;
    iptv::http::FetchResult last{};

    static iptv::XtreamFetchOutcome fetch(void *context, const std::string &url,
                                          std::string_view *body)
    {
        auto *self = static_cast<LibraryFetch *>(context);
        self->last = platform::fetch(url.c_str(), self->buffer.data(), self->buffer.size(),
                                     self->buffer.size() - 1u, &self->control);
        switch (self->last.status)
        {
        case iptv::http::Status::ok:
            *body = std::string_view(self->buffer.data(), self->last.bytes);
            return iptv::XtreamFetchOutcome::ok;
        case iptv::http::Status::response_too_large:
            return iptv::XtreamFetchOutcome::too_large;
        case iptv::http::Status::cancelled:
            return iptv::XtreamFetchOutcome::cancelled;
        default:
            return iptv::XtreamFetchOutcome::failed;
        }
    }
};

std::string network_words(const iptv::http::FetchResult &fetch)
{
    char text[200]{};
    iptv::http::DescribeFailure(fetch.status, fetch.http_status, fetch.native_error, nullptr, text,
                                sizeof(text));
    return text;
}

// "S01 E02" from an episode's numbers.
std::string episode_label(const iptv::ChannelView &episode)
{
    char label[32]{};
    std::snprintf(label, sizeof(label), "S%02u E%02u", static_cast<unsigned>(episode.season),
                  static_cast<unsigned>(episode.episode));
    return label;
}

} // namespace

// ---- shelves ----------------------------------------------------------------

void Model::swap_shelf(Shelf shelf)
{
    if (shelf == shelf_ || shelf >= Shelf::count)
        return;
    const auto exchange = [this](ShelfData &data)
    {
        std::swap(catalog_, data.catalog);
        std::swap(index_, data.index);
        std::swap(catalog_loaded_, data.loaded);
        std::swap(saved_unix_, data.saved_unix);
        std::swap(marks_, data.marks);
        std::swap(visible_, data.visible);
        std::swap(visible_count_, data.visible_count);
        std::swap(letter_starts_, data.letter_starts);
        std::swap(group_, data.group);
        std::swap(group_sizes_, data.group_sizes);
        std::swap(query_, data.query);
        std::swap(country_, data.country);
        std::swap(category_, data.category);
        std::swap(language_, data.language);
        std::swap(quality_, data.quality);
    };
    // The shelf on screen goes to its place; that place was empty, and the
    // new shelf's place is left empty in turn.
    exchange(shelves_[static_cast<unsigned>(shelf_)]);
    exchange(shelves_[static_cast<unsigned>(shelf)]);
    // Each shelf has its own focus and list in the view.
    view.shelf_focus[static_cast<unsigned>(shelf_)] = std::move(view.focused_channel);
    view.shelf_group[static_cast<unsigned>(shelf_)] = view.live_group;
    view.focused_channel = std::move(view.shelf_focus[static_cast<unsigned>(shelf)]);
    view.live_group = view.shelf_group[static_cast<unsigned>(shelf)];
    shelf_ = shelf;
}

void Model::set_shelf(Shelf shelf)
{
    if (shelf >= Shelf::count || shelf == shelf_)
        return;
    swap_shelf(shelf);
    if (shelf != Shelf::live)
    {
        library_check_source();
        const unsigned at = static_cast<unsigned>(shelf);
        if (has_library() && shelf_state_[at] == LibraryState::none)
        {
            shelf_state_[at] = LibraryState::loading;
            LibraryJob job;
            job.kind = LibraryJob::Kind::load;
            job.shelf = shelf;
            library_queue(std::move(job));
        }
    }
    mark_lists();
    recount_groups();
    rebuild_visible();
    diag::event("shelf %d: %u entries", static_cast<int>(shelf), channel_count());
}

bool Model::has_library() const
{
    return active_source_ == iptv::SourceKind::Xtream && xtream_ready();
}

LibraryState Model::shelf_state(Shelf shelf) const
{
    if (shelf == Shelf::live || shelf >= Shelf::count)
        return catalog_failed_ ? LibraryState::failed : LibraryState::ready;
    return has_library() ? shelf_state_[static_cast<unsigned>(shelf)] : LibraryState::none;
}

const std::string &Model::shelf_error(Shelf shelf) const
{
    static const std::string none;
    return shelf < Shelf::count ? shelf_error_[static_cast<unsigned>(shelf)] : none;
}

void Model::refresh_shelf(Shelf shelf)
{
    if (shelf == Shelf::live || shelf >= Shelf::count)
    {
        refresh();
        return;
    }
    library_check_source();
    if (!has_library())
        return;
    for (const LibraryJob &queued : library_jobs_)
        if (queued.kind == LibraryJob::Kind::download && queued.shelf == shelf)
            return;
    if (library_thread_ != nullptr && library_job_.kind == LibraryJob::Kind::download &&
        library_job_.shelf == shelf)
        return;
    const unsigned at = static_cast<unsigned>(shelf);
    const bool has_list = shelf == shelf_ ? catalog_loaded_ : shelves_[at].loaded;
    if (!has_list)
        shelf_state_[at] = LibraryState::loading;
    LibraryJob job;
    job.kind = LibraryJob::Kind::download;
    job.shelf = shelf;
    library_queue(std::move(job));
}

std::string Model::library_cache_path(Shelf shelf) const
{
    return cache_dir_ + (shelf == Shelf::series ? "/prosperotv-xtream-series.sqlite3"
                                                : "/prosperotv-xtream-movies.sqlite3");
}

// The library belongs to one account: another one (or none) and it starts over.
void Model::library_check_source()
{
    const std::uint64_t wanted = has_library() ? source_id(active_source_) : 0u;
    if (wanted == library_source_)
        return;
    library_reset();
    library_source_ = wanted;
    library_account_ = wanted != 0 ? xtream_ : iptv::XtreamCredentials{};
}

void Model::library_reset()
{
    library_stop();
    library_jobs_.clear();
    for (unsigned at = 1; at < kShelfCount; ++at)
    {
        shelves_[at] = ShelfData{};
        shelf_state_[at] = LibraryState::none;
        shelf_error_[at].clear();
    }
    if (shelf_ != Shelf::live)
    {
        catalog_ = {};
        index_.clear();
        catalog_loaded_ = false;
        saved_unix_ = 0;
        marks_.clear();
        rebuild_visible();
    }
    details_.clear();
    close_series();
}

// ---- details ------------------------------------------------------------------

const iptv::MediaDetails *Model::details(std::string_view id) const
{
    for (const DetailsEntry &entry : details_)
        if (entry.id == id)
            return entry.failed ? nullptr : &entry.details;
    return nullptr;
}

bool Model::details_failed(std::string_view id) const
{
    for (const DetailsEntry &entry : details_)
        if (entry.id == id)
            return entry.failed;
    return false;
}

void Model::want_details(unsigned catalog_index)
{
    if (catalog_index >= channel_count() || !has_library())
        return;
    const iptv::ChannelView title = catalog_[catalog_index];
    if (title.kind != iptv::MediaKind::movie && title.kind != iptv::MediaKind::series)
        return;
    for (const DetailsEntry &entry : details_)
        if (entry.id == title.id)
            return;
    if (library_thread_ != nullptr && library_job_.kind == LibraryJob::Kind::details &&
        library_job_.id == title.id)
        return;
    LibraryJob job;
    job.kind = LibraryJob::Kind::details;
    job.id = title.id;
    job.series = title.kind == iptv::MediaKind::series;
    if (job.series)
    {
        job.remote_id = title.series_id;
    }
    else
    {
        // A film's id ends in ":vod:<stream id>".
        const std::size_t vod = title.id.rfind(":vod:");
        if (vod != std::string_view::npos)
            job.remote_id = title.id.substr(vod + 5u);
    }
    if (job.remote_id.empty())
        return;
    library_queue(std::move(job));
}

// ---- one series ----------------------------------------------------------------

bool Model::open_series(unsigned catalog_index)
{
    if (catalog_index >= channel_count())
        return false;
    const iptv::ChannelView series = catalog_[catalog_index];
    if (series.kind != iptv::MediaKind::series || series.url.empty())
        return false;
    if (series_open_ == series.id && episodes_state_ != LibraryState::failed)
        return true;
    close_series();
    series_open_ = series.id;
    series_name_ = series.name;
    series_url_ = series.url;
    series_remote_ = series.series_id;
    episodes_state_ = LibraryState::loading;
    LibraryJob job;
    job.kind = LibraryJob::Kind::episodes;
    job.id = series.id;
    job.remote_id = series.series_id;
    job.url = series.url;
    job.name = series.name;
    library_queue(std::move(job));
    diag::event("series opened: \"%s\"", series_name_.c_str());
    return true;
}

void Model::close_series()
{
    series_open_.clear();
    series_name_.clear();
    series_url_.clear();
    series_remote_.clear();
    episodes_state_ = LibraryState::none;
    episodes_error_.clear();
    episodes_ = {};
    seasons_.clear();
    library_jobs_.erase(std::remove_if(library_jobs_.begin(), library_jobs_.end(),
                                       [](const LibraryJob &job)
                                       { return job.kind == LibraryJob::Kind::episodes; }),
                        library_jobs_.end());
}

void Model::retry_series()
{
    if (series_url_.empty() || episodes_state_ != LibraryState::failed)
        return;
    episodes_state_ = LibraryState::loading;
    episodes_error_.clear();
    LibraryJob job;
    job.kind = LibraryJob::Kind::episodes;
    job.id = series_open_;
    job.remote_id = series_remote_;
    job.url = series_url_;
    job.name = series_name_;
    library_queue(std::move(job));
}

bool Model::series_started() const
{
    for (const std::string &id : user_.recent_channel_ids)
        if (episodes_.Find(id) != iptv::Catalog::npos)
            return true;
    return false;
}

std::vector<unsigned> Model::season_episodes(std::uint16_t season) const
{
    std::vector<unsigned> found;
    for (unsigned index = 0; index < episodes_.size(); ++index)
        if (episodes_[index].season == season)
            found.push_back(index);
    std::stable_sort(found.begin(), found.end(), [this](unsigned left, unsigned right)
                     { return episodes_[left].episode < episodes_[right].episode; });
    return found;
}

int Model::continue_episode() const
{
    if (episodes_.empty())
        return -1;
    // Every episode in the order it is watched in.
    std::vector<unsigned> order(episodes_.size());
    for (unsigned index = 0; index < order.size(); ++index)
        order[index] = index;
    std::stable_sort(order.begin(), order.end(),
                     [this](unsigned left, unsigned right)
                     {
                         const iptv::ChannelView a = episodes_[left];
                         const iptv::ChannelView b = episodes_[right];
                         return a.season != b.season ? a.season < b.season : a.episode < b.episode;
                     });
    // The one after the newest one watched; the last stays the last.
    for (const std::string &id : user_.recent_channel_ids)
        for (std::size_t at = 0; at < order.size(); ++at)
            if (episodes_[order[at]].id == id)
                return static_cast<int>(order[at + 1u < order.size() ? at + 1u : at]);
    return static_cast<int>(order.front());
}

bool Model::play_episode(unsigned episode_index)
{
    if (episode_index >= episodes_.size())
        return false;
    const iptv::ChannelView episode = episodes_[episode_index];
    play_request_ = {};
    play_request_.channel_id = episode.id;
    const std::string label = episode_label(episode);
    play_request_.channel_name =
        series_name_.empty() ? std::string(episode.name) : series_name_ + "  " + label;
    if (!episode.url.empty())
        play_request_.urls.emplace_back(episode.url);
    for (const std::string_view alternate : episode.alternate_urls)
        if (!alternate.empty())
            play_request_.urls.emplace_back(alternate);
    play_request_.source_id = library_source_ != 0 ? library_source_ : episode.source_id;
    play_requested_ = !play_request_.urls.empty();
    diag::event("episode asked: \"%s\"", play_request_.channel_name.c_str());
    if (!play_requested_)
        return false;
    const std::vector<std::string> previous = user_.recent_channel_ids;
    (void)iptv::AddRecentChannel(&user_, episode.id);
    if (iptv::SaveUserState(path("iptv-favorites-v1.bin"), path("iptv-history-v1.bin"), user_) !=
        iptv::UserStateStatus::ok)
        user_.recent_channel_ids = previous;
    return true;
}

// ---- the worker ------------------------------------------------------------------

void Model::library_queue(LibraryJob job)
{
    // Only the newest wish for details counts: the focus has moved on.
    if (job.kind == LibraryJob::Kind::details)
        library_jobs_.erase(std::remove_if(library_jobs_.begin(), library_jobs_.end(),
                                           [](const LibraryJob &queued)
                                           { return queued.kind == LibraryJob::Kind::details; }),
                            library_jobs_.end());
    for (const LibraryJob &queued : library_jobs_)
        if (queued.kind == job.kind && queued.shelf == job.shelf && queued.id == job.id)
            return;
    // A list comes before the details of what is in it.
    if (job.kind == LibraryJob::Kind::details)
        library_jobs_.push_back(std::move(job));
    else
        library_jobs_.insert(std::find_if(library_jobs_.begin(), library_jobs_.end(),
                                          [](const LibraryJob &queued)
                                          { return queued.kind == LibraryJob::Kind::details; }),
                             std::move(job));
    // The worker takes it at the next poll.
}

void Model::library_poll()
{
    if (library_thread_ != nullptr)
    {
        if (!library_done_.load(std::memory_order_acquire))
            return;
        if (platform::thread_join(library_thread_) != 0)
            (void)platform::thread_detach(library_thread_);
        library_thread_ = nullptr;
        finish_library_job();
    }
    if (library_jobs_.empty() || library_stop_.load(std::memory_order_acquire))
        return;
    library_job_ = std::move(library_jobs_.front());
    library_jobs_.pop_front();
    library_result_ = {};
    library_done_.store(false, std::memory_order_relaxed);
    library_thread_ = platform::thread_start(&Model::library_entry, this, kLibraryThreadStackBytes,
                                             "iptv-library");
    if (library_thread_ == nullptr)
    {
        library_result_.error = "The download could not start.";
        finish_library_job();
    }
}

void Model::library_stop()
{
    library_stop_.store(true, std::memory_order_release);
    while (library_thread_ != nullptr && !library_done_.load(std::memory_order_acquire))
    {
        platform::network_cancel();
        platform::sleep_ms(10);
    }
    if (library_thread_ != nullptr)
    {
        if (platform::thread_join(library_thread_) != 0)
            (void)platform::thread_detach(library_thread_);
        library_thread_ = nullptr;
        // What it brought is not taken: a list half read stays unread, and
        // its shelf is asked for again when it is next opened.
        const unsigned at = static_cast<unsigned>(library_job_.shelf);
        if ((library_job_.kind == LibraryJob::Kind::load ||
             library_job_.kind == LibraryJob::Kind::download) &&
            shelf_state_[at] == LibraryState::loading)
            shelf_state_[at] = LibraryState::none;
        if (library_job_.kind == LibraryJob::Kind::episodes && library_job_.id == series_open_ &&
            episodes_state_ == LibraryState::loading)
            library_jobs_.push_front(library_job_);
    }
    for (const LibraryJob &queued : library_jobs_)
        if (queued.kind == LibraryJob::Kind::load || queued.kind == LibraryJob::Kind::download)
            if (shelf_state_[static_cast<unsigned>(queued.shelf)] == LibraryState::loading)
                shelf_state_[static_cast<unsigned>(queued.shelf)] = LibraryState::none;
    library_jobs_.erase(std::remove_if(library_jobs_.begin(), library_jobs_.end(),
                                       [](const LibraryJob &queued)
                                       { return queued.kind != LibraryJob::Kind::episodes; }),
                        library_jobs_.end());
    library_result_ = {};
    library_stop_.store(false, std::memory_order_release);
}

void *Model::library_entry(void *self)
{
    static_cast<Model *>(self)->run_library();
    return nullptr;
}

void Model::run_library()
{
    run_library_job(library_job_, &library_result_);
    library_done_.store(true, std::memory_order_release);
}

// Runs on the worker. It reads library_job_, library_account_ and
// library_source_, and writes library_result_; the frame loop leaves them
// alone until library_done_ is set.
void Model::run_library_job(const LibraryJob &job, LibraryResult *result)
{
    const iptv::StoreLimits limits = iptv::LibraryStoreLimits();
    if (job.kind == LibraryJob::Kind::load)
    {
        iptv::StoreReport report;
        const iptv::StoreStatus status =
            iptv::LoadCatalog(library_cache_path(job.shelf), &result->catalog, limits, &report);
        result->ok = status == iptv::StoreStatus::ok &&
                     result->catalog.source_id == library_source_ && !result->catalog.empty();
        if (!result->ok)
        {
            result->catalog = {};
            result->stale = true;
            return;
        }
        result->saved_unix = report.saved_unix;
        const std::uint64_t now = platform::unix_time();
        result->stale = report.saved_unix == 0 || now < report.saved_unix ||
                        now - report.saved_unix >= kLibraryRefreshSeconds;
        result->index.build(result->catalog);
        return;
    }

    const iptv::http::Status network = platform::network_init();
    if (network != iptv::http::Status::ok)
    {
        result->error = "The network did not start.";
        return;
    }
    LibraryFetch fetch;
    fetch.control = stop_control(&library_stop_);
    const iptv::XtreamFetcher fetcher{&LibraryFetch::fetch, &fetch};
    switch (job.kind)
    {
    case LibraryJob::Kind::download:
    {
        fetch.buffer.resize(iptv::kMaxXtreamLibraryResponseBytes + 1u);
        iptv::XtreamLibraryReport report;
        const iptv::XtreamStatus status =
            iptv::FetchXtreamLibrary(library_account_, library_source_,
                                     job.shelf == Shelf::series ? iptv::XtreamLibraryKind::series
                                                                : iptv::XtreamLibraryKind::movies,
                                     fetcher, &result->catalog, &report);
        result->ok = status == iptv::XtreamStatus::ok && !result->catalog.empty();
        if (result->ok)
        {
            result->saved = iptv::SaveCatalog(library_cache_path(job.shelf), result->catalog,
                                              limits) == iptv::StoreStatus::ok;
            result->saved_unix = result->saved ? platform::unix_time() : 0;
            result->index.build(result->catalog);
        }
        else
        {
            result->catalog = {};
            result->error = status == iptv::XtreamStatus::fetch_failed
                                ? network_words(fetch.last)
                                : std::string(iptv::XtreamStatusDescription(status)) + ".";
        }
        diag::event("library download: shelf=%d status=%d entries=%zu requests=%u saved=%d",
                    static_cast<int>(job.shelf), static_cast<int>(status), result->catalog.size(),
                    report.requests, result->saved ? 1 : 0);
        break;
    }
    case LibraryJob::Kind::episodes:
    {
        fetch.buffer.resize(kInfoBytes + 1u);
        std::string_view body;
        if (LibraryFetch::fetch(&fetch, job.url, &body) == iptv::XtreamFetchOutcome::ok)
        {
            const iptv::XtreamStatus status = iptv::ParseXtreamSeriesInfo(
                body, library_account_, job.remote_id, job.name, library_source_, &result->catalog);
            result->ok = status == iptv::XtreamStatus::ok;
            if (!result->ok)
                result->error = std::string(iptv::XtreamStatusDescription(status)) + ".";
            (void)iptv::ParseMediaInfo(body, &result->details);
        }
        else
        {
            result->error = network_words(fetch.last);
        }
        break;
    }
    case LibraryJob::Kind::details:
    {
        fetch.buffer.resize(kInfoBytes + 1u);
        std::string url;
        std::string_view body;
        if (!iptv::BuildXtreamApiUrlWithParam(
                library_account_, job.series ? "get_series_info" : "get_vod_info",
                job.series ? "series_id" : "vod_id", job.remote_id, &url))
            result->error = "This title has no details.";
        else if (LibraryFetch::fetch(&fetch, url, &body) == iptv::XtreamFetchOutcome::ok)
            result->ok = iptv::ParseMediaInfo(body, &result->details) == iptv::XtreamStatus::ok;
        else
            result->error = network_words(fetch.last);
        break;
    }
    case LibraryJob::Kind::load:
        break;
    }
    platform::network_shutdown();
}

// On the frame loop: what the worker brought goes where it belongs.
void Model::finish_library_job()
{
    LibraryJob job = std::move(library_job_);
    LibraryResult result = std::move(library_result_);
    library_job_ = {};
    library_result_ = {};
    const unsigned at = static_cast<unsigned>(job.shelf);
    switch (job.kind)
    {
    case LibraryJob::Kind::load:
    case LibraryJob::Kind::download:
    {
        const bool shown = job.shelf == shelf_;
        const bool had_list = shown ? catalog_loaded_ : shelves_[at].loaded;
        if (result.ok)
        {
            if (shown)
            {
                catalog_ = std::move(result.catalog);
                index_ = std::move(result.index);
                catalog_loaded_ = true;
                saved_unix_ = result.saved_unix;
                adopt_catalog();
            }
            else
            {
                ShelfData &data = shelves_[at];
                data.catalog = std::move(result.catalog);
                data.index = std::move(result.index);
                data.loaded = true;
                data.saved_unix = result.saved_unix;
                // Its lists are worked out when it is shown.
                data.marks.clear();
                data.visible.clear();
                data.visible_count = 0;
            }
            shelf_state_[at] = LibraryState::ready;
            shelf_error_[at].clear();
            if (job.kind == LibraryJob::Kind::download)
            {
                const std::size_t count = shown ? catalog_.size() : shelves_[at].catalog.size();
                notify(result.saved ? Level::ready : Level::warning,
                       job.shelf == Shelf::series ? "Series updated" : "Movies updated",
                       group_digits(static_cast<unsigned>(count)) + " " + shelf_words(job.shelf) +
                           (result.saved ? "" : ", not saved on the console"));
            }
        }
        else if (job.kind == LibraryJob::Kind::download)
        {
            if (had_list)
            {
                shelf_state_[at] = LibraryState::ready;
                notify(Level::warning,
                       std::string("The ") + shelf_words(job.shelf) + " could not be updated",
                       result.error);
            }
            else
            {
                shelf_state_[at] = LibraryState::failed;
                shelf_error_[at] = result.error.empty() ? "The download failed." : result.error;
            }
        }
        if (job.kind == LibraryJob::Kind::load && (result.stale || !result.ok))
            refresh_shelf(job.shelf);
        break;
    }
    case LibraryJob::Kind::episodes:
        if (job.id != series_open_)
            break;
        if (result.ok)
        {
            episodes_ = std::move(result.catalog);
            seasons_.clear();
            for (const iptv::ChannelView episode : episodes_)
                if (std::find(seasons_.begin(), seasons_.end(), episode.season) == seasons_.end())
                    seasons_.push_back(episode.season);
            std::sort(seasons_.begin(), seasons_.end());
            episodes_state_ = LibraryState::ready;
            episodes_error_.clear();
        }
        else
        {
            episodes_state_ = LibraryState::failed;
            episodes_error_ =
                result.error.empty() ? "The episodes could not be read." : result.error;
        }
        if (!result.details.plot.empty() || !result.details.genre.empty())
        {
            details_.push_front({job.id, std::move(result.details), false});
            if (details_.size() > kDetailsKept)
                details_.pop_back();
        }
        break;
    case LibraryJob::Kind::details:
        details_.push_front({job.id, std::move(result.details), !result.ok});
        if (details_.size() > kDetailsKept)
            details_.pop_back();
        break;
    }
}

} // namespace ptv
