// ProsperoTV - Updates on the console: the catalog check and the app replacing itself.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace tv
{

// Asks homebrew.page whether a newer ProsperoTV is listed, on a thread of its
// own; only the first call of a launch does anything. The interface hears the
// answer through ptv::platform::update_take().
void start_update_check();

} // namespace tv
