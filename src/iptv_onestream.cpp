/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_onestream.h"

#include "iptv_json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace iptv
{
namespace
{

using json::EqualsCi;
using json::JsonReader;
using json::ParseRatingTenths;
using json::ParseUnsigned;
using json::ParseYear;
using json::PercentEncode;
using json::ReadArray;
using json::ReadArrayResponse;
using json::ReadObject;
using json::SafeCredential;
using json::ValidExtension;

constexpr char kCredentialsMagic[] = "PROSPEROTV-ONESTREAM-1";
constexpr char kApiPath[] = "play/b2c/v1/";
constexpr std::size_t kMaxCategories = 4096u;
constexpr std::size_t kMaxIdBytes = 64u;
// A "links" object normally has one URL per output format; more than this is not useful.
constexpr std::size_t kMaxLinks = 8u;

const char *ContentPath(OneStreamContent content)
{
    switch (content)
    {
    case OneStreamContent::live:
        return "live";
    case OneStreamContent::vod:
        return "vod";
    case OneStreamContent::series:
        return "series";
    }
    return "live";
}

bool ValidToken(std::string_view token)
{
    if (token.empty() || token.size() > kMaxOneStreamTokenBytes)
        return false;
    return std::all_of(token.begin(), token.end(),
                       [](unsigned char byte) { return byte >= 0x21u && byte <= 0x7eu; });
}

bool ValidId(std::string_view id)
{
    return !id.empty() && id.size() <= kMaxIdBytes;
}

// <server>play/b2c/v1/<path>[?token=<token>[&<extra>]]
bool BuildApiUrl(const OneStreamCredentials &credentials, const OneStreamSession *session,
                 std::string_view path, std::string_view extra_query, std::string *url)
{
    if (!url || !ValidateOneStreamCredentials(credentials))
        return false;
    *url = credentials.server_url + kApiPath + std::string(path);
    if (session)
    {
        if (!ValidToken(session->token))
            return false;
        std::string token;
        PercentEncode(session->token, &token);
        *url += "?token=" + token;
        if (!extra_query.empty())
            *url += "&" + std::string(extra_query);
    }
    return url->size() <= kDefaultMaxUrlBytes;
}

std::string StableId(std::uint64_t source_id, std::string_view kind, std::string_view id)
{
    char prefix[40]{};
    std::snprintf(prefix, sizeof(prefix),
                  "onestream:%016llx:", static_cast<unsigned long long>(source_id));
    std::string stable(prefix);
    if (!kind.empty())
        stable.append(kind).push_back(':');
    stable.append(id);
    return stable;
}

std::unordered_map<std::string, std::string>
CategoryNames(const std::vector<XtreamCategory> &categories)
{
    std::unordered_map<std::string, std::string> names;
    names.reserve(categories.size());
    for (const XtreamCategory &category : categories)
        if (!category.id.empty())
            names.emplace(category.id, category.name);
    return names;
}

std::string CategoryName(const std::unordered_map<std::string, std::string> &names,
                         const std::string &id, const char *fallback)
{
    const auto found = names.find(id);
    return found == names.end() || found->second.empty() ? fallback : found->second;
}

// Reads a string or scalar, treating JSON null as empty: panels send "expire_at": null.
bool ReadText(JsonReader *value, std::string *output, std::size_t maximum = kDefaultMaxFieldBytes)
{
    if (!value->StringOrScalar(output, maximum))
        return false;
    if (output && value && *output == "null")
        output->clear();
    return true;
}

// Reads category ids from "categories": ["12", 15] (objects in the list are skipped).
bool ReadCategoryIds(JsonReader *value, std::vector<std::string> *ids)
{
    if (value->Peek() != '[')
        return value->SkipValue();
    return ReadArray(value,
                     [&](JsonReader *entry)
                     {
                         const char next = entry->Peek();
                         if (next == '{' || next == '[')
                             return entry->SkipValue();
                         std::string id;
                         if (!ReadText(entry, &id, kMaxIdBytes))
                             return false;
                         if (!id.empty() && ids->size() < 1u + kDefaultMaxAlternateGroups)
                             ids->push_back(std::move(id));
                         return true;
                     });
}

// The lower-cased extension of a URL's last path segment: "m3u8" for ".../index.m3u8?x=1".
std::string UrlExtension(std::string_view url)
{
    const std::size_t end = url.find_first_of("?#");
    const std::string_view path = url.substr(0, end);
    const std::size_t slash = path.rfind('/');
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return {};
    const std::string_view extension = path.substr(dot + 1u);
    if (extension.empty() || !ValidExtension(extension))
        return {};
    std::string lowered(extension);
    for (char &character : lowered)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return lowered;
}

struct Links
{
    std::string primary;
    std::vector<std::string> alternates;
};

// Reads "links": {"m3u8": url, "ts": url, ...}. Some panels send that object as a JSON
// string. HLS is preferred, then MPEG-TS, then the other formats in the order sent.
bool ReadLinks(JsonReader *value, Links *links)
{
    std::vector<std::pair<std::string, std::string>> found;
    const auto read_object = [&](JsonReader *reader)
    {
        return ReadObject(reader,
                          [&](const std::string &key, JsonReader *entry)
                          {
                              if (entry->Peek() != '"')
                                  return entry->SkipValue();
                              std::string url;
                              if (!entry->String(&url, kDefaultMaxUrlBytes))
                                  return false;
                              if (found.size() < kMaxLinks)
                                  found.emplace_back(key, std::move(url));
                              return true;
                          });
    };
    const char next = value->Peek();
    if (next == '{')
    {
        if (!read_object(value))
            return false;
    }
    else if (next == '"')
    {
        std::string text;
        if (!value->String(&text, 4u * kDefaultMaxUrlBytes))
            return false;
        JsonReader inner(text);
        if (inner.Peek() != '{' || !read_object(&inner) || !inner.Finished())
            found.clear();
    }
    else if (!value->SkipValue())
    {
        return false;
    }

    std::vector<std::string> ordered;
    const auto take = [&](std::string_view format)
    {
        for (auto &[key, url] : found)
        {
            if (url.empty() || (!format.empty() && !EqualsCi(key, format)))
                continue;
            std::string canonical;
            if (CanonicalizeStreamUrl(url, &canonical) &&
                std::find(ordered.begin(), ordered.end(), canonical) == ordered.end())
                ordered.push_back(std::move(canonical));
            url.clear();
        }
    };
    take("m3u8");
    take("ts");
    take({});
    *links = {};
    if (ordered.empty())
        return true;
    links->primary = std::move(ordered.front());
    for (std::size_t index = 1;
         index < ordered.size() && links->alternates.size() < kDefaultMaxAlternateUrls; ++index)
        links->alternates.push_back(std::move(ordered[index]));
    return true;
}

// "01:30:00" or "5400" -> seconds.
std::uint32_t ParseDuration(std::string_view text)
{
    constexpr std::uint32_t kMaximum = 24u * 3600u;
    if (text.find(':') == std::string_view::npos)
        return ParseUnsigned(text, kMaximum);
    std::uint32_t total = 0;
    std::size_t start = 0;
    for (unsigned part = 0; part < 3u && start <= text.size(); ++part)
    {
        const std::size_t colon = text.find(':', start);
        total = total * 60u + ParseUnsigned(text.substr(start, colon - start), 60000u);
        if (colon == std::string_view::npos)
            break;
        start = colon + 1u;
    }
    return std::min(total, kMaximum);
}

OneStreamStatus CheckResponseSize(std::string_view json)
{
    if (json.empty())
        return XtreamStatus::malformed_json;
    return json.size() > kMaxOneStreamResponseBytes ? XtreamStatus::too_large : XtreamStatus::ok;
}

OneStreamStatus FetchFailure(XtreamFetchOutcome outcome)
{
    if (outcome == XtreamFetchOutcome::cancelled)
        return XtreamStatus::cancelled;
    return outcome == XtreamFetchOutcome::too_large ? XtreamStatus::too_large
                                                    : XtreamStatus::fetch_failed;
}

// Fields shared by live and movie entries in content/live and content/vod.
struct StreamEntry
{
    std::string id;
    std::string name;
    std::string logo;
    std::string epg_id;
    std::string rating;
    std::string year;
    std::vector<std::string> category_ids;
    Links links;
};

OneStreamStatus ParseStreams(std::string_view json, const std::vector<XtreamCategory> &categories,
                             std::uint64_t source_id, MediaKind kind, Catalog *catalog,
                             ParseReport *report)
{
    if (!catalog || source_id == 0)
        return XtreamStatus::invalid_argument;
    catalog->Clear();
    catalog->source_id = source_id;
    if (report)
        *report = {};
    if (const OneStreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;

    const bool live = kind == MediaKind::live;
    const std::size_t limit = live ? kDefaultMaxChannels : kMaxXtreamLibraryEntries;
    const char *fallback_group = live ? "Live TV" : "Movies";
    const auto names = CategoryNames(categories);
    std::unordered_set<std::string> ids;
    JsonReader reader(json);
    bool found = false;
    std::uint32_t source_line = 0;
    const bool valid = ReadArrayResponse(
        &reader,
        [&](JsonReader *item)
        {
            ++source_line;
            if (report)
                ++report->lines_seen;
            StreamEntry entry;
            std::string category_id;
            if (!ReadObject(item,
                            [&](const std::string &key, JsonReader *value)
                            {
                                if (key == "stream_id")
                                    return ReadText(value, &entry.id, kMaxIdBytes);
                                if (key == "name")
                                    return ReadText(value, &entry.name);
                                if (key == "stream_icon")
                                    return ReadText(value, &entry.logo, kDefaultMaxUrlBytes);
                                if (key == "epg_channel_id")
                                    return ReadText(value, &entry.epg_id);
                                if (key == "rating")
                                    return ReadText(value, &entry.rating, 16u);
                                if (key == "year" || key == "release_date")
                                    return ReadText(value, &entry.year, 32u);
                                if (key == "categories")
                                    return ReadCategoryIds(value, &entry.category_ids);
                                if (key == "category_id")
                                    return ReadText(value, &category_id, kMaxIdBytes);
                                if (key == "links")
                                    return ReadLinks(value, &entry.links);
                                return value->SkipValue();
                            }))
                return false;
            if (entry.category_ids.empty() && !category_id.empty())
                entry.category_ids.push_back(std::move(category_id));
            if (!ValidId(entry.id) || entry.links.primary.empty() || catalog->size() >= limit ||
                !ids.emplace(entry.id).second)
            {
                if (report)
                    ++report->skipped;
                return true;
            }
            Channel channel;
            channel.kind = kind;
            channel.id = StableId(source_id, live ? "" : "vod", entry.id);
            channel.source_id = source_id;
            const char *placeholder = live ? "Channel " : "Movie ";
            channel.name = entry.name.empty() ? placeholder + entry.id : std::move(entry.name);
            if (live)
            {
                channel.tvg_name = channel.name;
                channel.tvg_id = std::move(entry.epg_id);
            }
            else
            {
                channel.rating_tenths = ParseRatingTenths(entry.rating);
                channel.year = ParseYear(entry.year);
                channel.container_ext = UrlExtension(entry.links.primary);
            }
            channel.group_title =
                entry.category_ids.empty()
                    ? std::string(fallback_group)
                    : CategoryName(names, entry.category_ids.front(), fallback_group);
            for (std::size_t index = 1; index < entry.category_ids.size(); ++index)
            {
                const auto other = names.find(entry.category_ids[index]);
                if (other != names.end() && !other->second.empty() &&
                    other->second != channel.group_title &&
                    channel.alternate_group_titles.size() < kDefaultMaxAlternateGroups)
                    channel.alternate_group_titles.push_back(other->second);
            }
            channel.source_line = source_line;
            std::string canonical;
            if (CanonicalizeStreamUrl(entry.logo, &canonical))
                channel.tvg_logo = std::move(canonical);
            channel.url = std::move(entry.links.primary);
            channel.alternate_urls = std::move(entry.links.alternates);
            if (!catalog->Add(channel) && report)
                ++report->skipped;
            if (report)
                ++report->accepted;
            return true;
        },
        &found, "content");
    if (!valid || !found)
    {
        catalog->Clear();
        return XtreamStatus::malformed_json;
    }
    if (!catalog->empty())
        return XtreamStatus::ok;
    return live ? XtreamStatus::no_channels : XtreamStatus::no_movies;
}

OneStreamStatus FetchContent(const OneStreamCredentials &credentials,
                             const OneStreamSession &session, std::uint64_t source_id,
                             OneStreamContent content, const OneStreamTransport &transport,
                             Catalog *output, XtreamLibraryReport *report)
{
    if (!output || !transport.get || !ValidateOneStreamCredentials(credentials) ||
        !ValidToken(session.token) || source_id == 0)
        return XtreamStatus::invalid_argument;
    output->Clear();
    output->source_id = source_id;
    XtreamLibraryReport local;
    XtreamLibraryReport &stats = report ? *report : local;
    stats = {};

    const OneStreamStatus empty_status =
        content == OneStreamContent::live  ? XtreamStatus::no_channels
        : content == OneStreamContent::vod ? XtreamStatus::no_movies
                                           : XtreamStatus::no_series;
    const auto parse = [&](std::string_view body, const std::vector<XtreamCategory> &categories,
                           Catalog *catalog, ParseReport *parse_report)
    {
        switch (content)
        {
        case OneStreamContent::live:
            return ParseOneStreamLiveStreams(body, categories, source_id, catalog, parse_report);
        case OneStreamContent::vod:
            return ParseOneStreamVodStreams(body, categories, source_id, catalog, parse_report);
        case OneStreamContent::series:
            break;
        }
        return ParseOneStreamSeriesList(body, credentials, categories, source_id, catalog,
                                        parse_report);
    };

    std::string url;
    std::string_view body;
    // Categories give entries readable group names. A panel that cannot list them still
    // gets its entries imported under a generic group, so a bad list is not fatal.
    std::vector<XtreamCategory> categories;
    if (!BuildOneStreamCategoriesUrl(credentials, session, content, &url))
        return XtreamStatus::invalid_argument;
    ++stats.requests;
    const XtreamFetchOutcome category_outcome = transport.get(transport.context, url, &body);
    if (category_outcome == XtreamFetchOutcome::cancelled ||
        category_outcome == XtreamFetchOutcome::failed)
        return FetchFailure(category_outcome);
    if (category_outcome == XtreamFetchOutcome::ok &&
        ParseOneStreamCategories(body, &categories) != XtreamStatus::ok)
        categories.clear();
    stats.categories = static_cast<unsigned>(categories.size());

    if (!BuildOneStreamContentUrl(credentials, session, content, "all", &url))
        return XtreamStatus::invalid_argument;
    ++stats.requests;
    const XtreamFetchOutcome full_outcome = transport.get(transport.context, url, &body);
    if (full_outcome == XtreamFetchOutcome::ok)
    {
        ParseReport parse_report;
        const OneStreamStatus status = parse(body, categories, output, &parse_report);
        stats.entries_skipped = static_cast<unsigned>(parse_report.skipped);
        if (status != XtreamStatus::ok)
        {
            output->Clear();
            output->source_id = source_id;
        }
        return status;
    }
    if (full_outcome != XtreamFetchOutcome::too_large)
        return FetchFailure(full_outcome);

    // The whole list is bigger than one response may be; split it by category.
    stats.used_category_fallback = true;
    if (categories.empty())
        return XtreamStatus::too_large;
    std::unordered_set<std::string> seen;
    for (const XtreamCategory &category : categories)
    {
        if (!BuildOneStreamContentUrl(credentials, session, content, category.id, &url))
        {
            ++stats.categories_skipped;
            continue;
        }
        ++stats.requests;
        const XtreamFetchOutcome outcome = transport.get(transport.context, url, &body);
        if (outcome == XtreamFetchOutcome::cancelled || outcome == XtreamFetchOutcome::failed)
        {
            output->Clear();
            output->source_id = source_id;
            return FetchFailure(outcome);
        }
        if (outcome == XtreamFetchOutcome::too_large)
        {
            ++stats.categories_skipped;
            continue;
        }
        Catalog part;
        ParseReport parse_report;
        const OneStreamStatus status = parse(body, categories, &part, &parse_report);
        stats.entries_skipped += static_cast<unsigned>(parse_report.skipped);
        if (status == XtreamStatus::ok)
            MergeXtreamLibraryPart(output, std::move(part), &seen);
        else if (status != empty_status)
            ++stats.categories_skipped;
    }
    if (output->empty())
    {
        output->Clear();
        output->source_id = source_id;
        return empty_status;
    }
    return XtreamStatus::ok;
}

} // namespace

bool NormalizeOneStreamServerUrl(std::string_view input, std::string *normalized)
{
    if (!normalized || input.size() > kMaxOneStreamServerBytes)
        return false;
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.front())))
        input.remove_prefix(1);
    while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back())))
        input.remove_suffix(1);
    if (input.empty() || input.find('?') != std::string_view::npos ||
        input.find('#') != std::string_view::npos)
        return false;
    std::string candidate(input);
    if (candidate.find("://") == std::string::npos)
        candidate.insert(0, "http://");
    std::string canonical;
    if (!CanonicalizeStreamUrl(candidate, &canonical))
        return false;
    // Drop an endpoint pasted from a browser or another app.
    std::string lowered(canonical);
    for (char &character : lowered)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    if (const std::size_t api = lowered.find("/play/b2c/"); api != std::string::npos)
        canonical.resize(api);
    while (!canonical.empty() && canonical.back() == '/')
        canonical.pop_back();
    if (canonical.size() < 8u)
        return false;
    canonical.push_back('/');
    if (canonical.size() > kMaxOneStreamServerBytes)
        return false;
    *normalized = std::move(canonical);
    return true;
}

