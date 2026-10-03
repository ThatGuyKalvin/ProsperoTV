// ProsperoTV - Dusk: the app's design language as kit tokens.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tv/theme.hpp"

namespace ptv
{

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
