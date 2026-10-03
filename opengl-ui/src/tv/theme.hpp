// ProsperoTV - Dusk: the app's design language as kit tokens.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "tv/kit.hpp"

namespace ptv
{

// The colours the screens name directly. Everything else comes from the theme.
namespace tone
{
inline const Color night = Color::rgb(0x190807);     // the page: red all but black
inline const Color panel = Color::rgb(0x2b100d);     // panels and tiles
inline const Color panel_lit = Color::rgb(0x3e1812); // the tile in focus
inline const Color cream = Color::rgb(0xfff3ea);     // text, and the one main action
inline const Color ink = Color::rgb(0x210b07);       // text on cream and on the accent
inline const Color accent = Color::rgb(0xff9445);    // orange: one per region
inline const Color ember = Color::rgb(0xff4f36);     // the light the focus gives off
inline const Color wine = Color::rgb(0x7d2412);      // the dark half of that light
inline const Color good = Color::rgb(0x6fe3ae);
inline const Color bad = Color::rgb(0xff7a7a);
inline const Color wait = Color::rgb(0xf6c35f);
} // namespace tone

// A red sky after sunset, as the kit's frosted-glass recipe.
ui::Theme dusk();

// The sky behind everything. `lean` and `lean_dark` are the colours of the
// channel in focus and `amount` (0..1) how far the clouds take them.
gfx::BackdropSpec dusk_sky(Color lean, Color lean_dark, float amount, float time);

} // namespace ptv
