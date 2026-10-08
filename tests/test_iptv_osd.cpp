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

void FillMenu(iptv_osd_state_t *state)
{
    const char *labels[3] = {"Aspect ratio", "Zoom", "Audio delay"};
    const char *values[3] = {"Auto", "Fill screen", "+100 ms"};
    const std::uint32_t kinds[3] = {IPTV_OSD_ROW_PICKER, IPTV_OSD_ROW_PICKER, IPTV_OSD_ROW_STEPPER};
    for (int row = 0; row < 3; ++row)
    {
        state->menu[row].kind = kinds[row];
        std::snprintf(state->menu[row].label, sizeof(state->menu[row].label), "%s", labels[row]);
        std::snprintf(state->menu[row].value, sizeof(state->menu[row].value), "%s", values[row]);
    }
}

// A painter that fills a rectangle at the image's right with opaque white.
int PaintWhite(void *context, const iptv_osd_state_t *state, iptv_osd_image_t *image)
{
    int *calls = static_cast<int *>(context);
    ++*calls;
    if (!state->menu_rows)
        return -1;
    image->x = 400u;
    image->y = 200u;
    image->width = 300u;
    image->height = 400u;
    for (std::uint32_t y = image->y; y < image->y + image->height; ++y)
        std::memset(image->rgba +
                        (static_cast<std::size_t>(y) * IPTV_OSD_MENU_IMAGE_WIDTH + image->x) * 4u,
                    255, image->width * 4u);
    return 0;
}

int PaintNothing(void *, const iptv_osd_state_t *, iptv_osd_image_t *)
{
    return -1;
}

TEST(IptvOsdTest, DrawsAPaintedMenuOverTheOverlay)
{
    int calls = 0;
    iptv_osd_set_menu_painter(PaintWhite, &calls);
    iptv_osd_state_t state = MediaState();
    state.menu_rows = 3u;
    FillMenu(&state);
    iptv_osd_publish(&state);
    EXPECT_EQ(calls, 1);
    // The same menu again is not painted again.
    iptv_osd_publish(&state);
    EXPECT_EQ(calls, 1);

    Frame frame(1920, 1080, 1);
    const iptv_osd_surface_t surface = frame.Surface();
    std::uint32_t first = 0;
    std::uint32_t rows = 0;
    ASSERT_EQ(iptv_osd_draw(&surface, &state, &first, &rows), 0);
    EXPECT_LE(first, 200u);
    // The image covers the right of the overlay: its column 400 is overlay column 1520.
    EXPECT_EQ(frame.Cover(1520 + 10, 300), 940u);
    EXPECT_EQ(frame.Cover(1520 - 10, 300), 64u);
    EXPECT_EQ(iptv_osd_reserved_columns(&surface, &state), 440u);

    // A painter that cannot paint leaves the menu to the built-in drawing.
    iptv_osd_set_menu_painter(PaintNothing, nullptr);
    state.menu_selected = 1u;
    iptv_osd_publish(&state);
    Frame plain(1920, 1080, 1);
    const iptv_osd_surface_t plain_surface = plain.Surface();
    ASSERT_EQ(iptv_osd_draw(&plain_surface, &state, &first, &rows), 0);
    EXPECT_GT(iptv_osd_reserved_columns(&plain_surface, &state), 600u);
    iptv_osd_set_menu_painter(nullptr, nullptr);
    iptv_osd_hide();
}

TEST(IptvOsdTest, DrawsTheSettingsMenuInsideTheVisiblePart)
{
    // Drawn in an area: columns 240..1680 and rows 134..944.
    Frame frame(1920, 1080, 1);
    iptv_osd_surface_t surface = frame.Surface();
    surface.x = 240u;
    surface.y = 134u;
    surface.width = 1440u;
    surface.height = 810u;
    iptv_osd_state_t state = MediaState();
    state.buttons |= IPTV_OSD_BUTTON_MENU;
    state.menu_rows = 3u;
    state.menu_selected = 1u;
    FillMenu(&state);
    std::uint32_t first = 0;
    std::uint32_t rows = 0;
    ASSERT_EQ(iptv_osd_draw(&surface, &state, &first, &rows), 0);
    EXPECT_GE(first, 134u);
    EXPECT_EQ(first + rows, 944u);
    // The menu reaches higher than the bar alone does.
    EXPECT_LT(first, 944u - 400u);
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
    // The panel covers the picture at its right.
    EXPECT_GT(frame.Cover(1500, first + 40u), 700u);
    EXPECT_GT(iptv_osd_reserved_rows(&surface, &state), 200u);
    state.kind = IPTV_OSD_HIDDEN;
    EXPECT_EQ(iptv_osd_reserved_rows(&surface, &state), 0u);
}

} // namespace
