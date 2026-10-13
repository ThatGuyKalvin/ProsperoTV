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
        std::string(std::getenv("KIT_FONTS")) + "/inter-regular.huifont", &data, 64u << 20));
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

TEST(PlaybackHud, TimelineAdvancesAndSpeedRemainsVisibleAfterControlsHide)
{
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    ptv::Model model("/tmp/prosperotv-hud-test");
    ptv::PlayRequest request;
    request.live = false;
    request.record_channel_result = false;
    ptv::PlaybackOsd osd(model, request);
    ASSERT_TRUE(osd.load_fonts(std::getenv("KIT_FONTS")));
    iptv_player_media_state_t media{};
    media.available = media.seekable = 1;
    media.duration_us = 7200000000ULL;
    media.position_us = 1800000000ULL;
    media.speed_percent = 100;
    osd.set_media_state(media);
    std::vector<std::uint8_t> frame(1920 * 1080 * 3 / 2, 99);
    // The controls are painted on the control thread once a frame has asked for them.
    const auto shown = [&](std::uint64_t now)
    {
        EXPECT_TRUE(osd.draw(nullptr, 0, 1920, 1080, 1920, 1080, 8, now));
        osd.input(-1, now);
        std::fill(frame.begin(), frame.end(), 99);
        return osd.draw(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, now);
    };
    // The timeline's bar crosses the middle of the screen near the bottom.
    const std::size_t bar = 930 * 1920 + 1000;
    ASSERT_TRUE(shown(1));
    const auto quarter = frame[bar];
    EXPECT_NE(quarter, 99);
    EXPECT_EQ(frame[0], 99);
    media.position_us = 5400000000ULL;
    osd.set_media_state(media);
    ASSERT_TRUE(shown(2));
    EXPECT_NE(frame[bar], quarter); // the accent has passed the middle
    EXPECT_FALSE(osd.draw(nullptr, 0, 1920, 1080, 1920, 1080, 8, 6000001));
    media.speed_percent = 200;
    osd.set_media_state(media);
    osd.input(-1, 6000002);
    std::fill(frame.begin(), frame.end(), 99);
    ASSERT_TRUE(osd.draw(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 6000002));
    EXPECT_NE(frame[76 * 1920 + 1800], 99); // the speed alone, at the top right
    EXPECT_EQ(frame[bar], 99);
    media.speed_percent = 100;
    osd.set_media_state(media);
    EXPECT_FALSE(osd.draw(nullptr, 0, 1920, 1080, 1920, 1080, 8, 6000003));
    EXPECT_EQ(osd.input(IPTV_INPUT_TRIANGLE, 6000004), 1);
    EXPECT_TRUE(osd.draw(nullptr, 0, 1920, 1080, 1920, 1080, 8, 6000005));
    std::fill(frame.begin(), frame.end(), 99);
    ASSERT_TRUE(
        osd.draw(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 6000006, {}, 0, true));
    osd.input(-1, 6000006);
    std::fill(frame.begin(), frame.end(), 99);
    ASSERT_TRUE(
        osd.draw(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 6000006, {}, 0, true));
    const auto buffering = frame;
    std::fill(frame.begin(), frame.end(), 99);
    ASSERT_TRUE(
        osd.draw(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 12000000, {}, 0, true));
    EXPECT_EQ(frame, buffering);
    ASSERT_TRUE(shown(12000001));
    EXPECT_NE(frame, buffering);
    EXPECT_EQ(frame[0], 99);
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
    // Integrate blue chroma rather than counting an exact antialiased glyph edge.
    unsigned yellow_chroma = 0, red_chroma = 0;
    for (std::size_t i = 640 * 360; i < hdr.size(); i += 2)
    {
        yellow_chroma += 512 - std::min<unsigned>(512, hdr[i]);
        red_chroma += std::max<unsigned>(512, hdr[i + 1]) - 512;
    }
    EXPECT_GT(yellow_chroma, 512u);
    EXPECT_GT(red_chroma, 16u);
}

