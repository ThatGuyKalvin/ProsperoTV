// ProsperoTV - The app without its screens: sources, catalog, filters, favorites.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Everything the interface shows comes from here and everything it does goes
// through here. Nothing in this file draws, and nothing in it knows which
// renderer is in use, so it runs unchanged in tests and on a PC.

#pragma once

#include "iptv_catalog.h"
#include "iptv_http.h"
#include "iptv_source_state.h"
#include "iptv_store.h"
#include "iptv_user_state.h"
#include "iptv_xtream.h"
#include "tv/catalog_index.hpp"
#include "tv/channel_text.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ptv
{

// What the player is asked to open: the channel's addresses, best first.
struct PlayRequest
{
    std::string channel_id;
    std::string channel_name;
    std::vector<std::string> urls;
    std::string user_agent;
    std::string referrer;
    std::uint64_t source_id = 0;
    bool reconnect_live = false;
};

// What the browse screens show: the live channels, or the films and the
// series of the source in use. Each keeps its own list, filters and focus.
enum class Shelf : std::uint8_t
{
    live,
    movies,
    series,
    count,
};
inline constexpr unsigned kShelfCount = static_cast<unsigned>(Shelf::count);

// How far a shelf's list (or a series' episodes) has come.
enum class LibraryState : std::uint8_t
{
    none,    // not asked for, or the source has none
    loading, // being read from the console or downloaded
    ready,
    failed, // error() says why
};

// The lists a catalog is browsed by.
enum class Group : std::uint8_t
{
    all,
    favorites,
    recent,
    news,
    sports,
    kids,
    count,
};
inline constexpr unsigned kGroupCount = static_cast<unsigned>(Group::count);

enum class Level : std::uint8_t
{
    ready,
    busy,
    warning,
    error,
};

enum class SourceHealth : std::uint8_t
{
    empty,      // not set up
    saved,      // set up, nothing downloaded yet
    cached,     // showing the copy saved on the console
    refreshing, // downloading now
    ready,      // downloaded and saved
    stale,      // the last download failed or could not be saved
    error,      // nothing to show
};

// Something that happened and is worth a line on screen once.
struct Notice
{
    Level level = Level::ready;
    std::string title;
    std::string body;
    float seconds = 0.0f; // how long it stays; 0: as long as notices usually do
};

// The last channel that would not open.
struct PlaybackFailure
{
    std::string channel_id;
    std::string channel_name;
    std::string reason;
    unsigned attempts = 0;
    bool can_retry = false; // the channel is still in the catalog
};

// What the interface keeps across a channel: the menu is closed while video
// plays and reopened where it was.
struct ViewState
{
    int tab = 0;
    Group live_group = Group::all;
    std::string focused_channel; // its id
    int focused_source = 0;
    // The films' and the series' own lists and focus.
    std::array<Group, kShelfCount> shelf_group{};
    std::array<std::string, kShelfCount> shelf_focus;
    std::string open_series; // the series whose episodes were on screen
    int open_season = 0;
};

class Model
{
  public:
    static constexpr unsigned kFacetMax = CatalogIndex::kFacetMax;
    static constexpr unsigned kSourceCount = 3;

    // data_dir is where the app keeps its files: the title's own storage or
    // /data/prosperotv/config on the console, any folder on a PC. cache_dir
    // holds the downloaded channel lists; empty means beside the rest.
    explicit Model(std::string data_dir, std::string cache_dir = {});
    Model(const Model &) = delete;
    Model &operator=(const Model &) = delete;

    // ---- one menu session ----
    // open() reads the sources, the lists and the saved catalog, and starts a
    // download when the saved copy is old. close() ends the keyboard and the
    // download. Both may be called again and again; the filters and the view
    // survive.
    bool open();
    void close();
    // Once per frame: keyboard answers, the steps of the account form, and
    // the result of a download.
    void poll();

    // ---- the catalog ----
    bool has_catalog() const
    {
        return catalog_loaded_;
    }
    unsigned channel_count() const
    {
        return static_cast<unsigned>(catalog_.size());
    }
    // A channel as the catalog holds it: read it and let it go (the texts are
    // the catalog's own, and a download replaces the catalog).
    iptv::ChannelView channel(unsigned index) const
    {
        return catalog_[index];
    }
    // The channels the group, the search and the filters leave, in the order
    // of the alphabet (see sort_key).
    unsigned visible_count() const
    {
        return visible_count_;
    }
    // The catalog index of the channel at a position of that list.
    unsigned visible(unsigned position) const
    {
        return visible_[position];
    }
    // Where a channel is in that list, or -1.
    int position_of(std::string_view channel_id) const;
    std::optional<iptv::ChannelView> find(std::string_view channel_id) const;

    // ---- shelves: live channels, films, series (tv/library.cpp) ----
    // Everything above (the catalog, its lists, groups, search and filters)
    // is the shelf on screen; each shelf keeps its own while another is.
    Shelf shelf() const
    {
        return shelf_;
    }
    void set_shelf(Shelf shelf);
    // The source in use has films and series (an Xtream account).
    bool has_library() const;
    LibraryState shelf_state(Shelf shelf) const;
    const std::string &shelf_error(Shelf shelf) const;
    // Downloads a shelf's list again.
    void refresh_shelf(Shelf shelf);
    // The details of a film or a series (plot, cast, runtime), once fetched;
    // want_details() asks for them, the newest wish first.
    const iptv::MediaDetails *details(std::string_view id) const;
    bool details_failed(std::string_view id) const;
    void want_details(unsigned catalog_index);

    // ---- one series' episodes (tv/library.cpp) ----
    // Opens the series at that index of the shelf on screen: its episodes
    // are fetched, and episodes() has them once episodes_state() is ready.
    bool open_series(unsigned catalog_index);
    void close_series();
    const std::string &series_id() const
    {
        return series_open_;
    }
    const std::string &series_name() const
    {
        return series_name_;
    }
    LibraryState episodes_state() const
    {
        return episodes_state_;
    }
    const std::string &episodes_error() const
    {
        return episodes_error_;
    }
    const iptv::Catalog &episodes() const
    {
        return episodes_;
    }
    // The seasons the series has, in order, and its episodes in a season.
    const std::vector<std::uint16_t> &seasons() const
    {
        return seasons_;
    }
    std::vector<unsigned> season_episodes(std::uint16_t season) const;
    // The episode to go on with: after the last one watched, or the first.
    int continue_episode() const;
    // An episode of it was watched (or begun) before.
    bool series_started() const;
    // Asks for the episodes again after they could not be read.
    void retry_series();
    bool play_episode(unsigned episode_index);
    // The letter (0 for '#', 1 to 26) the channel at a position is filed under.
    int letter_at(unsigned position) const;
    // The position of the first channel of that list under a letter, or -1.
    int letter_start(int letter) const
    {
        return visible_count_ > 0 && letter >= 0 && letter < kLetterCount
                   ? letter_starts_[static_cast<unsigned>(letter)]
                   : -1;
    }
    // A channel's place in the whole catalog read in the order of the
    // alphabet, from 1: the number it is known by whatever narrows the list.
    unsigned number_of(unsigned catalog_index) const
    {
        return catalog_index < index_.ranks.size() ? index_.ranks[catalog_index] + 1u : 0u;
    }
    // The scripts the list's names and groups are written in beyond the
    // European ones: the faces for them are large, and loaded only when asked for.
    bool uses_east_asian() const
    {
        return index_.east_asian;
    }
    bool uses_korean() const
    {
        return index_.korean;
    }
    // Changes whenever the visible list may have changed.
    unsigned revision() const
    {
        return revision_;
    }

    // ---- groups ----
    Group group() const
    {
        return group_;
    }
    void set_group(Group group);
    unsigned group_size(Group group) const
    {
        return group_sizes_[static_cast<unsigned>(group)];
    }
    static const char *group_name(Group group);

    // ---- search and filters ----
    const std::string &query() const
    {
        return query_;
    }
    void set_query(std::string_view query);
    std::span<const Facet> countries() const
    {
        return {index_.countries.data(), index_.country_count};
    }
    std::span<const Facet> categories() const
    {
        return {index_.categories.data(), index_.category_count};
    }
    std::span<const Facet> languages() const
    {
        return {index_.languages.data(), index_.language_count};
    }
    const std::string &country() const
    {
        return country_;
    }
    const std::string &category() const
    {
        return category_;
    }
    const std::string &language() const
    {
        return language_;
    }
    unsigned quality() const
    {
        return quality_;
    }
    void set_country(std::string_view value);
    void set_category(std::string_view value);
    void set_language(std::string_view value);
    void set_quality(unsigned quality);
    // A query or a filter narrows the list.
    bool filtering() const;
    void clear_filters();
    // Opens the console keyboard for the search text.
    bool ask_query();

    // ---- one channel ----
    bool is_favorite(const iptv::ChannelView &channel) const;
    bool is_recent(const iptv::ChannelView &channel) const;
    enum class Starred : std::uint8_t
    {
        added,
        removed,
        failed, // the list could not be saved; nothing changed
    };
    Starred toggle_favorite(unsigned catalog_index);

    // ---- playback ----
    // Queues the channel for the player; the frame loop takes the request,
    // closes the menu and plays it.
    bool play(unsigned catalog_index);
    bool take_play_request(PlayRequest *request);
    // Called when the menu reopens after a channel that would not play.
    void report_playback_failure(const char *channel_id, const char *channel_name, int result,
                                 unsigned attempts, const char *detail);
    const PlaybackFailure *failure() const
    {
        return has_failure_ ? &failure_ : nullptr;
    }
    void dismiss_failure();
    bool retry_failure();

    // ---- sources ----
    iptv::SourceKind active_source() const
    {
        return active_source_;
    }
    SourceHealth health(iptv::SourceKind source) const
    {
        return health_[static_cast<unsigned>(source)];
    }
    const std::string &custom_url() const
    {
        return custom_url_;
    }
    bool xtream_ready() const;
    const std::string &xtream_server() const
    {
        return xtream_.server_url;
    }
    bool is_set_up(iptv::SourceKind source) const;
    static const char *builtin_url();
    bool refreshing() const
    {
        return refresh_thread_ != nullptr;
    }
    // How many channels the download in progress has read so far.
    unsigned refresh_progress() const
    {
        return refresh_count_.load(std::memory_order_relaxed);
    }
    bool keyboard_ready() const
    {
        return keyboard_ready_;
    }
    // Makes a source the one in use. A source that is not set up opens its form.
    void use_source(iptv::SourceKind source);
    // Opens the form of a source (the address, or the three account fields).
    void edit_source(iptv::SourceKind source);
    // Downloads the source in use again.
    void refresh();
    // When the catalog on screen was saved (seconds since 1970; 0: unknown).
    std::uint64_t saved_unix() const
    {
        return saved_unix_;
    }

    // ---- what to say ----
    Level level() const
    {
        return level_;
    }
    // A few words for the corner of the screen.
    const std::string &status() const
    {
        return status_;
    }
    // The source in use: a headline and a sentence.
    const std::string &source_title() const
    {
        return source_title_;
    }
    const std::string &source_detail() const
    {
        return source_detail_;
    }
    // Nothing to browse and the download failed: why.
    bool catalog_failed() const
    {
        return catalog_failed_;
    }
    const std::string &catalog_error() const
    {
        return catalog_error_;
    }
    std::vector<Notice> take_notices();
    // Something the app itself has to say (a newer version exists).
    void announce(Level level, std::string title, std::string body, float seconds);

    ViewState view;

  private:
    enum class AccountStep : std::uint8_t
    {
        none,
        server,
        username,
        password,
    };

    // One shelf while another is on screen.
    struct ShelfData
    {
        iptv::Catalog catalog;
        CatalogIndex index;
        bool loaded = false;
        std::uint64_t saved_unix = 0;
        std::vector<std::uint8_t> marks;
        std::vector<std::uint32_t> visible;
        unsigned visible_count = 0;
        std::array<int, kLetterCount> letter_starts{};
        Group group = Group::all;
        std::array<unsigned, kGroupCount> group_sizes{};
        std::string query;
        std::string country;
        std::string category;
        std::string language;
        unsigned quality = kQualityAny;
    };
    // A job for the library's worker, and what it brings back.
    struct LibraryJob
    {
        enum class Kind : std::uint8_t
        {
            load,     // a shelf's saved copy
            download, // a shelf's list from the account
            episodes, // one series
            details,  // one film or series
        };
        Kind kind = Kind::load;
        Shelf shelf = Shelf::movies;
        std::string id;        // the entry asked about
        std::string remote_id; // the provider's id for it
        std::string url;       // a series' episode list
        std::string name;
        bool series = false;
    };
    struct LibraryResult
    {
        bool ok = false;
        bool stale = false; // a saved copy old enough to download again
        bool saved = false;
        std::uint64_t saved_unix = 0;
        std::string error;
        iptv::Catalog catalog;
        CatalogIndex index;
        iptv::MediaDetails details;
    };

    // Swaps the shelf on screen into its place in shelves_ and `shelf` out.
    void swap_shelf(Shelf shelf);
    std::string library_cache_path(Shelf shelf) const;
    void library_check_source();
    void library_reset();
    void library_queue(LibraryJob job);
    void library_poll();
    void library_stop();
    void finish_library_job();
    static void *library_entry(void *self);
    void run_library();
    void run_library_job(const LibraryJob &job, LibraryResult *result);

    std::string path(const char *name) const;
    std::string cache_path(iptv::SourceKind source) const;
    std::uint64_t source_id(iptv::SourceKind source) const;
    void load_cache();
    void load_live_cache();
    void adopt_catalog();
    void mark_lists();
    void recount_groups();
    void rebuild_visible();
    void set_status(std::string label, Level level);
    void set_source_text(std::string title, std::string detail);
    void notify(Level level, std::string title, std::string body = {});

    void apply_custom_url(const char *url);
    void apply_account_server(const char *server);
    void apply_account_username(const char *username);
    void apply_account_password(const char *password);
    void continue_account_form();
    void consume_refresh();
    bool join_refresh();
    static void *refresh_entry(void *self);
    void run_refresh();
    void save_account_receipt() const;

    static void on_query(const char *text, void *self);
    static void on_custom_url(const char *text, void *self);
    static void on_account_server(const char *text, void *self);
    static void on_account_username(const char *text, void *self);
    static void on_account_password(const char *text, void *self);

    std::string data_dir_;
    std::string cache_dir_;
    bool opened_once_ = false;
    bool keyboard_ready_ = false;

    // ---- catalog and lists ----
    iptv::Catalog catalog_;
    CatalogIndex index_; // of catalog_
    bool catalog_loaded_ = false;
    std::uint64_t saved_unix_ = 0;
    iptv::UserState user_;
    std::vector<std::uint8_t> marks_; // by catalog index: a favorite, a recent channel
    std::vector<std::uint32_t> visible_;
    unsigned visible_count_ = 0;
    std::array<int, kLetterCount> letter_starts_{};
    unsigned revision_ = 0;
    Group group_ = Group::all;
    std::array<unsigned, kGroupCount> group_sizes_{};

    // ---- filters ----
    std::string query_;
    std::string country_;
    std::string category_;
    std::string language_;
    unsigned quality_ = kQualityAny;

    // ---- sources ----
    iptv::SourceKind active_source_ = iptv::SourceKind::BuiltIn;
    std::array<SourceHealth, kSourceCount> health_{};
    std::string custom_url_;
    iptv::XtreamCredentials xtream_;
    iptv::XtreamCredentials account_form_;
    AccountStep account_step_ = AccountStep::none;
    bool account_prompt_pending_ = false;

    // ---- the download in progress ----
    void *refresh_thread_ = nullptr;
    bool refresh_queued_ = false;
    std::atomic<bool> refresh_done_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<unsigned> refresh_count_{0};
    iptv::SourceKind refresh_source_ = iptv::SourceKind::BuiltIn;
    std::string refresh_url_;
    std::string refresh_cache_path_;
    std::uint64_t refresh_source_id_ = 0;
    iptv::XtreamCredentials refresh_account_;
    iptv::http::Status pending_network_ = iptv::http::Status::not_initialized;
    iptv::http::FetchResult pending_fetch_{};
    iptv::ParseReport pending_report_{};
    iptv::Catalog pending_catalog_{};
    CatalogIndex pending_index_; // of pending_catalog_
    iptv::XtreamStatus pending_account_ = iptv::XtreamStatus::ok;
    std::string pending_account_stage_;
    std::string pending_account_message_;
    bool pending_saved_ = false;

    // ---- shelves and the library (tv/library.cpp) ----
    Shelf shelf_ = Shelf::live;
    std::array<ShelfData, kShelfCount> shelves_; // the one on screen is in the members above
    std::array<LibraryState, kShelfCount> shelf_state_{};
    std::array<std::string, kShelfCount> shelf_error_;
    std::uint64_t library_source_ = 0; // the source the library belongs to
    iptv::XtreamCredentials library_account_;
    std::deque<LibraryJob> library_jobs_;
    LibraryJob library_job_;       // the one the worker has
    LibraryResult library_result_; // what it brought back
    void *library_thread_ = nullptr;
    std::atomic<bool> library_done_{false};
    std::atomic<bool> library_stop_{false};
    struct DetailsEntry
    {
        std::string id;
        iptv::MediaDetails details;
        bool failed = false;
    };
    std::deque<DetailsEntry> details_; // the newest first, a few dozen at most
    std::string series_open_;
    std::string series_name_;
    std::string series_url_;
    std::string series_remote_;
    LibraryState episodes_state_ = LibraryState::none;
    std::string episodes_error_;
    iptv::Catalog episodes_;
    std::vector<std::uint16_t> seasons_;

    // ---- playback ----
    bool play_requested_ = false;
    PlayRequest play_request_;
    bool has_failure_ = false;
    PlaybackFailure failure_;

    // ---- words ----
    Level level_ = Level::ready;
    std::string status_ = "Starting";
    std::string source_title_;
    std::string source_detail_;
    bool catalog_failed_ = false;
    std::string catalog_error_;
    std::vector<Notice> notices_;
};

} // namespace ptv
