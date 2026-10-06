/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_http.h"
#include "iptv_source_state.h"
#include "iptv_xtream.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{

iptv::XtreamCredentials Credentials()
{
    return {"https://provider.example:25461", "test user", "p@ss&word"};
}

TEST(IptvXtreamTest, NormalizesServerAndBuildsEncodedEndpoints)
{
    std::string server;
    ASSERT_TRUE(
        iptv::NormalizeXtreamServerUrl("HTTPS://Provider.Example:25461/player_api.php/", &server));
    EXPECT_EQ(server, "https://provider.example:25461");

    const iptv::XtreamCredentials credentials = Credentials();
    std::string url;
    ASSERT_TRUE(iptv::BuildXtreamApiUrl(credentials, "get_live_streams", &url));
    EXPECT_EQ(url, "https://provider.example:25461/player_api.php?username=test%20user&password="
                   "p%40ss%26word&action=get_live_streams");
    ASSERT_TRUE(iptv::BuildXtreamLiveUrl(credentials, "42", "m3u8", &url));
    EXPECT_EQ(url, "https://provider.example:25461/live/test%20user/p%40ss%26word/42.m3u8");
}

TEST(IptvXtreamTest, ParsesAuthenticationAndExplainsAccountFailures)
{
    iptv::XtreamAuth auth;
    EXPECT_EQ(
        iptv::ParseXtreamAuth(
            R"({"user_info":{"auth":1,"status":"Active","message":"Welcome"},"server_info":{}})",
            &auth),
        iptv::XtreamStatus::ok);
    EXPECT_TRUE(auth.authenticated);
    EXPECT_EQ(auth.message, "Welcome");

    EXPECT_EQ(iptv::ParseXtreamAuth(
                  R"({"user_info":{"auth":"0","status":"Disabled","message":"Bad login"}})", &auth),
              iptv::XtreamStatus::authentication_failed);
    EXPECT_EQ(auth.message, "Bad login");

    EXPECT_EQ(iptv::ParseXtreamAuth(R"({"user_info":{"auth":true,"status":"Expired"}})", &auth),
              iptv::XtreamStatus::account_inactive);
}

TEST(IptvXtreamTest, ConvertsLiveJsonIntoTheSharedCatalog)
{
    const iptv::XtreamCredentials credentials = Credentials();
    std::vector<iptv::XtreamCategory> categories;
    ASSERT_EQ(
        iptv::ParseXtreamCategories(
            R"({"data":[{"category_id":"7","category_name":"Not\u00edcias"},{"category_id":8,"category_name":"Sports"}]})",
            &categories),
        iptv::XtreamStatus::ok);
    ASSERT_EQ(categories.size(), 2u);
    EXPECT_EQ(categories[0].name, "Not\xc3\xad"
                                  "cias");

    constexpr std::string_view streams =
        R"([{"stream_id":101,"name":"Canal \u00c1","stream_icon":"https:\/\/images.example\/101.png","epg_channel_id":"canal.a","category_id":"7","container_extension":"m3u8","direct_source":"https:\/\/cdn.example\/live\/master.m3u8","ignored":{"nested":[1,true,null]}},{"stream_id":"102","name":"Sports HD","category_id":8,"container_extension":"ts","stream_url":"https:\/\/cdn.example\/sports.ts"},{"name":"Missing id"}])";
    iptv::CatalogState catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamLiveStreams(streams, credentials, categories, 0x5854000000001234u,
                                           &catalog, &report),
              iptv::XtreamStatus::ok);
    ASSERT_EQ(catalog.channels.size(), 2u);
    EXPECT_EQ(catalog.channels[0].name, "Canal \xc3\x81");
    EXPECT_EQ(catalog.channels[0].group_title, "Not\xc3\xad"
                                               "cias");
    EXPECT_EQ(catalog.channels[0].tvg_id, "canal.a");
    EXPECT_EQ(catalog.channels[0].url, "https://cdn.example/live/master.m3u8");
    ASSERT_EQ(catalog.channels[0].alternate_urls.size(), 1u);
    EXPECT_EQ(catalog.channels[0].alternate_urls[0],
              "https://provider.example:25461/live/test%20user/p%40ss%26word/101.m3u8");
    EXPECT_EQ(catalog.channels[1].group_title, "Sports");
    EXPECT_EQ(catalog.channels[1].url, "https://cdn.example/sports.ts");
    EXPECT_EQ(report.accepted, 2u);
    EXPECT_EQ(report.skipped, 1u);
}

