/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_xtream.h"

#include "iptv_json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace iptv
{
namespace
{

constexpr char kCredentialsMagic[] = "PROSPEROTV-XTREAM-1";
constexpr std::size_t kMaxCategories = 4096u;

using json::EndsWithCi;
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

XtreamStatus ReplaceFile(const std::string &temporary, const std::string &path)
{
    return json::ReplaceFile(temporary, path) ? XtreamStatus::ok : XtreamStatus::io_error;
}

std::string_view CategoryName(const std::unordered_map<std::string, std::string> &categories,
                              const std::string &id, std::string_view fallback = "Live TV")
{
    const auto found = categories.find(id);
    return found == categories.end() || found->second.empty() ? fallback
                                                              : std::string_view(found->second);
}

bool BuildXtreamMediaUrl(const XtreamCredentials &credentials, std::string_view section,
                         std::string_view stream_id, std::string_view extension,
                         std::string_view default_extension, std::string *url)
{
    if (!url || !ValidateXtreamCredentials(credentials) || stream_id.empty() ||
        stream_id.size() > 64u || !ValidExtension(extension))
        return false;
    const std::string_view selected = extension.empty() ? default_extension : extension;
    std::string username;
    std::string password;
    std::string stream;
    PercentEncode(credentials.username, &username);
    PercentEncode(credentials.password, &password);
    PercentEncode(stream_id, &stream);
    *url = credentials.server_url + "/" + std::string(section) + "/" + username + "/" + password +
           "/" + stream + "." + std::string(selected);
    return url->size() <= kDefaultMaxUrlBytes;
}

std::string StableXtreamChannelId(std::uint64_t source_id, std::string_view stream_id)
{
    char prefix[32]{};
    std::snprintf(prefix, sizeof(prefix),
                  "xtream:%016llx:", static_cast<unsigned long long>(source_id));
    return std::string(prefix) + std::string(stream_id);
}

// "server/live/user/password/": what every live address of an account starts with.
std::string LiveUrlPrefix(const XtreamCredentials &credentials)
{
    std::string username;
    std::string password;
    PercentEncode(credentials.username, &username);
    PercentEncode(credentials.password, &password);
    return credentials.server_url + "/live/" + username + "/" + password + "/";
}

bool LiveUrlFromPrefix(const std::string &prefix, std::string_view stream_id,
                       std::string_view extension, std::string *url)
{
    if (stream_id.empty() || stream_id.size() > 64u || extension.size() > 12u)
        return false;
    const std::string_view selected_extension = extension.empty() ? "ts" : extension;
    if (!std::all_of(selected_extension.begin(), selected_extension.end(),
                     [](unsigned char value) { return std::isalnum(value) != 0; }))
        return false;
    std::string stream;
    PercentEncode(stream_id, &stream);
    url->assign(prefix);
    url->append(stream);
    url->push_back('.');
    url->append(selected_extension);
    return url->size() <= kDefaultMaxUrlBytes;
}

bool IsJsonSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

// Cuts an answer into the elements of its list as the bytes arrive: the list
// is the document itself, or the "data" member of the document. Only the
// element that is passing through is kept, and only when it straddles two
// pieces; everything around the list is checked for balance and let go.
class ListSplitter
{
  public:
    // False when the bytes cannot be such a document, or the handler refuses
    // an element. The handler is given each element as text.
    template <typename Handler> bool Feed(std::string_view bytes, Handler &&handler)
    {
        if (failed_)
            return false;
        std::size_t start = 0; // of the element passing through, in this piece
        for (std::size_t index = 0; index < bytes.size(); ++index)
        {
            const char value = bytes[index];
            switch (phase_)
            {
            case Phase::start:
                if (IsJsonSpace(value))
                    break;
                if (value == '[')
                {
                    found_ = true;
                    root_is_list_ = true;
                    phase_ = Phase::list;
                }
                else if (value == '{')
                {
                    depth_ = 1;
                    phase_ = Phase::object;
                }
                else
                {
                    return Fail();
                }
                break;
            case Phase::object:
                if (in_string_)
                {
                    if (escape_)
                        escape_ = false;
                    else if (value == '\\')
                        escape_ = true;
                    else if (value == '"')
                        in_string_ = false;
                    else if (depth_ == 1 && key_.size() < 8u)
                        key_.push_back(value);
                    break;
                }
                if (value == '"')
                {
                    in_string_ = true;
                    if (depth_ == 1)
                        key_.clear();
                }
                else if (value == '[' && depth_ == 1 && after_colon_ && !found_ && key_ == "data")
                {
                    found_ = true;
                    phase_ = Phase::list;
                }
                else if (value == '{' || value == '[')
                {
                    if (++depth_ > json::kMaxJsonDepth)
                        return Fail();
                }
                else if (value == '}' || value == ']')
                {
                    if (--depth_ == 0)
                        phase_ = Phase::done;
                }
                else if (depth_ == 1 && value == ':')
                {
                    after_colon_ = true;
                }
                else if (depth_ == 1 && value == ',')
                {
                    after_colon_ = false;
                }
                break;
            case Phase::list:
                if (IsJsonSpace(value) || value == ',')
                    break;
                if (value == ']')
                {
                    phase_ = root_is_list_ ? Phase::done : Phase::object;
                    after_colon_ = false;
                    break;
                }
                start = index;
                in_string_ = value == '"';
                escape_ = false;
                depth_element_ = value == '{' || value == '[' ? 1u : 0u;
                phase_ = Phase::element;
                break;
            case Phase::element:
            {
                bool ended = false;
                std::size_t end = index + 1u;
                if (in_string_)
                {
                    if (escape_)
                        escape_ = false;
                    else if (value == '\\')
                        escape_ = true;
                    else if (value == '"')
                    {
                        in_string_ = false;
                        ended = depth_element_ == 0;
                    }
                }
                else if (depth_element_ == 0)
                {
                    // A bare value ends where the list goes on.
                    if (value == ',' || value == ']' || IsJsonSpace(value))
                    {
                        ended = true;
                        end = index;
                        --index; // the list reads this byte itself
                    }
                }
                else if (value == '"')
                {
                    in_string_ = true;
                }
                else if (value == '{' || value == '[')
                {
                    ++depth_element_;
                }
                else if (value == '}' || value == ']')
                {
                    ended = --depth_element_ == 0;
                }
                if (!ended)
                    break;
                std::string_view element = bytes.substr(start, end - start);
                if (!element_.empty())
                {
                    if (element_.size() + element.size() > kMaxElementBytes)
                        return Fail();
                    element_.append(element);
                    element = element_;
                }
                if (!handler(element))
                    return Fail();
                element_.clear();
                phase_ = Phase::list;
                break;
            }
            case Phase::done:
                if (!IsJsonSpace(value))
                    return Fail();
                break;
            }
        }
        if (phase_ == Phase::element)
        {
            if (element_.size() + bytes.size() - start > kMaxElementBytes)
                return Fail();
            element_.append(bytes.substr(start));
        }
        return true;
    }

    // The document ended where a document ends.
    bool complete() const
    {
        return !failed_ && phase_ == Phase::done;
    }
    // It had a list.
    bool found() const
    {
        return found_;
    }

  private:
    enum class Phase : std::uint8_t
    {
        start,
        object,
        list,
        element,
        done,
    };
    static constexpr std::size_t kMaxElementBytes = 1024u * 1024u;

    bool Fail()
    {
        failed_ = true;
        return false;
    }

    Phase phase_ = Phase::start;
    bool failed_ = false;
    bool found_ = false;
    bool root_is_list_ = false;
    bool in_string_ = false;
    bool escape_ = false;
    bool after_colon_ = false;
    std::size_t depth_ = 0;
    std::size_t depth_element_ = 0;
    std::string key_;
    std::string element_;
};

} // namespace

bool NormalizeXtreamServerUrl(std::string_view input, std::string *normalized)
{
    if (!normalized || input.empty() || input.size() > kMaxXtreamServerBytes ||
        input.find('?') != std::string_view::npos || input.find('#') != std::string_view::npos)
        return false;
    std::string canonical;
    if (!CanonicalizeStreamUrl(input, &canonical))
        return false;
    while (!canonical.empty() && canonical.back() == '/')
        canonical.pop_back();
    constexpr std::string_view endpoint = "/player_api.php";
    if (EndsWithCi(canonical, endpoint))
        canonical.resize(canonical.size() - endpoint.size());
    while (!canonical.empty() && canonical.back() == '/')
        canonical.pop_back();
    if (canonical.size() < 8u)
        return false;
    *normalized = std::move(canonical);
    return true;
}

bool ValidateXtreamCredentials(const XtreamCredentials &credentials)
{
    std::string normalized;
    return NormalizeXtreamServerUrl(credentials.server_url, &normalized) &&
           normalized == credentials.server_url &&
           SafeCredential(credentials.username, kMaxXtreamCredentialBytes) &&
           SafeCredential(credentials.password, kMaxXtreamCredentialBytes);
}

std::uint64_t XtreamSourceId(const XtreamCredentials &credentials)
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
    return UINT64_C(0x5854000000000000) | (hash & UINT64_C(0x0000ffffffffffff));
}