TEST(PlaybackMenu, DrawsInColourBesideThePictureAndInsideTheCrop)
{
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    ptv::Model model("/tmp/prosperotv-playback-menu-test");
    ptv::PlayRequest request;
    ptv::PlaybackOsd osd(model, request);
    ASSERT_TRUE(osd.load_fonts(std::getenv("KIT_FONTS")));
    ASSERT_EQ(osd.input(IPTV_INPUT_OPTIONS, 1), 1);
    // A dark red panel at the right: chroma leaves grey; the left of the picture is untouched.
    std::vector<std::uint8_t> frame(1920 * 1080 * 3 / 2, 128);
    ASSERT_TRUE(osd.draw(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 2));
    const auto *chroma = frame.data() + 1920 * 1080;
    unsigned red = 0;
    for (unsigned y = 100; y < 500; ++y)
        for (unsigned x = 1300; x < 1860; x += 2)
            red += chroma[y * 1920 + x + 1] > 136;
    EXPECT_GT(red, 1000u);
    for (unsigned y = 0; y < 1080; y += 20)
        EXPECT_EQ(frame[y * 1920 + 600], 128) << y;
    // Main10, SDR and HDR: drawn in colour too.
    for (const unsigned hdr : {0u, 1u})
    {
        std::vector<std::uint16_t> main10(1920 * 1080 * 3 / 2, 512);
        ASSERT_TRUE(
            osd.draw(main10.data(), main10.size() * 2, 1920, 1080, 1920, 1080, 10, 3, {}, hdr));
        EXPECT_GT(std::count_if(main10.begin() + 1920 * 1080, main10.end(),
                                [&](auto v) { return hdr ? v != 512 : v > 540; }),
                  1000)
            << hdr;
    }
    // Zoomed 150%, the panel is drawn smaller, inside the part of the picture on screen.
    ptv::Settings zoomed;
    zoomed.picture_mode = 6;
    ptv::PlaybackOsd zoom(model, request, zoomed);
    ASSERT_TRUE(zoom.load_fonts(std::getenv("KIT_FONTS")));
    ASSERT_EQ(zoom.input(IPTV_INPUT_OPTIONS, 1), 1);
    std::fill(frame.begin(), frame.end(), 128);
    ASSERT_TRUE(zoom.draw(frame.data(), frame.size(), 1920, 1080, 1920, 1080, 8, 2));
    for (unsigned y = 0; y < 1080; y += 4)
        EXPECT_EQ(frame[y * 1920 + 1915], 128) << y;
    for (unsigned x = 0; x < 1920; x += 4)
        EXPECT_EQ(frame[1075 * 1920 + x], 128) << x;
}

