/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_catalog.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

constexpr std::uint64_t kSourceId = 0x1234u;

bool HasIssue(const iptv::ParseReport &report, iptv::ParseIssueCode code)
{
    return std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
                       [code](const iptv::ParseDiagnostic &diagnostic)
                       { return diagnostic.code == code; });
}

TEST(IptvCatalogTest, ParsesCrLfHttpOptionsAndSearchMetadata)
{
    const std::string playlist = "\xef\xbb\xbf#EXTM3U\r\n"
                                 "#EXTINF:-1 TVG-ID=\"news.br\" TVG-COUNTRY=\"BR\" "
                                 "TVG-LANGUAGE=\"Portuguese\",Brazil News\r\n"
                                 "#EXTVLCOPT:HTTP-USER-AGENT=Mozilla/5.0 (PlayStation 5)\r\n"
                                 "#EXTVLCOPT:http-referrer=https://portal.example/watch?id=7\r\n"
                                 "HTTPS://CDN.Example/live.m3u8#ignored-fragment\r\n";

    iptv::ParseReport report;
    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, {}, &report);

    ASSERT_EQ(catalog.size(), 1u);
    const iptv::ChannelView channel = catalog.front();
    EXPECT_EQ(channel.tvg_country, "BR");
    EXPECT_EQ(channel.tvg_language, "Portuguese");
    EXPECT_EQ(channel.http_user_agent, "Mozilla/5.0 (PlayStation 5)");
    EXPECT_EQ(channel.http_referrer, "https://portal.example/watch?id=7");
    EXPECT_EQ(channel.url, "https://cdn.example/live.m3u8");
    EXPECT_EQ(channel.source_line, 2u);
    EXPECT_EQ(report.accepted, 1u);
    EXPECT_EQ(report.skipped, 0u);
}

TEST(IptvCatalogTest, AcceptsRefererAliasAndTrimsOptionWhitespace)
{
    constexpr std::string_view playlist =
        "#EXTM3U\n"
        "#EXTINF:-1,Alias\n"
        "#extvlcopt: http-user-agent =  Agent with spaces  \n"
        "#EXTVLCOPT:http-referer=  https://origin.example/page  \n"
        "http://stream.example/live\n";

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId);

    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_EQ(catalog[0].http_user_agent, "Agent with spaces");
    EXPECT_EQ(catalog[0].http_referrer, "https://origin.example/page");
}

TEST(IptvCatalogTest, RejectsControlCharactersInHttpOptionValues)
{
    std::string playlist = "#EXTM3U\n"
                           "#EXTINF:-1,Unsafe headers\n"
                           "#EXTVLCOPT:http-user-agent=SafeAgent";
    playlist.push_back('\r');
    playlist += "Injected: true\n"
                "#EXTVLCOPT:http-referrer=https://origin.example/";
    playlist.push_back(static_cast<char>(0x7f));
    playlist += "Injected\nhttp://stream.example/live\n";

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId);

    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_TRUE(catalog[0].http_user_agent.empty());
    EXPECT_TRUE(catalog[0].http_referrer.empty());
}

TEST(IptvCatalogTest, DoesNotLeakOptionsAcrossRecords)
{
    constexpr std::string_view playlist = "#EXTM3U\n"
                                          "#EXTVLCOPT:http-user-agent=BeforeRecord\n"
                                          "#EXTINF:-1,Broken\n"
                                          "#EXTVLCOPT:http-user-agent=BrokenAgent\n"
                                          "ftp://stream.example/broken\n"
                                          "#EXTINF:-1,Clean\n"
                                          "http://stream.example/clean\n";

    iptv::ParseReport report;
    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, {}, &report);

    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_EQ(catalog[0].name, "Clean");
    EXPECT_TRUE(catalog[0].http_user_agent.empty());
    EXPECT_TRUE(catalog[0].http_referrer.empty());
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::unsupported_url_scheme));
}

TEST(IptvCatalogTest, IgnoresUnknownMalformedAndOversizedHttpOptions)
{
    constexpr std::string_view playlist = "#EXTM3U\n"
                                          "#EXTINF:-1,Channel\n"
                                          "#EXTVLCOPT:http-user-agent\n"
                                          "#EXTVLCOPT:network-caching=1000\n"
                                          "#EXTVLCOPT:http-user-agent=123456789\n"
                                          "http://stream.example/live\n";
    iptv::ParseLimits limits;
    limits.max_field_bytes = 8;

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, limits);

    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_TRUE(catalog[0].http_user_agent.empty());
}

