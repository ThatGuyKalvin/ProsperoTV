/* ProsperoTV - native PS5 IPTV client derived from ps5-native-app-boilerplate.
 * Copyright (C) 2026 BlackBearReloaded
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "iptv_osd.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{

// A transparent screen overlay: premultiplied colour, then coverage, each a 4:2:0 surface.
struct Frame
{
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch;
    std::uint32_t component_bytes;
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> mask;

    Frame(std::uint32_t w, std::uint32_t h, std::uint32_t component)
        : width(w), height(h), pitch(w + 64u), component_bytes(component),
          bytes(static_cast<std::size_t>(pitch) * (h + (h + 1u) / 2u) * component),
          mask(bytes.size())
    {
        const iptv_osd_surface_t surface = Surface();
        iptv_osd_clear(&surface, 0, height);
    }

    std::size_t samples() const
    {
        return bytes.size() / component_bytes;
    }
    std::uint32_t get(const std::vector<std::uint8_t> &plane, std::size_t index) const
    {
        if (component_bytes == 2u)
        {
            std::uint16_t value;
            std::memcpy(&value, plane.data() + index * 2u, 2u);
            return value;
        }
        return static_cast<std::uint32_t>(plane[index]) << 2;
    }
    std::uint32_t get(std::size_t index) const
    {
        return get(bytes, index);
    }
    std::uint32_t Luma(std::uint32_t x, std::uint32_t y) const
    {
        return get(bytes, static_cast<std::size_t>(y) * pitch + x);
    }
    std::uint32_t Cover(std::uint32_t x, std::uint32_t y) const
    {
        return get(mask, static_cast<std::size_t>(y) * pitch + x);
    }
    std::uint32_t ChromaU(std::uint32_t x, std::uint32_t y) const
    {
        return get(bytes,
                   static_cast<std::size_t>(pitch) * height + (y / 2u) * pitch + (x / 2u) * 2u);
    }
    iptv_osd_surface_t Surface()
    {
        return {bytes.data(), bytes.size(),    pitch, height, width,
                height,       component_bytes, 0,     0,      mask.data()};
    }
};

iptv_osd_state_t MediaState()
{
    iptv_osd_state_t state{};
    state.kind = IPTV_OSD_MEDIA;
    state.position_us = INT64_C(754) * 1000000;
    state.duration_us = INT64_C(3600) * 1000000;
    std::snprintf(state.title, sizeof(state.title), "Burn Notice");
    std::snprintf(state.subtitle, sizeof(state.subtitle), "S01 E01  Pilot");
    std::snprintf(state.detail, sizeof(state.detail), "ENG  AC-3 5.1");
    std::snprintf(state.clock, sizeof(state.clock), "Ends 21:43");
    state.buttons = IPTV_OSD_BUTTON_PAUSE | IPTV_OSD_BUTTON_SEEK | IPTV_OSD_BUTTON_BACK;
    return state;
}

TEST(IptvOsdTest, DrawsTheMediaBarOnlyAtTheBottom)
{
    for (const std::uint32_t component : {1u, 2u})
    {
        Frame frame(1920, 1080, component);
        iptv_osd_surface_t surface = frame.Surface();
        iptv_osd_state_t state = MediaState();
        std::uint32_t first = 0;
        std::uint32_t rows = 0;
        ASSERT_EQ(iptv_osd_draw(&surface, &state, &first, &rows), 0);
        EXPECT_EQ(first % 2u, 0u);
        EXPECT_EQ(first + rows, 1080u);
        EXPECT_GT(first, 600u);

        // Nothing above the controls; below, the overlay covers more towards the bottom.
        EXPECT_EQ(frame.Cover(10, first - 2u), 64u);
        EXPECT_EQ(frame.Luma(10, first - 2u), 64u);
        EXPECT_EQ(frame.ChromaU(10, first - 2u), 512u);
        EXPECT_GT(frame.Cover(10, first + 10u), 64u);
        EXPECT_LT(frame.Cover(10, first + 10u), frame.Cover(10, 1079));
        EXPECT_GT(frame.Cover(10, 1079), 780u);
        // The backdrop is dark: its premultiplied colour stays near black.
        EXPECT_LT(frame.Luma(10, 1079), 100u);
        // The elapsed part of the timeline is orange: blue-difference chroma well below grey.
        const std::uint32_t bar_y = 1080u - 150u;
        EXPECT_LT(frame.ChromaU(300, bar_y), 400u);
        // The rest of the timeline is the neutral track colour.
        EXPECT_GT(frame.ChromaU(1500, bar_y), 480u);
        // Every 10-bit value stays within the representable range.
        for (std::size_t index = 0; index < frame.samples(); ++index)
            ASSERT_LE(frame.get(index), 1023u);
    }
}

TEST(IptvOsdTest, DrawsTheLiveBannerAndScalesWithTheFrame)
{
    Frame frame(3840, 2160, 2);
    iptv_osd_surface_t surface = frame.Surface();
    iptv_osd_state_t state{};
    state.kind = IPTV_OSD_LIVE;
    std::snprintf(state.title, sizeof(state.title), "BBC One HD");
    std::snprintf(state.detail, sizeof(state.detail), "News at Six");
    std::snprintf(state.next, sizeof(state.next), "Next  18:30  Regional News");
    std::snprintf(state.clock, sizeof(state.clock), "18:12");
    std::snprintf(state.start_label, sizeof(state.start_label), "18:00");
    std::snprintf(state.end_label, sizeof(state.end_label), "18:30");
    state.position_us = INT64_C(720) * 1000000;
    state.duration_us = INT64_C(1800) * 1000000;
    std::uint32_t first = 0;
    std::uint32_t rows = 0;
    ASSERT_EQ(iptv_osd_draw(&surface, &state, &first, &rows), 0);
    EXPECT_GT(rows, 760u); // twice the height of the 1080p controls
    EXPECT_EQ(frame.Cover(100, first - 2u), 64u);
    // The channel name is bright text.
    std::uint32_t brightest = 0;
    for (std::uint32_t x = 400; x < 1400; ++x)
        brightest = std::max(brightest, frame.Luma(x, 2160u - 552u));
    EXPECT_GT(brightest, 800u);
}

TEST(IptvOsdTest, ClearsAndDrawsTheStatisticsLine)
{
    Frame frame(1920, 1080, 1);
    iptv_osd_surface_t surface = frame.Surface();
    std::uint32_t first = 0;
    std::uint32_t rows = 0;
    ASSERT_EQ(iptv_osd_draw_stats(&surface, "HEVC 10-bit  1920x800  23.98 fps  14.20 Mbps", &first,
                                  &rows),
              0);
    EXPECT_LT(first + rows, 120u);
    EXPECT_GT(frame.Cover(60, 50), 600u);
    EXPECT_EQ(frame.Cover(60, 200), 64u);
    iptv_osd_clear(&surface, first, rows);
    for (std::uint32_t y = first; y < first + rows; ++y)
        ASSERT_EQ(frame.Cover(60, y), 64u);
    EXPECT_EQ(frame.ChromaU(60, 50), 512u);
}

TEST(IptvOsdTest, RefusesSurfacesItCannotDrawOn)
{
    Frame frame(1920, 1080, 1);
    iptv_osd_surface_t surface = frame.Surface();
    iptv_osd_state_t state = MediaState();
    surface.bytes -= 1u;
    EXPECT_EQ(iptv_osd_draw(&surface, &state, nullptr, nullptr), -1);
    surface = frame.Surface();
    state.kind = IPTV_OSD_HIDDEN;
    EXPECT_EQ(iptv_osd_draw(&surface, &state, nullptr, nullptr), -1);
    surface = frame.Surface();
    surface.mask = nullptr;
    EXPECT_EQ(iptv_osd_draw(&surface, &state, nullptr, nullptr), -1);
    Frame tiny(160, 90, 1);
    surface = tiny.Surface();
    state.kind = IPTV_OSD_MEDIA;
    EXPECT_EQ(iptv_osd_draw(&surface, &state, nullptr, nullptr), -1);
}

TEST(IptvOsdTest, FormatsTimesAndPublishesStates)
{
    char text[24];
    iptv_osd_format_time(INT64_C(754) * 1000000 + 999999, text, sizeof(text));
    EXPECT_STREQ(text, "12:34");
    iptv_osd_format_time(INT64_C(3723) * 1000000, text, sizeof(text));
    EXPECT_STREQ(text, "1:02:03");
    iptv_osd_format_time(-1, text, sizeof(text));
    EXPECT_STREQ(text, "--:--");

    iptv_osd_state_t state = MediaState();
    const std::uint32_t before = iptv_osd_snapshot(nullptr);
    iptv_osd_publish(&state);
    iptv_osd_state_t copy{};
    const std::uint32_t after = iptv_osd_snapshot(&copy);
    EXPECT_NE(after, before);
    EXPECT_STREQ(copy.title, state.title);
    EXPECT_EQ(copy.position_us, state.position_us);
    iptv_osd_hide();
    EXPECT_EQ(iptv_osd_snapshot(&copy), after + 2u);
    EXPECT_EQ(copy.kind, static_cast<std::uint32_t>(IPTV_OSD_HIDDEN));
}

TEST(IptvOsdTest, DrawsTheControlsInsideTheVisiblePart)
{
    // Drawn in an area: columns 240..1680 and rows 134..944.
    Frame frame(1920, 1080, 1);
    iptv_osd_surface_t surface = frame.Surface();
    surface.x = 240u;
    surface.y = 134u;
    surface.width = 1440u;
    surface.height = 810u;
    iptv_osd_state_t state = MediaState();
    std::uint32_t first = 0;
    std::uint32_t rows = 0;
    ASSERT_EQ(iptv_osd_draw(&surface, &state, &first, &rows), 0);
    EXPECT_GE(first, 134u);
    EXPECT_EQ(first + rows, 944u);
    // Nothing outside the area changes.
    for (std::uint32_t y = 0; y < 1080u; y += 7u)
    {
        ASSERT_EQ(frame.Cover(100, y), 64u) << y;
        ASSERT_EQ(frame.Cover(1800, y), 64u) << y;
    }
    for (std::uint32_t x = 0; x < 1920u; x += 5u)
    {
        ASSERT_EQ(frame.Cover(x, 100), 64u) << x;
        ASSERT_EQ(frame.Cover(x, 1000), 64u) << x;
    }
    // The bar covers the bottom of the area.
    EXPECT_GT(frame.Cover(960, 944u - 40u), 300u);
}

} // namespace
