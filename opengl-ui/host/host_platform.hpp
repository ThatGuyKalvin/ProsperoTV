// ProsperoTV - The PC's stand-ins for the console: keyboard, network, clock.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

namespace host
{

// What the next keyboard request is answered with at the next poll. The
// answer is used once; with none set, a request stays open (as when the
// player has not typed yet) until cancel_keyboard().
void set_keyboard_text(const std::string &text);
void cancel_keyboard();
// How many requests the keyboard has had, and the title of the last one.
int keyboard_requests();
const std::string &keyboard_title();
void set_keyboard_available(bool available);

// The stand-in network: every download answers with this file, after this
// long, or fails when `reachable` is false.
void set_network(bool reachable, const std::string &playlist_path, unsigned delay_ms = 0);
// How many downloads were asked for.
int fetch_count();

// The clock the logic sees (seconds since 1970); 0 is the PC's own clock.
void set_unix_time(std::uint64_t seconds);

// Forgets everything above (between tests).
void reset();

} // namespace host