TEST(IptvCatalogTest, KeepsCountryAndLanguageWhenMergingDuplicates)
{
    constexpr std::string_view playlist = "#EXTM3U\n"
                                          "#EXTINF:-1 tvg-id=\"station\",First\n"
                                          "http://one.example/live\n"
                                          "#EXTINF:-1 tvg-id=\"STATION\" tvg-country=\"CA\" "
                                          "tvg-language=\"French\",Second\n"
                                          "http://two.example/live\n";

    iptv::ParseReport report;
    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, {}, &report);

    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_EQ(catalog[0].tvg_country, "CA");
    EXPECT_EQ(catalog[0].tvg_language, "French");
    ASSERT_EQ(catalog[0].alternate_urls.size(), 1u);
    EXPECT_EQ(catalog[0].alternate_urls[0], "http://two.example/live");
    EXPECT_EQ(report.duplicates, 1u);
}

TEST(IptvCatalogTest, RejectsPlaylistBeyondConfiguredLimit)
{
    constexpr std::string_view playlist = "#EXTINF:-1,One\nhttp://one.example/live\n";
    iptv::ParseLimits limits;
    limits.max_playlist_bytes = playlist.size() - 1u;
    iptv::ParseReport report;

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, limits, &report);

    EXPECT_TRUE(catalog.empty());
    EXPECT_TRUE(report.input_too_large);
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::input_too_large));
}

TEST(IptvCatalogTest, EnforcesFieldUrlAndChannelLimits)
{
    constexpr std::string_view playlist = "#EXTM3U\n"
                                          "#EXTINF:-1,TitleTooLong\n"
                                          "http://field.example/live\n"
                                          "#EXTINF:-1,One\n"
                                          "http://one.example/path-is-too-long\n"
                                          "#EXTINF:-1,Two\n"
                                          "http://two.example/x\n"
                                          "#EXTINF:-1,Three\n"
                                          "http://three.example/x\n";
    iptv::ParseLimits limits;
    limits.max_field_bytes = 5;
    limits.max_url_bytes = 26;
    limits.max_channels = 1;
    iptv::ParseReport report;

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, limits, &report);

    ASSERT_EQ(catalog.size(), 1u);
    EXPECT_EQ(catalog[0].name, "Two");
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::attribute_too_long));
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::url_too_long));
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::catalog_full));
}

TEST(IptvCatalogTest, OverlongRecordClearsPendingEntry)
{
    constexpr std::string_view playlist = "#EXTINF:-1,Pending\n"
                                          "# this comment is deliberately too long\n"
                                          "http://stream.example/live\n";
    iptv::ParseLimits limits;
    limits.max_record_bytes = 30;
    iptv::ParseReport report;

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, limits, &report);

    EXPECT_TRUE(catalog.empty());
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::overlong_record));
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::url_without_extinf));
}

TEST(IptvCatalogTest, ReportsMalformedAndIncompleteEntries)
{
    constexpr std::string_view playlist = "#EXTM3U\n"
                                          "http://orphan.example/live\n"
                                          "#EXTINF:not-a-duration,Bad duration\n"
                                          "#EXTINF:-1 tvg-id=\"unterminated,Bad quote\n"
                                          "#EXTINF:-1,Missing URL\n";
    iptv::ParseReport report;

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, {}, &report);

    EXPECT_TRUE(catalog.empty());
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::url_without_extinf));
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::malformed_extinf));
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::missing_url));
}

TEST(IptvCatalogTest, RejectsUnsafeOrMalformedStreamUrls)
{
    constexpr std::string_view playlist = "#EXTINF:-1,Credentials\n"
                                          "https://user:password@example.com/live\n"
                                          "#EXTINF:-1,Whitespace\n"
                                          "https://example.com/live stream\n"
                                          "#EXTINF:-1,Bad port\n"
                                          "https://example.com:not-a-port/live\n";
    iptv::ParseReport report;

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, {}, &report);

    EXPECT_TRUE(catalog.empty());
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::unsafe_url));
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::malformed_url));
}

