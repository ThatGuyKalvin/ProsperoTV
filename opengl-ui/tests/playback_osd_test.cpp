// ProsperoTV - Compositor bounds and the real baked font on video surfaces.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tv/playback_osd.hpp"
#include "core/save_file.hpp"
#include "iptv_color.h"
#include "iptv_input.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

TEST(VideoPanel, HdrPaperWhiteAndCaptionAlphaKeepVideoAndPaddingIntact)
{
    EXPECT_EQ(iptv_color_ui_luma(16, 1), 64);
    EXPECT_NEAR(iptv_color_ui_luma(235, 1), 721, 1);
    std::uint16_t hlg_white[3];
    iptv_color_ui_yuv(255, 255, 255, hlg_white, 1);
    EXPECT_NEAR(hlg_white[0], 721, 1);
    EXPECT_EQ(hlg_white[1], 512);
    EXPECT_EQ(hlg_white[2], 512);
    EXPECT_EQ(iptv_color_ui_luma(16, 0), 64);
    EXPECT_NEAR(iptv_color_ui_luma(235, 0), 573, 1);
    for (unsigned i = 1; i < 256; ++i)
        EXPECT_GE(iptv_color_ui_luma(i, 0), iptv_color_ui_luma(i - 1, 0));
    std::uint16_t white[3];
    iptv_color_ui_yuv(255, 255, 255, white, 0);
    EXPECT_NEAR(white[0], 573, 1);
    EXPECT_EQ(white[1], 512);
    EXPECT_EQ(white[2], 512);
    constexpr unsigned pitch = 8, sh = 4, count = pitch * sh * 3 / 2;
    std::vector<std::uint16_t> surface(count + 8, 256);
    ptv::VideoPanel panel;
    panel.reset(0, 0, 1920, 1080);
    std::fill(panel.pixels.begin(), panel.pixels.end(), 235);
    ASSERT_TRUE(panel.composite(surface.data(), count * 2, pitch, sh, 4, 4, 10, true));
    EXPECT_EQ(surface[0], white[0]);
    EXPECT_EQ(surface[4], 256);
    EXPECT_EQ(surface[pitch * sh], 512);
    EXPECT_EQ(surface.back(), 256);
    iptv::SubtitleCue cue;
    cue.canvas_width = cue.canvas_height = 4;
    iptv::SubtitleBitmap bitmap;
    bitmap.width = bitmap.height = 4;
    bitmap.argb.assign(16, 0x80ffffff);
    cue.bitmaps.push_back(bitmap);
    std::fill(surface.begin(), surface.end(), 256);
    ASSERT_TRUE(
        ptv::composite_subtitle_bitmaps(cue, surface.data(), count * 2, pitch, sh, 4, 4, 10, true));
    EXPECT_EQ(surface[0], (256 * 127 + white[0] * 128 + 127) / 255);
    EXPECT_EQ(surface[pitch * sh], (256 * 127 + 512 * 128 + 127) / 255);
    EXPECT_EQ(surface[4], 256);
    EXPECT_EQ(surface.back(), 256);
}

TEST(VideoPanel, CompositeKeepsPaddingAndSupportsNativeLowBitMain10)
{
    ptv::VideoPanel panel;
    panel.reset(480, 270, 960, 540);
    std::fill(panel.pixels.begin(), panel.pixels.end(), 235);
    constexpr unsigned pitch = 40, sh = 20, vw = 32, vh = 16;
    constexpr unsigned count = pitch * sh * 3 / 2;
    std::vector<std::uint8_t> eight(count + 32, 77);
    std::vector<std::uint16_t> ten(count + 16, 77 * 4);
    ASSERT_TRUE(panel.composite(eight.data(), count, pitch, sh, vw, vh, 8));
    ASSERT_TRUE(panel.composite(ten.data(), count * 2, pitch, sh, vw, vh, 10));
    for (unsigned y = 0; y < sh; ++y)
        for (unsigned x = 0; x < pitch; ++x)
        {
            const unsigned expected = y >= 4 && y < 12 && x >= 8 && x < 24 ? 235 : 77;
            EXPECT_EQ(eight[y * pitch + x], expected);
            EXPECT_EQ(ten[y * pitch + x], expected * 4);
        }
    for (unsigned y = 0; y < sh / 2; ++y)
        for (unsigned x = 0; x < pitch; ++x)
        {
            const unsigned expected = y >= 2 && y < 6 && x >= 8 && x < 24 ? 128 : 77;
            EXPECT_EQ(eight[pitch * sh + y * pitch + x], expected);
            EXPECT_EQ(ten[pitch * sh + y * pitch + x], expected * 4);
        }
    EXPECT_EQ(eight.back(), 77);
    EXPECT_EQ(ten.back(), 308);
    const auto unchanged = eight;
    EXPECT_FALSE(panel.composite(eight.data(), count - 1, pitch, sh, vw, vh, 8));
    EXPECT_FALSE(panel.composite(eight.data(), count, pitch, sh, pitch + 1, vh, 8));
    EXPECT_FALSE(panel.composite(eight.data(), count, pitch, sh, vw, vh, 12));
    EXPECT_FALSE(panel.composite(eight.data(), count, pitch - 1, sh, vw, vh, 8));
    EXPECT_FALSE(panel.composite(eight.data(), count, pitch, sh, vw, sh + 1, 8));
    EXPECT_EQ(eight, unchanged);
    panel.reset(1900, 0, 200, 200);
    EXPECT_TRUE(panel.pixels.empty());
}

