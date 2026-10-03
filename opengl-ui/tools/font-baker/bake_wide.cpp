// ProsperoTV - Offline SDF font atlas baker for channel names (host tool).
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// usage: bake_wide <font.ttf> <out.huifont> [pixel_size=48] [sdf_range=6] [atlas_width=2048]
//
// The UI kit's baker (tools/font-baker/bake_font.cpp there) with a wider
// alphabet: the kit bakes printable ASCII, which is all its own screens show.
// A channel list is written in the languages of the world, so this one adds
// Latin-1, Latin Extended-A, the Romanian and Baltic letters of Extended-B,
// Greek, Cyrillic and the common typographic marks. Letters a typeface lacks
// are skipped. The file format is the kit's (gfx/font_format.hpp).

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#include "gfx/font_format.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{

namespace ff = hui::gfx::font_format;

struct Baked
{
    int codepoint = 0;
    int w = 0;
    int h = 0;
    int xoff = 0;
    int yoff = 0;
    float advance = 0.0f;
    unsigned char *bitmap = nullptr;
    int x = 0;
    int y = 0;
};

std::vector<int> codepoints()
{
    std::vector<int> result;
    const auto range = [&](int first, int last)
    {
        for (int c = first; c <= last; ++c)
            result.push_back(c);
    };
    range(0x0020, 0x007E); // ASCII
    range(0x00A1, 0x00FF); // Latin-1: Western European letters and signs
    range(0x0100, 0x017F); // Latin Extended-A: Central European, Baltic, Turkish
    range(0x0218, 0x021B); // Romanian s and t with comma below
    range(0x0386, 0x03CE); // Greek, with tonos
    range(0x0400, 0x045F); // Cyrillic: Russian, Ukrainian, Bulgarian, Serbian, Macedonian
    range(0x0490, 0x0491); // Ukrainian ghe with upturn
    range(0x2018, 0x201E); // curly quotes
    // What the kit's own baker adds: dashes, bullet, ellipsis, arrows, a
    // check mark, block and pointing shapes, and the euro sign.
    const int extra[] = {0x2013, 0x2014, 0x2022, 0x2026, 0x20AC, 0x2190, 0x2191, 0x2192,
                         0x2193, 0x2713, 0x2588, 0x25CF, 0x25B2, 0x25B6, 0x25BC, 0x25C0};
    result.insert(result.end(), std::begin(extra), std::end(extra));
    return result;
}

