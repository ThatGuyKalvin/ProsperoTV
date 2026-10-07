// ProsperoTV - The app without its screens: sources, catalog, filters, favorites.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/model.hpp"

#include "iptv_ime.h"
#include "iptv_store.h"
#include "tv/platform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <utility>

namespace ptv
{

namespace
{

constexpr char kCatalogUrl[] = "https://iptv-org.github.io/iptv/index.m3u";
constexpr std::uint64_t kBuiltInSourceId = UINT64_C(0x495054562d4f5247);
constexpr std::uint64_t kCatalogRefreshSeconds = UINT64_C(12) * 60u * 60u;
constexpr std::size_t kCatalogThreadStackBytes = 4u * 1024u * 1024u;

constexpr unsigned kCustom = static_cast<unsigned>(iptv::SourceKind::Custom);
constexpr unsigned kXtream = static_cast<unsigned>(iptv::SourceKind::Xtream);

const char *source_name(iptv::SourceKind source)
{
    switch (source)
    {
    case iptv::SourceKind::Custom:
        return "your playlist";
    case iptv::SourceKind::Xtream:
        return "your Xtream account";
    case iptv::SourceKind::BuiltIn:
        break;
    }
    return "the iptv-org catalog";
}

bool valid_credential(const char *value)
{
    if (value == nullptr || *value == '\0')
        return false;
    const std::size_t bytes = std::strlen(value);
    if (bytes > iptv::kMaxXtreamCredentialBytes)
        return false;
    for (std::size_t index = 0; index < bytes; ++index)
    {
        const unsigned char byte = static_cast<unsigned char>(value[index]);
        if (byte < 0x20u || byte == 0x7fu)
            return false;
    }
    return true;
}

bool matches_query(const iptv::Channel &channel, std::string_view query)
{
    if (query.empty())
        return true;
    if (contains_nocase(channel.name, query) || contains_nocase(channel.tvg_name, query) ||
        contains_nocase(channel.tvg_id, query) || contains_nocase(channel.group_title, query) ||
        contains_nocase(channel.tvg_country, query) || contains_nocase(channel.tvg_language, query))
        return true;
    for (const std::string &group : channel.alternate_group_titles)
        if (contains_nocase(group, query))
            return true;
    // "hd", "1080p" and the like find the channels of that size.
    switch (quality_of(channel))
    {
    case kQualitySd:
        return contains_nocase("SD 480P 576P", query);
    case kQualityHd:
        return contains_nocase("HD 720P", query);
    case kQualityFullHd:
        return contains_nocase("FULL HD FHD 1080P", query);
    case kQualityUhd:
        return contains_nocase("4K UHD 2160P", query);
    default:
        return false;
    }
}

bool in_named_group(const iptv::Channel &channel, const char *term)
{
    if (contains_nocase(channel.group_title, term))
        return true;
    for (const std::string &alternate : channel.alternate_group_titles)
        if (contains_nocase(alternate, term))
            return true;
    return false;
}

bool matches_group(const iptv::Channel &channel, Group group, const iptv::UserState &user)
{
    switch (group)
    {
    case Group::favorites:
        return iptv::IsFavorite(user, channel.id);
    case Group::recent:
        return iptv::IsRecentChannel(user, channel.id);
    case Group::news:
        return in_named_group(channel, "news");
    case Group::sports:
        return in_named_group(channel, "sport");
    case Group::kids:
        return in_named_group(channel, "kid");
    default:
        return true;
    }
}

bool matches_filters(const iptv::Channel &channel, const std::string &country,
                     const std::string &category, const std::string &language, unsigned quality)
{
    if (!country.empty() && !field_has_value(channel.tvg_country, country))
        return false;
    if (!language.empty() && !field_has_value(channel.tvg_language, language))
        return false;
    if (!category.empty())
    {
        bool found = field_has_value(channel.group_title, category);
        for (const std::string &alternate : channel.alternate_group_titles)
            found = found || field_has_value(alternate, category);
        if (!found)
            return false;
    }
    return quality == kQualityAny || quality_of(channel) == quality;
}

// Why a download failed, in words a person can act on.
std::string fetch_problem(iptv::http::Status network, const iptv::http::FetchResult &fetch,
                          bool account, iptv::XtreamStatus account_status,
                          const std::string &account_message, std::size_t skipped)
{
    using iptv::http::Status;
    if (network != Status::ok)
        return "The console could not start its network connection.";
    if (account && account_status != iptv::XtreamStatus::ok)
    {
        std::string text = iptv::XtreamStatusDescription(account_status);
        if (!account_message.empty())
            text += ": " + account_message.substr(0, 96);
        return text + ".";
    }
    char text[160];
    switch (fetch.status)
    {
    case Status::ok:
        std::snprintf(text, sizeof(text),
                      "The playlist has no channels that can be played (%u entries skipped).",
                      static_cast<unsigned>(skipped));
        return text;
    case Status::http_status_error:
        std::snprintf(text, sizeof(text), "The server answered with error %d.", fetch.http_status);
        return text;
    case Status::deadline_exceeded:
        return "The server took too long to answer.";
    case Status::response_too_large:
        return "The playlist is larger than this app can load.";
    case Status::unsupported_url:
    case Status::invalid_argument:
        return "The address is not one this app can open.";
    case Status::redirect_error:
        return "The server sent the app in a circle.";
    case Status::cancelled:
        return "The download was stopped.";
    default:
        std::snprintf(text, sizeof(text), "The server could not be reached (code %d).",
                      fetch.native_error);
        return text;
    }
}

} // namespace

Model::Model(std::string data_dir, std::string cache_dir)
    : data_dir_(std::move(data_dir)), cache_dir_(std::move(cache_dir))
{
    if (cache_dir_.empty())
        cache_dir_ = data_dir_;
    health_.fill(SourceHealth::empty);
}

std::string Model::path(const char *name) const
{
    return data_dir_ + "/" + name;
}

std::string Model::cache_path(iptv::SourceKind source) const
{
    switch (source)
    {
    case iptv::SourceKind::Custom:
        return cache_dir_ + "/prosperotv-custom-catalog.sqlite3";
    case iptv::SourceKind::Xtream:
        return cache_dir_ + "/prosperotv-xtream-catalog.sqlite3";
    case iptv::SourceKind::BuiltIn:
        break;
    }
    return cache_dir_ + "/prosperotv-catalog.sqlite3";
}

std::uint64_t Model::source_id(iptv::SourceKind source) const
{
    switch (source)
    {
    case iptv::SourceKind::Custom:
        return iptv::CustomSourceId(custom_url_);
    case iptv::SourceKind::Xtream:
        return iptv::XtreamSourceId(xtream_);
    case iptv::SourceKind::BuiltIn:
        break;
    }
    return kBuiltInSourceId;
}

const char *Model::builtin_url()
{
    return kCatalogUrl;
}

const char *Model::group_name(Group group)
{
    static constexpr const char *names[kGroupCount] = {"All",  "Favorites", "Recent",
                                                       "News", "Sports",    "Kids"};
    return names[static_cast<unsigned>(group)];
}

bool Model::xtream_ready() const
{
    return iptv::ValidateXtreamCredentials(xtream_);
}

bool Model::is_set_up(iptv::SourceKind source) const
{
    switch (source)
    {
    case iptv::SourceKind::Custom:
        return !custom_url_.empty();
    case iptv::SourceKind::Xtream:
        return xtream_ready();
    case iptv::SourceKind::BuiltIn:
        break;
    }
    return true;
}

void Model::set_status(std::string label, Level level)
{
    status_ = std::move(label);
    level_ = level;
}

void Model::set_source_text(std::string title, std::string detail)
{
    source_title_ = std::move(title);
    source_detail_ = std::move(detail);
}

void Model::notify(Level level, std::string title, std::string body)
{
    // The interface shows a few at a time; a burst keeps its newest.
    if (notices_.size() >= 8)
        notices_.erase(notices_.begin());
    notices_.push_back({level, std::move(title), std::move(body), 0.0f});
}

void Model::announce(Level level, std::string title, std::string body, float seconds)
{
    notify(level, std::move(title), std::move(body));
    notices_.back().seconds = seconds;
}

std::vector<Notice> Model::take_notices()
{
    return std::exchange(notices_, {});
}

// ---- one menu session ------------------------------------------------------

bool Model::open()
{
    const bool first = !opened_once_;
    opened_once_ = true;

    account_form_ = {};
    account_step_ = AccountStep::none;
    account_prompt_pending_ = false;
    refresh_queued_ = false;
    refresh_done_.store(false, std::memory_order_relaxed);
    stop_requested_.store(false, std::memory_order_relaxed);
    pending_account_ = iptv::XtreamStatus::ok;
    pending_account_message_.clear();
    play_requested_ = false;
    play_request_ = {};
    catalog_failed_ = false;
    catalog_error_.clear();

    if (first)
    {
        health_.fill(SourceHealth::empty);
        std::string saved_url;
        if (iptv::LoadCustomSourceUrl(path("iptv-custom-source-v1.txt"), &saved_url) ==
            iptv::SourceStateStatus::ok)
        {
            custom_url_ = std::move(saved_url);
            health_[kCustom] = SourceHealth::saved;
        }
        if (iptv::LoadXtreamCredentials(path("prosperotv-xtream-v1.txt"), &xtream_) ==
            iptv::XtreamStatus::ok)
            health_[kXtream] = SourceHealth::saved;
        else
            xtream_ = {};

        active_source_ = iptv::SourceKind::BuiltIn;
        iptv::SourceKind saved = iptv::SourceKind::BuiltIn;
        if (iptv::LoadActiveSource(path("iptv-active-source-v1.txt"), &saved) ==
                iptv::SourceStateStatus::ok &&
            is_set_up(saved))
            active_source_ = saved;

        (void)iptv::LoadUserState(path("iptv-favorites-v1.bin"), path("iptv-history-v1.bin"),
                                  &user_);
        load_cache();
    }
    else if (catalog_loaded_)
    {
        // The catalog is still in memory: only what the last channel changed
        // is read again.
        (void)iptv::LoadPlaybackResults(path("prosperotv-playback-history.sqlite3"),
                                        catalog_.source_id, &catalog_);
        recount_groups();
        rebuild_visible();
    }
    else
    {
        load_cache();
    }

    keyboard_ready_ = iptv_ime_init();

    const std::uint64_t now = platform::unix_time();
    const bool fresh = catalog_loaded_ && now != 0 && saved_unix_ != 0 && now >= saved_unix_ &&
                       now - saved_unix_ < kCatalogRefreshSeconds;
    if (catalog_loaded_)
    {
        set_status("Saved copy", Level::ready);
        set_source_text("Showing the copy saved on this console",
                        fresh ? "It is up to date. Press Options to download it again."
                              : "A newer copy is being downloaded in the background.");
    }
    else
    {
        set_status("Loading", Level::busy);
        set_source_text("Downloading the channel list",
                        "Nothing is saved on this console yet. This happens once.");
    }
    if (!fresh)
        refresh();
    return true;
}

void Model::close()
{
    stop_requested_.store(true, std::memory_order_release);
    if (keyboard_ready_)
    {
        iptv_ime_shutdown();
        keyboard_ready_ = false;
    }
    while (refresh_thread_ != nullptr && !refresh_done_.load(std::memory_order_acquire))
    {
        platform::network_cancel();
        platform::sleep_ms(10);
    }
    while (refresh_thread_ != nullptr && !join_refresh())
        platform::sleep_ms(10);
    if (health_[static_cast<unsigned>(active_source_)] == SourceHealth::refreshing)
        health_[static_cast<unsigned>(active_source_)] =
            catalog_loaded_ ? SourceHealth::cached : SourceHealth::saved;
    refresh_queued_ = false;
    refresh_done_.store(false, std::memory_order_relaxed);
    stop_requested_.store(false, std::memory_order_relaxed);
    pending_catalog_ = {};
    account_step_ = AccountStep::none;
    account_prompt_pending_ = false;
}

void Model::poll()
{
    if (keyboard_ready_)
        iptv_ime_poll();
    continue_account_form();
    consume_refresh();
}

// ---- the catalog ------------------------------------------------------------

void Model::load_cache()
{
    const std::uint64_t wanted = source_id(active_source_);
    iptv::CatalogState cached;
    iptv::StoreReport report;
    const iptv::StoreStatus status =
        iptv::LoadCatalog(cache_path(active_source_), &cached, {}, &report);
    catalog_loaded_ =
        status == iptv::StoreStatus::ok && cached.source_id == wanted && !cached.channels.empty();
    catalog_ = catalog_loaded_ ? std::move(cached) : iptv::CatalogState{};
    saved_unix_ = catalog_loaded_ ? report.saved_unix : 0;
    if (catalog_loaded_)
        (void)iptv::LoadPlaybackResults(path("prosperotv-playback-history.sqlite3"), wanted,
                                        &catalog_);
    const bool own = active_source_ != iptv::SourceKind::BuiltIn;
    health_[static_cast<unsigned>(active_source_)] = catalog_loaded_ ? SourceHealth::cached
                                                     : own           ? SourceHealth::saved
                                                                     : SourceHealth::empty;
    index_names();
    rebuild_facets();
    recount_groups();
    rebuild_visible();
}

void Model::recount_groups()
{
    group_sizes_.fill(0);
    for (const iptv::Channel &channel : catalog_.channels)
    {
        ++group_sizes_[0];
        for (unsigned group = 1; group < kGroupCount; ++group)
            if (matches_group(channel, static_cast<Group>(group), user_))
                ++group_sizes_[group];
    }
}

void Model::rebuild_facets()
{
    using Entry = std::pair<std::string, unsigned>;
    std::vector<Entry> countries;
    std::vector<Entry> categories;
    std::vector<Entry> languages;
    const auto add = [](std::vector<Entry> *entries, const std::string &field)
    {
        const std::string value = first_value(field).substr(0, 47);
        if (value.empty())
            return;
        for (Entry &entry : *entries)
            if (equals_nocase(entry.first, value))
            {
                ++entry.second;
                return;
            }
        entries->emplace_back(value, 1u);
    };
    for (const iptv::Channel &channel : catalog_.channels)
    {
        add(&countries, channel.tvg_country);
        add(&categories, channel.group_title);
        add(&languages, channel.tvg_language);
        for (const std::string &category : channel.alternate_group_titles)
            add(&categories, category);
    }
    // The most common values, at most kFacetMax of each.
    const auto store =
        [](std::vector<Entry> *entries, std::array<Facet, kFacetMax> *facets, unsigned *count)
    {
        std::sort(entries->begin(), entries->end(),
                  [](const Entry &left, const Entry &right)
                  {
                      if (left.second != right.second)
                          return left.second > right.second;
                      return left.first < right.first;
                  });
        *count = static_cast<unsigned>(std::min<std::size_t>(entries->size(), kFacetMax));
        for (unsigned index = 0; index < *count; ++index)
        {
            (*facets)[index].value = (*entries)[index].first;
            (*facets)[index].count = (*entries)[index].second;
        }
    };
    store(&countries, &countries_, &country_count_);
    store(&categories, &categories_, &category_count_);
    store(&languages, &languages_, &language_count_);

    // A filter the new catalog has no word for cannot stay on.
    const auto kept =
        [](const std::array<Facet, kFacetMax> &facets, unsigned count, const std::string &selected)
    {
        if (selected.empty())
            return true;
        for (unsigned index = 0; index < count; ++index)
            if (equals_nocase(facets[index].value, selected))
                return true;
        return false;
    };
    if (!kept(countries_, country_count_, country_))
        country_.clear();
    if (!kept(categories_, category_count_, category_))
        category_.clear();
    if (!kept(languages_, language_count_, language_))
        language_.clear();
}

// The catalog changed: put its channels in the order of the alphabet once, so
// that every list made from it is in that order too.
void Model::index_names()
{
    const unsigned count = channel_count();
    std::vector<std::string> keys(count);
    order_.resize(count);
    letters_.resize(count);
    for (unsigned index = 0; index < count; ++index)
    {
        keys[index] = sort_key(catalog_.channels[index]);
        letters_[index] = static_cast<std::uint8_t>(letter_of_key(keys[index]));
        order_[index] = index;
    }
    // Channels of one name stay in the order the playlist gave them.
    std::stable_sort(order_.begin(), order_.end(),
                     [&keys](unsigned left, unsigned right) { return keys[left] < keys[right]; });
    ranks_.resize(count);
    for (unsigned rank = 0; rank < count; ++rank)
        ranks_[order_[rank]] = rank;
    note_scripts();
}

void Model::note_scripts()
{
    uses_east_asian_ = false;
    uses_korean_ = false;
    const auto scan = [this](const std::string &text)
    {
        // Every character of these scripts is three bytes of UTF-8.
        for (std::size_t index = 0; index + 2 < text.size(); ++index)
        {
            const unsigned char lead = static_cast<unsigned char>(text[index]);
            if (lead < 0xe3 || lead > 0xef)
                continue;
            const std::uint32_t codepoint =
                (static_cast<std::uint32_t>(lead & 0x0f) << 12) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 1]) & 0x3f) << 6) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(text[index + 2])) & 0x3f);
            if ((codepoint >= 0xac00 && codepoint <= 0xd7a3) || (codepoint >= 0x3130 && codepoint <= 0x318f))
                uses_korean_ = true;
            else if ((codepoint >= 0x3040 && codepoint <= 0x30ff) ||
                     (codepoint >= 0x3400 && codepoint <= 0x9fff) ||
                     (codepoint >= 0xf900 && codepoint <= 0xfaff))
                uses_east_asian_ = true;
            index += 2;
        }
    };
    for (const iptv::Channel &channel : catalog_.channels)
    {
        if (uses_east_asian_ && uses_korean_)
            break;
        scan(channel.name);
        scan(channel.group_title);
    }
}

