/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_guide.h"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <utility>

namespace iptv
{
namespace
{

constexpr int kGuideSchemaVersion = 1;

bool Execute(sqlite3 *database, const char *sql)
{
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

bool BindText(sqlite3_stmt *statement, int index, const std::string &value)
{
    return sqlite3_bind_text(statement, index, value.data(), static_cast<int>(value.size()),
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string ColumnText(sqlite3_stmt *statement, int column)
{
    const unsigned char *text = sqlite3_column_text(statement, column);
    const int bytes = sqlite3_column_bytes(statement, column);
    return text && bytes > 0
               ? std::string(reinterpret_cast<const char *>(text), static_cast<std::size_t>(bytes))
               : std::string();
}

} // namespace

std::string GuideChannelKey(const std::string &id)
{
    std::string key = id;
    for (char &character : key)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return key;
}

namespace
{

struct Import
{
    gzip::ReadFunction read;
    void *context;
    GuideWriter *writer;
    std::uint64_t max_bytes;
    GuideImportReport *report;
    xmltv::Parser *parser;
    unsigned char first[4096];
    std::size_t first_size = 0;
    std::size_t first_position = 0;
    bool writer_full = false;

    // Hands out the bytes read to detect compression before reading on.
    static long Read(void *self, unsigned char *buffer, std::size_t capacity)
    {
        auto *import = static_cast<Import *>(self);
        if (import->first_position < import->first_size)
        {
            const std::size_t count =
                std::min(capacity, import->first_size - import->first_position);
            std::copy_n(import->first + import->first_position, count, buffer);
            import->first_position += count;
            return static_cast<long>(count);
        }
        const long read = import->read(import->context, buffer, capacity);
        if (read < 0)
            import->report->read_failed = true;
        return read;
    }

    static bool Programme(void *self, const xmltv::Programme &programme)
    {
        auto *import = static_cast<Import *>(self);
        if (import->writer->Add(programme))
            return true;
        import->writer_full = true;
        return false;
    }

    // Takes XML bytes, plain or decompressed.
    static bool Xml(void *self, const char *data, std::size_t size)
    {
        auto *import = static_cast<Import *>(self);
        if (import->report->bytes + size > import->max_bytes)
        {
            import->report->too_large = true;
            return false;
        }
        import->report->bytes += size;
        return import->parser->Feed(data, size) == xmltv::Status::ok;
    }
};

} // namespace

bool ImportGuide(gzip::ReadFunction read, void *context, GuideWriter *writer,
                 std::uint64_t max_bytes, GuideImportReport *report)
{
    GuideImportReport local;
    report = report ? report : &local;
    *report = {};
    if (!read || !writer)
        return false;
    auto import = std::make_unique<Import>();
    import->read = read;
    import->context = context;
    import->writer = writer;
    import->max_bytes = max_bytes;
    import->report = report;
    xmltv::Parser parser(&Import::Programme, import.get());
    import->parser = &parser;

    // Read enough to recognise the gzip header (the source may return fewer bytes).
    while (import->first_size < 2u)
    {
        const long count = read(context, import->first + import->first_size,
                                sizeof(import->first) - import->first_size);
        if (count < 0)
        {
            report->read_failed = true;
            return false;
        }
        if (count == 0)
            break;
        import->first_size += static_cast<std::size_t>(count);
    }
    report->compressed = gzip::LooksGzipped(import->first, import->first_size);
    bool complete = false;
    if (report->compressed)
    {
        report->gzip = gzip::Inflate(&Import::Read, &Import::Xml, import.get(), max_bytes + 1u);
        complete = report->gzip == gzip::Status::ok;
    }
    else
    {
        unsigned char buffer[16384];
        for (;;)
        {
            const long count = Import::Read(import.get(), buffer, sizeof(buffer));
            if (count < 0)
                break;
            if (count == 0)
            {
                complete = true;
                break;
            }
            if (!Import::Xml(import.get(), reinterpret_cast<const char *>(buffer),
                             static_cast<std::size_t>(count)))
                break;
        }
    }
    report->xml = complete ? parser.Finish() : parser.Feed(nullptr, 0);
    report->programmes = parser.report().programmes;
    report->skipped = parser.report().skipped;
    // A full store keeps what it has: the guide is usable, only shorter.
    if (import->writer_full && !report->read_failed)
        return true;
    return complete && report->xml == xmltv::Status::ok && !report->read_failed &&
           !report->too_large;
}

GuideWriter::~GuideWriter()
{
    Abort();
}

GuideStatus GuideWriter::Open(const std::string &path, std::uint64_t source_id,
                              std::unordered_set<std::string> channels, std::int64_t window_start,
                              std::int64_t window_end)
{
    Abort();
    if (path.empty() || source_id == 0 || window_end <= window_start)
        return GuideStatus::invalid_argument;
    path_ = path;
    staging_ = path + ".tmp";
    source_id_ = source_id;
    channels_ = std::move(channels);
    window_start_ = window_start;
    window_end_ = window_end;
    stored_ = filtered_ = 0;
    failed_ = false;
    std::remove(staging_.c_str());
    if (sqlite3_open_v2(staging_.c_str(), &database_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK)
    {
        Abort();
        return GuideStatus::io_error;
    }
    // A staging file is thrown away on failure, so durability is not needed while writing.
    const bool ready =
        Execute(database_, "PRAGMA journal_mode=OFF;"
                           "PRAGMA synchronous=OFF;"
                           "PRAGMA temp_store=MEMORY;"
                           "CREATE TABLE metadata(key TEXT PRIMARY KEY NOT NULL,"
                           "value INTEGER NOT NULL);"
                           "CREATE TABLE programmes(channel TEXT NOT NULL,start INTEGER NOT NULL,"
                           "stop INTEGER NOT NULL,title TEXT NOT NULL,"
                           "description TEXT NOT NULL);") &&
        Execute(database_, "BEGIN") &&
        sqlite3_prepare_v2(database_,
                           "INSERT INTO programmes(channel,start,stop,title,description) "
                           "VALUES(?,?,?,?,?)",
                           -1, &insert_, nullptr) == SQLITE_OK;
    if (!ready)
    {
        Abort();
        return GuideStatus::io_error;
    }
    return GuideStatus::ok;
}

bool GuideWriter::Add(const xmltv::Programme &programme)
{
    if (!insert_ || failed_)
        return false;
    if (programme.stop <= window_start_ || programme.start >= window_end_)
    {
        ++filtered_;
        return true;
    }
    const std::string key = GuideChannelKey(programme.channel);
    if (!channels_.count(key))
    {
        ++filtered_;
        return true;
    }
    if (stored_ >= kMaxGuideProgrammes)
        return false;
    const bool written =
        BindText(insert_, 1, key) && sqlite3_bind_int64(insert_, 2, programme.start) == SQLITE_OK &&
        sqlite3_bind_int64(insert_, 3, programme.stop) == SQLITE_OK &&
        BindText(insert_, 4, programme.title) && BindText(insert_, 5, programme.description) &&
        sqlite3_step(insert_) == SQLITE_DONE;
    sqlite3_reset(insert_);
    sqlite3_clear_bindings(insert_);
    if (!written)
    {
        failed_ = true;
        return false;
    }
    ++stored_;
    return true;
}

GuideStatus GuideWriter::Commit(std::int64_t saved_unix)
{
    if (!database_ || failed_)
    {
        Abort();
        return GuideStatus::io_error;
    }
    sqlite3_finalize(insert_);
    insert_ = nullptr;
    char metadata[160];
    std::snprintf(metadata, sizeof(metadata),
                  "INSERT INTO metadata(key,value) VALUES('source_id',%lld),('saved_unix',%lld);"
                  "PRAGMA user_version=%d;",
                  static_cast<long long>(source_id_), static_cast<long long>(saved_unix),
                  kGuideSchemaVersion);
    const bool ok = Execute(database_, metadata) &&
                    Execute(database_, "CREATE INDEX programmes_by_channel "
                                       "ON programmes(channel,stop);") &&
                    Execute(database_, "COMMIT");
    const bool closed = sqlite3_close(database_) == SQLITE_OK;
    database_ = nullptr;
    if (!ok || !closed)
    {
        Abort();
        return GuideStatus::io_error;
    }
    std::remove(path_.c_str());
    if (std::rename(staging_.c_str(), path_.c_str()) != 0)
    {
        Abort();
        return GuideStatus::io_error;
    }
    staging_.clear();
    return GuideStatus::ok;
}

void GuideWriter::Abort()
{
    if (insert_)
        sqlite3_finalize(insert_);
    insert_ = nullptr;
    if (database_)
        sqlite3_close(database_);
    database_ = nullptr;
    if (!staging_.empty())
        std::remove(staging_.c_str());
    staging_.clear();
    channels_.clear();
}

GuideReader::~GuideReader()
{
    Close();
}

GuideStatus GuideReader::Open(const std::string &path, std::uint64_t source_id)
{
    Close();
    if (path.empty() || source_id == 0)
        return GuideStatus::invalid_argument;
    std::FILE *probe = std::fopen(path.c_str(), "rb");
    if (!probe)
        return GuideStatus::not_found;
    std::fclose(probe);
    if (sqlite3_open_v2(path.c_str(), &database_, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK)
    {
        Close();
        return GuideStatus::io_error;
    }
    sqlite3_stmt *statement = nullptr;
    std::int64_t stored_source = 0;
    int version = 0;
    if (sqlite3_prepare_v2(database_, "PRAGMA user_version", -1, &statement, nullptr) ==
            SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW)
        version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    statement = nullptr;
    if (sqlite3_prepare_v2(database_, "SELECT key,value FROM metadata", -1, &statement, nullptr) ==
        SQLITE_OK)
        while (sqlite3_step(statement) == SQLITE_ROW)
        {
            const std::string key = ColumnText(statement, 0);
            if (key == "source_id")
                stored_source = sqlite3_column_int64(statement, 1);
            else if (key == "saved_unix")
                saved_unix_ = sqlite3_column_int64(statement, 1);
        }
    sqlite3_finalize(statement);
    if (version != kGuideSchemaVersion || static_cast<std::uint64_t>(stored_source) != source_id)
    {
        Close();
        return GuideStatus::corrupt;
    }
    if (sqlite3_prepare_v2(database_,
                           "SELECT start,stop,title,description FROM programmes "
                           "WHERE channel=? AND stop>? ORDER BY start LIMIT 2",
                           -1, &query_, nullptr) != SQLITE_OK)
    {
        Close();
        return GuideStatus::corrupt;
    }
    return GuideStatus::ok;
}

void GuideReader::Close()
{
    if (query_)
        sqlite3_finalize(query_);
    query_ = nullptr;
    if (database_)
        sqlite3_close(database_);
    database_ = nullptr;
    saved_unix_ = 0;
}

bool GuideReader::NowNext(const std::string &channel, std::int64_t now, GuideEntry *current,
                          GuideEntry *next)
{
    if (current)
        *current = {};
    if (next)
        *next = {};
    if (!query_ || channel.empty())
        return false;
    const std::string key = GuideChannelKey(channel);
    bool found = false;
    if (BindText(query_, 1, key) && sqlite3_bind_int64(query_, 2, now) == SQLITE_OK)
    {
        unsigned row = 0;
        while (sqlite3_step(query_) == SQLITE_ROW)
        {
            GuideEntry entry;
            entry.start = sqlite3_column_int64(query_, 0);
            entry.stop = sqlite3_column_int64(query_, 1);
            entry.title = ColumnText(query_, 2);
            entry.description = ColumnText(query_, 3);
            // The first row is on air only once it has started; otherwise it is next.
            if (row == 0 && entry.start <= now)
            {
                if (current)
                    *current = std::move(entry);
            }
            else if (next && next->title.empty())
            {
                *next = std::move(entry);
            }
            found = true;
            ++row;
        }
    }
    sqlite3_reset(query_);
    sqlite3_clear_bindings(query_);
    return found;
}

} // namespace iptv
