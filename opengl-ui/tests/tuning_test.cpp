// ProsperoTV - Tests of the tuning screen the player shows while a channel opens.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv_plate.hpp"
#include "tv_tuning.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace
{

constexpr float kBarX = 400.0f;
constexpr float kBarY = 800.0f;
constexpr float kBarWidth = 1000.0f;
constexpr float kBarHeight = 10.0f;

std::uint8_t luma_at(const std::vector<std::uint8_t> &surface, int x, int y)
{
    return surface[static_cast<std::size_t>(y) * tuning::kWidth + static_cast<std::size_t>(x)];
}

// Frames as the loading thread asks for them, for about `seconds`.
int run(std::vector<std::uint8_t> &surface, double seconds)
{
    int frames = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until)
    {
        if (!tv_tuning_compose(surface.data(), surface.size()))
            return -frames - 1;
        ++frames;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return frames;
}

TEST(TuningScreen, TheBarCreepsWithTheStagesThenRunsOutAndFades)
{
    // A grey picture; the bar is white.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(tuning::kWidth) * tuning::kHeight * 4,
                                   60);
    const std::uint8_t white[3] = {255, 255, 255};
    std::vector<std::uint8_t> surface(tuning::surface_bytes(false));
    const int row = static_cast<int>(kBarY + kBarHeight * 0.5f);
    const auto filled = [&](float share)
    { return luma_at(surface, static_cast<int>(kBarX + kBarWidth * share), row) > 200; };

    EXPECT_EQ(tv_tuning_active(), 0);
    tv_tuning_set_picture(rgba.data(), kBarX, kBarY, kBarWidth, kBarHeight, white, 0.10f);
    ASSERT_EQ(tv_tuning_active(), 1);

    // The first frame is the picture with the bar where the menu left it.
    ASSERT_EQ(tv_tuning_compose(surface.data(), surface.size()), 1);
    EXPECT_TRUE(filled(0.05f));
    EXPECT_FALSE(filled(0.50f));
    EXPECT_NEAR(luma_at(surface, 100, 100), 16 + 219 * 60 / 255, 2);

    // A stage lifts the floor: the bar follows quickly, and does not run
    // ahead of its ceiling.
    tv_tuning_stage(0.60f, 0.62f);
    ASSERT_GT(run(surface, 0.6), 0);
    EXPECT_TRUE(filled(0.55f));
    EXPECT_FALSE(filled(0.70f));

    // The first picture is ready: the bar runs to its end, the screen fades
    // to black, and then there is nothing more to show.
    tv_tuning_finishing();
    std::vector<std::uint8_t> complete;
    for (int frame = 0; frame < 200; ++frame)
    {
        if (!tv_tuning_compose(surface.data(), surface.size()))
            break;
        if (complete.empty() && filled(0.99f))
            complete = surface;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    EXPECT_FALSE(complete.empty());
    EXPECT_EQ(tv_tuning_compose(surface.data(), surface.size()), 0);
    EXPECT_EQ(luma_at(surface, 100, 100), 16);
    EXPECT_EQ(luma_at(surface, static_cast<int>(kBarX + 20.0f), row), 16);

    tv_tuning_clear();
    EXPECT_EQ(tv_tuning_active(), 0);
}

TEST(TuningScreen, WithoutAPictureNothingIsShown)
{
    std::vector<std::uint8_t> surface(tuning::surface_bytes(false));
    const std::uint8_t white[3] = {255, 255, 255};
    tv_tuning_set_picture(nullptr, kBarX, kBarY, kBarWidth, kBarHeight, white, 0.1f);
    EXPECT_EQ(tv_tuning_active(), 0);
    EXPECT_EQ(tv_tuning_compose(surface.data(), surface.size()), 0);
    // A surface too small for the picture is refused.
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(tuning::kWidth) * tuning::kHeight * 4);
    tv_tuning_set_picture(rgba.data(), kBarX, kBarY, kBarWidth, kBarHeight, white, 0.1f);
    std::vector<std::uint8_t> small(1024);
    EXPECT_EQ(tv_tuning_compose(small.data(), small.size()), 0);
    tv_tuning_clear();
}

} // namespace
