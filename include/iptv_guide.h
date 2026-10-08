/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_GUIDE_H
#define IPTV_GUIDE_H

#include "iptv_inflate.h"
#include "iptv_xmltv.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>

struct sqlite3;
struct sqlite3_stmt;

// The TV guide database: programmes from an XMLTV guide, kept only for the source's own
// channels and for a window around the download time. Channel ids are matched without
// regard to case, as tvg-id / epg_channel_id against the guide's channel attribute.
namespace iptv
{

inline constexpr std::size_t kMaxGuideProgrammes = 400000u;

enum class GuideStatus : std::uint8_t
{
    ok,
    invalid_argument,
    not_found,
    io_error,
    corrupt,
};

struct GuideEntry
{
    std::int64_t start = 0;
    std::int64_t stop = 0;
    std::string title;
    std::string description;
};

// Builds a new guide in a staging file; Commit() replaces the previous guide.
class GuideWriter
{
  public:
    GuideWriter() = default;
    GuideWriter(const GuideWriter &) = delete;
    GuideWriter &operator=(const GuideWriter &) = delete;
    ~GuideWriter();

    // `channels` holds the lower-cased guide ids of the source's channels; programmes
    // outside [window_start, window_end) are dropped.
    GuideStatus Open(const std::string &path, std::uint64_t source_id,
                     std::unordered_set<std::string> channels, std::int64_t window_start,
                     std::int64_t window_end);
    // False once the programme cap is reached or a write failed.
    bool Add(const xmltv::Programme &programme);
    GuideStatus Commit(std::int64_t saved_unix);
    void Abort();

    std::size_t stored() const
    {
        return stored_;
    }
    std::size_t filtered() const
    {
        return filtered_;
    }

  private:
    sqlite3 *database_ = nullptr;
    sqlite3_stmt *insert_ = nullptr;
    std::string path_;
    std::string staging_;
    std::uint64_t source_id_ = 0;
    std::unordered_set<std::string> channels_;
    std::int64_t window_start_ = 0;
    std::int64_t window_end_ = 0;
    std::size_t stored_ = 0;
    std::size_t filtered_ = 0;
    bool failed_ = false;
};

class GuideReader
{
  public:
    GuideReader() = default;
    GuideReader(const GuideReader &) = delete;
    GuideReader &operator=(const GuideReader &) = delete;
    ~GuideReader();

    // Opens the guide if it belongs to `source_id`.
    GuideStatus Open(const std::string &path, std::uint64_t source_id);
    void Close();
    bool is_open() const
    {
        return database_ != nullptr;
    }
    std::int64_t saved_unix() const
    {
        return saved_unix_;
    }
    // The programme on air at `now` and the one after it. Returns false when the guide has
    // nothing for the channel from `now` on.
    bool NowNext(const std::string &channel, std::int64_t now, GuideEntry *current,
                 GuideEntry *next);

  private:
    sqlite3 *database_ = nullptr;
    sqlite3_stmt *query_ = nullptr;
    std::int64_t saved_unix_ = 0;
};

struct GuideImportReport
{
    bool compressed = false;
    std::uint64_t bytes = 0; // XML bytes parsed
    xmltv::Status xml = xmltv::Status::ok;
    gzip::Status gzip = gzip::Status::ok;
    bool read_failed = false;
    bool too_large = false;
    std::uint64_t programmes = 0;
    std::uint64_t skipped = 0;
};

// Streams a guide from `read` (plain or gzip-compressed XMLTV) into `writer`. Returns true
// when the whole guide was read; the caller then decides whether to commit it.
bool ImportGuide(gzip::ReadFunction read, void *context, GuideWriter *writer,
                 std::uint64_t max_bytes, GuideImportReport *report);

// Lower-cases ASCII letters, as guide channel ids are stored.
std::string GuideChannelKey(const std::string &id);

} // namespace iptv

#endif
