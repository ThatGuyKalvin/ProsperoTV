// ProsperoTV - Mixed playlist routing and persisted on-demand playback.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "host_platform.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

TEST(M3uVod, ImportsMixedPlusPlaylistAndKeepsMoviesAndEpisodesOutOfLiveAfterRestart)
{
    host::reset();
    char pattern[] = "/tmp/prosperotv-mixed-XXXXXX";
    const char *temporary = mkdtemp(pattern);
    ASSERT_NE(temporary, nullptr);
    const std::string dir(temporary);
    struct Cleanup
    {
        std::string dir;
        ~Cleanup()
        {
            host::reset();
            std::filesystem::remove_all(dir);
        }
    } cleanup{dir};
    const std::string url =
        "https://provider.invalid/get.php?username=u&password=p&type=m3u_plus&output=ts";
    std::ofstream(dir + "/mixed.m3u")
        << "#EXTM3U\n"
           "#EXTINF:-1 group-title=\"Live\",News\nhttps://provider.invalid/live/u/p/1.ts\n"
           "#EXTINF:-1 group-title=\"Movies\",Film\n"
           "#EXTVLCOPT:http-user-agent=MovieAgent\n"
           "#EXTVLCOPT:http-referrer=https://provider.invalid/\n"
           "https://provider.invalid/movie/u/p/2.mkv\n"
           "#EXTINF:-1 group-title=\"Drama\",Show "
           "S01E01\nhttps://provider.invalid/series/u/p/3.mp4\n"
           "#EXTINF:-1 group-title=\"Drama\",Show "
           "S01E02\nhttps://provider.invalid/series/u/p/4.mp4\n";
    host::set_network(true, dir + "/mixed.m3u");
    host::set_network_piece(1);
    {
        ptv::Library library;
        ASSERT_TRUE(library.open(dir + "/prosperotv-library.sqlite3"));
        ptv::SavedSource source{2, 1, "Mixed", url, {}, {}, {}, ptv::RefreshSchedule::manual};
        ASSERT_TRUE(library.save_source(&source));
        ASSERT_TRUE(library.select_source(2));
    }
    for (int launch = 0; launch < 2; ++launch)
    {
        ptv::Model model(dir);
        ASSERT_TRUE(model.open());
        for (int i = 0; i < 500 && model.refreshing(); ++i)
        {
            model.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        ASSERT_FALSE(model.refreshing());
        ASSERT_EQ(model.visible_count(), 1u);
        ASSERT_EQ(model.provider_categories().size(), 1u);
        EXPECT_EQ(model.provider_categories()[0].value, "Live");
        auto &vod = model.vod();
        ASSERT_TRUE(vod.available());
        vod.select(ptv::VodKind::movies);
        ASSERT_TRUE(vod.loaded());
        ASSERT_EQ(vod.catalog().size(), 1u);
        EXPECT_EQ(vod.catalog()[0].name, "Film");
        EXPECT_FALSE(model.play_channel(vod.catalog()[0].id));
        EXPECT_FALSE(model.preview_request(vod.catalog()[0].id).has_value());
        ASSERT_TRUE(model.play_vod(0));
        ptv::PlayRequest request;
        ASSERT_TRUE(model.take_play_request(&request));
        EXPECT_FALSE(request.live);
        EXPECT_FALSE(request.record_channel_result);
        EXPECT_EQ(request.user_agent, "MovieAgent");
        EXPECT_EQ(request.referrer, "https://provider.invalid/");
        vod.select(ptv::VodKind::series);
        ASSERT_EQ(vod.catalog().size(), 1u);
        EXPECT_EQ(vod.catalog()[0].name, "Show");
        const std::string show(vod.catalog()[0].tvg_id);
        vod.select(ptv::VodKind::episodes, show);
        ASSERT_EQ(vod.catalog().size(), 2u);
        ASSERT_TRUE(model.play_vod(1));
        ASSERT_TRUE(model.take_play_request(&request));
        EXPECT_EQ(request.channel_name, "Show S01E02");
        EXPECT_EQ(request.urls[0], "https://provider.invalid/series/u/p/4.mp4");
        model.close();
        // The second launch must get both live and VOD from the persisted source.
        host::set_network(false, "");
    }
}
