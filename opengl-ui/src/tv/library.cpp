// ProsperoTV - Saved sources, category visibility and favorite folders.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/library.hpp"
#include "tv/usb_playlist.hpp"

#include "iptv_catalog.h"
#include "iptv_xtream.h"

#include <sqlite3.h>
#include <algorithm>

namespace ptv
{
namespace
{
class Statement
{
  public:
    Statement(sqlite3 *db, const char *sql)
    {
        if (db != nullptr && sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    }
    ~Statement()
    {
        sqlite3_finalize(stmt);
    }
    bool text(int at, std::string_view value)
    {
        return stmt != nullptr &&
               sqlite3_bind_text(stmt, at, value.empty() ? "" : value.data(),
                                 static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }
    bool integer(int at, std::int64_t value)
    {
        return stmt != nullptr && sqlite3_bind_int64(stmt, at, value) == SQLITE_OK;
    }
    bool done()
    {
        return stmt != nullptr && sqlite3_step(stmt) == SQLITE_DONE;
    }
    bool row()
    {
        return stmt != nullptr && sqlite3_step(stmt) == SQLITE_ROW;
    }
    std::string string(int at) const
    {
        const auto *value = sqlite3_column_text(stmt, at);
        return value == nullptr ? std::string() : reinterpret_cast<const char *>(value);
    }
    sqlite3_stmt *stmt = nullptr;
};

bool valid_text(std::string_view text, std::size_t max)
{
    return !text.empty() && text.size() <= max &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char c) { return c < 32 || c == 127; });
}

bool valid_resume(const iptv::ChannelView &channel, std::string_view series)
{
    std::string canonical;
    const auto optional = [](std::string_view value, std::size_t limit)
    { return value.empty() || valid_text(value, limit); };
    // Playlist shows use category + newline + title as their internal series key.
    const auto separator = series.find('\n');
    const bool series_ok = separator == std::string_view::npos
                               ? optional(series, 256)
                               : optional(series.substr(0, separator), 2048) &&
                                     valid_text(series.substr(separator + 1), 2048);
    if (!valid_text(channel.id, 1024) || !valid_text(channel.name, 2048) ||
        channel.url.size() > 4096 || !iptv::CanonicalizeStreamUrl(channel.url, &canonical) ||
        !optional(channel.tvg_logo, 4096) || !optional(channel.group_title, 2048) || !series_ok ||
        !optional(channel.http_user_agent, 4096) || !optional(channel.http_referrer, 4096) ||
        !optional(channel.tvg_id, 2048) || channel.alternate_urls.size() > 3 ||
        channel.alternate_group_titles.size() > 4)
        return false;
    for (auto value : channel.alternate_urls)
        if (value.size() > 4096 || !iptv::CanonicalizeStreamUrl(value, &canonical))
            return false;
    for (auto value : channel.alternate_group_titles)
        if (!optional(value, 2048))
            return false;
    return true;
}

// One resume row: the twenty saved columns, then the position and duration
// joined from playback_progress. Empty when the row no longer makes sense.
std::optional<ResumeItem> read_resume(const Statement &query, std::uint64_t fingerprint)
{
    ResumeItem item;
    auto &channel = item.channel;
    channel.id = query.string(0);
    channel.source_id = fingerprint;
    channel.name = query.string(4);
    channel.url = query.string(5);
    channel.tvg_logo = query.string(6);
    channel.group_title = query.string(7);
    channel.adult = sqlite3_column_int(query.stmt, 8) != 0;
    item.series = query.string(9);
    channel.http_user_agent = query.string(10);
    channel.http_referrer = query.string(11);
    channel.tvg_id = query.string(12);
    channel.media_kind = item.series.empty() ? iptv::MediaKind::movie : iptv::MediaKind::episode;
    for (int i = 13; i < 16; ++i)
        if (auto value = query.string(i); !value.empty())
            channel.alternate_urls.push_back(std::move(value));
    for (int i = 16; i < 20; ++i)
        if (auto value = query.string(i); !value.empty())
            channel.alternate_group_titles.push_back(std::move(value));
    const auto position = sqlite3_column_int64(query.stmt, 20);
    const auto duration = sqlite3_column_int64(query.stmt, 21);
    if (position < 0 || duration < 0 || position > INT64_MAX / 4 || duration > INT64_MAX / 4 ||
        !valid_resume(channel, item.series))
        return std::nullopt;
    item.position_us = static_cast<std::uint64_t>(position);
    item.duration_us = static_cast<std::uint64_t>(duration);
    return item;
}

} // namespace

int account_expiry_notice(const AccountInfo &info, std::uint64_t now)
{
    if (!now || info.expires_unix <= 0)
        return 0;
    const auto expiry = static_cast<std::uint64_t>(info.expires_unix);
    if (expiry <= now)
        return -1;
    const auto remaining = expiry - now;
    return remaining <= 7u * 86400u ? static_cast<int>((remaining + 86399u) / 86400u) : 0;
}

bool valid_source(const SavedSource &source)
{
    if (source.id < 0 || source.kind < 0 || source.kind > 5 || !valid_text(source.name, 128) ||
        source.schedule < RefreshSchedule::daily || source.schedule > RefreshSchedule::manual)
        return false;
    std::string referer_url;
    if (!iptv::http::ParseExtraHeaders(source.extra_headers))
        return false;
    if ((!source.user_agent.empty() && !valid_text(source.user_agent, 512)) ||
        (!source.referrer.empty() && (!valid_text(source.referrer, 2048) ||
                                      !iptv::CanonicalizeStreamUrl(source.referrer, &referer_url))))
        return false;
    if (source.kind == 0)
        return source.id == 1;
    // An original source slot may remain unconfigured after migration.
    if (source.kind <= 3 && source.id == source.kind + 1 && source.url.empty())
        return source.username.empty() && source.password.empty() && source.mac.empty();
    if (source.kind == 1 && usb_playlist_path(source.url))
        return source.username.empty() && source.password.empty() && source.mac.empty();
    std::string normalized;
    if (!iptv::CanonicalizeStreamUrl(source.url, &normalized) || source.url.size() > 4096)
        return false;
    if (source.kind == 2)
        return iptv::ValidateXtreamCredentials({source.url, source.username, source.password});
    if (source.kind == 5)
        return (source.username.empty() ? source.password.empty()
                                        : valid_text(source.username, 255) &&
                                              source.username.find(':') == std::string::npos) &&
               (source.password.empty() || valid_text(source.password, 255));
    if (source.kind == 3)
    {
        if (source.mac.size() != 17)
            return false;
        for (std::size_t i = 0; i < source.mac.size(); ++i)
        {
            const char c = source.mac[i];
            if (i % 3 == 2
                    ? c != ':'
                    : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return false;
        }
    }
    return true;
}
bool refresh_due(RefreshSchedule schedule, std::uint64_t saved, std::uint64_t now)
{
    if (saved == 0)
        return true;
    if (schedule == RefreshSchedule::manual || now == 0 || now < saved)
        return false;
    const std::uint64_t interval = schedule == RefreshSchedule::weekly ? 7u * 86400u : 86400u;
    return now - saved >= interval;
}

const char *schedule_name(RefreshSchedule schedule)
{
    switch (schedule)
    {
    case RefreshSchedule::weekly:
        return "Weekly";
    case RefreshSchedule::manual:
        return "Only when asked";
    default:
        return "Daily";
    }
}

Library::~Library()
{
    close();
}

bool Library::close()
{
    if (sqlite3_close(db_) != SQLITE_OK)
        return false;
    db_ = nullptr;
    return true;
}

bool Library::open(const std::string &path)
{
    if (db_ != nullptr)
        return true;
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }
    constexpr const char *schema =
        "PRAGMA foreign_keys=ON; BEGIN;"
        "CREATE TABLE IF NOT EXISTS channel_history (source INTEGER NOT NULL, channel TEXT NOT "
        "NULL, first_seen INTEGER NOT NULL, watches INTEGER NOT NULL DEFAULT 0, PRIMARY "
        "KEY(source,channel));"
        "CREATE TABLE IF NOT EXISTS sources (id INTEGER PRIMARY KEY,kind INTEGER NOT NULL,"
        "name TEXT NOT NULL,url TEXT NOT NULL,username TEXT NOT NULL,password TEXT NOT NULL,"
        "mac TEXT NOT NULL,schedule INTEGER NOT NULL CHECK(schedule BETWEEN 0 AND 2));"
        "CREATE TABLE IF NOT EXISTS source_extra_headers (source INTEGER PRIMARY KEY REFERENCES "
        "sources(id)"
        " ON DELETE CASCADE,headers TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS source_http (source INTEGER PRIMARY KEY REFERENCES sources(id)"
        " ON DELETE CASCADE,user_agent TEXT NOT NULL,referrer TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS account_info (source INTEGER PRIMARY KEY REFERENCES sources(id)"
        " ON DELETE CASCADE,expires INTEGER NOT NULL,connections INTEGER NOT NULL,checked INTEGER "
        "NOT NULL);"
        "CREATE TRIGGER IF NOT EXISTS account_source_changed AFTER UPDATE OF "
        "kind,url,username,password ON sources "
        "WHEN OLD.kind!=NEW.kind OR OLD.url!=NEW.url OR OLD.username!=NEW.username OR "
        "OLD.password!=NEW.password "
        "BEGIN DELETE FROM account_info WHERE source=OLD.id; END;"
        "CREATE TABLE IF NOT EXISTS source_guide (source INTEGER PRIMARY KEY REFERENCES "
        "sources(id) "
        "ON DELETE CASCADE,url TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS guide_mapping (source INTEGER REFERENCES sources(id) ON DELETE "
        "CASCADE,channel TEXT NOT NULL,guide_id TEXT NOT NULL,PRIMARY KEY(source,channel));"
        "CREATE TRIGGER IF NOT EXISTS guide_source_changed AFTER UPDATE OF "
        "kind,url,username,password,mac "
        "ON sources WHEN OLD.kind!=NEW.kind OR OLD.url!=NEW.url OR OLD.username!=NEW.username OR "
        "OLD.password!=NEW.password OR OLD.mac!=NEW.mac BEGIN DELETE FROM source_guide WHERE "
        "source=OLD.id; "
        "DELETE FROM guide_mapping WHERE source=OLD.id; END;"
        "CREATE TRIGGER IF NOT EXISTS history_source_removed AFTER DELETE ON sources BEGIN "
        "DELETE FROM channel_history WHERE source=OLD.id; END;"
        "CREATE TABLE IF NOT EXISTS selection (singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "source INTEGER REFERENCES sources(id) ON DELETE SET NULL);"
        "CREATE TABLE IF NOT EXISTS last_channel (singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "source INTEGER REFERENCES sources(id) ON DELETE SET NULL,channel TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS hidden_categories (source TEXT NOT NULL,category TEXT NOT NULL,"
        "PRIMARY KEY(source,category));"
        "CREATE TABLE IF NOT EXISTS channel_names (source TEXT NOT NULL,channel TEXT NOT NULL,"
        "name TEXT NOT NULL,PRIMARY KEY(source,channel));"
        "CREATE TABLE IF NOT EXISTS channel_order (source TEXT NOT NULL,favorites INTEGER NOT NULL,"
        "channel TEXT NOT NULL,position INTEGER NOT NULL,PRIMARY KEY(source,favorites,channel));"
        "CREATE TABLE IF NOT EXISTS hidden_channels (source TEXT NOT NULL,channel TEXT NOT NULL,"
        "PRIMARY KEY(source,channel));"
        "CREATE TABLE IF NOT EXISTS category_rules (source TEXT NOT NULL,category TEXT NOT NULL,"
        "rule INTEGER NOT NULL CHECK(rule IN(1,2)),PRIMARY KEY(source,category));"
        "CREATE TABLE IF NOT EXISTS playback_progress (channel TEXT PRIMARY KEY,"
        "position INTEGER NOT NULL CHECK(position>=0),duration INTEGER NOT NULL "
        "CHECK(duration>=0));"
        "CREATE TABLE IF NOT EXISTS resume_items (channel TEXT PRIMARY KEY,"
        "source INTEGER NOT NULL REFERENCES sources(id) ON DELETE CASCADE,fingerprint TEXT NOT "
        "NULL,"
        "updated INTEGER NOT NULL,name TEXT NOT NULL,url TEXT NOT NULL,logo TEXT NOT NULL,"
        "category TEXT NOT NULL,adult INTEGER NOT NULL,series TEXT NOT NULL,agent TEXT NOT NULL,"
        "referrer TEXT NOT NULL,tvg_id TEXT NOT NULL,url1 TEXT NOT NULL,url2 TEXT NOT NULL,"
        "url3 TEXT NOT NULL,group1 TEXT NOT NULL,group2 TEXT NOT NULL,group3 TEXT NOT NULL,"
        "group4 TEXT NOT NULL);"
        "CREATE TRIGGER IF NOT EXISTS resume_source_changed AFTER UPDATE ON sources "
        "WHEN OLD.kind<>NEW.kind OR OLD.url<>NEW.url OR OLD.username<>NEW.username OR "
        "OLD.password<>NEW.password OR OLD.mac<>NEW.mac BEGIN "
        "DELETE FROM resume_items WHERE source=OLD.id; END;"
        "CREATE TABLE IF NOT EXISTS folders (name TEXT PRIMARY KEY);"
        "CREATE TABLE IF NOT EXISTS folder_channels (folder TEXT NOT NULL REFERENCES folders(name)"
        " ON DELETE CASCADE ON UPDATE CASCADE,channel TEXT NOT NULL,PRIMARY KEY(folder,channel));"
        "COMMIT;";
    if (sqlite3_exec(db, schema, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }
    db_ = db;
    return true;
}

std::map<std::string, std::string, std::less<>> Library::channel_names(std::uint64_t source) const
{
    std::map<std::string, std::string, std::less<>> result;
    Statement query(db_, "SELECT channel,name FROM channel_names WHERE source=?1");
    if (query.text(1, std::to_string(source)))
        while (query.row())
        {
            auto id = query.string(0);
            auto name = query.string(1);
            if (valid_text(id, 256) && valid_text(name, 256))
                result.emplace(std::move(id), std::move(name));
        }
    return result;
}

bool Library::rename_channel(std::uint64_t source, std::string_view channel, std::string_view name)
{
    if (!valid_text(channel, 256) || (!name.empty() && !valid_text(name, 256)))
        return false;
    Statement query(db_, name.empty()
                             ? "DELETE FROM channel_names WHERE source=?1 AND channel=?2"
                             : "INSERT INTO channel_names VALUES(?1,?2,?3) ON "
                               "CONFLICT(source,channel) DO UPDATE SET name=excluded.name");
    return query.text(1, std::to_string(source)) && query.text(2, channel) &&
           (name.empty() || query.text(3, name)) && query.done();
}

std::unordered_set<std::string> Library::hidden_channels(std::uint64_t source) const
{
    std::unordered_set<std::string> result;
    Statement query(db_, "SELECT channel FROM hidden_channels WHERE source=?1");
    if (query.text(1, std::to_string(source)))
        while (query.row())
        {
            auto id = query.string(0);
            if (valid_text(id, 256))
                result.insert(std::move(id));
        }
    return result;
}

bool Library::hide_channel(std::uint64_t source, std::string_view channel, bool hidden)
{
    if (!valid_text(channel, 256))
        return false;
    Statement query(db_, hidden ? "INSERT OR IGNORE INTO hidden_channels VALUES(?1,?2)"
                                : "DELETE FROM hidden_channels WHERE source=?1 AND channel=?2");
    return query.text(1, std::to_string(source)) && query.text(2, channel) && query.done();
}

std::vector<std::string> Library::channel_order(std::uint64_t source, bool favorites) const
{
    std::vector<std::string> result;
    Statement query(db_, "SELECT channel FROM channel_order WHERE source=?1 AND favorites=?2 ORDER "
                         "BY position,channel");
    if (query.text(1, std::to_string(source)) && query.integer(2, favorites))
        while (query.row())
            result.push_back(query.string(0));
    return result;
}

bool Library::save_channel_order(std::uint64_t source, bool favorites,
                                 const std::vector<std::string> &ids)
{
    if (!db_ || ids.size() > 250000 ||
        std::any_of(ids.begin(), ids.end(), [](const auto &id) { return !valid_text(id, 256); }) ||
        sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    const auto key = std::to_string(source);
    Statement clear(db_, "DELETE FROM channel_order WHERE source=?1 AND favorites=?2");
    bool ok = clear.text(1, key) && clear.integer(2, favorites) && clear.done();
    Statement row(db_, "INSERT INTO channel_order VALUES(?1,?2,?3,?4)");
    for (std::size_t i = 0; ok && i < ids.size(); ++i)
    {
        ok = row.text(1, key) && row.integer(2, favorites) && row.text(3, ids[i]) &&
             row.integer(4, static_cast<std::int64_t>(i)) && row.done();
        if (row.stmt)
            sqlite3_reset(row.stmt);
    }
    if (ok && sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
}

std::vector<SavedSource> Library::sources() const
{
    std::vector<SavedSource> result;
    Statement query(
        db_, "SELECT id,kind,name,url,username,password,mac,schedule FROM sources ORDER BY id");
    while (query.row())
    {
        SavedSource source{sqlite3_column_int64(query.stmt, 0),
                           sqlite3_column_int(query.stmt, 1),
                           query.string(2),
                           query.string(3),
                           query.string(4),
                           query.string(5),
                           query.string(6),
                           static_cast<RefreshSchedule>(sqlite3_column_int(query.stmt, 7))};
        Statement extra(db_, "SELECT headers FROM source_extra_headers WHERE source=?1");
        if (extra.integer(1, source.id) && extra.row())
            source.extra_headers = extra.string(0);
        Statement http(db_, "SELECT user_agent,referrer FROM source_http WHERE source=?1");
        if (http.integer(1, source.id) && http.row())
        {
            source.user_agent = http.string(0);
            source.referrer = http.string(1);
        }
        if (valid_source(source))
        {
            if (source.kind == 2)
            {
                Statement info(
                    db_, "SELECT expires,connections,checked FROM account_info WHERE source=?1");
                if (info.integer(1, source.id) && info.row())
                    source.account = {sqlite3_column_int64(info.stmt, 0),
                                      sqlite3_column_int64(info.stmt, 1),
                                      sqlite3_column_int64(info.stmt, 2)};
            }
            result.push_back(std::move(source));
        }
    }
    return result;
}

bool Library::save_account_info(std::int64_t source, const AccountInfo &info)
{
    if (info.expires_unix < -1 || info.expires_unix > INT64_C(253402300799) ||
        info.max_connections < -1 || info.max_connections > 1000000 || info.checked_unix <= 0)
        return false;
    Statement query(
        db_, "INSERT INTO account_info SELECT id,?2,?3,?4 FROM sources WHERE id=?1 AND kind=2 "
             "ON CONFLICT(source) DO UPDATE SET expires=excluded.expires,"
             "connections=excluded.connections,checked=excluded.checked");
    return query.integer(1, source) && query.integer(2, info.expires_unix) &&
           query.integer(3, info.max_connections) && query.integer(4, info.checked_unix) &&
           query.done() && sqlite3_changes(db_) == 1;
}

bool Library::save_source(SavedSource *source)
{
    if (source == nullptr || !valid_source(*source))
        return false;
    if (sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    Statement query(
        db_,
        "INSERT INTO sources VALUES(NULLIF(?1,0),?2,?3,?4,?5,?6,?7,?8)"
        " ON CONFLICT(id) DO UPDATE SET kind=excluded.kind,name=excluded.name,url=excluded.url,"
        "username=excluded.username,password=excluded.password,mac=excluded.mac,schedule=excluded."
        "schedule");
    bool saved = query.integer(1, source->id) && query.integer(2, source->kind) &&
                 query.text(3, source->name) && query.text(4, source->url) &&
                 query.text(5, source->username) && query.text(6, source->password) &&
                 query.text(7, source->mac) &&
                 query.integer(8, static_cast<int>(source->schedule)) && query.done();
    const auto id = source->id ? source->id : sqlite3_last_insert_rowid(db_);
    Statement http(db_, "INSERT INTO source_http VALUES(?1,?2,?3) ON CONFLICT(source) "
                        "DO UPDATE SET user_agent=excluded.user_agent,referrer=excluded.referrer");
    saved = saved && http.integer(1, id) && http.text(2, source->user_agent) &&
            http.text(3, source->referrer) && http.done();
    Statement extra(db_, "INSERT INTO source_extra_headers VALUES(?1,?2) ON CONFLICT(source) "
                         "DO UPDATE SET headers=excluded.headers");
    saved = saved && extra.integer(1, id) && extra.text(2, source->extra_headers) && extra.done();
    if (saved && sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK)
    {
        source->id = id;
        return true;
    }
    (void)sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
}

bool Library::remove_source(std::int64_t id)
{
    if (id <= 4)
        return false;
    Statement query(db_, "DELETE FROM sources WHERE id=?1");
    return query.integer(1, id) && query.done();
}

std::int64_t Library::selected_source() const
{
    Statement query(db_, "SELECT source FROM selection WHERE singleton=1");
    return query.row() ? sqlite3_column_int64(query.stmt, 0) : 0;
}

std::pair<std::uint64_t, std::uint64_t> Library::playback_progress(std::string_view channel) const
{
    Statement query(db_, "SELECT position,duration FROM playback_progress WHERE channel=?1");
    if (!valid_text(channel, 1024) || !query.text(1, channel) || !query.row())
        return {};
    const auto position = sqlite3_column_int64(query.stmt, 0);
    const auto duration = sqlite3_column_int64(query.stmt, 1);
    if (position < 0 || duration < 0 || position > INT64_MAX / 4 || duration > INT64_MAX / 4 ||
        (duration && position >= duration))
        return {};
    return {static_cast<std::uint64_t>(position), static_cast<std::uint64_t>(duration)};
}

bool Library::save_playback_progress(std::string_view channel, std::uint64_t position_us,
                                     std::uint64_t duration_us)
{
    if (!valid_text(channel, 1024) || position_us > INT64_MAX / 4 || duration_us > INT64_MAX / 4 ||
        (duration_us && position_us >= duration_us))
        return false;
    if (!position_us)
    {
        Statement query(db_, "DELETE FROM playback_progress WHERE channel=?1");
        return query.text(1, channel) && query.done();
    }
    Statement query(db_, "INSERT INTO playback_progress VALUES(?1,?2,?3) ON CONFLICT(channel)"
                         " DO UPDATE SET position=excluded.position,duration=excluded.duration");
    return query.text(1, channel) && query.integer(2, position_us) &&
           query.integer(3, duration_us) && query.done();
}

bool Library::remember_vod(std::int64_t source, std::uint64_t fingerprint,
                           const iptv::ChannelView &channel, std::string_view series,
                           std::int64_t updated)
{
    if (source <= 0 || !fingerprint || updated < 0 || !valid_resume(channel, series))
        return false;
    if (sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    Statement put(db_, "INSERT OR REPLACE INTO resume_items VALUES("
                       "?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17,?18,?19,?20)");
    bool ok = put.text(1, channel.id) && put.integer(2, source) &&
              put.text(3, std::to_string(fingerprint)) && put.integer(4, updated) &&
              put.text(5, channel.name) && put.text(6, channel.url) &&
              put.text(7, channel.tvg_logo) && put.text(8, channel.group_title) &&
              put.integer(9, channel.adult) && put.text(10, series) &&
              put.text(11, channel.http_user_agent) && put.text(12, channel.http_referrer) &&
              put.text(13, channel.tvg_id);
    for (unsigned i = 0; i < 3; ++i)
        ok = ok && put.text(14 + i, i < channel.alternate_urls.size() ? channel.alternate_urls[i]
                                                                      : std::string_view());
    for (unsigned i = 0; i < 4; ++i)
        ok = ok && put.text(17 + i, i < channel.alternate_group_titles.size()
                                        ? channel.alternate_group_titles[i]
                                        : std::string_view());
    ok = ok && put.done();
    // Keep at most fifty snapshots per local profile.
    ok = ok && sqlite3_exec(
                   db_,
                   "DELETE FROM resume_items WHERE channel NOT IN "
                   "(SELECT channel FROM resume_items ORDER BY updated DESC,rowid DESC LIMIT 50)",
                   nullptr, nullptr, nullptr) == SQLITE_OK;
    if (ok && sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return false;
}
std::vector<ResumeItem> Library::unfinished_vod(std::int64_t source,
                                                std::uint64_t fingerprint) const
{
    std::vector<ResumeItem> result;
    Statement query(db_,
                    "SELECT r.*,p.position,p.duration FROM resume_items r "
                    "JOIN playback_progress p ON p.channel=r.channel WHERE r.source=?1 AND "
                    "r.fingerprint=?2 AND p.position>0 AND (p.duration=0 OR p.position<p.duration) "
                    "ORDER BY r.updated DESC,r.rowid DESC LIMIT 50");
    if (!query.integer(1, source) || !query.text(2, std::to_string(fingerprint)))
        return result;
    while (query.row())
        if (auto item = read_resume(query, fingerprint))
            result.push_back(std::move(*item));
    return result;
}

std::optional<ResumeItem> Library::last_series_episode(std::int64_t source,
                                                       std::uint64_t fingerprint,
                                                       std::string_view series) const
{
    // A finished episode keeps its row and loses its position, so the position
    // is left-joined: 0 means "from the start".
    Statement query(db_, "SELECT r.*,IFNULL(p.position,0),IFNULL(p.duration,0) FROM resume_items r "
                         "LEFT JOIN playback_progress p ON p.channel=r.channel WHERE r.source=?1 "
                         "AND r.fingerprint=?2 AND r.series=?3 ORDER BY r.updated DESC,r.rowid "
                         "DESC LIMIT 1");
    if (series.empty() || !query.integer(1, source) ||
        !query.text(2, std::to_string(fingerprint)) || !query.text(3, series) || !query.row())
        return std::nullopt;
    return read_resume(query, fingerprint);
}

bool Library::select_source(std::int64_t id)
{
    Statement query(db_, "INSERT INTO selection VALUES(1,?1) ON CONFLICT(singleton) DO UPDATE SET "
                         "source=excluded.source");
    return query.integer(1, id) && query.done();
}

bool Library::remember_channel(std::int64_t source, std::string_view channel)
{
    if (!valid_text(channel, 256))
        return false;
    Statement query(db_, "INSERT INTO last_channel VALUES(1,?1,?2) ON CONFLICT(singleton)"
                         " DO UPDATE SET source=excluded.source,channel=excluded.channel");
    return query.integer(1, source) && query.text(2, channel) && query.done();
}

std::pair<std::int64_t, std::string> Library::last_channel() const
{
    Statement query(db_, "SELECT source,channel FROM last_channel WHERE singleton=1");
    if (!query.row())
        return {};
    return {sqlite3_column_int64(query.stmt, 0), query.string(1)};
}

std::unordered_set<std::string> Library::hidden_categories(std::uint64_t source) const
{
    std::unordered_set<std::string> result;
    Statement query(db_, "SELECT category FROM hidden_categories WHERE source=?1");
    if (query.text(1, std::to_string(source)))
        while (query.row())
            result.insert(query.string(0));
    return result;
}

bool Library::hide_category(std::uint64_t source, std::string_view category, bool hidden)
{
    if (!valid_text(category, iptv::kDefaultMaxFieldBytes))
        return false;
    Statement query(db_, hidden ? "INSERT OR IGNORE INTO hidden_categories VALUES(?1,?2)"
                                : "DELETE FROM hidden_categories WHERE source=?1 AND category=?2");
    return query.text(1, std::to_string(source)) && query.text(2, category) && query.done();
}

std::vector<std::string> Library::folders() const
{
    std::vector<std::string> result;
    Statement query(db_, "SELECT name FROM folders ORDER BY name COLLATE NOCASE,name");
    while (query.row())
        result.push_back(query.string(0));
    return result;
}

std::map<std::string, int> Library::category_rules(std::uint64_t source) const
{
    std::map<std::string, int> result;
    Statement query(db_, "SELECT category,rule FROM category_rules WHERE source=?1");
    if (query.text(1, std::to_string(source)))
        while (query.row())
            result.emplace(query.string(0), sqlite3_column_int(query.stmt, 1));
    return result;
}

bool Library::set_category_rule(std::uint64_t source, std::string_view category, int rule)
{
    if (!valid_text(category, iptv::kDefaultMaxFieldBytes) || rule < 0 || rule > 2)
        return false;
    Statement query(db_, rule == 0 ? "DELETE FROM category_rules WHERE source=?1 AND category=?2"
                                   : "INSERT OR REPLACE INTO category_rules VALUES(?1,?2,?3)");
    return query.text(1, std::to_string(source)) && query.text(2, category) &&
           (rule == 0 || query.integer(3, rule)) && query.done();
}

bool Library::add_folder(std::string_view name)
{
    if (!valid_text(name, 128))
        return false;
    Statement query(db_, "INSERT INTO folders VALUES(?1)");
    return query.text(1, name) && query.done();
}

bool Library::rename_folder(std::string_view name, std::string_view replacement)
{
    if (!valid_text(replacement, 128))
        return false;
    Statement query(db_, "UPDATE folders SET name=?2 WHERE name=?1");
    return query.text(1, name) && query.text(2, replacement) && query.done() &&
           sqlite3_changes(db_) == 1;
}

bool Library::remove_folder(std::string_view name)
{
    Statement query(db_, "DELETE FROM folders WHERE name=?1");
    return query.text(1, name) && query.done();
}

std::unordered_set<std::string> Library::folder_channels(std::string_view folder) const
{
    std::unordered_set<std::string> result;
    Statement query(db_, "SELECT channel FROM folder_channels WHERE folder=?1");
    if (query.text(1, folder))
        while (query.row())
            result.insert(query.string(0));
    return result;
}

bool Library::put_in_folder(std::string_view folder, std::string_view channel, bool included)
{
    if (!valid_text(channel, 256))
        return false;
    Statement query(db_, included ? "INSERT OR IGNORE INTO folder_channels VALUES(?1,?2)"
                                  : "DELETE FROM folder_channels WHERE folder=?1 AND channel=?2");
    return query.text(1, folder) && query.text(2, channel) && query.done();
}

bool Library::remove_from_folders(std::string_view channel)
{
    Statement query(db_, "DELETE FROM folder_channels WHERE channel=?1");
    return query.text(1, channel) && query.done();
}
} // namespace ptv

namespace ptv
{
std::string Library::guide_url(std::int64_t source) const
{
    Statement query(db_, "SELECT url FROM source_guide WHERE source=?1 AND length(url)<=4096");
    return query.integer(1, source) && query.row() ? query.string(0) : std::string();
}
std::map<std::string, std::string> Library::guide_mapping(std::int64_t source) const
{
    std::map<std::string, std::string> result;
    Statement query(
        db_, "SELECT channel,guide_id FROM guide_mapping WHERE source=?1 "
             "AND length(channel)<=256 AND length(guide_id)<=256 ORDER BY channel LIMIT 4096");
    if (query.integer(1, source))
        while (query.row())
            result.emplace(query.string(0), query.string(1));
    return result;
}
bool Library::set_guide_url(std::int64_t source, std::string_view url)
{
    std::string canonical;
    if (!url.empty() && (url.size() > 4096 || !iptv::CanonicalizeStreamUrl(url, &canonical)))
        return false;
    Statement change(db_, "INSERT INTO source_guide(source,url) VALUES(?1,?2) "
                          "ON CONFLICT(source) DO UPDATE SET url=excluded.url");
    return change.integer(1, source) &&
           change.text(2, url.empty() ? std::string_view("") : canonical) && change.done();
}
bool Library::set_guide_mapping(std::int64_t source, std::string_view channel,
                                std::string_view guide_id)
{
    if (!valid_text(channel, 256) || (!guide_id.empty() && !valid_text(guide_id, 256)))
        return false;
    if (guide_id.empty())
    {
        Statement erase(db_, "DELETE FROM guide_mapping WHERE source=?1 AND channel=?2");
        return erase.integer(1, source) && erase.text(2, channel) && erase.done();
    }
    Statement change(db_, "INSERT INTO guide_mapping(source,channel,guide_id) SELECT ?1,?2,?3 "
                          "WHERE (SELECT count(*) FROM guide_mapping WHERE source=?1)<4096 OR "
                          "EXISTS(SELECT 1 FROM guide_mapping WHERE source=?1 AND channel=?2) "
                          "ON CONFLICT(source,channel) DO UPDATE SET guide_id=excluded.guide_id");
    return change.integer(1, source) && change.text(2, channel) && change.text(3, guide_id) &&
           change.done() && sqlite3_changes(db_) != 0;
}
} // namespace ptv
