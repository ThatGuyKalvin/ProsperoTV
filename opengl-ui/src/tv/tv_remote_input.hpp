// ProsperoTV - The television's remote (HDMI Device Link) in the menu.
// Copyright (C) 2026 ThatGuyKalvin
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/input.hpp"
#include "iptv_tv_remote.h"

#include <cstdint>

namespace ptv
{
// Play acts as OK; Rewind and Fast forward page as L2 and R2.
inline hui::PadSample tv_remote_sample(std::uint32_t word, std::uint64_t now_us)
{
    std::uint32_t buttons = word & IPTV_TV_REMOTE_PAD_BUTTONS;
    if ((word & IPTV_TV_REMOTE_PLAY) != 0)
        buttons |= hui::pad_bits::kCross;
    if ((word & IPTV_TV_REMOTE_REWIND) != 0)
        buttons |= hui::pad_bits::kL2;
    if ((word & IPTV_TV_REMOTE_FAST_FORWARD) != 0)
        buttons |= hui::pad_bits::kR2;
    hui::PadSample sample;
    sample.buttons = buttons;
    sample.connected = true;
    sample.timestamp_us = now_us;
    return sample;
}

// A remote key counts as someone present, even with the controller off.
inline void merge_tv_remote(hui::InputFrame &input, const hui::InputFrame &remote)
{
    input.pressed |= remote.pressed;
    input.released |= remote.released;
    input.held |= remote.held;
    if (remote.nav != hui::Direction::none)
    {
        input.nav = remote.nav;
        input.nav_repeat = remote.nav_repeat;
        input.nav_from_stick = false;
    }
    if (remote.pressed != 0 || remote.held != 0)
    {
        input.connected = true;
        input.focus_lost = false;
    }
}
} // namespace ptv