void Model::rebuild_visible()
{
    if (order_.size() != channel_count())
        index_names();
    visible_count_ = 0;
    letter_starts_.fill(-1);
    for (const unsigned index : order_)
    {
        if (visible_count_ >= visible_.size())
            break;
        const iptv::Channel &channel = catalog_.channels[index];
        if (matches_group(channel, group_, user_) &&
            matches_filters(channel, country_, category_, language_, quality_) &&
            matches_query(channel, query_))
        {
            int &start = letter_starts_[letters_[index]];
            if (start < 0)
                start = static_cast<int>(visible_count_);
            visible_[visible_count_++] = index;
        }
    }
    ++revision_;
}

int Model::letter_at(unsigned position) const
{
    return position < visible_count_ ? letters_[visible_[position]] : 0;
}

int Model::position_of(std::string_view channel_id) const
{
    if (channel_id.empty())
        return -1;
    for (unsigned position = 0; position < visible_count_; ++position)
        if (catalog_.channels[visible_[position]].id == channel_id)
            return static_cast<int>(position);
    return -1;
}

const iptv::Channel *Model::find(std::string_view channel_id) const
{
    for (const iptv::Channel &channel : catalog_.channels)
        if (channel.id == channel_id)
            return &channel;
    return nullptr;
}

// ---- groups, search and filters ----------------------------------------------

