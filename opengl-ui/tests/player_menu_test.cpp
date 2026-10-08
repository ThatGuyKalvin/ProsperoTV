// ProsperoTV - Tests: the player's settings menu, drawn by the CPU.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/player_menu.hpp"
#include "tv/soft_raster.hpp"

#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb/stb_image_write.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace
{

struct MenuFonts
{
    hui::gfx::Font regular, semibold, display, mono;
    hui::ui::Fonts fonts;

    MenuFonts()
    {
        const char *dir = std::getenv("KIT_FONTS");
        load(dir, "inter-regular.huifont", &regular, &fonts.regular, 0xf0000001u);
        load(dir, "inter-semibold.huifont", &semibold, &fonts.semibold, 0xf0000002u);
        load(dir, "montserrat-medium.huifont", &display, &fonts.display, 0xf0000003u);
        load(dir, "dejavu-sans-mono.huifont", &mono, &fonts.mono, 0xf0000004u);
    }

    static void load(const char *dir, const char *name, hui::gfx::Font *font, hui::ui::FontRef *ref,
                     std::uint32_t handle)
    {
        std::string data;
        const std::string path = std::string(dir != nullptr ? dir : ".") + "/" + name;
        if (!hui::save::read_file(path, &data, 64u << 20) || !font->load(data))
        {
            ADD_FAILURE() << "cannot load " << path;
            return;
        }
        ref->font = font;
        ref->texture = handle;
    }
};

const MenuFonts &menu_fonts()
{
    static const MenuFonts fonts;
    return fonts;
}

void add_row(iptv_osd_state_t *state, std::uint32_t kind, const char *label, const char *value,
             std::uint32_t checked = 0, std::uint32_t preview = IPTV_OSD_PREVIEW_NONE,
             std::uint32_t milli = 0)
{
    iptv_osd_menu_row_t &row = state->menu[state->menu_rows++];
    row.kind = kind;
    row.checked = checked;
    row.preview = preview;
    row.preview_milli = milli;
    std::snprintf(row.label, sizeof(row.label), "%s", label);
    std::snprintf(row.value, sizeof(row.value), "%s", value);
}

iptv_osd_state_t settings_page()
{
    iptv_osd_state_t state{};
    state.kind = IPTV_OSD_MEDIA;
    std::snprintf(state.menu_title, sizeof(state.menu_title), "Playback");
    add_row(&state, IPTV_OSD_ROW_HEADER, "Sound", "");
    add_row(&state, IPTV_OSD_ROW_PICKER, "Audio", "English 5.1");
    add_row(&state, IPTV_OSD_ROW_PICKER, "Subtitles", "Off");
    add_row(&state, IPTV_OSD_ROW_STEPPER, "Audio delay", "+150 ms");
    add_row(&state, IPTV_OSD_ROW_HEADER, "Picture", "");
    add_row(&state, IPTV_OSD_ROW_PICKER, "Aspect ratio", "Auto \xC2\xB7 16:9");
    add_row(&state, IPTV_OSD_ROW_PICKER, "Zoom", "Fit");
    state.menu_selected = 5;
    return state;
}

iptv_osd_state_t aspect_page()
{
    iptv_osd_state_t state{};
    state.kind = IPTV_OSD_MEDIA;
    state.menu_page = IPTV_OSD_MENU_PICKER;
    std::snprintf(state.menu_title, sizeof(state.menu_title), "Aspect ratio");
    add_row(&state, IPTV_OSD_ROW_OPTION, "Auto", "16:9", 1, IPTV_OSD_PREVIEW_SHAPE, 1778);
    add_row(&state, IPTV_OSD_ROW_OPTION, "16:9", "", 0, IPTV_OSD_PREVIEW_SHAPE, 1778);
    add_row(&state, IPTV_OSD_ROW_OPTION, "4:3", "", 0, IPTV_OSD_PREVIEW_SHAPE, 1333);
    add_row(&state, IPTV_OSD_ROW_OPTION, "1.85:1", "", 0, IPTV_OSD_PREVIEW_SHAPE, 1850);
    add_row(&state, IPTV_OSD_ROW_OPTION, "2.39:1", "", 0, IPTV_OSD_PREVIEW_SHAPE, 2390);
    add_row(&state, IPTV_OSD_ROW_OPTION, "Stretch", "Fills the screen", 0, IPTV_OSD_PREVIEW_STRETCH,
            0);
    state.menu_selected = 2;
    return state;
}

iptv_osd_state_t zoom_page()
{
    iptv_osd_state_t state{};
    state.kind = IPTV_OSD_MEDIA;
    state.menu_page = IPTV_OSD_MENU_PICKER;
    std::snprintf(state.menu_title, sizeof(state.menu_title), "Zoom");
    add_row(&state, IPTV_OSD_ROW_OPTION, "Fit", "", 1, IPTV_OSD_PREVIEW_ZOOM, 1000);
    add_row(&state, IPTV_OSD_ROW_OPTION, "Zoom 115%", "", 0, IPTV_OSD_PREVIEW_ZOOM, 1150);
    add_row(&state, IPTV_OSD_ROW_OPTION, "Zoom 133%", "", 0, IPTV_OSD_PREVIEW_ZOOM, 1333);
    add_row(&state, IPTV_OSD_ROW_OPTION, "Fill screen", "Crops the edges", 0, IPTV_OSD_PREVIEW_ZOOM,
            0);
    state.menu_selected = 0;
    return state;
}

struct Image
{
    std::vector<std::uint8_t> rgba =
        std::vector<std::uint8_t>(IPTV_OSD_MENU_IMAGE_WIDTH * 4u * IPTV_OSD_OVERLAY_HEIGHT);
    iptv_osd_image_t image{rgba.data(), 0, 0, 0, 0};

    const std::uint8_t *at(std::uint32_t x, std::uint32_t y) const
    {
        return rgba.data() + (static_cast<std::size_t>(y) * IPTV_OSD_MENU_IMAGE_WIDTH + x) * 4u;
    }
};

// With PTV_MENU_PNG set to a folder, the menu over a stand-in picture is written there.
void write_preview(const Image &menu, const char *name)
{
    const char *folder = std::getenv("PTV_MENU_PNG");
    if (folder == nullptr)
        return;
    const int width = static_cast<int>(IPTV_OSD_OVERLAY_WIDTH);
    const int height = static_cast<int>(IPTV_OSD_OVERLAY_HEIGHT);
    std::vector<std::uint8_t> screen(static_cast<std::size_t>(width) * height * 3u);
    const int left = width - static_cast<int>(IPTV_OSD_MENU_IMAGE_WIDTH);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            // A bright, busy picture: the worst case for legibility.
            std::uint8_t *pixel = screen.data() + (static_cast<std::size_t>(y) * width + x) * 3u;
            const bool check = ((x / 60) + (y / 60)) % 2 == 0;
            float rgb[3] = {0.35f + 0.5f * static_cast<float>(x) / width, check ? 0.75f : 0.55f,
                            0.85f - 0.5f * static_cast<float>(y) / height};
            if (x >= left)
            {
                const std::uint8_t *over =
                    menu.at(static_cast<std::uint32_t>(x - left), static_cast<std::uint32_t>(y));
                const float a = over[3] / 255.0f;
                for (int c = 0; c < 3; ++c)
                    rgb[c] = over[c] / 255.0f + rgb[c] * (1.0f - a);
            }
            for (int c = 0; c < 3; ++c)
                pixel[c] = static_cast<std::uint8_t>(std::min(rgb[c], 1.0f) * 255.0f + 0.5f);
        }
    const std::string path = std::string(folder) + "/" + name;
    EXPECT_NE(stbi_write_png(path.c_str(), width, height, 3, screen.data(), width * 3), 0);
}