TEST(PlaybackScreen, ControlsAreDrawnAtTheScreensResolutionWhateverThePicture)
{
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    ptv::Model model("/tmp/prosperotv-playback-screen-test");
    ptv::PlayRequest request;
    request.live = false;
    request.record_channel_result = false;
    ptv::PlaybackOsd osd(model, request);
    ASSERT_TRUE(osd.load_fonts(std::getenv("KIT_FONTS")));
    iptv_player_media_state_t media{};
    media.available = media.seekable = 1;
    media.duration_us = 7200000000ULL;
    media.position_us = 1800000000ULL;
    media.speed_percent = 100;
    osd.set_media_state(media);
    // The screen's planes: 1920 x 1080 (1088 rows), colour then coverage, transparent.
    const std::size_t plane = 1920 * 1088 * 3 / 2;
    std::vector<std::uint8_t> color(plane), mask(plane);
    const auto clear = [&]
    {
        for (auto *planes : {&color, &mask})
        {
            std::fill(planes->begin(), planes->begin() + 1920 * 1088, 16);
            std::fill(planes->begin() + 1920 * 1088, planes->end(), 128);
        }
    };
    clear();
    const auto screen = [&]
    { return osd.draw_screen(color.data(), mask.data(), plane, 1920, 1088, 1920, 1080, 8, 0); };
    EXPECT_EQ(screen() & 1, 0); // nothing yet: the controls are painted once a frame asks for them
    // A small picture: nothing of the controls goes into it.
    std::vector<std::uint8_t> picture(720 * 576 * 3 / 2, 99);
    EXPECT_FALSE(osd.draw(nullptr, 0, 720, 576, 720, 576, 8, 1));
    osd.input(-1, 1);
    EXPECT_FALSE(osd.draw(picture.data(), picture.size(), 720, 576, 720, 576, 8, 1));
    EXPECT_TRUE(std::all_of(picture.begin(), picture.end(), [](auto v) { return v == 99; }));
    // On the planes instead, at full size: the timeline's bar crosses the middle near the bottom.
    EXPECT_EQ(screen(), 3);
    EXPECT_GT(mask[930 * 1920 + 1000], 200); // covered
    EXPECT_NE(color[930 * 1920 + 1000], 16);
    EXPECT_EQ(mask[100 * 1920 + 1000], 16); // the picture shows through above
    EXPECT_EQ(screen(), 1);                 // unchanged: nothing to redraw
    // The menu takes the controls' place.
    ASSERT_EQ(osd.input(IPTV_INPUT_OPTIONS, 2), 1);
    EXPECT_FALSE(osd.draw(nullptr, 0, 720, 576, 720, 576, 8, 2));
    osd.input(-1, 2);
    EXPECT_EQ(screen(), 3);
    EXPECT_EQ(mask[930 * 1920 + 300], 16);   // the controls are gone
    EXPECT_GT(mask[600 * 1920 + 1500], 200); // the menu at the right
    // Closed, and the banner over: the planes are transparent again.
    ASSERT_EQ(osd.input(IPTV_INPUT_CIRCLE, 3), 1);
    EXPECT_FALSE(osd.draw(nullptr, 0, 720, 576, 720, 576, 8, 60000000));
    osd.input(-1, 60000000);
    EXPECT_EQ(screen(), 2);
    EXPECT_TRUE(
        std::all_of(mask.begin(), mask.begin() + 1920 * 1080, [](auto v) { return v == 16; }));
    // After the presenter clears them, they are drawn again.
    EXPECT_EQ(osd.input(IPTV_INPUT_TRIANGLE, 60000001), 1);
    EXPECT_FALSE(osd.draw(nullptr, 0, 720, 576, 720, 576, 8, 60000002));
    osd.input(-1, 60000002);
    EXPECT_EQ(screen(), 3);
    clear();
    EXPECT_EQ(osd.draw_screen(color.data(), mask.data(), plane, 1920, 1088, 1920, 1080, 8, 0, true),
              3);
    EXPECT_GT(mask[930 * 1920 + 1000], 200);
}

TEST(PlaybackScreen, ScreensaverHidesPlanesAndWakeRestoresControls)
{
    ASSERT_NE(std::getenv("KIT_FONTS"), nullptr);
    ptv::Model model("/tmp/prosperotv-screen-sleep-test");
    ptv::PlayRequest request;
    request.record_channel_result = false;
    ptv::Settings settings;
    settings.screensaver_minutes = 1;
    ptv::PlaybackOsd osd(model, request, settings);
    ASSERT_TRUE(osd.load_fonts(std::getenv("KIT_FONTS")));
    iptv_player_media_state_t media{};
    media.available = media.paused = 1;
    media.duration_us = 100000000;
    osd.set_media_state(media);
    osd.set_paused(true);
    const std::size_t plane = 1920 * 1088 * 3 / 2;
    std::vector<std::uint8_t> color(plane, 16), mask(plane, 16);
    const auto screen = [&]
    { return osd.draw_screen(color.data(), mask.data(), plane, 1920, 1088, 1920, 1080, 8, 0); };
    screen();
    osd.draw(nullptr, 0, 720, 576, 720, 576, 8, 1);
    osd.input(-1, 1);
    ASSERT_EQ(screen() & 1, 1);
    ASSERT_TRUE(osd.draw(nullptr, 0, 720, 576, 720, 576, 8, 60000001));
    osd.input(-1, 60000001);
    EXPECT_EQ(screen() & 1, 0);
    EXPECT_TRUE(std::all_of(mask.begin(), mask.begin() + 1920 * 1080,
                            [](auto value) { return value == 16; }));
    EXPECT_EQ(osd.input(IPTV_INPUT_TRIANGLE, 60000002), 1);
    osd.draw(nullptr, 0, 720, 576, 720, 576, 8, 60000002);
    osd.input(-1, 60000002);
    EXPECT_EQ(screen() & 1, 1);
}