void Model::set_group(Group group)
{
    if (group == group_ || group >= Group::count)
        return;
    group_ = group;
    rebuild_visible();
}

void Model::set_query(std::string_view query)
{
    const std::string next(query.substr(0, IPTV_IME_MAX_TEXT_BYTES - 1u));
    if (next == query_)
        return;
    query_ = next;
    rebuild_visible();
}

void Model::set_country(std::string_view value)
{
    if (country_ == value)
        return;
    country_ = value;
    rebuild_visible();
}

void Model::set_category(std::string_view value)
{
    if (category_ == value)
        return;
    category_ = value;
    rebuild_visible();
}

void Model::set_language(std::string_view value)
{
    if (language_ == value)
        return;
    language_ = value;
    rebuild_visible();
}

void Model::set_quality(unsigned quality)
{
    if (quality >= kQualityCount || quality == quality_)
        return;
    quality_ = quality;
    rebuild_visible();
}

bool Model::filtering() const
{
    return !query_.empty() || !country_.empty() || !category_.empty() || !language_.empty() ||
           quality_ != kQualityAny;
}

void Model::clear_filters()
{
    if (!filtering())
        return;
    iptv_ime_cancel();
    query_.clear();
    country_.clear();
    category_.clear();
    language_.clear();
    quality_ = kQualityAny;
    rebuild_visible();
}

