// ProsperoTV - Bounded bidi runs and OpenType shaping for baked glyph-ID fonts.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/font.hpp"
#include <hb.h>
#include <hb-ot.h>
#include <fribidi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
namespace hui::gfx
{
namespace
{
constexpr std::size_t max_text = 16384;
constexpr std::size_t max_points = 4096;
struct Positioned
{
    std::uint32_t key, cluster;
    float x, y, advance;
};
struct Shaped
{
    std::vector<Positioned> glyphs;
    std::vector<std::size_t> boundaries;
    float width = 0;
};
struct Logical
{
    std::vector<FriBidiChar> points;
    std::vector<std::size_t> bytes;
};
Logical decode(std::string_view text)
{
    text = text.substr(0, max_text);
    Logical out;
    std::size_t end = 0;
    for (std::size_t at = 0; at < text.size() && out.points.size() < max_points;)
    {
        const auto start = at;
        auto point = next_codepoint(text, &at);
        const auto length = at - start;
        if ((length == 2 && point < 0x80) || (length == 3 && point < 0x800) ||
            (length == 4 && point < 0x10000) || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff))
            point = 0xfffd;
        out.bytes.push_back(start);
        out.points.push_back(point);
        end = at;
    }
    out.bytes.push_back(end);
    return out;
}
bool rtl_script(hb_script_t script)
{
    return script == HB_SCRIPT_ARABIC || script == HB_SCRIPT_HEBREW;
}
} // namespace
class FontShaper
{
  public:
    std::array<hb_font_t *, 2> fonts{};
    ~FontShaper()
    {
        for (auto *font : fonts)
            if (font)
                hb_font_destroy(font);
    }
    Shaped shape(std::string_view text, float tracking) const
    {
        text = text.substr(0, max_text);
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &entry : cache_)
            if (entry.text == text && entry.tracking == tracking)
                return entry.shaped;
        auto shaped = uncached(text, tracking);
        while (!cache_.empty() && (cache_.size() >= 64 || bytes_ + text.size() > 65536 ||
                                   glyphs_ + shaped.glyphs.size() > 32768))
        {
            bytes_ -= cache_.front().text.size();
            glyphs_ -= cache_.front().shaped.glyphs.size();
            cache_.erase(cache_.begin());
        }
        bytes_ += text.size();
        glyphs_ += shaped.glyphs.size();
        cache_.push_back({std::string(text), tracking, shaped});
        return shaped;
    }

  private:
    struct Cached
    {
        std::string text;
        float tracking;
        Shaped shaped;
    };
    mutable std::mutex mutex_;
    mutable std::vector<Cached> cache_;
    mutable std::size_t bytes_ = 0, glyphs_ = 0;
    Shaped uncached(std::string_view text, float tracking) const
    {
        Shaped out;
        const auto logical = decode(text);
        const auto &points = logical.points;
        const auto n = static_cast<FriBidiStrIndex>(points.size());
        if (!n)
            return out;
        std::vector<FriBidiCharType> types(n);
        std::vector<FriBidiBracketType> brackets(n);
        std::vector<FriBidiLevel> levels(n);
        std::vector<FriBidiStrIndex> visual(n);
        fribidi_get_bidi_types(points.data(), n, types.data());
        fribidi_get_bracket_types(points.data(), n, types.data(), brackets.data());
        FriBidiParType base = FRIBIDI_PAR_ON;
        if (!fribidi_get_par_embedding_levels_ex(types.data(), brackets.data(), n, &base,
                                                 levels.data()))
            return out;
        for (FriBidiStrIndex i = 0; i < n; ++i)
            visual[i] = i;
        if (!fribidi_reorder_line(FRIBIDI_FLAGS_DEFAULT, types.data(), n, 0, base, levels.data(),
                                  nullptr, visual.data()))
            return out;
        std::vector<unsigned> rank(n);
        for (FriBidiStrIndex i = 0; i < n; ++i)
            rank[visual[i]] = static_cast<unsigned>(i);
        struct Run
        {
            unsigned begin, end, face, rank;
            FriBidiLevel level;
            hb_script_t script;
        };
        std::vector<Run> runs;
        hb_script_t prior = HB_SCRIPT_LATIN;
        for (unsigned i = 0; i < points.size(); ++i)
        {
            auto script = hb_unicode_script(hb_unicode_funcs_get_default(), points[i]);
            if (script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED ||
                script == HB_SCRIPT_UNKNOWN)
                script = prior;
            prior = script;
            unsigned face = rtl_script(script) ? 1u : 0u;
            hb_codepoint_t glyph = 0;
            if (!hb_font_get_nominal_glyph(fonts[face], points[i], &glyph) &&
                hb_font_get_nominal_glyph(fonts[1 - face], points[i], &glyph))
                face = 1 - face;
            if (runs.empty() || runs.back().face != face || runs.back().level != levels[i] ||
                runs.back().script != script)
                runs.push_back({i, i + 1, face, rank[i], levels[i], script});
            else
            {
                runs.back().end = i + 1;
                runs.back().rank = std::min(runs.back().rank, rank[i]);
            }
        }
        std::stable_sort(runs.begin(), runs.end(),
                         [](const Run &a, const Run &b) { return a.rank < b.rank; });
        for (const auto &run : runs)
        {
            hb_buffer_t *buffer = hb_buffer_create();
            hb_buffer_set_direction(buffer, (run.level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            hb_buffer_set_script(buffer, run.script);
            hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
            hb_buffer_add_utf32(buffer, points.data(), n, run.begin,
                                static_cast<int>(run.end - run.begin));
            hb_shape(fonts[run.face], buffer, nullptr, 0);
            unsigned count = 0;
            const auto *info = hb_buffer_get_glyph_infos(buffer, &count);
            const auto *position = hb_buffer_get_glyph_positions(buffer, nullptr);
            if (hb_buffer_allocation_successful(buffer) && count <= max_points * 4 &&
                out.glyphs.size() + count <= max_points * 4)
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto cluster =
                        std::min<std::size_t>(info[i].cluster, logical.bytes.size() - 1);
                    const float advance = static_cast<float>(position[i].x_advance) / 64.0f;
                    out.glyphs.push_back(
                        {(run.face << 16) | info[i].codepoint,
                         static_cast<std::uint32_t>(logical.bytes[cluster]),
                         out.width + static_cast<float>(position[i].x_offset) / 64.0f,
                         -static_cast<float>(position[i].y_offset) / 64.0f, advance});
                    out.boundaries.push_back(logical.bytes[cluster]);
                    out.width += advance;
                    // Tracking belongs between grapheme clusters, never between a base and its
                    // marks.
                    if (run.script != HB_SCRIPT_ARABIC && i + 1 < count &&
                        info[i].cluster != info[i + 1].cluster)
                        out.width += tracking;
                }
            hb_buffer_destroy(buffer);
        }
        out.boundaries.push_back(logical.bytes.back());
        std::sort(out.boundaries.begin(), out.boundaries.end());
        out.boundaries.erase(std::unique(out.boundaries.begin(), out.boundaries.end()),
                             out.boundaries.end());
        return out;
    }
};
bool Font::load_shaped(std::string_view data)
{
    const auto header = header_;
    const std::size_t atlas_bytes = std::size_t(header.atlas_width) * header.atlas_height;
    const std::size_t prefix =
        sizeof(header) + std::size_t(header.glyph_count) * sizeof(font_format::Glyph) + atlas_bytes;
    if (header.kern_count || header.glyph_count > 131072 || atlas_bytes > 64u * 1024u * 1024u ||
        !header.atlas_width || !header.atlas_height || !std::isfinite(header.pixel_size) ||
        header.pixel_size <= 0 || header.pixel_size > 256 || !std::isfinite(header.ascent) ||
        !std::isfinite(header.descent) || !std::isfinite(header.line_gap) ||
        !std::isfinite(header.sdf_range) || header.sdf_range <= 0 || prefix > data.size() ||
        data.size() - prefix < 4)
    {
        error_ = "invalid shaped font dimensions";
        return false;
    }
    std::size_t offset = prefix;
    const auto word = [&](std::uint32_t &value)
    {
        if (data.size() - offset < 4)
            return false;
        std::memcpy(&value, data.data() + offset, 4);
        offset += 4;
        return true;
    };
    std::uint32_t count = 0;
    if (!word(count) || count != 2)
    {
        error_ = "invalid shaping faces";
        return false;
    }
    std::uint32_t baked_bits = 0;
    float baked_size = 0;
    if (!word(baked_bits))
    {
        error_ = "missing shaping size";
        return false;
    }
    std::memcpy(&baked_size, &baked_bits, 4);
    if (!std::isfinite(baked_size) || baked_size <= 0 || baked_size > 256)
    {
        error_ = "invalid shaping size";
        return false;
    }
    auto shaping = std::make_shared<FontShaper>();
    for (unsigned i = 0; i < count; ++i)
    {
        std::uint32_t size = 0;
        if (!word(size) || !size || size > 4u * 1024u * 1024u || size > data.size() - offset)
        {
            error_ = "invalid shaping face length";
            return false;
        }
        auto *blob =
            hb_blob_create(data.data() + offset, size, HB_MEMORY_MODE_DUPLICATE, nullptr, nullptr);
        auto *face = hb_face_create(blob, 0);
        if (!hb_face_get_glyph_count(face) || hb_face_get_glyph_count(face) > 65535)
        {
            hb_face_destroy(face);
            hb_blob_destroy(blob);
            error_ = "invalid OpenType face";
            return false;
        }
        auto *font = hb_font_create(face);
        hb_ot_font_set_funcs(font);
        hb_font_set_scale(font, static_cast<int>(baked_size * 64),
                          static_cast<int>(baked_size * 64));
        hb_font_make_immutable(font);
        shaping->fonts[i] = font;
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        offset += size;
    }
    if (offset != data.size())
    {
        error_ = "trailing shaped font data";
        return false;
    }
    std::string legacy(data.substr(0, prefix));
    auto first = header;
    first.version = font_format::kVersion;
    std::memcpy(legacy.data(), &first, sizeof(first));
    if (!load(legacy))
        return false;
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < glyphs_.size(); ++i)
    {
        const auto &glyph = glyphs_[i];
        if ((i && glyph.codepoint <= previous) || (glyph.codepoint >> 16) >= 2 ||
            (glyph.codepoint & 65535u) >=
                hb_face_get_glyph_count(hb_font_get_face(shaping->fonts[glyph.codepoint >> 16])) ||
            !std::isfinite(glyph.offset_x) || !std::isfinite(glyph.offset_y) ||
            !std::isfinite(glyph.advance))
        {
            error_ = "invalid shaped glyph";
            return false;
        }
        previous = glyph.codepoint;
    }
    shaping_ = std::move(shaping);
    return true;
}
bool Font::shaped_has_glyph(std::uint32_t codepoint) const
{
    hb_codepoint_t glyph = 0;
    return hb_font_get_nominal_glyph(shaping_->fonts[0], codepoint, &glyph) ||
           hb_font_get_nominal_glyph(shaping_->fonts[1], codepoint, &glyph);
}
float Font::shaped_layout(std::string_view text, float x, float y, float size, Align align,
                          std::vector<GlyphQuad> *quads, float tracking) const
{
    if (!std::isfinite(size) || size <= 0 || !std::isfinite(tracking))
        return 0;
    const float scale = size / header_.pixel_size;
    const auto shaped = shaping_->shape(text, tracking / scale);
    const float width = shaped.width * scale;
    if (!quads)
        return width;
    if (align == Align::center)
        x -= width * .5f;
    if (align == Align::right)
        x -= width;
    for (const auto &position : shaped.glyphs)
    {
        const auto *glyph = find(position.key);
        if (!glyph || !glyph->w || !glyph->h)
            continue;
        const float left = x + (position.x + glyph->offset_x) * scale;
        const float top = y + (position.y + glyph->offset_y) * scale;
        quads->push_back({left, top, left + glyph->w * scale, top + glyph->h * scale,
                          float(glyph->x) / header_.atlas_width,
                          float(glyph->y) / header_.atlas_height,
                          float(glyph->x + glyph->w) / header_.atlas_width,
                          float(glyph->y + glyph->h) / header_.atlas_height});
    }
    return width;
}
std::string Font::shaped_fit(std::string_view text, float size, float width, float tracking) const
{
    const auto shaped = shaping_->shape(text, 0);
    if (shaped.boundaries.empty())
        return {};
    text = text.substr(0, shaped.boundaries.back());
    if (measure(text, size, tracking) <= width)
        return std::string(text);
    const std::string mark = "\xe2\x80\xa6";
    if (measure(mark, size, tracking) > width)
        return {};
    std::size_t low = 0, high = shaped.boundaries.size();
    std::string best = mark;
    while (low < high)
    {
        const auto middle = low + (high - low) / 2;
        std::string candidate(text.substr(0, shaped.boundaries[middle]));
        candidate += mark;
        if (measure(candidate, size, tracking) <= width)
        {
            best = std::move(candidate);
            low = middle + 1;
        }
        else
            high = middle;
    }
    return best;
}
std::vector<std::string> Font::shaped_wrap(std::string_view text, float size, float width) const
{
    const auto bounded = decode(text);
    text = text.substr(0, bounded.bytes.back());
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size() && lines.size() < max_points)
    {
        const auto newline = text.find('\n', at);
        auto paragraph =
            text.substr(at, newline == std::string_view::npos ? text.size() - at : newline - at);
        const auto logical = decode(paragraph);
        std::vector<FriBidiCharType> types(logical.points.size());
        fribidi_get_bidi_types(logical.points.data(), static_cast<FriBidiStrIndex>(types.size()),
                               types.data());
        const bool rtl =
            fribidi_get_par_direction(types.data(), static_cast<FriBidiStrIndex>(types.size())) ==
            FRIBIDI_PAR_RTL;
        const std::string prefix = rtl ? "\xe2\x80\x8f" : "";
        if (paragraph.empty())
            lines.emplace_back();
        while (!paragraph.empty())
        {
            if (measure(paragraph, size) <= width)
            {
                lines.push_back(prefix + std::string(paragraph));
                break;
            }
            const auto shaped = shaping_->shape(paragraph, 0);
            std::size_t end = 0, low = 0, high = shaped.boundaries.size();
            while (low < high)
            {
                const auto middle = low + (high - low) / 2;
                if (measure(paragraph.substr(0, shaped.boundaries[middle]), size) <= width)
                {
                    end = shaped.boundaries[middle];
                    low = middle + 1;
                }
                else
                    high = middle;
            }
            if (end)
            {
                const auto space = paragraph.substr(0, end + 1).rfind(' ');
                if (space != std::string_view::npos && space > 0)
                    end = space;
            }
            if (!end)
                end = shaped.boundaries.size() > 1 ? shaped.boundaries[1] : paragraph.size();
            lines.push_back(prefix + std::string(paragraph.substr(0, end)));
            paragraph.remove_prefix(std::min(end, paragraph.size()));
            while (!paragraph.empty() && paragraph.front() == ' ')
                paragraph.remove_prefix(1);
        }
        if (newline == std::string_view::npos)
            break;
        at = newline + 1;
    }
    return lines;
}
} // namespace hui::gfx