bool ValidateOneStreamCredentials(const OneStreamCredentials &credentials)
{
    std::string normalized;
    return NormalizeOneStreamServerUrl(credentials.server_url, &normalized) &&
           normalized == credentials.server_url &&
           SafeCredential(credentials.username, kMaxOneStreamCredentialBytes) &&
           SafeCredential(credentials.password, kMaxOneStreamCredentialBytes);
}

std::uint64_t OneStreamSourceId(const OneStreamCredentials &credentials)
{
    std::uint64_t hash = UINT64_C(1469598103934665603);
    const auto add = [&hash](std::string_view value)
    {
        for (const unsigned char byte : value)
        {
            hash ^= byte;
            hash *= UINT64_C(1099511628211);
        }
        hash ^= 0xffu;
        hash *= UINT64_C(1099511628211);
    };
    add(credentials.server_url);
    add(credentials.username);
    add(credentials.password);
    return UINT64_C(0x4f53000000000000) | (hash & UINT64_C(0x0000ffffffffffff));
}

bool BuildOneStreamAuthRequest(const OneStreamCredentials &credentials, std::string *url,
                               std::string *form)
{
    if (!form || !BuildApiUrl(credentials, nullptr, "auth", {}, url))
        return false;
    std::string username;
    std::string password;
    PercentEncode(credentials.username, &username);
    PercentEncode(credentials.password, &password);
    *form = "username=" + username + "&password=" + password;
    return true;
}