bool Model::ask_query()
{
    if (!keyboard_ready_)
    {
        notify(Level::warning, "The keyboard is not available",
               "Close ProsperoTV and open it again.");
        return false;
    }
    iptv_ime_request(query_.c_str(), &Model::on_query, this);
    return true;
}

void Model::on_query(const char *text, void *self)
{
    if (self != nullptr && text != nullptr)
        static_cast<Model *>(self)->set_query(text);
}

// ---- one channel ----------------------------------------------------------------

bool Model::is_favorite(const iptv::Channel &channel) const
{
    return iptv::IsFavorite(user_, channel.id);
}

bool Model::is_recent(const iptv::Channel &channel) const
{
    return iptv::IsRecentChannel(user_, channel.id);
}

Model::Starred Model::toggle_favorite(unsigned catalog_index)
{
    if (catalog_index >= channel_count())
        return Starred::failed;
    const iptv::Channel &channel = catalog_.channels[catalog_index];
    const std::vector<std::string> previous = user_.favorite_ids;
    const bool favorite = iptv::ToggleFavorite(&user_, channel.id);
    if (iptv::SaveUserState(path("iptv-favorites-v1.bin"), path("iptv-history-v1.bin"), user_) !=
        iptv::UserStateStatus::ok)
    {
        user_.favorite_ids = previous;
        return Starred::failed;
    }
    recount_groups();
    // Only the favorites list changes shape; everywhere else the channel
    // stays where it is and only wears a star.
    if (group_ == Group::favorites)
        rebuild_visible();
    return favorite ? Starred::added : Starred::removed;
}

