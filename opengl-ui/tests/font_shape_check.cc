// ProsperoTV - Real baked-font shaping checks, independent of the console.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/font.hpp"
#include "gfx/draw_list.hpp"
#include <hb.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <thread>
static std::atomic<unsigned> shape_calls{0};
extern "C" void __real_hb_shape(hb_font_t *, hb_buffer_t *, const hb_feature_t *, unsigned);
extern "C" void __wrap_hb_shape(hb_font_t *font, hb_buffer_t *buffer, const hb_feature_t *features,
                                unsigned count)
{
    ++shape_calls;
    __real_hb_shape(font, buffer, features, count);
}
using hui::gfx::Align;
using hui::gfx::Font;
using hui::gfx::GlyphQuad;
namespace
{
unsigned checks = 0;
void require(bool value, const char *label)
{
    ++checks;
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}
std::vector<GlyphQuad> glyphs(const Font &font, std::string_view text)
{
    std::vector<GlyphQuad> result;
    font.layout(text, 0, 60, 48, Align::left, result);
    return result;
}
bool same_uv(const GlyphQuad &a, const GlyphQuad &b)
{
    return a.u0 == b.u0 && a.u1 == b.u1 && a.v0 == b.v0 && a.v1 == b.v1;
}
} // namespace
int main(int argc, char **argv)
{
    require(argc == 2, "font argument");
    std::ifstream stream(argv[1], std::ios::binary);
    const std::string data((std::istreambuf_iterator<char>(stream)), {});
    Font font;
    require(font.load(data), "load shaped font");
    for (auto point : {0x5d0u, 0x5b8u, 0x633u, 0x64eu, 0x31u})
        require(font.has_glyph(point), "required glyph coverage");
    const std::string arabic = "\xd8\xb3\xd9\x84\xd8\xa7\xd9\x85"; // salaam
    const std::string hebrew = "\xd7\x90\xd7\x91";                 // alef bet
    require(glyphs(font, arabic).size() == 3, "Arabic joining and lam-alef ligature");
    require(font.measure(arabic, 48, 5) == font.measure(arabic, 48),
            "Arabic joins remain connected under heading tracking");
    const auto pair = glyphs(font, hebrew), alef = glyphs(font, "\xd7\x90"),
               bet = glyphs(font, "\xd7\x91");
    require(pair.size() == 2 && same_uv(pair[0], bet[0]) && same_uv(pair[1], alef[0]),
            "Hebrew visual order");
    require(std::fabs(font.measure("\xd7\xa9\xd6\xb8", 48) - font.measure("\xd7\xa9", 48)) < .1f,
            "Hebrew mark has no advance");
    const std::string mixed = arabic + " HD 123 https://example.com";
    const auto unchanged = mixed;
    require(font.measure(mixed, 48) > 0, "mixed run measure");
    require(mixed == unchanged, "logical provider text unchanged");
    auto fitted = font.fit(arabic + " " + arabic, 48, font.measure(arabic, 48) + 8);
    require(font.measure(fitted, 48) <= font.measure(arabic, 48) + 8, "cluster ellipsis fits");
    for (const auto &line : font.wrap(mixed + " " + mixed, 48, 240))
        require(font.measure(line, 48) <= 241, "wrapped line fits");
    require(font.measure(std::string(100000, 'A'), 48) == font.measure(std::string(4096, 'A'), 48),
            "provider text work is bounded");
    require(std::isfinite(font.measure("\xff\xed\xa0\x80\xf4\x90\x80\x80", 48)),
            "malformed UTF8 bounded");
    auto malformed = data;
    malformed.pop_back();
    Font bad;
    require(!bad.load(malformed), "truncated font rejected");
    auto header = hui::gfx::font_format::Header{};
    std::memcpy(&header, data.data(), sizeof(header));
    malformed = data;
    auto invalid = header;
    invalid.glyph_count = 0xffffffff;
    std::memcpy(malformed.data(), &invalid, sizeof(invalid));
    require(!bad.load(malformed), "overflow dimensions rejected");
    auto larger = data;
    header.pixel_size /= 1.2f;
    std::memcpy(larger.data(), &header, sizeof(header));
    Font large;
    require(large.load(larger), "large-text shaped font");
    require(std::fabs(large.measure(arabic, 48) / font.measure(arabic, 48) - 1.2f) < .001f,
            "large text preserves advance scaling");
    hui::gfx::DrawList list;
    const hui::gfx::Rect box{20, 30, 100, 60};
    list.image(7, box, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff));
    const auto image = list.instances().front();
    list.clear();
    {
        hui::gfx::ScopedRtl direction(list, 1920);
        list.image(7, box, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff));
        const auto flipped = list.instances().front();
        require(flipped.rect[0] == 1800 && flipped.rect[2] == 100, "RTL image layout position");
        require(std::memcmp(image.extra, flipped.extra, sizeof(image.extra)) == 0,
                "video and image UV orientation preserved");
        list.push_clip({10, 10, 150, 100});
        list.push_clip({30, 20, 80, 50});
        list.rounded_rect(box, 0, hui::gfx::Color::rgb(0));
        const auto &clip = list.runs().back().clip;
        require(clip.x == 1810 && clip.y == 20 && clip.w == 80 && clip.h == 50,
                "nested RTL clips intersect in screen space");
        list.pop_clip();
        list.pop_clip();
        list.push_transform(2, 0, 0, 10, 0);
        list.image(7, box, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff));
        require(list.instances().back().rect[0] == 1670, "nested transform precedes RTL layout");
        list.pop_transform();
    }
    require(list.rtl_width() == 0, "RTL scope restores renderer state");
    const std::string trust =
        "0123456789abcdef0123456789abcdef 192.168.4.30:63614 https://host/a?x=12";
    hui::gfx::DrawList ltr, rtl;
    const float trust_width = ltr.text(font, 9, trust, 40, 60, 24, hui::gfx::Color::rgb(0xffffff));
    {
        hui::gfx::ScopedRtl direction(rtl, 1920);
        rtl.text(font, 9, trust, 40, 60, 24, hui::gfx::Color::rgb(0xffffff));
    }
    require(ltr.instances().size() == rtl.instances().size(), "trust text retains glyph count");
    for (std::size_t i = 0; i < ltr.instances().size(); ++i)
    {
        const auto &a = ltr.instances()[i], &b = rtl.instances()[i];
        require(std::fabs(b.rect[0] - (a.rect[0] + 1920 - 80 - trust_width)) < .01f,
                "numeric URL fingerprint glyph order remains LTR");
        require(std::memcmp(a.extra, b.extra, sizeof(a.extra)) == 0, "glyph UVs never mirror");
    }
    // A large catalogue's category row must stay warm while channel tiles change.
    std::vector<std::string> categories;
    std::vector<float> widths;
    for (unsigned i = 0; i < 200; ++i)
    {
        categories.push_back("Entertainment;News;Sports " + std::to_string(i));
        widths.push_back(font.measure(categories.back(), 22));
    }
    for (unsigned frame = 0; frame < 30; ++frame)
    {
        for (unsigned i = 0; i < 40; ++i)
            (void)glyphs(font, "Channel " + std::to_string(frame * 40 + i));
        const auto calls = shape_calls.load();
        for (unsigned i = 0; i < categories.size(); ++i)
            require(font.measure(categories[i], 22) == widths[i], "cached category width");
        require(shape_calls.load() == calls, "category row must not reshape while paging");
    }

    const auto start = std::chrono::steady_clock::now();
    std::thread worker(
        [&]
        {
            for (unsigned i = 0; i < 1000; ++i)
                (void)font.measure(mixed, 32);
        });
    for (unsigned i = 0; i < 1000; ++i)
        (void)glyphs(font, mixed);
    worker.join();
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    std::printf("PASS: %u shaping checks; 2000 cached measures/layouts on two threads: %lld us; "
                "atlas %ux%u.\n",
                checks, static_cast<long long>(us), font.atlas_width(), font.atlas_height());
}