bool BuildXtreamApiUrl(const XtreamCredentials &credentials, std::string_view action,
                       std::string *url)
{
    if (!url || !ValidateXtreamCredentials(credentials))
        return false;
    std::string username;
    std::string password;
    std::string encoded_action;
    PercentEncode(credentials.username, &username);
    PercentEncode(credentials.password, &password);
    PercentEncode(action, &encoded_action);
    *url =
        credentials.server_url + "/player_api.php?username=" + username + "&password=" + password;
    if (!action.empty())
        *url += "&action=" + encoded_action;
    return url->size() <= kDefaultMaxUrlBytes;
}

bool BuildXtreamApiUrlWithParam(const XtreamCredentials &credentials, std::string_view action,
                                std::string_view key, std::string_view value, std::string *url)
{
    if (key.empty() || key.size() > 32u || value.empty() || value.size() > 64u ||
        !std::all_of(key.begin(), key.end(),
                     [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; }))
        return false;
    std::string base;
    std::string encoded_value;
    if (!BuildXtreamApiUrl(credentials, action, &base))
        return false;
    PercentEncode(value, &encoded_value);
    if (!url)
        return false;
    *url = base + "&" + std::string(key) + "=" + encoded_value;
    return url->size() <= kDefaultMaxUrlBytes;
}

bool BuildXtreamGuideUrl(const XtreamCredentials &credentials, std::string *url)
{
    if (!url || !ValidateXtreamCredentials(credentials))
        return false;
    std::string username;
    std::string password;
    PercentEncode(credentials.username, &username);
    PercentEncode(credentials.password, &password);
    *url = credentials.server_url + "/xmltv.php?username=" + username + "&password=" + password;
    return url->size() <= kDefaultMaxUrlBytes;
}

