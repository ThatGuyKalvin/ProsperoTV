// ProsperoTV - Profile-local first observation and successful watch counts.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/library.hpp"
#include "iptv_catalog.h"
#include <sqlite3.h>
#include <algorithm>
#include <charconv>

namespace ptv
{
bool completed_watch(std::string_view receipt, std::int64_t seconds, int result)
{
    if (seconds < 10 || result < 0)
        return false;
    while (!receipt.empty())
    {
        const auto end = receipt.find('\n');
        auto line = receipt.substr(0, end);
        constexpr std::string_view key = "presented_frames=";
        if (line.starts_with(key))
        {
            line.remove_prefix(key.size());
            if (!line.empty() && line.back() == '\r')
                line.remove_suffix(1);
            std::uint64_t frames = 0;
            const auto parsed = std::from_chars(line.data(), line.data() + line.size(), frames);
            return parsed.ec == std::errc{} && parsed.ptr == line.data() + line.size() &&
                   frames > 0;
        }
        if (end == std::string_view::npos)
            break;
        receipt.remove_prefix(end + 1);
    }
    return false;
}
bool Library::observe_channels(std::int64_t source, const iptv::Catalog &catalog, std::int64_t now)
{
    if (!db_ || source <= 0 || now <= 0 ||
        sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok =
        sqlite3_prepare_v2(
            db_,
            "INSERT OR IGNORE INTO channel_history(source,channel,first_seen) VALUES(?1,?2,?3)", -1,
            &stmt, nullptr) == SQLITE_OK;
    for (std::size_t i = 0; ok && i < catalog.size(); ++i)
    {
        const auto channel = catalog[i];
        if (channel.media_kind != iptv::MediaKind::live)
            continue;
        sqlite3_reset(stmt);
        ok = sqlite3_bind_int64(stmt, 1, source) == SQLITE_OK &&
             sqlite3_bind_text(stmt, 2, channel.id.data(), static_cast<int>(channel.id.size()),
                               SQLITE_TRANSIENT) == SQLITE_OK &&
             sqlite3_bind_int64(stmt, 3, now) == SQLITE_OK && sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    if (ok)
        ok = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok)
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    return ok;
}
std::vector<ChannelHistory> Library::channel_history(std::int64_t source,
                                                     const iptv::Catalog &catalog) const
{
    std::vector<ChannelHistory> result(catalog.size());
    sqlite3_stmt *stmt = nullptr;
    if (db_ &&
        sqlite3_prepare_v2(db_,
                           "SELECT channel,first_seen,watches FROM channel_history WHERE source=?1",
                           -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_bind_int64(stmt, 1, source) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const auto *id = sqlite3_column_text(stmt, 0);
            const auto index =
                id ? catalog.Find(reinterpret_cast<const char *>(id)) : iptv::Catalog::npos;
            if (index != iptv::Catalog::npos)
                result[index] = {std::max<std::int64_t>(0, sqlite3_column_int64(stmt, 1)),
                                 std::max<std::int64_t>(0, sqlite3_column_int64(stmt, 2))};
        }
    }
    sqlite3_finalize(stmt);
    return result;
}
bool Library::record_watch(std::int64_t source, std::string_view channel)
{
    if (!db_ || source <= 0 || channel.empty() || channel.size() > 4096)
        return false;
    sqlite3_stmt *stmt = nullptr;
    bool ok = sqlite3_prepare_v2(db_,
                                 "UPDATE channel_history SET watches=MIN(watches+1,2147483647) "
                                 "WHERE source=?1 AND channel=?2",
                                 -1, &stmt, nullptr) == SQLITE_OK &&
              sqlite3_bind_int64(stmt, 1, source) == SQLITE_OK &&
              sqlite3_bind_text(stmt, 2, channel.data(), static_cast<int>(channel.size()),
                                SQLITE_TRANSIENT) == SQLITE_OK &&
              sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) == 1;
    sqlite3_finalize(stmt);
    return ok;
}
} // namespace ptv
