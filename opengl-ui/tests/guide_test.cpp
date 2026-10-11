// ProsperoTV - Programme parsing, storage and provider playback regressions.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/guide.hpp"
#include "tv/guide_sheet.hpp"
#include "tv/sources_screen.hpp"
#include "tv/model.hpp"
#include "tv/playback_osd.hpp"
#include "core/save_file.hpp"
#include "iptv_input.h"
#include <png.h>
#include "host_platform.hpp"
#include "iptv_store.h"
#include "iptv_xtream.h"
#include <gtest/gtest.h>
#include <zlib.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace
{
constexpr char kPlaylist[] =
    "#EXTM3U x-tvg-url=\"https://guide.invalid/guide.xml.gz\" catchup=\"default\" "
    "catchup-days=\"7\"\n"
    "#EXTINF:-1 tvg-id=\"station\" "
    "catchup-source=\"https://archive.invalid/stream?start={utc}&end={utcend}\" group-title=\"US / "
    "Sports / Football\",Sports One\n"
    "https://live.invalid/one.m3u8\n"
    "#EXTINF:-1 tvg-name=\"News Two\" group-title=\"US / News\",News "
    "Two\nhttps://live.invalid/two.ts\n";
constexpr char kXml[] =
    "<?xml version=\"1.0\"?><!DOCTYPE tv SYSTEM \"xmltv.dtd\"><tv>"
    "<channel id=\"news\"><display-name>News Two</display-name></channel>"
    "<programme channel=\"station\" start=\"20261008170000 +0100\" stop=\"20261008170000 "
    "+0000\"><title>Football &amp; friends</title><desc>Live from the stadium</desc></programme>"
    "<programme channel=\"station\" start=\"20261008170000 +0000\" stop=\"20261008180000 "
    "+0000\"><title><![CDATA[Post-match <analysis>]]></title></programme>"
    "<programme channel=\"station\" start=\"20261008150000 +0000\" stop=\"20261008160000 "
    "+0000\"><title>Previous match</title></programme>"
    "<programme channel=\"news\" start=\"20261008160000 "
    "+0000\"><title>Headlines</title></programme>"
    "<programme channel=\"news\" start=\"20261008170000 +0000\"><title>Weather</title></programme>"
    "<programme channel=\"unknown\" start=\"20261008160000 +0000\" stop=\"20261008170000 "
    "+0000\"><title>Not in our list</title></programme>"
    "</tv>";

class GuideTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host::reset();
        char pattern[] = "/tmp/prosperotv-guide-XXXXXX";
        dir = mkdtemp(pattern);
        catalog = iptv::ParseExtendedM3u(kPlaylist, 42);
        now = ptv::xmltv_time("20261008163000 +0000");
        host::set_unix_time(static_cast<std::uint64_t>(now));
    }
    void TearDown() override
    {
        host::reset();
        std::filesystem::remove_all(dir);
    }
    void parse(ptv::Guide &guide, std::string_view xml = kXml)
    {
        ptv::XmltvReader reader(catalog, now, guide);
        for (const char &c : xml)
            ASSERT_TRUE(reader.feed({&c, 1}));
        ASSERT_TRUE(reader.finish());
    }
    static void settle(ptv::Model &model)
    {
        for (int i = 0; i < 600; ++i)
        {
            model.poll();
            if (!model.refreshing() && !model.guide_refreshing())
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        FAIL() << "Guide worker did not finish";
    }
    iptv::Catalog catalog;
    std::string dir;
    std::int64_t now = 0;
};

