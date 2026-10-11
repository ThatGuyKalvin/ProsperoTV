// ProsperoTV - Provider number lookup and controller number pad.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/model.hpp"
#include "tv/library_sheet.hpp"
#include "host_platform.hpp"
#include <gtest/gtest.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

TEST(ChannelNumbers, NumberPadPlaysUniqueVisibleChannelAndRejectsAmbiguousOrHiddenNumbers)
{
    host::reset();
    char pattern[] = "/tmp/prosperotv-numbers-XXXXXX";
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
    std::ofstream(dir + "/list.m3u")
        << "#EXTM3U\n"
           "#EXTINF:-1 tvg-chno=\"11\" group-title=\"News\",Alpha\nhttps://provider.invalid/1.ts\n"
           "#EXTINF:-1 tvg-chno=\"2.1\" group-title=\"News\",Beta\nhttps://provider.invalid/2.ts\n"
           "#EXTINF:-1 tvg-chno=\"8\" group-title=\"News\",Gamma\nhttps://provider.invalid/3.ts\n"
           "#EXTINF:-1 tvg-chno=\"8\" group-title=\"Sport\",Delta\nhttps://provider.invalid/4.ts\n"
           "#EXTINF:-1 group-title=\"Sport\",Fallback\nhttps://provider.invalid/5.ts\n";
    host::set_network(true, dir + "/list.m3u");
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    for (int i = 0; i < 1000 && (!model.has_catalog() || model.refreshing()); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_EQ(model.visible_count(), 5u);
    EXPECT_EQ(model.position_for_number("8"), -2);
    EXPECT_EQ(model.position_for_number("bad"), -1);
    const int subchannel = model.position_for_number("2.1");
    ASSERT_GE(subchannel, 0);
    EXPECT_EQ(model.channel(model.visible(static_cast<unsigned>(subchannel))).name, "Beta");
    ASSERT_TRUE(model.hide_category("Sport", true));
    EXPECT_GE(model.position_for_number("8"), 0);
    const int target = model.position_for_number("11");
    ASSERT_GE(target, 0);
    EXPECT_EQ(model.number_label(model.visible(static_cast<unsigned>(target))), "11");
    hui::ui::Fonts fonts{};
    ptv::Shared shared(model, fonts, {});
    ptv::LibrarySheet sheet(shared);
    hui::ui::Feedback feedback;
    sheet.open(false, "", feedback);
    const auto press = [&](hui::Action action)
    {
        hui::InputFrame input{};
        input.pressed = hui::action_bit(action);
        sheet.handle(input, feedback);
    };
    const auto move = [&](hui::Direction direction)
    {
        hui::InputFrame input{};
        input.nav = direction;
        sheet.handle(input, feedback);
    };
    press(hui::Action::page_next); // the number pad
    press(hui::Action::confirm);   // 1
    press(hui::Action::confirm);   // 11
    move(hui::Direction::down);
    move(hui::Direction::down);
    move(hui::Direction::down);
    move(hui::Direction::down);
    move(hui::Direction::right); // Watch
    press(hui::Action::confirm);
    EXPECT_FALSE(sheet.is_open());
    ptv::PlayRequest request;
    ASSERT_TRUE(model.take_play_request(&request));
    EXPECT_EQ(request.channel_name, "Alpha");
    ASSERT_TRUE(model.hide_category("News", true));
    EXPECT_EQ(model.position_for_number("11"), -1);
    EXPECT_FALSE(model.take_play_request(&request));
    ASSERT_TRUE(model.hide_category("News", false));
    sheet.open(false, "", feedback, false); // selecting a multiview tile must not tune
    press(hui::Action::page_next);
    press(hui::Action::confirm);
    EXPECT_FALSE(model.take_play_request(&request));
    model.close();
    host::set_network(false, "");
    ASSERT_TRUE(model.open());
    ASSERT_GE(model.position_for_number("11"), 0); // persisted provider number
    model.close();
}
