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
            list << "#EXTINF:-1 tvg-id=\"id" << i << "\" group-title=\""
                 << (i == 0 ? "Private" : "General") << "\",Channel " << (i < 10 ? "0" : "") << i
                 << "\nhttps://stream.invalid/" << i << ".ts\n";
        list << "#EXTINF:90,Movie\nhttps://stream.invalid/movie.mp4\n";
        list.close();
        std::ofstream(dir + "/guide.xml")
            << R"(<tv><programme channel="id80" start="20261008160000 +0000" stop="20261008170000 +0000"><title>Football match</title></programme><programme channel="id80" start="20261008170000 +0000" stop="20261008180000 +0000"><title>News</title></programme></tv>)";
        host::set_unix_time(ptv::xmltv_time("20261008163000 +0000"));
        host::set_network(true, dir + "/list.m3u");
        host::add_playlist_source(dir);
        host::set_network_response("https://guide.invalid/list.xml", dir + "/guide.xml");
    }
    void TearDown() override
    {
        host::reset();
        std::filesystem::remove_all(dir);
    }
    void settle(ptv::Model &model)
    {
        for (int i = 0; i < 600; ++i)
        {
            model.poll();
            if (!model.refreshing() && !model.guide_refreshing())
                return;
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
        return "{\"operation\":\"" + std::string(op) + "\",\"source\":\"" + token +
               "\",\"channel\":\"" + std::string(id) + "\"}";
    }
    std::string dir;
};
TEST_F(RemoteBrowseTest, PagesSearchGuideAndPlaybackNeverExposeStreamAddresses)
{
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    std::string output;
    ASSERT_EQ(model.remote_browse(R"({"operation":"list"})", output), 200);
    const auto token = source(output);
    EXPECT_NE(output.find("Channel 00"), std::string::npos);
    EXPECT_EQ(output.find("Channel 40"), std::string::npos);
    EXPECT_EQ(output.find("https://"), std::string::npos);
    ASSERT_EQ(model.remote_browse(
                  "{\"operation\":\"list\",\"source\":\"" + token + "\",\"offset\":40}", output),
              200);
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
    ASSERT_TRUE(model.open());
    settle(model);
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
    auto blocked_snapshot = model.remote_browse_view();
    EXPECT_EQ(blocked_snapshot.handle(R"({"operation":"list"})", output), 403);
    EXPECT_TRUE(blocked_snapshot.order.empty());
    EXPECT_TRUE(blocked_snapshot.marks.empty());
    model.poll();
    host::set_keyboard_text("836295");
    model.poll();
    model.poll();
    host::set_keyboard_text("836295");
    model.poll();
    ASSERT_TRUE(model.parental().enabled());
    EXPECT_FALSE(model.parental().unlocked());
    model.parental_action(ptv::Model::ParentalAction::unlock);
    model.poll();
    host::set_keyboard_text("836295");
    model.poll();
    ASSERT_TRUE(model.set_category_rule("Private", 1));
    model.parental_action(ptv::Model::ParentalAction::lock);
    EXPECT_EQ(model.remote_browse(tune, output), 404);
    ASSERT_EQ(model.remote_browse(R"({"operation":"list"})", output), 200);
    EXPECT_EQ(output.find("Channel 00"), std::string::npos);
    ptv::PlayRequest play;
    EXPECT_FALSE(model.take_play_request(&play));
    model.close();
}
TEST_F(RemoteBrowseTest, PlaybackViewBorrowsClosedModelWithoutTuningUntilRevalidation)
{
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    settle(model);
    ASSERT_TRUE(model.hide_category("Private", true));
    ASSERT_TRUE(model.rename_channel(model.channel(80).id, "Phone football"));
    model.close(); // Catalog/guide remain owned, workers have stopped.
    ptv::RemoteBrowseView view;
    {
        auto original = model.remote_browse_view();
        view = std::move(original);
    } // The moved-from view is gone; all snapshot-owned data must remain valid.
    view.current_channel = std::string(model.channel(1).id);
    std::string output;
    ASSERT_EQ(view.handle(R"({"operation":"list","query":"Football"})", output), 200);
    EXPECT_NE(output.find("Phone football"), std::string::npos);
    const auto token = source(output);
    EXPECT_EQ(view.handle(request("play", token, model.channel(0).id), output), 404);
    EXPECT_TRUE(view.selected.empty());
    EXPECT_EQ(view.handle(request("play", token, model.channel(1).id), output), 200);
    EXPECT_TRUE(view.selected.empty()); // Already watching: no unnecessary restart.
    ASSERT_EQ(view.handle(request("guide", token, model.channel(80).id), output), 200);
    EXPECT_NE(output.find("News"), std::string::npos);
    ASSERT_EQ(view.handle(request("play", token, model.channel(80).id), output), 200);
    ptv::PlayRequest play;
    EXPECT_FALSE(model.take_play_request(&play)); // Phone only requests a normal stop.
    ASSERT_EQ(model.remote_browse(view.selection_request(), output), 200);
    ASSERT_TRUE(model.take_play_request(&play));
    EXPECT_EQ(play.channel_id, model.channel(80).id);
    ASSERT_TRUE(model.hide_channel(model.channel(80).id, true));
    EXPECT_EQ(view.handle(request("guide", token, model.channel(80).id), output), 200);
    EXPECT_EQ(model.remote_browse(view.selection_request(), output), 404);
    EXPECT_FALSE(model.take_play_request(&play)); // Policy is checked again after teardown.
    view.source = "stale";
    EXPECT_EQ(model.remote_browse(view.selection_request(), output), 409);
}
TEST_F(RemoteBrowseTest, LargeCatalogueRequestCost)
{
    std::ofstream list(dir + "/list.m3u");
    list << "#EXTM3U\n";
    for (unsigned i = 0; i < 250000; ++i)
        list << "#EXTINF:-1 group-title=\"General\",Channel " << i << "\nhttps://stream.invalid/"
             << i << ".ts\n";
    list.close();
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    for (unsigned i = 0; i < 12000 && model.refreshing(); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(model.channel_count(), 250000u);
    std::string output;
    auto borrowed = model.remote_browse_view(false);
    EXPECT_TRUE(borrowed.order.empty());
    EXPECT_TRUE(borrowed.marks.empty());
    EXPECT_TRUE(borrowed.names.empty());
    EXPECT_EQ(borrowed.menu_order.size(), 250000u);
    EXPECT_EQ(borrowed.menu_marks.size(), 250000u);
    auto started = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 10; ++i)
        ASSERT_EQ(model.remote_browse(R"({"operation":"list"})", output), 200);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    std::printf("250k catalogue first-page mean: %lld us\n", static_cast<long long>(elapsed / 10));
    EXPECT_LT(output.size(), 128u * 1024u);
    model.close();
}
} // namespace
