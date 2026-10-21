// ProsperoTV - The TV remote's keys (HDMI Device Link) as the menu reads them.
// Copyright (C) 2026 ThatGuyKalvin
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/tv_remote_input.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstring>

namespace
{
using hui::Action;

std::array<unsigned char, IPTV_TV_REMOTE_SAMPLE_SIZE> sample(std::uint32_t buttons,
                                                             unsigned char key)
{
    std::array<unsigned char, IPTV_TV_REMOTE_SAMPLE_SIZE> data{};
    std::memcpy(data.data(), &buttons, sizeof(buttons));
    data[IPTV_TV_REMOTE_KEY_OFFSET] = key;
    return data;
}

std::uint32_t word(std::uint32_t buttons, unsigned char key, bool system_has_screen = false)
{
    return iptv_tv_remote_buttons(sample(buttons, key).data(), system_has_screen);
}
} // namespace

TEST(TvRemote, KeysBecomeTheControllersButtons)
{
    EXPECT_EQ(word(0, 13), IPTV_TV_REMOTE_CROSS);
    EXPECT_EQ(word(0, 15), IPTV_TV_REMOTE_CIRCLE);
    EXPECT_EQ(word(0, 18), IPTV_TV_REMOTE_OPTIONS);
    EXPECT_EQ(word(0, 27), IPTV_TV_REMOTE_OPTIONS);
    for (unsigned char play : {22, 26, 45})
        EXPECT_EQ(word(0, play), IPTV_TV_REMOTE_PLAY);
    EXPECT_EQ(word(0, 23), IPTV_TV_REMOTE_REWIND);
    EXPECT_EQ(word(0, 24), IPTV_TV_REMOTE_FAST_FORWARD);
    EXPECT_EQ(word(0, 41), hui::pad_bits::kR1); // Channel +
    EXPECT_EQ(word(0, 42), hui::pad_bits::kL1); // Channel -
    EXPECT_EQ(word(0, 0), 0u);
    EXPECT_EQ(word(0, 99), 0u);
    // Arrows arrive as the D-pad; bits no controller has are left out.
    EXPECT_EQ(word(hui::pad_bits::kUp | 0x00010000u, 0), hui::pad_bits::kUp);
}

TEST(TvRemote, InterceptedSamplesAreTheAppsUnlessTheSystemHasTheScreen)
{
    const std::uint32_t intercepted = IPTV_TV_REMOTE_INTERCEPTED | hui::pad_bits::kDown;
    EXPECT_EQ(word(intercepted, 13), hui::pad_bits::kDown | IPTV_TV_REMOTE_CROSS);
    EXPECT_EQ(word(intercepted, 13, true), 0u);
    EXPECT_EQ(word(hui::pad_bits::kDown, 13, true), hui::pad_bits::kDown | IPTV_TV_REMOTE_CROSS);
}

TEST(TvRemote, MenuReadsOkPlayAndTheMediaKeys)
{
    hui::InputTracker tracker;
    auto press = [&](std::uint32_t remote_word, std::uint64_t now)
    {
        const hui::PadSample samples[] = {ptv::tv_remote_sample(remote_word, now),
                                          ptv::tv_remote_sample(0, now)};
        return tracker.update(samples, now);
    };
    EXPECT_TRUE(press(IPTV_TV_REMOTE_CROSS, 1000).is_pressed(Action::confirm));
    EXPECT_TRUE(press(IPTV_TV_REMOTE_PLAY, 2000).is_pressed(Action::confirm));
    EXPECT_TRUE(press(IPTV_TV_REMOTE_CIRCLE, 3000).is_pressed(Action::back));
    EXPECT_TRUE(press(IPTV_TV_REMOTE_OPTIONS, 4000).is_pressed(Action::menu));
    EXPECT_TRUE(press(IPTV_TV_REMOTE_REWIND, 5000).is_pressed(Action::jump_prev));
    EXPECT_TRUE(press(IPTV_TV_REMOTE_FAST_FORWARD, 6000).is_pressed(Action::jump_next));

    const hui::PadSample held[] = {ptv::tv_remote_sample(hui::pad_bits::kRight, 7000)};
    const hui::InputFrame right = tracker.update(held, 7000);
    EXPECT_EQ(right.nav, hui::Direction::right);
    EXPECT_TRUE(right.is_held(Action::right));
}

TEST(TvRemote, MergeKeepsTheControllersInputAndCountsTheRemoteAsPresent)
{
    hui::InputFrame input; // the controller is off
    input.focus_lost = true;
    input.pressed = hui::action_bit(Action::north);
    hui::InputFrame remote;
    remote.pressed = remote.held = hui::action_bit(Action::down);
    remote.nav = hui::Direction::down;
    remote.nav_repeat = true;
    ptv::merge_tv_remote(input, remote);
    EXPECT_TRUE(input.is_pressed(Action::north));
    EXPECT_TRUE(input.is_pressed(Action::down));
    EXPECT_EQ(input.nav, hui::Direction::down);
    EXPECT_TRUE(input.nav_repeat);
    EXPECT_TRUE(input.connected);
    EXPECT_FALSE(input.focus_lost);

    hui::InputFrame idle;
    hui::InputFrame nothing;
    ptv::merge_tv_remote(idle, nothing);
    EXPECT_FALSE(idle.connected);
    EXPECT_EQ(idle.nav, hui::Direction::none);
}