TEST(IptvCatalogTest, CapsStoredDiagnosticsWithoutHidingSkippedCount)
{
    constexpr std::string_view playlist = "http://one.example/live\n"
                                          "http://two.example/live\n"
                                          "http://three.example/live\n";
    iptv::ParseLimits limits;
    limits.max_diagnostics = 1;
    iptv::ParseReport report;

    const iptv::Catalog catalog = iptv::ParseExtendedM3u(playlist, kSourceId, limits, &report);

    EXPECT_TRUE(catalog.empty());
    EXPECT_EQ(report.skipped, 3u);
    ASSERT_EQ(report.diagnostics.size(), 1u);
    EXPECT_EQ(report.diagnostics[0].line, 1u);
}

// ---- the catalog itself ----------------------------------------------------------

iptv::Channel Station(int number, const char *group)
{
    iptv::Channel channel;
    channel.id = "station-" + std::to_string(number);
    channel.name = "Station " + std::to_string(number);
    channel.tvg_name = channel.name;
    channel.url = "https://streams.example/" + std::to_string(number) + ".m3u8";
    channel.group_title = group;
    channel.tvg_country = "US";
    channel.source_line = static_cast<std::uint32_t>(number);
    return channel;
}

TEST(IptvCatalogTest, HoldsChannelsAsViewsOfItsOwnText)
{
    iptv::Catalog catalog;
    catalog.source_id = kSourceId;
    EXPECT_TRUE(catalog.empty());
    EXPECT_EQ(catalog.Find("station-1"), iptv::Catalog::npos);

    iptv::Channel first = Station(1, "News");
    first.alternate_urls = {"https://backup.example/1", "https://backup.example/1b"};
    first.alternate_group_titles = {"Local"};
    first.playback_status = iptv::PlaybackStatus::failed;
    first.playback_result = -6;
    first.playback_checked_unix = 1700000000u;
    ASSERT_TRUE(catalog.Add(first));
    ASSERT_TRUE(catalog.Add(Station(2, "News")));
    ASSERT_EQ(catalog.size(), 2u);

    const iptv::ChannelView one = catalog[0];
    EXPECT_EQ(one.id, "station-1");
    EXPECT_EQ(one.source_id, kSourceId);
    EXPECT_EQ(one.name, "Station 1");
    EXPECT_EQ(one.url, "https://streams.example/1.m3u8");
    EXPECT_EQ(one.source_line, 1u);
    EXPECT_TRUE(one.tvg_logo.empty());
    ASSERT_EQ(one.alternate_urls.size(), 2u);
    EXPECT_EQ(one.alternate_urls[1], "https://backup.example/1b");
    EXPECT_TRUE(one.alternate_urls == first.alternate_urls);
    EXPECT_TRUE(one.alternate_group_titles == first.alternate_group_titles);
    EXPECT_TRUE(catalog[1].alternate_urls.empty());
    EXPECT_EQ(one.playback_status, iptv::PlaybackStatus::failed);
    EXPECT_EQ(one.playback_result, -6);
    EXPECT_EQ(one.playback_checked_unix, 1700000000u);
    EXPECT_EQ(catalog[1].playback_status, iptv::PlaybackStatus::unknown);

    // Every text is also a C string, and an empty one is still a text.
    EXPECT_STREQ(one.name.data(), "Station 1");
    EXPECT_STREQ(one.tvg_logo.data(), "");
    // What many channels say is written once: a second channel of the same
    // category points at the first one's, and a name said twice is one text.
    EXPECT_EQ(catalog[0].group_title.data(), catalog[1].group_title.data());
    EXPECT_EQ(one.tvg_name.data(), one.name.data());

    EXPECT_EQ(catalog.Find("station-2"), 1u);
    EXPECT_EQ(catalog.Find("station-3"), iptv::Catalog::npos);
    EXPECT_EQ(catalog.back().id, "station-2");

    ASSERT_TRUE(catalog.Set(1, iptv::Catalog::Field::tvg_country, "CA"));
    EXPECT_FALSE(catalog.Set(1, iptv::Catalog::Field::id, "other"));
    ASSERT_TRUE(catalog.AddAlternateUrl(1, "https://backup.example/2"));
    catalog.SetPlayback(1, iptv::PlaybackStatus::playable, 0, 5u);
    EXPECT_EQ(catalog[1].tvg_country, "CA");
    EXPECT_EQ(catalog[0].tvg_country, "US");
    EXPECT_EQ(catalog[1].alternate_urls.size(), 1u);
    EXPECT_EQ(catalog[1].playback_status, iptv::PlaybackStatus::playable);

    // A copy is a catalog of its own; a channel copied out keeps its texts.
    const iptv::Catalog copy = catalog;
    const iptv::Channel kept = catalog[0].Copy();
    iptv::Catalog moved = std::move(catalog);
    EXPECT_TRUE(catalog.empty()); // NOLINT(bugprone-use-after-move)
    ASSERT_EQ(moved.size(), 2u);
    ASSERT_EQ(copy.size(), 2u);
    moved.Clear();
    EXPECT_TRUE(moved.empty());
    EXPECT_EQ(copy[0].alternate_urls[0], "https://backup.example/1");
    EXPECT_EQ(copy[1].tvg_country, "CA");
    EXPECT_EQ(copy.Find("station-2"), 1u);
    EXPECT_EQ(kept.name, "Station 1");
    EXPECT_EQ(kept.alternate_group_titles, first.alternate_group_titles);
    std::size_t walked = 0;
    for (const iptv::ChannelView channel : copy)
        walked += channel.name.size();
    EXPECT_EQ(walked, 18u);

    // A text longer than a catalog holds is refused, and nothing is added.
    iptv::Channel huge = Station(3, "News");
    huge.tvg_logo.assign(70000u, 'x');
    iptv::Catalog small;
    EXPECT_FALSE(small.Add(huge));
    EXPECT_TRUE(small.empty());
}