bool BuildOneStreamUserInfoUrl(const OneStreamCredentials &credentials,
                               const OneStreamSession &session, std::string *url)
{
    return BuildApiUrl(credentials, &session, "user-info", {}, url);
}

bool BuildOneStreamCategoriesUrl(const OneStreamCredentials &credentials,
                                 const OneStreamSession &session, OneStreamContent content,
                                 std::string *url)
{
    return BuildApiUrl(credentials, &session, std::string("categories/") + ContentPath(content), {},
                       url);
}

bool BuildOneStreamContentUrl(const OneStreamCredentials &credentials,
                              const OneStreamSession &session, OneStreamContent content,
                              std::string_view category_id, std::string *url)
{
    if (!ValidId(category_id))
        return false;
    std::string encoded;
    PercentEncode(category_id, &encoded);
    return BuildApiUrl(credentials, &session, std::string("content/") + ContentPath(content),
                       "category_id=" + encoded, url);
}

bool BuildOneStreamSeriesInfoUrl(const OneStreamCredentials &credentials,
                                 const OneStreamSession &session, std::string_view series_id,
                                 std::string *url)
{
    if (!ValidId(series_id))
        return false;
    std::string encoded;
    PercentEncode(series_id, &encoded);
    return BuildApiUrl(credentials, &session, "content/series/" + encoded, {}, url);
}

