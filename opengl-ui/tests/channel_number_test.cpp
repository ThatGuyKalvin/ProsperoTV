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
    host::add_playlist_source(dir);
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

TEST(ChannelOrganization, NamesHiddenChannelsAndIndependentOrdersSurviveRestart)
{
    host::reset();
    char pattern[] = "/tmp/prosperotv-organize-XXXXXX";
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
           "#EXTINF:-1 tvg-id=\"a\" tvg-chno=\"1\",Alpha\nhttps://provider.invalid/1.ts\n"
           "#EXTINF:-1 tvg-id=\"b\" tvg-chno=\"2\",Beta\nhttps://provider.invalid/2.ts\n"
           "#EXTINF:-1 tvg-id=\"c\" tvg-chno=\"3\",Gamma\nhttps://provider.invalid/3.ts\n";
    host::set_network(true, dir + "/list.m3u");
    host::add_playlist_source(dir);
    ptv::Model model(dir);
    ASSERT_TRUE(model.open());
    for (int i = 0; i < 1000 && (!model.has_catalog() || model.refreshing()); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_EQ(model.visible_count(), 3u);
    const unsigned alpha = model.visible(0), beta = model.visible(1), gamma = model.visible(2);
    const std::string a(model.channel(alpha).id), b(model.channel(beta).id),
        c(model.channel(gamma).id);
    ASSERT_TRUE(model.move_channel(c, -1));
    EXPECT_EQ(model.position_of(c), 1);
    EXPECT_EQ(model.position_for_number("2"), 2);
    model.toggle_favorite(alpha);
    model.toggle_favorite(beta);
    model.toggle_favorite(gamma);
    model.set_group(ptv::Group::favorites);
    EXPECT_EQ(model.position_of(b), 1); // favorite order starts independently
    ASSERT_TRUE(model.move_channel(b, -1));
    EXPECT_EQ(model.position_of(b), 0);
    model.set_group(ptv::Group::all);
    EXPECT_EQ(model.position_of(b), 2);
    model.set_group(ptv::Group::recently_added);
    EXPECT_FALSE(model.move_channel(c, -1));
    EXPECT_FALSE(model.reset_channel_order());
    model.set_group(ptv::Group::all);
    EXPECT_EQ(model.position_of(c), 1);
    ASSERT_TRUE(model.rename_channel(a, "Zebra"));
    EXPECT_EQ(model.find(a)->name, "Zebra");
    model.set_query("Zebra");
    ASSERT_EQ(model.visible_count(), 1u);
    EXPECT_EQ(model.channel(model.visible(0)).id, a);
    model.set_query("");
    ASSERT_TRUE(model.hide_channel(a, true));
    EXPECT_EQ(model.position_of(a), -1);
    EXPECT_EQ(model.position_for_number("1"), -1);
    EXPECT_FALSE(model.play(alpha));
    EXPECT_FALSE(model.preview_request(a));
    EXPECT_EQ(model.hidden_channels().size(), 1u);
    EXPECT_FALSE(model.rename_channel(a, "Bad\nname"));
    model.refresh();
    for (int i = 0; i < 1000 && model.refreshing(); ++i)
    {
        model.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    ASSERT_FALSE(model.refreshing());
    EXPECT_EQ(model.find(a)->name, "Zebra");
    EXPECT_EQ(model.position_of(a), -1);
    model.close();
    host::set_network(false, "");
    ASSERT_TRUE(model.open());
    EXPECT_EQ(model.find(a)->name, "Zebra");
    EXPECT_EQ(model.position_of(a), -1);
    EXPECT_EQ(model.position_of(c), 0);
    model.set_group(ptv::Group::favorites);
    EXPECT_EQ(model.position_of(b), 0);
    ASSERT_TRUE(model.hide_channel(a, false));
    EXPECT_EQ(model.position_of(b), 0);
    ASSERT_TRUE(model.rename_channel(a, ""));
    EXPECT_EQ(model.find(a)->name, "Alpha"); // provider cache was never changed
    ASSERT_TRUE(model.reset_channel_order());
    EXPECT_EQ(model.position_of(a), 0);
    model.set_group(ptv::Group::all);
    EXPECT_EQ(model.position_of(c), 1); // resetting favorites did not reset live
    ASSERT_TRUE(model.reset_channel_order());
    EXPECT_EQ(model.position_of(b), 1);

    hui::ui::Fonts fonts{};
    ptv::Shared shared(model, fonts, {});
    ptv::LibrarySheet sheet(shared);
    hui::ui::Feedback feedback;
    sheet.open(false, a, feedback);
    const auto press = [&](hui::Action action)
    {
        hui::InputFrame input{};
        input.pressed = hui::action_bit(action);
        sheet.handle(input, feedback);
    };
    const auto down = [&]()
    {
        hui::InputFrame input{};
        input.nav = hui::Direction::down;
        sheet.handle(input, feedback);
    };
    press(hui::Action::page_prev);
    down();
    down(); // Hide channel
    press(hui::Action::confirm);
    EXPECT_EQ(model.position_of(a), -1);
    down();
    down();
    down();
    down(); // Hidden channels
    press(hui::Action::confirm);
    press(hui::Action::confirm); // Show Alpha
    EXPECT_GE(model.position_of(a), 0);
    model.close();
}

TEST(ChannelOrganization, LibrarySeparatesSourcesAndRollsBackFailedOrderChanges)
{
    ptv::Library library;
    ASSERT_TRUE(library.open(":memory:"));
    ASSERT_TRUE(library.rename_channel(11, "same-id", "First source"));
    ASSERT_TRUE(library.rename_channel(22, "same-id", "Second source"));
    ASSERT_TRUE(library.hide_channel(11, "same-id", true));
    EXPECT_EQ(library.channel_names(11).at("same-id"), "First source");
    EXPECT_EQ(library.channel_names(22).at("same-id"), "Second source");
    EXPECT_TRUE(library.hidden_channels(22).empty());
    ASSERT_TRUE(library.save_channel_order(11, false, {"b", "a"}));
    ASSERT_TRUE(library.save_channel_order(11, true, {"a", "b"}));
    EXPECT_FALSE(library.save_channel_order(11, false, {"c", "c"}));
    EXPECT_EQ(library.channel_order(11, false), (std::vector<std::string>{"b", "a"}));
    EXPECT_EQ(library.channel_order(11, true), (std::vector<std::string>{"a", "b"}));
    EXPECT_TRUE(library.channel_order(22, false).empty());
    ASSERT_TRUE(library.close());
    EXPECT_FALSE(library.hide_channel(11, "a", true));
    EXPECT_FALSE(library.rename_channel(11, "a", "Name"));
    EXPECT_FALSE(library.save_channel_order(11, false, {"a"}));
}