// ---- playback -----------------------------------------------------------------------

bool Model::play(unsigned catalog_index)
{
    if (catalog_index >= channel_count())
        return false;
    const iptv::Channel &channel = catalog_.channels[catalog_index];
    play_request_ = {};
    play_request_.channel_id = channel.id;
    play_request_.channel_name = channel.name;
    if (!channel.url.empty())
        play_request_.urls.push_back(channel.url);
    for (const std::string &alternate : channel.alternate_urls)
        if (!alternate.empty())
            play_request_.urls.push_back(alternate);
    play_request_.user_agent = channel.http_user_agent;
    play_request_.referrer = channel.http_referrer;
    play_request_.source_id = channel.source_id;
    play_request_.reconnect_live = active_source_ == iptv::SourceKind::Xtream;
    play_requested_ = !play_request_.urls.empty();
    if (!play_requested_)
        return false;
    const std::vector<std::string> previous = user_.recent_channel_ids;
    (void)iptv::AddRecentChannel(&user_, channel.id);
    if (iptv::SaveUserState(path("iptv-favorites-v1.bin"), path("iptv-history-v1.bin"), user_) !=
        iptv::UserStateStatus::ok)
        user_.recent_channel_ids = previous;
    return true;
}

bool Model::take_play_request(PlayRequest *request)
{
    if (request == nullptr || !play_requested_)
        return false;
    *request = std::move(play_request_);
    play_request_ = {};
    play_requested_ = false;
    return true;
}

void Model::report_playback_failure(const char *channel_id, const char *channel_name, int result,
                                    unsigned attempts, const char *detail)
{
    if (result >= 0)
        return;
    failure_ = {};
    failure_.channel_id = channel_id != nullptr ? channel_id : "";
    failure_.channel_name =
        channel_name != nullptr && *channel_name != '\0' ? channel_name : "this channel";
    failure_.reason =
        detail != nullptr && *detail != '\0' ? detail : "The channel may be offline right now.";
    failure_.attempts = attempts;
    failure_.can_retry = find(failure_.channel_id) != nullptr;
    has_failure_ = true;
    std::fprintf(stderr, "[ProsperoTV][player] channel=%s result=%d attempts=%u reason=%s\n",
                 failure_.channel_id.c_str(), result, attempts, failure_.reason.c_str());
}

void Model::dismiss_failure()
{
    has_failure_ = false;
    failure_ = {};
}

bool Model::retry_failure()
{
    if (!has_failure_)
        return false;
    const std::string channel_id = failure_.channel_id;
    dismiss_failure();
    for (unsigned index = 0; index < channel_count(); ++index)
        if (catalog_.channels[index].id == channel_id)
            return play(index);
    return false;
}

// ---- sources -------------------------------------------------------------------------

void Model::use_source(iptv::SourceKind source)
{
    if (refresh_thread_ != nullptr)
    {
        notify(Level::warning, "An update is running",
               "Wait for it to finish, then choose the source again.");
        return;
    }
    if (!is_set_up(source))
    {
        edit_source(source);
        return;
    }
    const bool changed = source != active_source_;
    if (changed)
    {
        active_source_ = source;
        load_cache();
    }
    if (iptv::SaveActiveSource(path("iptv-active-source-v1.txt"), source) !=
        iptv::SourceStateStatus::ok)
        notify(Level::warning, "The choice could not be saved",
               "It holds until ProsperoTV is closed.");
    else if (changed)
        notify(Level::ready, std::string("Now using ") + source_name(source));
    set_source_text(std::string("Using ") + source_name(source),
                    catalog_loaded_ ? "Showing the saved copy while a new one downloads."
                                    : "Nothing is saved yet. Downloading it now.");
    refresh();
}

void Model::edit_source(iptv::SourceKind source)
{
    if (source == iptv::SourceKind::BuiltIn)
    {
        notify(Level::ready, "The iptv-org catalog is built in", "It has nothing to set up.");
        return;
    }
    if (refresh_thread_ != nullptr)
    {
        notify(Level::warning, "An update is running",
               "Wait for it to finish, then edit the source.");
        return;
    }
    if (!keyboard_ready_)
    {
        notify(Level::warning, "The keyboard is not available",
               "Close ProsperoTV and open it again.");
        return;
    }
    if (source == iptv::SourceKind::Custom)
    {
        iptv_ime_request_prompt(custom_url_.c_str(), "Playlist address",
                                "http(s)://host/playlist.m3u", IPTV_IME_BUFFER_CHARACTERS,
                                &Model::on_custom_url, this);
        return;
    }
    account_form_ = xtream_;
    account_step_ = AccountStep::server;
    account_prompt_pending_ = true;
}

