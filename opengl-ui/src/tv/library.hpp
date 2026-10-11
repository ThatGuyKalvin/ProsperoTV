// ProsperoTV - Saved sources, category visibility and favorite folders.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "iptv_catalog.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <utility>
#include <map>

struct sqlite3;
namespace iptv
{
class Catalog;
}

namespace ptv
{

enum class RefreshSchedule : int
{
    daily,
    weekly,
    manual
};
bool refresh_due(RefreshSchedule schedule, std::uint64_t saved, std::uint64_t now);
const char *schedule_name(RefreshSchedule schedule);

struct AccountInfo
{
    std::int64_t expires_unix = -1;
    std::int64_t max_connections = -1;
    std::int64_t checked_unix = 0;
};
// Positive: days remaining (rounded up) within one week, -1: expired, 0: no warning.
int account_expiry_notice(const AccountInfo &info, std::uint64_t now);

// IDs 1..3 are the original built-in, playlist and Xtream source slots.
// Subsequent records allow any number of playlists and accounts.
struct SavedSource
{
    std::int64_t id = 0;
    int kind = 0; // 0: built-in, 1: playlist, 2: Xtream, 3: portal, 4: HDHomeRun, 5: Tvheadend
    std::string name;
    std::string url;
    std::string username;
    std::string password;
    std::string mac;
    RefreshSchedule schedule = RefreshSchedule::daily;
    AccountInfo account{};
    std::string user_agent{}, referrer{}, extra_headers{};
};
bool valid_source(const SavedSource &source);

// All mutations are committed before callers update their in-memory state.
// The original catalog and favorites files remain readable by older releases.
bool completed_watch(std::string_view receipt, std::int64_t seconds, int result);

struct ChannelHistory
{
    std::int64_t first_seen = 0;
    std::int64_t watches = 0;
};

struct ResumeItem
{
    iptv::Channel channel;
    std::string series;
    std::uint64_t position_us = 0, duration_us = 0;
};

class Library
{
  public:
    Library() = default;
    ~Library();
    Library(const Library &) = delete;
    Library &operator=(const Library &) = delete;
    bool open(const std::string &path);
    bool close();
    bool ready() const
    {
        return db_ != nullptr;
    }
    std::vector<SavedSource> sources() const;
    bool save_source(SavedSource *source);
    bool remove_source(std::int64_t id);
    bool save_account_info(std::int64_t source, const AccountInfo &info);
    std::string guide_url(std::int64_t source) const;
    std::map<std::string, std::string> guide_mapping(std::int64_t source) const;
    bool set_guide_url(std::int64_t source, std::string_view url);
    bool set_guide_mapping(std::int64_t source, std::string_view channel,
                           std::string_view guide_id);
    std::int64_t selected_source() const;
    bool select_source(std::int64_t id);
    bool observe_channels(std::int64_t source, const iptv::Catalog &catalog, std::int64_t now);
    std::vector<ChannelHistory> channel_history(std::int64_t source,
                                                const iptv::Catalog &catalog) const;
    bool record_watch(std::int64_t source, std::string_view channel);
    bool remember_channel(std::int64_t source, std::string_view channel);
    std::pair<std::int64_t, std::string> last_channel() const;
    std::pair<std::uint64_t, std::uint64_t> playback_progress(std::string_view channel) const;
    bool remember_vod(std::int64_t source, std::uint64_t fingerprint,
                      const iptv::ChannelView &channel, std::string_view series,
                      std::int64_t updated);
    std::vector<ResumeItem> unfinished_vod(std::int64_t source, std::uint64_t fingerprint) const;
    bool save_playback_progress(std::string_view channel, std::uint64_t position_us,
                                std::uint64_t duration_us);
    std::unordered_set<std::string> hidden_categories(std::uint64_t source) const;
    bool hide_category(std::uint64_t source, std::string_view category, bool hidden);
    std::map<std::string, std::string, std::less<>> channel_names(std::uint64_t source) const;
    bool rename_channel(std::uint64_t source, std::string_view channel, std::string_view name);
    std::unordered_set<std::string> hidden_channels(std::uint64_t source) const;
    bool hide_channel(std::uint64_t source, std::string_view channel, bool hidden);
    std::vector<std::string> channel_order(std::uint64_t source, bool favorites) const;
    bool save_channel_order(std::uint64_t source, bool favorites,
                            const std::vector<std::string> &ids);
    // 1: PIN required, 2: approved for kids. Rules include subcategories.
    std::map<std::string, int> category_rules(std::uint64_t source) const;
    bool set_category_rule(std::uint64_t source, std::string_view category, int rule);
    std::vector<std::string> folders() const;
    bool add_folder(std::string_view name);
    bool rename_folder(std::string_view name, std::string_view replacement);
    bool remove_folder(std::string_view name);
    std::unordered_set<std::string> folder_channels(std::string_view folder) const;
    bool put_in_folder(std::string_view folder, std::string_view channel, bool included);
    bool remove_from_folders(std::string_view channel);

  private:
    sqlite3 *db_ = nullptr;
};

} // namespace ptv
