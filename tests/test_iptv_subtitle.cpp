/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_subtitle.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

std::string Plain(const char *text, int ass, int *top = nullptr)
{
    char out[512];
    int at_top = 0;
    iptv_subtitle_plain_text(text, ass, out, sizeof(out), &at_top);
    if (top)
        *top = at_top;
    return out;
}

TEST(IptvSubtitleTextTest, ReadsAssEventsAndOverrides)
{
    int top = -1;
    EXPECT_EQ(Plain("0,0,Default,,0,0,0,,{\\i1}Hello{\\i0}\\Nworld", 1, &top),
              "\x01Hello\x02\nworld");
    EXPECT_EQ(top, 0);
    EXPECT_EQ(Plain("3,0,Default,,0,0,0,,{\\an8\\b1}At the top", 1, &top), "At the top");
    EXPECT_EQ(top, 1);
    // Commas inside the text stay.
    EXPECT_EQ(Plain("1,0,Default,,0,0,0,,Yes, sir.", 1), "Yes, sir.");
    // Drawings are not text.
    EXPECT_EQ(Plain("1,0,Default,,0,0,0,,{\\p1}m 0 0 l 10 0{\\p0}Said", 1), "Said");
}

TEST(IptvSubtitleTextTest, CleansPlainText)
{
    EXPECT_EQ(Plain("<i>Whispering</i>", 0), "\x01Whispering\x02");
    EXPECT_EQ(Plain("<font color=\"#ffff00\">Yellow</font>", 0), "Yellow");
    EXPECT_EQ(Plain("  one  \r\n\n\n  two\\hwords ", 0), "one\ntwo words");
    EXPECT_EQ(Plain("a < b", 0), "a < b");
    EXPECT_EQ(Plain("", 0), "");
}

TEST(IptvSubtitleTextTest, DecodesUtf8)
{
    std::uint32_t code = 0;
    EXPECT_EQ(iptv_subtitle_utf8("\xc3\xa9", &code), 2u);
    EXPECT_EQ(code, 0xe9u);
    EXPECT_EQ(iptv_subtitle_utf8("\xe2\x99\xaa", &code), 3u);
    EXPECT_EQ(code, 0x266au);
    EXPECT_EQ(iptv_subtitle_utf8("\xc3(", &code), 1u);
    EXPECT_EQ(code, 0xfffdu);
}

// A transparent 8-bit screen overlay: premultiplied colour, then coverage.
struct Frame
{
    std::uint32_t width, height, pitch;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> mask;
    Frame(std::uint32_t w, std::uint32_t h)
        : width(w), height(h), pitch(w), bytes(static_cast<std::size_t>(w) * h * 3u / 2u, 0),
          mask(bytes.size())
    {
        const iptv_osd_surface_t surface = Surface();
        iptv_osd_clear(&surface, 0, h);
    }
    iptv_osd_surface_t Surface(std::uint32_t x = 0, std::uint32_t y = 0, std::uint32_t w = 0,
                               std::uint32_t h = 0)
    {
        iptv_osd_surface_t surface{};
        surface.data = bytes.data();
        surface.bytes = bytes.size();
        surface.pitch = pitch;
        surface.surface_height = height;
        surface.width = w ? w : width;
        surface.height = h ? h : height;
        surface.component_bytes = 1;
        surface.x = x;
        surface.y = y;
        surface.mask = mask.data();
        return surface;
    }
    int Brightest(std::uint32_t row) const
    {
        int best = 0;
        for (std::uint32_t x = 0; x < width; ++x)
            best = std::max<int>(best, bytes[static_cast<std::size_t>(row) * pitch + x]);
        return best;
    }
    int Covered(std::uint32_t row) const
    {
        int best = 0;
        for (std::uint32_t x = 0; x < width; ++x)
            best = std::max<int>(best, mask[static_cast<std::size_t>(row) * pitch + x]);
        return best;
    }
    std::uint8_t V(std::uint32_t x, std::uint32_t y) const
    {
        return bytes[static_cast<std::size_t>(pitch) * height + (y / 2u) * pitch + (x / 2u) * 2u +
                     1u];
    }
};

class IptvSubtitleCueTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        iptv_subtitle_reset();
    }
    void TearDown() override
    {
        iptv_subtitle_reset();
    }
};

TEST_F(IptvSubtitleCueTest, ShowsATextCueOnlyWhileItIsDue)
{
    iptv_subtitle_add_text(1000000, 3000000, "Hello there", 0);
    Frame frame(1920u, 1080u);
    auto surface = frame.Surface();
    // Not rendered yet: nothing to draw.
    EXPECT_EQ(iptv_subtitle_signature(2000000), 0u);
    EXPECT_EQ(iptv_subtitle_draw(&surface, 2000000, nullptr, 0, nullptr, nullptr), -1);
    iptv_subtitle_prepare(1920u, 1080u, 1000000);
    std::uint32_t cues = 0, rendered = 0;
    iptv_subtitle_counts(&cues, &rendered);
    EXPECT_EQ(cues, 1u);
    EXPECT_EQ(rendered, 1u);
    EXPECT_NE(iptv_subtitle_signature(2000000), 0u);
    EXPECT_EQ(iptv_subtitle_signature(2000000), iptv_subtitle_signature(2500000));
    EXPECT_EQ(iptv_subtitle_signature(3000000), 0u);

    EXPECT_EQ(iptv_subtitle_draw(&surface, 500000, nullptr, 0, nullptr, nullptr), -1);
    EXPECT_EQ(iptv_subtitle_draw(&surface, 3000000, nullptr, 0, nullptr, nullptr), -1);
    std::uint32_t first = 0, rows = 0;
    ASSERT_EQ(iptv_subtitle_draw(&surface, 2000000, nullptr, 0, &first, &rows), 0);
    // Near the bottom, with white letters over a dark outline.
    EXPECT_GT(first, 900u);
    EXPECT_LE(first + rows, 1080u);
    int brightest = 0, covered = 0;
    for (std::uint32_t row = first; row < first + rows; ++row)
    {
        brightest = std::max(brightest, frame.Brightest(row));
        covered = std::max(covered, frame.Covered(row));
    }
    EXPECT_GT(brightest, 200);
    EXPECT_GT(covered, 230);
    EXPECT_EQ(frame.Covered(500u), 16);

    // Ended cues are forgotten.
    iptv_subtitle_prepare(1920u, 1080u, 10000000);
    iptv_subtitle_counts(&cues, nullptr);
    EXPECT_EQ(cues, 0u);
}