void Model::on_custom_url(const char *text, void *self)
{
    if (self != nullptr && text != nullptr)
        static_cast<Model *>(self)->apply_custom_url(text);
}

void Model::apply_custom_url(const char *url)
{
    if (refresh_thread_ != nullptr)
    {
        notify(Level::warning, "The address was not changed", "An update was still running.");
        return;
    }
    if (!iptv::http::IsSupportedPlaylistUrl(url))
    {
        notify(Level::error, "That address cannot be used",
               "It must start with http:// or https:// and have no spaces.");
        return;
    }
    const bool changed = custom_url_ != url;
    const iptv::SourceStateStatus saved =
        iptv::SaveCustomSourceUrl(path("iptv-custom-source-v1.txt"), url);
    if (saved != iptv::SourceStateStatus::ok)
    {
        notify(Level::error, "The address could not be saved",
               saved == iptv::SourceStateStatus::too_large ? "It is longer than 1,020 characters."
                                                           : "The console's storage refused it.");
        return;
    }
    custom_url_ = url;
    health_[kCustom] = SourceHealth::saved;
    if (changed && active_source_ == iptv::SourceKind::Custom)
        load_cache();
    use_source(iptv::SourceKind::Custom);
}

void Model::continue_account_form()
{
    if (!account_prompt_pending_ || !keyboard_ready_)
        return;
    account_prompt_pending_ = false;
    switch (account_step_)
    {
    case AccountStep::server:
        iptv_ime_request_prompt(account_form_.server_url.c_str(), "Xtream server (1 of 3)",
                                "http(s)://provider.example:port", IPTV_IME_BUFFER_CHARACTERS,
                                &Model::on_account_server, this);
        break;
    case AccountStep::username:
        iptv_ime_request_prompt(account_form_.username.c_str(), "Xtream user name (2 of 3)",
                                "User name", IPTV_IME_BUFFER_CHARACTERS,
                                &Model::on_account_username, this);
        break;
    case AccountStep::password:
        iptv_ime_request_password("Xtream password (3 of 3)", "Password",
                                  IPTV_IME_BUFFER_CHARACTERS, &Model::on_account_password, this);
        break;
    case AccountStep::none:
        break;
    }
}

void Model::on_account_server(const char *text, void *self)
{
    if (self != nullptr && text != nullptr)
        static_cast<Model *>(self)->apply_account_server(text);
}

void Model::on_account_username(const char *text, void *self)
{
    if (self != nullptr && text != nullptr)
        static_cast<Model *>(self)->apply_account_username(text);
}

void Model::on_account_password(const char *text, void *self)
{
    if (self != nullptr && text != nullptr)
        static_cast<Model *>(self)->apply_account_password(text);
}

void Model::apply_account_server(const char *server)
{
    std::string normalized;
    if (server == nullptr || !iptv::NormalizeXtreamServerUrl(server, &normalized))
    {
        account_step_ = AccountStep::none;
        notify(Level::error, "That server address cannot be used",
               "It must start with http:// or https://.");
        return;
    }
    account_form_.server_url = std::move(normalized);
    account_step_ = AccountStep::username;
    account_prompt_pending_ = true;
}

void Model::apply_account_username(const char *username)
{
    if (!valid_credential(username))
    {
        account_step_ = AccountStep::none;
        notify(Level::error, "The user name cannot be empty");
        return;
    }
    account_form_.username = username;
    account_step_ = AccountStep::password;
    account_prompt_pending_ = true;
}

void Model::apply_account_password(const char *password)
{
    account_step_ = AccountStep::none;
    if (!valid_credential(password))
    {
        notify(Level::error, "The password cannot be empty");
        return;
    }
    account_form_.password = password;
    if (!iptv::ValidateXtreamCredentials(account_form_))
    {
        notify(Level::error, "The account was not accepted",
               "Check the server address, the user name and the password.");
        return;
    }
    const bool changed =
        !xtream_ready() || iptv::XtreamSourceId(xtream_) != iptv::XtreamSourceId(account_form_);
    const iptv::XtreamStatus saved =
        iptv::SaveXtreamCredentials(path("prosperotv-xtream-v1.txt"), account_form_);
    if (saved != iptv::XtreamStatus::ok)
    {
        notify(Level::error, "The account could not be saved",
               iptv::XtreamStatusDescription(saved));
        return;
    }
    xtream_ = std::move(account_form_);
    account_form_ = {};
    health_[kXtream] = SourceHealth::saved;
    if (changed && active_source_ == iptv::SourceKind::Xtream)
        load_cache();
    use_source(iptv::SourceKind::Xtream);
}

// ---- the download ---------------------------------------------------------------------

