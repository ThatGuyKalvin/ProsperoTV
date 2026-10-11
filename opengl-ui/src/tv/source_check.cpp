// ProsperoTV - One muted probe at a time, without changing watch history.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/source_check.hpp"
#include "tv/preview.hpp"
#include "tv/platform.hpp"
#include <sqlite3.h>
#include <chrono>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>
namespace ptv
{
ProbeResult probe_http(int status, bool entry, bool timed_out)
{
    if (status == 401 || status == 403 || status == 429 || status >= 500)
        return {ProbeOutcome::service, status};
    if (entry && (status == 404 || status == 410))
        return {ProbeOutcome::missing, status};
    return {timed_out ? ProbeOutcome::timeout : ProbeOutcome::unknown, status};
}
ProbeResult combine_probe(ProbeResult prior, ProbeResult next, bool first)
{
    if (first || next.outcome == ProbeOutcome::frame)
        return next;
    if (prior.outcome == ProbeOutcome::frame)
        return prior;
    if (next.outcome == ProbeOutcome::service || next.outcome == ProbeOutcome::cancelled)
        return next;
    if (prior.outcome == ProbeOutcome::missing && next.outcome == ProbeOutcome::missing)
        return next;
    return {ProbeOutcome::unknown, next.http};
}
std::uint64_t check_fingerprint(const iptv::ChannelView &channel)
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto add = [&](std::string_view value)
    {
        for (const unsigned char c : value)
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        hash ^= 255;
        hash *= 1099511628211ull;
    };
    add(channel.url);
    for (auto url : channel.alternate_urls)
        add(url);
    add(channel.http_user_agent);
    add(channel.http_referrer);
    add(channel.portal_command);
    return hash;
}
CheckStore::~CheckStore()
{
    close();
}
bool CheckStore::open(const std::string &path, std::uint64_t source, unsigned capacity)
{
    close();
    capacity_ = std::min(capacity, 250000u);
    struct stat info{};
    if (stat(path.c_str(), &info) == 0 &&
        (info.st_size < 0 || static_cast<std::uint64_t>(info.st_size) > 64u * 1024u * 1024u))
        return false;
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK)
    {
        close();
        return false;
    }
    sqlite3_busy_timeout(db_, 250);
    sqlite3_exec(db_, "PRAGMA page_size=4096", nullptr, nullptr, nullptr);
    sqlite3_stmt *page = nullptr;
    sqlite3_prepare_v2(db_, "PRAGMA page_size", -1, &page, nullptr);
    const int page_size =
        page && sqlite3_step(page) == SQLITE_ROW ? sqlite3_column_int(page, 0) : 0;
    sqlite3_finalize(page);
    if (page_size < 512 || page_size > 65536 || (page_size & (page_size - 1)))
    {
        close();
        return false;
    }
    const std::string schema =
        "PRAGMA max_page_count=" +
        std::to_string(64u * 1024u * 1024u / static_cast<unsigned>(page_size)) +
        ";PRAGMA synchronous=FULL;CREATE TABLE IF NOT EXISTS identity(source INTEGER);"
        "CREATE TABLE IF NOT EXISTS checks(channel TEXT PRIMARY KEY,fingerprint INTEGER NOT "
        "NULL,outcome INTEGER NOT NULL,http INTEGER NOT NULL,checked INTEGER NOT NULL) WITHOUT "
        "ROWID;";
    if (sqlite3_exec(db_, schema.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        close();
        return false;
    }
    sqlite3_stmt *statement = nullptr;
    sqlite3_prepare_v2(db_, "SELECT source FROM identity", -1, &statement, nullptr);
    const bool same = statement && sqlite3_step(statement) == SQLITE_ROW &&
                      static_cast<std::uint64_t>(sqlite3_column_int64(statement, 0)) == source;
    sqlite3_finalize(statement);
    if (!same)
    {
        if (sqlite3_exec(db_, "BEGIN;DELETE FROM checks;DELETE FROM identity;", nullptr, nullptr,
                         nullptr) != SQLITE_OK)
        {
            close();
            return false;
        }
        sqlite3_prepare_v2(db_, "INSERT INTO identity VALUES(?)", -1, &statement, nullptr);
        sqlite3_bind_int64(statement, 1, static_cast<sqlite3_int64>(source));
        const bool ok = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        if (!ok || sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            close();
            return false;
        }
    }
    sqlite3_prepare_v2(db_, "SELECT count(*) FROM checks", -1, &statement, nullptr);
    count_ = statement && sqlite3_step(statement) == SQLITE_ROW
                 ? static_cast<unsigned>(sqlite3_column_int(statement, 0))
                 : capacity_ + 1;
    sqlite3_finalize(statement);
    if (count_ > capacity_)
    {
        close();
        return false;
    }
    return true;
}
bool CheckStore::save(std::string_view channel, std::uint64_t fingerprint, ProbeResult result,
                      std::uint64_t now)
{
    if (!db_ || channel.empty() || channel.size() > 256)
        return false;
    // Inconclusive probes never erase the last confirmed health result.
    if (result.outcome != ProbeOutcome::frame && result.outcome != ProbeOutcome::missing)
        return true;
    if (!now || now > 253402300799ull ||
        (result.outcome == ProbeOutcome::frame ? result.http != 0
                                               : result.http != 404 && result.http != 410))
        return false;
    sqlite3_stmt *statement = nullptr;
    sqlite3_prepare_v2(db_, "SELECT 1 FROM checks WHERE channel=?", -1, &statement, nullptr);
    sqlite3_bind_text(statement, 1, channel.data(), static_cast<int>(channel.size()),
                      SQLITE_TRANSIENT);
    const bool exists = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    if (!exists && count_ >= capacity_)
        return false;
    if (!pending_ && sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
        return false;
    sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO checks VALUES(?,?,?,?,?)", -1, &statement,
                       nullptr);
    sqlite3_bind_text(statement, 1, channel.data(), static_cast<int>(channel.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(fingerprint));
    sqlite3_bind_int(statement, 3, static_cast<int>(result.outcome));
    sqlite3_bind_int(statement, 4, result.http);
    sqlite3_bind_int64(statement, 5, static_cast<sqlite3_int64>(now));
    const bool ok = sqlite3_step(statement) == SQLITE_DONE;
    sqlite3_finalize(statement);
    if (!ok)
    {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        pending_ = 0;
        return false;
    }
    if (!exists)
        ++count_;
    ++pending_;
    return pending_ < 32 && now < flushed_ + 5 ? true : (flushed_ = now, flush());
}
bool CheckStore::flush(std::uint64_t now)
{
    if (!db_ || !pending_ || (now && now < flushed_ + 5))
        return true;
    if (now)
        flushed_ = now;
    const bool ok = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ok)
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    pending_ = 0;
    return ok;
}
void CheckStore::close()
{
    (void)flush();
    if (db_)
        sqlite3_close(db_);
    db_ = nullptr;
    pending_ = count_ = 0;
}
void CheckStore::apply(const std::string &path, iptv::Catalog &catalog, std::uint64_t identity)
{
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || info.st_size < 0 ||
        static_cast<std::uint64_t>(info.st_size) > 64u * 1024u * 1024u)
        return;
    sqlite3 *db = nullptr;
    if (sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
    {
        if (db)
            sqlite3_close(db);
        return;
    }
    sqlite3_stmt *statement = nullptr;
    sqlite3_prepare_v2(db,
                       "SELECT channel,fingerprint,outcome,http,checked FROM checks WHERE (SELECT "
                       "source FROM identity)=? LIMIT 250000",
                       -1, &statement, nullptr);
    if (statement)
    {
        sqlite3_bind_int64(statement, 1,
                           static_cast<sqlite3_int64>(identity ? identity : catalog.source_id));
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            const auto *id = sqlite3_column_text(statement, 0);
            const int id_bytes = sqlite3_column_bytes(statement, 0);
            if (!id || id_bytes <= 0 || id_bytes > 256 ||
                std::memchr(id, 0, static_cast<std::size_t>(id_bytes)))
                continue;
            const auto index = catalog.Find(std::string_view(reinterpret_cast<const char *>(id),
                                                             static_cast<std::size_t>(id_bytes)));
            if (index == iptv::Catalog::npos)
                continue;
            const auto channel = catalog[index];
            const auto stamp = sqlite3_column_int64(statement, 4);
            const auto stored_kind = sqlite3_column_int64(statement, 2);
            const auto status = sqlite3_column_int64(statement, 3);
            const auto now = platform::unix_time();
            if (stored_kind != static_cast<int>(ProbeOutcome::frame) &&
                stored_kind != static_cast<int>(ProbeOutcome::missing))
                continue;
            const auto kind = static_cast<ProbeOutcome>(stored_kind);
            if (status < 0 || status > 599 ||
                (kind == ProbeOutcome::frame ? status != 0 : status != 404 && status != 410) ||
                stamp > 253402300799ll ||
                (now && now <= 253402300799ull && stamp > 0 &&
                 static_cast<std::uint64_t>(stamp) > now + 300))
                continue;
            if (stamp <= 0 || static_cast<std::uint64_t>(stamp) <= channel.playback_checked_unix ||
                static_cast<std::uint64_t>(sqlite3_column_int64(statement, 1)) !=
                    check_fingerprint(channel) ||
                (kind != ProbeOutcome::frame && kind != ProbeOutcome::missing))
                continue;
            catalog.SetPlayback(index,
                                kind == ProbeOutcome::frame ? iptv::PlaybackStatus::playable
                                                            : iptv::PlaybackStatus::failed,
                                -static_cast<int>(status), static_cast<std::uint64_t>(stamp));
        }
    }
    sqlite3_finalize(statement);
    sqlite3_close(db);
}
SourceCheck::~SourceCheck()
{
    stop();
}
bool SourceCheck::start(const PlayRequest &request)
{
    if (busy())
        return false;
    request_ = std::make_shared<PlayRequest>(request);
    cancel_.store(false);
    done_.store(false);
    frame_.store(false);
    result_ = {};
    thread_ = platform::thread_start(work, this, 2u * 1024u * 1024u, "ptv-check");
    return thread_ != nullptr;
}
void SourceCheck::stop()
{
    cancel_.store(true);
    if (thread_)
        platform::thread_join(thread_);
    thread_ = nullptr;
    request_.reset();
}
bool SourceCheck::take(ProbeResult &result)
{
    if (!thread_ || !done_.load())
        return false;
    platform::thread_join(thread_);
    thread_ = nullptr;
    request_.reset();
    result = result_;
    return true;
}
void *SourceCheck::work(void *self)
{
    auto &check = *static_cast<SourceCheck *>(self);
    struct Control
    {
        SourceCheck &check;
        std::chrono::steady_clock::time_point until;
    } state{check, std::chrono::steady_clock::now() + std::chrono::seconds(15)};
    const iptv::http::RequestControl control{[](void *p)
                                             {
                                                 auto &s = *static_cast<Control *>(p);
                                                 return s.check.cancel_.load() ||
                                                        s.check.frame_.load() ||
                                                        std::chrono::steady_clock::now() >= s.until;
                                             },
                                             &state};
    platform::preview(
        *check.request_, control,
        [](void *p, const iptv_native_picture_t *picture)
        {
            auto &check = *static_cast<SourceCheck *>(p);
            ImagePixels pixels;
            if (picture && preview_pixels(*picture, &pixels, 2))
                check.frame_.store(true);
        },
        &check, nullptr, &check.result_);
    if (check.cancel_.load())
        check.result_ = {ProbeOutcome::cancelled, 0};
    else if (check.frame_.load())
        check.result_ = {ProbeOutcome::frame, 0};
    else if (std::chrono::steady_clock::now() >= state.until)
        check.result_ = {ProbeOutcome::timeout, 0};
    check.done_.store(true);
    return nullptr;
}
} // namespace ptv
