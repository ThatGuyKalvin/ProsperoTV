/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef IPTV_PANEL_H
#define IPTV_PANEL_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Provider panels: the reseller back ends that IPTV Smarters builds such as PSUV5 ask for a
// list of named servers ({panel}dns.php -> {"su":"url1,url2","sn":"name1,name2"}). The user
// picks one instead of typing a server address.
//
// No panel address is part of the source. A build can include a default one from
// include/iptv_panel_local.h, which git ignores (see iptv_panel_local.h.example), and the
// user can set or change it on the console.
namespace iptv
{

inline constexpr char kDefaultPanelPath[] = "/download0/prosperotv-panel-v1.txt";
inline constexpr std::size_t kMaxPanelUrlBytes = 1020u;
inline constexpr std::size_t kMaxPanelServers = 64u;
inline constexpr std::size_t kMaxPanelResponseBytes = 256u * 1024u;

enum class PanelStatus : std::uint8_t
{
    ok,
    invalid_argument,
    not_found,
    io_error,
    corrupt,
    malformed_json,
    no_servers,
};

struct PanelServer
{
    std::string name;
    std::string url;
};

// The panel compiled in from include/iptv_panel_local.h, or an empty string.
const char *BuiltInPanelUrl();

// Accepts "panel.example/api", "https://panel.example/api/" or a pasted ".../dns.php"
// address and produces "http(s)://panel.example/api/" (always ending in '/').
bool NormalizePanelUrl(std::string_view input, std::string *normalized);
bool BuildPanelServerListUrl(const std::string &panel_url, std::string *url);

// Reads dns.php. Servers without a usable address are skipped; a server without a name is
// named after its host.
PanelStatus ParsePanelServers(std::string_view json, std::vector<PanelServer> *servers);

PanelStatus SavePanelUrl(const std::string &path, const std::string &panel_url);
PanelStatus LoadPanelUrl(const std::string &path, std::string *panel_url);
PanelStatus SavePanelUrl(const std::string &panel_url);
PanelStatus LoadPanelUrl(std::string *panel_url);

const char *PanelStatusDescription(PanelStatus status);

} // namespace iptv

#endif