TEST_F(GuideTest, UnifiedSearchFindsFutureAndPastTitlesAndHonoursHiddenChannels)
{
    const auto playlist = dir + "/list.m3u", xml = dir + "/guide.xml";
    std::ofstream(playlist) << kPlaylist;
    std::ofstream(xml) << kXml;
    host::set_network(true, playlist);
    host::set_network_response("https://guide.invalid/guide.xml.gz", xml);
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    auto rows = model.search_results("match");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].kind, ptv::Model::SearchResult::Kind::programme);
    EXPECT_NE(rows[0].programme_start, rows[1].programme_start);
    EXPECT_FALSE(model.search_results("Sports One").empty());
    model.report_watch(rows[0].id);
    const auto home = model.home_items();
    ASSERT_EQ(home[2].size(), 1u);
    EXPECT_EQ(home[2][0].detail, "Football & friends");
    const auto revision = model.revision();
    model.poll();
    EXPECT_EQ(model.revision(), revision);
    host::set_unix_time(static_cast<std::uint64_t>(now + 45 * 60));
    model.poll();
    EXPECT_GT(model.revision(), revision);
    EXPECT_EQ(model.home_items()[2][0].detail, "Post-match <analysis>");
    ASSERT_TRUE(model.hide_category("US / Sports", true));
    EXPECT_TRUE(model.search_results("match").empty());
    EXPECT_TRUE(model.search_results("Sports One").empty());
    EXPECT_TRUE(model.home_items()[2].empty());
    EXPECT_EQ(model.search_results("Weather").size(), 1u);
    model.close();
}

TEST_F(GuideTest, PlaybackBannerUsesMappedGuideIdsAndUpdatesOnProgrammeChange)
{
    const auto playlist = dir + "/list.m3u", xml = dir + "/guide.xml";
    std::ofstream(playlist) << kPlaylist;
    std::ofstream(xml) << kXml;
    host::set_network(true, playlist);
    host::set_network_response("https://guide.invalid/guide.xml.gz", xml);
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    model.set_query("Sports One");
    ASSERT_EQ(model.visible_count(), 1u);
    ASSERT_TRUE(model.play(model.visible(0)));
    ptv::PlayRequest request;
    ASSERT_TRUE(model.take_play_request(&request));
    model.clear_filters();
    model.close();
    ptv::PlaybackOsd osd(model, request);
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    ASSERT_TRUE(osd.load_fonts(std::getenv("KIT_FONTS")));
    hui::gfx::Font font;
    std::string font_data;
    ASSERT_TRUE(hui::save::read_file(
        std::string(std::getenv("KIT_FONTS")) + "/inter-regular.huifont", &font_data));
    ASSERT_TRUE(font.load(font_data));
    std::vector<std::uint8_t> video(1920 * 1088 * 3 / 2, 100);
    const auto capture = [&](const char *suffix)
    {
        if (const char *prefix = std::getenv("PTV_OSD_CAPTURE"))
        {
            png_image image{};
            image.version = PNG_IMAGE_VERSION;
            image.width = 1920;
            image.height = 1080;
            image.format = PNG_FORMAT_GRAY;
            EXPECT_TRUE(png_image_write_to_file(&image, (std::string(prefix) + suffix).c_str(), 0,
                                                video.data(), 1920, nullptr));
            png_image_free(&image);
        }
    };
    const auto expect_guide = [&](const std::string &current, const std::string &next)
    {
        ptv::VideoPanel expected;
        expected.reset(80, 770, 1760, 230);
        expected.text(font, current, 30, 101, 28, 1700);
        expected.text(font, next, 30, 143, 28, 1700);
        std::vector<std::uint8_t> actual, wanted;
        for (unsigned y = 70; y < 160; ++y)
        {
            const auto begin = video.begin() + (770 + y) * 1920 + 80;
            actual.insert(actual.end(), begin, begin + 1760);
            wanted.insert(wanted.end(), expected.pixels.begin() + y * 1760,
                          expected.pixels.begin() + (y + 1) * 1760);
        }
        EXPECT_EQ(actual, wanted);
    };
    ASSERT_TRUE(osd.draw(video.data(), video.size(), 1920, 1088, 1920, 1080, 8, 1));
    expect_guide("Now " + ptv::programme_time(now - 1800) + " Football & friends",
                 "Next " + ptv::programme_time(now + 1800) + " Post-match <analysis>");
    capture("-banner.png");
    host::set_unix_time(now + 1800);
    osd.input(-1, 2);
    ASSERT_TRUE(osd.draw(video.data(), video.size(), 1920, 1088, 1920, 1080, 8, 2));
    expect_guide("Now " + ptv::programme_time(now + 1800) + " Post-match <analysis>", "");
    osd.input(IPTV_INPUT_CROSS, 3);
    std::fill(video.begin(), video.end(), 100);
    ASSERT_TRUE(osd.draw(video.data(), video.size(), 1920, 1088, 1920, 1080, 8, 3));
    capture("-list.png");
    iptv_player_audio_state_t audio{};
    audio.count = 3;
    audio.selected_pid = 0x111;
    audio.tracks[0] = {0x111, 0x0f, "eng", 0};
    audio.tracks[1] = {0x112, 0x81, "spa", 0};
    audio.tracks[2] = {0x113, 0x87, "fra", 3};
    osd.set_audio_state(audio);
    EXPECT_EQ(osd.input(IPTV_INPUT_OPTIONS, 4), 1);
    std::fill(video.begin(), video.end(), 100);
    ASSERT_TRUE(osd.draw(video.data(), video.size(), 1920, 1088, 1920, 1080, 8, 4));
    capture("-audio.png");
    iptv::SubtitleState subtitles;
    subtitles.tracks = {{11, iptv::SubtitleCodec::subrip, "eng", "English", false, false},
                        {12, iptv::SubtitleCodec::subrip, "spa", "Español", false, true}};
    subtitles.selected = 11;
    osd.set_subtitle_state(subtitles);
    EXPECT_EQ(osd.input(IPTV_INPUT_R1, 5), 1);
    std::fill(video.begin(), video.end(), 100);
    ASSERT_TRUE(osd.draw(video.data(), video.size(), 1920, 1088, 1920, 1080, 8, 5));
    capture("-subtitle-menu.png");
    EXPECT_EQ(osd.input(IPTV_INPUT_CIRCLE, 6), 1);
    auto cue = std::make_shared<iptv::SubtitleCue>();
    cue->text = "Hello, world!\n字幕测试";
    std::fill(video.begin(), video.end(), 100);
    ASSERT_TRUE(osd.draw(video.data(), video.size(), 1920, 1088, 1920, 1080, 8, 10000000, {cue}));
    capture("-subtitles.png");
}