// A playlist as providers write them: about 300 bytes a channel.
std::string ProviderPlaylist(int channels)
{
    std::string playlist = "#EXTM3U\n";
    playlist.reserve(static_cast<std::size_t>(channels) * 310u);
    char entry[512];
    for (int i = 0; i < channels; ++i)
    {
        std::snprintf(
            entry, sizeof(entry),
            "#EXTINF:-1 tvg-id=\"chan%d.example\" tvg-name=\"UK: Example Channel %d FHD\" "
            "tvg-logo=\"http://logos.provider.example:8080/images/channels/logo_%d.png\" "
            "group-title=\"UK | Entertainment %d\",UK: Example Channel %d FHD\n"
            "http://line.provider.example:8080/someuser1234/somepass5678/%d\n",
            i, i, i, i % 700, i, 100000 + i);
        playlist += entry;
    }
    return playlist;
}

TEST(IptvCatalogTest, HoldsAQuarterOfAMillionChannelsAndNoMore)
{
    const int kListed = static_cast<int>(iptv::kDefaultMaxChannels) + 250;
    const std::string playlist = ProviderPlaylist(kListed);
    ASSERT_GT(playlist.size(), 64u * 1024u * 1024u);

    // Read the way a download delivers it: in pieces, as they arrive.
    iptv::Catalog catalog;
    iptv::ParseReport report;
    iptv::M3uParser parser(&catalog, kSourceId, {}, &report);
    constexpr std::size_t kPiece = 64u * 1024u;
    std::size_t fed = 0;
    while (fed < playlist.size() && !parser.full())
    {
        ASSERT_TRUE(parser.Feed(std::string_view(playlist).substr(fed, kPiece)));
        fed += kPiece;
    }
    parser.Finish();

    // The first quarter of a million are in; the download can stop there.
    EXPECT_TRUE(parser.full());
    EXPECT_TRUE(report.catalog_full);
    EXPECT_LT(fed, playlist.size());
    ASSERT_EQ(catalog.size(), iptv::kDefaultMaxChannels);
    EXPECT_EQ(report.accepted, iptv::kDefaultMaxChannels);
    EXPECT_TRUE(HasIssue(report, iptv::ParseIssueCode::catalog_full));
    EXPECT_EQ(catalog.front().name, "UK: Example Channel 0 FHD");
    EXPECT_EQ(catalog.back().tvg_id, "chan249999.example");
    EXPECT_EQ(catalog[123456].url,
              "http://line.provider.example:8080/someuser1234/somepass5678/223456");
    EXPECT_EQ(catalog[123456].group_title, "UK | Entertainment 256");
    const std::string id(catalog[123456].id);
    EXPECT_EQ(catalog.Find(id), 123456u);
    // About a quarter of what a string per field costs.
    EXPECT_LT(catalog.MemoryBytes() / catalog.size(), 320u);
    EXPECT_LT(catalog.MemoryBytes(), 80u * 1024u * 1024u);
}

