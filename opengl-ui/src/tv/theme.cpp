// ProsperoTV - Dusk: the app's design language as kit tokens.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/theme.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ptv
{

bool load_menu_font(std::string data, gfx::Font &font, bool larger)
{
    if (larger && data.size() >= sizeof(gfx::font_format::Header))
    {
        gfx::font_format::Header header;
        std::memcpy(&header, data.data(), sizeof(header));
        if (!std::isfinite(header.pixel_size) || header.pixel_size <= 0)
            return false;
        header.pixel_size /= 1.25f;
        std::memcpy(data.data(), &header, sizeof(header));
    }
    return font.load(data);
}

void apply_appearance(Frame &frame, const Settings &settings)
{
    if (!settings.theme && !settings.high_contrast)
        return;
    const auto recolor = [&](float *c)
    {
        if (settings.high_contrast)
        {
            c[0] = c[1] = c[2] = 1;
            if (c[3] > 0)
                c[3] = 1;
        }
        else if (settings.theme == 1)
            std::swap(c[0], c[2]);
        else if (settings.theme == 2)
            std::swap(c[0], c[1]);
    };
    for (auto *list : {&frame.scene, &frame.overlay})
    {
        // DrawList exposes read-only inspection; this frame is ours and mutable.
        auto &instances = const_cast<std::vector<gfx::Instance> &>(list->instances());
        for (const auto &run : list->runs())
        {
            if (run.mesh)
                continue;
            for (unsigned i = run.first; i < run.first + run.count; ++i)
            {
                auto &item = instances[i];
                auto shape = static_cast<gfx::Shape>(static_cast<unsigned>(item.params[3]));
                if (shape == gfx::Shape::image)
                {
                    if (!settings.high_contrast || run.texture != frame.glass_texture)
                        continue;
                    shape = gfx::Shape::rounded_rect;
                    item.params[3] = static_cast<float>(shape);
                    // Image UV coordinates would rotate a rectangle in the batch shader.
                    std::fill(std::begin(item.extra), std::end(item.extra), 0.0f);
                    item.color_top[0] = item.color_top[1] = item.color_top[2] = 0;
                    item.color_bottom[0] = item.color_bottom[1] = item.color_bottom[2] = 0;
                    item.color_top[3] = item.color_bottom[3] = 1;
                }
                if (settings.high_contrast && shape == gfx::Shape::shadow)
                {
                    item.color_top[3] = item.color_bottom[3] = 0;
                    continue;
                }
                recolor(item.color_top);
                recolor(item.color_bottom);
                recolor(item.border_color);
                if (settings.high_contrast &&
                    (shape == gfx::Shape::rounded_rect || shape == gfx::Shape::chamfer) &&
                    item.rect[2] > 6 && item.rect[3] > 6)
                {
                    // Translucent white cards must become black, not hide white labels.
                    item.color_top[0] = item.color_top[1] = item.color_top[2] = 0;
                    item.color_bottom[0] = item.color_bottom[1] = item.color_bottom[2] = 0;
                    if (item.color_top[3] > 0 || item.color_bottom[3] > 0)
                    {
                        item.params[1] = std::max(item.params[1], 1.5f);
                        item.border_color[0] = item.border_color[1] = item.border_color[2] =
                            item.border_color[3] = 1;
                    }
                }
            }
        }
        for (auto &vertex : const_cast<std::vector<gfx::MeshVertex> &>(list->mesh_vertices()))
        {
            float color[] = {vertex.r, vertex.g, vertex.b, vertex.a};
            recolor(color);
            vertex.r = color[0];
            vertex.g = color[1];
            vertex.b = color[2];
            vertex.a = color[3];
        }
    }
    if (settings.high_contrast)
    {
        frame.backdrop.mode = gfx::BackdropMode::gradient;
        for (auto &color : frame.backdrop.colors)
            color = Color::rgb(0);
        frame.glass = false;
    }
    else
        for (auto &color : frame.backdrop.colors)
        {
            float c[] = {color.r, color.g, color.b, color.a};
            recolor(c);
            color = {c[0], c[1], c[2], c[3]};
        }
}

ui::Theme dusk()
{
    // The kit's Acrylic theme is the recipe (glass surfaces, hairlines of
    // light); the palette, the corners and the pace are ProsperoTV's own.
    ui::Theme t = ui::themes()[0];
    t.id = "dusk";
    t.name = "Dusk";
    t.family = "Frosted glass";
    t.summary = "Glass panels over a red sky, cream actions, one orange accent";

    t.backdrop = dusk_sky(tone::ember, tone::wine, 0.0f, 0.0f);
    t.page = tone::night;
    t.surface = tone::panel.with_alpha(0.5f);
    t.surface_high = Color::rgb(0xffffff, 0.1f);
    t.text = tone::cream;
    t.text_muted = tone::cream.with_alpha(0.62f);
    t.primary = tone::cream;
    t.on_primary = tone::ink;
    t.secondary = Color::rgb(0xffffff, 0.1f);
    t.on_secondary = tone::cream;
    t.accent = tone::accent;
    t.outline = Color::rgb(0xffffff, 0.16f);
    t.focus = tone::cream;
    t.shadow = Color::rgb(0x000000, 0.45f);
    t.light = Color::rgb(0xffffff, 0.22f);
    t.danger = tone::bad;
    t.success = tone::good;
    t.warning = tone::wait;

    t.style = ui::SurfaceStyle::glass;
    t.corner = ui::Corner::round;
    t.radius = 16.0f;
    t.radius_card = 24.0f;
    t.border = 1.5f;
    t.pill_chips = true;
    t.shadow_offset = 12.0f;
    t.shadow_blur = 34.0f;
    t.focus_width = 3.0f;
    t.focus_gap = 5.0f;

    t.heading = ui::FontRole::display;
    t.label = ui::FontRole::semibold;
    t.omega = 18.0f;
    t.damping = 1.0f;
    t.sounds = audio::SoundSet::glass;
    t.dark = true;
    return t;
}

gfx::BackdropSpec dusk_sky(Color lean, Color lean_dark, float amount, float time)
{
    gfx::BackdropSpec sky;
    sky.mode = gfx::BackdropMode::aurora;
    sky.colors[0] = Color::rgb(0x240a08);
    sky.colors[1] = Color::rgb(0x100504);
    // The clouds stay dark: they are a mood behind text, not a picture. One
    // is red, the other burnt orange.
    sky.colors[2] = gfx::mix(Color::rgb(0xb8261b), gfx::mix(lean, tone::night, 0.5f), amount);
    sky.colors[3] = gfx::mix(Color::rgb(0xc4521a), gfx::mix(lean_dark, lean, 0.2f), amount);
    sky.time = time;
    return sky;
}

} // namespace ptv