TEST_F(GuideTest, TimezonesGapsAndInvalidDates)
{
    EXPECT_EQ(ptv::xmltv_time("20261008173000 +0100"), now);
    EXPECT_EQ(ptv::xmltv_time("20261008123000 -0400"), now);
    EXPECT_EQ(ptv::xmltv_time("202610081630"), now);
    EXPECT_EQ(ptv::xmltv_time("20261008173000 BST"), now);
    EXPECT_EQ(ptv::xmltv_time("20260230123000 +0000"), 0);
    EXPECT_EQ(ptv::xmltv_time("20261008253000 +0000"), 0);
    EXPECT_EQ(ptv::xmltv_time("20261008163000 +0060"), 0);
    EXPECT_EQ(ptv::xmltv_time("invalid"), 0);
    ptv::Guide guide;
    parse(guide);
    ASSERT_EQ(guide.count(), 5u);
    ASSERT_NE(guide.now(catalog[0].id, now), nullptr);
    EXPECT_EQ(guide.now(catalog[0].id, now)->title, "Football & friends");
    EXPECT_EQ(guide.next(catalog[0].id, now)->title, "Post-match <analysis>");
    EXPECT_EQ(guide.now(catalog[0].id, now + 1800)->title, "Post-match <analysis>");
    EXPECT_EQ(guide.now(catalog[0].id, now + 5400), nullptr);
    EXPECT_EQ(guide.now(catalog[1].id, now)->title, "Headlines");
    EXPECT_EQ(guide.now(catalog[1].id, now + 3600), nullptr); // Unknown last stop is not guessed.
    EXPECT_TRUE(guide.matches_now(catalog[0].id, "football", now));
    EXPECT_FALSE(guide.matches_now(catalog[0].id, "previous", now));
}