void Model::refresh()
{
    if (refresh_thread_ != nullptr)
    {
        refresh_queued_ = true;
        return;
    }
    refresh_queued_ = false;
    if (!is_set_up(active_source_))
    {
        if (active_source_ == iptv::SourceKind::Xtream)
            edit_source(iptv::SourceKind::Xtream);
        return;
    }

    const bool custom = active_source_ == iptv::SourceKind::Custom;
    const bool account = active_source_ == iptv::SourceKind::Xtream;
    refresh_source_ = active_source_;
    refresh_url_ = custom ? custom_url_ : account ? std::string() : std::string(kCatalogUrl);
    refresh_cache_path_ = cache_path(active_source_);
    refresh_source_id_ = source_id(active_source_);
    refresh_account_ = account ? xtream_ : iptv::XtreamCredentials{};
    const SourceHealth before = health_[static_cast<unsigned>(active_source_)];
    health_[static_cast<unsigned>(active_source_)] = SourceHealth::refreshing;

    refresh_done_.store(false, std::memory_order_relaxed);
    stop_requested_.store(false, std::memory_order_relaxed);
    pending_saved_ = false;
    pending_account_ = iptv::XtreamStatus::ok;
    pending_account_message_.clear();
    set_status("Updating", Level::busy);
    set_source_text(std::string("Downloading ") + source_name(active_source_),
                    catalog_loaded_ ? "The saved channels stay available meanwhile."
                                    : "This can take a minute the first time.");
    catalog_failed_ = false;
    catalog_error_.clear();

    refresh_thread_ = platform::thread_start(&Model::refresh_entry, this, kCatalogThreadStackBytes,
                                             "iptv-catalog");
    if (refresh_thread_ != nullptr)
        return;

    health_[static_cast<unsigned>(active_source_)] =
        before == SourceHealth::refreshing ? SourceHealth::error : before;
    set_status("Update failed", Level::error);
    set_source_text("The update could not start", "Press Options to try again.");
    if (!catalog_loaded_)
    {
        health_[static_cast<unsigned>(active_source_)] = SourceHealth::error;
        catalog_failed_ = true;
        catalog_error_ = "The update could not start. Try again.";
    }
    else
    {
        notify(Level::warning, "The update could not start", "Press Options to try again.");
    }
}

void *Model::refresh_entry(void *self)
{
    static_cast<Model *>(self)->run_refresh();
    return nullptr;
}

// Runs on the worker thread. It touches only the pending_ and refresh_
// members, which the frame loop leaves alone until refresh_done_ is set.
void Model::run_refresh()
{
    pending_fetch_ = {};
    pending_catalog_ = {};
    pending_report_ = {};
    pending_saved_ = false;
    pending_account_ = iptv::XtreamStatus::ok;
    pending_account_stage_.clear();
    pending_account_message_.clear();
    pending_network_ = platform::network_init();

    const auto stopping = [this]() { return stop_requested_.load(std::memory_order_acquire); };
    if (pending_network_ == iptv::http::Status::ok && !stopping())
    {
        const iptv::http::RequestControl control{
            [](void *context)
            {
                return static_cast<const Model *>(context)->stop_requested_.load(
                    std::memory_order_acquire);
            },
            this};
        if (refresh_source_ == iptv::SourceKind::Xtream)
        {
            iptv::http::ListBuffer response =
                iptv::http::AllocateListBuffer(iptv::kMaxXtreamResponseBytes);
            std::string endpoint;
            std::vector<iptv::XtreamCategory> categories;
            iptv::XtreamAuth auth;
            bool reachable = true;
            const auto fetch = [&](std::string_view action)
            {
                if (!iptv::BuildXtreamApiUrl(refresh_account_, action, &endpoint))
                {
                    pending_account_ = iptv::XtreamStatus::invalid_argument;
                    return false;
                }
                pending_fetch_ = platform::fetch(endpoint.c_str(), response.data(), response.size(),
                                                 response.max_bytes, &control);
                return pending_fetch_.status == iptv::http::Status::ok && !stopping();
            };
            pending_account_stage_ = "authentication";
            if (fetch(""))
            {
                pending_account_ = iptv::ParseXtreamAuth(
                    std::string_view(response.data(), pending_fetch_.bytes), &auth);
                if (pending_account_ != iptv::XtreamStatus::ok)
                    pending_account_message_ = auth.message;
            }
            else
            {
                reachable = false;
            }
            if (reachable && pending_account_ == iptv::XtreamStatus::ok)
            {
                pending_account_stage_ = "categories";
                if (fetch("get_live_categories"))
                    pending_account_ = iptv::ParseXtreamCategories(
                        std::string_view(response.data(), pending_fetch_.bytes), &categories);
                else
                    reachable = false;
            }
            if (reachable && pending_account_ == iptv::XtreamStatus::ok)
            {
                pending_account_stage_ = "live-streams";
                if (fetch("get_live_streams"))
                    pending_account_ = iptv::ParseXtreamLiveStreams(
                        std::string_view(response.data(), pending_fetch_.bytes), refresh_account_,
                        categories, refresh_source_id_, &pending_catalog_, &pending_report_);
            }
        }
        else
        {
            iptv::http::ListBuffer playlist = iptv::http::AllocateListBuffer();
            pending_fetch_ = platform::fetch(refresh_url_.c_str(), playlist.data(), playlist.size(),
                                             playlist.max_bytes, &control);
            if (pending_fetch_.status == iptv::http::Status::ok && !stopping())
            {
                const std::string_view input(playlist.data(), pending_fetch_.bytes);
                pending_catalog_ =
                    iptv::ParseExtendedM3u(input, refresh_source_id_, {}, &pending_report_);
            }
        }
        if (!pending_catalog_.channels.empty() && !stopping())
            pending_saved_ =
                iptv::SaveCatalog(refresh_cache_path_, pending_catalog_) == iptv::StoreStatus::ok;
    }
    if (pending_network_ == iptv::http::Status::ok)
        platform::network_shutdown();
    refresh_done_.store(true, std::memory_order_release);
}