TEST(VideoPanel, SubtitleBitmapsScaleClipBlendAndLeavePaddingUntouched)
{
    constexpr unsigned pitch = 16, sh = 12, vw = 13, vh = 11, count = pitch * sh * 3 / 2;
    iptv::SubtitleCue cue;
    cue.canvas_width = cue.canvas_height = 5;
    iptv::SubtitleBitmap bitmap;
    bitmap.x = bitmap.y = 1;
    bitmap.width = bitmap.height = 3;
    bitmap.argb.assign(9, 0xffffffff);
    bitmap.argb[4] = 0; // Transparent hole must reveal the picture's luma/chroma.
    cue.bitmaps.push_back(bitmap);
    const auto opaque = [&](unsigned x, unsigned y)
    {
        if (x >= vw || y >= vh)
            return false;
        const auto sx = x * 5 / vw, sy = y * 5 / vh;
        return sx >= 1 && sx <= 3 && sy >= 1 && sy <= 3 && !(sx == 2 && sy == 2);
    };
    std::vector<std::uint8_t> eight(count + 20, 77);
    std::vector<std::uint16_t> ten(count + 10, 308);
    ASSERT_TRUE(ptv::composite_subtitle_bitmaps(cue, eight.data(), count, pitch, sh, vw, vh, 8));
    ASSERT_TRUE(ptv::composite_subtitle_bitmaps(cue, ten.data(), count * 2, pitch, sh, vw, vh, 10));
    for (unsigned y = 0; y < sh; ++y)
        for (unsigned x = 0; x < pitch; ++x)
        {
            EXPECT_EQ(eight[y * pitch + x], opaque(x, y) ? 235 : 77);
            EXPECT_EQ(ten[y * pitch + x], opaque(x, y) ? 940 : 308);
        }
    for (unsigned y = 0; y < sh / 2; ++y)
        for (unsigned x = 0; x < pitch; x += 2)
        {
            const unsigned alpha = opaque(x, y * 2) + opaque(x + 1, y * 2) + opaque(x, y * 2 + 1) +
                                   opaque(x + 1, y * 2 + 1);
            for (unsigned c = 0; c < 2; ++c)
            {
                EXPECT_EQ(eight[pitch * sh + y * pitch + x + c],
                          (77 * (4 - alpha) + 128 * alpha + 2) / 4);
                EXPECT_EQ(ten[pitch * sh + y * pitch + x + c],
                          (308 * (4 - alpha) + 512 * alpha + 2) / 4);
            }
        }
    EXPECT_EQ(eight.back(), 77);
    EXPECT_EQ(ten.back(), 308);
    const auto before = eight;
    EXPECT_FALSE(
        ptv::composite_subtitle_bitmaps(cue, eight.data(), count - 1, pitch, sh, vw, vh, 8));
    EXPECT_EQ(eight, before);
    cue.bitmaps[0].x = -1;
    EXPECT_TRUE(ptv::composite_subtitle_bitmaps(cue, eight.data(), count, pitch, sh, vw, vh, 8));
    cue.bitmaps[0].argb.clear();
    EXPECT_FALSE(ptv::composite_subtitle_bitmaps(cue, eight.data(), count, pitch, sh, vw, vh, 8));
}

TEST(VideoPanel, BakedUnicodeFontDrawsClippedAntialiasedText)
{
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    std::string data;
    ASSERT_TRUE(hui::save::read_file(
        std::string(std::getenv("KIT_FONTS")) + "/inter-regular.huifont", &data));
    hui::gfx::Font font;
    ASSERT_TRUE(font.load(data));
    ptv::VideoPanel panel;
    panel.reset(0, 0, 400, 80);
    panel.text(font, "Français · Ελληνικά · Новости", -6, 45, 32, 380);
    EXPECT_GT(
        std::count_if(panel.pixels.begin(), panel.pixels.end(), [](auto v) { return v > 220; }),
        100);
    EXPECT_GT(std::count_if(panel.pixels.begin(), panel.pixels.end(),
                            [](auto v) { return v > 28 && v < 220; }),
              100);
    const auto before = panel.pixels;
    panel.text(font, "Beyond the panel", 600, 200, 32, 300);
    EXPECT_EQ(panel.pixels, before);
}

