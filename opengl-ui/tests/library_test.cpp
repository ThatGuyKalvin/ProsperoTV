// ProsperoTV - Tests of the films and series shelves against a stand-in Xtream account.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "host_platform.hpp"
#include "iptv_source_state.h"
#include "iptv_store.h"
#include "iptv_xtream.h"
#include "tv/model.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace
{

namespace fs = std::filesystem;

// What the stand-in account answers, by the action asked for. The sign-in
// (no action) is the last route: every address of the account contains it.
struct Answer
{
    const char *fragment;
    const char *body;
};
constexpr Answer kAnswers[] = {
    {"action=get_live_categories", R"([{"category_id":"1","category_name":"News"}])"},
    {"action=get_live_streams",
     R"([{"stream_id":7,"name":"Alder News","category_id":"1","container_extension":"ts",)"
     R"("epg_channel_id":"Alder.uk"},)"
     R"({"stream_id":8,"name":"Birch Sport","category_id":"1"}])"},
    {"action=get_vod_categories", R"([{"category_id":"3","category_name":"Action"}])"},
    {"action=get_vod_streams",
     R"([{"stream_id":501,"name":"Comet Run","category_id":"3","container_extension":"mkv",)"
     R"("rating":"7.5","year":"2021"},)"
     R"({"stream_id":502,"name":"Delta Night","category_id":"3","container_extension":"mp4"}])"},
    {"action=get_vod_info",
     R"({"info":{"plot":"A courier outruns the dawn.","genre":"Action","duration_secs":6300,)"
     R"("rating":"7.8","video":{"codec_name":"hevc","width":3840,"height":2160}}})"},
    {"action=get_series_categories", R"([{"category_id":"4","category_name":"Drama"}])"},
    {"action=get_series_info",
     R"({"info":{"plot":"Two families, one harbour.","genre":"Drama"},"episodes":{)"
     R"("1":[{"id":"900","episode_num":1,"title":"Pilot","container_extension":"mkv",)"
     R"("info":{"duration_secs":2580},"season":1},)"
     R"({"id":"901","episode_num":2,"title":"Tides","container_extension":"mkv","season":1}],)"
     R"("2":[{"id":"910","episode_num":1,"title":"Back","container_extension":"mkv",)"
     R"("season":2}]}})"},
    {"action=get_series",
     R"([{"series_id":12,"name":"Harbour","category_id":"4","releaseDate":"2019-02-03"}])"},
    {"player_api.php", R"({"user_info":{"auth":1,"status":"Active"},"server_info":{}})"},
};

// The account's TV guide, around a fixed moment: 2025-10-09 07:46:40 UTC.
constexpr std::uint64_t kNow = 1759996000u;
constexpr const char *kGuide =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n"
    "<channel id=\"alder.uk\"><display-name>Alder News</display-name></channel>\n"
    "<programme start=\"20251009070000 +0000\" stop=\"20251009080000 +0000\" "
    "channel=\"alder.uk\"><title>Morning Briefing</title><desc>The day ahead.</desc></programme>\n"
    "<programme start=\"20251009080000 +0000\" stop=\"20251009090000 +0000\" "
    "channel=\"alder.uk\"><title>Markets</title></programme>\n"
    "<programme start=\"20251009070000 +0000\" stop=\"20251009080000 +0000\" "
    "channel=\"other.uk\"><title>Not ours</title></programme>\n"
    "</tv>\n";

class LibraryTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host::reset();
        char pattern[] = "/tmp/prosperotv-library-XXXXXX";
        dir_ = mkdtemp(pattern);
        for (const Answer &answer : kAnswers)
        {
            const std::string file = dir_ + "/" + std::string(answer.fragment).substr(7) + ".json";
            std::ofstream(file) << answer.body;
            host::set_network_route(answer.fragment, file);
        }
        std::ofstream(dir_ + "/guide.xml") << kGuide;
        host::set_network_route("xmltv.php", dir_ + "/guide.xml");
        host::set_network(true, dir_ + "/missing");
        account_.server_url = "http://provider.example.invalid:8080";
        account_.username = "viewer";
        account_.password = "secret";
        ASSERT_EQ(iptv::SaveXtreamCredentials(dir_ + "/prosperotv-xtream-v1.txt", account_),
                  iptv::XtreamStatus::ok);
        ASSERT_EQ(
            iptv::SaveActiveSource(dir_ + "/iptv-active-source-v1.txt", iptv::SourceKind::Xtream),
            iptv::SourceStateStatus::ok);
    }

    void TearDown() override
    {
        std::error_code error;
        fs::remove_all(dir_, error);
        host::reset();
    }

    // Polls until the condition holds, or a few seconds have gone.
    template <typename Condition> bool wait(ptv::Model &model, Condition condition)
    {
        for (int i = 0; i < 600; ++i)
        {
            model.poll();
            if (condition())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    void open(ptv::Model &model)
    {
        ASSERT_TRUE(model.open());
        ASSERT_TRUE(wait(model, [&] { return !model.refreshing(); }));
        ASSERT_TRUE(model.has_catalog());
        ASSERT_EQ(model.channel_count(), 2u);
        model.take_notices();
    }

    std::string dir_;
    iptv::XtreamCredentials account_;
};

TEST_F(LibraryTest, EachShelfKeepsItsOwnList)
{
    ptv::Model model(dir_);
    open(model);
    EXPECT_TRUE(model.has_library());
    model.set_query("Alder");
    EXPECT_EQ(model.visible_count(), 1u);

    model.set_shelf(ptv::Shelf::movies);
    EXPECT_EQ(model.shelf_state(ptv::Shelf::movies), ptv::LibraryState::loading);
    ASSERT_TRUE(wait(
        model, [&] { return model.shelf_state(ptv::Shelf::movies) == ptv::LibraryState::ready; }));
    ASSERT_EQ(model.channel_count(), 2u);
    // The search belongs to the live shelf: the films are all there.
    EXPECT_TRUE(model.query().empty());
    EXPECT_EQ(model.visible_count(), 2u);
    EXPECT_EQ(model.channel(model.visible(0)).name, "Comet Run");
    EXPECT_EQ(model.channel(model.visible(0)).kind, iptv::MediaKind::movie);

    model.set_shelf(ptv::Shelf::live);
    EXPECT_EQ(model.query(), "Alder");
    EXPECT_EQ(model.visible_count(), 1u);
    EXPECT_EQ(model.channel(model.visible(0)).name, "Alder News");
    model.close();
}

TEST_F(LibraryTest, TheListIsSavedAndReadBackOnTheNextLaunch)
{
    {
        ptv::Model model(dir_);
        open(model);
        model.set_shelf(ptv::Shelf::series);
        ASSERT_TRUE(
            wait(model, [&]
                 { return model.shelf_state(ptv::Shelf::series) == ptv::LibraryState::ready; }));
        ASSERT_EQ(model.channel_count(), 1u);
        model.close();
    }
    EXPECT_TRUE(fs::exists(dir_ + "/prosperotv-xtream-series.sqlite3"));

    // Offline, the next launch still lists the series from the saved copy.
    host::set_network(false, dir_ + "/missing");
    ptv::Model again(dir_);
    ASSERT_TRUE(again.open());
    again.set_shelf(ptv::Shelf::series);
    ASSERT_TRUE(wait(again, [&] { return again.has_catalog(); }));
    EXPECT_EQ(again.channel(0).name, "Harbour");
    EXPECT_EQ(again.shelf_state(ptv::Shelf::series), ptv::LibraryState::ready);
    again.close();
}

TEST_F(LibraryTest, AFilmHasItsDetailsAndPlaysFromTheAccount)
{
    ptv::Model model(dir_);
    open(model);
    model.set_shelf(ptv::Shelf::movies);
    ASSERT_TRUE(wait(model, [&] { return model.has_catalog(); }));
    const unsigned comet = model.visible(0);
    const std::string id(model.channel(comet).id);

    // The details come once asked for.
    model.want_details(comet);
    ASSERT_TRUE(wait(model, [&] { return model.details(id) != nullptr; }));
    EXPECT_EQ(model.details(id)->plot, "A courier outruns the dawn.");
    EXPECT_EQ(model.details(id)->video_height, 2160u);

    ASSERT_TRUE(model.play(comet));
    ptv::PlayRequest request;
    ASSERT_TRUE(model.take_play_request(&request));
    EXPECT_FALSE(request.reconnect_live);
    ASSERT_FALSE(request.urls.empty());
    EXPECT_NE(request.urls[0].find("/movie/viewer/secret/501."), std::string::npos);
    model.close();
}

TEST_F(LibraryTest, ASeriesListsItsSeasonsAndGoesOnAfterTheLastEpisodeWatched)
{
    ptv::Model model(dir_);
    open(model);
    model.set_shelf(ptv::Shelf::series);
    ASSERT_TRUE(wait(model, [&] { return model.has_catalog(); }));
    // A series is not played: its episodes are.
    EXPECT_FALSE(model.play(0));

    ASSERT_TRUE(model.open_series(0));
    EXPECT_EQ(model.series_name(), "Harbour");
    ASSERT_TRUE(wait(model, [&] { return model.episodes_state() == ptv::LibraryState::ready; }));
    ASSERT_EQ(model.seasons(), (std::vector<std::uint16_t>{1, 2}));
    const std::vector<unsigned> first = model.season_episodes(1);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(model.episodes()[first[0]].episode, 1u);
    EXPECT_FALSE(model.series_started());
    EXPECT_EQ(model.continue_episode(), static_cast<int>(first[0]));
    // The series' answer brought its details too.
    EXPECT_NE(model.details(model.series_id()), nullptr);

    ASSERT_TRUE(model.play_episode(first[0]));
    ptv::PlayRequest request;
    ASSERT_TRUE(model.take_play_request(&request));
    EXPECT_EQ(request.channel_name, "Harbour  S01 E01");
    // Next time it goes on with the second.
    EXPECT_TRUE(model.series_started());
    EXPECT_EQ(model.continue_episode(), static_cast<int>(first[1]));
    model.close();
}

TEST_F(LibraryTest, TheGuideSaysWhatIsOnNowAndNext)
{
    host::set_unix_time(kNow);
    ptv::Model model(dir_);
    open(model);
    const iptv::ChannelView alder = model.channel(model.visible(0));
    ASSERT_EQ(alder.name, "Alder News");
    ASSERT_TRUE(
        wait(model, [&] { return !model.on_now(model.channel(model.visible(0))).title.empty(); }));
    const ptv::OnNow &on = model.on_now(model.channel(model.visible(0)));
    EXPECT_EQ(on.title, "Morning Briefing");
    EXPECT_EQ(on.description, "The day ahead.");
    EXPECT_EQ(on.next_title, "Markets");
    EXPECT_EQ(on.stop, 1759996800ll);
    // A channel the guide does not know has nothing on.
    EXPECT_TRUE(model.on_now(model.channel(model.visible(1))).title.empty());
    model.close();
    EXPECT_TRUE(fs::exists(dir_ + "/prosperotv-guide.sqlite3"));
}

TEST_F(LibraryTest, ASourceWithoutAnAccountHasNoShelvesToFill)
{
    ASSERT_EQ(
        iptv::SaveActiveSource(dir_ + "/iptv-active-source-v1.txt", iptv::SourceKind::BuiltIn),
        iptv::SourceStateStatus::ok);
    host::set_network(true, dir_ + "/missing");
    ptv::Model model(dir_);
    ASSERT_TRUE(model.open());
    EXPECT_FALSE(model.has_library());
    model.set_shelf(ptv::Shelf::movies);
    for (int i = 0; i < 20; ++i)
        model.poll();
    EXPECT_FALSE(model.has_catalog());
    EXPECT_EQ(model.shelf_state(ptv::Shelf::movies), ptv::LibraryState::none);
    EXPECT_EQ(model.visible_count(), 0u);
    model.close();
}

} // namespace
