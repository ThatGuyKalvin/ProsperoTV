/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_panel.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace
{

using iptv::PanelStatus;

TEST(IptvPanelTest, NormalizesPanelAddresses)
{
    std::string panel;
    ASSERT_TRUE(iptv::NormalizePanelUrl("panel.example/psp/api", &panel));
    EXPECT_EQ(panel, "http://panel.example/psp/api/");
    ASSERT_TRUE(iptv::NormalizePanelUrl(" HTTPS://Panel.Example/api/dns.php ", &panel));
    EXPECT_EQ(panel, "https://panel.example/api/");
    ASSERT_TRUE(iptv::NormalizePanelUrl("https://panel.example", &panel));
    EXPECT_EQ(panel, "https://panel.example/");
    for (const char *bad : {"", "ftp://panel.example/", "https://panel.example/?x=1",
                            "https://panel.example/#a", "https://user@panel.example/"})
        EXPECT_FALSE(iptv::NormalizePanelUrl(bad, &panel)) << bad;

    std::string url;
    ASSERT_TRUE(iptv::BuildPanelServerListUrl("https://panel.example/api/", &url));
    EXPECT_EQ(url, "https://panel.example/api/dns.php");
    EXPECT_FALSE(iptv::BuildPanelServerListUrl("https://panel.example/api", &url));
}

TEST(IptvPanelTest, ReadsNamedServerLists)
{
    std::vector<iptv::PanelServer> servers;
    ASSERT_EQ(iptv::ParsePanelServers(
                  R"({"su":"http://one.example:8080, https://Two.Example/,three.example,ftp://bad,)"
                  R"(http://one.example:8080","sn":"Allvue, Maximus"})",
                  &servers),
              PanelStatus::ok);
    ASSERT_EQ(servers.size(), 3u);
    EXPECT_EQ(servers[0].name, "Allvue");
    EXPECT_EQ(servers[0].url, "http://one.example:8080");
    EXPECT_EQ(servers[1].name, "Maximus");
    EXPECT_EQ(servers[1].url, "https://two.example/");
    // Unnamed servers are named after their host.
    EXPECT_EQ(servers[2].name, "three.example");
    EXPECT_EQ(servers[2].url, "http://three.example");

    ASSERT_EQ(iptv::ParsePanelServers(R"({"su":"http://a.example"})", &servers), PanelStatus::ok);
    EXPECT_EQ(servers[0].name, "a.example");

    EXPECT_EQ(iptv::ParsePanelServers(R"({"su":""})", &servers), PanelStatus::no_servers);
    EXPECT_EQ(iptv::ParsePanelServers(R"({"su":"ftp://x"})", &servers), PanelStatus::no_servers);
    EXPECT_EQ(iptv::ParsePanelServers(R"({"sn":"x"})", &servers), PanelStatus::malformed_json);
    EXPECT_EQ(iptv::ParsePanelServers("[]", &servers), PanelStatus::malformed_json);
    EXPECT_EQ(iptv::ParsePanelServers("", &servers), PanelStatus::malformed_json);
}

TEST(IptvPanelTest, PersistsThePanelAddress)
{
    const std::string path = std::string(::testing::TempDir()) + "prosperotv-panel-test";
    ASSERT_EQ(iptv::SavePanelUrl(path, "https://panel.example/api/"), PanelStatus::ok);
    std::string panel;
    ASSERT_EQ(iptv::LoadPanelUrl(path, &panel), PanelStatus::ok);
    EXPECT_EQ(panel, "https://panel.example/api/");
    EXPECT_EQ(iptv::SavePanelUrl(path, "https://panel.example/api"), PanelStatus::invalid_argument);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "PROSPEROTV-PANEL-1\nnot a url\n";
    }
    EXPECT_EQ(iptv::LoadPanelUrl(path, &panel), PanelStatus::corrupt);
    std::remove(path.c_str());
    EXPECT_EQ(iptv::LoadPanelUrl(path, &panel), PanelStatus::not_found);
    // The built-in default is either empty or a normalized address.
    const std::string built_in = iptv::BuiltInPanelUrl();
    std::string normalized;
    EXPECT_TRUE(built_in.empty() ||
                (iptv::NormalizePanelUrl(built_in, &normalized) && normalized == built_in));
}

} // namespace
