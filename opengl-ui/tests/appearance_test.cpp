// ProsperoTV - Appearance changes affect rendering, not channel artwork.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/theme.hpp"
#include "core/save_file.hpp"
#include <gtest/gtest.h>
#include <cstdlib>

TEST(Appearance, ThemeAndContrastPreserveVideoAndMakeTextOpaque)
{
    ptv::Frame frame;
    frame.glass_texture = 7;
    frame.scene.rounded_rect({0, 0, 100, 100}, 0, ptv::Color{.8f, .2f, .1f, .3f});
    frame.scene.image(8, {100, 0, 100, 100}, ptv::gfx::kFullUv, ptv::kWhite);
    const auto image = frame.scene.instances().back();
    frame.overlay.image(7, {500, 500, 500, 500}, {.25f, .25f, .5f, .5f}, ptv::kWhite);
    ptv::Settings settings;
    settings.theme = 1;
    ptv::apply_appearance(frame, settings);
    EXPECT_FLOAT_EQ(frame.scene.instances()[0].color_top[0], .1f);
    EXPECT_FLOAT_EQ(frame.scene.instances()[0].color_top[2], .8f);
    EXPECT_EQ(frame.scene.instances().back().params[3], image.params[3]);
    EXPECT_EQ(frame.scene.instances().back().color_top[0], image.color_top[0]);
    settings.high_contrast = true;
    ptv::apply_appearance(frame, settings);
    EXPECT_FLOAT_EQ(frame.scene.instances()[0].color_top[0], 0);
    EXPECT_FLOAT_EQ(frame.scene.instances()[0].color_top[3], 1);
    EXPECT_EQ(frame.backdrop.mode, ptv::gfx::BackdropMode::gradient);
    EXPECT_FALSE(frame.glass);
    EXPECT_EQ(frame.overlay.instances()[0].extra[0], 0);
    EXPECT_EQ(frame.overlay.instances()[0].params[3], 0);
    EXPECT_EQ(frame.scene.instances().back().color_top[0], image.color_top[0]);
    ptv::Frame panel;
    panel.scene.rounded_rect({0, 0, 200, 100}, 12, ptv::kWhite.with_alpha(.1f));
    ptv::apply_appearance(panel, settings);
    EXPECT_EQ(panel.scene.instances()[0].color_top[0], 0);
    EXPECT_EQ(panel.scene.instances()[0].color_top[3], 1);
    EXPECT_GE(panel.scene.instances()[0].params[1], 1.5f);
}

TEST(Appearance, LargerFontsMeasureFitAndDrawAtTheSameScale)
{
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    std::string data;
    ASSERT_TRUE(hui::save::read_file(
        std::string(std::getenv("KIT_FONTS")) + "/inter-regular.huifont", &data, 64u << 20));
    ptv::gfx::Font normal, larger;
    ASSERT_TRUE(ptv::load_menu_font(data, normal, false));
    ASSERT_TRUE(ptv::load_menu_font(data, larger, true));
    EXPECT_NEAR(larger.measure("Settings", 26), normal.measure("Settings", 26) * 1.25, .01);
    EXPECT_NEAR(larger.line_height(26), normal.line_height(26) * 1.25, .01);
    std::vector<ptv::gfx::GlyphQuad> before, after;
    normal.layout("Settings", 0, 0, 26, ptv::gfx::Align::left, before);
    larger.layout("Settings", 0, 0, 26, ptv::gfx::Align::left, after);
    ASSERT_EQ(before.size(), after.size());
    EXPECT_NEAR(after[0].y1 - after[0].y0, (before[0].y1 - before[0].y0) * 1.25, .01);
    EXPECT_LE(larger.measure(larger.fit("Long channel name", 26, 100), 26), 100);
    EXPECT_EQ(normal.atlas(), larger.atlas());
    EXPECT_FALSE(ptv::load_menu_font("invalid", larger, true));
}
