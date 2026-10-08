/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_XTREAM_H
#define IPTV_XTREAM_H

#include "iptv_catalog.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace iptv
{

inline constexpr char kDefaultXtreamCredentialsPath[] =
    "/download0/prosperotv-xtream-v1.txt";
inline constexpr std::size_t kMaxXtreamServerBytes = 1020u;
inline constexpr std::size_t kMaxXtreamCredentialBytes = 255u;
// The list of live streams: about 600 bytes a channel, read as it arrives.
inline constexpr std::size_t kMaxXtreamResponseBytes = 512u * 1024u * 1024u;
// The sign-in and the categories are small answers, read whole.
inline constexpr std::size_t kMaxXtreamReplyBytes = 4u * 1024u * 1024u;
// A movie or series list, or one category of it, read whole; a list larger than this is
// asked for one category at a time.
inline constexpr std::size_t kMaxXtreamLibraryResponseBytes = 16u * 1024u * 1024u;
// VOD and series libraries are much larger than live lineups.
inline constexpr std::size_t kMaxXtreamLibraryEntries = kDefaultMaxLibraryEntries;
inline constexpr std::size_t kMaxXtreamEpisodes = 4096u;

enum class XtreamStatus : std::uint8_t
{
    ok,
    invalid_argument,
    not_found,
    too_large,
    io_error,
    corrupt,
    malformed_json,
    authentication_failed,
    account_inactive,
    no_channels,
    no_movies,
    no_series,
    no_episodes,
    fetch_failed,
    cancelled,
};

struct XtreamCredentials
{
    std::string server_url;
    std::string username;
    std::string password;
};

struct XtreamAuth
{
    bool authenticated = false;
    std::string status;
    std::string message;
};

struct XtreamCategory
{
    std::string id;
    std::string name;
};

bool NormalizeXtreamServerUrl(std::string_view input, std::string *normalized);
bool ValidateXtreamCredentials(const XtreamCredentials &credentials);
std::uint64_t XtreamSourceId(const XtreamCredentials &credentials);

bool BuildXtreamApiUrl(const XtreamCredentials &credentials, std::string_view action,
                       std::string *url);
// Adds one extra query parameter (for example category_id or series_id) to an API URL.
bool BuildXtreamApiUrlWithParam(const XtreamCredentials &credentials, std::string_view action,
                                std::string_view key, std::string_view value, std::string *url);
// The provider's XMLTV guide: server/xmltv.php?username=&password=.
bool BuildXtreamGuideUrl(const XtreamCredentials &credentials, std::string *url);
bool BuildXtreamLiveUrl(const XtreamCredentials &credentials, std::string_view stream_id,
                        std::string_view extension, std::string *url);
// Movie and episode URLs default to the HLS container, which the player supports.
bool BuildXtreamVodUrl(const XtreamCredentials &credentials, std::string_view stream_id,
                       std::string_view extension, std::string *url);
bool BuildXtreamEpisodeUrl(const XtreamCredentials &credentials, std::string_view episode_id,
                           std::string_view extension, std::string *url);
// True for containers ProsperoTV can demux (MPEG-TS and HLS). MP4/MKV titles may still
// be offered as MPEG-TS or HLS by the provider, so callers should try before giving up.
bool XtreamContainerStreamable(std::string_view extension);

XtreamStatus SaveXtreamCredentials(const std::string &path,
                                    const XtreamCredentials &credentials);
XtreamStatus LoadXtreamCredentials(const std::string &path, XtreamCredentials *credentials);
XtreamStatus SaveXtreamCredentials(const XtreamCredentials &credentials);
XtreamStatus LoadXtreamCredentials(XtreamCredentials *credentials);

XtreamStatus ParseXtreamAuth(std::string_view json, XtreamAuth *auth);
XtreamStatus ParseXtreamCategories(std::string_view json,
                                   std::vector<XtreamCategory> *categories);
// Reads the answer to get_live_streams as it arrives: Feed it the bytes in
// order, in pieces of any size, then Finish. Each stream goes into `catalog`
// when its last byte is in, so the answer (tens of megabytes for a large
// provider) is never held whole. Past max_channels the streams are counted in
// the report and left out; full() says so, which is when a download can stop.
class XtreamStreamsParser
{
  public:
    XtreamStreamsParser(const XtreamCredentials &credentials,
                        const std::vector<XtreamCategory> &categories, std::uint64_t source_id,
                        Catalog *catalog, ParseReport *report = nullptr,
                        std::size_t max_channels = kDefaultMaxChannels);
    ~XtreamStreamsParser();
    XtreamStreamsParser(const XtreamStreamsParser &) = delete;
    XtreamStreamsParser &operator=(const XtreamStreamsParser &) = delete;

    // False once the answer cannot be a list of streams this app reads.
    bool Feed(std::string_view bytes);
    // What became of it. Anything but ok and the catalog is empty.
    XtreamStatus Finish();
    bool full() const;

  private:
    struct State;
    std::unique_ptr<State> state_;
};

// The same, for an answer that is already whole in memory.
XtreamStatus ParseXtreamLiveStreams(std::string_view json,
                                    const XtreamCredentials &credentials,
                                    const std::vector<XtreamCategory> &categories,
                                    std::uint64_t source_id, Catalog *catalog,
                                    ParseReport *report = nullptr,
                                    std::size_t max_channels = kDefaultMaxChannels);
XtreamStatus ParseXtreamVodStreams(std::string_view json, const XtreamCredentials &credentials,
                                   const std::vector<XtreamCategory> &categories,
                                   std::uint64_t source_id, Catalog *catalog,
                                   ParseReport *report = nullptr);
XtreamStatus ParseXtreamSeriesList(std::string_view json, const XtreamCredentials &credentials,
                                   const std::vector<XtreamCategory> &categories,
                                   std::uint64_t source_id, Catalog *catalog,
                                   ParseReport *report = nullptr);
// Parses get_series_info into episode entries named "S01E02 Title", grouped by season.
XtreamStatus ParseXtreamSeriesInfo(std::string_view json, const XtreamCredentials &credentials,
                                   std::string_view series_id, std::string_view series_name,
                                   std::uint64_t source_id, Catalog *catalog,
                                   ParseReport *report = nullptr);

enum class XtreamLibraryKind : std::uint8_t
{
    movies,
    series,
};

enum class XtreamFetchOutcome : std::uint8_t
{
    ok,
    too_large,
    failed,
    cancelled,
};

// Supplied by the caller so the library download logic does not depend on the network
// layer. fetch() downloads `url` and on ok points *body at the response, which stays valid
// until the next call.
struct XtreamFetcher
{
    XtreamFetchOutcome (*fetch)(void *context, const std::string &url, std::string_view *body);
    void *context;
};

struct XtreamLibraryReport
{
    unsigned requests = 0;
    unsigned categories = 0;
    unsigned categories_skipped = 0;
    unsigned entries_skipped = 0;
    bool used_category_fallback = false;
};

// Appends entries of `part` whose ids are not in `seen`, up to kMaxXtreamLibraryEntries in
// `library`. Returns how many entries were added.
std::size_t MergeXtreamLibraryPart(Catalog *library, Catalog &&part,
                                   std::unordered_set<std::string> *seen);

// Downloads a movie or series library. It requests the whole list first and, when the
// response exceeds the size cap, falls back to one request per category and merges them.
// A failed or cancelled request aborts the download so the caller keeps its previous
// cache; a category that is oversized or malformed is skipped.
XtreamStatus FetchXtreamLibrary(const XtreamCredentials &credentials, std::uint64_t source_id,
                                XtreamLibraryKind kind, const XtreamFetcher &fetcher,
                                Catalog *library, XtreamLibraryReport *report = nullptr);

// Reads the "info" object of an Xtream get_vod_info or get_series_info response. An "info": [] (no metadata)
// gives empty details.
XtreamStatus ParseMediaInfo(std::string_view json, MediaDetails *details);

const char *XtreamStatusDescription(XtreamStatus status);

} // namespace iptv

#endif