bool BuildXtreamLiveUrl(const XtreamCredentials &credentials, std::string_view stream_id,
                        std::string_view extension, std::string *url)
{
    return url && ValidateXtreamCredentials(credentials) &&
           LiveUrlFromPrefix(LiveUrlPrefix(credentials), stream_id, extension, url);
}

bool BuildXtreamVodUrl(const XtreamCredentials &credentials, std::string_view stream_id,
                       std::string_view extension, std::string *url)
{
    return BuildXtreamMediaUrl(credentials, "movie", stream_id, extension, "m3u8", url);
}

bool BuildXtreamEpisodeUrl(const XtreamCredentials &credentials, std::string_view episode_id,
                           std::string_view extension, std::string *url)
{
    return BuildXtreamMediaUrl(credentials, "series", episode_id, extension, "m3u8", url);
}

bool XtreamContainerStreamable(std::string_view extension)
{
    return extension.empty() || EqualsCi(extension, "ts") || EqualsCi(extension, "m3u8") ||
           EqualsCi(extension, "m3u");
}

XtreamStatus SaveXtreamCredentials(const std::string &path, const XtreamCredentials &credentials)
{
    if (path.empty() || !ValidateXtreamCredentials(credentials))
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
    if (!written)
    {
        std::remove(temporary.c_str());
        return XtreamStatus::io_error;
    }
    const XtreamStatus replaced = ReplaceFile(temporary, path);
    if (replaced != XtreamStatus::ok)
        std::remove(temporary.c_str());
    return replaced;
}

XtreamStatus LoadXtreamCredentials(const std::string &path, XtreamCredentials *credentials)
{
    if (path.empty() || !credentials)
        return XtreamStatus::invalid_argument;
    std::FILE *input = std::fopen(path.c_str(), "rb");
    if (!input)
        return XtreamStatus::not_found;
    constexpr std::size_t capacity =
        sizeof(kCredentialsMagic) + kMaxXtreamServerBytes + 2u * kMaxXtreamCredentialBytes + 8u;
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
    XtreamCredentials loaded{lines[1], lines[2], lines[3]};
    if (!ValidateXtreamCredentials(loaded))
        return XtreamStatus::corrupt;
    *credentials = std::move(loaded);
    return XtreamStatus::ok;
}

XtreamStatus SaveXtreamCredentials(const XtreamCredentials &credentials)
{
    return SaveXtreamCredentials(kDefaultXtreamCredentialsPath, credentials);
}

XtreamStatus LoadXtreamCredentials(XtreamCredentials *credentials)
{
    return LoadXtreamCredentials(kDefaultXtreamCredentialsPath, credentials);
}

XtreamStatus ParseXtreamAuth(std::string_view json, XtreamAuth *auth)
{
    if (!auth)
        return XtreamStatus::invalid_argument;
    *auth = {};
    if (json.empty() || json.size() > kMaxXtreamResponseBytes)
        return json.size() > kMaxXtreamResponseBytes ? XtreamStatus::too_large
                                                     : XtreamStatus::malformed_json;
    JsonReader reader(json);
    bool found_user = false;
    std::string authenticated;
    const bool valid =
        ReadObject(&reader,
                   [&](const std::string &key, JsonReader *value)
                   {
                       if (key != "user_info")
                           return value->SkipValue();
                       found_user = true;
                       return ReadObject(value,
                                         [&](const std::string &field, JsonReader *entry)
                                         {
                                             if (field == "auth")
                                                 return entry->StringOrScalar(&authenticated, 16u);
                                             if (field == "status")
                                                 return entry->StringOrScalar(&auth->status, 64u);
                                             if (field == "message")
                                                 return entry->StringOrScalar(&auth->message, 256u);
                                             return entry->SkipValue();
                                         });
                   });
    if (!valid || !reader.Finished() || !found_user)
        return XtreamStatus::malformed_json;
    auth->authenticated = authenticated == "1" || EqualsCi(authenticated, "true");
    if (!auth->authenticated)
        return XtreamStatus::authentication_failed;
    if (!auth->status.empty() && !EqualsCi(auth->status, "active"))
        return XtreamStatus::account_inactive;
    return XtreamStatus::ok;
}

XtreamStatus ParseXtreamCategories(std::string_view json, std::vector<XtreamCategory> *categories)
{
    if (!categories)
        return XtreamStatus::invalid_argument;
    categories->clear();
    if (json.empty() || json.size() > kMaxXtreamResponseBytes)
        return json.size() > kMaxXtreamResponseBytes ? XtreamStatus::too_large
                                                     : XtreamStatus::malformed_json;
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
                                    return value->StringOrScalar(&category.id, 64u);
                                if (key == "category_name")
                                    return value->StringOrScalar(&category.name);
                                return value->SkipValue();
                            }))
                return false;
            if (!category.id.empty() && categories->size() < kMaxCategories)
                categories->push_back(std::move(category));
            return true;
        },
        &found);
    return valid && found ? XtreamStatus::ok : XtreamStatus::malformed_json;
}