template <typename T> void put(std::vector<unsigned char> &out, const T &value)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr,
                     "usage: %s font.ttf out.huifont [pixel_size] [sdf_range] [atlas_width]\n",
                     argv[0]);
        return 2;
    }
    const float pixel_size = argc > 3 ? std::strtof(argv[3], nullptr) : 48.0f;
    const int range = argc > 4 ? std::atoi(argv[4]) : 6;
    const int atlas_width = argc > 5 ? std::atoi(argv[5]) : 2048;
    constexpr int kMaxHeight = 4096;

    std::ifstream input(argv[1], std::ios::binary);
    std::vector<unsigned char> ttf((std::istreambuf_iterator<char>(input)),
                                   std::istreambuf_iterator<char>());
    stbtt_fontinfo font;
    if (ttf.empty() ||
        !stbtt_InitFont(&font, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
    {
        std::fprintf(stderr, "cannot read font %s\n", argv[1]);
        return 1;
    }
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, pixel_size);

    std::vector<Baked> glyphs;
    for (int codepoint : codepoints())
    {
        if (codepoint != ' ' && stbtt_FindGlyphIndex(&font, codepoint) == 0)
            continue;
        Baked glyph;
        glyph.codepoint = codepoint;
        int advance = 0;
        int bearing = 0;
        stbtt_GetCodepointHMetrics(&font, codepoint, &advance, &bearing);
        glyph.advance = static_cast<float>(advance) * scale;
        glyph.bitmap = stbtt_GetCodepointSDF(&font, scale, codepoint, range, 128,
                                             128.0f / static_cast<float>(range), &glyph.w, &glyph.h,
                                             &glyph.xoff, &glyph.yoff);
        glyphs.push_back(glyph);
    }

    // Shelf packing, tallest first, one pixel of spacing.
    std::vector<Baked *> order;
    for (Baked &glyph : glyphs)
        order.push_back(&glyph);
    std::sort(order.begin(), order.end(),
              [](const Baked *a, const Baked *b) { return a->h > b->h; });
    int pen_x = 1;
    int pen_y = 1;
    int shelf = 0;
    for (Baked *glyph : order)
    {
        if (glyph->bitmap == nullptr)
            continue;
        if (pen_x + glyph->w + 1 > atlas_width)
        {
            pen_x = 1;
            pen_y += shelf + 1;
            shelf = 0;
        }
        if (pen_y + glyph->h + 1 > kMaxHeight)
        {
            std::fprintf(stderr, "the atlas would be taller than %d rows\n", kMaxHeight);
            return 1;
        }
        glyph->x = pen_x;
        glyph->y = pen_y;
        pen_x += glyph->w + 1;
        shelf = std::max(shelf, glyph->h);
    }
    const int atlas_height = pen_y + shelf + 1;
    std::vector<unsigned char> atlas(
        static_cast<std::size_t>(atlas_width) * static_cast<std::size_t>(atlas_height), 0);
    for (const Baked &glyph : glyphs)
    {
        for (int row = 0; glyph.bitmap != nullptr && row < glyph.h; ++row)
            std::memcpy(&atlas[static_cast<std::size_t>((glyph.y + row) * atlas_width + glyph.x)],
                        glyph.bitmap + row * glyph.w, static_cast<std::size_t>(glyph.w));
    }

    // Kerning between the letters of one script: pairs across scripts do not
    // occur in a name, and the table would be mostly those.
    const auto script = [](int codepoint)
    {
        return codepoint < 0x0370 ? 0 : codepoint < 0x0400 ? 1 : 2;
    };
    std::vector<ff::Kern> kerns;
    for (const Baked &a : glyphs)
    {
        for (const Baked &b : glyphs)
        {
            if (script(a.codepoint) != script(b.codepoint) || a.codepoint > 0x04FF ||
                b.codepoint > 0x04FF)
                continue;
            const int kern = stbtt_GetCodepointKernAdvance(&font, a.codepoint, b.codepoint);
            if (kern != 0)
                kerns.push_back(ff::Kern{static_cast<std::uint32_t>(a.codepoint),
                                         static_cast<std::uint32_t>(b.codepoint),
                                         static_cast<float>(kern) * scale});
        }
    }

    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);
    ff::Header header{};
    header.magic = ff::kMagic;
    header.version = ff::kVersion;
    header.atlas_width = static_cast<std::uint16_t>(atlas_width);
    header.atlas_height = static_cast<std::uint16_t>(atlas_height);
    header.pixel_size = pixel_size;
    header.sdf_range = static_cast<float>(range);
    header.ascent = static_cast<float>(ascent) * scale;
    header.descent = static_cast<float>(descent) * scale;
    header.line_gap = static_cast<float>(line_gap) * scale;
    header.glyph_count = static_cast<std::uint32_t>(glyphs.size());
    header.kern_count = static_cast<std::uint32_t>(kerns.size());

    std::vector<unsigned char> out;
    put(out, header);
    std::sort(glyphs.begin(), glyphs.end(),
              [](const Baked &a, const Baked &b) { return a.codepoint < b.codepoint; });
    for (const Baked &glyph : glyphs)
    {
        ff::Glyph record{};
        record.codepoint = static_cast<std::uint32_t>(glyph.codepoint);
        record.x = static_cast<std::uint16_t>(glyph.x);
        record.y = static_cast<std::uint16_t>(glyph.y);
        record.w = static_cast<std::uint16_t>(glyph.w);
        record.h = static_cast<std::uint16_t>(glyph.h);
        record.offset_x = static_cast<float>(glyph.xoff);
        record.offset_y = static_cast<float>(glyph.yoff);
        record.advance = glyph.advance;
        put(out, record);
    }
    std::sort(kerns.begin(), kerns.end(), [](const ff::Kern &a, const ff::Kern &b)
              { return a.first != b.first ? a.first < b.first : a.second < b.second; });
    for (const ff::Kern &kern : kerns)
        put(out, kern);
    out.insert(out.end(), atlas.begin(), atlas.end());

    std::FILE *file = std::fopen(argv[2], "wb");
    if (file == nullptr || std::fwrite(out.data(), 1, out.size(), file) != out.size())
    {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::fclose(file);
    for (Baked &glyph : glyphs)
        stbtt_FreeSDF(glyph.bitmap, nullptr);
    std::printf("%s: %zu glyphs, %zu kerning pairs, atlas %dx%d, %zu bytes\n", argv[2],
                glyphs.size(), kerns.size(), atlas_width, atlas_height, out.size());
    return 0;
}