OneStreamStatus SaveOneStreamCredentials(const std::string &path,
                                         const OneStreamCredentials &credentials)
{
    if (path.empty() || !ValidateOneStreamCredentials(credentials))
        return XtreamStatus::invalid_argument;
    const std::string temporary = path + ".tmp";
    std::FILE *output = std::fopen(temporary.c_str(), "wb");
    if (!output)
        return XtreamStatus::io_error;
    const std::array<std::string_view, 4> lines = {kCredentialsMagic, credentials.server_url,
                                                   credentials.username, credentials.password};
    bool written = true;
    for (const std::string_view line : lines)
    {
        written = written && std::fwrite(line.data(), 1, line.size(), output) == line.size();
        written = written && std::fwrite("\n", 1, 1, output) == 1;
    }
    written = written && std::fflush(output) == 0;
    written = std::fclose(output) == 0 && written;
    if (!written || !json::ReplaceFile(temporary, path))
    {
        std::remove(temporary.c_str());
        return XtreamStatus::io_error;
    }
    return XtreamStatus::ok;
}

OneStreamStatus LoadOneStreamCredentials(const std::string &path, OneStreamCredentials *credentials)
{
    if (path.empty() || !credentials)
        return XtreamStatus::invalid_argument;
    std::FILE *input = std::fopen(path.c_str(), "rb");
    if (!input)
        return XtreamStatus::not_found;
    constexpr std::size_t capacity = sizeof(kCredentialsMagic) + kMaxOneStreamServerBytes +
                                     2u * kMaxOneStreamCredentialBytes + 8u;
    std::array<char, capacity> file{};
    const std::size_t bytes = std::fread(file.data(), 1, file.size(), input);
    const bool failed = std::ferror(input) != 0 || std::fclose(input) != 0;
    if (failed)
        return XtreamStatus::io_error;
    if (bytes == file.size())
        return XtreamStatus::too_large;
    std::array<std::string, 4> lines;
    std::size_t start = 0;
    for (std::size_t index = 0; index < lines.size(); ++index)
    {
        const std::size_t newline = std::string_view(file.data(), bytes).find('\n', start);
        if (newline == std::string_view::npos)
            return XtreamStatus::corrupt;
        std::size_t end = newline;
        if (end > start && file[end - 1u] == '\r')
            --end;
        lines[index].assign(file.data() + start, end - start);
        start = newline + 1u;
    }
    if (start != bytes || lines[0] != kCredentialsMagic)
        return XtreamStatus::corrupt;
    OneStreamCredentials loaded{lines[1], lines[2], lines[3]};
    if (!ValidateOneStreamCredentials(loaded))
        return XtreamStatus::corrupt;
    *credentials = std::move(loaded);
    return XtreamStatus::ok;
}