TEST(PlaybackScreensaver, BlanksPausedSdrAndHdrAndConsumesOnlyTheWakeInput)
{
    ptv::Model model("/tmp/prosperotv-unused-saver-test");
    ptv::PlayRequest request;
    ptv::Settings settings;
    settings.screensaver_minutes = 1;
    ptv::PlaybackOsd osd(model, request, settings);
    osd.set_paused(true);
    EXPECT_FALSE(osd.draw(nullptr, 0, 8, 4, 8, 4, 8, 1));
    EXPECT_TRUE(osd.draw(nullptr, 0, 8, 4, 8, 4, 8, 60000001));
    std::vector<std::uint8_t> pixels(52, 99);
    EXPECT_TRUE(osd.draw(pixels.data(), 48, 8, 4, 8, 4, 8, 60000001));
    EXPECT_EQ(pixels[0], 16);
    EXPECT_EQ(pixels[31], 16);
    EXPECT_EQ(pixels[32], 128);
    EXPECT_EQ(pixels[47], 128);
    EXPECT_EQ(pixels[48], 99);
    std::vector<std::uint16_t> hdr(52, 99);
    EXPECT_TRUE(osd.draw(hdr.data(), 96, 8, 4, 8, 4, 10, 60000001, {}, 1));
    EXPECT_EQ(hdr[0], 64);
    EXPECT_EQ(hdr[32], 512);
    EXPECT_EQ(hdr[48], 99);
    EXPECT_FALSE(osd.draw(hdr.data(), 95, 8, 4, 8, 4, 10, 60000001, {}, 1));
    EXPECT_EQ(osd.input(IPTV_INPUT_PLAY_PAUSE, 60000002), 1);
    EXPECT_EQ(osd.input(IPTV_INPUT_PLAY_PAUSE, 60000003), 0);
    EXPECT_FALSE(osd.draw(nullptr, 0, 8, 4, 8, 4, 8, 60000004));
    osd.set_paused(false);
    EXPECT_FALSE(osd.draw(nullptr, 0, 8, 4, 8, 4, 8, 600000001));
}

TEST(VideoPanel, ZoomKeepsPanelsInsideTheVisibleCenter)
{
    ptv::VideoPanel panel;
    panel.reset(0, 0, 192, 108);
    std::vector<std::uint8_t> frame(1920 * 1080 * 3 / 2, 99);
    ASSERT_TRUE(
        panel.composite(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 0, 1.5f, 1.5f));
    EXPECT_EQ(frame[0], 99);
    EXPECT_EQ(frame[180 * 1920 + 320], 28);
    EXPECT_EQ(frame[179 * 1920 + 320], 99);
    EXPECT_FALSE(panel.composite(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 0, .5f, 1));
}

TEST(PlaybackSubtitles, SizeAndColorAffectDecodedSurfaceAndSurviveSettingsCopy)
{
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    ptv::Model model("/tmp/prosperotv-subtitle-style-test");
    ptv::PlayRequest request;
    ptv::Settings settings;
    settings.subtitle_size = 2;
    settings.subtitle_color = 1;
    settings.picture_mode = 6;
    ptv::PlaybackOsd osd(model, request, settings);
    ASSERT_TRUE(osd.load_fonts(std::getenv("KIT_FONTS")));
    auto cue = std::make_shared<iptv::SubtitleCue>();
    cue->text = "Yellow captions";
    std::vector<std::shared_ptr<const iptv::SubtitleCue>> cues{cue};
    std::vector<std::uint8_t> frame(640 * 360 * 3 / 2, 128);
    ASSERT_TRUE(osd.draw(frame.data(), frame.size(), 640, 360, 640, 360, 8, 1, cues));
    EXPECT_GT(std::count_if(frame.begin() + 640 * 360, frame.end(), [](auto v) { return v < 100; }),
              10);
    ptv::Settings restored;
    ASSERT_TRUE(osd.copy_picture_settings(restored));
    EXPECT_EQ(restored.subtitle_size, 2);
    EXPECT_EQ(restored.subtitle_color, 1);
    EXPECT_EQ(restored.picture_mode, 6);
    std::vector<std::uint16_t> hdr(frame.size(), 512);
    ASSERT_TRUE(osd.draw(hdr.data(), hdr.size() * 2, 640, 360, 640, 360, 10, 2, cues, 1));
    EXPECT_GT(std::count_if(hdr.begin() + 640 * 360, hdr.end(), [](auto v) { return v < 450; }),
              10);
}