TEST(IptvCatalogTest, ReadsAPlaylistInPiecesOfAnySize)
{
    std::string playlist = "\xef\xbb\xbf#EXTM3U\r\n"
                           "#EXTINF:-1 tvg-id=\"news.br\" tvg-country=\"BR\",Brazil News\r\n"
                           "#EXTVLCOPT:http-user-agent=Mozilla/5.0 (PlayStation 5)\r\n"
                           "HTTPS://CDN.Example/live.m3u8\r\n"
                           "\r\n"
                           "#EXTINF:-1 tvg-id=\"NEWS.BR\" group-title=\"News\",Brazil News again\n"
                           "https://backup.example/live.m3u8\n"
                           "#EXTINF:-1 group-title=\"Sports\",Sport One\n"
                           "# ";
    playlist.append(400u, 'x'); // a line longer than a record may be
    playlist += "\nhttps://sports.example/one.ts\n"
                "http://orphan.example/live\n"
                "#EXTINF:-1,Last without a line break\n"
                "https://last.example/live";
    iptv::ParseLimits limits;
    limits.max_record_bytes = 200;

    iptv::ParseReport whole_report;
    const iptv::Catalog whole = iptv::ParseExtendedM3u(playlist, kSourceId, limits, &whole_report);
    ASSERT_EQ(whole.size(), 2u);
    EXPECT_EQ(whole[0].name, "Brazil News");
    EXPECT_EQ(whole[0].group_title, "News");
    ASSERT_EQ(whole[0].alternate_urls.size(), 1u);
    EXPECT_EQ(whole[1].name, "Last without a line break");
    EXPECT_EQ(whole_report.duplicates, 1u);
    EXPECT_TRUE(HasIssue(whole_report, iptv::ParseIssueCode::overlong_record));

    for (const std::size_t piece : {1u, 2u, 3u, 5u, 17u, 199u, 201u, 4096u})
    {
        iptv::Catalog pieces;
        iptv::ParseReport report;
        iptv::M3uParser parser(&pieces, kSourceId, limits, &report);
        for (std::size_t at = 0; at < playlist.size(); at += piece)
            ASSERT_TRUE(parser.Feed(std::string_view(playlist).substr(at, piece))) << piece;
        parser.Finish();
        EXPECT_FALSE(parser.full());
        ASSERT_EQ(pieces.size(), whole.size()) << piece;
        for (std::size_t index = 0; index < whole.size(); ++index)
        {
            EXPECT_EQ(pieces[index].id, whole[index].id) << piece;
            EXPECT_EQ(pieces[index].name, whole[index].name) << piece;
            EXPECT_EQ(pieces[index].url, whole[index].url) << piece;
            EXPECT_EQ(pieces[index].http_user_agent, whole[index].http_user_agent) << piece;
            EXPECT_EQ(pieces[index].source_line, whole[index].source_line) << piece;
            EXPECT_TRUE(pieces[index].alternate_urls == whole[index].alternate_urls) << piece;
        }
        EXPECT_EQ(report.lines_seen, whole_report.lines_seen) << piece;
        EXPECT_EQ(report.accepted, whole_report.accepted) << piece;
        EXPECT_EQ(report.duplicates, whole_report.duplicates) << piece;
        EXPECT_EQ(report.skipped, whole_report.skipped) << piece;
        ASSERT_EQ(report.diagnostics.size(), whole_report.diagnostics.size()) << piece;
        for (std::size_t index = 0; index < report.diagnostics.size(); ++index)
        {
            EXPECT_EQ(report.diagnostics[index].line, whole_report.diagnostics[index].line);
            EXPECT_EQ(report.diagnostics[index].code, whole_report.diagnostics[index].code);
        }
    }
}

TEST(IptvCatalogTest, APlaylistOverItsLimitStopsBeingRead)
{
    const std::string playlist = ProviderPlaylist(100);
    iptv::ParseLimits limits;
    limits.max_playlist_bytes = playlist.size() / 2u;
    iptv::Catalog catalog;
    iptv::ParseReport report;
    iptv::M3uParser parser(&catalog, kSourceId, limits, &report);
    bool taken = true;
    for (std::size_t at = 0; taken && at < playlist.size(); at += 1000u)
        taken = parser.Feed(std::string_view(playlist).substr(at, 1000u));
    EXPECT_FALSE(taken);
    EXPECT_FALSE(parser.Feed("#EXTINF:-1,More\nhttp://more.example/live\n"));
    parser.Finish();
    EXPECT_TRUE(catalog.empty());
    EXPECT_TRUE(report.input_too_large);
    EXPECT_EQ(report.accepted, 0u);
}

} // namespace
