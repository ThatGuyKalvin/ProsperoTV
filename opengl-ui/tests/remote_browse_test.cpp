// ProsperoTV - Paired-phone browsing revalidates catalogue and content policy.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "host_platform.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
namespace
{
class RemoteBrowseTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        host::reset();
        char pattern[] = "/tmp/prosperotv-phone-XXXXXX";
        dir = mkdtemp(pattern);
        std::ofstream list(dir + "/list.m3u");
        list << "#EXTM3U x-tvg-url=\"https://guide.invalid/list.xml\"\n";
        for (int i = 0; i < 90; ++i)
            list << "#EXTINF:-1 tvg-id=\"id" << i << "\" group-title=\"" << (i == 0 ? "Private" : "General")
                 << "\",Channel " << (i < 10 ? "0" : "") << i << "\nhttps://stream.invalid/" << i << ".ts\n";
        list << "#EXTINF:90,Movie\nhttps://stream.invalid/movie.mp4\n";
        list.close();
        std::ofstream(dir + "/guide.xml") << R"(<tv><programme channel="id80" start="20261008160000 +0000" stop="20261008170000 +0000"><title>Football match</title></programme><programme channel="id80" start="20261008170000 +0000" stop="20261008180000 +0000"><title>News</title></programme></tv>)";
        host::set_unix_time(ptv::xmltv_time("20261008163000 +0000"));
        host::set_network(true, dir + "/list.m3u");
        host::set_network_response("https://guide.invalid/list.xml", dir + "/guide.xml");
    }
    void TearDown() override { host::reset(); std::filesystem::remove_all(dir); }
    void settle(ptv::Model &model)
    {
        for (int i = 0; i < 600; ++i)
        {
            model.poll();
            if (!model.refreshing() && !model.guide_refreshing()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        FAIL() << "worker did not finish";
    }
    std::string source(const std::string &json)
    {
        const auto at = json.find("\"source\":\"") + 10;
        return json.substr(at, json.find('"', at) - at);
    }
    std::string request(const char *op, const std::string &token, std::string_view id)
    {
        return "{\"operation\":\"" + std::string(op) + "\",\"source\":\"" + token + "\",\"channel\":\"" + std::string(id) + "\"}";
    }
    std::string dir;
};
TEST_F(RemoteBrowseTest, PagesSearchGuideAndPlaybackNeverExposeStreamAddresses)
{
    ptv::Model model(dir);
    ASSERT_TRUE(model.open()); settle(model);
    std::string output;
    ASSERT_EQ(model.remote_browse(R"({"operation":"list"})", output), 200);
    const auto token = source(output);
    EXPECT_NE(output.find("Channel 00"), std::string::npos);
    EXPECT_EQ(output.find("Channel 40"), std::string::npos);
    EXPECT_EQ(output.find("https://"), std::string::npos);
    ASSERT_EQ(model.remote_browse("{\"operation\":\"list\",\"source\":\"" + token + "\",\"offset\":40}", output), 200);
    EXPECT_NE(output.find("Channel 40"), std::string::npos);
    EXPECT_EQ(output.find("Channel 00"), std::string::npos);
    ASSERT_EQ(model.remote_browse(R"({"operation":"list","query":"Football"})", output), 200);
    EXPECT_NE(output.find("Channel 80"), std::string::npos);
    EXPECT_EQ(output.find("Channel 79"), std::string::npos);
    ASSERT_EQ(model.remote_browse(request("guide", token, model.channel(80).id), output), 200);
    EXPECT_NE(output.find("Football match"), std::string::npos);
    EXPECT_NE(output.find("News"), std::string::npos);
    ASSERT_EQ(model.remote_browse(request("play", token, model.channel(80).id), output), 200);
    ptv::PlayRequest play;
    ASSERT_TRUE(model.take_play_request(&play));
    EXPECT_EQ(play.channel_id, model.channel(80).id);
    EXPECT_EQ(model.remote_browse(request("play", "stale", model.channel(80).id), output), 409);
    EXPECT_EQ(model.remote_browse(request("guide", token, model.channel(90).id), output), 404);
    EXPECT_EQ(model.remote_browse(R"({"operation":"list","offset":99999999})", output), 400);
    EXPECT_EQ(model.remote_browse(R"({"operation":"list","operation":"play"})", output), 400);
    model.close();
}
TEST_F(RemoteBrowseTest, RechecksHiddenAndParentalRulesBeforeReturningGuideOrTuning)
{
    ptv::Model model(dir);
    ASSERT_TRUE(model.open()); settle(model);
    std::string output;
    ASSERT_EQ(model.remote_browse(R"({"operation":"list"})", output), 200);
    const auto token = source(output);
    const auto tune = request("play", token, model.channel(0).id);
    ASSERT_TRUE(model.hide_category("Private", true));
    EXPECT_EQ(model.remote_browse(tune, output), 404);
    ASSERT_TRUE(model.hide_category("Private", false));
    ASSERT_TRUE(model.hide_channel(model.channel(0).id, true));
    EXPECT_EQ(model.remote_browse(tune, output), 404);
    EXPECT_EQ(model.remote_browse(request("guide", token, model.channel(0).id), output), 404);
    ASSERT_TRUE(model.hide_channel(model.channel(0).id, false));
    model.parental_action(ptv::Model::ParentalAction::set_pin);
    model.poll(); host::set_keyboard_text("836295"); model.poll();
    model.poll(); host::set_keyboard_text("836295"); model.poll();
    ASSERT_TRUE(model.parental().enabled());
    EXPECT_FALSE(model.parental().unlocked());
    model.parental_action(ptv::Model::ParentalAction::unlock);
    model.poll(); host::set_keyboard_text("836295"); model.poll();
    ASSERT_TRUE(model.set_category_rule("Private", 1));
    model.parental_action(ptv::Model::ParentalAction::lock);
    EXPECT_EQ(model.remote_browse(tune, output), 404);
    ASSERT_EQ(model.remote_browse(R"({"operation":"list"})", output), 200);
    EXPECT_EQ(output.find("Channel 00"), std::string::npos);
    ptv::PlayRequest play;
    EXPECT_FALSE(model.take_play_request(&play));
    model.close();
}
}
