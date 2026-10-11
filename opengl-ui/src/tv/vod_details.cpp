// ProsperoTV - Provider metadata stored beside each on-demand catalog.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/vod.hpp"
#include <sqlite3.h>
#include <algorithm>
namespace ptv
{
std::vector<std::string> vod_genres(std::string_view value)
{
    std::vector<std::string> out;
    while (!value.empty() && out.size() < 32)
    {
        auto end = value.find_first_of(",/;|");
        auto part = value.substr(0, end);
        while (!part.empty() && part.front() == ' ')
            part.remove_prefix(1);
        while (!part.empty() && part.back() == ' ')
            part.remove_suffix(1);
        if (!part.empty() && std::find(out.begin(), out.end(), part) == out.end())
            out.emplace_back(part);
        if (end == std::string_view::npos)
            break;
        value.remove_prefix(end + 1);
    }
    return out;
}
bool save_vod_metadata(const std::string &path, const VodMetadata &metadata)
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
    {
        sqlite3_close(db);
        return false;
    }
    bool ok = sqlite3_exec(db,
                           "BEGIN; CREATE TABLE IF NOT EXISTS vod_details(id TEXT PRIMARY KEY,plot "
                           "TEXT,cast_names TEXT,genre TEXT,year TEXT,rating TEXT,poster "
                           "TEXT,complete INTEGER); DELETE FROM vod_details;",
                           nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_stmt *stmt = nullptr;
    ok = ok && sqlite3_prepare_v2(db, "INSERT INTO vod_details VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", -1,
                                  &stmt, nullptr) == SQLITE_OK;
    for (const auto &[id, item] : metadata)
    {
        if (!ok)
            break;
        sqlite3_reset(stmt);
        const std::string *fields[] = {&id,        &item.plot,   &item.cast,  &item.genre,
                                       &item.year, &item.rating, &item.poster};
        for (int i = 0; ok && i < 7; ++i)
            ok = sqlite3_bind_text(stmt, i + 1, fields[i]->data(),
                                   static_cast<int>(fields[i]->size()),
                                   SQLITE_TRANSIENT) == SQLITE_OK;
        ok = ok && sqlite3_bind_int(stmt, 8, item.complete ? 1 : 0) == SQLITE_OK &&
             sqlite3_step(stmt) == SQLITE_DONE;
    }
    sqlite3_finalize(stmt);
    if (ok)
        ok = sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok)
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return ok;
}
VodMetadata load_vod_metadata(const std::string &path, const iptv::Catalog &catalog)
{
    VodMetadata result;
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        sqlite3_close(db);
        return result;
    }
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 16384);
    sqlite3_stmt *stmt = nullptr;
    std::size_t bytes = 0;
    if (sqlite3_prepare_v2(
            db, "SELECT id,plot,cast_names,genre,year,rating,poster,complete FROM vod_details", -1,
            &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW && result.size() < catalog.size())
        {
            std::string fields[7];
            bool valid = true;
            const int limits[] = {256, 4096, 1024, 256, 4, 16, 4096};
            std::size_t size = 0;
            for (int i = 0; i < 7; ++i)
            {
                const auto *text = sqlite3_column_text(stmt, i);
                const int count = sqlite3_column_bytes(stmt, i);
                if (count > limits[i])
                {
                    valid = false;
                    break;
                }
                if (text)
                    fields[i].assign(reinterpret_cast<const char *>(text), count);
                size += count;
            }
            if (!valid || catalog.Find(fields[0]) == iptv::Catalog::npos)
                continue;
            if (size > 32u * 1024u * 1024u - bytes)
                break;
            bytes += size;
            result.emplace(std::move(fields[0]),
                           VodDetails{std::move(fields[1]), std::move(fields[2]),
                                      std::move(fields[3]), std::move(fields[4]),
                                      std::move(fields[5]), std::move(fields[6]),
                                      sqlite3_column_int(stmt, 7) == 1});
        }
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return result;
}
} // namespace ptv
