// ProsperoTV - One query across active and saved source catalogs.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/i18n.hpp"
#include "tv/category_path.hpp"
#include "tv/local_tv.hpp"
#include "tv/platform.hpp"
#include "iptv_store.h"
#include <algorithm>

namespace ptv
{
void Model::begin_search()
{
    if (searching_)
        return;
    searching_ = true;
    // A search needs the lists themselves.
    search_movies_.configure_like(vod_);
    search_shows_.configure_like(vod_);
    search_movies_.select(VodKind::movies);
    search_shows_.select(VodKind::series);
}
void Model::end_search()
{
    searching_ = false;
    stop_discovery();
    if (vod_overview_)
    {
        // Back to the counts alone.
        search_movies_.select_counts(VodKind::movies);
        search_shows_.select_counts(VodKind::series);
        return;
    }
    search_movies_.stop();
    search_shows_.stop();
}
void Model::set_vod_overview(bool active, bool force)
{
    if (active == vod_overview_ && !force)
        return;
    vod_overview_ = active;
    if (!active)
    {
        if (!searching_)
        {
            search_movies_.stop();
            search_shows_.stop();
        }
        return;
    }
    if (!searching_ || force)
    {
        // The browser may have refreshed a shared cache: read the summaries again.
        search_movies_.configure_like(vod_, true);
        search_shows_.configure_like(vod_, true);
        if (searching_)
        {
            search_movies_.select(VodKind::movies, {}, force);
            search_shows_.select(VodKind::series, {}, force);
        }
        else
        {
            search_movies_.select_counts(VodKind::movies, force);
            search_shows_.select_counts(VodKind::series, force);
        }
    }
}
std::optional<unsigned> Model::vod_count(VodKind kind) const
{
    if (!vod_.available())
        return 0;
    if (active_source_ == iptv::SourceKind::Custom && !has_catalog())
        return std::nullopt;
    const auto &library = kind == VodKind::movies ? search_movies_ : search_shows_;
    if (library.summary().valid)
    {
        // Hidden categories and the parental rules apply per category.
        unsigned count = 0;
        for (const auto &category : library.summary().categories)
        {
            if (category_hidden(category.name))
                continue;
            iptv::ChannelView view{};
            view.group_title = category.name;
            view.media_kind =
                kind == VodKind::movies ? iptv::MediaKind::movie : iptv::MediaKind::episode;
            if (content_allowed(view))
                count += category.titles - category.adult;
            view.adult = true;
            if (category.adult && content_allowed(view))
                count += category.adult;
        }
        return count;
    }
    if (!library.loaded())
        return std::nullopt;
    unsigned count = 0;
    for (const auto item : library.catalog())
        if (search_vod_allowed(item))
            ++count;
    return count;
}
void Model::refresh_counts()
{
    counts_ = {};
    for (const auto channel : catalog_)
        if (channel.media_kind == iptv::MediaKind::live)
            ++counts_.channels;
    if (!vod_.available())
        return;
    if (active_source_ == iptv::SourceKind::Custom)
    {
        unsigned movies = 0, shows = 0;
        VodLibrary::playlist_counts(catalog_, &movies, &shows);
        counts_.movies = movies;
        counts_.shows = shows;
        return;
    }
    // Xtream lists are counted from their saved files, so a long list is never read for it.
    const auto saved = [&](VodKind kind) -> std::optional<unsigned>
    {
        iptv::StoreReport report;
        std::uint64_t source = 0;
        if (iptv::InspectCatalog(vod_.cache_file(kind), &report, &source) !=
                iptv::StoreStatus::ok ||
            report.catalog_version < 5 || source != iptv::XtreamSourceId(xtream_))
            return std::nullopt;
        return static_cast<unsigned>(report.records);
    };
    counts_.movies = saved(VodKind::movies);
    counts_.shows = saved(VodKind::series);
}
bool Model::channel_visible(unsigned index) const
{
    return index < catalog_.size() && index < marks_.size() && !(marks_[index] & 4u) &&
           content_allowed(channel(index));
}
bool Model::search_vod_allowed(const iptv::ChannelView &item) const
{
    if (!content_allowed(item) || category_hidden(item.group_title))
        return false;
    for (const auto group : item.alternate_group_titles)
        if (category_hidden(group))
            return false;
    return true;
}
std::string Model::search_status() const
{
    const auto state = [](const VodLibrary &library)
    {
        return library.busy() || library.requested() ? std::string(tr("Loading"))
               : !library.available() ? std::string(tr("Unavailable for this source"))
                                      : library.status();
    };
    const auto saved = discovery_thread_    ? std::string(tr("Searching saved sources…"))
                       : discovery_missing_ ? std::to_string(discovery_missing_) + " " +
                                                  tr("saved lists unavailable; refresh in Sources.")
                                            : std::string(tr("Saved sources searched offline"));
    return saved + " / " + std::string(tr("Movies")) + ": " + state(search_movies_) + " / " +
           tr("TV shows") + ": " + state(search_shows_) + " / " + tr("Guide") + ": " +
           (guide_refreshing() ? tr("Loading")
            : guide_.count()   ? tr("Saved programmes")
                               : tr("No guide available"));
}

namespace
{
std::uint64_t saved_fingerprint(const SavedSource &source)
{
    switch (source.kind)
    {
    case 1:
        return iptv::CustomSourceId(source.url);
    case 2:
        return iptv::XtreamSourceId({source.url, source.username, source.password});
    case 3:
        return portal_source_id(
            {source.url, source.mac, source.user_agent, source.referrer, source.extra_headers});
    case 4:
    case 5:
        return local_tv_source_id(source);
    default:
        return 0;
    }
}
} // namespace

void Model::stop_discovery()
{
    discovery_stop_.store(true, std::memory_order_release);
    if (discovery_thread_)
    {
        platform::thread_join(discovery_thread_);
        discovery_thread_ = nullptr;
    }
    discovery_matches_.clear();
    discovery_pending_.clear();
    discovery_sources_.clear();
    discovery_query_.clear();
    discovery_requested_.clear();
    discovery_missing_ = 0;
    discovery_truncated_ = false;
    ++discovery_revision_;
}

void Model::poll_discovery()
{
    if (!searching_)
        return;
    const auto identity = query_ + "\n" + country_ + "\n" + category_ + "\n" + language_ + "\n" +
                          std::to_string(quality_);
    if (discovery_requested_ != identity)
    {
        discovery_requested_ = identity;
        discovery_query_.clear();
        discovery_stop_.store(true, std::memory_order_release);
        discovery_matches_.clear();
        discovery_missing_ = 0;
        discovery_truncated_ = false;
        discovery_due_ = platform::monotonic_us() + 150000;
        ++discovery_revision_;
    }
    if (discovery_thread_ && discovery_done_.load(std::memory_order_acquire))
    {
        platform::thread_join(discovery_thread_);
        discovery_thread_ = nullptr;
        if (!discovery_stop_.load() && discovery_running_ == query_)
        {
            discovery_matches_ = std::move(discovery_pending_);
            discovery_query_ = discovery_running_;
            discovery_missing_ = discovery_pending_missing_;
            discovery_truncated_ = discovery_pending_truncated_;
            ++discovery_revision_;
        }
    }
    if (discovery_thread_ || query_.empty() || discovery_query_ == query_ ||
        platform::monotonic_us() < discovery_due_)
        return;
    discovery_sources_.clear();
    for (const auto &source : sources_)
    {
        if (source.id == selected_source_id_ || source.url.empty() || source.kind < 1 ||
            source.kind > 5)
            continue;
        DiscoverySource saved;
        saved.id = source.id;
        saved.kind = source.kind;
        saved.name = source.name;
        saved.fingerprint = saved_fingerprint(source);
        saved.hidden = library_.hidden_categories(saved.fingerprint);
        saved.hidden_channels = library_.hidden_channels(saved.fingerprint);
        saved.rules = library_.category_rules(saved.fingerprint);
        saved.names = library_.channel_names(saved.fingerprint);
        saved.valid = parental_.valid() && library_.ready();
        saved.parental = parental_.enabled();
        saved.kids = parental_.kids_only();
        saved.unlocked = parental_.unlocked();
        saved.cache = cache_dir_ + (source.id > 3 ? "/prosperotv-source-" +
                                                        std::to_string(source.id) + ".sqlite3"
                                    : source.kind == 1 ? "/prosperotv-custom-catalog.sqlite3"
                                                       : "/prosperotv-xtream-catalog.sqlite3");
        auto config = library_.guide_url(source.id);
        for (const auto &[channel, guide] : library_.guide_mapping(source.id))
            config += "\n" + channel + "\n" + guide;
        saved.guide_shift = library_.guide_timing(source.id).first;
        saved.guide_fingerprint =
            config.empty()
                ? saved.fingerprint
                : iptv::CustomSourceId(std::to_string(saved.fingerprint) + "\n" + config);
        discovery_sources_.push_back(std::move(saved));
    }
    discovery_running_ = query_;
    discovery_country_ = country_;
    discovery_category_ = category_;
    discovery_language_ = language_;
    discovery_quality_ = quality_;
    discovery_pending_.clear();
    discovery_pending_missing_ = 0;
    discovery_pending_truncated_ = false;
    discovery_stop_.store(false, std::memory_order_release);
    discovery_done_.store(false, std::memory_order_release);
    discovery_thread_ = platform::thread_start(discover_saved, this, 512 * 1024, "ptv-search");
    if (!discovery_thread_)
    {
        discovery_query_ = query_;
        discovery_missing_ = static_cast<unsigned>(discovery_sources_.size());
        ++discovery_revision_;
    }
}

void *Model::discover_saved(void *argument)
{
    auto &model = *static_cast<Model *>(argument);
    unsigned counts[4]{};
    const auto allowed = [](const DiscoverySource &source, const iptv::ChannelView &item)
    {
        bool visible = source.valid && !source.hidden_channels.contains(std::string(item.id));
        bool adult = item.adult, kids = false;
        const auto category = [&](std::string_view name)
        {
            adult |= adult_category(name);
            kids |= kids_category(name);
            for (name = category_trim(name); !name.empty(); name = category_parent(name))
            {
                visible &= !source.hidden.contains(std::string(name));
                const auto found = source.rules.find(std::string(name));
                if (found != source.rules.end())
                {
                    adult |= found->second == 1;
                    kids |= found->second == 2;
                }
            }
        };
        category(item.group_title);
        for (const auto group : item.alternate_group_titles)
            category(group);
        return visible &&
               (!source.parental || (source.kids ? kids && !adult : !adult || source.unlocked));
    };
    const auto add = [&](const DiscoverySource &source, SearchResult::Kind kind,
                         const iptv::ChannelView &item, std::string title, std::string detail,
                         std::int64_t start = 0)
    {
        if (kind == SearchResult::Kind::channel)
            if (const auto renamed = source.names.find(std::string(item.id));
                renamed != source.names.end())
                title = renamed->second;
        if (kind == SearchResult::Kind::channel &&
            ((model.discovery_quality_ != kQualityAny &&
              model.discovery_quality_ != quality_of(item)) ||
             !matches_channel_filters(item, model.discovery_country_, model.discovery_category_,
                                      model.discovery_language_)))
            return;
        if (!allowed(source, item) || !contains_nocase(title, model.discovery_running_))
            return;
        if (counts[static_cast<unsigned>(kind)]++ >= 250)
        {
            model.discovery_pending_truncated_ = true;
            return;
        }
        model.discovery_pending_.push_back(
            {{kind, std::string(item.id), std::move(title), std::move(detail), start,
              source.fingerprint, source.id, source.name},
             item.Copy()});
    };
    for (const auto &source : model.discovery_sources_)
    {
        if (model.discovery_stop_.load(std::memory_order_acquire))
            break;
        iptv::Catalog catalog;
        iptv::StoreLimits limits;
        limits.allow_empty = true;
        limits.quick_check = false;
        iptv::StoreReport report;
        const auto read = [&](const std::string &file)
        {
            return iptv::LoadCatalog(file, &catalog, limits, &report) == iptv::StoreStatus::ok &&
                   report.catalog_version >= 5 && catalog.source_id == source.fingerprint;
        };
        const bool live_saved = read(source.cache);
        if (!live_saved)
        {
            ++model.discovery_pending_missing_;
            catalog.Clear();
        }
        Guide guide;
        guide.load(source.cache + ".guide.sqlite3", source.guide_fingerprint);
        (void)guide.set_time_shift(source.guide_shift);
        for (std::size_t i = 0; i < catalog.size() && !model.discovery_stop_.load(); ++i)
        {
            const auto item = catalog[i];
            if (item.media_kind != iptv::MediaKind::live)
                continue;
            add(source, SearchResult::Kind::channel, item, std::string(item.name),
                std::string(item.group_title));
            for (const auto &programme : guide.programmes(item.id))
            {
                if (model.discovery_stop_.load())
                    break;
                add(source, SearchResult::Kind::programme, item, programme.title,
                    std::string(item.name) + " / " + programme_time(programme.start, true),
                    programme.start);
            }
        }
        guide = {};
        if (source.kind == 1)
        {
            VodLibrary library;
            library.configure_playlist(&catalog);
            for (const auto kind : {VodKind::movies, VodKind::series})
            {
                if (model.discovery_stop_.load())
                    break;
                library.select(kind);
                for (const auto item : library.catalog())
                {
                    if (model.discovery_stop_.load())
                        break;
                    add(source,
                        kind == VodKind::movies ? SearchResult::Kind::movie
                                                : SearchResult::Kind::show,
                        item, std::string(item.name), std::string(item.group_title));
                }
            }
        }
        else if (source.kind == 2)
        {
            catalog.Clear();
            for (int kind = 0; kind < 2 && !model.discovery_stop_.load(); ++kind)
            {
                const auto file = source.cache + ".vod-" + std::to_string(kind) + "-" +
                                  std::to_string(iptv::CustomSourceId({})) + ".sqlite3";
                if (!read(file))
                {
                    ++model.discovery_pending_missing_;
                    continue;
                }
                for (const auto item : catalog)
                {
                    if (model.discovery_stop_.load())
                        break;
                    add(source, kind == 0 ? SearchResult::Kind::movie : SearchResult::Kind::show,
                        item, std::string(item.name), std::string(item.group_title));
                }
            }
        }
    }
    model.discovery_done_.store(true, std::memory_order_release);
    return nullptr;
}

Model::DiscoverySource Model::discovery_policy(std::uint64_t source) const
{
    DiscoverySource policy;
    policy.hidden = library_.hidden_categories(source);
    policy.hidden_channels = library_.hidden_channels(source);
    policy.rules = library_.category_rules(source);
    return policy;
}

bool Model::discovery_allowed(const DiscoveryMatch &match, const DiscoverySource *policy) const
{
    const auto *source = saved_source(match.result.saved_source_id);
    if (!source || saved_fingerprint(*source) != match.result.source_id || !parental_.valid() ||
        !library_.ready())
        return false;
    DiscoverySource current;
    if (!policy)
    {
        current = discovery_policy(match.result.source_id);
        policy = &current;
    }
    bool adult = match.item.adult, kids = false, visible = true;
    const auto category = [&](std::string_view name)
    {
        adult |= adult_category(name);
        kids |= kids_category(name);
        for (name = category_trim(name); !name.empty(); name = category_parent(name))
        {
            visible &= !policy->hidden.contains(std::string(name));
            const auto rule = policy->rules.find(std::string(name));
            if (rule != policy->rules.end())
            {
                adult |= rule->second == 1;
                kids |= rule->second == 2;
            }
        }
    };
    category(match.item.group_title);
    for (const auto &group : match.item.alternate_group_titles)
        category(group);
    return visible && !policy->hidden_channels.contains(match.item.id) &&
           (!parental_.enabled() ||
            (parental_.kids_only() ? kids && !adult : !adult || parental_.unlocked()));
}

bool Model::activate_search_result(const SearchResult &result)
{
    if (!searching_ || pin_prompt() || !playback_allowed())
        return false;
    if (!result.saved_source_id || result.saved_source_id == selected_source_id_)
    {
        if (result.source_id != source_id(active_source_))
            return false;
        if (result.kind == SearchResult::Kind::channel)
            return play_channel(result.id);
        if (result.kind == SearchResult::Kind::programme)
        {
            const auto at = catalog_.Find(result.id);
            const auto programmes = guide_.programmes(result.id);
            return at != iptv::Catalog::npos && channel_visible(at) &&
                   std::any_of(programmes.begin(), programmes.end(), [&](const Programme &p)
                               { return p.start == result.programme_start; });
        }
        return activate_search_vod(result);
    }
    const auto found =
        std::find_if(discovery_matches_.begin(), discovery_matches_.end(),
                     [&](const DiscoveryMatch &match)
                     {
                         return match.result.saved_source_id == result.saved_source_id &&
                                match.result.source_id == result.source_id &&
                                match.result.kind == result.kind && match.result.id == result.id &&
                                match.result.programme_start == result.programme_start;
                     });
    if (found == discovery_matches_.end() || !discovery_allowed(*found) || refreshing())
        return false;
    const auto item = found->item; // Source selection closes search and releases its snapshots.
    use_saved_source(result.saved_source_id);
    if (selected_source_id_ != result.saved_source_id ||
        source_id(active_source_) != result.source_id)
        return false;
    if (result.kind == SearchResult::Kind::channel)
        return play_channel(result.id);
    if (result.kind == SearchResult::Kind::programme)
    {
        const auto at = catalog_.Find(result.id);
        const auto programmes = guide_.programmes(result.id);
        return at != iptv::Catalog::npos && channel_visible(at) &&
               std::any_of(programmes.begin(), programmes.end(),
                           [&](const Programme &p) { return p.start == result.programme_start; });
    }
    if (!search_vod_allowed(item))
        return false;
    if (result.kind == SearchResult::Kind::movie)
        return queue_vod(item, {});
    view.vod_series = item.tvg_id;
    view.vod_series_name = item.name;
    view.vod_series_cover = item.tvg_logo;
    view.vod_series_group = item.group_title;
    view.vod_series_adult = item.adult;
    view.vod_kind = static_cast<int>(VodKind::episodes);
    view.vod_category.clear();
    view.vod_query.clear();
    view.vod_all = false;
    view.vod_focus = 0;
    vod_.select(VodKind::episodes, view.vod_series);
    return true;
}
bool Model::activate_search_vod(const SearchResult &result)
{
    if (!searching_ || result.source_id != source_id(active_source_) || pin_prompt() ||
        (result.kind != SearchResult::Kind::movie && result.kind != SearchResult::Kind::show))
        return false;
    const auto &library = result.kind == SearchResult::Kind::movie ? search_movies_ : search_shows_;
    const auto at = library.catalog().Find(result.id);
    if (at == iptv::Catalog::npos)
        return false;
    const auto item = library.catalog()[at];
    if (!search_vod_allowed(item))
        return false;
    if (result.kind == SearchResult::Kind::movie)
        return queue_vod(item, {});
    view.vod_series = item.tvg_id;
    view.vod_series_name = item.name;
    view.vod_series_cover = item.tvg_logo;
    view.vod_series_group = item.group_title;
    view.vod_series_adult = item.adult;
    view.vod_kind = static_cast<int>(VodKind::episodes);
    view.vod_category.clear();
    view.vod_query.clear();
    view.vod_all = false;
    view.vod_focus = 0;
    vod_.select(VodKind::episodes, view.vod_series);
    return true;
}
} // namespace ptv
