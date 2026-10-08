/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_picture.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace
{

iptv_picture_layout_t Layout(std::uint32_t width, std::uint32_t height, std::uint32_t sar_num,
                             std::uint32_t sar_den, int aspect, int zoom)
{
    iptv_picture_layout_t layout{};
    EXPECT_EQ(
        iptv_picture_layout(width, height, sar_num, sar_den, aspect, zoom, 1920u, 1080u, &layout),
        0);
    return layout;
}

TEST(IptvPictureTest, KeepsAWideFilmsShapeWithBarsAboveAndBelow)
{
    const auto layout = Layout(1920u, 800u, 0u, 0u, IPTV_ASPECT_AUTO, IPTV_ZOOM_FIT);
    EXPECT_EQ(layout.x, 0);
    EXPECT_EQ(layout.y, 140);
    EXPECT_EQ(layout.width, 1920u);
    EXPECT_EQ(layout.height, 800u);
    EXPECT_EQ(layout.visible_x, 0u);
    EXPECT_EQ(layout.visible_y, 0u);
    EXPECT_EQ(layout.visible_width, 1920u);
    EXPECT_EQ(layout.visible_height, 800u);
}

TEST(IptvPictureTest, UsesThePixelShapeOfAnamorphicVideo)
{
    // PAL 4:3 (16:15 pixels): bars at the sides.
    auto layout = Layout(720u, 576u, 16u, 15u, IPTV_ASPECT_AUTO, IPTV_ZOOM_FIT);
    EXPECT_EQ(layout.width, 1440u);
    EXPECT_EQ(layout.height, 1080u);
    EXPECT_EQ(layout.x, 240);
    EXPECT_EQ(layout.y, 0);
    // PAL 16:9 (64:45 pixels): the whole screen.
    layout = Layout(720u, 576u, 64u, 45u, IPTV_ASPECT_AUTO, IPTV_ZOOM_FIT);
    EXPECT_EQ(layout.width, 1920u);
    EXPECT_EQ(layout.height, 1080u);
    // A damaged pixel shape is ignored.
    layout = Layout(1920u, 1080u, 100u, 1u, IPTV_ASPECT_AUTO, IPTV_ZOOM_FIT);
    EXPECT_EQ(layout.width, 1920u);
    EXPECT_EQ(layout.height, 1080u);
}

TEST(IptvPictureTest, ForcedShapesAndStretch)
{
    auto layout = Layout(1920u, 1080u, 0u, 0u, IPTV_ASPECT_4_3, IPTV_ZOOM_FIT);
    EXPECT_EQ(layout.width, 1440u);
    EXPECT_EQ(layout.x, 240);
    layout = Layout(1920u, 1080u, 0u, 0u, IPTV_ASPECT_2_39, IPTV_ZOOM_FIT);
    EXPECT_EQ(layout.width, 1920u);
    EXPECT_EQ(layout.height, 804u);
    EXPECT_EQ(layout.y, 138);
    layout = Layout(1920u, 800u, 0u, 0u, IPTV_ASPECT_STRETCH, IPTV_ZOOM_FIT);
    EXPECT_EQ(layout.width, 1920u);
    EXPECT_EQ(layout.height, 1080u);
    EXPECT_EQ(layout.visible_height, 800u);
}

TEST(IptvPictureTest, FillCutsTheSidesOfAWideFilm)
{
    const auto layout = Layout(1920u, 800u, 0u, 0u, IPTV_ASPECT_AUTO, IPTV_ZOOM_FILL);
    EXPECT_EQ(layout.height, 1080u);
    EXPECT_EQ(layout.width, 2592u);
    EXPECT_EQ(layout.x, -336);
    EXPECT_EQ(layout.y, 0);
    // The middle 1422 columns of the frame are on screen.
    EXPECT_EQ(layout.visible_width, 1422u);
    EXPECT_EQ(layout.visible_x, 248u);
    EXPECT_EQ(layout.visible_height, 800u);
}

TEST(IptvPictureTest, ZoomGrowsTheFittedPicture)
{
    const auto layout = Layout(1920u, 1080u, 0u, 0u, IPTV_ASPECT_AUTO, IPTV_ZOOM_133);
    EXPECT_EQ(layout.width, 2560u);
    EXPECT_EQ(layout.height, 1440u);
    EXPECT_EQ(layout.x, -320);
    EXPECT_EQ(layout.y, -180);
    EXPECT_EQ(layout.visible_width, 1440u);
    EXPECT_EQ(layout.visible_height, 810u);
    EXPECT_EQ(layout.visible_x, 240u);
    EXPECT_EQ(layout.visible_y, 134u);
    EXPECT_EQ(layout.visible_x % 2u, 0u);
    EXPECT_EQ(layout.visible_y % 2u, 0u);
}

TEST(IptvPictureTest, RejectsEmptySizesAndNamesModes)
{
    iptv_picture_layout_t layout{};
    EXPECT_EQ(iptv_picture_layout(0u, 1080u, 0u, 0u, 0, 0, 1920u, 1080u, &layout), -1);
    EXPECT_EQ(iptv_picture_layout(1920u, 1080u, 0u, 0u, 0, 0, 1920u, 0u, &layout), -1);
    EXPECT_STREQ(iptv_picture_aspect_label(IPTV_ASPECT_AUTO), "Auto");
    EXPECT_STREQ(iptv_picture_aspect_label(IPTV_ASPECT_2_39), "2.39:1");
    EXPECT_STREQ(iptv_picture_zoom_label(IPTV_ZOOM_FILL), "Fill screen");
    EXPECT_STREQ(iptv_picture_zoom_label(99), "Fit");

    iptv_picture_set_modes(IPTV_ASPECT_4_3, 42);
    int aspect = -1, zoom = -1;
    iptv_picture_modes(&aspect, &zoom);
    EXPECT_EQ(aspect, IPTV_ASPECT_4_3);
    EXPECT_EQ(zoom, IPTV_ZOOM_FIT);
    iptv_picture_set_modes(IPTV_ASPECT_AUTO, IPTV_ZOOM_FIT);
}

} // namespace
