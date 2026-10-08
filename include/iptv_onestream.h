/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_ONESTREAM_H
#define IPTV_ONESTREAM_H

#include "iptv_catalog.h"
#include "iptv_xtream.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// OneStream panel accounts: the token-based "play/b2c/v1" API used by IPTV Smarters
// builds such as PSUV5. The user signs in with a username and password, the panel returns
// a short-lived token, and every later request carries it as ?token=. Stream URLs are not
// built here: the panel lists them per entry in a "links" object.
//
// The account sources share their status values, categories, library kinds and reports
// with Xtream, so the app can report either one the same way.
namespace iptv
{

inline constexpr char kDefaultOneStreamCredentialsPath[] =
    "/download0/prosperotv-onestream-v1.txt";
inline constexpr std::size_t kMaxOneStreamServerBytes = kMaxXtreamServerBytes;
inline constexpr std::size_t kMaxOneStreamCredentialBytes = kMaxXtreamCredentialBytes;
inline constexpr std::size_t kMaxOneStreamTokenBytes = 2048u;
inline constexpr std::size_t kMaxOneStreamResponseBytes = kMaxXtreamLibraryResponseBytes;

using OneStreamStatus = XtreamStatus;

struct OneStreamCredentials
{
    std::string server_url; // normalized: http(s)://host[:port][/path]/ (always ends in '/')
    std::string username;
    std::string password;
};

// Held in memory for one refresh or one series download; never written to disk.
struct OneStreamSession
{
    std::string token;
};

// Read from user-info. Only `status` decides whether the account may sign in; the rest is
// shown to the user.
struct OneStreamAccount
{
    std::string status;
    std::string message;
    std::string expire_at;
    std::string created_at;
    std::uint32_t max_connections = 0;
    std::uint32_t active_connections = 0;
    std::string server_time;
    std::string timezone;
};

enum class OneStreamContent : std::uint8_t
{
    live,
    vod,
    series,
};

// Supplied by the caller so the module does not depend on the network layer.
// get() downloads `url`; post_form() POSTs `form` as application/x-www-form-urlencoded.
// On ok, *body points at the response until the next call. When post_form() fails because
// the panel answered with an error status, *body may hold the (short) error response so a
// panel message can be shown; otherwise it is empty.
struct OneStreamTransport
{
    XtreamFetchOutcome (*get)(void *context, const std::string &url, std::string_view *body);
    XtreamFetchOutcome (*post_form)(void *context, const std::string &url, const std::string &form,
                                    std::string_view *body);
    void *context;
};

// Accepts "panel.example", "http://panel.example/", or an endpoint URL pasted from a browser
// ("https://panel.example/play/b2c/v1/auth"), and produces "http(s)://panel.example/".
bool NormalizeOneStreamServerUrl(std::string_view input, std::string *normalized);
bool ValidateOneStreamCredentials(const OneStreamCredentials &credentials);
// Stable across logins: the token is not part of it. Prefixed "OS" so it never collides
// with Xtream ("XT") or custom playlist ("CU") ids.
std::uint64_t OneStreamSourceId(const OneStreamCredentials &credentials);

bool BuildOneStreamAuthRequest(const OneStreamCredentials &credentials, std::string *url,
                               std::string *form);
bool BuildOneStreamUserInfoUrl(const OneStreamCredentials &credentials,
                               const OneStreamSession &session, std::string *url);
bool BuildOneStreamCategoriesUrl(const OneStreamCredentials &credentials,
                                 const OneStreamSession &session, OneStreamContent content,
                                 std::string *url);
// category_id "all" lists everything; a category id narrows the list.
bool BuildOneStreamContentUrl(const OneStreamCredentials &credentials,
                              const OneStreamSession &session, OneStreamContent content,
                              std::string_view category_id, std::string *url);
bool BuildOneStreamSeriesInfoUrl(const OneStreamCredentials &credentials,
                                 const OneStreamSession &session, std::string_view series_id,
                                 std::string *url);

OneStreamStatus SaveOneStreamCredentials(const std::string &path,
                                         const OneStreamCredentials &credentials);
OneStreamStatus LoadOneStreamCredentials(const std::string &path,
                                         OneStreamCredentials *credentials);
OneStreamStatus SaveOneStreamCredentials(const OneStreamCredentials &credentials);
OneStreamStatus LoadOneStreamCredentials(OneStreamCredentials *credentials);

// {"auth_token": "..."}. A panel's {"message": "..."} is copied to *message when present.
OneStreamStatus ParseOneStreamAuth(std::string_view json, OneStreamSession *session,
                                   std::string *message = nullptr);
OneStreamStatus ParseOneStreamUserInfo(std::string_view json, OneStreamAccount *account);
OneStreamStatus ParseOneStreamCategories(std::string_view json,
                                         std::vector<XtreamCategory> *categories);
OneStreamStatus ParseOneStreamLiveStreams(std::string_view json,
                                          const std::vector<XtreamCategory> &categories,
                                          std::uint64_t source_id, Catalog *catalog,
                                          ParseReport *report = nullptr);
OneStreamStatus ParseOneStreamVodStreams(std::string_view json,
                                         const std::vector<XtreamCategory> &categories,
                                         std::uint64_t source_id, Catalog *catalog,
                                         ParseReport *report = nullptr);
// A series entry's url is its tokenless content/series/<id> address. Episodes are fetched
// with a fresh token when the series is opened.
OneStreamStatus ParseOneStreamSeriesList(std::string_view json,
                                         const OneStreamCredentials &credentials,
                                         const std::vector<XtreamCategory> &categories,
                                         std::uint64_t source_id, Catalog *catalog,
                                         ParseReport *report = nullptr);
OneStreamStatus ParseOneStreamSeriesInfo(std::string_view json, std::string_view series_id,
                                         std::string_view series_name, std::uint64_t source_id,
                                         Catalog *catalog, ParseReport *report = nullptr);

// Signs in (auth, then user-info). On ok, *session holds the token.
OneStreamStatus OneStreamLogin(const OneStreamCredentials &credentials,
                               const OneStreamTransport &transport, OneStreamSession *session,
                               OneStreamAccount *account, std::string *message = nullptr);
// Downloads the live lineup, or the movie or series library. Each requests the whole list
// and, when it exceeds the size cap, falls back to one request per category.
OneStreamStatus FetchOneStreamLive(const OneStreamCredentials &credentials,
                                   const OneStreamSession &session, std::uint64_t source_id,
                                   const OneStreamTransport &transport, Catalog *catalog,
                                   XtreamLibraryReport *report = nullptr);
OneStreamStatus FetchOneStreamLibrary(const OneStreamCredentials &credentials,
                                      const OneStreamSession &session, std::uint64_t source_id,
                                      XtreamLibraryKind kind, const OneStreamTransport &transport,
                                      Catalog *library, XtreamLibraryReport *report = nullptr);
// Signs in and downloads one series' episodes.
OneStreamStatus FetchOneStreamEpisodes(const OneStreamCredentials &credentials,
                                       std::string_view series_id, std::string_view series_name,
                                       std::uint64_t source_id,
                                       const OneStreamTransport &transport, Catalog *episodes);

// The panel's XMLTV guide: play/b2c/v1/xml-epg?token=.
bool BuildOneStreamGuideUrl(const OneStreamCredentials &credentials,
                            const OneStreamSession &session, std::string *url);
bool BuildOneStreamVodInfoUrl(const OneStreamCredentials &credentials,
                              const OneStreamSession &session, std::string_view stream_id,
                              std::string *url);

// Downloads the details of one movie (content == vod) or series (content == series).
// *session is reused when it holds a token and refreshed by signing in when it does not, or
// when the panel refuses it; the caller may keep it in memory for later requests.
OneStreamStatus FetchOneStreamMediaInfo(const OneStreamCredentials &credentials,
                                        OneStreamSession *session, OneStreamContent content,
                                        std::string_view id, const OneStreamTransport &transport,
                                        MediaDetails *details);

const char *OneStreamStatusDescription(OneStreamStatus status);

} // namespace iptv

#endif