struct XtreamStreamsParser::State
{
    std::string live_prefix;
    std::unordered_map<std::string, std::string> category_names;
    std::uint64_t source_id = 0;
    Catalog *catalog = nullptr;
    ParseReport local_report;
    ParseReport *report = nullptr;
    std::size_t max_channels = kDefaultMaxChannels;
    ListSplitter splitter;
    std::uint32_t source_line = 0;
    std::size_t bytes_seen = 0;
    bool usable = false;
    bool too_large = false;
    bool full = false;

    void LeftOut()
    {
        full = true;
        report->catalog_full = true;
        ++report->skipped;
    }

    // One stream of the list. False: it is not what a provider sends.
    bool Stream(std::string_view element)
    {
        ++source_line;
        ++report->lines_seen;
        std::string stream_id;
        std::string name;
        std::string logo;
        std::string epg_id;
        std::string category_id;
        std::string extension;
        std::string direct_source;
        std::string stream_url;
        JsonReader entry(element);
        if (!ReadObject(&entry,
                        [&](const std::string &key, JsonReader *value)
                        {
                            if (key == "stream_id")
                                return value->StringOrScalar(&stream_id, 64u);
                            if (key == "name")
                                return value->StringOrScalar(&name);
                            if (key == "stream_icon")
                                return value->StringOrScalar(&logo, kDefaultMaxUrlBytes);
                            if (key == "epg_channel_id")
                                return value->StringOrScalar(&epg_id);
                            if (key == "category_id")
                                return value->StringOrScalar(&category_id, 64u);
                            if (key == "container_extension")
                                return value->StringOrScalar(&extension, 12u);
                            if (key == "direct_source")
                                return value->StringOrScalar(&direct_source, kDefaultMaxUrlBytes);
                            if (key == "stream_url")
                                return value->StringOrScalar(&stream_url, kDefaultMaxUrlBytes);
                            return value->SkipValue();
                        }) ||
            !entry.Finished())
            return false;

        std::string generated_url;
        const std::string id = StableXtreamChannelId(source_id, stream_id);
        if (stream_id.empty() || catalog->Find(id) != Catalog::npos ||
            !LiveUrlFromPrefix(live_prefix, stream_id, extension, &generated_url))
        {
            ++report->skipped;
            return true;
        }
        if (catalog->size() >= max_channels)
        {
            LeftOut();
            return true;
        }
        if (name.empty())
            name = "Channel " + stream_id;
        std::string logo_url;
        std::string direct_url;
        ChannelView channel;
        channel.id = id;
        channel.source_id = source_id;
        channel.name = name;
        channel.tvg_name = name;
        channel.tvg_id = epg_id;
        channel.group_title = CategoryName(category_names, category_id);
        channel.source_line = source_line;
        if (CanonicalizeStreamUrl(logo, &logo_url))
            channel.tvg_logo = logo_url;
        if (direct_source.empty())
            direct_source = std::move(stream_url);
        const bool direct =
            CanonicalizeStreamUrl(direct_source, &direct_url) && direct_url != generated_url;
        channel.url = direct ? direct_url : generated_url;
        const std::size_t index = catalog->size();
        if (!catalog->Add(channel) || (direct && !catalog->AddAlternateUrl(index, generated_url)))
        {
            LeftOut();
            return true;
        }
        ++report->accepted;
        return true;
    }
};

XtreamStreamsParser::XtreamStreamsParser(const XtreamCredentials &credentials,
                                         const std::vector<XtreamCategory> &categories,
                                         std::uint64_t source_id, Catalog *catalog,
                                         ParseReport *report, std::size_t max_channels)
    : state_(new State)
{
    State &state = *state_;
    state.usable = catalog != nullptr && source_id != 0 && ValidateXtreamCredentials(credentials);
    state.source_id = source_id;
    state.catalog = catalog;
    state.report = report == nullptr ? &state.local_report : report;
    *state.report = {};
    state.max_channels = max_channels;
    if (!state.usable)
        return;
    state.live_prefix = LiveUrlPrefix(credentials);
    state.category_names.reserve(categories.size());
    for (const XtreamCategory &category : categories)
        if (!category.id.empty())
            state.category_names.emplace(category.id, category.name);
    catalog->Clear();
    catalog->source_id = source_id;
}

XtreamStreamsParser::~XtreamStreamsParser() = default;

bool XtreamStreamsParser::Feed(std::string_view bytes)
{
    State &state = *state_;
    if (!state.usable || state.too_large)
        return false;
    state.bytes_seen += bytes.size();
    if (state.bytes_seen > kMaxXtreamResponseBytes)
    {
        state.too_large = true;
        return false;
    }
    return state.splitter.Feed(bytes, [&state](std::string_view element)
                               { return state.Stream(element); });
}

XtreamStatus XtreamStreamsParser::Finish()
{
    State &state = *state_;
    if (!state.usable)
        return XtreamStatus::invalid_argument;
    // A download that stopped because the catalog was full ends in the middle
    // of the list: what it has is the first max_channels streams.
    const bool whole = state.splitter.complete() && state.splitter.found();
    if (state.too_large || (!whole && !state.full))
    {
        state.catalog->Clear();
        return state.too_large ? XtreamStatus::too_large : XtreamStatus::malformed_json;
    }
    return state.catalog->empty() ? XtreamStatus::no_channels : XtreamStatus::ok;
}

bool XtreamStreamsParser::full() const
{
    return state_->full;
}