TEST(PlayerMenu, PaintsTheSettingsPanelAtTheRight)
{
    ptv::PlayerMenu menu(menu_fonts().fonts);
    Image image;
    ASSERT_TRUE(menu.paint(settings_page(), &image.image));
    // Everything is inside the image, above the player's controls.
    EXPECT_GT(image.image.width, 600u);
    EXPECT_LE(image.image.x + image.image.width, IPTV_OSD_MENU_IMAGE_WIDTH);
    EXPECT_LE(image.image.y + image.image.height, 820u);
    // The panel is nearly opaque Dusk; a pixel left of it is untouched.
    const hui::gfx::Rect panel = menu.panel();
    const auto inside =
        image.at(static_cast<std::uint32_t>(panel.x - ptv::PlayerMenu::kImageLeft + 12.0f),
                 static_cast<std::uint32_t>(panel.y + panel.h * 0.5f));
    EXPECT_GT(inside[3], 200u);
    EXPECT_LT(inside[0], 80u);
    EXPECT_EQ(image.at(10, 500)[3], 0u);
    // Its text was drawn: the glyphs of the title and the rows.
    int glyphs = 0;
    for (const hui::gfx::Instance &instance : menu.recorded().instances())
        glyphs +=
            static_cast<int>(instance.params[3] + 0.5f) == static_cast<int>(hui::gfx::Shape::glyph);
    EXPECT_GT(glyphs, 80);
    write_preview(image, "player-menu-settings.png");
}