TEST_F(IptvSubtitleCueTest, SitsInThePictureAndAboveTheControls)
{
    iptv_subtitle_add_text(0, 5000000, "Line one\nLine two", 0);
    iptv_subtitle_prepare(1920u, 1080u, 0);
    Frame frame(1920u, 1080u);
    auto surface = frame.Surface();
    std::uint32_t low = 0, high = 0, rows = 0;
    ASSERT_EQ(iptv_subtitle_draw(&surface, 1000000, nullptr, 0, &low, &rows), 0);
    Frame second(1920u, 1080u);
    surface = second.Surface();
    ASSERT_EQ(iptv_subtitle_draw(&surface, 1000000, nullptr, 300u, &high, &rows), 0);
    EXPECT_LT(high + 250u, low);
    EXPECT_LE(high + rows, 1080u - 300u);

    // A letterboxed picture (rows 140..940 of the screen): inside it.
    Frame boxed(1920u, 1080u);
    surface = boxed.Surface();
    const std::int32_t picture[4] = {0, 140, 1920, 800};
    std::uint32_t first = 0;
    ASSERT_EQ(iptv_subtitle_draw(&surface, 1000000, picture, 0, &first, &rows), 0);
    EXPECT_GE(first, 140u);
    EXPECT_LE(first + rows, 940u);

    // A zoomed picture reaching past the screen: inside the screen.
    Frame zoomed(1920u, 1080u);
    surface = zoomed.Surface();
    const std::int32_t large[4] = {-320, -180, 2560, 1440};
    ASSERT_EQ(iptv_subtitle_draw(&surface, 1000000, large, 0, &first, &rows), 0);
    EXPECT_LE(first + rows, 1080u);
    EXPECT_GT(first, 850u);
}

TEST_F(IptvSubtitleCueTest, PlacesPicturesWhereTheirCanvasPutsThemUntilCleared)
{
    // A Blu-ray subtitle on its 1920x1080 canvas: the same place on the screen.
    std::vector<std::uint32_t> red(200u * 40u, 0xffff0000u);
    iptv_subtitle_rect_t rect{860, 960, 200u, 40u, red.data()};
    iptv_subtitle_add_bitmaps(1000000, -1, 1920u, 1080u, &rect, 1u);
    iptv_subtitle_add_bitmaps(4000000, -1, 1920u, 1080u, nullptr, 0u);
    iptv_subtitle_prepare(1920u, 1080u, 1000000);

    Frame frame(1920u, 1080u);
    auto surface = frame.Surface();
    const std::int32_t picture[4] = {0, 140, 1920, 800};
    std::uint32_t first = 0, rows = 0;
    ASSERT_EQ(iptv_subtitle_draw(&surface, 2000000, picture, 0, &first, &rows), 0);
    EXPECT_EQ(first, 960u);
    EXPECT_EQ(rows, 40u);
    EXPECT_GT(frame.V(960u, 980u), 200u); // red
    // With the controls up it moves above them.
    Frame raised(1920u, 1080u);
    surface = raised.Surface();
    ASSERT_EQ(iptv_subtitle_draw(&surface, 2000000, picture, 300u, &first, &rows), 0);
    EXPECT_LE(first + rows, 1080u - 300u);
    // The empty set at 4 s ended it.
    Frame later(1920u, 1080u);
    surface = later.Surface();
    EXPECT_EQ(iptv_subtitle_draw(&surface, 4500000, picture, 0, nullptr, nullptr), -1);
}

TEST_F(IptvSubtitleCueTest, AnOpenTextCueEndsWhenTheNextStarts)
{
    iptv_subtitle_add_text(0, -1, "First", 0);
    iptv_subtitle_add_text(2000000, 4000000, "Second", 0);
    iptv_subtitle_prepare(1920u, 1080u, 0);
    iptv_subtitle_prepare(1920u, 1080u, 0);
    Frame frame(1920u, 1080u);
    auto surface = frame.Surface();
    std::uint32_t first = 0, rows = 0;
    ASSERT_EQ(iptv_subtitle_draw(&surface, 1000000, nullptr, 0, &first, &rows), 0);
    const std::uint32_t one_line = rows;
    ASSERT_EQ(iptv_subtitle_draw(&surface, 3000000, nullptr, 0, &first, &rows), 0);
    // Only "Second" shows at 3 s: one line, not two stacked.
    EXPECT_EQ(rows, one_line);
}

} // namespace
