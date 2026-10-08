// ProsperoTV - The kit's draw lists drawn by the CPU, for where OpenGL is not running.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/soft_raster.hpp"

#include <algorithm>
#include <cmath>

namespace ptv
{

namespace
{

using gfx::Instance;
using gfx::Shape;

struct V2
{
    float x;
    float y;
};

struct V4
{
    float r;
    float g;
    float b;
    float a;
};

float clamp01(float value)
{
    return value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
}

float smoothstep(float edge0, float edge1, float x)
{
    const float t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float dot(V2 a, V2 b)
{
    return a.x * b.x + a.y * b.y;
}

float length(V2 v)
{
    return std::sqrt(v.x * v.x + v.y * v.y);
}

V4 mix(V4 a, V4 b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t};
}

V4 load(const float *v)
{
    return {v[0], v[1], v[2], v[3]};
}

// The shader's distance functions (gfx/gl_batch.cpp).
float round_box(V2 p, V2 b, float r)
{
    const V2 q{std::fabs(p.x) - b.x + r, std::fabs(p.y) - b.y + r};
    return length({std::max(q.x, 0.0f), std::max(q.y, 0.0f)}) + std::min(std::max(q.x, q.y), 0.0f) -
           r;
}

float triangle(V2 p, V2 p0, V2 p1, V2 p2)
{
    const V2 e0{p1.x - p0.x, p1.y - p0.y}, e1{p2.x - p1.x, p2.y - p1.y},
        e2{p0.x - p2.x, p0.y - p2.y};
    const V2 v0{p.x - p0.x, p.y - p0.y}, v1{p.x - p1.x, p.y - p1.y}, v2{p.x - p2.x, p.y - p2.y};
    const float t0 = clamp01(dot(v0, e0) / dot(e0, e0));
    const float t1 = clamp01(dot(v1, e1) / dot(e1, e1));
    const float t2 = clamp01(dot(v2, e2) / dot(e2, e2));
    const V2 q0{v0.x - e0.x * t0, v0.y - e0.y * t0};
    const V2 q1{v1.x - e1.x * t1, v1.y - e1.y * t1};
    const V2 q2{v2.x - e2.x * t2, v2.y - e2.y * t2};
    const float s = (e0.x * e2.y - e0.y * e2.x) < 0.0f ? -1.0f : 1.0f;
    V2 d{dot(q0, q0), s * (v0.x * e0.y - v0.y * e0.x)};
    const V2 d1{dot(q1, q1), s * (v1.x * e1.y - v1.y * e1.x)};
    const V2 d2{dot(q2, q2), s * (v2.x * e2.y - v2.y * e2.x)};
    d = {std::min(d.x, d1.x), std::min(d.y, d1.y)};
    d = {std::min(d.x, d2.x), std::min(d.y, d2.y)};
    return -std::sqrt(d.x) * (d.y > 0.0f ? 1.0f : d.y < 0.0f ? -1.0f : 0.0f);
}

float star5(V2 p, float r, float rf)
{
    const V2 k1{0.809016994375f, -0.587785252292f};
    const V2 k2{-k1.x, k1.y};
    p.x = std::fabs(p.x);
    float m = 2.0f * std::max(dot(k1, p), 0.0f);
    p = {p.x - m * k1.x, p.y - m * k1.y};
    m = 2.0f * std::max(dot(k2, p), 0.0f);
    p = {p.x - m * k2.x, p.y - m * k2.y};
    p.x = std::fabs(p.x);
    p.y -= r;
    const V2 ba{rf * -k1.y - 0.0f, rf * k1.x - 1.0f};
    const float h = std::clamp(dot(p, ba) / dot(ba, ba), 0.0f, r);
    const float side = p.y * ba.x - p.x * ba.y;
    return length({p.x - ba.x * h, p.y - ba.y * h}) * (side > 0.0f   ? 1.0f
                                                       : side < 0.0f ? -1.0f
                                                                     : 0.0f);
}

// A font atlas sampled as GL_LINEAR does: texel centres at half-texels, edges clamped.
float sample(const gfx::Font &font, float u, float v)
{
    const int w = font.atlas_width();
    const int h = font.atlas_height();
    const std::uint8_t *atlas = font.atlas().data();
    const float x = u * static_cast<float>(w) - 0.5f;
    const float y = v * static_cast<float>(h) - 0.5f;
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float tx = x - fx;
    const float ty = y - fy;
    const int x0 = std::clamp(static_cast<int>(fx), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(fy), 0, h - 1);
    const int x1 = std::clamp(static_cast<int>(fx) + 1, 0, w - 1);
    const int y1 = std::clamp(static_cast<int>(fy) + 1, 0, h - 1);
    const float a = atlas[static_cast<std::size_t>(y0) * w + x0];
    const float b = atlas[static_cast<std::size_t>(y0) * w + x1];
    const float c = atlas[static_cast<std::size_t>(y1) * w + x0];
    const float d = atlas[static_cast<std::size_t>(y1) * w + x1];
    return ((a + (b - a) * tx) * (1.0f - ty) + (c + (d - c) * tx) * ty) / 255.0f;
}

// Puts straight-alpha `color` over a premultiplied pixel.
void blend(std::uint8_t *pixel, V4 color)
{
    const float a = clamp01(color.a);
    if (a <= 0.0f)
        return;
    const float keep = 1.0f - a;
    const auto channel = [&](int index, float value)
    {
        const float out = value * 255.0f * a + static_cast<float>(pixel[index]) * keep;
        pixel[index] = static_cast<std::uint8_t>(std::clamp(out + 0.5f, 0.0f, 255.0f));
    };
    channel(0, clamp01(color.r));
    channel(1, clamp01(color.g));
    channel(2, clamp01(color.b));
    const float alpha = 255.0f * a + static_cast<float>(pixel[3]) * keep;
    pixel[3] = static_cast<std::uint8_t>(std::clamp(alpha + 0.5f, 0.0f, 255.0f));
}

struct Clip
{
    int x0;
    int y0;
    int x1;
    int y1;
};

// The colour one instance gives the pixel centred on virtual point p (the fragment shader).
V4 shade(const Instance &in, const gfx::Font *font, V2 p, V2 local, V2 centred, V2 half)
{
    const int shape = static_cast<int>(in.params[3] + 0.5f);
    const V2 size{in.rect[2], in.rect[3]};
    const float t = (shape == 0 && in.extra[1] > 0.5f) ? clamp01(local.x / std::max(size.x, 1e-3f))
                                                       : clamp01(local.y / std::max(size.y, 1e-3f));
    const V4 fill = mix(load(in.color_top), load(in.color_bottom), t);
    V4 color = fill;
    switch (shape)
    {
    case 0:
    case 8:
    {
        float d;
        if (shape == 0)
        {
            const float radius = std::min(in.params[0], std::min(half.x, half.y));
            d = round_box(centred, half, radius);
        }
        else
        {
            const float cut = std::min(in.params[0], std::min(half.x, half.y));
            const V2 q{std::fabs(centred.x) - half.x, std::fabs(centred.y) - half.y};
            const float box = length({std::max(q.x, 0.0f), std::max(q.y, 0.0f)}) +
                              std::min(std::max(q.x, q.y), 0.0f);
            d = std::max(box, (q.x + q.y + cut) * 0.70710678f);
        }
        if (in.params[1] > 0.0f)
        {
            const float inner = clamp01(0.5f - (d + in.params[1]));
            color = mix(load(in.border_color), fill, inner);
        }
        color.a *= clamp01(0.5f - d);
        break;
    }
    case 1:
    {
        if (!font)
            return {0, 0, 0, 0};
        const V2 corner{local.x / std::max(size.x, 1e-6f), local.y / std::max(size.y, 1e-6f)};
        const float u = in.extra[0] + (in.extra[2] - in.extra[0]) * corner.x;
        const float v = in.extra[1] + (in.extra[3] - in.extra[1]) * corner.y;
        const float distance = (sample(*font, u, v) - 0.5f) * 2.0f * in.params[0];
        color.a = fill.a * clamp01(distance + 0.5f);
        break;
    }
    case 2:
    {
        const float radius = std::min(in.params[0], std::min(half.x, half.y));
        const float d = round_box(centred, half, radius);
        color.a = fill.a * (1.0f - smoothstep(-in.params[2], in.params[2], d));
        break;
    }
    case 4:
    {
        const V2 pa{p.x - in.extra[0], p.y - in.extra[1]};
        const V2 ba{in.extra[2] - in.extra[0], in.extra[3] - in.extra[1]};
        const float h = clamp01(dot(pa, ba) / std::max(dot(ba, ba), 1e-6f));
        const float d = length({pa.x - ba.x * h, pa.y - ba.y * h}) - 0.5f * in.params[1];
        color.a = fill.a * clamp01(0.5f - d);
        break;
    }
    case 6:
    {
        float d = star5({centred.x, -centred.y}, std::min(half.x, half.y), 0.45f);
        if (in.params[1] > 0.0f)
            d = std::fabs(d + 0.5f * in.params[1]) - 0.5f * in.params[1];
        color.a = fill.a * clamp01(0.5f - d);
        break;
    }
    case 7:
    {
        const float rb = 0.5f * in.params[1];
        const float ra = std::min(half.x, half.y) - rb;
        const float half_sweep = 0.5f * in.extra[0];
        const float mid = in.params[0] + half_sweep;
        const float cm = std::cos(mid);
        const float sm = std::sin(mid);
        V2 q{centred.x, -centred.y};
        q = {std::fabs(q.x * cm - q.y * sm), q.x * sm + q.y * cm};
        const V2 sc{std::sin(half_sweep), std::cos(half_sweep)};
        const float ring = std::fabs(length(q) - ra);
        float d;
        if (in.extra[1] > 0.5f)
            d = std::max(ring - rb, q.x * sc.y - q.y * sc.x);
        else
            d = ((sc.y * q.x > sc.x * q.y) ? length({q.x - sc.x * ra, q.y - sc.y * ra}) : ring) -
                rb;
        color.a = fill.a * clamp01(0.5f - d);
        break;
    }
    case 5:
    {
        float d = triangle(centred, {0.0f, -half.y}, {-half.x, half.y}, {half.x, half.y});
        if (in.params[1] > 0.0f)
            d = std::fabs(d + 0.5f * in.params[1]) - 0.5f * in.params[1];
        color.a = fill.a * clamp01(0.5f - d);
        break;
    }
    default:
        return {0, 0, 0, 0}; // images and glass need textures this does not have
    }
    return color;
}

void draw_instance(const Instance &in, const gfx::Font *font, const SoftRaster::Target &target,
                   const Clip &clip, SoftRaster::Box &box)
{
    const int shape = static_cast<int>(in.params[3] + 0.5f);
    if (shape == static_cast<int>(Shape::image))
        return;
    const V2 size{in.rect[2], in.rect[3]};
    const V2 half{0.5f * size.x, 0.5f * size.y};
    const V2 centre{in.rect[0] + half.x, in.rect[1] + half.y};
    // The quad the vertex shader makes: grown by the softness and a pixel, maybe turned.
    const float pad = (shape == 1) ? 0.0f : in.params[2] + 1.0f;
    const bool turns = (shape == 0 || shape == 2 || shape == 5) && in.extra[0] != 0.0f;
    const float c = turns ? std::cos(in.extra[0]) : 1.0f;
    const float s = turns ? std::sin(in.extra[0]) : 0.0f;
    float reach_x = half.x + pad;
    float reach_y = half.y + pad;
    if (turns)
    {
        const float rx = std::fabs(c) * reach_x + std::fabs(s) * reach_y;
        const float ry = std::fabs(s) * reach_x + std::fabs(c) * reach_y;
        reach_x = rx;
        reach_y = ry;
    }
    const int x0 = std::max(clip.x0, static_cast<int>(std::floor(centre.x - reach_x)));
    const int y0 = std::max(clip.y0, static_cast<int>(std::floor(centre.y - reach_y)));
    const int x1 = std::min(clip.x1, static_cast<int>(std::ceil(centre.x + reach_x)));
    const int y1 = std::min(clip.y1, static_cast<int>(std::ceil(centre.y + reach_y)));
    if (x1 <= x0 || y1 <= y0)
        return;
    bool drawn = false;
    for (int y = y0; y < y1; ++y)
    {
        std::uint8_t *row =
            target.rgba + static_cast<std::size_t>(y - target.origin_y) * target.stride;
        for (int x = x0; x < x1; ++x)
        {
            const V2 p{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
            V2 centred{p.x - centre.x, p.y - centre.y};
            if (turns)
                centred = {c * centred.x + s * centred.y, -s * centred.x + c * centred.y};
            if (std::fabs(centred.x) > half.x + pad || std::fabs(centred.y) > half.y + pad)
                continue;
            const V2 local{centred.x + half.x, centred.y + half.y};
            const V4 color = shade(in, font, p, local, centred, half);
            if (color.a <= 0.0f)
                continue;
            blend(row + static_cast<std::size_t>(x - target.origin_x) * 4u, color);
            drawn = true;
        }
    }
    if (drawn)
    {
        box.x0 = std::min(box.x0, x0);
        box.y0 = std::min(box.y0, y0);
        box.x1 = std::max(box.x1, x1);
        box.y1 = std::max(box.y1, y1);
    }
}

// A filled triangle with per-vertex colour, without anti-aliasing (as GL draws a mesh).
void draw_triangle(const gfx::MeshVertex &a, const gfx::MeshVertex &b, const gfx::MeshVertex &c,
                   const SoftRaster::Target &, const Clip &clip, std::uint8_t *base,
                   std::size_t stride, int origin_x, int origin_y, SoftRaster::Box &box)
{
    const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(area) < 1e-6f)
        return;
    const int x0 = std::max(clip.x0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int y0 = std::max(clip.y0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int x1 = std::min(clip.x1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int y1 = std::min(clip.y1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    bool drawn = false;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float w0 = ((b.x - px) * (c.y - py) - (b.y - py) * (c.x - px)) / area;
            const float w1 = ((c.x - px) * (a.y - py) - (c.y - py) * (a.x - px)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;
            const V4 color{a.r * w0 + b.r * w1 + c.r * w2, a.g * w0 + b.g * w1 + c.g * w2,
                           a.b * w0 + b.b * w1 + c.b * w2, a.a * w0 + b.a * w1 + c.a * w2};
            blend(base + static_cast<std::size_t>(y - origin_y) * stride +
                      static_cast<std::size_t>(x - origin_x) * 4u,
                  color);
            drawn = true;
        }
    if (drawn)
    {
        box.x0 = std::min(box.x0, x0);
        box.y0 = std::min(box.y0, y0);
        box.x1 = std::max(box.x1, x1);
        box.y1 = std::max(box.y1, y1);
    }
}

} // namespace

void SoftRaster::set_font(unsigned slot, const gfx::Font *font)
{
    if (slot < fonts_.size())
        fonts_[slot] = font;
}

SoftRaster::Box SoftRaster::draw(const gfx::DrawList &list, const Target &target) const
{
    Box box{target.origin_x + target.width, target.origin_y + target.height, target.origin_x,
            target.origin_y};
    if (!target.rgba || target.width <= 0 || target.height <= 0)
        return {};
    const Clip whole{target.origin_x, target.origin_y, target.origin_x + target.width,
                     target.origin_y + target.height};
    const std::vector<Instance> &instances = list.instances();
    const std::vector<gfx::MeshVertex> &mesh = list.mesh_vertices();
    for (const gfx::Run &run : list.runs())
    {
        Clip clip = whole;
        if (run.clipped)
        {
            clip.x0 = std::max(clip.x0, static_cast<int>(std::floor(run.clip.x)));
            clip.y0 = std::max(clip.y0, static_cast<int>(std::floor(run.clip.y)));
            clip.x1 = std::min(clip.x1, static_cast<int>(std::ceil(run.clip.x + run.clip.w)));
            clip.y1 = std::min(clip.y1, static_cast<int>(std::ceil(run.clip.y + run.clip.h)));
            if (clip.x1 <= clip.x0 || clip.y1 <= clip.y0)
                continue;
        }
        if (run.mesh)
        {
            for (std::uint32_t index = run.first;
                 index + 2u < run.first + run.count && index + 2u < mesh.size(); index += 3u)
                draw_triangle(mesh[index], mesh[index + 1u], mesh[index + 2u], target, clip,
                              target.rgba, target.stride, target.origin_x, target.origin_y, box);
            continue;
        }
        // Glyphs sample the font their slot names; anything else bound to the run is a
        // texture this cannot draw.
        const bool textured = run.texture != 0;
        for (std::uint32_t index = run.first;
             index < run.first + run.count && index < instances.size(); ++index)
        {
            const Instance &in = instances[index];
            const int shape = static_cast<int>(in.params[3] + 0.5f);
            const gfx::Font *font = nullptr;
            if (shape == static_cast<int>(Shape::glyph))
            {
                if (textured)
                    continue;
                const unsigned slot = static_cast<unsigned>(in.params[2] + 0.5f);
                font = slot < fonts_.size() ? fonts_[slot] : nullptr;
                if (!font)
                    continue;
            }
            draw_instance(in, font, target, clip, box);
        }
    }
    if (box.x1 <= box.x0 || box.y1 <= box.y0)
        return {};
    return box;
}

} // namespace ptv