XtreamStatus ParseXtreamLiveStreams(std::string_view json, const XtreamCredentials &credentials,
                                    const std::vector<XtreamCategory> &categories,
                                    std::uint64_t source_id, Catalog *catalog, ParseReport *report,
                                    std::size_t max_channels)
{
    if (!catalog || !ValidateXtreamCredentials(credentials) || source_id == 0)
        return XtreamStatus::invalid_argument;
    XtreamStreamsParser parser(credentials, categories, source_id, catalog, report, max_channels);
    if (json.empty())
        return XtreamStatus::malformed_json;
    // A list that is whole in memory is read to its end, so that the report
    // counts every stream it left out.
    (void)parser.Feed(json);
    return parser.Finish();
}

namespace
{

std::string LibraryId(std::uint64_t source_id, std::string_view kind, std::string_view id)
{
    return StableXtreamChannelId(source_id, std::string(kind) + ":" + std::string(id));
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

bool LibraryArgumentsValid(const Catalog *catalog, const XtreamCredentials &credentials,
                           std::uint64_t source_id)
{
    return catalog && ValidateXtreamCredentials(credentials) && source_id != 0;
}

XtreamStatus CheckResponseSize(std::string_view json)
{
    if (json.empty())
        return XtreamStatus::malformed_json;
    return json.size() > kMaxXtreamLibraryResponseBytes ? XtreamStatus::too_large
                                                        : XtreamStatus::ok;
}

} // namespace

XtreamStatus ParseXtreamVodStreams(std::string_view json, const XtreamCredentials &credentials,
                                   const std::vector<XtreamCategory> &categories,
                                   std::uint64_t source_id, Catalog *catalog, ParseReport *report)
{
    if (!LibraryArgumentsValid(catalog, credentials, source_id))
        return XtreamStatus::invalid_argument;
    catalog->Clear();
    catalog->source_id = source_id;
    if (report)
        *report = {};
    if (const XtreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;

    const auto category_names = CategoryNames(categories);
    std::unordered_set<std::string> stream_ids;
    JsonReader reader(json);
    bool found = false;
    std::uint32_t source_line = 0;
    const bool valid = ReadArrayResponse(
        &reader,
        [&](JsonReader *entry)
        {
            ++source_line;
            if (report)
                ++report->lines_seen;
            std::string stream_id;
            std::string name;
            std::string logo;
            std::string category_id;
            std::string extension;
            std::string rating;
            std::string year;
            if (!ReadObject(entry,
                            [&](const std::string &key, JsonReader *value)
                            {
                                if (key == "stream_id")
                                    return value->StringOrScalar(&stream_id, 64u);
                                if (key == "name")
                                    return value->StringOrScalar(&name);
                                if (key == "stream_icon")
                                    return value->StringOrScalar(&logo, kDefaultMaxUrlBytes);
                                if (key == "category_id")
                                    return value->StringOrScalar(&category_id, 64u);
                                if (key == "container_extension")
                                    return value->StringOrScalar(&extension, 12u);
                                if (key == "rating")
                                    return value->StringOrScalar(&rating, 16u);
                                if (key == "year")
                                    return value->StringOrScalar(&year, 32u);
                                return value->SkipValue();
                            }))
                return false;
            std::string primary_url;
            std::string transport_stream_url;
            if (stream_id.empty() || !stream_ids.emplace(stream_id).second ||
                !BuildXtreamVodUrl(credentials, stream_id, "m3u8", &primary_url) ||
                !BuildXtreamVodUrl(credentials, stream_id, "ts", &transport_stream_url))
            {
                if (report)
                    ++report->skipped;
                return true;
            }
            if (catalog->size() >= kMaxXtreamLibraryEntries)
            {
                if (report)
                    ++report->skipped;
                return true;
            }
            Channel channel;
            channel.kind = MediaKind::movie;
            channel.id = LibraryId(source_id, "vod", stream_id);
            channel.source_id = source_id;
            // tvg_name is left empty: cards and search already fall back to the name.
            channel.name = name.empty() ? "Movie " + stream_id : std::move(name);
            channel.group_title = CategoryName(category_names, category_id, "Movies");
            channel.source_line = source_line;
            channel.container_ext = std::move(extension);
            channel.rating_tenths = ParseRatingTenths(rating);
            channel.year = ParseYear(year);
            std::string canonical;
            if (CanonicalizeStreamUrl(logo, &canonical))
                channel.tvg_logo = std::move(canonical);
            channel.url = std::move(primary_url);
            channel.alternate_urls.push_back(std::move(transport_stream_url));
            if (!catalog->Add(channel) && report)
                ++report->skipped;
            if (report)
                ++report->accepted;
            return true;
        },
        &found);
    if (!valid || !found)
    {
        catalog->Clear();
        return XtreamStatus::malformed_json;
    }
    return catalog->empty() ? XtreamStatus::no_movies : XtreamStatus::ok;
}

XtreamStatus ParseXtreamSeriesList(std::string_view json, const XtreamCredentials &credentials,
                                   const std::vector<XtreamCategory> &categories,
                                   std::uint64_t source_id, Catalog *catalog, ParseReport *report)
{
    if (!LibraryArgumentsValid(catalog, credentials, source_id))
        return XtreamStatus::invalid_argument;
    catalog->Clear();
    catalog->source_id = source_id;
    if (report)
        *report = {};
    if (const XtreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;

    const auto category_names = CategoryNames(categories);
    std::unordered_set<std::string> series_ids;
    JsonReader reader(json);
    bool found = false;
    std::uint32_t source_line = 0;
    const bool valid = ReadArrayResponse(
        &reader,
        [&](JsonReader *entry)
        {
            ++source_line;
            if (report)
                ++report->lines_seen;
            std::string series_id;
            std::string name;
            std::string cover;
            std::string category_id;
            std::string rating;
            std::string year;
            std::string release_date;
            if (!ReadObject(entry,
                            [&](const std::string &key, JsonReader *value)
                            {
                                if (key == "series_id")
                                    return value->StringOrScalar(&series_id, 64u);
                                if (key == "name")
                                    return value->StringOrScalar(&name);
                                if (key == "cover")
                                    return value->StringOrScalar(&cover, kDefaultMaxUrlBytes);
                                if (key == "category_id")
                                    return value->StringOrScalar(&category_id, 64u);
                                if (key == "rating")
                                    return value->StringOrScalar(&rating, 16u);
                                if (key == "year")
                                    return value->StringOrScalar(&year, 32u);
                                if (key == "releaseDate" || key == "release_date")
                                    return value->StringOrScalar(&release_date, 32u);
                                return value->SkipValue();
                            }))
                return false;
            // The URL of a series entry is its get_series_info request; episodes are
            // resolved lazily when the user opens the series.
            std::string info_url;
            if (series_id.empty() || !series_ids.emplace(series_id).second ||
                !BuildXtreamApiUrlWithParam(credentials, "get_series_info", "series_id", series_id,
                                            &info_url))
            {
                if (report)
                    ++report->skipped;
                return true;
            }
            if (catalog->size() >= kMaxXtreamLibraryEntries)
            {
                if (report)
                    ++report->skipped;
                return true;
            }
            Channel channel;
            channel.kind = MediaKind::series;
            channel.id = LibraryId(source_id, "series", series_id);
            channel.source_id = source_id;
            channel.name = name.empty() ? "Series " + series_id : std::move(name);
            channel.group_title = CategoryName(category_names, category_id, "Series");
            channel.source_line = source_line;
            channel.series_id = std::move(series_id);
            channel.rating_tenths = ParseRatingTenths(rating);
            channel.year = ParseYear(year.empty() ? release_date : year);
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
        &found);
    if (!valid || !found)
    {
        catalog->Clear();
        return XtreamStatus::malformed_json;
    }
    return catalog->empty() ? XtreamStatus::no_series : XtreamStatus::ok;
}

XtreamStatus ParseXtreamSeriesInfo(std::string_view json, const XtreamCredentials &credentials,
                                   std::string_view series_id, std::string_view series_name,
                                   std::uint64_t source_id, Catalog *catalog, ParseReport *report)
{
    if (!LibraryArgumentsValid(catalog, credentials, source_id) || series_id.empty())
        return XtreamStatus::invalid_argument;
    catalog->Clear();
    catalog->source_id = source_id;
    if (report)
        *report = {};
    if (const XtreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;

    std::unordered_set<std::string> episode_ids;
    std::uint32_t source_line = 0;
    const auto read_episode = [&](JsonReader *entry, std::uint32_t season_hint)
    {
        ++source_line;
        if (report)
            ++report->lines_seen;
        std::string episode_id;
        std::string title;
        std::string extension;
        std::string episode_number;
        std::string season_number;
        std::string duration;
        if (!ReadObject(entry,
                        [&](const std::string &key, JsonReader *value)
                        {
                            if (key == "id")
                                return value->StringOrScalar(&episode_id, 64u);
                            if (key == "title")
                                return value->StringOrScalar(&title);
                            if (key == "container_extension")
                                return value->StringOrScalar(&extension, 12u);
                            if (key == "episode_num")
                                return value->StringOrScalar(&episode_number, 16u);
                            if (key == "season")
                                return value->StringOrScalar(&season_number, 16u);
                            // Providers send "info": [] for episodes without metadata.
                            if (key == "info" && value->Peek() == '{')
                                return ReadObject(value,
                                                  [&](const std::string &field, JsonReader *info)
                                                  {
                                                      if (field == "duration_secs")
                                                          return info->StringOrScalar(&duration,
                                                                                      16u);
                                                      return info->SkipValue();
                                                  });
                            return value->SkipValue();
                        }))
            return false;
        std::string primary_url;
        std::string transport_stream_url;
        if (episode_id.empty() || !episode_ids.emplace(episode_id).second ||
            !BuildXtreamEpisodeUrl(credentials, episode_id, "m3u8", &primary_url) ||
            !BuildXtreamEpisodeUrl(credentials, episode_id, "ts", &transport_stream_url) ||
            catalog->size() >= kMaxXtreamEpisodes)
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
        channel.id = LibraryId(source_id, "ep", episode_id);
        channel.source_id = source_id;
        channel.name = title.empty() ? std::string(label) : std::string(label) + " " + title;
        channel.tvg_name = std::string(series_name);
        channel.group_title = group;
        channel.source_line = source_line;
        channel.series_id = std::string(series_id);
        channel.season = static_cast<std::uint16_t>(season);
        channel.episode = static_cast<std::uint16_t>(number);
        channel.duration_secs = ParseUnsigned(duration, 24u * 3600u);
        channel.container_ext = std::move(extension);
        channel.url = std::move(primary_url);
        channel.alternate_urls.push_back(std::move(transport_stream_url));
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

std::size_t MergeXtreamLibraryPart(Catalog *library, Catalog &&part,
                                   std::unordered_set<std::string> *seen)
{
    if (!library || !seen)
        return 0;
    std::size_t added = 0;
    for (const ChannelView channel : part)
    {
        if (library->size() >= kMaxXtreamLibraryEntries)
            break;
        if (!seen->insert(std::string(channel.id)).second)
            continue;
        if (library->Add(channel))
            ++added;
    }
    part.Clear();
    return added;
}

XtreamStatus FetchXtreamLibrary(const XtreamCredentials &credentials, std::uint64_t source_id,
                                XtreamLibraryKind kind, const XtreamFetcher &fetcher,
                                Catalog *library, XtreamLibraryReport *report)
{
    if (!library || !fetcher.fetch || !ValidateXtreamCredentials(credentials) || source_id == 0)
        return XtreamStatus::invalid_argument;
    library->Clear();
    library->source_id = source_id;
    XtreamLibraryReport local;
    XtreamLibraryReport &stats = report ? *report : local;
    stats = {};

    const bool movies = kind == XtreamLibraryKind::movies;
    const char *categories_action = movies ? "get_vod_categories" : "get_series_categories";
    const char *streams_action = movies ? "get_vod_streams" : "get_series";
    const XtreamStatus empty_status = movies ? XtreamStatus::no_movies : XtreamStatus::no_series;
    const auto parse = [&](std::string_view body, const std::vector<XtreamCategory> &categories,
                           Catalog *catalog, ParseReport *parse_report)
    {
        return movies ? ParseXtreamVodStreams(body, credentials, categories, source_id, catalog,
                                              parse_report)
                      : ParseXtreamSeriesList(body, credentials, categories, source_id, catalog,
                                              parse_report);
    };
    const auto failure = [](XtreamFetchOutcome outcome)
    {
        if (outcome == XtreamFetchOutcome::cancelled)
            return XtreamStatus::cancelled;
        return outcome == XtreamFetchOutcome::too_large ? XtreamStatus::too_large
                                                        : XtreamStatus::fetch_failed;
    };

    std::string url;
    std::string_view body;
    // Categories give entries readable group names. A provider that cannot list them still
    // gets its streams imported under a generic group, so a bad list is not fatal.
    std::vector<XtreamCategory> categories;
    if (!BuildXtreamApiUrl(credentials, categories_action, &url))
        return XtreamStatus::invalid_argument;
    ++stats.requests;
    const XtreamFetchOutcome category_outcome = fetcher.fetch(fetcher.context, url, &body);
    if (category_outcome == XtreamFetchOutcome::cancelled ||
        category_outcome == XtreamFetchOutcome::failed)
        return failure(category_outcome);
    if (category_outcome == XtreamFetchOutcome::ok &&
        ParseXtreamCategories(body, &categories) != XtreamStatus::ok)
        categories.clear();
    stats.categories = static_cast<unsigned>(categories.size());

    if (!BuildXtreamApiUrl(credentials, streams_action, &url))
        return XtreamStatus::invalid_argument;
    ++stats.requests;
    const XtreamFetchOutcome full_outcome = fetcher.fetch(fetcher.context, url, &body);
    if (full_outcome == XtreamFetchOutcome::ok)
    {
        ParseReport parse_report;
        const XtreamStatus status = parse(body, categories, library, &parse_report);
        stats.entries_skipped = static_cast<unsigned>(parse_report.skipped);
        if (status != XtreamStatus::ok)
            library->Clear();
        return status;
    }
    if (full_outcome != XtreamFetchOutcome::too_large)
        return failure(full_outcome);

    // The whole list is bigger than one response may be; split it by category.
    stats.used_category_fallback = true;
    if (categories.empty())
        return XtreamStatus::too_large;
    std::unordered_set<std::string> seen;
    for (const XtreamCategory &category : categories)
    {
        if (!BuildXtreamApiUrlWithParam(credentials, streams_action, "category_id", category.id,
                                        &url))
        {
            ++stats.categories_skipped;
            continue;
        }
        ++stats.requests;
        const XtreamFetchOutcome outcome = fetcher.fetch(fetcher.context, url, &body);
        if (outcome == XtreamFetchOutcome::cancelled || outcome == XtreamFetchOutcome::failed)
        {
            library->Clear();
            return failure(outcome);
        }
        if (outcome == XtreamFetchOutcome::too_large)
        {
            ++stats.categories_skipped;
            continue;
        }
        Catalog part;
        ParseReport parse_report;
        const XtreamStatus status = parse(body, categories, &part, &parse_report);
        stats.entries_skipped += static_cast<unsigned>(parse_report.skipped);
        if (status == XtreamStatus::ok)
            MergeXtreamLibraryPart(library, std::move(part), &seen);
        else if (status != empty_status)
            ++stats.categories_skipped;
    }
    if (library->empty())
    {
        library->Clear();
        return empty_status;
    }
    return XtreamStatus::ok;
}

namespace
{

constexpr std::size_t kMaxPlotBytes = 640u;
constexpr std::size_t kMaxCreditBytes = 240u;

// Cuts text to at most `maximum` bytes without splitting a UTF-8 sequence.
void TruncateUtf8(std::string *text, std::size_t maximum)
{
    if (text->size() <= maximum)
        return;
    std::size_t end = maximum;
    while (end && (static_cast<unsigned char>((*text)[end]) & 0xc0u) == 0x80u)
        --end;
    text->resize(end);
    text->append("...");
}

// "01:32:10" or "5530" -> seconds.
std::uint32_t ParseClockOrSeconds(std::string_view text)
{
    if (text.find(':') == std::string_view::npos)
        return ParseUnsigned(text, 24u * 3600u);
    std::uint32_t total = 0;
    std::size_t start = 0;
    for (unsigned part = 0; part < 3u; ++part)
    {
        const std::size_t colon = text.find(':', start);
        total = total * 60u + ParseUnsigned(text.substr(start, colon - start), 60000u);
        if (colon == std::string_view::npos)
            break;
        start = colon + 1u;
    }
    return std::min<std::uint32_t>(total, 24u * 3600u);
}

} // namespace

XtreamStatus ParseMediaInfo(std::string_view json, MediaDetails *details)
{
    if (!details)
        return XtreamStatus::invalid_argument;
    *details = {};
    if (const XtreamStatus size = CheckResponseSize(json); size != XtreamStatus::ok)
        return size;
    std::string duration;
    std::string duration_secs;
    std::string run_time;
    std::string rating;
    std::string video_width;
    std::string video_height;
    const auto text = [](JsonReader *value, std::string *output)
    {
        // Arrays and objects (backdrop lists, nested credits) carry nothing shown here.
        const char next = value->Peek();
        if (next == '[' || next == '{')
            return value->SkipValue();
        if (!value->StringOrScalar(output, 16u * 1024u))
            return false;
        if (*output == "null")
            output->clear();
        return true;
    };
    JsonReader reader(json);
    bool found = false;
    const bool valid = ReadObject(
        &reader,
        [&](const std::string &key, JsonReader *value)
        {
            if (key != "info" || value->Peek() != '{')
                return value->SkipValue();
            found = true;
            return ReadObject(
                value,
                [&](const std::string &field, JsonReader *entry)
                {
                    if (field == "plot" || (field == "description" && details->plot.empty()))
                        return text(entry, &details->plot);
                    if (field == "genre")
                        return text(entry, &details->genre);
                    if (field == "cast" || (field == "actors" && details->cast.empty()))
                        return text(entry, &details->cast);
                    if (field == "director")
                        return text(entry, &details->director);
                    if (field == "releasedate" || field == "releaseDate" || field == "release_date")
                        return text(entry, &details->release_date);
                    if (field == "duration_secs")
                        return text(entry, &duration_secs);
                    if (field == "duration")
                        return text(entry, &duration);
                    if (field == "episode_run_time")
                        return text(entry, &run_time);
                    if (field == "rating")
                        return text(entry, &rating);
                    if (field == "video" && entry->Peek() == '{')
                        return ReadObject(entry,
                                          [&](const std::string &name, JsonReader *item)
                                          {
                                              if (name == "width")
                                                  return text(item, &video_width);
                                              if (name == "height")
                                                  return text(item, &video_height);
                                              if (name == "codec_name")
                                                  return text(item, &details->video_codec);
                                              return item->SkipValue();
                                          });
                    return entry->SkipValue();
                });
        });
    if (!valid || !reader.Finished())
    {
        *details = {};
        return XtreamStatus::malformed_json;
    }
    if (!found)
        return XtreamStatus::ok;
    TruncateUtf8(&details->plot, kMaxPlotBytes);
    TruncateUtf8(&details->genre, kMaxCreditBytes);
    TruncateUtf8(&details->cast, kMaxCreditBytes);
    TruncateUtf8(&details->director, kMaxCreditBytes);
    TruncateUtf8(&details->release_date, 32u);
    details->duration_secs = !duration_secs.empty() ? ParseUnsigned(duration_secs, 24u * 3600u)
                             : !duration.empty()    ? ParseClockOrSeconds(duration)
                                                    : ParseUnsigned(run_time, 24u * 60u) * 60u;
    details->rating_tenths = ParseRatingTenths(rating);
    details->video_width = ParseUnsigned(video_width, 16384u);
    details->video_height = ParseUnsigned(video_height, 16384u);
    TruncateUtf8(&details->video_codec, 16u);
    return XtreamStatus::ok;
}

const char *XtreamStatusDescription(XtreamStatus status)
{
    switch (status)
    {
    case XtreamStatus::ok:
        return "ready";
    case XtreamStatus::invalid_argument:
        return "invalid Xtream server or credentials";
    case XtreamStatus::not_found:
        return "Xtream credentials are not configured";
    case XtreamStatus::too_large:
        return "Xtream response exceeds the supported size";
    case XtreamStatus::io_error:
        return "Xtream credentials could not be stored";
    case XtreamStatus::corrupt:
        return "saved Xtream credentials are invalid";
    case XtreamStatus::malformed_json:
        return "provider returned malformed Xtream data";
    case XtreamStatus::authentication_failed:
        return "Xtream username or password was rejected";
    case XtreamStatus::account_inactive:
        return "Xtream account is expired or inactive";
    case XtreamStatus::no_channels:
        return "Xtream provider returned no live channels";
    case XtreamStatus::no_movies:
        return "Xtream provider returned no movies";
    case XtreamStatus::no_series:
        return "Xtream provider returned no series";
    case XtreamStatus::no_episodes:
        return "Xtream provider returned no episodes";
    case XtreamStatus::fetch_failed:
        return "Xtream request failed";
    case XtreamStatus::cancelled:
        return "Xtream refresh was cancelled";
    }
    return "Xtream request failed";
}

} // namespace iptv