OneStreamStatus SaveOneStreamCredentials(const OneStreamCredentials &credentials)
{
    return SaveOneStreamCredentials(kDefaultOneStreamCredentialsPath, credentials);
}

OneStreamStatus LoadOneStreamCredentials(OneStreamCredentials *credentials)
{
    return LoadOneStreamCredentials(kDefaultOneStreamCredentialsPath, credentials);
}

OneStreamStatus ParseOneStreamAuth(std::string_view json, OneStreamSession *session,
                                   std::string *message)
{
    if (!session)
        return XtreamStatus::invalid_argument;
    *session = {};
    if (message)
        message->clear();
    if (const OneStreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;
    JsonReader reader(json);
    std::string token;
    std::string text;
    const bool valid = ReadObject(&reader,
                                  [&](const std::string &key, JsonReader *value)
                                  {
                                      if (key == "auth_token")
                                          return ReadText(value, &token, kMaxOneStreamTokenBytes);
                                      if (key == "message")
                                          return ReadText(value, &text, 256u);
                                      return value->SkipValue();
                                  });
    if (!valid || !reader.Finished())
        return XtreamStatus::malformed_json;
    if (message)
        *message = std::move(text);
    if (token.empty())
        return XtreamStatus::authentication_failed;
    if (!ValidToken(token))
        return XtreamStatus::malformed_json;
    session->token = std::move(token);
    return XtreamStatus::ok;
}

OneStreamStatus ParseOneStreamUserInfo(std::string_view json, OneStreamAccount *account)
{
    if (!account)
        return XtreamStatus::invalid_argument;
    *account = {};
    if (const OneStreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;
    JsonReader reader(json);
    bool found_user = false;
    std::string authenticated;
    std::string max_connections;
    std::string active_connections;
    const bool valid = ReadObject(
        &reader,
        [&](const std::string &key, JsonReader *value)
        {
            if (key == "user_info" && value->Peek() == '{')
            {
                found_user = true;
                return ReadObject(value,
                                  [&](const std::string &field, JsonReader *entry)
                                  {
                                      if (field == "auth")
                                          return ReadText(entry, &authenticated, 16u);
                                      if (field == "status")
                                          return ReadText(entry, &account->status, 64u);
                                      if (field == "message")
                                          return ReadText(entry, &account->message, 256u);
                                      if (field == "expire_at")
                                          return ReadText(entry, &account->expire_at, 64u);
                                      if (field == "created_at")
                                          return ReadText(entry, &account->created_at, 64u);
                                      if (field == "max_connections")
                                          return ReadText(entry, &max_connections, 16u);
                                      if (field == "active_connections")
                                          return ReadText(entry, &active_connections, 16u);
                                      return entry->SkipValue();
                                  });
            }
            if (key == "server_info" && value->Peek() == '{')
                return ReadObject(value,
                                  [&](const std::string &field, JsonReader *entry)
                                  {
                                      if (field == "time_now")
                                          return ReadText(entry, &account->server_time, 64u);
                                      if (field == "timezone")
                                          return ReadText(entry, &account->timezone, 64u);
                                      return entry->SkipValue();
                                  });
            return value->SkipValue();
        });
    if (!valid || !reader.Finished() || !found_user)
    {
        *account = {};
        return XtreamStatus::malformed_json;
    }
    account->max_connections = ParseUnsigned(max_connections, 9999u);
    account->active_connections = ParseUnsigned(active_connections, 9999u);
    if (authenticated == "0" || EqualsCi(authenticated, "false"))
        return XtreamStatus::authentication_failed;
    if (!account->status.empty() && !EqualsCi(account->status, "active"))
        return XtreamStatus::account_inactive;
    return XtreamStatus::ok;
}

OneStreamStatus ParseOneStreamCategories(std::string_view json,
                                         std::vector<XtreamCategory> *categories)
{
    if (!categories)
        return XtreamStatus::invalid_argument;
    categories->clear();
    if (const OneStreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;
    JsonReader reader(json);
    bool found = false;
    const bool valid = ReadArrayResponse(
        &reader,
        [&](JsonReader *entry)
        {
            XtreamCategory category;
            if (!ReadObject(entry,
                            [&](const std::string &key, JsonReader *value)
                            {
                                if (key == "category_id")
                                    return ReadText(value, &category.id, kMaxIdBytes);
                                if (key == "category_name")
                                    return ReadText(value, &category.name);
                                return value->SkipValue();
                            }))
                return false;
            if (!category.id.empty() && categories->size() < kMaxCategories)
                categories->push_back(std::move(category));
            return true;
        },
        &found, "content");
    if (!valid || !found)
    {
        categories->clear();
        return XtreamStatus::malformed_json;
    }
    return XtreamStatus::ok;
}

OneStreamStatus ParseOneStreamLiveStreams(std::string_view json,
                                          const std::vector<XtreamCategory> &categories,
                                          std::uint64_t source_id, Catalog *catalog,
                                          ParseReport *report)
{
    return ParseStreams(json, categories, source_id, MediaKind::live, catalog, report);
}

OneStreamStatus ParseOneStreamVodStreams(std::string_view json,
                                         const std::vector<XtreamCategory> &categories,
                                         std::uint64_t source_id, Catalog *catalog,
                                         ParseReport *report)
{
    return ParseStreams(json, categories, source_id, MediaKind::movie, catalog, report);
}

OneStreamStatus ParseOneStreamSeriesList(std::string_view json,
                                         const OneStreamCredentials &credentials,
                                         const std::vector<XtreamCategory> &categories,
                                         std::uint64_t source_id, Catalog *catalog,
                                         ParseReport *report)
{
    if (!catalog || !ValidateOneStreamCredentials(credentials) || source_id == 0)
        return XtreamStatus::invalid_argument;
    catalog->Clear();
    catalog->source_id = source_id;
    if (report)
        *report = {};
    if (const OneStreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;

    const auto names = CategoryNames(categories);
    std::unordered_set<std::string> ids;
    JsonReader reader(json);
    bool found = false;
    std::uint32_t source_line = 0;
    const bool valid = ReadArrayResponse(
        &reader,
        [&](JsonReader *item)
        {
            ++source_line;
            if (report)
                ++report->lines_seen;
            std::string series_id;
            std::string name;
            std::string cover;
            std::string rating;
            std::string release_date;
            std::string category_id;
            std::vector<std::string> category_ids;
            if (!ReadObject(item,
                            [&](const std::string &key, JsonReader *value)
                            {
                                if (key == "series_id")
                                    return ReadText(value, &series_id, kMaxIdBytes);
                                if (key == "name")
                                    return ReadText(value, &name);
                                if (key == "cover")
                                    return ReadText(value, &cover, kDefaultMaxUrlBytes);
                                if (key == "rating")
                                    return ReadText(value, &rating, 16u);
                                if (key == "release_date" || key == "releaseDate" || key == "year")
                                    return ReadText(value, &release_date, 32u);
                                if (key == "categories")
                                    return ReadCategoryIds(value, &category_ids);
                                if (key == "category_id")
                                    return ReadText(value, &category_id, kMaxIdBytes);
                                return value->SkipValue();
                            }))
                return false;
            if (category_ids.empty() && !category_id.empty())
                category_ids.push_back(std::move(category_id));
            std::string encoded;
            PercentEncode(series_id, &encoded);
            std::string info_url;
            if (!ValidId(series_id) || catalog->size() >= kMaxXtreamLibraryEntries ||
                !BuildApiUrl(credentials, nullptr, "content/series/" + encoded, {}, &info_url) ||
                !ids.emplace(series_id).second)
            {
                if (report)
                    ++report->skipped;
                return true;
            }
            Channel channel;
            channel.kind = MediaKind::series;
            channel.id = StableId(source_id, "series", series_id);
            channel.source_id = source_id;
            channel.name = name.empty() ? "Series " + series_id : std::move(name);
            channel.group_title = category_ids.empty()
                                      ? std::string("Series")
                                      : CategoryName(names, category_ids.front(), "Series");
            channel.source_line = source_line;
            channel.series_id = std::move(series_id);
            channel.rating_tenths = ParseRatingTenths(rating);
            channel.year = ParseYear(release_date);
            std::string canonical;
            if (CanonicalizeStreamUrl(cover, &canonical))
                channel.tvg_logo = std::move(canonical);
            channel.url = std::move(info_url);
            if (!catalog->Add(channel) && report)
                ++report->skipped;
            if (report)
                ++report->accepted;
            return true;
        },
        &found, "content");
    if (!valid || !found)
    {
        catalog->Clear();
        return XtreamStatus::malformed_json;
    }
    return catalog->empty() ? XtreamStatus::no_series : XtreamStatus::ok;
}

OneStreamStatus ParseOneStreamSeriesInfo(std::string_view json, std::string_view series_id,
                                         std::string_view series_name, std::uint64_t source_id,
                                         Catalog *catalog, ParseReport *report)
{
    if (!catalog || source_id == 0 || !ValidId(series_id))
        return XtreamStatus::invalid_argument;
    catalog->Clear();
    catalog->source_id = source_id;
    if (report)
        *report = {};
    if (const OneStreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;

    std::unordered_set<std::string> ids;
    std::uint32_t source_line = 0;
    const auto read_episode = [&](JsonReader *entry, std::uint32_t season_hint)
    {
        ++source_line;
        if (report)
            ++report->lines_seen;
        std::string episode_id;
        std::string title;
        std::string episode_number;
        std::string season_number;
        std::string duration;
        std::string image;
        Links links;
        if (!ReadObject(entry,
                        [&](const std::string &key, JsonReader *value)
                        {
                            if (key == "id" || (key == "stream_id" && episode_id.empty()))
                                return ReadText(value, &episode_id, kMaxIdBytes);
                            if (key == "title")
                                return ReadText(value, &title);
                            if (key == "episode_num")
                                return ReadText(value, &episode_number, 16u);
                            if (key == "season")
                                return ReadText(value, &season_number, 16u);
                            if (key == "links")
                                return ReadLinks(value, &links);
                            // Panels send "info": [] for episodes without metadata.
                            if (key == "info" && value->Peek() == '{')
                                return ReadObject(value,
                                                  [&](const std::string &field, JsonReader *info)
                                                  {
                                                      if (field == "duration_secs")
                                                          return ReadText(info, &duration, 16u);
                                                      if (field == "duration" && duration.empty())
                                                          return ReadText(info, &duration, 16u);
                                                      if (field == "movie_image")
                                                          return ReadText(info, &image,
                                                                          kDefaultMaxUrlBytes);
                                                      return info->SkipValue();
                                                  });
                            return value->SkipValue();
                        }))
            return false;
        if (!ValidId(episode_id) || links.primary.empty() ||
            catalog->size() >= kMaxXtreamEpisodes || !ids.emplace(episode_id).second)
        {
            if (report)
                ++report->skipped;
            return true;
        }
        const std::uint32_t season =
            season_number.empty() ? season_hint : ParseUnsigned(season_number, 9999u);
        const std::uint32_t number = ParseUnsigned(episode_number, 9999u);
        char label[32]{};
        std::snprintf(label, sizeof(label), "S%02u E%02u", static_cast<unsigned>(season),
                      static_cast<unsigned>(number));
        char group[32]{};
        std::snprintf(group, sizeof(group), "Season %u", static_cast<unsigned>(season));
        Channel channel;
        channel.kind = MediaKind::episode;
        channel.id = StableId(source_id, "ep", episode_id);
        channel.source_id = source_id;
        channel.name = title.empty() ? std::string(label) : std::string(label) + " " + title;
        channel.tvg_name = std::string(series_name);
        channel.group_title = group;
        channel.source_line = source_line;
        channel.series_id = std::string(series_id);
        channel.season = static_cast<std::uint16_t>(season);
        channel.episode = static_cast<std::uint16_t>(number);
        channel.duration_secs = ParseDuration(duration);
        channel.container_ext = UrlExtension(links.primary);
        std::string canonical;
        if (CanonicalizeStreamUrl(image, &canonical))
            channel.tvg_logo = std::move(canonical);
        channel.url = std::move(links.primary);
        channel.alternate_urls = std::move(links.alternates);
        if (!catalog->Add(channel) && report)
            ++report->skipped;
        if (report)
            ++report->accepted;
        return true;
    };
    const auto read_season = [&](JsonReader *value, std::uint32_t season_hint)
    {
        if (value->Peek() != '[')
            return value->SkipValue();
        return ReadArray(value,
                         [&](JsonReader *entry) { return read_episode(entry, season_hint); });
    };

    JsonReader reader(json);
    bool found = false;
    const bool valid =
        ReadObject(&reader,
                   [&](const std::string &key, JsonReader *value)
                   {
                       if (key != "episodes")
                           return value->SkipValue();
                       found = true;
                       // Episodes are keyed by season number, or sent as an array of seasons.
                       if (value->Peek() == '{')
                           return ReadObject(
                               value, [&](const std::string &season_key, JsonReader *season)
                               { return read_season(season, ParseUnsigned(season_key, 9999u)); });
                       if (value->Peek() == '[')
                       {
                           std::uint32_t index = 0;
                           return ReadArray(value, [&](JsonReader *season)
                                            { return read_season(season, ++index); });
                       }
                       return value->SkipValue();
                   });
    if (!valid || !reader.Finished() || !found)
    {
        catalog->Clear();
        return XtreamStatus::malformed_json;
    }
    return catalog->empty() ? XtreamStatus::no_episodes : XtreamStatus::ok;
}

OneStreamStatus OneStreamLogin(const OneStreamCredentials &credentials,
                               const OneStreamTransport &transport, OneStreamSession *session,
                               OneStreamAccount *account, std::string *message)
{
    if (message)
        message->clear();
    if (account)
        *account = {};
    if (!session || !transport.get || !transport.post_form ||
        !ValidateOneStreamCredentials(credentials))
        return XtreamStatus::invalid_argument;
    *session = {};

    std::string url;
    std::string form;
    if (!BuildOneStreamAuthRequest(credentials, &url, &form))
        return XtreamStatus::invalid_argument;
    std::string_view body;
    const XtreamFetchOutcome auth_outcome =
        transport.post_form(transport.context, url, form, &body);
    if (auth_outcome == XtreamFetchOutcome::failed && !body.empty())
    {
        // A panel that refuses the sign-in usually answers with an error status and a JSON
        // message; anything else is a network or server failure.
        OneStreamSession refused;
        std::string panel_message;
        if (ParseOneStreamAuth(body, &refused, &panel_message) != XtreamStatus::malformed_json)
        {
            if (message)
                *message = std::move(panel_message);
            return XtreamStatus::authentication_failed;
        }
    }
    if (auth_outcome != XtreamFetchOutcome::ok)
        return FetchFailure(auth_outcome);
    OneStreamSession signed_in;
    const OneStreamStatus auth_status = ParseOneStreamAuth(body, &signed_in, message);
    if (auth_status != XtreamStatus::ok)
        return auth_status;

    if (!BuildOneStreamUserInfoUrl(credentials, signed_in, &url))
        return XtreamStatus::invalid_argument;
    const XtreamFetchOutcome info_outcome = transport.get(transport.context, url, &body);
    if (info_outcome != XtreamFetchOutcome::ok)
        return FetchFailure(info_outcome);
    OneStreamAccount info;
    const OneStreamStatus info_status = ParseOneStreamUserInfo(body, &info);
    if (message && info_status != XtreamStatus::ok && !info.message.empty())
        *message = info.message;
    if (account)
        *account = std::move(info);
    if (info_status != XtreamStatus::ok)
        return info_status;
    *session = std::move(signed_in);
    return XtreamStatus::ok;
}

OneStreamStatus FetchOneStreamLive(const OneStreamCredentials &credentials,
                                   const OneStreamSession &session, std::uint64_t source_id,
                                   const OneStreamTransport &transport, Catalog *catalog,
                                   XtreamLibraryReport *report)
{
    return FetchContent(credentials, session, source_id, OneStreamContent::live, transport, catalog,
                        report);
}

OneStreamStatus FetchOneStreamLibrary(const OneStreamCredentials &credentials,
                                      const OneStreamSession &session, std::uint64_t source_id,
                                      XtreamLibraryKind kind, const OneStreamTransport &transport,
                                      Catalog *library, XtreamLibraryReport *report)
{
    return FetchContent(credentials, session, source_id,
                        kind == XtreamLibraryKind::movies ? OneStreamContent::vod
                                                          : OneStreamContent::series,
                        transport, library, report);
}

OneStreamStatus FetchOneStreamEpisodes(const OneStreamCredentials &credentials,
                                       std::string_view series_id, std::string_view series_name,
                                       std::uint64_t source_id, const OneStreamTransport &transport,
                                       Catalog *episodes)
{
    if (!episodes || source_id == 0 || !ValidId(series_id))
        return XtreamStatus::invalid_argument;
    episodes->Clear();
    OneStreamSession session;
    const OneStreamStatus login = OneStreamLogin(credentials, transport, &session, nullptr);
    if (login != XtreamStatus::ok)
        return login;
    std::string url;
    if (!BuildOneStreamSeriesInfoUrl(credentials, session, series_id, &url))
        return XtreamStatus::invalid_argument;
    std::string_view body;
    const XtreamFetchOutcome outcome = transport.get(transport.context, url, &body);
    if (outcome != XtreamFetchOutcome::ok)
        return FetchFailure(outcome);
    return ParseOneStreamSeriesInfo(body, series_id, series_name, source_id, episodes);
}

bool BuildOneStreamGuideUrl(const OneStreamCredentials &credentials,
                            const OneStreamSession &session, std::string *url)
{
    return BuildApiUrl(credentials, &session, "xml-epg", {}, url);
}

bool BuildOneStreamVodInfoUrl(const OneStreamCredentials &credentials,
                              const OneStreamSession &session, std::string_view stream_id,
                              std::string *url)
{
    if (!ValidId(stream_id))
        return false;
    std::string encoded;
    PercentEncode(stream_id, &encoded);
    return BuildApiUrl(credentials, &session, "content/vod/" + encoded, {}, url);
}

OneStreamStatus FetchOneStreamMediaInfo(const OneStreamCredentials &credentials,
                                        OneStreamSession *session, OneStreamContent content,
                                        std::string_view id, const OneStreamTransport &transport,
                                        MediaDetails *details)
{
    if (!session || !details || !ValidId(id) || content == OneStreamContent::live)
        return XtreamStatus::invalid_argument;
    *details = {};
    // A kept token may have expired; one fresh sign-in is tried before giving up.
    for (unsigned attempt = 0; attempt < 2u; ++attempt)
    {
        const bool fresh = session->token.empty();
        if (fresh)
        {
            const OneStreamStatus login = OneStreamLogin(credentials, transport, session, nullptr);
            if (login != XtreamStatus::ok)
                return login;
        }
        std::string url;
        const bool built = content == OneStreamContent::vod
                               ? BuildOneStreamVodInfoUrl(credentials, *session, id, &url)
                               : BuildOneStreamSeriesInfoUrl(credentials, *session, id, &url);
        if (!built)
            return XtreamStatus::invalid_argument;
        std::string_view body;
        const XtreamFetchOutcome outcome = transport.get(transport.context, url, &body);
        if (outcome == XtreamFetchOutcome::ok)
            return ParseMediaInfo(body, details);
        if (outcome != XtreamFetchOutcome::failed || fresh)
            return FetchFailure(outcome);
        session->token.clear();
    }
    return XtreamStatus::fetch_failed;
}

const char *OneStreamStatusDescription(OneStreamStatus status)
{
    switch (status)
    {
    case XtreamStatus::ok:
        return "ready";
    case XtreamStatus::invalid_argument:
        return "invalid OneStream panel address or credentials";
    case XtreamStatus::not_found:
        return "OneStream account is not configured";
    case XtreamStatus::too_large:
        return "OneStream response exceeds the supported size";
    case XtreamStatus::io_error:
        return "OneStream account could not be stored";
    case XtreamStatus::corrupt:
        return "saved OneStream account is invalid";
    case XtreamStatus::malformed_json:
        return "panel returned malformed OneStream data";
    case XtreamStatus::authentication_failed:
        return "OneStream username or password was rejected";
    case XtreamStatus::account_inactive:
        return "OneStream account is expired or inactive";
    case XtreamStatus::no_channels:
        return "OneStream panel returned no live channels";
    case XtreamStatus::no_movies:
        return "OneStream panel returned no movies";
    case XtreamStatus::no_series:
        return "OneStream panel returned no series";
    case XtreamStatus::no_episodes:
        return "OneStream panel returned no episodes";
    case XtreamStatus::fetch_failed:
        return "OneStream request failed";
    case XtreamStatus::cancelled:
        return "OneStream refresh was cancelled";
    }
    return "OneStream request failed";
}

} // namespace iptv