bool Model::join_refresh()
{
    if (refresh_thread_ == nullptr)
        return true;
    const int joined = platform::thread_join(refresh_thread_);
    if (joined != 0)
    {
        if (!refresh_done_.load(std::memory_order_acquire))
            return false;
        const int detached = platform::thread_detach(refresh_thread_);
        std::fprintf(stderr, "[ProsperoTV] refresh join failed: %d; detach fallback: %d\n", joined,
                     detached);
    }
    refresh_thread_ = nullptr;
    return true;
}

// The account's last answer, without the account: for looking into a provider
// that will not sign in.
void Model::save_account_receipt() const
{
    const std::string target = path("prosperotv-xtream-receipt.txt");
    const std::string temporary = target + ".tmp";
    std::FILE *file = std::fopen(temporary.c_str(), "wb");
    if (file == nullptr)
        return;
    std::fprintf(
        file,
        "PROSPEROTV_XTREAM_RECEIPT_V1\n"
        "stage=%s\nnetwork_status=%u\nfetch_status=%u\nhttp_status=%d\n"
        "native_error=0x%08x\nresponse_bytes=%llu\nxtream_status=%u\n"
        "xtream_description=%s\nlines_seen=%u\naccepted=%u\nskipped=%u\nchannels=%llu\n",
        pending_account_stage_.empty() ? "none" : pending_account_stage_.c_str(),
        static_cast<unsigned>(pending_network_), static_cast<unsigned>(pending_fetch_.status),
        pending_fetch_.http_status, static_cast<unsigned>(pending_fetch_.native_error),
        static_cast<unsigned long long>(pending_fetch_.bytes),
        static_cast<unsigned>(pending_account_), iptv::XtreamStatusDescription(pending_account_),
        static_cast<unsigned>(pending_report_.lines_seen),
        static_cast<unsigned>(pending_report_.accepted),
        static_cast<unsigned>(pending_report_.skipped),
        static_cast<unsigned long long>(pending_catalog_.channels.size()));
    const bool written = std::ferror(file) == 0 && std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed)
    {
        std::remove(temporary.c_str());
        return;
    }
    std::remove(target.c_str());
    if (std::rename(temporary.c_str(), target.c_str()) != 0)
        std::remove(temporary.c_str());
}

void Model::consume_refresh()
{
    if (refresh_thread_ == nullptr || !refresh_done_.load(std::memory_order_acquire))
        return;
    if (!join_refresh())
        return;
    refresh_done_.store(false, std::memory_order_relaxed);

    const bool account = refresh_source_ == iptv::SourceKind::Xtream;
    if (account)
        save_account_receipt();
    const bool success = pending_network_ == iptv::http::Status::ok &&
                         pending_fetch_.status == iptv::http::Status::ok &&
                         (!account || pending_account_ == iptv::XtreamStatus::ok) &&
                         !pending_catalog_.channels.empty();
    const unsigned source = static_cast<unsigned>(refresh_source_);
    if (success)
    {
        catalog_ = std::move(pending_catalog_);
        catalog_loaded_ = true;
        catalog_failed_ = false;
        catalog_error_.clear();
        saved_unix_ = pending_saved_ ? platform::unix_time() : 0;
        (void)iptv::LoadPlaybackResults(path("prosperotv-playback-history.sqlite3"),
                                        catalog_.source_id, &catalog_);
        index_names();
        rebuild_facets();
        recount_groups();
        rebuild_visible();
        if (has_failure_)
            failure_.can_retry = find(failure_.channel_id) != nullptr;
        health_[source] = pending_saved_ ? SourceHealth::ready : SourceHealth::stale;
        const std::string count = group_digits(channel_count()) + " channels";
        set_status(pending_saved_ ? "Up to date" : "Not saved",
                   pending_saved_ ? Level::ready : Level::warning);
        set_source_text(pending_saved_ ? "Up to date" : "Downloaded, but not saved",
                        pending_saved_
                            ? count + " from " + source_name(refresh_source_) + "."
                            : count + ". The console's storage refused the copy, so it lasts until "
                                      "ProsperoTV is closed.");
        notify(pending_saved_ ? Level::ready : Level::warning, "Channel list updated", count);
    }
    else
    {
        const std::string problem =
            fetch_problem(pending_network_, pending_fetch_, account, pending_account_,
                          pending_account_message_, pending_report_.skipped);
        if (catalog_loaded_)
        {
            health_[source] = SourceHealth::stale;
            set_status("Saved copy", Level::warning);
            set_source_text("The update failed", problem + " Showing the " +
                                                     group_digits(channel_count()) +
                                                     " channels saved on this console.");
            notify(Level::warning, "The channel list could not be updated", problem);
        }
        else
        {
            health_[source] = SourceHealth::error;
            set_status("No channels", Level::error);
            set_source_text("The channel list could not be downloaded", problem);
            catalog_failed_ = true;
            catalog_error_ = problem;
        }
    }
    pending_catalog_ = {};
    if (refresh_queued_)
    {
        refresh_queued_ = false;
        refresh();
    }
}

} // namespace ptv