TEST(IptvXtreamTest, LoadsAProviderWithTensOfThousandsOfChannels)
{
    // 33,000 streams of about 600 bytes each: an answer near 20 MiB, past the
    // 16 MiB the app once stopped at ("playlist too large").
    const iptv::XtreamCredentials credentials = Credentials();
    const std::vector<iptv::XtreamCategory> categories;
    constexpr int kStreams = 33000;
    const std::string padding(430, 'x');
    std::string streams = "[";
    streams.reserve(21u * 1024u * 1024u);
    for (int i = 0; i < kStreams; ++i)
    {
        if (i != 0)
            streams += ',';
        const std::string number = std::to_string(i + 1);
        streams +=
            R"({"num":)" + number + R"(,"name":"Channel )" + number +
            R"(","stream_type":"live","stream_id":)" + number +
            R"(,"stream_icon":"https:\/\/images.example\/)" + padding + number +
            R"(.png","epg_channel_id":"channel.)" + number +
            R"(","added":"1700000000","is_adult":"0","category_id":"1","custom_sid":"","tv_archive":0,"direct_source":"","tv_archive_duration":0})";
    }
    streams += "]";
    ASSERT_GT(streams.size(), 16u * 1024u * 1024u);
    ASSERT_LE(streams.size(), iptv::kMaxXtreamResponseBytes);

    iptv::CatalogState catalog;
    iptv::ParseReport report;
    ASSERT_EQ(iptv::ParseXtreamLiveStreams(streams, credentials, categories, 0x5854000000001234u,
                                           &catalog, &report),
              iptv::XtreamStatus::ok);
    // Every channel the app has room for; the rest are counted, not an error.
    EXPECT_EQ(catalog.channels.size(), iptv::kDefaultMaxChannels);
    EXPECT_EQ(report.skipped, static_cast<std::size_t>(kStreams) - iptv::kDefaultMaxChannels);
    EXPECT_EQ(catalog.channels.front().name, "Channel 1");
    EXPECT_EQ(catalog.channels.back().name, "Channel " + std::to_string(iptv::kDefaultMaxChannels));
}

TEST(IptvXtreamTest, ChannelListStorageHoldsTheLargestAnswer)
{
    iptv::http::ListBuffer buffer = iptv::http::AllocateListBuffer();
    ASSERT_NE(buffer.data(), nullptr);
    EXPECT_EQ(buffer.max_bytes, iptv::http::kHardMaxPlaylistBytes);
    EXPECT_EQ(buffer.size(), buffer.max_bytes + 1u);
    EXPECT_GE(iptv::http::kHardMaxPlaylistBytes, iptv::kMaxXtreamResponseBytes);
    // Never less than asked for as the least.
    iptv::http::ListBuffer small = iptv::http::AllocateListBuffer(1024u, 4096u);
    ASSERT_NE(small.data(), nullptr);
    EXPECT_EQ(small.max_bytes, 4096u);
}

TEST(IptvXtreamTest, RejectsMalformedDataAndPersistsCredentialsAndSource)
{
    const iptv::XtreamCredentials credentials = Credentials();
    iptv::CatalogState catalog;
    EXPECT_EQ(iptv::ParseXtreamLiveStreams("[{", credentials, {}, 1u, &catalog),
              iptv::XtreamStatus::malformed_json);

    const std::string prefix = std::string(::testing::TempDir()) + "prosperotv-xtream-test-";
    const std::string credentials_path = prefix + "credentials";
    const std::string source_path = prefix + "source";
    ASSERT_EQ(iptv::SaveXtreamCredentials(credentials_path, credentials), iptv::XtreamStatus::ok);
    iptv::XtreamCredentials loaded;
    ASSERT_EQ(iptv::LoadXtreamCredentials(credentials_path, &loaded), iptv::XtreamStatus::ok);
    EXPECT_EQ(loaded.server_url, credentials.server_url);
    EXPECT_EQ(loaded.username, credentials.username);
    EXPECT_EQ(loaded.password, credentials.password);

    ASSERT_EQ(iptv::SaveActiveSource(source_path, iptv::SourceKind::Xtream),
              iptv::SourceStateStatus::ok);
    iptv::SourceKind source = iptv::SourceKind::BuiltIn;
    ASSERT_EQ(iptv::LoadActiveSource(source_path, &source), iptv::SourceStateStatus::ok);
    EXPECT_EQ(source, iptv::SourceKind::Xtream);
    std::remove(credentials_path.c_str());
    std::remove(source_path.c_str());
}

} // namespace
