/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_panel.h"

#include "iptv_catalog.h"
#include "iptv_json.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <unordered_set>
#include <utility>

// A local build may define IPTV_DEFAULT_PANEL_URL here; the file is never committed.
#if defined(__has_include)
#if __has_include("iptv_panel_local.h")
#include "iptv_panel_local.h"
#endif
#endif
#ifndef IPTV_DEFAULT_PANEL_URL
#define IPTV_DEFAULT_PANEL_URL ""
#endif

namespace iptv
{
namespace
{

constexpr char kPanelMagic[] = "PROSPEROTV-PANEL-1";
constexpr std::size_t kMaxServerNameBytes = 64u;

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

std::vector<std::string_view> SplitCommas(std::string_view list)
{
    std::vector<std::string_view> parts;
    while (true)
    {
        const std::size_t comma = list.find(',');
        parts.push_back(Trim(list.substr(0, comma)));
        if (comma == std::string_view::npos || parts.size() > kMaxPanelServers * 2u)
            break;
        list.remove_prefix(comma + 1u);
    }
    return parts;
}

std::string HostOf(const std::string &url)
{
    const std::size_t start = url.find("://");
    if (start == std::string::npos)
        return url;
    const std::size_t end = url.find_first_of("/:?#", start + 3u);
    return url.substr(start + 3u, end == std::string::npos ? std::string::npos : end - start - 3u);
}

} // namespace

const char *BuiltInPanelUrl()
{
    return IPTV_DEFAULT_PANEL_URL;
}

bool NormalizePanelUrl(std::string_view input, std::string *normalized)
{
    input = Trim(input);
    if (!normalized || input.empty() || input.size() > kMaxPanelUrlBytes ||
        input.find('?') != std::string_view::npos || input.find('#') != std::string_view::npos)
        return false;
    std::string candidate(input);
    if (candidate.find("://") == std::string::npos)
        candidate.insert(0, "http://");
    std::string canonical;
    if (!CanonicalizeStreamUrl(candidate, &canonical))
        return false;
    constexpr std::string_view list = "dns.php";
    if (json::EndsWithCi(canonical, list))
        canonical.resize(canonical.size() - list.size());
    while (!canonical.empty() && canonical.back() == '/')
        canonical.pop_back();
    if (canonical.size() < 8u)
        return false;
    canonical.push_back('/');
    if (canonical.size() > kMaxPanelUrlBytes)
        return false;
    *normalized = std::move(canonical);
    return true;
}

bool BuildPanelServerListUrl(const std::string &panel_url, std::string *url)
{
    std::string normalized;
    if (!url || !NormalizePanelUrl(panel_url, &normalized) || normalized != panel_url)
        return false;
    *url = panel_url + "dns.php";
    return url->size() <= kDefaultMaxUrlBytes;
}

PanelStatus ParsePanelServers(std::string_view json, std::vector<PanelServer> *servers)
{
    if (!servers)
        return PanelStatus::invalid_argument;
    servers->clear();
    if (json.empty() || json.size() > kMaxPanelResponseBytes)
        return PanelStatus::malformed_json;
    std::string urls;
    std::string names;
    bool found = false;
    json::JsonReader reader(json);
    const bool valid = json::ReadObject(&reader,
                                        [&](const std::string &key, json::JsonReader *value)
                                        {
                                            if (key == "su")
                                            {
                                                found = true;
                                                return value->StringOrScalar(&urls, 64u * 1024u);
                                            }
                                            if (key == "sn")
                                                return value->StringOrScalar(&names, 16u * 1024u);
                                            return value->SkipValue();
                                        });
    if (!valid || !reader.Finished() || !found)
        return PanelStatus::malformed_json;
    const std::vector<std::string_view> url_parts = SplitCommas(urls);
    const std::vector<std::string_view> name_parts =
        names.empty() ? std::vector<std::string_view>{} : SplitCommas(names);
    std::unordered_set<std::string> seen;
    for (std::size_t index = 0; index < url_parts.size() && servers->size() < kMaxPanelServers;
         ++index)
    {
        std::string candidate(url_parts[index]);
        if (candidate.empty())
            continue;
        if (candidate.find("://") == std::string::npos)
            candidate.insert(0, "http://");
        std::string canonical;
        if (!CanonicalizeStreamUrl(candidate, &canonical) || !seen.insert(canonical).second)
            continue;
        PanelServer server;
        if (index < name_parts.size() && !name_parts[index].empty())
            server.name = std::string(name_parts[index].substr(0, kMaxServerNameBytes));
        else
            server.name = HostOf(canonical);
        server.url = std::move(canonical);
        servers->push_back(std::move(server));
    }
    return servers->empty() ? PanelStatus::no_servers : PanelStatus::ok;
}

PanelStatus SavePanelUrl(const std::string &path, const std::string &panel_url)
{
    std::string normalized;
    if (path.empty() || !NormalizePanelUrl(panel_url, &normalized) || normalized != panel_url)
        return PanelStatus::invalid_argument;
    const std::string temporary = path + ".tmp";
    std::FILE *output = std::fopen(temporary.c_str(), "wb");
    if (!output)
        return PanelStatus::io_error;
    const std::string contents = std::string(kPanelMagic) + "\n" + panel_url + "\n";
    bool written = std::fwrite(contents.data(), 1, contents.size(), output) == contents.size();
    written = std::fflush(output) == 0 && written;
    written = std::fclose(output) == 0 && written;
    if (!written || !json::ReplaceFile(temporary, path))
    {
        std::remove(temporary.c_str());
        return PanelStatus::io_error;
    }
    return PanelStatus::ok;
}

PanelStatus LoadPanelUrl(const std::string &path, std::string *panel_url)
{
    if (path.empty() || !panel_url)
        return PanelStatus::invalid_argument;
    std::FILE *input = std::fopen(path.c_str(), "rb");
    if (!input)
        return PanelStatus::not_found;
    std::array<char, sizeof(kPanelMagic) + kMaxPanelUrlBytes + 8u> file{};
    const std::size_t bytes = std::fread(file.data(), 1, file.size(), input);
    const bool failed = std::ferror(input) != 0 || std::fclose(input) != 0;
    if (failed)
        return PanelStatus::io_error;
    if (bytes == file.size())
        return PanelStatus::corrupt;
    const std::string_view contents(file.data(), bytes);
    const std::size_t first = contents.find('\n');
    if (first == std::string_view::npos || contents.substr(0, first) != kPanelMagic)
        return PanelStatus::corrupt;
    const std::size_t second = contents.find('\n', first + 1u);
    if (second == std::string_view::npos || second + 1u != contents.size())
        return PanelStatus::corrupt;
    const std::string url(contents.substr(first + 1u, second - first - 1u));
    std::string normalized;
    if (!NormalizePanelUrl(url, &normalized) || normalized != url)
        return PanelStatus::corrupt;
    *panel_url = url;
    return PanelStatus::ok;
}

PanelStatus SavePanelUrl(const std::string &panel_url)
{
    return SavePanelUrl(kDefaultPanelPath, panel_url);
}

PanelStatus LoadPanelUrl(std::string *panel_url)
{
    return LoadPanelUrl(kDefaultPanelPath, panel_url);
}

const char *PanelStatusDescription(PanelStatus status)
{
    switch (status)
    {
    case PanelStatus::ok:
        return "ready";
    case PanelStatus::invalid_argument:
        return "invalid panel address";
    case PanelStatus::not_found:
        return "no panel is set";
    case PanelStatus::io_error:
        return "the panel address could not be stored";
    case PanelStatus::corrupt:
        return "the saved panel address is invalid";
    case PanelStatus::malformed_json:
        return "the panel returned an unreadable server list";
    case PanelStatus::no_servers:
        return "the panel listed no servers";
    }
    return "the panel returned an unreadable server list";
}

} // namespace iptv
