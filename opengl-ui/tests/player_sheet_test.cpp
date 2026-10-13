// ProsperoTV - Tests: the player's settings menu, drawn by the CPU.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/player_sheet.hpp"
#include "tv/playback_osd.hpp"
#include "tv/soft_raster.hpp"

#include "core/save_file.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
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

using Kind = ptv::SheetRow::Kind;
using Preview = ptv::SheetRow::Preview;

struct MenuFonts
{
    hui::gfx::Font regular, semibold, display;
    hui::ui::Fonts fonts;

    MenuFonts()
    {
        const char *dir = std::getenv("KIT_FONTS");
        load(dir, "inter-regular.huifont", &regular, &fonts.regular, 0xf0000001u);
        load(dir, "inter-semibold.huifont", &semibold, &fonts.semibold, 0xf0000002u);
        load(dir, "montserrat-medium.huifont", &display, &fonts.display, 0xf0000003u);
        fonts.mono = fonts.regular;
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

ptv::SheetRow row(Kind kind, std::string label, std::string value = {}, bool checked = false,
                  Preview preview = Preview::none, unsigned milli = 0)
{
    ptv::SheetRow result;
    result.kind = kind;
    result.label = std::move(label);
    result.value = std::move(value);
    result.checked = checked;
    result.preview = preview;
    result.milli = milli;
    return result;
}

ptv::SheetModel settings_page()
{
    ptv::SheetModel model;
    model.title = "Playback";
    model.rows.push_back(row(Kind::header, "Sound"));
    model.rows.push_back(row(Kind::picker, "Audio", "English (eng)"));
    auto delay = row(Kind::stepper, "Sound delay");
    delay.current = 150;
    delay.low = -2000;
    delay.high = 2000;
    delay.step = 50;
    model.rows.push_back(delay);
    model.rows.push_back(row(Kind::header, "Subtitles"));
    model.rows.push_back(row(Kind::picker, "Subtitles", "Off"));
    model.rows.push_back(row(Kind::picker, "Subtitle size", "Medium"));
    model.rows.push_back(row(Kind::picker, "Subtitle color", "White"));
    model.rows.push_back(row(Kind::header, "Picture"));
    model.rows.push_back(row(Kind::picker, "Picture", "Fit \xC2\xB7 16:9"));
    model.rows.push_back(row(Kind::header, "Video"));
    model.rows.push_back(row(Kind::picker, "Chapters", "Opening"));
    model.rows.push_back(row(Kind::picker, "Quality", "1920 x 1080 / 6000 kbps"));
    model.selected = 8;
    return model;
}

ptv::SheetModel picture_page()
{
    ptv::SheetModel model;
    model.picker = true;
    model.title = "Picture";
    model.rows.push_back(
        row(Kind::option, "Fit original picture", "16:9", true, Preview::shape, 1778));
    model.rows.push_back(row(Kind::option, "Stretch to screen", "", false, Preview::stretch));
    model.rows.push_back(row(Kind::option, "Correct to 4:3", "", false, Preview::shape, 1333));
    model.rows.push_back(row(Kind::option, "Correct to 16:9", "", false, Preview::shape, 1778));
    model.rows.push_back(
        row(Kind::option, "Fill screen (crop edges)", "", false, Preview::zoom, 0));
    model.rows.push_back(row(Kind::option, "Zoom 125%", "", false, Preview::zoom, 1250));
    model.rows.push_back(row(Kind::option, "Zoom 150%", "", false, Preview::zoom, 1500));
    model.selected = 2;
    return model;
}

ptv::SheetModel colour_page()
{
    ptv::SheetModel model;
    model.picker = true;
    model.title = "Subtitle color";
    for (const auto &[name, rgb] : {std::pair<const char *, std::uint32_t>{"White", 0xffffff},
                                    {"Yellow", 0xffff00},
                                    {"Cyan", 0x00ffff}})
    {
        auto option = row(Kind::option, name, "", rgb == 0xffff00, Preview::swatch);
        option.rgb = rgb;
        model.rows.push_back(option);
    }
    return model;
}

std::uint32_t pixel(const iptv::SubtitleCue &cue, int x, int y)
{
    const auto &bitmap = cue.bitmaps.front();
    if (x < bitmap.x || y < bitmap.y || x >= bitmap.x + static_cast<int>(bitmap.width) ||
        y >= bitmap.y + static_cast<int>(bitmap.height))
        return 0;
    return bitmap.argb[static_cast<std::size_t>(y - bitmap.y) * bitmap.width + (x - bitmap.x)];
}

// With PTV_MENU_PNG set to a folder, the menu over a stand-in picture is written there.
void write_preview(const iptv::SubtitleCue &cue, const char *name)
{
    const char *folder = std::getenv("PTV_MENU_PNG");
    if (folder == nullptr)
        return;
    const int width = 1920, height = 1080;
    std::vector<std::uint8_t> screen(static_cast<std::size_t>(width) * height * 3u);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            // A bright, busy picture: the worst case for legibility.
            std::uint8_t *out = screen.data() + (static_cast<std::size_t>(y) * width + x) * 3u;
            const bool check = ((x / 60) + (y / 60)) % 2 == 0;
            float rgb[3] = {0.35f + 0.5f * static_cast<float>(x) / width, check ? 0.75f : 0.55f,
                            0.85f - 0.5f * static_cast<float>(y) / height};
            const std::uint32_t over = pixel(cue, x, y);
            const float a = static_cast<float>(over >> 24) / 255.0f;
            for (int c = 0; c < 3; ++c)
                rgb[c] = static_cast<float>((over >> (16 - 8 * c)) & 255u) / 255.0f * a +
                         rgb[c] * (1.0f - a);
            for (int c = 0; c < 3; ++c)
                out[c] = static_cast<std::uint8_t>(std::min(rgb[c], 1.0f) * 255.0f + 0.5f);
        }
    const std::string path = std::string(folder) + "/" + name;
    EXPECT_NE(stbi_write_png(path.c_str(), width, height, 3, screen.data(), width * 3), 0);
}

TEST(PlayerSheet, PaintsTheSettingsPanelAtTheRight)
{
    ptv::PlayerSheet sheet(menu_fonts().fonts, {});
    iptv::SubtitleCue cue;
    ASSERT_TRUE(sheet.paint(settings_page(), cue));
    ASSERT_EQ(cue.bitmaps.size(), 1u);
    EXPECT_EQ(cue.canvas_width, 1920u);
    EXPECT_EQ(cue.canvas_height, 1080u);
    const auto &bitmap = cue.bitmaps.front();
    // At the right, inside the screen, with every row on it.
    EXPECT_GT(bitmap.x, 1100);
    EXPECT_GT(bitmap.width, 600u);
    EXPECT_LE(bitmap.x + static_cast<int>(bitmap.width), 1920);
    EXPECT_LE(bitmap.y + static_cast<int>(bitmap.height), 1080);
    EXPECT_EQ(bitmap.argb.size(), static_cast<std::size_t>(bitmap.width) * bitmap.height);
    // The panel is nearly opaque Dusk: dark and red-leaning.
    const hui::gfx::Rect panel = sheet.panel();
    const std::uint32_t inside =
        pixel(cue, static_cast<int>(panel.x + 12.0f), static_cast<int>(panel.y + panel.h * 0.5f));
    EXPECT_GT(inside >> 24, 200u);
    EXPECT_LT((inside >> 16) & 255u, 90u);
    EXPECT_GE((inside >> 16) & 255u, inside & 255u);
    int glyphs = 0;
    for (const hui::gfx::Instance &instance : sheet.recorded().instances())
        glyphs +=
            static_cast<int>(instance.params[3] + 0.5f) == static_cast<int>(hui::gfx::Shape::glyph);
    EXPECT_GT(glyphs, 100);
    write_preview(cue, "player-sheet-settings.png");
}

TEST(PlayerSheet, PaintsTheValuesWithTheirPreviews)
{
    ptv::PlayerSheet sheet(menu_fonts().fonts, {});
    iptv::SubtitleCue cue;
    ASSERT_TRUE(sheet.paint(picture_page(), cue));
    write_preview(cue, "player-sheet-picture.png");
    const float picture_height = sheet.panel().h;
    // Zoom previews clip the spilled picture to their screen.
    int clips = 0;
    for (const auto &run : sheet.recorded().runs())
        clips += run.clipped;
    EXPECT_GE(clips, 3);
    ASSERT_TRUE(sheet.paint(colour_page(), cue));
    write_preview(cue, "player-sheet-colour.png");
    EXPECT_LT(sheet.panel().h, picture_height);
    // The yellow swatch is drawn in yellow.
    bool yellow = false;
    for (const auto value : cue.bitmaps.front().argb)
        yellow |= (value >> 24) == 255u && ((value >> 16) & 255u) > 240u &&
                  ((value >> 8) & 255u) > 240u && (value & 255u) < 20u;
    EXPECT_TRUE(yellow);
}

TEST(PlayerSheet, HighContrastTurnsThePanelBlack)
{
    ptv::Settings settings;
    settings.high_contrast = true;
    ptv::PlayerSheet sheet(menu_fonts().fonts, settings);
    iptv::SubtitleCue cue;
    ASSERT_TRUE(sheet.paint(settings_page(), cue));
    const hui::gfx::Rect panel = sheet.panel();
    const std::uint32_t inside =
        pixel(cue, static_cast<int>(panel.x + 12.0f), static_cast<int>(panel.y + panel.h * 0.5f));
    EXPECT_EQ(inside & 0xffffffu, 0u);
}

TEST(PlayerSheet, PaintsTheControlsWithButtonShapes)
{
    using Button = hui::ui::Button;
    ptv::PlayerSheet sheet(menu_fonts().fonts, {});
    ptv::ControlsModel film;
    film.title = "The Long Voyage Home";
    film.chip = "Paused";
    film.elapsed = "0:42:10";
    film.total = "1:58:03";
    film.progress = 357;
    film.hints = {
        {Button::l2, Button::none, "Resume"},      {Button::dpad, Button::none, "Seek"},
        {Button::l1, Button::r1, "1 minute"},      {Button::r2, Button::none, "Speed"},
        {Button::square, Button::none, "Restart"}, {Button::options, Button::none, "Menu"},
        {Button::circle, Button::none, "Back"}};
    iptv::SubtitleCue cue;
    ASSERT_TRUE(sheet.paint_controls(film, cue));
    write_preview(cue, "player-controls-film.png");
    const auto &bitmap = cue.bitmaps.front();
    // Along the bottom, nearly the whole width.
    EXPECT_GT(bitmap.width, 1700u);
    EXPECT_GT(bitmap.y, 700);
    EXPECT_LE(bitmap.y + static_cast<int>(bitmap.height), 1080);
    // The hints are glyphs, not words for the buttons: shapes are drawn as well as text.
    int shapes = 0;
    for (const hui::gfx::Instance &instance : sheet.recorded().instances())
        shapes +=
            static_cast<int>(instance.params[3] + 0.5f) != static_cast<int>(hui::gfx::Shape::glyph);
    EXPECT_GT(shapes, 20);

    ptv::ControlsModel live;
    live.title = "BBC One";
    live.line = "Now  18:00  The News at Six";
    live.detail = "Next  18:30  Regional News";
    live.chip = "Live";
    live.chip_lit = true;
    live.hints = {{Button::l2, Button::none, "Pause"},
                  {Button::dpad, Button::none, "Seek"},
                  {Button::r2, Button::none, "Back to live"},
                  {Button::cross, Button::none, "Channels"},
                  {Button::square, Button::none, "Last channel"},
                  {Button::touchpad, Button::none, "Record"},
                  {Button::options, Button::none, "Menu"},
                  {Button::circle, Button::none, "Back"}};
    ASSERT_TRUE(sheet.paint_controls(live, cue));
    write_preview(cue, "player-controls-live.png");
    ASSERT_TRUE(sheet.paint_badge("2x", cue));
    write_preview(cue, "player-controls-badge.png");
    EXPECT_GT(cue.bitmaps.front().x, 1700);
}

TEST(PlayerSheet, PaintsTheChannelList)
{
    ptv::PlayerSheet sheet(menu_fonts().fonts, {});
    ptv::SheetModel model;
    model.picker = true;
    model.title = "Channels  12 / 340";
    model.confirm = "Watch";
    model.back = "Close";
    const char *names[][2] = {{"10  BBC One", "The News at Six"},
                              {"11  BBC Two", "Gardeners' World"},
                              {"12  ITV1", "Emmerdale"},
                              {"13  Channel 4", "Countdown"},
                              {"14  Channel 5", ""},
                              {"15  Sky News", "Sky News Tonight"}};
    for (const auto &name : names)
        model.rows.push_back(
            row(Kind::option, name[0], name[1], std::string(name[0]) == "12  ITV1"));
    model.selected = 2;
    iptv::SubtitleCue cue;
    ASSERT_TRUE(sheet.paint(model, cue));
    write_preview(cue, "player-sheet-channels.png");
    EXPECT_GT(cue.bitmaps.front().x, 1100);
}

TEST(PlayerSheet, NothingToPaintWhenEmpty)
{
    ptv::PlayerSheet sheet(menu_fonts().fonts, {});
    iptv::SubtitleCue cue;
    EXPECT_FALSE(sheet.paint({}, cue));
    EXPECT_TRUE(cue.bitmaps.empty());
}

TEST(PlayerSheet, NamesPictureShapes)
{
    EXPECT_EQ(ptv::shape_name(1777), "16:9");
    EXPECT_EQ(ptv::shape_name(1333), "4:3");
    EXPECT_EQ(ptv::shape_name(2400), "2.39:1");
    EXPECT_EQ(ptv::shape_name(1600), "1.60:1");
    EXPECT_EQ(ptv::shape_name(0), "");
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

TEST(PlayerSheet, ProviderRowsUseBothScriptFallbackFonts)
{
    const char *dir = std::getenv("KIT_FONTS");
    hui::gfx::Font asian, korean;
    auto fonts = menu_fonts().fonts;
    MenuFonts::load(dir, "noto-sans-east-asian.huifont", &asian, &fonts.hand, 0xf0000005u);
    MenuFonts::load(dir, "noto-sans-korean.huifont", &korean, &fonts.pixel, 0xf0000006u);
    ptv::PlayerSheet sheet(fonts, {});
    ptv::SheetModel model;
    model.picker = true;
    model.title = "Channels";
    model.rows = {row(Kind::option, "1 中文", "한국어")};
    iptv::SubtitleCue cue;
    ASSERT_TRUE(sheet.paint(model, cue));
    bool has_asian = false, has_korean = false;
    for (const auto &instance : sheet.recorded().instances())
    {
        if (instance.params[3] != static_cast<float>(hui::gfx::Shape::glyph))
            continue;
        EXPECT_GT(instance.params[0], 0.0f); // zero spread turns every glyph into a solid box
        has_asian |= instance.params[2] == 5.0f;
        has_korean |= instance.params[2] == 6.0f;
    }
    EXPECT_TRUE(has_asian);
    EXPECT_TRUE(has_korean);
}