TEST_F(GuideTest, GzipAndCacheRoundTripKeepTheLastGoodCopy)
{
    ptv::Guide guide;
    const std::string file = dir + "/guide.xml.gz";
    gzFile zip = gzopen(file.c_str(), "wb");
    ASSERT_NE(zip, nullptr);
    ASSERT_EQ(gzwrite(zip, kXml, sizeof(kXml) - 1), static_cast<int>(sizeof(kXml) - 1));
    ASSERT_EQ(gzclose(zip), Z_OK);
    std::ifstream input(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    ptv::XmltvReader reader(catalog, now, guide);
    for (std::size_t i = 0; i < bytes.size(); i += 3)
        ASSERT_TRUE(reader.feed(std::string_view(bytes).substr(i, 3)));
    ASSERT_TRUE(reader.finish());
    guide.saved_unix = static_cast<std::uint64_t>(now);
    ASSERT_TRUE(guide.save(dir + "/cache.db"));
    ptv::Guide loaded;
    ASSERT_TRUE(loaded.load(dir + "/cache.db", 42));
    EXPECT_EQ(loaded.count(), 5u);
    EXPECT_EQ(loaded.saved_unix, static_cast<std::uint64_t>(now));
    EXPECT_FALSE(loaded.load(dir + "/cache.db", 43));
    EXPECT_EQ(loaded.count(), 5u);
    ptv::Guide partial;
    ptv::XmltvReader truncated(catalog, now, partial);
    ASSERT_TRUE(truncated.feed(std::string_view(bytes).substr(0, bytes.size() - 4)));
    EXPECT_FALSE(truncated.finish());
    ptv::XmltvReader bad(catalog, now, partial);
    EXPECT_FALSE(bad.feed("<tv><programme></tv>"));
    ptv::XmltvReader wrong(catalog, now, partial);
    EXPECT_FALSE(wrong.feed("<html/>"));
    EXPECT_FALSE(wrong.finish());
}

TEST_F(GuideTest, PlaylistAndXtreamMetadataSurviveCatalogStorage)
{
    ASSERT_EQ(catalog.guide_urls.size(), 1u);
    ASSERT_EQ(catalog[0].catchup, "default");
    EXPECT_EQ(catalog[0].catchup_days, "7");
    ASSERT_EQ(iptv::SaveCatalog(dir + "/channels.db", catalog), iptv::StoreStatus::ok);
    iptv::Catalog loaded;
    ASSERT_EQ(iptv::LoadCatalog(dir + "/channels.db", &loaded), iptv::StoreStatus::ok);
    EXPECT_EQ(loaded.guide_urls, catalog.guide_urls);
    EXPECT_EQ(loaded[0].catchup_source, catalog[0].catchup_source);
    const iptv::XtreamCredentials account{"https://service.invalid/sub", "user@home", "p&ss"};
    std::string url;
    ASSERT_TRUE(iptv::BuildXtreamGuideUrl(account, &url));
    EXPECT_EQ(url, "https://service.invalid/sub/xmltv.php?username=user%40home&password=p%26ss");
    ASSERT_EQ(
        iptv::ParseXtreamLiveStreams(
            "[{\"stream_id\":123,\"name\":\"Sports\",\"tv_archive\":1,\"tv_archive_duration\":7}]",
            account, {}, 43, &loaded),
        iptv::XtreamStatus::ok);
    EXPECT_EQ(loaded[0].catchup, "xc");
    EXPECT_EQ(loaded[0].catchup_days, "7");
    const ptv::Programme old{now - 5400, now - 1800, "Previous", {}, {}};
    EXPECT_EQ(
        ptv::catchup_url(loaded[0], old, now),
        "https://service.invalid/sub/timeshift/user%40home/p%26ss/60/2026-10-08:15-00/123.ts");
}

TEST_F(GuideTest, CatchupIsLimitedToTheAdvertisedArchive)
{
    const ptv::Programme old{now - 5400, now - 1800, "Previous", {}, {}};
    EXPECT_EQ(ptv::catchup_url(catalog[0], old, now),
              "https://archive.invalid/stream?start=" + std::to_string(old.start) +
                  "&end=" + std::to_string(old.end));
    EXPECT_TRUE(ptv::catchup_url(catalog[0], old, now + 8 * 86400).empty());
    EXPECT_TRUE(
        ptv::catchup_url(catalog[0], {now - 1800, now + 1800, "Live", {}, {}}, now).empty());
    auto channel = catalog[0].Copy();
    channel.catchup = "append";
    channel.catchup_source = "?start={Y}{m}{d}{H}{M}{S}&duration={duration}";
    EXPECT_EQ(ptv::catchup_url(channel, old, now),
              "https://live.invalid/one.m3u8?start=20261008150000&duration=3600");
    channel.catchup_source = "?start={unsupported}";
    EXPECT_TRUE(ptv::catchup_url(channel, old, now).empty());
}

TEST_F(GuideTest, DownloadSearchPlaybackAndFailedRefreshWorkTogether)
{
    const auto playlist = dir + "/list.m3u", xml = dir + "/guide.xml";
    std::ofstream(playlist) << kPlaylist;
    std::ofstream(xml) << kXml;
    host::set_network(true, playlist);
    host::set_network_response("https://guide.invalid/guide.xml.gz", xml);
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    ASSERT_EQ(model.guide().count(), 5u);
    model.set_query("football");
    ASSERT_EQ(model.visible_count(), 1u);
    const auto index = model.visible(0);
    const auto entries = model.guide().programmes(model.channel(index).id);
    ASSERT_EQ(entries.size(), 3u);
    ASSERT_TRUE(model.play_programme(index, entries[0]));
    ptv::PlayRequest request;
    ASSERT_TRUE(model.take_play_request(&request));
    EXPECT_FALSE(request.reconnect_live);
    EXPECT_FALSE(request.live);
    EXPECT_FALSE(request.record_channel_result);
    EXPECT_NE(request.urls[0].find("archive.invalid"), request.urls[0].npos);
    EXPECT_FALSE(model.play_programme(index, entries[2]));
    ASSERT_TRUE(model.play_programme(index, entries[1]));
    ASSERT_TRUE(model.take_play_request(&request));
    EXPECT_TRUE(request.record_channel_result);
    EXPECT_TRUE(request.live);
    EXPECT_EQ(request.urls[0], model.channel(index).url);
    host::set_network(false, "");
    model.refresh_guide();
    settle(model);
    EXPECT_EQ(model.guide().count(), 5u);
    EXPECT_NE(model.guide_status().find("failed"), model.guide_status().npos);
    model.close();
    ptv::Model offline(dir);
    ASSERT_TRUE(offline.open());
    settle(offline);
    EXPECT_EQ(offline.guide().count(), 5u);
    offline.close();
}

TEST_F(GuideTest, CustomGuideOverridesKeepSourceHeadersOnTheirOrigin)
{
    const auto playlist = dir + "/list.m3u", xml = dir + "/guide.xml";
    std::ofstream(playlist) << kPlaylist;
    std::ofstream(xml) << kXml;
    host::set_network(true, playlist);
    host::set_network_response("https://guide.invalid/guide.xml.gz", xml);
    host::set_network_response("https://source.invalid/guide.xml", xml);
    host::set_network_response("https://foreign.invalid/guide.xml", xml);
    {
        ptv::Library library;
        ASSERT_TRUE(library.open(dir + "/prosperotv-library.sqlite3"));
        ptv::SavedSource source{
            2,  1,  "Protected source",          "https://source.invalid/list.m3u", {},
            {}, {}, ptv::RefreshSchedule::manual};
        source.extra_headers = "X-Provider-Key: fixture-secret";
        ASSERT_TRUE(library.save_source(&source));
        ASSERT_TRUE(library.select_source(source.id));
    }
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    ASSERT_EQ(model.guide().count(), 5u);
    const auto initial = host::requests();
    ASSERT_FALSE(initial.empty());
    EXPECT_EQ(initial.back().url, "https://guide.invalid/guide.xml.gz");
    EXPECT_TRUE(initial.back().extra_headers.empty());

    ASSERT_TRUE(model.set_guide_url(2, "https://source.invalid/guide.xml"));
    settle(model);
    EXPECT_EQ(host::requests().back().extra_headers, "X-Provider-Key: fixture-secret");
    ASSERT_TRUE(model.set_guide_url(2, "https://foreign.invalid/guide.xml"));
    settle(model);
    const auto foreign = host::requests().back();
    EXPECT_EQ(foreign.url, "https://foreign.invalid/guide.xml");
    EXPECT_TRUE(foreign.extra_headers.empty());
    EXPECT_EQ(model.guide().count(), 5u);
    model.close();
}

TEST_F(GuideTest, ExplicitMatchesOverrideAutomaticNamesAndAllowSharedGuideIds)
{
    const std::string first(catalog[0].id), second(catalog[1].id);
    ptv::Guide guide;
    const std::map<std::string, std::string> mapping{
        {first, "news"}, {second, "news"}, {"missing-channel", "station"}};
    ptv::XmltvReader reader(catalog, now, guide, mapping);
    ASSERT_TRUE(reader.feed(kXml));
    ASSERT_TRUE(reader.finish());
    ASSERT_EQ(guide.programmes(first).size(), 2u);
    ASSERT_EQ(guide.programmes(second).size(), 2u);
    EXPECT_EQ(guide.programmes(first)[0].title, "Headlines");
    EXPECT_TRUE(guide.programmes("missing-channel").empty());
    ptv::Guide shared_auto;
    ptv::XmltvReader shared(catalog, now, shared_auto, {{first, "news"}});
    ASSERT_TRUE(shared.feed(kXml));
    ASSERT_TRUE(shared.finish());
    EXPECT_EQ(shared_auto.programmes(first).size(), 2u);
    EXPECT_EQ(shared_auto.programmes(second).size(), 2u);
    ptv::Guide unknown;
    ptv::XmltvReader no_match(catalog, now, unknown, {{second, "does-not-exist"}});
    ASSERT_TRUE(no_match.feed(kXml));
    ASSERT_TRUE(no_match.finish());
    EXPECT_TRUE(unknown.programmes(second).empty());
    EXPECT_EQ(unknown.programmes(first).size(), 3u);
}
TEST_F(GuideTest, GuideConfigurationPersistsIsolatesSourcesAndClearsOnIdentityChange)
{
    ptv::Library library;
    const auto database = dir + "/library.sqlite3";
    ASSERT_TRUE(library.open(database));
    ptv::SavedSource a{0,  1,  "A", "https://one.invalid/list",
                       {}, {}, {},  ptv::RefreshSchedule::manual};
    ptv::SavedSource b{0,  1,  "B", "https://two.invalid/list",
                       {}, {}, {},  ptv::RefreshSchedule::manual};
    ASSERT_TRUE(library.save_source(&a));
    ASSERT_TRUE(library.save_source(&b));
    EXPECT_FALSE(library.set_guide_url(a.id, "file:///etc/passwd"));
    ASSERT_TRUE(library.set_guide_url(a.id, "https://guide.invalid/custom.xml"));
    ASSERT_TRUE(library.set_guide_mapping(a.id, "channel", "news"));
    ASSERT_TRUE(library.set_guide_mapping(a.id, "channel", "station"));
    EXPECT_TRUE(library.guide_url(b.id).empty());
    EXPECT_TRUE(library.guide_mapping(b.id).empty());
    EXPECT_FALSE(library.set_guide_mapping(a.id, "channel", "bad\nvalue"));
    library.close();
    ASSERT_TRUE(library.open(database));
    EXPECT_EQ(library.guide_url(a.id), "https://guide.invalid/custom.xml");
    EXPECT_EQ(library.guide_mapping(a.id).at("channel"), "station");
    a.name = "Renamed";
    ASSERT_TRUE(library.save_source(&a));
    EXPECT_FALSE(library.guide_mapping(a.id).empty());
    a.url = "https://replacement.invalid/list";
    ASSERT_TRUE(library.save_source(&a));
    EXPECT_TRUE(library.guide_url(a.id).empty());
    EXPECT_TRUE(library.guide_mapping(a.id).empty());
}
TEST_F(GuideTest, OverrideAndMatchingInvalidateOldGuideAndSurviveOfflineRestart)
{
    const auto playlist = dir + "/list.m3u", xml = dir + "/guide.xml";
    std::ofstream(playlist) << kPlaylist;
    std::ofstream(xml) << kXml;
    host::set_network(true, playlist);
    host::set_network_response("https://guide.invalid/guide.xml.gz", xml);
    host::set_network_response("https://guide.invalid/custom.xml", xml);
    std::string first;
    {
        ptv::Model model(dir);
        ASSERT_TRUE(model.open());
        settle(model);
        first = model.channel(0).id;
        ASSERT_EQ(model.guide().programmes(first).size(), 3u);
        const hui::ui::Fonts fonts{};
        ptv::Shared shared(model, fonts, {});
        ptv::SourcesScreen sources(shared);
        hui::ui::Feedback feedback;
        hui::InputFrame input;
        input.pressed = hui::action_bit(hui::Action::jump_prev);
        sources.handle(input, feedback);
        EXPECT_NE(host::keyboard_title().find("XMLTV URL"), std::string::npos);
        host::set_keyboard_text("https://guide.invalid/custom.xml");
        settle(model);
        ptv::GuideSheet sheet(shared);
        sheet.open(first, feedback);
        input.pressed = hui::action_bit(hui::Action::menu);
        sheet.handle(input, feedback);
        input = {};
        input.nav = hui::Direction::down;
        for (int i = 0; i < 4; ++i)
            sheet.handle(input, feedback);
        input = {};
        input.pressed = hui::action_bit(hui::Action::confirm);
        sheet.handle(input, feedback);
        EXPECT_NE(host::keyboard_title().find("XMLTV channel ID"), std::string::npos);
        host::set_keyboard_text("news");
        settle(model);
        EXPECT_EQ(model.guide().programmes(first).size(), 2u);
        EXPECT_FALSE(model.set_guide_mapping("unknown-channel", "news"));
        ASSERT_TRUE(model.hide_category("US / Sports / Football", true));
        EXPECT_FALSE(model.set_guide_mapping(first, "station"));
        EXPECT_LT(model.position_of(first), 0);
        ASSERT_TRUE(model.hide_category("US / Sports / Football", false));
        model.close();
    }
    host::set_network(false, "");
    ptv::Model offline(dir);
    ASSERT_TRUE(offline.open());
    settle(offline);
    EXPECT_EQ(offline.guide_match(first), "news");
    EXPECT_EQ(offline.guide().programmes(first).size(), 2u);
    ASSERT_TRUE(offline.set_guide_mapping(first, "does-not-exist"));
    settle(offline);
    EXPECT_TRUE(offline.guide().programmes(first).empty());
    // Clearing the override restores the provider URL; automatic matching is separately restored.
    ASSERT_TRUE(offline.set_guide_url(offline.selected_source_id(), ""));
    settle(offline);
    ASSERT_TRUE(offline.set_guide_mapping(first, ""));
    settle(offline);
    host::set_network(true, playlist);
    offline.refresh_guide();
    settle(offline);
    EXPECT_EQ(offline.guide().programmes(first).size(), 3u);
    offline.close();
}
} // namespace