TEST(PlayerMenu, PaintsTheValuesWithTheirShapes)
{
    ptv::PlayerMenu menu(menu_fonts().fonts);
    Image image;
    ASSERT_TRUE(menu.paint(aspect_page(), &image.image));
    write_preview(image, "player-menu-aspect.png");
    const float aspect_height = menu.panel().h;
    // Painting again clears what was there: a shorter page leaves nothing below it.
    ASSERT_TRUE(menu.paint(zoom_page(), &image.image));
    write_preview(image, "player-menu-zoom.png");
    EXPECT_LT(menu.panel().h, aspect_height);
    const hui::gfx::Rect panel = menu.panel();
    EXPECT_EQ(image.at(static_cast<std::uint32_t>(panel.x - ptv::PlayerMenu::kImageLeft + 40.0f),
                       static_cast<std::uint32_t>(panel.y - 60.0f))[3],
              0u);
}

TEST(PlayerMenu, NothingToPaintWhenClosed)
{
    ptv::PlayerMenu menu(menu_fonts().fonts);
    Image image;
    iptv_osd_state_t state{};
    EXPECT_FALSE(menu.paint(state, &image.image));
}

TEST(SoftRaster, DrawsShapesLikeTheShader)
{
    hui::gfx::DrawList list;
    list.rounded_rect({10.0f, 10.0f, 40.0f, 20.0f}, 6.0f, hui::gfx::Color::rgb(0xff0000));
    list.line(60.0f, 20.0f, 90.0f, 20.0f, 4.0f, hui::gfx::Color::rgb(0x00ff00, 0.5f));
    std::vector<std::uint8_t> pixels(100u * 40u * 4u);
    ptv::SoftRaster raster;
    ptv::SoftRaster::Target target;
    target.rgba = pixels.data();
    target.width = 100;
    target.height = 40;
    target.stride = 400;
    const ptv::SoftRaster::Box box = raster.draw(list, target);
    EXPECT_LE(box.x0, 10);
    EXPECT_GE(box.x1, 90);
    const auto at = [&](int x, int y) { return pixels.data() + (y * 100 + x) * 4; };
    // Inside the rectangle: opaque red.
    EXPECT_EQ(at(30, 20)[0], 255u);
    EXPECT_EQ(at(30, 20)[3], 255u);
    // Its rounded corner leaves the very corner pixel clear.
    EXPECT_EQ(at(10, 10)[3], 0u);
    // The line is half-transparent green, premultiplied.
    EXPECT_NEAR(at(75, 20)[1], 128, 1);
    EXPECT_NEAR(at(75, 20)[3], 128, 1);
    EXPECT_EQ(at(75, 30)[3], 0u);
}

} // namespace
